# ScummVM on RoomWizard - Project Tracker

## Project Goal
Port ScummVM to the RoomWizard device to enable classic point-and-click adventure games with native framebuffer rendering and touch input.

## Hardware Specifications

### RoomWizard Device
- **CPU:** ARMv7 Processor rev 7 (v7l) with NEON SIMD
- **RAM:** 234 MB total, ~184 MB available
- **Storage:** 508 MB free space
- **Display:** 800x480 pixels, RGB565 framebuffer (`/dev/fb0`)
- **Input:** Resistive touchscreen, single-touch (`/dev/input/event0`)
- **OS:** Linux 4.14.52 (embedded)

### Touch Input Capabilities
- **Type:** Resistive (Panjit panjit_ts)
- **Resolution:** 12-bit (0-4095 raw coordinates)
- **Pressure:** Binary only (255=pressed, 0=released)
- **Multi-touch:** ❌ Not supported
- **Perfect for:** Point-and-click adventure games

## Why ScummVM is Ideal

### Perfect Match
1. **Point-and-Click Interface:** Single-touch is all you need
2. **Low Resource Requirements:** Most SCUMM games need 64-128 MB RAM
3. **Display Resolution:** 800x480 is perfect for upscaling 320x200 or 640x480 games
4. **No 3D Required:** Pure 2D rendering, perfect for framebuffer
5. **Proven ARM Support:** ScummVM has mature ARM ports

### Compatible Games (Examples)
- The Secret of Monkey Island
- Monkey Island 2: LeChuck's Revenge
- Day of the Tentacle
- Sam & Max Hit the Road
- Indiana Jones series
- Full Throttle
- Grim Fandango (if RAM permits)
- And hundreds more...

## Implementation Plan

### Phase 1: Research & Setup ✅ COMPLETED
- [x] Verify hardware compatibility
- [x] Analyze touch input system
- [x] Create project tracking document
- [x] Research ScummVM build system
- [x] Identify required dependencies
- [x] Check for existing ARM/embedded ports

### Phase 2: Cross-Compilation Environment ✅ COMPLETED
- [x] Install ScummVM source code
- [x] Configure for ARM cross-compilation
- [x] Set up minimal build (SCUMM engine only)
- [x] Configure for null backend (framebuffer will be custom)
- [x] Disable X11/SDL dependencies
- [x] Install build tools (make) in WSL/Linux environment
- [x] Complete initial build test

### Phase 3: Build ScummVM ✅ COMPLETED
- [x] Compile ScummVM for ARM 32-bit in Ubuntu 20.04 WSL
- [x] Strip unnecessary engines (SCUMM only - v0-v8 + HE)
- [x] Optimize for size (13 MB stripped binary)
- [x] Verify library dependencies (only standard C/C++ libs)
- [x] Resolve GLIBC compatibility (Ubuntu 20.04 = GLIBC 2.31)
- [x] Test binary on device - Segmentation fault in null backend
- [x] Create custom RoomWizard backend
- [x] Integrate framebuffer and touch input
- [x] Rebuild and test on device - **SUCCESS!**

### Phase 4: Input Integration 🔄 IN PROGRESS
- [x] Create touch-to-mouse input mapper
- [x] Implement cursor rendering
- [x] Cursor movement working correctly (overlay + game modes)
- [x] Coordinate transformation (dual-mode system)
- [ ] Fix button click events (LBUTTONDOWN/UP not working)
- [ ] Fix touch drag events (MOUSEMOVE during drag)
- [ ] Add touch calibration for corner accuracy
- [ ] Add on-screen keyboard for text input (deferred - not needed for most games)

### Phase 5: Integration
- [ ] Add ScummVM to game selector
- [ ] Create game data directory structure
- [ ] Add sample game for testing
- [ ] Create launcher script
- [ ] Test full workflow

### Phase 6: Polish & Documentation
- [ ] Optimize performance
- [ ] Add configuration UI
- [ ] Create user guide
- [ ] Document game installation process
- [ ] Test multiple games

## Technical Requirements

### Build Dependencies
- ARM cross-compiler (already have: `arm-linux-gnueabihf-gcc`)
- ScummVM source code (latest stable or 2.x branch)
- Build tools: make, pkg-config, etc.

### Runtime Dependencies (on device)
- Linux framebuffer support ✅ (already have)
- Touch input device ✅ (already have)
- Audio support ❓ (need to verify ALSA/OSS)
- File system access ✅ (already have)

### ScummVM Configuration
```
./configure \
  --host=arm-linux-gnueabihf \
  --backend=linuxfb \
  --disable-sdl \
  --disable-alsa \
  --disable-mt32emu \
  --disable-flac \
  --disable-mad \
  --disable-vorbis \
  --enable-release \
  --enable-optimizations \
  --with-staticlib-prefix=/path/to/libs
```

## Current Status

### Completed ✅
- Hardware analysis
- Touch input verification
- Feasibility assessment
- Project planning
- ScummVM build system research
- Dependency identification
- ARM/embedded ports analysis
- Cross-compilation environment setup
- Initial ARM build (13 MB binary)
- **Custom RoomWizard backend implementation**
- **Framebuffer integration (800x480)**
- **Touch input integration (single-touch)**
- **Device testing - WORKING!**
- **Cursor movement working correctly**
- **Dual-mode coordinate transformation (GUI + game modes)**

### In Progress 🔄
- Touch input event generation (button clicks and drag)
- Touch calibration for corner accuracy

### Blocked ⏸️
- None

### Next Steps
1. ✅ ~~Test binary on RoomWizard device~~ - **WORKING!**
2. ✅ ~~Verify it runs with null backend~~ - **Custom backend created instead**
3. ✅ ~~Create custom framebuffer backend~~ - **COMPLETED!**
4. ✅ ~~Integrate touch input~~ - **COMPLETED!**
5. ✅ ~~Fix cursor movement~~ - **WORKING!**
6. 🔄 **Fix button click events (LBUTTONDOWN/UP generation)**
7. 🔄 **Fix touch drag events (continuous MOUSEMOVE during touch)**
8. 🔄 **Add touch calibration for corner accuracy (~14-27px error)**
9. Remove debug logging for production build
10. Add game data files to device
11. Test actual game playback
12. Integrate into game selector
13. Performance optimization if needed

## Resources

### ScummVM Links
- **Official Site:** https://www.scummvm.org/
- **GitHub:** https://github.com/scummvm/scummvm
- **Wiki:** https://wiki.scummvm.org/
- **Build Guide:** https://wiki.scummvm.org/index.php/Compiling_ScummVM

### Embedded/ARM Ports
- **Raspberry Pi:** Similar ARM platform, good reference
- **OpenPandora:** ARM handheld, framebuffer backend
- **GCW Zero:** ARM handheld, similar constraints

### RoomWizard Resources
- [`PROJECT_STATUS.md`](PROJECT_STATUS.md) - Main project status
- [`native_games/common/touch_input.c`](native_games/common/touch_input.c) - Touch input library
- [`native_games/common/framebuffer.c`](native_games/common/framebuffer.c) - Framebuffer library

## Notes

### Memory Considerations
- ScummVM binary: ~5-15 MB (depends on engines included)
- Game data: 5-50 MB per game (varies widely)
- Runtime memory: 64-128 MB for most games
- **Total available:** 184 MB RAM, 508 MB storage
- **Recommendation:** Include only essential engines (SCUMM, AGI, SCI)

### Display Scaling
- Original games: 320x200, 320x240, 640x480
- RoomWizard: 800x480
- ScummVM has excellent scaling filters
- Aspect ratio correction available

### Touch Input Strategy
1. **Cursor Mode:** Touch moves cursor, tap = click
2. **Direct Mode:** Touch position = click position (no cursor)
3. **Hybrid:** Tap for direct click, hold+drag for cursor

### Audio Support
- Need to verify if device has audio hardware
- If no audio: disable sound engines to save space
- If audio available: configure ALSA/OSS backend

## Risk Assessment

### Low Risk ✅
- Hardware compatibility (ARMv7 is well-supported)
- Touch input (single-touch is sufficient)
- Display (framebuffer rendering is straightforward)

### Medium Risk ⚠️
- Build complexity (many dependencies)
- Binary size (need to fit in limited storage)
- Performance (older ARM, need optimization)

### High Risk ❌
- Audio support (unknown if hardware exists)
- Game compatibility (some games may be too demanding)
- User experience (on-screen keyboard for text input)

## Success Criteria

### Minimum Viable Product (MVP)
- [x] ScummVM binary runs on device - **WORKING!**
- [ ] Can launch at least one SCUMM game
- [ ] Touch input works for basic navigation
- [ ] Integrated into game selector
- [ ] Playable frame rate (15+ FPS)

### Full Success
- [ ] Multiple game engines supported
- [ ] Smooth performance (30+ FPS)
- [ ] Audio working (if hardware supports)
- [ ] Save/load game functionality
- [ ] Configuration UI
- [ ] Multiple games installed and playable

## Backend Implementation Success ✅

### What Works
- ✅ **ScummVM launches without crashing**
- ✅ **Framebuffer initialized (800x480)**
- ✅ **Touch input initialized (/dev/input/event0)**
- ✅ **Graphics manager operational**
- ✅ **Event loop running**
- ✅ **Static linking successful (13 MB binary)**
- ✅ **GLIBC compatibility confirmed**
- ✅ **Cursor movement working correctly**
- ✅ **Dual-mode coordinate transformation:**
  - GUI mode: Full 800x480 touchable area (1:1 mapping)
  - Game mode: Transformed coordinates for centered display
- ✅ **Cursor rendering with overlay-aware offset calculation**

### Current Issues 🔧
- ❌ **Button clicks not working** - LBUTTONDOWN/UP events not being generated
  - Root cause: Timing-based event generation is unreliable
  - Current logic sends MOUSEMOVE on touch start, then tries to send LBUTTONDOWN 50ms later
  - `pollEvent()` may not be called frequently enough for timing to work
  - **Solution needed:** Refactor to use event queue with proper state machine
  
- ❌ **Dragging not working** - MOUSEMOVE events during drag not being sent
  - Only sends MOUSEMOVE when position changes
  - Needs to track "still touching" state better
  - **Solution needed:** Generate MOUSEMOVE events continuously while touch is held
  
- ⚠️ **Touch calibration needed** - Corner accuracy issues
  - Resistive touchscreen has ~14-27px error in corners (normal for this hardware)
  - **Solution needed:** Add calibration offset to improve accuracy

### Test Output
```
RoomWizard: main() started
RoomWizard: backend created
WARNING: RoomWizard touch input initialized!
Touch input initialized: /dev/input/event0
Touch screen size set to: 800x480
RoomWizard backend initialized
initSize: 320x200
WARNING: RoomWizard framebuffer initialized: 800x480!
ScummVM 2.8.1pre (Feb 15 2026 09:24:21)
Features compiled in: SEQ TiMidity ENet

Touch events: 5, state: pressed=1 held=1 x=320 y=211
Touch STARTED at (320, 211)
Sending MOUSEMOVE to (320, 211)
warpMouse called: cursor moved to (320, 211)
drawCursor called: visible=1, hasPixels=1, hasFB=1, cursorPos=(320,211), overlayVisible=1
```

### Known Limitations
- No audio support (NullMixerManager - games will be silent)
- No keyboard input (touch only)
- Single-touch only (no multi-touch gestures)
- Right-click requires long-press (500ms)
- Missing data files (translations.dat, gui-icons.dat) - not critical

## Timeline Estimate

- **Phase 1 (Research):** 1-2 days
- **Phase 2 (Environment):** 1 day
- **Phase 3 (Build):** 2-3 days
- **Phase 4 (Input):** 2-3 days
- **Phase 5 (Integration):** 1-2 days
- **Phase 6 (Polish):** 2-3 days

**Total:** 9-14 days (assuming part-time work)

## Research Findings

### Build System Analysis
- **Configure Script:** ScummVM uses a custom shell-based configure script (not autotools)
- **Backend Support:** Has SDL, framebuffer, and embedded platform backends
- **ARM Support:** Excellent - supports Raspberry Pi, OpenPandora, GPH devices, PSP, DS, 3DS, Switch
- **Cross-Compilation:** Well-supported with `--host=` flag
- **Minimal Build:** Can disable engines individually, supports static linking

### Key Dependencies
**Required:**
- C++ compiler with C++11 support (we have: `arm-linux-gnueabihf-gcc`)
- zlib (for compression)

**Optional but Recommended:**
- libmad or tremor (for audio - MP3/OGG)
- libpng (for PNG support)
- freetype2 (for fonts)

**Not Needed:**
- SDL (we'll use framebuffer backend)
- X11/OpenGL (embedded device)
- ALSA (if no audio hardware)

### Existing ARM/Embedded Ports
**Similar Platforms Found:**
1. **OpenPandora** - ARM Cortex-A8, framebuffer, touchscreen
2. **GPH Devices** (GP2X, Caanoo) - ARM, framebuffer, minimal RAM
3. **Nintendo DS** - ARM7/ARM9, very limited resources
4. **PSP** - MIPS but similar constraints
5. **Raspberry Pi** - ARM, framebuffer support

**Best Reference:** OpenPandora port - most similar to RoomWizard

### Build Configuration Strategy
**Final Configuration (All SCUMM Games):**
```bash
./configure \
  --host=arm-linux-gnueabihf \
  --backend=null \
  --disable-alsa \
  --disable-mt32emu \
  --disable-flac \
  --disable-mad \
  --disable-vorbis \
  --disable-16bit \
  --enable-release \
  --enable-optimizations \
  --disable-all-engines \
  --enable-engine=scumm \
  --enable-engine=scumm-7-8 \
  --enable-engine=he
```

**Configuration Results:**
- ✅ ARM cross-compiler detected (GCC 13)
- ✅ C++11 support confirmed
- ✅ **SCUMM engine enabled (ALL games - v0-v8 + HE71+)**
  - v0-v6: Classic games (Monkey Island 1-2, Day of the Tentacle, Sam & Max)
  - v7-v8: Later games (Full Throttle, The Dig, Curse of Monkey Island)
  - HE71+: Humongous Entertainment games (Putt-Putt, Freddi Fish, Pajama Sam)
- ✅ Null backend configured (will need custom framebuffer integration)
- ✅ All 80+ unnecessary engines disabled
- ✅ Built successfully in WSL environment

**Build Results (Ubuntu 20.04 WSL):**
- ✅ Binary size: 13 MB (stripped)
- ✅ Architecture: ARM 32-bit (EABI5, VFPv3-D16)
- ✅ Dependencies: Only standard C/C++ libraries (libstdc++, libm, libgcc_s, libc)
- ✅ GLIBC compatibility: Requires 2.27-2.29, device has 2.31 ✅
- ✅ No SDL, X11, or audio dependencies
- ✅ Toolchain verified working (test program runs successfully)
- ❌ ScummVM binary segfaults on device

**Segmentation Fault Analysis:**
- Test C++ program works fine on device
- Toolchain and GLIBC are compatible
- Issue is with ScummVM's null backend initialization
- Null backend may not be fully functional or has bugs

**Next Steps:**
1. Debug null backend crash (add logging/error handling)
2. OR: Create custom framebuffer backend based on existing backends (linuxfb, openpandora)
3. OR: Try using an existing embedded backend (openpandora, gph)

**Recommendation:** Use OpenPandora backend as reference - it's designed for similar ARM handheld devices with framebuffer

## Questions Answered

- [x] Does RoomWizard have audio hardware? **Need to verify - assume no for MVP**
- [x] What is the CPU clock speed? **ARMv7 rev 7 - likely 800MHz-1GHz range**
- [x] Are there any existing ScummVM ARM framebuffer builds? **Yes - OpenPandora, GPH devices**
- [x] What is the maximum binary size? **Target < 10 MB, have 508 MB storage**
- [x] Which game engines are most important? **SCUMM (classic LucasArts games) for MVP**

## Custom Backend Development Plan

Creating a RoomWizard backend requires:

1. **Copy null backend as template**
   ```bash
   cp -r scummvm/backends/platform/null scummvm/backends/platform/roomwizard
   ```

2. **Create C++ wrappers for RoomWizard libraries**
   - Wrap `native_games/common/framebuffer.c` for graphics
   - Wrap `native_games/common/touch_input.c` for input
   - Create `backends/platform/roomwizard/roomwizard-graphics.h/cpp`
   - Create `backends/platform/roomwizard/roomwizard-events.h/cpp`

3. **Modify configure script**
   - Add roomwizard backend support to `configure`
   - Link against RoomWizard libraries

4. **Build and test**
   - Rebuild with `--backend=roomwizard`
   - Test on device

**Complexity:** High - requires C++ knowledge and ScummVM backend API understanding
**Time Estimate:** 4-8 hours for experienced developer
**Alternative:** Debug null backend crash (may be quicker)

---

**Last Updated:** 2026-02-15
**Status:** 🔄 **Phase 4 IN PROGRESS - Touch Input Refinement**

## Implementation Summary

The custom RoomWizard backend for ScummVM has been successfully implemented and tested on the device. The backend integrates with existing RoomWizard libraries for framebuffer rendering and touch input.

### Files Created
1. [`scummvm/backends/platform/roomwizard/roomwizard.h`](scummvm/backends/platform/roomwizard/roomwizard.h) - Main backend header
2. [`scummvm/backends/platform/roomwizard/roomwizard.cpp`](scummvm/backends/platform/roomwizard/roomwizard.cpp) - Main backend implementation
3. [`scummvm/backends/platform/roomwizard/roomwizard-graphics.h`](scummvm/backends/platform/roomwizard/roomwizard-graphics.h) - Graphics manager header
4. [`scummvm/backends/platform/roomwizard/roomwizard-graphics.cpp`](scummvm/backends/platform/roomwizard/roomwizard-graphics.cpp) - Graphics manager implementation
5. [`scummvm/backends/platform/roomwizard/roomwizard-events.h`](scummvm/backends/platform/roomwizard/roomwizard-events.h) - Event source header
6. [`scummvm/backends/platform/roomwizard/roomwizard-events.cpp`](scummvm/backends/platform/roomwizard/roomwizard-events.cpp) - Event source implementation
7. [`scummvm/backends/platform/roomwizard/module.mk`](scummvm/backends/platform/roomwizard/module.mk) - Build configuration
8. [`scummvm/backends/platform/roomwizard/README.md`](scummvm/backends/platform/roomwizard/README.md) - Backend documentation

### Build Configuration
- Modified [`scummvm/configure`](scummvm/configure) to add roomwizard backend support
- Static linking enabled to avoid dynamic library issues
- Binary size: 13 MB (stripped)
- Engines: SCUMM v0-v8 + HE71+

### Current Binary Location
- Device: `/tmp/scummvm_fixed` (working binary with cursor movement)
- Size: 13 MB (statically linked)

### Touch Input Implementation Details

#### Coordinate Transformation System
The backend implements a dual-mode coordinate transformation system:

**GUI Mode (Overlay Visible):**
- Uses full 800x480 touchable area
- 1:1 mapping between touch coordinates and screen coordinates
- No offset applied to cursor position
- Detected via `isOverlayVisible()` check

**Game Mode (Overlay Hidden):**
- Games typically use 320x200 or 640x480 resolution
- Display is centered on 800x480 screen with letterboxing
- Touch coordinates transformed to account for centering offset
- Cursor position includes offset for proper rendering

#### Event Generation State Machine
Current implementation uses timing-based approach (needs refactoring):
1. Touch start → Send MOUSEMOVE + warpMouse
2. Wait 50ms → Send LBUTTONDOWN (unreliable)
3. Touch end → Send LBUTTONUP

**Problem:** `pollEvent()` may not be called frequently enough for timing to work reliably.

**Proposed Solution:** Use event queue with proper state tracking:
1. Touch start → Queue MOUSEMOVE and LBUTTONDOWN immediately
2. Touch held → Queue MOUSEMOVE on position change
3. Touch end → Queue LBUTTONUP

### Remaining Work

#### High Priority
1. **Fix Button Click Events**
   - Refactor event generation to use queue instead of timing
   - Ensure LBUTTONDOWN/UP events are generated reliably
   - Test with GUI buttons (Add Game, About, Quit)

2. **Fix Touch Drag Events**
   - Generate continuous MOUSEMOVE events while touch is held
   - Track position changes during drag
   - Test with GUI scrolling and game interactions

3. **Add Touch Calibration**
   - Implement calibration offset for corner accuracy
   - Resistive touchscreen has ~14-27px error in corners
   - Could use simple linear correction or full calibration matrix

#### Medium Priority
4. **Remove Debug Logging**
   - Clean up printf statements from production build
   - Keep only critical error messages
   - Reduce binary size if possible

5. **Test with Actual Games**
   - Add game data files to device
   - Test gameplay with fixed touch input
   - Verify performance and responsiveness

#### Low Priority
6. **Integrate into Game Selector**
   - Add ScummVM to native game selector menu
   - Create launcher script
   - Add game management UI

### Technical Notes

**Event Sequence for Click:**
```
MOUSEMOVE (x, y) → warpMouse(x, y) → LBUTTONDOWN → LBUTTONUP
```

**Event Sequence for Drag:**
```
MOUSEMOVE (x1, y1) → warpMouse(x1, y1) → LBUTTONDOWN
→ MOUSEMOVE (x2, y2) → warpMouse(x2, y2)
→ MOUSEMOVE (x3, y3) → warpMouse(x3, y3)
→ ... (continuous while touching)
→ LBUTTONUP
```

**Coordinate Transformation Logic:**
```cpp
// In roomwizard-events.cpp
if (isOverlayVisible()) {
    // GUI mode: 1:1 mapping
    gameX = touchX;
    gameY = touchY;
} else {
    // Game mode: apply centering offset
    gameX = touchX - offsetX;
    gameY = touchY - offsetY;
}
```

**Cursor Drawing Logic:**
```cpp
// In roomwizard-graphics.cpp
if (!_overlayVisible) {
    // Game mode: apply offset
    offsetX = (800 - _screenWidth) / 2;
    offsetY = (480 - _screenHeight) / 2;
} else {
    // GUI mode: no offset
    offsetX = 0;
    offsetY = 0;
}
```

### Next Phase
Phase 5: Integration
- Complete touch input fixes
- Add game data files
- Test actual gameplay
- Integrate into game selector
- Performance optimization
