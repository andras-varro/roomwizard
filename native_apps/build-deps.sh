#!/bin/bash
# Cross-build native_apps' ARM dependencies into arm-deps/.
#
# Today that is exactly one library: **tinyalsa**, for the native-ALSA audio
# backend (../IMPROVEMENT_PLAN.md F1).  The OSS /dev/dsp shim it replaces needs no
# library at all, so this script is new with F1 Phase 1 and has one consumer for
# now — but two are planned: native_apps' own common/audio_dev.c (Phase 4) and the
# ScummVM backend's alsa-mixer.cpp (Phase 5).  ScummVM already reaches into
# native_apps/common/ for framebuffer.o and touch_input.o, so it will reach here
# the same way rather than building a second copy.  ONE tinyalsa, one pinned
# version, one LICENSE.md row.
#
# Usage:
#   ./build-deps.sh          # build anything missing (idempotent; skips fast)
#   ./build-deps.sh --force  # rebuild from scratch
#
# ../native_apps/build-and-deploy.sh calls this itself, guarded on the artifact,
# so a fresh clone deploys without a manual prerequisite step — the rule ScummVM's
# build_arm_deps() already follows.  Running it by hand is only for iterating here.
#
# WSL only: needs arm-linux-gnueabihf-gcc, which does not exist in Git Bash.
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard/native_apps && ./build-deps.sh"

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

DEPS_PREFIX="$SCRIPT_DIR/arm-deps"
SRC_DIR="$DEPS_PREFIX/src"

# ── pinned versions ─────────────────────────────────────────────────────────
# Pinned, not "latest", because "current upstream" is not a fact anyone can check
# later — the same reason LICENSE.md records versions.  v2.0.0 was the newest tag
# on 2026-08-15 (measured against the GitHub tags API: v2.0.0, v1.0.2, v1.0.0,
# 1.1.1, 1.1.0).
TINYALSA_VERSION="2.0.0"
TINYALSA_URL="https://github.com/tinyalsa/tinyalsa/archive/refs/tags/v${TINYALSA_VERSION}.tar.gz"

CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"
CC="${CROSS_COMPILE}gcc"
AR="${CROSS_COMPILE}ar"
NM="${CROSS_COMPILE}nm"

# The same warning flags every native_apps target uses.  Measured 2026-08-15:
# these five sources compile at ZERO warnings, so the tree's zero-warning baseline
# survives adding a third-party dependency.  Not -Werror, for the reason
# build-and-deploy.sh gives: a warning must not block a deploy.
WARN="-Wall -Wextra"

# ⚠️ FIVE of upstream's EIGHT sources, deliberately.  Upstream's own src/Makefile
# and CMakeLists.txt compile all eight unconditionally, including the three plugin
# files (pcm_plugin.c, mixer_plugin.c, snd_card_plugin.c).  We must not, and the
# reason is not tidiness — measured 2026-08-15:
#
#   - snd_card_plugin.c dlopen()s ("libsndcardparser.so", src/snd_card_plugin.c:105),
#     leaving dlopen/dlsym/dlclose as undefined externals in the archive.  Every
#     native_apps binary is -static, and static glibc + dlopen is precisely the
#     class of landmine that already cost this project a SIGSEGV-before-main
#     (../SYSTEM_ANALYSIS.md#6-building-for-this-device).  All of it is dead code:
#     the plugin path is behind #ifdef TINYALSA_USES_PLUGINS, which we never define.
#   - the three plugin files also emit five -Wstringop-truncation warnings, against
#     a tree whose baseline is zero.
#
# Dropping them costs one upstream bug, patched below.  assert_no_dl() is the
# negative control: re-add a plugin file and the build refuses instead of shipping.
TINYALSA_SRCS="limits mixer mixer_hw pcm pcm_hw"

TINYALSA_HDRS="asoundlib.h attributes.h interval.h limits.h mixer.h pcm.h plugin.h version.h"

# ── colour helpers (same shapes as build-and-deploy.sh) ─────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "[$(date '+%H:%M:%S')] ${GREEN}  ✓ $*${NC}"; }
info() { echo -e "[$(date '+%H:%M:%S')] ${YELLOW}  → $*${NC}"; }
warn() { echo -e "[$(date '+%H:%M:%S')] ${BLUE}  ! $*${NC}"; }
err()  { echo -e "[$(date '+%H:%M:%S')] ${RED}  ✗ $*${NC}"; exit 1; }

FORCE=0
case "${1:-}" in
    "")        ;;
    --force)   FORCE=1 ;;
    *)         echo "Usage: $0 [--force]"; exit 1 ;;
esac

# ── the one-line upstream fix ───────────────────────────────────────────────
# tinyalsa 2.0.0's pcm_close() calls snd_utils_close_dev_node() OUTSIDE the
# #ifdef TINYALSA_USES_PLUGINS that guards its four sibling call sites, so the
# five-file set fails to link on that one symbol.  Still ungated on upstream
# master (checked 2026-08-15) — it never bites upstream because their build always
# compiles snd_card_plugin.c.
#
# Gating it is behaviour-identical, not a behaviour change, and that is measured
# rather than assumed: struct pcm is calloc()'d (src/pcm.c:1046) and pcm->snd_node
# is assigned ONLY inside that same #ifdef, so with plugins off the argument is
# always NULL — and upstream's own implementation returns immediately on NULL
# (src/snd_card_plugin.c:60-63).
#
# The anchor is the FOUR-space indentation, which is unique: the other textually
# identical call site is indented eight, sitting under an `if (pcm->snd_node)`
# inside the #ifdef.  Both counts are asserted, because a sed that silently fails
# to apply is a scar this repo already carries.
patch_tinyalsa() {
    local f="src/pcm.c"
    local before after
    before=$(grep -c '^    snd_utils_close_dev_node(pcm->snd_node);$' "$f" || true)
    [ "$before" -eq 1 ] || err "tinyalsa $TINYALSA_VERSION: expected exactly 1 ungated snd_utils_close_dev_node(), found $before — the pin moved or the file changed; re-read src/pcm.c before touching this"

    sed -i 's|^    snd_utils_close_dev_node(pcm->snd_node);$|#ifdef TINYALSA_USES_PLUGINS\n    snd_utils_close_dev_node(pcm->snd_node);\n#endif|' "$f"

    # ⚠️ The post-condition is NOT "the line is gone" — the patch keeps the call and
    # its four-space indentation, it only wraps it.  An earlier version of this
    # function asserted the count had dropped to 0 and refused a correctly patched
    # tree on the first run.  What must be true is that no occurrence is left
    # UNGUARDED, so ask about the preceding line.
    after=$(awk '
        /^    snd_utils_close_dev_node\(pcm->snd_node\);$/ {
            if (prev != "#ifdef TINYALSA_USES_PLUGINS") n++
        }
        { prev = $0 }
        END { print n + 0 }
    ' "$f")
    [ "$after" -eq 0 ] || err "tinyalsa patch did not apply ($after call site(s) still outside #ifdef TINYALSA_USES_PLUGINS)"
    ok "patched pcm_close()'s ungated snd_utils_close_dev_node() (1 line)"
}

# ── negative control on the file set ────────────────────────────────────────
# The archive must not reference the dynamic loader.  This is the check that makes
# the five-file set self-enforcing: adding snd_card_plugin.c back — by following
# upstream's Makefile, which is the obvious thing to do — fails here loudly rather
# than putting dlopen into sixteen -static binaries.
assert_no_dl() {
    local a="$1" found
    found=$("$NM" -u "$a" 2>/dev/null | grep -oE '\b(dlopen|dlsym|dlclose)\b' | sort -u | tr '\n' ' ' || true)
    if [ -n "$found" ]; then
        err "$(basename "$a") references the dynamic loader ($found) — a plugin source crept into TINYALSA_SRCS; see the comment above it"
    fi
    ok "no dlopen/dlsym/dlclose in $(basename "$a") — safe to link -static"
}

# ── the other half of that control: does it actually LINK? ──────────────────
# assert_no_dl() catches a symbol we do not want; this catches one we are MISSING.
# Dropping the three plugin files is what exposes pcm_close()'s ungated
# snd_utils_close_dev_node(), and the only thing that noticed was a failed link —
# `ar` is happy to archive objects with unresolved externals, and both the
# ARM-safety gate and `nm -u` pass on an archive nothing can link against.  So the
# subset decision is proven from both sides, at dep-build time, instead of
# surfacing as a mystery in F1 Phase 4's first build.
assert_links() {
    local a="$1" inc="$2" tmp
    tmp="$(mktemp -d)"
    cat > "$tmp/linktest.c" <<'LINKTEST'
#include <string.h>
#include <tinyalsa/asoundlib.h>
/* Drives the surface common/audio_dev.c will use: open, negotiate, write, close. */
int main(void)
{
    struct pcm_config cfg;
    struct pcm *p;
    short buf[64];
    int written;

    memset(&cfg, 0, sizeof cfg);
    cfg.channels = 2;
    cfg.rate = 48000;
    cfg.period_size = 1024;
    cfg.period_count = 4;
    cfg.format = PCM_FORMAT_S16_LE;

    p = pcm_open(0, 0, PCM_OUT, &cfg);
    if (!pcm_is_ready(p)) {
        pcm_close(p);
        return 1;
    }
    (void)pcm_get_rate(p);
    (void)pcm_get_channels(p);
    (void)pcm_frames_to_bytes(p, 1024);
    memset(buf, 0, sizeof buf);
    /* pcm_writei is warn_unused_result; a (void) cast does NOT satisfy that, so
       assign it — otherwise this test adds a warning to a zero-warning tree. */
    written = pcm_writei(p, buf, 32);
    (void)written;
    pcm_close(p);
    return 0;
}
LINKTEST
    if ! $CC $WARN -O2 -static -I"$inc" "$tmp/linktest.c" "$a" -o "$tmp/linktest" 2>"$tmp/err"; then
        echo "" >&2
        sed 's/^/      /' "$tmp/err" >&2
        rm -rf "$tmp"
        err "libtinyalsa.a does not link -static — see above (a missing plugin-file symbol looks exactly like this)"
    fi
    rm -rf "$tmp"
    ok "links -static against a pcm_open/writei/close consumer"
}

download_tinyalsa() {
    local tarball="$SRC_DIR/tinyalsa-${TINYALSA_VERSION}.tar.gz"
    mkdir -p "$SRC_DIR"
    if [ -f "$tarball" ]; then
        info "tarball already downloaded: $(basename "$tarball")"
    else
        info "downloading tinyalsa ${TINYALSA_VERSION}..."
        wget -q -O "$tarball" "$TINYALSA_URL" || {
            rm -f "$tarball"
            err "failed to download $TINYALSA_URL"
        }
    fi
    rm -rf "$SRC_DIR/tinyalsa-${TINYALSA_VERSION}"
    tar -xzf "$tarball" -C "$SRC_DIR" || err "failed to extract $(basename "$tarball")"
    [ -d "$SRC_DIR/tinyalsa-${TINYALSA_VERSION}" ] || err "tarball did not extract to tinyalsa-${TINYALSA_VERSION}/"
}

build_tinyalsa() {
    if [ "$FORCE" -eq 0 ] && [ -f "$DEPS_PREFIX/lib/libtinyalsa.a" ]; then
        ok "tinyalsa already built (arm-deps/lib/libtinyalsa.a exists)"
        return 0
    fi

    info "cross-building tinyalsa ${TINYALSA_VERSION} for ARM..."
    download_tinyalsa

    local build_dir="$SRC_DIR/tinyalsa-${TINYALSA_VERSION}"
    cd "$build_dir"

    patch_tinyalsa

    local objs=""
    for s in $TINYALSA_SRCS; do
        [ -f "src/$s.c" ] || err "tinyalsa ${TINYALSA_VERSION} has no src/$s.c — the pin moved; re-read src/Makefile"
        $CC $WARN -O2 -Iinclude -c "src/$s.c" -o "$s.o" || err "failed to compile tinyalsa src/$s.c"
        objs="$objs $s.o"
    done

    # No -march / -mcpu on purpose.  The bare -O2 path is already Cortex-A8 safe;
    # what would break it is an -march implying the idiv extension
    # (../SYSTEM_ANALYSIS.md#61-cortex-a8-has-no-hardware-integer-divide).  The
    # gate below is what proves it rather than the flag list.
    rm -f libtinyalsa.a
    # shellcheck disable=SC2086
    $AR rcs libtinyalsa.a $objs || err "failed to archive libtinyalsa.a"

    assert_no_dl "$build_dir/libtinyalsa.a"
    assert_links "$build_dir/libtinyalsa.a" "$build_dir/include"

    # ⚠️ Gate the ARCHIVE, and gate it HERE.  Phase 1 ships no consumer, so
    # check-arm-safe.sh over build/ would see none of this code; and once there is
    # a consumer, an sdiv inside a static library is a SIGILL with a blank screen
    # like any other.  Measured 2026-08-15: the gate DOES disassemble every archive
    # member — a poisoned object (compiled -march=armv7ve, one real sdiv) is caught
    # as both the first and the last member.  Its `checked=1` counts FILES, not
    # members, so do not read that 1 as "one of six things was looked at".
    if [ -x "$SCRIPT_DIR/check-arm-safe.sh" ]; then
        info "ARM-safety gate on libtinyalsa.a..."
        ( cd "$SCRIPT_DIR" && ./check-arm-safe.sh "$build_dir/libtinyalsa.a" ) \
            || err "libtinyalsa.a contains a hardware divide — it would SIGILL on Cortex-A8"
    else
        warn "check-arm-safe.sh missing — skipping hardware-divide gate on libtinyalsa.a"
    fi

    mkdir -p "$DEPS_PREFIX/lib" "$DEPS_PREFIX/include/tinyalsa"
    cp libtinyalsa.a "$DEPS_PREFIX/lib/" || err "failed to install libtinyalsa.a"
    for h in $TINYALSA_HDRS; do
        [ -f "include/tinyalsa/$h" ] || err "tinyalsa ${TINYALSA_VERSION} has no include/tinyalsa/$h — the pin moved"
        cp "include/tinyalsa/$h" "$DEPS_PREFIX/include/tinyalsa/" || err "failed to install $h"
    done

    cd "$SCRIPT_DIR"
    ok "tinyalsa ${TINYALSA_VERSION} installed to arm-deps/ ($(du -h "$DEPS_PREFIX/lib/libtinyalsa.a" | cut -f1))"
}

echo ""
echo "════════════════════════════════════════"
echo " native_apps ARM dependencies"
echo "════════════════════════════════════════"

command -v "$CC" >/dev/null 2>&1 || err "$CC not found. Install with:\n  sudo apt-get install gcc-arm-linux-gnueabihf"
command -v wget >/dev/null 2>&1 || err "wget not found (present in WSL, absent in Git Bash — check which shell this is)"

build_tinyalsa

echo ""
ok "ARM dependencies ready at arm-deps/"
ls -l "$DEPS_PREFIX/lib/"*.a
echo ""
