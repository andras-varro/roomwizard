# RoomWizard Backend for ScummVM

This is a custom ScummVM backend for the RoomWizard device, implementing native framebuffer graphics and touch input support.

## Features

- **Native Framebuffer Rendering**: Direct rendering to `/dev/fb0` (800x480 @ 32-bit ARGB)
- **Touch Input Support**: Single-touch resistive touchscreen via `/dev/input/event0`
- **Automatic Scaling**: Games are centered and scaled to fit the display
- **Software Cursor**: Rendered cursor with palette support
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
  --disable-alsa \
  --disable-mt32emu \
  --disable-flac \
  --disable-mad \
  --disable-vorbis \
  --enable-release \
  --enable-optimizations \
  --disable-all-engines \
  --enable-engine=scumm \
  --enable-engine=scumm-7-8 \
  --enable-engine=he

make
```

## Supported Pixel Formats

- **CLUT8**: 8-bit indexed color (with palette)
- **RGB565**: 16-bit color
- **ARGB8888**: 32-bit color with alpha

## Input Mapping

- **Touch**: Left mouse button
- **Long Press (500ms)**: Right mouse button
- **Drag**: Mouse movement

## Limitations

- **No Audio**: Audio subsystem is disabled (NullMixerManager)
- **Single-Touch Only**: No multi-touch gestures
- **No Keyboard**: Touch input only
- **Software Rendering**: No hardware acceleration

## Dependencies

The backend links against the RoomWizard native libraries:

- `native_games/common/framebuffer.c` - Framebuffer management
- `native_games/common/touch_input.c` - Touch input handling

## Architecture

```
ScummVM Core
    ↓
OSystem_RoomWizard (backends/platform/roomwizard/roomwizard.cpp)
    ├── RoomWizardGraphicsManager (roomwizard-graphics.cpp)
    │   └── framebuffer.c (native library)
    ├── RoomWizardEventSource (roomwizard-events.cpp)
    │   └── touch_input.c (native library)
    ├── DefaultTimerManager
    ├── DefaultEventManager
    ├── DefaultSaveFileManager
    └── NullMixerManager
```

## Testing

Tested with:
- LSL 5
- KQ 1

## Author

Created for the RoomWizard embedded device project.

## License

GPL v3+ (same as ScummVM)
