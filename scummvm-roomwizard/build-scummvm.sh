#!/bin/bash
#
# ScummVM Cross-Compilation Script for RoomWizard
# Builds ScummVM with custom RoomWizard backend for ARM embedded device
#
# Usage: ./build-scummvm.sh [clean|configure|build|strip|deploy|all]
#
# Requirements:
#   - WSL Ubuntu 20.04 or later
#   - ARM cross-compiler: sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
#   - SSH access to device (192.168.50.73)
#

set -e  # Exit on error

# Configuration
DEVICE_IP="192.168.50.73"
DEVICE_USER="root"
DEVICE_PATH="/opt/games"
SCUMMVM_DIR="../scummvm"
NATIVE_GAMES_DIR="../native_games"

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
        exit 1
    fi
    
    # Check native_games directory (for common libraries)
    if [ ! -d "$NATIVE_GAMES_DIR/common" ]; then
        log_error "Native games common directory not found: $NATIVE_GAMES_DIR/common"
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
    
    # Remove object files from native_games/common
    rm -f "$NATIVE_GAMES_DIR/common/"*.o
    
    # Remove config files
    rm -f config.mk config.h
    
    log_success "Clean complete"
}

# Configure ScummVM build
configure_build() {
    log_info "Configuring ScummVM build..."
    
    cd "$SCUMMVM_DIR"
    
    # Ensure clean state
    rm -f "$NATIVE_GAMES_DIR/common/"*.o
    
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
        --disable-alsa \
        --disable-mt32emu \
        --disable-flac \
        --disable-mad \
        --disable-vorbis \
        --enable-release \
        --enable-optimizations
    
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
    make -j4 LDFLAGS='-static'
    
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
    log_info "Deploying to device ($DEVICE_IP)..."
    
    cd "$SCUMMVM_DIR"
    
    if [ ! -f "scummvm" ]; then
        log_error "Binary not found! Run: $0 build"
        exit 1
    fi
    
    # Check if device is reachable
    if ! ping -c 1 -W 2 "$DEVICE_IP" &> /dev/null; then
        log_error "Device not reachable at $DEVICE_IP"
        exit 1
    fi
    
    # Copy binary to device
    log_info "Copying binary to device..."
    scp scummvm "$DEVICE_USER@$DEVICE_IP:$DEVICE_PATH/"
    
    # Make executable
    log_info "Setting executable permissions..."
    ssh "$DEVICE_USER@$DEVICE_IP" "chmod +x $DEVICE_PATH/scummvm"
    
    # Verify deployment
    log_info "Verifying deployment..."
    REMOTE_SIZE=$(ssh "$DEVICE_USER@$DEVICE_IP" "du -h $DEVICE_PATH/scummvm | cut -f1")
    
    log_success "Deployment complete - Remote size: $REMOTE_SIZE"
    log_info "Run on device: ssh $DEVICE_USER@$DEVICE_IP '$DEVICE_PATH/scummvm'"
}

# Show build info
show_info() {
    log_info "ScummVM RoomWizard Build Information"
    echo ""
    echo "Configuration:"
    echo "  Device IP:      $DEVICE_IP"
    echo "  Device Path:    $DEVICE_PATH"
    echo "  ScummVM Dir:    $SCUMMVM_DIR"
    echo "  Native Games:   $NATIVE_GAMES_DIR"
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
    echo "  - Touch input support"
    echo "  - Framebuffer rendering (800x480)"
    echo "  - Bezel-aware viewport scaling"
    echo ""
}

# Main script
main() {
    case "${1:-all}" in
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
            deploy_to_device
            ;;
        all)
            check_prerequisites
            clean_build
            configure_build
            build_scummvm
            strip_binary
            log_success "Build complete! Binary ready at: $SCUMMVM_DIR/scummvm"
            echo ""
            log_info "To deploy: $0 deploy"
            ;;
        info)
            show_info
            ;;
        *)
            echo "Usage: $0 [clean|configure|build|strip|deploy|all|info]"
            echo ""
            echo "Commands:"
            echo "  clean      - Clean build artifacts"
            echo "  configure  - Configure build system"
            echo "  build      - Compile ScummVM"
            echo "  strip      - Strip debug symbols"
            echo "  deploy     - Deploy to device"
            echo "  all        - Clean, configure, build, and strip (default)"
            echo "  info       - Show build information"
            echo ""
            echo "Example workflow:"
            echo "  $0 all      # Build everything"
            echo "  $0 deploy   # Deploy to device"
            exit 1
            ;;
    esac
}

# Run main function
main "$@"
