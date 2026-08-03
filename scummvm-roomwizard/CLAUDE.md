# scummvm-roomwizard — authoring guide

Rules and traps for working on the ScummVM backend port. Deep reference (engine tables, memory
budget, bug history, optimisation log) is in [`SCUMMVM_DEV.md`](SCUMMVM_DEV.md). Device facts are
in [`../SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md). Open work is in
[`../IMPROVEMENT_PLAN.md`](../IMPROVEMENT_PLAN.md).

## The source-of-truth trap — read this first

The real source lives in **`../scummvm/backends/platform/roomwizard/`** (an untracked upstream
checkout). `backend-files/` in this repo is the *version-controlled copy*.

```bash
bash manage-scummvm-changes.sh sync     # scummvm/ edits  -> backend-files/   (do this after editing)
bash manage-scummvm-changes.sh restore  # backend-files/ -> scummvm/
```

Consequences you must respect:

- Editing `backend-files/` alone changes nothing that gets built. Either edit in `../scummvm/`
  and `sync`, or edit here and `restore`.
- `backend-files/README.md` is copied **from** the upstream tree and copied back by `sync`.
  Anything you write there is upstream-facing and will be overwritten if the upstream copy
  differs. Do not put RoomWizard project documentation in it.
- Restoring files from version control breaks `make`'s timestamp logic — the existing `.o` looks
  newer than the restored source, so you get a silently stale binary. `build-and-deploy.sh`
  handles this by `touch`ing restored sources and deleting the matching `.o`. If you copy files
  by hand, do the same.

## Build

```bash
./build-and-deploy.sh <ip>            # the supported path (all|clean|configure|build|strip|deploy|set-default|info)
```

`build_arm_deps()` cross-compiles zlib and libpng into `arm-deps/` and is idempotent. It runs on
every code path — do not add a shortcut that skips it.

**`clean` is the only clean path — do not add a second one.** A root-level `clean.sh` used to exist
and was deleted 2026-08-03 (`../IMPROVEMENT_PLAN.md` B19a): it was this tree's clean script with no
shebang and no `cd`, so from the repo root its `find . -name '*.o' -delete` reached
`native_apps/build/` and the `usb_host/linux-4.14.52/` kernel objects, and `-name '*.d'` matched
*directories* in the extracted rootfs under `partitions/`. `clean_build()` already does strictly
more than it did — `make clean` inside the tree, plus `native_apps/common/*.o`, which is the one
that actually bites (a stale x86 `.o` there fails the cross-build with "file format not
recognized").

**Never trust `config.mk`; check the artifact.** A stale `USE_PNG = 1` from an earlier configure
once made `make` compile `image/png.cpp` with no `libpng.a` on disk, failing on `png.h`. Test for
`arm-deps/lib/libpng.a`, not for the flag.

Ubuntu Focal WSL cannot do armhf multiarch (`dpkg --add-architecture armhf` fails — the standard
mirrors carry no armhf), which is why dependencies are built from source rather than installed.
To add one, extend `build_arm_deps()` following the zlib/libpng pattern and drop the matching
`--disable-*` from configure. Changing dependencies requires a full `clean` + `configure` + `make`.

When building libpng with `-mfpu=neon`, pass `-DPNG_ARM_NEON_OPT=0` — the NEON assembly files are
not part of the manual build and the linker will fail without it.

## Critical: never use `--whole-archive` with `-lpthread`

Static ARM builds targeting a kernel older than 5.1 must link `-lpthread` plainly.

`--whole-archive` pulls in *all* of glibc 2.31's pthread init, which calls `clock_gettime64`
(ARM syscall 403, added in kernel 5.1). On this device's 4.14.52 kernel that returns `-ENOSYS`,
after which glibc dereferences a NULL VDSO pointer — **SIGSEGV before `main()`**. Even
`scummvm --version` dies, and no log is written.

Diagnostic signature in `dmesg`:

```
PC is at 0x40
r0 : ffffffda        <- -38 = -ENOSYS
```

The native C apps escape this only because they never link pthread. If plain `-lpthread` ever
produces link errors, the options are an older-glibc toolchain, a sysroot with 4.14 headers,
musl, or dynamic linking — not `--whole-archive`.

## Architecture

```
ScummVM Core -> OSystem_RoomWizard
  |- RoomWizardGraphicsManager -> /dev/fb0   (RGB565, double-buffered)
  |- RoomWizardEventSource     -> /dev/input/event*  (touch, keyboard, mouse, gamepad)
  |- OssMixerManager           -> /dev/dsp   (22050 Hz MONO, O_NONBLOCK) -> TWL4030 -> SPKR1
  \- Default managers (timer, events, saves, filesystem)
```

**This backend runs the framebuffer at 16bpp RGB565**, not 32bpp. It calls `fb_set_bpp(...,16)`
on startup. Screenshots must be decoded with `--bpp 16`. Native apps run 32bpp and reset it on
launch, so the mode depends entirely on which app last ran.

**It links its own copy of `native_apps/common/touch_input.o` and `framebuffer.o`** (see
`backend-files/configure.patch`). The build refreshes them, but a *deployed* ScummVM keeps
whatever it was built with — redeploy ScummVM after changing touch or framebuffer code in
`native_apps/common/`.

**The backend is an ordinary `fb_init()` client, bezel included.** It has no bezel logic of its
own: `_fb->width`/`_fb->height` are the logical (visible) screen. Everything that indexes
`_fb->back_buffer` must use `fbWidth()`/`fbHeight()` as the stride and bounds — the buffer is
logical-sized (e.g. 800×450×2 bytes at 16bpp), so a hardcoded 800-pixel stride writes past the
allocation.

## Content and the overlay go in the touch-safe rectangle

`fbWidth()`/`fbHeight()` are the stride and bounds of the back buffer and **nothing else** — they are
not where content goes. The digitizer saturates before the panel edge, so a band at each end of the
visible surface is drawable but **not pressable** (~19 px top / ~16 px bottom / ~6 px each side on
RW09; measured at runtime by `touch_input.c`, `0` until a panel's edge reach has been swept). Neither
a game nor the GUI theme can be audited for which of its pixels have to be reachable, so:

| What | Rectangle | Set by |
|---|---|---|
| **overlay** — launcher, GMM, virtual keyboard | `safeWidth()` × `safeHeight()` at `safeLeft()`/`safeTop()`, **always** | `getOverlayWidth/Height()`, `_overlaySurface.create()`, the composite loop in `updateScreen()`, `drawCursor()`'s overlay branch |
| **game picture** | the safe rect by default; the whole surface when `rwFullContentArea()` | `getScalingInfo()` — the single chokepoint |
| gesture corners, overlay touch coordinates | safe rect, **always** | `roomwizard-events.cpp` via `safeRect()`, which reads `getSafeRect()` |
| virtual cursor (USB mouse, gamepad stick) | wherever the current content is — safe rect in the GUI, the *picture* in game mode | `cursorBounds()` in `roomwizard-events.cpp` |

The overlay surface **must** be exactly `getOverlayWidth()` × `getOverlayHeight()`: `ThemeEngine` and
the virtual keyboard `grabOverlay()` into a surface sized from those and `copyRectToOverlay()` it
straight back, so a mismatch corrupts every dialog backdrop.

`getScalingInfo()` returns offsets that already include the content rect's origin, which is why
`transformCoordinates()`'s game branch and `drawCursor()`'s game branch need no knowledge of any of
this. Keep it that way — one chokepoint, not three.

**Ordering, and the trap it fixes:** `SCREEN_SAFE_*` is only correct after both `fb_init()` and
`touch_init()`. `RoomWizardEventSource` is constructed *before* the framebuffer exists, so
`initSize()` calls `initFramebuffer()` and then **`syncScreenGeometry()` immediately**, before
sizing the overlay surface — that call is what republishes the geometry and with it the measured
inset. Moving it back below the surface creation silently sizes the overlay from the pre-bezel
defaults.

`ROOMWIZARD_CONTENT_AREA=visible` (one-off) or `rw_content_area=visible` in `scummvm.ini` opts the
**picture** out. It deliberately does not move the overlay or any gesture zone; an option that could
strand the launcher's bottom button row is a footgun, not a feature.

## The config file is at one absolute path

`OSystem_RoomWizard::getDefaultConfigFileName()` returns **`/opt/games/scummvm.ini`**. The base
`OSystem` returns the bare relative name `"scummvm.ini"`, which `Common::FSNode` resolves against the
process's **current directory** — and the cwd differs per launch method, because the init script does
not `cd` and `app_launcher` `execl()`s without `chdir()`. That gave RW09 three config files
(`/scummvm.ini` from boot, `/home/root/scummvm.ini` from SSH, `/opt/games/scummvm.ini` from a `cd`),
settings that did not follow the user between launch methods, and an "is the setting being ignored?"
report that was really an edit to the wrong file (`../IMPROVEMENT_PLAN.md` B3h, fixed 2026-08-03).
`OSystem_POSIX` solves this with an absolute `$HOME/.config` path, but this backend derives from
`ModularGraphicsBackend`, not from it. `/opt/games` is the right home: next to the binary, the icons
and the game data, writable, and independent of `$HOME`, which the init script does not set.

**Backend defaults belong in `initBackend()`, and must be flushed.** ConfMan only persists keys that
were actually *set* — `registerDefault()` is not written to the file either — so a key the backend
merely reads with `hasKey()` never appears in `scummvm.ini`, and the option is undiscoverable from the
device (`../IMPROVEMENT_PLAN.md` B3g). `initBackend()` therefore writes `rw_content_area=safe` on first
run, behind `!ConfMan.hasKey()` so a user's `visible` is never overwritten. Use **`setAndFlush`**, not
`set`: `quit()` calls `exit(0)` and bypasses ScummVM's normal shutdown flush, so a plain `set()` can be
lost on the one exit path this device actually takes. `ConfMan` is loaded (`base/main.cpp:478`) well
before `initBackend()` (`:572`), and both the read and the write go through
`createConfigReadStream`/`createConfigWriteStream`, hence through the override above — so the default
lands in the same one file.

Numbers and method: [`../SYSTEM_ANALYSIS.md#33-touch`](../SYSTEM_ANALYSIS.md#33-touch).

## Audio

The OSS shim's bugs are device facts and documented in
[`../SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#34-audio). The backend-specific consequences:

- **Mono, 22050 Hz.** Stereo is not merely unsupported — the shim silently ignores
  `SNDCTL_DSP_STEREO`, so interleaved L/R gets consumed as separate frames and everything plays
  at half speed. Mono also halves OPL synthesis load. ScummVM's mixer downmixes automatically.
- **Set SPEED -> FMT -> CHANNELS**, then read back with `SOUND_PCM_READ_RATE/BITS/CHANNELS` and
  use the *read-back* rate for `_outputRate`. Set-ioctl return values do not reflect device state,
  and a wrong `_outputRate` makes OPL run at the wrong tempo.
- **`O_NONBLOCK` + wall-clock deadline pacing.** Blocking `write()` stalls ~506 ms on the ALSA
  hardware period. Treat `EAGAIN` as a safety valve, not the pacing mechanism.
- **No `SNDCTL_DSP_SETFRAGMENT`** — the default ~500 ms ring is the jitter buffer.
- **`SCHED_OTHER`, never `SCHED_RR`.** An RT audio thread starves the main thread on this
  single core and you get a black screen.
- **Pre-fill 3 silence buffers** (~280 ms) before the pacing loop or the first playback XRUNs.
- **50 % attenuation** (`>>1` post-mix) — the speaker distorts at full scale.

Quickest audio test: KQ3 `intro`, which starts music immediately.

## Debugging

```bash
ssh root@<ip> 'ROOMWIZARD_DEBUG=1 /opt/games/scummvm'   # touch circles + touch-state logging
ssh root@<ip> '/opt/games/scummvm > /tmp/scummvm.log 2>&1'
```

`WARNING` goes to stderr and appears immediately; stdout is block-buffered, so redirect both.
`top -H` gives per-thread CPU.

## Gesture navigation

Triple-tap in a corner: bottom-right = Global Main Menu, bottom-left = virtual keyboard,
top-right = Enter. The 80 px corner zones are gesture-only — taps there are suppressed from the
game. Emit a synthetic `LBUTTONUP` when an overlay opens or closes, or SCI games get stuck
walking.

## Rendering

Software only. The optimisation log (O1–O12, CPU 80 % -> 32 %) is in
[`SCUMMVM_DEV.md`](SCUMMVM_DEV.md#optimization-backlog). The reusable techniques — palette LUTs,
precomputed source-column tables, row deduplication, skipping `fb_swap` on unchanged frames — are
summarised in [`../CLAUDE.md`](../CLAUDE.md).

Note that O9 ("DSS hardware scaler — not viable") was **wrong**; the OMAP3 DSS exposes three
overlay planes with an independent-input/output-size hardware scaler at
`/sys/devices/platform/omapdss/`, usable from sysfs with no kernel work. See
`../IMPROVEMENT_PLAN.md` F2 — ScummVM is the prime candidate.

## Leaving a game, and why `quit()` keeps its `exit(0)`

**Do not replace `OSystem_RoomWizard::quit()`'s `exit(0)` with a `_quit` flag.** It is not on the
game-exit path and never was: across the whole ScummVM tree the only caller of `OSystem::quit()` is
`common/recorderfile.cpp`, and `OSystem_SDL::quit()` does `destroy(); exit(0);` too. Exiting the
process here is *correct* on this device — it is how the init script's respawn hands the panel back to
`app_launcher`.

What decides whether leaving a game returns to the ScummVM launcher is the launcher loop in
`base/main.cpp:832`, which `break`s out — ending the process — when a game returns `kNoError`, no
return-to-launcher was requested, and neither `kFeatureNoQuit` nor `gui_return_to_launcher_at_exit` is
set. That is upstream's default on every platform, so "it returns to the launcher on Ubuntu" was a
desktop config with that Global Options box ticked, not a backend difference. `initBackend()` sets
`gui_return_to_launcher_at_exit` on first run (`../IMPROVEMENT_PLAN.md` B12b, fixed 2026-08-03).

**`kFeatureNoQuit` satisfies the same condition and is a trap.** It also hides the `Quit` button on the
ScummVM launcher (`gui/launcher.cpp:264`) *and* the in-game global menu (`engines/dialogs.cpp:90`), and
loops `launcherDialog()` until a game starts. Quitting ScummVM is the only route back to `app_launcher`
and the native games, so that traps the user inside ScummVM. Never set it here.

## 32-bit target

`sizeof(long) == 4`. Baseline all timing to a start timestamp captured at init; never multiply a
raw `tv_sec` by 1000 or 1000000. `getMillis()` does the multiply in `uint32` since 2026-08-03, so it
wraps cleanly at 49.7 days rather than overflowing signed `time_t` at 24.85 — **built, deployed and
verified on RW09 the same day** (`../IMPROVEMENT_PLAN.md` B10). All six `getMillis()` consumers
compute `now - _last` in `uint32` and are wrap-safe; keep it that way.
