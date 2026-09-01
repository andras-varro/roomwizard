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

**Nothing is outstanding.** The last item — the crack when a finger lifts off `Tap-a-Theremin`'s pad — was
answered at the panel 2026-09-01 on `.188`: *"working perfectly"*, unhedged. ⚠️ **That is a verdict where a
description was asked for**, so the ~139 ms tail the fade leaves behind the finger went unmentioned and is
neither confirmed nor denied. The pass stands anyway, because the guard was against a *tail* being reported
as the defect and no defect was reported at all — but a verdict is the weaker answer, and the next item
should say which one it needs.

The high-score chime and the game-over descent were confirmed distinct to the ear 2026-09-01, so the
question of whether two simultaneous sounds separate does not need asking again for that pair.

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

### B3c. Whether the interior touch slope is steeper than the outer bands — open, not established

The model and the fix have shipped; [`SYSTEM_ANALYSIS.md#33-touch`](SYSTEM_ANALYSIS.md#33-touch)
carries the measurement, the method and the reference capture. **Read that section before touching
the touch model** — this question has been answered wrongly in *both* directions, and each wrong
answer came from inferring a hardware limit *through* the calibration under suspicion.

**The second-unit question is settled and needed no code change.** Touch is confirmed working on two or
more units (operator, 2026-08-31) — the *shape* generalising, not the digits transferring, which is what
`[n=1]` means. Either answer always left per-panel variation to the runtime measurement, so a recorded
`SWEEP`/`INSET` tsv from a second panel is documentary value now, not a blocker.

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

### B33. A USB babble error leaves a `printk` loop that hard-resets the device — open, **measured 2026-08-17**

⚠️ **One babble error puts the kernel into an unbounded message loop that outlives the device's removal and
ends in a hardware reset 46 min later.** From `.188`'s persistent log — `/home/root/log/messages` on p3,
which keeps the previous boot's tail across a reset — `musb-hdrc: Babble`, then `usb 1-1: USB disconnect,
device number 2`, then `musb_bus_suspend 2589: trying to suspend as a_idle while active` repeating: syslog
collapsed it as `last message buffered 1201140 times` in one 10-minute window (≈2000/s), and it ran **46
minutes with no pad plugged in**. `FAT-fs (mmcblk0p1): Volume was not properly unmounted` in the next boot
says hard reset, not clean `reboot`. **The reset itself is the hardware watchdog** — `/usr/sbin/watchdog`
carries no check directives (`SYSTEM_ANALYSIS.md`), so it decided nothing; it was starved of CPU and missed
its 60 s feed. ⚠️ **It is also a measurement contaminant**: anything judged by ear or timed during a storm
was judged on a starved device, and a frozen app is a *symptom*, not the bug — an on-panel tool appearing
to hang is what surfaced this. **Run `dmesg | grep -c musb_bus_suspend` before trusting an on-device
measurement.** The loop is not yet read out of the driver: start at `musb_bus_suspend()` in
`usb_host/linux-4.14.52/drivers/usb/musb/` and at whether the `Babble` path leaves the port marked active.
Distinct from B32, which is about enumeration.

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

### B35. `gamepad_rescan()` logs a line per poll, so one session fills the log — open, measured 2026-08-21

`/var/log/roomwizard/app_stdout.log` grew 134 KB across one play session on `.188`, and **1720 of those
lines are the same one**: `gamepad: found gamepad 'Microsoft X-Box 360 pad' at /dev/input/eventN`. Every
game calls `gamepad_rescan()` on a 5 s timer (`RESCAN_INTERVAL_MS`, `native_apps/CLAUDE.md` → *Input*) and
the rescan re-`open()`s and re-announces a pad that never left. It is cosmetically harmless and
operationally not: that log is the only instrument a no-microphone audio session has, and 1720 repeats of
one line is what a real counter line has to be found among. Print on a CHANGE in what was found, not on a
poll — and keep the first announcement, which is genuinely useful.

---

## Features

All userspace. No kernel work.

### F20. Audio tidy-up left behind by the ScummVM adapter — open

**The audio subsystem is DONE where a player can hear it** — the mix bus, the continuous stream, the clip
bank, the WAV beds, the per-game sound sets and the level all ship and are heard. Its device facts are
[`SYSTEM_ANALYSIS.md#34-audio`](SYSTEM_ANALYSIS.md#34-audio) and its authoring rules
`native_apps/CLAUDE.md` → *Audio*, *Mixing* and *Sound assets*; the ALSA port is **not planned**
(`/dev/dsp` and `/dev/snd/pcmC0D0p` are the same PCM, so it buys latency and nothing else, and no latency
symptom has ever been reported).

✅ **The adapter is DONE, and heard.** `oss-mixer.cpp` keeps only the mixer, the fill and the service
thread; the `/dev/dsp` open, the ioctl order, the ring query, the silence prefill, the EAGAIN retry, the
wall-clock deadline and the emergency second write all live in `common/audio_out.{c,h}` instead — one
implementation of the device half, which is the point, for the emulator ports still to come. Linked,
deployed and checked at the panel 2026-09-01: **Full Throttle plays correctly, audio and all**, and
**King's Quest 2's AdLib synthesis and its shore-wave sample both play as expected** — two engines and
two synthesis paths, operator unhedged on both.

⚠️ **The service thread sleeps `audio_out_service_interval_us() / 2`, and the halving is still unmeasured
against the whole interval.** That function documents the *longest* a caller may go between services, and
the native path stays far under it by servicing from its render loop; half was chosen so one late wakeup
on a core with 20–40 ms of jitter cannot starve the stream. It ships and it sounds right, which is not the
same as being the right number.

**One residual, and it is not audible:** the pre-continuous tone path does not die when the games leave
it. `device_tools`, `hardware_config` and `hardware_test` still call `audio_tone()` without a bus, and
they are also the last callers of the interrupt-then-tone pair that `common/audio.h` warns against
(`native_apps/CLAUDE.md` → *Audio* owns that rule). Porting the three to the mix bus is the close-out;
nothing a player can hear depends on it, and `audio_tone()`'s own path is measured working.

---

### F19. The music beds — the files, their provenance and their delivery ⏳ nearly closed

⚠️ **This entry no longer owns the PLAYBACK path** (the streaming sample voice ships, F1 phase 8) nor the
*licence text* (`LICENSE.md` carries the one row for all 24 beds, its terms and the two limits on them).
It is the anchor the audio sources cite for "the bed", so it stays as one; what is left in it is small.

✅ **DONE and verified: format, git, deploy.** The beds are `native_apps/music/<stem><n>-mono.wav`, one set
per game — what `./check-bed-files.sh` counts, and it fails the build when one a game names is missing —
44100 / mono / 16-bit, byte-for-byte the mixer's internal format, so nothing resamples or downmixes at
runtime (`-ac 1` averages rather than sums, so it cannot clip on the way down). **Store mono, play stereo:**
the speaker sums L + R ([§3.4](SYSTEM_ANALYSIS.md#34-audio)), so a mono file duplicated at playback is
audibly identical to a stereo original at half the size and half the SD read. They are committed under
Git LFS (`native_apps/music/**`, verified to leave `sounds/fx_*.wav` at `filter: unspecified`), and
`native_apps/build-and-deploy.sh` installs them to `/opt/sound` on the online path (md5-gated: 8.8 MB is
not re-sent on every deploy) and the `--bundle` path (unconditional).
⚠️ **A clone without `git-lfs` leaves ~130-byte POINTER TEXT files in their place**, and a pointer deployed
to the device is refused in silence — so both deploy paths refuse one by its first line rather than let it
travel. `git lfs install && git lfs pull` is the fix. ⚠️ Run `git` from **Git Bash**; `git-lfs` is absent
from this WSL (`CLAUDE.md` → *Working from this host*).

✅ **The loop seam is MEASURED and there is no seam, so no crossfade is needed.** `officerunner1-mono.wav` ran as
a looping bed through three full wraps on `.188` at `LVL` 5/6, louder than the settled level, and the
operator heard *"nothing. Wonderful continuation"* across two deliberate attempts to catch it. ⚠️ That is a
property of **these two files**, not of a third one someone adds. The bed's own counter is `AudioWav.loops`.

✅ **The level is settled and the headroom is not.** Office Runner with the bed under it *"played well"*
with the effects audible over it, so the clean level is right for sustained music. ⚠️ **That session's
counters carry `clip=126`** — bed + effect past full scale on 126 samples of ~18.7 M, the first non-zero
`clip` measured here. Any future *"make it louder"* is therefore a CONTENT change, not a level one.

⚠️ **`starve` counts the FIRST service of a fresh stream**, where `in_flight` is legitimately 0, so one per
bed start is expected and is not an underrun. Chase it only when it climbs *during* playback.

✅ **The keep rule now states what it keeps.** `device-files/clean-rules.conf:189` keeps `/opt/sound`
wholesale and its reason is measured rather than guessed: **118 MB on `.188` 2026-09-01** — 24 beds, 14
effect WAVs and three vendor UI clips — against **222 MB free on a 931 MB rootfs (75 % used)**. It is the
largest single thing on the device, and wiping it only forces a re-send, since the beds install md5-gated.

⚠️ **That free-space figure is the one number here worth re-measuring before adding music.** A bed set
this size is ~13× what this entry claimed while nobody checked, and the headroom is finite.

⏳ **All that is left is bookkeeping:** this entry has no open work, but five places cite `F19` as the
anchor for "the bed" — `.gitignore:508`, `native_apps/build-and-deploy.sh:444` and `:565`,
`native_apps/CLAUDE.md:749`, `native_apps/common/audio_wav.h:17`. Each of those sentences must be rewritten
to carry its own reason before the entry is deleted, per the rule that an ID is not a durable reference.

### F2. Use the DSS overlay planes — open, **biggest performance win available**

**What compositing costs today, measured 2026-08-31 and *accepted* rather than filed as a fault:**
`samegame` tapping over a music bed runs at **45 % CPU** on the one 600 MHz core, ScummVM playing *Full
Throttle* at **12–13 % CPU / 5.4 % memory**. 45 % is what an overlay composite has to beat. ⚠️ **Not the
same quantity as the 32 % below** — a game mixing while it redraws, against O1–O12's endpoint.

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

### F9. Ship binaries as GitHub releases — **published and fetchable 2026-09-01**

`release.sh` at the repo root, `lib/rw-bundle.sh` (the bundle layout, sourced by every producer and
consumer) and `--bundle <dir>` on all four components are built and exercised. ✅ **`--tag` has now run
end to end**: tag `v1.0.0` on `andras-varro/roomwizard`, with its `targetCommitish` verified equal to the
`HEAD` the binaries were built from and its published asset digest verified against a local `sha256sum`.
`usb_host` is in a release like any other component — only its p1 power step is not, which is
[F15](#f15-usb-host-mode-through-commissioning--done-2026-08-08-confirmed-on-a-unit-2026-08-09).

✅ **And the fetch half is done and confirmed on a unit.** `lib/rw-release.sh` resolves, downloads and
sha256-verifies a published asset; `deploy-all.sh --from-release <tag|latest> <ip>`,
`commissioning/commission-offline.sh --release <tag|latest>` and `roomwizard.sh` items 3 and 6 are its
consumers. Confirmed 2026-09-01 against `v1.0.0` on `192.168.50.188`: 105 files installed, md5- and
`+x`-verified, launcher grid back on the panel. The measurements that shaped it are in `lib/CLAUDE.md`.

**What is still open here, and it is small:**

- ⚠️ **`release.sh` checks its publish preconditions only after the build and staging are done**, so a
  refusal costs a full rebuild before it prints. Correct, but wasteful — moving them ahead of the
  component loop needs `GIT_REV`/`GIT_DIRTY` computed earlier than the bundle metadata that consumes
  them.
- **Whether the `NOTICE` written offer is actually discharged has not been checked by anyone qualified to
  say so.** `release.sh` generates the per-release half and `LICENSE.md` says the two must agree; that is
  a bookkeeping guarantee, not a legal opinion. ⚠️ **Measure a dependency's licence *version* rather than
  carrying it forward**: this entry once said ScummVM was GPLv2+ and the tree is **GPL-3.0-or-later**.

**The host pulls the tarball; the device is untouched.** Nothing new runs on the device and there is no
CA-certificate problem to solve on a 2022 vendor image.

**[F10](#f10-single-pass-offline-commissioning--done-2026-08-05-confirmed-on-a-unit-2026-08-06) depends on
this one** — an offline commissioner has no toolchain to fall back on, so the release *is* its only source
of binaries. The obligations that only bite once artifacts are published are enumerated per artifact in
[`LICENSE.md`](LICENSE.md).

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
— but that path is now `common/audio_out` for every component (F20), so the reroute has one home rather
than two. A2DP's ~100–200 ms latency is fine for point-and-click and wrong for anything twitchy. **The
controller half is much more likely to land than the audio half; do not sell them as one feature.**

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
   loop, the summed voices and the pump pacing).
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
| 6 | `native_apps/CLAUDE.md` compress | 891 → **713** | ✅ 2026-08-16 |
| 7 | `MEMORY.md` index compression | 10.2 KB → **4.5 KB** | ✅ 2026-08-16 |

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

**Phase 6 landed at 713, and the ~690 it was priced at was mispriced in the same way phases 3 and 4 were —
by classification, in two rows only.** Every block of `native_apps/CLAUDE.md` was classified *duplicate / war
story / reference* before any editing, which caught most of it; the per-section arithmetic below summed to
**~203 lines out of 891** and delivered 178. The two rows it got wrong are Rendering and Audio, and the `keep`
figures in the table are the **measured floors**, not the survey's:

| section | keep | out | what compresses, and what does not |
|---|---|---|---|
| Build | 62 | 19 | the tinyalsa rules and the `asound.h` ABI check are pointers to `build-deps.sh`; the `Makefile` and `check-arm-safe` paragraphs duplicate root `CLAUDE.md` |
| The common library | 35 | 3 | the module table is reference with no other home — it compresses to nothing |
| App lifecycle | 57 | 5 | the `main()` skeleton is the canonical copy; only the Snake and `argv[0]` narration goes |
| Pixel format | 23 | 9 | the `fbset` warning and the bpp facts are in root `CLAUDE.md` and §3.2 |
| Hardware API | 20 | 3 | five numbered rules, all reference |
| Rendering | 113 | 13 | **the largest block**: the six war stories compress to their rules, but a *rule plus its measurement* is the floor — the survey's 55 counted the measurements as narration |
| Coordinates, portrait | 23 | 2 | rules only |
| Screen edges | 76 | 42 | the measured sweep table is duplicated in §3.3 *with an extra column*; the layout rules are not duplicated anywhere |
| Touch model | 80 | 14 | the fit, the stage diagram and the 13-row rules table are the only copy; four receipt tokens live here |
| Input | 88 | 26 | the uinput fact is in root `CLAUDE.md`, the MUSB facts in §3.6; the host-testability half is unique |
| 32-bit target | 10 | 1 | four lines duplicate root `CLAUDE.md` |
| Audio | 97 | 6 | the pump's rules and the `audio_gen` split live **nowhere else**; the survey's 24 assumed narration around them that a *measured* rule does not have |

**Reaching ~325 would mean deleting the module table, the lifecycle skeleton, the touch rules table and
the pump's rules** — the reference content this file exists to be. That is phase 4 part 2's mistake
with a different file, and the classification is what caught it before any editing rather than after.

⚠️ **Part 3 stopped at 713 rather than 690, and the residue is priced rather than spent — 6 lines in Rendering
and 10 in Audio.** Both sections are at their wrap floor: measured, Rendering averages 84 chars over 93
non-blank lines and Audio 88 over 85, and every line under 70 chars is a paragraph's wrap remainder, a table
header or the protected `main()`-loop skeleton. The next line out is therefore a measurement or a named
identifier, never narration; the full list of what it would cost is in the plan file. **A war story does not
compress to its rule; it compresses to its rule *plus the measurement that proves it*, which is where the
survey's 55 and 24 came from.** Fourth mispriced target in this cleanup and the second by classification —
the difference is that this one was found by measuring the floor instead of by deleting an identifier and
reverting it, which is what part 2 had to do.

⚠️ **Prose compression paid 8 lines and one structural change paid 5.** Inlining `gameover_needs_redraw()`'s
two-line fence into the sentence that already explained it lost nothing and freed a fence, a blank and two
short code lines. **When a fence holds one statement, it is narration in a code voice** — the mandate to keep
"the main-loop skeleton" is about the skeleton, not about every fence near it.

**Part 1 landed 891 → 855**, taking the two clean duplicate deletions and the four highest-value war
stories: the sweep table (16 → 5, now a pointer), the uinput narration (8 → 5), the two MUSB paragraphs
(15 → 8), samegame's per-game `else`-branch table, the derive-state table, the D-pad removal history and
the tinyalsa build rules. Two prose counts went with them, per the invariant — the deploy no longer claims
a number of executables or targets.

**Part 2 landed 855 → 732, and it is not finished: ~42 lines remain, all of it in two sections.**
Rendering is 126 against a keep of 107 and Audio 103 against 87; every other section is at or within four
lines of its surveyed figure (Build 66/62, common library 36/35, lifecycle 58/57, pixel format **23/23**,
hardware API 22/20, coordinates 25/23, screen edges 80/76, touch model 84/80, input 89/88, 32-bit 11/10).
⚠️ **Re-measure those rather than trusting them** — one command against the `keep` column above:

```bash
awk '/^## /{if(p!="")print p": "NR-s; p=$0; s=NR} END{print p": "NR-s+1}' native_apps/CLAUDE.md
```

⚠️ **The survey's "reference with no other home" was wrong for eight blocks, and that is why the first
half of part 2 stalled at ~55 % of every section's figure.** Compressing narration alone bought 87 lines
and then flattened out — each further line cost a named function or a measurement, which is the point at
which compression stops being free. What unstuck it was asking the *phase 5 classification question a
second time*, against `SYSTEM_ANALYSIS.md` rather than against the file itself: the bezel/viewport
preamble, the margin defaults, the per-unit inset digits and where to read them on the device, the
endpoint-clamp measurement, the stale-`vnc_client` misparse example, the bpp table and its
"16bpp bands every gradient" rationale, the `argv[0]`/`ps w` justification, the MUSB probe-time
enumeration fact and gotcha 5's whole-periods measurement are **all** stated in `SYSTEM_ANALYSIS.md` as
well — 40 lines, deleted to pointers in an afternoon after two hours of squeezing prose had bought less.
**Classify a block against every other document, not against the one you are editing**: "is this
reference?" and "is this reference *here*?" are different questions, and only the second one prices the
work.

⚠️ **And `SYSTEM_ANALYSIS.md` had already declared the split in four of those eight cases** — §3.3 ends
"The library rules, the deleted legacy-migration clamp and the sanity gate's actual criterion are
`native_apps/CLAUDE.md` → *Touch model*", §3.3's inset paragraph ends "Which rectangle a call site wants,
the cap on the inset and the drawing policy are `native_apps/CLAUDE.md` → *Screen edges*", and §3.2/§5.3
do the same. The duplication was therefore visible from the *destination* the whole time, in a sentence
naming this file. **When one document says "X lives over there", read X and check nothing came back.**

⚠️ **A drifted claim, found the same way and the sixth pointer-vs-destination hit of this cleanup.**
`native_apps/CLAUDE.md` said the bezel hides "~15 px top and bottom **on the reference unit**";
`SYSTEM_ANALYSIS.md` §3.2 says "**10–15 px** on the top and bottom edges only. **Measured on two
devices.**" The narrower figure attributed to one unit was the stale copy, and it is now a pointer rather
than a restatement.

⚠️ **Two of the eight pointers had to be checked against their destination before they were allowed to
stand, and a token grep could not do it.** The MUSB probe fact and gotcha 5's whole-periods measurement
were both confirmed present — §3.6 words it "a device is enumerated only if it is attached when the MUSB
driver probes" and §3.4 shouts `WHOLE PERIODS` — but **neither matches the phrasing this file used**, so
no group C row can express either move: one is a rewording, the other a case difference. Same family as
part 1's `omap2430_ops`. ⚠️ **And `ps w` is unusable as a receipt token for a third reason** — it is a
substring of "kee**ps w**hatever" and "swa**ps w**idth", both still in this file, so a row on it would
report `NOT MOVED` forever. Six rows were added (`15/15/0/0`, `red rect = visible`, `+19 px`,
`0 1020 3074 4095`, `bands every gradient`, `XRGB8888`), four more for the root-`CLAUDE.md` deletions
(`1,536,000`, `4-number`, `1000000L`, `injected successfully`), and the `Makefile`, `sdiv`, `ps w`, MUSB
and whole-periods verifications are greps recorded in the commit.

**What is left is a squeeze, not a survey**, and both sections' reference content is identified: Rendering
keeps the main-loop skeleton, the three-grounds table and the two `usleep` shapes; Audio keeps the nine
pump rules, the three-line conversion snippet and the `audio_gen` split. The narration around them is what
has to give.

The survey above is the remaining work list.

⚠️ **A duplicate deletion cannot always carry a receipt, and this is the other half of phase 5's lesson.**
The sweep table got one — `flat (saturated)` appears in exactly two files, so the row is exact. The MUSB
paragraphs could not: `omap2430_ops` and `a_wait_bcon` appear in **nine** files each, so any receipt on
them reports `NOT MOVED` forever. Where phase 5 said *a receipt needs a token unique to the moved block*,
the corollary is that **a repo-wide token means the verification is a grep recorded in the commit, not a
gate row** — `SYSTEM_ANALYSIS.md` §3.6 was confirmed to hold both facts before the paragraphs were cut.

**Phase 7 landed at 4.5 KB, not the ~3 KB priced.** All 22 memories are still indexed, one line each,
longest hook 189 characters against 1097 before — and ~1 KB of the file is the header, the four group
headings and the `_archive/` footer, which is why the last 1.5 KB is not there: below about 90 characters a
hook stops being able to distinguish 22 similarly-named memories, which is the one job the index has.
⚠️ **And the hook that priced this phase was itself a stale count** — it said hooks ran to 684 characters,
measured 1097, third-longest. A count in an index rots exactly like a count in prose.

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

1. **The audio subsystem is DONE where a player can hear it** — the mix bus, the continuous stream, the
   clip bank, the beds and the per-game sound sets all ship and are heard, and the ALSA port is not
   planned. What is left is the F20 tidy-up,
   none of which is audible.
2. **F2 (DSS overlays)** is the biggest performance win, also pure sysfs.
3. **C10 before the next panel check** — it converts a play session into one launch, and every future
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
cannot fire here. What stands between a non-developer and a working unit is now narrower than it was:
`README.md` leads with `roomwizard.sh` and documents installing from a published release. A tarball alone
still carries no boot-time loaders, and that is now a **settled decision rather than a gap** — bundles hold
built artifacts only, because the one consumer that installs device scripts runs from a clone and has
`device-files/` beside it either way (`lib/rw-bundle.sh` header).
[F13](#f13-commissioning-from-windows-without-wsl-and-from-macos--open-unsolved) is
recorded rather than planned, because the honest answer is a bootable image.

Everything else is genuinely unranked rather than deprioritised. **F6 (multi-touch) is the one to
consider promoting**: the register map is published, so it is far less speculative than its position
in this list suggests.
