#!/bin/bash
#
# rw_clean_test.sh — regression for rw-clean.sh and device-files/clean-rules.conf
#
# Host-only, no device, no SD card, no root. Run it:
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/rw_clean_test.sh"
#
# ── Why the fixture is synthetic ────────────────────────────────────────────
#
# An earlier plan said "run the clean against a copy of partitions.new/". That
# cannot work: both card captures were copied through Windows and contain NO
# SYMLINKS AT ALL — etc/rc0.d … rc6.d and etc/rcS.d are empty directories
# (CLAUDE.md → "Working from this host"). A whitelist regression whose rc5.d is
# empty passes by having nothing to decide, which is the loudest kind of silence.
#
# So the vendor tree here is BUILT, with real symlinks, in $(mktemp -d) — WSL's
# own filesystem, because /mnt/c is DrvFs and cannot hold a symlink either. Its
# rc*.d contents are SYSTEM_ANALYSIS.md#52's measured keep-lists plus invented
# vendor service names that must be swept.
#
# ── What each group of cases is for ────────────────────────────────────────
#
#   A  rw_clean_del's prefix guard. THE case this file exists for: unprefixed,
#      the rules resolve to /usr/lib, /opt and /etc on the dev host.
#   B  the parser. A malformed line is an ERROR: a missing reason is how a
#      delete gets in without anyone having to justify it. Also the construction
#      guarantee — no rule may NAME rc0.d or rc6.d, so they are unreachable
#      rather than merely unvisited.
#   C  plan compilation, including four negative controls — rc0.d/rc6.d appear in
#      NO plan under ANY group selection (they are shutdown, not startup); a
#      DISABLED group's paths become protections, without which --keep-java would
#      be silently undone by the /opt sweep; every plan is asserted to have
#      COMPILED before anything is asserted about its contents; and --remove's
#      plan must differ from --deep-clean's by nothing except the sweeps.
#   D  device-absolute path -> the right one of four offline mounts.
#   E  the whole clean against the synthetic tree. Asserts all three directions:
#      the keeps survived, the unknowns were SWEPT (a rule that deletes nothing
#      passes a test that only checks the keeps), and nothing outside the copy
#      was touched — md5 manifest of a canary tree, taken before and after. Then
#      the three opt-outs: --keep-java, --keep-factory and --remove.
#
# ── Measured against deliberately broken copies ────────────────────────────
#
# Written before the code, and each group checked by breaking the thing it is
# supposed to catch. Re-run them with tests/measure_sabotage.sh — the sabotages
# live in a file because a sed pattern containing \t and [++nk] does not survive
# being quoted through `wsl.exe -e bash -lc`, and one that fails to apply reports
# "0 failed", which reads exactly like a suite that cannot detect the breakage.
#
# Counts out of 148, measured 2026-08-06:
#
#   scope records ignored, i.e. keeps applied and nothing swept     12 fail
#       This is the shape the plan warns about: "a rule that deletes nothing
#       passes a test that only checks the keeps survived."
#   a disabled group's paths no longer protect                       4 fail
#       C15, C17a, E50, E51 — --keep-java would leave the JRE named only by a
#       delete nobody runs, and the /opt sweep would remove it anyway.
#   `factory` reverted to opt-in                                     4 fail
#       C16, C26, E22, E61 — the 2026-08-06 reversal. One default across every
#       flag; --keep-factory is the only way out.
#   --remove given the sweeps too, i.e. made a synonym                6 fail
#   the `scope` records moved back into `base`, same effect           6 fail
#       C21, C22, C27, C29, E62-E64 — --remove must be a SUBSET.
#   the eight named vendor logs dropped, relying on the sweep         4 fail
#       E66-E69. ⚠️ This one failed ZERO cases until those four were added: the
#       sweep covers the logs under --deep-clean, so the gap was invisible in
#       every plan the suite checked. The C11 plan diff found it; a gap a one-off
#       migration script finds and the regression suite does not is a gap that
#       comes back.
#   the guardless del() — setup-device.sh's live executor lifted offline with no
#   base check, plain concatenation                                 11 fail
#       Against the 116-case version, with rm sandboxed by hand; not re-measured
#       because unprefixed it would rm -rf this host's /etc/shadow. A1–A4 each
#       resolved to it; A5 deleted the base directory itself, which is why
#       A11/A12 then "passed" for the wrong reason.
#
# The count at the end includes a check on the harness itself: a file that
# silently ran zero cases reports success just as loudly as one that ran all of
# them.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=../rw-identify.sh
. "$REPO_DIR/rw-identify.sh"
# shellcheck source=../rw-clean.sh
. "$REPO_DIR/rw-clean.sh"

RULES="$REPO_DIR/device-files/clean-rules.conf"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

PASS=0
FAIL=0

ok()  { PASS=$((PASS + 1)); echo -e "  ${GREEN}pass${NC}  $1"; }
bad() { FAIL=$((FAIL + 1)); echo -e "  ${RED}FAIL${NC}  $1"; }

# assert_eq <want> <got> <description>
assert_eq() {
    if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 (want '$1', got '$2')"; fi
}

# exists <path> <description>   /  gone <path> <description>
# -e is false for a DANGLING symlink, which is exactly what an offline tool sees
# (/var/cron/tabs/root points at an absolute path on another partition), so both
# tests are -e OR -L.
exists() { if [ -e "$1" ] || [ -L "$1" ]; then ok "$2"; else bad "$2 — missing: $1"; fi; }
gone()   { if [ -e "$1" ] || [ -L "$1" ]; then bad "$2 — still present: $1"; else ok "$2"; fi; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "A. rw_clean_del — the prefix guard"
echo "   (unprefixed, these rules resolve to /etc, /opt and /usr/lib on THIS host)"
# ═══════════════════════════════════════════════════════════════════════════

# The canary tree. Deliberately laid out like a real root, and deliberately
# OUTSIDE every base used below, so that a guard failure has somewhere visible
# to land instead of only showing up as a passing test.
CANARY="$TMP/canary"
mkdir -p "$CANARY/etc" "$CANARY/opt/openjre-8" "$CANARY/usr/lib"
echo "root:x:0:0" > "$CANARY/etc/shadow"
echo "vendor"     > "$CANARY/opt/openjre-8/release"
echo "so"         > "$CANARY/usr/lib/libX11.so.6"
CANARY_MD5=$(cd "$CANARY" && find . | LC_ALL=C sort | md5sum)

# A base that is a real directory with real content to remove.  Laid out like a
# mounted card — BASE/{root,data,log,backup} — because rw_clean_del resolves a
# device-absolute path through rw_clean_offline_path, so /opt/junk lands under
# BASE/root and not BASE.
B="$TMP/base"
mkdir -p "$B/root/etc/rc5.d" "$B/root/opt/junk" "$B/root/usr/lib" \
         "$B/data" "$B/log" "$B/backup"
: > "$B/root/opt/junk/thing"
: > "$B/root/usr/lib/libXtest.so.6"
: > "$B/root/usr/lib/libkeepme.so.6"

# refuse <base> <path> <description>
refuse() {
    local base="$1" path="$2" desc="$3" out
    if out=$(rw_clean_del "$base" "$path" 2>&1); then
        bad "$desc — rw_clean_del ACCEPTED it${out:+ ($out)}"
    else
        ok "$desc"
    fi
}

refuse ""      /etc/shadow          "A1 empty base is refused"
refuse "/"     /etc/shadow          "A2 base '/' is refused"
refuse "/."    /etc/shadow          "A3 base '/.' is refused (normalises to /)"
refuse "//"    /etc/shadow          "A4 base '//' is refused"
refuse "$B"    ""                   "A5 empty path is refused"
refuse "$B"    "opt/junk"           "A6 relative path is refused"
refuse "$B"    "/../escape"         "A7 leading .. is refused"
refuse "$B"    "/opt/../../escape"  "A8 embedded .. is refused"
refuse "$B"    "/"                  "A9 path '/' is refused (it would delete the whole tree)"
refuse "$TMP/does-not-exist" /etc/shadow "A10 base that is not a directory is refused"

# The positive controls: a guard that refuses everything is invisible.
if rw_clean_del "$B" /opt/junk >/dev/null 2>&1; then
    gone "$B/root/opt/junk" "A11 an ordinary absolute path IS deleted"
else
    bad "A11 an ordinary absolute path IS deleted — rw_clean_del refused it"
fi

if rw_clean_del "$B" '/usr/lib/libX*.so*' >/dev/null 2>&1; then
    gone   "$B/root/usr/lib/libXtest.so.6"  "A12 a glob deletes what it matches"
    exists "$B/root/usr/lib/libkeepme.so.6" "A13 a glob leaves what it does not match"
else
    bad "A12 a glob deletes what it matches — rw_clean_del refused it"
    bad "A13 a glob leaves what it does not match — not reached"
fi

# A path that is not there is NORMAL: a card may already be partly cleaned.
if rw_clean_del "$B" /opt/never-existed >/dev/null 2>&1; then
    ok "A14 a missing path is not an error"
else
    bad "A14 a missing path is not an error — rw_clean_del returned non-zero"
fi

assert_eq "$CANARY_MD5" "$(cd "$CANARY" && find . | LC_ALL=C sort | md5sum)" \
    "A15 the canary tree outside the base is byte-for-byte unchanged"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "B. the data-file parser"
# ═══════════════════════════════════════════════════════════════════════════

if rw_clean_validate "$RULES" >/dev/null; then
    ok "B1 device-files/clean-rules.conf validates"
else
    bad "B1 device-files/clean-rules.conf validates"
    rw_clean_validate "$RULES" | sed 's/^/        /'
fi

RCOUNT=$(rw_clean_parse "$RULES" | grep -c . || true)
if [ "$RCOUNT" -gt 100 ]; then
    ok "B2 the shipped file has $RCOUNT records"
else
    bad "B2 the shipped file has $RCOUNT records (expected > 100 — did the parse silently drop everything?)"
fi

# reject <file-body> <description>   — a conf that must NOT validate
reject() {
    local body="$1" desc="$2" f="$TMP/reject.conf"
    printf '%s\n' "$body" > "$f"
    if rw_clean_validate "$f" >/dev/null 2>&1; then
        bad "$desc — validate ACCEPTED it"
    else
        ok "$desc"
    fi
}

reject "$(printf 'delete\tbase\t/opt/thing')"                "B3 a 3-field line (no reason) is rejected"
reject "$(printf 'nuke\tbase\t/opt/thing\tbecause')"          "B4 an unknown record type is rejected"
reject "$(printf 'delete\tbroswer\t/opt/thing\ttypo')"        "B5 an unknown group is rejected"
reject "$(printf 'delete base /opt/thing spaces not tabs')"   "B6 space-separated fields are rejected"
reject "$(printf 'delete\tbase\topt/thing\tnot absolute')"    "B7 a relative path in the file is rejected"
reject "$(printf 'delete\tbase\t/opt/../etc\tdotdot')"        "B8 a path containing .. is rejected"
reject ""                                                     "B9 a file with no records at all is rejected"
reject "$(printf 'delete\tbase\t/etc/rc6.d/S90reboot\tshutdown')" "B12 no rule may name rc6.d — shutdown, not startup"
reject "$(printf 'scope\tbase\t/etc/rc0.d\tshutdown')"        "B13 nor rc0.d"
reject "$(printf 'delete\tbase\t/usr/*/libX11.so\tmid-glob')"  "B14 a glob outside the last component is rejected (it would silently match nothing)"

# Comments and blanks are not records.
CMT="$TMP/comments.conf"
printf '# a comment\n\n   \n%s\n' "$(printf 'delete\tbase\t/opt/thing\ta reason')" > "$CMT"
assert_eq "1" "$(rw_clean_parse "$CMT" | grep -c . || true)" \
    "B10 comments and blank lines are not records"

# The reason is dropped by the parser but a tab inside it must not shift fields.
TABBY="$TMP/tabby.conf"
printf '%s\n' "$(printf 'delete\tbase\t/opt/thing\ta reason\twith a tab in it')" > "$TABBY"
assert_eq "delete	base	/opt/thing" "$(rw_clean_parse "$TABBY")" \
    "B11 a tab inside the reason does not shift the first three fields"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "C. plan compilation"
# ═══════════════════════════════════════════════════════════════════════════

plan_of() { rw_clean_plan "$RULES" "$1"; }

PLAN_DEFAULT=$(plan_of "$(rw_clean_default_groups)")
# The smallest plan that still sweeps. `scope` records live in the `sweeps` group
# so that --remove can be the same plan without them, so "base" alone no longer
# implies a whitelist sweep — which is the point of the group and the reason this
# is not spelled "base".
PLAN_BASE=$(plan_of "base sweeps")
PLAN_REMOVE=$(plan_of "$(rw_clean_remove_groups)")
PLAN_KEEPJAVA=$(plan_of "base browser snmp mail extras factory sweeps")
PLAN_KEEPFACTORY=$(plan_of "base browser java snmp mail extras sweeps")

has()    { printf '%s\n' "$2" | grep -qxF "$1"; }
expect() { if has "$1" "$2"; then ok "$3"; else bad "$3 — plan has no '$1'"; fi; }
absent() { if has "$1" "$2"; then bad "$3 — plan HAS '$1'"; else ok "$3"; fi; }

# ⚠️ Which part of the count is the harness?
#
# `absent` against an EMPTY plan passes, so a group name this file uses that
# rw-clean.sh does not know compiles to nothing and then reads as a row of
# successes. That is not hypothetical: it is how this group behaved for the whole
# of the pre-change run, where six `absent` cases "passed" against plans that had
# failed to compile. So every plan asserts that it compiled BEFORE anything is
# asserted about its contents.
plan_built() {
    if [ -n "$2" ]; then ok "$1"; else bad "$1 — the plan is EMPTY, so every assertion below it is meaningless"; fi
}
plan_built "C0a the default plan compiles"        "$PLAN_DEFAULT"
plan_built "C0b the base+sweeps plan compiles"    "$PLAN_BASE"
plan_built "C0c --remove's plan compiles"         "$PLAN_REMOVE"
plan_built "C0d --keep-java's plan compiles"      "$PLAN_KEEPJAVA"
plan_built "C0e --keep-factory's plan compiles"   "$PLAN_KEEPFACTORY"

expect "$(printf 'sweep\t/etc/rc5.d')"                     "$PLAN_BASE"    "C1 rc5.d is swept"
expect "$(printf 'sweep\t/etc/rcS.d')"                     "$PLAN_BASE"    "C2 rcS.d is swept"
expect "$(printf 'keep\t/etc/rc5.d\tS50watchdog')"         "$PLAN_BASE"    "C3 the HARDWARE watchdog link is kept"
expect "$(printf 'keep\t/etc/rc5.d\tS09sshd')"             "$PLAN_BASE"    "C4 sshd is kept"
expect "$(printf 'keep\t/etc/rc5.d\tS30avahi-daemon')"     "$PLAN_BASE"    "C5 the avahi link is kept (D8)"
expect "$(printf 'keep\t/etc/rcS.d\tS45mountnfs.sh')"      "$PLAN_BASE"    "C6 S45mountnfs.sh is kept, per the measurement"
expect "$(printf 'keep\t/home/root/data\t*.hig')"          "$PLAN_BASE"    "C7 high scores are kept by glob"
expect "$(printf 'keep\t/home/root/data\tcron')"           "$PLAN_BASE"    "C8 cron's spool root is kept"
expect "$(printf 'truncate\t/home/root/data/cron/tabs/root')" "$PLAN_BASE" "C9 the vendor crontab is truncated, not unlinked"
expect "$(printf 'del\t/home/root/data/websign')"          "$PLAN_BASE"    "C10 websign is deleted (half the D7b fix)"

# ⚠️ THE negative control, the same shape as p1's absence from RW_PART_ROLES:
# rc0.d and rc6.d are shutdown, not startup. They must be unreachable through
# the plan under EVERY group selection, not merely absent from the default one.
RC06_HITS=0
for p in "$PLAN_BASE" "$PLAN_DEFAULT" "$PLAN_KEEPJAVA" "$PLAN_KEEPFACTORY" "$PLAN_REMOVE"; do
    n=$(printf '%s\n' "$p" | grep -c 'rc0\.d\|rc6\.d' || true)
    RC06_HITS=$((RC06_HITS + n))
done
assert_eq "0" "$RC06_HITS" "C11 rc0.d and rc6.d appear in NO plan under ANY group selection"

# A disabled group's paths must become PROTECTIONS, or --keep-java is silently
# undone by the /opt sweep and says nothing.
expect "$(printf 'del\t/opt/openjre-8')"      "$PLAN_DEFAULT"  "C12 the JRE is deleted by default"
absent "$(printf 'keep\t/opt\topenjre-8')"    "$PLAN_DEFAULT"  "C13 and is not also protected"
absent "$(printf 'del\t/opt/openjre-8')"      "$PLAN_KEEPJAVA" "C14 --keep-java drops the JRE delete"
expect "$(printf 'keep\t/opt\topenjre-8')"    "$PLAN_KEEPJAVA" "C15 --keep-java also protects it from the /opt sweep"

# ⚠️ Factory is delete-by-DEFAULT, and this is the inversion of what these three
# cases asserted before 2026-08-06. Choosing to clean a unit of its vendor
# software is a decision, and it is not reversible by a button press: the clean
# disables the factory-reset MECHANISM anyway, so leaving the 472 MB payload
# behind keeps only the ability to undo a commissioning it can no longer perform.
# The gate is the host-side full-card backup, asked for once and up front.
expect "$(printf 'del\t/home/root/backup/factory/sd_rootfs_part.img*')" "$PLAN_DEFAULT" \
    "C16 the 444 MB restore image IS deleted by default"
absent "$(printf 'del\t/home/root/backup/factory/sd_rootfs_part.img*')" "$PLAN_KEEPFACTORY" \
    "C17 --keep-factory drops that delete"
expect "$(printf 'keep\t/home/root/backup/factory\tsd_rootfs_part.img*')" "$PLAN_KEEPFACTORY" \
    "C17a and protects it, so no sweep can take it instead"
expect "$(printf 'keep\t/home/root/backup\tfactory')" "$PLAN_DEFAULT" \
    "C18 the factory DIRECTORY survives either way — it holds the fallback kernel"
expect "$(printf 'keep\t/home/root/backup\tfactory')" "$PLAN_KEEPFACTORY" \
    "C18a in both plans"

# ── --remove is a NAMED GROUP SUBSET of the same plan, not a second list ──────
#
# It means "delete the vendor stacks we have named", where --deep-clean also means
# "and anything the whitelist does not name". So the one difference is `sweeps`.
absent "$(printf 'sweep\t/etc/rc5.d')"        "$PLAN_REMOVE" "C21 --remove does not sweep rc5.d"
absent "$(printf 'sweep\t/opt')"              "$PLAN_REMOVE" "C22 nor /opt"
expect "$(printf 'sweep\t/opt')"              "$PLAN_DEFAULT" "C23 --deep-clean does"
expect "$(printf 'del\t/opt/openjre-8')"      "$PLAN_REMOVE" "C24 --remove still deletes the named stacks"
expect "$(printf 'del\t/usr/share/cjkfont')"  "$PLAN_REMOVE" "C25 including the browser group"
expect "$(printf 'del\t/home/root/backup/factory/sd_rootfs_part.img*')" "$PLAN_REMOVE" \
    "C26 and the factory payload — ONE default across every flag, not a softer one here"
SWEEPS_IN_REMOVE=$(printf '%s\n' "$PLAN_REMOVE" | grep -c '^sweep' || true)
assert_eq "0" "$SWEEPS_IN_REMOVE" "C27 --remove's plan contains no sweep at all"
SWEEPS_IN_DEFAULT=$(printf '%s\n' "$PLAN_DEFAULT" | grep -c '^sweep' || true)
if [ "$SWEEPS_IN_DEFAULT" -ge 8 ]; then
    ok "C28 --deep-clean's plan contains $SWEEPS_IN_DEFAULT sweeps"
else
    bad "C28 --deep-clean's plan contains only $SWEEPS_IN_DEFAULT sweeps (expected >= 8)"
fi
# The two lists must differ ONLY by the sweeps, or "one plan, one subset" is a
# story rather than a fact.
ONLY_SWEEPS=$(diff <(printf '%s\n' "$PLAN_DEFAULT" | grep -v '^sweep' | LC_ALL=C sort) \
                   <(printf '%s\n' "$PLAN_REMOVE"  | grep -v '^sweep' | LC_ALL=C sort) | grep -c '^[<>]' || true)
assert_eq "0" "$ONLY_SWEEPS" "C29 --remove and --deep-clean differ by NOTHING except the sweeps"

# base cannot be switched off.
if rw_clean_plan "$RULES" "browser java" >/dev/null 2>&1; then
    bad "C19 a group list without 'base' is refused"
else
    ok "C19 a group list without 'base' is refused"
fi
if rw_clean_plan "$RULES" "base bogus" >/dev/null 2>&1; then
    bad "C20 an unknown group name is refused"
else
    ok "C20 an unknown group name is refused"
fi

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "D. device-absolute path -> the right one of four offline mounts"
# ═══════════════════════════════════════════════════════════════════════════

assert_eq "/mnt/x/root/etc/rc5.d"        "$(rw_clean_offline_path /mnt/x /etc/rc5.d)"              "D1 /etc -> p6"
assert_eq "/mnt/x/data/websign"          "$(rw_clean_offline_path /mnt/x /home/root/data/websign)" "D2 data -> p2"
assert_eq "/mnt/x/log/Xorg.0.log"        "$(rw_clean_offline_path /mnt/x /home/root/log/Xorg.0.log)" "D3 log -> p3"
assert_eq "/mnt/x/backup/factory"        "$(rw_clean_offline_path /mnt/x /home/root/backup/factory)" "D4 backup -> p5"
# The longest-prefix case: root's device path is "/", which prefixes everything,
# and /home/root is NOT one of the three mount points.
assert_eq "/mnt/x/root/home/root/.ssh"   "$(rw_clean_offline_path /mnt/x /home/root/.ssh)"         "D5 /home/root/.ssh -> p6, not p2"
assert_eq "/mnt/x/root/home/rootless"    "$(rw_clean_offline_path /mnt/x /home/rootless)"          "D6 a prefix match on a partial component does not count"
assert_eq "/mnt/x/data"                  "$(rw_clean_offline_path /mnt/x /home/root/data)"          "D7 the mount point itself maps to the mount"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "E. the whole clean, against a synthetic vendor card"
# ═══════════════════════════════════════════════════════════════════════════

CARD="$TMP/card"

# The links a real unit has, from SYSTEM_ANALYSIS.md#52 — measured on a unit in
# service and re-read on a second unit, 2026-08-05.
KEEP_RCS="S02banner.sh S03sysfs.sh S04udev S05modutils.sh S06alignment.sh S06devpts.sh
          S10checkroot.sh S30procps.sh S30ramdisk S35mountall.sh S37populate-volatile.sh
          S39hostname.sh S40networking S43syslog S45mountnfs.sh S55bootmisc.sh S99finish.sh"
KEEP_RC5="S02dbus-1 S09sshd S20cron S28time-sync S29audio-enable S30avahi-daemon
          S40ctrlblk S50watchdog S89xpad-modules S90usb-host S99roomwizard-app"
KEEP_RC24="S02dbus-1 S09sshd S20hwclock.sh S40ctrlblk S50watchdog S99roomwizard-app S99stop-bootlogd"

# Vendor services that must be SWEPT. The first eight are real names read off a
# stock card's /etc/init.d; the last three are invented, which is the point —
# an unrecognised vendor service on a unit nobody has inspected must go without
# anyone adding a blacklist entry for it.
SWEEP_RC5="S15webserver S17jetty S18hsqldb S19browser S21cursor.sh S22x11 S23snmpd S24vsftpd
           S25nullmailer S26startautoupgrade S27webmonitor S75bootscrub S80cleaup_partition
           S99rmnologin.sh S45roomcast S46steelcase-telemetryd S47asl-presenced"
SWEEP_RCS="S01psplash S58wpa_supplicant S60networkmanager S59rwbootstrap"

build_card() {
    rm -rf "$CARD"
    mkdir -p "$CARD/root/etc/init.d" "$CARD/root/etc/rcS.d" "$CARD/root/etc/rc5.d" \
             "$CARD/root/etc/rc0.d" "$CARD/root/etc/rc6.d" \
             "$CARD/root/etc/rc2.d" "$CARD/root/etc/rc3.d" "$CARD/root/etc/rc4.d" \
             "$CARD/root/etc/ssh" "$CARD/root/etc/network" "$CARD/root/etc/sysctl.d" \
             "$CARD/root/var/cron/tabs" "$CARD/root/var/lib" \
             "$CARD/root/usr/lib" "$CARD/root/usr/share" "$CARD/root/usr/sbin" \
             "$CARD/root/home/root/.ssh" \
             "$CARD/data/lost+found" "$CARD/data/cron/tabs" \
             "$CARD/log/lost+found" "$CARD/backup/lost+found" "$CARD/backup/factory"

    # rw_is_rootfs's markers, so rw_check_card_mounts is satisfiable.
    : > "$CARD/root/etc/shadow"
    : > "$CARD/root/etc/hosts"
    : > "$CARD/root/etc/ssh/sshd_config"
    : > "$CARD/root/etc/network/interfaces"
    echo 'SteelCase RW20 Embedded Platform (Yocto) 3.1.4 \n \l' > "$CARD/root/etc/issue"

    # Real symlinks, ../init.d/<name> like the vendor's, with a target to reach.
    local d n
    for n in $KEEP_RCS $KEEP_RC5 $KEEP_RC24 $SWEEP_RC5 $SWEEP_RCS; do
        : > "$CARD/root/etc/init.d/${n#S[0-9][0-9]}"
    done
    for n in $KEEP_RCS; do ln -s "../init.d/${n#S[0-9][0-9]}" "$CARD/root/etc/rcS.d/$n"; done
    for n in $SWEEP_RCS; do ln -s "../init.d/${n#S[0-9][0-9]}" "$CARD/root/etc/rcS.d/$n"; done
    for n in $KEEP_RC5;  do ln -s "../init.d/${n#S[0-9][0-9]}" "$CARD/root/etc/rc5.d/$n"; done
    for n in $SWEEP_RC5; do ln -s "../init.d/${n#S[0-9][0-9]}" "$CARD/root/etc/rc5.d/$n"; done
    for d in rc2.d rc3.d rc4.d; do
        for n in $KEEP_RC24;  do ln -s "../init.d/${n#S[0-9][0-9]}" "$CARD/root/etc/$d/$n"; done
        for n in $SWEEP_RC5;  do ln -s "../init.d/${n#S[0-9][0-9]}" "$CARD/root/etc/$d/$n"; done
    done
    # Shutdown links. Nothing may touch these.
    for n in K09sshd K20dbus-1 K85watchdog S20sendsigs S25save-rtc.sh S40umountfs S90halt; do
        ln -s "../init.d/halt" "$CARD/root/etc/rc0.d/$n"
    done
    for n in K09sshd K20dbus-1 K85watchdog S20sendsigs S25save-rtc.sh S40umountfs S90reboot; do
        ln -s "../init.d/reboot" "$CARD/root/etc/rc6.d/$n"
    done
    : > "$CARD/root/etc/init.d/halt"
    : > "$CARD/root/etc/init.d/reboot"

    # /opt: the stock set (measured in the card capture) plus ours plus an
    # invented vendor directory.
    mkdir -p "$CARD/root/opt/hsqldb" "$CARD/root/opt/jetty-9-4-11" "$CARD/root/opt/openjre-8" \
             "$CARD/root/opt/pv02" "$CARD/root/opt/sbin/watchdog" "$CARD/root/opt/sound" \
             "$CARD/root/opt/games" "$CARD/root/opt/roomwizard" "$CARD/root/opt/vnc_client" \
             "$CARD/root/opt/rwconnector"
    : > "$CARD/root/opt/sbin/watchdog/watchdog.sh"
    : > "$CARD/root/opt/sbin/networkmanager"
    : > "$CARD/root/opt/sound/asl_click.wav"
    : > "$CARD/root/opt/games/snake"

    # The base-OS stacks.
    mkdir -p "$CARD/root/usr/share/cjkfont" "$CARD/root/usr/share/snmp" "$CARD/root/usr/lib/ts"
    : > "$CARD/root/usr/lib/libwebkit2gtk-4.0.so.37"
    : > "$CARD/root/usr/lib/libicuuc.so.66"
    : > "$CARD/root/usr/lib/libc.so.6"
    : > "$CARD/root/usr/sbin/snmpd"
    : > "$CARD/root/usr/sbin/wpa_supplicant"
    : > "$CARD/root/usr/sbin/avahi-daemon"
    : > "$CARD/root/usr/sbin/sshd"
    # /var/log is a SYMLINK to /home/root/log on a real card (measured on a unit
    # in service). Offline, with p3 mounted elsewhere, it DANGLES — which is the
    # case a naive `[ -e ]` gets wrong, so the fixture has to carry it.
    ln -s /home/root/log "$CARD/root/var/log"

    # p2, including the crontab reached through the symlink on p6.
    printf '# vendor crontab\n*/5 * * * * /opt/sbin/watchdog/watchdog.sh\n' \
        > "$CARD/data/cron/tabs/root"
    printf 'log noise\n' > "$CARD/data/cron/log"
    ln -s /home/root/data/cron/tabs/root "$CARD/root/var/cron/tabs/root"
    mkdir -p "$CARD/data/websign" "$CARD/data/roombooker" "$CARD/data/selftest" \
             "$CARD/data/frontpanel" "$CARD/data/rwmeetingcache"
    echo dhcp > "$CARD/data/websign/net.mode"
    : > "$CARD/data/test.hex"
    : > "$CARD/data/snake.hig"
    : > "$CARD/data/tetris.hig"

    # p3 and p5.
    : > "$CARD/log/Xorg.0.log"
    : > "$CARD/log/browser.err"
    : > "$CARD/log/networkmngr.err"
    : > "$CARD/log/snmp_daemon.log"
    printf 'vendor noise\n' > "$CARD/log/messages"
    printf 'wtmp\n'         > "$CARD/log/wtmp"
    mkdir -p "$CARD/log/jetty_logs"
    : > "$CARD/backup/serialno"
    : > "$CARD/backup/pointercal"
    : > "$CARD/backup/factory/uImage-system-original"
    : > "$CARD/backup/factory/sd_rootfs_part.img"
    : > "$CARD/backup/factory/sd_rootfs_part.img.md5"
    mkdir -p "$CARD/backup/websigns" "$CARD/backup/usagereports" "$CARD/backup/logs"
}

build_card

# rw-identify.sh's own sanity check must pass on the fixture, or every later
# assertion is about the wrong tree.
if rw_check_card_mounts "$CARD" >/dev/null; then
    ok "E1 the synthetic card passes rw_check_card_mounts"
else
    bad "E1 the synthetic card passes rw_check_card_mounts"
    rw_check_card_mounts "$CARD" | sed 's/^/        /'
fi

# ── E: dry run first. It must print, and must delete nothing. ──────────────
CARD_MD5_BEFORE=$(cd "$CARD" && find . | LC_ALL=C sort | md5sum)
rw_clean_plan "$RULES" "$(rw_clean_default_groups)" > "$TMP/plan"
DRY_OUT=$(RW_CLEAN_DRY=1 rw_clean_apply "$CARD" "$TMP/plan")

assert_eq "$CARD_MD5_BEFORE" "$(cd "$CARD" && find . | LC_ALL=C sort | md5sum)" \
    "E2 --dry-run deletes nothing at all"

DRY_LINES=$(printf '%s\n' "$DRY_OUT" | grep -c 'would ' || true)
if [ "$DRY_LINES" -gt 30 ]; then
    ok "E3 --dry-run printed $DRY_LINES resolved paths"
else
    bad "E3 --dry-run printed only $DRY_LINES resolved paths (expected > 30)"
fi
# Every path it printed must be absolute AND under the base — that is the whole
# claim of a dry run.
NOT_UNDER=$(printf '%s\n' "$DRY_OUT" | awk -v b="$CARD/" '/would /{p=$NF; if (index(p,b)!=1) print p}' | grep -c . || true)
assert_eq "0" "$NOT_UNDER" "E4 every path the dry run printed is absolute and under the base"

# ── E: for real. ───────────────────────────────────────────────────────────
APPLY_OUT=$(rw_clean_apply "$CARD" "$TMP/plan")

echo ""
echo "   the keeps survived"
MISS=0
for n in $KEEP_RCS; do [ -L "$CARD/root/etc/rcS.d/$n" ] || { MISS=$((MISS+1)); echo "        lost rcS.d/$n"; }; done
for n in $KEEP_RC5; do [ -L "$CARD/root/etc/rc5.d/$n" ] || { MISS=$((MISS+1)); echo "        lost rc5.d/$n"; }; done
for d in rc2.d rc3.d rc4.d; do
    for n in $KEEP_RC24; do [ -L "$CARD/root/etc/$d/$n" ] || { MISS=$((MISS+1)); echo "        lost $d/$n"; }; done
done
assert_eq "0" "$MISS" "E5 all $(echo $KEEP_RCS $KEEP_RC5 | wc -w) rcS.d/rc5.d keeps plus rc2-4.d survived"

exists "$CARD/root/opt/sbin/networkmanager" "E6 /opt/sbin survives (the reference material F5 needs)"
exists "$CARD/root/opt/sound/asl_click.wav" "E7 /opt/sound survives (the UI WAVs)"
exists "$CARD/root/opt/pv02"                "E8 /opt/pv02 survives"
exists "$CARD/root/opt/games/snake"         "E9 /opt/games survives"
exists "$CARD/root/opt/vnc_client"          "E10 /opt/vnc_client survives"
exists "$CARD/root/usr/lib/libc.so.6"       "E11 libc survives — no rule may reach /lib or /usr/lib wholesale"
exists "$CARD/root/usr/sbin/sshd"           "E12 sshd survives"
exists "$CARD/root/usr/sbin/avahi-daemon"   "E13 avahi-daemon survives (D8: the deep clean used to delete it)"
exists "$CARD/data/snake.hig"               "E14 high scores survive"
exists "$CARD/data/cron/tabs/root"          "E15 the crontab file still exists"
exists "$CARD/data/lost+found"              "E16 p2 lost+found survives"
exists "$CARD/log/lost+found"               "E17 p3 lost+found survives"
exists "$CARD/backup/lost+found"            "E18 p5 lost+found survives"
exists "$CARD/backup/serialno"              "E19 serialno survives (device identity)"
exists "$CARD/backup/pointercal"            "E20 the vendor touch calibration survives"
exists "$CARD/backup/factory/uImage-system-original" "E21 the fallback kernel survives"
gone   "$CARD/backup/factory/sd_rootfs_part.img"     "E22 the restore image is GONE by default"

assert_eq "" "$(cat "$CARD/data/cron/tabs/root")" "E23 the vendor crontab was TRUNCATED"
assert_eq "" "$(cat "$CARD/data/cron/log")"       "E24 the 131 MB cron log was truncated"
exists "$CARD/root/var/cron/tabs/root"            "E25 and /var/cron/tabs/root still points at it"

# syslogd holds these open (measured from /proc/<pid>/fd on a unit in service), so
# unlinking them live leaves it writing to an unlinked inode and logging stops.
exists "$CARD/log/messages"                       "E25a syslog's messages survives the p3 sweep"
assert_eq "" "$(cat "$CARD/log/messages")"        "E25b and was truncated instead"
assert_eq "" "$(cat "$CARD/log/wtmp")"            "E25c wtmp likewise"
# The dangling /var/log symlink must be left alone, not "cleaned up" as broken.
if [ -L "$CARD/root/var/log" ]; then
    ok "E25d the dangling /var/log -> /home/root/log symlink is untouched"
else
    bad "E25d the dangling /var/log -> /home/root/log symlink is untouched"
fi

echo ""
echo "   the unknowns were swept"
SURV=0
for n in $SWEEP_RC5; do [ -L "$CARD/root/etc/rc5.d/$n" ] && { SURV=$((SURV+1)); echo "        survived rc5.d/$n"; }; done
for n in $SWEEP_RCS; do [ -L "$CARD/root/etc/rcS.d/$n" ] && { SURV=$((SURV+1)); echo "        survived rcS.d/$n"; }; done
for d in rc2.d rc3.d rc4.d; do
    for n in $SWEEP_RC5; do [ -L "$CARD/root/etc/$d/$n" ] && { SURV=$((SURV+1)); echo "        survived $d/$n"; }; done
done
assert_eq "0" "$SURV" "E26 every vendor boot link was swept, including the three invented names"

gone "$CARD/root/opt/rwconnector"         "E27 an invented /opt vendor directory was swept"
gone "$CARD/root/opt/openjre-8"           "E28 the JRE is gone"
gone "$CARD/root/opt/jetty-9-4-11"        "E29 Jetty is gone"
gone "$CARD/root/opt/hsqldb"              "E30 HSQLDB is gone"
gone "$CARD/root/usr/lib/libwebkit2gtk-4.0.so.37" "E31 WebKit is gone"
gone "$CARD/root/usr/lib/libicuuc.so.66"  "E32 ICU is gone"
gone "$CARD/root/usr/share/cjkfont"       "E33 the CJK font is gone"
gone "$CARD/root/usr/share/snmp"          "E34 the SNMP MIBs are gone"
gone "$CARD/root/usr/sbin/snmpd"          "E35 snmpd is gone"
gone "$CARD/root/usr/sbin/wpa_supplicant" "E36 wpa_supplicant is gone (no WiFi on this board)"
gone "$CARD/root/usr/lib/ts"              "E37 tslib is gone"
gone "$CARD/log/browser.err"              "E38 stale vendor logs on p3 are swept"
gone "$CARD/data/websign"                 "E39 websign is gone — this is what removes D7b's window"
gone "$CARD/data/rwmeetingcache"          "E40 an invented p2 vendor directory was swept"
gone "$CARD/data/test.hex"                "E41 the factory burn-in pattern is gone"
gone "$CARD/log/Xorg.0.log"               "E42 p3 was swept"
gone "$CARD/log/jetty_logs"               "E43 p3 subdirectories were swept"
gone "$CARD/backup/websigns"              "E44 p5 was swept"
gone "$CARD/backup/usagereports"          "E45 vendor telemetry is gone"

# The harness check the plan warns about: "a rule that deletes nothing passes a
# test that only checks the keeps survived."
DELETED=$(printf '%s\n' "$APPLY_OUT" | grep -c 'delete\|truncat' || true)
if [ "$DELETED" -gt 40 ]; then
    ok "E46 the run reported $DELETED deletions — it did not pass by doing nothing"
else
    bad "E46 the run reported only $DELETED deletions (expected > 40)"
fi

echo ""
echo "   rc0.d and rc6.d, and everything outside the card"
RC0=$(ls "$CARD/root/etc/rc0.d" | LC_ALL=C sort | tr '\n' ' ')
RC6=$(ls "$CARD/root/etc/rc6.d" | LC_ALL=C sort | tr '\n' ' ')
assert_eq "K09sshd K20dbus-1 K85watchdog S20sendsigs S25save-rtc.sh S40umountfs S90halt " "$RC0" \
    "E47 rc0.d is untouched — it carries umountfs, sendsigs and save-rtc.sh"
assert_eq "K09sshd K20dbus-1 K85watchdog S20sendsigs S25save-rtc.sh S40umountfs S90reboot " "$RC6" \
    "E48 rc6.d is untouched"

assert_eq "$CANARY_MD5" "$(cd "$CANARY" && find . | LC_ALL=C sort | md5sum)" \
    "E49 the canary tree outside the card is still byte-for-byte unchanged"

# ── E: the opt-outs, on a fresh card ──────────────────────────────────────
echo ""
echo "   --keep-java, --keep-factory and --remove"
build_card
rw_clean_plan "$RULES" "base browser snmp mail extras factory sweeps" > "$TMP/plan.keepjava"
rw_clean_apply "$CARD" "$TMP/plan.keepjava" >/dev/null
exists "$CARD/root/opt/openjre-8"   "E50 --keep-java keeps the JRE through the /opt sweep"
exists "$CARD/root/opt/jetty-9-4-11" "E51 --keep-java keeps Jetty through the /opt sweep"
gone   "$CARD/root/opt/rwconnector" "E52 and the sweep still removes what no group protects"
gone   "$CARD/root/usr/share/cjkfont" "E53 and the other groups still ran"

build_card
rw_clean_plan "$RULES" "base browser java snmp mail extras sweeps" > "$TMP/plan.keepfactory"
rw_clean_apply "$CARD" "$TMP/plan.keepfactory" >/dev/null
exists "$CARD/backup/factory/sd_rootfs_part.img"     "E54 --keep-factory keeps the 444 MB image"
exists "$CARD/backup/factory/sd_rootfs_part.img.md5" "E55 and its .md5"
exists "$CARD/backup/factory/uImage-system-original" "E56 and the fallback kernel either way"
gone   "$CARD/root/opt/rwconnector"                  "E57 and the rest of the clean still ran"

# ── E: --remove, the named-subset run ─────────────────────────────────────
#
# The whole difference from --deep-clean is the sweeps: the named vendor stacks go,
# and a vendor directory nobody has whitelisted STAYS. That second half is the
# assertion that --remove is a subset rather than a synonym.
build_card
rw_clean_plan "$RULES" "$(rw_clean_remove_groups)" > "$TMP/plan.remove"
REMOVE_OUT=$(rw_clean_apply "$CARD" "$TMP/plan.remove")
gone   "$CARD/root/opt/openjre-8"          "E58 --remove deletes the JRE"
gone   "$CARD/root/usr/share/cjkfont"      "E59 and the CJK font"
gone   "$CARD/data/websign"                "E60 and websign"
gone   "$CARD/backup/factory/sd_rootfs_part.img" \
    "E61 and the factory payload — the same default as --deep-clean, not a softer one"
exists "$CARD/root/opt/rwconnector"        "E62 but an unwhitelisted /opt vendor directory SURVIVES --remove"
exists "$CARD/root/etc/rc5.d/S45roomcast" "E63 and so does an unwhitelisted boot link"
exists "$CARD/data/rwmeetingcache"         "E64 and an unwhitelisted p2 directory"
exists "$CARD/backup/factory/uImage-system-original" "E65 the fallback kernel survives --remove too"

# ⚠️ The eight NAMED vendor logs must go under --remove as well.
#
# `scope /home/root/log` would sweep every one of them, but that scope is in the
# `sweeps` group and --remove runs without it — so relying on the sweep alone
# leaves the vendor's logs on a --remove'd device, which the heredoc --remove used
# to be did not. Measured: deleting those eight rules and relying on the sweep
# fails ZERO cases before these four exist, and the C11 plan diff was the only
# thing that caught it. A gap a one-off migration script finds and the regression
# suite does not is a gap that comes back.
gone "$CARD/log/browser.err"        "E66 --remove deletes the named vendor logs, not just the sweep"
gone "$CARD/log/Xorg.0.log"         "E67 including Xorg's"
gone "$CARD/log/jetty_logs"         "E68 and Jetty's log directory"
gone "$CARD/log/networkmngr.err"    "E69 and the network regenerator's"
exists "$CARD/log/messages"         "E70 but syslog's own messages still survives --remove"
assert_eq "" "$(cat "$CARD/log/messages")" "E71 and was truncated instead of unlinked"

REMOVE_DELS=$(printf '%s\n' "$REMOVE_OUT" | grep -c 'delete\|truncat' || true)
if [ "$REMOVE_DELS" -gt 15 ]; then
    ok "E72 --remove reported $REMOVE_DELS deletions — it did not pass by doing nothing"
else
    bad "E72 --remove reported only $REMOVE_DELS deletions (expected > 15)"
fi

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "════════════════════════════════════════"
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed, $TOTAL total"

# A file that silently ran zero cases reports success as loudly as one that ran
# all of them.
if [ "$TOTAL" -lt 100 ]; then
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
