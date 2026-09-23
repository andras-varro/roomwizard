#!/bin/bash
# build-modules.sh - Build every out-of-tree driver in kernel/drivers/ against OUR image's tree
# Run this script in WSL (Linux), after build-image.sh: the modules link against the
# Module.symvers and vermagic of ~/rw-kbuild-image, so a .ko built here loads on a unit that
# boots that image and on no other kernel. The vendor-kernel modules (xpad and friends) are
# built by usb_host/build-kernel-modules.sh instead, from the unpatched tree.
#
# Produces: <out>/<driver>.ko, one per kernel/drivers/<driver>/.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_DIR="${HOME}/rw-kbuild-image/linux-4.14.52"
STAGE_DIR="${HOME}/rw-kbuild-image/modules"
CROSS_COMPILE="arm-linux-gnueabihf-"
ARCH="arm"
OUT_DIR=""

usage() {
    cat <<'EOF'
Usage: build-modules.sh --out <dir>

Copies each kernel/drivers/<driver>/ into ~/rw-kbuild-image/modules (never builds on /mnt/c)
and builds it with M= against ~/rw-kbuild-image/linux-4.14.52, which build-image.sh must
already have built. Copies each resulting .ko to <dir>.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --out)     shift; OUT_DIR="${1:?--out needs a directory}" ;;
        -h|--help) usage; exit 0 ;;
        *)         echo "ERROR: unknown argument $1"; usage; exit 1 ;;
    esac
    shift
done
[ -n "$OUT_DIR" ] || { usage; exit 1; }

for f in .config Module.symvers vmlinux; do
    [ -f "${KERNEL_DIR}/${f}" ] || { echo "ERROR: ${KERNEL_DIR}/${f} missing; run build-image.sh first."; exit 1; }
done

mkdir -p "$OUT_DIR"
built=0
for src in "${SCRIPT_DIR}"/drivers/*/; do
    [ -f "${src}Kbuild" ] || continue
    name="$(basename "$src")"
    rm -rf "${STAGE_DIR:?}/${name}"
    mkdir -p "${STAGE_DIR}/${name}"
    cp "${src}"* "${STAGE_DIR}/${name}/"
    make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE M="${STAGE_DIR}/${name}" modules
    for ko in "${STAGE_DIR}/${name}"/*.ko; do
        cp "$ko" "$OUT_DIR/"
        echo "built: ${OUT_DIR}/$(basename "$ko")  md5 $(md5sum "$ko" | cut -d' ' -f1)"
        built=$((built + 1))
    done
done
[ "$built" -gt 0 ] || { echo "ERROR: no module was built (no kernel/drivers/*/Kbuild?)."; exit 1; }
