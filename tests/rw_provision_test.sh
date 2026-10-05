#!/bin/bash
#
# rw_provision_test.sh — regression for lib/rw-provision.sh and
#                        device-files/provision-rules.conf
#
# Host-only, no device, no SD card, no root. Run it:
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/rw_provision_test.sh"
#
# The delete half's suite is tests/rw_clean_test.sh, and the
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
#      drift between them, and the online executor is exercised through the same
#      interpreter the SSH path pipes to the device, not a re-implementation.
#
# ── Modes cannot be verified here ──────────────────────────────────────────
#
# /mnt/c reports every file 0777 and discards chmod, and WSL's own /tmp does honour
# modes — so group D runs under $(mktemp -d) and CAN assert a mode. What it cannot
# do is prove the DEVICE gets it; that assertion is commissioning/commission-offline.sh's verify
# phase against real ext4.

# Source-path directive: resolves the source= hints below against this script's directory.
# shellcheck source-path=SCRIPTDIR
set -u

# ⚠️ Diagnostics from the stub-log guard in push() must survive a CALL SITE that
# redirects push, and F9 does exactly that (`if push … > "$FW/bad.out" 2>&1`). Sent to
# plain stderr they land in that file, so the guard exits 2 while run-all.sh prints the
# PREVIOUS case as the last line — the harness error becomes invisible at the one site
# that can trigger it. Measured. fd 9 is a copy of the real stderr, taken before any
# redirection, and the guard writes there.
exec 9>&2

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
expect "$(printf 'install\t0644\t/etc/sysctl.conf\tdevice-files/sysctl.conf')" \
    "$PLAN_ALL" "B2 sysctl.conf is 0644, not 0755"
expect "$(printf 'unlink\t-\t/etc/sysctl.d/99-security.conf\t-')" \
    "$PLAN_ALL" "B2b the old sysctl.d copy, which nothing reads, is removed"
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

# ── Network listeners: syslogd off the network, avahi narrowed ───────────────
# Measured on a unit in service: /usr/sbin/syslogd is sysklogd v2.1.1, started with
# no arguments, and its own help says it listens on UDP 514 on every interface
# unless started with -s; twice means no network socket at all. The init script
# passes $SYSLOGD from /etc/default/syslogd, which the image does not ship.
expect "$(printf 'install\t0644\t/etc/default/syslogd\tdevice-files/default-syslogd')" \
    "$PLAN_ALL" "B19 /etc/default/syslogd is installed, in base"
expect "$(printf 'install\t0644\t/etc/default/syslogd\tdevice-files/default-syslogd')" \
    "$PLAN_NOMDNS" "B19b and stays when mDNS is off — it has nothing to do with avahi"
expect "$(printf 'install\t0644\t/etc/avahi/avahi-daemon.conf\tdevice-files/avahi-daemon.conf')" \
    "$PLAN_ALL" "B20 avahi-daemon.conf is installed with the mdns group"
absent "$(printf 'install\t0644\t/etc/avahi/avahi-daemon.conf\tdevice-files/avahi-daemon.conf')" \
    "$PLAN_NOMDNS" "B21 --no-mdns leaves the vendor avahi config alone"

# What syslogd is actually started with: the init script's own expansion of $SYSLOGD.
# shellcheck source=/dev/null
SYSLOGD_ARGS=$( (SYSLOGD=; . "$REPO_DIR/device-files/default-syslogd" 2>/dev/null; echo "$SYSLOGD") )
NSFLAG=0
for w in $SYSLOGD_ARGS; do
    case "$w" in -ss) NSFLAG=$((NSFLAG + 2)) ;; -s) NSFLAG=$((NSFLAG + 1)) ;; esac
done
assert_eq 2 "$NSFLAG" "B22 syslogd gets -s twice: no UDP socket at all (got '$SYSLOGD_ARGS')"
case " $SYSLOGD_ARGS " in
    *" -a"*|*" -b"*) bad "B23 no -a/-b, which would re-open a network socket" ;;
    *)               ok  "B23 no -a/-b, which would re-open a network socket" ;;
esac

# avahi-daemon.conf is INI; a commented key is NOT set — the vendor file ships
# "#allow-interfaces=eth0", so a grep for the key alone would pass on it.
ini() {   # FILE SECTION KEY -> value, empty if unset
    awk -F= -v s="[$2]" -v k="$3" '
        /^[[:space:]]*\[/ { sec = $0; gsub(/[[:space:]]/, "", sec); next }
        sec == s && $1 == k { v = $2; sub(/^[[:space:]]+/, "", v); sub(/[[:space:]]+$/, "", v); val = v }
        END { print val }' "$1"
}
printf '[server]\n#allow-interfaces=eth0\nuse-ipv4=yes\n' > "$TMP/ini-control"
assert_eq "" "$(ini "$TMP/ini-control" server allow-interfaces)" \
    "B24 control: the INI reader does not read a commented key as set"
assert_eq "yes" "$(ini "$TMP/ini-control" server use-ipv4)" \
    "B24b control: and does read a set one"
AV="$REPO_DIR/device-files/avahi-daemon.conf"
if [ -f "$AV" ]; then
    assert_eq "eth0" "$(ini "$AV" server allow-interfaces)" "B25 avahi answers on eth0 only"
    assert_eq "no"   "$(ini "$AV" server use-ipv6)"         "B26 and over IPv4 only"
    assert_eq "no"   "$(ini "$AV" wide-area enable-wide-area)" \
        "B27 wide-area off — the second, random-port UDP socket avahi opened"
    assert_eq "no"   "$(ini "$AV" reflector enable-reflector)" "B28 no reflector, explicitly"
    # mDNS must keep working: provision.sh and the operator reach units as <name>.local.
    assert_eq "yes"  "$(ini "$AV" server use-ipv4)"         "B29 mDNS over IPv4 stays on"
    if [ "$(ini "$AV" publish disable-publishing)" != yes ] && [ "$(ini "$AV" publish publish-addresses)" != no ]; then
        ok  "B30 the host's A record is still published — <name>.local resolves"
    else
        bad "B30 the host's A record is still published — <name>.local resolves"
    fi
else
    bad "B25 device-files/avahi-daemon.conf exists — B25-B30 are vacuous without it"
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
exists "$CARD/root/etc/sysctl.conf"    "D6 sysctl.conf installed"
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
assert_eq "644" "$(stat -c %a "$CARD/root/etc/sysctl.conf")" "D12 sysctl.conf is 0644, as declared"
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

# /etc/default and /etc/avahi are not in build_card: the installer must create them.
for pair in "etc/default/syslogd:default-syslogd" "etc/avahi/avahi-daemon.conf:avahi-daemon.conf"; do
    t="${pair%%:*}"; s="${pair#*:}"
    if [ -f "$CARD/root/$t" ] && cmp -s "$REPO_DIR/device-files/$s" "$CARD/root/$t"; then
        ok "D19b /$t is byte-for-byte device-files/$s"
    else
        bad "D19b /$t is byte-for-byte device-files/$s"
    fi
    assert_eq "644" "$(stat -c %a "$CARD/root/$t" 2>/dev/null)" "D19c /$t is 0644"
done

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
echo "   (the only check that catches drift between the two executors)"
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
echo "F. the online path's copy step — the one-of-eight install defect"
# ═══════════════════════════════════════════════════════════════════════════
#
# ⚠️ Group E cannot reach this, and the gap is structural rather than an oversight.
# E compares two --dry-run PLANS; a dry run copies nothing, so the scp step is
# precisely the part of the online path that has no offline counterpart to be
# compared against. That is how the defect shipped past 94 passing cases:
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

# ⚠️ The stub ssh MUST slurp its stdin. That is the behaviour that caused the defect, so a
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
push() {
    RW_SSH="$FW/bin/ssh" RW_SCP="$FW/bin/scp" rw_provision_push_installs "$@"
    local rc=$?
    # ⚠️ Every assertion in this group counts lines in the two stub call logs, and the
    # NEGATIVE one — F10, "it refused before running anything on the device" — is
    # VACUOUS if a log is absent. It does not even fail cleanly: on a missing file
    # `grep -c .` writes nothing to stdout, so
    #     $(( $(grep -c . <absent>) + $(grep -c . <absent>) ))
    # collapses to `$(( + ))`, a bash SYNTAX error whose value is the empty string —
    # and the assertion then reports `want '0', got ''`, a missing fixture wearing a
    # subject defect's clothes. That is not hypothetical: a whole plan entry was
    # opened against this suite on the strength of such a failure, and the suite was
    # fine. So refuse to judge rather than judge on absent evidence. Exit 2 is the
    # harness-error channel tests/run-all.sh reports apart from BOTH pass and fail.
    local f
    for f in "$FW/ssh.calls" "$FW/scp.calls"; do
        [ -f "$f" ] && continue
        echo "HARNESS: a stub call log is absent after a push, so reset_stubs did not run." >&9
        echo "HARNESS: every call-count assertion here would be vacuous. Not judging." >&9
        echo "HARNESS: missing stub call log: $f" >&9
        exit 2
    done
    return $rc
}

PPLAN="$FW/plan"
rw_provision_plan "$RULES" "base usb" > "$PPLAN" || bad "F0 the plan compiles"
NINST=$(awk -F'\t' '$1 == "install"' "$PPLAN" | wc -l | tr -d ' ')
if [ "$NINST" -ge 8 ]; then
    ok "F0 the shipped plan has $NINST install record(s)"
else
    bad "F0 the shipped plan has $NINST install record(s) — expected >= 8, this group is vacuous below that"
fi

# ── F1: the harness can reproduce the defect ─────────────────────────────────
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
# The two that do not exist on a vendor unit — /etc/init.d does, so it proves nothing.
NODIR=0
for d in /opt/roomwizard /usr/local/bin; do
    grep -q "mkdir -p '$d'" "$FW/ssh.calls" || NODIR=$((NODIR + 1))
done
assert_eq 0 "$NODIR" "F8 the two directories a vendor unit lacks are created first"

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

# ── F12b: the bluetooth component's group ─────────────────────────────────────
# bluetooth/build-and-deploy.sh compiles its own group the same way. Its boot link
# must come out of the plan (S91, after S90usb-host: the dongle is on that port) and
# every one of its verbatim files must be copied.
BPLAN="$FW/btplan"
if rw_provision_plan_component "$RULES" bluetooth > "$BPLAN" 2>/dev/null; then
    ok "F12b the bluetooth component plan compiles"
else
    bad "F12b the bluetooth component plan compiles"
fi
assert_eq "6" "$(awk -F'\t' '$1 == "install"' "$BPLAN" | wc -l | tr -d ' ')" \
    "F12b the bluetooth group installs the init script, two dbus policies, main.conf, input.conf and 20-bluealsa.conf"
# CVE-2023-45866: BlueZ 5.66 accepts HID input from an unbonded device unless input.conf
# says otherwise (the default flipped only in 5.71). The record and the uncommented key
# are both needed — a commented-out key is 5.66's own shipped file, which protects nothing.
BT_INPUT_SRC="$(awk -F'\t' '$1 == "install" && $3 == "/etc/bluetooth/input.conf" {print $4}' "$BPLAN")"
assert_eq "device-files/bluetooth-input.conf" "$BT_INPUT_SRC" \
    "F12b the bluetooth group installs /etc/bluetooth/input.conf from device-files/bluetooth-input.conf"
if [[ -n "$BT_INPUT_SRC" ]] && awk '/^\[/ {s=$0} s == "[General]" && /^ClassicBondedOnly=true[[:space:]]*$/ {f=1} END {exit !f}' "$REPO_DIR/$BT_INPUT_SRC" 2>/dev/null; then
    ok "F12b input.conf sets [General] ClassicBondedOnly=true (HID only from bonded devices)"
else
    bad "F12b input.conf sets [General] ClassicBondedOnly=true (HID only from bonded devices)"
fi
assert_eq "../init.d/bluetooth" "$(awk -F'\t' '$1 == "link" && $3 == "/etc/rc5.d/S91bluetooth" {print $4}' "$BPLAN")" \
    "F12b the bluetooth group links S91bluetooth after S90usb-host"

# ── F12c: which bonded devices the init script reconnects ─────────────────────
# BlueZ 5.66 initiates no BR/EDR connection when an adapter powers up, so the init
# script's watcher connects the bonded devices itself, at boot and whenever hci0 is
# re-created. Its list comes from bluetoothd's storage: one adapter's devices that are
# Trusted, not Blocked, and hold a link key. Anything else must stay off it — an
# untrusted or unbonded device would be paged every time, and a device bonded to another
# dongle is not reachable through this one.
BTS="$FW/btstore"; BTA="A0:AD:9F:70:DD:CA"
bt_info() { # <adapter> <device> <general lines> [linkkey]
    mkdir -p "$BTS/$1/$2"
    { printf '[General]\nName=x\n%b\n' "$3"
      [ -z "${4:-}" ] || printf '\n[LinkKey]\nKey=00\nType=4\n'
    } > "$BTS/$1/$2/info"
}
bt_info "$BTA" 11:11:11:11:11:01 'Trusted=true\nBlocked=false' key
bt_info "$BTA" 11:11:11:11:11:02 'Trusted=false\nBlocked=false' key
bt_info "$BTA" 11:11:11:11:11:03 'Trusted=true\nBlocked=false'
bt_info "$BTA" 11:11:11:11:11:04 'Trusted=true\nBlocked=true' key
bt_info "$BTA" 11:11:11:11:11:05 'Trusted=true' key
bt_info 00:00:00:00:00:99 11:11:11:11:11:06 'Trusted=true\nBlocked=false' key
mkdir -p "$BTS/$BTA/cache/11:11:11:11:11:07"; : > "$BTS/$BTA/settings"
BT_CAND=$(BT_STORAGE="$BTS" sh "$REPO_DIR/device-files/bluetooth" candidates "$BTA" 2>/dev/null </dev/null | sort | tr '\n' ' ')
assert_eq "11:11:11:11:11:01 11:11:11:11:11:05 " "$BT_CAND" \
    "F12c the reconnect list is the adapter's trusted, unblocked, bonded devices only"
BT_NONE=$(BT_STORAGE="$BTS" sh "$REPO_DIR/device-files/bluetooth" candidates 22:22:22:22:22:22 2>/dev/null </dev/null)
assert_eq "" "$BT_NONE" "F12c control: an adapter with no storage reconnects nothing"

# ── F13-F14: the summary line accounts for every action ──────────────────────
#
# The old summary read "35 action(s) — 8 install, 9 link, 10 unlink", which accounts
# for 27. backup, touch, the four directives and the two droplines were simply not
# in the breakdown — the kind of arithmetic that hides a verb nobody is executing.
SUM=$(rw_provision_plan_summary "$PPLAN")
STOT=$(printf '%s' "$SUM" | sed 's/ action.*//')
SADD=$(printf '%s' "$SUM" | sed 's/^.*— //' | tr ',' '\n' | awk '{s += $1} END {print s + 0}')
assert_eq "$(grep -c . "$PPLAN")" "$STOT" "F13 the summary's total equals the plan's line count"
assert_eq "$STOT" "$SADD" "F14 the summary's per-type counts add up to its total"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "G. --ssh-auth: the two modes, their preconditions, and the undo"
# ═══════════════════════════════════════════════════════════════════════════
# shellcheck source=../lib/rw-sshd.sh
. "$REPO_DIR/lib/rw-sshd.sh"
GW="$TMP/g"; mkdir -p "$GW/etc/ssh" "$GW/etc/init.d"

PW=$(rw_provision_plan_sshd "$RULES" password)
KY=$(rw_provision_plan_sshd "$RULES" key)
has() { case "$1" in *"$2"*) ok "$3" ;; *) bad "$3" ;; esac; }
hasnt() { case "$1" in *"$2"*) bad "$3" ;; *) ok "$3" ;; esac; }
has   "$PW" "PasswordAuthentication=yes" "G1 password mode keeps PasswordAuthentication yes"
has   "$PW" "PermitRootLogin=yes"        "G1b and PermitRootLogin yes"
hasnt "$PW" "PasswordAuthentication=no"  "G1c and never writes PasswordAuthentication no"
has   "$KY" "PasswordAuthentication=no"  "G2 key mode turns PasswordAuthentication off"
has   "$KY" "KbdInteractiveAuthentication=no" "G2b and keyboard-interactive"
has   "$KY" "PermitRootLogin=prohibit-password" "G2c and lets root in by key only"
hasnt "$KY" "PasswordAuthentication=yes" "G2d and never writes PasswordAuthentication yes"
for P in "$PW" "$KY"; do
    hasnt "$P" "hmac-sha1" "G3 no SHA-1 MAC in either mode"
    hasnt "$P" "umac-64"   "G3b no 64-bit-tag MAC in either mode"
    hasnt "$P" "ssh-rsa,"  "G3c no ssh-rsa signature in either mode"
done
if rw_provision_plan "$RULES" "base sshd sshd-key sshd-password" >/dev/null 2>&1; then
    bad "G4 both auth groups at once are refused"
else
    ok "G4 both auth groups at once are refused"
fi
if rw_provision_plan "$RULES" "base sshd sshd-key" >/dev/null 2>&1; then
    ok "G4b control: one auth group compiles"
else
    bad "G4b control: one auth group compiles"
fi
if rw_provision_ssh_auth_group nonsense >/dev/null 2>&1; then
    bad "G4c an unknown --ssh-auth value is refused"
else
    ok "G4c an unknown --ssh-auth value is refused"
fi

# The vendor file's shape, from the card capture: prose comments that START with
# a keyword ("# Ciphers and keying"), commented-out settings, and one live Ciphers.
vendor_cfg() {
    printf '%s\n' '# Ciphers and keying' '#LoginGraceTime 2m' 'PermitRootLogin yes' \
        '#PubkeyAuthentication yes' '#PasswordAuthentication yes' 'PermitEmptyPasswords yes' \
        'ChallengeResponseAuthentication no' 'UsePAM yes' '#Match User anoncvs' \
        '# Ciphers and keying' 'Ciphers aes128-ctr,aes192-ctr,aes256-ctr'
}
mkcard_ssh() { rm -rf "$1"; mkdir -p "$1"/root/etc/ssh "$1"/data "$1"/log "$1"/backup
               vendor_cfg > "$1/root/etc/ssh/sshd_config"; }
printf '%s\n' "$KY" > "$GW/key.plan"
mkcard_ssh "$GW/off"
rw_provision_apply_offline "$GW/off" "$GW/key.plan" "$REPO_DIR" >/dev/null 2>&1 \
    || bad "G5 the key plan applies offline"
C="$GW/off/root/etc/ssh/sshd_config"
assert_eq "2" "$(grep -c '^# Ciphers and keying$' "$C")" \
    "G5 a prose comment starting with a keyword is left alone (it used to become a 2nd/3rd Ciphers line)"
for k in Ciphers MACs KexAlgorithms PasswordAuthentication PermitRootLogin PubkeyAuthentication; do
    assert_eq "1" "$(grep -c "^$k " "$C")" "G5.$k set exactly once"
done

# Same bytes from both executors, not just the same dry run (group E's limit).
mkdir -p "$GW/on/etc/ssh"; vendor_cfg > "$GW/on/etc/ssh/sshd_config"
rw_provision_online_script > "$GW/online.sh"
RW_PROVISION_ROOT="$GW/on" sh "$GW/online.sh" "$GW/key.plan" >/dev/null 2>&1 \
    || bad "G6 the key plan applies through the online executor"
assert_eq "$(md5sum < "$C")" "$(md5sum < "$GW/on/etc/ssh/sshd_config")" \
    "G6 both executors write byte-identical sshd_config"

# rw_sshd_check_offline against make-fake-card.sh's sshd strings.
bash "$REPO_DIR/tests/make-fake-card.sh" "$GW/fc" >/dev/null 2>&1
FSSHD="$GW/fc/root/usr/sbin/sshd"
if rw_sshd_check_offline "$C" "$FSSHD" "$GW/key.plan" >/dev/null; then
    ok "G7 control: the written config passes the offline check"
else
    bad "G7 control: the written config passes the offline check"
    rw_sshd_check_offline "$C" "$FSSHD" "$GW/key.plan" | sed 's/^/        /'
fi
grep -v 'pubkeyacceptedkeytypes' "$FSSHD" > "$GW/old-sshd"
if rw_sshd_check_offline "$C" "$GW/old-sshd" "$GW/key.plan" >/dev/null; then
    bad "G7b a keyword the card's sshd does not know is refused"
else
    ok "G7b a keyword the card's sshd does not know is refused"
fi
sed 's/hmac-sha2-256-etm@openssh.com,//' "$FSSHD" > "$GW/old-sshd2"
if rw_sshd_check_offline "$C" "$GW/old-sshd2" "$GW/key.plan" >/dev/null; then
    bad "G7c an algorithm the card's sshd does not know is refused"
else
    ok "G7c an algorithm the card's sshd does not know is refused"
fi
cp "$C" "$GW/dup.cfg"; echo 'passwordauthentication yes' >> "$GW/dup.cfg"
if rw_sshd_check_offline "$GW/dup.cfg" "$FSSHD" "$GW/key.plan" >/dev/null; then
    bad "G7d a second spelling of a key (case-insensitive) is refused"
else
    ok "G7d a second spelling of a key (case-insensitive) is refused"
fi
cp "$C" "$GW/match.cfg"; echo 'Match User x' >> "$GW/match.cfg"
if rw_sshd_check_offline "$GW/match.cfg" "$FSSHD" "$GW/key.plan" >/dev/null; then
    bad "G7e an active Match block is refused"
else
    ok "G7e an active Match block is refused"
fi

# rw_sshd_key_installed: key-only offline needs a key written by THIS run.
AK="$GW/ak"; MK="$GW/mark"
rm -f "$AK"; : > "$MK"
if rw_sshd_key_installed "$AK" "$MK" 2>/dev/null; then bad "G8 no authorized_keys: refused"; else ok "G8 no authorized_keys: refused"; fi
echo 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIB operator@host' > "$AK"
touch -d '2000-01-01' "$AK"
if rw_sshd_key_installed "$AK" "$MK" 2>/dev/null; then bad "G8b a key older than this run: refused"; else ok "G8b a key older than this run: refused"; fi
echo 'ssh-dss AAAAB3NzaC1kc3MAAACB operator@host' > "$AK"; touch -d '2099-01-01' "$AK"
if rw_sshd_key_installed "$AK" "$MK" 2>/dev/null; then bad "G8c a DSA-only key (refused by the hardened config): refused"; else ok "G8c a DSA-only key (refused by the hardened config): refused"; fi
echo 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIB operator@host' > "$AK"; touch -d '2099-01-01' "$AK"
if rw_sshd_key_installed "$AK" "$MK" 2>/dev/null; then ok "G8d control: a fresh ed25519 key is accepted"; else bad "G8d control: a fresh ed25519 key is accepted"; fi

# The device-side guard, run as the device would run it, against a stub sshd.
rw_sshd_guard_script > "$GW/guard.sh"
printf '#!/bin/sh\nexit 0\n' > "$GW/sshd-ok"; printf '#!/bin/sh\necho "Bad configuration option: X"\nexit 255\n' > "$GW/sshd-bad"
printf '#!/bin/sh\necho reload >> "%s/reloads"\n' "$GW" > "$GW/etc/init.d/sshd"
chmod +x "$GW/sshd-ok" "$GW/sshd-bad" "$GW/etc/init.d/sshd"
guard() { RW_SSHD_ROOT="$GW" RW_SSHD_BIN="$GW/$1" sh "$GW/guard.sh" "${@:2}"; }
echo 'OLD' > "$GW/etc/ssh/sshd_config"
guard sshd-ok snapshot >/dev/null
echo 'NEW' > "$GW/etc/ssh/sshd_config"
if guard sshd-bad check >/dev/null 2>&1; then bad "G9 sshd -t failing makes check fail"; else ok "G9 sshd -t failing makes check fail"; fi
assert_eq "OLD" "$(cat "$GW/etc/ssh/sshd_config")" "G9b and the previous sshd_config is restored"
echo 'NEW' > "$GW/etc/ssh/sshd_config"
if guard sshd-ok check >/dev/null 2>&1; then ok "G9c control: sshd -t passing keeps the new file"; else bad "G9c control: sshd -t passing keeps the new file"; fi
assert_eq "NEW" "$(cat "$GW/etc/ssh/sshd_config")" "G9d (still NEW)"
guard sshd-ok arm 1 >/dev/null 2>&1
n=0; while [ "$(cat "$GW/etc/ssh/sshd_config")" != OLD ] && [ "$n" -lt 50 ]; do sleep 0.1; n=$((n + 1)); done
assert_eq "OLD" "$(cat "$GW/etc/ssh/sshd_config")" "G9e an unconfirmed reload is undone by the dead-man timer"
echo 'NEW' > "$GW/etc/ssh/sshd_config"
guard sshd-ok arm 1 >/dev/null 2>&1; guard sshd-ok confirm >/dev/null
sleep 1.5
assert_eq "NEW" "$(cat "$GW/etc/ssh/sshd_config")" "G9f control: a confirmed reload is kept"

# rw_sshd_commit_ssh's re-proof, against a scripted probe. The reload is SIGHUP and
# sshd refuses connections while it re-execs (~0.76 s measured), so the first probe
# can say `down` for a config a key login accepts. SCRIPT is the probe's answers in
# order, the last one repeating; ssh itself (check/arm/confirm) is stubbed to pass.
commit_with() {   # SCRIPT -> prints "rc tries", stderr to $GW/commit.err
    (
        echo "$1" | tr ' ' '\n' > "$GW/probe.script"; : > "$GW/probe.n"
        ssh() { return 0; }
        sleep() { :; }
        rw_ssh_probe() {
            local a; for a in "$@"; do
                if [ "$a" = PubkeyAuthentication=no ]; then
                    RW_SSH_LAST_STATE=auth; RW_SSH_LAST_STDERR="root@x: Permission denied (publickey)."; return 1
                fi
            done
            echo x >> "$GW/probe.n"
            RW_SSH_LAST_STATE=$(sed -n "$(wc -l < "$GW/probe.n")p" "$GW/probe.script")
            [ -n "$RW_SSH_LAST_STATE" ] || RW_SSH_LAST_STATE=$(tail -1 "$GW/probe.script")
            case "$RW_SSH_LAST_STATE" in
                ok)   RW_SSH_LAST_STDERR=""; return 0 ;;
                down) RW_SSH_LAST_STDERR="ssh: connect to host x port 22: Connection refused" ;;
                *)    RW_SSH_LAST_STDERR="root@x: Permission denied (publickey)." ;;
            esac
            return 1
        }
        rc=0; rw_sshd_commit_ssh root@x key >/dev/null 2>"$GW/commit.err" || rc=$?
        echo "$rc $(wc -l < "$GW/probe.n")"
    )
}
RW_SSHD_SETTLE_SECS=30
assert_eq "0 3" "$(commit_with 'down down ok')" "G10 refused twice during the reload, then a key login: committed"
assert_eq "1 1" "$(commit_with 'auth')" "G10b control: a refused key is not retried and fails"
if grep -q 'ssh: root@x: Permission denied (publickey)' "$GW/commit.err" && grep -q "'auth' after 1 attempt" "$GW/commit.err"; then
    ok "G10c the failure prints the probe state and ssh's own stderr"
else bad "G10c the failure prints the probe state and ssh's own stderr"; sed 's/^/        /' "$GW/commit.err"; fi
RW_SSHD_SETTLE_SECS=0
assert_eq "1 1" "$(commit_with 'down')" "G10d a server that stays down fails once the settle time is spent"
if grep -q 'Connection refused' "$GW/commit.err"; then ok "G10e and says Connection refused"; else bad "G10e and says Connection refused"; fi
RW_SSHD_SETTLE_SECS=30

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
