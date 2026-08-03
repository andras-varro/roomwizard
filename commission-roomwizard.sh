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
if [ ! -d "$ROOTFS/etc" ]; then
    error "The mounted partition at $ROOTFS doesn't look like a valid rootfs."
    error "Expected to find /etc directory."
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
# Feed the password on stdin, never as an argument: an argument is world-readable
# in /proc/<pid>/cmdline for the lifetime of the openssl process, which defeats
# the `read -s` above.  `printf` is a shell builtin, so it forks nothing and the
# password never reaches another process's command line either.  (B17.)
PASSWORD_HASH=$(printf '%s\n' "$PASSWORD" | openssl passwd -6 -stdin)
if [ -z "$PASSWORD_HASH" ]; then
    error "openssl produced no password hash. Is 'openssl passwd -6 -stdin' supported?"
    exit 1
fi
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
        
        # Create .ssh directory (root home is at /home/root on this system)
        sudo mkdir -p "$ROOTFS/home/root/.ssh"
        sudo chmod 700 "$ROOTFS/home/root/.ssh"
        
        # Copy public key
        cat "$SSH_KEY_PATH" | sudo tee "$ROOTFS/home/root/.ssh/authorized_keys" > /dev/null
        sudo chmod 600 "$ROOTFS/home/root/.ssh/authorized_keys"
        
        success "SSH key installed successfully."
        info "You will be able to SSH without a password using your key."
    fi
else
    info "Skipping SSH key setup."
    info "You will need to use password authentication to SSH."
fi
echo ""

# ── Remove every eth0 stanza from an interfaces(5) file read on stdin ──────
#
# The old implementation was `sed -i '/^auto eth0/,/^$/d'` plus the same range
# for /^iface eth0/.  A sed range whose end address never matches runs to EOF,
# so on a file whose eth0 stanza is last — or that simply has no blank lines,
# which is how plenty of vendor images ship — it deleted everything from eth0
# onwards.  Any `auto lo` / `iface lo` below that point went with it, and the
# device then booted with no network and no SSH: an unrecoverable commissioning
# short of re-mounting the card.  (IMPROVEMENT_PLAN.md B17.)
#
# This is a stanza-aware filter instead.  Two things about interfaces(5) that
# the sed version got wrong:
#   - `auto` and `allow-*` lines carry a LIST of interfaces, so eth0 has to be
#     removed token by token and the line kept if anything else remains
#     (`auto lo eth0` must become `auto lo`, not vanish).
#   - an `iface` stanza owns every following line up to the next stanza keyword
#     *or* a blank line — not "up to the next blank line", which assumes a
#     formatting convention the file is under no obligation to follow.
strip_eth0_stanzas() {
    awk '
    BEGIN { skip = 0 }
    {
        kw = $0
        sub(/^[ \t]+/, "", kw)

        # auto / allow-hotplug / allow-auto: a list of interface names
        if (kw ~ /^(auto|allow-[^ \t]+)([ \t]|$)/) {
            skip = 0
            n = split(kw, tok, /[ \t]+/)
            out = tok[1]; kept = 0; dropped = 0
            for (i = 2; i <= n; i++) {
                if (tok[i] == "") continue
                if (tok[i] == "eth0") { dropped = 1; continue }
                out = out " " tok[i]; kept = 1
            }
            if (!dropped) print         # untouched — keep the original spacing
            else if (kept) print out
            next
        }

        # iface: opens a stanza whose option lines belong to it
        if (kw ~ /^iface([ \t]|$)/) {
            split(kw, tok, /[ \t]+/)
            skip = (tok[2] == "eth0")
            if (!skip) print
            next
        }

        # any other stanza keyword closes the current stanza
        if (kw ~ /^(mapping|source|source-directory|no-auto-down|no-scripts)([ \t]|$)/) {
            skip = 0; print; next
        }

        if (kw == "") { skip = 0; print; next }   # a blank line also closes it

        if (!skip) print                          # option line of a kept stanza
    }'
}

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
    # Strip any existing eth0 stanza, then append the DHCP one.  Built in a temp
    # file so the target is either the old file or the complete new one, never a
    # half-edited in-place result.
    TMP_INTERFACES=$(mktemp)
    {
        strip_eth0_stanzas < "$INTERFACES_FILE"
        echo ""
        echo "auto eth0"
        echo "iface eth0 inet dhcp"
    } > "$TMP_INTERFACES"

    # Assert the loopback survived rather than trusting the filter: losing it is
    # the exact failure this replaces, and it costs one grep to make impossible.
    LO_RE='^[[:space:]]*iface[[:space:]]+lo([[:space:]]|$)'
    if grep -Eq "$LO_RE" "$INTERFACES_FILE" && ! grep -Eq "$LO_RE" "$TMP_INTERFACES"; then
        rm -f "$TMP_INTERFACES"
        error "Refusing to write $INTERFACES_FILE: the loopback stanza would be lost."
        error "The file is unchanged (backup: $INTERFACES_FILE.backup). Please edit it by hand."
        exit 1
    fi

    # cp onto the existing file, so its mode and owner are preserved.
    sudo cp "$TMP_INTERFACES" "$INTERFACES_FILE"
    rm -f "$TMP_INTERFACES"

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

# Print next steps from COMMISSIONING.md (single source of truth)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GUIDE="$SCRIPT_DIR/COMMISSIONING.md"
if [ -f "$GUIDE" ]; then
    sed -n '/<!-- NEXT_STEPS_START -->/,/<!-- NEXT_STEPS_END -->/{/<!--/d; p;}' "$GUIDE"
else
    echo "  See COMMISSIONING.md for next steps."
fi
echo ""
echo "================================================"
