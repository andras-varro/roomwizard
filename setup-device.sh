#!/bin/bash
#
# setup-device.sh — Phase 2: One-time system setup over SSH
#
# Run this ONCE after the first SSH login to a freshly commissioned device.
# It disables Steelcase bloatware, installs the generic app launcher, and
# sets up audio + time-sync boot scripts.
#
# Usage:
#   ./setup-device.sh <target>                 # system setup + reboot
#   ./setup-device.sh <target> --remove        # + remove bloatware files (~178 MB freed)
#   ./setup-device.sh <target> --status        # show device status only
#   ./setup-device.sh <target> --hostname rw09 # set the host name only, no reboot
#
# <target> is an IPv4 address or a host name — `rw09.local` works once mDNS is
# enabled (this script does that) and the unit has a unique name (--hostname).
#
# Prerequisites:
#   - Device commissioned with commission-roomwizard.sh (Phase 1)
#   - SSH access as root
#
# What it does:
#   1. Deploys disable-steelcase.sh → /opt/roomwizard/
#   2. Runs disable-steelcase.sh (watchdog bypass, cron cleanup, service stop)
#   3. Installs roomwizard-app-init.sh as /etc/init.d/roomwizard-app
#      Registers the init service (priority S99)
#      Deploys audio-enable + time-sync boot scripts, and enables avahi (mDNS)
#   4. Hardens SSH (PermitEmptyPasswords=no, brute-force limits)
#   5. Applies kernel/sysctl security settings (ASLR, no ip_forward, etc.)
#   6. Optionally removes bloatware files + Steelcase artifacts (--remove)
#   7. Reboots device
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

# Both halves of the job are DATA in one file each, shared with
# commission-offline.sh so that the live and offline passes cannot drift:
#
#   device-files/clean-rules.conf      what is REMOVED  (rw-clean.sh)
#   device-files/provision-rules.conf  what is INSTALLED (rw-provision.sh)
#
# Each library is the parser and the plan compiler. The executors differ — "/" is
# the correct prefix on a device and a refused one offline, and on this path the
# work happens on the far side of an ssh pipe — but there is one implementation of
# each, and rw_provision_online_script is the one this script ships to the device
# (IMPROVEMENT_PLAN.md F10, C11, C12).
# shellcheck source=rw-identify.sh
. "$SCRIPT_DIR/rw-identify.sh"
# shellcheck source=rw-clean.sh
. "$SCRIPT_DIR/rw-clean.sh"
# shellcheck source=rw-provision.sh
. "$SCRIPT_DIR/rw-provision.sh"

# ── --keep-<group> and --no-<group> are extracted before positional parsing ──
#
# --keep-<g> switches off part of the CLEAN, --no-<g> part of the PROVISION. Both
# are named after the groups in their own data file, so neither list is repeated
# here. They are pulled out of "$@" first because everything below is positional
# ($1 target, $2 flag, $3 --dry-run) and a --keep-browser sitting in $3 would
# otherwise be rejected as an unknown option.
KEEP_GROUPS=""
NO_PROV_GROUPS=""
_ARGS=()
for _a in "$@"; do
    case "$_a" in
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

DEVICE_IP="${1:-}"
FLAG="${2:-}"
DEVICE="root@${DEVICE_IP}"
REMOTE_DIR="/opt/roomwizard"
INIT_SCRIPT="/etc/init.d/roomwizard-app"
# Files installed onto the device verbatim. They live in device-files/ rather
# than in a heredoc here so that the offline installer writes the same bytes —
# two copies of an init script is two things to keep in step, and the one that
# drifts is discovered on a device that boots to a black screen.
DEVICE_FILES="$SCRIPT_DIR/device-files"
CLEAN_RULES="$DEVICE_FILES/clean-rules.conf"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }

# ── usage ───────────────────────────────────────────────────────────────────
usage() {
    echo "Usage: $0 <target> [--remove|--deep-clean|--status] [--dry-run]"
    echo "       $0 <target> --hostname NAME"
    echo ""
    echo "  <target>          Device IPv4 address, or a host name (e.g. rw09.local)"
    echo "  --remove          Delete the named vendor software stacks (~178 MB +"
    echo "                    the 472 MB factory-restore payload)."
    echo "  --deep-clean      --remove PLUS the whitelist sweeps: anything in"
    echo "                    /etc/rc*.d, /opt or the data partitions that the"
    echo "                    keep-list does not name. ~560 MB more."
    echo "                    Both read device-files/clean-rules.conf; the only"
    echo "                    difference is the 'sweeps' group."
    echo "                    Both ask for a full-card backup first. Neither is"
    echo "                    reversible on the device — the restore payload goes."
    echo "  --status          Show device status only (no changes)"
    echo "  --dry-run         With --remove or --deep-clean: list what would be"
    echo "                    deleted, delete nothing"
    echo "  --keep-<group>    Leave one stack on disk. Groups:"
    echo "                    $(rw_clean_optional_groups)"
    echo "                    Files only — it does not re-enable a boot link, because"
    echo "                    the rc*.d whitelist is what removes an unknown service."
    echo "                    --keep-factory keeps the 472 MB restore payload;"
    echo "                    --keep-sweeps turns --deep-clean into --remove."
    echo "  --no-<group>      Skip one group of the provision plan. Groups:"
    echo "                    $(rw_provision_optional_groups)"
    echo "                    --no-mdns leaves <name>.local unresolvable; --no-sshd"
    echo "                    leaves PermitEmptyPasswords at the factory 'yes'."
    echo "  --hostname NAME   Set the device host name only, and exit. No reboot."
    echo "                    NAME is a single label — 'rw09', not 'rw09.local'."
    exit 1
}

[[ -z "$DEVICE_IP" ]] && usage

# Validate the target before doing anything: every step past here is destructive
# and ends in a reboot (../IMPROVEMENT_PLAN.md B19).
#
# An IPv4 address OR a DNS name is accepted. The name form is what makes
# `./setup-device.sh rw09.local` work, which is the whole point of enabling
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
        "$SCRIPT_DIR/disable-steelcase.sh:$REMOTE_DIR/disable-steelcase.sh"
        "$SCRIPT_DIR/roomwizard-app-init.sh:$INIT_SCRIPT"
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

# No mode takes a fourth argument, so a stray one is a mistake — say so rather
# than ignoring it, for the same reason the flag case above is exhaustive.
if [[ -n "${4:-}" ]]; then
    echo "Unexpected argument: $4"; echo ""; usage
fi

# --hostname is the one flag that takes a value, so it consumes $3 and --dry-run
# cannot also live there.
NEW_HOSTNAME=""
DRY_RUN=""
if [[ "$FLAG" == "--hostname" ]]; then
    NEW_HOSTNAME="${3:-}"
    if [[ -z "$NEW_HOSTNAME" ]]; then
        echo "--hostname requires a NAME"; echo ""; usage
    fi
    # Validated HERE, at parse time, for two reasons: a typo should not need a
    # reachable device to be caught, and the name is later interpolated into an
    # ssh command string, so nothing unexpected should ever get that far.
    # set-hostname.sh validates again on the device and remains the authority.
    if [[ ! "$NEW_HOSTNAME" =~ ^[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?$ ]]; then
        echo "Not a valid host name: $NEW_HOSTNAME"
        echo "  RFC-1123, single label: letters, digits and hyphens, alphanumeric at both ends."
        echo "  Set 'rw09', not 'rw09.local' — mDNS appends the .local itself."
        echo ""
        usage
    fi
else
    DRY_RUN="${3:-}"
    if [[ -n "$DRY_RUN" && "$DRY_RUN" != "--dry-run" ]]; then
        echo "Unknown option: $DRY_RUN"; echo ""; usage
    fi
fi

# NOTE: the target was validated at the top of this file, against both an IPv4
# address and a DNS name. A second, weaker check used to sit here; it was
# unreachable, and deleting it is what makes `rw09.local` usable.

# --deep-clean is --remove plus the `sweeps` group, and both are one call to
# run_clean. It no longer rewrites FLAG to "--remove": that mattered when the two
# were separate mechanisms running in sequence, and now it would run the clean
# twice.
DEEP_CLEAN=0
[[ "$FLAG" == "--deep-clean" ]] && DEEP_CLEAN=1


# ── deep clean ──────────────────────────────────────────────────────────────
#
# ⚠️ The decisions are NOT here. Every keep and every delete lives in
# device-files/clean-rules.conf with a reason per entry, read by this script and
# by commission-offline.sh so the live and offline cleans cannot drift
# (IMPROVEMENT_PLAN.md F10). rw-clean.sh compiles that file into a plan; what
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
    local mode="$1" title base_groups groups g confirm2

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

    # ── The gate: asked once, asked FIRST, and about the backup ──────────────
    #
    # Same question and same wording as commission-offline.sh's phase 0, because it
    # is the same precondition. There is no per-flag opt-out to soften it with: a
    # user who cleans a unit of its vendor software has made a decision, and the
    # recovery path for that decision is a host-side card image, not a switch here
    # (IMPROVEMENT_PLAN.md C11). The device has no serial console, so a failed boot
    # yields no diagnostics at all (SYSTEM_ANALYSIS.md#312-serial-ports).
    if [[ "$DRY_RUN" == "--dry-run" ]]; then
        warn "DRY RUN — nothing will be deleted"
        confirm2="yes"
    else
        warn "This removes the Steelcase software from this device: the vendor"
        warn "services, their data and configuration, and — unless --keep-factory —"
        warn "the 472 MB on-device factory-restore payload."
        warn ""
        warn "The original RoomWizard functionality does not come back afterwards,"
        warn "and the device's own restore mechanism goes with it. The 5 MB fallback"
        warn "kernel is kept either way; p1 is never touched."
        warn ""
        warn "PRECONDITION: a full-card image backup exists somewhere other than"
        warn "this card. Recovery from a bad boot means dd-ing it back."
        read -r -p "  Do you have that backup? (yes/no): " confirm2
        if [[ "$confirm2" != "yes" ]]; then
            echo "  Make one first: pull the card, then"
            echo "    sudo dd if=/dev/sdX of=card.img bs=4M status=progress"
            info "Clean cancelled"
            return 0
        fi
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


# ── SSH check ───────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " RoomWizard System Setup"
echo "════════════════════════════════════════"

info "Testing SSH connection to $DEVICE_IP..."
ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
    || err "Cannot reach $DEVICE — check IP and SSH key"
ok "SSH OK"

# ── dry-run: report the clean and exit WITHOUT running setup or rebooting ──
if [[ "$DRY_RUN" == "--dry-run" ]]; then
    if [[ "$DEEP_CLEAN" == "1" ]]; then
        run_clean deep
    elif [[ "$FLAG" == "--remove" ]]; then
        run_clean remove
    else
        err "--dry-run needs --remove or --deep-clean — there is nothing else to preview"
    fi
    echo ""
    info "Dry run only — no setup performed, device not rebooted."
    exit 0
fi

# ── hostname-only mode ──────────────────────────────────────────────────────
# Targeted and reboot-free, so it can be run against an already-commissioned
# unit — including one that is a live display and must not be rebooted, which is
# the case this flag exists for. The work itself is set-hostname.sh, the same
# script commission-roomwizard.sh runs offline; it is staged to /tmp rather than
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
        warn "Run a full './setup-device.sh $DEVICE_IP' to install it (that reboots)."
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

# ── 1. Provision: the boot scripts, the links, sshd, the config fix-ups ─────
echo ""
echo "════════════════════════════════════════"
echo " 1. Provision"
echo "════════════════════════════════════════"

# ⚠️ The decisions are NOT here. Every file, link, mode and config edit lives in
# device-files/provision-rules.conf with a reason per entry, read by this script AND
# by commission-offline.sh, so the two cannot drift (IMPROVEMENT_PLAN.md C12).
#
# What used to be here: five scp calls, an `ssh <<'REMOTE'` block of ln -sf, a second
# one for avahi, a four-command sed block over sshd_config, and a third for the
# sysctl file — every one of them written out a second time in commission-offline.sh.
# They HAD drifted: this path deleted stale rc*.d links before relinking and the
# offline path did not.
#
# The plan is compiled HERE, where the parser lives, and shipped as data the device
# only interprets — the same division as the clean. The interpreter itself comes
# from rw_provision_online_script, so there is one implementation of each verb and
# `--dry-run` on either path prints the same resolved set.
PROV_RULES="$DEVICE_FILES/provision-rules.conf"
[[ -f "$PROV_RULES" ]] || err "missing $PROV_RULES"
if ! PCHECK="$(rw_provision_validate "$PROV_RULES" "$SCRIPT_DIR")"; then
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
info "Provision plan: $(grep -c . "$PROV_PLAN") action(s) — $(grep -c '^install' "$PROV_PLAN") install, $(grep -c '^link' "$PROV_PLAN") link, $(grep -c '^unlink' "$PROV_PLAN") unlink"

# The install verb's SOURCE bytes are on this host, so they go over scp first and
# the remote interpreter only sets the declared mode. That asymmetry is the whole
# reason the two executors exist; everything else about them is shared.
while IFS=$'\t' read -r _kind _mode _target _src; do
    [[ "$_kind" == "install" ]] || continue
    [[ -f "$SCRIPT_DIR/$_src" ]] || err "missing $SCRIPT_DIR/$_src"
    ssh "$DEVICE" "mkdir -p '$(dirname "$_target")'"
    scp -q "$SCRIPT_DIR/$_src" "$DEVICE:$_target" || err "could not copy $_src to $_target"
    info "copied $_src → $_target"
done < "$PROV_PLAN"

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
if [[ "$DEEP_CLEAN" == "1" ]]; then
    run_clean deep
elif [[ "$FLAG" == "--remove" ]]; then
    run_clean remove
else
    info "Vendor software left in place (use --remove or --deep-clean)"
fi

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

# ── Reboot ──────────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " Rebooting"
echo "════════════════════════════════════════"

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
