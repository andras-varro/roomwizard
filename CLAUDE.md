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

## Working style

**Delegate the reading.** This repo's docs are long by design and its sources are large
(`device_tools.c` is 2651 lines), so answering "where is X / which call sites do Y / does this
pattern hold across all seven games" by reading files into the main context burns the budget that
the actual edit and its verification need. Spin up subagents for that — one per independent
question, in parallel — and keep only their conclusions. Do the edits, the build and the on-device
verification in the main thread, where the context is worth spending.

**A plan entry's recorded cause *and* its prescribed fix are both hypotheses.** `open, confirmed` in
`IMPROVEMENT_PLAN.md` means the **symptom** reproduced — nothing more. The cause written beside it and
the fix suggested under it were both written by someone who had not yet done the work. That has now
been wrong three times, and each time one `grep` would have settled it before any code was touched:
B12's silence timeout (which would have killed healthy idle VNC sessions), B13i's `break`, and B12b —
where the named cause (`quit()`), the prescribed fix (a `_quit` flag) and even its "compare with the
SDL backend" pointer were **all** false, because `OSystem_SDL::quit()` does the same `exit(0)`. So
reproduce the symptom, then find the cause yourself; and **when an entry says "compare with X", read
X**. Record what the cause turned out to be when you close the row — several Closed entries exist
mainly to retract their own diagnosis.

**Give every new check a negative control, and ask which part of the count is the harness.** A gate
that reports a number can be wrong in *both* directions, and this repo has hit both: the `sdiv`/`udiv`
gate counted five x86 binaries and three PNGs it could not actually read (false negative,
`IMPROVEMENT_PLAN.md` D2b), and B17's `/proc` scanner counted its own `grep` argv. The same gate
false-**positives** ~9 times on a *stripped* ScummVM binary and zero on the identical file unstripped —
literal pools decoded as instructions — and those phantom operands are **not** reliably invalid
(`udiv pc, fp, sl` is dismissible, `udiv r7, r1, lr` is a legal encoding), so eyeballing them is not
triage; the symbol table is the only thing that answers it. If a fix is supposed to drive a number to
zero, check that it reaches zero.

**Editing the two long docs.** `IMPROVEMENT_PLAN.md` is ~1600 lines and its Closed section is mostly
headings that other entries deep-link to: anchor an `Edit` on text you intend to **keep**, and expect to
fix inbound links whenever you retitle a heading — closing a row usually means retitling one. The IDE's
markdownlint does this for you: **`MD051/link-fragments` fires on exactly the anchors you just broke**,
so it is a free dangling-link checker, and `comm -23` of the `(#fragment)`s against the slugified
headings confirms the whole file. (`MD060` table-style warnings, by contrast, fire on every table in the
repo — that is the established style, not something you introduced.) A long multi-line `old_string` can
fail to match for no visible reason; the reliable fallback is `head -N` / `cat <<'EOF'` / `tail -n +M`,
which also preserves LF. **Never bulk-edit a source file with a python script**: it rewrites the whole
file's line endings. Use `Edit`, or `sed -i`.

## Build & deploy

Everything builds with the ARM cross-compiler and deploys over SSH. There is **no
CI, no test runner, no lint** — "tests" are interactive on-device diagnostic tools.

```bash
# Front door: a menu over all three phases below. Implements nothing itself —
# every item shells out to these same scripts, so they stay directly callable.
./roomwizard.sh                     # item 5 = full bring-up, 1 -> 2 -> 3

# One-time device bring-up (see COMMISSIONING.md / README.md)
./commission-roomwizard.sh          # Phase 1: SD-card prep (offline; sets host name)
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

**Note:** `native_apps/` has no `Makefile` — there was one, it targeted host `gcc` with ARM flags
and could not compile anything, and it was deleted. `native_apps/build-and-deploy.sh`
(cross-compiler, `-static`) is the only build path and the source of truth for how binaries are
actually built and shipped.

## Working from this host — Windows, WSL and the tools

The repo lives on `c:\work\roomwizard`; **everything that compiles or decodes lives in WSL.** These are
tool-level traps rather than device facts, and each one has cost real time.

- **WSL is not just for the cross-compiler.** Git Bash has no host `gcc` and no usable `python3`, so
  the host-side regressions (`tests/*_test.c`, built with host gcc) and every `fb565_to_png.py` decode
  need WSL as much as the cross-builds do. Invoke as
  `wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard/<component> && ./build-and-deploy.sh <ip>"`.
- ⚠️ **`command -v python3` returns success in Git Bash and the interpreter does not exist.** It
  resolves to the Windows App Execution Alias, which is a real file that prints *"Python was not found;
  run without arguments to install from the Microsoft Store"* and fails. So a `command -v` probe is a
  **false positive** here — test by running `python3 --version`, not by looking it up. WSL's python3 has
  `PIL` (10.4.0), which is what the framebuffer decoder needs.
- **Git Bash `/tmp` and WSL `/tmp` are different filesystems.** Capture a framebuffer with the Bash
  tool into Git Bash's `/tmp` and the WSL decoder cannot see it. Write captures somewhere under
  `c:\work\roomwizard` and both sides reach them.
- **The Bash tool's working directory does not reliably persist between calls.** Use an absolute path
  in every command. A `cd` into `scummvm/` silently applied to one later call and not the next,
  producing a bogus "No such file or directory".
- **No foreground `sleep`** — it is blocked. Use `run_in_background`, or put the sleep inside the remote
  command: `ssh root@<ip> 'sleep 3; …'` works, because the local command is `ssh`.
- **A compound `ssh` command can be refused by the permission classifier** — a `cp` chained with
  `echo`/`cat` was. Re-issue it as one plain single-purpose command.
- **File modes are unobservable on this host.** `/mnt/c` is DrvFs 9p: it reports every file
  `-rwxrwxrwx` and silently discards `chmod`, so a missing-`+x` bug can neither fire nor be
  demonstrated here (`IMPROVEMENT_PLAN.md` B19) — and you cannot build a negative control for one by
  `chmod`ing under `/mnt/c`.
- **`shellcheck` is not installed in this WSL** (C7 wants it). `bash -n` is what you have, plus
  `dash -n` on anything carrying a `/bin/sh` shebang.
- **Never run a ScummVM build concurrently with a `native_apps` build.**
  `scummvm-roomwizard/build-and-deploy.sh` does `rm -f native_apps/common/*.o` at **two** points
  (lines 296 and 466) — deliberately, because a stale x86 `.o` there fails the cross-build with
  "file format not recognized". It takes ~1m35s–2m20s; wait it out.

**Redeploy scope by changed file.** All three components link objects from `native_apps/common/`, so
what you touched decides how much has to go out. A *deployed* binary keeps whatever it was built with,
and a stale one does not error — it misparses (see the touch-calibration bullet below).

| Changed | Redeploy |
|---------|----------|
| an app's own source, `common/common.c`, `common/gamepad.c` | `native_apps` |
| `common/hardware.c`, `common/config.c`, `common/logger.c` | `native_apps` + `vnc_client` |
| `common/framebuffer.c`, `common/touch_input.c` | **all three** — `./deploy-all.sh <ip>`; ScummVM is the slow one |
| `roomwizard-app-init.sh`, `disable-steelcase.sh` | neither — **only** `./setup-device.sh <ip>`, which ends in a reboot |

A **new** native binary goes in `GAMES_BINARIES` in `native_apps/build-and-deploy.sh` and nowhere else;
that one array drives the upload, the remote `chmod +x` and the md5 verification. Details and the
reason there is no second list: `native_apps/CLAUDE.md`.

## Non-obvious constraints (things that will silently break)

- **You cannot script a touch interaction on this device, and the tool that claims to will lie to
  you.** There is no `/dev/uinput` (`CONFIG_INPUT_UINPUT` unset, no module), and evdev's `write()`
  path is for *output* events (force feedback, LEDs) — so `tests/touch_inject.c` writes to
  `/dev/input/event0`, **prints "injected successfully", exits 0, and delivers nothing to any
  reader.** Measured on RW09 2026-08-02. Automated verification therefore stops at the **first**
  screen: SSH-launch the binary, `cat /dev/fb0`, decode, assert. Everything past it needs a human at
  the panel — write the tap-by-tap checklist instead of a test. Detail:
  `native_apps/CLAUDE.md` → *Input*, disposition in `IMPROVEMENT_PLAN.md` C6.
- **Don't probe I2C bus 1.** `pv02_app 5` (the vendor light-sensor factory test) can hang the bus,
  and **bus 1 carries the PMIC** — see `SYSTEM_ANALYSIS.md#39-i2c`. There is no light sensor to find:
  the enclosure has no aperture at all.
- **Cortex-A8 has no hardware integer divide.** A binary containing an `sdiv`/`udiv`
  *instruction* crashes instantly with SIGILL (exit 132) — blank screen, no output, no log.
  **Verify with `native_apps/check-arm-safe.sh`** (runs automatically from
  `build-and-deploy.sh` before every deploy, and on build-only runs too).
  The expected count is a **hard zero**, and it currently is zero across all **31** ARM build
  artifacts. (The gate skips non-ARM files in `build/` — the `tests/` regressions are host-compiled
  into the same directory, and under WSL every file on `/mnt/c` looks executable, so it used to
  "check" five x86 binaries and three PNGs and report 38. See `IMPROVEMENT_PLAN.md` D2b.)
  With the toolchain default `-march=armv7-a+fp`, app-level 32-bit `int`
  division compiles to a *call* to the software helper `__aeabi_uidiv`/`__udivsi3`, so the
  deploy path's bare `$CC -O2 -static` is already safe; `-mcpu=cortex-a8 -mfpu=neon`
  makes no difference to the emitted code.
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
  format is set at runtime by whatever app is running: **every native menu, tool and game now pins
  32bpp XRGB8888** via `fb_set_bpp(dev, 32)` before `fb_init()` (`app_launcher` also re-asserts it
  after every child exits); **ScummVM and the VNC remote session run 16bpp RGB565** (they call
  `fb_set_bpp(...,16)` to halve memory bandwidth). ⚠️ **Until 2026-08-03 no *game* pinned it** — they
  inherited the previous app's depth, which was safe under the launcher and **not safe from a bare SSH
  launch**: after a 16bpp app a game came up at 16bpp and every `uint32_t` primitive overran the back
  buffer 2× (`IMPROVEMENT_PLAN.md` B24, the reachable case of B1). Both halves are fixed:
  the 11 remaining `fb_init()` call sites pin 32bpp, **and** `framebuffer.c`'s primitives now dispatch
  on `bytes_per_pixel` — they take RGB888 and pack to RGB565 at 16bpp, so no depth corrupts the heap
  any more. **Deployed to all three components and verified on RW09 2026-08-03**, including the original
  failure: a `vnc_client` killed mid-session leaves the panel at 16bpp, and `brick_breaker` launched over
  SSH into that state now flips it to 32bpp and renders correctly.
  `fb_swap()` and `fb_clear(…,0)` were already byte-sized; what was 32bpp-only was everything else,
  including `fb_clear` with a *non-zero* colour and the portrait rotation.
  Screenshot: `ssh root@<ip> cat /dev/fb0 > fb.raw` (one 32bpp frame = 800×480×4 =
  1,536,000 bytes — coincidentally the same size as two 16bpp pages, which is why the old
  16bpp decoder looked size-correct while decoding garbage), then `python3 fb565_to_png.py
  fb.raw fb.png` (defaults to 32bpp; pass `--bpp 16` for ScummVM/VNC-session screens).
  **Check `fbset | grep geometry` on the device before trusting a decode** — a stale 16bpp app can
  leave the panel in a mode no running app asked for.
- **Steelcase software watchdog reboots the device ~every 70 min** in game mode (when
  Jetty/HSQLDB/browser are absent). It's a cron job (`/opt/sbin/watchdog/watchdog.sh`).
  **`disable-steelcase.sh` is what disables it** — `touch /var/watchdog_test` (its *first* command,
  deliberately) plus a freshly written crontab, not a commented-out line. `setup-device.sh <ip>`
  deploys and runs that script; `/etc/init.d/roomwizard-app` re-runs it **on every boot**, which is
  why a failure in it used to be invisible and left the watchdog armed (`IMPROVEMENT_PLAN.md` B18).
  It now prints whether the bypass is in place, so `setup-device.sh` output answers the question.
  **A device can be running an older copy than the repo's until `setup-device.sh` is re-run** —
  **`./setup-device.sh <ip> --status` now md5s both deployed scripts against the repo's and says
  `matches repo` or `DRIFTED` per file** (read-only, no reboot; it also runs after a normal setup).
  Check that before reproducing anything against a device. Measured 2026-08-03: RW09 matches on both,
  `.53` is drifted on both.
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
  raw 12-bit (0–4095) → **panel** pixel by a per-axis **piecewise-linear** curve (three segments,
  knots fixed at panel ¼ and ¾), then **panel → logical** by subtracting the bezel viewport origin.
  Stage 1 is calibration; stage 2 is the bezel; they are separate and the fit for stage 1 must be
  done in panel coordinates or the bezel gets subtracted twice.
  **Why three segments:** the format keeps them, but with the endpoints unclamped (see below) the
  stored curve *is* a straight line — the host regression asserts **zero** deviation from the
  interior fit. The model consistent with every measurement is **linear across the panel, hard-clipped
  at raw 0 and 4095**. (An earlier revision claimed Y runs ~9.9 raw counts/px in the middle and ~8.8
  in the outer bands; that came from a single target and is **not supported** — residuals against the
  interior line are ±80 raw, ≈±8 px of tap placement, with no consistent sign.) The three-segment
  format is retained anyway because changing line 1's field count is the one edit that silently breaks
  a component linking a stale `touch_input.o`.
  `/etc/touch_calibration.conf`: line 1 = eight numbers, per axis the raw readings at panel
  `0 ¼ ¾ dim-1` (`x0 xk_lo xk_hi x1  y0 yk_lo yk_hi y1`); line 2 = `bezel top bottom left right`;
  line 3 = optional, keyword-tagged `reach x_lo x_hi y_lo y_hi` — what the *physical* edges actually
  emit, from the wizard's sweep. Absent means "assume the hardware limit". A legacy 4-number line 1 is
  still read and **migrated on load** — knots onto the line it described, endpoints left where the
  line puts them.
  ⚠️ **Never clamp a fitted endpoint into `0..4095`.** A correct fit on this panel legitimately
  extrapolates outside it (the reference Y fit is `-279..4382`), because the interior line reaches raw
  0/4095 *before* the panel edge. The clamp that used to be applied asserted raw 4095 is emitted at
  panel 479 when it is actually emitted at panel ~450, which tilted the outer segment so the reported
  position ran **ahead of the finger by up to +19 px across the bottom quarter**. Both the fit's clamp
  and `touch_input.c`'s legacy-migration `clamp_to_hw()` are deleted; endpoints outside the emittable
  range are the measurement, not an error.
  Both geometry lines are written by **one wizard** — Device Tools → **Display** → `CALIBRATE TOUCH`
  (`SCREEN EDGES` jumps to its margins step, its `REACH` step measures line 3; `RESET` restores
  hardware defaults). It runs with the
  bezel zeroed so a drawn pixel is a panel pixel, fits from **interior targets only**, and writes
  nothing until you confirm on the live mapping behind a 20 s auto-revert. **The fit lives in
  `common/touch_calib.c` and nowhere else** — it previously existed in three places with the same
  defect. **ScummVM and vnc_client each link their own copy of `touch_input.o`, so redeploy every
  component after changing that file or the calibration file format.** A stale binary does not error:
  its 4-number `sscanf` succeeds on the first four of the eight values and it accepts them as a legacy
  config, so touch silently collapses (a stale vnc_client read `X [0..1020] Y [3074..4095]`).
- **Screen edges are the library's problem, not the app's.** The bezel covers ~15px top and bottom.
  `fb_init()` shrinks the drawing surface to the visible rectangle and `fb_swap()` places it on the
  panel at the viewport origin, leaving the hidden bands black — so `fb.width`/`fb.height` and
  `SCREEN_VISIBLE_*` are the **logical** screen (800×450 at the shipped margins) and every pixel in it
  is visible. Never add your own bezel arithmetic. Margins default to `FB_BEZEL_*_DEFAULT`
  (15/15/0/0) when the config has no line 2.
- **Drawable ≠ pressable: the digitizer saturates before the panel edge on Y.** Every edge *does*
  drive raw to 0/4095 — all 16 sweep buckets on all four edges — but the value is **clipped flat over
  a band inside the edge**, and on Y that band is ~30 px. Measured on RW09 2026-08-01 with
  `touch_raw`'s SWEEP + INSET modes (calibration and bezel zeroed, so a drawn pixel is a panel pixel):
  raw 4095 is first emitted at panel ~450, raw 0 at panel ~30; on X the flat band is ~0–12 px.
  Confirmed independently by the 11-target interior fit, which never sees an edge sample and predicts
  the same 30 / 450. **A bezel press alone cannot distinguish "clipping starts at the edge" from
  "clipping starts 30 px inside it"** — which is why the earlier revision of this bullet was wrong on
  exactly that point, and why the bug survived three sessions. So the codebase carries **two**
  rectangles: `SCREEN_VISIBLE_*` (the full logical screen — backgrounds, titles, status rows,
  playfields) and `SCREEN_SAFE_*` (visible **∩** touchable — buttons, toggles, tab bars, touch grids).
  The band between them is good screen area and stays fully drawable; it just must not hold anything
  the user has to press. The inset is **measured at runtime, never hardcoded** — `publish_safe_area()`
  pushes the raw edge extremes through the production `scale_coordinates()` — so it is `0` until a
  panel has been swept, and `SCREEN_SAFE_*` is only correct after `touch_init()`. Read it back from
  Device Tools → Display → `TOUCHABLE:` or the wizard's REPORT screen; the numbers above are the
  reference capture, and **the live inset is per-unit and per-calibration-run** — RW09's current fit
  publishes `X 6..793 Y 19..438 of 800x455`, i.e. ~19/16 on Y *and* ~6 px each side on X. **X's band is
  much smaller than Y's and can be zero, but a non-zero X inset is the model working, not a fault** —
  it comes from fitted X endpoints outside `0..4095`, exactly as on Y. A dead band is a **fact about
  this panel**, not a bug.
  **ScummVM and `vnc_client` are the exception to "the band is drawable, so use it":** their content
  is third-party — a remote taskbar, a game's verb bar, the ScummVM theme's button row — so nobody
  can audit which of its pixels must be pressable. Both confine the *content rectangle itself* to
  `SCREEN_SAFE_*` and leave the band black, with a per-component opt-out
  (`content_area = visible` in `vnc_client.conf`, `ROOMWIZARD_CONTENT_AREA=visible` /
  `rw_content_area=visible` for ScummVM) that moves only the picture, never their own buttons.
  Numbers, method and the raw capture: `SYSTEM_ANALYSIS.md#33-touch`.
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
| **Front door (menu over all of the below)** | `roomwizard.sh` | whenever you'd rather not remember the flags |
| SD-card commissioning | `commission-roomwizard.sh` | once, offline |
| System setup (cleanup, init, audio, time-sync, mDNS) | `setup-device.sh` | once, over SSH |
| Build + deploy all components | `deploy-all.sh` | per deploy |
| Per-component build/deploy/manifest | `*/build-and-deploy.sh` | per component |
| App respawn loop at boot | `roomwizard-app-init.sh` (`/etc/init.d/roomwizard-app`) | every boot |

`roomwizard.sh` is a **composition layer with no logic of its own** except `wait_for_ssh`: every
item execs one of the scripts below it with arguments, and all of them stay non-interactive when
called directly. The one thing it adds is the two waits — after first boot, and after
`setup-device.sh`'s reboot — and it polls **SSH, not ping**, because ping answers while `sshd` is
still starting, which is exactly the window that produces a spurious "Cannot reach". They are not
merged because the cleanup's targets span four partitions that only a booted kernel assembles into
one tree; the full argument is in `COMMISSIONING.md`.

**Host name is set in two files, by one script.** `set-hostname.sh` writes `/etc/hostname` **and**
rewrites `/etc/hosts`, because the vendor image maps its own name to an unreachable corporate
address (`161.218.140.212 RW09.ppmd.siemens.net RW09`) — so *every* unit cloned from that image is
`RW09` pointing at a dead IP (`IMPROVEMENT_PLAN.md` D7). It is called from both bring-up paths —
`commission-roomwizard.sh` offline against `$ROOTFS`, and `setup-device.sh <ip> --hostname NAME`
over SSH — so the two cannot drift. `--hostname` is targeted and does **not** reboot, which is what
makes it usable on a unit in service as a live display. It is staged to `/tmp` per run rather than
installed, so it is deliberately *not* in `--status`'s drift list.

System setup is done **once** by `setup-device.sh` — component deploy scripts must not
duplicate it. `deploy-all.sh` auto-discovers components (any subdir with a
`build-and-deploy.sh`), always runs `native_apps` first, then sets `app_launcher` as
the default boot app.

**Stopping what is running belongs to the init script too, and it matches on the executable.**
`/etc/init.d/roomwizard-app stop` is the only implementation; the three component scripts call it and
must not carry a `killall` of their own. The reason is not tidiness: a name-based rule cannot see the
app that `app_launcher` *started*, and that grandchild is normally the process holding `/dev/fb0`. Its
basename is in no config file, so `do_stop()`'s old `killall $(basename default-app)` only ever killed
`app_launcher` — which is how a `vnc_client` survived a full `deploy-all.sh` plus two `stop` calls,
held the panel at 16bpp and repainted over every app that tried to start (`IMPROVEMENT_PLAN.md`
B25/B20). `app_pids()` walks `/proc/*/exe` against the three deploy directories instead, because the
exe link is the only identity neither chosen by the process nor limited to the configured app.
Two consequences: **`setup-device.sh <ip>` is what pushes that script**, so a `do_stop()` change does
not reach a device until it is re-run; and to see what is running, use
`/etc/init.d/roomwizard-app status` — `ps w` on this busybox is a trap, see
`SYSTEM_ANALYSIS.md#53-app-launcher-and-manifests`.

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
- `touch_input.c` (the raw→panel→logical map), `touch_calib.c` (**the** implementation of the
  calibration fit — targets, interior masks, per-axis verdict, sanity gate, `.bakN` backup; linked
  only by `device_tools` and `touch_raw`), `hardware.c` (LED/backlight sysfs), `common.c` (buttons, ModalDialog,
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
