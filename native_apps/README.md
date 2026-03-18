# Native Apps for RoomWizard

Native C apps and tools for the Steelcase RoomWizard (600MHz ARMv7, 800×480 framebuffer). Direct framebuffer rendering — no browser, no overhead.

See [PROJECT.md](PROJECT.md) for architecture and development status.

## Table of Contents

1. [Apps & Tools](#apps--tools)
2. [Input Support](#input-support)
3. [Build & Deploy](#build--deploy-cross-compile-from-wsl)
4. [App Launcher](#app-launcher)
5. [App Manifests](#app-manifests)
6. [System Optimization](#system-optimization)
7. [Permanent App Mode](#permanent-app-mode-boot)
8. [Resources](#resources)

---

## Apps & Tools

| Binary | Type | Controls / Notes |
|---|---|---|
| `snake` | Game | Tap / arrow keys / D-pad to steer |
| `frogger` | Game | Tap / arrow keys / D-pad to hop |
| `tetris` | Game | Tap / keys / D-pad to move, rotate, drop (DAS auto-repeat) |
| `pong` | Game | Touch-drag / keys / analog stick for paddle |
| `brick_breaker` | Game | Touch / mouse / keys / analog stick for paddle |
| `samegame` | Game | Touch / mouse cursor + keyboard navigation |
| `platformer` | Game | Touch / keys / gamepad — reference input implementation |
| `app_launcher` | Launcher | Visual grid launcher — keyboard/mouse/gamepad nav, auto-starts on boot |
| `game_selector` | Launcher | D-pad grid nav + Enter/A select, mouse click, legacy text menu |
| `hardware_test` | Tool | Diagnostics |
| `usb_test` | Tool | USB device tester — keyboard, mouse, gamepad visualization |
| `watchdog_feeder` | Daemon | Feeds `/dev/watchdog` every 30 s |
| `unified_calibrate` | Tool | Touch + bezel calibration |

---

## Input Support

All native apps share a unified input system built on [`gamepad.h`](common/gamepad.h) / [`gamepad.c`](common/gamepad.c). This library provides a single polling API that unifies four input sources — **touch**, **USB keyboard**, **USB mouse**, and **USB gamepad** — into a common abstract button model.

### Abstract Button Model

Every input device maps to the same set of abstract buttons, allowing games to be written against a single API:

| Button ID | Purpose |
|-----------|---------|
| `BTN_ID_UP` | Direction up / navigate up |
| `BTN_ID_DOWN` | Direction down / navigate down |
| `BTN_ID_LEFT` | Direction left / navigate left |
| `BTN_ID_RIGHT` | Direction right / navigate right |
| `BTN_ID_JUMP` | Primary action (jump / select) |
| `BTN_ID_RUN` | Secondary action (run / sprint) |
| `BTN_ID_ACTION` | Tertiary action (interact / confirm) |
| `BTN_ID_PAUSE` | Pause game |
| `BTN_ID_BACK` | Back / exit to launcher |

### Common Controls

| Action | Keyboard | Gamepad | Purpose |
|--------|----------|---------|---------|
| Up | Arrow Up / W | D-pad Up / Left Stick | Direction / Navigate |
| Down | Arrow Down / S | D-pad Down / Left Stick | Direction / Navigate |
| Left | Arrow Left / A | D-pad Left / Left Stick | Direction / Navigate |
| Right | Arrow Right / D | D-pad Right / Left Stick | Direction / Navigate |
| Jump/Select | Space | A (South) | Primary action |
| Run | Shift | B (East) | Secondary action |
| Action | Enter | X (West) | Tertiary action |
| Pause | Escape | Start | Pause game |
| Back/Exit | Backspace | Select | Exit to launcher |

### Mouse Support

USB mice provide direct cursor control with **3-tier acceleration**:

- **Slow** (< 3 px movement) — 1:1 pixel mapping for precision
- **Medium** (3–10 px) — 2× multiplier
- **Fast** (> 10 px) — 4× multiplier

Mouse sensitivity is configurable via `/etc/input_config.conf`.

### Gamepad Button Mapping

Gamepad button mapping is configurable to support clone/third-party controllers that may report different button codes than standard Xbox/PlayStation layouts. Remap buttons in `/etc/input_config.conf` (see [Configuration](#input-configuration) below).

### Per-App Input Matrix

| App | Touch | Keyboard | Mouse | Gamepad | Notes |
|-----|:-----:|:--------:|:-----:|:-------:|-------|
| Snake | ✅ | ✅ | — | ✅ | Arrow keys / D-pad for direction |
| Frogger | ✅ | ✅ | — | ✅ | Arrow keys / D-pad for hopping |
| Tetris | ✅ | ✅ | — | ✅ | DAS auto-repeat, hard drop, rotate |
| Pong | ✅ | ✅ | — | ✅ | Analog stick proportional paddle |
| Brick Breaker | ✅ | ✅ | ✅ | ✅ | Mouse/analog for paddle, full control |
| SameGame | ✅ | ✅ | ✅ | ✅ | Mouse cursor + hover highlight |
| Platformer | ✅ | ✅ | — | ✅ | Reference implementation |
| App Launcher | ✅ | ✅ | ✅ | ✅ | Grid nav + Enter/A select, 500ms post-launch cooldown |
| Game Selector | ✅ | ✅ | ✅ | ✅ | D-pad grid nav + Enter/A select + mouse click, 500ms cooldown |
| USB Test | ✅ | ✅ | ✅ | ✅ | Device diagnostic visualizer |

### USB Hotplug

All apps scan `/dev/input/event*` for newly connected USB devices **every 5 seconds**. Plugging in a keyboard, mouse, or gamepad while an app is running will be detected and usable within seconds — no restart needed.

### Input Configuration

All input settings are stored in `/etc/input_config.conf`, shared across all native apps and the ScummVM backend.

```ini
# /etc/input_config.conf — Unified input configuration
# All values are optional; defaults are used if omitted.

# Mouse sensitivity multiplier (float, default: 1.0)
# Higher = faster cursor movement
mouse_sensitivity=1.0

# Mouse acceleration enable (0 = off, 1 = on, default: 1)
mouse_acceleration=1

# Gamepad analog stick dead zone (0–32767, default: 8000)
# Movements below this threshold are ignored
gamepad_deadzone=8000

# Gamepad button remapping for clone controllers
# Format: gamepad_btn_<abstract>=<linux_evdev_code>
# Use evtest on device to find button codes for your controller
gamepad_btn_a=304
gamepad_btn_b=305
gamepad_btn_x=307
gamepad_btn_y=308
gamepad_btn_start=315
gamepad_btn_select=314
gamepad_btn_l1=310
gamepad_btn_r1=311

# Analog stick axis mapping
# Default: ABS_X=0, ABS_Y=1 (left stick)
gamepad_axis_x=0
gamepad_axis_y=1
```

---

## Build & Deploy (cross-compile from WSL)

```bash
cd native_apps

# Build only
./build-and-deploy.sh

# Build + deploy binaries + manifests
./build-and-deploy.sh 192.168.50.53

# Build + deploy + set app launcher as default boot app
./build-and-deploy.sh 192.168.50.53 set-default
```

## App Launcher

The `app_launcher` provides a visual grid interface for launching apps:

- Scans `/opt/roomwizard/apps/*.app` manifest files
- Displays apps as coloured icon tiles in a 3×2 grid
- Supports PPM icons or auto-generated letter tiles
- Touch tile to launch, edge touch for pagination
- Re-scans manifests after each app exits (picks up new deployments)
- Respawns automatically via the init script if it crashes

## App Manifests

Each app is registered via a `.app` manifest in `/opt/roomwizard/apps/`:

```ini
name=Snake
exec=/opt/games/snake
icon=/opt/roomwizard/icons/snake.ppm
args=fb,touch
```

Fields:
- `name` — Display name (required)
- `exec` — Absolute path to executable (required)
- `icon` — Path to PPM P6 icon file (optional, auto letter-tile if absent)
- `args` — Argument mode: `fb,touch` (default), `fb`, `touch`, or `none`

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
- **Visible:** `snake`, `tetris`, `pong`, `hardware_test`, `usb_test`, `scummvm`

## Resources

- **Device / SSH setup:** [COMMISSIONING.md](../COMMISSIONING.md)
- **Hardware specs:** [SYSTEM_ANALYSIS.md](../SYSTEM_ANALYSIS.md)
- **ScummVM backend:** [scummvm-roomwizard/SCUMMVM_DEV.md](../scummvm-roomwizard/SCUMMVM_DEV.md)
