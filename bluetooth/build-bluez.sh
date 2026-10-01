#!/bin/bash
# Cross-compile BlueZ 5.66 userspace for the device's soft-float userspace — bluetoothd,
# bluetoothctl and libbluetooth — into a staging tree a deploy step can copy from.
#
# Usage:
#   ./build-bluez.sh            # build into staging/ unless already there
#   ./build-bluez.sh --force    # rebuild from scratch (the downloads stay cached)
#   ./build-bluez.sh --staging  # print the staging path, build nothing
#
# Output: staging/ is the DESTDIR of a --prefix=/usr --sysconfdir=/etc install, so every
# path inside it is the device path:
#   staging/usr/libexec/bluetooth/bluetoothd
#   staging/usr/bin/bluetoothctl
#   staging/usr/lib/libbluetooth.so.3*
#   staging/etc/dbus-1/system.d/bluetooth.conf   (the policy that lets it own org.bluez)
# The binaries are left UNSTRIPPED: check-arm-safe.sh cannot judge a stripped one.
#
# ── Where the headers and link libraries come from ───────────────────────────
# Nothing but BlueZ is compiled.  The device already ships glib 2.62.6, libdbus 1.12
# and readline 8.0, so only their headers and something to link against are needed —
# and Debian bullseye's armel port is the same ABI (soft-float EABI5, /lib/ld-linux.so.3,
# glibc 2.31).  Its -dev and runtime .debs are unpacked into arm-deps-softfp/sysroot.
#   * glib: bullseye is 2.66, newer than the device.  glib has no symbol versioning,
#     so a call into 2.63+ API would link here and fail to resolve on the device.
#     GLIB_VERSION_MAX_ALLOWED=2.62 turns every such call into a "Not available
#     before" warning, and this script refuses the build if one appears.
#   * dbus 1.12.28 vs the device's 1.12: same series, versioned LIBDBUS_1_3 symbols.
#   * readline 8.1 vs the device's 8.0: same soname; bluetoothctl uses old API only.
# The bullseye runtime libraries' own dependencies (pcre, ffi, mount, tinfo6 ...) are
# not fetched; --allow-shlib-undefined lets the link go through without them.  They
# are the device's business at run time, where its own copies are what load.
#
# ── What is switched off, and why ────────────────────────────────────────────
#   udev, systemd, cups      the device has none of them (no libudev dev, SysVinit)
#   obex                     needs libical, absent on the device
#   mesh                     needs json-c and ell, absent on the device
#   midi                     needs libasound at build time; off by default, said explicitly
#   monitor, manpages        btmon and the man pages are not wanted on the panel
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS="$SCRIPT_DIR/arm-deps-softfp"
SYSROOT="$DEPS/sysroot"
STAGING="$SCRIPT_DIR/staging"

# 5.66 is Debian bookworm's BlueZ: the newest release with a distribution's worth of
# field use behind it, and new enough for the LE and mgmt fixes we want.
BLUEZ_VER=5.66
BLUEZ_TARBALL="bluez-$BLUEZ_VER.tar.xz"
BLUEZ_URL="https://www.kernel.org/pub/linux/bluetooth/$BLUEZ_TARBALL"
BLUEZ_SHA256=39fea64b590c9492984a0c27a89fc203e1cdc74866086efb8f4698677ab2b574

# archive.debian.org is frozen, so these URLs and digests do not move.
DEB_BASE="https://archive.debian.org/debian/pool/main"
DEBS=(
    "g/glib2.0/libglib2.0-dev_2.66.8-1+deb11u4_armel.deb d950dab1ba45ded9572cc7468864359cbd00a9b3791952aa4ef46981751b2a8d"
    "g/glib2.0/libglib2.0-0_2.66.8-1+deb11u4_armel.deb 2eb3cf5a1d630cd808f1e0f5b12bd49602979d5cf6979e1021e46681252041b9"
    "d/dbus/libdbus-1-dev_1.12.28-0+deb11u1_armel.deb 6671ace07fadc6b167eac8bfa30f682a8fb689ac0ebe4d8852585e3798b6d662"
    "d/dbus/libdbus-1-3_1.12.28-0+deb11u1_armel.deb 882462d0dd96087a62c450db58e78dd9a813265f6e721b278ec30665de7d8cf5"
    "r/readline/libreadline-dev_8.1-1_armel.deb 09b2d43bcf5a14d683ba277ec174e54543142368388379d16e7407ca8e4dc800"
    "r/readline/libreadline8_8.1-1_armel.deb 84ac170b08f1098032cf2cf0169ccbd4fdd1ce0a3be716599f6d22e648b5acd5"
)

TC=arm-linux-gnueabi
ARMFLAGS="-march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=softfp"
GLIB_PIN="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_62 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_62"

case "${1:-}" in
    --staging) echo "$STAGING"; exit 0 ;;
    --force)   rm -rf "${STAGING:?}" "${SYSROOT:?}" ;;
    "") ;;
    *) echo "Usage: $0 [--force | --staging]"; exit 1 ;;
esac

# The artifacts, not a stamp: a half-finished install leaves one of them missing.
if [ -f "$STAGING/usr/libexec/bluetooth/bluetoothd" ] && [ -f "$STAGING/usr/bin/bluetoothctl" ] \
   && [ -f "$STAGING/usr/lib/libbluetooth.so.3" ]; then
    echo "BlueZ $BLUEZ_VER already built ($STAGING)"
    exit 0
fi

for tool in "$TC-gcc" dpkg-deb wget pkg-config; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool not found. Install every host prerequisite with setup-build-env.sh, at the repo root."
        exit 1
    }
done

# fetch <url> <sha256> — cached in arm-deps-softfp/src, so a --force rebuild needs no network.
fetch() {
    local url=$1 sum=$2 file
    file="$DEPS/src/$(basename "$url")"
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
fetch "$BLUEZ_URL" "$BLUEZ_SHA256"

if [ ! -f "$SYSROOT/usr/include/glib-2.0/glib.h" ]; then
    rm -rf "${SYSROOT:?}"
    mkdir -p "$SYSROOT"
    for entry in "${DEBS[@]}"; do
        read -r path sum <<<"$entry"
        fetch "$DEB_BASE/$path" "$sum"
        dpkg-deb -x "$DEPS/src/$(basename "$path")" "$SYSROOT"
    done
    # Debian's -dev symlinks are absolute (/lib/arm-linux-gnueabi/libdbus-1.so.3); they
    # must point into the sysroot, not at the host.
    find "$SYSROOT" -type l | while read -r link; do
        target=$(readlink "$link")
        case "$target" in /*) ln -sfn "$SYSROOT$target" "$link" ;; esac
    done
    # pkg-config insists every Requires.private package exists even for a dynamic
    # link, and those (libpcre, libffi, zlib, mount, libselinux, tinfo) are exactly
    # the transitive dependencies not fetched — see the header.
    sed -i '/^Requires.private:/d' "$SYSROOT"/usr/lib/"$TC"/pkgconfig/*.pc
fi

MULTIARCH="$SYSROOT/usr/lib/$TC"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$MULTIARCH/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_PATH=

# Build on the Linux side: a configure + make over /mnt/c DrvFs is several times slower.
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/bluez-build.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT
LOG="$BUILD_DIR/build.log"
tar xJf "$DEPS/src/$BLUEZ_TARBALL" -C "$BUILD_DIR"
cd "$BUILD_DIR/bluez-$BLUEZ_VER"

echo "Building BlueZ $BLUEZ_VER for $TC (soft-float)..."
rm -rf "${STAGING:?}"
if ! { CC="$TC-gcc" \
       CFLAGS="-O2 $ARMFLAGS $GLIB_PIN" \
       CPPFLAGS="-I$SYSROOT/usr/include" \
       LDFLAGS="-L$SYSROOT/lib/$TC -L$MULTIARCH -Wl,--allow-shlib-undefined" \
       ./configure --host="$TC" --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
            --enable-library --disable-udev --disable-systemd --disable-cups \
            --disable-obex --disable-mesh --disable-midi --disable-manpages --disable-monitor \
       && make -j"$(nproc)" \
       && make install DESTDIR="$STAGING"; } >"$LOG" 2>&1; then
    tail -30 "$LOG"
    echo "BlueZ build FAILED — the tail of the log is above"
    trap - EXIT
    echo "Build tree kept: $BUILD_DIR"
    exit 1
fi

# A glib call newer than the device's 2.62 links fine here and fails on the device.
if grep -q 'Not available before' "$LOG"; then
    grep 'Not available before' "$LOG" | sort -u
    echo "BlueZ calls glib API newer than the device's 2.62 — refusing the build"
    rm -rf "${STAGING:?}"
    exit 1
fi

BINS=("$STAGING/usr/libexec/bluetooth/bluetoothd" "$STAGING/usr/bin/bluetoothctl")
# `file` says "ARM, EABI5" for hard-float too; the ABI tag is what tells them apart.
for f in "${BINS[@]}" "$STAGING/usr/lib/libbluetooth.so.3"; do
    if "$TC-readelf" -A "$f" | grep -q 'Tag_ABI_VFP_args'; then
        echo "$f is hard-float; the device cannot load it"
        exit 1
    fi
done
OBJDUMP="$TC-objdump" bash "$SCRIPT_DIR/../native_apps/check-arm-safe.sh" "${BINS[@]}" \
    "$STAGING/usr/lib/libbluetooth.so.3" || {
    echo "ARM-safety check failed — the staged binaries contain a hardware divide"
    exit 1
}
echo "BlueZ $BLUEZ_VER installed to $STAGING"
