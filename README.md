# RoomWizard Project

> **Development projects for the Steelcase RoomWizard embedded Linux device**

## Quick Navigation

### 📊 Status & Setup
- **[System Analysis](SYSTEM_ANALYSIS.md)** - Hardware and firmware analysis
- **[System Setup](SYSTEM_SETUP.md)** - SSH access and configuration

### 🎮 Projects
- **[Native Games](native_games/)** - High-performance C games with direct framebuffer rendering
- **[Browser Games](browser_games/)** - HTML5 games with LED control
- **[ScummVM Backend](scummvm-roomwizard/)** - Classic adventure games port

---

## Project Overview

Three main projects for the RoomWizard embedded device:

1. **Native Games** - Direct framebuffer C games (Snake, Tetris, Pong)
2. **Browser Games** - HTML5 brick breaker with LED feedback
3. **ScummVM** - Custom backend for classic point-and-click adventures (with OPL/AdLib music, touch controls, virtual keyboard)

For detailed hardware specifications, see **[Hardware Platform](SYSTEM_ANALYSIS.md#hardware-platform)** in the System Analysis document.

## Repository Structure

```
roomwizard/
├── README.md                    # This file
├── SYSTEM_ANALYSIS.md           # Hardware analysis
├── SYSTEM_SETUP.md              # SSH & system setup
├── native_games/                # C games (Snake, Tetris, Pong)
├── browser_games/               # HTML5 games + LED control
└── scummvm-roomwizard/          # ScummVM backend
```

## Quick Start

### Native Games
```bash
cd native_games
./build-and-deploy.sh                          # build only
./build-and-deploy.sh 192.168.50.73            # build + deploy
./build-and-deploy.sh 192.168.50.73 permanent  # + boot service + reboot
```

### Browser Games
See [`browser_games/README.md`](browser_games/README.md)

### ScummVM
See [`scummvm-roomwizard/README.md`](scummvm-roomwizard/README.md)
