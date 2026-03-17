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

step " 1/27" "framebuffer";  $CC -O2 -static -c common/framebuffer.c    -o build/framebuffer.o
step " 2/27" "touch_input";  $CC -O2 -static -c common/touch_input.c    -o build/touch_input.o
step " 3/27" "hardware";     $CC -O2 -static -c common/hardware.c        -o build/hardware.o
step " 4/27" "common";       $CC -O2 -static -c common/common.c          -o build/common.o
step " 5/27" "highscore";    $CC -O2 -static -c common/highscore.c       -o build/highscore.o
step " 6/27" "ui_layout";    $CC -O2 -static -c common/ui_layout.c       -o build/ui_layout.o
step " 7/27" "audio";        $CC -O2 -static -c common/audio.c           -o build/audio.o
step " 8/27" "ppm";          $CC -O2 -static -c common/ppm.c             -o build/ppm.o
step " 9/27" "logger";       $CC -O2 -static -c common/logger.c          -o build/logger.o
step "10/27" "config";       $CC -O2 -static -c common/config.c          -o build/config.o
step "11/27" "gamepad";      $CC -O2 -static -c common/gamepad.c         -o build/gamepad.o

COMMON_OBJ="build/framebuffer.o build/touch_input.o build/hardware.o build/common.o build/highscore.o build/audio.o build/config.o"

step "12/27" "snake";        $CC -O2 -static snake/snake.c             $COMMON_OBJ -o build/snake         -lm
step "13/27" "tetris";       $CC -O2 -static tetris/tetris.c           $COMMON_OBJ -o build/tetris        -lm
step "14/27" "pong";         $CC -O2 -static pong/pong.c               $COMMON_OBJ -o build/pong          -lm

step "15/27" "brick_breaker"
$CC -O2 -static brick_breaker/brick_breaker.c $COMMON_OBJ -o build/brick_breaker -lm

step "16/27" "samegame"
$CC -O2 -static samegame/samegame.c $COMMON_OBJ -o build/samegame -lm

step "17/27" "frogger"
$CC -O2 -static frogger/frogger.c $COMMON_OBJ -o build/frogger -lm

step "18/27" "platformer"
$CC -O2 -static platformer/platformer.c $COMMON_OBJ build/gamepad.o -o build/platformer -lm

step "19/27" "game_selector"
$CC -O2 -static -I. game_selector/game_selector.c $COMMON_OBJ build/ui_layout.o -o build/game_selector -lm

step "20/27" "app_launcher"
$CC -O2 -static -I. app_launcher/app_launcher.c $COMMON_OBJ build/ppm.o build/logger.o -o build/app_launcher -lm

step "21/27" "hardware_test"
$CC -O2 -static -I. hardware_test/hardware_test_gui.c $COMMON_OBJ build/ui_layout.o -o build/hardware_test -lm

step "22/27" "hardware_config"
$CC -O2 -static -I. hardware_config/hardware_config.c $COMMON_OBJ build/ui_layout.o -o build/hardware_config -lm

step "23/27" "hardware_diag"
$CC -O2 -static -I. hardware_diag/hardware_diag.c $COMMON_OBJ -o build/hardware_diag -lm

step "24/27" "unified_calibrate"
$CC -O2 -static -I. tests/unified_calibrate.c $COMMON_OBJ -o build/unified_calibrate -lm

step "25/27" "audio_touch_test"
$CC -O2 -static -I. \
  tests/audio_touch_test.c \
  $COMMON_OBJ build/logger.o build/ppm.o \
  -o build/audio_touch_test -lm

step "26/27" "backlight"
$CC -O2 -static -I. backlight/backlight.c build/hardware.o build/config.o -o build/backlight

step "27/27" "device_tools"
$CC -O2 -static -I. device_tools/device_tools.c $COMMON_OBJ build/ui_layout.o -o build/device_tools -lm

# Collect icon files from source dirs → build/icons/
mkdir -p build/icons
ICON_COUNT=0
for ppm in *//*.ppm; do
    [ -f "$ppm" ] || continue
    cp "$ppm" build/icons/
    ICON_COUNT=$((ICON_COUNT + 1))
done
[ $ICON_COUNT -gt 0 ] && echo "  Collected $ICON_COUNT icon(s) → build/icons/"

echo ""
echo "Build sizes:"
ls -lh build/snake build/tetris build/pong build/brick_breaker build/samegame build/frogger build/platformer build/game_selector build/app_launcher build/hardware_test build/hardware_config build/hardware_diag build/unified_calibrate build/audio_touch_test build/backlight build/device_tools \
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

# Stop running launcher (avoids "Text file busy" on scp)
info "Stopping running launcher (if any)..."
ssh "$DEVICE" bash <<'STOP'
# Kill respawn wrapper first, then the app itself
killall -9 respawn.sh   2>/dev/null || true
killall -9 app_launcher 2>/dev/null || true
rm -f /opt/roomwizard/respawn.sh /var/run/roomwizard-app.pid
# Brief pause to ensure file handles are released
sleep 1
STOP
ok "Launcher stopped"

# Ensure target directory exists
info "Ensuring target directories exist..."
ssh "$DEVICE" "mkdir -p $GAMES_DIR /var/log/roomwizard"
ok "Target directories ready"

# Upload game binaries
info "Uploading game binaries → $GAMES_DIR/"
scp build/snake build/tetris build/pong \
    build/brick_breaker \
    build/samegame \
    build/frogger \
    build/platformer \
    build/game_selector build/hardware_test \
    build/hardware_config \
    build/hardware_diag \
    build/unified_calibrate \
    build/audio_touch_test \
    build/backlight \
    build/device_tools \
    "$DEVICE:$GAMES_DIR/"
ok "Game binaries uploaded"

# Upload app launcher
info "Uploading app launcher → /opt/roomwizard/"
scp build/app_launcher "$DEVICE:/opt/roomwizard/"
ok "App launcher uploaded"

# Upload icons (if any)
if ls build/icons/*.ppm &>/dev/null; then
    info "Uploading icons → /opt/roomwizard/icons/"
    ssh "$DEVICE" "mkdir -p /opt/roomwizard/icons"
    scp build/icons/*.ppm "$DEVICE:/opt/roomwizard/icons/"
    ok "Icons uploaded ($(ls build/icons/*.ppm | wc -l) file(s))"
fi

# Set permissions + markers
info "Setting permissions and markers..."
ssh "$DEVICE" bash <<'REMOTE'
chmod +x /opt/games/snake /opt/games/tetris /opt/games/pong \
         /opt/games/brick_breaker \
         /opt/games/samegame \
         /opt/games/frogger \
         /opt/games/platformer \
         /opt/games/game_selector /opt/games/hardware_test \
         /opt/games/hardware_config \
         /opt/games/hardware_diag \
         /opt/games/unified_calibrate /opt/games/backlight \
         /opt/games/device_tools \
         /opt/roomwizard/app_launcher

# .noargs marker for scummvm (if present)
[ -f /opt/games/scummvm ] && touch /opt/games/scummvm.noargs && chmod 644 /opt/games/scummvm.noargs

# .hidden markers for dev tools (hidden from game_selector but still accessible via SSH)
for name in touch_test touch_debug touch_inject touch_calibrate pressure_test backlight hardware_test hardware_config hardware_diag unified_calibrate; do
    touch  /opt/games/$name.hidden 2>/dev/null || true
    chmod 644 /opt/games/$name.hidden 2>/dev/null || true
done
REMOTE
ok "Permissions and markers set"

# Deploy app manifests
info "Installing app manifests..."
ssh "$DEVICE" bash <<'REMOTE'
mkdir -p /opt/roomwizard/apps

cat > /opt/roomwizard/apps/snake.app << 'APP'
name=Snake
exec=/opt/games/snake
icon=/opt/roomwizard/icons/snake.ppm
args=fb,touch
APP

cat > /opt/roomwizard/apps/tetris.app << 'APP'
name=Tetris
exec=/opt/games/tetris
icon=/opt/roomwizard/icons/tetris.ppm
args=fb,touch
APP

cat > /opt/roomwizard/apps/pong.app << 'APP'
name=Pong
exec=/opt/games/pong
icon=/opt/roomwizard/icons/pong.ppm
args=fb,touch
APP

cat > /opt/roomwizard/apps/brick_breaker.app << 'APP'
name=Brick Breaker
exec=/opt/games/brick_breaker
icon=/opt/roomwizard/icons/brick_breaker.ppm
args=fb,touch
APP

cat > /opt/roomwizard/apps/samegame.app << 'APP'
name=SameGame
exec=/opt/games/samegame
icon=/opt/roomwizard/icons/samegame.ppm
args=fb,touch
APP

cat > /opt/roomwizard/apps/frogger.app << 'APP'
name=Frogger
exec=/opt/games/frogger
icon=/opt/roomwizard/icons/frogger.ppm
args=fb,touch
APP

cat > /opt/roomwizard/apps/platformer.app << 'APP'
name=Office Runner
exec=/opt/games/platformer
icon=/opt/roomwizard/icons/platformer.ppm
args=fb,touch
APP

cat > /opt/roomwizard/apps/audio_touch_test.app << 'APP'
name=Tap-a-Theremin
exec=/opt/games/audio_touch_test
icon=/opt/roomwizard/icons/audio_touch_test.ppm
args=fb,touch
APP

cat > /opt/roomwizard/apps/device_tools.app << 'APP'
name=Device Tools
exec=/opt/games/device_tools
icon=/opt/roomwizard/icons/device_tools.ppm
args=
APP

# Remove old tool manifests that are now consolidated into device_tools
rm -f /opt/roomwizard/apps/hardware_test.app \
      /opt/roomwizard/apps/hardware_config.app \
      /opt/roomwizard/apps/hardware_diag.app \
      /opt/roomwizard/apps/calibrate.app \
      /opt/roomwizard/apps/usb_test.app
REMOTE
ok "App manifests installed"
echo ""

# ── 4. set-default mode? ────────────────────────────────────────────────────
if [[ "$MODE" == "set-default" ]]; then
    info "Setting app launcher as default app..."
    ssh "$DEVICE" "mkdir -p /opt/roomwizard && echo '/opt/roomwizard/app_launcher' > /opt/roomwizard/default-app"
    ok "Default app → /opt/roomwizard/app_launcher"
fi

# ── 5. restart launcher ─────────────────────────────────────────────────────
# If the init service is installed, restart it (re-creates respawn wrapper).
# Otherwise just tell the user how to start manually.
if ssh "$DEVICE" '[ -f /etc/init.d/roomwizard-app ]' 2>/dev/null; then
    info "Restarting app launcher..."
    ssh "$DEVICE" '/etc/init.d/roomwizard-app start' 2>&1 | grep -v '^$'
    ok "Launcher running"
else
    echo ""
    echo "  To start app launcher:"
    echo "    ssh $DEVICE '/opt/roomwizard/app_launcher'"
fi

echo ""
