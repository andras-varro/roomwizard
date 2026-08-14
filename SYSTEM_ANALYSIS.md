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
| `J5`, `J6` | 1×10 sockets, 2 mm pitch, **empty as shipped** | XBee radio site — [see below](#24-unpopulated-and-expansion) |
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

**`J5` + `J6` — the XBee socket, empty as shipped.** Two 1×10 female strips at **2.0 mm pitch**, rows
**~24 mm** apart: the Digi XBee footprint (2 mm pitch, 22.86 mm row spacing). `J5` carries a white
**pin-1 dot**, and the **metal inner bezel has a trapezoidal cut-out matching the XBee outline** — the
chassis was tooled for this module. A real XBee test-fits perfectly. **No radio was fitted in any of
the three units as received**, so the batch shipped without the option; there is no antenna on the PCB
because on an XBee the antenna is part of the module. Vendor software confirms the intent — see
[Serial ports](#312-serial-ports).

⚠️ **One of our units now has a module seated in it** (reported 2026-08-13, fitted by hand). That is a
fact about that unit, not about the design, and **nothing about it is verified**: not the orientation,
not whether the module survived being powered, not whether it is Series 1 or Series 2. `J5` pin 1 is a
live 3.3 V rail whatever UART3 does, so a reversed insertion is already a completed experiment. The
staging that protected a single module is in
[`IMPROVEMENT_PLAN.md` F5](IMPROVEMENT_PLAN.md#f5-roomwizard-to-roomwizard-wireless-via-the-802154-radio--open);
two spare modules are on hand.

**Pinout, partly measured 2026-07-30.** `J5` carries XBee pins **1–10** (pin 1 is the dotted end),
`J6` carries **11–20**. Numbering runs down one strip and back up the other like a DIP, so pins 1
and 10 are at opposite ends of `J5`, *not* across from each other — the usual way to get this
backwards.

| XBee pin | Socket | Signal | Status |
|---|---|---|---|
| 1 | `J5` | `VCC` | **measured 3.3 V.** In spec — an XBee's absolute max is 3.6 V, so a 5 V reading would have been a stop. Powering a module is safe. |
| 3 | `J5` | `DIN` — the SoC's TX | not measured. An idle UART transmitter sits **high**, so ~3.3 V here is the cheapest proof a `uart3` pinmux edit took effect; floating or low means it didn't. |
| 5 | `J5` | `RESET` | not measured; should sit ~3.3 V released rather than held low. |
| 9 | `J5` | `SLEEP_RQ` | not measured; should not be sitting high. |
| 10 | `J5` | `GND` | **measured ground.** With pin 1 at 3.3 V this confirms the socket is correctly identified *and* correctly oriented. |

The electrical question is therefore settled; what is unproven is the DTB pinmux edit
([Serial ports](#312-serial-ports)). An XBee fed reversed dies instantly, which is why the
orientation was measured before anything was inserted — and why, on the unit that now has one seated,
the module's health is an open question rather than an assumption.

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

**Gotcha — bpp is per-app and set at runtime.** `/dev/fb0`'s format is **global mutable state**:
whatever the running app last selected via `FBIOPUT_VSCREENINFO`.

| App | bpp |
|---|---|
| every native app — games, launcher, tools, diagnostics | **32bpp XRGB8888**, pinned via `fb_set_bpp(dev, 32)` before `fb_init()`. `app_launcher` and `game_selector` also re-assert it after every child exits |
| ScummVM, VNC remote session | **16bpp RGB565**, to halve write bandwidth |

`native_apps/common/framebuffer.c` is **bpp-aware**: its primitives dispatch on `bytes_per_pixel`
through `fb_pack565`/`fb_unpack565`/`fb_store`/`fb_load`, so the public API takes RGB888 at either
depth and no depth corrupts the back buffer. `fb_init()` accepts 16 and 32; on any other depth it
forces 32bpp and reports why on stderr. So the reason an app pins the depth is **determinism and
appearance** — 16bpp bands every gradient, and how an app looks must not depend on which app ran
before it — not memory safety.

When screenshotting, decode at the bpp of whatever was running: `ssh root@<ip> cat /dev/fb0 > fb.raw`
then `fb565_to_png.py fb.raw fb.png` (defaults to 32bpp; `--bpp 16` for ScummVM/VNC). One 32bpp
frame is 800×480×4 = 1,536,000 bytes — coincidentally the same size as two 16bpp pages, which is
why a wrong-bpp decode looks size-correct while producing garbage. **Run `fbset | grep geometry`
and believe it** rather than inferring the depth from which app you launched: an app killed
mid-session leaves the panel in a mode nothing running asked for. RGB565 read as 32bpp has a
recognisable signature — R == B with a low G, e.g. (64,8,64), (96,16,96).

**Visible area.** The bezel hides **10–15 px on the top and bottom edges only**, and effectively
nothing left or right. Measured on two devices.

Apps never deal with this. `fb_init()` shrinks the drawing surface to the visible rectangle and
`fb_swap()` places it on the panel at the viewport origin, leaving the hidden bands black — so
`fb.width`/`fb.height` and `SCREEN_VISIBLE_*` are the **logical** (fully visible) screen, 800×450 at
the shipped 15/15/0/0 margins. (`SCREEN_SAFE_*` is a *different* rectangle — visible ∩ touchable;
see [§3.3](#33-touch).) Margins come from `/etc/touch_calibration.conf` line 2, default
`FB_BEZEL_*_DEFAULT` = 15/15/0/0 (`native_apps/common/framebuffer.h`), and are set on-device from
**Device Tools → Display → SCREEN EDGES**.

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

`fb1` is the second framebuffer (`CONFIG_FB_OMAP2_NUM_FBS=2`, see above) and is the natural
small-surface render target for a scaled overlay — draw at 400×240 there, let the DSS stretch it.
`/dev/video0` (`omap_vout`) is the V4L2 *output* path and accepts **YUV with hardware colour-space
conversion**, which is what would make a video player conceivable on a part that could never
software-decode one. Both are untried; the DMA-channel error above may be exactly what blocks the
`omap_vout` route.

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

Stage 1 is per-axis **piecewise linear** over the whole 800×480 panel, bezel included: three
segments joined at two knots fixed at panel `dim/4` and `3*dim/4`. Raw is 12-bit (0–4095), and four
raw values per axis define the curve — the readings at panel `0`, `dim/4`, `3*dim/4` and `dim-1`:

```text
raw_min .. knot_lo   ->  panel       0 .. dim/4       outer segment
knot_lo .. knot_hi   ->  panel   dim/4 .. 3*dim/4     fitted interior
knot_hi .. raw_max   ->  panel 3*dim/4 .. dim-1       outer segment
```

`touch_map_axis_panel()` is the single implementation; a linear map is the `knot_lo == knot_hi == 0`
sentinel. There is **no affine transform, no rotation, no shear and no bilinear corner
correction** — each axis remains an independent monotone 1-D curve.

**Why the format has three segments — and why the curve is currently a straight line.** The model
consistent with every measurement is **linear across the panel, hard-clipped at raw 0 and 4095**. The
clipping starts *inside* the panel: on Y, raw 4095 is first emitted around panel 450 and raw 0 around
panel 30 (see **Reach** below). A single line fitted to the interior therefore extrapolates outside
the emittable range — the reference capture's Y fit is `-279..4382` against a hardware range of
`0..4095` — and **that overshoot is the correct answer, not an error.** With the endpoints stored
unclamped, the three-segment curve reduces to exactly that line; the host regression asserts *zero*
deviation.

The format is kept anyway for one practical reason: changing line 1's field count is the single edit
that silently breaks a component linking a stale `touch_input.o` (the old 4-number `sscanf` returns 4
on an 8-number line and accepts it as a legacy config). Three segments also leave room to model a
genuinely non-linear panel if one ever turns up.

> **A claim that was here and is not supported by the data:** a per-segment slope table
> (`8.81 / 9.66 / 9.93 / 8.76` raw counts per panel pixel over Y spans 22→120→240→360→458) with the
> conclusion "the interior is ~12 % steeper than the outer bands". Residuals against the interior line
> are ±80 raw — about ±8 px of finger placement — with no consistent sign, and over ~100 px baselines
> that alone accounts for ±8 % of slope. One run cannot distinguish 12 % from noise. Do not build
> anything on outer-band compression without a repeated, multi-target measurement.

Stage 2 subtracts the viewport origin (`screen_view_x`/`screen_view_y`), which is exactly the
offset `fb_swap()` draws at. A touch on the bezel therefore clamps to the nearest logical edge, and
drawing and touch always share one coordinate system.

Keep the stages apart: calibration must be fitted against **panel** coordinates. Fitting it against
logical coordinates bakes the bezel into line 1, and stage 2 then subtracts it a second time.

**`/etc/touch_calibration.conf`**, loaded automatically by `touch_init()` and `fb_init()`:

- Line 1: `x0 x_knot_lo x_knot_hi x1  y0 y_knot_lo y_knot_hi y1` — the raw→panel curve, per axis the
  raw readings at panel `0`, `dim/4`, `3*dim/4`, `dim-1`. Endpoints may legitimately fall outside
  `0..4095`; that is the measurement.
  A legacy **4-number** line (`min_x max_x min_y max_y`) is still accepted and migrated on load:
  the knots go onto the line it described and the endpoints are left exactly where that line puts
  them, so the legacy mapping is reproduced rather than "improved". A legacy file carries no edge
  measurement, so there is no basis for moving its endpoints — if it described a map with a dead band,
  the migration keeps the dead band and `TOUCHABLE:` reports it. The migration is logged.
- Line 2: `bezel top bottom left right` — panel pixels hidden by the bezel. Omit the line to get the
  compiled defaults (15/15/0/0).
- Line 3 (optional, keyword-tagged): `reach x_lo x_hi y_lo y_hi` — the raw values the **physical**
  edges actually emit, from the wizard's `REACH` sweep. Absent means "assume the `EVIOCGABS` limits",
  which yields a zero inset. Tagged and trailing on purpose: an old parser reads lines 1–2
  positionally and ignores what it does not recognise, so the file stayed back-compatible.

**Device Tools → Display** owns both lines, through **one wizard** — `run_calib_wizard()` in
`native_apps/device_tools/device_tools.c`. Everything it does runs with the bezel zeroed
(`fb_set_bezel(fb,0,0,0,0)`) on the full 800×480 panel, so a drawn pixel *is* a panel pixel and
both lines are measured against the same premise:

| Step | Measures | How |
|---|---|---|
| `TAP` | line 1 | 11 targets × 3 taps, median; least-squares per axis from **interior targets only** |
| `CHECK` | — | the derived curve, per-axis dead-band verdict, edge-probe residuals; ACCEPT / REDO / RESET |
| `EDGES` | line 2 | numbered 2 px ladders at each panel edge; raise each margin until its line clears the plastic |
| `REACH` | line 3 | slide a finger along each of the four edges; 16 coverage cells per edge, all four live at once. An unswept edge falls back to the hardware limit; a *completed* sweep that fell short widens the reported band |
| `REPORT` | — | visible rectangle vs touch-safe rectangle and the per-side inset in px, with the reminder that the band is still drawable. Amber on **magnitude** (`DISP_INSET_SUSPECT`, 24 px), not on "non-zero" |
| `CONFIRM` | — | goes live on the new mapping with a 20 s countdown; reverts unless you press KEEP |

Two entry buttons, one implementation: `CALIBRATE TOUCH` runs the whole thing, `SCREEN EDGES`
jumps to the `EDGES` step for a margins-only tweak. `RESET` restores the hardware `EVIOCGABS`
range and the default margins.

**Nothing is written until CONFIRM,** and the wizard hit-tests its own buttons through the
*entry* calibration until then — so a bad fit can never leave you unable to press the button that
rejects it. `touch_calib_backup()` copies the old file to `.bakN` first.

> **Why it is one flow.** It used to be two, and they disagreed. The 9-tap calibration inset its
> crosshairs only 40 px — inside the band where raw compresses — so the fit slope came out shallow
> and extrapolated outside 0..4095, inventing a phantom X inset. The separate
> `SCREEN EDGES` adjuster drew its reference frame on the *logical* edge, i.e. measured the bezel
> through the bezel. There was also a third copy of the same defective fit in a standalone
> `unified_calibrate` binary. One wizard, and one fit in `common/touch_calib.c`, is what stops
> that recurring.

**The fit lives in `native_apps/common/touch_calib.c`** and nowhere else: the target set, the
per-axis interior masks (≥100 px from each end on X, ≥80 px on Y), the least-squares call,
`touch_calib_curve_from_fit()` (which places the knots on the fitted line and stores the endpoints
**unclamped** — `c->v0 = f->in0`, `c->v1 = f->in1`), the per-axis dead-band verdict, the reach
calculation, `touch_calib_inset_from_reach()`, the shared edge-sweep accumulator (`TouchCalibSweep`,
`touch_calib_sweep_*`), the sanity gate and the `.bakN` backup. Both the wizard and `touch_raw` link
it, so the diagnostic validates the very code the wizard calibrates with. Its sanity gate is **not**
"reject outside 0..4095" — a correct fit on this panel legitimately extrapolates outside it. It
requires `2 × overlap(fit, hw) ≥ max(fit_span, hw_span)`, which accepts the measured-good
`-279..4382` and rejects a skewed `0..60000`.

> ⚠️ **Never reintroduce the endpoint clamp.** `touch_calib_curve_from_fit()` used to clamp `v0`/`v1`
> into `0..4095`, and `touch_input.c`'s legacy migration had a `clamp_to_hw()` doing the same. Both
> are deleted. The clamp asserted that raw 4095 is emitted at panel 479 when it is emitted at panel
> ~450, which tilted the upper outer segment so the reported position ran **ahead of the finger by up
> to +19 px across the bottom quarter** — visibly worse than the bug it was meant to fix.
> `overshoot_lo/hi` remains, as reporting only.

**`touch_raw`** (`native_apps/tests/touch_raw.c`, deployed to `/opt/games/`, hidden from the
launcher; reachable from Device Tools → Display → `TOUCH DIAGNOSTIC`) is the diagnostic that settled
reach, and the only tool that shows the panel with **no calibration and no bezel**: it resets the raw
range to the `EVIOCGABS` values and calls `fb_set_bezel(fb,0,0,0,0)`, so the dot is
`raw × 799 / 4095`. Four modes, and the split between the middle two is the whole point:

| Mode | Question it answers | Method |
|---|---|---|
| `LIVE` | free tracking | crosshair, trail, session extremes, pin flag when raw sticks at a hardware limit |
| `SWEEP` | *what raw does the physical edge emit?* | slide a finger along each edge; per-bucket extreme along the edge, so a corner reading differently from the middle shows up instead of averaging away |
| `INSET` | *where does raw first reach that value?* | tap a bar walked inward from each edge at 0/10/20/35/55 px; the answer localises where the flat band starts |
| `TARGETS` | the interior line | 11 targets × 3 taps, then a hard press on each bezel; fits interior-only vs all-points and reports what each predicts at the edges, per axis |

**Only `INSET` decides the curve.** `SWEEP` alone cannot: a finger sliding along an edge yields no
position information, so it reads identically whether clipping begins at the edge or 30 px inside it.
Conflating the two questions is what kept the endpoint bug alive across three sessions. Logs to
`/tmp/touch_raw.tsv` with a monotonic millisecond column; it can write the interior fit to line 1
after backing the file up to `.bakN`.

> **Beware two-point slopes.** `INSET`'s derived slope must come from the 11-target interior fit, not
> from two adjacent bars: ±80 raw of tap noise over a 10–20 px baseline printed "raw reached at panel
> 594, 614, 817 and −4" on a 480-row panel. Adjacent bars are only good for the *comparison* "is this
> bar already at the limit?".

**ScummVM and `vnc_client` each link their own copy of `touch_input.o`** — rebuild and redeploy both
after changing that file or its touch silently goes stale (see *Reach* below for how silently).

Accuracy: ~3 px at centre, 14–27 px error at the corners before calibration.

**Reach at the border — every edge drives raw to its limit, but not at the edge.** Settled on
2026-08-01 with `touch_raw`'s `SWEEP` and `INSET` modes, with the calibration and bezel zeroed so a
drawn pixel is a panel pixel. Raw capture:
[`touch_raw-2026-08-01-rw09.tsv`](touch_raw-2026-08-01-rw09.tsv), 16:53.

> **Every number in this section is the reference capture, not the live calibration of any unit.**
> The fit is re-run per unit (and per wizard run), so the curve stored in
> `/etc/touch_calibration.conf` on a given device will not match the values below — see
> *Provenance* at the end of this section for what RW09 actually carries. What generalises is the
> *shape* of the result — linear interior, a saturated band inside each Y edge, a much smaller one on
> X — not the digits. The host regression deliberately replays this capture's medians rather than
> reading the device, which is what makes it a regression instead of a snapshot.

`SWEEP` first: **all 16 buckets on all four edges** drive raw to `0`/`4095`, uniformly, with no
corner-vs-middle variation. So the electrode array is not short.

`INSET` then asks where that limit is *first* reached, and this is the part a bezel press cannot see:

| Axis end | raw limit first emitted at panel | panel extent | flat (saturated) band |
|---|---|---|---|
| X left | ~0–12 | 0 | ~0–12 px |
| X right | ~790–799 | 799 | ~0–9 px |
| **Y top** | **~30** | 0 | **~30 px** |
| **Y bottom** | **~450** | 479 | **~29 px** |

Confirmed independently: the 11-target interior fit (`X 10..4076`, `Y -296..4376`), which never sees
an edge sample, predicts a bezel press landing at panel **30 / 450** — the same numbers from a
different method. Whether the cause is a short electrode array or controller clipping is not
distinguished by this data and does not matter for calibration. (Two fits from that day are quoted in
this document: `X 10..4076 / Y -296..4376` above, and `X 17..4084 / Y -279..4382`, which is what the
host regression replays. They differ by ≤7 raw — an order below the ±80 raw tap residual — and
neither is what is stored on RW09 now.)

**Consequence: drawable ≠ pressable.** The band at each end of Y is visible and fully drawable but
cannot be pressed, so `native_apps/common/framebuffer.h` carries **two** rectangles:
`SCREEN_VISIBLE_*` (the full logical screen) and `SCREEN_SAFE_*` (visible ∩ touchable). On the
reference capture at bezel T=11 B=14 the inset is **~17 px top / ~16 px bottom**, with X ≈ 0; the
live RW09 calibration gives 19/16 **and ~6 px on each side of X** (see *Provenance*). **X's band is
much smaller than Y's and can be zero, but a non-zero X inset is not a fault** — it arises by exactly
the same mechanism as Y, from fitted X endpoints that fall outside `0..4095`. Treat any of these
numbers as per-unit, per-calibration.

The inset is **measured at runtime, never hardcoded** — `publish_safe_area()` pushes the four raw edge
extremes through the production `scale_coordinates()` and calls `fb_set_touch_inset()`, so it is
correct in portrait and under any bezel, is `0` until an edge sweep has been recorded, and is capped at
`FB_TOUCH_INSET_MAX` (48 px) with a loud warning. Read it from Device Tools → Display → `TOUCHABLE:`,
the wizard's `REPORT` screen, or the display test's `SAFE AREA` page (red rect = visible, green =
touchable). **A dead band is a fact about this panel, not a bug.**

**The mapping lives in the config file, so deploying code never fixes a bad stored curve.** Line 1 of
`/etc/touch_calibration.conf` is what `touch_init()` uses; a unit whose line 1 was written by older
code keeps that behaviour, symptom intact, across any number of correct deploys until the wizard is
re-run. Corollary for handovers: "the fix is deployed" and "the device behaves correctly" are separate
claims.

**Provenance — the reference capture vs what RW09 actually carries.**

The figures above come from the 16:53 diagnostic run. The user then ran the wizard at **18:50 on
2026-08-01** and kept the result, so RW09's live config is a *different* fit of the same panel:

```text
line 1  -33 1007 3087 4122   -296 875 3217 4379
line 2  11 14 0 0
line 3  reach 0 4095 0 4095
        -> published inset: X 6..793  Y 19..438  of 800x455
```

Three things to read off that, because each one otherwise looks like a contradiction:

- **`reach 0 4095 0 4095` means the sweep found the hardware limits on all four edges** — the
  "assume the hardware limit" case, contributing *nothing* to the inset. So on this unit the entire
  published inset comes from the fit's endpoint overshoot (`-33`/`4122` on X, `-296`/`4379` on Y
  against a `0..4095` hardware range), which is the model working as designed.
- **Therefore 19/16/6 and the ~30/29 px flat band are not the same measurement** and must not be
  reconciled. The inset is *logical rows the current curve cannot address*; the flat band is *panel
  pixels over which the sensor's reading is saturated*, measured by `INSET` mode with no calibration
  in the path. Both are true at once, of different things.
- **No `touch_raw` capture exists for the 18:50 calibration.** The wizard writes the config; only the
  diagnostic writes `/tmp/touch_raw.tsv`, and it was not run afterwards (nor would it survive a
  reboot). The repo's captures are 07-31 and 08-01 16:53, and the 16:53 one stands as the reference.

**Three claims that were in this document and were wrong** — recorded because the mistakes are easy
to repeat, and because two of them were *opposite* errors made two days apart:

1. *"The electrode array is ~11 mm shorter than the LCD; the digitizer spans 153.4 × 80.1 mm."*
   That height was computed by mapping bezel-press raw values through the very fit under test. The
   sweep shows every edge driving raw to its limit, so the array is not short. The millimetre figure
   was an artifact.
2. *"The top ~15 and bottom ~15 rows cannot be touched, and no calibration recovers it."* Wrong as
   stated — the *cause* was the fit, and the band is not 15 px.
3. *"Every visible pixel is touchable; a dead band now means a bug."* (2026-08-01, morning.) This
   over-corrected #1 and #2. It was right that the extrapolated fit manufactured dead bands, and
   wrong that the sensor has none. The clamp it introduced made the bottom edge measurably worse
   (+19 px). A bezel press drives raw to `4095` whether clipping starts at panel 479 or panel 450, so
   that evidence could never have supported the conclusion.

The earlier per-edge figures (~10 px left/right, ~25 top, ~30 bottom) were wrong on X for a separate
reason: the 9-tap flow inset its crosshairs only 40 px. The deeper problem was methodological —
*every* figure gathered before `touch_raw`'s `INSET` mode was inferred **through** a calibration, or
from a gesture that carried no position information. Design the measurement to answer **one** question
and state what it cannot answer.

Two secondary effects, measured at the same time:

- **Finger-centroid scatter is real.** Nine hard presses on the top bezel returned raw
  `22, 76, 105, 111, 93, 79, 90, 118, 47`. Pressing flat near the saturation zone gives a tall
  contact patch whose centroid is pushed inward, and it scatters widely. It shifts the intercept,
  not the slope. Prefer target taps to bezel presses when measuring anything quantitative; use
  bezel presses only to ask the yes/no question "does raw reach its limit here?".
- **A stale binary misparses the config instead of failing.** A `vnc_client` built before the
  8-number line 1 read `0 1020 3074 4095  0 874 3215 4095` as `X [0..1020] Y [3074..4095]`, confining
  touch to the left quarter and the bottom strip. It presented as "touch is broken in vnc_client", not
  as a version mismatch. To check what a deployed binary parsed, look for `Touch raw curve set:` plus
  `(piecewise)` in its startup output; the old code printed `Touch raw range set (linear):`.

Measurements are from **RW09 only**; a second unit has not been measured. Open work:
[`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md) B3c.

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

**Host name and resolution — the image ships a broken one.** `/etc/hostname` on the vendor rootfs
is `RW09`, and `/etc/hosts` carries an extra *non-loopback* line mapping that same name to an
external address that is unreachable from anywhere these units are used:

```text
127.0.0.1 localhost
<external address> RW09.<external domain> RW09
```

Two defects in one file. The name is **baked into the image** rather than generated, so every unit
cloned from it claims `RW09` — which is also where this repo's name for the reference unit came
from. And on a stock image **the device's own name resolves to a dead address**, so anything that
resolves its own hostname gets the wrong answer. `commissioning/set-hostname.sh` fixes both files together, to
loopback-only (it is what `commissioning/card-prep.sh`'s prompt and `commissioning/provision.sh --hostname` both
call); disposition in `IMPROVEMENT_PLAN.md` D7.

**⚠️ The vendor regenerates all four network files on every boot, so editing them is not the last
word.** `/opt/sbin/networkmanager` — a 24,894-byte shell script, started by `/etc/init.d/networkmanager`
and **byte-identical on both captured cards** — rewrites these from `/home/root/data/websign/net.*`:

| File | Written by | From |
|---|---|---|
| `/etc/hosts` | `set_manual()`: truncated to `# Generated by PV networkmanager` + `127.0.0.1 localhost`, then `<ip> <name>` appended | `net.ipaddress`, `net.hostname` |
| `/etc/hostname` | `set_manual()` and `set_dhcp()` | `net.hostname` |
| `/etc/resolv.conf` | truncated and rewritten | `net.dnsserver` |
| `/etc/dhclient.conf` → `send host-name` | `set_dhcp()` — **this is the name a DHCP server, and therefore a router's device list, sees** | `net.hostname` |

- **`websign/net.mode` selects the branch, and `manual` makes the unit unreachable.** `dhcp` runs
  `dhclient eth0`; `manual` runs `ifconfig eth0 <net.ipaddress> netmask <net.netmask> up` and **never
  sends a DHCP request** — so the unit appears in no lease list and answers only on whatever subnet its
  previous owner used. A stock card can ship either way: one captured unit is `manual`, with a static
  RFC-1918 address on its previous owner's subnet and `net.hostname = null`, and its own syslog records
  `Manual IP Mode detected.` / `Vaild host name found: null` / `status: manual-bound`.
- **Deleting `/home/root/data/websign` makes the script inert for the host name.** Both writers live
  *inside* `set_manual()`/`set_dhcp()`; with `net.mode` unreadable neither branch runs, so `/etc/hosts`
  and `/etc/hostname` are never touched again. `commissioning/provision.sh`'s deep clean removes that directory,
  which is why a cleaned unit keeps the name `commissioning/set-hostname.sh` gave it — and an uncleaned one does not
  (`IMPROVEMENT_PLAN.md` D7b).
- ⚠️ **The vendor's own validator rejects hyphens.** `net.hostname` is filtered by an awk regex that
  accepts `RW09`, `RW20`, `rwtest` and `null` but **rejects `RW-Test` and `rw-test`**; a rejected name
  logs `Invalid host name detected.` and the DHCP client then announces the hardcoded fallback
  `rwtwenty`. Prefer a hyphen-free name on any unit that still has the vendor stack.
- **There are two dhclient scripts, and only the vendor's rewrites `/etc/hosts`.** `/etc/dhclient-script`
  (vendor, 10,370 bytes) has a `# PV02 Addition` block that on every `BOUND` event writes
  `net.hostname` into `/etc/hostname`, truncates `/etc/hosts` to `127.0.0.1 localhost` and appends
  `<leased-ip> <name>` — D7's defect, regenerated. `/sbin/dhclient-script` (the stock ifupdown one,
  16,772 bytes) contains **zero** references to `/etc/hosts`. Which one runs depends on who starts
  `dhclient`: the vendor `networkmanager` passes `-sf /etc/dhclient-script`, while `S40networking` +
  `auto eth0 / iface eth0 inet dhcp` uses the default. **Measured on a unit in service** (vendor stack
  removed, ifupdown DHCP, `dhclient -pf /var/run/dhclient.eth0.pid eth0` running): `/etc/hosts` and
  `/etc/hostname` have not been written for five months while `/var/lib/dhcp/dhclient.leases` updates
  daily. So once the `networkmanager` boot link is gone, **nothing regenerates either file** — which is
  what makes an offline-set name stick.
- ⚠️ **`/etc/dhclient.conf`'s `send host-name` is a third copy of the name, and nothing in this repo
  writes it.** The vendor image ships `send host-name "RW09";` and the same unit still announces `RW09`
  to DHCP — so a router's device list keeps showing the shipped name however often `/etc/hostname` is
  corrected. `commissioning/set-hostname.sh` should own this file too (`IMPROVEMENT_PLAN.md` D7b).

**mDNS is present but not started.** `/usr/sbin/avahi-daemon` (109 KB) and a complete
`/etc/init.d/avahi-daemon` are both on the vendor image — there is simply **no `rc5.d` link**, so it
never runs. One symlink enables it (`commissioning/provision.sh` now adds `S30avahi-daemon`), after which the
unit answers to `<name>.local` and neither SSH nor the deploy scripts need a DHCP-lease hunt. Its
`Required-Start` is `$remote_fs dbus`, and dbus is one of the few dynamic consumers the deep clean
deliberately keeps, so the dependency holds even on a fully cleaned device. `enable-wide-area=yes`
and `publish-workstation=no` are the shipped defaults and neither affects `.local` name resolution.
⚠️ This is only useful **after** each unit gets a unique name: with the stock image every unit is
`RW09`, and avahi resolves the collision by renaming to `RW09-2.local`, `RW09-3.local` and so on.
**Cost measured on RW09 2026-08-03 — cheap:** ~3.9 MB RSS total (2424 kB `avahi-daemon` + a 1512 kB
chroot helper) of 234 MB, and it does **not** start dbus — `dbus-daemon` was already running at a
lower PID and costs its own 1692 kB regardless.
⚠️ **`.local` resolves from Windows but not from WSL**, whose `nsswitch.conf` is `hosts: files dns`
with no mDNS module — so `ssh root@rw09.local` works in PowerShell while the WSL-based build and
deploy scripts cannot resolve it until `libnss-mdns` is installed there (`IMPROVEMENT_PLAN.md` D7).

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

**Hack 1 — MUSB forced to IRQ-driven PIO, patched at runtime through `/dev/mem`.** The OEM kernel has
*both* `CONFIG_USB_INVENTRA_DMA` and `CONFIG_MUSB_PIO_ONLY` unset, so MUSB init always fails with
`DMA controller not set` (`-ENODEV`). This is a build defect, not a version problem. The fix writes
noop stub function pointers into the `dma_init`/`dma_exit` fields of the `omap2430_ops` struct in
live kernel memory, forcing PIO fallback; the driver then rebinds successfully. Re-applied every
boot by `/etc/init.d/usb-host` (S90).

> ⚠️ **State this the right way round: the patch makes MUSB *give up* on DMA.** It does not fix or enable
> DMA — it installs stubs so the core falls back to interrupt-driven programmed I/O. **DMA remains
> unavailable on this device**, and every USB transfer costs CPU. That is fine for input devices and a
> Bluetooth dongle (tens of KB/s) and starts to matter for uncompressed USB audio (~190 KB/s).
> ⚠️ `CONFIG_DMADEVICES=y` and `CONFIG_TI_EDMA=y` *are* set and are a **red herring** — that is the
> **system** EDMA via dmaengine, not the Inventra engine inside the MUSB block that OMAP3 uses.
> Whether DMA is reachable at all is [`IMPROVEMENT_PLAN.md` F17](IMPROVEMENT_PLAN.md#f17-bluetooth-peripherals-and-whether-usb-dma-is-reachable--open-measured-2026-08-08).

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

**Why this one cannot be a boot-time script, when Hack 1 can.** Read out of the 4.14.52 source
2026-08-08:

| Where | What happens |
|---|---|
| `drivers/usb/musb/omap2430.c:452` | `of_property_read_u32(np, "power", (u32 *)&pdata->power)` — read from the device tree **at driver probe** |
| `drivers/usb/musb/musb_core.c:2369`/`:2381` | passed on as `musb_host_setup(musb, plat->power)` |
| `drivers/usb/musb/musb_host.c:2797` | `hcd->power_budget = 2 * (power_budget ? : 250);` |

The device tree is appended to the kernel image *inside* `uImage-system`, and the driver reads it before
any init script exists — so unlike Hack 1, which patches a *static* struct that stays patched until
reboot, there is no file on the normal filesystem to edit. **That one number is the only reason p1 is
written at all.**

⚠️ **Note the `? : 250`: an absent or zero property would already give 500 mA.** The vendor deliberately
set 100 mA, overriding a kernel default that was what we wanted. It is a vendor choice, not a hardware
limit.

**The vendor kernel is byte-identical across units — measured 2026-08-08, five sources, three units:**

| File | md5 | Size |
|---|---|---|
| vendor `uImage-system` (p1 of both card captures, both p5 factory payloads, RW09's copy) | `edc637ac14f90e0187b1ed65ffedf6d7` | 5,225,796 |
| `uImage-system-patched` | `a1fd1af8da18c430a34b24762aa16dab` | 5,225,796 |

Nothing generates it per-unit, unlike the filesystem UUIDs
([§4.2](#42-partitions)). The two differ in **exactly 9 bytes**: the uImage header CRC (offsets 4–7), the
data CRC (24–27), and one value byte at `0x4FA2CF`. That makes an md5 gate a complete check, which is what
[`IMPROVEMENT_PLAN.md` F15](IMPROVEMENT_PLAN.md#f15-usb-host-mode-through-commissioning--done-2026-08-08-confirmed-on-a-unit-2026-08-09)
builds on.

> ⚠️ **This patch does not survive re-imaging.** It is a persistent one-time fix *per SD image* —
> after any reflash it must be re-applied. Tools: `usb_host/find_dtb.py`, `usb_host/patch_dtb.py`
> (recomputes CRCs correctly), `usb_host/verify_patch.sh`.

**Supported device types:**

| Device | Driver | Works out of the box |
|---|---|---|
| Keyboard, mouse, touchpad, hub | `usbhid` / `hub` (built in) | ✅ |
| HID gamepad (generic) | `usbhid` | ✅ if HID-compliant |
| Xbox 360 / One controller | `xpad` (module) | ❌ needs the three modules |
| **Bluetooth dongle** | `btusb` — ⚠️ **not built** | ❌ `# CONFIG_BT is not set`; see below |

**A Bluetooth dongle is the only route to a wireless peripheral, and the kernel side is unbuilt.** There
is no radio on the board at all ([§2.4](#24-unpopulated-and-expansion)), so BT means a dongle in this
single connector. `# CONFIG_BT is not set` — exactly the situation `CONFIG_INPUT_JOYDEV` was in before
Hack 2 — and its dependencies are satisfiable: `CONFIG_NET`, `CONFIG_CRC16`, `CONFIG_HID` and
`CRYPTO_AES` are all `=y`, while `CRYPTO_SHA256`, `CRYPTO_BLKCIPHER`, `CRYPTO_ECB` and `CRYPTO_CMAC` are
`=m` and would have to be **built and shipped**, since `/lib/modules/4.14.52/` ships empty.
`CONFIG_CRYPTO_ECDH` is unset and is needed only for BT LE Secure Connections. ⚠️ **The controller is far
more likely to work than the audio** — A2DP needs software SBC encoding on this single core. Also unbuilt
and worth knowing: `CONFIG_SND=y` and `CONFIG_SND_USB=y` but `# CONFIG_SND_USB_AUDIO is not set`, so a
wired USB DAC is one module away too. Both are
[`IMPROVEMENT_PLAN.md` F17](IMPROVEMENT_PLAN.md#f17-bluetooth-peripherals-and-whether-usb-dma-is-reachable--open-measured-2026-08-08).

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

⚠️ **A device is enumerated only if it is attached when the MUSB driver probes.** Boot a unit with
nothing plugged in and the port is dead for the rest of that boot — anything inserted afterwards is
never even powered. Measured on `.188` 2026-08-13, reproducible on demand with a driver unbind/bind, and
the operator reports it has always been so on every unit: *"USB only worked if it was connected at
boot."* Where `$MUSB` = `/sys/devices/platform/68000000.ocp/480ab000.usb_otg_hs/musb-hdrc.0.auto`:

| Action | Result |
|---|---|
| device attached as the driver probes (boot, or a rebind) | **works.** `Vbus on`, enumerates ~0.5 s later |
| driver probes with **nothing** attached | `Vbus on` for a few seconds, then **`Vbus off`** and the port is dead. The OTG adapter alone does not prevent this — measured with the adapter in and no device in it |
| plug a device into that dead port | **nothing at all.** No log line, no VBUS, the device's own LED stays dark |
| unplug a working device, wait 95 s, replug | **works.** `Vbus on` throughout; `dmesg` shows `unhandled DISCONNECT transition (a_idle)` and the disconnect is not processed until the device returns |
| driver unbind + bind with the device attached | **the only userspace recovery found.** Enumerates ~0.5 s later |

**`$MUSB/vbus` is the diagnostic — `Vbus on` live, `Vbus off` dead.** Its on/off half is a real read of
the DEVCTL VBUS comparator, not cached state.

**Why, read out of the 4.14.52 source** in [`usb_host/linux-4.14.52/`](usb_host/linux-4.14.52/) — vanilla
upstream, and authoritative for this code because none of it is vendor-patched:

- VBUS here is driven **solely by the DEVCTL `SESSION` bit**. `twl4030` registers no `set_vbus` op, so
  `otg_set_vbus()` returns `-ENOTSUPP` and `omap2430_musb_set_vbus()` does nothing.
- **The DTB is the root cause.** `mode = <0x03>` on the musb node (`usb_host/original.dts:3820`) is
  `MUSB_PORT_MODE_DUAL_ROLE` (`musb_core.h:82-84`) — on a kernel built `# CONFIG_USB_GADGET is not set`
  (`usb_host/device_config:3106`) where `musb_gadget.c` is not even compiled. Dual-role therefore buys
  nothing this kernel can use, and costs two things: `musb_host_setup()` claims `default_a`/`A_IDLE` only
  for `MUSB_PORT_MODE_HOST` (`musb_host.c:2789-2793`), and `musb_start()` (`musb_core.c:1074-1080`) masks
  `SESSION` off and restores it **only** when that clause is false or VBUS already reads invalid.
- `musb_start()` has **three** call sites, not one: `musb_virthub.c:398` and `:461`
  (`SetPortFeature(PORT_POWER)`) and `musb_core.c:1977` (babble recovery). The hub one is re-enterable at
  runtime through the port over-current path (`musb_core.c:711-713` → `hub.c:5079`).
- **The OTG ID pin is watched by the TWL4030 PMIC, not by MUSB** — its own interrupt line
  (`phy-twl4030-usb.c:747`), reading an always-powered `PM_MASTER` register (`STS_HW_CONDITIONS`,
  `:298-314`), so it fires with the PHY asleep, MUSB in standby and VBUS off. ⚠️ But what an ID event
  produces is a **resume**, and a resume replays the *cached* DEVCTL (`musb_core.c:2609-2610`). On a cold
  port the cached `SESSION` bit is clear, so there is nothing to resume — which is why ID-ground alone
  cannot revive a dead port (measured; see the table below).
- With no device connected, `musb_pm_runtime_check_session()` matches `MUSB_QUIRK_A_DISCONNECT_19` and
  after 3×1000 ms polls drops its pm_runtime reference. Once a session exists it is never torn down:
  `omap2430_ops` has no `.try_idle`, so `musb_platform_try_idle()` is a no-op and `SESSION` is never
  cleared — which is why a live port stays live indefinitely, including across an unplug.

⚠️ **Measured on `.188`, 2026-08-13 — and two of these refute what this section used to say.**

| Measurement | Result |
|---|---|
| Replug an already-working peripheral at 70, 75, 90, 120, 150, 180, 240, 300 s | **works every time.** Gap length is not a variable; the earlier "10 s recovers, 60 s does not" reading has no counterpart on current hardware |
| A passive hub attached to a **dead** port, nothing else | `Vbus off` at 1, 2 and 3 min and for several minutes after; a device then plugged into that hub enumerates nothing |
| Re-seat an OTG adapter on a port that **had already had a session** | revives it immediately, even after minutes dark |
| `/etc/init.d/usb-host recover` on a dead port, pad attached | works — but has needed **two consecutive runs** both times, and the second time **both** runs started from `Vbus off`, ruling out "VBUS still valid at re-probe" as the general cause |

⚠️ **So a hub or adapter left permanently attached does NOT fix this.** That claim was written here and in
`device-files/usb-host` on the strength of the driver's *teardown* path, which assumes a session already
exists; it was refuted by the hub row above within the hour. It holds a port **open**, not a port **alive**.

⚠️ **Six readings and writes that look like the answer and are not.** Three of these were believed and
written down before being refuted — one of them in this document.

| Looks like | Actually |
|---|---|
| `echo host > $MUSB/mode` | **silent no-op.** `omap2430_ops` has no `.set_mode`, so `musb_platform_set_mode()` returns 0 and the store reports success having done nothing. ⚠️ This section previously recorded it as *the* cure for a failed replug on the strength of one `.225` observation. The source refutes it: whatever recovered that unit, it was not this write |
| `$MUSB/vbus`'s `timeout 1100 msec` | **inert.** Nothing on omap2430 reads `musb->a_wait_bcon` — there is no `.try_idle` — so it is an untouched default from `allocate_instance()`. Writing `0` or `3600000` changes the printed number and nothing else |
| `power/control = on` (forbidding runtime PM) | **does not prevent the drop.** Measured with `runtime_status` reading `active` throughout and the port still going dark |
| `$MUSB/mode` as a state reading | **not diagnostic.** Reads `a_idle` with a pad enumerated, `js0` present and the game responding to it |
| `twl4030-usb/vbus` = `off` | **0444, and it is not a port-state reading at all** — it reports `vbus_supplied`, i.e. somebody feeding *us*, which is cleared whenever `twl4030_is_driving_vbus()` is true (`phy-twl4030-usb.c:301-306`). So it reads `off` in the working state **and** the dead one |
| `lsmod` → `xpad 28672 0` | a module refcount counts module *users* (`ff_memless 16384 1 xpad`), not bound devices. It reads `0` with a pad bound and `event1`/`js0` present |

**The one real userspace trigger besides a rebind is debugfs `softconnect`** (`musb_debugfs.c:301-343`,
`CONFIG_DEBUG_FS=y`) — and it sets `SESSION` **only** in `OTG_STATE_A_WAIT_BCON`, so it cannot revive a
port sitting in `a_idle`. Ranked candidates, including the DTB `mode` patch that addresses the cause:
[`IMPROVEMENT_PLAN.md` B32](IMPROVEMENT_PLAN.md#b32-usb-is-enumerated-only-at-driver-probe--cause-established-2026-08-13-no-automatic-fix).

**Reading the live device tree.** `/sys/firmware/devicetree/base/` is the unflattened tree as the
running kernel holds it, and `/sys/firmware/fdt` is the raw blob (67 273 bytes on `.188`, magic
`d00dfeed`, `totalsize` matching the file size exactly — so it is complete and parseable by
`usb_host/uimage.py`'s walk). ⚠️ **`find /proc/device-tree -name X` silently finds nothing**:
`/proc/device-tree` is a *symlink* to the sysfs path and `find` does not follow it. Use the
`/sys/firmware/devicetree/base` path. That is how `mode = <3>` was confirmed against the running
kernel rather than against the decompiled `usb_host/original.dts` — and how `mode` was measured to be
the **only** property of that name in the whole tree.

**`/etc/init.d/usb-host recover`** does the rebind — unbind, settle `RECOVER_SETTLE` (2 s) so VBUS can
decay below VBusValid, bind — and retries up to `RECOVER_TRIES` (3), stopping the moment a **non-hub**
device appears and exiting non-zero on exhaustion. Plug the device in **first**. Reachable from the panel
as Device Tools → USB → **RESCAN**, which forks it when a scan finds nothing. **Measured on `.188`
2026-08-14:** one tap, with a pad attached and dark on a port reading `Vbus off`, took ~5 s and left
`Vbus on`, `1-1` present, `event1` + `js0` created and the pad playable. ⚠️ It is deliberately not on
a timer, and the reason is not merely the wasted rebinds: **nothing in software can distinguish "nothing
is plugged in" from "a pad is plugged into an unpowered port"** — VBUS is off either way and no connect
interrupt can arrive in either — so an operator who has just plugged something in holds the one bit no
poll can obtain.
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

**The backlight ceiling is 100, and a cleaned unit is already sitting on it.** `max_brightness` is 100
and a write above it does not raise the duty — `echo 150` reads back `100` — which is why the `150` in
the vendor's own commented-out `backlight.sh` line was never brighter than `100`. Measured on `rwtest`
2026-08-06 with a temporary `rc5.d` `S01` probe, i.e. before `sshd` and long before `roomwizard-app`: a
freshly booted **cleaned** unit reads **100 of 100** with nothing in our stack having written it —
`app_launcher` makes no `hw_set_backlight()` call at all. The vendor's entire mechanism
(`adjustbklight.sh` → `setbacklight.sh` / `backlight.sh -1`) writes **this one node** from
`websign/brightness.conf` and **defaults to 100** when that file is missing, which is the state our
clean leaves behind. So a cleaned unit is already at the brightest state the vendor firmware could
reach, and a boot-time setter would write 100 over 100.

⚠️ **"The panel looks dim" is therefore not a software question — at a fixed duty cycle, perceived
brightness follows what is *drawn*.** The launcher grid measures **19.2 % mean luminance**, 87.5 % of
its pixels in the darkest quarter (32bpp capture, 2026-08-06); the vendor's browser filled the same
panel with a near-white page. A dark UI at full backlight looks dimmer than a white page at the same
full backlight, and no write to `brightness` closes that gap. Judge a brightness claim with **identical
content on both panels**, or it measures the UI's palette rather than the hardware.

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

Working correctly — `hwclock -r` matches `date`, and `commissioning/provision.sh` already does `hwclock -w`
after `rdate`.

**Hold-up is a supercapacitor, not a battery.** `U17` is a Panasonic/Matsushita **"Gold Cap"
5.5 V 0.47 F** supercap (`GC5.5V0.47F`). MADC channel 9 reads **3184 mV**, meaning the cap is
charged — not that a cell is healthy.

- **Nothing to replace on a schedule and no leakage risk** — there is no chemistry to exhaust.
  Supercaps age (ESR climbs, capacitance falls) over decades, not years. One less worry on
  eight-year-old hardware.
- **But hold-up is hours-to-days, not months.** **Expect the clock to be wrong after any extended
  unplugged period.** That is why `commissioning/provision.sh` does time-sync at boot, and it means anything
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
power patch and is **unproven**. Proposal: `IMPROVEMENT_PLAN.md` F5. Socket pinout and the measured
3.3 V rail: [Unpopulated and expansion](#24-unpopulated-and-expansion).

**Expect the vendor to have assumed a Series 1 module.** A settable `ATMY` and `ATCH` are 802.15.4
(Series 1) commands. On a Series 2 / ZB part `ATMY` is **read-only** and `ATCH` only *reports* the
operating channel — so an S2 module answers `+++` and `ATID` but gives a partial response to the
rest. **Do not read that as a wiring fault**; check the module label first.

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

**`disable-steelcase.sh` handles this**, not `commissioning/provision.sh` directly: it creates
`/var/watchdog_test` and **replaces the whole crontab** with the two cleanup jobs worth keeping
(`rotatelogfiles.sh`, `cleanupfiles.sh`). It does *not* comment the line out and does *not* back the
original up — an earlier revision of this section claimed both. Commenting out was the old approach
and was abandoned because it re-appended its own header on every run and inflated the crontab to
~19 KB; the factory crontab's content is recoverable from the partition images under `partitions/`
if it is ever wanted.

Two consequences of *where* that script runs, both of which have bitten:

- `commissioning/provision.sh <ip>` is what **deploys** it (to `/opt/roomwizard/`) and runs it once, and
  `/etc/init.d/roomwizard-app` runs it again **on every boot** as a safety net. So a device can be
  running a copy older than the repo's until `commissioning/provision.sh` is re-run — measured on RW09
  2026-08-03, where the deployed copy predated a bug the repo had already grown.
- Because it runs unattended at boot and nothing checked its exit status, a failure was invisible.
  Under `set -e` an unguarded `sed` on `/etc/profile` used to run *before* the `touch`, so a device
  with no `/etc/profile` kept the watchdog armed and rebooted every ~70 minutes with no diagnostic
  anywhere. The bypass is now the script's first command and it reports the bypass state on its last
  line ([`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md) B18, done 2026-08-03).

### 3.14 What is not present

**Confirmed absent:**

- ❌ **WiFi / Bluetooth** — no radio of any kind fitted. A USB Bluetooth dongle is the only route, and
  `CONFIG_BT` is unset ([§3.6](#36-usb), [`IMPROVEMENT_PLAN.md` F17](IMPROVEMENT_PLAN.md#f17-bluetooth-peripherals-and-whether-usb-dma-is-reachable--open-measured-2026-08-08)).
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
- ⚠️ **USB gadget mode** (presenting the device *as* a USB keyboard, serial port or ethernet
  adapter) — **`CONFIG_USB_GADGET` is not set**. The MUSB controller is dual-role capable and the
  micro-B port is the one physical socket, so this is a config-only block, but still a rebuild.

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

**What actually occupies each partition** — measured 2026-08-05 on the second captured card, stock and
uncommissioned. `/home/root/{data,log,backup}` are **mount points**, so a rootfs mounted offline shows
all three empty; the vendor's config and logs are on the other partitions.

| Partition | Used | Largest items |
|---|---|---|
| p6 `/` | 379 MB | `/usr` 223 MB (of which `/usr/lib` 138 MB, `/usr/share` 60 MB) · `/opt` 142 MB (`openjre-8` 93 MB, `jetty-9-4-11` 43 MB). **Everything outside `/usr` and `/opt` totals 15 MB**: `/lib` 5.4, `/sbin` 3.9, `/bin` 3.7, `/etc` 1.6 |
| p2 `/home/root/data` | 144 MB | `cron/` **131 MB** — a *log*, not the spool (`commissioning/provision.sh` truncates it rather than deleting the directory) · `test.hex` 10 MB · `websign/` 220 KB, the network config of [§3.5](#35-network-and-power) |
| p3 `/home/root/log` | 31 MB | `jetty_logs` 18 MB · `browser.err` 8 MB · `messages` 4 MB |
| p5 `/home/root/backup` | 492 MB | `factory/` **472 MB** — vendor upgrade/restore images plus `.md5` files · `websigns/` 15 MB |

**`/usr/lib`'s 138 MB is a kiosk-browser stack that nothing in this project uses.** `libwebkit2gtk`
36 MB, `libicudata` 27 MB, `libjavascriptcoregtk` 9.5 MB, `libgtk-3` 5.9 MB, plus `webkit2gtk-4.0/`,
`xorg/`, `X11/`, `gstreamer-1.0/`, `gdk-pixbuf-2.0/` and a spell checker (`aspell-0.60/`,
`enchant-2/`); `/usr/share` carries its `X11/`, `fonts/`, `themes/`, `icons/`, `fontconfig/`. The
matching service is `/etc/init.d/browser`, and `Xorg.0.log` + the 8 MB `browser.err` on p3 are its
output. Every component here draws straight to `/dev/fb0`, so none of it is linked or loaded.
`/usr/lib` also holds `libpython3.8`, `perl5/` and `ts/` (tslib) — worth keeping, unlike the above.

**The vendor's upgrade machinery is on disk and its payload is p5's `factory/`:**
`/etc/init.d/startautoupgrade`, `/opt/sbin/upgrade_logger.sh`, `IsUpgradeRunning` on p5, and the
litter of `upgradeProgressListener_upgradeStatus=*` files dropped in `/` show it has run. Whether it
can still fire unattended has **not** been established — but deleting `factory/` removes the payload
it would need, which is why that deletion is a safety measure and not a space measure
(`IMPROVEMENT_PLAN.md` F10).

**The layout is the identity; the UUIDs are not.** Measured across two units of the same firmware
build (`/etc/version` `20180309123456`), the partition table is byte-identical — same start sector
and same size for p1, p2, p3, p5 and p6:

```text
p1 : start=      63, size= 144522, type=c, bootable
p2 : start=  144585, size= 514080, type=83
p3 : start=  658665, size= 498015, type=83
p4 : start= 1156680,               type=5    (extended container)
p5 : start= 1156743, size=2939832, type=83
p6 : start= 4096638, size=2008062, type=83
p7 : start= 6104763,               type=82   (swap)
```

p4 and p7 sizes differ between units — they absorb the difference in physical card size (two nominal
4 GB cards measured 3.71 and 3.86 GiB), so **do not pin them**.

⚠️ **Every filesystem UUID differs per unit — all four of them.** A UUID is assigned at mkfs time and
units are mkfs'd independently at the factory, so two RoomWizards on identical firmware share none:

| | p2 | p3 | p5 | p6 (`/`) |
|---|---|---|---|---|
| RW09 | `d5758df8` | `da4cda60` | `26a7a226` | `108a1490` |
| a second unit | `2932baf1` | `d4d5ddbd` | `0f17045a` | `8acefb02` |

**Nothing on the device consumes a UUID.** U-Boot passes `root=/dev/mmcblk0p6` (compiled in, and
there is no `saveenv` — §4.4), and `/etc/fstab` names `/dev/mmcblk0p{2,3,5,7}`. Both are by position,
so p6 *is* the rootfs and cannot be renumbered. Host tooling identifies a card by layout and a rootfs
by content — `lib/rw-identify.sh`, and `COMMISSIONING.md` → *Finding the card*. A hardcoded UUID
recognises exactly one unit, and assigning it to a second card so it "matches" produces two cards
claiming one UUID.

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
  why the DTB patch works at all. Both CRCs, the header's own size field and the `power` value are
  checkable in pure Python with `usb_host/verify_uimage.py`; **neither `mkimage` nor `dtc` is
  installed in this WSL and neither is needed.** ⚠️ The data CRC must be recomputed *before* the
  header CRC — the header carries the data CRC, so the other order signs a header that is already
  stale, and these two CRCs are the only thing standing between a bad write and a unit that does not
  come up with no serial console to say why.
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

⚠️ **One deliberate exception, and it costs Layer 1 on that unit: the USB 500 mA patch.** `uImage-system`
is the only file `bootcmd` will load and U-Boot has no `saveenv`, so a unit that comes up at 500 mA by
itself requires patching that name in place — `IMPROVEMENT_PLAN.md` F15 has the decision and its accepted
cost. `lib/rw-usbpower.sh` is the only writer, and the in-place remedy is **`uImage-system.vendor`** on
p1, which it creates and md5-verifies (`edc637ac14f90e0187b1ed65ffedf6d7`) *before* touching the
original:

```sh
# On the device, or on the card in a reader — same two commands either way.
mount -t vfat /dev/mmcblk0p1 /tmp/bootpart
cp /tmp/bootpart/uImage-system.vendor /tmp/bootpart/uImage-system
sync; umount /tmp/bootpart
# Confirm before rebooting: this md5 is the vendor kernel, byte-identical on every
# unit measured (§3.6). a1fd1af8da18c430a34b24762aa16dab is the 500 mA one.
md5sum /tmp/bootpart/uImage-system
```

**Layer 2 — pull the card.** The whole system is on removable microSD (`mmcblk0`, root
`mmcblk0p6`). `dd` a known-good backup back, roughly 10 minutes. **This is the working recovery
loop for this project:** pop the card, reimage, set up DHCP, SSH back in. ⚠️ It is also the *only*
recovery from a bad `uImage-system` if `uImage-system.vendor` is gone too — nothing runs before the
kernel loads, so there is no SSH and no serial console to fix it from.

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
are simply *absent* without being disabled properly. `commissioning/provision.sh <ip>` does all of this.

**Result: ~80 MB RAM freed, no unwanted reboots, stable game mode.** Optionally
`commissioning/provision.sh <ip> --remove` deletes the bloatware (~178 MB, and removes a vulnerable
Jetty/HSQLDB/Java stack); `--deep-clean` frees ~560 MB more.

**Why cleanup this aggressive is safe: every binary we ship is `-static`** (see
[Building for this device](#6-building-for-this-device)). Nothing we run depends on a shared library,
an interpreter or a runtime that a deletion could take out from under it, so the blast radius of
removing a package is limited to the vendor software that used it.

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
| `roomwizard-app` | App launcher respawn service (`S99`, runlevels 2–5) |
| `rotatelogfiles.sh` (cron, 4 h) | Log rotation — prevents disk fill |
| `cleanupfiles.sh` (cron, 4 h) | Temp file cleanup |

**The boot links a working unit actually has** — read from a unit in service on 2026-08-05 (vendor
bloatware removed, games running, 4 days uptime) and re-read on a second unit the same day, which
matches it link for link. This is the empirical keep-list: everything else under `rc5.d`/`rcS.d` on a
stock card can go, which is what lets the cleanup be a **whitelist** rather than a growing list of
vendor service names. `device-files/clean-rules.conf` is that whitelist, transcribed from this table.

| Directory | Links |
|---|---|
| `rcS.d` | `S02banner.sh` `S03sysfs.sh` `S04udev` `S05modutils.sh` `S06alignment.sh` `S06devpts.sh` `S10checkroot.sh` `S30procps.sh` `S30ramdisk` `S35mountall.sh` `S37populate-volatile.sh` `S39hostname.sh` `S40networking` `S43syslog` `S45mountnfs.sh` `S55bootmisc.sh` `S99finish.sh` |
| `rc5.d` | `S02dbus-1` `S09sshd` `S20cron` `S28time-sync` `S29audio-enable` `S40ctrlblk` `S50watchdog` `S99roomwizard-app` (+ `S89xpad-modules` `S90usb-host` where `usb_host` is installed) |
| `rc2.d`–`rc4.d` | `S02dbus-1` `S09sshd` `S20hwclock.sh` `S40ctrlblk` `S50watchdog` `S99roomwizard-app` `S99stop-bootlogd` |
| `rc0.d`, `rc6.d` | `K09sshd` `K20dbus-1` `K20hwclock.sh` `K20psplash` `K20wpa_supplicant` `K31alsa-state` `K85watchdog` `S20sendsigs` `S25save-rtc.sh` `S31umountnfs.sh` `S38urandom` `S40umountfs` `S90halt`/`S90reboot` |

⚠️ **`rc0.d` and `rc6.d` are shutdown, not startup — never clean them.** They carry `umountfs`,
`sendsigs` and `save-rtc.sh`; a unit that cannot unmount cleanly is a unit whose next fsck is not
optional. `rw_clean_validate` rejects a rules file that even names them, so they are unreachable rather
than merely unvisited.

**`S30avahi-daemon` is absent** on both units while `/usr/sbin/avahi-daemon`, `/etc/avahi/` (three
entries) and `/etc/init.d/avahi-daemon` are all present — mDNS is enabled by a link `commissioning/provision.sh`
adds, and all four paths are `keep` entries in `clean-rules.conf` so that no clean can delete what
setup enables.

⚠️ **`/var/log` is a symlink to `/home/root/log`, i.e. p3, and `syslogd` holds three files there
open.** Measured from `/proc/<pid>/fd` on a unit in service, 2026-08-05: `messages` (its target per
`/etc/syslog.conf`), `upgrade.log` and `concurrent.log`. **Unlinking any of them on a running device
leaves `syslogd` writing to an unlinked inode and logging silently stops until reboot** — so
`clean-rules.conf` keeps and truncates them rather than deleting them, and the same applies to `wtmp`
and `lastlog`, which `sshd` and `login` hold open. Offline the symlink dangles, so a rule naming
`/var/log/anything` reaches nothing at all; name the p3 path.

⚠️ **The root crontab lives on p2, reached through a symlink.** `/var/cron/tabs/root` is a symlink to
`/home/root/data/cron/tabs/root` (measured on the unit in service, symlink dated Jan 2022). Two
consequences that matter to any cleanup:

- **`rm -rf /home/root/data/cron` destroys the crontab and cron's spool root**, not just the 131 MB
  `log` file inside it. `commissioning/provision.sh --remove` truncates the log for exactly this reason, and
  `clean-rules.conf` keeps the directory and **truncates** both the crontab and the log — cron itself
  stays running, because the two surviving jobs are in this table's *Kept* list.
- **Offline, the crontab is on a different partition from `/var`.** A tool that mounts only p6 sees
  `/var/cron/tabs/root` as a dangling symlink and `/home/root/data` as an empty mount point, so it can
  neither read nor write the crontab. The four mounts of [§4.2](#42-partitions) are not optional.

The two surviving jobs on that unit are `rotatelogfiles.sh` and `cleanupfiles.sh` at `0 */4` and
`5 */4`, in a crontab whose header reads `# RoomWizard crontab - managed by disable-steelcase.sh` —
i.e. the crontab `disable-steelcase.sh` writes, not the vendor's.

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

**Seeing what is running: use `ps`, never `ps w`.** This busybox (v1.31.1) treats `ps w` as
"processes with a controlling TTY", which on RW09 is **3 lines against plain `ps`'s 51** — the two
gettys and the header. An app started at boot or by the launcher has no TTY, so `ps w` shows nothing
and the process looks absent. Measured 2026-08-03, and it is why a surviving `vnc_client` took a
session to find (`IMPROVEMENT_PLAN.md` B25). What works:

| Want | Command |
|---|---|
| everything | `ps` (shows `comm`, i.e. the **executable's** basename) |
| the RoomWizard apps, with exe and cmdline | `/etc/init.d/roomwizard-app status` |
| find one by name | `pidof <basename>` — matches `comm` **or** `basename(argv[0])` |
| what a PID really is | `readlink /proc/<pid>/exe` |

`comm` comes from the file being executed and **not** from `argv[0]`, so it is trustworthy even when a
parent sets a display title: `killall`/`pidof` match a process whose `cmdline` reads `VNC Client`.
`/proc/<pid>/cmdline` is the one that can lie about identity.

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

**Checking a binary — the expected count is a hard zero.** Use
[`native_apps/check-arm-safe.sh`](native_apps/check-arm-safe.sh), which
`build-and-deploy.sh` runs before every deploy and on build-only runs too. It reports zero across all
**31** ARM build artifacts. It skips anything whose `objdump -f` architecture is not ARM and says how
many it skipped — `build/` also collects host-gcc test binaries, and under WSL every file on `/mnt/c`
looks executable, so a gate that does not filter counts files it cannot actually disassemble as
evidence that it passed.

**Where each component's gate runs, and why ScummVM's is where it is.** All three now gate, but not at
the same point:

| Component | Gate site | Artifact checked |
|---|---|---|
| `native_apps` | after the build, before deploy **and** before `--bundle` | all 31, unstripped (nothing is stripped) |
| `vnc_client` | after `make`, before deploy and before `--bundle` | `vnc_client`, deliberately **not** `vnc_client_stripped` |
| `scummvm-roomwizard` | inside `strip_binary`, **before** the `strip` runs | `scummvm`, unstripped |

ScummVM's is inside `strip_binary` because `arm-linux-gnueabihf-strip scummvm` strips **in place** — no
unstripped copy survives it, so that function's first half is the only moment the gate has a readable
artifact. A gate added anywhere later would be checking a stripped binary, which is the second wrong
answer below.

Two ways to get a wrong answer out of this check, both measured on this repo. First, matching too
loosely:

> ⚠️ **Do not match the line; match the tab-delimited mnemonic field.** A bare `grep 'sdiv\|udiv'`
> matches the *substring* `udiv` inside the **names** of the software-divide helpers — `__udivsi3`
> (×20), `__udivmoddi4` (×6) and their call sites. Those are symbol names and branch targets, not
> instructions, and their presence is positive evidence that division is being done in software.
> `libgcc.a` on this toolchain contains **zero** hardware `sdiv`/`udiv`. There is nothing to
> allowlist, and any hit from a correctly-matched gate is real.

Second, feeding it an artifact it cannot read correctly:

> ⚠️ **Gate the *unstripped* artifact.** `objdump` needs the symbol table to tell **Thumb-2 from ARM**,
> and these binaries are Thumb-2. Stripped, it falls back to 32-bit ARM and re-reads the same bytes as
> ARM words, manufacturing divides that are not in the file. Measured on `samegame` 2026-08-08:
> `objdump -s` prints `4846ebf7 1bfe3de7` at `0x42618` for the unstripped file *and* the stripped copy —
> `strip` cannot alter `.text` — but unstripped that is `mov r0, r9` / `bl …` / `b.n …`, while read as ARM
> the second word alone becomes `e73dfe1b` = `udiv sp, fp, lr`. Neighbouring lines decode as
> `<UNDEFINED>` and `sbcsne pc, r1, …`, which is the signature. The same effect gives ScummVM **9**
> phantom hits and `vnc_client` **1**. The phantom operands are **not** reliably invalid:
> `udiv pc, fp, sl` is dismissible, but `udiv r7, r1, lr` is a legal encoding indistinguishable from
> compiler output — so eyeballing operands is not triage, the symbol table is the only thing that
> settles it. Offsets cannot be allowlisted either: `base/version.o` re-embeds the build date on
> every link, which moves every address after it.
>
> So the gate **refuses to judge** a stripped target rather than reporting a hit for it — and rather than
> skipping it quietly, which is the same defect from the false-negative side. Three outcomes:
> **0** clean, **1** a real hit, **2** something could not be judged; the last line is always
> `ARM-SUMMARY checked=N unverified=N bad=N skipped=N`. ⚠️ **Do not read that status through `xargs`** —
> it maps any 1–125 onto its own 123 and erases the difference between 1 and 2.

The check itself:

```bash
arm-linux-gnueabihf-objdump -d BIN | awk '/\t(sdiv|udiv)(\.w)?\t/ {print}'
# Expected output: nothing.
```

What must hold is: **no `sdiv`/`udiv` instruction anywhere in the binary.**

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
