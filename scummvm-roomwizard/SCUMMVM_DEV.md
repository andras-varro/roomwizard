# ScummVM RoomWizard - Development Guide

## Status: ✅ WORKING - Virtual keyboard + gesture navigation implemented

**Binary:** 14.6 MB statically linked, deployed to device  
**Location:** `/opt/games/scummvm` on device  
**Last build:** 2026-02-18 (WSL Ubuntu-20.04, arm-linux-gnueabihf-g++ 9, `--enable-vkeybd`)  
**Version:** ScummVM 2.8.1pre with custom RoomWizard backend

## Quick Build

```bash
cd scummvm
./configure --host=arm-linux-gnueabihf --backend=roomwizard \
  --disable-all-engines --enable-engine=scumm --enable-engine=scumm-7-8 --enable-engine=he \
  --enable-engine=agi --enable-engine=sci --enable-engine=agos --enable-engine=sky --enable-engine=queen \
  --disable-alsa --disable-mt32emu --disable-flac --disable-mad --disable-vorbis \
  --enable-release --enable-optimizations --enable-vkeybd
make -j4 LDFLAGS='-static'
arm-linux-gnueabihf-strip scummvm

# Deploy to device (IP: 192.168.50.73)
scp scummvm root@192.168.50.73:/opt/games/
scp backends/vkeybd/packs/vkeybd_small.zip root@192.168.50.73:/opt/games/
ssh root@192.168.50.73 'chmod +x /opt/games/scummvm'
```


## Architecture

```
ScummVM Core → OSystem_RoomWizard
    ├── RoomWizardGraphicsManager → framebuffer.c → /dev/fb0
    ├── RoomWizardEventSource → touch_input.c → /dev/input/event0
    ├── NullMixerManager (no audio)
    └── Default managers (timer, events, saves, filesystem)
```

## Backend Files

Located in [`scummvm/backends/platform/roomwizard/`](../scummvm/backends/platform/roomwizard/):
- [`roomwizard.cpp/h`](../scummvm/backends/platform/roomwizard/roomwizard.cpp) - Main backend
- [`roomwizard-graphics.cpp/h`](../scummvm/backends/platform/roomwizard/roomwizard-graphics.cpp) - Graphics (800x480 framebuffer, bezel scaling)
- [`roomwizard-events.cpp/h`](../scummvm/backends/platform/roomwizard/roomwizard-events.cpp) - Touch input events & calibration
- [`module.mk`](../scummvm/backends/platform/roomwizard/module.mk) - Build config

## Screen Scaling & Calibration

Games are scaled to fill the bezel-safe area using nearest-neighbour scaling with aspect ratio preserved.  

**Run calibration on the device before first use** (requires `unified_calibrate` binary from `native_games/build/`):
```bash
ssh root@192.168.50.73 '/opt/games/unified_calibrate'
# Phase 1: tap the 4 corner crosshairs to calibrate touch accuracy
# Phase 2: tap +/- zones to adjust bezel margins until the safe-area rectangle
#           aligns with the visible screen inside the bezel
# Saves to /etc/touch_calibration.conf automatically
```
ScummVM reads this file at startup automatically — no manual config needed.

**Data flow:**
1. `unified_calibrate` saves `/etc/touch_calibration.conf` (touch offsets + bezel margins)
2. On startup:
   - `RoomWizardEventSource::initTouch()` calls `touch_load_calibration()` → touch hardware calibration enabled
   - `RoomWizardGraphicsManager::initFramebuffer()` calls `loadBezelMargins()` → parses bezel T/B/L/R from same file
3. `getScalingInfo()` computes scaled game area: fills `(800 - bezelL - bezelR) × (480 - bezelT - bezelB)`, aspect-ratio-clamped, centered
4. `blitGameSurfaceToFramebuffer()` nearest-neighbour scales game surface into the computed region
5. `transformCoordinates()` reverses the mapping: touch → game coordinates via `getScalingInfo()`
6. `initSize()` calls `setGameScreenSize()` on the event source to keep logical game dims in sync

**Config file format** (`/etc/touch_calibration.conf`):
```
# Touch calibration offsets (corners)
tl_x tl_y  tr_x tr_y  bl_x bl_y  br_x br_y
# Bezel margins (pixels from each edge)
bezel_top bezel_bottom bezel_left bezel_right
```

**Fallback:** if the file is absent, zero margins are used (old centred-letterbox behaviour).

## Gesture Navigation

Three-finger-free gesture detection built into `RoomWizardEventSource`. All gestures inject
standard ScummVM key events — no custom UI required.

| Gesture | Zone | Action |
|---|---|---|
| Triple-tap | Bottom-right corner (x>720, y>400) | **Global Main Menu** (Ctrl+F5) — save, load, options, quit |
| Triple-tap | Bottom-left corner (x<80, y>400) | **Virtual Keyboard** — text input for save names etc. |

**Triple-tap:** 3 taps within 1200 ms in the same corner zone (80px from edge).  
**On gesture fire:** the triggering touch is consumed (no click generated).  
**GMM:** works in any SCUMM/AGI/SCI game, gives access to save/load/quit without keyboard.  
**VKB:** loads `vkeybd_small.zip` from the ScummVM data path on first use (`/opt/games/`).

**Implementation:** `roomwizard-events.cpp` — `checkGestures()`, `pushKeyEvent()`, `pushEvent()`  
**VKB host:** `roomwizard.cpp` — `OSystem_RoomWizard::showVirtualKeyboard()` lazily loads the pack.

## Next Steps

1. **Test gestures on device** — verify triple-tap corners open GMM and VKB
2. **Remove debug log spam** — many `warning()` calls still in place from development
3. **Test with real game data** — add game files, verify full gameplay
4. **Watchdog integration** — run alongside `watchdog_feeder` or add internal feeding
5. **Integrate with game selector** — hook ScummVM into the native game selector menu

## What Works ✅

- ScummVM launches and runs
- Framebuffer rendering (800x480)
- Touch input device opens
- **Button clicks (LBUTTONDOWN/UP)** ✅
- **Touch drag (MOUSEMOVE during hold)** ✅
- Cursor movement (GUI + game modes)
- Dual-mode coordinate transformation (overlay vs game)
- **Full-screen bezel-aware scaling** ✅ verified on device (2026-02-18)
- **Calibration file loading** ✅ verified on device (2026-02-18)
- **Gesture navigation** ✅ built & deployed (2026-02-18) — test pending
  - Triple-tap bottom-right → Global Main Menu (save/load/quit)
  - Triple-tap bottom-left → Virtual Keyboard
- **Virtual keyboard** ✅ built with `--enable-vkeybd`, `vkeybd_small.zip` deployed
- Static linking (no dependencies)

## Touch Input Details

**Hardware:** Single-touch resistive (Panjit panjit_ts)
- 12-bit resolution (0-4095 raw → 0-800, 0-480 screen)
- Binary pressure (pressed/not pressed)
- No multi-touch

**Event Order (Critical):**
- Hardware sends: ABS_X/Y → BTN_TOUCH → SYN_REPORT
- **Must capture coordinates BEFORE press event**
- See [`native_games/PROJECT.md`](../native_games/PROJECT.md#key-technical-details) for proven implementation pattern

**Event Mapping:**
- Touch press → MOUSEMOVE + LBUTTONDOWN
- Touch drag → MOUSEMOVE (continuous)
- Touch release → LBUTTONUP
- Long press (500ms) → RBUTTONDOWN

**Coordinate Transform (game mode):**
```cpp
// Query graphics manager for scaled game region
getScalingInfo(scaledW, scaledH, fbOffsetX, fbOffsetY);
// Reverse-map touch → game coords
gameX = (touchX - fbOffsetX) * gameWidth  / scaledW;
gameY = (touchY - fbOffsetY) * gameHeight / scaledH;
```

## Graphics Details

**Framebuffer:** 800x480 @ 32-bit ARGB  
**Game Resolutions:** 320x200, 320x240, 640x480 (scaled to fill safe area)  
**Scaling:** Nearest-neighbour, aspect-ratio-preserving, bezel-margin-aware  
**Pixel Formats:** CLUT8 (palette), RGB565, ARGB8888 → ARGB8888  
**Cursor:** Software cursor composited over game graphics

## Git Management

ScummVM is external repo. Use [`manage-scummvm-changes.sh`](manage-scummvm-changes.sh):
```bash
./manage-scummvm-changes.sh status   # Check config
./manage-scummvm-changes.sh backup   # Create backup
./manage-scummvm-changes.sh update   # Update from upstream
```

Local changes protected via:
- `.git/info/exclude` - Ignores `backends/platform/roomwizard/`
- `git update-index --skip-worktree configure` - Ignores configure changes

## Hardware Specs

For comprehensive hardware documentation, see [`SYSTEM_ANALYSIS.md#hardware-platform`](../SYSTEM_ANALYSIS.md#hardware-platform).

**Quick Reference:**
- **CPU:** ARMv7 with NEON SIMD
- **RAM:** 184 MB available
- **Display:** 800x480 framebuffer
- **Input:** Single-touch resistive
- **OS:** Linux 4.14.52

## Critical Requirements

**Watchdog Timer:** 60-second hardware watchdog. Must run alongside [`watchdog_feeder`](../native_games/watchdog_feeder/watchdog_feeder.c) or implement internal feeding every 30s.  
See [`SYSTEM_ANALYSIS.md#hardware-watchdog-timer`](../SYSTEM_ANALYSIS.md#1-hardware-watchdog-timer).

## Limitations

- No audio (NullMixerManager)
- No keyboard (touch only)
- Single-touch only
- Right-click = long-press (500ms)
- Software rendering only

## Supported Games

**SCUMM v0-v6:** Monkey Island 1-2, Day of the Tentacle, Sam & Max, Indiana Jones, Loom  
**SCUMM v7-v8:** Full Throttle, The Dig, Curse of Monkey Island  
**HE71+:** Putt-Putt, Freddi Fish, Pajama Sam, Spy Fox

## Memory Usage

- Binary: 13 MB
- Game surface: ~1.2 MB
- Framebuffer: ~3 MB (double-buffered)
- Total: ~17 MB (184 MB available)

## Related Resources

- **SSH Access & System Setup:** See [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md) for SSH key configuration and device IP (192.168.50.73)
- **Touch Input Patterns:** See [`native_games/PROJECT.md`](../native_games/PROJECT.md#touch-input) for proven touch handling
- **Calibration Tool:** [`native_games/tests/unified_calibrate.c`](../native_games/tests/unified_calibrate.c) — saves `/etc/touch_calibration.conf`
- **Framebuffer Library:** Reference implementation at [`native_games/common/framebuffer.c`](../native_games/common/framebuffer.c)
- **Watchdog Handling:** Must feed every 30s — see [`SYSTEM_ANALYSIS.md#hardware-watchdog-timer`](../SYSTEM_ANALYSIS.md#1-hardware-watchdog-timer)


**Binary:** 13 MB statically linked, runs on device  
**Location:** `/opt/games/scummvm` on device  
**Version:** ScummVM 2.8.1pre with custom RoomWizard backend

## Quick Build

```bash
cd scummvm
./configure --host=arm-linux-gnueabihf --backend=roomwizard \
  --disable-all-engines --enable-engine=scumm --enable-engine=scumm-7-8 --enable-engine=he \
  --disable-alsa --disable-mt32emu --disable-flac --disable-mad --disable-vorbis \
  --enable-release --enable-optimizations
make -j4 LDFLAGS='-static'
arm-linux-gnueabihf-strip scummvm

# Deploy to device (IP: 192.168.50.73)
# For SSH setup, see ../SYSTEM_SETUP.md
scp scummvm root@192.168.50.73:/opt/games/
ssh root@192.168.50.73 'chmod +x /opt/games/scummvm'
```


## Architecture

```
ScummVM Core → OSystem_RoomWizard
    ├── RoomWizardGraphicsManager → framebuffer.c → /dev/fb0
    ├── RoomWizardEventSource → touch_input.c → /dev/input/event0
    ├── NullMixerManager (no audio)
    └── Default managers (timer, events, saves, filesystem)
```

## Backend Files

Located in [`scummvm/backends/platform/roomwizard/`](../scummvm/backends/platform/roomwizard/):
- [`roomwizard.cpp/h`](../scummvm/backends/platform/roomwizard/roomwizard.cpp) - Main backend
- [`roomwizard-graphics.cpp/h`](../scummvm/backends/platform/roomwizard/roomwizard-graphics.cpp) - Graphics (800x480 framebuffer)
- [`roomwizard-events.cpp/h`](../scummvm/backends/platform/roomwizard/roomwizard-events.cpp) - Touch input events
- [`module.mk`](../scummvm/backends/platform/roomwizard/module.mk) - Build config

## Current Issues

### 0. Screen Area ⚠️
**Problem:** We need to add a logic that upscales the game to the screen size, considering the bezel, that obscures ~10px mainly on the top and bottom. This is critical for games that rely on precise cursor positioning (e.g. Monkey Island 1-2) where the edges of the screen are often used for interactions. We alredy implemented a logic in native_games/tests/unified_calibrate.c to save the coursor calibration and the bezel offset in the /etc/touch_calibration.conf. See how the touch calibration loading the file. This is important to scale the clicks too.
**Fix Needed:** Implement functionality

## What Works ✅

- ScummVM launches and runs
- Framebuffer rendering (800x480)
- Touch input device opens
- **Button clicks (LBUTTONDOWN/UP)** ✅
- **Touch drag (MOUSEMOVE during hold)** ✅
- Cursor movement (GUI + game modes)
- Dual-mode coordinate transformation
- Static linking (no dependencies)

## Touch Input Details

**Hardware:** Single-touch resistive (Panjit panjit_ts)
- 12-bit resolution (0-4095 raw → 0-800, 0-480 screen)
- Binary pressure (pressed/not pressed)
- No multi-touch

**Event Order (Critical):**
- Hardware sends: ABS_X/Y → BTN_TOUCH → SYN_REPORT
- **Must capture coordinates BEFORE press event**
- See [`native_games/PROJECT.md`](../native_games/PROJECT.md#key-technical-details) for proven implementation pattern

**Event Mapping:**
- Touch press → MOUSEMOVE + LBUTTONDOWN
- Touch drag → MOUSEMOVE (continuous)
- Touch release → LBUTTONUP
- Long press (500ms) → RBUTTONDOWN

**Coordinate Transform:**
```cpp
// GUI mode (overlay visible): 1:1 mapping
gameX = touchX; gameY = touchY;

// Game mode: apply centering offset
gameX = touchX - (800 - gameWidth) / 2;
gameY = touchY - (480 - gameHeight) / 2;
```

## Graphics Details

**Framebuffer:** 800x480 @ 32-bit ARGB  
**Game Resolutions:** 320x200, 320x240, 640x480 (centered with letterboxing)  
**Pixel Formats:** CLUT8 (palette), RGB565, ARGB8888 → ARGB8888  
**Cursor:** Software cursor composited over game graphics

## Git Management

ScummVM is external repo. Use [`manage-scummvm-changes.sh`](manage-scummvm-changes.sh):
```bash
./manage-scummvm-changes.sh status   # Check config
./manage-scummvm-changes.sh backup   # Create backup
./manage-scummvm-changes.sh update   # Update from upstream
```

Local changes protected via:
- `.git/info/exclude` - Ignores `backends/platform/roomwizard/`
- `git update-index --skip-worktree configure` - Ignores configure changes

## Next Steps

1. **Remove debug logs** - Clean up warning() statements
2. **Add calibration** - Corner accuracy improvement (~14-27px error)
3. **Test with games** - Add game data, test gameplay
4. **Integrate** - Add to game selector

## Fixes Applied (2026-02-15)

### Button Click Fix ✅ VERIFIED
**Root Cause:** `warpMouse()` calls `purgeMouseEvents()` which cleared the event queue
**Solution:** Removed all `g_system->warpMouse()` calls from [`roomwizard-events.cpp`](../scummvm/backends/platform/roomwizard/roomwizard-events.cpp)
**Result:** All touch events working - LBUTTONDOWN/UP, drag, cursor movement

### Keymapper Bypass ✅
Added `allowMapping() { return false; }` to [`roomwizard-events.h:44`](../scummvm/backends/platform/roomwizard/roomwizard-events.h:44) to bypass keymapper processing

### Test Results
Verified on device - user confirmed clicks working correctly. Cursor follows touch properly.

## Hardware Specs

For comprehensive hardware documentation, see [`SYSTEM_ANALYSIS.md#hardware-platform`](../SYSTEM_ANALYSIS.md#hardware-platform).

**Quick Reference:**
- **CPU:** ARMv7 with NEON SIMD
- **RAM:** 184 MB available
- **Display:** 800x480 framebuffer
- **Input:** Single-touch resistive
- **OS:** Linux 4.14.52

## Critical Requirements

**Watchdog Timer:** The device has a 60-second hardware watchdog that will reset the system if not fed. ScummVM must either:
- Run alongside the [`watchdog_feeder`](../native_games/watchdog_feeder/watchdog_feeder.c) from native_games
- Implement internal watchdog feeding (every 30s)
- See [`SYSTEM_ANALYSIS.md#hardware-watchdog-timer`](../SYSTEM_ANALYSIS.md#1-hardware-watchdog-timer) for details

**Consequences of not feeding watchdog:**
- System automatically resets after 60 seconds
- Game progress lost
- May increment boot tracker (leading to failure mode)

## Limitations

- No audio (NullMixerManager)
- No keyboard (touch only)
- Single-touch only
- Right-click = long-press (500ms)
- Software rendering only

## Supported Games

**SCUMM v0-v6:** Monkey Island 1-2, Day of the Tentacle, Sam & Max, Indiana Jones, Loom  
**SCUMM v7-v8:** Full Throttle, The Dig, Curse of Monkey Island  
**HE71+:** Putt-Putt, Freddi Fish, Pajama Sam, Spy Fox

## Memory Usage

- Binary: 13 MB
- Game surface: ~1.2 MB
- Framebuffer: ~3 MB (double-buffered)
- Total: ~17 MB (184 MB available)

## Related Resources

- **SSH Access & System Setup:** See [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md) for SSH key configuration and device IP (192.168.50.73)
- **Touch Input Patterns:** See [`native_games/PROJECT.md`](../native_games/PROJECT.md#touch-input) for proven touch handling (12-bit → 800x480 scaling, event order)
- **Framebuffer Library:** Reference implementation at [`native_games/common/framebuffer.c`](../native_games/common/framebuffer.c) (double-buffered rendering)
- **Watchdog Handling:** Must feed every 30s to prevent 60s reset - see [`SYSTEM_ANALYSIS.md#hardware-watchdog-timer`](../SYSTEM_ANALYSIS.md#1-hardware-watchdog-timer)
- **Hardware Details:** See [`SYSTEM_ANALYSIS.md#hardware-platform`](../SYSTEM_ANALYSIS.md#hardware-platform) for comprehensive specs
