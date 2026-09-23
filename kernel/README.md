# kernel/ — building our own 4.14.52 image

The working notes for the kernel we build ourselves: how the image is produced, what we patch and why,
the drivers still missing, and the methods that recovered what the vendor never published. **Device
facts found here move to `SYSTEM_ANALYSIS.md`**: the policy and the dropped-driver table are in
[§7](../SYSTEM_ANALYSIS.md#7-kernel-policy), the panel timings in
[§3.2](../SYSTEM_ANALYSIS.md#32-display), and the touch register map in
[§3.3](../SYSTEM_ANALYSIS.md#33-touch). Open work is in `IMPROVEMENT_PLAN.md`. Rules for editing this
directory are in [`CLAUDE.md`](CLAUDE.md).

| Path | What it is |
|---|---|
| `build-image.sh` | extract → patch → configure → `zImage` → DTB → `uImage`, all inside WSL; `--help` is current |
| `patches/*.patch` | kernel source patches, `patch -p1` from the tree root, GPL-2.0-only |
| `dts/*.sh` | scripts that edit the vendor DTB `usb_host/original.dtb` in place with `fdtput`, run as `bash <script> <dtb>` |
| `tools/i2c_touch_read.c` | userspace burst reader for the touch controller over `/dev/i2c-N`; it never writes to the part |

## Building an image

```bash
wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && kernel/build-image.sh --out /mnt/c/work/rw-scratch/img"
```

The script extracts a fresh tree from `usb_host/linux-4.14.52.tar.xz` into WSL `$HOME` and applies
`patches/`. It configures from the device's own `usb_host/device_config` with `olddefconfig`, then
writes `dropped-symbols.txt`. It builds `zImage` (about 1.5 min fresh; `--reuse` is incremental) and
appends a DTB, then wraps the result:

```
cat arch/arm/boot/zImage board.dtb > zImage-with-dtb
mkimage -A arm -O linux -T kernel -C none -a 0x80008000 -e 0x80008000 -n '' -d zImage-with-dtb uImage
```

- **The DTB, by default, is ours:** a copy of `usb_host/original.dtb`, edited in place by every
  `dts/*.sh` in sorted order. `--vendor-dtb` appends the vendor's own instead, byte for byte, as the
  control arm. The board has no separate `.dtb`; the vendor one is appended to `uImage-system`, and
  `usb_host/original.dtb` is that blob (md5 `2c2be9b5…`, measured equal). The edits are scripts rather
  than a diff because a diff against the decompiled DTS carries vendor text as context, and the vendor
  device tree is not ours to publish; a script carries only our values and looks every node and phandle
  up in the blob it is given.
- ⚠️ **`-C none`, never `-C gzip`.** `-C` describes the blob handed to `mkimage`, and a `zImage`
  already decompresses itself, so `CONFIG_KERNEL_GZIP` does not enter into it. **Measured** from byte
  31 of the vendor header. The recipe is proven by round trip: re-wrapping the vendor's *own* payload
  under `SOURCE_DATE_EPOCH=1529971593` reproduces `edc637ac14f90e0187b1ed65ffedf6d7` with zero
  differing bytes.
- `mkimage` and `fdtput` (package `device-tree-compiler`, probed as `dtc`) are in `setup-build-env.sh`'s `kmod` group. A clone without them can build
  modules but cannot package an image.
- ⚠️ **None of our DTBs carries the 500 mA USB power patch**, which `lib/rw-usbpower.sh` applies to
  the vendor image in place. An image of ours therefore boots with the stock USB current limit.

Installing the image on a unit follows the p1 rules in `lib/CLAUDE.md`. Take a backup of the running
`uImage-system`, copy the new one over it, and have the operator reboot. The undo is copying the
backup back, by SSH if the image answers or with a card reader if it does not.

## What we patch, and why

| Patch | Without it | Found by |
|---|---|---|
| `patches/smsc911x-reset-pulse.patch` | no network: the DT marks the LAN9221 reset line active-high, and vanilla `smsc911x.c` requests it low and never releases it | function-size diff: the vendor probe adds `set(0); msleep(100); set(1)` |
| `patches/omapdss-honour-syncclk-active.patch` | `videomode_to_omap_video_timings` copies the pixel-data clock edge onto the sync edge, so DT cannot express this panel's sync-on-falling, data-on-rising | reading `dss/display.c`; changes nothing for a DT without `syncclk-active` |
| `dts/panel-dpi.sh` | no `/dev/fb0`: the vendor `/display` node names `sharp,lq070y3lg4a`, which no vanilla driver claims | disassembling the vendor `sharp_lq_*` driver (below) |

**The panel patch, in detail.**
- `/display` becomes `compatible = "panel-dpi"` with `enable-gpios` = pwrdn and a `panel-timing` node
  carrying the vendor's numbers and polarity.
- The lvds and backlight lines are held high by `gpio-hog` nodes on gpio1.
- The LCD pinmux moves from `/display` to a pinctrl hog on its pad controller, so it applies whether or
  not a panel binds.
- ⚠️ **Booted and lit on `.188` (measured); the power sequence below has shown no visible fault yet.** It diverges from the vendor driver in two ways, both **inferred** from its
  disassembly. The vendor raises pwrdn → lvds → backlight only after DPI enable plus 50 ms; the hogs
  raise lvds and backlight at gpio probe, and `panel-dpi` adds no delay. And blanking can drop only
  pwrdn. If the panel starts badly, the fallback is a ~250-line clone of `panel-dpi` that drives all
  three lines with the vendor's delays.

## Reading the vendor kernel

The vendor source is not available, and that has not mattered. The vendor's `/proc/kallsyms` is
compared with our `System.map`, with each function's size taken as the distance to the next symbol.
The functions whose size differs, or that exist only in the vendor image, are what it patched. Each is
then disassembled from the
decompressed vendor `Image`, and its `bl` targets and literal-pool strings are resolved. It found the
Ethernet reset pulse. It also recovered the whole panel driver: timings, GPIO order, delays, and the
signal polarity that sysfs never exposed.

The tools are scratch-grade and live outside the repo, in `C:\work\rw-scratch`: `fsize.py` for sizes,
`calls.py <fn> <vendor_size> <our_size>` for the call-sequence diff, `dis.sh`, and `sharp_dis.py`. Their
data files are `vendor-Image` and `vendor.kallsyms`. `led_pwm_probe` is read: the vendor adds a u32 `brightness`
DT property to `struct led_pwm` and applies it at probe, which vanilla hard-codes to `LED_OFF` — not yet a
patch here, so the backlight boots dark. Still unexamined: `fb_find_logo`.

## Drivers still missing from our image

| Function | State | Route |
|---|---|---|
| Panel | **works — measured 2026-09-23 on `.188`**: `panel-dpi` binds, full-screen noise written to `/dev/fb0` is seen on the glass. Two defects remain, both open work: fb0 gets memory for one **16bpp** frame (770048 B), so a 32bpp app halves it to `800x240p-110` ([inferred] from source; hand fix measured: `fb0/overlays` detach, `size` 1536000, reattach), and the backlight boots at brightness 0 | `dts/panel-dpi.sh`, above |
| Touch | no driver: `CONFIG_TOUCHSCREEN_PANJIT` has no vanilla source | adapt `drivers/input/touchscreen/cy8ctmg110_ts.c`; register map in [§3.3](../SYSTEM_ANALYSIS.md#33-touch); `tools/i2c_touch_read.c` is the witness |
| USB host | `musb_init_controller failed with status -19` | not investigated; read `drivers/usb/musb/` for the `-ENODEV` returns |

Building `tools/i2c_touch_read.c`: `arm-linux-gnueabihf-gcc -O2 -static -o i2c_touch_read
i2c_touch_read.c`, then run `native_apps/check-arm-safe.sh` on the result, as for any device binary.
