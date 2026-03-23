# ScummVM RoomWizard - Development Guide

## Status: ✅ GUI + touch + keyboard + mouse + gamepad + audio (OPL/AdLib music + SFX) working

**Binary:** ~15 MB statically linked → `/opt/games/scummvm` on device  
**Version:** ScummVM 2.8.1pre with custom RoomWizard backend  
**Build env:** WSL Ubuntu-20.04, arm-linux-gnueabihf-g++ 9, `--enable-vkeybd`

---

## Quick Build & Deploy

```bash
cd scummvm
./configure --host=arm-linux-gnueabihf --backend=roomwizard \
  --disable-all-engines --enable-engine=scumm --enable-engine=scumm-7-8 --enable-engine=he \
  --enable-engine=agi --enable-engine=sci --enable-engine=agos --enable-engine=sky --enable-engine=queen \
  --disable-mt32emu --disable-flac --disable-mad --disable-vorbis \
  --enable-release --enable-optimizations --enable-vkeybd
make -j4 LDFLAGS='-static' LIBS='-lpthread -lm'
arm-linux-gnueabihf-strip scummvm
scp scummvm root@192.168.50.73:/opt/games/
ssh root@192.168.50.73 'chmod +x /opt/games/scummvm'
```

First-time extras:
```bash
# Scaled virtual keyboard pack
scp ../scummvm-roomwizard/vkeybd_roomwizard.zip root@192.168.50.73:/opt/games/
# To regenerate: python3 ../scummvm-roomwizard/make_vkeybd_scaled.py vkeybd_small_source.zip ../scummvm-roomwizard/vkeybd_roomwizard.zip 2
```

Sync backend source to version control — see [`README.md`](README.md#backend-files) or use:
```bash
bash manage-scummvm-changes.sh sync     # scummvm/ edits → backend-files/
bash manage-scummvm-changes.sh restore  # backend-files/ → scummvm/
```

---

## Architecture

```
ScummVM Core → OSystem_RoomWizard
  ├── RoomWizardGraphicsManager  → /dev/fb0 (800×480 RGB565, double-buffered)
  ├── RoomWizardEventSource      → /dev/input/event* (touch, keyboard, mouse w/ cursor, gamepad)
  ├── OssMixerManager            → /dev/dsp (22050 Hz MONO, O_NONBLOCK) → TWL4030 → SPKR1
  └── Default managers (timer, events, saves, filesystem)
```

Backend files: [`backends/platform/roomwizard/`](../scummvm/backends/platform/roomwizard/) — `roomwizard.cpp/h`, `roomwizard-graphics.cpp/h`, `roomwizard-events.cpp/h`  
OSS mixer: [`backends/mixer/oss/oss-mixer.cpp/h`](../scummvm/backends/mixer/oss/oss-mixer.cpp)

---

## Gesture Navigation

| Gesture | Zone | Action |
|---|---|---|
| Triple-tap | Bottom-right (x>720, y>400) | Global Main Menu (Ctrl+F5) |
| Triple-tap | Bottom-left (x<80, y>400) | Virtual Keyboard |
| Triple-tap | Top-right (x>720, y<80) | Enter key |

Corner zones are gesture-only — all taps in 80px corners are suppressed from the game. `_waitForRelease` blocks new touch until finger lifts. Overlay-transition guard emits synthetic LBUTTONUP when GMM opens/closes to prevent "stuck walking" in SCI games.

---

## Audio

**Signal path:** OssMixerManager → `/dev/dsp` (O_NONBLOCK, 22050 Hz mono S16_LE) → ALSA OSS shim → TWL4030 DAC1 → HandsfreeL/R → SPKR1  
**Amp enable:** GPIO12 HIGH (set by `/etc/init.d/audio-enable` at boot)  
**Hardware audio details:** See [`SYSTEM_ANALYSIS.md#audio`](../SYSTEM_ANALYSIS.md#audio)

**Design choices:**
- **O_NONBLOCK** — prevents 506 ms ALSA HW-period stall
- **No `SNDCTL_DSP_SETFRAGMENT`** — keeps default ~500 ms ring as jitter buffer
- **Wall-clock deadline pacing** — sleep until `now + usPerBuf` after each write; `EAGAIN` is safety valve only
- **Mono output** — single speaker; eliminates stereo/mono mismatch bugs; halves all audio-thread work
- **22050 Hz** — halves OPL synthesis load vs 44100
- **2048-frame buffer** — 93 ms at 22050 Hz; 4096 bytes mono
- **SCHED_OTHER** — SCHED_RR starved main thread on single-core ARM; ~500 ms ring absorbs jitter
- **50% volume attenuation** — `>>1` on int16 samples post-mix; prevents speaker distortion
- **Read-back ioctls** — `SOUND_PCM_READ_RATE/BITS/CHANNELS` verify actual device state after setup; `_outputRate` uses read-back rate so OPL sample-counting matches real playback
- **Ring pre-fill** — 3 silence buffers (~280 ms) written before pacing loop starts; prevents initial XRUN
- **XRUN detection** — `SNDCTL_DSP_GETOSPACE` monitors ring fill level; emergency extra buffer if near-empty
- **Diagnostic counters** — every ~10s logs: ring fill, EAGAIN count, write errors, deadline resets, XRUNs

### OSS Stereo Caveat (ALSA OSS shim)

The ALSA OSS shim on this device (Linux 4.14.52, TWL4030) has **known bugs**:
1. `SNDCTL_DSP_STEREO` is silently ignored (returns success but stays mono)
2. `SNDCTL_DSP_SPEED` may reset format and/or channels
3. `SNDCTL_DSP_SETFMT` may reset speed
4. Set-ioctl output values may NOT reflect the actual device state

When stereo interleaved L/R samples hit a mono device, each sample is consumed as a separate frame → **half-speed playback**. When `_outputRate` doesn't match the actual device rate, OPL generates music at the wrong tempo (half-speed if `_outputRate` is 2× real rate).

**Fix:** (1) Mono mixer — `MixerImpl(_outputRate, false, _samples)` + `SNDCTL_DSP_CHANNELS(1)`. (2) Set SPEED first, then FMT, then CHANNELS (so FMT/CH survive any reset from SPEED). (3) Read back actual device params with `SOUND_PCM_READ_RATE/BITS/CHANNELS` and use the read-back rate for `_outputRate`. ScummVM's mixer handles stereo→mono downmix for DualOPL2/OPL3/iMUSE sources automatically.

**Diagnostic:** [`native_apps/tests/ch_test.c`](../native_apps/tests/ch_test.c) — verifies channel ioctl behavior. On-device log shows read-back values: `OssMixerManager: read-back: rate=N bits=N channels=N`.

---

## Debug Mode

```bash
ssh root@192.168.50.73 'ROOMWIZARD_DEBUG=1 /opt/games/scummvm'
```

Enables touch feedback circles and TOUCH_NONE→PRESSED debug logging. Read once at startup (`getenv`), cached — zero runtime overhead when off.

**Debugging tips:**
- KQ3 `intro` plays music immediately — quickest audio test
- `> /tmp/scummvm.log 2>&1` — WARNING lines go to stderr immediately (stdout is block-buffered)
- `top -H` shows per-thread CPU

---

## Bug Fix History

Condensed log of issues found and fixed. Full debugging context is in git history.

| Bug | Root Cause | Fix | Files |
|---|---|---|---|
| OPL music = noise fragment | `mixCallback(buf, _samples)` passed frame count instead of byte count | Pass `bufBytes` = samples × bytes-per-frame | `oss-mixer.cpp` |
| OPL music half-speed | ALSA OSS shim ignores stereo → L/R consumed as separate mono frames | Switched to mono mixer entirely | `oss-mixer.cpp` |
| Black screen (post-audio fix) | SCHED_RR audio thread starved main thread on single-core ARM | Removed SCHED_RR, use SCHED_OTHER | `oss-mixer.cpp` |
| Black screen (persistent) | 32-bit `long` overflow in frame-rate cap (`timeval` delta from epoch) | Init timing baseline to current time, not epoch | `roomwizard-graphics.cpp` |
| Audio "bru-bru" stuttering | Blocking `write()` stalls for 506 ms ALSA HW period | O_NONBLOCK + wall-clock pacing + no SETFRAGMENT | `oss-mixer.cpp` |
| OPL still half-speed after mono fix | `_outputRate` may not match actual device rate (set-ioctl output unreliable) | Read back actual rate with `SOUND_PCM_READ_RATE`; use that for `_outputRate` | `oss-mixer.cpp` |
| Audio breakup after extended play | Ring buffer draining below safe level (wall-clock pacing drift / XRUN) | Pre-fill ring with 3 silence buffers; GETOSPACE monitoring; emergency refill on near-empty | `oss-mixer.cpp` |
| **Input not working (12 sub-bugs)** | Multiple issues across event system, graphics manager, and build system | See breakdown below | multiple |

### Input Bug Fixes (12 sub-bugs)

Keyboard, mouse (with cursor), and gamepad all verified working in launcher and in-game (EcoQuest, Full Throttle).

| Sub-bug | Fix | Files |
|---|---|---|
| Keymapper disabled | `allowMapping()` returns `true` — enables keyboard/gamepad mapping | `roomwizard-events.cpp` |
| Mouse movement lost on click | Flush pending mouse movement before emitting button events | `roomwizard-events.cpp` |
| In-game mouse events ignored | `getScreenChangeID()` returns incrementing counter (was returning 0) | `roomwizard-graphics.cpp` |
| Cursor not visible/tracking | Cursor position synced from event manager in `updateScreen()` | `roomwizard-graphics.cpp` |
| Cursor coordinates wrong in-game | Cursor position scaled to framebuffer coordinates in game mode | `roomwizard-graphics.cpp` |
| Cursor palette broken | `setFeatureState(kFeatureCursorPalette)` properly implemented | `roomwizard-graphics.cpp` |
| Stale `.o` files after backend changes | `build-and-deploy.sh` auto-restores backend files with `touch` + stale `.o` cleanup | `build-and-deploy.sh` |

**32-bit ARM lesson:** `sizeof(long) == 4` — never compute `(now.tv_sec - epoch_0) * 1000000L`. Always init timing baselines to current time.

---

## Build & Cross-Compilation Learnings

Hard-won lessons from build and runtime failures. These are non-obvious pitfalls specific to the RoomWizard cross-compilation environment.

### `png.h: No such file or directory` (March 2026)

**Symptom:** `image/png.cpp:28:10: fatal error: png.h: No such file or directory` when building via `./deploy-all.sh <IP>`

**Root cause:** The `deploy` command path in `build-and-deploy.sh` ran `build_scummvm()` → `make` without calling `build_arm_deps()` first. The `arm-deps/` directory with cross-compiled zlib and libpng didn't exist. However, `config.mk` from a prior configure still had `USE_PNG = 1`, so make tried to compile `image/png.cpp` which `#include <png.h>`.

**Why it wasn't caught before:** The `all` command path calls `build_arm_deps()` → `configure_build()` → `build_scummvm()` in order. The `deploy` path was a shortcut that skipped dependency building.

**Fix:**
- Added `build_arm_deps` call at the start of `build_scummvm()` — runs on every code path, idempotent (skips if `libpng.a` exists)
- Strengthened staleness check to verify `$ARM_DEPS_PREFIX/lib/libpng.a` exists on disk, not just that `config.mk` says `USE_PNG = 1`

**Lesson:** Generated config files can become stale. Always verify the actual build artifacts exist, not just config flags.

### SIGSEGV before `main()` — glibc 2.31 static pthread vs kernel 4.14.52 (March 2026)

**Symptom:** ScummVM crashes immediately with `Segmentation fault (core dumped)`. Even `scummvm --version` crashes. No log file created. `dmesg` shows:
```
PC is at 0x40
LR is at 0x130415d
r0 : ffffffda                     ← -38 = -ENOSYS
ISA ThumbEE
```

**Root cause:** The build used `-Wl,--whole-archive -lpthread -Wl,--no-whole-archive` for static linking. This forces ALL symbols from `libpthread.a` into the binary, including glibc 2.31's pthread initialization code that uses `clock_gettime64` (syscall 403 on ARM).

The device kernel **4.14.52 does NOT support `clock_gettime64`** — this syscall was added in **kernel 5.1**. During glibc's `__libc_start_main`, the pthread init path calls `clock_gettime64`, gets `-ENOSYS` (r0=-38), then dereferences a NULL VDSO function pointer at offset 0x40 → **SIGSEGV before `main()` ever executes**.

**Why native C apps weren't affected:** The C apps (brick_breaker, app_launcher, etc.) don't link with `-lpthread` at all, so the problematic pthread initialization code is never included.

**Build environment:**

| Component | Version |
|-----------|---------|
| Cross-compiler | `arm-linux-gnueabihf-g++` 9.4.0 |
| Cross glibc | 2.31 (built against kernel 5.4 headers) |
| Cross linux-libc-dev | 5.4.0 headers |
| Target kernel | **4.14.52** (pre-time64, pre-`clock_gettime64`) |

**Diagnostic signature:** If you see `PC=0x40, r0=0xffffffda (-ENOSYS)` in dmesg, this is the classic glibc 2.31 `clock_gettime64` + static pthread + old kernel crash.

**Fix:** Changed `LIBS += -Wl,--whole-archive -lpthread -Wl,--no-whole-archive` to `LIBS += -lpthread`. Without `--whole-archive`, the linker only includes pthread symbols actually referenced by ScummVM code, avoiding the problematic `clock_gettime64` initialization path.

**⚠️ CRITICAL RULE:** NEVER use `--whole-archive` with `-lpthread` for static ARM builds targeting kernel < 5.1. The glibc 2.31 pthread library contains `clock_gettime64` syscall code that crashes on pre-5.1 kernels.

**Alternative approaches if `-lpthread` alone causes link errors:**
1. Use a cross-toolchain with older glibc (2.27 or earlier, pre-time64)
2. Build a custom sysroot with `linux-libc-dev` headers matching kernel 4.14
3. Switch to musl-libc for static linking (musl handles old kernels gracefully)
4. Switch to dynamic linking and use the device's own libc.so

---

## Optimization Backlog

**Baseline:** CPU ~80% → **32%** after all optimizations (LSL5 gameplay, OPL music + animation)

| # | Optimization | Status | Notes |
|---|---|---|---|
| O1 | Precomputed `_palette32[256]` LUT | done | 1 indexed load replaces 7 ops/pixel |
| O2 | Precomputed `srcXtab[]` + row pointer lifting | done | Eliminates per-pixel division |
| O3 | Border-only clear (skip overwritten pixels) | done | |
| O4 | Cached `rwSystem()` global | done | Eliminates `dynamic_cast` per poll |
| O5 | Dead code removal | done | |
| O6 | Right-click fix (LBUTTONUP→RBUTTONDOWN sequence) | done | Correctness, no CPU impact |
| O7 | Skip `fb_swap` on unchanged frames | done | Menu CPU 35%→15% |
| O8 | 16bpp RGB565 framebuffer | done | Halves write bandwidth |
| O9 | OMAP3 DSS hardware scaler | not viable | `caps.ctrl=0`, no kernel support |
| O10 | NEON `vst1q_u16` 8-pixel blit | done | |
| O11 | Row deduplication (L1-cache tempRow) | done | 57% of scaled rows are dupes |
| O12 | Mono mixer | done | Halves audio-thread work |

---

## Limitations & Open Issues

- **Single-touch only** — right-click = long-press 500 ms
- **PNG crash ("No PNG support compiled!")** — **Fixed.** Clicking the thumbnails/grid-view icon in the launcher would crash with "No PNG support compiled!" because the ARM cross-compilation sysroot lacked `libpng`. The `./configure` script silently disables PNG when it cannot find `libpng` for the target architecture. **Fix:** the build script now automatically cross-compiles zlib 1.3.1 and libpng 1.6.43 from source into a local `arm-deps/` prefix directory (idempotent — skips if already built), then passes `--with-zlib-prefix` and `--with-png-prefix` to `./configure`. No manual package installation needed; Ubuntu Focal WSL doesn't support armhf multiarch (`dpkg --add-architecture armhf` fails because standard mirrors don't carry armhf). After changing dependencies, a full rebuild is required (`clean` + `configure` + `make`).
- **Software rendering only** — no GPU, no DSS scaler
- **No MIDI** — NullMidiDriver; future option: software synth (FluidSynth or similar)
- **OPL tempo** — verify on device with KQ3 intro vs reference recording after mono mixer fix
- **Exit game returns to launcher on Ubuntu but exits ScummVM on RoomWizard** — the backend's `quit()` is being called when the engine exits instead of returning to the ScummVM launcher. Likely cause: `OSystem_RoomWizard::quit()` calls `exit()` unconditionally rather than setting a flag that lets the main loop drop back to the launcher. Compare with SDL backend's `_quit` flag + launcher loop.
- **Wireframe green UI (missing theme data)** — **Fixed.** The build-and-deploy script now automatically deploys `scummremastered.zip` and `gui-icons.dat` from `gui/themes/` to `/opt/games/` on the device. If you still see the green wireframe UI, re-run `./build-and-deploy.sh <ip>` to redeploy the theme files.

## ScummVM Engine Availability on RoomWizard

### Currently Enabled
All default (stable, build-by-default=yes) ScummVM engines are enabled. Engines with unmet library dependencies are automatically skipped by configure.

### 16-bit Color Support (USE_RGB_COLOR)
The RoomWizard backend was added to the 16-bit backend whitelist via [`configure.patch`](backend-files/configure.patch). This enables `USE_RGB_COLOR` which is required by ~30 engines. After building, verify with:
```bash
grep USE_RGB_COLOR scummvm/config.h
```
If not defined, ~17 engines will be silently skipped (ADL, Blade Runner, Blazing Dragons, Freescape, Griffon Legend, Grim, Hopkins FBI, Hypno/UFOs, mTropolis, NGI, Pegasus Prime, Tony Tough, Trecision, V-Cruise, Riven, Ultima IV/VIII).

### Skipped Engines — Missing Library Dependencies

| Library to Cross-Compile | Engines Unlocked | Priority |
|--------------------------|-----------------|----------|
| **libjpeg** | Groovie 2, Myst 3, Myst ME, Wintermute, Titanic, Glk (partial) — 7 engines | High |
| **FreeType2** | Buried in Time, Z-Vision, Petka, Stark (partial), Glk (partial) — 6 engines | Medium |
| **Lua 5.x** | HDB, Sword25 (partial), Ultima VI — 4 engines | Medium |
| **libvorbis** (remove `--disable-vorbis`) | Nancy Drew, Stark (partial) — 3 engines | Low |
| **libmad** (remove `--disable-mad`) | AGS, Titanic (partial) — 2 engines | Low |
| **libtheora** | Sword25 (partial) — 2 engines | Low |

To add a library, extend `build_arm_deps()` in [`build-and-deploy.sh`](build-and-deploy.sh) following the zlib/libpng pattern, and remove any `--disable-*` flags from configure.

### WIP/Unstable Engines (can be force-enabled)

These engines have all their dependencies met but are marked `build-by-default=no`. They can be force-enabled by adding `--enable-engine=<name>` to the configure call:

Avalanche, Chamber, Cryo/Lost Eden, DM/Dungeon Master, ICB/In Cold Blood, Immortal, Last Express, Lilliput, MacVenture, MADS V2, Carmen Sandiego (Mohawk sub), Mutation of JB, Sludge, Star Trek, Ultima I, WAGE

**Note**: Playground 3d and TestBed are testing/debug engines, not games.

### Cannot Be Enabled (require OpenGL GPU)

Hpl1, Watchmaker — require OpenGL which is not available on the RoomWizard hardware.

### Build Artifacts Note
When compiling libpng with `-mfpu=neon`, add `-DPNG_ARM_NEON_OPT=0` to disable libpng's NEON assembly optimizations (the assembly files are not compiled in our manual build, causing linker errors). See [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#5-libpng-arm-neon-optimization-issue) for details.

## Memory Budget

| Component | Size |
|---|---|
| Binary | ~14 MB |
| Game surface | ~1.2 MB |
| Framebuffer (double-buffered) | ~3 MB |
| Audio mix buffer (mono) | ~4 KB |
| **Total** | **~18 MB** of 184 MB available |