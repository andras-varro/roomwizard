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
- Kernel work is in scope; the image build is F101. What stays excluded is in [Out of Scope](#out-of-scope).

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

### B33. A stale `is_active` leaves a `printk` loop that hard-resets the device — open, **measured 2026-09-08**

⚠️ **An unbounded kernel message loop that outlives the device's removal and ends in a hardware reset
~46 min later, and it is also a measurement contaminant** — anything judged by ear or timed during a storm
was judged on a starved device, and a frozen app is a *symptom*, not the bug; an on-panel tool appearing to
hang is what surfaced this. **Run `dmesg | grep -c musb_bus_suspend` before trusting any on-device
measurement** and treat a non-zero count as "discard this measurement".

**The violated invariant: `musb->is_active` must be false in any non-connected OTG state** (`A_IDLE`,
`A_WAIT_BCON`). The `MUSB_INTR_DISCONNECT` switch's `default:` arm in
`usb_host/linux-4.14.52/drivers/usb/musb/musb_core.c:897-900` only prints `musb_stage0_irq 898: unhandled
DISCONNECT transition (a_idle)` and never clears it — the gadget-side twin does, at `musb_gadget.c:2105`.
`musb_bus_suspend()` (`musb_host.c:2588`) then reads `is_active` as "a device is attached and running" and
returns `-EBUSY`, and `WARNING()` is a plain `printk` with no ratelimit (`musb_debug.h:38-41`). The caller
is the **root hub's runtime-PM autosuspend**, which is unbounded in both directions:
`hcd_bus_suspend()`'s failure path carries no retry counter and no backoff, and `hub.c:1734` sets the hub's
`autosuspend_delay` to 0. ⚠️ `omap2430_ops` has no `.recover`, so `musb_platform_recover()` is a no-op on
this SoC — the AM335x software-babble workaround lives in `musb_dsps.c`, which is **not** this glue, so do
not reach for it.

⚠️ **A storm needs BOTH conditions, which is why it is rare: `is_active` stale AND the child device gone,
so the root hub actually attempts a suspend.** Each half was measured alone 2026-09-08 and neither stormed.
A clean unplug produces the `a_idle` warning but **retains** child `1-1` — no `usb 1-1: USB disconnect` is
logged at all, because the disconnect went unprocessed — so the root hub stays `active` and `bus_suspend`
is never called. A driver unbind+bind with an empty port removes the child but re-allocates `musb` with
`is_active` clear, so the root hub **suspends successfully**; that one doubles as a positive control
proving the instrument can report a healthy suspend. **The remaining untested condition is a device that
raises `CONNECT` but never enumerates**, which is what the Aug 13 storm below shows
(`xpad … usb_submit_urb failed with result -19` beforehand) and which needs a marginal connection to stage.

⚠️ **Babble is neither necessary nor sufficient, so do not treat it as the cause.** `.188`'s persistent log
— `/home/root/log/messages` on p3, which keeps the previous boot's tail across a reset — holds more
`musb-hdrc: Babble` events than storms, and its Aug 13 storm has no babble within three days of it; check
with `grep -ci babble` and `grep -c musb_bus_suspend` on that file. The Aug 17 storm is the one that fits
the old story: `Babble`, then `usb 1-1: USB disconnect, device number 2`, then the warning repeating,
collapsed by syslog as `last message buffered 1237959 times` in a 10-minute window (≈2060/s), running 46
min to a reboot. `FAT-fs (mmcblk0p1): Volume was not properly unmounted` next boot says hard reset, not a
clean `reboot`, and **the reset is the hardware watchdog** starved of CPU past its 60 s feed —
`/usr/sbin/watchdog` carries no check directives, so it decided nothing. ⚠️ On a unit still holding the
vendor soft watchdog, a reset cannot be attributed to the hardware one: check which watchdogs are running
before drawing that conclusion.

**Recovery is not a reboot** — a driver unbind+bind ends a live storm, which is how Aug 13's stopped in
~20 s. **The fix is a driver patch, folded into F101**, and cannot ship before an image we built boots.
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

Userspace except F101, which is the image build.

### F2. Use the DSS overlay planes — open, **biggest performance win available**

**What compositing costs today, measured 2026-08-31 and *accepted* rather than filed as a fault:**
`samegame` tapping over a music bed runs at **45 % CPU** on the one 600 MHz core, ScummVM playing *Full
Throttle* at **12–13 % CPU / 5.4 % memory**. 45 % is what an overlay composite has to beat. ⚠️ **Not the
same quantity as the 32 % below** — a game mixing while it redraws, against O1–O12's endpoint.

Three hardware overlay planes — **two of them with a scaler** — plus z-order, global alpha and
colour-key, sitting unused. On a GPU-less 600 MHz part this is the only graphics acceleration that
exists, and it is pure sysfs — no kernel work. Inventory, the live sysfs dump and the legacy-omapdss
caveat: [`SYSTEM_ANALYSIS.md#32-display`](SYSTEM_ANALYSIS.md#32-display).

Suggested order. ⚠️ **The mechanism is proven and the recipe measured** — `vid1` upscaling
400×240 → 800×480 full-screen over the running app, four writes, no reboot and no boot parameter. That
also corrected two things the steps below used to assert: `overlay0` (`gfx`) **cannot scale at all**,
and `zorder` is **not writable** on this SoC. Recipe, scaling limits and the driver citations are in
the display section linked above.

1. **Take the CPU win.** Render one game at 400×240 into `fb1` on `vid1` rather than 800×480 into
   `fb0`, and measure against the 45 % above — a quarter of the pixel fill cost for the same visual
   size. This is the step that pays; the rest are bonuses. It needs a seam in `common/framebuffer.c`
   that can target `fb1` at a smaller geometry, then one game, then ScummVM and the VNC client.
2. **HUD plane.** Put the unscaled HUD on `gfx` (`overlay0`) and the scaled game on `vid1` — or use
   `vid2`, which is interchangeable with `vid1` and also scales. `global_alpha` works; `zorder` does
   not, so the fixed GFX < VID1 < VID2 order decides what is on top and the layout must suit it.
3. **Colour-key transparency** via `trans_key_enabled` for zero-CPU sprite masking.
4. **Video playback**, speculatively — `/dev/video0` accepts YUV with hardware colour-space
   conversion. Furthest from proven of the four, and the boot-time `omap_vout: failed to allocate DMA
   Channel for video-1` may be exactly what blocks it.

⚠️ **Verification is operator-in-the-loop.** `cat /dev/fb0` returns the gfx plane's memory, not the
composited panel, so no screenshot can see an overlay — say so in any checklist this work produces.

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
controller scribbles into RAM.** The clean way is the two config symbols in an image we build — fold it
into F101 rather than extending the runtime patch ([§7](SYSTEM_ANALYSIS.md#7-kernel-policy)).

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

### F13. Commissioning from Windows without WSL, and from macOS — open, unsolved

**Delivery** — someone clones the repo, puts a card in a reader, answers a few questions, puts the card
back, and the device works, never building anything — assumes the operator can run the card path. Today
that means Linux, or Windows with WSL2. This entry exists so the gap is recorded rather than discovered
by someone holding a card.

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

### F100. USB audio output — one out-of-tree module, independent of F101 — open, measured 2026-09-06

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

### F101. Build our own 4.14.52 image — open

**The deliverable is a `uImage` we compiled, staged on p1 beside the vendor's.** The policy and the
standing costs are [§7](SYSTEM_ANALYSIS.md#7-kernel-policy); this entry is the work. The tree is
`usb_host/linux-4.14.52/`, already configured from the device's own `/proc/config.gz` by
`build-xpad-module.sh` and already **measured** producing modules that load on the device.

**What the image is for — the payoff is deployment stability, not speed.** A kernel compiled here ships
with its own corresponding source and can go in a release, which is what retires the `/dev/mem`
byte-patch route into p1; that in turn dissolves F23's per-firmware pattern gate and gives back the free
undo both bring-up paths lost. ⚠️ **None of that is delivered until an image we built is booting on a
unit**, so F23 stays open and the byte patch stays shipped meanwhile — do not delete either on the
strength of this entry.

**What to fold in, so the image is built once with everything wanted in it:**

| Wanted | Change | Note |
|---|---|---|
| Touch | a `panjit_ts` equivalent, written from the published Cypress register map ([`#33-touch`](SYSTEM_ANALYSIS.md#33-touch)) | **the one blocker.** Build it as an **out-of-tree module** beside `xpad.ko`, not into the image — this kernel force-loads modules, and keeping a vendor-shaped driver out of the image keeps the image cleanly ours to publish. Decided 2026-09-07 |
| MUSB DMA | `CONFIG_USB_INVENTRA_DMA` set, `CONFIG_MUSB_PIO_ONLY` unset | a genuine build defect; retires the runtime patch |
| Scheduling | `PREEMPT`, `HZ=250` | config-only, and never measured to limit anything — include it, but do not justify the image with it |
| USB gadget mode | `CONFIG_USB_GADGET` | config-only: the micro-B socket is already the one physical port |
| Enumeration | a **driver** change in `drivers/usb/musb/` | ⚠️ not a config option ([`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy)). The image makes it *possible*; it is separate work, and B33 is the other half of that driver's story |
| Disconnect cleanup | clear `is_active` in the `default:` arm of `musb_core.c:897-900` | **the fix for B33**, whose entry holds the invariant and its measurement. A driver change like the row above, so the image is what makes it shippable |

**Verification is the expensive half, and it needs a plan before the first build.** A bad image costs a
card pull and a reimage by hand, and nothing on the device says why it failed
([`#4-boot-chain-and-recovery`](SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery)). ⚠️ **Stage every
experimental image under a NEW filename and leave `uImage-system` in place**, so the working boot
survives a bad one. Whether to fit the `P4` console header — the only channel that shows a boot log, and
a board change the hardware policy currently bars — is an open question for the operator.

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

⚠️ **Touch injection does not work and cannot be made to work on this device** (no `/dev/uinput`;
evdev's `write()` is the output-event path). The rule and the evidence are in `CLAUDE.md` →
*Non-obvious constraints*. **This invalidates the touch half of anything built on injection, so read
it first.**

One piece remains — the host-gcc regressions over the pure-logic parsers are done and gated, and so is
the absent-fixture guard that stopped `tests/rw_provision_test.sh` reporting a missing stub as a subject
defect:

**Write the first-screen smoke harness.** SSH-launch a binary, `cat /dev/fb0`, decode with
`fb565_to_png.py`, and inspect the screen drawn before any input: `assert not-all-black`,
`assert alive after 2 s`, across all ~15 binaries. That is a real smoke test and it has caught real
defects when done by hand. Anything past the first screen needs a tap-by-tap checklist for a human
instead. ⚠️ **`assert not-all-black` is nearly vacuous on its own** — assert a minimum count of
distinct pixel values, take the depth from `fbset | grep geometry` on the device rather than assuming
32bpp, and keep *did not start* / *started and died* / *black screen* / *harness could not tell* as
separate outcomes, because silence is not success. It needs a device to **run** but not to **write**:
prove every branch on the host with an `ssh` stub on `PATH`, the way `tests/rw_provision_test.sh` does.

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

### C7. Burn down the shellcheck backlog — open

The shell scripts *are* the deployment system and they run as root over SSH. `tests/run-all.sh` phase 3
now runs `shellcheck` over every tracked script before any deploy or release, in two tiers, so **nothing
new can be added** — the tiers, the ratchet and the `SC1124` trap are in `tests/CLAUDE.md`. What is open
is the backlog that was already there when the gate landed, recorded one row per `(file, code)` in
`tests/shellcheck-baseline.txt`. `sort -k3 -rn tests/shellcheck-baseline.txt | head` puts the worst files
first; `scummvm-roomwizard/manage-scummvm-changes.sh` leads it, then
`probes/xbee_socket_continuity.sh`, then `scummvm-roomwizard/build-and-deploy.sh` and
`commissioning/card-prep.sh`. ⚠️ **`native_apps/build-and-deploy.sh` is done and is no longer a
leader** — its `SC1091` and `SC2162` rows reached zero and were deleted, and what is left there is
deliberate: word-splitting that a rewrite cannot exercise, client-side expansion of local constants
that `SC2029`'s remedy would break, and deploy-path `ls` sites free at the next real deploy.

Three things to know before starting:

- ⚠️ **A `# shellcheck` comment whose first word is followed by anything but a valid directive makes the
  tool exit 1 having analysed NOTHING in that file.** It reads exactly like a clean run that found one
  small problem. A directive in front of a single `case` branch is enough to do it — put it in front of
  the whole function. ⚠️ **Prose is enough to do it too**, and that is the easy way in: a comment
  explaining a directive, opening with the checker's own name, is parsed *as* a directive and reports
  `SC1072`/`SC1073` while suppressing every real finding. Measured: a file's count fell from 69 to 2
  that way. ⚠️ **So a small post-patch total is not evidence of success — the expected distribution per
  code is**, and a voided file has almost none of it. Never open an explanatory line with that word.
- **Prefer a fix that changes no behaviour to a `disable=` directive**, which is why no shipped script
  carries one for this backlog. The two `error:`-severity findings that existed are gone that way: they
  were `$key[` inside `"…$key[[:space:]]…"` in `lib/rw-provision.sh`, which shellcheck reads as a botched
  array expansion where the code is in fact correct, and `${key}` braced says so.
- ⚠️ **`-x` does not help, and `# shellcheck source-path=SCRIPTDIR` does.** Measured: `-x` over the
  whole tracked set leaves the output byte-identical. What clears `SC1091` is that directive, because
  the tool resolves a `source=` path against its **own** working directory — the repo root when the
  gate runs — and `SCRIPTDIR` makes it the script's instead. It is not a `disable=`; it makes the tool
  analyse *more*. ⚠️ **Placement is the whole trick and it is file-wide only above the first command**:
  negative controls measured in place gave four findings with the directive removed and four with it
  moved below `set -e`, zero only above. The worked example, with that mechanism written out, is at the
  top of `native_apps/build-and-deploy.sh`.
- ⚠️ **Measure in the gate's shape — every tracked script at once, from the repo root — never one file
  alone.** The same file checked alone reports four `SC1091` the gate does not, because the gate passes
  the sourced libraries as inputs too. An "alone" measurement contradicted the rule above and cost real
  time before the gate-shape run settled it. `shellcheck -f gcc $(git ls-files -- '*.sh')` is the form.

Do not "fix" a finding by rewriting a line you cannot exercise. Several of the leaders are in build
scripts that only a real deploy runs.

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

### C12. Offline commissioning has never been run against a real disk — open

`tests/commission_offline_test.sh` contains no `--dry-run`, so each of its cases already **is** a real
`--base` pass — the clean, `card-prep.sh`, the provision plan, the install and the verify all run for
real on a fabricated card tree. `--base` cannot locate p1 by construction, so the p1 gate, backup,
patch, verify and rollback are the one unexercised half, and reaching them needs a physical card in a
reader. ⚠️ **Not feasible on this dev host — it has no card reader, and the USB-reader route is closed
to us.** The entry stays open as a known gap in the delivery path, not as work anybody can pick up here.

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

Recorded so the decision is not re-litigated. ⚠️ **A kernel rebuild is no longer a reason to be in this
table** — the image build is F101, and a config symbol on its own no longer blocks anything. What keeps
these rows here is the *board*: anything needing a connector, a populated device or a wire is out under
[`#8-hardware-policy`](SYSTEM_ANALYSIS.md#8-hardware-policy). Requesting GPL source from Steelcase stays
ruled out, and is needed for nothing
([`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy)).

| Item | Blocked by | Detail |
|------|---|---|
| Enable the two EHCI USB host ports | **Nowhere to plug in** — no second USB connector and no unpopulated footprint on the board, so `CONFIG_USB_EHCI_HCD` is not what blocks this and an image we build gains nothing here | [`#36-usb`](SYSTEM_ANALYSIS.md#36-usb) |
| SPI | Four controllers `okay` in the DT and `CONFIG_SPI` unset, but **nothing is on the bus** — no children are declared, and putting a device there needs a wire | [`#314-what-is-not-present`](SYSTEM_ANALYSIS.md#314-what-is-not-present) |
| Piezo buzzer on TWL4030 PWM | **Needs a wire**, and all 3 dmtimer PWMs are taken; `CONFIG_PWM_TWL` is the cheap half | [`#39-i2c`](SYSTEM_ANALYSIS.md#39-i2c) |
| Mainline 5.x/6.x port | **Closed on DRM/KMS, not on effort:** `omapdrm` would break the runtime bpp switching ScummVM and the VNC client depend on, lose the DSS overlay sysfs, and cost RAM. Building 4.14.52 ourselves (F101) is the opposite decision and keeps all three intact | [`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy) |
| Ambient-light sensor / auto-backlight | **No such hardware.** The teardown found no sensor and, decisively, no aperture, window or light pipe anywhere in the enclosure — a sensor would have nothing to sense even if fitted. ⚠️ Do **not** probe for it: `pv02_app 5` can hang I2C bus 1, which carries the PMIC. *Time-of-day* dimming needs no sensor and is still available. | [`#39-i2c`](SYSTEM_ANALYSIS.md#39-i2c) |
| Serial console | Located and pinned out (`P4`), then declined: the recovery loop is *pull the card, reimage, DHCP, SSH*, and since NAND and U-Boot stay untouched the card **is** the entire failure surface. Serial would add boot visibility, not recovery capability. Revisit only if NAND or U-Boot ever get written — or once we are iterating on our own images (F101), where serial is the only channel that shows *why* one failed to boot, though fitting `P4` is itself a board change. | [`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports) |
| Native ALSA backend (the "ALSA port") | **Nothing** — it needs no kernel work and the userspace side is complete on a stock unit ([`#34-audio`](SYSTEM_ANALYSIS.md#34-audio)). Declined on **value**, and the reason is now settled rather than pending: `/dev/dsp` and the ALSA device are the same PCM, the only measured win is ~2× at the period, and no latency symptom has ever been reported. Both other arguments once recorded beside it are gone — *mixing* shipped in userspace, and the *frame arithmetic* lives in `audio_gen.c`, which a port would leave unchanged. The tinyalsa dependency, its build script and its licence rows were deleted with this decision; nothing in the tree prepares for it. **Revisit only if something we port needs ALSA.** | [`#34-audio`](SYSTEM_ANALYSIS.md#34-audio) |

**Note:** enabling **UART3** as a `ttyO2` is *not* in this table — it may be reachable by patching the
appended DTB, which needs no kernel source ([`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports)).

---

## Where to start

**This is the operator's ranking, re-set 2026-09-08, and it is the authority.** The tiers and their order
are theirs; the ⚠️ notes under each are what measurement has since added, not a re-ranking.

### Stability first

Clear. The tier's one item — a single home for the host build prerequisites — is done:
`setup-build-env.sh` at the repo root carries the package set, and the component scripts now report a
missing tool and point at it instead of each reciting its own `apt` line.

### Usability, features, maintainability

F2 (the biggest performance win available) · F100 (USB audio) · B35 · C1 · C4 · C6 with C7 · C2 · B30 ·
F4 · C5 · C8 · F17 · F6 · F14 · F23 (scoped for kernel stability) · B33 (its fix is a driver patch, so it
waits on F101).

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
