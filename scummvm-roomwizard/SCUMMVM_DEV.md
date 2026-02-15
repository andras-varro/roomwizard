# ScummVM RoomWizard - Development Guide

## Status: ✅ WORKING - Touch Input Needs Fixes

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

**Prerequisites:** Ensure SSH access is configured. See [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md) for SSH key setup and device IP configuration.

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

### 0. Screen Safe Area (CRITICAL) ⚠️
**Problem:** Bezel obscures ~30-40px on all edges (visible: ~720x420 from 800x480)
**Impact:** UI elements near edges may be hidden/unreachable
**Fix Needed:** Add safe area margins to GUI rendering
**Reference:** See [`SYSTEM_ANALYSIS.md#display`](../SYSTEM_ANALYSIS.md#display) for specifications and [`native_games/common/common.h`](../native_games/common/common.h) for `LAYOUT_*` macro implementation

### 1. Button Clicks Not Working ❌
**Problem:** LBUTTONDOWN/UP events not generated  
**Root Cause:** Timing-based event generation unreliable  
**Fix Needed:** Refactor to event queue with state machine

### 2. Touch Drag Not Working ❌
**Problem:** MOUSEMOVE during drag not sent  
**Fix Needed:** Generate continuous MOUSEMOVE while touching

### 3. Touch Calibration Needed ⚠️
**Problem:** ~14-27px error in corners (resistive touchscreen)  
**Fix Needed:** Add calibration offset

## What Works ✅

- ScummVM launches and runs
- Framebuffer rendering (800x480)
- Touch input device opens
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

1. **Fix button clicks** - Refactor event generation
2. **Fix touch drag** - Continuous MOUSEMOVE during touch
3. **Add calibration** - Corner accuracy improvement
4. **Remove debug logs** - Clean up printf statements
5. **Test with games** - Add game data, test gameplay
6. **Integrate** - Add to game selector

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
