#!/bin/bash
# build-and-deploy.sh — USB host mode + Xbox controller support for the RoomWizard
#
# Usage:
#   ./build-and-deploy.sh                          # build the four artifacts only
#   ./build-and-deploy.sh <ip>                     # build + deploy + patch p1
#   ./build-and-deploy.sh <ip> --no-usb-power      # ...but leave p1 alone
#   ./build-and-deploy.sh --bundle <dir>           # build + stage into an offline bundle
#
# Prerequisites:
#   - arm-linux-gnueabihf-gcc  (sudo apt install gcc-arm-linux-gnueabihf)
#   - bc, libssl-dev, bison, flex               (kernel module build, first run only)
#   - python3                                   (the p1 device-tree patch)
#   - SSH key auth to root@<ip>                 (deploy only)
#
# ── ⚠️ THREE mechanisms, THREE homes. Only one of them is p1 ────────────────
#
# This component used to look like one indivisible thing that "needs p1", which is
# how ../IMPROVEMENT_PLAN.md F15 came to record all of USB as unshippable. It is
# three:
#
#   1. the /dev/mem patch of omap2430_ops.dma_init/.dma_exit + a MUSB rebind
#      -> USB HOST MODE ITSELF. Forces IRQ-driven PIO; it does not fix DMA.
#      Lives in device-files/enable-usb-host.sh + device-files/usb-host, i.e. in
#      device-files/provision-rules.conf's `usb` group. Nothing on p1.
#   2. xpad.ko / joydev.ko / ff-memless.ko, force-loaded
#      -> THE CONTROLLER AS /dev/input/event*. Build artifacts, so they travel in
#      the bundle (../lib/rw-bundle.sh) to /lib/modules/4.14.52/extra on p6.
#   3. the device tree's usb_otg_hs `power` property, 0x32 -> 0xfa
#      -> 100 mA to 500 mA, i.e. a controller with NO POWERED HUB. Inside
#      uImage-system, which is on p1. ../lib/rw-usbpower.sh, and the only reason
#      p1 is involved at all.
#
# So this script no longer restates any of that. It BUILDS the four artifacts,
# runs the ARM gate over them, and then drives the same three implementations
# every other path drives.
#
# ── ⚠️ Never ship the vendor kernel ────────────────────────────────────────
#
# uImage-system is a 5.2 MB Steelcase binary and .gitignore already calls it
# copyrighted. The patch is DERIVED from the device's own copy, md5-gated in and
# md5-asserted out; release.sh refuses any bundle entry that names it.

set -e
_START_SECONDS=$(date +%s)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# patch_dtb.py now takes <in> <out> on argv (../IMPROVEMENT_PLAN.md B19 — it used
# to open 'uImage-system' relative to the cwd, so this script worked only because
# deploy-all.sh wrapped it in a subshell cd). The cd stays for the local build
# paths; nothing depends on it any more.
cd "$SCRIPT_DIR"
KERNEL_VERSION="4.14.52"
MODULES_DIR="$SCRIPT_DIR/modules"

REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=../lib/rw-ssh.sh
. "$REPO_ROOT/lib/rw-ssh.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "[$(date '+%H:%M:%S')] ${GREEN}  ✓ $*${NC}"; }
info() { echo -e "[$(date '+%H:%M:%S')] ${YELLOW}  → $*${NC}"; }
warn() { echo -e "[$(date '+%H:%M:%S')] ${BLUE}  ! $*${NC}"; }
err()  { echo -e "[$(date '+%H:%M:%S')] ${RED}  ✗ $*${NC}" >&2; exit 1; }
ts()   { echo "[$(date '+%H:%M:%S')] $*"; }

usage() {
    echo "Usage: $0 [<ip>] [--no-usb-power]"
    echo "       $0 --bundle <dir>"
    echo ""
    echo "  <ip>             Device IPv4 address; omit to build without deploying"
    echo "  --no-usb-power   Do not patch uImage-system on p1. USB host mode and the"
    echo "                   controller modules still go on; the budget stays at"
    echo "                   100 mA, so a pad needs a POWERED hub."
    echo "  --bundle <dir>   Build, then stage the four artifacts under <dir>/root/"
    echo "                   with a declared-mode manifest. No device needed."
    echo ""
    echo "  The three device scripts and the two rc5.d links are NOT installed from"
    echo "  here in isolation — they are device-files/provision-rules.conf's \`usb\`"
    echo "  group, and this script runs that same plan. ./commissioning/provision.sh"
    echo "  <ip> and commission-offline.sh install it too, from the same records."
    exit 1
}

# ── arguments ───────────────────────────────────────────────────────────────
BUNDLE_DIR=""
DEVICE_IP=""
NO_USB_POWER=0

if [[ "${1:-}" == "--bundle" ]]; then
    BUNDLE_DIR="${2:-}"
    [[ -n "$BUNDLE_DIR" ]] || { echo "--bundle requires a directory"; echo ""; usage; }
    [[ -z "${3:-}" ]] || { echo "Unexpected argument after --bundle <dir>: $3"; exit 1; }
    # shellcheck source=../lib/rw-bundle.sh
    . "$REPO_ROOT/lib/rw-bundle.sh"
else
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --no-usb-power) NO_USB_POWER=1; shift ;;
            --help|-h)      usage ;;
            -*)             echo "Unknown option: $1"; echo ""; usage ;;
            *)
                [[ -z "$DEVICE_IP" ]] || { echo "Unexpected argument: $1"; echo ""; usage; }
                DEVICE_IP="$1"; shift ;;
        esac
    done
    # Validated before building, not at the first ssh: this script builds kernel
    # modules and patches a kernel image (../IMPROVEMENT_PLAN.md B19).
    IPV4_RE='^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}$'
    if [[ -n "$DEVICE_IP" && ! "$DEVICE_IP" =~ $IPV4_RE ]]; then
        echo "Not an IPv4 address: $DEVICE_IP"; echo ""; usage
    fi
fi
DEVICE="root@$DEVICE_IP"

# ── the four built artifacts, declared ONCE ─────────────────────────────────
#
# "<mode>|<local path>|<device path>". Both the --bundle staging and the scp
# deploy read this array, so a new artifact is added in one place — the same rule
# native_apps' GAMES_BINARIES follows. Modes are DECLARED, never read off disk:
# /mnt/c reports every file 0777 and discards chmod (../CLAUDE.md).
USB_ARTIFACTS=(
    "0755|$SCRIPT_DIR/devmem_write|/usr/local/bin/devmem_write"
    "0644|$MODULES_DIR/ff-memless.ko|/lib/modules/$KERNEL_VERSION/extra/ff-memless.ko"
    "0644|$MODULES_DIR/joydev.ko|/lib/modules/$KERNEL_VERSION/extra/joydev.ko"
    "0644|$MODULES_DIR/xpad.ko|/lib/modules/$KERNEL_VERSION/extra/xpad.ko"
)

echo ""
echo "════════════════════════════════════════"
echo " RoomWizard USB Host + Controller"
echo "════════════════════════════════════════"
ts "Started — $(date '+%Y-%m-%d %H:%M:%S')"
[[ -n "$BUNDLE_DIR" ]] && info "Staging a bundle: $BUNDLE_DIR"
[[ -n "$DEVICE_IP" ]] && info "Target: $DEVICE"
echo ""

# ── 0. prerequisites ────────────────────────────────────────────────────────
ts "[0/8] Prerequisites"
command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1 \
    || err "arm-linux-gnueabihf-gcc not found. Install: sudo apt install gcc-arm-linux-gnueabihf"
ok "arm-linux-gnueabihf-gcc"

if [[ -n "$DEVICE_IP" && "$NO_USB_POWER" -eq 0 ]]; then
    command -v python3 >/dev/null 2>&1 \
        || err "python3 not found, and it is what patches the device tree. Install it, or pass --no-usb-power."
    ok "python3"
fi

if [[ -n "$DEVICE_IP" ]]; then
    # The shared gate (../lib/rw-ssh.sh, F16): it tells "down" from "up and
    # refusing us" and, on a terminal, offers to install a key.
    rw_ssh_gate "$DEVICE" || err "Cannot continue without SSH to $DEVICE"
    ok "SSH to $DEVICE"
fi
echo ""

# ── 1. devmem_write ─────────────────────────────────────────────────────────
ts "[1/8] devmem_write"
if [[ ! -f "$SCRIPT_DIR/devmem_write" || "$SCRIPT_DIR/devmem_write.c" -nt "$SCRIPT_DIR/devmem_write" ]]; then
    arm-linux-gnueabihf-gcc -static -O2 -o "$SCRIPT_DIR/devmem_write" "$SCRIPT_DIR/devmem_write.c"
    ok "built $(file -b "$SCRIPT_DIR/devmem_write" 2>/dev/null | cut -d, -f1-2)"
else
    ok "up to date"
fi
echo ""

# ── 2. the three kernel modules ─────────────────────────────────────────────
ts "[2/8] Xbox controller kernel modules"
if [[ -f "$MODULES_DIR/ff-memless.ko" && -f "$MODULES_DIR/joydev.ko" && -f "$MODULES_DIR/xpad.ko" ]]; then
    ok "already built in modules/"
else
    command -v bc >/dev/null 2>&1 || err "'bc' not found. Install: sudo apt install bc"

    # The kernel config comes off a device. ⚠️ With --bundle there is no device to
    # ask, so this is a refusal with the one command that fixes it rather than a
    # confusing failure inside build-xpad-module.sh.
    if [[ ! -f "$SCRIPT_DIR/device_config" ]]; then
        if [[ -z "$DEVICE_IP" ]]; then
            err "the modules are not built and usb_host/device_config is absent, so
     build-xpad-module.sh has no kernel config to build against — and with no
     <ip> there is no device to read one from. Get it from any unit once:
       ssh root@<ip> cat /proc/config.gz | gunzip > usb_host/device_config
     Then re-run. (device_config and modules/ are gitignored: they are
     cross-compilation artifacts, not sources.)"
        fi
        info "fetching /proc/config.gz from the device..."
        ssh "$DEVICE" "cat /proc/config.gz" > "$SCRIPT_DIR/device_config.gz"
        gunzip -f "$SCRIPT_DIR/device_config.gz"
        ok "saved usb_host/device_config"
    fi
    info "building (several minutes on the first run)..."
    bash "$SCRIPT_DIR/build-xpad-module.sh"
fi
for mod in ff-memless.ko joydev.ko xpad.ko; do
    [[ -f "$MODULES_DIR/$mod" ]] || err "module $mod not in $MODULES_DIR — run build-xpad-module.sh by hand to debug"
done
ok "ff-memless.ko, joydev.ko, xpad.ko"
echo ""

# ── 3. the ARM-safety gate, on all four ─────────────────────────────────────
#
# Cortex-A8 has no hardware integer divide: an sdiv/udiv INSTRUCTION is SIGILL
# (exit 132) with a blank screen and no log. Runs before the deploy AND before
# --bundle, for the same reason vnc_client's does — a published bundle is installed
# by someone with no toolchain.
#
# ⚠️ Exit 2 ("could not judge") is a REFUSAL here, unlike in
# commission-offline.sh. Every usb_host artifact is unstripped by construction —
# `$CC -static` with no strip, and the module build does not strip either — so a 2
# means the build changed, not that this component ships stripped binaries the way
# scummvm and vnc_client do. Keeping it fatal is what makes "usb_host contributes
# zero TAKEN ON TRUST entries to a bundle" a checked property (F15, C9).
#
# ⚠️ The status is read directly, NEVER through xargs: xargs collapses any exit of
# 1–125 onto its own 123 and erases the difference between "a real hit" and "could
# not judge".
ts "[3/8] ARM-safety gate (no sdiv/udiv)"
ARM_TARGETS=()
for a in "${USB_ARTIFACTS[@]}"; do ARM_TARGETS+=("${a#*|}"); ARM_TARGETS[-1]="${ARM_TARGETS[-1]%%|*}"; done
if [[ -x "$REPO_ROOT/native_apps/check-arm-safe.sh" || -f "$REPO_ROOT/native_apps/check-arm-safe.sh" ]]; then
    arm_rc=0
    bash "$REPO_ROOT/native_apps/check-arm-safe.sh" "${ARM_TARGETS[@]}" || arm_rc=$?
    case "$arm_rc" in
        0) ok "hard zero across ${#ARM_TARGETS[@]} artifact(s)" ;;
        2) err "the gate could not judge one of these artifacts — it is STRIPPED.
     Every usb_host artifact is unstripped by construction, so this means the
     build changed. objdump reads Thumb-2 as ARM without a symbol table and
     invents sdiv/udiv, so there is no sound verdict to be had on a stripped
     file (../IMPROVEMENT_PLAN.md C9). Do not deploy or bundle these." ;;
        *) err "an artifact would SIGILL on this device — refusing to deploy or bundle" ;;
    esac
else
    err "../native_apps/check-arm-safe.sh is missing — refusing to ship ungated ARM binaries"
fi
echo ""

# ── 4. --bundle: stage and stop ─────────────────────────────────────────────
if [[ -n "$BUNDLE_DIR" ]]; then
    ts "[4/8] Staging → $BUNDLE_DIR"
    rw_bundle_init "$BUNDLE_DIR" usb_host || err "could not prepare $BUNDLE_DIR"
    for a in "${USB_ARTIFACTS[@]}"; do
        mode="${a%%|*}"; rest="${a#*|}"; src="${rest%%|*}"; dev="${rest#*|}"
        rw_bundle_add "$BUNDLE_DIR" usb_host "$mode" "$src" "$dev" \
            || err "staging failed: $dev"
    done
    # ⚠️ uImage-system is deliberately NOT staged, patched or otherwise. It is a
    # Steelcase binary; the installer derives the patch from the card's own copy.
    warn "uImage-system deliberately NOT bundled — it is the vendor's kernel"
    warn "  (release.sh refuses any manifest entry that names it)"
    warn "The three device scripts are provision-rules.conf's, not the bundle's"
    ok "Staged $(rw_bundle_finish "$BUNDLE_DIR" usb_host) file(s)"
    echo ""
    exit 0
fi

if [[ -z "$DEVICE_IP" ]]; then
    echo "No IP supplied — built only. To deploy:"
    echo "  ./build-and-deploy.sh <ip>"
    echo "  ./build-and-deploy.sh <ip> --no-usb-power"
    exit 0
fi

# ── 4. the `usb` group of the provision plan ────────────────────────────────
#
# ⚠️ The decisions are NOT here. The three device scripts, their modes and the two
# rc5.d links are records in device-files/provision-rules.conf, read by this
# script, by commissioning/provision.sh and by commission-offline.sh — so the three
# paths cannot drift (../IMPROVEMENT_PLAN.md C12). The executor is the SAME
# generated interpreter provision.sh pipes to the device.
#
# What used to be here: three scp calls, three chmod +x, two `ln -sf` and an
# /etc/init.d name (S89xpad-modules) that matched nothing else in the repo.
ts "[4/8] Device scripts and boot links (provision-rules.conf, group usb)"
# shellcheck source=../lib/rw-identify.sh
. "$REPO_ROOT/lib/rw-identify.sh"
# shellcheck source=../lib/rw-clean.sh
. "$REPO_ROOT/lib/rw-clean.sh"
# shellcheck source=../lib/rw-provision.sh
. "$REPO_ROOT/lib/rw-provision.sh"

PROV_RULES="$REPO_ROOT/device-files/provision-rules.conf"
CLEAN_RULES="$REPO_ROOT/device-files/clean-rules.conf"
[[ -f "$PROV_RULES" ]] || err "missing $PROV_RULES"
if ! PCHECK="$(rw_provision_validate "$PROV_RULES" "$REPO_ROOT")"; then
    echo "$PCHECK"; err "provision-rules.conf does not validate"
fi
# A boot link the clean's whitelist does not name is deleted by the next
# --deep-clean, so the unit boots right once and loses USB on the following clean.
if ! KCHECK="$(rw_provision_check_keeps "$PROV_RULES" "$CLEAN_RULES")"; then
    echo "$KCHECK"; err "a boot link in provision-rules.conf is not kept by clean-rules.conf"
fi

USB_PLAN=$(mktemp)
trap 'rm -f "$USB_PLAN"' EXIT INT TERM
rw_provision_plan_component "$PROV_RULES" usb > "$USB_PLAN" \
    || err "could not compile the usb provision plan"
info "$(rw_provision_plan_summary "$USB_PLAN")"

# `install` is the one verb the remote interpreter cannot do alone: the source
# bytes are on this host, so they go over scp first and it only sets the mode.
# ⚠️ The loop is lib/rw-provision.sh's, not a copy — see B28: this script and
# commissioning/provision.sh each had one, both reading the plan on stdin with an
# `ssh` in the body, and both therefore installed exactly one file.
rw_provision_push_installs "$USB_PLAN" "$REPO_ROOT" "$DEVICE" \
    || err "could not copy the usb provision sources to the device"

ssh "$DEVICE" "cat > /tmp/rw-usb-plan" < "$USB_PLAN"
rw_provision_online_script | ssh "$DEVICE" "cat > /tmp/rw-usb-provision.sh"
ssh "$DEVICE" "sh /tmp/rw-usb-provision.sh /tmp/rw-usb-plan; rc=\$?; rm -f /tmp/rw-usb-provision.sh /tmp/rw-usb-plan; exit \$rc" \
    || err "the usb provision step failed on the device"
ok "scripts installed, S89xpad-modules and S90usb-host linked"
echo ""

# ── 5. the four built artifacts ─────────────────────────────────────────────
ts "[5/8] Built artifacts"
ssh "$DEVICE" "mkdir -p /usr/local/bin /lib/modules/$KERNEL_VERSION/extra"
for a in "${USB_ARTIFACTS[@]}"; do
    mode="${a%%|*}"; rest="${a#*|}"; src="${rest%%|*}"; dev="${rest#*|}"
    scp -q "$src" "$DEVICE:$dev" || err "could not copy $src to $dev"
    ssh "$DEVICE" "chmod $mode '$dev'"
    ok "$(basename "$src") → $dev ($mode)"
done
ssh "$DEVICE" "depmod -a $KERNEL_VERSION 2>/dev/null || true"
ok "depmod"
echo ""

# ── 6. p1: the 500 mA power budget ──────────────────────────────────────────
#
# ⚠️ ONE implementation of this sequence, in ../lib/rw-usbpower.sh, shared with
# commissioning/provision.sh and commission-offline.sh. Only the transport differs.
# Never write a second copy of the gate/backup/patch/verify/rollback here — it is
# the one step tests/rw_provision_test.sh group E cannot compare between
# executors, so a duplicate would drift undetected.
ts "[6/8] USB power budget (p1)"
P1_STATE="not attempted"
if [[ "$NO_USB_POWER" -eq 1 ]]; then
    warn "--no-usb-power: p1 untouched. The budget stays at 100 mA, so a controller"
    warn "  needs a POWERED hub. A power cycle remains a free undo on this unit."
    P1_STATE="skipped (--no-usb-power)"
else
    # shellcheck source=../lib/rw-usbpower.sh
    . "$REPO_ROOT/lib/rw-usbpower.sh"
    UP_WORK=$(mktemp -d)
    if rw_usbpower_apply_ssh "$DEVICE" "$UP_WORK"; then
        P1_STATE="500 mA (patched and verified)"
    else
        P1_STATE="FAILED — read the block above"
        warn "the p1 patch did not succeed. USB host mode and the controller"
        warn "  modules are still installed; the budget is whatever it was."
    fi
    rm -rf "$UP_WORK"
fi
echo ""

# ── 7. bring it up now, without a reboot ────────────────────────────────────
#
# The /dev/mem patch and the module loads are runtime state, so they can be done
# immediately — which is what makes the dev loop one command. The p1 change is the
# only part that needs a reboot.
ts "[7/8] Enabling host mode and loading the modules now"
ssh "$DEVICE" "/etc/init.d/xpad-modules start" || warn "module load reported a failure"
ssh "$DEVICE" "/usr/local/bin/enable-usb-host.sh" || warn "enable-usb-host.sh reported a failure"
echo ""

# ── 8. verify ───────────────────────────────────────────────────────────────
ts "[8/8] Verifying on the device"
echo ""
ssh "$DEVICE" sh -s <<'VERIFY'
echo "--- USB bus ---"
if [ -d /sys/bus/usb/devices/usb1 ]; then
    echo "  host mode: ACTIVE"
else
    echo "  host mode: NOT ACTIVE"
fi

echo ""
echo "--- power budget (live device tree) ---"
POWER=$(hexdump -e '4/1 "%02x"' /proc/device-tree/ocp*/usb_otg_hs*/power 2>/dev/null)
case "$POWER" in
    000000fa) echo "  500 mA (0xfa) — a controller works with no powered hub" ;;
    00000032) echo "  100 mA (0x32) — REBOOT REQUIRED for a p1 patch to take effect" ;;
    *)        echo "  unknown ($POWER)" ;;
esac

echo ""
echo "--- p1 ---"
if mkdir -p /tmp/rw-boot-chk && mount -t vfat /dev/mmcblk0p1 /tmp/rw-boot-chk 2>/dev/null; then
    for f in uImage-system uImage-system.vendor; do
        [ -f "/tmp/rw-boot-chk/$f" ] && md5sum "/tmp/rw-boot-chk/$f" | sed 's|/tmp/rw-boot-chk/|  |'
    done
    umount /tmp/rw-boot-chk 2>/dev/null
    rmdir /tmp/rw-boot-chk 2>/dev/null
else
    echo "  (could not mount p1 to check)"
fi

echo ""
echo "--- modules ---"
lsmod 2>/dev/null | grep -E "^Module|xpad|joydev|ff_memless" || echo "  (none)"

echo ""
echo "--- USB devices ---"
lsusb 2>/dev/null || echo "  (lsusb not available)"

echo ""
echo "--- input devices ---"
for ev in /dev/input/event*; do
    [ -e "$ev" ] || continue
    echo "  $ev: $(cat "/sys/class/input/$(basename "$ev")/device/name" 2>/dev/null)"
done

echo ""
echo "--- boot links ---"
for l in /etc/rc5.d/S89xpad-modules /etc/rc5.d/S90usb-host; do
    if [ -L "$l" ]; then
        echo "  $l -> $(readlink "$l")"
    else
        echo "  $l MISSING"
    fi
done
VERIFY

echo ""
_ELAPSED=$(( $(date +%s) - _START_SECONDS ))
echo "════════════════════════════════════════"
printf "  Done — %dm%02ds\n" $((_ELAPSED / 60)) $((_ELAPSED % 60))
echo "════════════════════════════════════════"
echo "  p1 power budget: $P1_STATE"
if [[ "$P1_STATE" == "500 mA"* ]]; then
    echo ""
    echo "  ⚠  REBOOT REQUIRED before the 500 mA budget is live:"
    echo "       ssh $DEVICE 'sync; reboot'"
    echo "     Then plug a controller in DIRECTLY, with no powered hub — that is"
    echo "     the check that the p1 patch took effect."
    echo ""
    echo "  To undo: copy uImage-system.vendor over uImage-system on p1."
fi
echo ""
