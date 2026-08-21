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

**Provenance.** Software facts were read off live units (primarily RW09, `192.168.50.73`), plus
on-device measurements across two further units. The hardware facts here inherit the teardown and the
continuity measurements recorded in [`HARDWARE.md`](HARDWARE.md), which also holds the photos.

---

## Contents

1. [Read this first](#1-read-this-first)
2. [The board](#2-the-board) — the hardware itself is [`HARDWARE.md`](HARDWARE.md)
3. [Subsystems](#3-subsystems)
4. [Boot chain and recovery](#4-boot-chain-and-recovery)
5. [Software stack](#5-software-stack)
6. [Building for this device](#6-building-for-this-device)
7. [Kernel policy](#7-kernel-policy)

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

### How to read a claim in this document

**Every statement here is measured on this hardware unless it carries a tag**, and there are three:
**`[inferred]`** — derived from a datasheet, a driver source or another measurement, but not itself
observed (a good pedigree is not a measurement: two inferences read straight out of the kernel source
were refuted on hardware in one afternoon); **`[unverified]`** — nobody has tried it, no pedigree at all;
**`[n=1]`** — measured once, or on one unit, so the *shape* usually generalises and the digits do not.
⚠️ **Anything needing a test to resolve is an open item in `IMPROVEMENT_PLAN.md`, not a hedge here.** A
tagged claim is one this document is content to carry untested; a hedge is work nobody has filed.

---

## 2. The board

The board as a physical object — the parts inventory with markings, every connector and what it goes
to, the unpopulated `J5`/`J6` XBee socket with its measured pinout, the `P4` RS-232 console and `P3`
JTAG headers, the enclosure and how it comes apart, and the teardown photos placed beside the parts
they show — is [`HARDWARE.md`](HARDWARE.md).

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

**Sequential read is ~11.4 MB/s — measured on `.188`, 2026-08-20**, reading a 3,898,628 B file whole.
That settles whether an asset can be *streamed* off the card rather than loaded: uncompressed 44.1 kHz
16-bit mono audio needs 88.2 KB/s, i.e. **0.77 %** of it. ⚠️ **That is throughput, not per-read
LATENCY** — the half that lands in a render loop, which a streaming consumer must absorb with a
read-ahead buffer it refills only when dry.

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

`fb1` is the second framebuffer (`CONFIG_FB_OMAP2_NUM_FBS=2`, see above), and `/dev/video0`
(`omap_vout`) is the V4L2 *output* path, which accepts **YUV with hardware colour-space conversion**.
Both are untried. **[inferred]** `fb1` is the natural small-surface render target for a scaled overlay
(draw at 400×240, let the DSS stretch it), `omap_vout` is what would make a video player conceivable on
a part that could never software-decode one, and the DMA-channel error above may be what blocks the
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

> ⚠️ **Outer-band slope compression is NOT established, and nothing may be built on it.** One run's
> per-segment slopes suggested a ~12 % steeper interior; residuals against the interior line are
> **±80 raw** (≈±8 px of finger placement) with no consistent sign, which over ~100 px baselines accounts
> for ±8 % of slope on its own. One run cannot distinguish 12 % from noise. The repeated multi-target
> measurement that would settle it is `IMPROVEMENT_PLAN.md` B3c.

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
(`fb_set_bezel(fb,0,0,0,0)`) on the full 800×480 panel, so a drawn pixel *is* a panel pixel and both
lines are measured against the same premise:

| Step | Measures | How |
|---|---|---|
| `TAP` | line 1 | 11 targets × 3 taps, median; least-squares per axis from **interior targets only** |
| `CHECK` | — | the derived curve, per-axis dead-band verdict, edge-probe residuals; ACCEPT / REDO / RESET |
| `EDGES` | line 2 | numbered 2 px ladders at each panel edge; raise each margin until its line clears the plastic |
| `REACH` | line 3 | slide a finger along each of the four edges; 16 coverage cells per edge, all four live at once. An unswept edge falls back to the hardware limit; a *completed* sweep that fell short widens the reported band |
| `REPORT` | — | visible rectangle vs touch-safe rectangle and the per-side inset in px, with the reminder that the band is still drawable. Amber on **magnitude** (`DISP_INSET_SUSPECT`, 24 px), not on "non-zero" |
| `CONFIRM` | — | goes live on the new mapping with a 20 s countdown; reverts unless you press KEEP |

`CALIBRATE TOUCH` runs the whole thing, `SCREEN EDGES` jumps to the `EDGES` step for a margins-only
tweak, and `RESET` restores the hardware `EVIOCGABS` range and the default margins. ⚠️ **Nothing is
written until CONFIRM, and the wizard hit-tests its own buttons through the *entry* calibration until
then** — so a bad fit can never leave you unable to press the button that rejects it.
`touch_calib_backup()` copies the old file to `.bakN` first.

> **One flow and one fit, deliberately.** This was two flows plus a third copy of the same defective fit
> in a standalone `unified_calibrate`, and they disagreed. ⚠️ **An edge adjuster that draws its reference
> frame on the *logical* edge is measuring the bezel through the bezel**, and a crosshair inset only
> 40 px sits inside the band where raw compresses, so its fit comes out shallow.

**One implementation of the fit, in `native_apps/common/touch_calib.c`** — the target set, the per-axis
interior masks (≥100 px from each end on X, ≥80 px on Y), the least-squares, the unclamped endpoints, the
per-axis dead-band verdict, the reach→inset calculation, the edge-sweep accumulator, the sanity gate and
the `.bakN` backup. Both the wizard and `touch_raw` link it, so the diagnostic validates the very code
the wizard calibrates with. ⚠️ **The fitted endpoints must never be clamped into `0..4095`** — a correct
fit on this panel legitimately extrapolates outside it, and the clamp that used to do it asserted raw
4095 is emitted at panel 479 when it is emitted at panel ~450, running the reported position **ahead of
the finger by up to +19 px across the bottom quarter**. The library rules, the deleted legacy-migration
clamp and the sanity gate's actual criterion are `native_apps/CLAUDE.md` → *Touch model*.

**`touch_raw`** (`native_apps/tests/touch_raw.c`, deployed to `/opt/games/`, hidden from the
launcher; reachable from Device Tools → Display → `TOUCH DIAGNOSTIC`) is the diagnostic that settled
reach, and the only tool that shows the panel with **no calibration and no bezel**: it resets the raw
range to the `EVIOCGABS` values and calls `fb_set_bezel(fb,0,0,0,0)`, so the dot is
`raw × 799 / 4095`. It logs to `/tmp/touch_raw.tsv` with a monotonic millisecond column and can write
the interior fit to line 1 after a `.bakN` backup. Four modes, and the split between the middle two is
the whole point:

| Mode | Question it answers | Method |
|---|---|---|
| `LIVE` | free tracking | crosshair, trail, session extremes, pin flag when raw sticks at a hardware limit |
| `SWEEP` | *what raw does the physical edge emit?* | slide a finger along each edge; per-bucket extreme along the edge, so a corner reading differently from the middle shows up instead of averaging away |
| `INSET` | *where does raw first reach that value?* | tap a bar walked inward from each edge at 0/10/20/35/55 px; the answer localises where the flat band starts |
| `TARGETS` | the interior line | 11 targets × 3 taps, then a hard press on each bezel; fits interior-only vs all-points and reports what each predicts at the edges, per axis |

⚠️ **Only `INSET` decides the curve.** `SWEEP` alone cannot: a finger sliding along an edge yields no
position information, so it reads identically whether clipping begins at the edge or 30 px inside it,
and conflating the two questions kept the endpoint bug alive across three sessions. Nor may a slope come
from two adjacent `INSET` bars — that rule and its numbers are `native_apps/CLAUDE.md` → *Touch model*.

**ScummVM and `vnc_client` each link their own copy of `touch_input.o`** — rebuild and redeploy both
after changing that file or its touch silently goes stale (see *Reach* below for how silently).

Accuracy: ~3 px at centre, 14–27 px error at the corners before calibration.

**Reach at the border — every edge drives raw to its limit, but not at the edge.** Settled on
2026-08-01 with `touch_raw`'s `SWEEP` and `INSET` modes, with the calibration and bezel zeroed so a
drawn pixel is a panel pixel. Raw capture:
[`touch_raw-2026-08-01-rw09.tsv`](touch_raw-2026-08-01-rw09.tsv), 16:53.

> **Every number below is the reference capture, not the live calibration of any unit** — the fit is
> re-run per unit and per wizard run (see *Provenance* at the end of this section for what RW09 carries).
> What generalises is the *shape*: linear interior, a saturated band inside each Y edge, a much smaller
> one on X. Not the digits. The host regression replays this capture's medians rather than reading a
> device, which is what makes it a regression and not a snapshot.

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
distinguished by this data and does not matter for calibration. (Two fits of that panel are quoted in
this section — the one above and `X 17..4084 / Y -279..4382`, which the host regression replays. They
differ by ≤7 raw, an order below the ±80 raw tap residual.)

**Consequence: drawable ≠ pressable, and a dead band is a fact about this panel rather than a bug.** The
band at each end of Y is visible and fully drawable but cannot be pressed, so two rectangles exist —
`SCREEN_VISIBLE_*` (the full logical screen) and `SCREEN_SAFE_*` (visible ∩ touchable). On the reference
capture at bezel T=11 B=14 the inset is **~17 px top / ~16 px bottom** with X ≈ 0; RW09's live
calibration gives 19/16 **and ~6 px on each side of X** (see *Provenance*). **X's band is much smaller
than Y's and can be zero, but a non-zero X inset is not a fault** — it arises by exactly the same
mechanism as Y, from fitted X endpoints falling outside `0..4095`. Treat every one of these numbers as
per-unit and per-calibration.

The inset is **measured at runtime, never hardcoded** — the four raw edge extremes are pushed through the
production mapping, so it is correct in portrait and under any bezel and is `0` until an edge sweep has
been recorded. Read it from Device Tools → Display → `TOUCHABLE:`, the wizard's `REPORT` screen, or the
display test's `SAFE AREA` page (red rect = visible, green = touchable). Which rectangle a call site
wants, the cap on the inset and the drawing policy are `native_apps/CLAUDE.md` → *Screen edges*.

**The mapping lives in the config file, so deploying code never fixes a bad stored curve.** Line 1 of
`/etc/touch_calibration.conf` is what `touch_init()` uses; a unit whose line 1 was written by older
code keeps that behaviour, symptom intact, across any number of correct deploys until the wizard is
re-run. Corollary for handovers: "the fix is deployed" and "the device behaves correctly" are separate
claims.

**Provenance — what a live unit carries.** The figures above are the 16:53 diagnostic run; RW09's stored
config is a *different* fit of the same panel, from the wizard run that was kept at 18:50 the same day:

```text
line 1  -33 1007 3087 4122   -296 875 3217 4379
line 2  11 14 0 0
line 3  reach 0 4095 0 4095    -> published inset: X 6..793  Y 19..438  of 800x455
```

`reach 0 4095 0 4095` means the sweep found the hardware limits on all four edges — the "assume the
hardware limit" case, contributing *nothing* — so this unit's entire published inset comes from the fit's
endpoint overshoot (`-33`/`4122` on X, `-296`/`4379` on Y against a `0..4095` hardware range), which is
the model working as designed. ⚠️ **The inset and the ~30/29 px flat band are therefore not the same
measurement and must not be reconciled:** the inset is *logical rows the current curve cannot address*,
the flat band is *panel pixels over which the sensor's reading is saturated*, measured by `INSET` with no
calibration in the path. Both are true at once, of different things. No `touch_raw` capture exists for
the 18:50 calibration — the wizard writes the config, only the diagnostic writes `/tmp/touch_raw.tsv`.

⚠️ **A bezel press cannot locate where clipping starts** — raw hits `4095` whether that is panel 479 or
panel 450 — and nothing inferred *through* a calibration can measure that calibration. Every dead-band
figure this document carried before `touch_raw`'s `INSET` mode existed was wrong for one of those two
reasons, twice in *opposite* directions (a "~11 mm shorter electrode array" computed by mapping bezel
presses through the fit under test; then "every visible pixel is touchable, so a dead band is a bug",
whose clamp made the bottom edge measurably worse). **Design the measurement to answer one question and
state what it cannot answer.**

Two secondary effects, measured at the same time:

- **Finger-centroid scatter is real, and it shifts the intercept rather than the slope.** Nine hard
  presses on the top bezel returned raw `22, 76, 105, 111, 93, 79, 90, 118, 47` — pressing flat near the
  saturation zone gives a tall contact patch whose centroid is pushed inward, and it scatters widely.
  Prefer target taps for anything quantitative; use a bezel press only for the yes/no question "does raw
  reach its limit here?".
- **A stale binary misparses the config instead of failing.** A `vnc_client` built before the 8-number
  line 1 read `0 1020 3074 4095  0 874 3215 4095` as `X [0..1020] Y [3074..4095]`, confining touch to the
  left quarter and the bottom strip — presenting as "touch is broken in vnc_client" rather than as a
  version mismatch. ⚠️ **The discriminator is `(piecewise)` in the binary's `Calibration loaded from:`
  line**, which only 8-number-aware code prints. It is *not* the presence of
  `Touch raw range set (linear):` — that string is live in `touch_set_raw_range()`, the `EVIOCGABS`/RESET
  path, and a current binary prints it whenever the hardware range is set.

Every touch measurement in this section is **`[n=1]`** — RW09 only, and no second unit has been
swept. Open work: [`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md) B3c.

**Multi-touch exists in hardware but not in the driver.** `panjit_ts` reports only
`ABS_X`/`ABS_Y`/`BTN_TOUCH` with no MT slots. The controller itself is **2-point multi-touch with
on-chip gesture recognition** — the vendor factory-test binary `opt/pv02/pv02_app` reads
`Num_Touch` plus two coordinate pairs and exercises pinch-zoom, two-finger pan and multi-touch
click. Reaching it means bypassing the driver on `/dev/i2c-2`. Userspace-only, no kernel work.
Proposal: `IMPROVEMENT_PLAN.md` F6.

**Pressure is declared but untested.** `ABS_PRESSURE` appears in the device's capabilities
(`capabilities/abs = 1000003` → bits 0, 1, 24) and is discarded by `touch_input.c`. **[unverified]**
whether the value actually varies — `native_apps/hardware_test/pressure_test.c` is the unfinished probe,
and `IMPROVEMENT_PLAN.md` F6 carries it as the cheap first step.

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

**Native ALSA needs no kernel work and nothing shipped.** `CONFIG_SND`, `SND_PCM`, `SND_SOC`,
`SND_OMAP_SOC`, `SND_OMAP_SOC_MCBSP` and `SND_SOC_TWL4030` are all `=y`
(`usb_host/device_config:2711`, `:2713`, `:2757`, `:2778-2779`, `:2855`) — OSS is `SND_PCM_OSS` plus
`SND_PCM_OSS_PLUGINS` **emulation** (`:2718-2720`) layered on this same `rw20` card. Going native
removes a layer; it adds nothing to the kernel and carries no brick risk. Userspace is already
complete on a stock unit: `libasound.so.2.0.0` (**alsa-lib 1.2.1.2**), `aplay`, `amixer`, `alsactl`,
`speaker-test`, and 238 files under `/usr/share/alsa`. Only the **dev** side is absent — no headers,
no `.a`, no linker symlink — which is why a statically linked client that talks to the kernel
directly (tinyalsa) is the route rather than `-lasound`.

- ⚠️ **`CONFIG_SND_SEQUENCER` is not set** (`:2731`), so ScummVM's `--enable-alsa` is a trap: that
  flag is MIDI/sequencer support and cannot produce PCM output here. PCM has to be hand-written,
  which is why `oss-mixer.cpp` exists at all.
- **The deep clean is not a hazard here — checked, not assumed.** No `scope` sweep covers `/usr/lib`
  or `/usr/share` (all nine sweeps are `/etc/rc*.d`, `/opt` and the three `/home/root` trees), and no
  `delete` glob reaches `libasound`. `/usr/share/alsa` survives because nothing names it; the
  *intent* is recorded on `device-files/clean-rules.conf:356`, whose reason reads "NOT
  `/usr/share/alsa`, which the OSS shim needs".

**What `hw:0,0` actually grants — measured on `.188`, 2026-08-14**, with
`native_apps/tests/alsa_probe.sh`. That probe needs nothing cross-compiled: the vendor's `aplay`
has `--dump-hw-params`, which is the same `SNDRV_PCM_IOCTL_HW_REFINE` any client calls.

| Parameter | Range | Consequence |
|---|---|---|
| `CHANNELS` | **2, exactly** | ⚠️ stereo-only, see below |
| `RATE` | `[8000 96000]`, continuous | 8000/11025/22050/32000/44100/48000/96000 all granted **exactly** (`exact rate 22050 (22050/1)`), so no userspace resampling is ever needed |
| `FORMAT` | `S16_LE`, `S32_LE` | `S16_LE`, as today |
| `PERIOD_SIZE` | `[4 16384]` frames | ⚠️ the shim always takes 2048, see gotcha 1 |
| `BUFFER_SIZE` | `[640 32768]` frames | 13 ms … 683 ms at 48000 |
| `PERIODS` | `[2 255]` | |
| `ACCESS` | `MMAP_INTERLEAVED`, `RW_INTERLEAVED` | a plain `write()` loop is fine; no mmap needed |

⚠️ **`hw:0,0` is stereo-only. The hardware is mono; the interface is not.** `CHANNELS: 2` is a point,
not a range, and `aplay -c 1` is **refused at every one of seven rates**. **The speaker sums L and R —
measured, see below** — so mono remains the right *source* model, but the device boundary must hand the
kernel **interleaved stereo frames** with the sample duplicated into both. The interleave arithmetic is
*confined to that one place* rather than removed. Frame/byte confusion is the bug to guard against
(`bytes == frames * 2ch * 2B`), not the interleaving itself.

**`SPKR1` sees L + R, not L − R — measured on `.188`, 2026-08-14.** Three 48 kHz stereo tones identical
but for channel phase (`native_apps/tests/audio_phase_gen.py`, played by `audio_phase_test.sh`):

| Tone | L, R | Heard |
|---|---|---|
| `left` | `s`, `0` | audible — reference |
| `dup` | `s`, `s` | **slightly louder than `left`** |
| `anti` | `s`, `−s` | **inaudible** |

Complete cancellation on anti-phase is possible only if the speaker is driven by the **sum**. So
**duplicating a mono sample into both channels is correct, and is the loudest of the three options.**
⚠️ **"HandsfreeL/R class-D bridge" above means each amp is internally bridge-tied — it does NOT mean
`SPKR1` bridges L against R.** That ambiguity is worth spelling out because the wrong reading predicts
silence for exactly the write a mono backend performs, and the two readings are indistinguishable by
playing music: real stereo content never has `L == R`. The harness self-checks (equal peaks, and
`R==0` / `R==L` / `R==-L` in every loud frame) so that a loudness comparison cannot be measuring
differing content instead of phase.

- ⚠️ **The tones must be *self-identifying*.** The first attempt played all three back to back and the
  operator reported *"I think I heard only two"* — which cannot distinguish "one was silent" from "I lost
  count". `audio_phase_test.sh` precedes tone *N* with *N* marker clicks, which made the answer decisive.
- **Consequence for streamed stereo content**: a stereo file plays as an analogue `L+R` downmix. So
  storing music **mono on disk and duplicating at playback halves the file and the SD read bandwidth
  at no audible cost** — a mono `(L+R)/2` source, duplicated, reproduces what the speaker already does.
- ⚠️ **A pure sine at peak 18000 does NOT reproduce cleanly, and the ceiling is FREQUENCY-DEPENDENT — the
  driver is excursion-limited, not amplitude-limited.** Measured `.188` 2026-08-16/17 **[n=1, by ear against
  a signal generator]** through the vendor's `aplay`, so none of this repo's audio code is in the path. A
  peak-6000 *square* is harsh and audibly **louder** than the sine (3 dB more RMS at equal peak — the
  control showing the listener was discriminating). At 440 Hz the ladder is monotonic: 6000 clean, 8000
  *"woodwind"*, 12000 *"brass", harsher*, 18000 distorted. At a **constant** 18000 it instead **falls with
  pitch**: 220 Hz *"clearly a square wave"*, 440 Hz noisy, 1320 Hz *"cleaner"*. So **≈55 % is not a safe
  acoustic ceiling**, the overdrive is amp-or-cone below every digital stage, and ⚠️ **one global peak is
  the wrong shape of limit** — it over-quietens the nearly-clean band and still leaves 220 Hz square.
- ⚠️ **The usable BAND is narrow, and low pitches fail on LEVEL rather than on reproduction.** Sharp
  rolloff below ~700 Hz; below ~300 Hz inaudible at viewing distance, audible only with an ear at the
  panel; below 500 Hz hard to hear at the shipped level. A continuous glide is what measures this — one
  `aplay` per tone restarts the stream and cannot. **An effect centred below ~700 Hz is unusable on this
  hardware whatever its shape**, and everything audible in the tree today is ≥ 880 Hz.
- ⚠️ **TWO sustained sines INTERMODULATE, and no level law fixes it — the fix is source material.** Measured
  `.188` 2026-08-20 the same way, a **pre-mixed** two-sine WAV through the vendor's `aplay` with no mixer,
  generator or pump in the path: *"same distortion as on the device"*. 440 + 880 (peak 12287, RMS 6143) is
  far worse than a lone 440 (peak 16383, RMS 11585), so it is not level in either sense — what the ear gets
  is |m·f1 ± n·f2|, inharmonic to both inputs unless they are octaves. ⚠️ **Two BROADBAND streams at the
  same level are CLEAN** (`music1-mono` + `music2-mono` overlapping in full), so **effects must be
  broadband, not sustained tones, whatever their source**; per-frequency budgeting is comfort, not the fix.
- ⚠️ **The +12 dB of apparently unused analog gain is NOT headroom — spending it distorts.** `DAC1 Analog
  Playback Volume` (numid=6) sits at **12 of 18** on a −24 … +12 dB scale and `DAC1 Digital Fine` (numid=2)
  is already at 0 dB, so the codec looks able to pay for a lower digital peak. It cannot: measured `.188`
  2026-08-17 **[n=1, by ear]**, peak 6000 is clean at 0 dB analog and *"louder but distorted"* at +12 dB —
  **identical samples**, so the limit is **acoustic**, downstream of every gain stage the codec exposes.
- ⚠️ **The kernel will NOT mix — a second concurrent stream is refused.** One card, **one playback
  subdevice** (`/proc/asound/pcm`), and `default` is `plug` over raw `hw:0,0` (`aplay -v`; `/etc/asound.conf`
  is an empty comment), so a second `aplay` during a first dies `Device or resource busy` — measured `.188`
  2026-08-17. `aplay -D plug:dmix` **does** take two streams, but only via `libasound`, which ships
  **shared**-only. **Mixing two sounds is userspace work by construction, not an optimisation.**
- ⚠️ **Audio on this device cannot be measured acoustically — there is no microphone, and the codec's
  loopbacks run the wrong way.** `arecord` works and the card really does have a capture subdevice
  (`/proc/asound/pcm`), so this looks possible and is not: with `TX1 Capture Route` on `Analog`, both main-mic
  and AUXL capture switches on and gain at maximum, a recording is **railed 50 Hz mains hum**, and the 440 Hz
  bin is *identical* between a silent capture and one taken during a loud 440 Hz playback — measured `.188`
  2026-08-17, so there is no acoustic and no electrical coupling to find. Nor can the codec be turned into a
  loop: **both loopback families route capture → playback** (mic-to-speaker), not playback → capture
  (`../usb_host/linux-4.14.52/sound/soc/codecs/twl4030.c:1553-1579`). ⚠️ **So every timbre and loudness
  finding in this section is necessarily `[n=1, by ear]`, and no future session can promote one by
  instrumenting the device.** What *can* be measured objectively is the byte stream we send it
  (`native_apps/tests/oss_play.c --dump`), which is a different claim.
- ⚠️ **There is no AVLS, AGC or output limiter — and no external amplifier.** `SPKR1` is driven from the
  TPS65930 itself (`../HARDWARE.md`), and the driver's only playback-side dynamics are an **anti-pop ramp**
  on output enable (`handsfree_ramp()`, `headset_ramp()`, `twl4030.c:593-720`, ramping *up*) plus a one-time
  offset cancellation at init. So a *"starts distorted, then settles"* onset cannot be an AVLS.
- ⚠️ **Every audible click is a stream transition, not the samples, and there are TWO** — `.188` 2026-08-17,
  operator at the panel, each prediction stated before the listen, `[n=1, by ear]`. **(a)** A lone click ~5 s
  after the last sound is the ASoC power-down: `/sys/devices/platform/sound/TWL4030 HiFi/pmdown_time` reads
  **5000**, and writing a large value removes it — an 8 s gap then stays silent. **(b)** A click still ends
  every sound with the power-down held off, and it survives **400 ms of trailing silence appended to the
  file**, so it is the stream *stop* (DAI teardown, which `pmdown_time` does not guard), not a truncated tail
  — `oss_play.c:336` already drains with `SNDCTL_DSP_SYNC`. Heard as *"an old CB radio push-to-talk"*: a step
  through the class-D bridge. **Native ALSA clicks too** (*"clinking then a klack then a beep"*, below), so
  no userspace path avoids it — only not stopping the stream does.
- **The OSS shim adds no conversion when the parameters already match — it cannot reshape a waveform.**
  Every plugin in `snd_pcm_plug_format_plugins()` is gated on a mismatch (`sound/core/oss/pcm_plugin.c:414`
  onward: mu-law, channel reduction, resample, format), and with `S16_LE`/2/44100 granted at **both** layers
  — `SOUND_PCM_READ_*` on our side, `/proc/asound/card0/pcm0p/sub0/hw_params` on the slave's — none is
  built, so the write is a plain copy into the same ring `aplay` fills. `direct` is 0 here, but that only
  runs the *builder*, which then adds nothing. **[source-read, with both endpoint states measured]**
- ⚠️ **The production OSS buffer is 743 ms in 46.4 ms periods, and the shim grants 2048 frames and 16
  periods at EVERY rate and channel count tested** — measured `.188` 2026-08-17 and again 2026-08-18
  (`native_apps/tests/oss_geom.c`, both client configurations) with no `SNDCTL_DSP_SETFRAGMENT`:
  `period_size` **2048** frames, `buffer_size` **32768**, `fragsize` scaling with the frame size alone
  (8192 B stereo, 4096 B mono), against `aplay`'s 5512 / 27560. So at 22050 mono that same 2048 frames is
  **92 ms** and the ring **1486 ms**. It is what both consumers actually get.
- **The two consumers attenuate differently:** the native synth pins a peak of **18000**
  (`AUDIO_PEAK` in `native_apps/common/audio_gen.h`, a constant rather than a shift); ScummVM does `>>1`
  post-mix. The summing is why ~50 % looked about right, but the ladder above puts 18000 past clean at
  every pitch a canned sound uses, and no mixer control refunds the loudness that lowering it costs.
- ⚠️ **Digital attenuation inside the codec does not fix a distortion introduced upstream of it, and that
  is what makes it a probe.** Cutting `DAC1 Digital Fine Playback Volume` by 20 dB with `amixer` (numid=2,
  63 → 43) made the app's tone quieter with the timbre **unchanged** — measured `.188` 2026-08-16 — which
  localises a corruption **above** that control, where a cleaned-up timbre would localise it below.
  ⚠️ **On its own it localises nothing**, because a controlled comparison then found no corruption to
  localise. Restore with `amixer cset numid=2 63,63`.

⚠️ **A `-c 1` probe fails on CHANNELS at every rate, which reads as "no rate is accepted"** — that is how
`alsa_probe.sh`'s first run produced seven REFUSED lines from one mistake; the number was the harness. Same
cause: the shipped `/opt/sound/*.wav` are mono, so `aplay -D hw:0,0 asl_click.wav` fails and **that is not
a broken audio path** — use `plughw:0,0`, or generate stereo (`speaker-test -c 2`).

**Native ALSA is audible on hardware — confirmed by the operator at `.188`, 2026-08-14.** Both paths
of `alsa_probe.sh` step 7 exited 0 *and* were heard: the three mono WAVs through `plughw:0,0`, then a
440 Hz sine **straight at `hw:0,0`** — no plug, no conversion, the same path a tinyalsa backend takes.
Reported as *"some clinking then a klack then a beep"*. So the native path is proven end to end
independently of anything this project writes.

- The **"klack"** falls between the two, i.e. at a stream open — and a start-of-stream pop is now the
  *measured* explanation of the ~60 ms minimum-tone rule rather than a plausible one: see gotcha **6**
  below, where keeping a stream continuously fed drops the audible floor to 5 ms.

⚠️ **Native ALSA's latency win over the shim is ~2× at the period, not the ~24× a 506 ms period would
imply.** `period_size=1024, buffer_size=4096` is **granted exactly** at 48000 (`period_time: 21333` µs)
and at 22050; vanilla `omap-pcm.c:49-52` (`period_bytes_min = 32`, `periods_min = 2`,
`buffer_bytes_max = 128 * 1024`) puts the floor at 8 frames and the ceiling at 32768. **A client that
does not ask for a small period gets a big one, and the shim cannot ask because
`SNDCTL_DSP_SETFRAGMENT` is ignored** — but it settles for 2048 frames, so it is 21 ms against 46 ms.

**Gotcha — the OSS shim is buggy, in four distinct ways.** All of these are in `snd-pcm-oss`
emulation, not the hardware. ALSA itself works correctly.

1. **A blocking `write()` stalls for hundreds of ms — the ~506 ms figure is that STALL, not a period.**
   The period is 2048 frames (46 ms) and the ring 32768 (743 ms), both measured twice (above), so a
   blocking write waits on the RING draining rather than on a period; the exact 506 ms is unaccounted
   for, and the ~22,317-frame "period" it was read as reproduces in **no** configuration `oss_geom.c`
   tested. Measured effect: 185 ms of audio, 321 ms of silence, repeating — the "bru-bru-bru-KLICK"
   artifact, diagnosed with `native_apps/tests/oss_diag.c`. **Always open `/dev/dsp` with `O_NONBLOCK`**
   and handle `EAGAIN` with a ~5 ms sleep. ⚠️ **Both consumers already work around this**, so it is not
   an argument for the ALSA port — latency, mixing and frame arithmetic are.
2. **Speaker distortion at full scale.** Apply ~50 % software attenuation (`>>1` on int16) before
   writing. ScummVM does this post-mix.
3. **ioctls reset each other.** `SNDCTL_DSP_STEREO` is **silently ignored** (returns `rc=0,
   stereo=1` while the device stays mono — verified with `native_apps/tests/ch_test.c`);
   ⚠️ note the PCM underneath is **stereo-only** (measured above), so what stays mono is the *shim's*
   view of it, with `SND_PCM_OSS_PLUGINS` converting below — *inference from the two measurements, not
   itself measured.* It is also why a buffer sized as interleaved stereo has never sounded wrong:
   `native_apps` writes `frames * channels * 2B` — with the channel count read back, since F1 Phase 2 —
   and the shim consumes exactly that.
   `SNDCTL_DSP_SPEED` may reset format and/or channels; `SNDCTL_DSP_SETFMT` may reset speed; and
   set-ioctl output values may not reflect actual device state. **Workaround:** set SPEED → FMT →
   CHANNELS in that order, then read back the truth with `SOUND_PCM_READ_RATE`,
   `SOUND_PCM_READ_BITS`, `SOUND_PCM_READ_CHANNELS`, and use the read-back rate **and the read-back
   channel count**. *Evidence:* at
   22050 Hz music played at half speed; at 48000 Hz it got proportionally worse (~4×), consistent
   with `_outputRate` not matching the real device rate. Working implementations:
   `scummvm-roomwizard/backend-files/oss-mixer.cpp` and, since F1 Phase 2,
   `native_apps/common/audio.c`'s `configure_dsp()` — which reads the channel count back too, so no byte
   count in `native_apps` spells a channel count into a constant any more.
4. **32-bit `time_t` overflow.** `sizeof(long) == 4`. Never compute
   `(now.tv_sec - epoch_0) * 1000000L` — baseline timers to *current* time, not epoch zero.
5. ⚠️ **The shim only hands ALSA WHOLE PERIODS, and an underrun DISCARDS what it was staging.**
   Measured on `.188` 2026-08-15 with `native_apps/tests/audio_mix_test` holding `/dev/dsp` open, from
   `/proc/asound/card0/pcm0p/sub0/`: our `O_NONBLOCK` open negotiates **`period_size` 2048 frames
   (46 ms) / `buffer_size` 32768 (743 ms) / 16 fragments of 8192 B**, and `appl_ptr` steps
   2048 → 4096 → 6144 and **never lands between two periods** — while `SNDCTL_DSP_GETOSPACE` counts the
   partial period the shim is still accumulating. So **a write smaller than one period is invisible to
   ALSA until the period completes**, and a writer that keeps only 1.7 periods queued leaves ALSA one
   playable period: it drains that, `state` goes **`XRUN`**, and the recovery throws the staged bytes
   away. Polled 14 × 40 ms, the cycle is `RUNNING → XRUN → RUNNING` about every **120 ms**, which is
   also the crack rate an operator counted by ear in a 3 s tone.
   **Consequence for any incremental writer here: measure the period and queue a whole number of them,
   at least three.** `native_apps/common/audio_gen.c`'s `audio_pump_lead_frames()` is that rule;
   `IMPROVEMENT_PLAN.md` F1 Phase 3 has the derivation.
   ⚠️ **`GETOSPACE` over-reports what is really buffered by ~1.3 periods (~60 ms), so a nominal lead is
   worth that much less in real audio.** Measured `.188` 2026-08-18 by `native_apps/tests/oss_keepalive.c`,
   reading `GETOSPACE` and `/proc/asound/card0/pcm0p/sub0/status` at the same instant: `in_flight` stands
   **2650–2670 frames** above the kernel's own `buffer_size − avail` on every `RUNNING` row. A 3-period
   139 ms target therefore holds ~79 ms, falling to **44 ms** at its shallowest under a 33 ms service loop.
   ⚠️ **Hence a never-reset stream must be serviced every ~66 ms or better: 33 and 66 ms produce ZERO dry
   windows, 100 ms empties the ring on 15 of 59 services, and 150 and 200 ms on every one** (one run, one
   continuous stream, swept clean-to-dirty; 150/200 are the probe's own positive control, since both
   exceed the lead arithmetically). ⚠️ **An underrun here is otherwise INVISIBLE** — the shim swallows it,
   and `CONFIG_SND_PCM_XRUN_DEBUG` is unset (`usb_host/device_config:2730`) so neither `xrun_debug` nor a
   `dmesg` trace exists. The two `/proc` witnesses, and the way each of them lies, are in that probe's header.
6. ⚠️ **The minimum audible tone length is a property of RESTARTING the stream, not of
   `SNDCTL_DSP_RESET`** — removing the reset does not change it. Measured on `.188` 2026-08-15: with the
   ring allowed to empty between sounds, 5–40 ms is inaudible, 60 ms partial, 100 ms clean; with the
   stream **continuously fed**, **5 ms is audible and 20 ms recognisable** — first via
   `audio_pump_set_keepalive()`, then again 2026-08-18 on the shipped never-reset stream. Any claim about
   a minimum tone length must say which regime it was measured under; nothing in the tree clamps it.

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
call).

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
  which is why a cleaned unit keeps the name `commissioning/set-hostname.sh` gave it — and an uncleaned one does not.
- ⚠️ **The vendor's own validator rejects hyphens.** `net.hostname` is filtered by an awk regex that
  accepts `RW09`, `RW20`, `rwtest` and `null` but **rejects `RW-Test` and `rw-test`**; a rejected name
  logs `Invalid host name detected.` and the DHCP client then announces the hardcoded fallback
  `rwtwenty`. Prefer a hyphen-free name on any unit that still has the vendor stack.
- **There are two dhclient scripts, and only the vendor's rewrites `/etc/hosts`.** `/etc/dhclient-script`
  (vendor, 10,370 bytes) has a `# PV02 Addition` block that on every `BOUND` event writes
  `net.hostname` into `/etc/hostname`, truncates `/etc/hosts` to `127.0.0.1 localhost` and appends
  `<leased-ip> <name>`. `/sbin/dhclient-script` (the stock ifupdown one, 16,772 bytes) contains
  **zero** references to `/etc/hosts`. Which one runs depends on who starts `dhclient`: the vendor
  `networkmanager` passes `-sf /etc/dhclient-script`, while `S40networking` +
  `auto eth0 / iface eth0 inet dhcp` uses the default. So once the `networkmanager` boot link is gone
  **nothing regenerates either file**, which is what makes an offline-set name stick — measured on a
  unit in service, five months of daily `/var/lib/dhcp/dhclient.leases` updates with `/etc/hosts` and
  `/etc/hostname` untouched.
- ⚠️ **`/etc/dhclient.conf`'s `send host-name` is a third copy of the name**, and it is the copy a
  router's device list shows: the vendor image ships `send host-name "RW09";`, and a unit renamed
  months earlier still announced `RW09`. `commissioning/set-hostname.sh` writes this file too.

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
deploy scripts cannot resolve it until `libnss-mdns` is installed there.

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
| `power` patched — 500 mA, what a commissioned unit runs | `a1fd1af8da18c430a34b24762aa16dab` | 5,225,796 |
| `power` **and** `mode` patched (`RW_UIMAGE_BOTH_MD5`) | `9021923205825a2ec36edeaa1fe3ccc3` | 5,225,796 |

Nothing generates it per-unit, unlike the filesystem UUIDs
([§4.2](#42-partitions)). The power patch differs from the vendor image in **exactly 9 bytes**: the uImage
header CRC (offsets 4–7), the data CRC (24–27), and one value byte at `0x4FA2CF`; the both-patched image
differs in **10**. That makes an md5 gate a complete check, which is what `lib/rw-usbpower.sh`'s
three-state classifier (`vendor` / `power` / `both` / `unknown`) is built on.

⚠️ **The third row is a firmware state no delivery path produces.** The `mode` patch was refuted on
hardware and is out of every deploy path, so the md5 is recorded for the *classifier* — a unit that was
patched by hand classifies as `both` and can be re-derived back down to `power`, which is how `.188` was
reverted. It was measured by running `patch_dtb.py --mode` over `.188`'s own `uImage-system.vendor`
twice, byte-identical, with the power-only derivation from the same source reproducing `a1fd1af8…` as the
control — so the source was the pristine vendor image and the toolchain is reproducible. A *mode-only*
image is unreachable by construction (`--mode` patches both properties in one pass) and classifies as
`unknown`, which is correct.

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
is no radio on the board at all ([`HARDWARE.md` §4](HARDWARE.md#4-unpopulated-and-expansion)), so BT means a dongle in this
single connector. `# CONFIG_BT is not set` — exactly the situation `CONFIG_INPUT_JOYDEV` was in before
Hack 2 — and its dependencies are satisfiable: `CONFIG_NET`, `CONFIG_CRC16`, `CONFIG_HID` and
`CRYPTO_AES` are all `=y`, while `CRYPTO_SHA256`, `CRYPTO_BLKCIPHER`, `CRYPTO_ECB` and `CRYPTO_CMAC` are
`=m` and would have to be **built and shipped**, since `/lib/modules/4.14.52/` ships empty.
`CONFIG_CRYPTO_ECDH` is unset and is needed only for BT LE Secure Connections. ⚠️ **[inferred] the
controller is far more likely to work than the audio** — A2DP needs software SBC encoding on this single
core. Also unbuilt
and worth knowing: `CONFIG_SND=y` and `CONFIG_SND_USB=y` but `# CONFIG_SND_USB_AUDIO is not set`, so a
wired USB DAC is one module away too. Both are
[`IMPROVEMENT_PLAN.md` F17](IMPROVEMENT_PLAN.md#f17-bluetooth-peripherals-and-whether-usb-dma-is-reachable--open-measured-2026-08-08).

Hubs work, including combo devices with a built-in hub; multiple simultaneous devices are fine.

⚠️ **A babble error leaves a `printk` loop that survives unplugging and ends in a hardware reset ~46 min
later, and it invalidates any measurement taken during it** — mechanism, log evidence and the one-command
check in [`IMPROVEMENT_PLAN.md` B33](IMPROVEMENT_PLAN.md#b33-a-usb-babble-error-leaves-a-printk-loop-that-hard-resets-the-device--open-measured-2026-08-17).
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
boot."* ✅ **Treat this as a standing property of the hardware with a working one-tap remedy, not as an
open bug** (agreed 2026-08-14): three mechanisms inferred from the driver source have each been applied
and refuted on hardware, and Device Tools → USB → **RESCAN** revives a dead port in one tap, ~5 s,
verified on a panel. Once a port is live, replug works normally at any gap — so it is **one tap per
boot**, and none at all if the device was plugged in at boot.
Where `$MUSB` = `/sys/devices/platform/68000000.ocp/480ab000.usb_otg_hs/musb-hdrc.0.auto`:

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
- **The OTG ID pin is watched by the TWL4030 PMIC, not by MUSB** — its own interrupt line
  (`phy-twl4030-usb.c:747`) reading an always-powered `PM_MASTER` register, so it fires with the PHY
  asleep and VBUS off. ⚠️ But what an ID event produces is a **resume**, and a resume replays the
  *cached* DEVCTL (`musb_core.c:2609-2610`): on a cold port the cached `SESSION` bit is clear, so there
  is nothing to resume. **[inferred]** as the reason ID-ground alone cannot revive a dead port; the
  failure itself is measured.
- **Once a session exists it is never torn down.** `omap2430_ops` has no `.try_idle`, so
  `musb_platform_try_idle()` is a no-op and `SESSION` is never cleared — which is why a live port stays
  live indefinitely, including across an unplug. With nothing connected,
  `musb_pm_runtime_check_session()` matches `MUSB_QUIRK_A_DISCONNECT_19` and after 3×1000 ms polls drops
  its pm_runtime reference.

⚠️ **A hub or adapter left permanently attached does NOT fix this.** The driver's teardown path says it
should, and the claim was written here on that reading — but it assumes a session **already exists**. A
passive hub on a dead port reads `Vbus off` at 1, 2 and 3 min and for minutes after, and a device plugged
into that hub enumerates nothing. Re-seating an adapter on a port that *had* already had a session revives
it immediately, which is the distinction: it holds a port **open**, not a port **alive**.

⚠️ **Five readings that look diagnostic and are not** — three were believed and written down before being
refuted, one of them in this document.

| Reading | Actually |
|---|---|
| `echo host > $MUSB/mode` | **silent no-op** — `omap2430_ops` has no `.set_mode`, so the store reports success having done nothing |
| `$MUSB/vbus`'s `timeout 1100 msec` | **inert** — nothing on omap2430 reads `musb->a_wait_bcon`; writing it changes the printed number and nothing else |
| `power/control = on` (forbidding runtime PM) | **does not prevent the drop** — measured with `runtime_status` reading `active` throughout |
| `$MUSB/mode` as a state reading | **not diagnostic** — reads `a_idle` with a pad enumerated, `js0` present and the game responding |
| `twl4030-usb/vbus` | **not a port-state reading at all** — 0444, reports `vbus_supplied` (somebody feeding *us*), so it reads `off` in the working state **and** the dead one |
| `lsmod` → `xpad … 0` | a refcount of module *users*, not bound devices — reads `0` with a pad bound and `event1`/`js0` present |

**The one real userspace trigger besides a rebind is debugfs `softconnect`** (`musb_debugfs.c:301-343`) —
and it sets `SESSION` **only** in `OTG_STATE_A_WAIT_BCON`, so it cannot revive a port sitting in `a_idle`.

⚠️ **Three source-derived mechanisms have each been applied and refuted on hardware**, the last being the
DTB `mode` 3 → 1 patch: `.188` was patched to `mode = <1>`, the **booted kernel's own tree** read it back,
and with the socket empty at boot a pad plugged in afterwards still stayed dark — while
`/etc/init.d/usb-host recover` brought it up on attempt 1 with the same pad and cable as the negative
control. **The common thread is that none of the three explains how a port that probed with an empty
socket ever obtains a session** — VBUS and the ID pin are both inert at that point. Require an answer to
that question of any further candidate before spending a reboot on it. Candidates and what each
measurement closed:
[`IMPROVEMENT_PLAN.md` B32](IMPROVEMENT_PLAN.md#b32-usb-is-enumerated-only-at-driver-probe--cause-established-2026-08-13-no-automatic-fix).

**Reading the live device tree.** `/sys/firmware/devicetree/base/` is the unflattened tree as the running
kernel holds it, and `/sys/firmware/fdt` the raw blob, parseable by `usb_host/uimage.py`'s walk.
⚠️ **`find /proc/device-tree -name X` silently finds nothing**: `/proc/device-tree` is a *symlink* to the
sysfs path and `find` does not follow it. Use the `/sys/firmware/devicetree/base` path — that is how the
booted `mode` value was confirmed against the running kernel rather than against the decompiled
`usb_host/original.dts`.

**`/etc/init.d/usb-host recover`** does the rebind — unbind, settle `RECOVER_SETTLE` (2 s) so VBUS can
decay below VBusValid, bind — and retries up to `RECOVER_TRIES` (3), stopping the moment a **non-hub**
device appears and exiting non-zero on exhaustion. Plug the device in **first**. Reachable from the panel
as Device Tools → USB → **RESCAN**, which forks it when a scan finds nothing; measured on `.188`
2026-08-14 at ~5 s from one tap, leaving `Vbus on`, `1-1`, `event1` + `js0` and the pad playable. ⚠️ It is
deliberately not on a timer, and the reason is not merely wasted rebinds: **nothing in software can
distinguish "nothing is plugged in" from "a pad is plugged into an unpowered port"** — VBUS is off either
way and no connect interrupt can arrive in either — so an operator who has just plugged something in
holds the one bit no poll can obtain.
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
and a write above it does not raise the duty — `echo 150` reads back `100`. Measured on `rwtest`
2026-08-06 before `sshd` and long before `roomwizard-app`: a freshly booted **cleaned** unit reads
**100 of 100** with nothing in our stack having written it — `app_launcher` makes no `hw_set_backlight()`
call at all. The vendor's own mechanism (`adjustbklight.sh` → `setbacklight.sh` / `backlight.sh -1`)
writes this one node from `websign/brightness.conf` and **defaults to 100** when that file is missing,
which is the state our clean leaves behind. So a boot-time setter would write 100 over 100.

⚠️ **"The panel looks dim" is therefore not a software question — at a fixed duty cycle, perceived
brightness follows what is *drawn*.** The launcher grid measures **19.2 % mean luminance**, 87.5 % of
its pixels in the darkest quarter (32bpp capture, 2026-08-06); the vendor's browser filled the same
panel with a near-white page. Judge a brightness claim with **identical content on both panels**, or it
measures the UI's palette rather than the hardware.

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
[Unpopulated and expansion](HARDWARE.md#4-unpopulated-and-expansion). Proposal: `IMPROVEMENT_PLAN.md` F4.

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
[Unpopulated and expansion](HARDWARE.md#4-unpopulated-and-expansion).

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
3.3 V rail: [Unpopulated and expansion](HARDWARE.md#4-unpopulated-and-expansion).

**[inferred] expect the vendor to have assumed a Series 1 module** — from the command set the vendor's
own tooling uses, not from a module ever being read on a unit. A settable `ATMY` and `ATCH` are 802.15.4
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
original up; the factory crontab's content is recoverable from the partition images under `partitions/`
if it is ever wanted.

⚠️ **A device can be running a copy of that script older than the repo's.** `commissioning/provision.sh
<ip>` is what deploys it (to `/opt/roomwizard/`) and runs it once; `/etc/init.d/roomwizard-app` re-runs
the *deployed* copy on every boot. So check `--status` before drawing a conclusion about behaviour — the
bypass is now the script's first command and it reports the bypass state on its last line, but a device
that has not been re-provisioned is running whatever it was given.

### 3.14 What is not present

**Confirmed absent:**

- ❌ **WiFi / Bluetooth** — no radio of any kind fitted. A USB Bluetooth dongle is the only route, and
  `CONFIG_BT` is unset ([§3.6](#36-usb), [`IMPROVEMENT_PLAN.md` F17](IMPROVEMENT_PLAN.md#f17-bluetooth-peripherals-and-whether-usb-dma-is-reachable--open-measured-2026-08-08)).
- ❌ **Ambient light sensor** — and none is possible. The teardown found no sensor part **and no
  aperture, window or light pipe anywhere in the enclosure**: the case is light-tight. The vendor
  factory test does have a light-sensor step (`functionaltest.sh` → `pv02_app 5`, on `/dev/i2c-1`) —
  it is shared firmware across a product family. **Do not probe bus 1 looking for it.**
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
| p6 `/` | 379 MB | `/usr` 223 MB (`/usr/lib` 138, `/usr/share` 60) · `/opt` 142 MB (`openjre-8` 93, `jetty-9-4-11` 43). **Everything outside `/usr` and `/opt` totals 15 MB** |
| p2 `/home/root/data` | 144 MB | `cron/` **131 MB** — a *log*, not the spool (`commissioning/provision.sh` truncates it rather than deleting the directory) · `test.hex` 10 MB · `websign/` 220 KB, the network config of [§3.5](#35-network-and-power) |
| p3 `/home/root/log` | 31 MB | `jetty_logs` 18 MB · `browser.err` 8 MB · `messages` 4 MB |
| p5 `/home/root/backup` | 492 MB | `factory/` **472 MB** — vendor upgrade/restore images plus `.md5` files · `websigns/` 15 MB |

**`/usr/lib`'s 138 MB is a kiosk-browser stack that nothing in this project uses** — `libwebkit2gtk`,
`libicudata`, `libjavascriptcoregtk`, `libgtk-3`, the X11 and gstreamer trees, a spell checker — served by
`/etc/init.d/browser`, with `Xorg.0.log` and p3's `browser.err` as its output. Every component here draws
straight to `/dev/fb0`, so none of it is linked or loaded. `/usr/lib` also holds `libpython3.8`, `perl5/`
and `ts/` (tslib), which are worth keeping.

**The vendor's upgrade machinery is on disk and its payload is p5's `factory/`:**
`/etc/init.d/startautoupgrade`, `/opt/sbin/upgrade_logger.sh`, `IsUpgradeRunning` on p5, and the
litter of `upgradeProgressListener_upgradeStatus=*` files dropped in `/` show it has run.
**[unverified]** whether it can still fire unattended — nobody has established it either way — but
deleting `factory/` removes the payload it would need, which is why that deletion is a safety measure
and not a space measure.

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
  retries per partition and exit code 6 on final failure. Modify anything in that tree and every
  `.md5` beside it has to be regenerated.

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
[Serial ports](#312-serial-ports). The loop it enables, with a card backup taken first: stage the
experiment under a **new** filename on p1, interrupt into `rw20 #`, `fatload mmc 0 0x82000000 uImage-test;
run sysargs; bootm 0x82000000`. If it panics, power-cycle and do **not** interrupt — `bootcmd` loads the
untouched `uImage-system` and nothing has changed state. Promote only after several clean boots, keeping
the old image under another name.

> **JTAG is required only if you damage the 12 KB NAND redirector (`mtd0`) or write a bad
> `mlo`/`u-boot.bin` to p1.** Observe the rules in [§1](#the-rules-that-prevent-a-brick) and it
> never comes up. `P3` is a TI-14 JTAG header **[inferred]** if it ever does.

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

⚠️ **`rc0.d` and `rc6.d` are shutdown, not startup — never clean them.** Why they are unreachable by
construction rather than merely unvisited: `device-files/CLAUDE.md`.

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

**Seeing what is running: use `ps`, never `ps w`.** This busybox (v1.31.1) treats `ps w` as "processes
with a controlling TTY" — on RW09 **3 lines against plain `ps`'s 51**, the two gettys and the header. An
app started at boot or by the launcher has no TTY, so `ps w` shows nothing and the process looks absent.

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
[`native_apps/check-arm-safe.sh`](native_apps/check-arm-safe.sh), which all three component build scripts
run before every deploy and on build-only runs too. It reports zero across every ARM artifact, skips
anything whose `objdump -f` architecture is not ARM, and says how many it skipped.

Two ways to get a wrong answer out of it, both measured on this repo:

> ⚠️ **Match the tab-delimited mnemonic field, not the line.** A bare `grep 'sdiv\|udiv'` matches the
> *substring* `udiv` inside the **names** of the software-divide helpers — `__udivsi3`, `__udivmoddi4`
> and their call sites. Those are symbol names and branch targets, and their presence is positive
> evidence that division is being done in software. `libgcc.a` on this toolchain contains **zero**
> hardware `sdiv`/`udiv`, so there is nothing to allowlist and any correctly-matched hit is real.
>
> ⚠️ **Gate the *unstripped* artifact.** `objdump` needs the symbol table to tell **Thumb-2 from ARM**,
> and these binaries are Thumb-2; stripped, it re-reads the same `.text` bytes as 32-bit ARM words and
> manufactures divides that are not in the file — 9 phantom hits on ScummVM, 1 on `vnc_client`. The
> phantom operands are not reliably invalid, so eyeballing them is not triage. The gate therefore
> **refuses to judge** a stripped target: **0** clean, **1** a real hit, **2** could not be judged.
> ScummVM's gate sits inside `strip_binary` *before* the `strip` call for this reason — the strip is
> in-place, so that is the only moment an unstripped artifact exists. Harness rules, including why the
> status must not be read through `xargs`: `tests/CLAUDE.md`.

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

**Upgrading would be a net loss anyway.** Every hoped-for benefit is either already available or not a
version problem: ALSA works today and the bug is in the `snd-pcm-oss` emulation layer, so that fix is
pure userspace at zero risk; USB host/DMA and `PREEMPT_NONE`/`HZ=100` are kernel *config* defects,
unfixable without source whatever the version; there is no WiFi hardware to gain a driver for; and this
is a LAN-only device with no browser and no untrusted input.

**The DRM/KMS trap is the decisive argument.** `omapfb` and `omapdss` were deprecated across 4.x and
**removed from mainline during 5.x**; the OMAP3 replacement is `omapdrm`, a DRM/KMS driver. Under it
`/dev/fb0` exists only via `CONFIG_DRM_FBDEV_EMULATION`, whose fbdev emulation exposes a **fixed** pixel
format — while this project switches bpp at runtime in three components ([Display](#32-display)). The DSS
overlay sysfs interface, the best free performance win available, disappears outright, and a 6.x kernel
has a materially larger footprint on a 234 MB box. **[inferred]** that the emulation would *reject* the
switch — it follows from how that emulation works but could not be tested, because this device has no DRM
at all. What is measured is only that the *current* stack supports the switch
(`/sys/class/graphics/fb0/bits_per_pixel` tracks whichever app is running).

**Brick risk for kernel work: LOW** (removable SD plus the untouched-`uImage-system` discipline).
**Value: LOW.** The ratio does not justify it. Treat this as a userspace problem with a
kernel-config footnote: the two highest-value improvements available — ALSA audio and DSS
overlays — need no kernel work at all.
