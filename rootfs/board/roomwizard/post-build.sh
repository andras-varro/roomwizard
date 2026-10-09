#!/bin/sh
# Buildroot post-build hook: runs after the overlay is copied in and before the tarball is
# made. $1 is the target directory. It does what an overlay cannot:
#   - Git on Windows stores no symlinks and no modes, and the overlay is copied from a
#     DrvFs checkout where every file looks 0777; so every link is made here and every mode
#     is set here.
#   - Buildroot's own /etc/init.d/S??* scripts are removed so nothing starts twice.
#   - /etc/passwd and /etc/shadow are edited after Buildroot has generated them.
set -eu

TARGET=${1:?usage: post-build.sh <target dir>}
OVERLAY=$(cd "$(dirname "$0")" && pwd)/overlay

# Modes. Init scripts are executable, every other overlay file is plain data.
(cd "$OVERLAY" && find . -type d) | while read -r d; do
    chmod 0755 "$TARGET/$d"
done
(cd "$OVERLAY" && find . -type f) | while read -r f; do
    case "$f" in
        ./etc/init.d/*) chmod 0755 "$TARGET/$f" ;;
        *) chmod 0644 "$TARGET/$f" ;;
    esac
done

# Buildroot's own boot scripts. Ours are installed under plain names by the overlay.
rm -f "$TARGET"/etc/init.d/S??* "$TARGET"/etc/init.d/rcK

# SysV runlevel links. Relative, so they resolve inside a mounted card as well.
link() { # <runlevel dir> <link name> <script in init.d>
    mkdir -p "$TARGET/etc/$1"
    ln -sfn "../init.d/$3" "$TARGET/etc/$1/$2"
}
link rcS.d S01mountall    mountall
# After mountall by name order: seedrng writes its new seed to the root filesystem.
link rcS.d S01seedrng     seedrng
link rcS.d S02alignment   alignment
link rcS.d S03udev        udev
link rcS.d S04hostname    hostname
link rcS.d S05sysctl      sysctl
link rcS.d S06watchdog    watchdog
link rcS.d S07syslog      syslog
link rcS.d S08networking  networking
link rc5.d S02dbus-1      dbus-1
link rc5.d S09sshd        sshd
link rc5.d S20hwclock.sh  hwclock.sh
for lvl in rc0.d rc6.d; do
    link $lvl K10sshd       sshd
    link $lvl K20dbus-1     dbus-1
    link $lvl K30hwclock.sh hwclock.sh
    link $lvl K40syslog     syslog
    link $lvl K50seedrng    seedrng
    link $lvl K99watchdog   watchdog
done
mkdir -p "$TARGET/etc/rc1.d" "$TARGET/etc/rc2.d" "$TARGET/etc/rc3.d" "$TARGET/etc/rc4.d"

# /var layout: /var/volatile is a tmpfs (fstab); the rest of the volatile directories link
# into it, /var/log is the log partition, and /run and /tmp follow /var. The skeleton makes
# /run a real directory and /var/run a link to it, so both are replaced.
rm -rf "$TARGET/run" "$TARGET/tmp" "$TARGET/var/run" "$TARGET/var/lock" \
    "$TARGET/var/tmp" "$TARGET/var/cache" "$TARGET/var/log" "$TARGET/var/volatile"
# The directories exist in the image as well as on the tmpfs: Buildroot creates the dbus
# user's home /run/dbus after this script, and mkdir through a dangling link fails.
mkdir -p "$TARGET/var/volatile/run" "$TARGET/var/volatile/lock" "$TARGET/var/volatile/tmp" \
    "$TARGET/var/volatile/cache"
ln -s volatile/run "$TARGET/var/run"
ln -s volatile/lock "$TARGET/var/lock"
ln -s volatile/tmp "$TARGET/var/tmp"
ln -s volatile/cache "$TARGET/var/cache"
ln -s /home/root/log "$TARGET/var/log"
ln -s var/run "$TARGET/run"
ln -s var/tmp "$TARGET/tmp"
# udhcpc's default script writes /etc/resolv.conf; it lands on the tmpfs.
rm -f "$TARGET/etc/resolv.conf"
ln -s /var/run/resolv.conf "$TARGET/etc/resolv.conf"

# Mount points for the SD card partitions and the app tree.
mkdir -p "$TARGET/home/root/data" "$TARGET/home/root/log" "$TARGET/home/root/backup" \
    "$TARGET/opt/games" "$TARGET/opt/roomwizard" "$TARGET/media"
mkdir -p "$TARGET/home/root/.ssh"
chmod 0700 "$TARGET/home/root/.ssh"
chmod 0755 "$TARGET/home/root"

# sshd host keys live on the data partition; /etc/init.d/sshd creates them there.
for t in ed25519 rsa; do
    rm -f "$TARGET/etc/ssh/ssh_host_${t}_key" "$TARGET/etc/ssh/ssh_host_${t}_key.pub"
    ln -s "/home/root/data/ssh/ssh_host_${t}_key" "$TARGET/etc/ssh/ssh_host_${t}_key"
    ln -s "/home/root/data/ssh/ssh_host_${t}_key.pub" "$TARGET/etc/ssh/ssh_host_${t}_key.pub"
done

# Root: home on the card's tree, password locked (key-only login).
sed -i 's|^root:\([^:]*\):0:0:root:/root:|root:\1:0:0:root:/home/root:|' "$TARGET/etc/passwd"
sed -i 's|^root:[^:]*:|root:*:|' "$TARGET/etc/shadow"

exit 0
