# RoomWizard Project

> **Development projects for the Steelcase RoomWizard embedded Linux device**

## Quick Navigation

### Setup & Commissioning
- **[Commissioning Guide](COMMISSIONING.md)** — Complete setup workflow (SD card → system setup → deploy)
- **[System Analysis](SYSTEM_ANALYSIS.md)** — Hardware specs and firmware analysis

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

1. **Native Apps** — Direct framebuffer C apps (Snake, Tetris, Pong, App Launcher, Hardware Test)
2. **Browser Games** — HTML5 brick breaker with LED feedback
3. **ScummVM** — Custom backend for classic point-and-click adventures (OPL/AdLib music, touch controls, virtual keyboard)
4. **VNC Client** — Lightweight VNC viewer for Raspberry Pi remote desktop (weather/clock) with touch pass-through (~5% CPU, bilinear scaling)

The **App Launcher** is a visual grid shell deployed by `native_apps/build-and-deploy.sh`.
It scans manifest files from all projects and displays them as touch-friendly icon tiles.
The init script respawns it automatically when an app exits.

For hardware specs, see **[Hardware Platform](SYSTEM_ANALYSIS.md#hardware-platform)** in System Analysis.
