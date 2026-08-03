# Improvement Plan

Prioritised backlog from the full code + hardware review of 2026-07-29.

**How to read this**

- **B**n = bug, **F**n = feature, **D**n = doc/infra, **C**n = cleanup. IDs are never reused or
  renumbered — they are referenced from commit messages and session handovers.
- **Status is one word after the heading**, and there are only six:

  | In the live phases | Means |
  |---|---|
  | `open` | Nothing has shipped. Found by reading the code; not reproduced on the device. |
  | `open, confirmed <date>` | Reproduced — on the panel or by running the command. Still unfixed. |
  | `partly done <date>` | Some of it shipped; the entry describes only what is left. |

  | In [Closed](#closed) | Means |
  |---|---|
  | `done <date>` | Fixed and shipped. |
  | `closed <date>` | Not a defect, or the hardware does not exist. Kept so it is not re-proposed. |
  | `declined <date>` | Real, understood, deliberately not doing it. |

  In verification tables, a result is **pass** or **unverified** — never a tick, which used to mean
  "confirmed defect" in the headings and "verification passed" in the tables.
- Finished items live in [Closed](#closed): one line each, except the three whose reasoning is the
  only record of *why* a subsystem is shaped the way it is (B3c, B3e, B22), which are kept in full.
  Where an archived item still has open work, that residue stays in the live phase as `partly done`
  and points here.
- Nothing in this plan requires a kernel rebuild. Items that would are listed under
  [Out of Scope](#out-of-scope).

**Before starting anything here, read [`SYSTEM_ANALYSIS.md`](SYSTEM_ANALYSIS.md) §1 — *Read this
first*.** Device facts live there, not in this plan: what the silicon is, the rules that prevent a
brick, and why recovery is cheap. This file holds only what we intend to *do* about them, and every
item below links to the section that describes the hardware it touches.

---

## Phase 0 — Do these first (no risk, high leverage)

Nothing here can break a running device. D1–D5 are done — see [Closed](#closed). Only D6 remains,
and its remaining step is not a code change.

### D6. Secrets — partly done 2026-07-29

`vnc_client/vnc_client.conf` held a plaintext password and was tracked. Now untracked +
gitignored, with `vnc_client.conf.example` as the template and `chmod 600` on deploy.

⚠️ **Still to do:** the password remains in git history. For a LAN VNC password the pragmatic fix
is to **change the password on the VNC server** rather than rewrite history. Do that before
making the repo public.

---

## Phase 1 — Correctness bugs

Ordered by (severity × likelihood of being hit).

### B3c. Second-unit measurement of the touch dead band — partly done 2026-08-01

The fix shipped and is archived in full at [B3c in Closed](#b3c-edge-bands-that-could-not-be-touched--done-2026-08-01-evening) —
read that before touching the touch model, because this item has been wrong in both directions.

**The only part left: measure a second unit** with `/opt/games/touch_raw` (SWEEP then INSET on all
four edges). Every number in the archived entry is RW09 only. What a second unit settles is whether
the ~30 px Y band generalises — if it varies per panel, the runtime measurement already handles it
and **no code changes**. Save the `/tmp/touch_raw.tsv` capture into the repo before the device
reboots: the **wizard writes no tsv**, only the diagnostic does, which is why RW09's live 18:50
calibration has no capture of its own and the 16:53 one remains the reference (see
`SYSTEM_ANALYSIS.md#33-touch` → *Provenance*).

### B3g. ScummVM's `rw_content_area` is invisible until you know it exists — open, confirmed 2026-08-01

`rwFullContentArea()` (`roomwizard.cpp`) reads the key with `ConfMan.hasKey("rw_content_area")` and
never writes it. ConfMan only persists keys that were *set*, so the line **does not appear** in any
`scummvm.ini` — the option is undiscoverable from the device, and the first thing a user does is look
in the file, not find it, and conclude it does not work. (Reported 2026-08-01, exactly that way.)

`ConfMan.registerDefault()` does not help: registered defaults are not written to the file either.

Fix options, cheapest first:

- `ConfMan.setAndFlush("rw_content_area", "safe")` on first run when `!hasKey()`, so the key is
  present and self-documenting from then on. One line, and it makes the file the discovery surface.
- Expose it in the GUI. ScummVM's Options dialog is upstream code, so this means a backend-specific
  tab — much more work. B3f solved the same "no UI" problem on the VNC side by shrinking that
  screen's row pitch, which is not a move that is available here.

Until then the reliable route is the environment variable `ROOMWIZARD_CONTENT_AREA=visible`, which
takes precedence over the config and needs no file at all. It is documented in
`scummvm-roomwizard/README.md`; the config key should not be documented as the *primary* route while
this is open.

### B3h. ScummVM's config file location depends on the working directory — open, confirmed 2026-08-01

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

### B25. `vnc_client` sets a process title the deploy scripts cannot kill — open, confirmed 2026-08-02

`vnc_client` presents its `cmdline` as `VNC Client` (with a space), so `killall vnc_client` matches
nothing. On RW09 2026-08-02 an instance survived `deploy-all.sh`, two `/etc/init.d/roomwizard-app
stop` calls and a service restart, holding `/dev/fb0` at 16bpp and repainting over every app that
tried to start — `app_launcher` came up and could not keep the screen. It had to be killed by PID.
Diagnosing it is also harder than it should be: busybox `ps w` lists only processes with a TTY, so
the survivor is invisible unless you walk `/proc/*/cmdline`.

This is the concrete failure B20 predicts ("each kills a different basename") and it argues for
raising B20's priority: the shared `/etc/init.d/roomwizard-app stop` path must match on the
*executable*, not a title the process chose.

### B10. ScummVM `getMillis()` overflows at 24.85 days — open

`scummvm-roomwizard/backend-files/roomwizard.cpp:184`. Correctly baselined to start time, but
`(curTime.tv_sec - _startTime.tv_sec) * 1000` is evaluated in 32-bit signed `time_t`. A wall
display is always on; the reference unit is already at 7 days. Long-press detection, cursor
timing, touch-feedback fade and `DefaultTimerManager` all break simultaneously.

**Fix:** `(uint32)(curTime.tv_sec - _startTime.tv_sec) * 1000u + …`.

### B11. VNC: framebuffer leaked on every reconnect — open

`vnc_client/vnc_client.c:632`. `vnc_malloc_fb` mallocs `width*height*4`; `rfbClientCleanup()`
frees `raw_buffer`/`ultra_buffer`/`desktopName`/`serverHost` but **not** `frameBuffer`, and
nothing else does either. With `RECONNECT_MAX_ATTEMPTS 0` (unlimited) and a 1080p host that is
~8.3 MB per drop on a 234 MB device — OOM after ~25 reconnects.

**Fix:** `free(g_vnc_client->frameBuffer)` before `rfbClientCleanup()`.

### B12. VNC: no dead-peer detection — open

`vnc_client/vnc_client.c:589`. `WaitForMessage(…, 10000)` returns 0 on timeout and the loop just
spins; libvncclient 0.9.14 sets no `SO_KEEPALIVE` and the client never pings. A silent TCP death
(AP drop, NAT idle timeout, VM suspend) leaves a stale frame on screen forever — "connection lost"
never logs and the reconnect UI never appears.

**Fix:** track the time of the last successful `HandleRFBServerMessage` and break after N seconds;
set `SO_KEEPALIVE` after `rfbInitClient`.

### B12b. ScummVM: exiting a game quits ScummVM instead of returning to the launcher — open, confirmed

`OSystem_RoomWizard::quit()` calls `exit()` unconditionally rather than setting a flag that lets
the main loop fall back to the ScummVM launcher. The same build returns to the launcher correctly
on Ubuntu. Compare with the SDL backend's `_quit` flag + launcher loop.

Diagnosed but unfixed; was recorded only in `scummvm-roomwizard/SCUMMVM_DEV.md`.

### B12c. ScummVM: OPL tempo unverified after the mono-mixer fix — open

Open verification task — play the KQ3 intro on the device and compare against a reference
recording. The mono mixer and the `SOUND_PCM_READ_RATE` read-back were supposed to fix half-speed
OPL; nobody confirmed it on hardware.

### B13. Game-specific bugs — open

Fixed rows (B13a, B13c, B13d, B13g, B13k, B13l) are in [Closed](#closed).

| # | Where | Bug |
|---|-------|-----|
| B13b | `brick_breaker.c:459` | Indestructible bricks use `health = -1`, which every other site reads as "destroyed" (`health <= 0`) → from level 5 they are invisible and have no collision. |
| B13e | `tetris.c:567` | No wall kick — an I-piece rotated vertically against the right wall can never rotate back. |
| B13f | `snake.c:320` | Growing exposes a stale `body[]` slot: a detached cell is drawn for one tick and stepping on it ends the game. |
| B13h | `brick_breaker.c:535` | Speed power-ups compound — `effect_mult` is never divided out, so SLOW DOWN can make the ball faster. |
| B13i | `platformer.c:951` | Stomping two overlapping enemies kills the player (no `break` after a successful stomp). |
| B13j | `samegame.c:250` | `pixel_to_grid` truncates toward zero, so taps up to one block outside the left/top edge select row/column 0. |

### B14. Blocking `usleep()` inside input/update paths — open

Up to ~1.2 s of frozen UI: `tetris.c:286` (game-over LED pulse *inside* `handle_input`),
`pong.c:267/290/359/380`, `brick_breaker.c:1124`, `samegame.c:713` (a 300–1500 ms render loop that
never polls touch, so the exit button is dead during it).

**Fix:** drive these from the existing non-blocking `LEDEffect`/`get_time_ms()` pattern that
snake, frogger and platformer already use.

### B22. Game-over screen — one row still unverified — partly done 2026-08-02

The fix and its follow-up re-fix shipped and are archived in full at
[B22 in Closed](#b22-the-game-over-screen-only-appeared-after-a-tap--done-2026-08-02) — including the
`NAME_ENTRY` exception, which is the one place where the "fall through in the same call" instinct is
wrong. Read it before touching `gameover_update()`.

**What is left is verification, and it needs hardware:**

| Game | Status |
|---|---|
| tetris, snake, frogger, samegame, brick_breaker, pong | pass — confirmed on the panel 2026-08-02, see the archived table for what each one exercised |
| platformer | **unverified** — needs a **USB keyboard or gamepad** attached. B13k made it controller-only, so B13a's `BTN_ID_BACK` exit cannot be checked without one. Also confirm `RESTART`/`EXIT` respond to touch. |

Not script-verifiable past the first screen (no `/dev/uinput` — see C6), so this row needs a human at
the panel. `RESET SCORES` on an empty table is the cheap way to reach the name-entry keyboard: it
makes any subsequent score qualify.

---

## Phase 2 — Script safety

### B15. `clone-to-32gb.sh` can destroy a host disk — open, **most dangerous item in the repo**

`clone-to-32gb.sh:250`. The only blacklist is the literal string `/dev/sda`. The mount guard
(`:269`) misses LVM/LUKS roots (`mount` shows `/dev/mapper/…`, never the disk) and any unmounted
disk. The size gate (`:281`) has a 16 GB **minimum** and no maximum. On this Windows host a
`wsl --mount`ed physical drive appears as `/dev/sdd`/`/dev/sde` and is typically unmounted.
`:355` then runs `dd if="$SOURCE" of="$DEVICE" bs=4M`.

**Fix:** require `/sys/block/$(basename $dev)/removable == 1`; reject the disk backing `/`
(`findmnt -no SOURCE /` → `lsblk -no PKNAME`); add `MAX_TARGET_SIZE_GB`.

### B17. `commission-roomwizard.sh` sed can wipe the network config — open

`:244` — `sed -i '/^auto eth0/,/^$/d'` deletes to EOF if the eth0 stanza is last or the file has
no blank lines, taking `auto lo` with it. Device boots with no network and no SSH.

Related, `:103`: `openssl passwd -6 "$PASSWORD"` puts the plaintext password in
`/proc/<pid>/cmdline`, defeating the `read -s` two lines earlier. Use `-stdin`.

### B18. `disable-steelcase.sh` fails silently and skips the watchdog disable — open

`set -e` at `:22` plus an unguarded `sed` at `:28` — if `/etc/profile` is absent the script dies
**before** `touch /var/watchdog_test` (`:32`), so the Steelcase software watchdog stays armed and
the device reboots every ~70 minutes. It runs on every boot from `roomwizard-app-init.sh:44`, so
the failure is invisible.

**Fix:** `|| true` on the best-effort commands.

### B19. Deploy hygiene — open

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

### B20. Three component scripts hand-roll the init script's `stop` logic — open, drift confirmed 2026-08-02

`native_apps:155`, `vnc_client:89`, `scummvm-roomwizard:557` all duplicate
`killall -9 respawn.sh` + `rm -f …pid`, which is exactly what `do_stop()` exists for — and the
copies have already drifted (each kills a different basename). `CLAUDE.md` says component scripts
must not do this. This is the deploy-script half of B6, which was left open when B6 shipped.

**The drift is no longer hypothetical — see B25.** A `vnc_client` whose process title is `VNC Client`
survived a full `deploy-all.sh` plus two `stop` calls and had to be killed by PID, meanwhile holding
the framebuffer at 16bpp and repainting over `app_launcher`. Consolidating on one `stop` is necessary
but **not sufficient**: whatever that single implementation matches on must be the executable, not a
title the process chose for itself.

**Fix:** replace with `ssh "$DEVICE" '/etc/init.d/roomwizard-app stop'`, end with `restart`.

---

## Phase 3 — Features (all userspace, no kernel work)

### F1. Port audio from OSS to ALSA — open, **highest user-visible payoff**

**ALSA already works on this kernel**, and the "bru-bru-KLICK" stall, the 506 ms period problem and
the ioctl-ordering fragility all live in the `snd-pcm-oss` **emulation shim**, not the hardware —
see [`SYSTEM_ANALYSIS.md#34-audio`](SYSTEM_ANALYSIS.md#34-audio) for the card, the mixer path and the
four OSS bugs in detail.

Rewriting `native_apps/common/audio.c` and
`scummvm-roomwizard/backend-files/oss-mixer.cpp` against ALSA (or tinyalsa) fixes the project's
longest-standing audio complaints with **zero kernel work and zero brick risk**.

While in there, fix these three so the ALSA version doesn't inherit them:

- `audio.c:84` uses `SNDCTL_DSP_STEREO`, which the file's own comment says is ignored; it never
  verifies the channel count, yet every buffer is sized assuming interleaved stereo.
- `audio.c:378` abandons a chunk mid-frame on a short write, desynchronising L/R permanently.
- `oss-mixer.cpp:298` the emergency anti-underrun `write()` ignores errors and partial writes.

**Decision 2026-07-30 — commit to mono end-to-end.** The hardware is permanently mono (one speaker,
no jack, no jack footprint, no mic — `SYSTEM_ANALYSIS.md#34-audio`), so the two stereo bugs above are
fixed by *removing* the interleaved-stereo bookkeeping rather than by making it correct. This also
closes the microphone-as-input idea.

### F2. Use the DSS overlay planes — open, **biggest performance win available**

Three hardware overlay planes with a scaler, z-order, global alpha and colour-key, sitting unused.
On a GPU-less 600 MHz part this is the only graphics acceleration that exists, and it is pure sysfs —
no kernel work. Inventory, the live sysfs dump and the legacy-omapdss caveat:
[`SYSTEM_ANALYSIS.md#32-display`](SYSTEM_ANALYSIS.md#32-display).

Suggested order:

1. **Prove the scaler.** Render at 400×240 into `fb1`, set `overlay0` `input_size=400,240`
   `output_size=800,480`. A quarter of the pixel fill cost for the same visual size. Start with
   one game, then ScummVM and the VNC client.
2. **HUD plane.** Enable `overlay1` (`vid1`) above the game plane with `zorder` + `global_alpha`
   for score bars, pause menus and modal dialogs — composited free, no redraw underneath.
3. **Colour-key transparency** via `trans_key_enabled` for zero-CPU sprite masking.
4. **Video playback**, speculatively — `/dev/video0` accepts YUV with hardware colour-space
   conversion. Furthest from proven of the four, and the boot-time `omap_vout: failed to allocate
   DMA Channel for video-1` may be exactly what blocks it.

⚠️ Cheap today, but it would need rewriting as DRM atomic plane code if the kernel ever changed —
which, per current policy, it won't.

### F3. Auto-backlight from the ambient light sensor — closed 2026-07-30

No such hardware. Full reasoning, and the salvageable time-of-day alternative, in
[Closed](#closed).

### F4. Surface the MADC — temperature and analogue inputs — open

Three MADC channels are readable with `cat` **today** and have zero references in the codebase
([`SYSTEM_ANALYSIS.md#311-adc-and-temperature-twl4030-madc`](SYSTEM_ANALYSIS.md#311-adc-and-temperature-twl4030-madc)):

- `in_temp1_input` — SoC die temperature. Add a readout to Device Tools (~10 minutes).
- `in_voltage2..7` — six idle general-purpose inputs. A potentiometer on one channel is a real
  analogue paddle for Pong/Breakout; two channels plus `/dev/dsp` is a complete analogue controller
  with no USB at all. Needs a reachable pad — §2.4 describes the cheap way to map a test point to a
  channel without a teardown.
- `in_voltage9` — RTC backup cell voltage. A "battery low" warning is nearly free.

### F5. RoomWizard-to-RoomWizard wireless via the 802.15.4 radio — open

The most *interesting* capability on the board: two-player games across a corridor, high-score
sync, presence beacons — with no network involved.

**The hardware side is settled; this is now a pure software task.** The board carries a populated
but empty XBee socket (`J5`/`J6`), the chassis was tooled for that exact module, and the socket's
orientation and 3.3 V rail were measured on 2026-07-30 — so powering a module is safe. Socket,
pinout and measurements:
[`SYSTEM_ANALYSIS.md#24-unpopulated-and-expansion`](SYSTEM_ANALYSIS.md#24-unpopulated-and-expansion).
Vendor protocol references, the `ttyS2`→UART3 mapping and the Series 1 vs Series 2 `AT` caveat:
[`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports).

**The one unproven thing is the DTB pinmux edit.** UART3 is `status = "disabled"` with no pinmux
entry. The DTB is appended to `uImage-system` and this project already binary-patches it
(`usb_host/patch_dtb.py`, which recomputes the uImage CRCs correctly) — but adding a whole pinmux
node is materially harder than the existing one-word power-budget patch and **has never been done**.
Recovery if it misboots is a power cycle: `bootcmd` is hardcoded to the untouched `uImage-system`
([`#47-recovery`](SYSTEM_ANALYSIS.md#47-recovery)).

**Staging — one module is enough to de-risk the whole thing, and it stays out of the socket until
step 3:**

1. **Patch the DTB, module still out, and measure `J5` pin 3** (`DIN`, the SoC's TX). ~3.3 V means
   the pinmux entry took effect; floating or low means it didn't. This is the cheapest possible proof
   of the only genuinely unproven part, and it costs nothing if the patch is wrong.
2. **Check the module label** for Series 1 vs Series 2 before reading any partial `AT` response as a
   wiring fault.
3. **Insert the module** and try `+++` then `ATID` at 57600 8N1. That validates the DTB patch, the
   socket wiring *and* whether a decade-old module still works — three unknowns, one experiment, no
   purchase.
4. **Only then buy a second module** for the actual device-to-device link. Two are needed for
   multiplayer; one is enough to prove everything else.

There is only one module and an XBee fed reversed dies instantly, which is why step 1 comes before
step 3 and why the orientation was measured first.

### F6. Multi-touch via direct I2C — open

The panel controller is 2-point multi-touch with on-chip gestures and `panjit_ts` flattens it to
single-touch. Bypass the driver via `/dev/i2c-2` — userspace-only, so the kernel policy does not
touch this. Enables pinch-zoom in ScummVM, two-players-on-one-screen, launcher gestures.

**Materially easier than it looks:** the controller is a Cypress PSoC part whose I2C register map is
**published documentation**, so there is no unknown protocol to reverse-engineer from bus captures.
Part number, node, reg address, IRQ and reset GPIOs:
[`SYSTEM_ANALYSIS.md#33-touch`](SYSTEM_ANALYSIS.md#33-touch). **Consider promoting this item.**

Cheaper first step: finish `native_apps/hardware_test/pressure_test.c` and determine whether
`ABS_PRESSURE` actually varies. If it does, that is free analogue input (draw thickness,
charge-up shot power, velocity-sensitive keys).

### F7. Use NAND `mtd4` "scratch" for persistent data — open

`mtd4` is 11 MB of blank, unused NAND that **survives an SD card reflash** — a natural home for high
scores and save games, and safe to write. (`mtd0` must never be written; see
[`SYSTEM_ANALYSIS.md#43-nand-is-effectively-unused`](SYSTEM_ANALYSIS.md#43-nand-is-effectively-unused)
for the partition map and why.)

### F8. Smooth LED effects — open

The two LEDs are true PWM and drive to red / amber / green with smooth crossfade, visible from
outside the room ([`SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm`](SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm)).
Ideas: health/timer bar, heartbeat pulse during ScummVM loading, flash on high score. `hardware.c`
already reaches both channels; this is presentation work only.

---

## Phase 4 — Structural

### C1. Extract the shared evdev layer — open

Three parallel implementations of device classification, the `/dev/input/event*` scan, the
`/etc/input_config.conf` parser and the hotplug rescan timer:

| Primitive | `common/gamepad.c` | `vnc_client/vnc_input.c` | `roomwizard-events.cpp` |
|---|---|---|---|
| Classifier | `:63` | `:132` | `:174` |
| Scan loop | `:216` | `:235` | `:214` |
| Config parser | `:294` | `:172` | `:429` |
| Rescan timer | `:492` | `:468` | `:1263` |

**They have already drifted** — `MAX_INPUT_DEVICES` is 16 in the VNC client but 32 in the other
two, so a keyboard on `event17` works everywhere except VNC (this constant has already been manually
resynced once). The "clear errno before the read loop" hardening exists only in the ScummVM copy.

The ScummVM copy is defensible (C++, different event model, links only 4 common objects). **The
VNC copy is not** — `vnc_client/Makefile:21-29` already compiles five objects from
`../native_apps/common/`; it could link `gamepad.o` too.

**Fix:** extract classifier + scan + config parser into `common/evdev_scan.c` (~150 lines).
Quick win in the meantime: bump `MAX_INPUT_DEVICES` to 32.

### C2. Split `device_tools.c` (2651 lines) — open

Five previously-separate GUIs behind a tab enum, sharing nothing but the tab bar. Splitting into
`tab_settings.c` / `tab_diag.c` / `tab_tests.c` / `tab_calib.c` behind a small vtable is
mechanical and costs one line each in `build-and-deploy.sh`.

### C4. Make the common library use the logger — open

`common/logger.c` exists and apps use it (`app_launcher` 18 calls, `device_tools` 17), but the
library they all link writes to stdout unconditionally: `touch_input.c` 15 `printf` / 0 `LOG_`;
`gamepad.c` 7/0; `framebuffer.c` 5/0. `touch_init()` alone emits ~5 lines, and `app_launcher`
calls it after **every** child exit, so launcher stdout grows the same banner forever. Log rotation
now bounds the file (B21, done — see [Closed](#closed)), but the noise is still the cause.

### C5. Fix `text_truncate` and the 8px/6px font-width confusion — partly done 2026-08-02

- `common.c:83` `text_truncate()` takes **no destination size** and does `strcpy(dest, upper)`
  (up to 256 bytes) plus `strcat(dest, "...")`. Callers survive on arithmetic luck —
  `device_tools.c:2141` passes a 48-byte buffer for a 128-byte `EVIOCGNAME` string. One geometry
  change from a stack smash. Add a `size_t dest_size` parameter.
- `common.c:389/398/415/423` and `ui_layout.c:326` compute text width as **8 px/char**, but
  `fb_draw_text` advances **6 px/char**. Titles render ~17% left of centre; long strings clip off
  the left edge. Use `text_measure_width()` everywhere. **Partly done 2026-08-02:**
  `screen_draw_welcome*()` was rewritten for B3k and now measures with `text_measure_width()`
  throughout. Still wrong: `screen_draw_game_over()` (message and score widths) and
  `ui_layout.c:326`.

### C6. Extend the host-buildable test harness — open

⚠️ **`touch_inject` does not work and cannot be made to work on this device** (no `/dev/uinput`;
evdev's `write()` is the output-event path). The rule and the evidence are in `CLAUDE.md` →
*Non-obvious constraints* and `native_apps/CLAUDE.md` → *Input*. **This invalidates the touch half of
everything below, so read it first.**

What that means for this item, which was planned around injection:

- **`touch_inject` should be deleted or reduced to a loud "this cannot work, and here is why" stub.**
  As it stands it reports success and does nothing, which is worse than not existing.
- **`tests/test_game_selector_scroll.py` (277 lines) has never worked** for the same reason and
  should not be refactored into a harness as previously planned. Delete it, or rewrite it against
  framebuffer capture.

**What automated on-device testing is still possible, and is what the 2026-08-02 session used:**
SSH-launch a binary, `cat /dev/fb0`, decode with `fb565_to_png.py`, and inspect the *first* screen —
the one drawn before any input. That is enough for a real smoke test (`assert not-all-black`,
`assert alive after 2 s`) across all ~15 binaries, and it verified all five games' welcome screens
after B3k. **Write that harness.** Anything past the first screen needs a tap-by-tap checklist for a
human instead.

Kept because it is still true for anyone reading a raw value off the wire: **screen→raw conversion
must read `/etc/touch_calibration.conf`, not assume 0..4095** — the fit legitimately extrapolates
past the 12-bit range, and assuming `0..4095` is ~30 px out on Y. Use
`raw = screen*(max-min)/(dim-1) + min`.

Separately, host-gcc tests over the pure-logic functions, where regressions are invisible until
you're mis-tapping by 30 px. **Started 2026-07-31:** `tests/touch_calib_test.c` covers the
calibration fit end-to-end — it replays the 11 target medians from the reference capture and
asserts `touch_calib_fit()` still lands on `X 17..4084` / `Y -279..4382`, plus the per-axis verdict
and the sanity gate's accept/reject boundaries. **Added 2026-08-02:** `tests/gradient_test.c` covers
`fb_fill_rect_gradient()` (B7) — ascending, descending, mixed per-channel directions, `h == 1`,
`h == 0`, horizontal uniformity. It also demonstrates the pattern for testing *drawing* primitives on
the host: `fb_draw_pixel()` only touches `back_buffer` / `width` / `height` / `double_buffering`, so a
synthetic `Framebuffer` over a `malloc`'d buffer exercises the real code with no `/dev/fb0`.
**Added 2026-08-03:** `tests/framebuffer_bpp_test.c` covers the bpp dispatch (B1) — a guard region
after a 16bpp back buffer, all 17 primitives driven over the whole surface including its last pixel,
the RGB565 pixel values, the alpha path's unpack-blend-repack, the source-space colour key, and the
same sweep repeated at 32bpp so the fix cannot regress the depth every app actually uses. It extends
`gradient_test.c`'s pattern in the one way that matters for a heap overflow: **guard bytes make an
out-of-bounds write an assertion instead of a mystery**, which is the only way to see this bug at all
— on the device it corrupts whatever `malloc` handed out next rather than drawing anything wrong.
**Also added 2026-08-03:** `tests/gamepad_latch_test.c` covers the held-state model (B2) — a touch
region asserting and *clearing*, a key and a hat latching across quiet frames, the stick following
itself back to centre, an unplug while deflected, and two sources OR-ing rather than overwriting. It
widens the harness past drawing primitives for the first time, and it retires a standing assumption
worth retiring: **"input cannot be tested without `/dev/uinput`" is about the device, not about the
code.** `gamepad_poll()` takes the touch coordinate as a plain argument, and its evdev sources are
`read(2)` on an fd — so a temp file of `struct input_event` assigned to `gm.gamepad_fd` drives the real
`poll_gamepad()`, returning each event and then 0 at EOF exactly as a quiet non-blocking evdev fd does.
No kernel support, no device, no human. The same trick will work for anything else in `gamepad.c`.
Build lines are in each file header; all four are host gcc, so `build-and-deploy.sh` runs none of them.

**Write the failing version first.** `gradient_test.c` was compiled against
`git show HEAD:native_apps/common/framebuffer.c` and confirmed to fail (12 assertions) before the fix
was trusted; `framebuffer_bpp_test.c` was confirmed to fail with **29** against the pre-B1 file, and
the 32bpp half of it passed from the start, which is the evidence that the sweep is measuring depth
and not just "does anything draw"; `gamepad_latch_test.c` was confirmed to fail with **10** against the
pre-B2 `gamepad.c`, and those ten are the reported panel symptoms verbatim. On a codebase with no CI, a
test that has only ever been seen passing is not evidence that it can fail.

Still uncovered and worth the same treatment: `scale_coordinates()`, `parse_args()` (would have
caught the `args=` bug immediately), and the `config.c`/`ppm.c` parsers.

### C7. Run shellcheck — open

The shell scripts *are* the deployment system and they run as root over SSH.
`shellcheck *.sh */*.sh` — one command, no config, no repo changes.

### C8. Retire `hardware_diag` — it is a second copy of a `device_tools` tab — open, confirmed 2026-08-02

Raised on the panel 2026-08-02: *"it is working well, but why do we keep this, this is integrated in
device tools"*. The redundancy is already half-acknowledged —
[`native_apps/README.md:37`](native_apps/README.md) calls it "superseded by `device_tools` (hidden)",
and `build-and-deploy.sh:349` deliberately deletes its `.app` manifest so it never appears in the
launcher. So it ships, is built on every deploy, is unreachable without SSH, and duplicates read-only
info pages that `device_tools` renders from the same sysfs/procfs sources.

Two independent copies of the same six pages is exactly the drift C3 (calibration maths) and B3e's
`diag_exit_rect()` were both about — and the 2026-08-02 batch had to fix `hardware_diag`'s EXIT corner
and header band **separately** from the equivalent code in `device_tools`, which is the cost being paid.

Before deleting, confirm page-by-page that `device_tools` actually covers all six (System, Memory,
Storage, Hardware, Config, Network) — the diag pages are terse and one of them may have a field the
tabs lack. Then drop the source, the two build steps (`build-and-deploy.sh:102-103`), the four
deploy/marker references, and the README rows. If a page turns out to be unique, move that page into
`device_tools` rather than keeping the binary.

---

## Out of Scope

Recorded so the decision is not re-litigated. All of these need a kernel rebuild, and the vendor
kernel source is unavailable — the full rationale, the three un-portable drivers and the per-symbol
evidence are in [`SYSTEM_ANALYSIS.md#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy) and
[`#314-what-is-not-present`](SYSTEM_ANALYSIS.md#314-what-is-not-present). Requesting GPL source from
Steelcase has been explicitly ruled out.

| Item | Blocked by | Detail |
|------|---|---|
| Enable the two EHCI USB host ports | `CONFIG_USB_EHCI_HCD` unset — **and doubly dead:** no second USB connector and no unpopulated footprint on the board | [`#36-usb`](SYSTEM_ANALYSIS.md#36-usb) |
| Fix MUSB DMA properly | `CONFIG_USB_INVENTRA_DMA` + `CONFIG_MUSB_PIO_ONLY` both unset — a genuine build defect. The `/dev/mem` runtime patch stays. | [`#36-usb`](SYSTEM_ANALYSIS.md#36-usb) |
| `PREEMPT` / `HZ=250` / PREEMPT_RT | Config-only, but still a rebuild | [`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy) |
| SPI | Four controllers `okay` in the DT, `CONFIG_SPI` unset | [`#314-what-is-not-present`](SYSTEM_ANALYSIS.md#314-what-is-not-present) |
| USB gadget mode | No `CONFIG_USB_GADGET` | [`#314-what-is-not-present`](SYSTEM_ANALYSIS.md#314-what-is-not-present) |
| Piezo buzzer on TWL4030 PWM | Needs `CONFIG_PWM_TWL` **and** a wire — all 3 dmtimer PWMs are taken | [`#39-i2c`](SYSTEM_ANALYSIS.md#39-i2c) |
| Mainline 6.x port | Would break runtime bpp switching (ScummVM + VNC), lose the DSS overlay sysfs, cost RAM | [`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy) |

**Note:** enabling **UART3** for the ZigBee radio (F5) is *not* in this table — it may be reachable
by patching the appended DTB, which needs no kernel source.

---

## Closed

Finished work. IDs are retained so older references still resolve. Most entries are one line: the
code and git history are the record. Three are kept in full — **B3c**, **B3e** and **B22** — because
each is the only place that records *why* a subsystem is shaped the way it is, and each documents at
least one deliberate non-fix that reads as an oversight without the reasoning.

### Done — one line each

**B1. 16bpp framebuffer heap overflow** — done 2026-08-03, deployed and verified on RW09 the same day.
The back buffer is sized `width * height * bytes_per_pixel`, but every drawing primitive wrote a
`uint32_t` unconditionally, so at 16bpp it overran the allocation **2×**. Reachable, not theoretical —
see B24. `framebuffer.c` now dispatches on `bytes_per_pixel` through four helpers (`fb_pack565` /
`fb_unpack565` / `fb_store` / `fb_load`) which are **the only code that knows the surface format**; the
public API still takes RGB888 everywhere, so no caller changed. Converted: `fb_draw_pixel` (which
carries every shape, the font and the gradient), `fb_clear`'s **non-zero** path (only its `memset`
black path had ever been correct), `fb_draw_pixel_alpha` (which also had to *unpack* the destination,
or a 16bpp read-modify-write blends RGB565 bits as if they were RGB888), all three `fb_blit_sprite*`
variants, and `fb_swap`'s portrait rotation, which was 32bpp-only *and* ignored `line_length`.
Two deliberate choices: a sprite's **colour key is compared in the source's 32-bit space, before
packing** (two RGB888 colours can collapse onto one RGB565 word, so keying after packing turns opaque
pixels transparent), and **`fb_unpack565` replicates high bits rather than dividing**
(`(r << 3) | (r >> 2)`) — exact at both ends, and a `/31` in the alpha inner loop would be a call into
`__aeabi_uidiv` on this core. `fb_init()` still accepts 16bpp (ScummVM and `vnc_client` drive it on
purpose); what it no longer accepts is a depth with *no* primitives — on anything but 16 or 32 it
forces 32bpp on the fd it already holds, re-reads the stride, and **fails with a reason on stderr**,
because a refusing app beats a heap-corrupting one. Nothing on this device reports anything else, so
that branch is unreachable by design.
Covered by `tests/framebuffer_bpp_test.c`, written failing-first per C6: **29 failures** against the
pre-fix file, 0 after. A guard region sits immediately after a 16bpp back buffer and all 17 primitives
are driven over the whole surface *including its last pixel* — a `uint32_t` write at the last 16bpp
index lands a whole buffer past the end — then the pixel *values* are checked, because a primitive can
stay in bounds and still write the wrong format; the same sweep re-runs at 32bpp so the fix cannot
regress the depth every app actually uses.
**Verified on RW09 2026-08-03** after `./deploy-all.sh` rebuilt all three consumers (`native_apps`,
`vnc_client`, ScummVM — the last one linking `framebuffer.o` via `configure.patch`): the launcher is
pixel-identical to before, which is the point; ScummVM's launcher renders correctly at 16bpp
(build stamp `Aug 3 2026 10:54:54`, confirming the fresh link); and a live `vnc_client` remote session
renders full-colour at 16bpp — the real exercise of the dispatch on hardware. The one thing left
undone is item 3 of the old residue: ScummVM and `vnc_client` could now drop their private text
renderers onto `fb_draw_text`. **Not done, deliberately out of scope** — file it separately if wanted.

**B24. No game asserted the framebuffer bpp, so B1 was reachable from a bare SSH launch** — done
2026-08-03, deployed and verified on RW09 the same day. `fb_set_bpp()` had ten call sites and **not one
was a game**: `app_launcher` (×2), `game_selector` (×2), `device_tools` (×2), `hardware_config`,
`hardware_diag`, `touch_raw`. Games inherited whatever depth the previous app left. Under the launcher
that is harmless — it re-asserts 32bpp after every child exits, which is exactly why this was never
seen. It was **not** harmless for a directly-launched binary, which is what an SSH smoke test does
(C6): on RW09 2026-08-02 a stale `vnc_client` (B25) had left `/dev/fb0` at 16bpp and `brick_breaker`
came up logging `800x455 logical … 16 bpp`, running B1's full 2× overflow.
Both halves are in: B1's dispatch makes the depth a correctness non-issue, and **all 11 remaining
`fb_init()` call sites now pin 32bpp first** — the seven games plus `hardware_test_gui`, `usb_test`,
`tests/audio_touch_test` and `tests/touch_trace`. So the reason to pin is no longer memory safety but
**determinism and appearance**: 16bpp bands every gradient, and how an app looks must not depend on
which app ran before it. That rationale now lives once, on `fb_set_bpp()` in `framebuffer.h`; the five
call-site comments saying "the common draw helpers write one uint32 per pixel, so the framebuffer must
be 32bpp" were stale the moment B1 landed and now point at it. Also corrected `CLAUDE.md` and
`native_apps/CLAUDE.md`, which both claimed the native menus *and games* forced 32bpp — only the menus
did.
**Verified on RW09 2026-08-03 by reproducing the original failure exactly**: `vnc_client` was started
(panel → 16bpp for the remote session) and then `SIGKILL`ed by matching `/proc/*/exe`, so it left
`/dev/fb0` at 16bpp with nothing running — the 2026-08-02 state. `fbset` confirmed `800 480 … 16`;
`/opt/games/brick_breaker` was then launched over SSH and `fbset` immediately read `… 32`, with a
clean, correctly-coloured welcome screen. Repeated independently with `fbset -depth 16` → `tetris`.
Note for anyone re-running this: because the pin works, the game does **not** come up at 16bpp, so the
capture decodes at `--bpp 32` — run `fbset | grep geometry` and believe it rather than assuming a
depth from which app you launched.

**B2. Gamepad buttons latched on and were never released** — done 2026-08-03.
`poll_touch()` (virtual touch regions) and the analog-stick→D-pad merge both set `.held = true` on the
caller's `InputState`, and **nothing anywhere cleared it** — `gamepad_poll` deliberately didn't, and no
caller did. One tap on a virtual left pad ran the player left forever; a `.pressed` reader saw its
first tap in a region and then nothing. Confirmed on RW09 2026-08-02 in frogger: the zones stayed
highlighted light-blue and the frog "sometimes just randomly jumps".
**The fix separates the two kinds of source, which is the whole point** — the naive "clear `held` at
the top of every poll" breaks keys and the D-pad hat, whose level legitimately has to survive quiet
frames because a key-up may be hundreds of frames away. So event-driven level state moved into
`GamepadManager.held_latched[]` (written by `poll_gamepad`/`poll_keyboard`), the position-reporting
sources write a **per-frame** `derived[]` array rebuilt on every poll, and `state->buttons[i].held`
became a pure **output** = `latched || derived`. Three consequences that were part of the same bug:
the stick merge had to move out of `poll_gamepad` into `gamepad_poll` (it must run after this frame's
`EV_ABS` events and after the no-pad branch); `poll_gamepad` now **zeroes the axes when
`gamepad_fd < 0`**, or a stick unplugged while deflected asserts its direction forever; and
`gamepad_rescan()` clears `held_latched[]`, or a key held at unplug time freezes on because its key-up
will never arrive. Because `held` is now an output, `app_launcher.c`'s drain loop no longer depends on
reusing one persistent `InputState` — that comment's "separate zeroed `InputState`s" hazard is
structurally impossible and has been trimmed to say so.
**Covered by `tests/gamepad_latch_test.c`** (host gcc; build line in its header), written
failing-first: **10 failures** against the pre-fix `gamepad.c`, 26 assertions green after. Contrary to
the standing assumption that none of this is testable without a human at the panel, it needs no device
and no `/dev/uinput`: `gamepad_poll()` takes the touch coordinate as a plain argument, and the evdev
sources are `read(2)` on an fd — so a temp file of `struct input_event` assigned to `gm.gamepad_fd`
drives the real `poll_gamepad()`, returning each event and then 0 at EOF exactly as a quiet
non-blocking evdev fd does. The ten pre-fix failures are the reported symptoms verbatim: region stays
held after lift, no released edge, second tap produces no press edge, stick stays held after centring,
axes stale after unplug, latch survives a rescan. What still needs the panel is only whether a game
*feels* right — B13g removed the last shipped consumer, so nothing on the device exercises the path
today.

**B13g. Snake's touch regions were wiped by `gamepad_init()` ordering** — done 2026-08-03, **as a
deletion, not the reorder the row implied**. `gamepad_init()` ran after `init_game()` and `memset`s the
manager, so the four `TouchRegion`s snake registered never took effect. Making them live would have
been actively wrong twice over: snake **already** hops relative to the head (`snake.c:454`), so the
regions were a redundant second input path — the exact situation B13k deleted from frogger — and they
**overlapped pathologically**, UP/DOWN being the full-width top/bottom halves of the grid while
LEFT/RIGHT were the full-height left/right halves, so *every* in-grid tap asserted two directions at
once. Reordering would have activated that, and pre-B2 the latch would have made both stick. So the
array and the `gamepad_set_touch_regions()` call are gone, and that API now has **zero** callers
(`gamepad_draw_touch_controls()` already had none). Both library functions are kept — they are correct
surface now that B2 has landed, and B2 landed first precisely so the next caller is safe.

**B23. The backlight slider's live preview wrote to a node that does not exist** — done 2026-08-03.
`device_tools.c:512` and `hardware_config.c:127` held a verbatim copy each of `apply_backlight()`,
writing `/sys/class/backlight/pwm-backlight/brightness`. **That path is not present on this device** —
`/sys/class/backlight/` is empty and the panel is a LED-class device at
`/sys/class/leds/backlight/brightness` (measured on RW09 2026-08-02), so dragging either slider did
nothing until the value was saved and some later `hw_set_backlight()` picked it up, and the `fopen`
failure was unchecked. Both copies now call a new `hw_set_backlight_raw()` in `hardware.c`, which
writes the node `hardware.c` already owns through the same clamping/error-reporting `write_brightness()`
helper as everything else, and both callers check the return. **Raw is deliberate, not an oversight:**
the slider is choosing the very scale factor `hw_set_backlight()` applies, so previewing through the
scaling setter would multiply by the factor being replaced (that was B9, and it is closed). This also
removes the duplication the entry asked about, for the backlight path — `do_led_test()` is still
duplicated between the two tools, which is C8's business (it proposes retiring one tool outright).

**D2b. `check-arm-safe.sh` counted files it could not check** — done 2026-08-03. The gate defaults to
"every executable regular file in `build/`", and `build/` also collects **host** binaries: the
`tests/` regressions are compiled with native gcc into the same directory, and because `/mnt/c` under
WSL reports every file as mode 0777, three stray `.png` captures were "executable" too. `arm-objdump`
cannot disassemble an x86-64 ELF or a PNG, so all eight passed trivially while proving nothing — which
is how the headline "zero across 38 build artifacts" came about when only **31** of them were ARM. The
loop now skips anything whose `objdump -f` architecture is not ARM and reports the count it skipped.
No ARM artifact's treatment changed; this only makes the number mean what it says. Docs quoting 38 (or
the older 30) were corrected to 31.

**B7. Descending gradients rendered wrong** — done 2026-08-02.
`fb_fill_rect_gradient()` computed its channel deltas in `uint32_t`, so a descending channel wrapped
`(bottom - top)` to ~2³² and the following division did not undo it. Now signed deltas, with the
`h > 1 ? h - 1 : 1` span hoisted out of the row loop. **No clamp** — `j <= span`, so each channel
provably stays between two endpoints already masked to `0..255`, and adding one would be dead code.
The symptom was *not* the "garbage" the original entry claimed: the wrapped delta's high bits bled
across channel boundaries, so a ramp still appeared but non-monotone and ending on the wrong colour
(platformer's sky red ended at 117 instead of 100) — which reads as banding, not corruption, and is
why it survived. Every one of brick_breaker's eight `ROW_COLORS` descends on all three channels, so
every brick was affected, as were the paddle and the platformer sky.
Covered by a new host regression, `tests/gradient_test.c` (ascending, descending, mixed per-channel
directions, `h == 1`, `h == 0`, horizontal uniformity); it was confirmed to **fail** against the
pre-fix `framebuffer.c` before being trusted. Verified on RW09 pixel-exact from a first-screen
capture: brick_breaker's welcome-screen rule (`RGB(0,220,255)` → `RGB(255,80,200)`, green and blue
both descending) reads `(0,220,255) / (127,150,228) / (255,80,200)` across all 640 px of its run.

**B9. Backlight get/set asymmetry permanently dimmed the panel** — done 2026-08-02.
`hw_set_backlight()`/`hw_set_led()` scale by the configured percentage; the getters returned the raw
sysfs value, so the three `int original = hw_get_backlight(); … hw_set_backlight(original);` restore
pairs multiplied the panel by pct/100 on every run. Fixed in the **getters** (`hw_unscale_brightness()`,
the inverse of `hw_scale_brightness()`), not at the call sites, so all three are fixed at once and the
natural `hw_set_backlight(hw_get_backlight())` is now a no-op — the trap is closed rather than
documented. A raw value above the configured maximum can only come from something that bypassed the
API, so it clamps to 100 instead of reporting >100; a read error (−1) propagates unchanged.
`hw_get_led()` gets the same treatment, which also fixes `hardware_test.c:112-129`, a read-back test
that printed the scaled values while claiming it had set 75/25.
`hardware.h`'s file header claimed "all brightness values are 0-100 (percentage)" — that ambiguity is
what allowed the two halves of the API to drift, so it now names the space explicitly ("percent of the
user's configured maximum") and both getters say they round-trip.
Verified on RW09 with `backlight_brightness=50`: `set 80` → raw 40, `get` → 80, and three feed-back
cycles all held raw at 40. The old behaviour, reproduced by feeding the raw value back as the old
getter did, decayed 80 → 40 → 20 → 10 → 5.
The plan's line reference `device_tools.c:1339` was stale — the site is `:1302`. Two adjacent bugs
found while doing this and filed separately: **B23** (the slider's live preview writes a nonexistent
sysfs node) and, indirectly, **B24**.
Note `native_apps/backlight` gained a `get` subcommand. It is not a convenience: with no `/dev/uinput`
(C6) and no keyboard, an SSH-readable value was the only way to verify the round trip at all.

**B3. A bad calibration could wedge the device with no recovery** — done 2026-07-31.
Three parts: `touch_calib_range_sane()` (`2 × overlap(fit, hw) ≥ max span`, deliberately **not**
"reject outside 0..4095" — a correct fit legitimately extrapolates); the wizard keeps the *entry*
calibration installed through its own screens and goes live only at CONFIRM behind a 20 s
auto-revert, with `RESET` always reachable; `touch_wait_for_press_raw()` polls with a 200 ms slice
and returns −1 on a real error instead of spinning.

**B3a. The 9-tap fit ran through its own edge-compressed samples** — done 2026-07-31.
Fit from **interior targets only** (≥100 px from each end on X, ≥80 px on Y). The host regression
(`tests/touch_calib_test.c`, see C6) is the live record of the expected output. X endpoints outside
`0..4095` are **not** by themselves evidence of edge leakage — what indicts a fit is an interior mask
that admits near-edge targets, or a slope that disagrees with the interior line.

**B3b. `touch_raw` printed one global verdict where the panel needs two** — done 2026-07-31.
`touch_calib_axis_verdict()` reports per axis, in **panel pixels**. Two earlier wordings were wrong in
opposite directions, which is the lesson: **a verdict about reach must come from `INSET`-style inward
stepping, never from a bezel press or an edge sweep** — those read identically under either hypothesis.

**B3d. Folded `unified_calibrate` and `SCREEN EDGES` into one flow** — done 2026-07-31.
Device Tools → Display → TAP → CHECK → EDGES → REACH → REPORT → CONFIRM, bezel zeroed, writing both
config lines; `tests/unified_calibrate.c` deleted. Design point worth keeping: **the bezel is measured
by looking, not by touching** — numbered 2 px ladders at each panel edge. The old adjuster drew its
reference frame on the *logical* edge, which is defined by the margins it was trying to measure.

**B3f. `content_area` was config-file-only, with no UI** — done 2026-08-03, panel-confirmed.
The blocker was layout, not plumbing: `vnc_settings.c` had `ROW_COUNT 6` and a fixed 52 px pitch, so a
seventh row landed on the status line. Fixed by **deriving** the pitch — `settings_row_pitch()` divides
the space between `FIRST_ROW_Y` and the status line and caps at the old 52, so row height joins the
button row and both keypads in coming from `SCREEN_SAFE_BOTTOM` at runtime (45/41 on RW09's inset,
51/47 on a full 480-row panel, i.e. unchanged there). Row 7 is **CONTENT** with a `TOGGLE`. The trap:
the renderer keeps the flag in a static that only `vnc_renderer_set_remote_size()` reads, once per
session, so **both** `SETTINGS_SAVE` sites must re-publish with `vnc_content_set_full()` or the row
appears to do nothing until a restart. Rules in `vnc_client/CLAUDE.md`. ScummVM's half is **B3g**.

**B3i. HUD text sat in the safe area, wasting the band it was allowed to use** — done 2026-08-02.
The inverse of B3e and easy to get backwards: **pressed → `SAFE`, only seen → `VISIBLE`.** Tetris'
SCORE/LVL and frogger's HUD moved into the `SCREEN_VISIBLE_TOP` band above the button row (they were
being drawn *behind* the buttons), with a fallback that drops them into the row's vertical centre when
the band is too short. Constraint from the B3c audit: HUD text meant to *align* with `SAFE`-anchored
buttons must stay `SAFE` — check the intended alignment per string, not per file.

**B3j. Tetris' board overflowed the bottom of the screen** — done 2026-08-02.
`board_top` was a literal `SAFE_TOP + 55`, **smaller than the button row it was meant to clear**
(`+ 60`), and the vertical budget ignored the 2 px frame `fb_draw_rect()` draws *outside* the cells.
Now derived from `LAYOUT_MENU_BTN_Y + BTN_MENU_HEIGHT` and `SCREEN_VISIBLE_BOTTOM`. Moving SCORE/LVL
into the top band (B3i) is what paid for the clearance.

**B3k. The shared welcome screen overlapped and mis-centred its own text** — done 2026-08-02.
Root cause was sharper than "not centred": **`fb_draw_text()` does not interpret `'\n'`**, so every
caller's multi-line string rendered as one long line, measured at the wrong font width (`strlen * 8`
where the font advances 6) and centred on that wrong width. Fixed in one place —
`screen_draw_centered_block()` / `screen_measure_block()`; `screen_draw_welcome*()` now **sets**
`start_btn->x/y`, so the callers' own `button_init()` coordinates are only a pre-first-draw fallback.
Tetris had its own copy of the whole screen and now calls the shared one.

**B4. Respawn loop always logged exit code 127** — done 2026-08-02.
`roomwizard-app-init.sh` reaped the child inside a `kill -0` loop, so the later `wait` returned 127
unconditionally — meaning **exit 132 (SIGILL), the Cortex-A8 divide trap and the one failure this log
exists to catch**, could never be reported. `wait "$CHILD_PID"` now runs inside the loop. Verified on
RW09 with both `SIGTERM` (code 0) and `SIGILL` (code 132).

**B5. No fallback when `default-app` is broken** — done 2026-08-02.
`FALLBACK` + `MAX_FAILURES=3` + `FAST_EXIT_SECS=5`, with backoff capped at 30 s and no
fallback-to-itself. `do_start`'s pre-flight `[ ! -x ] && return 1` had to become a warning that still
starts the wrapper — otherwise the service refuses to run and the fallback never gets its chance,
which is the exact wedge the item describes. Both branches verified on RW09.

**B6. `start-stop-daemon` fallback started a second app** — done 2026-08-02.
`start-stop-daemon --start` exits **1 when a matching process is already running** — its normal
"already up" signal — so the `||` fired precisely then and two apps fought over `/dev/fb0`. Guarded on
`pidof -x respawn.sh`, **before** the heredoc, which also fixes an unrecorded hazard: rewriting a
script file that a running `sh` still has open can make that `sh` misparse the rest of it. The
deploy-script half is still open as **B20**.

**B8. Non-atomic config/highscore saves** — done 2026-08-02.
`file_write_atomic_open()` / `_commit()` / `_abort()`: temp file, `fsync`, `rename`, then **`fsync` the
parent directory** so the rename survives a power cut. It lives in **`common/config.c`** rather than a
new `common/atomic_file.c` on purpose — a new object would have to be added to every link line in
`native_apps/build-and-deploy.sh`, `vnc_client/Makefile` and
`scummvm-roomwizard/backend-files/configure.patch`, for ten lines of code.

**B13a. platformer: game-over buttons could never fire, and no way out** — done 2026-08-02.
`touch_poll()` was called a **second** time in the same frame and clears `TouchState.pressed` at
entry, eating the press edge `handle_input()` had already captured. Platformer was also the only game
with no `BTN_ID_BACK` handler. Both fixed; **panel verification is still open — see B22 in Phase 1.**

**B13c. samegame locked to 10 FPS** — done 2026-08-02.
`needs_redraw = false` was set **before** the pacing ternary, so `usleep()` always picked IDLE. Capture
`bool drew = needs_redraw;` first and sleep on `drew` — the pattern `native_apps/CLAUDE.md` documents.

**B13d. Tetris gravity counted loop iterations, not time** — done 2026-08-02.
Idle frames sleep 100 ms, so pieces fell ~3× too slow (measured on the panel: one row every 5–6 s) and
sped up while a key was held. Now a `get_time_ms()` delta with the interval in ms, **at most one row
per call** so a long pause cannot replay a backlog, and a `drop_clock_stale` flag set in
`update_game()`'s own not-playing branch so none of the six `SCREEN_PLAYING` sites has to remember to.

**B13k. Virtual D-pads removed from frogger and platformer** — done 2026-08-02.
Frogger never needed them — `handle_input()` already hops the frog from a plain tap relative to its
position — so the regions were a redundant second path that made the frog jump on its own, and B2's
un-cleared `.held` left the overlay stuck light-blue. Platformer removed on the user's call in the same
pass; it has no tap-relative fallback, so it is **controller-only** and warns on the welcome screen when
nothing is connected (`screen_draw_welcome_warn()`). `snake.c` still calls
`gamepad_set_touch_regions()` but its regions are dead code — see B13g and B2.

**B13l. Pong served every ball too slowly** — done 2026-08-02, panel-confirmed.
Every serve started at `5.0` px/frame ≈ **7 s to cross the playfield**, and `reset_ball()` runs after
every point, so all 11 points restarted slow. `BALL_START_SPEED 8.5f` — the old rally's comfortable
point, from the first serve — and the four scattered `* 1.05` literals are one `BALL_SPEEDUP` macro.
Nothing tunnels at any speed: the paddle and wall tests are half-planes that clamp on contact, which is
why a cap was not needed for correctness. **Two deliberate open questions, the user's to call:** the
rally ramp is uncapped and now starts higher (10 hits ≈ 13.8 px/frame, ~1.7× the old peak), and
`ai_speed = 3.0 + difficulty` = 5 px/frame at medium makes the AI the slow side.

**B16. Deleted `native_apps/Makefile`** — done 2026-07-31.
It could not work — `CC = gcc` with `-march=armv7-a` fails on x86, rules pointed at moved files, and
`install:` copied x86 binaries into the **host's** `/opt/games`. `build-and-deploy.sh` is the only
build path and always was.

**B21. `app_stdout.log` was never rotated** — done 2026-08-02.
`rotate_log()` only touched `respawn.log`; a crash-looping app wrote forever on a rootfs under 1 GB.
Measured on RW09 before the fix: **2 091 622 bytes** against a rotated `respawn.log` of 122 956.
`rotate_one <file>` covers both. It stays at the top of the respawn loop, which is the right boundary:
the child's `>>` redirection is reopened on each launch, so a rotation between launches actually frees
the inode. See **C4** for the noise that causes it.

**C3. De-duplicated the calibration math** — done 2026-07-31.
Three copies of the same safety-critical fit (`device_tools`' Calibration tab, the standalone
`unified_calibrate`, and a private one inside `touch_raw`). They drifted, and the drift kept the
endpoint bug alive across three sessions (`SYSTEM_ANALYSIS.md#33-touch`). Now one implementation in
`common/touch_calib.c`, linked by `device_tools` and `touch_raw`.

### B3c. Edge bands that could not be touched — done 2026-08-01 (evening)

*Open residue tracked as [B3c in Phase 1](#b3c-second-unit-measurement-of-the-touch-dead-band--partly-done-2026-08-01).*

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
zero deviation). The dead band is then **exposed rather than hidden**:

- `framebuffer.h` carries two rectangles — `SCREEN_VISIBLE_*` (full logical screen) and
  `SCREEN_SAFE_*` (visible ∩ touchable). The band stays fully drawable, which the user explicitly
  wanted: a status bar or score row there is good use of screen.
- The inset is **measured at runtime**, never hardcoded: `publish_safe_area()` pushes the four raw
  edge extremes through the production `scale_coordinates()`. `0` until an edge sweep is recorded;
  capped at `FB_TOUCH_INSET_MAX` (48 px) with a warning.
- Config line 3 (`reach x_lo x_hi y_lo y_hi`, keyword-tagged and optional) persists the swept reach;
  the wizard's `REACH` step measures it. Tagged and trailing so old parsers ignore it.
- Per-app audit done: draw-only call sites moved to `SCREEN_VISIBLE_*` (Tetris board, Brick Breaker
  play area, `hardware_diag` in full, titles/hints/score tables in `common.c`, `app_launcher`,
  `game_selector`, `pong`). SameGame's grid stayed on `SCREEN_SAFE_*` — every block is a tap target.

Consequently **not needed**: `SCREEN_TOUCH_*` macros, bigger bezel margins, and a band-limited
stretch. A dead band is a fact about the panel, so the wizard's `REPORT` step and the `TOUCHABLE:` row
report it as a number and go amber only on *magnitude* (24 px), not on "non-zero".

The audit also turned up **B3e** (touch targets at hardcoded offsets), which is tracked separately.

**Follow-on, done 2026-08-01 (late):** the per-app audit above only covers code we wrote. ScummVM and
`vnc_client` display **third-party** content that cannot be audited for what has to be pressable — a
remote taskbar, a game's verb bar, the ScummVM theme's button row — so both were still placing guest
pixels in the dead band (reported on the device as "~10 px unreachable top and bottom" right after a
precision calibration). Both now confine the guest content rectangle *itself* to `SCREEN_SAFE_*`, and
everything hit-tested in them (VNC settings/reconnect/exit gesture, ScummVM's overlay and gesture
corners) is on the safe rect unconditionally. Each has an opt-out that moves only the picture, whose
discoverability is **B3g** on the ScummVM side (**B3f** was the VNC side — fixed 2026-08-03, it is a
row on the settings screen now) and whose config-file location is **B3h**. Rules in
`vnc_client/CLAUDE.md` and `scummvm-roomwizard/CLAUDE.md`.

### B3e. Buttons positioned with hardcoded offsets lose rows to the touch inset — done 2026-08-02

Found during the B3c per-app safe-area audit (2026-08-01) and deferred there, because it is a
pre-existing violation of the "never hardcode 800/480/400" rule rather than part of that change.

**The wider sweep the old entry asked for was the whole value of this item.** The audit had listed
`tetris.c` alone; `grep -n 'button_init(&[a-z_]*, *[0-9]'` found **four** games with the same
`button_init(&menu_button, 10, 10, …)` + `fb.width - BTN_EXIT_WIDTH - 10` pair — `tetris.c`,
`pong.c`, `snake.c`, `frogger.c` — because the audit's method only saw call sites that already
mentioned `SCREEN_SAFE_*`, and a hardcoded one is invisible to it by construction.

All five sites now derive from the macros, and every replacement expression was chosen to be
**byte-identical to the old literal at inset 0**, so nothing moves on an uncalibrated panel:

| Site | Fix |
|---|---|
| `tetris.c`, `pong.c`, `snake.c`, `frogger.c` | `LAYOUT_MENU_BTN_X/Y` + `LAYOUT_EXIT_BTN_X/Y` |
| `pong.c`, `snake.c` playfield top | `LAYOUT_MENU_BTN_Y + BTN_MENU_HEIGHT + 20`, height from `SCREEN_VISIBLE_BOTTOM` |
| `frogger.c` HUD | runtime `hud_height = SCREEN_SAFE_TOP + HUD_HEIGHT`; band, grid top, `available_height` and the three content rows all offset. Its 70 px band had to grow with the row, or the timer bar would cross the buttons' middle and they would push 9 px into the playfield |
| `hardware_diag.c` | `diag_exit_rect()` — one rect shared by the drawn box (`:329`) and the hit-test (`:814`), which were two independent literals that had to agree by hand. Header band is `max(50, SCREEN_SAFE_TOP + 40)`, capped at 68 so it never grows into the page content at `y=70` |

Page content in `hardware_diag` is **deliberately not** shifted: dense read-only text, one page with
a `y < SCREEN_H - 60` guard, and per the B3c audit it is draw-only everywhere but the EXIT corner.

One pre-existing cosmetic overlap is preserved exactly rather than fixed: frogger's timer bar
(`hud_height - 18`, full grid width) is drawn after the buttons and covers their bottom 8 px. The
relative geometry is unchanged by this batch — it was 8 px before and is 8 px now. **The panel pass
then found the right fix** and it shipped as part of B3i: move the bar up, between the two buttons.

**Panel verification, RW09, 2026-08-02.** Device Tools → Display reported
`touchable: X 6..793 Y 19..438  visible: 800x455 of 800x480` — i.e. a real ~19 px top inset, so these
rows genuinely moved rather than being a no-op. (Those digits are the 2026-08-01 18:50 calibration read
back a day later, not a fresh measurement.) Per app:

| App | Result |
|---|---|
| Tetris | pass — MENU and EXIT both in the safe area, but three *other* defects surfaced: B3i, B3j, B13d |
| Pong | pass |
| Snake | pass |
| Frogger | pass — MENU and EXIT in the safe area, all lanes visible with logs and lily pads; surfaced B3i, B3k, and confirmed B2 |
| SameGame | pass — "works much better than before, buttons, score, blocks well aligned and visible" (also confirms B13c) |
| `hardware_diag` | pass — EXIT corner and pages all correct; prompted C8 |

### B22. The game-over screen only appeared after a tap — done 2026-08-02

*Open residue tracked as [B22 in Phase 1](#b22-game-over-screen--one-row-still-unverified--partly-done-2026-08-02).*

Reported from the panel: *"if a game ends, by all lives exhausted or by retire, I need to tap the
screen to advance to the highscore screen."* Real, and in the **shared** component, so it affected six
of the seven games that use it.

`gameover_update()` (`common/common.c`) is a three-state machine — `CHECK` → (`NAME_ENTRY`) →
`DISPLAY` — and **only `DISPLAY` draws anything.** Every game calls it from inside its *draw*
function, which a dirty-flagged main loop runs only when `needs_redraw` is set. So on the frame a game
entered `SCREEN_GAME_OVER`: the screen-transition test set the flag, the playfield was drawn, `CHECK`
computed `hs_qualifies` and **returned without drawing**, `fb_swap()` presented a bare playfield — and
then the flag cleared and the static-screen branch only re-set it on input activity. The tap was the
only thing that could produce the second frame, and the same applied to the name-entry keyboard.

**`samegame` was the one game that did not show it**, because it already carried a local workaround —
an unconditional redraw while `SCREEN_GAME_OVER` with the comment *"Game-over screen processes input
inside draw — always redraw"*. Someone hit this before and patched the caller rather than the
component, so the other six kept the bug and samegame pinned a static overlay to 30 fps.

**Fixed in the component, with one predicate for the callers** — `gameover_needs_redraw()`, backed by
two new fields:

- `pending_draw` — "I owe the screen a frame". True from `gameover_init()`, cleared straight after
  `gameover_draw()`, **re-set when `RESET SCORES` empties the table** so the emptied table appears
  immediately instead of on the next tap (same class of bug, one line).
- `armed` — false until the first `DISPLAY` frame has been drawn; input is ignored while false.

`CHECK` now **falls through** to `NAME_ENTRY`/`DISPLAY` in the same call, so the no-highscore path
draws on the transition frame with no extra frame at all. `NAME_ENTRY` deliberately keeps its `return`:
`hs_enter_name()` is a blocking keyboard that repaints and swaps the framebuffer itself, so drawing our
overlay in the same call would composite it over the keyboard's last frame. It leaves `pending_draw`
set and takes one clean frame, with the playfield redrawn underneath first.

**`armed` is part of the fix, not a nicety.** `gameover_draw()` runs before the button checks in the
same call, so making the overlay appear on the transition frame also puts the *press that ended the
game* in front of its buttons — `touch_active` is a rising edge that survives until the next
`touch_poll()`. In brick_breaker the pause dialog's `RETIRE` (y 266..310) overlaps this screen's
`RESET SCORES` (y 289..349) by 21 px, so without the guard a `RETIRE` press landing in that band would
**wipe the high-score table** on a screen nobody had seen yet.

Also fixed in passing: `gameover_update()` early-returned on `!touch_active` *before*
`button_check_press()` could clear `Button.was_pressed`, so `RESET SCORES` only ever fired **once** per
game over. All three buttons are now fed every frame with `touch_active && button_is_touched(...)`.

The seven callers each gained the same three-line block before their `if (needs_redraw)`; samegame's
unconditional redraw was replaced by it. The rule this bug breaks is now recorded in
`native_apps/CLAUDE.md` under *Rendering: dirty flag + adaptive sleep* — **a component whose
`update()` both draws and reads input has to tell the caller when it still needs frames.**

**Follow-up: the first fix wedged samegame, and the reason is worth keeping.** Panel test 2026-08-02:
samegame advanced to the high-score screen on its own (the fix working), and then *"the user interface
becomes unresponsive — I needed to kill it from the commandline"*. Tetris, snake and frogger were fine.

`gameover_needs_redraw()` reported only `pending_draw` — "I owe the screen a frame" — and **the
component's buttons are read inside the draw path**, so a frame the loop declines to run is also an
input event the component never sees. Six games hid that: they each have an
`else { /* static screens: redraw on input activity */ if (ts.pressed || ts.held) … }` branch, so a
tap produces a frame by itself. **samegame has no such branch** — its dirty flag is a pure
visible-state diff (screen, highlight, highlight count, score, blocks remaining, mouse, anim state)
and a tap on an overlay changes none of those. Its old unconditional `SCREEN_GAME_OVER` redraw *was*
its input path, and replacing it with a predicate that correctly goes quiet removed the only frame
source. With `handle_input()` routing nothing else on that screen, all three buttons were dead and
SSH was the only way out.

Fixed in the component, not in samegame's loop: the predicate now returns true on three grounds —
`pending_draw` (owes a frame), `ts.pressed || ts.held` (has input to act on), and **any button's
`was_pressed` still latched**. The third is not belt-and-braces: `button_check_press()` clears that
latch only on a frame where the button is *not* touched, and at `FRAME_DELAY_IDLE_US` a press and its
release can both arrive in one `touch_poll()` — so there may be no `held`/`released` frame at all and
the *next* press would be silently eaten. Idle still produces no frames, so the 30 fps-static-overlay
problem does not come back.

**The lesson is about the predicate's contract, not about samegame:** "needs redraw" for a component
that reads input in its draw path means *draw pending **or** input pending **or** re-arm pending*. Six
correct-looking callers concealed an incomplete predicate, which is the same shape of mistake as the
original B22 (one caller's local workaround concealing a component defect).

**Panel status, 2026-08-02:**

| Game | Status |
|---|---|
| tetris, snake, frogger | pass — game over → high-score screen advances on its own, no tap |
| samegame | pass — **re-fix confirmed on the panel**: advances on its own *and* the settled overlay stays responsive; the full sequence (lose → overlay+table → `RESET SCORES` empties it immediately → name-entry keyboard with no tap → entry appears → `RESET SCORES` again on a second game over → `RESTART` → `EXIT`) all behaves |
| brick_breaker | pass — `MENU` → `RETIRE` shows the overlay immediately **with the table still populated**, so the `armed` guard holds against the 21 px `RETIRE`/`RESET SCORES` overlap |
| pong | pass — parked the paddle and let the AI run out `WINNING_SCORE` 11 |
| platformer | **unverified** — see [B22 in Phase 1](#b22-game-over-screen--one-row-still-unverified--partly-done-2026-08-02) |

### D1. Compiler warnings — done 2026-07-30

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

### D2. sdiv/udiv pre-deploy gate — done 2026-07-30

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

Matching the tab-delimited **mnemonic** field instead gives **zero across every ARM artifact**, so the
gate needs no allowlist and any hit is real. `CLAUDE.md` has been corrected. (The artifact *count* was
itself wrong until D2b — the gate was also being handed host binaries it could not disassemble.)

### D3. Missing/duplicated files — done 2026-07-30

`CLAUDE.md` and `fb565_to_png.py` are tracked. The four redundant framebuffer decoders
(`native_apps/tests/fb_to_png_{16,32}bit.py`, `scummvm-roomwizard/fb_to_png.py`,
`scummvm-roomwizard/convert_fb.py`) are deleted — `fb565_to_png.py` is a superset (both bpp, page
select) and nothing invoked them.

`.gitignore` now exempts `Screenshots/` alongside `HardwarePhotos/`, with a matching LFS rule, so a
doc screenshot no longer needs `git add -f`.

*(A fifth copy of the decode logic is still inlined in
`native_apps/tests/test_game_selector_scroll.py:80` — that one belongs to **C6**, not here.)*

### D4. `.gitattributes` — done 2026-07-30

`*.sh text eol=lf`. The CRLF-shebang-vs-BusyBox reasoning now lives as a comment in
`.gitattributes` itself.

### D5. Documentation corrections — done 2026-07-29

SoC (OMAP3503), GPIO banks, touch panel type, sensor inventory, deploy modes, compiler-flag claims;
new SoC/display/boot-chain/panel-timing/kernel sections in `SYSTEM_ANALYSIS.md`; per-component
`CLAUDE.md` guides; nine stale docs deleted.

### F3. Auto-backlight from an ambient light sensor — closed 2026-07-30, no such hardware

Kept because it will otherwise be re-proposed. The full teardown found no sensor and — decisively —
**no aperture, window or light pipe anywhere in the enclosure**. The case is light-tight, so a
sensor would have nothing to sense even if populated. The vendor factory test's I2C-bus-1
light-sensor step is shared firmware for a product family in which this SKU is not the variant with
the sensor. **Do not probe for it** — that hazard is now a standing rule in `CLAUDE.md`
(`pv02_app 5` can hang the bus, and bus 1 carries the PMIC; see
[`SYSTEM_ANALYSIS.md#39-i2c`](SYSTEM_ANALYSIS.md#39-i2c)).

**Salvage:** *time-of-day* dimming needs no sensor — there is an RTC and
`/sys/class/leds/backlight/brightness` works. Fix **B9** first; auto-dimming on a broken setter
makes things worse.

### Serial console — declined 2026-07-30

`P4` was located and its pinout verified, but the recovery loop is *pull the SD card, reimage, DHCP,
SSH* — and since the standing rules keep NAND and U-Boot untouched, the card **is** the entire
failure surface. Serial would add boot visibility, not recovery capability. Revisit only if NAND or
U-Boot ever get written.

---

## Suggested order of work

Forecast only. What actually happened is in the dates on each entry and in `git log`.

1. **Phase 0** — done 2026-07-30 except D6's password rotation, which is an action on the VNC
   server rather than a code change.
2. **The crash/wedge class.** B3, B4, B5, B6 are done — the device can now always recover to a
   usable launcher on its own, and the log that diagnoses a SIGILL finally reports it.
   **B1 + B24 are deployed and verified on RW09 (2026-08-03)**, all three components rebuilt, with the
   original 16bpp-after-a-VNC-session failure reproduced and shown fixed. **B2 (latched `.held`) is
   done the same day** and now has a host-side regression (`tests/gamepad_latch_test.c`), which also
   settled that this bug *was* testable without a device — the touch coordinate is a plain argument and
   evdev is just `read(2)`. **B13g followed it immediately, as a deletion** rather than the reorder its
   one-line row implied, so `gamepad_set_touch_regions()` now has zero callers.
   **Outstanding first:** B22's panel table has **one** row unconfirmed — platformer, which needs a
   USB keyboard or gamepad attached. ← **next, and it needs a human at the panel**
   **B7 and B9 are done** (2026-08-02) — they were the available quick wins. Doing them turned up
   three new items, all confirmed on the panel the same day: **B23** (backlight slider previewed to a
   sysfs node that does not exist — **done 2026-08-03**), **B24** (no game asserted bpp, which made B1
   reachable from a bare SSH launch — done with it) and **B25** (a `vnc_client` the deploy scripts
   cannot kill, which is B20's predicted failure actually happening). **B25 is the cheap one left** in
   this class, and it was hit again on 2026-08-03 while reproducing B24: `killall vnc_client` still
   matches nothing, and the process has to be found by `readlink /proc/*/exe`.
3. **The per-app layout pass** — done 2026-08-02 (B3e, B3i, B3j, B3k, B13d, B13k). It also
   established that **`touch_inject` cannot work on this device at all** (no `CONFIG_INPUT_UINPUT`),
   which is why C6 had to be rewritten around framebuffer capture instead.
4. **B15** — stop the scripts from being able to hurt you. B17–B20 are the rest of Phase 2 and are
   cheaper; take them in the same pass if the appetite is there.
5. **F1 (ALSA)** — biggest user-visible improvement in the project.
6. **Deep clean the device** (`--deep-clean`), then **F2 (DSS overlays)**.
7. **Open the unit and inspect the hardware** — done 2026-07-30. Full teardown, folded into
   [`SYSTEM_ANALYSIS.md`](SYSTEM_ANALYSIS.md). Serial console declined — see [Closed](#closed).
8. Everything else as appetite allows: B3g/B3h, B10–B12c, the six open B13 rows, B14, and all
   of C1–C8. **These are genuinely unranked**, not deprioritised — C6's smoke-test harness and C1's
   `MAX_INPUT_DEVICES` one-liner are both cheap enough to take out of order.
