# Native Games for RoomWizard

Native C games and tools for the Steelcase RoomWizard (300MHz ARMv7, 800×480 framebuffer). Direct framebuffer rendering — no browser, no overhead.

See [PROJECT.md](PROJECT.md) for architecture and development status.

## Table of Contents

1. [Games & Tools](#games--tools)
2. [Build & Deploy](#build--deploy-cross-compile-from-wsl)
3. [System Optimization](#system-optimization)
4. [Permanent Game Mode](#permanent-game-mode-boot)
5. [Game Selector Markers](#game-selector-markers)
6. [Resources](#resources)

---

## Games & Tools

| Binary | Type | Controls / Notes |
|---|---|---|
| `snake` | Game | Tap direction to steer |
| `tetris` | Game | Tap left/right to move, center to rotate, bottom to drop |
| `pong` | Game | Touch-drag to move paddle |
| `hardware_test` | Tool | Diagnostics; visible in game selector |
| `game_selector` | Launcher | Main menu — auto-starts on boot |
| `watchdog_feeder` | Daemon | Feeds `/dev/watchdog` every 30 s; hidden from menu |
| `unified_calibrate` | Tool | Touch + bezel calibration; hidden from menu |

## Build & Deploy (cross-compile from WSL)

```bash
cd native_games

# Build only
./build-and-deploy.sh

# Build + deploy binaries
./build-and-deploy.sh 192.168.50.73

# Build + deploy + install boot service + cleanup + reboot
./build-and-deploy.sh 192.168.50.73 permanent --remove

# Cleanup services only (no build/deploy)
./build-and-deploy.sh 192.168.50.73 cleanup

# Cleanup services + remove bloatware files
./build-and-deploy.sh 192.168.50.73 cleanup --remove
```

The script cross-compiles all binaries, uploads them to `/opt/games/`, sets permissions, and creates `.noargs`/`.hidden` marker files.

**Permanent mode** also:
- Disables watchdog test/repair (prevents unwanted reboots)
- Stops and disables unnecessary services (browser, X11, Jetty, HSQLDB, etc.)
- Frees ~80 MB RAM
- See [System Optimization](#system-optimization) below

Rebuilding just `game_selector` after changes:
```bash
arm-linux-gnueabihf-gcc -O2 -static -I. \
  common/framebuffer.c common/touch_input.c common/hardware.c \
  common/common.c common/ui_layout.c \
  game_selector/game_selector.c -o build/game_selector -lm
```

---

## System Optimization

### The Problem

The RoomWizard firmware includes:
1. **Watchdog system** that monitors browser/webserver stack (causes reboots in game mode)
2. **Bloatware files** (~178 MB) that are never used in game mode
3. **Vulnerable software** (Jetty 9.4.11, HSQLDB 2.0.0, outdated Java)

**Symptoms:**
- Device reboots unexpectedly every 3-4 hours
- 47% disk usage (405 MB used out of 931 MB)
- 45% RAM usage (106 MB used out of 234 MB)

### Two-Level Cleanup

**Level 1: Service Cleanup (Safe, Reversible)**
- Disables watchdog test/repair
- Stops unnecessary services
- Frees ~80 MB RAM
- Prevents reboots

**Level 2: File Removal (Aggressive, Permanent)**
- Removes bloatware files
- Frees ~178 MB disk space
- Removes vulnerable software
- Requires SD card backup to restore

### Quick Service Cleanup

```bash
./build-and-deploy.sh 192.168.50.73 cleanup
```

**Result:** No reboots, 80 MB RAM freed, fully reversible

### Full Cleanup with File Removal

```bash
# Analyze + remove files (requires confirmation)
./build-and-deploy.sh 192.168.50.73 cleanup --remove
```

**Bloatware Removed:**
| Package | Size | Security Issue |
|---------|------|----------------|
| Jetty 9.4.11 | 43 MB | CVE-2019-10241, CVE-2019-10247 |
| OpenJRE 8 | 93 MB | Outdated 2018 build |
| HSQLDB 2.0.0 | 3.5 MB | CVE-2018-16336 (RCE) |
| CJK Fonts | 31 MB | Not needed |
| X11 Data | 5.2 MB | Not needed |
| SNMP MIBs | 2.5 MB | Not needed |
| **Total** | **~178 MB** | |

**Result:** 178 MB disk freed, vulnerabilities removed, **permanent**

### Services Disabled

| Service | Purpose | Why Disable |
|---------|---------|-------------|
| webserver, browser, x11 | Web interface | Not needed in game mode |
| jetty, hsqldb | Java backend | Not needed in game mode |
| snmpd, vsftpd | Monitoring/FTP | Not needed |
| nullmailer | Email sending | Not needed |
| ntpd | Time synchronization | Not critical for games |
| startautoupgrade | Auto-upgrade | Not needed |
| psplash | Boot splash screen | Uses 6MB RAM after boot |

### Services Kept Running

| Service | Purpose | Why Keep |
|---------|---------|----------|
| watchdog | Hardware watchdog feeder | Critical |
| sshd | SSH access | Remote access |
| cron, dbus, ntpd | System services | Essential |
| audio-enable | Speaker amplifier | Required for sound |
| roomwizard-games | Game selector | Main application |

### Memory Impact

**Before:** 106 MB used / 128 MB free (45% used)  
**After:** 27 MB used / 207 MB free (12% used)  
**Freed:** 80 MB RAM

### Disk Impact (with file removal)

**Before:** 405 MB used / 474 MB free (47% used)  
**After:** 227 MB used / 652 MB free (26% used)  
**Freed:** 178 MB disk space

### Verification

```bash
ssh root@192.168.50.73

# Check watchdog (should be disabled)
grep -E '^(test-binary|repair-binary)' /etc/watchdog.conf

# Check processes (should be none)
ps aux | grep -E 'java|Xorg|browser|jetty'

# Check memory
free -h
```

### Reverting Changes

```bash
ssh root@192.168.50.73

# Restore watchdog
cp /etc/watchdog.conf.backup /etc/watchdog.conf
/etc/init.d/watchdog restart

# Re-enable browser mode
update-rc.d browser defaults
update-rc.d x11 defaults
update-rc.d webserver defaults

reboot
```

**Note:** File removal cannot be reverted without SD card backup.

---

## Permanent Game Mode (boot)

Use the deploy script:
```bash
./build-and-deploy.sh 192.168.50.73 permanent
```

Or manually: `ssh root@<ip> '/etc/init.d/roomwizard-games start|stop|status'`

## Game Selector Markers

Two non-executable marker files in `/opt/games/` control how `game_selector` handles each binary. Because they lack execute permission (`chmod 644`) they are never listed themselves.

| Marker | Effect |
|---|---|
| `<name>.noargs` | Launch without device-path args (for apps that open devices themselves, e.g. ScummVM) |
| `<name>.hidden` | Hide from the games list entirely |

```bash
# Hide:    touch /opt/games/<name>.hidden  && chmod 644 /opt/games/<name>.hidden
# Un-hide: rm /opt/games/<name>.hidden
# No-args: touch /opt/games/<name>.noargs  && chmod 644 /opt/games/<name>.noargs
```

Current state on device:
- **Hidden:** `watchdog_feeder`, `touch_test`, `touch_debug`, `touch_inject`, `touch_calibrate`, `unified_calibrate`, `pressure_test`
- **No-args:** `scummvm`
- **Visible:** `snake`, `tetris`, `pong`, `hardware_test`, `scummvm`

## Resources

- **Device / SSH setup:** [SYSTEM_SETUP.md](../SYSTEM_SETUP.md)
- **Hardware specs:** [SYSTEM_ANALYSIS.md](../SYSTEM_ANALYSIS.md)
- **ScummVM backend:** [scummvm-roomwizard/SCUMMVM_DEV.md](../scummvm-roomwizard/SCUMMVM_DEV.md)
