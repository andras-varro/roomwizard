#!/bin/bash
#
# roomwizard.sh — the front door: one menu over the bring-up paths
#
# This script implements NOTHING of its own. Every item shells out to the
# existing script with arguments, so anything that works today keeps working
# exactly as it did, and those scripts stay non-interactive when called directly:
#
#   commission-roomwizard.sh   Phase 1, offline, needs a mounted card + sudo
#   setup-device.sh            Phase 2, over SSH, ends in a reboot
#   deploy-all.sh              Phase 3, over SSH, per-component
#   commission-offline.sh      all three at once, offline, ONE boot
#
# Why a composition layer and not one merged script: the three phases have
# genuinely different connection models, and the cleanup in Phase 2 touches paths
# spread across FOUR partitions that only a booted kernel assembles into one tree,
# while commissioning locates just p6. commission-offline.sh is what does mount
# all four and map every absolute path onto them (IMPROVEMENT_PLAN.md F10); the
# SSH phases stay as the verified development loop. There are three further
# reasons in COMMISSIONING.md ("why these are separate").
#
# What this script does add is the two things missing between the phases: a
# wait_for_ssh so the operator is not guessing when a rebooted device is back,
# and a single place that knows the phases run in order.
#
# Every child is invoked as `bash <script>`, not `./<script>`. A clone can land
# without the executable bit — the mode lives in the git index, so one bad commit
# breaks every fresh clone — and `./` then fails at the point of use with
# "Permission denied". Nothing is in doubt about which interpreter to use; all
# three are #!/bin/bash. deploy-all.sh already does the same for the
# per-component scripts it discovers.
#
# Usage:
#   ./roomwizard.sh          # interactive menu
#   bash roomwizard.sh       # ... if this file itself lost its +x
#   ./roomwizard.sh --help

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── colour helpers (same vocabulary as the three child scripts) ──────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; }
hdr()  { echo ""; echo -e "${CYAN}════════════════════════════════════════${NC}";
         echo -e "${CYAN} $*${NC}";
         echo -e "${CYAN}════════════════════════════════════════${NC}"; }

usage() {
    cat <<'USAGE'
Usage: ./roomwizard.sh

Interactive front door for RoomWizard bring-up. Menu-driven; it takes no
arguments of its own beyond --help, and reimplements none of the flags of the
scripts it calls. To script a step, call that script directly:

  ./commission-roomwizard.sh                    Phase 1 (offline, needs sudo)
  ./setup-device.sh <target> [flags]            Phase 2 (ssh, reboots)
  ./setup-device.sh <target> --hostname NAME    name only, no reboot
  ./deploy-all.sh <target> [component]          Phase 3 (ssh)
  ./commission-offline.sh --bundle <b>          all three, offline, one boot

Full guide: COMMISSIONING.md
USAGE
}

case "${1:-}" in
    ""       ) ;;
    -h|--help) usage; exit 0 ;;
    *        ) err "Unknown argument: $1"; echo ""; usage; exit 1 ;;
esac

# ── the one piece of real logic: waiting for a device ────────────────────────
# Lives here rather than in the three scripts because it is only needed BETWEEN
# phases — a device that has just been powered on, or has just been rebooted by
# setup-device.sh. Polling SSH itself (not ping) is deliberate: ping answers
# while sshd is still starting, which is exactly the window that produces a
# confusing "Cannot reach <ip>" from the next phase.
wait_for_ssh() {
    local target="$1" timeout="${2:-180}" waited=0
    info "Waiting for SSH on $target (up to ${timeout}s)..."
    while [ "$waited" -lt "$timeout" ]; do
        if ssh -o ConnectTimeout=5 -o BatchMode=yes -o StrictHostKeyChecking=no \
               "root@$target" true 2>/dev/null; then
            ok "$target is up (after ${waited}s)"
            return 0
        fi
        sleep 5
        waited=$((waited + 5))
        printf '.'
    done
    echo ""
    err "$target did not answer SSH within ${timeout}s"
    return 1
}

# Ask for a target once and remember it for the rest of the session.
TARGET=""
ask_target() {
    local prompt="Device IP or host name"
    [ -n "$TARGET" ] && prompt="$prompt [$TARGET]"
    local reply
    read -r -p "$prompt: " reply
    reply="${reply:-$TARGET}"
    if [ -z "$reply" ]; then
        err "No target given."
        return 1
    fi
    TARGET="$reply"
    return 0
}

pause() {
    echo ""
    read -r -p "Press Enter to return to the menu... " _
}

confirm() {
    local reply
    read -r -p "$1 [y/N]: " reply
    case "$reply" in [yY]|[yY][eE][sS]) return 0 ;; *) return 1 ;; esac
}

# ── Phase 1 ─────────────────────────────────────────────────────────────────
do_commission() {
    hdr "1. Commission an SD card (offline)"
    # Guard rather than letting the child script fail from the inside: it needs a
    # mounted card and sudo, and neither is something this menu can arrange.
    cat <<'PRE'
  This runs OFFLINE against the SD card, not the device. Before continuing:

    - the card must be out of the RoomWizard and in this host's reader
    - its rootfs (p6) must be mounted. The script finds it by content, so any
      mount point works; if it cannot, it names the disk it found and prints
      the mount command. You can also `export ROOTFS=/mnt/rw`.
    - you will be asked for sudo

  It sets the root password, the host name, SSH access and DHCP.
PRE
    echo ""
    confirm "Card mounted and ready?" || { warn "Skipped."; return 0; }
    echo ""
    bash "$SCRIPT_DIR/commission-roomwizard.sh" || err "Commissioning failed."
}

# ── Phase 1+2+3 in one offline pass ─────────────────────────────────────────
# A composition like everything else here: it execs commission-offline.sh, which
# in turn orchestrates commission-roomwizard.sh rather than restating its prompts.
do_commission_offline() {
    hdr "6. Commission a card completely, offline (one boot)"
    cat <<'PRE'
  This does the WHOLE job against the card: password, host name, SSH, DHCP, the
  vendor cleanup, the boot scripts and the apps. Then one boot and the unit works.

  Before continuing:

    - the card is out of the RoomWizard and in this host's reader
    - a full-card image backup exists SOMEWHERE ELSE. You will be asked.
    - you have a bundle: ./release.sh --stage-only leaves one in build/release,
      or point --bundle at a release tarball
    - you will be asked for sudo, because it mounts all four partitions

  It never touches p1, so a power cycle undoes nothing it did to the boot chain.
PRE
    echo ""
    local bundle
    read -r -p "  Bundle (tarball or directory) [build/release]: " bundle
    bundle="${bundle:-build/release}"
    if [ ! -e "$SCRIPT_DIR/$bundle" ] && [ ! -e "$bundle" ]; then
        err "No such bundle: $bundle"
        info "Build one first:  ./release.sh --stage-only"
        return 1
    fi
    [ -e "$bundle" ] || bundle="$SCRIPT_DIR/$bundle"
    echo ""
    confirm "Card in the reader and ready?" || { warn "Skipped."; return 0; }
    echo ""
    # sudo here rather than inside: mounting is the only step that needs root, and
    # the child refuses clearly if it is missing.
    sudo bash "$SCRIPT_DIR/commission-offline.sh" --bundle "$bundle" \
        || err "Offline commissioning failed."
}

# ── Phase 2 ─────────────────────────────────────────────────────────────────
do_setup_menu() {
    while true; do
        hdr "2. Set up a booted device (ssh)"
        cat <<'MENU'
  a) Standard setup                 disable bloatware, install launcher, reboot
  b) Setup + remove vendor software  --remove       (named stacks, PERMANENT)
  c) Deep clean DRY RUN             --deep-clean --dry-run   (deletes nothing)
  d) Deep clean                     --deep-clean   (+ whitelist sweeps, PERMANENT)
  e) Set host name only             --hostname NAME          (no reboot)
  f) Device status                  --status                 (read-only)
  q) Back
MENU
        echo ""
        local choice
        read -r -p "Choice: " choice
        case "$choice" in
            a) ask_target || { pause; continue; }
               bash "$SCRIPT_DIR/setup-device.sh" "$TARGET" || err "Setup failed."
               pause ;;
            b) ask_target || { pause; continue; }
               warn "--remove DELETES the Steelcase software, including the 472 MB"
               warn "on-device factory restore. Recovery is your host-side card image."
               confirm "Proceed with --remove on $TARGET?" \
                   && { bash "$SCRIPT_DIR/setup-device.sh" "$TARGET" --remove || err "Setup failed."; } \
                   || warn "Skipped."
               pause ;;
            c) ask_target || { pause; continue; }
               bash "$SCRIPT_DIR/setup-device.sh" "$TARGET" --deep-clean --dry-run \
                   || err "Dry run failed."
               pause ;;
            d) ask_target || { pause; continue; }
               warn "Deep clean is PERMANENT: --remove plus every path in /etc/rc*.d,"
               warn "/opt and the data partitions that the keep-list does not name."
               warn "Run option (c) first if you have not."
               confirm "Really deep-clean $TARGET?" \
                   && { bash "$SCRIPT_DIR/setup-device.sh" "$TARGET" --deep-clean || err "Deep clean failed."; } \
                   || warn "Skipped."
               pause ;;
            e) ask_target || { pause; continue; }
               local name
               read -r -p "New host name (single label, e.g. rw09): " name
               if [ -n "$name" ]; then
                   bash "$SCRIPT_DIR/setup-device.sh" "$TARGET" --hostname "$name" \
                       || err "Could not set the host name."
               else
                   warn "No name given; skipped."
               fi
               pause ;;
            f) ask_target || { pause; continue; }
               bash "$SCRIPT_DIR/setup-device.sh" "$TARGET" --status || err "Status failed."
               pause ;;
            back|q|Q|"") return 0 ;;
            *) err "Not a choice: $choice"; pause ;;
        esac
    done
}

# ── Phase 3 ─────────────────────────────────────────────────────────────────
do_deploy() {
    hdr "3. Deploy apps (ssh)"
    info "Discovered components:"
    bash "$SCRIPT_DIR/deploy-all.sh" --list
    echo ""
    ask_target || return 0
    local comp
    read -r -p "Component (Enter = all): " comp
    echo ""
    if [ -n "$comp" ]; then
        bash "$SCRIPT_DIR/deploy-all.sh" "$TARGET" "$comp" || err "Deploy failed."
    else
        bash "$SCRIPT_DIR/deploy-all.sh" "$TARGET" || err "Deploy failed."
    fi
}

# ── Status ──────────────────────────────────────────────────────────────────
do_status() {
    hdr "4. Device status (read-only)"
    ask_target || return 0
    bash "$SCRIPT_DIR/setup-device.sh" "$TARGET" --status || err "Status failed."
}

# ── Full bring-up ───────────────────────────────────────────────────────────
# The ONLY item that chains, and the only reason wait_for_ssh exists: the two
# gaps it closes are (1) card -> first boot, and (2) setup-device.sh's reboot,
# after which the operator otherwise guesses when to start deploying.
do_full() {
    hdr "5. Full bring-up: commission -> set up -> deploy"
    cat <<'PRE'
  Runs all three phases in order, waiting for the device between them.
  You will be prompted at each transition; nothing destructive happens
  without its own confirmation.
PRE
    echo ""
    confirm "Start full bring-up?" || { warn "Cancelled."; return 0; }

    do_commission || return 1

    hdr "Boot the device"
    cat <<'PRE'
  Now:
    1. sync && sudo umount <mountpoint>
    2. put the card back in the RoomWizard
    3. connect Ethernet and power it on
PRE
    echo ""
    read -r -p "Press Enter once the device is powered on... " _
    echo ""
    ask_target || return 1
    wait_for_ssh "$TARGET" 300 || return 1

    hdr "Phase 2: system setup"
    info "Standard setup (no file removal). Use menu item 2 for --remove/--deep-clean."
    confirm "Run setup on $TARGET now?" || { warn "Stopping after Phase 1."; return 0; }
    bash "$SCRIPT_DIR/setup-device.sh" "$TARGET" || { err "Setup failed."; return 1; }

    # setup-device.sh ends in a reboot, so the device is going away right now.
    hdr "Waiting out the reboot"
    info "setup-device.sh rebooted the device; waiting for it to come back."
    sleep 10
    wait_for_ssh "$TARGET" 300 || return 1

    hdr "Phase 3: deploy"
    confirm "Build and deploy all components to $TARGET?" \
        || { warn "Stopping after Phase 2."; return 0; }
    bash "$SCRIPT_DIR/deploy-all.sh" "$TARGET" || { err "Deploy failed."; return 1; }

    hdr "Done"
    ok "Commissioned, set up and deployed: $TARGET"
    info "The launcher is the default boot app. 'ssh root@$TARGET reboot' to see it."
}

# ── main menu ───────────────────────────────────────────────────────────────
while true; do
    hdr "RoomWizard"
    [ -n "$TARGET" ] && info "Target: $TARGET"
    cat <<'MENU'
  1) Commission an SD card        (offline)
  2) Set up a booted device       (ssh)
  3) Deploy apps                  (ssh)
  4) Device status                (read-only)
  5) Full bring-up: 1 -> 2 -> 3
  6) Commission COMPLETELY offline, one boot  (needs a bundle)
  q) Quit
MENU
    echo ""
    read -r -p "Choice: " CHOICE || { echo ""; exit 0; }
    case "$CHOICE" in
        1) do_commission; pause ;;
        2) do_setup_menu ;;
        3) do_deploy; pause ;;
        4) do_status; pause ;;
        5) do_full; pause ;;
        6) do_commission_offline; pause ;;
        q|Q|quit|exit) echo ""; ok "Bye."; exit 0 ;;
        "") ;;
        *) err "Not a choice: $CHOICE"; pause ;;
    esac
done
