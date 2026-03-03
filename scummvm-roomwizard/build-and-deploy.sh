#!/bin/bash
#
# ScummVM Cross-Compilation Script for RoomWizard
# Builds ScummVM with custom RoomWizard backend for ARM embedded device
#
# Usage:
#   ./build-and-deploy.sh                          # build only (= all)
#   ./build-and-deploy.sh <ip>                     # build + deploy
#   ./build-and-deploy.sh <ip> set-default         # build + deploy + set as boot app
#   ./build-and-deploy.sh <ip> <command>            # run a specific build stage + deploy
#
# Commands: clean, configure, build, strip, deploy, set-default, all, info
#
# Requirements:
#   - WSL Ubuntu 20.04 or later
#   - ARM cross-compiler: sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
#   - SSH access to device
#
# System setup (bloatware cleanup, init service, audio, time-sync) is handled
# separately by setup-device.sh.  Run that once before deploying for the first time.
#

set -e  # Exit on error

# Capture script directory before any cd
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Argument parsing ────────────────────────────────────────────────────────
# Detect whether $1 is an IP address or a command.
# If it looks like an IP, treat as: <ip> [command]
# Otherwise treat as: [command]  (build-only, no deploy)
if echo "${1:-}" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'; then
    DEVICE_IP="$1"
    CMD="${2:-deploy}"
else
    DEVICE_IP=""
    CMD="${1:-all}"
fi

# Configuration
DEVICE_USER="root"
DEVICE_PATH="/opt/games"
SCUMMVM_DIR="../scummvm"
NATIVE_APPS_DIR="../native_apps"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check if running in WSL
    if ! grep -qi microsoft /proc/version; then
        log_warning "Not running in WSL - this script is designed for WSL"
    fi
    
    # Check for ARM cross-compiler
    if ! command -v arm-linux-gnueabihf-gcc &> /dev/null; then
        log_error "ARM cross-compiler not found!"
        echo "Install with: sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf"
        exit 1
    fi
    
    # Check ScummVM directory
    if [ ! -d "$SCUMMVM_DIR" ]; then
        log_error "ScummVM directory not found: $SCUMMVM_DIR"
        echo ""
        echo "You need to clone the ScummVM repository first:"
        echo "  cd $(dirname $SCUMMVM_DIR)"
        echo "  git clone https://github.com/scummvm/scummvm.git"
        echo "  cd scummvm"
        echo "  git checkout branch-2-8"
        echo ""
        echo "Then restore the RoomWizard backend files:"
        echo "  cd ../scummvm-roomwizard"
        echo "  bash manage-scummvm-changes.sh restore"
        exit 1
    fi
    
    # Check if backend files are synced
    if [ ! -f "$SCUMMVM_DIR/backends/platform/roomwizard/roomwizard.cpp" ]; then
        log_error "RoomWizard backend files not found in ScummVM directory!"
        echo ""
        echo "Run the following to restore backend files:"
        echo "  bash manage-scummvm-changes.sh restore"
        echo ""
        echo "This will copy the backend files from backend-files/ to the ScummVM directory."
        exit 1
    fi
    
    # Check native_apps directory (for common libraries)
    if [ ! -d "$NATIVE_APPS_DIR/common" ]; then
        log_error "Native apps common directory not found: $NATIVE_APPS_DIR/common"
        exit 1
    fi
    
    log_success "Prerequisites check passed"
}

# Clean build artifacts
clean_build() {
    log_info "Cleaning build artifacts..."
    
    cd "$SCUMMVM_DIR"
    
    # Clean ScummVM build
    if [ -f "Makefile" ]; then
        make clean || true
    fi
    
    # Remove object files from native_apps/common
    rm -f "$NATIVE_APPS_DIR/common/"*.o
    
    # Remove config files
    rm -f config.mk config.h
    
    log_success "Clean complete"
}

# Configure ScummVM build
configure_build() {
    log_info "Configuring ScummVM build..."
    
    cd "$SCUMMVM_DIR"
    
    # Configure with ARM cross-compiler
    # Explicitly set CC and CXX to ensure C files are compiled with ARM compiler
    CC=arm-linux-gnueabihf-gcc \
    CXX=arm-linux-gnueabihf-g++ \
    ./configure \
        --host=arm-linux-gnueabihf \
        --backend=roomwizard \
        --disable-all-engines \
        --enable-engine=scumm \
        --enable-engine=scumm-7-8 \
        --enable-engine=he \
        --enable-engine=agi \
        --enable-engine=sci \
        --enable-engine=agos \
        --enable-engine=sky \
        --enable-engine=queen \
        --disable-mt32emu \
        --disable-flac \
        --disable-mad \
        --disable-vorbis \
        --enable-release \
        --enable-optimizations \
        --enable-vkeybd
    
    # Verify CC and CXX are set correctly in config.mk
    CC_SET=$(grep "^CC " config.mk | head -1 || echo "")
    CXX_SET=$(grep "^CXX " config.mk | head -1 || echo "")
    
    if [ -z "$CC_SET" ]; then
        log_warning "CC not set in config.mk - adding it manually"
        # Add CC after CXX line
        sed -i '/^CXX :=/a CC := arm-linux-gnueabihf-gcc' config.mk
        log_success "Added CC := arm-linux-gnueabihf-gcc to config.mk"
    elif echo "$CC_SET" | grep -q "arm-linux-gnueabihf-gcc"; then
        log_success "Configuration complete - CC set to ARM compiler"
    else
        log_warning "CC is set but may not be correct: $CC_SET"
    fi
    
    if echo "$CXX_SET" | grep -q "arm-linux-gnueabihf-g++"; then
        log_success "CXX set to ARM compiler"
    else
        log_warning "CXX may not be set correctly: $CXX_SET"
    fi
}

# Build ScummVM
build_scummvm() {
    log_info "Building ScummVM..."
    
    cd "$SCUMMVM_DIR"
    
    # Check if configured
    if [ ! -f "config.mk" ]; then
        log_error "Not configured! Run: $0 configure"
        exit 1
    fi
    
    # Build with static linking
    # Use -j4 for parallel compilation (adjust based on CPU cores)
    make -j4 LDFLAGS='-static' LIBS='-lpthread -lm'
    
    # Check if binary was created
    if [ -f "scummvm" ]; then
        # Get binary size
        SIZE=$(du -h scummvm | cut -f1)
        log_success "Build complete - Binary size: $SIZE"
        
        # Check architecture
        ARCH=$(file scummvm | grep -o "ARM" || echo "UNKNOWN")
        if [ "$ARCH" = "ARM" ]; then
            log_success "Binary architecture: ARM (correct)"
        else
            log_error "Binary architecture check failed!"
            file scummvm
            exit 1
        fi
    else
        log_error "Build failed - scummvm binary not found"
        exit 1
    fi
}

# Strip binary
strip_binary() {
    log_info "Stripping binary..."
    
    cd "$SCUMMVM_DIR"
    
    if [ ! -f "scummvm" ]; then
        log_error "Binary not found! Run: $0 build"
        exit 1
    fi
    
    # Get size before stripping
    SIZE_BEFORE=$(du -h scummvm | cut -f1)
    
    # Strip debug symbols
    arm-linux-gnueabihf-strip scummvm
    
    # Get size after stripping
    SIZE_AFTER=$(du -h scummvm | cut -f1)
    
    log_success "Strip complete - Size: $SIZE_BEFORE → $SIZE_AFTER"
}

# Deploy to device
deploy_to_device() {
    if [ -z "$DEVICE_IP" ]; then
        log_error "No IP supplied. Usage: $0 <ip> [deploy|set-default]"
        exit 1
    fi

    log_info "Deploying to device ($DEVICE_IP)..."
    
    cd "$SCUMMVM_DIR"
    
    if [ ! -f "scummvm" ]; then
        log_error "Binary not found! Run: $0 build"
        exit 1
    fi
    
    local DEVICE="$DEVICE_USER@$DEVICE_IP"
    
    # Check if device is reachable
    log_info "Testing SSH connection..."
    ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
        || { log_error "Cannot reach $DEVICE — check IP and SSH key"; exit 1; }
    
    # Verify system setup has been done
    if ! ssh "$DEVICE" "[ -f /opt/roomwizard/disable-steelcase.sh ]" 2>/dev/null; then
        log_warning "System setup not detected on device."
        log_warning "Run setup-device.sh first:  ../setup-device.sh $DEVICE_IP"
        echo ""
        read -p "Continue deploying anyway? (y/n): " confirm
        [[ "$confirm" != "y" ]] && exit 1
    fi
    
    # Ensure target directory exists
    ssh "$DEVICE" "mkdir -p $DEVICE_PATH /var/log/roomwizard"
    
    # Stop running launcher/respawn (avoids "Text file busy")
    log_info "Stopping running launcher (if any)..."
    ssh "$DEVICE" bash <<'STOP'
killall -9 respawn.sh   2>/dev/null || true
killall -9 app_launcher 2>/dev/null || true
killall -9 scummvm      2>/dev/null || true
rm -f /opt/roomwizard/respawn.sh /var/run/roomwizard-app.pid
sleep 1
STOP
    
    # Copy binary to device
    log_info "Copying binary to device..."
    scp scummvm "$DEVICE:$DEVICE_PATH/"
    
    # Make executable + set markers
    log_info "Setting permissions and markers..."
    ssh "$DEVICE" "chmod +x $DEVICE_PATH/scummvm; touch $DEVICE_PATH/scummvm.noargs; chmod 644 $DEVICE_PATH/scummvm.noargs"
    
    # Verify deployment
    log_info "Verifying deployment..."
    REMOTE_SIZE=$(ssh "$DEVICE" "du -h $DEVICE_PATH/scummvm | cut -f1")
    
    log_success "Deployment complete - Remote size: $REMOTE_SIZE"

    # Install app manifest (for app launcher)
    log_info "Installing app manifest and icon..."
    ssh "$DEVICE" "mkdir -p /opt/roomwizard/apps /opt/roomwizard/icons"
    # Upload icon from scummvm-roomwizard/ dir (not from the scummvm build dir)
    if [ -f "$SCRIPT_DIR/scummvm.ppm" ]; then
        scp "$SCRIPT_DIR/scummvm.ppm" "$DEVICE:/opt/roomwizard/icons/scummvm.ppm"
    fi
    ssh "$DEVICE" bash <<'REMOTE'
cat > /opt/roomwizard/apps/scummvm.app << 'APP'
name=ScummVM
exec=/opt/games/scummvm
icon=/opt/roomwizard/icons/scummvm.ppm
args=none
APP
REMOTE
    log_success "App manifest installed"
    log_info "Run on device: ssh $DEVICE '$DEVICE_PATH/scummvm'"

    # Restart launcher (picks up updated manifest/icon)
    if ssh "$DEVICE" '[ -f /etc/init.d/roomwizard-app ]' 2>/dev/null; then
        log_info "Restarting launcher..."
        ssh "$DEVICE" '/etc/init.d/roomwizard-app start' 2>&1 | grep -v '^$'
        log_success "Launcher running"
    fi
}

# Set ScummVM as default boot app
set_default_app() {
    if [ -z "$DEVICE_IP" ]; then
        log_error "No IP supplied. Usage: $0 <ip> set-default"
        exit 1
    fi

    log_info "Setting ScummVM as default app on $DEVICE_IP..."
    
    local DEVICE="$DEVICE_USER@$DEVICE_IP"
    
    # Check if device is reachable
    ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
        || { log_error "Cannot reach $DEVICE — check IP and SSH key"; exit 1; }
    
    ssh "$DEVICE" "mkdir -p /opt/roomwizard && echo '$DEVICE_PATH/scummvm' > /opt/roomwizard/default-app"
    
    log_success "Default app → $DEVICE_PATH/scummvm"
    echo ""
    # Restart launcher
    if ssh "$DEVICE" '[ -f /etc/init.d/roomwizard-app ]' 2>/dev/null; then
        log_info "Restarting launcher..."
        ssh "$DEVICE" '/etc/init.d/roomwizard-app start' 2>&1 | grep -v '^$'
        log_success "Launcher running"
    fi
}

# Show build info
show_info() {
    log_info "ScummVM RoomWizard Build Information"
    echo ""
    echo "Configuration:"
    echo "  Device IP:      $DEVICE_IP"
    echo "  Device Path:    $DEVICE_PATH"
    echo "  ScummVM Dir:    $SCUMMVM_DIR"
    echo "  Native Apps:    $NATIVE_APPS_DIR"
    echo ""
    echo "Engines Enabled:"
    echo "  - SCUMM (v0-v6): Monkey Island, Day of the Tentacle, etc."
    echo "  - SCUMM-7-8:     Full Throttle, The Dig, Curse of Monkey Island"
    echo "  - HE:            Putt-Putt, Freddi Fish, Pajama Sam"
    echo "  - AGI:           Kings Quest 1-3, Police Quest 1, Larry 1"
    echo "  - SCI:           Kings Quest 4-6, Police Quest 2-3, Larry 2-6"
    echo "  - AGOS:          Simon the Sorcerer"
    echo "  - SKY:           Beneath a Steel Sky"
    echo "  - QUEEN:         Flight of the Amazon Queen"
    echo ""
    echo "Build Features:"
    echo "  - Static linking (no dependencies)"
    echo "  - Release build with optimizations"
    echo "  - Custom RoomWizard backend"
    echo "  - Touch input support (resistive single-touch)"
    echo "  - Framebuffer rendering (800x480)"
    echo "  - Bezel-aware viewport scaling"
    echo "  - Virtual keyboard (vkeybd_roomwizard.zip)"
    echo "  - Audio via TWL4030 speaker (/dev/dsp OSS, O_NONBLOCK, 22050 Hz, 50% attenuation)"
    echo ""
}

# Main script
case "$CMD" in
    clean)
        check_prerequisites
        clean_build
        ;;
    configure)
        check_prerequisites
        configure_build
        ;;
    build)
        check_prerequisites
        build_scummvm
        ;;
    strip)
        check_prerequisites
        strip_binary
        ;;
    deploy)
        check_prerequisites
        build_scummvm
        strip_binary
        deploy_to_device
        ;;
    set-default)
        check_prerequisites
        build_scummvm
        strip_binary
        deploy_to_device
        set_default_app
        ;;
    all)
        check_prerequisites
        clean_build
        configure_build
        build_scummvm
        strip_binary
        log_success "Build complete! Binary ready at: $SCUMMVM_DIR/scummvm"
        echo ""
        log_info "To deploy: $0 <ip>"
        log_info "To deploy + set as boot app: $0 <ip> set-default"
        ;;
    info)
        show_info
        ;;
    *)
        echo "Usage: $0 [ip] [command]"
        echo ""
        echo "  $0                          Build only (clean + configure + build + strip)"
        echo "  $0 <ip>                     Build + deploy"
        echo "  $0 <ip> set-default         Build + deploy + set as boot app"
        echo ""
        echo "Commands:"
        echo "  all          - Clean, configure, build, and strip (default)"
        echo "  clean        - Clean build artifacts"
        echo "  configure    - Configure build system"
        echo "  build        - Compile ScummVM"
        echo "  strip        - Strip debug symbols"
        echo "  deploy       - Build + strip + deploy to device"
        echo "  set-default  - Build + strip + deploy + set as default boot app"
        echo "  info         - Show build information"
        exit 1
        ;;
esac
