# ScummVM RoomWizard - Development Guide

## Status: ✅ GUI + touch + audio (including OPL/MIDI music) working.

**Binary:** ~15 MB statically linked  
**Location:** `/opt/games/scummvm` on device (192.168.50.73)  
**Last build:** 2026-02-23 (WSL Ubuntu-20.04, arm-linux-gnueabihf-g++ 9, `--enable-vkeybd`)  
**Last source edit:** 2026-02-23 — Three audio/display fixes + volume attenuation:  
1. Fixed `mixCallback` byte-count bug (was passing frame count → 75% unmixed buffer, OPL at 1/4 rate)  
2. Removed SCHED_RR from audio thread (starved main thread on single-core ARM → black screen)  
3. Fixed 32-bit `long` overflow in `updateScreen()` frame-rate cap (epoch-relative subtraction overflowed → every frame skipped)  
4. Added 50% volume attenuation (−6 dB) to prevent small speaker distortion  
**Version:** ScummVM 2.8.1pre with custom RoomWizard backend

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

# Only needed once (deploy scaled keyboard pack):
scp ../scummvm-roomwizard/vkeybd_roomwizard.zip root@192.168.50.73:/opt/games/
# To regenerate vkeybd_roomwizard.zip from upstream source:
# python3 ../scummvm-roomwizard/make_vkeybd_scaled.py vkeybd_small_source.zip ../scummvm-roomwizard/vkeybd_roomwizard.zip 2

# Only needed once (or after game_selector changes) - run from native_games/:
arm-linux-gnueabihf-gcc -O2 -static -I. common/framebuffer.c common/touch_input.c \
  common/hardware.c common/common.c common/ui_layout.c game_selector/game_selector.c \
  -o build/game_selector -lm
scp build/game_selector root@192.168.50.73:/opt/games/
ssh root@192.168.50.73 'chmod +x /opt/games/game_selector && touch /opt/games/scummvm.noargs && chmod 644 /opt/games/scummvm.noargs'
# Marker files control game_selector behaviour (non-executable so they are never listed):
#   <name>.noargs  — launch without fb_dev/touch_dev args (ScummVM opens devices itself)
#   <name>.hidden  — hide entirely from the games list (dev tools, utilities)
# Hidden on device: watchdog_feeder touch_test touch_debug touch_inject
#                   touch_calibrate unified_calibrate pressure_test
# hardware_test is visible (useful diagnostics)
```

Sync backend source to version control after editing:
```powershell
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard-events.cpp   .\backend-files\ -Force
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard-events.h     .\backend-files\ -Force
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard-graphics.cpp .\backend-files\ -Force
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard.cpp          .\backend-files\ -Force
Copy-Item ..\scummvm\backends\platform\roomwizard\module.mk               .\backend-files\ -Force
Copy-Item ..\scummvm\backends\mixer\oss\oss-mixer.h                       .\backend-files\ -Force
Copy-Item ..\scummvm\backends\mixer\oss\oss-mixer.cpp                     .\backend-files\ -Force
```

---

## Architecture

```
ScummVM Core -> OSystem_RoomWizard
    +-- RoomWizardGraphicsManager -> framebuffer.c -> /dev/fb0
    +-- RoomWizardEventSource -> touch_input.c -> /dev/input/event0
    +-- OssMixerManager -> /dev/dsp (OSS compat) -> TWL4030 codec -> SPKR1
    +-- Default managers (timer, events, saves, filesystem)
```

Backend files in [`scummvm/backends/platform/roomwizard/`](../scummvm/backends/platform/roomwizard/):
- `roomwizard.cpp/h` — Main backend, `showVirtualKeyboard()`, feature flags
- `roomwizard-graphics.cpp/h` — 800x480 framebuffer, bezel-aware scaling
- `roomwizard-events.cpp/h` — Touch input, state machine, gesture detection

---

## What Works

| Feature | Status |
|---|---|
| ScummVM launches, GUI renders | OK |
| Framebuffer 800×480 @ 32bpp | OK |
| Bezel-aware full-screen scaling | OK |
| Touch calibration (`/etc/touch_calibration.conf`) | OK |
| Button clicks (LBUTTONDOWN/UP) | OK |
| Touch drag (MOUSEMOVE while held) | OK |
| Long-press right-click (500 ms) | OK |
| Quick-tap (press+release same poll cycle) | OK |
| Triple-tap bottom-right → Global Main Menu (Ctrl+F5) | OK |
| Triple-tap bottom-left → Virtual Keyboard | OK |
| Triple-tap top-right → Enter key | OK |
| VKB renders over game (overlay composite) | OK |
| VKB hit-areas match display size (`vkeybd_roomwizard.zip`) | OK |
| Overlay transparent clear-key 0xF81F (magenta) | OK |
| GMM opaque background | OK |
| VKB text input field opaque background | OK |
| Corner-zone tap suppression (taps 1+2 of triple-tap) | OK |
| Post-gesture `_waitForRelease` lockout | OK |
| Overlay-transition LBUTTONUP injection | OK |
| Ctrl+F5 releases Ctrl modifier cleanly | OK |
| Game selector integration (`.noargs` + `.hidden` markers) | OK |
| Audio via TWL4030 speaker (OssMixerManager, `/dev/dsp`) | OK |
| OPL / AdLib music (KQ3, LSL5, …) | OK (fixed: mixCallback byte-count bug) |
| Static linking | OK |
| Debug mode (`ROOMWIZARD_DEBUG=1`) | OK |

---

## Gesture Navigation

| Gesture | Zone | Action |
|---|---|---|
| Triple-tap | Bottom-right (x>720, y>400) | Global Main Menu (Ctrl+F5) |
| Triple-tap | Bottom-left (x<80, y>400) | Virtual Keyboard |
| Triple-tap | Top-right (x>720, y<80) | Enter key (dismiss "Press ENTER" prompts) |

**Corner zones are gesture-only** — ALL taps in the 80px corner areas are suppressed from
the game. Taps 1 and 2 of a triple-tap are silently consumed; tap 3 fires the gesture.
This prevents character movement to corners during gesture input.

**Key implementation details (`roomwizard-events.cpp`):**
- `checkGestures()` — records tap timestamps per corner, fires on 3rd tap within 1200 ms
- `_waitForRelease` flag — set on gesture fire AND on every corner-zone tap; blocks all new
  touch processing until hardware reports finger lifted (`held=false`)
- `touch_poll()` resets `pressed`/`released` each call. Quick taps where press+release land
  in the same poll are handled in `TOUCH_PRESSED`: emits LBUTTONDOWN + queues LBUTTONUP.
- **Overlay-transition guard** — `_prevOverlayVisible` tracks overlay state each poll. When
  overlay transitions (GMM opens/closes), any in-flight `TOUCH_HELD` is terminated with a
  synthetic LBUTTONUP and `_waitForRelease` set. Prevents SCI "stuck walking" caused by
  LBUTTONDOWN going to the overlay context and LBUTTONUP going to the game context.

---

## Screen Scaling & Calibration

Run once on device:
```bash
ssh root@192.168.50.73 '/opt/games/unified_calibrate'
# Phase 1: tap 4 corner crosshairs -> touch offset calibration
# Phase 2: tap +/- zones -> bezel margin adjustment
# Saves to /etc/touch_calibration.conf automatically
```

**Current device calibration:**
```
TL(8,19) TR(-9,25) BL(8,-22) BR(-8,-27)
bezel: T=10 B=10 L=0 R=0
```

**Data flow:**
1. `initTouch()` -> `touch_load_calibration()` -> touch offset correction active
2. `initFramebuffer()` -> `loadBezelMargins()` -> bezel T/B/L/R parsed
3. `getScalingInfo()` -> scaled area = (800-L-R) x (480-T-B), aspect-clamped, centered
4. `blitGameSurfaceToFramebuffer()` -> nearest-neighbour scale into region
5. `transformCoordinates()` -> touch to game coords via reverse getScalingInfo()

---

## Debug Mode

Set `ROOMWIZARD_DEBUG=1` before launching to enable runtime debug features:

```bash
ssh root@192.168.50.73 'ROOMWIZARD_DEBUG=1 /opt/games/scummvm'
```

| Feature | Off (default) | On |
|---|---|---|
| Touch feedback circles | disabled | Fading red circle on each touch |
| TOUCH_NONE→PRESSED log | silent | `debug()` to stdout |

The flag is read once at startup (`getenv`) and cached — no runtime overhead.  
`logMessage()` suppresses all `kDebug` output when the flag is off, so any future `debug()` call is automatically silent without needing individual gating.

---

## Audio Details

**Hardware path:** TWL4030 HiFi codec (ALSA card `rw20`, `hw:0,0`) → HandsfreeL/R class-D amp → SPKR1  
**Driver:** ALSA OSS compatibility shim (`/dev/dsp`, `/dev/audio`, `/dev/mixer`)  
**Amp enable:** GPIO12 (sysfs) must be driven HIGH; done by `/etc/init.d/audio-enable` at boot  
**Mixer path (must be configured at boot):**
```
DAC1 → HandsfreeL/R Mux (AudioL1/R1) → HandsfreeL/R Switch (on)
```
Boot script `/etc/rc5.d/S29audio-enable` → `/etc/init.d/audio-enable` sets GPIO12 and runs `amixer` to configure the path.  
ALSA mixer state (DAC1 volumes, Predrive) saved to `/var/lib/alsa/asound.state` via `alsactl store` and restored by `/etc/init.d/alsa-state`.

**ScummVM integration:** `OssMixerManager` (`backends/mixer/oss/oss-mixer.{h,cpp}`) opens `/dev/dsp` **O_NONBLOCK**, sets **22050 Hz** stereo S16_LE (no `SNDCTL_DSP_SETFRAGMENT`), then runs a dedicated pthread pulling `mixCallback()` → volume attenuation (50% / −6 dB) → `write()` with wall-clock pacing. 22050 Hz halves OPL synthesis workload and avoids the expensive non-integer SRC from 44100→48000 in the ALSA OSS shim.

**Root-cause history and final design:**

| Attempt | Problem | Effect |
|---|---|---|
| Blocking write + no SETFRAGMENT | Ring ~500 ms; write() never blocks | Audio thread spins 100% CPU |
| Blocking write + SETFRAGMENT(32KB) | ALSA HW period 506 ms >> OSS ring 185 ms | write() stalls 506 ms; 321 ms silence gaps ("bru-bru") |
| O_NONBLOCK + SETFRAGMENT(32KB) + usleep(5ms) | Scheduling jitter stretches usleep; 18×20ms=360ms > 185ms ring | Fragment-boundary underruns ("Te-te-CLICK") |
| **O_NONBLOCK + no SETFRAGMENT + wall-clock deadline** | ✅ | Large ring absorbs jitter; deadline sleep is precise |

**Final design:**
- **O_NONBLOCK** — prevents the 506 ms ALSA HW-period stall on `write()`
- **No `SNDCTL_DSP_SETFRAGMENT`** — keeps the default ~500 ms ring; provides a large jitter buffer
- **Wall-clock deadline pacing** — after each `write()`, the thread sleeps until `now + usPerBuf`. This is the primary pacing mechanism; the thread is idle ~90/93 ms of the time. `EAGAIN` is only a safety valve for the rare ring-full case (e.g. after resume).
- **2048-frame mix buffer** (93 ms at 22050 Hz) — one `mixCallback()` call per deadline period
- **SCHED_OTHER** (default priority) — SCHED_RR was removed because it starved the main thread on single-core ARM after the mixCallback fix increased audio CPU load 4×. The ~500 ms ring easily absorbs SCHED_OTHER jitter.
- **50% volume attenuation** — arithmetic right-shift (>>1) on all int16 samples post-mix. The RoomWizard's small speaker distorts at full-scale output.

**Diagnosis tool:** [`native_games/tests/oss_diag.c`](../native_games/tests/oss_diag.c) — measures write() blocking, EAGAIN behaviour, and ring geometry on the device.

---

## OPL / AdLib Music — Fixed ✅

### Previous symptom
Games using OPL2 AdLib emulation (KQ3, LSL5, most SCI/AGI titles) played music as a **repeated noise fragment** — the OPL sequencer never advanced past the first note.

### Root cause — `mixCallback` byte-count bug in `OssMixerManager`
`MixerImpl::mixCallback(byte *samples, uint len)` expects `len` in **bytes** (documented in `mixer_intern.h`: *"Length of the provided buffer to fill (in bytes, should be divisible by 4)"*). The OSS mixer was passing `_samples` (frame count = 2048) instead of byte count (should be 8192 = `_samples * 4`):

```cpp
// BEFORE (broken):
_mixer->mixCallback(buf, _samples);     // 2048, interpreted as bytes
// Inside mixCallback: memset(buf,0,2048) then len>>=2 → 512 frames mixed

// AFTER (fixed):
_mixer->mixCallback(buf, bufBytes);     // 8192 (= _samples * 4)
// Inside mixCallback: memset(buf,0,8192) then len>>=2 → 2048 frames mixed
```

**Effects of the bug:**
1. Only 25% of each audio buffer was actually mixed; the rest was stale/uninitialized data
2. `EmulatedOPL::readBuffer()` generated only 512 frames per 93 ms buffer — the OPL music
   callback fired at ~22 Hz instead of ~100 Hz → sequencer barely advanced
3. 75% of the buffer sent to `/dev/dsp` was garbage → "repeated noise fragment"

**Key insight:** The previous diagnosis ("timer starvation via `TimerManager`") was wrong.
ScummVM's OPL emulators (`DOSBox::OPL`, `MAME::OPL`, `NUKED::OPL`) are all `EmulatedOPL`
subclasses. They do **not** use `TimerManager::installTimerProc()`. Instead, they fire
callbacks internally via `readBuffer()` based on sample-counting. The `checkTimers()` calls
in `delayMillis()`/`pollEvent()` are irrelevant for OPL music (but harmless and useful for
any timer that does use `TimerManager`).

**Comparison with other backends:**
- Atari: `_mixer->mixCallback(_samplesBuf, _samples * _outputChannels * 2)` ✅
- MaxMod: `mixer->mixCallback(dest, length * 4)` ✅
- SDL: receives `len` from SDL callback ✅ (already bytes)

Files changed: [`backend-files/oss-mixer.cpp`](backend-files/oss-mixer.cpp).

### Threading / mutex notes (retained for reference)

`NullMutexInternal` + `DefaultTimerManager` + cooperative `checkTimers(10)` in both
`delayMillis()` and `pollEvent()` remain in place. This avoids priority-inversion deadlocks
between the SCHED_RR audio thread and the main thread on single-core ARM. The OPL music
fix does not depend on any of this — it was purely a `mixCallback` argument error.

### Debugging tips
- KQ3 `intro` sequence plays music immediately after start — quickest test.
- Run with `> /tmp/scummvm.log 2>&1`; `WARNING:` lines go to stderr and appear immediately (stdout is block-buffered to file).
- `top -H` — a `SCHED_RR` audio thread appears as priority `-11` or `rt` in the `PR` column.

---

---

## SCHED_RR Removal — Fixed ✅

### Previous symptom
After the mixCallback byte-count fix, ScummVM displayed a **black screen** on startup. The GUI never appeared, though the process was running.

### Root cause — audio thread CPU starvation
The audio thread had `SCHED_RR` (real-time round-robin) priority 10. Before the mixCallback fix, it mixed only 512 frames (25% of the buffer) per 93 ms cycle — fast and light. After the fix, it correctly mixes 2048 frames — **4× more CPU work**. On a single-core 300 MHz ARM, the RT-priority audio thread now consumed enough CPU during init to starve the main thread, preventing any screen rendering.

### Fix
Removed the `SCHED_RR` block from `OssMixerManager::init()`. The ~500 ms OSS ring buffer absorbs 20–40 ms of SCHED_OTHER scheduling jitter easily. No audio glitches observed.

Files changed: [`backend-files/oss-mixer.cpp`](backend-files/oss-mixer.cpp).

---

## Frame Rate Cap Overflow — Fixed ✅

### Previous symptom
Black screen persisted even after removing SCHED_RR. ScummVM was running (responsive to SSH kill), but never rendered anything to the framebuffer.

### Root cause — 32-bit `long` overflow in `updateScreen()`
The 30fps frame-rate cap in `updateScreen()` used:
```cpp
static struct timeval _lastFrame = {0, 0};
// ...
long _elapsedUs = (now.tv_sec - _lastFrame.tv_sec) * 1000000L
                + (now.tv_usec - _lastFrame.tv_usec);
if (_elapsedUs < 33333) return;  // skip frame
```

On 32-bit ARM, `long` is 32 bits (range ±2.1 billion). With `_lastFrame` initialized to epoch (1970), the delta is ~1.74 billion seconds. Multiplying by 1,000,000 produces `1,740,000,000 * 1,000,000 = 1.74×10¹⁵`, which **overflows** a 32-bit signed `long` to a **negative** value. Since a negative value is always `< 33333`, **every frame is skipped forever**.

### Fix
Changed to a first-call initialization pattern:
```cpp
static bool _firstFrame = true;
static struct timeval _lastFrame;
if (_firstFrame) {
    gettimeofday(&_lastFrame, nullptr);
    _firstFrame = false;
}
```
Also added `_elapsedUs > 0` guard to handle rare `gettimeofday` jitter.

### Lesson learned
On 32-bit ARM (`sizeof(long) == 4`), **never** subtract epoch-relative `timeval` values that may be far apart. Always initialize timing variables to the current time on first use.

Files changed: [`backend-files/roomwizard-graphics.cpp`](backend-files/roomwizard-graphics.cpp).

---

## Volume Attenuation — Applied ✅

The RoomWizard's small PCB speaker distorts at full-scale DAC output. All int16 samples are attenuated by 50% (−6 dB) via arithmetic right-shift (`>>1`) immediately after `mixCallback()` returns and before writing to `/dev/dsp`. This is a zero-allocation, branch-free operation on every buffer (2048 frames × 2 channels = 4096 samples).

Files changed: [`backend-files/oss-mixer.cpp`](backend-files/oss-mixer.cpp).

---

## Next Steps

1. Watchdog is fed by `/usr/sbin/watchdog` system daemon — ScummVM runs indefinitely.
2. If more volume control is needed, the attenuation shift can be changed (>>2 = 25%, >>0 = 100%).

---

## Touch Input Details

**Hardware:** Single-touch resistive, Panjit panjit_ts
- 12-bit raw (0-4095) scaled to 800x480
- Binary pressure; no multi-touch
- Event order: ABS_X/Y -> BTN_TOUCH -> SYN_REPORT (coords arrive before press flag)
- `touch_poll()` resets `pressed` and `released` to false each call; `held` is persistent

**State machine:** TOUCH_NONE -> TOUCH_PRESSED -> TOUCH_HELD -> TOUCH_NONE

**Coordinate transform (game mode):**
```cpp
getScalingInfo(scaledW, scaledH, fbOffsetX, fbOffsetY);
gameX = (touchX - fbOffsetX) * gameWidth  / scaledW;
gameY = (touchY - fbOffsetY) * gameHeight / scaledH;
```

---

## Graphics Details

**Framebuffer:** 800x480 @ 32-bit ARGB (double-buffered)
**Game resolutions:** 320x200, 320x240, 640x480 — scaled to fill bezel-safe area
**Scaling:** Nearest-neighbour, aspect-ratio-preserving
**Pixel formats:** CLUT8 (palette), RGB565, ARGB8888 -> ARGB8888
**Cursor:** Software cursor composited over game surface

---

## Hardware Specs (quick ref)

- **CPU:** ARMv7 with NEON SIMD
- **RAM:** 184 MB available
- **Display:** 800x480 framebuffer `/dev/fb0`
- **Input:** `/dev/input/event0`
- **Audio:** TWL4030 HiFi codec → `/dev/dsp`; amp enable GPIO12; speaker SPKR1
- **OS:** Linux 4.14.52
- **Watchdog:** 60 s hardware reset; fed by `/usr/sbin/watchdog` system daemon

Full specs: [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#hardware-platform)

---

## Limitations

- Single-touch only; right-click = long-press 500 ms
- Software rendering only
- No MIDI (NullMidiDriver)

## Supported Engines

**SCUMM v0-v6:** Monkey Island 1-2, DOTT, Sam & Max, Indiana Jones, Loom
**SCUMM v7-v8:** Full Throttle, The Dig, Curse of Monkey Island
**HE71+:** Putt-Putt, Freddi Fish, Pajama Sam, Spy Fox
**AGI/SCI:** King's Quest, Space Quest, Leisure Suit Larry series

## Memory Budget

- Binary: ~14 MB
- Game surface: ~1.2 MB
- Framebuffer: ~3 MB (double-buffered)
- Audio mix buffer: ~8 KB
- Total: ~18 MB of 184 MB available

---

## Related Resources

- **SSH / device setup:** [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md)
- **Touch patterns:** [`native_games/PROJECT.md`](../native_games/PROJECT.md#touch-input)
- **Calibration tool source:** [`native_games/tests/unified_calibrate.c`](../native_games/tests/unified_calibrate.c)
- **Framebuffer reference:** [`native_games/common/framebuffer.c`](../native_games/common/framebuffer.c)
- **Watchdog:** [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#1-hardware-watchdog-timer)
