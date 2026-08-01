# native_apps — authoring guide

Guidance for writing code in this directory. Device facts live in
[`../SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md); open bugs and planned work live in
[`../IMPROVEMENT_PLAN.md`](../IMPROVEMENT_PLAN.md); what each app *is* lives in
[`README.md`](README.md). This file is only about **how to write correct code here**.

`native_apps/` = a shared C library (`common/`) + games + on-device tools + the launcher.

## Build

```bash
./build-and-deploy.sh                          # build only
./build-and-deploy.sh <ip>                     # build + deploy
./build-and-deploy.sh <ip> set-default         # + make it the boot app
```

`set-default` is the only mode this script accepts. Cleanup and boot-service install live in
`../setup-device.sh`.

All targets compile with `-Wall -Wextra -Wno-unused-parameter` and the tree is currently at
**zero warnings** — keep it there, so a new warning means a new problem. Not `-Werror`, so a
warning will not block a deploy; that is your job.

Every build (deploy or not) then runs `./check-arm-safe.sh`, which rejects any binary containing a
hardware `sdiv`/`udiv` — the Cortex-A8 has no integer divide and would SIGILL with a blank screen.
The expected count is a hard zero. Run it standalone on any binary: `./check-arm-safe.sh <path>`.

**`make` does not work.** `Makefile` targets host `gcc` with ARM flags and cannot compile
anything; several of its rules point at files that moved. `build-and-deploy.sh` is the only
build path. (Slated for deletion — `../IMPROVEMENT_PLAN.md` B16.)

Every app links `framebuffer.o touch_input.o hardware.o common.o highscore.o keyboard.o
audio.o config.o`; games add `gamepad.o`; some add `ui_layout.o ppm.o logger.o`. See
`build-and-deploy.sh:59`. Add new objects there, not to the Makefile.

## The common library

| Module | Use it for | Never do this instead |
|---|---|---|
| `framebuffer.c` | double-buffered draw, sprite blit, safe-area macros | writing `/dev/fb0` yourself |
| `touch_input.c` | touch events, calibration | reading evdev directly |
| `gamepad.c` | **all** input: touch + USB keyboard/mouse + Xbox pad → abstract buttons | per-app evdev scanning |
| `hardware.c` | LEDs, backlight | writing `/sys/class/leds/*` |
| `common.c` | buttons, `ModalDialog`, safe-area screens, `acquire_instance_lock()` | hand-rolled widgets |
| `ui_layout.c` | grid/list layout, `ScrollableList` | manual pixel arithmetic |
| `audio.c` | beeps, tones, streaming | opening `/dev/dsp` yourself |
| `config.c` | `/opt/games/rw_config.conf` | ad-hoc config files |
| `keyboard.c` | on-screen keyboard (ALPHA / ALPHANUM / FULL / NUMERIC) | — |
| `highscore.c`, `ppm.c`, `logger.c` | scores, icons, logging | — |

`keyboard_enter(fb, touch, "Title", buf, max_len, KB_LAYOUT_ALPHA)` — `buf` must hold
`max_len + 1` bytes.

`ModalDialog`: `modal_dialog_init()` → `modal_dialog_set_button()` per button →
`modal_dialog_draw()` after all other content but before `fb_swap()` → `modal_dialog_update()`
returns `MODAL_ACTION_BTN0..BTN3` or `-1`. Two buttons render side-by-side; 1, 3 or 4 stack.
While a dialog is active, route **all** input to it and skip every other handler.

## App lifecycle

```c
int main(int argc, char *argv[]) {
    /* 1. Parse args (fb_device, touch_device) */
    /* 2. Singleton guard */
    int lock_fd = acquire_instance_lock("my_app");
    if (lock_fd < 0) return 1;

    /* 3. Signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 4. Hardware init + apply configured backlight */
    hw_init();
    hw_set_backlight(100);      /* 100 = 100% OF THE CONFIGURED MAX, not raw 100 */

    /* 5. Audio (non-fatal if /dev/dsp is unavailable) */
    Audio audio;  audio_init(&audio);

    /* 6. Framebuffer BEFORE touch - touch_init() reads the screen dims fb_init() sets */
    Framebuffer fb;    fb_init(&fb, fb_device);
    TouchInput  touch; touch_init(&touch, touch_device);

    /* 7. gamepad_init() BEFORE registering any TouchRegion - it memsets the manager */
    GamepadManager gp; gamepad_init(&gp);
    /* ...register touch regions here... */

    /* 8. Main loop */
    while (running) { /* ... */ }

    /* 9. Cleanup, reverse order */
    hw_leds_off();
    hw_set_backlight(100);      /* restore for the next app */
    audio_close(&audio);
    gamepad_close(&gp);
    touch_close(&touch);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    fb_close(&fb);
    return 0;
}
```

Two ordering rules in there are load-bearing and have both been violated in shipped code:

- **`fb_init()` before `touch_init()`** — `touch_init()` reads `screen_base_width/height`,
  which `fb_init()` sets. Reversed, portrait mode silently gets 800×480 instead of 480×800.
- **`gamepad_init()` before registering `TouchRegion`s** — it `memset`s the manager and zeroes
  `touch_region_count`. Snake gets this backwards, which is why its virtual D-pad is dead code.

## Hardware API rules

1. **LEDs: `hw_set_led()` / `hw_set_leds()`.** Never write `/sys/class/leds/` directly — these
   respect the `led_enabled` and `led_brightness` config.
2. **Backlight: `hw_set_backlight()`.** It scales by the configured `backlight_brightness`
   percentage, so `hw_set_backlight(100)` means "the user's chosen maximum".
3. **Call `hw_set_backlight(100)` at both startup and exit.** Startup so the app matches the
   user's preference; exit so the next app inherits a sane value. `snake.c` misses the exit
   call and `app_launcher.c` misses both — do not copy them.
4. **`hw_leds_off()` deliberately bypasses config** and always writes 0, so cleanup works even
   when LEDs are disabled.
5. Direct sysfs writes are acceptable **only** in test functions that intentionally exercise
   raw hardware.

**Config cache is per-process.** `hardware.c` caches config on the first `hw_set_*()` call to
avoid re-reading during animations. A `fork()`/`exec()` child gets a fresh cache, so launched
apps always see current settings — but a long-running parent (the launcher) must call
`hw_reload_config()` after a child that may have changed settings exits. Config changes never
propagate into a running process on their own.

## Rendering: dirty flag + adaptive sleep

600 MHz, no GPU. Unconditional 60 fps redraws burn 40 %+ CPU. Track `needs_redraw`, draw only
when set, and sleep longer when idle — this alone takes a static UI from ~40 % to under 5 %.

Static UI (launchers, menus, settings): draw only on state change.
Games: dirty-flag the non-playing states (welcome, paused, game-over); render every frame
during actual gameplay.

```c
bool needs_redraw = true;               /* first frame always draws */

while (running) {
    int old_selected = state.selected;

    touch_poll(&touch);
    gamepad_poll(&gp);
    /* ...handle input, update state... */

    if (state.selected != old_selected /* || other visible change */)
        needs_redraw = true;

    bool drew = needs_redraw;           /* capture BEFORE clearing */
    if (needs_redraw) {
        draw_screen(&state);
        fb_swap(&fb);
        needs_redraw = false;
    }

    usleep(drew ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US);
}
```

The `bool drew` matters. Testing `needs_redraw` in the `usleep` after clearing it inside the
`if` always yields the idle delay, pinning the app to 10 fps — that is a real shipped bug
(`../IMPROVEMENT_PLAN.md` B13c). Either capture it first, as above, or clear the flag *after*
the `usleep`.

`FRAME_DELAY_ACTIVE_US` = 33 333 (~30 fps), `FRAME_DELAY_IDLE_US` = 100 000 (~10 fps), both in
`common/common.h`. Use them; don't hardcode `usleep()` values. Reference implementation:
`app_launcher/app_launcher.c`.

## Coordinates, dimensions, portrait

- **Never hardcode 800, 480, or 400.** Use `fb.width` / `fb.height`, `screen_base_width` /
  `screen_base_height`, and the `SCREEN_SAFE_*` / `LAYOUT_*` macros. Hardcoding `400` as the
  centre-X was historically the single most common bug in this codebase.
- **Compute layout at init, not compile time.** Replace `#define`d grid/tile constants with
  runtime values computed after `fb_init()`. See `compute_grid_layout()` in `app_launcher.c`
  and `init_brick_layout()` in `brick_breaker.c`.
- **Scale element counts to available area** (`count = space / (size + padding)`), don't fix
  them for one orientation.
- **Touch and drawing must share a coordinate space.** Call `touch_set_screen_size()` with the
  virtual dimensions. A mismatch produces the "cursor runs ahead of the finger" symptom.
- **Guard game logic by state** — ball physics, AI and scoring must not run on the welcome
  screen.

Portrait mode (flag file `/opt/games/portrait.mode`):

- `fb_init()` swaps width/height and rotates the safe margins. `fb_swap()` does a 90° CCW
  rotated copy; landscape is a straight `memcpy`.
- Touch rotates **after** the linear map, in `scale_coordinates()`.
- Use `fb.portrait_mode` to branch only when the layout must differ fundamentally (3×2 vs 2×3);
  otherwise let the runtime dimensions do the work.
- **Calibrate in landscape.** Calibration in portrait is not supported.

## Screen edges — the bezel is already handled

The plastic bezel hides a band of panel pixels (~15 px top and bottom on the reference unit).
**Apps never compensate for it.** `fb_init()` sizes the drawing surface to the visible rectangle
and `fb_swap()` places it on the panel at `fb->view_x/view_y`, leaving the hidden bands black.

So `fb.width` / `fb.height`, `screen_base_width` / `screen_base_height` and `SCREEN_SAFE_*` are
all the **logical** screen — 800×450 at the shipped 15/15/0/0 margins — and every pixel in it is
visible. `SCREEN_SAFE_LEFT`/`TOP` are `0` and `SCREEN_SAFE_RIGHT`/`BOTTOM` are the logical size:
the logical screen *is* the safe area. Never subtract a margin yourself; that double-corrects.

Margins live on line 2 of `/etc/touch_calibration.conf`, default to `FB_BEZEL_*_DEFAULT`
(15/15/0/0) when absent, and are set from Device Tools → Set Screen → `SCREEN EDGES`.
`fb_set_bezel()` changes them on a live framebuffer (it resizes the back buffer, so re-run any
layout computed from `SCREEN_SAFE_*` afterwards — see `rebuild_ui()` in `device_tools.c`).

The remaining constraint is digitizer reach, and it is **not** symmetric: measured 2026-07-31 with
`touch_raw`, the sensor reaches past both the left and right panel edges but stops ~30 panel px
short at the top and bottom. In the logical coordinates you draw in, that means:

| Edge | Reachable? |
|---|---|
| left, right | **yes, fully** — x 0…799 is touchable |
| top, bottom | **no** — the first ~15 and last ~15 rows of the 450 cannot be touched |

So the logical screen is the safe area for *drawing*, but for *touch* it is 15 px inset top and
bottom. Keep interactive targets out of those two bands; decoration can go to the edge. Minimum
comfortable touch target is about 60×40 px, with the 200 ms `BTN_DEBOUNCE_MS`.

This is a sensor property (the electrode array is ~11 mm shorter than the LCD), not a calibration
error, so no calibration recovers it. The older "~10 px left/right" figure *was* a calibration
error — see [`../SYSTEM_ANALYSIS.md#33-touch`](../SYSTEM_ANALYSIS.md#33-touch) for the numbers and
the method.

## Touch model

Two stages, in `scale_coordinates()`:

```text
raw -> panel    = (raw - raw_min) * (panel_dim - 1) / (raw_max - raw_min)   [calibration]
     -> rotate 90° CCW if portrait
     -> logical  = panel - view_origin                                      [bezel viewport]
     -> clamp to the logical screen
```

The panel is **linear** — a `touch_trace` capture confirms a traced border is a straight-edged
rectangle — so stage 1 is a plain per-axis scale+offset. Stage 2 is the exact inverse of what
`fb_swap()` does, which is what keeps touch and drawing in one coordinate system.

Calibration fits `raw_min`/`raw_max` per axis by least squares over 9 crosshair taps (inset 40 px
for reliable capture) and extrapolates to the true panel edges. File format is in
`../SYSTEM_ANALYSIS.md`.

**Rules that are easy to get wrong:**

| Rule | Why |
|---|---|
| Fit calibration against **panel** coordinates, not logical ones | Targets are drawn in logical space, so add `fb->view_x/view_y` and pass `screen_panel_width/height` as the dim before calling `touch_fit_axis_range()`. Otherwise the bezel is baked into line 1 and stage 2 subtracts it again. |
| Validate with `touch_map_raw()`, not a private copy of the maths | It runs the production path, so the summary screen cannot drift from what apps actually see. |
| Call `touch_set_screen_size()` (or `touch_set_viewport()`) after any `fb_set_bezel()` | The `TouchInput` caches the panel size and viewport origin. |
| No affine / rotation / skew term | Solves a problem the hardware does not have; the extra freedom lets one bad tap rotate the whole mapping. |
| No bilinear / 4-corner offsets | Same, and `touch_save_calibration()` only persists the raw range, so they would be silently discarded. |

`touch_enable_calibration()` is a **no-op** — the flag is written but never read, so calibration
cannot be turned off. Other known defects in this area: `../IMPROVEMENT_PLAN.md` B3.

**ScummVM links its own copy of `touch_input.o`.** The build refreshes it, but a *deployed*
ScummVM keeps whatever it was built with — redeploy ScummVM after changing touch code.

## Input

Use `gamepad.c` for everything; don't scan evdev per-app. Abstract buttons are
`BTN_ID_UP..BTN_ID_BACK`, configurable via `/etc/input_config.conf`.

`.pressed` is a rising edge, `.held` is level. Prefer `.pressed` for menu navigation and
discrete actions; `.held` for continuous movement. Note `.held` is currently **never cleared**
for touch regions and analog-stick directions (`../IMPROVEMENT_PLAN.md` B2), so until that is
fixed a virtual D-pad latches on permanently.

Every app should handle `BTN_ID_BACK` as "exit / back". Platformer does not, which leaves its
game-over screen with no way out.

## 32-bit target

`sizeof(long) == 4`. Never write `tv_sec * 1000000L` — baseline timers to a start timestamp
captured at init, not to epoch 0, and do the multiply in `uint32_t` or `int64_t`.
