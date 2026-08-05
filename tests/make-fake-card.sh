#!/bin/bash
#
# make-fake-card.sh — build a synthetic MOUNTED RoomWizard card under $1.
#
# NOT a repo tool: a scratch fixture builder for exercising commission-offline.sh
# without a card and without a device. It is committed because the alternative is
# retyping it, and because the one thing it must get right — REAL SYMLINKS in
# rc*.d — is exactly what partitions/ and partitions.new/ lost on the way through
# Windows (CLAUDE.md → "Working from this host").
#
#   wsl.exe -u root -e bash -lc "cd /mnt/c/work/roomwizard && tests/make-fake-card.sh /tmp/fake"
#
# Build it under /tmp, never under /mnt/c: DrvFs cannot hold a symlink and
# discards chmod, so a fixture there can neither carry a boot link nor show a
# missing +x.
set -eu

BASE="${1:?usage: make-fake-card.sh <dir>}"
case "$BASE" in
    /tmp/*) ;;
    *) echo "refusing to build a fixture outside /tmp: $BASE" >&2; exit 1 ;;
esac
rm -rf "$BASE"

mkdir -p "$BASE"/root/etc/{init.d,rcS.d,rc0.d,rc2.d,rc3.d,rc4.d,rc5.d,rc6.d,ssh,network,sysctl.d} \
         "$BASE"/root/{opt,usr/sbin,usr/lib,usr/share,var/cron/tabs,var/log,home/root} \
         "$BASE"/data/{lost+found,cron/tabs} "$BASE"/log/lost+found \
         "$BASE"/backup/{lost+found,factory}

# rw_is_rootfs's four required files plus a vendor marker and the banner.
: > "$BASE/root/etc/shadow"
echo 'root:$6$x$y:19000:0:99999:7:::'    > "$BASE/root/etc/shadow"
printf '127.0.0.1 localhost\n10.9.8.7 RW09\n' > "$BASE/root/etc/hosts"
printf 'RW09\n' > "$BASE/root/etc/hostname"
printf 'send host-name "RW09";\nrequest subnet-mask;\n' > "$BASE/root/etc/dhclient.conf"
printf 'PermitRootLogin no\nPermitEmptyPasswords yes\n#PasswordAuthentication no\n' \
    > "$BASE/root/etc/ssh/sshd_config"
printf 'auto lo\niface lo inet loopback\n\nauto eth0\niface eth0 inet static\n  address 10.9.8.7\n' \
    > "$BASE/root/etc/network/interfaces"
echo 'SteelCase RW20 Embedded Platform (Yocto) 3.1.4 \n \l' > "$BASE/root/etc/issue"
echo '20180309123456' > "$BASE/root/etc/version"
printf '4:12345:respawn:/sbin/getty 38400 tty4\n' > "$BASE/root/etc/inittab"
printf '. /opt/sbin/wsplatform.conf\n' > "$BASE/root/etc/profile"

# Vendor init scripts, and REAL symlinks pointing at them.
for n in banner.sh sysfs.sh udev modutils.sh alignment.sh devpts.sh checkroot.sh procps.sh \
         ramdisk mountall.sh populate-volatile.sh hostname.sh networking syslog mountnfs.sh \
         bootmisc.sh finish.sh dbus-1 sshd cron ctrlblk watchdog hwclock.sh stop-bootlogd \
         avahi-daemon psplash wpa_supplicant networkmanager cursor.sh bootscrub \
         cleaup_partition rmnologin.sh webserver jetty browser x11 hsqldb snmpd vsftpd \
         nullmailer startautoupgrade webmonitor mta.sh ntpd halt reboot \
         rwconnectord asl-presenced; do
    printf '#!/bin/sh\n# vendor stub\nexit 0\n' > "$BASE/root/etc/init.d/$n"
    chmod 755 "$BASE/root/etc/init.d/$n"
done

l() { ln -sf "../init.d/$2" "$BASE/root/etc/$1/$3"; }
for p in S02:banner.sh:S02banner.sh S03:sysfs.sh:S03sysfs.sh; do :; done
l rcS.d banner.sh S02banner.sh;            l rcS.d sysfs.sh S03sysfs.sh
l rcS.d udev S04udev;                      l rcS.d modutils.sh S05modutils.sh
l rcS.d alignment.sh S06alignment.sh;      l rcS.d devpts.sh S06devpts.sh
l rcS.d checkroot.sh S10checkroot.sh;      l rcS.d procps.sh S30procps.sh
l rcS.d ramdisk S30ramdisk;                l rcS.d mountall.sh S35mountall.sh
l rcS.d populate-volatile.sh S37populate-volatile.sh
l rcS.d hostname.sh S39hostname.sh;        l rcS.d networking S40networking
l rcS.d syslog S43syslog;                  l rcS.d mountnfs.sh S45mountnfs.sh
l rcS.d bootmisc.sh S55bootmisc.sh;        l rcS.d finish.sh S99finish.sh
# must be swept
l rcS.d psplash S01psplash;                l rcS.d wpa_supplicant S58wpa_supplicant
l rcS.d networkmanager S60networkmanager

l rc5.d dbus-1 S02dbus-1;                  l rc5.d sshd S09sshd
l rc5.d cron S20cron;                      l rc5.d ctrlblk S40ctrlblk
l rc5.d watchdog S50watchdog
# must be swept
l rc5.d webserver S15webserver;            l rc5.d jetty S17jetty
l rc5.d hsqldb S18hsqldb;                  l rc5.d browser S19browser
l rc5.d cursor.sh S21cursor.sh;            l rc5.d x11 S22x11
l rc5.d snmpd S23snmpd;                    l rc5.d vsftpd S24vsftpd
l rc5.d nullmailer S25nullmailer;          l rc5.d startautoupgrade S26startautoupgrade
l rc5.d webmonitor S27webmonitor;          l rc5.d bootscrub S75bootscrub
l rc5.d cleaup_partition S80cleaup_partition
l rc5.d rmnologin.sh S99rmnologin.sh;      l rc5.d rwconnectord S46rwconnectord
l rc5.d asl-presenced S47asl-presenced

for d in rc2.d rc3.d rc4.d; do
    l "$d" dbus-1 S02dbus-1;   l "$d" sshd S09sshd
    l "$d" hwclock.sh S20hwclock.sh; l "$d" ctrlblk S40ctrlblk
    l "$d" watchdog S50watchdog;     l "$d" stop-bootlogd S99stop-bootlogd
    l "$d" browser S19browser        # swept
done
for n in K09sshd K20dbus-1 K85watchdog S20sendsigs S25save-rtc.sh S40umountfs S90halt; do
    l rc0.d halt "$n"
done
for n in K09sshd K20dbus-1 K85watchdog S20sendsigs S25save-rtc.sh S40umountfs S90reboot; do
    l rc6.d reboot "$n"
done

# The stock /opt, plus an invented vendor directory.
mkdir -p "$BASE"/root/opt/{hsqldb,jetty-9-4-11,openjre-8,pv02,sound,sbin/watchdog,rwconnector}
: > "$BASE/root/opt/sbin/watchdog/watchdog.sh"
: > "$BASE/root/opt/sbin/networkmanager"
: > "$BASE/root/opt/sound/asl_click.wav"
: > "$BASE/root/opt/sbin/wsplatform.conf"

# Base-OS stacks, and the things that must survive.
mkdir -p "$BASE"/root/usr/share/{cjkfont,snmp,X11} "$BASE/root/usr/lib/ts"
: > "$BASE/root/usr/lib/libwebkit2gtk-4.0.so.37"
: > "$BASE/root/usr/lib/libc.so.6"
: > "$BASE/root/usr/sbin/snmpd"
: > "$BASE/root/usr/sbin/avahi-daemon"
mkdir -p "$BASE/root/etc/avahi"; : > "$BASE/root/etc/avahi/avahi-daemon.conf"
: > "$BASE/root/var/log/browser.err"

# p2: the crontab reached through the p6 symlink, high scores, and websign.
printf '# vendor\n*/5 * * * * /opt/sbin/watchdog/watchdog.sh\n' > "$BASE/data/cron/tabs/root"
printf 'noise\n' > "$BASE/data/cron/log"
ln -sf /home/root/data/cron/tabs/root "$BASE/root/var/cron/tabs/root"
mkdir -p "$BASE/data/websign" "$BASE/data/roombooker" "$BASE/data/rwmeetingcache"
printf 'manual\n' > "$BASE/data/websign/net.mode"
printf 'RW09\n'   > "$BASE/data/websign/net.hostname"
: > "$BASE/data/test.hex"
: > "$BASE/data/snake.hig"

# p3 and p5.
: > "$BASE/log/Xorg.0.log"
: > "$BASE/backup/serialno"
: > "$BASE/backup/pointercal"
: > "$BASE/backup/factory/uImage-system-original"
: > "$BASE/backup/factory/sd_rootfs_part.img"
mkdir -p "$BASE/backup/websigns"

echo "fixture built: $BASE"
