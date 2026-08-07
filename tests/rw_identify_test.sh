#!/bin/bash
#
# rw_identify_test.sh — regression for lib/rw-identify.sh
#
# Host-only, no device, no SD card, no root. Run it:
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/rw_identify_test.sh"
#
# WHAT IT COVERS, and why each case is here rather than being obvious:
#
#   rw_is_rootfs      Every state a real card can be in — vendor-fresh, after
#                     `commissioning/provision.sh --remove` (which deletes /opt/pv02), and
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
#   rw_part_dev       The two partition-naming schemes. /dev/sdf + 6 is
#   rw_card_partitions  /dev/sdf6 but /dev/mmcblk0 + 6 is /dev/mmcblk0p6, and
#                     getting that wrong means mounting nothing at all. Plus the
#                     assertion that matters most: p1 is NOT in the role table,
#                     so no caller can reach it through these functions.
#
#   rw_host_root_disk Resolved, not guessed. Checked against findmnt independently
#                     of the implementation, and the veto is checked in both
#                     directions — a veto that always says "no" is invisible.
#
#   rw_check_card_mounts
#                     Both halves, including the one that earns its keep: four
#                     mounts in the WRONG ORDER must be rejected. A rootfs where
#                     p2 was expected is the mistake that makes every later path
#                     resolve under the wrong tree.
#
# The count at the end includes a check on the harness itself: a test file that
# silently ran zero cases reports success just as loudly as one that ran all of
# them.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=../lib/rw-identify.sh
. "$REPO_DIR/lib/rw-identify.sh"

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

# 2. After commissioning/provision.sh --remove: /opt/pv02 is gone, /opt/roomwizard added.
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

# ── the four mounts, by position ────────────────────────────────────────────

# expect_eq <want> <got> <description>
expect_eq() {
    if [ "$1" = "$2" ]; then
        ok "$3"
    else
        bad "$3 (expected '$1', got '$2')"
    fi
}

echo ""
echo "rw_part_dev"

# The scheme split. /dev/sdf6 vs /dev/mmcblk0p6: appending the number naively
# gives /dev/mmcblk06, which does not exist, so an offline tool would find no
# partitions on the device's OWN naming scheme and report "not a RoomWizard card".
expect_eq "/dev/sdf6"        "$(rw_part_dev /dev/sdf 6)"        "sd disk: /dev/sdf + 6"
expect_eq "/dev/mmcblk0p6"   "$(rw_part_dev /dev/mmcblk0 6)"    "mmcblk: a 'p' is inserted"
expect_eq "/dev/loop3p2"     "$(rw_part_dev /dev/loop3 2)"      "loop device: a 'p' is inserted"
expect_eq "/dev/nvme0n1p5"   "$(rw_part_dev /dev/nvme0n1 5)"    "nvme: a 'p' is inserted"

echo ""
echo "rw_card_partitions"

PARTS=$(rw_card_partitions /dev/mmcblk0)
expect_eq "root /dev/mmcblk0p6" "$(echo "$PARTS" | grep '^root ')"   "p6 is root"
expect_eq "data /dev/mmcblk0p2" "$(echo "$PARTS" | grep '^data ')"   "p2 is data"
expect_eq "log /dev/mmcblk0p3"  "$(echo "$PARTS" | grep '^log ')"    "p3 is log"
expect_eq "backup /dev/mmcblk0p5" "$(echo "$PARTS" | grep '^backup ')" "p5 is backup"
expect_eq "4" "$(echo "$PARTS" | grep -c .)"                         "exactly four roles"

# ⚠️ THE assertion in this section. p1 carries mlo, u-boot.bin, ctrlblock.bin and
# uImage-system; an untouched p1 is what keeps a power cycle a free undo. If it
# ever appears in the role table, every consumer gains the ability to write to it
# — so the table is checked for its absence rather than the consumers for their
# restraint. p4 (extended container) and p7 (swap) likewise have nothing to mount.
for forbidden in 1 4 7; do
    if echo "$PARTS" | grep -q "p${forbidden}\$"; then
        bad "p$forbidden must NOT be in the role table"
    else
        ok "p$forbidden is absent from the role table"
    fi
done

echo ""
echo "rw_role_device_path"
expect_eq "/"                    "$(rw_role_device_path root)"   "root  → /"
expect_eq "/home/root/data"      "$(rw_role_device_path data)"   "data  → /home/root/data"
expect_eq "/home/root/log"       "$(rw_role_device_path log)"    "log   → /home/root/log"
expect_eq "/home/root/backup"    "$(rw_role_device_path backup)" "backup → /home/root/backup"
if rw_role_device_path boot >/dev/null 2>&1; then
    bad "an unknown role must not resolve to a path"
else
    ok "an unknown role does not resolve"
fi

echo ""
echo "rw_host_root_disk / rw_is_host_root_disk"

HOST_ROOT_DISK=$(rw_host_root_disk 2>/dev/null || true)
if [ -z "$HOST_ROOT_DISK" ]; then
    skipped "host root disk" "lsblk/findmnt could not resolve /"
    skipped "root-disk veto (positive)" "no root disk resolved"
    skipped "root-disk veto (negative)" "no root disk resolved"
else
    # Cross-checked against findmnt directly rather than against the function's
    # own output, so this is a second opinion and not a tautology.
    ROOT_SRC=$(findmnt -no SOURCE --target / 2>/dev/null)
    case "$ROOT_SRC" in
        *"$HOST_ROOT_DISK"*) ok "root disk '$HOST_ROOT_DISK' is a prefix of '$ROOT_SRC'" ;;
        *) bad "root disk '$HOST_ROOT_DISK' does not appear in '$ROOT_SRC'" ;;
    esac

    # Both directions. A veto stuck at "yes" blocks every card; one stuck at "no"
    # is worse — it silently permits writing to this host's own disk, which is
    # the one mistake here with no undo.
    if rw_is_host_root_disk "/dev/$HOST_ROOT_DISK"; then
        ok "the host root disk is vetoed"
    else
        bad "the host root disk is NOT vetoed"
    fi
    if rw_is_host_root_disk "/dev/rw-no-such-disk-$$"; then
        bad "a nonexistent device must not be vetoed as the root disk"
    else
        ok "a nonexistent device is not vetoed"
    fi
fi

echo ""
echo "rw_check_card_mounts"

# A correctly mounted card: rootfs at root/, three non-rootfs trees beside it.
# The three are built from what those partitions really hold on a stock unit
# (measured 2026-08-05 — SYSTEM_ANALYSIS.md#42-partitions), so the negative half
# of the check is exercised against realistic input rather than empty directories.
GOOD="$TMP/mnt-good"
mkdir -p "$GOOD"
cp -a "$V" "$GOOD/root"
mkdir -p "$GOOD/data/websign" "$GOOD/data/cron/tabs" "$GOOD/log" "$GOOD/backup/factory"
: > "$GOOD/data/websign/net.mode"
: > "$GOOD/log/messages"
: > "$GOOD/backup/serialno"
if OUT=$(rw_check_card_mounts "$GOOD"); then
    ok "a correctly mounted card passes"
else
    bad "a correctly mounted card was rejected: $OUT"
fi

# ⚠️ The case this function exists for: p2 mounted where p6 was expected. Two
# rootfs trees, so `root/` still looks right and only the SECOND half of the check
# can catch it. Without that half the clean runs against the wrong tree and
# reports having deleted nothing.
SWAPPED="$TMP/mnt-swapped"
mkdir -p "$SWAPPED"
cp -a "$V" "$SWAPPED/root"
cp -a "$V" "$SWAPPED/data"
mkdir -p "$SWAPPED/log" "$SWAPPED/backup"
if rw_check_card_mounts "$SWAPPED" >/dev/null; then
    bad "a rootfs mounted as 'data' must be rejected"
else
    ok "a rootfs mounted as 'data' is rejected (wrong partition order)"
fi

# root/ is not a rootfs at all — e.g. p2 mounted alone, which is what happens if
# the partition numbers are read off a differently-partitioned card.
NOROOT="$TMP/mnt-noroot"
mkdir -p "$NOROOT/root/cron" "$NOROOT/data" "$NOROOT/log" "$NOROOT/backup"
if rw_check_card_mounts "$NOROOT" >/dev/null; then
    bad "a non-rootfs mounted as 'root' must be rejected"
else
    ok "a non-rootfs mounted as 'root' is rejected"
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

# A harness that runs nothing reports success.  The non-skippable cases are:
#   7  rw_is_rootfs
#   1  rw_rootfs_firmware
#   4  rw_is_card_disk (synthetic)
#   4  rw_part_dev
#   5  rw_card_partitions + 3 forbidden-partition assertions
#   5  rw_role_device_path
#   3  rw_check_card_mounts
# = 32.  rw_host_root_disk's 3 are skippable (they need a working lsblk), and the
# two real card images are gitignored, so neither is counted.
MIN_CASES=32
if [ "$TOTAL" -lt "$MIN_CASES" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: only $TOTAL cases ran, expected at least $MIN_CASES."
    echo "  Cases were skipped that cannot be skipped, or the file was truncated."
    exit 2
fi

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
echo -e "  ${GREEN}all good${NC}"
