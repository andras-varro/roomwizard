# RoomWizard System Setup Guide

> **Complete guide for SSH access, root configuration, and minimal system setup**

## Table of Contents

1. [Overview](#overview)
2. [SSH Root Access Setup](#ssh-root-access-setup)
3. [Minimal CLI System Setup](#minimal-cli-system-setup)
4. [Network Configuration](#network-configuration)
5. [Security Considerations](#security-considerations)
6. [Troubleshooting](#troubleshooting)

---

## Current Device Configuration

**Device IP:** 192.168.50.73
**SSH Access:** Configured with key authentication
**User:** root
**Status:** Development device

This IP is referenced throughout the project documentation. If your device has a different IP, update your deployment scripts accordingly.

---

## Overview

This guide covers two main system setup scenarios:
1. **SSH Root Access:** Enabling SSH access with root login for development and deployment
2. **Minimal CLI System:** Stripping down the system to a minimal CLI configuration for custom applications

Both approaches require modifying the SD card partitions while the card is mounted on a Linux system.

---

## SSH Root Access Setup

### Overview

By default, the RoomWizard may have SSH disabled or root login restricted. This guide shows how to enable SSH with root access for development purposes.

### Prerequisites

- RoomWizard SD card
- Linux machine (or WSL2 on Windows)
- Root/sudo access on your Linux machine

### Method 1: Enable Root SSH (Simplest)

#### Step 1: Mount the Root Filesystem

```bash
# Insert SD card and identify partitions
lsblk

# Mount the root filesystem (usually partition 5 or 6)
sudo mkdir -p /mnt/roomwizard
sudo mount /dev/sdb6 /mnt/roomwizard  # Adjust device name as needed
```

#### Step 2: Modify SSH Configuration

```bash
# Edit SSH daemon configuration
sudo nano /mnt/roomwizard/etc/ssh/sshd_config

# Ensure these settings are present:
PermitRootLogin yes
PasswordAuthentication yes
PubkeyAuthentication yes

# Save and exit (Ctrl+X, Y, Enter)
```

#### Step 3: Set Root Password

```bash
# Generate password hash
openssl passwd -6 "your_password_here"
# Copy the output hash

# Edit shadow file
sudo nano /mnt/roomwizard/etc/shadow

# Find the line starting with "root:" (looks like):
# root:*:10933:0:99999:7:::

# Replace the * with your generated hash:
# root:$6$xyz...your.hash.here...:10933:0:99999:7:::

# Save and exit
```

#### Step 4: Verify SSH Service is Enabled

```bash
# Check if SSH init script exists
ls -l /mnt/roomwizard/etc/init.d/sshd

# Check if it's enabled in runlevels
ls -l /mnt/roomwizard/etc/rc*.d/*ssh*

# If not enabled, create symlinks:
sudo ln -s ../init.d/sshd /mnt/roomwizard/etc/rc3.d/S20sshd
sudo ln -s ../init.d/sshd /mnt/roomwizard/etc/rc5.d/S20sshd
```

#### Step 5: Unmount and Test

```bash
# Sync and unmount
sync
sudo umount /mnt/roomwizard

# Insert SD card into RoomWizard and boot
# Wait for system to start (30-60 seconds)

# Find device IP (check your router or use nmap)
# Then SSH in:
ssh root@<roomwizard-ip>
# Password: your_password_here
```

### Method 2: Add SSH Keys (More Secure)

#### Step 1: Generate SSH Key Pair (on your dev machine)

```bash
# Generate key if you don't have one
ssh-keygen -t rsa -b 4096 -C "roomwizard-access"

# This creates:
# ~/.ssh/id_rsa (private key)
# ~/.ssh/id_rsa.pub (public key)
```

#### Step 2: Add Public Key to RoomWizard

```bash
# Mount root filesystem
sudo mount /dev/sdb6 /mnt/roomwizard

# Create .ssh directory for root
sudo mkdir -p /mnt/roomwizard/root/.ssh
sudo chmod 700 /mnt/roomwizard/root/.ssh

# Copy your public key
sudo cp ~/.ssh/id_rsa.pub /mnt/roomwizard/root/.ssh/authorized_keys
sudo chmod 600 /mnt/roomwizard/root/.ssh/authorized_keys

# Unmount
sync
sudo umount /mnt/roomwizard
```

#### Step 3: Connect with SSH Key

```bash
# SSH without password
ssh root@<roomwizard-ip>
```

### Quick Reference

```bash
# SSH with password
ssh root@<roomwizard-ip>

# SSH with key
ssh -i ~/.ssh/id_rsa root@<roomwizard-ip>

# SCP files to device
scp file.txt root@<roomwizard-ip>:/opt/

# SCP files from device
scp root@<roomwizard-ip>:/var/log/messages ./
```

---

## Minimal CLI System Setup

### Overview

This section explains how to strip down the RoomWizard to a minimal CLI system, removing the browser/X11 interface and heavy services. This is ideal for custom applications that don't need the full GUI stack.

### What Gets Removed

- ❌ **X11/Xorg** - Display server
- ❌ **Browser** - WebKit/Epiphany
- ❌ **Jetty** - Web server
- ❌ **Java** - Runtime (optional)

### What Remains

- ✅ **Networking** - DHCP and SSH
- ✅ **Framebuffer** - Direct graphics access
- ✅ **Touch Input** - Linux input events
- ✅ **LED Control** - Sysfs access
- ✅ **Watchdog** - Lightweight feeder

### Prerequisites

- RoomWizard SD card
- Linux machine with root access
- Backup of original SD card image

### Step-by-Step Process

#### Step 1: Identify Mounted Partitions

```bash
# List all mounted partitions
df -h | grep /media

# You should see partitions like:
# /media/user/XXXX-XXXX          <- Boot partition (FAT32)
# /media/user/108a1490-...       <- Root filesystem (ext3)
# /media/user/d5758df8-...       <- Data partition (ext3)

# Set variables for easier access
BOOT="/media/user/XXXX-XXXX"
ROOTFS="/media/user/108a1490-8feb-4d0c-b3db-995dc5fc066c"
DATA="/media/user/d5758df8-c160-4364-bac2-b68fcabeef30"
```

#### Step 2: Configure User Access

**Enable Root Login:**
```bash
# Backup shadow file
sudo cp $ROOTFS/etc/shadow $ROOTFS/etc/shadow.backup

# Generate password hash
openssl passwd -6 "roomwizard"
# Copy the output hash

# Edit shadow file
sudo nano $ROOTFS/etc/shadow

# Find root line and replace * with hash:
# root:$6$xyz...your.hash.here...:10933:0:99999:7:::
```

**Create Regular User with Sudo:**
```bash
# Add user to passwd
sudo nano $ROOTFS/etc/passwd
# Add line:
rwuser:x:1000:1000:RoomWizard User:/home/rwuser:/bin/bash

# Add user to shadow
openssl passwd -6 "userpassword"
sudo nano $ROOTFS/etc/shadow
# Add line:
rwuser:$6$xyz...your.hash.here...:19000:0:99999:7:::

# Add user to sudo group
sudo nano $ROOTFS/etc/group
# Find sudo line and add user:
sudo:x:27:rwuser

# Create home directory
sudo mkdir -p $ROOTFS/home/rwuser
sudo chown -R 1000:1000 $ROOTFS/home/rwuser
sudo chmod 755 $ROOTFS/home/rwuser
```

#### Step 3: Change Boot to CLI (Runlevel 3)

```bash
# Edit inittab
sudo nano $ROOTFS/etc/inittab

# Find line 5:
# id:5:initdefault:

# Change to runlevel 3 (CLI only):
# id:3:initdefault:

# Save and exit
```

#### Step 4: Disable Heavy Services

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
# Change:
# run_watchdog=0
```

#### Step 5: Create Simple Watchdog Feeder

```bash
# Create the script
sudo nano $ROOTFS/usr/local/bin/simple-watchdog-feeder.sh
```

Add this content:
```bash
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
```

```bash
# Make executable
sudo chmod +x $ROOTFS/usr/local/bin/simple-watchdog-feeder.sh

# Create init script
sudo nano $ROOTFS/etc/init.d/simple-watchdog
```

Add this content:
```bash
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
```

```bash
# Make executable
sudo chmod +x $ROOTFS/etc/init.d/simple-watchdog

# Enable at boot
sudo ln -sf ../init.d/simple-watchdog $ROOTFS/etc/rc3.d/S01simple-watchdog
```

#### Step 6: Configure Network for DHCP

```bash
# Edit network interfaces
sudo nano $ROOTFS/etc/network/interfaces
```

Ensure it contains:
```
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet dhcp
```

#### Step 7: Verify Critical Files

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

#### Step 8: Unmount and Test

```bash
# Sync all changes to disk
sync

# Unmount all partitions
sudo umount $BOOT
sudo umount $ROOTFS
sudo umount $DATA

# Verify unmounted
df -h | grep /media
# Should show nothing
```

### Post-Boot Verification

After booting the device:

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

### Expected Results

#### Boot Time
- **Before:** 60-90 seconds
- **After:** 15-25 seconds

#### Memory Usage
- **Before:** ~180-250 MB
- **After:** ~30-50 MB

#### Running Services
- init (PID 1)
- udevd (device management)
- syslogd (logging)
- simple-watchdog-feeder (watchdog only)
- sshd (remote access)
- dhclient (network)
- getty (serial console)

#### Available for Your Application
- Framebuffer (`/dev/fb0`) for graphics
- Input events (`/dev/input/eventX`) for touchscreen
- OpenGL ES libraries for hardware acceleration
- ~200 MB free RAM
- Full CPU available
- Network connectivity
- Remote SSH access

---

## Network Configuration

### DHCP Configuration (Default)

```bash
# Edit /etc/network/interfaces
auto eth0
iface eth0 inet dhcp
```

### Static IP Configuration

```bash
# Edit /etc/network/interfaces
auto eth0
iface eth0 inet static
    address 192.168.1.100
    netmask 255.255.255.0
    gateway 192.168.1.1
    dns-nameservers 8.8.8.8 8.8.4.4
```

### Network Troubleshooting

```bash
# Check interface status
ip addr show eth0

# Check routing
ip route

# Test connectivity
ping 8.8.8.8

# Restart networking
/etc/init.d/networking restart

# Manual IP assignment
ifconfig eth0 192.168.1.100 netmask 255.255.255.0
route add default gw 192.168.1.1
```

---

## Security Considerations

### SSH Security

**Recommended Settings:**
```bash
# In /etc/ssh/sshd_config:
PermitRootLogin yes              # For development only
PasswordAuthentication yes       # Disable after setting up keys
PubkeyAuthentication yes         # Preferred method
PermitEmptyPasswords no          # Always disabled
```

**Production Recommendations:**
1. Use SSH keys instead of passwords
2. Disable root login after setup
3. Use a non-root user with sudo
4. Consider changing SSH port
5. Use fail2ban for brute-force protection

### Firewall Configuration

```bash
# Basic iptables rules
iptables -A INPUT -i lo -j ACCEPT
iptables -A INPUT -m state --state ESTABLISHED,RELATED -j ACCEPT
iptables -A INPUT -p tcp --dport 22 -j ACCEPT
iptables -A INPUT -j DROP

# Save rules
iptables-save > /etc/iptables.rules
```

### Password Policy

- Use strong passwords (12+ characters)
- Mix uppercase, lowercase, numbers, symbols
- Don't reuse passwords
- Consider using a password manager

---

## Troubleshooting

### Can't SSH to Device

**Check 1: Is SSH running?**
```bash
ps aux | grep sshd
```

**Check 2: Is network configured?**
```bash
ip addr show eth0
ping <roomwizard-ip>
```

**Check 3: Firewall blocking?**
```bash
iptables -L -n
```

**Check 4: SSH configuration**
```bash
cat /etc/ssh/sshd_config | grep -E "PermitRoot|PasswordAuth"
```

### System Won't Boot After Modifications

**Cause:** MD5 checksum mismatch or watchdog timeout

**Solution:**
1. Boot with serial console connected
2. Watch for error messages
3. May need to manually feed watchdog: `echo 1 > /dev/watchdog` in a loop
4. Restore from backup if necessary

### Can't Login After Password Change

**Check 1: Verify password hash**
```bash
# Hash should start with $6$ for SHA-512
sudo cat /mnt/roomwizard/etc/shadow | grep root
```

**Check 2: No extra spaces**
```bash
# Ensure no trailing spaces in shadow file
```

**Check 3: Try root login first**
```bash
# If root works, create user from root shell
```

### No Network After Boot

**Check 1: Cable connection**
```bash
# Verify Ethernet cable is connected
# Check link light on port
```

**Check 2: DHCP server**
```bash
# Verify DHCP server is running on network
# Check router DHCP settings
```

**Check 3: Interface configuration**
```bash
# Check /etc/network/interfaces syntax
cat /etc/network/interfaces
```

**Manual Fix:**
```bash
ifconfig eth0 192.168.1.100 netmask 255.255.255.0
route add default gw 192.168.1.1
```

---

## Related Documentation

- [System Analysis](SYSTEM_ANALYSIS.md) - Hardware and firmware analysis
- [Native Games Guide](native_games/README.md) - Native C games development
- [ScummVM Backend](scummvm-roomwizard/README.md) - Classic adventure games port
- [Project Status](README.md) - Project overview and status

---

