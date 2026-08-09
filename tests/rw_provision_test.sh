#!/bin/bash
#
# rw_provision_test.sh — regression for lib/rw-provision.sh and
#                        device-files/provision-rules.conf
#
# Host-only, no device, no SD card, no root. Run it:
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/rw_provision_test.sh"
#
# IMPROVEMENT_PLAN.md C12. The delete half's suite is tests/rw_clean_test.sh, and the
# two share rw_clean_offline_path — path mapping is tested there, not here.
#
# ── What each group of cases is for ────────────────────────────────────────
#
#   A  the parser and the validator. Every check is a thing that would otherwise
#      fail silently and in the permissive direction. The two that matter most:
#      an ABSOLUTE link source (correct on a device, dangling on a card, skipped
#      in silence at boot), and a mode read off disk instead of declared.
#   B  plan compilation and ORDER. Order is the interface: unlink must precede
#      link or the glob eats the link just made; install must precede link or the
#      link dangles on a card; dropline must come last because it edits files
#      install may have just written.
#   C  the cross-file invariant: every link this file creates must be named by a
#      keep in clean-rules.conf, or the next --deep-clean deletes it. Both files
#      parse, so this is checkable rather than a comment asking a human.
#   D  the offline executor against a synthetic card, plus the canary: nothing
#      outside the base is touched.
#   E  ⚠️ THE case this file exists for — both executors' --dry-run over the same
#      inputs print the same resolved set. It is the only check that catches the
#      drift C12 documents, and the online executor is exercised through the same
#      interpreter the SSH path pipes to the device, not a re-implementation.
#
# ── Modes cannot be verified here ──────────────────────────────────────────
#
# /mnt/c reports every file 0777 and discards chmod, and WSL's own /tmp does honour
# modes — so group D runs under $(mktemp -d) and CAN assert a mode. What it cannot
# do is prove the DEVICE gets it; that assertion is commissioning/commission-offline.sh's verify
# phase against real ext4.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=../lib/rw-identify.sh
. "$REPO_DIR/lib/rw-identify.sh"
# shellcheck source=../lib/rw-clean.sh
. "$REPO_DIR/lib/rw-clean.sh"
# shellcheck source=../lib/rw-provision.sh
# $RW_PROVISION_LIB points the suite at a staged copy of the library — the hook
# tests/measure_provision_sabotage.sh drives, so a sabotage stages ONE file instead
# of copying a tree out of /mnt/c (which blows a 300 s budget over DrvFs).
. "${RW_PROVISION_LIB:-$REPO_DIR/lib/rw-provision.sh}"

RULES="$REPO_DIR/device-files/provision-rules.conf"
CLEAN_RULES="$REPO_DIR/device-files/clean-rules.conf"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
PASS=0; FAIL=0
ok()  { PASS=$((PASS + 1)); echo -e "  ${GREEN}pass${NC}  $1"; }
bad() { FAIL=$((FAIL + 1)); echo -e "  ${RED}FAIL${NC}  $1"; }
assert_eq() { if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 (want '$1', got '$2')"; fi; }
exists() { if [ -e "$1" ] || [ -L "$1" ]; then ok "$2"; else bad "$2 — missing: $1"; fi; }
gone()   { if [ -e "$1" ] || [ -L "$1" ]; then bad "$2 — still present: $1"; else ok "$2"; fi; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "A. the data-file parser and validator"
# ═══════════════════════════════════════════════════════════════════════════

if rw_provision_validate "$RULES" >/dev/null; then
    ok "A1 device-files/provision-rules.conf validates"
else
    bad "A1 device-files/provision-rules.conf validates"
    rw_provision_validate "$RULES" | sed 's/^/        /'
fi

RCOUNT=$(rw_provision_parse "$RULES" | grep -c . || true)
if [ "$RCOUNT" -gt 25 ]; then
    ok "A2 the shipped file has $RCOUNT records"
else
    bad "A2 the shipped file has $RCOUNT records (expected > 25 — did the parse drop everything?)"
fi

# reject <body> <description> — a conf that must NOT validate
reject() {
    local body="$1" desc="$2" f="$TMP/reject.conf"
    printf '%s\n' "$body" > "$f"
    if rw_provision_validate "$f" >/dev/null 2>&1; then
        bad "$desc — validate ACCEPTED it"
    else
        ok "$desc"
    fi
}
R() { printf '%s\t%s\t%s\t%s\t%s\t%s' "$1" "$2" "$3" "$4" "$5" "$6"; }

reject "$(printf 'install\tbase\t0755\t/etc/init.d/x\tdevice-files/audio-enable')" \
    "A3 a 5-field line (no reason) is rejected"
reject "$(R install base 0755 /etc/init.d/x device-files/audio-enable '')" \
    "A4 an empty reason field is rejected"
reject "$(R frobnicate base - /etc/x - why)" \
    "A5 an unknown record type is rejected"
reject "$(R install bogus 0755 /etc/init.d/x device-files/audio-enable why)" \
    "A6 an unknown group is rejected"
reject "install base 0755 /etc/init.d/x device-files/audio-enable spaces not tabs" \
    "A7 space-separated fields are rejected"
reject "$(R install base 0755 etc/init.d/x device-files/audio-enable why)" \
    "A8 a relative target is rejected"
reject "$(R install base 0755 /etc/../shadow device-files/audio-enable why)" \
    "A9 a target containing .. is rejected"
reject "$(R install base 0755 / device-files/audio-enable why)" \
    "A10 a target of / is rejected"
reject "$(R install base 0755 /etc/init.d/x/ device-files/audio-enable why)" \
    "A11 a trailing slash is rejected"
reject "" "A12 a file with no records at all is rejected"

# ⚠️ The mode is DECLARED. A missing or malformed one must not be tolerated,
# because it cannot be recovered from disk on this host.
reject "$(R install base - /etc/init.d/x device-files/audio-enable why)" \
    "A13 an install with no declared mode is rejected"
reject "$(R install base rwxr-xr-x /etc/init.d/x device-files/audio-enable why)" \
    "A14 a symbolic mode is rejected — octal only"
reject "$(R install base 0999 /etc/init.d/x device-files/audio-enable why)" \
    "A15 a non-octal digit in a mode is rejected"
reject "$(R link base 0755 /etc/rc5.d/S28x ../init.d/x why)" \
    "A16 a link with a mode is rejected — the field means nothing there"

# ⚠️ THE case: an absolute link source is correct on a device and dangling on a card.
reject "$(R link base - /etc/rc5.d/S28time-sync /etc/init.d/time-sync why)" \
    "A17 an ABSOLUTE link source is rejected (it dangles on a mounted card)"
reject "$(R link-opt base - /etc/rc5.d/S30avahi-daemon /etc/init.d/avahi-daemon why)" \
    "A18 and for link-opt too"

reject "$(R install base 0755 /etc/init.d/x does-not-exist-anywhere why)" \
    "A19 an install whose source is not in the repo is rejected"
reject "$(R install base 0755 /etc/init.d/x /etc/passwd why)" \
    "A20 an absolute install source is rejected — sources are repo-relative"
reject "$(R directive sshd - /etc/ssh/sshd_config PermitRootLogin why)" \
    "A21 a directive with no = is rejected"
reject "$(R unlink base - '/etc/rc*.d/S99roomwizard-app' - why)" \
    "A22 a glob outside the last component is rejected (it would silently match nothing)"

# rc0.d / rc6.d are shutdown. Unreachable by construction, as in clean-rules.conf.
reject "$(R unlink base - /etc/rc6.d/K09sshd - why)" \
    "A23 no rule may name rc6.d — shutdown, not startup"
reject "$(R link base - /etc/rc0.d/S20sendsigs ../init.d/x why)" \
    "A24 nor rc0.d"

# Comments, blanks, and a tab inside the reason.
CMT="$TMP/comments.conf"
printf '# c\n\n   \n%s\n' "$(R touch base 0644 /var/x - a reason)" > "$CMT"
assert_eq "1" "$(rw_provision_parse "$CMT" | grep -c . || true)" \
    "A25 comments and blank lines are not records"
TABBY="$TMP/tabby.conf"
printf '%s\n' "$(printf 'touch\tbase\t0644\t/var/x\t-\ta reason\twith a tab')" > "$TABBY"
assert_eq "touch	base	0644	/var/x	-" "$(rw_provision_parse "$TABBY")" \
    "A26 a tab inside the reason does not shift the first five fields"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "B. plan compilation and order"
# ═══════════════════════════════════════════════════════════════════════════

PLAN_ALL=$(rw_provision_plan "$RULES" "$(rw_provision_default_groups)")
if [ -n "$PLAN_ALL" ]; then
    ok "B0 the default plan compiles"
else
    bad "B0 the default plan compiles — it is EMPTY, so every assertion below is meaningless"
fi

has()    { printf '%s\n' "$2" | grep -qxF "$1"; }
expect() { if has "$1" "$2"; then ok "$3"; else bad "$3 — plan has no '$1'"; fi; }
absent() { if has "$1" "$2"; then bad "$3 — plan HAS '$1'"; else ok "$3"; fi; }

expect "$(printf 'install\t0755\t/etc/init.d/audio-enable\tdevice-files/audio-enable')" \
    "$PLAN_ALL" "B1 the audio-enable install, with its declared mode"
expect "$(printf 'install\t0644\t/etc/sysctl.d/99-security.conf\tdevice-files/99-security.conf')" \
    "$PLAN_ALL" "B2 99-security.conf is 0644, not 0755"
expect "$(printf 'install\t0755\t/etc/init.d/roomwizard-app\tdevice-files/roomwizard-app')" \
    "$PLAN_ALL" "B3 the init script is installed under a different name from its source"
expect "$(printf 'link\t-\t/etc/rc5.d/S99roomwizard-app\t../init.d/roomwizard-app')" \
    "$PLAN_ALL" "B4 the rc5.d app link"
expect "$(printf 'unlink\t-\t/etc/rc5.d/S*roomwizard-games\t-')" \
    "$PLAN_ALL" "B5 the stale former-name link is unlinked — THE drift this file fixes"
expect "$(printf 'touch\t0644\t/var/watchdog_test\t-')" \
    "$PLAN_ALL" "B6 the watchdog bypass"
expect "$(printf 'directive\t-\t/etc/ssh/sshd_config\tPermitEmptyPasswords=no')" \
    "$PLAN_ALL" "B7 the sshd directive that the factory default requires"
expect "$(printf 'dropline\t-\t/etc/profile\twsplatform\\.conf')" \
    "$PLAN_ALL" "B8 the /etc/profile fix, which used to exist only on the online path"
expect "$(printf 'link-opt\t-\t/etc/rc5.d/S30avahi-daemon\t../init.d/avahi-daemon')" \
    "$PLAN_ALL" "B9 the avahi link is optional, not mandatory"

# PermitRootLogin must NOT be here: root is the only account and there is no serial
# console to recover through if keys break.
if printf '%s\n' "$PLAN_ALL" | grep -q 'PermitRootLogin'; then
    bad "B10 PermitRootLogin is deliberately not a directive"
else
    ok "B10 PermitRootLogin is deliberately not a directive"
fi

# ── Order is the interface ────────────────────────────────────────────────
pos() { printf '%s\n' "$PLAN_ALL" | grep -n "^$1" | head -1 | cut -d: -f1; }
LAST_UNLINK=$(printf '%s\n' "$PLAN_ALL" | grep -n '^unlink' | tail -1 | cut -d: -f1)
FIRST_LINK=$(pos link)
LAST_INSTALL=$(printf '%s\n' "$PLAN_ALL" | grep -n '^install' | tail -1 | cut -d: -f1)
FIRST_DROP=$(pos dropline)
LAST_OTHER=$(printf '%s\n' "$PLAN_ALL" | grep -vn '^dropline' | tail -1 | cut -d: -f1)

if [ "$LAST_UNLINK" -lt "$FIRST_LINK" ]; then
    ok "B11 every unlink precedes every link — else the glob eats the link just made"
else
    bad "B11 every unlink precedes every link (last unlink $LAST_UNLINK, first link $FIRST_LINK)"
fi
if [ "$LAST_INSTALL" -lt "$FIRST_LINK" ]; then
    ok "B12 every install precedes every link — else the link dangles on a card"
else
    bad "B12 every install precedes every link (last install $LAST_INSTALL, first link $FIRST_LINK)"
fi
if [ "$FIRST_DROP" -gt "$LAST_OTHER" ]; then
    ok "B13 dropline comes last — it edits files install may have just written"
else
    bad "B13 dropline comes last (first dropline $FIRST_DROP, last other $LAST_OTHER)"
fi

# ── Groups ────────────────────────────────────────────────────────────────
PLAN_NOMDNS=$(rw_provision_plan "$RULES" "base sshd")
absent "$(printf 'link-opt\t-\t/etc/rc5.d/S30avahi-daemon\t../init.d/avahi-daemon')" \
    "$PLAN_NOMDNS" "B14 --no-mdns drops the avahi link"
expect "$(printf 'link\t-\t/etc/rc5.d/S28time-sync\t../init.d/time-sync')" \
    "$PLAN_NOMDNS" "B15 and nothing else"
PLAN_NOSSHD=$(rw_provision_plan "$RULES" "base mdns")
if printf '%s\n' "$PLAN_NOSSHD" | grep -q 'sshd_config'; then
    bad "B16 --no-harden-sshd drops every sshd record"
else
    ok "B16 --no-harden-sshd drops every sshd record"
fi
if rw_provision_plan "$RULES" "mdns sshd" >/dev/null 2>&1; then
    bad "B17 a group list without 'base' is refused"
else
    ok "B17 a group list without 'base' is refused"
fi
if rw_provision_plan "$RULES" "base nosuchgroup" >/dev/null 2>&1; then
    bad "B18 an unknown group name is refused"
else
    ok "B18 an unknown group name is refused"
fi

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "C. the cross-file invariant: a link the whitelist does not name gets swept"
# ═══════════════════════════════════════════════════════════════════════════

if OUT=$(rw_provision_check_keeps "$RULES" "$CLEAN_RULES" 2>&1); then
    ok "C1 every boot link in provision-rules.conf is kept by clean-rules.conf"
else
    bad "C1 every boot link in provision-rules.conf is kept by clean-rules.conf"
    printf '%s\n' "$OUT" | sed 's/^/        /'
fi

# The negative control: a link nothing keeps must be caught.
SAB="$TMP/sabotage.conf"
{
    cat "$RULES"
    R link base - /etc/rc5.d/S77nothing-keeps-this ../init.d/roomwizard-app 'a link no keep names'
    echo ""
} > "$SAB"
if rw_provision_check_keeps "$SAB" "$CLEAN_RULES" >/dev/null 2>&1; then
    bad "C2 a link with no matching keep is CAUGHT"
else
    ok "C2 a link with no matching keep is CAUGHT"
fi

# And the other direction: a keep whose priority differs from the link's.
SAB2="$TMP/sabotage2.conf"
sed 's|/etc/rc5.d/S28time-sync|/etc/rc5.d/S27time-sync|' "$RULES" > "$SAB2"
if rw_provision_check_keeps "$SAB2" "$CLEAN_RULES" >/dev/null 2>&1; then
    bad "C3 changing a link's PRIORITY without changing the keep is caught"
else
    ok "C3 changing a link's PRIORITY without changing the keep is caught"
fi

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "D. the offline executor, against a synthetic card"
# ═══════════════════════════════════════════════════════════════════════════

CARD="$TMP/card"
build_card() {
    rm -rf "$CARD"
    mkdir -p "$CARD/root/etc/init.d" "$CARD/root/etc/rc2.d" "$CARD/root/etc/rc3.d" \
             "$CARD/root/etc/rc4.d" "$CARD/root/etc/rc5.d" "$CARD/root/etc/ssh" \
             "$CARD/root/etc/sysctl.d" "$CARD/root/usr/sbin" "$CARD/root/var" \
             "$CARD/data" "$CARD/log" "$CARD/backup"
    # The vendor's sshd_config, with the factory default this hardening exists for.
    printf 'PermitRootLogin yes\nPermitEmptyPasswords yes\n' > "$CARD/root/etc/ssh/sshd_config"
    # /etc/profile as measured on both card captures.
    printf 'export PATH=/usr/bin\n. /home/root/data/websign/wsplatform.conf\numask 022\n' \
        > "$CARD/root/etc/profile"
    printf '::sysinit:/etc/init.d/rcS\n4:12345:respawn:/sbin/getty 38400 tty4\n' \
        > "$CARD/root/etc/inittab"
    # avahi, so link-opt has something to resolve to.
    : > "$CARD/root/etc/init.d/avahi-daemon"; chmod +x "$CARD/root/etc/init.d/avahi-daemon"
    : > "$CARD/root/usr/sbin/avahi-daemon"
    # Stale links from an older setup run — the drift this file fixes.
    ln -sf ../init.d/roomwizard-games "$CARD/root/etc/rc5.d/S50roomwizard-games"
    ln -sf ../init.d/roomwizard-app   "$CARD/root/etc/rc5.d/S50roomwizard-app"
}

# The canary: laid out like a root and OUTSIDE the base, so a prefix bug lands
# somewhere visible instead of only showing up as a passing test.
CANARY="$TMP/canary"
mkdir -p "$CANARY/etc/init.d" "$CANARY/etc/rc5.d" "$CANARY/var"
echo "root:x:0:0" > "$CANARY/etc/shadow"
printf 'PermitEmptyPasswords yes\n' > "$CANARY/etc/ssh_config_lookalike"
CANARY_MD5=$(cd "$CANARY" && find . -type f | LC_ALL=C sort | xargs md5sum | md5sum)

build_card
rw_provision_plan "$RULES" "$(rw_provision_default_groups)" > "$TMP/plan"

# ── the dry run must change nothing ──────────────────────────────────────
CARD_MD5=$(cd "$CARD" && find . | LC_ALL=C sort | md5sum)
DRY_OUT=$(RW_PROVISION_DRY=1 rw_provision_apply_offline "$CARD" "$TMP/plan" "$REPO_DIR")
assert_eq "$CARD_MD5" "$(cd "$CARD" && find . | LC_ALL=C sort | md5sum)" \
    "D1 --dry-run changes nothing at all"
DRYLINES=$(printf '%s\n' "$DRY_OUT" | grep -c 'would ' || true)
if [ "$DRYLINES" -gt 20 ]; then
    ok "D2 --dry-run printed $DRYLINES actions"
else
    bad "D2 --dry-run printed only $DRYLINES actions (expected > 20)"
fi

# ── for real ─────────────────────────────────────────────────────────────
build_card
rw_provision_apply_offline "$CARD" "$TMP/plan" "$REPO_DIR" > "$TMP/apply.out" 2>&1 \
    || bad "D3 rw_provision_apply_offline returned non-zero"

exists "$CARD/root/etc/init.d/audio-enable"          "D4 audio-enable installed"
exists "$CARD/root/etc/init.d/time-sync"             "D5 time-sync installed"
exists "$CARD/root/etc/sysctl.d/99-security.conf"    "D6 99-security.conf installed"
exists "$CARD/root/etc/init.d/roomwizard-app"        "D7 the init script installed under its DEPLOYED name"
exists "$CARD/root/opt/roomwizard/disable-steelcase.sh" "D8 disable-steelcase.sh installed, directory created"

# The bytes must be the repo's, not a heredoc's idea of them.
assert_eq "$(md5sum < "$REPO_DIR/device-files/audio-enable")" \
          "$(md5sum < "$CARD/root/etc/init.d/audio-enable")" \
    "D9 audio-enable is byte-for-byte the repo's copy"
assert_eq "$(md5sum < "$REPO_DIR/device-files/roomwizard-app")" \
          "$(md5sum < "$CARD/root/etc/init.d/roomwizard-app")" \
    "D10 and so is the init script, despite the rename"

# Modes. WSL's /tmp honours them; /mnt/c would not.
assert_eq "755" "$(stat -c %a "$CARD/root/etc/init.d/audio-enable")" "D11 audio-enable is 0755"
assert_eq "644" "$(stat -c %a "$CARD/root/etc/sysctl.d/99-security.conf")" "D12 99-security.conf is 0644, as declared"
assert_eq "755" "$(stat -c %a "$CARD/root/opt/roomwizard/disable-steelcase.sh")" "D13 disable-steelcase.sh is 0755"

# Links, and that they RESOLVE — a dangling rc5.d link is skipped in silence.
for l in rc5.d/S28time-sync rc5.d/S29audio-enable rc5.d/S99roomwizard-app \
         rc2.d/S99roomwizard-app rc3.d/S99roomwizard-app rc4.d/S99roomwizard-app; do
    if [ -L "$CARD/root/etc/$l" ] && [ -e "$CARD/root/etc/$l" ]; then
        ok "D14.$(basename "$l") $l links and resolves"
    else
        bad "D14 $l — $( [ -L "$CARD/root/etc/$l" ] && echo 'DANGLING' || echo 'missing')"
    fi
done
if [ -L "$CARD/root/etc/rc5.d/S30avahi-daemon" ] && [ -e "$CARD/root/etc/rc5.d/S30avahi-daemon" ]; then
    ok "D15 the avahi link is made when the image has avahi"
else
    bad "D15 the avahi link is made when the image has avahi"
fi

# The drift: stale links gone.
gone "$CARD/root/etc/rc5.d/S50roomwizard-games" "D16 the stale former-name link is GONE — the offline path used to keep it"
gone "$CARD/root/etc/rc5.d/S50roomwizard-app"   "D17 and the wrong-priority copy of our own link"

exists "$CARD/root/var/watchdog_test"           "D18 the watchdog bypass file exists"
assert_eq "644" "$(stat -c %a "$CARD/root/var/watchdog_test")" "D19 with its declared mode"

# sshd: the substitution AND the appends, and the backup taken once.
if grep -q '^PermitEmptyPasswords no$' "$CARD/root/etc/ssh/sshd_config"; then
    ok "D20 PermitEmptyPasswords is no"
else
    bad "D20 PermitEmptyPasswords is no — got: $(grep -i permitempty "$CARD/root/etc/ssh/sshd_config" | tr '\n' ' ')"
fi
assert_eq "1" "$(grep -c '^PermitEmptyPasswords' "$CARD/root/etc/ssh/sshd_config")" \
    "D21 and exactly once — a directive is SET, not appended beside the old value"
for d in MaxAuthTries LoginGraceTime MaxSessions; do
    assert_eq "1" "$(grep -c "^$d " "$CARD/root/etc/ssh/sshd_config")" "D22.$d $d set once"
done
assert_eq "yes" "$(awk '/^PermitRootLogin/{print $2}' "$CARD/root/etc/ssh/sshd_config")" \
    "D23 PermitRootLogin is left alone — root is the only account"
exists "$CARD/root/etc/ssh/sshd_config.orig" "D24 the factory sshd_config is backed up"
assert_eq "$(printf 'PermitRootLogin yes\nPermitEmptyPasswords yes\n' | md5sum)" \
          "$(md5sum < "$CARD/root/etc/ssh/sshd_config.orig")" \
    "D25 and the backup is the FACTORY bytes, not the hardened ones"

# droplines
if grep -q 'wsplatform' "$CARD/root/etc/profile"; then
    bad "D26 /etc/profile no longer sources the deleted wsplatform.conf"
else
    ok "D26 /etc/profile no longer sources the deleted wsplatform.conf"
fi
assert_eq "2" "$(grep -c . "$CARD/root/etc/profile")" "D27 and the rest of /etc/profile is intact"
if grep -q 'tty4' "$CARD/root/etc/inittab"; then
    bad "D28 the tty4 getty line is gone from /etc/inittab"
else
    ok "D28 the tty4 getty line is gone from /etc/inittab"
fi
assert_eq "1" "$(grep -c . "$CARD/root/etc/inittab")" "D29 and sysinit survived"

# ── idempotence: a second run must be a no-op, not a doubling ────────────
rw_provision_apply_offline "$CARD" "$TMP/plan" "$REPO_DIR" >/dev/null 2>&1
assert_eq "1" "$(grep -c '^MaxAuthTries ' "$CARD/root/etc/ssh/sshd_config")" \
    "D30 a second run does not append MaxAuthTries twice"
assert_eq "$(printf 'PermitRootLogin yes\nPermitEmptyPasswords yes\n' | md5sum)" \
          "$(md5sum < "$CARD/root/etc/ssh/sshd_config.orig")" \
    "D31 and does not overwrite the backup with the already-hardened file"

# ── link-opt on an image WITHOUT avahi: skip, do not dangle ──────────────
build_card
rm -f "$CARD/root/etc/init.d/avahi-daemon"
rw_provision_apply_offline "$CARD" "$TMP/plan" "$REPO_DIR" >/dev/null 2>&1
if [ -L "$CARD/root/etc/rc5.d/S30avahi-daemon" ]; then
    bad "D32 link-opt must NOT create a dangling link on an image without avahi"
else
    ok "D32 link-opt skips when its target is absent, rather than dangling"
fi
exists "$CARD/root/etc/rc5.d/S99roomwizard-app" "D33 and the rest of the plan still ran"

# ── the guard, and the canary ────────────────────────────────────────────
if rw_provision_apply_offline "" "$TMP/plan" "$REPO_DIR" >/dev/null 2>&1; then
    bad "D34 an empty base is refused"
else
    ok "D34 an empty base is refused"
fi
if rw_provision_apply_offline "/" "$TMP/plan" "$REPO_DIR" >/dev/null 2>&1; then
    bad "D35 a base of / is refused — that is this host's root"
else
    ok "D35 a base of / is refused — that is this host's root"
fi
assert_eq "$CANARY_MD5" "$(cd "$CANARY" && find . -type f | LC_ALL=C sort | xargs md5sum | md5sum)" \
    "D36 the canary tree outside the base is byte-for-byte unchanged"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "E. ⚠️ both executors' dry runs print the same resolved set"
echo "   (the only check that catches the drift C12 documents)"
# ═══════════════════════════════════════════════════════════════════════════

build_card
OFF=$(RW_PROVISION_DRY=1 rw_provision_apply_offline "$CARD" "$TMP/plan" "$REPO_DIR" \
        | rw_provision_canonical)

# The ONLINE executor, run here as the device would run it: rw_provision_online_script
# emits the interpreter that commissioning/provision.sh pipes to `ssh <target> sh -s`, and it is
# run against a chroot-shaped copy so nothing touches this host. That is what makes
# this a comparison of the two EXECUTORS rather than of one executor and a wish.
ONLINE_ROOT="$TMP/online"
rm -rf "$ONLINE_ROOT"; cp -a "$CARD/root" "$ONLINE_ROOT"
ON=$(RW_PROVISION_DRY=1 RW_PROVISION_ROOT="$ONLINE_ROOT" \
        sh -c "$(rw_provision_online_script)" -- "$TMP/plan" | rw_provision_canonical)

if [ -n "$OFF" ] && [ -n "$ON" ]; then
    ok "E1 both dry runs produced output"
else
    bad "E1 both dry runs produced output (offline $(printf '%s' "$OFF" | wc -l) lines, online $(printf '%s' "$ON" | wc -l))"
fi
if [ "$OFF" = "$ON" ]; then
    ok "E2 the two resolved sets are IDENTICAL, modulo the prefix"
else
    bad "E2 the two resolved sets are identical, modulo the prefix"
    diff <(printf '%s\n' "$OFF") <(printf '%s\n' "$ON") | head -20 | sed 's/^/        /'
fi
NREC=$(grep -cve '^[[:space:]]*#' -e '^[[:space:]]*$' "$TMP/plan" || true)
assert_eq "$NREC" "$(printf '%s\n' "$OFF" | grep -c . || true)" \
    "E3 and the offline set covers every record in the plan, not a subset"

# The negative control on E2 itself: if one executor drops a verb, E2 must fire.
SHORT=$(grep -v '^unlink' "$TMP/plan")
printf '%s\n' "$SHORT" > "$TMP/plan.short"
OFF2=$(RW_PROVISION_DRY=1 rw_provision_apply_offline "$CARD" "$TMP/plan.short" "$REPO_DIR" \
        | rw_provision_canonical)
if [ "$OFF2" = "$ON" ]; then
    bad "E4 E2 would notice a dropped verb"
else
    ok "E4 E2 would notice a dropped verb"
fi

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "F. the online path's copy step — B28"
# ═══════════════════════════════════════════════════════════════════════════
#
# ⚠️ Group E cannot reach this, and the gap is structural rather than an oversight.
# E compares two --dry-run PLANS; a dry run copies nothing, so the scp step is
# precisely the part of the online path that has no offline counterpart to be
# compared against. That is how B28 shipped past 94 passing cases:
# commissioning/provision.sh installed 1 of its 8 files, because `ssh` inside
# `while read … done < "$PLAN"` reads its own stdin and forwarded the whole rest of
# the plan to the remote `mkdir`.
#
# So this group runs the REAL rw_provision_push_installs against a local directory
# through the $RW_SSH/$RW_SCP stubs lib/rw-usbpower.sh already established, and
# asserts 8 of 8. **Never "more than one"** — the defect produced exactly one, so
# any threshold below the full count passes on the broken tree.
#
# The got-vs-want guard inside the function is not reachable from here (it needs a
# loop that actually loses records); its negative control is
# tests/measure_provision_sabotage.sh, which puts the plan back on stdin.

FW="$TMP/push"; mkdir -p "$FW/bin" "$FW/dev"

# ⚠️ The stub ssh MUST slurp its stdin. That is the behaviour that caused B28, so a
# stub that skips it cannot fail the way production failed and every case below
# becomes a vacuous pass. F1 is the check that this one can.
cat > "$FW/bin/ssh" <<'STUB'
#!/bin/sh
shift                                  # the target
cat > /dev/null                        # what a real ssh does with a non-tty stdin
echo "$*" >> "$STUBLOG/ssh.calls"
d=$(printf '%s' "$*" | sed -n "s/^mkdir -p '\(.*\)'$/\1/p")
[ -n "$d" ] && mkdir -p "$STUBDEV$d"
exit 0
STUB
cat > "$FW/bin/scp" <<'STUB'
#!/bin/sh
[ "$1" = "-q" ] && shift
src=$1; dst=$2; p=${dst#*:}
# Faithful to scp: it does NOT create the parent, it fails. So a dropped mkdir -p
# is measurable here rather than papered over by the stub.
[ -d "$STUBDEV${p%/*}" ] || { echo "scp: $p: No such file or directory" >&2; exit 1; }
cp "$src" "$STUBDEV$p" || exit 1
echo "$src -> $p" >> "$STUBLOG/scp.calls"
STUB
chmod +x "$FW/bin/ssh" "$FW/bin/scp"
export STUBLOG="$FW" STUBDEV="$FW/dev"
reset_stubs() { : > "$FW/ssh.calls"; : > "$FW/scp.calls"; rm -rf "$FW/dev"; mkdir -p "$FW/dev"; }
push() { RW_SSH="$FW/bin/ssh" RW_SCP="$FW/bin/scp" rw_provision_push_installs "$@"; }

PPLAN="$FW/plan"
rw_provision_plan "$RULES" "base usb" > "$PPLAN" || bad "F0 the plan compiles"
NINST=$(awk -F'\t' '$1 == "install"' "$PPLAN" | wc -l | tr -d ' ')
if [ "$NINST" -ge 8 ]; then
    ok "F0 the shipped plan has $NINST install record(s)"
else
    bad "F0 the shipped plan has $NINST install record(s) — expected >= 8, this group is vacuous below that"
fi

# ── F1: the harness can reproduce B28 ────────────────────────────────────────
reset_stubs
(
    PATH="$FW/bin:$PATH"; D=root@fake
    while IFS=$'\t' read -r k m t s; do
        [ "$k" = install ] || continue
        ssh "$D" "mkdir -p '${t%/*}'"
        scp -q "$REPO_DIR/$s" "$D:$t"
    done < "$PPLAN"
) >/dev/null 2>&1
assert_eq 1 "$(grep -c . "$FW/scp.calls" || :)" \
    "F1 the pre-fix shape (plan on stdin) copies exactly 1 — the stubs do reproduce B28"

# ── F2-F8: the real function ─────────────────────────────────────────────────
reset_stubs
if push "$PPLAN" "$REPO_DIR" root@fake > "$FW/out" 2>&1; then
    ok "F2 rw_provision_push_installs succeeds over the shipped plan"
else
    bad "F2 rw_provision_push_installs succeeds over the shipped plan"
    sed 's/^/        /' "$FW/out"
fi
assert_eq "$NINST" "$(grep -c . "$FW/scp.calls" || :)" \
    "F3 all $NINST install records were copied, not 1"
assert_eq "$NINST" "$(grep -c '^  copied' "$FW/out" || :)" \
    "F4 one reported copied line per install record"

MISS=0; DIFFER=0
while IFS=$'\t' read -r k m t s; do
    [ "$k" = install ] || continue
    if [ ! -f "$FW/dev$t" ]; then MISS=$((MISS + 1)); continue; fi
    cmp -s "$REPO_DIR/$s" "$FW/dev$t" || DIFFER=$((DIFFER + 1))
done < "$PPLAN"
assert_eq 0 "$MISS"   "F5 every install target exists on the fake device"
assert_eq 0 "$DIFFER" "F6 every installed file is byte-identical to its device-files source"

assert_eq "$NINST" "$(grep -c '^mkdir -p' "$FW/ssh.calls" || :)" \
    "F7 a mkdir -p preceded every copy"
# The three that do not exist on a vendor unit — /etc/init.d does, so it proves nothing.
NODIR=0
for d in /etc/sysctl.d /opt/roomwizard /usr/local/bin; do
    grep -q "mkdir -p '$d'" "$FW/ssh.calls" || NODIR=$((NODIR + 1))
done
assert_eq 0 "$NODIR" "F8 the three directories a vendor unit lacks are created first"

# ── F9-F11: a missing source is a refusal, before the device is touched ──────
reset_stubs
printf 'install\t0755\t/etc/init.d/x\tdevice-files/does-not-exist\n' > "$FW/badplan"
if push "$FW/badplan" "$REPO_DIR" root@fake > "$FW/bad.out" 2>&1; then
    bad "F9 a missing install source is refused"
else
    ok "F9 a missing install source is refused"
fi
# ⚠️ "It returned non-zero" is NOT the measurement: scp fails on a missing source
# anyway, so deleting the check entirely still refuses. What the check buys is that
# nothing ran on the device first and that the message names the host-side path —
# so those are what F10 and F11 assert.
assert_eq 0 "$(( $(grep -c . "$FW/scp.calls" || :) + $(grep -c . "$FW/ssh.calls" || :) ))" \
    "F10 it refused before running anything on the device"
if grep -q 'device-files/does-not-exist' "$FW/bad.out"; then
    ok "F11 the refusal names the missing host-side source"
else
    bad "F11 the refusal names the missing host-side source"
fi

# ── F12: the other caller's shape — usb_host/build-and-deploy.sh ─────────────
UPLAN="$FW/usbplan"
rw_provision_plan_component "$RULES" usb > "$UPLAN" || bad "F12 the usb component plan compiles"
NU=$(awk -F'\t' '$1 == "install"' "$UPLAN" | wc -l | tr -d ' ')
reset_stubs
push "$UPLAN" "$REPO_DIR" root@fake >/dev/null 2>&1
if [ "$NU" -ge 2 ]; then
    assert_eq "$NU" "$(grep -c . "$FW/scp.calls" || :)" \
        "F12 the usb-group plan copies all $NU of its install records"
else
    bad "F12 the usb-group plan has only $NU install record(s) — nothing to measure"
fi

# ── F13-F14: the summary line accounts for every action ──────────────────────
#
# B28's header read "35 action(s) — 8 install, 9 link, 10 unlink", which accounts
# for 27. backup, touch, the four directives and the two droplines were simply not
# in the breakdown — the kind of arithmetic that hides a verb nobody is executing.
SUM=$(rw_provision_plan_summary "$PPLAN")
STOT=$(printf '%s' "$SUM" | sed 's/ action.*//')
SADD=$(printf '%s' "$SUM" | sed 's/^.*— //' | tr ',' '\n' | awk '{s += $1} END {print s + 0}')
assert_eq "$(grep -c . "$PPLAN")" "$STOT" "F13 the summary's total equals the plan's line count"
assert_eq "$STOT" "$SADD" "F14 the summary's per-type counts add up to its total"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "════════════════════════════════════════"
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed, $TOTAL total"
if [ "$TOTAL" -lt 70 ]; then
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
