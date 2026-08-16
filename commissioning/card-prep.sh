#!/bin/bash
#
# RoomWizard SD Card Commissioning Script
#
# This script automates the initial setup of a RoomWizard device after
# removing the SD card from the box. It configures root access, SSH,
# and DHCP networking.
#
# Usage: ./commissioning/card-prep.sh
#
# Prerequisites:
# - SD card must be inserted into a Linux machine
# - Partitions should be auto-mounted (or manually mounted)
# - openssl command must be available
#

set -e  # Exit on error

# Defined up here rather than at first use: it is needed both by the host-name
# step (to run set-hostname.sh beside it) and by the next-steps block at the end
# (to read COMMISSIONING.md at the repo root), and one definition cannot drift
# from another.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

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
#
# By CONTENT, not by UUID. A filesystem UUID is generated at mkfs time, so it
# names one card and not a model: two RoomWizards on the identical firmware
# build share none of their four UUIDs, and a hardcoded one recognises only the
# unit whose card the constant was copied from. lib/rw-identify.sh holds the markers
# and the full reasoning.
#
# Sourced with `.` rather than run, and via an absolute path, because the Bash
# tool's working directory is not dependable.
# shellcheck source=../lib/rw-identify.sh
. "$REPO_ROOT/lib/rw-identify.sh"
# shellcheck source=../lib/rw-ssh.sh
. "$REPO_ROOT/lib/rw-ssh.sh"

if [ -n "${ROOTFS:-}" ]; then
    # The documented escape hatch: a card mounted by hand, or a copy of a rootfs
    # used to exercise this script with no SD card at all. Honoured, but still
    # checked — `export ROOTFS=/` would otherwise rewrite this host's own
    # /etc/shadow. A failed check warns and asks rather than refusing, so the
    # hatch stays usable for a deliberately odd target.
    info "Using ROOTFS from the environment: $ROOTFS (detection skipped)"
    if rw_is_rootfs "$ROOTFS"; then
        success "It looks like a RoomWizard rootfs: $(rw_rootfs_firmware "$ROOTFS")"
    else
        warning "$ROOTFS does not look like a RoomWizard rootfs."
        warning "Expected all of: $RW_ROOTFS_REQUIRED"
        warning "plus one of: $RW_ROOTFS_VENDOR (or '$RW_ISSUE_RE' in etc/issue)"
        echo ""
        warning "Continuing will edit /etc/shadow, /etc/hosts, /etc/hostname,"
        warning "/etc/ssh/sshd_config and /etc/network/interfaces UNDER THIS PATH."
        echo ""
        read -p "Type 'yes' to continue anyway: " FORCE_ROOTFS
        if [ "$FORCE_ROOTFS" != "yes" ]; then
            error "Aborted. Nothing was changed."
            exit 1
        fi
    fi
else
    info "Locating the RoomWizard rootfs among the mounted filesystems..."

    # `|| true`: no match is a normal outcome, reported below with a diagnosis.
    MOUNTED_ROOTFS=$(rw_find_rootfs || true)
    ROOTFS_COUNT=$(printf '%s' "$MOUNTED_ROOTFS" | grep -c . || true)

    if [ "$ROOTFS_COUNT" -eq 0 ]; then
        error "No RoomWizard rootfs is mounted."
        echo ""

        # Make the failure actionable instead of generic. This is a read-only
        # scan of partition tables — nothing is mounted and nothing is written.
        CARDS=$(rw_find_card_disks || true)
        if [ -n "$CARDS" ]; then
            echo "  A disk carrying the RoomWizard partition layout IS present:"
            echo ""
            echo "$CARDS" | while read -r dev size; do
                echo "    $dev   $size"
            done
            echo ""
            echo "  Its rootfs is the ~980 MB ext4 partition on that disk. Mount it and"
            echo "  re-run — the device name below is resolved, not guessed:"
            echo ""
            echo "$CARDS" | while read -r dev _; do
                echo "    sudo mkdir -p /mnt/rw"
                echo "    sudo mount $(rw_part_dev "$dev" 6) /mnt/rw"
                break
            done
        else
            echo "  No disk on this host carries the RoomWizard partition layout"
            echo "  (7 partitions; p6 at sector 4096638, 980.5 MB)."
            echo ""
            echo "  Check that the card is in the reader and visible:"
            echo "    lsblk -o NAME,FSTYPE,SIZE,MOUNTPOINT | grep -v loop"
            echo ""
            echo "  On WSL the card must first be attached from Windows:"
            echo "    wsl --mount \\\\.\\PHYSICALDRIVEn --bare"
            echo ""
            # Deliberately NOT "mount /dev/sdX6": whoever is holding the card
            # cannot see partition numbers, and lib/rw-identify.sh exists so
            # that nobody has to work one out. Once the disk is visible this
            # script names the exact device itself, in the branch above.
            echo "  Then re-run this script: with the card visible it finds the"
            echo "  rootfs by content and prints the exact mount command for it."
        fi
        echo ""
        echo "  Either way, you can point this script at a mounted tree directly:"
        echo "    export ROOTFS=/mnt/rw"
        exit 1
    fi

    if [ "$ROOTFS_COUNT" -gt 1 ]; then
        error "More than one RoomWizard rootfs is mounted:"
        echo ""
        echo "$MOUNTED_ROOTFS" | while read -r m; do
            echo "    $m   [$(rw_rootfs_firmware "$m")]"
        done
        echo ""
        echo "  Commissioning writes a host name and a password, so it must not"
        echo "  guess which card you meant. Unmount the others, or name one:"
        echo "    export ROOTFS=<mountpoint>"
        exit 1
    fi

    ROOTFS="$MOUNTED_ROOTFS"
    success "Found rootfs at: $ROOTFS"
    info "Firmware: $(rw_rootfs_firmware "$ROOTFS")"
fi
echo ""

# A read-only mount is the one way detection can succeed and every edit below
# fail. Catch it here rather than emitting a pile of "Read-only file system".
if ! rw_is_rootfs_writable "$ROOTFS"; then
    error "$ROOTFS is mounted read-only; commissioning cannot write to it."
    echo ""
    echo "  Remount it read-write:"
    echo "    sudo mount -o remount,rw $ROOTFS"
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
# password never reaches another process's command line either.
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

# Step 3b: Host name
echo "================================================"
echo "  Host Name"
echo "================================================"
echo ""

# Every unit cloned from this image claims the SAME name, and the image also maps
# that name, on a non-loopback line, to an external address that is unreachable
# here — so a device resolving its own name gets a bogus IP. Naming the card here
# is the only point in the flow where it costs nothing: no device to reach, no
# reboot. See commissioning/set-hostname.sh for what gets written.
CURRENT_HOSTNAME=""
if [ -f "$ROOTFS/etc/hostname" ]; then
    CURRENT_HOSTNAME=$(head -1 "$ROOTFS/etc/hostname" | tr -d ' \011\015\012')
fi
info "The card currently claims the name: ${CURRENT_HOSTNAME:-(none)}"
info "A unique name lets you reach the unit as <name>.local once mDNS is on"
info "(commissioning/provision.sh enables it), instead of hunting for a DHCP lease."
echo ""

while true; do
    read -p "Host name [${CURRENT_HOSTNAME:-roomwizard}]: " NEW_HOSTNAME
    NEW_HOSTNAME="${NEW_HOSTNAME:-${CURRENT_HOSTNAME:-roomwizard}}"

    # commissioning/set-hostname.sh is the single implementation and the single validator; it
    # refuses a bad name and changes nothing, so let it be the judge rather than
    # duplicating the RFC-1123 regex here.
    #
    # Run through `bash` rather than as `./commissioning/set-hostname.sh`: a clone can land
    # without the executable bit, and failing HERE — after /etc/shadow has
    # already been rewritten — leaves a half-commissioned card and loops on
    # "Please try again" forever. The interpreter is not in doubt.
    if sudo bash "$SCRIPT_DIR/set-hostname.sh" "$NEW_HOSTNAME" "$ROOTFS"; then
        success "Host name set to: $NEW_HOSTNAME"
        break
    fi
    echo ""
    warning "Please try again."
    echo ""
done
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

# ── The invoking operator's home, which is not always $HOME ────────────────
#
# The resolution lives in lib/rw-ssh.sh, not here, because rw_ssh_keygen below needs
# the same answer and two copies of "whose home" is how the original bug got in: this
# script sudo's each individual write rather than requiring root, but
# commissioning/commission-offline.sh runs as root and calls it, and under sudo $HOME
# is /root. The key was therefore NEVER found in the offline flow.
OPERATOR_HOME="$(rw_ssh_operator_home)"

# Step 6: SSH Key Setup (optional)
echo "================================================"
echo "  SSH Key Setup (Optional)"
echo "================================================"
echo ""

read -p "Do you want to set up SSH key authentication? (y/n): " SETUP_SSH_KEYS

if [[ "$SETUP_SSH_KEYS" =~ ^[Yy]$ ]]; then
    # Both key types, and the lookup lives in lib/rw-ssh.sh so that this script and
    # the SSH gate agree on which key they mean. ed25519 first: on a host that has
    # both, it is the newer one and the one ssh offers first.
    DEFAULT_KEY="$(rw_ssh_pubkey)" || DEFAULT_KEY=""
    SSH_KEY_PATH=""
    if [ -n "$DEFAULT_KEY" ]; then
        info "Found SSH public key: $DEFAULT_KEY"
        read -p "Use this key? (y/n): " USE_DEFAULT

        if [[ "$USE_DEFAULT" =~ ^[Yy]$ ]]; then
            SSH_KEY_PATH="$DEFAULT_KEY"
        else
            read -p "Enter path to your SSH public key: " SSH_KEY_PATH
            SSH_KEY_PATH="${SSH_KEY_PATH/#\~/$OPERATOR_HOME}"
        fi
    else
        # An explicit `if`, not `[ ... ] && info ...`: `set -e` is on (line 17),
        # and while bash does not exit on a failing left operand of &&, that is a
        # subtlety to rely on in a script whose failure mode is a half-written
        # card. Measured, not assumed — but written so it needs no measuring.
        if [ "$OPERATOR_HOME" != "$HOME" ]; then
            info "Looked in $OPERATOR_HOME/.ssh (\$SUDO_USER's home, not root's)"
        fi
        # ⚠️ OFFER TO GENERATE, before asking for a path. This is the other half of
        # the missing-key defect: an operator with no key at all was asked for a
        # path they did not have, and the card was then written WITHOUT
        # authorized_keys while every later script told them to "check IP and SSH
        # key". One command closes it, and the key is theirs for everything else
        # rather than a project-local secret. No password is stored anywhere.
        info "No SSH public key found in $OPERATOR_HOME/.ssh."
        if rw_ssh_ask "Generate one now (ed25519, no passphrase)?"; then
            if SSH_KEY_PATH="$(rw_ssh_keygen)"; then
                success "Generated $SSH_KEY_PATH"
            else
                warning "Could not generate a key."
                SSH_KEY_PATH=""
            fi
        fi
        if [ -z "$SSH_KEY_PATH" ]; then
            read -p "Enter path to your SSH public key (e.g., ~/.ssh/id_rsa.pub): " SSH_KEY_PATH
            # ~ expands to the OPERATOR's home for the same reason: under sudo, the
            # shell's own ~ would be /root.
            SSH_KEY_PATH="${SSH_KEY_PATH/#\~/$OPERATOR_HOME}"
        fi
    fi

    if [ ! -f "$SSH_KEY_PATH" ]; then
        error "SSH key file not found: $SSH_KEY_PATH"
        warning "This card is being written WITHOUT authorized_keys."
        warning "Password login still works (this script enables it), and"
        warning "commissioning/provision.sh will offer to install a key on first contact."
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
# short of re-mounting the card.
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

# ── Step 7b: the vendor boot-time network regenerator ──────────────────────
#
# Everything written above — the host name, /etc/hosts, dhclient.conf's
# `send host-name` and the DHCP stanza — is undone ~7 s into the FIRST boot.
# /opt/sbin/networkmanager, started from etc/rcS.d/S60networkmanager, rewrites
# all four out of /home/root/data/websign/net.*.
#
# Measured on an RW20, 2026-08-08: a card prepared here as `rwtest` booted,
# S39hostname.sh set `rwtest` (the syslog prefix proves it), and 7 s later
# S60networkmanager logged "Manual IP Mode detected" / "Vaild host name found:
# null", reset /etc/hostname to `null`, rewrote /etc/hosts as
# `10.11.140.22 null`, killed the dhclient that S40networking had started and
# applied a static address on a subnet the host could not reach. Nothing had
# failed, and the unit was unreachable — so phase 1 could not lead to phase 2,
# which needs SSH.
#
# Hence: MEASURE, then offer. Two reasons it is not an unconditional delete.
# The vendor's static address is the right answer for an operator who IS on
# that subnet and has no DHCP server, and both writers live inside
# set_manual()/set_dhcp(), so a card whose websign/ is already gone has nothing
# to fix. The measurement decides which of those a card is.
#
# websign/ is on p2 and this script has p6. The probe therefore resolves the
# card's own disk from the mounted rootfs and mounts p2 READ-ONLY for the three
# reads — never writes it, and never guesses a device name.

VENDOR_REGEN_LINK="$ROOTFS/etc/rcS.d/S60networkmanager"
VENDOR_WEBSIGN="unknown"     # yes | no | unknown  (unknown = could not measure)
VENDOR_NET_MODE=""
VENDOR_NET_IP=""
VENDOR_NET_NAME=""

# ---------------------------------------------------------------------------
# probe_vendor_net ROOTFS
#
# Fill VENDOR_WEBSIGN / VENDOR_NET_{MODE,IP,NAME} from p2. Returns 1 without
# setting VENDOR_WEBSIGN if the card's data partition could not be reached at
# all, which the caller reports as "unmeasured" rather than as "nothing to do".
#
# Three vetoes, in order. A ROOTFS pointing at a copied tree resolves to this
# HOST's root device, so mounting "its p2" would mount a stranger — that is
# what rw_is_host_root_disk refuses. rw_is_card_disk then requires the
# RoomWizard partition table, and rw_card_partitions names p2 by POSITION,
# never by UUID (two cards share none of their four).
# ---------------------------------------------------------------------------
probe_vendor_net() {
    local rootfs="$1" src disk data tmp

    command -v findmnt >/dev/null 2>&1 || return 1
    command -v lsblk   >/dev/null 2>&1 || return 1

    src=$(findmnt -no SOURCE --target "$rootfs" 2>/dev/null) || return 1
    case "$src" in /dev/*) ;; *) return 1 ;; esac

    disk="/dev/$(lsblk -rnso NAME "$src" 2>/dev/null | tail -1)"
    [ -b "$disk" ] || return 1
    # An explicit `if`, not `rw_is_host_root_disk "$disk" && return 1`: `set -e`
    # is on (line 17), and that list's status is the veto's own failure — so the
    # normal case, a card that is NOT this host's root disk, would abort the run.
    if rw_is_host_root_disk "$disk"; then
        return 1
    fi
    rw_is_card_disk "$disk" || return 1

    data=$(rw_card_partitions "$disk" | awk '$1 == "data" { print $2 }')
    [ -b "$data" ] || return 1

    tmp=$(mktemp -d) || return 1
    if ! sudo mount -o ro "$data" "$tmp" 2>/dev/null; then
        rmdir "$tmp" 2>/dev/null
        return 1
    fi

    if [ -d "$tmp/websign" ]; then
        VENDOR_WEBSIGN=yes
        # awk, not cat: these files are written by the vendor's web UI and a
        # trailing CR or a second field would otherwise land in the verdict.
        VENDOR_NET_MODE=$(sudo awk 'NR==1 { print $1 }' "$tmp/websign/net.mode"      2>/dev/null)
        VENDOR_NET_IP=$(  sudo awk 'NR==1 { print $1 }' "$tmp/websign/net.ipaddress" 2>/dev/null)
        VENDOR_NET_NAME=$(sudo awk 'NR==1 { print $1 }' "$tmp/websign/net.hostname"  2>/dev/null)
    else
        VENDOR_WEBSIGN=no
    fi

    sudo umount "$tmp" 2>/dev/null
    rmdir "$tmp" 2>/dev/null
    return 0
}

# ---------------------------------------------------------------------------
# vendor_regen_verdict LINK_PRESENT WEBSIGN MODE
#
# The whole decision, as a pure function of three measurements, so it is
# host-testable without a card (tests/commission_prep_test.sh).
#
#   absent      the rcS.d link is gone; the regenerator cannot run
#   inert       link present, websign/ absent — both writers are inside
#               set_manual()/set_dhcp(), and neither branch is entered
#   manual      link + websign + mode=manual — static address, dhclient killed,
#               host name reset. This is the unreachable case.
#   dhcp        link + websign + mode=dhcp — the unit still gets a lease, but
#               /etc/hostname and /etc/hosts are still rewritten from net.*
#   unmeasured  p2 unreachable, or a mode this script does not recognise
# ---------------------------------------------------------------------------
vendor_regen_verdict() {
    local link="$1" websign="$2" mode="$3"

    [ "$link" = yes ] || { echo absent; return 0; }

    case "$websign" in
        no) echo inert ;;
        yes)
            case "$mode" in
                manual) echo manual ;;
                dhcp)   echo dhcp ;;
                *)      echo unmeasured ;;
            esac
            ;;
        *) echo unmeasured ;;
    esac
}

# ---------------------------------------------------------------------------
# disable_vendor_regen
#
# Remove the rcS.d link, and only that. /etc/init.d/networkmanager stays, so
# the restore is one `ln -s` — which is also why the link is NOT renamed in
# place: /etc/init.d/rc globs S[0-9][0-9]*, so `S60networkmanager.disabled`
# would still be executed.
#
# This is not a new deletion decision. device-files/clean-rules.conf already
# names this link, so every cleaned unit has it removed anyway; doing it here
# only moves it earlier, to the one phase that has no other way to survive.
# ---------------------------------------------------------------------------
disable_vendor_regen() {
    local target
    target=$(readlink "$VENDOR_REGEN_LINK" 2>/dev/null)
    sudo rm -f "$VENDOR_REGEN_LINK" || return 1
    success "Removed etc/rcS.d/S60networkmanager${target:+  (was -> $target)}"
    info "/etc/init.d/networkmanager is untouched. To restore, on the card:"
    echo "    sudo ln -s ../init.d/networkmanager $VENDOR_REGEN_LINK"
    return 0
}

echo "================================================"
echo "  Vendor Network Regenerator"
echo "================================================"
echo ""

if [ -n "${RW_COMMISSION_ORCHESTRATED:-}" ]; then
    # The full offline pass deletes websign/ AND this link in its clean, and its
    # verify pass fails if either survives. Asking here would ask about a file
    # that is about to go regardless.
    info "Skipped: the offline pass deletes websign/ and this link in its clean."
    echo ""
else
    if [ -L "$VENDOR_REGEN_LINK" ] || [ -e "$VENDOR_REGEN_LINK" ]; then
        LINK_PRESENT=yes
    else
        LINK_PRESENT=no
    fi

    # Same reason as inside probe_vendor_net: an explicit `if`, because a probe
    # that cannot reach p2 is a measurement failure, not a run failure.
    if [ "$LINK_PRESENT" = yes ]; then
        probe_vendor_net "$ROOTFS" || true
    fi
    VERDICT=$(vendor_regen_verdict "$LINK_PRESENT" "$VENDOR_WEBSIGN" "$VENDOR_NET_MODE")

    case "$VERDICT" in
        absent)
            success "etc/rcS.d/S60networkmanager is already gone — nothing rewrites the above."
            ;;
        inert)
            success "websign/ is absent on p2, so the regenerator writes nothing."
            info "Its rcS.d link is still there; both writers are inside set_manual()/set_dhcp()."
            ;;
        manual|dhcp|unmeasured)
            case "$VERDICT" in
                manual)
                    warning "This unit will IGNORE the DHCP setting just written."
                    echo ""
                    echo "  Vendor network config (p2, websign/net.*):"
                    echo "      mode      manual"
                    echo "      address   ${VENDOR_NET_IP:-(unset)}"
                    echo "      host name ${VENDOR_NET_NAME:-(unset)}"
                    echo ""
                    echo "  ~7 s into the first boot, /opt/sbin/networkmanager takes the static"
                    echo "  address above, kills dhclient, and resets /etc/hostname and /etc/hosts"
                    echo "  to '${VENDOR_NET_NAME:-null}'. If that address is not on a subnet this host can"
                    echo "  reach, the unit appears in no DHCP lease list and phase 2 is impossible."
                    echo ""
                    echo "  This host's own addresses, for comparison:"
                    # An explicit `if`: in `ip ... | awk ... || echo`, the
                    # pipeline's status is awk's, so a missing `ip` would print
                    # nothing at all rather than the fallback.
                    if command -v ip >/dev/null 2>&1; then
                        ip -o -4 addr show scope global 2>/dev/null \
                            | awk '{ printf "      %-8s %s\n", $2, $4 }'
                    else
                        echo "      (could not read; 'ip' is not on this host)"
                    fi
                    ;;
                dhcp)
                    warning "The host name just set will not survive the first boot."
                    echo ""
                    echo "  Vendor network config (p2, websign/net.*):"
                    echo "      mode      dhcp"
                    echo "      host name ${VENDOR_NET_NAME:-(unset)}"
                    echo ""
                    echo "  The unit still gets a lease, so you can reach it. But ~7 s into the"
                    echo "  first boot /opt/sbin/networkmanager rewrites /etc/hostname and"
                    echo "  /etc/hosts from the values above, so the name you chose is lost and"
                    echo "  the router will list this unit under '${VENDOR_NET_NAME:-null}'."
                    ;;
                unmeasured)
                    warning "Could not read the vendor network config on p2."
                    echo ""
                    if [ "$VENDOR_WEBSIGN" = yes ]; then
                        echo "  websign/net.mode says '${VENDOR_NET_MODE:-(empty)}', which is neither"
                        echo "  'manual' nor 'dhcp'."
                    else
                        echo "  p2 (the data partition) could not be resolved from $ROOTFS,"
                        echo "  so whether the regenerator will act is unknown."
                    fi
                    echo ""
                    echo "  etc/rcS.d/S60networkmanager IS present, so it will run. If"
                    echo "  websign/net.* holds values, everything set above is rewritten from"
                    echo "  them ~7 s into the first boot."
                    ;;
            esac
            echo ""
            echo "  [d] disable the regenerator, so DHCP and '${NEW_HOSTNAME:-the name above}' survive the boot"
            echo "      removes etc/rcS.d/S60networkmanager on p6. The deep clean in phase 2"
            echo "      removes it anyway; /etc/init.d/networkmanager stays, so one ln -s undoes this."
            echo "  [k] keep it, and reach the unit at the vendor's address"
            echo ""

            # Interactive only. Removing a boot link is a write, and a batch
            # caller that nobody is watching must not have one chosen for it —
            # so a non-TTY run reports and changes nothing, loudly, rather than
            # letting an EOF read as either answer.
            if [ -t 0 ]; then
                read -r -p "  Choice [d]: " REGEN_CHOICE
                REGEN_CHOICE="${REGEN_CHOICE:-d}"
            else
                REGEN_CHOICE=k
                echo "  ################################################################"
                echo "  # NOT A TTY: nobody was asked, so the regenerator is KEPT."
                echo "  # Everything this script wrote above may be undone by the first"
                echo "  # boot. To disable it, on the mounted card:"
                echo "  #     sudo rm -f $VENDOR_REGEN_LINK"
                echo "  ################################################################"
            fi
            echo ""

            case "$REGEN_CHOICE" in
                d|D) disable_vendor_regen || error "Could not remove $VENDOR_REGEN_LINK" ;;
                *)   warning "Kept. The values written above may not survive the first boot." ;;
            esac
            ;;
    esac
    echo ""
fi

# Step 8: Summary and next steps
#
# ── Why an explicit flag and not a ROOTFS test ─────────────────────────────
#
# The next-steps block below tells the operator to boot the unit, then run
# commissioning/provision.sh and deploy-all.sh. That is correct for a standalone run and
# WRONG under commissioning/commission-offline.sh, which has already done both by the time it
# gets here — an operator who follows it hunts for an IP to repeat work that is
# finished, and the banner above says "Complete!" while three phases remain.
#
# The signal is an env flag the orchestrator sets deliberately, NOT the presence
# of ROOTFS. ROOTFS is this script's documented standalone escape hatch
# ("you can also `export ROOTFS=/mnt/rw`" — roomwizard.sh's own phase-1 text), so
# sniffing it would silence the next steps for the hand-mounted case, which is
# exactly the case that needs them most.
if [ -n "${RW_COMMISSION_ORCHESTRATED:-}" ]; then
    echo "================================================"
    echo "  Card prep complete — continuing"
    echo "================================================"
    echo ""
    success "Password, host name, SSH and DHCP are written to the card."
    info "Returning to commissioning/commission-offline.sh for the clean, the install and the verify."
    echo ""
    exit 0
fi

echo "================================================"
echo "  Commissioning Complete!"
echo "================================================"
echo ""
success "All configuration changes have been applied."
echo ""

# Print next steps from COMMISSIONING.md (single source of truth)
GUIDE="$REPO_ROOT/COMMISSIONING.md"
if [ -f "$GUIDE" ]; then
    sed -n '/<!-- NEXT_STEPS_START -->/,/<!-- NEXT_STEPS_END -->/{/<!--/d; p;}' "$GUIDE"
else
    echo "  See COMMISSIONING.md for next steps."
fi
echo ""
echo "================================================"
