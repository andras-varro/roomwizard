#!/bin/bash
#
# rw_bundle_ssh_test.sh — regression for rw_bundle_install_ssh, the SSH bundle
#                         installer (IMPROVEMENT_PLAN.md C12, F9).
#
# Host-only, no device, no SD card, no root. Run it:
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/rw_bundle_ssh_test.sh"
#
# ── How a thing that only exists over SSH is tested without SSH ─────────────
#
# $RW_SSH replaces the ssh command and $RW_BUNDLE_ROOT prefixes every device path,
# both empty in production. The fake RW_SSH here drops its first argument (the
# target) and runs the rest through `sh -c` locally, so the REAL function runs — its
# real tar pipeline, its real chmod loop, its real md5 comparison — against a
# directory on this host.
#
# The alternative was "it worked on a device once", which is not a test. It is also
# not available: the unit at 192.168.50.225 answers but accepts neither of this
# host's keys non-interactively, so the on-device path could not be exercised even
# once at the time this was written.
#
# ⚠️ What this therefore does NOT prove: that ssh itself is invoked correctly (the
# quoting of the remote command as ssh passes it to the remote shell), or anything
# about BusyBox's tar, xargs or md5sum as opposed to GNU's. The remote side here is
# this host's /bin/sh. Those remain untested until someone runs it at a device.
#
# ── Modes ──────────────────────────────────────────────────────────────────
#
# The fixture is built under $(mktemp -d) — WSL's own filesystem, which honours
# chmod. On /mnt/c it would not, and the +x case would pass for the wrong reason.
#
# ── Measured against deliberately broken copies ────────────────────────────
#
# Re-run with tests/measure_bundle_ssh_sabotage.sh, which asserts each patch APPLIED
# before reporting a count. Out of 23, measured 2026-08-06:
#
#   the chmod loop dropped, i.e. modes taken from the tar        9 fail
#   rw_bundle_check skipped                                     3 fail
#   the +x check removed                                        2 fail
#   a failed transfer no longer fatal                           2 fail
#   md5 mismatch no longer reported                             1 fail
#   the owner digit read from the end instead of by position     1 fail
#
# ⚠️ Two of those reported ZERO at first. The transfer one was a bad sabotage —
# it appended `; true` to an echo while the `return 1` on the next line still ran.
# The owner-digit one was a real gap: D1/D2 assert that an 0700 entry installs
# cleanly, which is true under both readings, and even after D3 was added it stayed
# green because app_launcher was still 0755 and got caught either way. It needed
# EVERY executable entry at 0700. Both are the same lesson as the first four
# sabotages, which all reported 22/0 because none of the patterns had matched at all.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=../rw-bundle.sh
. "$REPO_DIR/rw-bundle.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
PASS=0; FAIL=0
ok()  { PASS=$((PASS + 1)); echo -e "  ${GREEN}pass${NC}  $1"; }
bad() { FAIL=$((FAIL + 1)); echo -e "  ${RED}FAIL${NC}  $1"; }
assert_eq() { if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 (want '$1', got '$2')"; fi; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# The fake ssh: drop the target, run the command here.
FAKE="$TMP/fake-ssh"
cat > "$FAKE" <<'FAKESSH'
#!/bin/sh
shift                # the target
exec sh -c "$*"
FAKESSH
chmod +x "$FAKE"

DEVROOT="$TMP/device"
mkdir -p "$DEVROOT"
export RW_SSH="$FAKE"
export RW_BUNDLE_ROOT="$DEVROOT"

# ── the bundle ─────────────────────────────────────────────────────────────
BUNDLE="$TMP/bundle"
build_bundle() {
    rm -rf "$BUNDLE"
    mkdir -p "$TMP/src"
    printf '#!/bin/sh\necho launcher\n'   > "$TMP/src/app_launcher"
    printf '#!/bin/sh\necho snake\n'      > "$TMP/src/snake"
    printf 'name=Snake\nexec=/opt/games/snake\n' > "$TMP/src/snake.app"
    printf 'P6\n1 1\n255\n'               > "$TMP/src/snake.ppm"
    rw_bundle_init "$BUNDLE" native_apps
    rw_bundle_add "$BUNDLE" native_apps 0755 "$TMP/src/app_launcher" /opt/roomwizard/app_launcher
    rw_bundle_add "$BUNDLE" native_apps 0755 "$TMP/src/snake"        /opt/games/snake
    rw_bundle_add "$BUNDLE" native_apps 0644 "$TMP/src/snake.app"    /opt/roomwizard/apps/snake.app
    rw_bundle_add "$BUNDLE" native_apps 0644 "$TMP/src/snake.ppm"    /opt/roomwizard/icons/snake.ppm
    rw_bundle_finish "$BUNDLE" native_apps >/dev/null
}

echo ""
echo "A. a clean install"
build_bundle
rm -rf "$DEVROOT"; mkdir -p "$DEVROOT"
OUT=$(rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?

assert_eq "0" "$ST" "A1 the install succeeds"
for f in opt/roomwizard/app_launcher opt/games/snake \
         opt/roomwizard/apps/snake.app opt/roomwizard/icons/snake.ppm; do
    if [ -f "$DEVROOT/$f" ]; then ok "A2.$(basename "$f") /$f arrived"; else bad "A2 /$f missing"; fi
done
assert_eq "$(md5sum < "$TMP/src/snake")" "$(md5sum < "$DEVROOT/opt/games/snake")" \
    "A3 the bytes are the bundle's"

# ⚠️ Modes come from the MANIFEST, not from the tar. The staging tree usually lives
# on /mnt/c, where every file reads 0777 and chmod is discarded, so a tar that
# preserved modes would carry a number nobody measured.
assert_eq "755" "$(stat -c %a "$DEVROOT/opt/games/snake")"              "A4 an 0755 entry is 755"
assert_eq "644" "$(stat -c %a "$DEVROOT/opt/roomwizard/apps/snake.app")" "A5 an 0644 entry is 644"
assert_eq "755" "$(stat -c %a "$DEVROOT/opt/roomwizard/app_launcher")"  "A6 the launcher is executable"

if printf '%s\n' "$OUT" | grep -q 'md5 verified'; then
    ok "A7 it reports md5 and +x verified"
else
    bad "A7 it reports md5 and +x verified — got: $(printf '%s' "$OUT" | tail -1)"
fi
if printf '%s\n' "$OUT" | grep -q '4 file(s) installed'; then
    ok "A8 and names the count"
else
    bad "A8 and names the count"
fi

echo ""
echo "B. it is idempotent"
OUT=$(rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?
assert_eq "0" "$ST" "B1 a second install succeeds"
assert_eq "755" "$(stat -c %a "$DEVROOT/opt/games/snake")" "B2 and the mode is still right"

echo ""
echo "C. the sabotages — each must FAIL the install, not pass it"

# C1: a corrupted payload. The md5 in the manifest no longer matches the staged
# file, which is what a truncated transfer looks like from the far end.
build_bundle
printf 'corrupted\n' > "$BUNDLE/root/opt/games/snake"
rm -rf "$DEVROOT"; mkdir -p "$DEVROOT"
OUT=$(rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?
if [ "$ST" -ne 0 ] && printf '%s\n' "$OUT" | grep -q 'md5 mismatch'; then
    ok "C1 a payload whose bytes do not match the manifest is caught by md5"
else
    bad "C1 a payload whose bytes do not match the manifest is caught by md5 (status $ST)"
fi

# C2: a manifest entry with no staged file — rw_bundle_check's first direction.
build_bundle
rm -f "$BUNDLE/root/opt/games/snake"
rm -rf "$DEVROOT"; mkdir -p "$DEVROOT"
OUT=$(rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?
if [ "$ST" -ne 0 ] && printf '%s\n' "$OUT" | grep -q 'not self-consistent'; then
    ok "C2 a manifest entry with no staged file is refused BEFORE anything is written"
else
    bad "C2 a manifest entry with no staged file is refused (status $ST)"
fi
if [ ! -f "$DEVROOT/opt/roomwizard/app_launcher" ]; then
    ok "C3 and nothing was written"
else
    bad "C3 and nothing was written — it wrote before checking"
fi

# C4: a file staged by hand that no manifest names — the direction that catches a
# file nothing will ever chmod.
build_bundle
mkdir -p "$BUNDLE/root/opt/games"
printf 'orphan\n' > "$BUNDLE/root/opt/games/orphan"
rm -rf "$DEVROOT"; mkdir -p "$DEVROOT"
OUT=$(rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?
if [ "$ST" -ne 0 ] && printf '%s\n' "$OUT" | grep -q 'staged but in no manifest'; then
    ok "C4 a staged file no manifest names is refused"
else
    bad "C4 a staged file no manifest names is refused (status $ST)"
fi

# C5: an empty bundle must not report success over zero files.
rm -rf "$BUNDLE"; mkdir -p "$BUNDLE/root" "$BUNDLE/manifest.d"
rm -rf "$DEVROOT"; mkdir -p "$DEVROOT"
OUT=$(rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?
if [ "$ST" -ne 0 ]; then
    ok "C5 a bundle naming no files at all is refused, not reported as 0 installed"
else
    bad "C5 a bundle naming no files at all is refused"
fi

# C6: the +x measurement. Declare 0755 and have the chmod not happen.
#
# The fake ssh is swapped for one that silently drops chmod, which is the shape of
# the bug the check exists for: an installer that trusts the archive's modes. On
# /mnt/c this cannot be demonstrated at all — chmod is discarded there, so the
# sabotage and the fix look identical.
build_bundle
NOCHMOD="$TMP/fake-ssh-nochmod"
cat > "$NOCHMOD" <<'FAKESSH'
#!/bin/sh
shift
case "$*" in
    *"sh -s"*) cat >/dev/null; exit 0 ;;   # swallow the chmod script
esac
exec sh -c "$*"
FAKESSH
chmod +x "$NOCHMOD"
rm -rf "$DEVROOT"; mkdir -p "$DEVROOT"
OUT=$(RW_SSH="$NOCHMOD" rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?
if [ "$ST" -ne 0 ] && printf '%s\n' "$OUT" | grep -q 'declared executable but is not'; then
    ok "C6 an installer that skips chmod is caught by the +x check"
else
    bad "C6 an installer that skips chmod is caught by the +x check (status $ST)"
    printf '%s\n' "$OUT" | tail -3 | sed 's/^/        /'
fi

# C7: a failed transfer must fail the install rather than proceed to verify.
build_bundle
FAILTAR="$TMP/fake-ssh-failtar"
cat > "$FAILTAR" <<'FAKESSH'
#!/bin/sh
shift
case "$*" in
    *tar*) cat >/dev/null; exit 1 ;;
esac
exec sh -c "$*"
FAKESSH
chmod +x "$FAILTAR"
rm -rf "$DEVROOT"; mkdir -p "$DEVROOT"
OUT=$(RW_SSH="$FAILTAR" rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?
if [ "$ST" -ne 0 ] && printf '%s\n' "$OUT" | grep -q 'payload transfer failed'; then
    ok "C7 a failed transfer fails the install"
else
    bad "C7 a failed transfer fails the install (status $ST)"
fi

echo ""
echo "D. the owner-execute digit is read by position, not from the end"
# 0700 is executable by owner and by nobody else.
build_bundle
sed -i 's|^0755 /opt/games/snake$|0700 /opt/games/snake|' "$BUNDLE/manifest.d/native_apps.list"
rm -rf "$DEVROOT"; mkdir -p "$DEVROOT"
OUT=$(rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?
assert_eq "0" "$ST" "D1 an 0700 entry installs and passes the +x check"
assert_eq "700" "$(stat -c %a "$DEVROOT/opt/games/snake")" "D2 with mode 700"

# ⚠️ D1/D2 alone do NOT catch reading the digit from the end — an 0700 entry that is
# skipped by the check also installs cleanly, so both readings pass them. Nor is one
# 0700 file enough: with ANY 0755 entry left in the bundle, that one is caught by
# either reading and the case passes for the wrong reason. So EVERY executable entry
# is 0700 here, which makes the check's candidate list empty under the wrong reading.
# Measured: the "owner digit read from the end" sabotage failed zero cases both
# before D3 existed and while app_launcher was still 0755.
sed -i 's|^0755 /opt/roomwizard/app_launcher$|0700 /opt/roomwizard/app_launcher|' \
    "$BUNDLE/manifest.d/native_apps.list"
rm -rf "$DEVROOT"; mkdir -p "$DEVROOT"
OUT=$(RW_SSH="$NOCHMOD" rw_bundle_install_ssh fake-target "$BUNDLE" 2>&1); ST=$?
if [ "$ST" -ne 0 ] && printf '%s\n' "$OUT" | grep -q 'declared executable but is not'; then
    ok "D3 with 0700 as the only executable mode, a missing chmod is still caught"
else
    bad "D3 with 0700 as the only executable mode, a missing chmod is still caught (status $ST) — the owner digit is being read from the end"
fi

echo ""
echo "════════════════════════════════════════"
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed, $TOTAL total"
if [ "$TOTAL" -lt 20 ]; then
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
