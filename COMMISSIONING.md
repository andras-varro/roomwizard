# RoomWizard Commissioning

This document describes the two-phase commissioning process for RoomWizard devices.

## Overview

Commissioning is split into two phases:

| Phase | Script | Connection | When |
|-------|--------|------------|------|
| **1. SD Card** | `commission-roomwizard.sh` | Offline (SD in PC) | Once per device |
| **2. System Setup** | `setup-device.sh` | SSH over network | Once per device |

After both phases, deploy a project and set it as the default boot app.

## Phase 1: SD Card Commissioning

The [`commission-roomwizard.sh`](commission-roomwizard.sh) script configures the device offline by mounting its SD card on a Linux machine.

### What it does
- Sets root password (SHA-512 hash in `/etc/shadow`)
- Enables SSH (root login, password + pubkey auth)
- Optionally installs your SSH public key
- Configures DHCP networking on eth0
- Backs up every file it modifies

### Usage

1. **Insert the SD card** into a Linux machine (or WSL)
2. Run:
   ```bash
   chmod +x commission-roomwizard.sh
   ./commission-roomwizard.sh
   ```
3. Follow the prompts for password / SSH key
4. Unmount, re-insert into device, power on

## Phase 2: System Setup (SSH)

The [`setup-device.sh`](setup-device.sh) script is run once over SSH after the device first boots.
It disables Steelcase bloatware and installs the generic app launcher framework.

### What it does
1. Deploys [`disable-steelcase.sh`](disable-steelcase.sh) → `/opt/roomwizard/`
2. Runs it (watchdog bypass, cron cleanup, service stop/disable)
3. Installs [`roomwizard-app-init.sh`](roomwizard-app-init.sh) as `/etc/init.d/roomwizard-app` (S99)
4. Deploys audio-enable boot script (GPIO12 speaker amplifier)
5. Deploys time-sync boot script (rdate)
6. Optionally removes bloatware files (`--remove`, ~178 MB freed)
7. Reboots

### Usage

```bash
./setup-device.sh <ip>              # system setup + reboot
./setup-device.sh <ip> --remove     # + remove bloatware files
./setup-device.sh <ip> --status     # show device status only
```

### Why is this needed?

The Steelcase firmware includes a cron-based software watchdog (`/opt/sbin/watchdog/watchdog.sh`)
that checks every 5 minutes whether HSQLDB, Jetty, and the browser are running. When these
services are absent (which they are after we repurpose the device), the watchdog triggers a repair
cycle and eventually **reboots the device** (~70 min after first failure). It also includes a
backlight schedule that turns the screen off at 19:00 on weekdays.

`setup-device.sh` disables all of these non-essential mechanisms. See
[SYSTEM_ANALYSIS.md](SYSTEM_ANALYSIS.md#game-mode-optimization) for the complete rationale.

## Deploying a Project

After both commissioning phases, deploy a project and set it as the default boot app:

### Native Games
```bash
cd native_games
./build-and-deploy.sh <ip> set-default    # build + deploy + set as boot app
```

### VNC Client
```bash
cd vnc_client
./build-and-deploy.sh <ip> set-default    # build + deploy + set as boot app
```

### ScummVM
```bash
cd scummvm-roomwizard
./build-and-deploy.sh deploy              # build + deploy to /opt/games/
# Then set as default:
ssh root@<ip> 'echo /opt/games/scummvm > /opt/roomwizard/default-app'
```

After setting the default app, reboot: `ssh root@<ip> reboot`

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

```
commission-roomwizard.sh         Phase 1: SD card (offline)
setup-device.sh                  Phase 2: SSH one-time setup
disable-steelcase.sh             Shared: disable bloatware (idempotent)
roomwizard-app-init.sh           Generic init: starts /opt/roomwizard/default-app
native_games/build-and-deploy.sh Build + deploy games (no system setup)
vnc_client/build-and-deploy.sh   Build + deploy VNC client (no system setup)
```

### On-device layout
```
/opt/roomwizard/
├── disable-steelcase.sh         Bloatware cleanup (run on every boot)
└── default-app                  One line: path to executable (e.g. /opt/games/game_selector)

/etc/init.d/
├── roomwizard-app               Generic app launcher (S99)
├── audio-enable                 Speaker amplifier setup (S29)
└── time-sync                    NTP via rdate (S28)

/opt/games/                      Native games binaries
/opt/vnc_client/                 VNC client binary + config
```

## Troubleshooting

### "Could not find mounted rootfs partition" (Phase 1)
Mount the SD card manually:
```bash
sudo mkdir -p /mnt/rw
sudo mount /dev/sdX6 /mnt/rw    # Replace X with your device letter
```

### Device reboots after ~70 minutes
System setup (Phase 2) wasn't completed. Run `./setup-device.sh <ip>`.

### Screen goes blank at 19:00
The backlight cron wasn't disabled. Run `./setup-device.sh <ip>` or manually:
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
- [SYSTEM_SETUP.md](SYSTEM_SETUP.md) — Manual setup guide (reference)
- [native_games/README.md](native_games/README.md) — Game development docs
- [vnc_client/README.md](vnc_client/README.md) — VNC client docs
