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

# The keep/delete decisions --deep-clean applies are DATA, in one file, shared
# with commission-offline.sh so that the live and offline cleans cannot drift.
# rw-clean.sh is the parser and the plan compiler; the executor below is this
# script's own, because "/" is the correct prefix on a device and a refused one
# offline (IMPROVEMENT_PLAN.md F10).
# shellcheck source=rw-clean.sh
. "$SCRIPT_DIR/rw-clean.sh"

# ── --keep-<group> is extracted before the positional parsing ──────────────
#
# The clean's opt-outs are named after the groups in clean-rules.conf, so the
# list of valid ones is not repeated here. They are pulled out of "$@" first
# because everything below is positional ($1 target, $2 flag, $3 --dry-run) and a
# --keep-browser sitting in $3 would otherwise be rejected as an unknown option.
KEEP_GROUPS=""
_ARGS=()
for _a in "$@"; do
    case "$_a" in
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
    echo "  --remove          Also remove vendor bloatware files (~178 MB freed)"
    echo "  --deep-clean      --remove PLUS extended cleanup (~560 MB more)."
    echo "                    Includes the 474 MB on-device factory restore image."
    echo "                    See IMPROVEMENT_PLAN.md and the warnings it prints."
    echo "  --status          Show device status only (no changes)"
    echo "  --dry-run         With --deep-clean: list what would be deleted, delete nothing"
    echo "  --keep-<group>    With --deep-clean: leave one stack on disk. Groups:"
    echo "                    $(rw_clean_optional_groups)"
    echo "                    Files only — it does not re-enable a boot link, because"
    echo "                    the rc*.d whitelist is what removes an unknown service."
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

# --deep-clean implies --remove
DEEP_CLEAN=0
if [[ "$FLAG" == "--deep-clean" ]]; then
    DEEP_CLEAN=1
    FLAG="--remove"
fi


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
run_deep_clean() {
    echo ""
    echo "════════════════════════════════════════"
    echo " Deep Clean"
    echo "════════════════════════════════════════"

    [[ -f "$CLEAN_RULES" ]] || err "missing $CLEAN_RULES"
    if ! CHECK="$(rw_clean_validate "$CLEAN_RULES")"; then
        echo "$CHECK"
        err "device-files/clean-rules.conf does not validate — refusing to clean"
    fi

    local groups="base" g
    for g in $(rw_clean_optional_groups); do
        case " $KEEP_GROUPS " in
            *" $g "*) ;;
            *) groups="$groups $g" ;;
        esac
    done
    [[ -n "$KEEP_GROUPS" ]] && info "Keeping:$KEEP_GROUPS"

    DEL_FACTORY=0
    if [[ "$DRY_RUN" == "--dry-run" ]]; then
        warn "DRY RUN — nothing will be deleted"
        DEL_FACTORY=1   # so the dry run shows the factory images too
        confirm2="yes"
    else
        warn "This permanently deletes ~85 MB of rootfs + data-partition files."
        read -p "Continue? (yes/no): " confirm2

        if [[ "$confirm2" == "yes" ]]; then
            echo ""
            warn "OPTIONAL: also delete the 474 MB on-device factory restore image"
            warn "  (/home/root/backup/factory/*.img + .tar.gz, dated Jan 2022)"
            warn "  This removes the device's SELF-restore capability. Recovery would"
            warn "  then require pulling the SD card and dd-ing your host-side backup."
            warn "  PRECONDITION: verify roomwizard.img on this host is intact FIRST."
            read -p "  Delete factory images too? (yes/no): " confirm_factory
            [[ "$confirm_factory" == "yes" ]] && DEL_FACTORY=1
        fi
    fi
    [[ "$DEL_FACTORY" == "1" ]] && groups="$groups factory"

    if [[ "$confirm2" == "yes" ]]; then
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

# Not a deletion, so it is not in the data file: an EDIT of a config file, which
# the four record types cannot express and which has no offline equivalent worth
# having (the tty4 getty costs 1.4 MB of RSS on a running device only).
echo ""
echo "-- inittab: drop the pointless tty4 getty (~1.4 MB RSS) --"
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
            ok "Deep clean complete"
        fi
    else
        info "Deep clean cancelled"
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

# ── dry-run: report the deep clean and exit WITHOUT running setup or rebooting ──
if [[ "$DRY_RUN" == "--dry-run" ]]; then
    [[ "$DEEP_CLEAN" == "1" ]] || err "--dry-run is only supported together with --deep-clean"
    run_deep_clean
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

# ── 1. Deploy disable-steelcase.sh ─────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 1. Disable Steelcase Bloatware"
echo "════════════════════════════════════════"

info "Deploying disable-steelcase.sh → $REMOTE_DIR/"
ssh "$DEVICE" "mkdir -p $REMOTE_DIR"
scp "$SCRIPT_DIR/disable-steelcase.sh" "$DEVICE:$REMOTE_DIR/disable-steelcase.sh"
ssh "$DEVICE" "chmod +x $REMOTE_DIR/disable-steelcase.sh"

info "Running disable-steelcase.sh..."
ssh "$DEVICE" "$REMOTE_DIR/disable-steelcase.sh"
ok "Steelcase bloatware disabled"

# ── 2. Install generic init script ─────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 2. Install App Launcher Service"
echo "════════════════════════════════════════"

info "Deploying roomwizard-app-init.sh → $INIT_SCRIPT"
scp "$SCRIPT_DIR/roomwizard-app-init.sh" "$DEVICE:$INIT_SCRIPT"

info "Registering service..."
ssh "$DEVICE" bash <<'REMOTE'
chmod +x /etc/init.d/roomwizard-app

# Clean up old roomwizard-games service if it exists
rm -f /etc/rc5.d/S*roomwizard-games 2>/dev/null || true
rm -f /etc/rc2.d/S*roomwizard-games 2>/dev/null || true
rm -f /etc/rc3.d/S*roomwizard-games 2>/dev/null || true
rm -f /etc/rc4.d/S*roomwizard-games 2>/dev/null || true
update-rc.d -f roomwizard-games remove 2>/dev/null || true

# Remove old roomwizard-app symlinks (might have wrong priority)
rm -f /etc/rc5.d/S*roomwizard-app 2>/dev/null || true
rm -f /etc/rc2.d/S*roomwizard-app 2>/dev/null || true
rm -f /etc/rc3.d/S*roomwizard-app 2>/dev/null || true
rm -f /etc/rc4.d/S*roomwizard-app 2>/dev/null || true
update-rc.d -f roomwizard-app remove 2>/dev/null || true

# Install with high priority (99) — starts AFTER all other services.
# This ensures browser/x11/webserver have started before we stop them.
ln -sf /etc/init.d/roomwizard-app /etc/rc5.d/S99roomwizard-app
ln -sf /etc/init.d/roomwizard-app /etc/rc2.d/S99roomwizard-app
ln -sf /etc/init.d/roomwizard-app /etc/rc3.d/S99roomwizard-app
ln -sf /etc/init.d/roomwizard-app /etc/rc4.d/S99roomwizard-app
REMOTE
ok "Service installed (S99roomwizard-app)"

# ── 3. Deploy audio-enable boot script ─────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 3. Deploy Boot Scripts"
echo "════════════════════════════════════════"

# The two init scripts are FILES in device-files/, not heredocs, so that
# commission-offline.sh writes the same bytes onto a card. A second copy of
# /etc/init.d/audio-enable would be a second thing to keep in step, and the one
# that drifts is found on a device that boots silent.
for _f in audio-enable time-sync; do
    info "Deploying $_f boot script..."
    [[ -f "$DEVICE_FILES/$_f" ]] || err "missing $DEVICE_FILES/$_f"
    scp -q "$DEVICE_FILES/$_f" "$DEVICE:/etc/init.d/$_f"
done
# S28 before S29 before S30avahi-daemon, and all three after S20cron; the numbers
# are the keep-list in device-files/clean-rules.conf, so changing one here means
# changing it there too or the whitelist sweep removes the link.
ssh "$DEVICE" "chmod +x /etc/init.d/audio-enable /etc/init.d/time-sync && ln -sf /etc/init.d/audio-enable /etc/rc5.d/S29audio-enable && ln -sf /etc/init.d/time-sync /etc/rc5.d/S28time-sync"
ok "Audio and time-sync boot scripts deployed"
ok "Time sync boot script deployed"

info "Enabling mDNS (avahi-daemon)..."
# The vendor image already carries /usr/sbin/avahi-daemon and a full
# /etc/init.d/avahi-daemon, but ships NO rc5.d link, so it never starts. Adding
# the link is the whole change — there is no daemon to write and no package to
# install. S30 puts it after S29audio-enable and after networking is up.
#
# Its Required-Start is "$remote_fs dbus", and dbus is one of the few dynamic
# consumers the deep clean deliberately keeps, so the dependency is satisfied
# on a fully cleaned device too.
#
# Payoff: `ssh root@<name>.local` and `./setup-device.sh <name>.local` instead of
# hunting DHCP leases. This is only useful once the unit has a UNIQUE name —
# every unit cloned from the vendor image claims RW09, and avahi would resolve
# the conflict by renaming to RW09-2.local etc. Hence --hostname above.
ssh "$DEVICE" bash <<'AVAHI_REMOTE'
if [ -x /etc/init.d/avahi-daemon ] && [ -x /usr/sbin/avahi-daemon ]; then
    ln -sf /etc/init.d/avahi-daemon /etc/rc5.d/S30avahi-daemon
    echo "  linked S30avahi-daemon"
else
    echo "  avahi-daemon not present on this image — skipped"
fi
AVAHI_REMOTE
ok "mDNS enabled (resolves <hostname>.local after reboot)"

# ── 4. SSH Hardening ──────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 4. SSH Hardening"
echo "════════════════════════════════════════"

# NOTE: SSH keys must already be deployed (Phase 1 / commission-roomwizard.sh).
# We do NOT disable PasswordAuthentication here — do that manually AFTER
# confirming key-based login works:
#   sed -i 's/^#PasswordAuthentication yes/PasswordAuthentication no/' /etc/ssh/sshd_config
#
# PermitRootLogin stays "yes" because:
#   - There is no other user account on the device (only root in /etc/passwd)
#   - All deployment (scp, ssh) requires root@<ip>
#   - The device has no adduser/useradd to create non-root accounts
#   - Security is achieved via key-based auth + PermitEmptyPasswords=no
info "Hardening SSH configuration..."
ssh "$DEVICE" bash <<'SSH_HARDEN'
    # Backup original if not already backed up
    [ ! -f /etc/ssh/sshd_config.orig ] && cp /etc/ssh/sshd_config /etc/ssh/sshd_config.orig

    # CRITICAL FIX: Disable empty password login (factory default allows it!)
    sed -i 's/^PermitEmptyPasswords yes/PermitEmptyPasswords no/' /etc/ssh/sshd_config

    # PermitRootLogin stays "yes" — root is the only account on the device.
    # Do NOT change to "prohibit-password" as it would lock us out if keys break.

    # Add brute-force / session limits if not present
    grep -q '^MaxAuthTries' /etc/ssh/sshd_config || echo 'MaxAuthTries 3' >> /etc/ssh/sshd_config
    grep -q '^LoginGraceTime' /etc/ssh/sshd_config || echo 'LoginGraceTime 30' >> /etc/ssh/sshd_config
    grep -q '^MaxSessions' /etc/ssh/sshd_config || echo 'MaxSessions 5' >> /etc/ssh/sshd_config

    # NOTE: Don't restart sshd here — it would break subsequent SSH commands in this script.
    # Changes take effect on next reboot (script requests reboot at the end).
SSH_HARDEN
ok "SSH hardened (PermitEmptyPasswords=no, MaxAuthTries=3) — takes effect on reboot"

# ── 5. Kernel/sysctl hardening ───────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 5. Kernel Security Settings"
echo "════════════════════════════════════════"

# NOTE on firewall: The RoomWizard firmware has no iptables binary, no
# ip_tables.ko kernel module in /lib/modules/4.14.52, no busybox iptables
# applet, no TCP wrappers (libwrap), and no package manager to install any
# of these. The kernel has CONFIG_NETFILTER=y but the ip_tables module was
# never compiled. Network security relies on:
#   - Disabling all unnecessary services (FTP, SNMP, HTTP, Java)
#   - SSH hardening (empty passwords blocked, brute-force limits)
#   - sysctl network hardening (below)
#   - Home network — device is not internet-facing
info "Applying kernel security settings..."
# 99-security.conf is a FILE in device-files/, same reasoning as the two init
# scripts: the offline installer writes the same bytes.
[[ -f "$DEVICE_FILES/99-security.conf" ]] || err "missing $DEVICE_FILES/99-security.conf"
scp -q "$DEVICE_FILES/99-security.conf" "$DEVICE:/etc/sysctl.d/99-security.conf"
ssh "$DEVICE" sh -s <<'SYSCTL'
# Apply now. The fallback exists because a kernel without sysctl.d support would
# otherwise accept the file and apply none of it, silently.
sysctl -p /etc/sysctl.d/99-security.conf 2>/dev/null || {
    sysctl -w kernel.randomize_va_space=2 2>/dev/null
    sysctl -w kernel.dmesg_restrict=1 2>/dev/null
    sysctl -w kernel.sysrq=0 2>/dev/null
}
SYSCTL
ok "Kernel security settings applied"

# ── 6. Analyze + optionally remove bloatware files ─────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " 6. Bloatware Files"
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

if [[ "$FLAG" == "--remove" ]]; then
    echo ""
    warn "File removal mode — this will delete bloatware files (PERMANENT)"
    read -p "Continue? (yes/no): " confirm
    
    if [[ "$confirm" == "yes" ]]; then
        info "Removing bloatware files..."
        ssh "$DEVICE" bash <<'REMOTE'
cat > /home/root/bloatware-removed.txt <<EOF
Bloatware removed on $(date)
- /opt/jetty-9-4-11 (43 MB)
- /opt/openjre-8 (93 MB)
- /opt/hsqldb (3.5 MB)
- /usr/share/cjkfont (31 MB)
- /usr/share/X11 (5.2 MB)
- /usr/share/snmp (2.5 MB)
Total freed: ~178 MB
EOF
rm -rf /opt/jetty-9-4-11 /opt/jetty /opt/openjre-8 /opt/java /opt/hsqldb
rm -rf /usr/share/cjkfont /usr/share/X11 /usr/share/snmp

# Additional Steelcase artifacts and data
# /opt/pv02 (44 KB) is KEPT from 2026-08-05: it is one of rw_is_rootfs's identity
# markers and a hardware reference, and device-files/clean-rules.conf keeps it
# with that reason. An `rm -rf /opt/pv02` here would have contradicted the data
# file the deep clean below reads — two lists in one script is exactly the drift
# that file exists to prevent.
# NOTE: /opt/sound (113 KB) is deliberately KEPT - it holds three usable UI WAVs
# (asl_click.wav, asl_error.wav, asl_success.wav) that are directly useful as
# launcher/game feedback sounds via common/audio.c. Cheap to keep.
rm -f /home/root/sqltool.rc 2>/dev/null
rm -rf /home/root/data/websign 2>/dev/null

# Clean shell profile references to deleted Steelcase configs
# (wsplatform.conf sourced in /etc/profile causes login errors)
sed -i '/wsplatform\.conf/d' /etc/profile 2>/dev/null
rm -rf /home/root/data/rwdb 2>/dev/null
rm -rf /home/root/data/conctest 2>/dev/null
# BUGFIX 2026-07-29: this used to be `rm -rf /home/root/data/cron`, which is
# DESTRUCTIVE - /var/cron/tabs/root is a SYMLINK into that directory, so deleting
# it destroys the root crontab and cron's spool root. Truncate the 79 MB log
# instead (it has been accumulating since 2017). --deep-clean truncates the
# crontab itself as well, and keeps cron running.
[ -f /home/root/data/cron/log ] && : > /home/root/data/cron/log
rm -rf /home/root/backup/websigns 2>/dev/null
rm -f /var/crontab.steelcase.bak 2>/dev/null

# Steelcase service configs
rm -f /etc/vsftpd.conf 2>/dev/null
rm -rf /etc/nullmailer 2>/dev/null
rm -rf /etc/snmp 2>/dev/null
rm -rf /var/lib/net-snmp 2>/dev/null
rm -rf /var/lib/nullmailer 2>/dev/null
rm -rf /var/lib/ntp 2>/dev/null

# Stale logs from Steelcase services
rm -f /var/log/browser.err 2>/dev/null
rm -f /var/log/jettystart 2>/dev/null
rm -rf /var/log/jetty_logs 2>/dev/null
rm -f /var/log/hsqldbstart 2>/dev/null
rm -f /var/log/snmp_daemon.log 2>/dev/null
rm -f /var/log/networkmngr.err 2>/dev/null
rm -f /home/root/log/Xorg.0.log 2>/dev/null
rm -f /home/root/log/get_time_from_server.err 2>/dev/null
rm -f /home/root/log/browser.err 2>/dev/null
rm -f /home/root/log/jettystart 2>/dev/null
rm -f /home/root/log/concurrent.log 2>/dev/null
rm -f /home/root/log/snmp_daemon.log 2>/dev/null
rm -f /home/root/log/networkmngr.err 2>/dev/null
rm -rf /home/root/log/jetty_logs 2>/dev/null

# NOTE: RoomWizard-zbgatewayd and wpantools_roomwizard are the 802.15.4 / ZigBee
# radio tooling. They are KEPT from 2026-07-29 onwards as the protocol reference
# for device-to-device wireless. The J5/J6 XBee socket is real and empty on every
# unit we have (see SYSTEM_ANALYSIS.md section 3.12), so this tooling is what a
# fitted module would be driven with. ~1.3 MB total. Copies also exist in the
# host-side partitions/ dump, so removing them is recoverable - but keeping them
# costs almost nothing. --deep-clean removes them.

# Dangerous init scripts - remove entirely (already disabled)
for svc in vsftpd snmpd nullmailer ntpd webserver jetty browser startautoupgrade webmonitor x11 hsqldb mta.sh; do
    rm -f "/etc/init.d/$svc" 2>/dev/null
done
REMOTE
        ok "Bloatware files removed (extended cleanup)"
        warn "Backup list saved to /home/root/bloatware-removed.txt"
    else
        info "File removal cancelled"
    fi
else
    info "Bloatware files left in place (use --remove to delete)"
fi

# ── deep clean (function defined above) ─────────────────────────────────────
if [[ "$DEEP_CLEAN" == "1" ]]; then
    run_deep_clean
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
