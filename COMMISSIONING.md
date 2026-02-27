# RoomWizard Commissioning Script

This document describes the automated commissioning script for RoomWizard devices.

## Overview

The [`commission-roomwizard.sh`](commission-roomwizard.sh) script automates the initial setup of a RoomWizard device after removing the SD card from the box. It handles:

- Root password configuration
- SSH access setup
- Optional SSH key installation
- DHCP network configuration

This script replaces the manual steps described in sections 2-3 of [SYSTEM_SETUP.md](SYSTEM_SETUP.md).

## Prerequisites

- Linux machine (or WSL on Windows)
- RoomWizard SD card inserted and partitions auto-mounted
- `openssl` command available (usually pre-installed)
- `sudo` access

## Usage

1. **Insert the SD card** into your Linux machine. Modern desktops will auto-mount the partitions.

2. **Make the script executable** (first time only):
   ```bash
   chmod +x commission-roomwizard.sh
   ```

3. **Run the script**:
   ```bash
   ./commission-roomwizard.sh
   ```

4. **Follow the prompts**:
   - Enter and confirm the desired root password
   - Choose whether to set up SSH key authentication
   - If yes, provide the path to your SSH public key (default: `~/.ssh/id_rsa.pub`)

## What the Script Does

### 1. Locates the rootfs partition
Automatically finds the mounted rootfs partition (p6) using its UUID.

### 2. Sets root password
- Prompts for password (with confirmation)
- Generates a secure SHA-512 hash using `openssl passwd -6`
- Updates `/etc/shadow` with the new password hash
- Creates a backup of the original file

### 3. Configures SSH
- Updates `/etc/ssh/sshd_config` to allow root login
- Enables both password and public key authentication
- Verifies sshd is enabled at boot (creates symlink if needed)
- Creates a backup of the original config

### 4. Sets up SSH keys (optional)
- Creates `/home/root/.ssh/` directory with proper permissions (700)
- Copies your public key to `authorized_keys`
- Sets proper file permissions (600)

### 5. Enables DHCP
- Updates `/etc/network/interfaces` to configure eth0 for DHCP
- Removes any static IP configuration
- Creates a backup of the original file

### 6. Provides next steps
Displays instructions for unmounting, booting, and connecting to the device.

## After Commissioning

Once the script completes:

1. **Sync and unmount** the SD card:
   ```bash
   sync
   sudo umount /path/to/rootfs
   ```

2. **Insert the SD card** back into the RoomWizard device

3. **Connect Ethernet** and power on

4. **Wait ~30 seconds** for boot

5. **Find the device IP** from your router's DHCP leases

6. **SSH into the device**:
   ```bash
   ssh root@<device-ip>
   ```

7. **Deploy games and ScummVM**:
   ```bash
   cd native_games
   ./build-and-deploy.sh <device-ip> permanent
   ```

## Troubleshooting

### "Could not find mounted rootfs partition"
The SD card partitions may not be auto-mounted. Mount manually:
```bash
sudo mkdir -p /mnt/rw
sudo mount /dev/sdX6 /mnt/rw  # Replace X with your device letter
export ROOTFS=/mnt/rw
```

Then run the script again.

### "Permission denied" errors
Make sure you have sudo access and the script is executable:
```bash
chmod +x commission-roomwizard.sh
```

### SSH key not working after setup
Verify the key was copied correctly:
```bash
sudo cat /path/to/rootfs/home/root/.ssh/authorized_keys
```

Check permissions:
```bash
ls -la /path/to/rootfs/home/root/.ssh/
# Should show: drwx------ for .ssh/ and -rw------- for authorized_keys
```

## Backups

The script automatically creates backups of modified files:
- `/etc/shadow.backup`
- `/etc/ssh/sshd_config.backup`
- `/etc/network/interfaces.backup`

These backups are stored on the SD card and can be used to revert changes if needed.

## Related Documentation

- [SYSTEM_SETUP.md](SYSTEM_SETUP.md) - Complete system setup guide
- [native_games/README.md](native_games/README.md) - Game deployment instructions
- [native_games/build-and-deploy.sh](native_games/build-and-deploy.sh) - Build and deployment script
