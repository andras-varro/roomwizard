# ScummVM RoomWizard - Development Guide

## Status: ✅ WORKING — audio enabled 2026-02-23

**Binary:** ~14 MB statically linked  
**Location:** `/opt/games/scummvm` on device (192.168.50.73)  
**Last build:** 2026-02-23 (WSL Ubuntu-20.04, arm-linux-gnueabihf-g++ 9, `--enable-vkeybd`) — clean, 0 warnings  
**Last source edit:** 2026-02-23 — OssMixerManager: `O_NONBLOCK` + EAGAIN retry fixes TWL4030 506 ms HW-period stall (root cause of bru-bru-bru audio corruption)  
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

**ScummVM integration:** `OssMixerManager` (`backends/mixer/oss/oss-mixer.{h,cpp}`) opens `/dev/dsp` **O_NONBLOCK**, sets 44100 Hz stereo S16_LE, constrains the OSS ring to 2×16384 bytes (`SNDCTL_DSP_SETFRAGMENT` — must be first ioctl), then runs a dedicated pthread pulling `mixCallback()` → `write()`. The ALSA OSS shim performs SRC from 44100→48000 Hz in-kernel automatically.

**Why O_NONBLOCK is essential — the TWL4030 HW period problem:**  
Diagnosis via `native_games/tests/oss_diag.c` showed that blocking `write()` stalls for ~**506 ms** after the OSS ring fills, not the expected 93 ms:
```
write[0]:  30 ms   ← fills fragment 0
write[1]:   0 ms   ← fills fragment 1 (ring full, 32 KB = 185 ms)
write[2]: 421 ms   ← blocked waiting for ALSA HW period  ← root cause
write[3+]:506 ms   ← blocked for one full ALSA HW period every call
```
The TWL4030 ALSA driver has a hardware period of ~22,317 frames (~506 ms). The OSS shim's blocking `write()` waits for the ALSA HW period to complete before accepting more data — even though the OSS ring only holds 185 ms. Result: 185 ms audio → 321 ms silence → click → repeat ("bru-bru-bru-KLICK").

**Fix:** Open with `O_WRONLY | O_NONBLOCK`. `write()` copies data into the OSS software ring immediately and returns `EAGAIN` when full. The audio thread retries with a 5 ms sleep on `EAGAIN`. The OSS ring drains continuously at the hardware sample rate regardless of the underlying ALSA period size — no silent gaps.

**Buffer layout:**
- OSS ring: 2 fragments × 16384 bytes = 32768 bytes = 185 ms  
- Mix buffer: 4096 frames = 16384 bytes = 93 ms  
- Steady state: write fills fragment 0 instantly → sleeps ~18 × 5 ms on EAGAIN ≈ 90 ms until it drains → clean, continuous audio with ~185 ms latency

---

## Next Steps

*No outstanding blockers.* The watchdog is fed by `/usr/sbin/watchdog` system daemon — ScummVM runs indefinitely without any extra steps.

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
