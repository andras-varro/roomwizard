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
- [`common/audio.c`](common/audio.c) - Speaker audio (OSS/TWL4030; beeps, tones, fanfares)

**Programs:**
- `snake/`, `tetris/`, `pong/` - Games
- `game_selector/` - Menu with scrolling; see [Game Selector Markers](#game-selector-markers) below
- `watchdog_feeder/` - Prevents 60s reset
- `hardware_test/` - Diagnostics (GUI + CLI)
- `tests/` - Touch debug/inject, framebuffer capture

## Game Selector Markers

See [`README.md`](README.md#game-selector-markers) for marker file reference.

## Key Technical Details

- **Touch:** Resistive single-touch, 12-bit raw → 800×480; event order ABS_X/Y → BTN_TOUCH → SYN_REPORT; capture coords before press. See [`SYSTEM_ANALYSIS.md#input`](../SYSTEM_ANALYSIS.md#input)
- **Screen safe area:** 800×480 fb, ~720×420 visible (bezel ~30-40px). Use `LAYOUT_*` macros from `common.h`
- **Speaker:** TWL4030 → `/dev/dsp` (OSS); amp enable GPIO12 HIGH; apply 50% volume attenuation. See [`common/audio.c`](common/audio.c)
- **Watchdog:** `/dev/watchdog` (60 s timeout); fed by system `/usr/sbin/watchdog`
- **LEDs:** `/sys/class/leds/{red_led,green_led}/brightness` (0-100)

## Backlog

### Tooling

- [x] **`build-and-deploy.sh`** ✅ — single script: cross-compile + `scp` all binaries + `chmod` in one step (takes IP as arg; replaces the separate `compile_for_roomwizard.sh` + manual scp)
- [ ] **Fix `Makefile`** — currently targets plain `gcc`, not the ARM cross-compiler; either wire it to `arm-linux-gnueabihf-gcc` or remove it to avoid confusion

### System Tools

- [ ] **Configuration tool (`rw_config`)** ⬅ **NEXT** — CLI tool to configure persistent device settings: set LED brightness percentages (red_led, green_led, backlight separately, 0-100); disable/enable audio (writes `/opt/games/audio.disabled` flag, all programs check this before opening `/dev/dsp`); set portrait/landscape mode (writes `/opt/games/portrait.mode` flag, all GUI programs check and apply a 90° coordinate+render transform). Run with no args prints current settings. Examples: `rw_config --backlight 70 --green 0 --audio off`, `rw_config --portrait on`, `rw_config --status`. Settings are stored in `/opt/games/rw_config.conf` (key=value) so they survive across launches.
- [ ] **Diagnostics tool (`rw_diag`)** — Standalone CLI/screen tool showing: free/used disk space for all mounted partitions (`/`, `/home/root/data`, `/home/root/log`, `/home/root/backup`); free/used RAM and swap from `/proc/meminfo`; current LED brightness values (`/sys/class/leds/{red_led,green_led,backlight}/brightness`); CPU model and clock from `/proc/cpuinfo`; kernel version and uptime (`/proc/version`, `/proc/uptime`); load average from `/proc/loadavg`; framebuffer mode (`/sys/class/graphics/fb0/`). Output to stdout (plain text columns); optionally draws to framebuffer when run on-device without a terminal.

### Gameplay

- [x] **Persistent high scores** ✅
- [x] **Audio component** ✅ — `common/audio.{h,c}` provides `audio_init/close/tone/beep/blip/success/fail`; games include `audio.h` and add `build/audio.o` to their link (already in `COMMON_OBJ`)
- [x] **Wire audio into games** ✅ — `Audio audio;` global in each game; `audio_init/close` in `main()`; **snake:** `audio_beep` on direction change, `audio_blip` on food eaten, `audio_fail` on death; **tetris:** `audio_beep` on move/rotate, `audio_blip` on line clear, `audio_success` on Tetris (4 lines), `audio_fail` on game over; **pong:** `audio_beep` on paddle hit (replaces `usleep`), `audio_blip` on scoring a point, `audio_success` on player win, `audio_fail` on player loss
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

**ScummVM touch patterns:** See [`../scummvm-roomwizard/SCUMMVM_DEV.md`](../scummvm-roomwizard/SCUMMVM_DEV.md#gesture-navigation).
