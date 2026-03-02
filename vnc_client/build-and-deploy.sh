#!/bin/bash
# Build and deploy the VNC client to a RoomWizard device.
#
# Usage:
#   ./build-and-deploy.sh                        # build only
#   ./build-and-deploy.sh <ip>                   # build + deploy
#   ./build-and-deploy.sh <ip> run               # build + deploy + run
#   ./build-and-deploy.sh <ip> run --kill        # kill running client, then build + deploy + run
#   ./build-and-deploy.sh <ip> set-default       # build + deploy + set as default boot app
#
# System setup (bloatware cleanup, init service, audio, time-sync) is handled
# separately by setup-device.sh.  Run that once before deploying for the first time.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

DEVICE_IP="${1:-}"
MODE="${2:-}"
KILL_FIRST="${3:-}"
DEVICE="root@${DEVICE_IP}"
REMOTE_DIR="/opt/vnc_client"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }

# ── 1. cross-compiler check ─────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " VNC Client Build + Deploy"
echo "════════════════════════════════════════"

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

# Kill running client if requested or if deploying (binary will be busy)
if [[ "$KILL_FIRST" == "--kill" ]] || [[ "$MODE" == "run" ]]; then
    info "Stopping running VNC client (if any)..."
    ssh "$DEVICE" 'killall vnc_client 2>/dev/null || true; sleep 1'
    ok "Stopped"
fi

# Ensure target directory exists
info "Creating $REMOTE_DIR on device..."
ssh "$DEVICE" "mkdir -p $REMOTE_DIR"

# Upload binary
info "Uploading vnc_client..."
scp vnc_client_stripped "$DEVICE:$REMOTE_DIR/vnc_client"
ssh "$DEVICE" "chmod +x $REMOTE_DIR/vnc_client"
ok "Binary uploaded ($(ls -lh vnc_client_stripped | awk '{print $5}'))"

# Upload config file (only if not already present on device, to preserve edits)
if ssh "$DEVICE" "[ ! -f $REMOTE_DIR/vnc_client.conf ]" 2>/dev/null; then
    info "Uploading default config file..."
    scp vnc_client.conf "$DEVICE:$REMOTE_DIR/vnc_client.conf"
    ok "Config file deployed"
else
    ok "Config file already present (preserved)"
    warn "To force-overwrite: scp vnc_client.conf $DEVICE:$REMOTE_DIR/vnc_client.conf"
fi

echo ""

# ── 5. run / set-default? ─────────────────────────────────────────────────────
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
    echo ""
    echo "  Reboot to start:  ssh $DEVICE reboot"
    echo "  Or start now:     ssh $DEVICE '/etc/init.d/roomwizard-app restart'"
else
    echo "  Deployed. To run interactively:"
    echo "    ssh $DEVICE '$REMOTE_DIR/vnc_client'"
    echo ""
    echo "  To build, deploy, and run in one step:"
    echo "    ./build-and-deploy.sh $DEVICE_IP run"
    echo ""
    echo "  To kill a running client first:"
    echo "    ./build-and-deploy.sh $DEVICE_IP run --kill"
    echo ""
    echo "  To set as default boot app:"
    echo "    ./build-and-deploy.sh $DEVICE_IP set-default"
fi

echo ""
