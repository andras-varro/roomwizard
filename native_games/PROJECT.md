# Native Games Project

## Current Status

**System:** Fully functional ✅
- 3 games: Snake, Tetris, Pong
- Game selector with scrolling
- Watchdog feeder
- Hardware test suite
- Touch input: accurate (3px center, 14-27px corners)
- Double buffering: flicker-free rendering
- LED effects: integrated

**Device:** RoomWizard - See [`SYSTEM_ANALYSIS.md#hardware-platform`](../SYSTEM_ANALYSIS.md#hardware-platform) for full specs
**IP:** 192.168.50.73 - See [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md) for SSH key auth setup

## Quick Commands

```bash
# Build
./compile_for_roomwizard.sh

# Deploy
./deploy.sh 192.168.50.73 test        # Test mode
./deploy.sh 192.168.50.73 permanent   # Permanent mode
```

## Architecture

**Libraries:**
- [`common/framebuffer.c`](common/framebuffer.c) - Double-buffered rendering
- [`common/touch_input.c`](common/touch_input.c) - Touch events (12-bit → 800x480)
- [`common/hardware.c`](common/hardware.c) - LED/backlight control
- [`common/common.c`](common/common.c) - Unified UI (buttons, screens, safe area)
- [`common/ui_layout.c`](common/ui_layout.c) - Layouts + ScrollableList widget

**Programs:**
- `snake/`, `tetris/`, `pong/` - Games
- `game_selector/` - Menu with scrolling
- `watchdog_feeder/` - Prevents 60s reset
- `hardware_test/` - Diagnostics (GUI + CLI)
- `tests/` - Touch debug/inject, framebuffer capture

## Key Technical Details

### Touch Input

See [`SYSTEM_ANALYSIS.md#input`](../SYSTEM_ANALYSIS.md#input) for hardware specs and coordinate scaling.

**Implementation Notes:**
- Event order: ABS_X/Y → BTN_TOUCH → SYN_REPORT
- Capture coordinates BEFORE press event
- Reference: [`common/touch_input.c`](common/touch_input.c)

### Screen Safe Area

- Framebuffer: 800x480
- Visible: ~720x420 (bezel obscures ~30-40px on edges)
- Use `LAYOUT_*` macros from common.h

### Hardware

- Touchscreen: Resistive, single-touch only (Panjit panjit_ts)
- Pressure: Binary (255=pressed, 0=released), no variable pressure
- LEDs: `/sys/class/leds/{red_led,green_led}/brightness` (0-100)
- Watchdog: `/dev/watchdog` (60s timeout, feed every 30s)

## Next Development Ideas

**Immediate:**
- [ ] Tetris portrait mode (rotate 90°)
- [ ] More games (Brick Breaker, Space Invaders)
- [ ] Expand hardware test suite

**Future:**
- [ ] Drawing program (finger painting)
- [ ] Sprite/animation system
- [ ] High score persistence
- [ ] Sound effects (if audio hardware exists)

## Known Issues

None - system is fully functional!

## Testing Tools

- `touch_debug` - Raw event stream viewer
- `touch_test` - Visual touch accuracy test
- `touch_inject` - Synthetic event injection
- `test_game_selector_scroll.py` - Automated UI testing
- `fb_to_png.py` - Framebuffer screenshot capture

**Note:** Touch input patterns from this project are used by the ScummVM backend - see [`../scummvm-roomwizard/SCUMMVM_DEV.md`](../scummvm-roomwizard/SCUMMVM_DEV.md) for details.

## Build Notes

- Cross-compiler: `arm-linux-gnueabihf-gcc`
- Static linking: `-static` flag
- Optimization: `-O2`
- All binaries: ARM 32-bit EABI5
