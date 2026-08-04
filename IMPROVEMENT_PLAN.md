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

## Open right now

Everything not in [Closed](#closed), so "what is open?" costs one screen instead of scrolling past a
~1100-line archive. **This table is an index, not a second copy** — status and reasoning live in the
entry, and if the two disagree the entry wins. Deliberately unranked; see
[Suggested order of work](#suggested-order-of-work).

| Item | What | Status | Needs |
|---|---|---|---|
| [B3c](#b3c-second-unit-measurement-of-the-touch-dead-band--partly-done-2026-08-01) | Second-unit touch dead-band sweep | partly done | a human at `.53`'s panel |
| [B12c](#b12c-scummvm-opl-tempo-unverified-after-the-mono-mixer-fix--open) | ScummVM OPL/AdLib tempo unverified | open | an AdLib-capable game installed |
| [F1](#f1-port-audio-from-oss-to-alsa--open-highest-user-visible-payoff) | Port audio OSS → ALSA | open | — **highest user-visible payoff** |
| [F2](#f2-use-the-dss-overlay-planes--open-biggest-performance-win-available) | Use the DSS overlay planes | open | — **biggest performance win** |
| [F4](#f4-surface-the-madc--temperature-and-analogue-inputs--open) | Surface the MADC (temperature, analogue in) | open | — |
| [F5](#f5-roomwizard-to-roomwizard-wireless-via-the-802154-radio--open) | RW-to-RW wireless via 802.15.4 | open | hardware that is not fitted |
| [F6](#f6-multi-touch-via-direct-i2c--open) | Multi-touch via direct I2C | open | — |
| [F7](#f7-use-nand-mtd4-scratch-for-persistent-data--open) | NAND `mtd4` for persistent data | open | — |
| [F8](#f8-smooth-led-effects--open) | Smooth LED effects | open | — |
| [F9](#f9-ship-binaries-as-github-releases--open) | Ship binaries as GitHub releases | open | — removes the toolchain from the deploy path |
| [C1](#c1-extract-the-shared-evdev-layer--open) | Extract the shared evdev layer | open | — the `MAX_INPUT_DEVICES` stopgap is spent |
| [C2](#c2-split-device_toolsc-2651-lines--open) | Split `device_tools.c` (2651 lines) | open | — |
| [C4](#c4-make-the-common-library-use-the-logger--open) | Make the common library use the logger | open | — |
| [C5](#c5-fix-text_truncate-and-the-8px6px-font-width-confusion--partly-done-2026-08-02) | `text_truncate` / 8px-vs-6px font width | partly done | — |
| [C6](#c6-extend-the-host-buildable-test-harness--open) | Extend the host-buildable test harness | open | — |
| [C7](#c7-run-shellcheck--open) | Run shellcheck | open | `shellcheck` is not in this WSL |
| [C8](#c8-retire-hardware_diag--it-is-a-second-copy-of-a-device_tools-tab--open-confirmed-2026-08-02) | Retire `hardware_diag` (duplicate of a tab) | open, confirmed | — |
| [C9](#c9-gate-the-scummvm-binary-too--and-gate-it-unstripped--open-measured-2026-08-03) | Gate the ScummVM binary, **unstripped** | open, measured | — |

Also still needing a human at a panel, tracked inside their Closed entries rather than as open rows:
**brick_breaker levels 5+** (grey striped bricks, B13b) — postponed at the reporter's request, so
make the level reachable (`--level N` or a debug entry) instead of asking again; and B13h's SPEED UP
→ SLOW DOWN monotonicity, which is **not** level-gated and is a one-minute check at level 1.

---

## Phase 0 — Do these first (no risk, high leverage)

Nothing here can break a running device. D1–D6 are all done — see [Closed](#closed).

**Shipped 2026-08-03 (host-side only, nothing applied to a device yet):**

- `set-hostname.sh` — one implementation, writing **both** files, called from both bring-up paths
  (`commission-roomwizard.sh` offline against `$ROOTFS`; `setup-device.sh <ip> --hostname NAME` over
  SSH). It refuses a name that is not RFC-1123, backs up **once** so a second run cannot overwrite
  the vendor original with its own output, and **refuses to write at all if the `localhost` entry
  would be lost** — the same negative-control shape as the loopback guard in
  `commission-roomwizard.sh`.
- `setup-device.sh` now links the image's existing `avahi-daemon` into `rc5.d` (S30), so a named
  unit answers to `<name>.local`.
- **Prerequisite fixed in the same change:** `setup-device.sh` rejected anything that was not strict
  IPv4 and exited, while a *second, weaker* validator further down also accepted a DNS name — dead
  code that could never run. So the script looked like it took `rw09.local` and refused it, which
  would have made mDNS buy nothing. One validator now, accepting an address or a name; a
  digits-and-dots string that is not a valid address is still rejected as the typo it is.
- `commission-roomwizard.sh` also now honours a pre-set `$ROOTFS`. Its own error message tells the
  operator to `export ROOTFS=/mnt/rw`, but the assignment was unconditional, so that advice never
  worked — and it is what lets the offline path be exercised against a copy of a real rootfs.

**Verified on the host:** the validator table-driven over 13 good/bad names in both directions; the
whole offline path run against a copy of the real vendor `etc/`, 
`127.0.0.1 <name>` present, `localhost` intact and the backup still holding the vendor original
after a second run; and the localhost guard tripped deliberately with `161.218.140.212 RW09
localhost` to confirm it refuses and changes nothing.

**Residue — one caveat, and one unit unchecked:**

1. **RW09 done 2026-08-03.** `--hostname rw09` applied: `hostname` and `/etc/hostname` are `rw09`,
   `/etc/hosts` is `127.0.0.1 localhost` + `127.0.0.1 rw09`, and `S30avahi-daemon` is linked and
   persistent. `rw09.local` resolves from Windows (`ping rw09.local` → 192.168.50.73).
2. ⚠️ **`.local` does not resolve from WSL, which is where the deploy scripts run.** WSL's
   `/etc/nsswitch.conf` is `hosts: files dns` — no mDNS module — so `./setup-device.sh rw09.local`
   passes the validator, reaches the SSH step and then fails to resolve. Windows resolves it fine.
   Fix is host-side and one package: `sudo apt install libnss-mdns` in WSL. **Until that is done the
   mDNS payoff applies to Windows-side `ssh` only, not to the build/deploy path.**

**Correction to this entry's own scope, measured 2026-08-03.** RW09's *deployed* `/etc/hosts` was
**not** the vendor line — it read `192.168.50.73 RW09`, a hardcoded self-IP, presumably edited at
some point in this unit's life. 

**avahi cost, measured on RW09 2026-08-03 — cheap, keep it.** ~3.9 MB RSS total (2424 kB for
`avahi-daemon` plus a 1512 kB chroot helper) out of 234 MB, with 164 MB free at the time. It did
**not** drag dbus awake: `dbus-daemon` was already running at a lower PID (2231 vs 267x) and uses
1692 kB of its own regardless. Recorded in `SYSTEM_ANALYSIS.md#35-network-and-power`.

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

**A second unit is available: `192.168.50.53`** (noted 2026-08-03 — nothing in this repo mentioned it
before). It has the components deployed and is reachable over SSH, so this is a panel-time task, not a
hardware-acquisition one. Two caveats: as of that date it is still running **pre-B25 deploy scripts**
(`./setup-device.sh 192.168.50.53` has not been re-run, and an orphan `vnc_client` had survived a
`stop` there), and `touch_raw` needs a human at the panel — SWEEP and INSET are finger measurements.

### B3g. ScummVM's `rw_content_area` is invisible until you know it exists — done 2026-08-03

See [Closed](#closed).

### B3h. ScummVM's config file location depends on the working directory — done 2026-08-03

See [Closed](#closed).


### B10. ScummVM `getMillis()` overflows at 24.85 days — done 2026-08-03

See [Closed](#closed).

### B12b. ScummVM: exiting a game quits ScummVM instead of returning to the launcher — done 2026-08-03

See [Closed](#closed). **Confirmed on the panel** with King's Quest 1: leaving the game returns to the
ScummVM main window. Read the Closed entry before touching `quit()` — it is *not* the cause and must
keep its `exit(0)`, and `kFeatureNoQuit` is a trap.

### B12c. ScummVM: OPL tempo unverified after the mono-mixer fix — open

Open verification task — play the KQ3 intro on the device and compare against a reference
recording. The mono mixer and the `SOUND_PCM_READ_RATE` read-back were supposed to fix half-speed
OPL; nobody confirmed it on hardware.

**No longer blocked on "there is no game data", but not yet satisfiable either.** RW09 had no game data
at all when this was measured on 2026-08-03; a game was installed the same day — **King's Quest 1
(CoCo3), `agi` engine**, data under `/home/root/.local/share/scummvm/`. That is the wrong target for
this check: KQ3 is `sci`, and the CoCo3 platform's AGI sound is not the AdLib/OPL path this item is
about — the installed target's `guioptions` lists `sndNoSpeech hercGreen hercAmber cga ega amiga 2gs
atari macintosh`, with no AdLib among them. So B12c still needs an OPL-driven target added. What *is*
now settled is the prerequisite that blocked everything: adding a game works, via `Add Game...` (a
touch file browser), and the resulting entry persists to `/opt/games/scummvm.ini`.

### B13. Game-specific bugs — done 2026-08-03

**All rows are closed.** B13a, B13c, B13d, B13g, B13k and B13l closed earlier; B13b, B13e, B13f,
B13h, B13i and B13j closed 2026-08-03. See [Closed](#closed).

### B14. Blocking `usleep()` inside input/update paths — done 2026-08-03

See [Closed](#closed).

### B22. Game-over screen — done 2026-08-03

See [Closed](#closed). **All seven games are now panel-confirmed** — platformer last, on 2026-08-03
with a gamepad attached: its game-over screen paints *without* a tap and `RESTART`/`EXIT` respond to
touch. Read the Closed entry before touching `gameover_update()`, including the `NAME_ENTRY` exception,
which is the one place where the "fall through in the same call" instinct is wrong.

---

## Phase 2 — Script safety

**Phase 2 is closed.** B15, B17, B18, B19a and B19 are all done — see [Closed](#closed).

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

### F9. Ship binaries as GitHub releases — open

**Why this may be the highest-leverage item in Phase 3:** the build is the slowest and most
environment-bound step in the entire flow — WSL, an ARM cross-compiler, ScummVM's ~1m35s–2m20s link
that also deletes `native_apps/common/*.o` twice — and it is **pure overhead whenever the source has
not changed.** Anyone who wants to put apps on a device today must reproduce the whole toolchain
first. The artifacts suit distribution unusually well: everything ships `-static`, so there is no ABI
surface to match against the device's glibc.

Design, so it does not have to be re-derived:

- **The host pulls the tarball; the device is untouched.** The existing `scp` path stays exactly as
  it is. Nothing new runs on the device and there is no CA-certificate problem to solve on a 2022
  vendor image.
- `deploy-all.sh` and the per-component scripts gain `--from-release <tag>` that skips **only** the
  build step.
- **The release must carry the md5 manifest.** Deploy-time verification currently compares against a
  local `build/`, which will not exist on a build-free path.

Two caveats to record before anyone tries it:

- `base/version.o` re-embeds the build date on every link, so releases are **not byte-reproducible**.
  The md5 list must be *generated per release*, not asserted against a known-good set.
- A release must publish **binaries only, never configs**. This tree's history contains
  `/etc/hosts` with a corporate address (D7) and the password rotation of D6; a glob that sweeps up
  `*.conf` would publish exactly the things those two entries exist to have removed.

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

**They have already drifted** — `MAX_INPUT_DEVICES` was 16 in the VNC client but 32 in the other two,
so a keyboard on `event17` worked everywhere except VNC. **That constant is now resynced to 32 (done
2026-08-03) — and it is the second manual resync, which is the argument for this item rather than a
substitute for it.** The "clear errno before the read loop" hardening still exists only in the ScummVM
copy.

The ScummVM copy is defensible (C++, different event model, links only 4 common objects). **The
VNC copy is not** — `vnc_client/Makefile:21-29` already compiles five objects from
`../native_apps/common/`; it could link `gamepad.o` too.

**Fix:** extract classifier + scan + config parser into `common/evdev_scan.c` (~150 lines). The cheap
`MAX_INPUT_DEVICES` stopgap is spent; there is no quick win left here.

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

### C9. Gate the ScummVM binary too — and gate it unstripped — open, measured 2026-08-03

Two halves, and the second is why this is not a one-line addition.

`scummvm-roomwizard/build-and-deploy.sh` never calls `check-arm-safe.sh`, so the largest ARM binary in
the project — the only C++ one, and the one doing the most division — ships through no SIGILL gate at
all. `native_apps` has had a hard-zero gate since D2, and the whole point of that gate is that a
hardware `sdiv`/`udiv` on this Cortex-A8 is the worst failure mode available: blank screen, no output,
no log, indistinguishable from "the app didn't start".

But it cannot just be bolted on where the script strips, because **the gate is unreliable on a stripped
binary, and ScummVM is the case that demonstrates it.** The A/B that settles it, on one file: the gate
reports **8–9 hardware divide instructions** in the stripped binary and **zero** in the unstripped one —
the *same binary*, before and after `strip`, which removes the symbol table and cannot alter `.text`.
Without symbols, objdump cannot separate code from the literal pools embedded in `.text`, so four-byte
constants decode as plausible instructions. The script's header already documents this mode from a
single `vnc_client` phantom and prints a warning on symbol-less targets. What is new:

- On ScummVM it fires ~9 times, not once, so it reads like a real and widespread problem.
- **The phantom operands are not reliably invalid.** `udiv pc, fp, sl` and `sdiv sp, sp, r5` are
  architecturally UNPREDICTABLE and easy to dismiss, but `udiv r3, fp, r9` and `udiv r7, r1, lr` are
  legal encodings that look exactly like compiler output. So "eyeball the operands" is **not** a
  sufficient triage rule — the symbol table is what settles it, nothing else.

So: call it from `build_scummvm()` **before** `strip_binary()`, on the unstripped artifact. Do not put
it after the strip step, and do not add an allowlist of offsets — they move on every build, because
`base/version.o` re-embeds the build date on every link, which alone shifts every address after it.
The ScummVM binary was confirmed clean unstripped on this date, so adding the gate should be a no-op
that stays a no-op.

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

**B12b. Leaving a game terminated ScummVM — and both the recorded cause and the prescribed fix were
wrong** — done 2026-08-03, **confirmed on the panel** with King's Quest 1: leaving the game returns to
the ScummVM main window, not to `app_launcher`.

The entry said `OSystem_RoomWizard::quit()` "calls `exit()` unconditionally rather than setting a flag",
and prescribed comparing with "the SDL backend's `_quit` flag + launcher loop". Three things are wrong
with that, all checkable by reading upstream:

- **`OSystem_SDL::quit()` does `destroy(); exit(0);` too.** There is no `_quit` flag to copy. The
  backend it points at as the good example behaves identically to ours.
- **Nothing in the game-exit path calls `quit()` at all.** Across the whole tree the only caller is
  `common/recorderfile.cpp`. `quit()` was never on the path being blamed.
- **The decision is in `base/main.cpp:832`**, in the launcher loop: it `break`s out — ending the
  process — when a game returns `kNoError`, no return-to-launcher was requested, and neither
  `kFeatureNoQuit` nor `gui_return_to_launcher_at_exit` is set. That is upstream's default on **every**
  platform, which also explains the "the same build returns to the launcher correctly on Ubuntu"
  observation: not a backend difference, but a desktop config that had the option on — it is a Global
  Options checkbox, registered `false` by default in `base/commandLine.cpp:383`.

**Fix:** `initBackend()` sets `gui_return_to_launcher_at_exit` true on first run, behind `!hasKey()` so
the user keeps control of it, next to the `rw_content_area` default from B3g. Nothing in `quit()`
changed, and a comment there now says so — with the reason — because "replace `exit()` with a flag" is
exactly what the next reader will try.

**The other way to satisfy `main.cpp:832` is `kFeatureNoQuit`, and it is a trap.** It would also hide
the `Quit` button on both the ScummVM launcher (`gui/launcher.cpp:264`) and the in-game global menu
(`engines/dialogs.cpp:90`), and make `launcherDialog()` loop until a game is started
(`base/main.cpp:109`). On this device quitting ScummVM is the **only** way back to `app_launcher` and
the native games, so that would trap the user inside ScummVM — a worse bug than the one being fixed.
The option chosen leaves both Quit buttons alone, and a framebuffer capture after the fix showing the
launcher's `Quit` still present is the control for exactly that.

**Verified on the panel the same day, and by a real session rather than a scripted one.** King's Quest 1
was installed through `Add Game...`, played, and left — and it returned to the ScummVM main window. That
single session also confirmed **B3h and B3g** end to end from a direction my own test could not reach:
the new game's `[kq1-coco3]` entry persisted into `/opt/games/scummvm.ini` (not into a cwd-dependent
stray, and neither stray reappeared), and `gui_return_to_launcher_at_exit=true` was read back *out* of
that same file to produce the behaviour. Three items, one tap sequence.

**C9. The ScummVM binary is not gated for sdiv/udiv, and gating it stripped would cry wolf** — see
[C9 in Phase 4](#c9-gate-the-scummvm-binary-too--and-gate-it-unstripped--open-measured-2026-08-03).

**B3h + B3g. One config file, at one absolute path, with its options written into it** — done
2026-08-03, both in `roomwizard.cpp`, built and deployed to RW09 together.

**B3h** — `OSystem_RoomWizard` now overrides `getDefaultConfigFileName()` to return
`/opt/games/scummvm.ini`. It had inherited the base `OSystem`'s bare relative `"scummvm.ini"`, which
`Common::FSNode` resolves against the process's **current directory**; the init script does not `cd`
and `app_launcher` `execl()`s without `chdir()`, so the location was an accident of the launch method
and RW09 had accumulated three files. `OSystem_POSIX` avoids this with an absolute `$HOME/.config`
path, but this backend derives from `ModularGraphicsBackend`, not from it. `/opt/games` is next to the
binary, the icons and the game data, and does not depend on `$HOME` — which the init script's
environment does not set.

**B3g** — `initBackend()` writes `rw_content_area=safe` on first run behind `!ConfMan.hasKey()`, so
the option appears in the file instead of being an undocumented reader. `setAndFlush`, not `set`,
deliberately: `quit()` calls `exit(0)` and bypasses ScummVM's normal shutdown flush, so a plain `set()`
can be lost on the one exit path this device actually takes. `ROOMWIZARD_CONTENT_AREA` still wins at
read time and is deliberately *not* persisted — it is documented as a one-off override.

**Verified on the device, with the negative control that matters — the same binary run from two
different working directories:**

| Check | Result |
|---|---|
| baseline mtimes of all three inis | recorded before either run |
| run with `cd /` (the boot-launcher case) | `/scummvm.ini` and `/home/root/scummvm.ini` mtimes **unchanged**; only `/opt/games/scummvm.ini` advanced |
| run with `cd /home/root` (the SSH case) | both strays still **unchanged** — this is the run that used to create `/home/root/scummvm.ini` |
| `rw_content_area` in the file | present, `=safe`, alongside the migrated `extrapath` / `iconspath` / `browser_lastpath` / `gui_browser_show_hidden` |
| second run's mtime | **unchanged** — the `!hasKey()` guard declines to rewrite on every launch, which is the behaviour that makes it safe to leave in `initBackend()` |

The cwd runs are the control: without them, "the setting is in `/opt/games/scummvm.ini`" is equally
consistent with the old code, because a `cd /opt/games` run would have put it there too. What proves
the fix is the file that did **not** get written.

`/scummvm.ini` was copied onto the new path first (so its settings survived) and both strays were then
deleted. **The archived claim that `/scummvm.ini` was "the one with the real game list" was wrong** —
dumped before touching it, it held seven lines of paths and GUI state and *no game entries at all*, as
did the `$HOME` copy. Nothing on RW09 had a configured game, so the migration risked nothing; do not
repeat the "real game list" claim. Docs corrected in `scummvm-roomwizard/README.md`, whose paragraph
describing the cwd-relative behaviour as a device fact is now the fixed behaviour instead.

**B10. ScummVM `getMillis()` overflowed at 24.85 days** — done 2026-08-03. The multiply now happens in
`uint32`, so it wraps cleanly at 49.7 days instead of overflowing a signed 32-bit `time_t` at 24.85 —
which was UB and took long-press detection, cursor timing, the touch-feedback fade and
`DefaultTimerManager` with it. All six `getMillis()` consumers compute `now - _last` in `uint32`, so
they are wrap-safe and needed no change. **Built and deployed 2026-08-03** (the source-only half was
committed in `4c6feef` and left unbuilt deliberately; the build is the multi-minute one and its
`build-and-deploy.sh` does `rm -f native_apps/common/*.o` at two points, so it cannot run concurrently
with a native_apps build). Verified three ways, because the overflow itself is **not observable** —
it needs 24.85 days of uptime:

- **The binary on the device is the new one.** `md5sum /opt/games/scummvm` == the local stripped
  artifact (`84ceb107…`, was `e546214f…`), and the ScummVM launcher's own version line, read off a
  decoded `--bpp 16` framebuffer capture, reads `2.8.1pre (Aug 3 2026 17:19:35)` — the build that
  finished at 17:19:59. A build stamp rendered on the panel is the one identity check that cannot be
  satisfied by a stale binary.
- **It still runs.** Launched over SSH, `fbset` reports 16bpp as this backend intends, and the
  launcher renders correctly with the overlay inside the safe rect.
- **The arithmetic, with a negative control.** A host-compiled check of both expressions at
  `tv_sec` diff 2147484 (24.85 days): the old signed form yields **−2147482796**, the new `uint32`
  form **2147484500**, and at 4294968 s it wraps to 704 as intended. The negative control is the point
  — without it "the new expression returns the right number" says nothing about whether the old one
  was ever wrong.

**B13 + B14. The last six game bugs, and the blocking sleeps** — done 2026-08-03. All built with zero
warnings, `check-arm-safe.sh` at a hard zero across 31 ARM binaries, the three host regressions
(`touch_calib_test`, `gamepad_latch_test`, `framebuffer_bpp_test`) passing, and deployed to RW09 with
18/18 md5 verified. **Verification stops at the first screen** — no `/dev/uinput`, so all six games
were SSH-launched, confirmed alive at 32bpp with a decoded `/dev/fb0`, and everything past the welcome
screen needs a human at the panel (see C6). **Panel verification 2026-08-03, in two sittings:** with a
gamepad attached, **platformer, pong and frogger** were played and reported working (which also
confirmed B2 — buttons no longer latch), and later the same day **tetris**, **snake** and **samegame**
were driven into their changed states:

| Game | Effect | Result |
|---|---|---|
| tetris | I-piece wall and floor kicks (B13e) | **pass** — it does not rotate out of the wall; behaves as expected |
| snake | first food pickup, stray cell at the grid origin (B13f) | **pass** — the reporter remembered the artifact from before and it is **gone**, which makes this a before/after confirmation rather than an absence of evidence |
| samegame | MENU/EXIT during the pre-game-over pause (B14), taps just outside the grid | **pass, reported as "I think it works"** — recorded as the hedge it was. Nothing looked wrong; nobody deliberately timed a tap into the ~300 ms window |
| brick_breaker | levels 5+ grey striped bricks (B13b), SPEED UP → SLOW DOWN monotonicity (B13h) | **postponed 2026-08-03 at the reporter's request** — reaching level 5 by playing is *"a considerable amount of effort"* |

**brick_breaker is the only gameplay effect still unobserved, and asking again is the wrong move** — the
cost is real and it falls on a human every time. The cheap unblock is to make the state reachable
instead: a level-select or start-at-level path (a `--level N` argument, or a debug entry in the pause
dialog) turns a long play session into one launch, and it would serve any future level-dependent bug
too. B13b's fix is the one that most wants it, because indestructible bricks *only* exist from level 5
up. Note the two effects are independent — the SPEED UP → SLOW DOWN sequence needs no particular level
and could be checked at level 1 in under a minute; only the grey bricks are gated.

- **B13b** — `brick_breaker.c`: indestructible bricks stored `health = -1` and five other sites tested
  `health <= 0`, so from level 5 up they were invisible **and** had no collision. Both their bounce
  path (`if (b->fireball)` / else fall through) and their diagonal-stripe rendering were already
  written and were dead code, which is what confirms the marker was the defect. Fixed with one
  predicate, `brick_is_destroyed(b)` ⇔ `health == 0`, plus a named `BRICK_HEALTH_INDESTRUCTIBLE`. No
  `health <= 0` test remains in the file. **Levels 5+ are now harder than anyone has played them.**
- **B13h** — `brick_breaker.c`: `normalize_ball_speeds()` divided out `lv->speed_mult` (a no-op — it
  multiplied it straight back) but never the previously-applied `effect_mult`, so SPEED UP / SLOW DOWN
  compounded on a figure that already contained them. Going +2 → +1 gave `1.5 × 1.25 = 1.875`: **SLOW
  DOWN made the ball faster**, exactly as reported. Fixed by making the multiplier *derived* rather
  than accumulated — `Ball.base_speed` holds the speed without it, `ball_apply_speed()` is now the only
  writer of `Ball.speed`, and the per-brick `BALL_SPEED_INC` goes onto the base. `BALL_MAX_SPEED` still
  clamps the **effective** speed, deliberately: an 11 px/frame cap is what stops the ball tunnelling
  through a brick, so it belongs on the speed the ball travels at, not the base. At effect level 0 the
  emitted behaviour is unchanged, which is what keeps the default feel intact.
- **B13e** — `tetris.c`: no wall kick. Added `try_rotate()`, which replaces four identical
  `if (!check_collision(...)) rotation = new` sites (two touch, clockwise and counter-clockwise
  gamepad) and tries in place → ∓1 → ∓2 → the same nudges one row up. Two is what an I-piece needs
  (its rotation centre sits one cell in from the end); the upward floor kick is what frees a piece
  resting on the stack, and it is representable because `check_collision()` permits negative `board_y`
  and `lock_piece()` skips those cells. A rotation that fits nowhere is still refused.
- **B13f** — `snake.c`: the body shift writes only `body[1..length-1]`, so `length++` on eating
  published `body[length]` — whatever an earlier, longer tick left there, and `{0,0}` on the very
  **first** grow because the struct is zero-initialised. That put a detached cell at the grid origin
  which was drawn, counted by the self-collision test, and fatal if the head stepped on it. It healed
  on the next tick's shift, which is why it read as a one-frame glitch. The new segment now takes the
  cell the tail just vacated, captured before the shift.
- **B13i** — `platformer.c`: **the plan's prescribed `break` would not have fixed this.** A successful
  stomp sets `player.vy = STOMP_BOUNCE` (negative), so the second of two overlapping enemies failed
  `vy > 0` and killed the player — but a `break` leaves enemy #2 alive directly under the rising
  player, and at 6.0 px/frame against a 22 px enemy the overlap persists ~3 more frames with `vy`
  already negative, so the player dies on the *next* frame instead. `check_enemy_collisions()` now
  resolves the whole frame before acting: each enemy is judged against the velocity the player arrived
  with, everything genuinely landed on dies, the bounce and sound fire once, and the player only dies
  if nothing was stomped. Residual narrow case, left alone deliberately: enemies at *different* heights
  where one is a side-hit and one a stomp leaves the side-hit enemy alive and fatal next frame — all
  enemies share one height (`TILE_SIZE - 2`), so same-platform stacks, the reachable case, are clean.
- **B13j** — `samegame.c`: `pixel_to_grid()` divided before range-checking, and C integer division
  truncates toward zero — a delta in `(-block_size, 0)` yields `0`, not `-1`, so the `col < 0 ||
  row < 0` guard could never fire and every tap within one block outside the left or top edge selected
  column or row 0. Now rejects a negative delta before dividing. The high edges were always fine;
  truncation rounds those down, into range.
- **B14** — the blocking sleeps. `common/hardware.c` gains a non-blocking sibling to the existing
  (and still correct, for shutdown) blocking `hw_blink_led`: `LedPulse` + `hw_led_pulse_start/update/
  stop/active`. **It takes `now_ms` as a parameter, not a `get_time_ms()` call** — `hardware.c` is
  linked by `vnc_client`, which does **not** link `common.c`, so calling into it would break that
  link; a caller-supplied clock is also host-testable. It went in the library rather than as three
  more private copies because four games already carry their own `LEDEffect`. Converted: tetris'
  game-over pulse (1.2 s, inside `update_game()`, and it ran *before* the game-over screen was drawn
  so the player stared at the old playfield); pong's four win/loss pulses, folded into the
  `enter_game_over()` helper that already branched on `game.winner` — the sound moved with them, so
  four duplicated blocks became none; brick_breaker's 300 ms lost-ball flash and 50 ms power-up flash;
  and the 100 ms game-start green flash in tetris, pong and samegame (samegame's went through its own
  `start_led_effect(1)`, which is already exactly a 100 ms green flash — no second mechanism in one
  file). Games that can cancel a pulse mid-flight call `hw_led_pulse_stop()` from `reset_game()` /
  `init_level()` so a flourish cannot flash into the next round.
  samegame's was the different one: a 300–1500 ms `while (running) { draw; fb_swap; usleep }` that
  never called `touch_poll()`, so **MENU and EXIT were dead for its whole duration**. It is now a
  fourth animation state, `ANIM_GAMEOVER_DELAY`, so the ordinary main loop keeps polling and drawing —
  its dirty flag already forces frames on `anim_state != ANIM_NONE`, and `handle_input()` checks both
  buttons *before* it gates the grid on the animation. Bonus: `update_game()` returns early unless
  `SCREEN_PLAYING`, so pausing mid-delay holds the overlay back instead of dropping it over the pause
  dialog.
  **The remaining `usleep()` loops in the games are deliberate and stay:** the 3 × 100 ms exit
  flourishes immediately before `running = false`. There is nothing left to be responsive to, and
  `hw_blink_led()` exists for exactly that case.

**D6. Secrets** — done 2026-08-03. `vnc_client/vnc_client.conf` held a plaintext password and was
tracked; it became untracked + gitignored on 2026-07-29, with `vnc_client.conf.example` as the
template and `chmod 600` on deploy — which left the old password live in git history. Closed the
prescribed way (**rotate, don't rewrite history**): the x11vnc password on the RPi server
(`192.168.50.56`, `x11vnc-roomwizard.service`, `-rfbauth /home/pi/.vnc/x11vnc_passwd`) was
regenerated with `x11vnc -storepasswd`, the service restarted, and the new value written to
`/opt/vnc_client/vnc_client.conf` on **both** clients (`.73`, `.53`, mode 0600) plus the dev host's
gitignored copy. The old file is kept as `x11vnc_passwd.bak` on the pi.
Verified with a positive **and** a negative control, because "it connected" alone does not prove the
server changed: on RW09 the client authenticated and streamed the live 1920×1080 desktop at 16bpp
(framebuffer capture, `--bpp 16`), the same binary run with `--password <old>` logged
`VNC connection failed: password check failed!`, and `.53` independently logged
`VNC authentication succeeded`.
⚠️ **VncAuth truncates the password to 8 characters** — that is the protocol, not a bug here, and it
applies at both ends (x11vnc's passwd file is 8 bytes; libvncclient truncates identically). So only
the first 8 characters are the actual secret. The previous password was also longer than 8, so
nothing changed operationally, but do not pick a password whose distinguishing characters are past
position 8. The password is still plaintext in three files on two devices and is sent under the
legacy VncAuth scheme — it is a throwaway LAN credential by design, as
`vnc_client.conf.example` says.

**B17. `commission-roomwizard.sh` could delete the network config, and leaked the password** — done
2026-08-03, both halves host-verified against the pre-fix code.

*The sed range.* `sed -i '/^auto eth0/,/^$/d'` plus the same range for `/^iface eth0/`. **A sed range
whose end address never matches runs to EOF**, so on an `interfaces` file whose eth0 stanza is last —
or that simply has no blank lines — it deleted everything from eth0 onwards, and any `auto lo` /
`iface lo` below that point went with it. The device then boots with no loopback and no SSH, which on
a freshly commissioned card means re-mounting it on the dev host.
Replaced with a stanza-aware `strip_eth0_stanzas()` awk filter, built into a temp file and installed
with `cp` (so the target is either the old file or the whole new one, never a half-edited in-place
result), behind an **assertion that the loopback survived** — losing it is the entire failure mode,
and one `grep` makes it impossible rather than unlikely.
Exercised in a harness that `eval`s the real function out of the script and runs the old sed and the
new filter over the same four inputs. **Two of the four cases were defects nobody had recorded:**

| Input | Old sed | New filter |
|---|---|---|
| lo first, blank-line separated (the case it was written for) | correct | identical output |
| **eth0 stanza last, `lo` after it** | **loopback deleted — B17 exactly** | loopback kept |
| no blank lines, a `usb0` stanza after eth0 | **`usb0` silently eaten too** | `usb0` kept |
| **`auto lo eth0`** (one auto line, two interfaces) | **`auto eth0` left behind** while `/^iface eth0/` deleted its stanza → a dangling `auto` entry for an interface with no `iface`, which fails at `ifup` | token removed, `auto lo` kept |

The last row is why the fix is a parser and not a better regex: per `interfaces(5)` an `auto` /
`allow-*` line carries a **list**, and an `iface` stanza owns every following line until the next
stanza keyword *or* a blank line — "until the next blank line" assumed a formatting convention the
file is under no obligation to follow.

*The password leak.* `openssl passwd -6 "$PASSWORD"` put the plaintext in `/proc/<pid>/cmdline`,
world-readable for the life of the process, two lines after a `read -s` taken specifically to keep it
off the screen. Now `printf '%s\n' "$PASSWORD" | openssl passwd -6 -stdin` — `printf` is a shell
builtin, so it forks nothing and the password reaches no process's argv at all. Measured, because
"probably too fast to catch" is not an argument: a `/proc` scanner looping against 60 hash
invocations caught the old form **dozens** of times and the new form **zero** (the scanner's own
`grep` pattern had to move into a file, and the harness into a file, or both self-match and inflate
the count — the first two runs did exactly that). Also checked the round trip on three passwords
including one with a space, a backslash and a `$`: `crypt.crypt(pw, hash) == hash` and **no trailing
newline leaks into the hash**, which is the one way `-stdin` could have silently produced a
password nobody can log in with.

**B11 + B12 + C1's quick win. VNC: leaked a framebuffer per reconnect, never noticed a dead peer,
and could not see `event17`** — done 2026-08-03, keepalive verified on RW09's live socket.

**B11** — `rfbClientCleanup()` frees `raw_buffer`, `ultra_buffer`, `desktopName` and `serverHost` but
**not** `client->frameBuffer`, which `vnc_malloc_fb()` allocated and nothing freed. With
`RECONNECT_MAX_ATTEMPTS 0` (unlimited) against a 1080p host that is ~8.3 MB per drop on a 234 MB
device. Both teardown sites now go through one `vnc_client_destroy(rfbClient **)` that frees the
buffer, NULLs it and then cleans up, so a third site cannot reintroduce the leak.

**B12 — implemented as `SO_KEEPALIVE` + TCP tuning only, and the other half of the prescribed fix was
deliberately NOT done.** The entry said to "track the time of the last successful
`HandleRFBServerMessage` and break after N seconds". **That would disconnect healthy sessions.**
Steady-state `FramebufferUpdateRequest`s are *incremental*, so a server with a static screen correctly
sends nothing for minutes — and a wall-mounted panel showing an unchanging dashboard is this
component's main use case. Silence is not evidence of death; an unacknowledged TCP segment is. So
`vnc_enable_keepalive()` arms `SO_KEEPALIVE` after `rfbInitClient()` (the socket does not exist
before it) and tightens the kernel defaults from 2 h idle + 9 probes × 75 s (~11 min to notice) to
**idle 20 s, 3 probes 10 s apart ≈ 50 s**, which is inside the 60 s hardware-watchdog window. A dead
peer then errors the socket, `WaitForMessage()` returns < 0, and the *existing* "VNC connection lost"
branch runs the reconnect UI — no new state machine.

Verified on RW09, and this is the check worth repeating because `TCP_KEEPIDLE` is the part that could
silently fail on a 4.14 kernel: with a session up 9 s, `/proc/net/tcp` reports the connection to
`:170C` as

```text
1: 4932A8C0:C99A 3832A8C0:170C 01 ... tr=02:00000483
```

`timer_active = 02` is *keepalive*, and `tm->when = 0x483` = 1155 jiffies ≈ **11.6 s** — i.e. 20 s
idle minus the 9 s elapsed. The kernel default would have read ~7200 s, so this proves the tuning was
accepted and not merely attempted. Each `setsockopt` degrades independently: a failure warns and
falls back rather than aborting the connection.

**C1's quick win** — `MAX_INPUT_DEVICES` 16 → 32, matching `GAMEPAD_MAX_DEVICES` and
`MAX_EVDEV_DEVICES`. It bounds the scan loop and sizes **no array** (checked — its only use is
`for (i = 0; i < MAX_INPUT_DEVICES; i++)`), so the cost is 16 more failed `open()` calls per 5 s
rescan. C1 itself stays open: this resyncs a constant for the second time, which is the argument for
the shared scanner, not a substitute for it.

Not measured end-to-end: the leak was fixed by inspection (`free()` on a `malloc()`ed pointer that
nothing else released) rather than by counting RSS across ~25 reconnects. Forcing that many session
teardowns needs either a human at the panel (exit gesture → SAVE & RECONNECT) or interrupting the
shared x11vnc server on `192.168.50.56` — which **`.53` is a live client of**, so it was left alone.
Compiles at **no new warnings** (3 before, 3 after — all pre-existing: two unused parameters and one
ignored `write()` return).

**B19. Deploy hygiene** — done 2026-08-03, verified against both units. Five separate defects; the
useful part of the entry is which ones were reproduced and which are latent.

| Defect | Fix | Verified |
|---|---|---|
| No IP validation — `./deploy-all.sh vnc_client` built *everything* including the multi-minute ScummVM build, then failed at `ssh root@vnc_client` | Octet-range IPv4 check **before** the build in `deploy-all.sh`, `setup-device.sh`, `native_apps`, `vnc_client`, `usb_host`; `deploy-all.sh` names the component-in-`$1` case explicitly | pass — all guards exit 1 with no build; `--list` still works |
| No verification of what landed | `native_apps` md5-compares all 18 executables against `build/` and fails with a per-file diff | pass — `Verified 18/18`, and a **negative control** (a byte appended to `/opt/games/touch_inject` on the device) was caught and named |
| `audio_touch_test` never `chmod +x`'d | added — but see below | **latent, not reproducible from this host** |
| Only `vnc_client` cd'd to its own directory | `cd "$SCRIPT_DIR"` in `native_apps` and `usb_host`; `scummvm`'s `SCUMMVM_DIR`/`NATIVE_APPS_DIR` anchored to `$REPO_ROOT` | pass — before/after measured, see below |
| Nothing said which script version a device runs | `report_script_versions()` in `setup-device.sh`, in `--status` and after a normal run | pass — **on both units, with opposite results** |

**The chmod fix cannot be demonstrated from this host, and that is worth knowing before someone
"verifies" it.** `/mnt/c` is a DrvFs 9p mount that reports every file `-rwxrwxrwx` and silently
discards `chmod` — measured: `chmod 644 build/audio_touch_test` leaves it `-rwxrwxrwx`. So scp always
carries an executable mode from a Windows host and the missing `chmod +x` is unreachable here. It
becomes reachable from a Linux dev host, or if a build step ever produces that artifact with a tool
other than `gcc -o`. The fix is correct and removes the dependency on an accident; it is not a
behaviour change anyone can observe from Windows. Same debt shape as B15's and B17's.

**The drift report was validated against a known positive, not just a happy path** — the failure mode
this repo keeps hitting is a harness that only ever reports success:

| Unit | `disable-steelcase.sh` | `roomwizard-app` |
|---|---|---|
| RW09 (`.73`) | matches repo `76514c7d` | matches repo `82d60fcc` |
| `.53` | **DRIFTED** — device `3bf114ce` | **DRIFTED** — device `c5da12f1` |

which is exactly `.53`'s recorded state (deliberately left on pre-B25 scripts). Byte comparison is
valid because `.gitattributes` pins `*.sh` to `eol=lf`, so the working tree is LF even on this
Windows host and scp copies it unchanged — without that the check would report permanent false drift.
This closes the entry's last bullet with a command instead of "run `status` and notice a missing
section".

**Two things the fix does beyond what the entry asked for**, both because the entry's own bullets
pointed at them:

- **The deployed-binary list is now defined once** (`GAMES_BINARIES`), and the remote `chmod` receives
  it as `"$@"` through `ssh bash -s --` rather than being written out again. `audio_touch_test` was
  missing from the chmod list *because* the list existed twice; adding a third copy for the md5 check
  would have recreated the same bug by construction.
- **`native_apps` and `vnc_client` now reject an unknown mode.** `set-default` is the only mode
  `native_apps` accepts and anything else was silently ignored, so a typo deployed without doing what
  was asked. `vnc_client`'s dead `KILL_FIRST="${3:-}"` (assigned, never read) went with it.

Measured before/after for the cwd bullet, since "breaks when invoked by path" was two different
failures and one non-failure:

- `native_apps` — fails **loudly**: `bash native_apps/build-and-deploy.sh` from the repo root dies at
  the first compile, `common/framebuffer.c: No such file or directory`, exit 1. Not a silently skipped
  `./check-arm-safe.sh` as the `./` there suggests — the build never reaches it.
- `scummvm` — resolved `../scummvm` against the **cwd**, so from the repo root it looked for the tree
  in the repo's *parent*: pre-fix `clean` prints `ScummVM directory not found: ../scummvm`. Post-fix
  `info` prints `/mnt/c/work/roomwizard/scummvm`. (It previously worked from inside
  `scummvm-roomwizard/` only by coincidence — `scummvm/` is a sibling, so `../scummvm` evaluated from
  *inside* `scummvm/` also resolves to itself, which is why the repeated `cd "$SCUMMVM_DIR"` calls
  never broke.)
- `usb_host` — already used `"$SCRIPT_DIR/…"` for every local path, so the entry's "the others break"
  was too broad. It breaks for a *different* reason: `patch_dtb.py` opens `'uImage-system'` relative
  to the **cwd** (its documented contract — `usb_host/README.md`) while the script scp's that file to
  `$SCRIPT_DIR`. Invoked by path the two disagree and the kernel-image patch dies. Fixed with
  `cd "$SCRIPT_DIR"` rather than by changing the Python's contract.

Not verified end-to-end: `usb_host` and `scummvm` deploys were not run (the first patches a kernel
image, the second is a multi-minute build and neither was otherwise needed); their changes are
argument- and path-resolution only, exercised via `info`/guard paths. `bash -n` on all ten scripts and
`dash -n` on the two `/bin/sh` ones that run under busybox ash. `native_apps` rebuilt and deployed to
RW09 at **zero warnings** with `check-arm-safe.sh` at **31 binaries, hard zero**.

**B19a. `clean.sh` could delete every other component's build output** — done 2026-08-03 by
**deleting the script**, blast radius measured first. Three lines, no shebang, no `set -e`, no `cd`,
and its contents (`rm -f config.mk config.h config.log scummvm`) name the *ScummVM* tree's configure
output — so it only ever made sense run from `/scummvm/`, while living at the repo root. From the
root, measured on the dev host:

| Its line | What it actually reaches from the repo root |
|---|---|
| `find . -name '*.o' -delete` | **307 `.o` outside `scummvm/`** — all of `native_apps/build/`, the stray `native_apps/common/*.o`, and the `usb_host/linux-4.14.52/` kernel objects (a multi-hour rebuild for the xpad modules) |
| `find . -name '*.d' -delete` | `-name` matches **directories**, and `-delete` implies `-depth`: **69 empty `*.d` directories** under `partitions/` (the extracted device rootfs) are `rmdir`'d — `/etc/rc0.d`…`/etc/rcS.d`, `/etc/modprobe.d`, `/etc/network/if-up.d`, `/etc/security/limits.d` |

The `.d` behaviour was verified in a scratch tree rather than assumed, and is worse than "it removes
empty directories": a `*.d` directory whose contents are *also* `*.d` is emptied child-first and then
removed outright. Only a `*.d` directory holding a non-matching file survives, with a
`Directory not empty` error. `usb_host/modules/` — named in the original entry — holds `.ko`, not
`.o`, so that specific claim was wrong; the kernel tree it builds from is the real casualty.

**Deleted rather than hardened**, because `scummvm-roomwizard/build-and-deploy.sh clean`
(`clean_build()`) already does strictly more: `make clean` inside the tree, which handles that tree's
`.o`/`.d` correctly, plus `rm -f native_apps/common/*.o`, which is the removal that actually matters
(a stale x86 `.o` there fails the cross-build with "file format not recognized"). It is the
documented supported command, `clean.sh` had **zero callers** anywhere in the repo, and hardening it
would have meant a second implementation of clean to keep in step — the same mistake as the three
copies of the calibration fit. Same precedent as `native_apps/`'s deleted `Makefile`. Noted in
`scummvm-roomwizard/CLAUDE.md` so it is not reintroduced.

**B18. `disable-steelcase.sh` died before the watchdog bypass, invisibly** — done 2026-08-03,
reproduced on RW09 and verified there across a reboot. `set -e`, then an **unguarded**
`sed -i '/wsplatform\.conf/d' /etc/profile 2>/dev/null` as the first command, then
`touch /var/watchdog_test`. On a device with no `/etc/profile` the script therefore exited **1 at its
second command**, leaving the Steelcase software watchdog armed — a reboot every ~70 minutes — and
because `roomwizard-app-init.sh` runs it on every boot and did not check the exit status, **the
failure produced no output anywhere**.
Reproduced against the pre-fix file on RW09 with `/etc/profile` moved aside: `rc=1`, **zero lines of
output**, `/var/watchdog_test` MISSING. The same conditions with the fixed file: `rc=0`, bypass
present. Note the deployed copy on RW09 was *older than the repo's* and predated the `sed` entirely,
so reproducing this needed `git show HEAD:disable-steelcase.sh` staged to `/tmp` — the device's own
script could not have shown it.
Three changes, and the ordering is as load-bearing as the guards:

- **The watchdog bypass is step 0**, ahead of every fallible command. It is one syscall and it is the
  whole reason the device stays up, so a future unguarded line cannot re-arm the watchdog the way the
  sed did. `|| true` on the profile sed and on the `find /etc/rc*.d/` sweep (which also exits
  non-zero when a directory in the glob is absent), and a warning on the `crontab -` install.
- **It says out loud whether the bypass is in place** (step 7), so `setup-device.sh` output and the
  boot log both carry the answer. The other half of this bug was that nobody could see it.
- **`roomwizard-app-init.sh`'s call is now `"$DISABLE_SCRIPT" || { warn; touch /var/watchdog_test; }`**
  — the same safety net the `else` branch already had for a missing script, extended to a script that
  is present but fails (truncated scp, CRLF shebang).

Verified on RW09 after `./setup-device.sh 192.168.50.73`: both files md5-match the repo, and
`/var/watchdog_test` carries a timestamp from **this boot** (uptime 1 min), i.e. the every-boot path
recreated it. `bash -n` and **`dash -n`** on all three edited scripts — the two `/bin/sh` ones run
under busybox ash, where a bashism is a runtime error on a device you may have just lost SSH to.

**B15. `clone-to-32gb.sh` could destroy a host disk** — done 2026-08-03, guard exercised in an
isolated harness against this host's real disks. The old `check_device_safe()` blacklisted the literal
string `/dev/sda` and called `mount | grep "^${dev}"`, then `dd if=… of="$DEVICE" bs=4M`. Measured on
the dev host the same day, and the numbers are the argument for the shape of the fix:

| Disk here | What it is | Old guard | New guard |
|---|---|---|---|
| `/dev/sda`, `/dev/sdb` | 0 GB WSL stubs | **blacklisted** | rejected (size) |
| `/dev/sdc` | 8 GB, swap only | **passed the mount check** — `mount` never lists swap — then rejected by the 16 GB minimum, which is luck, not a guard | rejected (mounted: `lsblk` sees `[SWAP]`) |
| `/dev/sdd` | 1024 GB, **carries `/`** | passed the blacklist; caught by the mount check only because `/` is mounted straight from the disk. An LVM/LUKS root shows `/dev/mapper/…` and would have passed both | rejected by name against the resolved root disk |

So the disk the plan named as the likely *target* (`/dev/sdd`, where a `wsl --mount`ed card lands) is
also this host's *root* disk, and the one name the old code protected was a 0 GB stub. Now:
`root_whole_disk()` resolves `findmnt -no SOURCE --target /` through `lsblk -rnso NAME`, so partitions
and device-mapper stacks all reduce to the whole disk and the target is refused if it matches; the
mount check walks the holder tree with `lsblk -rno NAME,MOUNTPOINT` instead of grepping `mount`, which
is what catches swap, LVM and LUKS; a partition target is refused outright (the script writes a
partition table); and `MAX_TARGET_SIZE_GB` (default 128, env-overridable) closes the open top end —
a minimum alone rejects the original 4 GB card and waves through a 4 TB drive.

**One deliberate deviation from the fix this entry prescribed.** `removable == 1` is a **flag-gated
default, not a hard requirement**: every disk on this host reads `removable=0`, the root disk included,
so a hard gate would reject every legitimate target and the first thing anyone would do is comment it
out. It refuses by default and prints what to look at; `--allow-fixed-disk` opens it, and **cannot**
bypass the root-disk, mount, whole-disk or size checks. A safety check that has to be disabled to get
work done is not a safety check.

Not verified end-to-end: nothing was cloned, because doing that needs a spare card and the only disks
here are the host's own. The guard was extracted into a harness and run against `/dev/sdd`, `/dev/sdc`,
a nonexistent path and `/dev/sdc` with the override — all four behaved as the table says.

**B25. A `vnc_client` no `stop` path could kill — done 2026-08-03, reproduced and verified on RW09
the same day. The recorded cause was wrong, and the fix is not where the entry said it was.**

What was recorded (2026-08-02): `vnc_client` "sets a process title the deploy scripts cannot kill",
so `killall vnc_client` matches nothing. Measured on RW09 2026-08-03, **every part of that mechanism
is false**:

| Claim | Measured |
|---|---|
| `vnc_client` sets its own title | It does not — no `prctl`, no `argv[0]` write anywhere in the component. **`app_launcher` sets it**, `execl(app->exec_path, app->name, …)`, passing the manifest's *display* name |
| `cmdline` being `VNC Client` defeats `killall` | It does not. The kernel takes `comm` from the **file** being executed, not from `argv[0]`, so `comm` was always `vnc_client` — and busybox matches `comm` **or** `basename(argv[0])`. `pidof vnc_client` → the PID; `killall vnc_client` → rc=0, process dead |

The real mechanism was in `do_stop()` all along: after the pidfile and `respawn.sh`, it killed
`basename(default-app)` — which is `app_launcher` — **and nothing else**. The process holding
`/dev/fb0` is normally the app the launcher *started*: a grandchild whose basename appears in no
config file, so no name-based rule could ever have found it. Reproduced against the pre-fix script:
`stop` reported success (**rc=0**), `app_launcher` died, `vnc_client` survived, panel left at
`geometry 800 480 800 480 16`. That is the reported failure exactly, and the title was a red herring
that cost a session.

Fixed by making the executable the identity. `roomwizard-app-init.sh` gained `app_pids()`, which
walks `/proc/*/exe` and matches the three directories components deploy into (`/opt/games`,
`/opt/roomwizard`, `/opt/vnc_client`); `do_stop()` TERMs that set, waits up to 3 s, `KILL`s the
remainder and **returns non-zero if anything is still alive** — the bare `exit 0` at the bottom of the
script was itself part of the silence. Verified: TERM alone cleared two `vnc_client`s (one of them the
survivor of the old stop) plus the launcher, rc=0, and `start` brought the launcher back at 32bpp.

Two things came off the same measurement:

- **The title is a diagnosis problem, not a kill problem**, and it is real: busybox `ps w` lists only
  processes with a TTY — 3 lines against `ps`'s 51 on RW09 — so a launcher-started app is invisible to
  it, and `cmdline` was then the one thing left to walk. `app_launcher` now passes `exec_path` as
  `argv[0]`, so `cmdline` names the binary, and `do_status` prints exe **and** cmdline per app process.
  Nothing read `argv[0]` except the usage text of three CLI tools.
- **Don't reason about busybox from memory.** `killall` was blamed for two sessions on the strength of
  a plausible story about `cmdline`. One `pidof` on the device would have settled it.

**B20. Three component scripts hand-rolled the init script's `stop` logic** — done 2026-08-03,
together with B25, which is the failure it predicted. `native_apps`, `vnc_client` and
`scummvm-roomwizard` each carried their own `killall -9` list (`respawn.sh` + `app_launcher` + their
own binary, so three different lists), which could only ever kill basenames whoever wrote the copy
thought of. All three now call `ssh "$DEVICE" '/etc/init.d/roomwizard-app stop'` behind an `[ -x ]`
guard, with `|| warn` so a failed stop is reported rather than aborting a `set -e` deploy mid-way, and
a comment saying not to re-add a `killall`. The trailing `start` calls were already there and are
correct after a real stop. Exercised for real on RW09 by a `native_apps` deploy and a `vnc_client`
deploy; the ScummVM copy is textually identical but was not run (multi-minute build). **Note that
changing `do_stop()` means re-running `setup-device.sh <ip>` — that is the only thing that pushes
`/etc/init.d/roomwizard-app`, so until it runs, a device keeps the old stop behaviour.**

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

**B2. Gamepad buttons latched on and were never released** — done 2026-08-03, **confirmed on the panel
the same day with a real gamepad attached: the buttons no longer stick**, across platformer, pong and
frogger.
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
appears to do nothing until a restart. Rules in `vnc_client/CLAUDE.md`. ScummVM's half was **B3g** —
fixed 2026-08-03, by writing the key's default into `scummvm.ini` rather than by adding a UI.

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
deploy-script half shipped later as **B20** (2026-08-03).

**B8. Non-atomic config/highscore saves** — done 2026-08-02.
`file_write_atomic_open()` / `_commit()` / `_abort()`: temp file, `fsync`, `rename`, then **`fsync` the
parent directory** so the rename survives a power cut. It lives in **`common/config.c`** rather than a
new `common/atomic_file.c` on purpose — a new object would have to be added to every link line in
`native_apps/build-and-deploy.sh`, `vnc_client/Makefile` and
`scummvm-roomwizard/backend-files/configure.patch`, for ten lines of code.

**B13a. platformer: game-over buttons could never fire, and no way out** — done 2026-08-02.
`touch_poll()` was called a **second** time in the same frame and clears `TouchState.pressed` at
entry, eating the press edge `handle_input()` had already captured. Platformer was also the only game
with no `BTN_ID_BACK` handler. Both fixed, and **both panel-confirmed 2026-08-03** with a gamepad
attached — the game-over buttons answer touch, so the double-`touch_poll()` really was what ate the
press edge (see the B22 panel table).

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
discoverability was **B3g** on the ScummVM side (**B3f** was the VNC side — fixed 2026-08-03, it is a
row on the settings screen now) and whose config-file location was **B3h**. Both ScummVM rows are
fixed 2026-08-03: the key's default is written into a `scummvm.ini` that now lives at one absolute
path. Rules in `vnc_client/CLAUDE.md` and `scummvm-roomwizard/CLAUDE.md`.

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

*Verification completed 2026-08-03 — all seven games pass; see the panel table at the end of this
entry. Nothing open remains.*

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

**Panel status — six games 2026-08-02, platformer 2026-08-03 (all pass):**

| Game | Status |
|---|---|
| tetris, snake, frogger | pass — game over → high-score screen advances on its own, no tap |
| samegame | pass — **re-fix confirmed on the panel**: advances on its own *and* the settled overlay stays responsive; the full sequence (lose → overlay+table → `RESET SCORES` empties it immediately → name-entry keyboard with no tap → entry appears → `RESET SCORES` again on a second game over → `RESTART` → `EXIT`) all behaves |
| brick_breaker | pass — `MENU` → `RETIRE` shows the overlay immediately **with the table still populated**, so the `armed` guard holds against the 21 px `RETIRE`/`RESET SCORES` overlap |
| pong | pass — parked the paddle and let the AI run out `WINNING_SCORE` 11 |
| platformer | pass — **confirmed 2026-08-03** with a gamepad attached (it is controller-only since B13k, which is why this row lagged the other six by a day): died, the game-over screen painted on its own with no tap, and `RESTART`/`EXIT` both answered touch |

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

1. **Phase 0** — done. D1–D5 on 2026-07-30; **D6's password rotation done 2026-08-03**, on the VNC
   server and both clients rather than by rewriting git history.
2. **The crash/wedge class.** B3, B4, B5, B6 are done — the device can now always recover to a
   usable launcher on its own, and the log that diagnoses a SIGILL finally reports it.
   **B1 + B24 are deployed and verified on RW09 (2026-08-03)**, all three components rebuilt, with the
   original 16bpp-after-a-VNC-session failure reproduced and shown fixed. **B2 (latched `.held`) is
   done the same day** and now has a host-side regression (`tests/gamepad_latch_test.c`), which also
   settled that this bug *was* testable without a device — the touch coordinate is a plain argument and
   evdev is just `read(2)`. **B2 is now panel-confirmed too** (2026-08-03, gamepad attached — buttons no
   longer stick). **B13g followed it immediately, as a deletion** rather than the reorder its
   one-line row implied, so `gamepad_set_touch_regions()` now has zero callers.
   **B22 is fully closed 2026-08-03** — platformer was the last row, and with a gamepad attached its
   game-over screen painted with no tap and `RESTART`/`EXIT` answered touch. tetris, snake and samegame
   were driven into their B13/B14 states the same day and all pass. ← **the only panel work left is
   brick_breaker levels 5+, postponed at the reporter's request; make the level reachable rather than
   asking for it again**
   **B7 and B9 are done** (2026-08-02) — they were the available quick wins. Doing them turned up
   three new items, all confirmed on the panel the same day: **B23** (backlight slider previewed to a
   sysfs node that does not exist — **done 2026-08-03**), **B24** (no game asserted bpp, which made B1
   reachable from a bare SSH launch — done with it) and **B25** (a `vnc_client` no `stop` path could
   kill, which is B20's predicted failure actually happening). **B25 and B20 are both done 2026-08-03**,
   reproduced and verified on RW09 — and B25's recorded cause turned out to be wrong in every part.
   It was never the process title: `killall vnc_client` matches fine, because `comm` comes from the
   executable and not from `argv[0]`. `do_stop()` was killing `basename(default-app)`, i.e.
   `app_launcher`, and never the app the launcher had started. See the Closed entry before quoting
   anything about this bug.
3. **The per-app layout pass** — done 2026-08-02 (B3e, B3i, B3j, B3k, B13d, B13k). It also
   established that **`touch_inject` cannot work on this device at all** (no `CONFIG_INPUT_UINPUT`),
   which is why C6 had to be rewritten around framebuffer capture instead.
4. **B15** — stop the scripts from being able to hurt you. **Done 2026-08-03**, and the measurement
   reframed it: on this host the disk the entry named as the likely target (`/dev/sdd`) is also the
   root disk, and the one name the old code blacklisted (`/dev/sda`) is a 0 GB WSL stub. **B17 and
   B18 are also done** (2026-08-03), both reproduced against the pre-fix code first — B17's harness
   turned up two unrecorded defects in the same sed, and B18's reproduction needed the repo's
   pre-fix file because RW09's deployed copy was older than the bug. **B19 and B19a are done too
   (2026-08-03), which closes Phase 2 entirely.** B19a was a deletion — `clean.sh` had no callers and
   `build-and-deploy.sh clean` already did more. B19's five defects split three ways worth
   remembering: two were reproduced and fixed with a negative control (the md5 check catches a
   corrupted deployed binary; the drift report shows RW09 clean and `.53` drifted), two were
   path-resolution bugs whose *failure modes differed per script*, and one — `audio_touch_test`'s
   missing `chmod +x` — is **unreachable from this Windows host at all**, because `/mnt/c` discards
   `chmod` and reports everything executable. **B20 is done** (2026-08-03, with B25).
5. **F1 (ALSA)** — biggest user-visible improvement in the project.
6. **Deep clean the device** (`--deep-clean`), then **F2 (DSS overlays)**.
7. **Open the unit and inspect the hardware** — done 2026-07-30. Full teardown, folded into
   [`SYSTEM_ANALYSIS.md`](SYSTEM_ANALYSIS.md). Serial console declined — see [Closed](#closed).
8. Everything else as appetite allows: B12b/B12c and all of C1–C8. **These are genuinely
   unranked**, not deprioritised. **The six open B13 rows and B14 are all done 2026-08-03** — see
   [Closed](#closed); two of them (B13b, B13i) had real gameplay impact and **B13i's prescribed fix
   was wrong**, which is the third time this plan's suggested fix turned out to be a hypothesis.
   **B10 is done 2026-08-03** — built, deployed and verified on RW09; the panel's own build stamp is
   what proves the running binary is the new one. **B3h and B3g are done the same day**, in one
   ScummVM build, verified by running the same binary from two working directories and showing the
   stray inis were not written. C1's `MAX_INPUT_DEVICES` stopgap is **spent**
   (resynced to 32), so what is left there is the shared evdev scanner itself, not a quick win.
