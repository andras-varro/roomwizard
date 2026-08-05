# Improvement Plan

**Open work only.** Finished items are not listed here — the code and `git log` are the record.

**How to read this**

- **B**n = bug, **F**n = feature, **D**n = doc/infra, **C**n = cleanup. IDs are never reused or
  renumbered, because they are cited from commit messages and from the component docs. To find a
  closed one: `git log --grep=B13i`.
- **Status is one word after the heading:**

  | Status | Means |
  |---|---|
  | `open` | Nothing has shipped. Found by reading the code; not reproduced on the device. |
  | `open, confirmed <date>` | Reproduced — on the panel or by running the command. Still unfixed. |

- **A recorded cause and a prescribed fix are both hypotheses.** `open, confirmed` means the
  *symptom* reproduced and nothing more. Reproduce it, then find the cause yourself; and when an
  entry says "compare with X", read X. See `CLAUDE.md` → *Working style*.
- Nothing here requires a kernel rebuild. Items that would are in [Out of Scope](#out-of-scope).

**Before starting anything, read [`SYSTEM_ANALYSIS.md`](SYSTEM_ANALYSIS.md) §1 — *Read this first*.**
Device facts live there; this file holds only what we intend to *do* about them.

---

## Needs a human at the panel

There is no `/dev/uinput`, so nothing past an app's first screen is script-verifiable
(`CLAUDE.md` → *Non-obvious constraints*). Panel time is the project's scarce resource, so these are
grouped to be handed over as **one checklist** rather than asked for one at a time.

| # | Check | Cost | Blocked on |
|---|---|---|---|
| 1 | **brick_breaker SPEED UP → SLOW DOWN is monotonic** — the multiplier is derived now, so +2 → +1 must slow the ball down | ~1 min at level 1 | nothing |
| 2 | **brick_breaker levels 5+ grey striped bricks** are visible and bounce the ball | a full play session | **make the level reachable first** — [C10](#c10-make-a-deep-game-state-reachable-without-playing-to-it--open) |
| 3 | **Second-unit touch dead-band sweep** | one sweep, four edges | [B3c](#b3c-the-touch-dead-band-is-measured-on-one-unit-only--open) |
| 4 | **ScummVM OPL/AdLib tempo** | one intro | an AdLib target must be installed — [B12c](#b12c-scummvm-opladlib-tempo-is-unverified--open) |

Rules for asking: price the check before requesting it, split an item when only part of it is gated,
and record the answer with the confidence it was given — "I think it works" is a hedge, not a pass.

---

## Correctness and verification

### B3c. The touch dead band is measured on one unit only — open

The model and the fix have shipped; [`SYSTEM_ANALYSIS.md#33-touch`](SYSTEM_ANALYSIS.md#33-touch)
carries the measurement, the method and the reference capture. **Read that section before touching
the touch model** — this question has been answered wrongly in *both* directions, and each wrong
answer came from inferring a hardware limit *through* the calibration under suspicion.

**What is left: sweep a second panel** with `/opt/games/touch_raw` — `SWEEP` then `INSET`, all four
edges. Every number on record is one unit's. What a second unit settles is whether the ~30 px Y band
generalises; if it varies per panel the runtime measurement already handles it and **no code
changes**.

Two practical notes:

- **Save `/tmp/touch_raw.tsv` into the repo before the device reboots.** The calibration wizard
  writes no tsv — only the diagnostic does.
- A second unit is available and reachable over SSH, so this is panel time, not hardware
  acquisition. Check `./setup-device.sh <ip> --status` first: a unit on older deploy scripts will
  mislead you about anything else you observe there.

### B12c. ScummVM OPL/AdLib tempo is unverified — open

The mono mixer and the `SOUND_PCM_READ_RATE` read-back were supposed to fix half-speed OPL. Nobody
has confirmed it on hardware. Play an AdLib-driven intro on the device and compare against a
reference recording.

**Not yet satisfiable: no installed target drives OPL.** The one installed game is King's Quest 1
(CoCo3, `agi`) — the CoCo3 platform's AGI sound is not the AdLib path, and that target's `guioptions`
lists no AdLib at all. So this needs an OPL-capable game added first. Adding one works and persists:
`Add Game...` is a touch file browser and the entry lands in `/opt/games/scummvm.ini`.

### D7. mDNS does not resolve from WSL, which is where the deploy scripts run — open, confirmed 2026-08-03

`set-hostname.sh` and the avahi link have shipped, so a named unit answers to `<name>.local` from
Windows. Two pieces of residue:

1. **WSL cannot resolve `.local`.** Its `/etc/nsswitch.conf` is `hosts: files dns` — no mDNS module —
   so `./setup-device.sh rw09.local` passes validation, reaches the SSH step and then fails to
   resolve. The fix is host-side and one package: `sudo apt install libnss-mdns` in WSL. **Until
   then the mDNS payoff applies to Windows-side `ssh` only, not to the build/deploy path.**
2. **The reboot path is unproven.** `S30avahi-daemon` is in place but the link was written directly
   rather than by a full `setup-device.sh` run, so "it comes up on its own after a reboot" has not
   been observed.

Scope note for anyone extending this: the defect is confirmed **in the vendor image**, which a newly
commissioned unit inherits. Units already in service were found carrying a hardcoded *self-IP* line
instead — a different defect (a DHCP address goes stale as soon as the lease moves), equally worth
rewriting, but do not assume which variant a given unit has. Read it. A third variant is on record: a
non-loopback line mapping an RFC-1918 address to the name `null`, on a card whose `/etc/hostname` is
also `null`. `set-hostname.sh` handles all three, because it keys on the name it reads rather than a
hardcoded one.

### D7b. `/etc/hosts` may be regenerated on boot, undoing the offline rewrite — open, **unverified**

One card's `/etc/hosts` carries the header `# Generated by PV networkmanager`. If the vendor's
`networkmngr` rewrites that file at boot, then `set-hostname.sh`'s offline half is undone on the first
boot after commissioning and the whole D7 fix silently reverts.

**This has not been tested in either direction.** RW09 has been in service with its name set and no
regression has been reported, which is weak evidence at best — nobody has checked `/etc/hosts` on a
booted unit *after* a commissioning that set the name. Do that before concluding anything:

```bash
ssh root@<ip> 'cat /etc/hosts; echo ---; grep -rn "etc/hosts" /etc/init.d/networkmngr /opt/sbin 2>/dev/null'
```

If it does regenerate, the fix belongs in the boot path (the init script already re-runs
`disable-steelcase.sh` every boot, so there is a place for it), not in more offline editing.

---

## Features

All userspace. No kernel work.

### F1. Port audio from OSS to ALSA — open, **highest user-visible payoff**

**ALSA already works on this kernel.** The "bru-bru-KLICK" stall, the 506 ms period problem and the
ioctl-ordering fragility all live in the `snd-pcm-oss` **emulation shim**, not the hardware — see
[`SYSTEM_ANALYSIS.md#34-audio`](SYSTEM_ANALYSIS.md#34-audio) for the card, the mixer path and the
four OSS bugs in detail.

Rewriting `native_apps/common/audio.c` and `scummvm-roomwizard/backend-files/oss-mixer.cpp` against
ALSA (or tinyalsa) fixes the project's longest-standing audio complaints with **zero kernel work and
zero brick risk**.

Fix these three in passing, so the ALSA version does not inherit them:

- `audio.c:84` uses `SNDCTL_DSP_STEREO`, which the file's own comment says is ignored; it never
  verifies the channel count, yet every buffer is sized assuming interleaved stereo.
- `audio.c:378` abandons a chunk mid-frame on a short write, desynchronising L/R permanently.
- `oss-mixer.cpp:298` the emergency anti-underrun `write()` ignores errors and partial writes.

**Decision — commit to mono end-to-end.** The hardware is permanently mono (one speaker, no jack, no
jack footprint, no mic — `SYSTEM_ANALYSIS.md#34-audio`), so the two stereo bugs above are fixed by
*removing* the interleaved-stereo bookkeeping rather than by making it correct. This also closes the
microphone-as-input idea.

### F2. Use the DSS overlay planes — open, **biggest performance win available**

Three hardware overlay planes with a scaler, z-order, global alpha and colour-key, sitting unused. On
a GPU-less 600 MHz part this is the only graphics acceleration that exists, and it is pure sysfs — no
kernel work. Inventory, the live sysfs dump and the legacy-omapdss caveat:
[`SYSTEM_ANALYSIS.md#32-display`](SYSTEM_ANALYSIS.md#32-display).

Suggested order:

1. **Prove the scaler.** Render at 400×240 into `fb1`, set `overlay0` `input_size=400,240`
   `output_size=800,480`. A quarter of the pixel fill cost for the same visual size. Start with one
   game, then ScummVM and the VNC client.
2. **HUD plane.** Enable `overlay1` (`vid1`) above the game plane with `zorder` + `global_alpha` for
   score bars, pause menus and modal dialogs — composited free, no redraw underneath.
3. **Colour-key transparency** via `trans_key_enabled` for zero-CPU sprite masking.
4. **Video playback**, speculatively — `/dev/video0` accepts YUV with hardware colour-space
   conversion. Furthest from proven of the four, and the boot-time `omap_vout: failed to allocate DMA
   Channel for video-1` may be exactly what blocks it.

⚠️ Cheap today, but it would need rewriting as DRM atomic plane code if the kernel ever changed —
which, per current policy, it won't.

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

The most *interesting* capability on the board: two-player games across a corridor, high-score sync,
presence beacons — with no network involved.

**The hardware side is settled; this is a pure software task.** The board carries a populated but
empty XBee socket (`J5`/`J6`), the chassis was tooled for that exact module, and the socket's
orientation and 3.3 V rail are measured — so powering a module is safe. Socket, pinout and
measurements:
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

1. **Patch the DTB, module still out, and measure `J5` pin 3** (`DIN`, the SoC's TX). ~3.3 V means the
   pinmux entry took effect; floating or low means it didn't. This is the cheapest possible proof of
   the only genuinely unproven part, and it costs nothing if the patch is wrong.
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

The panel controller is 2-point multi-touch with on-chip gestures, and `panjit_ts` flattens it to
single-touch. Bypass the driver via `/dev/i2c-2` — userspace only, so the kernel policy does not touch
this. Enables pinch-zoom in ScummVM, two-players-on-one-screen, launcher gestures.

**Materially easier than it looks:** the controller is a Cypress PSoC part whose I2C register map is
**published documentation**, so there is no unknown protocol to reverse-engineer from bus captures.
Part number, node, reg address, IRQ and reset GPIOs:
[`SYSTEM_ANALYSIS.md#33-touch`](SYSTEM_ANALYSIS.md#33-touch). **Consider promoting this item.**

Cheaper first step: finish `native_apps/hardware_test/pressure_test.c` and determine whether
`ABS_PRESSURE` actually varies. If it does, that is free analogue input (draw thickness, charge-up
shot power, velocity-sensitive keys).

### F7. Use NAND `mtd4` "scratch" for persistent data — open

`mtd4` is 11 MB of blank, unused NAND that **survives an SD card reflash** — a natural home for high
scores and save games, and safe to write. (`mtd0` must never be written; see
[`SYSTEM_ANALYSIS.md#43-nand-is-effectively-unused`](SYSTEM_ANALYSIS.md#43-nand-is-effectively-unused)
for the partition map and why.)

### F8. Smooth LED effects — open

The two LEDs are true PWM and drive to red / amber / green with smooth crossfade, visible from outside
the room
([`SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm`](SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm)).
Ideas: health/timer bar, heartbeat pulse during ScummVM loading, flash on high score. `hardware.c`
already reaches both channels and already has the non-blocking `LedPulse` API, so this is presentation
work only.

### F9. Ship binaries as GitHub releases — open

**Why this may be the highest-leverage item here:** the build is the slowest and most
environment-bound step in the entire flow — WSL, an ARM cross-compiler, ScummVM's ~1m35s–2m20s link
that also deletes `native_apps/common/*.o` twice — and it is **pure overhead whenever the source has
not changed.** Anyone who wants to put apps on a device today must reproduce the whole toolchain
first. The artifacts suit distribution unusually well: everything ships `-static`, so there is no ABI
surface to match against the device's glibc.

Design, so it does not have to be re-derived:

- **The host pulls the tarball; the device is untouched.** The existing `scp` path stays exactly as it
  is. Nothing new runs on the device and there is no CA-certificate problem to solve on a 2022 vendor
  image.
- `deploy-all.sh` and the per-component scripts gain `--from-release <tag>` that skips **only** the
  build step.
- **The release must carry the md5 manifest.** Deploy-time verification compares against a local
  `build/`, which will not exist on a build-free path.

Two caveats to record before anyone tries it:

- `base/version.o` re-embeds the build date on every link, so releases are **not byte-reproducible**.
  The md5 list must be *generated per release*, not asserted against a known-good set.
- A release must publish **binaries only, never configs**. Device config carries things that must not
  be republished — the `/etc/hosts` mapping of D7, and the VNC password; a glob that swept up
  `*.conf` would publish exactly what those two exist to have removed.

---

## Structural and cleanup

### C1. Extract the shared evdev layer — open

Three parallel implementations of device classification, the `/dev/input/event*` scan, the
`/etc/input_config.conf` parser and the hotplug rescan timer:

| Primitive | `common/gamepad.c` | `vnc_client/vnc_input.c` | `roomwizard-events.cpp` |
|---|---|---|---|
| Classifier | `:63` | `:132` | `:174` |
| Scan loop | `:216` | `:235` | `:214` |
| Config parser | `:294` | `:172` | `:429` |
| Rescan timer | `:492` | `:468` | `:1263` |

**They have already drifted, and the cheap fix is spent.** `MAX_INPUT_DEVICES` was 16 in the VNC
client and 32 in the other two, so a keyboard on `event17` worked everywhere except VNC. It has now
been resynced **twice by hand** — which is the argument for this item, not a substitute for it. The
"clear errno before the read loop" hardening still exists only in the ScummVM copy.

The ScummVM copy is defensible (C++, different event model, links only 4 common objects). **The VNC
copy is not** — `vnc_client/Makefile:21-29` already compiles five objects from
`../native_apps/common/`; it could link `gamepad.o` too.

**Fix:** extract classifier + scan + config parser into `common/evdev_scan.c` (~150 lines).

### C2. Split `device_tools.c` (2651 lines) — open

Five previously-separate GUIs behind a tab enum, sharing nothing but the tab bar. Splitting into
`tab_settings.c` / `tab_diag.c` / `tab_tests.c` / `tab_calib.c` behind a small vtable is mechanical
and costs one line each in `build-and-deploy.sh`.

### C4. Make the common library use the logger — open

`common/logger.c` exists and apps use it (`app_launcher` 18 calls, `device_tools` 17), but the library
they all link writes to stdout unconditionally: `touch_input.c` 15 `printf` / 0 `LOG_`; `gamepad.c`
7/0; `framebuffer.c` 5/0. `touch_init()` alone emits ~5 lines, and `app_launcher` calls it after
**every** child exit, so launcher stdout grows the same banner forever. Log rotation bounds the file
now, but the noise is still the cause.

### C5. Fix `text_truncate` and the 8px/6px font-width confusion — open

- `common.c:83` `text_truncate()` takes **no destination size** and does `strcpy(dest, upper)` (up to
  256 bytes) plus `strcat(dest, "...")`. Callers survive on arithmetic luck — `device_tools.c:2141`
  passes a 48-byte buffer for a 128-byte `EVIOCGNAME` string. One geometry change from a stack smash.
  Add a `size_t dest_size` parameter.
- Text width must come from `text_measure_width()`, because `fb_draw_text` advances **6 px/char**
  while several sites compute **8**. Titles render ~17 % left of centre and long strings clip off the
  left edge. `screen_draw_welcome*()` is fixed; **still wrong: `screen_draw_game_over()`** (message and
  score widths) **and `ui_layout.c:326`**.

### C6. Extend the host-buildable test harness — open

⚠️ **`touch_inject` does not work and cannot be made to work on this device** (no `/dev/uinput`;
evdev's `write()` is the output-event path). The rule and the evidence are in `CLAUDE.md` →
*Non-obvious constraints*. **This invalidates the touch half of anything built on injection, so read
it first.**

Three pieces of work:

1. **Delete the two dead harnesses, or make them say why they cannot work.** `tests/touch_inject.c`
   reports success and delivers nothing, which is worse than not existing.
   `tests/test_game_selector_scroll.py` (277 lines) has never worked, for the same reason — delete it
   or rewrite it against framebuffer capture. It also carries a fifth inlined copy of the
   framebuffer-decode logic that `fb565_to_png.py` supersedes.
2. **Write the first-screen smoke harness.** SSH-launch a binary, `cat /dev/fb0`, decode with
   `fb565_to_png.py`, and inspect the screen drawn before any input: `assert not-all-black`,
   `assert alive after 2 s`, across all ~15 binaries. That is a real smoke test and it has caught real
   defects when done by hand. Anything past the first screen needs a tap-by-tap checklist for a human
   instead.
3. **Extend the host-gcc regressions** over the pure-logic functions, where a regression is invisible
   until you are mis-tapping by 30 px. Four exist — `tests/touch_calib_test.c` (the calibration fit
   end-to-end), `tests/gradient_test.c`, `tests/framebuffer_bpp_test.c`, `tests/gamepad_latch_test.c`.
   Build lines are in each file header; all are host gcc, so `build-and-deploy.sh` runs none of them.
   **Still uncovered and worth the same treatment: `scale_coordinates()`, `parse_args()` and the
   `config.c` / `ppm.c` parsers.**

Three rules these established, all load-bearing:

- **Write the failing version first.** Each existing regression was compiled against the pre-fix
  source and confirmed to fail before the fix was trusted. On a codebase with no CI, a test that has
  only ever been seen passing is not evidence that it can fail.
- **Guard bytes turn a heap overflow into an assertion** instead of a mystery. That is the only way to
  see an out-of-bounds framebuffer write at all: on the device it corrupts whatever `malloc` handed out
  next rather than drawing anything wrong.
- **A device limit is not a code limit.** "Input cannot be tested without `/dev/uinput`" was believed
  for months and is false: `gamepad_poll()` takes the touch coordinate as a plain argument and its
  evdev sources are `read(2)` on an fd, so a temp file of `struct input_event` assigned to
  `gm.gamepad_fd` drives the real code path. Before writing a "needs a human" checklist, ask whether
  the thing needs the *kernel* or only needs *events*.

And for anyone reading a raw value off the wire: **screen→raw conversion must read
`/etc/touch_calibration.conf`, not assume `0..4095`** — the fit legitimately extrapolates past the
12-bit range, so assuming the hardware limits lands ~30 px out on Y. Use
`raw = screen*(max-min)/(dim-1) + min`.

### C7. Run shellcheck — open

The shell scripts *are* the deployment system and they run as root over SSH.
`shellcheck *.sh */*.sh` — one command, no config, no repo changes. **`shellcheck` is not installed in
this WSL**; `bash -n`, plus `dash -n` on anything with a `/bin/sh` shebang, is the current substitute.

### C8. Retire `hardware_diag` — it is a second copy of a `device_tools` tab — open, confirmed 2026-08-02

Raised on the panel: *"it is working well, but why do we keep this, this is integrated in device
tools"*. The redundancy is already half-acknowledged —
[`native_apps/README.md:37`](native_apps/README.md) calls it "superseded by `device_tools` (hidden)",
and `build-and-deploy.sh:349` deliberately deletes its `.app` manifest so it never appears in the
launcher. So it ships, is built on every deploy, is unreachable without SSH, and duplicates read-only
info pages that `device_tools` renders from the same sysfs/procfs sources. The cost is already being
paid: a layout batch had to fix `hardware_diag`'s EXIT corner and header band **separately** from the
equivalent code in `device_tools`.

Before deleting, confirm page-by-page that `device_tools` covers all six (System, Memory, Storage,
Hardware, Config, Network) — the diag pages are terse and one may have a field the tabs lack. Then drop
the source, the two build steps (`build-and-deploy.sh:102-103`), the four deploy/marker references and
the README rows. If a page turns out to be unique, move that page into `device_tools` rather than
keeping the binary. `do_led_test()` is also duplicated between the two tools and goes with it.

### C9. Gate the ScummVM binary too — and gate it unstripped — open, measured 2026-08-03

`scummvm-roomwizard/build-and-deploy.sh` never calls `check-arm-safe.sh`, so the largest ARM binary in
the project — the only C++ one, and the one doing the most division — ships through no SIGILL gate at
all. `native_apps` has had a hard-zero gate for months, and the point of it is that a hardware
`sdiv`/`udiv` on this Cortex-A8 is the worst failure mode available: blank screen, no output, no log,
indistinguishable from "the app didn't start".

**It cannot just be bolted on where the script strips, because the gate is unreliable on a stripped
binary and ScummVM is the case that demonstrates it.** The A/B on one file: the gate reports **8–9
hardware divide instructions** stripped and **zero** unstripped — the same binary, and `strip` cannot
alter `.text`. Without symbols, objdump cannot separate code from the literal pools embedded in
`.text`, so four-byte constants decode as plausible instructions. ⚠️ **The phantom operands are not
reliably invalid** — `udiv pc, fp, sl` is dismissible, but `udiv r7, r1, lr` is a legal encoding that
looks exactly like compiler output. "Eyeball the operands" is **not** triage; the symbol table is the
only thing that answers it.

So: call it from `build_scummvm()` **before** `strip_binary()`, on the unstripped artifact. Do not put
it after the strip step, and do not add an allowlist of offsets — they move on every build, because
`base/version.o` re-embeds the build date on every link. The binary was confirmed clean unstripped, so
adding the gate should be a no-op that stays a no-op.

### C10. Make a deep game state reachable without playing to it — open

`brick_breaker`'s indestructible bricks only exist from **level 5 up**, so verifying them costs a full
play session of somebody's time — which is why that check keeps being postponed, reasonably. A
`--level N` argument or a debug entry in the pause dialog turns it into one launch, and would serve any
future level-dependent bug. Generalise to the other games where a state is expensive to reach.

---

## Out of Scope

Recorded so the decision is not re-litigated. Most of these need a kernel rebuild, and the vendor
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
| Ambient-light sensor / auto-backlight | **No such hardware.** The teardown found no sensor and, decisively, no aperture, window or light pipe anywhere in the enclosure — a sensor would have nothing to sense even if fitted. ⚠️ Do **not** probe for it: `pv02_app 5` can hang I2C bus 1, which carries the PMIC. *Time-of-day* dimming needs no sensor and is still available. | [`#39-i2c`](SYSTEM_ANALYSIS.md#39-i2c) |
| Serial console | Located and pinned out (`P4`), then declined: the recovery loop is *pull the card, reimage, DHCP, SSH*, and since NAND and U-Boot stay untouched the card **is** the entire failure surface. Serial would add boot visibility, not recovery capability. Revisit only if NAND or U-Boot ever get written. | [`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports) |

**Note:** enabling **UART3** for the ZigBee radio (F5) is *not* in this table — it may be reachable by
patching the appended DTB, which needs no kernel source.

---

## Where to start

Deliberately not a ranking of everything — only the claims worth making.

1. **F1 (ALSA)** is the biggest user-visible improvement available, and it is pure userspace.
2. **F2 (DSS overlays)** is the biggest performance win, also pure sysfs. Deep-clean the device
   (`--deep-clean`) first if disk space is tight.
3. **C10 before panel check #2** — it converts a play session into one launch, and every future
   level-dependent bug pays the same toll until it exists.

Everything else is genuinely unranked rather than deprioritised. **F6 (multi-touch) is the one to
consider promoting**: the register map is published, so it is far less speculative than its position
in this list suggests.
