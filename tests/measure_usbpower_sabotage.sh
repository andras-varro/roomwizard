#!/bin/bash
#
# measure_usbpower_sabotage.sh — re-measure tests/rw_usbpower_test.sh against
# deliberately broken copies of the p1 writer and its three Python tools.
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && bash tests/measure_usbpower_sabotage.sh"
#
# IMPROVEMENT_PLAN.md F15. Host-only: no device, no card, no root. Needs python3,
# so WSL rather than Git Bash.
#
# ── Why this file exists ────────────────────────────────────────────────────
#
# There is no CI, so a suite that has only ever been seen passing is not evidence
# that it can fail — and rw_usbpower_test.sh passed 94/94 on its first run. Two
# sabotages were attempted by hand on 2026-08-08 and BOTH runs were abandoned on a
# timeout, so neither count was ever read. Until this harness ran, every group F–K
# claim was "seen passing" and nothing more.
#
# ⚠️ The cause of those timeouts is worth knowing, because it is the reason this
# file stages FIVE FILES and not two directories: `cp -a lib usb_host` from /mnt/c
# into /tmp is slow enough over DrvFs to blow a 300 s budget, and a harness that
# hangs looks exactly like a suite that hangs. The five files below are the entire
# set the suite reaches through RW_USBPOWER_LIB; the baseline case is what asserts
# that, so a sixth file added to what the suite sources fails here rather than
# silently reading the shipped copy.
#
# ⚠️ Every sabotage is asserted APPLIED before its count is believed, in two
# independent ways: the staged file's md5 must differ from the shipped one, AND the
# intended text must be present. A sed pattern that stops matching leaves the file
# correct, the suite passes, and "0 failed" reads exactly like "this sabotage is
# undetectable" — tests/measure_bundle_ssh_sabotage.sh records four sabotages that
# all reported 22/0 for that reason.
#
# This harness never restates an assertion. It drives the REAL suite via
# RW_USBPOWER_LIB, because a harness that re-implements what it checks can repair
# the very defect it was written to catch — tests/commission_prep_test.sh had
# exactly that bug, re-emitting the assignment its sabotage removed.
#
# There is no pre-fix-tree case here, unlike measure_arm_gate_sabotage.sh and
# measure_ssh_sabotage.sh: lib/rw-usbpower.sh and the three tools are new in F15,
# so there is no earlier revision of them to restore. Every case below is a sed.
#
# ── What each sabotage is, and why it is the one worth writing ───────────────
#
#   1  classify says "vendor" for an unknown md5   the refusal that stops the patch
#                                                  being applied to firmware the
#                                                  9-byte diff was never measured
#                                                  against
#   2  the post-write re-read replaced by the      step 9. vfat write-back means
#      constant it is being compared to            step 7's verdict was about a
#                                                  cache, not about the card
#   3  the backup verification deleted             step 6. p1 has no recovery over
#                                                  SSH, so a backup nobody checked
#                                                  is not a backup
#   4  verify_uimage.py always exits 0             a validator that cannot fail is
#                                                  indistinguishable from a file
#                                                  that is always valid
#   5  uimage_fix_crcs' CRC order swapped          signs a stale header. U-Boot
#                                                  refuses the image and the unit
#                                                  does not boot AT ALL — the
#                                                  worst outcome in the repo
#   6  the two-state md5 gate restored             an already-power-patched card is
#                                                  reported as done, so the mode
#                                                  patch writes NOTHING and exits 0
#   7  the transition chains off the patched       patch_dtb.py refuses an
#      image instead of deriving from the backup   already-patched input, so no card
#                                                  can move between patched states
#
# The expected minimums are MEASURED, printed by this script, and recorded below.
# They are minimums, not equalities: a case added to the suite may raise a count,
# and that is not a regression. Which cases fail matters more than how many, so
# they are named — a sabotage that trips a plausible number of the WRONG checks is
# the failure mode a bare count cannot show.
#
# Measured 2026-08-08, 13 s for the whole run; re-measured 2026-08-14 after the
# two-state md5 gate became three (B32), which moved two counts and added two
# sabotages. The 2026-08-14 counts are in the second column:
#
#   baseline                                     94 passed,  0 failed   (155/0)
#   1 classify returns vendor for an unknown     87 passed,  7 failed   (5 failed)
#       B5 B6       — a fourth md5 stops being refused
#       F11 G6 J7   — the run still fails, at step 3, but the word REFUSING is
#                     gone and a dry run stops refusing altogether
#       ⚠️ 7 -> 5 on 2026-08-14, and the MINIMUM was lowered rather than the
#       sabotage widened. B7 B8 (the empty md5) used to fail here too; classify now
#       guards an empty argument BEFORE the constants, so they pass under this
#       sabotage. That guard exists because an unset constant would make
#       `case "" in "$RW_UIMAGE_BOTH_MD5")` match and classify a missing file as
#       patched, and group A5 is what keeps the constants non-empty. The five above
#       were read from a run of this sabotage alone, not inferred from the total.
#   2 post-write re-read replaced by constant    86 passed,  8 failed   (8 failed)
#       I1-I4 I6-I9 — the whole of group I: a corrupted write is reported as a
#                     success, so neither the rollback nor its failure is reached
#   3 backup verification deleted                91 passed,  3 failed   (3 failed)
#       H1 H2 H3    — a pre-existing file that only LOOKS like an undo is accepted
#                     and p1 is written with no working undo
#   4 verify_uimage.py always exits 0            87 passed,  7 failed   (8 failed)
#       C6-C11 C13  — every way the validator must fail, including a missing file
#   5 uimage_fix_crcs CRC order swapped          71 passed, 23 failed  (40 failed)
#       D4 D6 D7    — the patched image no longer verifies and the diff is 5 bytes
#                     rather than 9: signing the header before storing the data CRC
#                     leaves offset 4 EQUAL to the vendor's, because the header it
#                     signed is the vendor's header
#       F H I J K M — and every sequence case, since step 5's md5 assert then
#                     refuses the derived image outright. The widest blast radius
#                     of the five, which is proportionate: this is the one that
#                     produces a unit that does not boot at all.
#   6 the two-state gate restored                       (2026-08-14)   (9 failed)
#       M4 M5 M6    — a card already carrying the 500 mA image is reported as done,
#                     so the mode patch writes NOTHING and exits 0. THE defect B32
#                     warned about, and group M's reason to exist
#       M8 M9 M11   — and the same early return swallows the refusals a power-only
#                     card with no usable backup must produce
#       M22 M23 M26 — the dry run stops reporting the transition, and the ssh
#                     transport reaches the wrong bytes
#       ⚠️ M17 does NOT fail, and the reason is fixture cascade rather than a hole:
#       the m2 card never becomes both-patched, so the downgrade case is handed a
#       power-only card and correctly reports "already". Not worth isolating — M4 is
#       the case that speaks — but do not read M17's pass as coverage.
#   7 the transition chains off the patched image       (2026-08-14)  (10 failed)
#       M3-M6 M13   — patch_dtb.py refuses an already-patched input, so a
#                     power -> both transition dies at step 4 instead of deriving
#                     from the verified backup, and the follow-on idempotence case
#                     is then handed the wrong state
#       M21-M23 M25 M26 — both other transports report the failure too
#
# ── The harness's own negative controls, measured 2026-08-08 ─────────────────
#
# An applied-assert that has only ever been seen passing is the same defect one
# level up, so all three of its refusals were driven deliberately, against copies
# of this file placed under tests/ (it resolves REPO from its own location, so a
# copy in /tmp measures nothing and says so):
#
#   sabotage 1's sed pattern rotted so it matches nothing
#       -> NOT APPLIED, "byte-identical to the shipped one", count discarded, exit 1
#   sabotage 1's applied-grep pointed at text the sed does not produce
#       -> NOT APPLIED, "changed, but /…/ is not in it", count discarded, exit 1
#   the address range dropped from sabotage 3, so both identical guards are patched
#       -> "0 copies of the guard left, want 1", count discarded, exit 1
#
# A fourth, found by accident and worth keeping in mind: changing `echo vendor` to
# `echo VENDOR` still APPLIES and still parses, but the library's `case` then falls
# through to its own `*)` and refuses anyway — so the sabotage weakens to 4 failures
# and the >= 5 minimum is what catches it (that weakening now yields 2, for the
# reason in sabotage 1's note above). The minimums are not decoration.

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SUITE="$REPO/tests/rw_usbpower_test.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

command -v python3 >/dev/null 2>&1 || {
    echo "python3 is required (WSL, not Git Bash) — the three tools under test are Python"
    exit 1
}
[ -f "$SUITE" ] || { echo "no suite at $SUITE"; exit 1; }

WORK="$(mktemp -d /tmp/rw-usbsab.XXXXXX)"
TREE="$WORK/tree"
trap 'rm -rf "$WORK"' EXIT INT TERM

fail=0

# ── the five files, and nothing else ───────────────────────────────────────
#
# rw_usbpower_test.sh reaches the tree under test in exactly three ways: it sources
# $TREE/lib/rw-usbpower.sh and $TREE/lib/rw-identify.sh, and it runs the three
# tools that rw_usbpower_tool resolves from the library's own BASH_SOURCE. Staging
# only these is what keeps the harness at seconds rather than minutes.
FILES="lib/rw-usbpower.sh lib/rw-identify.sh
       usb_host/uimage.py usb_host/patch_dtb.py usb_host/verify_uimage.py"

stage_tree() {
    local rel
    rm -rf "$TREE"
    mkdir -p "$TREE/lib" "$TREE/usb_host" || return 1
    for rel in $FILES; do
        cp "$REPO/$rel" "$TREE/$rel" || return 1
    done
}

run_suite() {
    RW_USBPOWER_LIB="$TREE/lib/rw-usbpower.sh" bash "$SUITE" 2>&1
}
counts() { grep -oE '[0-9]+ passed, [0-9]+ failed' <<<"$1" | tail -1; }
failed_n() {
    local n
    n="$(grep -oE '[0-9]+ failed' <<<"$1" | tail -1 | grep -oE '[0-9]+')"
    echo "${n:-0}"
}

# syntax_ok <path> — bash -n for a shell file, compile() for a python one. A
# sabotage that only breaks the parse measures nothing: the suite would fail for a
# reason no real defect could produce.
syntax_ok() {
    case "$1" in
        *.sh) bash -n "$1" 2>/dev/null ;;
        *.py) python3 -c 'import sys; compile(open(sys.argv[1]).read(), sys.argv[1], "exec")' "$1" 2>/dev/null ;;
        *)    return 0 ;;
    esac
}

# ═══════════════════════════════════════════════════════════════════════════
# baseline — the shipped five files, staged the same way every sabotage is
# ═══════════════════════════════════════════════════════════════════════════
#
# This is not a formality. It is the assertion that the five-file list above is
# COMPLETE: if the suite grows a sixth dependency, RW_USBPOWER_LIB would point at a
# tree missing it and the baseline fails here, rather than every sabotage below
# quietly measuring the shipped copy.
echo ""
echo "  baseline — the shipped tree, staged as five files"
stage_tree || { echo -e "  ${RED}FAIL${NC}  could not stage the tree"; exit 1; }
base_out="$(run_suite)"; base_rc=$?
if [ "$base_rc" -eq 0 ]; then
    echo -e "  ${GREEN}pass${NC}  $(counts "$base_out")"
else
    echo -e "  ${RED}FAIL${NC}  the shipped tree does not pass its own suite — fix that first"
    echo -e "  ${YELLOW}note:${NC} if the failures name a missing file, add it to FILES above"
    printf '%s\n' "$base_out" | grep -E '✗|failed' | head -8 | sed 's/^/        /'
    exit 1
fi

# ═══════════════════════════════════════════════════════════════════════════
# measure <label> <rel> <applied-grep> <want-min>
# ═══════════════════════════════════════════════════════════════════════════
#
# The caller stages the tree and applies the sed; this decides whether the count
# may be believed and then whether the suite caught it.
measure() {
    local label="$1" rel="$2" applied="$3" want="$4"
    local f="$TREE/$rel"

    if [ "$(md5sum "$f" | cut -d' ' -f1)" = "$(md5sum "$REPO/$rel" | cut -d' ' -f1)" ]; then
        echo -e "  ${RED}NOT APPLIED${NC}  $label"
        echo -e "               the staged $rel is byte-identical to the shipped one;"
        echo -e "               the sed matched nothing. Count discarded."
        fail=$((fail + 1))
        return
    fi
    if ! grep -qE "$applied" "$f"; then
        echo -e "  ${RED}NOT APPLIED${NC}  $label"
        echo -e "               $rel changed, but /$applied/ is not in it — the sed did"
        echo -e "               something other than what this case claims. Count discarded."
        fail=$((fail + 1))
        return
    fi
    if ! syntax_ok "$f"; then
        echo -e "  ${RED}NOT USABLE${NC}   $label — the sabotage broke $rel's syntax, so the"
        echo -e "               suite would fail for a reason no real defect produces."
        fail=$((fail + 1))
        return
    fi

    local out rc got
    out="$(run_suite)"; rc=$?
    got="$(failed_n "$out")"

    if [ "$rc" -ne 0 ] && [ "$got" -ge "$want" ]; then
        printf '  '; printf "${GREEN}caught${NC}"; printf '  %-44s %s\n' "$label" "$(counts "$out")"
    else
        printf '  '; printf "${RED}MISSED${NC}"; printf '  %-44s exit %s, %s (wanted >= %s failed)\n' \
            "$label" "$rc" "$(counts "$out")" "$want"
        fail=$((fail + 1))
    fi
}

echo ""
echo "  sabotages"

# ── 1. the refusal removed from the md5 gate ───────────────────────────────
#
# "unknown" is what stands between the patch and firmware the 9-byte diff was never
# measured against. Returning "vendor" instead does NOT immediately corrupt p1 —
# step 3's re-check of the pulled copy still refuses — which is exactly why this is
# worth measuring: the run still fails, but it fails with the wrong words and a dry
# run stops refusing at all.
stage_tree || exit 1
sed -i 's|^        \*)                        echo unknown; return 1 ;;$|        *)                        echo vendor; return 0 ;;|' \
    "$TREE/lib/rw-usbpower.sh"
measure "classify: unknown md5 reported as vendor" "lib/rw-usbpower.sh" \
    '^        \*\)                        echo vendor; return 0 ;;$' 5

# ── 2. step 9 trusts the write instead of re-reading the card ──────────────
#
# ⚠️ The 4-space anchor is load-bearing: the identical expression appears again at
# 8 spaces inside the rollback branch, and patching both would make the rollback
# claim success as well — two defects, so a negative control for neither.
#
# ⚠️ The replacement must name the variable the library actually compares against.
# It used to be $RW_UIMAGE_PATCHED_MD5; when the two-state gate became three
# (B32) that became $target, and a sabotage assigning an UNSET variable assigns
# the empty string — step 9 then correctly detects a mismatch and rolls back, so
# the suite passes and the sabotage reports "not caught" for the wrong reason.
stage_tree || exit 1
sed -i 's|^    got=\$(_rwup_md5 "\$img")$|    got="$target"  # SABOTAGE: assumed, not re-read|' \
    "$TREE/lib/rw-usbpower.sh"
measure "step 9: the re-read replaced by the expected md5" "lib/rw-usbpower.sh" \
    '^    got="\$target"  # SABOTAGE' 8

# ── 3. the backup is created and never checked ─────────────────────────────
#
# ⚠️ Restricted to step 6's block by an address range. The guard line is
# CHARACTER-IDENTICAL to step 3's re-check of the pulled copy — same text, same
# indent — so an unrestricted sed patches both, and the run then fails at step 3
# for a reason that has nothing to do with the backup.
stage_tree || exit 1
sed -i '/6: the backup/,/vendor kernel backed up/ s|^    if \[ "\$got" != "\$RW_UIMAGE_VENDOR_MD5" \]; then$|    if false; then  # SABOTAGE: backup accepted unchecked|' \
    "$TREE/lib/rw-usbpower.sh"
# The negative control for the range itself: step 3's guard must still be there.
n_left=$(grep -c '^    if \[ "\$got" != "\$RW_UIMAGE_VENDOR_MD5" \]; then$' "$TREE/lib/rw-usbpower.sh")
if [ "$n_left" != "1" ]; then
    echo -e "  ${RED}NOT APPLIED${NC}  backup verification deleted"
    echo -e "               $n_left copies of the guard left, want 1 — the range caught"
    echo -e "               step 3's identical line too. Count would measure the wrong defect."
    fail=$((fail + 1))
else
    measure "step 6: the backup is never verified" "lib/rw-usbpower.sh" \
        '^    if false; then  # SABOTAGE' 3
fi

# ── 4. the validator cannot fail ───────────────────────────────────────────
#
# Every line it prints is unchanged; only the exit status lies. That is the shape
# that matters, because steps 5, 7 and 9 all read the status and none reads the
# prose — and CLAUDE.md's C9 note records the same class of bug reaching the tree
# once already, a status collapsed by xargs.
stage_tree || exit 1
sed -i 's|^    sys\.exit(main(sys\.argv))$|    main(sys.argv); sys.exit(0)  # SABOTAGE: always clean|' \
    "$TREE/usb_host/verify_uimage.py"
measure "verify_uimage.py always exits 0" "usb_host/verify_uimage.py" \
    '^    main\(sys\.argv\); sys\.exit\(0\)  # SABOTAGE' 7

# ── 5. the header is signed before the data CRC is stored in it ────────────
#
# The data CRC lives IN the header, so writing it after the header has been signed
# leaves a header CRC computed over the old value. U-Boot checks the header first
# and refuses the image: no kernel, no console, no SSH.
#
# ⚠️ The fixture builder is unaffected and must stay so. tests/make-fake-uimage.py
# imports uimage_fix_crcs from the REAL repo's usb_host (its sys.path is relative
# to its own file, not to the tree under test), so the fixture is still correct and
# the sabotage is visible instead of being baked into the input as well.
stage_tree || exit 1
sed -i -e 's|^    struct\.pack_into(">I", data, 24, dcrc)$|    pass  # SABOTAGE: data CRC not stored yet|' \
       -e 's|^    return hcrc, dcrc$|    struct.pack_into(">I", data, 24, dcrc)\n    return hcrc, dcrc|' \
    "$TREE/usb_host/uimage.py"
measure "uimage_fix_crcs: the CRC order swapped" "usb_host/uimage.py" \
    '^    pass  # SABOTAGE: data CRC not stored yet$' 23

# ── 6. the two-state gate restored: any patched image means "nothing to do" ──
#
# ⚠️ THE defect B32 warned about, reproduced exactly. The old gate knew two md5s
# and returned 0 at its `patched` arm before anything else ran, so a unit already
# carrying the 500 mA image — which is every unit commissioned so far, .188
# included — would be asked for the mode patch, write NOTHING, and report success.
# This is the negative control for group M: if group M cannot see this, group M is
# decoration.
stage_tree || exit 1
sed -i 's|^    if \[ "\$state" = "\$want" \]; then$|    if [ "$state" != vendor ] \&\& [ "$state" != unknown ]; then  # SABOTAGE: two-state gate|' \
    "$TREE/lib/rw-usbpower.sh"
measure "the gate treats any patched image as done" "lib/rw-usbpower.sh" \
    '^    if \[ "\$state" != vendor \] && \[ "\$state" != unknown \]; then  # SABOTAGE' 9

# ── 7. the transition chains instead of deriving from the backup ────────────
#
# The other half of the same decision. patch_dtb.py refuses an already-patched
# input with exit 1, so leaving src at $img means a power -> both transition dies
# at step 4 — a loud failure rather than a silent one, but still a unit that cannot
# be moved between states. It is worth its own case because the two lines are
# independent: fixing the gate without fixing the source produces exactly this.
stage_tree || exit 1
sed -i 's|^            src="\$bak"$|            src="$img"  # SABOTAGE: chain the patches|' \
    "$TREE/lib/rw-usbpower.sh"
measure "the re-derivation chains off the patched image" "lib/rw-usbpower.sh" \
    '^            src="\$img"  # SABOTAGE: chain the patches$' 10

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "  ════════════════════════════════════════"
if [ "$fail" -eq 0 ]; then
    echo -e "  ${GREEN}every sabotage applied and was caught${NC}"
    echo ""
    exit 0
fi
echo -e "  ${RED}$fail sabotage(s) not applied or not caught${NC}"
echo -e "  ${YELLOW}an unapplied sabotage measures nothing — treat it as a harness bug${NC}"
echo ""
exit 1
