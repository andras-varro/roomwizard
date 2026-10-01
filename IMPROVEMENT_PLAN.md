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

**Nothing is outstanding.** There is no `/dev/uinput`, so nothing past an app's first screen is
script-verifiable (`CLAUDE.md` → *Non-obvious constraints*), and panel time is the project's scarce
resource: price the check before requesting it, group requests into **one checklist**, split an item when
only part of it is gated, and record the answer with the confidence it was given — "I think it works" is a
hedge, and a verdict is a weaker answer than a description, so say which one the next item needs.

---

## Correctness and verification

### B29. Two findings from the 2026-08-09 walkthrough — open

1. ⚠️ **`card-prep.sh` still asks the operator to mount the rootfs; `commission-offline.sh` does not, and
   the asymmetry has no reason left.** The operator is holding the card either way, and
   `rw_mount_card`/`rw_check_card_mounts` already exist and are what the offline pass uses. Phase 1
   should find the card disk (`rw_find_card_disks`), mount what it needs, and unmount on every exit path
   — with `$ROOTFS` still honoured as the "I mounted it myself" hatch, and the desktop-automounted case
   detected rather than double-mounted. ⚠️ **It needs p2 as well as p6** (`websign/net.mode` is read from
   p2, mounted read-only), so this is one mount decision covering both. Whatever mounts must also be
   reachable from a failure trap, the same rule `rw_umount_boot` follows.
2. **The panel keeps displaying the vendor's old IP after phase 1, while SSH answers on the new one** —
   observed 2026-08-09 on the unit commissioned with the regenerator disabled. Consistent with the
   display reading `websign/net.ipaddress`/`net.status` on **p2**, which phase 1 does not touch: the
   vendor UI is showing its own stale config, not the live interface. Benign, and it disappears with the
   clean that deletes `websign/`. **Worth confirming that is the source** before writing it down as
   fact anywhere else — it is currently an inference from where the value could have come from.

### B30. `brick_breaker` hides lives past the ninth — open, latent, cosmetic

Three games draw a capped HUD lives row and each caps it differently:

| Game | Shape | Verdict |
|---|---|---|
| `frogger.c:1269` | `lives_shown = min(lives, 5)`, then lays out from `lives_shown * LIFE_ICON_PITCH` | correct |
| `platformer.c:1541` | lays out from the capped number and draws one icon plus `x10` past the cap | correct |
| `brick_breaker.c:1224` | `for (i < game.lives && i < 9)`, each heart at a fixed anchor minus `i * 14` | **silently truncates** |

The extra-life power-up at `brick_breaker.c:718` does `game.lives++` with no cap, so a tenth life and
every one after it is **invisible**: the HUD reads nine whether you hold nine or fourteen, and losing one
appears to change nothing. Cosmetic, never a crash, and it needs a power-up-heavy run to reach, which is
why it has not been seen.

Fix is the rule the other two follow: cap first, lay out from the capped number, and make the number
appear somewhere once it exceeds what is drawn — a heart plus `x10`, or a raised cap.

### B41. An adapter replug can print `configured as A device timeout` on our image — open, seen once 2026-09-29

**Measured on `.188`** (uImage md5 `f3b446c6b2d731e0d118583cada62c36`, the ID-ground patch booted):
`musb-hdrc musb-hdrc.0.auto: configured as A device timeout` was printed once, at one adapter replug —
the one following a spell in which the root hub had been runtime-suspended; the earlier replug did not
print it. Enumeration succeeded anyway. The line comes from `omap2430_musb_set_vbus()`, which waits for
DEVCTL `BDEVICE` to clear (100 × `mdelay(5)`, 1 s timeout) — now reached on every ID-ground, since the
patch calls it there. Console error lines are defects even when harmless. **[inferred]** that the prior
root-hub suspend is the difference; n=1. **Next:** reproduce (suspend the empty root hub, replug the
adapter) with the `rwsv` kprobe on `omap2430_musb_set_vbus` and a DEVCTL read, then decide whether the
wait needs the PHY/glue resumed first or the loop is simply too short.

### B45. Unplugging a hub that carries a streaming USB audio card prints a kernel WARNING — open, seen once 2026-10-01

**Measured on `.188`** (our 4.14.52 image): `WARNING: CPU: 0 PID: 1858 at drivers/usb/musb/musb_host.c:138
musb_h_tx_flush_fifo+0x134/0x138` with `musb-hdrc musb-hdrc.0.auto: Could not flush host TX10 fifo: csr:
2003`; chain `hub_event` → `usb_disconnect` → `usb_audio_disconnect` → `release_urbs` → `deactivate_urbs` →
`usb_hcd_unlink_urb` → `musb_urb_dequeue` → `musb_cleanup_urb` → `musb_h_tx_flush_fifo`. The system
continued: the hub re-enumerated and audio returned to the dongle. Nine plain dongle unplugs in the same
run did not print it. Console warnings are defects even when harmless; n=1. Capture on the device:
`/home/root/log/s1001-b42-hub-warn.log`. **Next:** read `musb_h_tx_flush_fifo` in
`usb_host/linux-4.14.52/drivers/usb/musb/musb_host.c` and check later mainline for a change to that `WARN`,
before reproducing.

### D7. mDNS does not resolve from WSL, which is where the deploy scripts run — open, confirmed 2026-08-15

A named unit answers to `<name>.local` from Windows (`commissioning/set-hostname.sh`, the avahi link). Two
residues:

1. **WSL cannot resolve `.local`.** Its `/etc/nsswitch.conf` is `hosts: files dns` — no mDNS module —
   so `./commissioning/provision.sh rw09.local` passes validation, reaches the SSH step and then fails to
   resolve. ⚠️ **`libnss-mdns` alone is insufficient, measured 2026-09-06:** it is installed and in
   `nsswitch.conf`, avahi runs on the device, and `.local` still does not resolve — WSL2 is NAT'd onto its own
   subnet and mDNS is link-local multicast, so it cannot cross. The fix is `networkingMode=mirrored` in
   `.wslconfig`, which changes networking for every distro on the host and is the operator's call. **Until
   then the mDNS payoff applies to Windows-side `ssh` only, not to the build/deploy path.**
2. **The reboot path is unproven.** `S30avahi-daemon` is in place but the link was written directly
   rather than by a full `commissioning/provision.sh` run, so "it comes up on its own after a reboot" has not
   been observed.

### B38. Mix Bus Test cracks from ~6 voices under a full redraw — open, confirmed 2026-09-28, parked

**Parked by the operator 2026-09-28 ("we can live with this").** **Cause measured** at `.188` with the
honest PAD arm (readout band only, via `present_rect`): the full redraw is the trigger — 440 Hz voices
crack from the 7th under PAD, from the 6th and on every meter redraw under FULL; not clipping
(`clip=0 lim=0`). Mechanism **[inferred]**: an ALSA underrun at `snd_pcm_writei` that `alsa_recover()`
hides; `audio_out.c` logs `underran at the write`. Remedies if unparked: a cheaper oscillator than the
per-sample `sin()` in `common/audio_gen.c`, or a larger lead. Also **[inferred, code only]**:
`native_apps/tests/audio_mix_test.c` calls `fb_fade_out()` then `audio_close()` with no pump between, so
the stream's tail starves during the fade (`:1112-1113` today) — pump the bus through the fade.

### B43. `measure_audio_tone_sabotage.sh` case 9 edits a line that no longer exists — open, confirmed 2026-09-29

`native_apps/tests/measure_audio_tone_sabotage.sh` case 9 deletes
`audio_out_set_device_pref(config_audio_device_stored());` from `audio.c`, and prints `NO-OP EDIT —
pattern rotted`: since `8a62c31` the line reads `audio_out_set_device_pref(pref ? pref :
config_audio_device_stored());` in `audio_init_unchecked_pref()` (`git show HEAD~1:native_apps/common/audio.c`
has zero copies of the old form). So group J's only host-reachable sabotage has proved nothing since
2026-09-28. **Fix:** re-key the `sed` to the current line, then confirm case 9 reports failures again
(and case 10 still its documented 0).

### B44. Mix Bus Test is not layout-sensitive, so it runs degraded in portrait — open, operator request 2026-09-29

`native_apps/tests/audio_mix_test.c` lays out for landscape only. Known defect **[inferred from code, not
screenshotted]**: its in-place readout repaint is a no-op in portrait, because `present_rect()` returns
early on `fb->portrait_mode` (`tests/audio_mix_test.c:466`; the comment at 462-463 says landscape only,
since `fb_swap()`'s rotation would be needed). **Fix:** make the layout follow the orientation, and either
give portrait a repaint path or fall back to a full `fb_swap()` there. The operator ruled 2026-09-29 that
it runs degraded in portrait rather than being refused; it launches from the control panel's Audio page and
this is the portrait half of that. Verify on the panel in both orientations. Distinct from B38 (the crack under a full
redraw), which constrains how costly a portrait redraw may be.

## Features

Userspace except F101, which is the image build, and F2, which now waits on it.

### F2. Use the DSS overlay planes — open, **gated on a kernel build; waits on F101**

What is left needs a kernel image; no UI change is wanted. The userspace half (`vid1` upscale of a reduced
surface) was built, measured and rejected on image quality — software nearest-neighbour stays. Facts, the
overlay recipe and scaling limits, the A/B outcome and its instruments:
[`SYSTEM_ANALYSIS.md#32-display`](SYSTEM_ANALYSIS.md#32-display); the reduced-surface UI conversion:
`native_apps/CLAUDE.md` → *Coordinates, dimensions, portrait*; ScummVM's output rect:
`scummvm-roomwizard/CLAUDE.md`.

**Kernel-side, both rows of F101's fold-in table.** Done-when: an image built from F101 carries each, and
the panel confirms it.

- **`CONFIG_FB_OMAP2_NUM_FBS` 2 → 3.** Config-only, no source patch. Three overlays enumerate but only `fb0`
  and `fb1` exist, so `vid2` has no node to bind and step 2 below is unreachable until this lands.
- **All-identity 8-phase scaler table in `dss/dispc_coefs.c`** (or a selector that reaches one).
  **[inferred]** it would give hardware nearest-neighbour — sharp *and* ~178 µs/frame against ~12.7 ms for
  the software resample — and overturn the software-wins ruling; it is reasoning about the DISPC FIR, not
  something the source states. The DSS is built in, so no module reaches it, and a rebuilt image inherits
  the dead-touchscreen blocker ([§7](SYSTEM_ANALYSIS.md#7-kernel-policy)). The rehearsal this needs is a
  pillarbox mode in `dss_scale_ab` (the 640×400 test was a live sysfs poke, not a harness feature) — and
  the target is ScummVM's isotropic pillarboxed rect, not a full-screen stretch.

Further steps, in order, all behind the above:

1. **HUD plane.** Unscaled HUD on `gfx` (`overlay0`), scaled game on `vid1`; `vid2` waits on `NUM_FBS=3`.
   `global_alpha` works and `zorder` does not, so the fixed GFX < VID1 < VID2 order decides what is on top.
2. **Colour-key transparency** via `trans_key_enabled` for zero-CPU sprite masking.
3. **Video playback**, speculatively — `/dev/video0` accepts YUV with hardware colour-space conversion.
   Furthest from proven; the boot-time `omap_vout: failed to allocate DMA Channel for video-1` may be what
   blocks it.

⚠️ **Verification is operator-in-the-loop.** `cat /dev/fb0` returns the gfx plane's memory, not the
composited panel, so no screenshot can see an overlay — say so in any checklist this work produces.

⚠️ Cheap today, but it would need rewriting as DRM atomic plane code after a **mainline** port — which
is out of scope, and which a 4.14.52 rebuild is not: that leaves omapdss and this code intact.

**Cleanup when this concludes:** delete `tests/fb_plane_bench.c` and `tests/dss_scale_ab.c` (both
DSS-scaling instruments, deployed hidden) together with their build steps, their `GAMES_BINARIES`
entries in `native_apps/build-and-deploy.sh`, and their `CTEST_NOT_HOST` rows in
`tests/run-all.sh`.

### F4. Surface the two MADC channels that need no wire — open

Both are readable with `cat` today and have zero references in the codebase
([`SYSTEM_ANALYSIS.md#311-adc-and-temperature-twl4030-madc`](SYSTEM_ANALYSIS.md#311-adc-and-temperature-twl4030-madc)):

- `in_temp1_input` — SoC die temperature. Add a readout to Control Panel (~10 minutes).
- `in_voltage9` — RTC backup cell voltage. A "battery low" warning is nearly free.

### F8. Smooth LED effects — open

The two LEDs are true PWM and drive to red / amber / green with smooth crossfade, visible from outside
the room
([`SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm`](SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm)).
Ideas: health/timer bar, heartbeat pulse during ScummVM loading, flash on high score. `hardware.c`
already reaches both channels and already has the non-blocking `LedPulse` API, so this is presentation
work only.

---

### F17. Bluetooth peripherals — open, measured 2026-08-08

**The want:** a wireless game controller and a headset or speaker for ScummVM. The unit is PoE-wired, the
Xbox pad is wired, and the integrated speaker is poor
([§3.4](SYSTEM_ANALYSIS.md#34-audio)) — so every current option is a cable. A wired USB DAC already fixes the
speaker for anyone willing to run one ([§3.4](SYSTEM_ANALYSIS.md#34-audio) has its measured PIO cost), so what
this entry adds is the "no cables" half.

**This is a module build, not a kernel rebuild.** The kernel half ships as modules with no p1 write, and the
dongle's identity, patches and firmware are in [`kernel/README.md`](kernel/README.md). DMA is not a
prerequisite: A2DP is tens of KB/s and a controller a few hundred bytes/s, which PIO carries.
**Bluetooth needs a USB dongle — there is no radio on the board**
([`HARDWARE.md` §4](HARDWARE.md#4-unpopulated-and-expansion); the `J5`/`J6` XBee socket is 802.15.4 and cannot
host Bluetooth), and there is no second USB port ([§3.6](SYSTEM_ANALYSIS.md#36-usb)), so the dongle occupies
the single connector. BlueZ userspace is cross-built, not yet deployed (step 1 below).

**Open now:**

- **ALSA is the audio route** ([§3.4](SYSTEM_ANALYSIS.md#34-audio)), because `bluez-alsa` is an alsa-lib
  *plugin*. Operator by ear on `.188`: a game on onboard and on the USB dongle, `control_panel` TEST AUDIO on
  both, ScummVM. The Mix Bus Test crack is B38. **Loudness:** an onboard
  probe tone at amplitude 6000 was faint while the mixer read 0 dB — compare loudness game-vs-game and against
  the vendor's `aplay`, at equal amplitude **[inferred: amplitude only]**.
- **Nothing is persistent on `.188` yet** (measured 2026-10-01). The 18 modules from
  `kernel/build-bt-modules.sh` (`BT_LE=y`, `BT_BREDR=y`, measured from its `.config`) are `insmod`'d by hand from
  `/lib/modules/4.14.52/bt/`; `bluetoothd` runs by hand from `/tmp/s1002/` and is lost on reboot. Only
  `/etc/dbus-1/system.d/bluetooth.conf` is installed. `hci0` is up on an RTL8761CU (`rtl_bt/rtl8761cu_fw.bin` +
  config), controller `A0:AD:9F:70:DD:CA`.
- **Pairing recipe (measured):** a `NoInputNoOutput` agent; scripted `bluetoothctl` needs a ~2 s delay before
  `agent` or registration fails. Classic HID works end to end: a "BT Keyboard 5.1" (`E6:7A:00:00:20:9F`, class
  0x002540) was found by inquiry and paired Just Works with no PIN; `hidp` → `hid-generic` made one input node
  carrying both keys and touchpad, which `app_launcher` hot-plugged.
- **Pads: the 8BitDo Pro 2 works in A mode** (DualShock 4 emulation, `hid-generic`; identities and the failed
  modes in [§3.6](SYSTEM_ANALYSIS.md#36-usb)). Unmeasured: a reconnect after the pad sleeps or the unit
  reboots, which needs packaging (step 2) first.

**Next, in order:**

1. **Done (measured 2026-10-01):** `bluetooth/build-bluez.sh` builds BlueZ 5.66 against Debian bullseye armel
   `-dev` `.deb`s used as the sysroot — no glib or dbus source build, so it is pinned to the glib 2.62 API.
   Output goes to `bluetooth/staging/` (gitignored). It configures `--disable-monitor`, so `btmon` is not staged;
   a one-off build with `--enable-monitor` was the instrument that diagnosed pairing.
2. Packaging: a deploy path for the modules, `rtl_bt` firmware, `bluetoothd`/`bluetoothctl`/`libbluetooth` and the
   dbus conf, plus boot-time module load and daemon start. Undecided whether that is a `bluetooth/` component in
   `deploy-all.sh` and the release bundle, or part of `usb_host`; device files go through
   `commissioning/provision.sh`.
3. `sbc` + `bluez-alsa` v4.3.1 into **our** alsa-lib's plugin dir.
4. The control panel's Bluetooth page (adapter power, scan, pair/connect/forget) is a `CpPage` with its own tile;
   the grid has none until then. Paired devices appear in the Input page's testers — **a pad or keyboard on its
   own node needs no extra code; a keyboard+touchpad combo node needed the reader fix** (shipped).
5. `audio_out`: the `bluez-alsa` plugin returns `-ENODEV` from `writei` on sink loss, which
   `audio_out.c` (~`:485`) already classifies as `AO_ERR_LOST`, but `audio_out_usb_returned()` knows only
   USB card 1. A2DP adds ~150-250 ms latency **[inferred]**.

⚠️ **The hard problem is audio CPU, not USB — measure before promising.** A2DP means software SBC encoding
on one 600 MHz core that ScummVM already holds at ~32 %
([§6.5](SYSTEM_ANALYSIS.md#65-software-rendering-techniques-that-paid-off)). NEON is available and D-Bus
already runs (`S02dbus-1` is a `keep`), so BlueZ has its bus, and `bluez-alsa` is the lean bridge rather
than PulseAudio on 234 MB. A2DP's latency is fine for point-and-click and wrong for anything twitchy. **The
controller half is much more likely to land than the audio half; do not sell them as one feature.**

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

⚠️ **The interim is one honest line in `COMMISSIONING.md`** stating the host requirement; the real answer is a
bootable image, and macOS cannot be tested from here at all.

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

### F101. Build our own 4.14.52 image — open

**The deliverable is a `uImage` we compiled, staged on p1 beside the vendor's.** The policy and the
standing costs are [§7](SYSTEM_ANALYSIS.md#7-kernel-policy); this entry is the work. The image boots, takes
DHCP and answers SSH, and the panel works ([`kernel/README.md`](kernel/README.md) holds the build, the patches
and the p1 files). What is left is building the touch driver into the image, the fbcon cursor and boot
console, and one unexplained dmesg line.

**What the image is for — the payoff is deployment stability, not speed.** A kernel compiled here ships
with its own corresponding source and can go in a release, which is what retires the `/dev/mem`
byte-patch route into p1; that in turn retires the per-release md5 gate in `lib/rw-usbpower.sh` (it refuses every Steelcase release but the reference unit's — `COMMISSIONING.md`) and gives back the free
undo both bring-up paths lost. ⚠️ **None of that is delivered until an image we built is the one a unit
is deployed on** — panel and touch working, USB power carried in its own DTB — so the md5 gate and the
byte patch stay shipped meanwhile; do not delete either on the strength of this entry.

**What to fold in, so the image is built once with everything wanted in it:**

| Wanted | Change | Note |
|---|---|---|
| Touch | finish `kernel/drivers/cy8ctmg120_ts/` — single- and multi-touch work on our image as a `.ko` from `kernel/build-modules.sh`, loaded at boot by `device-files/touch-module` ([`kernel/README.md`](kernel/README.md) has its state), handshake in [§3.3](SYSTEM_ANALYSIS.md#33-touch) | Open: **(iii) pressure** — test a profile peak-height sum against a light/firm press, the columns and rows being mapped ([§3.3](SYSTEM_ANALYSIS.md#33-touch)); **(iv) calibration accuracy on our driver** — an operator check of the corners; it is unchecked beyond "taps land on tiles" |
| fbcon cursor | `vt.global_cursor_default=0` via `CONFIG_CMDLINE_EXTEND` | permanently off. U-Boot's bootargs stay untouched — they cannot be persisted |
| Boot messages on the panel | append `console=tty0` **last** in the same `CONFIG_CMDLINE_EXTEND`, so the panel is `/dev/console` (operator's choice) | ⚠️ **Resolve the hazard first — measured by code search:** no app sets `KD_GRAPHICS` or touches the VT, and apps `mmap` `/dev/fb0` directly, so once `tty0` is a console any printk at the default console loglevel — the known USB printk loop, say — draws over a running game. **The fix is `KDSETMODE KD_GRAPHICS` in `fb_init()` in `native_apps/common/framebuffer.c`** (operator agreed; every shipped fb program goes through it, so redeploy all three components): open `/dev/tty0` explicitly (apps have no controlling tty), set it unconditionally on every init so a crashed or `kill -9`ed predecessor is repaired, and do **not** restore `KD_TEXT` in `fb_close()` — the launcher closes and re-inits around each child, so that would flash the console; restore it only in the init script's `stop`, via a small helper. A `loglevel=` stays as a second line of defence. The serial getty on `ttyO1` comes from `inittab`, so it is unaffected **[inferred]** |
| Scheduling | `PREEMPT`, `HZ=250` | config-only, and never measured to limit anything — include it, but do not justify the image with it |
| USB gadget mode | `CONFIG_USB_GADGET` | config-only: the micro-B socket is already the one physical port |
| Third overlay plane | `CONFIG_FB_OMAP2_NUM_FBS=3` | **config-only, no source patch, and the cheapest win in this table.** Three DSS overlays enumerate against two framebuffers, so `vid2` has no node to bind and cannot be funded from userspace at all — F2 is what this unblocks; the measurement is in [§3.2](SYSTEM_ANALYSIS.md#32-display) |
| DSS scaler coefficients | an all-identity 8-phase table in `dss/dispc_coefs.c`, or a selector that reaches one | **[inferred]** the only route to hardware nearest-neighbour upscaling; the DSS is built in, so no module can reach it. [§3.2](SYSTEM_ANALYSIS.md#32-display) holds the A/B this would overturn and the coefficients |

**The order to do it in, cheapest first.**

1. **The touch driver, the fbcon cursor and the boot console** — the three rows above, which share one
   image build and one p1 write; the operator has allowed a reboot and a p1 write of `.188` for them. ⚠️ Until the
   module is loaded, `app_launcher` still exits after boot on the missing `/dev/input/touchscreen0` and
   the respawn loop clears fb0 every ~30 s — stop the init script before judging a panel frame.
2. **Explain the dmesg line our image adds**, `omap2_set_init_voltage: unable
   to find boot up OPP` for `vdd_mpu_iva`/`vdd_core` — first check whether the vendor kernel's dmesg
   prints the same line; if it does, this is not ours.

**Which boot channel — writing an image needs no console.** Booting an alternate filename requires the
`rw20 #` prompt, so it requires the console; overwriting `uImage-system` does not, and recovery for that is a
card pull plus copying a backup back onto p1
([`#4-boot-chain-and-recovery`](SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery)). The operator accepts the
overwrite ("I can re-flash easily"), which does not retire
[§1](SYSTEM_ANALYSIS.md#1-read-this-first) rule 3: **take a verified p1 backup before the write**, of the
*running* kernel and not merely the pristine vendor one, because on a unit carrying the USB-power patch those
are different files. The serial console (`P4`, fitted; RS-232 behind `U27`, a MAX3232 breakout ordered for
the operator's TTL cables — [`HARDWARE.md#4-unpopulated-and-expansion`](HARDWARE.md#4-unpopulated-and-expansion))
is off the critical path now that SSH answers, and remains the only channel for an image that does not.
⚠️ **Do not repoint `ctrlblock.bin` at a bootstrap image**: it overwrites two protected files and destroys
the vendor recovery image.

### F102. Build our own root filesystem for p6 — open, asked for by the operator 2026-09-25

**The deliverable is a p6 image we built (Buildroot or similar), replacing the vendor's Yocto 3.1.4
rootfs instead of cleaning it.** F101 replaces only the kernel. Everything the cleanup fights lives on
p6: the vendor services, the network-file regenerator ([§3.5](SYSTEM_ANALYSIS.md#35-network-and-power)),
the software watchdog, and the vendor's `rc`/`rcS` wrappers, whose leftover switch files the clean has
to chase. A rootfs we build would also be the first one we are allowed to ship: the vendor's may not be
redistributed (`LICENSE.md`).

**What makes it small** — our binaries need nothing beyond glibc, `libstdc++` and `libasound` (native apps and
ScummVM link those dynamically; `vnc_client` is `-static`), measured
([§6](SYSTEM_ANALYSIS.md#6-building-for-this-device)). So the base needs those, `/usr/share/alsa`, and what our init scripts and
services call: `sshd`, `cron`, `dbus` (inferred as needed), an mDNS responder, the hardware watchdog
feeder, `rdate`, `amixer`, `insmod`, and a `start-stop-daemon`/`ps` that `device-files/roomwizard-app`
accepts. Inferred, from reading `device-files/` and `device-files/provision-rules.conf`, not from a
build. `disable-steelcase.sh` and the whole of `device-files/clean-rules.conf` would have nothing left
to act on.

**Fixed by the boot chain, measured:** the root must stay on p6. U-Boot's `root=/dev/mmcblk0p6` is
compiled in, with no `saveenv`, and `/etc/fstab` names p2, p3, p5 and p7 by position
([§4](SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery)). p1's `mlo`/`u-boot.bin`/`ctrlblock.bin` stay
untouched, so the recovery is still "reimage the card".

**Open before designing it — none measured:**

| Question | Why it matters |
|---|---|
| Where does the MAC address come from — the LAN9221's EEPROM, U-Boot, or a vendor script? | If a vendor script sets it, replacing p6 changes every unit's address. |
| Which per-unit state lives on p6? | `/etc/touch_calibration.conf` and `/var/lib/alsa/asound.state` are on `/`, so a new p6 loses them unless they move to p2 or get carried over. |
| Does anything in userspace need `/usr/share/alsa`? | `clean-rules.conf` keeps it "for the OSS shim", but OSS here is kernel emulation ([§3.4](SYSTEM_ANALYSIS.md#34-audio)). |
| Does `S40ctrlblk` do anything we need? | [unverified]. The kept boot link finds no `/opt/sbin/ctrlblk` after the clean. |
| What obligations come with busybox and the other GPL/LGPL packages? | Their source offer goes beside the kernel's in `LICENSE.md`. Operator ruling 2026-09-29: the whole `LICENSE.md` overhaul is part of this item — our GPL kernel image and modules now ship (source-offer duty), native apps and ScummVM link glibc and libasound dynamically, the glibc row names only `gnueabihf`, and the obligation column is unreviewed. |
| What does p5 become? | It frees 1.5 GB of space. |

### F106. Support BeagleBone Black boards — open, operator idea 2026-10-01, future

The operator inherited many BeagleBone Black boards (photos in `beaglebone/image/`). **Measured from the
photos:** TI AM3358 (Cortex-A8, 1 GHz — also no hardware integer divide), TPS65217C PMIC, 512 MB DDR3,
eMMC, SMSC LAN8710A PHY, an NXP HDMI framer to micro-HDMI, USB-A host, mini-USB, microSD, P8/P9 headers.

**Inferred, not built:** our apps need only `/dev/fb0`, evdev and ALSA, so the differences are board-specific —
no touch panel, other LED/backlight/watchdog paths, no speaker GPIO or bezel, 5 V power. That points to a
**board-profile layer** under `common/hardware.c`, `common/touch_input.c` and `commissioning/` rather than
`#ifdef`s. Our binaries are soft-float against the RoomWizard rootfs while stock BBB Debian is hard-float, so
this ties to F102 (one rootfs for both boards). A BBB wants a mainline kernel with DRM fbdev emulation; the
RoomWizard stays on 4.14.52 omapfb. **Done when** the first question is answered: which board-specific
paths a launcher plus one game actually touch, listed from a BBB boot.

### F103. Software power-off: make `poweroff` more than a halt — open, asked by the operator 2026-09-29

**Today `poweroff` is `halt`.** Measured on .188 (our 4.14.52 image): `twl4030_power_off`, `pm_power_off`
and `gpio_poweroff_driver` are in `/proc/kallsyms`, and `CONFIG_TWL4030_POWER=y`,
`CONFIG_POWER_RESET_GPIO=y`, but no device is bound to `twl4030_power` or `poweroff-gpio`. The DT's
`twl@48` node has no `ti,twl4030-power*` child and no `ti,system-power-controller`, in the vendor
`original.dtb` and in `kernel/dts` alike. `drivers/mfd/twl4030-power.c` sets `pm_power_off` only when
that property is present, and with `pm_power_off` NULL `kernel/reboot.c` turns POWER_OFF into HALT. The
rootfs halt script already runs `halt -d -f -p -h`, so userspace is not the missing piece. Measured on
.188 2026-09-29 (n=1) and again 2026-09-30 via the launcher's SHUT DOWN (n=2, both halts panel WHITE): `shutdown -h now` halts with the panel bright white and the backlight on, down
over 3.5 min with no watchdog reboot. Cause [inferred from source]: omapdss stops DISPC and panel-dpi
drops its enable GPIO, while `kernel/dts/panel-dpi.sh` holds the LVDS and backlight-enable GPIOs high as
hogs and the TWL PWM backlight stays powered.

**Inferred, not measured:** power is 802.3af PoE only (TPS23750 front end and buck upstream of the PMIC),
so a TWL4030 OFF drops the SoC and RAM rails but probably not the PoE front end. The backlight supply is
unknown, so the panel may stay lit. Waking needs a start-on event; there is no power button and the PWRON
wiring is unknown, so pulling PoE is the expected only way back.

**Action.** A DT-only change in `kernel/dts`: a `ti,twl4030-power` child under `twl@48` with
`ti,system-power-controller` (the generic compatible loads no sequencing scripts, the lowest risk). Stage
it as a test image under a new filename, then run ONE `poweroff` with the operator watching the panel and
the PoE port draw. **Done when** we know whether `poweroff` darkens the panel and reduces PoE draw, and
the launcher's Shutdown either uses it or keeps the halt, its screen's unplug-when-white wording
following whichever end state ships.

### F104. CPU-usage graph on the Monitor page — open, operator request 2026-09-30, later

A history graph of CPU utilisation on the control panel's Monitor page, beside the planned SoC temperature (F4).
⚠️ **Trap: `/proc/stat`'s total column is not a valid denominator on this kernel** — `NO_HZ_IDLE` makes it
unreliable; the fact and its measurement live in
[`SYSTEM_ANALYSIS.md#34-audio`](SYSTEM_ANALYSIS.md#34-audio) (the PIO-cost paragraph). Use busy ticks over
wall-clock seconds × `CONFIG_HZ`. **Done when** the graph on .188 reads near zero on an idle panel and
rises under a known load (a `yes > /dev/null` over SSH), which also checks the denominator.

### F105. Auto-rescan on the USB page while it is open — open, operator idea 2026-09-30, later

The control panel's USB page re-reads the bus only on opening and on RESCAN (the Input page already polls `/dev/input`
once a second, but that is the evdev node list, not the USB bus). Add a periodic re-read while it is open,
repainting only when the list changes, as Network's 2 s change detection does. ⚠️ **Constraint: the
automatic path must only READ.** The MUSB port re-probe blocks for a few seconds and stays on an explicit
RESCAN (and the one opening scan of an empty port). **Done when** a device plugged in or pulled on .188 appears or disappears on the open page within
the interval with no tap, an idle page does not repaint, and no re-probe runs unprompted. Not yet seen on
the panel: the page's "+N MORE" row (shown when the list outgrows the rows that fit; .188 has 7 devices and
all fit), so plug in enough devices to see it.

### F107. Control Panel menu has no keyboard navigation — open, measured by the operator on .188 2026-10-01

A keyboard works in the Input page's keyboard tester but cannot move around the menu. `control_panel.c`'s main
loop polls only `touch_poll(&touch)` (~`:768`) and never calls `gamepad_poll()`, whereas `app_launcher.c` does
(`:753`) and routes it through `handle_gamepad_input()` (`:374`) **[read from source; not tried on the panel]**.
**Done when** arrow keys + Enter/Esc move through the tile grid and a page on .188.

### F108. No mouse pointer in the launcher or the Control Panel — open, measured by the operator on .188 2026-10-01

A mouse works only in the Input page's mouse tester. `app_launcher.c` consumes `mouse_left_pressed` (`:805`) but
a grep finds no cursor drawn there, and `control_panel.c` never polls `gamepad_poll()` at all **[read from
source; the launcher's click path not exercised with a mouse on the panel]**. **Done when** a pointer is drawn
and a click activates a tile in both; the draw belongs in one shared helper, not per app.

## Structural and cleanup

### C1. Extract the shared evdev layer — open, classifier and scan done

**Classifier + scan are one implementation, `common/input_scan.c`/`.h`**, called by `common/gamepad.c`
(so every game), `control_panel`'s Input page testers, `vnc_client` and ScummVM's `roomwizard-events.cpp`
(`input_scan_with()` carries ScummVM's touchscreen name filter). Measured by host tests only
(`input_scan_test`, `gamepad_latch_test`, 19 ctests passed 2026-09-29); on-device check pending.

**Left: the `/etc/input_config.conf` parser and the hotplug rescan timer**, still one copy each in
`gamepad.c`, `vnc_client/vnc_input.c` (`load_input_config`) and `roomwizard-events.cpp`. They have
drifted before — `MAX_INPUT_DEVICES` was resynced twice by hand — and the "clear errno before the read
loop" hardening still exists only in the ScummVM copy.

### C4. Make the common library use the logger — open

`common/logger.c` exists and apps use it (`app_launcher` 18 calls, `control_panel` 17), but the library
they all link writes to stdout unconditionally: `touch_input.c` 15 `printf` / 0 `LOG_`; `gamepad.c`
7/0; `framebuffer.c` 5/0. `touch_init()` alone emits ~5 lines, and `app_launcher` calls it after
**every** child exit, so launcher stdout grows the same banner forever. Log rotation bounds the file
now, but the noise is still the cause.

### C5. Fix `text_truncate` and the 8px/6px font-width confusion — open

- `common.c:108` `text_truncate()` takes **no destination size** and does `strcpy(dest, upper)` (up to
  256 bytes) plus `strcat(dest, "...")`. Callers survive on arithmetic luck — `control_panel/usb_page.c:243`
  passes a 48-byte buffer for a 48-byte product name (safe only because the source is itself 48), while the
  Input page's two callers use 256-byte buffers. One longer source from a stack smash.
  Add a `size_t dest_size` parameter.
- Text width must come from `text_measure_width()`, because `fb_draw_text` advances **6 px/char**
  while several sites compute **8**. Titles render ~17 % left of centre and long strings clip off the
  left edge. **Wrong: `screen_draw_game_over()`** (message and
  score widths) **and `ui_layout.c:326`**.

### C6. Extend the host-buildable test harness — open

The host-gcc regressions over the pure-logic parsers are done and gated. One piece remains.

**Write the first-screen smoke harness.** SSH-launch a binary, `cat /dev/fb0`, decode with
`fb565_to_png.py`, and inspect the screen drawn before any input: `assert not-all-black`,
`assert alive after 2 s`, across all ~15 binaries. That has caught real defects when done by hand.
Anything past the first screen needs a tap-by-tap checklist for a human. ⚠️ **`assert not-all-black` is
nearly vacuous on its own** — assert a minimum count of distinct pixel values, take the depth from
`fbset | grep geometry` on the device rather than assuming 32bpp, and keep *did not start* / *started and
died* / *black screen* / *harness could not tell* as separate outcomes, because silence is not success. It
needs a device to **run** but not to **write**: prove every branch on the host with an `ssh` stub on
`PATH`, the way `tests/rw_provision_test.sh` does.

**Gap, measured 2026-09-30: `./tests/run-all.sh` does not compile `control_panel`.** A `control_panel.c`
with a compile error passed the host gate (32 passed, 0 failed, 2 skipped); only the ARM build in
`native_apps/build-and-deploy.sh` caught it. **Done when** a gate step compiles the control panel's
sources (syntax-only is enough), and it is seen failing against a deliberately broken copy kept outside
the repo.

### C7. Burn down the shellcheck backlog — open

The shell scripts *are* the deployment system and they run as root over SSH. The gate's two tiers, the
ratchet and the `SC1124` trap are in `tests/CLAUDE.md`, as are the directive traps and the gate-shape
measurement rule. What is open is the backlog recorded one row per `(file, code)` in
`tests/shellcheck-baseline.txt`. `sort -k3 -rn tests/shellcheck-baseline.txt | head` puts the worst files
first: `scummvm-roomwizard/manage-scummvm-changes.sh`, then `probes/xbee_socket_continuity.sh`, then
`scummvm-roomwizard/build-and-deploy.sh` and `commissioning/card-prep.sh`. What remains in
`native_apps/build-and-deploy.sh` is deliberate: word-splitting that a rewrite cannot exercise,
client-side expansion of local constants that `SC2029`'s remedy would break, and deploy-path `ls` sites
free at the next real deploy.

**Prefer a fix that changes no behaviour to a `disable=` directive.** Do not "fix" a finding by rewriting
a line you cannot exercise — several of the leaders are in build scripts that only a real deploy runs.

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
play session of somebody's time. A `--level N` argument or a debug entry in the pause dialog turns it into
one launch, and would serve any future level-dependent bug; generalise to the other games where a state is
expensive to reach. **Operator ruling: a test-only command-line switch is acceptable** — the launcher
passes no arguments, so such a flag is reachable over SSH and deliberately not from the panel, which is
what a test entry point wants. `brick_breaker` already has both halves (`--test` and a pause toggle), so
its level-5 problem is open on the level number, not on the mechanism. A pause-dialog toggle (the shape
Office Runner's TRAINING toggle in `platformer.c` uses) is not script-reachable — there is no
`/dev/uinput` — so a mode with no CLI entry has no first-screen SSH check either; it makes a deep state
cheaper for a human, not automatable.

### C12. Offline commissioning has never been run against a real disk — open

`tests/commission_offline_test.sh` contains no `--dry-run`, so each of its cases already **is** a real
`--base` pass — the clean, `card-prep.sh`, the provision plan, the install and the verify all run for
real on a fabricated card tree. `--base` cannot locate p1 by construction, so the p1 gate, backup,
patch, verify and rollback are the one unexercised half, and reaching them needs a physical card in a
reader. ⚠️ **Not feasible on this dev host — it has no card reader, and the USB-reader route is closed
to us.** The entry stays open as a known gap in the delivery path, not as work anybody can pick up here.

### C15. The bare plan-ID scan collides with function-key names — open, measured 2026-09-03

`bare_sites()` in `tests/doc_check.sh` matches an `F`-numbered ID in parentheses or after `see`/`is`/
`was`, and F1-F12 are key names as well as plan IDs here — a ScummVM key-table row such as
`Save/load dialog (F5)` counts as a citation, and it turns into a dangling one the moment an entry of that
number closes. **So the reported citation count carries false positives, and the next F-numbered entry to
close can fail the gate on unrelated documentation.** Narrow the scan rather than excluding a file: a hit
on a line that also carries a key-binding marker (`Ctrl+`, `Alt+`, `Shift+`), or one inside a two-column
key table, is not a citation. ⚠️ Needs a control in both directions — a real bare citation must still fire,
and it must fire in a file of the same kind, or the scan goes blind where it used to see.

### C17. A layout rule for `control_panel` pages and future apps — open, operator request 2026-10-01, later

**Measured from the code.** Every page computes its own geometry and chooses its own font scale; the button
widget draws at the scale it is given (`button_init_full`, `common/common.c`) with no automatic fit.
`control_panel/input_page.c`'s TOUCH row is always one row of four and drops the whole row to scale 1 if any
label exceeds `width-8`; its KBD/MOUSE/PAD row is one row of three by the same rule. `display_page.c`'s test
buttons are the only flow layout (3 columns, falling back to 2, font kept) and it is private to that page. No
shared flow or wrap helper exists in `common/`; `ui_layout.c` has grid and list helpers that no control-panel
page uses.

**Operator preference:** keep the font size and the button width and wrap buttons into rows, like a WPF flow
container. **Question to settle with the operator:** one shared flow helper in `common/` for every app, or
per-page code. ⚠️ **The Input page's portrait receipt reports "1 label(s) cut" and over-reports:** RESET GEOMETRY
at scale 1 is 84 px against a limit of `bw-8` = 83 (`bw` 91), yet the operator saw it render correctly, so the
`-8` allowance in that receipt is stricter than the drawing. Settle the allowance with the helper.

**Two related questions, both the operator's.** Whether `touch_trace` (a calibrated finger trail against the
raw one, logging both; deployed hidden, SSH-only) belongs on the Input page beside the multi-touch test and
the touch diagnostic: fold duplicates into one rather than adding a button per tool. And whether to take up
the deferred `dlopen`'d `CpPage` modules, which an out-of-tree page would need (design requirements in
`native_apps/CLAUDE.md` → *control_panel*).

---

## Out of Scope

Recorded so the decision is not re-litigated. Anything needing a connector, a populated device or a wire
is out under [`#8-hardware-policy`](SYSTEM_ANALYSIS.md#8-hardware-policy); a kernel config symbol on its own
blocks nothing, since we build the image (F101). Requesting GPL source from Steelcase stays ruled out and
is needed for nothing ([`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy)).

| Item | Blocked by | Detail |
|------|---|---|
| Enable the two EHCI USB host ports | **Nowhere to plug in** — no second USB connector and no unpopulated footprint on the board, so `CONFIG_USB_EHCI_HCD` is not what blocks this and an image we build gains nothing here | [`#36-usb`](SYSTEM_ANALYSIS.md#36-usb) |
| SPI | Four controllers `okay` in the DT and `CONFIG_SPI` unset, but **nothing is on the bus** — no children are declared, and putting a device there needs a wire | [`#314-what-is-not-present`](SYSTEM_ANALYSIS.md#314-what-is-not-present) |
| Piezo buzzer on TWL4030 PWM | **Needs a wire**, and all 3 dmtimer PWMs are taken; `CONFIG_PWM_TWL` is the cheap half | [`#39-i2c`](SYSTEM_ANALYSIS.md#39-i2c) |
| Mainline 5.x/6.x port | **Closed on DRM/KMS, not on effort:** `omapdrm` would break the runtime bpp switching ScummVM and the VNC client depend on, lose the DSS overlay sysfs, and cost RAM. Building 4.14.52 ourselves (F101) is the opposite decision and keeps all three intact | [`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy) |
| Ambient-light sensor / auto-backlight | **No such hardware.** The teardown found no sensor and, decisively, no aperture, window or light pipe anywhere in the enclosure — a sensor would have nothing to sense even if fitted. ⚠️ Do **not** probe for it: `pv02_app 5` can hang I2C bus 1, which carries the PMIC. *Time-of-day* dimming needs no sensor and is still available. | [`#39-i2c`](SYSTEM_ANALYSIS.md#39-i2c) |
| Serial console | Located and pinned out (`P4`), then declined: the recovery loop is *pull the card, reimage, DHCP, SSH*, and since NAND and U-Boot stay untouched the card **is** the entire failure surface. Serial would add boot visibility, not recovery capability. Revisit only if NAND or U-Boot ever get written — or once we are iterating on our own images (F101), where serial is the only channel that shows *why* one failed to boot, though fitting `P4` is itself a board change. | [`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports) |

**Note:** enabling **UART3** as a `ttyO2` is *not* in this table — it may be reachable by patching the
appended DTB, which needs no kernel source ([`#312-serial-ports`](SYSTEM_ANALYSIS.md#312-serial-ports)).

⚠️ **Operator ruling, 2026-09-06: no further USB work beyond USB audio.** Enumeration-at-probe is closed
and that is a result rather than a gap — three mechanisms read out of the MUSB driver were each applied
and **refuted on hardware**, and the answer that ships is the one-tap RESCAN, verified on a panel. Before
anyone proposes a fourth theory, read the refuted table in `usb_host/README.md`, which names the one
never-attempted candidate and the question any candidate must answer first.

**Bundles hold built artifacts only — settled**, because the one consumer that installs device scripts runs
from a clone and has `device-files/` beside it either way.
