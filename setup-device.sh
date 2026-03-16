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
    echo "Usage: $0 <ip> [--remove|--status]"
    echo ""
    echo "  <ip>              Device IP address"
    echo "  --remove          Also remove bloatware files (~178 MB freed)"
    echo "  --status          Show device status only (no changes)"
    exit 1
}

[[ -z "$DEVICE_IP" ]] && usage

# ── SSH check ───────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " RoomWizard System Setup"
echo "════════════════════════════════════════"

info "Testing SSH connection to $DEVICE_IP..."
ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
    || err "Cannot reach $DEVICE — check IP and SSH key"
ok "SSH OK"

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

    # Restart SSH to apply
    /etc/init.d/sshd restart
SSH_HARDEN
ok "SSH hardened (PermitEmptyPasswords=no, MaxAuthTries=3, LoginGraceTime=30)"

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
rm -rf /opt/sound 2>/dev/null
rm -f /home/root/sqltool.rc 2>/dev/null
rm -rf /home/root/data/websign 2>/dev/null
rm -rf /home/root/data/rwdb 2>/dev/null
rm -rf /home/root/data/conctest 2>/dev/null
rm -rf /home/root/data/cron 2>/dev/null
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

# Remove Steelcase management binaries (but keep /opt/sbin/cleanup/ for log rotation)
rm -f /opt/sbin/RoomWizard-zbgatewayd 2>/dev/null
rm -f /opt/sbin/wpantools_roomwizard 2>/dev/null

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
