# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## What this is

Projects that repurpose the **Steelcase RoomWizard** — a wall-mounted meeting-room display — into a
games/apps platform. The device is an embedded Linux box: **TI OMAP3503** ARM Cortex-A8 @ 600 MHz,
234 MB RAM, 800×480 framebuffer, projected-capacitive touch, kernel 4.14.52, SysVinit. No GPU, no
DSP — all rendering is software. Display is legacy **omapfb/omapdss**; there is no DRM/KMS.

**There is no local app to run.** Everything is cross-compiled on the dev host and deployed over SSH
to a physical device (reference unit `192.168.50.73`, aka RW09). Verifying a change means deploying it
and looking at the panel — framebuffer screenshots via `fb565_to_png.py`.

### One fact, one home

Three top-level docs with disjoint jobs. **If something appears in two of them, the other copy is
stale.**

| Doc | Answers | Read it |
|---|---|---|
| `SYSTEM_ANALYSIS.md` | *What is true about this device?* — silicon, board, boot chain, OS, traps | Before touching anything hardware-related (audio, USB, watchdog, boot, GPIO, cross-compile) |
| `IMPROVEMENT_PLAN.md` | *What should we do about it?* — open bug + feature backlog with `file:line` | **Before starting work**, so you don't rediscover a known bug |
| `CLAUDE.md` (this file) | *What must I know before my first edit?* | Loaded every session |

`SYSTEM_ANALYSIS.md` is organised by subsystem — what's there, how to drive it, the gotchas — plus the
full board teardown and a photo index for [`HardwarePhotos/`](HardwarePhotos/).
`IMPROVEMENT_PLAN.md` holds **open work only**; closed items are in `git log --grep=<id>`.

Each component directory has its own `CLAUDE.md` (`native_apps/`, `scummvm-roomwizard/`,
`vnc_client/`), loaded automatically when you work on files there. **Those describe their own code
only** — device facts belong in `SYSTEM_ANALYSIS.md`, open work in `IMPROVEMENT_PLAN.md`. Link, don't
copy. `native_apps/CLAUDE.md` is the deepest of the three; go there for the rendering loop, the input
model, the touch model and the screen rectangles.

**Kernel policy: do not attempt a kernel rebuild or upgrade.** The vendor's kernel source is not
available (the repo's `usb_host/linux-4.14.52/` is vanilla upstream, missing both `panjit_ts` and the
panel driver), and requesting it from Steelcase is ruled out. Anything gated on a kernel config change
is out of scope — `SYSTEM_ANALYSIS.md#7-kernel-policy`.

## Working style

**Delegate the reading.** The docs are long by design and the sources are large (`device_tools.c` is
2651 lines). Answering "where is X / which call sites do Y / does this pattern hold across all seven
games" by reading files into the main context burns the budget the actual edit and its verification
need. Spin up subagents for that — one per independent question, in parallel — and keep only their
conclusions. Do the edits, the build and the on-device verification in the main thread.

**A recorded cause and a prescribed fix are both hypotheses.** `open, confirmed` in
`IMPROVEMENT_PLAN.md` means the *symptom* reproduced — nothing more. The cause written beside it and
the fix suggested under it were both written by someone who had not yet done the work, and both have
been wrong more than once, each time refutable by a single `grep`. So: reproduce the symptom, then
find the cause yourself. **When an entry says "compare with X", read X** — do not assume it contains
the pattern being described. And before setting a flag, read every use of it: the first mechanism that
*would* produce the symptom is not automatically the right fix.

**Give every new check a negative control, and ask which part of the count is the harness.** A gate
that reports a number can be wrong in both directions, and this repo has hit both — a gate counting
artifacts it could not actually read (false negative), a `/proc` scanner counting its own `grep` argv
(false positive), and the `sdiv`/`udiv` gate firing ~9 times on a *stripped* binary and zero on the
identical file unstripped. Put search patterns and multi-line harnesses in files, not in argv. Skip
inputs your tool cannot inspect rather than passing them. **If a fix is supposed to drive a number to
zero, check that it reaches zero** — a small residue is the tell, both times it happened.

**Write the failing version first.** There is no CI, so a test that has only ever been seen passing is
not evidence that it can fail. Compile it against the pre-fix source and count the failures.

**Say what you could not verify.** "Latent, not reproducible from this host" and "reported as *I think
it works*" are the honest forms. Do not promote a hedge to a confirmation.

**Editing the long docs.** Anchor an `Edit` on text you intend to **keep**, and expect to fix inbound
links whenever you retitle a heading. The IDE's markdownlint is a free dangling-link checker —
**`MD051/link-fragments` fires on exactly the anchors you just broke**; `comm -23` of the
`(#fragment)`s against the slugified headings confirms a whole file. (`MD060` table warnings fire on
every table in the repo — that is the established style, not something you introduced.) A long
multi-line `old_string` can fail to match for no visible reason; fall back to
`head -N` / `cat <<'EOF'` / `tail -n +M`, which also preserves LF. **Never bulk-edit a source file
with a python script** — it rewrites the whole file's line endings. Use `Edit`, or `sed -i`.

## Build & deploy

Everything builds with the ARM cross-compiler and deploys over SSH. There is **no CI, no test runner,
no lint** — "tests" are host-gcc regressions over pure-logic functions plus interactive on-device
diagnostic tools.

```bash
./roomwizard.sh                     # front door: a menu over everything below

./commission-roomwizard.sh          # bring-up 1: SD-card prep (offline; sets host name)
./setup-device.sh <ip>              # bring-up 2: SSH cleanup + init service + SW-watchdog bypass

./deploy-all.sh <ip>                # build + deploy everything (native_apps first)
./deploy-all.sh <ip> <component>    # one component;  --list  to see them

cd native_apps && ./build-and-deploy.sh [<ip>] [set-default]
```

`--help` on any of them is current; `README.md` has the annotated walkthrough. Two shapes worth
knowing without looking them up: **`set-default` is the only mode `native_apps/build-and-deploy.sh`
accepts** (anything else is rejected rather than ignored), and **cleanup, bloatware removal and the
boot service all live in `setup-device.sh`** — `--remove`, `--deep-clean`, `--status`, `--hostname
NAME` — never in a component script.

Components (each a subdir with a `build-and-deploy.sh`): `native_apps` (C games + launcher + tools),
`scummvm-roomwizard` (ScummVM backend port), `vnc_client`, `usb_host` (USB host-mode enablement +
Xbox controller modules).

**Toolchain:** `arm-linux-gnueabihf-gcc` (`sudo apt install gcc-arm-linux-gnueabihf`). ScummVM
additionally needs **WSL Ubuntu 20.04+** and `g++-arm-linux-gnueabihf`; it cross-compiles its own
zlib/libpng. `usb_host` needs kernel-module build deps (`bc libssl-dev bison flex`) + `python3`.

**`native_apps/` has no `Makefile`.** `native_apps/build-and-deploy.sh` (cross-compiler, `-static`) is
the only build path and the source of truth for how binaries are built and shipped. A **new** binary
goes in `GAMES_BINARIES` there and nowhere else — that one array drives the upload, the remote
`chmod +x` and the md5 verification. Details: `native_apps/CLAUDE.md`.

### Redeploy scope by changed file

All three components link objects from `native_apps/common/`, so what you touched decides how much has
to go out. A *deployed* binary keeps whatever it was built with, and a stale one does not error — it
misparses.

| Changed | Redeploy |
|---|---|
| an app's own source, `common/common.c`, `common/gamepad.c` | `native_apps` |
| `common/hardware.c`, `common/config.c`, `common/logger.c` | `native_apps` + `vnc_client` |
| `common/framebuffer.c`, `common/touch_input.c` | **all three** — `./deploy-all.sh <ip>`; ScummVM is the slow one |
| `roomwizard-app-init.sh`, `disable-steelcase.sh` | neither — **only** `./setup-device.sh <ip>`, which ends in a reboot |

When in doubt, over-deploy. The failure mode is silent.

## Working from this host — Windows, WSL and the tools

The repo lives on `c:\work\roomwizard`; **everything that compiles or decodes lives in WSL.** These are
tool-level traps rather than device facts, and each has cost real time.

- **WSL is not just for the cross-compiler.** Git Bash has no host `gcc` and no usable `python3`, so
  the host regressions (`tests/*_test.c`) and every `fb565_to_png.py` decode need WSL too. Invoke as
  `wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard/<component> && ./build-and-deploy.sh <ip>"`.
- ⚠️ **`command -v python3` succeeds in Git Bash and the interpreter does not exist.** It resolves to
  the Windows App Execution Alias — a real file that prints *"Python was not found"* and fails. Test by
  running `python3 --version`, not by looking it up. WSL's python3 has `PIL`, which the framebuffer
  decoder needs.
- **Git Bash `/tmp` and WSL `/tmp` are different filesystems.** Write captures somewhere under
  `c:\work\roomwizard` and both sides reach them.
- **The Bash tool's working directory does not reliably persist between calls.** Use absolute paths.
- **No foreground `sleep`** — it is blocked. Use `run_in_background`, or put the sleep inside the remote
  command: `ssh root@<ip> 'sleep 3; …'` works, because the local command is `ssh`.
- **A compound `ssh` command can be refused by the permission classifier.** Re-issue it as one plain
  single-purpose command. The same applies to a `bash -c` heredoc whose body contains apostrophes.
- **File modes are unobservable here.** `/mnt/c` is DrvFs 9p: it reports every file `-rwxrwxrwx` and
  silently discards `chmod`. A missing-`+x` bug can neither fire nor be demonstrated on this host, and
  you cannot build a negative control for one by `chmod`ing under `/mnt/c`.
- **Every disk here reports `removable = 0`**, including the root disk, and a `wsl --mount`ed card
  lands on the same `/dev/sd?` names as `/`. So a "removable media only" gate rejects everything.
  Resolve the root disk before writing to any device:
  `lsblk -rnso NAME "$(findmnt -no SOURCE --target /)" | tail -1`. `mount | grep ^/dev/sdX` is not a
  substitute — it never lists swap.
- **`shellcheck` is not installed in this WSL** (`IMPROVEMENT_PLAN.md` C7). `bash -n` is what you have,
  plus `dash -n` on anything carrying a `/bin/sh` shebang.
- **Never run a ScummVM build concurrently with a `native_apps` build.**
  `scummvm-roomwizard/build-and-deploy.sh` does `rm -f native_apps/common/*.o` at two points —
  deliberately, because a stale x86 `.o` there fails the cross-build with "file format not
  recognized". It takes ~1m35s–2m20s; wait it out.
- Markdown files are **LF** in the working tree but `.gitattributes` pins only `*.sh`, `*.py`, `*.conf`
  and `*.app`. With `core.autocrlf=true` set on this host, expect git to warn about LF→CRLF on docs.

## Non-obvious constraints (things that will silently break)

- **You cannot script a touch interaction, and the tool that claims to will lie to you.** There is no
  `/dev/uinput` (`CONFIG_INPUT_UINPUT` unset, no module), and evdev's `write()` path is for *output*
  events (force feedback, LEDs) — so `tests/touch_inject.c` writes to `/dev/input/event0`, **prints
  "injected successfully", exits 0, and delivers nothing to any reader.** Automated on-device
  verification therefore stops at the **first** screen: SSH-launch the binary, `cat /dev/fb0`, decode,
  assert. Past that, write a tap-by-tap checklist for a human instead of a test.
  ⚠️ **This is a limit on the *device*, not on the code.** `gamepad.c`'s whole state machine is
  host-testable — `gamepad_poll()` takes the touch coordinate as a plain argument and its evdev sources
  are `read(2)` on an fd, so a temp file of `struct input_event` drives the real path. Ask whether the
  thing needs the *kernel* or only needs *events*. Detail: `native_apps/CLAUDE.md` → *Input*.
- **Cortex-A8 has no hardware integer divide.** A binary containing an `sdiv`/`udiv` *instruction*
  crashes instantly with SIGILL (exit 132) — blank screen, no output, no log, indistinguishable from
  "the app didn't start". **Verify with `native_apps/check-arm-safe.sh`**, which runs automatically
  from `build-and-deploy.sh` before every deploy and on build-only runs. The expected count is a
  **hard zero** across all 31 ARM artifacts, and it is zero. The bare `$CC -O2 -static` deploy path is
  already safe; what would break it is an explicit `-march` implying the idiv extension. Two ways to
  get a wrong answer out of the gate — matching the line instead of the mnemonic field, and gating a
  *stripped* binary — are in `SYSTEM_ANALYSIS.md#61-cortex-a8-has-no-hardware-integer-divide`.
- **Framebuffer bpp is per-app — confirm it before decoding a screenshot.** `/dev/fb0`'s format is
  global mutable state: every native app pins **32bpp XRGB8888** via `fb_set_bpp(dev, 32)` before
  `fb_init()`; **ScummVM and the VNC remote session run 16bpp RGB565** to halve write bandwidth.
  `framebuffer.c`'s primitives dispatch on `bytes_per_pixel`, so the public API takes RGB888 at either
  depth and no depth corrupts the buffer — the reason to pin is **determinism and appearance**, not
  memory safety.
  Screenshot: `ssh root@<ip> cat /dev/fb0 > fb.raw`, then `python3 fb565_to_png.py fb.raw fb.png`
  (defaults to 32bpp; `--bpp 16` for ScummVM/VNC screens). **Run `fbset | grep geometry` on the device
  and believe it** — an app killed mid-session leaves the panel in a mode nothing running asked for,
  and a 32bpp frame is exactly the size of two 16bpp pages, so file size never catches the mistake.
  Full table and the RGB565-read-as-32bpp signature: `SYSTEM_ANALYSIS.md#32-display`.
- **Screen edges are the library's problem, not the app's.** The bezel hides ~15 px top and bottom.
  `fb_init()` shrinks the drawing surface to the visible rectangle and `fb_swap()` places it at the
  viewport origin, leaving the hidden bands black — so `fb.width`/`fb.height` and `SCREEN_VISIBLE_*`
  are the **logical** screen (800×450 at the shipped margins) and every pixel in it is visible. Never
  add your own bezel arithmetic.
- **Drawable ≠ pressable.** The digitizer's reading saturates *inside* the panel edge — a band of
  ~30 px at each end of Y, much smaller on X but not always zero. So there are **two** rectangles:
  `SCREEN_VISIBLE_*` (everything drawable — backgrounds, titles, status rows, playfields) and
  `SCREEN_SAFE_*` (visible **∩** touchable — buttons, toggles, tab bars, touch grids). The band
  between them is good screen area and stays fully drawable; it just must not hold anything the user
  has to press. **A dead band is a fact about this panel, not a bug.**
  The inset is **measured at runtime, never hardcoded or asserted** — it is per-unit and
  per-calibration-run, and `0` until a panel has been swept. Read it from Device Tools → Display →
  `TOUCHABLE:`. Choosing per call site needs the **hit-test**, not the look: see
  `native_apps/CLAUDE.md` → *Screen edges*. Numbers and method: `SYSTEM_ANALYSIS.md#33-touch`.
  **ScummVM and `vnc_client` are the exception to "the band is drawable, so use it":** their content is
  third-party, so nobody can audit which of its pixels must be pressable. Both confine the *content
  rectangle itself* to `SCREEN_SAFE_*`, with a per-component opt-out that moves only the picture, never
  their own buttons.
- **Touch coordinates must be captured before the `BTN_TOUCH` press event** (order: ABS_X, ABS_Y,
  BTN_TOUCH, SYN_REPORT). `common/touch_input.c` maps a reading in **two stages** — raw 12-bit →
  **panel** pixel by a per-axis piecewise-linear curve, then panel → **logical** by subtracting the
  bezel viewport origin. Stage 1 is calibration, stage 2 is the bezel; they are separate, and a fit for
  stage 1 must be done in panel coordinates or the bezel is subtracted twice.
  - ⚠️ **Never clamp a fitted endpoint into `0..4095`.** A correct fit legitimately extrapolates
    outside it, because the interior line reaches the raw limits *before* the panel edge. Endpoints
    outside the emittable range are the measurement, not an error.
  - **The fit lives in `common/touch_calib.c` and nowhere else** — it previously existed in three
    places with the same defect. Both geometry lines are written by one wizard: Device Tools →
    Display → `CALIBRATE TOUCH`.
  - ⚠️ **ScummVM and `vnc_client` each link their own `touch_input.o`, so redeploy every component
    after changing that file or the config format.** A stale binary does not error: its 4-number
    `sscanf` succeeds on the first four of the eight values, accepts them as a legacy config, and touch
    silently collapses. Extend the config with new keyword-tagged trailing lines; **never change line
    1's field count.**
  - Format, the measured reach, the model and the reference capture: `SYSTEM_ANALYSIS.md#33-touch`.
    Authoring rules: `native_apps/CLAUDE.md` → *Touch model*.
- **Don't probe I2C bus 1.** `pv02_app 5` (the vendor light-sensor factory test) can hang the bus, and
  **bus 1 carries the PMIC** — `SYSTEM_ANALYSIS.md#39-i2c`. There is no light sensor to find: the
  enclosure has no aperture at all.
- **The Steelcase software watchdog reboots the device ~every 70 min** in game mode. It is a cron job
  (`/opt/sbin/watchdog/watchdog.sh`), and **`disable-steelcase.sh` is what disables it** —
  `touch /var/watchdog_test` as its *first* command, deliberately ahead of every fallible line, plus a
  freshly written crontab. It prints whether the bypass is in place. `setup-device.sh <ip>` deploys and
  runs it; `/etc/init.d/roomwizard-app` re-runs it on **every boot**.
  **A device can be running an older copy than the repo's until `setup-device.sh` is re-run** —
  `./setup-device.sh <ip> --status` md5s both deployed scripts against the repo's and says
  `matches repo` or `DRIFTED` per file (read-only, no reboot). **Check that before reproducing anything
  against a device**, or you will draw conclusions about code the device is not running.
  The *hardware* watchdog (`/usr/sbin/watchdog`) is fine — keep it, but any app that takes over the
  screen for long periods must keep feeding `/dev/watchdog` (60 s) or the device hard-resets.
- **Audio via OSS `/dev/dsp` is buggy** (ALSA OSS shim on TWL4030): open with `O_NONBLOCK` and handle
  `EAGAIN` or you get a "bru-bru-KLICK" stall (~506 ms ALSA HW period); apply ~50 % software
  attenuation (the small speaker distorts at full scale); `SNDCTL_DSP_STEREO` is silently ignored, and
  SPEED/FMT/CHANNELS ioctls reset each other — set SPEED→FMT→CHANNELS and read back the actual rate
  with `SOUND_PCM_READ_*`. The hardware is permanently mono. See
  `scummvm-roomwizard/backend-files/oss-mixer.cpp` and `SYSTEM_ANALYSIS.md#34-audio`.
- **32-bit ARM:** `sizeof(long) == 4`. Never do `(now.tv_sec - 0) * 1000000L` — it overflows. Baseline
  timers to current time, not epoch 0.
- **Firmware / boot edits.** There is **no boot-time MD5 check** of the kernel and no signing — the
  only gate on `uImage-system` is its uImage header + data CRC (which `usb_host/patch_dtb.py`
  recomputes correctly). U-Boot has no `saveenv`, so the environment cannot be persisted or corrupted.
  **Rules:** never write `/dev/mtd*`; never overwrite `mlo`, `u-boot.bin` or `ctrlblock.bin` on p1;
  stage experimental kernels under a *new* filename and leave `uImage-system` alone — `bootcmd` is
  hardcoded to it, so a power cycle is a free undo. Observe that and JTAG never comes up. Detail and
  recovery procedure: `SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery`.

## Cross-component build rules

Learned the hard way in one component, applicable to all of them. **Full detail, measurements and the
flags each component actually uses: `SYSTEM_ANALYSIS.md#6-building-for-this-device`.**

- **Never use `--whole-archive` with `-lpthread` for static ARM builds.** It pulls in glibc 2.31's
  pthread init, which calls `clock_gettime64` (ARM syscall 403, kernel 5.1+). This kernel is 4.14.52,
  so it gets `-ENOSYS`, then dereferences a NULL VDSO pointer: **SIGSEGV before `main()`**, no output,
  no log. The `dmesg` signature is `PC is at 0x40` with `r0 : ffffffda`. Plain `-lpthread` is fine; the
  native C apps escape it only because they never link pthread at all.
- **Verify build artifacts on disk, not config flags.** Generated `config.mk`/`config.h` go stale — a
  leftover `USE_PNG = 1` once made the build compile `image/png.cpp` with no `libpng.a` present. Test
  for the `.a`, not the flag.
- **Cross-compiled dependencies must be built from source.** This WSL cannot do armhf multiarch, which
  is why ScummVM and the VNC client each build their own zlib/libpng/libjpeg into a local prefix. With
  `-mfpu=neon`, libpng needs `-DPNG_ARM_NEON_OPT=0`.
- **No `SCHED_RR` audio thread.** On this single 600 MHz core an RT audio thread starves the main
  thread and you get a black screen. `SCHED_OTHER` plus the ~500 ms OSS ring is enough.
- **The DSS *can* scale in hardware** — three overlay planes with independent input/output sizes at
  `/sys/devices/platform/omapdss/`, driven from sysfs with no kernel work. The only graphics
  acceleration on this GPU-less part; unused so far (`IMPROVEMENT_PLAN.md` F2).
- Before optimising a software renderer, read `SYSTEM_ANALYSIS.md#65-software-rendering-techniques-that-paid-off` —
  seven techniques that together took ScummVM from 80 % to 32 % CPU, and which the VNC client reused.

## Architecture

**Deployment model — strict separation of concerns:**

| Layer | Script | When |
|---|---|---|
| Front door (menu over everything below) | `roomwizard.sh` | whenever you'd rather not remember the flags |
| SD-card commissioning | `commission-roomwizard.sh` | once, offline |
| System setup (cleanup, init, audio, time-sync, mDNS) | `setup-device.sh` | once, over SSH |
| Build + deploy all components | `deploy-all.sh` | per deploy |
| Per-component build/deploy/manifest | `*/build-and-deploy.sh` | per component |
| App respawn loop at boot | `roomwizard-app-init.sh` (`/etc/init.d/roomwizard-app`) | every boot |

`roomwizard.sh` is a **composition layer with no logic of its own** except `wait_for_ssh`: every item
execs one of the scripts below it, and all of them stay non-interactive when called directly. It polls
**SSH, not ping**, because ping answers while `sshd` is still starting — exactly the window that
produces a spurious "Cannot reach". Commissioning and setup are not merged because the cleanup's
targets span four partitions that only a booted kernel assembles into one tree; the argument is in
`COMMISSIONING.md`.

**System setup is done once by `setup-device.sh`** — component deploy scripts must not duplicate it.
`deploy-all.sh` auto-discovers components (any subdir with a `build-and-deploy.sh`), always runs
`native_apps` first, then sets `app_launcher` as the default boot app.

**Host name is set in two files, by one script.** `set-hostname.sh` writes `/etc/hostname` **and**
rewrites `/etc/hosts`, because the vendor image maps the device's own name on a **non-loopback** line
to an unreachable address — so every unit cloned from it claims the same name and resolves it wrongly.
It is called from both bring-up paths, so the two cannot drift, and `--hostname` does **not** reboot,
which is what makes it usable on a unit in service as a live display.

**Stopping what is running belongs to the init script, and it matches on the executable.**
`/etc/init.d/roomwizard-app stop` is the only implementation; the three component scripts call it and
**must not carry a `killall` of their own.** The reason is not tidiness: a name-based rule cannot see
the app that `app_launcher` *started*, and that grandchild is normally the process holding `/dev/fb0` —
its basename appears in no config file. `app_pids()` walks `/proc/*/exe` against the three deploy
directories instead, because the exe link is the only identity neither chosen by the process nor
limited to the configured app. Two consequences: **`setup-device.sh <ip>` is what pushes that
script**, so a `do_stop()` change does not reach a device until it is re-run; and to see what is
running use `/etc/init.d/roomwizard-app status`, because `ps w` on this busybox lists only processes
with a TTY. See `SYSTEM_ANALYSIS.md#53-app-launcher-and-manifests`.

**App launcher + manifests.** The boot init script reads `/opt/roomwizard/default-app` and respawns
that binary whenever it exits (so exiting a game returns to the launcher). `app_launcher` scans
`/opt/roomwizard/apps/*.app` manifests (INI: `name=`, `exec=`, `icon=` PPM P6, `args=` one of
`fb,touch`/`fb`/`touch`/`none`) and renders them as a touch grid. Each component's deploy script writes
its own `.app` manifests + PPM icons — this is how projects plug into the launcher without a central
registry.

**native_apps shared C library (`native_apps/common/`)** — every game and tool links these:

- `framebuffer.c` — double-buffered 800×480 rendering, bpp-aware primitives, sprite blit
- `gamepad.c` — **unified input abstraction** across touch + USB keyboard + USB mouse + Xbox gamepad,
  mapped to abstract buttons (`BTN_ID_UP`…`BTN_ID_BACK`). New apps should use this rather than reading
  evdev directly. Configurable via `/etc/input_config.conf`.
- `touch_input.c` (the raw→panel→logical map), `touch_calib.c` (**the** implementation of the
  calibration fit — linked only by `device_tools` and `touch_raw`), `hardware.c` (LED/backlight sysfs
  plus the non-blocking `LedPulse`), `common.c` (buttons, `ModalDialog`, shared welcome/game-over
  screens), `ui_layout.c` (layouts + `ScrollableList`), `audio.c` (OSS beeps/tones), `config.c`
  (`/opt/games/rw_config.conf`), `keyboard.c` (on-screen touch keyboard), `highscore.c`, `ppm.c`,
  `logger.c`.

ScummVM has its **own independent** evdev input + OSS audio implementation (not the native_apps common
lib) but applies the same Cortex-A8 / OSS fixes.

## Device paths worth knowing

- `/opt/games/` — native binaries · `/opt/roomwizard/apps/` — `.app` manifests ·
  `/opt/roomwizard/icons/` — PPM icons · `/opt/roomwizard/default-app` — boot target
- `/dev/fb0` (framebuffer) · `/dev/input/event*` (evdev) ·
  `/sys/class/leds/{red_led,green_led,backlight}/brightness` (0–100)
- `/dev/dsp` (OSS audio, GPIO12 must be HIGH to unmute the speaker) · `/dev/watchdog` (60 s HW)
- `/etc/touch_calibration.conf` (touch curve + bezel) · `/etc/input_config.conf` (input mapping)
