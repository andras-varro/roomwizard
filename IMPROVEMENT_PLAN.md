# Improvement Plan

**Open work only.** Finished items are not listed here — the code and `git log` are the record.

**How to read this**

- **B**n = bug, **F**n = feature, **D**n = doc/infra, **C**n = cleanup. IDs are never reused or
  renumbered, because commit messages cite them. ⚠️ **Nothing outside this file may cite one** — an entry
  is deleted the moment it closes, so any citation elsewhere has a scheduled expiry built in
  (`tests/doc_check.sh` group B counts them, resolving or not). To find a closed one: `git log --grep=B13i`.
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
Distinct from enumeration-at-probe, which is about a cold port never obtaining a session.

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

⚠️ Cheap today, but it would need rewriting as DRM atomic plane code after a **mainline** port — which
is out of scope, and which a 4.14.52 rebuild is not: that leaves omapdss and this code intact.

### F4. Surface the two MADC channels that need no wire — open

Both are readable with `cat` today and have zero references in the codebase
([`SYSTEM_ANALYSIS.md#311-adc-and-temperature-twl4030-madc`](SYSTEM_ANALYSIS.md#311-adc-and-temperature-twl4030-madc)):

- `in_temp1_input` — SoC die temperature. Add a readout to Device Tools (~10 minutes).
- `in_voltage9` — RTC backup cell voltage. A "battery low" warning is nearly free.

⚠️ **The analogue-paddle half is closed, 2026-09-06, and must not be re-proposed.** `in_voltage2..7`
are six idle general-purpose channels and a potentiometer on one would be a real analogue paddle, but
`ADCIN2..ADCIN7` have no populated test point — an input device needs one physically wired to a
channel, which [`SYSTEM_ANALYSIS.md#8-hardware-policy`](SYSTEM_ANALYSIS.md#8-hardware-policy) rules
out. That is a scope decision, not a difficulty one.

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
[F23](#f23-the-p1-gate-knows-one-firmware-release-and-refuses-every-other--open-measured-2026-09-02).

✅ **And the fetch half is done and confirmed on a unit.** `lib/rw-release.sh` resolves, downloads and
sha256-verifies a published asset; `deploy-all.sh --from-release <tag|latest> <ip>`,
`commissioning/commission-offline.sh --release <tag|latest>` and `roomwizard.sh` items 3 and 6 are its
consumers. Confirmed 2026-09-01 against `v1.0.0` on `192.168.50.188`: 105 files installed, md5- and
`+x`-verified, launcher grid back on the panel. The measurements that shaped it are in `lib/CLAUDE.md`.

✅ **The publish preconditions now run before the build, 2026-09-07.** `gh`'s presence, origin's
owner/repo, a clean tree and a pushed `HEAD` are all readable from local state, so they sit above the
component loop; a refusal costs a second instead of a four-component rebuild, and it no longer wipes
`build/release` on the way (the `rm -rf "$OUT"` is below the preflight). Measured: `--tag` on this
unpushed `HEAD` refused in 12 s having built nothing and left the staged bundle's mtime untouched, and
`--stage-only` still skips the preflight entirely (8 s, exit 0). The dirty check is **re-measured after
the build** as well, because the hoisted one reads the pre-build tree. ⚠️ **`gh auth status` and "does
this tag already exist" are deliberately NOT hoisted** — both need the network, and an early refusal for
a reason unrelated to the release is worse than a late one.

✅ **A unit can now say what it is running, 2026-09-07.** `manifest.d/bundle.info` is staged as an
ordinary artifact at `/opt/roomwizard/bundle.info` under a reserved manifest component, so
`rw_bundle_entries` carries it and **both** installers write and md5-verify it with no installer change.
Same bytes as the bundle's own copy, so the two cannot disagree. `rw_bundle_components` excludes the
reserved name — every caller of it is telling an operator what the bundle contains, and it also fills
`bundle.info`'s own `components=` line. `/opt/roomwizard` is kept whole by `clean-rules.conf`, so the
stamp survives a re-clean.

**What is still open here:**

- **`deploy-all.sh <ip>` clears the stamp; one component's `build-and-deploy.sh` does not.** A source
  deploy must not leave a stamp naming a release whose bytes are gone, so the whole-tree path removes it
  — absence is the honest answer there. Running a single component directly bypasses that, so a stamp is
  only as trustworthy as the last whole-tree deploy. Fixing it properly means one writer shared by four
  scripts, not four copies of an `ssh rm -f`.
- ⚠️ **`release.sh` has no test suite at all, and it holds an `rm -rf` of a caller-supplied path.**
  Measured 2026-09-07 while fixing that guard (below): nothing in `tests/` invokes `release.sh` except
  as a fixture builder. `tests/rw_clean_test.sh` section A is the model to mirror.
- **Whether the `NOTICE` written offer is actually discharged has not been checked by anyone qualified to
  say so.** `release.sh` generates the per-release half and `LICENSE.md` says the two must agree; that is
  a bookkeeping guarantee, not a legal opinion. ⚠️ **Measure a dependency's licence *version* rather than
  carrying it forward**: this entry once said ScummVM was GPLv2+ and the tree is **GPL-3.0-or-later**.

⚠️ **A `case` pattern in quotes is a literal, and it silently disarmed the `rm -rf` guard — fixed
2026-09-07.** `--out`'s guard read `""|"/"|"/*"`, and a **quoted** `"/*"` matches only the
two-character string `/*`, never an absolute path. So `--out /usr` was accepted straight into
`rm -rf "$OUT"`, and `--out //` and `--out /.` were too. The comment claimed `rw_clean_del`'s reasoning
while porting only its two cheap string refusals: it had dropped the slash/dot **normalisation** and the
**containment** check, which are what actually make `del()` safe. `--out` has no base to contain
against, so ownership substitutes for containment — an existing `$OUT` must be a previous bundle
(`root/` + `manifest.d/`) or empty. Verified both ways: a decoy directory holding one file is refused and
the file survives; `/`, `//` and `/.` are all refused; an empty directory and a re-stage over a real
bundle both still succeed.

**The host pulls the tarball; the only thing new on the device is the stamp.** Nothing new *runs* there,
and there is no CA-certificate problem to solve on a 2022 vendor image.

**`commissioning/commission-offline.sh` depends on this one** — an offline commissioner has no
toolchain to fall back on, so the release *is* its only source of binaries. The obligations that only
bite once artifacts are published are enumerated per artifact in [`LICENSE.md`](LICENSE.md).

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

### F23. The p1 gate knows one firmware release, and refuses every other — open, measured 2026-09-02

**The symptom, hit for real:** offline commissioning of a newly-acquired unit refused at step 6 with
`uImage-system md5 is 5642fd05969e366c58e930e51de48ccb, which is none of` the three it knows, and wrote
nothing. That refusal was **correct**, and the unit is not damaged or half-patched: `verify_uimage.py`
reports both CRCs valid and `power=0x32 (50) 100mA`, and there is no `uImage-system.vendor` beside it —
the writer creates that backup *before* it writes, so p1 was never touched. The cause is that the unit
ships a different Steelcase release (`SYSTEM_ANALYSIS.md#51-as-shipped`), so its kernel is a different
binary with a different md5.

**Why this does not scale, and what actually pins it.** `lib/rw-usbpower.sh` gates on **identity** —
`RW_UIMAGE_VENDOR_MD5` / `_POWER_MD5` / `_BOTH_MD5`, three hardcoded strings, plus the backup assertion in
`commissioning/commission-offline.sh`. Those strings are the *only* thing tied to one release. Two
measurements say so: `usb_host/patch_dtb.py` verifies the input's own header and data CRCs before
anything, **finds** the appended tree by `uimage.py`'s three-condition walk rather than trusting
`DTB_OFFSET_HINT`, and refuses unless the source byte reads exactly `POWER_VENDOR` — and it patched the
unknown release first try, at a different offset, with `verify_uimage.py --expect-power 0xfa` passing
afterwards. Meanwhile `tests/rw_usbpower_test.sh` already drives the whole apply/verify/revert sequence
with all three constants **overridden by md5s of its own fixtures**, so the sequence is proven
release-independent; only the production constants are not.

**Proposed fix — two tiers, with the second opt-in and never silent.** Tier 1 is today's md5 lookup,
unchanged. Tier 2 applies when that returns `unknown`: proceed only if the structural gate passes (both
CRCs valid ∧ the walk finds `power` inside a `usb_otg_hs` node ∧ that byte reads the vendor value), and
replace each md5 comparison with a measurement that is strictly stronger than the constant it retires —
read the actual `power`/`mode` property values for the current state instead of looking up an identity;
compare the card byte-for-byte against the patched file just produced locally instead of against
`_POWER_MD5`; and assert the backup matches what was read off the card *before* the write instead of
against `RW_UIMAGE_VENDOR_MD5`.

⚠️ **What tier 2 cannot buy, and the reason it must stay opt-in:** an md5 in that table also records that
somebody booted *that exact image* and the unit came back up. No structural check can establish that, and
a unit that does not come up has no serial console to say why. Whether a given kernel's MUSB honours the
property at all is a read of `musb_host.c:2797` in the vanilla 4.14.52 tree — the only tree available.

⚠️ **Two traps for the implementer.** `tests/measure_usbpower_sabotage.sh` `sed`-matches the *exact* line
`if [ "$got" != "$RW_UIMAGE_VENDOR_MD5" ]; then` inside the backup step; editing that line rots the
sabotage into a false negative rather than failing loudly. And the three constants must not simply become
a longer table — a table still has to be fed a new release before it helps, which is the defect.

**The alternative that retires the gate instead of generalising it, and that is why it lives here.** This
entry's md5 table exists for one reason: to protect the p1 write. An experiment that removes the write
removes the need for any gate at all, tier 2 included. Patch the **in-RAM** copy of the `usb_otg_hs`
`power` property through `/dev/mem`: verify it reads the vendor `0x32`, write `0xfa`, rebind, confirm
500 mA. That makes the whole fix an ordinary boot script and lets `--no-usb-power` go, with no p1 write
left to gate. One SSH session, no case-open. **It needs no new code** — `devmem_write` is a general
physical peek/poke and is already on the device at `/usr/local/bin/devmem_write`. ⚠️ **The whole
difficulty is finding the address**: the unflattened tree is early-boot allocated rather than a static
symbol, so nothing names where its `power` property lands, and that one unknown *is* the experiment.
⚠️ **This is not the sysfs override already recorded as failed** — `usb_host/README.md` keeps both, and
says which is which.

**Ruled out: shipping a prebuilt patched kernel as a release artifact.** It would not scale (obtaining
every release is the same table plus 5 MB of payload each) and it is not ours to publish — see
`LICENSE.md`.

**Not a blocker for anything today.** `--no-usb-power` commissions such a unit fully; the cost is that it
keeps the vendor's 100 mA budget, so USB peripherals on *that* unit stay limited.

**A same-release card restore is the other way out, and it worked** — the refusing unit was re-imaged by
`dd` from a whole-card capture of the reference unit and then patched, after which its p1 carries
`uImage-system` = `RW_UIMAGE_POWER_MD5` and `uImage-system.vendor` = `RW_UIMAGE_VENDOR_MD5` exactly, so
tier 1 accepts it with no code change. ⚠️ **That also pins what the three constants are: they are the
reference unit's release**, not the other release in the fleet
([`SYSTEM_ANALYSIS.md#51-as-shipped`](SYSTEM_ANALYSIS.md#51-as-shipped)). It is a workaround and not the
fix — it needs a card capture of a matching release on hand, and it replaces the whole card, so the
restored unit inherits the donor's `/etc/touch_calibration.conf` and needs recalibrating
([`SYSTEM_ANALYSIS.md#33-touch`](SYSTEM_ANALYSIS.md#33-touch)). ⚠️ **Only
p1 may be restored file-by-file** (FAT32, all regular files); any ext partition must go back with `dd`,
because a per-partition file copy of a live rootfs carries no symlinks and leaves the unit with no
`/bin/sh` and no boot sequence, on hardware with no serial console.

### F17. Bluetooth peripherals, and whether USB DMA is reachable — open, measured 2026-08-08

**The want:** a wireless game controller and a headset or speaker for ScummVM. The unit is PoE-wired, the
Xbox pad is wired, and the integrated speaker is poor
([§3.4](SYSTEM_ANALYSIS.md#34-audio)) — so every current option is a cable, and the one that carries sound
is the worst-sounding one.

⚠️ **The dongle is identified and the verdict is "one number decides it" — measured 2026-09-06.** The
operator's dongle is `0b05:1bf6` (ASUSTek; no model or chipset is published for that PID). `0x1bf6`
appears **nowhere** in `drivers/bluetooth/` in the 4.14.52 tree, but the dongle's USB class is
`e0-01-01`, which `btusb.c:75,81` match generically — so `btusb` binds it and `hci0` appears. ⚠️ **The
trap is that a generic match has `driver_info == 0`, so `btusb.c:3142` never takes the
`BTUSB_REALTEK` branch and `btrtl_setup_realtek` does not run at all** — no firmware or config download
happens, whatever the chip is. This `btrtl` knows five ROM subversions only (8723A, 8723B, 8821A, 8822B,
8761A); RTL8761**B**/BU, the likely chip, landed around 5.8, and there is no `hci_rev` lookup table yet.

**So it is a module build (`CONFIG_BT`, `BT_BREDR`, `BT_RFCOMM`, `BT_HIDP`, `BT_HCIBTUSB`,
`BT_HCIBTUSB_RTL`, `RFKILL` — all tristate, no image rebuild; `CONFIG_BT` is currently `n` at
`usb_host/device_config:1070`) that either just works or needs a `btrtl` backport, and the host cannot
tell which.** ⚠️ **Do not fetch a firmware file or source a second dongle before the number exists.**
Build and load the modules, then read `lmp_subver` — from `btrtl`'s own line if it runs, otherwise
`hcitool -i hci0 cmd 0x04 0x0001` bytes 7-8. `0x8723`/`0x8821`/`0x8761`/`0x8822` ⇒ proceed;
anything else ⇒ this dongle needs newer source than we have, and the safe substitutes are a CSR8510
(`0a12:0001`) or an ASUS USB-BT400 (`0b05:17cb`, Broadcom BCM20702).

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
as `CONFIG_INPUT_JOYDEV` was before the USB-host path (`usb_host/README.md`) shipped its three
modules — and that precedent worked. Every hard dependency is satisfiable, measured from
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
— but that path is now `common/audio_out` for every component, so the reroute has one home rather
than two. A2DP's ~100–200 ms latency is fine for point-and-click and wrong for anything twitchy. **The
controller half is much more likely to land than the audio half; do not sell them as one feature.**

**Can we get USB DMA?** Probably, but it is research with a worse failure mode than today's.
`# CONFIG_USB_INVENTRA_DMA is not set`, so `musbhsdma.c` is not compiled at all. ⚠️ **The
`CONFIG_DMADEVICES=y` / `CONFIG_TI_EDMA=y` that *are* set are a red herring** — that is the **system**
EDMA via dmaengine, not the Inventra engine inside the MUSB block that OMAP3 uses;
`CONFIG_USB_TI_CPPI41_DMA` (the dmaengine-based path) is unset and is for AM335x anyway. The lever is
`CONFIG_KALLSYMS_ALL=y`: every built-in symbol's address is readable at runtime, so a force-loaded module
could supply `musbhs_dma_controller_create` and `omap2430_ops.dma_init` could be pointed at it — the same
family as [F23](#f23-the-p1-gate-knows-one-firmware-release-and-refuses-every-other--open-measured-2026-09-02)'s
existing patch. ⚠️ **But today's noop stubs fail *safely*, falling back to PIO, whereas a misbehaving DMA
controller scribbles into RAM.** A kernel rebuild would be the clean way and is not impossible, only
ruled out on value ([§7](SYSTEM_ANALYSIS.md#7-kernel-policy)).

**Where the two questions do connect.** `CONFIG_SND=y` and `CONFIG_SND_USB=y` but
`# CONFIG_SND_USB_AUDIO is not set` — so a **wired USB DAC** is also one module build away, with no
encoding, no pairing and no latency, and it fixes the speaker complaint directly. But uncompressed PCM at
48 kHz stereo is ~190 KB/s over PIO, which is where DMA would start to pay. **BT audio: low bandwidth,
high CPU. USB audio: high bandwidth, low CPU.** If the goal is "sound that does not suck", the DAC is the
cheaper experiment; if it is "no cables", it is Bluetooth.

**Two cross-cutting constraints on any dongle:** it draws ~50–100 mA, which is marginal against the
current 100 mA budget — an *independent* argument for the 500 mA p1 power patch — plus the 802.3af
power budget and the case's total lack of ventilation slots
([`HARDWARE.md` §4](HARDWARE.md#4-unpopulated-and-expansion)).

---

### F11. One home for the host build prerequisites — open

**Two delivery modes, and only one of them has a toolchain.** *Delivery*: someone clones the repo,
puts a card in a reader, answers a few questions, puts the card back, and the device works — they may
never build anything. *Development*: we build and deploy onto an already-clean device. The offline
path serves the first, `deploy-all.sh` the second. This item is about making the second reachable on
a fresh machine.

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

⚠️ **The measured host inventory, and the shell each claim was measured in, live in `COMMISSIONING.md`
→ *The dev host*.** An installer for this must state which shell it is measuring — a prerequisite check
run from the wrong one reports the wrong answer — and `wsl.exe -e bash -lc` is the one that counts.

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

### F100. USB audio output — one out-of-tree module, no kernel rebuild — open, measured 2026-09-06

**The ALSA core is already built in, so a USB DAC costs a module and no app changes.** Measured on
`.188`: `/proc/asound/card0` is `[rw20]`, `/proc/asound/oss/sndstat` reports `Type 10: ALSA emulation`,
and no `snd*.ko` exists anywhere under `/lib/modules` because the whole stack is compiled in.
`usb_host/device_config` has `CONFIG_SND=y`, `CONFIG_SND_PCM=y`, `CONFIG_SND_SOC_TWL4030=y`,
`CONFIG_SND_OSSEMUL=y`, `CONFIG_SND_PCM_OSS=y`, `CONFIG_MODULES=y`, `CONFIG_MODVERSIONS=y` — and
`CONFIG_SND_USB_AUDIO` unset, which is the only gap. So `/dev/dsp` is an OSS shim over a live ALSA
core, and this entry does **not** reopen the declined native-ALSA-client decision: the driver is
consumed through the same shim, with no `libasound` linkage and no app rewrite.

**Build it the way `xpad` is already built** (`usb_host/build-xpad-module.sh`): vanilla tree,
`device_config` → `.config`, `modules_prepare`, then `M=sound/usb`. Four modules ship —
`snd-usb-audio`, `snd-usbmidi-lib` (same obj line), plus `snd-hwdep` and `snd-rawmidi`, which
`SND_USB_AUDIO` selects and the built-in kernel lacks. ⚠️ **After `olddefconfig`, assert
`CONFIG_SND_HWDEP=m` and `CONFIG_SND_RAWMIDI=m`, not `=y`** — resolved to `y` they drop out of the
module build while the running kernel has no such symbols, and `insmod` then fails on unresolved
symbols rather than on anything that names the cause. `depmod -a` on the device afterwards.

⚠️ **There IS app work, and the earlier "no app work" claim was wrong — measured 2026-09-06.**
`SND_DYNAMIC_MINORS` is unset, so OSS minors are static and card 1 lands deterministically at
`/dev/dsp1`, created by devtmpfs+udevd on plug exactly as `js0` was for `xpad` — but nothing on the
device can be pointed at it. `common/audio_out.c:481` defines the path as a compile-time macro
(`DSP_DEVICE`), opened at `:506`; there is no config lookup and no `getenv` anywhere in the file, and
`audio_out_open_oss()`/`OSS_DEV` take no path argument. So the vtable is the right scaffolding and the
seam is simply absent. ⚠️ **`oss_open()` also calls `enable_amp()` unconditionally at `:500`** (GPIO12
HIGH) — card 0's speaker amp, meaningless for a DAC, so that must become conditional on the path. ⚠️
**Both live in `audio_out.c`, which is on ScummVM's `OBJS` list, so this forces an all-three-component
redeploy** — price that in before starting.

**The dongle is identified: `0d8c:0014` C-Media Audio Adapter (Unitek Y-247A), reported by the operator
2026-09-06.** It is plain UAC1 to this driver — no descriptor, format, clock or endpoint quirk applies.
The only quirk that touches it is cosmetic: `sound/usb/mixer_quirks.c:1882` sets `min_mute` on any
"Playback" control for `0d8c:0014`, a mixer-scale detail. Four `.ko` are needed, not two —
`snd-usb-audio` and `snd-usbmidi-lib` from `M=sound/usb`, plus `snd-hwdep` and `snd-rawmidi` from a
**second** `M=sound/core` pass, because `SND_USB_AUDIO` selects them and the built-in kernel has
neither.

⚠️ **The unknown that could sink it is PIO cost, and it has never been measured.** MUSB DMA is
noop-stubbed and falls back to PIO; a 48 kHz stereo stream is ~190 KB/s, which F17 records as where
DMA would start to pay, and the baseline is 45 % of the one core for `samegame` over a music bed.
**Measure before writing code:** `lsusb` to identify the dongle, then `aplay -D plughw:1,0` with `top`
alongside. ⚠️ **Plugging USB is what provokes B33's babble storm**, which both hard-resets the unit and
invalidates anything measured during it — run `dmesg | grep -c musb_bus_suspend` first and treat a
non-zero count as "discard this measurement".

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
installer again.** The block is not the mount; `--base` needs no root.

**A second half-measure in the same vocabulary:** `provision.sh --dry-run` exits before the provision
step, so it previews the clean and the p1 write but never the install/link plan. The plan is compiled on
the host, so a full preview is cheap; nobody has asked for one.

### C15. The bare plan-ID scan collides with function-key names — open, measured 2026-09-03

`bare_sites()` in `tests/doc_check.sh` matches an `F`-numbered ID in parentheses or after `see`/`is`/
`was`, and F1-F12 are key names as well as plan IDs here — so the ScummVM key-table row
`Save/load dialog (F5)` counted as a citation. It resolved silently for as long as that heading
existed, then became a dangling citation the moment the entry was deleted; the tree was made green by
rewording the key row in `scummvm-roomwizard/README.md`. **So the reported citation count carries
false positives, and the next F-numbered entry to close will fail the gate on unrelated
documentation.** Narrow the scan rather than excluding a file: a hit on a line that also carries a
key-binding marker (`Ctrl+`, `Alt+`, `Shift+`), or one inside a two-column key table, is not a
citation. ⚠️ Needs a control in both directions — a real bare citation must still fire, and it must
fire in a file of the same kind, or the scan goes blind where it used to see.

---

## Out of Scope

Recorded so the decision is not re-litigated. Most of these need a kernel rebuild, which is ruled out
on **value, not feasibility** — the full rationale, the single blocking driver (`panjit_ts`) and the
per-symbol evidence are in [`SYSTEM_ANALYSIS.md#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy)
and [`#314-what-is-not-present`](SYSTEM_ANALYSIS.md#314-what-is-not-present). Requesting GPL source
from Steelcase has been explicitly ruled out.

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
| Native ALSA backend (the "ALSA port") | **Nothing** — it needs no kernel work and the userspace side is complete on a stock unit ([`#34-audio`](SYSTEM_ANALYSIS.md#34-audio)). Declined on **value**, and the reason is now settled rather than pending: `/dev/dsp` and the ALSA device are the same PCM, the only measured win is ~2× at the period, and no latency symptom has ever been reported. Both other arguments once recorded beside it are gone — *mixing* shipped in userspace, and the *frame arithmetic* lives in `audio_gen.c`, which a port would leave unchanged. The tinyalsa dependency, its build script and its licence rows were deleted with this decision; nothing in the tree prepares for it. **Revisit only if something we port needs ALSA.** | [`#34-audio`](SYSTEM_ANALYSIS.md#34-audio) |

**Note:** enabling **UART3** as a `ttyO2` is *not* in this table — it may be reachable by patching the
appended DTB, which needs no kernel source ([`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports)).

---

## Where to start

**This is the operator's ranking, set 2026-09-06, and it is the authority.** The tiers and their order
are theirs; the ⚠️ notes under each are what measurement has since added, not a re-ranking.

### Stability first

1. **F9** — the release path. ⚠️ **What is left of it is now mostly NOT engineering.** The precondition
   ordering and the device provenance stamp both landed 2026-09-07; the remaining named item is **blocked
   on non-engineering expertise** — whether the `NOTICE` written offer is discharged has never been
   checked by anyone qualified to say so, and `release.sh` guarantees bookkeeping rather than legality.
   ⚠️ **Measure a dependency's licence *version*** — this entry once said GPLv2+ and the ScummVM tree is
   GPL-3.0-or-later. What IS still engineering is small and feeds the tier below: `release.sh` has no test
   suite, and a single component's `build-and-deploy.sh` does not clear the stamp.
2. **B33** — the babble `printk` loop. It reboots the unit *and* silently invalidates anything measured
   during a storm, which makes it the one bug that corrupts other work. First step needs no device: read
   `musb_bus_suspend()` in `usb_host/linux-4.14.52/drivers/usb/musb/`.
3. **F23** — tier 2 of the p1 gate, so a unit on any other Steelcase release can take the 500 mA patch.
4. **F11** — one home for the host build prerequisites.

### Usability, features, maintainability

F2 (the biggest performance win available) · F100 (USB audio) · B35 · C1 · C4 · C6 with C7 · C2 · B30 ·
F4 · C5 · C8 · F17 · F6 · F14.

⚠️ **Measured 2026-09-06 — only two gates run before a deploy**, `check-arm-safe.sh` and
`check-audio-pacing.sh`, both blocking. No test suite runs from any build script, from `deploy-all.sh` or
from `release.sh`, so C6 and C7 are one task: a pre-deploy gate that runs the host regressions and
shellcheck beside the two that already block. **shellcheck is installed as of 2026-09-06**, so C7 is no
longer blocked.

⚠️ **Most of this tier is NOT gated on a kernel rebuild, measured 2026-09-06.** This repo already builds
and ships modules against the vanilla tree — `xpad.ko`, `joydev.ko` and `ff-memless.ko` are deployed — so
F17 and F100 are module builds, F6 is userspace `/dev/i2c-2` against a published register map, and F14's
cheaper option draws its splash in `app_launcher`, which already owns the framebuffer. **What genuinely
needs kernel work is short: enumeration reliability — making a cold port obtain a session without the
RESCAN tap — and MUSB DMA.** Anything else claiming to need
a rebuild should be checked against that list first. ⚠️ **F17's dongle reads as ASUS by vendor and Realtek
by chip, and 4.14.52's `btrtl` knows RTL8761A only** — RTL8761B/BU support landed around kernel 5.8 — so
read `lsusb`'s VID:PID before building anything.

⚠️ **F4 has been split and halved**, 2026-09-06: the analogue-paddle half is closed on
[`SYSTEM_ANALYSIS.md#8-hardware-policy`](SYSTEM_ANALYSIS.md#8-hardware-policy) and what is left is two
`cat`-able channels. ⚠️ **C5 is also two items**, and only one is cosmetic: `text_truncate()` takes no
destination size and one caller hands it a 48-byte buffer for a 128-byte device name, which is a stack
overwrite waiting on a geometry change. The 8px/6px centring is the cosmetic half.

### Nice to have

B29 · C9 · C10 · C15 · F8 · D7 · F13.

⚠️ **C9 is accepted rather than open for our own bundles**: we gate before stripping and the installer
says `TAKEN ON TRUST` in those words. The gap is third-party bundles, which do not exist yet.
⚠️ **C10's shape decision is REVERSED, 2026-09-06, by the operator**: a test-only command-line switch is
acceptable after all, superseding the 2026-08-10 ruling that only a pause-dialog entry would do. The
launcher passes no arguments, so such a flag is reachable over SSH and deliberately not from the panel —
which is what a test entry point wants. ⚠️ **D7's prescribed fix is insufficient, measured 2026-09-06**:
`libnss-mdns` is installed and in `nsswitch.conf`, avahi runs on the device, and `.local` still does not
resolve — WSL2 is NAT'd onto its own subnet and mDNS is link-local multicast, so it cannot cross. The fix
is `networkingMode=mirrored` in `.wslconfig`, which changes networking for every distro on the host and is
the operator's call. ⚠️ **F13's interim is one honest line in `COMMISSIONING.md`** stating the host
requirement; the real answer is a bootable image, and macOS cannot be tested from here at all.

**F7 is dropped, measured 2026-09-06.** High scores live at `/home/root/data/*.hig` on **p2** and survive
both re-commissioning paths by an explicit `keep base` rule; `build-and-deploy.sh` never touches that
tree. Only a whole-card reflash loses them. NAND would buy the card-swap case alone, and buy it with a
store that a reflash cannot clear. ⚠️ **The one caveat worth keeping: that whitelist matches `*.hig`, so a
future game storing anything else under `/home/root/data` is swept by the clean.**

⚠️ **Operator ruling, 2026-09-06: no further USB work beyond USB audio.** Enumeration-at-probe is closed
and that is a result rather than a gap — three mechanisms read out of the MUSB driver were each applied
and **refuted on hardware**, and the answer that ships is the one-tap RESCAN, verified on a panel. Before
anyone proposes a fourth theory, read the refuted table in `usb_host/README.md`, which names the one
never-attempted candidate and the question any candidate must answer first.

One device experiment remains optional rather than blocking, one SSH session and no case-open: F23's
in-RAM alternative to the p1 write, which would retire that gate rather than generalise it. Bundles hold
built artifacts only — settled, because the one consumer that installs device scripts runs from a clone
and has `device-files/` beside it either way.
