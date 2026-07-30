# Improvement Plan

Prioritised backlog from the full code + hardware review of 2026-07-29.

**How to read this**

- **B**n = bug, **F**n = feature, **D**n = doc/infra, **C**n = cleanup.
- **Verified** means someone actually read the code or ran the command and confirmed it.
  **Reported** means a reviewer found it but it has not been independently re-checked — still
  likely real, but confirm before you spend an afternoon on it.
- Nothing in this plan requires a kernel rebuild. Items that would are listed at the bottom
  under [Out of Scope](#out-of-scope).

**Ground rules discovered during the review** — worth internalising before touching anything:

- The device is an **OMAP3503** (Cortex-A8, no GPU, no DSP).
- Every shipped binary is `-static`, which is why aggressive rootfs cleanup is safe.
- Recovery is cheap: the SD card is removable and `bootcmd` always loads `uImage-system`, so
  staging experiments under a different filename is a free undo.

---

## Phase 0 — Do these first (no risk, high leverage)

Nothing here can break a running device.

### D1. Turn on compiler warnings ✅ *highest value per minute in this document*

**Status:** Verified. `native_apps/build-and-deploy.sh` compiles all 28 targets with bare
`$CC -O2 -static`. **No `-Wall`, no `-Wextra`.** ~30k lines of shipping C compiled with warnings off.

Add near `CC=` (line ~34) and interpolate into every compile line:

```sh
WARN="-Wall -Wextra -Wno-unused-parameter"
```

Expect a flood on first run. Even fixing only `-Wmaybe-uninitialized` and `-Wformat` hits is worth
it — several bugs below (B3, B7) are exactly what those flags catch.

### D2. Add the sdiv/udiv pre-deploy gate

**Status:** Verified — the check is fully specified in `CLAUDE.md` and **zero scripts implement it**
(`grep -r sdiv *.sh` returns nothing). The failure mode it guards against is the worst one in the
project: SIGILL, exit 132, blank screen, no output.

Create `native_apps/check-arm-safe.sh`: run the documented `objdump` incantation, allowlist the
~45 known-unreachable libc hits (`_dl_*`, `hack_digit`, `_i18n_number_rewrite`, `__aeabi_ldivmod`,
`__udivmoddi4`), fail if any hit lands in an app's own function. Call it before each `scp`.

Follows the existing `native_apps/check-evdev.sh` pattern.

### D3. Commit the things that are missing

- `CLAUDE.md` — **untracked**. The authoritative instruction file exists on one disk.
- `fb565_to_png.py` — **untracked**, yet `CLAUDE.md` names it as *the* verification tool.
  Meanwhile four *other* framebuffer decoders are tracked
  (`native_apps/tests/fb_to_png_{16,32}bit.py`, `scummvm-roomwizard/fb_to_png.py`,
  `scummvm-roomwizard/convert_fb.py`). Commit the canonical one, delete the other four.
- Root cause: `.gitignore` blanket-ignores `*.png`/`*.jpg`/`*.raw`. Sensible for scratch captures,
  but it means no doc screenshot can be committed without `-f`.

### D4. Add `.gitattributes` — **done 2026-07-30**

**Status:** Verified. All shell scripts are currently LF in index and worktree — but this machine
has `core.autocrlf=true` system-wide, so that is luck, not policy. `roomwizard-app-init.sh` and
`disable-steelcase.sh` are **scp'd byte-for-byte onto the device**; a CRLF normalisation produces
`#!/bin/sh\r`, which BusyBox rejects with a confusing "no such file or directory" — the device
boots to a black screen and the init script never runs.

```gitattributes
*.sh text eol=lf
```

### D5. Documentation corrections — **done 2026-07-29**

Recorded here for history:

Corrected the SoC (OMAP3503), GPIO bank count, touch panel type, sensor inventory, deploy
modes and compiler-flag claims across all docs; added SoC identification, display stack, boot
chain, panel timings, kernel assessment and unused-hardware sections to `SYSTEM_ANALYSIS.md`.
Dissolved `native_apps/PROJECT.md` into `native_apps/CLAUDE.md` + `README.md` + this file, and
added per-component `CLAUDE.md` authoring guides. Deleted nine stale/duplicated docs.

### D6. Secrets — **partly done 2026-07-29**

`vnc_client/vnc_client.conf` held a plaintext password and was tracked. Now untracked +
gitignored, with `vnc_client.conf.example` as the template and `chmod 600` on deploy.

⚠️ **Still to do:** the password remains in git history. For a LAN VNC password the pragmatic fix
is to **change the password on the VNC server** rather than rewrite history. Do that before
making the repo public.

---

## Phase 1 — Correctness bugs

Ordered by (severity × likelihood of being hit).

### B1. 16bpp framebuffer heap overflow — **worst latent bug in the tree**

`native_apps/common/framebuffer.c:248` allocates the back buffer as
`width * height * bytes_per_pixel`, but `fb_clear` (`:319`) memsets
`width * height * sizeof(uint32_t)` and `fb_draw_pixel` (`:342`) writes `uint32_t`. **At 16bpp
that is a 768 KB heap overflow.** `fb_init` never rejects a non-32bpp mode.

Found independently by two reviewers. Currently survived only because `vnc_client` hand-writes
`uint16_t` and never calls a common draw helper. Triggered by: any game launched while a crashed
ScummVM/VNC left the panel at 16bpp, or one `fb_draw_text` added to the VNC client.

Also: `app_launcher` guards with `fb_set_bpp(32)` at startup, but `game_selector` (`:439`),
`device_tools` (`:2486`), `hardware_config` (`:178`) and `unified_calibrate` (`:143`) do not.

**Fix:** always allocate `width * height * 4`; make `fb_init` fail loudly if
`bits_per_pixel != 32`; add the missing `fb_set_bpp(dev, 32)` calls.

### B2. Gamepad buttons latch on and never release

`native_apps/common/gamepad.c:791` (virtual touch regions) and `:649` (analog stick → D-pad) both
set `.held = true`, and **nothing anywhere ever clears it** — `gamepad_poll` deliberately doesn't
(`:828`), and no caller does.

- platformer: one tap on the virtual left pad → the player runs left forever.
- frogger/snake (which read `.pressed`): first tap works, every later tap in that region is dead.

**Fix:** keep touch/stick-derived state in a separate per-frame bitmask and OR it with the latched
key/hat state each poll.

### B3. A bad calibration can wedge the device with no recovery

Three compounding problems:

1. `touch_input.c:244` only rejects raw ranges narrower than 16 counts, so a skewed fit
   (e.g. `0..60000`) is accepted, saved to `/etc/touch_calibration.conf`, and auto-loaded by
   every app.
2. Phase-2 ACCEPT/REDO buttons are hit-tested **through the new calibration**
   (`device_tools.c:1827`) — if the fit is bad you can press neither.
3. `touch_input.c:155` `touch_wait_for_press_raw` has `while(1)` with no `else` on the read: any
   read error spins at 100% CPU forever, `return -1` is unreachable dead code, and glibc
   `signal()` implies `SA_RESTART` so SIGTERM won't break it either.

**Fix:** sanity-check the fitted range against the hardware `EVIOCGABS` range (reject if overlap
< 50%); add a **RESET CALIBRATION** button; add a phase-2 timeout that reverts; handle the read
error and use `poll()` so the loop can observe `running`.

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

### B16. Delete `native_apps/Makefile`

It **cannot work**: `CC = gcc` with `-march=armv7-a` fails on x86; three rules point at moved
files (`game_selector.c`, `watchdog_feeder.c`, `game-mode-init.sh` — the last doesn't exist);
and `install:` (`:169`) copies x86 binaries into the **host's** `/opt/games` and drops an init
script into the host's `/etc/init.d`.

`CLAUDE.md` and `native_apps/CLAUDE.md` both say it is not the deployment path.
`build-and-deploy.sh` is a complete replacement.

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
(`SPKR1`), **no** 3.5 mm jack and **no** jack footprint (`HARDWARE_INSPECTION.md` §E). So the
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

Also investigate `omap_vout: failed to allocate DMA Channel for video-1` at boot.

⚠️ This is a **legacy omapdss** interface. It is cheap now and would need rewriting as DRM atomic
plane code if the kernel ever changed — which, per current policy, it won't.

### F3. ~~Auto-backlight from the ambient light sensor~~ — **CLOSED 2026-07-30, no such hardware**

Was: a wall display at 100% backlight in a dark corridor at 3 am is a genuine annoyance.

**Closed by** [`HARDWARE_INSPECTION.md`](HARDWARE_INSPECTION.md#d-ambient-light-sensor) §D. The
full teardown (bezel separated from LCD and board) found **no sensor and — decisively — no
aperture, window or light pipe anywhere in the enclosure**. The case is light-tight, so a sensor
would have nothing to sense even if one were populated. The vendor factory test's I2C-bus-1
light-sensor step is shared firmware for a product family in which this SKU is not the variant
with the sensor. No probe needed; don't run `pv02_app 5` (it can hang the bus, and bus 1 carries
the PMIC).

**Salvage:** *time-of-day* backlight dimming needs no hardware at all — the device has an RTC and
`/sys/class/leds/backlight/brightness` already works. That is a small, self-contained feature if
the original annoyance is still worth solving. See also **B9**, which must be fixed first: the
backlight get/set asymmetry permanently dims the panel, and any auto-dimming built on top of a
broken setter will make things worse.

### F4. Surface the MADC — temperature and analogue inputs

Readable with `cat` **today**, zero references in the codebase:

- `in_temp1_input` — SoC die temperature. Add a readout to Device Tools (~10 minutes).
- `in_voltage2..7` — six idle general-purpose analogue inputs. A potentiometer on one channel is a
  real analogue paddle for Pong/Breakout; two channels plus `/dev/dsp` is a complete analogue
  controller with no USB at all. Needs a reachable pad — see `HARDWARE_INSPECTION.md` §G.
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
[`HARDWARE_INSPECTION.md`](HARDWARE_INSPECTION.md#a-uart--serial-console--highest-priority-declined-2026-07-30).

⚠️ **One check before first power-on with the module inserted:** confirm 3.3 V on `J5` pin 1 and
GND on pin 10 with the socket empty. The fit is mechanically keyed and pin 1 is dotted, so this is
belt-and-braces — but an XBee fed reversed dies instantly, and there is only one module.

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

### C3. De-duplicate the calibration math

`device_tools.c:1657` is literally commented *"Calibration Tab (from unified_calibrate.c)"*, and
`:1719-1800` duplicates `unified_calibrate.c:42-120`. Both binaries still ship, and
`device_tools` was documented as *replacing* `unified_calibrate`, but the older binary was never removed.
Two copies of safety-critical math that must stay in sync.

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

Separately, a `tests/host_tests.c` with plain `assert()` compiled by **host** gcc would cover the
pure-logic functions where regressions are invisible until you're mis-tapping by 30 px:
`touch_fit_axis_range()`, `scale_coordinates()`, `parse_args()` (would have caught the `args=`
bug immediately), and the `config.c`/`ppm.c` parsers.

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
| Mainline 6.x port | Would break runtime bpp switching (ScummVM + VNC), lose the DSS overlay sysfs, and cost RAM. See `SYSTEM_ANALYSIS.md#kernel-upgrade-assessment`. |

**Note:** enabling **UART3** for the ZigBee radio (F5) is *not* in this table — it may be reachable
by patching the appended DTB, which needs no kernel source.

---

## Suggested order of work

1. **Phase 0 entirely** — a couple of hours, zero risk, and D1/D2 will surface more bugs.
2. **B1, B2, B3, B4, B5, B6** — the crash/wedge class.
3. **B15, B16** — stop the scripts from being able to hurt you.
4. **F1 (ALSA)** — biggest user-visible improvement in the project.
5. **Deep clean the device** (`--deep-clean`), then **F2 (DSS overlays)**.
6. ~~**Open the unit once** and work through `HARDWARE_INSPECTION.md` §A and §H together.~~
   **Done 2026-07-30 — the whole checklist is answered**, except §A, which is **declined**: the
   console header was located (`P4`, RS-232 behind a MAX3232) but will not be wired up. The
   recovery loop is **pull the SD card, reimage, DHCP, SSH** — and since the standing rules keep
   NAND and U-Boot untouched, the card *is* the entire failure surface, so serial would add boot
   visibility rather than recovery capability. Revisit only if NAND or U-Boot ever get written.
7. Everything else as appetite allows.
