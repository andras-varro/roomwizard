# ScummVM RoomWizard Backend

Custom ScummVM backend for RoomWizard embedded device enabling classic point-and-click adventure games.

## Quick Start

### Build
```bash
cd ../scummvm
./configure --host=arm-linux-gnueabihf --backend=roomwizard \
  --disable-all-engines --enable-engine=scumm --enable-engine=scumm-7-8 --enable-engine=he \
  --disable-alsa --disable-mt32emu --disable-flac --disable-mad --disable-vorbis \
  --enable-release --enable-optimizations
make -j4 LDFLAGS='-static'
arm-linux-gnueabihf-strip scummvm
```

### Deploy
```bash
scp scummvm root@192.168.50.73:/opt/games/
ssh root@192.168.50.73 'chmod +x /opt/games/scummvm'
```

### Run
```bash
ssh root@192.168.50.73
cd /opt/games
./scummvm
```

## Status

✅ **WORKING** - Backend functional, cursor movement working  
🔧 **IN PROGRESS** - Button click events need fixing

## Documentation

- **[`SCUMMVM_DEV.md`](SCUMMVM_DEV.md)** - Main development guide (start here)
- **[`manage-scummvm-changes.sh`](manage-scummvm-changes.sh)** - Git management script

## Backend Files

Located in `../scummvm/backends/platform/roomwizard/`:
- `roomwizard.cpp/h` - Main backend
- `roomwizard-graphics.cpp/h` - Graphics manager (framebuffer)
- `roomwizard-events.cpp/h` - Event source (touch input)
- `module.mk` - Build configuration

## Git Management

```bash
./manage-scummvm-changes.sh status   # Check configuration
./manage-scummvm-changes.sh backup   # Create backup
./manage-scummvm-changes.sh update   # Update from upstream
```

## Hardware

For comprehensive hardware documentation, see [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#hardware-platform).

- **Display:** 800x480 framebuffer
- **Input:** Single-touch resistive touchscreen
- **CPU:** ARMv7 @ 300MHz
- **RAM:** 256MB total (184MB available)

## Current Issues

1. Button clicks not working (LBUTTONDOWN/UP not generated)
2. Touch drag not working (MOUSEMOVE during drag)
3. Touch calibration needed (~14-27px corner error)

See [`SCUMMVM_DEV.md`](SCUMMVM_DEV.md) for details.
