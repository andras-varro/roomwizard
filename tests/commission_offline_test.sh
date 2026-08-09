#!/bin/bash
#
# commission_offline_test.sh — regression for commissioning/commission-offline.sh's VERIFY pass
#
# Host-only. No SD card and no device: the card is the synthetic tree from
# tests/make-fake-card.sh, mounted nowhere and handed over with --base.
#
#   wsl.exe -u root -e bash -lc "cd /mnt/c/work/roomwizard && tests/commission_offline_test.sh"
#
# Needs root (or passwordless sudo), because commissioning/card-prep.sh writes
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
# Section 4 is the one exception and is the mirror image: the p1 skip paths must
# exit ZERO and still say which flag skipped them, so they use expect_says rather
# than expect_fires. Section 0 is neither — it is the structural check on the
# fixture tree itself.
#
# The sabotages run against COPIES — a copy of the bundle, and a copy of just the
# scripts commissioning/commission-offline.sh reads (not the repo, which carries 4 GB of card
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
    echo -e "  ${YELLOW}skip${NC}  needs root — commissioning/card-prep.sh writes through sudo"
    exit 0
fi
if [ ! -d "$BUNDLE_SRC" ]; then
    echo -e "  ${RED}✗${NC} no staged bundle at $BUNDLE_SRC"
    echo "     build one:  ./release.sh --stage-only --component native_apps"
    exit 1
fi

TMP=$(mktemp -d /tmp/rw-comm-test.XXXXXX)
trap 'rm -rf "$TMP"' EXIT INT TERM

# The two answers plus commissioning/card-prep.sh's own three prompts.
ANSWERS='yes\nrwfake\nrwfake\nrwfake\nn\n'

# A copy of only what commissioning/commission-offline.sh reads. `cp -a` on device-files/ so the
# init scripts keep their bytes — that copy is also what brings roomwizard-app and
# disable-steelcase.sh, which the provision plan installs from there; nothing here
# needs the 4 GB card images.
REPO="$TMP/repo"
mkdir -p "$REPO/native_apps" "$REPO/commissioning" "$REPO/lib"
for f in commissioning/commission-offline.sh commissioning/card-prep.sh commissioning/set-hostname.sh \
         lib/rw-identify.sh lib/rw-clean.sh lib/rw-provision.sh lib/rw-bundle.sh \
         lib/rw-usbpower.sh lib/rw-ssh.sh \
         COMMISSIONING.md; do
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
    OUT=$(printf "$ANSWERS" | bash "$repo/commissioning/commission-offline.sh" \
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

# expect_says <regex> <description>   — the run must have SUCCEEDED, and said it.
#
# The mirror of expect_fires, for the skip paths. Both halves are load-bearing and
# they are different bugs: a skip that exits non-zero turns a deliberate opt-out
# into a failed commissioning, and a skip that goes unmentioned leaves the operator
# believing a step ran. Asserting only the exit code would pass a silent skip.
expect_says() {
    if [ "$ST" -ne 0 ]; then
        bad "$2 — the tool exited $ST, but a deliberate skip must still succeed"
        printf '%s\n' "$OUT" | grep -E '✗|FAIL' | sed 's/^/        /' | head -5
    elif printf '%s\n' "$OUT" | grep -qE "$1"; then
        ok "$2"
    else
        bad "$2 — exited 0 but no line matched /$1/"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "0. the fixture tree covers everything the tool sources"
# ═══════════════════════════════════════════════════════════════════════════
#
# The negative control for the copy list above, and the case that would have caught
# lib/rw-usbpower.sh's omission from it. That omission was invisible for a whole
# session because the `.` of it is LAZY — it sits inside phase 6's else branch, and
# every case in this file passes --base, which skips p1 before the source line is
# reached. So a suite that only ever runs --base cannot discover a missing library
# by running; it has to look.
#
# Reads the COPIED scripts, not the repo's, so the thing asserted is the tree the
# cases below actually execute.
#
# ⚠️ Scans EVERY script in the fixture, not just commission-offline.sh. The first
# version of this check read that one file, and thereby missed lib/rw-ssh.sh —
# sourced by commissioning/card-prep.sh, which commission-offline.sh hands over to
# in phase 3. Every case downstream of phase 3 died on it. A negative control for a
# copy list has to cover every script the list is FOR.
#
# The pattern keys on the "/lib/" path component rather than on $REPO_ROOT, so a
# caller using $SCRIPT_DIR/../lib is covered too.
SRC_LINES=$(grep -hoE '\.[[:space:]]+"[^"]*/lib/[^"]+"' \
                "$REPO"/commissioning/*.sh "$REPO"/lib/*.sh \
            | sed 's|.*/\(lib/[^"]*\)"|\1|' | sort -u)
SRC_N=$(printf '%s\n' "$SRC_LINES" | grep -c . )
# Ask which part of the count is the harness: a grep whose pattern has rotted
# matches nothing and every per-file case below then passes over an empty list.
if [ "$SRC_N" -ge 6 ]; then
    ok "0a the source-line grep found $SRC_N libraries (>= 6)"
else
    bad "0a the source-line grep found only $SRC_N libraries — the pattern has rotted"
fi
while read -r f; do
    [ -n "$f" ] || continue
    if [ -f "$REPO/$f" ]; then
        ok "0b sourced by a fixture script and present in the fixture: $f"
    else
        bad "0b sourced by a fixture script but MISSING from the fixture: $f"
    fi
done <<< "$SRC_LINES"

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
# The usb group is ON by default, so its two links must be among the ones checked.
# Named explicitly because the generic 'boot links resolve' above still matches when
# they are silently left out — which is how the gap survived (IMPROVEMENT_PLAN.md F15).
if printf '%s\n' "$OUT" | grep -qE 'boot links resolve .*S89.*S90'; then
    ok "1c the default run checks the usb group's boot links too (S89, S90)"
else
    bad "1c the default run checks the usb group's boot links too (S89, S90)"
fi

# Kept for section 4: a default --base run is exactly the p1 '--base' skip case, so
# it is asserted against THIS run rather than paying for a second identical one.
HAPPY_OUT="$OUT"; HAPPY_ST="$ST"

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
    "$R/commissioning/commission-offline.sh"
grep -q 'no chmod — deliberately broken' "$R/commissioning/commission-offline.sh" \
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
OUT=$(printf "$ANSWERS" | OBJDUMP=definitely-no-such-objdump bash "$REPO/commissioning/commission-offline.sh" \
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
OUT=$(printf "$ANSWERS" | OBJDUMP=definitely-no-such-objdump bash "$REPO/commissioning/commission-offline.sh" \
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
OUT=$(printf "$ANSWERS" | bash "$REPO/commissioning/commission-offline.sh" \
          --bundle "$BUNDLE" --base "$TMP/card" 2>&1); ST=$?
set -e
expect_fires 'do not look right|does not look like' "3a the four mounts in the wrong order are refused"

# A base that is not a card at all.
mkdir -p "$TMP/notacard"
set +e
OUT=$(printf "$ANSWERS" | bash "$REPO/commissioning/commission-offline.sh" \
          --bundle "$BUNDLE" --base "$TMP/notacard" 2>&1); ST=$?
set -e
expect_fires 'do not look right|does not look like' "3b a directory that is not a card is refused"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "4. p1: the three ways the power patch is skipped"
# ═══════════════════════════════════════════════════════════════════════════
#
# ⚠️ What this section does NOT cover: the p1 WRITE. Reaching the
# gate/backup/patch/verify/rollback sequence needs --disk — a real card or a
# loopback image with a vfat p1 — and this whole file is built on --base, which
# hands the tool four mount points and no disk. tests/rw_usbpower_test.sh (94 cases)
# and tests/measure_usbpower_sabotage.sh (five sabotages, all caught) own that
# sequence over both transports. What THIS file can own is which mode reaches it,
# i.e. that every non-writing path says so and still succeeds. Do not read the
# passes below as evidence that the write is covered here.
#
# Each string is asserted in TWO places on purpose: the per-file verify line and the
# closing summary. A verdict that reaches the detail block but not the summary is a
# skip the operator scrolls past, and the summary is the only part they are told to
# read.

# ── --no-usb-power: the driver is installed, only the budget is left alone.
run "$BUNDLE" "$REPO" --no-usb-power
expect_says 'p1: skipped \(--no-usb-power\)' \
    "4a --no-usb-power skips p1, succeeds, and the verify block says which flag did it"
expect_says 'USB power budget: skipped \(--no-usb-power\)' \
    "4b --no-usb-power is named in the closing summary too"

# ── --no-usb: the whole group goes, and it must IMPLY --no-usb-power. Patching p1
# for a unit with no /etc/init.d/usb-host would be a gratuitous least-reversible
# write. The distinct string is what proves the implication was taken rather than
# the plain --no-usb-power branch being reached by accident.
run "$BUNDLE" "$REPO" --no-usb
expect_says 'p1: skipped \(--no-usb\)' \
    "4c --no-usb implies --no-usb-power, and says so by its own name"
expect_says 'USB power budget: skipped \(--no-usb\)' \
    "4d --no-usb is named in the closing summary too"
# And the boot-link check must drop S89/S90 rather than fail over links nobody asked
# to install. This is the half of that check that could turn an opt-out into a
# verification failure, so it is asserted from the opt-out side.
#
# ⚠️ Asserted as "the line exists AND does not name S89", not as a match on the
# short list: commission-offline.sh's ok() appends ${NC}, so nothing can be anchored
# with `$`, and unanchored `boot links resolve \(S28, S29, S99\)` is a PREFIX of the
# default run's own message — it would pass whether or not the exclusion works.
_bl=$(printf '%s\n' "$OUT" | grep -E 'boot links resolve' | head -1)
if [ -n "$_bl" ] && ! printf '%s\n' "$_bl" | grep -q 'S89'; then
    ok "4e --no-usb drops S89/S90 from the boot-link check instead of failing on them"
else
    bad "4e --no-usb drops S89/S90 from the boot-link check instead of failing on them"
    printf '%s\n' "$OUT" | grep -E 'boot link' | sed 's/^/        /' | head -5
fi

# ── --base with no flags at all: the default path every other case in this file
# takes. ⚠️ This is reached even though the p1 patch is ON by default, because
# DO_USB_POWER is tested BEFORE -z "$MOUNTED_BASE" — so the flag cases above are
# genuinely distinct from this one and not all three the same branch.
OUT="$HAPPY_OUT"; ST="$HAPPY_ST"
expect_says 'p1: skipped \(--base: no disk given, so p1 cannot be located\)' \
    "4f --base alone skips p1 and says a card, not a mount point, is what it needs"
expect_says 'USB power budget: skipped \(--base' \
    "4g the --base skip is named in the closing summary too"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "════════════════════════════════════════"
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed, $TOTAL total"
if [ "$TOTAL" -lt 30 ]; then
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
