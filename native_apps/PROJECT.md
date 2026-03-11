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
- [x] **Tap-a-Theremin layout in portrait** — *(See [Fix #5](#fix-5-tap-a-theremin-layout--done))*
- [x] **Tetris UX polish in portrait** — *(See [Fix #6](#fix-6-tetris-ux--done))*

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
- **Status:** ✅ FIXED (2026-03-11)

#### 6. Tetris Minor UX Issues in Portrait
- **File:** [`tetris/tetris.c`](tetris/tetris.c)
- Rotation only works in a narrow center band; touching closer to edges triggers left/right move instead of rotate. Since there's ample screen space in portrait, touching the game area should rotate, and only areas close to/outside the board edges should move left/right
- Next block preview overlaps with the exit button
- Control hint text "L/R: MOVE, CENTER: ROTATE, BOTTOM: DROP" starts at left screen edge instead of being centered under the play area
- **Status:** ✅ FIXED (2026-03-11)

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

**Fix #5: Tap-a-Theremin Layout — DONE**
- **File:** [`tests/audio_touch_test.c`](tests/audio_touch_test.c)
- Removed hardcoded `SCREEN_W=800`/`SCREEN_H=480` constants
- Title and instruction text now center using `screen_base_width/2`
- `PAD_Y` and `PAD_H` computed from safe area macros (adapts to portrait height)
- Touch screen size set from runtime dimensions (fixes marker acceleration bug)
- Star positions scale with `fb->width`/`fb->height`

**Fix #6: Tetris UX — DONE**
- **File:** [`tetris/tetris.c`](tetris/tetris.c)
- Rotation zone expanded from center 1/3 to center 60% of board; sides 20% each for movement
- Next block preview repositioned left of exit button (no overlap)
- Control hint text centered under play area using `text_measure_width()`

#### Build Verification — Pending

All 6 completed fixes need to be cross-compiled and tested on device:
```bash
native_apps/build-and-deploy.sh                        # build only
native_apps/build-and-deploy.sh 192.168.50.73          # build + deploy
```

## Best Practices & Pitfalls for Portrait Mode Development

### ✅ Best Practices

1. **Never hardcode screen dimensions** — Use `fb.width`/`fb.height` or `screen_base_width`/`screen_base_height` instead of literal `800`/`480` values. Use `SCREEN_SAFE_*` macros for layout within safe areas. Every hardcoded pixel value is a portrait-mode bug waiting to happen.

2. **Use `fb.width / 2` for horizontal centering** — Never hardcode `400` as center X. This was the #1 most common bug pattern found across app_launcher, theremin, and other apps. Use `fb.width / 2` or `screen_base_width / 2`.

3. **Compute layout at init time, not compile time** — Replace `#define` constants for grid dimensions, tile sizes, and element counts with runtime variables computed in an `init_layout()` function called after `fb_init()`. See [`app_launcher.c`](app_launcher/app_launcher.c) `compute_grid_layout()` and [`brick_breaker.c`](brick_breaker/brick_breaker.c) `init_brick_layout()` as examples.

4. **Scale game elements proportionally to screen dimensions** — For games with grids (brick rows, tile columns), calculate counts from available area: `count = available_space * ratio / (element_size + padding)`. Don't use fixed counts that only work for one orientation.

5. **Touch coordinate space must match drawing coordinate space** — Call `touch_set_screen_size()` with the virtual screen dimensions (`screen_base_width`, `screen_base_height`), not hardcoded values. Mismatched coordinate spaces cause markers/cursors to track incorrectly (the "marker hurries forward" bug).

6. **Calibration is auto-loaded now** — `touch_init()` automatically loads calibration from `/etc/touch_calibration.conf` and enables it. Apps no longer need to manually call `touch_load_calibration()` + `touch_enable_calibration()`. Don't disable calibration unless there's a specific reason.

7. **Guard game state properly** — Ensure game logic (ball movement, AI, scoring) only runs in the PLAYING state, not during welcome/start screens. The Pong bug where AI scored during the start screen is a classic state-management pitfall.

8. **Use `fb.portrait_mode` for orientation-specific branching** — When layout needs to be fundamentally different (e.g., 3×2 vs 2×3 grid), check `fb.portrait_mode` and branch. For simple scaling, let the runtime dimensions do the work automatically.

### ⚠️ Common Pitfalls

1. **Hardcoded pixel positions for decorative elements** — Stars, background patterns, and decorative positions often get hardcoded and forgotten. Scale them relative to `fb->width`/`fb->height`.

2. **Touch zones using fixed pixel widths** — Touch zones for game controls (rotate/move) should be percentages of the board/play area, not fixed pixel widths. A zone that feels right at 800px is too narrow at 480px.

3. **UI elements overlapping in portrait** — Elements positioned relative to screen edges (exit button, next-block preview) can collide when the screen is narrower. Always check that elements positioned relative to different edges don't overlap.

4. **Forgetting to recompute layout after app returns** — If your app launches a child process and re-acquires the framebuffer, recompute layout variables. The framebuffer dimensions don't change, but variables derived from them need re-initialization.

5. **Calibration pipeline order matters** — The correct pipeline is: raw → scale to physical (800×480) → bilinear calibration → bezel margins → rotate if portrait → clamp. Applying calibration in the wrong coordinate space produces incorrect offsets.

6. **PPM splash images not rotated** — PPM images are loaded at their native orientation. Portrait mode rotation happens at the framebuffer level (`fb_swap()`), so PPM content displays correctly if drawn to the back buffer. However, images designed for 800×480 will have wrong aspect ratio in portrait — consider loading different images or scaling.

7. **`#define` macros referencing runtime values** — Macros like `#define PAD_H 310` become stale constants. If a dimension depends on screen size, it should either be a macro that references a runtime variable (e.g., `#define PAD_H (SCREEN_SAFE_BOTTOM - PAD_Y - 100)`) or a plain variable.

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

## Phase 5: Portrait Mode Polish & UI Unification

> **✅ All Phase 5 items verified deployed on device (2026-03-11).** Cross-compiled, deployed via `build-and-deploy.sh`, and confirmed working on hardware.

### Reported Issues (2026-03-11)

1. **Bezel compensation incorrect in portrait mode** — In portrait mode, the bezel compensation padding meant for the longer edges (top/bottom in landscape) is being applied to the shorter edges (top/bottom in portrait). Affects BrickBreaker, Theremin, Snake, Device Tools, and likely other apps. Root cause: `fb_load_safe_area()` reads margins as physical `top bottom left right` but doesn't rotate them when portrait mode is active. `[FIXED]`

2. **BrickBreaker ball speed too slow** — The distance between paddle and blocks in portrait mode is too large. Ball takes 6-7 seconds to reach blocks from paddle. Initial ball speed should be calculated based on the distance the ball needs to cover (paddle-to-blocks distance). `[FIXED]`

3. **BrickBreaker needs Game Over screen with highscore** — Currently inserts "PLAYER" as name without prompting. Should have proper name entry like Snake/Tetris. Create reusable game-over components from the Snake approach. `[FIXED]`

4. **Game Over screen needs Exit option** — Add an Exit button to the Game Over screen (return to game selector/launcher). `[FIXED]`

5. **Pong start screen text not centered** — "TOUCH TO MOVE PADDLE" and "FIRST TO 11 WINS" text on the start/welcome screen is not properly centered. `[FIXED]`

6. **Pong game screen text not centered** — "TOUCH TO MOVE PADDLE" instruction text during gameplay is not properly centered. `[FIXED]`

7. **Pong user point indicator positioning** — In portrait mode, the user (green) score at the bottom is drawn at the very edge of the game area. Either move user score to the top beside AI score, or leave sufficient space at the bottom. `[FIXED]`

8. **Pong Game Over screen not unified** — Should match Snake/Tetris pattern with: transparent background overlay, highscore support (track last winners or user's score against AI), exit button. Since Pong always goes to 11, "high score" could track opponents beaten or the user's points when AI wins. `[FIXED]`

9. **Snake and Tetris game logic runs during start screen** — The game starts processing in the background while the start/welcome screen is displayed. Game logic should only begin after the player dismisses the start screen. `[FIXED]`

10. **Tetris start screen text not centered** — "TAP LEFT/RIGHT: MOVE..." instruction text on the start screen is not properly centered. `[FIXED]`

11. **Pause menu transparency inconsistent** — BrickBreaker's pause menu has ~50% transparent overlay showing the game underneath. Pong, Snake, and Tetris pause menus clear to solid black. Make all pause menus consistent with ~50% transparency. `[FIXED]`

12. **Tetris screen area underutilized** — Significant unused space to the right, left, and bottom of the Tetris play area. In portrait mode, the board is drawn from the top with ~1 inch gap at the bottom. Better utilize available screen real estate and center the play area vertically. `[FIXED]`

13. **Device Tools portrait mode toggle text incorrect** — The portrait mode toggle switch label says "PORTRAIT MODE (REBOOT)" — reboot is not true or required; the change takes effect on next app launch. `[FIXED]`

14. **Device Tools should apply changes immediately** — When saving settings for portrait mode and backlight, changes should be applied to the running device tools instance immediately, not require restart. `[FIXED]`

15. **Device Tools tab overflow** — The tab bar shows only ~2.5 tabs (Settings, Diagnostics, Tes...) before the Exit X button overlaps with the "Tests" tab. The "Calibration" tab is not visible at all. Need tab scrolling or a more compact tab layout. `[FIXED]`

### Implementation Plan

> **Status**: All 15 issues implemented (2026-03-11). Pending cross-compile and deploy verification.

#### Priority Order
1. **Foundation fixes** — Bezel compensation rotation (affects all apps)
2. **Reusable components** — Unified game-over screen with highscore, exit, transparent overlay
3. **Game-specific fixes** — Ball speed, text centering, score positioning, screen area usage
4. **Device Tools fixes** — Tab scrolling, label text, apply-on-save
5. **Polish** — Pause menu transparency, prevent background game logic

## Phase 6: Portrait Mode Testing & Fixes (Device Tools)

**Status**: In Progress

User tested portrait mode on hardware and identified the following issues:

### Issue 1: Settings Tab — Text and Slider Overflow
- **Symptom**: In portrait mode, the "SETTINGS" text in the tab bar overflows boundaries. The Backlight and LED Brightness controls (with + and - buttons and the progress bar) are too wide — the right side is not visible.
- **Root Cause**: Tab label "SETTINGS" (8 chars) is too long for the narrow portrait tab width (~93px). `BAR_WIDTH` is hardcoded at 300px, and the +/- button positions computed from `CONTENT_LEFT + 190 + BAR_WIDTH + 70` = ~560px exceeds portrait content width of ~460px.
- **Fix**: Rename "SETTINGS" short label to "SET" or "CONFIG" when tab width is narrow. Make `BAR_WIDTH` dynamic: `min(300, CONTENT_WIDTH - 200)` to fit within available horizontal space. Recompute +/- button positions based on dynamic bar width.
- [x] Implemented
- [x] Verified on device

### Issue 2: Diagnostics Page 5/5 — Default App Value Overflow
- **Symptom**: In portrait mode, on diagnostics config page (5/5), the "DEFAULT APP:" value text flows out of the screen boundary.
- **Root Cause**: `draw_info_row()` places the value column at `CONTENT_LEFT + 270`, which in portrait leaves only ~190px for the value text. Long paths like `/opt/roomwizard/app_launcher` overflow.
- **Fix**: Make the value column offset dynamic based on `CONTENT_WIDTH`. In portrait, either reduce the label column width or wrap/truncate the value text. Consider using `CONTENT_LEFT + min(270, CONTENT_WIDTH * 55/100)` for the value column position.
- [x] Implemented
- [x] Verified on device

### Issue 3: Tests Page — Only 4 Tests Visible
- **Symptom**: In portrait mode, only 4 tests are visible (RED LED, GREEN LED, BLINK, COLORS). The remaining 6 tests are not accessible.
- **Root Cause**: Test grid is configured as 5 columns × 2 rows with `item_width=140`, totaling 732px which overflows 460px portrait content width. The `ui_layout_get_item_position()` bounds check rejects items where `x + width > screen_width - margin_right`, so items in columns 3-4 are rejected/not drawn.
- **Fix**: Detect portrait mode and reduce grid columns (e.g., 3 columns × 4 rows or 2 columns × 5 rows). May also need to reduce `item_width` or add scrolling for the extra rows.
- [x] Implemented
- [x] Verified on device

### Issue 4: Calibration Tool — Portrait Coordinate Mapping
- **Symptom**: The calibration tool works in portrait, but produces swapped bezel compensation values. In landscape, correct values are T:10, B:10, L:0, R:0. In portrait, the calibration produces T:0, B:0, L:10, R:10. However, these values are applied in screen native (physical) coordinates, so the portrait calibration values are incorrect.
- **Root Cause**: The calibration phase 2 (bezel margin adjustment) operates in virtual coordinates, but margins are saved and applied in physical coordinates. When calibrating in portrait, the user adjusts what they perceive as "top/bottom" (physical left/right), but the saved margins are labeled as virtual T/B/L/R without rotation mapping.
- **Fix**: Two possible approaches:
  1. **Force landscape calibration**: Temporarily disable portrait rotation during calibration, so the user always calibrates in native orientation. Document that calibration runs in landscape regardless of portrait mode setting.
  2. **Rotate margins on save**: When saving calibration results in portrait mode, apply inverse rotation: virtual_top→physical_left, virtual_bottom→physical_right, virtual_left→physical_top, virtual_right→physical_bottom.
- **Decision**: Deferred — calibration should be performed in landscape mode. The bezel compensation values (T:10, B:10, L:0, R:0) are applied in physical screen coordinates. When calibrating in portrait, the user perceives swapped axes (gets T:0, B:0, L:10, R:10 instead). Since calibration is a one-time setup operation, requiring landscape mode for calibration is the simplest and least error-prone approach. A future enhancement could add a warning in the calibration tab when portrait mode is active, suggesting the user switch to landscape first.
- [ ] Implemented
- [ ] Verified on device

## Phase 7: Portrait Mode Testing & Fixes (Device Tools — Round 2)

**Status**: In Progress

User tested portrait mode on hardware (2026-03-11) and confirmed Phase 6 fixes:
- ✅ Settings tab text and slider layout now fits in portrait
- ✅ Safe area and alpha blending works correctly in portrait orientation
- ✅ Backlight and LED brightness controls fit properly

### Issues Found

#### 1. Portrait Mode Toggle Overlaps Backlight Button
- **Symptom**: In portrait mode, the portrait mode toggle switch partially overlaps with the backlight [-] button in the Settings tab
- **Root Cause**: The Display section vertical spacing doesn't account for the additional portrait toggle element below the backlight slider. The backlight control row and the portrait toggle are placed too close together.
- **Fix**: Increase vertical spacing between the backlight control row and the portrait mode toggle in the Settings tab portrait layout.
- [x] Implemented
- [ ] Verified on device
