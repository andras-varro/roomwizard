# Native Games Project

## Current Status

**System:** Fully functional ✅
- 5 games: Snake, Tetris, Pong, Theremin, Brickbreaker
- Graphical game selector with horizontal scrolling
- Hardware test suite
- Touch input: accurate with calibration
- Double buffering: flicker-free rendering
- LED effects: integrated
- Audio effects: integrated (beeps, tones, fanfares)
- Watchdog: handled by system `/usr/sbin/watchdog` — no custom feeder needed

**Device:** RoomWizard - See [`SYSTEM_ANALYSIS.md#hardware-platform`](../SYSTEM_ANALYSIS.md#hardware-platform) for full specs
**IP:** 192.168.50.73 - See [`COMMISSIONING.md`](../COMMISSIONING.md) for device setup

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
- [`common/config.c`](common/config.c) - Persistent configuration (key=value file, `/opt/games/rw_config.conf`)

**Programs:**
- `snake/`, `tetris/`, `pong/` - Games
- `app_launcher/` - Menu with scrolling; see [Game Selector Markers](#game-selector-markers) below
- `watchdog_feeder/` - Prevents 60s reset
- `hardware_test/` - Diagnostics (GUI + CLI)
- `hardware_config/` - Device settings GUI (audio, LEDs, backlight, config file management)
- `tests/` - Touch debug/inject, framebuffer capture

## Game Selector Markers

See [`README.md`](README.md#game-selector-markers) for marker file reference.

## Key Technical Details

- **Touch:** Resistive single-touch, 12-bit raw → 800×480; event order ABS_X/Y → BTN_TOUCH → SYN_REPORT; capture coords before press. See [`SYSTEM_ANALYSIS.md#input`](../SYSTEM_ANALYSIS.md#input)
- **Screen safe area:** 800×480 fb, ~720×420 visible. Use `LAYOUT_*` macros from `common.h`
- **Speaker:** TWL4030 → `/dev/dsp` (OSS); amp enable GPIO12 HIGH; apply 50% volume attenuation. See [`common/audio.c`](common/audio.c)
- **Watchdog:** `/dev/watchdog` (60 s timeout); fed by system `/usr/sbin/watchdog`
- **LEDs:** `/sys/class/leds/{red_led,green_led}/brightness` (0-100)
- **Config:** `/opt/games/rw_config.conf` (key=value); read by `config.h` at app startup; settings: `audio_enabled`, `led_enabled`, `led_brightness`, `backlight_brightness`

## Backlog

### Tooling

- [x] **`build-and-deploy.sh`** ✅ — single script: cross-compile + `scp` all binaries + `chmod` in one step (takes IP as arg; replaces the separate `compile_for_roomwizard.sh` + manual scp)
- [ ] **Fix `Makefile`** — currently targets plain `gcc`, not the ARM cross-compiler; either wire it to `arm-linux-gnueabihf-gcc` or remove it to avoid confusion

### System Tools

- [x] **Configuration tool (`hardware_config`)** ✅ — GUI tool to configure persistent device settings - potentially integrate with hardware-test, renaming the tool appropriately:
  - [x] disable LED effects (writes `/opt/games/led_effects.disabled` flag, all programs check this before writing LED brightness);
  - [x] set LED brightness percentages (red_led, green_led, backlight separately, 0-100);
  - [x] disable/enable audio (writes `/opt/games/audio.disabled` flag, all programs check this before opening `/dev/dsp`). scummvm-roomwizard as well;
  - [x] Settings are stored in `/opt/games/rw_config.conf` (key=value) so they survive across launches and can be edited via shell if needed; the config tool provides a friendly UI for changing them without a shell. Consider which is better, the marker files or a single config file; the file approach is more scalable and easier to manage as settings grow, but marker files are simpler to check in code (just `access()` instead of parsing a config).
  - [x] Consider adding a "Test" button for each setting (e.g., flash LEDs at configured brightness, play a test tone for audio, show a rotation test pattern for portrait mode) to verify changes before saving.
  - [x] Add a "Reset to Defaults" option that removes all custom settings and restores factory behavior.
  - [x] The tool should gracefully handle missing `/opt/games/` directory (create it if needed with proper permissions).
  - [x] All config changes should take effect immediately for newly launched programs; running programs may need restart to pick up changes (display a notice if applicable).
  - **Known issues (from device testing 2026-03-09):**
    - [x] **Backlight config: app_launcher resets to 100%** — The `app_launcher` plays a fade-out from 100% before launching each app, and starts new apps at 100% backlight. It writes to sysfs directly instead of using `hw_set_backlight()`. Fix: find the fade animation in `app_launcher.c` and route through `hw_set_backlight()`, or read config and use configured max as the fade target instead of 100%. — Fixed: `game_selector.c` now calls `hw_set_backlight(100)` after child game exits to restore configured backlight.
    - [x] **Backlight config: most programs exit at 100%** — hardware_test, hardware_config, Tetris, Brick Breaker, Pong, ScummVM, VNC client all exit with backlight at 100%. Snake and Theremin correctly exit with configured backlight (their fade-out goes through `hw_set_backlight()`). Fix: ensure all programs' exit/cleanup paths use `hw_set_backlight()` instead of direct sysfs writes. — Fixed: all programs now call `hw_set_backlight(100)` on exit, which applies the configured percentage.
    - [x] **LED config: Snake ignores LED settings** — Snake uses full LED brightness regardless of `led_enabled` or `led_brightness` config. It likely writes to `/sys/class/leds/` directly instead of calling `hw_set_led()`. Brick Breaker, Tetris, and Pong correctly respect both LED enable and brightness settings (they use `hw_set_led()`). Fix: update `snake.c` to use `hw_set_led()` for all LED operations. — Fixed: `hw_set_red_led()`, `hw_set_green_led()`, and `hw_set_leds()` now route through `hw_set_led()` which respects config.
    - [ ] **Test buttons bypass config (by design)** — The audio and LED test buttons in hardware_config intentionally bypass config to allow testing hardware when settings are disabled. Consider whether they should respect the enabled/disabled toggles (so user can verify config takes effect) or keep current behavior (so user can always test raw hardware). Current: bypasses config via direct `/dev/dsp` open and direct sysfs writes.
    - [x] **VNC Client doesn't use hardware.h** — Fixed: added `config.c` to VNC client Makefile so `hardware.c` can read `rw_config.conf`. VNC client now respects backlight, LED, and audio settings. `hw_set_backlight(100)` called at startup and exit.
  - **Future: merge with hardware_test** — The hardware_config (Settings) and hardware_test (Diagnostics) tools should be merged into a single "Device Tools" app with tabbed navigation. This reduces app launcher clutter and provides a unified device management experience.
- [ ] **Portrait mode support** — writes `/opt/games/portrait.mode` flag; all GUI programs check and apply a 90° coordinate+render transform. Add to hardware_config when implemented.
- [ ] **Diagnostics tool (`hardware_diag`)** — GUI tool showing system infos and settings. - potentially integrate with hardware-test, renaming the tool appropriately:
  - [ ] free/used disk space for all mounted partitions (`/`, `/home/root/data`, `/home/root/log`, `/home/root/backup`); 
  - [ ] free/used RAM and swap from `/proc/meminfo`; 
  - [ ] current LED brightness values (`/sys/class/leds/{red_led,green_led,backlight}/brightness`); 
  - [ ] CPU model and clock from `/proc/cpuinfo`; 
  - [ ] kernel version and uptime (`/proc/version`, `/proc/uptime`); 
  - [ ] load average from `/proc/loadavg`; 
  - [ ] framebuffer mode (`/sys/class/graphics/fb0/`). Output to stdout (plain text columns); optionally draws to framebuffer when run on-device without a terminal.

### Gameplay

- [x] **Persistent high scores** ✅
- [x] **Audio component** ✅ — `common/audio.{h,c}` provides `audio_init/close/tone/beep/blip/success/fail`; games include `audio.h` and add `build/audio.o` to their link (already in `COMMON_OBJ`)
- [x] **Wire audio into games** ✅ — `Audio audio;` global in each game; `audio_init/close` in `main()`; **snake:** `audio_beep` on direction change, `audio_blip` on food eaten, `audio_fail` on death; **tetris:** `audio_beep` on move/rotate, `audio_blip` on line clear, `audio_success` on Tetris (4 lines), `audio_fail` on game over; **pong:** `audio_beep` on paddle hit (replaces `usleep`), `audio_blip` on scoring a point, `audio_success` on player win, `audio_fail` on player loss
- [ ] **Portrait mode for Tetris** — rotate framebuffer 90° so the Tetris board is tall; requires coordinate transform in touch input and a rotated render path
- [ ] **Sprite-based game (e.g. Frogger)** — character crosses lanes of traffic; needs sprite blitting helpers in `framebuffer.c` (masked blit), game logic, and multi-lane scrolling
- [ ] **More games** — Brick Breaker (ball + paddle + bricks), Space Invaders (scanline sprites, wave progression)

### Game Selector

- [X] **Graphical launcher** — replace text list with a grid/icon view; each game has a small thumbnail or icon drawn via `framebuffer.c`; animated selection highlight
- [X] **Launch animation** — brief transition effect (e.g. fade or wipe) when selecting a game

### Hardware Test

- [ ] **System info panel** — CPU load, free memory (`/proc/meminfo`), uptime, kernel version, framebuffer mode — display in a scrollable panel
- [X] **Additional tests** — backlight ramp (fade in/out), LED blink pattern, touch raw-value display (live ABS_X/ABS_Y stream), watchdog status
- [ ] **Stress test mode** — sustained rendering loop + touch read to check for hangs or overheating

### Touch Calibration

- [X] **Integrate calibration into game selector** — add "Calibrate" entry in the menu that launches `unified_calibrate` directly (un-hide it or make game_selector invoke it inline) so users don't need a shell
- [ ] **Calibration prompt on first boot** — detect missing `/etc/touch_calibration.conf` at game_selector startup and offer to run calibration before showing the menu

---

**ScummVM touch patterns:** See [`../scummvm-roomwizard/SCUMMVM_DEV.md`](../scummvm-roomwizard/SCUMMVM_DEV.md#gesture-navigation).
