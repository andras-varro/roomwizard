# ScummVM RoomWizard Backend

Custom ScummVM backend for the RoomWizard embedded device enabling classic point-and-click adventure games.

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

## Backend Files

Located in `../scummvm/backends/platform/roomwizard/`:
- `roomwizard.cpp/h` - Main backend
- `roomwizard-graphics.cpp/h` - Graphics manager (framebuffer)
- `roomwizard-events.cpp/h` - Event source (touch input)
- `module.mk` - Build configuration

## Git Management

ScummVM is an external repo. Use `manage-scummvm-changes.sh` to manage local modifications:

```bash
./manage-scummvm-changes.sh status   # Check configuration
./manage-scummvm-changes.sh backup   # Create backup
./manage-scummvm-changes.sh update   # Update from upstream
```

## Documentation

- [`PROJECT.md`](PROJECT.md) - Full project tracker
- [`BACKEND_DESIGN.md`](BACKEND_DESIGN.md) - Architecture design
- [`IMPLEMENTATION.md`](IMPLEMENTATION.md) - Implementation details
- [`DEBUG_LOG.md`](DEBUG_LOG.md) - Debug notes
- [`CHANGES_MANAGEMENT.md`](CHANGES_MANAGEMENT.md) - Git workflow

## Hardware

- **Display:** 800x480 framebuffer
- **Input:** Single-touch resistive touchscreen
- **CPU:** ARMv7 @ 300MHz
- **RAM:** 234MB (184MB available)
