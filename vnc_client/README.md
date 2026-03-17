# VNC Client for RoomWizard

A lightweight VNC client for the Steelcase RoomWizard embedded device.
Displays a remote desktop (e.g. Raspberry Pi weather/clock display) on the
RoomWizard's 800×480 screen with touch pass-through.

## Features

- **Direct framebuffer rendering** — no X11, no compositor overhead
- **Bilinear interpolation** — smooth downscaling (1920×1080 → 795×447)
- **Tight/ZRLE encoding** — 10-100× bandwidth reduction vs raw
- **16bpp RGB565** — halved memory bandwidth (same as ScummVM backend)
- **Partial region updates** — only dirty rectangles are re-rendered
- **Touch input** — resistive touchscreen mapped to VNC pointer events
- **USB keyboard forwarding** — full key press/release forwarded as X11 keysyms
- **USB mouse forwarding** — relative movements with 3-tier acceleration, all buttons + scroll
- **Runtime config file** — all settings in `/opt/vnc_client/vnc_client.conf`
- **Exit gesture** — long-press top-left corner (3 s) to return to launcher
- **Watchdog integration** — prevents system reboot during operation

### Performance (v2.1)

| Metric | v1.0 (MVP) | v2.1 (Current) |
|--------|-----------|----------------|
| CPU | 99% | ~5% |
| Frame rate | <1 fps | 5-7 fps |
| Bandwidth | ~120 Mbps | <5 Mbps |
| Encoding | Raw | Tight |

---

## Quick Start

### Prerequisites

Linux build machine with ARM cross-compiler (tested on WSL 2):

```bash
sudo apt-get install build-essential cmake wget tar
sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```

### Build

```bash
cd vnc_client

# First time: build dependencies (~5 min)
chmod +x build-deps.sh
./build-deps.sh

# Build client
make
```

Produces `vnc_client_stripped` (~870 KB statically linked binary).

### Deploy

```bash
make deploy DEVICE_IP=192.168.50.53
```

This copies the binary and config file to `/opt/vnc_client/` on the device.

### Run

```bash
ssh root@192.168.50.53 '/opt/vnc_client/vnc_client'
```

---

## Configuration

Settings are loaded in this order (later overrides earlier):

1. **Compile-time defaults** — `config.h`
2. **Config file** — `/opt/vnc_client/vnc_client.conf`
3. **Command-line arguments**

### Config File

```ini
# /opt/vnc_client/vnc_client.conf
host = 192.168.50.56
port = 5901
password = roomwizard
encodings = tight zrle copyrect hextile zlib raw
compress_level = 6
quality_level = 5
```

### Command-Line

```
vnc_client [OPTIONS] [host [port]]

Options:
  --config <path>   Config file (default: /opt/vnc_client/vnc_client.conf)
  --host <ip>       VNC server host
  --port <port>     VNC server port
  --password <pw>   VNC password
  --help, -h        Show help
```

Legacy positional syntax still works: `vnc_client 192.168.50.56 5901`

### Compile-Time Settings (config.h)

| Setting | Default | Description |
|---------|---------|-------------|
| `TARGET_FPS` | 30 | Frame rate cap |
| `USE_16BPP` | 1 | Enable 16bpp RGB565 framebuffer |
| `EXIT_ZONE_SIZE` | 60 | Exit gesture corner size (pixels) |
| `EXIT_HOLD_MS` | 3000 | Exit gesture hold duration (ms) |
| `DEBUG_ENABLED` | 1 | Print debug info to stderr |

---

## Architecture

```
RPi3 VNC Server (192.168.50.56:5901)
         │ RFB Protocol (Tight encoding)
         ▼
    LibVNCClient (decode)
         │
         ▼
   vnc_renderer.c (bilinear scale + BGR→RGB565)
         │
         ▼
    /dev/fb0 (800×480 16bpp)

/dev/input/event0 (touch)         ─┐
/dev/input/eventN (USB keyboard)  ─┼─→ vnc_input.c ─→ VNC key/pointer events → RPi3
/dev/input/eventN (USB mouse)     ─┘    (coordinate mapping, acceleration,
                                          exit gesture detection)
```

### Source Files

| File | Purpose |
|------|---------|
| `vnc_client.c` | Main application, config parser, event loop |
| `vnc_renderer.c/h` | Bilinear scaling, BGR→RGB565, partial updates |
| `vnc_input.c/h` | Touch/keyboard/mouse→VNC mapping, acceleration, exit gesture |
| `config.h` | Compile-time defaults and constants |
| `vnc_client.conf` | Runtime configuration template |
| `Makefile` | Cross-compilation and deployment |
| `build-deps.sh` | Downloads and builds zlib, libjpeg-turbo, LibVNCClient |
| `rpi_setup.py` | RPi VNC server setup script |

### Shared Libraries (from native_apps)

- `framebuffer.c` — mmap'd double-buffered `/dev/fb0` access
- `touch_input.c` — `/dev/input/event0` polling
- `hardware.c` — LEDs, backlight control

---

## RPi VNC Server Setup

The RoomWizard uses **x11vnc** on port 5901 (standard VncAuth) because
RealVNC on port 5900 uses proprietary encryption incompatible with LibVNCClient.

```bash
# Requires: pip install paramiko
python3 rpi_setup.py <rpi_host> <rpi_user> <rpi_password> [action]
```

| Action | Description |
|--------|-------------|
| `fix-and-install` | Fix Buster EOL repos + install x11vnc (default) |
| `manual-install` | Direct .deb download if apt fails |
| `verify` | Check x11vnc service status |
| `inspect` | Inspect RPi display environment |

This creates a systemd service (`x11vnc-roomwizard`) on port 5901 with
password authentication. RealVNC on port 5900 is left untouched.

---

## Exit Gesture

To return to the game launcher, **long-press the top-left corner** of the
screen for 3 seconds. The client exits cleanly, restoring 32bpp for other
applications.

No touch events are sent to the VNC server while holding the exit zone.

---

## USB Input Forwarding

In addition to touch, the VNC client forwards USB keyboard and mouse events to the remote VNC server. Input handling is implemented in [`vnc_input.c`](vnc_input.c) / [`vnc_input.h`](vnc_input.h).

### USB Keyboard

Full forwarding of key press/release events to the VNC server as **X11 keysyms**:

- All standard keys mapped — letters, numbers, F-keys (F1–F12), arrow keys, modifiers (Shift, Ctrl, Alt), punctuation, Enter, Backspace, Tab, Escape
- Auto-repeat events from the kernel are forwarded as-is
- Key press and key release are sent as separate VNC key events, preserving modifier state
- Works identically to a keyboard connected directly to the remote machine

### USB Mouse

Relative mouse movements are accumulated with **3-tier acceleration** and forwarded as VNC absolute pointer events in remote desktop coordinate space:

- **Slow** (< 3 px) — 1:1 mapping for precision
- **Medium** (3–10 px) — 2× multiplier
- **Fast** (> 10 px) — 4× multiplier

Button support:
- Left click, right click, middle click
- Scroll wheel up/down (VNC button 4/5)

> **Note:** The mouse operates in **remote desktop coordinates** (e.g. 1920×1080), not local 800×480 screen space. Cursor position is tracked internally and scaled to the remote resolution before sending pointer events.

### Auto-Detection

USB devices are auto-detected by scanning `/dev/input/event*` **every 5 seconds**. USB hotplug is fully supported — plug in a keyboard or mouse while the VNC client is running and it starts working immediately.

### Touch Preserved

Existing touch forwarding and the exit gesture (long-press top-left corner) are unchanged. All input methods work simultaneously — touch, keyboard, and mouse can be used interchangeably.

### Configuration

Mouse sensitivity is configurable via `/etc/input_config.conf` (shared with native apps):

```ini
# /etc/input_config.conf
mouse_sensitivity=1.0
mouse_acceleration=1
```

See [native_apps/README.md](../native_apps/README.md#input-configuration) for the full configuration reference.

---

## Optimizations

Key techniques adopted from the ScummVM RoomWizard backend:

| ID | Technique | Impact |
|----|-----------|--------|
| O1 | Tight/ZRLE encoding negotiation | 10-100× bandwidth reduction |
| O2 | Precomputed bilinear X-LUT | No per-pixel division |
| O3 | Border-only clearing | Avoids full-screen memset |
| O8 | 16bpp RGB565 framebuffer | Halves fb_swap cost |
| O11 | Row deduplication (temp row) | Skips ~57% of identical rows |
| — | Bilinear interpolation | Smooth text at 2.4:1 downscale |
| — | Partial region updates | Only dirty rectangles rendered |
| — | Frame-rate-capped present | No unnecessary fb_swap calls |

---

## Hardware

| Component | Specification |
|-----------|--------------|
| CPU | ARM Cortex-A8 @ 600 MHz |
| RAM | 234 MB (182 MB available) |
| Display | 800×480 framebuffer (`/dev/fb0`) |
| Input | Resistive single-touch (`/dev/input/event0`) + USB keyboard/mouse (`/dev/input/event*`) |
| Network | 10/100 Mbps Ethernet |

## Dependencies

| Library | Version | License |
|---------|---------|---------|
| LibVNCClient | 0.9.14 | GPL v2 |
| zlib | 1.2.13 | zlib |
| libjpeg-turbo | 2.1.5.1 | BSD-style |

Built as static libraries by `build-deps.sh` using the ARM cross-compiler.

---

## Troubleshooting

### Connection Failed
- Verify x11vnc is running: `ssh pi@<rpi> 'systemctl status x11vnc-roomwizard'`
- Check port: `nmap -p 5901 <rpi_ip>`
- Check password in `/opt/vnc_client/vnc_client.conf`

### Colors Wrong (orange ↔ blue)
- x11vnc sends BGR byte order; the renderer handles this automatically
- If colors are swapped, check `vnc_renderer.c` BGR→RGB565 conversion

### Touch Not Working
- Verify device: `ls -l /dev/input/event0`
- Check debug output for "Touch" messages

### Poor Frame Rate
- Lower `quality_level` in config (trades JPEG quality for speed)
- Raise `compress_level` (more server CPU, less bandwidth)
- Normal for this hardware is 5-7 fps with 1080p source

### System Reboots
- The watchdog thread feeds `/dev/watchdog` every 30 s
- If the client crashes without cleanup, the system will reboot in ~60 s

---

## Auto-Start on Boot

Create `/etc/init.d/vnc-client`:

```bash
#!/bin/sh
### BEGIN INIT INFO
# Provides:          vnc-client
# Required-Start:    $network
# Required-Stop:
# Default-Start:     5
# Default-Stop:      0 1 6
### END INIT INFO

case "$1" in
    start)
        /opt/vnc_client/vnc_client &
        ;;
    stop)
        killall vnc_client
        ;;
esac
```

```bash
chmod +x /etc/init.d/vnc-client
ln -s /etc/init.d/vnc-client /etc/rc5.d/S99vnc-client
```

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| v1.0 | 2026-02-27 | MVP: raw encoding, 99% CPU, <1 fps |
| v2.0 | 2026-03-01 | Tight encoding, 16bpp, partial updates, row dedup → 5% CPU, 5 fps |
| v2.1 | 2026-03-01 | Bilinear scaling, runtime config file, exit gesture, merged docs |

---

## License

Custom code (`vnc_client.c`, `vnc_renderer.c`, `vnc_input.c`) is provided as-is
for use with the RoomWizard device. Dependencies carry their own licenses (see above).
