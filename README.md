# RoomWizard Project

> **Development projects for the Steelcase RoomWizard embedded Linux device**

## Quick Navigation

## Documentation map

| Doc | What it owns | Read it when |
|---|---|---|
| [CLAUDE.md](CLAUDE.md) | How to build, deploy, and not break things. The trap list. | Before writing any code |
| [SYSTEM_ANALYSIS.md](SYSTEM_ANALYSIS.md) | Authoritative device facts: SoC, boot chain, display, audio, GPIO, unused hardware | Before touching anything hardware-related |
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

### 1. Commission the device (once)
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
```

Or all three phases in one offline pass, which is the delivery path — no SSH, one boot:

```bash
sudo ./commissioning/commission-offline.sh --bundle <tar.gz|dir>
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

### 2. Deploy a project
```bash
# All at once (recommended)
./deploy-all.sh <ip>

# Or individually:
cd native_apps
./build-and-deploy.sh <ip> set-default

cd vnc_client
./build-and-deploy.sh <ip> set-default

cd scummvm-roomwizard
./build-and-deploy.sh <ip> set-default
```

### 3. Reboot
```bash
ssh root@<ip> reboot
```

## Architecture

```
roomwizard/
├── roomwizard.sh                # Front door: a menu over everything below
├── deploy-all.sh                # Phase 3: build + deploy all components
├── release.sh                   # Build + stage an offline release bundle
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
| **SD card setup** | `commissioning/card-prep.sh` | Once (offline) |
| **System setup** | `commissioning/provision.sh` | Once (SSH) |
| **Deploy all** | `deploy-all.sh` | After setup (builds + deploys everything) |
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
