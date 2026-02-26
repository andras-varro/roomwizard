#!/bin/bash
# Enhanced cleanup script to disable services AND remove bloatware files
# Frees ~160 MB disk space + 80 MB RAM
#
# Usage:
#   ./cleanup-bloatware-full.sh <device-ip>
#   ./cleanup-bloatware-full.sh <device-ip> --remove-files  # Also delete files

set -e

DEVICE_IP="${1:-}"
REMOVE_FILES="${2:-}"

if [[ -z "$DEVICE_IP" ]]; then
    echo "Usage: $0 <device-ip> [--remove-files]"
    echo "Example: $0 192.168.50.73"
    echo "         $0 192.168.50.73 --remove-files  # Also delete bloatware files"
    exit 1
fi

DEVICE="root@${DEVICE_IP}"

# Colour helpers
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }

echo ""
echo "════════════════════════════════════════"
echo " RoomWizard Bloatware Cleanup (Enhanced)"
echo "════════════════════════════════════════"
echo ""

# Check SSH reachable
info "Testing SSH connection..."
ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
    || err "Cannot reach $DEVICE — check IP and SSH key"
ok "SSH OK"

# Analyze filesystem
info "Analyzing filesystem..."
ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Current disk usage:"
df -h / | tail -1

echo ""
echo "Bloatware analysis:"
echo "  Jetty:    $(du -sh /opt/jetty-9-4-11 2>/dev/null | awk '{print $1}')"
echo "  OpenJRE:  $(du -sh /opt/openjre-8 2>/dev/null | awk '{print $1}')"
echo "  HSQLDB:   $(du -sh /opt/hsqldb 2>/dev/null | awk '{print $1}')"
echo "  CJK Font: $(du -sh /usr/share/cjkfont 2>/dev/null | awk '{print $1}')"
echo "  X11:      $(du -sh /usr/share/X11 2>/dev/null | awk '{print $1}')"
echo "  SNMP:     $(du -sh /usr/share/snmp 2>/dev/null | awk '{print $1}')"
echo ""
echo "Total bloatware: ~160 MB"
REMOTE

# Disable watchdog test/repair
info "Disabling watchdog test and repair binaries..."
ssh "$DEVICE" bash <<'REMOTE'
if [ -f /etc/watchdog.conf ]; then
    # Backup original
    cp /etc/watchdog.conf /etc/watchdog.conf.backup 2>/dev/null || true
    
    # Comment out test-binary and repair-binary
    sed -i 's/^test-binary/#test-binary/' /etc/watchdog.conf
    sed -i 's/^repair-binary/#repair-binary/' /etc/watchdog.conf
    
    # Restart watchdog daemon to apply changes
    /etc/init.d/watchdog restart 2>/dev/null || true
    
    echo "WATCHDOG_DISABLED"
else
    echo "WATCHDOG_NOT_FOUND"
fi
REMOTE
ok "Watchdog test/repair disabled"

# Stop and disable unnecessary services
info "Stopping and disabling unnecessary services..."
ssh "$DEVICE" bash <<'REMOTE'
# Services to disable
SERVICES="webserver browser x11 jetty hsqldb snmpd vsftpd"

for svc in $SERVICES; do
    if [ -f /etc/init.d/$svc ]; then
        /etc/init.d/$svc stop 2>/dev/null || true
        update-rc.d $svc remove 2>/dev/null || true
    fi
done

echo "SERVICES_DISABLED"
REMOTE
ok "Unnecessary services disabled"

# Kill remaining bloatware processes
info "Killing remaining bloatware processes..."
ssh "$DEVICE" bash <<'REMOTE'
killall java 2>/dev/null || true
killall Xorg 2>/dev/null || true
killall browser 2>/dev/null || true
killall epiphany 2>/dev/null || true
killall webkit 2>/dev/null || true

echo "PROCESSES_KILLED"
REMOTE
ok "Bloatware processes terminated"

# Remove files if requested
if [[ "$REMOVE_FILES" == "--remove-files" ]]; then
    echo ""
    warn "File removal mode enabled - this will delete bloatware files"
    warn "This action is IRREVERSIBLE without SD card backup"
    echo ""
    read -p "Continue? (yes/no): " confirm
    
    if [[ "$confirm" != "yes" ]]; then
        echo "File removal cancelled"
    else
        info "Removing bloatware files..."
        ssh "$DEVICE" bash <<'REMOTE'
# Create backup list
echo "Creating backup list..."
cat > /root/bloatware-removed.txt << 'EOF'
# Bloatware removed on $(date)
# To restore, you need the original SD card image

Removed directories:
- /opt/jetty-9-4-11 (43 MB) - Jetty web server
- /opt/openjre-8 (93 MB) - Java runtime
- /opt/hsqldb (3.5 MB) - HSQLDB database
- /usr/share/cjkfont (31 MB) - CJK fonts
- /usr/share/X11 (5.2 MB) - X11 data files
- /usr/share/snmp (2.5 MB) - SNMP MIBs

Total freed: ~178 MB

To restore: Reflash SD card from backup image
EOF

# Remove directories
echo "Removing Jetty..."
rm -rf /opt/jetty-9-4-11
rm -f /opt/jetty

echo "Removing OpenJRE..."
rm -rf /opt/openjre-8
rm -f /opt/java

echo "Removing HSQLDB..."
rm -rf /opt/hsqldb

echo "Removing CJK fonts..."
rm -rf /usr/share/cjkfont

echo "Removing X11 data..."
rm -rf /usr/share/X11

echo "Removing SNMP MIBs..."
rm -rf /usr/share/snmp

echo "FILES_REMOVED"
REMOTE
        ok "Bloatware files removed (~178 MB freed)"
        warn "Backup list saved to /root/bloatware-removed.txt on device"
    fi
fi

# Show current status
info "Checking current status..."
ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "═══════════════════════════════════════"
echo " System Status"
echo "═══════════════════════════════════════"

echo ""
echo "Disk usage:"
df -h / | tail -1

echo ""
echo "Memory usage:"
free -h | grep -E 'Mem:|Swap:'

echo ""
echo "Active services in rc5.d:"
ls /etc/rc5.d/S* 2>/dev/null | grep -v -E 'watchdog|roomwizard|audio|ssh|cron|dbus|ntpd' | wc -l | xargs echo "  Unnecessary services:"
ls /etc/rc5.d/S* 2>/dev/null | grep -E 'watchdog|roomwizard|audio|ssh|cron|dbus|ntpd' | wc -l | xargs echo "  Essential services:"

echo ""
echo "Running processes (bloatware):"
ps aux | grep -E 'java|Xorg|browser|jetty|hsqldb' | grep -v grep | wc -l | xargs echo "  Count:"

echo ""
echo "Watchdog configuration:"
if grep -q "^#test-binary" /etc/watchdog.conf 2>/dev/null; then
    echo "  test-binary: disabled ✓"
else
    echo "  test-binary: ENABLED (will cause reboots!)"
fi
if grep -q "^#repair-binary" /etc/watchdog.conf 2>/dev/null; then
    echo "  repair-binary: disabled ✓"
else
    echo "  repair-binary: ENABLED"
fi
echo "  watchdog-timeout: $(grep watchdog-timeout /etc/watchdog.conf | awk '{print $3}') seconds"

echo ""
echo "Bloatware status:"
[ -d /opt/jetty-9-4-11 ] && echo "  Jetty: present (43 MB)" || echo "  Jetty: removed ✓"
[ -d /opt/openjre-8 ] && echo "  OpenJRE: present (93 MB)" || echo "  OpenJRE: removed ✓"
[ -d /opt/hsqldb ] && echo "  HSQLDB: present (3.5 MB)" || echo "  HSQLDB: removed ✓"
[ -d /usr/share/cjkfont ] && echo "  CJK fonts: present (31 MB)" || echo "  CJK fonts: removed ✓"
[ -d /usr/share/X11 ] && echo "  X11 data: present (5.2 MB)" || echo "  X11 data: removed ✓"
[ -d /usr/share/snmp ] && echo "  SNMP: present (2.5 MB)" || echo "  SNMP: removed ✓"

echo ""
REMOTE

echo ""
ok "Cleanup complete!"
echo ""
echo "  The device is now running lean with:"
echo "    - Watchdog test/repair disabled (no more unwanted reboots)"
echo "    - Browser/X11/Jetty/HSQLDB services disabled"
echo "    - Memory freed: ~80 MB"

if [[ "$REMOVE_FILES" == "--remove-files" ]]; then
    echo "    - Disk space freed: ~178 MB"
    echo ""
    warn "  Files were removed - cannot be restored without SD backup"
    warn "  Backup list saved to /root/bloatware-removed.txt on device"
else
    echo "    - Disk space: unchanged (use --remove-files to delete bloatware)"
fi

echo ""
echo "  To revert service changes:"
echo "    ssh $DEVICE 'cp /etc/watchdog.conf.backup /etc/watchdog.conf'"
echo "    ssh $DEVICE 'update-rc.d browser defaults; update-rc.d x11 defaults'"
echo ""
