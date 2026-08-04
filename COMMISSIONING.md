# RoomWizard Commissioning

This document describes the commissioning process for RoomWizard devices.

## Overview

Bring-up is three phases, run in order, each by its own script:

| Phase | Script | Connection | When |
|-------|--------|------------|------|
| **1. SD Card** | `commission-roomwizard.sh` | Offline (SD in PC) | Once per device |
| **2. System Setup** | `setup-device.sh` | SSH over network | Once per device |
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

| Partition (UUID prefix) | Mounted as | Cleanup that touches it |
|---|---|---|
| `108a1490` | `/` (p6) | `/opt/jetty*`, `/opt/openjre-8`, `/usr/share/cjkfont`, WebKit/GTK/X11 libs, `/etc/init.d/*` |
| `d5758df8` | `/home/root/data` | `websign`, `conctest`, the 79 MB `cron/log` truncation |
| `da4cda60` | `/home/root/log` | `Xorg.0.log`, `browser.err`, `jettystart`, … |
| `26a7a226` | `/home/root/backup` | the 474 MB `factory/*.img` deep-clean option |

`commission-roomwizard.sh` locates exactly **one** of these (p6, by UUID). Three further reasons:
`disable-steelcase.sh` is re-run **on every boot** by the init script, so the disable half is
inherently a running-system job; already-commissioned units exist and must stay cleanable without
pulling their card; and the deep clean measures `df` before/after and offers a dry run, which
assumes a live device. Commissioning's job is to produce a device you can *reach* — a cleanup that
broke boot there would cost you SSH and tell you nothing.

## Phase 1: SD Card Commissioning

The [`commission-roomwizard.sh`](commission-roomwizard.sh) script configures the device offline by mounting its SD card on a Linux machine.

### What it does
- Sets root password (SHA-512 hash in `/etc/shadow`)
- **Sets the host name** in `/etc/hostname` *and* `/etc/hosts` (see below)
- Enables SSH (root login, password + pubkey auth)
- Optionally installs your SSH public key
- Configures DHCP networking on eth0
- Backs up every file it modifies

### The host name, and why `/etc/hosts` is rewritten too

The vendor image ships `/etc/hostname` as `RW09` and `/etc/hosts` as:

```text
127.0.0.1 localhost
```

Setting `/etc/hostname` alone leaves the bogus mapping in place.
The prompt therefore writes both, via [`set-hostname.sh`](set-hostname.sh) — one implementation
shared with `setup-device.sh --hostname`, so the offline and over-SSH paths cannot drift.

Give each unit a **unique single label** (`rw09`, not `rw09.local` — mDNS appends `.local`
itself). Combined with Phase 2 enabling mDNS, that is what makes `ssh root@rw09.local` and
`./setup-device.sh rw09.local` work instead of hunting for a DHCP lease.

### Usage

1. **Insert the SD card** into a Linux machine (or WSL)
2. Run:
   ```bash
   chmod +x commission-roomwizard.sh
   ./commission-roomwizard.sh
   ```
   If auto-detection cannot find p6, mount it by hand and `export ROOTFS=/mnt/rw` —
   the script honours a pre-set `$ROOTFS`.
3. Follow the prompts for password / host name / SSH key
4. Unmount, re-insert into device, power on

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
       ./setup-device.sh <ip>

     To also DELETE ~178 MB of vendor files (permanent), add --remove.

  6. Deploy all apps:
       ./deploy-all.sh <ip>

  Or do steps 5-6 from the menu:   ./roomwizard.sh

  Full guide: COMMISSIONING.md

<!-- NEXT_STEPS_END -->

## Phase 2: System Setup (SSH)

The [`setup-device.sh`](setup-device.sh) script is run once over SSH after the device first boots.
It **disables** the Steelcase services and installs the generic app launcher framework.

⚠️ **Disable and remove are different acts, and only one of them is the default.**

| | What it does | Default? | Reversible |
|---|---|---|---|
| **disable** | stops and de-registers the vendor services, writes the watchdog bypass. Re-applied on **every boot** by the init script. Deletes nothing. | **yes** | yes |
| **remove** (`--remove`) | additionally *deletes* ~178 MB of vendor files | no, opt-in | no — needs a reflash |
| **deep clean** (`--deep-clean`) | `--remove` plus ~560 MB more, including the 474 MB on-device factory restore image | no, opt-in | no — needs a reflash |

So a plain `./setup-device.sh <ip>` **deletes nothing**. Use `--deep-clean --dry-run` to see what
the destructive option would remove before committing to it.

### What it does
1. Deploys [`disable-steelcase.sh`](disable-steelcase.sh) → `/opt/roomwizard/`
2. Runs it (watchdog bypass, cron cleanup, service stop/disable)
3. Installs [`roomwizard-app-init.sh`](roomwizard-app-init.sh) as `/etc/init.d/roomwizard-app` (S99)
4. Deploys audio-enable boot script (GPIO12 speaker amplifier)
5. Deploys time-sync boot script (rdate)
6. Enables **mDNS** — links the image's existing `avahi-daemon` into `rc5.d` (S30)
7. Optionally removes bloatware files (`--remove`, ~178 MB freed)
8. Reboots

### Usage

```bash
./setup-device.sh <target>                    # system setup + reboot (deletes nothing)
./setup-device.sh <target> --remove           # + delete bloatware files (~178 MB)
./setup-device.sh <target> --deep-clean --dry-run   # list what deep clean would delete
./setup-device.sh <target> --deep-clean       # + extended cleanup (~560 MB more)
./setup-device.sh <target> --status           # report only, no changes
./setup-device.sh <target> --hostname rw09    # set the host name only. NO reboot.
```

`<target>` is an IPv4 address **or** a host name. `--status` also md5s the two deployed scripts
against the repo's and reports `matches repo` or `DRIFTED` per file.

### mDNS and `--hostname`

Phase 2 enables mDNS by symlinking `/etc/init.d/avahi-daemon` into `rc5.d`. The daemon and its
init script are already on the vendor image; the image just ships no boot link, so it never
started. After the reboot the unit answers to `<hostname>.local`:

```bash
ssh root@rw09.local
./setup-device.sh rw09.local --status
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

`setup-device.sh` disables all of these non-essential mechanisms. See
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
commission-roomwizard.sh                Phase 1: SD card (offline)
setup-device.sh                        Phase 2: SSH one-time setup
deploy-all.sh                          Phase 3: build + deploy all components
disable-steelcase.sh                   Shared: disable bloatware (idempotent)
roomwizard-app-init.sh                 Generic init: starts /opt/roomwizard/default-app
native_apps/build-and-deploy.sh        Build + deploy native apps
vnc_client/build-and-deploy.sh         Build + deploy VNC client
scummvm-roomwizard/build-and-deploy.sh Build + deploy ScummVM
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

/opt/games/                      Native games + ScummVM binaries
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
- [native_apps/README.md](native_apps/README.md) — Native apps development docs
- [vnc_client/README.md](vnc_client/README.md) — VNC client docs
- [scummvm-roomwizard/README.md](scummvm-roomwizard/README.md) — ScummVM backend docs
