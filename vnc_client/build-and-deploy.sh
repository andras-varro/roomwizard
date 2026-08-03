#!/bin/bash
# Build and deploy the VNC client to a RoomWizard device.
#
# Usage:
#   ./build-and-deploy.sh                        # build only
#   ./build-and-deploy.sh <ip>                   # build + deploy
#   ./build-and-deploy.sh <ip> run               # build + deploy + run
#   ./build-and-deploy.sh <ip> set-default       # build + deploy + set as default boot app
#
# System setup (bloatware cleanup, init service, audio, time-sync) is handled
# separately by setup-device.sh.  Run that once before deploying for the first time.

set -e
_START_SECONDS=$(date +%s)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

DEVICE_IP="${1:-}"
MODE="${2:-}"
DEVICE="root@${DEVICE_IP}"
REMOTE_DIR="/opt/vnc_client"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "[$(date '+%H:%M:%S')] ${GREEN}  ✓ $*${NC}"; }
info() { echo -e "[$(date '+%H:%M:%S')] ${YELLOW}  → $*${NC}"; }
warn() { echo -e "[$(date '+%H:%M:%S')] ${BLUE}  ! $*${NC}"; }
err()  { echo -e "[$(date '+%H:%M:%S')] ${RED}  ✗ $*${NC}"; exit 1; }

# ── argument validation ─────────────────────────────────────────────────────
# Validate before building, not at the first ssh (../IMPROVEMENT_PLAN.md B19).
usage() {
    echo "Usage: $0 [<ip>] [run|set-default]"
    echo ""
    echo "  <ip>          Device IPv4 address; omit to build without deploying"
    echo "  run           Also start vnc_client on the device"
    echo "  set-default   Also make vnc_client the boot app"
    exit 1
}

IPV4_RE='^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}$'
if [[ -n "$DEVICE_IP" ]] && [[ ! "$DEVICE_IP" =~ $IPV4_RE ]]; then
    echo "Not an IPv4 address: $DEVICE_IP"; echo ""; usage
fi

case "$MODE" in
    ""|run|set-default) ;;
    *) echo "Unknown mode: $MODE"; echo ""; usage ;;
esac

# ── 1. cross-compiler check ─────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " VNC Client Build + Deploy"
echo "════════════════════════════════════════"
info "Started — $(date '+%Y-%m-%d %H:%M:%S')"

CC=arm-linux-gnueabihf-gcc
if ! command -v $CC &>/dev/null; then
    err "ARM cross-compiler not found. Install with:\n  sudo apt-get install gcc-arm-linux-gnueabihf"
fi
info "Compiler: $($CC --version | head -1)"
echo ""

# ── 2. check dependencies ───────────────────────────────────────────────────
if [ ! -f deps/lib/libvncclient.a ] || [ ! -f deps/lib/libz.a ] || [ ! -f deps/lib/libjpeg.a ]; then
    info "Dependencies not found, building..."
    chmod +x build-deps.sh
    ./build-deps.sh
    ok "Dependencies built"
else
    ok "Dependencies present"
fi

# ── 3. build ─────────────────────────────────────────────────────────────────
info "Cleaning previous build..."
make clean 2>/dev/null || true

info "Building VNC client..."
make 2>&1 | tail -5
echo ""
ok "Build complete: $(ls -lh vnc_client_stripped | awk '{print $5}')"
echo ""

# ── 3b. ARM-safety gate ─────────────────────────────────────────────────────
# Cortex-A8 has no hardware integer divide; an sdiv/udiv means SIGILL (exit 132)
# with a blank screen and no output.  Checks the UNSTRIPPED binary on purpose:
# without symbols objdump disassembles literal pools as code and reports
# phantom hits (vnc_client_stripped yields a bogus "sdiv r4, sp, pc").
if [[ -x ../native_apps/check-arm-safe.sh ]]; then
    ../native_apps/check-arm-safe.sh vnc_client \
        || err "ARM-safety check failed — refusing to deploy"
else
    warn "../native_apps/check-arm-safe.sh missing — skipping hardware-divide gate"
fi
echo ""

# ── 4. deploy? ───────────────────────────────────────────────────────────────
if [[ -z "$DEVICE_IP" ]]; then
    echo "No IP supplied — build only. To deploy:"
    echo "  ./build-and-deploy.sh <ip>"
    echo "  ./build-and-deploy.sh <ip> run"
    echo "  ./build-and-deploy.sh <ip> run --kill"
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

# Stop whatever is running (avoids "Text file busy").
#
# One stop implementation, on the device, matching on the executable — it catches
# a vnc_client the launcher started, which the `killall vnc_client` that used to
# be here could not reliably do (../IMPROVEMENT_PLAN.md B20, B25).  Do not re-add
# a killall here.
info "Stopping running apps (device init script)..."
ssh "$DEVICE" 'if [ -x /etc/init.d/roomwizard-app ]; then
    /etc/init.d/roomwizard-app stop
else
    echo "  /etc/init.d/roomwizard-app not installed - run ../setup-device.sh <ip>"
fi' || warn "stop reported a failure - a surviving process may hold the binary"
ok "Stopped"

# Ensure target directory exists
info "Creating $REMOTE_DIR on device..."
ssh "$DEVICE" "mkdir -p $REMOTE_DIR"

# Upload binary
info "Uploading vnc_client..."
scp vnc_client_stripped "$DEVICE:$REMOTE_DIR/vnc_client"
ssh "$DEVICE" "chmod +x $REMOTE_DIR/vnc_client"
ok "Binary uploaded ($(ls -lh vnc_client_stripped | awk '{print $5}'))"

# Upload config file (only if not already present on device, to preserve edits).
# vnc_client.conf is gitignored (it holds a plaintext password); fall back to
# the tracked template so a fresh clone can still deploy.
CONF_SRC="vnc_client.conf"
if [ ! -f "$CONF_SRC" ]; then
    CONF_SRC="vnc_client.conf.example"
    warn "vnc_client.conf not found locally - using $CONF_SRC (password is a placeholder)"
    warn "Create it with: cp vnc_client.conf.example vnc_client.conf  # then edit"
fi
if ssh "$DEVICE" "[ ! -f $REMOTE_DIR/vnc_client.conf ]" 2>/dev/null; then
    info "Uploading default config file..."
    scp "$CONF_SRC" "$DEVICE:$REMOTE_DIR/vnc_client.conf"
    ssh "$DEVICE" "chmod 600 $REMOTE_DIR/vnc_client.conf"
    ok "Config file deployed (mode 0600)"
else
    ok "Config file already present (preserved)"
    warn "To force-overwrite: scp $CONF_SRC $DEVICE:$REMOTE_DIR/vnc_client.conf"
fi

# Install app manifest (for app launcher)
info "Installing app manifest and icon..."
ssh "$DEVICE" "mkdir -p /opt/roomwizard/apps /opt/roomwizard/icons"
if [ -f vnc_client.ppm ]; then
    scp vnc_client.ppm "$DEVICE:/opt/roomwizard/icons/vnc_client.ppm"
fi
ssh "$DEVICE" bash <<'REMOTE'
cat > /opt/roomwizard/apps/vnc_client.app << 'APP'
name=VNC Client
exec=/opt/vnc_client/vnc_client
icon=/opt/roomwizard/icons/vnc_client.ppm
args=none
APP
REMOTE
ok "App manifest installed"

echo ""

# ── 5. run / set-default / restart? ───────────────────────────────────────────
if [[ "$MODE" == "run" ]]; then
    echo "════════════════════════════════════════"
    echo " Starting VNC Client"
    echo "════════════════════════════════════════"

    info "Launching on device (Ctrl+C to stop)..."
    echo ""
    ssh -t "$DEVICE" "$REMOTE_DIR/vnc_client 2>&1"
elif [[ "$MODE" == "set-default" ]]; then
    info "Setting VNC client as default app..."
    ssh "$DEVICE" "mkdir -p /opt/roomwizard && echo '$REMOTE_DIR/vnc_client' > /opt/roomwizard/default-app"
    ok "Default app → $REMOTE_DIR/vnc_client"
    # Restart init service
    if ssh "$DEVICE" '[ -f /etc/init.d/roomwizard-app ]' 2>/dev/null; then
        info "Restarting launcher..."
        ssh "$DEVICE" '/etc/init.d/roomwizard-app start' 2>&1 | grep -v '^$'
        ok "Launcher running"
    fi
else
    # Restart init service (launcher will pick up updated manifest)
    if ssh "$DEVICE" '[ -f /etc/init.d/roomwizard-app ]' 2>/dev/null; then
        info "Restarting launcher..."
        ssh "$DEVICE" '/etc/init.d/roomwizard-app start' 2>&1 | grep -v '^$'
        ok "Launcher running"
    else
        echo "  Deployed. To run interactively:"
        echo "    ssh $DEVICE '$REMOTE_DIR/vnc_client'"
    fi
fi

_END_SECONDS=$(date +%s)
_ELAPSED=$((_END_SECONDS - _START_SECONDS))
printf "[$(date '+%H:%M:%S')] Total time: %dm%02ds\n" $((_ELAPSED / 60)) $((_ELAPSED % 60))
echo ""
