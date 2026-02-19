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
  --disable-alsa --disable-mt32emu --disable-flac --disable-mad --disable-vorbis \
  --enable-release --enable-optimizations --enable-vkeybd
make -j4 LDFLAGS='-static'
arm-linux-gnueabihf-strip scummvm
```

### Deploy
```bash
scp scummvm root@192.168.50.73:/opt/games/
ssh root@192.168.50.73 'chmod +x /opt/games/scummvm'
```

ScummVM appears in the native game selector automatically. The `scummvm.noargs` marker (already on device) ensures it launches without device-path arguments.

### Touch & Bezel Calibration
Run once on the device:
```bash
ssh root@192.168.50.73 '/opt/games/unified_calibrate'
```
- **Phase 1:** Tap the 4 corner crosshairs (touch offset)
- **Phase 2:** Tap `+`/`−` zones (bezel margins)
- Saves to `/etc/touch_calibration.conf` — loaded automatically

## Status

✅ Fully functional — see [SCUMMVM_DEV.md](SCUMMVM_DEV.md) for details

## Backend Files

Version-controlled in [`backend-files/`](backend-files/), synced to/from `../scummvm/backends/platform/roomwizard/`:

| File | Purpose |
|---|---|
| `roomwizard.cpp/h` | Main backend, VKB, feature flags |
| `roomwizard-graphics.cpp/h` | 800×480 framebuffer, bezel-aware scaling, overlay compositing |
| `roomwizard-events.cpp/h` | Touch input, state machine, corner gestures |
| `module.mk` | Build configuration |
| `configure.patch` | ScummVM configure modifications |

Sync after editing:
```powershell
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard-events.cpp  .\backend-files\ -Force
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard-graphics.cpp .\backend-files\ -Force
Copy-Item ..\scummvm\backends\platform\roomwizard\roomwizard.cpp         .\backend-files\ -Force
```

## Git Management

```bash
./manage-scummvm-changes.sh sync     # Sync changes TO version control
./manage-scummvm-changes.sh restore  # Restore changes FROM version control
./manage-scummvm-changes.sh status   # Check configuration
```
