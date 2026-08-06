#!/bin/bash
#
# rw-provision.sh — read device-files/provision-rules.conf, compile it into a plan,
#                   and apply that plan either OFFLINE (to a mounted card) or LIVE
#                   (as an interpreter piped to the device over ssh).
#
# SOURCED, not executed:   . "$REPO_ROOT/rw-provision.sh"
#                          (needs rw-identify.sh and rw-clean.sh sourced first —
#                          rw_clean_offline_path does the p2/p3/p5/p6 mapping and
#                          rw_clean_check_base is the guard)
#
# IMPROVEMENT_PLAN.md C12. The delete half is rw-clean.sh; this is the install half
# and it is deliberately the same shape.
#
# ── The decisions are not in this file ──────────────────────────────────────
#
# Every file, link, mode and config edit lives in device-files/provision-rules.conf
# with a reason per entry, read by BOTH consumers:
#
#   setup-device.sh         live, over SSH
#   commission-offline.sh   offline, card in a reader
#
# so the two cannot drift. They HAD drifted: the online path removed stale rc*.d
# links before relinking and the offline path did not.
#
# ── Two executors, and why the online one is a generated script ─────────────
#
# The offline executor is a shell function here. The online one CANNOT be, because
# the work happens on the far side of an ssh pipe — so rw_provision_online_script
# emits the interpreter as text and setup-device.sh pipes it to `ssh <t> sh -s`
# together with the plan. Consequences worth knowing:
#
#   - It is /bin/sh for BusyBox ash, not bash. No [[, no arrays, no local.
#   - `install` is the one verb it cannot do alone: the source bytes are on the
#     host. setup-device.sh scp's them first and the interpreter only chmods.
#   - It honours $RW_PROVISION_ROOT, which is "" on a device. That is what lets
#     tests/rw_provision_test.sh group E run it against a copied tree and compare
#     its dry run with the offline one — a comparison of two executors rather than
#     of one executor and a wish.
#
# ── The plan format, which is the interface between the two ─────────────────
#
# Tab-separated, four fields, emitted in DEPENDENCY order (not file order):
#
#   unlink	-	<path-or-glob>	-
#   install	<mode>	<device-path>	<repo-relative-source>
#   link	-	<link-path>	<link-target>
#   link-opt	-	<link-path>	<link-target>
#   touch	<mode>	<device-path>	-
#   backup	-	<copy-to>	<copy-from>
#   directive	-	<config-file>	<Key>=<Value>
#   dropline	-	<config-file>	<ERE>
#
# unlink before link (a glob would eat the link just made), install before link (a
# link to a not-yet-written file dangles on a card), dropline last (it edits files
# install may just have placed). Paths are DEVICE-absolute; mapping them onto
# p2/p3/p5/p6 is the offline executor's job.

RW_PROVISION_TYPES="install link link-opt unlink touch backup directive dropline"
RW_PROVISION_GROUPS_ALL="base mdns sshd"
RW_PROVISION_GROUPS_DEFAULT="base mdns sshd"
RW_PROVISION_GROUPS_OPTIONAL="mdns sshd"

rw_provision_default_groups()  { echo "$RW_PROVISION_GROUPS_DEFAULT"; }
rw_provision_optional_groups() { echo "$RW_PROVISION_GROUPS_OPTIONAL"; }

# ---------------------------------------------------------------------------
# rw_provision_rules_file
#
# Resolved from this file's own location, so a caller in a subdirectory or one
# invoked through a symlink still finds it.
# ---------------------------------------------------------------------------
rw_provision_rules_file() {
    local d
    d=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)
    echo "$d/device-files/provision-rules.conf"
}

# ---------------------------------------------------------------------------
# rw_provision_parse FILE
#
# Echo "<type>\t<group>\t<mode>\t<target>\t<source>" per record, dropping the
# reason. A tab inside the reason must not shift the first five fields, so the
# reason is "field 6 onwards" rather than "field 6".
# ---------------------------------------------------------------------------
rw_provision_parse() {
    [ -f "$1" ] || { echo "rw_provision_parse: no such file: $1" >&2; return 1; }
    awk -F'\t' '
        /^[ \t]*#/ { next }
        /^[ \t]*$/ { next }
        NF >= 6    { printf "%s\t%s\t%s\t%s\t%s\n", $1, $2, $3, $4, $5 }
    ' "$1"
}

# ---------------------------------------------------------------------------
# rw_provision_validate FILE [REPO_ROOT]
#
# Echo every problem; return 1 if there were any.
# ---------------------------------------------------------------------------
rw_provision_validate() {
    local file="$1" repo="${2:-}" out
    [ -f "$file" ] || { echo "  no such file: $file"; return 1; }
    if [ -z "$repo" ]; then
        repo=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)
    fi

    out=$(awk -F'\t' -v types="$RW_PROVISION_TYPES" -v groups="$RW_PROVISION_GROUPS_ALL" \
              -v repo="$repo" '
        BEGIN {
            n = split(types, t, " ");  for (i = 1; i <= n; i++) TYPE[t[i]] = 1
            n = split(groups, g, " "); for (i = 1; i <= n; i++) GROUP[g[i]] = 1
            # Which types use which columns. Everything else must be "-", because a
            # value in a column the type ignores is a value somebody expected to
            # take effect.
            WANTMODE["install"] = 1; WANTMODE["touch"] = 1
            WANTSRC["install"] = 1; WANTSRC["link"] = 1; WANTSRC["link-opt"] = 1
            WANTSRC["backup"] = 1;  WANTSRC["directive"] = 1; WANTSRC["dropline"] = 1
        }
        /^[ \t]*#/ { next }
        /^[ \t]*$/ { next }
        {
            records++
            if (NF < 6) {
                # Also catches a space-separated line, which arrives as NF == 1.
                printf "  line %d: %d tab-separated field(s), need 6 (<type> <group> <mode> <target> <source> <reason>): %s\n", NR, NF, $0
                bad++; next
            }
            type = $1; group = $2; mode = $3; target = $4; src = $5
            for (i = 1; i <= 6; i++) {
                if ($i == "") { printf "  line %d: field %d is empty (use \"-\" for not-applicable)\n", NR, i; bad++; empty = 1 }
            }
            if (empty) { empty = 0; next }

            if (!(type in TYPE))   { printf "  line %d: unknown record type \"%s\"\n", NR, type; bad++; next }
            if (!(group in GROUP)) { printf "  line %d: unknown group \"%s\"\n", NR, group; bad++ }

            # ── target ──
            if (target !~ /^\//)   { printf "  line %d: target is not absolute: %s\n", NR, target; bad++ }
            if (target ~ /(^|\/)\.\.(\/|$)/) { printf "  line %d: target contains \"..\": %s\n", NR, target; bad++ }
            if (target ~ /\/\//)   { printf "  line %d: target contains \"//\": %s\n", NR, target; bad++ }
            if (target ~ /\/$/)    { printf "  line %d: target has a trailing slash: %s\n", NR, target; bad++ }
            if (target == "/")     { printf "  line %d: target is \"/\"\n", NR; bad++ }

            # ⚠️ rc0.d and rc6.d are SHUTDOWN, not startup. Unreachable through this
            # file by construction, the same guarantee as clean-rules.conf gives and
            # as p1s absence from RW_PART_ROLES.
            if (target ~ /\/rc[06]\.d(\/|$)/) {
                printf "  line %d: rc0.d and rc6.d are shutdown, not startup — no rule may name them: %s\n", NR, target
                bad++
            }

            # A glob is allowed only in the LAST component: the executors quote the
            # directory part so a base containing a space still resolves, which means
            # a mid-path glob would be taken literally and match nothing, silently.
            dir = target; sub(/\/[^\/]*$/, "", dir)
            if (dir ~ /[*?[]/) {
                printf "  line %d: a glob is only allowed in the last component: %s\n", NR, target
                bad++
            }
            if (type != "unlink" && target ~ /[*?]/) {
                printf "  line %d: only unlink may take a glob target: %s\n", NR, target
                bad++
            }

            # ── mode: DECLARED, never read off disk ──
            # /mnt/c reports every file 0777 and discards chmod, so a mode taken from
            # the source on the dev host would be a constant, not a measurement.
            if (type in WANTMODE) {
                if (mode !~ /^[0-7][0-7][0-7][0-7]?$/) {
                    printf "  line %d: %s needs a declared octal mode, got \"%s\"\n", NR, type, mode
                    bad++
                }
            } else if (mode != "-") {
                printf "  line %d: %s ignores the mode column, so it must be \"-\", not \"%s\"\n", NR, type, mode
                bad++
            }

            # ── source ──
            if (type in WANTSRC) {
                if (src == "-") { printf "  line %d: %s needs a source\n", NR, type; bad++; next }
            } else if (src != "-") {
                printf "  line %d: %s ignores the source column, so it must be \"-\", not \"%s\"\n", NR, type, src
                bad++
            }

            if (type == "install") {
                if (src ~ /^\//) {
                    printf "  line %d: an install source is repo-relative, not absolute: %s\n", NR, src
                    bad++
                } else if (src ~ /(^|\/)\.\.(\/|$)/) {
                    printf "  line %d: install source contains \"..\": %s\n", NR, src
                    bad++
                } else if (system("test -f " repo "/" src) != 0) {
                    printf "  line %d: install source is not in the repo: %s\n", NR, src
                    bad++
                }
            }

            # ⚠️ A link source must be RELATIVE. An absolute symlink target is correct
            # on a running device and DANGLING on a mounted card, where /etc lives at
            # $BASE/root/etc — and a dangling rc5.d link is skipped in silence at boot.
            # This is the one defect this file could introduce that nothing downstream
            # would catch.
            if (type == "link" || type == "link-opt") {
                if (src ~ /^\//) {
                    printf "  line %d: a link source must be RELATIVE (it dangles on a mounted card otherwise): %s\n", NR, src
                    bad++
                }
            }

            if (type == "directive" && src !~ /^[A-Za-z][A-Za-z0-9]*=/) {
                printf "  line %d: a directive source must be Key=Value, got: %s\n", NR, src
                bad++
            }
        }
        END {
            if (records == 0) { print "  no records at all — every line is a comment or blank"; bad++ }
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
# rw_provision_check_keeps PROVISION_RULES CLEAN_RULES
#
# ⚠️ The cross-file invariant. A boot link this file creates that
# clean-rules.conf does not name with a `keep` is deleted by the next
# --deep-clean, so the unit boots correctly once and loses the link on the
# following clean.
#
# This used to be a comment in both files asking a human to remember. Both files
# parse, so it is checkable.
# ---------------------------------------------------------------------------
rw_provision_check_keeps() {
    local prules="$1" crules="$2" bad=0 kind group mode target src dir name

    [ -f "$prules" ] || { echo "  no such file: $prules"; return 1; }
    [ -f "$crules" ] || { echo "  no such file: $crules"; return 1; }

    while IFS=$'\t' read -r kind group mode target src; do
        case "$kind" in link|link-opt) ;; *) continue ;; esac
        # Only rc*.d links are subject to a sweep; a link elsewhere is not swept
        # because no scope covers its directory.
        case "$target" in */rc[2-5S].d/*) ;; *) continue ;; esac
        dir="${target%/*}"
        name="${target##*/}"
        if ! rw_clean_parse "$crules" | awk -F'\t' -v d="$dir" -v n="$name" '
                $1 == "keep" {
                    kd = $3; sub(/\/[^\/]*$/, "", kd)
                    kn = $3; sub(/^.*\//, "", kn)
                    if (kd == d && kn == n) { found = 1 }
                }
                END { exit(found ? 0 : 1) }'; then
            echo "  $target is created by provision-rules.conf and NOT kept by clean-rules.conf"
            echo "      the next --deep-clean will sweep it; add:  keep	base	$target	<reason>"
            bad=1
        fi
    done <<EOF
$(rw_provision_parse "$prules")
EOF

    [ "$bad" = 0 ]
}

# ---------------------------------------------------------------------------
# rw_provision_plan FILE GROUPS
#
# Compile FILE into a plan for the enabled GROUPS (space-separated, must include
# "base"), emitted in dependency order.
#
# Unlike rw_clean_plan there is no "a disabled group protects" rule: not
# installing a file has no counterpart that could then remove it by surprise.
# ---------------------------------------------------------------------------
rw_provision_plan() {
    local file="$1" groups="$2" g found

    [ -f "$file" ] || { echo "rw_provision_plan: no such file: $file" >&2; return 1; }

    case " $groups " in
        *" base "*) ;;
        *) echo "rw_provision_plan: the group list must include 'base' (it cannot be switched off)" >&2
           return 1 ;;
    esac
    for g in $groups; do
        found=0
        case " $RW_PROVISION_GROUPS_ALL " in *" $g "*) found=1 ;; esac
        [ "$found" = 1 ] || { echo "rw_provision_plan: unknown group '$g'" >&2; return 1; }
    done

    if ! rw_provision_validate "$file" >/dev/null; then
        echo "rw_provision_plan: $file does not validate:" >&2
        rw_provision_validate "$file" >&2
        return 1
    fi

    # ⚠️ Order is emitted here, not read from the file, so a rule added in the wrong
    # place in the data file cannot produce a plan that installs after it links.
    rw_provision_parse "$file" | awk -F'\t' -v enabled=" $groups " '
        function on(g) { return index(enabled, " " g " ") > 0 }
        {
            if (!on($2)) next
            rec = $1 "\t" $3 "\t" $4 "\t" $5
            if      ($1 == "unlink")    unl[++nu]  = rec
            else if ($1 == "install")   ins[++ni]  = rec
            else if ($1 == "backup")    bak[++nb]  = rec
            else if ($1 == "link")      lnk[++nl]  = rec
            else if ($1 == "link-opt")  lnk[++nl]  = rec
            else if ($1 == "touch")     tch[++nt]  = rec
            else if ($1 == "directive") dir[++nd]  = rec
            else if ($1 == "dropline")  drp[++np]  = rec
        }
        END {
            for (i = 1; i <= nu; i++) print unl[i]   # stale links first
            for (i = 1; i <= ni; i++) print ins[i]   # then the payload
            for (i = 1; i <= nb; i++) print bak[i]   # back up before editing
            for (i = 1; i <= nl; i++) print lnk[i]   # link only what exists
            for (i = 1; i <= nt; i++) print tch[i]
            for (i = 1; i <= nd; i++) print dir[i]
            for (i = 1; i <= np; i++) print drp[i]   # edits last
        }'
}

# ---------------------------------------------------------------------------
# rw_provision_canonical
#
# Filter: stdin is either executor's dry-run output, stdout is the comparable set.
#
# ⚠️ This is what makes "one list, two executors" checkable rather than intended.
# Both executors print
#
#     <verb> <mode> <device-path> <source>    -> <resolved-host-path>
#
# and this strips the resolved path, because that is the ONE thing that legitimately
# differs (`/etc/...` on a device, `$BASE/root/etc/...` offline). Everything to the
# left must match exactly, so a wrong mode, a wrong source or a dropped record all
# show up as a diff.
#
# ⚠️ The separator before `->` is a TAB, not spaces. `s/ *->.*//` silently strips
# nothing and every line then carries its own host path, so the two sets differ on
# all of them and the diff says "1,31c1,31" — which reads like a total mismatch
# rather than a broken filter.
# ---------------------------------------------------------------------------
rw_provision_canonical() {
    sed -n 's/^  would \([a-z-]*\) *//p' | sed 's/[[:space:]]*->.*$//; s/[[:space:]]*$//' | LC_ALL=C sort
}

# ---------------------------------------------------------------------------
# rw_provision_apply_offline BASE PLANFILE REPO_ROOT
#
# The OFFLINE executor. RW_PROVISION_DRY=1 resolves and prints everything and
# changes nothing.
# ---------------------------------------------------------------------------
rw_provision_apply_offline() {
    local base="$1" plan="$2" repo="$3"
    local kind mode target src dest hostdir name m rc=0

    rw_clean_check_base "$base" || return 1
    [ -f "$plan" ] || { echo "rw_provision_apply_offline: no such plan: $plan" >&2; return 1; }
    [ -d "$repo" ] || { echo "rw_provision_apply_offline: no such repo root: $repo" >&2; return 1; }

    # resolve <device-path> -> host path, refusing anything that lands outside base.
    _rwp_resolve() {
        local dev="$1" out
        out=$(rw_clean_offline_path "$base" "$dev") || return 1
        case "$out/" in
            "${base%/}"/*) ;;
            *) echo "  refusing $dev — resolved to $out, outside $base" >&2; return 1 ;;
        esac
        printf '%s\n' "$out"
    }

    while IFS=$'\t' read -r kind mode target src; do
        [ -n "$kind" ] || continue
        case "$kind" in
            unlink)
                hostdir=$(_rwp_resolve "${target%/*}") || { rc=1; continue; }
                name="${target##*/}"
                # ⚠️ A dry run prints the RECORD, once, and never the per-match
                # expansion. What matched is card state, not plan — and printing
                # both makes the two executors' sets differ by whatever happens to
                # be on this particular card, which is exactly the asymmetry the
                # comparison exists to detect.
                if [ -n "${RW_PROVISION_DRY:-}" ]; then
                    printf '  would unlink    -     %s\t-\t-> %s/%s\n' "$target" "$hostdir" "$name"
                    continue
                fi
                # Only the last component is unquoted, so the glob expands and a base
                # containing a space still resolves. -e is false for a dangling
                # symlink and a dangling symlink is exactly what an offline tool
                # sees, so test -L too.
                for m in "$hostdir"/$name; do
                    [ -e "$m" ] || [ -L "$m" ] || continue
                    printf '  unlink          %s\n' "$m"; rm -f "$m"
                done
                ;;
            install)
                dest=$(_rwp_resolve "$target") || { rc=1; continue; }
                if [ ! -f "$repo/$src" ]; then
                    echo "  MISSING source $repo/$src" >&2; rc=1; continue
                fi
                if [ -n "${RW_PROVISION_DRY:-}" ]; then
                    printf '  would install   %-5s %s\t%s\t-> %s\n' "$mode" "$target" "$src" "$dest"
                else
                    mkdir -p "$(dirname "$dest")"
                    cp "$repo/$src" "$dest" && chmod "$mode" "$dest" \
                        && printf '  install         %-5s %s\n' "$mode" "$dest" || rc=1
                fi
                ;;
            backup)
                dest=$(_rwp_resolve "$target") || { rc=1; continue; }
                hostdir=$(_rwp_resolve "$src")  || { rc=1; continue; }
                if [ -n "${RW_PROVISION_DRY:-}" ]; then
                    printf '  would backup    -     %s\t%s\t-> %s\n' "$target" "$src" "$dest"
                elif [ -f "$hostdir" ] && [ ! -f "$dest" ]; then
                    cp "$hostdir" "$dest" && printf '  backup          %s\n' "$dest" || rc=1
                fi
                ;;
            link|link-opt)
                dest=$(_rwp_resolve "$target") || { rc=1; continue; }
                if [ -n "${RW_PROVISION_DRY:-}" ]; then
                    printf '  would %-9s -     %s\t%s\t-> %s\n' "$kind" "$target" "$src" "$dest"
                else
                    mkdir -p "$(dirname "$dest")"
                    # link-opt: skip rather than dangle. The link target is relative
                    # to the link's own directory, so resolve it there.
                    if [ "$kind" = "link-opt" ] && [ ! -e "$(dirname "$dest")/$src" ]; then
                        printf '  skip link-opt   %s (target %s is absent on this image)\n' "$target" "$src"
                        continue
                    fi
                    ln -sf "$src" "$dest" && printf '  link            %s -> %s\n' "$dest" "$src" || rc=1
                fi
                ;;
            touch)
                dest=$(_rwp_resolve "$target") || { rc=1; continue; }
                if [ -n "${RW_PROVISION_DRY:-}" ]; then
                    printf '  would touch     %-5s %s\t-\t-> %s\n' "$mode" "$target" "$dest"
                else
                    mkdir -p "$(dirname "$dest")"
                    [ -f "$dest" ] || : > "$dest"
                    chmod "$mode" "$dest" && printf '  touch           %-5s %s\n' "$mode" "$dest" || rc=1
                fi
                ;;
            directive)
                dest=$(_rwp_resolve "$target") || { rc=1; continue; }
                if [ -n "${RW_PROVISION_DRY:-}" ]; then
                    printf '  would directive -     %s\t%s\t-> %s\n' "$target" "$src" "$dest"
                else
                    _rwp_set_directive "$dest" "${src%%=*}" "${src#*=}" || rc=1
                fi
                ;;
            dropline)
                dest=$(_rwp_resolve "$target") || { rc=1; continue; }
                if [ -n "${RW_PROVISION_DRY:-}" ]; then
                    printf '  would dropline  -     %s\t%s\t-> %s\n' "$target" "$src" "$dest"
                else
                    _rwp_dropline "$dest" "$src" || rc=1
                fi
                ;;
            *) echo "  unknown plan verb: $kind" >&2; rc=1 ;;
        esac
    done < "$plan"

    unset -f _rwp_resolve
    return "$rc"
}

# ---------------------------------------------------------------------------
# _rwp_dropline FILE ERE
#
# Remove every line matching ERE.
#
# ⚠️ awk and not `sed -E "/$ere/d"`, because these EREs contain SLASHES —
# `^4:12345:respawn:/sbin/getty 38400 tty4` closes sed's address after `respawn:`
# and the rest is read as a command, so sed exits non-zero and the line stays. The
# symptom was a passing install and an unedited /etc/inittab.
#
# The result is written back through `cat >` rather than `mv`, so the file keeps its
# inode, owner and mode — /etc/profile and /etc/inittab are vendor files and a mv
# would silently give them the temp file's 0600.
# ---------------------------------------------------------------------------
_rwp_dropline() {
    local file="$1" ere="$2" tmp
    [ -f "$file" ] || return 0
    grep -qE "$ere" "$file" || return 0
    tmp="$file.rwp.$$"
    awk -v re="$ere" '$0 !~ re' "$file" > "$tmp" || { rm -f "$tmp"; return 1; }
    cat "$tmp" > "$file" && rm -f "$tmp" || { rm -f "$tmp"; return 1; }
    printf '  dropline        %s (%s)\n' "$file" "$ere"
}

# ---------------------------------------------------------------------------
# _rwp_set_directive FILE KEY VALUE
#
# "KEY VALUE" is present exactly once afterwards: substituted if the key is there
# (commented or not), appended if it is not.
#
# ⚠️ Stronger than the `sed s/^PermitEmptyPasswords yes/.../` it replaces, which
# matched one exact string — so a config saying "#PermitEmptyPasswords yes" or
# "PermitEmptyPasswords YES" passed through untouched and the hardening silently
# did nothing. Idempotent, which matters because both bring-up paths can be re-run.
# ---------------------------------------------------------------------------
_rwp_set_directive() {
    local file="$1" key="$2" val="$3"
    [ -f "$file" ] || { echo "  directive: no such file: $file" >&2; return 1; }
    if grep -qE "^[[:space:]]*#?[[:space:]]*$key[[:space:]]" "$file"; then
        sed -i -E "s|^[[:space:]]*#?[[:space:]]*$key[[:space:]].*|$key $val|" "$file"
    else
        printf '%s %s\n' "$key" "$val" >> "$file"
    fi
    printf '  directive       %s: %s %s\n' "$file" "$key" "$val"
}

# ---------------------------------------------------------------------------
# rw_provision_online_script
#
# Echo the LIVE executor as text, for `ssh <target> sh -s -- /tmp/rw-provision-plan`.
#
# ⚠️ /bin/sh for BusyBox ash: no [[, no arrays, no local, no `sed -E` guarantees
# beyond what busybox provides (it has -E). $RW_PROVISION_ROOT is "" on a device
# and is set only by the test harness, which is what lets the same interpreter be
# compared against the offline one.
#
# `install` arrives already scp'd — the caller puts the bytes in place and this only
# sets the mode, because the source lives on the host.
# ---------------------------------------------------------------------------
rw_provision_online_script() {
    cat <<'ONLINE'
# rw-provision live executor. Generated by rw_provision_online_script; do not edit
# on the device. R is "" in production and a test root in the harness.
PLAN="$1"
R="${RW_PROVISION_ROOT:-}"
DRY="${RW_PROVISION_DRY:-}"
rc=0

set_directive() {
    f="$1"; k="$2"; v="$3"
    [ -f "$f" ] || { echo "  directive: no such file: $f" >&2; return 1; }
    if grep -qE "^[[:space:]]*#?[[:space:]]*$k[[:space:]]" "$f"; then
        sed -i -E "s|^[[:space:]]*#?[[:space:]]*$k[[:space:]].*|$k $v|" "$f"
    else
        printf '%s %s\n' "$k" "$v" >> "$f"
    fi
    printf '  directive       %s: %s %s\n' "$f" "$k" "$v"
}

# awk, not `sed -E "/$ere/d"`: these EREs contain slashes, which close sed's address
# early. Written back through `cat >` so the vendor file keeps its inode and mode.
dropline() {
    f="$1"; re="$2"
    [ -f "$f" ] || return 0
    grep -qE "$re" "$f" || return 0
    t="$f.rwp.$$"
    awk -v re="$re" '$0 !~ re' "$f" > "$t" || { rm -f "$t"; return 1; }
    cat "$t" > "$f" && rm -f "$t" || { rm -f "$t"; return 1; }
    printf '  dropline        %s (%s)\n' "$f" "$re"
}

while IFS='	' read -r kind mode target src; do
    [ -n "$kind" ] || continue
    dest="$R$target"
    case "$kind" in
    unlink)
        d="$R${target%/*}"; n="${target##*/}"
        if [ -n "$DRY" ]; then
            # The record, once — never the per-match expansion. See the offline half.
            printf '  would unlink    -     %s\t-\t-> %s/%s\n' "$target" "$d" "$n"
        else
            for m in "$d"/$n; do
                [ -e "$m" ] || [ -L "$m" ] || continue
                printf '  unlink          %s\n' "$m"; rm -f "$m"
            done
        fi
        ;;
    install)
        # The bytes were scp'd by the caller; only the mode is ours to set.
        if [ -n "$DRY" ]; then
            printf '  would install   %-5s %s\t%s\t-> %s\n' "$mode" "$target" "$src" "$dest"
        elif [ -f "$dest" ]; then
            chmod "$mode" "$dest" && printf '  install         %-5s %s\n' "$mode" "$dest" || rc=1
        else
            echo "  MISSING $dest — it should have been copied before this ran" >&2; rc=1
        fi
        ;;
    backup)
        if [ -n "$DRY" ]; then
            printf '  would backup    -     %s\t%s\t-> %s\n' "$target" "$src" "$dest"
        elif [ -f "$R$src" ] && [ ! -f "$dest" ]; then
            cp "$R$src" "$dest" && printf '  backup          %s\n' "$dest" || rc=1
        fi
        ;;
    link|link-opt)
        if [ -n "$DRY" ]; then
            printf '  would %-9s -     %s\t%s\t-> %s\n' "$kind" "$target" "$src" "$dest"
        else
            mkdir -p "${dest%/*}"
            if [ "$kind" = "link-opt" ] && [ ! -e "${dest%/*}/$src" ]; then
                printf '  skip link-opt   %s (target %s is absent on this image)\n' "$target" "$src"
            else
                ln -sf "$src" "$dest" && printf '  link            %s -> %s\n' "$dest" "$src" || rc=1
            fi
        fi
        ;;
    touch)
        if [ -n "$DRY" ]; then
            printf '  would touch     %-5s %s\t-\t-> %s\n' "$mode" "$target" "$dest"
        else
            mkdir -p "${dest%/*}"
            [ -f "$dest" ] || : > "$dest"
            chmod "$mode" "$dest" && printf '  touch           %-5s %s\n' "$mode" "$dest" || rc=1
        fi
        ;;
    directive)
        if [ -n "$DRY" ]; then
            printf '  would directive -     %s\t%s\t-> %s\n' "$target" "$src" "$dest"
        else
            set_directive "$dest" "${src%%=*}" "${src#*=}" || rc=1
        fi
        ;;
    dropline)
        if [ -n "$DRY" ]; then
            printf '  would dropline  -     %s\t%s\t-> %s\n' "$target" "$src" "$dest"
        else
            dropline "$dest" "$src" || rc=1
        fi
        ;;
    *) echo "  unknown plan verb: $kind" >&2; rc=1 ;;
    esac
done < "$PLAN"
exit "$rc"
ONLINE
}
