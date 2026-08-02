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

**There is no `Makefile`.** There was one; it targeted host `gcc` with ARM flags, could not
compile anything, and was deleted. `build-and-deploy.sh` is the only build path — do not
reintroduce a second one.

Every app links `$COMMON_OBJ` = `framebuffer.o touch_input.o hardware.o common.o highscore.o
keyboard.o audio.o config.o`; games add `gamepad.o`; some add `ui_layout.o ppm.o logger.o`; the two
tools that measure the touch mapping (`device_tools`, `touch_raw`) add `$CALIB_OBJ` =
`touch_calib.o`. Add new objects to `build-and-deploy.sh`.

## The common library

| Module | Use it for | Never do this instead |
|---|---|---|
| `framebuffer.c` | double-buffered draw, sprite blit, the `SCREEN_VISIBLE_*` / `SCREEN_SAFE_*` macros | writing `/dev/fb0` yourself |
| `touch_input.c` | touch events, the raw→panel→logical map, publishing the touch inset | reading evdev directly |
| `touch_calib.c` | measuring that map: targets, fit, verdict, edge sweep, reach→inset, sanity gate, backup | a second copy of the fit or the sweep |
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
  `screen_base_height`, and the `SCREEN_VISIBLE_*` / `SCREEN_SAFE_*` / `LAYOUT_*` macros. Hardcoding
  `400` as the centre-X was historically the single most common bug in this codebase. Pick between the
  two screen rectangles by asking whether the thing has to be *pressed* or only *seen* — see
  *Screen edges* below.
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
- Touch rotates **after** the raw→panel curve, in `scale_coordinates()`.
- Use `fb.portrait_mode` to branch only when the layout must differ fundamentally (3×2 vs 2×3);
  otherwise let the runtime dimensions do the work.
- **Calibrate in landscape.** Calibration in portrait is not supported.

## Screen edges — two rectangles, both handled by the library

The plastic bezel hides a band of panel pixels (~15 px top and bottom on the reference unit).
**Apps never compensate for it.** `fb_init()` sizes the drawing surface to the visible rectangle
and `fb_swap()` places it on the panel at `fb->view_x/view_y`, leaving the hidden bands black.

So `fb.width` / `fb.height`, `screen_base_width` / `screen_base_height` and `SCREEN_VISIBLE_*` are
all the **logical** screen — 800×450 at the shipped 15/15/0/0 margins — and every pixel in it is
visible. Never subtract a bezel margin yourself; that double-corrects.

Margins live on line 2 of `/etc/touch_calibration.conf`, default to `FB_BEZEL_*_DEFAULT`
(15/15/0/0) when absent, and are set from Device Tools → Display → `SCREEN EDGES`.
`fb_set_bezel()` changes them on a live framebuffer (it resizes the back buffer, so re-run any
layout computed from `SCREEN_SAFE_*` afterwards — see `rebuild_ui()` in `device_tools.c`).

**Visible is not the same as touchable.** The digitizer's reading saturates *before* the physical
panel edge — badly on Y, mildly and calibration-dependently on X — so a band at each end is drawable
but unpressable. Hence two macro families:

| Macro family | Means | Use for |
|---|---|---|
| `fb.width` / `fb.height` | drawing surface | all drawing calls, unchanged |
| `SCREEN_VISIBLE_*` | the full logical screen | backgrounds, borders, titles, status/score rows, game playfields, read-only info pages |
| `SCREEN_SAFE_*` | visible **∩** touchable | buttons, toggles, tab bars, touch grids, anything hit-tested |

Measured on RW09 2026-08-01 (the 16:53 reference capture) with `touch_raw`'s SWEEP and INSET modes
(calibration and bezel zeroed, so a drawn pixel is a panel pixel):

| Axis end | raw limit first emitted at panel | flat (saturated) band |
|---|---|---|
| X left | ~0–12 | ~0–12 px |
| X right | ~790–799 | ~0–9 px |
| **Y top** | **~30** | **~30 px** |
| **Y bottom** | **~450** | **~29 px** |

Every edge *does* drive raw to its limit — all 16 sweep buckets on all four edges — but the value is
clipped flat over that band. A bezel press alone **cannot** tell "clipping starts at the edge" from
"clipping starts 30 px inside it", which is why two earlier revisions of this file got it wrong in
opposite directions: first blaming an electrode array "~11 mm shorter than the LCD" (wrong), then
declaring every pixel touchable and a dead band a bug (also wrong). The interior fit, which never
sees an edge sample, independently predicts panel 30 / 450 — same answer, different method.

At bezel T=11 B=14 that capture gives a **~17 px top / ~16 px bottom** logical inset with X ≈ 0. **Do
not treat those digits, or a zero X inset, as properties of the hardware:** the fit is re-run per unit
and per wizard run, and RW09's live calibration (2026-08-01 18:50) publishes `X 6..793 Y 19..438 of
800x455` — i.e. ~6 px on *each* side of X as well, by exactly the same mechanism (fitted endpoints
outside `0..4095`). X's band is much smaller than Y's and can be zero; a non-zero one is the model
working, not a fault. The inset is **measured at runtime,
never hardcoded** — `touch_input.c`'s `publish_safe_area()` pushes the four raw edge extremes
through the production `scale_coordinates()` and calls `fb_set_touch_inset()`, so it is automatically
right in portrait and under any bezel. It is **`0` on a panel whose reach has not been swept**, and
capped at `FB_TOUCH_INSET_MAX` (48 px) with a loud warning. Consequences:

- `SCREEN_SAFE_*` is only correct **after** `touch_init()`. Compute layout after both `fb_init()` and
  `touch_init()` — the documented lifecycle anyway — and re-run it after any `fb_set_bezel()`.
- Read the live numbers from Device Tools → Display → `TOUCHABLE:`, or the wizard's REPORT screen, or
  the `SAFE AREA` page of the display test (red rect = visible, green = touchable).
- The band **stays fully drawable and is good screen area** — a status bar, score row, title or
  background belongs there. Do not shrink the drawing surface or grow the bezel to make visible ==
  touchable. **This is a native_apps policy, not a device-wide one:** `scummvm-roomwizard` and
  `vnc_client` display third-party content nobody can audit for what must be pressable, so they
  confine the *content rectangle itself* to `SCREEN_SAFE_*` and leave the band black. See their own
  `CLAUDE.md` files.
- Not every playfield can move: Tetris' board and Brick Breaker's play area are drawn and collided
  against but never pressed (touch is X-only), so they use `SCREEN_VISIBLE_*`; SameGame's grid *is*
  tapped cell-by-cell, so it stays inside `SCREEN_SAFE_*`.

See [`../SYSTEM_ANALYSIS.md#33-touch`](../SYSTEM_ANALYSIS.md#33-touch) for the numbers and the method.

Minimum comfortable touch target is about 60×40 px, with the 200 ms `BTN_DEBOUNCE_MS`.

## Touch model

Two stages, in `scale_coordinates()`:

```text
raw -> panel    piecewise-linear, 3 segments, knots at panel dim/4 and 3*dim/4  [calibration]
                  raw_min    .. knot_lo  ->  panel      0 .. dim/4    outer
                  knot_lo    .. knot_hi  ->  panel  dim/4 .. 3*dim/4  fitted interior
                  knot_hi    .. raw_max  ->  panel 3*dim/4 .. dim-1   outer
     -> rotate 90° CCW if portrait
     -> logical  = panel - view_origin                                      [bezel viewport]
     -> clamp to the logical screen
```

`touch_map_axis_panel()` is the single implementation, and `touch_calib_predict_*()` delegate to it
so a predicted reach can never disagree with the live mapping. Stage 2 is the exact inverse of what
`fb_swap()` does, which is what keeps touch and drawing in one coordinate system.

**The panel is linear, and hard-clipped at raw 0 and 4095.** That is the model consistent with every
measurement: a single straight line across the panel, with the reading pinned flat over a band inside
each Y edge (~30 px top, ~29 px bottom on RW09 — target 10 at panel y 458 returns raw 4095 on all
three taps because clipping already started around panel 450). **Do not believe "~8.8 raw/px in the
outer band vs ~9.9 interior"** — an earlier revision of this file asserted it from one target;
residuals against the interior line are ±80 raw (≈±8 px, i.e. tap placement) with no consistent sign.

Calibration therefore has two parts. Least squares over 11 targets × 3 taps gives the interior
**line**, and **only targets far from the ends enter it** — ≥100 px on X, ≥80 px on Y
(`touch_calib_interior_x/y()`). Then `touch_calib_curve_from_fit()` turns that line into the curve
that ships: knots on the line at panel ¼ and ¾, and the endpoints **exactly where the line puts
them** (`c->v0 = f->in0`, `c->v1 = f->in1`) — so with the endpoints unclamped the stored
three-segment curve *is* a straight line, and the host regression asserts **zero** deviation. The
three-segment format is kept anyway, because changing line 1's field count is the one edit that
silently breaks a component linking a stale `touch_input.o`. File format is in
`../SYSTEM_ANALYSIS.md`.

⚠️ **The endpoints used to be clamped into `0..4095`, and that was the bug.** A correct fit on this
panel legitimately extrapolates outside the emittable range (the reference Y fit is `-279..4382`),
precisely because the line reaches raw 0/4095 before the panel edge. Clamping asserted "raw 4095 is
emitted at panel 479" when it is emitted at panel ~450, tilting the upper outer segment so the
reported position ran **ahead of the finger by up to +19 px across the bottom quarter**. Both that
clamp and `touch_input.c`'s legacy-migration `clamp_to_hw()` are deleted. `overshoot_lo/hi` survives
as *reporting only*. Never reintroduce either.

**`common/touch_calib.c` is the only implementation of the fit.** It holds the target set, the
interior masks, the per-axis verdict, the reach calculation, the edge-sweep accumulator
(`TouchCalibSweep`, `touch_calib_sweep_*`), the sanity gate and the `.bakN` backup. `device_tools`'
Display wizard and the `touch_raw` diagnostic both link it — which is what lets the diagnostic
validate the code the wizard actually calibrates with. Do not write a second copy; there were three
of the fit and two of the sweep, and they drifted.

After changing it, run the host-side regression. It replays the reference capture's 11 target
medians, asserts the interior fit still lands on `X 17..4084` / `Y -279..4382`, asserts the curve
derived from it reproduces that line with **zero** deviation, and — the assertions that matter —
that the endpoints are **not** clamped (`Y v0 -279 / v1 4382`) and that the per-axis verdict reports
the real dead band (`Y: sensor saturates 28 px inside the low edge, 30 inside the high`):

```bash
gcc -Wall -Wextra -Wno-unused-parameter -I common -o build/touch_calib_test \
    tests/touch_calib_test.c common/touch_calib.c common/touch_input.c \
    common/framebuffer.c common/hardware.c common/config.c -lm && ./build/touch_calib_test
```

It is host gcc, not the cross-compiler, so `build-and-deploy.sh` does not run it.

**Rules that are easy to get wrong:**

| Rule | Why |
|---|---|
| Fit calibration against **panel** coordinates, not logical ones | Targets are drawn in logical space, so add `fb->view_x/view_y` and pass `screen_panel_width/height` as the dim before calling `touch_fit_axis_range()`. Otherwise the bezel is baked into line 1 and stage 2 subtracts it again. |
| **Never clamp a fitted endpoint into the emittable raw range** | The sensor clips before the panel edge, so a correct line legitimately passes raw 0/4095 inside the panel. Clamping fabricates a steeper outer segment and the cursor runs ahead of the finger. Endpoints outside `0..4095` are the measurement, not an error. |
| Derive a slope from the multi-target fit, never from two adjacent near-edge samples | ±80 raw of tap noise over a 10–20 px baseline is nonsense: `touch_raw`'s old INSET report printed "raw reached at panel 594, 614, 817, −4" on a 480-row panel. Near-edge taps are only good for locating *where* the reading goes flat, which is a comparison, not a division. |
| Validate with `touch_map_raw()`, not a private copy of the maths | It runs the production path, so the summary screen cannot drift from what apps actually see. |
| Call `touch_set_screen_size()` (or `touch_set_viewport()`) after any `fb_set_bezel()` | The `TouchInput` caches the panel size and viewport origin. |
| Extend the config file with new **keyword-tagged trailing lines**, never by changing line 1's arity | An old parser reads lines 1–2 positionally and ignores what it does not recognise, so a tagged line 3 is back-compatible by construction. This is how the measured edge reach was added. |
| A UI whose buttons sit on a screen that is itself **sampling edge touches** must keep them clear of all four edge bands, **on both axes** | An edge-sweep collector treats a finger anywhere in the outer sixth of an axis as a sample for that edge. `WIZ_REACH`'s CANCEL at the normal button row (`y=384..440`) is inside the BOTTOM band (`y > H*5/6 == 400`), so pressing CANCEL would record its own tap as a bottom extreme. Its row sits at `y=258`, `x` in `[200,610]` — clear on both axes. `touch_raw` hit the same trap and solved it the same way; if you move either row, re-check both. |
| Report the touch-safe area by reading `screen_touch_inset_*`, never by re-deriving the mapping | `display_touchable_rect()` prints the same globals `SCREEN_SAFE_*` resolves to, so the number on screen cannot disagree with what layouts actually got. It used to re-run the arithmetic from a `TouchInput *`, which is a second copy that can drift; it now takes no `TouchInput *` and returns nothing. |
| No affine / rotation / skew term | Solves a problem the hardware does not have; the extra freedom lets one bad tap rotate the whole mapping. Piecewise-linear *per axis* is fine and is what ships — each axis stays an independent monotone 1-D curve, which is the shape the hardware actually has. |
| No bilinear / 4-corner offsets | Same, and the file format persists four raw values per axis and nothing else, so they would be silently discarded. |
| Knots live at panel dim/4 and 3\*dim/4, and nowhere else | They are implicit in the file format (`TOUCH_KNOT_LO/HI`). Moving them silently reinterprets every stored calibration. |

`touch_enable_calibration()` is a **no-op** — the flag is written but never read, so calibration
cannot be turned off. Other known defects in this area: `../IMPROVEMENT_PLAN.md` B3.

**ScummVM and vnc_client each link their own copy of `touch_input.o`.** The build refreshes it, but a
*deployed* binary keeps whatever it was built with — redeploy **both** after changing touch code, and
especially after changing the calibration file format. A stale binary produces no error: the old
4-number `sscanf` succeeds on the first four of the eight values and accepts them as a legacy config,
so touch silently collapses instead of failing loudly. A stale vnc_client read the current
`0 1020 3074 4095  0 874 3215 4095` as `X [0..1020] Y [3074..4095]`, which confined touch to the left
quarter of the panel.

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
