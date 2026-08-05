#!/bin/bash
#
# rw_identify_test.sh — regression for rw-identify.sh
#
# Host-only, no device, no SD card, no root. Run it:
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/rw_identify_test.sh"
#
# WHAT IT COVERS, and why each case is here rather than being obvious:
#
#   rw_is_rootfs      Every state a real card can be in — vendor-fresh, after
#                     `setup-device.sh --remove` (which deletes /opt/pv02), and
#                     stripped down to nothing but the login banner. Plus the
#                     negative control that matters: a tree that has all four
#                     files the caller EDITS and none of the vendor markers,
#                     i.e. an ordinary Linux host's own root. The detector this
#                     replaces looked up a UUID, so it could not select the dev
#                     host; a content scan could, and case 4 is what forbids it.
#
#   rw_is_card_disk   Partition tables built with sfdisk on sparse files, so the
#                     positive AND the negative control are synthetic and need
#                     no card. If the two real card images happen to be present
#                     (they are gitignored) they are checked too — one of them
#                     is the unit that made the UUID approach fail.
#
# The count at the end includes a check on the harness itself: a test file that
# silently ran zero cases reports success just as loudly as one that ran all of
# them.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=../rw-identify.sh
. "$REPO_DIR/rw-identify.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

PASS=0
FAIL=0
SKIP=0

ok()      { PASS=$((PASS + 1)); echo -e "  ${GREEN}pass${NC}  $1"; }
bad()     { FAIL=$((FAIL + 1)); echo -e "  ${RED}FAIL${NC}  $1"; }
skipped() { SKIP=$((SKIP + 1)); echo -e "  ${YELLOW}skip${NC}  $1 — $2"; }

# expect_rootfs <yes|no> <dir> <description>
expect_rootfs() {
    local want="$1" dir="$2" desc="$3" got
    if rw_is_rootfs "$dir"; then got=yes; else got=no; fi
    if [ "$got" = "$want" ]; then
        ok "$desc"
    else
        bad "$desc (expected $want, got $got)"
    fi
}

# expect_disk <yes|no> <device-or-image> <description>
expect_disk() {
    local want="$1" dev="$2" desc="$3" got
    if rw_is_card_disk "$dev"; then got=yes; else got=no; fi
    if [ "$got" = "$want" ]; then
        ok "$desc"
    else
        bad "$desc (expected $want, got $got)"
    fi
}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# ── synthetic rootfs trees ──────────────────────────────────────────────────

# The four files a caller edits. Present in every case below, including the
# negative controls, so that no case passes or fails for the wrong reason.
make_required() {
    mkdir -p "$1/etc/ssh" "$1/etc/network"
    : > "$1/etc/shadow"
    : > "$1/etc/hosts"
    : > "$1/etc/ssh/sshd_config"
    : > "$1/etc/network/interfaces"
}

echo ""
echo "rw_is_rootfs"

# 1. Vendor-fresh card: every marker present.
V="$TMP/vendor"
make_required "$V"
mkdir -p "$V/opt/pv02" "$V/opt/sbin/watchdog"
: > "$V/opt/sbin/watchdog/watchdog.sh"
echo 'SteelCase RW20 Embedded Platform (Yocto) 3.1.4 \n \l' > "$V/etc/issue"
expect_rootfs yes "$V" "vendor-fresh rootfs"

# 2. After setup-device.sh --remove: /opt/pv02 is gone, /opt/roomwizard added.
R="$TMP/removed"
make_required "$R"
mkdir -p "$R/opt/sbin/watchdog" "$R/opt/roomwizard"
: > "$R/opt/sbin/watchdog/watchdog.sh"
echo 'SteelCase RW20 Embedded Platform (Yocto) 3.1.4 \n \l' > "$R/etc/issue"
expect_rootfs yes "$R" "rootfs after --remove (no /opt/pv02)"

# 3. Stripped to the banner alone: no /opt marker of any kind survives.
B="$TMP/banner"
make_required "$B"
echo 'SteelCase RW20 Embedded Platform (Yocto) 3.1.4 \n \l' > "$B/etc/issue"
expect_rootfs yes "$B" "rootfs identified by /etc/issue alone"

# 4. THE negative control: an ordinary Linux root. Everything a caller edits is
#    present; nothing identifies it as a RoomWizard. Selecting this would mean
#    rewriting the dev host's own /etc/shadow.
H="$TMP/host"
make_required "$H"
mkdir -p "$H/opt"
echo 'Ubuntu 20.04.6 LTS \n \l' > "$H/etc/issue"
expect_rootfs no "$H" "ordinary Linux root is rejected"

# 5. A RoomWizard non-root partition (p2/p3/p5): no /etc at all.
D="$TMP/datapart"
mkdir -p "$D/cron" "$D/websign"
expect_rootfs no "$D" "RoomWizard data partition is rejected"

# 6. Vendor markers present but a file the caller edits is missing. Identity is
#    not the question here — proceeding would fail partway through.
I="$TMP/incomplete"
make_required "$I"
rm -f "$I/etc/shadow"
mkdir -p "$I/opt/pv02"
expect_rootfs no "$I" "vendor tree missing /etc/shadow is rejected"

# 7. Empty directory — an unmounted mount point.
mkdir -p "$TMP/empty"
expect_rootfs no "$TMP/empty" "empty directory is rejected"

# ── firmware description ────────────────────────────────────────────────────

echo ""
echo "rw_rootfs_firmware"
echo '20180309123456' > "$V/etc/version"
FW=$(rw_rootfs_firmware "$V")
case "$FW" in
    "SteelCase RW20 Embedded Platform (Yocto) 3.1.4 (build 20180309123456)")
        ok "firmware line reads: $FW" ;;
    *)  bad "firmware line reads: $FW" ;;
esac

# ── synthetic partition tables ──────────────────────────────────────────────

echo ""
echo "rw_is_card_disk"

if ! command -v sfdisk >/dev/null 2>&1; then
    skipped "synthetic partition tables" "sfdisk not installed"
else
    # 4 GB sparse file. sfdisk on a regular file needs no root and touches no
    # block device, so these two cases are safe to run anywhere.
    build_table() {
        local img="$1" table="$2"
        rm -f "$img"
        truncate -s 4G "$img"
        sfdisk --no-reread --no-tell-kernel "$img" >/dev/null 2>&1 <<EOF
$table
EOF
    }

    # The real RoomWizard table. p4 and p7 sizes are deliberately NOT the ones
    # from either measured card — the fingerprint must not pin them, because
    # that is where two cards of the same nominal size legitimately differ.
    RW_TABLE='label: dos
1 : start=63, size=144522, type=c, bootable
2 : start=144585, size=514080, type=83
3 : start=658665, size=498015, type=83
4 : start=1156680, size=7000000, type=5
5 : start=1156743, size=2939832, type=83
6 : start=4096638, size=2008062, type=83
7 : start=6104763, size=400000, type=82'

    if build_table "$TMP/good.img" "$RW_TABLE"; then
        expect_disk yes "$TMP/good.img" "RoomWizard layout (p4/p7 sizes differing)"
    else
        skipped "RoomWizard layout" "sfdisk could not write the table"
    fi

    # p6 moved. Everything else identical, so this fails on exactly one field —
    # if the check were a no-op returning 0, this case is what catches it.
    MOVED_TABLE=$(echo "$RW_TABLE" | sed 's/^6 : start=4096638/6 : start=4096640/')
    if build_table "$TMP/moved.img" "$MOVED_TABLE"; then
        expect_disk no "$TMP/moved.img" "p6 start moved by 2 sectors is rejected"
    else
        skipped "p6 moved" "sfdisk could not write the table"
    fi

    if build_table "$TMP/plain.img" 'label: dos
1 : start=2048, size=8000000, type=83'; then
        expect_disk no "$TMP/plain.img" "ordinary single-partition disk is rejected"
    else
        skipped "single-partition disk" "sfdisk could not write the table"
    fi

    expect_disk no "$TMP/empty" "a directory is rejected"
fi

# ── the real card images, when they are here ────────────────────────────────
#
# Both are gitignored, so this section is opportunistic. It is the only part
# that exercises a real vendor partition table, and roomvizard_new.img is the
# card that shares no filesystem UUID with the reference unit — the failure
# this whole file exists because of.

echo ""
echo "real card images (gitignored; skipped when absent)"
for img in roomwizard.img roomvizard_new.img; do
    if [ -f "$REPO_DIR/$img" ]; then
        expect_disk yes "$REPO_DIR/$img" "$img carries the RoomWizard layout"
    else
        skipped "$img" "not present in $REPO_DIR"
    fi
done

# ── result, including a check on the harness ────────────────────────────────

echo ""
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed, $SKIP skipped"

# A harness that runs nothing reports success. There are 12 non-skippable cases
# above (7 rootfs + 1 firmware + 4 disk); assert the run actually reached them
# rather than trusting that it did.
MIN_CASES=12
if [ "$TOTAL" -lt "$MIN_CASES" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: only $TOTAL cases ran, expected at least $MIN_CASES."
    echo "  Cases were skipped that cannot be skipped, or the file was truncated."
    exit 2
fi

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
echo -e "  ${GREEN}all good${NC}"
