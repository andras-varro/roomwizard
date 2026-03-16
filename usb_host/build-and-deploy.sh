#!/bin/bash
# build-and-deploy.sh — Build and deploy USB host mode + Xbox controller support
#
# Usage: ./build-and-deploy.sh <device_ip>
# Example: ./build-and-deploy.sh 192.168.50.73
#
# Prerequisites:
#   - SSH key-based auth to root@<device_ip>
#   - arm-linux-gnueabihf-gcc (install: sudo apt-get install gcc-arm-linux-gnueabihf)
#   - bc, libssl-dev, bison, flex (for kernel module build)
#   - python3 (for DTB patching)
#
# This script:
#   1. Cross-compiles devmem_write.c for ARM
#   2. Builds Xbox controller kernel modules (if not already built)
#   3. Patches DTB USB power budget (100mA → 500mA) if needed
#   4. Deploys everything to the device
#   5. Enables USB host mode (runtime kernel patch)
#   6. Loads Xbox controller modules
#   7. Installs boot persistence for both
#   8. Verifies everything is working
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_VERSION="4.14.52"

# --- Argument validation ---
if [ -z "$1" ]; then
    echo "Usage: $0 <device_ip>"
    echo "Example: $0 192.168.50.73"
    exit 1
fi
DEVICE_IP="$1"
DEVICE="root@$DEVICE_IP"

echo "============================================"
echo "  RoomWizard USB Host + Controller Setup"
echo "  Target: $DEVICE"
echo "============================================"
echo ""

# --- Step 0: Check prerequisites ---
echo "[0/9] Checking prerequisites..."

if ! command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
    echo "ERROR: arm-linux-gnueabihf-gcc not found."
    echo "Install: sudo apt-get install gcc-arm-linux-gnueabihf"
    exit 1
fi
echo "  ✓ arm-linux-gnueabihf-gcc found"

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found (needed for DTB patching)."
    exit 1
fi
echo "  ✓ python3 found"

if ! ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" "echo OK" >/dev/null 2>&1; then
    echo "ERROR: Cannot SSH to $DEVICE"
    echo "Check: network connectivity, SSH key auth, device is powered on"
    exit 1
fi
echo "  ✓ SSH to $DEVICE works"
echo ""

# --- Step 1: Cross-compile devmem_write ---
echo "[1/9] Cross-compiling devmem_write for ARM..."

if [ ! -f "$SCRIPT_DIR/devmem_write" ] || [ "$SCRIPT_DIR/devmem_write.c" -nt "$SCRIPT_DIR/devmem_write" ]; then
    arm-linux-gnueabihf-gcc -static -O2 -o "$SCRIPT_DIR/devmem_write" "$SCRIPT_DIR/devmem_write.c"
    echo "  Built: $(file "$SCRIPT_DIR/devmem_write")"
else
    echo "  Skipped (binary is up-to-date)"
fi
echo ""

# --- Step 2: Build Xbox controller kernel modules ---
echo "[2/9] Building Xbox controller kernel modules..."

MODULES_DIR="$SCRIPT_DIR/modules"
MODULES_NEEDED=true

if [ -f "$MODULES_DIR/ff-memless.ko" ] && [ -f "$MODULES_DIR/joydev.ko" ] && [ -f "$MODULES_DIR/xpad.ko" ]; then
    echo "  Modules already built in $MODULES_DIR/"
    MODULES_NEEDED=false
else
    # Check additional build prerequisites
    for tool in bc; do
        if ! command -v $tool >/dev/null 2>&1; then
            echo "ERROR: '$tool' not found. Install: sudo apt-get install $tool"
            exit 1
        fi
    done

    # Get device config if not present
    if [ ! -f "$SCRIPT_DIR/device_config" ]; then
        echo "  Fetching kernel config from device..."
        ssh "$DEVICE" "cat /proc/config.gz" > "$SCRIPT_DIR/device_config.gz"
        gunzip -f "$SCRIPT_DIR/device_config.gz"
        echo "  ✓ Saved device kernel config"
    fi

    # Run the module build script
    echo "  Building modules (this may take several minutes on first run)..."
    bash "$SCRIPT_DIR/build-xpad-module.sh"
fi

# Verify modules exist
for mod in ff-memless.ko joydev.ko xpad.ko; do
    if [ ! -f "$MODULES_DIR/$mod" ]; then
        echo "ERROR: Module $mod not found in $MODULES_DIR/"
        echo "  Run build-xpad-module.sh manually to debug."
        exit 1
    fi
done
echo "  ✓ All modules present: ff-memless.ko, joydev.ko, xpad.ko"
echo ""

# --- Step 3: Patch DTB USB power budget if needed ---
echo "[3/9] Checking USB power budget in device tree..."

DTB_NEEDS_REBOOT=false

# Read the current power value from the live device tree
POWER_HEX=$(ssh "$DEVICE" "hexdump -e '4/1 \"%02x\"' /proc/device-tree/ocp*/usb_otg_hs*/power 2>/dev/null" || echo "")

if [ "$POWER_HEX" = "000000fa" ]; then
    echo "  ✓ USB power budget already set to 250 (500mA) — no patch needed"
elif [ "$POWER_HEX" = "00000032" ]; then
    echo "  USB power budget is 50 (100mA) — patching to 250 (500mA)..."

    # Mount boot partition on device
    ssh "$DEVICE" "mkdir -p /tmp/bootpart && mount -t vfat /dev/mmcblk0p1 /tmp/bootpart 2>/dev/null || true"

    # Copy uImage to workstation
    UIMAGE_LOCAL="$SCRIPT_DIR/uImage-system"
    echo "  Copying uImage-system from device..."
    scp -q "$DEVICE:/tmp/bootpart/uImage-system" "$UIMAGE_LOCAL"

    # Run patch_dtb.py
    echo "  Running patch_dtb.py..."
    python3 "$SCRIPT_DIR/patch_dtb.py"

    if [ ! -f "$SCRIPT_DIR/uImage-system-patched" ]; then
        echo "ERROR: patch_dtb.py did not produce uImage-system-patched"
        exit 1
    fi

    # Backup original on device and deploy patched version
    echo "  Backing up original uImage on device..."
    ssh "$DEVICE" "cp /tmp/bootpart/uImage-system /tmp/bootpart/uImage-system.bak"

    echo "  Deploying patched uImage..."
    scp -q "$SCRIPT_DIR/uImage-system-patched" "$DEVICE:/tmp/bootpart/uImage-system"

    # Sync and unmount
    ssh "$DEVICE" "sync && umount /tmp/bootpart 2>/dev/null || true"
    echo "  ✓ Patched uImage deployed (power: 50 → 250, 100mA → 500mA)"
    echo "  NOTE: DTB change requires a reboot to take effect"
    DTB_NEEDS_REBOOT=true
else
    echo "  WARNING: Unexpected power value '$POWER_HEX' — skipping DTB patch"
    echo "  (Expected '00000032' for 100mA or '000000fa' for 500mA)"
fi
echo ""

# --- Step 4: Deploy USB host files ---
echo "[4/9] Deploying USB host files to device..."

scp -q "$SCRIPT_DIR/devmem_write"      "$DEVICE:/usr/local/bin/devmem_write"
echo "  ✓ devmem_write -> /usr/local/bin/devmem_write"

scp -q "$SCRIPT_DIR/enable-usb-host.sh" "$DEVICE:/usr/local/bin/enable-usb-host.sh"
echo "  ✓ enable-usb-host.sh -> /usr/local/bin/enable-usb-host.sh"

scp -q "$SCRIPT_DIR/usb-host-initd.sh"  "$DEVICE:/etc/init.d/usb-host"
echo "  ✓ usb-host-initd.sh -> /etc/init.d/usb-host"

ssh "$DEVICE" "chmod +x /usr/local/bin/devmem_write /usr/local/bin/enable-usb-host.sh /etc/init.d/usb-host"
echo "  ✓ Permissions set"
echo ""

# --- Step 5: Deploy Xbox controller modules ---
echo "[5/9] Deploying Xbox controller modules..."

ssh "$DEVICE" "mkdir -p /lib/modules/${KERNEL_VERSION}/extra"
scp -q "$MODULES_DIR/ff-memless.ko" "$MODULES_DIR/joydev.ko" "$MODULES_DIR/xpad.ko" \
    "$DEVICE:/lib/modules/${KERNEL_VERSION}/extra/"
echo "  ✓ Modules -> /lib/modules/${KERNEL_VERSION}/extra/"

scp -q "$SCRIPT_DIR/S89xpad-modules" "$DEVICE:/etc/init.d/S89xpad-modules"
ssh "$DEVICE" "chmod +x /etc/init.d/S89xpad-modules"
echo "  ✓ S89xpad-modules -> /etc/init.d/S89xpad-modules"

ssh "$DEVICE" "depmod -a ${KERNEL_VERSION} 2>/dev/null || true"
echo "  ✓ depmod complete"
echo ""

# --- Step 6: Load Xbox controller modules ---
echo "[6/9] Loading Xbox controller modules..."

ssh "$DEVICE" bash -s <<'LOAD_SCRIPT'
for mod in ff-memless joydev xpad; do
    if lsmod 2>/dev/null | grep -q "^${mod//-/_}"; then
        echo "  Already loaded: $mod"
    else
        insmod "/lib/modules/4.14.52/extra/${mod}.ko" 2>/dev/null || \
        insmod -f "/lib/modules/4.14.52/extra/${mod}.ko" 2>/dev/null || true
        echo "  Loaded: $mod"
    fi
done
LOAD_SCRIPT
echo ""

# --- Step 7: Enable USB host mode ---
echo "[7/9] Enabling USB host mode (runtime kernel patch)..."

ssh "$DEVICE" "/usr/local/bin/enable-usb-host.sh"
echo ""

# --- Step 8: Install boot persistence ---
echo "[8/9] Installing boot persistence..."

ssh "$DEVICE" "\
    ln -sf ../init.d/S89xpad-modules /etc/rc5.d/S89xpad-modules && \
    echo '  ✓ S89xpad-modules -> rc5.d (controller modules at boot)'; \
    ln -sf ../init.d/usb-host /etc/rc5.d/S90usb-host && \
    echo '  ✓ S90usb-host -> rc5.d (USB host mode at boot)'"
echo ""

# --- Step 9: Verify ---
echo "[9/9] Verifying..."
echo ""

ssh "$DEVICE" bash -s <<'VERIFY_SCRIPT'
echo "--- USB Bus ---"
if [ -d /sys/bus/usb/devices/usb1 ]; then
    echo "  USB host mode: ACTIVE"
else
    echo "  USB host mode: FAILED"
fi

echo ""
echo "--- USB Power Budget ---"
POWER=$(hexdump -e '4/1 "%02x"' /proc/device-tree/ocp*/usb_otg_hs*/power 2>/dev/null)
if [ "$POWER" = "000000fa" ]; then
    echo "  USB power budget: 500mA (0xfa) ✓"
elif [ "$POWER" = "00000032" ]; then
    echo "  USB power budget: 100mA (0x32) — NEEDS REBOOT for DTB patch to take effect"
else
    echo "  USB power budget: unknown ($POWER)"
fi

echo ""
echo "--- Loaded Modules ---"
lsmod 2>/dev/null | grep -E "^Module|xpad|joydev|ff_memless" || echo "  (none)"

echo ""
echo "--- USB Devices ---"
lsusb 2>/dev/null || echo "  (lsusb not available)"

echo ""
echo "--- Input Devices ---"
for ev in /dev/input/event*; do
    [ -e "$ev" ] || continue
    name=$(cat "/sys/class/input/$(basename "$ev")/device/name" 2>/dev/null)
    echo "  $ev: $name"
done

echo ""
echo "--- Boot Persistence ---"
ls -la /etc/rc5.d/S89xpad-modules 2>/dev/null && echo "  ✓ Controller modules: INSTALLED" || echo "  ✗ Controller modules: NOT INSTALLED"
ls -la /etc/rc5.d/S90usb-host 2>/dev/null && echo "  ✓ USB host mode: INSTALLED" || echo "  ✗ USB host mode: NOT INSTALLED"
VERIFY_SCRIPT

echo ""
echo "============================================"
echo "  Setup complete."
if [ "$DTB_NEEDS_REBOOT" = true ]; then
    echo ""
    echo "  ⚠  DTB was patched — REBOOT REQUIRED!"
    echo "  Run: ssh $DEVICE 'sync; reboot'"
    echo "  Wait ~60s, then re-run this script to"
    echo "  verify everything after reboot."
fi
echo ""
echo "  Connect USB devices via OTG adapter."
echo "  A powered USB hub is recommended."
echo "============================================"
