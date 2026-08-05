#!/bin/bash
# build-xpad-module.sh - Cross-compile Xbox controller kernel modules for RoomWizard
# Run this script in WSL (Linux), from the usb_host/ directory or its parent.
#
# Prerequisites (install in WSL):
#   sudo apt-get install gcc-arm-linux-gnueabihf bc libssl-dev bison flex
#
# Produces: usb_host/modules/ff-memless.ko, joydev.ko, xpad.ko

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

KERNEL_VERSION="4.14.52"
KERNEL_DIR="linux-${KERNEL_VERSION}"
KERNEL_TARBALL="linux-${KERNEL_VERSION}.tar.xz"
KERNEL_URL="https://cdn.kernel.org/pub/linux/kernel/v4.x/${KERNEL_TARBALL}"
DEVICE_CONFIG="device_config"
MODULES_DIR="modules"
CROSS_COMPILE="arm-linux-gnueabihf-"
ARCH="arm"

echo "=============================================="
echo "  Xbox Controller Module Builder"
echo "  Target: Linux ${KERNEL_VERSION} / ARM (armv7l)"
echo "=============================================="

# --- Step 0: Check prerequisites ---
echo ""
echo "[0/8] Checking prerequisites..."
if ! command -v ${CROSS_COMPILE}gcc &>/dev/null; then
    echo "ERROR: ${CROSS_COMPILE}gcc not found. Install with:"
    echo "  sudo apt-get install gcc-arm-linux-gnueabihf"
    exit 1
fi
if ! command -v bc &>/dev/null; then
    echo "ERROR: bc not found. Install with: sudo apt-get install bc"
    exit 1
fi
if [ ! -f "$DEVICE_CONFIG" ]; then
    echo "ERROR: Device config not found at ${DEVICE_CONFIG}"
    echo "  Copy it from the device: scp root@192.168.50.73:/proc/config.gz device_config.gz"
    echo "  Then: gunzip -k device_config.gz && mv device_config.gz device_config"
    exit 1
fi
echo "  Cross compiler: $(${CROSS_COMPILE}gcc --version | head -1)"
echo "  Device config: ${DEVICE_CONFIG}"

# --- Step 1: Download kernel source ---
echo ""
echo "[1/8] Downloading kernel source..."
if [ -f "$KERNEL_TARBALL" ]; then
    echo "  Tarball already exists, skipping download."
else
    echo "  Downloading ${KERNEL_URL}..."
    wget -q --show-progress "$KERNEL_URL" -O "$KERNEL_TARBALL"
    echo "  Download complete."
fi

# --- Step 2: Extract kernel source ---
echo ""
echo "[2/8] Extracting kernel source..."
if [ -d "$KERNEL_DIR" ]; then
    echo "  Source directory already exists, skipping extraction."
else
    echo "  Extracting ${KERNEL_TARBALL}..."
    tar xf "$KERNEL_TARBALL"
    echo "  Extraction complete."
fi

# --- Step 3: Copy device config ---
echo ""
echo "[3/8] Copying device kernel config..."
cp "$DEVICE_CONFIG" "${KERNEL_DIR}/.config"
echo "  Copied ${DEVICE_CONFIG} -> ${KERNEL_DIR}/.config"

# --- Step 4: Modify config for Xbox controller modules ---
echo ""
echo "[4/8] Modifying kernel config for Xbox controller support..."

CONFIG_FILE="${KERNEL_DIR}/.config"

# Function to set a config option
set_config() {
    local key="$1"
    local value="$2"
    if grep -q "^${key}=" "$CONFIG_FILE"; then
        sed -i "s|^${key}=.*|${key}=${value}|" "$CONFIG_FILE"
        echo "  Updated: ${key}=${value}"
    elif grep -q "^# ${key} is not set" "$CONFIG_FILE"; then
        sed -i "s|^# ${key} is not set|${key}=${value}|" "$CONFIG_FILE"
        echo "  Enabled: ${key}=${value}"
    else
        echo "${key}=${value}" >> "$CONFIG_FILE"
        echo "  Added:   ${key}=${value}"
    fi
}

# Enable force-feedback memless support (module)
set_config "CONFIG_INPUT_FF_MEMLESS" "m"

# Enable joydev interface (module) - provides /dev/input/jsX
set_config "CONFIG_INPUT_JOYDEV" "m"

# Enable joystick subsystem (built-in, needed for xpad)
set_config "CONFIG_INPUT_JOYSTICK" "y"

# Enable Xbox gamepad driver (module)
set_config "CONFIG_JOYSTICK_XPAD" "m"

# Enable Xbox gamepad force feedback
set_config "CONFIG_JOYSTICK_XPAD_FF" "y"

# Enable Xbox gamepad LED support
set_config "CONFIG_JOYSTICK_XPAD_LEDS" "y"

# --- Step 5: Run olddefconfig ---
echo ""
echo "[5/8] Running olddefconfig to resolve dependencies..."
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE olddefconfig
echo "  Config resolved."

# Verify our options survived olddefconfig
echo "  Verifying config options:"
for opt in CONFIG_INPUT_FF_MEMLESS CONFIG_INPUT_JOYDEV CONFIG_INPUT_JOYSTICK CONFIG_JOYSTICK_XPAD CONFIG_JOYSTICK_XPAD_FF CONFIG_JOYSTICK_XPAD_LEDS; do
    val=$(grep "^${opt}=" "${KERNEL_DIR}/.config" || echo "NOT FOUND")
    echo "    $val"
done

# --- Step 6: Prepare modules ---
echo ""
echo "[6/8] Running modules_prepare..."
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE modules_prepare
echo "  Modules prepared."

# --- Step 7: Build the modules ---
echo ""
echo "[7/8] Building kernel modules..."

echo "  Building ff-memless.ko and joydev.ko (drivers/input/)..."
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE M=drivers/input modules
echo ""

echo "  Building xpad.ko (drivers/input/joystick/)..."
make -C "$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE M=drivers/input/joystick modules
echo ""

# --- Step 8: Collect and strip modules ---
echo ""
echo "[8/8] Collecting and stripping modules..."
mkdir -p "$MODULES_DIR"

# Find and copy the built .ko files
for mod in ff-memless joydev; do
    src="${KERNEL_DIR}/drivers/input/${mod}.ko"
    if [ -f "$src" ]; then
        cp "$src" "${MODULES_DIR}/"
        echo "  Copied: ${mod}.ko"
    else
        echo "  ERROR: ${src} not found!"
        exit 1
    fi
done

src="${KERNEL_DIR}/drivers/input/joystick/xpad.ko"
if [ -f "$src" ]; then
    cp "$src" "${MODULES_DIR}/"
    echo "  Copied: xpad.ko"
else
    echo "  ERROR: ${src} not found!"
    exit 1
fi

# Strip debug symbols to reduce size
echo "  Stripping debug symbols..."
for ko in "${MODULES_DIR}"/*.ko; do
    before=$(stat -c%s "$ko")
    ${CROSS_COMPILE}strip --strip-debug "$ko"
    after=$(stat -c%s "$ko")
    echo "    $(basename "$ko"): ${before} -> ${after} bytes"
done

echo ""
echo "=============================================="
echo "  Build complete! Modules in ${MODULES_DIR}/:"
ls -la "${MODULES_DIR}"/*.ko
echo ""
echo "  Next step: Run deploy-xpad.sh from PowerShell"
echo "=============================================="
