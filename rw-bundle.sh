#!/bin/bash
#
# rw-bundle.sh — the bundle layout that F9 publishes and F10 installs.
#
# SOURCED, not executed:   . "$REPO_ROOT/rw-bundle.sh"
#
# ── Layout ──────────────────────────────────────────────────────────────────
#
#   <dir>/root/<device-path>             the file, at its device path minus the
#                                        leading /  — so root/opt/games/snake is
#                                        installed to /opt/games/snake
#   <dir>/manifest.d/<component>.list    "<mode> <device-path>", one per line
#   <dir>/manifest.d/<component>.md5     "<md5>  <device-path>", one per line
#   <dir>/manifest.d/bundle.info         tag, build date, component list
#   <dir>/NOTICE                         licence obligations (ScummVM is GPLv2+)
#
# ── ⚠️ Modes are DECLARED, never read off disk ───────────────────────────────
#
# This repo lives on /mnt/c, which is DrvFs 9p: it reports every file
# -rwxrwxrwx and silently discards chmod (CLAUDE.md → "Working from this host").
# So `stat -c %a` here is a constant, not a measurement, and a bundle that
# derived its modes from disk would declare 777 for everything — then the
# installer would either apply that (wrong) or ignore it (and ship a launcher
# binary with no +x, which is the one failure that cannot be reproduced on this
# host at all).  Hence: the caller states the mode, and .list is the authority.
#
# ── Why md5 is a separate file from .list ────────────────────────────────────
#
# F9's caveat: base/version.o re-embeds the build date on every link, so releases
# are not byte-reproducible and the md5 list must be GENERATED per release rather
# than asserted against a known-good set.  Keeping it out of .list means the
# declared modes — which ARE stable — can be diffed between releases without the
# checksums making every line differ.

# ---------------------------------------------------------------------------
# rw_bundle_init DIR COMPONENT
#
# Prepare DIR and start (or restart) COMPONENT's manifests.  Truncating here
# rather than appending is deliberate: a re-run of a component's --bundle must
# not leave the previous run's entries behind, or a binary that was renamed stays
# in the manifest and the installer fails verification on a file nobody ships.
# ---------------------------------------------------------------------------
rw_bundle_init() {
    local dir="$1" comp="$2"
    [ -n "$dir" ]  || { echo "rw_bundle_init: no directory" >&2; return 1; }
    [ -n "$comp" ] || { echo "rw_bundle_init: no component name" >&2; return 1; }
    mkdir -p "$dir/root" "$dir/manifest.d" || return 1
    : > "$dir/manifest.d/$comp.list"
    : > "$dir/manifest.d/$comp.md5"
}

# ---------------------------------------------------------------------------
# rw_bundle_add DIR COMPONENT MODE LOCAL_FILE DEVICE_PATH
#
# Stage one file.  Refuses rather than warns on every input it cannot make
# correct, because a bundle that is silently missing a file installs cleanly and
# leaves a device with a launcher tile that does nothing.
# ---------------------------------------------------------------------------
rw_bundle_add() {
    local dir="$1" comp="$2" mode="$3" src="$4" dev="$5" dest

    [ -f "$src" ] || { echo "rw_bundle_add: no such file: $src" >&2; return 1; }

    # A 3- or 4-digit octal mode. Anything else is a typo, and a typo here is
    # applied verbatim by the installer's chmod.
    case "$mode" in
        [0-7][0-7][0-7]|[0-7][0-7][0-7][0-7]) ;;
        *) echo "rw_bundle_add: '$mode' is not a 3- or 4-digit octal mode" >&2; return 1 ;;
    esac

    # The device path must be absolute and must not be able to escape root/.
    # `..` is the whole reason this is checked: root/opt/../../etc/shadow is a
    # perfectly ordinary-looking manifest line that writes outside the tree.
    case "$dev" in
        /*) ;;
        *)  echo "rw_bundle_add: device path must be absolute: $dev" >&2; return 1 ;;
    esac
    case "$dev" in
        *//*|*/./*|*/../*|*/..) echo "rw_bundle_add: unsafe device path: $dev" >&2; return 1 ;;
    esac

    dest="$dir/root${dev}"
    mkdir -p "$(dirname "$dest")" || return 1
    cp "$src" "$dest" || return 1

    printf '%s %s\n' "$mode" "$dev" >> "$dir/manifest.d/$comp.list"
    printf '%s  %s\n' "$(md5sum "$src" | cut -d' ' -f1)" "$dev" >> "$dir/manifest.d/$comp.md5"
}

# ---------------------------------------------------------------------------
# rw_bundle_finish DIR COMPONENT
#
# Sort both manifests and echo the file count.  Sorted so that two bundles built
# from the same tree differ only where the artifacts differ — an unsorted list
# reorders whenever a glob's directory order changes, which makes every diff
# useless.
# ---------------------------------------------------------------------------
rw_bundle_finish() {
    local dir="$1" comp="$2" f
    for f in "$dir/manifest.d/$comp.list" "$dir/manifest.d/$comp.md5"; do
        [ -f "$f" ] || continue
        LC_ALL=C sort -k2 -o "$f" "$f"
    done
    grep -c . "$dir/manifest.d/$comp.list" 2>/dev/null || echo 0
}

# ---------------------------------------------------------------------------
# rw_bundle_components DIR
#
# Echo the component names present in DIR, one per line.
# ---------------------------------------------------------------------------
rw_bundle_components() {
    local dir="$1" f
    for f in "$dir"/manifest.d/*.list; do
        [ -f "$f" ] || continue
        basename "$f" .list
    done
}

# ---------------------------------------------------------------------------
# rw_bundle_entries DIR
#
# Echo "<mode> <device-path>" for every component, one per line, sorted.
# The installer's single source of what to write and with which mode.
# ---------------------------------------------------------------------------
rw_bundle_entries() {
    local dir="$1"
    cat "$dir"/manifest.d/*.list 2>/dev/null | grep . | LC_ALL=C sort -k2
}

# ---------------------------------------------------------------------------
# rw_bundle_check DIR
#
# Structural check, run before any install and at the end of every stage: every
# manifest line names a file that is actually under root/, and every file under
# root/ is named by some manifest line.  Both directions matter — the first
# catches a manifest entry whose cp failed, the second catches a file staged by
# hand that nothing will chmod.
#
# Echoes each problem; returns 1 if there were any.
# ---------------------------------------------------------------------------
rw_bundle_check() {
    local dir="$1" bad=0 mode dev f rel

    [ -d "$dir/root" ]       || { echo "  missing $dir/root"; return 1; }
    [ -d "$dir/manifest.d" ] || { echo "  missing $dir/manifest.d"; return 1; }

    while read -r mode dev; do
        [ -n "$dev" ] || continue
        if [ ! -f "$dir/root$dev" ]; then
            echo "  manifest names a file that is not staged: $dev"
            bad=$((bad + 1))
        fi
    done <<EOF
$(rw_bundle_entries "$dir")
EOF

    # The reverse direction. Built as a here-doc-fed loop rather than a pipe so
    # `bad` survives (a `find | while` body runs in a subshell).
    local manifest_paths
    manifest_paths=$(rw_bundle_entries "$dir" | awk '{print $2}')
    while read -r f; do
        [ -n "$f" ] || continue
        rel="${f#$dir/root}"
        if ! printf '%s\n' "$manifest_paths" | grep -qxF "$rel"; then
            echo "  staged but in no manifest: $rel"
            bad=$((bad + 1))
        fi
    done <<EOF
$(find "$dir/root" -type f 2>/dev/null)
EOF

    [ "$bad" -eq 0 ]
}
