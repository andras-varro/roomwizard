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
| 5 | ~~First boot of an offline-commissioned unit~~ — **done 2026-08-06.** Commissioned offline, booted first time, launcher grid, SSH, games, sound. Two findings came out of it: the backlight, since measured and **not** a defect ([`SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm`](SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm)), and [F14](#f14-decide-whether-the-boot-progress-bar-comes-back--open) | — | closed |

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

`commissioning/set-hostname.sh` and the avahi link have shipped, so a named unit answers to `<name>.local` from
Windows. Two pieces of residue:

1. **WSL cannot resolve `.local`.** Its `/etc/nsswitch.conf` is `hosts: files dns` — no mDNS module —
   so `./commissioning/provision.sh rw09.local` passes validation, reaches the SSH step and then fails to
   resolve. The fix is host-side and one package: `sudo apt install libnss-mdns` in WSL. **Until
   then the mDNS payoff applies to Windows-side `ssh` only, not to the build/deploy path.**
2. **The reboot path is unproven.** `S30avahi-daemon` is in place but the link was written directly
   rather than by a full `commissioning/provision.sh` run, so "it comes up on its own after a reboot" has not
   been observed.

Scope note for anyone extending this: the defect is confirmed **in the vendor image**, which a newly
commissioned unit inherits. Units already in service were found carrying a hardcoded *self-IP* line
instead — a different defect (a DHCP address goes stale as soon as the lease moves), equally worth
rewriting, but do not assume which variant a given unit has. Read it. A third variant is on record: a
non-loopback line mapping an RFC-1918 address to the name `null`, on a card whose `/etc/hostname` is
also `null`. `commissioning/set-hostname.sh` handles all three, because it keys on the name it reads rather than a
hardcoded one.

### D7b. `/etc/hosts` and `/etc/hostname` are regenerated on boot — closed 2026-08-08

Confirmed by reading the vendor script on both captured cards (`diff` reports them identical) and by
the second unit's own syslog. `/opt/sbin/networkmanager` rewrites `/etc/hosts`, `/etc/hostname`,
`/etc/resolv.conf` and `/etc/dhclient.conf`'s `send host-name` on **every boot** from
`/home/root/data/websign/net.*`. Mechanism, table and evidence:
[`SYSTEM_ANALYSIS.md#35-network-and-power`](SYSTEM_ANALYSIS.md#35-network-and-power). So
`commissioning/set-hostname.sh`'s offline half **is** undone by the first boot after commissioning, as suspected —
observed on a unit commissioned as `RW-Test`, which booted with `/etc/hostname` back to `null`.

**What RW09's "weak evidence" actually was.** `--remove` deletes `/home/root/data/websign` and the
deep clean's data file names it too, and both writers live *inside* `set_manual()`/`set_dhcp()` — so on
a cleaned unit neither branch runs and the name is never touched again. **The exposure window is exactly
"commissioned but not yet cleaned",** which is why nothing regressed on RW09 and why a card read
straight after commissioning shows the revert.

**All three fixes are in, and the third was closed by deciding not to build it.**

1. **Ordering — done, in [F10](#f10-single-pass-offline-commissioning--done-2026-08-05-confirmed-on-a-unit-2026-08-06).**
   `commissioning/commission-offline.sh` names the card and deletes both `websign` and the `rcS.d/S60networkmanager`
   link in the same offline pass, so no boot happens in between, and its verify pass fails if either
   survives. That removes the window rather than patching it. **Confirmed on real hardware 2026-08-06:**
   a unit commissioned as `rwtest` booted with its name intact and answered on the network.
2. **Closed 2026-08-08 by [C13](#c13-the-ssh-pass-and-the-offline-pass-share-one-clean--closed-2026-08-08), not implemented.** The proposed fix was to teach
   `commissioning/set-hostname.sh` to write `websign/net.hostname` too. It is unnecessary: the SSH flow
   kept the vendor stack only because `provision.sh`'s clean was opt-in, and that default is now
   flipped. A commissioning path that cleans **removes** this window instead of patching it — the same
   argument as item 1, now true of both paths.
3. **`/etc/dhclient.conf`'s `send host-name` — done.** `commissioning/set-hostname.sh` now owns it, in both the
   offline and the live path, with a negative control: it refuses to write a result that would not
   announce the new name. It is the third place the name is stored and the one a DHCP server, and
   therefore a router's device list, reads. A unit in service was announcing the shipped `RW09` months
   after being renamed, measured 2026-08-05 ([§3.5](SYSTEM_ANALYSIS.md#35-network-and-power)).

⚠️ **It is `networkmanager` that has to go, not `/etc/hosts` that has to be re-edited.** The
non-loopback `<leased-ip> <name>` line is written by the **vendor's** `/etc/dhclient-script`, which runs
only when `networkmanager` starts `dhclient` with `-sf /etc/dhclient-script`. The ifupdown path
(`S40networking` + `iface eth0 inet dhcp`) uses `/sbin/dhclient-script`, which contains no reference to
`/etc/hosts` at all. Measured on a unit in service: with the `rcS.d` link gone and `websign` deleted,
`/etc/hosts` and `/etc/hostname` have been untouched for five months while leases renew daily
([§3.5](SYSTEM_ANALYSIS.md#35-network-and-power)).

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
- **The publish step has never run**, though it is now reachable: `gh` 2.86.0 is installed in WSL from
  the release `.deb` (focal's apt has no `gh`, and the snap links against a glibc newer than 2.31).
  `origin` is `git@github.com-personal:…` — an SSH host alias — so whoever publishes needs `gh auth`
  for that account, and `gh` may need `--repo andras-varro/roomwizard` because it resolves the owner
  from the remote URL.
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
  be republished — the `/etc/hosts` mapping of D7, and the VNC password; a glob that swept up
  `*.conf` would publish exactly what those two exist to have removed. `release.sh` now greps the
  staged manifest for `*.conf`, `/etc/hosts`, `/etc/hostname` and the three device config basenames and
  refuses — the negative control for a future component that forgets, rather than a rule each component
  is trusted to remember.

**[F10](#f10-single-pass-offline-commissioning--done-2026-08-05-confirmed-on-a-unit-2026-08-06) depends on this one** — an
offline commissioner has no toolchain to fall back on, so the release *is* its only source of
binaries. Two obligations that only bite once artifacts are published: ScummVM is GPLv2+, so a binary
needs the corresponding-source offer, and `vnc_client`'s dependency licences need a pass. Both are now
written into the bundle's `NOTICE` by `release.sh`; whether that discharges the GPL offer has not been
checked by anyone qualified to say so.

---

### F10. Single-pass offline commissioning — **done 2026-08-05**, confirmed on a unit 2026-08-06

**`commissioning/commission-offline.sh` exists and does the whole job.** The card goes into a reader, the operator
answers two questions, the card goes back, and the device should boot working. What has **not**
happened is the only thing left: running it against a real card and booting a real unit.

**Why offline is not merely a convenience.** A unit whose `websign/net.mode` is `manual` takes a static
address and never sends a DHCP request, so it appears in no lease list and **the SSH phase can never
reach it** ([§3.5](SYSTEM_ANALYSIS.md#35-network-and-power)). Stock cards ship that way. Editing the
card is the only bootstrap for such a unit. It also **removes
[D7b](#d7b-etchosts-and-etchostname-are-regenerated-on-boot--closed-2026-08-08)'s window**
rather than patching it: the regenerator's input is deleted in the same pass that sets the name, so no
boot happens in between.

#### What is built

| Piece | Where |
|---|---|
| The offline commissioner | `commissioning/commission-offline.sh` — identify, mount, orchestrate, clean, install, verify, unmount |
| The keep/delete decisions | `device-files/clean-rules.conf` — 198 records, four tab-separated fields, a reason on every one |
| The parser, plan compiler and guarded `del()` | `lib/rw-clean.sh` |
| Files installed verbatim | `device-files/{audio-enable,time-sync,99-security.conf}` — no longer heredocs inside `commissioning/provision.sh` |
| The bundle it installs | `release.sh` → `lib/rw-bundle.sh` ([F9](#f9-ship-binaries-as-github-releases--partly-built-2026-08-05-open)) |
| Card and mount identification | `lib/rw-identify.sh`, by content and by **position**, never UUID |
| Regressions | `tests/rw_clean_test.sh` (116 cases), `tests/commission_offline_test.sh` (21), `tests/make-fake-card.sh` |

**`commissioning/provision.sh --deep-clean` reads the same data file**, so the live and offline cleans cannot
drift. Each keeps its own executor: `/` is the correct prefix on a device and a refused one offline.
The host compiles the rules into a line-based plan and ships that, so the device needs neither `bash`
nor a copy of the rules.

#### The safety model

- **One `del()` that refuses an empty or `/` base**, and every deletion — including every one a sweep
  decides on — goes through it. Unprefixed, these rules resolve to the dev host's own `/etc`, `/opt`
  and `/usr/lib`. Measured against a guardless copy: 11 of the 15 group-A cases fail and four of them
  resolve to this host's `/etc/shadow`.
- `--dry-run` prints every fully-resolved absolute path before anything is unlinked, and a case asserts
  that every printed path is absolute and under the base.
- **The host's root disk is resolved and excluded** (`rw_is_host_root_disk`), checked before mounting
  rather than after. Every disk on this host reports `removable = 0`, so a "removable only" gate
  rejects everything.
- **p1 is unreachable.** It is absent from `RW_PART_ROLES` and `tests/rw_identify_test.sh` asserts its
  absence, so no caller can reach `mlo`, `u-boot.bin`, `ctrlblock.bin` or `uImage-system` at all. That
  is what keeps a power cycle a free undo.
- ⚠️ **`rc0.d` and `rc6.d` are shutdown, not startup.** `rw_clean_validate` **rejects a rules file that
  names them**, so they are unreachable by construction rather than merely unvisited. They carry
  `umountfs`, `sendsigs` and `save-rtc.sh`.

#### The cleanup criterion is "what runs", not "what it costs"

The risk being managed is **an unknown vendor service on a unit nobody has inspected**. Disk space is
explicitly not a motive: p6 has 474 MB free before anything is deleted
([§4.2](SYSTEM_ANALYSIS.md#42-partitions)). That criterion decides the shape:

- **Whitelist everything that can start** — `rcS.d`, `rc2.d`–`rc5.d`, `/opt`,
  `/home/root/{data,log,backup}`. Keep a named few, sweep the rest, so an unrecognised vendor service
  is removed **by construction**. The keep-lists are
  [§5.2](SYSTEM_ANALYSIS.md#52-as-we-run-it--game-mode)'s measured table.
- **Blacklist inside the base OS**, and only by *named stack*: `browser`, `java`, `snmp`, `mail`,
  `extras`. **Never `/lib`, `/etc`, `/bin`, `/sbin`.** A case asserts libc survives.
- **Keep inert vendor artifacts** — `/opt/{sbin,pv02,sound}`, `perl5/`, the ZigBee tooling. Once
  nothing starts them they cost nothing, and reading them is how
  [§3.5](SYSTEM_ANALYSIS.md#35-network-and-power) was established.
- **The upgrade payload is a `factory` group, deleted by default.** Reversed 2026-08-06: cleaning a
  unit of its vendor software is a decision, and the payload restores a stack whose start-up mechanism
  the same clean removes. `--keep-factory` opts out. `factory/uImage-system-original` — the 5 MB
  fallback kernel — is kept either way.
- ⚠️ **`--keep-<group>` protects that group's paths from every sweep, not just from its own delete
  lines.** Without that rule the `/opt` whitelist would remove `/opt/openjre-8` anyway and the flag
  would do nothing and say nothing. What it does *not* do is re-enable a boot link: the rc\*.d
  whitelist is the mechanism, and punching a per-stack hole in it would defeat it.

#### Verification the tool performs

md5 of every **installed** file against the bundle's `.md5`; `+x` asserted (real ext4 honours it,
unlike `/mnt/c`, so this is a measurement offline and could not be one on the dev host);
`native_apps/check-arm-safe.sh` over the **downloaded** binaries, refusing loudly and naming the
unchecked count if `arm-linux-gnueabihf-objdump` is absent rather than reporting a pass over zero
artifacts; every `.app`'s `exec=` and `icon=` installed and executable; `default-app` names one of
them; `dash -n` every `/bin/sh` script written, plus an explicit CRLF check on the shebang.

⚠️ **`dash -n` catches parse errors and CRLF, not bashisms.** `[[ -n "$x" ]]` parses fine — `dash`
reads `[[` as a command name — so it passes and then fails at boot with `[[: not found`. Catching that
needs shellcheck ([C7](#c7-run-shellcheck--open)).

Each of those checks has a negative control in `tests/commission_offline_test.sh`: a corrupted staged
file, an installer that skips `chmod`, a manifest naming an absent binary, a `default-app` that names
nothing, a parse error, a CRLF shebang, a bundle with no ELF at all, a missing `objdump`, and the four
mounts in the wrong order. **The one thing not controlled is a binary that really contains an `sdiv`** —
this host's compiler will not emit one for Cortex-A8, so both ways the gate can lie *by omission* are
controlled instead.

#### Confirmed on hardware 2026-08-06

**A unit was commissioned offline and booted working on the first try.** Card from an uncommissioned
unit, one pass on a Kubuntu host, card back, one boot: launcher grid, SSH reachable by name, games
playable, sound. The unit is `rwtest` at `192.168.50.225`, and it has since survived a reboot. Every
verification in the tool passed on the real card — md5 across 46 installed files, `+x` on 22 (a real
measurement, impossible on `/mnt/c`), all 9 `.app` manifests resolving, `default-app` → `app_launcher`,
`dash -n` clean, boot links non-dangling, `websign` gone, host name consistent across three files.
Run with `--delete-factory`, so the 472 MB on-card restore payload was gone by choice — that is now
the default and the flag is redundant.

ScummVM and `vnc_client` are absent **because the bundle was `native_apps` only** — not a failure.
Deploy them over SSH, or restage a three-component bundle.

#### What is left

1. **`COMMISSIONING.md`'s ORDERING — the prose is done, the emphasis is not.** `b88933a` rewrote the
   doc as a description rather than a plan, so what is left is smaller and more specific: it still
   **leads with the three-phase SSH flow** while `commission-offline.sh` is the verified path for
   *delivery*. Decide whether the offline pass goes first. Two things to fix while there, both found
   2026-08-07: the line saying `card-prep.sh`'s rename "is recorded in C12" is stale (the rename
   shipped in `f9f895a`, after the doc rewrite), and `NEXT_STEPS` step 4 still tells the operator to
   run `ssh-copy-id` by hand — the scripts now offer it (`git log --grep=F16`).
2. **Unit B — anything more aggressive, one increment per boot.** A failed boot yields no diagnostics
   (no serial console, [§3.12](SYSTEM_ANALYSIS.md#312-serial-ports)); the only post-mortem is mounting
   p3 offline and reading `messages`, which only helps if it got as far as syslog. Still the reason to
   stop unless something specific is being chased.

#### Scope boundaries

- **Still needs the device:** touch calibration only — per-unit, per-panel, and the wizard exists
  (Device Tools → Display → `CALIBRATE TOUCH`). One boot remains; this removes two of three plus the
  IP hunt.
- **Does not replace the SSH path.** `commissioning/provision.sh` and `deploy-all.sh` stay as the verified
  development loop; the offline tool is for *delivery*.
- **Distribution:** binaries only, never the vendor image (a third party's copyright) and never device
  configs (F9's caveat — `/etc/hosts` and the VNC password). ScummVM is GPLv2+, so a published binary
  needs the corresponding-source offer; `vnc_client`'s dependency licences need a pass.
- **`usb_host` is excluded from every bundle** — it patches `uImage-system` on p1.

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

### F15. USB host mode through commissioning — driver, p1 patch and tests DONE 2026-08-08, docs left

**Symptom that opened this:** USB does not work on the offline-commissioned unit (`rwtest`,
192.168.50.225).

⚠️ **`usb_host` is three independent mechanisms, and only one of them touches p1. Do not treat the p1 rule
as a blocker on USB as a whole** — it blocks exactly one power-budget refinement:

| Mechanism | Delivers | Lives on | p1? |
|---|---|---|---|
| `/dev/mem` patch of `omap2430_ops.dma_init`/`.dma_exit` + MUSB rebind | **USB host mode itself** | nothing on disk; re-applied each boot by `/etc/init.d/usb-host` | **no** |
| `xpad.ko` / `joydev.ko` / `ff-memless.ko`, force-loaded | **controller as `/dev/input/event*`** | `/lib/modules/4.14.52/extra` (p6) | **no** |
| DTB `power` `0x32`→`0xfa` in `uImage-system` | 100 mA→**500 mA** budget, i.e. a pad with no powered hub | p1 | **yes** |

So "the USB driver" is the first two, and they are eight files entirely on p6 — ordinary
`provision-rules.conf` `install` + `link` records plus four bundled build artifacts, needing no new
bundle verb. **The true statement is only that no bundle can deliver the 500 mA budget.**

**Asked 2026-08-08: if we are gutting the vendor stack anyway, what is p1 being protected from?** Not
the vendor software — that is all on p6/p2/p3/p5, and every bit of it is recoverable over SSH from a
running unit. p1 holds only the boot chain, and the asymmetry is the *recovery path*: break p6 and you
still have SSH, break p1 and there is no SSH to fix it from. ⚠️ **But "brick" overstates it** — the boot
media is a removable SD card, and a full-card `dd` restore is a demonstrated routine here, so the real
cost is a card pull, not a dead unit. What the rule actually protects is *unattended delivery*: a
wall-mounted unit that fails to boot needs someone standing at it with a card reader.

**Why the 500 mA value cannot be a boot-time script, when the driver fix can.** The obvious question —
"why not ship it as a launch-time target like everything else?" — has a source-backed answer, read out of
`usb_host/linux-4.14.52/` on 2026-08-08:

- `drivers/usb/musb/omap2430.c:452` — `of_property_read_u32(np, "power", (u32 *)&pdata->power)` reads the
  value from the device tree **at driver probe**.
- `drivers/usb/musb/musb_core.c:2369`/`:2381` — passes it on as `musb_host_setup(musb, plat->power)`.
- `drivers/usb/musb/musb_host.c:2797` — `hcd->power_budget = 2 * (power_budget ? : 250);`

The device tree is appended to the kernel image *inside* `uImage-system`, and the driver reads it before
any init script exists. There is no file on the normal filesystem to edit. **That one number is the sole
reason p1 enters the picture** — not the driver. The DMA/PIO fix persists as a boot script only because it
patches a *static* kernel struct, which stays patched until reboot.

Line 2797 also records something worth keeping: the `? : 250` fallback means an **absent or zero**
property would already yield 500 mA. The vendor deliberately set 100 mA, overriding a kernel default that
was what we wanted. It is a vendor choice, not a hardware limit.

**And `bootcmd` cannot be repointed**, because U-Boot has no `saveenv`
([§4](SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery)). The repo's escape hatch for kernel work — "stage it
under a new name, leave `uImage-system` alone" — only helps if you can *boot* the new name, which means
interrupting U-Boot over serial every time. So patching `uImage-system` in place is the only route to a
unit that comes up with 500 mA by itself.

**Both measurements this decision rested on are answered.**

1. ✅ **The vendor `uImage-system` is byte-identical across units**, measured 2026-08-08:
   `edc637ac14f90e0187b1ed65ffedf6d7` on p1 of *both* full-card captures, in *both* units' p5
   factory-restore payloads, and in RW09's own copy — five sources, three units. Nothing generates it
   per-unit, unlike the filesystem UUIDs. So an md5-gated patch is sound.
   `uImage-system-patched` is `a1fd1af8da18c430a34b24762aa16dab` and differs in **exactly 9 bytes**: the
   uImage header CRC (offsets 4–7), the data CRC (24–27), and one value byte at `0x4FA2CF`.
2. ✅ **The card needs the case popped open** — feasible, but it takes experience and an inexperienced
   attempt can break the case. Answered by the user 2026-08-08. Their call: this is a README note under a
   no-warranty heading, **not** a blocker.

⚠️ **Never ship the vendor kernel binary.** `usb_host/.gitignore` already calls `uImage-system*`
"Copyrighted device-specific files", and this repo is meant to be published. So the installer **derives**
the patch from the device's own file with the committed `patch_dtb.py` rather than shipping a 5.2 MB
Steelcase binary. The md5 gate on input plus the md5 assert on output makes that a complete check, and it
keeps the bundle 5.2 MB smaller. `release.sh` gains a **copyright refusal** for any `uImage-system`
manifest entry — the negative control for this rule, parallel to its existing config refusal.

⚠️ **Bundling `xpad.ko` is GPL-2.0 redistribution** and needs a written source offer — vanilla kernel.org
4.14.52 plus the committed `build-xpad-module.sh`. Cheap to satisfy, but it has to be written down, which
is why a **`LICENSE.md` (MIT for our own code)** is part of this work. MIT is compatible with both
GPL-2.0 and GPL-3.0, which matters because the repo ships GPL-2.0 module binaries *and* links into
GPL-3.0+ ScummVM. Apache-2.0 would conflict with the former.

✅ **DECIDED 2026-08-08, with the costs stated: the p1 power patch ships ON by default**, `--no-usb-power`
as the opt-out, `--no-usb` (free from the `provision-rules.conf` group mechanism) implying it. The
accepted cost is that **a power cycle is no longer a free undo** on a default-commissioned unit;
`uImage-system.vendor` on p1 is the in-place remedy and a card pull is the fallback. Do not relitigate,
and do not re-raise the card-access question as a risk.

**Built 2026-08-08 — the p6 driver and the p1 patch mechanism. Two call sites left.**

✅ **Done, and verified on this host:**

- `device-files/enable-usb-host.sh`, `device-files/usb-host`, `device-files/xpad-modules` — the three
  device scripts, `git mv`'d out of `usb_host/` and named as they are *deployed*, the
  `roomwizard-app`/`S99roomwizard-app` precedent. `.gitattributes` already pins `device-files/**` to
  `eol=lf`, which these `/bin/sh` scripts need.
- `device-files/provision-rules.conf` — a new **`usb` group**: three `install` (0755), two `link`
  (`S89xpad-modules` → `../init.d/xpad-modules`, `S90usb-host` → `../init.d/usb-host`, relative
  sources), and one `unlink` of the module loader's former name `/etc/init.d/S89xpad-modules` —
  nothing sweeps `/etc/init.d`, so without it the old copy would sit there forever.
  `rw_provision_check_keeps` passes with no `clean-rules.conf` edit, as predicted.
- `lib/rw-provision.sh` — the `usb` group in all three group lists, plus
  **`rw_provision_plan_component FILE GROUP`**: a plan of ONE optional group, for a component script
  installing its own payload standalone. A separate entry point rather than a flag, so a
  commissioning path cannot reach a `base`-less plan by mistyping a group list; it refuses `base`.
  The ordering awk moved into `_rw_provision_emit` so both entry points share it.
- `usb_host/uimage.py` — the one implementation of "read a uImage, find the MUSB power property".
  ⚠️ The DTB is **found**, not asserted at `0x4eb788`: that offset is tried first as a hint, and a
  candidate must carry a valid FDT header **and** yield `power` inside a `usb_otg_hs` node, because a
  compressed kernel payload can contain `d00dfeed` by accident. Asserting the offset is what made a
  small synthetic test fixture impossible.
- `usb_host/patch_dtb.py` — rewritten: `<in> <out>` on argv (B19's cwd hazard), the input's own CRCs
  checked *before* patching, and a refusal rather than a rewrite on an unexpected power value.
  Measured: it reproduces `a1fd1af8da18c430a34b24762aa16dab` byte-for-byte from the vendor image, and
  `cmp -l` gives exactly the 9 recorded bytes — `0x4`–`0x7`, `0x18`–`0x1B`, `0x4FA2CF`.
- `usb_host/verify_uimage.py` — magic, both CRCs, the header's own size field, and the power value;
  `--expect-power` turns the reading into an assertion. Pure Python, no `mkimage`/`dtc`.
- `lib/rw-usbpower.sh` — the gate/backup/patch/verify/rollback sequence, once, with the transport
  behind six primitives (`RWUP_XPORT` = `local` | `ssh`, `$RW_SSH`/`$RW_SCP` overridable for tests).
  All four outcomes measured against a synthetic p1 carrying the real vendor image: dry run, patch,
  idempotent re-run, and refusal on a third md5.
- `lib/rw-identify.sh` — `RW_BOOT_PARTITION`, `rw_card_boot_partition`, `rw_is_boot_tree`,
  `rw_mount_boot`, `rw_umount_boot`. `RW_PART_ROLES` is unchanged and
  `tests/rw_identify_test.sh`'s p1-absence assertion still passes untouched.
- `usb_host/build-and-deploy.sh` — rewritten. `--bundle <dir>` stages the four artifacts from one
  declared `USB_ARTIFACTS` table; `--no-usb-power` skips p1; the three scripts and two links now come
  from `rw_provision_plan_component` run through the *same* generated online executor
  `commissioning/provision.sh` uses, so its own `scp`/`chmod`/`ln -sf` sequence is gone. The ARM gate
  runs on all four artifacts before either path, and ⚠️ **exit 2 is fatal here** unlike in
  `commission-offline.sh`: every `usb_host` artifact is unstripped by construction, so "could not
  judge" means the build changed. Measured: `checked=4 unverified=0 bad=0`.
- `release.sh` — `usb_host` added to `RELEASE_COMPONENTS`, the "excluded, it patches p1" header
  rewritten, a **vendor-firmware refusal** beside the config one (`uImage*`, `mlo`, `u-boot*`,
  `ctrlblock*`, matched on the basename because p1 is not a bundle path at all), and the `NOTICE`
  gained the **GPL-2.0 written source offer** for the three `.ko`s.
- Deleted `usb_host/find_dtb.py` and `usb_host/verify_patch.sh`. The first is a second, unguarded
  copy of the `d00dfeed` scan — exactly the false-positive `uimage.py` now refuses; the second
  shelled out to `mkimage`/`dtc` from a hardcoded `/mnt/c/work/roomwizard/usb_host` and could not run
  on this host at all.
- Host suites after the change: `rw_provision_test` 94/94, `rw_identify_test` 37/37,
  `rw_clean_test` 148/148, `c11_plan_diff` clean on both plans. Group E picks the new `usb` records up
  automatically, which is the check that the two executors agree about them.

✅ **Wired into both commissioning entry points 2026-08-08 — the p1 patch is ON by default.**

- `commissioning/provision.sh` — a new step 5, `run_usbpower`, calling `rw_usbpower_apply_ssh`.
  `--no-usb-power` opts out, `--no-usb` implies it (read off `NO_PROV_GROUPS`), a declined backup
  question skips it, and a missing `python3` degrades to a named skip rather than an abort. The verdict
  is a `P1_STATE` string reported in a closing "This run:" block, and the run's own reboot is what makes
  the new budget live — stated there, so nothing further is needed.
- `commissioning/commission-offline.sh` — a new **phase 6** between install and verify:
  `rw_mount_boot` → `rw_usbpower_apply_offline` → the caller owns the unmount.
  ⚠️ **`rw_umount_boot` is reachable from `cleanup_and_exit`**, guarded by its own `BOOT_MOUNTED`
  variable rather than `MOUNTED_BASE`, and ordered *before* `rw_umount_card` because
  `rmdir "$MOUNTED_BASE"` fails while `boot/` is still there. Phase 7 (verify) then **re-reads p1 with
  `md5sum`**, not through the tool that wrote it, and asserts both the patched image and the vendor
  backup; a `FAILED` p1 is a `vfail`, so the card cannot be declared bootable.
  ⚠️ **`--base` skips p1 deliberately** — that mode is handed four mount points and no disk, and
  inferring a device node from a mount point is exactly what `lib/rw-identify.sh` exists to refuse.
- Both usages, both phase-0/consent texts and both closing summaries were rewritten. The offline
  closing block no longer says "no bundle can install it: USB HOST MODE" — that sentence was the last
  statement of the old belief.
- ⚠️ **The `--no-usb-power` case arm must precede the `--no-*` glob** in both scripts. `case` takes the
  first match, so an arm placed after it is never reached and the operator gets
  `Unknown provision group: usb-power`. `--no-clean` was already shadowing for the same reason.

✅ **[C13](#c13-the-ssh-pass-and-the-offline-pass-share-one-clean--closed-2026-08-08) implemented in the
same pass**, since both changes rewrite the same defaults, `--help` and closing summary:

- `commissioning/provision.sh` **cleans by default** (`CLEAN_MODE=deep`), `--no-clean` opts out,
  `--remove` narrows to the named stacks, `--deep-clean` names the default explicitly.
- **One consent gate, `ask_consent`, covering both irreversible steps** — the clean and the p1 write —
  asked once, before the first write, and *after* `--status`/`--hostname` have had their chance to exit
  so neither ever sees it. On a TTY a declined answer skips both and says so; the provision and the
  reboot still happen, because both are repeatable.
- ⚠️ **The non-TTY branch is a loud banner, and the loudness is the safety property.** It names what is
  being done with nobody having answered, per selected step. The defect it replaces: an unguarded
  `read` whose EOF left the answer empty, cancelled the clean and returned 0 — so a scripted run
  silently did not clean while the operator believed the default did.
- `--dry-run` was lifted out of positional `$3` into the pre-parse loop, so a bare
  `provision.sh <ip> --dry-run` now previews the default clean *and* the p1 write. It is rejected with
  `--status`/`--hostname`, which write nothing — silently accepting it there would preview a clean
  nobody asked for, because the dry-run branch runs before both of those.

✅ **`tests/rw_usbpower_test.sh` — 94 cases, host-only, no card, no root** (needs `python3`, so WSL).
Groups A–K: the shipped md5 constants, `rw_usbpower_classify`'s three outcomes, `verify_uimage.py`
against five one-check-each sabotages, `patch_dtb.py`'s four refusals plus the 9-byte assertion, the
argument and transport guards, the md5 gate's three outcomes, the dry run, backup-before-write,
**rollback and failed-rollback**, and group J.

- New `tests/make-fake-uimage.py` builds the fixture: a ~386-byte uImage carrying a real FDT with one
  `usb_otg_hs` node. ⚠️ **Synthetic only, and it has to be** — the vendor kernel is gitignored and must
  never be committed. What makes it usable is `uimage.py` *finding* the DTB by magic. Each sabotage is a
  declared flag, and CRCs are recomputed before a structural sabotage and after a CRC one so that every
  flag fails **exactly one** check — a fixture that trips two is the negative control for neither.
- ⚠️ **Groups F–K override `RW_UIMAGE_VENDOR_MD5`/`_PATCHED_MD5` with the fixture's own**, because those
  constants *are* the identity of one firmware and no synthetic image can match them. The sequence is
  what is under test; group A asserts the shipped values separately. The expected patched md5 is
  computed with the **real** `patch_dtb.py`, so a sabotaged copy is caught rather than accommodated.
- **Rollback is reached by injecting a fault into one private transport primitive** (`_rwup_mv`
  redefined inside a subshell). The local transport is a `cp` and a `cp` does not fail on demand, so
  there is no other way to reach step 10 — and what runs is the real rollback code, not a restatement
  of it. The failed-rollback case pre-places a correct backup so that `_rwup_cp` is reached only by the
  rollback and not by step 6.
- **Group J is this file's stand-in for `rw_provision_test.sh` group E** — the p1 step is the one thing
  that comparison cannot cover, so the same sequence runs over both transports and must leave
  byte-identical results *and* identical prose. The ssh half runs against a directory on this host
  through the `$RW_SSH`/`$RW_SCP` stubs the library already documents; every far-side operation really
  happens.
- `RW_USBPOWER_LIB` points the suite at a copy of the tree. Because `rw_usbpower_tool` resolves
  `usb_host/` from the library's own `BASH_SOURCE`, that one variable also redirects all three Python
  tools — so a sabotage of any of the four is measurable through one door.

✅ **`tests/measure_usbpower_sabotage.sh` — five sabotages, all five caught, 9 s.** The gap below is
closed: `rw_usbpower_test.sh` has now been seen failing, and it fails the *right* cases.

| Sabotage | Failed | Owned by |
|---|---|---|
| `rw_usbpower_classify` returns `vendor` for an unknown md5 | 7 | B5–B8, F11, G6, J7 |
| step 9's re-read replaced by the constant it compares against | 8 | all of group I |
| step 6's backup verification deleted | 3 | H1–H3 |
| `verify_uimage.py` always exits 0 | 7 | C6–C11, C13 |
| `uimage_fix_crcs`' CRC order swapped | 23 | D4, D6, D7 + every sequence case |

- **It stages FIVE FILES, not two directories** — `lib/rw-usbpower.sh`, `lib/rw-identify.sh` and the
  three `usb_host/*.py`, which is the entire set the suite reaches through `RW_USBPOWER_LIB`. That is
  what took the run from "abandoned on a 300 s timeout, twice" to 9 s. ⚠️ **The baseline case is the
  assertion that the list is complete**: a sixth dependency makes the baseline fail here rather than
  letting every sabotage below quietly measure the shipped copy.
- ⚠️ **Two sed patterns need care, and both are recorded in the file.** Step 9's re-read is
  character-identical to the one inside the rollback branch and is distinguished only by its 4-space
  indent — patching both would make the rollback claim success too, i.e. two defects and a negative
  control for neither. And step 6's backup guard is character-identical to step 3's re-check of the
  pulled copy, *same indent included*, so that sabotage is confined by an address range and a separate
  assert requires exactly one copy of the guard to survive.
- **All three of the harness's own refusals were driven deliberately** — a rotted sed pattern, an
  applied-grep pointing at text the sed does not produce, and the address range dropped — because an
  applied-assert that has only ever been seen passing is the same defect one level up. A fourth was
  found by accident and is the argument for keeping the minimums: `echo VENDOR` for `echo vendor`
  applies and parses, but the library's own `case` falls through to `*)` and refuses anyway, so the
  sabotage weakens to 4 failures and only the `>= 7` minimum catches it.
- ⚠️ **A copy of the harness under `/tmp` measures nothing** and says so: it resolves the repo from its
  own location, so `/tmp/x.sh` looks for the suite at `//tests/`. Put test copies under `tests/`.
- The fixture builder is unaffected by sabotage 5 **by construction**: `tests/make-fake-uimage.py`
  imports `uimage_fix_crcs` from the *real* repo's `usb_host` (its `sys.path` is relative to its own
  file), so a broken CRC order cannot be baked into the input as well as the output.

⬜ **Left, in order:**

1. **`tests/commission_offline_test.sh`** — ⚠️ **add `lib/rw-usbpower.sh` to the fixture repo's copy
   list** at `tests/commission_offline_test.sh:67-71`. The suite passes today only because `--base`
   skips the p1 phase *before* the lazy `.` of that file is reached; a future `--disk` case would fail to
   source it. Then add the p1 cases below. `ANSWERS` needs no change — no new prompt was added, phase 0's
   existing question was only reworded.

   **Measured 2026-08-08 so the next pass does not re-derive it** (all host-only; the suite itself still
   needs root and has NOT been run):

   - The three skip strings are exact, from `commissioning/commission-offline.sh:689-710`:
     `skipped (--no-usb-power)`, `skipped (--no-usb)`,
     `skipped (--base: no disk given, so p1 cannot be located)`. Each is printed twice — once by phase 6
     and once by the closing `USB power budget:` summary.
   - ⚠️ **All three are reachable with `--base`, which is what every case in the file already passes**,
     because `DO_USB_POWER -eq 0` is tested *before* `-z "$MOUNTED_BASE"`. So `--base` alone gives the
     third string and is already exercised by case 1; the other two need only the flag added.
   - **`--no-usb-power` and `--no-usb` are both accepted** — verified by running the real script with
     `--bundle /nope` and getting `No such bundle` rather than `Unknown provision group: usb-power`,
     which is the `--no-*` glob-shadowing trap and needs neither root nor a bundle to check.
   - **`--no-usb` cannot break another verify check**: the boot-link check at `:884` is a hardcoded list
     of `S28time-sync`, `S29audio-enable`, `S99roomwizard-app` and does not include the `usb` group's
     links, so excluding the group leaves it unaffected.
   - ⚠️ **Which is itself a small gap worth a line in the same pass**: on a *default* offline run the
     `usb` group installs `S89xpad-modules` and `S90usb-host`, and that loop never checks they resolve.
     A dangling `rc5.d` link is skipped in silence at boot, so it is exactly the class the check exists
     for. Either add them (they are optional, so the loop must skip them when `usb` is excluded) or say
     in the code why not.
   - **The p1 *patching* path is not reachable from this file at all** — it needs `--disk`, i.e. a real
     card or a loopback image with a vfat p1. `tests/rw_usbpower_test.sh` plus
     `tests/measure_usbpower_sabotage.sh` own the sequence; what this file can own is which mode reaches
     it. Say so, rather than leaving a reader to assume the write is covered here.
   - **Worth adding beside the copy-list fix, as its negative control**: a case that greps the *copied*
     `commission-offline.sh` for every `. "$REPO_ROOT/…"` and asserts each file is in the fixture tree.
     All five are on one grep (`lib/rw-{identify,clean,provision,bundle,usbpower}.sh`). That is what
     would have caught this omission, and the next one, instead of a `--disk` run discovering it.
2. **`LICENSE.md`** — MIT, plus the third-party enumeration (LibVNCServer GPL-2.0 under
   `vnc_client/deps/`, the ScummVM backend compiled into GPL-3.0+ ScummVM). The GPL-2.0 **written
   source offer** for the three `.ko`s is already in `release.sh`'s `NOTICE`; `LICENSE.md` is the
   repo-level half.
3. **`COMMISSIONING.md`, `README.md`, `usb_host/README.md`** — the new defaults and flags.
   `COMMISSIONING.md` step 5 still says the SSH pass deletes nothing; `usb_host/README.md`'s File
   Reference and its Steps 4/6 still name the pre-move paths. `CLAUDE.md` is done.

⚠️ **Not attempted and not needed: `usb_host` reaches `deploy-all.sh`'s bundle path by no new
mechanism.** `--from-bundle` installs whatever the manifests name, so the four artifacts arrive there
for free — but a bundle install alone does **not** patch p1 and does **not** install the three device
scripts. Those come from `commissioning/provision.sh` or `commissioning/commission-offline.sh`.

**One scoped experiment would retire the p1 path entirely, and is worth trying first.** Patch the
**in-RAM** copy of the `usb_otg_hs` `power` property via `/dev/mem`: verify it reads `0x00000032`, write
`0x000000fa`, rebind, confirm 500 mA. That is the *same* mechanism as the existing `omap2430_ops` patch
aimed at a different target, and it would make the 500 mA fix an ordinary boot script.
⚠️ **This is not the sysfs override already recorded as failed** in `usb_host/README.md`'s *Failed
Approaches* — sysfs exposes no writable `power_budget`; `/dev/mem` against the unflattened tree was never
tried (`git log -p --follow -- usb_host/enable-usb-host.sh` shows no power-related code, ever). The open
risk is address stability: `omap2430_ops` is a *static* symbol at a fixed address, which is why the
existing patch can self-verify against `quirks == 0x00000004`, whereas the unflattened DT is early-boot
allocated. One SSH session, no panel time.

---

### F17. Bluetooth peripherals, and whether USB DMA is reachable — open, measured 2026-08-08

**The want:** a wireless game controller and a headset or speaker for ScummVM. The unit is PoE-wired, the
Xbox pad is wired, and the integrated speaker is poor
([§3.4](SYSTEM_ANALYSIS.md#34-audio)) — so every current option is a cable, and the one that carries sound
is the worst-sounding one.

⚠️ **DMA and Bluetooth are independent, and DMA is not what unblocks Bluetooth.** BT is
bandwidth-trivial: A2DP is tens of KB/s and a controller is a few hundred bytes/s, which PIO handles
easily. Do not treat "get DMA working" as a prerequisite.

**Bluetooth needs a USB dongle — there is no radio on the board.** No WiFi and no Bluetooth is fitted
([§2.4](SYSTEM_ANALYSIS.md#24-unpopulated-and-expansion)). The only radio site is `J5`/`J6`, an **XBee
802.15.4** socket, empty in all three units, on UART3 which is `disabled` in the device tree — XBee is
Zigbee and cannot host Bluetooth. And there is no second USB port and no footprint for one
([§3.6](SYSTEM_ANALYSIS.md#36-usb)), so the dongle occupies the single connector.

**The kernel side is the `joydev` precedent again, and looks feasible.** `# CONFIG_BT is not set`, exactly
as `CONFIG_INPUT_JOYDEV` was before [F15](#f15-usb-host-mode-through-commissioning--driver-p1-patch-and-tests-done-2026-08-08-docs-left)'s
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
— this overlaps [F1](#f1-port-audio-from-oss-to-alsa--open-highest-user-visible-payoff). A2DP's
~100–200 ms latency is fine for point-and-click and wrong for anything twitchy. **The controller half is
much more likely to land than the audio half; do not sell them as one feature.**

**Can we get USB DMA?** Probably, but it is research with a worse failure mode than today's.
`# CONFIG_USB_INVENTRA_DMA is not set`, so `musbhsdma.c` is not compiled at all. ⚠️ **The
`CONFIG_DMADEVICES=y` / `CONFIG_TI_EDMA=y` that *are* set are a red herring** — that is the **system**
EDMA via dmaengine, not the Inventra engine inside the MUSB block that OMAP3 uses;
`CONFIG_USB_TI_CPPI41_DMA` (the dmaengine-based path) is unset and is for AM335x anyway. The lever is
`CONFIG_KALLSYMS_ALL=y`: every built-in symbol's address is readable at runtime, so a force-loaded module
could supply `musbhs_dma_controller_create` and `omap2430_ops.dma_init` could be pointed at it — the same
family as [F15](#f15-usb-host-mode-through-commissioning--driver-p1-patch-and-tests-done-2026-08-08-docs-left)'s
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
([§2.4](SYSTEM_ANALYSIS.md#24-unpopulated-and-expansion)).

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

### C12. One commissioning entry point — open

**Everything else in this entry has landed 2026-08-06** (`git log --grep=C12`):

- ✅ **One provisioning list, two executors.** `device-files/provision-rules.conf` +
  `lib/rw-provision.sh`, the install half of what `clean-rules.conf` + `lib/rw-clean.sh` are for the delete
  half. The drift it removes was real: the online path deleted stale `rc*.d` links before relinking and
  the offline path did not, so a card carrying an older `S50roomwizard-app` came out of offline
  commissioning with two links to one init script at two priorities. Both `--dry-run`s now print the
  same resolved set, asserted in `tests/rw_provision_test.sh` group E.
- ✅ **The two in-place config edits that had no offline equivalent** — `/etc/profile`'s dangling
  `wsplatform.conf` source and `/etc/inittab`'s tty4 getty — are `dropline` records, so both paths do
  them.
- ✅ **Bundle-install-over-SSH.** `./deploy-all.sh --from-bundle <tar.gz|dir> <ip>` puts a release
  bundle on a device and builds **nothing**, so the person being delivered to needs no toolchain. One
  executor — `rw_bundle_install_ssh` in `lib/rw-bundle.sh` — with the manifest as the authority for
  modes, the SSH twin of `commissioning/commission-offline.sh`'s install loop.
  `tests/rw_bundle_ssh_test.sh`, 23 cases. Distinct from F9's `--from-release <tag>`, which is the same
  install fed by a *download* and is still open.
- ✅ **The folder move**, below: `lib/`, `commissioning/`, and the two device-side scripts into
  `device-files/`. One commit, no logic change.
- ⬜ **One entry point for commissioning**, below.

⚠️ **What is left is the front door, not a capability.** Both delivery situations are reachable with no
toolchain now, but by two different scripts with two different flag vocabularies, and the SSH one still
has a reboot in the middle.

| Situation | Today | One command? |
|---|---|---|
| Bought a unit, **no network access to it** | `sudo ./commissioning/commission-offline.sh --bundle <tar.gz>` | **yes** |
| **Already has SSH** to it | `./commissioning/provision.sh <ip>` → reboot → `./deploy-all.sh --from-bundle <b> <ip>` | no — two, with a reboot between |

`commissioning/commission.sh` is the remaining idea: **one** entry with `--card [--disk X]` or
`--ssh <target>` and `--bundle` on both, composing the scripts that already exist rather than adding
behaviour. `roomwizard.sh` covers the same ground as a menu today, which is why this is an idea and not
a defect.

⚠️ **[C13](#c13-the-ssh-pass-and-the-offline-pass-share-one-clean--closed-2026-08-08)
makes this more than an idea**, though not in the direction this entry assumed: the decision recorded
there is to flip `provision.sh`'s own default rather than to build `--ssh` for it, so the front door no
longer needs a reason beyond convenience. (The other reason this row used to fail at its first command
— no SSH key, and eight gates that only said "check IP and SSH key" — is fixed: `git log --grep=F16`.)

#### There are no duplicate scripts to delete — one duplicated *fact*, now fixed

Checked pairwise 2026-08-06. `commissioning/card-prep.sh` is **step 3 of** `commissioning/commission-offline.sh`, not an
alternative to it; the `rw-*.sh` are libraries with several consumers each; `release.sh` and
`deploy-all.sh` genuinely differ. **Nothing in the root is a redundant copy of anything else.** What
*was* duplicated — the provisioning list, written out once per executor — is now one data file.

#### Two category errors, worth more than the file move itself

1. **`roomwizard-app-init.sh` and `disable-steelcase.sh` were at the repo root**, and both
   are installed verbatim by **both** paths — one `install` record each in
   [device-files/provision-rules.conf](device-files/provision-rules.conf), read by the SSH executor and
   the offline one alike — which is precisely `CLAUDE.md`'s stated
   condition for living in `device-files/`. They were in the wrong *category*, not merely the wrong
   directory — and the init script is installed under a different name
   (`/etc/init.d/roomwizard-app`), so the file now carries the name it is deployed as. All five
   `install` records therefore have a `device-files/` source, which is the
   invariant that was previously two-thirds true.
2. **`commission-roomwizard.sh`'s name was the most misleading thing in the tree.** It read as the
   sibling of `commission-offline.sh` and it is a subroutine of it. Renaming it (`card-prep.sh`) removed
   more confusion than the folder move did.

#### The layout

```text
roomwizard.sh            front door — at root; it is the answer to "what do I run"
deploy-all.sh            development loop AND --from-bundle delivery — at root, NOT commissioning
release.sh               produces the bundle — the build side, at root
lib/            rw-identify.sh  rw-clean.sh  rw-bundle.sh  rw-provision.sh
commissioning/  commission-offline.sh   card-prep.sh (was commission-roomwizard.sh)
                provision.sh (was setup-device.sh)  set-hostname.sh  clone-to-32gb.sh
device-files/   roomwizard-app (was roomwizard-app-init.sh)  disable-steelcase.sh
                audio-enable  time-sync  99-security.conf
                clean-rules.conf  provision-rules.conf
```

`lib/` at the top level rather than `commissioning/lib/`: `lib/rw-bundle.sh` is sourced by all three
component `build-and-deploy.sh` scripts on the **write** side and by the commissioner on the **read**
side, so filing it under `commissioning/` is the same mistake as filing `disable-steelcase.sh` there.

`commission-offline.sh` and `set-hostname.sh` went to `commissioning/` on the same test the others
were judged by — **what is this script's role**: the first is invoked by the front door as one of the
commissioning paths, the second is called only by `card-prep.sh` (offline) and `provision.sh` (over
SSH) and by nothing else. Neither is a library and neither is an answer to "what do I run".

#### What the move had to be careful about

- **Three places in `lib/` derived the repo root from the library's own directory** —
  `rw_clean_rules_file`, `rw_provision_rules_file` and `rw_provision_validate`'s default. All three now
  go up one level. Miss one and the rules file is looked for in `lib/`, where it is not.
- **The four `commissioning/` scripts needed `REPO_ROOT` next to `SCRIPT_DIR`**, because `$SCRIPT_DIR`
  stopped meaning "the repo" for them. `card-prep.sh` calling its sibling `set-hostname.sh` still wants
  `SCRIPT_DIR`; sourcing `lib/` and reading `device-files/` want `REPO_ROOT`. The install loop in
  `provision.sh` resolves the plan's repo-relative sources against `REPO_ROOT` too.
- ⚠️ **Three device-side paths look like repo paths and must not be rewritten**: `/tmp/rw-provision.sh`
  and `/tmp/set-hostname.sh` (staged on the far side of an ssh pipe) and
  `/opt/roomwizard/disable-steelcase.sh` (the deployed copy). A blanket rename over the tree turns them
  into `/tmp/lib/rw-provision.sh` and friends, and nothing on this host would notice.
- **`tests/commission_prep_test.sh`'s Step 8 stub had to follow**: it injects the variable the extracted
  block reads to find `COMMISSIONING.md`, which is now `REPO_ROOT`, not `SCRIPT_DIR`.

#### Verification, and what cannot be verified from this host

- ✅ **The move changed no behaviour anything can measure from here.** All five host-only suites pass
  unchanged (148 + 94 + 37 + 17 + 23 = 319), `tests/c11_plan_diff.sh` is clean on both plans,
  `deploy-all.sh --list` still discovers exactly the four components, every tracked `*.sh` plus the two
  `device-files/` init scripts pass `bash -n`, the two device-side scripts pass `dash -n` and are still
  LF, and `git ls-files -s -- '*.sh'` is still all `100755`.
- ✅ **Both executors' `--dry-run` print the same resolved set**, compared through
  `rw_provision_canonical`. `tests/rw_provision_test.sh` group E, with its own negative control (drop a
  verb from one side and the comparison must fire). The online half runs the *generated* interpreter, not
  a re-implementation of it.
- ⚠️ **Neither `tests/commission_offline_test.sh` nor any non-dry `commissioning/commission-offline.sh` run has been
  executed since the provision fold.** Both need root and `sudo` cannot be driven non-interactively from
  this harness (`sudo: a password is required`). The block is not the mount — `--base` needs no root —
  but `commissioning/card-prep.sh`'s `sudo` on the `/etc/shadow` write, which is where a run stalls
  waiting for a password it cannot be given. What *was* done instead: the test's file list now copies
  `lib/rw-provision.sh` (without which every case would fail at the source line), and the offline path was
  driven end to end with `--base --dry-run` through the real script — 29 actions, correct order, correct
  declared modes. The executor itself is covered by `tests/rw_provision_test.sh` group D, which does
  perform real writes, real `chmod` and real symlinks against a synthetic card in WSL's own filesystem.
  **Run the root suite under an interactive sudo before trusting the offline installer again.**
- ⚠️ **A mode cannot be verified on `/mnt/c`.** It reports every file 0777 and discards `chmod`, so the
  provision plan **declares** modes exactly as `lib/rw-bundle.sh` does. `tests/rw_provision_test.sh` group D
  runs under `$(mktemp -d)` in WSL's own filesystem, which does honour modes, so it asserts 0755/0644
  there; the assertion that a *device* gets them stays `commissioning/commission-offline.sh`'s `+x` check on real ext4.
- The reorg commit's own control is that all five host-only suites still pass unchanged, and that
  `git ls-files -s -- '*.sh'` is still all `100755` afterwards.


### C13. The SSH pass and the offline pass share one clean — closed 2026-08-08

**Closed by the same commit as [F15](#f15-usb-host-mode-through-commissioning--driver-p1-patch-and-tests-done-2026-08-08-docs-left)**, whose ✅ block records what was built. Kept here for the two
measurements the decision rested on, because both are still load-bearing and are the kind of thing that
gets re-raised as a risk.

`commissioning/provision.sh` now cleans by default, with the same group list
`commissioning/commission-offline.sh` uses, so **the result of commissioning no longer depends on which
path ran** — the one thing the two shared data files exist to prevent. Both paths read
[`device-files/clean-rules.conf`](device-files/clean-rules.conf) through `lib/rw-clean.sh`, so *what* a
clean does never could drift; only *whether* one happened did.

⚠️ **Do not re-raise these as risks — they are measured, not argued.**

- **Nothing we install is sweepable.** `/opt/games`, `/opt/roomwizard`, `/opt/vnc_client` and
  `/opt/scummvm` are `keep` records in group `base`, which is never disabled, and so are
  `/etc/rc5.d/S89xpad-modules` and `S90usb-host`. `/etc/init.d` is not a `scope` sweep at all, so the
  init scripts behind every one of those links survive too. **A second deep clean over a fully deployed
  unit removes none of it.** (Those two USB links are now `provision-rules.conf` records as well, so
  `rw_provision_check_keeps` asserts the pairing rather than a comment asking a human to remember it.)
- **The residual case is one unit state, not a general hazard:** a unit *not yet* deep-cleaned. It runs
  the sweeps for the first time during what the operator meant as "push a changed `device-files/`
  script" — behind the backup question, which is asked first.

**The cost the decision accepted:** the developer's own re-run answers a prompt every time. `--no-clean`
is the way out, and it is the flag to put in a personal alias. Any change under `device-files/` reaches a
device through no other path (`CLAUDE.md` → *Redeploy scope by changed file*), and the run ends in a
reboot.

**The rejected alternative** was to put the default on `commission.sh --ssh`
([C12](#c12-one-commissioning-entry-point--open)'s remaining `⬜`), leaving `provision.sh` as the
developer's opt-in tool. It would have kept the dev loop non-interactive, but the `⬜` has to be built
first and the two paths stay divergent until it is. Neither option reintroduces a middle setting: there
is one default per *situation served*, and `--keep-<group>` remains the only partial opt-out.

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

1. **[F15](#f15-usb-host-mode-through-commissioning--driver-p1-patch-and-tests-done-2026-08-08-docs-left)'s
   remaining ⬜ list** — the code, its 94-case suite and the sabotage harness that proves the suite can
   fail are all in; what is left is the p1 cases in `tests/commission_offline_test.sh`, `LICENSE.md`, and
   three prose files. All host-only.
2. **The three device checks nothing here can do** — `sudo tests/commission_offline_test.sh`, a full
   bundle installed on `.225`, and an Xbox pad plugged in with **no powered hub** after a reboot. That
   last one is the only check that the p1 patch took effect, and until it runs, "500 mA" is a verified
   *write* and an unverified *effect*.
3. **`COMMISSIONING.md`'s ordering** — F10's *What is left* #1. The prose is a description now; what is
   left is that it leads with the SSH flow while the offline pass is the verified delivery path, plus
   two stale lines named there. Worth doing after F15's docs, since C13 changed what the SSH flow does.
4. **F1 (ALSA)** is the biggest user-visible improvement available, and it is pure userspace.
5. **F2 (DSS overlays)** is the biggest performance win, also pure sysfs. Deep-clean the device first if
   disk space is tight — which is now the default.
6. **C10 before panel check #2** — it converts a play session into one launch, and every future
   level-dependent bug pays the same toll until it exists.

[F11](#f11-one-home-for-the-host-build-prerequisites--open) reads more urgent than it is: **this WSL
has the whole toolchain** (measured 2026-08-06 — see F11), so it is a fresh-machine and documentation
item rather than a blocker, and [B27](#b27-sfdisk-absence-is-reported-as-a-test-failure-not-a-skip--open-latent)
cannot fire here. [F12](#f12-install-from-a-published-release--open) unblocks anyone who is
not the developer; [F13](#f13-commissioning-from-windows-without-wsl-and-from-macos--open-unsolved) is
recorded rather than planned, because the honest answer is a bootable image.
[F15](#f15-usb-host-mode-through-commissioning--driver-p1-patch-and-tests-done-2026-08-08-docs-left) is
**built and host-tested; its effect on a device is not yet measured**, which is the distinction its own
entry keeps. [C13](#c13-the-ssh-pass-and-the-offline-pass-share-one-clean--closed-2026-08-08) and
[D7b](#d7b-etchosts-and-etchostname-are-regenerated-on-boot--closed-2026-08-08) closed with it — D7b's
item 2 by deciding not to build it.

Everything else is genuinely unranked rather than deprioritised. **F6 (multi-touch) is the one to
consider promoting**: the register map is published, so it is far less speculative than its position
in this list suggests.
