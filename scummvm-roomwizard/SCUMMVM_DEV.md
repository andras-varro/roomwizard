# ScummVM RoomWizard - Development Guide

## Status: ✅ GUI + touch + audio (OPL/AdLib music + SFX) working

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
  ├── RoomWizardEventSource      → /dev/input/event0 (touch, gestures)
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

**Diagnostic:** [`native_games/tests/ch_test.c`](../native_games/tests/ch_test.c) — verifies channel ioctl behavior. On-device log shows read-back values: `OssMixerManager: read-back: rate=N bits=N channels=N`.

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

**32-bit ARM lesson:** `sizeof(long) == 4` — never compute `(now.tv_sec - epoch_0) * 1000000L`. Always init timing baselines to current time.

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
- **Software rendering only** — no GPU, no DSS scaler
- **No MIDI** — NullMidiDriver; future option: software synth (FluidSynth or similar)
- **OPL tempo** — verify on device with KQ3 intro vs reference recording after mono mixer fix
- **Cursor not moved by touch** — in-game cursor stays at its initial position; touch events fire clicks at the correct game coordinates but `MOUSEMOVE` is either not synthesised or not applied to the hardware cursor. ScummVM uses `warpMouse()` + `MOUSEMOVE` events to drive the cursor position; `RoomWizardGraphicsManager` may need to implement `showMouse()`/`warpMouse()` or the event source needs to emit `MOUSEMOVE` before each `LBUTTONDOWN`.
- **Exit game returns to launcher on Ubuntu but exits ScummVM on RoomWizard** — the backend's `quit()` is being called when the engine exits instead of returning to the ScummVM launcher. Likely cause: `OSystem_RoomWizard::quit()` calls `exit()` unconditionally rather than setting a flag that lets the main loop drop back to the launcher. Compare with SDL backend's `_quit` flag + launcher loop.
- **Wireframe green UI (missing theme data)** — ScummVM falls back to the built-in theme because `gui-icons.dat` and the `scummremastered` skin are not present on the device. These files ship in the ScummVM data package. Copy them to `/opt/games/` alongside the binary (or to a path ScummVM searches, e.g. the same dir as the binary). Files needed: `gui-icons.dat`, `scummremastered.zip` (or the `scummremastered/` directory from the ScummVM data tarball).

## Supported Engines

SCUMM v0–v8, HE71+, AGI, SCI, AGOS, Beneath a Steel Sky, Flight of the Amazon Queen

## Memory Budget

| Component | Size |
|---|---|
| Binary | ~14 MB |
| Game surface | ~1.2 MB |
| Framebuffer (double-buffered) | ~3 MB |
| Audio mix buffer (mono) | ~4 KB |
| **Total** | **~18 MB** of 184 MB available |