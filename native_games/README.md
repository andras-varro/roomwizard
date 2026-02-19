# Native Games for RoomWizard

Native C games and tools for the Steelcase RoomWizard (300MHz ARMv7, 800×480 framebuffer). Direct framebuffer rendering — no browser, no overhead.

See [PROJECT.md](PROJECT.md) for architecture and development status.

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

## Build (cross-compile from WSL)

```bash
cd native_games
./compile_for_roomwizard.sh   # builds all to build/
```

Rebuilding just `game_selector` after changes:
```bash
arm-linux-gnueabihf-gcc -O2 -static -I. \
  common/framebuffer.c common/touch_input.c common/hardware.c \
  common/common.c common/ui_layout.c \
  game_selector/game_selector.c -o build/game_selector -lm
```

## Deploy

```bash
scp build/* root@192.168.50.73:/opt/games/
ssh root@192.168.50.73 'chmod +x /opt/games/game_selector /opt/games/snake /opt/games/tetris /opt/games/pong'
```

## Permanent Game Mode (boot)

Install the SysVinit script to replace the browser with the game selector on boot:

```bash
scp roomwizard-games-init.sh root@192.168.50.73:/etc/init.d/roomwizard-games
ssh root@192.168.50.73 'chmod +x /etc/init.d/roomwizard-games && update-rc.d browser remove && update-rc.d x11 remove && update-rc.d roomwizard-games defaults && reboot'
```

Manual: `/etc/init.d/roomwizard-games start|stop|status`

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
