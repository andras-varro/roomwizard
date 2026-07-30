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
| [HARDWARE_INSPECTION.md](HARDWARE_INSPECTION.md) | Physical checks needing a screwdriver. Retired once filled in. | Before opening the case |

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

### 1. Commission the device (once)
```bash
# Phase 1: SD card — set password, SSH, DHCP
./commission-roomwizard.sh

# Insert SD card, boot device, find its IP...
# deploy private key for SSH access:
# ssh-copy-id -i ~/.ssh/id_rsa.pub root@<ip>
# login once to add the device to known_hosts:
# ssh root@<ip>

# Phase 2: SSH — disable bloatware, install app launcher
./setup-device.sh <ip>
```

### 1b. Reclaim disk space (optional, recommended)
```bash
./setup-device.sh <ip> --remove       # vendor bloatware (~178 MB)
./setup-device.sh <ip> --deep-clean   # extended cleanup (~560 MB more)
./setup-device.sh <ip> --status       # report current state
```

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
├── commission-roomwizard.sh     # Phase 1: SD card commissioning (offline)
├── setup-device.sh              # Phase 2: SSH system setup (one-time)
├── deploy-all.sh                # Phase 3: build + deploy all components
├── disable-steelcase.sh         # Bloatware cleanup (deployed to device)
├── roomwizard-app-init.sh       # Generic init script (deployed to device)
├── COMMISSIONING.md             # Commissioning workflow
├── SYSTEM_ANALYSIS.md           # Hardware analysis
├── native_apps/                # C apps (games, launcher, tools)
├── browser_games/               # HTML5 games + LED control
├── scummvm-roomwizard/          # ScummVM backend
└── vnc_client/                  # VNC remote desktop viewer
```

### Separation of Concerns

| Layer | Script | Runs |
|-------|--------|------|
| **SD card setup** | `commission-roomwizard.sh` | Once (offline) |
| **System setup** | `setup-device.sh` | Once (SSH) |
| **Deploy all** | `deploy-all.sh` | After setup (builds + deploys everything) |
| **Bloatware cleanup** | `disable-steelcase.sh` | On setup + every boot |
| **App launcher** | `roomwizard-app-init.sh` | Every boot (respawn loop, reads `/opt/roomwizard/default-app`) |
| **Project deploy** | `*/build-and-deploy.sh` | Per project (build + deploy + app manifests) |

Each project's `build-and-deploy.sh` handles building, deploying binaries, and installing
`.app` manifests to `/opt/roomwizard/apps/` for the visual launcher.
System setup is done once by `setup-device.sh` — no duplication across projects.

## Projects

1. **Native Apps** — Direct framebuffer C apps (Snake, Tetris, Pong, App Launcher, Hardware Test). USB input fully supported: keyboard, mouse, and Xbox 360 controller via unified evdev polling.
2. **Browser Games** — HTML5 brick breaker with LED feedback
3. **ScummVM** — Custom backend for classic point-and-click adventures (OPL/AdLib music, touch controls, virtual keyboard). Independent evdev input: keyboard, mouse, and gamepad.
4. **VNC Client** — Lightweight VNC viewer for Raspberry Pi remote desktop (weather/clock) with keyboard/mouse forwarding (~5% CPU, bilinear scaling)

The **App Launcher** is a visual grid shell deployed by `native_apps/build-and-deploy.sh`.
It scans manifest files from all projects and displays them as touch-friendly icon tiles.
The init script respawns it automatically when an app exits.

For hardware specs, see **[Hardware Platform](SYSTEM_ANALYSIS.md#hardware-platform)** in System Analysis.


---

## Project Status

See **[IMPROVEMENT_PLAN.md](IMPROVEMENT_PLAN.md)** for the current backlog. Highlights:

- Known bugs are catalogued there with `file:line` references.
- **Kernel upgrades are out of scope** — vendor source is unavailable and a mainline port would
  break the runtime bpp switching that ScummVM and the VNC client depend on. See
  [Kernel Upgrade Assessment](SYSTEM_ANALYSIS.md#kernel-upgrade-assessment).

## A note on secrets

`vnc_client/vnc_client.conf` is **gitignored** because it holds a plaintext VNC password.
Copy the template to create your own:

```bash
cp vnc_client/vnc_client.conf.example vnc_client/vnc_client.conf
# then edit host/password
```
