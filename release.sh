#!/bin/bash
#
# release.sh — build every component, stage one bundle, publish it as a GitHub
#              release.
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
# commissioning/commission-offline.sh is the first non-developer consumer — an offline
# commissioner has no toolchain to fall back on, so a bundle IS its only source
# of binaries.
#
# ── What is NOT published, and why ──────────────────────────────────────────
#
#   * Device config.  Never.  /etc/hosts carries a mapping that exists to be
#     removed and vnc_client.conf carries a plaintext
#     VNC password.  A glob that swept up *.conf would publish exactly what those
#     two exist to have removed.  Each component's --bundle decides; this script
#     re-checks below.
#   * The vendor image or any part of it — a third party's copyright.  In
#     particular uImage-system, which usb_host patches: the installer DERIVES the
#     patch from the card's own copy (lib/rw-usbpower.sh, md5-gated in and
#     md5-asserted out) rather than shipping 5.2 MB of Steelcase kernel.  The
#     manifest check below is the negative control for that rule, exactly parallel
#     to the config one.
#
# ── usb_host IS published, and only its p1 step is not ──────────────────────
#
# It was excluded outright until 2026-08-08, on the grounds that it "patches
# uImage-system, which lives on p1".  That conflated three mechanisms
# (IMPROVEMENT_PLAN.md F15): the /dev/mem MUSB patch and the xpad/joydev modules
# are entirely on p6, and it is only the 500 mA power budget that touches p1.  So
# the four build artifacts are bundled like any others; the three device scripts
# are device-files/provision-rules.conf's `usb` group; and the p1 patch is a
# separate, opt-out-able step of the installer that no bundle carries.

#
# ── Not byte-reproducible ───────────────────────────────────────────────────
#
# base/version.o re-embeds the build date on every link, so two releases from an
# identical tree differ.  The md5 manifest is therefore GENERATED per release by
# lib/rw-bundle.sh, never asserted against a known-good set.
#
# ── gh ──────────────────────────────────────────────────────────────────────
#
# `gh` 2.86.0 is installed in WSL (from the release .deb — focal's apt has no `gh`
# and the snap links against a glibc newer than 2.31).  --stage-only stays the
# network-free path and produces a tarball commissioning/commission-offline.sh
# --bundle <file> takes directly.
#
# ⚠️ Every gh call below passes --repo, and it is NOT optional.  `origin` is an SSH
# host alias, and gh does not merely fail to guess the owner from it — it refuses
# the repository outright: "none of the git remotes configured for this repository
# point to a known GitHub host".  So the publish step derives owner/repo itself.
# Whoever publishes still needs `gh auth` for an account with write access.

set -e
_START_SECONDS=$(date +%s)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# shellcheck source=lib/rw-bundle.sh
. "$SCRIPT_DIR/lib/rw-bundle.sh"
# shellcheck source=lib/rw-release.sh
# Sourced for rw_release_repo alone — the one owner/repo derivation, shared with
# the fetch side so a fork publishes to and fetches from the same place.
. "$SCRIPT_DIR/lib/rw-release.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
info() { echo -e "${YELLOW}  → $*${NC}"; }
warn() { echo -e "${BLUE}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}" >&2; exit 1; }

# ── the components a release carries ────────────────────────────────────────
# Order matters only in that native_apps writes /opt/roomwizard/default-app and
# the app_launcher every other component's tile is reached through.
#
# usb_host is here as of 2026-08-08 (IMPROVEMENT_PLAN.md F15). Its --bundle needs
# usb_host/device_config and usb_host/modules/*.ko, both gitignored build
# artifacts; on a fresh clone it refuses with the one command that fetches the
# config from any unit.
RELEASE_COMPONENTS=(native_apps vnc_client scummvm-roomwizard usb_host)

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
    echo "Components: ${RELEASE_COMPONENTS[*]}"
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
# `rm -rf`.  The same reasoning as lib/rw-clean.sh's del() guard, and the same
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
# The full SHA as well, for gh --target only.  GitHub's API takes a branch name or
# a 40-character SHA for target_commitish and rejects an abbreviated one outright:
# a short rev there fails the whole publish with "HTTP 422 Validation Failed:
# Release.target_commitish is invalid", after the build and the staging are done.
GIT_REV_FULL="$(git -C "$SCRIPT_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"
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
General Public License, version 3 or later.  (Measured, not assumed: the upstream
tree's COPYING is GPLv3 and its source headers read "either version 3 of the
License, or (at your option) any later version".)  The complete corresponding
source for this binary is the upstream ScummVM tree plus this project's backend
port in `scummvm-roomwizard/backend-files/`; both are available from the
repository this release was published from.  Written offers for the source are
honoured for the lifetime of the release.

Three data files travel with it and are ALSO GPLv3+, not this project's own:
`scummremastered.zip` and `gui-icons.dat` are staged verbatim out of the ScummVM
tree, and `vkeybd_roomwizard.zip` is a 2x scaled derivative of ScummVM's
`vkeybd_small.zip`.  Same corresponding-source offer.

vnc_client
----------
Links LibVNCClient (GPLv2+), zlib (zlib licence) and libjpeg-turbo
(IJG / BSD-3-Clause).  Same corresponding-source offer as above.

native_apps
-----------
This project's own code (MIT — see LICENSE.md), statically linked against glibc
(LGPL-2.1+).

Audio assets  ⚠️ NOT FOR COMMERCIAL USE
---------------------------------------
The sound effects installed to /opt/sound as `fx_*.wav` (eleven files) are AI-
generated audio from https://elevenlabs.io, produced on a FREE account, which
does not grant commercial use.  They are the only content in this bundle that is
narrower than its code licence: THIS BUNDLE MAY NOT BE USED OR REDISTRIBUTED
COMMERCIALLY unless those eleven files are removed or replaced.  Removing them is
safe — every game falls back to its built-in note tables.

ATTRIBUTION, REQUIRED — this credit must travel with the bundle:

    Sound effects generated with ElevenLabs — elevenlabs.io.

Both obligations were confirmed by ElevenLabs and relayed by the author on
2026-08-23, and they are the whole of what is established: non-commercial use,
plus that credit.  See LICENSE.md, which is the repo-level half of this notice
and must agree with it.

The music beds installed to /opt/sound as `<stem><n>-mono.wav` (one set per game)
are AI-generated and are not this project's composition.  All of them are from
musely.ai and ARE royalty-free for commercial use per that generator's FAQ, with
no attribution requirement — so they are NOT a restriction on this bundle.  Two
limits: the FAQ asserts clearance but not ownership, so they cannot be licensed
onward as this project's own, and every bed carries the string "made with suno" in
its WAV metadata, which is consistent with musely.ai being a Suno front end and is
not a separate licence claim.  LICENSE.md is the repo-level half of this and must
agree with it.  Removing any bed is safe — a game whose configured bed is missing
plays its effects and carries on.

tinyalsa
--------
The native_apps audio backend is built against tinyalsa 2.0.0, cross-built from
source by `native_apps/build-deps.sh` (not vendored, not modified beyond one
one-line build fix documented in that script).  BSD-3-Clause requires its
copyright notice, list of conditions and disclaimer to accompany a binary
distribution, so they are reproduced here in full:

    Copyright 2011, The Android Open Source Project

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright
          notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
          notice, this list of conditions and the following disclaimer in the
          documentation and/or other materials provided with the distribution.
        * Neither the name of The Android Open Source Project nor the names of
          its contributors may be used to endorse or promote products derived
          from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
    DAMAGE.

usb_host
--------
`devmem_write` is this project's own code (MIT).

⚠️ `ff-memless.ko`, `joydev.ko` and `xpad.ko` are LINUX KERNEL MODULES and are
licensed under the GNU General Public License, version 2 (GPL-2.0-only), as is
the kernel they are built against.  WRITTEN OFFER FOR SOURCE: the complete
corresponding source is the unmodified upstream Linux 4.14.52 tree, as published
at

    https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-4.14.52.tar.xz

together with the kernel configuration and build script in this project's
repository at `usb_host/build-xpad-module.sh`.  No kernel source was modified;
the three modules are upstream drivers compiled for this device's kernel
configuration, which is read off the device itself.  This offer is honoured for
the lifetime of the release.

Not included
------------
No device configuration is published, deliberately: /etc/hosts carries a
host-name mapping that exists in order to be removed, and vnc_client.conf carries
a plaintext VNC password.  Create the latter on the device after installing.

No vendor firmware is published either.  In particular this bundle does NOT
contain `uImage-system`: the USB 500 mA power patch is derived on the spot from
the copy already on the device, gated on its md5 and backed up first.
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
    err "a release must publish binaries only, never device config"
fi
ok "No config files staged"

# The never-publish-the-vendor-kernel rule, enforced rather than trusted — the
# negative control for lib/rw-usbpower.sh's "derive, don't ship" design and the
# reason a bundle stays 5.2 MB smaller.  usb_host/.gitignore already calls
# uImage-system* "Copyrighted device-specific files"; this repo is meant to be
# published, so a component that decided to stage one anyway must not get past
# here (IMPROVEMENT_PLAN.md F15).
#
# Matched on the BASENAME, not on a path: p1 is not a bundle path at all, so a
# staged copy would arrive at some invented location like /opt/roomwizard/ — the
# hazard is the bytes, wherever they are put.
info "Checking that no vendor firmware was staged..."
FIRMWARE_HITS="$(rw_bundle_entries "$OUT_ABS" | awk '{print $2}' \
    | grep -E '(^|/)(uImage[^/]*|mlo|MLO|u-boot[^/]*|ctrlblock[^/]*)$' || true)"
if [[ -n "$FIRMWARE_HITS" ]]; then
    echo "$FIRMWARE_HITS" | sed 's/^/    /'
    err "a release must never publish the vendor kernel or boot chain — it is a third
     party's copyright, and the usb_host power patch is DERIVED from the device's
     own copy for exactly this reason (IMPROVEMENT_PLAN.md F15)"
fi
ok "No vendor firmware staged"

FILE_COUNT="$(rw_bundle_entries "$OUT_ABS" | grep -c . || true)"
info "$FILE_COUNT file(s) staged:"
rw_bundle_components "$OUT_ABS" | while read -r c; do
    printf '    %-22s %s file(s)\n' "$c" "$(grep -c . "$OUT_ABS/manifest.d/$c.list")"
done

# ── tar ─────────────────────────────────────────────────────────────────────
# --owner/--group/--numeric-owner so the archive does not carry whoever built it,
# and so two builds differ only in the artifacts.  Modes inside the tar are NOT
# the authority — manifest.d/*.list is (see lib/rw-bundle.sh); on /mnt/c every file
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
    echo "    ./commissioning/commission-offline.sh --bundle $TARBALL"
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

    # gh resolves owner/repo from the remote URL and CANNOT resolve this one:
    # `origin` is an SSH host alias (git@github.com-personal:owner/repo.git), and
    # gh rejects the whole repository with "none of the git remotes configured for
    # this repository point to a known GitHub host".  Measured, not anticipated —
    # every gh call here therefore needs an explicit --repo.
    #
    # ⚠️ The derivation itself lives in lib/rw-release.sh and there is ONE of it,
    # because the fetch side needs exactly the same answer: a fork must fetch from
    # the fork it published to.  Do not write a second copy here.
    GH_REPO="$(rw_release_repo "$SCRIPT_DIR" 2>/dev/null || true)"
    case "$GH_REPO" in
        */*) ;;
        *)   err "could not derive owner/repo from origin
     ('$(git -C "$SCRIPT_DIR" remote get-url origin 2>/dev/null)').
     gh cannot resolve it either, so pass a tarball to 'gh release create --repo
     <owner>/<repo>' by hand. The tarball is already built: $TARBALL" ;;
    esac
    info "Publishing to $GH_REPO"

    # A tag whose commit is not on the remote makes the bundle's NOTICE false.
    # NOTICE carries a GPL written offer — "the complete corresponding source ...
    # available from the repository this release was published from" — for ScummVM
    # (GPLv3+), LibVNCClient (GPLv2+) and three kernel modules (GPL-2.0-only).  An
    # unpushed or dirty tree does not satisfy that offer, and the failure is silent:
    # gh would tag the remote default branch's HEAD instead, so the release would
    # name a commit that builds different binaries.
    [[ -z "$GIT_DIRTY" ]] || err "the working tree is dirty, so the source for these
     binaries is not in any commit — and NOTICE offers exactly that source. Commit
     first. The tarball is already built: $TARBALL"
    if [[ -z "$(git -C "$SCRIPT_DIR" branch -r --contains HEAD 2>/dev/null)" ]]; then
        err "HEAD ($GIT_REV) is on no remote branch, so the corresponding source
     NOTICE offers is unpublished. Push first. The tarball is already built: $TARBALL"
    fi

    if [[ -z "$NOTES_FILE" ]]; then
        NOTES_FILE="$(mktemp)"
        {
            echo "Statically linked ARM binaries for the Steelcase RoomWizard."
            echo ""
            echo "Install offline, with no toolchain:"
            echo ""
            echo '```'
            echo "./commissioning/commission-offline.sh --bundle $(basename "$TARBALL")"
            echo '```'
            echo ""
            echo "Built from ${GIT_REV}${GIT_DIRTY}. Components:"
            rw_bundle_components "$OUT_ABS" | sed 's/^/- /'
            echo ""
            echo "See NOTICE inside the tarball for licence obligations (ScummVM is GPLv3+)."
        } > "$NOTES_FILE"
    fi

    info "gh release create $TAG"
    # --target pins the tag to the commit the binaries were actually built from.
    # Without it gh tags the remote default branch's HEAD, which is a different
    # commit whenever anything landed on main after this build started.
    gh release create "$TAG" "$TARBALL" \
        --repo "$GH_REPO" \
        --target "$GIT_REV_FULL" \
        --title "RoomWizard apps $TAG" \
        --notes-file "$NOTES_FILE" \
        || err "gh release create failed — the tarball is still at $TARBALL"
    ok "Published $TAG"
    gh release view "$TAG" --repo "$GH_REPO" --json url --jq .url 2>/dev/null | sed 's/^/    /'
fi

_ELAPSED=$(( $(date +%s) - _START_SECONDS ))
echo ""
printf "  Total time: %dm%02ds\n" $((_ELAPSED / 60)) $((_ELAPSED % 60))
echo ""
