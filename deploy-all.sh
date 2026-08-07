#!/bin/bash
#
# deploy-all.sh — Build and deploy all components to a RoomWizard device
#
# Scans the workspace for build-and-deploy.sh scripts and runs them in
# order.  After all components are deployed, sets the app launcher as the
# default boot app.
#
# Usage:
#   ./deploy-all.sh <ip>                     # build + deploy everything
#   ./deploy-all.sh <ip> <component>         # build + deploy one component
#   ./deploy-all.sh --list                   # show discovered components
#   ./deploy-all.sh --from-bundle <b> <ip>   # install a bundle; build NOTHING
#
# Components are detected automatically from subdirectories containing a
# build-and-deploy.sh script.  They are deployed in a deterministic order:
# native_apps first (provides the launcher), then the rest alphabetically.
#
# ── --from-bundle: the delivery mode, and why it is here ────────────────────
#
# Every other mode BUILDS, so every other mode needs arm-linux-gnueabihf-gcc.
# Someone being handed a device has no toolchain — that is what a release bundle is
# for — so before this existed, the offline card pass was the only way to put
# binaries on a unit even for someone who already had SSH to it
# (IMPROVEMENT_PLAN.md C12, and F9's `--from-release`).
#
# It is the SSH twin of commissioning/commission-offline.sh's install loop and shares its
# authority: rw_bundle_install_ssh in lib/rw-bundle.sh, one implementation, modes
# DECLARED by the manifest rather than carried by the transfer.
#
# Prerequisites:
#   - Device set up with commissioning/provision.sh (one-time)
#   - ARM cross-compiler installed          — except for --from-bundle
#   - WSL (for ScummVM builds)              — except for --from-bundle

set -e
_START_SECONDS=$(date +%s)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# shellcheck source=lib/rw-bundle.sh
. "$SCRIPT_DIR/lib/rw-bundle.sh"

DEVICE_IP="${1:-}"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; NC='\033[0m'
ok()   { echo -e "[$(date '+%H:%M:%S')] ${GREEN}  ✓ $*${NC}"; }
info() { echo -e "[$(date '+%H:%M:%S')] ${YELLOW}  → $*${NC}"; }
warn() { echo -e "[$(date '+%H:%M:%S')] ${BLUE}  ! $*${NC}"; }
err()  { echo -e "[$(date '+%H:%M:%S')] ${RED}  ✗ $*${NC}"; exit 1; }

# ── discover components ─────────────────────────────────────────────────────
# Returns an ordered list: native_apps first, then the rest alphabetically.
discover_components() {
    local components=()
    local first=""

    for script in "$SCRIPT_DIR"/*/build-and-deploy.sh; do
        [ -f "$script" ] || continue
        local dir
        dir="$(basename "$(dirname "$script")")"

        # Skip directories that aren't real components
        # (e.g. build/, partitions/, browser_games/ have no deploy script)
        if [[ "$dir" == "native_apps" ]]; then
            first="$dir"
        else
            components+=("$dir")
        fi
    done

    # Sort the rest alphabetically
    IFS=$'\n' components=($(sort <<<"${components[*]}")); unset IFS

    # native_apps first (provides the launcher — other manifests depend on it)
    if [[ -n "$first" ]]; then
        echo "$first"
    fi
    for c in "${components[@]}"; do
        echo "$c"
    done
}

# ── list mode ───────────────────────────────────────────────────────────────
if [[ "$DEVICE_IP" == "--list" ]]; then
    echo ""
    echo "Discovered components:"
    echo ""
    idx=1
    while IFS= read -r comp; do
        local_script="$SCRIPT_DIR/$comp/build-and-deploy.sh"
        desc=$(grep -m1 '^# [A-Za-z]' "$local_script" | sed 's/^# *//')
        printf "  %d. %-22s  %s\n" "$idx" "$comp" "$desc"
        idx=$((idx + 1))
    done < <(discover_components)
    echo ""
    echo "Usage: $0 <ip> [component]"
    exit 0
fi

# ── --from-bundle mode: install a staged bundle, build nothing ─────────────
#
# Parsed before the IPv4 validation below because the IP is $2 here, not $1.
FROM_BUNDLE=""
if [[ "$DEVICE_IP" == "--from-bundle" ]]; then
    FROM_BUNDLE="${2:-}"
    DEVICE_IP="${3:-}"
    [[ -n "$FROM_BUNDLE" ]] || err "--from-bundle needs a tarball or staged directory"
    [[ -n "$DEVICE_IP" ]]   || err "--from-bundle <bundle> <ip> — the IP is missing"
fi

# ── usage ───────────────────────────────────────────────────────────────────
if [[ -z "$DEVICE_IP" ]]; then
    echo "Usage: $0 <ip> [component]"
    echo "       $0 --from-bundle <tar.gz|dir> <ip>"
    echo "       $0 --list"
    echo ""
    echo "Builds and deploys all components (or a single one) to the device."
    echo "Sets native_apps/app_launcher as the default boot app."
    echo ""
    echo "  --from-bundle   Install a release bundle over SSH and build NOTHING."
    echo "                  Needs no cross-compiler; this is the delivery mode."
    echo "                  Make one with:  ./release.sh --stage-only"
    echo ""
    echo "Examples:"
    echo "  $0 192.168.50.53              # deploy everything"
    echo "  $0 192.168.50.53 vnc_client   # deploy VNC client only"
    echo "  $0 --from-bundle build/release 192.168.50.53"
    echo "  $0 --list                     # show available components"
    exit 1
fi

FILTER="${2:-}"
[[ -n "$FROM_BUNDLE" ]] && FILTER=""

# ── validate the device IP before building anything ─────────────────────────
# `./deploy-all.sh vnc_client` (forgetting the IP) used to build *everything*,
# including the multi-minute ScummVM build, and only fail at `ssh root@vnc_client`
# minutes later.  The single most likely typo is a component name in $1, so name
# that case explicitly (../IMPROVEMENT_PLAN.md B19).
IPV4_RE='^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}$'
if [[ ! "$DEVICE_IP" =~ $IPV4_RE ]]; then
    echo ""
    if [[ -f "$SCRIPT_DIR/$DEVICE_IP/build-and-deploy.sh" ]]; then
        echo "  '$DEVICE_IP' is a component, not an IP — the IP comes first:"
        echo "      $0 <ip> $DEVICE_IP"
    else
        echo "  Not an IPv4 address: '$DEVICE_IP'"
    fi
    echo ""
    echo "Usage: $0 <ip> [component]"
    echo "       $0 --list"
    exit 1
fi

# ── main ────────────────────────────────────────────────────────────────────
echo ""
echo "╔═══════════════════════════════════════╗"
echo "║  RoomWizard Deploy All                ║"
echo "╚═══════════════════════════════════════╝"
info "Started — $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# ── --from-bundle: install and stop. No component loop, no compiler. ────────
if [[ -n "$FROM_BUNDLE" ]]; then
    DEVICE="root@${DEVICE_IP}"
    ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
        || err "Cannot reach $DEVICE — check the IP and the SSH key"
    ok "SSH OK"

    # A tarball is unpacked into a temp dir; a directory is used where it is.
    BUNDLE_TMP=""
    if [[ -d "$FROM_BUNDLE" ]]; then
        BUNDLE_DIR="$(cd "$FROM_BUNDLE" && pwd)"
    elif [[ -f "$FROM_BUNDLE" ]]; then
        BUNDLE_TMP=$(mktemp -d)
        # shellcheck disable=SC2064  # $BUNDLE_TMP is meant to expand now
        trap "rm -rf '$BUNDLE_TMP'" EXIT
        info "Unpacking $(basename "$FROM_BUNDLE")"
        tar -xzf "$FROM_BUNDLE" -C "$BUNDLE_TMP" || err "could not unpack $FROM_BUNDLE"
        # release.sh tars the staged directory, so root/ may be one level down.
        if [[ -d "$BUNDLE_TMP/root" ]]; then
            BUNDLE_DIR="$BUNDLE_TMP"
        else
            BUNDLE_DIR=$(find "$BUNDLE_TMP" -maxdepth 2 -type d -name root -print -quit)
            BUNDLE_DIR="${BUNDLE_DIR%/root}"
            [[ -n "$BUNDLE_DIR" ]] || err "no root/ directory inside $FROM_BUNDLE"
        fi
    else
        err "no such bundle: $FROM_BUNDLE"
    fi

    info "Components in the bundle: $(rw_bundle_components "$BUNDLE_DIR" | tr '\n' ' ')"

    # Stop whatever holds /dev/fb0 before overwriting the binary behind it. The init
    # script is the only implementation of "what is running" — it walks /proc/*/exe
    # rather than matching a name, because the process holding the framebuffer is
    # usually the grandchild app_launcher started and its basename is in no config.
    if ssh "$DEVICE" '[ -x /etc/init.d/roomwizard-app ]' 2>/dev/null; then
        info "Stopping the running app"
        ssh "$DEVICE" '/etc/init.d/roomwizard-app stop' 2>&1 | sed 's/^/    /' || true
    else
        warn "no /etc/init.d/roomwizard-app — run ./commissioning/provision.sh $DEVICE_IP first"
    fi

    echo ""
    if rw_bundle_install_ssh "$DEVICE" "$BUNDLE_DIR"; then
        ok "Bundle installed"
    else
        err "the bundle install failed — see the messages above"
    fi

    # default-app, then restart, exactly as the build path does below.
    echo ""
    info "Default → /opt/roomwizard/app_launcher"
    ssh "$DEVICE" "mkdir -p /opt/roomwizard && echo '/opt/roomwizard/app_launcher' > /opt/roomwizard/default-app"
    if ssh "$DEVICE" '[ -x /opt/roomwizard/app_launcher ]' 2>/dev/null; then
        ok "Default app set and present"
    else
        warn "default-app names /opt/roomwizard/app_launcher, which is NOT installed"
        warn "  the init script would respawn nothing — check the bundle's components"
    fi
    if ssh "$DEVICE" '[ -f /etc/init.d/roomwizard-app ]' 2>/dev/null; then
        info "Starting the launcher"
        ssh "$DEVICE" '/etc/init.d/roomwizard-app start' 2>&1 | grep -v '^$' | sed 's/^/    /'
        ok "Launcher running"
    fi

    echo ""
    ok "Installed to $DEVICE_IP from $(basename "$FROM_BUNDLE") — nothing was built"
    _END_SECONDS=$(date +%s)
    _ELAPSED=$((_END_SECONDS - _START_SECONDS))
    printf "[$(date '+%H:%M:%S')] Total time: %dm%02ds\n" $((_ELAPSED / 60)) $((_ELAPSED % 60))
    echo ""
    exit 0
fi

# Collect components
mapfile -t ALL_COMPONENTS < <(discover_components)

if [[ ${#ALL_COMPONENTS[@]} -eq 0 ]]; then
    err "No components found (no */build-and-deploy.sh scripts)"
fi

# Filter to a single component if requested
if [[ -n "$FILTER" ]]; then
    found=false
    for c in "${ALL_COMPONENTS[@]}"; do
        if [[ "$c" == "$FILTER" ]]; then
            found=true
            break
        fi
    done
    if ! $found; then
        err "Unknown component: $FILTER\n  Available: ${ALL_COMPONENTS[*]}"
    fi
    ALL_COMPONENTS=("$FILTER")
    info "Deploying single component: $FILTER"
else
    info "Deploying ${#ALL_COMPONENTS[@]} component(s): ${ALL_COMPONENTS[*]}"
fi
echo ""

# ── deploy each component ──────────────────────────────────────────────────
SUCCEEDED=()
FAILED=()

for comp in "${ALL_COMPONENTS[@]}"; do
    DEPLOY_SCRIPT="$SCRIPT_DIR/$comp/build-and-deploy.sh"

    echo "════════════════════════════════════════"
    info "Component: $comp"
    echo "════════════════════════════════════════"
    echo ""

    if [[ ! -x "$DEPLOY_SCRIPT" ]]; then
        chmod +x "$DEPLOY_SCRIPT"
    fi

    # Run the component's build-and-deploy with the device IP.
    # Each script handles its own build, upload, manifest, and restart.
    if (cd "$SCRIPT_DIR/$comp" && bash build-and-deploy.sh "$DEVICE_IP"); then
        SUCCEEDED+=("$comp")
        ok "$comp deployed successfully"
    else
        FAILED+=("$comp")
        echo -e "${RED}  ✗ $comp FAILED${NC}"
        warn "Continuing with remaining components..."
    fi
    echo ""
done

# ── set default app (native_apps/app_launcher) ─────────────────────────────
# Only set-default if native_apps was deployed (or we're deploying everything)
DEVICE="root@${DEVICE_IP}"

set_default=false
for c in "${SUCCEEDED[@]}"; do
    [[ "$c" == "native_apps" ]] && set_default=true
done

if $set_default; then
    echo "════════════════════════════════════════"
    echo "  Setting default app"
    echo "════════════════════════════════════════"
    echo ""

    info "Default → /opt/roomwizard/app_launcher"
    ssh "$DEVICE" "mkdir -p /opt/roomwizard && echo '/opt/roomwizard/app_launcher' > /opt/roomwizard/default-app"
    ok "Default app set"

    # Restart the init service so it picks up the new default
    if ssh "$DEVICE" '[ -f /etc/init.d/roomwizard-app ]' 2>/dev/null; then
        info "Restarting app launcher..."
        ssh "$DEVICE" '/etc/init.d/roomwizard-app start' 2>&1 | grep -v '^$'
        ok "Launcher running"
    fi
    echo ""
fi

# ── summary ─────────────────────────────────────────────────────────────────
echo "╔═══════════════════════════════════════╗"
echo "║  Deploy Summary                       ║"
echo "╚═══════════════════════════════════════╝"
echo ""

if [[ ${#SUCCEEDED[@]} -gt 0 ]]; then
    echo -e "  ${GREEN}Succeeded:${NC} ${SUCCEEDED[*]}"
fi
if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo -e "  ${RED}Failed:${NC}    ${FAILED[*]}"
fi
echo ""

if [[ ${#FAILED[@]} -gt 0 ]]; then
    warn "${#FAILED[@]} component(s) failed. Check output above for details."
    echo ""
    exit 1
fi

ok "All ${#SUCCEEDED[@]} component(s) deployed to $DEVICE_IP"
_END_SECONDS=$(date +%s)
_ELAPSED=$((_END_SECONDS - _START_SECONDS))
printf "[$(date '+%H:%M:%S')] Total time: %dm%02ds\n" $((_ELAPSED / 60)) $((_ELAPSED % 60))
echo ""
