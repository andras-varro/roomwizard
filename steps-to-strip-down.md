# RoomWizard SD Card Modification Guide
## Minimal CLI System with Networking, SSH, Root Access, and Sudo User

This guide provides step-by-step instructions for modifying the mounted SD card partitions to create a minimal system.

**Assumptions:**
- SD card partitions are mounted at `/media/pi/[partition-guid]`
- You have root/sudo access on your Linux machine
- You'll generate password hashes using `openssl passwd -6 "password"`

---

## PART 1: IDENTIFY MOUNTED PARTITIONS

```bash
# List all mounted partitions
df -h | grep /media/pi

# You should see three partitions:
# /media/pi/XXXX-XXXX          <- Boot partition (FAT32)
# /media/pi/108a1490-...       <- Root filesystem (ext3)
# /media/pi/d5758df8-...       <- Data partition (ext3)

# Set variables for easier access (adjust GUIDs as needed)
BOOT="/media/pi/XXXX-XXXX"
ROOTFS="/media/pi/108a1490-8feb-4d0c-b3db-995dc5fc066c"
DATA="/media/pi/d5758df8-c160-4364-bac2-b68fcabeef30"
```

---

## PART 2: USER ACCESS CONFIGURATION

### Step 2.1: Enable Root Login

**File to modify:** `$ROOTFS/etc/shadow`

```bash
# Backup the original file
sudo cp $ROOTFS/etc/shadow $ROOTFS/etc/shadow.backup

# Generate password hash for root
openssl passwd -6 "roomwizard"
# Copy the output hash

# Edit shadow file
sudo nano $ROOTFS/etc/shadow

# Find the line starting with "root:" (currently looks like):
# root:*:10933:0:99999:7:::

# Replace the * with your generated hash:
# root:$6$xyz...your.hash.here...:10933:0:99999:7:::

# Save and exit (Ctrl+X, Y, Enter)
```

### Step 2.2: Create Regular User with Sudo Privileges

**A. Add user to passwd file**

File: `$ROOTFS/etc/passwd`

```bash
# Edit passwd file
sudo nano $ROOTFS/etc/passwd

# Add this line at the end:
rwuser:x:1000:1000:RoomWizard User:/home/rwuser:/bin/bash

# Save and exit
```

**B. Add user to shadow file**

File: `$ROOTFS/etc/shadow`

```bash
# Generate password hash for user
openssl passwd -6 "userpassword"
# Copy the output hash

# Edit shadow file
sudo nano $ROOTFS/etc/shadow

# Add this line at the end (replace hash with your generated one):
rwuser:$6$xyz...your.hash.here...:19000:0:99999:7:::

# Save and exit
```

**C. Add user to group file**

File: `$ROOTFS/etc/group`

```bash
# Edit group file
sudo nano $ROOTFS/etc/group

# Find the line starting with "sudo:" (line 22):
# sudo:x:27:

# Change it to include your user:
# sudo:x:27:rwuser

# Also add user to other useful groups by modifying these lines:
# video:x:44:rwuser
# audio:x:29:rwuser
# input:x:19:rwuser
# dialout:x:20:rwuser

# Save and exit
```

**D. Create user home directory**

```bash
# Create home directory
sudo mkdir -p $ROOTFS/home/rwuser

# Set ownership (UID 1000, GID 1000)
sudo chown -R 1000:1000 $ROOTFS/home/rwuser

# Set permissions
sudo chmod 755 $ROOTFS/home/rwuser
```

**E. Install/Configure sudo**

File: `$ROOTFS/etc/sudoers`

```bash
# Check if sudoers file exists
ls -l $ROOTFS/etc/sudoers

# If it doesn't exist, create it:
sudo nano $ROOTFS/etc/sudoers

# Add this content:
# sudoers file
#
# This file MUST be edited with the 'visudo' command as root.
#
# See the man page for details on how to write a sudoers file.
#

Defaults        env_reset
Defaults        mail_badpass
Defaults        secure_path="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

# Host alias specification

# User alias specification

# Cmnd alias specification

# User privilege specification
root    ALL=(ALL:ALL) ALL

# Allow members of group sudo to execute any command
%sudo   ALL=(ALL:ALL) ALL

# Save with permissions 0440
# Save and exit
```

```bash
# Set correct permissions on sudoers file
sudo chmod 0440 $ROOTFS/etc/sudoers
```

---

## PART 3: CONFIGURE MINIMAL SYSTEM

### Step 3.1: Change Boot to CLI (Runlevel 3)

File: `$ROOTFS/etc/inittab`

```bash
# Edit inittab
sudo nano $ROOTFS/etc/inittab

# Find line 5:
# id:5:initdefault:

# Change to runlevel 3 (CLI only):
# id:3:initdefault:

# Save and exit
```

### Step 3.2: Disable Heavy Services

```bash
# Navigate to init.d directory
cd $ROOTFS/etc/init.d

# Disable X11 (GUI)
sudo mv x11 x11.disabled

# Disable browser
sudo mv browser browser.disabled

# Disable webserver/Jetty
sudo mv webserver webserver.disabled

# Disable heavy watchdog daemon
sudo nano $ROOTFS/etc/default/watchdog

# Change line 2 from:
# run_watchdog=1
# To:
# run_watchdog=0

# Save and exit
```

### Step 3.3: Keep Essential Services Enabled

These services should remain enabled:
- `networking` - Network connectivity
- `sshd` - SSH server for remote access
- `syslog` - System logging
- `udev` - Device management

```bash
# Verify SSH is enabled
ls -l $ROOTFS/etc/init.d/sshd
# Should exist and be executable

# Check SSH configuration
sudo nano $ROOTFS/etc/ssh/sshd_config

# Ensure these settings are present:
# PermitRootLogin yes
# PasswordAuthentication yes
# PubkeyAuthentication yes

# Save if modified
```

### Step 3.4: Configure Network for DHCP

File: `$ROOTFS/etc/network/interfaces`

```bash
# Edit network interfaces
sudo nano $ROOTFS/etc/network/interfaces

# Ensure it contains:
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet dhcp

# Save and exit
```

---

## PART 4: CREATE SIMPLE WATCHDOG FEEDER

### Step 4.1: Create Watchdog Feeder Script

```bash
# Create the script
sudo nano $ROOTFS/usr/local/bin/simple-watchdog-feeder.sh

# Add this content:
#!/bin/sh
# Simple watchdog feeder - minimal overhead
WATCHDOG_DEV=/dev/watchdog
INTERVAL=30

# Open watchdog device
exec 3<>$WATCHDOG_DEV

echo "Simple watchdog feeder started (PID: $$)"

# Main loop
while true; do
    echo "." >&3
    sleep $INTERVAL
done

# Save and exit
```

```bash
# Make executable
sudo chmod +x $ROOTFS/usr/local/bin/simple-watchdog-feeder.sh
```

### Step 4.2: Create Init Script for Watchdog Feeder

```bash
# Create init script
sudo nano $ROOTFS/etc/init.d/simple-watchdog

# Add this content:
#!/bin/sh
### BEGIN INIT INFO
# Provides:          simple-watchdog
# Required-Start:    $local_fs
# Required-Stop:     $local_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Simple watchdog feeder
### END INIT INFO

DAEMON=/usr/local/bin/simple-watchdog-feeder.sh
PIDFILE=/var/run/simple-watchdog.pid

case "$1" in
    start)
        echo "Starting simple watchdog feeder..."
        start-stop-daemon --start --background --make-pidfile \
            --pidfile $PIDFILE --exec $DAEMON
        ;;
    stop)
        echo "Stopping simple watchdog feeder..."
        start-stop-daemon --stop --pidfile $PIDFILE
        echo V > /dev/watchdog
        ;;
    restart)
        $0 stop
        sleep 1
        $0 start
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
        ;;
esac

exit 0

# Save and exit
```

```bash
# Make executable
sudo chmod +x $ROOTFS/etc/init.d/simple-watchdog

# Create symbolic links for runlevels (enable at boot)
sudo ln -sf ../init.d/simple-watchdog $ROOTFS/etc/rc3.d/S01simple-watchdog
sudo ln -sf ../init.d/simple-watchdog $ROOTFS/etc/rc5.d/S01simple-watchdog
```

---

## PART 5: CONFIGURE GRAPHICAL CAPABILITIES

### Step 5.1: Keep Framebuffer and Input Drivers

```bash
# Verify framebuffer device will be available
# The kernel modules should load automatically

# Check that input libraries are present
ls -l $ROOTFS/usr/lib/libinput*
ls -l $ROOTFS/usr/lib/libevdev*

# These should exist for touchscreen support
```

### Step 5.2: Keep Minimal Graphics Libraries

```bash
# Keep these for graphical applications:
# - /usr/lib/libEGL*
# - /usr/lib/libGLES*
# - /usr/lib/libgbm*
# - /usr/lib/libdrm*

# Verify they exist
ls -l $ROOTFS/usr/lib/libEGL*
ls -l $ROOTFS/usr/lib/libGLES*
```

---

## PART 6: REGENERATE MD5 CHECKSUMS

**CRITICAL STEP:** After modifying any files, you must regenerate MD5 checksums to prevent boot failures.

```bash
# Find all .md5 files in the root filesystem
find $ROOTFS -name "*.md5" -type f

# For each directory containing .md5 files, regenerate checksums
# Example for /etc directory:
cd $ROOTFS/etc
sudo md5sum * 2>/dev/null | sudo tee checksums.md5

# If there are specific .md5 files, update them:
# Example:
cd $ROOTFS/etc/init.d
if [ -f checksums.md5 ]; then
    sudo md5sum * 2>/dev/null | sudo tee checksums.md5
fi

# Repeat for other directories with .md5 files
```

---

## PART 7: FINAL VERIFICATION

### Step 7.1: Verify Critical Files

```bash
# Check passwd file
cat $ROOTFS/etc/passwd | grep -E "^root:|^rwuser:"

# Check shadow file (verify hashes are present, not *)
sudo cat $ROOTFS/etc/shadow | grep -E "^root:|^rwuser:"

# Check group file
cat $ROOTFS/etc/group | grep sudo

# Check inittab
cat $ROOTFS/etc/inittab | grep initdefault

# Check network config
cat $ROOTFS/etc/network/interfaces

# Check watchdog feeder exists
ls -l $ROOTFS/usr/local/bin/simple-watchdog-feeder.sh
ls -l $ROOTFS/etc/init.d/simple-watchdog
```

### Step 7.2: Verify Disabled Services

```bash
# These should be renamed/disabled:
ls -l $ROOTFS/etc/init.d/*.disabled

# Should show:
# x11.disabled
# browser.disabled
# webserver.disabled
```

### Step 7.3: Verify Enabled Services

```bash
# Check SSH is enabled
ls -l $ROOTFS/etc/init.d/sshd

# Check networking is enabled
ls -l $ROOTFS/etc/init.d/networking
```

---

## PART 8: UNMOUNT AND TEST

### Step 8.1: Safely Unmount

```bash
# Sync all changes to disk
sync

# Unmount all partitions
sudo umount $BOOT
sudo umount $ROOTFS
sudo umount $DATA

# Verify unmounted
df -h | grep /media/pi
# Should show nothing
```

### Step 8.2: Insert SD Card and Boot

1. Insert SD card into RoomWizard device
2. Power on the device
3. Wait 30-60 seconds for boot
4. Connect via serial console (115200 baud on ttyO1) or SSH

### Step 8.3: First Boot Login

**Via Serial Console:**
```
RoomWizard login: root
Password: roomwizard

# Or login as regular user:
RoomWizard login: rwuser
Password: userpassword
```

**Via SSH (after getting IP from DHCP):**
```bash
# Find device IP (check your router or use nmap)
ssh root@192.168.1.xxx
# Or
ssh rwuser@192.168.1.xxx
```

---

## PART 9: POST-BOOT VERIFICATION

### Step 9.1: Verify System Status

```bash
# Check runlevel
runlevel
# Should show: N 3

# Check watchdog is running
ps aux | grep watchdog
# Should show simple-watchdog-feeder.sh

# Check network
ip addr show eth0
# Should show IP address from DHCP

# Check SSH is running
ps aux | grep sshd
# Should show sshd daemon

# Test sudo (as rwuser)
sudo whoami
# Should return: root
```

### Step 9.2: Verify Graphical Capabilities

```bash
# Check framebuffer device
ls -l /dev/fb0
# Should exist

# Check input devices
ls -l /dev/input/event*
# Should show touchscreen and other input devices

# Check graphics libraries
ldconfig -p | grep -E "EGL|GLES"
# Should show OpenGL ES libraries
```

---

## SUMMARY OF MODIFIED FILES

### User Access:
- `$ROOTFS/etc/shadow` - Root and user password hashes
- `$ROOTFS/etc/passwd` - Added rwuser account
- `$ROOTFS/etc/group` - Added rwuser to sudo and other groups
- `$ROOTFS/etc/sudoers` - Sudo configuration
- `$ROOTFS/home/rwuser/` - User home directory

### System Configuration:
- `$ROOTFS/etc/inittab` - Changed to runlevel 3
- `$ROOTFS/etc/default/watchdog` - Disabled heavy watchdog
- `$ROOTFS/etc/init.d/x11.disabled` - Disabled X11
- `$ROOTFS/etc/init.d/browser.disabled` - Disabled browser
- `$ROOTFS/etc/init.d/webserver.disabled` - Disabled Jetty

### Watchdog:
- `$ROOTFS/usr/local/bin/simple-watchdog-feeder.sh` - Lightweight feeder
- `$ROOTFS/etc/init.d/simple-watchdog` - Init script
- `$ROOTFS/etc/rc3.d/S01simple-watchdog` - Runlevel 3 link
- `$ROOTFS/etc/rc5.d/S01simple-watchdog` - Runlevel 5 link

### Network:
- `$ROOTFS/etc/network/interfaces` - DHCP configuration
- `$ROOTFS/etc/ssh/sshd_config` - SSH server config

---

## EXPECTED RESULTS

### Boot Time:
- **Before:** 60-90 seconds
- **After:** 15-25 seconds

### Memory Usage:
- **Before:** ~180-250 MB
- **After:** ~30-50 MB

### Running Services:
- init (PID 1)
- udevd (device management)
- syslogd (logging)
- simple-watchdog-feeder (watchdog only)
- sshd (remote access)
- dhclient (network)
- getty (serial console)

### Available for Your Application:
- Framebuffer (`/dev/fb0`) for graphics
- Input events (`/dev/input/eventX`) for touchscreen
- OpenGL ES libraries for hardware acceleration
- ~200 MB free RAM
- Full CPU available
- Network connectivity
- Remote SSH access

---

## TROUBLESHOOTING

### If System Won't Boot:
1. Boot with serial console connected
2. Watch for error messages
3. May need to manually feed watchdog: `echo 1 > /dev/watchdog` in a loop

### If Can't Login:
1. Check password hash was correctly copied
2. Verify shadow file syntax (no extra spaces)
3. Try root login first, then create user from root shell

### If No Network:
1. Check cable connection
2. Verify DHCP server is running on network
3. Check `/etc/network/interfaces` syntax
4. Try manual IP: `ifconfig eth0 192.168.1.100 netmask 255.255.255.0`

### If SSH Won't Connect:
1. Verify sshd is running: `ps aux | grep sshd`
2. Check sshd config: `/etc/ssh/sshd_config`
3. Try restarting: `/etc/init.d/sshd restart`

---

## NEXT STEPS

After successful boot, you can:
1. Install your graphical application
2. Configure it to run at boot
3. Use framebuffer for display output
4. Access touchscreen via `/dev/input/event*`
5. Control LEDs via `/sys/class/leds/`
6. Develop and test remotely via SSH

Your minimal system is now ready for custom graphical applications!