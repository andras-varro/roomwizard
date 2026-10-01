#!/bin/bash
# Cross-compile BlueALSA 4.3.1 (bluez-alsa) and the SBC codec it needs for the device's
# soft-float userspace — the bluealsa daemon, bluealsa-aplay and the two ALSA plugins —
# into a staging tree a deploy step can copy from.  A2DP with SBC only: the goal is
# the panel as an A2DP source to headphones.
#
# Usage:
#   ./build-bluealsa.sh            # build into staging-bluealsa/ unless already there
#   ./build-bluealsa.sh --force    # rebuild from scratch (the downloads stay cached)
#   ./build-bluealsa.sh --staging  # print the staging path, build nothing
#
# Output: staging-bluealsa/ is the DESTDIR of a --prefix=/usr --sysconfdir=/etc install:
#   staging-bluealsa/usr/bin/bluealsa, usr/bin/bluealsa-aplay
#   staging-bluealsa/usr/lib/alsa-lib/libasound_module_{pcm,ctl}_bluealsa.so
#   staging-bluealsa/etc/alsa/conf.d/20-bluealsa.conf     (defines pcm.bluealsa, ctl.bluealsa)
#   staging-bluealsa/etc/dbus-1/system.d/bluealsa.conf    (lets it own org.bluealsa)
# /usr/lib/alsa-lib is the plugin directory compiled into the device's own libasound, and
# /etc/alsa/conf.d is the first entry of the vendor alsa.conf's load hook.  Its own tree,
# not build-bluez.sh's staging/: that script empties staging/ whenever it rebuilds.
# The binaries are left UNSTRIPPED: check-arm-safe.sh cannot judge a stripped one.
#
# ── Where the headers and link libraries come from ───────────────────────────
#   glib, gio, dbus  build-bluez.sh's sysroot (Debian bullseye armel), linked DYNAMICALLY
#                    against the device's own 2.62 copies — same rule and same refusal as
#                    there: GLIB_VERSION_MAX_ALLOWED=2.62 makes a newer call a warning, and
#                    a warning refuses the build.
#   bluez            build-bluez.sh's staging/: headers, and libbluetooth.so.3, which the
#                    daemon links (hci_*, ba2str) and the bluetooth component deploys
#   alsa             ../native_apps/build-alsa-lib.sh — alsa-lib pinned to the device's
#                    1.2.1.2, so the plugins resolve nothing the vendor libasound lacks
#   sbc              built here, STATICALLY into bluealsa: the device has no libsbc
#
# ── What is switched off ─────────────────────────────────────────────────────
# Every codec but SBC (aac, aptx, ldac, mp3, msbc, opus, lc3 are all off by default and
# said here only so a newer release cannot flip one on), ofono, upower, systemd, the
# man pages and the test suite.  HFP/HSP are runtime profiles (-p), not configure flags.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS="$SCRIPT_DIR/arm-deps-softfp"
SYSROOT="$DEPS/sysroot"
SBC_PREFIX="$DEPS/sbc"
BLUEZ_STAGING="$SCRIPT_DIR/staging"
STAGING="$SCRIPT_DIR/staging-bluealsa"
ALSA_PREFIX="$SCRIPT_DIR/../native_apps/arm-deps-softfp/usr"

SBC_VER=2.0
SBC_TARBALL="sbc-$SBC_VER.tar.xz"
SBC_URL="https://www.kernel.org/pub/linux/bluetooth/$SBC_TARBALL"
SBC_SHA256=8f12368e1dbbf55e14536520473cfb338c84b392939cc9b64298360fd4a07992

BA_VER=4.3.1
BA_TARBALL="bluez-alsa-$BA_VER.tar.gz"
BA_URL="https://github.com/arkq/bluez-alsa/archive/refs/tags/v$BA_VER.tar.gz"
BA_SHA256=933fe898dfac21fdfeb5f4ffa685c2aa2db9c064d639170ac2652f156e956a2a

TC=arm-linux-gnueabi
ARMFLAGS="-march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=softfp"
GLIB_PIN="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_62 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_62"

case "${1:-}" in
    --staging) echo "$STAGING"; exit 0 ;;
    --force)   rm -rf "${STAGING:?}" "${SBC_PREFIX:?}" ;;
    "") ;;
    *) echo "Usage: $0 [--force | --staging]"; exit 1 ;;
esac

PLUGIN_DIR="$STAGING/usr/lib/alsa-lib"
ARTIFACTS=("$STAGING/usr/bin/bluealsa" "$STAGING/usr/bin/bluealsa-aplay"
           "$PLUGIN_DIR/libasound_module_pcm_bluealsa.so" "$PLUGIN_DIR/libasound_module_ctl_bluealsa.so")

# The artifacts, not a stamp: a half-finished install leaves one of them missing.
built=1
for f in "${ARTIFACTS[@]}"; do [ -f "$f" ] || built=0; done
if [ $built -eq 1 ]; then
    echo "BlueALSA $BA_VER already built ($STAGING)"
    exit 0
fi

for tool in "$TC-gcc" wget pkg-config autoreconf libtoolize gdbus-codegen; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool not found. Install every host prerequisite with setup-build-env.sh, at the repo root."
        exit 1
    }
done

# Both short-circuit on their own artifacts.
bash "$SCRIPT_DIR/build-bluez.sh"
bash "$SCRIPT_DIR/../native_apps/build-alsa-lib.sh"

fetch() {
    local url=$1 sum=$2 file
    file="$DEPS/src/$(basename "$url")"
    [ "$3" ] && file="$DEPS/src/$3"
    if [ ! -f "$file" ]; then
        echo "Downloading $url"
        wget -q -O "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "$sum  $file" | sha256sum -c --quiet - || {
        echo "SHA-256 mismatch on $file — delete it and rerun"
        exit 1
    }
}

mkdir -p "$DEPS/src"
fetch "$SBC_URL" "$SBC_SHA256"
fetch "$BA_URL" "$BA_SHA256" "$BA_TARBALL"

MULTIARCH="$SYSROOT/usr/lib/$TC"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$MULTIARCH/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_PATH=

# Build on the Linux side: a configure + make over /mnt/c DrvFs is several times slower.
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/bluealsa-build.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT
LOG="$BUILD_DIR/build.log"

fail() {
    tail -30 "$LOG"
    echo "$1 build FAILED — the tail of the log is above"
    trap - EXIT
    echo "Build tree kept: $BUILD_DIR"
    exit 1
}

# ── sbc: static, position-dependent, into a private prefix ──────────────────
if [ ! -f "$SBC_PREFIX/usr/lib/libsbc.a" ]; then
    echo "Building sbc $SBC_VER for $TC (soft-float, static)..."
    tar xJf "$DEPS/src/$SBC_TARBALL" -C "$BUILD_DIR"
    rm -rf "${SBC_PREFIX:?}"
    ( cd "$BUILD_DIR/sbc-$SBC_VER" \
      && CC="$TC-gcc" CFLAGS="-O2 $ARMFLAGS" \
         ./configure --host="$TC" --prefix=/usr --enable-static --disable-shared \
              --disable-tools --disable-tester \
      && make -j"$(nproc)" && make install DESTDIR="$SBC_PREFIX" ) >"$LOG" 2>&1 || fail sbc
fi

# ── bluez-alsa ──────────────────────────────────────────────────────────────
# The release tarball is a git snapshot: no configure until autoreconf makes one.
# ALSA, BlueZ and SBC are handed over as *_CFLAGS/*_LIBS rather than through pkg-config,
# whose single sysroot cannot serve three trees; the plugin and conf dirs are therefore
# given explicitly, since configure would otherwise ask pkg-config for them.
echo "Building BlueALSA $BA_VER for $TC (soft-float)..."
tar xzf "$DEPS/src/$BA_TARBALL" -C "$BUILD_DIR"
cd "$BUILD_DIR/bluez-alsa-$BA_VER"
rm -rf "${STAGING:?}"
if ! { autoreconf --install \
       && CC="$TC-gcc" \
          CFLAGS="-O2 $ARMFLAGS $GLIB_PIN" \
          LDFLAGS="-L$SYSROOT/lib/$TC -L$MULTIARCH -Wl,--allow-shlib-undefined" \
          ALSA_CFLAGS="-I$ALSA_PREFIX/include" ALSA_LIBS="-L$ALSA_PREFIX/lib -lasound" \
          BLUEZ_CFLAGS="-I$BLUEZ_STAGING/usr/include" BLUEZ_LIBS="-L$BLUEZ_STAGING/usr/lib -lbluetooth" \
          SBC_CFLAGS="-I$SBC_PREFIX/usr/include" SBC_LIBS="$SBC_PREFIX/usr/lib/libsbc.a" \
          ./configure --host="$TC" --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
            --with-alsaplugindir=/usr/lib/alsa-lib --with-alsaconfdir=/etc/alsa/conf.d \
            --with-dbusconfdir=/etc/dbus-1/system.d \
            --disable-aac --disable-aptx --disable-aptx_hd --disable-ldac --disable-mp3lame \
            --disable-mpg123 --disable-msbc --disable-opus --disable-lc3-swb --disable-lc3plus \
            --disable-faststream --disable-midi --disable-ofono --disable-upower --disable-systemd \
            --disable-manpages --disable-test --disable-cli --disable-rfcomm --disable-hcitop \
            --enable-aplay --disable-static \
       && make -j"$(nproc)" \
       && make install DESTDIR="$STAGING"; } >"$LOG" 2>&1; then
    fail BlueALSA
fi

# Every refusal below empties staging-bluealsa/: the short-circuit at the top trusts
# the artifacts' presence, so a refused build must not leave them behind.
refuse() {
    echo "$1"
    rm -rf "${STAGING:?}"
    exit 1
}

if grep -q 'Not available before' "$LOG"; then
    grep 'Not available before' "$LOG" | sort -u
    refuse "BlueALSA calls glib API newer than the device's 2.62 — refusing the build"
fi
rm -f "$PLUGIN_DIR"/*.la

for f in "${ARTIFACTS[@]}"; do
    [ -f "$f" ] || refuse "$f was not installed"
    if "$TC-readelf" -A "$f" | grep -q 'Tag_ABI_VFP_args'; then
        refuse "$f is hard-float; the device cannot load it"
    fi
done
# The plugins load into every ALSA client, so they may need only what the device ships
# under exactly these names: its own libasound and libdbus, and glibc.
for f in "$PLUGIN_DIR"/*.so; do
    extra=$("$TC-readelf" -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' \
            | grep -v -x -E 'libasound\.so\.2|libdbus-1\.so\.3|libc\.so\.6|libpthread\.so\.0|libm\.so\.6|librt\.so\.1|libdl\.so\.2|ld-linux\.so\.3' || true)
    [ -z "$extra" ] || refuse "$f needs a library the device lacks: $extra"
done
OBJDUMP="$TC-objdump" bash "$SCRIPT_DIR/../native_apps/check-arm-safe.sh" "${ARTIFACTS[@]}" \
    || refuse "ARM-safety check failed — the staged binaries contain a hardware divide"
echo "BlueALSA $BA_VER installed to $STAGING"
