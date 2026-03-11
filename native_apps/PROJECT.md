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
- Portrait mode: Phase 1 (foundation) + Phase 2 (app-level) + Phase 3 (rotation/layout) complete ✅ — transparent 90° rotation layer, touch transform, per-game layout adaptations

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
| **Settings** | `hardware_config` | Audio enable/disable, LED enable/disable + brightness slider, backlight brightness slider, portrait mode toggle, save/reset config. Test buttons bypass config for raw hardware verification. |
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

## Portrait Mode Support

### Completed (Phase 1 — Foundation)

- [x] **Make `SCREEN_SAFE_*` macros dynamic** — [`framebuffer.h`](common/framebuffer.h)/[`framebuffer.c`](common/framebuffer.c) now uses runtime globals (`screen_base_width`, `screen_base_height`) instead of hardcoded 800×480
- [x] **Touch input defaults use runtime globals** — [`touch_input.c`](common/touch_input.c) references `screen_base_width`/`screen_base_height` instead of hardcoded 800/480
- [x] **Verify `LAYOUT_*` macros auto-adapt** — all 8 macros in [`common.h`](common/common.h) and 3 screen template functions in [`common.c`](common/common.c) derive from `SCREEN_SAFE_*` with no hardcoded dimensions

### Completed (Phase 2 — App-level hardcoded values)

| File | Status | Changes |
|------|--------|---------|
| [`device_tools.c`](device_tools/device_tools.c) | ✅ | Removed `#define SCREEN_W 800` / `SCREEN_H 480`; converted `TZ_CELL_W`/`TZ_CELL_H` macros to local variables computed from `fb->width`/`fb->height`; replaced ~30 hardcoded 800/480/400 values with dynamic `fb->width`, `fb->height`, `fb->width / 2` throughout test functions (`draw_test_screen`, `test_touch_zone`, `test_display`, `test_audio_diag`, `run_calib_done`, `draw_display_page`) |
| [`hardware_test_gui.c`](hardware_test/hardware_test_gui.c) | ✅ | Removed `TZ_CELL_W`/`TZ_CELL_H` macros (converted to local variables); replaced ~25 hardcoded values with dynamic equivalents throughout (`draw_test_menu`, `draw_test_screen`, `test_touch_zone`, `test_display`, `test_audio_diag`, main layout init and exit button) |
| [`game_selector.c`](game_selector/game_selector.c) | ✅ | Replaced hardcoded `400` center X with `selector->fb.width / 2`; replaced `start_y = 100` with `SCREEN_SAFE_TOP + 80` for consistency with draw code |
| [`snake.c`](snake/snake.c) | ✅ | Replaced hardcoded game-over button Y positions: `326` → `fb.height * 68 / 100`, `396` → `restart_button.y + restart_button.height + 10` |
| [`tetris.c`](tetris/tetris.c) | ✅ | Replaced `board_offset_y = 80` with `SCREEN_SAFE_TOP + 60`; replaced game-over button Y positions: `326` → `fb.height * 68 / 100`, `396` → `restart_button.y + restart_button.height + 10` |
| [`pong.c`](pong/pong.c) | ✅ | Already fully dynamic — no changes needed |

### Completed (Phase 3 — Rotation + Game Layout Adaptations)

- [x] **Framebuffer rotation layer** — [`framebuffer.h`](common/framebuffer.h)/[`framebuffer.c`](common/framebuffer.c): `portrait_mode`, `phys_width`, `phys_height`, `back_buffer_size` fields; `fb_is_portrait_mode()` checks for `/opt/games/portrait.mode` flag; `fb_init()` swaps `width`↔`height` for virtual 480×800; `fb_swap()` performs cache-friendly 90° CCW rotated copy in portrait mode
- [x] **Touch coordinate transform** — [`touch_input.h`](common/touch_input.h)/[`touch_input.c`](common/touch_input.c): `portrait_mode` field on `TouchInput`; `scale_coordinates()` scales raw→physical, applies calibration, then rotates (virtual_x = phys_h−1−phys_y, virtual_y = phys_x)
- [x] **Config detection** — Flag file `/opt/games/portrait.mode` checked by both `fb_is_portrait_mode()` and `touch_init()`
- [x] **Pong portrait layout** — [`pong.c`](pong/pong.c): Horizontal paddles at top (AI) / bottom (player); ball bounces off left/right walls; player moves paddle with touch X; full `if (portrait_mode)` branching in init, reset, update, input, and draw
- [x] **Tetris portrait layout** — [`tetris.c`](tetris/tetris.c): Board centered horizontally (90px margins); NEXT preview moved to HUD area (top-right, compact); touch zones split board into left/center/right for move/rotate; controls hint below board
- [x] **Snake auto-adapts** — Grid is square, `cell_size = min(usable_w, usable_h) / GRID_SIZE`; narrower portrait width produces smaller cells, grid centers vertically with space above/below. No code changes needed.
- [x] **Brick Breaker auto-adapts** — Uses `SCREEN_SAFE_*` macros for play area and `AREA_W`/`AREA_H` derived from safe zone. No code changes needed.
- [x] **Game Selector / App Launcher auto-adapt** — Use `fb.width`/`fb.height` and `SCREEN_SAFE_*` for all positioning. No code changes needed.
- [x] **Device Tools portrait toggle** — [`device_tools.c`](device_tools/device_tools.c): Settings tab gains portrait mode toggle in Display section; Save creates/removes `/opt/games/portrait.mode`; Reset removes flag file
- [x] **Design document** — [`PORTRAIT_MODE_DESIGN.md`](PORTRAIT_MODE_DESIGN.md): Full architecture spec for Phase 3

### Future (Phase 4 — Polish)

- [ ] **PPM splash screen rotation** — PPM images (`*.ppm`) loaded by `ppm_display()` are not rotated; they render in physical orientation. Either pre-rotate the images or add rotation to the PPM loader.
- [x] **Calibration applied in portrait** — `touch_init()` now auto-loads and auto-enables calibration; bezel margins applied in `scale_coordinates()`. *(See [Fix #1](#fix-1-calibration-not-applied-in-portrait-mode--done))*
- [x] **App Launcher layout in portrait** — Dynamic `compute_grid_layout()` replaces hardcoded grid; all centering uses `fb.width / 2`. *(See [Fix #2](#fix-2-app-launcher-layout-broken-in-portrait--done))*
- [x] **Brick Breaker row count in portrait** — `init_brick_layout()` dynamically computes cols/rows from screen dimensions. *(See [Fix #3](#fix-3-brick-breaker-row-count--done))*
- [x] **Pong AI scoring during start screen** — Early-return guard in `update_game()` + `reset_game()` on transition. *(See [Fix #4](#fix-4-pong-ai-scoring-during-start-screen--done))*
- [ ] **Tap-a-Theremin layout in portrait** — *(See [Testing Result #5](#5-tap-a-theremin-layout-broken-in-portrait))*
- [ ] **Tetris UX polish in portrait** — *(See [Testing Result #6](#6-tetris-minor-ux-issues-in-portrait))*

## Portrait Mode Testing Results (Phase 4)

### What Works Well

- **Snake**: Great in portrait mode, no issues
- **Pong**: Works vertically, fun gameplay (has a separate bug — see [Issue #4](#4-pong--ai-scores-during-start-screen-not-portrait-specific) below)
- **Brick Breaker**: Scales very well, play area adapts correctly
- **Tetris**: Full screen in portrait, great overall (has minor UX issues — see [Issue #6](#6-tetris-minor-ux-issues-in-portrait) below)

### Issues Found

#### 1. Calibration Not Applied in Portrait Mode
- **File:** [`common/touch_input.c`](common/touch_input.c)
- Calibration settings from `/etc/touch_calibration.conf` are not being applied when in portrait mode
- **Status:** ✅ FIXED (2026-03-11)

#### 2. App Launcher Layout Broken in Portrait
- **File:** [`app_launcher/app_launcher.c`](app_launcher/app_launcher.c)
- The 2-row × 3-column grid overflows the 480px portrait width (3×200px tiles = 600+px)
- Icon grid layout should be calculated dynamically from available screen space
- "ROOMWIZARD" title text is not centered — starts at middle and overflows (hardcoded x=400)
- Page indicator dots at bottom are not centered (hardcoded x=400)
- Paging arrows work correctly at screen edges
- **Status:** ✅ FIXED (2026-03-11)

#### 3. Brick Breaker Row Count Not Adapted
- **File:** [`brick_breaker/brick_breaker.c`](brick_breaker/brick_breaker.c)
- The 5 rows of bricks look funny in portrait mode (too few for the tall screen)
- Number of brick rows should be calculated from available screen height
- **Status:** ✅ FIXED (2026-03-11)

#### 4. Pong — AI Scores During Start Screen (Not portrait-specific)
- **File:** [`pong/pong.c`](pong/pong.c)
- When waiting at "Tap to start" screen, the AI accumulates points before the game begins
- If you wait and then tap, AI already has 2+ points
- **Status:** ✅ FIXED (2026-03-11)

#### 5. Tap-a-Theremin Layout Broken in Portrait
- **File:** [`tests/audio_touch_test.c`](tests/audio_touch_test.c)
- Play area (color bars, low-high text) scales well
- Title "THEREMIN" is off-center (uses hardcoded `SCREEN_W=800` for centering)
- Instruction text "TOUCH AND SLIDE TO PLAY" is off-center (same hardcoded centering)
- Touch transformation is incorrect — marker position accelerates/jumps ahead during swipe
- **Status:** NEEDS FIX

#### 6. Tetris Minor UX Issues in Portrait
- **File:** [`tetris/tetris.c`](tetris/tetris.c)
- Rotation only works in a narrow center band; touching closer to edges triggers left/right move instead of rotate. Since there's ample screen space in portrait, touching the game area should rotate, and only areas close to/outside the board edges should move left/right
- Next block preview overlaps with the exit button
- Control hint text "L/R: MOVE, CENTER: ROTATE, BOTTOM: DROP" starts at left screen edge instead of being centered under the play area
- **Status:** NEEDS FIX

#### 7. VNC Client & ScummVM
Not affected by orientation, no changes needed currently.

### Portrait Mode Fix Progress (2026-03-11)

#### Fixes Completed

**Fix #1: Calibration Not Applied in Portrait Mode — DONE**
- **File:** [`common/touch_input.c`](common/touch_input.c)
- **Root cause:** Apps never called `touch_load_calibration()` + `touch_enable_calibration()`, so `calib.enabled` remained false
- **Fix:** `touch_init()` now auto-loads calibration from `/etc/touch_calibration.conf` and auto-enables it
- Also: `touch_load_calibration()` now auto-enables on successful parse
- Also: Added bezel margin application in `scale_coordinates()` — margins were loaded but never applied
- Pipeline is now: raw → scale to physical (800×480) → bilinear calibration → bezel margins → rotate if portrait → clamp

**Fix #2: App Launcher Layout Broken in Portrait — DONE**
- **File:** [`app_launcher/app_launcher.c`](app_launcher/app_launcher.c)
- Replaced hardcoded grid constants with dynamic `compute_grid_layout()` function
- Portrait: 2 cols × 3 rows (tiles sized to fit 480px width); Landscape: 3 cols × 2 rows (200×150 tiles)
- Fixed all hardcoded `400` center-X values → `fb.width / 2` (title, page dots, empty state messages)
- Grid is recomputed after returning from launched apps

**Fix #3: Brick Breaker Row Count — DONE**
- **File:** [`brick_breaker/brick_breaker.c`](brick_breaker/brick_breaker.c)
- Added `init_brick_layout()` function that dynamically computes `brick_cols` and `brick_rows` from screen dimensions
- Portrait: 8 cols (60px-wide bricks) × ~14 rows (fills top 45% of play area); Landscape: 12 cols × 8 rows (unchanged)
- Level row counts scale proportionally: `scaled_rows = level.rows * brick_rows / 8`
- Health patterns stretch to fill extra rows via `base_r` mapping

**Fix #4: Pong AI Scoring During Start Screen — DONE**
- **File:** [`pong/pong.c`](pong/pong.c)
- Added early-return guard in `update_game()` when `current_screen != SCREEN_PLAYING`
- Added `reset_game()` call when transitioning from welcome to playing state
- Prevents ball movement/scoring during start screen; scores guaranteed 0-0 on game start

#### Fixes Remaining (Next Session)

**Fix #5: Tap-a-Theremin Layout — NOT STARTED**
- **File:** [`tests/audio_touch_test.c`](tests/audio_touch_test.c)
- Issues: Title "THEREMIN" off-center (hardcoded `SCREEN_W=800`), instruction text off-center, touch transformation incorrect (marker jumps ahead)
- Plan: Replace `SCREEN_W`/`SCREEN_H` hardcoded constants with `fb.width`/`fb.height` runtime values; recalculate `PAD_X`, `PAD_Y`, `PAD_W`, `PAD_H` from safe area macros; fix star coordinate positions; fix touch-to-frequency mapping to use correct coordinate space
- Hardcoded values to fix: `SCREEN_W=800`, `SCREEN_H=480`, `PAD_Y=70`, `PAD_H=310`, star positions at lines 122–123

**Fix #6: Tetris UX Issues in Portrait — NOT STARTED**
- **File:** [`tetris/tetris.c`](tetris/tetris.c)
- **Issue A:** Rotation zone too narrow — touching near board edges triggers left/right move instead of rotate. Fix: Expand rotate zone to cover the entire game board area; only areas close to or beyond board edges should trigger movement
- **Issue B:** Next block preview overlaps exit button. Fix: Reposition the next-block preview or the exit button so they don't overlap
- **Issue C:** Control hint text "L/R: MOVE, CENTER: ROTATE, BOTTOM: DROP" starts at left screen edge. Fix: Center it under the play area using `board_offset_x + board_width_px / 2`

#### Build Verification — Pending

All 4 completed fixes need to be cross-compiled and tested on device:
```bash
native_apps/build-and-deploy.sh                        # build only
native_apps/build-and-deploy.sh 192.168.50.73          # build + deploy
```

## Backlog

### Tooling

- [ ] **Fix `Makefile`** — currently targets plain `gcc`, not the ARM cross-compiler; either wire it to `arm-linux-gnueabihf-gcc` or remove it to avoid confusion

### System Tools

- [x] **Portrait mode support** ✅ — `/opt/games/portrait.mode` flag file; transparent 90° rotation in `fb_swap()`; touch coordinate transform; per-game layout adaptations (Pong top/bottom paddles, Tetris centered board); device_tools Settings toggle. See [Portrait Mode Support](#portrait-mode-support).
- [ ] **Audio volume control** — add a centralized `audio_volume` setting (0-100) to `rw_config.conf` that scales all audio output. Currently audio is only on/off (`audio_enabled`). The `audio.c` library would need to apply a volume multiplier when generating samples. Add a volume slider to device_tools Settings alongside the existing audio toggle. ScummVM would need its own volume integration.
- [ ] **Screen saver** — add a `screensaver_timeout` setting (minutes, 0=disabled) that dims/blanks the backlight after a period of touch inactivity. Could be implemented as:
  - A feature in `app_launcher.c` that monitors touch activity and dims backlight after timeout
  - Touch event wakes the screen back up (set backlight to configured brightness)
  - Show a simple animation (clock, bouncing logo, or just blank) to prevent burn-in
  - The timeout and enable/disable should be configurable via device_tools Settings

### Gameplay

- [x] **Wire audio into games** ✅ — `Audio audio;` global in each game; `audio_init/close` in `main()`; **snake:** `audio_beep` on direction change, `audio_blip` on food eaten, `audio_fail` on death; **tetris:** `audio_beep` on move/rotate, `audio_blip` on line clear, `audio_success` on Tetris (4 lines), `audio_fail` on game over; **pong:** `audio_beep` on paddle hit (replaces `usleep`), `audio_blip` on scoring a point, `audio_success` on player win, `audio_fail` on player loss
- [x] **Portrait mode for Tetris** ✅ — board centered horizontally, NEXT preview in HUD area, touch zones adapted for narrower screen
- [ ] **Sprite-based game (e.g. Frogger)** — character crosses lanes of traffic; needs sprite blitting helpers in `framebuffer.c` (masked blit), game logic, and multi-lane scrolling
- [ ] **More games** — Brick Breaker (ball + paddle + bricks), Space Invaders (scanline sprites, wave progression)

### Touch Calibration

- [ ] **Calibration prompt on first boot** — detect missing `/etc/touch_calibration.conf` at game_selector startup and offer to run calibration before showing the menu

---

**ScummVM touch patterns:** See [`../scummvm-roomwizard/SCUMMVM_DEV.md`](../scummvm-roomwizard/SCUMMVM_DEV.md#gesture-navigation).
