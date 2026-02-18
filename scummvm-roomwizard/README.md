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

### Touch & Bezel Calibration
Run once on the device (needs `unified_calibrate` from `native_games/build/`):
```bash
ssh root@192.168.50.73 '/opt/games/unified_calibrate'
```
- **Phase 1:** Tap the 4 corner crosshairs to correct touch accuracy
- **Phase 2:** Tap `+`/`-` zones to adjust bezel margins until the on-screen rectangle matches the visible area
- Saves to `/etc/touch_calibration.conf` — ScummVM loads it automatically on next launch

### Run
```bash
ssh root@192.168.50.73
cd /opt/games
./scummvm
```

## Status

✅ **FULLY FUNCTIONAL** - Backend complete, verified on device  
✅ **FULL-SCREEN SCALING** - Game upscaled to fill display, aspect-ratio-preserving  
✅ **BEZEL COMPENSATION** - Loads `/etc/touch_calibration.conf`; scales to bezel-safe area  
✅ **COORDINATE MAPPING** - Touch coords reverse-mapped through scaled game region  

## Documentation

- **[`SCUMMVM_DEV.md`](SCUMMVM_DEV.md)** - Main development guide (start here)
- **[`manage-scummvm-changes.sh`](manage-scummvm-changes.sh)** - Git management script

## Backend Files

**Version-controlled** in [`backend-files/`](backend-files/):
- `roomwizard.cpp/h` - Main backend
- `roomwizard-graphics.cpp/h` - Graphics manager (framebuffer)
- `roomwizard-events.cpp/h` - Event source (touch input)
- `module.mk` - Build configuration
- `configure.patch` - ScummVM configure modifications
- `DEBUG_LOG.md` - Implementation debug notes
- `IMPLEMENTATION_SUCCESS.md` - Implementation documentation

These files are synced to/from `../scummvm/backends/platform/roomwizard/` using the management script.

### Why Version Control?

Backend files are now tracked individually (not tar.gz) for:
- ✅ File-by-file git history and diffs
- ✅ Easy code review and change tracking
- ✅ Better merge conflict resolution
- ✅ Simplified porting to new ScummVM versions

## Git Management

**Common Commands:**
```bash
./manage-scummvm-changes.sh sync     # Sync changes TO version control
./manage-scummvm-changes.sh restore  # Restore changes FROM version control
./manage-scummvm-changes.sh update   # Update from upstream (auto-syncs)
./manage-scummvm-changes.sh status   # Check configuration
```

**Typical Workflow:**

After making changes in ScummVM:
```bash
cd scummvm-roomwizard
./manage-scummvm-changes.sh sync
git add backend-files/
git commit -m "Update backend implementation"
```

After fresh clone:
```bash
cd scummvm-roomwizard
./manage-scummvm-changes.sh restore  # Copies backend-files/ to scummvm/
```

Updating ScummVM from upstream:
```bash
./manage-scummvm-changes.sh update  # Auto-syncs before and after
git add backend-files/
git commit -m "Sync after upstream update"
```

## Hardware

For comprehensive hardware documentation, see [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#hardware-platform).

- **Display:** 800x480 framebuffer
- **Input:** Single-touch resistive touchscreen
- **CPU:** ARMv7 @ 300MHz
- **RAM:** 256MB total (184MB available)

See [`SCUMMVM_DEV.md`](SCUMMVM_DEV.md) for complete development documentation.
