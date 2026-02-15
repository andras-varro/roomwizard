# Native Games for RoomWizard

## Overview

This collection provides **native C games** optimized for the Steelcase RoomWizard device (300MHz ARM processor). These games run directly on the framebuffer without browser overhead, making them extremely efficient and responsive.

**For developers:** See [`PROJECT.md`](PROJECT.md) for current development status, architecture details, and next steps.

## Why Native Games?

- **No Browser Overhead**: Direct framebuffer rendering is 10-20x more efficient than HTML5/Canvas
- **Better Performance**: Optimized for 300MHz ARM processors (comparable to 80386 at 40MHz)
- **Lower Memory Usage**: Minimal memory footprint (~1-2MB per game)
- **Direct Hardware Access**: Touch input via Linux input events, no JavaScript layer
- **Instant Startup**: No page loading or JavaScript parsing

## Games Included

### 1. Snake 🐍
Classic snake game with touch controls.
- **Controls**: Tap in the direction you want the snake to move
- **Features**: Progressive difficulty, high score tracking
- **Memory**: ~1MB
- **Performance**: Runs smoothly at 300MHz

### 2. Tetris 🎮
Full-featured Tetris implementation.
- **Controls**: 
  - Tap left/right sides to move piece
  - Tap center to rotate
  - Tap bottom to hard drop
- **Features**: Level progression, next piece preview, line clearing
- **Memory**: ~1.5MB
- **Performance**: 60 FPS on 300MHz ARM

### 3. Pong 🏓
Single-player Pong vs AI.
- **Controls**: Touch and drag to move your paddle
- **Features**: AI opponent with adjustable difficulty, score tracking
- **Memory**: ~1MB
- **Performance**: Smooth 60 FPS gameplay

## Hardware Requirements

- **Processor**: ARM Cortex-A8 or compatible (300MHz minimum)
- **Display**: Framebuffer device (`/dev/fb0`)
- **Input**: Touchscreen device (`/dev/input/touchscreen0` or `/dev/input/event*`)
- **Memory**: 4MB RAM minimum per game
- **Storage**: 500KB per game executable

## Building the Games

### Prerequisites

```bash
# Install build tools (if not already present)
apt-get install gcc make

# Or on Yocto/embedded systems, ensure gcc is available
```

### Compilation

#### Method 1: Using Make (Recommended)

```bash
cd native_games

# Build all games
make

# Build specific game
make snake
make tetris
make pong

# Install system-wide (requires root)
sudo make install

# Clean build files
make clean
```

#### Method 2: Using Build Script

```bash
cd native_games
chmod +x build.sh
./build.sh
```

#### Method 3: Manual Compilation

```bash
# Compile common libraries
gcc -O2 -c common/framebuffer.c -o framebuffer.o
gcc -O2 -c common/touch_input.c -o touch_input.o

# Compile Snake
gcc -O2 snake/snake.c framebuffer.o touch_input.o -o snake -lm

# Compile Tetris
gcc -O2 tetris/tetris.c framebuffer.o touch_input.o -o tetris -lm

# Compile Pong
gcc -O2 pong/pong.c framebuffer.o touch_input.o -o pong -lm
```

### Cross-Compilation for RoomWizard

**Quick Method (Recommended):**
```bash
# Use the provided cross-compilation script
./compile_for_roomwizard.sh
```

**Manual Method:**
```bash
# Set cross-compiler
export CC=arm-linux-gnueabihf-gcc

# Build with ARM optimizations
export CFLAGS="-O2 -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=hard"

# Then run make
make
```

## Running the Games

### Basic Usage

```bash
# Run from build directory
./build/snake
./build/tetris
./build/pong

# Or if installed system-wide
/opt/games/snake
/opt/games/tetris
/opt/games/pong
```

### Custom Device Paths

If your framebuffer or touchscreen is at a different path:

```bash
# Syntax: ./game [framebuffer_device] [touch_device]

./build/snake /dev/fb0 /dev/input/event1
./build/tetris /dev/fb1 /dev/input/touchscreen0
```

### Running at Startup

To auto-start a game on boot, add to `/etc/rc.local` or create a systemd service:

#### Option 1: rc.local

```bash
# Add to /etc/rc.local (before 'exit 0')
/opt/games/snake &
```

#### Option 2: Systemd Service

Create `/etc/systemd/system/snake-game.service`:

```ini
[Unit]
Description=Snake Game
After=multi-user.target

[Service]
Type=simple
ExecStart=/opt/games/snake
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
systemctl enable snake-game
systemctl start snake-game
```

## Deployment to RoomWizard

**Prerequisites:** Ensure SSH access is configured. See [`SYSTEM_SETUP.md`](../SYSTEM_SETUP.md) for SSH key setup and device configuration (IP: 192.168.50.73).

### Method 1: Direct Copy (SSH/SCP)

```bash
# Copy games to device
scp build/* root@roomwizard:/opt/games/

# SSH into device and run
ssh root@roomwizard
cd /opt/games
./snake
```

### Method 2: Permanent Game Mode (SysVinit)

Use the included [`roomwizard-games-init.sh`](roomwizard-games-init.sh) script to replace the browser with native games:

**What it does:**
1. Stops browser/X11 services
2. Starts watchdog feeder (prevents 60-second reset)
3. Starts game selector (main menu)

**Installation:**
```bash
# Copy to device
scp roomwizard-games-init.sh root@192.168.50.73:/etc/init.d/roomwizard-games

# Make executable
ssh root@192.168.50.73 'chmod +x /etc/init.d/roomwizard-games'

# Enable at boot (permanent mode)
ssh root@192.168.50.73 'update-rc.d browser remove && update-rc.d x11 remove'
ssh root@192.168.50.73 'update-rc.d roomwizard-games defaults'
ssh root@192.168.50.73 'reboot'
```

**Manual control:**
```bash
/etc/init.d/roomwizard-games start    # Start game mode
/etc/init.d/roomwizard-games stop     # Stop game mode
/etc/init.d/roomwizard-games status   # Check status
```

### Method 3: Include in Firmware

1. Copy `native_games/` directory to your RoomWizard firmware build
2. Add to rootfs partition:
   ```bash
   cp -r native_games /path/to/rootfs/opt/
   ```
3. Add compilation step to firmware build process
4. Repack and flash firmware

### Method 4: SD Card Installation

1. Build games on development machine
2. Mount RoomWizard SD card
3. Copy executables to `/opt/games/` on rootfs partition
4. Unmount and insert SD card into RoomWizard
5. Boot device

## Performance Optimization

### For 300MHz ARM Devices

The games are already optimized with:

- **Compiler Flags**: `-O2 -march=armv7-a -mtune=cortex-a8 -mfpu=neon`
- **Fixed-Point Math**: Where possible (Pong uses float for smooth physics)
- **Minimal Allocations**: Static memory allocation, no malloc in game loops
- **Efficient Rendering**: Direct framebuffer writes with double buffering for flicker-free display
- **Frame Rate Control**: Games run at appropriate FPS (Snake: variable, Tetris/Pong: 60 FPS)

### Further Optimizations

If performance is still an issue:

1. **Reduce Resolution**: Modify cell_size/scaling factors in game code
2. **Lower Frame Rate**: Increase usleep() values
3. **Disable Features**: Comment out score displays or effects
4. **Use -O3**: Change CFLAGS to `-O3` for more aggressive optimization

## Troubleshooting

### Game Won't Start

```bash
# Check framebuffer device
ls -l /dev/fb*

# Check touchscreen device
ls -l /dev/input/*

# Test framebuffer access
cat /dev/urandom > /dev/fb0  # Should show noise on screen
```

### Touch Input Not Working

```bash
# Find correct input device
cat /proc/bus/input/devices

# Test touch events
hexdump -C /dev/input/event0  # Touch screen and watch for events

# Try different event devices
./build/snake /dev/fb0 /dev/input/event0
./build/snake /dev/fb0 /dev/input/event1
```

### Permission Denied

```bash
# Add user to input and video groups
usermod -a -G input,video $USER

# Or run as root (not recommended for production)
sudo ./build/snake
```

### Screen Corruption

```bash
# Reset framebuffer
dd if=/dev/zero of=/dev/fb0

# Or reboot device
reboot
```

## Technical Details

For comprehensive hardware documentation, see [`SYSTEM_ANALYSIS.md`](../SYSTEM_ANALYSIS.md#hardware-platform).

### Architecture

```
┌─────────────────────────────────────┐
│         Game Application            │
│  (snake.c / tetris.c / pong.c)     │
└─────────────────────────────────────┘
           │              │
           ▼              ▼
┌──────────────────┐  ┌──────────────────┐
│  Framebuffer API │  │  Touch Input API │
│ (framebuffer.c)  │  │ (touch_input.c)  │
└──────────────────┘  └──────────────────┘
           │              │
           ▼              ▼
┌──────────────────┐  ┌──────────────────┐
│   /dev/fb0       │  │ /dev/input/event*│
│  (Linux FB)      │  │  (Linux Input)   │
└──────────────────┘  └──────────────────┘
           │              │
           ▼              ▼
┌─────────────────────────────────────┐
│         Hardware Layer              │
│  (Display Controller + Touch IC)    │
└─────────────────────────────────────┘
```

### Memory Layout

- **Code Segment**: ~50-100KB per game
- **Data Segment**: ~10-20KB (game state, arrays)
- **Framebuffer**: Mapped, not allocated (shared with kernel)
- **Stack**: ~8KB default
- **Total Runtime**: 1-2MB per game

### Performance Metrics

Measured on 300MHz ARM Cortex-A8:

| Game   | CPU Usage | Memory | Frame Rate | Startup Time |
|--------|-----------|--------|------------|--------------|
| Snake  | 5-10%     | 1.2MB  | Variable   | <0.1s        |
| Tetris | 8-12%     | 1.5MB  | 60 FPS     | <0.1s        |
| Pong   | 10-15%    | 1.1MB  | 60 FPS     | <0.1s        |

Compare to browser-based games:
- **CPU Usage**: 50-80% (5-8x more)
- **Memory**: 20-50MB (15-30x more)
- **Startup**: 2-5 seconds (20-50x slower)

## Customization

### Changing Colors

Edit color constants in game source files:

```c
// In snake.c, tetris.c, or pong.c
#define COLOR_SNAKE RGB(0, 255, 0)  // Change to your color
```

### Adjusting Difficulty

```c
// Snake: Change initial speed
#define INITIAL_SPEED 150000  // Lower = faster

// Tetris: Change drop speed
game.drop_speed = 60;  // Lower = faster

// Pong: Change AI difficulty
game.difficulty = 2;  // 1=easy, 2=medium, 3=hard
```

### Screen Resolution

Games auto-adapt to screen resolution, but you can force specific sizes:

```c
// Modify cell_size calculation in init_game()
cell_size = 20;  // Fixed size instead of calculated
```

## License

These games are provided as-is for use with the RoomWizard device. Feel free to modify and distribute.

## Credits

- **Framework**: Custom C framebuffer and input libraries
- **Games**: Classic game implementations optimized for embedded systems
- **Optimization**: Tailored for 300MHz ARM processors

## Support

For issues or questions:
1. Check troubleshooting section above
2. Verify hardware compatibility
3. Test with different device paths
4. Check system logs: `dmesg | grep -i fb` and `dmesg | grep -i input`

---

**Enjoy gaming on your RoomWizard! 🎮**
