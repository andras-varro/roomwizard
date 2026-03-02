#!/bin/sh
# disable-steelcase.sh — Disable non-essential Steelcase firmware services
#
# Idempotent: safe to run multiple times.  Called by:
#   - setup-device.sh  (one-time commissioning over SSH)
#   - roomwizard-app-init.sh  (on every boot as a safety net)
#
# What it does:
#   1. Creates /var/watchdog_test bypass file (disables Steelcase software watchdog)
#   2. Comments out non-essential cron jobs (persistent — only modifies if needed)
#   3. Stops and kill-9s conflicting services/processes
#   4. Removes boot symlinks for conflicting services
#
# What it keeps:
#   - Hardware watchdog daemon (/usr/sbin/watchdog)
#   - sshd, cron, dbus
#   - rotatelogfiles.sh, cleanupfiles.sh (cron)
#   - audio-enable, time-sync, roomwizard-app (init)

set -e

# ── 1. Software watchdog bypass ────────────────────────────────────────────
# The Steelcase watchdog_test.sh exits 0 immediately when this file exists.
touch /var/watchdog_test

# ── 2. Disable non-essential cron jobs ─────────────────────────────────────
# Only modify if there are still active /opt/sbin/ jobs (idempotent).
if crontab -l 2>/dev/null | grep -q '^[^#].*/opt/sbin/'; then
    # Back up original crontab (only if no backup exists yet)
    [ ! -f /var/crontab.steelcase.bak ] && \
        crontab -l > /var/crontab.steelcase.bak 2>/dev/null || true

    crontab -l 2>/dev/null | sed \
        -e 's|^\([^#].*/opt/sbin/watchdog/watchdog\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/get_time_from_server\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/clock/sync_clocks\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/cleanup/rotatedbtables\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/backup/backup\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/scheduledusagereport\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/gettimestamp\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/remove_older_sync_meetings\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/runfsck\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/cleanup/checkformemoryusage\.sh\)|# DISABLED \1|' \
        -e 's|^\([^#].*/opt/sbin/backlight/adjustbklight\.sh\)|# DISABLED \1|' \
        | crontab -
    echo "  Cron: non-essential jobs disabled (kept: rotatelogfiles, cleanupfiles)"
else
    echo "  Cron: already clean"
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

echo "  Services: non-essential stopped and disabled"
