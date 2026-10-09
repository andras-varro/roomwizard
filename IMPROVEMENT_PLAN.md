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
DEVCTL `BDEVICE` to clear — now reached on every ID-ground, since the patch calls it there. **Read from
source, not timed on the device:** upstream's loop is 100 × `mdelay(5)`, so it gave up after ~505 ms, before
the 1 s jiffies deadline it appeared to have, then carried on as on success. Console error lines are defects
even when harmless. **[inferred]** that the prior root-hub suspend is the difference; n=1. **Watch:** the
message now reads `configured as A device timeout: devctl %02x after %lld ms[ (irq)]` (`kernel/patches/musb-omap2430-set-vbus-report.patch`),
so the next occurrence measures itself; read the devctl value and elapsed ms there before reproducing, then
decide whether the wait needs the PHY/glue resumed first or the loop is simply too short.

### B47. Lockdep reports recursive L2CAP socket locking on the first incoming BT connection — open, seen twice (2026-10-01, 2026-10-05)

**Measured on `.188`:** at the 8BitDo pad's first incoming connection after boot the kernel printed `WARNING:
possible recursive locking detected` — `sk_lock-AF_BLUETOOTH-BTPROTO_L2CAP` taken twice, `l2cap_sock_new_connection_cb`
→ `bt_accept_enqueue` → `lock_sock_nested`, from `l2cap_connect` / `hci_rx_work`. It did not reappear on the later
reconnect. **Second sighting 2026-10-05, ~60 s after boot, during incoming BT connections:** the same splat in
`hci_rx_work` → `l2cap_recv_frame` → `l2cap_connect` → `l2cap_sock_new_connection_cb` → `bt_accept_enqueue` →
`lock_sock_nested`, held `conn->chan_lock`, `chan->lock#2/2` and `sk_lock-AF_BLUETOOTH-BTPROTO_L2CAP`, kernel 4.14.52
tainted `O` (our out-of-tree modules). Console warnings are defects even when harmless. Our image evidently has lockdep enabled
(`CONFIG_PROVE_LOCKING` or similar) **[inferred; the config was not read]**. Cause not investigated;
**[inferred]** candidate: the 4.14 parent/child L2CAP socket false positive that upstream later annotated with a
nesting subclass. **Next:** read `.config` for the lockdep symbols and the upstream change to
`l2cap_sock_new_connection_cb`; decide between backporting the annotation and dropping lockdep from the image.

### B51. BT failover miss: a BlueALSA A2DP stream froze without error — open, sporadic, seen once 2026-10-02

**Measured, `.188`, 11:15 CDT:** `WI-C310` (`90:7A:58:B9:C6:F5`) pinned, `VT360` (`08:EB:ED:FE:C1:3D`) connected. In
the Mix Bus Test the music voice stopped consuming after 11.2 s while `snd_pcm_avail` kept succeeding with the ring
full; there was no "is gone", underrun or reopen line (`services=2801` over 94.7 s, `starve=0`), so no failover ran.
The music release never completed, so the next WAV tap was refused ("bed REFUSED - see /tmp/mix.log"). **Not
distinguished [inferred]:** a frozen BlueALSA hardware pointer versus a blocked `bluealsad`. The operator tried many
times and could not reproduce it.

**Detector deployed, never fired:** `alsa_space` in `common/audio_out.c` treats a ring that owes playback and makes no
progress for `ALSA_STALL_MS` (2000) as a lost device, logs `audio_out: <pcm> stalled — no progress for N ms (avail=…
state=…)` and refuses that PCM until a link change. "is gone" / "is available" and the `audio_mix_test` session start
carry wall-clock stamps. **Seen on a real link loss, measured `.188` 13:55 device time:** the operator switched off the `WI-C310`, then unplugged the BT dongle; both times it logged "stalled — no progress for ~2030 ms (avail=26624 state=DISCONNECTED)", then "is gone" and a working failover. The detector works on a real loss; the original freeze (no error, no disconnect) has not recurred. **Known consequence:** with one headset a pin stall can fall to the same frozen transport and
stall again, up to ~4 s of silence. **Open work, on the next occurrence:** read `/tmp/mix.log` and `app_stdout.log` for
the "stalled" line and its time, correlate with `/var/log/messages`, and optionally run `bluealsa` verbose. **Done when**
a recurrence is attributed to one of the two mechanisms, or none recurs over a long session.

### B52. The 8BitDo Pro 2 does not reconnect by itself after a bluetoothd restart — open, confirmed 2026-10-03

**Measured, `.188`, `btmon`.** (1) `/etc/init.d/bluetooth restart` (`device-files/bluetooth`: stop kills `bluetoothd`,
then `rmmod`s every BT module; start reloads them) disconnects the pad with reason 0x15 "Remote Device Terminated due
to Power Off"; page scan is off for 8.2 s, then Page Scan on and every paired device is re-added "allow incoming". The
pad blinked its reconnect LED yet sent **zero** Connect Requests in ~45 s, then powered off; 2 of 2 runs. (2) A
`bluetoothd`-only restart with the modules kept: page-scan-off window 0.73 s, still zero Connect Requests in 65 s — so
the dead window is **refuted** as the cause. (3) After either, turning the pad off and on (Home) connects within 1 s:
Connect Request accepted, stored link key, E0 encryption, HID L2CAP channels; it is paired, bonded and trusted with the
key loaded. (4) A host-initiated `bluetoothctl connect` while the pad blinked failed with
`org.bluez.Error.Failed br-connection-create-socket`; the HCI reason was not captured (that `btmon` capture came back
empty). **Inferred, not measured:** the pad backs off after the 0x15 reason, and the same mechanism sits behind the
reconnect-on-Home-press behaviour after a reboot recorded in `SYSTEM_ANALYSIS.md`. Only a `bluetooth` component deploy
or a reboot triggers it; the operator workaround is to power-cycle the pad.

**Next steps.** Capture the host-initiated page with `btmon` (Create Connection status, page timeout) while the pad
blinks; if host paging works, have the init script or a `bluetoothd` policy connect trusted HID devices after start;
check whether `bluetoothd` can stop without powering the adapter off, so no 0x15 is sent. **Done when** a restart
leaves the pad connected, or the pad's behaviour is attributed and the workaround documented.

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

### B57. Does MADC ADCIN6 follow what is plugged into the USB port? — open, n=1, needs a joint session (operator replugs while ch6 is read)

`in_voltage6` read 26-29 mV on `.188` while an Xbox 360 pad was on the USB port and a steady 344-364 mV after the BT dongle replaced it
([`SYSTEM_ANALYSIS.md#311-adc-and-temperature-twl4030-madc`](SYSTEM_ANALYSIS.md#311-adc-and-temperature-twl4030-madc)); the two states
differ in more than the device, so the link is unproven. It needs a joint session (operator confirmed 2026-10-07): the operator replugs while ch6 is read. **Done when** an operator swaps pad and dongle back and forth on the port
(reading `in_voltage6_input` after each swap, and once with the port empty) and the table says which condition, if any, moves it.


### B58. The monitor service (`rwmond`) is not running, so the Control Panel monitor page has no history — open, confirmed 2026-10-09 (operator report plus one `ps`)

The operator reports the monitor page has no history. Measured on `.188` on our own root after `provision.sh --no-clean`
and a reboot: `rwmond` (`/etc/init.d/rwmond`, link `S95rwmond` present) is absent from `ps`. The operator says it
was already not running on the original vendor-root image, so this is not specific to our root. Cause not investigated: start
with whether the init script runs at all, then whether `rwmond` starts and exits. **Done when** `rwmond` is in `ps` after a
boot and the monitor page shows history.

### B63. Every reflash changes the host keys, and WSL keeps its own `known_hosts` — open, confirmed 2026-10-09

`deploy-all.sh` run in WSL fails "REMOTE HOST IDENTIFICATION HAS CHANGED" after a reflash although Git Bash's entry was
already removed; `ssh-keygen -R <ip>` is needed in **both** shells. Fix direction to evaluate: carry the unit's host keys
forward in `state.tar` (the per-unit identity ruling allows it). **Done when** a reflash of a known unit needs no
`known_hosts` edit in either shell, or the scripts say which shell needs it.

## Features

Userspace except F101, which is the image build.

### F8. Smooth LED effects — open

The two LEDs are true PWM and drive to red / amber / green with smooth crossfade, visible from outside
the room
([`SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm`](SYSTEM_ANALYSIS.md#37-leds-backlight-and-pwm)).
Ideas: health/timer bar, heartbeat pulse during ScummVM loading, flash on high score. `hardware.c`
already reaches both channels and already has the non-blocking `LedPulse` API, so this is presentation
work only.

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

### F101. Build our own kernel image, rebased onto 4.14.336 — open

**The deliverable is a `uImage` we compiled, staged on p1 beside the vendor's, on 4.14.336 — the final 4.14 release
(4.14 has been EOL since Jan 2024, so no newer one exists; a mainline kernel is unevaluated, see the kernel notes).** It brings every
stable fix, including **FragmentSmack CVE-2018-5391** (a large `inet_frag` rework, not patched on .52; the stop-gap
`ipfrag_high_thresh=262144` / `ipfrag_low_thresh=196608` is not applied). **Measured:** all 9 of our patches dry-run
clean on 4.14.336 (one offset); the 12 `tcp-*` patches are redundant there; 3 of the 5 `patches-modules` (KNOB, HIDP
length, `remote_efs`) are in .336, the two RTL8761CU ones still apply (`btusb` needs fuzz 2, refresh);
`CPU_SPECTRE`/`HARDEN_BRANCH_PREDICTOR` exist there and .336 prints the IBE state at boot. **Risks (measured diffs):**
`omap2430.c` drops `.set_vbus` from `omap2430_ops` (changes the SESSREQ/port-power VBUS path and the IRQ branch of our
set-vbus-report patch), and `musbhsdma.c` has a TX-completion rework (our audio DMA path). `4.14.52` is pinned in
`kernel/build-image.sh:18`, `build-modules.sh:15,21`, `build-bt-modules.sh:17,83`, `usb_host/build-kernel-modules.sh:16`,
`usb_host/build-and-deploy.sh:56`, `bluetooth/build-and-deploy.sh:43`, `device-files/bluetooth:29`, `touch-module:8`,
`usb-audio-modules:18`, `xpad-modules:8-10` (use `uname -r`), `provision-rules.conf:118,128` and the `release.sh` licence
text. **Rebase steps, stepwise:** (1) generalise the module paths to `uname -r`; (2) build .336 with our 9 patches and
diff `dropped-symbols.txt`; (3) boot it on `.188`; (4) re-run the USB and audio checks; (5) retire the redundant
patches. With vendor-kernel support dropped (see the cleanup item of that name) no second `.52` module set is needed.
**Effort [inferred]:** about 1 h for the image, 0.5-1 day for the scripts, 1-2 operator sessions.

**The image work:** the policy and the
standing costs are [§7](SYSTEM_ANALYSIS.md#7-kernel-policy); this entry is the work. The image boots, takes
DHCP and answers SSH, and the panel works ([`kernel/README.md`](kernel/README.md) holds the build, the patches
and the p1 files). What is left is building the touch driver into the image, the fbcon cursor and boot
console, and one unexplained dmesg line.

**What the image is for — the payoff is deployment stability, not speed.** A kernel compiled here ships
with its own corresponding source and can go in a release, which gives back the free undo both bring-up
paths lost. The vendor-kernel byte-patch path is deleted (tag `last-vendor-kernel`), so the release
supports only our image. ⚠️ **That is not delivered until an image we built is the one a unit is
deployed on** — panel and touch working.

**What to fold in, so the image is built once with everything wanted in it:**

| Wanted | Change | Note |
|---|---|---|
| Touch | finish `kernel/drivers/cy8ctmg120_ts/` — single- and multi-touch work on our image as a `.ko` from `kernel/build-modules.sh`, loaded at boot by `device-files/touch-module` ([`kernel/README.md`](kernel/README.md) has its state), handshake in [§3.3](SYSTEM_ANALYSIS.md#33-touch) | Open: **(iii) pressure** — test a profile peak-height sum against a light/firm press, the columns and rows being mapped ([§3.3](SYSTEM_ANALYSIS.md#33-touch)); **(iv) calibration accuracy on our driver** — an operator check of the corners; it is unchecked beyond "taps land on tiles" |
| fbcon cursor | `vt.global_cursor_default=0` via `CONFIG_CMDLINE_EXTEND` | permanently off. U-Boot's bootargs stay untouched — they cannot be persisted |
| Boot messages on the panel | append `console=tty0` **last** in the same `CONFIG_CMDLINE_EXTEND`, so the panel is `/dev/console` (operator's choice) | ⚠️ **Resolve the hazard first — measured by code search:** no app sets `KD_GRAPHICS` or touches the VT, and apps `mmap` `/dev/fb0` directly, so once `tty0` is a console any printk at the default console loglevel — the known USB printk loop, say — draws over a running game. **The fix is `KDSETMODE KD_GRAPHICS` in `fb_init()` in `native_apps/common/framebuffer.c`** (operator agreed; every shipped fb program goes through it, so redeploy all three components): open `/dev/tty0` explicitly (apps have no controlling tty), set it unconditionally on every init so a crashed or `kill -9`ed predecessor is repaired, and do **not** restore `KD_TEXT` in `fb_close()` — the launcher closes and re-inits around each child, so that would flash the console; restore it only in the init script's `stop`, via a small helper. A `loglevel=` stays as a second line of defence. The serial getty on `ttyO1` comes from `inittab`, so it is unaffected **[inferred]** |
| Scheduling | `PREEMPT`, `HZ=250` | config-only, and never measured to limit anything — include it, but do not justify the image with it |
| USB gadget mode | `CONFIG_USB_GADGET` | config-only: the micro-B socket is already the one physical port |

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
*running* kernel and not merely the pristine vendor one, because they can be different files. The serial console (`P4`, fitted; RS-232 behind `U27`, a MAX3232 breakout ordered for
the operator's TTL cables — [`HARDWARE.md#4-unpopulated-and-expansion`](HARDWARE.md#4-unpopulated-and-expansion))
is off the critical path now that SSH answers, and remains the only channel for an image that does not.
⚠️ **Do not repoint `ctrlblock.bin` at a bootstrap image**: it overwrites two protected files and destroys
the vendor recovery image.

### F135. Boot the newest LTS kernel with omapfb on a spare card — open, operator request 2026-10-05, step 1 only

Mainline still carries `omapfb`, so a newer kernel is unevaluated rather than ruled out; the evidence, the
unproven list and the gains and costs are in `kernel/README.md` → *Mainline and omapfb* and are not repeated
here. **Step 1 only:** on a **separate card**, so `.188`'s card is untouched, boot the newest LTS with omapfb
and a DT panel node, and answer two questions: does the panel light, and does the runtime 32/16bpp switch work
(`fbset`, `/sys/class/graphics/fb0/bits_per_pixel`). Anything past that (touch, Bluetooth, modules) waits on
a yes to both.

### F137. Users other than root — open, operator wish 2026-10-09, not designed

The operator wants root not to be the default user on our root filesystem. Open question to the operator: login only
(a non-root SSH account, apps stay root), or apps run as a non-root user too (which touches `/dev/fb0`, `/dev/input`,
`/dev/dsp`, `/dev/watchdog` and the `roomwizard-app` respawn loop). **Done when** the operator has answered and the chosen shape is built.

### F139. Provisioning supports our own root filesystem only — open, operator decision 2026-10-09

The vendor root is retired (tag `last-vendor-rootfs`, at the last commit that can commission it). Direction: `provision.sh` becomes
"update an already-imaged unit online", `make-card-image.sh --bundle` is the primary install, and the consent gate and the
`--unattended` guard simplify once there is no clean. It stays the online update/upgrade path for an imaged unit, and should take a `commissioning/backup.sh`
backup before it changes anything. Scope, from a reading of the tree (re-grep before starting):

1. ⚠️ **Hazard until the clean is gone: `provision.sh` without `--no-clean` on our root deletes our boot links.** `clean-rules.conf`'s
   `scope sweeps /etc/rcS.d` keep-list (~120-140) names only the vendor's links; our `rcS.d` links (`rootfs/CLAUDE.md`) are not in
   it and the `rc5.d` keep-list is unchecked for `S02dbus-1`, `S09sshd`, `S20hwclock.sh`. Predicted, not run: no network or SSH
   after the reboot, recovery by reflash. Use only `--no-clean` on our root.
2. The clean: `device-files/clean-rules.conf` (whole file, incl. the p5 rules that still describe vendor p5 content),
   `lib/rw-clean.sh`, `commissioning/provision.sh` (`DO_CLEAN` :95, `run_clean` :462, `ask_consent` :365/:802, `--remove`/`--deep-clean`),
   `commissioning/commission-offline.sh` (`DO_CLEAN` :133, backup question :331, clean :632-662, `--unattended` guard :287-298).
3. The watchdog bypass: `device-files/disable-steelcase.sh`, its call from `device-files/roomwizard-app` (~:86-95, with the
   `touch /var/watchdog_test` fallbacks), `provision.sh:255`, and the provision-rules rows for `disable-steelcase.sh` (:105) and
   `/var/watchdog_test` (:162). Also `/etc/default/syslogd` (:106) if our root already starts syslogd quietly (check first).
4. `commissioning/card-prep.sh`'s edits of the vendor `shadow` (:215-230), `sshd_config` (:281-319), `dhclient.conf`/`hosts` (:535-544).
5. Vendor markers in `lib/rw-identify.sh`: `RW_ROOTFS_VENDOR` (:59), `RW_ISSUE_RE` (:60) and the banner fallback (:93-96), the p2/p5 vendor
   content notes (:270-285, :413); `/home/root/backup` readers `provision.sh:520,624,628`, `lib/rw-identify.sh:296` and
   `control_panel/monitor_page.c:60` (p5 is empty and unmounted on our root, so they report p6's numbers).
6. The sshd policy: `provision-rules.conf` sshd rows (:205-223). The default `--ssh-auth=password` writes `PasswordAuthentication yes` +
   `PermitRootLogin yes` over the image's key-only `sshd_config` (root password is locked, so not exploitable, but a regression), and its
   fixed `KexAlgorithms` drops the post-quantum kex, so the OpenSSH client warns on every connection after provisioning (confirmed
   2026-10-09). Target: leave the image's policy intact unless the operator asks; `--ssh-auth` and `lib/rw-sshd.sh` shrink or go.
7. The vendor `/etc/profile` drop of `wsplatform.conf` (`provision-rules.conf:235`) and `provision.sh:589`.

**Done when** no code path targets the vendor root, `./tests/run-all.sh` is green, and a card plus an online provision both pass on `.188`.

### F140. A generic "flash and go" base card image — open, operator wish 2026-10-09, not designed

No per-unit state: flash, insert, and it boots and works. First boot picks a random host name (or asks a few optional questions).
**The published image carries no `authorized_keys` at all** (operator, 2026-10-09: a builder's key in a downloadable image is a
login to every unit flashed from it); the image build refuses one, like the `lib/rw-state.sh` deny check. `sshd` is key-only, so
the image is closed out of the box (root's password is locked: no default password exists). Operator's design, 2026-10-09: a
Control Panel SSH page — mode Off / Key only / Key + password / Password only, "set password" on the on-screen keyboard (required
before any password mode), and the unit's IP and host name; README then explains: set a password, pick Key + password,
`ssh-copy-id`, optionally back to Key only. While `/etc/touch_calibration.conf` is
absent the launcher shows a "not calibrated" status text on top. The per-unit image (a `state.tar` from one unit) stays as the second
package kind. Needs: `rootfs/make-card-image.sh` accepting no `state.tar` (today `rootfs/fetch-card-parts.sh` makes it from a unit);
a first-boot init script for the host name (`commissioning/set-hostname.sh` has the `/etc/hosts` logic; host keys already generate on
first boot on p2); the banner in `native_apps/app_launcher/app_launcher.c`. **Done when** a card built with no unit state boots on a
unit, shows the banner, refuses SSH until the panel's SSH page opens it, and the README's key procedure then works.

### F106. Support BeagleBone Black boards — open, operator idea 2026-10-01, future

The operator inherited many BeagleBone Black boards (photos: `beaglebone/image/beaglebone-black-board-{1,2}.jpg`). **Measured from the
photos:** TI AM3358 (Cortex-A8, 1 GHz — also no hardware integer divide), TPS65217C PMIC, 512 MB DDR3,
eMMC, SMSC LAN8710A PHY, an NXP HDMI framer to micro-HDMI, USB-A host, mini-USB, microSD, P8/P9 headers.

**Inferred, not built:** our apps need only `/dev/fb0`, evdev and ALSA, so the differences are board-specific —
no touch panel, other LED/backlight/watchdog paths, no speaker GPIO or bezel, 5 V power. That points to a
**board-profile layer** under `common/hardware.c`, `common/touch_input.c` and `commissioning/` rather than
`#ifdef`s. Our binaries are soft-float against the RoomWizard rootfs while stock BBB Debian is hard-float, so
this ties to our own root filesystem (`rootfs/`; one rootfs for both boards). A BBB wants a mainline kernel with DRM fbdev emulation; the
RoomWizard stays on 4.14.52 omapfb. **Done when** the first question is answered: which board-specific
paths a launcher plus one game actually touch, listed from a BBB boot.

**Future scope, operator-agreed, not scheduled:** mouse-only operation of the launcher, Control Panel and games, for a unit with no touch and no keyboard or pad. The operator has a RoomWizard whose touch is broken, and a BeagleBone has no touch. The mouse pointer in the launcher and the Control Panel exists and is operator-verified; the games still take no mouse pointer.

### F111. Redraw the launcher's tile icons in the Control Panel's rounded style — open, operator request 2026-10-01, future

The launcher tiles look dated next to the Control Panel's page icons. **Where each comes from (read from
source):** the launcher and the Control Panel draw their tiles through the same `icon_grid_draw_tile()`
(`common/icon_grid.c:116`, `TILE_RADIUS` 12 at `:17`; called from `app_launcher.c:264` and `control_panel.c:346`),
so the tile frame is already rounded and shared — the difference is the 96x96 PPM *inside* it
(`ICON_GRID_ICON_SIZE`, `common/icon_grid.h:23`). The Control Panel's are generated by
`control_panel/gen_cp_icons.py` (`base()` at `:28` draws a supersampled `rounded_rectangle` radius 16 in an
accent colour, glyphs drawn white on it). The launcher's are per-app `<app>/<app>.ppm` — generated for only three
by `frogger/gen_icon.py`, `platformer/gen_icon.py`, `samegame/gen_icon.py` (64x64 square scenes, no rounding), the
rest committed as bare PPMs with no generator — and collected by the `*//*.ppm` glob in
`native_apps/build-and-deploy.sh:307`; manifests name them in `app-manifests.sh:39-47`. **Fix:** one generator in
the style of `gen_cp_icons.py` for all ten apps, so every PPM has a source, then delete the three old scripts.
**Done when** the launcher grid on the panel reads as the same family as the Control Panel's. Operator 2026-10-07 also wants a better icon for the Control Panel's System page, done with this rework: its page has `.icon = NULL` (`system_page.c:563`) and `ICONS` in `gen_cp_icons.py:178` has no System entry.

### F117. Rename the project away from "RoomWizard" — open, operator idea 2026-10-02, future

"RoomWizard" is likely a Steelcase trademark **[unchecked]**. The operator's idea is "Lizard" (Linux + Wizard): not
tied to rooms, since a BeagleBone target has nothing to do with rooms, and it fits a Steelcase-free distro/rootfs
(`rootfs/`) and the BeagleBone port (F106). Candidate names are open. **Done when** a name is chosen and the tree, docs,
device paths and `LICENSE.md` follow it.

## Structural and cleanup

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
to us.** The entry stays open as a known gap in the delivery path, not as work anybody can pick up here. (`rootfs/make-card-image.sh --bundle` now runs the install against a real image's partitions through loop mounts, but it is `--base`, so p1 stays unexercised.)

### C15. The bare plan-ID scan collides with function-key names — open, measured 2026-09-03

`bare_sites()` in `tests/doc_check.sh` matches an `F`-numbered ID in parentheses or after `see`/`is`/
`was`, and F1-F12 are key names as well as plan IDs here — a ScummVM key-table row such as
`Save/load dialog (F5)` counts as a citation, and it turns into a dangling one the moment an entry of that
number closes. **So the reported citation count carries false positives, and the next F-numbered entry to
close can fail the gate on unrelated documentation.** Narrow the scan rather than excluding a file: a hit
on a line that also carries a key-binding marker (`Ctrl+`, `Alt+`, `Shift+`), or one inside a two-column
key table, is not a citation. ⚠️ Needs a control in both directions — a real bare citation must still fire,
and it must fire in a file of the same kind, or the scan goes blind where it used to see.

### C19. Windows leaves "System Volume Information" on p1 — open, harmless

Windows writes it onto p1 when the card is written; p1 is meant to hold the boot files only. **Done when** a later step
deletes it, or a note records that it is ignored.

### D17. `LICENSE.md` overhaul — open, operator ruling 2026-09-29

Our GPL kernel and modules ship (source-offer duty), the glibc row names only `gnueabihf`, and the obligation column is
unreviewed; Buildroot's `legal-info` gives the package manifest. The offline path (`commission-offline.sh --base`) would need
its clean sweeps, `rw_is_rootfs` markers and stdin prompts dealt with first. **Done when** every shipped package has a
licence row and the source-offer duty is stated.

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
| Mainline 5.x/6.x port | **Standing operator decision to stay on our 4.14 build, not a closed technical verdict:** `omapdrm` would break the runtime bpp switching and the DSS overlay sysfs, but mainline still carries `omapfb` (upstream source, 2026-10-05), which is unevaluated on this device. Building 4.14.52 ourselves (F101) keeps all three intact | [`#7-kernel-policy`](SYSTEM_ANALYSIS.md#7-kernel-policy) |
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

**Design (defaults the operator may overrule, 2026-10-07).** New `native_apps/common/start_menu.h/.c` on the `GameOverScreen` pattern (init/update/draw/needs_redraw; the layout rects are the hit-test). The caller passes `get_time_ms()` as the game clock; blink is 1 s, 500 on / 500 off, and a new `audio_ping` (1319 Hz, 40 ms) fires on on-edges where `blink_idx % 6 < 3`, edge-detected, skipped when effects are off. UP/DOWN move through `ui_focus` (add `ui_focus.o` to `GAMEPAD_OBJ`), LEFT/RIGHT cycle a choice, JUMP/ACTION/PAUSE activate, BACK exits, touch acts on release. 2 PLAYERS is enabled only while `gamepad_player_mask(gm)&3==3` and offered only by games that implement 2P, pong first (P2 drives the right paddle, `pong.c:232`). EXIT entry last; TEST MODE / TRAINING are added as choices and the pause toggles kept. Migration order: widget + host test, pilot snake, pong (1P/2P + difficulty), tetris + frogger, samegame + platformer, brick_breaker, then retire `screen_draw_welcome*`.
