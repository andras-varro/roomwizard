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
#   operator_home     Under sudo, $HOME is /root, so the offline flow looked for
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

# ── extract operator_home() from the real file ──────────────────────────────
#
# By its own definition boundaries, so the test cannot silently run against a
# stale inline copy. If the function is renamed or deleted, this fails loudly
# rather than testing nothing.
sed -n '/^operator_home() {$/,/^}$/p' "$SRC" > "$TMP/operator_home.sh"
if [ ! -s "$TMP/operator_home.sh" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: operator_home() not found in commissioning/card-prep.sh"
    echo "  It was renamed or removed. Update this test, do not delete the case."
    exit 2
fi
# shellcheck source=/dev/null
. "$TMP/operator_home.sh"

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
    # The two lines Step 6 depends on, both taken from the source rather than
    # restated. ⚠️ The OPERATOR_HOME assignment is EXTRACTED, not echoed: an
    # earlier version of this harness wrote `OPERATOR_HOME="$(operator_home)"`
    # itself, which silently repaired a sabotaged call site and let the defect
    # pass every case here. Measured — sabotage A failed 1 case with the echo and
    # 3 with the extraction.
    sed -n '/^operator_home() {$/,/^}$/p' "$SRC"
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

# 6. The other half of the same hole: the lookup must go through operator_home,
#    not $HOME. Static, because a call site that ignores the function is only
#    visible in the source once the functional case above is skipped.
if grep -q 'OPERATOR_HOME="\$(operator_home)"' "$SRC"; then
    ok "the key lookup is wired to operator_home, not \$HOME"
else
    bad "OPERATOR_HOME is not assigned from operator_home() — the function is dead code"
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
# = 13.  Four are skippable and deliberately not counted: operator_home's
# SUDO_USER's-real-home case and the three end-to-end key-lookup cases all need
# getent plus a non-root user with a writable home, and an environment lacking
# those should skip rather than fail on a fact about itself.
MIN_CASES=13
if [ "$TOTAL" -lt "$MIN_CASES" ]; then
    echo -e "  ${RED}HARNESS ERROR${NC}: only $TOTAL cases ran, expected at least $MIN_CASES."
    echo "  Cases were skipped that cannot be skipped, or the file was truncated."
    exit 2
fi

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
echo -e "  ${GREEN}all good${NC}"
