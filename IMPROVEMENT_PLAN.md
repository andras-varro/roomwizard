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
| 5 | **The mix bus, on the panel — two defects already found by ear and FIXED; the second fix is deployed and UNHEARD.** (a) ✅ two sounds heard as two · (b) `audio_success()` arpeggio-not-chord, still unasked · (c) ✅ answered, and it **refuted** the clipping diagnosis — `LIMIT: HARD` vs `SOFT` was inaudible · (d) ✅ the ~60 ms rule, all three walks — **keepalive removes it, 5 ms audible / 20 ms recognisable** · (e) CPU% while mixing, unmeasured. **First move: listen to the period-aligned lead** — [F1](#f1-port-audio-from-oss-to-alsa--open-phase-state-in-the-table-below) Phase 3 defect 2. Then `brick_breaker` latency under load, and ScummVM music+speech vs the 32 % baseline | one play session | the game and ScummVM halves still need F1 Phases 4–5. Fold item 4 in — the OPL check is the same ear on the same trip |
| 6 | **Do `music1.wav`/`music2.wav` still sound right when mixed under gameplay** — and is 192 KB/s of SD streaming free inside the render loop | part of item 5's session | [F19](#f19-background-music-in-the-platformer--open-asked-for-2026-08-14). ⚠️ **Still blocked, and item 5 does not unblock it**: the mix bus takes synthesised tone voices only, so a PCM-source voice kind is F19's own first step |

Rules for asking: price the check before requesting it, split an item when only part of it is gated,
and record the answer with the confidence it was given — "I think it works" is a hedge, not a pass.

---

## Correctness and verification

### B29. Two findings left from the 2026-08-09 walkthrough — open

1. ⚠️ **`card-prep.sh` still asks the operator to mount the rootfs; `commission-offline.sh` does not, and
   the asymmetry has no reason left.** The operator is holding the card either way, and
   `rw_mount_card`/`rw_check_card_mounts` already exist and are what the offline pass uses. Phase 1
   should find the card disk (`rw_find_card_disks`), mount what it needs, and unmount on every exit path
   — with `$ROOTFS` still honoured as the "I mounted it myself" hatch, and the desktop-automounted case
   detected rather than double-mounted. ⚠️ **It now needs p2 as well as p6** (the sibling change in
   `ce30399` mounts p2 read-only to read `websign/net.mode`), so this is one mount decision covering
   both, not a bolt-on. Whatever mounts must also be reachable from a failure trap, the same rule
   `rw_umount_boot` follows.
2. **The panel keeps displaying the vendor's old IP after phase 1, while SSH answers on the new one** —
   observed 2026-08-09 on the unit commissioned with the regenerator disabled. Consistent with the
   display reading `websign/net.ipaddress`/`net.status` on **p2**, which phase 1 does not touch: the
   vendor UI is showing its own stale config, not the live interface. Benign, and it disappears with the
   clean that deletes `websign/`. **Worth confirming that is the source** before writing it down as
   fact anywhere else — it is currently an inference from where the value could have come from.
3. **~~The `--deep-clean` menu item announces the USB power change~~ — done 2026-08-09.** The p1 500 mA
   write is step 5 of *every* `provision.sh` mode; the gate was right and the labelling was not. The
   menu-2 block now says so under the items, naming (a) — which writes p1 while deleting nothing — and
   `--keep-sweeps` — which still writes it — as the two cases that separate the steps, and
   `provision.sh --help` carries the same paragraph. The consent prompt is unchanged: it already named
   the two writes separately.

### B30. `brick_breaker` hides lives past the ninth — open, latent, cosmetic

Found 2026-08-10 while giving Office Runner a training mode. Three games draw a capped HUD lives row
and each caps it differently:

| Game | Shape | Verdict |
|---|---|---|
| `frogger.c:1269` | `lives_shown = min(lives, 5)`, then lays out from `lives_shown * LIFE_ICON_PITCH` | correct |
| `platformer.c:1541` | positioned from `game_lives * 16`, drew `min(game_lives, 5)` | **was wrong** — fixed 2026-08-10, see below |
| `brick_breaker.c:1224` | `for (i < game.lives && i < 9)`, each heart at a fixed anchor minus `i * 14` | **silently truncates** |

Brick Breaker's arithmetic cannot produce Office Runner's gap — it grows leftward from a fixed
anchor, so the row is always where it belongs — but the extra-life power-up at
`brick_breaker.c:718` does `game.lives++` with no cap, so a tenth life and every one after it is
**invisible**: the HUD reads nine whether you hold nine or fourteen, and losing one appears to change
nothing. Cosmetic, never a crash, and it needs a power-up-heavy run to reach, which is why it has not
been seen.

Fix is the rule the other two now follow: cap first, lay out from the capped number, and say what the
cap hid — Office Runner draws one icon plus `x10` rather than five icons meaning ten. Do the same with
a heart plus `x10`, or raise the cap; either way the number has to appear somewhere once it exceeds
what is drawn.

### B32. USB is enumerated only at driver probe — cause established 2026-08-13, no automatic fix

> ✅ **CLOSED 2026-08-14 — not as fixed, and not as a bug.** "Nothing enumerates unless it was plugged
> in at boot" is a **standing property of this hardware with a working one-tap remedy** — Device Tools →
> USB → RESCAN, which brought a dark pad up on the first tap on `.188` (`/etc/init.d/usb-host recover`
> is the same code path from a shell). It stays in this file, which holds open work only, as a deliberate
> exception: [`SYSTEM_ANALYSIS.md` §3.6](SYSTEM_ANALYSIS.md#36-usb) links to its anchor, and the record of
> *what was refuted* is what stops a fourth session rediscovering the same dead ends. ⚠️ **The heading is
> imprecise and is kept only for that anchor** — retitle it when there is a reason to touch the link.

⚠️ **Do not reopen it from source reading alone.** Three mechanisms read out of the driver were each
applied and refuted **on hardware**: the DTB `mode = <3>` → `<1>` patch (live in the booted tree, pad
still dark), a permanently attached hub — ID-ground alone does not revive a dead port — and debugfs
`softconnect`, which sets `SESSION` only in `a_wait_bcon` while a cold port sits in `a_idle`. A fourth,
`echo host > .../mode`, is a **silent no-op**: `omap2430_ops` has no `.set_mode`. Trace, register
readings and the ones that are *not* diagnostic: [`SYSTEM_ANALYSIS.md#36-usb`](SYSTEM_ANALYSIS.md#36-usb).

⚠️ **Whatever the next candidate is, it must first explain how a port that probed with an EMPTY socket
ever obtains a session** — that is the state every failed measurement started from, and each refuted
mechanism assumed a session it did not have. The one candidate never attempted is the DEVCTL
`SESSION`-bit poke through `devmem_write`; its address, and the clock-gating external-abort hazard that
comes with it, are in `usb_host/README.md` beside the untried in-RAM device-tree experiment.

The `mode`-patch tooling is sound and stays, so a unit that was patched can be reverted — but the patch
itself is **out of every deploy path** (`lib/CLAUDE.md` → the p1 writer).

**One small open measurement, moved here from `SYSTEM_ANALYSIS.md` §3.6 in phase 4 because it needs a test
rather than a hedge in prose.** `usb-host recover` succeeded on attempt **1** on the mode-patched kernel,
where both earlier manual runs on the power-only firmware needed **two consecutive** invocations. **[n=1]**,
a different boot, and `recover`'s own 3-try loop makes a single invocation a weak instrument for counting
rebinds — so this is not evidence that the two firmwares differ. Settling it needs repeated boot-empty →
plug → `recover` cycles on **both** firmwares, one reboot each. Low value: the remedy works either way.

### B3c. The touch dead band is measured on one unit only — open

The model and the fix have shipped; [`SYSTEM_ANALYSIS.md#33-touch`](SYSTEM_ANALYSIS.md#33-touch)
carries the measurement, the method and the reference capture. **Read that section before touching
the touch model** — this question has been answered wrongly in *both* directions, and each wrong
answer came from inferring a hardware limit *through* the calibration under suspicion.

**What is left: sweep a second panel** with `/opt/games/touch_raw` — `SWEEP` then `INSET`, all four
edges. Every number on record is one unit's. What a second unit settles is whether the ~30 px Y band
generalises; if it varies per panel the runtime measurement already handles it and **no code
changes**.

**And on either panel: is the interior slope actually steeper than the outer bands?** One `TARGETS` run
gave per-segment slopes suggesting ~12 %, and `SYSTEM_ANALYSIS.md` §3.3 now records that as **not
established** — residuals against the interior line are ±80 raw with no consistent sign, which over
~100 px baselines accounts for ±8 % of slope by itself, so one run cannot separate 12 % from noise. The
measurement is **`TARGETS` repeated** — three or more independent 11-target × 3-tap runs on the same
panel, comparing per-segment slopes *across* runs rather than within one. A real effect survives the
repeat; noise does not. Nothing in the shipped model depends on the answer (the curve is a straight line
today), so this is only worth doing if a genuinely non-linear panel is ever suspected.

Two practical notes:

- **Save `/tmp/touch_raw.tsv` into the repo before the device reboots.** The calibration wizard
  writes no tsv — only the diagnostic does.
- A second unit is available and reachable over SSH, so this is panel time, not hardware
  acquisition. Check `./commissioning/provision.sh <ip> --status` first: a unit on older deploy scripts will
  mislead you about anything else you observe there.

### B12c. ScummVM OPL/AdLib tempo is unverified — open

The mono mixer and the `SOUND_PCM_READ_RATE` read-back were supposed to fix half-speed OPL. Nobody
has confirmed it on hardware. Play an AdLib-driven intro on the device and compare against a
reference recording.

**Not yet satisfiable: no installed target drives OPL.** The one installed game is King's Quest 1
(CoCo3, `agi`) — the CoCo3 platform's AGI sound is not the AdLib path, and that target's `guioptions`
lists no AdLib at all. So this needs an OPL-capable game added first. Adding one works and persists:
`Add Game...` is a touch file browser and the entry lands in `/opt/games/scummvm.ini`.

⚠️ **Ordering, settled rather than left to be discovered: this verifies the very OSS workaround that
[F1](#f1-port-audio-from-oss-to-alsa--open-phase-state-in-the-table-below) Phase 5
deletes.** What it is really checking is that `_outputRate` matches the device — and on the ALSA path
that stops being a read-back-the-truth problem, because `hw:0,0` grants **22050 exactly** (measured,
`SYSTEM_ANALYSIS.md#34-audio`). So do **not** spend a panel trip verifying OPL on the OSS path: add
the OPL-capable target now, and make "OPL plays at correct tempo" an acceptance criterion of Phase 5.
Verifying the doomed implementation is the one way to spend this check and learn nothing.

### B27. `sfdisk` absence is reported as a test failure, not a skip — open, latent

`tests/rw_identify_test.sh:363-369` guards the real-card-image cases on **file presence** but not on
the **tool**, so on a host without `sfdisk` both report `expected yes, got no` — a red failure for
something the harness could not measure. The synthetic block at `:169` gets this right and skips. This
is the "which part of the count is the harness" trap from `CLAUDE.md` → *Working style*.

⚠️ **Latent, not reproducible from this host, and the earlier claim that it fires here was a
mismeasurement.** `sfdisk` is present in this WSL at `/usr/sbin/sfdisk` and on the non-root `PATH`;
both card images are also present, so the two cases run and pass — measured 2026-08-06, 37 passed, 0
failed, 0 skipped. The absence was observed in **Git Bash**, which has neither the toolchain nor
`sfdisk` and is not where these tests run. The defect is real by inspection of the guard; the
reproduction is not. To see it fire, run the suite with `PATH` stripped of `/usr/sbin`.

Fix: skip with the reason, and account for it in `MIN_CASES` so a skip cannot silently shrink coverage.

### D7. mDNS does not resolve from WSL, which is where the deploy scripts run — open, confirmed 2026-08-15

`commissioning/set-hostname.sh` and the avahi link have shipped, so a named unit answers to `<name>.local` from
Windows. Two pieces of residue:

1. **WSL cannot resolve `.local`.** Its `/etc/nsswitch.conf` is `hosts: files dns` — no mDNS module —
   so `./commissioning/provision.sh rw09.local` passes validation, reaches the SSH step and then fails to
   resolve. The fix is host-side and one package: `sudo apt install libnss-mdns` in WSL. **Until
   then the mDNS payoff applies to Windows-side `ssh` only, not to the build/deploy path.**
2. **The reboot path is unproven.** `S30avahi-daemon` is in place but the link was written directly
   rather than by a full `commissioning/provision.sh` run, so "it comes up on its own after a reboot" has not
   been observed.

### D9. `/var/watchdog_test` is absent on a running unit — open, **benign today, confirmed 2026-08-05**

`disable-steelcase.sh` touches it as its *first* command and runs on every boot from
`device-files/roomwizard-app`, yet the unit in service (4 days uptime) does not have the file. It is benign
**only** because the same script also installs a crontab with no `watchdog.sh` job, so the vendor
software watchdog is never scheduled — the bypass file is the second line of defence, not the first.
Two candidate causes, neither measured: the boot-time run is not happening (that unit's deployed
`disable-steelcase.sh` is dated Mar 16, so `--status` would report drift), or `cleanupfiles.sh` (cron,
every 4 h) sweeps it.

⚠️ **[F10](#f10-single-pass-offline-commissioning--done-2026-08-05-confirmed-on-a-unit-2026-08-06) now
`touch`es it offline anyway**, and says out loud that it is belt and braces only. That is defensible
because the same pass truncates the vendor crontab — which is what would have scheduled `watchdog.sh` in
the first place — but it does mean the offline tool places a file that something on the device may
delete. Settling which of the two causes it is would let that line be either removed or relied on.

---

## Features

All userspace. No kernel work.

### F1. Port audio from OSS to ALSA — open, **phase state in the table below**

⚠️ **The heading deliberately carries no phase number.** It used to, and each phase that closed
retitled it and dangled every inbound link — seven of them at Phase 2's close, all named by
markdownlint's `MD051`. The state lives here instead, where updating it costs nothing:

| Phase | What it is | State |
|---|---|---|
| 0 | measure `hw:0,0` before rewriting anything | ✅ **closed, passed** — `.188`, 2026-08-14 |
| 1 | cross-build tinyalsa into `arm-deps/` | ✅ **closed, passed** — host-only, 2026-08-15 |
| 2 | split the pure generator out and host-test it | ✅ **closed, passed** — host-only, 2026-08-15 |
| 3 | the mix bus, as an optional per-frame pump | ⚠️ **BUILT, host-green at 154 checks, DEPLOYED to `.188`. Two defects found BY PANEL and fixed; the fix for the second is deployed but NOT yet heard** (2026-08-15) |
| 4 | rebuild `audio.c`'s device half on tinyalsa | open, next |
| 5 | ScummVM's `alsa-mixer.cpp` | open; folds in [B12c](#b12c-scummvm-opladlib-tempo-is-unverified--open) |
| 6 | the docs and the comments this makes stale | open |

**ALSA already works on this kernel, and now it is measured rather than assumed.** The card, the mixer
path, the four OSS bugs, the in-kernel config, the on-device ALSA userspace and the full `hw:0,0`
constraint table are all in [`SYSTEM_ANALYSIS.md#34-audio`](SYSTEM_ANALYSIS.md#34-audio) — read it
before touching this, and do not restate its numbers here.

Rewriting `native_apps/common/audio.c` and `scummvm-roomwizard/backend-files/oss-mixer.cpp` against
tinyalsa fixes the project's longest-standing audio complaints with **zero kernel work and zero brick
risk**. The full phased plan is `~/.claude/plans/plan-f1-alsa-8-14-2026.md` (approved).

**Phases 1 and 2 are closed, host-only, 2026-08-15.** `native_apps/build-deps.sh` pins tinyalsa **2.0.0**
into `native_apps/arm-deps/` (gitignored) and `build-and-deploy.sh` §1b calls it guarded on the artifact;
`native_apps/common/audio_gen.{c,h}` now holds the audio logic with no device in it — frame and byte
arithmetic with **the channel count as an argument**, the `timeval → ms` and flush-wait arithmetic, the
tone envelope, one gliding oscillator with the fade-out as a *mode*, the mono→interleaved duplication, and
`audio_write_frames()`, the only code that decides when to stop writing. The four rules that came out of
the cross-build — compile five of upstream's eight sources, the `pcm_close()` patch, what
`check-arm-safe.sh` can and cannot judge about a static archive, and why no ALSA header needs vendoring —
live in `native_apps/CLAUDE.md` → *Build*.

Three results from those phases worth not re-deriving:

- ⚠️ **A hand-rolled `Audio` went silently mute the moment `channels` existed.** `device_tools.c` and
  `hardware_config.c` were verbatim duplicates that `memset` an `Audio` and set three fields, so `channels`
  stayed **0** — and `audio_bytes_for_frames(8820, 0)` is **0**, against 35280 for two channels. ⚠️ **The
  prescribed fix "convert them to `audio_init()`" was wrong and the code said so**: `hardware_config.c:71`
  reads *"Bypass config-gated `audio_init`"*, and that bypass is deliberate — a hardware *test* must drive
  the speaker even when the user has switched audio off in config. The defect was the duplication, so the
  fix is one `audio_init_unchecked()` that both tabs call. Negative control for the whole class:
  `grep -rn 'open(DSP_DEVICE\|open("/dev/dsp"' --include=*.c native_apps/`, whose only legitimate hit is
  `audio.c` itself (two standalone OSS probes under `tests/` also hit it; neither uses `Audio`, neither is
  in `build-and-deploy.sh`).
- ⚠️ **The 32-bit overflow cannot be *observed* on this host — `sizeof(long)` is 8 here.** The test models
  the target's truncation explicitly (`(int32_t)(uint32_t)` of the 64-bit product); a test that merely
  wrote the shipped expression would pass here and prove nothing about armhf.
- The invariant Phase 4 could break is **`bytes == frames * channels * 2`, with `channels` consumed from a
  device read-back rather than a literal.** `configure_dsp()` reads it back with `SOUND_PCM_READ_CHANNELS`
  and warns **once per `Audio`**, never once per call — it runs on every `audio_flush()`.

**Phase 3 is BUILT, host-green and deployed to `.188` — and not closed, because two of its questions can
only be answered by an ear.** The mix bus is `AudioMixer` + `AudioVoice` in `audio_gen.c` (pure) driven by
`audio_pump()` in `audio.c` (the device half); `tests/audio_gen_test.c` is green at 154 checks and all
seven sabotaged copies of `audio_gen.c` were caught. Its design rules, each of which cost something:

- **A pump, never a thread.** `native_apps` links no pthread, and static ARM + pthread is the
  `clock_gettime64` → SIGSEGV-before-`main()` scar. `audio_pump(Audio*)` renders active voices → sums in
  `int32` → clamps once → writes what fits, called once per frame beside `fb_swap()`.
- ⚠️ **It is OPT-IN by a branch on `audio->pumping`, and the plan's sketch of how was impossible.** The
  plan said `audio_tone()` should enqueue a voice *and* write what fits immediately. A bounded immediate
  write truncates any tone longer than the lead (every tone here is), and an unbounded one hands the whole
  tone to the kernel — which is exactly what makes it unmixable. An app that never calls
  `audio_pump_enable()` therefore takes today's code byte for byte, which is stronger than "degrades
  gracefully".
- ⚠️ **The pump targets a LEAD; it must never write into the free space.** An empty ~506 ms OSS ring will
  accept half a second of audio, and a sound triggered on the next frame then plays half a second late. The
  lead is both the queue depth and the latency ceiling, and `audio_pump_frames()` returns 0 whenever we are
  already that far ahead. ⚠️ Group K pins that **with the cap taken out of the way**, because while
  `AUDIO_PUMP_CAP_MS == AUDIO_PUMP_LEAD_MS` the obvious check passes against a space-filling pump by
  accident — which the sabotage measurement caught.
- ⚠️ **A canned arpeggio needs per-voice start offsets, or it becomes a chord.** Three voices added at once
  sound together, so `AudioVoice` has a `delay` and the four canned sounds are four note tables and one
  sequencer, signatures unchanged at ~45 call sites. Group A6 is the control: the same three notes with no
  offsets clip, with offsets do not.
- ⚠️ **A full bus REFUSES and counts; it never steals a voice.** Stealing the oldest is what a synth does
  and it is wrong here — the longest voice is
  [F19](#f19-background-music-in-the-platformer--open-asked-for-2026-08-14)'s 44 s soundtrack, and a
  missing blip is far cheaper than a chopped track. `audio_pump_dropped()` is the number to watch.
- **The clamp is once, after the whole sum, and it counts.** Group I asserts that **slot order cannot
  change the mix**, which is the check that catches an `int16` accumulator — and that sabotage fails 5
  assertions.
- ⚠️ **`audio_interrupt()` on the pump is "stop all voices" and no longer resets the ring**, because the
  reset is what makes mixing impossible. Consequence: up to one lead of already-written tail still sounds.
  Signature unchanged, ~23 call sites.
- **A pumping app must not idle at `FRAME_DELAY_IDLE_US` mid-sound.** 100 ms of sleep against the lead
  starves the device and you hear a gap — which would read as a mixing defect rather than a pacing one.
  `audio_pump_active()` is the predicate, and it returns true while keepalive is on because that is a
  promise of frames too. ⚠️ **The theremin and the pump cannot both own the ring**, so
  `audio_stream_start()` refuses loudly when the pump is on rather than letting two writers interleave
  frames into one device.

⏳ **Outstanding, and the reason Phase 3 is not closed.** `native_apps/tests/audio_mix_test` is the
interactive tool built for exactly this (33rd artifact, launcher tile *Mix Bus Test*): **four** toggles
(PUMP / KEEP / **LIMIT** / STOP ALL) so the rejected behaviour is the negative control on the same panel,
a drone plus three far-apart pitches, the four canned sounds beside a deliberate CHORD, a live
`voices`/`clip`/`lim`/`starve`/`lost`/`drop` + *worst frame* readout, and a 5/10/20/40/60/100 ms row. Every
tap logs the pad's own name, freq and ms plus both toggle states to `/tmp/mix.log`, and `audio_pump()`
traces the raw ring numbers for 40 calls per session. Panel state so far:

| # | Question | Answer |
|---|---|---|
| 1 | two overlapping sounds heard as **two** | ✅ **yes** — DRONE 220 + 440, reported as "two tones" 2026-08-15 |
| 2 | `audio_success()` is an arpeggio, not a chord | ⏳ **unasked** — CHORD was unrecognisable for a reason since fixed, so SUCCESS is worth re-hearing first |
| 3 | is the summed clamp audible | ✅ **NO, and that refuted the diagnosis** — `LIMIT: HARD` vs `SOFT` was *"no change at all"* by ear while `clip` provably went 0 → 4189. Clipping was real and is fixed, but it was **not** what the panel was hearing |
| 4 | the ~60 ms rule, three walks | ✅ **PUMP off: unchanged · PUMP on, keepalive OFF: still ~60 ms · PUMP on, keepalive ON: 5 ms is audible, 20 ms recognisable.** The rule is a property of **restarting the stream**, not of `SNDCTL_DSP_RESET` — see [`SYSTEM_ANALYSIS.md#34-audio`](SYSTEM_ANALYSIS.md#34-audio) |
| 5 | CPU% while mixing | ⏳ **unmeasured** — `top -b -n 2 \| grep audio_mix_test` from a second shell while a drone runs |
| 6 | does the ~50 % attenuation belong on the **synth** or on all output | ⏳ **unmeasured** — `AUDIO_PEAK` 18000 is an inference from `SPKR1` summing L + R, and 48 kHz stereo music through `aplay` (no attenuation) was *"loud but not distorted"*, which points at synth-only. A per-voice gain walk (one voice at 18000 vs 26000 vs 32767, listening for distortion) settles it, and Phase 4 needs the number before picking a peak for the tinyalsa path |

**Two defects were found by ear and both are fixed in the tree; the second fix has NOT been heard yet.**

⚠️ **Defect 1 — the bus had no headroom, and one voice's peak is an ACOUSTIC limit.** Every voice plays at
`AUDIO_PEAK` (18000, ≈55 % of full scale because `SPKR1` sums L + R), so three voices sum to 54000 against
int16's 32767. Measured on `.188`: `clip` **15402**, read off the panel with `PUMP: ON` *recorded* rather
than recalled. The fix is `audio_mix_limit()` — linear to a knee at `AUDIO_MIX_KNEE` (= `AUDIO_PEAK`
exactly, so **one voice stays byte-identical**), then `y = K + (C−K)·u/(1+u)` asymptotic to
`AUDIO_MIX_CEIL` 26000. Three properties make it checkable rather than plausible: it is bounded, so
⚠️ **`clipped` must reach exactly 0** (it does, measured both host-side and on the panel: `clip=0` while
`lim=6181`); the ceiling is **below** two voices' arithmetic sum, so it protects the speaker and not just
the store; and `AUDIO_MIX_HARD` keeps the rejected clamp reachable as the on-panel control.

⚠️ **Defect 2 — a lead in MILLISECONDS is the wrong unit, and this is the one the panel was hearing.**
Measured on `.188` 2026-08-15: the OSS shim moves `appl_ptr` in whole **2048-frame periods** and never
between, while `GETOSPACE` counts the partial period it is still staging. So an 80 ms lead is **1.7
periods**, of which ALSA can play one; it drains that, the next is not complete, `state` goes **`XRUN`**,
and the shim's recovery **discards** the staged audio. Consequences, all reported and all one mechanism:
a crack every ~120 ms (operator counted "20–25" in a 3 s drone — the same cadence as the measured
`RUNNING → XRUN → RUNNING` cycle), CHORD *shorter* than the un-pumped version, and a tone chopped ~8 ×/s
that reads as a **square wave** — which is why the limiter changed nothing audible. The fix is
`audio_pump_lead_frames()`: take the device's period, floor the lead at `AUDIO_PUMP_LEAD_PERIODS` (3) of
them, round **up** to a whole period, cap at half the ring. On this device that is 6144 frames ≈ **139 ms**,
and ⚠️ **the lead is the latency ceiling**, so that is a real cost of the shim's period size — Phase 4 buys
it back, because Phase 0 measured tinyalsa granting `period_size=1024` (23 ms).

**Two suspects this file recorded are now REFUTED by measurement, and neither should be re-raised:**
`lost=0` on every logged pump kills "`WPOL_PUMP`'s `stop_on_again` drops rendered frames", and the tool's
worst frame time stayed at **107 ms while `starve` kept climbing** — one slow frame at start-up, not a
chronically slow loop — which kills "the render loop cannot feed the lead". The third suspect, the tool's
own toggle wiring, was checked and was **sound**: the panel capture showed `PUMP: ON` and every log line
agrees with `audio_pump_active()`.

⏳ **What the next session must do first: listen.** The period-aligned lead is deployed to `.188` and has
never been heard. Ask for PUMP + KEEP, then DRONE 220 + 440, then CHORD, and poll
`/proc/asound/card0/pcm0p/sub0/status` from a second shell — the `XRUN` cycle disappearing is the
script-verifiable half of the same answer.

**Decisions taken by the operator:** tinyalsa linked **static** · **nothing shipped to the device** (neither
`libasound` nor `/usr/share/alsa`) · **both** `native_apps` and ScummVM · **add real mixing** · one mono
generator feeding a stereo device (below) · **one tinyalsa for the whole repo** — ScummVM points at
`../native_apps/arm-deps` in Phase 5 rather than building a second copy, because zlib is built twice
here and `LICENSE.md` carries both versions as a result.

**Phase 0 was a gate and it passed on `.188` 2026-08-14**, answering the plan's largest risk in the
process. Its numbers are device facts and live in
[`SYSTEM_ANALYSIS.md#34-audio`](SYSTEM_ANALYSIS.md#34-audio) — the small period tinyalsa is granted against
the shim's ~506 ms, every rate accepted exactly so **no userspace resampling anywhere**, and `hw:0,0` being
**stereo-only**. The design consequence is the part that belongs here: **mono source, stereo frames, one
conversion point.** The hardware is still mono, so mono stays right as the *source* model, but the
interleaved-stereo bookkeeping cannot be deleted — only confined to a sample duplication at the write
boundary. ✅ **And that duplication is measured, not assumed**: `SPKR1` sums L + R, so writing the same
sample to both channels is right *and* the loudest option.

⚠️ **The probe needed nothing cross-compiled, and its own first run was still wrong.** The vendor's `aplay`
has `--dump-hw-params`, which took the cross-build off Phase 0's critical path entirely; then a `-c 1` loop
in `native_apps/tests/alsa_probe.sh` printed **seven REFUSED rates** that were all one channel-count
failure. Read its step 3 before believing its step 4.

⏳ **One inherited defect is left, and it belongs to Phase 5:** `oss-mixer.cpp:298`'s emergency
anti-underrun `write()` ignores errors and partial writes. The three in `audio.c` closed in Phase 2 — the
channel count that was set with `SNDCTL_DSP_STEREO` and never read back, the three write paths that could
abandon a chunk mid-frame, and the `(long)sample_rate * duration_ms` overflow past ~48.7 s. ⚠️ A
stereo-only interface makes the mid-frame one **worse** than it read before, not moot: there is no mono path
underneath to absorb a half frame. The microphone-as-input idea stays closed — there is no mic and no jack
footprint.

### F19. Background music in the platformer — open, asked for 2026-08-14

The operator hand-copied `music1.wav` and `music2.wav` to `/opt/sound` on `.188` and wants them under
`native_apps/platformer/`. **Both play correctly today** — `aplay -D hw:0,0`, `rc=0`, reported
*"surprisingly loud, but not distorted"*.

**The files are already in the ideal format**: `S16_LE / 48000 Hz / stereo`, which is bit-for-bit what
`hw:0,0` grants (`SYSTEM_ANALYSIS.md#34-audio`) — no resampling, no channel conversion, no `plug`.
8,486,604 B (44.2 s) and 10,698,444 B (55.7 s), 19.2 MB together, streaming at 192 KB/s.

⚠️ **This is blocked on [F1](#f1-port-audio-from-oss-to-alsa--open-phase-state-in-the-table-below)
Phase 3, and it is that phase's headline use case.** Music under a game means *music playing while a
jump or coin effect fires* — i.e. two streams summed. Today `audio.c` is one-sound-at-a-time enforced by
`SNDCTL_DSP_RESET`, so on the current backend the music would be chopped by every sound effect. Do not
build this against the OSS path; F1 Phase 3's mix bus is exactly what it needs, and this request is the
concrete answer to "is real mixing worth it".

⚠️ **Phase 3's bus now exists, and it does NOT yet unblock this — `AudioVoice` renders a synthesised
tone and nothing else.** So F19's own first step is a second voice kind that pulls PCM from a source,
which is a smaller job than the bus was but is not free, and it lands two decisions on it that the tone
voice never had to answer: where the file read happens (a `read()` inside `audio_pump()` puts SD latency
in the render loop, which is the thing this project spent seven techniques avoiding), and what
`AUDIO_MAX_VOICES`-full means when the voice that would be dropped is the soundtrack rather than a blip.
The refusal-not-stealing rule was written with this case in mind; check it still reads correctly from
F19's side before building on it.

Decisions to take before it ships:

- ✅ **Licence: cleared, and the provenance is recorded here so nobody has to reconstruct it.** Both
  tracks are **AI-generated by the operator at `https://musely.ai/tools/platformer-level-music`**
  (2026-08-14, two tracks, no others). That page's FAQ — *"Can I use the generated platformer music in
  commercial projects?"* — answers *"Yes, absolutely! All music generated using Musely's AI Platformer
  Level Music Generator is royalty-free and cleared for both personal and commercial use"*, and says
  users may add such soundtracks to games or streams *"without worrying about licensing fees or
  copyright claims"*. Verified against the live page 2026-08-15; **no attribution requirement and no
  usage restriction appears on it.** (The fetch was checked for proxy interception — the page returned
  its real title *"Free AI Platformer Level Music Generator | Musely"*, its generator form fields, its
  `Advanced Settings`/`PRO` badge and all seven section headings, so it was not a Zscaler/captcha wall.
  ⚠️ Both reads went through the same summariser and the second likely hit the 15-minute URL cache, so
  they are one retrieval re-read, not two independent ones.)
- ⚠️ **The page also advertises *"Seamlessly Loopable Audio"* as a feature — treat that as a marketing
  claim, not a measurement.** Background music under a platformer must loop, and a 44 s track loops
  every 44 s, so an audible seam would be heard constantly and is the single most likely disappointment
  in this feature. **Check the loop point before building around it**: `aplay` the file twice back to
  back and listen at the join, which costs one SSH command and no code. If it seams, the fix is a
  crossfade in the mix bus — cheap, but only if it is known about before the streaming design is fixed.
  ⚠️ **Two limits on that, stated rather than glossed:** the page asserts royalty-free *clearance* but
  **never says who owns the output**, so what we hold is a permission claim rather than a named licence
  grant; and it is a marketing/FAQ page, **not a terms-of-service or licence agreement** — none is
  linked from it. That is a normal basis for a hobby project and it is the operator's to price, not a
  blocker; it is written down because `LICENSE.md` enumerates every other file precisely and this one
  cannot be reconstructed from the bytes.
- **`LICENSE.md` row is pre-drafted and OUTSTANDING until the files are committed.** Nothing is added
  yet, because `LICENSE.md` must not describe files the repo does not have. When they land, add to
  *Third-party code committed in this repository*:
  `| /opt/sound/music{1,2}.wav | Royalty-free, commercial use permitted | AI-generated at musely.ai/tools/platformer-level-music (2026-08-14). Not our composition; no attribution required per that page's FAQ, which asserts clearance but not ownership. |`
  — and a matching row in *Distributed binaries* if they go into a bundle.
- ⚠️ **Still open, and it is a repo-weight decision rather than a licence one: 19.2 MB of WAV in git
  history is permanent.** Converting to mono first halves it to **9.6 MB** at zero audible cost (below),
  which is the cheapest version of "yes". The alternative — leave them on the device and load at runtime,
  degrading silently when absent — keeps git small but loses them on every re-commission. Operator's call.
- **Store mono, play stereo — halves everything for free.** The speaker sums L + R (measured), so a
  mono `(L+R)/2` file duplicated at playback is *audibly identical* to the stereo original while
  halving both the file and the SD read bandwidth. The duplication code is required anyway by `hw:0,0`
  being stereo-only, so this costs nothing to adopt.
- **Deployment.** Hand-copied files do not survive a re-commission. If the music ships, it belongs in
  `native_apps/build-and-deploy.sh` and the bundle manifest. Note `device-files/clean-rules.conf:189`
  keeps `/opt/sound` wholesale, so the files are safe from the clean; its *reason* text ("113 KB of
  usable UI WAVs") goes stale the moment they are permanent.
- **Streaming, not loading.** 19 MB into 234 MB of RAM is possible and wasteful; `audio.c` already has
  four streaming functions whose only consumer is `tests/audio_touch_test.c`, so they can be redesigned
  freely (F1 Phase 2). Measure the SD read cost inside the render loop before assuming 192 KB/s is free.

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
[`HARDWARE.md#4-unpopulated-and-expansion`](HARDWARE.md#4-unpopulated-and-expansion).
Vendor protocol references, the `ttyS2`→UART3 mapping and the Series 1 vs Series 2 `AT` caveat:
[`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports).

**The one unproven thing is the DTB pinmux edit.** UART3 is `status = "disabled"` with no pinmux
entry. The DTB is appended to `uImage-system` and this project already binary-patches it
(`usb_host/patch_dtb.py`, which recomputes the uImage CRCs correctly) — but adding a whole pinmux
node is materially harder than the existing one-word power-budget patch and **has never been done**.
Recovery if it misboots is a power cycle: `bootcmd` is hardcoded to the untouched `uImage-system`
([`#47-recovery`](SYSTEM_ANALYSIS.md#47-recovery)).

**Staging.** ⚠️ **Rewritten 2026-08-13: the hardware situation changed and the old staging is spent.**
There are now **three modules**, and **one is already seated** in a unit — so the ordering that existed
to keep a single irreplaceable module out of a possibly-mis-muxed socket no longer buys anything, and
step 4's "buy a second module" is done. What the change did *not* do is make the module's health known:
`J5` pin 1 is a live 3.3 V rail regardless of UART3, so a reversed insertion has already had its effect.

1. **Patch the DTB and measure `J5` pin 3** (`DIN`, the SoC's TX). ~3.3 V means the pinmux entry took
   effect; floating or low means it didn't. Still the cheapest proof of the only genuinely unproven
   part, and it costs nothing if the patch is wrong. It can be done with a module in the socket — the
   measurement is of the *board*.
2. **Check the seated module's orientation against the pin-1 dot before powering the unit again**, and
   read its label for Series 1 vs Series 2, so a partial `AT` response is not read as a wiring fault.
3. **`+++` then `ATID` at 57600 8N1 on `ttyS2`.** That validates the DTB patch, the socket wiring *and*
   whether a decade-old module still works — three unknowns, one experiment.
4. **A silent module does not distinguish the three failure modes.** With two spares, swapping is now a
   cheap control: a second module that is also silent points at the DTB or the wiring, one that answers
   points at the first module. Do step 1 first anyway, because it separates board from module without
   consuming anything.

An XBee fed reversed dies instantly. That is why the orientation was measured before insertion, and it
is now the leading explanation to *rule out* rather than a hazard to avoid.

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

### F9. Ship binaries as GitHub releases — **partly built 2026-08-05**, open

**Built and exercised on this host:** `release.sh` at the repo root, `lib/rw-bundle.sh` (the bundle layout,
sourced by every producer and consumer), and `--bundle <dir>` on `native_apps`, `vnc_client` and
`scummvm-roomwizard`. One run of `./release.sh --stage-only --component native_apps --component
vnc_client` staged 49 files and produced a 5.6 MB tarball in 50 s. The `.app` manifests are now data in
`native_apps/app-manifests.sh` and one heredoc-into-a-file in each of the other two, written locally and
copied by both the deploy path and `--bundle` — the nine `cat > … << APP` blocks that used to live
inside an `ssh … <<REMOTE` heredoc are gone, so a manifest can no longer differ between the two paths.
`scummvm-roomwizard` gained the ARM-safety gate it never had, inside `strip_binary` before the in-place
`strip` ([§6.1](SYSTEM_ANALYSIS.md#61-cortex-a8-has-no-hardware-integer-divide)).

**Still open:**

- **`--from-release <tag>` on `deploy-all.sh` and the per-component scripts**, which is what makes the
  release useful to the *existing* SSH flow. Nothing of this is written yet.
- **The publish step has never run, but it is no longer blocked.** `gh` 2.86.0 is installed in WSL from
  the release `.deb` (focal's apt has no `gh`, and the snap links against a glibc newer than 2.31), and
  ✅ **`gh auth status` is green, measured 2026-08-10 in WSL**: logged in to github.com as
  `andras-varro`, active, token scopes `admin:public_key, gist, read:org, repo`. `repo` covers release
  creation and `origin` is `git@github.com-personal:andras-varro/roomwizard.git`, so the authenticated
  account **is** the repo owner. Note `gh` resolves the owner from the remote URL and that URL is an SSH
  host **alias**, so `--repo andras-varro/roomwizard` may still be needed — a flag, not a blocker. The
  publish path is therefore unexercised for want of someone running it, nothing else.
- **`usb_host` is excluded by design**, not by omission: it patches `uImage-system` on p1, and
  [F10](#f10-single-pass-offline-commissioning--done-2026-08-05-confirmed-on-a-unit-2026-08-06) must not touch p1.

Design, so it does not have to be re-derived:

- **The host pulls the tarball; the device is untouched.** The existing `scp` path stays exactly as it
  is. Nothing new runs on the device and there is no CA-certificate problem to solve on a 2022 vendor
  image.
- `deploy-all.sh` and the per-component scripts gain `--from-release <tag>` that skips **only** the
  build step.
- **The release must carry the md5 manifest.** Deploy-time verification compares against a local
  `build/`, which will not exist on a build-free path. `lib/rw-bundle.sh` writes
  `manifest.d/<component>.md5` alongside `.list` for this; they are separate files because the declared
  modes are stable and the checksums are not (next bullet), so a mode diff between two releases stays
  readable.

Two caveats to record before anyone tries it:

- `base/version.o` re-embeds the build date on every link, so releases are **not byte-reproducible**.
  The md5 list must be *generated per release*, not asserted against a known-good set.
- A release must publish **binaries only, never configs**. Device config carries things that must not
  be republished — the device's `/etc/hosts` name mapping, and the VNC password; a glob that swept up
  `*.conf` would publish exactly what those two exist to have removed. `release.sh` now greps the
  staged manifest for `*.conf`, `/etc/hosts`, `/etc/hostname` and the three device config basenames and
  refuses — the negative control for a future component that forgets, rather than a rule each component
  is trusted to remember.

**[F10](#f10-single-pass-offline-commissioning--done-2026-08-05-confirmed-on-a-unit-2026-08-06) depends on this one** — an
offline commissioner has no toolchain to fall back on, so the release *is* its only source of
binaries. The obligations that only bite once artifacts are published are enumerated per artifact in
[`LICENSE.md`](LICENSE.md) — ⚠️ **measure a dependency's licence *version* rather than carrying it
forward**: this entry said ScummVM was GPLv2+ and the tree is **GPL-3.0-or-later**. `release.sh` generates
the per-release half as the bundle's `NOTICE`, and `LICENSE.md` says the two must agree; whether that
discharges the written source offer has not been checked by anyone qualified to say so.

---

### F10. Single-pass offline commissioning — **done 2026-08-05**, confirmed on a unit 2026-08-06

`commissioning/commission-offline.sh` does the whole job — card into a reader, two questions, card back,
unit boots working — and it was confirmed that way on a real card: one pass on a Kubuntu host, one boot,
launcher grid, SSH reachable by name, games, sound, and a reboot survived. What is built, its safety model
and every verification it performs are in `COMMISSIONING.md` and `commissioning/CLAUDE.md`. What is left:

- **Touch calibration still needs the device**, and always will — it is per-unit and per-panel. The wizard
  exists (Device Tools → Display → `CALIBRATE TOUCH`); one boot is all it costs.
- **The confirmed run carried `native_apps` only**, so ScummVM and `vnc_client` reached that unit over SSH
  afterwards. A three-component bundle has not been staged and commissioned in one pass. ⚠️ `usb_host`
  stays out of every bundle by design — it patches `uImage-system` on p1.
- ⚠️ **Anything more aggressive goes one increment per boot.** A failed boot yields no diagnostics — no
  serial console ([§3.12](SYSTEM_ANALYSIS.md#312-serial-ports)) — and the only post-mortem is mounting p3
  offline and reading `messages`, which helps only if it got as far as syslog.

---

### F14. Decide whether the boot progress bar comes back — open

**What was lost, and it is not a mystery:** the vendor's boot splash with a progress bar was `psplash`.
`device-files/clean-rules.conf` deletes `/etc/init.d/psplash` and `/etc/rcS.d/S01psplash` with the
reason *"Splash screen; it holds `/dev/fb0`"* — a real conflict, since our launcher needs that
framebuffer. Reported 2026-08-06 as missed but not much missed.

**What survives:** `/usr/bin/psplash`, `psplash-write` and `psplash.psplash-angstrom` are all in
`/usr/bin`, which nothing sweeps. Only the init script and its `rcS.d` link were removed, so this is a
*decision*, not a loss.

Two ways to have it back, if wanted:

1. **Restore the link and hand off cleanly.** `psplash` must release `/dev/fb0` before
   `S99roomwizard-app` starts — `psplash-write QUIT` is the mechanism. The keep-list and the boot-link
   set would both have to name it, since `device-files/clean-rules.conf`'s whitelist
   makes a link it does not name get swept on the next clean.
2. **Draw our own.** `app_launcher` already owns the framebuffer and there is no fb0 contention at all
   — a splash drawn by our stack sidesteps the handoff entirely, and can show something honest about
   what is loading.

Option 2 is the smaller change and cannot regress the boot; option 1 restores exactly what was there.
Neither is urgent — recorded so the deletion stays a decision with a known cost rather than a surprise.

---

### F15. USB host mode through commissioning — DONE 2026-08-08, confirmed on a unit 2026-08-09

USB host mode, the `xpad`/`joydev`/`ff-memless` modules and the 500 mA p1 power patch all ship, ON by
default, through both commissioning entry points. **Confirmed on a unit 2026-08-09 — the effect, not just
the write:** an Xbox controller straight into the micro-USB port through a passive adapter, no powered
hub. Mechanism, files and the *Failed Approaches* record live in `usb_host/README.md`; the p1 writer's
rules are in `lib/CLAUDE.md`.

**Still unmeasured, and now optional** — one SSH session each, no case-open, because
`rw_usbpower_apply_ssh` mounts `/dev/mmcblk0p1` on the running device: the `000000fa` hexdump of
`/proc/device-tree/ocp*/usb_otg_hs*/power` (the working pad *is* the same fact read at the other end), and
**a p1 rollback through `uImage-system.vendor`** — the remedy has never been exercised on hardware, so it
is worth one deliberate run *before* anyone needs it in anger.

**One scoped experiment would retire the p1 write entirely, and is worth trying first.** Patch the
**in-RAM** copy of the `usb_otg_hs` `power` property through `/dev/mem`: verify it reads `0x00000032`,
write `0x000000fa`, rebind, confirm 500 mA. That would make the whole fix an ordinary boot script and let
`--no-usb-power` go. ⚠️ **This is not the sysfs override already recorded as failed** — `usb_host/README.md`
keeps both, and says which is which. Open risk is address stability: the unflattened DT is early-boot
allocated, unlike the static symbol the existing patch aims at.

### F17. Bluetooth peripherals, and whether USB DMA is reachable — open, measured 2026-08-08

**The want:** a wireless game controller and a headset or speaker for ScummVM. The unit is PoE-wired, the
Xbox pad is wired, and the integrated speaker is poor
([§3.4](SYSTEM_ANALYSIS.md#34-audio)) — so every current option is a cable, and the one that carries sound
is the worst-sounding one.

⚠️ **DMA and Bluetooth are independent, and DMA is not what unblocks Bluetooth.** BT is
bandwidth-trivial: A2DP is tens of KB/s and a controller is a few hundred bytes/s, which PIO handles
easily. Do not treat "get DMA working" as a prerequisite.

**Bluetooth needs a USB dongle — there is no radio on the board.** No WiFi and no Bluetooth is fitted
([`HARDWARE.md` §4](HARDWARE.md#4-unpopulated-and-expansion)). The only radio site is `J5`/`J6`, an **XBee
802.15.4** socket, empty in all three units as received, on UART3 which is `disabled` in the device tree
— XBee is Zigbee and cannot host Bluetooth. And there is no second USB port and no footprint for one
([§3.6](SYSTEM_ANALYSIS.md#36-usb)), so the dongle occupies the single connector.

**A dongle is on hand as of 2026-08-13**, so this is no longer gated on a purchase. Its chipset is
unrecorded and decides which module is needed: `btusb` covers most, but the `lsusb` vendor:product is
the first thing to read, before any module is built.

**The kernel side is the `joydev` precedent again, and looks feasible.** `# CONFIG_BT is not set`, exactly
as `CONFIG_INPUT_JOYDEV` was before [F15](#f15-usb-host-mode-through-commissioning--done-2026-08-08-confirmed-on-a-unit-2026-08-09)'s
three modules — and that precedent worked. Every hard dependency is satisfiable, measured from
`usb_host/device_config`:

| Need | State | Consequence |
|---|---|---|
| `CONFIG_NET`, `CONFIG_CRC16`, `CONFIG_HID` | `=y` | built in, nothing to do |
| `CONFIG_CRYPTO_AES` | `=y` | built in |
| `CRYPTO_SHA256`, `CRYPTO_BLKCIPHER`, `CRYPTO_ECB`, `CRYPTO_CMAC` | `=m` | ⚠️ the `.ko`s must be **built and shipped** — the device's `/lib/modules/4.14.52/` ships empty |
| `CONFIG_CRYPTO_ECDH` | not set | needed only for BT LE Secure Connections; buildable as a module |
| `CONFIG_RFKILL` | not set | optional for `bluetooth`/`btusb`, not a blocker |

Module set: `bluetooth.ko`, `btusb.ko`, a dongle-specific firmware loader (`btrtl`/`btintel`/`btbcm`),
`hidp.ko` for the controller. Loadable because `CONFIG_MODULES=y`, `CONFIG_MODULE_FORCE_LOAD=y` and
`CONFIG_MODULE_SIG` unset.

⚠️ **The hard problem is audio CPU, not USB — measure before promising.** A2DP means software SBC encoding
on one 600 MHz core that ScummVM already holds at ~32 %
([§6.5](SYSTEM_ANALYSIS.md#65-software-rendering-techniques-that-paid-off)). NEON is available and D-Bus
already runs (`S02dbus-1` is a `keep`), so BlueZ has its bus, and `bluez-alsa` is the lean bridge rather
than PulseAudio on 234 MB. But ScummVM writes OSS `/dev/dsp` **mono**, so the audio path needs rerouting
— this overlaps [F1](#f1-port-audio-from-oss-to-alsa--open-phase-state-in-the-table-below). A2DP's
~100–200 ms latency is fine for point-and-click and wrong for anything twitchy. **The controller half is
much more likely to land than the audio half; do not sell them as one feature.**

**Can we get USB DMA?** Probably, but it is research with a worse failure mode than today's.
`# CONFIG_USB_INVENTRA_DMA is not set`, so `musbhsdma.c` is not compiled at all. ⚠️ **The
`CONFIG_DMADEVICES=y` / `CONFIG_TI_EDMA=y` that *are* set are a red herring** — that is the **system**
EDMA via dmaengine, not the Inventra engine inside the MUSB block that OMAP3 uses;
`CONFIG_USB_TI_CPPI41_DMA` (the dmaengine-based path) is unset and is for AM335x anyway. The lever is
`CONFIG_KALLSYMS_ALL=y`: every built-in symbol's address is readable at runtime, so a force-loaded module
could supply `musbhs_dma_controller_create` and `omap2430_ops.dma_init` could be pointed at it — the same
family as [F15](#f15-usb-host-mode-through-commissioning--done-2026-08-08-confirmed-on-a-unit-2026-08-09)'s
existing patch. ⚠️ **But today's noop stubs fail *safely*, falling back to PIO, whereas a misbehaving DMA
controller scribbles into RAM.** No kernel rebuild is available to do it the clean way
([§7](SYSTEM_ANALYSIS.md#7-kernel-policy)).

**Where the two questions do connect.** `CONFIG_SND=y` and `CONFIG_SND_USB=y` but
`# CONFIG_SND_USB_AUDIO is not set` — so a **wired USB DAC** is also one module build away, with no
encoding, no pairing and no latency, and it fixes the speaker complaint directly. But uncompressed PCM at
48 kHz stereo is ~190 KB/s over PIO, which is where DMA would start to pay. **BT audio: low bandwidth,
high CPU. USB audio: high bandwidth, low CPU.** If the goal is "sound that does not suck", the DAC is the
cheaper experiment; if it is "no cables", it is Bluetooth.

**Two cross-cutting constraints on any dongle:** it draws ~50–100 mA, which is marginal against the
current 100 mA budget — an *independent* argument for F15's 500 mA patch — plus the 802.3af power budget
and the case's total lack of ventilation slots
([`HARDWARE.md` §4](HARDWARE.md#4-unpopulated-and-expansion)).

---

### F11. One home for the host build prerequisites — open

**Two delivery modes, and only one of them has a toolchain.** *Delivery*: someone clones the repo,
puts a card in a reader, answers a few questions, puts the card back, and the device works — they may
never build anything. *Development*: we build and deploy onto an already-clean device. F10 serves the
first, `deploy-all.sh` the second. This item is about making the second reachable on a fresh machine.

**What exists today: six checks, no installer, and they disagree.**
[`native_apps/build-and-deploy.sh:127`](native_apps/build-and-deploy.sh#L127),
[`vnc_client/build-and-deploy.sh:92`](vnc_client/build-and-deploy.sh#L92),
[`scummvm-roomwizard/build-and-deploy.sh:266`](scummvm-roomwizard/build-and-deploy.sh#L266),
[`vnc_client/build-deps.sh:146`](vnc_client/build-deps.sh#L146) and
[`usb_host/build-and-deploy.sh:66`](usb_host/build-and-deploy.sh#L66) each do their own `command -v`
and print their own hand-written `apt` line — `gcc` only, versus `gcc g++`, versus `+cmake wget tar`.
None installs anything.

**One asymmetry that is correct and stays:** the *cross-compiled* dependencies already install
themselves. `build_arm_deps` fetches and builds zlib 1.3.1 + libpng 1.6.43 into
`scummvm-roomwizard/arm-deps/`, and `vnc_client/build-deps.sh` does zlib / libjpeg-turbo /
LibVNCServer into its own prefix. Both idempotent, neither needs `sudo`. Only the *host packages* are
check-and-tell.

**Intent: one `setup-build-env.sh` at the repo root, one `roomwizard.sh` entry, one package set.**

⚠️ **What this dev host actually has, measured 2026-08-06** — because the opposite was on record here
and it changed how the whole backlog was ranked. Inside **WSL** (Ubuntu 20.04): `gcc`, `g++`,
`arm-linux-gnueabihf-gcc`, `arm-linux-gnueabihf-objdump`, `sfdisk` (at `/usr/sbin/sfdisk`, on the
non-root `PATH` in both login and non-login shells), `python3`, `dash`, `gh`. Absent: `shellcheck` only
([C7](#c7-run-shellcheck--open)). In **Git Bash**: none of them, and `python3` resolves to the Windows
App Execution Alias that prints *"Python was not found"*. **A prerequisite check run from the wrong
shell reports the wrong answer**, which is what happened — so an installer for this must state which
shell it is measuring, and `wsl.exe -e bash -lc` is the one that counts.

```text
gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf
build-essential cmake wget tar dash          # every component, one set
python3 python3-pil                          # fb565_to_png.py only
bc libssl-dev bison flex                     # usb_host kernel modules only
```

- ⚠️ **Name `binutils-arm-linux-gnueabihf` explicitly** even though the `gcc` package pulls it in.
  `commissioning/commission-offline.sh` needs `arm-linux-gnueabihf-objdump` on a host that has **no compiler at
  all**, and a missing objdump there is a refusal, not a pass.
- **The component scripts keep their own checks** — they are meant to run standalone — but stop
  reciting package lists and point at the one script instead. Flagging stays; only the six copies go.
- **Prompt only when stdin is a TTY**, plus `--install-deps` for scripted use. A blocking `read` would
  hang `release.sh` and `deploy-all.sh`, which invoke the component scripts non-interactively.
- Print the exact `apt` command before running it. `apt`-only, with a clean refusal on a non-Debian
  host rather than a guess.
- ⚠️ **The ScummVM half is not `apt`, and it is the actual blocker on a fresh clone.** The upstream tree
  at the repo root is gitignored, so a clone has no `scummvm/`:
  `git clone https://github.com/scummvm/scummvm.git`, `git checkout branch-2-8`, then
  `bash manage-scummvm-changes.sh restore`. This WSL sits at `eaccc461` (2024-08-29). An installer that
  skips this has not solved the problem it exists to solve. `vkeybd_roomwizard.zip` and `scummvm.ppm`
  *are* tracked, so those come with the clone.

---

### F12. Install from a published release — open

`--bundle` is already `commissioning/commission-offline.sh`'s single source of binaries and everything downstream is
origin-agnostic — unpack, `rw_bundle_check`, the ARM gate, install, md5 — so `--release <tag|latest>`
is a fetch into a temp directory plus a handoff to the existing path. That also removes the
copy-a-tarball-to-the-commissioning-host step from the delivery mode of
[F11](#f11-one-home-for-the-host-build-prerequisites--open).

- **Never the default, and the `--help` must say so.** The script is called `commissioning/commission-offline.sh` and
  its premise is that no network is required. A stock unit with `net.mode = manual` is unreachable by
  any other means, which is exactly when there may be no network to hand.
- ⚠️ **The bundle's `.md5` manifest proves internal consistency, not authenticity.** Nothing is signed.
  Print the tarball md5 — `release.sh` already prints it at publish time — and compare against the
  asset digest `gh` reports.
- `gh release download` preferred, `curl -L` as the fallback for a public repo; auth is needed only if
  the repo is private.
- ⚠️ **Untestable until a release exists.** `--tag` has never run
  ([F9](#f9-ship-binaries-as-github-releases--partly-built-2026-08-05-open)). Publish one first, then
  build this against a real asset rather than a hand-made fixture.
- Distinct from F9's still-open `--from-release <tag>` on `deploy-all.sh`, which serves the SSH
  development flow rather than the offline one.

---

### F18. `roomwizard.sh` item 3 has no bundle option — open, asked for 2026-08-09

Menu item 3 always builds from source: [`roomwizard.sh:278-292`](roomwizard.sh#L278-L292) calls
`deploy-all.sh "$TARGET"` (or one component), and nothing in the menu reaches
`deploy-all.sh --from-bundle <tar.gz|dir> <ip>` — **which already exists and already works.** So this
is menu exposure, not new capability: a second prompt beside "Component (Enter = all)", defaulting to
build-from-source so no keystroke changes meaning.

Why it is worth having: after a `./release.sh --stage-only` there is a tarball in `build/release`, and
re-deploying from it is seconds against ScummVM's ~1m35s–2m20s rebuild. It also puts the *tested* bytes
on the device rather than a fresh build of them.

- **Offer the same default path the offline installer uses** — `build/release`, which item 6 already
  prompts with — so the two front doors name one location.
- ⚠️ **A bundle carries no config**, by construction: `release.sh` refuses to publish `*.conf`,
  `/etc/hosts`, `touch_calibration` and the rest. So a from-bundle deploy leaves an unprovisioned unit's
  touch calibration absent, exactly as a from-source deploy does. Not a difference between the two, but
  the prompt should not imply the bundle is the whole device state.
- Adjacent and both still open: [F9](#f9-ship-binaries-as-github-releases--partly-built-2026-08-05-open)'s
  `--from-release <tag>` on `deploy-all.sh` (fetch, rather than a local file) and
  [F12](#f12-install-from-a-published-release--open)'s `--release` on the offline installer. This one
  needs neither a network nor a published release, which is why it is separable and small.

---

### F13. Commissioning from Windows without WSL, and from macOS — open, unsolved

The delivery mode of [F11](#f11-one-home-for-the-host-build-prerequisites--open) assumes the operator
can run the card path. Today that means Linux, or Windows with WSL2. This entry exists so the gap is
recorded rather than discovered by someone holding a card.

⚠️ **This is not a shell-portability problem, and rewriting `bash` as POSIX `sh` would not touch it.**
The blocker is the *kernel's* filesystem support: `commissioning/commission-offline.sh` needs read-write ext4 across
four partitions, real symlink creation (the `rc*.d` links) and a real `chmod` (the `+x` assertion is a
measurement precisely because ext4 honours it). No shell dialect supplies any of that.

| Host | Route | Status |
|---|---|---|
| Linux | native reader | works; the only fully verified path once Unit A passes |
| Windows + WSL2 | `wsl --mount \\.\PHYSICALDRIVEn --bare` | the documented path; `wsl --install` is one command |
| Windows, no WSL | none | no native ext4. Third-party drivers are not something to stake a card on |
| macOS | Linux VM, or a paid ext4 driver | worse than Windows: no kernel ext4 write support, and `ext4fuse` is read-only |

**The option that would actually deliver all three is a bootable USB commissioner image** — a small
Linux that boots, finds the card and runs the existing script unchanged. That keeps one implementation
and moves the portability problem to a boot medium instead of into the script. Substantial new work,
deliberately not scoped here.

**Interim, and cheap:** state the host requirement plainly in `COMMISSIONING.md` instead of letting the
instructions imply that any machine with a card reader will do.

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

### C2. Split `device_tools.c` — open

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
   until you are mis-tapping by 30 px. Six exist — `tests/touch_calib_test.c` (the calibration fit
   end-to-end), `tests/gradient_test.c`, `tests/framebuffer_bpp_test.c`, `tests/gamepad_latch_test.c`,
   `tests/button_latch_test.c` (the once-per-process touch button latch — its group A drives the old
   `button_is_touched() && button_check_press()` idiom and asserts the second tap is swallowed),
   `tests/audio_gen_test.c` (the audio generator and the mix bus — arithmetic, the frame-aligned write
   loop, the summed voices and the pump's pacing —
   [F1](#f1-port-audio-from-oss-to-alsa--open-phase-state-in-the-table-below) Phases 2–3).
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

### C9. A bundle cannot prove its stripped binaries were ever gated — open, measured 2026-08-08

`native_apps/check-arm-safe.sh` is sound only on a binary that still has its symbol table, and both
`scummvm` and `vnc_client` ship stripped. Why, with the byte-level measurement:
[`SYSTEM_ANALYSIS.md#61-cortex-a8-has-no-hardware-integer-divide`](SYSTEM_ANALYSIS.md#61-cortex-a8-has-no-hardware-integer-divide).
So all three component build scripts gate the unstripped artifact at build time, and
`commissioning/commission-offline.sh` reports the stripped remainder as **taken on trust** — loudly, by
count and by name — rather than refusing it.

⚠️ **What is open is that "taken on trust" is the honest description, and it should not have to be.** The
sound verdict exists only at build time, so it has to travel with the bundle: a per-component attestation
in `manifest.d/`, written where the unstripped artifact is still on disk, and checked by the installer
instead of re-disassembling. `release.sh`'s own bundles would then carry proof, and a third-party bundle
carrying none would be *visibly* unattested instead of indistinguishable from an attested one. Until then
the installer's summary must keep saying `TAKEN ON TRUST` in those words.

### C10. Make a deep game state reachable without playing to it — open

`brick_breaker`'s indestructible bricks only exist from **level 5 up**, so verifying them costs a full
play session of somebody's time — which is why that check keeps being postponed, reasonably. A
`--level N` argument or a debug entry in the pause dialog turns it into one launch, and would serve any
future level-dependent bug. Generalise to the other games where a state is expensive to reach.

**The fork above is decided: a pause-dialog entry, not a CLI argument** (2026-08-10). Office Runner's
TRAINING toggle is the first worked example — `platformer.c`'s pause dialog, 10 lives and one more per
50 coins, which makes its level 3 reachable by hand without a flawless run. A `--training` flag was
offered and declined, so the shape to copy is menu-only. Note what that costs, because it is the whole
of C10's original argument: a menu toggle is **not** script-reachable — there is no `/dev/uinput`, so
nothing can tap it — and a mode with no CLI entry therefore has no first-screen SSH check either. It
makes a deep state cheaper for a **human**, not automatable. `brick_breaker` already has both halves
(`--test` and a pause toggle), so its level-5 problem is still open on the level number, not on the
mechanism.

### C12. One commissioning entry point — open

⚠️ **What is left is the front door, not a capability.** Both delivery situations are reachable with no
toolchain, but by two different scripts with two different flag vocabularies, and the SSH one still has a
reboot in the middle:

| Situation | Today | One command? |
|---|---|---|
| Bought a unit, **no network access to it** | `sudo ./commissioning/commission-offline.sh --bundle <tar.gz>` | **yes** |
| **Already has SSH** to it | `./commissioning/provision.sh <ip>` → reboot → `./deploy-all.sh --from-bundle <b> <ip>` | no — two, with a reboot between |

`commissioning/commission.sh` is the remaining idea: **one** entry with `--card [--disk X]` or
`--ssh <target>` and `--bundle` on both, composing the scripts that already exist rather than adding
behaviour. `roomwizard.sh` covers the same ground as a menu today, which is why this is an idea and not a
defect — and `provision.sh` now cleans by default, so the front door no longer needs a reason beyond
convenience. Everything else this entry opened for landed 2026-08-06/08 (`git log --grep=C12`): one
provisioning data file with two executors, the two `dropline` config edits, `--from-bundle` over SSH, and
the `lib/`+`commissioning/`+`device-files/` layout.

⚠️ **A blocker this entry inherited, and it is a test-coverage gap rather than a code one:** neither
`tests/commission_offline_test.sh` nor any non-dry `commission-offline.sh` run has been executed since
the provision fold, because `commissioning/card-prep.sh`'s `sudo` on the `/etc/shadow` write cannot be
driven non-interactively from this harness (`sudo: a password is required`) — a run stalls waiting for a
password it cannot be given. **Run the root suite under an interactive sudo before trusting the offline
⚠️ **A blocker this entry inherited, and it is a test-coverage gap rather than a code one:** neither
`tests/commission_offline_test.sh` nor any non-dry `commission-offline.sh` run has been executed since
the provision fold, because `commissioning/card-prep.sh`'s `sudo` on the `/etc/shadow` write cannot be
driven non-interactively from this harness (`sudo: a password is required`) — a run stalls waiting for a
password it cannot be given. **Run the root suite under an interactive sudo before trusting the offline
installer again.** The block is not the mount; `--base` needs no root.

**A second half-measure in the same vocabulary:** `provision.sh --dry-run` exits before the provision
step, so it previews the clean and the p1 write but never the install/link plan. The plan is compiled on
the host, so a full preview is cheap; nobody has asked for one.

### C14. Documentation cleanup — open, **phase state in the table below**

Three documents had outgrown their jobs, measured by reading each in full: this file 2287 lines against
a stated job of "open work only", `SYSTEM_ANALYSIS.md` 2214 with ~470 of history, hypothesis and
host-tooling process, `CLAUDE.md` 829 of which ~525 was duplicate, reference or war-story narration.
Working detail: `~/.claude/plans/peaceful-herding-valiant.md`.

| # | phase | from → to | state |
|---|---|---|---|
| 1 | `CLAUDE.md` → `lib/`, `commissioning/`, `tests/`, `device-files/` + compress | 829 → 366 | ✅ 2026-08-15 |
| 2 | De-reference the plan IDs cited from code — **qualified form** | 83 → **0** | ✅ 2026-08-15 |
| 2b | The same, **bare form** (`(<ID>)`, `see <ID>`, `IMPROVEMENT_PLAN <ID>`) | 41 → **0** | ✅ 2026-08-16 |
| 3 | This file: extract the six unique facts, then cut to open work | 2287 → 1256 | ✅ 2026-08-15 |
| 3b | (folded into 3 — the second half of the same cut) | 2149 → 1256 | ✅ 2026-08-15 |
| 4 | `SYSTEM_ANALYSIS.md`: evict history/hypothesis/process, tag unmeasured claims | 2214 → **2049**, ~1890 met by phase 5 | ✅ 2026-08-16 |
| 5 | `HARDWARE.md` split out, with the photos beside the parts they show | 2049 → **1871** | ✅ 2026-08-16 |
| 6 | `native_apps/CLAUDE.md` compress | **891** → ~325 | ⬜ |
| 7 | `MEMORY.md` index compression (hook lines up to 684 chars) | **10.2 KB** → ~3 KB | ⬜ |

**Phase 3 is done and it landed at 1256, not the ~700 the table used to promise.** ⚠️ **That target was
never reachable from its own itemised work list, and the arithmetic says so**: the eight deletions and four
compressions it named remove ~1040 lines gross from 2287, and the stubs and receipts put ~100 back. Reaching
700 would have meant cutting a further ~550 lines of *open* items, which is not what this phase was for.
**Price a target against the list that is supposed to deliver it before writing the target down** — the
number here is now the measured result.

⚠️ **Phase 4's ~1150 had the same defect and it was caught before any cutting, which is the point.** The
itemised survey — every block located by reading `SYSTEM_ANALYSIS.md` in full, with what survives each
compression — sums to **~325 lines**, so the reachable figure is **~1890**, and phase 5 taking §2 and the
photo index out brings it to **~1680**. Reaching 1150 would mean deleting ~700 lines of *measured device
facts*, which is that document's entire job. And the original survey's own finding ("~470 lines of history,
hypothesis and host-tooling process", i.e. 2214 − 470 = **1744**) does not yield 1150 either — so the
number was never derived from the measurement that was supposed to support it. **Two phases in a row: sum
the list.** Phase 4 parts 1+2 landed 2026-08-16 at **2049**.

⚠️ **And the ~325 itemised survey was mispriced too, in a third and different way — by classification, not
arithmetic.** §3.3 Touch was priced at ~85 lines out and the whole of part 2 delivered ~50, because the
survey counted the wizard step table, the `touch_raw` mode table and the config-format bullets as
narration. They are **reference content with no other home**, and cutting them only moves the cost to
whoever next re-derives them from `device_tools.c`. What *did* compress hard was every block already
duplicated in `native_apps/CLAUDE.md` — six of part 2's cuts are duplicate deletions, each now carrying a
group C receipt. **Ask "where else does this live?" of a block before pricing it:** a duplicate is nearly
free, a war story compresses to its rule, a reference table compresses to nothing. Remaining headroom here
is small; **phase 5 (§2 + the photo index out to `HARDWARE.md`) is where ~1890 is actually met.**

**Phase 4 part 3 closed 2026-08-16, and it was a content pass with no line target** — the line goal was
already met by phase 5, so leaving the row `⏳` against a number that had moved said nothing about the work
left. What part 3 owed was the *checkable* half of the tagging invariant, and all of it is in place: the
legend is §1, the tags are applied, and both hedges that needed a test were filed as items rather than left
in prose (**F1 panel question 6**, the second half of **B3c**). The closing sweep looked for unmeasured
claims by **hedge vocabulary** — *probably · likely · presumably · appears to · seems · may be · assume ·
untested · untried · nobody has* — and the triage is the point: most hits were the instrument or were
already honest. Two were the legend defining those very words, nine sat inside an existing tag or were
explicit *negative* statements (§3.3's "outer-band slope compression is **NOT** established", §3.4's
"checked, not assumed"), and one described what the *code* assumes rather than what this document claims. **One
genuine untagged inference survived** — §3.12's "expect the vendor to have assumed a Series 1 module",
inferred from the command set the vendor's tooling uses and from no module ever read on a unit, now tagged.
⚠️ **A hedge-vocabulary sweep mostly finds its own legend and its own warnings**, so triage it before
believing the count — same shape as every other gate here.

**Phase 5 landed 2026-08-16 at 1871, and it was priced correctly by asking the classification question
first.** Every block in §2 and Appendix A was surveyed as *duplicate / war story / reference* before a
target was written, and the answer was that **none of it is duplicated and none of it is narration** — it
is all reference, so it compresses to nothing and the phase is a pure move: ≈183 lines of §2 body and
photo appendix out, ~5 back as the pointer §2 becomes and the Contents row naming the new file — net
**178**. Predicted ~1870, landed **1871**, the first target in four to hold. `HARDWARE.md` is
236 lines, i.e. larger than what left, because photo captions and a seam were written for it — that is the
point of the split rather than a cost of it, since the always-open document is the one that had to shrink.

⚠️ **Seven part numbers that looked like clean move receipts were already duplicated inside
`SYSTEM_ANALYSIS.md`** — `550-0204-03`, `MT29F2G16ABBEAHC`, `POE13F-12L`, `GC5.5V0.47F`, `1-6605834-1`,
`TPS23750` and `TI-14` each appear in §2's inventory *and* in the subsystem section that drives the part
(§3.5, §3.10, §4.3, §4.7). A receipt on any of them would have fired `NOT MOVED` forever. The eleven
tokens used instead were each grepped for uniqueness first, and finding this is the reason to grep: **a
"move it out" receipt needs a token that is unique to the block being moved, not merely distinctive.**

⚠️ **A fifth stale claim, and again it was a pointer that its destination did not satisfy.** §2.4 said
"the staging that protected a single module is in `IMPROVEMENT_PLAN.md` F5" — F5 was rewritten 2026-08-13
to say that staging is **spent**: there are three modules, one is seated, and the open steps are proving
the pinmux and the seated module's health. Rewritten to what F5 now says, in `HARDWARE.md` §4. **That is
five for five: every pointer checked against its destination this cleanup has found one wrong.**

⚠️ **Group C is the instrument for a duplicate deletion, not only for a planned extraction.** Writing the
row *before* the cut is what proves the destination already holds the fact, and it is cheap — part 2 added
six rows (`TouchCalibSweep`, `clamp_to_hw`, `0..60000`, `594, 614, 817`, `FB_TOUCH_INSET_MAX`,
`publish_safe_area`) and all went green. ⚠️ **But a pointer must itself be checked against its
destination:** two cuts pointed at `native_apps/CLAUDE.md` for the fit's sanity-gate criterion, which
lived **only** in `SYSTEM_ANALYSIS.md`. It was written into `native_apps/CLAUDE.md` before the pointer was
allowed to stand — a "see X" that X does not satisfy is worse than the duplication it replaced.

⚠️ **A fourth stale claim, caught the same way as the three above.** §3.3 told the reader to treat
`Touch raw range set (linear):` as the signature of a pre-8-number binary. That string is **live** —
`touch_input.c:428`, `touch_set_raw_range()`, the `EVIOCGABS`/RESET path — so a current binary prints it
routinely and the test was worthless. The real discriminator is `(piecewise)` in the
`Calibration loaded from:` line (`:589`). Fixed with the measurement, not by deleting the sentence.

What the second half cut: `B28`, `B31`, `D7b` and the 8 struck-through panel rows deleted; `B32`
(186 → 24), `F15` (388 → 18) and `F10` (132 → 15) reduced to open stubs with their headings byte-for-byte
intact; `F1` compressed 281 → 156 with its phase table and ⏳ outstanding block untouched; and the missing
`D7` heading restored — 19 lines of mDNS residue had been sitting under `B27`, and `git log -S` recovered
the heading the 2026-08-05 edit dropped. `B28`'s one genuinely-open remainder survives in `C12`, where the
other half-measure in the same flag vocabulary already lived: `provision.sh --dry-run` exits before the
provision step, so it previews the clean and the p1 write but never the install/link plan.

⚠️ **Deleting an item breaks the gate in the shape phase 2 did not fix.** Group B went 41 → **53** on the
cut, because `D7b` (8 sites) and `B28` (4) were cited in the **bare** form that phase 2 left for 2b, and a
bare citation of a *live* ID resolves — so those 12 were invisible until the heading went away. All 12 were
de-referenced with the clause they already implied and the gate is back at 41, which is 2b's own list.
**Before deleting an entry, grep the bare form too, not only the qualified one.**

⚠️ **Three claims in the deleted text were wrong, and one was wrong in a way a receipt cannot see.**
`F10`'s opening said its only outstanding task was "running it against a real card and booting a real
unit", 91 lines above its own *Confirmed on hardware* subsection saying exactly that had happened. `F9` and
`F10` both said ScummVM is GPLv2+; the tree is **GPL-3.0-or-later** (`scummvm/COPYING`, `LICENSE.md`). And a
survey of the third reported `LICENSE.md`'s written source offer as a missing-file compliance gap —
`LICENSE.md:95` says outright that `NOTICE` is generated per release by `release.sh` and shipped in the
tarball, so **checking the claim is what stopped that one reaching a doc.**

**Decisions taken, do not relitigate.** No closed-work ledger: closed items are deleted outright,
because ⚠️ **an ID is not a durable reference** — 20 IDs cited from shipped source resolve to nothing
here, and `git log --grep` does not rescue them either (`B3k` and `B13c` return **zero** commits). The
replacement rule is that a code comment must carry its own reason, which is what phase 2 does. Phase 4
keeps this file's filename and every heading it keeps content for, because ~130 anchor links into it
live in `.sh`, `.conf`, `.c` and `.py` files.

**The gate for that rule is `tests/doc_check.sh` group B**, host-only, with a negative control in both
directions. It scans two citation shapes and fails on any ID with no heading here, which makes it
self-maintaining in the direction that matters: **deleting a closed item below fails the gate until
every citation of it has been rewritten.** ⚠️ It currently reports **41**, all bare-form — that is
phase 2b's work list, printed by the run rather than written down anywhere.

⚠️ **The counts in this table were both bigger than the survey said, and the surveys were not sloppy —
they were scoped.** Phase 2 was planned as "~45 sites" from `.c/.h/.sh/.py/.conf`; the qualified figure
was **83**, because four `.md` files cite IDs in a backticked form the first regex did not match. The
bare form was found only after the qualified scan reached zero. **A clean zero from a gate is evidence
about what the gate looks at, not about the repo.**

⚠️ **Three of the 83 comments were stale about the current tree, not just about a dead ID** — an ID in
a comment is often the only reason nobody re-read it. `fb_clear()`/`fb_draw_pixel()` are bpp-aware now
(`vnc_client/CLAUDE.md` said otherwise); `poll_touch()` no longer latches a touch region's `.held`
(`frogger.c`, `platformer.c` said otherwise, present tense); `commissioning/set-hostname.sh` does write
`/etc/dhclient.conf` (`SYSTEM_ANALYSIS.md` §3.5 said nothing did). Each was fixed with the measurement.

⚠️ **`doc_check.sh` counted its own prose three times**, in three shapes — a quoted heredoc fixture,
then the header text about bare IDs, then the header text about the no-`.md` form — inflating the number
it reported each time. Both scans now skip it; the self-test fixture is its coverage. **When a gate's
number looks wrong, check what part of it is the harness first.**

**Invariants the cleanup establishes**, each checkable: no file outside this one depends on a plan ID; a
statement in `SYSTEM_ANALYSIS.md` is measured or it is tagged (`[inferred]`, `[unverified]`, `[n=1]` —
**the legend is now §1 *How to read a claim in this document***, added by phase 4 part 2, which also
replaced §1's *older version* changelog table after verifying all five of its rows were stated in their
own sections), and anything needing a test becomes an item here instead — part 2 filed two: **F1 panel
question 6** (does the ~50 % attenuation belong on the synth or on all output) and a second half of
**B3c** (`TARGETS` repeated, to settle whether the interior slope is really steeper than the outer bands).
No line count in prose that a session could carry stale (this file said `device_tools.c` was 2651 lines; it
is 3703 — the count is out of `C2`'s title rather than corrected there, per the invariant).

**Phase 1's negative control:** every one of the old `CLAUDE.md`'s 56 ⚠️ warnings was checked to survive in
one of the five files or in `native_apps/CLAUDE.md`; 56 of 56 accounted for.

**Group C — extraction receipts — is built (2026-08-15), and it checks a MOVE, not a copy.** Each row is
a distinctive token, the file that must now hold it, and the file it must have *left*; "one fact, one
home" means a token present in both is the drift this cleanup exists to remove. Run *before* a deletion to
confirm the destination has the fact and *after* to confirm it survived. Its self-test fires in both
directions (an unextracted token and a copied-not-moved one). ⚠️ **Three shapes of the same defect it
cannot see, all under-reporting:** a token can be present while the sentence around it is wrong (three of
phase 2's 83 comments were exactly that); a fact extracted with no row is invisible, so a clean run says
"every receipt holds", never "nothing was lost"; and it cannot tell a paragraph from a stray see-also
line. The ⚠️-warning survival census therefore stays a manual per-phase control. **All eleven receipts are
green as of phase 3's close** — the six `NOT MOVED` rows that were the pre-deletion state went to zero as
each source entry was cut, which is what makes the group a progress meter and not just a gate.

**Group A — every markdown anchor resolves — is built (2026-08-16)**, which is what phase 4 needed: a
moved or retitled section is otherwise silent, and ~120 of these anchors live in `.sh`, `.c`, `.py` and
`.conf` comments that no markdown linter reads. It resolves each anchor's fragment against the slugified
headings of the target file, is fence-aware (a `#` comment inside a ```` ``` ```` block is not a
heading), and is controlled in both directions. **234 anchors, 0 dangling** as of build, so phase 4
cannot move a section silently. ⚠️ **Most of the first run's 41 findings were the gate, not the repo**,
which is the reason to itemise before believing a number: 25 were bare filenames in code comments that
resolve only from the repo root, 7 were CSS colours written `(#0a0e27)`, and 2 were a fragment eating a
sentence-ending period. **What it genuinely found: 8 anchors in shipped `.sh` comments truncated to a
section number** — `#61` where the heading slugifies to `61-cortex-a8-…`, all dead, all now spelled out —
and 2 more of this gate's own documentation matching itself. ⚠️ **Naming one of those in prose made the
number 1 again, twice.** For group A the safe form is a fragment in angle brackets or a fragment with no
`.md` in front of it: bracketing only the *filename* half does not help, because the scan's path part
matches the empty string.

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

1. **[F1](#f1-port-audio-from-oss-to-alsa--open-phase-state-in-the-table-below) (ALSA) is the place to
   start, and its gate is already through.** It is the biggest user-visible improvement available, pure
   userspace, no kernel work, no brick risk — and Phase 0 is **measured on `.188`, not assumed**: a 21 ms
   period is granted, every rate is granted exactly (so nothing resamples anywhere), and native ALSA was
   *heard* making sound before a line of our code existed. Phases 1–2 are closed; **Phase 3 is deployed
   to `.188` and its period-aligned lead has never been heard — that listen is the next action**, and it
   costs one panel visit rather than a session.
2. **F2 (DSS overlays)** is the biggest performance win, also pure sysfs.
3. **C10 before panel check #2** — it converts a play session into one launch, and every future
   level-dependent bug pays the same toll until it exists.

⚠️ **B32 is not the place to start, and that is a result rather than a gap.** Three mechanisms read out
of the MUSB driver have each been applied and **refuted on hardware**; the answer is the shipped RESCAN
button, verified on a panel. **Read B32's measured/inferred split before proposing a fourth theory, and
require of it the one thing all three failed to explain: how a port that probed with an EMPTY socket ever
obtains a session.**

Two device checks remain and both are optional rather than blocking, one SSH session each and no
case-open: they are [F15](#f15-usb-host-mode-through-commissioning--done-2026-08-08-confirmed-on-a-unit-2026-08-09)'s
two remainders.

[F11](#f11-one-home-for-the-host-build-prerequisites--open) reads more urgent than it is: **this WSL
has the whole toolchain** (measured 2026-08-06 — see F11), so it is a fresh-machine and documentation
item rather than a blocker, and [B27](#b27-sfdisk-absence-is-reported-as-a-test-failure-not-a-skip--open-latent)
cannot fire here. [F12](#f12-install-from-a-published-release--open) unblocks anyone who is
not the developer; [F13](#f13-commissioning-from-windows-without-wsl-and-from-macos--open-unsolved) is
recorded rather than planned, because the honest answer is a bootable image.

Everything else is genuinely unranked rather than deprioritised. **F6 (multi-touch) is the one to
consider promoting**: the register map is published, so it is far less speculative than its position
in this list suggests.
