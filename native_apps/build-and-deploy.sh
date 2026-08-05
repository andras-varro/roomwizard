#!/bin/bash
# Build all native games and optionally deploy to a RoomWizard device.
#
# Usage:
#   ./build-and-deploy.sh                      # build only
#   ./build-and-deploy.sh <ip>                 # build + deploy binaries
#   ./build-and-deploy.sh <ip> set-default     # build + deploy + set as default app
#   ./build-and-deploy.sh --bundle <dir>       # build + stage into an offline bundle
#
# System setup (bloatware cleanup, init service, audio, time-sync) is handled
# separately by setup-device.sh.  Run that once before deploying for the first time.
#
# --bundle stages the same artifacts this script would scp, under
# <dir>/root/<device-path>, with a declared-mode manifest — the layout
# ../release.sh publishes and ../commission-offline.sh installs from
# (../IMPROVEMENT_PLAN.md F9, F10).  It always builds first: this component's
# 31 targets take well under a minute, so there is no reason for a
# stage-what-is-already-there mode and therefore no way to bundle a stale
# binary.  (ScummVM, whose link is ~2 minutes, does have one.)

set -e
_START_SECONDS=$(date +%s)

# Work from this script's own directory, so it can be invoked by path.  Every
# source, build and icon path below is relative (common/*.c, build/, tests/,
# ./check-arm-safe.sh); without this the first compile dies with
# "common/framebuffer.c: No such file or directory" and the script only ever
# worked because deploy-all.sh wraps it in a subshell cd
# (../IMPROVEMENT_PLAN.md B19).
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# The .app manifests are data in one file, written locally, and copied by BOTH
# the deploy path and --bundle.  They used to be nine heredocs inside an
# `ssh … <<REMOTE` block, so they existed only when a device was reachable and
# the offline installer had no way to produce the same bytes
# (../IMPROVEMENT_PLAN.md F10).
# shellcheck source=app-manifests.sh
. "$SCRIPT_DIR/app-manifests.sh"

# ── argument shapes ─────────────────────────────────────────────────────────
# Two, and they do not mix: `--bundle <dir>` needs no device, and every deploy
# form needs one.  Parsed before anything else so a typo cannot reach the build.
BUNDLE_DIR=""
if [[ "${1:-}" == "--bundle" ]]; then
    BUNDLE_DIR="${2:-}"
    DEVICE_IP=""
    MODE=""
    if [[ -z "$BUNDLE_DIR" ]]; then
        echo "--bundle requires a directory"
        echo ""
        echo "Usage: $0 --bundle <dir>"
        exit 1
    fi
    if [[ -n "${3:-}" ]]; then
        echo "Unexpected argument after --bundle <dir>: $3"
        exit 1
    fi
    # shellcheck source=../rw-bundle.sh
    . "$REPO_ROOT/rw-bundle.sh"
else
    DEVICE_IP="${1:-}"
    MODE="${2:-}"
fi
DEVICE="root@${DEVICE_IP}"
GAMES_DIR="/opt/games"

# ── colour helpers ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "[$(date '+%H:%M:%S')] ${GREEN}  ✓ $*${NC}"; }
info() { echo -e "[$(date '+%H:%M:%S')] ${YELLOW}  → $*${NC}"; }
warn() { echo -e "[$(date '+%H:%M:%S')] ${BLUE}  ! $*${NC}"; }
err()  { echo -e "[$(date '+%H:%M:%S')] ${RED}  ✗ $*${NC}"; exit 1; }

# ── argument validation ─────────────────────────────────────────────────────
# Validate BEFORE building.  A bad first argument used to surface only at the
# first ssh, i.e. after all 31 targets had already been compiled
# (../IMPROVEMENT_PLAN.md B19).
usage() {
    echo "Usage: $0 [<ip>] [set-default]"
    echo "       $0 --bundle <dir>"
    echo ""
    echo "  <ip>          Device IPv4 address; omit to build without deploying"
    echo "  set-default   Also make app_launcher the boot app"
    echo "  --bundle <dir>  Build, then stage every artifact under <dir>/root/"
    echo "                  with a declared-mode manifest. No device needed."
    echo ""
    echo "Examples:"
    echo "  $0                          # build only"
    echo "  $0 192.168.50.73            # build + deploy"
    echo "  $0 192.168.50.73 set-default"
    echo "  $0 --bundle /tmp/rw-bundle"
    exit 1
}

IPV4_RE='^(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}$'
if [[ -n "$DEVICE_IP" ]] && [[ ! "$DEVICE_IP" =~ $IPV4_RE ]]; then
    echo "Not an IPv4 address: $DEVICE_IP"
    echo ""
    usage
fi

# set-default is the only mode this script accepts; anything else was silently
# ignored, so a typo deployed without doing what you asked.
case "$MODE" in
    ""|set-default) ;;
    *) echo "Unknown mode: $MODE"; echo ""; usage ;;
esac

# ── 1. cross-compiler check ─────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " RoomWizard Build + Deploy"
echo "════════════════════════════════════════"
info "Started — $(date '+%Y-%m-%d %H:%M:%S')"

CC=arm-linux-gnueabihf-gcc

# Warning flags applied to every compile line.  These are advisory only — the build
# does not use -Werror, because ~30k lines of C were written with warnings off and a
# hard failure would block every deploy.  Capture the output and work the list down:
#   ./build-and-deploy.sh 2>&1 | grep -E 'warning:' | sort | uniq -c | sort -rn
WARN="-Wall -Wextra -Wno-unused-parameter"

if ! command -v $CC &>/dev/null; then
    err "ARM cross-compiler not found. Install with:\n  sudo apt-get install gcc-arm-linux-gnueabihf"
fi
info "Compiler: $($CC --version | head -1)"
echo ""

# ── 2. build ─────────────────────────────────────────────────────────────────
mkdir -p build

step() { echo "[$1] $2..."; }

step " 1/31" "framebuffer";  $CC $WARN -O2 -static -c common/framebuffer.c    -o build/framebuffer.o
step " 2/31" "touch_input";  $CC $WARN -O2 -static -c common/touch_input.c    -o build/touch_input.o
step " 3/31" "touch_calib";  $CC $WARN -O2 -static -c common/touch_calib.c    -o build/touch_calib.o
step " 4/31" "hardware";     $CC $WARN -O2 -static -c common/hardware.c        -o build/hardware.o
step " 5/31" "common";       $CC $WARN -O2 -static -c common/common.c          -o build/common.o
step " 6/31" "highscore";    $CC $WARN -O2 -static -c common/highscore.c       -o build/highscore.o
step " 7/31" "keyboard";     $CC $WARN -O2 -static -c common/keyboard.c        -o build/keyboard.o
step " 8/31" "ui_layout";    $CC $WARN -O2 -static -c common/ui_layout.c       -o build/ui_layout.o
step " 9/31" "audio";        $CC $WARN -O2 -static -c common/audio.c           -o build/audio.o
step "10/31" "ppm";          $CC $WARN -O2 -static -c common/ppm.c             -o build/ppm.o
step "11/31" "logger";       $CC $WARN -O2 -static -c common/logger.c          -o build/logger.o
step "12/31" "config";       $CC $WARN -O2 -static -c common/config.c          -o build/config.o
step "13/31" "gamepad";      $CC $WARN -O2 -static -c common/gamepad.c         -o build/gamepad.o

COMMON_OBJ="build/framebuffer.o build/touch_input.o build/hardware.o build/common.o build/highscore.o build/keyboard.o build/audio.o build/config.o"

# touch_calib.o is NOT in COMMON_OBJ: only the two tools that measure the touch
# mapping need it, and there is no reason to carry the target table into eight
# games.  Both of them must link it, though — it is the one place the fit lives.
CALIB_OBJ="build/touch_calib.o"

step "14/31" "snake";        $CC $WARN -O2 -static snake/snake.c             $COMMON_OBJ build/gamepad.o -o build/snake         -lm
step "15/31" "tetris";       $CC $WARN -O2 -static tetris/tetris.c           $COMMON_OBJ build/gamepad.o -o build/tetris        -lm
step "16/31" "pong";         $CC $WARN -O2 -static pong/pong.c               $COMMON_OBJ build/gamepad.o -o build/pong          -lm

step "17/31" "brick_breaker"
$CC $WARN -O2 -static brick_breaker/brick_breaker.c $COMMON_OBJ build/gamepad.o -o build/brick_breaker -lm

step "18/31" "samegame"
$CC $WARN -O2 -static samegame/samegame.c $COMMON_OBJ build/gamepad.o -o build/samegame -lm

step "19/31" "frogger"
$CC $WARN -O2 -static frogger/frogger.c $COMMON_OBJ build/gamepad.o -o build/frogger -lm

step "20/31" "platformer"
$CC $WARN -O2 -static platformer/platformer.c $COMMON_OBJ build/gamepad.o -o build/platformer -lm

step "21/31" "game_selector"
$CC $WARN -O2 -static -I. game_selector/game_selector.c $COMMON_OBJ build/gamepad.o build/ui_layout.o -o build/game_selector -lm

step "22/31" "app_launcher"
$CC $WARN -O2 -static -I. app_launcher/app_launcher.c $COMMON_OBJ build/gamepad.o build/ppm.o build/logger.o -o build/app_launcher -lm

step "23/31" "hardware_test"
$CC $WARN -O2 -static -I. hardware_test/hardware_test_gui.c $COMMON_OBJ build/ui_layout.o -o build/hardware_test -lm

step "24/31" "hardware_config"
$CC $WARN -O2 -static -I. hardware_config/hardware_config.c $COMMON_OBJ build/ui_layout.o -o build/hardware_config -lm

step "25/31" "hardware_diag"
$CC $WARN -O2 -static -I. hardware_diag/hardware_diag.c $COMMON_OBJ -o build/hardware_diag -lm

step "26/31" "audio_touch_test"
$CC $WARN -O2 -static -I. \
  tests/audio_touch_test.c \
  $COMMON_OBJ build/logger.o build/ppm.o \
  -o build/audio_touch_test -lm

step "27/31" "backlight"
$CC $WARN -O2 -static -I. backlight/backlight.c build/hardware.o build/config.o -o build/backlight

# Owns the calibration wizard (Display tab), which is why it links CALIB_OBJ.
# The standalone unified_calibrate was folded into it and deleted — it was a
# second, independent copy of the same 9-tap fit, carrying the same defect.
step "28/31" "device_tools"
$CC $WARN -O2 -static -I. device_tools/device_tools.c $COMMON_OBJ $CALIB_OBJ build/ui_layout.o -o build/device_tools -lm

# Touch diagnostics. All three were previously absent from this script, which is
# why the deployed touch_trace was stale (pre-bezel) and touch_inject got a
# .hidden marker below without ever being built.
step "29/31" "touch_raw"
$CC $WARN -O2 -static -I. tests/touch_raw.c $COMMON_OBJ $CALIB_OBJ -o build/touch_raw -lm

step "30/31" "touch_trace"
$CC $WARN -O2 -static -I. tests/touch_trace.c $COMMON_OBJ -o build/touch_trace -lm

step "31/31" "touch_inject"
$CC $WARN -O2 -static -I. tests/touch_inject.c -o build/touch_inject

# Collect icon files from source dirs → build/icons/
mkdir -p build/icons
ICON_COUNT=0
for ppm in *//*.ppm; do
    [ -f "$ppm" ] || continue
    cp "$ppm" build/icons/
    ICON_COUNT=$((ICON_COUNT + 1))
done
[ $ICON_COUNT -gt 0 ] && echo "  Collected $ICON_COUNT icon(s) → build/icons/"

# Write the .app manifests locally, from app-manifests.sh's data.  Both the
# deploy path and --bundle copy these exact files, which is the point of having
# them on disk rather than in an ssh heredoc.
rw_write_app_manifests build/apps
echo "  Wrote $(ls build/apps/*.app 2>/dev/null | wc -l) app manifest(s) → build/apps/"

# ── deployed artifacts ──────────────────────────────────────────────────────
# ONE list, used for the build-size listing, the upload, the chmod, the md5
# verification and --bundle.  There used to be two (scp and chmod) and
# audio_touch_test was missing from the chmod one — it worked only because scp
# happens to carry the source file's mode.  A third copy for the md5 check would
# have recreated exactly that bug (../IMPROVEMENT_PLAN.md B19), and a fourth for
# --bundle would recreate it again.
GAMES_BINARIES=(snake tetris pong brick_breaker samegame frogger platformer
                game_selector hardware_test hardware_config hardware_diag
                audio_touch_test backlight device_tools
                touch_raw touch_trace touch_inject)

# .hidden markers: hidden from game_selector's grid but still reachable over SSH.
# Data rather than a loop body in the remote heredoc, so --bundle ships the same
# set.  It must contain only binaries this script actually builds: it used to
# name touch_test, touch_debug, touch_calibrate, pressure_test and
# unified_calibrate, none of which were ever built, so the device accumulated
# markers for binaries that did not exist and `ls /opt/games` lied about what
# was installed.
HIDDEN_MARKERS=(touch_inject touch_raw touch_trace backlight
                hardware_test hardware_config hardware_diag)

echo ""
echo "Build sizes:"
ls -lh "${GAMES_BINARIES[@]/#/build/}" build/app_launcher \
    | awk '{printf "  %-24s %s\n", $9, $5}'
ok "Build complete ($(( $(date +%s) - _START_SECONDS ))s)"
echo ""

# ── 2b. ARM-safety gate ─────────────────────────────────────────────────────
# Cortex-A8 has no hardware integer divide; an sdiv/udiv means SIGILL (exit 132)
# with a blank screen and no output.  Runs on build-only too, so the problem is
# caught at the desk rather than on the wall.
if [[ -x ./check-arm-safe.sh ]]; then
    ./check-arm-safe.sh || err "ARM-safety check failed — refusing to deploy"
else
    warn "check-arm-safe.sh missing — skipping hardware-divide gate"
fi
echo ""

# ── 2c. --bundle: stage instead of deploying ────────────────────────────────
# Deliberately AFTER the ARM-safety gate.  A bundle is published and then
# installed by someone with no toolchain, so it is the one artifact that must
# never carry an sdiv/udiv — the failure would surface on a wall-mounted panel as
# a blank screen with no output (../SYSTEM_ANALYSIS.md#61).
if [[ -n "$BUNDLE_DIR" ]]; then
    echo "════════════════════════════════════════"
    echo " Staging bundle → $BUNDLE_DIR"
    echo "════════════════════════════════════════"

    rw_bundle_init "$BUNDLE_DIR" native_apps || err "could not prepare $BUNDLE_DIR"

    # 0755 for everything executable, 0644 for data.  Stated here, not read off
    # disk — /mnt/c reports 0777 for every file and discards chmod, so a mode
    # measured on this host is a constant (../CLAUDE.md, ../rw-bundle.sh).
    for b in "${GAMES_BINARIES[@]}"; do
        rw_bundle_add "$BUNDLE_DIR" native_apps 0755 "build/$b" "$GAMES_DIR/$b" \
            || err "staging failed: $b"
    done
    rw_bundle_add "$BUNDLE_DIR" native_apps 0755 build/app_launcher /opt/roomwizard/app_launcher \
        || err "staging failed: app_launcher"

    for m in "${HIDDEN_MARKERS[@]}"; do
        # The marker's CONTENT is irrelevant — app_launcher and game_selector test
        # for existence — but a bundle entry needs a real file to copy and an md5
        # to verify, so stage an empty one rather than special-casing "touch" in
        # the installer.
        : > "build/$m.hidden"
        rw_bundle_add "$BUNDLE_DIR" native_apps 0644 "build/$m.hidden" "$GAMES_DIR/$m.hidden" \
            || err "staging failed: $m.hidden"
    done

    for f in build/apps/*.app; do
        [ -f "$f" ] || continue
        rw_bundle_add "$BUNDLE_DIR" native_apps 0644 "$f" "/opt/roomwizard/apps/$(basename "$f")" \
            || err "staging failed: $f"
    done

    for f in build/icons/*.ppm; do
        [ -f "$f" ] || continue
        rw_bundle_add "$BUNDLE_DIR" native_apps 0644 "$f" "/opt/roomwizard/icons/$(basename "$f")" \
            || err "staging failed: $f"
    done

    # app_launcher is this component's boot target, and the only component that
    # has one — the launcher is what makes every other .app reachable.  0644:
    # /opt/roomwizard/default-app is read, never executed.
    echo '/opt/roomwizard/app_launcher' > build/default-app
    rw_bundle_add "$BUNDLE_DIR" native_apps 0644 build/default-app /opt/roomwizard/default-app \
        || err "staging failed: default-app"

    ok "Staged $(rw_bundle_finish "$BUNDLE_DIR" native_apps) file(s)"
    echo ""
    exit 0
fi

# ── 3. deploy? ───────────────────────────────────────────────────────────────
if [[ -z "$DEVICE_IP" ]]; then
    echo "No IP supplied — build only. To deploy:"
    echo "  ./build-and-deploy.sh <ip>"
    echo "  ./build-and-deploy.sh <ip> set-default"
    exit 0
fi

echo "════════════════════════════════════════"
echo " Deploying to $DEVICE_IP"
echo "════════════════════════════════════════"

# Check SSH reachable
info "Testing SSH connection..."
ssh -o ConnectTimeout=5 -o BatchMode=yes "$DEVICE" true 2>/dev/null \
    || err "Cannot reach $DEVICE — check IP and SSH key"
ok "SSH OK"

# Verify system setup has been done
if ! ssh "$DEVICE" "[ -f /opt/roomwizard/disable-steelcase.sh ]" 2>/dev/null; then
    warn "System setup not detected on device."
    warn "Run setup-device.sh first:  ../setup-device.sh $DEVICE_IP"
    echo ""
    read -p "Continue deploying anyway? (y/n): " confirm
    [[ "$confirm" != "y" ]] && exit 1
fi

# Stop whatever is running (avoids "Text file busy" on scp).
#
# There is exactly one stop implementation and it lives on the device, in
# /etc/init.d/roomwizard-app — it matches processes on their executable, so it
# also catches an app that app_launcher started.  Do NOT re-add a `killall` here:
# a per-script copy only knows the basenames whoever wrote it thought of, which
# is how a vnc_client survived a full deploy and repainted over the launcher
# (../IMPROVEMENT_PLAN.md B20, B25).
info "Stopping running apps (device init script)..."
ssh "$DEVICE" 'if [ -x /etc/init.d/roomwizard-app ]; then
    /etc/init.d/roomwizard-app stop
else
    echo "  /etc/init.d/roomwizard-app not installed - run ../setup-device.sh <ip>"
fi' || warn "stop reported a failure - a surviving process may hold the binaries"
ok "Launcher stopped"

# Ensure target directory exists
info "Ensuring target directories exist..."
ssh "$DEVICE" "mkdir -p $GAMES_DIR /var/log/roomwizard"
ok "Target directories ready"

# ── deployed artifacts ──────────────────────────────────────────────────────
# GAMES_BINARIES and HIDDEN_MARKERS are declared once, up with the build, so
# --bundle and this deploy path cannot ship different sets.

# Upload game binaries
info "Uploading game binaries → $GAMES_DIR/ (${#GAMES_BINARIES[@]} files)"
scp "${GAMES_BINARIES[@]/#/build/}" "$DEVICE:$GAMES_DIR/"
ok "Game binaries uploaded"

# Upload app launcher
info "Uploading app launcher → /opt/roomwizard/"
scp build/app_launcher "$DEVICE:/opt/roomwizard/"
ok "App launcher uploaded"

# Upload icons (if any)
if ls build/icons/*.ppm &>/dev/null; then
    info "Uploading icons → /opt/roomwizard/icons/"
    ssh "$DEVICE" "mkdir -p /opt/roomwizard/icons"
    scp build/icons/*.ppm "$DEVICE:/opt/roomwizard/icons/"
    ok "Icons uploaded ($(ls build/icons/*.ppm | wc -l) file(s))"
fi

# Every executable this script put on the device, as the device sees it.
DEPLOYED_EXECUTABLES=("${GAMES_BINARIES[@]/#/$GAMES_DIR/}" /opt/roomwizard/app_launcher)

# Set permissions + markers
# The chmod list is passed in as "$@" rather than written out again, so it
# cannot drift from what was uploaded.  The .hidden marker names come in as a
# second, NUL-free argument list for the same reason — HIDDEN_MARKERS is declared
# with the build, and --bundle stages exactly these names.
info "Setting permissions and markers..."
ssh "$DEVICE" bash -s -- "${#DEPLOYED_EXECUTABLES[@]}" "${DEPLOYED_EXECUTABLES[@]}" "${HIDDEN_MARKERS[@]}" <<'REMOTE'
nexe="$1"; shift
chmod +x "${@:1:$nexe}"
shift "$nexe"

# .noargs marker for scummvm (if present)
[ -f /opt/games/scummvm ] && touch /opt/games/scummvm.noargs && chmod 644 /opt/games/scummvm.noargs

# .hidden markers for dev tools (hidden from game_selector but still reachable
# over SSH).
for name in "$@"; do
    touch  /opt/games/$name.hidden 2>/dev/null || true
    chmod 644 /opt/games/$name.hidden 2>/dev/null || true
done

# Retired tools: sweep the orphan markers, and the one binary that was really
# deployed before being folded into device_tools' Display tab.
rm -f /opt/games/touch_test.hidden \
      /opt/games/touch_debug.hidden \
      /opt/games/touch_calibrate.hidden \
      /opt/games/pressure_test.hidden \
      /opt/games/unified_calibrate.hidden \
      /opt/games/unified_calibrate
REMOTE
ok "Permissions and markers set"

# ── verify what landed ──────────────────────────────────────────────────────
# 18 executables were copied and made runnable with nothing checking that the
# bytes on the device are the bytes that were built.  A truncated scp, a full
# filesystem, or a surviving process still holding an old inode (the B20/B25
# failure mode) all look like a successful deploy otherwise.
info "Verifying deployed binaries (md5)..."
LOCAL_SUMS="$(
    for remote in "${DEPLOYED_EXECUTABLES[@]}"; do
        printf '%s  %s\n' "$remote" "$(md5sum "build/$(basename "$remote")" | cut -d' ' -f1)"
    done | sort
)"
REMOTE_SUMS="$(ssh "$DEVICE" md5sum "${DEPLOYED_EXECUTABLES[@]}" | awk '{print $2"  "$1}' | sort)"

if [[ "$LOCAL_SUMS" == "$REMOTE_SUMS" ]]; then
    ok "Verified ${#DEPLOYED_EXECUTABLES[@]}/${#DEPLOYED_EXECUTABLES[@]} binaries — md5 matches what was built"
else
    echo ""
    echo "  built (local)                        vs  deployed (device):"
    diff <(printf '%s\n' "$LOCAL_SUMS") <(printf '%s\n' "$REMOTE_SUMS") | sed 's/^/    /' || true
    echo ""
    err "md5 mismatch — what is on the device is NOT what was built.\n     A process may still hold an old binary: ssh $DEVICE /etc/init.d/roomwizard-app status"
fi

# Deploy app manifests
#
# The manifests were WRITTEN to build/apps/ during the build, from
# app-manifests.sh's data, and are copied here.  They used to be nine
# `cat > … << APP` heredocs inside this ssh block, which meant the offline
# installer could not produce the same bytes without a second copy of them
# (../IMPROVEMENT_PLAN.md F10).
info "Installing app manifests..."
ssh "$DEVICE" "mkdir -p /opt/roomwizard/apps"
scp build/apps/*.app "$DEVICE:/opt/roomwizard/apps/"
ssh "$DEVICE" bash -s -- $RW_APP_MANIFESTS_RETIRED <<'REMOTE'
chmod 644 /opt/roomwizard/apps/*.app

# Manifests this component used to install: their tools were folded into
# device_tools' tabs, and a stale manifest renders a tile whose exec= is gone.
for name in "$@"; do
    rm -f "/opt/roomwizard/apps/$name.app"
done
REMOTE
ok "App manifests installed ($(ls build/apps/*.app | wc -l) file(s))"
echo ""

# ── 4. set-default mode? ────────────────────────────────────────────────────
if [[ "$MODE" == "set-default" ]]; then
    info "Setting app launcher as default app..."
    ssh "$DEVICE" "mkdir -p /opt/roomwizard && echo '/opt/roomwizard/app_launcher' > /opt/roomwizard/default-app"
    ok "Default app → /opt/roomwizard/app_launcher"
fi

# ── 5. restart launcher ─────────────────────────────────────────────────────
# If the init service is installed, restart it (re-creates respawn wrapper).
# Otherwise just tell the user how to start manually.
if ssh "$DEVICE" '[ -f /etc/init.d/roomwizard-app ]' 2>/dev/null; then
    info "Restarting app launcher..."
    ssh "$DEVICE" '/etc/init.d/roomwizard-app start' 2>&1 | grep -v '^$'
    ok "Launcher running"
else
    echo ""
    echo "  To start app launcher:"
    echo "    ssh $DEVICE '/opt/roomwizard/app_launcher'"
fi

_END_SECONDS=$(date +%s)
_ELAPSED=$((_END_SECONDS - _START_SECONDS))
printf "[$(date '+%H:%M:%S')] Total time: %dm%02ds\n" $((_ELAPSED / 60)) $((_ELAPSED % 60))
echo ""
