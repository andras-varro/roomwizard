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
| 5 | ~~First boot of an offline-commissioned unit~~ — **done 2026-08-06.** Commissioned offline, booted first time, launcher grid, SSH, games, sound. Two findings came out of it: [B26](#b26-the-backlight-is-dimmer-than-under-vendor-firmware--open-confirmed-2026-08-06) and [F14](#f14-decide-whether-the-boot-progress-bar-comes-back--open) | — | closed |

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

### B26. The backlight is dimmer than under vendor firmware — open, confirmed 2026-08-06

**Symptom, observed on `rwtest` (192.168.50.225):** after offline commissioning the panel is visibly
dimmer than the same class of unit running stock firmware. Not seen before the clean.

**A cause chain read out of the captured vendor tree — a hypothesis, so measure before fixing:**

1. `/etc/init.d/browser:77` called `/opt/sbin/backlight/adjustbklight.sh` on every boot.
2. That family (`setbacklight.sh`) reads `/home/root/data/websign/brightness.conf` and writes the value
   to `/sys/class/leds/backlight/brightness`, **defaulting to 100** when the file is empty or absent.
3. Our clean deletes **both** ends of that: `/etc/init.d/browser` (the caller) and `websign/` (the
   stored level, D7b's input).
4. So nothing writes `brightness` at boot any more and the panel sits at the driver's power-on default.

⚠️ **`/opt/sbin` is kept, so `/opt/sbin/backlight/*` still exists on the card** — but do not simply call
it: it depends on `websign/brightness.conf`, which we delete on purpose. Note also that a *dim but
working* panel means the deletion removed a **setter**, not a driver.

**Measure first, on the unit, cheaply and over SSH:**

```sh
cat /sys/class/leds/backlight/brightness /sys/class/leds/backlight/max_brightness
echo 100 > /sys/class/leds/backlight/brightness      # does it brighten?
```

If it brightens, the cause is confirmed and the fix is a setter in **our** boot path — a
`device-files/` init script alongside `audio-enable` and `time-sync`, so the panel is bright from boot
rather than only once an app runs. `common/hardware.c` already drives this sysfs node, so an app-level
set is the weaker option. If it does *not* brighten, the cause is elsewhere and this entry is wrong.

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

### D7b. `/etc/hosts` and `/etc/hostname` are regenerated on boot — open, **confirmed 2026-08-05**

Confirmed by reading the vendor script on both captured cards (`diff` reports them identical) and by
the second unit's own syslog. `/opt/sbin/networkmanager` rewrites `/etc/hosts`, `/etc/hostname`,
`/etc/resolv.conf` and `/etc/dhclient.conf`'s `send host-name` on **every boot** from
`/home/root/data/websign/net.*`. Mechanism, table and evidence:
[`SYSTEM_ANALYSIS.md#35-network-and-power`](SYSTEM_ANALYSIS.md#35-network-and-power). So
`set-hostname.sh`'s offline half **is** undone by the first boot after commissioning, as suspected —
observed on a unit commissioned as `RW-Test`, which booted with `/etc/hostname` back to `null`.

**What RW09's "weak evidence" actually was.** `--remove` deletes `/home/root/data/websign` and the
deep clean's data file names it too, and both writers live *inside* `set_manual()`/`set_dhcp()` — so on
a cleaned unit neither branch runs and the name is never touched again. **The exposure window is exactly
"commissioned but not yet cleaned",** which is why nothing regressed on RW09 and why a card read
straight after commissioning shows the revert.

**What remains open is the SSH flow only.** Two of the three fixes are in:

1. **Ordering — done, in [F10](#f10-single-pass-offline-commissioning--done-2026-08-05-confirmed-on-a-unit-2026-08-06).**
   `commission-offline.sh` names the card and deletes both `websign` and the `rcS.d/S60networkmanager`
   link in the same offline pass, so no boot happens in between, and its verify pass fails if either
   survives. That removes the window rather than patching it. **Confirmed on real hardware 2026-08-06:**
   a unit commissioned as `rwtest` booted with its name intact and answered on the network.
2. **STILL OPEN, for the SSH flow, which keeps the vendor stack:** `set-hostname.sh` must also write
   `websign/net.hostname`, and `websign/net.mode` must read `dhcp` or the unit is unreachable at all
   (a `manual` card takes a static address and sends no DHCP request). Note the vendor's validator
   **rejects hyphens**, so `RW-Test` would still be replaced by its fallback `rwtwenty`.
3. **`/etc/dhclient.conf`'s `send host-name` — done.** `set-hostname.sh` now owns it, in both the
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
`roomwizard-app-init.sh`, yet the unit in service (4 days uptime) does not have the file. It is benign
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

**Built and exercised on this host:** `release.sh` at the repo root, `rw-bundle.sh` (the bundle layout,
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
  `build/`, which will not exist on a build-free path. `rw-bundle.sh` writes
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

**`commission-offline.sh` exists and does the whole job.** The card goes into a reader, the operator
answers two questions, the card goes back, and the device should boot working. What has **not**
happened is the only thing left: running it against a real card and booting a real unit.

**Why offline is not merely a convenience.** A unit whose `websign/net.mode` is `manual` takes a static
address and never sends a DHCP request, so it appears in no lease list and **the SSH phase can never
reach it** ([§3.5](SYSTEM_ANALYSIS.md#35-network-and-power)). Stock cards ship that way. Editing the
card is the only bootstrap for such a unit. It also **removes
[D7b](#d7b-etchosts-and-etchostname-are-regenerated-on-boot--open-confirmed-2026-08-05)'s window**
rather than patching it: the regenerator's input is deleted in the same pass that sets the name, so no
boot happens in between.

#### What is built

| Piece | Where |
|---|---|
| The offline commissioner | `commission-offline.sh` — identify, mount, orchestrate, clean, install, verify, unmount |
| The keep/delete decisions | `device-files/clean-rules.conf` — 198 records, four tab-separated fields, a reason on every one |
| The parser, plan compiler and guarded `del()` | `rw-clean.sh` |
| Files installed verbatim | `device-files/{audio-enable,time-sync,99-security.conf}` — no longer heredocs inside `setup-device.sh` |
| The bundle it installs | `release.sh` → `rw-bundle.sh` ([F9](#f9-ship-binaries-as-github-releases--partly-built-2026-08-05-open)) |
| Card and mount identification | `rw-identify.sh`, by content and by **position**, never UUID |
| Regressions | `tests/rw_clean_test.sh` (116 cases), `tests/commission_offline_test.sh` (21), `tests/make-fake-card.sh` |

**`setup-device.sh --deep-clean` reads the same data file**, so the live and offline cleans cannot
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
- **The upgrade payload is a `factory` group.** Today it is off by default and `--delete-factory` opts
  in; that default is **reversed by
  [C11](#c11-one-clean-one-mechanism--open-confirmed-2026-08-05)**, which is where the reasoning lives.
  `factory/uImage-system-original` is kept either way.
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
Run with `--delete-factory`, so the 472 MB on-card restore payload is gone by choice.

ScummVM and `vnc_client` are absent **because the bundle was `native_apps` only** — not a failure.
Deploy them over SSH, or restage a three-component bundle.

#### What is left

1. **`COMMISSIONING.md` can now be a description rather than a plan.** This was deliberately deferred
   until the tool had commissioned a unit. It still documents the three-phase SSH flow as the primary
   path; the offline single pass is now the verified one for *delivery*.
2. **Unit B — anything more aggressive, one increment per boot.** A failed boot yields no diagnostics
   (no serial console, [§3.12](SYSTEM_ANALYSIS.md#312-serial-ports)); the only post-mortem is mounting
   p3 offline and reading `messages`, which only helps if it got as far as syslog. Still the reason to
   stop unless something specific is being chased.

#### Scope boundaries

- **Still needs the device:** touch calibration only — per-unit, per-panel, and the wizard exists
  (Device Tools → Display → `CALIBRATE TOUCH`). One boot remains; this removes two of three plus the
  IP hunt.
- **Does not replace the SSH path.** `setup-device.sh` and `deploy-all.sh` stay as the verified
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
   set would both have to name it, since [C11](#c11-one-clean-one-mechanism--open-confirmed-2026-08-05)
   makes a link the whitelist does not name get swept on the next clean.
2. **Draw our own.** `app_launcher` already owns the framebuffer and there is no fb0 contention at all
   — a splash drawn by our stack sidesteps the handoff entirely, and can show something honest about
   what is loading.

Option 2 is the smaller change and cannot regress the boot; option 1 restores exactly what was there.
Neither is urgent — recorded so the deletion stays a decision with a known cost rather than a surprise.

---

### F15. USB host mode is unreachable from the delivery flow — open, confirmed 2026-08-06

**Symptom:** USB does not work on the offline-commissioned unit (`rwtest`, 192.168.50.225).

**This is by construction, not a regression.** USB host mode needs the `usb_host` component, which
patches the DTB inside `uImage-system` — and that lives on **p1**, which the offline commissioner must
never write, because an untouched p1 is what keeps a power cycle a free undo
([§4.7](SYSTEM_ANALYSIS.md#47-recovery)). `release.sh` therefore excludes `usb_host` from **every**
bundle, deliberately. So no bundle can ever deliver USB, and nothing in the flow says so.

**Today's only path** is over SSH after the unit boots: `usb_host/build-and-deploy.sh <ip>`. That needs
the kernel-module build deps (`bc libssl-dev bison flex`) plus `python3`, i.e. a full toolchain host —
which is exactly what the delivery mode does not have.

The tension is real and this entry is where it gets resolved rather than rediscovered:

- **Say so, cheaply.** `commission-offline.sh`'s closing list and `COMMISSIONING.md` should state that a
  commissioned unit has no USB host mode and name the one command that adds it. One line each; it
  removes the surprise without touching the p1 rule.
- **A patched kernel under a *new* filename on p1 is not the same as overwriting `uImage-system`** —
  `bootcmd` is hardcoded, so a staged alternative is inert and a power cycle still recovers. Whether the
  bundle should be allowed to *stage* one, without ever changing what boots, is the open design
  question. It is not obviously safe: p1 is the one partition absent from `RW_PART_ROLES` precisely so
  that no caller can reach it, and adding a reason to reach it weakens a guarantee that has held.
- **Do not fold `usb_host` into a bundle before that is settled.** The exclusion is load-bearing.

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
  `commission-offline.sh` needs `arm-linux-gnueabihf-objdump` on a host that has **no compiler at
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

`--bundle` is already `commission-offline.sh`'s single source of binaries and everything downstream is
origin-agnostic — unpack, `rw_bundle_check`, the ARM gate, install, md5 — so `--release <tag|latest>`
is a fetch into a temp directory plus a handoff to the existing path. That also removes the
copy-a-tarball-to-the-commissioning-host step from the delivery mode of
[F11](#f11-one-home-for-the-host-build-prerequisites--open).

- **Never the default, and the `--help` must say so.** The script is called `commission-offline.sh` and
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
The blocker is the *kernel's* filesystem support: `commission-offline.sh` needs read-write ext4 across
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

### C11. One clean, one mechanism — open, confirmed 2026-08-05

**Three delete policies exist for one job, and two of them are not the data file.** Measured by reading
the source, not inferred:

| Path | Mechanism | Reads `clean-rules.conf`? |
|---|---|---|
| `setup-device.sh --remove` | ~85 lines of hardcoded `rm -rf` inside an `ssh … <<'REMOTE'` heredoc, [setup-device.sh:700-786](setup-device.sh#L700-L786) | **no** |
| `setup-device.sh --deep-clean` | implies `--remove`, then runs the compiled plan — so **both** mechanisms, in sequence | partly |
| `commission-offline.sh` | the compiled plan only, always on, `--no-clean` to disable | yes |

The heredoc even carries its own keep-comments for `/opt/pv02`, `/opt/sound` and the ZigBee tooling,
restating decisions the data file already records with reasons — and a comment inside it says so
outright: *"two lists in one script is exactly the drift that file exists to prevent."* It is the older
of the two and nothing has retired it.

**The factory payload diverges the same way:** `setup-device.sh` prompts for it interactively and has
no flag at all ([setup-device.sh:298-307](setup-device.sh#L298-L307)); `commission-offline.sh` has
`--delete-factory` and never prompts. Same rules file, two policies.

#### Intent, decided 2026-08-05

**Both delivery and development want the identical clean**, and it should not matter which path ran it.
The device is being repurposed as a small Linux computer: delete everything the vendor stack needs and
nothing a normal Linux needs. If something in the vendor image later turns out to be wanted, the fix is
to edit the rules file — the original card image is the fallback, which is why a full-card backup is
already a precondition.

1. **`clean-rules.conf` + `rw-clean.sh` become the only mechanism.** Delete the heredoc. Every target of
   it that is not already a rule becomes one, with a reason in the fourth field.
2. **`--remove` becomes a named group subset of the same plan**, not a second implementation. It keeps
   its meaning ("bloatware only") and loses its private list.
3. ⚠️ **The factory payload is deleted by default; `--keep-factory` opts out.** This **reverses** the
   current default. The reasoning: that payload restores the vendor stack the clean just removed, so on
   a commissioned unit it is 472 MB whose only function is to undo the commissioning. The host-side
   full-card backup is already required and is a strictly better recovery path.
   `factory/uImage-system-original` — the fallback kernel — stays kept either way.
4. **Both consumers take the same flags and produce the same effect.** They keep separate *executors*
   only because `/` is the correct prefix on a device and a refused one offline.

#### Verification

- **Write the failing version first.** `tests/rw_clean_test.sh` C17, C18 and E22 currently assert the
  *old* factory default; they invert. Compile the new assertions against the pre-change source and
  count the failures before fixing anything.
- **The negative control for folding the heredoc is a plan diff**: every path the heredoc removed must
  appear in the compiled plan, or be deliberately absent with a recorded reason. A fold that silently
  drops a target looks exactly like a successful one.
- `tests/commission_offline_test.sh` covers the offline half; `--dry-run` on both paths over the same
  card should now print the same set of resolved paths, which is the assertion that "one clean" is true
  rather than merely intended.

#### Sequencing

✅ **The gate is discharged, 2026-08-06.** This was held back so that the first run against real
hardware would not also be the first run of a newly inverted destructive default — a failed boot has no
serial console, and "the 472 MB restore payload was just deleted" would have been one more variable in
that post-mortem. F10 has now commissioned a unit that booted working first time (`rwtest`,
192.168.50.225) **with `--delete-factory` applied**, so the inverted default has been exercised once on
real hardware already. Nothing else blocks this.

⚠️ **Do it in one pass with [C12](#c12-one-provisioning-list-two-executors--and-a-missing-online-mode--open).**
Both fold a hardcoded list out of `setup-device.sh` into a data file with two executors — C11 the
*delete* half, C12 the *install* half. Done separately they will invent two plan formats and two test
harnesses.

---

### C12. One provisioning list, two executors — and a missing online mode — open

**There are two operator situations, and only one of them has a single command.** Measured by reading
every root script 2026-08-06, not inferred:

| Situation | Today | One command? |
|---|---|---|
| Bought a unit, **no network access to it** | `sudo ./commission-offline.sh --bundle <tar.gz>` | **yes** |
| **Already has SSH** to it | `commission-roomwizard.sh` → boot → find the IP → `setup-device.sh` → `deploy-all.sh` | no — 3 scripts, a reboot and an IP hunt |

⚠️ **The online path cannot deliver, because `deploy-all.sh` *builds*.** It needs the ARM toolchain, and
the person being delivered to by definition has none. So the online mode is not a repackaging of
existing code: **bundle-install-over-SSH does not exist anywhere in this repo.** That is the real
asymmetry, and it is new code rather than a merge — roughly one executor over
`rw_bundle_entries`, the SSH twin of `commission-offline.sh`'s `put`. It is the same capability F9
records as `deploy-all.sh --from-release`, which is also still open.

#### There are no duplicate scripts to delete — one duplicated *fact* instead

Checked pairwise. `commission-roomwizard.sh` is **step 3 of** `commission-offline.sh`
([commission-offline.sh:404](commission-offline.sh#L404)), not an alternative to it; the three `rw-*.sh`
are libraries with five consumers each; `release.sh` and `deploy-all.sh` genuinely differ. **Nothing in
the root is a redundant copy of anything else.** What *is* duplicated is the provisioning list — which
`device-files/` go to which device path, which `rc*.d` links, the sshd hardening, the sysctl file —
written out once per executor:

| Fact | Offline (`put` / `link_boot` into `$BASE/root`) | Online (`scp` + `ssh` heredoc) |
|---|---|---|
| boot scripts + payload → paths | [:475-489](commission-offline.sh#L475-L489) | [:517-531](setup-device.sh#L517-L531), [:538-541](setup-device.sh#L538-L541), [:578-586](setup-device.sh#L578-L586) |
| `S28`/`S29`/`S99`/`S30avahi` links | [:490-536](commission-offline.sh#L490-L536) | [:546-566](setup-device.sh#L546-L566), [:605-607](setup-device.sh#L605-L607) |
| sshd hardening | [:537-560](commission-offline.sh#L537-L560) | [:636-649](setup-device.sh#L636-L649) |
| `99-security.conf` | [:472](commission-offline.sh#L472) | [:667-674](setup-device.sh#L667-L674) |

**They have already drifted:** the online path deletes stale `rc*.d` links before relinking
([setup-device.sh:546-557](setup-device.sh#L546-L557)) and the offline path does not. This is exactly
the shape `clean-rules.conf` fixed for the *delete* half — one plan as data, two executors, one
prefixing `/` and one `$BASE/root`. The install half never got it, which is why this is
[C11](#c11-one-clean-one-mechanism--open-confirmed-2026-08-05)'s other half and not a separate project.

#### Two category errors, worth more than any file move

1. ⚠️ **`disable-steelcase.sh` and `roomwizard-app-init.sh` are device payload, not host tooling.** Both
   are installed verbatim by **both** paths ([commission-offline.sh:487-488](commission-offline.sh#L487-L488),
   [setup-device.sh:165-166](setup-device.sh#L165-L166)), which is precisely `CLAUDE.md`'s stated
   condition for living in `device-files/`. They are in the wrong *category*, not merely the wrong
   directory — and `roomwizard-app-init.sh` is installed under a different name
   (`/etc/init.d/roomwizard-app`), so the file should carry the name it is deployed as.
2. **`commission-roomwizard.sh`'s name is the most misleading thing in the tree.** It reads as the
   sibling of `commission-offline.sh` and it is a subroutine of it. Renaming it (`card-prep.sh`) removes
   more confusion than the folder move does.

#### Proposed layout

```text
roomwizard.sh            front door — stays at root; it is the answer to "what do I run"
deploy-all.sh            development loop — stays at root, NOT commissioning
release.sh               produces the bundle — the build side, stays at root
lib/     rw-identify.sh  rw-clean.sh  rw-bundle.sh  rw-provision.sh (new)
commissioning/  commission.sh   ONE entry: --card [--disk X] | --ssh <target>, both --bundle
                card-prep.sh    (was commission-roomwizard.sh)
                provision.sh    (was setup-device.sh)
                clone-to-32gb.sh
device-files/   roomwizard-app  disable-steelcase.sh  audio-enable  time-sync
                99-security.conf  clean-rules.conf
```

`lib/` at the top level rather than `commissioning/lib/`: `rw-bundle.sh` is sourced by all three
component `build-and-deploy.sh` scripts on the **write** side and by the commissioner on the **read**
side, so filing it under `commissioning/` is the same mistake as filing `disable-steelcase.sh` there.

#### Cost of the move, measured

**438 mentions of these filenames across 32 tracked text files** — `IMPROVEMENT_PLAN.md` 47,
`COMMISSIONING.md` 41, `CLAUDE.md` 40, `setup-device.sh` 40, `roomwizard.sh` 38, plus 4 test scripts
that source by path (`tests/commission_offline_test.sh` alone has 18) and 5 component build scripts.
Mechanical, but it must be **its own commit containing no logic change**, or every diff after it is
unreviewable.

#### Sequencing, decided 2026-08-06

**Behaviour first, move last** — the user's call, when offered the alternative. So: C11 + the provision
plan as one pass, then bundle-install-over-SSH, then `COMMISSIONING.md`, and the folder move as the
final mechanical commit. Two reasons it is not first: renaming files whose logic is mid-rewrite makes
both diffs unreadable, and C11's fold removes ~85 lines from `setup-device.sh` before it gets moved.

#### Verification, and what cannot be verified from this host

- **The negative control for the fold is a plan diff** — the same one C11 needs. Every path the online
  heredoc installs must appear in the compiled provision plan, or be deliberately absent with a recorded
  reason. A fold that silently drops a target looks exactly like a successful one.
- ⚠️ **`--dry-run` on both executors over the same inputs should print the same resolved set**, modulo
  the `/` versus `$BASE/root` prefix. That is the assertion that "one list" is *true* rather than
  intended, and it is the only one that catches the drift above.
- ⚠️ **A mode cannot be verified on this host.** `/mnt/c` reports every file 0777 and discards `chmod`,
  so the provision plan must **declare** modes exactly as `rw-bundle.sh` does, and the `+x` assertion
  stays a measurement only on real ext4.
- The reorg commit's own control is that all four host-only suites still pass unchanged, and that
  `git ls-files -s -- '*.sh'` is still all `100755` afterwards.

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

0. **[B26](#b26-the-backlight-is-dimmer-than-under-vendor-firmware--open-confirmed-2026-08-06) — the
   backlight.** It is the one thing a user sees every second the device is on, the unit is reachable at
   `192.168.50.225`, and the whole first measurement is two SSH commands. A recorded cause chain is
   waiting to be confirmed or refuted.
1. **[C11](#c11-one-clean-one-mechanism--open-confirmed-2026-08-05) together with
   [C12](#c12-one-provisioning-list-two-executors--and-a-missing-online-mode--open).** Three delete
   policies for one job is the largest live inconsistency in the repo, and one of them is an 85-line
   heredoc that the data file was created to replace. C12 is the same fold applied to the *install*
   half, plus the capability the delivery story is actually missing: **the online mode cannot install a
   bundle, because `deploy-all.sh` builds.** Do them as one pass — separately they invent two plan
   formats. The folder reorganisation is the *last* commit of that work, not the first.
2. **`COMMISSIONING.md`** — F10's *What is left*. The doc can finally be a description rather than a
   plan, and the reorg above renames the scripts it documents, so it lands after them. (The two bugs the
   first real run exposed are fixed: `git log --grep=F10`.)
3. **F1 (ALSA)** is the biggest user-visible improvement available, and it is pure userspace.
4. **F2 (DSS overlays)** is the biggest performance win, also pure sysfs. Deep-clean the device
   (`--deep-clean`) first if disk space is tight.
5. **C10 before panel check #2** — it converts a play session into one launch, and every future
   level-dependent bug pays the same toll until it exists.

[F11](#f11-one-home-for-the-host-build-prerequisites--open) reads more urgent than it is: **this WSL
has the whole toolchain** (measured 2026-08-06 — see F11), so it is a fresh-machine and documentation
item rather than a blocker, and [B27](#b27-sfdisk-absence-is-reported-as-a-test-failure-not-a-skip--open-latent)
cannot fire here. [F12](#f12-install-from-a-published-release--open) unblocks anyone who is
not the developer; [F13](#f13-commissioning-from-windows-without-wsl-and-from-macos--open-unsolved) and
[F15](#f15-usb-host-mode-is-unreachable-from-the-delivery-flow--open-confirmed-2026-08-06) are recorded
rather than planned, because the honest answers are a bootable image and a p1 decision respectively.

Everything else is genuinely unranked rather than deprioritised. **F6 (multi-touch) is the one to
consider promoting**: the register map is published, so it is far less speculative than its position
in this list suggests.
