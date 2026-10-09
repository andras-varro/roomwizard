#!/bin/bash
#
# commissioning/provision.sh — UPDATE a unit that runs our own root filesystem, over SSH
#
# Re-runnable. It installs the files and links device-files/provision-rules.conf
# declares (boot scripts, the app launcher's init service, audio + time-sync, mDNS,
# USB host mode, Bluetooth, sshd hardening, sysctl), applies the sysctl settings,
# and reboots. It deletes no software: the unit's root is ours, not the vendor's.
#
# Usage:
#   ./commissioning/provision.sh <target>                 # backup, update, reboot
#   ./commissioning/provision.sh <target> --dry-run       # print the provision plan; change nothing
#   ./commissioning/provision.sh <target> --status        # show device status only (read-only)
#   ./commissioning/provision.sh <target> --hostname rw09 # set the host name only, no reboot
#   ./commissioning/provision.sh <target> --sshd-only --ssh-auth=key  # SSH config only, no reboot
#
# <target> is an IPv4 address or a host name — `rw09.local` works once mDNS is
# enabled (this script does that) and the unit has a unique name (--hostname).
#
# Prerequisites:
#   - A unit running our root image (rootfs/), reachable by SSH as root
#
# What it does:
#   0. Backs the unit's per-unit state up to this host (commissioning/backup.sh)
#      BEFORE the first write; a failed backup aborts the run.
#   1. Installs the provision plan: device-files/roomwizard-app as
#      /etc/init.d/roomwizard-app, the audio-enable + time-sync boot scripts, avahi
#      (mDNS), the USB host-mode scripts and their boot links (--no-usb skips), the
#      Bluetooth boot script and dbus policy (--no-bluetooth skips)
#   2. Hardens SSH (PermitEmptyPasswords=no, brute-force limits, no SHA-1) and
#      sets the auth mode: --ssh-auth=password (default) or --ssh-auth=key
#   3. Applies kernel/sysctl security settings (ASLR, no ip_forward, etc.)
#   4. Reboots the device
#
# NOTE: No iptables firewall — the kernel has CONFIG_NETFILTER=y but no ip_tables
# module was built, and there is no package manager to add one.
# Network security relies on: SSH hardening + sysctl + disabling services.
#
# After this, deploy a project and set it as the default app:
#   cd native_apps && ./build-and-deploy.sh <ip> set-default
#   cd vnc_client   && ./build-and-deploy.sh <ip> set-default

# Source-path directive: resolves the source= hints below against this script's directory.
# shellcheck source-path=SCRIPTDIR
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# What is INSTALLED is DATA in one file, device-files/provision-rules.conf, shared
# with commissioning/commission-offline.sh so that the live and offline passes cannot
# drift. lib/rw-provision.sh is the parser and the plan compiler; the executors
# differ ("/" is the correct prefix on a device and a refused one offline, and on
# this path the work happens on the far side of an ssh pipe), but there is one
# implementation of each, and rw_provision_online_script is the one this script
# ships to the device.
# shellcheck source=../lib/rw-identify.sh
. "$REPO_ROOT/lib/rw-identify.sh"
# rw-clean.sh is sourced ONLY for rw_provision_check_keeps, which still reads
# clean-rules.conf; both go when that check does.
# shellcheck source=../lib/rw-clean.sh
. "$REPO_ROOT/lib/rw-clean.sh"
# shellcheck source=../lib/rw-provision.sh
. "$REPO_ROOT/lib/rw-provision.sh"
# shellcheck source=../lib/rw-ssh.sh
. "$REPO_ROOT/lib/rw-ssh.sh"
# shellcheck source=../lib/rw-sshd.sh
. "$REPO_ROOT/lib/rw-sshd.sh"

# ── the non-positional flags are extracted before positional parsing ────────
#
# --no-<g> switches off part of the PROVISION, named after the groups in
# provision-rules.conf so the list is not repeated here. They are pulled out of "$@"
# first because everything below is positional ($1 target, $2 flag) and a --no-usb
# sitting in $3 would otherwise be rejected as an unknown option.
NO_PROV_GROUPS=""
DRY_RUN=""
SSH_AUTH="$RW_PROVISION_SSH_AUTH_DEFAULT"
_ARGS=()
for _a in "$@"; do
    case "$_a" in
        --dry-run)      DRY_RUN="--dry-run" ;;
        --ssh-auth=*)
            SSH_AUTH="${_a#--ssh-auth=}"
            rw_provision_ssh_auth_group "$SSH_AUTH" >/dev/null || exit 1 ;;
        --no-*)
            _g="${_a#--no-}"
            case " $(rw_provision_optional_groups) " in
                *" $_g "*) NO_PROV_GROUPS="$NO_PROV_GROUPS $_g" ;;
                *) echo "Unknown provision group: $_g"
                   echo "  --no- accepts: $(rw_provision_optional_groups)"
                   exit 1 ;;
            esac
            ;;
        *) _ARGS+=("$_a") ;;
    esac
done
set -- "${_ARGS[@]}"

# --no-sshd leaves sshd_config alone entirely, so an auth mode beside it is a
# contradiction rather than something to pick a winner for.
case " $NO_PROV_GROUPS " in
    *" sshd "*)
        if [[ "$SSH_AUTH" != "$RW_PROVISION_SSH_AUTH_DEFAULT" ]]; then
            echo "--no-sshd and --ssh-auth=$SSH_AUTH contradict each other — --no-sshd leaves sshd_config alone."
            exit 1
        fi ;;
esac

DEVICE_IP="${1:-}"
FLAG="${2:-}"
DEVICE="root@${DEVICE_IP}"
INIT_SCRIPT="/etc/init.d/roomwizard-app"
# Files installed onto the device verbatim. They live in device-files/ rather
# than in a heredoc here so that the offline installer writes the same bytes —
# two copies of an init script is two things to keep in step, and the one that
# drifts is discovered on a device that boots to a black screen.
DEVICE_FILES="$REPO_ROOT/device-files"
# Read ONLY by rw_provision_check_keeps below; goes with that check.
CLEAN_RULES="$DEVICE_FILES/clean-rules.conf"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }

# ── usage ───────────────────────────────────────────────────────────────────
usage() {
    echo "Usage: $0 <target> [--status] [--dry-run] [--no-<group>] [--ssh-auth=password|key]"
    echo "       $0 <target> --sshd-only [--ssh-auth=password|key] [--dry-run]"
    echo "       $0 <target> --hostname NAME"
    echo ""
    echo "  With no flags this UPDATES a unit that runs our own root: it backs the"
    echo "  unit's per-unit state up to this host (commissioning/backup.sh), installs"
    echo "  the provision plan, applies sysctl and reboots. It deletes no software."
    echo "  p1 is never written."
    echo ""
    echo "  <target>          Device IPv4 address, or a host name (e.g. rw09.local)"
    echo "  --status          Show device status only (no changes, no backup, no reboot)"
    echo "  --dry-run         Print the provision plan this run would apply; change"
    echo "                    nothing, take no backup, do not reboot."
    echo "  --no-<group>      Skip one group of the provision plan. Groups:"
    echo "                    $(rw_provision_optional_groups)"
    echo "                    --no-mdns leaves <name>.local unresolvable; --no-sshd"
    echo "                    skips the sshd hardening; --no-usb installs no USB host"
    echo "                    mode; --no-bluetooth installs no BT boot script or dbus"
    echo "                    policy."
    echo "  --ssh-auth=MODE   password (default): root may log in by password or key."
    echo "                    key: key only — PasswordAuthentication and keyboard-"
    echo "                    interactive off, PermitRootLogin prohibit-password."
    echo "                    REFUSED unless a BatchMode key login works first, then"
    echo "                    sshd -t, a reload and a fresh key login must pass or the"
    echo "                    previous sshd_config comes back by itself. Either mode"
    echo "                    also drops SHA-1 MACs and ssh-rsa signatures."
    echo "  --sshd-only       Apply only the sshd part (with --ssh-auth), and exit."
    echo "                    No backup, no reboot."
    echo "  --hostname NAME   Set the device host name only, and exit. No backup, no"
    echo "                    reboot."
    echo "                    NAME is a single label — 'rw09', not 'rw09.local'."
    exit 1
}

[[ -z "$DEVICE_IP" ]] && usage

# Validate the target before doing anything: every step past here is destructive
# and ends in a reboot.
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

# The init script is the file this script copies whose staleness is silent, and it
# reaches a device ONLY through here — deploy-all.sh does not push it. So a device
# can run an older copy than the repo's with nothing saying so; md5 settles it in
# command.
#
# Byte comparison is valid because .gitattributes pins *.sh to eol=lf, so the
# working tree is LF even on this Windows host and scp copies it unchanged.
report_script_versions() {
    local pairs=(
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
    ""|--status|--hostname|--sshd-only) ;;
    *) echo "Unknown option: $FLAG"; echo ""; usage ;;
esac
if [[ "$FLAG" == "--sshd-only" ]] && [[ " $NO_PROV_GROUPS " == *" sshd "* ]]; then
    echo "--sshd-only with --no-sshd would do nothing."; echo ""; usage
fi

# --dry-run previews the provision plan, and these two modes install nothing — so the
# combination is a mistake rather than a no-op, and is refused out loud.
if [[ -n "$DRY_RUN" && ( "$FLAG" == "--status" || "$FLAG" == "--hostname" ) ]]; then
    echo "--dry-run does not apply to $FLAG — neither writes anything."
    echo ""
    usage
fi

# NOTE: a stray positional is rejected per-mode below, where the number of
# expected ones is known — --hostname takes a NAME, nothing else takes anything.

# --hostname is the one flag that takes a value, so it consumes $3. Every other
# mode is exhausted by $1 and $2 — --dry-run and the
# --no-<group> options were lifted out of "$@" above, so a leftover positional
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


# ── SSH check ───────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " RoomWizard System Setup"
echo "════════════════════════════════════════"

info "Testing SSH connection to $DEVICE_IP..."
# One gate, in lib/rw-ssh.sh: it tells "down" from "up and refusing us" and, on a
# terminal, offers to generate a key and ssh-copy-id it. This is FIRST contact for
# anyone who did not prep the card, so a bare `check IP and SSH key` — advice about
# a key nothing offered to make — is not enough here.
rw_ssh_gate "$DEVICE" || err "Cannot continue without SSH to $DEVICE"
ok "SSH OK"

# ── key-only is refused without a PROVEN key, before anything is written ─────
#
# rw_ssh_gate passing already implies a BatchMode login, but asked again here with
# publickey as the ONLY method, so that what is proven is exactly what key-only
# will leave. Before the consent question and before the first write: a refusal
# here leaves the unit untouched.
if [[ "$SSH_AUTH" == key && "$FLAG" != "--status" && "$FLAG" != "--hostname" ]]; then
    if ! rw_sshd_key_login_ok "$DEVICE"; then
        echo "${RW_SSH_LAST_STDERR:-}" >&2
        err "--ssh-auth=key refused: a publickey-only login to $DEVICE failed, so key-only would lock you out. Install a key first (ssh-copy-id $DEVICE) and re-run."
    fi
    ok "A publickey-only login works, so key-only is safe to apply"
fi

# apply_plan_guarded PLANFILE WITH_SSHD
#
# Ship PLANFILE and run it through the online executor. WITH_SSHD=1 wraps it in
# lib/rw-sshd.sh's guard: snapshot sshd_config first, restore it if the plan
# fails, and after it succeeds run sshd -t, reload under the dead-man timer and
# re-prove the key login on a fresh connection (rw_sshd_commit_ssh). The full run
# and --sshd-only both come through here, so there is one sequence.
apply_plan_guarded() {
    local plan="$1" with_sshd="$2"
    if [[ "$with_sshd" == 1 ]]; then
        rw_sshd_guard_script | ssh "$DEVICE" "cat > /tmp/rw-sshd-guard.sh" \
            || err "could not copy the sshd guard to the device"
        ssh "$DEVICE" "sh /tmp/rw-sshd-guard.sh snapshot" \
            || err "could not snapshot sshd_config — refusing to edit it without a way back"
    fi
    ssh "$DEVICE" "cat > /tmp/rw-provision-plan" < "$plan"
    rw_provision_online_script | ssh "$DEVICE" "cat > /tmp/rw-provision.sh"
    if ! ssh "$DEVICE" "sh /tmp/rw-provision.sh /tmp/rw-provision-plan; rc=\$?; rm -f /tmp/rw-provision.sh /tmp/rw-provision-plan; exit \$rc"; then
        if [[ "$with_sshd" == 1 ]]; then ssh "$DEVICE" "sh /tmp/rw-sshd-guard.sh restore" || true; fi
        err "the provision step failed on the device"
    fi
    if [[ "$with_sshd" == 1 ]]; then
        info "Checking the new sshd_config (sshd -t, reload, fresh key login)..."
        rw_sshd_commit_ssh "$DEVICE" "$SSH_AUTH" \
            || err "the new sshd_config did not pass — the previous one is (or is about to be) back"
        ok "sshd: ${SSH_AUTH} mode, checked and live"
    fi
}

# ── sshd-only mode ──────────────────────────────────────────────────────────
# The sshd records alone — crypto, limits, and the --ssh-auth mode — on a unit that
# is already commissioned. No backup (nothing here is irreversible: the guard keeps
# the previous file), and no reboot: the reload in
# rw_sshd_commit_ssh is what makes it live.
if [[ "$FLAG" == "--sshd-only" ]]; then
    SSHD_PLAN=$(mktemp)
    rw_provision_plan_sshd "$DEVICE_FILES/provision-rules.conf" "$SSH_AUTH" > "$SSHD_PLAN" \
        || { rm -f "$SSHD_PLAN"; err "could not compile the sshd plan"; }
    if [[ "$DRY_RUN" == "--dry-run" ]]; then
        info "Dry run — the sshd records that --ssh-auth=$SSH_AUTH would apply:"
        sed 's/^/    /' "$SSHD_PLAN"
        rm -f "$SSHD_PLAN"
        exit 0
    fi
    info "Applying the sshd records (--ssh-auth=$SSH_AUTH)..."
    apply_plan_guarded "$SSHD_PLAN" 1
    rm -f "$SSHD_PLAN"
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
echo "HW watchdog:      $([ -c /dev/watchdog ] && echo 'active (fed by /usr/sbin/watchdog)' || echo 'n/a')"
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
# by commissioning/commission-offline.sh, so the two cannot drift.
#
# What used to be here: five scp calls, an `ssh <<'REMOTE'` block of ln -sf, a second
# one for avahi, a four-command sed block over sshd_config, and a third for the
# sysctl file — every one of them written out a second time in commissioning/commission-offline.sh.
# They HAD drifted: this path deleted stale rc*.d links before relinking and the
# offline path did not.
#
# The plan is compiled HERE, where the parser lives, and shipped as data the device
# only interprets. The interpreter itself comes
# from rw_provision_online_script, so there is one implementation of each verb and
# `--dry-run` on either path prints the same resolved set.
PROV_RULES="$DEVICE_FILES/provision-rules.conf"
[[ -f "$PROV_RULES" ]] || err "missing $PROV_RULES"
if ! PCHECK="$(rw_provision_validate "$PROV_RULES" "$REPO_ROOT")"; then
    echo "$PCHECK"
    err "device-files/provision-rules.conf does not validate — refusing to provision"
fi
# Still read from clean-rules.conf until that check is retired: every boot link the
# plan makes must be named there, or a deep clean of an older image would drop it.
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
WITH_SSHD=0
if [[ " $PROV_GROUPS " == *" sshd "* ]]; then
    PROV_GROUPS="$PROV_GROUPS $(rw_provision_ssh_auth_group "$SSH_AUTH")"
    WITH_SSHD=1
    info "SSH authentication: $SSH_AUTH"
fi

PROV_PLAN=$(mktemp)
rw_provision_plan "$PROV_RULES" "$PROV_GROUPS" > "$PROV_PLAN" \
    || { rm -f "$PROV_PLAN"; err "could not compile the provision plan"; }
info "Provision plan: $(rw_provision_plan_summary "$PROV_PLAN")"

# ── dry-run: print the plan and stop, before anything is written ────────────
#
# The resolved records this run WOULD apply (install / link / unlink / ...), one per
# line, from the same compiler the real run uses. No backup is taken, nothing is
# copied, the device is not rebooted: the only remote call so far is the SSH probe.
if [[ "$DRY_RUN" == "--dry-run" ]]; then
    echo ""
    sed 's/^/    /' "$PROV_PLAN"
    rm -f "$PROV_PLAN"
    echo ""
    info "Dry run only — nothing backed up, nothing written, device not rebooted."
    exit 0
fi

# ── 0. Back the unit up, before the FIRST write ─────────────────────────────
#
# The unit's per-unit state (settings, calibration, pairings, high scores, host
# keys, authorized_keys) goes to backups/ on this host. A failed backup aborts the
# run: an update that cannot be undone from the archive does not start.
echo ""
echo "════════════════════════════════════════"
echo " 0. Backup"
echo "════════════════════════════════════════"
if ! bash "$SCRIPT_DIR/backup.sh" "$DEVICE_IP"; then
    rm -f "$PROV_PLAN"
    err "backup of $DEVICE failed — nothing was written to the device"
fi
ok "Backup taken"

# The install verb's SOURCE bytes are on this host, so they go over scp first and
# the remote interpreter only sets the declared mode. That asymmetry is the whole
# reason the two executors exist; everything else about them is shared.
#
# ⚠️ The loop lives in lib/rw-provision.sh, not here. It was written out in full in
# this script and again in usb_host/build-and-deploy.sh, both reading the plan on
# stdin with an `ssh` in the body — so the first ssh ate the rest of the plan and
# both installed exactly one file. Do not inline it again.
rw_provision_push_installs "$PROV_PLAN" "$REPO_ROOT" "$DEVICE" \
    || { rm -f "$PROV_PLAN"; err "could not copy the provision sources to the device"; }

apply_plan_guarded "$PROV_PLAN" "$WITH_SSHD"
rm -f "$PROV_PLAN"
ok "Boot scripts, boot links, sshd, sysctl and the config fix-ups done"

# ── 2. Apply the sysctl settings now ───────────────────────────────────────
#
# NOTE on firewall: this image has no iptables binary, no ip_tables.ko in
# /lib/modules/4.14.52, no busybox iptables applet, no TCP wrappers and no package
# manager to add any of them. The kernel has CONFIG_NETFILTER=y and ip_tables was
# never compiled. Network security is: no unnecessary services, sshd hardened,
# sysctl hardening, and a home network the device is not exposed through.
#
# The FILE was installed by the plan as /etc/sysctl.conf, which rcS.d/S30procps.sh
# applies at every boot; applying it to the running kernel now is again an action.
# No 2>/dev/null: a key this kernel lacks is an error on the boot console too.
info "Applying kernel security settings..."
ssh "$DEVICE" "sysctl -q -p /etc/sysctl.conf"
ok "Kernel security settings applied"

# ── Status summary ──────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " Status Summary"
echo "════════════════════════════════════════"

ssh "$DEVICE" bash <<'REMOTE'
echo ""
echo "Disk:    $(df -h / | tail -1 | awk '{print $3 " used, " $4 " free (" $5 " used)"}')"
echo "Memory:  $(free -h | grep Mem | awk '{print $3 " used, " $7 " available"}')"
echo "HW watchdog:      $([ -c /dev/watchdog ] && echo 'active (fed by /usr/sbin/watchdog)' || echo 'n/a')"
echo "Default app:      $(cat /opt/roomwizard/default-app 2>/dev/null || echo '(not set)')"
echo ""
echo "Active cron jobs:"
crontab -l 2>/dev/null | grep -v '^#' | grep -v '^$' | sed 's/^/  /'
REMOTE

# Confirm the init script this run just pushed is byte-identical on the device.
# scp reporting success is not the same as the right bytes landing, and a stale
# init script is silent.
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
