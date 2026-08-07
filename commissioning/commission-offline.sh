#!/bin/bash
#
# commissioning/commission-offline.sh — put a RoomWizard card in a reader, answer two
#                         questions, put it back, and the device BOOTS WORKING.
#
# IMPROVEMENT_PLAN.md F10, step 4.
#
# Usage:
#   sudo ./commissioning/commission-offline.sh --bundle <file.tar.gz|dir> [options]
#   sudo ./commissioning/commission-offline.sh --bundle <b> --dry-run
#        ./commissioning/commission-offline.sh --bundle <b> --base /mnt/rw   # already mounted
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
# (commissioning/card-prep.sh -> boot -> commissioning/provision.sh -> deploy-all.sh). That is
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
#   the two operator prompts   ROOTFS=<mnt> commissioning/card-prep.sh already asks
#                              exactly the password and host-name questions, and
#                              does shadow / sshd / DHCP / the SSH key. This
#                              orchestrates it.
#   the host name              commissioning/set-hostname.sh, which owns /etc/hostname,
#                              /etc/hosts AND /etc/dhclient.conf (D7b item 3).
#   which card, which mount    lib/rw-identify.sh, by content and by POSITION, never
#                              by UUID. p1 is unreachable through it by design.
#   what to delete             device-files/clean-rules.conf, shared with
#                              commissioning/provision.sh --remove/--deep-clean so the two
#                              cannot drift.
#   what to install            device-files/provision-rules.conf — the boot scripts,
#                              the rc*.d links, the sshd directives and the config
#                              fix-ups, shared with commissioning/provision.sh for the same
#                              reason. Modes are DECLARED there, never read off disk.
#   which binaries             the bundle's own manifest (lib/rw-bundle.sh). Modes are
#                              DECLARED there too.
#
# ── Still needs the device ─────────────────────────────────────────────────
#
# Touch calibration only — it is per-unit and per-panel, and the wizard lives in
# Device Tools -> Display -> CALIBRATE TOUCH. One boot remains; this removes two
# of the three, plus the IP hunt.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# shellcheck source=../lib/rw-identify.sh
. "$REPO_ROOT/lib/rw-identify.sh"
# shellcheck source=../lib/rw-clean.sh
. "$REPO_ROOT/lib/rw-clean.sh"
# shellcheck source=../lib/rw-provision.sh
. "$REPO_ROOT/lib/rw-provision.sh"
# shellcheck source=../lib/rw-bundle.sh
. "$REPO_ROOT/lib/rw-bundle.sh"

DEVICE_FILES="$REPO_ROOT/device-files"
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
NO_PROV_GROUPS=""
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
  --keep-<group>     Leave one vendor stack on disk. Groups:
                     $(rw_clean_optional_groups)
                     Files only — it does not re-enable a boot link, because the
                     rc*.d whitelist is what removes an unknown vendor service.
                     --keep-factory keeps the 472 MB on-device restore payload,
                     which is otherwise deleted like the rest of the vendor stack.
  --delete-factory   Accepted and now redundant: that is the default.
  --no-<group>       Skip one group of the provision plan. Groups:
                     $(rw_provision_optional_groups)
                     --no-mdns leaves <name>.local unresolvable; --no-sshd leaves
                     PermitEmptyPasswords at the factory "yes". Both are in
                     device-files/provision-rules.conf with their reasons.
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
        --delete-factory) DEL_FACTORY=1; shift ;;   # a no-op since 2026-08-06; see below
        --arm-check=skip) ARM_CHECK="skip"; shift ;;
        --no-*)
            g="${1#--no-}"
            case " $(rw_provision_optional_groups) " in
                *" $g "*) NO_PROV_GROUPS="$NO_PROV_GROUPS $g" ;;
                *) echo "Unknown provision group: $g"; echo "  --no- accepts: $(rw_provision_optional_groups)"; exit 1 ;;
            esac
            shift ;;
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
    warn "The Steelcase software does not come back afterwards. That includes the"
    warn "472 MB on-device factory-restore payload, which is deleted with the rest"
    warn "of the vendor stack — it would only restore software whose start-up this"
    warn "same clean removes. --keep-factory opts out. The 5 MB fallback kernel is"
    warn "kept either way."
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
            # The not-root case first, because it is the one that produces an empty
            # scan on a host that HAS a card in the reader: rw_find_card_disks
            # identifies by partition table and sfdisk -d needs root, so every
            # candidate silently fails to match and the scan looks conclusive.
            if [[ "$(id -u)" -ne 0 ]]; then
                err "No card found — but this ran as $(id -un 2>/dev/null || echo "uid $(id -u)"), and
     identifying a card reads its partition table, which needs root. A non-root
     scan finds nothing even with the card in the reader. Re-run with sudo before
     concluding anything about the card."
            fi
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

    # Why this precedes rw_is_card_disk: that function returns 1 both for "not a
    # RoomWizard" and for "could not read the table", and reporting the second as
    # the first sends the operator to inspect the card when the fix is `sudo`.
    # Measured 2026-08-05: a non-root run on a genuine unit's card said
    # "does not carry the RoomWizard partition layout".
    if ! rw_can_read_partition_table "$DISK"; then
        if ! command -v sfdisk >/dev/null 2>&1; then
            err "sfdisk is not installed, so no disk can be identified.
     Fix it:  sudo apt install util-linux"
        fi
        if [[ "$(id -u)" -ne 0 ]]; then
            err "cannot read $DISK's partition table as $(id -un 2>/dev/null || echo "uid $(id -u)").
     \`sfdisk -d\` needs root, so a non-root run cannot identify a card AT ALL —
     this is not a statement about the card. Re-run with sudo."
        fi
        err "cannot read $DISK's partition table even as root — is the card seated?
     Check:  sudo dmesg | tail -20"
    fi

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
    if ! xargs -d '\n' -a "$ELF_LIST" bash "$REPO_ROOT/native_apps/check-arm-safe.sh"; then
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

# ── 3. name and password: commissioning/card-prep.sh, not a second copy ───────
echo ""
echo "────────────────────────────────────────"
echo " 3. Name, password, SSH and DHCP"
echo "────────────────────────────────────────"

if [[ -n "$DRY" ]]; then
    warn "Dry run: skipping commissioning/card-prep.sh (it prompts and writes)"
else
    # ROOTFS is its documented escape hatch, and passing it skips its own
    # detection — which is what makes this an orchestration rather than a second
    # implementation of the two prompts. `bash <script>`, never ./<script>: a
    # fresh clone can land without the executable bit and /mnt/c cannot even show
    # whether it has one.
    #
    # RW_COMMISSION_ORCHESTRATED suppresses its closing "Commissioning Complete!"
    # banner and the next-steps block it reads out of COMMISSIONING.md, which tell
    # the operator to boot the unit and then run commissioning/provision.sh and deploy-all.sh
    # — everything phases 4-6 below are about to do. It is a separate flag from
    # ROOTFS on purpose: ROOTFS alone also means "I mounted the card myself", and
    # that operator does still need the next steps.
    info "Handing over to commissioning/card-prep.sh for the two questions..."
    echo ""
    ROOTFS="$BASE/root" RW_COMMISSION_ORCHESTRATED=1 \
        bash "$SCRIPT_DIR/card-prep.sh" \
        || err "commissioning/card-prep.sh failed — the card is half-written; fix it before booting"
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
    # Every group in the default set, minus each --keep-<group>. `factory` is in
    # that default set as of 2026-08-06 (IMPROVEMENT_PLAN.md C11): cleaning a unit
    # of its vendor software is a decision, and the payload that would undo it is
    # 472 MB restoring a stack whose start-up mechanism this same clean removes.
    # --keep-factory is the opt-out; the phase-0 backup question is the gate.
    CLEAN_GROUPS=""
    for g in $(rw_clean_default_groups); do
        case " $KEEP_GROUPS " in
            *" $g "*) ;;
            *) CLEAN_GROUPS="$CLEAN_GROUPS $g" ;;
        esac
    done
    [[ "$DEL_FACTORY" -eq 1 ]] && info "--delete-factory is the default now; no need to pass it"
    [[ -n "$KEEP_GROUPS" ]] && info "Keeping:$KEEP_GROUPS"
    info "Groups:$CLEAN_GROUPS"

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

# ── 5a. the provision plan: boot scripts, links, sshd, config edits ─────────
#
# ⚠️ The decisions are NOT here. Every file, link, mode and config edit lives in
# device-files/provision-rules.conf with a reason per entry, read by BOTH this
# script and commissioning/provision.sh, so the two cannot drift (IMPROVEMENT_PLAN.md C12).
# They HAD drifted: the online path removed stale rc*.d links before relinking and
# this one did not, so a card carrying an old S50roomwizard-app came out of offline
# commissioning with two links to one init script at two priorities.
#
# What used to be here: five put() calls, seven link_boot() calls, a bare touch of
# /var/watchdog_test, and a four-command sed block over sshd_config — every one of
# them written out a second time in commissioning/provision.sh.
PROV_RULES="$DEVICE_FILES/provision-rules.conf"
[[ -f "$PROV_RULES" ]] || err "missing $PROV_RULES"
if ! PCHECK="$(rw_provision_validate "$PROV_RULES" "$REPO_ROOT")"; then
    echo "$PCHECK"
    err "device-files/provision-rules.conf does not validate — refusing to install"
fi
# The cross-file invariant: a boot link the clean's whitelist does not name is
# deleted by the next --deep-clean, so the unit boots right once and then loses it.
if ! KCHECK="$(rw_provision_check_keeps "$PROV_RULES" "$CLEAN_RULES")"; then
    echo "$KCHECK"
    err "a boot link in provision-rules.conf is not kept by clean-rules.conf"
fi

PROV_GROUPS="base"
for g in $(rw_provision_optional_groups); do
    case " $NO_PROV_GROUPS " in
        *" $g "*) ;;
        *) PROV_GROUPS="$PROV_GROUPS $g" ;;
    esac
done
[[ -n "$NO_PROV_GROUPS" ]] && info "Skipping:$NO_PROV_GROUPS"

PROV_PLAN="$TMPROOT/provision.plan"
[[ -n "$TMPROOT" ]] || { TMPROOT=$(mktemp -d /tmp/rw-bundle.XXXXXX); PROV_PLAN="$TMPROOT/provision.plan"; }
rw_provision_plan "$PROV_RULES" "$PROV_GROUPS" > "$PROV_PLAN" \
    || err "could not compile the provision plan"
info "Provision plan: $(grep -c . "$PROV_PLAN") action(s) — $(grep -c '^install' "$PROV_PLAN") install, $(grep -c '^link' "$PROV_PLAN") link, $(grep -c '^unlink' "$PROV_PLAN") unlink"
RW_PROVISION_DRY="$DRY" rw_provision_apply_offline "$BASE" "$PROV_PLAN" "$REPO_ROOT" \
    || err "the provision step failed"

# Feed the plan's declared modes into the +x measurement below. Reading them from
# the plan rather than having the executor export an array keeps one authority for
# "what mode was declared" — the data file.
if [[ -z "$DRY" ]]; then
    while IFS=$'\t' read -r pkind pmode ptarget psrc; do
        case "$pkind" in install|touch) ;; *) continue ;; esac
        pdest=$(rw_clean_offline_path "$BASE" "$ptarget") || continue
        INSTALLED+=("$pmode|$ptarget|$pdest")
    done < "$PROV_PLAN"
fi
ok "Boot scripts, boot links, sshd and the config fix-ups done"

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
echo "  Not installed, and no bundle can install it: USB HOST MODE. It needs the"
echo "  usb_host component, which patches the DTB inside uImage-system on p1 — the"
echo "  partition this tool refuses to touch, because an untouched p1 is what keeps"
echo "  a power cycle a free undo. To add it later, from a machine with the ARM"
echo "  toolchain:   cd usb_host && ./build-and-deploy.sh <ip>"
echo "  (IMPROVEMENT_PLAN.md F15.)"
echo ""
cleanup_and_exit 0
