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
| `drivers/cy8ctmg120_ts/` | out-of-tree touch driver (`.c` + `Kbuild`), GPL-2.0-only, adapted from vanilla `cy8ctmg110_ts.c`; binds the unchanged `panjit_ts` DT node and names its input device `panjit_ts` |
| `build-modules.sh` | builds every `drivers/*/` with a `Kbuild` via `M=` against the tree `build-image.sh` left in WSL `$HOME`; `--out <dir>` receives the `.ko`, which loads only on our image |
| `build-bt-modules.sh` | builds the 18 Bluetooth modules (`BT=m` and dependencies) from a copy of the image's tree, so no p1 write; `kernel/patches-modules/*.patch` are applied here only and `build-image.sh` never reads them |
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

`.188`'s p1 keeps the images it has run beside `uImage-system`, as rollbacks and negative controls:
`.vendor` (`edc637ac…`), `.500ma` (`a1fd1af8…`, vendor plus the USB power patch), `.mod` (`17243454…`, the
same patched image without it), `.ours-nopanel` (`3713faf7…`, vendor DTB), `.panel-v1` (`8bd1e362…`, before the
fb-size and backlight patches) and `.disconnect` (`926896a5…`, before the ID-ground patch). The running
`uImage-system` is our image with every `kernel/patches/` patch and our panel DTB (`f3b446c6…`, no 500 mA
power patch).

## Bluetooth modules

`build-bt-modules.sh` makes 18 modules with `BT=m`: `bluetooth`, `btusb`, `btrtl`, `hidp`, `uhid`, `uinput`,
`ecdh_generic`, `af_alg`, `algif_hash`, `algif_skcipher` and the crypto set `cmac`, `ecb`, `sha256_generic`,
`hmac`, `drbg`. **Measured:** the relinked `vmlinux` is byte-identical to the image's and all 762 imported
CRCs match; on `.188` all load by `insmod` except `jitterentropy_rng` (`host not compliant with requirements:
2`, harmless — `drbg` loads without it). Loadable because `CONFIG_MODULES=y`, `CONFIG_MODULE_FORCE_LOAD=y`
and `CONFIG_MODULE_SIG` is unset. Nothing loads them at boot, and no script deploys them or the firmware:
both sit on `.188` only (`/lib/modules/4.14.52/bt/`, `/lib/firmware/rtl_bt/`).

The operator's dongle `0b05:1bf6` is a Realtek **RTL8761CU**: `btrtl` logs `hci_ver=0d hci_rev=000e
lmp_ver=0d lmp_subver=8761`, rom_version 1. Mainline knows the 8761CU from v6.19 (`ic_id_table`, lmp `0x8761`,
hci_rev `0x0e`) and has no `0b05:1bf6` in any tag (nearest `0b05:1bef`); 4.14's `btrtl` keys on `lmp_subver`
alone, which 8761A, 8761B and 8761CU share. Its class `e0-01-01` binds `btusb` generically with `driver_info
== 0`, so `BTUSB_REALTEK` is never taken without our patch. `patches-modules/btusb-asus-1bf6-realtek.patch`
adds the ID and `btrtl-rtl8761cu.patch` the chip (8761CU firmware is epatch v1, which 4.14 parses; project id
51; an unknown 8761 `hci_rev` is refused). With the firmware from `3rdparty/realtek/bluetooth/` (provenance and
md5s in its `README.md`) the 263-byte download succeeds: `hci_revision` goes `0x000e` to `0x7bf1`,
manufacturer 93. No MUSB DMA question stands in the way: A2DP is tens of KB/s, which PIO carries.

## What we patch, and why

| Patch | Without it | Found by |
|---|---|---|
| `patches/smsc911x-reset-pulse.patch` | no network: the DT marks the LAN9221 reset line active-high, and vanilla `smsc911x.c` requests it low and never releases it | function-size diff: the vendor probe adds `set(0); msleep(100); set(1)` |
| `patches/omapdss-honour-syncclk-active.patch` | `videomode_to_omap_video_timings` copies the pixel-data clock edge onto the sync edge, so DT cannot express this panel's sync-on-falling, data-on-rising | reading `dss/display.c`; changes nothing for a DT without `syncclk-active` |
| `dts/panel-dpi.sh` | no `/dev/fb0`: the vendor `/display` node names `sharp,lq070y3lg4a`, which no vanilla driver claims | disassembling the vendor `sharp_lq_*` driver (below) |
| `patches/omapfb-panel-dpi-data-lines.patch` | `panel_dpi_probe_of` never reads `data-lines`, so `omapdss_default_get_recommended_bpp` (`dss/display.c:47`) says 16 and fb0 is sized for one 16bpp frame | reading the source; `dts/panel-dpi.sh` sets `data-lines = <24>` |
| `patches/leds-pwm-dt-brightness.patch` | vanilla `led_pwm_add` starts every PWM LED at `LED_OFF`, so the backlight boots dark; the DTB already carries `brightness` (backlight 100, red/green 50, measured with `fdtget`) | `led_pwm_probe` function-size diff (below) |
| `patches/musb-release-mstandby-on-probe.patch` | with `USB_INVENTRA_DMA`, DMA dies at the first `usb-host recover`: the unbind's `omap2430_low_level_exit` sets `OTG_FORCESTDBY.ENABLEFORCE`, only `omap2430_runtime_resume` clears it, and the glue stays runtime-active across the rebind — the channel stays enabled with its count unmoved, the `dma` IRQ never fires, the audio writer blocks. **Booted — measured 2026-09-27 on `.188`**: after replug + `recover`, `0x480AB414 = 0`, `hw_ptr` advances ~49.5k frames/s, the `dma` IRQ keeps rising, and the operator heard it play | unpatched, after a recover: `0x480AB414 = 1`, DMA ch0 `CNTL 0x6a9 COUNT 4`; writing `0` there by hand completed the transfer within 1 s |
| `patches/musb-a-idle-disconnect.patch` | a clean unplug hits the `default:` arm of the `MUSB_INTR_DISCONNECT` switch (`unhandled DISCONNECT transition (a_idle)`), so `is_active` stays set and the child is never disconnected — no `USB disconnect` logged, and once the child goes the root hub's autosuspend loops on `musb_bus_suspend` `-EBUSY`. The patch adds `A_IDLE`/`A_WAIT_BCON` arms calling `musb_host_resume_root_hub()` + `musb_root_disconnect()`. **Booted 2026-09-28 on `.188`** (uImage md5 `926896a5…`): a hub pulled from the root port logs `USB disconnect` for every child, `1-1` leaves sysfs, and `unhandled DISCONNECT`/`musb_bus_suspend` count 0. Its replug miss (no `CONNECT`, mode `a_idle`) is gone with the patch below | reading `musb_core.c` against the gadget-side twin, which clears `is_active` |
| `patches/musb-omap2430-session-on-id-ground.patch` | `omap_musb_set_mailbox()` acts on `MUSB_ID_GROUND` and `MUSB_VBUS_OFF` only `if (musb->gadget_driver)`, always NULL here (`# CONFIG_USB_GADGET is not set`), so an adapter plug never sets `SESSION` and a cold port stays dead until a rebind ([§3.6](../SYSTEM_ANALYSIS.md#36-usb)). The patch drops the guard on both arms: ID-ground → `omap_control_usb_set_mode(HOST)` + `set_vbus(1)`, VBUS-off → `set_vbus(0)`. **Booted 2026-09-29 on `.188`** (uImage md5 `f3b446c6…`, every patch here; undo `uImage-system.disconnect`): adapter pulled with a hub and 6 devices → `status=4`, `set_vbus(0)`, all disconnect; replugged → `status=1`, `set_vbus(1)`, the hub enumerates 0.5 s later and all 7 are back within 3 s with no RESCAN, mode `a_host`. A hub swapped behind a seated adapter (no ID edge) re-enumerates too, mode staying `a_host`, which the previous image missed — why, **[inferred]**: the port now reaches a proper `a_host` | debugfs kprobes `p:rwmb omap2430_musb_mailbox status=%r0:u32` and `p:rwsv omap2430_musb_set_vbus on=%r1:u32` (no `dynamic_debug` on our image); `twl4030_usb` in `/proc/interrupts` ticks once per ID/VBUS edge |

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
DT property and applies it at probe, which vanilla hard-codes to `LED_OFF`. Still unexamined: `fb_find_logo`.

## Drivers still missing from our image

| Function | State | Route |
|---|---|---|
| Panel | **works — measured 2026-09-23 on `.188`**: at boot fb0 is `800x480` 32bpp with 1536000 B, the backlight comes up at 100 with Tux on the glass, and a full 32bpp frame of noise written to `/dev/fb0` fills the whole panel | `dts/panel-dpi.sh` and the two patches above |
| Touch | **works, with type-B multi-touch — measured 2026-09-23 on `.188`**: launcher paging, tile taps and Brick Breaker respond (operator); `ABS_X`/`ABS_Y` `0..4095`. Two MT slots — slot 0 is X1/Y1, slot 1 is X2/Y2, each active while the burst's count byte covers it — plus `INPUT_PROP_DIRECT`; the legacy `ABS_X`/`ABS_Y`/`BTN_TOUCH` follow slot 0. **Slots are reported by hand**, because `input_mt_sync_frame()`'s pointer emulation emits `BTN_TOUCH` *before* `ABS_X`/`ABS_Y`, the wrong order for `touch_input.c`. An evdev capture with two fingers (operator) showed both slots streaming with distinct tracking IDs; lifting the second finger ends only slot 1. The driver works around three chip behaviours ([§3.3](../SYSTEM_ANALYSIS.md#33-touch)), each verified by the operator on the panel 2026-09-23: a **count-0 burst with a real X1** is a finger joining or leaving, skipped (≤10 in a row), so adding a finger no longer releases the first; **X2's unresolved marker** holds the slot's last X on a one-finger frame, or 4095 if it has none — the blue dot now always shows; **the chip is reset after every two-finger touch** (`reset_after_mt`, default on, ~120 ms blind), without which later touches never resolve X2. **Two-finger positions come from the electrode profiles** ([§3.3](../SYSTEM_ANALYSIS.md#33-touch)), read in the same I2C transfer as the burst: `cy8_peaks()` takes the two tallest peaks per axis at their 3-point centroid, and `cy8_axis()` uses the chip's own value only where a peak confirms it to within one electrode (it can be stale), holds a fading end as a lifting finger, and flags ends within 2.5 electrodes as *near*. `cy8_pair()` then picks the corner pairing closest to each slot's velocity prediction, with peak-height mismatch as the tiebreak; a new scan is keyed on the source the frame uses (profile for two fingers, burst for one), and while an axis is *near* the prediction coasts on its velocity for up to `DR_MAX` scans. **Operator, 2026-09-24 on `.188`:** X and Y swaps track through the crossing, including a pause at the crossing; a two-finger hold never swaps; lifting either finger keeps the other in its slot. ⚠️ **Two inputs remain undecidable, not buggy:** both fingers landing within one 60 ms scan with equal pressure (a coin toss, and it lost in the logged case), and a path that *reverses* at the crossing (a D and a mirrored D), which coasting reads as a crossing. Parameters `debug` (one line per changed burst, with the pairing `k`, a `nx`/`ny` near flag, both slots and velocities), `regdump` + `regdump_len` (registers 0..`regdump_len`−1, up to 255, in 32-byte rows); read `dmesg`, not `/proc/kmsg`, which the device's log daemon also drains — a `dmesg -c` loop to a file survives a long capture. `ABS_PRESSURE` is deliberately absent. Out-of-tree `.ko` in `/lib/modules/4.14.52/extra/`, loaded at boot by `device-files/touch-module` (`S31`; written 2026-09-24, **not yet run on a unit**) only while I2C client `2-0003` has no driver, so on the vendor kernel, whose `panjit_ts` owns it, the script is a no-op; `build-modules.sh --deploy <ip>` installs it and reloads it. `rmmod` and a hand `insmod` with debug parameters still work for a debugging session | `drivers/cy8ctmg120_ts/` via `build-modules.sh`; handshake and register map in [§3.3](../SYSTEM_ANALYSIS.md#33-touch); `tools/i2c_touch_read.c` is the witness |
| USB host | **works with `USB_INVENTRA_DMA` — measured 2026-09-27 on `.188`**: the C-Media `0d8c:0014` dongle enumerates at boot as ALSA card 1, directly or behind the Terminus `1a40:0101` hub, and `aplay -D plughw:1,0` plays (operator heard it); the vendor-built `snd-usb-audio`/`xpad` modules load. **Why a backend must be chosen:** the vendor config sets neither `MUSB_PIO_ONLY` nor a DMA backend, so `omap2430_ops` has no `.dma_init` and `musb_core.c:2264-2268` fails the probe with `-ENODEV` — the same defect `enable-usb-host.sh` stubs over on the vendor kernel, whose addresses our image does not have (hence its `Struct verification failed`, harmless: it exits early once `usb1` exists). ⚠️ **Without `musb-release-mstandby-on-probe.patch` DMA streams until the first `usb-host recover`, then never again** (measured, `.188`): after unplug, replug and `recover` the IRQ freezes and the settings app hangs until ALSA's write timeout. ⚠️ **`usbcore.autosuspend=-1` stays on the cmdline as a defence.** Its failures were in `a_idle` during `recover` (measured on `.188`, 2026-09-27, same DMA image, only that parameter changed): at the default `2`, `recover` with hub + dongle lost the dongle or found it only on attempt 3, amid a 638/783-line `musb_bus_suspend … a_idle while active` burst; at `-1`, attempt 1 both times with no burst. On the ID-ground image one run at the default was clean (2026-09-29, n=1, emulated at runtime via `/sys/module/usbcore/parameters/autosuspend` and an adapter cycle, not booted with it): with HID devices and `xpad` bound hubs never suspend; a non-hub child such as the audio dongle keeps `power/control=on` and so its hub awake; an empty hub left 30 s suspended with `usb1` (mode `a_suspend`), and the dongle plugged into it woke it by remote wakeup, enumerated and played, with `musb_bus_suspend`/`unhandled DISCONNECT` at 0 throughout. Operator stress test, same day, n=1: a keyboard with its own hub and touchpad, the audio dongle, a USB-powered speaker, an Xbox 360 pad and a 2.4 GHz receiver all worked through one hub, all vanished when the hub was pulled and all returned when it was replugged, with no burst. The musb `mode` sysfs file can read `a_idle` while a device streams — not a health signal | `kernel/config-changes` |

Building `tools/i2c_touch_read.c`: `arm-linux-gnueabihf-gcc -O2 -static -o i2c_touch_read
i2c_touch_read.c`, then run `native_apps/check-arm-safe.sh` on the result, as for any device binary.
The device has no `i2cget`/`i2cdump` (measured 2026-09-21), hence the tool. ⚠️ **`scp` does not preserve the exec
bit**: `chmod +x` on the device, or the run dies with *Permission denied* into a redirect and reads as an
empty result. ⚠️ **Stop whatever owns the screen before an `--evdev` witness**: `app_launcher` may hold an
`EVIOCGRAB` on the touch node, and a grabbed device silences the witness exactly like a finger that never landed.
Building the drivers, after `build-image.sh`: `wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && kernel/build-modules.sh --out /mnt/c/work/rw-scratch/ko [--deploy <ip>]"`.
