#!/bin/bash
#
# release.sh — build every component, stage one bundle, publish it as a GitHub
#              release.  IMPROVEMENT_PLAN.md F9.
#
# Usage:
#   ./release.sh --stage-only [--out <dir>]        # build + stage + tar, no network
#   ./release.sh --tag <tag>  [--out <dir>]        # the above, then gh release create
#   ./release.sh --stage-only --component native_apps
#   ./release.sh --help
#
# ── Why this exists ─────────────────────────────────────────────────────────
#
# The build is the slowest and most environment-bound step in the whole flow:
# WSL, an ARM cross-compiler, and a ScummVM link that takes ~1m35s–2m20s and
# deletes native_apps/common/*.o twice on the way.  All of that is pure overhead
# whenever the source has not changed, and it is a hard barrier for anyone who
# wants to put apps on a device without first reproducing the toolchain.  The
# artifacts suit distribution unusually well: everything ships -static, so there
# is no ABI surface to match against the device's glibc.
#
# commission-offline.sh is the first non-developer consumer — an offline
# commissioner has no toolchain to fall back on, so a bundle IS its only source
# of binaries.
#
# ── What is NOT published, and why ──────────────────────────────────────────
#
#   * Device config.  Never.  /etc/hosts carries a mapping that exists to be
#     removed (IMPROVEMENT_PLAN.md D7) and vnc_client.conf carries a plaintext
#     VNC password.  A glob that swept up *.conf would publish exactly what those
#     two exist to have removed.  Each component's --bundle decides; this script
#     re-checks below.
#   * The vendor image or any part of it — a third party's copyright.
#   * usb_host.  It patches uImage-system, which lives on p1, and the offline
#     installer must never touch p1: an untouched p1 is what keeps a power cycle
#     a free undo (SYSTEM_ANALYSIS.md#47-recovery).
#
# ── Not byte-reproducible ───────────────────────────────────────────────────
#
# base/version.o re-embeds the build date on every link, so two releases from an
# identical tree differ.  The md5 manifest is therefore GENERATED per release by
# rw-bundle.sh, never asserted against a known-good set.
#
# ── gh ──────────────────────────────────────────────────────────────────────
#
# `gh` 2.86.0 is installed in WSL (from the release .deb — focal's apt has no `gh`
# and the snap links against a glibc newer than 2.31), but --tag has still never
# been run; --stage-only is the tested path and produces a tarball
# commission-offline.sh --bundle <file> takes directly.  That is why the publish
# step is a thin wrapper around one gh command and nothing depends on its output.
# `origin` is an SSH host alias (git@github.com-personal:…), so whoever publishes
# needs `gh auth` for that account — and gh may need --repo, since it resolves the
# owner from the remote URL.

set -e
_START_SECONDS=$(date +%s)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# shellcheck source=rw-bundle.sh
. "$SCRIPT_DIR/rw-bundle.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}" >&2; exit 1; }

# ── the components a release carries ────────────────────────────────────────
# usb_host is absent on purpose — see the header.  Order matters only in that
# native_apps writes /opt/roomwizard/default-app and the app_launcher every other
# component's tile is reached through.
RELEASE_COMPONENTS=(native_apps vnc_client scummvm-roomwizard)

usage() {
    echo "Usage: $0 --stage-only [--out <dir>] [--component <name>]..."
    echo "       $0 --tag <tag>  [--out <dir>] [--component <name>]..."
    echo ""
    echo "  --stage-only      Build, stage and tar. No network, no gh."
    echo "  --tag <tag>       The above, then publish with 'gh release create <tag>'."
    echo "  --out <dir>       Where to stage (default: build/release)."
    echo "  --component <n>   Only this component; repeatable. Default: all of"
    echo "                    ${RELEASE_COMPONENTS[*]}"
    echo "  --notes <file>    Release notes for gh (default: generated)."
    echo ""
    echo "Components: ${RELEASE_COMPONENTS[*]}   (usb_host is excluded: it patches p1)"
    exit 1
}

TAG=""
OUT="build/release"
STAGE_ONLY=0
NOTES_FILE=""
COMPONENTS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage-only) STAGE_ONLY=1; shift ;;
        --tag)        TAG="${2:-}";  [[ -n "$TAG" ]]  || { echo "--tag needs a value"; usage; }; shift 2 ;;
        --out)        OUT="${2:-}";  [[ -n "$OUT" ]]  || { echo "--out needs a value"; usage; }; shift 2 ;;
        --notes)      NOTES_FILE="${2:-}"; [[ -f "$NOTES_FILE" ]] || { echo "no such notes file: ${2:-}"; usage; }; shift 2 ;;
        --component)
            [[ -n "${2:-}" ]] || { echo "--component needs a value"; usage; }
            # Validated against the list rather than passed through: an unknown
            # name would otherwise stage nothing and produce an empty bundle that
            # installs cleanly and delivers no apps.
            found=0
            for c in "${RELEASE_COMPONENTS[@]}"; do [[ "$c" == "$2" ]] && found=1; done
            [[ $found -eq 1 ]] || { echo "Unknown component: $2"; echo ""; usage; }
            COMPONENTS+=("$2"); shift 2 ;;
        --help|-h)    usage ;;
        *)            echo "Unknown option: $1"; echo ""; usage ;;
    esac
done

[[ ${#COMPONENTS[@]} -gt 0 ]] || COMPONENTS=("${RELEASE_COMPONENTS[@]}")

if [[ $STAGE_ONLY -eq 0 && -z "$TAG" ]]; then
    echo "Give either --stage-only or --tag <tag>."
    echo ""
    usage
fi
if [[ $STAGE_ONLY -eq 1 && -n "$TAG" ]]; then
    err "--stage-only and --tag are mutually exclusive"
fi

echo ""
echo "════════════════════════════════════════"
echo " RoomWizard release"
echo "════════════════════════════════════════"
info "Components: ${COMPONENTS[*]}"
info "Staging to: $OUT"
[[ -n "$TAG" ]] && info "Tag:        $TAG"

# ── stage ───────────────────────────────────────────────────────────────────
# A whole-directory wipe, because a bundle assembled on top of a previous one can
# carry a file no manifest names — which rw_bundle_check then reports, but only
# after the operator has already been told the release is ready.
#
# Guarded rather than trusted: $OUT comes from the command line and this is an
# `rm -rf`.  The same reasoning as rw-clean.sh's del() guard, and the same
# refusal.
case "$OUT" in
    ""|"/"|"/*") err "refusing to stage into '$OUT'" ;;
esac
[[ "$OUT" == *".."* ]] && err "refusing to stage into a path containing '..': $OUT"
if [[ -e "$OUT" && ! -d "$OUT" ]]; then
    err "$OUT exists and is not a directory"
fi
rm -rf "$OUT"
mkdir -p "$OUT"
OUT_ABS="$(cd "$OUT" && pwd)"

for comp in "${COMPONENTS[@]}"; do
    echo ""
    echo "────────────────────────────────────────"
    echo " $comp"
    echo "────────────────────────────────────────"
    [[ -x "$SCRIPT_DIR/$comp/build-and-deploy.sh" || -f "$SCRIPT_DIR/$comp/build-and-deploy.sh" ]] \
        || err "$comp/build-and-deploy.sh not found"

    # `bash <script>`, never ./<script>: a fresh clone can land without the
    # executable bit and /mnt/c cannot even show whether it has one (CLAUDE.md).
    ( cd "$SCRIPT_DIR/$comp" && bash ./build-and-deploy.sh --bundle "$OUT_ABS" ) \
        || err "$comp --bundle failed"
done

# ── bundle metadata ─────────────────────────────────────────────────────────
echo ""
echo "────────────────────────────────────────"
echo " Bundle"
echo "────────────────────────────────────────"

GIT_REV="$(git -C "$SCRIPT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"
GIT_DIRTY=""
git -C "$SCRIPT_DIR" diff --quiet 2>/dev/null || GIT_DIRTY=" (dirty)"

{
    echo "tag=${TAG:-untagged}"
    echo "built=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "commit=${GIT_REV}${GIT_DIRTY}"
    echo "components=$(rw_bundle_components "$OUT_ABS" | tr '\n' ' ')"
} > "$OUT_ABS/manifest.d/bundle.info"

# ── NOTICE: the two licence obligations F9 records ──────────────────────────
cat > "$OUT_ABS/NOTICE" <<'NOTICE'
RoomWizard app bundle
=====================

This bundle contains statically linked ARM binaries for the Steelcase RoomWizard
(TI OMAP3503, Cortex-A8, kernel 4.14.52).  It contains NO vendor firmware and NO
device configuration.

ScummVM
-------
The `scummvm` binary is a build of ScummVM, which is licensed under the GNU
General Public License, version 2 or later.  The complete corresponding source
for this binary is the upstream ScummVM tree plus this project's backend port in
`scummvm-roomwizard/backend-files/`; both are available from the repository this
release was published from.  Written offers for the source are honoured for the
lifetime of the release.

vnc_client
----------
Links LibVNCClient (GPLv2+), zlib (zlib licence) and libjpeg-turbo
(IJG / BSD-3-Clause).  Same corresponding-source offer as above.

native_apps
-----------
This project's own code, statically linked against glibc (LGPL-2.1+).

Not included
------------
No device configuration is published, deliberately: /etc/hosts carries a
host-name mapping that exists in order to be removed, and vnc_client.conf carries
a plaintext VNC password.  Create the latter on the device after installing.
NOTICE

# ── structural + policy checks on what was staged ───────────────────────────
info "Checking bundle structure..."
if ! CHECK_OUT="$(rw_bundle_check "$OUT_ABS")"; then
    echo "$CHECK_OUT"
    err "bundle is not self-consistent"
fi
ok "Every manifest entry is staged, and every staged file is in a manifest"

# The publish-no-configs rule, enforced rather than trusted.  Each component's
# --bundle already declines to stage its config; this is the negative control
# that would catch a future component that forgot.
info "Checking that no device config was staged..."
CONFIG_HITS="$(rw_bundle_entries "$OUT_ABS" | awk '{print $2}' | grep -E '\.conf$|/etc/hosts$|/etc/hostname$|rw_config|touch_calibration|input_config' || true)"
if [[ -n "$CONFIG_HITS" ]]; then
    echo "$CONFIG_HITS" | sed 's/^/    /'
    err "a release must publish binaries only, never device config (IMPROVEMENT_PLAN.md F9)"
fi
ok "No config files staged"

FILE_COUNT="$(rw_bundle_entries "$OUT_ABS" | grep -c . || true)"
info "$FILE_COUNT file(s) staged:"
rw_bundle_components "$OUT_ABS" | while read -r c; do
    printf '    %-22s %s file(s)\n' "$c" "$(grep -c . "$OUT_ABS/manifest.d/$c.list")"
done

# ── tar ─────────────────────────────────────────────────────────────────────
# --owner/--group/--numeric-owner so the archive does not carry whoever built it,
# and so two builds differ only in the artifacts.  Modes inside the tar are NOT
# the authority — manifest.d/*.list is (see rw-bundle.sh); on /mnt/c every file
# reads 0777 and the tar would say so.
TARBALL="$SCRIPT_DIR/build/roomwizard-apps-${TAG:-$(date -u '+%Y%m%d')}.tar.gz"
mkdir -p "$(dirname "$TARBALL")"
info "Creating $TARBALL"
tar -czf "$TARBALL" \
    --owner=root --group=root --numeric-owner \
    -C "$OUT_ABS" root manifest.d NOTICE
ok "Tarball: $(du -h "$TARBALL" | cut -f1)"

md5sum "$TARBALL" | sed 's/^/    /'

# ── publish ─────────────────────────────────────────────────────────────────
if [[ $STAGE_ONLY -eq 1 ]]; then
    echo ""
    ok "Staged only — nothing was published."
    echo ""
    echo "  Install it offline, with no network and no toolchain:"
    echo "    ./commission-offline.sh --bundle $TARBALL"
    echo ""
    echo "  Or publish it:"
    echo "    $0 --tag <tag>"
else
    echo ""
    echo "────────────────────────────────────────"
    echo " Publishing"
    echo "────────────────────────────────────────"
    command -v gh >/dev/null 2>&1 || err "gh is not installed — install it, or use --stage-only.
     The tarball is already built: $TARBALL"

    if [[ -z "$NOTES_FILE" ]]; then
        NOTES_FILE="$(mktemp)"
        {
            echo "Statically linked ARM binaries for the Steelcase RoomWizard."
            echo ""
            echo "Install offline, with no toolchain:"
            echo ""
            echo '```'
            echo "./commission-offline.sh --bundle $(basename "$TARBALL")"
            echo '```'
            echo ""
            echo "Built from ${GIT_REV}${GIT_DIRTY}. Components:"
            rw_bundle_components "$OUT_ABS" | sed 's/^/- /'
            echo ""
            echo "See NOTICE inside the tarball for licence obligations (ScummVM is GPLv2+)."
        } > "$NOTES_FILE"
    fi

    info "gh release create $TAG"
    gh release create "$TAG" "$TARBALL" \
        --title "RoomWizard apps $TAG" \
        --notes-file "$NOTES_FILE" \
        || err "gh release create failed — the tarball is still at $TARBALL"
    ok "Published $TAG"
fi

_ELAPSED=$(( $(date +%s) - _START_SECONDS ))
echo ""
printf "  Total time: %dm%02ds\n" $((_ELAPSED / 60)) $((_ELAPSED % 60))
echo ""
