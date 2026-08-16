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
`../commissioning/provision.sh`.

**ARM dependencies are built by `./build-deps.sh` into `arm-deps/` (gitignored), and
`build-and-deploy.sh` §1b calls it itself**, guarded on the artifact `arm-deps/lib/libtinyalsa.a` — never
on a flag, which goes stale. So a fresh clone deploys without a manual prerequisite step, which matters
because `../deploy-all.sh` drives this script unattended. Run it by hand only when iterating on the dep
itself; `--force` rebuilds. Today it builds exactly one library, **tinyalsa 2.0.0** (pinned), for the
native-ALSA audio backend (`../IMPROVEMENT_PLAN.md` F1).

Three rules live in that script's comments and are repeated here only as pointers, because getting any
of them wrong is silent:

- ⚠️ **Compile five of upstream's eight sources.** `snd_card_plugin.c` `dlopen()`s and these binaries are
  `-static` — the same family as the `clock_gettime64` SIGSEGV-before-`main()` trap. The plugin path is
  dead code anyway (`#ifdef TINYALSA_USES_PLUGINS`, never defined) and the three files add warnings to a
  zero-warning tree. `assert_no_dl()` refuses the build rather than shipping that.
- ⚠️ **That subset needs the one-line `pcm_close()` patch** the script applies and asserts:
  `src/pcm.c:978` calls `snd_utils_close_dev_node()` **outside** the `#ifdef` guarding its four siblings,
  and it is **still ungated on upstream master** — so do not expect a version bump to retire the patch.
  Gating it is behaviour-identical and measured so: `struct pcm` is `calloc`'d and `pcm->snd_node` written
  only inside that `#ifdef`, so the argument is always `NULL` and upstream returns immediately on `NULL`.
  Without the patch the archive builds, passes `nm -u` *and* the ARM gate, and fails only at link — which
  is what `assert_links()` is for.
- ⚠️ **The ARM-safety gate runs on `libtinyalsa.a` inside `build-deps.sh`**, because nothing in `build/`
  links it yet. It does disassemble every archive member (measured), but its `checked=1` is a file count.

⚠️ **No vendored ALSA header is needed, and that is measured rather than assumed.** The cross
toolchain's `sound/asound.h` is **ABI-identical** to the device kernel's — `SNDRV_PCM_VERSION` 2.0.14 and
`SNDRV_CTL_VERSION` 2.0.7 in both, and all 172 diff lines against
`../usb_host/linux-4.14.52/include/uapi/sound/asound.h` are `__user`/`__force` annotations, guard names
and one `#include <time.h>`. This matters because **ALSA ioctl numbers embed `sizeof(struct)`**: a struct
that had grown between 4.14.52 and the toolchain's headers would compile cleanly here and return
`-ENOTTY` on the device. Re-check it if the toolchain is ever upgraded; do not vendor a header to avoid
checking.

**Never build a second copy of a dependency that lives here.** ScummVM will point at
`../native_apps/arm-deps` for tinyalsa, the same way it already links `native_apps/common/framebuffer.o`
and `touch_input.o`. zlib is built twice in this repo and `../LICENSE.md` has to carry both versions as a
result — one pin, one licence row.

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
keyboard.o audio.o audio_gen.o config.o`; games add `gamepad.o`; some add `ui_layout.o ppm.o
logger.o`; the two tools that measure the touch mapping (`device_tools`, `touch_raw`) add
`$CALIB_OBJ` = `touch_calib.o`. Add new objects to `build-and-deploy.sh`. `audio_gen.o` is not
optional — `audio.c` calls into it for every frame count, byte count, envelope and write.

**A new *binary* goes in `GAMES_BINARIES` and nowhere else.** That one array drives the upload, the
remote `chmod +x` (passed in as `"$@"` through `ssh bash -s --`) and the md5 verification. It used to
be two hand-written lists, and `audio_touch_test` was missing from the `chmod` one — harmless only
because scp happens to carry the source file's mode, and invisible on a Windows host because `/mnt/c`
is a DrvFs mount that reports every file executable and discards `chmod` outright. Do not add a
second list.

**The deploy verifies itself.** After `chmod`, every executable is md5-compared against `build/` and a
mismatch is fatal with a per-file diff — a truncated scp, a full filesystem, or a surviving process
holding an old inode (the stale-binary failure mode below) otherwise all look like a clean deploy.
`./build-and-deploy.sh <ip>` also validates the IP and the mode *before* compiling anything, and it
`cd`s to its own directory, so it can be invoked by path.

## The common library

| Module | Use it for | Never do this instead |
|---|---|---|
| `framebuffer.c` | double-buffered draw, sprite blit, the `SCREEN_VISIBLE_*` / `SCREEN_SAFE_*` macros | writing `/dev/fb0` yourself |
| `touch_input.c` | touch events, the raw→panel→logical map, publishing the touch inset | reading evdev directly |
| `touch_calib.c` | measuring that map: targets, fit, verdict, edge sweep, reach→inset, sanity gate, backup | a second copy of the fit or the sweep |
| `gamepad.c` | **all** input: touch + USB keyboard/mouse + Xbox pad → abstract buttons | per-app evdev scanning |
| `hardware.c` | LEDs, backlight, non-blocking `LedPulse` | writing `/sys/class/leds/*`, or a `usleep()` LED loop |
| `common.c` | buttons, `ModalDialog`, `GameOverScreen`, safe-area screens, `acquire_instance_lock()` | hand-rolled widgets |
| `ui_layout.c` | grid/list layout, `ScrollableList` | manual pixel arithmetic |
| `audio.c` | beeps, tones, streaming, the per-frame mix pump | opening `/dev/dsp` yourself |
| `audio_gen.c` | the audio logic with no device in it: frame/byte arithmetic, the tone envelope, the one gliding oscillator, the mix bus, mono→interleaved, the frame-aligned write loop | a second sine loop, a `frames * 4` with the channel count spelled into the constant, or an audio thread |
| `config.c` | `/opt/games/rw_config.conf` | ad-hoc config files |
| `keyboard.c` | on-screen keyboard (ALPHA / ALPHANUM / FULL / NUMERIC) | — |
| `highscore.c`, `ppm.c`, `logger.c` | scores, icons, logging | — |

`keyboard_enter(fb, touch, "Title", buf, max_len, KB_LAYOUT_ALPHA)` — `buf` must hold
`max_len + 1` bytes.

**`fb_draw_text()` does not interpret `'\n'`.** A newline takes the unprintable-character branch and
just advances 6·scale px, so a multi-line string renders as one long line. Anything with embedded
newlines must be split per line by the caller — `screen_draw_welcome*()` does this now, and until
2026-08-02 it did not, which is why every game's welcome text was one 500 px line centred on the wrong
width.

`screen_draw_welcome(fb, title, instructions, start_btn)` / `screen_draw_welcome_warn(…, warning, …)`
**position `start_btn` as well as drawing it** — below the measured instruction (and warning) block,
centred in and clamped to `SCREEN_SAFE_*`. So the drawn rect and the hit-test rect are one
computation, and a caller's `button_init()` coordinates for the start button are only a fallback for a
hit-test arriving before the first draw; use `LAYOUT_CENTER_X` / `LAYOUT_BOTTOM_BTN_Y` there, not a
`fb.height / 2 + 40`-style literal. `instructions` and `warning` may both contain `'\n'`; the warning
block renders amber and exists for "this game needs a controller and none is connected".

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

    /* 6. Framebuffer BEFORE touch - touch_init() reads the screen dims fb_init() sets.
     *    Pin the depth first: /dev/fb0 keeps whatever the last process set. */
    fb_set_bpp(fb_device, 32);
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
  `touch_region_count`. Snake had this backwards, so its virtual D-pad was dead code; those regions
  are now deleted outright and nothing in the tree registers any, so this rule currently has
  no live example — get it right in the app that revives it.

**`app_launcher` launches children with `argv[0]` = `exec_path`, not the manifest's `name=`.** The
display name is for the grid, not for the process table: passing it made `/proc/<pid>/cmdline` read
`VNC Client` while the binary was `/opt/vnc_client/vnc_client`, and since busybox `ps w` shows nothing
without a TTY, that cmdline was the only handle left on a launcher-started app — it cost a session and
a misdiagnosis. Do not restore the pretty name. Killing was never the
problem: `comm` comes from the file being executed, so it always read `vnc_client`
(`../SYSTEM_ANALYSIS.md#53-app-launcher-and-manifests`).

## Pixel format — pin it, and never assume it

`/dev/fb0`'s depth is **global mutable state**: it is whatever the last process to run set it to.
The native menus, tools and games all pin 32bpp XRGB8888; **ScummVM and `vnc_client` set 16bpp
RGB565 on purpose**, to halve write bandwidth on this memory-bound part, and they are allowed to —
`fb_init()` must keep accepting 16bpp.

So there are two rules, and until 2026-08-03 the codebase broke both:

- **Every app calls `fb_set_bpp(dev, 32)` before `fb_init()`.** No game did, so a game launched over
  SSH after a VNC session inherited 16bpp. Every `fb_init()` call site in this tree now pins the depth
  first — `grep -n 'fb_set_bpp\|fb_init(&' */*.c tests/*.c` is the check, and every `fb_init(&` must
  have an `fb_set_bpp` immediately above it. The reason to pin is no longer safety — it is that 16bpp
  bands every gradient, and how an app looks must not depend on what ran before it.
- **The primitives dispatch on `fb->bytes_per_pixel`.** They used to write `uint32_t`
  unconditionally, which at 16bpp overran the back buffer — sized `w * h * bpp` — by exactly 2×.
  Four helpers in `framebuffer.c` (`fb_pack565` / `fb_unpack565` / `fb_store` / `fb_load`) are
  the only code that knows the format; **the API is unchanged, callers always pass RGB888**. If you
  add a primitive, go through `fb_store()`/`fb_load()` — do not index `back_buffer` as `uint32_t`.

Two subtleties worth not rediscovering: a sprite's **colour key is compared in the source's 32-bit
space, before packing** (two RGB888 colours can share one RGB565 word, so a key tested after packing
turns opaque pixels transparent), and `fb_unpack565` **replicates high bits rather than dividing**,
because a `/31` in the alpha inner loop is a call into `__aeabi_uidiv` on this core.

Covered by `tests/framebuffer_bpp_test.c` (host gcc, build line in its header): guard bytes after a
16bpp back buffer turn an overflow into an assertion, all 17 primitives are driven over the whole
surface *including its last pixel*, and the same sweep runs again at 32bpp so a fix cannot regress the
depth every app actually uses. It failed 29 assertions against the pre-fix file before being trusted.

⚠️ **When capturing `/dev/fb0`, run `fbset | grep geometry` first.** A 32bpp frame and two 16bpp
pages are both 1,536,000 bytes, so a wrong `--bpp` decodes garbage at exactly the right size.

## Hardware API rules

1. **LEDs: `hw_set_led()` / `hw_set_leds()`.** Never write `/sys/class/leds/` directly — these
   respect the `led_enabled` and `led_brightness` config.
2. **Backlight: `hw_set_backlight()`.** It scales by the configured `backlight_brightness`
   percentage, so `hw_set_backlight(100)` means "the user's chosen maximum". The one exception is a
   settings slider *previewing* a value — it is choosing that percentage, so scaling by the outgoing
   one shows the wrong brightness. That is what `hw_set_backlight_raw()` is for, and it exists so a
   preview does not carry its own copy of the sysfs path: two of them did, both naming a node that
   does not exist on this device, so both previews silently did nothing.
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
`if` always yields the idle delay, pinning the app to 10 fps — that **was** a real shipped bug, in
`samegame.c` (fixed 2026-08-02). Either capture it first, as above, or
clear the flag *after* the `usleep`.

**A component whose `update()` both draws and reads input has to tell the caller when it still needs
frames.** The dirty flag is computed by the loop from things the loop can see — state changes and input
— so a component with internal states of its own is invisible to it and gets starved. That shipped:
`gameover_update()` is a `CHECK` → (`NAME_ENTRY`) → `DISPLAY` machine in which **only `DISPLAY`
draws**, so entering game over drew the playfield, ran `CHECK`, returned without drawing, and then
nothing redrew until the player tapped — the game-over overlay and the high-score keyboard both needed
a tap to appear. The component now exposes `gameover_needs_redraw()`
and every game ORs it in:

```c
if (current_screen == SCREEN_GAME_OVER && gameover_needs_redraw(&gos))
    needs_redraw = true;
```

Two corollaries, both learned from the same bug:

- **Do not fix it in the caller.** `samegame.c` did — an unconditional redraw while `SCREEN_GAME_OVER`
  — which cured samegame, left the other six games broken, and pinned a static overlay to 30 fps.
  Ask the component; it is the only thing that knows.
- **Making the first frame appear on the transition frame puts the press that caused the transition in
  front of the new screen's buttons.** `TouchState.pressed` is a rising edge that survives until the
  next `touch_poll()`, and a draw-then-check-input component draws before it reads. So the component
  must ignore input on its own first drawn frame (`GameOverScreen.armed`); brick_breaker's pause-dialog
  `RETIRE` overlaps the game-over `RESET SCORES` by 21 px, so without that guard retiring could wipe
  the high-score table on a screen nobody had seen.
- **But do not make every state fall through — `NAME_ENTRY` deliberately keeps its `return`.**
  `gameover_update()`'s `CHECK` state falls through to `NAME_ENTRY`/`DISPLAY` in the same call, so the
  no-highscore path costs no extra frame. `NAME_ENTRY` must not: `hs_enter_name()` is a **blocking**
  keyboard that repaints and swaps the framebuffer itself, so drawing the overlay in the same call
  would composite it over the keyboard's last frame. It leaves `pending_draw` set and takes one clean
  frame with the playfield redrawn underneath. This reads like an oversight if you only know the
  fall-through rule — it isn't.

**And the corollary that cost a wedged app: the predicate must cover pending *input*, not just a
pending draw.** If the component reads its buttons inside the draw path, then a frame the loop
declines to run is also **an input event the component never sees** — so a predicate that only
reports "I owe a frame" goes quiet the moment the screen settles and the buttons die. Do not rely on
the caller having an input-activity `else` branch of its own: samegame's dirty flag is a pure
visible-state diff and a tap on an overlay changes none of it, so its game-over screen drew correctly
and was then completely unresponsive — all three buttons dead, no other handler on that screen, only
killable over SSH. `gameover_needs_redraw()` returns true on three grounds, each load-bearing:

| Ground | Why |
|---|---|
| `pending_draw` | the multi-frame machine owes the screen a frame |
| `ts.pressed \|\| ts.held` | there is input to act on, and only a drawn frame lets us act on it |
| any button's `was_pressed` | `button_check_press()` clears that latch only on a frame where the button is **not** touched. At `FRAME_DELAY_IDLE_US` a press and its release can both land in one `touch_poll()`, so there may be no `held`/`released` frame at all — and then the *next* press is silently eaten. Ask the buttons, not the touch state |

Idle still costs nothing: with no finger down and nothing latched, all three are false and the
static overlay produces no frames — which was the point of asking the component in the first place.

`FRAME_DELAY_ACTIVE_US` = 33 333 (~30 fps), `FRAME_DELAY_IDLE_US` = 100 000 (~10 fps), both in
`common/common.h`. Use them; don't hardcode `usleep()` values. Reference implementation:
`app_launcher/app_launcher.c`.

**The only `usleep()` in a game is the one at the bottom of the main loop.** Anything else in an
update or input path stops the world: no `touch_poll()`, no `gamepad_poll()`, no redraw. Every game
had at least one and they were fixed on 2026-08-03. Two shapes, and
both have a replacement:

| Shape | Was | Use instead |
|---|---|---|
| "flash an LED for N ms" | `hw_set_led(); usleep(); hw_leds_off();` — up to 1.2 s in tetris and pong | `LedPulse` + `hw_led_pulse_start/update/stop()` in `hardware.c` |
| "hold this screen for N ms" | `while (...) { draw; fb_swap; usleep; }` — samegame's 300–1500 ms pre-game-over hold | a state in the app's existing animation machine (`ANIM_GAMEOVER_DELAY`) |

Three things about that, learned from doing it:

- **`hw_led_pulse_update()` takes `now_ms` as an argument rather than calling `get_time_ms()`.**
  `hardware.c` is linked by `vnc_client`, which does **not** link `common.c`, so a call into it breaks
  that link. Same reasoning as `gamepad_poll()`'s touch coordinate — and it makes the pulse
  host-testable. `hw_blink_led()`/`hw_pulse_led()` are still there and still blocking; they are
  correct for exactly one case, the exit flourish before `running = false`, where there is nothing
  left to be responsive to.
- **The render-loop shape is the one that hides a dead button.** samegame's held the screen without
  polling, so MENU and EXIT were dead for its whole duration. As an animation state it costs nothing:
  the dirty flag already forces frames on `anim_state != ANIM_NONE`, and `handle_input()` checks the
  buttons *before* it gates the grid on the animation. Check that order if you add one.
- **A pulse outlives the state that started it, so whoever can leave that state must cancel it.**
  `reset_game()` / `init_level()` call `hw_led_pulse_stop()`, or a game-over flourish flashes into the
  next round. If a code path writes the LED directly (`hw_leds_off()` at brick_breaker's level-clear),
  stop the pulse first or the next update re-lights it.

**A per-frame motion constant is a speed only in combination with the frame delay, so sanity-check it
in px/s.** At 30 fps the conversion is ×30, which makes small-looking numbers unplayably slow: pong
served every ball at `5.0` px/frame — 150 px/s, and only ~3.5 px/frame along the long axis at a 45°
serve, so **~7 s to cross the playfield**. It read as a perfectly ordinary constant in source, and
`BALL_START_SPEED` carries the arithmetic in its comment now.
Two rules follow. Multiply by 30 and ask whether you'd enjoy that speed before you commit the number.
And never count *frames* where you mean *time* — a dirty-flagged loop's frame rate varies with what the
app is doing, so a per-iteration counter runs at whatever pace the screen happens to need; that was
tetris' gravity, which fell ~3× too slow while idle and sped up when a key was held. Motion
tied to a fixed physics step (a ball's `vx`) may live in px/frame; anything the player experiences as a
duration wants a `get_time_ms()` delta.

**Derive state; don't accumulate it, and don't let a marker mean two things.** Three separate game
bugs were one shape — a multiplier re-applied to a figure that already contained it (`brick_breaker`'s
`effect_mult` onto `Ball.speed`, so **SLOW DOWN made the ball faster**), a sentinel every other site
read as something else (a brick's `health = -1` for "indestructible" against five `health <= 0`
"destroyed" tests, so it was invisible and had no collision), and a published slot nothing wrote
(`snake`'s `length++` exposing a `body[length]` the shift never writes, a stray cell at the grid
origin). In each the fix was to make the derived value derived rather than to patch the failing site:
give it **one writer** and read the rest from it. `ball_apply_speed()` is the only writer of
`Ball.speed` and reads a separate `base_speed`; `brick_is_destroyed()` is the only test; the new snake
segment takes the tail cell captured *before* the shift.

Two things that make this class quick to confirm and safe to fix:

- **Look for the dead code that proves the diagnosis.** `brick_breaker` already had a written bounce
  path and a diagonal-stripe renderer for indestructible bricks, neither of which could ever run.
  Unreachable code that was clearly written on purpose tells you which value is being misread.
- **When a clamp moves during the refactor, ask what it was protecting.** `BALL_MAX_SPEED` stayed on
  the *effective* speed, not the base: 11 px/frame is what stops the ball tunnelling through a brick,
  so it belongs on the speed the ball actually travels at. At effect level 0 the emitted behaviour
  must come out byte-identical — that is what preserves the default feel.

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

The saturated band is ~30 px inside each Y edge and ~0–12 px inside each X edge, measured on RW09 with
`touch_raw`'s SWEEP and INSET modes; every edge *does* drive raw to its limit, but the value is clipped
flat over that band, and the interior fit predicts the same panel 30 / 450 by a different method. The
table, and why two earlier revisions of this rule got it wrong in *opposite* directions:
[`../SYSTEM_ANALYSIS.md#33-touch`](../SYSTEM_ANALYSIS.md#33-touch).

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
- **A top button row is `LAYOUT_MENU_BTN_X/Y` + `LAYOUT_EXIT_BTN_X/Y`, never a literal `10, 10`** —
  and **the reserve below it derives from the row**, not from a second literal. So a playfield that
  starts under the row is `LAYOUT_MENU_BTN_Y + BTN_MENU_HEIGHT + <gap>`, and its height comes off
  `SCREEN_VISIBLE_BOTTOM`, not `fb.height - <sum of the gaps>`. A literal offset silently loses its
  top rows to the inset, and a literal reserve leaves the row sitting on the playfield once it moves.
  If the row lives inside a fixed-height HUD band, **the band has to grow with it**
  (`frogger.c`'s `hud_height = SCREEN_SAFE_TOP + HUD_HEIGHT`) or its own content collides with the
  buttons. Where a drawn rect and a hit-test describe the same target, compute both from **one**
  helper (`hardware_diag.c`'s `diag_exit_rect()`) — two literals that have to agree by hand will
  eventually not. All of this was swept and fixed on 2026-08-02;
  `grep -n 'button_init(&[a-z_]*, *[0-9]'` is the check.
  **When you replace a literal, choose the expression that is byte-identical to it at inset 0** — so
  an uncalibrated panel, where the inset is `0`, is provably unaffected and the diff can only move
  pixels on a panel that has actually been swept. That is what makes this class of change safe to do
  in bulk without re-testing every unit.
  **A literal reserve that is *smaller* than the row is the same bug from the other side**, and it
  shipped: tetris' board used `SCREEN_SAFE_TOP + 55` against a row occupying `SAFE_TOP + 10 .. + 60`,
  so the buttons were drawn on top of the board *and* the board ran 5 px off the bottom. Also
  count the **frame**: a `fb_draw_rect()` outline sits *outside* the content rect on every side, so its
  thickness belongs in the vertical budget too, from the same named constant the drawing uses.
- **A capped row is positioned from the count you draw, not the count you have.** Three HUDs draw a
  lives row and all three cap the icons; only the arithmetic differs. `frogger.c` computes
  `lives_shown = min(lives, 5)` and lays out from `lives_shown * LIFE_ICON_PITCH` — right.
  `platformer.c` positioned from `game_lives * 16` while drawing `min(game_lives, 5)`, which agrees at
  3 lives and diverges the moment something grants more: Office Runner's training mode gives 10, the
  row start clamped to `fb.width/2 + 60`, and five icons ended up stranded mid-HUD with a gap to EXIT
  (fixed 2026-08-10). Cap first, lay out from the capped number, and **say what the cap hid** — one
  icon plus `x10`, never five icons meaning ten. `brick_breaker` still truncates silently at nine
  (`../IMPROVEMENT_PLAN.md` B30).
- **Put a status row in the band above the button row, not below it.** The inverse of the rule above
  and the other way to get this wrong: HUD text is only *seen*, so it belongs in `SCREEN_VISIBLE_TOP`
  — the band the two-rectangle split exists to keep usable. Stacking it under `SCREEN_SAFE_TOP`
  instead put tetris' `LVL` and frogger's lives icons **behind** the buttons, which are drawn after
  them. The pattern both games now use, and the reason it is safe on an uncalibrated panel:

  ```c
  int band_h = LAYOUT_MENU_BTN_Y - SCREEN_VISIBLE_TOP;   /* == inset + 10 */
  int row_y  = (band_h >= text_h)
               ? SCREEN_VISIBLE_TOP + (band_h - text_h) / 2   /* in the band */
               : LAYOUT_MENU_BTN_Y + (BTN_MENU_HEIGHT - text_h) / 2;  /* level with the row */
  ```

  At inset 0 the band is 10 px and cannot hold 14 px of text, so the row drops level with the buttons
  — which is only safe because **horizontally it is confined to the gap between MENU and EXIT**
  (`LAYOUT_MENU_BTN_X + BTN_MENU_WIDTH` → `LAYOUT_EXIT_BTN_X`). Lay it out from a *measured* total
  width so it stays centred in that gap as the numbers grow; frogger's old HUD used `fb.width/2 - 100`
  and friends and drifted. That gap is also where a full-width bar goes: frogger's timer bar used to
  span the grid at `hud_height - 18` and covered the bottom 8 px of both buttons.

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

⚠️ **The sanity gate is not "reject outside `0..4095`"** — that is the clamp bug in gate form. It
requires `2 × overlap(fit, hw) ≥ max(fit_span, hw_span)`, which **accepts** the measured-good
`-279..4382` and rejects a skewed `0..60000`.

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
cannot be turned off.

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
discrete actions; `.held` for continuous movement.

**All three fields are pure outputs of `gamepad_poll()`** — it recomputes them from scratch every call,
so writing them from an app has no effect past the next poll. `held` is the OR of two kinds of source,
and that split is what stops a virtual D-pad latching (done 2026-08-03):

| Source | Reports | Level lives in |
|---|---|---|
| gamepad buttons, D-pad hat, keyboard | press/release **events** | `GamepadManager.held_latched[]` — must survive quiet frames, because a key-up may be hundreds of frames away |
| touch regions, analog stick | an absolute **position** | a per-frame array inside `gamepad_poll()`, rebuilt every call |

Until 2026-08-03 both kinds wrote `.held` on the caller's `InputState` and only the first kind was ever
cleared, so **a virtual D-pad latched on permanently** and a `.pressed` reader saw one tap per region
for the life of the process. If you add a source, decide which column it is in first: the naive
"clear `held` at the top of the poll" is what breaks the event-driven half.

Two smaller holes closed with it, both worth not reopening: `poll_gamepad()` **zeroes the axes when
`gamepad_fd < 0`** (a stick unplugged while deflected otherwise asserts its direction forever), and
`gamepad_rescan()` clears `held_latched[]` (a key held at unplug time never gets its key-up).

**A `TouchRegion` virtual D-pad is safe again, but still usually the wrong design.** Snake's four
regions *overlapped* — UP/DOWN the full-width top/bottom halves of the grid, LEFT/RIGHT the full-height
left/right halves, so every in-grid tap asserted two directions at once — and they duplicated the
head-relative input path it already had. So **no *app* calls `gamepad_set_touch_regions()`**; the only
caller is `tests/gamepad_latch_test.c`, and `gamepad_draw_touch_controls()` has **no** caller at all.
Both are kept as library surface; the latter's boxes never matched the regions they claimed to draw.

Prefer a **tap relative to the object being controlled** over a virtual pad — frogger hops the frog
towards wherever you tap in the playfield, which needs no regions, cannot latch, and makes the whole
playfield one target. Where that does not map (platformer needs simultaneous run + jump), say so on the
welcome screen with `screen_draw_welcome_warn()` rather than shipping controls that do not work.

**What you can and cannot test from a script.** `CONFIG_INPUT_UINPUT` is unset in this kernel — no node,
no module — so `tests/touch_inject.c`'s `write()` to `/dev/input/event0` succeeds, reports success and
delivers nothing: evdev's `write()` path is for **output** events, and there is nothing to "fix" in it.
Script-side you can launch a binary over SSH and `cat /dev/fb0` to check the **first** screen and no
further; everything past it needs a human at the panel (`../IMPROVEMENT_PLAN.md` C6).

But that is a limit on *the device*, not on the code: `gamepad.c`'s own state machine is fully testable
on the host, and `tests/gamepad_latch_test.c` does it. `gamepad_poll()` takes the touch coordinate as a
plain argument, and its evdev sources are `read(2)` on an fd — so a temp file of `struct input_event`
assigned to `gm.gamepad_fd` drives the real `poll_gamepad()`, returning each event and then 0 at EOF
exactly as a quiet non-blocking evdev fd does. Run that after touching input logic; what still needs
the panel is only whether a game *feels* right.

**Ask a touch button with `button_check_tap()`, every frame.** `button_check_press()` derives the press
edge from `btn->was_pressed` and clears that latch on exactly one kind of call — one where
`currently_pressed` is **false**. So this shape, which shipped in `platformer.c` and `frogger.c`, fires
**once per process** and is dead afterwards:

```c
if (ts.pressed) {                                          /* WRONG */
    if (button_is_touched(&menu_button, ts.x, ts.y) &&
        button_check_press(&menu_button, true, now)) { … }  /* never called with false */
}
if (button_check_tap(&menu_button, &ts, now)) { … }         /* right — every frame */
```

Office Runner's hamburger menu died after its first use and the report came off the panel 2026-08-10.
Three things make this class hard to see, and all three are why it needs
a helper rather than care:

- **EXIT hides it.** An exit button only ever needs to fire once, because it ends the process — same
  defect, no symptom. In the report EXIT worked and MENU didn't, in the same HUD row.
- **Touch gameplay hides it.** `brick_breaker.c` and `tetris.c` pass the *value* and survive, but only
  because their players tap elsewhere constantly and that is what produces the false call. A
  controller-only game has nothing to clear the latch.
- **The obvious fix keeps it.** Testing `button_is_touched()` alone on quiet frames does not clear it
  either: `touch_input.c` leaves `state.x/y` at the last touched point after release, so the
  coordinates stay inside the rect with no finger on the panel. The level signal has to come from
  `ts.pressed || ts.held`, which is what `button_check_tap()` does.

`tests/button_latch_test.c` is the regression, and **group A drives the old idiom** so a green run cannot
mean the harness is blind. `grep -rn 'button_is_touched(&[a-z_]*, *[a-z.]*x, *[a-z.]*y) *&&' --include=*.c`
is the tree-wide check.

**Every app with a game loop calls `gamepad_rescan()` on a timer.** `RESCAN_INTERVAL_MS` = 5000 in each
game; nothing inside the library calls it, so an app that omits it never re-detects a device. The read
loop is `while (read(fd, &ev, sizeof(ev)) == sizeof(ev))` — a pad that leaves the bus (unplugged, or a
wireless one idling out) leaves a stale fd that fails forever, so input is dead for the life of the
process and only relaunching fixes it. Platformer was the one app of ten without it, measured from the
same panel report. It also clears `held_latched[]`, so a direction held at unplug time is not stuck on.

⚠️ **The rescan fixes a stale fd, not a dead port — and the two are complementary.** It re-`open()`s
`/dev/input/event0..31` from scratch, so it finds a *new* node but cannot create one, and a unit booted
with an empty port has no node for this poll to find for the rest of that boot: MUSB powers the port only
for a device attached at the moment the driver probes (measured 2026-08-13 on `.188`). A recovery needs
**both** halves — `/etc/init.d/usb-host recover`, a driver re-probe and **not** a sysfs write to `mode`
(a silent no-op on this SoC), to make the device enumerate, and this poll to pick it up without a
relaunch. Do not read the 5 s poll as "unplug/replug recovers", and read
[`../SYSTEM_ANALYSIS.md#36-usb`](../SYSTEM_ANALYSIS.md#36-usb) before building anything on it.

Every app should handle `BTN_ID_BACK` as "exit / back" — `fb_fade_out()` then `running = false`, as
`frogger.c` does. Platformer was the one game that didn't, which left its game-over screen with no way
out at all; fixed 2026-08-02, and it is the reason that screen's buttons are reachable now.

## 32-bit target

`sizeof(long) == 4`. Never write `tv_sec * 1000000L` — baseline timers to a start timestamp
captured at init, not to epoch 0, and do the multiply in `uint32_t` or `int64_t`. A duration times a
sample rate is the same trap: `(long)44100 * 49000` leaves 32 bits, which is why
`audio_frames_for_ms()` computes in `long long` and clamps.

⚠️ **A host regression cannot *observe* any of this — this host's `long` is 64 bits.** The overflow has
to be **modelled**, by truncating the 64-bit product to `int32_t` the way armhf does; a test that simply
writes the shipped expression passes on the host and says nothing about the target. `tests/audio_gen_test.c`
group A does it that way and labels it.

## Audio: the generator is separate from the device

`common/audio_gen.c` is the audio logic with **no fd, no ioctl and no clock in it** — so it is the half a
host regression can reach (`tests/audio_gen_test.c`, 154 checks, build line in its header). `audio.c` keeps
the device half: the config gate, `/dev/dsp`, the ioctls, the GPIO12 amp poke — and it **consumes
`audio_gen` for everything else**, so there is no arithmetic in it a host test cannot reach. Three rules
live there and each has cost something:

- **The channel count is an argument, never a literal.** `hw:0,0` is stereo-only and the speaker sums
  L + R (both measured, [`../SYSTEM_ANALYSIS.md#34-audio`](../SYSTEM_ANALYSIS.md#34-audio)), so the
  generator is mono and single-sample and `audio_interleave()` is the one conversion point. A `frames * 4`
  with the 4 spelled out is only accidentally right. `configure_dsp()` reads the count back with
  `SOUND_PCM_READ_CHANNELS` and warns **once per `Audio`** if it has to fall back to 2 — that function
  runs on every `audio_flush()`, so a per-call warning would spam.
- ⚠️ **A write must never stop mid-frame.** Half a frame handed to the kernel swaps L and R for the rest
  of the stream, permanently, and a stereo-only interface has no mono path underneath to absorb it.
  `audio_write_frames()` is the only code that decides when to stop, and it stops on frame boundaries or
  reports `misaligned`. Its mid-frame retry is bounded by `AUDIO_ALIGN_TRIES` regardless of the caller's
  policy — an unlimited policy against a full sink hangs the render loop, which is worse than the swap.
  The four EAGAIN loops `audio.c` used to hand-roll are now four **named policies** — `WPOL_TONE`,
  `WPOL_PREFILL`, `WPOL_CHUNK`, `WPOL_FADE` — over one `write_mono()`. Do not add a fifth loop; add a
  fifth policy.
- **The fade-out is a MODE of the one oscillator, not a second copy.** `AUDIO_OSC_FADE_OUT` deliberately
  holds frequency and amplitude still; deleting it while collapsing the duplicated generators deletes the
  fade. `AUDIO_OSC_GLIDE` reproduces the old stream generator byte for byte, and split calls equal one
  long call — which is what lets a caller write whatever the ring will take without a seam.

### Mixing: an optional per-frame pump

Two sounds at once needs userspace to hold the audio and hand the device small pieces of it, because you
cannot mix into a buffer the kernel already has. `audio_pump()` does that **from the render loop —
never a thread**: static ARM plus pthread is the `clock_gettime64` SIGSEGV-before-`main()` scar
(`../CLAUDE.md`). Converting an app is three lines:

```c
audio_init(&audio);
audio_pump_enable(&audio, true);              /* once, after init */
while (running) {
    /* ... */
    audio_pump(&audio);                       /* once per frame, beside fb_swap() */
    usleep((drew || audio_pump_active(&audio)) ? FRAME_DELAY_ACTIVE_US
                                               : FRAME_DELAY_IDLE_US);
}
```

Nine rules, each of which is a way to get this wrong:

- ⚠️ **The lead is measured in DEVICE PERIODS, never in milliseconds alone.** `audio_pump_lead_frames()`
  takes the period off the device, floors the lead at `AUDIO_PUMP_LEAD_PERIODS` (3) of them and rounds
  **up**. A lead of 1.7 periods is what a panel heard as a crack every ~120 ms, a shortened CHORD and a
  tone so chopped it read as a square wave — because the OSS shim only hands ALSA whole periods and
  **discards the staged remainder on an underrun**
  ([`../SYSTEM_ANALYSIS.md#34-audio`](../SYSTEM_ANALYSIS.md#34-audio), gotcha 5). ⚠️ **The lead is also
  the latency ceiling** — ~139 ms on the shim's 46 ms period, which Phase 4's 23 ms period buys back.
- ⚠️ **The sum needs HEADROOM, and `AUDIO_PEAK` is an acoustic limit, not a digital one.** Every voice
  plays at 18000 ≈55 % of full scale *because `SPKR1` sums L + R*, so three voices reach 54000 against
  32767. `audio_mix_limit()` is linear up to a knee at `AUDIO_PEAK` — which is what keeps **one voice
  byte-identical** — then asymptotic to `AUDIO_MIX_CEIL` 26000, deliberately **below** two voices'
  arithmetic sum so it protects the speaker too. Because the curve is bounded, ⚠️ **`clipped` must read
  exactly 0**: that is the check, not a comfort margin. `AUDIO_MIX_HARD` keeps the old clamp for A/B.
- ⚠️ **The counters are the diagnosis, and each means ONE thing.** `clip` (int16 could not hold it —
  0 under the soft limiter), `lim` (the knee bent it — expected, not a fault), `starve` (the ring was dry
  with audio still owed: **one audible gap each, and it attributes crackle to pacing rather than to
  mixing**), `lost` (frames rendered, voices advanced, device refused them), `drop` (a full bus). Two
  plausible suspects were **refuted** by exactly these numbers: `lost` stayed 0, and the worst frame time
  stayed at 107 ms while `starve` climbed.
- ⚠️ **It is opt-in, and an app that never enables it takes today's path byte for byte.**
  `audio_tone()` branches on `audio->pumping`; it does **not** "enqueue and also write immediately",
  which was the plan's sketch and cannot work — a bounded immediate write truncates any tone longer than
  the 80 ms lead, and an unbounded one hands the whole tone to the kernel, which is what makes it
  unmixable. A silent binary is this project's "does not error — it misparses", and nobody would notice
  until they played that game.
- ⚠️ **`audio_pump_active()` must be in the frame-pacing decision.** The pump keeps only
  `AUDIO_PUMP_LEAD_MS` (80 ms) inside the device, so a loop that drops to `FRAME_DELAY_IDLE_US` (100 ms)
  mid-sound starves it and you hear a gap — which reads as a mixing defect rather than a pacing one.
  Ask the component, exactly as with `gameover_needs_redraw()`.
- ⚠️ **The pump targets a LEAD; it never writes into the free space.** An empty ~506 ms OSS ring will
  accept half a second of audio, and then the next sound plays half a second late.
- **A voice carries a `delay`, and `audio_success()` depends on it.** Three voices added at once are a
  *chord*; the four canned sounds are four note tables and one sequencer that offsets each note by the
  ones before it. All four signatures are unchanged (~45 call sites), as is `audio_interrupt()`'s (~23) —
  which on the pump means "stop all voices" and no longer resets the ring, so up to 80 ms of tail
  survives it.
- **A full bus refuses and counts (`audio_pump_dropped()`); it never steals a voice.** The longest voice
  is the one a dropped blip must not cut — `../IMPROVEMENT_PLAN.md` F19's soundtrack.
- **`WPOL_PUMP` is a fifth *policy*, not a fifth loop.** Same rule as the four above it.

The clamp is a single one after the whole `int32` sum, so slot order cannot change the mix, and it
**counts** — `audio_pump_clipped()`. `AUDIO_PEAK` is ≈55 % of full scale, so two loud voices exceed
int16 by ~10 %; whether that is audible is a panel question, not one to invent a gain for.

`tests/audio_mix_test.c` is the interactive tool for the panel questions, and its **PUMP toggle puts the
pre-pump path on the same screen as the negative control**. It is also how the ~60 ms minimum-tone rule
gets re-measured rather than carried forward or deleted on faith.

⚠️ **An `Audio` must be filled by `audio_init()` or `audio_init_unchecked()`, never by hand.** Two tabs
used to `memset` one and set three fields — `dsp_fd`, `available`, `sample_rate` — and open `/dev/dsp`,
the three ioctls and GPIO12 themselves. The moment the struct gained `channels`, that idiom left it at
**0**, and a 0-channel byte count is 0: **silently mute**, measured (`audio_bytes_for_frames(8820, 0)`
= 0 against 35280 for 2 channels). `audio_init_unchecked()` is the one place the config gate is bypassed,
which the Settings-tab hardware test genuinely needs — a test that drives the speaker must not obey the
setting it exists to test. The tree-wide check is one grep, and its only legitimate hit is `audio.c`:

```bash
grep -rn 'open(DSP_DEVICE\|open("/dev/dsp"' --include=*.c native_apps/ | grep -v arm-deps
```

(`tests/ch_test.c` and `tests/oss_diag.c` also hit it — they are standalone OSS probes that use no
`Audio` at all and are not in `build-and-deploy.sh`.)

Open work and the phasing: [`../IMPROVEMENT_PLAN.md`](../IMPROVEMENT_PLAN.md) F1.
