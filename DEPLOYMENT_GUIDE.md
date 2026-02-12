# RoomWizard Brick Breaker Game - Deployment Guide

This guide provides step-by-step instructions for deploying the modified RoomWizard firmware with the brick breaker game and LED control to your device.

## Overview

The deployment involves:
1. Compiling the LED control servlet
2. Copying modified files to SD card
3. Regenerating MD5 checksums
4. Booting the device with modified firmware

## Prerequisites

- **Backup**: Complete backup of original SD card (CRITICAL!)
- **Java Development Kit**: JDK 8 or compatible for compiling servlet
- **SD Card Access**: Ability to mount and modify SD card partitions
- **Serial Console** (recommended): For debugging if boot fails

## Files Modified

### 1. Game Files
- `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/pages/index.html`
- `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/pages/index.html.template`

### 2. LED Control Servlet
- `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes/LEDControlServlet.java`
- `partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/web.xml`

## Deployment Steps

### Step 1: Compile the LED Control Servlet

The servlet needs to be compiled before deployment. You have two options:

#### Option A: Compile on Development Machine (Recommended)

```bash
# Navigate to the servlet directory
cd "partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes"

# Compile the servlet (requires servlet-api.jar from Jetty)
javac -cp "../../lib/servlet-api-3.1.jar" LEDControlServlet.java

# Verify compilation
ls -la LEDControlServlet.class
```

If you don't have the servlet-api JAR, you can find it in the Jetty installation or download it:
```bash
# The servlet-api JAR should be in one of these locations:
# - partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/lib/servlet-api-3.1.jar
# - partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/lib/jetty-servlet-*.jar
```

#### Option B: Compile on Device After Deployment

If you can't compile beforehand, you can compile on the device after boot:

```bash
# SSH into device (if network is configured)
ssh root@<device-ip>

# Navigate to servlet directory
cd /opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes

# Compile using device's Java
/opt/openjre-8/bin/javac -cp "../../lib/servlet-api-3.1.jar" LEDControlServlet.java

# Restart Jetty
/etc/init.d/webserver restart
```

### Step 2: Prepare the Root Filesystem Partition

The root filesystem partition needs to be modified and repacked.

#### Mount the Partition Image

```bash
# Create mount point
mkdir -p /tmp/rootfs_mount

# Mount the root filesystem image
sudo mount -o loop partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c.img /tmp/rootfs_mount

# Or if it's a directory (already extracted):
# The partition is already extracted as a directory, so we can work directly
```

#### Verify Modified Files

```bash
# Check that all modified files are present
ls -la "partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/pages/index.html"
ls -la "partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes/LEDControlServlet.class"
ls -la "partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/web.xml"
```

### Step 3: Create/Update Partition Image

If working with extracted directories, you need to create a partition image:

```bash
# Calculate required size (add 10% overhead)
SIZE=$(du -sb partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c | awk '{print int($1 * 1.1)}')

# Create ext3 filesystem image
dd if=/dev/zero of=sd_rootfs_part.img bs=1 count=0 seek=$SIZE
mkfs.ext3 sd_rootfs_part.img

# Mount and copy files
mkdir -p /tmp/new_rootfs
sudo mount -o loop sd_rootfs_part.img /tmp/new_rootfs
sudo cp -a partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/* /tmp/new_rootfs/
sudo umount /tmp/new_rootfs
```

### Step 4: Regenerate MD5 Checksums

**CRITICAL**: All modified partition images must have updated MD5 checksums or the device will refuse to boot.

```bash
# Generate MD5 for root filesystem
md5sum sd_rootfs_part.img > sd_rootfs_part.img.md5

# Verify the MD5 file was created
cat sd_rootfs_part.img.md5
```

### Step 5: Prepare SD Card

#### Option A: Direct SD Card Modification (Risky)

```bash
# Insert SD card and identify device (e.g., /dev/sdb)
lsblk

# Mount the root partition (usually partition 5 or 6)
sudo mount /dev/sdb6 /mnt/sdcard

# Copy modified files
sudo cp -a partitions/108a1490-8feb-4d0c-b3db-995dc5fc066c/opt/jetty-9-4-11/webapps/frontpanel/* \
    /mnt/sdcard/opt/jetty-9-4-11/webapps/frontpanel/

# Unmount
sudo umount /mnt/sdcard
```

#### Option B: Full Image Write (Safer)

```bash
# Create complete SD card image with modifications
# This requires reassembling all partitions

# Write to SD card (CAUTION: This will erase the card!)
sudo dd if=modified_sdcard.img of=/dev/sdb bs=4M status=progress
sync
```

### Step 6: Boot the Device

1. **Insert modified SD card** into RoomWizard
2. **Power on** the device
3. **Monitor boot process** via serial console (if available)
4. **Wait for X11 and browser** to start (may take 60-90 seconds)

### Step 7: Verify Deployment

#### Check Game Loads

1. The device should boot to the brick breaker game
2. You should see the difficulty selection screen
3. Touch controls should work

#### Test LED Control

Open browser console (if accessible) or test via command line:

```bash
# SSH into device
ssh root@<device-ip>

# Test LED control directly
echo 100 > /sys/class/leds/green_led/brightness
sleep 1
echo 0 > /sys/class/leds/green_led/brightness

# Test via HTTP endpoint
curl "http://localhost/frontpanel/led?color=green&brightness=100"
curl "http://localhost/frontpanel/led?color=red&brightness=50"
curl "http://localhost/frontpanel/led?color=green&brightness=0"
```

#### Verify Servlet is Running

```bash
# Check Jetty logs
tail -f /var/log/jetty.log

# Check for servlet loading
grep -i "LEDControlServlet" /var/log/jetty.log

# Test servlet directly
curl -v "http://localhost/frontpanel/led?color=green&brightness=100"
```

## Troubleshooting

### Device Won't Boot

**Symptoms**: Device resets repeatedly, never shows display

**Causes**:
1. MD5 checksum mismatch
2. Corrupted partition image
3. Watchdog timeout

**Solutions**:
```bash
# 1. Verify MD5 checksums
md5sum -c sd_rootfs_part.img.md5

# 2. Check control block
# The device may have incremented boot_tracker to failure mode (≥2)
# You may need to reset the control block or restore from backup

# 3. Restore original SD card backup
sudo dd if=original_backup.img of=/dev/sdb bs=4M status=progress
```

### Game Loads But LEDs Don't Work

**Symptoms**: Game works, but no LED feedback

**Diagnosis**:
```bash
# Check if servlet compiled
ls -la /opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/classes/LEDControlServlet.class

# Check servlet registration
grep -A5 "ledControlServlet" /opt/jetty-9-4-11/webapps/frontpanel/WEB-INF/web.xml

# Check Jetty logs for errors
tail -100 /var/log/jetty.log | grep -i error

# Test LED hardware directly
echo 100 > /sys/class/leds/red_led/brightness
```

**Solutions**:
1. Compile servlet if .class file is missing
2. Verify web.xml has correct servlet registration
3. Check file permissions (should be readable by jetty user)
4. Restart Jetty: `/etc/init.d/webserver restart`

### Servlet Compilation Errors

**Error**: `package javax.servlet does not exist`

**Solution**:
```bash
# Find servlet-api JAR
find /opt/jetty-9-4-11 -name "*servlet*.jar"

# Compile with correct classpath
javac -cp "/opt/jetty-9-4-11/lib/servlet-api-3.1.jar" LEDControlServlet.java
```

### Permission Denied on LED Control

**Error**: `Failed to control LED: Permission denied`

**Solution**:
```bash
# Check LED file permissions
ls -la /sys/class/leds/*/brightness

# Jetty may need permission to write to LED files
# Add jetty user to appropriate group or modify udev rules
chmod 666 /sys/class/leds/*/brightness
```

## Rollback Procedure

If anything goes wrong:

1. **Power off** the device
2. **Remove SD card**
3. **Restore original backup**:
   ```bash
   sudo dd if=original_backup.img of=/dev/sdb bs=4M status=progress
   sync
   ```
4. **Reinsert card** and power on

## Performance Optimization

After successful deployment, you can optimize:

1. **Precompile Java classes** for faster startup
2. **Adjust JVM heap size** if needed
3. **Monitor memory usage**: `free -m`
4. **Check CPU usage**: `top`

## Security Considerations

1. **LED Control Access**: The servlet is accessible without authentication
   - Consider adding authentication if device is network-accessible
   - Rate limiting may be needed to prevent LED abuse

2. **File Permissions**: Ensure LED control files have appropriate permissions

3. **Network Security**: If device is on network, consider firewall rules

## Additional Notes

- **Watchdog Timer**: The game includes watchdog feeding every 30 seconds
- **Screen Blanking**: Automatic after 5 minutes, touch to wake
- **High Scores**: Stored in browser localStorage (persists across reboots)
- **Game State**: Not preserved on reboot (intentional)

## Support Files

- **Analysis**: See `analysis.md` for detailed firmware analysis
- **LED Implementation**: See `LED_CONTROL_IMPLEMENTATION.md` for technical details
- **Game Documentation**: See `BRICK_BREAKER_README.md` for game features

## Success Criteria

✅ Device boots successfully
✅ Brick breaker game displays on screen
✅ Touch controls work (paddle movement, ball launch)
✅ LEDs flash during game events (level complete, power-up, etc.)
✅ Game persists high scores
✅ Screen blanking works after 5 minutes
✅ Device remains stable (no watchdog resets)

## Contact & Support

If you encounter issues not covered in this guide:
1. Check serial console output for boot errors
2. Review `/var/log/` files on device
3. Verify all MD5 checksums match
4. Ensure backup is available for rollback