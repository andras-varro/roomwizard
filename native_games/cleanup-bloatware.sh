#!/bin/bash
# Cleanup script to disable unnecessary RoomWizard services and watchdog checks
# This prevents unwanted reboots when running in game mode
#
# Usage:
#   ./cleanup-bloatware.sh <device-ip>

set -e

DEVICE_IP="${1:-}"
if [[ -z "$DEVICE_IP" ]]; then
    echo "Usage: $0 <device-ip>"
    echo "Example: $0 192.168.50.73"
    exit 1
fi

DEVICE="root@${DEVICE_IP}"

# Colour helpers
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }

echo ""
echo "════════════════════════════════════════"
echo " RoomWizard Bloatware Cleanup"
echo "════════════════════════════════════════"
echo ""

# Check SSH reachable
info "Testing SSH connection..."
ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
    || err "Cannot reach $DEVICE — check IP and SSH key"
ok "SSH OK"

# Disable watchdog test/repair
info "Disabling watchdog test and repair binaries..."
ssh "$DEVICE" bash <<'REMOTE'
if [ -f /etc/watchdog.conf ]; then
    # Backup original
    cp /etc/watchdog.conf /etc/watchdog.conf.backup
    
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
        echo "  Stopping $svc..."
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

# Show current status
info "Checking current status..."
ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Active services in rc5.d:"
ls /etc/rc5.d/S* 2>/dev/null | grep -v -E 'watchdog|roomwizard|audio|ssh|cron|dbus' || echo "  (none)"

echo ""
echo "Running processes (excluding kernel/system):"
ps aux | grep -E 'java|Xorg|browser|jetty|hsqldb' | grep -v grep || echo "  (none)"

echo ""
echo "Watchdog configuration:"
grep -E '^(test-binary|repair-binary|watchdog-timeout)' /etc/watchdog.conf 2>/dev/null || echo "  test-binary: disabled"
echo "  watchdog-timeout: $(grep watchdog-timeout /etc/watchdog.conf | awk '{print $3}') seconds"

echo ""
echo "Memory usage:"
free -h | grep -E 'Mem:|Swap:'
REMOTE

echo ""
ok "Cleanup complete!"
echo ""
echo "  The device is now running lean with:"
echo "    - Watchdog test/repair disabled (no more unwanted reboots)"
echo "    - Browser/X11/Jetty/HSQLDB disabled"
echo "    - Only essential services running"
echo ""
echo "  To revert changes:"
echo "    ssh $DEVICE 'cp /etc/watchdog.conf.backup /etc/watchdog.conf'"
echo "    ssh $DEVICE 'update-rc.d browser defaults; update-rc.d x11 defaults'"
echo ""
