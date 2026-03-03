# RoomWizard Backend for ScummVM

This is a custom ScummVM backend for the RoomWizard device, implementing native framebuffer graphics and touch input support.

## Features

- **Native Framebuffer Rendering**: Direct rendering to `/dev/fb0` (800x480 @ 32-bit ARGB)
- **Touch Input Support**: Single-touch resistive touchscreen via `/dev/input/event0`
- **Automatic Scaling**: Games are centered and scaled to fit the display, with bezel-aware margins
- **Software Cursor**: Rendered cursor with palette support
- **Audio via OSS**: OPL/AdLib music and SFX through TWL4030 speaker (`/dev/dsp`)
- **Virtual Keyboard**: On-screen keyboard via triple-tap gesture
- **POSIX Filesystem**: Standard save/load functionality

## Hardware Requirements

- **Display**: 800x480 framebuffer (RGB/ARGB)
- **Input**: Resistive touchscreen (single-touch)
- **CPU**: ARMv7 with NEON SIMD
- **RAM**: 128+ MB available
- **OS**: Linux with framebuffer and input event support

## Building

Configure ScummVM with the roomwizard backend:

```bash
./configure \
  --host=arm-linux-gnueabihf \
  --backend=roomwizard \
  --disable-all-engines \
  --enable-engine=scumm \
  --enable-engine=scumm-7-8 \
  --enable-engine=he \
  --enable-engine=agi \
  --enable-engine=sci \
  --enable-engine=agos \
  --enable-engine=sky \
  --enable-engine=queen \
  --disable-mt32emu \
  --disable-flac \
  --disable-mad \
  --disable-vorbis \
  --enable-release \
  --enable-optimizations \
  --enable-vkeybd

make -j4 LDFLAGS='-static' LIBS='-lpthread -lm'
```

## Supported Pixel Formats

- **CLUT8**: 8-bit indexed color (with palette)
- **RGB565**: 16-bit color
- **ARGB8888**: 32-bit color with alpha

## Input Mapping

- **Touch**: Left mouse button
- **Long Press (500ms)**: Right mouse button
- **Drag**: Mouse movement
- **Triple-tap bottom-right**: Global Main Menu (Ctrl+F5)
- **Triple-tap bottom-left**: Virtual Keyboard
- **Triple-tap top-right**: Enter key

## Limitations

- **Single-Touch Only**: No multi-touch gestures; right-click = long-press 500 ms
- **No MIDI**: NullMidiDriver (OPL emulation works for AdLib music)
- **Software Rendering**: No hardware acceleration
- **Volume**: Fixed 50% attenuation to prevent speaker distortion

## Dependencies

The backend links against the RoomWizard native libraries:

- `native_apps/common/framebuffer.c` - Framebuffer management
- `native_apps/common/touch_input.c` - Touch input handling

## Architecture

```
ScummVM Core
    ↓
OSystem_RoomWizard (backends/platform/roomwizard/roomwizard.cpp)
    ├── RoomWizardGraphicsManager (roomwizard-graphics.cpp)
    │   └── framebuffer.c (native library)
    ├── RoomWizardEventSource (roomwizard-events.cpp)
    │   └── touch_input.c (native library)
    ├── OssMixerManager (oss-mixer.cpp)
    │   └── /dev/dsp (TWL4030 via ALSA OSS shim)
    ├── DefaultTimerManager
    ├── DefaultEventManager
    └── DefaultSaveFileManager
```
```

## Testing

Tested with:
- Monkey Island 1 & 2
- Day of the Tentacle
- King's Quest 1–6 (AGI/SCI)
- Leisure Suit Larry 1–5
- Space Quest 1–5
- Simon the Sorcerer
- Beneath a Steel Sky
- Flight of the Amazon Queen
- Putt-Putt, Freddi Fish, Pajama Sam (HE)

## Author

Created for the RoomWizard embedded device project.

## License

GPL v3+ (same as ScummVM)
