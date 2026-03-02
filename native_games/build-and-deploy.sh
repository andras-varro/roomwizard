#!/bin/bash
# Build all native games and optionally deploy to a RoomWizard device.
#
# Usage:
#   ./build-and-deploy.sh                          # build only
#   ./build-and-deploy.sh <ip>                     # build + deploy binaries
#   ./build-and-deploy.sh <ip> permanent           # build + deploy + install boot service + cleanup + reboot
#   ./build-and-deploy.sh <ip> permanent --remove  # build + deploy + install boot service + cleanup + remove bloatware + reboot
#   ./build-and-deploy.sh <ip> cleanup             # cleanup services only (no build/deploy)
#   ./build-and-deploy.sh <ip> cleanup --remove    # cleanup services + remove bloatware files

set -e

DEVICE_IP="${1:-}"
MODE="${2:-}"
REMOVE_FILES="${3:-}"
DEVICE="root@${DEVICE_IP}"
GAMES_DIR="/opt/games"
INIT_SCRIPT="/etc/init.d/roomwizard-games"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }

# ── cleanup function ────────────────────────────────────────────────────────
do_cleanup() {
    local DEVICE="$1"
    local REMOVE_FILES="$2"
    
    echo ""
    echo "════════════════════════════════════════"
    echo " System Cleanup"
    echo "════════════════════════════════════════"
    
    # Analyze filesystem
    if [[ "$REMOVE_FILES" == "--remove" ]]; then
        info "Analyzing filesystem..."
        ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Bloatware analysis:"
echo "  Jetty:    $(du -sh /opt/jetty-9-4-11 2>/dev/null | awk '{print $1}')"
echo "  OpenJRE:  $(du -sh /opt/openjre-8 2>/dev/null | awk '{print $1}')"
echo "  HSQLDB:   $(du -sh /opt/hsqldb 2>/dev/null | awk '{print $1}')"
echo "  CJK Font: $(du -sh /usr/share/cjkfont 2>/dev/null | awk '{print $1}')"
echo "  X11:      $(du -sh /usr/share/X11 2>/dev/null | awk '{print $1}')"
echo "  SNMP:     $(du -sh /usr/share/snmp 2>/dev/null | awk '{print $1}')"
echo "  Total: ~178 MB"
REMOTE
    fi
    
    # Disable watchdog test/repair
    info "Disabling watchdog test and repair..."
    ssh "$DEVICE" bash <<'REMOTE'
if [ -f /etc/watchdog.conf ]; then
    cp /etc/watchdog.conf /etc/watchdog.conf.backup 2>/dev/null || true
    sed -i 's/^test-binary/#test-binary/' /etc/watchdog.conf
    sed -i 's/^repair-binary/#repair-binary/' /etc/watchdog.conf
    /etc/init.d/watchdog restart 2>/dev/null || true
fi
REMOTE
    ok "Watchdog test/repair disabled"
    
    # Stop and disable services
    info "Stopping and disabling unnecessary services..."
    ssh "$DEVICE" bash <<'REMOTE'
for svc in webserver browser x11 jetty hsqldb snmpd vsftpd nullmailer ntpd startautoupgrade; do
    [ -f /etc/init.d/$svc ] && /etc/init.d/$svc stop 2>/dev/null || true
    update-rc.d $svc remove 2>/dev/null || true
done
REMOTE
    ok "Unnecessary services disabled"
    
    # Kill processes
    info "Killing bloatware processes..."
    ssh "$DEVICE" 'killall java Xorg browser epiphany webkit psplash nullmailer-send 2>/dev/null || true'
    ok "Bloatware processes terminated"
    
    # Remove files if requested
    if [[ "$REMOVE_FILES" == "--remove" ]]; then
        echo ""
        warn "File removal mode - this will delete bloatware files (PERMANENT)"
        read -p "Continue? (yes/no): " confirm
        
        if [[ "$confirm" == "yes" ]]; then
            info "Removing bloatware files..."
            ssh "$DEVICE" bash <<'REMOTE'
cat > /home/root/bloatware-removed.txt <<'EOF'
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
REMOTE
            ok "Bloatware files removed (~178 MB freed)"
            warn "Backup list saved to /home/root/bloatware-removed.txt"
        else
            info "File removal cancelled"
        fi
    fi
    
    # Show status
    info "System status..."
    ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Disk: $(df -h / | tail -1 | awk '{print $3 " used, " $4 " free (" $5 " used)"}')"
echo "Memory: $(free -h | grep Mem | awk '{print $3 " used, " $7 " available"}')"
echo "Watchdog: $(grep -q '^#test-binary' /etc/watchdog.conf 2>/dev/null && echo 'disabled ✓' || echo 'ENABLED')"
echo "Bloatware processes: $(ps aux | grep -E 'java|Xorg|browser' | grep -v grep | wc -l)"
REMOTE
    
    ok "Cleanup complete!"
    echo ""
}

# ── cleanup-only mode ───────────────────────────────────────────────────────
if [[ "$MODE" == "cleanup" ]]; then
    [[ -z "$DEVICE_IP" ]] && err "Usage: $0 <ip> cleanup [--remove]"
    
    info "Testing SSH connection..."
    ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
        || err "Cannot reach $DEVICE"
    ok "SSH OK"
    
    do_cleanup "$DEVICE" "$REMOVE_FILES"
    exit 0
fi

# ── 1. cross-compiler check ─────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " RoomWizard Build + Deploy"
echo "════════════════════════════════════════"

CC=arm-linux-gnueabihf-gcc
if ! command -v $CC &>/dev/null; then
    err "ARM cross-compiler not found. Install with:\n  sudo apt-get install gcc-arm-linux-gnueabihf"
fi
info "Compiler: $($CC --version | head -1)"
echo ""

# ── 2. build ─────────────────────────────────────────────────────────────────
mkdir -p build

step() { echo "[$1] $2..."; }

step "1/15" "framebuffer";  $CC -O2 -static -c common/framebuffer.c    -o build/framebuffer.o
step "2/15" "touch_input";  $CC -O2 -static -c common/touch_input.c    -o build/touch_input.o
step "3/15" "hardware";     $CC -O2 -static -c common/hardware.c        -o build/hardware.o
step "4/15" "common";       $CC -O2 -static -c common/common.c          -o build/common.o
step "5/15" "highscore";    $CC -O2 -static -c common/highscore.c       -o build/highscore.o
step "6/15" "ui_layout";    $CC -O2 -static -c common/ui_layout.c       -o build/ui_layout.o
step "7/15" "audio";        $CC -O2 -static -c common/audio.c           -o build/audio.o

COMMON_OBJ="build/framebuffer.o build/touch_input.o build/hardware.o build/common.o build/highscore.o build/audio.o"

step "8/15"  "snake";        $CC -O2 -static snake/snake.c             $COMMON_OBJ -o build/snake         -lm
step "9/15"  "tetris";       $CC -O2 -static tetris/tetris.c           $COMMON_OBJ -o build/tetris        -lm
step "10/15" "pong";         $CC -O2 -static pong/pong.c               $COMMON_OBJ -o build/pong          -lm

step "11/15" "game_selector"
$CC -O2 -static -I. game_selector/game_selector.c $COMMON_OBJ build/ui_layout.o -o build/game_selector -lm

step "12/15" "hardware_test"
$CC -O2 -static -I. hardware_test/hardware_test_gui.c $COMMON_OBJ build/ui_layout.o -o build/hardware_test -lm

step "13/15" "unified_calibrate"
$CC -O2 -static -I. tests/unified_calibrate.c $COMMON_OBJ -o build/unified_calibrate -lm

step "14/15" "audio_touch_test"
$CC -O2 -static -I. \
  tests/audio_touch_test.c \
  common/audio.c common/touch_input.c common/framebuffer.c \
  common/hardware.c common/common.c \
  -o build/audio_touch_test -lm

step "15/15" "backlight"
$CC -O2 -static -I. backlight/backlight.c build/hardware.o -o build/backlight

echo ""
echo "Build sizes:"
ls -lh build/snake build/tetris build/pong build/game_selector build/hardware_test build/unified_calibrate build/audio_touch_test build/backlight \
    | awk '{printf "  %-24s %s\n", $9, $5}'
ok "Build complete"
echo ""

# ── 3. deploy? ───────────────────────────────────────────────────────────────
if [[ -z "$DEVICE_IP" ]]; then
    echo "No IP supplied — build only. To deploy:"
    echo "  ./build-and-deploy.sh <ip>"
    echo "  ./build-and-deploy.sh <ip> permanent [--remove]"
    echo "  ./build-and-deploy.sh <ip> cleanup [--remove]"
    exit 0
fi

echo "════════════════════════════════════════"
echo " Deploying to $DEVICE_IP"
echo "════════════════════════════════════════"

# Check SSH reachable
info "Testing SSH connection..."
ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
    || err "Cannot reach $DEVICE — check IP and SSH key"
ok "SSH OK"

# Ensure target directory exists
info "Ensuring target directory exists..."
ssh "$DEVICE" "mkdir -p $GAMES_DIR"
ok "Target directory ready"

# Upload binaries
info "Uploading binaries → $GAMES_DIR/"
scp build/snake build/tetris build/pong \
    build/game_selector build/hardware_test \
    build/unified_calibrate \
    build/audio_touch_test \
    build/backlight \
    "$DEVICE:$GAMES_DIR/"
ok "Binaries uploaded"

# Deploy audio-enable boot script
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

# Deploy time-sync boot script
info "Deploying time-sync boot script..."
ssh "$DEVICE" bash <<'TIMESYNC_REMOTE'
cat > /etc/init.d/time-sync << 'EOF'
#!/bin/sh
# Simple time synchronization for RoomWizard
# Syncs time with time server if network is available
# Try multiple time servers in order using rdate (RFC 868 Time Protocol)

# Wait a bit for network to be fully up
sleep 2

# Try multiple time servers using rdate
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

# Set permissions + markers
info "Setting permissions and markers..."
ssh "$DEVICE" bash <<'REMOTE'
chmod +x /opt/games/snake /opt/games/tetris /opt/games/pong \
         /opt/games/game_selector /opt/games/hardware_test \
         /opt/games/unified_calibrate /opt/games/backlight

# .noargs marker for scummvm (if present)
[ -f /opt/games/scummvm ] && touch /opt/games/scummvm.noargs && chmod 644 /opt/games/scummvm.noargs

# .hidden markers for dev tools
for name in touch_test touch_debug touch_inject touch_calibrate pressure_test unified_calibrate backlight; do
    touch  /opt/games/$name.hidden 2>/dev/null || true
    chmod 644 /opt/games/$name.hidden 2>/dev/null || true
done
REMOTE
ok "Permissions and markers set"
echo ""

# ── 4. permanent mode? ───────────────────────────────────────────────────────
if [[ "$MODE" == "permanent" ]]; then
    echo "════════════════════════════════════════"
    echo " Installing permanent boot service"
    echo "════════════════════════════════════════"

    info "Uploading init script..."
    scp roomwizard-games-init.sh "$DEVICE:$INIT_SCRIPT"
    ok "Init script uploaded"

    # Run cleanup
    do_cleanup "$DEVICE" "$REMOVE_FILES"

    info "Registering service and rebooting..."
    ssh "$DEVICE" bash <<'REMOTE'
chmod +x /etc/init.d/roomwizard-games

# Remove conflicting service symlinks from all runlevels
echo "Removing conflicting service symlinks..."
for svc in browser webserver x11 jetty hsqldb ntpd; do
    rm -f /etc/rc5.d/S*${svc} 2>/dev/null || true
    rm -f /etc/rc5.d/K*${svc} 2>/dev/null || true
    rm -f /etc/rc2.d/S*${svc} 2>/dev/null || true
    rm -f /etc/rc3.d/S*${svc} 2>/dev/null || true
    rm -f /etc/rc4.d/S*${svc} 2>/dev/null || true
    update-rc.d -f $svc remove 2>/dev/null || true
done

# Remove old roomwizard-games symlinks if they exist (might have wrong priority)
rm -f /etc/rc5.d/S*roomwizard-games 2>/dev/null || true
rm -f /etc/rc2.d/S*roomwizard-games 2>/dev/null || true
rm -f /etc/rc3.d/S*roomwizard-games 2>/dev/null || true
rm -f /etc/rc4.d/S*roomwizard-games 2>/dev/null || true
update-rc.d -f roomwizard-games remove 2>/dev/null || true

# Install roomwizard-games with high priority (99) so it starts AFTER all other services
# This ensures browser/x11/webserver have started before we stop them
echo "Installing roomwizard-games service with priority 99..."
ln -sf /etc/init.d/roomwizard-games /etc/rc5.d/S99roomwizard-games
ln -sf /etc/init.d/roomwizard-games /etc/rc2.d/S99roomwizard-games
ln -sf /etc/init.d/roomwizard-games /etc/rc3.d/S99roomwizard-games
ln -sf /etc/init.d/roomwizard-games /etc/rc4.d/S99roomwizard-games

echo "Service installed. Rebooting..."
reboot
REMOTE
    ok "Service installed — device is rebooting"
    echo ""
    echo "  Wait ~30 s then: ssh root@$DEVICE_IP"
else
    echo "  Binaries deployed. To start the game selector now:"
    echo "    ssh $DEVICE '/opt/games/game_selector /dev/fb0 /dev/input/event0'"
    echo ""
    echo "  To install as boot service:"
    echo "    ./build-and-deploy.sh $DEVICE_IP permanent"
    echo ""
    echo "  To install as boot service + remove bloatware:"
    echo "    ./build-and-deploy.sh $DEVICE_IP permanent --remove"
    echo ""
    echo "  To cleanup services only:"
    echo "    ./build-and-deploy.sh $DEVICE_IP cleanup"
    echo ""
    echo "  To cleanup + remove bloatware files:"
    echo "    ./build-and-deploy.sh $DEVICE_IP cleanup --remove"
fi

echo ""
