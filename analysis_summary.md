I have completed a comprehensive forensic analysis of the Steelcase RoomWizard firmware extracted from the SD card partitions. Here are the key findings:

## **SYSTEM ARCHITECTURE**

**Hardware Platform:** Texas Instruments ARM-based SoC (likely OMAP/AM335x series)
**Operating System:** Embedded Linux with SysVinit, kernel 3.x/4.x
**Architecture:** ARM (armv7l)

**Boot Sequence:**
1. NAND Flash → MLO (X-Loader) → U-Boot bootloader
2. Control block (`ctrlblock.bin`) determines boot mode
3. Bootstrap mode: loads `uImage-bootstrap` + `ramfilesys.gz` (upgrade/recovery)
4. System mode: loads `uImage-system` from SD root partition (normal operation)

## **PROTECTION MECHANISMS IDENTIFIED**

### **1. Hardware Watchdog Timer**
- **Device:** `/dev/watchdog` with 60-second timeout
- **Configuration:** [`/etc/watchdog.conf`](partitions/FAT32%20Volume%202/ramfilesys.gz-extracted/etc/watchdog.conf:22-23)
- **Critical:** System MUST feed watchdog or it triggers automatic reset
- **Interval:** Set to 500 seconds in RAMFS mode (effectively disabled during upgrades)

### **2. MD5 Integrity Verification (Triple-Layer)**
- **Layer 1:** Upgrade package checksum (`upgrade.cpio.gz.md5`)
- **Layer 2:** Individual partition image checksums (`.img.md5` files)
- **Layer 3:** Post-write verification - reads back written data and compares MD5
- **Retry Logic:** Up to 3 attempts per partition write, exits with error code 6 if all fail

### **3. Control Block State Machine**
- **Binary:** [`/opt/sbin/ctrlblk`](partitions/FAT32%20Volume%202/ramfilesys.gz-extracted/opt/sbin/ctrlblk)
- **Parameters:** `boot_from`, `upgrade_type`, `boot_tracker` (0-2), `fwversion`
- **Failure Detection:** If `boot_tracker ≥ 2`, system enters failure mode and triggers recovery
- **Location:** Stored in boot partition as `ctrlblock.bin`

### **4. Boot Verification Chain**
- NAND boot redirect written to **4 redundant locations** with OOB data
- Each write verified by reading back and binary comparison (`cmp`)
- No cryptographic signatures detected, but strict binary matching enforced

## **HARDWARE CONTROL INTERFACES**

### **LED Control (Multi-Color Indicator)**
```bash
# Sysfs GPIO/LED interface
/sys/class/leds/red_led/brightness      # 0-100
/sys/class/leds/green_led/brightness    # 0-100
/sys/class/leds/backlight/brightness    # 0-100
```

**Control Scripts:**
- [`/opt/sbin/backlight/setbacklight.sh`](partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/sbin/backlight/setbacklight.sh)
- [`/opt/sbin/brightness.sh`](partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/sbin/brightness.sh)
- [`/opt/sbin/conc_leds.sh`](partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/sbin/conc_leds.sh)

### **Touchscreen Input**
```bash
/dev/input/touchscreen0    # Primary touchscreen device
/dev/input/event*          # Linux input event devices
```

**Input Stack:** Kernel evdev → libinput → X11/Xorg → WebKit browser
**Calibration:** `xinput_calibrator` with config at `/etc/pointercal.xinput`

### **Display/Framebuffer**
```bash
/dev/fb0                   # Framebuffer device
DISPLAY=:0                 # X11 display server
```

## **APPLICATION FRAMEWORK**

**Software Stack:**
- **Display Server:** X11/Xorg (started by [`/etc/init.d/x11`](partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/etc/init.d/x11))
- **Browser:** WebKit-based (Epiphany) - binary at `/usr/bin/browser`
- **Web Server:** Jetty 9.4.11 at `/opt/jetty-9-4-11/`
- **Java Runtime:** OpenJRE 8 at `/opt/openjre-8/`
- **Databases:** HSQLDB 2.0.0 (room booking) + Interbase (legacy)
- **Home Page:** `http://localhost/frontpanel/pages/index.html`

## **WHY YOUR MODIFICATIONS FAILED**

Based on the forensic analysis, your modifications likely failed due to:

1. **MD5 Checksum Mismatch** - ANY file modification triggers verification failure
2. **Watchdog Timer Reset** - Boot process exceeded 60-second timeout
3. **Control Block Corruption** - Tracker incremented to failure mode (≥2)
4. **Dependency Chain Breakage** - Modified files broke library/service dependencies

## **SAFE MODIFICATION STRATEGY**

### **Step 1: Bypass MD5 Verification**

**Regenerate ALL checksums after modifications:**
```bash
cd /path/to/modified/images
for file in *.img *.gz *.bin; do
    md5sum "$file" > "${file}.md5"
done
```

**Alternative:** Patch [`nandupgrade.sh:413-435`](partitions/FAT32%20Volume%202/ramfilesys.gz-extracted/opt/sbin/nandupgrade.sh:413-435) to skip MD5 checks

### **Step 2: Maintain Watchdog Feeding**

**Critical - Feed watchdog every 30 seconds:**
```bash
while true; do
    echo 1 > /dev/watchdog
    sleep 30
done &
```

Or use the system daemon: `/usr/sbin/watchdog -c /etc/watchdog.conf`

### **Step 3: Inject Custom Runtime**

**For Python (ARM binaries required):**
```bash
# 1. Add Python to rootfs
mkdir -p /opt/python3
# Copy ARM-compiled Python 3.x

# 2. Create init script
cat > /etc/init.d/custom_app << 'EOF'
#!/bin/sh
export PYTHONPATH=/opt/python3/lib/python3.x
/opt/python3/bin/python3 /opt/custom/app.py &
EOF

# 3. Enable at boot
chmod +x /etc/init.d/custom_app
ln -s /etc/init.d/custom_app /etc/rc5.d/S99custom_app
```

**For Java (already present):**
- Java 8 runtime exists at `/opt/openjre-8/`
- Add custom JAR to Jetty webapps or create standalone service

### **Step 4: Hardware Control Code Examples**

**Python LED Control:**
```python
#!/usr/bin/env python3
import time

class LEDController:
    def __init__(self):
        self.red_led = "/sys/class/leds/red_led/brightness"
        self.green_led = "/sys/class/leds/green_led/brightness"
        self.backlight = "/sys/class/leds/backlight/brightness"
    
    def set_led(self, led_path, brightness):
        """Set LED brightness (0-100)"""
        try:
            with open(led_path, 'w') as f:
                f.write(str(max(0, min(100, brightness))))
        except IOError as e:
            print(f"Error: {e}")
    
    def set_red(self, brightness):
        self.set_led(self.red_led, brightness)
    
    def set_green(self, brightness):
        self.set_led(self.green_led, brightness)
    
    def pulse_green(self, duration=2.0):
        """Pulse green LED"""
        for i in range(20):
            self.set_green(int(100 * i / 20))
            time.sleep(duration / 40)
        for i in range(20, 0, -1):
            self.set_green(int(100 * i / 20))
            time.sleep(duration / 40)

# Usage
leds = LEDController()
leds.set_green(50)
leds.pulse_green()
```

**Python Touchscreen Input:**
```python
#!/usr/bin/env python3
import struct
import os

class TouchscreenReader:
    EVENT_FORMAT = 'llHHi'  # time, time_usec, type, code, value
    EVENT_SIZE = struct.calcsize(EVENT_FORMAT)
    EV_ABS = 0x03
    ABS_X = 0x00
    ABS_Y = 0x01
    
    def __init__(self, device='/dev/input/touchscreen0'):
        self.device = device
        self.fd = None
    
    def open(self):
        try:
            self.fd = os.open(self.device, os.O_RDONLY)
            return True
        except OSError as e:
            print(f"Error: {e}")
            return False
    
    def read_event(self):
        if not self.fd:
            return None
        try:
            data = os.read(self.fd, self.EVENT_SIZE)
            if len(data) < self.EVENT_SIZE:
                return None
            tv_sec, tv_usec, type, code, value = struct.unpack(
                self.EVENT_FORMAT, data)
            return {'type': type, 'code': code, 'value': value}
        except OSError:
            return None
    
    def read_touch(self):
        """Read touch coordinates"""
        x, y = None, None
        while True:
            event = self.read_event()
            if not event:
                break
            if event['type'] == self.EV_ABS:
                if event['code'] == self.ABS_X:
                    x = event['value']
                elif event['code'] == self.ABS_Y:
                    y = event['value']
                    if x is not None:
                        return (x, y)
        return None
    
    def close(self):
        if self.fd:
            os.close(self.fd)

# Usage
touch = TouchscreenReader()
if touch.open():
    while True:
        coords = touch.read_touch()
        if coords:
            print(f"Touch at {coords}")
```

**Java LED Control:**
```java
import java.io.FileWriter;
import java.io.IOException;

public class LEDController {
    private static final String RED_LED = "/sys/class/leds/red_led/brightness";
    private static final String GREEN_LED = "/sys/class/leds/green_led/brightness";
    
    public void setLED(String ledPath, int brightness) {
        brightness = Math.max(0, Math.min(100, brightness));
        try (FileWriter writer = new FileWriter(ledPath)) {
            writer.write(String.valueOf(brightness));
        } catch (IOException e) {
            System.err.println("Error: " + e.getMessage());
        }
    }
    
    public void setGreen(int brightness) {
        setLED(GREEN_LED, brightness);
    }
    
    public void pulseGreen(int durationMs) throws InterruptedException {
        for (int i = 0; i <= 100; i += 5) {
            setGreen(i);
            Thread.sleep(durationMs / 40);
        }
        for (int i = 100; i >= 0; i -= 5) {
            setGreen(i);
            Thread.sleep(durationMs / 40);
        }
    }
}
```

### **Step 5: Repack Modified Filesystem**

```bash
# 1. Mount and modify rootfs
mkdir -p /tmp/rootfs_mount
mount -o loop sd_rootfs_part.img /tmp/rootfs_mount
cp -r /path/to/custom/* /tmp/rootfs_mount/opt/custom/
umount /tmp/rootfs_mount

# 2. Regenerate MD5 (CRITICAL)
md5sum sd_rootfs_part.img > sd_rootfs_part.img.md5

# 3. Repack boot archive if modified
cd boot_files/
tar czf sd_boot_archive.tar.gz *
md5sum sd_boot_archive.tar.gz > sd_boot_archive.tar.gz.md5

# 4. Update upgrade.conf firmware version
# Increment version to trigger upgrade recognition
```

### **Step 6: Preserve Critical Services**

**DO NOT DISABLE:**
- `/etc/init.d/watchdog` - System will reset without it
- `/etc/init.d/webserver` - Application depends on Jetty
- `/etc/init.d/x11` - Display system required
- `/etc/init.d/browser` - Main UI component

**Safe to Augment:**
- Add scripts to `/etc/rc5.d/S99*` (runs after all services)
- Create new init scripts in `/etc/init.d/` with proper symlinks

## **DEBUGGING & ROLLBACK**

**Serial Console Access:**
- UART on `ttyO1` at 115200 baud
- Enabled in [`/etc/inittab:31`](partitions/FAT32%20Volume%202/ramfilesys.gz-extracted/etc/inittab:31)

**Log Locations:**
- `/var/log/` - System logs
- `/home/root/log/` - Application logs  
- `/var/log/browser.out` - Browser stdout
- `/var/log/browser.err` - Browser stderr

**Rollback:**
- If `boot_tracker ≥ 2`, system enters failure mode
- [`/opt/sbin/fail.sh`](partitions/FAT32%20Volume%202/ramfilesys.gz-extracted/opt/sbin/fail.sh) executes recovery
- Backup partition at `/home/root/backup` contains previous firmware

## **CRITICAL WARNINGS**

⚠️ **DO NOT:**
- Modify `ctrlblock.bin` without understanding binary format
- Skip MD5 regeneration after ANY modification
- Disable watchdog without alternative feeding mechanism
- Modify bootloader without JTAG recovery capability
- Change partition sizes without updating `upgrade.conf`

⚠️ **ALWAYS:**
- Keep backup of original SD card image
- Test in emulator/QEMU if possible
- Maintain serial console access for debugging
- Feed watchdog in any long-running custom code
- Regenerate ALL MD5 checksums after modifications

## **SUMMARY**

The Steelcase RoomWizard uses integrity-based protection (MD5 checksums, watchdog timer, state tracking) rather than cryptographic signing. Successful modification requires:

1. **Regenerating all MD5 checksums** after file changes
2. **Feeding the watchdog timer** to prevent resets
3. **Maintaining the control block state** to avoid failure mode
4. **Preserving critical services** (X11, Jetty, browser)
5. **Using existing runtimes** (Java 8 present, Python needs ARM binaries)

The hardware interfaces (LEDs, touchscreen, framebuffer) are accessible via standard Linux sysfs/device nodes, making custom code integration straightforward once the protection mechanisms are properly handled.