#!/bin/bash
#
# setup-device.sh — Phase 2: One-time system setup over SSH
#
# Run this ONCE after the first SSH login to a freshly commissioned device.
# It disables Steelcase bloatware, installs the generic app launcher, and
# sets up audio + time-sync boot scripts.
#
# Usage:
#   ./setup-device.sh <ip>                    # system setup + reboot
#   ./setup-device.sh <ip> --remove           # + remove bloatware files (~178 MB freed)
#   ./setup-device.sh <ip> --status           # show device status only
#
# Prerequisites:
#   - Device commissioned with commission-roomwizard.sh (Phase 1)
#   - SSH access as root
#
# What it does:
#   1. Deploys disable-steelcase.sh → /opt/roomwizard/
#   2. Runs disable-steelcase.sh (watchdog bypass, cron cleanup, service stop)
#   3. Installs roomwizard-app-init.sh as /etc/init.d/roomwizard-app
#      Registers the init service (priority S99)
#      Deploys audio-enable + time-sync boot scripts
#   4. Hardens SSH (PermitEmptyPasswords=no, brute-force limits)
#   5. Applies kernel/sysctl security settings (ASLR, no ip_forward, etc.)
#   6. Optionally removes bloatware files + Steelcase artifacts (--remove)
#   7. Reboots device
#
# NOTE: No iptables firewall — the Steelcase firmware ships without iptables
# userspace tools, the ip_tables.ko kernel module is not included in
# /lib/modules/4.14.52, and there's no package manager to install them.
# The kernel has CONFIG_NETFILTER=y but no ip_tables module was built.
# Network security relies on: SSH hardening + sysctl + disabling services.
#
# After this, deploy a project and set it as the default app:
#   cd native_apps && ./build-and-deploy.sh <ip> set-default
#   cd vnc_client   && ./build-and-deploy.sh <ip> set-default

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

DEVICE_IP="${1:-}"
FLAG="${2:-}"
DEVICE="root@${DEVICE_IP}"
REMOTE_DIR="/opt/roomwizard"
INIT_SCRIPT="/etc/init.d/roomwizard-app"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }

# ── usage ───────────────────────────────────────────────────────────────────
usage() {
    echo "Usage: $0 <ip> [--remove|--deep-clean|--status] [--dry-run]"
    echo ""
    echo "  <ip>              Device IP address"
    echo "  --remove          Also remove vendor bloatware files (~178 MB freed)"
    echo "  --deep-clean      --remove PLUS extended cleanup (~560 MB more)."
    echo "                    Includes the 474 MB on-device factory restore image."
    echo "                    See IMPROVEMENT_PLAN.md and the warnings it prints."
    echo "  --status          Show device status only (no changes)"
    echo "  --dry-run         With --deep-clean: list what would be deleted, delete nothing"
    exit 1
}

[[ -z "$DEVICE_IP" ]] && usage

# Validate the IP before doing anything: every step past here is destructive and
# ends in a reboot (../IMPROVEMENT_PLAN.md B19).
IPV4_RE='^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}$'
if [[ ! "$DEVICE_IP" =~ $IPV4_RE ]]; then
    echo "Not an IPv4 address: $DEVICE_IP"
    echo ""
    usage
fi

# ── deployed-script drift ───────────────────────────────────────────────────
# These two files are the only ones this script copies from the repo, and they
# reach a device ONLY through here — deploy-all.sh does not push them.  So a
# device can silently run an older copy than the repo's, with nothing saying so:
# while B18 was being reproduced, RW09's disable-steelcase.sh turned out to be
# older than the repo's *pre-fix* copy, which is why that repro had to stage the
# tracked file to /tmp instead of using the device's own.  md5 settles it in one
# command (../IMPROVEMENT_PLAN.md B19).
#
# Byte comparison is valid because .gitattributes pins *.sh to eol=lf, so the
# working tree is LF even on this Windows host and scp copies it unchanged.
report_script_versions() {
    local pairs=(
        "$SCRIPT_DIR/disable-steelcase.sh:$REMOTE_DIR/disable-steelcase.sh"
        "$SCRIPT_DIR/roomwizard-app-init.sh:$INIT_SCRIPT"
    )
    local pair local_path remote_path local_md5 remote_md5 drift=0
    for pair in "${pairs[@]}"; do
        local_path="${pair%%:*}"
        remote_path="${pair#*:}"
        local_md5="$(md5sum "$local_path" | cut -d' ' -f1)"
        remote_md5="$(ssh "$DEVICE" "md5sum $remote_path 2>/dev/null" 2>/dev/null | cut -d' ' -f1 || true)"
        if [[ -z "$remote_md5" ]]; then
            warn "$(basename "$remote_path"): NOT INSTALLED"
            drift=1
        elif [[ "$local_md5" == "$remote_md5" ]]; then
            ok "$(basename "$remote_path"): matches repo (${local_md5:0:8})"
        else
            warn "$(basename "$remote_path"): DRIFTED — device ${remote_md5:0:8}, repo ${local_md5:0:8}"
            drift=1
        fi
    done
    if [[ $drift -ne 0 ]]; then
        warn "Run '$0 $DEVICE_IP' to push the repo's versions (this REBOOTS the device)"
    fi
    return 0
}

# Reject unknown flags rather than silently falling through to a full setup+reboot
case "$FLAG" in
    ""|--remove|--deep-clean|--status) ;;
    *) echo "Unknown option: $FLAG"; echo ""; usage ;;
esac

DRY_RUN="${3:-}"
if [[ -n "$DRY_RUN" && "$DRY_RUN" != "--dry-run" ]]; then
    echo "Unknown option: $DRY_RUN"; echo ""; usage
fi

# Basic sanity on the target so a typo doesn't run commands against the wrong host
if ! echo "$DEVICE_IP" | grep -qE '^([0-9]{1,3}\.){3}[0-9]{1,3}$|^[a-zA-Z][a-zA-Z0-9.-]*$'; then
    err "'$DEVICE_IP' does not look like an IP address or hostname"
fi

# --deep-clean implies --remove
DEEP_CLEAN=0
if [[ "$FLAG" == "--deep-clean" ]]; then
    DEEP_CLEAN=1
    FLAG="--remove"
fi

# ── deep clean ──────────────────────────────────────────────────────────────
# Extended cleanup beyond the vendor-bloatware list above. Everything here was
# verified unused on a live device (RW09) on 2026-07-29. The enabling fact is
# that every binary this project ships is statically linked -- `ldd` reports
# "not a dynamic executable" for app_launcher, vnc_client and scummvm -- so the
# only dynamic consumers left are sshd, dbus, syslogd, cron, watchdog, udevd,
# dhclient and busybox. Nothing below is in their closure.
#
# Deliberately NOT included (could not be proven safe):
#   /usr/share/fonts      4.5 MB - ScummVM glyph source not ruled out
#   /usr/lib/locale       2.9 MB - static ScummVM embeds locale-archive paths
#   /usr/lib/perl5        2.5 MB - grep for #!/usr/bin/perl shebangs first
#   /opt/sbin/*           1.4 MB - entangled with ctrlblk / restore.sh
run_deep_clean() {
    echo ""
    echo "════════════════════════════════════════"
    echo " Deep Clean"
    echo "════════════════════════════════════════"

    DEL_FACTORY=0
    if [[ "$DRY_RUN" == "--dry-run" ]]; then
        warn "DRY RUN — nothing will be deleted"
        DEL_FACTORY=1   # so the dry run shows the factory images too
        confirm2="yes"
    else
        warn "This permanently deletes ~85 MB of rootfs + data-partition files."
        read -p "Continue? (yes/no): " confirm2

        if [[ "$confirm2" == "yes" ]]; then
            echo ""
            warn "OPTIONAL: also delete the 474 MB on-device factory restore image"
            warn "  (/home/root/backup/factory/*.img + .tar.gz, dated Jan 2022)"
            warn "  This removes the device's SELF-restore capability. Recovery would"
            warn "  then require pulling the SD card and dd-ing your host-side backup."
            warn "  PRECONDITION: verify roomwizard.img on this host is intact FIRST."
            read -p "  Delete factory images too? (yes/no): " confirm_factory
            [[ "$confirm_factory" == "yes" ]] && DEL_FACTORY=1
        fi
    fi

    if [[ "$confirm2" == "yes" ]]; then
        ssh "$DEVICE" "DRY='$DRY_RUN' DEL_FACTORY='$DEL_FACTORY' bash -s" <<'REMOTE'
before_root=$(df -k /            | tail -1 | awk '{print $3}')
before_data=$(df -k /home/root/data   2>/dev/null | tail -1 | awk '{print $3}')
before_bkp=$(df -k /home/root/backup  2>/dev/null | tail -1 | awk '{print $3}')

# del <paths...>  - remove, or just report under DRY
del() {
    for p in "$@"; do
        for m in $p; do
            [ -e "$m" ] || continue
            sz=$(du -sh "$m" 2>/dev/null | awk '{print $1}')
            if [ -n "$DRY" ]; then
                echo "  would delete  $sz  $m"
            else
                echo "  deleting      $sz  $m"
                rm -rf "$m"
            fi
        done
    done
}

echo ""
echo "-- Browser / WebKit stack (~48 MB) --"
del /usr/lib/libwebkit2gtk-4.0.so* /usr/lib/libjavascriptcoregtk-4.0.so*
del /usr/libexec/webkit2gtk-4.0 /usr/lib/webkit2gtk-4.0
del /usr/bin/WebKitWebDriver /usr/bin/browser /usr/bin/webmonitor

echo "-- ICU (only referenced by webkit/JSC/harfbuzz-icu) (~31 MB) --"
del /usr/lib/libicu*.so* /usr/lib/libharfbuzz-icu.so*

echo "-- GTK3 + icon/mime/theme data (~17 MB) --"
del /usr/lib/libgtk-3.so* /usr/lib/libgdk-3.so* /usr/lib/libgdk_pixbuf*
del /usr/lib/gdk-pixbuf-2.0 /usr/lib/girepository-1.0
del /usr/share/icons /usr/share/mime /usr/share/themes

echo "-- X11 client libs + server (~6 MB) --"
del /usr/lib/libX*.so* /usr/lib/libxcb*.so* /usr/lib/libepoxy.so*
del /usr/lib/libGL.so* /usr/lib/libEGL.so* /usr/lib/libgbm.so*
del /usr/lib/xorg /usr/lib/X11 /usr/lib/dri /usr/share/drirc.d
del /usr/bin/Xorg /usr/bin/xkbcomp
del /usr/libexec/libinput /usr/share/libinput

echo "-- GStreamer (~4.5 MB) --"
del /usr/lib/libgst*.so* /usr/lib/liborc-0.4.so*
del /usr/lib/gstreamer-1.0 /usr/libexec/gstreamer-1.0 /usr/share/gst-plugins-base

echo "-- net-snmp / aspell / lftp / orphaned python / tslib (~9 MB) --"
del /usr/lib/libnetsnmp*.so* /usr/sbin/snmpd /usr/bin/snmp* /usr/bin/net-snmp*
del /usr/lib/aspell-0.60 /usr/lib/enchant-2 /usr/share/enchant-2
del /usr/lib/libaspell.so* /usr/lib/libenchant*.so* /usr/bin/aspell*
del /usr/lib/lftp /usr/share/lftp /usr/lib/liblftp*.so* /usr/bin/lftp*
del /usr/lib/libpython3.8.so*      # orphaned: no python3 stdlib on this device
del /usr/lib/ts /usr/lib/libts.so*
del /usr/share/sounds              # alsa test wavs (NOT /usr/share/alsa - keep that)

echo "-- Unused daemons + Steelcase mail tooling (~3 MB) --"
del /usr/sbin/wpa_supplicant /usr/sbin/ntpd.ntp /usr/sbin/vsftpd /usr/sbin/avahi-daemon
del /etc/avahi /etc/wpa_supplicant* /etc/ntp.conf /etc/ntp.conf.template
del /usr/local/bin/mutt /usr/local/bin/muttbug /usr/local/bin/pgpewrap
del /usr/local/bin/pgpring /usr/local/bin/smime_keys /usr/local/bin/flea
del /usr/libexec/nullmailer /var/spool/nullmailer
del /etc/init.d/avahi-daemon /etc/init.d/wpa_supplicant /etc/init.d/networkmanager
del /etc/init.d/cursor.sh /etc/init.d/psplash /etc/init.d/cleaup_partition

echo "-- ZigBee tooling (only reachable if the radio is populated) --"
del /opt/sbin/RoomWizard-zbgatewayd /opt/sbin/wpantools_roomwizard

echo "-- Boot links the stale on-device disable script missed --"
# KEEP /etc/rc5.d/S40ctrlblk (boot_tracker management) and S50watchdog (HW watchdog)
del /etc/rc5.d/S21cursor.sh /etc/rc5.d/S75bootscrub /etc/rc5.d/S80cleaup_partition
del /etc/rc5.d/S99rmnologin.sh /etc/rcS.d/S01psplash /etc/rcS.d/S58wpa_supplicant
del /etc/rcS.d/S60networkmanager /etc/rcS.d/S45mountnfs.sh

echo "-- Data partition (~91 MB) --"
if [ -f /home/root/data/cron/log ]; then
    sz=$(du -sh /home/root/data/cron/log | awk '{print $1}')
    if [ -n "$DRY" ]; then
        echo "  would truncate $sz  /home/root/data/cron/log"
    else
        echo "  truncating     $sz  /home/root/data/cron/log"
        : > /home/root/data/cron/log
    fi
fi
del /home/root/data/test.hex        # 10 MB factory burn-in pattern
del /home/root/data/splash /home/root/data/frontpanel /home/root/data/misc
del /home/root/data/roombooker /home/root/data/selftest /home/root/data/uploaded
del /home/root/data/wpa /home/root/data/wpa_ca
del /home/root/data/pushconfig.tar.gz /home/root/data/rwdb.tgz
# KEEP /home/root/data/*.hig - our high scores

echo "-- Log churn on the backup partition --"
del /home/root/backup/logs/rotate_logs /home/root/backup/logs/restore
# KEEP /home/root/backup/serialno - device identity

echo "-- Cron: both remaining jobs are broken/leaky --"
# cleanupfiles.sh errors out ("[: 812M: integer expression expected") and both
# jobs leak 12 files/day into rotate_logs with nothing pruning them. Our own
# respawn.sh self-rotates, so cron has no remaining purpose.
if [ -n "$DRY" ]; then
    echo "  would remove crontab, /etc/rc5.d/S20cron, /opt/sbin/cleanup"
else
    crontab -r 2>/dev/null || true
    rm -f /etc/rc5.d/S20cron
    rm -rf /opt/sbin/cleanup
    rm -rf /home/root/data/cron
    echo "  cron removed"
fi

echo "-- inittab: drop the pointless tty4 getty (~1.4 MB RSS) --"
if grep -q '^4:12345:respawn:/sbin/getty 38400 tty4' /etc/inittab 2>/dev/null; then
    if [ -n "$DRY" ]; then
        echo "  would remove tty4 getty line from /etc/inittab"
    else
        sed -i '/^4:12345:respawn:\/sbin\/getty 38400 tty4/d' /etc/inittab
        echo "  tty4 getty removed (takes effect next boot)"
    fi
fi

if [ "$DEL_FACTORY" = "1" ]; then
    echo "-- Factory restore image (474 MB) --"
    del /home/root/backup/factory/sd_rootfs_part.img*
    del /home/root/backup/factory/sd_boot_archive.tar.gz*
    del /home/root/backup/factory/sd_data_part.img*
    del /home/root/backup/factory/sd_log_part.img*
    # KEEP uImage-system-original (5 MB) - cheap local fallback kernel
fi

if [ -z "$DRY" ]; then
    echo ""
    echo "Freed:"
    after=$(df -k / | tail -1 | awk '{print $3}')
    echo "  rootfs:  $(( (before_root - after) / 1024 )) MB"
    if [ -n "$before_data" ]; then
        a=$(df -k /home/root/data | tail -1 | awk '{print $3}')
        echo "  data:    $(( (before_data - a) / 1024 )) MB"
    fi
    if [ -n "$before_bkp" ]; then
        a=$(df -k /home/root/backup | tail -1 | awk '{print $3}')
        echo "  backup:  $(( (before_bkp - a) / 1024 )) MB"
    fi
    echo ""
    df -h / /home/root/data /home/root/backup 2>/dev/null
fi
REMOTE
        if [[ "$DRY_RUN" == "--dry-run" ]]; then
            ok "Dry run complete — nothing was deleted"
        else
            ok "Deep clean complete"
        fi
    else
        info "Deep clean cancelled"
    fi
}


# ── SSH check ───────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " RoomWizard System Setup"
echo "════════════════════════════════════════"

info "Testing SSH connection to $DEVICE_IP..."
ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
    || err "Cannot reach $DEVICE — check IP and SSH key"
ok "SSH OK"

# ── dry-run: report the deep clean and exit WITHOUT running setup or rebooting ──
if [[ "$DRY_RUN" == "--dry-run" ]]; then
    [[ "$DEEP_CLEAN" == "1" ]] || err "--dry-run is only supported together with --deep-clean"
    run_deep_clean
    echo ""
    info "Dry run only — no setup performed, device not rebooted."
    exit 0
fi

# ── status-only mode ────────────────────────────────────────────────────────
if [[ "$FLAG" == "--status" ]]; then
    echo ""
    info "Device status:"
    ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Disk:    $(df -h / | tail -1 | awk '{print $3 " used, " $4 " free (" $5 " used)"}')"
echo "Memory:  $(free -h | grep Mem | awk '{print $3 " used, " $7 " available"}')"
wdog_cron=$(crontab -l 2>/dev/null | grep -c '^[^#].*/watchdog\.sh')
echo "SW watchdog cron: $([ $wdog_cron -eq 0 ] && echo 'disabled ✓' || echo 'ENABLED ✗')"
echo "Bypass file:      $([ -f /var/watchdog_test ] && echo 'present ✓' || echo 'MISSING ✗')"
echo "HW watchdog:      $([ -c /dev/watchdog ] && echo 'active (fed by /usr/sbin/watchdog)' || echo 'n/a')"
echo "Bloatware procs:  $(ps aux | grep -E 'java|Xorg|browser' | grep -v grep | wc -l)"
echo "Default app:      $(cat /opt/roomwizard/default-app 2>/dev/null || echo '(not set)')"
echo ""
echo "Active cron jobs:"
crontab -l 2>/dev/null | grep -v '^#' | grep -v '^$' | sed 's/^/  /'
echo ""
echo "Init services in rc5.d:"
ls -1 /etc/rc5.d/S* 2>/dev/null | sed 's|.*/||; s/^/  /'
REMOTE
    echo ""
    info "Deployed script versions:"
    report_script_versions
    echo ""
    exit 0
fi

# ── 1. Deploy disable-steelcase.sh ─────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 1. Disable Steelcase Bloatware"
echo "════════════════════════════════════════"

info "Deploying disable-steelcase.sh → $REMOTE_DIR/"
ssh "$DEVICE" "mkdir -p $REMOTE_DIR"
scp "$SCRIPT_DIR/disable-steelcase.sh" "$DEVICE:$REMOTE_DIR/disable-steelcase.sh"
ssh "$DEVICE" "chmod +x $REMOTE_DIR/disable-steelcase.sh"

info "Running disable-steelcase.sh..."
ssh "$DEVICE" "$REMOTE_DIR/disable-steelcase.sh"
ok "Steelcase bloatware disabled"

# ── 2. Install generic init script ─────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 2. Install App Launcher Service"
echo "════════════════════════════════════════"

info "Deploying roomwizard-app-init.sh → $INIT_SCRIPT"
scp "$SCRIPT_DIR/roomwizard-app-init.sh" "$DEVICE:$INIT_SCRIPT"

info "Registering service..."
ssh "$DEVICE" bash <<'REMOTE'
chmod +x /etc/init.d/roomwizard-app

# Clean up old roomwizard-games service if it exists
rm -f /etc/rc5.d/S*roomwizard-games 2>/dev/null || true
rm -f /etc/rc2.d/S*roomwizard-games 2>/dev/null || true
rm -f /etc/rc3.d/S*roomwizard-games 2>/dev/null || true
rm -f /etc/rc4.d/S*roomwizard-games 2>/dev/null || true
update-rc.d -f roomwizard-games remove 2>/dev/null || true

# Remove old roomwizard-app symlinks (might have wrong priority)
rm -f /etc/rc5.d/S*roomwizard-app 2>/dev/null || true
rm -f /etc/rc2.d/S*roomwizard-app 2>/dev/null || true
rm -f /etc/rc3.d/S*roomwizard-app 2>/dev/null || true
rm -f /etc/rc4.d/S*roomwizard-app 2>/dev/null || true
update-rc.d -f roomwizard-app remove 2>/dev/null || true

# Install with high priority (99) — starts AFTER all other services.
# This ensures browser/x11/webserver have started before we stop them.
ln -sf /etc/init.d/roomwizard-app /etc/rc5.d/S99roomwizard-app
ln -sf /etc/init.d/roomwizard-app /etc/rc2.d/S99roomwizard-app
ln -sf /etc/init.d/roomwizard-app /etc/rc3.d/S99roomwizard-app
ln -sf /etc/init.d/roomwizard-app /etc/rc4.d/S99roomwizard-app
REMOTE
ok "Service installed (S99roomwizard-app)"

# ── 3. Deploy audio-enable boot script ─────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 3. Deploy Boot Scripts"
echo "════════════════════════════════════════"

info "Deploying audio-enable boot script..."
ssh "$DEVICE" bash <<'AUDIO_REMOTE'
cat > /etc/init.d/audio-enable << 'EOF'
#!/bin/sh
# Enable RoomWizard speaker amplifier (GPIO12) and configure TWL4030 HiFi path
echo out > /sys/class/gpio/gpio12/direction
echo 1   > /sys/class/gpio/gpio12/value
amixer -c 0 cset name="HandsfreeL Mux" AudioL1  > /dev/null 2>&1
amixer -c 0 cset name="HandsfreeR Mux" AudioR1  > /dev/null 2>&1
amixer -c 0 cset name="HandsfreeL Switch" on    > /dev/null 2>&1
amixer -c 0 cset name="HandsfreeR Switch" on    > /dev/null 2>&1
EOF
chmod +x /etc/init.d/audio-enable
ln -sf /etc/init.d/audio-enable /etc/rc5.d/S29audio-enable
AUDIO_REMOTE
ok "Audio boot script deployed"

info "Deploying time-sync boot script..."
ssh "$DEVICE" bash <<'TIMESYNC_REMOTE'
cat > /etc/init.d/time-sync << 'EOF'
#!/bin/sh
# Simple time synchronization for RoomWizard
# Syncs time with time server if network is available.
# Try multiple time servers in order using rdate (RFC 868 Time Protocol).

# Wait a bit for network to be fully up
sleep 2

for server in time.nist.gov time-a-g.nist.gov time-b-g.nist.gov time-c-g.nist.gov; do
    if rdate -s "$server" >/dev/null 2>&1; then
        hwclock -w
        logger "time-sync: Successfully synced time with $server"
        exit 0
    fi
done

logger "time-sync: Failed to sync time with any time server"
EOF
chmod +x /etc/init.d/time-sync
ln -sf /etc/init.d/time-sync /etc/rc5.d/S28time-sync
TIMESYNC_REMOTE
ok "Time sync boot script deployed"

# ── 4. SSH Hardening ──────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 4. SSH Hardening"
echo "════════════════════════════════════════"

# NOTE: SSH keys must already be deployed (Phase 1 / commission-roomwizard.sh).
# We do NOT disable PasswordAuthentication here — do that manually AFTER
# confirming key-based login works:
#   sed -i 's/^#PasswordAuthentication yes/PasswordAuthentication no/' /etc/ssh/sshd_config
#
# PermitRootLogin stays "yes" because:
#   - There is no other user account on the device (only root in /etc/passwd)
#   - All deployment (scp, ssh) requires root@<ip>
#   - The device has no adduser/useradd to create non-root accounts
#   - Security is achieved via key-based auth + PermitEmptyPasswords=no
info "Hardening SSH configuration..."
ssh "$DEVICE" bash <<'SSH_HARDEN'
    # Backup original if not already backed up
    [ ! -f /etc/ssh/sshd_config.orig ] && cp /etc/ssh/sshd_config /etc/ssh/sshd_config.orig

    # CRITICAL FIX: Disable empty password login (factory default allows it!)
    sed -i 's/^PermitEmptyPasswords yes/PermitEmptyPasswords no/' /etc/ssh/sshd_config

    # PermitRootLogin stays "yes" — root is the only account on the device.
    # Do NOT change to "prohibit-password" as it would lock us out if keys break.

    # Add brute-force / session limits if not present
    grep -q '^MaxAuthTries' /etc/ssh/sshd_config || echo 'MaxAuthTries 3' >> /etc/ssh/sshd_config
    grep -q '^LoginGraceTime' /etc/ssh/sshd_config || echo 'LoginGraceTime 30' >> /etc/ssh/sshd_config
    grep -q '^MaxSessions' /etc/ssh/sshd_config || echo 'MaxSessions 5' >> /etc/ssh/sshd_config

    # NOTE: Don't restart sshd here — it would break subsequent SSH commands in this script.
    # Changes take effect on next reboot (script requests reboot at the end).
SSH_HARDEN
ok "SSH hardened (PermitEmptyPasswords=no, MaxAuthTries=3) — takes effect on reboot"

# ── 5. Kernel/sysctl hardening ───────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 5. Kernel Security Settings"
echo "════════════════════════════════════════"

# NOTE on firewall: The RoomWizard firmware has no iptables binary, no
# ip_tables.ko kernel module in /lib/modules/4.14.52, no busybox iptables
# applet, no TCP wrappers (libwrap), and no package manager to install any
# of these. The kernel has CONFIG_NETFILTER=y but the ip_tables module was
# never compiled. Network security relies on:
#   - Disabling all unnecessary services (FTP, SNMP, HTTP, Java)
#   - SSH hardening (empty passwords blocked, brute-force limits)
#   - sysctl network hardening (below)
#   - Home network — device is not internet-facing
info "Applying kernel security settings..."
ssh "$DEVICE" bash <<'SYSCTL'
    cat > /etc/sysctl.d/99-security.conf << 'EOF'
# RoomWizard security hardening
kernel.randomize_va_space = 2
kernel.dmesg_restrict = 1
kernel.core_pattern = |/bin/false
kernel.sysrq = 0
net.ipv4.ip_forward = 0
net.ipv4.conf.all.accept_redirects = 0
net.ipv4.conf.default.accept_redirects = 0
net.ipv4.conf.all.accept_source_route = 0
net.ipv4.conf.default.accept_source_route = 0
net.ipv4.conf.all.rp_filter = 1
net.ipv4.conf.default.rp_filter = 1
net.ipv4.conf.all.log_martians = 1
net.ipv4.icmp_echo_ignore_broadcasts = 1
net.ipv4.tcp_syncookies = 1
EOF
    # Apply now
    sysctl -p /etc/sysctl.d/99-security.conf 2>/dev/null || {
        # Fallback: apply individually if sysctl.d not supported
        sysctl -w kernel.randomize_va_space=2 2>/dev/null
        sysctl -w kernel.dmesg_restrict=1 2>/dev/null
        sysctl -w kernel.sysrq=0 2>/dev/null
    }
SYSCTL
ok "Kernel security settings applied"

# ── 6. Analyze + optionally remove bloatware files ─────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 6. Bloatware Files"
echo "════════════════════════════════════════"

info "Analyzing filesystem..."
ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Bloatware analysis:"
echo "  Jetty:    $(du -sh /opt/jetty-9-4-11 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  OpenJRE:  $(du -sh /opt/openjre-8 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  HSQLDB:   $(du -sh /opt/hsqldb 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  CJK Font: $(du -sh /usr/share/cjkfont 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  X11:      $(du -sh /usr/share/X11 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  SNMP:     $(du -sh /usr/share/snmp 2>/dev/null | awk '{print $1}' || echo 'removed')"
REMOTE

if [[ "$FLAG" == "--remove" ]]; then
    echo ""
    warn "File removal mode — this will delete bloatware files (PERMANENT)"
    read -p "Continue? (yes/no): " confirm
    
    if [[ "$confirm" == "yes" ]]; then
        info "Removing bloatware files..."
        ssh "$DEVICE" bash <<'REMOTE'
cat > /home/root/bloatware-removed.txt <<EOF
Bloatware removed on $(date)
- /opt/jetty-9-4-11 (43 MB)
- /opt/openjre-8 (93 MB)
- /opt/hsqldb (3.5 MB)
- /usr/share/cjkfont (31 MB)
- /usr/share/X11 (5.2 MB)
- /usr/share/snmp (2.5 MB)
Total freed: ~178 MB
EOF
rm -rf /opt/jetty-9-4-11 /opt/jetty /opt/openjre-8 /opt/java /opt/hsqldb
rm -rf /usr/share/cjkfont /usr/share/X11 /usr/share/snmp

# Additional Steelcase artifacts and data
rm -rf /opt/pv02 2>/dev/null
# NOTE: /opt/sound (113 KB) is deliberately KEPT - it holds three usable UI WAVs
# (asl_click.wav, asl_error.wav, asl_success.wav) that are directly useful as
# launcher/game feedback sounds via common/audio.c. Cheap to keep.
rm -f /home/root/sqltool.rc 2>/dev/null
rm -rf /home/root/data/websign 2>/dev/null

# Clean shell profile references to deleted Steelcase configs
# (wsplatform.conf sourced in /etc/profile causes login errors)
sed -i '/wsplatform\.conf/d' /etc/profile 2>/dev/null
rm -rf /home/root/data/rwdb 2>/dev/null
rm -rf /home/root/data/conctest 2>/dev/null
# BUGFIX 2026-07-29: this used to be `rm -rf /home/root/data/cron`, which is
# DESTRUCTIVE - /var/cron/tabs/root is a SYMLINK into that directory, so deleting
# it destroys the root crontab and cron's spool root. Truncate the 79 MB log
# instead (it has been accumulating since 2017). Use --deep-clean to remove cron
# entirely, deliberately.
[ -f /home/root/data/cron/log ] && : > /home/root/data/cron/log
rm -rf /home/root/backup/websigns 2>/dev/null
rm -f /var/crontab.steelcase.bak 2>/dev/null

# Steelcase service configs
rm -f /etc/vsftpd.conf 2>/dev/null
rm -rf /etc/nullmailer 2>/dev/null
rm -rf /etc/snmp 2>/dev/null
rm -rf /var/lib/net-snmp 2>/dev/null
rm -rf /var/lib/nullmailer 2>/dev/null
rm -rf /var/lib/ntp 2>/dev/null

# Stale logs from Steelcase services
rm -f /var/log/browser.err 2>/dev/null
rm -f /var/log/jettystart 2>/dev/null
rm -rf /var/log/jetty_logs 2>/dev/null
rm -f /var/log/hsqldbstart 2>/dev/null
rm -f /var/log/snmp_daemon.log 2>/dev/null
rm -f /var/log/networkmngr.err 2>/dev/null
rm -f /home/root/log/Xorg.0.log 2>/dev/null
rm -f /home/root/log/get_time_from_server.err 2>/dev/null
rm -f /home/root/log/browser.err 2>/dev/null
rm -f /home/root/log/jettystart 2>/dev/null
rm -f /home/root/log/concurrent.log 2>/dev/null
rm -f /home/root/log/snmp_daemon.log 2>/dev/null
rm -f /home/root/log/networkmngr.err 2>/dev/null
rm -rf /home/root/log/jetty_logs 2>/dev/null

# NOTE: RoomWizard-zbgatewayd and wpantools_roomwizard are the 802.15.4 / ZigBee
# radio tooling. They are KEPT from 2026-07-29 onwards as the protocol reference
# for device-to-device wireless. The J5/J6 XBee socket is real and empty on every
# unit we have (see SYSTEM_ANALYSIS.md section 3.12), so this tooling is what a
# fitted module would be driven with. ~1.3 MB total. Copies also exist in the
# host-side partitions/ dump, so removing them is recoverable - but keeping them
# costs almost nothing. --deep-clean removes them.

# Dangerous init scripts - remove entirely (already disabled)
for svc in vsftpd snmpd nullmailer ntpd webserver jetty browser startautoupgrade webmonitor x11 hsqldb mta.sh; do
    rm -f "/etc/init.d/$svc" 2>/dev/null
done
REMOTE
        ok "Bloatware files removed (extended cleanup)"
        warn "Backup list saved to /home/root/bloatware-removed.txt"
    else
        info "File removal cancelled"
    fi
else
    info "Bloatware files left in place (use --remove to delete)"
fi

# ── deep clean (function defined above) ─────────────────────────────────────
if [[ "$DEEP_CLEAN" == "1" ]]; then
    run_deep_clean
fi

# ── Status summary ──────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " Status Summary"
echo "════════════════════════════════════════"

ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Disk:    $(df -h / | tail -1 | awk '{print $3 " used, " $4 " free (" $5 " used)"}')"
echo "Memory:  $(free -h | grep Mem | awk '{print $3 " used, " $7 " available"}')"
wdog_cron=$(crontab -l 2>/dev/null | grep -c '^[^#].*/watchdog\.sh')
echo "SW watchdog cron: $([ $wdog_cron -eq 0 ] && echo 'disabled ✓' || echo 'ENABLED ✗')"
echo "Bypass file:      $([ -f /var/watchdog_test ] && echo 'present ✓' || echo 'MISSING ✗')"
echo "HW watchdog:      $([ -c /dev/watchdog ] && echo 'active (fed by /usr/sbin/watchdog)' || echo 'n/a')"
echo "Default app:      $(cat /opt/roomwizard/default-app 2>/dev/null || echo '(not set)')"
echo ""
echo "Active cron jobs:"
crontab -l 2>/dev/null | grep -v '^#' | grep -v '^$' | sed 's/^/  /'
REMOTE

# Confirm the two scripts this run just pushed are byte-identical on the device.
# scp reporting success is not the same as the right bytes landing, and these are
# the files whose silent staleness cost a session (B18, B19).
echo ""
info "Deployed script versions:"
report_script_versions

# ── Reboot ──────────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " Rebooting"
echo "════════════════════════════════════════"

info "Rebooting device..."
ssh "$DEVICE" reboot || true
ok "Device is rebooting"

echo ""
echo "  System setup complete! Wait ~30 s then: ssh root@$DEVICE_IP"
echo ""
echo "  Next step — build and deploy all components:"
echo "    ./deploy-all.sh $DEVICE_IP"
echo ""
echo "  Or deploy individually:"
echo "    cd native_apps      && ./build-and-deploy.sh $DEVICE_IP set-default"
echo "    cd vnc_client        && ./build-and-deploy.sh $DEVICE_IP"
echo "    cd scummvm-roomwizard && ./build-and-deploy.sh $DEVICE_IP"
echo ""
