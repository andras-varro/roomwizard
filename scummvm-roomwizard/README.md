# ScummVM RoomWizard Backend

Custom ScummVM backend for the Steelcase RoomWizard embedded device. Runs classic point-and-click adventure games on 800×480 framebuffer with touch input.

## Quick Start

### Build
```bash
cd ../scummvm
./configure --host=arm-linux-gnueabihf --backend=roomwizard \
  --disable-all-engines \
  --enable-engine=scumm --enable-engine=scumm-7-8 --enable-engine=he \
  --enable-engine=agi --enable-engine=sci --enable-engine=agos \
  --enable-engine=sky --enable-engine=queen \
  --disable-mt32emu --disable-flac --disable-mad --disable-vorbis \
  --enable-release --enable-optimizations --enable-vkeybd
make -j4 LDFLAGS='-static' LIBS='-lpthread -lm'
arm-linux-gnueabihf-strip scummvm
```

> **Note:** `build-and-deploy.sh` auto-restores backend files from `backend-files/` into the ScummVM build tree before each build, so manual `manage-scummvm-changes.sh restore` is no longer needed.

### Deploy
```bash
scp scummvm root@192.168.50.73:/opt/games/
ssh root@192.168.50.73 'chmod +x /opt/games/scummvm'
```

ScummVM appears in the native game selector automatically. The `scummvm.noargs` marker (already on device) ensures it launches without device-path arguments.

### Touch Calibration

Run once, on the device screen: **Device Tools → Display → `CALIBRATE TOUCH`**.
(The standalone `/opt/games/unified_calibrate` binary was folded into that wizard and removed.)

- Tap 11 targets, 3 times each; a per-axis least-squares fit over the **interior** targets maps
  raw → panel, and a check screen shows the reach and per-axis verdict before anything is applied
- Writes both lines of `/etc/touch_calibration.conf` — loaded automatically by ScummVM and all
  native apps
- **Redeploy ScummVM after any change to `native_apps/common/touch_input.c`**: it links its own
  copy, so a deployed binary keeps whatever it was built with

### Screen area

Once the panel's touch reach has been measured (the wizard's `REACH` step), ScummVM keeps both the
game picture and its own GUI inside the part of the screen a finger can actually reach — the
digitizer saturates a little before the panel edge, so a band at each end is visible but not
pressable, and a game's verb bar or the launcher's button row must not land there.

The GUI (launcher, global menu, virtual keyboard) always stays inside that rectangle. The **game
picture** can be given the whole screen back if you prefer the extra size to the reachability:

```bash
ROOMWIZARD_CONTENT_AREA=visible /opt/games/scummvm     # one run
```

To make it permanent, add `rw_content_area = visible` under `[scummvm]` in the config file. **Note
that ScummVM resolves `scummvm.ini` relative to its working directory on this device**, so the copy
the boot launcher uses is `/scummvm.ini` — an SSH shell in `$HOME` writes `/home/root/scummvm.ini`
instead, and editing the wrong one looks like the setting being ignored. The environment variable
takes precedence over both. Before the panel has been swept, `safe` and `visible` are identical.

## Status

✅ Fully functional — GUI, touch, keyboard, mouse, gamepad, audio (OPL/AdLib music + SFX), virtual keyboard all working. All input verified with EcoQuest and Full Throttle.
See [SCUMMVM_DEV.md](SCUMMVM_DEV.md) for architecture, audio design, and optimization history.

## Input Devices

All four input methods work **simultaneously** — users can freely mix touch, keyboard, mouse, and gamepad at any time. Input handling is implemented in [`roomwizard-events.cpp`](backend-files/roomwizard-events.cpp) / [`roomwizard-events.h`](backend-files/roomwizard-events.h).

### Touch

Existing behaviour preserved:
- Tap = left click, long-press = right click
- Corner gestures for ScummVM shortcuts
- Bezel-aware coordinate mapping (uses `/etc/touch_calibration.conf`)

### USB Keyboard

Full key mapping for adventure game interaction:

| Key | ScummVM Action |
|-----|----------------|
| F5 | Open save/load dialog |
| Ctrl+F5 | Global Main Menu (GMM) |
| Space | Skip cutscene |
| Escape | Skip dialog / cancel |
| Enter | Confirm selection |
| Arrow keys | Navigate menus / move cursor |
| Letters/Numbers | Type save game names, in-game input |

All standard keys are forwarded — letters, numbers, F-keys, arrows, modifiers, and punctuation.

### USB Mouse

Direct cursor control with 3-tier acceleration (same as native apps):
- Left click = left click, right click = right click, middle click = middle click
- Precise point-and-click gameplay — ideal for adventure games
- Mouse sensitivity configurable via `/etc/input_config.conf`

### USB Gamepad

D-pad and analog stick control the on-screen cursor. Full button mapping:

| Gamepad Button | ScummVM Action |
|----------------|----------------|
| A (South) | Left click |
| B (East) | Right click |
| X (West) | Skip cutscene (Space) |
| Y (North) | Escape / cancel |
| Start | Global Main Menu (Ctrl+F5) |
| Select | Save/load dialog (F5) |
| L Bumper | Skip dialog text |
| R Bumper | Skip dialog text |
| D-pad / Left Stick | Move cursor |

### Configuration

Mouse sensitivity and gamepad button mapping are configurable via `/etc/input_config.conf` (shared with native apps — see [native_apps/README.md](../native_apps/README.md#input-configuration)).

### Auto-Detection

Devices are auto-detected by scanning `/dev/input/event*` every 5 seconds. USB hotplug is fully supported — plug in a controller mid-game and it works immediately.

## Backend Files

Version-controlled in [`backend-files/`](backend-files/), synced to/from `../scummvm/backends/platform/roomwizard/`:

| File | Purpose |
|---|---|
| `roomwizard.cpp/h` | Main backend, VKB, feature flags |
| `roomwizard-graphics.cpp/h` | 800×480 framebuffer, bezel-aware scaling, overlay compositing |
| `roomwizard-events.cpp/h` | Touch + keyboard + mouse + gamepad input, state machine, corner gestures |
| `oss-mixer.cpp/h` | OSS audio mixer (TWL4030 via `/dev/dsp`, O_NONBLOCK) |
| `module.mk` | Build configuration |
| `configure.patch` | ScummVM configure modifications |

Sync after editing:
```bash
bash manage-scummvm-changes.sh sync     # scummvm/ edits → backend-files/
bash manage-scummvm-changes.sh restore  # backend-files/ → scummvm/
bash manage-scummvm-changes.sh status   # check configuration
```
