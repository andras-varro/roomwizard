# Native Apps for RoomWizard

Native C apps and tools for the Steelcase RoomWizard (600MHz ARMv7, 800×480 framebuffer). Direct framebuffer rendering — no browser, no overhead.

See [CLAUDE.md](CLAUDE.md) for how to write code here, and [../IMPROVEMENT_PLAN.md](../IMPROVEMENT_PLAN.md) for open work.

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
| `device_tools` | Tool | **Unified hardware app** — the one you want. Tabs: Settings, Diagnostics, Tests, Display, USB |
| `audio_touch_test` | Toy | "Tap-a-Theremin" — touch-controlled tone generator |
| `hardware_test` | Tool | GUI diagnostics (hidden from the launcher; run over SSH) |
| `hardware_config` | Tool | Settings GUI — superseded by `device_tools` (hidden) |
| `hardware_diag` | Tool | System diagnostics GUI — superseded by `device_tools` (hidden) |
| `backlight` | Tool | CLI backlight control (hidden) |
| `touch_raw` | Tool | Digitizer reach: no calibration, no bezel — live crosshair + interior-only fit (hidden) |
| `touch_trace` | Tool | Live finger trail against the *calibrated* mapping (hidden) |
| `touch_inject` | Tool | Injects synthetic evdev touch events, `touch_inject <raw_x> <raw_y> [ms]` (hidden) |

Tools marked *hidden* have a `.hidden` marker in `/opt/games/`, so they do not appear in the
launcher grid but remain runnable over SSH. `usb_test` and `watchdog_feeder` exist as source but
are **not built or deployed** — USB testing lives in the `device_tools` USB tab, and the system
`/usr/sbin/watchdog` daemon handles the hardware watchdog.

The three touch tools need the framebuffer at 32 bpp; `touch_raw` asserts that itself, `touch_trace`
does not — run `fbset -depth 32` first if ScummVM or `vnc_client` left it at 16. Stop the launcher
before running either (`/etc/init.d/roomwizard-app stop`), and start it again afterwards.

### Device Tools

[`device_tools/device_tools.c`](device_tools/device_tools.c) consolidates five previously
separate GUI utilities behind a tab bar:

| Tab | Replaces | What it does |
|---|---|---|
| **Settings** | `hardware_config` | Audio on/off, LED on/off + brightness, save/reset, and the SYSTEM shutdown/reboot pair. Test buttons deliberately bypass config to exercise raw hardware. |
| **Diagnostics** | `hardware_diag` | Read-only system info across 6 pages (System, Memory, Storage, Hardware, Config, Network). |
| **Tests** | `hardware_test_gui` | 10 interactive hardware tests (LED ramp, backlight, pulse, blink, colour cycle, touch-zone grid, display diagnostics, audio sweep). Each takes over the full screen. |
| **Display** | `unified_calibrate` | Everything about the screen: backlight, portrait toggle, and the calibration wizard that writes both lines of `/etc/touch_calibration.conf`. See below. |
| **USB** | `usb_test` | Keyboard, mouse and gamepad visualisation for attached USB devices. |

#### The Display tab and the calibration wizard

Backlight and portrait mode live here rather than under Settings because they are screen
properties, and because calibration refuses to run in portrait — the toggle that disables it
should be visible from the same screen.

`CALIBRATE TOUCH` runs a five-step wizard that writes **both** lines of
`/etc/touch_calibration.conf`. Every step runs with the bezel zeroed on the full 800×480 panel,
so a drawn pixel is a panel pixel:

| Step | What you do |
|---|---|
| `TAP` | Tap 11 targets, 3 times each. They sit well inside the edges on purpose. |
| `CHECK` | Read the fitted range, the reach, a **per-axis** verdict and the edge-probe residuals. `ACCEPT` / `REDO` / `RESET`. |
| `EDGES` | Raise each margin until its yellow line clears the plastic, against a numbered 2 px ladder. |
| `REPORT` | Visible rectangle vs touchable rectangle, with the per-edge gap spelled out. |
| `CONFIRM` | The new mapping goes live for 20 s. Press `KEEP THESE` or it reverts on its own. |

`SCREEN EDGES` jumps straight to `EDGES` for a margins-only tweak. `RESET` puts both lines back to
the hardware `EVIOCGABS` range and the default margins — the escape hatch if a calibration ever
leaves the screen hard to press.

**Nothing is written until `CONFIRM`,** the previous file is copied to `.bakN` first, and every
button before that step is hit-tested through the *old* calibration, so a bad fit can never leave
you unable to press the button that rejects it.

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

To rebuild a single app, run `./build-and-deploy.sh` — it is fast and always links the
correct object set. Hand-rolled single-file compile lines go stale as `common/` grows
and will fail to link.

---

## System Optimization

The vendor firmware ships a software watchdog that reboots the device roughly every 70 minutes
in game mode, plus ~178 MB of bloatware (Jetty, OpenJRE, HSQLDB, X11, CJK fonts) and a further
~560 MB that can be reclaimed on top of that.

None of this is handled here — it is owned by `../setup-device.sh`:

```bash
../setup-device.sh <ip>                        # disable the SW watchdog + services
../setup-device.sh <ip> --remove               # + delete vendor bloatware (~178 MB)
../setup-device.sh <ip> --deep-clean           # + extended cleanup (~560 MB more)
../setup-device.sh <ip> --deep-clean --dry-run # preview, deletes nothing
../setup-device.sh <ip> --status               # report current state
```

Which services are disabled and why it is safe is documented in
[`../SYSTEM_ANALYSIS.md#52-as-we-run-it--game-mode`](../SYSTEM_ANALYSIS.md#52-as-we-run-it--game-mode).

---

## Permanent App Mode (boot)

```bash
./build-and-deploy.sh 192.168.50.73 set-default
```

This writes `/opt/roomwizard/default-app`; the init service respawns whatever it points at.
Installing the service itself is done once by `../setup-device.sh`.

Or manually: `ssh root@<ip> '/etc/init.d/roomwizard-app start|stop|status'`

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
- **Hidden:** `touch_inject`, `touch_raw`, `touch_trace`, `backlight`, `hardware_test`, `hardware_config`, `hardware_diag`
- **No-args:** `scummvm`
- **Visible:** `snake`, `tetris`, `pong`, `hardware_test`, `usb_test`, `scummvm`

## Resources

- **Device / SSH setup:** [COMMISSIONING.md](../COMMISSIONING.md)
- **Hardware specs:** [SYSTEM_ANALYSIS.md](../SYSTEM_ANALYSIS.md)
- **ScummVM backend:** [scummvm-roomwizard/SCUMMVM_DEV.md](../scummvm-roomwizard/SCUMMVM_DEV.md)
