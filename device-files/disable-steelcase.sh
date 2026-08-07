#!/bin/sh
# disable-steelcase.sh — Disable non-essential Steelcase firmware services
#
# Idempotent: safe to run multiple times.  Called by:
#   - commissioning/provision.sh  (one-time commissioning over SSH)
#   - device-files/roomwizard-app  (on every boot as a safety net)
#
# What it does:
#   0. Creates /var/watchdog_test bypass file (disables Steelcase software watchdog)
#   1. Cleans shell profile references to deleted Steelcase configs
#   2. Installs clean crontab (replaces bloated Steelcase crontab)
#   3. Stops and kill-9s conflicting services/processes
#   4. Removes boot symlinks for conflicting services
#   5. Makes dangerous init scripts non-executable
#   6. Removes unnecessary boot service symlinks
#
# What it keeps:
#   - Hardware watchdog daemon (/usr/sbin/watchdog)
#   - sshd, cron, dbus
#   - rotatelogfiles.sh, cleanupfiles.sh (cron, if present)
#   - audio-enable, time-sync, roomwizard-app (init)

set -e

# Everything below is best-effort cleanup on a device whose factory filesystem
# has already been partly deleted (commissioning/provision.sh --remove/--deep-clean), so a
# missing file is normal and must never abort the run.  Under `set -e` it did:
# the /etc/profile sed below was unguarded and ran *before* the watchdog bypass,
# so on a device with no /etc/profile this script died at its second command and
# left the Steelcase software watchdog armed - rebooting the device every ~70
# minutes.  It runs on every boot from device-files/roomwizard-app, which does not
# check the exit status, so the failure was completely invisible.  (B18.)
#
# Two rules follow, and the ordering is as load-bearing as the guards:
#   - guard every command that may legitimately fail with `|| true` (or a warning)
#   - do the cheapest, most important thing FIRST, so nothing else can skip it

# ── 0. Software watchdog bypass ────────────────────────────────────────────
# The Steelcase watchdog_test.sh exits 0 immediately when this file exists.
# This is step 0 on purpose: it is one syscall, it is the whole reason the
# device stays up, and putting it ahead of every fallible command means a
# future unguarded line cannot re-arm the watchdog the way the sed did.
touch /var/watchdog_test || echo "  WARNING: cannot create /var/watchdog_test - Steelcase watchdog stays ARMED"

# ── 1. Clean shell profile references to deleted Steelcase configs ─────────
# The factory /etc/profile sources wsplatform.conf which no longer exists
# after bloatware removal, causing "-sh: ...wsplatform.conf: No such file"
# on every login.
sed -i '/wsplatform\.conf/d' /etc/profile 2>/dev/null || true

# ── 2. Install clean crontab ─────────────────────────────────────────────
# Replace the entire Steelcase crontab with only essential maintenance tasks.
# Previous approach used sed to comment out lines, which added duplicate headers
# on every run and inflated the crontab to ~19KB. Writing fresh prevents bloat.
echo "  Installing clean crontab..."
if [ -d "/opt/sbin/cleanup" ]; then
    crontab - << 'CRONTAB_EOF' || echo "  Cron: WARNING - crontab install failed, Steelcase cron may still be active"
# RoomWizard crontab - managed by disable-steelcase.sh
0 */4 * * * /opt/sbin/cleanup/rotatelogfiles.sh 1>/dev/null 2>/dev/null
5 */4 * * * /opt/sbin/cleanup/cleanupfiles.sh 1>/dev/null 2>/dev/null
CRONTAB_EOF
    echo "  Cron: clean crontab installed (kept: rotatelogfiles, cleanupfiles)"
else
    # No cleanup scripts available (removed by --remove), empty crontab
    crontab -r 2>/dev/null || true
    echo "  Cron: crontab cleared (cleanup scripts not found)"
fi

# ── 3. Stop conflicting services ──────────────────────────────────────────
for svc in browser webserver x11 jetty hsqldb snmpd vsftpd nullmailer ntpd startautoupgrade; do
    [ -x "/etc/init.d/$svc" ] && /etc/init.d/$svc stop 2>/dev/null || true
done
sleep 1

# Force-kill any remaining processes
for proc in java Xorg browser epiphany webkit psplash nullmailer-send ntpd; do
    killall -9 "$proc" 2>/dev/null || true
done

# ── 4. Disable conflicting services from boot ─────────────────────────────
for svc in browser webserver x11 jetty hsqldb snmpd vsftpd nullmailer ntpd startautoupgrade; do
    rm -f /etc/rc5.d/S*${svc} /etc/rc5.d/K*${svc} 2>/dev/null || true
    rm -f /etc/rc2.d/S*${svc} /etc/rc3.d/S*${svc} /etc/rc4.d/S*${svc} 2>/dev/null || true
    update-rc.d -f "$svc" remove 2>/dev/null || true
done

# ── 5. Make dangerous init scripts non-executable ─────────────────────────
# Prevent accidental re-enable of disabled services
echo "  Making dangerous init scripts non-executable..."
for svc in browser webserver x11 jetty hsqldb snmpd vsftpd nullmailer ntpd startautoupgrade webmonitor mta.sh; do
    if [ -f "/etc/init.d/$svc" ]; then
        chmod -x "/etc/init.d/$svc" 2>/dev/null || true
    fi
done

# ── 6. Remove unnecessary boot service symlinks ──────────────────────────
# These are non-essential Steelcase boot services still symlinked
echo "  Removing unnecessary boot service symlinks..."
for pattern in cursor.sh bootscrub cleaup_partition upgradecomplete psplash wpa_supplicant networkmanager rmnologin; do
    # `find /etc/rc*.d/` exits non-zero when a directory in the glob is absent -
    # and if the glob matches nothing at all it is passed through literally.
    find /etc/rc*.d/ -name "S*${pattern}*" -type l -delete 2>/dev/null || true
done

echo "  Services: non-essential stopped and disabled"

# ── 7. Report the one thing that must have worked ─────────────────────────
# The other half of B18 was that nobody could see the failure: this runs on
# every boot from device-files/roomwizard-app, which does not check the exit status.
# Say out loud whether the bypass is in place, so `commissioning/provision.sh` output and
# the boot log both carry the answer.
if [ -f /var/watchdog_test ]; then
    echo "  Watchdog: Steelcase software watchdog bypassed (/var/watchdog_test present)"
else
    echo "  Watchdog: WARNING - /var/watchdog_test MISSING, device will reboot every ~70 min"
fi
