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
- `device_tools/` - **Unified hardware management app** (replaces `hardware_config`, `hardware_diag`, `hardware_test_gui`, `unified_calibrate`); see [Device Tools](#device-tools) below
- `hardware_test/` - CLI diagnostics (`hardware_test`, `pressure_test`); GUI version consolidated into `device_tools`
- `hardware_config/` - *(consolidated into `device_tools`)* Device settings GUI
- `hardware_diag/` - *(consolidated into `device_tools`)* System diagnostics GUI
- `tests/` - Touch debug/inject, framebuffer capture; `unified_calibrate` consolidated into `device_tools`

## Device Tools

[`device_tools/device_tools.c`](device_tools/device_tools.c) is a unified hardware management app that consolidates four separate GUI utilities into a single tab-based interface. Build with `make device_tools`.

**Tabs:**

| Tab | Replaces | Description |
|-----|----------|-------------|
| **Settings** | `hardware_config` | Audio enable/disable, LED enable/disable + brightness slider, backlight brightness slider, save/reset config. Test buttons bypass config for raw hardware verification. |
| **Diagnostics** | `hardware_diag` | Read-only system information across 5 sub-pages (System, Memory, Storage, Hardware, Config) with prev/next navigation. |
| **Tests** | `hardware_test_gui` | 10 interactive hardware tests (LED ramp, backlight, pulse, blink, color cycle, touch zone grid, display diagnostics, audio frequency sweep) in a 5×2 grid menu. Tests take over full screen. |
| **Calibration** | `unified_calibrate` | Touch calibration (4-corner crosshair) + bezel margin adjustment. Saves to `/etc/touch_calibration.conf`. |

**Notes:**
- The CLI tools `hardware_test` and `pressure_test` remain as separate binaries — they do not require a framebuffer/touch and are useful for headless diagnostics.
- Full design specification: [`device_tools/DESIGN.md`](device_tools/DESIGN.md)
- Build target: `make device_tools` (included in `make all` via `UTILS`)

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

- [ ] **Fix `Makefile`** — currently targets plain `gcc`, not the ARM cross-compiler; either wire it to `arm-linux-gnueabihf-gcc` or remove it to avoid confusion

### System Tools

- [ ] **Portrait mode support** — writes `/opt/games/portrait.mode` flag; all GUI programs check and apply a 90° coordinate+render transform. Add to device_tools Settings when implemented.
- [ ] **Audio volume control** — add a centralized `audio_volume` setting (0-100) to `rw_config.conf` that scales all audio output. Currently audio is only on/off (`audio_enabled`). The `audio.c` library would need to apply a volume multiplier when generating samples. Add a volume slider to device_tools Settings alongside the existing audio toggle. ScummVM would need its own volume integration.
- [ ] **Screen saver** — add a `screensaver_timeout` setting (minutes, 0=disabled) that dims/blanks the backlight after a period of touch inactivity. Could be implemented as:
  - A feature in `app_launcher.c` that monitors touch activity and dims backlight after timeout
  - Touch event wakes the screen back up (set backlight to configured brightness)
  - Show a simple animation (clock, bouncing logo, or just blank) to prevent burn-in
  - The timeout and enable/disable should be configurable via device_tools Settings

### Gameplay

- [x] **Wire audio into games** ✅ — `Audio audio;` global in each game; `audio_init/close` in `main()`; **snake:** `audio_beep` on direction change, `audio_blip` on food eaten, `audio_fail` on death; **tetris:** `audio_beep` on move/rotate, `audio_blip` on line clear, `audio_success` on Tetris (4 lines), `audio_fail` on game over; **pong:** `audio_beep` on paddle hit (replaces `usleep`), `audio_blip` on scoring a point, `audio_success` on player win, `audio_fail` on player loss
- [ ] **Portrait mode for Tetris** — rotate framebuffer 90° so the Tetris board is tall; requires coordinate transform in touch input and a rotated render path
- [ ] **Sprite-based game (e.g. Frogger)** — character crosses lanes of traffic; needs sprite blitting helpers in `framebuffer.c` (masked blit), game logic, and multi-lane scrolling
- [ ] **More games** — Brick Breaker (ball + paddle + bricks), Space Invaders (scanline sprites, wave progression)

### Touch Calibration

- [ ] **Calibration prompt on first boot** — detect missing `/etc/touch_calibration.conf` at game_selector startup and offer to run calibration before showing the menu

---

**ScummVM touch patterns:** See [`../scummvm-roomwizard/SCUMMVM_DEV.md`](../scummvm-roomwizard/SCUMMVM_DEV.md#gesture-navigation).
