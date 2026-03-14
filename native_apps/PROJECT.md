# Native Games Project

## Current Status

**System:** Fully functional ✅
- 6 games: Snake, Tetris, Pong, Theremin, Brickbreaker, SameGame
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
- [`common/common.c`](common/common.c) - Unified UI (buttons, screens, safe area, modal dialogs)
- [`common/ui_layout.c`](common/ui_layout.c) - Layouts + ScrollableList widget
- [`common/audio.c`](common/audio.c) - Speaker audio (OSS/TWL4030; beeps, tones, fanfares)
- [`common/config.c`](common/config.c) - Persistent configuration (key=value file, `/opt/games/rw_config.conf`)

**ModalDialog System** ([`common/common.c`](common/common.c) / [`common/common.h`](common/common.h)):

Reusable modal overlay component supporting 1–4 buttons with auto-layout. All 6 games use it for pause menus; `device_tools` uses it for shutdown/reboot confirmation dialogs. The former `screen_draw_pause()` helper was removed (it had zero callers — all games now use ModalDialog directly).

| API | Description |
|-----|-------------|
| `ModalDialog` struct | Modal overlay with title, optional message, and 1–4 configurable buttons |
| `modal_dialog_init(dlg, title, message, button_count)` | Initialize dialog with N buttons (1–4) |
| `modal_dialog_set_button(dlg, index, text, bg_color, text_color)` | Configure individual button text and colors |
| `modal_dialog_init_confirm(dlg, title, message, confirm_text, confirm_color, cancel_text, cancel_color)` | Convenience wrapper for 2-button confirm/cancel dialogs |
| `modal_dialog_show()` / `modal_dialog_hide()` / `modal_dialog_is_active()` | State management |
| `modal_dialog_draw()` | Renders semi-transparent overlay + centered dialog box + text + buttons |
| `modal_dialog_update()` | Touch input handler; returns `ModalDialogAction` — button index (0–3) or -1 for no action |

**Layout rules:** 2 buttons are placed side-by-side; 1, 3, or 4 buttons are vertically stacked. Dialog height is auto-calculated based on button count.

**Unified pause menu pattern** — all 6 games follow this structure:

```c
static ModalDialog pause_dialog;

// Init: 2 buttons (standard) or 4 buttons (brick breaker)
modal_dialog_init(&pause_dialog, "PAUSED", NULL, 2);
modal_dialog_set_button(&pause_dialog, 0, "RESUME", BTN_COLOR_PRIMARY, COLOR_WHITE);
modal_dialog_set_button(&pause_dialog, 1, "EXIT", BTN_COLOR_DANGER, COLOR_WHITE);

// Draw: replaces inline overlay code
modal_dialog_draw(&pause_dialog, &fb);

// Input: returns button index
ModalDialogAction action = modal_dialog_update(&pause_dialog, tx, ty, touching, now);
```

**Brick breaker special case:** uses 4 buttons (RESUME, TEST MODE: ON/OFF, RETIRE, EXIT). The test mode button is a stateful toggle that dynamically updates its text (`"TEST MODE: ON"` / `"TEST MODE: OFF"`) and background color via `modal_dialog_set_button()` each time the mode changes.

**Apps using ModalDialog:**

| App | Use Case | Buttons |
|-----|----------|---------|
| pong | Pause menu | 2: Resume, Exit |
| snake | Pause menu | 2: Resume, Exit |
| tetris | Pause menu | 2: Resume, Exit |
| frogger | Pause menu | 2: Resume, Exit |
| samegame | Pause menu | 2: Resume, Exit |
| brick_breaker | Pause menu | 4: Resume, Test Mode, Retire, Exit |
| device_tools | Confirmation dialogs | 2: Shut Down/Reboot + Cancel |

**Programs:**
- `snake/`, `tetris/`, `pong/`, `samegame/` - Games
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

- [x] **`SCREEN_SAFE_*` macros made dynamic** — [`framebuffer.h`](common/framebuffer.h)/[`framebuffer.c`](common/framebuffer.c) uses runtime globals (`screen_base_width`, `screen_base_height`) instead of hardcoded 800×480. All `LAYOUT_*` macros in [`common.h`](common/common.h) and screen templates in [`common.c`](common/common.c) derive from these — no hardcoded dimensions. (completed)

### Completed (Phase 2 — App-level hardcoded values)

All apps converted from hardcoded 800/480 pixel values to dynamic `fb->width`/`fb->height` equivalents: [`device_tools.c`](device_tools/device_tools.c), [`hardware_test_gui.c`](hardware_test/hardware_test_gui.c), [`game_selector.c`](game_selector/game_selector.c), [`snake.c`](snake/snake.c), [`tetris.c`](tetris/tetris.c). Pong was already fully dynamic. (completed)

### Completed (Phase 3 — Rotation + Game Layout Adaptations)

- [x] **Framebuffer rotation layer** — [`framebuffer.c`](common/framebuffer.c): `fb_is_portrait_mode()` checks `/opt/games/portrait.mode` flag; `fb_init()` swaps `width`↔`height` for virtual 480×800; `fb_swap()` performs cache-friendly 90° CCW rotated copy. (completed)
- [x] **Touch coordinate transform** — [`touch_input.c`](common/touch_input.c): `scale_coordinates()` scales raw→physical, applies calibration, then rotates (virtual_x = phys_h−1−phys_y, virtual_y = phys_x). (completed)
- [x] **Per-game adaptations** — Pong: horizontal paddles top/bottom; Tetris: centered board, compact NEXT preview, touch zones adapted; Snake & Brick Breaker: auto-adapt via `SCREEN_SAFE_*` macros; Game Selector & App Launcher: auto-adapt via `fb.width`/`fb.height`. (completed)

### Key Architectural Note: Touch Calibration Pipeline

`touch_init()` auto-loads calibration from `/etc/touch_calibration.conf` and auto-enables it. The pipeline order is: **raw → scale to physical (800×480) → bilinear calibration → bezel margins → rotate if portrait → clamp**. Apps no longer need to manually call `touch_load_calibration()` + `touch_enable_calibration()`.

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

9. **Verify control spacing with portrait-specific Y offsets** — When `create_*_ui()` functions use `portrait ? valueA : valueB` for Y positions, manually trace the math: compute each element's bottom edge (`y + height`) and ensure no overlap with the next element's top edge. A simple table of element positions during code review catches overlap bugs before they reach hardware testing.

### ⚠️ Common Pitfalls

1. **Hardcoded pixel positions for decorative elements** — Stars, background patterns, and decorative positions often get hardcoded and forgotten. Scale them relative to `fb->width`/`fb->height`.

2. **Touch zones using fixed pixel widths** — Touch zones for game controls (rotate/move) should be percentages of the board/play area, not fixed pixel widths. A zone that feels right at 800px is too narrow at 480px.

3. **UI elements overlapping in portrait** — Elements positioned relative to screen edges (exit button, next-block preview) can collide when the screen is narrower. Always check that elements positioned relative to different edges don't overlap.

4. **Forgetting to recompute layout after app returns** — If your app launches a child process and re-acquires the framebuffer, recompute layout variables. The framebuffer dimensions don't change, but variables derived from them need re-initialization.

5. **Calibration pipeline order matters** — The correct pipeline is: raw → scale to physical (800×480) → bilinear calibration → bezel margins → rotate if portrait → clamp. Applying calibration in the wrong coordinate space produces incorrect offsets.

6. **PPM splash images not rotated** — PPM images are loaded at their native orientation. Portrait mode rotation happens at the framebuffer level (`fb_swap()`), so PPM content displays correctly if drawn to the back buffer. However, images designed for 800×480 will have wrong aspect ratio in portrait — consider loading different images or scaling.

7. **`#define` macros referencing runtime values** — Macros like `#define PAD_H 310` become stale constants. If a dimension depends on screen size, it should either be a macro that references a runtime variable (e.g., `#define PAD_H (SCREEN_SAFE_BOTTOM - PAD_Y - 100)`) or a plain variable.

8. **Vertical spacing between stacked controls in portrait** — In landscape, UI controls (sliders, toggles, buttons) may sit side-by-side with ample horizontal space. In portrait, these often stack vertically, requiring explicit vertical spacing calculations. Always verify that the bottom edge of one control (`element.y + element.height`) has at least 10-15px clearance before the next element's Y position.

## Backlog

### Tooling

- [ ] **Fix `Makefile`** — currently targets plain `gcc`, not the ARM cross-compiler; either wire it to `arm-linux-gnueabihf-gcc` or remove it to avoid confusion

### System Tools

- [ ] **Audio volume control** — add a centralized `audio_volume` setting (0-100) to `rw_config.conf` that scales all audio output. Currently audio is only on/off (`audio_enabled`). The `audio.c` library would need to apply a volume multiplier when generating samples. Add a volume slider to device_tools Settings alongside the existing audio toggle. ScummVM would need its own volume integration.
- [ ] **Screen saver** — add a `screensaver_timeout` setting (minutes, 0=disabled) that dims/blanks the backlight after a period of touch inactivity. Could be implemented as:
  - A feature in `app_launcher.c` that monitors touch activity and dims backlight after timeout
  - Touch event wakes the screen back up (set backlight to configured brightness)
  - Show a simple animation (clock, bouncing logo, or just blank) to prevent burn-in
  - The timeout and enable/disable should be configurable via device_tools Settings

### Gameplay

- [x] **Audio wired into all games** (completed) — `audio_init/close` in each game's `main()`; snake: `audio_beep` on direction change, `audio_blip` on food, `audio_fail` on death; tetris: `audio_beep` on move/rotate, `audio_blip` on line clear, `audio_success` on Tetris (4 lines), `audio_fail` on game over; pong: `audio_beep` on paddle hit, `audio_blip` on scoring, `audio_success`/`audio_fail` on win/loss
- [ ] **Sprite-based game (e.g. Frogger)** — character crosses lanes of traffic; needs sprite blitting helpers in `framebuffer.c` (masked blit), game logic, and multi-lane scrolling
- [ ] **More games** — Brick Breaker (ball + paddle + bricks), Space Invaders (scanline sprites, wave progression)
- [x] **Add Game: SameGame** (completed) — Puzzle game with colored block groups, gravity, column collapse, (n-1)² scoring, animations, screen shake, audio effects. See [`samegame/samegame.c`](samegame/samegame.c).

### Touch Calibration

- [ ] **Calibration prompt on first boot** — detect missing `/etc/touch_calibration.conf` at game_selector startup and offer to run calibration before showing the menu
- [ ] **Calibration in portrait mode** — Currently deferred. Calibration should be performed in landscape mode since bezel compensation values are in physical screen coordinates. A future enhancement could add a warning in the calibration tab when portrait mode is active, suggesting the user switch to landscape first.

---

**ScummVM touch patterns:** See [`../scummvm-roomwizard/SCUMMVM_DEV.md`](../scummvm-roomwizard/SCUMMVM_DEV.md#gesture-navigation).

## Completed Phase History

> The following phases have been completed and verified on device. Details retained for historical context; see git history for full implementation notes.

- **Phase 5: Portrait Mode Polish & UI Unification** (2026-03-11) — 15 issues fixed: bezel compensation rotation for portrait, unified game-over screen with highscore/exit/transparent overlay, ball speed scaling, text centering across all games, score positioning, screen area utilization, Device Tools tab scrolling/label/apply-on-save, pause menu transparency, game state guards for start screens. All verified deployed on device.

- **Phase 6: Device Tools Portrait Fixes** (2026-03-11) — Settings tab text/slider overflow fixed (dynamic `BAR_WIDTH`), diagnostics value column overflow fixed (dynamic offset), tests page grid adapted for portrait (2-col layout).

- **Phase 7: Device Tools Portrait Fixes Round 2** (2026-03-11) — Portrait mode toggle / backlight button overlap fixed (vertical spacing adjustment).
