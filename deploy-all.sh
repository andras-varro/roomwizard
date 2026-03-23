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
#
# Components are detected automatically from subdirectories containing a
# build-and-deploy.sh script.  They are deployed in a deterministic order:
# native_apps first (provides the launcher), then the rest alphabetically.
#
# Prerequisites:
#   - Device set up with setup-device.sh (one-time)
#   - ARM cross-compiler installed
#   - WSL (for ScummVM builds)

set -e
_START_SECONDS=$(date +%s)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

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

# ── usage ───────────────────────────────────────────────────────────────────
if [[ -z "$DEVICE_IP" ]]; then
    echo "Usage: $0 <ip> [component]"
    echo "       $0 --list"
    echo ""
    echo "Builds and deploys all components (or a single one) to the device."
    echo "Sets native_apps/app_launcher as the default boot app."
    echo ""
    echo "Examples:"
    echo "  $0 192.168.50.53              # deploy everything"
    echo "  $0 192.168.50.53 vnc_client   # deploy VNC client only"
    echo "  $0 --list                     # show available components"
    exit 1
fi

FILTER="${2:-}"

# ── main ────────────────────────────────────────────────────────────────────
echo ""
echo "╔═══════════════════════════════════════╗"
echo "║  RoomWizard Deploy All                ║"
echo "╚═══════════════════════════════════════╝"
info "Started — $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

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
