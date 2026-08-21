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
# separately by commissioning/provision.sh.  Run that once before deploying for the first time.
#
# --bundle stages the same artifacts this script would scp, under
# <dir>/root/<device-path>, with a declared-mode manifest — the layout
# ../release.sh publishes and ../commissioning/commission-offline.sh installs from
# (../IMPROVEMENT_PLAN.md F9, F10).  It always builds first: this component's
# 35 targets take well under a minute, so there is no reason for a
# stage-what-is-already-there mode and therefore no way to bundle a stale
# binary.  (ScummVM, whose link is ~2 minutes, does have one.)

set -e
_START_SECONDS=$(date +%s)

# Work from this script's own directory, so it can be invoked by path.  Every
# source, build and icon path below is relative (common/*.c, build/, tests/,
# ./check-arm-safe.sh); without this the first compile dies with
# "common/framebuffer.c: No such file or directory" and the script only ever
# worked because deploy-all.sh wraps it in a subshell cd.
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

# The shared SSH gate. Sourced unconditionally (unlike ../lib/rw-bundle.sh, which
# only the --bundle path needs) because every deploy goes through it.
# shellcheck source=../lib/rw-ssh.sh
. "$REPO_ROOT/lib/rw-ssh.sh"

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
    # shellcheck source=../lib/rw-bundle.sh
    . "$REPO_ROOT/lib/rw-bundle.sh"
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
# first ssh, i.e. after all 35 targets had already been compiled.
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

# ── 1b. ARM dependencies ────────────────────────────────────────────────────
# tinyalsa, for the native-ALSA audio backend (../IMPROVEMENT_PLAN.md F1).
# Guarded on the ARTIFACT, never on a flag or a marker file — a generated flag
# goes stale and a stale one has already cost this project a build failure
# (../CLAUDE.md, and ScummVM's build_arm_deps()).  Called from here rather than
# left as a manual prerequisite so a fresh clone still works through
# ../deploy-all.sh, which drives this script unattended.
if [[ ! -f arm-deps/lib/libtinyalsa.a ]]; then
    info "ARM dependencies missing — building them first"
    bash ./build-deps.sh || err "build-deps.sh failed — cannot build without libtinyalsa.a"
fi
echo ""

# ── 2. build ─────────────────────────────────────────────────────────────────
mkdir -p build

step() { echo "[$1] $2..."; }

step " 1/35" "framebuffer";  $CC $WARN -O2 -static -c common/framebuffer.c    -o build/framebuffer.o
step " 2/35" "touch_input";  $CC $WARN -O2 -static -c common/touch_input.c    -o build/touch_input.o
step " 3/35" "touch_calib";  $CC $WARN -O2 -static -c common/touch_calib.c    -o build/touch_calib.o
step " 4/35" "hardware";     $CC $WARN -O2 -static -c common/hardware.c        -o build/hardware.o
step " 5/35" "common";       $CC $WARN -O2 -static -c common/common.c          -o build/common.o
step " 6/35" "highscore";    $CC $WARN -O2 -static -c common/highscore.c       -o build/highscore.o
step " 7/35" "keyboard";     $CC $WARN -O2 -static -c common/keyboard.c        -o build/keyboard.o
step " 8/35" "ui_layout";    $CC $WARN -O2 -static -c common/ui_layout.c       -o build/ui_layout.o
step " 9/35" "audio";        $CC $WARN -O2 -static -c common/audio.c           -o build/audio.o
step "10/35" "audio_gen";    $CC $WARN -O2 -static -c common/audio_gen.c       -o build/audio_gen.o
step "11/35" "audio_out";    $CC $WARN -O2 -static -c common/audio_out.c       -o build/audio_out.o
step "12/35" "audio_wav";    $CC $WARN -O2 -static -c common/audio_wav.c       -o build/audio_wav.o
step "13/35" "ppm";          $CC $WARN -O2 -static -c common/ppm.c             -o build/ppm.o
step "14/35" "logger";       $CC $WARN -O2 -static -c common/logger.c          -o build/logger.o
step "15/35" "config";       $CC $WARN -O2 -static -c common/config.c          -o build/config.o
step "16/35" "gamepad";      $CC $WARN -O2 -static -c common/gamepad.c         -o build/gamepad.o

COMMON_OBJ="build/framebuffer.o build/touch_input.o build/hardware.o build/common.o build/highscore.o build/keyboard.o build/audio.o build/audio_gen.o build/audio_out.o build/audio_wav.o build/config.o"

# audio_gen.o rides with audio.o and is not optional: audio.c calls into it for
# every frame count, every byte count, the envelope and the write loop
# (../IMPROVEMENT_PLAN.md F1 Phase 2).  Both are in COMMON_OBJ, so all 17
# binaries that link the library get them; `backlight` links neither.
#
# audio_out.o joined them in Phase 2 and is not optional either: `audio.h`
# includes `audio_out.h` and every `Audio` embeds an `AudioOut`, so the link
# fails without it rather than degrading — which is the right failure.  ⚠️ It is
# ALSO what makes `native_apps` the only component to redeploy for a change to
# `common/audio*.c`: neither vnc_client (its Makefile's SRCS) nor ScummVM links
# any of the three today, and ScummVM's own OSS mixer becomes a thin adapter over
# this file only at F1 Phase 6.
#
# audio_wav.o joined in Phase 8 and is not optional either, for a DIFFERENT and
# less obvious reason than audio_out.o's: `audio.h` includes `audio_wav.h` and
# every `Audio` embeds two `AudioSampleVoice`, so the STRUCT is complete without
# the object and only `audio_music_start()`/`audio_sfx_play()` reference its code.
# A binary that never plays a file therefore links fine — but audio.o always
# calls audio_wav_open/close/fill, so in practice the link fails without it,
# which is the right failure.  It has no device in it at all (plain stdio), so it
# is also the one audio object a host test needs no shim to reach.

# touch_calib.o is NOT in COMMON_OBJ: only the two tools that measure the touch
# mapping need it, and there is no reason to carry the target table into eight
# games.  Both of them must link it, though — it is the one place the fit lives.
CALIB_OBJ="build/touch_calib.o"

step "17/35" "snake";        $CC $WARN -O2 -static snake/snake.c             $COMMON_OBJ build/gamepad.o -o build/snake         -lm
step "18/35" "tetris";       $CC $WARN -O2 -static tetris/tetris.c           $COMMON_OBJ build/gamepad.o -o build/tetris        -lm
step "19/35" "pong";         $CC $WARN -O2 -static pong/pong.c               $COMMON_OBJ build/gamepad.o -o build/pong          -lm

step "20/35" "brick_breaker"
$CC $WARN -O2 -static brick_breaker/brick_breaker.c $COMMON_OBJ build/gamepad.o -o build/brick_breaker -lm

step "21/35" "samegame"
$CC $WARN -O2 -static samegame/samegame.c $COMMON_OBJ build/gamepad.o -o build/samegame -lm

step "22/35" "frogger"
$CC $WARN -O2 -static frogger/frogger.c $COMMON_OBJ build/gamepad.o -o build/frogger -lm

step "23/35" "platformer"
$CC $WARN -O2 -static platformer/platformer.c $COMMON_OBJ build/gamepad.o -o build/platformer -lm

step "24/35" "game_selector"
$CC $WARN -O2 -static -I. game_selector/game_selector.c $COMMON_OBJ build/gamepad.o build/ui_layout.o -o build/game_selector -lm

step "25/35" "app_launcher"
$CC $WARN -O2 -static -I. app_launcher/app_launcher.c $COMMON_OBJ build/gamepad.o build/ppm.o build/logger.o -o build/app_launcher -lm

step "26/35" "hardware_test"
$CC $WARN -O2 -static -I. hardware_test/hardware_test_gui.c $COMMON_OBJ build/ui_layout.o -o build/hardware_test -lm

step "27/35" "hardware_config"
$CC $WARN -O2 -static -I. hardware_config/hardware_config.c $COMMON_OBJ build/ui_layout.o -o build/hardware_config -lm

step "28/35" "hardware_diag"
$CC $WARN -O2 -static -I. hardware_diag/hardware_diag.c $COMMON_OBJ -o build/hardware_diag -lm

step "29/35" "audio_touch_test"
$CC $WARN -O2 -static -I. \
  tests/audio_touch_test.c \
  $COMMON_OBJ build/logger.o build/ppm.o \
  -o build/audio_touch_test -lm

step "30/35" "backlight"
$CC $WARN -O2 -static -I. backlight/backlight.c build/hardware.o build/config.o -o build/backlight

# Owns the calibration wizard (Display tab), which is why it links CALIB_OBJ.
# The standalone unified_calibrate was folded into it and deleted — it was a
# second, independent copy of the same 9-tap fit, carrying the same defect.
step "31/35" "device_tools"
$CC $WARN -O2 -static -I. device_tools/device_tools.c $COMMON_OBJ $CALIB_OBJ build/ui_layout.o -o build/device_tools -lm

# Touch diagnostics. All three were previously absent from this script, which is
# why the deployed touch_trace was stale (pre-bezel) and touch_inject got a
# .hidden marker below without ever being built.
step "32/35" "touch_raw"
$CC $WARN -O2 -static -I. tests/touch_raw.c $COMMON_OBJ $CALIB_OBJ -o build/touch_raw -lm

step "33/35" "touch_trace"
$CC $WARN -O2 -static -I. tests/touch_trace.c $COMMON_OBJ -o build/touch_trace -lm

step "34/35" "touch_inject"
$CC $WARN -O2 -static -I. tests/touch_inject.c -o build/touch_inject

# The mix bus, driven by hand.  Groups I/J/K of tests/audio_gen_test.c cover the
# arithmetic; whether two sounds are AUDIBLE as two, and whether the ~60 ms
# minimum-tone rule survives a stream that is never reset, need an ear at the
# panel (../IMPROVEMENT_PLAN.md F1 Phase 3, panel items 12 and 14).
step "35/35" "audio_mix_test"
$CC $WARN -O2 -static -I. tests/audio_mix_test.c $COMMON_OBJ -o build/audio_mix_test -lm

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
# have recreated exactly that bug, and a fourth for
# --bundle would recreate it again.
GAMES_BINARIES=(snake tetris pong brick_breaker samegame frogger platformer
                game_selector hardware_test hardware_config hardware_diag
                audio_touch_test audio_mix_test backlight device_tools
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

# ── 2b2. Audio pacing gate ──────────────────────────────────────────────────
# A source check, not a binary one: an app that turns the mix bus on must also
# service it every iteration and keep audio_pump_active() in its frame pacing.
# Neither omission errors — the game runs and the sound has gaps in it — so this
# is arithmetic rather than a rule to remember while F1 Phase 5 converts the
# remaining games.  Runs on build-only too, for the same reason as the gate above.
if [[ -x ./check-audio-pacing.sh ]]; then
    ./check-audio-pacing.sh || err "Audio pacing check failed — refusing to deploy"
else
    warn "check-audio-pacing.sh missing — skipping audio pacing gate"
fi
echo ""

# ── 2c. --bundle: stage instead of deploying ────────────────────────────────
# Deliberately AFTER the ARM-safety gate.  A bundle is published and then
# installed by someone with no toolchain, so it is the one artifact that must
# never carry an sdiv/udiv — the failure would surface on a wall-mounted panel as
# a blank screen with no output (../SYSTEM_ANALYSIS.md#61-cortex-a8-has-no-hardware-integer-divide).
if [[ -n "$BUNDLE_DIR" ]]; then
    echo "════════════════════════════════════════"
    echo " Staging bundle → $BUNDLE_DIR"
    echo "════════════════════════════════════════"

    rw_bundle_init "$BUNDLE_DIR" native_apps || err "could not prepare $BUNDLE_DIR"

    # 0755 for everything executable, 0644 for data.  Stated here, not read off
    # disk — /mnt/c reports 0777 for every file and discards chmod, so a mode
    # measured on this host is a constant (../CLAUDE.md, ../lib/rw-bundle.sh).
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

    # The effect clips, for the same reason the online path uploads them: without
    # them the canned sounds are note tables below the speaker's knee.  0644 —
    # data, read by audio.c, never executed.
    for f in sounds/fx_*.wav; do
        [ -f "$f" ] || continue
        rw_bundle_add "$BUNDLE_DIR" native_apps 0644 "$f" "/opt/sound/$(basename "$f")" \
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
# The shared gate (lib/rw-ssh.sh): "down" and "up but refusing our key" are different
# answers, and only the second one has a remedy worth offering.
rw_ssh_gate "$DEVICE" || err "Cannot continue without SSH to $DEVICE"
ok "SSH OK"

# Verify system setup has been done
if ! ssh "$DEVICE" "[ -f /opt/roomwizard/disable-steelcase.sh ]" 2>/dev/null; then
    warn "System setup not detected on device."
    warn "Run commissioning/provision.sh first:  ../commissioning/provision.sh $DEVICE_IP"
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
# is how a vnc_client survived a full deploy and repainted over the launcher.
info "Stopping running apps (device init script)..."
ssh "$DEVICE" 'if [ -x /etc/init.d/roomwizard-app ]; then
    /etc/init.d/roomwizard-app stop
else
    echo "  /etc/init.d/roomwizard-app not installed - run ../commissioning/provision.sh <ip>"
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

# Upload the generated effect clips → /opt/sound/
#
# ⚠️ Without these the four canned sounds fall back to their note tables, which
# is a working device that has NOT had the audibility fix (../IMPROVEMENT_PLAN.md
# F1 Phase 5 ③) — every one of those tones is at or below the speaker's knee.  So
# this is not an optional asset step.
#
# /opt/sound is where the music bed already lives and device-files/clean-rules.conf
# keeps that directory wholesale, so a re-commission does not take them away.  They
# are checked in (sounds/fx_*.wav, byte-reproducible from sounds/gen-sounds.sh), so
# unlike the music there is nothing to hand-copy.
if ls sounds/fx_*.wav &>/dev/null; then
    info "Uploading effect clips → /opt/sound/"
    ssh "$DEVICE" "mkdir -p /opt/sound"
    scp sounds/fx_*.wav "$DEVICE:/opt/sound/"
    ok "Effect clips uploaded ($(ls sounds/fx_*.wav | wc -l) file(s))"
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
# 19 executables were copied and made runnable with nothing checking that the
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
