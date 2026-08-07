#!/bin/bash
#
# lib/rw-clean.sh — read device-files/clean-rules.conf, compile it into a plan, and
#               apply that plan to a card mounted OFFLINE.
#
# SOURCED, not executed:   . "$REPO_ROOT/lib/rw-clean.sh"
#                          (needs lib/rw-identify.sh sourced first)
#
# IMPROVEMENT_PLAN.md F10, step 3.
#
# ── The decisions are not in this file ──────────────────────────────────────
#
# Every keep and every delete lives in device-files/clean-rules.conf, with a
# reason per entry, read by BOTH consumers:
#
#   commissioning/provision.sh --deep-clean   live, over SSH, base = /
#   commissioning/commission-offline.sh       offline, card in a reader, base = a mount point
#
# so the two cannot drift.  What each consumer keeps is its own EXECUTOR, because
# "/" is the correct prefix on a device and a refused one offline.  This file is
# the offline executor and the shared parser; commissioning/provision.sh ships the compiled
# plan to the device and interprets it there with no prefix at all.
#
# ── The plan format, which is the interface between the two ─────────────────
#
# Tab-separated, one action per line, emitted in this order:
#
#   keep	<dir>	<name-glob>     <dir>/<name> survives every sweep
#   sweep	<dir>                   delete every direct child of <dir> that no keep covers
#   del	<path>                          delete <path> (globs allowed in the last component)
#   truncate	<path>              empty <path> in place, do not unlink it
#
# Keeps come first so a single-pass interpreter works.  Paths are DEVICE-absolute
# in the plan; mapping them onto p2/p3/p5/p6 is the offline executor's job, via
# rw_clean_offline_path.
#
# ── ⚠️ The guard is the point of this file ──────────────────────────────────
#
# Those rules resolve, unprefixed, to /opt, /etc/rc5.d and /usr/lib — which on
# the dev host is /opt, /etc/rc5.d and /usr/lib.  `rm -rf` over that list with an
# empty prefix is catastrophic and silent.  So rw_clean_del refuses an empty or
# "/" base before it looks at anything else, and every deletion — including every
# one a sweep decides on — goes through it.  tests/rw_clean_test.sh group A is
# that guard, and it was written before the guard existed and seen failing.

RW_CLEAN_TYPES="scope keep delete truncate"

# Every group name the file may use.  `base` is mandatory and always enabled.
RW_CLEAN_GROUPS_ALL="base browser java snmp mail extras factory sweeps"

# ── Enabled unless the caller says otherwise: ALL of them ────────────────────
#
# ⚠️ `factory` is in here, and that is the 2026-08-06 reversal of an earlier
# opt-in.  Choosing to clean a unit of its vendor software is a DECISION, and it
# is not meant to be reversible by a button press.  The clean disables the
# factory-reset mechanism anyway, so the 472 MB payload that survives it retains
# only the ability to undo a commissioning it can no longer actually perform.
# The gate is the host-side full-card backup, which is asked for once and up
# front — a strictly better recovery path than the on-device one.
# `--keep-factory` is the deliberate opt-out.  IMPROVEMENT_PLAN.md C11.
RW_CLEAN_GROUPS_DEFAULT="base browser java snmp mail extras factory sweeps"

# The ones --keep-<name> can switch off.  `base` is not among them.
RW_CLEAN_GROUPS_OPTIONAL="browser java snmp mail extras factory sweeps"

# ── What --remove is ────────────────────────────────────────────────────────
#
# A NAMED GROUP SUBSET of the same plan, not a second implementation: everything
# except the whitelist sweeps.  So --remove means "delete the vendor stacks we
# have named" and --deep-clean means "and anything the whitelist does not name",
# which is exactly the difference the two flags always claimed to have — it just
# used to be spelled as ~85 lines of hardcoded `rm -rf` in an ssh heredoc.
#
# It is identical to `--deep-clean --keep-sweeps`, and that is the whole
# implementation.  Note that it does NOT soften the factory default: one default
# across every flag, per above.
RW_CLEAN_GROUPS_REMOVE="base browser java snmp mail extras factory"

rw_clean_default_groups()  { echo "$RW_CLEAN_GROUPS_DEFAULT"; }
rw_clean_optional_groups() { echo "$RW_CLEAN_GROUPS_OPTIONAL"; }
rw_clean_remove_groups()   { echo "$RW_CLEAN_GROUPS_REMOVE"; }

# ---------------------------------------------------------------------------
# rw_clean_rules_file
#
# Echo the shipped rules file.  Resolved from this file's own location — one level
# up, because this library lives in lib/ — so that a caller in a subdirectory, or
# one invoked through a symlink, finds it.
# ---------------------------------------------------------------------------
rw_clean_rules_file() {
    local d
    d=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)
    echo "$d/device-files/clean-rules.conf"
}

# ---------------------------------------------------------------------------
# rw_clean_parse FILE
#
# Echo "<type>\t<group>\t<path>" for every record, dropping the reason.
#
# A tab inside the reason must not shift the first three fields, so the reason is
# "field 4 onwards" rather than "field 4".
# ---------------------------------------------------------------------------
rw_clean_parse() {
    [ -f "$1" ] || { echo "rw_clean_parse: no such file: $1" >&2; return 1; }
    awk -F'\t' '
        /^[ \t]*#/ { next }
        /^[ \t]*$/ { next }
        NF >= 4    { printf "%s\t%s\t%s\n", $1, $2, $3 }
    ' "$1"
}

# ---------------------------------------------------------------------------
# rw_clean_validate FILE
#
# Echo every problem; return 1 if there were any.  Every check here is a thing
# that would otherwise fail silently and in the permissive direction.
# ---------------------------------------------------------------------------
rw_clean_validate() {
    local file="$1" out
    [ -f "$file" ] || { echo "  no such file: $file"; return 1; }

    out=$(awk -F'\t' \
        -v types="$RW_CLEAN_TYPES" -v groups="$RW_CLEAN_GROUPS_ALL" '
        BEGIN {
            n = split(types, t, " ");  for (i = 1; i <= n; i++) TYPE[t[i]] = 1
            n = split(groups, g, " "); for (i = 1; i <= n; i++) GROUP[g[i]] = 1
        }
        /^[ \t]*#/ { next }
        /^[ \t]*$/ { next }
        {
            records++
            if (NF < 4) {
                # Also catches a space-separated line, which arrives as NF == 1.
                printf "  line %d: %d tab-separated field(s), need 4 (<type> <group> <path> <reason>): %s\n", NR, NF, $0
                bad++; next
            }
            if ($1 == "" || $2 == "" || $3 == "" || $4 == "") {
                printf "  line %d: an empty field\n", NR; bad++; next
            }
            if (!($1 in TYPE))  { printf "  line %d: unknown record type \"%s\"\n", NR, $1; bad++ }
            if (!($2 in GROUP)) { printf "  line %d: unknown group \"%s\"\n", NR, $2; bad++ }
            if ($3 !~ /^\//)    { printf "  line %d: path is not absolute: %s\n", NR, $3; bad++ }
            if ($3 ~ /(^|\/)\.\.(\/|$)/) { printf "  line %d: path contains \"..\": %s\n", NR, $3; bad++ }
            if ($3 ~ /\/\//)    { printf "  line %d: path contains \"//\": %s\n", NR, $3; bad++ }
            if ($3 ~ /\/$/ && $3 != "/") { printf "  line %d: path has a trailing slash: %s\n", NR, $3; bad++ }
            if ($3 == "/")      { printf "  line %d: path is \"/\"\n", NR; bad++ }

            # A glob may appear only in the LAST component.  rw_clean_del quotes
            # the directory part so that a base containing a space still works,
            # which means a glob in the middle would be taken literally and the
            # rule would silently do nothing.
            dir = $3; sub(/\/[^\/]*$/, "", dir)
            if (dir ~ /[*?[]/) {
                printf "  line %d: a glob is only allowed in the last component: %s\n", NR, $3
                bad++
            }

            # ⚠️ rc0.d and rc6.d are SHUTDOWN, not startup.  Unreachable through
            # this file by construction, the same guarantee as p1s absence from
            # RW_PART_ROLES: they carry umountfs, sendsigs and save-rtc.sh, and a
            # unit that cannot unmount cleanly is a unit whose next fsck is not
            # optional.
            if ($3 ~ /\/rc[06]\.d(\/|$)/) {
                printf "  line %d: rc0.d and rc6.d are shutdown, not startup — no rule may name them: %s\n", NR, $3
                bad++
            }
        }
        END {
            if (records == 0) print "  no records at all — every line is a comment or blank"
            if (records == 0) bad++
            exit(bad > 0 ? 1 : 0)
        }
    ' "$file")

    if [ -n "$out" ]; then
        printf '%s\n' "$out"
        return 1
    fi
    return 0
}

# ---------------------------------------------------------------------------
# rw_clean_plan FILE GROUPS
#
# Compile FILE into a plan for the enabled GROUPS (space-separated, must include
# "base").  Echoed on stdout.
#
# ⚠️ A DISABLED group's paths become PROTECTIONS, not merely skipped deletes.
# Without that, --keep-java would leave /opt/openjre-8 named only by a delete
# nobody runs, and the /opt whitelist sweep would remove it anyway — the flag
# would do nothing and say nothing.
# ---------------------------------------------------------------------------
rw_clean_plan() {
    local file="$1" groups="$2" g found

    [ -f "$file" ] || { echo "rw_clean_plan: no such file: $file" >&2; return 1; }

    case " $groups " in
        *" base "*) ;;
        *) echo "rw_clean_plan: the group list must include 'base' (it cannot be switched off)" >&2
           return 1 ;;
    esac
    for g in $groups; do
        found=0
        case " $RW_CLEAN_GROUPS_ALL " in *" $g "*) found=1 ;; esac
        [ "$found" = 1 ] || { echo "rw_clean_plan: unknown group '$g'" >&2; return 1; }
    done

    if ! rw_clean_validate "$file" >/dev/null; then
        echo "rw_clean_plan: $file does not validate:" >&2
        rw_clean_validate "$file" >&2
        return 1
    fi

    rw_clean_parse "$file" | awk -F'\t' -v enabled=" $groups " '
        function dirof(p)  { d = p; sub(/\/[^\/]*$/, "", d); return (d == "" ? "/" : d) }
        function nameof(p) { n = p; sub(/^.*\//, "", n); return n }
        function on(g)     { return index(enabled, " " g " ") > 0 }
        {
            type = $1; group = $2; path = $3
            if (type == "keep") {
                if (on(group)) keeps[++nk] = dirof(path) "\t" nameof(path)
            } else if (type == "scope") {
                if (on(group)) sweeps[++ns] = path
            } else if (type == "delete") {
                if (on(group)) dels[++nd] = path
                else keeps[++nk] = dirof(path) "\t" nameof(path)
            } else if (type == "truncate") {
                if (on(group)) truncs[++nt] = path
                else keeps[++nk] = dirof(path) "\t" nameof(path)
            }
        }
        END {
            for (i = 1; i <= nk; i++) print "keep\t"     keeps[i]
            for (i = 1; i <= ns; i++) print "sweep\t"    sweeps[i]
            for (i = 1; i <= nd; i++) print "del\t"      dels[i]
            for (i = 1; i <= nt; i++) print "truncate\t" truncs[i]
        }'
}

# ---------------------------------------------------------------------------
# rw_clean_offline_path BASE DEVPATH
#
# Map a device-absolute path onto the right one of the four offline mounts.
#
# LONGEST prefix wins, and it must match on a component boundary.  Both matter:
# role "root" has device path "/", which prefixes everything, and /home/rootless
# must not be mistaken for something under /home/root.
# ---------------------------------------------------------------------------
rw_clean_offline_path() {
    local base="$1" dev="$2" entry role dp best_role="root" best_dp="/" rest
    [ -n "$base" ] || return 1
    case "$dev" in /*) ;; *) return 1 ;; esac
    base="${base%/}"

    for entry in $RW_ROLE_DEVICE_PATH; do
        role="${entry%%:*}"
        dp="${entry#*:}"
        [ "$dp" = "/" ] && continue
        case "$dev" in
            "$dp"|"$dp"/*)
                if [ "${#dp}" -gt "${#best_dp}" ]; then best_role="$role"; best_dp="$dp"; fi
                ;;
        esac
    done

    if [ "$best_role" = "root" ]; then
        echo "$base/root$dev"
    else
        rest="${dev#$best_dp}"
        echo "$base/$best_role$rest"
    fi
}

# ---------------------------------------------------------------------------
# rw_clean_check_base BASE
#
# The guard.  Refuses every base that would make the rules resolve to the dev
# host's own filesystem.  Called once by rw_clean_apply and again by every
# rw_clean_del, because a caller that skipped the first would otherwise get no
# check at all.
# ---------------------------------------------------------------------------
rw_clean_check_base() {
    local base="$1" norm

    if [ -z "$base" ]; then
        echo "rw-clean: refusing an EMPTY base — the rules would resolve to this host's /etc, /opt and /usr/lib" >&2
        return 1
    fi

    # Collapse repeated slashes and a trailing "/." so that "//" and "/." are
    # recognised as "/" rather than sneaking past a string comparison.
    norm=$(printf '%s' "$base" | sed -e 's:/\{2,\}:/:g' -e 's:/\.$:/:' -e 's:\(.\)/$:\1:')
    if [ "$norm" = "/" ] || [ -z "$norm" ]; then
        echo "rw-clean: refusing base '$base' — '/' is this host's root, not a mounted card" >&2
        return 1
    fi

    case "$base" in
        *..*) echo "rw-clean: refusing base '$base' — it contains '..'" >&2; return 1 ;;
    esac

    if [ ! -d "$base" ]; then
        echo "rw-clean: refusing base '$base' — not a directory" >&2
        return 1
    fi
    return 0
}

# ---------------------------------------------------------------------------
# rw_clean_del BASE DEVPATH
#
# Delete DEVPATH (device-absolute, globs allowed in the last component) from the
# card mounted under BASE.  Set RW_CLEAN_DRY=1 to report and delete nothing.
#
# A path that is not there is NORMAL and not an error: a card may already be
# partly cleaned, and pulling one to rename a unit is an ordinary thing to do.
# ---------------------------------------------------------------------------
rw_clean_del() {
    local base="$1" dev="$2" dir name hostdir m sz

    rw_clean_check_base "$base" || return 1

    if [ -z "$dev" ]; then
        echo "rw_clean_del: empty path" >&2
        return 1
    fi
    case "$dev" in
        /) echo "rw_clean_del: refusing '/' — that is the whole tree" >&2; return 1 ;;
        /*) ;;
        *) echo "rw_clean_del: path must be device-absolute: $dev" >&2; return 1 ;;
    esac
    case "$dev" in
        *..*) echo "rw_clean_del: refusing a path containing '..': $dev" >&2; return 1 ;;
    esac

    dir="${dev%/*}"; [ -n "$dir" ] || dir="/"
    name="${dev##*/}"

    hostdir=$(rw_clean_offline_path "$base" "$dir") || return 1

    # Belt and braces: after mapping, the target must still be under the base.
    case "$hostdir/" in
        "${base%/}"/*) ;;
        *) echo "rw_clean_del: '$dev' resolved to '$hostdir', outside '$base' — refusing" >&2
           return 1 ;;
    esac

    # Only the last component is unquoted, so a glob expands and a base
    # containing a space still resolves.  -e is false for a DANGLING symlink, and
    # a dangling symlink is exactly what an offline tool sees, so test -L too.
    for m in "$hostdir"/$name; do
        [ -e "$m" ] || [ -L "$m" ] || continue
        if [ -L "$m" ]; then
            sz="link"
        else
            sz=$(du -sh "$m" 2>/dev/null | awk '{print $1}')
        fi
        if [ -n "${RW_CLEAN_DRY:-}" ]; then
            printf '  would delete  %-6s %s\n' "${sz:-?}" "$m"
        else
            printf '  delete        %-6s %s\n' "${sz:-?}" "$m"
            rm -rf "$m"
        fi
    done
    return 0
}

# ---------------------------------------------------------------------------
# rw_clean_truncate BASE DEVPATH
#
# Empty DEVPATH in place.  For a file something else owns the directory of —
# /var/cron/tabs/root is a symlink into /home/root/data/cron, so removing that
# directory destroys the crontab and cron's spool root
# (SYSTEM_ANALYSIS.md#52-as-we-run-it--game-mode).
# ---------------------------------------------------------------------------
rw_clean_truncate() {
    local base="$1" dev="$2" host sz

    rw_clean_check_base "$base" || return 1
    case "$dev" in
        /*) ;;
        *) echo "rw_clean_truncate: path must be device-absolute: $dev" >&2; return 1 ;;
    esac
    case "$dev" in
        *..*) echo "rw_clean_truncate: refusing a path containing '..': $dev" >&2; return 1 ;;
    esac

    host=$(rw_clean_offline_path "$base" "$dev") || return 1
    [ -f "$host" ] || return 0

    sz=$(du -sh "$host" 2>/dev/null | awk '{print $1}')
    if [ -n "${RW_CLEAN_DRY:-}" ]; then
        printf '  would truncate %-6s %s\n' "${sz:-?}" "$host"
    else
        printf '  truncate      %-6s %s\n' "${sz:-?}" "$host"
        : > "$host"
    fi
    return 0
}

# ---------------------------------------------------------------------------
# rw_clean_apply BASE PLANFILE
#
# Run a compiled plan against a card mounted under BASE.  RW_CLEAN_DRY=1 prints
# every fully-resolved absolute path and deletes nothing — which is what
# --dry-run is, and it has to happen HERE rather than in the caller, because the
# resolution is the part worth showing.
#
# Order: explicit deletes, then the sweeps, then the truncations.  Truncations
# last so that nothing can unlink a file this run has just emptied.
# ---------------------------------------------------------------------------
rw_clean_apply() {
    local base="$1" plan="$2" kind a b hostdir child name kept

    rw_clean_check_base "$base" || return 1
    [ -f "$plan" ] || { echo "rw_clean_apply: no such plan: $plan" >&2; return 1; }

    while IFS=$'\t' read -r kind a b; do
        [ "$kind" = "del" ] || continue
        rw_clean_del "$base" "$a"
    done < "$plan"

    while IFS=$'\t' read -r kind a b; do
        [ "$kind" = "sweep" ] || continue
        hostdir=$(rw_clean_offline_path "$base" "$a") || continue
        [ -d "$hostdir" ] || continue

        # find rather than a glob, so a dotfile is swept too and a dangling
        # symlink is still listed.  -mindepth/-maxdepth 1: a scope never
        # recurses, so a kept directory's contents are never examined.
        while IFS= read -r child; do
            [ -n "$child" ] || continue
            name=$(basename "$child")
            kept=0
            while IFS=$'\t' read -r k kdir kname; do
                [ "$k" = "keep" ] || continue
                [ "$kdir" = "$a" ] || continue
                # shellcheck disable=SC2254  # the glob is the point
                case "$name" in $kname) kept=1; break ;; esac
            done < "$plan"
            [ "$kept" = 1 ] && continue
            rw_clean_del "$base" "$a/$name"
        done <<EOF
$(find "$hostdir" -mindepth 1 -maxdepth 1 2>/dev/null | LC_ALL=C sort)
EOF
    done < "$plan"

    while IFS=$'\t' read -r kind a b; do
        [ "$kind" = "truncate" ] || continue
        rw_clean_truncate "$base" "$a"
    done < "$plan"

    return 0
}
