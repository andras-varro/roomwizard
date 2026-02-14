# RoomWizard Project Documentation

> **Comprehensive documentation for RoomWizard hardware analysis, native games development, and system modifications**

## Quick Navigation

### 📊 Active Development
- **[Project Status](PROJECT_STATUS.md)** - Current development status, tasks, and progress tracking

### 📖 Core Documentation
- **[System Analysis](SYSTEM_ANALYSIS.md)** - Hardware, firmware, and system architecture analysis
- **[Native Games Guide](NATIVE_GAMES_GUIDE.md)** - Complete guide for native C games development
- **[Browser Modifications](BROWSER_MODIFICATIONS.md)** - Browser-based game modifications and LED control
- **[System Setup](SYSTEM_SETUP.md)** - SSH access, root configuration, and minimal system setup

### 🎮 Native Games
- **[Native Games README](native_games/README.md)** - Detailed native games documentation and API reference

---

## Project Overview

The RoomWizard Project provides comprehensive documentation and tools for modifying and developing on the Steelcase RoomWizard embedded Linux device. The project includes:

- **System Analysis:** Deep dive into hardware, firmware, and protection mechanisms
- **Native Games:** High-performance C games using direct framebuffer rendering
- **Browser Modifications:** HTML5 games with LED control integration
- **System Tools:** SSH access, watchdog management, and system configuration

### Hardware Platform

- **SoC:** Texas Instruments AM335x (ARM Cortex-A8 @ 300MHz)
- **RAM:** 256MB DDR3
- **Display:** 800x480 TFT LCD with resistive touchscreen
- **OS:** Embedded Linux (kernel 4.14.52) with SysVinit
- **Storage:** SD card (4-8GB)

---

## Getting Started

### For Native Games Development

1. Read **[Native Games Guide](NATIVE_GAMES_GUIDE.md)** for complete setup instructions
2. Follow the **[Quick Start](NATIVE_GAMES_GUIDE.md#quick-start)** section
3. Compile games using the provided build scripts
4. Deploy to your RoomWizard device via SSH/SCP

**Quick Commands:**
```bash
# Compile games (in WSL)
cd /mnt/c/work/roomwizard/native_games
./compile_for_roomwizard.sh

# Deploy to device (quick method)
./deploy.sh 192.168.50.73 test        # Test mode
./deploy.sh 192.168.50.73 permanent   # Permanent mode

# OR manual deploy
scp build/* root@<roomwizard-ip>:/opt/games/
```

### For Browser Modifications

1. Read **[Browser Modifications](BROWSER_MODIFICATIONS.md)** for deployment process
2. Compile the LED control servlet
3. Modify partition images
4. Regenerate MD5 checksums
5. Write to SD card

**Key Steps:**
```bash
# Compile servlet
javac -cp "servlet-api.jar" LEDControlServlet.java

# Regenerate checksums
md5sum sd_rootfs_part.img > sd_rootfs_part.img.md5
```

### For System Setup

1. Read **[System Setup](SYSTEM_SETUP.md)** for SSH and system configuration
2. Enable SSH root access for development
3. Optionally strip down to minimal CLI system
4. Configure network and security settings

**Quick SSH Setup:**
```bash
# Mount SD card root filesystem
sudo mount /dev/sdb6 /mnt/roomwizard

# Set root password
openssl passwd -6 "your_password"
# Edit /mnt/roomwizard/etc/shadow with the hash

# Enable SSH
# Edit /mnt/roomwizard/etc/ssh/sshd_config
# Set: PermitRootLogin yes
```

---

## Documentation Structure

### System Documentation

#### [System Analysis](SYSTEM_ANALYSIS.md)
Comprehensive technical analysis covering:
- System architecture and boot sequence
- Hardware platform specifications
- Protection mechanisms (watchdog, MD5 checksums, control block)
- Hardware control interfaces (LEDs, touchscreen, framebuffer)
- Application framework (X11, WebKit, Jetty, Java)
- USB subsystem analysis
- Safe modification strategies

#### [System Setup](SYSTEM_SETUP.md)
System configuration and access:
- SSH root access setup
- Minimal CLI system configuration
- Network configuration (DHCP/static)
- Security considerations
- Troubleshooting guides

### Game Development

#### [Native Games Guide](NATIVE_GAMES_GUIDE.md)
Complete native games development guide:
- Game mode setup and installation
- Permanent game mode configuration
- Touch input system and coordinate fix
- Development guide for new games
- Hardware control (LEDs, backlight)
- Performance optimization
- Troubleshooting

#### [Native Games README](native_games/README.md)
Detailed API documentation:
- Framebuffer API
- Touch input API
- Hardware control API
- Game common utilities
- Build system and compilation

### Browser Modifications

#### [Browser Modifications](BROWSER_MODIFICATIONS.md)
Browser-based modifications:
- Brick Breaker game implementation
- LED control system (Java servlet + JavaScript)
- Deployment process
- Modification status
- Troubleshooting

---

## Key Features

### Native Games System

✅ **Direct Framebuffer Rendering** - 5-8x better performance than browser games  
✅ **Touch-Based Game Selector** - No keyboard required  
✅ **Automatic Watchdog Feeding** - Prevents system resets  
✅ **Hardware LED Control** - Visual feedback for game events  
✅ **Exit Buttons** - Return to selector from any game  
✅ **SSH Access Maintained** - Remote management available  

**Performance Comparison:**
| Metric | Browser | Native | Improvement |
|--------|---------|--------|-------------|
| CPU Usage | 50-80% | 5-15% | 5-8x better |
| Memory | 20-50MB | 1-2MB | 15-30x better |
| Startup | 2-5s | <0.1s | 20-50x faster |
| Frame Rate | 20-40 FPS | 60 FPS | 2-3x better |

### Browser Modifications

✅ **HTML5 Brick Breaker Game** - Full-featured with touch controls  
✅ **LED Control Integration** - Hardware feedback via Java servlet  
✅ **Screen Blanking** - Automatic after 5 minutes (burn-in prevention)  
✅ **High Score Persistence** - localStorage-based tracking  
✅ **Optimized Performance** - 60fps on 300MHz ARM  

---

## Common Tasks

### Compile and Deploy Native Games

```bash
# On development machine (WSL/Linux)
cd /mnt/c/work/roomwizard/native_games
./compile_for_roomwizard.sh

# Quick deploy (recommended)
./deploy.sh 192.168.50.73 test        # Test mode (browser still auto-starts)
./deploy.sh 192.168.50.73 permanent   # Permanent mode (replaces browser)

# OR manual transfer
scp build/* root@<roomwizard-ip>:/opt/games/
ssh root@<roomwizard-ip> "chmod +x /opt/games/*"

# For permanent game mode (manual)
ssh root@<roomwizard-ip>
update-rc.d browser remove
update-rc.d x11 remove
update-rc.d roomwizard-games defaults
reboot
```

### Control LEDs from Command Line

```bash
# SSH into device
ssh root@<roomwizard-ip>

# Set red LED to 50%
echo 50 > /sys/class/leds/red_led/brightness

# Set green LED to full
echo 100 > /sys/class/leds/green_led/brightness

# Turn off backlight
echo 0 > /sys/class/leds/backlight/brightness
```

### Test Touch Input

```bash
# SSH into device
ssh root@<roomwizard-ip>

# Run touch debug tool
/opt/games/touch_debug

# Run visual touch test
/opt/games/touch_test
```

---

## Project Structure

```
roomwizard/
├── README.md                          # This file - Master navigation
├── PROJECT_STATUS.md                  # Active development tracker
│
├── SYSTEM_ANALYSIS.md                 # System analysis documentation
├── NATIVE_GAMES_GUIDE.md             # Native games guide
├── BROWSER_MODIFICATIONS.md          # Browser modifications guide
├── SYSTEM_SETUP.md                   # System setup guide
│
│
├── browser_games/                     # Browser-based games
│   ├── bouncing-ball.html            # Simple bouncing ball demo
│   ├── brick-breaker-game.html       # Brick breaker game
│   ├── brick-breaker-enhanced.html   # Enhanced version
│   ├── compile_servlet.bat           # Windows servlet compiler
│   └── compile_servlet.sh            # Linux servlet compiler
│
└── native_games/                      # Native games source code
    ├── README.md                      # Comprehensive native games documentation
    ├── common/                        # Shared libraries (framebuffer, touch, hardware)
    ├── snake/                         # Snake game
    ├── tetris/                        # Tetris game
    ├── pong/                          # Pong game
    ├── build/                         # Compiled binaries
    ├── compile_for_roomwizard.sh     # Build script
    └── deploy.sh                      # Deployment script (NEW)
```

---

## Important Notes

### Protection Mechanisms

The RoomWizard uses integrity-based protection:
- **MD5 Checksums:** All partition images must have valid checksums
- **Hardware Watchdog:** 60-second timeout, must be fed every 30 seconds
- **Boot Tracker:** Increments on failed boots, triggers recovery at ≥2
- **Control Block:** Manages boot mode and failure detection

**Always:**
- Keep backup of original SD card image
- Regenerate MD5 checksums after modifications
- Feed the watchdog in custom applications
- Test modifications with serial console access

### USB Port Limitation

The micro USB port is configured in **device/gadget mode only** and cannot host USB peripherals like keyboards or mice. This is a deliberate hardware/software configuration.

**Alternatives:**
- Use touchscreen for input
- Use SSH for remote access
- Use serial console for debugging

---

## Support & Resources

### Documentation
- All consolidated documentation is in the root directory
- Original documentation preserved in [`backup_docs/`](backup_docs/)

### Development
- Native games source in [`native_games/`](native_games/)
- Build scripts and Makefile provided
- Cross-compilation support for ARM

### Community
- Check [`PROJECT_STATUS.md`](PROJECT_STATUS.md) for current development status
- Refer to troubleshooting sections in each guide
- Review backup documentation if needed

---

## Quick Links

### Most Common Tasks
- [Enable SSH Access](SYSTEM_SETUP.md#ssh-root-access-setup)
- [Compile Native Games](NATIVE_GAMES_GUIDE.md#quick-start)
- [Deploy Games to Device](NATIVE_GAMES_GUIDE.md#installation-steps)
- [Control LEDs](SYSTEM_ANALYSIS.md#led-control-multi-color-indicator)
- [Fix Touch Input Issues](NATIVE_GAMES_GUIDE.md#touch-input-system)

### Technical References
- [Hardware Specifications](SYSTEM_ANALYSIS.md#hardware-platform)
- [Boot Sequence](SYSTEM_ANALYSIS.md#boot-sequence)
- [Protection Mechanisms](SYSTEM_ANALYSIS.md#protection-mechanisms)
- [API Documentation](native_games/README.md)

---

## License & Credits

This project provides documentation and tools for the Steelcase RoomWizard embedded device. All modifications are provided as-is for educational and development purposes.

**Credits:**
- System analysis and reverse engineering
- Native games framework development
- Browser modifications and LED control
- Documentation consolidation

---

*Last Updated: 2026-02-14*  
*Documentation Version: 2.1 (Added deployment script)*
