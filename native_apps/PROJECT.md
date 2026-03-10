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
- `hardware_diag/` - System diagnostics GUI (CPU, memory, disk, hardware status)
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

## App Development Best Practices

### App Lifecycle Template

Every app that uses the framebuffer/touch/hardware should follow this pattern in `main()`:

```c
int main(int argc, char *argv[]) {
    // 1. Parse args (fb_device, touch_device)
    // 2. Singleton guard
    int lock_fd = acquire_instance_lock("my_app");
    if (lock_fd < 0) return 1;

    // 3. Signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 4. Hardware init + apply configured backlight
    hw_init();
    hw_set_backlight(100);  // Scales to configured percentage

    // 5. Audio init (non-fatal)
    Audio audio;
    audio_init(&audio);

    // 6. Framebuffer + touch init
    Framebuffer fb;
    fb_init(&fb, fb_device);
    TouchInput touch;
    touch_init(&touch, touch_device);

    // 7. Main loop
    while (running) { /* ... */ }

    // 8. Cleanup (reverse order)
    hw_leds_off();
    hw_set_backlight(100);  // Restore configured brightness on exit
    audio_close(&audio);
    touch_close(&touch);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    fb_close(&fb);
    return 0;
}
```

### Hardware API Rules

1. **Always use `hw_set_led()` / `hw_set_leds()`** for LED control — never write to `/sys/class/leds/` directly. These functions respect the `led_enabled` and `led_brightness` config settings.

2. **Always use `hw_set_backlight()`** for backlight control — never write to `/sys/class/leds/backlight/brightness` directly. This function scales by the configured `backlight_brightness` percentage.

3. **Call `hw_set_backlight(100)` at both startup AND exit** of every app. The `100` means "100% of the configured maximum" — if the user set 40% in settings, this writes 40 to sysfs. This ensures:
   - Startup: backlight matches user's preference immediately
   - Exit: backlight is restored for the next app

4. **`hw_leds_off()` bypasses config** — it always writes 0 to sysfs. This is intentional: cleanup must work even when LEDs are disabled in config.

5. **Direct sysfs writes are only acceptable** in `hardware_config.c` test functions that intentionally bypass config to test raw hardware.

### Config Cache Behavior

- `hardware.c` caches config values on first `hw_set_led()` or `hw_set_backlight()` call. This avoids re-reading the config file on every LED write during rapid animations.
- **The cache is per-process** — each `fork()`/`exec()` child gets a fresh cache. This means newly launched apps always read the latest config.
- **Long-running processes** (like `game_selector`) must call `hw_reload_config()` after a child process exits that may have changed settings (e.g., `hardware_config`).
- **Do NOT assume config changes propagate automatically** to running processes.

### Common Pitfalls

| Pitfall | Consequence | Fix |
|---------|-------------|-----|
| Writing to sysfs directly instead of `hw_set_led()` | LED brightness/enable config ignored | Use `hw_set_led()` or `hw_set_leds()` |
| Missing `hw_set_backlight(100)` at startup | App starts with wrong brightness | Add after `hw_init()` |
| Missing `hw_set_backlight(100)` at exit | Next app inherits wrong brightness | Add before `fb_close()` |
| Not calling `hw_reload_config()` in launcher | Stale config after settings change | Call after child process returns |
| Using `hw_set_red_led()`/`hw_set_green_led()` | These route through `hw_set_led()` — safe | N/A (these are correct to use) |

### Audio Integration

- Call `audio_init(&audio)` early in `main()` — it's non-fatal if `/dev/dsp` is unavailable
- Check `config_audio_enabled()` before opening audio (handled by `audio_init`)
- Call `audio_close(&audio)` in cleanup
- Use `audio_interrupt()` before `audio_tone()` to cut off previous sound for responsive feedback

### Build System

- All apps link against common objects: `framebuffer.o`, `touch_input.o`, `hardware.o`, `common.o`, `config.o`, `ppm.o`, `logger.o`
- The `build-and-deploy.sh` script handles cross-compilation — the `Makefile` targets plain `gcc` and is not currently wired to the ARM cross-compiler
- VNC client has a separate `Makefile` in `vnc_client/` that references `../native_apps/common/` sources

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
- [x] **Diagnostics tool (`hardware_diag`)** ✅ — GUI tool showing system infos and settings on the framebuffer touchscreen. Multi-page design with tap navigation:
  - [x] free/used disk space for all mounted partitions (`/`, `/home/root/data`, `/home/root/log`, `/home/root/backup`);
  - [x] free/used RAM and swap from `/proc/meminfo`;
  - [x] current LED brightness values (`/sys/class/leds/{red_led,green_led,backlight}/brightness`);
  - [x] CPU model and clock from `/proc/cpuinfo`;
  - [x] kernel version and uptime (`/proc/version`, `/proc/uptime`);
  - [x] load average from `/proc/loadavg`;
  - [x] framebuffer mode (`/sys/class/graphics/fb0/`). Draws to framebuffer with touch navigation (5 pages: System, Memory, Storage, Hardware, Config).
- [ ] **Audio volume control** — add a centralized `audio_volume` setting (0-100) to `rw_config.conf` that scales all audio output. Currently audio is only on/off (`audio_enabled`). The `audio.c` library would need to apply a volume multiplier when generating samples. Add a volume slider to `hardware_config` alongside the existing audio toggle. ScummVM would need its own volume integration.
- [ ] **Screen saver** — add a `screensaver_timeout` setting (minutes, 0=disabled) that dims/blanks the backlight after a period of touch inactivity. Could be implemented as:
  - A feature in `app_launcher.c` that monitors touch activity and dims backlight after timeout
  - Touch event wakes the screen back up (set backlight to configured brightness)
  - Show a simple animation (clock, bouncing logo, or just blank) to prevent burn-in
  - The timeout and enable/disable should be configurable via `hardware_config`

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
