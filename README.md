# RoomWizard Project

> **Development projects for the Steelcase RoomWizard embedded Linux device**

## Quick Navigation

### Setup & Commissioning
- **[Commissioning Guide](COMMISSIONING.md)** — Two-phase device setup workflow
- **[System Analysis](SYSTEM_ANALYSIS.md)** — Hardware specs and firmware analysis
- **[System Setup](SYSTEM_SETUP.md)** — Manual setup reference

### Projects
- **[Native Games](native_games/)** — High-performance C games with direct framebuffer rendering
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
# Native Games
cd native_games
./build-and-deploy.sh <ip> set-default

# VNC Client
cd vnc_client
./build-and-deploy.sh <ip> set-default

# ScummVM
cd scummvm-roomwizard
./build-and-deploy.sh deploy
ssh root@<ip> 'echo /opt/games/scummvm > /opt/roomwizard/default-app'
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
├── disable-steelcase.sh         # Bloatware cleanup (deployed to device)
├── roomwizard-app-init.sh       # Generic init script (deployed to device)
├── COMMISSIONING.md             # Commissioning workflow
├── SYSTEM_ANALYSIS.md           # Hardware analysis
├── SYSTEM_SETUP.md              # Manual setup reference
├── native_games/                # C games (Snake, Tetris, Pong)
├── browser_games/               # HTML5 games + LED control
├── scummvm-roomwizard/          # ScummVM backend
└── vnc_client/                  # VNC remote desktop viewer
```

### Separation of Concerns

| Layer | Script | Runs |
|-------|--------|------|
| **SD card setup** | `commission-roomwizard.sh` | Once (offline) |
| **System setup** | `setup-device.sh` | Once (SSH) |
| **Bloatware cleanup** | `disable-steelcase.sh` | On setup + every boot |
| **App launcher** | `roomwizard-app-init.sh` | Every boot (reads `/opt/roomwizard/default-app`) |
| **Project deploy** | `*/build-and-deploy.sh` | Per project (build + deploy only) |

Each project's `build-and-deploy.sh` handles only building and deploying that project's binaries.
System setup is done once by `setup-device.sh` — no duplication across projects.

## Projects

1. **Native Games** — Direct framebuffer C games (Snake, Tetris, Pong, Game Selector)
2. **Browser Games** — HTML5 brick breaker with LED feedback
3. **ScummVM** — Custom backend for classic point-and-click adventures (OPL/AdLib music, touch controls, virtual keyboard)
4. **VNC Client** — Lightweight VNC viewer for Raspberry Pi remote desktop (weather/clock) with touch pass-through (~5% CPU, bilinear scaling)

For hardware specs, see **[Hardware Platform](SYSTEM_ANALYSIS.md#hardware-platform)** in System Analysis.
