#!/bin/bash
# fetch-card-parts.sh <unit-ip> <outdir>
#
# Reads the pieces of a running RoomWizard's SD card that make-card-image.sh
# needs: the partition table and p1 (raw), the extended-partition boot records
# and the per-unit files of the live p6. The three data partitions are NOT read:
# the new card gets them empty (nothing of the vendor stays on it).
#
# READ-ONLY on the unit: everything streams over ssh stdout, nothing is written
# there. Run it as the normal WSL user (that user's key reaches the unit under
# key auth; WSL root has no key). The unit's tar is BusyBox: it records
# numeric ids, and GNU tar extracts them with --numeric-owner.
set -u

[ $# -eq 2 ] || { echo "usage: $0 <unit-ip> <outdir>" >&2; exit 2; }
IP="$1"
OUT="$2"
[ "$(id -u)" -ne 0 ] || { echo "run as the normal user, not root (root has no ssh key)" >&2; exit 2; }
case "$OUT" in /mnt/*) echo "outdir must be on WSL's native fs, not DrvFs: $OUT" >&2; exit 2 ;; esac

# shellcheck source=lib/rw-ssh.sh
source "$(dirname "$0")/../lib/rw-ssh.sh"
rw_ssh_gate "root@$IP" || { echo "cannot continue without SSH to root@$IP" >&2; exit 1; }
SSH=(ssh -o "ConnectTimeout=10" "root@$IP")
mkdir -p "$OUT" || exit 1

die() { echo "FAIL: $*" >&2; exit 1; }

# ssh prints a post-quantum warning on stderr for this old sshd; keep it out of
# the log noise but let real errors through.
rssh() { "${SSH[@]}" "$@" < /dev/null 2> >(grep -v -i -e 'quantum' -e 'vulnerable' -e 'upgraded' >&2); }

echo "== geometry"
GEOM=$(cat <<'REMOTE'
cd /sys/block/mmcblk0 || exit 1
echo "mmcblk0 0 $(cat size)"
for p in mmcblk0p*; do echo "$p $(cat "$p/start") $(cat "$p/size")"; done
REMOTE
)
rssh "$GEOM" > "$OUT/geometry.txt" || die "geometry"
cat "$OUT/geometry.txt"

P2START=$(awk '$1=="mmcblk0p2"{print $2}' "$OUT/geometry.txt")
[ -n "$P2START" ] || die "no p2 in geometry"

echo "== head.bin: sectors 0..$((P2START - 1)) (MBR, gap, p1)"
rssh "dd if=/dev/mmcblk0 bs=512 count=$P2START 2>/dev/null" > "$OUT/head.bin" || die "head dd"
[ "$(stat -c %s "$OUT/head.bin")" -eq $((P2START * 512)) ] || die "head.bin has the wrong size"
rssh "dd if=/dev/mmcblk0 bs=512 count=$P2START 2>/dev/null | md5sum" | awk '{print $1}' > "$OUT/md5.txt" \
    || die "head md5"
echo "unit md5 $(cat "$OUT/md5.txt")  local $(md5sum < "$OUT/head.bin" | awk '{print $1}')"

echo "== EBRs (whole 63-sector gap before each logical partition)"
while read -r name start _; do
    case "$name" in mmcblk0p[5-9]|mmcblk0p[1-9][0-9]) ;; *) continue ;; esac
    n="${name#mmcblk0p}"
    rssh "dd if=/dev/mmcblk0 bs=512 skip=$((start - 63)) count=63 2>/dev/null" > "$OUT/ebr-$n.bin" \
        || die "ebr $n"
    sig=$(od -An -tx1 -j510 -N2 "$OUT/ebr-$n.bin" | tr -d ' ')
    echo "ebr-$n: sector $((start - 63)), signature $sig"
    [ "$sig" = 55aa ] || die "ebr-$n has no 55AA signature"
done < "$OUT/geometry.txt"

echo "== per-unit state of the live p6"
# The list is built on the unit so that only files which exist are named, and
# is relative to /, so a symlink such as /etc/localtime is archived as one.
LISTER=$(cat <<'REMOTE'
cd / || exit 1
for f in etc/touch_calibration.conf etc/touch_calibration.conf.bak* etc/input_config.conf opt/games/rw_config.conf opt/games/scummvm.ini opt/vnc_client/vnc_client.conf var/lib/alsa/asound.state var/lib/bluetooth var/lib/bluealsa etc/hostname etc/hosts etc/timezone etc/localtime home/root/.ssh opt/roomwizard/default-app; do
    if [ -e "$f" ] || [ -L "$f" ]; then echo "$f"; fi
done
for f in home/root/*; do
    case "$f" in home/root/uImage-*) continue ;; esac
    if [ -f "$f" ]; then echo "$f"; fi
done
REMOTE
)
rssh "$LISTER" > "$OUT/state.list" || die "state list"
if grep -q '[[:space:]]' "$OUT/state.list"; then die "a state file name contains whitespace"; fi
cat "$OUT/state.list"
rssh "cd / && tar -cf - $(tr '\n' ' ' < "$OUT/state.list")" > "$OUT/state.tar" || die "state tar"
echo "state entries: $(wc -l < "$OUT/state.list")"

echo "== done"
ls -l "$OUT"
