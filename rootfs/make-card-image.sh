#!/bin/bash
# make-card-image.sh <partsdir> <rootfs.tar> <out.img>
#
# Builds a whole SD-card IMAGE FILE for a RoomWizard whose p6 is our own root
# filesystem. Inputs: the directory fetch-card-parts.sh filled (partition table
# and p1 raw, the extended-partition boot records, the content of the three data
# partitions, the per-unit state of the old p6) and a Buildroot rootfs.tar.
#
# Run as ROOT in WSL (wsl.exe -u root -e bash -lc ...), on the native WSL fs:
# DrvFs (/mnt/c) keeps no symlinks, modes or device nodes, so the image is
# refused there and copied to Windows afterwards. The only block device this
# script touches is the loop device it creates itself on the image file; it
# never opens /dev/mmcblk* or any other disk, and any argument that is a
# block device is refused.
#
# The p1 area (mlo, u-boot.bin, ctrlblock.bin) is copied byte for byte, so the
# boot chain is the unit's own and the recovery stays "write the old card back".
set -u

die() { echo "FAIL: $*" >&2; exit 1; }

[ $# -eq 3 ] || { echo "usage: $0 <partsdir> <rootfs.tar> <out.img>" >&2; exit 2; }
PARTS="$1"; ROOTTAR="$2"; OUT="$3"

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
for f in geometry.txt head.bin md5.txt state.tar p2.tar p3.tar p5.tar; do
    [ -f "$PARTS/$f" ] || die "missing $PARTS/$f"
done
for t in sfdisk losetup mke2fs mkswap e2fsck truncate; do
    command -v "$t" > /dev/null || die "$t not installed"
done

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck disable=SC1090,SC1091
. "$SCRIPT_DIR/../lib/rw-identify.sh" || die "cannot source rw-identify.sh"

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
for n in 2 3 5 6 7; do
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
mkext ext3 none rw-backup "$(rw_part_dev "$LOOP" 5)"
mkext ext4 ^metadata_csum,^64bit rw-root "$(rw_part_dev "$LOOP" 6)"
mkswap "$(rw_part_dev "$LOOP" 7)" > /dev/null || die "mkswap"

mkdir "$MNT/p" || die "mkdir mnt"
fill() {   # fill <partnum> <tar>...
    local n="$1"; shift
    mount "$(rw_part_dev "$LOOP" "$n")" "$MNT/p" || die "mount p$n"
    for t in "$@"; do
        tar --numeric-owner -xpf "$t" -C "$MNT/p" || die "extract $t into p$n"
    done
    if [ "$n" = 6 ]; then
        # The three data partitions mount here; their content is on p2/p3/p5.
        mkdir -p "$MNT/p/home/root/data" "$MNT/p/home/root/log" "$MNT/p/home/root/backup"
        [ ! -d "$MNT/p/home/root/.ssh" ] || chmod 700 "$MNT/p/home/root/.ssh"
        [ ! -f "$MNT/p/home/root/.ssh/authorized_keys" ] || chmod 600 "$MNT/p/home/root/.ssh/authorized_keys"
        echo "p6 holds $(find "$MNT/p" -xdev | wc -l) entries; files inside the three mount points: $(find "$MNT/p/home/root/data" "$MNT/p/home/root/log" "$MNT/p/home/root/backup" -mindepth 1 | wc -l)"
    fi
    sync
    umount "$MNT/p" || die "umount p$n"
}
echo "== filling partitions"
fill 2 "$PARTS/p2.tar"
fill 3 "$PARTS/p3.tar"
fill 5 "$PARTS/p5.tar"
fill 6 "$ROOTTAR" "$PARTS/state.tar"

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

echo "== head check"
IMG_MD5=$(dd if="$OUTABS" bs=512 count="$P2START" status=none | md5sum | awk '{print $1}')
if [ "$IMG_MD5" = "$WANT_MD5" ]; then MATCH=yes; else MATCH=no; fi

STATE_N=$(tar -tf "$PARTS/state.tar" | grep -vc '/$')
echo
echo "==================== SUMMARY ===================="
echo "image:           $OUTABS"
echo "size:            $(stat -c %s "$OUTABS") bytes apparent, $(du -h "$OUTABS" | awk '{print $1}') allocated"
echo "rootfs tar:      $ROOTTAR ($(stat -c %y "$ROOTTAR"))"
echo "head md5 match:  $MATCH (sectors 0..$((P2START - 1)) vs the unit's md5)"
echo "fsck:            $([ "$FSCK_BAD" -eq 0 ] && echo 'all four clean' || echo 'PROBLEMS, see above')"
echo "state carried:   $STATE_N non-directory entries from the old p6"
echo
echo "Next, on Windows: copy the image out of WSL, e.g."
printf '  copy \\\\wsl$\\<distro>%s C:\\work\\rw-scratch-s0930\\s1000\\card.img\n' "$OUTABS"
echo "(outside the repo), write it with Rufus, balenaEtcher or Win32 Disk Imager to the"
echo "8 GB card (the operator authorized wiping that card), then put it in the unit."
echo "The original card stays untouched as the fallback."
[ "$MATCH" = yes ] && [ "$FSCK_BAD" -eq 0 ]
