# RoomWizard Project

> **Development projects for the Steelcase RoomWizard embedded Linux device**

## Quick Navigation

### 📊 Status & Setup
- **[Project Status](PROJECT_STATUS.md)** - Current development status and progress
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
3. **ScummVM** - Custom backend for classic point-and-click adventures

## Hardware

- **CPU:** ARM Cortex-A8 @ 300MHz
- **RAM:** 256MB (184MB available)
- **Display:** 800x480 touchscreen
- **OS:** Embedded Linux 4.14.52

## Repository Structure

```
roomwizard/
├── README.md                    # This file
├── PROJECT_STATUS.md            # Development status
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
./compile_for_roomwizard.sh
./deploy.sh 192.168.50.73 test
```

### Browser Games
See [`browser_games/README.md`](browser_games/README.md)

### ScummVM
See [`scummvm-roomwizard/README.md`](scummvm-roomwizard/README.md)
