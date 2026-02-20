# Native Games Project

## Current Status

**System:** Fully functional ✅
- 3 games: Snake, Tetris, Pong
- Game selector with scrolling
- Hardware test suite
- Touch input: accurate (3px center, 14-27px corners)
- Double buffering: flicker-free rendering
- LED effects: integrated
- Watchdog: handled by system `/usr/sbin/watchdog` — no custom feeder needed

**Device:** RoomWizard - See [`SYSTEM_ANALYSIS.md#hardware-platform`](../SYSTEM_ANALYSIS.md#hardware-platform) for full specs
**IP:** 192.168.50.73 - See [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md) for SSH key auth setup

## Quick Commands

```bash
./build-and-deploy.sh                        # build only
./build-and-deploy.sh 192.168.50.73          # build + deploy binaries
./build-and-deploy.sh 192.168.50.73 permanent  # + install boot service + reboot
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
- `game_selector/` - Menu with scrolling; see [Game Selector Markers](#game-selector-markers) below
- `watchdog_feeder/` - Prevents 60s reset
- `hardware_test/` - Diagnostics (GUI + CLI)
- `tests/` - Touch debug/inject, framebuffer capture

## Game Selector Markers

Two non-executable marker files alongside binaries in `/opt/games/` control `game_selector` without recompiling.

| Marker | Effect |
|---|---|
| `<name>.noargs` | Launch without `fb_dev`/`touch_dev` args (apps that open devices themselves) |
| `<name>.hidden` | Hide from the menu entirely |

```bash
# Hide:    touch /opt/games/<name>.hidden  && chmod 644 /opt/games/<name>.hidden
# Un-hide: rm /opt/games/<name>.hidden
# No-args: touch /opt/games/<name>.noargs  && chmod 644 /opt/games/<name>.noargs
```

Current on device: **Hidden:** `watchdog_feeder`, `touch_test`, `touch_debug`, `touch_inject`, `touch_calibrate`, `unified_calibrate`, `pressure_test` | **No-args:** `scummvm`

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

## Backlog

### Tooling

- [x] **`build-and-deploy.sh`** ✅ — single script: cross-compile + `scp` all binaries + `chmod` in one step (takes IP as arg; replaces the separate `compile_for_roomwizard.sh` + manual scp)
- [ ] **Fix `Makefile`** — currently targets plain `gcc`, not the ARM cross-compiler; either wire it to `arm-linux-gnueabihf-gcc` or remove it to avoid confusion

### Gameplay

- [x] **Persistent high scores** ✅ — top-5 leaderboard per game (`snake.hig`, `tetris.hig` in `/home/root/data/`); touch-driven on-screen keyboard for name entry (10 chars, A-Z + space + DEL/CLEAR/OK); gold/silver/bronze ranking display; RESET SCORES button on game over screen; central component in `common/highscore.{h,c}`
- [ ] **Portrait mode for Tetris** — rotate framebuffer 90° so the Tetris board is tall; requires coordinate transform in touch input and a rotated render path
- [ ] **Sprite-based game (e.g. Frogger)** — character crosses lanes of traffic; needs sprite blitting helpers in `framebuffer.c` (masked blit), game logic, and multi-lane scrolling
- [ ] **More games** — Brick Breaker (ball + paddle + bricks), Space Invaders (scanline sprites, wave progression)

### Game Selector

- [ ] **Graphical launcher** — replace text list with a grid/icon view; each game has a small thumbnail or icon drawn via `framebuffer.c`; animated selection highlight
- [ ] **Launch animation** — brief transition effect (e.g. fade or wipe) when selecting a game

### Hardware Test

- [ ] **System info panel** — CPU load, free memory (`/proc/meminfo`), uptime, kernel version, framebuffer mode — display in a scrollable panel
- [ ] **Additional tests** — backlight ramp (fade in/out), LED blink pattern, touch raw-value display (live ABS_X/ABS_Y stream), watchdog status
- [ ] **Stress test mode** — sustained rendering loop + touch read to check for hangs or overheating

### Touch Calibration

- [ ] **Integrate calibration into game selector** — add "Calibrate" entry in the menu that launches `unified_calibrate` directly (un-hide it or make game_selector invoke it inline) so users don't need a shell
- [ ] **Calibration prompt on first boot** — detect missing `/etc/touch_calibration.conf` at game_selector startup and offer to run calibration before showing the menu

---

**Note:** Touch input patterns used by the ScummVM backend — see [`../scummvm-roomwizard/SCUMMVM_DEV.md`](../scummvm-roomwizard/SCUMMVM_DEV.md).
