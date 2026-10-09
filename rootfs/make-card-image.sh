#!/bin/bash
# make-card-image.sh [--modules <dir>] <partsdir> <rootfs.tar> <out.img>
#
# Builds a whole SD-card IMAGE FILE for a RoomWizard whose p6 is our own root
# filesystem. Inputs: the directory fetch-card-parts.sh filled (partition table
# and p1 raw, the extended-partition boot records, the per-unit state of the old
# p6), a Buildroot rootfs.tar, and a directory of kernel modules (*.ko, built by
# kernel/build-modules.sh --out <dir>; default ~/rw-kmods of the invoking user,
# i.e. $SUDO_USER under sudo; cy8ctmg120_ts.ko must be there).
#
# Run as ROOT in WSL (wsl.exe -u root -e bash -lc ...), on the native WSL fs:
# DrvFs (/mnt/c) keeps no symlinks, modes or device nodes, so the image is
# refused there and copied to Windows afterwards. The only block device this
# script touches is the loop device it creates itself on the image file; it
# never opens /dev/mmcblk* or any other disk, and any argument that is a
# block device is refused.
#
# p1 is copied byte for byte (the FAT geometry must not change), then everything
# on it except the five boot-chain files is deleted. p2/p3/p5 are made fresh and
# EMPTY; p6 gets the rootfs, the per-unit state and the kernel modules.
#
# With --bundle <dir|tar> (a release.sh --stage-only bundle) it then mounts p6, p2,
# p3 and p5 as <base>/{root,data,log,backup} and runs commissioning/commission-offline.sh
# over them: the boot scripts, their links and every component's files go in, and that
# script's own verify pass checks them. It asks nothing and cleans nothing: the tree is
# ours, with no vendor stack, and the per-unit state is already in p6. p1 is not mounted
# by that step.
set -u

die() { echo "FAIL: $*" >&2; exit 1; }

usage() {
    cat <<'USAGE'
Usage: make-card-image.sh [--modules <dir>] [--bundle <dir|tar>] <partsdir> <rootfs.tar> <out.img>

  --modules <dir>  directory of kernel modules (*.ko) copied to p6
                   /lib/modules/4.14.52/extra/ (default: ~/rw-kmods of the
                   invoking user, $SUDO_USER under sudo). cy8ctmg120_ts.ko is required.
  --bundle <b>     a release.sh --stage-only bundle (directory or .tar.gz),
                   installed with commissioning/commission-offline.sh (no prompt),
                   so the card boots with every component
                   and no provision.sh or deploy-all.sh afterwards. Needs
                   arm-linux-gnueabihf-objdump (that script's ARM check).
  -h, --help       this text
USAGE
}
MODDIR=""
BUNDLE=""
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --modules) [ $# -ge 2 ] || { usage >&2; exit 2; }; MODDIR="$2"; shift 2 ;;
        --modules=*) MODDIR="${1#--modules=}"; shift ;;
        --bundle) [ $# -ge 2 ] || { usage >&2; exit 2; }; BUNDLE="$2"; shift 2 ;;
        --bundle=*) BUNDLE="${1#--bundle=}"; shift ;;
        --) shift; break ;;
        -*) usage >&2; exit 2 ;;
        *) break ;;
    esac
done
[ $# -eq 3 ] || { usage >&2; exit 2; }
PARTS="$1"; ROOTTAR="$2"; OUT="$3"
if [ -z "$MODDIR" ]; then
    UHOME=$HOME
    if [ -n "${SUDO_USER:-}" ]; then UHOME=$(getent passwd "$SUDO_USER" | cut -d: -f6); fi
    MODDIR="$UHOME/rw-kmods"
fi

for a in "$@"; do
    [ ! -b "$a" ] || die "$a is a block device; this script only writes an image file"
done
[ "$(id -u)" -eq 0 ] || die "run as root (wsl.exe -u root ...)"
OUTABS=$(realpath -m -- "$OUT")
case "$OUTABS" in
    /mnt/*) die "$OUTABS is on DrvFs (no symlinks, modes or device nodes); build on the native WSL fs and copy the finished image out" ;;
    /dev/*) die "$OUTABS is under /dev" ;;
esac
[ ! -e "$OUTABS" ] || [ -f "$OUTABS" ] || die "$OUTABS exists and is not a regular file"
[ -d "$PARTS" ] || die "no parts directory $PARTS"
[ -f "$ROOTTAR" ] || die "no rootfs tar $ROOTTAR"
[ -f "$MODDIR/cy8ctmg120_ts.ko" ] || die "no $MODDIR/cy8ctmg120_ts.ko (build with kernel/build-modules.sh --out <dir>, or pass --modules)"
for f in geometry.txt head.bin md5.txt state.tar; do
    [ -f "$PARTS/$f" ] || die "missing $PARTS/$f"
done
for t in sfdisk losetup mke2fs mkswap e2fsck truncate; do
    command -v "$t" > /dev/null || die "$t not installed"
done
BUNDLE_TAG=""
if [ -n "$BUNDLE" ]; then
    # Absolute, because commission-offline.sh runs from the repo root. Checked here,
    # before any image exists, rather than after p6 is filled.
    [ -e "$BUNDLE" ] || die "no bundle $BUNDLE"
    BUNDLE=$(realpath -- "$BUNDLE") || die "realpath $BUNDLE"
    command -v "${OBJDUMP:-arm-linux-gnueabihf-objdump}" > /dev/null \
        || die "${OBJDUMP:-arm-linux-gnueabihf-objdump} not installed; commission-offline.sh refuses unverified ARM binaries"
    if [ -d "$BUNDLE" ]; then
        INFO=$(cat "$BUNDLE/manifest.d/bundle.info" 2>/dev/null) || INFO=""
    else
        INFO=$(tar -xzOf "$BUNDLE" --wildcards '*manifest.d/bundle.info' 2>/dev/null) || INFO=""
    fi
    BUNDLE_TAG=$(printf '%s\n' "$INFO" | awk -F= '$1=="tag"{t=$2} $1=="commit"{c=$2} $1=="built"{b=$2}
        END{ if (t != "") printf "%s (commit %s, built %s)", t, c, b }')
    [ -n "$BUNDLE_TAG" ] || BUNDLE_TAG="untagged (no manifest.d/bundle.info)"
fi

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck disable=SC1090,SC1091
. "$SCRIPT_DIR/../lib/rw-identify.sh" || die "cannot source rw-identify.sh"
# shellcheck disable=SC1090,SC1091
. "$SCRIPT_DIR/../lib/rw-state.sh" || die "cannot source rw-state.sh"

# The image gate, before any image exists: state.tar may hold only the IMAGE set of
# lib/rw-state.sh — never the VNC password, ScummVM's ini, saves or game data, loose
# /home/root files, calibration .bak files, the vendor /etc/hosts or p2 contents.
# Refused, not filtered: a state.tar that fails was made by an older
# fetch-card-parts.sh, and the cure is to fetch a clean one.
STATE_LIST=$(tar -tf "$PARTS/state.tar") || die "$PARTS/state.tar does not list"
if ! STATE_DENIED=$(printf '%s\n' "$STATE_LIST" | rw_state_image_deny); then
    echo "FAIL: $PARTS/state.tar carries entries no card image may hold:" >&2
    printf '%s\n' "$STATE_DENIED" | sed 's/^/  /' >&2
    echo "No image was written. Regenerate the parts with the current script (as the normal user):" >&2
    echo "  rootfs/fetch-card-parts.sh <unit-ip> $PARTS" >&2
    echo "and keep the unit's private config with commissioning/backup.sh <unit-ip>." >&2
    exit 1
fi

geom() { awk -v n="$1" -v c="$2" '$1==n{print $c}' "$PARTS/geometry.txt"; }
DISK_SECT=$(geom mmcblk0 3)
P2START=$(geom mmcblk0p2 2)
if [ -z "$DISK_SECT" ] || [ -z "$P2START" ]; then die "geometry.txt lacks the disk or p2"; fi

# Prove the copy of the head is exact BEFORE building on it.
WANT_MD5=$(tr -d ' \n' < "$PARTS/md5.txt")
HAVE_MD5=$(md5sum < "$PARTS/head.bin" | awk '{print $1}')
[ "$WANT_MD5" = "$HAVE_MD5" ] || die "head.bin md5 $HAVE_MD5 differs from the unit's $WANT_MD5"
[ "$(stat -c %s "$PARTS/head.bin")" -eq $((P2START * 512)) ] || die "head.bin has the wrong size"

LOOP=""
MNT=$(mktemp -d /tmp/rw-card-mnt.XXXXXX) || die "mktemp"
cleanup() {
    local rc=$?
    set +e
    if mountpoint -q "$MNT/p"; then umount "$MNT/p"; fi
    for r in root data log backup; do
        if mountpoint -q "$MNT/base/$r"; then umount "$MNT/base/$r"; fi
    done
    if [ -n "$LOOP" ]; then losetup -d "$LOOP"; fi
    rm -rf "$MNT"
    exit $rc
}
trap cleanup EXIT INT TERM

echo "== creating sparse $OUTABS ($DISK_SECT sectors)"
mkdir -p "$(dirname "$OUTABS")" || die "mkdir"
rm -f "$OUTABS"
truncate -s $((DISK_SECT * 512)) "$OUTABS" || die "truncate"
dd if="$PARTS/head.bin" of="$OUTABS" bs=512 conv=notrunc status=none || die "write head"
EBRS=0
for e in "$PARTS"/ebr-*.bin; do
    [ -e "$e" ] || break
    n=$(basename "$e" .bin); n=${n#ebr-}
    s=$(geom "mmcblk0p$n" 2)
    [ -n "$s" ] || die "no geometry for p$n"
    dd if="$e" of="$OUTABS" bs=512 seek=$((s - 63)) conv=notrunc status=none || die "write ebr $n"
    EBRS=$((EBRS + 1))
done
[ "$EBRS" -ge 1 ] || die "no ebr-*.bin in $PARTS"

echo "== partition table of the image"
sfdisk -d "$OUTABS"
rw_is_card_disk "$OUTABS" || die "rw_is_card_disk says the image does not carry the RoomWizard layout"
echo "rw_is_card_disk: yes"
for n in 1 2 3 5 6 7; do
    want="$(geom "mmcblk0p$n" 2),$(geom "mmcblk0p$n" 3)"
    got=$(sfdisk -d "$OUTABS" | awk -v n="$n" '$1 ~ ("[^0-9]" n "$") && match($0, /start=[ ]*[0-9]+/) {
        s=substr($0, RSTART, RLENGTH); gsub(/[^0-9]/, "", s)
        match($0, /size=[ ]*[0-9]+/); z=substr($0, RSTART, RLENGTH); gsub(/[^0-9]/, "", z); print s "," z }')
    [ "$want" = "$got" ] || die "p$n: geometry.txt says $want, the image table says $got"
done
# p4 is the extended container: sysfs reports 2 sectors, the table its true span, so only its start is compared.
sfdisk -d "$OUTABS" | grep -Eq "[^0-9]4 : start= *$(geom mmcblk0p4 2)," || die "p4 start differs from geometry.txt"
echo "all seven partitions match geometry.txt (p4 by start only)"

LOOP=$(losetup -P --show -f "$OUTABS") || die "losetup"
echo "== loop device $LOOP"
for n in 1 2 3 5 6 7; do
    PD=$(rw_part_dev "$LOOP" "$n")
    for _ in 1 2 3 4 5 6 7 8 9 10; do [ -b "$PD" ] && break; sleep 0.5; done
    [ -b "$PD" ] || die "$PD did not appear"
done

echo "== mkfs"
# ext4 features are limited to what kernel 4.14 mounts and what an older
# e2fsck understands: no metadata_csum, no 64bit. Labels are informational;
# nothing identifies a partition by label or UUID.
mkext() { mke2fs -F -q -t "$1" -O "$2" -L "$3" -m 1 "$4" || die "mke2fs $4"; }
mkext ext4 ^metadata_csum,^64bit rw-data "$(rw_part_dev "$LOOP" 2)"
mkext ext4 ^metadata_csum,^64bit rw-log "$(rw_part_dev "$LOOP" 3)"
# p5: formatted like p2, empty and not mounted (kept so the table matches the unit).
mkext ext4 ^metadata_csum,^64bit rw-spare "$(rw_part_dev "$LOOP" 5)"
mkext ext4 ^metadata_csum,^64bit rw-root "$(rw_part_dev "$LOOP" 6)"
mkswap "$(rw_part_dev "$LOOP" 7)" > /dev/null || die "mkswap"

echo "== p1: whitelist (the byte copy keeps the FAT geometry; only entries are deleted)"
P1DEV=$(rw_part_dev "$LOOP" 1)
mkdir "$MNT/p" || die "mkdir mnt"
mount -t vfat "$P1DEV" "$MNT/p" || die "mount p1"
P1KEEP="mlo u-boot.bin u-boot-sd.bin ctrlblock.bin uImage-system"
shopt -s nullglob dotglob
for e in "$MNT/p"/*; do
    b=$(basename "$e")
    keep=0
    for k in $P1KEEP; do [ "${b,,}" != "${k,,}" ] || keep=1; done   # FAT names are case-insensitive
    if [ "$keep" -eq 0 ]; then
        echo "p1: removing $b"
        rm -rf -- "$e" || die "rm p1/$b"
    fi
done
shopt -u nullglob dotglob
for k in $P1KEEP; do [ -f "$MNT/p/$k" ] || die "p1 lacks $k after the whitelist"; done
sync
echo "p1 final listing:"
ls -la "$MNT/p"
umount "$MNT/p" || die "umount p1"

fill() {   # fill <partnum> <rootfs tar> [<state tar>]
    local n="$1"
    mount "$(rw_part_dev "$LOOP" "$n")" "$MNT/p" || die "mount p$n"
    # The rootfs keeps its owners (dbus, avahi and the like are real users there).
    tar --numeric-owner -xpf "$2" -C "$MNT/p" || die "extract $2 into p$n"
    # The per-unit state does not: it is files captured from a unit, and the owner an
    # entry can carry (default-app was 1000/1000 in one) names no user on p6.
    if [ $# -ge 3 ]; then
        tar --no-same-owner -xpf "$3" -C "$MNT/p" || die "extract $3 into p$n"
    fi
    if [ "$n" = 6 ]; then
        # The data partitions mount here; the mount points stay empty (p5 is not mounted).
        mkdir -p "$MNT/p/home/root/data" "$MNT/p/home/root/log" "$MNT/p/home/root/backup"
        [ ! -d "$MNT/p/home/root/.ssh" ] || chmod 700 "$MNT/p/home/root/.ssh"
        [ ! -f "$MNT/p/home/root/.ssh/authorized_keys" ] || chmod 600 "$MNT/p/home/root/.ssh/authorized_keys"
        # state.tar no longer carries /etc/hosts (the vendor's), so the rootfs's own
        # maps 127.0.1.1 to the build-time name; map the carried one to loopback too.
        if [ -f "$MNT/p/etc/hostname" ]; then
            UNIT_NAME=$(head -n 1 "$MNT/p/etc/hostname" | tr -d ' \t\r')
            sh "$SCRIPT_DIR/../commissioning/set-hostname.sh" "$UNIT_NAME" "$MNT/p" \
                || die "set-hostname.sh $UNIT_NAME on p6"
        fi
        EXTRA="$MNT/p/lib/modules/4.14.52/extra"
        mkdir -p "$EXTRA" || die "mkdir $EXTRA"
        NKO=0
        for ko in "$MODDIR"/*.ko; do
            [ -f "$ko" ] || continue
            install -m 0644 -o 0 -g 0 "$ko" "$EXTRA/" || die "install $ko"
            NKO=$((NKO + 1))
        done
        [ -f "$EXTRA/cy8ctmg120_ts.ko" ] || die "cy8ctmg120_ts.ko did not land in p6"
        echo "p6 holds $(find "$MNT/p" -xdev | wc -l) entries; $NKO kernel modules in /lib/modules/4.14.52/extra"
    fi
    sync
    umount "$MNT/p" || die "umount p$n"
}
echo "== filling p6 (p2, p3, p5 stay empty)"
fill 6 "$ROOTTAR" "$PARTS/state.tar"

if [ -n "$BUNDLE" ]; then
    echo "== installing bundle $BUNDLE_TAG"
    # The layout rw_check_card_mounts and rw_offline_path expect: p6 as root,
    # p2/p3/p5 as data/log/backup — mounted, so a bundle path under /home/root/data
    # lands on p2 and not in p6's empty mount point. p1 is not mounted.
    for rp in root:6 data:2 log:3 backup:5; do
        mkdir -p "$MNT/base/${rp%%:*}" || die "mkdir base"
        mount "$(rw_part_dev "$LOOP" "${rp#*:}")" "$MNT/base/${rp%%:*}" || die "mount p${rp#*:} as ${rp%%:*}"
    done
    if ! bash "$SCRIPT_DIR/../commissioning/commission-offline.sh" --bundle "$BUNDLE" \
            --base "$MNT/base" < /dev/null; then
        rm -f "$OUTABS"
        die "commission-offline.sh failed (above); the image is deleted, write nothing"
    fi
    sync
    for r in root data log backup; do umount "$MNT/base/$r" || die "umount base/$r"; done
fi

echo "== fsck"
FSCK_BAD=0
for n in 2 3 5 6; do
    if e2fsck -fn "$(rw_part_dev "$LOOP" "$n")" > "$MNT/fsck.$n" 2>&1; then
        echo "p$n: clean ($(tail -n 1 "$MNT/fsck.$n"))"
    else
        echo "p$n: e2fsck reported problems"; cat "$MNT/fsck.$n"; FSCK_BAD=1
    fi
done

losetup -d "$LOOP" && LOOP=""
sync

echo "== head check (MBR and gap before p1; p1 itself was edited on purpose)"
P1START=$(geom mmcblk0p1 2)
[ -n "$P1START" ] || die "geometry.txt lacks p1"
IMG_MD5=$(dd if="$OUTABS" bs=512 count="$P1START" status=none | md5sum | awk '{print $1}')
HEAD_MD5=$(dd if="$PARTS/head.bin" bs=512 count="$P1START" status=none | md5sum | awk '{print $1}')
if [ "$IMG_MD5" = "$HEAD_MD5" ]; then MATCH=yes; else MATCH=no; fi

STATE_N=$(tar -tf "$PARTS/state.tar" | grep -vc '/$')
echo
echo "==================== SUMMARY ===================="
echo "image:           $OUTABS"
echo "size:            $(stat -c %s "$OUTABS") bytes apparent, $(du -h "$OUTABS" | awk '{print $1}') allocated"
echo "rootfs tar:      $ROOTTAR ($(stat -c %y "$ROOTTAR"))"
echo "head match:      $MATCH (sectors 0..$((P1START - 1)) vs head.bin)"
echo "fsck:            $([ "$FSCK_BAD" -eq 0 ] && echo 'all four clean' || echo 'PROBLEMS, see above')"
echo "state carried:   $STATE_N non-directory entries from the old p6"
echo "bundle:          ${BUNDLE_TAG:-none (no --bundle: run provision.sh and deploy-all.sh after the first boot)}"
echo
echo "Next, on Windows: copy the image out of WSL, e.g."
printf '  copy \\\\wsl$\\<distro>%s C:\\work\\rw-scratch-s0930\\s1000\\card.img\n' "$OUTABS"
echo "(outside the repo), write it with Rufus, balenaEtcher or Win32 Disk Imager to the"
echo "8 GB card (the operator authorized wiping that card), then put it in the unit."
echo "The original card stays untouched as the fallback."
[ "$MATCH" = yes ] && [ "$FSCK_BAD" -eq 0 ]
