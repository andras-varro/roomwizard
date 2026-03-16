# USB Host Mode for RoomWizard

## Quick Start

One command to enable USB host mode with game controller support:

```bash
./build-and-deploy.sh 192.168.50.73
```

This will:
1. Cross-compile the `devmem_write` tool and Xbox controller kernel modules
2. Patch the device tree USB power budget (100mA → 500mA) if needed
3. Deploy everything to the device
4. Enable USB host mode (runtime kernel patch)
5. Load game controller modules
6. Install boot persistence for both

## Prerequisites

- **Linux/WSL** with `arm-linux-gnueabihf-gcc` installed
- **Additional packages**: `bc`, `libssl-dev`, `bison`, `flex` (for kernel module build)
- **Python 3** (for DTB patching script)
- **SSH root access** to the RoomWizard (key-based auth recommended)
- **Micro USB OTG adapter** (Micro-B male → USB-A female)
- **Powered USB hub** (recommended — the port may not supply VBUS)

Install all prerequisites:
```bash
sudo apt-get install gcc-arm-linux-gnueabihf bc libssl-dev bison flex
```

## How It Works

### Problem 1: USB Host Mode Disabled

The OEM kernel (`4.14.52`) has a broken MUSB USB configuration: the driver expects DMA
(`CONFIG_MUSB_PIO_ONLY` is off), but no DMA backend is compiled in (`CONFIG_USB_INVENTRA_DMA`
and `CONFIG_USB_TI_CPPI41_DMA` are both off). This causes the MUSB driver to always fail with
`"DMA controller not set"` (`-ENODEV`).

### Solution 1: Runtime Memory Patching

Runtime `/dev/mem` patching of the `omap2430_ops` struct in kernel memory. We write noop stub
function pointers into the `dma_init` and `dma_exit` fields, forcing a PIO (Programmed I/O)
mode fallback. The patch is:

- **Non-destructive** — original kernel on disk is unchanged
- **Not persistent** — must be re-applied on each boot (handled by init script)
- **Idempotent** — safe to run multiple times

After patching, we rebind the MUSB driver, which re-probes successfully and registers a USB
host bus.

### Problem 2: Game Controllers Not Detected

Even with USB host mode working, game controllers (specifically Xbox 360 controllers,
`045e:028e`) appear in `lsusb` but do NOT create `/dev/input/event*` device nodes.

**Root cause**: The kernel was compiled with the entire joystick subsystem disabled:

```
# CONFIG_INPUT_JOYSTICK is not set   # No joystick drivers at all
# CONFIG_INPUT_JOYDEV is not set     # No /dev/input/jsX interface
# CONFIG_INPUT_FF_MEMLESS is not set # No force-feedback support
```

The Xbox 360 controller uses a **vendor-specific USB interface class** (`bInterfaceClass=ff`,
`bInterfaceSubClass=5d`, `bInterfaceProtocol=01`) — it is NOT standard HID. The `usbhid`
driver (the only HID driver in this kernel) only binds to devices with `bInterfaceClass=03`
(HID). Without the `xpad` kernel driver, no driver claims the controller.

### Solution 2: Cross-Compiled Kernel Modules

The kernel supports loadable modules (`CONFIG_MODULES=y`) with force loading
(`CONFIG_MODULE_FORCE_LOAD=y`) and no signature requirement (`CONFIG_MODULE_SIG` not set).
We cross-compile three kernel modules from the matching kernel source (4.14.52):

| Module | Config Option | Purpose |
|--------|--------------|---------|
| `ff-memless.ko` | `CONFIG_INPUT_FF_MEMLESS=m` | Force-feedback memless support (xpad dependency) |
| `joydev.ko` | `CONFIG_INPUT_JOYDEV=m` | Joystick device interface (`/dev/input/jsX`) |
| `xpad.ko` | `CONFIG_JOYSTICK_XPAD=m` | Xbox gamepad driver (Xbox, Xbox 360, Xbox One) |

After loading these modules, the controller appears as:
- `/dev/input/eventX` — evdev interface (used by `usb_test` and games)
- `/dev/input/jsX` — joystick interface
- Name: `Microsoft X-Box 360 pad`
- Capabilities: `EV_KEY` (buttons), `EV_ABS` (sticks/triggers), `EV_FF` (force feedback)

### Problem 3: USB Bus Power Budget Too Low

Even with USB host mode working and the xpad driver loaded, the Xbox 360 controller was
sometimes rejected with `"rejected 1 configuration due to insufficient available bus power"`
when connected **directly** (without a powered hub). The kernel's USB hub driver enforces
a power budget based on the MUSB controller's device tree `power` property.

**Root cause**: The device tree blob (DTB) embedded in `uImage-system` sets the MUSB
controller's power budget to only **100mA**:

```dts
usb_otg_hs@480ab000 {
    compatible = "ti,omap3-musb";
    ...
    power = <0x32>;   /* 50 × 2mA = 100mA — too low for Xbox 360 controller */
};
```

The MUSB driver interprets this value as "units of 2mA", so `power = <0x32>` (50 decimal)
means 100mA total bus power. The Xbox 360 controller requests 500mA via its USB
configuration descriptor (`bMaxPower = 250`, also in 2mA units).

### Solution 3: Binary DTB Patching in uImage

The DTB is **not** a standalone `.dtb` file — it's appended to the zImage kernel inside the
legacy uImage at offset `0x4eb788` (67,004 bytes). We binary-patch it in place:

1. **Find the DTB** inside `uImage-system` by scanning for the DTB magic (`0xd00dfeed`)
2. **Walk the DTB struct block** to locate the `power` property within the `usb_otg_hs` node
3. **Change the 4-byte value** from `0x00000032` (50 = 100mA) to `0x000000fa` (250 = 500mA)
4. **Recalculate uImage CRCs** (both data CRC and header CRC)
5. **Deploy** the patched uImage back to the FAT32 boot partition (`/dev/mmcblk0p1`)

The patch is:

- **Persistent** — survives reboots (written to the boot partition)
- **Reversible** — a backup (`uImage-system.bak`) is created before patching
- **Idempotent** — `patch_dtb.py` checks the current value and skips if already patched

#### DTB Location in uImage

```
uImage-system (5,225,796 bytes)
├── uImage header (64 bytes)
├── zImage kernel (5,158,728 bytes, uncompressed)
└── Appended DTB at offset 0x4eb788 (67,004 bytes)
    └── usb_otg_hs node
        └── power property at DTB internal offset 0xeb44
            → Absolute uImage offset: 0x4fa2cc
```

#### Power Budget Values

| `power` value | Hex | Bus power | Result |
|:---:|:---:|:---:|---|
| 50 (original) | `0x32` | 100mA | Xbox 360 controller **rejected** |
| 250 (patched) | `0xfa` | 500mA | Xbox 360 controller **accepted** |

---

## Step-by-Step Manual Playbook

For those who want to understand or do it manually.

### Step 1: Install cross-compiler and build tools

On your WSL/Linux workstation:

```bash
sudo apt-get install gcc-arm-linux-gnueabihf bc libssl-dev bison flex
```

### Step 2: Cross-compile devmem_write

```bash
cd usb_host
arm-linux-gnueabihf-gcc -static -O2 -o devmem_write devmem_write.c
```

### Step 3: Build Xbox controller kernel modules

```bash
bash build-xpad-module.sh
```

This downloads the Linux 4.14.52 kernel source, configures it with the device's config,
enables the joystick subsystem, and cross-compiles the three modules into `modules/`.

### Step 4: Copy files to device

```bash
DEVICE=192.168.50.73

# USB host files
scp devmem_write      root@$DEVICE:/usr/local/bin/devmem_write
scp enable-usb-host.sh root@$DEVICE:/usr/local/bin/enable-usb-host.sh
scp usb-host-initd.sh  root@$DEVICE:/etc/init.d/usb-host

# Xbox controller modules
ssh root@$DEVICE "mkdir -p /lib/modules/4.14.52/extra"
scp modules/*.ko root@$DEVICE:/lib/modules/4.14.52/extra/
scp S89xpad-modules root@$DEVICE:/etc/init.d/S89xpad-modules
ssh root@$DEVICE "chmod +x /usr/local/bin/devmem_write /usr/local/bin/enable-usb-host.sh /etc/init.d/usb-host /etc/init.d/S89xpad-modules"
```

### Step 5: Enable USB host mode and load modules

```bash
ssh root@$DEVICE /usr/local/bin/enable-usb-host.sh
ssh root@$DEVICE "depmod -a 4.14.52 && insmod /lib/modules/4.14.52/extra/ff-memless.ko && insmod /lib/modules/4.14.52/extra/joydev.ko && insmod /lib/modules/4.14.52/extra/xpad.ko"
```

### Step 6: Install boot persistence

```bash
ssh root@$DEVICE "ln -sf ../init.d/usb-host /etc/rc5.d/S90usb-host"
ssh root@$DEVICE "ln -sf ../init.d/S89xpad-modules /etc/rc5.d/S89xpad-modules"
```

### Step 7: Verify

```bash
ssh root@$DEVICE "lsusb && echo '' && lsmod && echo '' && cat /proc/bus/input/devices"
```

Expected: Xbox controller visible in lsusb, xpad/joydev/ff_memless in lsmod,
`Microsoft X-Box 360 pad` in input devices.

---

## Technical Details

### MUSB DMA Patching

#### Root Cause

The OEM kernel config (from `/proc/config.gz`) shows:

```
CONFIG_USB_MUSB_HDRC=y          # MUSB driver: enabled (built-in)
CONFIG_USB_MUSB_HOST=y          # MUSB host mode: enabled
CONFIG_USB_MUSB_OMAP2PLUS=y     # OMAP2+ glue layer: enabled
# CONFIG_MUSB_PIO_ONLY is not set      # PIO fallback: DISABLED
# CONFIG_USB_INVENTRA_DMA is not set   # DMA backend: MISSING
# CONFIG_USB_TI_CPPI41_DMA is not set  # Alt DMA backend: MISSING
```

#### What the Patch Does

Patches two function pointers in the `omap2430_ops` struct via `/dev/mem` `mmap()`:

| Field | Address (phys) | Before | After |
|-------|---------------|--------|-------|
| `dma_init` | `0x80851b58` | `0x00000000` (NULL) | `0xc01aa83c` (`noop_ret`) |
| `dma_exit` | `0x80851b5c` | `0x00000000` (NULL) | `0xc01aa838` (`noop`) |

#### Why mmap() Works but write() Doesn't

`CONFIG_STRICT_KERNEL_RWX=y` marks kernel `.data` pages as read-only in the linear mapping
used by `/dev/mem`'s `write()` syscall. However, `mmap()` creates **new page table entries**
in userspace that map the same physical pages with `PROT_READ | PROT_WRITE`, bypassing the
kernel's read-only protection.

#### Memory Addresses

These are specific to OEM kernel `4.14.52` (built by `oe-user@oe-host`, 2018-06-26).

| Symbol | Virtual | Physical | Notes |
|--------|---------|----------|-------|
| `omap2430_ops` | `0xc0851b10` | `0x80851b10` | Struct base |
| `.dma_init` | `0xc0851b58` | `0x80851b58` | Offset +0x48 |
| `.dma_exit` | `0xc0851b5c` | `0x80851b5c` | Offset +0x4c |
| `noop_ret` | `0xc01aa83c` | — | Returns 0 |
| `noop` | `0xc01aa838` | — | Void return |

### Xbox Controller Kernel Module Details

#### Why the Controller Needs a Special Driver

The Xbox 360 controller (`045e:028e`) identifies itself as:
- `bDeviceClass=ff` (vendor-specific)
- `bInterfaceClass=ff` (vendor-specific)
- `bInterfaceSubClass=5d`
- `bInterfaceProtocol=01`

Standard USB HID devices use `bInterfaceClass=03`. The `usbhid` driver only binds to class
`03` devices, so it ignores the Xbox controller entirely. The `xpad` driver has a specific
USB device ID table that matches `045e:028e` regardless of the interface class.

#### Kernel Module Loading

The kernel has zero built-in loadable modules (the `/lib/modules/4.14.52/` directory was
empty). Key module-related config:

```
CONFIG_MODULES=y              # Loadable modules: ENABLED
CONFIG_MODULE_FORCE_LOAD=y    # Force loading despite version mismatch: ENABLED
CONFIG_MODULE_FORCE_UNLOAD=y  # Force unloading: ENABLED
CONFIG_MODVERSIONS=y          # CRC versioning: ENABLED (but force-load bypasses)
# CONFIG_MODULE_SIG is not set # Module signing: NOT REQUIRED
```

Modules are loaded with `insmod` (direct path) rather than `modprobe` since there's no
pre-existing `modules.dep` database. Boot persistence uses an init script that runs
`insmod` for each module in dependency order.

#### Boot Sequence

```
S89xpad-modules  →  loads ff-memless.ko, joydev.ko, xpad.ko
S90usb-host      →  patches MUSB DMA, rebinds driver, USB bus comes up
                     xpad driver auto-binds to any Xbox controllers found
```

### Key Kernel Config Values

| Config | Value | Significance |
|--------|-------|-------------|
| `CONFIG_USB_MUSB_HDRC` | `=y` | MUSB driver built-in |
| `CONFIG_USB_MUSB_HOST` | `=y` | Host mode enabled |
| `CONFIG_MUSB_PIO_ONLY` | not set | PIO fallback disabled (**THE BUG**) |
| `CONFIG_USB_INVENTRA_DMA` | not set | DMA backend missing (**THE BUG**) |
| `CONFIG_INPUT_JOYSTICK` | not set | Joystick subsystem missing (**FIXED**) |
| `CONFIG_INPUT_JOYDEV` | not set | Joystick device interface missing (**FIXED**) |
| `CONFIG_INPUT_FF_MEMLESS` | not set | Force-feedback missing (**FIXED**) |
| `CONFIG_MODULES` | `=y` | Loadable modules supported |
| `CONFIG_MODULE_FORCE_LOAD` | `=y` | Force-load despite CRC mismatch |
| `CONFIG_MODULE_SIG` | not set | No signature verification |
| `CONFIG_DEVMEM` | `=y` | `/dev/mem` available |
| `CONFIG_STRICT_DEVMEM` | not set | No memory access restrictions |
| `CONFIG_STRICT_KERNEL_RWX` | `=y` | Kernel pages read-only (blocks `write()`) |
| `CONFIG_HID_GENERIC` | `=y` | Generic HID driver (doesn't help Xbox) |
| `CONFIG_USB_HID` | `=y` | USB HID transport (class 03 only) |

### omap2430_ops Struct Layout

Dumped from physical address `0x80851b10` (128 bytes):

```
+0x00: 00000004  quirks = MUSB_DMA_INVENTRA
+0x04: c05d01dc  init = omap2430_musb_init
+0x08: c05d00cc  exit = omap2430_musb_exit
+0x0c: c05cfe80  enable = omap2430_musb_enable
+0x10: c05cfe5c  disable = omap2430_musb_disable
+0x14: 00000000  (fifo_mode / ep_offset / ep_select etc.)
...
+0x48: 00000000  dma_init = NULL  ← PATCHED to 0xc01aa83c (noop_ret)
+0x4c: 00000000  dma_exit = NULL  ← PATCHED to 0xc01aa838 (noop)
...
+0x60: c05cfca4  set_vbus
+0x70: c05d0698  clear_ep_rxintr
+0x78: "omap2430" (name string)
```

### Why DTB `dr_mode` Modification Alone Doesn't Fix USB Host

DTB changes (e.g., setting `dr_mode = "host"`) were investigated but don't solve the
DMA problem. The root cause is in the kernel's compiled-in code path, not the device tree
configuration. The `omap2430_ops.dma_init` pointer is NULL at compile time because
`CONFIG_USB_INVENTRA_DMA` is not set — no device tree change can populate it.

However, the DTB `power` property **does** need to be patched (see Problem 3 above) to
raise the USB bus power budget from 100mA to 500mA for direct controller connections.

### DTB Patching Details

#### Boot Partition Layout

The RoomWizard boots from a FAT32 partition (`/dev/mmcblk0p1`) containing:

| File | Size | Purpose |
|------|------|---------|
| `mlo` | 50 KB | First-stage bootloader (MLO/SPL) |
| `u-boot.bin` | 468 KB | U-Boot bootloader |
| `u-boot-sd.bin` | 468 KB | U-Boot for SD boot |
| `uImage-system` | 5.0 MB | Kernel + appended DTB (legacy uImage) |
| `uImage-bootstrap` | 5.1 MB | Bootstrap/recovery kernel |
| `ramfilesys.gz` | 11.3 MB | Initial ramdisk |

U-Boot loads `uImage-system` to `0x80008000`. The kernel self-extracts and finds the
appended DTB using the `ARM_ATAG_DTB_COMPAT` mechanism (the DTB starts with magic
`0xd00dfeed` immediately after the kernel image).

#### DTB Binary Structure

The DTB (Flattened Device Tree) is a binary format with three main sections:

1. **Header** (40 bytes) — magic, sizes, offsets
2. **Structure block** — tree of nodes and properties encoded as tokens
3. **Strings block** — property name strings (deduplicated)

Property lookup requires walking the structure block token-by-token:
- `FDT_BEGIN_NODE` (0x01) — start of a node, followed by name
- `FDT_PROP` (0x03) — property, followed by length + strings-block offset + data
- `FDT_END_NODE` (0x02) — end of a node

The `patch_dtb.py` script walks this structure to find the `power` property specifically
within the `usb_otg_hs` node, avoiding false matches in other nodes.

#### uImage CRC Recalculation

After patching the DTB bytes, the uImage checksums must be recalculated:

1. **Data CRC** (offset 24–27): CRC32 of all bytes after the 64-byte header
2. **Header CRC** (offset 4–7): CRC32 of the 64-byte header with CRC field zeroed

Without correct CRCs, U-Boot will refuse to boot the image.

---

## File Reference

| File | Runs On | Purpose |
|------|---------|---------|
| `build-and-deploy.sh` | Workstation (WSL/Linux) | End-to-end build and deploy automation |
| `build-xpad-module.sh` | Workstation (WSL/Linux) | Cross-compile Xbox controller kernel modules |
| `patch_dtb.py` | Workstation (Python 3) | Binary-patch MUSB power property in uImage DTB |
| `find_dtb.py` | Workstation (Python 3) | Extract DTB from uImage for inspection |
| `devmem_write.c` | Compiled for ARM | `/dev/mem` mmap-based read/write tool |
| `enable-usb-host.sh` | Device | Runtime kernel patch + MUSB driver rebind |
| `usb-host-initd.sh` | Device | SysV init.d wrapper for USB host boot persistence |
| `S89xpad-modules` | Device | SysV init.d script for loading controller modules at boot |

### Files Deployed to Device

| Device Path | Source | Purpose |
|-------------|--------|---------|
| `/usr/local/bin/devmem_write` | `devmem_write` (compiled ARM binary) | Physical memory read/write |
| `/usr/local/bin/enable-usb-host.sh` | `enable-usb-host.sh` | USB host mode enabler |
| `/etc/init.d/usb-host` | `usb-host-initd.sh` | Boot persistence for USB host |
| `/etc/rc5.d/S90usb-host` | Symlink → `../init.d/usb-host` | Runlevel 5 boot hook |
| `/lib/modules/4.14.52/extra/ff-memless.ko` | `modules/ff-memless.ko` | Force-feedback module |
| `/lib/modules/4.14.52/extra/joydev.ko` | `modules/joydev.ko` | Joystick device module |
| `/lib/modules/4.14.52/extra/xpad.ko` | `modules/xpad.ko` | Xbox controller driver |
| `/etc/init.d/S89xpad-modules` | `S89xpad-modules` | Boot persistence for modules |
| `/etc/rc5.d/S89xpad-modules` | Symlink → `../init.d/S89xpad-modules` | Runlevel 5 boot hook |
| `/dev/mmcblk0p1/uImage-system` | `uImage-system-patched` (via `patch_dtb.py`) | Kernel with patched DTB (500mA power) |

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| SSH connection fails | Device not on network | Check Ethernet, ping device IP |
| `devmem_write` won't compile | Missing cross-compiler | `sudo apt-get install gcc-arm-linux-gnueabihf` |
| Struct verification fails | Different kernel version | Re-derive addresses from `vmlinux` analysis |
| "DMA controller not set" in dmesg | Normal (from initial boot) | Check for `MUSB HDRC host driver` **after** patch |
| USB device not detected | OTG adapter issue or no VBUS | Use powered USB hub, check adapter polarity |
| Controller in lsusb but no /dev/input | xpad module not loaded | `insmod /lib/modules/4.14.52/extra/xpad.ko` |
| insmod fails "Invalid module format" | Version mismatch | Use `insmod -f` (force) — `MODULE_FORCE_LOAD=y` allows this |
| Module build fails | Missing build deps | `sudo apt-get install bc libssl-dev bison flex` |
| No event device after xpad loads | Controller unplugged during load | Replug controller, or unbind/rebind via sysfs |
| "rejected configuration due to insufficient bus power" | DTB power budget too low | Run `patch_dtb.py` to set power=250, redeploy uImage |
| Device won't boot after DTB patch | Corrupt uImage CRCs | Restore backup: mount p1, `cp uImage-system.bak uImage-system` |

---

## Failed Approaches (for reference)

| Approach | Why It Failed |
|----------|---------------|
| Vanilla kernel cross-compilation | Bricked device — missing OEM patches (LCD, touch, boot) |
| Binary patching of OEM zImage | Address relocation + inlining made patch point unfindable |
| `/dev/mem` via `write()` syscall | `CONFIG_STRICT_KERNEL_RWX` blocks write to kernel pages |
| DTB `dr_mode` modification only | Root cause of DMA failure is compiled-in code, not device tree |
| EHCI/OHCI alternative | `CONFIG_USB_EHCI_HCD` and `CONFIG_USB_OHCI_HCD` not compiled |
| usbhid for Xbox controller | Controller uses vendor-specific class `ff`, not HID class `03` |
| Runtime DTB power override via sysfs | MUSB power budget is read-only once driver initializes |

---

## Device Info

| Property | Value |
|----------|-------|
| Device | Steelcase RoomWizard II |
| SoC | TI AM335x (OMAP3 family), ARM Cortex-A8 |
| Kernel | Linux 4.14.52 |
| Build | `oe-user@oe-host` (Yocto/OpenEmbedded), 2018-06-26 |
| Boot | U-Boot → uImage from SD card FAT32 |
| USB Port | Micro-B OTG (single port) |
