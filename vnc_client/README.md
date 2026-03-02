# VNC Client for RoomWizard

A lightweight VNC client designed to run on the Steelcase RoomWizard embedded device, enabling remote desktop viewing and touch interaction with VNC servers (e.g., Raspberry Pi displays).

## Features

- ✅ **Direct Framebuffer Rendering** - No X11 overhead
- ✅ **Touch Input Support** - Map touchscreen to VNC mouse events
- ✅ **Automatic Scaling** - Letterbox mode maintains aspect ratio
- ✅ **Visual Touch Feedback** - See where you touched
- ✅ **Watchdog Integration** - Prevents system reboot
- ✅ **FPS Counter** - Monitor performance
- ✅ **Connection Status** - Visual feedback
- 🔄 **On-Screen Keyboard** - Coming in Phase 3

## Quick Start

### Prerequisites

On your build machine (Linux):
```bash
sudo apt-get install build-essential cmake wget tar
sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```

### Build

1. **Build dependencies** (first time only):
```bash
cd vnc_client
chmod +x build-deps.sh
./build-deps.sh
```

This will download and cross-compile:
- LibVNCClient 0.9.14
- zlib 1.3
- libjpeg-turbo 3.0.1

2. **Build VNC client**:
```bash
make
```

This produces `vnc_client_stripped` (~200KB binary).

### Deploy to RoomWizard

```bash
# Create directory on device
ssh root@192.168.50.73 'mkdir -p /opt/vnc_client'

# Deploy binary
make deploy DEVICE_IP=192.168.50.73
```

### Run

On the RoomWizard device:
```bash
ssh root@192.168.50.73
/opt/vnc_client/vnc_client <raspberry_pi_ip>
```

Example:
```bash
/opt/vnc_client/vnc_client 192.168.1.100
```

## Configuration

Edit [`config.h`](config.h) to customize:

### VNC Server Settings
```c
#define VNC_DEFAULT_HOST "192.168.1.100"  // Your RPi IP
#define VNC_DEFAULT_PORT 5900              // VNC port
#define VNC_PASSWORD ""                    // Password if needed
```

### Display Settings
```c
#define DEFAULT_SCALING_MODE SCALING_LETTERBOX  // or SCALING_STRETCH
#define SHOW_REMOTE_CURSOR 1                    // Show cursor
#define ENABLE_DOUBLE_BUFFER 1                  // Smooth rendering
```

### Performance Settings
```c
#define TARGET_FPS 30                      // Frame rate limit
#define SHOW_FPS_COUNTER 1                 // Display FPS
#define DEBUG_ENABLED 1                    // Debug output
```

## Architecture

```
┌─────────────────────────────────────────────────────┐
│ vnc_client.c (Main Application)                     │
│  - Connection management                            │
│  - Main event loop                                  │
│  - Watchdog feeding                                 │
└────────────┬────────────────────────────────────────┘
             │
             ├──> vnc_renderer.c (Display)
             │     - Framebuffer rendering
             │     - Scaling (letterbox/stretch)
             │     - Touch feedback
             │     - FPS counter
             │
             ├──> vnc_input.c (Input Handling)
             │     - Touch event processing
             │     - Coordinate mapping
             │     - VNC pointer events
             │
             └──> LibVNCClient (VNC Protocol)
                   - RFB protocol handling
                   - Encoding/decoding
                   - Network communication
```

## Project Structure

```
vnc_client/
├── config.h              # Configuration settings
├── vnc_client.c          # Main application
├── vnc_renderer.h/c      # Display rendering
├── vnc_input.h/c         # Input handling
├── Makefile              # Build system
├── build-deps.sh         # Dependency builder
├── README.md             # This file
└── deps/                 # Built dependencies (created by build-deps.sh)
    ├── lib/              # Static libraries
    ├── include/          # Headers
    ├── src/              # Downloaded sources
    └── build/            # Build artifacts
```

## Usage Examples

### Basic Connection
```bash
# Connect to VNC server at default port
/opt/vnc_client/vnc_client 192.168.1.100
```

### Custom Port
```bash
# Connect to VNC server on custom port
/opt/vnc_client/vnc_client 192.168.1.100 5901
```

### Auto-start on Boot

Create init script `/etc/init.d/vnc-client`:
```bash
#!/bin/sh
### BEGIN INIT INFO
# Provides:          vnc-client
# Required-Start:    $network
# Required-Stop:
# Default-Start:     5
# Default-Stop:      0 1 6
# Short-Description: VNC Client
### END INIT INFO

VNC_SERVER="192.168.1.100"

case "$1" in
    start)
        echo "Starting VNC client..."
        /opt/vnc_client/vnc_client $VNC_SERVER &
        ;;
    stop)
        echo "Stopping VNC client..."
        killall vnc_client
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
        ;;
esac
```

Enable:
```bash
chmod +x /etc/init.d/vnc-client
ln -s /etc/init.d/vnc-client /etc/rc5.d/S99vnc-client
```

## Troubleshooting

### Connection Failed
- Check VNC server is running: `nmap -p 5900 <rpi_ip>`
- Verify network connectivity: `ping <rpi_ip>`
- Check firewall settings on RPi

### Black Screen
- Verify VNC server is sending framebuffer updates
- Check debug output: Look for "Framebuffer update" messages
- Try different scaling mode in config.h

### Touch Not Working
- Check touch device: `ls -l /dev/input/event0`
- Verify touch calibration
- Check debug output for "Touch" messages

### System Reboots
- Watchdog thread may have failed
- Check `/dev/watchdog` permissions
- Verify watchdog daemon is not conflicting

### Poor Performance
- Reduce `TARGET_FPS` in config.h
- Disable `SHOW_FPS_COUNTER`
- Use `SCALING_STRETCH` instead of `SCALING_LETTERBOX`
- Enable compression on VNC server

## Performance

### Expected Performance
- **Frame Rate:** 15-30 fps (depends on content)
- **CPU Usage:** 10-20% typical, 30-40% during updates
- **Memory:** ~6MB (framebuffers + network buffers)
- **Network:** <100 Kbps for static content, 1-5 Mbps for active updates
- **Touch Latency:** <50ms on local network

### Optimization Tips
1. Use Tight or ZRLE encoding on VNC server
2. Reduce color depth on VNC server (16-bit vs 24-bit)
3. Lower frame rate if not needed
4. Use SCALING_STRETCH for faster rendering
5. Disable FPS counter in production

## Development

### Building for Development
```bash
# Build with debug symbols
make CFLAGS="-g -O0"

# Run with verbose output
DEBUG_ENABLED=1 ./vnc_client <ip>
```

### Adding Features
1. Edit source files
2. Rebuild: `make clean && make`
3. Test locally or deploy to device
4. Update documentation

### Code Style
- Follow existing code style
- Use DEBUG_PRINT() for debug output
- Comment complex algorithms
- Update README for new features

## Known Limitations

### Current Version (v1.0 - MVP)
- ❌ No on-screen keyboard (planned for v1.1)
- ❌ No auto-reconnect (planned for v1.1)
- ❌ No configuration file (planned for v1.1)
- ❌ Partial region updates not optimized
- ❌ Only tested with Raw encoding

### Hardware Limitations
- Single-touch only (no multi-touch)
- Resistive touchscreen (less accurate at edges)
- 800x480 display (may need scaling for larger remotes)

## Roadmap

### Phase 1: MVP (Current)
- [x] Basic VNC connection
- [x] Framebuffer rendering
- [x] Touch input
- [x] Scaling support
- [x] Watchdog integration

### Phase 2: Optimization (Next)
- [ ] Tight/ZRLE encoding support
- [ ] Partial region updates
- [ ] Auto-reconnect
- [ ] Configuration file
- [ ] Performance optimization

### Phase 3: Advanced Features
- [ ] On-screen keyboard
- [ ] Connection settings UI
- [ ] Multiple server profiles
- [ ] Screenshot capture
- [ ] Recording mode

## Contributing

This is a custom project for the RoomWizard device. If you have improvements:

1. Test thoroughly on actual hardware
2. Document changes
3. Update this README
4. Consider backward compatibility

## License

This project uses:
- **LibVNCClient** - GPL v2 (https://github.com/LibVNC/libvncserver)
- **zlib** - zlib License (https://zlib.net/)
- **libjpeg-turbo** - BSD-style licenses (https://libjpeg-turbo.org/)

Custom code (vnc_client.c, vnc_renderer.c, vnc_input.c) is provided as-is for use with the RoomWizard device.

## Support

For issues or questions:
1. Check troubleshooting section above
2. Review debug output
3. Check system logs: `/var/log/messages`
4. Verify hardware functionality with native_games

## References

- [LibVNCClient Documentation](https://libvnc.github.io/)
- [RFB Protocol Specification](https://github.com/rfbproto/rfbproto)
- [RoomWizard System Analysis](../SYSTEM_ANALYSIS.md)
- [Native Games Project](../native_games/)
- [Project Planning Documents](../plans/)

## Version History

### v1.0 (2026-02-27) - Initial Release
- Basic VNC client functionality
- Touch input support
- Letterbox scaling
- Watchdog integration
- FPS counter
- Connection status display

---

**Built for RoomWizard** | ARM Cortex-A8 @ 600MHz | 800x480 Display
