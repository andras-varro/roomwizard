#!/bin/bash
# build-image.sh - Build a bootable uImage from the vanilla 4.14.52 tree for RoomWizard
# Run this script in WSL (Linux). The recipe and the reasoning behind each step are in
# kernel/README.md.
#
# Prerequisites: every host package this needs comes from ../setup-build-env.sh, the one
# home for them (the cross-compiler is its `core` group; mkimage and fdtput its `kmod` group,
# fdtput by way of the device-tree-compiler package).
#
# Produces: <out>/zImage-with-dtb and <out>/uImage-test. It never writes uImage-system:
# that file has exactly one writer, lib/rw-usbpower.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

KERNEL_VERSION="4.14.52"
KERNEL_TARBALL="${REPO_DIR}/usb_host/linux-${KERNEL_VERSION}.tar.xz"
DEVICE_CONFIG="${REPO_DIR}/usb_host/device_config"
PATCH_DIR="${SCRIPT_DIR}/patches"
CONFIG_CHANGES="${SCRIPT_DIR}/config-changes"
DTS_SCRIPT_DIR="${SCRIPT_DIR}/dts"
# The DTB appended to the vendor uImage-system, sliced out byte for byte: original.dtb
# is identical to it (both 67004 B). Gitignored, like device_config: neither is ours to publish.
VENDOR_DTB="${REPO_DIR}/usb_host/original.dtb"
VENDOR_DTB_MD5="2c2be9b54cc33635f17a2c6611239d58"
CROSS_COMPILE="arm-linux-gnueabihf-"
ARCH="arm"

WORK_DIR="${HOME}/rw-kbuild-image"
OUT_DIR=""
DTB=""
DTB_MODE="scripts"
REUSE=0

usage() {
    cat <<'USAGE'
Usage: kernel/build-image.sh [--work <dir>] [--out <dir>] [--vendor-dtb | --dtb <file>] [--reuse]

Extract a fresh 4.14.52 tree from usb_host/linux-4.14.52.tar.xz, apply kernel/patches/*.patch
in sorted order, configure from usb_host/device_config + kernel/config-changes + olddefconfig,
build zImage, append a DTB and wrap it with mkimage into uImage-test. Stage that under a NEW
name on p1.

The DTB, by default: a copy of usb_host/original.dtb (md5-checked) edited in place by every
kernel/dts/*.sh in sorted order, each run as `bash <script> <dtb>`.

  --work <dir>   Build tree parent, in the WSL filesystem (default: ~/rw-kbuild-image).
                 Refused under /mnt: a kernel build on DrvFs is many times slower.
  --out <dir>    Where zImage-with-dtb and uImage-test go (default: the work dir).
  --vendor-dtb   Append usb_host/original.dtb verbatim, md5-checked. The control arm.
  --dtb <file>   Append this DTB instead.
  --reuse        Keep an existing tree, applying only patches not yet in it. Fast rebuilds.
  -h, --help     This text.

Writes <work>/dropped-symbols.txt: every symbol =y/=m in device_config that the new
.config does not carry as =y/=m. Nothing in the repo is written. Timestamps are pinned
to the repo's last commit, so the same inputs give the same uImage.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --work)       shift; WORK_DIR="${1:?--work needs a directory}" ;;
        --out)        shift; OUT_DIR="${1:?--out needs a directory}" ;;
        --vendor-dtb) DTB_MODE="vendor" ;;
        --dtb)        shift; DTB="${1:?--dtb needs a file}"; DTB_MODE="file" ;;
        --reuse)      REUSE=1 ;;
        -h|--help)    usage; exit 0 ;;
        *)            echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done
OUT_DIR="${OUT_DIR:-$WORK_DIR}"
KERNEL_DIR="${WORK_DIR}/linux-${KERNEL_VERSION}"

echo "[0/6] Checking prerequisites..."
missing=0
for tool in "${CROSS_COMPILE}gcc" mkimage fdtput fdtget bc bison flex; do
    command -v "$tool" &>/dev/null || { echo "ERROR: ${tool} not found."; missing=1; }
done
if [ "$missing" -ne 0 ]; then
    echo "  Install them with: ./setup-build-env.sh --install-deps   (kmod group: mkimage, bc, bison, flex; its device-tree-compiler carries fdtput)"
    exit 1
fi
[ -f "$KERNEL_TARBALL" ] || { echo "ERROR: ${KERNEL_TARBALL} not found."; exit 1; }
# Both are gitignored vendor files, so a fresh clone has neither: they come off a unit.
if [ ! -f "$DEVICE_CONFIG" ]; then
    echo "ERROR: ${DEVICE_CONFIG} not found (gitignored; it is the unit's own /proc/config.gz)."
    echo "  In usb_host/: scp root@<ip>:/proc/config.gz device_config.gz && gunzip device_config.gz"
    exit 1
fi
if [ ! -f "$VENDOR_DTB" ]; then
    echo "ERROR: ${VENDOR_DTB} not found (gitignored; it is the vendor's own DTB)."
    echo "  It is the 67004-byte DTB appended to p1's uImage-system at offset 0x4eb788, sliced out"
    echo "  byte for byte (usb_host/README.md, 'Binary DTB Patching in uImage'); md5 ${VENDOR_DTB_MD5}."
    exit 1
fi
case "$(realpath -m "$WORK_DIR")" in
    /mnt/*) echo "ERROR: work dir ${WORK_DIR} is on DrvFs. Use a path under \$HOME."; exit 1 ;;
esac
mapfile -t PATCHES < <(find "$PATCH_DIR" -maxdepth 1 -name '*.patch' | sort)
[ "${#PATCHES[@]}" -gt 0 ] || { echo "ERROR: no patches in ${PATCH_DIR} (the image boots with no network without them)."; exit 1; }
mapfile -t DTS_SCRIPTS < <(find "$DTS_SCRIPT_DIR" -maxdepth 1 -name '*.sh' 2>/dev/null | sort)
md5() { md5sum "$1" | cut -d' ' -f1; }
[ "$(md5 "$VENDOR_DTB")" = "$VENDOR_DTB_MD5" ] || { echo "ERROR: ${VENDOR_DTB} is not the vendor DTB."; exit 1; }
# Pin every timestamp and build-identity string the image embeds, so the same inputs give the
# same bytes: the kernel's UTS version string (read literally), and mkimage's header time.
EPOCH=$(git -C "$REPO_DIR" log -1 --format=%ct 2>/dev/null || stat -c %Y "$KERNEL_TARBALL")
KBUILD_BUILD_TIMESTAMP="$(date -u -d "@${EPOCH}")"
export SOURCE_DATE_EPOCH="$EPOCH" KBUILD_BUILD_TIMESTAMP KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER=roomwizard KBUILD_BUILD_HOST=build
echo "  Cross compiler: $(${CROSS_COMPILE}gcc --version | head -1)"
echo "  Build epoch:    ${EPOCH} (${KBUILD_BUILD_TIMESTAMP})"

# First, because it takes a second and fails before a kernel build would. The base is the
# md5-checked vendor blob itself, so the DT scripts' edits are the only difference.
echo; echo "[1/6] Device tree (${DTB_MODE})..."
mkdir -p "$WORK_DIR"
case "$DTB_MODE" in
    vendor) DTB="$VENDOR_DTB"; DTB_SOURCE="vendor usb_host/original.dtb, verbatim" ;;
    file)   [ -f "$DTB" ] || { echo "ERROR: DTB ${DTB} not found."; exit 1; }
            DTB_SOURCE="--dtb ${DTB}" ;;
    scripts)
        DTB="${WORK_DIR}/roomwizard.dtb"
        cp "$VENDOR_DTB" "$DTB"
        for s in "${DTS_SCRIPTS[@]}"; do
            bash "$s" "$DTB" || { echo "ERROR: DT script $(basename "$s") failed."; exit 1; }
            echo "  Applied: $(basename "$s")"
        done
        DTB_SOURCE="original.dtb + ${#DTS_SCRIPTS[@]} DT script(s):$(for s in "${DTS_SCRIPTS[@]}"; do printf ' %s' "$(basename "$s")"; done)"
        ;;
esac

echo; echo "[2/6] Extracting kernel source..."
if [ "$REUSE" -eq 1 ] && [ -f "${KERNEL_DIR}/Makefile" ]; then
    echo "  --reuse: keeping ${KERNEL_DIR}"
else
    rm -rf "$KERNEL_DIR"
    tar xf "$KERNEL_TARBALL" -C "$WORK_DIR"
    echo "  Extracted into ${KERNEL_DIR}"
fi

# A patch counts as applied only when it reverses cleanly afterwards. Under --reuse a patch that
# already reverses is skipped; one that neither reverses nor applies was edited since, so refuse.
# ⚠️ The reverse probe needs --forward: `-R --batch` on an UNpatched tree prints "Unreversed patch
# detected! Ignoring -R." and exits 0 (measured), so every check here would pass vacuously.
echo; echo "[3/6] Applying ${#PATCHES[@]} patch(es) from ${PATCH_DIR}..."
for p in "${PATCHES[@]}"; do
    if patch -d "$KERNEL_DIR" -p1 -R --forward --dry-run -s < "$p" &>/dev/null; then
        if [ "$REUSE" -eq 0 ]; then echo "ERROR: $(basename "$p") reverses on a fresh tree."; exit 1; fi
        echo "  Already applied: $(basename "$p")"
        continue
    fi
    if ! patch -d "$KERNEL_DIR" -p1 --forward --batch --no-backup-if-mismatch < "$p"; then
        echo "ERROR: $(basename "$p") did not apply."
        [ "$REUSE" -eq 0 ] || echo "  Under --reuse a changed patch needs a fresh tree: drop --reuse."
        exit 1
    fi
    patch -d "$KERNEL_DIR" -p1 -R --forward --dry-run -s < "$p" >/dev/null \
        || { echo "ERROR: $(basename "$p") reported success but does not reverse."; exit 1; }
    echo "  Applied: $(basename "$p")"
done

echo; echo "[4/6] Configuring from device_config + olddefconfig..."
cp "$DEVICE_CONFIG" "${KERNEL_DIR}/.config"
# kernel/config-changes: `<verb> <SYMBOL> [value]`, the verb being scripts/config's.
while read -r verb sym val; do
    case "$verb" in ''|'#'*) continue ;; esac
    if [ "$verb" = set-str ]; then
        "${KERNEL_DIR}/scripts/config" --file "${KERNEL_DIR}/.config" --set-str "$sym" "$val"
    else
        "${KERNEL_DIR}/scripts/config" --file "${KERNEL_DIR}/.config" "--${verb}" "$sym"
    fi
    echo "  config-changes: ${verb} ${sym}${val:+ ${val}}"
done < "$CONFIG_CHANGES"
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE olddefconfig
# Asserted after olddefconfig, which drops an added symbol whose dependency is missing.
while read -r verb sym val; do
    case "$verb" in
        ''|'#'*) continue ;;
        enable)  want="CONFIG_${sym}=y" ;;
        disable) want="# CONFIG_${sym} is not set" ;;
        set-str) want="CONFIG_${sym}=\"${val}\"" ;;
        *)       echo "ERROR: config-changes: unknown verb ${verb}"; exit 1 ;;
    esac
    grep -qxF "$want" "${KERNEL_DIR}/.config" \
        || { echo "ERROR: .config lacks '${want}' after olddefconfig"; exit 1; }
done < "$CONFIG_CHANGES"
# Lowercase is legal in a symbol name (CONFIG_VFPv3), so the class is [A-Za-z0-9_].
sym_re='^CONFIG_[A-Za-z0-9_]+=[ym]$'
DROPPED="${WORK_DIR}/dropped-symbols.txt"
comm -23 <(grep -E "$sym_re" "$DEVICE_CONFIG" | cut -d= -f1 | sort -u) \
         <(grep -E "$sym_re" "${KERNEL_DIR}/.config" | cut -d= -f1 | sort -u) > "$DROPPED"
echo "  Dropped by olddefconfig ($(wc -l < "$DROPPED") symbols, saved to ${DROPPED}):"
sed 's/^/    /' "$DROPPED"

echo; echo "[5/6] Building zImage with -j$(nproc)..."
start=$SECONDS
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j"$(nproc)" zImage
echo "  zImage built in $((SECONDS - start)) s."

# -C none, never -C gzip: -C describes the blob handed over, and a zImage self-extracts.
echo; echo "[6/6] Appending DTB and wrapping with mkimage..."
mkdir -p "$OUT_DIR"
ZIMAGE="${KERNEL_DIR}/arch/arm/boot/zImage"
cat "$ZIMAGE" "$DTB" > "${OUT_DIR}/zImage-with-dtb"
mkimage -A arm -O linux -T kernel -C none -a 0x80008000 -e 0x80008000 -n '' \
        -d "${OUT_DIR}/zImage-with-dtb" "${OUT_DIR}/uImage-test"

echo; echo "  Image built. DTB source: ${DTB_SOURCE}"
echo "  md5sums:"
md5sum "$ZIMAGE" "$DTB" "${OUT_DIR}/uImage-test" | sed 's/^/    /'
echo "  Stage it on p1 under a NEW filename; never as uImage-system."
