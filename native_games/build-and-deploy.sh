#!/bin/bash
# Build all native games and optionally deploy to a RoomWizard device.
#
# Usage:
#   ./build-and-deploy.sh                   # build only
#   ./build-and-deploy.sh <ip>              # build + deploy binaries
#   ./build-and-deploy.sh <ip> permanent    # build + deploy + install boot service + reboot

set -e

DEVICE_IP="${1:-}"
MODE="${2:-}"
DEVICE="root@${DEVICE_IP}"
GAMES_DIR="/opt/games"
INIT_SCRIPT="/etc/init.d/roomwizard-games"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }

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

step "1/14" "framebuffer";  $CC -O2 -static -c common/framebuffer.c    -o build/framebuffer.o
step "2/14" "touch_input";  $CC -O2 -static -c common/touch_input.c    -o build/touch_input.o
step "3/14" "hardware";     $CC -O2 -static -c common/hardware.c        -o build/hardware.o
step "4/14" "common";       $CC -O2 -static -c common/common.c          -o build/common.o
step "5/14" "highscore";    $CC -O2 -static -c common/highscore.c       -o build/highscore.o
step "6/14" "ui_layout";    $CC -O2 -static -c common/ui_layout.c       -o build/ui_layout.o
step "7/14" "audio";        $CC -O2 -static -c common/audio.c           -o build/audio.o

COMMON_OBJ="build/framebuffer.o build/touch_input.o build/hardware.o build/common.o build/highscore.o build/audio.o"

step "8/14"  "snake";        $CC -O2 -static snake/snake.c             $COMMON_OBJ -o build/snake         -lm
step "9/14"  "tetris";       $CC -O2 -static tetris/tetris.c           $COMMON_OBJ -o build/tetris        -lm
step "10/14" "pong";         $CC -O2 -static pong/pong.c               $COMMON_OBJ -o build/pong          -lm

step "11/14" "game_selector"
$CC -O2 -static -I. game_selector/game_selector.c $COMMON_OBJ build/ui_layout.o -o build/game_selector -lm

step "12/14" "hardware_test"
$CC -O2 -static -I. hardware_test/hardware_test_gui.c $COMMON_OBJ build/ui_layout.o -o build/hardware_test -lm

step "13/14" "unified_calibrate"
$CC -O2 -static -I. tests/unified_calibrate.c $COMMON_OBJ -o build/unified_calibrate -lm

step "14/15" "watchdog_feeder"
$CC -O2 -static watchdog_feeder/watchdog_feeder.c -o build/watchdog_feeder

step "15/15" "audio_touch_test"
$CC -O2 -static -I. \
  tests/audio_touch_test.c \
  common/audio.c common/touch_input.c common/framebuffer.c \
  common/hardware.c common/common.c \
  -o build/audio_touch_test -lm

echo ""
echo "Build sizes:"
ls -lh build/snake build/tetris build/pong build/game_selector build/hardware_test build/unified_calibrate build/watchdog_feeder build/audio_touch_test \
    | awk '{printf "  %-24s %s\n", $9, $5}'
ok "Build complete"
echo ""

# ── 3. deploy? ───────────────────────────────────────────────────────────────
if [[ -z "$DEVICE_IP" ]]; then
    echo "No IP supplied — build only. To deploy:"
    echo "  ./build-and-deploy.sh <ip>"
    echo "  ./build-and-deploy.sh <ip> permanent"
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

# Upload binaries
info "Uploading binaries → $GAMES_DIR/"
scp build/snake build/tetris build/pong \
    build/game_selector build/hardware_test \
    build/unified_calibrate build/watchdog_feeder \
    build/audio_touch_test \
    "$DEVICE:$GAMES_DIR/"
ok "Binaries uploaded"

# Deploy audio-enable boot script (installs once; harmless to repeat)
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
echo "AUDIO_ENABLE_OK"
AUDIO_REMOTE
ok "Audio boot script deployed"

# Set execute permissions + create marker files
info "Setting permissions and markers..."
ssh "$DEVICE" bash <<'REMOTE'
chmod +x /opt/games/snake /opt/games/tetris /opt/games/pong \
         /opt/games/game_selector /opt/games/hardware_test \
         /opt/games/unified_calibrate

# .noargs marker for scummvm (if binary present)
[ -f /opt/games/scummvm ] && touch /opt/games/scummvm.noargs && chmod 644 /opt/games/scummvm.noargs

# .hidden markers for dev tools
for name in watchdog_feeder touch_test touch_debug touch_inject \
            touch_calibrate pressure_test \
            ; do
    touch  /opt/games/$name.hidden
    chmod 644 /opt/games/$name.hidden
done
echo "MARKERS_OK"
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

    info "Registering service and rebooting..."
    ssh "$DEVICE" bash <<'REMOTE'
chmod +x /etc/init.d/roomwizard-games
update-rc.d browser remove  2>/dev/null || true
update-rc.d x11    remove  2>/dev/null || true
update-rc.d roomwizard-games defaults
echo "SERVICE_REGISTERED"
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
fi

echo ""
