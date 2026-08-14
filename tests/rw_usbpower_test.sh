#!/bin/bash
#
# tests/rw_usbpower_test.sh — regression for lib/rw-usbpower.sh and the three
#                             device-tree tools it drives.
#
# IMPROVEMENT_PLAN.md F15. Host-only: no device, no card, no root. Needs python3.
#
# ── What is under test, and what a fixture can and cannot be ────────────────
#
# The subject is the SEQUENCE — gate on md5, derive, verify, back up, verify the
# backup, land under a temporary name, move, re-read, roll back — plus the two
# transports it runs over. It is the one step tests/rw_provision_test.sh group E
# cannot compare between the offline and online executors, because it is not
# expressible as a provision rule; group J below is this file's stand-in for that
# comparison.
#
# ⚠️ SYNTHETIC FIXTURES ONLY. The vendor uImage-system is a 5.2 MB copyrighted
# Steelcase binary, excluded by usb_host/.gitignore, and this repo is meant to be
# published — so it can never be committed as a fixture. tests/make-fake-uimage.py
# builds a few-hundred-byte uImage carrying a real FDT with one usb_otg_hs node,
# which reaches exactly the same walk because uimage.py FINDS the device tree by
# magic rather than asserting the vendor's 0x4eb788.
#
# ⚠️ The consequence, stated so nobody reads more into a pass than is there: the
# three md5 constants cannot be exercised against a synthetic image, because their
# whole content is "this is the firmware the 9-byte diff was measured against".
# Groups F–M therefore OVERRIDE them with the fixture's own md5s — the sequence is
# what is being tested, not the identity of one kernel. Group A asserts the shipped
# values separately, which is the half a synthetic fixture can still do.
#
# ── The overrides, and why the tools are not stubbed ────────────────────────
#
#   RW_USBPOWER_LIB   the library to test. Defaults to the shipped one; the
#                     sabotage harness points it at a broken COPY OF THE TREE, and
#                     because rw_usbpower_tool resolves usb_host/ from the
#                     library's own BASH_SOURCE, that one variable also redirects
#                     patch_dtb.py, verify_uimage.py and uimage.py. So a sabotage
#                     of any of the four is measurable through the same door.
#
# The fixture BUILDER is always the real repo's, never the tree under test: a
# generator that imports a sabotaged uimage.py would emit an image matching the
# defect and mask it.
#
# The python tools are driven for real. Stubbing them would leave the CRC ordering,
# the FDT walk and the value refusal untested, and those are the parts that decide
# whether p1 boots.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

LIB="${RW_USBPOWER_LIB:-$REPO/lib/rw-usbpower.sh}"
TREE="$(cd "$(dirname "$LIB")/.." && pwd)"
TOOLS="$TREE/usb_host"
REAL_TOOLS="$REPO/usb_host"
MAKE_IMG="$SCRIPT_DIR/make-fake-uimage.py"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
PASS=0; FAIL=0
ok()  { PASS=$((PASS + 1)); echo -e "  ${GREEN}✓${NC} $*"; }
bad() { FAIL=$((FAIL + 1)); echo -e "  ${RED}✗${NC} $*"; }

# eq <got> <want> <desc>
eq() {
    if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 — got '$1', want '$2'"; fi
}
# rc_is <got-rc> <want-rc> <desc>
rc_is() {
    if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 — exit $1, want $2"; fi
}
# says <text> <regex> <desc>
says() {
    if printf '%s\n' "$1" | grep -qE "$2"; then ok "$3"
    else bad "$3 — no line matched /$2/"; printf '%s\n' "$1" | sed 's/^/        /' | head -6; fi
}
# says_not <text> <regex> <desc>
says_not() {
    if printf '%s\n' "$1" | grep -qE "$2"; then
        bad "$3 — a line matched /$2/ and should not have"
    else ok "$3"; fi
}
md5of() { md5sum "$1" 2>/dev/null | cut -d' ' -f1; }

command -v python3 >/dev/null 2>&1 || { echo "python3 is required (WSL, not Git Bash)"; exit 1; }
[ -f "$LIB" ] || { echo "no library at $LIB"; exit 1; }

# ⚠️ Under /tmp, not under the repo: the fixtures include a file that must be
# byte-identical to a computed md5, and DrvFs on /mnt/c is close enough to lie about
# other things that it is not worth finding out about this one.
W="$(mktemp -d /tmp/rw-usbpower-test.XXXXXX)"
trap 'rm -rf "$W"' EXIT

echo ""
echo "  library: $LIB"
echo "  tools:   $TOOLS"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "A. the shipped constants — measured, not guessable, and not for editing"
# ═══════════════════════════════════════════════════════════════════════════
#
# The one thing a synthetic fixture can never check. Measured 2026-08-08 across
# five sources on three units; if either value changes, whoever changed it has to
# have re-measured, and this case is where they find that out.
A_VENDOR=$(grep -E '^RW_UIMAGE_VENDOR_MD5=' "$LIB"  | cut -d'"' -f2)
A_POWER=$(grep  -E '^RW_UIMAGE_POWER_MD5=' "$LIB"   | cut -d'"' -f2)
A_BOTH=$(grep   -E '^RW_UIMAGE_BOTH_MD5=' "$LIB"    | cut -d'"' -f2)
A_NAME=$(grep   -E '^RW_UIMAGE_NAME=' "$LIB"        | cut -d'"' -f2)
A_BACKUP=$(grep -E '^RW_UIMAGE_BACKUP=' "$LIB"      | cut -d'"' -f2)
eq "$A_VENDOR" "edc637ac14f90e0187b1ed65ffedf6d7" "A1 the vendor uImage-system md5 is the measured one"
eq "$A_POWER"  "a1fd1af8da18c430a34b24762aa16dab" "A2 the 500 mA uImage-system md5 is the measured one"
eq "$A_NAME"   "uImage-system"                    "A3 the file written on p1 is uImage-system"
eq "$A_BACKUP" "uImage-system.vendor"             "A4 the backup is uImage-system.vendor"
# Measured 2026-08-14: patch_dtb.py --mode over .188's own uImage-system.vendor.
eq "$A_BOTH"   "9021923205825a2ec36edeaa1fe3ccc3" "A7 the 500 mA + host-mode md5 is the measured one"

# All three must differ PAIRWISE, or the gate cannot tell "already done" from "do
# it" — and with two patches that is three comparisons, not one. The both-patched
# constant landing equal to the power-only one is exactly how a mode patch would
# report success having written nothing.
A_DUPES=$(printf '%s\n' "$A_VENDOR" "$A_POWER" "$A_BOTH" | sort | uniq -d | wc -l)
if [ -n "$A_VENDOR" ] && [ -n "$A_POWER" ] && [ -n "$A_BOTH" ] && [ "$A_DUPES" -eq 0 ]; then
    ok "A5 the vendor, 500 mA and host-mode md5s are three distinct non-empty values"
else
    bad "A5 the three md5 constants are not three distinct non-empty values"
fi

# p1 must not have become reachable through the role table as a side effect.
if grep -qE 'RW_PART_ROLES=.*(^|[^0-9])1:' "$REPO/lib/rw-identify.sh"; then
    bad "A6 RW_PART_ROLES now contains p1 — the whole point is that it does not"
else
    ok "A6 RW_PART_ROLES still does not contain p1"
fi

# shellcheck source=../lib/rw-identify.sh
. "$TREE/lib/rw-identify.sh"
# shellcheck source=../lib/rw-usbpower.sh
. "$LIB"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "B. rw_usbpower_classify — four outcomes, and 'unknown' is a refusal"
# ═══════════════════════════════════════════════════════════════════════════
set +e
b_out=$(rw_usbpower_classify "$RW_UIMAGE_VENDOR_MD5");  b_rc=$?
eq "$b_out" "vendor" "B1 the vendor md5 classifies as vendor"
rc_is "$b_rc" 0      "B2 ...and returns 0"
b_out=$(rw_usbpower_classify "$RW_UIMAGE_POWER_MD5"); b_rc=$?
eq "$b_out" "power" "B3 the 500 mA md5 classifies as power"
rc_is "$b_rc" 0       "B4 ...and returns 0"
b_out=$(rw_usbpower_classify "00000000000000000000000000000000"); b_rc=$?
eq "$b_out" "unknown" "B5 a fourth md5 classifies as unknown"
rc_is "$b_rc" 1       "B6 ...and returns 1, so a caller cannot fall through to patching"
b_out=$(rw_usbpower_classify ""); b_rc=$?
eq "$b_out" "unknown" "B7 an empty md5 classifies as unknown"
rc_is "$b_rc" 1       "B8 ...and returns 1"
# ── the both-patched state, and the target selector ──
# ⚠️ B9 is the case the two-state gate could not have: a power-only unit asked for
# the mode patch used to hit the `patched` early return and report success having
# written nothing.
b_out=$(rw_usbpower_classify "$RW_UIMAGE_BOTH_MD5"); b_rc=$?
eq "$b_out" "both" "B9 the 500 mA + host-mode md5 classifies as both, not unknown"
rc_is "$b_rc" 0     "B10 ...and returns 0"
eq "$(RW_USBPOWER_WITH_MODE= rw_usbpower_want)"  "power" "B11 the default want is power — the mode patch is opt-in"
eq "$(RW_USBPOWER_WITH_MODE=0 rw_usbpower_want)" "power" "B12 ...and 0 is off, not 'set'"
eq "$(RW_USBPOWER_WITH_MODE=1 rw_usbpower_want)" "both"  "B13 RW_USBPOWER_WITH_MODE=1 asks for both"
eq "$(rw_usbpower_target_md5 power)" "$RW_UIMAGE_POWER_MD5" "B14 the power target is the 500 mA md5"
eq "$(rw_usbpower_target_md5 both)"  "$RW_UIMAGE_BOTH_MD5"  "B15 the both target is the host-mode md5"
b_out=$(rw_usbpower_target_md5 sideways); b_rc=$?
eq "$b_out" ""  "B16 a word that is not a state yields no md5"
rc_is "$b_rc" 1 "B17 ...and returns 1, so a caller cannot compare against the empty string"
eq "$(RW_USBPOWER_WITH_MODE=1 rw_usbpower_target_md5)" "$RW_UIMAGE_BOTH_MD5" \
    "B18 with no argument the target follows rw_usbpower_want"
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "C. verify_uimage.py — the validator, and every way it must FAIL"
# ═══════════════════════════════════════════════════════════════════════════
#
# Written before the library cases on purpose: a validator that cannot fail is
# indistinguishable from a file that is always valid, and steps 5, 7 and 9 of the
# sequence all rest on this one program.
python3 "$MAKE_IMG" "$W/vendor.img" >/dev/null || bad "C0 harness: could not build the fixture"
[ -s "$W/vendor.img" ] || bad "C0 harness: the fixture is empty"

set +e
c_out=$(python3 "$TOOLS/verify_uimage.py" "$W/vendor.img" 2>&1); c_rc=$?
rc_is "$c_rc" 0 "C1 a well-formed synthetic uImage verifies clean"
says "$c_out" 'magic=27051956 OK'  "C2 ...reporting the magic"
says "$c_out" 'header=[0-9a-f]+/[0-9a-f]+ OK' "C3 ...the header CRC"
says "$c_out" 'data=[0-9a-f]+/[0-9a-f]+ OK'   "C4 ...the data CRC"
says "$c_out" 'power=0x32 \(50\) 100mA OK'    "C5 ...and the vendor power value"

# Each sabotage is one flag of the fixture builder, and each must fail EXACTLY the
# check it names — a fixture that trips two is the negative control for neither.
for pair in "--break-dcrc:data=[0-9a-f]+/[0-9a-f]+ BAD:C6 a flipped payload byte fails the data CRC" \
            "--break-hcrc:header=[0-9a-f]+/[0-9a-f]+ BAD:C7 a changed header byte fails the header CRC" \
            "--bad-magic:magic=[0-9a-f]+ BAD:C8 a wrong magic is refused as not-a-uImage" \
            "--bad-size:size=[0-9]+/[0-9]+ BAD:C9 a lying ih_size is caught" \
            "--no-usb-node:power=absent BAD:C10 an image with no usb_otg_hs node is refused, not guessed"; do
    flag="${pair%%:*}"; rest="${pair#*:}"; re="${rest%%:*}"; desc="${rest#*:}"
    python3 "$MAKE_IMG" "$W/sab.img" "$flag" >/dev/null
    c_out=$(python3 "$TOOLS/verify_uimage.py" "$W/sab.img" 2>&1); c_rc=$?
    if [ "$c_rc" -eq 0 ]; then bad "$desc — verify_uimage.py exited 0"
    else says "$c_out" "$re" "$desc"; fi
done

# --expect-power turns the reading into an assertion, which is what step 5 needs.
c_out=$(python3 "$TOOLS/verify_uimage.py" "$W/vendor.img" --expect-power 0xfa 2>&1); c_rc=$?
if [ "$c_rc" -eq 0 ]; then bad "C11 --expect-power 0xfa must fail on the 0x32 image"
else ok "C11 --expect-power 0xfa fails on the vendor image"; fi
c_out=$(python3 "$TOOLS/verify_uimage.py" "$W/vendor.img" --expect-power 0x32 2>&1); c_rc=$?
rc_is "$c_rc" 0 "C12 --expect-power 0x32 passes on the vendor image"
c_out=$(python3 "$TOOLS/verify_uimage.py" "$W/no-such-file" 2>&1); c_rc=$?
if [ "$c_rc" -eq 0 ]; then bad "C13 a missing file must not verify"
else ok "C13 a missing file is a failure, not a pass"; fi
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "D. patch_dtb.py — derives the image, and refuses everything else"
# ═══════════════════════════════════════════════════════════════════════════
set +e
d_out=$(python3 "$TOOLS/patch_dtb.py" "$W/vendor.img" "$W/patched.img" 2>&1); d_rc=$?
rc_is "$d_rc" 0 "D1 the vendor fixture patches"
says "$d_out" 'power at 0x[0-9a-f]+ = 0x32' "D2 ...having read 0x32 first"
says "$d_out" 'power now 0xfa'              "D3 ...and written 0xfa"
d_out=$(python3 "$TOOLS/verify_uimage.py" "$W/patched.img" --expect-power 0xfa 2>&1); d_rc=$?
rc_is "$d_rc" 0 "D4 the patched output verifies, both CRCs and the value"
# ⚠️ The CRC ORDER is the whole reason D4 can fail: uimage_fix_crcs writes the data
# CRC into the header and only then signs the header. The other order signs a stale
# header, U-Boot refuses the image, and the device does not boot at all.
if [ "$(stat -c %s "$W/vendor.img")" = "$(stat -c %s "$W/patched.img")" ]; then
    ok "D5 the patch changes no file length — it is a value edit, not a rewrite"
else
    bad "D5 the patched image changed size"
fi
d_n=$(cmp -l "$W/vendor.img" "$W/patched.img" | wc -l)
eq "$d_n" "9" "D6 exactly 9 bytes differ (header CRC 4, data CRC 4, the value 1)"

# Idempotence and the refusals.
d_out=$(python3 "$TOOLS/patch_dtb.py" "$W/patched.img" "$W/again.img" 2>&1); d_rc=$?
says "$d_out" 'already|0xfa' "D7 an already-patched input is recognised, not patched twice"
python3 "$MAKE_IMG" "$W/odd.img" --power 0x64 >/dev/null
d_out=$(python3 "$TOOLS/patch_dtb.py" "$W/odd.img" "$W/odd-out.img" 2>&1); d_rc=$?
if [ "$d_rc" -eq 0 ]; then bad "D8 an unexpected power value must be refused, not overwritten"
else ok "D8 an unexpected power value (0x64) is refused rather than overwritten"; fi
if [ -f "$W/odd-out.img" ]; then bad "D9 the refusal still wrote an output file"
else ok "D9 the refusal wrote no output file"; fi
python3 "$MAKE_IMG" "$W/badcrc.img" --break-dcrc >/dev/null
d_out=$(python3 "$TOOLS/patch_dtb.py" "$W/badcrc.img" "$W/badcrc-out.img" 2>&1); d_rc=$?
if [ "$d_rc" -eq 0 ]; then bad "D10 an input whose own CRC is wrong must be refused"
else ok "D10 an input whose own CRC is wrong is refused before patching"; fi
python3 "$MAKE_IMG" "$W/nonode.img" --no-usb-node >/dev/null
d_out=$(python3 "$TOOLS/patch_dtb.py" "$W/nonode.img" "$W/nonode-out.img" 2>&1); d_rc=$?
if [ "$d_rc" -eq 0 ]; then bad "D11 an image with no usb_otg_hs node must be refused"
else ok "D11 an image with no usb_otg_hs node is refused, not written blind"; fi
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "E. rw_usbpower_apply — the argument and transport guards"
# ═══════════════════════════════════════════════════════════════════════════
set +e
e_out=$(RWUP_XPORT=local rw_usbpower_apply "" "$W" 2>&1); e_rc=$?
rc_is "$e_rc" 1 "E1 an empty boot directory is refused"
e_out=$(RWUP_XPORT=local rw_usbpower_apply "$W" "$W/no-such-dir" 2>&1); e_rc=$?
rc_is "$e_rc" 1 "E2 a work directory that does not exist is refused"
says "$e_out" 'work directory' "E3 ...and says which argument was wrong"
e_out=$(RWUP_XPORT=carrier-pigeon rw_usbpower_apply "$W" "$W" 2>&1); e_rc=$?
rc_is "$e_rc" 1 "E4 an unknown transport is refused"
says "$e_out" "local.*ssh|ssh.*local" "E5 ...naming the two that exist"
e_out=$(RWUP_XPORT=ssh RWUP_TARGET="" rw_usbpower_apply "$W" "$W" 2>&1); e_rc=$?
rc_is "$e_rc" 1 "E6 the ssh transport with no target is refused"
mkdir -p "$W/empty-boot"
e_out=$(RWUP_XPORT=local rw_usbpower_apply "$W/empty-boot" "$W" 2>&1); e_rc=$?
rc_is "$e_rc" 1 "E7 a boot directory with no uImage-system is refused"
says "$e_out" 'not a mounted p1' "E8 ...and says it is not a mounted p1"
e_out=$(rw_usbpower_apply_offline "$W/empty-boot" "$W" 2>&1); e_rc=$?
rc_is "$e_rc" 1 "E9 rw_usbpower_apply_offline refuses a directory that is not a boot tree"
says "$e_out" 'mlo|u-boot|not a mounted p1' "E10 ...naming the markers it wanted"
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "F. the md5 gate's four outcomes, over the local transport"
# ═══════════════════════════════════════════════════════════════════════════
#
# From here on the three constants are the FIXTURE's, for the reason in the header.
# The expected md5s are computed with the REAL patch_dtb.py, so a sabotaged
# copy that produces different bytes is caught rather than accommodated.
python3 "$REAL_TOOLS/patch_dtb.py" "$W/vendor.img" "$W/expected.img" >/dev/null 2>&1 \
    || bad "F0 harness: the real patch_dtb.py could not build the expected image"
python3 "$REAL_TOOLS/patch_dtb.py" --mode "$W/vendor.img" "$W/expected-both.img" >/dev/null 2>&1 \
    || bad "F0 harness: the real patch_dtb.py --mode could not build the expected image"
RW_UIMAGE_VENDOR_MD5="$(md5of "$W/vendor.img")"
RW_UIMAGE_POWER_MD5="$(md5of "$W/expected.img")"
RW_UIMAGE_BOTH_MD5="$(md5of "$W/expected-both.img")"
F_DUPES=$(printf '%s\n' "$RW_UIMAGE_VENDOR_MD5" "$RW_UIMAGE_POWER_MD5" "$RW_UIMAGE_BOTH_MD5" \
    | sort | uniq -d | wc -l)
[ -n "$RW_UIMAGE_VENDOR_MD5" ] && [ -n "$RW_UIMAGE_BOTH_MD5" ] && [ "$F_DUPES" -eq 0 ] \
    || bad "F0 harness: the three fixture md5s are missing or not all distinct"

# fresh_boot <dir> [<image>] — a directory that looks like a mounted p1.
# mlo and u-boot.bin are present because rw_is_boot_tree requires all three, and a
# fixture that satisfied only the one under test would not reach the offline half.
fresh_boot() {
    rm -rf "$1"; mkdir -p "$1"
    cp "${2:-$W/vendor.img}" "$1/$RW_UIMAGE_NAME"
    : > "$1/mlo"; : > "$1/u-boot.bin"
}
fresh_work() { rm -rf "$W/work"; mkdir -p "$W/work"; echo "$W/work"; }

set +e
# ── vendor: patch it ──
fresh_boot "$W/b1"
f_out=$(RWUP_XPORT=local rw_usbpower_apply "$W/b1" "$(fresh_work)" 2>&1); f_rc=$?
rc_is "$f_rc" 0 "F1 a vendor image is patched and the call succeeds"
eq "$(md5of "$W/b1/$RW_UIMAGE_NAME")" "$RW_UIMAGE_POWER_MD5" "F2 uImage-system now carries 500 mA"
eq "$(md5of "$W/b1/$RW_UIMAGE_BACKUP")" "$RW_UIMAGE_VENDOR_MD5" "F3 the vendor image is backed up"
says "$f_out" 'verified by re-reading' "F4 ...and the result was re-read from the card, not assumed"
says "$f_out" 'reboot is required'     "F5 ...and the reboot requirement is stated"
if [ -f "$W/b1/$RW_UIMAGE_NAME.new" ]; then bad "F6 the .new staging file was left behind"
else ok "F6 the .new staging file is gone"; fi

# ── patched: idempotent, and it must not rewrite ──
f_before=$(md5of "$W/b1/$RW_UIMAGE_NAME")
f_out=$(RWUP_XPORT=local rw_usbpower_apply "$W/b1" "$(fresh_work)" 2>&1); f_rc=$?
rc_is "$f_rc" 0 "F7 a second run over the same card succeeds (idempotent)"
says "$f_out" 'already|nothing to do' "F8 ...saying there was nothing to do"
eq "$(md5of "$W/b1/$RW_UIMAGE_NAME")" "$f_before" "F9 ...and the file was not rewritten"

# ── unknown: refuse, and write nothing at all ──
python3 "$MAKE_IMG" "$W/third.img" --power 0x64 >/dev/null
fresh_boot "$W/b2" "$W/third.img"
f_before=$(md5of "$W/b2/$RW_UIMAGE_NAME")
f_out=$(RWUP_XPORT=local rw_usbpower_apply "$W/b2" "$(fresh_work)" 2>&1); f_rc=$?
rc_is "$f_rc" 1 "F10 a third md5 is REFUSED"
says "$f_out" 'REFUSING' "F11 ...loudly"
eq "$(md5of "$W/b2/$RW_UIMAGE_NAME")" "$f_before" "F12 ...and uImage-system is untouched"
if [ -e "$W/b2/$RW_UIMAGE_BACKUP" ]; then bad "F13 a refused run still created a backup"
else ok "F13 a refused run created no backup"; fi
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "G. the dry run reports the verdict and writes nothing"
# ═══════════════════════════════════════════════════════════════════════════
set +e
fresh_boot "$W/b3"
g_before=$(md5of "$W/b3/$RW_UIMAGE_NAME")
g_out=$(RW_USBPOWER_DRY=1 RWUP_XPORT=local rw_usbpower_apply "$W/b3" "$(fresh_work)" 2>&1); g_rc=$?
rc_is "$g_rc" 0 "G1 a dry run over a vendor image succeeds"
says "$g_out" 'would patch'   "G2 ...saying what it would patch"
says "$g_out" 'would back up' "G3 ...and what it would back up"
eq "$(md5of "$W/b3/$RW_UIMAGE_NAME")" "$g_before" "G4 ...and changed nothing"
if [ -e "$W/b3/$RW_UIMAGE_BACKUP" ]; then bad "G5 a dry run created a backup"
else ok "G5 a dry run created no backup"; fi
# A dry run over an unknown image must still refuse: "preview" is not "ignore".
fresh_boot "$W/b4" "$W/third.img"
g_out=$(RW_USBPOWER_DRY=1 RWUP_XPORT=local rw_usbpower_apply "$W/b4" "$(fresh_work)" 2>&1); g_rc=$?
rc_is "$g_rc" 1 "G6 a dry run over an unknown md5 still refuses"
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "H. the backup is verified BEFORE the original is touched"
# ═══════════════════════════════════════════════════════════════════════════
#
# The single property the whole sequence is built around: p1 has no recovery over
# SSH, so a backup nobody checked is not a backup. A pre-existing file under that
# name which is NOT the vendor image is the case that matters — it looks like an
# undo and is not one.
set +e
fresh_boot "$W/b5"
printf 'this is not a kernel\n' > "$W/b5/$RW_UIMAGE_BACKUP"
h_before=$(md5of "$W/b5/$RW_UIMAGE_NAME")
h_out=$(RWUP_XPORT=local rw_usbpower_apply "$W/b5" "$(fresh_work)" 2>&1); h_rc=$?
rc_is "$h_rc" 1 "H1 a pre-existing backup that is not the vendor image is refused"
says "$h_out" 'no working undo|not the vendor image' "H2 ...saying there would be no undo"
eq "$(md5of "$W/b5/$RW_UIMAGE_NAME")" "$h_before" "H3 ...and uImage-system is untouched"
# A pre-existing CORRECT backup is reused, not overwritten — the second run of a
# re-commissioned card must not be a refusal.
fresh_boot "$W/b6"
cp "$W/vendor.img" "$W/b6/$RW_UIMAGE_BACKUP"
h_out=$(RWUP_XPORT=local rw_usbpower_apply "$W/b6" "$(fresh_work)" 2>&1); h_rc=$?
rc_is "$h_rc" 0 "H4 a pre-existing correct backup is accepted"
eq "$(md5of "$W/b6/$RW_UIMAGE_NAME")" "$RW_UIMAGE_POWER_MD5" "H5 ...and the patch proceeds"
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "I. rollback — a bad write restores the vendor kernel and says so"
# ═══════════════════════════════════════════════════════════════════════════
#
# Fault injection by overriding ONE private transport primitive, inside a subshell
# so nothing leaks into later groups. This is the only way to reach step 10: the
# local transport is a cp, and a cp does not fail on demand. What is exercised is
# the real rollback code, not a re-implementation of it.
set +e
fresh_boot "$W/b7"
i_out=$( ( _rwup_mv() { printf 'corrupted by the test\n' > "$2"; rm -f "$1"; }
           RWUP_XPORT=local rw_usbpower_apply "$W/b7" "$(fresh_work)" ) 2>&1 ); i_rc=$?
rc_is "$i_rc" 1 "I1 a write that lands corrupted fails the call"
says "$i_out" 'reads back as' "I2 ...caught by re-reading, not by trusting the write"
says "$i_out" 'Restor'        "I3 ...and the vendor kernel is restored"
eq "$(md5of "$W/b7/$RW_UIMAGE_NAME")" "$RW_UIMAGE_VENDOR_MD5" "I4 uImage-system is the vendor image again"
says_not "$i_out" 'THE RESTORE ALSO FAILED' "I5 ...and it does not claim the restore failed"

# Both broken: the loudest branch in the file. The backup is pre-placed so that
# _rwup_cp is only reached by the ROLLBACK — otherwise the run would fail at step 6
# and never get here.
fresh_boot "$W/b8"
cp "$W/vendor.img" "$W/b8/$RW_UIMAGE_BACKUP"
i_out=$( ( _rwup_mv() { printf 'corrupted by the test\n' > "$2"; rm -f "$1"; }
           _rwup_cp() { printf 'also corrupted\n' > "$2"; }
           RWUP_XPORT=local rw_usbpower_apply "$W/b8" "$(fresh_work)" ) 2>&1 ); i_rc=$?
rc_is "$i_rc" 1 "I6 a failed rollback also fails the call"
says "$i_out" 'THE RESTORE ALSO FAILED' "I7 ...and says p1 is in an unknown state"
says "$i_out" 'DO NOT BOOT'             "I8 ...and says not to boot the unit"
says "$i_out" 'card reader|dd'          "I9 ...and names the only remedy left"
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "J. both transports, one sequence — the group-E comparison this file owes"
# ═══════════════════════════════════════════════════════════════════════════
#
# tests/rw_provision_test.sh group E compares the offline and online executors over
# one plan, and it is the check that catches C12-style drift. The p1 step is the one
# thing it cannot cover, because it is not a provision rule. So it is covered here:
# the same sequence over RWUP_XPORT=local and RWUP_XPORT=ssh must reach the same end
# state, byte for byte.
#
# The ssh side is driven by two stubs that really perform every far-side operation,
# locally. They are the transport the library already documents $RW_SSH/$RW_SCP for;
# nothing about the sequence itself is stubbed.
cat > "$W/stub-ssh" <<'STUB'
#!/bin/sh
# ssh stub: drop the target, run the command. Every far-side operation is real.
shift
sh -c "$*"
STUB
cat > "$W/stub-scp" <<'STUB'
#!/bin/sh
# scp stub: drop -q, strip the "root@fake:" prefix, copy locally.
src=""; dst=""
for a in "$@"; do
    [ "$a" = "-q" ] && continue
    case "$a" in root@fake:*) a=${a#root@fake:} ;; esac
    if [ -z "$src" ]; then src="$a"; else dst="$a"; fi
done
cp "$src" "$dst"
STUB
chmod +x "$W/stub-ssh" "$W/stub-scp"

set +e
fresh_boot "$W/j-local"
RWUP_XPORT=local rw_usbpower_apply "$W/j-local" "$(fresh_work)" >"$W/j-local.log" 2>&1
j_local_rc=$?
fresh_boot "$W/j-ssh"
RW_SSH="$W/stub-ssh" RW_SCP="$W/stub-scp" RWUP_XPORT=ssh RWUP_TARGET="root@fake" \
    rw_usbpower_apply "$W/j-ssh" "$(fresh_work)" >"$W/j-ssh.log" 2>&1
j_ssh_rc=$?
rc_is "$j_local_rc" 0 "J1 the local transport patches the fixture"
rc_is "$j_ssh_rc"   0 "J2 the ssh transport patches the fixture"
if cmp -s "$W/j-local/$RW_UIMAGE_NAME" "$W/j-ssh/$RW_UIMAGE_NAME"; then
    ok "J3 both transports leave a byte-identical uImage-system"
else
    bad "J3 the two transports left different uImage-system bytes"
fi
if cmp -s "$W/j-local/$RW_UIMAGE_BACKUP" "$W/j-ssh/$RW_UIMAGE_BACKUP"; then
    ok "J4 both transports leave a byte-identical backup"
else
    bad "J4 the two transports left different backups"
fi
# The prose must agree too: a difference there is a second implementation forming.
if diff -q <(sed 's|'"$W"'/j-local|BOOT|g' "$W/j-local.log") \
           <(sed 's|'"$W"'/j-ssh|BOOT|g'   "$W/j-ssh.log") >/dev/null; then
    ok "J5 both transports print the same sequence of verdicts"
else
    bad "J5 the two transports narrate the sequence differently"
    diff <(sed 's|'"$W"'/j-local|BOOT|g' "$W/j-local.log") \
         <(sed 's|'"$W"'/j-ssh|BOOT|g'   "$W/j-ssh.log") | sed 's/^/        /' | head -8
fi
# And the ssh half must refuse a third md5 exactly as the local half did.
fresh_boot "$W/j-ssh2" "$W/third.img"
j_out=$(RW_SSH="$W/stub-ssh" RW_SCP="$W/stub-scp" RWUP_XPORT=ssh RWUP_TARGET="root@fake" \
    rw_usbpower_apply "$W/j-ssh2" "$(fresh_work)" 2>&1); j_rc=$?
rc_is "$j_rc" 1 "J6 the ssh transport refuses a third md5 too"
says "$j_out" 'REFUSING' "J7 ...with the same refusal"
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "K. rw_usbpower_apply_offline — the boot-tree gate, and it unsets its state"
# ═══════════════════════════════════════════════════════════════════════════
set +e
fresh_boot "$W/k1"
k_out=$(rw_usbpower_apply_offline "$W/k1" "$(fresh_work)" 2>&1); k_rc=$?
rc_is "$k_rc" 0 "K1 the offline entry point patches a real-looking boot tree"
eq "$(md5of "$W/k1/$RW_UIMAGE_NAME")" "$RW_UIMAGE_POWER_MD5" "K2 ...and the image is patched"
# A tree with uImage-system but no mlo is NOT p1 — the marker set is the check that
# a mount landed on the right partition.
rm -rf "$W/k2"; mkdir -p "$W/k2"; cp "$W/vendor.img" "$W/k2/$RW_UIMAGE_NAME"
k_out=$(rw_usbpower_apply_offline "$W/k2" "$(fresh_work)" 2>&1); k_rc=$?
rc_is "$k_rc" 1 "K3 uImage-system alone is not a boot tree — mlo and u-boot.bin are required"
eq "$(md5of "$W/k2/$RW_UIMAGE_NAME")" "$RW_UIMAGE_VENDOR_MD5" "K4 ...and it wrote nothing"
k_out=$(rw_usbpower_apply_offline "" "$(fresh_work)" 2>&1); k_rc=$?
rc_is "$k_rc" 1 "K5 an empty boot mount is refused"
# The transport globals must not survive a call, or a later caller inherits a
# transport it never asked for.
eq "${RWUP_XPORT:-unset}" "unset" "K6 RWUP_XPORT is unset after the offline call"
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "L. the mode property — locating it, and patching it (B32)"
# ═══════════════════════════════════════════════════════════════════════════
#
# `mode = <3>` is MUSB_PORT_MODE_DUAL_ROLE on a kernel built with no gadget
# support, which costs the SESSION bit; B32 wants <1> (MUSB_PORT_MODE_HOST).
#
# ⚠️ These cases exist because the obvious implementation is wrong in a way that
# still produces an answer. Measured on `.188`'s live blob 2026-08-14: dtc emits
# NO standalone `mode` entry in the strings block — `usb_mode` sits at nameoff
# 0x3e0 and `mode` at 0x3e4, i.e. `mode` is the SUFFIX of `usb_mode`. The fixture
# reproduces that, and also puts a decoy 4-byte `usb_mode` property in a
# different node. So a locator that searches the strings block for b"mode\0",
# or that does not scope to the usb_otg_hs node, lands on a plausible wrong
# offset instead of missing cleanly. L3 is the case that catches it.
set +e

# --- the locator, exercised directly, because the offset is the whole risk
cat > "$W/locate.py" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
from uimage import find_mode_offset, find_power_offset, MODE_VENDOR, MODE_WANTED
data = bytearray(open(sys.argv[2], "rb").read())
dtb_m, off_m = find_mode_offset(data)
dtb_p, off_p = find_power_offset(data)
val = int.from_bytes(data[off_m:off_m + 4], "big")
# The decoy usb_mode property holds 1 and lives in another node. Report the
# comparison as a POSITIVE assertion, so a crash fails the case instead of
# passing it by producing no output for says_not to miss.
decoy = data.find((1).to_bytes(4, "big"), dtb_m, off_m)
print("dtb %d %d  mode_off %d  mode_val %d  vendor %d wanted %d  same_dtb %s"
      % (dtb_m, dtb_p, off_m, val, MODE_VENDOR, MODE_WANTED, dtb_m == dtb_p))
print("not_decoy %s" % (val == MODE_VENDOR and off_m != decoy))
PY
l_out=$(python3 "$W/locate.py" "$TOOLS" "$W/vendor.img" 2>&1); l_rc=$?
rc_is "$l_rc" 0 "L1 find_mode_offset exists and locates the property"
says "$l_out" 'mode_val 3'    "L2 ...reading the vendor value 3 (MUSB_PORT_MODE_DUAL_ROLE)"
says "$l_out" 'vendor 3 wanted 1' "L2b ...and MODE_VENDOR/MODE_WANTED are 3 and 1"
says "$l_out" 'not_decoy True' "L3 ...not the decoy usb_mode in another node"
says "$l_out" 'same_dtb True'  "L4 mode and power are found in the SAME dtb candidate"

# --- patch_dtb.py --mode
python3 "$MAKE_IMG" "$W/l-vendor.img" >/dev/null
l_out=$(python3 "$TOOLS/patch_dtb.py" --mode "$W/l-vendor.img" "$W/l-both.img" 2>&1); l_rc=$?
rc_is "$l_rc" 0 "L5 patch_dtb.py --mode patches the mode property"
says "$l_out" 'mode at 0x[0-9a-f]+ = 0x03' "L6 ...having read 0x03 first"
says "$l_out" 'mode now 0x01'              "L7 ...and written 0x01"
# ⚠️ --mode must patch BOTH, because a p1 write is one shot and the two-patch
# end state has to be a single md5. A mode-only image would be a fourth state.
says "$l_out" 'power now 0xfa'             "L8 ...and the power patch too, in one pass"
l_n=$(cmp -l "$W/l-vendor.img" "$W/l-both.img" | wc -l)
eq "$l_n" "10" "L9 exactly 10 bytes differ (2 CRCs = 8, power 1, mode 1)"
l_out=$(python3 "$TOOLS/verify_uimage.py" "$W/l-both.img" --expect-power 0xfa --expect-mode 0x01 2>&1)
rc_is "$?" 0 "L10 the doubly-patched image verifies, both values and both CRCs"
# ...and the default (no --mode) must still be power-only, or every existing
# call site silently changes behaviour and D6's 9-byte diff becomes a lie.
python3 "$MAKE_IMG" "$W/l-p.img" >/dev/null
python3 "$TOOLS/patch_dtb.py" "$W/l-p.img" "$W/l-ponly.img" >/dev/null 2>&1
l_n=$(cmp -l "$W/l-p.img" "$W/l-ponly.img" | wc -l)
eq "$l_n" "9" "L11 without --mode the diff is still 9 bytes — the default is unchanged"
l_out=$(python3 "$TOOLS/verify_uimage.py" "$W/l-ponly.img" --expect-mode 0x01 2>&1); l_rc=$?
rc_is "$l_rc" 1 "L12 ...and such an image fails --expect-mode 0x01"

# --- the refusals, mirroring D8/D11
l_out=$(python3 "$TOOLS/patch_dtb.py" --mode "$W/l-both.img" "$W/l-again.img" 2>&1); l_rc=$?
# A fully-patched input is caught by the POWER check, which runs first — so this
# case asserts recognition, not the mode wording.
rc_is "$l_rc" 1 "L13 an already fully-patched input is refused, not patched twice"
says "$l_out" 'already' "L13b ...saying so"
# The mode branch's own already-patched refusal needs an input the tool cannot
# itself produce: vendor power, mode already 1.
python3 "$MAKE_IMG" "$W/l-mp.img" --mode 0x01 >/dev/null
l_out=$(python3 "$TOOLS/patch_dtb.py" --mode "$W/l-mp.img" "$W/l-mp-out.img" 2>&1); l_rc=$?
rc_is "$l_rc" 1 "L13c a vendor-power image whose mode is already 1 is refused"
says "$l_out" 'mode 0x01|mode.*already|already.*mode' "L13d ...naming mode, not power"
if [ -f "$W/l-mp-out.img" ]; then bad "L13e that refusal still wrote an output file"
else ok "L13e that refusal wrote no output file"; fi
python3 "$MAKE_IMG" "$W/l-oddmode.img" --mode 0x07 >/dev/null
l_out=$(python3 "$TOOLS/patch_dtb.py" --mode "$W/l-oddmode.img" "$W/l-om-out.img" 2>&1); l_rc=$?
if [ "$l_rc" -eq 0 ]; then bad "L14 an unexpected mode value must be refused, not overwritten"
else ok "L14 an unexpected mode value (0x07) is refused rather than overwritten"; fi
if [ -f "$W/l-om-out.img" ]; then bad "L15 the refusal still wrote an output file"
else ok "L15 the refusal wrote no output file"; fi
python3 "$MAKE_IMG" "$W/l-nomode.img" --no-mode >/dev/null
l_out=$(python3 "$TOOLS/patch_dtb.py" --mode "$W/l-nomode.img" "$W/l-nm-out.img" 2>&1); l_rc=$?
if [ "$l_rc" -eq 0 ]; then bad "L16 an image with no mode property must be refused under --mode"
else ok "L16 an image with no mode property is refused under --mode"; fi
# ...but such an image is still a legal POWER patch, so the default must survive it.
l_out=$(python3 "$TOOLS/patch_dtb.py" "$W/l-nomode.img" "$W/l-nm-p.img" 2>&1); l_rc=$?
rc_is "$l_rc" 0 "L17 ...and it still patches power without --mode"
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "M. the mode patch through the SEQUENCE — the four-state gate (B32)"
# ═══════════════════════════════════════════════════════════════════════════
#
# Group L drives patch_dtb.py --mode directly. This group drives rw_usbpower_apply,
# which is where the defect lived: with a two-element md5 set, a power-only unit
# asked for the mode patch hit the `patched` arm and returned 0 having written
# NOTHING. M3-M5 are that case. Every unit already commissioned is in that state.
#
# fresh_boot leaves no backup, so a re-derivation fixture has to place one; that is
# deliberate, because "no pristine backup" is a reachable field state and M8-M10
# are the refusal it must produce.
with_backup() { cp "$W/vendor.img" "$1/$RW_UIMAGE_BACKUP"; }

set +e
# ── vendor + opt-in: lands the both-patched image ──
fresh_boot "$W/m1"
m_out=$(RW_USBPOWER_WITH_MODE=1 RWUP_XPORT=local rw_usbpower_apply "$W/m1" "$(fresh_work)" 2>&1); m_rc=$?
rc_is "$m_rc" 0 "M1 a vendor image with RW_USBPOWER_WITH_MODE=1 is patched"
eq "$(md5of "$W/m1/$RW_UIMAGE_NAME")" "$RW_UIMAGE_BOTH_MD5" "M2 ...to the 500 mA + host-mode image"
# md5 equality is evidence about bytes; this is evidence about the property.
m_out2=$(python3 "$REAL_TOOLS/verify_uimage.py" "$W/m1/$RW_UIMAGE_NAME" \
    --expect-power 0xfa --expect-mode 0x01 2>&1); m_rc=$?
rc_is "$m_rc" 0 "M2b ...and the image on the card really carries power 0xfa and mode 0x01"
eq "$(md5of "$W/m1/$RW_UIMAGE_BACKUP")" "$RW_UIMAGE_VENDOR_MD5" "M2c ...with the vendor image backed up"

# ── ⚠️ THE DEFECT: a power-only unit asked for both must not report success ──
fresh_boot "$W/m2" "$W/expected.img"
with_backup "$W/m2"
m_out=$(RW_USBPOWER_WITH_MODE=1 RWUP_XPORT=local rw_usbpower_apply "$W/m2" "$(fresh_work)" 2>&1); m_rc=$?
rc_is "$m_rc" 0 "M3 a power-only card asked for both succeeds"
eq "$(md5of "$W/m2/$RW_UIMAGE_NAME")" "$RW_UIMAGE_BOTH_MD5" \
    "M4 ...and the mode patch IS applied — not an early 'nothing to do' return"
says "$m_out" 're-deriving' "M5 ...saying it re-derived from the backup rather than chaining"
says "$m_out" 'verified by re-reading' "M6 ...and the result was re-read from the card"
eq "$(md5of "$W/m2/$RW_UIMAGE_BACKUP")" "$RW_UIMAGE_VENDOR_MD5" "M7 ...leaving the backup pristine"

# ── the same transition with no usable backup: refuse, and write nothing ──
fresh_boot "$W/m3" "$W/expected.img"
m_before=$(md5of "$W/m3/$RW_UIMAGE_NAME")
m_out=$(RW_USBPOWER_WITH_MODE=1 RWUP_XPORT=local rw_usbpower_apply "$W/m3" "$(fresh_work)" 2>&1); m_rc=$?
rc_is "$m_rc" 1 "M8 a power-only card with NO backup cannot be re-derived, so it is refused"
says "$m_out" 'DERIVED|pristine|vendor kernel' "M9 ...explaining that a pristine vendor image is required"
eq "$(md5of "$W/m3/$RW_UIMAGE_NAME")" "$m_before" "M10 ...and uImage-system is untouched"
fresh_boot "$W/m4" "$W/expected.img"
echo "not a kernel" > "$W/m4/$RW_UIMAGE_BACKUP"
m_before=$(md5of "$W/m4/$RW_UIMAGE_NAME")
m_out=$(RW_USBPOWER_WITH_MODE=1 RWUP_XPORT=local rw_usbpower_apply "$W/m4" "$(fresh_work)" 2>&1); m_rc=$?
rc_is "$m_rc" 1 "M11 a power-only card with a BOGUS backup is refused too"
eq "$(md5of "$W/m4/$RW_UIMAGE_NAME")" "$m_before" "M12 ...and wrote nothing"

# ── both + opt-in: idempotent ──
m_before=$(md5of "$W/m2/$RW_UIMAGE_NAME")
m_out=$(RW_USBPOWER_WITH_MODE=1 RWUP_XPORT=local rw_usbpower_apply "$W/m2" "$(fresh_work)" 2>&1); m_rc=$?
rc_is "$m_rc" 0 "M13 a second run over the both-patched card succeeds"
says "$m_out" 'already|nothing to do' "M14 ...saying there was nothing to do"
eq "$(md5of "$W/m2/$RW_UIMAGE_NAME")" "$m_before" "M15 ...and did not rewrite it"

# ── the undo: a both-patched card asked for power re-derives DOWNWARD ──
# The remedy if panel item 10 fails, and the reason the transition is expressed as
# "derive the wanted image from the backup" rather than "apply a patch".
m_out=$(RWUP_XPORT=local rw_usbpower_apply "$W/m2" "$(fresh_work)" 2>&1); m_rc=$?
rc_is "$m_rc" 0 "M16 a both-patched card asked for power succeeds"
eq "$(md5of "$W/m2/$RW_UIMAGE_NAME")" "$RW_UIMAGE_POWER_MD5" "M17 ...and is the power-only image again"

# ── the opt-in guarantee, at the sequence level ──
# ⚠️ B32 item 10 is unverified on hardware, so a default run must never produce a
# mode-patched card. L11 asserts this of the tool; this asserts it of the writer.
fresh_boot "$W/m5"
m_out=$(RWUP_XPORT=local rw_usbpower_apply "$W/m5" "$(fresh_work)" 2>&1); m_rc=$?
rc_is "$m_rc" 0 "M18 a default run over a vendor card succeeds"
eq "$(md5of "$W/m5/$RW_UIMAGE_NAME")" "$RW_UIMAGE_POWER_MD5" \
    "M19 ...and lands the POWER-ONLY image — the mode patch is opt-in"
m_out2=$(python3 "$REAL_TOOLS/verify_uimage.py" "$W/m5/$RW_UIMAGE_NAME" --expect-mode 0x01 2>&1); m_rc=$?
rc_is "$m_rc" 1 "M20 ...and that card fails --expect-mode 0x01"

# ── the dry run names both ends of the transition and writes nothing ──
fresh_boot "$W/m6" "$W/expected.img"
with_backup "$W/m6"
m_before=$(md5of "$W/m6/$RW_UIMAGE_NAME")
m_out=$(RW_USBPOWER_DRY=1 RW_USBPOWER_WITH_MODE=1 RWUP_XPORT=local \
    rw_usbpower_apply "$W/m6" "$(fresh_work)" 2>&1); m_rc=$?
rc_is "$m_rc" 0 "M21 the dry run over a power-only card asked for both succeeds"
says "$m_out" 'would patch' "M22 ...saying what it would do"
says "$m_out" "power .*-> both" "M23 ...naming the state it found and the one it wants"
eq "$(md5of "$W/m6/$RW_UIMAGE_NAME")" "$m_before" "M24 ...and changed nothing"

# ── the ssh transport reaches the same end state (group J's argument, for mode) ──
fresh_boot "$W/m7" "$W/expected.img"
with_backup "$W/m7"
m_out=$(RW_USBPOWER_WITH_MODE=1 RWUP_XPORT=ssh RWUP_TARGET="root@fake" \
    RW_SSH="$W/stub-ssh" RW_SCP="$W/stub-scp" \
    rw_usbpower_apply "$W/m7" "$(fresh_work)" 2>&1); m_rc=$?
rc_is "$m_rc" 0 "M25 the same transition over the ssh transport succeeds"
eq "$(md5of "$W/m7/$RW_UIMAGE_NAME")" "$RW_UIMAGE_BOTH_MD5" "M26 ...and reaches the same bytes"
set -e

# ═══════════════════════════════════════════════════════════════════════════
echo ""
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed, $TOTAL total"
if [ "$FAIL" -eq 0 ]; then
    echo -e "  ${GREEN}✓ all $TOTAL cases passed${NC}"
    echo ""
    exit 0
fi
echo -e "  ${RED}✗ $FAIL case(s) failed${NC}"
echo -e "  ${YELLOW}note:${NC} groups F-M override the three md5 constants with the synthetic"
echo -e "        fixture's own — see this file's header before concluding anything"
echo -e "        about the real uImage-system from a failure here."
echo ""
exit 1
