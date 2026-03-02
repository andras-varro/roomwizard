#!/bin/bash
# Build all native games and optionally deploy to a RoomWizard device.
#
# Usage:
#   ./build-and-deploy.sh                      # build only
#   ./build-and-deploy.sh <ip>                 # build + deploy binaries
#   ./build-and-deploy.sh <ip> set-default     # build + deploy + set as default app
#
# System setup (bloatware cleanup, init service, audio, time-sync) is handled
# separately by setup-device.sh.  Run that once before deploying for the first time.

set -e

DEVICE_IP="${1:-}"
MODE="${2:-}"
DEVICE="root@${DEVICE_IP}"
GAMES_DIR="/opt/games"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
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
    echo "  ./build-and-deploy.sh <ip> set-default"
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

# Verify system setup has been done
if ! ssh "$DEVICE" "[ -f /opt/roomwizard/disable-steelcase.sh ]" 2>/dev/null; then
    warn "System setup not detected on device."
    warn "Run setup-device.sh first:  ../setup-device.sh $DEVICE_IP"
    echo ""
    read -p "Continue deploying anyway? (y/n): " confirm
    [[ "$confirm" != "y" ]] && exit 1
fi

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

# Set permissions + markers
info "Setting permissions and markers..."
ssh "$DEVICE" bash <<'REMOTE'
chmod +x /opt/games/snake /opt/games/tetris /opt/games/pong \
         /opt/games/game_selector /opt/games/hardware_test \
         /opt/games/unified_calibrate /opt/games/backlight

# .noargs marker for scummvm (if present)
[ -f /opt/games/scummvm ] && touch /opt/games/scummvm.noargs && chmod 644 /opt/games/scummvm.noargs

# .hidden markers for dev tools
for name in touch_test touch_debug touch_inject touch_calibrate pressure_test backlight; do
    touch  /opt/games/$name.hidden 2>/dev/null || true
    chmod 644 /opt/games/$name.hidden 2>/dev/null || true
done
REMOTE
ok "Permissions and markers set"
echo ""

# ── 4. set-default mode? ────────────────────────────────────────────────────
if [[ "$MODE" == "set-default" ]]; then
    info "Setting native games as default app..."
    ssh "$DEVICE" "mkdir -p /opt/roomwizard && echo '$GAMES_DIR/game_selector' > /opt/roomwizard/default-app"
    ok "Default app → $GAMES_DIR/game_selector"
    echo ""
    echo "  Reboot to start:  ssh $DEVICE reboot"
    echo "  Or start now:     ssh $DEVICE '/etc/init.d/roomwizard-app restart'"
else
    echo "  Binaries deployed. To set as default boot app:"
    echo "    ./build-and-deploy.sh $DEVICE_IP set-default"
    echo ""
    echo "  To start game selector now (without reboot):"
    echo "    ssh $DEVICE '$GAMES_DIR/game_selector'"
fi

echo ""
