# RoomWizard SSH Root Access Setup Guide

## Overview

This guide explains how to configure the RoomWizard device for SSH root access before first boot. The device has no keyboard, so all configuration must be done by editing files on the SD card.

---

## Prerequisites

- SD card mounted at `/media/pi/[partition-guid]`
- Linux machine with root/sudo access
- Router with DHCP server
- Ethernet cable

---

## Step 1: Identify Mounted Partitions

```bash
# List mounted partitions
df -h | grep /media/pi

# Set variables (adjust GUIDs to match your mounts)
ROOTFS="/media/pi/108a1490-8feb-4d0c-b3db-995dc5fc066c"

# Verify mount
ls -l $ROOTFS/etc/passwd
```

---

## Step 2: Configure DHCP Networking

### Edit: `$ROOTFS/etc/network/interfaces`

```bash
# Backup original
sudo cp $ROOTFS/etc/network/interfaces $ROOTFS/etc/network/interfaces.backup

# Edit file
sudo nano $ROOTFS/etc/network/interfaces
```

**Content:**
```
# Loopback interface
auto lo
iface lo inet loopback

# Ethernet with DHCP
auto eth0
iface eth0 inet dhcp
```

Save and exit (Ctrl+X, Y, Enter)

---

## Step 3: Configure SSH Server

### Edit: `$ROOTFS/etc/ssh/sshd_config`

```bash
# Backup original
sudo cp $ROOTFS/etc/ssh/sshd_config $ROOTFS/etc/ssh/sshd_config.backup

# Edit file
sudo nano $ROOTFS/etc/ssh/sshd_config
```

**Add or modify these lines:**
```
PermitRootLogin yes
PasswordAuthentication yes
PubkeyAuthentication yes
AuthorizedKeysFile .ssh/authorized_keys
Port 22
```

Save and exit

---

## Step 4: Set Root Password

### Edit: `$ROOTFS/etc/shadow`

```bash
# Backup original
sudo cp $ROOTFS/etc/shadow $ROOTFS/etc/shadow.backup

# Generate password hash
openssl passwd -6 "roomwizard"
# Copy the output (starts with $6$...)
# change roomwizard to your desired password

# Edit shadow file
sudo nano $ROOTFS/etc/shadow
```

**Find the root line:**
```
root:*:10933:0:99999:7:::
```

**Replace `*` with your hash:**
```
root:$6$xyz...your.hash.here...:10933:0:99999:7:::
```

Save and exit

---

## Step 5: (Optional) Add SSH Key

```bash
# Generate key on your computer (if needed)
ssh-keygen -t ed25519 -C "roomwizard"

# View public key
cat ~/.ssh/id_ed25519.pub
# Copy entire line

# Create .ssh directory on SD card
sudo mkdir -p $ROOTFS/home/root/.ssh
sudo chmod 700 $ROOTFS/home/root/.ssh

# Create authorized_keys
sudo nano $ROOTFS/home/root/.ssh/authorized_keys
# Paste your public key

# Set permissions
sudo chmod 600 $ROOTFS/home/root/.ssh/authorized_keys
sudo chown -R 0:0 $ROOTFS/home/root/.ssh
```

---

## Step 6: Verify Configuration

```bash
# Check network config
cat $ROOTFS/etc/network/interfaces

# Check SSH config
grep "PermitRootLogin" $ROOTFS/etc/ssh/sshd_config

# Check root password
sudo grep "^root:" $ROOTFS/etc/shadow
# Should show hash, not *

# Check SSH key (if added)
ls -l $ROOTFS/home/root/.ssh/authorized_keys
```

---

## Step 7: Unmount and Boot

```bash
# Sync changes
sync

# Unmount
sudo umount $ROOTFS

# Insert SD card into RoomWizard
# Connect Ethernet cable to router
# Power on device
# Wait 60-90 seconds for boot
```

---

## Step 8: Find Device IP

### Method 1: Router Admin Panel
- Log into router web interface
- Check DHCP client list
- Find "RoomWizard" or new device
- Note IP address

### Method 2: Network Scan
```bash
# Scan for SSH servers
sudo nmap -p 22 --open 192.168.1.0/24
```

### Method 3: ARP Table
```bash
arp -a | grep 192.168.1
```

---

## Step 9: Connect via SSH

```bash
# With password
ssh root@192.168.1.xxx

# With SSH key
ssh -i ~/.ssh/id_ed25519 root@192.168.1.xxx
```

**First connection:**
```
The authenticity of host '...' can't be established.
Are you sure you want to continue connecting (yes/no)?
```
Type `yes` and press Enter

---

## Step 10: Verify Access

```bash
# Check you're root
whoami
# Output: root

# Check network
ip addr show eth0

# Check system
df -h
free -h
uname -a
```

---

## Troubleshooting

### Can't Find IP Address
- Wait 2 minutes for full boot
- Check router DHCP client list
- Use serial console (ttyO1, 115200 baud)
- Set static IP in interfaces file

### SSH Connection Refused
- Verify device is powered on
- Check Ethernet cable connection
- Wait longer for boot (up to 3 minutes)
- Verify SSH daemon: connect serial console, run `ps aux | grep sshd`

### Authentication Failed
- Verify password hash copied correctly (no extra spaces)
- Regenerate hash and try again
- Check shadow file syntax
- Try SSH key if password fails

### Network Not Working
- Verify Ethernet cable is connected
- Check router DHCP is enabled
- Try static IP configuration
- Connect serial console to debug

---

## Quick Reference

### Files Modified
- `$ROOTFS/etc/network/interfaces` - DHCP config
- `$ROOTFS/etc/ssh/sshd_config` - SSH settings  
- `$ROOTFS/etc/shadow` - Root password
- `$ROOTFS/home/root/.ssh/authorized_keys` - SSH key (optional)

### Connection
```bash
ssh root@<device-ip>
```

### Password Generation
```bash
openssl passwd -6 "your_password"
```

---

## Next Steps After Connection

1. **Change root password** (if needed):
   ```bash
   passwd
   ```

2. **Update system** (if package manager available):
   ```bash
   opkg update
   ```

3. **Check development tools**:
   ```bash
   which gcc make
   ```

4. **Test framebuffer**:
   ```bash
   ls -l /dev/fb0
   ```

5. **Check input devices**:
   ```bash
   ls -l /dev/input/event*
   ```

You now have full root SSH access to your RoomWizard device!
