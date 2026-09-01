# RoomWizard Project

> **Development projects for the Steelcase RoomWizard embedded Linux device**

## Documentation map

| Doc | What it owns | Read it when |
|---|---|---|
| [CLAUDE.md](CLAUDE.md) | How to build, deploy, and not break things. The trap list. | Before writing any code |
| [SYSTEM_ANALYSIS.md](SYSTEM_ANALYSIS.md) | Authoritative device facts: SoC, boot chain, display, audio, GPIO, unused hardware | Before touching anything hardware-related |
| [HARDWARE.md](HARDWARE.md) | The board itself: parts, connectors, headers, the unpopulated XBee socket, the enclosure — with the teardown photos | Opening a unit, or probing a header |
| [IMPROVEMENT_PLAN.md](IMPROVEMENT_PLAN.md) | The single backlog — bugs and features with `file:line` | Before starting work, so you don't rediscover a known bug |
| [COMMISSIONING.md](COMMISSIONING.md) | Setup workflow: SD card → system setup → deploy | Bringing up a new unit |
| [SD_CARD_UPGRADE.md](SD_CARD_UPGRADE.md) | Optional 4 GB → 32 GB card upgrade | Only if you run out of disk |

Each component directory also has a `CLAUDE.md` (authoring guidance for that component) and a
`README.md` (what it is and how to use it):
[native_apps](native_apps/CLAUDE.md) · [scummvm-roomwizard](scummvm-roomwizard/CLAUDE.md) ·
[vnc_client](vnc_client/CLAUDE.md).

**The rule:** component docs describe their own code only. Device facts belong in
SYSTEM_ANALYSIS.md, open work belongs in IMPROVEMENT_PLAN.md. Link, don't copy.

### The device in one line

TI **OMAP3503** (ARM Cortex-A8 @ 600 MHz, **no GPU, no DSP**), 234 MB RAM, 800×480 LCD via
legacy `omapfb`/`omapdss` (no DRM/KMS), projected-capacitive touch, Linux 4.14.52, SysVinit.
All code is cross-compiled on the dev host and deployed over SSH — there is no local app to run.

### Projects
- **[Native Apps](native_apps/)** — High-performance C apps with direct framebuffer rendering
- **[Browser Games](browser_games/)** — HTML5 games with LED control
- **[ScummVM Backend](scummvm-roomwizard/)** — Classic adventure games port
- **[VNC Client](vnc_client/)** — Remote desktop viewer for Raspberry Pi displays

---

## Quick Start

> **Cloning:** the teardown photos in [HardwarePhotos/](HardwarePhotos/) are stored in
> **Git LFS**. Run `git lfs install` once before cloning, or those files arrive as
> one-line pointer stubs (`git lfs pull` fixes an existing clone). Nothing in the
> build or deploy path depends on them — they are documentation only.

### Start here — `./roomwizard.sh`

One menu over every path below, and the reason a single card write can produce a working unit at next
boot. It has **no logic of its own**: each item execs one of the scripts documented further down, and
every one of those stays non-interactive when called directly.

```bash
./roomwizard.sh
```

```
  1) Prepare the card            PHASE 1 of 3   offline; then 2 and 3, over ssh
  2) Set up a booted device      PHASE 2 of 3   ssh; cleans, ends in a reboot
  3) Deploy apps                 PHASE 3 of 3   ssh; source, bundle or release
  5) All three, in sequence      1 -> 2 -> 3    ssh between; you boot the unit

  6) THE WHOLE JOB, offline, one boot   <-- deliver a unit  (bundle, or fetch one)

  4) Device status               read-only
```

Which item you want depends on whether you are **delivering** a unit or **developing** on one. Both
paths are supported and neither is going away; they differ in how many boots and how much toolchain
they need.

### Delivering a unit — item 6, offline, one boot

Everything the two-phase path does over SSH, done to the card instead: card commissioning, system
setup and the binaries, in one pass. The unit works at first boot and never needs to be reachable.

```bash
sudo ./commissioning/commission-offline.sh --bundle <tar.gz|dir>
sudo ./commissioning/commission-offline.sh --release latest   # fetch the bundle first
```

⚠️ **`--release` is the one step here that opens a socket** — everything else is offline. It and
`--bundle` are mutually exclusive: both name a source of binaries, so pass one.

Make a bundle with `./release.sh --stage-only`, or publish one with `./release.sh --tag <tag>`.

### Installing from a published release — no cross-compiler

Every other mode builds, so every other mode needs `arm-linux-gnueabihf-gcc`. Someone handed a device
has no toolchain, so a published release is installable directly — over SSH, or onto a card:

```bash
./deploy-all.sh --from-release latest <ip>       # over SSH, to a set-up unit
./deploy-all.sh --from-release v1.0.0 <ip>       # a specific tag
```

The tarball's sha256 is checked against the digest GitHub publishes, and it is cached under
`build/release-cache/<tag>/`, so a second unit costs no second download. Digest mismatch refuses and
deletes the partial file. `v1.0.0` is the first published release (~125 MiB, one asset); the fetch
rules are in [lib/CLAUDE.md](lib/CLAUDE.md).

`--from-bundle <tar.gz|dir>` is the same install with a local bundle instead of a download.

### Developing on a unit — the two-phase SSH loop

This is the build/deploy loop, and what you want while writing code.

```bash
# Phase 1: SD card — set password, SSH, DHCP
./commissioning/card-prep.sh

# Insert SD card, boot device, find its IP...
# deploy private key for SSH access:
# ssh-copy-id -i ~/.ssh/id_rsa.pub root@<ip>
# login once to add the device to known_hosts:
# ssh root@<ip>

# Phase 2: SSH — provision, deep clean, the 500 mA USB budget on p1, reboot
./commissioning/provision.sh <ip>

# Phase 3: build + deploy everything (native_apps first — it provides the launcher)
./deploy-all.sh <ip>
./deploy-all.sh <ip> vnc_client      # or one component;  --list  to see them
```

⚠️ **Both bring-up paths CLEAN and WRITE p1 by default**, so that they leave the same unit. Each asks
once whether the card is backed up, before the first write. The opt-outs:

```bash
./commissioning/provision.sh <ip> --dry-run        # what would be deleted, what p1 would get
./commissioning/provision.sh <ip> --no-clean       # delete nothing
./commissioning/provision.sh <ip> --remove         # named vendor stacks only, no sweeps
./commissioning/provision.sh <ip> --no-usb-power   # leave p1 alone (budget stays at 100 mA)
./commissioning/provision.sh <ip> --no-usb         # no USB host mode at all; implies the above
./commissioning/provision.sh <ip> --status         # report current state; changes nothing
```

Neither the clean nor the p1 write is undoable **on the device**: the 472 MB factory-restore payload goes
with the rest of the vendor stack, and a power cycle no longer reverts p1. The vendor kernel is backed up
beside itself as `uImage-system.vendor`, which is the in-place remedy; a card pull is the fallback.
Details, and why the 500 mA value cannot be a boot-time script: [COMMISSIONING.md](COMMISSIONING.md).

## Architecture

```
roomwizard/
├── roomwizard.sh                # Front door: a menu over everything below
├── deploy-all.sh                # Phase 3: build + deploy all components
├── release.sh                   # Build + stage a bundle; --tag also publishes it
├── commissioning/
│   ├── card-prep.sh             # Phase 1: SD card commissioning (offline)
│   ├── provision.sh             # Phase 2: SSH system setup (one-time)
│   ├── commission-offline.sh    # Phases 1-3 in one offline pass
│   ├── set-hostname.sh          # /etc/hostname + /etc/hosts + dhclient.conf
│   └── clone-to-32gb.sh         # Clone a card onto a larger one
├── lib/                         # Sourced, never executed
│   ├── rw-identify.sh           # Which card, which partition, by content/position
│   ├── rw-clean.sh              # clean-rules.conf -> a plan
│   ├── rw-provision.sh          # provision-rules.conf -> a plan
│   ├── rw-usbpower.sh           # The ONE writer of p1: the 500 mA USB budget
│   ├── rw-ssh.sh                # The one "can I reach this device" gate
│   ├── rw-release.sh            # Resolve + fetch + verify a published release
│   └── rw-bundle.sh             # The release-bundle layout, both directions
├── device-files/                # Installed onto the device verbatim
│   ├── roomwizard-app           # Generic init script (app respawn loop)
│   ├── disable-steelcase.sh     # Bloatware cleanup (run at every boot)
│   ├── enable-usb-host.sh       # The /dev/mem MUSB host-mode patch
│   ├── usb-host                 # Runs it at every boot (S90)
│   ├── xpad-modules             # insmod -f the three controller modules (S89)
│   ├── clean-rules.conf         # What a clean removes, one reason per line
│   └── provision-rules.conf     # What the device ends up with
├── LICENSE.md                   # MIT, plus the third-party enumeration
├── COMMISSIONING.md             # Commissioning workflow
├── SYSTEM_ANALYSIS.md           # Hardware analysis
├── native_apps/                 # C apps (games, launcher, tools)
├── browser_games/               # HTML5 games + LED control
├── scummvm-roomwizard/          # ScummVM backend
├── usb_host/                    # USB host mode + Xbox controller modules
└── vnc_client/                  # VNC remote desktop viewer
```

### Separation of Concerns

| Layer | Script | Runs |
|-------|--------|------|
| **Front door** | `roomwizard.sh` | Whenever you'd rather not remember the flags (composition only) |
| **SD card setup** | `commissioning/card-prep.sh` | Once (offline) |
| **System setup** | `commissioning/provision.sh` | Once (SSH) |
| **All of the above, offline** | `commissioning/commission-offline.sh` | Once, offline, to deliver a unit |
| **Deploy all** | `deploy-all.sh` | After setup (builds + deploys everything) |
| **Build + stage + publish** | `release.sh` | Per release |
| **Bloatware cleanup** | `disable-steelcase.sh` | On setup + every boot |
| **App launcher** | `device-files/roomwizard-app` | Every boot (respawn loop, reads `/opt/roomwizard/default-app`) |
| **Project deploy** | `*/build-and-deploy.sh` | Per project (build + deploy + app manifests) |

Each project's `build-and-deploy.sh` handles building, deploying binaries, and installing
`.app` manifests to `/opt/roomwizard/apps/` for the visual launcher.
System setup is done once by `commissioning/provision.sh` — no duplication across projects.

## Projects

1. **Native Apps** — Direct framebuffer C apps (Snake, Tetris, Pong, App Launcher, Hardware Test). USB input fully supported: keyboard, mouse, and Xbox 360 controller via unified evdev polling.
2. **Browser Games** — HTML5 brick breaker with LED feedback
3. **ScummVM** — Custom backend for classic point-and-click adventures (OPL/AdLib music, touch controls, virtual keyboard). Independent evdev input: keyboard, mouse, and gamepad.
4. **VNC Client** — Lightweight VNC viewer for Raspberry Pi remote desktop (weather/clock) with keyboard/mouse forwarding (~5% CPU, bilinear scaling)

The **App Launcher** is a visual grid shell deployed by `native_apps/build-and-deploy.sh`.
It scans manifest files from all projects and displays them as touch-friendly icon tiles.
The init script respawns it automatically when an app exits.

For hardware specs, see **[Subsystems](SYSTEM_ANALYSIS.md#3-subsystems)** in the device reference.


---

## Project Status

See **[IMPROVEMENT_PLAN.md](IMPROVEMENT_PLAN.md)** for the current backlog. Highlights:

- Known bugs are catalogued there with `file:line` references.
- **Kernel upgrades are out of scope** — vendor source is unavailable and a mainline port would
  break the runtime bpp switching that ScummVM and the VNC client depend on. See
  [Kernel policy](SYSTEM_ANALYSIS.md#7-kernel-policy).

## A note on secrets

`vnc_client/vnc_client.conf` is **gitignored** because it holds a plaintext VNC password.
Copy the template to create your own:

```bash
cp vnc_client/vnc_client.conf.example vnc_client/vnc_client.conf
# then edit host/password
```

## Licence, and one no-warranty note

This project's own code is **MIT**. Two things it distributes are not, and both carry obligations that
MIT does not — the `scummvm` binary and its data (GPL-3.0-or-later), and the three controller kernel
modules (GPL-2.0-only, written source offer). Everything is enumerated in
[LICENSE.md](LICENSE.md), which is also where MIT's *scope* is stated: it governs the source and does not
decide the licence of a binary it is linked into.

⚠️ **No warranty, meant literally.** Commissioning writes to the SD card's boot partition by default (the
500 mA USB budget), the vendor stack is deleted by default, and recovering a unit that will not boot
means reaching the card — which means opening the case. That is feasible and it takes experience; an
inexperienced attempt can break the enclosure. `--no-usb-power` and `--no-clean` opt out of the two
irreversible steps.

Steelcase and RoomWizard are trademarks of their respective owner. This project is unaffiliated with,
and not endorsed by, Steelcase.
