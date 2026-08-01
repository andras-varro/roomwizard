# RoomWizard Device Reference

Everything known to be true about the Steelcase RoomWizard as a hardware and software platform:
the silicon, the board, the boot chain, the OS, and the traps. Verified on real units — where
something is inferred rather than measured, it says so.

**This document answers "what is true?".** It does not argue for changes. Open work, bugs and
feature proposals live in [`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md); the short must-know set
loaded into every session lives in [`CLAUDE.md`](CLAUDE.md). One fact, one home — if you find
something stated in two places, the other copy is the stale one.

Each subsystem below follows the same shape, so you can skim to the part you need:

| Field | Contains |
|---|---|
| **What's there** | parts, addresses, device nodes |
| **How to drive it** | the path, API or command that works today |
| **Gotchas** | what bites — stated next to the thing, not collected in a warnings chapter |
| **As shipped** | stock Steelcase behaviour, *only* where it explains the present |

**Provenance.** Software facts were read off live units (primarily RW09, `192.168.50.73`).
Hardware facts come from a full teardown on 2026-07-30 of the unit labelled `RW29 1G-093`, plus
on-device measurements across two further units. Photos: [`HardwarePhotos/`](HardwarePhotos/) —
see [Appendix A](#appendix-a-photo-index).

---

## Contents

1. [Read this first](#1-read-this-first)
2. [The board](#2-the-board)
3. [Subsystems](#3-subsystems)
4. [Boot chain and recovery](#4-boot-chain-and-recovery)
5. [Software stack](#5-software-stack)
6. [Building for this device](#6-building-for-this-device)
7. [Kernel policy](#7-kernel-policy)
8. [Appendix A: photo index](#appendix-a-photo-index)

---

## 1. Read this first

### What this device is

A wall-mounted meeting-room display repurposed as a games/apps platform. TI **OMAP3503** ARM
Cortex-A8 at 600 MHz, 234 MB RAM, 800×480 LCD, projected-capacitive touch, Linux 4.14.52 with
SysVinit, booting from a removable microSD card and powered over Ethernet. **No GPU and no DSP** —
all rendering is software. There is no local app to run: everything is cross-compiled on a host and
deployed over SSH.

### The rules that prevent a brick

Five rules. Observe them and there is no failure mode this device cannot recover from by itself.

1. **Never write `/dev/mtd*`.** The 12 KB NAND redirector in `mtd0` is the only irreplaceable
   non-SD component on the device, and the only thing that would make JTAG necessary.
2. **Never overwrite `mlo`, `u-boot.bin`, `u-boot-sd.bin` or `ctrlblock.bin`** on boot partition 1.
3. **Leave `uImage-system` alone.** Stage experimental kernels under a *different* filename.
   `bootcmd` is hardcoded to `uImage-system`, so a power cycle is a free undo.
4. **Keep a known-good SD image.** It is the entire failure surface and the entire recovery story.
5. **Feed the watchdog.** Any app owning the screen for long periods must keep `/dev/watchdog`
   fed (60 s) or the device hard-resets.

Follow these and the worst case is a card reflash. Full detail:
[Boot chain and recovery](#4-boot-chain-and-recovery).

### Physical safety

- **The unit is PoE-powered.** "Unplug it" means pulling the Ethernet cable — there is no other
  power lead and no barrel jack.
- **Up to 57 V sits on the RJ45 (`J3`) magnetics centre taps when powered.** Keep probes out of
  that corner. Everything downstream of the flyback is isolated and safe.
- **Releasing the bezel tears the touch flex if you don't disconnect `J2` first.** The touch glass
  bonds to the bezel; its flex lands on the board. They are tethered with no slack. This has
  already cost one unit its touchscreen.
- The board is 3.3 V logic — **no 5 V TTL adapters**. Note that the `P4` console header is behind a
  MAX3232 and is therefore at **RS-232** levels, not TTL. See [Serial ports](#312-serial-ports).

### If you have read an older version of this document

Five things it used to say that are now known to be wrong. Listed only because they may still be
in your head or quoted elsewhere — not as a changelog.

| Was stated | Actually |
|---|---|
| Visible area ~720×420, bezel hides 30–40 px on all edges | **~800×455** — bezel hides 10–15 px top and bottom *only* |
| Touch controller is a Panjit chip | Panjit is the **module** vendor; the silicon is a **Cypress CY8CTMG120** |
| Ambient light sensor probably present | **Absent, and impossible** — the enclosure has no aperture |
| `U17` is a coin cell / battery | A **0.47 F supercapacitor**, so RTC hold-up is hours, not months |
| Ethernet PHY is a separate LAN8700 | `LAN9221` is **MAC+PHY in one package** |

---

## 2. The board

### 2.1 Identification

| | |
|---|---|
| Board | Steelcase Inc **`550-0204-03`**, **© 2010**, `STM-5 STM-5E20784`, `94V-0`, `TESTED Compulrol` |
| Model string | `Steelcase RoomWizard 20` (`/proc/device-tree/model`) |
| Compatible | `ti,omap3-rw20`, `ti,omap3` |
| U-Boot identity | `soc=omap3`, `board=rw20` |
| OS build | `SteelCase RW20 Embedded Platform (Yocto) 3.1.4` — a Yocto build by eInfochips |
| Teardown unit | case label `RW29 1G-093`; asset labels `46837.0300`, `47270.0310` |

**The `© 2010` silkscreen is a design copyright, not a build date.** Three independent date codes
on the teardown unit cluster in March 2018: the LCD module (`W180322`), its T-CON board (`1810`)
and the RJ45 MagJack (`18111`). A panel swap would not also replace the Ethernet jack, so this is
a 2018-built unit on an eight-year-old board design. (The touch controller's `1043` code is
long-lived module stock, ordinary for a bonded touch assembly.)

### 2.2 Parts inventory

| Ref | Marking | Part |
|---|---|---|
| `U11` | `OMAP3503ECUS`, `72P19HQ`, `G1` | TI **OMAP3503** application processor |
| `U12` | `D9RMJ`, `70CI7 / XQ52` | Micron **mobile DDR SDRAM** (`D9RMJ` is Micron's FBGA code) |
| `U13` | `NQH53`, `7ME12 / X5Y3` | Micron **NAND flash** — MT29F2G16ABBEAHC, 256 MiB SLC, 16-bit, BCH8 |
| `U14` | `TPS65930A2`, `74AJKFW $4`, `G1` | TI **TPS65930** — PMIC + audio codec, **TWL4030 family** |
| `U15` | `LAN9221-ABZJ`, `A1751-AB24` | SMSC **LAN9221** Ethernet **MAC+PHY** on the GPMC bus |
| `U16` | `LVDS83B`, `77AK23K G4` | TI **SN65LVDS83B** parallel-RGB → LVDS transmitter |
| `U27` | `MA3232C`, `7AK G4`, `A1R7` | TI **MAX3232C** dual RS-232 transceiver |
| `U1` | `TPS23750`, `6BTG4 / A89N` | TI **802.3af PoE PD** interface + DC/DC controller |
| `U2` | `MT1107 V74968` | isolated feedback regulator (PoE secondary side) |
| `U4` | `PSS4325 / 68154 / C935` | **TPS54325**-class synchronous buck |
| `U17` | `⧠M` logo, `GC5.5V0.47F`, `JAPAN` | Panasonic/Matsushita **"Gold Cap" supercapacitor**, 5.5 V 0.47 F — RTC hold-up |
| `U22`, `U23` | — | Side LED bar drivers (`L8`/`L2` inductors adjacent) |
| `U24` | `CCH / TI 8A / Z86W` | TI, **unidentified** |
| `U25`, `U32` | `WP245 / TI 81K / CD4S` (16-pin) | TI, **unidentified**. `U25` by the touch flex, `U32` by `J5`/`LED1`–`4`. Position suggests level shifters. |
| `U33` | — | **Unpopulated** wide SOIC/SSOP land |
| `Q1`, `D1` | `4848 5BD` · `NHSTQW 3406` | PoE flyback FET / secondary rectifier |
| `SPKR1` | — | Single square metal-can magnetic speaker, ~20 mm |
| `LED1`–`LED5` | — | Discrete SMD LEDs; `LED1`–`4` in a row beside `J5` |
| — | Coilcraft **POE13F-12L** (`1809 J`) | PoE isolated flyback transformer |
| — | `C&K CHINA(9) EP11 0.4VA MAX 1744` | Small transformer near the touch flex — **purpose unconfirmed** |

### 2.3 Connectors

| Ref | Type | Goes to |
|---|---|---|
| `J1` | **microSD** push-push socket | Boot and root storage |
| `J2` | 8-pin white **FFC** | Touch controller flex — ⚠ release before separating the bezel |
| `J3` | TE **MagJack `1-6605834-1`** RJ45 | Ethernet **and PoE power in** |
| `J4` | **micro-USB** | The only USB connector (likely MUSB OTG) |
| `J5`, `J6` | 1×10 sockets, 2 mm pitch, **empty** | XBee radio site — [see below](#24-unpopulated-and-expansion) |
| `J7`, `J8` | 5-pin white **JST** | Side LED status bars in the left/right case edges |
| `P2` | Long fine-pitch 2-row, **unpopulated** | **Unknown** — the last unidentified footprint |
| `P3` | 2×7, 0.1", **unpopulated** | **TI-14 JTAG** (high confidence) |
| `P4` | 2×5, 0.1", **unpopulated** | **RS-232 console** via `U27` — verified |
| — | 40-pin **FFC** | Display panel harness |

`J5`/`J6` are silkscreened on the **bottom** face; `P3`/`P4` on the **top** face. They occupy the
same board area — `P3`/`P4`'s through-holes pass down the channel between the two sockets — which
is why both appear together in bottom-side photos.

**Case openings**, along the rear edge in order: RJ45, micro-USB, a second rectangular slot
(unidentified — nothing behind it, possibly a variant SKU), and a pinhole over a white tact
**reset button**.

### 2.4 Unpopulated and expansion

**`J5` + `J6` — an empty XBee socket.** Two 1×10 female strips at **2.0 mm pitch**, rows **~24 mm**
apart: the Digi XBee footprint (2 mm pitch, 22.86 mm row spacing). `J5` carries a white **pin-1
dot**, and the **metal inner bezel has a trapezoidal cut-out matching the XBee outline** — the
chassis was tooled for this module. A real XBee test-fits perfectly. **No radio is fitted in any of
three units**, so the batch shipped without the option; there is no antenna on the PCB because on
an XBee the antenna is part of the module. Vendor software confirms the intent — see
[Serial ports](#312-serial-ports).

**`P4` — the RS-232 console. Pinout verified by continuity:**

```
P4 pin 2  ->  U27 pin 14 (T1OUT)   console TX, RS-232 level, out of the device
P4 pin 3  ->  U27 pin 13 (R1IN)    console RX, RS-232 level, into the device
P4 pin 5  ->  U27 pin 15 (GND)     ground
```

Pin 1 is the square pad; even pins on the top row, odd on the bottom. Only these three are wired —
MAX3232 channel 1 only. Three wires, no soldering strictly required (a 0.1" female jumper or pogo
pins in the plated holes will do). **RS-232 levels: a 3.3 V TTL adapter will not work here** — use
a real USB↔DB9 adapter, or tap `U27`'s logic side instead.

**`P3` — TI-14 JTAG, high confidence.** Continuity against `U27` produces the TI-14 signature:

| `P3` pin | Measured | TI-14 expects | |
|---|---|---|---|
| 4 | GND (via `U16` pin 53) | GND | ✔ |
| 5 | 3.3 V (`U27` pin 16) | `PD` / Vref | ✔ |
| 6 | open | **keyed, no pin** | ✔ |
| 8 | GND | GND | ✔ |
| 10 | GND | GND | ✔ |
| 12 | open | GND | ✖ one discrepancy |

Vref on 5 with grounds on 4/8/10 plus the key at 6 is not an arrangement anything else uses. Pin 12
is the loose end — a no-connect on this board, or a missed probe. The remaining pins (TMS, nTRST,
TDI, TDO, RTCK, TCK, EMU0, EMU1) read open against `U27` because they run to the SoC. Not
actionable — the rules in §1 exist so that JTAG is never needed — but don't mistake it for a second
serial port.

**Test points: `TP1`–`TP59`**, individually labelled and probe-sized, over the whole top side.
Dense clusters at `TP19`–`TP31` (around the SD socket), `TP13`–`TP18` (mid-board), `TP34`–`TP36`
and a long run `TP39`–`TP59`. They are numbered, not named, so silkscreen alone tells you nothing
about the net. Mapping them by meter means probing back to `U14`, whose ADCIN pins are BGA-hidden.
**The practical route is the reverse:** power the unit, touch a resistor from 3.3 V to a candidate
test point, and watch which `in_voltage2..7_raw` moves. That needs no teardown.

**Room inside the case:** modest but real, around the `J5`/`J6`/`P3`/`P4` corner and near the
unpopulated `U33` and `P2` lands. Two constraints on anything added: the 802.3af power budget
([Power](#35-network-and-power)) and the fact that the case has **no ventilation slots**.

### 2.5 Enclosure and service

**Reopenable, non-destructively: screws and clips, no glue.** Four metal threaded bosses moulded
into the bezel, self-tapping screws for the LCD mount, plastic retention clips along the top and
bottom edges. Bezel material is `>PC/ABS<`, moulded part `560-0540-0x`. The only bonded joint is
touch glass to bezel, which is meant to be permanent.

**The one destructive risk is the touch flex.** Disconnect `J2` before separating the bezel from
the board — see [Physical safety](#physical-safety).

Removing **only the rear cover** exposes the entire bottom face — SoC, RAM, NAND, Ethernet, PoE,
`J5`/`J6` — without going near the bezel or the touch flex. That is the safe way to inspect a
working unit.

---

## 3. Subsystems

### 3.1 SoC, memory and storage

**What's there.** TI **OMAP3503 ES3.1.2**, GP (general-purpose, non-secure) silicon — the ARM-only
member of the OMAP34xx family. **No SGX530 GPU, no IVA2 DSP.** The DSS overlay hardware is the only
graphics acceleration that exists on this part.

```
$ cat /sys/devices/soc0/{family,machine,revision,type}
OMAP3 / OMAP3503 / ES3.1.2 / GP
$ dmesg | head -1
OMAP3503 ES3.1.2 (l2cache neon isp)
```

| | |
|---|---|
| CPU | ARM Cortex-A8 (part `0xc08`), 300 MHz base, **600 MHz** with cpufreq scaling active |
| BogoMIPS | 594.73 |
| Features | `half thumb fastmult vfp edsp thumbee neon vfpv3 tls vfpd32` |
| RAM | 239,904 KB (**234 MB**) usable of 256 MB; ~182 MB free in game mode |
| Swap | 258 MB, unused |
| Storage | microSD, 3.7 GB usable (SanDisk "Video Buffer" 4 GB, SDHC, UHS-I, **U3**) |
| NAND | 256 MB, **effectively unused** — see [Boot chain](#4-boot-chain-and-recovery) |

**Gotcha — this is an OMAP3, not an AM335x.** Worth stating because the two get confused and the
addresses differ throughout: GPMC `6e000000`; I2C1/2/3 `48070000`/`48072000`/`48060000`; UART2
`4806c000`; McBSP2 `49022000`; six GPIO banks (AM335x has four); MUSB glue `omap2430`; serial
naming `ttyO*` not `ttyS*`; companion PMIC TWL4030 (AM335x uses TPS65217).

**Gotcha — Cortex-A8 has no hardware integer divide.** This shapes every static build. See
[Building for this device](#6-building-for-this-device).

**The SD card is the entire failure surface**, and therefore the entire recovery story. "Video
Buffer" is SanDisk's OEM high-endurance line (dashcams, surveillance) — a sensible choice for
hardware that gets hard power-cycled, and worth matching when cloning to a larger card.

### 3.2 Display

**What's there.** Sharp **`LQ070Y3LG4A`** — 7" WVGA TFT, 800×480, LVDS input, LED backlight. The
vendor DTS `compatible = "sharp,lq070y3lg4a"` is the literal part number. Signal path:

```
OMAP3 DSS (24-bit RGB888 DPI)
  -> U16 SN65LVDS83B  (parallel RGB -> LVDS)
    -> 40-pin FFC -> discrete twisted-pair harness
      -> JAE ~30-position connector (only ~14-16 positions wired: 4 data pairs + clock + power)
        -> Sharp T-CON board "K5784TP"
             60153F00B0 timing controller  +  JRC A770 gamma buffer
          -> panel
```

The panel is a **separate, field-replaceable module** and Sharp was still producing it in 2018 — a
cracked screen is a repair, not a write-off.

**How to drive it.** `/dev/fb0`, memory-mapped, double-buffered. Legacy **omapfb + omapdss**:

```
$ cat /proc/fb            -> 0 omapfb / 1 omapfb   (CONFIG_FB_OMAP2_NUM_FBS=2; we only use fb0)
$ ls /dev/dri /sys/class/drm
(neither exists - there is no DRM/KMS on this device at all)
```

Backlight: `/sys/class/leds/backlight/brightness`, 0–100.

**Gotcha — bpp is per-app and set at runtime.** `/dev/fb0`'s format is whatever the running app
last selected via `FBIOPUT_VSCREENINFO`:

| App | bpp |
|---|---|
| `app_launcher` | **32bpp XRGB8888** — set on startup *and* after every child exits |
| `game_selector` | 32bpp, but only after a child exits |
| `device_tools`, `hardware_config`, `unified_calibrate` | **never set it** — open bug, see `IMPROVEMENT_PLAN.md` B1 |
| ScummVM, VNC session | **16bpp RGB565**, to halve write bandwidth |

`native_apps/common/framebuffer.c` writes `uint32_t` per pixel and is **32bpp-only**. When
screenshotting, decode at the bpp of whatever was running: `ssh root@<ip> cat /dev/fb0 > fb.raw`
then `fb565_to_png.py fb.raw fb.png` (defaults to 32bpp; `--bpp 16` for ScummVM/VNC). One 32bpp
frame is 800×480×4 = 1,536,000 bytes — coincidentally the same size as two 16bpp pages, which is
why a wrong-bpp decode looks size-correct while producing garbage.

**Visible area.** The bezel hides **10–15 px on the top and bottom edges only**, and effectively
nothing left or right. Measured on two devices.

Apps never deal with this. `fb_init()` shrinks the drawing surface to the visible rectangle and
`fb_swap()` places it on the panel at the viewport origin, leaving the hidden bands black — so
`fb.width`/`fb.height` and `SCREEN_SAFE_*` are the **logical** (fully visible) screen, 800×450 at
the shipped 15/15/0/0 margins. Margins come from `/etc/touch_calibration.conf` line 2, default
`FB_BEZEL_*_DEFAULT` = 15/15/0/0 (`native_apps/common/framebuffer.h`), and are set on-device from
**Device Tools → Set Screen → SCREEN EDGES**.

> **The constraint that actually binds interactive layout is digitizer reach, not the bezel** —
> and on this panel the two do not coincide. Touch is not reported in a band at the top and bottom
> of the *logical* surface, so keep interactive targets out of it; decoration can go to the edge.
> The measured figures live in [§3.3 Touch](#33-touch); do not restate them here.

**Panel timings — recovered, and worth preserving.** The device tree contains no timing block; the
timings were compiled into a vendor panel driver whose source does not exist in any tree we have.
omapdss exposes them at runtime, so they were read off a live device before anything could change:

```
$ cat /sys/devices/platform/omapdss/display0/timings
33230770,800/40/88/128,480/9/26/9
```

| Parameter | Value |
|---|---|
| Pixel clock | **33,230,770 Hz** |
| Horizontal | 800 active / 40 front / 88 back / 128 sync → **htotal 1056** |
| Vertical | 480 active / 9 front / 26 back / 9 sync → **vtotal 524** |
| Refresh | 33230770 / (1056 × 524) = **60.05 Hz** |
| Bus format | 24-bit RGB888 DPI (`data-lines = <24>`) |
| Panel GPIOs | pwrdn `gpio1[14]`, lvds `gpio1[15]`, backlight `gpio1[19]` — all active-high |

With these numbers the panel needs no custom driver on any kernel: it reduces to a stock
`panel-dpi` / `panel-simple` node with a `panel-timing {}` block. This was the single hardest
blocker to any future kernel work, and it now lives here rather than only inside a binary on a
2018 SD card.

**DSS overlay planes — present and unused.** Three overlays with an independent hardware scaler:

```
$ ls /sys/devices/platform/omapdss/
display0  manager0  manager1  overlay0  overlay1  overlay2

overlay0: name=gfx   enabled=1  manager=lcd  input_size=800,480  output_size=800,480  zorder=0  global_alpha=255
overlay1: name=vid1  enabled=0
overlay2: name=vid2  enabled=0
manager0: lcd  trans_key_enabled=0  alpha_blending_enabled=0
manager1: tv   display=<none>
/dev/video0 = omap_vout (V4L2 output)
```

Each overlay exposes `input_size`, `output_size`, `position`, `zorder`, `global_alpha` and
`pre_mult_alpha`; the manager exposes `trans_key_enabled` (colour keying) and
`alpha_blending_enabled`. **Because input and output sizes are independent, the DSS performs
arbitrary hardware scaling** — on a GPU-less 600 MHz part this is the only graphics acceleration
available, and nothing in the project uses it. (`omap_vout: failed to allocate DMA Channel for
video-1` appears at boot and is uninvestigated.) Proposal: `IMPROVEMENT_PLAN.md` F2.

> ⚠️ This is a **legacy omapdss sysfs** interface. It does not exist under `omapdrm`. Anything
> built on it is cheap today and would need rewriting as DRM atomic plane programming after a
> kernel jump — see [Kernel policy](#7-kernel-policy).

**As shipped.** X11/Xorg (`Xorg -br -nolisten tcp -nocursor -pn -dpms vt8 :0`, started by
`/etc/init.d/x11`) hosting a WebKit browser. All removed in game mode; apps render straight to the
framebuffer.

### 3.3 Touch

**What's there.** A **projected-capacitive** panel on I2C bus 2 at address `0x03`. The controller
silicon is a **Cypress `CY8CTMG120-56LTXI`** — a PSoC **TrueTouch Multi-Touch Gesture** chip, `IC1`
on an orange flex marked `EDT REV.A 40-0016-2` (56-QFN, date code `1043`).

> **"Panjit" is the touch-module vendor, not the chip vendor**, despite the kernel driver being
> called `panjit_ts`. This matters: the CY8CTMG120's I2C register map is published Cypress
> documentation, so reaching the controller directly is *implementing a documented protocol*, not
> reverse-engineering an unknown one.

**How to drive it.** evdev at `/dev/input/touchscreen0` (also `/dev/input/event0`). Driver
`panjit_ts` is out-of-tree. DT node `tsc_panjit@03`: reg `0x03`, IRQ `gpio1[23]` falling, reset
`gpio1[16]`.

**Gotcha — capture coordinates BEFORE the press event.** The event order is:

```
ABS_X  ->  ABS_Y  ->  BTN_TOUCH  ->  SYN_REPORT
```

Read the coordinates on `ABS_X`/`ABS_Y` and latch them; if you wait for `BTN_TOUCH` you get stale
values. Reference implementation: `native_apps/common/touch_input.c`.

**Coordinate model — two independent stages.** A raw reading becomes an app coordinate in two
steps, each owned by a different concern and configured on its own line of the config file:

| Stage | Maps | Owner | Config |
|---|---|---|---|
| 1. Calibration | raw digitiser → **panel** pixel | `scale_coordinates()` in `touch_input.c` | line 1 |
| 2. Bezel viewport | panel pixel → **logical** pixel | the same function, from the framebuffer globals | line 2 |

Stage 1 is per-axis linear over the whole 800×480 panel, bezel included. Raw is 12-bit (0–4095):

```c
panel_x = (raw_x - raw_min_x) * (panel_w - 1) / (raw_max_x - raw_min_x);
panel_y = (raw_y - raw_min_y) * (panel_h - 1) / (raw_max_y - raw_min_y);
```

The panel is linear — a traced border comes out a straight-edged rectangle, no keystone or shear —
so scale-and-offset per axis is sufficient. There is **no affine transform and no bilinear corner
correction**.

Stage 2 subtracts the viewport origin (`screen_view_x`/`screen_view_y`), which is exactly the
offset `fb_swap()` draws at. A touch on the bezel therefore clamps to the nearest logical edge, and
drawing and touch always share one coordinate system.

Keep the stages apart: calibration must be fitted against **panel** coordinates. Fitting it against
logical coordinates bakes the bezel into line 1, and stage 2 then subtracts it a second time.

**`/etc/touch_calibration.conf`**, loaded automatically by `touch_init()` and `fb_init()`:

- Line 1: `raw min_x max_x min_y max_y` — calibrated raw range, mapped linearly onto the whole panel.
- Line 2: `bezel top bottom left right` — panel pixels hidden by the bezel. Omit the line to get the
  compiled defaults (15/15/0/0).

**Device Tools → Set Screen** owns both, on separate buttons:

- `TOUCH CALIBRATION` (also the standalone `unified_calibrate`) taps 9 crosshairs (corners, edges,
  centre, inset 40 px), least-squares fits a line per axis in panel space, and extrapolates to the
  true panel edges so the inset targets still reach the corners. A summary shows target-vs-landing
  before saving. **Its 40 px inset is too small** — the corner and edge-mid crosshairs sit inside
  the compressed band, which is what produced the phantom X inset above. `IMPROVEMENT_PLAN.md` B3a.
- `SCREEN EDGES` draws a frame on the logical edge and steps each margin by 1 px, re-letterboxing
  live. Any part of the frame you cannot see means that margin is too small.

**`touch_raw`** (`native_apps/tests/touch_raw.c`, deployed to `/opt/games/`, hidden from the
launcher) is the diagnostic that settled reach, and the only tool that shows the panel with **no
calibration and no bezel**: it resets the raw range to the `EVIOCGABS` values and calls
`fb_set_bezel(fb,0,0,0,0)`, so a drawn pixel *is* a panel pixel and the dot is `raw × 799 / 4095`.
Two modes — a live crosshair with per-edge 10 px ladders, session extremes and a pin flag when raw
sticks at a hardware limit; and a capture pass (11 targets × 3 taps, then a hard press on each
bezel) that fits interior-only vs all-points and reports what each predicts at the panel edges.
Logs to `/tmp/touch_raw.tsv` with a monotonic millisecond column. It can write the interior fit to
line 1 after backing the file up to `.bakN`.

Caveat: its printed verdict is a **single global H1/H4 driven by the worst axis**, which is
misleading on this panel — read the per-axis table it prints, not the verdict line.
`IMPROVEMENT_PLAN.md` B3b.

> **ScummVM links its own copy of `touch_input.o`** — rebuild it after changing that file or its
> touch silently goes stale.

Accuracy: ~3 px at centre, 14–27 px error at the corners before calibration.

**Reach at the border — measured, and it differs per axis.** The controller hard-clamps raw at 0
and 4095 while the finger is still moving. *Where* that clamp lands on the glass was settled on
2026-07-31 with `touch_raw` (see below): fit each axis from **interior targets only**, then test
that fit against edge probes and against a finger pressed hard into the bezel. Raw capture:
[`touch_raw-2026-07-31-rw09.tsv`](touch_raw-2026-07-31-rw09.tsv).

| | X | Y |
|---|---|---|
| interior-only fit → raw at panel 0 | **+17** | **−279** |
| interior-only fit → raw at panel max | **4084** (of 799) | **4382** (of 479) |
| raw 0..4095 therefore covers panel | **−3 … 801** | **29 … 449** |
| bezel press, low edge | LEFT `raw 0` → panel **−3** | TOP `raw 22` → panel **31** |
| bezel press, high edge | RIGHT `raw 4095` → panel **801** | BOTTOM `raw 4095` → panel **449** |
| **inset** | **none** | **~30 px top and bottom** |

**X reaches both edges. Y is short by ~30 px at each end** (~5.7 mm at 5.25 px/mm). Two independent
methods — an interior fit extrapolated outward, and a fingertip pressed into the plastic — agree on
Y to within 2 px. Physically the digitizer spans about **153.4 × 80.1 mm** against an LCD active
area of 152.4 × 91.4 mm: the electrode array matches the width and is ~11 mm short of the height.

> **Touchable is smaller than visible, on Y only.** The logical surface is panel y 15…464; the
> touchable band is panel y 29…449. So **the top ~15 and bottom ~15 rows of the drawing surface
> cannot be touched**, while every column can. This is the one place where "the logical screen is
> the safe area" does not hold, and it is a property of the sensor, not of the bezel.

The earlier per-edge figures (~10 px left/right, ~25 top, ~30 bottom) were **wrong on X**. They came
from the 9-tap calibration, whose crosshairs are inset only 40 px — inside the compressed band — so
the fit slope came out too shallow and extrapolated to raw values outside 0..4095, inventing a
horizontal inset that varied from run to run. Fitting from interior targets only removes it, and
recovers the full 800 px width on a config change alone. Remaining loose end: the x=780 probe reads
+6 px high, so raw does bunch a little in the last ~20 px on the right and probably pins near panel
795 rather than 801 — a handful of pixels, not measured precisely. Open work:
[`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md) B3a/B3b.

**Multi-touch exists in hardware but not in the driver.** `panjit_ts` reports only
`ABS_X`/`ABS_Y`/`BTN_TOUCH` with no MT slots. The controller itself is **2-point multi-touch with
on-chip gesture recognition** — the vendor factory-test binary `opt/pv02/pv02_app` reads
`Num_Touch` plus two coordinate pairs and exercises pinch-zoom, two-finger pan and multi-touch
click. Reaching it means bypassing the driver on `/dev/i2c-2`. Userspace-only, no kernel work.
Proposal: `IMPROVEMENT_PLAN.md` F6.

**Pressure is declared but untested.** `ABS_PRESSURE` appears in the device's capabilities
(`capabilities/abs = 1000003` → bits 0, 1, 24) and is discarded by `touch_input.c`. Whether the
value actually varies has never been run to a conclusion — see
`native_apps/hardware_test/pressure_test.c`.

**As shipped.** The stock stack used `xinput_calibrator` and `/etc/pointercal.xinput`. Both belong
to the removed X11 stack and are **not** used by anything current.

### 3.4 Audio

**What's there.** TWL4030 (`U14` TPS65930) HiFi codec driving a **single** metal-can speaker
(`SPKR1`, ~20 mm) through the HandsfreeL/R class-D bridge.

**There is no microphone, no 3.5 mm jack, and no jack footprint.** Confirmed by teardown. The
codec registers a capture PCM and exposes 62 mixer controls including `Analog Left Main Mic`,
`Analog Left Headset Mic`, `AUXL/AUXR`, `TX1`, `TX2` and digital loopback — and the stereo
`Headset` output path is distinct from the `PreDriv` path that drives the speaker. **All of it goes
nowhere.** Mono, one small speaker, is a hardware fact and not a driver limitation.

**How to drive it.** ALSA card `rw20`, `hw:0,0` (`twl4030-hifi` ↔ `49022000.mcbsp`). OSS shim at
`/dev/dsp`, `/dev/audio`, `/dev/mixer`. **GPIO12 must be driven HIGH to unmute the amplifier.**

```
DAC1 (app rate -> 48000 SRC) -> HandsfreeL Mux (AudioL1) -> HandsfreeL Switch -\
                              -> HandsfreeR Mux (AudioR1) -> HandsfreeR Switch --> SPKR1
```

| Control | Range | Use |
|---|---|---|
| `DAC1 Digital Fine Playback Volume` | 0..63 | 63 |
| `DAC1 Digital Coarse Playback Volume` | 0..2 | 0 (0 dB) |
| `PreDriv Playback Volume` | 0..3 | 0 = mute, 3 = +6 dB |

Boot setup is `/etc/init.d/audio-enable` (→ `rc5.d/S29audio-enable`):

```bash
echo out > /sys/class/gpio/gpio12/direction
echo 1   > /sys/class/gpio/gpio12/value
amixer -c 0 cset name="HandsfreeL Mux" AudioL1
amixer -c 0 cset name="HandsfreeR Mux" AudioR1
amixer -c 0 cset name="HandsfreeL Switch" on
amixer -c 0 cset name="HandsfreeR Switch" on
```

DAC volumes persist via `alsactl store` → `/var/lib/alsa/asound.state`, restored by
`/etc/init.d/alsa-state`.

Native rate is 48000 Hz; the OSS shim sample-rate-converts automatically. ScummVM runs 22050 Hz
(halves OPL synthesis cost), native games 44100 Hz.

**Gotcha — the OSS shim is buggy, in four distinct ways.** All of these are in `snd-pcm-oss`
emulation, not the hardware. ALSA itself works correctly.

1. **The ~506 ms period stall.** The TWL4030 ALSA driver has a hardware period of ~22,317 frames
   (~506 ms at 44100). A *blocking* `write()` to `/dev/dsp` stalls for the full ALSA period once
   the OSS ring fills — not the ~93 ms OSS fragment. Result: 185 ms of audio, 321 ms of silence,
   repeating — the "bru-bru-bru-KLICK" artifact. **Always open `/dev/dsp` with `O_NONBLOCK`** and
   handle `EAGAIN` with a ~5 ms sleep. The OSS ring drains at the hardware rate continuously
   regardless of ALSA period size. Diagnosed with `native_apps/tests/oss_diag.c`.
2. **Speaker distortion at full scale.** Apply ~50 % software attenuation (`>>1` on int16) before
   writing. ScummVM does this post-mix.
3. **ioctls reset each other.** `SNDCTL_DSP_STEREO` is **silently ignored** (returns `rc=0,
   stereo=1` while the device stays mono — verified with `native_apps/tests/ch_test.c`);
   `SNDCTL_DSP_SPEED` may reset format and/or channels; `SNDCTL_DSP_SETFMT` may reset speed; and
   set-ioctl output values may not reflect actual device state. **Workaround:** set SPEED → FMT →
   CHANNELS in that order, then read back the truth with `SOUND_PCM_READ_RATE`,
   `SOUND_PCM_READ_BITS`, `SOUND_PCM_READ_CHANNELS`, and use the read-back rate. *Evidence:* at
   22050 Hz music played at half speed; at 48000 Hz it got proportionally worse (~4×), consistent
   with `_outputRate` not matching the real device rate. Working implementation:
   `scummvm-roomwizard/backend-files/oss-mixer.cpp`.
4. **32-bit `time_t` overflow.** `sizeof(long) == 4`. Never compute
   `(now.tv_sec - epoch_0) * 1000000L` — baseline timers to *current* time, not epoch zero.

**No `SCHED_RR` audio thread.** On this single 600 MHz core an RT audio thread starves the main
thread and you get a black screen. `SCHED_OTHER` plus the ~500 ms OSS ring is enough.

**As shipped.** The vendor's `init_amixer.sh` never unmutes any mic — corroborating that nothing
was ever wired to the capture path.

### 3.5 Network and power

**Ethernet.** 10/100 Mbps via `J3` (TE MagJack `1-6605834-1`) and `U15` **SMSC LAN9221** — a
**MAC+PHY in one package** on the GPMC bus, so there is no separate PHY chip. MAC seen on RW09:
`00:07:B0:0D:30:53`.

**Power: 802.3af PoE only. There is no barrel jack.** The whole PD front end is on the main board:

| Ref | Part | Role |
|---|---|---|
| `J3` | TE MagJack `1-6605834-1` | RJ45 with integrated magnetics and centre taps |
| `U1` | TI **TPS23750** | 802.3af PD interface **+** DC/DC controller |
| — | Coilcraft **POE13F-12L** | isolated flyback transformer (Coilcraft's TPS23750 reference part) |
| `Q1` / `D1` / `U2` | `4848 5BD` / `NHSTQW 3406` / `MT1107` | flyback FET / rectifier / isolated feedback |
| `U4` | TPS54325-class | secondary-rail buck |

- **A PoE injector or PoE switch port is the only way to power the unit**, on the bench as much as
  on the wall. Budget for one before planning any out-of-case session.
- **Class budget is 12.95 W at the PD**, enforced by the TPS23750's class signature. Everything
  added inside the case draws from it — an XBee at ~50 mA is nothing, a bus-powered USB hard disk
  is not going to work.
- The RJ45 is isolated (magnetics in `J3`, transformer isolation in the flyback), so the Ethernet
  side is safe — but **`J3`'s centre taps carry up to 57 V while powered**.

**Invisible to software.** `/sys/class/power_supply/` shows only `twl4030_ac` and `twl4030_usb`,
both reading 0 (not connected). Nothing reports PoE state.

**No wireless of any kind is fitted.** No WiFi, no Bluetooth. The 802.15.4 socket is empty — see
[Serial ports](#312-serial-ports).

### 3.6 USB

**One connector: `J4`, micro-USB, MUSB OTG in host mode.** Working today for keyboards, mice,
touchpads and game controllers. A micro-USB OTG adapter is required, and a powered hub is
recommended (the port may not supply enough VBUS).

**There is no second USB port and no unpopulated footprint for one.** The device tree declares two
EHCI high-speed host ports with their own PHYs and VBUS regulators, and `hsusb2_phy` even carries a
board-specific reset GPIO (`gpio1[13]`) implying deliberate wiring — but nothing was ever brought
out to a connector on board revision `550-0204-03`. `CONFIG_USB_EHCI_HCD` and
`CONFIG_USB_OHCI_HCD` are both unset and `dmesg | grep ehci` is empty. **Even with kernel source,
enabling EHCI would gain nothing — there is nowhere to plug in.** Recorded so the decision is not
re-litigated.

**Three problems had to be solved to get host mode working. All three fixes are hacks, and all
three are load-bearing:**

**Hack 1 — MUSB DMA, patched at runtime through `/dev/mem`.** The OEM kernel has *both*
`CONFIG_USB_INVENTRA_DMA` and `CONFIG_MUSB_PIO_ONLY` unset, so MUSB init always fails with
`DMA controller not set` (`-ENODEV`). This is a build defect, not a version problem. The fix writes
noop stub function pointers into the `dma_init`/`dma_exit` fields of the `omap2430_ops` struct in
live kernel memory, forcing PIO fallback; the driver then rebinds successfully. Re-applied every
boot by `/etc/init.d/usb-host` (S90).

**Hack 2 — three cross-compiled kernel modules for Xbox controllers.** `CONFIG_INPUT_JOYSTICK`,
`CONFIG_INPUT_JOYDEV` and `CONFIG_INPUT_FF_MEMLESS` are all unset, and the Xbox 360 pad
(`045e:028e`) uses vendor-specific class `ff` rather than HID class `03`, so `usbhid` ignores it.
Three modules built from matching 4.14.52 source and loaded in order by `/etc/init.d/S89xpad-modules`:

| Module | Size | Purpose |
|---|---|---|
| `ff-memless.ko` | 8.4 KB | force-feedback support (xpad dependency) |
| `joydev.ko` | 19.5 KB | `/dev/input/jsX` interface |
| `xpad.ko` | 36 KB | Xbox gamepad driver |

Loadable modules work because `CONFIG_MODULES=y`, `CONFIG_MODULE_FORCE_LOAD=y`, and
`CONFIG_MODULE_SIG` is unset.

**Hack 3 — the DTB power-budget patch (`0x32` → `0xfa`).** The DTB embedded in `uImage-system` set
the MUSB `power` property to `0x32` (50 → **100 mA**), so anything drawing more — an Xbox pad wants
500 mA — was rejected with `rejected 1 configuration due to insufficient available bus power` when
connected directly without a hub. The fix binary-patches the DTB *inside* `uImage-system` to `0xfa`
(250 → **500 mA**), recomputes the uImage CRCs, and writes the image back to `/dev/mmcblk0p1`.

> ⚠️ **This patch does not survive re-imaging.** It is a persistent one-time fix *per SD image* —
> after any reflash it must be re-applied. Tools: `usb_host/find_dtb.py`, `usb_host/patch_dtb.py`
> (recomputes CRCs correctly), `usb_host/verify_patch.sh`.

**Supported device types:**

| Device | Driver | Works out of the box |
|---|---|---|
| Keyboard, mouse, touchpad, hub | `usbhid` / `hub` (built in) | ✅ |
| HID gamepad (generic) | `usbhid` | ✅ if HID-compliant |
| Xbox 360 / One controller | `xpad` (module) | ❌ needs the three modules |

Hubs work, including combo devices with a built-in hub; multiple simultaneous devices are fine.
Touchpad-plus-keyboard combos create two event nodes.

**Verified working:**

```
musb-hdrc musb-hdrc.0.auto: MUSB HDRC host driver
hub 1-0:1.0: USB hub found
input: HID 04d9:a088 as .../input3   (keyboard)
input: HID 04d9:a088 as .../input4   (mouse)
input: Microsoft X-Box 360 pad as .../input5
```

Full technical detail, including MUSB memory addresses, the `omap2430_ops` struct layout, why
`mmap()` works where `write()` does not, and the approaches that failed:
[`usb_host/README.md`](usb_host/README.md).

**Application-side input.** All input is evdev (`/dev/input/event0`–`event31`). Native apps use
`native_apps/common/gamepad.c`, which abstracts touch + USB keyboard + USB mouse + Xbox pad behind
abstract button IDs (`BTN_ID_UP` … `BTN_ID_BACK`) and is configurable via `/etc/input_config.conf`.
**New apps should use it rather than reading evdev directly.** ScummVM has an independent evdev
implementation carrying the same fixes. Analogue stick centre is computed as
`(axis_min + axis_max) / 2` rather than trusting the kernel-reported value; default dead zone 25 %.

### 3.7 LEDs, backlight and PWM

**What's there.** A **bi-colour red/green** status LED and the panel backlight, all three on true
`leds-pwm` channels backed by dedicated dmtimers:

```
pwmchip0 - dmtimer-pwm@9      pwmchip1 - dmtimer-pwm@11      pwmchip2 - dmtimer-pwm@10
```

```bash
/sys/class/leds/red_led/brightness     # 0-100, real duty cycle
/sys/class/leds/green_led/brightness   # 0-100
/sys/class/leds/backlight/brightness   # 0-100
```

C implementation: `native_apps/common/hardware.c`. Vendor scripts that still exist:
`/opt/sbin/backlight/setbacklight.sh`, `/opt/sbin/brightness.sh`, `/opt/sbin/conc_leds.sh`.

**There is no third colour and no light bar on the main indicator** — but driving red and green
together gives amber, so the effective palette is red / amber / green with smooth crossfade.

Separately, `J7`/`J8` feed **side LED status bars** in the left and right case edges (driven by
`U22`/`U23`), and `LED1`–`LED5` are discrete SMD indicators on the board itself.

**Gotcha — all three PWM channels are consumed.** There is no spare PWM for a buzzer.

### 3.8 GPIO and pinmux

Six SoC banks of 32, plus the PMIC's and the GPMC's:

```
gpiochip0    GPIO   0-31    48310000.gpio
gpiochip32   GPIO  32-63    49050000.gpio
gpiochip64   GPIO  64-95    49052000.gpio
gpiochip96   GPIO  96-127   49054000.gpio
gpiochip128  GPIO 128-159   49056000.gpio
gpiochip160  GPIO 160-191   49058000.gpio
gpiochip490  TWL4030 GPIO 490-507   (18 pins)
gpiochip508  GPMC GPIO 508+         (4 pins)
```

**Known assignments** (all in bank 1, `gpiochip0`):

| GPIO | Function | Ours? |
|---|---|---|
| 12 | Speaker amplifier enable (out, high) | **Yes** — audio unmute |
| 13 | HSUSB2 PHY reset | No (port is dead) |
| 14 | LCD panel power-down | No (driver-owned) |
| 15 | LVDS enable | No (driver-owned) |
| 16 | Touch controller reset | No (driver-owned) |
| 17 | Ethernet (SMSC) reset | No (driver-owned) |
| 19 | LCD backlight enable | No (driver-owned) |
| 23 | Touch interrupt (active low) | No (driver-owned) |
| twl | SD card-detect | No |

Currently exported: `gpio12` only.

> ⚠️ **Unclaimed ≠ usable.** The pinmux node configures only ten function groups — `backlight`,
> `dss_dpi`, `gpmc`, `green_led`, `red_led`, `hsusb_otg`, `i2c1`, `i2c2`, `mmc1_cd`, `uart2`. Every
> other SoC ball sits at ROM default and may not reach a pad at all. **The 18 TWL4030 GPIOs are the
> safer expansion target** — they are guaranteed real chip pins.

**`debugfs` is not mounted**, which is why `/sys/kernel/debug/gpio` is unavailable. Mounting it
gives the definitive pin-by-pin label/direction/value dump and should be step one of any GPIO work.

### 3.9 I2C

| Bus | Address | Device |
|---|---|---|
| 1 (`48070000.i2c`) | `0x48` | **TWL4030 PMIC** (multi-function) |
| 1 | `0x49`–`0x4b` | dummy placeholders |
| 2 (`48072000.i2c`) | `0x03` | **Touch controller** (Cypress CY8CTMG120) |
| 3 (`48060000.i2c`) | — | `status = "disabled"` |

**Gotcha — do not scan bus 1.** The vendor's own factory-test wrapper warns its light-sensor probe
can hang the bus, and bus 1 carries the PMIC. There is nothing to find there anyway (see
[Not present](#314-what-is-not-present)).

**TWL4030 subsystems**, all on the one chip: audio codec (`twl4030-codec`), battery-charger
interface (`twl4030-bci`), 18-pin GPIO expander, RTC (`twl_rtc`), USB transceiver
(`twl4030-usb`), and the MADC.

**TWL4030 blocks declared in the DT but with no driver compiled:** two general PWMs, `pwmled`
(LEDA/LEDB), a matrix keypad controller, `pwrbutton`, `bci`, and its own watchdog. The
`pwm`/`pwmled` outputs are the natural place to hang a piezo buzzer — but that needs both a kernel
option (blocked) and a wire.

### 3.10 RTC and hold-up

```
Device:  /dev/rtc0
Driver:  twl_rtc (TWL4030 integrated)
```

Working correctly — `hwclock -r` matches `date`, and `setup-device.sh` already does `hwclock -w`
after `rdate`.

**Hold-up is a supercapacitor, not a battery.** `U17` is a Panasonic/Matsushita **"Gold Cap"
5.5 V 0.47 F** supercap (`GC5.5V0.47F`). MADC channel 9 reads **3184 mV**, meaning the cap is
charged — not that a cell is healthy.

- **Nothing to replace on a schedule and no leakage risk** — there is no chemistry to exhaust.
  Supercaps age (ESR climbs, capacitance falls) over decades, not years. One less worry on
  eight-year-old hardware.
- **But hold-up is hours-to-days, not months.** **Expect the clock to be wrong after any extended
  unplugged period.** That is why `setup-device.sh` does time-sync at boot, and it means anything
  trusting the RTC across a shelf-storage gap needs a sanity check rather than blind faith.

### 3.11 ADC and temperature (TWL4030 MADC)

Present, working, and used by nothing in this project.

```
/sys/bus/iio/devices/iio:device0 -> 48070000.i2c:twl@48:madc

in_temp1_input   = 56          # SoC die temperature, degrees C
in_voltage0..15_{raw,mean_raw,input}
  ch2..ch7  = 7..122 mV        # ADCIN2..ADCIN7 - general-purpose, idle, available
  ch9       = 3184 mV          # VBKP - the RTC supercap
  ch12      = 3266 mV          # VBAT
```

`CONFIG_TWL4030_MADC=y` and the driver probes cleanly at boot. `in_voltage*_mean_raw` gives free
hardware averaging. **Six general-purpose analogue inputs sitting idle** is the cheapest path to
real analogue input on this device — the catch is getting a wire to one, see
[Unpopulated and expansion](#24-unpopulated-and-expansion). Proposal: `IMPROVEMENT_PLAN.md` F4.

### 3.12 Serial ports

| DT node | Linux | Address | Status |
|---|---|---|---|
| `serial@4806a000` (UART1) | — | `0x4806a000` | **disabled** — do not probe |
| `serial@4806c000` (UART2) | `/dev/ttyO1` | `0x4806c000` | **okay** — the console |
| `serial@49020000` (UART3) | — | `0x49020000` | **disabled** — the radio port |

**`ttyO1` is the console:** 115200 8N1, kernel console output plus a **root login shell already
running** (`/etc/inittab`: `O1:12345:respawn:/bin/start_getty 115200 ttyO1 vt102`, and `ttyO1` is
in `/etc/securetty`). U-Boot prints there too with `bootdelay=1` — a one-second window to reach the
`rw20 #` prompt. Physically it comes out at **`P4`** at RS-232 levels — pinout in
[Unpopulated and expansion](#24-unpopulated-and-expansion).

> **The serial console is deliberately not used by this project.** The recovery loop is: pull the
> SD card, reimage, DHCP, SSH. Since the rules in §1 keep NAND and U-Boot untouched, the card *is*
> the entire failure surface — serial would add boot-time *visibility*, not recovery capability.
> Revisit only if NAND or U-Boot ever get written. The header is fully characterised, so picking
> it up later is cheap.

**UART3 is the 802.15.4 / XBee port, and it is dark.** Two independent vendor sources agree:

```
opt/sbin/RoomWizard-zbgatewayd/readme.txt:
  ./zbgatewayd /dev/ttyS2 --baud 57600 --stdout --nodaemon --config gateway.conf
gateway.conf: channelmask 0x07fff800                          # channels 11-26, 2.4 GHz 802.15.4
              tclinkkey 5a6967426565416c6c69616e63653039      # "ZigBeeAlliance09"

opt/pv02/pv02_app  - full XBee AT-command implementation on /dev/ttyS2:
  ATID (PAN ID)  ATCH (channel 0x0B-0x1A)  ATMY (source addr)  ATDL (dest addr)
  "Start an XBee Loopback Test ... Latency = %iusec"
conc_xbeespam.sh   - runs `pv02_app 8 spam` as a burn-in test
```

Legacy `/dev/ttyS2` under the vendor's old 2.6 kernel = OMAP **UART3** = `serial@49020000`. Under
4.14 that node is `status = "disabled"` **and has no pinmux entry** (the pinmux declares only
`uart2`). `/dev/ttyS0..3` exist as stale nodes with nothing bound. Enabling it is a DTB edit —
conceivable without kernel source, since the DTB is appended to `uImage-system` and this project
already binary-patches it, but adding a whole pinmux node is materially harder than the one-word
power patch and is **unproven**. Proposal: `IMPROVEMENT_PLAN.md` F5.

### 3.13 Watchdogs

**Two independent watchdogs. Keep one, disable the other.**

**Hardware — OMAP WDT2. Keep it.**

```
/dev/watchdog, /dev/watchdog0 (OMAP WDT, rev 0x31), /dev/watchdog1
Timeout:  60 seconds
Daemon:   /usr/sbin/watchdog, started by /etc/init.d/watchdog
Config:   /etc/watchdog.conf  (feeds the timer only; test-binary and repair-binary commented out)
Enable:   /etc/default/watchdog -> run_watchdog=1
Feed:     ~1 s (daemon default)
```

Low-overhead and it prevents hard resets. **Once opened it must be fed continuously** — any app
that owns the screen for long periods must keep feeding it.

**Steelcase software watchdog — cron-based. MUST DISABLE.**

```
*/5 * * * * /opt/sbin/watchdog/watchdog.sh
```

`watchdog.sh` → `watchdog_test.sh` checks whether HSQLDB, Jetty and the browser are running and
whether the browser log is fresh. Any failure exits 100–112, `watchdog_repair.sh` runs, and when
repair fails it calls `/sbin/reboot`. There is a grace period of roughly 65 minutes of "repeat
failure in grace period" before it gives up.

**In game mode those services are all absent, so this reboots the device every ~70 minutes.**

The bypass is built in — `watchdog_test.sh` skips every check when the state file is missing:

```bash
if [ ! -f /var/watchdog_test ] && [ ! -f /var/watchdog_test_checkmem ]; then
    # only perform application level checks when the state file is there
```

`setup-device.sh <ip>` handles this: creates `/var/watchdog_test`, comments out the cron job, and
backs the original crontab up to `/var/crontab.steelcase.bak`.

### 3.14 What is not present

**Confirmed absent:**

- ❌ **WiFi / Bluetooth** — no radio of any kind fitted.
- ❌ **Ambient light sensor** — and none is possible. The vendor factory test has a light-sensor
  step (`functionaltest.sh` → `pv02_app 5`, strings `Tests the Light sensor`, `/dev/i2c-1`,
  `Brightness: %u`), and it is absent from the 4.14 device tree — which is why this sat as an
  unverified maybe for a long time. The full teardown settles it: no sensor part, **and no
  aperture, window or light pipe anywhere in the enclosure**. The case is light-tight, so ambient
  sensing is impossible on this SKU regardless of what is populated. The factory test is shared
  firmware across a product family. **Do not probe bus 1 looking for it.**
- ❌ **Microphone, and any audio input path to the outside** — no MEMS mic, no electret, no
  acoustic port. See [Audio](#34-audio).
- ❌ **3.5 mm jack**, and no unpopulated footprint for one.
- ❌ **Second USB port**, and no footprint. See [USB](#36-usb).
- ❌ **Occupancy / PIR / proximity sensor** — a grep of the entire vendor rootfs for
  `rfid|nfc|badge.?reader|PIR|occupancy|proximity|motion.?sensor` returns nothing. RoomWizard 2.0
  detects presence by people tapping the screen.
- ❌ **Badge / NFC / RFID reader** — not on this model.
- ❌ **Camera** — the ISP block is enabled in the DT but no sensor is declared and no driver module
  exists on disk.
- ❌ **Accelerometer.**
- ❌ **Second SD slot** — `mmc@480ad000` and `mmc@480b4000` are both `status = "disabled"`.
- ❌ **Video output** — no HDMI or VGA. A composite/CVBS encoder exists in the SoC and `manager1:
  tv` is present in omapdss, but no connector is known to be routed. Unverified.
- ❌ **Hardware RNG** — `/dev/hwrng` exists but `rng_current = none` (not bound on GP silicon). Use
  `/dev/urandom`.

**Present but unusable without a kernel rebuild** (which is out of scope — see
[Kernel policy](#7-kernel-policy)):

- ⚠️ **SPI** — four controllers (`spi@48098000`, `@4809a000`, `@480b8000`, `@480ba000`) are all
  `status = "okay"` in the DT, but **`CONFIG_SPI` is not set**, so `/sys/bus/spi/` does not exist.
  No children declared.
- ⚠️ **EHCI USB host** — see [USB](#36-usb). Doubly dead: no kernel support *and* no connector.

---

## 4. Boot chain and recovery

### 4.1 The chain

```
OMAP3 ROM
 └─> NAND /dev/mtd0 "boot" @0x0 (+ mirror @0x20000)
       TI GP header, 12,548 B, load addr 0x40200800 (SRAM)
       strings: "Nuvation NAND-Boot Redirector Ver 1.0"
       └─> redirects boot to SD
 └─> SD /dev/mmcblk0p1  (FAT32, bootable, 70.6 MB)   <-- everything lives here
       mlo (50,336 B)  ->  u-boot.bin (467,696 B)
 └─> U-Boot 2015.07 (Dec 13 2021), board=rw20, soc=omap3
       bootcmd: mmc dev 0; mmc rescan;
                cb_getinfo 0x82000000        <-- custom command, READS ctrlblock.bin from FAT
                if cb_boot_mode == "system":    run loadsysimage; run sysboot
                if cb_boot_mode == "bootstrap": loadramfs + uimage-bootstrap
 └─> uImage-system (5,225,796 B)
       = 64 B legacy uImage header + zImage (5,158,728 B) + APPENDED DTB (67,004 B @ 0x4eb788)
       CONFIG_ARM_APPENDED_DTB=y  -> there is no separate .dtb file anywhere
 └─> Linux 4.14.52, root=/dev/mmcblk0p6 (ext4 driver, ext3 filesystem)
```

### 4.2 Partitions

| Partition | Type | Size | Mount | Contents |
|---|---|---|---|---|
| p1 | FAT32 | 70.6 MB | `/var/volatile/boot` | `mlo`, `u-boot.bin`, `uImage-system`, `ctrlblock.bin`. **The DTB is inside `uImage-system`** — no separate `.dtb` file exists. |
| p2 | ext3 | 256 MB | `/home/root/data` | Application data |
| p3 | ext3 | 250 MB | `/home/root/log` | System logs |
| p5 | ext3 | 1500 MB | `/home/root/backup` | Firmware backup, incl. `factory/` upgrade images + `.md5` files |
| p6 | ext3 | ~981 MB | `/` | Root filesystem |

> **Gotcha — p6 is ext3, mounted by the ext4 driver.** U-Boot passes `rootfstype=ext4` and the ext4
> driver happily mounts the ext3 filesystem. **Do not reformat it as ext4.**

Live usage: root 47 % used (474 MB free), data 40 %, log 4 %.

### 4.3 NAND is effectively unused

There **is** NAND (`U13`, Micron MT29F2G16ABBEAHC, 256 MiB SLC, 16-bit, BCH8), but only `mtd0`
holds anything. Everything else reads blank (`ff ff ff ff ...`):

| Partition | Size | Contents |
|---|---|---|
| `mtd0` boot | — | 12 KB Nuvation redirector (the u-boot slot @0x80000 is blank) |
| `mtd1` nandkernel | 11 M | **blank** |
| `mtd2` sdkernel | 11 M | **blank** |
| `mtd3` bootstrap | 92 M | **blank** |
| `mtd4` scratch | 11 M | **blank** — 11 MB of free space that survives an SD reflash |
| `mtd5` controlblock | 4 M | **blank** |

**This is a pure SD-boot device with a 12 KB NAND shim.** That shim is the only irreplaceable
non-SD component, and there is no reason ever to write to it. (Writing `/dev/mtd4` is safe;
`mtd0` is not.)

### 4.4 The U-Boot environment cannot be persisted

`u-boot.bin` contains `setenv`, `printenv`, `editenv` and `showvar` but **no `saveenv`** — verified,
zero occurrences of the string. It is built `CONFIG_ENV_IS_NOWHERE`, consistent with the absence of
`fw_printenv`/`fw_setenv` on the device, no `/etc/fw_env.config`, and no `environ` MTD partition.

> **You cannot brick this device through the bootloader environment.** Every reset restores the
> compiled-in defaults verbatim. Anything typed at the `rw20 #` prompt is a one-shot experiment.

Recovered defaults:

```
bootdelay=1                      # 1 second to interrupt into the "rw20 # " prompt
console=ttyO1,115200n8
mmcdev=0
loadaddr=0x82000000
sysroot=/dev/mmcblk0p6 rw
sysrootfstype=ext4 rootwait
sysargs=setenv bootargs console=${console} vram=${vram} initcall_blacklist=fb_logo_late_init \
        consoleblank=0 mpurate=${mpurate} root=${sysroot} rootfstype=${sysrootfstype}
loadsysimage=fatload mmc ${mmcdev} ${loadaddr} uImage-system
sysboot=echo Booting from mmc ...; run sysargs; bootm ${loadaddr}
loadbootscript=fatload mmc ${mmcdev} ${loadaddr} boot.scr
bootscript=echo Running bootscript from mmc ...; source ${loadaddr}
```

Available commands include `ext4load`, `tftpboot`, `usb`, `i2c`, `mmc`, `nboot`, plus the custom
`cb_getinfo`.

> **Gotcha — `boot.scr` is a trap.** Support is compiled in but **`bootcmd` never calls it**.
> Dropping a `boot.scr` on p1 does *not* override the boot. Overrides require the serial prompt or
> replacing `uImage-system` itself.

### 4.5 Control block and `boot_tracker`

**Binary:** `/opt/sbin/ctrlblk` · **Storage:** `ctrlblock.bin`, 28 bytes, on FAT32 p1 — not in
NAND, not in the U-Boot environment.

| Field | Values |
|---|---|
| `boot_from` | `bootstrap` \| `system` |
| `upgrade_type` | `factory` \| `field` |
| `boot_tracker` | 0–2 (failure counter) |
| `fwversion` | firmware version string |

Verified behaviour:

- U-Boot's `cb_getinfo` **only reads** it, setting `cb_boot_mode`, `cb_upgrade_type`,
  `cb_boot_tracker`. It never writes.
- It is incremented or reset only by **userspace** `/opt/sbin/ctrlblk`, driven by
  `/etc/init.d/ctrlblk` (`S40ctrlblk`), which acts only when `BOOTMODE=system && TRACKER==1` —
  i.e. the single boot immediately after a firmware upgrade.
- **`/opt/sbin/fail.sh` does not exist on this device.** The automatic recovery path that older
  notes referred to is already dismantled.

> **Therefore a kernel that fails to boot does not increment `boot_tracker` and does not trigger
> recovery mode.** U-Boot prints `Failure to load system kernel image`, `bootcmd` falls through,
> and you land at the `rw20 #` prompt. Nothing has changed state.

### 4.6 Integrity checking — what is and is not verified

**Nothing is cryptographically signed. There is no secure boot.**

- **No boot-time MD5 verification of the kernel.** The only integrity gate on `uImage-system` is
  its uImage header CRC + data CRC — which `usb_host/patch_dtb.py` recomputes correctly, which is
  why the DTB patch works at all.
- **No `.md5` files exist on p1.**
- The MD5 scheme that *does* exist guards the **upgrade package** on p5
  (`/home/root/backup/factory/`), and is checked by scripts inside the bootstrap ramdisk, in three
  layers: the whole package (`upgrade.cpio.gz.md5`); each partition image
  (`sd_rootfs_part.img.md5`, `sd_boot_archive.tar.gz.md5`, `sd_data_part.img.md5`,
  `sd_log_part.img.md5`); and a post-write read-back comparison after each `dd`, with up to 3
  retries per partition and exit code 6 on final failure.

If you modify anything inside that upgrade tree, regenerate the checksums:

```bash
cd /path/to/modified/images
for file in *.img *.gz *.bin; do md5sum "$file" > "${file}.md5"; done
```

### 4.7 Recovery

**Three independent layers. JTAG is not one of them.**

**Layer 1 — the untouched-kernel trick (zero-cost rollback).** `bootcmd` is hardcoded to
`fatload ... uImage-system`. Stage experiments under a *different filename* and leave
`uImage-system` alone; a failed experiment is undone by a power cycle.

**Layer 2 — pull the card.** The whole system is on removable microSD (`mmcblk0`, root
`mmcblk0p6`). `dd` a known-good backup back, roughly 10 minutes. **This is the working recovery
loop for this project:** pop the card, reimage, set up DHCP, SSH back in.

**Layer 3 — the serial console**, if you ever wire it up. `bootdelay=1` gives a one-second window
to `rw20 #`; a root shell is already running there. Not used by this project — see
[Serial ports](#312-serial-ports).

```sh
# 1. Host, once: full SD backup
sudo dd if=/dev/sdX of=roomwizard-original-4gb.img bs=4M status=progress
md5sum roomwizard-original-4gb.img | tee roomwizard-original-4gb.img.md5

# 2. Stage a new kernel under a NEW name on p1 (never overwrite uImage-system)
sudo mount /dev/sdX1 /mnt/boot && sudo cp uImage-test /mnt/boot/ && sudo umount /mnt/boot

# 3. With serial attached: power on, press a key within 1 s -> "rw20 # "
mmc dev 0
mmc rescan
fatload mmc 0 0x82000000 uImage-test
run sysargs
bootm 0x82000000

# 4. If it panics or hangs: power-cycle and do NOT interrupt.
#    bootcmd loads the untouched uImage-system. You are back. No state changed.

# 5. Promote only after several clean boots, keeping the old one:
cp uImage-system uImage-system-known-good && cp uImage-test uImage-system
```

> **JTAG is required only if you damage the 12 KB NAND redirector (`mtd0`) or write a bad
> `mlo`/`u-boot.bin` to p1.** Observe the rules in [§1](#the-rules-that-prevent-a-brick) and it
> never comes up. `P3` appears to be a TI-14 JTAG header if it ever does.

**Logs** live in `/var/log/` (system), `/home/root/log/` (application),
`/var/log/browser.{out,err}` and `/var/log/jettystart`.

---

## 5. Software stack

### 5.1 As shipped

```
Linux 4.14.52 (SysVinit)
  -> X11 / Xorg
    -> WebKit browser (Epiphany), home page http://localhost/frontpanel/pages/index.html
      -> Jetty 9.4.11  (/opt/jetty-9-4-11/, init script /etc/init.d/webserver)
        -> OpenJRE 8   (/opt/openjre-8/)
          -> Steelcase room-booking application
            -> HSQLDB 2.0.0  (/home/root/data/rwdb/)
            -> Interbase     (/opt/interbase/data/websign.gdb, legacy)
```

This entire stack is removed or disabled in game mode. Java 8 still exists at `/opt/openjre-8/` if
something needs it; Python requires cross-compiled ARM binaries.

### 5.2 As we run it — game mode

Native apps render straight to the framebuffer, so X11, the browser, Jetty, Java and the databases
are all unnecessary — and the Steelcase software watchdog reboots the device roughly hourly if they
are simply *absent* without being disabled properly. `setup-device.sh <ip>` does all of this.

**Result: ~80 MB RAM freed, no unwanted reboots, stable game mode.** Optionally
`setup-device.sh <ip> --remove` deletes the bloatware (~178 MB, and removes a vulnerable
Jetty/HSQLDB/Java stack); `--deep-clean` frees ~560 MB more.

**Init services disabled:**

| Service | Why |
|---|---|
| `webserver`, `jetty` | Jetty wrapper + servlet container — not needed |
| `browser` | Epiphany/WebKit — games use the framebuffer |
| `x11` | Xorg — games use the framebuffer |
| `hsqldb` | Room-booking database — not needed |
| `snmpd` | SNMP monitoring — not needed |
| `vsftpd` | FTP server — not needed, security risk |
| `nullmailer` | Mail relay — not needed |
| `ntpd` | Replaced by the `time-sync` init script |
| `startautoupgrade` | Steelcase OTA upgrades — not needed |

**Cron jobs disabled:**

| Job | Why |
|---|---|
| `watchdog.sh` | **Root cause of the ~70-minute reboots** — monitors the absent Steelcase stack |
| `get_time_from_server.sh` | Steelcase NTP — fails repeatedly, spams logs |
| `sync_clocks.sh` | SW/HW clock sync — spams "time difference" messages |
| `rotatedbtables.sh` | HSQLDB table rotation — database removed |
| `backup.sh` | Steelcase data backup |
| `scheduledusagereport.sh` | Steelcase telemetry |
| `gettimestamp.sh` | Steelcase timestamp |
| `remove_older_sync_meetings.sh` | Meeting data cleanup |
| `runfsck.sh` | Filesystem check at 03:10 — can stall the system |
| `checkformemoryusage.sh` | Java heap monitor — Java removed |
| `adjustbklight.sh` | Backlight schedule — turns the screen off at 19:00 |

**Kept:**

| Item | Purpose |
|---|---|
| `watchdog` | Hardware watchdog feeder — prevents hard resets |
| `sshd` | Remote access |
| `cron` | Runs the two surviving jobs |
| `dbus` | System message bus |
| `audio-enable` | Speaker amplifier GPIO + mixer setup |
| `time-sync` | `rdate`-based time sync at boot (matters — see [RTC](#310-rtc-and-hold-up)) |
| `roomwizard-games` | App launcher |
| `rotatelogfiles.sh` (cron, 4 h) | Log rotation — prevents disk fill |
| `cleanupfiles.sh` (cron, 4 h) | Temp file cleanup |

Full guide including filesystem analysis and security considerations:
[`native_apps/README.md#system-optimization`](native_apps/README.md#system-optimization).

### 5.3 App launcher and manifests

`/etc/init.d/roomwizard-app` reads `/opt/roomwizard/default-app` and **respawns that binary
whenever it exits** — which is how quitting a game returns you to the launcher.

`app_launcher` scans `/opt/roomwizard/apps/*.app` manifests and renders them as a touch grid.
Manifest format is INI: `name=`, `exec=`, `icon=` (PPM P6), `args=` (one of `fb,touch` / `fb` /
`touch` / `none`). **Each component's deploy script writes its own manifests and icons** — that is
how projects plug into the launcher with no central registry.

Device paths worth knowing:

```
/opt/games/                  native binaries
/opt/roomwizard/apps/        .app manifests
/opt/roomwizard/icons/       PPM icons
/opt/roomwizard/default-app  boot target
```

---

## 6. Building for this device

Everything cross-compiles on the host and deploys over SSH. **There is no CI, no test runner and no
lint** — the "tests" are interactive on-device diagnostic tools.

Toolchain: `arm-linux-gnueabihf-gcc` (`sudo apt install gcc-arm-linux-gnueabihf`). ScummVM
additionally needs WSL Ubuntu 20.04+ and `g++-arm-linux-gnueabihf`.

### 6.1 Cortex-A8 has no hardware integer divide

The core does **not** implement `sdiv`/`udiv` (those need ARMv7ve, Cortex-A15 and later).
Executing one raises **SIGILL** — the binary dies instantly with exit code 132, no output, no log
files. `dmesg` may not even show the trap on 4.14.52.

The conventional advice is to build with `-mcpu=cortex-a8 -mfpu=neon`. **On this toolchain that
makes no difference to the emitted code** — app-level 32-bit `int` division already compiles to a
call to the software helper `__aeabi_idiv`, and the output is byte-identical with and without the
flags (verified). Keep them for explicitness, but they are not what saves you.

| Component | Flags actually used |
|---|---|
| Native apps | none — bare `$CC -O2 -static` ([`native_apps/build-and-deploy.sh`](native_apps/build-and-deploy.sh)) |
| ScummVM | `-mcpu=cortex-a8 -mfpu=neon` added to `config.mk` after configure |
| ARM dependency libraries | same flags as ScummVM |

**Checking a binary — the raw count is NOT expected to be zero.** A `-static` glibc binary always
carries **~45** `sdiv`/`udiv` in *unreachable* libc internals: the `_dl_*` TLS loader, the
`hack_digit` / `_i18n_number_rewrite` printf-locale paths, and the `__aeabi_ldivmod` /
`__udivmoddi4` 64-bit divmod helpers. These are byte-identical across known-good deployed binaries
and never execute.

What must hold is: **no `sdiv`/`udiv` inside the application's own functions.**

```bash
arm-linux-gnueabihf-objdump -d BIN | grep -B300 'sdiv\|udiv' | grep '^[0-9a-f]* <'
# Every hit should land in one of the libc internals listed above.
```

Dynamic linking is unaffected — the device's own `libgcc` handles division correctly. This is
**only** a static-linking concern.

### 6.2 Never use `--whole-archive` with `-lpthread`

It pulls in all of glibc 2.31's pthread init, which calls `clock_gettime64` — ARM syscall 403,
added in kernel 5.1. This device runs 4.14.52, gets `-ENOSYS`, then dereferences a NULL VDSO
pointer: **SIGSEGV before `main()`**, with no output and no log. The `dmesg` signature is
`PC is at 0x40` with `r0 : ffffffda`.

Plain `-lpthread` is fine. The native C apps escape this only because they never link pthread.

### 6.3 Cross-compiled dependencies must be built from source

Ubuntu Focal under WSL cannot do armhf multiarch — `dpkg --add-architecture armhf` fails and the
standard mirrors carry no armhf. This is why ScummVM and the VNC client each build their own zlib,
libpng and libjpeg into a local prefix.

**libpng needs `-DPNG_ARM_NEON_OPT=0`.** With `-mfpu=neon`, libpng's build system detects NEON and
enables NEON code paths in the C source — but the actual NEON assembly files
(`png_do_expand_palette_rgba8_neon`, `png_riffle_palette_neon`,
`png_do_expand_palette_rgb8_neon`, `png_init_filter_functions_neon`) are not compiled by our manual
build, producing undefined-reference link errors. Disabling the paths costs nothing measurable for
the small PNGs in use.

### 6.4 Verify artifacts on disk, not config flags

Generated `config.mk` / `config.h` files go stale. A leftover `USE_PNG = 1` once made the build
compile `image/png.cpp` with no `libpng.a` present. **Test for the `.a`, not the flag.**

### 6.5 Software rendering techniques that paid off

ScummVM went from 80 % to 32 % CPU using these; the VNC client independently reused the same set:

- precomputed palette LUTs
- a precomputed source-column table plus row-pointer lifting, to remove per-pixel division
- border-only clearing
- skipping `fb_swap` entirely on unchanged frames
- 16bpp RGB565 to halve write bandwidth
- NEON `vst1q_u16` 8-pixel blits
- row deduplication via an L1-resident temp row (~57 % of scaled rows are duplicates)

---

## 7. Kernel policy

**Do not rebuild or upgrade the kernel. Do not attempt a mainline port.** This is a settled
decision, recorded so it is not re-litigated.

**The running kernel cannot be rebuilt from anything available.** `usb_host/linux-4.14.52/` is
**vanilla upstream kernel.org 4.14.52**, not Steelcase source. Three things in the device's own
`/proc/config.gz` have no counterpart in it:

| Symbol | Status in vanilla 4.14.52 |
|---|---|
| `CONFIG_TOUCHSCREEN_PANJIT=y` | **Does not exist.** Vanilla has only `TOUCHSCREEN_USB_PANJIT`, a different USB driver. No `panjit*.c` in the tree. |
| `CONFIG_FB_OMAP2_PANEL_SHARP_LQ070Y3LG4A=y` | **Does not exist.** No `panel-sharp-lq070y3lg4a.c` upstream, ever. |
| `arch/arm/boot/dts/omap3-rw20.dts` | **Does not exist.** |

`build-xpad-module.sh` runs `olddefconfig`, which silently drops the first two. Harmless for
building `.ko` modules — but it means **a kernel image built from this repo would boot with no
display and no touchscreen.** Obtaining the vendor's GPL source would unblock a rebuild;
**pursuing that has been explicitly ruled out.**

**Only three things are genuinely un-portable:** the board DTS, `panjit_ts`, and the panel driver.
Everything else is stock mainline (TWL4030, smsc911x, omap2-nand, musb, leds-pwm, hsmmc,
`ti,omap-twl4030` audio). And the panel is no longer a blocker now that its timings are recorded in
[Display](#32-display) — it reduces to a stock `panel-dpi` node.

**Upgrading would be a net loss anyway:**

| Hoped-for benefit | Reality here |
|---|---|
| Working ALSA instead of the buggy OSS shim | **ALSA already works.** The bug is in the `snd-pcm-oss` emulation layer. **Fix is pure userspace, on this kernel, zero risk.** |
| Better USB host / DMA | A **kernel config defect**, not a version problem. Unfixable without source; the `/dev/mem` runtime patch stays. |
| PREEMPT_RT / lower latency | Currently `PREEMPT_NONE`, `HZ=100`. Config-only, and 4.14 has an official `-rt` branch. Unfixable without source. |
| DRM/KMS instead of fbdev | **Net negative — see below.** |
| Modern WiFi dongle support | No WiFi hardware. 4.14 already carries `rtl8xxxu`, `rtl8192cu`, `mt7601u`, `ath9k_htc`. |
| Security patches | LAN-only device, no browser, no untrusted input. |

**The DRM/KMS trap is the decisive argument.** `omapfb` and `omapdss` were deprecated across 4.x
and **removed from mainline during 5.x**; the OMAP3 replacement is `omapdrm`, a DRM/KMS driver.
Under `omapdrm` you get `/dev/fb0` only via `CONFIG_DRM_FBDEV_EMULATION`, and **DRM's fbdev
emulation exposes a fixed pixel format**. This project switches bpp at runtime in three different
components ([Display](#32-display)), so that switch is expected to fail — breaking ScummVM and the
VNC client until both are rewritten against DRM dumb buffers, on top of hand-writing a board DTS
and reverse-engineering `panjit_ts`. The DSS overlay sysfs interface, the best free performance win
available, disappears too. And a 6.x kernel has a materially larger footprint on a 234 MB box.

> **Verification status:** that the *current* stack supports runtime bpp switching is verified
> (`/sys/class/graphics/fb0/bits_per_pixel` tracks whichever app is running). That DRM fbdev
> emulation would *reject* it is a well-founded expectation based on how that emulation works, but
> it could **not** be tested here — this device has no DRM at all. Treat it as a strong prior, not
> a measurement.

**Brick risk for kernel work: LOW** (removable SD plus the untouched-`uImage-system` discipline).
**Value: LOW.** The ratio does not justify it. Treat this as a userspace problem with a
kernel-config footnote: the two highest-value improvements available — ALSA audio and DSS
overlays — need no kernel work at all.

---

## Appendix A: photo index

17 images in [`HardwarePhotos/`](HardwarePhotos/), from the 2026-07-30 teardown of `RW29 1G-093`.
`Top-*` is the face toward the screen; `Bottom-*` is the face toward the rear cover — the one
carrying the SoC, RAM, NAND, Ethernet, PoE and all the headers.

The photos are stored in **Git LFS**. A clone made without `git lfs install` gets pointer stubs
instead of JPEGs; `git lfs pull` repairs it.

| File | Shows |
|---|---|
| [`Top-Overwiev.jpg`](HardwarePhotos/Top-Overwiev.jpg) | **Complete top face**, unobstructed — `P3`/`P4`, `U27`, `J1`, `J7`/`J8`, `J3`, 40-pin FFC |
| [`Top-Left-Top.jpg`](HardwarePhotos/Top-Left-Top.jpg), [`Top-Top-Middle.jpg`](HardwarePhotos/Top-Top-Middle.jpg), [`Top-right-bottom.jpg`](HardwarePhotos/Top-right-bottom.jpg) | Top-face details |
| [`Bottom-Overview.jpg`](HardwarePhotos/Bottom-Overview.jpg) | **Complete bottom face** |
| [`Bottom-Top-Right.jpg`](HardwarePhotos/Bottom-Top-Right.jpg) | `J5`/`J6` XBee socket, `P3`/`P4` through-holes, `LED1`–`4`, `U32` |
| [`Bottom-Top-Left2.jpg`](HardwarePhotos/Bottom-Top-Left2.jpg), [`Bottom-Center.jpg`](HardwarePhotos/Bottom-Center.jpg), [`Bottom-Bottom-Left.jpg`](HardwarePhotos/Bottom-Bottom-Left.jpg), [`Bottom-Bottom-Right.jpg`](HardwarePhotos/Bottom-Bottom-Right.jpg) | Bottom-face details; PoE section; `SPKR1` |
| [`Connectors-reset-button.jpg`](HardwarePhotos/Connectors-reset-button.jpg) | Rear case edge — RJ45, micro-USB, unidentified slot, reset pinhole |
| [`Touch-Connector-Probably.jpg`](HardwarePhotos/Touch-Connector-Probably.jpg) | Touch flex, `IC1` Cypress `CY8CTMG120`, `U25` |
| [`Screen-Controller.jpg`](HardwarePhotos/Screen-Controller.jpg), [`display_controller_closeup.jpg`](HardwarePhotos/display_controller_closeup.jpg) | Sharp T-CON `K5784TP` and its silicon |
| [`display_back_overview.jpg`](HardwarePhotos/display_back_overview.jpg) | LCD module rear — part-number labels, JAE connector, harness |
| [`display_screen.jpg`](HardwarePhotos/display_screen.jpg) | Bare LCD module removed from the bezel |
| [`bezel_with_touch_screen.jpg`](HardwarePhotos/bezel_with_touch_screen.jpg) | Bezel inner face — screw bosses, side LED bars, **no light aperture** |
