#!/bin/bash
#
# RoomWizard SD Card Commissioning Script
#
# This script automates the initial setup of a RoomWizard device after
# removing the SD card from the box. It configures root access, SSH,
# and DHCP networking.
#
# Usage: ./commission-roomwizard.sh
#
# Prerequisites:
# - SD card must be inserted into a Linux machine
# - Partitions should be auto-mounted (or manually mounted)
# - openssl command must be available
#

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored messages
info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Banner
echo "================================================"
echo "  RoomWizard SD Card Commissioning Script"
echo "================================================"
echo ""

# Step 1: Locate the rootfs partition (p6)
info "Locating rootfs partition (UUID: 108a1490-8feb-4d0c-b3db-995dc5fc066c)..."

ROOTFS=$(findmnt -rno TARGET --source "$(blkid -U 108a1490-8feb-4d0c-b3db-995dc5fc066c 2>/dev/null)" 2>/dev/null || echo "")

if [ -z "$ROOTFS" ]; then
    error "Could not find mounted rootfs partition (p6)."
    echo ""
    echo "Please ensure the SD card is inserted and partitions are mounted."
    echo "You can check with: lsblk -o NAME,UUID,FSTYPE,SIZE,MOUNTPOINT | grep -v loop"
    echo ""
    echo "If needed, mount manually:"
    echo "  sudo mkdir -p /mnt/rw"
    echo "  sudo mount /dev/sdX6 /mnt/rw"
    echo "  export ROOTFS=/mnt/rw"
    exit 1
fi

success "Found rootfs at: $ROOTFS"
echo ""

# Verify it looks like a valid rootfs
if [ ! -d "$ROOTFS/etc" ] || [ ! -d "$ROOTFS/root" ]; then
    error "The mounted partition at $ROOTFS doesn't look like a valid rootfs."
    error "Expected to find /etc and /root directories."
    exit 1
fi

# Step 2: Get root password
echo "================================================"
echo "  Root Password Configuration"
echo "================================================"
echo ""

while true; do
    read -s -p "Enter desired root password: " PASSWORD
    echo ""
    read -s -p "Confirm root password: " PASSWORD_CONFIRM
    echo ""
    
    if [ "$PASSWORD" = "$PASSWORD_CONFIRM" ]; then
        if [ -z "$PASSWORD" ]; then
            error "Password cannot be empty. Please try again."
            echo ""
        else
            break
        fi
    else
        error "Passwords do not match. Please try again."
        echo ""
    fi
done

info "Generating password hash..."
PASSWORD_HASH=$(openssl passwd -6 "$PASSWORD")
success "Password hash generated."
echo ""

# Step 3: Update /etc/shadow
info "Updating /etc/shadow with new root password..."

SHADOW_FILE="$ROOTFS/etc/shadow"
if [ ! -f "$SHADOW_FILE" ]; then
    error "Could not find $SHADOW_FILE"
    exit 1
fi

# Backup original shadow file
sudo cp "$SHADOW_FILE" "$SHADOW_FILE.backup"
info "Created backup: $SHADOW_FILE.backup"

# Replace root password in shadow file
sudo sed -i "s|^root:[^:]*:|root:$PASSWORD_HASH:|" "$SHADOW_FILE"
success "Root password updated in /etc/shadow"
echo ""

# Step 4: Configure SSH
echo "================================================"
echo "  SSH Configuration"
echo "================================================"
echo ""

SSHD_CONFIG="$ROOTFS/etc/ssh/sshd_config"
if [ ! -f "$SSHD_CONFIG" ]; then
    error "Could not find $SSHD_CONFIG"
    exit 1
fi

info "Configuring SSH to allow root login..."

# Backup original sshd_config
sudo cp "$SSHD_CONFIG" "$SSHD_CONFIG.backup"
info "Created backup: $SSHD_CONFIG.backup"

# Update SSH configuration
sudo sed -i 's/^#*PermitRootLogin.*/PermitRootLogin yes/' "$SSHD_CONFIG"
sudo sed -i 's/^#*PasswordAuthentication.*/PasswordAuthentication yes/' "$SSHD_CONFIG"
sudo sed -i 's/^#*PubkeyAuthentication.*/PubkeyAuthentication yes/' "$SSHD_CONFIG"

# Add settings if they don't exist
if ! grep -q "^PermitRootLogin" "$SSHD_CONFIG"; then
    echo "PermitRootLogin yes" | sudo tee -a "$SSHD_CONFIG" > /dev/null
fi
if ! grep -q "^PasswordAuthentication" "$SSHD_CONFIG"; then
    echo "PasswordAuthentication yes" | sudo tee -a "$SSHD_CONFIG" > /dev/null
fi
if ! grep -q "^PubkeyAuthentication" "$SSHD_CONFIG"; then
    echo "PubkeyAuthentication yes" | sudo tee -a "$SSHD_CONFIG" > /dev/null
fi

success "SSH configuration updated."
echo ""

# Step 5: Verify sshd starts at boot
info "Verifying sshd is enabled at boot..."
if ls "$ROOTFS/etc/rc5.d/" 2>/dev/null | grep -q ssh; then
    success "sshd is already configured to start at boot."
else
    warning "sshd not found in rc5.d, creating symlink..."
    sudo ln -sf ../init.d/sshd "$ROOTFS/etc/rc5.d/S09sshd"
    success "sshd enabled at boot."
fi
echo ""

# Step 6: SSH Key Setup (optional)
echo "================================================"
echo "  SSH Key Setup (Optional)"
echo "================================================"
echo ""

read -p "Do you want to set up SSH key authentication? (y/n): " SETUP_SSH_KEYS

if [[ "$SETUP_SSH_KEYS" =~ ^[Yy]$ ]]; then
    # Check for default SSH public key
    if [ -f "$HOME/.ssh/id_rsa.pub" ]; then
        DEFAULT_KEY="$HOME/.ssh/id_rsa.pub"
        info "Found SSH public key: $DEFAULT_KEY"
        read -p "Use this key? (y/n): " USE_DEFAULT
        
        if [[ "$USE_DEFAULT" =~ ^[Yy]$ ]]; then
            SSH_KEY_PATH="$DEFAULT_KEY"
        else
            read -p "Enter path to your SSH public key: " SSH_KEY_PATH
        fi
    else
        read -p "Enter path to your SSH public key (e.g., ~/.ssh/id_rsa.pub): " SSH_KEY_PATH
        SSH_KEY_PATH="${SSH_KEY_PATH/#\~/$HOME}"  # Expand ~ to home directory
    fi
    
    if [ ! -f "$SSH_KEY_PATH" ]; then
        error "SSH key file not found: $SSH_KEY_PATH"
        warning "Skipping SSH key setup. You can add it manually later."
    else
        info "Setting up SSH key authentication..."
        
        # Create .ssh directory
        sudo mkdir -p "$ROOTFS/root/.ssh"
        sudo chmod 700 "$ROOTFS/root/.ssh"
        
        # Copy public key
        cat "$SSH_KEY_PATH" | sudo tee "$ROOTFS/root/.ssh/authorized_keys" > /dev/null
        sudo chmod 600 "$ROOTFS/root/.ssh/authorized_keys"
        
        success "SSH key installed successfully."
        info "You will be able to SSH without a password using your key."
    fi
else
    info "Skipping SSH key setup."
    info "You will need to use password authentication to SSH."
fi
echo ""

# Step 7: Enable DHCP
echo "================================================"
echo "  Network Configuration (DHCP)"
echo "================================================"
echo ""

INTERFACES_FILE="$ROOTFS/etc/network/interfaces"
if [ ! -f "$INTERFACES_FILE" ]; then
    error "Could not find $INTERFACES_FILE"
    exit 1
fi

info "Configuring eth0 to use DHCP..."

# Backup original interfaces file
sudo cp "$INTERFACES_FILE" "$INTERFACES_FILE.backup"
info "Created backup: $INTERFACES_FILE.backup"

# Check if eth0 is already configured for DHCP
if grep -A 1 "^auto eth0" "$INTERFACES_FILE" | grep -q "iface eth0 inet dhcp"; then
    success "eth0 is already configured for DHCP."
else
    # Remove existing eth0 configuration and add DHCP
    sudo sed -i '/^auto eth0/,/^$/d' "$INTERFACES_FILE"
    sudo sed -i '/^iface eth0/,/^$/d' "$INTERFACES_FILE"
    
    # Add DHCP configuration
    echo "" | sudo tee -a "$INTERFACES_FILE" > /dev/null
    echo "auto eth0" | sudo tee -a "$INTERFACES_FILE" > /dev/null
    echo "iface eth0 inet dhcp" | sudo tee -a "$INTERFACES_FILE" > /dev/null
    
    success "eth0 configured for DHCP."
fi
echo ""

# Step 8: Summary and next steps
echo "================================================"
echo "  Commissioning Complete!"
echo "================================================"
echo ""
success "All configuration changes have been applied."
echo ""
echo "Next steps:"
echo "  1. Sync and unmount the SD card:"
echo "     sync"
echo "     sudo umount $ROOTFS"
echo ""
echo "  2. Insert the SD card back into the RoomWizard device"
echo ""
echo "  3. Connect Ethernet cable and power on the device"
echo ""
echo "  4. Wait ~30 seconds for boot"
echo ""
echo "  5. Find the device IP from your router's DHCP leases"
echo ""
echo "  6. SSH into the device:"
echo "     ssh root@<device-ip>"
echo ""
echo "  7. Deploy games and ScummVM using build-and-deploy.sh"
echo "     (See native_games/README.md for details)"
echo ""
echo "================================================"
