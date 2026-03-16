#!/bin/bash
# clone-to-32gb.sh — Clone and expand RoomWizard SD card from 4GB to 32GB
#
# Usage:
#   sudo ./clone-to-32gb.sh --clone-from <image_or_device> /dev/sdX
#   sudo ./clone-to-32gb.sh --expand-only /dev/sdX
#
# This script clones the original RoomWizard ~4GB SD card image onto a larger
# (16-32GB) SD card and expands the root filesystem (p6) to use all available
# space, while preserving partition numbering and filesystem UUID.
#
# The original RoomWizard partition table has 7 partitions:
#   p1: FAT32, ~70.6 MB, bootable (MLO, U-Boot, uImage, DTB)
#   p2: ext3, ~251 MB (application data)
#   p3: ext3, ~243 MB (system logs)
#   p4: extended container, ~3.14 GB (holds p5-p7)
#   p5: ext3, ~1.40 GB (OEM backup)
#   p6: ext3, ~981 MB (root filesystem)
#   p7: swap, ~259 MB (swap space — dropped during upgrade)
#
# The upgrade drops p7 (swap) to maximize rootfs space. Swap is not needed
# for our gaming use case, and removing it lets p6 expand further.
#
# For full background, partition layout details, and manual procedure, see:
#   SD_CARD_UPGRADE.md
#
# Strategy:
#   1. Clone the original 4GB image to the 32GB card with dd
#   2. Use sfdisk to expand extended partition (p4) and rootfs (p6)
#   3. Drop swap partition (p7) — not needed for gaming
#   4. Use e2fsck + resize2fs to grow the ext3 filesystem
#   5. Preserve p5 (backup) at original size to maintain p6 device numbering
#   6. UUID is preserved because we resize, not reformat

set -euo pipefail

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
EXPECTED_ROOTFS_UUID="108a1490-8feb-4d0c-b3db-995dc5fc066c"
MIN_TARGET_SIZE_GB=16
EXPECTED_PARTITION_COUNT=7  # Original has 7 partitions (including swap on p7)
SCRIPT_NAME="$(basename "$0")"

# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# ---------------------------------------------------------------------------
# Global state
# ---------------------------------------------------------------------------
MODE=""            # "clone" or "expand"
SOURCE=""          # Image file or source device (clone mode only)
DEVICE=""          # Target block device
DRY_RUN=false
STEP=0

# ---------------------------------------------------------------------------
# Logging helpers
# ---------------------------------------------------------------------------
info()    { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; }
step()    { STEP=$((STEP + 1)); echo -e "\n${BLUE}${BOLD}=== Step ${STEP}: $* ===${NC}"; }
dry_run() { echo -e "${YELLOW}[DRY-RUN]${NC} $*"; }

# ---------------------------------------------------------------------------
# Error trap
# ---------------------------------------------------------------------------
on_error() {
    local exit_code=$?
    local line_no=$1
    error "Script failed at line ${line_no} (exit code ${exit_code})"
    if [ $STEP -gt 0 ]; then
        error "Failure occurred during Step ${STEP}"
    fi
    error "The target device may be in an inconsistent state."
    error "If cloning was complete, you can re-run with --expand-only to retry expansion."
    exit "${exit_code}"
}
trap 'on_error ${LINENO}' ERR

# ---------------------------------------------------------------------------
# Partition naming helper
# ---------------------------------------------------------------------------
# Given a base device like /dev/sdb or /dev/mmcblk0, return the partition
# device path for partition number N.
#   /dev/sdb   → /dev/sdb1, /dev/sdb2, ...
#   /dev/mmcblk0 → /dev/mmcblk0p1, /dev/mmcblk0p2, ...
part_dev() {
    local base="$1"
    local num="$2"
    if [[ "$base" =~ [0-9]$ ]]; then
        echo "${base}p${num}"
    else
        echo "${base}${num}"
    fi
}

# ---------------------------------------------------------------------------
# Usage / help
# ---------------------------------------------------------------------------
usage() {
    echo -e "${BOLD}${SCRIPT_NAME}${NC} — Clone and expand RoomWizard SD card from 4GB to 32GB"
    echo ""
    echo -e "${BOLD}USAGE${NC}"
    echo "  sudo ./${SCRIPT_NAME} --clone-from <image_or_device> <target_device>"
    echo "  sudo ./${SCRIPT_NAME} --expand-only <target_device>"
    echo "  sudo ./${SCRIPT_NAME} --help"
    echo ""
    echo -e "${BOLD}MODES${NC}"
    echo "  --clone-from <src>   Clone from an image file or source device, then expand."
    echo "                       Example: --clone-from roomwizard-original-4gb.img /dev/sdb"
    echo "                       Example: --clone-from /dev/sdc /dev/sdb"
    echo ""
    echo "  --expand-only        Skip cloning; expand partitions on a card that already"
    echo "                       has the RoomWizard image cloned onto it."
    echo "                       Example: --expand-only /dev/sdb"
    echo ""
    echo -e "${BOLD}OPTIONS${NC}"
    echo "  --dry-run            Show what would be done without making any changes."
    echo "  --help, -h           Show this help message."
    echo ""
    echo -e "${BOLD}SAFETY${NC}"
    echo "  • Must be run as root (sudo)"
    echo "  • Refuses to operate on /dev/sda (likely system disk)"
    echo "  • Refuses to operate on mounted devices"
    echo "  • Requires explicit confirmation before destructive operations"
    echo "  • Validates target is a block device ≥${MIN_TARGET_SIZE_GB} GB"
    echo ""
    echo -e "${BOLD}WHAT IT DOES${NC}"
    echo "  1. (Clone mode) dd the source image/device to the target"
    echo "  2. Verify expected 7-partition layout (originally) and rootfs UUID"
    echo "  3. Expand extended partition (p4) and rootfs (p6) to fill the card"
    echo "  4. Drop swap partition (p7) to maximize rootfs space"
    echo "  5. Preserve backup partition (p5) at original size for device numbering"
    echo "  6. Grow the ext3 filesystem on p6 with resize2fs"
    echo "  7. Verify UUID is preserved"
    echo ""
    echo -e "${BOLD}PARTITION LAYOUT (original 7 partitions)${NC}"
    echo "  p1: FAT32  ~70.6 MB  bootable  (boot: MLO, U-Boot, uImage, DTB)"
    echo "  p2: ext3   ~251 MB             (application data)"
    echo "  p3: ext3   ~243 MB             (system logs)"
    echo "  p4: ext    ~3.14 GB            (extended container for p5-p7)"
    echo "  p5: ext3   ~1.40 GB            (OEM backup)"
    echo "  p6: ext3   ~981 MB             (root filesystem)"
    echo "  p7: swap   ~259 MB             (swap — DROPPED during upgrade)"
    echo ""
    echo -e "${BOLD}SEE ALSO${NC}"
    echo "  SD_CARD_UPGRADE.md — Full documentation of the upgrade procedure"
    echo ""
    exit 0
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
parse_args() {
    if [ $# -eq 0 ]; then
        usage
    fi

    while [ $# -gt 0 ]; do
        case "$1" in
            --help|-h)
                usage
                ;;
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --clone-from)
                MODE="clone"
                if [ $# -lt 2 ]; then
                    error "--clone-from requires a source argument"
                    exit 1
                fi
                SOURCE="$2"
                shift 2
                ;;
            --expand-only)
                MODE="expand"
                shift
                ;;
            -*)
                error "Unknown option: $1"
                echo "Run '${SCRIPT_NAME} --help' for usage."
                exit 1
                ;;
            *)
                # Positional argument = target device
                if [ -n "$DEVICE" ]; then
                    error "Multiple target devices specified: '${DEVICE}' and '$1'"
                    exit 1
                fi
                DEVICE="$1"
                shift
                ;;
        esac
    done

    # Validate required arguments
    if [ -z "$MODE" ]; then
        error "Must specify either --clone-from <source> or --expand-only"
        echo "Run '${SCRIPT_NAME} --help' for usage."
        exit 1
    fi

    if [ -z "$DEVICE" ]; then
        error "Must specify target device (e.g., /dev/sdb)"
        echo "Run '${SCRIPT_NAME} --help' for usage."
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------
check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        error "This script must be run as root (use sudo)"
        exit 1
    fi
}

check_dependencies() {
    local missing=()
    for cmd in dd sfdisk e2fsck resize2fs blkid blockdev partprobe lsblk; do
        if ! command -v "$cmd" &>/dev/null; then
            missing+=("$cmd")
        fi
    done
    if [ ${#missing[@]} -gt 0 ]; then
        error "Missing required commands: ${missing[*]}"
        echo "Install with: sudo apt-get install -y util-linux e2fsprogs coreutils"
        exit 1
    fi
}

check_device_safe() {
    local dev="$1"

    # Must not be /dev/sda
    if [[ "$dev" == "/dev/sda" ]]; then
        error "Refusing to operate on /dev/sda (likely your system disk!)"
        exit 1
    fi

    # Must exist
    if [ ! -e "$dev" ]; then
        error "Device '${dev}' does not exist"
        exit 1
    fi

    # Must be a block device
    if [ ! -b "$dev" ]; then
        error "'${dev}' is not a block device"
        exit 1
    fi

    # Must not be mounted (check all partitions)
    local mounts
    mounts=$(mount | grep "^${dev}" || true)
    if [ -n "$mounts" ]; then
        error "Device '${dev}' (or its partitions) appears to be mounted:"
        echo "$mounts"
        error "Unmount all partitions first: sudo umount ${dev}* 2>/dev/null"
        exit 1
    fi

    # Check minimum size
    local size_bytes
    size_bytes=$(blockdev --getsize64 "$dev")
    local size_gb=$(( size_bytes / 1073741824 ))
    if [ "$size_gb" -lt "$MIN_TARGET_SIZE_GB" ]; then
        error "Target device is ${size_gb} GB — minimum ${MIN_TARGET_SIZE_GB} GB required"
        error "This check ensures you're not accidentally writing to the original 4GB card"
        exit 1
    fi

    info "Target device: ${dev} (${size_gb} GB) — OK"
}

check_source() {
    local src="$1"
    if [ -b "$src" ]; then
        info "Source is a block device: ${src}"
        # Check it's not the same as target
        if [ "$src" = "$DEVICE" ]; then
            error "Source and target are the same device!"
            exit 1
        fi
    elif [ -f "$src" ]; then
        local size_mb=$(( $(stat -c%s "$src") / 1048576 ))
        info "Source is an image file: ${src} (${size_mb} MB)"
    else
        error "Source '${src}' is not a block device or regular file"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# User confirmation
# ---------------------------------------------------------------------------
confirm() {
    local prompt="$1"
    if $DRY_RUN; then
        dry_run "Would ask: ${prompt}"
        return 0
    fi
    echo ""
    echo -e "${YELLOW}${BOLD}${prompt}${NC}"
    echo -n "Type 'yes' to continue, anything else to abort: "
    local answer
    read -r answer
    if [ "$answer" != "yes" ]; then
        error "Aborted by user"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Phase 1: Clone
# ---------------------------------------------------------------------------
do_clone() {
    step "Clone source image to target device"

    echo ""
    info "Source: ${SOURCE}"
    info "Target: ${DEVICE}"
    echo ""
    warn "This will OVERWRITE ALL DATA on ${DEVICE}!"
    echo ""

    # Show target device info
    lsblk "$DEVICE" 2>/dev/null || true
    echo ""

    confirm "Proceed with cloning ${SOURCE} → ${DEVICE}?"

    if $DRY_RUN; then
        dry_run "dd if=${SOURCE} of=${DEVICE} bs=4M status=progress conv=fsync"
        dry_run "sync"
        dry_run "partprobe ${DEVICE}"
        return
    fi

    info "Cloning (this may take several minutes)..."
    dd if="$SOURCE" of="$DEVICE" bs=4M status=progress conv=fsync
    info "Flushing buffers..."
    sync
    info "Re-reading partition table..."
    partprobe "$DEVICE"
    # Give the kernel a moment to settle
    sleep 2

    info "Clone complete"
}

# ---------------------------------------------------------------------------
# Phase 2: Verify partition structure
# ---------------------------------------------------------------------------
do_verify() {
    step "Verify partition structure"

    local p6_dev
    p6_dev=$(part_dev "$DEVICE" 6)

    # Count partitions
    local part_count
    part_count=$(lsblk -n -o NAME "$DEVICE" | grep -c -v "^$(basename "$DEVICE")$" || true)

    info "Found ${part_count} partition(s) on ${DEVICE}"

    if [ "$part_count" -ne "$EXPECTED_PARTITION_COUNT" ]; then
        error "Expected ${EXPECTED_PARTITION_COUNT} partitions (originally), found ${part_count}"
        echo ""
        lsblk "$DEVICE"
        error "The target device does not have the expected RoomWizard partition layout"
        error "Expected 7 partitions: p1(FAT32,boot) p2(ext3) p3(ext3) p4(extended) p5(ext3) p6(ext3,rootfs) p7(swap)"
        exit 1
    fi
    info "Partition count: ${part_count} (7 partitions including swap) — OK"

    # Show current partition table
    echo ""
    info "Current partition table:"
    sfdisk -l "$DEVICE"
    echo ""

    # Check p6 UUID
    if $DRY_RUN; then
        dry_run "Would check UUID of ${p6_dev}"
        return
    fi

    local p6_uuid
    p6_uuid=$(blkid -s UUID -o value "$p6_dev" 2>/dev/null || echo "")

    if [ -z "$p6_uuid" ]; then
        error "Could not read UUID from ${p6_dev}"
        error "Is the RoomWizard image properly cloned to this card?"
        exit 1
    fi

    if [ "$p6_uuid" != "$EXPECTED_ROOTFS_UUID" ]; then
        warn "p6 UUID mismatch!"
        warn "  Expected: ${EXPECTED_ROOTFS_UUID}"
        warn "  Found:    ${p6_uuid}"
        echo ""
        confirm "UUID does not match expected value. Continue anyway?"
    else
        info "p6 UUID: ${p6_uuid} — matches expected value ✓"
    fi
}

# ---------------------------------------------------------------------------
# Phase 3: Repartition
# ---------------------------------------------------------------------------
do_repartition() {
    step "Repartition — expand extended (p4) and rootfs (p6), drop swap (p7)"

    # Save current partition table
    local saved_table
    saved_table=$(sfdisk -d "$DEVICE")

    info "Current sfdisk dump:"
    echo "$saved_table"
    echo ""

    # Parse partition entries from sfdisk dump
    # Format: /dev/sdb1 : start=        63, size=      144522, type=c, bootable
    local p1_start p1_size p1_type p1_bootable
    local p2_start p2_size p2_type
    local p3_start p3_size p3_type
    local p4_start p4_size p4_type
    local p5_start p5_size p5_type
    local p6_start p6_size p6_type
    local p7_start p7_size p7_type

    parse_partition() {
        local line="$1"
        echo "$line" | sed -E 's/.*start=\s*([0-9]+).*/\1/'
    }
    parse_size() {
        local line="$1"
        echo "$line" | sed -E 's/.*size=\s*([0-9]+).*/\1/'
    }
    parse_type() {
        local line="$1"
        echo "$line" | sed -E 's/.*type=\s*([0-9a-fA-F]+).*/\1/'
    }

    # Extract header lines (label, label-id, device, unit, sector-size)
    local header
    header=$(echo "$saved_table" | grep -E '^(label|label-id|device|unit|sector-size):' || true)

    # Extract label-id for preservation
    local label_id
    label_id=$(echo "$saved_table" | grep -oP 'label-id:\s*\K.*' || echo "0x00000000")

    # Extract each partition line
    local p1_line p2_line p3_line p4_line p5_line p6_line p7_line

    p1_line=$(echo "$saved_table" | grep "$(part_dev "$DEVICE" 1) " || echo "$saved_table" | grep "$(part_dev "$DEVICE" 1)$" || true)
    p2_line=$(echo "$saved_table" | grep "$(part_dev "$DEVICE" 2) " || echo "$saved_table" | grep "$(part_dev "$DEVICE" 2)$" || true)
    p3_line=$(echo "$saved_table" | grep "$(part_dev "$DEVICE" 3) " || echo "$saved_table" | grep "$(part_dev "$DEVICE" 3)$" || true)
    p4_line=$(echo "$saved_table" | grep "$(part_dev "$DEVICE" 4) " || echo "$saved_table" | grep "$(part_dev "$DEVICE" 4)$" || true)
    p5_line=$(echo "$saved_table" | grep "$(part_dev "$DEVICE" 5) " || echo "$saved_table" | grep "$(part_dev "$DEVICE" 5)$" || true)
    p6_line=$(echo "$saved_table" | grep "$(part_dev "$DEVICE" 6) " || echo "$saved_table" | grep "$(part_dev "$DEVICE" 6)$" || true)
    p7_line=$(echo "$saved_table" | grep "$(part_dev "$DEVICE" 7) " || echo "$saved_table" | grep "$(part_dev "$DEVICE" 7)$" || true)

    # Verify we found all 7 partitions
    for pnum in 1 2 3 4 5 6 7; do
        local varname="p${pnum}_line"
        if [ -z "${!varname}" ]; then
            error "Could not parse partition ${pnum} from sfdisk dump"
            exit 1
        fi
    done

    # Check if p1 has bootable flag
    p1_bootable=""
    if echo "$p1_line" | grep -q "bootable"; then
        p1_bootable=", bootable"
    fi

    # Parse values
    p1_start=$(parse_partition "$p1_line"); p1_size=$(parse_size "$p1_line"); p1_type=$(parse_type "$p1_line")
    p2_start=$(parse_partition "$p2_line"); p2_size=$(parse_size "$p2_line"); p2_type=$(parse_type "$p2_line")
    p3_start=$(parse_partition "$p3_line"); p3_size=$(parse_size "$p3_line"); p3_type=$(parse_type "$p3_line")
    p4_start=$(parse_partition "$p4_line"); p4_size=$(parse_size "$p4_line"); p4_type=$(parse_type "$p4_line")
    p5_start=$(parse_partition "$p5_line"); p5_size=$(parse_size "$p5_line"); p5_type=$(parse_type "$p5_line")
    p6_start=$(parse_partition "$p6_line"); p6_size=$(parse_size "$p6_line"); p6_type=$(parse_type "$p6_line")
    p7_start=$(parse_partition "$p7_line"); p7_size=$(parse_size "$p7_line"); p7_type=$(parse_type "$p7_line")

    info "Parsed current layout (7 partitions):"
    info "  p1: start=${p1_start}, size=${p1_size}, type=${p1_type}${p1_bootable}  (~$(( p1_size * 512 / 1048576 )) MB, FAT32 boot)"
    info "  p2: start=${p2_start}, size=${p2_size}, type=${p2_type}  (~$(( p2_size * 512 / 1048576 )) MB, data)"
    info "  p3: start=${p3_start}, size=${p3_size}, type=${p3_type}  (~$(( p3_size * 512 / 1048576 )) MB, log)"
    info "  p4: start=${p4_start}, size=${p4_size}, type=${p4_type}  (~$(( p4_size * 512 / 1048576 )) MB, extended)"
    info "  p5: start=${p5_start}, size=${p5_size}, type=${p5_type}  (~$(( p5_size * 512 / 1048576 )) MB, backup)"
    info "  p6: start=${p6_start}, size=${p6_size}, type=${p6_type}  (~$(( p6_size * 512 / 1048576 )) MB, rootfs)"
    info "  p7: start=${p7_start}, size=${p7_size}, type=${p7_type}  (~$(( p7_size * 512 / 1048576 )) MB, swap — WILL BE DROPPED)"

    # Record old p6 size for summary
    OLD_P6_SIZE=$p6_size

    # Calculate new sizes
    local total_sectors
    total_sectors=$(blockdev --getsz "$DEVICE")
    info "Total device sectors: ${total_sectors}"

    # New p4 (extended): same start, extends to end of disk
    local new_p4_size=$(( total_sectors - p4_start ))

    # p5 stays the same
    # p6: same start, expand to fill remaining space in extended (p7 swap is dropped)
    # Calculate p6 size: from p6 start to end of the extended partition
    local new_p6_size=$(( p4_start + new_p4_size - p6_start ))

    local old_p6_mb=$(( p6_size * 512 / 1048576 ))
    local new_p6_mb=$(( new_p6_size * 512 / 1048576 ))
    local p7_mb=$(( p7_size * 512 / 1048576 ))

    echo ""
    info "New layout plan (6 partitions — p7 swap dropped):"
    info "  p1: start=${p1_start}, size=${p1_size}, type=${p1_type}${p1_bootable}  (unchanged)"
    info "  p2: start=${p2_start}, size=${p2_size}, type=${p2_type}  (unchanged)"
    info "  p3: start=${p3_start}, size=${p3_size}, type=${p3_type}  (unchanged)"
    info "  p4: start=${p4_start}, size=${new_p4_size}, type=${p4_type}  (EXPANDED)"
    info "  p5: start=${p5_start}, size=${p5_size}, type=${p5_type}  (unchanged)"
    info "  p6: start=${p6_start}, size=${new_p6_size}, type=${p6_type}  (EXPANDED: ${old_p6_mb} MB → ${new_p6_mb} MB)"
    info "  p7: DROPPED (was swap, ${p7_mb} MB — not needed for gaming)"
    echo ""

    confirm "Apply new partition table to ${DEVICE}?"

    # Build the sfdisk input
    # We use the dump format that sfdisk understands
    # Preserve label-id and bootable flag from the original table
    local p1_dev p2_dev p3_dev p4_dev p5_dev p6_dev
    p1_dev=$(part_dev "$DEVICE" 1)
    p2_dev=$(part_dev "$DEVICE" 2)
    p3_dev=$(part_dev "$DEVICE" 3)
    p4_dev=$(part_dev "$DEVICE" 4)
    p5_dev=$(part_dev "$DEVICE" 5)
    p6_dev=$(part_dev "$DEVICE" 6)

    # Note: We omit size for p4 and p6 so sfdisk fills to max available space.
    # We explicitly set label-id to preserve the original value (0x00000000).
    # We include the bootable flag on p1 to match the original layout.
    local new_table
    new_table="label: dos
label-id: ${label_id}
unit: sectors
sector-size: 512

${p1_dev} : start=${p1_start}, size=${p1_size}, type=${p1_type}${p1_bootable}
${p2_dev} : start=${p2_start}, size=${p2_size}, type=${p2_type}
${p3_dev} : start=${p3_start}, size=${p3_size}, type=${p3_type}
${p4_dev} : start=${p4_start}, type=${p4_type}
${p5_dev} : start=${p5_start}, size=${p5_size}, type=${p5_type}
${p6_dev} : start=${p6_start}, type=${p6_type}
"

    if $DRY_RUN; then
        dry_run "Would apply the following partition table:"
        echo "$new_table"
        dry_run "partprobe ${DEVICE}"
        # Store for summary
        NEW_P6_SIZE=$new_p6_size
        return
    fi

    info "Writing new partition table..."
    echo "$new_table" | sfdisk --force "$DEVICE"

    info "Re-reading partition table..."
    partprobe "$DEVICE"
    sleep 2

    # Store new p6 size for summary
    NEW_P6_SIZE=$new_p6_size

    info "Repartition complete"
    echo ""
    info "New partition table:"
    sfdisk -l "$DEVICE"
}

# ---------------------------------------------------------------------------
# Phase 4: Filesystem expansion
# ---------------------------------------------------------------------------
do_resize_fs() {
    step "Expand ext3 filesystem on rootfs (p6)"

    local p6_dev
    p6_dev=$(part_dev "$DEVICE" 6)

    if $DRY_RUN; then
        dry_run "e2fsck -f -y ${p6_dev}"
        dry_run "resize2fs ${p6_dev}"
        return
    fi

    info "Running filesystem check on ${p6_dev}..."
    # e2fsck returns 1 if errors were corrected, which is OK for us
    e2fsck -f -y "$p6_dev" || {
        local rc=$?
        if [ $rc -le 1 ]; then
            info "e2fsck completed (corrected minor issues)"
        else
            error "e2fsck failed with exit code ${rc}"
            exit 1
        fi
    }

    info "Resizing filesystem to fill partition..."
    resize2fs "$p6_dev"

    info "Filesystem expansion complete"
}

# ---------------------------------------------------------------------------
# Phase 5: Final verification
# ---------------------------------------------------------------------------
do_final_verify() {
    step "Final verification"

    local p6_dev
    p6_dev=$(part_dev "$DEVICE" 6)

    if $DRY_RUN; then
        dry_run "blkid ${p6_dev}"
        dry_run "sfdisk -l ${DEVICE}"
        dry_run "dumpe2fs -h ${p6_dev}"
        echo ""
        dry_run "Summary:"
        local old_mb=$(( OLD_P6_SIZE * 512 / 1048576 ))
        local new_mb=$(( NEW_P6_SIZE * 512 / 1048576 ))
        dry_run "  Old rootfs partition: ${old_mb} MB"
        dry_run "  New rootfs partition: ${new_mb} MB"
        dry_run "  Swap partition (p7): DROPPED"
        return
    fi

    # Check UUID
    echo ""
    info "Checking rootfs UUID..."
    local p6_uuid
    p6_uuid=$(blkid -s UUID -o value "$p6_dev" 2>/dev/null || echo "UNKNOWN")

    if [ "$p6_uuid" = "$EXPECTED_ROOTFS_UUID" ]; then
        info "✓ UUID preserved: ${p6_uuid}"
    else
        warn "✗ UUID MISMATCH!"
        warn "  Expected: ${EXPECTED_ROOTFS_UUID}"
        warn "  Got:      ${p6_uuid}"
        warn "  The commissioning script may not find the rootfs."
        warn "  Fix with: sudo tune2fs -U ${EXPECTED_ROOTFS_UUID} ${p6_dev}"
    fi

    # Show blkid details
    echo ""
    info "blkid output for ${p6_dev}:"
    blkid "$p6_dev"

    # Show final partition table
    echo ""
    info "Final partition table:"
    sfdisk -l "$DEVICE"

    # Show filesystem size via dumpe2fs
    echo ""
    info "Filesystem details (dumpe2fs):"
    local block_count block_size
    block_count=$(dumpe2fs -h "$p6_dev" 2>/dev/null | grep "Block count:" | awk '{print $3}')
    block_size=$(dumpe2fs -h "$p6_dev" 2>/dev/null | grep "Block size:" | awk '{print $3}')

    if [ -n "$block_count" ] && [ -n "$block_size" ]; then
        local fs_size_mb=$(( block_count * block_size / 1048576 ))
        info "  Block count: ${block_count}"
        info "  Block size:  ${block_size} bytes"
        info "  Filesystem:  ${fs_size_mb} MB ($(( fs_size_mb / 1024 )) GB)"
    fi

    # Summary
    echo ""
    echo -e "${GREEN}${BOLD}════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}${BOLD}  SD Card Upgrade Complete!${NC}"
    echo -e "${GREEN}${BOLD}════════════════════════════════════════════════════${NC}"
    echo ""

    local old_mb=$(( OLD_P6_SIZE * 512 / 1048576 ))
    local new_mb=$(( NEW_P6_SIZE * 512 / 1048576 ))

    echo -e "  Device:           ${BOLD}${DEVICE}${NC}"
    echo -e "  Rootfs partition: ${BOLD}${p6_dev}${NC}"
    echo -e "  Old rootfs size:  ${old_mb} MB (~981 MB)"
    echo -e "  New rootfs size:  ${BOLD}${new_mb} MB${NC} ($(( new_mb / 1024 )) GB)"
    echo -e "  Swap (p7):        ${BOLD}DROPPED${NC} (was ~259 MB — not needed for gaming)"
    echo -e "  UUID:             ${BOLD}${p6_uuid}${NC}"
    echo ""

    if [ -n "${fs_size_mb:-}" ]; then
        echo -e "  Filesystem size:  ${BOLD}${fs_size_mb} MB${NC} ($(( fs_size_mb / 1024 )) GB)"
        echo ""
    fi

    echo -e "  ${GREEN}Next steps:${NC}"
    echo -e "    1. Insert card into RoomWizard"
    echo -e "    2. Run: ${BOLD}./commission-roomwizard.sh${NC}"
    echo -e "    3. Run: ${BOLD}./setup-device.sh <device-ip> --remove${NC}"
    echo -e "    4. Run: ${BOLD}./deploy-all.sh <device-ip>${NC}"
    echo ""
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo ""
    echo -e "${BLUE}${BOLD}╔══════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}${BOLD}║  RoomWizard SD Card Clone & Expand Tool              ║${NC}"
    echo -e "${BLUE}${BOLD}║  4 GB → 32 GB (or any ≥${MIN_TARGET_SIZE_GB} GB card)               ║${NC}"
    echo -e "${BLUE}${BOLD}╚══════════════════════════════════════════════════════╝${NC}"
    echo ""

    parse_args "$@"

    if $DRY_RUN; then
        warn "DRY-RUN mode — no changes will be made"
        echo ""
    fi

    # Pre-flight checks
    step "Pre-flight checks"
    check_root
    check_dependencies

    if [ "$MODE" = "clone" ]; then
        check_source "$SOURCE"
    fi

    check_device_safe "$DEVICE"

    # Initialize size tracking
    OLD_P6_SIZE=0
    NEW_P6_SIZE=0

    # Execute phases
    if [ "$MODE" = "clone" ]; then
        do_clone
    else
        info "Expand-only mode — skipping clone phase"
    fi

    do_verify
    do_repartition
    do_resize_fs
    do_final_verify
}

main "$@"
