#!/bin/bash
#
# commission_offline_test.sh — regression for commission-offline.sh's VERIFY pass
#
# Host-only. No SD card and no device: the card is the synthetic tree from
# tests/make-fake-card.sh, mounted nowhere and handed over with --base.
#
#   wsl.exe -u root -e bash -lc "cd /mnt/c/work/roomwizard && tests/commission_offline_test.sh"
#
# Needs root (or passwordless sudo), because commission-roomwizard.sh writes
# through sudo. Needs a staged bundle: ./release.sh --stage-only [--component
# native_apps] leaves one in build/release.
#
# ── Why every case here is a SABOTAGE ──────────────────────────────────────
#
# The verify pass is eight checks that all report success on a good install, and a
# check that cannot fail is decoration. So each case breaks exactly one thing and
# asserts that (a) the right check fires and (b) the tool exits non-zero. The
# happy path is case 1 and is the control for all of them.
#
# The sabotages run against COPIES — a copy of the bundle, and a copy of just the
# scripts commission-offline.sh reads (not the repo, which carries 4 GB of card
# images). SCRIPT_DIR is derived from the script's own location, so a copied tree
# is a genuinely different installation.
#
# ⚠️ One thing is NOT controlled here, deliberately: a binary that really does
# contain an sdiv. This host's compiler will not emit one for Cortex-A8, so there
# is no way to build the positive case without hand-assembling it —
# native_apps/check-arm-safe.sh carries that reasoning and SYSTEM_ANALYSIS.md#61
# the two ways to get a wrong answer out of the gate. What IS controlled is both
# ways the gate can lie by omission: a bundle with zero ARM binaries (2g) and a
# host with no arm objdump (2j, 2k), which is the failure F10 warns about by name.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUNDLE_SRC="$REPO_DIR/build/release"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
PASS=0; FAIL=0
ok()  { PASS=$((PASS + 1)); echo -e "  ${GREEN}pass${NC}  $1"; }
bad() { FAIL=$((FAIL + 1)); echo -e "  ${RED}FAIL${NC}  $1"; }

if [ "$(id -u)" -ne 0 ]; then
    echo -e "  ${YELLOW}skip${NC}  needs root — commission-roomwizard.sh writes through sudo"
    exit 0
fi
if [ ! -d "$BUNDLE_SRC" ]; then
    echo -e "  ${RED}✗${NC} no staged bundle at $BUNDLE_SRC"
    echo "     build one:  ./release.sh --stage-only --component native_apps"
    exit 1
fi

TMP=$(mktemp -d /tmp/rw-comm-test.XXXXXX)
trap 'rm -rf "$TMP"' EXIT INT TERM

# The two answers plus commission-roomwizard.sh's own three prompts.
ANSWERS='yes\nrwfake\nrwfake\nrwfake\nn\n'

# A copy of only what commission-offline.sh reads. `cp -a` on device-files/ so the
# init scripts keep their bytes; nothing here needs the 4 GB card images.
REPO="$TMP/repo"
mkdir -p "$REPO/native_apps"
for f in commission-offline.sh commission-roomwizard.sh set-hostname.sh \
         rw-identify.sh rw-clean.sh rw-bundle.sh \
         roomwizard-app-init.sh disable-steelcase.sh COMMISSIONING.md; do
    cp "$REPO_DIR/$f" "$REPO/$f"
done
cp -a "$REPO_DIR/device-files" "$REPO/device-files"
cp "$REPO_DIR/native_apps/check-arm-safe.sh" "$REPO/native_apps/"

# run <bundle-dir> <repo-dir> [extra args...]  -> stdout+stderr in $OUT, status in $ST
OUT=""; ST=0
run() {
    local bundle="$1" repo="$2"; shift 2
    bash "$SCRIPT_DIR/make-fake-card.sh" "$TMP/card" >/dev/null || return 1
    set +e
    OUT=$(printf "$ANSWERS" | bash "$repo/commission-offline.sh" \
              --bundle "$bundle" --base "$TMP/card" "$@" 2>&1)
    ST=$?
    set -e
}

# expect_fires <regex> <description>   — the run must have failed, with that message
expect_fires() {
    if [ "$ST" -eq 0 ]; then
        bad "$2 — the tool exited 0"
    elif printf '%s\n' "$OUT" | grep -qE "$1"; then
        ok "$2"
    else
        bad "$2 — exited $ST but no line matched /$1/"
        printf '%s\n' "$OUT" | grep -E '✗|FAIL' | sed 's/^/        /' | head -5
    fi
}

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "1. the happy path — the control for every case below"
# ═══════════════════════════════════════════════════════════════════════════

BUNDLE="$TMP/bundle"
cp -a "$BUNDLE_SRC" "$BUNDLE"
run "$BUNDLE" "$REPO"
if [ "$ST" -eq 0 ]; then
    ok "1a a good bundle installs and verifies clean"
else
    bad "1a a good bundle installs and verifies clean (exit $ST)"
    printf '%s\n' "$OUT" | tail -20 | sed 's/^/        /'
fi
for want in 'md5: all' '\+x: all' '\.app: all' 'default-app:' 'n: all .* /bin/sh' \
            'boot links resolve' 'D7b closed'; do
    if printf '%s\n' "$OUT" | grep -qE "$want"; then
        ok "1b every verify check ran: /$want/"
    else
        bad "1b a verify check did not run: /$want/"
    fi
done

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "2. sabotage: one check each"
# ═══════════════════════════════════════════════════════════════════════════

# ── md5. Change a staged file's bytes and leave the manifest alone: this is a
# truncated or corrupted write, which is what a failing card looks like.
B="$TMP/b-md5"; cp -a "$BUNDLE_SRC" "$B"
printf 'corrupted\n' >> "$B/root/opt/roomwizard/apps/snake.app"
run "$B" "$REPO"
expect_fires 'md5 mismatch' "2a a corrupted staged file is caught by md5"

# ── the executable bit. The plausible defect: an installer that trusts the
# archive's modes instead of the DECLARED ones. On /mnt/c that bug is invisible
# (DrvFs reports 0777 and discards chmod); on real ext4 the check is a
# measurement, which is the whole reason it is in the offline tool and not here.
B="$TMP/b-nochmod"; cp -a "$BUNDLE_SRC" "$B"
R="$TMP/repo-nochmod"; cp -a "$REPO" "$R"
sed -i 's/^    chmod "\$mode" "\$dest"$/    : "no chmod — deliberately broken copy"/' \
    "$R/commission-offline.sh"
grep -q 'no chmod — deliberately broken' "$R/commission-offline.sh" \
    || bad "2b harness: the chmod sabotage did not apply"
# The bundle's staged files must not already be executable, or the check would
# pass for the wrong reason — cp -a from /mnt/c carries 0777.
find "$B/root" -type f -exec chmod 0644 {} +
run "$B" "$R"
expect_fires 'declared mode .* not executable' "2b an installer that skips chmod is caught by the +x check"

# ── .app exec=. A manifest naming a binary that is not there renders a launcher
# tile that does nothing when tapped, which looks exactly like broken touch.
# The .md5 line is regenerated so that ONLY this check fires.
B="$TMP/b-app"; cp -a "$BUNDLE_SRC" "$B"
sed -i 's|^exec=.*|exec=/opt/games/no_such_game|' "$B/root/opt/roomwizard/apps/snake.app"
newmd5=$(md5sum "$B/root/opt/roomwizard/apps/snake.app" | cut -d' ' -f1)
sed -i "s|^[0-9a-f]*  /opt/roomwizard/apps/snake.app$|$newmd5  /opt/roomwizard/apps/snake.app|" \
    "$B/manifest.d/native_apps.md5"
run "$B" "$REPO"
expect_fires 'exec=/opt/games/no_such_game is not installed' \
    "2c a manifest naming an absent binary is caught"

# ── default-app. /etc/init.d/roomwizard-app respawns whatever this names, so a
# wrong value is a device that boots to a black screen and stays there.
B="$TMP/b-default"; cp -a "$BUNDLE_SRC" "$B"
printf '/opt/roomwizard/not_a_launcher\n' > "$B/root/opt/roomwizard/default-app"
newmd5=$(md5sum "$B/root/opt/roomwizard/default-app" | cut -d' ' -f1)
sed -i "s|^[0-9a-f]*  /opt/roomwizard/default-app$|$newmd5  /opt/roomwizard/default-app|" \
    "$B/manifest.d/native_apps.md5"
run "$B" "$REPO"
expect_fires "default-app is '/opt/roomwizard/not_a_launcher'" \
    "2d a default-app that names nothing installed is caught"

# ── dash -n. A parse error in an init script does not fail at install time; it
# fails at boot, on a device with no serial console.
#
# ⚠️ What `dash -n` does NOT catch is a BASHISM. `[[ -n "$x" ]]` parses fine —
# dash reads `[[` as a command name — so it passes the check and then fails at
# boot with "[[: not found". Measured while writing this case, which is why the
# sabotage below is a real syntax error instead. Catching bashisms needs
# shellcheck, which is not installed in this WSL (IMPROVEMENT_PLAN.md C7).
R="$TMP/repo-parse"; cp -a "$REPO" "$R"
printf 'if [ -n "$server" ]; then\n  echo unterminated\n' >> "$R/device-files/time-sync"
run "$BUNDLE" "$R"
expect_fires '\-n failed' "2e a parse error in a /bin/sh init script is caught"

# ── CRLF. BusyBox rejects `#!/bin/sh\r` with a misleading "no such file or
# directory" and the device boots to a black screen with no obvious cause. This
# is why .gitattributes pins device-files/** to eol=lf.
R="$TMP/repo-crlf"; cp -a "$REPO" "$R"
sed -i 's/$/\r/' "$R/device-files/audio-enable"
run "$BUNDLE" "$R"
expect_fires 'CRLF shebang' "2f a CRLF shebang is caught"

# ── the ARM gate's zero-artifact case. "no hardware divide in 0 binaries" is a
# pass over nothing, and it is the failure the plan warns about by name.
B="$TMP/b-noelf"; mkdir -p "$B/root/opt/roomwizard/apps" "$B/manifest.d"
printf 'name=X\nexec=/opt/games/x\nicon=\nargs=\n' > "$B/root/opt/roomwizard/apps/x.app"
printf '0644 /opt/roomwizard/apps/x.app\n' > "$B/manifest.d/only.list"
printf '%s  /opt/roomwizard/apps/x.app\n' \
    "$(md5sum "$B/root/opt/roomwizard/apps/x.app" | cut -d' ' -f1)" > "$B/manifest.d/only.md5"
run "$B" "$REPO"
expect_fires 'no ELF binaries' "2g a bundle with no ARM binaries is refused, not silently passed"

# ── the ARM gate when the toolchain is absent. THE case F10 spells out: it must
# say so loudly rather than report a pass over zero artifacts. Simulated through
# the OBJDUMP override, because uninstalling binutils to test this is absurd.
set +e
OUT=$(printf "$ANSWERS" | OBJDUMP=definitely-no-such-objdump bash "$REPO/commission-offline.sh" \
          --bundle "$BUNDLE" --base "$TMP/card" 2>&1); ST=$?
set -e
if [ "$ST" -ne 0 ] && printf '%s\n' "$OUT" | grep -q 'IS NOT INSTALLED' \
   && printf '%s\n' "$OUT" | grep -q 'refusing to install unverified'; then
    ok "2j a missing arm objdump refuses loudly, naming the count it did not check"
else
    bad "2j a missing arm objdump refuses loudly (exit $ST)"
fi

bash "$SCRIPT_DIR/make-fake-card.sh" "$TMP/card" >/dev/null
set +e
OUT=$(printf "$ANSWERS" | OBJDUMP=definitely-no-such-objdump bash "$REPO/commission-offline.sh" \
          --bundle "$BUNDLE" --base "$TMP/card" --arm-check=skip 2>&1); ST=$?
set -e
if [ "$ST" -eq 0 ] && printf '%s\n' "$OUT" | grep -q 'NOT CHECKED'; then
    ok "2k --arm-check=skip proceeds, and the summary still says NOT CHECKED"
else
    bad "2k --arm-check=skip proceeds, and the summary still says NOT CHECKED (exit $ST)"
fi

# ── the bundle's own structural check, in both directions.
B="$TMP/b-unstaged"; cp -a "$BUNDLE_SRC" "$B"
rm -f "$B/root/opt/games/snake"
run "$B" "$REPO"
expect_fires 'not self-consistent' "2h a manifest entry with no staged file is refused"

# ── --no-clean must SAY that it leaves D7b open rather than quietly doing so.
run "$BUNDLE" "$REPO" --no-clean
if [ "$ST" -eq 0 ] && printf '%s\n' "$OUT" | grep -q 'websign in place'; then
    ok "2i --no-clean warns that the host name will be overwritten (D7b)"
else
    bad "2i --no-clean warns that the host name will be overwritten (D7b)"
fi

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "3. the card itself"
# ═══════════════════════════════════════════════════════════════════════════

# Wrong partition order: p2 mounted where p6 was expected. The one mistake that
# makes every later path resolve under the wrong tree.
bash "$SCRIPT_DIR/make-fake-card.sh" "$TMP/card" >/dev/null
mv "$TMP/card/root" "$TMP/card/.r"; mv "$TMP/card/data" "$TMP/card/root"; mv "$TMP/card/.r" "$TMP/card/data"
set +e
OUT=$(printf "$ANSWERS" | bash "$REPO/commission-offline.sh" \
          --bundle "$BUNDLE" --base "$TMP/card" 2>&1); ST=$?
set -e
expect_fires 'do not look right|does not look like' "3a the four mounts in the wrong order are refused"

# A base that is not a card at all.
mkdir -p "$TMP/notacard"
set +e
OUT=$(printf "$ANSWERS" | bash "$REPO/commission-offline.sh" \
          --bundle "$BUNDLE" --base "$TMP/notacard" 2>&1); ST=$?
set -e
expect_fires 'do not look right|does not look like' "3b a directory that is not a card is refused"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "════════════════════════════════════════"
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed, $TOTAL total"
if [ "$TOTAL" -lt 18 ]; then
    echo -e "  ${RED}✗ only $TOTAL cases ran — the harness itself is broken${NC}"
    exit 1
fi
if [ "$FAIL" -gt 0 ]; then
    echo -e "  ${RED}✗ $FAIL failure(s)${NC}"
    exit 1
fi
echo -e "  ${GREEN}✓ all $TOTAL cases passed${NC}"
echo ""
exit 0
