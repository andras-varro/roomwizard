#!/bin/bash
# Cross-compile alsa-lib 1.2.1.2 for the device's soft-float userspace — the headers
# and a libasound.so to LINK against.  Nothing built here is deployed: the device
# ships its own libasound.so.2 (same soname, same 1.2.1.2), and that is the runtime.
#
# Usage:
#   ./build-alsa-lib.sh            # build into arm-deps-softfp/ unless already there
#   ./build-alsa-lib.sh --force    # rebuild from scratch
#   ./build-alsa-lib.sh --prefix   # print the sysroot path, build nothing
#
# Output: arm-deps-softfp/usr/include/alsa/*.h and arm-deps-softfp/usr/lib/libasound.so*
# (DESTDIR layout of a --prefix=/usr install, so the .pc and .la files name the device's
# paths).  Callers compile with -I<prefix>/usr/include and link -L<prefix>/usr/lib -lasound.
#
# It lives here because native_apps/common is the layer every component links: the
# ALSA AudioOutDev goes into common/audio_out.c, which ScummVM links too, so both build
# scripts call this one.  Soft-float only — a hard-float binary cannot load the device's
# libasound at all (../SYSTEM_ANALYSIS.md#63-cross-compiled-dependencies-must-be-built-from-source).
#
# The version is pinned to the device's own copy, so a symbol our build resolves is one
# its runtime has.  Configure line and CFLAGS are the ones the 2026-09-27 probe was built
# with, which played on both cards on .188.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="$SCRIPT_DIR/arm-deps-softfp"

ALSA_VER=1.2.1.2
ALSA_TARBALL="alsa-lib-$ALSA_VER.tar.bz2"
ALSA_URL="https://www.alsa-project.org/files/pub/lib/$ALSA_TARBALL"
ALSA_SHA256=958e260e3673f1f6ff6b2d2c0df3fc2e469bea5b2957163ce96ce17f23e87943

TC=arm-linux-gnueabi
ARMFLAGS="-march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=softfp"

case "${1:-}" in
    --prefix) echo "$PREFIX"; exit 0 ;;
    --force)  rm -rf "${PREFIX:?}/usr" ;;
    "") ;;
    *) echo "Usage: $0 [--force | --prefix]"; exit 1 ;;
esac

# The artifact, not a stamp: a half-finished install leaves no libasound.so.
if [ -f "$PREFIX/usr/lib/libasound.so" ] && [ -f "$PREFIX/usr/include/alsa/asoundlib.h" ]; then
    echo "alsa-lib $ALSA_VER already built ($PREFIX/usr/lib/libasound.so)"
    exit 0
fi

if ! command -v "$TC-gcc" >/dev/null 2>&1; then
    echo "$TC-gcc not found. Install every host prerequisite with setup-build-env.sh, at the repo root."
    exit 1
fi

# The tarball is cached beside the output, so a --force rebuild needs no network.
mkdir -p "$PREFIX/src"
if [ ! -f "$PREFIX/src/$ALSA_TARBALL" ]; then
    echo "Downloading $ALSA_URL"
    wget -q -O "$PREFIX/src/$ALSA_TARBALL.part" "$ALSA_URL"
    mv "$PREFIX/src/$ALSA_TARBALL.part" "$PREFIX/src/$ALSA_TARBALL"
fi
echo "$ALSA_SHA256  $PREFIX/src/$ALSA_TARBALL" | sha256sum -c --quiet - || {
    echo "SHA-256 mismatch on $PREFIX/src/$ALSA_TARBALL — delete it and rerun"
    exit 1
}

# Build on the Linux side: a configure + make over /mnt/c DrvFs is several times slower.
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/alsa-lib-build.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT
LOG="$BUILD_DIR/build.log"
tar xjf "$PREFIX/src/$ALSA_TARBALL" -C "$BUILD_DIR"
cd "$BUILD_DIR/alsa-lib-$ALSA_VER"

echo "Building alsa-lib $ALSA_VER for $TC (soft-float)..."
# python, alisp, topology and ucm are features the device's copy is not asked for by
# anything we build, and alisp/python each pull in a host dependency that fails cross.
if ! { CC="$TC-gcc" CFLAGS="-O2 $ARMFLAGS" ./configure --host="$TC" --prefix=/usr \
            --disable-python --disable-alisp --disable-topology --disable-ucm \
            --with-configdir=/usr/share/alsa \
       && make -j"$(nproc)" \
       && make install DESTDIR="$PREFIX"; } >"$LOG" 2>&1; then
    tail -30 "$LOG"
    echo "alsa-lib build FAILED — the tail of the log is above"
    trap - EXIT
    echo "Build tree kept: $BUILD_DIR"
    exit 1
fi

# `file` says "ARM, EABI5" for hard-float too; the ABI tag is what tells them apart —
# a hard-float object carries Tag_ABI_VFP_args, a soft-float one carries none.
if "$TC-readelf" -A "$PREFIX/usr/lib/libasound.so" | grep -q 'Tag_ABI_VFP_args'; then
    echo "Built libasound.so is hard-float; the device cannot load it"
    exit 1
fi
echo "alsa-lib $ALSA_VER installed to $PREFIX/usr"
