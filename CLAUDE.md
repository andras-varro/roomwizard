# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Development projects that repurpose the **Steelcase RoomWizard** — a wall-mounted
meeting-room display — into a games/apps platform. The device is an embedded Linux
box (**TI OMAP3503** ARM Cortex-A8 @ 600MHz, 234MB RAM, 800×480 framebuffer,
**projected-capacitive** touch, kernel 4.14.52, SysVinit). No GPU, no DSP — all
rendering is software. Display is legacy **omapfb/omapdss**; there is no DRM/KMS. There is **no local app to run**: all code is
cross-compiled on the dev host and deployed over SSH to a physical device (the
reference unit is `192.168.50.73`, aka RW09). Verifying a change means deploying it
and looking at the device screen (framebuffer screenshots via `fb565_to_png.py`).

**One fact, one home.** Three top-level docs with disjoint jobs — if something appears in two of
them, the other copy is stale:

| Doc | Answers | Read it |
|-----|---------|---------|
| `SYSTEM_ANALYSIS.md` | *What is true about this device?* — silicon, board, boot chain, OS, traps | Before touching anything hardware-related (audio, USB, watchdog, boot, GPIO, cross-compile) |
| `IMPROVEMENT_PLAN.md` | *What should we do about it?* — prioritised bug + feature backlog with `file:line` | **Before starting work**, so you don't rediscover a known bug |
| `CLAUDE.md` (this file) | *What must I know before my first edit?* | Loaded every session |

`SYSTEM_ANALYSIS.md` is organised by subsystem; each one states what's there, how to drive it, the
gotchas, and (only where it explains the present) how Steelcase shipped it. It also carries the
full board teardown — parts inventory, connectors, unpopulated headers — and a photo index for
[`HardwarePhotos/`](HardwarePhotos/).

Each component directory has its own `CLAUDE.md` with authoring guidance for that component
(`native_apps/`, `scummvm-roomwizard/`, `vnc_client/`); those load automatically when you work
on files there. Component docs describe **their own code only** — device facts belong in
`SYSTEM_ANALYSIS.md`, open work belongs in `IMPROVEMENT_PLAN.md`. Link, don't copy.

**Kernel policy: do not attempt a kernel rebuild or upgrade.** The vendor's kernel source is
not available (the repo's `usb_host/linux-4.14.52/` is vanilla upstream and is missing both
`panjit_ts` and the panel driver), and requesting it from Steelcase has been explicitly ruled
out. Anything gated on a kernel config change is out of scope — see
`SYSTEM_ANALYSIS.md#7-kernel-policy`.

## Build & deploy

Everything builds with the ARM cross-compiler and deploys over SSH. There is **no
CI, no test runner, no lint** — "tests" are interactive on-device diagnostic tools.

```bash
# One-time device bring-up (see COMMISSIONING.md / README.md)
./commission-roomwizard.sh          # Phase 1: SD-card prep (offline)
./setup-device.sh <ip>              # Phase 2: SSH bloatware cleanup + init service

# Deploy everything (native_apps first — it provides the launcher, then the rest)
./deploy-all.sh <ip>
./deploy-all.sh <ip> <component>    # deploy one component (e.g. vnc_client)
./deploy-all.sh --list             # list discovered components

# Per-component (each dir has its own build-and-deploy.sh with the same arg shape):
cd native_apps && ./build-and-deploy.sh              # build only
                  ./build-and-deploy.sh <ip>         # build + deploy
                  ./build-and-deploy.sh <ip> set-default   # + make it the boot app

# NOTE: `set-default` is the ONLY mode native_apps/build-and-deploy.sh accepts.
# Cleanup / bloatware removal / boot-service install live in setup-device.sh:
./setup-device.sh <ip>              # system setup + init service (also disables the SW watchdog)
./setup-device.sh <ip> --remove     # + delete bloatware (see --help for scope)
./setup-device.sh <ip> --deep-clean # + extended cleanup (~560MB more; see IMPROVEMENT_PLAN.md)
./setup-device.sh <ip> --status     # report what is installed/removed

ssh root@<ip> reboot
```

Components (each a subdir with a `build-and-deploy.sh`): `native_apps` (C games +
launcher + tools), `scummvm-roomwizard` (ScummVM backend port), `vnc_client`,
`usb_host` (USB host-mode enablement + Xbox controller modules).

**Toolchain:** `arm-linux-gnueabihf-gcc` (`sudo apt install gcc-arm-linux-gnueabihf`).
ScummVM additionally needs **WSL Ubuntu 20.04+** and `g++-arm-linux-gnueabihf`; it
cross-compiles its own zlib/libpng. `usb_host` needs kernel-module build deps
(`bc libssl-dev bison flex`) + `python3`.

**Note:** `native_apps/Makefile` uses native `gcc` and is *not* the deployment path —
`native_apps/build-and-deploy.sh` (cross-compiler, `-static`) is the source of truth
for how binaries are actually built and shipped.

## Non-obvious constraints (things that will silently break)

- **Cortex-A8 has no hardware integer divide.** A binary containing an `sdiv`/`udiv`
  *instruction* crashes instantly with SIGILL (exit 132) — blank screen, no output, no log.
  **Verify with `native_apps/check-arm-safe.sh`** (runs automatically from
  `build-and-deploy.sh` before every deploy, and on build-only runs too).
  The expected count is a **hard zero**, and it currently is zero across all 30 build
  artifacts. With the toolchain default `-march=armv7-a+fp`, app-level 32-bit `int`
  division compiles to a *call* to the software helper `__aeabi_uidiv`/`__udivsi3`, so the
  deploy path's bare `$CC -O2 -static` is already safe; `-mcpu=cortex-a8 -mfpu=neon`
  (present only in the dead `native_apps/Makefile`) makes no difference to the emitted code.
  What *would* break it is an explicit `-march` that implies the idiv extension —
  `-march=armv7ve`, `-mcpu=cortex-a7/a15/a17`, or anything ARMv8. Dynamic linking is
  unaffected. libpng needs `-DPNG_ARM_NEON_OPT=0`.

  ⚠️ **Do not use the old `grep 'sdiv\|udiv'` check** — corrected 2026-07-30. It matches the
  *substring* "udiv" inside the **names** of the software divide helpers (`__udivsi3` ×20,
  `__udivmoddi4` ×6, plus their call sites), which is why it reported "~45 unreachable libc
  hits that must be allowlisted". Those 45 are not instructions and not a hazard — they are
  positive evidence that division is being done in software. The claim that
  `libgcc.a` carries `sdiv`/`udiv` is also false for this toolchain (measured: zero).
  Match the tab-delimited mnemonic field, as `check-arm-safe.sh` does; then no allowlist is
  needed and any hit is real.
- **Framebuffer bpp is per-app — always confirm before decoding a screenshot.** `/dev/fb0`
  format is set at runtime by whatever app is running: **native menu + games force 32bpp
  XRGB8888** via `fb_set_bpp(fb_dev,32)` at startup (`app_launcher` also re-asserts it after
  every child exits; `game_selector` only does it after a child exits); **ScummVM and the VNC
  remote session run 16bpp RGB565** (they call `fb_set_bpp(...,16)` to halve memory
  bandwidth). `framebuffer.c` (common lib) draws `uint32_t` per pixel, so its primitives are
  32bpp-only — but `fb_swap()` and `fb_clear(…,0)` are byte-sized and correct at any bpp, which
  is what lets ScummVM drive its own 16bpp pixels through the same `Framebuffer`.
  Screenshot: `ssh root@<ip> cat /dev/fb0 > fb.raw` (one 32bpp frame = 800×480×4 =
  1,536,000 bytes — coincidentally the same size as two 16bpp pages, which is why the old
  16bpp decoder looked size-correct while decoding garbage), then `python3 fb565_to_png.py
  fb.raw fb.png` (defaults to 32bpp; pass `--bpp 16` for ScummVM/VNC-session screens).
- **Steelcase software watchdog reboots the device ~every 70 min** in game mode (when
  Jetty/HSQLDB/browser are absent). It's a cron job (`/opt/sbin/watchdog/watchdog.sh`).
  `setup-device.sh <ip>` disables it (`touch /var/watchdog_test` + comment out cron).
  The *hardware* watchdog (`/usr/sbin/watchdog` daemon) is fine — keep it, but note any app
  that takes over the screen for long periods must keep feeding `/dev/watchdog` (60 s) or the
  device hard-resets.
- **Audio via OSS `/dev/dsp` is buggy** (ALSA OSS shim on TWL4030): open with `O_NONBLOCK`
  and handle `EAGAIN` or you get a "bru-bru-KLICK" stall (~506ms ALSA HW period); apply
  ~50% software attenuation (small speaker distorts at full scale); `SNDCTL_DSP_STEREO`
  is silently ignored, and SPEED/FMT/CHANNELS ioctls reset each other — set SPEED→FMT→CHANNELS
  and read back actual rate with `SOUND_PCM_READ_*`. See `scummvm-roomwizard/backend-files/oss-mixer.cpp`.
- **Touch coordinates must be captured before the `BTN_TOUCH` press event** (event order:
  ABS_X, ABS_Y, BTN_TOUCH, SYN_REPORT). The panel is projected-capacitive (Panjit on i2c-2 @ 0x03)
  and the controller is 2-point multi-touch capable, but the `panjit_ts` driver exposes only
  ABS_X/ABS_Y/BTN_TOUCH. `common/touch_input.c` maps a raw reading in **two stages**:
  raw 12-bit (0–4095) → **panel** pixel by a per-axis LINEAR map (no affine, no keystone — a
  `touch_trace` capture confirms the panel is linear), then **panel → logical** by subtracting the
  bezel viewport origin. Stage 1 is calibration; stage 2 is the bezel; they are separate and the
  fit for stage 1 must be done in panel coordinates or the bezel gets subtracted twice.
  `/etc/touch_calibration.conf`: line 1 = `raw min_x max_x min_y max_y` (calibration),
  line 2 = `bezel top bottom left right`. Both are set from **Device Tools → Set Screen**, on
  separate buttons: `TOUCH CALIBRATION` (tap 9 crosshairs, per-axis least-squares fit
  extrapolated to the true panel edges, summary before save) and `SCREEN EDGES`. ScummVM links
  its own copy of `touch_input.o`, so rebuild it after changing this file or its touch goes stale.
- **Screen edges are the library's problem, not the app's.** The bezel covers ~15px top and bottom.
  `fb_init()` shrinks the drawing surface to the visible rectangle and `fb_swap()` places it on the
  panel at the viewport origin, leaving the hidden bands black — so `fb.width`/`fb.height` and
  `SCREEN_SAFE_*` are the **logical** screen (800×450 at the shipped margins) and every pixel in it
  is visible. Never add your own bezel arithmetic. Margins default to `FB_BEZEL_*_DEFAULT`
  (15/15/0/0) when the config has no line 2.
- **Touchable is smaller than visible — on Y only** (measured 2026-07-31 with `touch_raw`). The
  digitizer reaches *past* both the left and right panel edges, but stops **~30 panel px short at
  the top and bottom**. In logical coordinates that means the top ~15 and bottom ~15 rows of the
  drawing surface **cannot be touched**, while every column can. Keep *interactive* targets out of
  those two bands; decoration can go to the edge. It is a sensor property — the electrode array is
  ~11 mm shorter than the LCD — so no calibration recovers it.
  The old "~10px left/right, ~25 top, ~30 bottom" figures were **wrong on X**: an artifact of the
  9-tap calibration fitting crosshairs inset only 40px, i.e. inside the compressed band. Numbers,
  method and the raw capture: `SYSTEM_ANALYSIS.md#33-touch`.
- **32-bit ARM:** `sizeof(long)==4`. Never do `(now.tv_sec - 0) * 1000000L` (overflows) —
  baseline timers to current time, not epoch 0.
- **Firmware / boot edits.** There is **no boot-time MD5 check** of the kernel and no
  signing — the only gate on `uImage-system` is its uImage header + data CRC (which
  `usb_host/patch_dtb.py` recomputes correctly). `boot_tracker` is written only by userspace
  after an upgrade, so a kernel that fails to boot changes no state and triggers no recovery.
  U-Boot has no `saveenv`, so the environment cannot be persisted or corrupted.
  **Rules:** never write `/dev/mtd*`; never overwrite `mlo`, `u-boot.bin` or `ctrlblock.bin`
  on p1; stage experimental kernels under a *new* filename and leave `uImage-system` alone —
  `bootcmd` is hardcoded to it, so a power cycle is a free undo. Observe that and JTAG never
  comes up. Full detail + recovery procedure: `SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery`.

## Cross-component engineering rules

Learned the hard way in one component, applicable to all of them.

- **Never use `--whole-archive` with `-lpthread` for static ARM builds.** It pulls in all of
  glibc 2.31's pthread init, which calls `clock_gettime64` (ARM syscall 403, added in kernel
  5.1). This device runs 4.14.52, so it gets `-ENOSYS`, then dereferences a NULL VDSO pointer:
  **SIGSEGV before `main()`**, no output, no log. Diagnostic signature in `dmesg` is
  `PC is at 0x40` with `r0 : ffffffda`. Plain `-lpthread` is fine. The native C apps escape
  this only because they never link pthread at all.
- **Verify build artifacts on disk, not config flags.** Generated `config.mk`/`config.h` files
  go stale — a leftover `USE_PNG = 1` once made the build compile `image/png.cpp` with no
  `libpng.a` present. Test for the `.a`, not the flag.
- **No `SCHED_RR` audio thread.** On this single 600 MHz core an RT audio thread starves the
  main thread and you get a black screen. `SCHED_OTHER` plus the ~500 ms OSS ring is enough.
- **Software rendering techniques that paid off** (ScummVM went 80 % → 32 % CPU; the VNC client
  independently reused the same set): precomputed palette LUTs; a precomputed source-column
  table plus row-pointer lifting to remove per-pixel division; border-only clearing; skipping
  `fb_swap` entirely on unchanged frames; 16bpp RGB565 to halve write bandwidth; NEON
  `vst1q_u16` 8-pixel blits; and row deduplication via an L1-resident temp row (~57 % of scaled
  rows are duplicates).
- **The DSS *can* scale in hardware.** An older note claimed otherwise. The OMAP3 DSS exposes
  three overlay planes with independent input/output sizes at `/sys/devices/platform/omapdss/`,
  driven from sysfs with no kernel work — the only graphics acceleration on this GPU-less part.
  See `IMPROVEMENT_PLAN.md` F2.
- **Cross-compiled dependencies must be built from source.** Ubuntu Focal under WSL cannot do
  armhf multiarch (`dpkg --add-architecture armhf` fails; the standard mirrors carry no armhf),
  which is why ScummVM and the VNC client each build their own zlib/libpng/libjpeg into a local
  prefix. When building libpng with `-mfpu=neon`, add `-DPNG_ARM_NEON_OPT=0`.

## Architecture

**Deployment model — strict separation of concerns:**

| Layer | Script | When |
|-------|--------|------|
| SD-card commissioning | `commission-roomwizard.sh` | once, offline |
| System setup (cleanup, init, audio, time-sync) | `setup-device.sh` | once, over SSH |
| Build + deploy all components | `deploy-all.sh` | per deploy |
| Per-component build/deploy/manifest | `*/build-and-deploy.sh` | per component |
| App respawn loop at boot | `roomwizard-app-init.sh` (`/etc/init.d/roomwizard-app`) | every boot |

System setup is done **once** by `setup-device.sh` — component deploy scripts must not
duplicate it. `deploy-all.sh` auto-discovers components (any subdir with a
`build-and-deploy.sh`), always runs `native_apps` first, then sets `app_launcher` as
the default boot app.

**App launcher + manifests:** The boot init script reads `/opt/roomwizard/default-app`
and respawns that binary whenever it exits (so exiting a game returns to the launcher).
`app_launcher` scans `/opt/roomwizard/apps/*.app` manifests (INI: `name=`, `exec=`,
`icon=` PPM P6, `args=` one of `fb,touch`/`fb`/`touch`/`none`) and renders them as a
touch grid. Each component's deploy script writes its own `.app` manifests + PPM icons —
this is how projects plug into the launcher without a central registry.

**native_apps shared C library (`native_apps/common/`)** — every game/tool links these:
- `framebuffer.c` — double-buffered 800×480 32-bit rendering, sprite blit
- `gamepad.c` — **unified input abstraction** across touch + USB keyboard + USB mouse +
  Xbox gamepad, mapped to abstract buttons (`BTN_ID_UP`…`BTN_ID_BACK`). New apps should
  use this rather than reading evdev directly. Configurable via `/etc/input_config.conf`.
- `touch_input.c`, `hardware.c` (LED/backlight sysfs), `common.c` (buttons, ModalDialog,
  safe-area screens), `ui_layout.c` (layouts + ScrollableList), `audio.c` (OSS beeps/tones),
  `config.c` (`/opt/games/rw_config.conf`), `keyboard.c` (on-screen touch keyboard),
  `highscore.c`, `ppm.c`, `logger.c`.

ScummVM has its **own independent** evdev input + OSS audio implementation (not the
native_apps common lib) but applies the same Cortex-A8 / OSS fixes.

## Device paths worth knowing

- `/opt/games/` — native binaries · `/opt/roomwizard/apps/` — `.app` manifests ·
  `/opt/roomwizard/icons/` — PPM icons · `/opt/roomwizard/default-app` — boot target
- `/dev/fb0` (framebuffer, 32-bit RGBA) · `/dev/input/event*` (evdev) ·
  `/sys/class/leds/{red_led,green_led,backlight}/brightness` (0–100)
- `/dev/dsp` (OSS audio, GPIO12 must be HIGH to unmute speaker) · `/dev/watchdog` (60s HW)
