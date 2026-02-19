# ScummVM RoomWizard - Development Guide

## Status: ✅ WORKING — VKB overlay + scaled keyboard + Enter gesture deployed 2026-02-18

**Binary:** 14 MB statically linked  
**Location:** `/opt/games/scummvm` on device (192.168.50.73)  
**Last build:** 2026-02-18 (WSL Ubuntu-20.04, arm-linux-gnueabihf-g++ 9, `--enable-vkeybd`)  
**Version:** ScummVM 2.8.1pre with custom RoomWizard backend

---

## Quick Build & Deploy

```bash
cd scummvm
./configure --host=arm-linux-gnueabihf --backend=roomwizard \
  --disable-all-engines --enable-engine=scumm --enable-engine=scumm-7-8 --enable-engine=he \
  --enable-engine=agi --enable-engine=sci --enable-engine=agos --enable-engine=sky --enable-engine=queen \
  --disable-alsa --disable-mt32emu --disable-flac --disable-mad --disable-vorbis \
  --enable-release --enable-optimizations --enable-vkeybd
make -j4 LDFLAGS='-static'
arm-linux-gnueabihf-strip scummvm
scp scummvm root@192.168.50.73:/opt/games/
ssh root@192.168.50.73 'chmod +x /opt/games/scummvm'

# Only needed once (deploy scaled keyboard pack):
scp ../scummvm-roomwizard/vkeybd_roomwizard.zip root@192.168.50.73:/opt/games/
# To regenerate vkeybd_roomwizard.zip from upstream source:
# python3 ../scummvm-roomwizard/make_vkeybd_scaled.py vkeybd_small_source.zip ../scummvm-roomwizard/vkeybd_roomwizard.zip 2
```

Sync backend source to version control after editing:
```powershell
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard-events.cpp  .\backend-files\ -Force
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard-events.h    .\backend-files\ -Force
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard-graphics.cpp .\backend-files\ -Force
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard.cpp         .\backend-files\ -Force
```

---

## Architecture

```
ScummVM Core -> OSystem_RoomWizard
    +-- RoomWizardGraphicsManager -> framebuffer.c -> /dev/fb0
    +-- RoomWizardEventSource -> touch_input.c -> /dev/input/event0
    +-- NullMixerManager (no audio)
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
| Framebuffer 800x480 @ 32bpp | OK |
| Bezel-aware full-screen scaling | OK - verified on device |
| Touch calibration file load | OK - /etc/touch_calibration.conf |
| Button clicks (LBUTTONDOWN/UP) | OK |
| Touch drag (MOUSEMOVE while held) | OK |
| Long-press right-click (500 ms) | OK |
| Quick-tap (press+release same poll cycle) | OK - fixed 2026-02-18 |
| Triple-tap bottom-right -> Global Main Menu (Ctrl+F5) | OK - confirmed |
| Triple-tap bottom-left -> Virtual Keyboard | OK - 2x scaled pack, overlay composited |
| Triple-tap top-right -> Enter key | OK |
| VKB renders over game (not replacing it) | OK - fixed 2026-02-18 |
| VKB hit-areas match display size | OK - `vkeybd_roomwizard.zip` at 640x480 |
| Post-gesture _waitForRelease lockout | OK |
| Corner-zone tap suppression (taps 1+2 of triple-tap) | OK - fixed 2026-02-18 |
| Overlay-transition LBUTTONUP injection | OK - fixed 2026-02-18 |
| Ctrl+F5 releases Ctrl modifier cleanly | OK - fixed 2026-02-18 |
| Static linking | OK |

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

## Next Steps

1. **Remove debug warning spam** — strip `warning()` log calls from event/graphics code
   (especially the `drawCursor` flood and touch state logging)

2. **Watchdog integration** — 60 s hardware watchdog will reset device. Either launch
   `watchdog_feeder` alongside ScummVM or add internal `/dev/watchdog` writes every 30 s.
   See [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#1-hardware-watchdog-timer).

3. **Game selector integration** — hook ScummVM launch into native game selector menu

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
- **OS:** Linux 4.14.52
- **Watchdog:** 60 s hardware reset if not fed

Full specs: [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#hardware-platform)

---

## Limitations

- No audio (NullMixerManager)
- Single-touch only; right-click = long-press 500 ms
- Software rendering only
- GMM overlay: true-black UI pixels show game bleed-through (negligible in practice)

## Supported Engines

**SCUMM v0-v6:** Monkey Island 1-2, DOTT, Sam & Max, Indiana Jones, Loom
**SCUMM v7-v8:** Full Throttle, The Dig, Curse of Monkey Island
**HE71+:** Putt-Putt, Freddi Fish, Pajama Sam, Spy Fox
**AGI/SCI:** King's Quest, Space Quest, Leisure Suit Larry series

## Memory Budget

- Binary: ~14 MB
- Game surface: ~1.2 MB
- Framebuffer: ~3 MB (double-buffered)
- Total: ~18 MB of 184 MB available

---

## Related Resources

- **SSH / device setup:** [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md)
- **Touch patterns:** [`native_games/PROJECT.md`](../native_games/PROJECT.md#touch-input)
- **Calibration tool source:** [`native_games/tests/unified_calibrate.c`](../native_games/tests/unified_calibrate.c)
- **Framebuffer reference:** [`native_games/common/framebuffer.c`](../native_games/common/framebuffer.c)
- **Watchdog:** [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#1-hardware-watchdog-timer)
