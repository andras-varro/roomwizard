#!/bin/bash
#
# commission_prep_test.sh — regression for commissioning/card-prep.sh's two
#                           host-side decisions
#
# Host-only, no device, no SD card, no root. Run it:
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/commission_prep_test.sh"
#
# WHY THESE TWO, and why a test rather than a look:
#
#   rw_ssh_operator_home   Under sudo, $HOME is /root, so the offline flow looked for
#                     the operator's SSH public key in /root/.ssh and never found
#                     it — on hosts that had one. The bug is invisible in a
#                     standalone run (where $HOME is right) and invisible in a
#                     code read (where $HOME looks right), which is exactly the
#                     shape that needs a case per environment.
#
#   the next-steps    commissioning/card-prep.sh ends by reading COMMISSIONING.md's
#   suppression       NEXT_STEPS block aloud: boot the unit, find its IP, run
#                     commissioning/provision.sh, run deploy-all.sh. Correct standalone,
#                     WRONG under commissioning/commission-offline.sh, which has already done
#                     all of it — and the banner says "Complete!" with three
#                     phases still to run.
#
#                     ⚠️ The case that earns its keep is case 8: ROOTFS set and
#                     the flag NOT set must STILL print the next steps. ROOTFS is
#                     the documented standalone escape hatch for a hand-mounted
#                     card, so suppressing on ROOTFS — the obvious fix, and the
#                     wrong one — would silence the instructions for the one
#                     operator who most needs them. A test that only checks the
#                     flag cannot tell the two implementations apart.
#
# HOW: neither behaviour can be reached by running the script, which prompts for a
# password from its second step and needs a mounted rootfs. Both are therefore
# extracted from the real file by line range and run in isolation — so the thing
# under test is the shipped source, not a copy of it that can drift.
#
# The count at the end includes a check on the harness itself: a test file that
# silently ran zero cases reports success just as loudly as one that ran all of
# them.
#
# MEASURED AGAINST BROKEN COPIES, 2026-08-09 — 41 pass clean, and:
#
#   verdict: unknown websign treated as inert          3 failed
#   non-TTY answers "d" (deletes with nobody asked)    1 failed
#   disable also deletes /etc/init.d/networkmanager    1 failed
#   the host-root-disk veto written as a && list       1 failed
#   p2 mounted read-write instead of -o ro             1 failed
#   menu item 1 relabelled back to the incident's      1 failed
#
# ⚠️ Two of those took a second attempt, both worth knowing. The
# init.d sabotage's `sed` pattern contains `||`, so a `|`-delimited s/// silently
# did not apply and the suite reported "0 failed" — the exact signature of a
# check that cannot fail. Put the pattern in a file and assert it applied. And
# the non-TTY sabotage originally HUNG: the sabotaged path calls the real `sudo`
# with stdin closed. `make_step7b` stubs `sudo` for that reason and no other.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$REPO_DIR/commissioning/card-prep.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

PASS=0
FAIL=0
SKIP=0

ok()      { PASS=$((PASS + 1)); echo -e "  ${GREEN}pass${NC}  $1"; }
bad()     { FAIL=$((FAIL + 1)); echo -e "  ${RED}FAIL${NC}  $1"; }
skipped() { SKIP=$((SKIP + 1)); echo -e "  ${YELLOW}skip${NC}  $1 — $2"; }

[ -f "$SRC" ] || { echo -e "  ${RED}HARNESS ERROR${NC}: $SRC not found"; exit 2; }

TMP=$(mktemp -d /tmp/rw-prep-test.XXXXXX)
trap 'rm -rf "$TMP"' EXIT

# ── extract rw_ssh_operator_home() from the real file ───────────────────────
#
# By its own definition boundaries, so the test cannot silently run against a
# stale inline copy. If the function is renamed or deleted, this fails loudly
# rather than testing nothing.
#
# ⚠️ It lives in lib/rw-ssh.sh, not in card-prep.sh: rw_ssh_keygen needs the same
# answer, and two copies of "whose home" is how the original bug got in.
# What stays card-prep.sh's own is the CALL SITE, which is
# what cases 5-6 and the end-to-end block below check.
LIB="$REPO_DIR/lib/rw-ssh.sh"
[ -f "$LIB" ] || { echo -e "  ${RED}HARNESS ERROR${NC}: $LIB not found"; exit 2; }
sed -n '/^rw_ssh_operator_home() {$/,/^}$/p' "$LIB" > "$TMP/operator_home.sh"
if [ ! -s "$TMP/operator_home.sh" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: rw_ssh_operator_home() not found in lib/rw-ssh.sh"
    echo "  It was renamed or removed. Update this test, do not delete the case."
    exit 2
fi
# shellcheck source=/dev/null
. "$TMP/operator_home.sh"
operator_home() { rw_ssh_operator_home; }

echo ""
echo "── operator_home: whose ~/.ssh the key is looked for in ─────────────────"

# expect_home <want> <desc>   — SUDO_USER/HOME are set by the caller
expect_home() {
    local want="$1" desc="$2" got
    got=$(operator_home)
    if [ "$got" = "$want" ]; then
        ok "$desc"
    else
        bad "$desc (wanted $want, got $got)"
    fi
}

# expect_out <yes|no> <needle> <file> <desc>
expect_out() {
    local want="$1" needle="$2" file="$3" desc="$4" got
    if grep -qF -- "$needle" "$file"; then got=yes; else got=no; fi
    if [ "$got" = "$want" ]; then
        ok "$desc"
    else
        bad "$desc (wanted $want, got $got for '$needle')"
    fi
}

# The cases run in THIS shell, with $HOME/$SUDO_USER saved and restored around
# them, rather than in a subshell per case: a subshell cannot increment the
# counters, so a case that failed there would print FAIL and still be counted as
# nothing at all.
REAL_HOME="$HOME"
REAL_SUDO_USER="${SUDO_USER:-}"

# 1. No sudo at all: $HOME is the operator's own and must be used unchanged.
unset SUDO_USER
HOME=/home/nobody-in-particular
expect_home "/home/nobody-in-particular" "standalone run (no SUDO_USER) uses \$HOME"

# 2. sudo from a root login: SUDO_USER=root names no separate home.
SUDO_USER=root
expect_home "/home/nobody-in-particular" "SUDO_USER=root falls back to \$HOME"

# 3. A name that resolves to nothing must fall back rather than invent a path.
#    /home/<user> is a guess, and a wrong one for a system account.
SUDO_USER="rw-no-such-user-$$"
expect_home "/home/nobody-in-particular" "unresolvable SUDO_USER falls back to \$HOME"

# 4. The case the bug was: a real invoking user, $HOME pointing at root's.
#    This is the only one that distinguishes the fix from the defect.
if command -v getent >/dev/null 2>&1; then
    ME="$(id -un)"
    MY_HOME="$(getent passwd "$ME" 2>/dev/null | cut -d: -f6)"
    if [ -n "$MY_HOME" ] && [ -d "$MY_HOME" ] && [ "$ME" != "root" ]; then
        SUDO_USER="$ME"
        HOME=/root
        expect_home "$MY_HOME" "under sudo, SUDO_USER's home wins over /root"
    else
        skipped "under sudo, SUDO_USER's home wins over /root" \
                "no non-root user with a resolvable home on this host"
    fi
else
    skipped "under sudo, SUDO_USER's home wins over /root" "getent absent"
fi

HOME="$REAL_HOME"
if [ -n "$REAL_SUDO_USER" ]; then SUDO_USER="$REAL_SUDO_USER"; else unset SUDO_USER; fi

# 5. Static: the defect was a literal "$HOME/.ssh". Its absence is not proof the
#    fix is right, but its presence is proof something reintroduced it.
if grep -q '\$HOME/\.ssh' "$SRC"; then
    bad "no bare \$HOME/.ssh remains in commissioning/card-prep.sh"
else
    ok "no bare \$HOME/.ssh remains in commissioning/card-prep.sh"
fi

# ── the key lookup itself, driven end to end ────────────────────────────────
#
# ⚠️ WHY THIS CASE EXISTS. Every case above passes against a version where
# operator_home() is perfect and the call site ignores it —
# `OPERATOR_HOME="$HOME"` reintroduces the entire defect and fails none of them,
# because case 4 tests the function in isolation and case 5 greps for a string
# that sabotage does not contain. That hole was found by sabotaging the fix, and
# this is what closes it: the shipped Step 6 block, run with a real key in a real
# SUDO_USER home and $HOME pointing at root's, asserting the key is FOUND.
echo ""
echo "── the SSH-key lookup, under sudo, end to end ───────────────────────────"

sed -n '/^# Step 6: SSH Key Setup (optional)$/,/^# ── Remove every eth0 stanza/p' "$SRC" \
    | sed '$d' > "$TMP/step6.body"
if ! grep -q 'SSH Key Setup' "$TMP/step6.body"; then
    echo -e "  ${RED}HARNESS ERROR${NC}: could not extract Step 6 from commissioning/card-prep.sh"
    exit 2
fi

FAKE_USER_HOME="$TMP/home/operator"
mkdir -p "$FAKE_USER_HOME/.ssh"
echo "ssh-rsa AAAAB3NzaC1yc2E-test-key operator@host" > "$FAKE_USER_HOME/.ssh/id_rsa.pub"
mkdir -p "$TMP/rootfs"

# The stubs: the script's four colour helpers reduced to echo, and `sudo` made a
# passthrough so the install half runs unprivileged into a temp tree. OPERATOR_HOME
# is NOT stubbed — it is computed by the shipped code, which is the point.
{
    echo '#!/bin/bash'
    echo 'set -e'
    echo 'info()    { echo "  [--] $*"; }'
    echo 'success() { echo "  [ok] $*"; }'
    echo 'warning() { echo "  [!!] $*"; }'
    echo 'error()   { echo "  [XX] $*"; }'
    echo 'sudo()    { "$@"; }'
    echo "ROOTFS='$TMP/rootfs'"
    # The two things Step 6 depends on, both taken from the source rather than
    # restated. ⚠️ The OPERATOR_HOME assignment is EXTRACTED, not echoed: an
    # earlier version of this harness wrote `OPERATOR_HOME="$(operator_home)"`
    # itself, which silently repaired a sabotaged call site and let the defect
    # pass every case here. Measured — sabotage A failed 1 case with the echo and
    # 3 with the extraction.
    #
    # The library is SOURCED rather than extracted, which is the strongest form of
    # the same rule: rw_ssh_operator_home, rw_ssh_pubkey, rw_ssh_ask and
    # rw_ssh_keygen are the shipped implementations, not copies.
    echo ". '$LIB'"
    sed -n '/^OPERATOR_HOME=/p' "$SRC"
    cat "$TMP/step6.body"
} > "$TMP/step6.sh"

bash -n "$TMP/step6.sh" || { echo -e "  ${RED}HARNESS ERROR${NC}: extracted Step 6 does not parse"; exit 2; }

if command -v getent >/dev/null 2>&1 && [ "$(id -un)" != "root" ]; then
    # HOME=/root is what sudo leaves behind, and $SUDO_USER's home is overridden
    # to the fixture via a passwd lookup this test cannot fake — so instead the
    # fixture IS this user's home for the duration, by pointing HOME at /root and
    # asserting the code looks somewhere OTHER than /root. The key it must find is
    # placed in the real resolvable home of the current user only if that home is
    # writable; otherwise the case is skipped rather than faked.
    ME="$(id -un)"
    MY_HOME="$(getent passwd "$ME" 2>/dev/null | cut -d: -f6)"
    if [ -n "$MY_HOME" ] && [ -d "$MY_HOME" ] && [ -w "$MY_HOME" ]; then
        KEY_INSTALLED=0
        if [ ! -e "$MY_HOME/.ssh/id_rsa.pub" ] && [ ! -e "$MY_HOME/.ssh/id_ed25519.pub" ]; then
            mkdir -p "$MY_HOME/.ssh"
            echo "ssh-rsa AAAAB3NzaC1yc2E-rw-test-key $ME@test" > "$MY_HOME/.ssh/id_rsa.pub"
            KEY_INSTALLED=1
        fi
        printf 'y\ny\n' | env SUDO_USER="$ME" HOME=/root bash "$TMP/step6.sh" \
            > "$TMP/step6.out" 2>&1 || true
        expect_out yes "$MY_HOME/.ssh" "$TMP/step6.out" \
            "under sudo, the key is FOUND in \$SUDO_USER's home (not /root)"
        expect_out no  "/root/.ssh"    "$TMP/step6.out" \
            "under sudo, /root/.ssh is never the path reported"
        if [ -s "$TMP/rootfs/home/root/.ssh/authorized_keys" ]; then
            ok "the found key is written to the card's /home/root/.ssh/authorized_keys"
        else
            bad "the found key is written to the card's /home/root/.ssh/authorized_keys"
        fi
        [ "$KEY_INSTALLED" -eq 1 ] && rm -f "$MY_HOME/.ssh/id_rsa.pub"
    else
        skipped "the SSH-key lookup end to end" "no writable resolvable home for $(id -un)"
        skipped "under sudo, /root/.ssh is never the path reported" "same"
        skipped "the found key is written to the card" "same"
    fi
else
    skipped "the SSH-key lookup end to end" "needs getent and a non-root user"
    skipped "under sudo, /root/.ssh is never the path reported" "same"
    skipped "the found key is written to the card" "same"
fi

# 6. The other half of the same hole: the lookup must go through the library's
#    resolver, not $HOME. Static, because a call site that ignores the function is
#    only visible in the source once the functional case above is skipped.
if grep -q 'OPERATOR_HOME="\$(rw_ssh_operator_home)"' "$SRC"; then
    ok "the key lookup is wired to rw_ssh_operator_home, not \$HOME"
else
    bad "OPERATOR_HOME is not assigned from rw_ssh_operator_home() — the function is dead code"
fi

# 6b. ⚠️ The second half of F16: an operator with NO key was asked for a path they did
#     not have, and the card was written keyless while every later script told them to
#     "check IP and SSH key". The offer to generate must come BEFORE the path prompt,
#     which is a thing only the order in the source shows.
if grep -q 'rw_ssh_keygen' "$SRC"; then
    ok "card-prep.sh offers to generate a key when none exists"
else
    bad "card-prep.sh offers to generate a key when none exists — F16's second gap is open"
fi
GEN_LINE=$(grep -n 'rw_ssh_keygen' "$SRC" | head -1 | cut -d: -f1)
PATH_LINE=$(grep -n 'Enter path to your SSH public key (e.g' "$SRC" | head -1 | cut -d: -f1)
if [ -n "$GEN_LINE" ] && [ -n "$PATH_LINE" ] && [ "$GEN_LINE" -lt "$PATH_LINE" ]; then
    ok "the generate offer comes before the 'enter a path' fallback"
else
    bad "the generate offer comes before the 'enter a path' fallback (gen=$GEN_LINE path=$PATH_LINE)"
fi

# 6c. A keyless card must SAY it is keyless and name what fixes it. The old wording
#     was "Skipping SSH key setup. You can add it manually later." — which names
#     neither the consequence nor the command.
if grep -q 'WITHOUT authorized_keys' "$SRC"; then
    ok "a keyless card says so, and names what will offer to fix it"
else
    bad "a keyless card says so, and names what will offer to fix it"
fi

# ── extract the closing summary and run it both ways ────────────────────────
echo ""
echo "── next steps: printed standalone, suppressed when orchestrated ─────────"

sed -n '/^# Step 8: Summary and next steps$/,$p' "$SRC" > "$TMP/step8.body"
if [ ! -s "$TMP/step8.body" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: 'Step 8: Summary and next steps' not found"
    exit 2
fi

# The stubs are the script's own colour helpers, reduced to plain echo. REPO_ROOT
# points at the real repo so the sed below reads the real COMMISSIONING.md — the
# markers moving or being deleted is a failure this should catch, not route around.
{
    echo '#!/bin/bash'
    echo 'success() { echo "  [ok] $*"; }'
    echo 'info()    { echo "  [--] $*"; }'
    echo "REPO_ROOT='$REPO_DIR'"
    cat "$TMP/step8.body"
} > "$TMP/step8.sh"

bash -n "$TMP/step8.sh" || { echo -e "  ${RED}HARNESS ERROR${NC}: extracted body does not parse"; exit 2; }

# 6-7. Standalone: the next steps and the banner both appear. Asserted on two
#      needles — the block's own heading, and the specific instruction that is
#      wrong under the orchestrator.
env -u RW_COMMISSION_ORCHESTRATED -u ROOTFS bash "$TMP/step8.sh" > "$TMP/plain.out" 2>&1
expect_out yes "Remaining steps"   "$TMP/plain.out" "standalone: prints the next-steps block"
expect_out yes "./deploy-all.sh"   "$TMP/plain.out" "standalone: names deploy-all.sh"
expect_out yes "Commissioning Complete!" "$TMP/plain.out" "standalone: prints the Complete banner"

# 8. ⚠️ THE CASE THAT DISTINGUISHES THE FIX FROM THE OBVIOUS WRONG ONE.
#    ROOTFS is the documented hand-mounted escape hatch. Sniffing it would pass
#    every other case in this file and silence the instructions for that operator.
env -u RW_COMMISSION_ORCHESTRATED ROOTFS=/mnt/rw bash "$TMP/step8.sh" > "$TMP/rootfs.out" 2>&1
expect_out yes "Remaining steps" "$TMP/rootfs.out" \
    "ROOTFS set but not orchestrated: STILL prints the next steps"

# 9-11. Orchestrated: neither the banner nor the block, and a line saying what
#       actually happens next instead of nothing at all.
RW_COMMISSION_ORCHESTRATED=1 bash "$TMP/step8.sh" > "$TMP/orch.out" 2>&1
expect_out no  "Remaining steps"         "$TMP/orch.out" "orchestrated: no next-steps block"
expect_out no  "Commissioning Complete!" "$TMP/orch.out" "orchestrated: no Complete banner"
expect_out yes "Card prep complete"      "$TMP/orch.out" "orchestrated: says what happens next"

# 12. The orchestrator must actually set the flag. Two correct halves that never
#     meet is the failure this catches, and it is the whole fix in one grep.
if grep -q 'RW_COMMISSION_ORCHESTRATED=1' "$REPO_DIR/commissioning/commission-offline.sh"; then
    ok "commissioning/commission-offline.sh sets RW_COMMISSION_ORCHESTRATED"
else
    bad "commissioning/commission-offline.sh does NOT set RW_COMMISSION_ORCHESTRATED — the suppression is dead code"
fi

# ── the vendor network regenerator: verdict, wiring, and the two writes ─────
#
# WHY: phase 1 wrote a host name and DHCP, and an RW20 undid both ~7 s into the
# first boot (measured 2026-08-08) — leaving a unit at a
# static address on a foreign subnet, so phase 2 was unreachable and phase 1
# could not lead anywhere. The decision is now a pure function of three
# measurements, which is the only part of it a host can exercise: the probe
# needs a card and the prompt needs a person.
echo ""
echo "── vendor network regenerator ───────────────────────────────────────────"

sed -n '/^vendor_regen_verdict() {$/,/^}$/p' "$SRC" > "$TMP/verdict.sh"
if [ ! -s "$TMP/verdict.sh" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: vendor_regen_verdict() not found in $SRC"
    exit 2
fi
bash -n "$TMP/verdict.sh" || { echo -e "  ${RED}HARNESS ERROR${NC}: vendor_regen_verdict does not parse"; exit 2; }

# verdict LINK WEBSIGN MODE -> EXPECTED
verdict_is() {
    local link="$1" websign="$2" mode="$3" want="$4" got
    got=$(bash -c ". '$TMP/verdict.sh'; vendor_regen_verdict '$link' '$websign' '$mode'" 2>&1)
    if [ "$got" = "$want" ]; then
        ok "verdict($link, $websign, '$mode') = $want"
    else
        bad "verdict($link, $websign, '$mode') = '$got', expected '$want'"
    fi
}

verdict_is no  yes     manual  absent      # ⚠️ link absence wins over everything
verdict_is no  unknown ""      absent
verdict_is yes no      ""      inert       # writers live inside set_manual/set_dhcp
verdict_is yes yes     manual  manual
verdict_is yes yes     dhcp    dhcp
verdict_is yes yes     ""      unmeasured  # blank net.mode is a real vendor state
verdict_is yes yes     static  unmeasured  # a mode we do not model is not "safe"
verdict_is yes unknown ""      unmeasured  # p2 unreachable != nothing to do

# ── the section, run end to end against a fixture tree ──────────────────────
#
# Extracted from '# ── Step 7b' to the Step 8 marker, so what runs is the shipped
# source. lib/rw-identify.sh is SOURCED, not extracted — the strongest form of
# "the wiring is real". The probe self-vetoes here: findmnt on a /tmp path
# resolves to this host's root disk, which rw_is_host_root_disk refuses, so p2 is
# never reached and no sudo runs.
sed -n '/^# ── Step 7b: the vendor boot-time network regenerator/,/^# Step 8: Summary and next steps$/p' \
    "$SRC" | sed '$d' > "$TMP/step7b.body"
if [ ! -s "$TMP/step7b.body" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: Step 7b not found in $SRC"
    exit 2
fi

make_step7b() {   # make_step7b FIXTURE_ROOT
    {
        echo '#!/bin/bash'
        echo 'set -e'
        echo 'success() { echo "  [ok] $*"; }'
        echo 'info()    { echo "  [--] $*"; }'
        echo 'warning() { echo "  [!!] $*"; }'
        echo 'error()   { echo "  [XX] $*"; }'
        # ⚠️ sudo MUST be stubbed, and for a harness reason rather than a tidiness
        # one. The correct source never reaches a write on this path, so an
        # unstubbed run passes — but a SABOTAGED source (non-TTY answering "d")
        # calls the real sudo with stdin closed, which on a host that prompts
        # HANGS the suite instead of failing a case. A negative control that
        # hangs is worse than one that is absent: it looks like a slow test.
        echo 'sudo() { "$@"; }'
        echo ". '$REPO_DIR/lib/rw-identify.sh'"
        echo "ROOTFS='$1'"
        echo "NEW_HOSTNAME=rwtest"
        cat "$TMP/step7b.body"
    } > "$TMP/step7b.sh"
    bash -n "$TMP/step7b.sh" || { echo -e "  ${RED}HARNESS ERROR${NC}: extracted Step 7b does not parse"; exit 2; }
}

# A fixture with the link present. Real symlink, so /tmp — DrvFs cannot hold one.
FIX="$TMP/card"
mkdir -p "$FIX/etc/rcS.d" "$FIX/etc/init.d"
: > "$FIX/etc/init.d/networkmanager"
ln -sf ../init.d/networkmanager "$FIX/etc/rcS.d/S60networkmanager"

make_step7b "$FIX"

# 13-15. Not a TTY: the diagnosis is printed, the banner says nobody was asked,
#        and — the case that matters — the link is STILL THERE. An EOF read as
#        "d" would delete a boot link in a batch run nobody is watching.
env -u RW_COMMISSION_ORCHESTRATED bash "$TMP/step7b.sh" < /dev/null > "$TMP/regen.out" 2>&1 || true
expect_out yes "NOT A TTY" "$TMP/regen.out" "non-TTY: says nobody was asked"
expect_out yes "Could not read the vendor network config" "$TMP/regen.out" \
    "non-TTY: p2 unreachable is reported as unmeasured, not as nothing-to-do"
if [ -L "$FIX/etc/rcS.d/S60networkmanager" ]; then
    ok "non-TTY: the boot link is NOT removed"
else
    bad "non-TTY: the boot link was removed with nobody asked"
fi

# 16. Orchestrated: the offline pass deletes websign/ and this link in its clean,
#     so asking here would ask about a file that is already going.
RW_COMMISSION_ORCHESTRATED=1 bash "$TMP/step7b.sh" < /dev/null > "$TMP/regen.orch" 2>&1 || true
expect_out yes "Skipped" "$TMP/regen.orch" "orchestrated: the section is skipped"
expect_out no  "NOT A TTY" "$TMP/regen.orch" "orchestrated: does not warn about a question it never asked"

# 17. Link already gone: reported as settled, not as a problem.
rm -f "$FIX/etc/rcS.d/S60networkmanager"
make_step7b "$FIX"
env -u RW_COMMISSION_ORCHESTRATED bash "$TMP/step7b.sh" < /dev/null > "$TMP/regen.absent" 2>&1 || true
expect_out yes "already gone" "$TMP/regen.absent" "link absent: reported as settled"

# ── the write itself ────────────────────────────────────────────────────────
# 18-19. disable_vendor_regen removes the rcS.d link and ONLY that. Leaving
#        /etc/init.d/networkmanager in place is what makes the restore one ln -s,
#        and it is the difference between disabling a service and deleting it.
sed -n '/^disable_vendor_regen() {$/,/^}$/p' "$SRC" > "$TMP/disable.sh"
if [ ! -s "$TMP/disable.sh" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: disable_vendor_regen() not found in $SRC"
    exit 2
fi
ln -sf ../init.d/networkmanager "$FIX/etc/rcS.d/S60networkmanager"
{
    echo '#!/bin/bash'
    echo 'success() { echo "  [ok] $*"; }'
    echo 'info()    { echo "  [--] $*"; }'
    echo 'sudo() { "$@"; }'
    echo "VENDOR_REGEN_LINK='$FIX/etc/rcS.d/S60networkmanager'"
    cat "$TMP/disable.sh"
    echo 'disable_vendor_regen'
} > "$TMP/disable_run.sh"
bash "$TMP/disable_run.sh" > "$TMP/disable.out" 2>&1 || true
if [ ! -e "$FIX/etc/rcS.d/S60networkmanager" ] && [ ! -L "$FIX/etc/rcS.d/S60networkmanager" ]; then
    ok "disable_vendor_regen removes the rcS.d link"
else
    bad "disable_vendor_regen left the rcS.d link in place"
fi
if [ -f "$FIX/etc/init.d/networkmanager" ]; then
    ok "disable_vendor_regen leaves /etc/init.d/networkmanager alone"
else
    bad "disable_vendor_regen deleted /etc/init.d/networkmanager — the restore is no longer one ln -s"
fi

# ── wiring that no run can demonstrate ──────────────────────────────────────
# 20. The probe must mount p2 READ-ONLY. It is a measurement; a rw mount of the
#     partition holding the vendor's config is a write this script never intends.
if grep -q 'mount -o ro' "$SRC"; then
    ok "probe_vendor_net mounts p2 read-only"
else
    bad "probe_vendor_net does not mount p2 with -o ro"
fi

# 21. ⚠️ The veto that stops this from mounting the DEV HOST's own disk, written
#     as an `if` because `A && return 1` under `set -e` aborts on the normal case.
if grep -q '^    if rw_is_host_root_disk "$disk"; then' "$SRC"; then
    ok "the host-root-disk veto is an explicit if, not a && list"
else
    bad "the host-root-disk veto is missing or written as a && list (set -e aborts the normal case)"
fi

# 22. The prompt is [ -t 0 ]-gated. release.sh and deploy-all.sh drive these
#     scripts as a batch; a blocking read there hangs a run nobody is watching.
if grep -q 'if \[ -t 0 \]; then' "$SRC"; then
    ok "the regenerator prompt is [ -t 0 ]-gated"
else
    bad "the regenerator prompt is not [ -t 0 ]-gated"
fi

# 23. ⚠️ THE PAIRING CHECK. Phase 1 now removes a boot link, and phase 2's clean
#     decides what a unit keeps from device-files/clean-rules.conf. If that file
#     did not also name this link, the two phases would disagree about what a
#     commissioned unit looks like — the same class of drift rw_provision_check_keeps
#     exists to prevent in the other direction.
if grep -q 'rcS.d/S60networkmanager' "$REPO_DIR/device-files/clean-rules.conf"; then
    ok "clean-rules.conf also names rcS.d/S60networkmanager"
else
    bad "clean-rules.conf does NOT name rcS.d/S60networkmanager — phase 1 and phase 2 disagree"
fi

# 24. The menu label must not read as the whole job. This is the incident itself:
#     'Commission an SD card (offline)' was picked, correctly, for a full offline
#     commission, and delivered phase 1 of 3.
if grep -qE '^  1\) .*PHASE 1 of 3' "$REPO_DIR/roomwizard.sh"; then
    ok "roomwizard.sh menu item 1 says it is one phase"
else
    bad "roomwizard.sh menu item 1 does not say it is one phase"
fi

echo ""
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed, $SKIP skipped"

# A harness that runs nothing reports success. The non-skippable cases are:
#   3  operator_home  (no-sudo, SUDO_USER=root, unresolvable)
#   1  static: no bare $HOME/.ssh
#   1  static: the lookup is wired to operator_home, not $HOME
#   3  standalone output
#   1  ROOTFS-without-flag
#   3  orchestrated output
#   1  the orchestrator sets the flag
#   8  regenerator verdict (a pure function of three measurements)
#   5  regenerator section end to end (non-TTY x3, orchestrated x2)
#   1  regenerator section: link already gone
#   2  disable_vendor_regen: the link goes, /etc/init.d stays
#   5  wiring: -o ro, the host-root veto's `if`, [ -t 0 ], the clean-rules
#      pairing, the menu label
# = 35.  Four are skippable and deliberately not counted: operator_home's
# SUDO_USER's-real-home case and the three end-to-end key-lookup cases all need
# getent plus a non-root user with a writable home, and an environment lacking
# those should skip rather than fail on a fact about itself.
MIN_CASES=39
if [ "$TOTAL" -lt "$MIN_CASES" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: only $TOTAL cases ran, expected at least $MIN_CASES."
    echo "  Cases were skipped that cannot be skipped, or the file was truncated."
    exit 2
fi

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
echo -e "  ${GREEN}all good${NC}"
