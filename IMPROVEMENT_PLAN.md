# Improvement Plan

Prioritised backlog from the full code + hardware review of 2026-07-29.

**How to read this**

- **B**n = bug, **F**n = feature, **D**n = doc/infra, **C**n = cleanup.
- **Verified** means someone actually read the code or ran the command and confirmed it.
  **Reported** means a reviewer found it but it has not been independently re-checked — still
  likely real, but confirm before you spend an afternoon on it.
- ✅ means **confirmed to be a real defect** — it does *not* mean fixed. Finished items move to
  [Closed](#closed) at the bottom, which keeps only the reasoning worth not re-litigating;
  where the code and git history are the whole story, the item is simply deleted.
- Nothing in this plan requires a kernel rebuild. Items that would are listed at the bottom
  under [Out of Scope](#out-of-scope).

**Ground rules discovered during the review** — worth internalising before touching anything:

- The device is an **OMAP3503** (Cortex-A8, no GPU, no DSP).
- Every shipped binary is `-static`, which is why aggressive rootfs cleanup is safe.
- Recovery is cheap: the SD card is removable and `bootcmd` always loads `uImage-system`, so
  staging experiments under a different filename is a free undo.

---

## Phase 0 — Do these first (no risk, high leverage)

Nothing here can break a running device. **D1–D5 are done** — see [Closed](#closed).
Only D6 remains, and its remaining step is not a code change.

### D6. Secrets — **partly done 2026-07-29**

`vnc_client/vnc_client.conf` held a plaintext password and was tracked. Now untracked +
gitignored, with `vnc_client.conf.example` as the template and `chmod 600` on deploy.

⚠️ **Still to do:** the password remains in git history. For a LAN VNC password the pragmatic fix
is to **change the password on the VNC server** rather than rewrite history. Do that before
making the repo public.

---

## Phase 1 — Correctness bugs

Ordered by (severity × likelihood of being hit).

### B1. 16bpp framebuffer heap overflow — **partly fixed**

The back buffer is allocated as `width * height * bytes_per_pixel`, but the drawing primitives
(`fb_draw_pixel`, and everything built on it) write `uint32_t` unconditionally. **At 16bpp that
overruns the allocation by a factor of two.**

Closed so far: `fb_clear`, `fb_swap` and the `fb_set_bezel` reallocation are now byte-sized and
correct at any bpp, and every app that uses the common primitives (`app_launcher`,
`game_selector`, `device_tools`, `hardware_config`, `touch_raw`) calls
`fb_set_bpp(dev, 32)` before `fb_init()`, so none of them can inherit 16bpp from a crashed
ScummVM or VNC session.

Still open: the primitives themselves. Nothing currently reaches them at 16bpp — ScummVM and
`vnc_client` both hand-write `uint16_t` — but one `fb_draw_text` added to either would corrupt
the heap.

**Fix:** make the primitives dispatch on `bytes_per_pixel` (a 16bpp `fb_draw_pixel` writing
RGB565 would also let ScummVM and the VNC client drop their private text renderers). Note
`fb_init` must *not* reject non-32bpp modes: ScummVM legitimately runs the framebuffer at 16bpp
through `fb_init`/`fb_swap`.

### B2. Gamepad buttons latch on and never release

`native_apps/common/gamepad.c:791` (virtual touch regions) and `:649` (analog stick → D-pad) both
set `.held = true`, and **nothing anywhere ever clears it** — `gamepad_poll` deliberately doesn't
(`:828`), and no caller does.

- platformer: one tap on the virtual left pad → the player runs left forever.
- frogger/snake (which read `.pressed`): first tap works, every later tap in that region is dead.

**Fix:** keep touch/stick-derived state in a separate per-frame bitmask and OR it with the latched
key/hat state each poll.

### ~~B3. A bad calibration can wedge the device with no recovery~~ — **DONE 2026-07-31**

All three parts fixed:

1. **Sanity gate.** `touch_calib_range_sane()` (`common/touch_calib.c`) requires
   `2 × overlap(fit, hw) ≥ max(fit_span, hw_span)`. Accepts the measured-good `-279..4382`
   (overlap 4095 of max span 4661), rejects `0..60000` and any range disjoint from the hardware's.
   Deliberately **not** "reject outside 0..4095" — a correct fit on this panel legitimately
   extrapolates outside it on Y.
2. **Hit-testing.** The wizard keeps the *entry* calibration installed through TAP, CHECK, EDGES
   and REPORT, so its own buttons always work. The new mapping goes live only at CONFIRM, behind a
   20 s auto-revert countdown, and nothing is written until you press KEEP. Plus a `RESET` on the
   Display tab and inside CHECK, so there is always a way back without SSH.
3. **`touch_wait_for_press_raw()`** now uses `poll()` with a 200 ms slice, returns -1 on a real
   read error instead of spinning, and can be interrupted.

### ~~B3a. The 9-tap calibration fits through its own edge-compressed samples~~ — **DONE 2026-07-31**

Fixed by fitting from interior targets only (≥100 px from each end on X, ≥80 px on Y) in the new
wizard. The duplicate implementation in `tests/unified_calibrate.c` is gone — it was folded into
`device_tools` and deleted, so there is now one fit, in `common/touch_calib.c`.

Regression: feeding the 11 target medians from
[`touch_raw-2026-07-31-rw09.tsv`](touch_raw-2026-07-31-rw09.tsv) through `touch_calib_fit()`
reproduces `X 17..4084` / `Y -279..4382` exactly, with edge-probe residuals `-7/+6` px on X and
`+8/-9` px on Y. Note that X endpoints *outside* `0..4095` are **not** by themselves a symptom of edge
samples leaking into the fit — RW09's accepted 2026-08-01 18:50 calibration is `X -33..4122`, and the
sanity gate in B3 exists precisely because overshoot is legitimate on either axis. What indicts a fit
is an interior mask that admits near-edge targets, or a slope that disagrees with the interior line.

### ~~B3b. `touch_raw` prints one global verdict where the panel needs two~~ — **DONE 2026-07-31, reworded 2026-08-01 (twice)**

`touch_calib_axis_verdict()` produces one line per axis, and both `touch_raw` and the wizard's CHECK
screen print both. It now takes the hardware range and reports the **dead band in panel pixels**,
because that is what the measurement actually shows. On the reference data it reads
`X: reaches both edges, no dead band` and
`Y: sensor saturates 28 px inside the low edge, 30 inside the high - still drawable, not pressable`.

Two earlier wordings were wrong and are recorded so the mistake is not repeated: the original
H1/H4 text asserted a hardware inset it had not measured, and the 2026-08-01 morning rewrite replaced
it with `- edges still reach`, which asserted the opposite and was equally unmeasured. A verdict about
reach must come from `INSET`-style inward stepping, never from a bezel press or an edge sweep.

### ~~B3c. Edge bands that could not be touched~~ — **DONE 2026-08-01 (evening)**

This item has been wrong in both directions; the settled answer is that **both effects were real**.

1. The dead bands *were* partly manufactured by the fit. A single line fitted to the interior
   extrapolates outside the emittable range (`Y -279..4382` against `0..4095`), and
   `scale_coordinates()` mapped that fitted span onto the whole panel, so the sensor's real extremes
   landed inside it. On X the overshoot was run-to-run tap noise, which is why the right edge worked
   on some runs and not others; the `6 4181` config saved on 2026-08-01 lost 17 logical columns.
2. The sensor *also* saturates before the physical edge. Measured with `touch_raw`'s `INSET` mode on
   2026-08-01 (capture: `touch_raw-2026-08-01-rw09.tsv`): raw 4095 is first emitted around panel 450
   and raw 0 around panel 30 — a flat band of ~30 px at each end of Y, and ~0–12 px on X. `SWEEP`
   separately confirms every edge *does* drive raw to its limit (16/16 buckets, all four edges), which
   is why a bezel press could never settle this: it reads identically under either hypothesis.

**The morning fix over-corrected.** It bent the outer segments to land raw 0/4095 on panel 0 and
panel dim-1 — i.e. it clamped the endpoints — which asserted raw 4095 is emitted at panel 479 when it
is emitted at panel ~450. That tilted the upper outer segment and made the reported position run
**ahead of the finger by up to +19 px across the bottom quarter**: visibly worse than the bug it
replaced.

**The fix that shipped.** The clamp is gone from both `touch_calib_curve_from_fit()` and
`touch_input.c`'s legacy migration (`clamp_to_hw()`, deleted). Endpoints are stored exactly where the
fitted line puts them, so the stored three-segment curve *is* that line (the host regression asserts
zero deviation) and interior accuracy is ±2 px. The dead band is then **exposed rather than hidden**:

- `framebuffer.h` carries two rectangles — `SCREEN_VISIBLE_*` (full logical screen) and
  `SCREEN_SAFE_*` (visible ∩ touchable). The band stays fully drawable, which the user explicitly
  wanted: a status bar or score row there is good use of screen.
- The inset is **measured at runtime**, never hardcoded: `publish_safe_area()` pushes the four raw
  edge extremes through the production `scale_coordinates()`. `0` until an edge sweep is recorded;
  capped at `FB_TOUCH_INSET_MAX` (48 px) with a warning.
- Config line 3 (`reach x_lo x_hi y_lo y_hi`, keyword-tagged and optional) persists the swept reach;
  the wizard's new `REACH` step measures it. Tagged and trailing so old parsers ignore it.
- Per-app audit done: draw-only call sites moved to `SCREEN_VISIBLE_*` (Tetris board, Brick Breaker
  play area, `hardware_diag` in full, titles/hints/score tables in `common.c`, `app_launcher`,
  `game_selector`, `pong`). SameGame's grid stayed on `SCREEN_SAFE_*` — every block is a tap target.

Consequently **not needed**: `SCREEN_TOUCH_*` macros, bigger bezel margins, and a band-limited
stretch. A dead band is a fact about the panel, so the wizard's `REPORT` step and the `TOUCHABLE:` row
report it as a number and go amber only on *magnitude* (24 px), not on "non-zero".

Still open, and the only part of this item left: **measure a second unit** with `/opt/games/touch_raw`
(SWEEP then INSET on all four edges). Everything above is RW09 only. What a second unit settles is
whether the ~30 px Y band generalises — if it varies per panel, the runtime measurement already
handles it and no code changes. Save the `/tmp/touch_raw.tsv` capture into the repo before the device
reboots: the **wizard writes no tsv**, only the diagnostic does, which is why RW09's live 18:50
calibration has no capture of its own and the 16:53 one remains the reference (see
`SYSTEM_ANALYSIS.md#33-touch` → *Provenance*).

The audit also turned up **B3e** (touch targets at hardcoded offsets), which is tracked separately.

**Follow-on, done 2026-08-01 (late):** the per-app audit above only covers code we wrote. ScummVM and
`vnc_client` display **third-party** content that cannot be audited for what has to be pressable — a
remote taskbar, a game's verb bar, the ScummVM theme's button row — so both were still placing guest
pixels in the dead band (reported on the device as "~10 px unreachable top and bottom" right after a
precision calibration). Both now confine the guest content rectangle *itself* to `SCREEN_SAFE_*`, and
everything hit-tested in them (VNC settings/reconnect/exit gesture, ScummVM's overlay and gesture
corners) is on the safe rect unconditionally. Each has an opt-out that moves only the picture, whose
discoverability is **B3f/B3g** and whose config-file location is **B3h**. Rules in
`vnc_client/CLAUDE.md` and `scummvm-roomwizard/CLAUDE.md`.

### ~~B3d. Fold `unified_calibrate` and `SCREEN EDGES` into one `touch_raw`-based flow~~ — **DONE 2026-07-31**

Done as the Display tab's `run_calib_wizard()`: TAP → CHECK → EDGES → REACH → REPORT → CONFIRM, all with
the bezel zeroed on the full panel, writing both config lines. `tests/unified_calibrate.c` is
deleted and the fit moved to `common/touch_calib.c`, shared with `touch_raw` — so the diagnostic
now validates the same code the wizard calibrates with, and the two lines can no longer be measured
against different assumptions.

The bezel is measured by *looking*, not by touching: numbered 2 px ladders at each panel edge, and
the operator raises each margin until its line clears the plastic. The old adjuster drew its
reference frame on the *logical* edge, which is defined by the margins it was trying to measure.

### B3e. Buttons positioned with hardcoded offsets lose rows to the touch inset

Found during the B3c per-app safe-area audit (2026-08-01) and **not** fixed there, because it is a
pre-existing violation of the "never hardcode 800/480/400" rule rather than part of that change.

A handful of touch targets are placed at literal pixel offsets instead of `SCREEN_SAFE_*` /
`LAYOUT_*`. That was harmless while `SCREEN_SAFE_TOP` was `0`; now that the touch inset is real, the
outermost rows of those targets fall in the band no finger can reach:

| Site | Code | Effect at a 17 px inset |
|---|---|---|
| `native_apps/tetris/tetris.c:203,205` | `button_init(&menu_button, 10, 10, …)` and the matching exit button | rows 10–16 of a 40 px button are dead; 17–50 still respond |
| `native_apps/hardware_diag/hardware_diag.c` (`draw_page_header`) | `fb_fill_rect(fb, 720, 2, 76, 36, …)` EXIT zone | rows 2–16 dead, 17–38 respond |

Both still work, but by luck — a larger inset on another panel would kill them outright, and the
inset is measured per unit (B3c). Fix: use `LAYOUT_MENU_BTN_X/Y` and `LAYOUT_EXIT_BTN_X/Y` in
`tetris.c`, and derive the `hardware_diag` EXIT zone from `SCREEN_SAFE_RIGHT` / `SCREEN_SAFE_TOP`.

Worth a wider sweep at the same time: `grep -n 'button_init\|fb_fill_rect'` for any touch target
whose coordinates are literals rather than macros. The audit only covered call sites that already
mentioned `SCREEN_SAFE_*`, so a hardcoded one is invisible to that method by construction.

### B3f. The `content_area` setting is config-file-only, with no UI

Added with the guest-content change (2026-08-01, the follow-on to B3c): `vnc_client` letterboxes the
remote desktop into `SCREEN_SAFE_*` so all of it is reachable, and `content_area = safe | visible` in
`/opt/vnc_client/vnc_client.conf` trades that back for ~11 % more pixels on a 1080p desktop.

**It is not on the settings screen.** On a wall-mounted panel with no keyboard, that means the only
way to change it is SSH — every other setting in that file is editable on the device.

Deliberate, for a concrete reason: `vnc_settings.c` has `ROW_COUNT 6`, the row block already spans
y 44–352 at a 52 px pitch, and the status line sits at 367 — a seventh row lands on top of it. Adding
the control needs a layout change, not just a row:

- a second page (the screen has no paging today), or
- shrink `ROW_H`/`ROW_GAP` to fit 7 rows in the same band, or
- put it on a settings *tab bar* like `device_tools` has.

The value does round-trip safely in the meantime — `save_config_file()` writes the key even though
nothing edits it, so pressing SAVE cannot silently reset a hand-edited choice. That was the trap
worth closing first.

Same gap on the ScummVM side, for the same reason (`rw_content_area` — see B3g), so whichever fix
lands should cover both.

### B3g. ScummVM's `rw_content_area` is invisible until you know it exists

`rwFullContentArea()` (`roomwizard.cpp`) reads the key with `ConfMan.hasKey("rw_content_area")` and
never writes it. ConfMan only persists keys that were *set*, so the line **does not appear** in any
`scummvm.ini` — the option is undiscoverable from the device, and the first thing a user does is look
in the file, not find it, and conclude it does not work. (Reported 2026-08-01, exactly that way.)

`ConfMan.registerDefault()` does not help: registered defaults are not written to the file either.

Fix options, cheapest first:

- `ConfMan.setAndFlush("rw_content_area", "safe")` on first run when `!hasKey()`, so the key is
  present and self-documenting from then on. One line, and it makes the file the discovery surface.
- Expose it in the GUI. ScummVM's Options dialog is upstream code, so this means a backend-specific
  tab — much more work, and it shares the "no UI" problem with B3f.

Until then the reliable route is the environment variable `ROOMWIZARD_CONTENT_AREA=visible`, which
takes precedence over the config and needs no file at all. It is documented in
`scummvm-roomwizard/README.md`; the config key should not be documented as the *primary* route while
this is open.

### B3h. ScummVM's config file location depends on the working directory

`OSystem_RoomWizard` does not override `getDefaultConfigFileName()`, so it inherits the base
`OSystem` implementation — `common/system.cpp:245` returns the bare relative name `"scummvm.ini"`.
`OSystem_POSIX` overrides that with an absolute `$HOME/.config/scummvm/…` path, but our backend
derives from `ModularGraphicsBackend`, not from it. So the config file is resolved **against the
process's current directory**, and RW09 now has three of them:

| File | Written when | Contents |
|---|---|---|
| `/scummvm.ini` | launched by the boot init script — `/etc/init.d/roomwizard-app` does not `cd`, and `app_launcher` `execl()`s without `chdir()`, so the cwd is `/` | the real one; has the game list |
| `/home/root/scummvm.ini` | someone ran `/opt/games/scummvm` from an SSH shell, where the cwd is `$HOME` | a stale partial copy |
| `/opt/games/scummvm.ini` | ran with `cd /opt/games` first | was created empty on 2026-08-01 and deleted again |

**Nothing in the repo copies or deploys an ini** — `scummvm-roomwizard/build-and-deploy.sh` ships no
`.ini` at all. Each of those files is one ScummVM wrote for itself wherever it happened to be
started. That answers "why do we copy our ini to the home folder": we do not, and neither does the
launcher; ScummVM does, and the location is an accident of the invocation.

Consequences beyond the confusion: settings do not follow the user between an SSH-launched run and a
boot-launched one, save-game paths and the game list can differ per launch method, and editing "the"
ini is a coin flip (which is how B3g surfaced).

Fix: override `getDefaultConfigFileName()` in `OSystem_RoomWizard` to return one absolute path —
`/opt/games/scummvm.ini` is the natural home, next to the binary, the icons and the game data, and it
survives the `$HOME`-less environment the init script runs in. Then migrate the existing
`/scummvm.ini` onto it once (it is the one with the real game list) and delete the strays. Cheap, and
it makes B3g's `setAndFlush` land somewhere predictable.

### B4. Respawn loop always logs exit code 127

`roomwizard-app-init.sh:122-126`. The `while kill -0 …; do wait; done` loop reaps the child and
consumes its status; the `wait` after the loop targets an already-reaped PID and returns 127
unconditionally.

Per `CLAUDE.md`, **exit 132 (SIGILL) is the diagnostic for the Cortex-A8 divide trap** — the one
failure this log exists to catch is the one it can never report. The construct is also a
100%-CPU busy-spin if `wait` ever returns while the PID is live.

**Fix:** `wait "$CHILD_PID"; EXIT_CODE=$?` *inside* the loop; drop the outer `wait`.

### B5. No fallback when `default-app` is broken

`roomwizard-app-init.sh:113-135`. A missing/non-executable `default-app` loops on a 10 s sleep
**forever** with a black screen and no on-device recovery. An app that crashes instantly restarts
every 2 s with no backoff. Both `vnc_client` and `scummvm` deploy paths let you point
`default-app` at a binary that may not exist yet.

**Fix:** after N consecutive failures (or if the path isn't executable), fall back to
`/opt/roomwizard/app_launcher` and log it, so touch access is always recoverable.

### B6. `start-stop-daemon` fallback starts a second app

`roomwizard-app-init.sh:141-146`. `start-stop-daemon --start` exits **1 when a matching process is
already running** — its normal "already up" signal. The `||` therefore fires precisely then and
launches a second wrapper; two apps fight over `/dev/fb0`. Every deploy path calls `start`, not
`restart`, and `setup-device.sh` installs the symlink in rc2–rc5.d.

**Fix:** call `restart` from deploy scripts; make the fallback conditional on a real failure.

### B7. Descending gradients render garbage — ✅ *verified*

`native_apps/common/framebuffer.c:558`. `tr`/`br` are `uint32_t`, so when the bottom colour is
darker `(br - tr)` wraps to ~2³², and the subsequent division does not undo it. Only row 0 is
correct. Affects **every** descending gradient — all brick colours, the paddle, the platformer sky.

**Fix:** `int dr = (int)br - (int)tr;` and clamp to 0..255.

### B8. Non-atomic config/highscore saves

`config.c:112` and `highscore.c:57` both `fopen(path,"w")` (immediate truncate) → `fprintf` →
`fclose`, with no `fsync`. This device gets power-cycled; `hs_save()` runs at game-over. A
truncated config silently reverts every setting to defaults.

**Fix:** write `<path>.tmp`, `fflush` + `fsync(fileno(f))`, `fclose`, `rename()`.

### B9. Backlight get/set asymmetry permanently dims the panel

`hardware.c:201` `hw_set_backlight()` applies the config percentage; `:208` `hw_get_backlight()`
returns the raw sysfs value. `device_tools.c:1339`, `hardware_test.c:53` and
`hardware_test_gui.c:276` all do `int original = hw_get_backlight(); … hw_set_backlight(original);`.
With `backlight_brightness=50`, **each run of the backlight test halves the panel** (100→50→25→…).
Same asymmetry for LEDs.

**Fix:** unscale in the getter, or add `hw_set_backlight_raw()` for restore paths.

### B10. ScummVM `getMillis()` overflows at 24.85 days

`scummvm-roomwizard/backend-files/roomwizard.cpp:184`. Correctly baselined to start time, but
`(curTime.tv_sec - _startTime.tv_sec) * 1000` is evaluated in 32-bit signed `time_t`. A wall
display is always on; the reference unit is already at 7 days. Long-press detection, cursor
timing, touch-feedback fade and `DefaultTimerManager` all break simultaneously.

**Fix:** `(uint32)(curTime.tv_sec - _startTime.tv_sec) * 1000u + …`.

### B11. VNC: framebuffer leaked on every reconnect

`vnc_client/vnc_client.c:632`. `vnc_malloc_fb` mallocs `width*height*4`; `rfbClientCleanup()`
frees `raw_buffer`/`ultra_buffer`/`desktopName`/`serverHost` but **not** `frameBuffer`, and
nothing else does either. With `RECONNECT_MAX_ATTEMPTS 0` (unlimited) and a 1080p host that is
~8.3 MB per drop on a 234 MB device — OOM after ~25 reconnects.

**Fix:** `free(g_vnc_client->frameBuffer)` before `rfbClientCleanup()`.

### B12. VNC: no dead-peer detection

`vnc_client/vnc_client.c:589`. `WaitForMessage(…, 10000)` returns 0 on timeout and the loop just
spins; libvncclient 0.9.14 sets no `SO_KEEPALIVE` and the client never pings. A silent TCP death
(AP drop, NAT idle timeout, VM suspend) leaves a stale frame on screen forever — "connection lost"
never logs and the reconnect UI never appears.

**Fix:** track the time of the last successful `HandleRFBServerMessage` and break after N seconds;
set `SO_KEEPALIVE` after `rfbInitClient`.

### B12b. ScummVM: exiting a game quits ScummVM instead of returning to the launcher

`OSystem_RoomWizard::quit()` calls `exit()` unconditionally rather than setting a flag that lets
the main loop fall back to the ScummVM launcher. The same build returns to the launcher correctly
on Ubuntu. Compare with the SDL backend's `_quit` flag + launcher loop.

Diagnosed but unfixed; was recorded only in `scummvm-roomwizard/SCUMMVM_DEV.md`.

### B12c. ScummVM: OPL tempo unverified after the mono-mixer fix

Open verification task — play the KQ3 intro on the device and compare against a reference
recording. The mono mixer and the `SOUND_PCM_READ_RATE` read-back were supposed to fix half-speed
OPL; nobody confirmed it on hardware.

### B13. Game-specific bugs

| # | Where | Bug |
|---|-------|-----|
| B13a | `platformer.c:1537` | Game-over screen polls touch a second time in the same frame, clearing `pressed` → RESTART/EXIT never fire. Also the **only** game with no `BTN_ID_BACK` handler, so the process can only be killed. |
| B13b | `brick_breaker.c:459` | Indestructible bricks use `health = -1`, which every other site reads as "destroyed" (`health <= 0`) → from level 5 they are invisible and have no collision. |
| B13c | `samegame.c:1638` ✅ *verified* | `needs_redraw = false` set **before** the pacing ternary → permanently locked to 10 FPS. Every other game resets it after the `usleep`. |
| B13d | `tetris.c:863` | Gravity counted in loop iterations while idle frames sleep 100 ms → pieces fall ~3× too slow, and speed up while a key is held. |
| B13e | `tetris.c:567` | No wall kick — an I-piece rotated vertically against the right wall can never rotate back. |
| B13f | `snake.c:320` | Growing exposes a stale `body[]` slot: a detached cell is drawn for one tick and stepping on it ends the game. |
| B13g | `snake.c:623` | `gamepad_init()` called *after* `init_game()` wipes the registered touch regions (frogger/platformer get the order right). |
| B13h | `brick_breaker.c:535` | Speed power-ups compound — `effect_mult` is never divided out, so SLOW DOWN can make the ball faster. |
| B13i | `platformer.c:951` | Stomping two overlapping enemies kills the player (no `break` after a successful stomp). |
| B13j | `samegame.c:250` | `pixel_to_grid` truncates toward zero, so taps up to one block outside the left/top edge select row/column 0. |

### B14. Blocking `usleep()` inside input/update paths

Up to ~1.2 s of frozen UI: `tetris.c:286` (game-over LED pulse *inside* `handle_input`),
`pong.c:267/290/359/380`, `brick_breaker.c:1124`, `samegame.c:713` (a 300–1500 ms render loop that
never polls touch, so the exit button is dead during it).

**Fix:** drive these from the existing non-blocking `LEDEffect`/`get_time_ms()` pattern that
snake, frogger and platformer already use.

---

## Phase 2 — Script safety

### B15. `clone-to-32gb.sh` can destroy a host disk — **most dangerous item in the repo**

`clone-to-32gb.sh:250`. The only blacklist is the literal string `/dev/sda`. The mount guard
(`:269`) misses LVM/LUKS roots (`mount` shows `/dev/mapper/…`, never the disk) and any unmounted
disk. The size gate (`:281`) has a 16 GB **minimum** and no maximum. On this Windows host a
`wsl --mount`ed physical drive appears as `/dev/sdd`/`/dev/sde` and is typically unmounted.
`:355` then runs `dd if="$SOURCE" of="$DEVICE" bs=4M`.

**Fix:** require `/sys/block/$(basename $dev)/removable == 1`; reject the disk backing `/`
(`findmnt -no SOURCE /` → `lsblk -no PKNAME`); add `MAX_TARGET_SIZE_GB`.

### ~~B16. Delete `native_apps/Makefile`~~ — **DONE 2026-07-31**

Deleted. It could not work — `CC = gcc` with `-march=armv7-a` fails on x86, several rules pointed
at moved or deleted files, and `install:` copied x86 binaries into the **host's** `/opt/games` and
dropped an init script into the host's `/etc/init.d`. `build-and-deploy.sh` is the only build path
and always was.

### B17. `commission-roomwizard.sh` sed can wipe the network config

`:244` — `sed -i '/^auto eth0/,/^$/d'` deletes to EOF if the eth0 stanza is last or the file has
no blank lines, taking `auto lo` with it. Device boots with no network and no SSH.

Related, `:103`: `openssl passwd -6 "$PASSWORD"` puts the plaintext password in
`/proc/<pid>/cmdline`, defeating the `read -s` two lines earlier. Use `-stdin`.

### B18. `disable-steelcase.sh` fails silently and skips the watchdog disable

`set -e` at `:22` plus an unguarded `sed` at `:28` — if `/etc/profile` is absent the script dies
**before** `touch /var/watchdog_test` (`:32`), so the Steelcase software watchdog stays armed and
the device reboots every ~70 minutes. It runs on every boot from `roomwizard-app-init.sh:44`, so
the failure is invisible.

**Fix:** `|| true` on the best-effort commands.

### B19. Deploy hygiene

- **No IP validation** (`deploy-all.sh:28`, `setup-device.sh:43`, `native_apps:15`). Only
  `scummvm-roomwizard:35` checks. `./deploy-all.sh vnc_client` (forgetting the IP) builds
  *everything*, including the multi-minute ScummVM build, then fails at `ssh root@vnc_client`.
  Unknown flags are silently ignored and the script proceeds with the full destructive setup.
- **No verification of what landed.** 16 binaries are scp'd then `chmod +x`'d with no check.
  Add `md5sum` comparison and fail on mismatch.
- **`audio_touch_test` is never `chmod +x`'d** (`native_apps:201-211`) — the only deployed binary
  missing from the list. Works today only because scp carries the mode.
- **Only `vnc_client` cd's to its own directory.** The others break when invoked by path;
  they work only because `deploy-all.sh:156` wraps them in a subshell `cd`.
- **`clean.sh`** has no shebang, no `set -e`, no `cd` — run from the repo root its
  `find . -name '*.o' -delete` wipes `native_apps/build/`, `usb_host/modules/` and the ScummVM tree.

### B20. Three component scripts hand-roll the init script's `stop` logic

`native_apps:155`, `vnc_client:89`, `scummvm-roomwizard:557` all duplicate
`killall -9 respawn.sh` + `rm -f …pid`, which is exactly what `do_stop()` exists for — and the
copies have already drifted (each kills a different basename). `CLAUDE.md` says component scripts
must not do this.

**Fix:** replace with `ssh "$DEVICE" '/etc/init.d/roomwizard-app stop'`, end with `restart`.

---

## Phase 3 — Features (all userspace, no kernel work)

### F1. Port audio from OSS to ALSA — **highest user-visible payoff**

**ALSA already works on this kernel** — card `rw20`, `twl4030-hifi ↔ 49022000.mcbsp`, all mainline
drivers, `hw:0,0` present. The "bru-bru-KLICK" stall, the 506 ms period problem and the
ioctl-ordering fragility all live in the `snd-pcm-oss` **emulation shim**, not the hardware.

Rewriting `native_apps/common/audio.c` and
`scummvm-roomwizard/backend-files/oss-mixer.cpp` against ALSA (or tinyalsa) fixes the project's
longest-standing audio complaints with **zero kernel work and zero brick risk**.

While in there, fix the reported OSS bugs so the ALSA version doesn't inherit them:

- `audio.c:84` uses `SNDCTL_DSP_STEREO`, which the file's own comment says is ignored; it never
  verifies the channel count, yet every buffer is sized assuming interleaved stereo.
- `audio.c:378` abandons a chunk mid-frame on a short write, desynchronising L/R permanently.
- `oss-mixer.cpp:298` the emergency anti-underrun `write()` ignores errors and partial writes.

**Update 2026-07-30 — the output is mono, permanently.** The teardown confirmed **one** speaker
(`SPKR1`), **no** 3.5 mm jack and **no** jack footprint (`SYSTEM_ANALYSIS.md#34-audio`). So the
codec's `Headset` stereo path goes nowhere, and the two stereo-related bugs above are best fixed by
**committing to mono end-to-end** rather than by making the interleaved-stereo bookkeeping correct.
Also closes the microphone-as-input idea: there is no mic on the board and no acoustic port.

### F2. Use the DSS overlay planes — **biggest performance win available**

Three hardware overlay planes with a scaler, z-order, global alpha and colour-key, all sitting
unused at `/sys/devices/platform/omapdss/`. On a GPU-less 600 MHz part this is the only graphics
acceleration that exists. Pure sysfs — no kernel work.

Suggested order:

1. **Prove the scaler.** Render at 400×240 into `fb1`, set `overlay0` `input_size=400,240`
   `output_size=800,480`. A quarter of the pixel fill cost for the same visual size. Start with
   one game, then ScummVM and the VNC client.
2. **HUD plane.** Enable `overlay1` (`vid1`) above the game plane with `zorder` + `global_alpha`
   for score bars, pause menus and modal dialogs — composited free, no redraw underneath.
3. **Colour-key transparency** via `trans_key_enabled` for zero-CPU sprite masking.
4. **Video playback**, speculatively. `/dev/video0` (`omap_vout`, V4L2 output) accepts YUV with
   hardware colour-space conversion, which makes a video player plausible on a part that could
   never software-decode one. Furthest from proven of the four.

Also investigate `omap_vout: failed to allocate DMA Channel for video-1` at boot — it may be
exactly what blocks item 4.

⚠️ This is a **legacy omapdss** interface. It is cheap now and would need rewriting as DRM atomic
plane code if the kernel ever changed — which, per current policy, it won't.

### F3. ~~Auto-backlight from the ambient light sensor~~

Closed 2026-07-30 — there is no such hardware. See [Closed](#closed).

### F4. Surface the MADC — temperature and analogue inputs

Readable with `cat` **today**, zero references in the codebase:

- `in_temp1_input` — SoC die temperature. Add a readout to Device Tools (~10 minutes).
- `in_voltage2..7` — six idle general-purpose analogue inputs. A potentiometer on one channel is a
  real analogue paddle for Pong/Breakout; two channels plus `/dev/dsp` is a complete analogue
  controller with no USB at all. Needs a reachable pad — see
  [`SYSTEM_ANALYSIS.md#24-unpopulated-and-expansion`](SYSTEM_ANALYSIS.md#24-unpopulated-and-expansion),
  which describes the cheap software-side way to map a test point to an ADC channel.
- `in_voltage9` — RTC backup cell voltage. A "battery low" warning is nearly free.

### F5. RoomWizard-to-RoomWizard wireless via the 802.15.4 radio

The most *interesting* capability on the board: two-player games across a corridor, high-score
sync, presence beacons — with no network involved.

**Update 2026-07-30 — the hardware side is done. This is now a pure software task.**

The teardown found `J5`+`J6`: a **2×10 / 2 mm-pitch XBee socket, populated but with no module
fitted**, on the bottom side of the board. **None of the three devices has a radio**, so this was
not a per-unit option — the batch shipped without it.

But the mechanical evidence is emphatic. A real Digi XBee (~10 years old, working condition
unknown) was test-fitted and **seats perfectly**: `J5` has a white **pin-1 dot** aligning with the
module's pin 1, and the **metal inner bezel carries a trapezoidal cut-out matching the XBee
outline**. The chassis was tooled for this exact module. There is no longer any question about
what the socket is.

**Staging — one module is enough to de-risk the whole thing:**

1. **Prove the port** with the single module on the touch-broken unit: patch the DTB, insert, and
   see whether the XBee answers `+++` / `ATID`. That validates the DTB patch, the socket wiring
   *and* whether a decade-old module still works — three unknowns for one experiment, no purchase.
2. **Only then buy a second module** for the actual device-to-device link. Two are needed for
   multiplayer; one is enough to prove everything else.

Recovery if the DTB patch misboots: power cycle. `bootcmd` is hardcoded to the untouched
`uImage-system`, and the SD card can be reimaged — see the recovery discussion in
[`SYSTEM_ANALYSIS.md#47-recovery`](SYSTEM_ANALYSIS.md#47-recovery).

**De-risking ladder — the module stays out of the socket until step 4.** `J5` = XBee pins 1–10
(pin 1 is the dotted end), `J6` = pins 11–20; numbering runs down one strip and back up the other
like a DIP, so pins 1 and 10 are at opposite ends of `J5`, not across from each other.

1. ~~**Power and ground.**~~ ✅ **Verified 2026-07-30: `J5` pin 1 reads 3.3 V, `J5` pin 10 is
   ground.** The socket is correctly identified and correctly oriented, and the rail is in spec
   for an XBee — absolute max is 3.6 V, so a 5 V reading would have been a stop. **Powering the
   module is safe.** Not checked, and only worth a glance if the module later misbehaves: pin 5
   (`RESET`) should sit at ~3.3 V released rather than held low, and pin 9 (`SLEEP_RQ`) should not
   be sitting high.
2. **Apply the DTB patch, module still out, then measure `J5` pin 3** (`DIN` = the SoC's TX). An
   idle UART transmitter sits **high**, so a working pinmux shows **~3.3 V** here where a disabled
   UART3 shows floating or low. **This is the cheapest possible proof that the pinmux entry took
   effect** — the genuinely unproven part of this item — and it costs nothing if the patch is wrong.
3. **Insert the module** and try `+++` then `ATID` at **57600 8N1**.

The remaining risk is no longer electrical. An XBee fed reversed dies instantly and there is only
one module, but that question is now settled — what is still unproven is whether the DTB patch can
add a `uart3` pinmux node at all, which is exactly what step 2 measures before the module goes in.

**Expect a Series 1 module to be what the vendor assumed.** `ATCH` and a settable `ATMY` are
802.15.4 (Series 1) commands; on a Series 2 / ZB module `ATMY` is read-only and `ATCH` only reports
the operating channel. An S2 part will still answer `+++` and `ATID`, which is enough for the
"does the port work" test — so do not read a partial `AT` response as a wiring fault. Check the
module label first.

The remaining software work:

- UART3 (`serial@49020000`) is `status = "disabled"` and has no pinmux entry.
- **Possible without kernel source:** the DTB is appended to `uImage-system` and this project
  already binary-patches it (`usb_host/patch_dtb.py`, which recomputes the uImage CRCs correctly).
- ⚠️ Adding a whole pinmux node to a compiled DTB is materially harder than the existing one-word
  power-budget patch, and this is **unproven**. Recovery is the untouched-`uImage-system` trick.
- Protocol reference: `opt/pv02/pv02_app` (XBee AT commands, 57600 baud) and
  `opt/sbin/RoomWizard-zbgatewayd`.

### F6. Multi-touch via direct I2C

The panel controller is 2-point multi-touch with on-chip gestures; `panjit_ts` flattens it to
single-touch. Bypass via `/dev/i2c-2` (node `tsc_panjit@03`: reg `0x03`, IRQ `gpio1[23]`, reset
`gpio1[16]`). Userspace-only. Enables pinch-zoom in ScummVM, two-players-on-one-screen, launcher
gestures.

**Update 2026-07-30 — this item got materially easier.** The teardown identified the controller as
a **Cypress `CY8CTMG120-56LTXI`** PSoC TrueTouch chip ("Panjit" is the *module* vendor, not the
silicon). Its I2C register map is published Cypress documentation, so there is no unknown protocol
to reverse-engineer from bus captures — `pv02_app` drops from *the* reference to a cross-check.
Consider promoting this item; it is userspace-only, so the kernel policy does not touch it.

Cheaper first step: finish `native_apps/hardware_test/pressure_test.c` and determine whether
`ABS_PRESSURE` actually varies. If it does, that is free analogue input (draw thickness,
charge-up shot power, velocity-sensitive keys).

### F7. Use NAND `mtd4` "scratch" for persistent data

11 MB, blank, unused, and it **survives an SD card reflash** — a natural home for high scores and
save games. Writing to `/dev/mtd4` is safe. (`mtd0` is the 12 KB boot redirector and must never
be written.)

### F8. Smooth LED effects

`red_led`/`green_led` are true PWM on dedicated dmtimer channels, and driving both gives amber —
so the palette is red / amber / green with smooth crossfade, visible from outside the room. Ideas:
health/timer bar, heartbeat pulse during ScummVM loading, flash on high score. `hardware.c`
already reaches both channels; this is presentation work only.

---

## Phase 4 — Structural

### C1. Extract the shared evdev layer

Three parallel implementations of device classification, the `/dev/input/event*` scan, the
`/etc/input_config.conf` parser and the hotplug rescan timer:

| Primitive | `common/gamepad.c` | `vnc_client/vnc_input.c` | `roomwizard-events.cpp` |
|---|---|---|---|
| Classifier | `:63` | `:132` | `:174` |
| Scan loop | `:216` | `:235` | `:214` |
| Config parser | `:294` | `:172` | `:429` |
| Rescan timer | `:492` | `:468` | `:1263` |

**They have already drifted** — `MAX_INPUT_DEVICES` is 16 in the VNC client but 32 in the other
two, so a keyboard on `event17` works everywhere except VNC (this constant has already been manually resynced once). The "clear errno before the read loop" hardening exists
only in the ScummVM copy.

The ScummVM copy is defensible (C++, different event model, links only 4 common objects). **The
VNC copy is not** — `vnc_client/Makefile:21-29` already compiles five objects from
`../native_apps/common/`; it could link `gamepad.o` too.

**Fix:** extract classifier + scan + config parser into `common/evdev_scan.c` (~150 lines).
Quick win in the meantime: bump `MAX_INPUT_DEVICES` to 32.

### C2. Split `device_tools.c` (2651 lines)

Five previously-separate GUIs behind a tab enum, sharing nothing but the tab bar. Splitting into
`tab_settings.c` / `tab_diag.c` / `tab_tests.c` / `tab_calib.c` behind a small vtable is
mechanical and costs one line each in `build-and-deploy.sh`.

It would also make C3 deletable.

### ~~C3. De-duplicate the calibration math~~ — **DONE 2026-07-31**

There were three copies of the same safety-critical fit: `device_tools`' Calibration tab, the
standalone `unified_calibrate`, and a private one inside `touch_raw`. They drifted, and the drift
cost a day of measurement to unpick (`SYSTEM_ANALYSIS.md#33-touch`).

Now: one implementation in `common/touch_calib.c`, linked by `device_tools` and `touch_raw`.
`unified_calibrate` is deleted. See B3a/B3d.

### C4. Make the common library use the logger

`common/logger.c` exists and apps use it (`app_launcher` 18 calls, `device_tools` 17), but the
library they all link writes to stdout unconditionally: `touch_input.c` 15 `printf` / 0 `LOG_`;
`gamepad.c` 7/0; `framebuffer.c` 5/0. `touch_init()` alone emits ~5 lines, and `app_launcher`
calls it after **every** child exit, so launcher stdout grows the same banner forever
(see also B21 below).

### B21. `app_stdout.log` is never rotated

`roomwizard-app-init.sh:116`. `rotate_log()` (`:80`) only touches `respawn.log`. A crash-looping
app restarting every 2 s writes forever; the rootfs has under 1 GB. A full rootfs means no config
writes, no high scores, and a failed next deploy.

### C5. Fix `text_truncate` and the 8px/6px font-width confusion

- `common.c:83` `text_truncate()` takes **no destination size** and does `strcpy(dest, upper)`
  (up to 256 bytes) plus `strcat(dest, "...")`. Callers survive on arithmetic luck —
  `device_tools.c:2141` passes a 48-byte buffer for a 128-byte `EVIOCGNAME` string. One geometry
  change from a stack smash. Add a `size_t dest_size` parameter.
- `common.c:389/398/415/423` and `ui_layout.c:326` compute text width as **8 px/char**, but
  `fb_draw_text` advances **6 px/char**. Titles render ~17% left of centre; long strings clip off
  the left edge. Use `text_measure_width()` everywhere.

### C6. Extend the host-buildable test harness

**You already have one** — `native_apps/tests/test_game_selector_scroll.py` (277 lines) does
SSH-launch → `cat /dev/fb0` → inject touch via `touch_inject` → numpy-diff the frames. It is
aimed at the *older* launcher and appears to have been used once.

Refactor the SSH/capture/inject/diff plumbing into `tests/rw_harness.py`, then a ~20-line
`smoke_all_apps.py`: for each `.app` manifest — launch, capture, assert not-all-black, assert the
process is alive after 2 s, kill. That covers the two most common regressions (crash at startup,
renders black) across all ~15 binaries in one command.

⚠️ **Two harness traps found 2026-07-30 while testing the Phase 0 changes** — both cost real time:

- ~~**`touch_inject` is not built.**~~ Fixed — `build-and-deploy.sh` compiles and deploys it, and
  the `.hidden` marker loop no longer names binaries that do not exist.
- **Screen→raw conversion must read `/etc/touch_calibration.conf`, not assume 0..4095.** The
  reference unit is calibrated to `17 4084 -279 4382` (the least-squares fit extrapolates past
  the 12-bit range on Y, hence the out-of-range values). Assuming 0..4095 lands every tap ~30 px
  high, which presents as *intermittent* hits rather than a clean failure — some buttons work,
  some don't, and it looks like device flakiness. Use `raw = screen*(max-min)/(dim-1) + min`.
- **`touch_inject`'s 0..4095 argument clamp is now stale** (`tests/touch_inject.c:83`). It mirrors
  the hardware range, which is correct for *raw* input, but the note that once stood here — "the
  bottom ~34 px cannot be expressed" — was written against the old bad calibration and no longer
  describes anything real. Either teach it to read the config and accept panel coordinates, or
  document precisely that its arguments are raw digitiser counts, not pixels. Small, but it is a
  trap for whoever writes the harness above.

Separately, host-gcc tests over the pure-logic functions, where regressions are invisible until
you're mis-tapping by 30 px. **Started 2026-07-31:** `tests/touch_calib_test.c` covers the
calibration fit end-to-end — it replays the 11 target medians from the reference capture and
asserts `touch_calib_fit()` still lands on `X 17..4084` / `Y -279..4382`, plus the per-axis verdict
and the sanity gate's accept/reject boundaries. Build line is in the file header; it is host gcc,
so `build-and-deploy.sh` does not run it.

Still uncovered and worth the same treatment: `scale_coordinates()`, `parse_args()` (would have
caught the `args=` bug immediately), and the `config.c`/`ppm.c` parsers.

### C7. Run shellcheck

The shell scripts *are* the deployment system and they run as root over SSH.
`shellcheck *.sh */*.sh` — one command, no config, no repo changes.

---

## Out of Scope

Recorded so the decision is not re-litigated. All of these need a kernel rebuild, and **the
vendor kernel source is unavailable** — the repo's `usb_host/linux-4.14.52/` is vanilla upstream
and is missing `CONFIG_TOUCHSCREEN_PANJIT`, the Sharp panel driver, and `omap3-rw20.dts`. A kernel
built from it would boot with no display and no touch. Requesting GPL source from Steelcase has
been explicitly ruled out.

| Item | Why it's blocked |
|------|------------------|
| Enable the two EHCI USB host ports | `CONFIG_USB_EHCI_HCD` unset — **and now doubly dead:** the 2026-07-30 teardown found **no second USB connector and no unpopulated USB footprint** on board rev `550-0204-03`. The ports exist in the SoC and the DT but were never brought out to anything pluggable, so even a kernel rebuild would gain nothing. |
| Fix MUSB DMA properly | `CONFIG_USB_INVENTRA_DMA` and `CONFIG_MUSB_PIO_ONLY` both unset — a genuine build defect. The `/dev/mem` runtime patch stays. |
| `PREEMPT` / `HZ=250` / PREEMPT_RT | Config-only, but still a rebuild. |
| SPI | Four controllers enabled in the DT, `CONFIG_SPI` unset. |
| USB gadget mode (device as a USB keyboard/serial/ethernet) | No `CONFIG_USB_GADGET`. |
| Piezo buzzer on TWL4030 PWM | Needs `CONFIG_PWM_TWL` **and** a wire. All 3 dmtimer PWMs are taken. |
| Mainline 6.x port | Would break runtime bpp switching (ScummVM + VNC), lose the DSS overlay sysfs, and cost RAM. See `SYSTEM_ANALYSIS.md#7-kernel-policy`. |

**Note:** enabling **UART3** for the ZigBee radio (F5) is *not* in this table — it may be reachable
by patching the appended DTB, which needs no kernel source.

---

## Closed

Finished work. Kept only where the *reasoning* is worth not rediscovering; the code and git
history are the record for everything else. IDs are retained so older references still resolve.

### D1. Compiler warnings — **done 2026-07-30**

`WARN="-Wall -Wextra -Wno-unused-parameter"` in `native_apps/build-and-deploy.sh`, interpolated
into all 28 compile lines. Deliberately **not** `-Werror`: a hard failure would block every deploy
over pre-existing noise.

Two things the original write-up got wrong, worth recording:

- **There was no flood** — 29 warnings total across ~30k lines, now **zero**. The tree was in far
  better shape than assumed. The build is at a clean baseline, so the *next* warning is visible.
- **It did not catch B3 or B7,** which the item predicted it would. Neither
  `-Wmaybe-uninitialized` nor `-Wformat` fired anywhere. B7 is unsigned subtraction — legal C that
  no warning flags; B3 is a logic/range problem. **Warnings are not a substitute for reading the
  Phase 1 list.**

One genuine defect surfaced: `game_selector.c:97-98` copied 255/511 bytes into 256/512-byte buffers
with **no NUL termination** and hardcoded sizes (now `snprintf` + `sizeof`). The other 28 were
hygiene: 8 `int`/`uint32_t` sign-compares on `fb->width`, 5 dead variables (including three
leftovers from a per-page launcher navigation scheme that absolute indexing replaced), 9
`-Wstringop-truncation` on `strncpy(n-1)`+manual-NUL, 3 ignored return values, 3 two-statements-on-
one-line indentation traps, 1 `/*` inside a comment.

### D2. sdiv/udiv pre-deploy gate — **done 2026-07-30**

`native_apps/check-arm-safe.sh`, called from `build-and-deploy.sh` before deploy (and on build-only
runs). Verified both ways: passes the 30 real artifacts, and correctly fails a binary built with
`-march=armv7ve`, naming the offending function.

⚠️ **The check this item specified was wrong, and so was the `CLAUDE.md` text it came from.**
`grep 'sdiv\|udiv'` matches the *substring* "udiv" inside the **names** of the software divide
helpers — `__udivsi3` (×20), `__udivmoddi4` (×6) and their call sites. That is the entire source of
the "~45 known-unreachable libc hits" that were supposed to need an allowlist. They are not
instructions, they are not unreachable, and they are not a hazard: they are positive evidence that
division is being done in software. The related claim that the cross-compiler's `libgcc.a` contains
`sdiv`/`udiv` is also false for this toolchain (measured: zero).

Matching the tab-delimited **mnemonic** field instead gives **zero across all 30 artifacts**, so the
gate needs no allowlist and any hit is real. `CLAUDE.md` has been corrected.

### D3. Missing/duplicated files — **done 2026-07-30**

`CLAUDE.md` and `fb565_to_png.py` are tracked. The four redundant framebuffer decoders
(`native_apps/tests/fb_to_png_{16,32}bit.py`, `scummvm-roomwizard/fb_to_png.py`,
`scummvm-roomwizard/convert_fb.py`) are deleted — `fb565_to_png.py` is a superset (both bpp, page
select) and nothing invoked them.

`.gitignore` now exempts `Screenshots/` alongside `HardwarePhotos/`, with a matching LFS rule, so a
doc screenshot no longer needs `git add -f`.

*(A fifth copy of the decode logic is still inlined in
`native_apps/tests/test_game_selector_scroll.py:80` — that one belongs to **C6**, not here.)*

### D4. `.gitattributes` — **done 2026-07-30**

`*.sh text eol=lf`. The CRLF-shebang-vs-BusyBox reasoning now lives as a comment in
`.gitattributes` itself.

### D5. Documentation corrections — **done 2026-07-29**

SoC (OMAP3503), GPIO banks, touch panel type, sensor inventory, deploy modes, compiler-flag claims;
new SoC/display/boot-chain/panel-timing/kernel sections in `SYSTEM_ANALYSIS.md`; per-component
`CLAUDE.md` guides; nine stale docs deleted.

### F3. Auto-backlight from an ambient light sensor — **closed 2026-07-30, no such hardware**

Kept because it will otherwise be re-proposed. The full teardown found no sensor and — decisively —
**no aperture, window or light pipe anywhere in the enclosure**. The case is light-tight, so a
sensor would have nothing to sense even if populated. The vendor factory test's I2C-bus-1
light-sensor step is shared firmware for a product family in which this SKU is not the variant with
the sensor. Don't probe: `pv02_app 5` can hang the bus, and bus 1 carries the PMIC.

**Salvage:** *time-of-day* dimming needs no sensor — there is an RTC and
`/sys/class/leds/backlight/brightness` works. Fix **B9** first; auto-dimming on a broken setter
makes things worse.

### Serial console — **declined 2026-07-30**

`P4` was located and its pinout verified, but the recovery loop is *pull the SD card, reimage, DHCP,
SSH* — and since the standing rules keep NAND and U-Boot untouched, the card **is** the entire
failure surface. Serial would add boot visibility, not recovery capability. Revisit only if NAND or
U-Boot ever get written.

---

## Suggested order of work

1. ~~**Phase 0 entirely.**~~ **Done 2026-07-30** except D6's password rotation, which is an action
   on the VNC server rather than a code change. It surfaced one real defect, not the expected
   flood — see [Closed](#closed).
2. **B1, B2, B3, B4, B5, B6** — the crash/wedge class. ← **next**
3. **B15, B16** — stop the scripts from being able to hurt you.
4. **F1 (ALSA)** — biggest user-visible improvement in the project.
5. **Deep clean the device** (`--deep-clean`), then **F2 (DSS overlays)**.
6. ~~**Open the unit once** and inspect the hardware.~~ **Done 2026-07-30.** Full teardown; every
   question answered and folded into [`SYSTEM_ANALYSIS.md`](SYSTEM_ANALYSIS.md) (parts inventory,
   connectors, unpopulated headers, photo index). Serial console declined — see
   [Closed](#closed).
7. Everything else as appetite allows.
