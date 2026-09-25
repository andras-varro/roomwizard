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
~20 s. **The fix is a driver patch, folded into F101**, and cannot ship before an image we built is the deployed one.
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

### B36. Nothing recovers an output device unplugged mid-playback — open, confirmed 2026-09-09

Surfaced by the USB-audio work rather than by a report: exercising the dongle end to end is what put a
hand on a cable mid-playback. Pull a USB DAC while sound is playing and audio goes silent and stays
silent: no error surfaces, nothing
falls back to the panel speaker, and `audio_live()` keeps reporting the vanished device healthy. Operator
-confirmed at the panel and **acceptable to them**, so this is quality, not function — but the silence is
permanent for the life of the process.

⚠️ **`dsp_reopen()` (`common/audio.c`) already performs the entire fallback and its own comment says so**
— it re-resolves the device path, so both `usb` and `auto` fall back to `/dev/dsp` and GPIO12 is
re-enabled for it. What is missing is a caller from the write path; its only three callers reach it from
an already-closed state.

**Three signals are dropped, and a fix must score all of them**, because which one fires first on a real
unplug is **not established**:

- `audio_pump()` returns bare when `SNDCTL_DSP_GETOSPACE` fails, so on the non-continuous path the stale
  fd never reaches `write()` and every counter downstream **freezes** rather than climbing. A frozen
  `pump_starved` while sound is expected is the current unrecorded signature.
- Both sinks compare `errno` to `EAGAIN` and nothing else, so `ENODEV`/`EIO`/`EINTR` collapse into
  `audio_gen.h`'s deliberately lossy `sink_error` — which the old path discards outright and the
  continuous path counts into `audio_out_sink_errors()`, **read by nobody in the repo**.
- ⚠️ **A third counter, missed by the first reading of this:** `audio_out_service()` reads the device's
  `space()` *before* it writes and answers `-1` with `refused++` when that fails, and `cont_service()`
  discards that `-1`. So on the continuous path `refused` is the counter most likely to move first, and a
  detector watching only the write could never fire at all.

No device-lost state exists either: `available` is never lowered by anything on the write path.

**Fix shape** — a consecutive-fault score feeding the existing fallback, on **both** paths, and it must
`close()` first: `dsp_reopen()` overwrites the fd without closing, and on the continuous path the fd
belongs to `AudioOut` whose one-per-process interlock refuses a second open. ⚠️ **A threshold of 1 is
wrong and the reason is measurable:** a spurious fire on the continuous path costs a bounded-but-real
`audio_out_close()` drain — ring plus period, ~0.79 s at 44100/2ch — **inside a render loop**, plus the
reopen's prefill. `EAGAIN` cannot reach the score at all (`audio_write_frames()` services a full sink
before `sink_error` is reachable), so the threshold is buying tolerance of a one-off `EINTR`/`EIO`, not of
load. ⚠️ **And a failed recovery must be re-armed**, or lowering `available` mutes the app permanently —
a worse silence than the defect.

⚠️ **Reclassifying `EINTR` as retryable belongs in a SEPARATE commit**: that edit is in `audio_out.c`,
which ScummVM links and `audio.c` is not, so it reaches a stream that has been ear-verified since
2026-08-03.

**Gate:** the decision logic is fully host-testable in `audio_tone_test.c` — the only host test linking
`common/audio.c` — including a fake device that stops answering, and it needs a healthy-device control
serviced many times and never taken back, or an inverted score would pass every positive case while
tearing the stream down every frame. ⚠️ **Two things no host test can reach**, and they are the panel's
job: that a real unplugged DAC *fails* `GETOSPACE`/`write` rather than hanging or succeeding with zero
bytes, and that the fallback is heard. ⚠️ That file is also built and run **on the device**, where the
recovery really succeeds — so any assertion about `available` or voice count passes on the host and fails
on ARM.

## Features

Userspace except F101, which is the image build, and F2, which now waits on it.

### F2. Use the DSS overlay planes — open, **gated on a kernel build; waits on F101**

⚠️ **Reclassified 2026-09-11 by the operator: what is left of this entry needs a kernel image, and no
UI change is wanted.** *"We should keep what we learned, and tools, documented, but move the F2
together with the other 'needs kernel compile' bucket. No changes are needed on the UI."* The heading
used to read *biggest performance win available*; the userspace win was measured and then **rejected on
image quality** — the A/B and the ruling are below. One config-only kernel item and one coefficient
patch are what remain, and both fold into F101.

**What compositing costs today, measured 2026-08-31 and *accepted* rather than filed as a fault:**
`samegame` tapping over a music bed runs at **45 % CPU** on the one 600 MHz core, ScummVM playing *Full
Throttle* at **12–13 % CPU / 5.4 % memory**. 45 % is what an overlay composite has to beat. ⚠️ **Not the
same quantity as the 32 % below** — a game mixing while it redraws, against O1–O12's endpoint.

Three hardware overlay planes — **two of them with a scaler** — plus z-order, global alpha and
colour-key, sitting unused. On a GPU-less 600 MHz part this is the only graphics acceleration that
exists. ⚠️ **Reaching it was assumed to be pure sysfs. Measurement refuted that**: the userspace half
works and lost on image quality, and every remaining step is kernel-side. Inventory, the live sysfs dump
and the legacy-omapdss caveat: [`SYSTEM_ANALYSIS.md#32-display`](SYSTEM_ANALYSIS.md#32-display).

Suggested order. ⚠️ **The mechanism is proven and the recipe measured** — `vid1` upscaling
400×240 → 800×480 full-screen over the running app, four writes, no reboot and no boot parameter. That
also corrected two things the steps below used to assert: `overlay0` (`gfx`) **cannot scale at all**,
and `zorder` is **not writable** on this SoC. Recipe, scaling limits and the driver citations are in
the display section linked above.

1. **Take the CPU win — measured, and it is real.** ⚠️ **The seam the step used to ask for already
   exists**: `fb_init(fb, device)` takes the node as a parameter, accepts whatever geometry it finds and
   never sets one, so nothing in `common/framebuffer.c` needs changing — and every game already reads
   `argv[1]` as its framebuffer. **Rendering the same scene into a 400×240 surface on `vid1` costs
   5 510 µs of CPU per frame against 26 075 µs at 800×480 on `fb0` (37 → 179 fps), and enabling the
   overlay costs nothing measurable.** Instrument: `native_apps/tests/fb_plane_bench.c`, device-only and
   hidden from the grid, whose scene is defined in fractions of the surface so the work scales with area
   — it prints its own pixel count as the receipt, and two runs whose counts are not in the ratio of
   their areas are not an A/B. Four points on `.188`, 200 frames each: `fb0` 800×480; **`fb1` funded to
   the same 800×480 as a device control, which came within 0.4 %** — so the win is the pixel count and
   not the node; `fb1` 400×240 with `vid1` disabled; and the same with `vid1` upscaling to 800×480 and
   `gfx` switched off, which moved nothing. Per-Mpixel store cost also *fell* 9 % on the smaller surface,
   so the gain slightly exceeds the area ratio. ⚠️ **The remaining work is per-game re-tuning, not
   plumbing**, and it is where the effort now goes.
   - **The shared conversion exists and is in use, 2026-09-11.** `fb_scale_ui_px()` in
     `common/framebuffer.c` converts a DESIGN pixel count into surface pixels by the surface:panel
     ratio, with `fb_ui_px_x()`/`fb_ui_px_y()` over the published geometry; `fb_apply_viewport()` now
     also publishes `screen_true_panel_width`/`_height`, a pair separate from the surface-valued
     `screen_panel_*` so touch keeps its meaning. Converted so far: the six `BTN_*_WIDTH`/`_HEIGHT`
     constants and the six `LAYOUT_*` insets in `common/common.h`, and `FB_TOUCH_INSET_MAX`, whose flat
     48 was ~10 % of 480 rows but 20 % of 240. ⚠️ **The ratio must be surface:panel and not a fixed
     800×480 reference** — portrait is 480×800 at full resolution and a fixed reference shrinks every
     control there to 60 %. Bit-exact no-op at both shipped geometries, so no device changed behaviour
     and nothing is owed at the panel for this part. Host test `native_apps/tests/ui_scale_test.c`,
     30 assertions in 8 groups, in the gate. ⚠️ **Group 8 drives the wrappers and is not optional**:
     measured, a fixed-reference wrapper and a transposed wrapper each fail group 8 and nothing else,
     one assertion apiece, because every other group hands the ratio in as a parameter.
   - ⚠️ **The subject is ScummVM, not the seven games — re-scoped 2026-09-11 on the operator's
     challenge, and the entry had this backwards.** Two measurements decide it. First, **nothing on this
     device is frame-limited**: the target is `FRAME_DELAY_ACTIVE_US` 33 333 µs, the heaviest 800×480
     scene above costs 26 075 µs — inside a 30 fps budget with ~7 ms spare — and no shipped app has a
     recorded performance complaint or a measured fps figure at all, with whole-session audio counters
     reading `starve=0 lost=0 drop=0 lim=0 clip=0` and one `starve=1` ever. Second, the *direction* of
     the trade is opposite in the two places. **The seven games are authored at 800×480, so a reduced
     surface DOWNSAMPLES their art** to buy CPU nobody needs. **ScummVM keeps the engine's own 320×200
     and upscales it in SOFTWARE today**, so a reduced surface REMOVES work on art that was already that
     size: `initSize()` stores the engine resolution verbatim, `getWidth()`/`getHeight()` return it
     rather than the panel, and `blitGameSurfaceToFramebuffer()` resamples it nearest-neighbour with an X
     lookup table and identical-row dedup. ⚠️ **"800/320 is 2.5 and 480/200 is 2.4, both non-integer" is
     MEASURED FALSE as a description of the shipped path, 2026-09-11** — corrected here rather than
     restated. `getScalingInfo()`:163-171 takes **one isotropic scale**, the smaller of
     `rectW*256/w` and `rectH*256/h`, and centres the picture inside the content rect; the leftover
     strips are memset to black at :455-478. So ScummVM's two axes are **always equal** and its output is
     **pillarboxed**: 320×200 into a full 800×480 is **767×479 at (16,0)** (≈2.397×), and into the
     *default* content rect — the safe rect, `rwFullContentArea()` defaults to `"safe"` — with the 15/15
     bezel it is **720×450 at (40,0)**, an exact **2.25×**. The resample is still non-integer, so it still
     doubles some columns and triples others, and the comparison is still *uneven nearest-neighbour
     versus filtered* rather than sharp versus soft. What changes is the target a hardware arm has to
     hit: **a pillarboxed 720×450 at an offset, not a full-screen 800×480 stretch**, and the exact figure
     is a function of the runtime bezel and touch insets rather than a constant. The backend's own
     O9 row already names ScummVM the prime candidate. ⚠️ `vnc_client` is **not** a candidate: it
     *downscales* from a larger remote, so the DSS would need a framebuffer the size of the remote
     desktop to scale from.
   - **The instrument that decides it: `native_apps/tests/dss_scale_ab.c`**, built as step 37/37,
     deployed, hidden from the grid, and linking nothing from `common/` because `fb_init()` would apply a
     bezel viewport it must not have. Three modes over one synthetic 320×200 card — 1 px and 2 px column
     and row combs, diagonals, 1 px rings, four ramps, hard-edged blocks, and a 1 px border so a crop is
     seen rather than deduced. `soft` runs the software resample on `fb0`; `hard` puts a 320×200 `fb1`
     under `vid1` at 800×480; `split` shows **both arms at once**, the card's top half only at
     320×100 → 800×240, which is the *same* 2.5×/2.4× pair — shrinking the output without shrinking the
     input would have compared two different scale factors and answered nothing. `--swap` moves the
     hardware arm to the bottom and is the **viewing-angle control**: without it "the top looked softer"
     is not yet a statement about the scaler, and it refuses rather than reporting a swap that
     `overlay1/position` would not accept. ⚠️ There is deliberately **no `--ppm`**: the only source that
     would settle the question is a frame out of a running engine, and a grab off `fb0` today is the
     800×480 *output* of the software arm, i.e. already arm A.
   - ⚠️ **Measured on `.188` 2026-09-11, before any run: `vid1` is `enabled=0` and `fb1/size` is `0` —
     the plane is wired but UNFUNDED — while `output_size` still reads a stale `800,120` and `input_size`
     `400,240` left by the earlier eye run.** So funding `fb1` is a step and not a given. `overlay1` also
     carries no `trans_key*` attribute of its own, which step 3 below has to reckon with.
   - ⚠️ **The instrument was repaired before it was ever trusted, 2026-09-11 — it is now DEPLOYED on
     `.188` and HAS BEEN RUN; the verdict is the bullet below.** It is not the tool the bullet above
     describes.
     Five defects could each have produced a confident wrong eye verdict. `set_mode_565()` discarded the
     `FBIOPUT_VSCREENINFO` writeback, so a clamped resolution or a 32bpp grant read as success while
     every RGB565 store and the `line_length / 2` stride stayed 16bpp — garbage on the panel, indistinguishable
     from a scaler artefact; it now compares the grant against the request and refuses. `position` was
     written only when `pos_y != 0 || want_swap`, so `hard` and unswapped `split` inherited a previous
     `--swap` run's `0,240` while announcing `y=0`; it is now written unconditionally and refuses if it
     cannot reach `0,0`. The undo restored only `enabled` and `fb1/size`, never `output_size` or
     `position` — which is what left the stale `800,120` above, i.e. the tool manufactured the state that
     corrupts its own next run; both are now saved, restored and printed in the receipt. Three inline
     copies of the plane mapping ignored both `ioctl` returns and fed a possibly uninitialised
     `line_length` to `mmap` as a length, and none blacked the stride tail, whose stale contents the
     hardware arm would upscale onto the panel; one `map_plane()` helper now does all three. And `--swap`
     was accepted in every mode, printed as `swap=1` in the banner, and applied only by `split`.
     ⚠️ **The mode-acceptance refusal cannot be seen firing from here** — nothing available makes the
     driver grant something other than what was asked — so it is the one fix that stays unvalidated.
     ⚠️ The header's own receipt figure was **false**: it cited `write 128000, read 131072` while the code
     rounded to a page *before* writing, so request and readback were always identical and the
     page-rounding line always described a no-op. The exact byte count is now written, which is what
     makes the documented rounding something the receipt can show.
   - **THE EYE A/B IS RUN. Its ruling was "make it selectable" rather than "replace" — operator,
     2026-09-11 on `.188`. ⚠️ That switch was then WITHDRAWN by the operator; the real-art A/B below
     killed the hardware arm at both reachable ratios, so nothing is selectable and no config key
     exists.** Four runs in the prescribed order with the launcher stopped, each restore
     printed: `hard`, `soft`, `split`, `split --swap`. Verbatim: `hard` — *"test screen pattern. low
     res"*; `soft` — *"low res image, but very sharp"*; `split` (hardware top) — *"top is low res,
     blurry, bottom is low res sharp"*; `split --swap` (hardware bottom) — *"top is low res sharp,
     bottom is low res blurry"*. **The blur followed the hardware arm when the halves were exchanged, so
     viewing angle and any fixed panel asymmetry are ruled out** — that is what `--swap` was built for
     and it earned its place. Cost, from the tool's own receipts: hardware **92–211 µs/frame**, software
     **6 407–12 912 µs/frame** — the software resample spends ~12.7 ms of a 33 ms frame budget where the
     hardware spends ~0.2 ms. The position fix was also seen working rather than assumed: `position` was
     hand-poisoned to `0,240` first, the receipt shows it corrected to `0,0` and restored to `0,240`
     after, and the page-rounding line finally described something real (`wrote 128000, driver reads
     131072`). ⚠️ **The operator declined the framing "is the blur a problem":** *"I don't say that the
     blurryness is a problem. In older games it can be a blessing."* **So the deliverable stopped being a
     replacement** — and the selectable path it became was itself **withdrawn** once real art was run;
     see the withdrawal below.
   - ⚠️ **What that eye run does NOT settle, and the real-art prediction it produced — kept here
     because it was REFUTED, and the refutation is below.** A card of 1 px and
     2 px combs is the **best possible case for nearest neighbour and the worst possible case for a
     filter**: NN duplicates pixels so a comb stays a comb, while any filter averages it toward grey.
     ⚠️ **The prediction that followed is measured FALSE**: *"real SCUMM art carries no 1 px combs —
     painted backgrounds, dithered gradients and diagonal edges are where NN's uneven column doubling
     reads as wobble and a filter's softening reads as smoothing, so the two could rank the other way
     round on game art."* They rank the same way, harder. ⚠️ **And the card is 4%
     anisotropic where ScummVM is not** — see the geometry correction above. Both arms carry it
     identically so the A/B is not biased, but it is the likely source of the operator's unprompted *"the
     bottom one also felt a bit compressed"* [inferred]. **A path to real art exists that needs no
     ScummVM change**: when upscaling, the software arm's nearest-neighbour map is *surjective* onto
     every source pixel, so sampling one representative destination pixel per source pixel inverts an
     `fb0` grab back to the exact game surface — modulo the RGB565 quantisation the framebuffer already
     holds, and excluding the cursor, which `drawCursor()`:585-588 writes destructively at OUTPUT
     resolution after the scale. That defeats the "a grab off `fb0` is already arm A" argument that used
     to stand against a `--ppm` loader, and both halves of it are now built.
   - **A real 320×200 frame is captured, and the destination rect is MEASURED end to end — `.188`,
     2026-09-11.** `/opt/games/scummvm kq2` started over SSH with the launcher stopped renders without
     any touch, so the capture needs no hand at the panel: AGI, static screens, no SMUSH decoder. One
     16bpp page off `/dev/fb0` while exactly one engine was running is at
     `C:\work\rw-scratch\kq2_fb.raw`. ⚠️ **The non-black bounding box of that frame — 732×450 at panel
     (0,30) — is real and must not be used as the rect.** Two independent implementations agree on the
     box, so it is a property of the bytes; a row-by-row profile of the same grab says why it is
     worthless: an arrow-shaped **mouse cursor** occupies columns 61-69 of rows 30-44, and a 16-pixel
     **speck** sits at columns 0-7 of rows 478-479 — below the bottom bezel, outside the logical surface,
     and written by nothing in our code that has been identified. Strip those two and the picture content
     is columns **60..731**, rows 46..398.
   - **The chain that produces the rect, every step measured on this unit.** Bezel line `15 13 0 0` gives
     a logical surface of 800×452 at `view` (0,15). Calibration line 1 `-20 1019 3099 4134
     -288 881 3221 4381` with line 3 `reach 0 4082 0 4095`, pushed through `publish_safe_area()`, gives
     touch insets **left 3, right 10, top 14, bottom 17**, none of them clamped. `getScalingInfo()`:163-171
     over that safe rect then gives `scaleX` 629, `scaleY` 538, `scale` **538** (height-limited),
     `scaledWidth` **672**, `scaledHeight` **420**, `offsetX` **60**, `offsetY` **14** — panel
     **(60,29), 672×420**. ⚠️ **The columns confirm it and the rows cannot**: the grab's first non-black
     column is 60 on *every* content row and its last is 731, which is 672 wide to the pixel, while KQ2
     paints black at the top and bottom of its own 320×200 surface so no row pins an edge of the rect.
     ⚠️ **So 720×450 at (40,0) is what a 15/15 bezel with zero insets would give, and no real unit is
     that** — the rect is a function of the runtime bezel *and* the runtime insets, and a non-black bbox
     can never stand in for it: this one cleared a 2% aspect-similarity gate while being wrong on both
     axes.
   - **Both tools the real-art run needed now exist.** `fb_to_game_ppm.py` at the repo root inverts the
     software upscale — one forward pass buckets each destination column by its source column and reads
     the **middle** of each bucket, so a truncation boundary cannot absorb an off-by-one — and refuses a
     downscale rather than inventing pixels. Its `--self-test` needs no device and no files, and pairs a
     bit-exact round trip with negative controls for a shifted rect, a downscale and a wrong stride;
     rewriting the middle-of-bucket choice to the bucket's first index makes the shifted-rect control
     recover exactly, which is what proves that control is not vacuous. Run against the real grab with
     the rect above, it recovers a coherent *King's Quest 2* throne-room screen. `--ppm FILE` in
     `native_apps/tests/dss_scale_ab.c` reads a strictly 320×200 P6 **instead of** building the synthetic
     card, through a local reader because that file deliberately links nothing from `common/`; the
     geometry is left alone so the run swaps art and nothing else.
   - **THE REAL-ART RUN IS DONE, and the prediction above is REFUTED BY MEASUREMENT — `.188`
     (arwtest2, *not* the `.73` reference unit), 2026-09-11.** `/opt/games/dss_scale_ab` with
     `--ppm /opt/games/kq2_game.ppm` (md5 `afda0f96fcd597cfc9b660bc7d19cb12`), a real *King's Quest II*
     frame. **Software arm**: 12 724 µs/frame over 384 000 written pixels; operator's eye *"nice image,
     sharp"*; screenshottable, and the decoded capture confirms real art at full 800×480 with legible
     text. **Hardware arm, 2.5×/2.4× full-screen**: 178 µs/frame over 64 000 written pixels; operator's
     eye across two separate runs *"This is very bad"* and *"The text is barely readable"*. So real art
     did not reverse the ranking — it **widened** it.
     ⚠️ **Why the prediction failed, which is the durable lesson**: it took "real art" to mean painted
     backgrounds and dithered gradients, and overlooked that a SCUMM adventure is **text-heavy**. The
     dialogue box is a bitmap font with 1-pixel strokes — the worst case for any filter, and exactly
     where the player's eye is. Real art therefore carries *worse* 1 px combs than the synthetic card,
     not fewer. ⚠️ Recorded honestly: this verdict came from **two sequential full-screen runs, not a
     simultaneous `split`**. The viewing-angle confound is excluded by the earlier synthetic
     `split --swap` run (the blur followed the hardware arm across the swap) and was **not**
     re-established with real art.
   - **A THIRD arm was tested and also loses: exact 2×.** Tested on `.188` 2026-09-11 with **zero
     rebuild** — while `hard` mode held the plane, `output_size` was rewritten to `640,400` and
     `position` to `80,40` from a second `ssh`; readback confirmed `640,400` / `80,40` / `enabled 1` with
     `input_size` still `320,200`, i.e. exactly **2.000× on both axes with the ratio as the only changed
     variable**. The rationale was the identity kernel the driver reaches at that bucket — coefficients
     and citations in [`SYSTEM_ANALYSIS.md#32-display`](SYSTEM_ANALYSIS.md#32-display), not restated
     here. **Operator's eye: NOT an improvement.** Artifacts — *"extra little lines at the top and
     bottom, like an over sharpened JPG"* — i.e. **ringing**, which is what the phase-4 negative lobes
     predict. They also noted the 640×400 image is visibly smaller and does not fill the screen.
     **Geometry cost**: exact 2× forces 640×400, which is *smaller* than the **672×420** ScummVM's own
     `getScalingInfo()` already produces on this unit, so it pays image size **and** rings.
   - **RULING: no hardware configuration wins on pixel-art text from USERSPACE**, at either ratio
     tested, and the driver says none can — the ratio is the only choice a caller has. **Software
     nearest-neighbour therefore stays, unchanged and unswitched.** Do not re-propose replacing it.
     ⚠️ **And the user-facing switch is WITHDRAWN — operator, 2026-09-11.** The `rw_upscale` key in
     `/opt/games/rw_config.conf` that was decided earlier is cancelled: **no config key, no settings-app
     row, no ScummVM backend code path.** The hardware arm lost on image quality at both
     userspace-reachable ratios, so its only remaining benefit was CPU that nothing is measurably short
     of — this entry's own baseline records ScummVM playing *Full Throttle* at 12–13 % CPU. Not
     re-argued here.
     ⚠️ **What is still open is a KERNEL path, and it is image-side rather than a module build.**
     **[inferred]** a coefficient-table patch could give hardware nearest neighbour outright: a table
     carrying the identity kernel in **all 8 phases** would make every output pixel take the centre tap
     whatever its sub-pixel phase, which is what an NN upscale does — sharp *and* ~178 µs/frame, which
     would overturn the software-wins ruling. Tagged inferred: it is reasoning about what the DISPC FIR
     does with an all-identity table, not something the source states. ⚠️ **The cheap module route does
     not reach it** — measured on `.188` from `/proc/config.gz`, the DSS is **built in**, with no DSS
     module loaded and no module file on disk. ⚠️ **And a rebuilt image inherits the standing blocker**:
     the same running config has `CONFIG_TOUCHSCREEN_PANJIT=y`, which has no vanilla source and which
     `olddefconfig` drops silently, so an image built from this tree boots with a dead touchscreen. So
     the coefficient patch is **blocked behind that**, not free, and it is a row in F101's fold-in table.
     Both the built-in finding and the blocker live in `SYSTEM_ANALYSIS.md` —
     [§3.2](SYSTEM_ANALYSIS.md#32-display) and [§7](SYSTEM_ANALYSIS.md#7-kernel-policy).
   - ⚠️ **The cheapest kernel win found, and it unblocks step 2 below: raise
     `CONFIG_FB_OMAP2_NUM_FBS` from 2 to 3.** Config-only, no source patch. **Three** DSS overlays
     enumerate on this device while only `fb0` and `fb1` exist, so `vid2` can never be funded from
     userspace — there is no `fb2` node to bind. Measured on `.188` 2026-09-11 by `zcat /proc/config.gz`,
     `ls /sys/class/graphics/fb*` and `ls -d /sys/devices/platform/omapdss/overlay*`; the device fact is
     in [§3.2](SYSTEM_ANALYSIS.md#32-display). A row in F101's fold-in table.
   - ⚠️ **Still owed in the harness, if the kernel path is ever taken: the isotropic-pillarbox
     rehearsal.** `dss_scale_ab` has no proper mode for it — the 640×400 exact-2× test above was a live
     sysfs poke from a second shell, not a harness feature, so nothing repeats it and no receipt records
     it. **The tools themselves ship nothing and stay as they are**: `dss_scale_ab` with
     `soft`/`hard`/`split`/`--swap`/`--ppm`, `fb_to_game_ppm.py`, and the recovered KQ2 fixture.
   - **Two eye readings of the SOFTWARE arm on real art, operator at the panel 2026-09-11 — arm A
     alone, not an A/B.** *Full Throttle*: *"yes too sharp :)"*. *King's Quest 2*: *"King's Quest is
     running. Very sharp"*. Both point the same way the ruling already does — sharp is not automatically
     the win on old art — but neither is a comparison, so neither ranks the arms. ⚠️ The *Full Throttle*
     capture was **discarded**: two `scummvm` processes were running and fighting over `fb0`, operator-
     confirmed from the panel (*"flasing like crazy"*, *"two engines fighting over: yes"*), from a single
     `nohup` launch whose duplication is **unexplained**. Verify exactly one PID before trusting any
     capture, and re-check it after the grab.
   - ⚠️ **The switch's shape was fully mapped and is now dead work — do not build it.**
     `getScalingInfo()`:138-172 was the one chokepoint, `blitGameSurfaceToFramebuffer()`:440-573 the
     resample a hardware arm would bypass, and `rwFullContentArea()` (`roomwizard.cpp`:105-135) the
     env-then-ConfMan idiom a toggle would have copied. All of it is superseded by the withdrawal above.
     What survives from that reading, because it is true of any future backend work: `hasFeature()`
     returns true for `kFeatureCursorPalette` **only** and `beginGFXTransaction`/`endGFXTransaction` are
     no-ops, so **there is no runtime mode-change plumbing in this backend to hang anything on**; and
     **nothing bridges `/opt/games/scummvm.ini` and `/opt/games/rw_config.conf`** — `device_tools` never
     reads the former, the backend never reads the latter, and the one exception is the hand-declared
     `config_audio_device_stored()` (`oss-mixer.h`:34, called at `oss-mixer.cpp`:138) that opens
     `CONFIG_FILE_PATH` itself. ⚠️ ScummVM redeploy is ~1 m 35 s – 2 m 20 s and `rm -f`s
     `native_apps/common/*.o` twice, so it must never run concurrently with a `native_apps` build.
   - **What the per-game conversion would cost, if it is ever wanted.** None of it is owed while ScummVM
     is the subject, and each game would additionally need its own operator eye run. Three layers, worst
     first:
     1. **The shared widgets in `common/common.c`. ⚠️ The list that stood here understated this layer by
        an order of magnitude, measured 2026-09-11.** `common/common.c` contains **zero** calls to
        `fb_ui_px_*()`, so `gameover_init()` and `modal_dialog_draw()` are *mixed-unit* expressions
        today — a scaled `BTN_LARGE_HEIGHT` added to a flat `btn_gap = 15` and a flat `- 15` margin. It is
        ~40 layout literals across 12 functions, not three. `modal_dialog_draw()` alone carries
        `150,50`, `30`, `25`, `200,44`, `8` and `20`, and `modal_dialog_init()`'s `dialog_width = 420` is
        compared against the *real* `fb->width`, so the box overflows a 400-pixel surface with both side
        borders off it. `button_draw()`'s `- 20` icon inset is the highest-leverage single literal, at 51
        call sites across 7 apps. **Three different font-width constants coexist** — `6` at
        `common.c:92`, `8` at `:131`, and `8` hand-rolled twice more in `screen_draw_game_over()` — so the
        33 % over-measure is duplicated rather than localised, and fixing `:131` is a prerequisite for the
        button-label overrun noted below. And the icon **minimum-size floors cannot scale down**: 3 px
        bars with 6 px gaps in `icon_draw_hamburger()`, a 3 px stroke in `icon_draw_x()`, which together
        with that `- 20` exceed the icon box a half-size surface leaves.
     2. **Each game's own literals.** ⚠️ **"`snake.c` has none of them and is the cheapest first
        subject" was wrong** and is corrected here: snake has a fixed 80-pixel top band and 40-pixel
        margins bounding its playfield, fixed HUD rows at y=28 and y=53 against `SCREEN_SAFE`-anchored
        buttons, and a food radius of `cell_size / 2 - 2` that reaches 1 at a 400×240 surface, 0 at a
        modest touch inset and **−1 — food never drawn at all** — at the worst legal inset. It is still
        the cheapest subject; it is not a free one.
     3. **The named per-game constants**: `PADDLE_*`/`BALL_*`/`BRICK_H` in `brick_breaker.c` and
        `pong.c`, `HUD_HEIGHT` in `samegame.c` and `frogger.c`, and `TILE_SIZE` in `platformer.c`, which
        makes the visible world a function of resolution. `platformer.c` is the most expensive.
     Then ScummVM, which by the re-scope above comes **first** rather than last.
   - ⚠️ **Text does not scale, and this is a floor rather than an oversight.** The glyph size is an
     integer multiplier with no rung below 1, so a scale-3 label cannot halve. The `button_init` macro's
     `(w) > 150 ? 3 : 2` threshold is deliberately left in raw surface pixels so a scaled width falls
     through to 2, the closest rung to the 1.5 wanted — but 2 is not exact: measured, a scaled
     `BTN_LARGE_WIDTH` of 110 takes scale 2, whose `"PLAY AGAIN"` is 120 surface pixels against a
     110-pixel button, so **the label overruns**. Sizing text to fit needs `text_measure_width()`, which
     over-measures by 33 % until the 8px/6px font-width confusion in it is fixed — so this waits on that
     item rather than being half-corrected here.
   - **The bezel band no longer doubles, and touch turned out to need nothing.** The
     margins name pixels the plastic bezel physically covers, so they are *panel* pixels, and
     `fb_apply_viewport()` subtracted them from the framebuffer's own `xres`/`yres`: `.188`'s measured
     T=15 B=13 took 28 of 480 rows at full size and 28 of **240** at half, twice the panel band, which is
     why the ladder's area ratio is 4.31 rather than 4.00. The operator saw that band at top and bottom
     during the eye run above, ~1–2 mm each, against ~15 and ~13 panel rows predicted. `fb_init()` now
     reads the panel's true size from the display's mode timings — not from any framebuffer's geometry,
     which is the surface — and converts the margins with `fb_scale_bezel_to_surface()`, a pure function
     so the arithmetic is reachable from a host test (`native_apps/tests/bezel_scale_test.c`). It rounds
     down, so the art runs slightly under the bezel rather than stopping short of it, and it is a
     bit-exact no-op at 800×480. ⚠️ **A "touch on a scaled node is wrong by the scale factor and needs
     dividing" note stood here and in `framebuffer.h`, and it is measured FALSE** — the globals
     `fb_apply_viewport()` publishes are *surface* dims despite being named for the panel,
     `touch_init()` copies them into the field the curve takes as its `dim`, and the knots sit at
     `dim/4` and `3*dim/4`, so stage 1 already tracks the surface and stage 2 subtracts an origin
     already converted to it. The prescribed divide **breaks** touch: 12 failures in
     `native_apps/tests/touch_map_test.c` group J, which drives one full-size calibration over
     800×480, 400×240 and 200×120. It was never broken — the same assignment read the surface before
     the bezel work too. **Nothing blocks `snake` at reduced resolution.** Do not re-propose the divide.
   - **The composited output has an attributed eye.** One run, `overlay0` (`gfx`) disabled and confirmed
     so by readback, `vid1` upscaling a 400×240 `fb1` to 800×480, watched from the first frame through
     announced phases: launcher, then black when `vid1` covered it, then black with `gfx` off, then an
     animating grid, then black, then the launcher back. The grid **filled the panel's full width** — 400
     surface pixels across 800 panel pixels is the 2× upscale, seen rather than inferred — and the
     operator described it unprompted as *low res*, which is the scaler's filter signature. ⚠️ `input_size`
     is **not writable** (`Permission denied`); the driver derives it from the framebuffer's geometry, so
     only `output_size` is set. `fb1/size` also reads back **page-rounded**: 384000 written, 385024 read.
2. **HUD plane.** Put the unscaled HUD on `gfx` (`overlay0`) and the scaled game on `vid1`. ⚠️ **`vid2`
   is NOT an alternative from userspace** — it enumerates but has no framebuffer to bind, so it waits on
   the `NUM_FBS=3` row of F101. `global_alpha` works; `zorder` does
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

⚠️ **The deciding number is not reachable with what is on the unit — measured on `.188` 2026-09-11:
`hcitool`, `bluetoothctl` and `bluetoothd` are all ABSENT.** There is no BlueZ userspace at all, so the
fallback path above has nothing to run, and the module set is not sufficient on its own — a cross-built
BlueZ is a prerequisite for the one measurement this entry says everything depends on. The entry
formerly inferred BlueZ's presence from D-Bus being kept, which establishes the dependency and not the
package. (The `/lib/modules/4.14.52/` "ships empty" note above is a *stock* measurement; a provisioned
unit has `extra/` and a module index.)

⚠️ **"We have no ALSA" is false, and it changes what `bluez-alsa` would cost — measured on `.188`
2026-09-11.** `libasound.so.2.0.0`, `aplay`, `amixer`, `alsactl` and `speaker-test` are all present, and
`/proc/asound/cards` lists the panel card and the USB dongle. OSS `/dev/dsp` is an emulation layer over
that same card, not the native one ([`SYSTEM_ANALYSIS.md#34-audio`](SYSTEM_ANALYSIS.md#34-audio)). What
is missing is the alsa-lib **dev** side only — `/usr/include/alsa` does not exist. So `bluez-alsa` is
not blocked by ALSA's absence; it is a cross-compile against alsa-lib headers we would have to source,
a cost this entry never priced, on top of the audio half it already calls the unlikely half. ⚠️ This is
also the trigger the declined *Native ALSA backend* item names for revisiting it, and **the operator ruled
2026-09-23: on board with moving audio to ALSA wholesale** — that row is reopened as open work.

**Owning the kernel changes nothing on the audio side — analysed 2026-09-23.** `usb_host/device_config`
already has `SND_SOC`, OMAP McBSP and TWL4030 `=y` and `SND_PCM_OSS=y`; `SND_USB_AUDIO` is already an
out-of-tree module. A native-ALSA client is one raw-ioctl `AudioOutDev` in `native_apps/common/audio_out.c`
(no alsa-lib; the toolchain's `asound.h` is protocol 2.0.14, matching; est. 150–250 lines **[inferred]**,
the time64 `sync_ptr` layout a risk). It reaches `native_apps` **and** ScummVM, whose `oss-mixer.cpp` calls
`audio_out_open_oss`; `vnc_client` has no audio. But `audio.c`'s legacy direct `dsp_fd` path must fold into
`audio_out` first, and `bluez-alsa` is an alsa-lib *plugin*, so a raw-ioctl client cannot reach it — apps
would need dynamic `libasound` **[inferred]**. **So keep OSS for the speaker**; revisit ALSA only as this
entry's audio half, after the BT modules, BlueZ and `lmp_subver`.

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

**Where the two questions do connect — and the cheaper experiment has already been run.** A wired USB
DAC needed no encoding, no pairing and no latency budget, and it is now built, shipping and proven on
hardware, which fixes the speaker complaint directly for anyone willing to run a cable
([§3.4](SYSTEM_ANALYSIS.md#34-audio) has the measured PIO cost, and it is ~0.6 pp of the core, so DMA is
not what USB audio was waiting for). **BT audio: low bandwidth, high CPU. USB audio: high bandwidth, low
CPU.** So what is left of this entry is only the "no cables" half, and its hard problem is the software
SBC encoding above, not the transport.

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

### F101. Build our own 4.14.52 image — open

**The deliverable is a `uImage` we compiled, staged on p1 beside the vendor's.** The policy and the
standing costs are [§7](SYSTEM_ANALYSIS.md#7-kernel-policy); this entry is the work. The tree is
`usb_host/linux-4.14.52/`, already configured from the device's own `/proc/config.gz` by
`build-kernel-modules.sh` and already **measured** producing modules that load on the device. ⚠️ **The
image boots — measured 2026-09-23 on `.188`**: with `kernel/patches/` applied it reaches
userspace, takes DHCP and answers SSH ([§7](SYSTEM_ANALYSIS.md#7-kernel-policy) holds the cause the
unpatched image died of, and the recipe is
[`#4-boot-chain-and-recovery`](SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery)). What is left is building the touch driver into the image, the fbcon cursor and boot console, and two unexplained dmesg lines; the panel works ([`kernel/README.md`](kernel/README.md)).

**What the image is for — the payoff is deployment stability, not speed.** A kernel compiled here ships
with its own corresponding source and can go in a release, which is what retires the `/dev/mem`
byte-patch route into p1; that in turn dissolves F23's per-firmware pattern gate and gives back the free
undo both bring-up paths lost. ⚠️ **None of that is delivered until an image we built is the one a unit
is deployed on** — panel and touch working, USB power carried in its own DTB — so F23 stays open and the
byte patch stays shipped meanwhile; do not delete either on the strength of this entry.

**What to fold in, so the image is built once with everything wanted in it:**

| Wanted | Change | Note |
|---|---|---|
| Touch | finish `kernel/drivers/cy8ctmg120_ts/` — single- and multi-touch work on our image as a `.ko` from `kernel/build-modules.sh`, loaded at boot by `device-files/touch-module` ([`kernel/README.md`](kernel/README.md) has its state), handshake in [§3.3](SYSTEM_ANALYSIS.md#33-touch) | Open: **(iii) pressure** — test a profile peak-height sum against a light/firm press, the columns and rows being mapped ([§3.3](SYSTEM_ANALYSIS.md#33-touch)); **(iv) calibration accuracy on our driver** — an operator check of the corners; it is unchecked beyond "taps land on tiles" |
| fbcon cursor | `vt.global_cursor_default=0` via `CONFIG_CMDLINE_EXTEND` | permanently off. U-Boot's bootargs stay untouched — they cannot be persisted |
| Boot messages on the panel | append `console=tty0` **last** in the same `CONFIG_CMDLINE_EXTEND`, so the panel is `/dev/console` (operator's choice, 2026-09-23) | ⚠️ **Resolve the hazard first — measured by code search:** no app sets `KD_GRAPHICS` or touches the VT, and apps `mmap` `/dev/fb0` directly, so once `tty0` is a console any printk at the default console loglevel — the known USB printk loop, say — draws over a running game. **The fix is `KDSETMODE KD_GRAPHICS` in `fb_init()` in `native_apps/common/framebuffer.c`** (operator agreed 2026-09-23; every shipped fb program goes through it, so redeploy all three components): open `/dev/tty0` explicitly (apps have no controlling tty), set it unconditionally on every init so a crashed or `kill -9`ed predecessor is repaired, and do **not** restore `KD_TEXT` in `fb_close()` — the launcher closes and re-inits around each child, so that would flash the console; restore it only in the init script's `stop`, via a small helper. A `loglevel=` stays as a second line of defence. The serial getty on `ttyO1` comes from `inittab`, so it is unaffected **[inferred]** |
| MUSB DMA | `CONFIG_USB_INVENTRA_DMA` set | a genuine build defect; retires the runtime patch. ⚠️ Only that one symbol changes — `CONFIG_MUSB_PIO_ONLY` is **already unset** at `usb_host/device_config:3053`, so do not count it as a second edit |
| Scheduling | `PREEMPT`, `HZ=250` | config-only, and never measured to limit anything — include it, but do not justify the image with it |
| USB gadget mode | `CONFIG_USB_GADGET` | config-only: the micro-B socket is already the one physical port |
| Enumeration | a **driver** change in `drivers/usb/musb/` | ⚠️ not a config option ([`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy)). The image makes it *possible*; it is separate work, and B33 is the other half of that driver's story |
| Disconnect cleanup | clear `is_active` in the `default:` arm of `musb_core.c:897-900` | **the fix for B33**, whose entry holds the invariant and its measurement. A driver change like the row above, so the image is what makes it shippable |
| Third overlay plane | `CONFIG_FB_OMAP2_NUM_FBS=3` | **config-only, no source patch, and the cheapest win in this table.** Three DSS overlays enumerate against two framebuffers, so `vid2` has no node to bind and cannot be funded from userspace at all — F2 holds the measurement and is what this unblocks |
| DSS scaler coefficients | an all-identity 8-phase table in `dss/dispc_coefs.c`, or a selector that reaches one | **[inferred]** the only route to hardware nearest-neighbour upscaling; the DSS is built in, so no module can reach it. F2 holds the A/B this would overturn and [§3.2](SYSTEM_ANALYSIS.md#32-display) the coefficients |

**The order to do it in, cheapest first.** Each step is worth finishing before the next is started.

1. **~~Triage the board-file drop~~ and ~~boot one image, asserted over SSH~~ — both done.** Nothing the
   vanilla tree lacks blocks a boot except the Ethernet reset pulse, which is a source patch and not a
   config symbol ([§7](SYSTEM_ANALYSIS.md#7-kernel-policy) has the mechanism and the function-size diff
   that found it). **`.188`'s p1 now holds** `uImage-system` = our image with all four patches and our panel DTB (md5 `776a3dc5…`,
   without the 500 mA USB power patch), beside `uImage-system.panel-v1` (`8bd1e362…`, before the fb-size
   and backlight patches), `uImage-system.ours-nopanel` (`3713faf7…`, vendor DTB), `uImage-system.vendor`
   (`edc637ac…`), `uImage-system.500ma` (`a1fd1af8…`) and `uImage-system.mod` (`17243454…`, the same image
   *without* the patch, kept as the negative control).
2. **The touch driver, the fbcon cursor and the boot console** — the three rows above, which share one
   image build and one p1 write; the operator has allowed a reboot and a p1 write of `.188` for them. ⚠️ Until the
   module is loaded, `app_launcher` still exits after boot on the missing `/dev/input/touchscreen0` and
   the respawn loop clears fb0 every ~30 s — stop the init script before judging a panel frame.
3. **Explain the two dmesg lines our image adds.** `musb-hdrc musb-hdrc.0.auto: musb_init_controller
   failed with status -19` — USB host does not come up on our image, cause not investigated; read
   `drivers/usb/musb/` for the `-ENODEV` returns before theorising. And `omap2_set_init_voltage: unable
   to find boot up OPP` for `vdd_mpu_iva`/`vdd_core` — first check whether the vendor kernel's dmesg
   prints the same line; if it does, this is not ours.

**Which boot channel, and what it costs — writing an image needs no console.** Booting an alternate
filename requires the `rw20 #` prompt, so it requires the console; overwriting `uImage-system` does not,
and recovery for that is a card pull plus copying a backup back onto p1
([`#4-boot-chain-and-recovery`](SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery)). **The operator has ruled
that overwrite acceptable — 2026-09-21, "feel free to overwrite, I can re-flash easily".** It does not
retire the standing rule it suspends ([§1](SYSTEM_ANALYSIS.md#1-read-this-first) rule 3 is correct for
anyone without a card writer to hand): **take a verified p1 backup before the write** — of the *running*
kernel and not merely the pristine vendor one, because on a unit carrying the USB-power patch those are
different files. The serial console (`P4`, fitted; RS-232 behind `U27`, a MAX3232 breakout ordered for
the operator's TTL cables — [`HARDWARE.md#4-unpopulated-and-expansion`](HARDWARE.md#4-unpopulated-and-expansion))
is **off the critical path** now that SSH answers, and remains the only channel for an image that does
not. ⚠️ **Do not repoint `ctrlblock.bin` at a bootstrap image**: it overwrites two protected files and
destroys the vendor recovery image. Considered and rejected 2026-09-21.

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
| Native ALSA backend (the "ALSA port") | **Reopened by the operator 2026-09-23** — on board with a full move to ALSA; the value case below is what a design must beat, not a veto. Needs no kernel work, and the userspace side is complete on a stock unit ([`#34-audio`](SYSTEM_ANALYSIS.md#34-audio)). Declined earlier on **value**: `/dev/dsp` and the ALSA device are the same PCM, the only measured win is ~2× at the period, and no latency symptom has ever been reported. Both other arguments once recorded beside it are gone — *mixing* shipped in userspace, and the *frame arithmetic* lives in `audio_gen.c`, which a port would leave unchanged. The tinyalsa dependency, its build script and its licence rows were deleted with this decision; nothing in the tree prepares for it. **Revisit only if something we port needs ALSA.** | [`#34-audio`](SYSTEM_ANALYSIS.md#34-audio) |

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

C1 · C4 · C6 with C7 · C2 · B30 ·
F4 · C5 · C8 · F17 · F23 (scoped for kernel stability) · B33 (its fix is a driver patch, so it
waits on F101) · **F2 — moved here 2026-09-11 by the operator**, out of the head of this tier: the
userspace overlay win was measured and rejected on image quality, the switch it would have needed is
withdrawn, and what is left of the entry is one config-only item and one coefficient patch that both
wait on F101. Then, nice-to-have and last: B36 — **ranked there by the operator 2026-09-11**, who
raised it, judged the silence acceptable and wants it behind everything above.

⚠️ **Measured 2026-09-06 — only two gates run before a deploy**, `check-arm-safe.sh` and
`check-audio-pacing.sh`, both blocking. No test suite runs from any build script, from `deploy-all.sh` or
from `release.sh`, so C6 and C7 are one task: a pre-deploy gate that runs the host regressions and
shellcheck beside the two that already block. **shellcheck is installed as of 2026-09-06**, so C7 is no
longer blocked.

⚠️ **Most of this tier is NOT gated on a kernel rebuild, measured 2026-09-06.** This repo already builds
and ships modules against the vanilla tree — `xpad.ko`, `joydev.ko` and `ff-memless.ko` are deployed — so
F17 is a module build. **What genuinely
needs kernel work is short: enumeration reliability — making a cold port obtain a session without the
RESCAN tap — MUSB DMA, and, added 2026-09-11, all of F2.** Anything else claiming to need
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
