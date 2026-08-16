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
#   ./build-and-deploy.sh --bundle <dir>            # build + stage into an offline bundle
#
# Commands: clean, configure, build, strip, deploy, set-default, all, info
#
# --bundle stages this component's artifacts under <dir>/root/<device-path> with
# a declared-mode manifest (../IMPROVEMENT_PLAN.md F9, F10).  This link takes
# ~1m35s–2m20s, so RW_BUNDLE_NO_BUILD=1 stages the binary already in
# $SCUMMVM_DIR instead of relinking — it refuses if there is nothing there, so it
# cannot silently bundle a component that was never built.  It CAN bundle a stale
# binary, which is the price of the escape hatch; leave it unset for a release.
#
# Requirements:
#   - WSL Ubuntu 20.04 or later
#   - ARM cross-compiler: sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
#   - wget (for downloading zlib/libpng sources): sudo apt install wget
#   - ARM libpng/zlib are cross-compiled automatically by this script (build_arm_deps)
#   - SSH access to device
#
# System setup (bloatware cleanup, init service, audio, time-sync) is handled
# separately by commissioning/provision.sh.  Run that once before deploying for the first time.
#

set -e  # Exit on error
_START_SECONDS=$(date +%s)

# Capture script directory before any cd
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# The shared SSH gate (../lib/rw-ssh.sh). Both of this script's gates —
# deploy and set-default — go through it.
# shellcheck source=../lib/rw-ssh.sh
. "$REPO_ROOT/lib/rw-ssh.sh"

# ── Argument parsing ────────────────────────────────────────────────────────
# Three shapes.  `--bundle <dir>` needs no device and is checked first, because
# it is neither an IP nor one of the build commands and would otherwise fall into
# the usage error at the bottom.
# If $1 looks like an IP, treat as: <ip> [command]
# Otherwise treat as: [command]  (build-only, no deploy)
BUNDLE_DIR=""
if [ "${1:-}" = "--bundle" ]; then
    BUNDLE_DIR="${2:-}"
    DEVICE_IP=""
    CMD="bundle"
    if [ -z "$BUNDLE_DIR" ]; then
        echo "--bundle requires a directory"
        echo "Usage: $0 --bundle <dir>"
        exit 1
    fi
    if [ -n "${3:-}" ]; then
        echo "Unexpected argument after --bundle <dir>: $3"
        exit 1
    fi
elif echo "${1:-}" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'; then
    DEVICE_IP="$1"
    CMD="${2:-deploy}"
else
    DEVICE_IP=""
    CMD="${1:-all}"
fi

# Configuration
DEVICE_USER="root"
DEVICE_PATH="/opt/games"
# Anchored to this script, not to the cwd.  Both were "../scummvm" and
# "../native_apps" relative, so invoking this by path resolved them against
# wherever you happened to be standing — `bash scummvm-roomwizard/build-and-deploy.sh`
# from the repo root looked for the ScummVM tree in the repo's *parent*.  It
# worked only because deploy-all.sh wraps it in a subshell cd.
SCUMMVM_DIR="$REPO_ROOT/scummvm"
NATIVE_APPS_DIR="$REPO_ROOT/native_apps"
ARM_DEPS_PREFIX="$SCRIPT_DIR/arm-deps"

# Engine batch level (0-5). Each level includes all engines from previous levels.
# 0 = base (8 original engines only)
# 1 = + small zero-dep engines (lure, drascula, touche, teenagent, tucker, hugo, draci, plumbers, supernova, efh, cge, cge2, dreamweb, bbvs, cine, cruise, lab, parallaction)
# 2 = + larger zero-dep engines (kyra, sword1, sword2, tinsel, gob, saga, tsage, made, mads, toon, sherlock, access, kingdom, mm, voyeur, hypno, chewy, gnap, illusions)
# 3 = + highres/16bit engines (griffon, neverhood, composer, mortevielle, toltecs, prince, hadesch, pink, private, asylum, cryomni3d, dragons, hopkins, tony, ngi, bladerunner, pegasus, freescape, mtropolis, trecision, adl, vcruise)
# 4 = + built-in feature engines (hdb, mohawk, ultima, twine, grim)
# 5 = + cross-compiled lib engines (requires building additional libs)
ENGINE_BATCH="${ENGINE_BATCH:-0}"

# Optional: add a single extra engine for testing
# Usage: EXTRA_ENGINE=kyra ./build-and-deploy.sh build
EXTRA_ENGINE="${EXTRA_ENGINE:-}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
log_info() {
    echo -e "[$(date '+%H:%M:%S')] ${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "[$(date '+%H:%M:%S')] ${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "[$(date '+%H:%M:%S')] ${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "[$(date '+%H:%M:%S')] ${RED}[ERROR]${NC} $1"
}

# Cross-compile zlib and libpng for ARM.
# These are needed for PNG thumbnail support in ScummVM.  Ubuntu Focal WSL
# doesn't support armhf multiarch packages (standard mirrors don't carry
# armhf — it lives on ports.ubuntu.com), so we build from source instead.
# Idempotent: skips if $ARM_DEPS_PREFIX/lib/libpng.a already exists.
build_arm_deps() {
    if [ -f "$ARM_DEPS_PREFIX/lib/libpng.a" ]; then
        log_success "ARM dependencies already built (${ARM_DEPS_PREFIX}/lib/libpng.a exists)"
        return 0
    fi

    local ORIG_DIR="$(pwd)"

    log_info "Cross-compiling zlib and libpng for ARM..."

    local BUILD_DIR
    BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/arm-deps-build.XXXXXX") || {
        log_error "Failed to create temp build directory"; cd "$ORIG_DIR"; exit 1
    }
    log_info "Using build directory: $BUILD_DIR"

    local ZLIB_LOG="$BUILD_DIR/zlib-build.log"
    local PNG_LOG="$BUILD_DIR/libpng-build.log"

    mkdir -p "$ARM_DEPS_PREFIX" || {
        log_error "Failed to create ARM deps prefix: $ARM_DEPS_PREFIX"; cd "$ORIG_DIR"; exit 1
    }

    # ── zlib 1.3.1 (skip if already built) ──────────────────────────────────
    if [ ! -f "$ARM_DEPS_PREFIX/lib/libz.a" ]; then
        log_info "Building zlib 1.3.1..."
        cd "$BUILD_DIR" || { log_error "Failed to cd into $BUILD_DIR"; cd "$ORIG_DIR"; exit 1; }

        log_info "Downloading zlib..."
        wget -L --no-check-certificate -O zlib-1.3.1.tar.gz \
            "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz" || {
            log_error "Failed to download zlib (tried GitHub mirror)"
            rm -rf "$BUILD_DIR"
            cd "$ORIG_DIR"; exit 1
        }

        tar xf zlib-1.3.1.tar.gz || {
            log_error "Failed to extract zlib tarball"; rm -rf "$BUILD_DIR"; cd "$ORIG_DIR"; exit 1
        }
        cd zlib-1.3.1 || { log_error "Failed to cd into zlib-1.3.1"; rm -rf "$BUILD_DIR"; cd "$ORIG_DIR"; exit 1; }

        CC=arm-linux-gnueabihf-gcc ./configure --prefix="$ARM_DEPS_PREFIX" --static \
            > "$ZLIB_LOG" 2>&1 || {
            log_error "zlib configure failed. Log: $ZLIB_LOG"
            tail -20 "$ZLIB_LOG"
            cd "$ORIG_DIR"; exit 1
        }
        make -j"$(nproc)" >> "$ZLIB_LOG" 2>&1 || {
            log_error "zlib build failed. Log: $ZLIB_LOG"
            tail -20 "$ZLIB_LOG"
            cd "$ORIG_DIR"; exit 1
        }
        make install >> "$ZLIB_LOG" 2>&1 || {
            log_error "zlib install failed. Log: $ZLIB_LOG"
            tail -20 "$ZLIB_LOG"
            cd "$ORIG_DIR"; exit 1
        }
        log_success "zlib installed to $ARM_DEPS_PREFIX"
    else
        log_success "zlib already built (skipped)"
    fi

    # ── libpng 1.6.43 (skip if already built) ───────────────────────────────
    if [ ! -f "$ARM_DEPS_PREFIX/lib/libpng.a" ]; then
        log_info "Building libpng 1.6.43..."
        cd "$BUILD_DIR" || { log_error "Failed to cd into $BUILD_DIR"; cd "$ORIG_DIR"; exit 1; }

        log_info "Downloading libpng..."
        wget -L --no-check-certificate -O libpng-1.6.43.tar.gz \
            "https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.43.tar.gz" || {
            log_error "Failed to download libpng"
            rm -rf "$BUILD_DIR"
            cd "$ORIG_DIR"; exit 1
        }

        log_info "Extracting libpng..."
        tar xzf libpng-1.6.43.tar.gz || {
            log_error "Failed to extract libpng"
            rm -rf "$BUILD_DIR"
            cd "$ORIG_DIR"; exit 1
        }

        # Find extracted directory (GitHub strips 'v' prefix from tag)
        LIBPNG_DIR=$(tar tzf libpng-1.6.43.tar.gz | head -1 | cut -d/ -f1)
        cd "$LIBPNG_DIR" || {
            log_error "libpng source directory not found"
            rm -rf "$BUILD_DIR"
            cd "$ORIG_DIR"; exit 1
        }

        # Use pre-built config header (no autotools needed)
        cp scripts/pnglibconf.h.prebuilt pnglibconf.h || {
            log_error "Failed to copy pnglibconf.h"
            rm -rf "$BUILD_DIR"
            cd "$ORIG_DIR"; exit 1
        }

        # Compile all libpng source files
        log_info "Compiling libpng..."
        PNG_SRCS="png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c pngwtran.c pngwutil.c"
        for src in $PNG_SRCS; do
            arm-linux-gnueabihf-gcc -c -O2 -I"$ARM_DEPS_PREFIX/include" "$src" || {
                log_error "Failed to compile $src"
                rm -rf "$BUILD_DIR"
                cd "$ORIG_DIR"; exit 1
            }
        done

        # Create static library
        arm-linux-gnueabihf-ar rcs libpng.a png.o pngerror.o pngget.o pngmem.o pngpread.o pngread.o pngrio.o pngrtran.o pngrutil.o pngset.o pngtrans.o pngwio.o pngwrite.o pngwtran.o pngwutil.o || {
            log_error "Failed to create libpng.a"
            rm -rf "$BUILD_DIR"
            cd "$ORIG_DIR"; exit 1
        }

        # Install
        cp libpng.a "$ARM_DEPS_PREFIX/lib/" || { log_error "Failed to install libpng.a"; cd "$ORIG_DIR"; exit 1; }
        cp png.h pngconf.h pnglibconf.h "$ARM_DEPS_PREFIX/include/" || { log_error "Failed to install headers"; cd "$ORIG_DIR"; exit 1; }

        log_success "libpng built and installed"
        cd "$BUILD_DIR"
    else
        log_success "libpng already built (skipped)"
    fi

    # Restore original working directory
    cd "$ORIG_DIR"

    # Clean up build directory
    rm -rf "$BUILD_DIR"
    log_success "ARM dependencies ready at $ARM_DEPS_PREFIX"
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
    
    # Ensure ARM zlib/libpng are available before configuring
    build_arm_deps
    
    cd "$SCUMMVM_DIR"
    
    # Remove stale config files so configure starts fresh
    if [ -f "config.mk" ] || [ -f "config.h" ]; then
        log_info "Removing stale config.mk/config.h before reconfigure..."
        rm -f config.mk config.h
    fi
    
    # ── Build engine list based on ENGINE_BATCH level ────────────────────────
    # Base engines (always included)
    ENGINES="--enable-engine=scumm --enable-engine=scumm-7-8 --enable-engine=he"
    ENGINES="$ENGINES --enable-engine=agi --enable-engine=sci --enable-engine=agos"
    ENGINES="$ENGINES --enable-engine=sky --enable-engine=queen"

    if [ "$ENGINE_BATCH" -ge 1 ]; then
        echo ">>> Including Batch 1 engines (small zero-dep)..."
        ENGINES="$ENGINES --enable-engine=lure --enable-engine=drascula --enable-engine=touche"
        ENGINES="$ENGINES --enable-engine=teenagent --enable-engine=tucker --enable-engine=hugo"
        ENGINES="$ENGINES --enable-engine=draci --enable-engine=plumbers --enable-engine=supernova"
        ENGINES="$ENGINES --enable-engine=efh --enable-engine=cge --enable-engine=cge2"
        ENGINES="$ENGINES --enable-engine=dreamweb --enable-engine=bbvs --enable-engine=cine"
        ENGINES="$ENGINES --enable-engine=cruise --enable-engine=lab --enable-engine=parallaction"
    fi

    if [ "$ENGINE_BATCH" -ge 2 ]; then
        echo ">>> Including Batch 2 engines (larger zero-dep)..."
        ENGINES="$ENGINES --enable-engine=kyra --enable-engine=sword1 --enable-engine=sword2"
        ENGINES="$ENGINES --enable-engine=tinsel --enable-engine=gob --enable-engine=saga"
        ENGINES="$ENGINES --enable-engine=tsage --enable-engine=made --enable-engine=mads"
        ENGINES="$ENGINES --enable-engine=toon --enable-engine=sherlock --enable-engine=access"
        ENGINES="$ENGINES --enable-engine=kingdom --enable-engine=mm --enable-engine=voyeur"
        ENGINES="$ENGINES --enable-engine=hypno --enable-engine=chewy --enable-engine=gnap"
        ENGINES="$ENGINES --enable-engine=illusions"
    fi

    if [ "$ENGINE_BATCH" -ge 3 ]; then
        echo ">>> Including Batch 3 engines (highres/16bit)..."
        ENGINES="$ENGINES --enable-engine=griffon --enable-engine=neverhood --enable-engine=composer"
        ENGINES="$ENGINES --enable-engine=mortevielle --enable-engine=toltecs --enable-engine=prince"
        ENGINES="$ENGINES --enable-engine=hadesch --enable-engine=pink --enable-engine=private"
        ENGINES="$ENGINES --enable-engine=asylum --enable-engine=cryomni3d --enable-engine=dragons"
        ENGINES="$ENGINES --enable-engine=hopkins --enable-engine=tony --enable-engine=ngi"
        ENGINES="$ENGINES --enable-engine=bladerunner --enable-engine=pegasus --enable-engine=freescape"
        ENGINES="$ENGINES --enable-engine=mtropolis --enable-engine=trecision --enable-engine=adl"
        ENGINES="$ENGINES --enable-engine=vcruise"
    fi

    if [ "$ENGINE_BATCH" -ge 4 ]; then
        echo ">>> Including Batch 4 engines (built-in features: lua, tinygl, bink)..."
        ENGINES="$ENGINES --enable-engine=hdb --enable-engine=mohawk --enable-engine=ultima"
        ENGINES="$ENGINES --enable-engine=twine --enable-engine=grim"
    fi

    if [ "$ENGINE_BATCH" -ge 5 ]; then
        echo ">>> Including Batch 5 engines (need cross-compiled libs)..."
        echo ">>> WARNING: Batch 5 requires libjpeg, freetype2, tremor, libmad to be cross-compiled"
        ENGINES="$ENGINES --enable-engine=groovie --enable-engine=wintermute"
        ENGINES="$ENGINES --enable-engine=buried --enable-engine=zvision --enable-engine=petka"
        ENGINES="$ENGINES --enable-engine=nancy --enable-engine=ags"
    fi

    # Optional: add a single extra engine for testing
    if [ -n "$EXTRA_ENGINE" ]; then
        echo ">>> Adding extra test engine: $EXTRA_ENGINE"
        ENGINES="$ENGINES --enable-engine=$EXTRA_ENGINE"
    fi

    echo ">>> Engine batch level: $ENGINE_BATCH"
    echo ">>> Configure engines: $ENGINES"

    # Configure with ARM cross-compiler
    # Explicitly set CC and CXX to ensure C files are compiled with ARM compiler
    # --with-zlib-prefix and --with-png-prefix point configure at our
    # cross-compiled ARM libraries (needed for PNG thumbnail support;
    # without it configure silently disables PNG and ScummVM crashes on
    # "No PNG support compiled!").
    CC=arm-linux-gnueabihf-gcc \
    CXX=arm-linux-gnueabihf-g++ \
    ./configure \
        --host=arm-linux-gnueabihf \
        --backend=roomwizard \
        --with-zlib-prefix="$ARM_DEPS_PREFIX" \
        --with-png-prefix="$ARM_DEPS_PREFIX" \
        --disable-all-engines \
        $ENGINES \
        --disable-mt32emu \
        --disable-flac \
        --disable-mad \
        --disable-vorbis \
        --enable-release \
        --enable-optimizations \
        --enable-vkeybd
    
    # Add pthread support - use --whole-archive for static linking to ensure
    # all pthread symbols are resolved (needed for ARM static cross-compilation)
    echo "LIBS += -Wl,--whole-archive -lpthread -Wl,--no-whole-archive" >> config.mk
    
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

# Restore backend source files from version control into the build tree.
# This ensures the build tree always reflects our latest edits.
# It also touches sources and removes stale .o files so make will recompile.
restore_backend_files() {
    log_info "Restoring backend files into build tree..."
    bash "$SCRIPT_DIR/manage-scummvm-changes.sh" restore
    log_success "Backend files restored and stale objects invalidated"
}

# Build ScummVM
build_scummvm() {
    log_info "Building ScummVM..."
    
    # Always sync latest backend sources into the build tree before compiling.
    # manage-scummvm-changes.sh restore also touches sources and removes stale
    # .o files, so make will recompile any changed backend files.
    restore_backend_files
    
    cd "$SCUMMVM_DIR"
    
    # Auto-configure if not yet configured, or reconfigure if stale
    if [ ! -f "config.mk" ]; then
        log_warning "Not configured yet — running configure automatically..."
        configure_build
        cd "$SCUMMVM_DIR"
    elif ! grep -q "^USE_PNG = 1" config.mk; then
        log_warning "Stale config detected (missing PNG support), reconfiguring..."
        configure_build
        cd "$SCUMMVM_DIR"
    fi
    
    # Always clean stale .o files from native_apps/common/ before building.
    # These are compiled by ScummVM's make (via configure.patch OBJS) and must
    # match the cross-compiler.  A stale x86 .o here causes:
    #   "file not recognized: file format not recognized"
    log_info "Cleaning native_apps/common/*.o (avoid stale cross-compile artifacts)..."
    rm -f "$NATIVE_APPS_DIR/common/"*.o
    
    # Build with static linking
    # Use -j4 for parallel compilation (adjust based on CPU cores)
    # Pass CC explicitly to ensure .c files use the ARM cross-compiler
    make -j4 CC=arm-linux-gnueabihf-gcc LDFLAGS='-static'
    
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

        # Report binary size for tracking
        UNSTRIPPED_SIZE=$(ls -la "$SCUMMVM_DIR/scummvm" | awk '{print $5}')
        STRIPPED_SIZE=$(ls -la scummvm_stripped 2>/dev/null | awk '{print $5}')
        echo ">>> Binary size (unstripped): $UNSTRIPPED_SIZE bytes ($(( UNSTRIPPED_SIZE / 1024 / 1024 )) MB)"
        if [ -n "$STRIPPED_SIZE" ]; then
            echo ">>> Binary size (stripped):   $STRIPPED_SIZE bytes ($(( STRIPPED_SIZE / 1024 / 1024 )) MB)"
        fi
        echo ">>> ENGINE_BATCH=$ENGINE_BATCH, EXTRA_ENGINE=${EXTRA_ENGINE:-none}"
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

    # ── ARM-safety gate, HERE and nowhere else ──────────────────────────────
    # Cortex-A8 has no hardware integer divide; an sdiv/udiv means SIGILL
    # (exit 132) — blank screen, no output, indistinguishable from "it didn't
    # start" (../SYSTEM_ANALYSIS.md#61-cortex-a8-has-no-hardware-integer-divide).  This is the only moment the UNSTRIPPED
    # binary exists: the strip below is in place.  Checking after it would
    # disassemble literal pools as code and report phantom hits, which is exactly
    # how vnc_client_stripped produced a bogus "sdiv r4, sp, pc".
    if [ -x "$NATIVE_APPS_DIR/check-arm-safe.sh" ]; then
        "$NATIVE_APPS_DIR/check-arm-safe.sh" scummvm || {
            log_error "ARM-safety check failed — refusing to strip or deploy"
            exit 1
        }
    else
        log_warning "$NATIVE_APPS_DIR/check-arm-safe.sh missing — skipping hardware-divide gate"
    fi

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
    # The shared gate (../lib/rw-ssh.sh): "down" and "up but refusing our key" are
    # different answers, and only the second has a remedy worth offering.
    rw_ssh_gate "$DEVICE" \
        || { log_error "Cannot continue without SSH to $DEVICE"; exit 1; }
    
    # Verify system setup has been done
    if ! ssh "$DEVICE" "[ -f /opt/roomwizard/disable-steelcase.sh ]" 2>/dev/null; then
        log_warning "System setup not detected on device."
        log_warning "Run commissioning/provision.sh first:  ../commissioning/provision.sh $DEVICE_IP"
        echo ""
        read -p "Continue deploying anyway? (y/n): " confirm
        [[ "$confirm" != "y" ]] && exit 1
    fi
    
    # Ensure target directory exists
    ssh "$DEVICE" "mkdir -p $DEVICE_PATH /var/log/roomwizard"
    
    # Stop whatever is running (avoids "Text file busy").
    #
    # One stop implementation, on the device, matching on the executable, so it also
    # catches an app that app_launcher started.  Do not re-add a killall here: this copy
    # used to kill `scummvm` and `app_launcher` by name and nothing else.
    log_info "Stopping running apps (device init script)..."
    ssh "$DEVICE" 'if [ -x /etc/init.d/roomwizard-app ]; then
    /etc/init.d/roomwizard-app stop
else
    echo "  /etc/init.d/roomwizard-app not installed - run ../commissioning/provision.sh <ip>"
fi' || log_warning "stop reported a failure - a surviving process may hold the binary"
    
    # Copy binary to device
    log_info "Copying binary to device..."
    scp scummvm "$DEVICE:$DEVICE_PATH/"
    
    # Deploy theme/GUI data files (without these, ScummVM falls back to green wireframe UI)
    log_info "Deploying theme files..."
    if [ -f "gui/themes/scummremastered.zip" ]; then
        scp gui/themes/scummremastered.zip "$DEVICE:$DEVICE_PATH/"
        log_success "Deployed scummremastered.zip"
    else
        log_warning "gui/themes/scummremastered.zip not found — theme will not be available"
    fi
    if [ -f "gui/themes/gui-icons.dat" ]; then
        scp gui/themes/gui-icons.dat "$DEVICE:$DEVICE_PATH/"
        log_success "Deployed gui-icons.dat"
    else
        log_warning "gui/themes/gui-icons.dat not found — GUI icons will not be available"
    fi
    
    # Deploy virtual keyboard pack (custom scaled layout for 800x480 display)
    local VKEYBD_ZIP="$SCRIPT_DIR/vkeybd_roomwizard.zip"
    if [ -f "$VKEYBD_ZIP" ]; then
        log_info "Deploying virtual keyboard pack..."
        scp "$VKEYBD_ZIP" "$DEVICE:$DEVICE_PATH/"
        log_success "Deployed vkeybd_roomwizard.zip"
    else
        log_warning "vkeybd_roomwizard.zip not found at $VKEYBD_ZIP — virtual keyboard will be disabled"
    fi
    
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
    # Written locally by the one generator, then copied — the same file --bundle
    # stages (../IMPROVEMENT_PLAN.md F10).
    local MANIFEST_DIR
    MANIFEST_DIR=$(mktemp -d)
    write_app_manifest "$MANIFEST_DIR"
    scp "$MANIFEST_DIR/scummvm.app" "$DEVICE:/opt/roomwizard/apps/scummvm.app"
    ssh "$DEVICE" "chmod 644 /opt/roomwizard/apps/scummvm.app"
    rm -rf "$MANIFEST_DIR"
    log_success "App manifest installed"
    log_info "Run on device: ssh $DEVICE '$DEVICE_PATH/scummvm'"

    # Restart launcher (picks up updated manifest/icon)
    if ssh "$DEVICE" '[ -f /etc/init.d/roomwizard-app ]' 2>/dev/null; then
        log_info "Restarting launcher..."
        ssh "$DEVICE" '/etc/init.d/roomwizard-app start' 2>&1 | grep -v '^$'
        log_success "Launcher running"
    fi
}

# ── the .app manifest, written once ─────────────────────────────────────────
# One heredoc, into a file, used by BOTH deploy_to_device and stage_bundle.  It
# used to live inside `ssh "$DEVICE" bash <<REMOTE`, so it existed only when a
# device was reachable (../IMPROVEMENT_PLAN.md F10).
#
# args=none: ScummVM opens /dev/fb0 and the evdev devices itself — it has its own
# input and audio layer, not native_apps/common (../SYSTEM_ANALYSIS.md#52-as-we-run-it--game-mode).
write_app_manifest() {
    cat > "$1/scummvm.app" <<'APP'
name=ScummVM
exec=/opt/games/scummvm
icon=/opt/roomwizard/icons/scummvm.ppm
args=none
APP
}

# ── stage this component into an offline bundle ─────────────────────────────
# Everything deploy_to_device scps, plus the .noargs marker it touches on the
# device.  Nothing here is config, so F9's "binaries only, never configs" caveat
# is satisfied by construction — ScummVM's own scummvm.ini is created on first run
# by the device.
#
# ⚠️ ScummVM is GPLv2+.  Publishing this binary carries a corresponding-source
# obligation; release.sh writes the offer into the bundle's NOTICE file.
stage_bundle() {
    local dir="$1" n

    # shellcheck source=../lib/rw-bundle.sh
    . "$REPO_ROOT/lib/rw-bundle.sh"

    if [ ! -f "$SCUMMVM_DIR/scummvm" ]; then
        log_error "No binary at $SCUMMVM_DIR/scummvm — nothing to stage."
        log_error "Run '$0 build' first, or unset RW_BUNDLE_NO_BUILD."
        exit 1
    fi

    rw_bundle_init "$dir" scummvm || { log_error "could not prepare $dir"; exit 1; }

    rw_bundle_add "$dir" scummvm 0755 "$SCUMMVM_DIR/scummvm" "$DEVICE_PATH/scummvm" \
        || { log_error "staging failed: scummvm"; exit 1; }

    # .noargs tells app_launcher to exec this with no framebuffer/touch argument.
    # deploy_to_device `touch`es it on the device; a bundle needs a real file.
    local NOARGS
    NOARGS=$(mktemp)
    : > "$NOARGS"
    rw_bundle_add "$dir" scummvm 0644 "$NOARGS" "$DEVICE_PATH/scummvm.noargs" \
        || { log_error "staging failed: scummvm.noargs"; exit 1; }
    rm -f "$NOARGS"

    # Theme and GUI data.  Without scummremastered.zip ScummVM falls back to the
    # green wireframe UI, which looks like a broken install rather than a missing
    # file — so these are warned about individually, not swept with a glob.
    for pair in \
        "$SCUMMVM_DIR/gui/themes/scummremastered.zip:$DEVICE_PATH/scummremastered.zip" \
        "$SCUMMVM_DIR/gui/themes/gui-icons.dat:$DEVICE_PATH/gui-icons.dat" \
        "$SCRIPT_DIR/vkeybd_roomwizard.zip:$DEVICE_PATH/vkeybd_roomwizard.zip" \
        "$SCRIPT_DIR/scummvm.ppm:/opt/roomwizard/icons/scummvm.ppm"
    do
        local src="${pair%%:*}" dev="${pair#*:}"
        if [ -f "$src" ]; then
            rw_bundle_add "$dir" scummvm 0644 "$src" "$dev" \
                || { log_error "staging failed: $src"; exit 1; }
        else
            log_warning "not staged (absent): $src"
        fi
    done

    local MANIFEST_DIR
    MANIFEST_DIR=$(mktemp -d)
    write_app_manifest "$MANIFEST_DIR"
    rw_bundle_add "$dir" scummvm 0644 "$MANIFEST_DIR/scummvm.app" \
        /opt/roomwizard/apps/scummvm.app || { log_error "staging failed: scummvm.app"; exit 1; }
    rm -rf "$MANIFEST_DIR"

    n=$(rw_bundle_finish "$dir" scummvm)
    log_success "Staged $n file(s) → $dir"
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
    rw_ssh_gate "$DEVICE" \
        || { log_error "Cannot continue without SSH to $DEVICE"; exit 1; }
    
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
log_info "ScummVM build started — $(date '+%Y-%m-%d %H:%M:%S') — command: $CMD"
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
        build_arm_deps
        clean_build
        configure_build
        build_scummvm
        strip_binary
        if [ -n "$DEVICE_IP" ]; then
            deploy_to_device
        else
            log_success "Build complete! Binary ready at: $SCUMMVM_DIR/scummvm"
            echo ""
            log_info "To deploy: $0 <ip>"
            log_info "To deploy + set as boot app: $0 <ip> set-default"
        fi
        ;;
    bundle)
        # RW_BUNDLE_NO_BUILD skips the ~2-minute link and stages the binary that
        # is already there.  stage_bundle refuses if there is none, so this cannot
        # quietly produce a bundle with no ScummVM in it.
        if [ -n "${RW_BUNDLE_NO_BUILD:-}" ]; then
            log_warning "RW_BUNDLE_NO_BUILD set — staging the existing binary, not rebuilding"
        else
            check_prerequisites
            build_scummvm
            strip_binary
        fi
        stage_bundle "$BUNDLE_DIR"
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
        echo "  $0 --bundle <dir>           Build + stage into an offline bundle"
        echo ""
        echo "Commands:"
        echo "  all          - Clean, configure, build, and strip (default)"
        echo "  clean        - Clean build artifacts"
        echo "  configure    - Configure build system"
        echo "  build        - Compile ScummVM"
        echo "  strip        - Strip debug symbols"
        echo "  deploy       - Build + strip + deploy to device"
        echo "  set-default  - Build + strip + deploy + set as default boot app"
        echo "  bundle       - Build + strip + stage offline (use --bundle <dir>)"
        echo "  info         - Show build information"
        exit 1
        ;;
esac
_END_SECONDS=$(date +%s)
_ELAPSED=$((_END_SECONDS - _START_SECONDS))
log_success "$(printf 'ScummVM build script finished — total time: %dm%02ds' $((_ELAPSED / 60)) $((_ELAPSED % 60)))"
