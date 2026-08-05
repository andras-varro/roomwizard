#!/bin/bash
#
# commission-offline.sh — put a RoomWizard card in a reader, answer two
#                         questions, put it back, and the device BOOTS WORKING.
#
# IMPROVEMENT_PLAN.md F10, step 4.
#
# Usage:
#   sudo ./commission-offline.sh --bundle <file.tar.gz|dir> [options]
#   sudo ./commission-offline.sh --bundle <b> --dry-run
#        ./commission-offline.sh --bundle <b> --base /mnt/rw   # already mounted
#
#   --bundle <path>     A release tarball from `./release.sh --stage-only`, or a
#                       staged bundle directory. THE source of binaries: an
#                       offline commissioner has no toolchain to fall back on.
#   --disk <dev>        The card. Default: the one RoomWizard disk found, if
#                       exactly one is present.
#   --base <dir>        The card is ALREADY mounted, as <dir>/{root,data,log,backup}.
#                       Mounts nothing, unmounts nothing, needs no root.
#   --dry-run           Print every resolved absolute path and change nothing.
#   --keep-<group>      Leave one vendor stack on disk. See --help for the list.
#   --delete-factory    Also delete the 472 MB on-device restore payload.
#   --arm-check=skip    Proceed with UNVERIFIED binaries when the ARM objdump is
#                       absent. Read what it prints before you use it.
#   --no-clean          Install only; run no cleanup at all.
#
# ── Why offline, and why one pass ───────────────────────────────────────────
#
# Today's flow is three phases with a reboot and an IP hunt in the middle
# (commission-roomwizard.sh -> boot -> setup-device.sh -> deploy-all.sh). That is
# fine as a development loop and unusable by anyone who is not developing this.
#
# Offline is also not merely a convenience. A unit whose websign/net.mode is
# `manual` takes a static address and never sends a DHCP request, so it appears in
# no lease list and PHASE 2 CAN NEVER REACH IT (SYSTEM_ANALYSIS.md#35). Stock
# cards ship that way. Editing the card is the only bootstrap for such a unit.
#
# And it REMOVES D7b instead of patching it: the boot-time network regenerator's
# input (/home/root/data/websign) is deleted in the same pass that sets the name,
# so no boot happens in between and nothing overwrites /etc/hostname.
#
# ── What this script does NOT reimplement ──────────────────────────────────
#
# Nothing here restates a decision that lives somewhere else:
#
#   the two operator prompts   ROOTFS=<mnt> commission-roomwizard.sh already asks
#                              exactly the password and host-name questions, and
#                              does shadow / sshd / DHCP / the SSH key. This
#                              orchestrates it.
#   the host name              set-hostname.sh, which owns /etc/hostname,
#                              /etc/hosts AND /etc/dhclient.conf (D7b item 3).
#   which card, which mount    rw-identify.sh, by content and by POSITION, never
#                              by UUID. p1 is unreachable through it by design.
#   what to delete             device-files/clean-rules.conf, shared with
#                              setup-device.sh --deep-clean so the two cannot drift.
#   what to install            the bundle's own manifest (rw-bundle.sh). Modes are
#                              DECLARED there, never read off disk.
#   the boot scripts           device-files/{audio-enable,time-sync,99-security.conf},
#                              the same files setup-device.sh copies.
#
# ── Still needs the device ─────────────────────────────────────────────────
#
# Touch calibration only — it is per-unit and per-panel, and the wizard lives in
# Device Tools -> Display -> CALIBRATE TOUCH. One boot remains; this removes two
# of the three, plus the IP hunt.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# shellcheck source=rw-identify.sh
. "$SCRIPT_DIR/rw-identify.sh"
# shellcheck source=rw-clean.sh
. "$SCRIPT_DIR/rw-clean.sh"
# shellcheck source=rw-bundle.sh
. "$SCRIPT_DIR/rw-bundle.sh"

DEVICE_FILES="$SCRIPT_DIR/device-files"
CLEAN_RULES="$DEVICE_FILES/clean-rules.conf"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}" >&2; cleanup_and_exit 1; }

BUNDLE=""
DISK=""
BASE=""
DRY=""
KEEP_GROUPS=""
DEL_FACTORY=0
ARM_CHECK="require"
DO_CLEAN=1

# State the cleanup trap needs.  Set before any mount so an early failure still
# unwinds, and used by err() above — which is why they are declared up here even
# though nothing reads them yet.
MOUNTED_BASE=""
TMPROOT=""

cleanup_and_exit() {
    local code="${1:-0}"
    if [ -n "$MOUNTED_BASE" ]; then
        info "Unmounting $MOUNTED_BASE"
        rw_umount_card "$MOUNTED_BASE" || true
        rmdir "$MOUNTED_BASE"/{root,data,log,backup} 2>/dev/null || true
        rmdir "$MOUNTED_BASE" 2>/dev/null || true
        MOUNTED_BASE=""
    fi
    [ -n "$TMPROOT" ] && rm -rf "$TMPROOT"
    exit "$code"
}
trap 'cleanup_and_exit 1' INT TERM

usage() {
    cat <<USAGE
Usage: sudo $0 --bundle <file.tar.gz|dir> [options]

  --bundle <path>    Release tarball or staged bundle directory (REQUIRED)
  --disk <dev>       The SD card whole disk, e.g. /dev/sdf. Auto-detected if
                     exactly one RoomWizard card is present.
  --base <dir>       The card is already mounted as <dir>/{root,data,log,backup}.
                     Nothing is mounted or unmounted; no root needed.
  --dry-run          Resolve and print everything, change nothing.
  --no-clean         Install only, clean nothing.
  --delete-factory   Also delete the 472 MB on-device restore payload.
  --keep-<group>     Leave one vendor stack on disk. Groups:
                     $(rw_clean_optional_groups)
                     Files only — it does not re-enable a boot link, because the
                     rc*.d whitelist is what removes an unknown vendor service.
  --arm-check=skip   Install binaries this host cannot verify. Say why to
                     yourself first; the message it replaces explains the risk.
  --help

The card is identified by CONTENT and by PARTITION POSITION, never by UUID, and
p1 (mlo, u-boot.bin, ctrlblock.bin, uImage-system) is unreachable from here — an
untouched p1 is what keeps a power cycle a free undo.
USAGE
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bundle)         BUNDLE="${2:-}"; [[ -n "$BUNDLE" ]] || { echo "--bundle needs a value"; usage; }; shift 2 ;;
        --disk)           DISK="${2:-}";   [[ -n "$DISK" ]]   || { echo "--disk needs a value"; usage; };   shift 2 ;;
        --base)           BASE="${2:-}";   [[ -n "$BASE" ]]   || { echo "--base needs a value"; usage; };   shift 2 ;;
        --dry-run)        DRY=1; shift ;;
        --no-clean)       DO_CLEAN=0; shift ;;
        --delete-factory) DEL_FACTORY=1; shift ;;
        --arm-check=skip) ARM_CHECK="skip"; shift ;;
        --keep-*)
            g="${1#--keep-}"
            case " $(rw_clean_optional_groups) " in
                *" $g "*) KEEP_GROUPS="$KEEP_GROUPS $g" ;;
                *) echo "Unknown clean group: $g"; echo "  --keep- accepts: $(rw_clean_optional_groups)"; exit 1 ;;
            esac
            shift ;;
        --help|-h) usage ;;
        *) echo "Unknown option: $1"; echo ""; usage ;;
    esac
done

[[ -n "$BUNDLE" ]] || { echo "A bundle is required — an offline commissioner has no toolchain."; echo ""; usage; }
[[ -e "$BUNDLE" ]] || { echo "No such bundle: $BUNDLE"; exit 1; }
[[ -n "$DISK" && -n "$BASE" ]] && { echo "--disk and --base are mutually exclusive."; exit 1; }
[[ -f "$CLEAN_RULES" ]] || { echo "Missing $CLEAN_RULES"; exit 1; }

echo ""
echo "════════════════════════════════════════"
echo " RoomWizard offline commissioning"
echo "════════════════════════════════════════"

# ── 0. the backup ───────────────────────────────────────────────────────────
#
# Asked, not assumed, and asked FIRST. Everything below is destructive, the
# device has no serial console, and a failed boot yields no diagnostics at all —
# the only post-mortem is mounting p3 offline and reading `messages`, which only
# helps if it got as far as syslog (SYSTEM_ANALYSIS.md#312-serial-ports).
if [[ -z "$DRY" ]]; then
    echo ""
    warn "This rewrites the card: root password, host name, network, and a"
    warn "whitelist cleanup that deletes every vendor service it does not"
    warn "recognise. p1 is never touched, so a power cycle undoes nothing else."
    warn ""
    warn "PRECONDITION: a full-card image backup exists somewhere other than"
    warn "this card. Recovery from a bad boot means dd-ing it back."
    read -r -p "  Do you have that backup? (yes/no): " have_backup
    [[ "$have_backup" == "yes" ]] || { echo "  Make one first: sudo dd if=/dev/sdX of=card.img bs=4M status=progress"; exit 1; }
else
    warn "DRY RUN — every path is resolved and printed; nothing is written."
fi

# ── 1. find and mount the card ──────────────────────────────────────────────
echo ""
echo "────────────────────────────────────────"
echo " 1. The card"
echo "────────────────────────────────────────"

if [[ -n "$BASE" ]]; then
    BASE="${BASE%/}"
    info "Using an already-mounted card at $BASE (nothing will be mounted or unmounted)"
    [[ -d "$BASE" ]] || err "$BASE is not a directory"
else
    if [[ -z "$DISK" ]]; then
        info "Looking for a RoomWizard card..."
        # A read-only scan of partition tables. The host's own root disk is
        # excluded by RESOLUTION, not by name and not by the removable flag —
        # every disk on this host reports removable = 0, including the root disk,
        # and a wsl --mount'ed card lands in the same /dev/sd? namespace as /.
        CARDS=$(rw_find_card_disks || true)
        n=$(printf '%s' "$CARDS" | grep -c . || true)
        if [[ "$n" -eq 0 ]]; then
            err "No disk on this host carries the RoomWizard partition layout.
     On WSL, attach it from Windows first:  wsl --mount \\\\.\\PHYSICALDRIVEn --bare
     Then re-run, or name it:               --disk /dev/sdX"
        fi
        if [[ "$n" -gt 1 ]]; then
            echo "$CARDS" | sed 's/^/    /'
            err "More than one RoomWizard card is present — name one with --disk"
        fi
        DISK=$(printf '%s' "$CARDS" | awk '{print $1}')
    fi
    ok "Card: $DISK"

    rw_is_card_disk "$DISK" || err "$DISK does not carry the RoomWizard partition layout"
    # Checked here as well as inside rw_mount_card, because a --disk given by hand
    # is the one path where the operator could name the wrong device, and the
    # message wants to be about that rather than about mounting.
    if rw_is_host_root_disk "$DISK"; then
        err "$DISK is THIS HOST'S ROOT DISK — refusing"
    fi
    ok "Not this host's root disk (which is /dev/$(rw_host_root_disk 2>/dev/null || echo '?'))"

    printf '  the four trees, by position:\n'
    rw_card_partitions "$DISK" | sed 's/^/    /'

    if [[ -n "$DRY" ]]; then
        # A dry run must not need root, so it does not mount. Everything below
        # that needs a tree is skipped and said to be skipped, rather than
        # silently reporting a pass over nothing.
        warn "Dry run: not mounting. Re-run with --base <dir> against a mounted card"
        warn "to see the resolved paths."
    else
        [[ "$(id -u)" -eq 0 ]] || err "mounting needs root — re-run with sudo, or mount the four by hand and use --base"
        MOUNTED_BASE=$(mktemp -d /tmp/rw-commission.XXXXXX)
        info "Mounting the four partitions under $MOUNTED_BASE"
        rw_mount_card "$DISK" "$MOUNTED_BASE" | sed 's/^/    /' \
            || err "could not mount all four partitions"
        BASE="$MOUNTED_BASE"
    fi
fi

if [[ -n "$BASE" ]]; then
    # ⚠️ The negative half is the half that earns its keep: a rootfs where p2 was
    # expected means the partitions are in the wrong order, which would make every
    # path below resolve under the wrong tree and the clean report deleting nothing.
    if ! CHECK="$(rw_check_card_mounts "$BASE")"; then
        echo "$CHECK"
        err "the four mounts do not look right"
    fi
    ok "p6 is a rootfs, p2/p3/p5 are not: $(rw_rootfs_firmware "$BASE/root")"
    rw_is_rootfs_writable "$BASE/root" || err "$BASE/root is mounted read-only"
fi

# ── 2. the bundle ───────────────────────────────────────────────────────────
echo ""
echo "────────────────────────────────────────"
echo " 2. The bundle"
echo "────────────────────────────────────────"

if [[ -d "$BUNDLE" ]]; then
    BUNDLE_DIR="$(cd "$BUNDLE" && pwd)"
    info "Staged bundle directory: $BUNDLE_DIR"
else
    TMPROOT=$(mktemp -d /tmp/rw-bundle.XXXXXX)
    BUNDLE_DIR="$TMPROOT/bundle"
    mkdir -p "$BUNDLE_DIR"
    info "Unpacking $BUNDLE"
    tar -xzf "$BUNDLE" -C "$BUNDLE_DIR" || err "could not unpack $BUNDLE"
fi

if ! CHECK="$(rw_bundle_check "$BUNDLE_DIR")"; then
    echo "$CHECK"
    err "the bundle is not self-consistent — do not install it"
fi
ok "Every manifest entry is staged, and every staged file is in a manifest"

[[ -f "$BUNDLE_DIR/manifest.d/bundle.info" ]] && sed 's/^/    /' "$BUNDLE_DIR/manifest.d/bundle.info"
BUNDLE_FILES=$(rw_bundle_entries "$BUNDLE_DIR" | grep -c . || true)
[[ "$BUNDLE_FILES" -gt 0 ]] || err "the bundle contains no files at all"
info "$BUNDLE_FILES file(s), components: $(rw_bundle_components "$BUNDLE_DIR" | tr '\n' ' ')"

# ── ARM safety, on the DOWNLOADED binaries ──────────────────────────────────
#
# A binary nobody built on the spot is exactly what check-arm-safe.sh is for: the
# Cortex-A8 has no hardware integer divide, and an sdiv/udiv INSTRUCTION dies with
# SIGILL — blank screen, no output, no log, indistinguishable from "the app didn't
# start". So the gate runs here too, over the bundle rather than over build/.
#
# ⚠️ The count is asserted, not the exit status alone. check-arm-safe.sh skips
# non-ARM files and then reports success — "no hardware divide in 0 binaries" is a
# pass over nothing, and that is the failure this block exists to make impossible.
info "Checking the bundle's ARM binaries..."
ELF_LIST="$TMPROOT/elf.list"
[[ -n "$TMPROOT" ]] || { TMPROOT=$(mktemp -d /tmp/rw-bundle.XXXXXX); ELF_LIST="$TMPROOT/elf.list"; }
: > "$ELF_LIST"
while read -r _mode dev; do
    [[ -n "$dev" ]] || continue
    f="$BUNDLE_DIR/root$dev"
    [[ -f "$f" ]] || continue
    # ELF magic read directly. `file` is not guaranteed present and a .app or a
    # .ppm must not be handed to objdump — skipping what a tool cannot inspect,
    # rather than passing it through, is what keeps the count honest.
    [[ "$(head -c 4 "$f" | od -An -tx1 | tr -d ' \n')" == "7f454c46" ]] || continue
    printf '%s\n' "$f" >> "$ELF_LIST"
done < <(rw_bundle_entries "$BUNDLE_DIR")

ELF_COUNT=$(grep -c . "$ELF_LIST" || true)
[[ "$ELF_COUNT" -gt 0 ]] || err "the bundle contains no ELF binaries — it cannot be a RoomWizard app bundle"

OBJDUMP="${OBJDUMP:-arm-linux-gnueabihf-objdump}"
if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
    echo ""
    echo -e "${RED}  ╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${RED}  ║  $OBJDUMP IS NOT INSTALLED.${NC}"
    echo -e "${RED}  ║${NC}"
    echo -e "${RED}  ║  $ELF_COUNT ARM binaries in this bundle were NOT CHECKED for the${NC}"
    echo -e "${RED}  ║  Cortex-A8 hardware-divide instruction. If one carries an sdiv or${NC}"
    echo -e "${RED}  ║  udiv, the app dies with SIGILL the moment it is tapped: black${NC}"
    echo -e "${RED}  ║  screen, no output, no log, nothing in dmesg you would look for.${NC}"
    echo -e "${RED}  ║${NC}"
    echo -e "${RED}  ║  Fix it:   sudo apt install binutils-arm-linux-gnueabihf${NC}"
    echo -e "${RED}  ║  Or run:   native_apps/check-arm-safe.sh <files>  on a build host${NC}"
    echo -e "${RED}  ║  Or force: --arm-check=skip${NC}"
    echo -e "${RED}  ╚════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    [[ "$ARM_CHECK" == "skip" ]] || err "refusing to install unverified ARM binaries"
    warn "--arm-check=skip given: installing $ELF_COUNT UNVERIFIED binaries"
    ARM_VERIFIED="NOT CHECKED — $OBJDUMP absent"
else
    # xargs rather than $(cat): the list can be long, and a path with a space in
    # it would otherwise be split into two arguments that both fail to exist.
    if ! xargs -d '\n' -a "$ELF_LIST" bash "$SCRIPT_DIR/native_apps/check-arm-safe.sh"; then
        err "a bundled binary would SIGILL on this device — do not install it"
    fi
    ARM_VERIFIED="$ELF_COUNT binaries, hard zero"
fi

if [[ -z "$BASE" ]]; then
    echo ""
    warn "Dry run without a mounted card: the bundle and the rules were checked,"
    warn "and nothing further can be resolved. Mount the four and use --base to"
    warn "see every path this would write and delete."
    cleanup_and_exit 0
fi

# ── 3. name and password: commission-roomwizard.sh, not a second copy ───────
echo ""
echo "────────────────────────────────────────"
echo " 3. Name, password, SSH and DHCP"
echo "────────────────────────────────────────"

if [[ -n "$DRY" ]]; then
    warn "Dry run: skipping commission-roomwizard.sh (it prompts and writes)"
else
    # ROOTFS is its documented escape hatch, and passing it skips its own
    # detection — which is what makes this an orchestration rather than a second
    # implementation of the two prompts. `bash <script>`, never ./<script>: a
    # fresh clone can land without the executable bit and /mnt/c cannot even show
    # whether it has one.
    info "Handing over to commission-roomwizard.sh for the two questions..."
    echo ""
    ROOTFS="$BASE/root" bash "$SCRIPT_DIR/commission-roomwizard.sh" \
        || err "commission-roomwizard.sh failed — the card is half-written; fix it before booting"
    ok "Password, host name, /etc/hosts, /etc/dhclient.conf, sshd and DHCP done"
fi

# ── 4. clean ────────────────────────────────────────────────────────────────
echo ""
echo "────────────────────────────────────────"
echo " 4. Clean"
echo "────────────────────────────────────────"

if [[ "$DO_CLEAN" -eq 0 ]]; then
    warn "--no-clean: the vendor stack is left exactly as it is."
    warn "⚠️ That leaves /home/root/data/websign in place, so the boot-time"
    warn "   regenerator will overwrite the host name just set (D7b)."
else
    if ! CHECK="$(rw_clean_validate "$CLEAN_RULES")"; then
        echo "$CHECK"
        err "device-files/clean-rules.conf does not validate"
    fi
    CLEAN_GROUPS="base"
    for g in $(rw_clean_optional_groups); do
        case " $KEEP_GROUPS " in
            *" $g "*) ;;
            *) CLEAN_GROUPS="$CLEAN_GROUPS $g" ;;
        esac
    done
    [[ "$DEL_FACTORY" -eq 1 ]] && CLEAN_GROUPS="$CLEAN_GROUPS factory"
    [[ -n "$KEEP_GROUPS" ]] && info "Keeping:$KEEP_GROUPS"
    info "Groups: $CLEAN_GROUPS"

    PLAN="$TMPROOT/clean.plan"
    [[ -n "$TMPROOT" ]] || { TMPROOT=$(mktemp -d /tmp/rw-bundle.XXXXXX); PLAN="$TMPROOT/clean.plan"; }
    rw_clean_plan "$CLEAN_RULES" "$CLEAN_GROUPS" > "$PLAN" || err "could not compile the clean plan"
    info "$(grep -c '^del' "$PLAN") delete(s), $(grep -c '^sweep' "$PLAN") sweep(s), $(grep -c '^keep' "$PLAN") protected path(s)"
    echo ""
    RW_CLEAN_DRY="$DRY" rw_clean_apply "$BASE" "$PLAN" || err "the clean failed"
    echo ""
    ok "Clean complete"
fi

# ── 5. install ──────────────────────────────────────────────────────────────
echo ""
echo "────────────────────────────────────────"
echo " 5. Install"
echo "────────────────────────────────────────"

# put MODE DEVICE_PATH SOURCE
#
# One writer for everything installed, so the mode is applied in exactly one
# place. The mode is DECLARED by the caller and never read off disk: /mnt/c is
# DrvFs, reports every file 0777 and discards chmod, so a mode derived from the
# source here would be a constant rather than a measurement (CLAUDE.md).
INSTALLED=()
put() {
    local mode="$1" dev="$2" src="$3" dest
    dest=$(rw_clean_offline_path "$BASE" "$dev") || { err "cannot resolve $dev"; }
    case "$dest/" in
        "${BASE%/}"/*) ;;
        *) err "$dev resolved to $dest, outside $BASE — refusing" ;;
    esac
    if [[ -n "$DRY" ]]; then
        printf '  would write   %-5s %s\n' "$mode" "$dest"
        return 0
    fi
    mkdir -p "$(dirname "$dest")"
    cp "$src" "$dest"
    chmod "$mode" "$dest"
    INSTALLED+=("$mode|$dev|$dest")
}

# ── 5a. the boot scripts, from device-files/ ────────────────────────────────
info "Boot scripts"
for f in audio-enable time-sync; do
    [[ -f "$DEVICE_FILES/$f" ]] || err "missing $DEVICE_FILES/$f"
    put 0755 "/etc/init.d/$f" "$DEVICE_FILES/$f"
done
[[ -f "$DEVICE_FILES/99-security.conf" ]] || err "missing $DEVICE_FILES/99-security.conf"
put 0644 /etc/sysctl.d/99-security.conf "$DEVICE_FILES/99-security.conf"

# The app respawn loop and the bypass script, from the repo root — the same two
# files setup-device.sh pushes, and the two whose staleness a live device reports
# through `--status`.
put 0755 /etc/init.d/roomwizard-app "$SCRIPT_DIR/roomwizard-app-init.sh"
put 0755 /opt/roomwizard/disable-steelcase.sh "$SCRIPT_DIR/disable-steelcase.sh"

# ── 5b. the boot links ─────────────────────────────────────────────────────
#
# The numbers here and the keep-list in device-files/clean-rules.conf are the same
# facts: a link this creates that the whitelist does not name is swept on the next
# clean, so the two are written together or not at all.
link_boot() {
    local rcdir="$1" name="$2" target="$3" dest
    dest="$BASE/root/etc/$rcdir/$name"
    if [[ -n "$DRY" ]]; then
        printf '  would link    %s -> %s\n' "$dest" "$target"
        return 0
    fi
    [[ -d "$BASE/root/etc/$rcdir" ]] || mkdir -p "$BASE/root/etc/$rcdir"
    ln -sf "$target" "$dest"
}
info "Boot links"
link_boot rc5.d S28time-sync    ../init.d/time-sync
link_boot rc5.d S29audio-enable ../init.d/audio-enable
for d in rc2.d rc3.d rc4.d rc5.d; do
    link_boot "$d" S99roomwizard-app ../init.d/roomwizard-app
done

# mDNS. The vendor image already carries /usr/sbin/avahi-daemon and a full
# /etc/init.d/avahi-daemon and ships NO rc5.d link, so adding the link is the
# whole change. clean-rules.conf keeps all four paths, which is what closes D8 —
# the deep clean used to delete the daemon this link points at.
if [[ -x "$BASE/root/etc/init.d/avahi-daemon" && -f "$BASE/root/usr/sbin/avahi-daemon" ]]; then
    link_boot rc5.d S30avahi-daemon ../init.d/avahi-daemon
    ok "mDNS enabled — <name>.local resolves after the first boot"
else
    warn "avahi-daemon is not on this image — <name>.local will not resolve"
fi

# The software-watchdog bypass, offline. /var is NOT tmpfs (only /var/volatile is,
# per /etc/fstab), so this persists across the boot.
# ⚠️ It is belt and braces only: disable-steelcase.sh touches the same file as its
# FIRST command on every boot, and D9 is the open question of why a unit in
# service does not have it — possibly cleanupfiles.sh (cron, every 4 h) sweeping
# it. The crontab this run truncates is what would have scheduled the watchdog.
if [[ -n "$DRY" ]]; then
    printf '  would touch   %s\n' "$BASE/root/var/watchdog_test"
else
    mkdir -p "$BASE/root/var"
    : > "$BASE/root/var/watchdog_test"
fi
ok "Steelcase software-watchdog bypass in place (/var/watchdog_test)"

# ── 5c. sshd hardening ─────────────────────────────────────────────────────
#
# commission-roomwizard.sh turns root login and key auth ON; this is the other
# half, and the factory default is the reason it cannot wait for a second phase:
# the shipped sshd_config has PermitEmptyPasswords yes.
#
# PermitRootLogin stays "yes": root is the only account in /etc/passwd, there is
# no adduser on the device, and every deploy path is root@<ip>.
SSHD="$BASE/root/etc/ssh/sshd_config"
if [[ -f "$SSHD" ]]; then
    if [[ -n "$DRY" ]]; then
        printf '  would harden  %s\n' "$SSHD"
    else
        [[ -f "$SSHD.orig" ]] || cp "$SSHD" "$SSHD.orig"
        sed -i 's/^PermitEmptyPasswords yes/PermitEmptyPasswords no/' "$SSHD"
        grep -q '^MaxAuthTries'   "$SSHD" || echo 'MaxAuthTries 3'   >> "$SSHD"
        grep -q '^LoginGraceTime' "$SSHD" || echo 'LoginGraceTime 30' >> "$SSHD"
        grep -q '^MaxSessions'    "$SSHD" || echo 'MaxSessions 5'     >> "$SSHD"
        grep -q '^PermitEmptyPasswords no' "$SSHD" \
            || warn "could not set PermitEmptyPasswords=no — check $SSHD by hand"
    fi
    ok "sshd hardened (PermitEmptyPasswords=no, MaxAuthTries=3)"
fi

# ── 5d. the bundle ─────────────────────────────────────────────────────────
info "Bundle: $BUNDLE_FILES file(s)"
while read -r mode dev; do
    [[ -n "$dev" ]] || continue
    put "$mode" "$dev" "$BUNDLE_DIR/root$dev"
done < <(rw_bundle_entries "$BUNDLE_DIR")
ok "Installed"

# ── 6. verify ───────────────────────────────────────────────────────────────
echo ""
echo "────────────────────────────────────────"
echo " 6. Verify"
echo "────────────────────────────────────────"

if [[ -n "$DRY" ]]; then
    warn "Dry run: nothing was written, so there is nothing to verify."
    cleanup_and_exit 0
fi

VBAD=0
vfail() { VBAD=$((VBAD + 1)); echo -e "${RED}  ✗ $*${NC}"; }

# ── md5, against the bundle's own manifest, reading the INSTALLED file ─────
# The point is the bytes that landed on the card, not the bytes that were staged:
# a truncated write on a full or failing card is exactly what this catches, and it
# reports success just as loudly if you check the source instead.
MD5_CHECKED=0
for m in "$BUNDLE_DIR"/manifest.d/*.md5; do
    [[ -f "$m" ]] || continue
    while read -r want dev; do
        [[ -n "$dev" ]] || continue
        dest=$(rw_clean_offline_path "$BASE" "$dev")
        if [[ ! -f "$dest" ]]; then
            vfail "not installed: $dev"
            continue
        fi
        got=$(md5sum "$dest" | cut -d' ' -f1)
        MD5_CHECKED=$((MD5_CHECKED + 1))
        [[ "$got" == "$want" ]] || vfail "md5 mismatch: $dev (want $want, got $got)"
    done < "$m"
done
if [[ "$MD5_CHECKED" -eq "$BUNDLE_FILES" ]]; then
    ok "md5: all $MD5_CHECKED installed file(s) match the bundle manifest"
else
    vfail "md5 checked $MD5_CHECKED of $BUNDLE_FILES files — the manifests do not cover the bundle"
fi

# ── the executable bit ─────────────────────────────────────────────────────
# Real ext4 honours it, unlike /mnt/c, so this is a MEASUREMENT here and could
# not be one on the dev host. A missing +x on app_launcher is the failure that
# cannot be reproduced from Windows at all.
XCOUNT=0
for entry in "${INSTALLED[@]}"; do
    mode="${entry%%|*}"; rest="${entry#*|}"; dev="${rest%%|*}"; dest="${rest##*|}"
    case "$mode" in
        *[1357]|*[1357][0-9]|*[1357][0-9][0-9]) ;;   # owner-execute set
        *) continue ;;
    esac
    XCOUNT=$((XCOUNT + 1))
    [[ -x "$dest" ]] || vfail "declared mode $mode but not executable on the card: $dev"
done
[[ "$VBAD" -eq 0 ]] && ok "+x: all $XCOUNT file(s) declared executable are executable"

# ── every .app's exec= exists and is executable ────────────────────────────
# A manifest whose exec= names a binary that is not there renders a launcher tile
# that does nothing when tapped — which looks exactly like a broken touch panel.
APPS_DIR="$BASE/root/opt/roomwizard/apps"
APPCOUNT=0
EXECS=""
if [[ -d "$APPS_DIR" ]]; then
    for a in "$APPS_DIR"/*.app; do
        [[ -f "$a" ]] || continue
        APPCOUNT=$((APPCOUNT + 1))
        e=$(sed -n 's/^exec=//p' "$a" | head -1 | tr -d '\r')
        if [[ -z "$e" ]]; then
            vfail "$(basename "$a"): no exec= line"
            continue
        fi
        EXECS="$EXECS $e"
        d=$(rw_clean_offline_path "$BASE" "$e")
        if [[ ! -f "$d" ]]; then
            vfail "$(basename "$a"): exec=$e is not installed"
        elif [[ ! -x "$d" ]]; then
            vfail "$(basename "$a"): exec=$e is installed but not executable"
        fi
        # icon= too: a tile with no icon renders, but as a hole in the grid.
        i=$(sed -n 's/^icon=//p' "$a" | head -1 | tr -d '\r')
        if [[ -n "$i" ]]; then
            di=$(rw_clean_offline_path "$BASE" "$i")
            [[ -f "$di" ]] || vfail "$(basename "$a"): icon=$i is not installed"
        fi
    done
fi
if [[ "$APPCOUNT" -eq 0 ]]; then
    vfail "no .app manifests were installed — the launcher would render an empty grid"
else
    ok ".app: all $APPCOUNT manifest(s) name an installed, executable binary"
fi

# ── default-app names one of them ──────────────────────────────────────────
DEFAULT_APP_FILE="$BASE/root/opt/roomwizard/default-app"
if [[ ! -f "$DEFAULT_APP_FILE" ]]; then
    vfail "no /opt/roomwizard/default-app — /etc/init.d/roomwizard-app would start nothing"
else
    da=$(head -1 "$DEFAULT_APP_FILE" | tr -d ' \t\r\n')
    dad=$(rw_clean_offline_path "$BASE" "$da")
    if [[ ! -x "$dad" ]]; then
        vfail "default-app is '$da', which is not installed or not executable"
    else
        ok "default-app: $da (installed, executable)"
    fi
    # It should be the launcher, or at least something with a tile — a default-app
    # that no manifest mentions boots straight into one game with no way back.
    case " $EXECS $da " in
        *" $da "*) ;;
        *) warn "default-app '$da' is in no .app manifest — nothing returns to a launcher grid" ;;
    esac
fi

# ── dash -n every /bin/sh script written ───────────────────────────────────
# A parse error or a CRLF in an init script does not fail at install time: it
# fails at boot, on a device with no serial console. `dash` is the closest thing
# this host has to BusyBox ash.
#
# ⚠️ It catches parse errors and CRLF, NOT bashisms. `[[ -n "$x" ]]` parses fine
# under dash — `[[` is read as a command name — so it passes here and fails at
# boot with "[[: not found". Measured while writing
# tests/commission_offline_test.sh case 2e. Catching that needs shellcheck, which
# is not installed in this WSL (IMPROVEMENT_PLAN.md C7).
SHCHECK="dash"
command -v dash >/dev/null 2>&1 || SHCHECK="sh"
SHCOUNT=0
for entry in "${INSTALLED[@]}"; do
    dest="${entry##*|}"
    [[ -f "$dest" ]] || continue
    read -r shebang < "$dest" 2>/dev/null || continue
    case "$shebang" in
        '#!/bin/sh'*|'#! /bin/sh'*) ;;
        *) continue ;;
    esac
    SHCOUNT=$((SHCOUNT + 1))
    case "$shebang" in
        *$'\r'*) vfail "CRLF shebang (BusyBox rejects it as 'not found'): ${entry#*|}" ;;
    esac
    "$SHCHECK" -n "$dest" 2>/dev/null || vfail "$SHCHECK -n failed: ${entry#*|}"
done
if [[ "$SHCOUNT" -eq 0 ]]; then
    vfail "no /bin/sh scripts were checked — the init scripts should have been among them"
else
    ok "$SHCHECK -n: all $SHCOUNT /bin/sh script(s) parse"
fi

# ── the boot links resolve ─────────────────────────────────────────────────
LINKBAD=0
for l in "$BASE/root/etc/rc5.d/S28time-sync" "$BASE/root/etc/rc5.d/S29audio-enable" \
         "$BASE/root/etc/rc5.d/S99roomwizard-app"; do
    [[ -L "$l" ]] || { vfail "missing boot link: ${l#$BASE/root}"; LINKBAD=1; continue; }
    # A relative link resolves against its own directory, so test it from there —
    # `[ -e "$link" ]` from the wrong cwd is a dangling-link false positive.
    ( cd "$(dirname "$l")" && [ -e "$(readlink "$l")" ] ) \
        || { vfail "dangling boot link: ${l#$BASE/root} -> $(readlink "$l")"; LINKBAD=1; }
done
[[ "$LINKBAD" -eq 0 ]] && ok "boot links resolve (S28, S29, S99)"

# ── websign is gone, i.e. D7b's window is closed ───────────────────────────
if [[ "$DO_CLEAN" -eq 1 ]]; then
    if [[ -e "$BASE/data/websign" ]]; then
        vfail "/home/root/data/websign survives — the boot-time regenerator will overwrite the host name (D7b)"
    elif [[ -e "$BASE/root/etc/rcS.d/S60networkmanager" ]]; then
        vfail "/etc/rcS.d/S60networkmanager survives — the vendor dhclient-script will rewrite /etc/hosts (D7b)"
    else
        ok "D7b closed: websign and S60networkmanager are both gone"
    fi
    hn=$(head -1 "$BASE/root/etc/hostname" 2>/dev/null | tr -d ' \t\r\n')
    dh=$(sed -n 's/^send host-name "\(.*\)";.*/\1/p' "$BASE/root/etc/dhclient.conf" 2>/dev/null | head -1)
    if [[ -n "$dh" && "$dh" != "$hn" ]]; then
        vfail "/etc/hostname says '$hn' but /etc/dhclient.conf announces '$dh'"
    elif [[ -n "$hn" ]]; then
        ok "host name '$hn' is consistent across /etc/hostname, /etc/hosts and dhclient.conf"
    fi
fi

echo ""
if [[ "$VBAD" -gt 0 ]]; then
    echo -e "${RED}  ✗ $VBAD verification failure(s) — do NOT boot this card until they are understood${NC}"
    cleanup_and_exit 1
fi

echo "════════════════════════════════════════"
echo " Done"
echo "════════════════════════════════════════"
ok "ARM safety: $ARM_VERIFIED"
ok "$BUNDLE_FILES bundled file(s) installed and md5-verified on the card"
echo ""
echo "  Put the card back and power the unit on. ONE boot."
echo ""
echo "  Then verify, in this order — each one is cheap and rules out the next:"
echo "    1. it comes up as a launcher grid, not a black screen"
echo "    2. ssh root@$(head -1 "$BASE/root/etc/hostname" 2>/dev/null | tr -d ' \t\r\n').local   (or find it in the DHCP leases)"
echo "    3. tap a game; it plays and exiting returns to the grid"
echo "    4. sound: Device Tools -> Audio, or Tap-a-Theremin"
echo "    5. touch: Device Tools -> Display -> CALIBRATE TOUCH — the one step"
echo "       that still needs the panel, because it is per-unit."
echo ""
cleanup_and_exit 0
