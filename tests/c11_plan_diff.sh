#!/bin/bash
#
# One-off migration control for the clean-rules migration: every path the retired
# commissioning/provision.sh --remove heredoc deleted must be covered by the plan
# compiled from device-files/clean-rules.conf, or be a recorded deliberate omission.
#
# NOT a regression test. The heredoc it reads no longer exists in the working tree
# — it is extracted from git — so this cannot be re-run once the commit that
# deleted it is history. Its output goes in the commit message instead.
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && bash tests/c11_plan_diff.sh"

set -u
cd "$(dirname "$0")/.." || exit 1

. ./lib/rw-identify.sh
. ./lib/rw-clean.sh

RULES=device-files/clean-rules.conf

# ⚠️ Checked against BOTH plans, and --remove's is the one that matters.
#
# The heredoc WAS --remove, and --remove's plan has no sweeps. Run against
# --deep-clean's plan alone this reported 47/47 covered while --remove had silently
# lost 13 targets: `sweep /opt` and `sweep /home/root/data` cover most explicit
# deletes, so a missing `delete` rule is invisible there. A control that can only
# see the failure in one of the two plans is not a control for the other.
PLANS="remove:$(rw_clean_remove_groups)|deep:$(rw_clean_default_groups)"

# The heredoc, from the commit before the fold.
HD=$(mktemp)
git show HEAD:commissioning/provision.sh \
    | sed -n '/^if \[\[ "\$FLAG" == "--remove" \]\]; then/,/^    info "Bloatware files left in place/p' > "$HD"

# Only the paths it ACTED on: the argument list of an rm, or a truncation target.
# Deliberately not "every path-shaped string in the block" — that would count the
# ones named in its keep-comments as if they were deletions, and those comments are
# the whole reason the block had to go.
ACTED=$(mktemp); trap 'rm -f "$PLAN" "$HD" "$ACTED"' EXIT
{
    grep -oE '^rm -[rf]+ [^|;&]*' "$HD" | sed 's/^rm -[rf]* //' | tr ' ' '\n'
    grep -oE '^\[ -f [^ ]+ \] && : >' "$HD" | awk '{print $3}'
    # the `for svc in ... ; rm -f "/etc/init.d/$svc"` loop
    sed -n 's/^for svc in \(.*\); do$/\1/p' "$HD" | tr ' ' '\n' | sed 's|^|/etc/init.d/|'
} | sed 's/2>\/dev\/null//' | tr -d '"' | grep '^/' | LC_ALL=C sort -u > "$ACTED"

# covered_by <path> — echo the plan record that accounts for it, or nothing.
covered_by() {
    local p="$1" kind a b dir name
    dir="${p%/*}"; name="${p##*/}"

    while IFS=$'\t' read -r kind a b; do
        case "$kind" in
            del|truncate)
                [ "$a" = "$p" ] && { echo "$kind $a"; return 0; }
                # shellcheck disable=SC2254
                case "$p" in $a) echo "$kind $a (glob)"; return 0 ;; esac
                ;;
        esac
    done < "$PLAN"

    # A direct child of a swept directory, that no keep protects.
    while IFS=$'\t' read -r kind a b; do
        [ "$kind" = "sweep" ] || continue
        [ "$a" = "$dir" ] || continue
        while IFS=$'\t' read -r k kd kn; do
            [ "$k" = "keep" ] || continue
            [ "$kd" = "$dir" ] || continue
            # shellcheck disable=SC2254
            case "$name" in $kn) echo "KEPT by 'keep $kd $kn'"; return 1 ;; esac
        done < "$PLAN"
        echo "sweep $a"
        return 0
    done < "$PLAN"

    # /var/log is a symlink to /home/root/log (p3), so a /var/log/x deletion is
    # the same inode as /home/root/log/x and the p3 sweep covers it.
    case "$p" in
        /var/log/*) covered_by "/home/root/log/${p#/var/log/}" && return 0 ;;
    esac
    return 1
}

RC=0
OLDIFS="$IFS"; IFS='|'
for spec in $PLANS; do
    IFS="$OLDIFS"
    label="${spec%%:*}"; groups="${spec#*:}"
    PLAN=$(mktemp)
    rw_clean_plan "$RULES" "$groups" > "$PLAN" || { echo "could not compile the $label plan"; exit 1; }

    TOTAL=0; COV=0; MISS=0; KEPT=0
    echo ""
    echo "C11 plan diff, --$label: $(wc -l < "$ACTED") paths the retired heredoc acted on"
    echo ""
    while read -r p; do
        TOTAL=$((TOTAL + 1))
        if out=$(covered_by "$p"); then
            COV=$((COV + 1))
            printf '  ok       %-46s  %s\n' "$p" "$out"
        elif [ -n "$out" ]; then
            KEPT=$((KEPT + 1))
            printf '  DIVERGES %-46s  %s\n' "$p" "$out"
        else
            MISS=$((MISS + 1))
            printf '  MISSING  %-46s  nothing in the plan accounts for it\n' "$p"
        fi
    done < "$ACTED"

    echo ""
    echo "  --$label: $COV covered, $KEPT deliberate divergence(s), $MISS unaccounted for, $TOTAL total"
    [ "$MISS" -eq 0 ] || { echo "  ✗ the fold dropped $MISS target(s) from --$label"; RC=1; }
    rm -f "$PLAN"
    IFS='|'
done
IFS="$OLDIFS"

echo ""
[ "$RC" -eq 0 ] || exit 1
echo "  ✓ nothing was silently dropped from either plan"

# ⚠️ What this control does NOT distinguish: delete from truncate.
# /home/root/log/concurrent.log reads as covered because the plan TRUNCATES it,
# where the heredoc unlinked it. The vendor's bytes are gone either way, and the
# inode surviving is the deliberate part — syslogd holds it open, so unlinking it
# live leaves syslogd writing to an unlinked inode and logging stops until reboot.
# /home/root/bloatware-removed.txt is absent from the list by design: the heredoc
# WROTE it, and only rm/truncate targets are extracted.
