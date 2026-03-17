#!/bin/sh
# disable-steelcase.sh — Disable non-essential Steelcase firmware services
#
# Idempotent: safe to run multiple times.  Called by:
#   - setup-device.sh  (one-time commissioning over SSH)
#   - roomwizard-app-init.sh  (on every boot as a safety net)
#
# What it does:
#   1. Creates /var/watchdog_test bypass file (disables Steelcase software watchdog)
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

# ── 0. Clean shell profile references to deleted Steelcase configs ─────────
# The factory /etc/profile sources wsplatform.conf which no longer exists
# after bloatware removal, causing "-sh: ...wsplatform.conf: No such file"
# on every login.
sed -i '/wsplatform\.conf/d' /etc/profile 2>/dev/null

# ── 1. Software watchdog bypass ────────────────────────────────────────────
# The Steelcase watchdog_test.sh exits 0 immediately when this file exists.
touch /var/watchdog_test

# ── 2. Install clean crontab ─────────────────────────────────────────────
# Replace the entire Steelcase crontab with only essential maintenance tasks.
# Previous approach used sed to comment out lines, which added duplicate headers
# on every run and inflated the crontab to ~19KB. Writing fresh prevents bloat.
echo "  Installing clean crontab..."
if [ -d "/opt/sbin/cleanup" ]; then
    crontab - << 'CRONTAB_EOF'
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
    find /etc/rc*.d/ -name "S*${pattern}*" -type l -delete 2>/dev/null
done

echo "  Services: non-essential stopped and disabled"
