#!/bin/bash
#
# Measure rw_clean_test.sh against deliberately broken copies of the code it tests.
# The counts in that file's header come from here.
#
# In a FILE and not in argv: a sed pattern containing \t and [++nk] does not survive
# being quoted through `wsl.exe -e bash -lc "..."`, and a sabotage that fails to
# apply reports "0 failed" — which reads exactly like a test suite that cannot
# detect the breakage. That happened once; it is why this is a file.
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && bash tests/measure_sabotage.sh"

set -u
cd "$(dirname "$0")/.." || exit 1

BAK=/tmp/rw-clean.orig.$$
RULESBAK=/tmp/clean-rules.orig.$$
cp lib/rw-clean.sh "$BAK"
cp device-files/clean-rules.conf "$RULESBAK"
restore() { cp "$BAK" lib/rw-clean.sh; cp "$RULESBAK" device-files/clean-rules.conf; }
trap 'restore; rm -f "$BAK" "$RULESBAK"' EXIT INT TERM

run() { ./tests/rw_clean_test.sh 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | grep -oE '[0-9]+ passed, [0-9]+ failed'; }

echo ""
echo -n "  baseline (nothing broken)          "; run

# ── 1. scope records ignored: keeps applied, nothing swept ──────────────────
# "A rule that deletes nothing passes a test that only checks the keeps survived."
restore
perl -0pi -e 's/if \(on\(group\)\) sweeps\[\+\+ns\] = path/if (0) sweeps[++ns] = path/' lib/rw-clean.sh
echo -n "  scope records ignored              "; run

# ── 2. a disabled group's paths no longer protect ──────────────────────────
# Without this, --keep-java leaves /opt/openjre-8 named only by a delete nobody
# runs, and the /opt sweep removes it anyway: the flag does nothing and says nothing.
restore
perl -0pi -e 's/\n\s+else keeps\[\+\+nk\] = dirof\(path\) "\\t" nameof\(path\)//g' lib/rw-clean.sh
echo -n "  disabled group does not protect    "; run

# ── 3. factory back to opt-in ──────────────────────────────────────────────
# The 2026-08-06 reversal. One default across every flag; --keep-factory opts out.
restore
perl -0pi -e 's/^RW_CLEAN_GROUPS_DEFAULT="base browser java snmp mail extras vendorscripts factory sweeps"$/RW_CLEAN_GROUPS_DEFAULT="base browser java snmp mail extras vendorscripts sweeps"/m' lib/rw-clean.sh
perl -0pi -e 's/^RW_CLEAN_GROUPS_REMOVE="base browser java snmp mail extras vendorscripts factory"$/RW_CLEAN_GROUPS_REMOVE="base browser java snmp mail extras vendorscripts"/m' lib/rw-clean.sh
echo -n "  factory reverted to opt-in         "; run

# ── 4. --remove is a synonym for --deep-clean, not a subset ────────────────
# i.e. someone "simplifies" by giving --remove the sweeps too.
restore
perl -0pi -e 's/^RW_CLEAN_GROUPS_REMOVE="base browser java snmp mail extras vendorscripts factory"$/RW_CLEAN_GROUPS_REMOVE="base browser java snmp mail extras vendorscripts factory sweeps"/m' lib/rw-clean.sh
echo -n "  --remove given the sweeps too      "; run

# ── 5. the scope records moved back into `base` ────────────────────────────
# The shape of a revert-by-tidying: --remove then sweeps whatever it is handed.
restore
sed -i 's/^scope\tsweeps\t/scope\tbase\t/' device-files/clean-rules.conf
echo -n "  scope records back in 'base'       "; run

# ── 6. the eight named vendor logs dropped, relying on the sweep ───────────
# The gap `tests/c11_plan_diff.sh` found: invisible against --deep-clean's plan,
# real against --remove's.
restore
sed -i '/^delete\tbase\t\/home\/root\/log\/[A-Za-z]/d' device-files/clean-rules.conf
echo -n "  the 8 named vendor logs dropped    "; run

# ── 7. vendorscripts back to opt-in ────────────────────────────────────────
# The 2026-09-03 decision: /opt/sbin is deleted by default, --keep-vendorscripts
# is the only way out. This is the same shape as case 3 one group over, and it is
# what proves the E6/C30 pair is two-sided rather than a restatement of the
# default. ⚠️ Both patterns name the group list VERBATIM, so adding a group to
# lib/rw-clean.sh makes them no-ops and the harness then prints "not caught".
restore
perl -0pi -e 's/^RW_CLEAN_GROUPS_DEFAULT="base browser java snmp mail extras vendorscripts factory sweeps"$/RW_CLEAN_GROUPS_DEFAULT="base browser java snmp mail extras factory sweeps"/m' lib/rw-clean.sh
perl -0pi -e 's/^RW_CLEAN_GROUPS_REMOVE="base browser java snmp mail extras vendorscripts factory"$/RW_CLEAN_GROUPS_REMOVE="base browser java snmp mail extras factory"/m' lib/rw-clean.sh
echo -n "  vendorscripts reverted to opt-in   "; run

# ── 8. the /opt/sbin delete dropped, back to a keep ─────────────────────────
# The revert-by-tidying shape for this group: someone restores the record this
# change removed. The delete must be the thing that takes /opt/sbin, so with the
# rule gone the /opt sweep must not quietly do it instead — which is why the
# vendorscripts cases assert the plan, not only the tree.
restore
sed -i '/^delete\tvendorscripts\t\/opt\/sbin\t/d' device-files/clean-rules.conf
printf 'keep\tbase\t/opt/sbin\trestored by hand, the pre-2026-09-03 record\n' >> device-files/clean-rules.conf
echo -n "  /opt/sbin back to a base keep      "; run

# ── 9. the /opt/sbin delete moved into `base`, i.e. made UNCONDITIONAL ──────
# The case cases 7 and 8 cannot reach: with the record filed under `base` the
# default plan is unchanged and E6 still passes, but --keep-vendorscripts no
# longer means anything. This is the sabotage C31/C31a exist for — and the reason
# both directions are asserted, since a group-gated delete and an unconditional
# one are indistinguishable from the default plan alone.
restore
sed -i 's|^delete\tvendorscripts\t/opt/sbin\t|delete\tbase\t/opt/sbin\t|' device-files/clean-rules.conf
echo -n "  /opt/sbin delete made base-group   "; run

restore
echo ""
echo "  (the guardless del() case — commissioning/provision.sh's live executor lifted offline"
echo "   with no base check — is not run here: unprefixed it would rm -rf this host's"
echo "   /etc/shadow. It was measured at 11 failures against the 116-case version of"
echo "   the suite, with rm sandboxed by hand.)"
echo ""
