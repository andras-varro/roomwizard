#!/bin/bash
#
# commissioning/provision.sh — Phase 2: One-time system setup over SSH
#
# Run this ONCE after the first SSH login to a freshly commissioned device.
# It disables Steelcase bloatware, installs the generic app launcher, and
# sets up audio + time-sync boot scripts.
#
# Usage:
#   ./commissioning/provision.sh <target>                 # setup + deep clean + p1 + reboot
#   ./commissioning/provision.sh <target> --no-clean      # setup only, delete nothing
#   ./commissioning/provision.sh <target> --remove        # the named stacks, without the sweeps
#   ./commissioning/provision.sh <target> --dry-run       # list what the clean would delete
#   ./commissioning/provision.sh <target> --status        # show device status only
#   ./commissioning/provision.sh <target> --hostname rw09 # set the host name only, no reboot
#
# <target> is an IPv4 address or a host name — `rw09.local` works once mDNS is
# enabled (this script does that) and the unit has a unique name (--hostname).
#
# Prerequisites:
#   - Device commissioned with commissioning/card-prep.sh (Phase 1)
#   - SSH access as root
#
# What it does:
#   1. Deploys disable-steelcase.sh → /opt/roomwizard/
#   2. Runs disable-steelcase.sh (watchdog bypass, cron cleanup, service stop)
#   3. Installs device-files/roomwizard-app as /etc/init.d/roomwizard-app
#      Registers the init service (priority S99)
#      Deploys audio-enable + time-sync boot scripts, and enables avahi (mDNS)
#      Installs the USB host-mode scripts and their boot links (--no-usb skips)
#   4. Hardens SSH (PermitEmptyPasswords=no, brute-force limits)
#   5. Applies kernel/sysctl security settings (ASLR, no ip_forward, etc.)
#   6. Deletes the vendor software stack (--no-clean opts out; IMPROVEMENT_PLAN.md C13)
#   7. Raises the USB power budget to 500 mA by patching p1 (--no-usb-power opts out)
#   8. Reboots device
#
# ⚠️ THE DEFAULTS ARE DESTRUCTIVE, and deliberately the same defaults
# commissioning/commission-offline.sh has: a plain run of either tool leaves the
# same unit. Both ask the full-card-backup question first, and both say loudly
# what they auto-answered when stdin is not a terminal.
#
# NOTE: No iptables firewall — the Steelcase firmware ships without iptables
# userspace tools, the ip_tables.ko kernel module is not included in
# /lib/modules/4.14.52, and there's no package manager to install them.
# The kernel has CONFIG_NETFILTER=y but no ip_tables module was built.
# Network security relies on: SSH hardening + sysctl + disabling services.
#
# After this, deploy a project and set it as the default app:
#   cd native_apps && ./build-and-deploy.sh <ip> set-default
#   cd vnc_client   && ./build-and-deploy.sh <ip> set-default

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Both halves of the job are DATA in one file each, shared with
# commissioning/commission-offline.sh so that the live and offline passes cannot drift:
#
#   device-files/clean-rules.conf      what is REMOVED  (lib/rw-clean.sh)
#   device-files/provision-rules.conf  what is INSTALLED (lib/rw-provision.sh)
#
# Each library is the parser and the plan compiler. The executors differ — "/" is
# the correct prefix on a device and a refused one offline, and on this path the
# work happens on the far side of an ssh pipe — but there is one implementation of
# each, and rw_provision_online_script is the one this script ships to the device
# (IMPROVEMENT_PLAN.md F10, C11, C12).
# shellcheck source=../lib/rw-identify.sh
. "$REPO_ROOT/lib/rw-identify.sh"
# shellcheck source=../lib/rw-clean.sh
. "$REPO_ROOT/lib/rw-clean.sh"
# shellcheck source=../lib/rw-provision.sh
. "$REPO_ROOT/lib/rw-provision.sh"
# shellcheck source=../lib/rw-ssh.sh
. "$REPO_ROOT/lib/rw-ssh.sh"

# ── the non-positional flags are extracted before positional parsing ────────
#
# --keep-<g> switches off part of the CLEAN, --no-<g> part of the PROVISION. Both
# are named after the groups in their own data file, so neither list is repeated
# here. They are pulled out of "$@" first because everything below is positional
# ($1 target, $2 flag) and a --keep-browser sitting in $3 would otherwise be
# rejected as an unknown option.
#
# ⚠️ --no-clean and --no-usb-power MUST be matched before the --no-* glob. `case`
# takes the first match, so an arm placed after it is never reached and the
# operator gets "Unknown provision group: usb-power" instead. commission-offline.sh
# has the same two arms in the same order, for the same reason.
KEEP_GROUPS=""
NO_PROV_GROUPS=""
DO_CLEAN=1
DO_USB_POWER=1
DRY_RUN=""
_ARGS=()
for _a in "$@"; do
    case "$_a" in
        --no-clean)     DO_CLEAN=0 ;;
        --no-usb-power) DO_USB_POWER=0 ;;
        --usb-mode)     export RW_USBPOWER_WITH_MODE=1 ;;
        --dry-run)      DRY_RUN="--dry-run" ;;
        --no-*)
            _g="${_a#--no-}"
            case " $(rw_provision_optional_groups) " in
                *" $_g "*) NO_PROV_GROUPS="$NO_PROV_GROUPS $_g" ;;
                *) echo "Unknown provision group: $_g"
                   echo "  --no- accepts: $(rw_provision_optional_groups)"
                   exit 1 ;;
            esac
            ;;
        --keep-*)
            _g="${_a#--keep-}"
            case " $(rw_clean_optional_groups) " in
                *" $_g "*) KEEP_GROUPS="$KEEP_GROUPS $_g" ;;
                *) echo "Unknown clean group: $_g"
                   echo "  --keep- accepts: $(rw_clean_optional_groups)"
                   exit 1 ;;
            esac
            ;;
        *) _ARGS+=("$_a") ;;
    esac
done
set -- "${_ARGS[@]}"

# --no-usb implies --no-usb-power. With no driver and no controller modules
# installed there is nothing to spend the raised budget on, so patching p1 would be
# a gratuitous write to the one partition worth not writing.
case " $NO_PROV_GROUPS " in
    *" usb "*) DO_USB_POWER=0 ;;
esac

DEVICE_IP="${1:-}"
FLAG="${2:-}"
DEVICE="root@${DEVICE_IP}"
REMOTE_DIR="/opt/roomwizard"
INIT_SCRIPT="/etc/init.d/roomwizard-app"
# Files installed onto the device verbatim. They live in device-files/ rather
# than in a heredoc here so that the offline installer writes the same bytes —
# two copies of an init script is two things to keep in step, and the one that
# drifts is discovered on a device that boots to a black screen.
DEVICE_FILES="$REPO_ROOT/device-files"
CLEAN_RULES="$DEVICE_FILES/clean-rules.conf"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }

# ── usage ───────────────────────────────────────────────────────────────────
usage() {
    echo "Usage: $0 <target> [--remove|--deep-clean|--status] [--no-clean] [--dry-run]"
    echo "       $0 <target> --hostname NAME"
    echo ""
    echo "  With no flags this does the FULL commissioning: provision, deep clean,"
    echo "  the 500 mA USB power budget on p1, and a reboot. Same end state as"
    echo "  commissioning/commission-offline.sh, which has the same defaults."
    echo ""
    echo "  The p1 write is step 5 of EVERY mode, not part of the clean: --no-clean"
    echo "  still writes it, --keep-sweeps still writes it, and --no-usb-power is the"
    echo "  only way to skip it. The two share ONE consent prompt because both are"
    echo "  irreversible, which is the only sense in which they are one action."
    echo ""
    echo "  <target>          Device IPv4 address, or a host name (e.g. rw09.local)"
    echo "  --no-clean        Delete nothing. Provision, harden, reboot — that is all."
    echo "  --remove          Clean the named vendor software stacks only, WITHOUT"
    echo "                    the whitelist sweeps. Narrower than the default."
    echo "  --deep-clean      The default, stated explicitly: --remove PLUS the"
    echo "                    whitelist sweeps — anything in /etc/rc*.d, /opt or the"
    echo "                    data partitions that the keep-list does not name."
    echo "                    Both read device-files/clean-rules.conf; the only"
    echo "                    difference is the 'sweeps' group. Neither is reversible"
    echo "                    on the device — the 472 MB restore payload goes."
    echo "  --status          Show device status only (no changes, no clean, no reboot)"
    echo "  --dry-run         List what the clean would delete and what would be"
    echo "                    written to p1; change nothing, do not reboot."
    echo "  --keep-<group>    Leave one stack on disk. Groups:"
    echo "                    $(rw_clean_optional_groups)"
    echo "                    Files only — it does not re-enable a boot link, because"
    echo "                    the rc*.d whitelist is what removes an unknown service."
    echo "                    --keep-factory keeps the 472 MB restore payload;"
    echo "                    --keep-sweeps turns the default into --remove."
    echo "  --no-<group>      Skip one group of the provision plan. Groups:"
    echo "                    $(rw_provision_optional_groups)"
    echo "                    --no-mdns leaves <name>.local unresolvable; --no-sshd"
    echo "                    leaves PermitEmptyPasswords at the factory 'yes';"
    echo "                    --no-usb installs no USB host mode and implies"
    echo "                    --no-usb-power."
    echo "  --no-usb-power    Leave p1 alone. The USB budget stays at the vendor's"
    echo "                    100 mA, so a controller needs a POWERED hub — and a"
    echo "                    power cycle stays a free undo."
    echo "  --usb-mode        ALSO patch the MUSB 'mode' property from 3 (DUAL_ROLE)"
    echo "                    to 1 (HOST), so the port is live after a boot with an"
    echo "                    empty socket. UNVERIFIED ON HARDWARE — see"
    echo "                    IMPROVEMENT_PLAN.md B32. Undo: uImage-system.vendor."
    echo "  --hostname NAME   Set the device host name only, and exit. No reboot,"
    echo "                    no clean, no p1 write."
    echo "                    NAME is a single label — 'rw09', not 'rw09.local'."
    exit 1
}

[[ -z "$DEVICE_IP" ]] && usage

# Validate the target before doing anything: every step past here is destructive
# and ends in a reboot (../IMPROVEMENT_PLAN.md B19).
#
# An IPv4 address OR a DNS name is accepted. The name form is what makes
# `./commissioning/provision.sh rw09.local` work, which is the whole point of enabling
# avahi below — an IPv4-only gate here silently defeated it. (This used to be
# IPv4-only and there was a SECOND, weaker validator further down that did
# accept a name; the strict one exited first, so the permissive one was dead
# code and the script only *looked* like it took a host name. Both are now this
# one check.)
IPV4_RE='^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}$'
# RFC-1123: dot-separated labels, each 1..63 chars, alphanumeric at both ends,
# hyphens allowed inside. No trailing dot.
DNSNAME_RE='^[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$'
if [[ "$DEVICE_IP" =~ $IPV4_RE ]]; then
    :   # an address
elif [[ "$DEVICE_IP" =~ ^[0-9.]+$ ]]; then
    # Digits and dots only, but not a valid IPv4 — that is a mistyped address,
    # not a host name. Accepting it as one would turn 192.168.50.999 into a DNS
    # lookup and a confusing timeout instead of an immediate complaint.
    echo "Not a valid IPv4 address: $DEVICE_IP"
    echo ""
    usage
elif [[ "$DEVICE_IP" =~ $DNSNAME_RE ]]; then
    :   # a host name, e.g. rw09.local
else
    echo "Not an IPv4 address or host name: $DEVICE_IP"
    echo ""
    usage
fi

# ── deployed-script drift ───────────────────────────────────────────────────
# These two files are the only ones this script copies from the repo, and they
# reach a device ONLY through here — deploy-all.sh does not push them.  So a
# device can silently run an older copy than the repo's, with nothing saying so:
# while B18 was being reproduced, RW09's disable-steelcase.sh turned out to be
# older than the repo's *pre-fix* copy, which is why that repro had to stage the
# tracked file to /tmp instead of using the device's own.  md5 settles it in one
# command (../IMPROVEMENT_PLAN.md B19).
#
# Byte comparison is valid because .gitattributes pins *.sh to eol=lf, so the
# working tree is LF even on this Windows host and scp copies it unchanged.
report_script_versions() {
    local pairs=(
        "$REPO_ROOT/device-files/disable-steelcase.sh:$REMOTE_DIR/disable-steelcase.sh"
        "$REPO_ROOT/device-files/roomwizard-app:$INIT_SCRIPT"
    )
    local pair local_path remote_path local_md5 remote_md5 drift=0
    for pair in "${pairs[@]}"; do
        local_path="${pair%%:*}"
        remote_path="${pair#*:}"
        local_md5="$(md5sum "$local_path" | cut -d' ' -f1)"
        remote_md5="$(ssh "$DEVICE" "md5sum $remote_path 2>/dev/null" 2>/dev/null | cut -d' ' -f1 || true)"
        if [[ -z "$remote_md5" ]]; then
            warn "$(basename "$remote_path"): NOT INSTALLED"
            drift=1
        elif [[ "$local_md5" == "$remote_md5" ]]; then
            ok "$(basename "$remote_path"): matches repo (${local_md5:0:8})"
        else
            warn "$(basename "$remote_path"): DRIFTED — device ${remote_md5:0:8}, repo ${local_md5:0:8}"
            drift=1
        fi
    done
    if [[ $drift -ne 0 ]]; then
        warn "Run '$0 $DEVICE_IP' to push the repo's versions (this REBOOTS the device)"
    fi
    return 0
}

# Reject unknown flags rather than silently falling through to a full setup+reboot
case "$FLAG" in
    ""|--remove|--deep-clean|--status|--hostname) ;;
    *) echo "Unknown option: $FLAG"; echo ""; usage ;;
esac

# --dry-run previews writes, and these two modes make none — so the combination is a
# mistake rather than a no-op. Said out loud because the dry-run branch runs BEFORE
# the --status and --hostname branches, so silently accepting it would preview a
# clean the operator never asked for.
if [[ -n "$DRY_RUN" && ( "$FLAG" == "--status" || "$FLAG" == "--hostname" ) ]]; then
    echo "--dry-run does not apply to $FLAG — neither writes anything."
    echo ""
    usage
fi

# NOTE: a stray positional is rejected per-mode below, where the number of
# expected ones is known — --hostname takes a NAME, nothing else takes anything.

# --hostname is the one flag that takes a value, so it consumes $3. Every other
# mode is exhausted by $1 and $2 — --dry-run, --no-clean, --no-usb-power and the
# two group families were all lifted out of "$@" above, so a leftover positional
# here is a mistake. Say so rather than ignoring it, for the same reason the flag
# case above is exhaustive.
NEW_HOSTNAME=""
if [[ "$FLAG" == "--hostname" ]]; then
    NEW_HOSTNAME="${3:-}"
    if [[ -z "$NEW_HOSTNAME" ]]; then
        echo "--hostname requires a NAME"; echo ""; usage
    fi
    if [[ -n "${4:-}" ]]; then
        echo "Unexpected argument: $4"; echo ""; usage
    fi
    # Validated HERE, at parse time, for two reasons: a typo should not need a
    # reachable device to be caught, and the name is later interpolated into an
    # ssh command string, so nothing unexpected should ever get that far.
    # commissioning/set-hostname.sh validates again on the device and remains the authority.
    if [[ ! "$NEW_HOSTNAME" =~ ^[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?$ ]]; then
        echo "Not a valid host name: $NEW_HOSTNAME"
        echo "  RFC-1123, single label: letters, digits and hyphens, alphanumeric at both ends."
        echo "  Set 'rw09', not 'rw09.local' — mDNS appends the .local itself."
        echo ""
        usage
    fi
elif [[ -n "${3:-}" ]]; then
    echo "Unexpected argument: $3"; echo ""; usage
fi

# NOTE: the target was validated at the top of this file, against both an IPv4
# address and a DNS name. A second, weaker check used to sit here; it was
# unreachable, and deleting it is what makes `rw09.local` usable.

# ── which clean, if any ─────────────────────────────────────────────────────
#
# IMPROVEMENT_PLAN.md C13: the deep clean is the DEFAULT, because the offline pass
# has always defaulted to it and the result of commissioning must not depend on
# which path ran. --no-clean is the opt-out; --remove narrows it to the named
# stacks; --deep-clean names the default explicitly. --deep-clean does not rewrite
# FLAG to "--remove": that mattered when the two were separate mechanisms running in
# sequence, and now it would run the clean twice.
CLEAN_MODE="deep"
[[ "$FLAG" == "--remove" ]] && CLEAN_MODE="remove"


# ── ONE consent gate, for every irreversible thing this run will do ─────────
#
# Asked once, asked FIRST, and about the backup — the same question and the same
# wording as commissioning/commission-offline.sh's phase 0, because it is the same
# precondition. It covers BOTH irreversible steps: the clean, and the p1 write.
# There is no per-flag opt-out to soften it with; the opt-outs are --no-clean and
# --no-usb-power, and choosing neither IS the decision (IMPROVEMENT_PLAN.md C11,
# C13). The device has no serial console, so a failed boot yields no diagnostics at
# all (SYSTEM_ANALYSIS.md#312-serial-ports).
#
# ⚠️ The non-TTY branch is the whole point of C13, and its loudness IS the safety
# property. What it replaced: an unguarded `read`, which at EOF left the answer
# empty, cancelled the clean and returned 0 — so a scripted run SILENTLY did not
# clean while the operator believed the default did. A false-negative gate. The
# decision taken was that a caller who passed neither opt-out has already decided,
# so the work proceeds — but the printed record of what nobody answered is then the
# only thing standing between that and an unexplained unit.
CONSENT="no"
ask_consent() {
    local answer=""

    # Nothing irreversible selected: there is nothing to consent to. A provision-only
    # run installs files and reboots, which has never been gated and is undone by
    # re-running it.
    if [[ "$DO_CLEAN" -eq 0 && "$DO_USB_POWER" -eq 0 ]]; then
        CONSENT="yes"
        return 0
    fi
    if [[ "$DRY_RUN" == "--dry-run" ]]; then
        CONSENT="yes"
        return 0
    fi

    echo ""
    echo "════════════════════════════════════════"
    echo " Before anything is written"
    echo "════════════════════════════════════════"
    if [[ "$DO_CLEAN" -eq 1 ]]; then
        warn "This removes the Steelcase software from this device: the vendor"
        warn "services, their data and configuration, and — unless --keep-factory —"
        warn "the 472 MB on-device factory-restore payload."
        warn ""
        warn "The original RoomWizard functionality does not come back afterwards,"
        warn "and the device's own restore mechanism goes with it."
        warn "  (--no-clean skips this entirely.)"
        warn ""
    fi
    if [[ "$DO_USB_POWER" -eq 1 ]]; then
        warn "This also WRITES p1 — one value in the device tree inside"
        warn "uImage-system, raising the USB power budget from the vendor's 100 mA"
        warn "to 500 mA so a controller works with no powered hub. The vendor image"
        warn "is copied to uImage-system.vendor on p1 first and md5-verified both"
        warn "ways, and restoring that copy undoes it."
        warn ""
        warn "⚠️ A POWER CYCLE IS THEREFORE NO LONGER A FREE UNDO on this unit."
        warn "  (--no-usb-power skips this entirely and keeps it one.)"
        warn ""
    fi
    warn "PRECONDITION: a full-card image backup exists somewhere other than"
    warn "this card. Recovery from a bad boot means dd-ing it back."

    if [[ -t 0 ]]; then
        read -r -p "  Do you have that backup? (yes/no): " answer
        if [[ "$answer" == "yes" ]]; then
            CONSENT="yes"
        else
            CONSENT="no"
            echo "  Make one first: pull the card, then"
            echo "    sudo dd if=/dev/sdX of=card.img bs=4M status=progress"
            echo ""
            info "Nothing irreversible will be done on this run."
            info "  The provision step still runs, and the device still reboots —"
            info "  both are repeatable. Re-run when the backup exists."
        fi
        return 0
    fi

    # Not a terminal. Nobody is going to answer, so say — unmissably — what is
    # being done without an answer, and what would have prevented it.
    echo ""
    echo "  ══════════════════════════════════════════════════════════════════"
    echo "   ⚠  STDIN IS NOT A TERMINAL. THE BACKUP QUESTION ABOVE IS BEING"
    echo "      AUTO-ANSWERED \"yes\" AND THIS RUN IS PROCEEDING."
    echo "  ══════════════════════════════════════════════════════════════════"
    echo "   Nobody confirmed a backup exists. Proceeding anyway, because passing"
    echo "   neither --no-clean nor --no-usb-power is itself the decision"
    echo "   (IMPROVEMENT_PLAN.md C13). On this run that means:"
    if [[ "$DO_CLEAN" -eq 1 ]]; then
        echo "     · the vendor software stack is being DELETED ($CLEAN_MODE), and the"
        echo "       factory-restore payload with it unless --keep-factory was passed"
    fi
    if [[ "$DO_USB_POWER" -eq 1 ]]; then
        echo "     · p1 is being WRITTEN: uImage-system patched to a 500 mA USB budget,"
        echo "       so a power cycle stops being a free undo on this unit"
    fi
    echo ""
    echo "   To get a prompt, run this from a terminal. To avoid the question,"
    echo "   pass the opt-out you meant."
    echo "  ══════════════════════════════════════════════════════════════════"
    CONSENT="yes"
    return 0
}


# ── deep clean ──────────────────────────────────────────────────────────────
#
# ⚠️ The decisions are NOT here. Every keep and every delete lives in
# device-files/clean-rules.conf with a reason per entry, read by this script and
# by commissioning/commission-offline.sh so the live and offline cleans cannot drift
# (IMPROVEMENT_PLAN.md F10). lib/rw-clean.sh compiles that file into a plan; what
# follows is only this script's EXECUTOR, and it is separate from the offline one
# because "/" is the correct prefix on a device and a refused one offline.
#
# The shape of the list, and why it is a whitelist under rc*.d, /opt and
# /home/root/{data,log,backup}: the risk being managed is an unknown vendor
# service on a unit nobody has inspected, so an unrecognised one must be removed
# by construction rather than by someone adding a name. Disk space is not the
# motive — p6 has 474 MB free before anything is deleted.
#
# Everything the file names was verified unused on a live device. The enabling
# fact is that every binary this project ships is statically linked -- `ldd`
# reports "not a dynamic executable" for app_launcher, vnc_client and scummvm --
# so the only dynamic consumers left are sshd, dbus, syslogd, cron, watchdog,
# udevd, dhclient and busybox.
#
# Deliberately NOT in the file (could not be proven safe):
#   /usr/share/fonts      4.5 MB - ScummVM glyph source not ruled out
#   /usr/lib/locale       2.9 MB - static ScummVM embeds locale-archive paths
#   /usr/lib/perl5        2.5 MB - grep for #!/usr/bin/perl shebangs first
#   /opt/sbin/*           1.4 MB - kept deliberately; the reference material
#                                  SYSTEM_ANALYSIS.md 3.5 was read out of
run_clean() {
    local mode="$1" title base_groups groups g

    case "$mode" in
        remove) title="Remove Vendor Software"; base_groups="$(rw_clean_remove_groups)" ;;
        deep)   title="Deep Clean";             base_groups="$(rw_clean_default_groups)" ;;
        *)      err "run_clean: unknown mode '$mode'" ;;
    esac

    echo ""
    echo "════════════════════════════════════════"
    echo " $title"
    echo "════════════════════════════════════════"

    [[ -f "$CLEAN_RULES" ]] || err "missing $CLEAN_RULES"
    if ! CHECK="$(rw_clean_validate "$CLEAN_RULES")"; then
        echo "$CHECK"
        err "device-files/clean-rules.conf does not validate — refusing to clean"
    fi

    # The enabled set is the mode's group list minus every --keep-<group>. `base`
    # can never be in KEEP_GROUPS: the argument parser only accepts the names
    # rw_clean_optional_groups lists, and base is not one of them.
    groups=""
    for g in $base_groups; do
        case " $KEEP_GROUPS " in
            *" $g "*) ;;
            *) groups="$groups $g" ;;
        esac
    done
    [[ -n "$KEEP_GROUPS" ]] && info "Keeping:$KEEP_GROUPS"
    info "Groups:$groups"

    # ── The gate is NOT here ─────────────────────────────────────────────────
    #
    # It used to be, and it asked only about the clean. The p1 write is the second
    # irreversible step, so there is now ONE question covering both, asked before
    # anything is written — ask_consent above. run_clean is only reached when the
    # answer was yes, so a `read` here would be a second prompt for one decision.
    if [[ "$DRY_RUN" == "--dry-run" ]]; then
        warn "DRY RUN — nothing will be deleted"
    fi

    # Compiled on the HOST, where the parser lives, and shipped as a plan the
    # device only has to interpret. That way the device needs neither bash nor
    # a copy of the rules, and there is exactly one implementation of "which
    # groups are on" and "what a disabled group protects".
    local plan
    plan=$(mktemp)
    rw_clean_plan "$CLEAN_RULES" "$groups" > "$plan" \
        || { rm -f "$plan"; err "could not compile the clean plan"; }
    info "$(grep -c '^del' "$plan") delete(s), $(grep -c '^sweep' "$plan") sweep(s), $(grep -c '^keep' "$plan") protected path(s)"
    ssh "$DEVICE" "cat > /tmp/rw-clean-plan" < "$plan"
    rm -f "$plan"

    ssh "$DEVICE" "DRY='$DRY_RUN' sh -s" <<'REMOTE'
PLAN=/tmp/rw-clean-plan
before_root=$(df -k /            | tail -1 | awk '{print $3}')
before_data=$(df -k /home/root/data   2>/dev/null | tail -1 | awk '{print $3}')
before_bkp=$(df -k /home/root/backup  2>/dev/null | tail -1 | awk '{print $3}')

# del <path-or-glob> — the LIVE executor. No prefix, because on the device the
# paths in the plan are already correct; rw_clean_del's refusal of an empty
# prefix is what the OFFLINE executor needs and this one must not have.
# $1 is deliberately unquoted so a glob expands.
del() {
    for m in $1; do
        [ -e "$m" ] || [ -L "$m" ] || continue
        if [ -L "$m" ]; then
            sz=link
        else
            sz=$(du -sh "$m" 2>/dev/null | awk '{print $1}')
        fi
        if [ -n "$DRY" ]; then
            echo "  would delete  ${sz:-?}  $m"
        else
            echo "  delete        ${sz:-?}  $m"
            rm -rf "$m"
        fi
    done
}

echo ""
echo "-- Named stacks --"
awk -F'\t' '$1 == "del" { print $2 }' "$PLAN" | while read -r p; do
    [ -n "$p" ] && del "$p"
done

echo ""
echo "-- Whitelist sweeps: anything not on the keep list --"
awk -F'\t' '$1 == "sweep" { print $2 }' "$PLAN" | while read -r d; do
    [ -n "$d" ] || continue
    [ -d "$d" ] || continue
    keeps=$(awk -F'\t' -v dir="$d" '$1 == "keep" && $2 == dir { print $3 }' "$PLAN")
    # Dotfile globs included explicitly: BusyBox sh has no dotglob, and a vendor
    # directory that hid its payload in a dotfile would otherwise survive.
    for c in "$d"/* "$d"/.[!.]* "$d"/..?*; do
        [ -e "$c" ] || [ -L "$c" ] || continue
        n=${c##*/}
        kept=0
        for k in $keeps; do
            case "$n" in $k) kept=1; break ;; esac
        done
        [ "$kept" = 1 ] && continue
        del "$c"
    done
done

echo ""
echo "-- Truncated in place, not unlinked --"
awk -F'\t' '$1 == "truncate" { print $2 }' "$PLAN" | while read -r p; do
    [ -n "$p" ] || continue
    [ -f "$p" ] || continue
    sz=$(du -sh "$p" 2>/dev/null | awk '{print $1}')
    if [ -n "$DRY" ]; then
        echo "  would truncate ${sz:-?}  $p"
    else
        echo "  truncate      ${sz:-?}  $p"
        : > "$p"
    fi
done

# Not deletions, so they are not in the data file: in-place EDITS of a config
# file, which the four record types cannot express. Both move to the provision
# plan's `dropline` records in the next commit, which is what gets them an offline
# equivalent too — today they exist only on this path.
echo ""
echo "-- config files that reference what we just deleted --"
# /etc/profile:36 is `. /home/root/data/websign/wsplatform.conf`, and the clean
# deletes websign/ (half the D7b fix). Measured in both card captures. Left behind,
# every login prints an error for a file that is never coming back.
if grep -q 'wsplatform\.conf' /etc/profile 2>/dev/null; then
    if [ -n "$DRY" ]; then
        echo "  would remove the wsplatform.conf source line from /etc/profile"
    else
        sed -i '/wsplatform\.conf/d' /etc/profile
        echo "  /etc/profile no longer sources the deleted wsplatform.conf"
    fi
fi

# The tty4 getty costs 1.4 MB of RSS on a running device and serves nothing —
# there is no serial console (SYSTEM_ANALYSIS.md#312-serial-ports).
if grep -q '^4:12345:respawn:/sbin/getty 38400 tty4' /etc/inittab 2>/dev/null; then
    if [ -n "$DRY" ]; then
        echo "  would remove tty4 getty line from /etc/inittab"
    else
        sed -i '/^4:12345:respawn:\/sbin\/getty 38400 tty4/d' /etc/inittab
        echo "  tty4 getty removed (takes effect next boot)"
    fi
fi

rm -f "$PLAN"

if [ -z "$DRY" ]; then
    echo ""
    echo "Freed:"
    after=$(df -k / | tail -1 | awk '{print $3}')
    echo "  rootfs:  $(( (before_root - after) / 1024 )) MB"
    if [ -n "$before_data" ]; then
        a=$(df -k /home/root/data | tail -1 | awk '{print $3}')
        echo "  data:    $(( (before_data - a) / 1024 )) MB"
    fi
    if [ -n "$before_bkp" ]; then
        a=$(df -k /home/root/backup | tail -1 | awk '{print $3}')
        echo "  backup:  $(( (before_bkp - a) / 1024 )) MB"
    fi
    echo ""
    df -h / /home/root/data /home/root/backup 2>/dev/null
fi
REMOTE
    if [[ "$DRY_RUN" == "--dry-run" ]]; then
        ok "Dry run complete — nothing was deleted"
    else
        ok "$title complete"
    fi
}


# ── the 500 mA USB power budget, on p1 ──────────────────────────────────────
#
# ⚠️ The gate/backup/patch/verify/rollback sequence is NOT here. It is
# lib/rw-usbpower.sh, shared byte-for-byte with commissioning/commission-offline.sh
# and usb_host/build-and-deploy.sh, with only the transport differing — this path
# pulls the image over scp, patches it on the host and pushes it back. Never write a
# second copy of that sequence into a caller: it is the one step
# tests/rw_provision_test.sh group E cannot compare between the two executors, so a
# duplicate would drift undetected (IMPROVEMENT_PLAN.md F15, C12).
#
# Why this cannot be an ordinary provision-rules.conf record, when the rest of USB
# host mode is: the value lives in the device tree appended INSIDE uImage-system on
# p1, and omap2430.c reads it at driver probe, before any init script exists. There
# is no file on the normal filesystem to edit. The other two USB mechanisms are
# entirely on p6 and are plain `usb`-group records.
P1_STATE="not attempted"
run_usbpower() {
    local work prereq

    if [[ "$DO_USB_POWER" -eq 0 ]]; then
        case " $NO_PROV_GROUPS " in
            *" usb "*) P1_STATE="skipped (--no-usb)" ;;
            *)         P1_STATE="skipped (--no-usb-power)" ;;
        esac
        warn "$P1_STATE: p1 untouched. The budget stays at the vendor's 100 mA, so a"
        warn "  controller needs a POWERED hub. A power cycle remains a free undo."
        return 0
    fi
    # A dry run needs no consent: it writes nothing. ask_consent has not even been
    # called on that path — it is asked after --status/--hostname have had their
    # chance to exit, so that neither of those ever sees the question.
    if [[ "$DRY_RUN" != "--dry-run" && "$CONSENT" != "yes" ]]; then
        P1_STATE="skipped (no backup confirmed)"
        warn "$P1_STATE — p1 untouched."
        return 0
    fi

    # shellcheck source=../lib/rw-usbpower.sh
    . "$REPO_ROOT/lib/rw-usbpower.sh"
    if ! prereq="$(rw_usbpower_prereqs)"; then
        P1_STATE="skipped (missing prerequisites)"
        warn "cannot patch p1 — the device-tree tooling is not available here:"
        printf '%s\n' "$prereq"
        warn "  Install python3, or pass --no-usb-power to say so deliberately."
        return 0
    fi

    work=$(mktemp -d)
    if [[ "$DRY_RUN" == "--dry-run" ]]; then
        RW_USBPOWER_DRY=1 rw_usbpower_apply_ssh "$DEVICE" "$work" || true
        P1_STATE="not attempted (dry run)"
    elif rw_usbpower_apply_ssh "$DEVICE" "$work"; then
        if [[ "$(rw_usbpower_want)" == both ]]; then
            P1_STATE="500 mA + host mode (patched and verified)"
        else
            P1_STATE="500 mA (patched and verified)"
        fi
    else
        P1_STATE="FAILED — read the block above"
        warn "the p1 patch did not succeed. Everything else this run installed is"
        warn "  in place; the USB budget is whatever it was."
    fi
    rm -rf "$work"
    return 0
}


# ── SSH check ───────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " RoomWizard System Setup"
echo "════════════════════════════════════════"

info "Testing SSH connection to $DEVICE_IP..."
# One gate, in lib/rw-ssh.sh: it tells "down" from "up and refusing us" and, on a
# terminal, offers to generate a key and ssh-copy-id it. This is FIRST contact for
# anyone who did not prep the card, so the old `check IP and SSH key` — advice about
# a key nothing offered to make — was the whole of F16.
rw_ssh_gate "$DEVICE" || err "Cannot continue without SSH to $DEVICE"
ok "SSH OK"

# ── dry-run: report what would change and exit WITHOUT setup or a reboot ────
#
# --dry-run no longer needs a clean flag to be meaningful: the clean is the default,
# so a bare `provision.sh <ip> --dry-run` is the preview of a bare run.
if [[ "$DRY_RUN" == "--dry-run" ]]; then
    if [[ "$DO_CLEAN" -eq 1 ]]; then
        run_clean "$CLEAN_MODE"
    else
        info "--no-clean: nothing would be deleted"
    fi
    echo ""
    echo "════════════════════════════════════════"
    echo " USB power budget (p1)"
    echo "════════════════════════════════════════"
    run_usbpower
    echo ""
    info "Dry run only — no setup performed, nothing written, device not rebooted."
    exit 0
fi

# ── hostname-only mode ──────────────────────────────────────────────────────
# Targeted and reboot-free, so it can be run against an already-commissioned
# unit — including one that is a live display and must not be rebooted, which is
# the case this flag exists for. The work itself is commissioning/set-hostname.sh, the same
# script commissioning/card-prep.sh runs offline; it is staged to /tmp rather than
# installed, because it is a one-shot and nothing on the device calls it again
# (so it stays out of report_script_versions' drift list).
if [[ "$FLAG" == "--hostname" ]]; then
    echo ""
    info "Setting host name to '$NEW_HOSTNAME'..."
    scp -q "$SCRIPT_DIR/set-hostname.sh" "$DEVICE:/tmp/set-hostname.sh"
    ssh "$DEVICE" "chmod +x /tmp/set-hostname.sh"
    ssh "$DEVICE" "/tmp/set-hostname.sh $NEW_HOSTNAME"
    ssh "$DEVICE" "rm -f /tmp/set-hostname.sh"
    ok "Host name set"
    echo ""
    info "Verifying:"
    ssh "$DEVICE" bash <<'REMOTE'
echo "  hostname:      $(hostname)"
echo "  /etc/hostname: $(cat /etc/hostname)"
echo "  /etc/hosts:"
sed 's/^/    /' /etc/hosts
REMOTE
    echo ""
    if ssh "$DEVICE" "test -L /etc/rc5.d/S30avahi-daemon" 2>/dev/null; then
        ok "mDNS is enabled — after the next reboot, try: ssh root@$NEW_HOSTNAME.local"
    else
        warn "mDNS is NOT enabled on this device, so <name>.local will not resolve."
        warn "Run a full './commissioning/provision.sh $DEVICE_IP' to install it (that reboots)."
    fi
    echo ""
    exit 0
fi

# ── status-only mode ────────────────────────────────────────────────────────
if [[ "$FLAG" == "--status" ]]; then
    echo ""
    info "Device status:"
    ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Disk:    $(df -h / | tail -1 | awk '{print $3 " used, " $4 " free (" $5 " used)"}')"
echo "Memory:  $(free -h | grep Mem | awk '{print $3 " used, " $7 " available"}')"
wdog_cron=$(crontab -l 2>/dev/null | grep -c '^[^#].*/watchdog\.sh')
echo "SW watchdog cron: $([ $wdog_cron -eq 0 ] && echo 'disabled ✓' || echo 'ENABLED ✗')"
echo "Bypass file:      $([ -f /var/watchdog_test ] && echo 'present ✓' || echo 'MISSING ✗')"
echo "HW watchdog:      $([ -c /dev/watchdog ] && echo 'active (fed by /usr/sbin/watchdog)' || echo 'n/a')"
echo "Bloatware procs:  $(ps aux | grep -E 'java|Xorg|browser' | grep -v grep | wc -l)"
echo "Default app:      $(cat /opt/roomwizard/default-app 2>/dev/null || echo '(not set)')"
echo ""
echo "Active cron jobs:"
crontab -l 2>/dev/null | grep -v '^#' | grep -v '^$' | sed 's/^/  /'
echo ""
echo "Init services in rc5.d:"
ls -1 /etc/rc5.d/S* 2>/dev/null | sed 's|.*/||; s/^/  /'
REMOTE
    echo ""
    info "Deployed script versions:"
    report_script_versions
    echo ""
    exit 0
fi

# ── Consent, once, before the first write ───────────────────────────────────
#
# Deliberately AFTER --status and --hostname have had their chance to exit: neither
# writes anything irreversible, so neither should ever see the question. Everything
# below this line changes the device and ends in a reboot.
ask_consent

# ── 1. Provision: the boot scripts, the links, sshd, the config fix-ups ─────
echo ""
echo "════════════════════════════════════════"
echo " 1. Provision"
echo "════════════════════════════════════════"

# ⚠️ The decisions are NOT here. Every file, link, mode and config edit lives in
# device-files/provision-rules.conf with a reason per entry, read by this script AND
# by commissioning/commission-offline.sh, so the two cannot drift (IMPROVEMENT_PLAN.md C12).
#
# What used to be here: five scp calls, an `ssh <<'REMOTE'` block of ln -sf, a second
# one for avahi, a four-command sed block over sshd_config, and a third for the
# sysctl file — every one of them written out a second time in commissioning/commission-offline.sh.
# They HAD drifted: this path deleted stale rc*.d links before relinking and the
# offline path did not.
#
# The plan is compiled HERE, where the parser lives, and shipped as data the device
# only interprets — the same division as the clean. The interpreter itself comes
# from rw_provision_online_script, so there is one implementation of each verb and
# `--dry-run` on either path prints the same resolved set.
PROV_RULES="$DEVICE_FILES/provision-rules.conf"
[[ -f "$PROV_RULES" ]] || err "missing $PROV_RULES"
if ! PCHECK="$(rw_provision_validate "$PROV_RULES" "$REPO_ROOT")"; then
    echo "$PCHECK"
    err "device-files/provision-rules.conf does not validate — refusing to provision"
fi
# A boot link the clean's whitelist does not name is deleted by the next
# --deep-clean, so the unit would boot right once and lose the link afterwards.
if ! KCHECK="$(rw_provision_check_keeps "$PROV_RULES" "$CLEAN_RULES")"; then
    echo "$KCHECK"
    err "a boot link in provision-rules.conf is not kept by clean-rules.conf"
fi

PROV_GROUPS="base"
for _g in $(rw_provision_optional_groups); do
    case " $NO_PROV_GROUPS " in
        *" $_g "*) ;;
        *) PROV_GROUPS="$PROV_GROUPS $_g" ;;
    esac
done
[[ -n "$NO_PROV_GROUPS" ]] && info "Skipping:$NO_PROV_GROUPS"

PROV_PLAN=$(mktemp)
rw_provision_plan "$PROV_RULES" "$PROV_GROUPS" > "$PROV_PLAN" \
    || { rm -f "$PROV_PLAN"; err "could not compile the provision plan"; }
info "Provision plan: $(rw_provision_plan_summary "$PROV_PLAN")"

# The install verb's SOURCE bytes are on this host, so they go over scp first and
# the remote interpreter only sets the declared mode. That asymmetry is the whole
# reason the two executors exist; everything else about them is shared.
#
# ⚠️ The loop lives in lib/rw-provision.sh, not here. It was written out in full in
# this script and again in usb_host/build-and-deploy.sh, both reading the plan on
# stdin with an `ssh` in the body — so the first ssh ate the rest of the plan and
# both installed exactly one file (B28). Do not inline it again.
rw_provision_push_installs "$PROV_PLAN" "$REPO_ROOT" "$DEVICE" \
    || { rm -f "$PROV_PLAN"; err "could not copy the provision sources to the device"; }

ssh "$DEVICE" "cat > /tmp/rw-provision-plan" < "$PROV_PLAN"
rm -f "$PROV_PLAN"
rw_provision_online_script | ssh "$DEVICE" "cat > /tmp/rw-provision.sh"
ssh "$DEVICE" "sh /tmp/rw-provision.sh /tmp/rw-provision-plan; rc=\$?; rm -f /tmp/rw-provision.sh /tmp/rw-provision-plan; exit \$rc" \
    || err "the provision step failed on the device"
ok "Boot scripts, boot links, sshd, sysctl and the config fix-ups done"

# ── 2. Disable the Steelcase services ──────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 2. Disable Steelcase Bloatware"
echo "════════════════════════════════════════"

# disable-steelcase.sh was installed by the provision plan above, with its mode.
# Running it is an ACTION rather than a piece of state, so it is not a plan record —
# and it has no offline equivalent by nature: it stops running processes and writes a
# crontab. /etc/init.d/roomwizard-app re-runs it on every boot, which is what makes
# the offline path's omission harmless.
info "Running $REMOTE_DIR/disable-steelcase.sh..."
ssh "$DEVICE" "$REMOTE_DIR/disable-steelcase.sh"
ok "Steelcase bloatware disabled"

# ── 3. Apply the sysctl settings now ───────────────────────────────────────
#
# NOTE on firewall: this image has no iptables binary, no ip_tables.ko in
# /lib/modules/4.14.52, no busybox iptables applet, no TCP wrappers and no package
# manager to add any of them. The kernel has CONFIG_NETFILTER=y and ip_tables was
# never compiled. Network security is: no unnecessary services, sshd hardened,
# sysctl hardening, and a home network the device is not exposed through.
#
# The FILE was installed by the plan; applying it to the running kernel is again an
# action. The fallback exists because a kernel without sysctl.d support would accept
# the file and apply none of it, silently.
info "Applying kernel security settings..."
ssh "$DEVICE" sh -s <<'SYSCTL'
sysctl -p /etc/sysctl.d/99-security.conf 2>/dev/null || {
    sysctl -w kernel.randomize_va_space=2 2>/dev/null
    sysctl -w kernel.dmesg_restrict=1 2>/dev/null
    sysctl -w kernel.sysrq=0 2>/dev/null
}
SYSCTL
ok "Kernel security settings applied"

# ── 4. Report what vendor software is still on disk ────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 4. Vendor Software On Disk"
echo "════════════════════════════════════════"

info "Analyzing filesystem..."
ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Bloatware analysis:"
echo "  Jetty:    $(du -sh /opt/jetty-9-4-11 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  OpenJRE:  $(du -sh /opt/openjre-8 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  HSQLDB:   $(du -sh /opt/hsqldb 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  CJK Font: $(du -sh /usr/share/cjkfont 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  X11:      $(du -sh /usr/share/X11 2>/dev/null | awk '{print $1}' || echo 'removed')"
echo "  SNMP:     $(du -sh /usr/share/snmp 2>/dev/null | awk '{print $1}' || echo 'removed')"
REMOTE

# ── The clean: ONE mechanism, two flags that differ by a group ──────────────
#
# --remove and --deep-clean are both `rw_clean_plan` over
# device-files/clean-rules.conf; the only difference is whether the `sweeps` group
# is on. --remove used to be ~85 lines of hardcoded `rm -rf` inside an
# `ssh <<'REMOTE'` heredoc, which restated decisions the data file already records
# WITH reasons, and had drifted from them in two places: it deleted
# /home/root/log/concurrent.log (syslogd holds it open — unlinking it live leaves
# syslogd writing to an unlinked inode) and it carried its own keep-comments for
# /opt/pv02 and /opt/sound. A comment inside it said so outright.
#
# Every path it named is either a rule in that file, covered by a sweep, or
# recorded there as a deliberate omission — see its "Three deliberate differences"
# header. IMPROVEMENT_PLAN.md C11.
# The DEFAULT is `deep` (IMPROVEMENT_PLAN.md C13) — the same default
# commissioning/commission-offline.sh has always had, so the two paths leave the same
# unit. --no-clean is the opt-out, and a declined backup question is the other one.
CLEAN_STATE="not attempted"
if [[ "$DO_CLEAN" -eq 0 ]]; then
    CLEAN_STATE="skipped (--no-clean)"
    info "--no-clean: vendor software left in place"
elif [[ "$CONSENT" != "yes" ]]; then
    CLEAN_STATE="skipped (no backup confirmed)"
    warn "$CLEAN_STATE — vendor software left in place"
else
    run_clean "$CLEAN_MODE"
    case "$CLEAN_MODE" in
        deep)   CLEAN_STATE="deep clean (named stacks + whitelist sweeps)" ;;
        remove) CLEAN_STATE="named stacks only (--remove, no sweeps)" ;;
    esac
    [[ -n "$KEEP_GROUPS" ]] && CLEAN_STATE="$CLEAN_STATE, keeping$KEEP_GROUPS"
fi

# ── 5. The 500 mA USB power budget on p1 ───────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 5. USB Power Budget (p1)"
echo "════════════════════════════════════════"
run_usbpower

# ── Status summary ──────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " Status Summary"
echo "════════════════════════════════════════"

ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Disk:    $(df -h / | tail -1 | awk '{print $3 " used, " $4 " free (" $5 " used)"}')"
echo "Memory:  $(free -h | grep Mem | awk '{print $3 " used, " $7 " available"}')"
wdog_cron=$(crontab -l 2>/dev/null | grep -c '^[^#].*/watchdog\.sh')
echo "SW watchdog cron: $([ $wdog_cron -eq 0 ] && echo 'disabled ✓' || echo 'ENABLED ✗')"
echo "Bypass file:      $([ -f /var/watchdog_test ] && echo 'present ✓' || echo 'MISSING ✗')"
echo "HW watchdog:      $([ -c /dev/watchdog ] && echo 'active (fed by /usr/sbin/watchdog)' || echo 'n/a')"
echo "Default app:      $(cat /opt/roomwizard/default-app 2>/dev/null || echo '(not set)')"
echo ""
echo "Active cron jobs:"
crontab -l 2>/dev/null | grep -v '^#' | grep -v '^$' | sed 's/^/  /'
REMOTE

# Confirm the two scripts this run just pushed are byte-identical on the device.
# scp reporting success is not the same as the right bytes landing, and these are
# the files whose silent staleness cost a session (B18, B19).
echo ""
info "Deployed script versions:"
report_script_versions

# ── What this run actually did ──────────────────────────────────────────────
#
# The verdicts follow what was established, not merely that we got this far — a
# green tick on a step that was skipped is the thing C13 is about.
echo ""
info "This run:"
case "$CLEAN_STATE" in
    "deep clean"*|"named stacks"*) ok "clean: $CLEAN_STATE" ;;
    *)                             warn "clean: $CLEAN_STATE" ;;
esac
case "$P1_STATE" in
    "500 mA"*) ok "USB power budget: $P1_STATE" ;;
    *)         warn "USB power budget: $P1_STATE" ;;
esac

# ── Reboot ──────────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " Rebooting"
echo "════════════════════════════════════════"

# The reboot is also what makes a p1 patch live: omap2430.c reads the power
# property at driver probe, so the new budget takes effect on the next boot and no
# earlier. That happens here, so nothing further is needed.
info "Rebooting device..."
ssh "$DEVICE" reboot || true
ok "Device is rebooting"

echo ""
echo "  System setup complete! Wait ~30 s then: ssh root@$DEVICE_IP"
echo ""
echo "  Next step — build and deploy all components:"
echo "    ./deploy-all.sh $DEVICE_IP"
echo ""
echo "  Or deploy individually:"
echo "    cd native_apps      && ./build-and-deploy.sh $DEVICE_IP set-default"
echo "    cd vnc_client        && ./build-and-deploy.sh $DEVICE_IP"
echo "    cd scummvm-roomwizard && ./build-and-deploy.sh $DEVICE_IP"
echo ""
if [[ "$P1_STATE" == "500 mA"* ]]; then
    echo "  USB: the reboot above makes the 500 mA budget live. Then plug a"
    echo "  controller in DIRECTLY, with no powered hub — that is the check that the"
    echo "  p1 patch took effect:"
    echo "    ssh root@$DEVICE_IP '/etc/init.d/usb-host status; lsusb'"
    echo "  To undo: copy uImage-system.vendor over uImage-system on p1."
    echo ""
fi
