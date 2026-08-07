# RoomWizard Commissioning

This document describes the commissioning process for RoomWizard devices.

## Overview

Bring-up is three phases, run in order, each by its own script:

| Phase | Script | Connection | When |
|-------|--------|------------|------|
| **1. SD Card** | `commissioning/card-prep.sh` | Offline (SD in PC) | Once per device |
| **2. System Setup** | `commissioning/provision.sh` | SSH over network | Once per device |
| **3. Deploy apps** | `deploy-all.sh` | SSH over network | Per deploy |

**The recommended path is [`roomwizard.sh`](roomwizard.sh)** — a menu over all three, which
implements nothing of its own and shells out to the scripts above:

```bash
./roomwizard.sh          # interactive menu; item 5 is the full 1 -> 2 -> 3 bring-up
```

Item 5 is the only one that chains, and it exists because of the two human gaps between the
phases: after commissioning you must boot the device, and Phase 2 *reboots* it, so the operator
was otherwise guessing when to start the next step. `roomwizard.sh` polls SSH — not ping, which
answers while `sshd` is still starting — at both transitions. Every phase remains callable
directly and non-interactively; nothing in the three scripts changed to accommodate the menu.

### Why the three are separate, and stay separate

The obvious simplification — fold Phase 2's cleanup into offline commissioning — does not work,
for a mechanical reason. Phase 2's targets are spread across **four partitions** that only a
booted kernel assembles into one tree:

| Partition | Mounted as | Cleanup that touches it |
|---|---|---|
| p6 | `/` | `/opt/jetty*`, `/opt/openjre-8`, `/usr/share/cjkfont`, WebKit/GTK/X11 libs, `/etc/init.d/*` |
| p2 | `/home/root/data` | `websign`, `conctest`, the 79 MB `cron/log` truncation |
| p3 | `/home/root/log` | `Xorg.0.log`, `browser.err`, `jettystart`, … |
| p5 | `/home/root/backup` | `serialno`, `pointercal`, the 5 MB fallback kernel, and the 472 MB `factory/*.img` restore payload the clean deletes |

`commissioning/card-prep.sh` locates exactly **one** of these (p6, by content — see
[*Finding the card*](#finding-the-card)). Three further reasons:
`disable-steelcase.sh` is re-run **on every boot** by the init script, so the disable half is
inherently a running-system job; already-commissioned units exist and must stay cleanable without
pulling their card; and the deep clean measures `df` before/after and offers a dry run, which
assumes a live device. Commissioning's job is to produce a device you can *reach* — a cleanup that
broke boot there would cost you SSH and tell you nothing.

## Finding the card

⚠️ **A partition is identified by position and content, never by filesystem UUID.** A UUID is
generated at mkfs time, so it names one *card*, not a model: units are mkfs'd independently at the
factory, and two RoomWizards running the identical firmware build share **none** of their four
UUIDs. A hardcoded UUID therefore recognises only the unit its constant was copied from and rejects
every other RoomWizard. It cannot be repaired by assigning the constant to the new card either —
two cards with one UUID is a worse bug than the one it hides.

Nothing on the device consumes a UUID at all: U-Boot passes `root=/dev/mmcblk0p6` and `/etc/fstab`
names `/dev/mmcblk0p{2,3,5,7}`, both by position.

[`lib/rw-identify.sh`](lib/rw-identify.sh) holds the two checks, sourced by both
`commissioning/card-prep.sh` and `commissioning/clone-to-32gb.sh`:

| Function | Question | How |
|---|---|---|
| `rw_is_rootfs` | is this mounted tree a RoomWizard rootfs? | the four files commissioning edits, plus one vendor marker — `/opt/sbin/watchdog/watchdog.sh`, `/opt/pv02`, `/opt/roomwizard`, or the `RW20 Embedded Platform` banner in `/etc/issue`. The marker set is an **or** because `commissioning/provision.sh --remove` deletes `/opt/pv02`, and a card already in service must still be recognisable. |
| `rw_is_card_disk` | is this disk a RoomWizard card? | the partition table: start and size of p1 p2 p3 p5 p6, which are byte-identical on every unit. p4 and p7 are **not** pinned — they absorb the difference in physical card size. |

Two safety properties worth knowing, because a content scan can reach places a UUID lookup could
not:

- **`/` is never a candidate.** Commissioning is a card-in-reader operation, so the live root is
  never the target — and selecting it would rewrite this host's own `/etc/shadow`. It is excluded
  from the scan unconditionally. To act on a live root, set `ROOTFS=/` by hand.
- **An explicit `$ROOTFS` is still checked**, and a tree that fails the check needs a typed `yes`
  before anything is written. The escape hatch stays usable for a deliberately odd target;
  `export ROOTFS=/` by accident does not silently proceed.

Regression: [`tests/rw_identify_test.sh`](tests/rw_identify_test.sh) — host-only, no card, no root.
It builds synthetic rootfs trees for every state a card can be in and synthetic partition tables
with `sfdisk` on sparse files, so both the positive and the negative controls are self-contained.

```bash
./tests/rw_identify_test.sh
```

## Phase 1: SD Card Commissioning

The [`commissioning/card-prep.sh`](commissioning/card-prep.sh) script configures the device offline by mounting its SD card on a Linux machine.

### What it does
- Sets root password (SHA-512 hash in `/etc/shadow`)
- **Sets the host name** in `/etc/hostname` *and* `/etc/hosts` (see below)
- Enables SSH (root login, password + pubkey auth)
- Optionally installs your SSH public key
- Configures DHCP networking on eth0
- Backs up every file it modifies

### The host name, and why `/etc/hosts` is rewritten too

A unit as it arrives carries **a name it did not choose and a mapping for that name that does not
work**. Both are properties of the image rather than of the unit, so both are the same on every
card, but *what* the name is varies — measured examples are `RW09` and `null`. What is consistent is
the shape: `/etc/hosts` maps the device's own name on a **non-loopback** line, to an address that is
unreachable from anywhere the unit is now used. So more than one unit can claim one name, and every
unit resolves its own name wrongly.

Setting `/etc/hostname` alone would leave that mapping in place, so anything on the device that
resolves its own name would still get the wrong answer. The prompt therefore writes both files, via
[`commissioning/set-hostname.sh`](commissioning/set-hostname.sh) — one implementation shared with `commissioning/provision.sh --hostname`,
so the offline and over-SSH paths cannot drift. It keys the removal on the name it reads from
`/etc/hostname`, not on a hardcoded one, which is why it works on a card whose shipped name is
anything at all. The result is loopback-only:

```text
127.0.0.1 localhost
127.0.0.1 <name>
```

Give each unit a **unique single label** (`rw09`, not `rw09.local` — mDNS appends `.local`
itself). Combined with Phase 2 enabling mDNS, that is what makes `ssh root@rw09.local` and
`./commissioning/provision.sh rw09.local` work instead of hunting for a DHCP lease.

### Usage

1. **Insert the SD card** into a Linux machine (or WSL), and mount its rootfs (p6). Any mount
   point works — the script finds it by content.
2. Run:
   ```bash
   ./commissioning/card-prep.sh
   ```
   If it cannot find a rootfs it names the disk it *did* find and prints the mount command. You
   can also point it at a mounted tree directly with `export ROOTFS=/mnt/rw`.
3. Follow the prompts for password / host name / SSH key
4. Unmount, re-insert into device, power on

<!--
  ⚠️ The block between the two markers below is READ AT RUNTIME, not just by humans.
  commissioning/card-prep.sh prints it verbatim as its epilogue, so this file is the single
  source of truth for the next steps and the script has no second copy. Two consequences:

    - Keep both markers. tests/commission_prep_test.sh asserts the block appears in a
      standalone run and does NOT appear when RW_COMMISSION_ORCHESTRATED is set (the
      offline single pass has already done every step it names). Deleting a marker turns
      that test red, which is the intended alarm rather than a nuisance.
    - It is printed to a terminal, so it is indented plain text with no markdown syntax.
      Bullets and backticks would be read out literally.
-->
<!-- NEXT_STEPS_START -->

  Remaining steps
  ────────────────
  1. Unmount SD card:       sync && sudo umount <mountpoint>

  2. Reinsert SD card into device, connect Ethernet, power on

  3. Wait ~30 s, find device IP from router DHCP leases
     (after step 5 the device also answers to NAME.local, where NAME is
      the host name you just set)

  4. If SSH key was NOT installed during commissioning:
       ssh-copy-id -i ~/.ssh/id_rsa.pub root@<ip>

  5. One-time system setup. Stops the vendor services, installs the app
     launcher and enables mDNS. Deletes nothing, and ends in a reboot:
       ./commissioning/provision.sh <ip>

     To also DELETE the vendor software (permanent), add --remove.
     It asks for a card backup first, and the factory restore goes too.

  6. Deploy all apps:
       ./deploy-all.sh <ip>

  Or do steps 5-6 from the menu:   ./roomwizard.sh

  Full guide: COMMISSIONING.md

<!-- NEXT_STEPS_END -->

## Phase 2: System Setup (SSH)

The [`commissioning/provision.sh`](commissioning/provision.sh) script is run once over SSH after the device first boots.
It **disables** the Steelcase services and installs the generic app launcher framework.

⚠️ **Disable and remove are different acts, and only one of them is the default.**

| | What it does | Default? | Reversible |
|---|---|---|---|
| **disable** | stops and de-registers the vendor services, writes the watchdog bypass. Re-applied on **every boot** by the init script. Deletes nothing. | **yes** | yes |
| **remove** (`--remove`) | additionally *deletes* the named vendor stacks — ~178 MB plus the 472 MB factory-restore payload | no, opt-in | no — needs the host-side card image |
| **deep clean** (`--deep-clean`) | `--remove` plus the whitelist sweeps: everything in `/etc/rc*.d`, `/opt` and the data partitions that the keep-list does not name. ~560 MB more | no, opt-in | no — needs the host-side card image |

So a plain `./commissioning/provision.sh <ip>` **deletes nothing**. Both destructive flags read the same
[`device-files/clean-rules.conf`](device-files/clean-rules.conf) and differ only by its `sweeps`
group; both ask for a full-card backup before they start. Use `--dry-run` with either to see the
resolved list first.

⚠️ **Neither is undoable on the device.** The 472 MB on-device factory-restore payload is deleted with
the rest of the vendor stack — it would only restore software whose start-up mechanism the same clean
removes, so keeping it preserves the ability to undo a commissioning it can no longer perform.
`--keep-factory` opts out; the 5 MB fallback kernel is kept either way; p1 is never written.

### What it does

Nothing in the list below is written out in `commissioning/provision.sh`. Steps 1–3 are one compiled plan over
[`device-files/provision-rules.conf`](device-files/provision-rules.conf) and step 5 is one compiled plan
over [`device-files/clean-rules.conf`](device-files/clean-rules.conf) — the **same two data files**
`commissioning/commission-offline.sh` reads, so the SSH pass and the offline pass cannot drift.

1. **Provision** — the boot scripts (`audio-enable`, `time-sync`, `99-security.conf`,
   `roomwizard-app`, `disable-steelcase.sh`), the `rc*.d` links (`S28`, `S29`, `S30avahi-daemon`,
   `S99roomwizard-app` in rc2–5.d), the sshd directives, the `/var/watchdog_test` bypass, and the two
   config fix-ups. Stale `rc*.d` links from an earlier install are removed first.
2. **Disable** — runs `disable-steelcase.sh`: watchdog bypass, a fresh crontab, services stopped.
3. **Apply** the sysctl settings to the running kernel.
4. **Report** what vendor software is still on disk.
5. **Clean**, only if asked: `--remove` or `--deep-clean`.
6. **Reboot.**

⚠️ **A plan record is state; running a script is an action.** `disable-steelcase.sh` is *installed* by
the plan and therefore also lands on an offline-commissioned card, but *running* it stops live processes
and writes a crontab, so it has no offline equivalent by nature. `/etc/init.d/roomwizard-app` re-runs it
on every boot, which is what makes the offline path's omission harmless rather than a gap.

### Usage

```bash
./commissioning/provision.sh <target>                    # system setup + reboot (deletes nothing)
./commissioning/provision.sh <target> --remove           # + delete the named vendor stacks
./commissioning/provision.sh <target> --remove --dry-run       # list what --remove would delete
./commissioning/provision.sh <target> --deep-clean --dry-run   # list what deep clean would delete
./commissioning/provision.sh <target> --deep-clean       # + the whitelist sweeps (~560 MB more)
./commissioning/provision.sh <target> --deep-clean --keep-factory   # ... but keep the restore payload
./commissioning/provision.sh <target> --status           # report only, no changes
./commissioning/provision.sh <target> --hostname rw09    # set the host name only. NO reboot.
```

`<target>` is an IPv4 address **or** a host name. `--status` also md5s the two deployed scripts
against the repo's and reports `matches repo` or `DRIFTED` per file.

### mDNS and `--hostname`

Phase 2 enables mDNS by symlinking `/etc/init.d/avahi-daemon` into `rc5.d`. The daemon and its
init script are already on the vendor image; the image just ships no boot link, so it never
started. After the reboot the unit answers to `<hostname>.local`:

```bash
ssh root@rw09.local
./commissioning/provision.sh rw09.local --status
```

This is only useful once the unit has a **unique** name — every unit cloned from the vendor image
claims `RW09`, and avahi would resolve the collision by renaming to `RW09-2.local`. Phase 1
prompts for the name; `--hostname NAME` is the way to set it on a unit that is **already
commissioned**, because it is targeted and does **not** reboot. That matters for a unit in
service as a live display.

### Why is this needed?

The Steelcase firmware includes a cron-based software watchdog (`/opt/sbin/watchdog/watchdog.sh`)
that checks every 5 minutes whether HSQLDB, Jetty, and the browser are running. When these
services are absent (which they are after we repurpose the device), the watchdog triggers a repair
cycle and eventually **reboots the device** (~70 min after first failure). It also includes a
backlight schedule that turns the screen off at 19:00 on weekdays.

`commissioning/provision.sh` disables all of these non-essential mechanisms. See
[SYSTEM_ANALYSIS.md](SYSTEM_ANALYSIS.md#52-as-we-run-it--game-mode) for the complete rationale.

## Phase 3: Deploy Apps

After both commissioning phases, deploy apps to the device.

### All at once (recommended)
```bash
./deploy-all.sh <ip>              # build + deploy all components
./deploy-all.sh --list            # show discovered components
```

### Individually
```bash
cd native_apps       && ./build-and-deploy.sh <ip> set-default
cd vnc_client        && ./build-and-deploy.sh <ip>
cd scummvm-roomwizard && ./build-and-deploy.sh <ip>
```

The `set-default` flag makes that app start on boot.
After deploying, reboot: `ssh root@<ip> reboot`

### From a bundle, with no toolchain — the delivery mode

⚠️ **Everything above BUILDS.** `deploy-all.sh <ip>` and every `build-and-deploy.sh` need
`arm-linux-gnueabihf-gcc`, and ScummVM needs WSL and a C++ cross-compiler too. Someone who has been
handed a device has none of that, which is what a release bundle is for:

```bash
./release.sh --stage-only                                  # on a build host, once
./deploy-all.sh --from-bundle build/release <ip>            # anywhere, no compiler
./deploy-all.sh --from-bundle roomwizard-<tag>.tar.gz <ip>  # or from the tarball
```

It stops the running app, installs, md5-verifies every file against the bundle's manifest, asserts `+x`
on every entry declared executable, sets `default-app` and restarts the launcher. Modes come from the
manifest, never from the transfer — see [`CLAUDE.md`](CLAUDE.md) → *Bundles*.

### ⚠️ USB host mode is not in any bundle, and cannot be

A commissioned unit has **no USB host mode**, whichever path commissioned it. That is by construction:
USB host needs the `usb_host` component, which patches the DTB inside `uImage-system` on **p1** — the one
partition every offline tool refuses to touch, because an untouched p1 is what keeps a power cycle a free
undo. `release.sh` therefore excludes `usb_host` from every bundle, deliberately.

The one command that adds it, over SSH after the unit boots:

```bash
cd usb_host && ./build-and-deploy.sh <ip>      # needs bc libssl-dev bison flex python3
```

That needs a full toolchain host — precisely what the delivery mode does not have. The tension is
recorded, not solved: [`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md) F15.

### Switching Apps

To switch which app starts on boot, just set a different default:
```bash
# Switch to VNC client
ssh root@<ip> 'echo /opt/vnc_client/vnc_client > /opt/roomwizard/default-app'

# Switch to native games
ssh root@<ip> 'echo /opt/games/game_selector > /opt/roomwizard/default-app'

# Check current default
ssh root@<ip> 'cat /opt/roomwizard/default-app'
```

Then reboot or restart the service: `ssh root@<ip> /etc/init.d/roomwizard-app restart`

## Architecture

**Two data files hold every decision; the scripts are executors over them.** Neither the SSH pass nor
the offline pass decides what to install or delete, which is what makes "the result is the same either
way" a fact rather than an intention.

```
device-files/clean-rules.conf      WHAT IS REMOVED     <type> <group> <path> <reason>
device-files/provision-rules.conf  WHAT IS INSTALLED   <type> <group> <mode> <target> <source> <reason>

lib/rw-clean.sh       parses clean-rules.conf     -> a plan, plus the offline executor
lib/rw-provision.sh   parses provision-rules.conf -> a plan, plus BOTH executors
lib/rw-bundle.sh      the bundle layout, plus the SSH bundle installer
lib/rw-identify.sh    which card, which partition — by content and POSITION, never by UUID

roomwizard.sh                          Front door: a menu over everything below
commissioning/card-prep.sh             Phase 1: SD card (offline). A SUBROUTINE of the next one
commissioning/commission-offline.sh    All three phases in ONE offline pass — the delivery path
commissioning/provision.sh             Phase 2: SSH provision + optional clean, ends in a reboot
deploy-all.sh                          Phase 3: build + deploy everything
deploy-all.sh --from-bundle            Phase 3 with NO toolchain — install a release bundle
release.sh                             Build all components + stage one offline bundle
device-files/disable-steelcase.sh      Device payload: watchdog bypass + fresh crontab, every boot
device-files/roomwizard-app            Device payload: installed as /etc/init.d/roomwizard-app
*/build-and-deploy.sh                  One per component
```

⚠️ **`commissioning/card-prep.sh` is step 3 *of* `commissioning/commission-offline.sh`, not an alternative to it.** The
name reads like a sibling and is misleading; renaming it is recorded in
[`IMPROVEMENT_PLAN.md`](IMPROVEMENT_PLAN.md) C12.

### On-device layout
```
/opt/roomwizard/
├── disable-steelcase.sh         Bloatware cleanup (run on every boot)
├── apps/*.app                   Launcher manifests (INI: name=, exec=, icon=, args=)
├── icons/*.ppm                  Tile icons, PPM P6
└── default-app                  One line: path to executable (e.g. /opt/games/game_selector)

/etc/init.d/
├── roomwizard-app               Generic app launcher (S99 in rc2-5.d)
├── audio-enable                 Speaker amplifier setup (S29)
└── time-sync                    rdate at boot (S28) — the RTC has no battery

/etc/sysctl.d/99-security.conf   Kernel hardening; there is no iptables on this image
/var/watchdog_test               The Steelcase software-watchdog bypass
/opt/games/                      Native games + tools
/opt/vnc_client/                 VNC client binary + config
/opt/scummvm/                    ScummVM, where installed
```

⚠️ **Every `rc*.d` link above is also on `clean-rules.conf`'s keep list.** A link the whitelist does not
name is deleted by the next `--deep-clean`, so the pair is asserted rather than remembered —
`rw_provision_check_keeps` refuses to provision if one is missing.

## Troubleshooting

### "No RoomWizard rootfs is mounted" (Phase 1)

The script prints the diagnosis itself — if a disk with the RoomWizard partition layout is present
it names it and gives you the exact `mount` command. If it found no such disk, the card is not
visible to Linux at all; on WSL it must first be attached from Windows:

```bash
wsl --mount \\.\PHYSICALDRIVEn --bare
sudo mkdir -p /mnt/rw
sudo mount /dev/sdX6 /mnt/rw          # p6 is the rootfs, always
```

**Do not go looking for a particular UUID** — see [*Finding the card*](#finding-the-card).

### Device reboots after ~70 minutes
System setup (Phase 2) wasn't completed. Run `./commissioning/provision.sh <ip>`.

### Screen goes blank at 19:00
The backlight cron wasn't disabled. Run `./commissioning/provision.sh <ip>` or manually:
```bash
ssh root@<ip> /opt/roomwizard/disable-steelcase.sh
```

### No app starts after reboot
No default app configured. Set one:
```bash
ssh root@<ip> 'echo /opt/games/game_selector > /opt/roomwizard/default-app'
ssh root@<ip> reboot
```

## Backups

Phase 1 creates backups on the SD card:
- `/etc/shadow.backup`
- `/etc/ssh/sshd_config.backup`
- `/etc/network/interfaces.backup`

Phase 2 backs up the original crontab:
- `/var/crontab.steelcase.bak`

## Related Documentation

- [SYSTEM_ANALYSIS.md](SYSTEM_ANALYSIS.md) — Hardware specs, watchdog details, cron job tables
- [native_apps/README.md](native_apps/README.md) — Native apps development docs
- [vnc_client/README.md](vnc_client/README.md) — VNC client docs
- [scummvm-roomwizard/README.md](scummvm-roomwizard/README.md) — ScummVM backend docs
