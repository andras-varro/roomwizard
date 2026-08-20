#!/bin/bash
# Pre-deploy gate: an app that turns the audio bus ON must also FEED it.
#
# F1 Phase 5 converts the games one at a time, and the conversion is three lines
# in two different places:
#
#     audio_cont_enable(&audio, true);          /* once, after audio_init()  */
#     audio_pump(&audio);                       /* EVERY iteration           */
#     usleep((drew || audio_pump_active(&audio)) ? ACTIVE : IDLE);
#
# Miss the second and the stream is never serviced; miss the third and the loop
# drops to FRAME_DELAY_IDLE_US (100 ms) against a ~55 ms service ceiling and the
# device runs dry ~2.5 times a second.  ⚠️ **Neither mistake errors, and neither
# is visible in a screenshot** — the game runs, the panel looks right, and the
# sound has gaps in it.  With six more games to convert this is a rule nobody
# should have to remember, so it is arithmetic instead.
#
# Usage:
#   ./check-audio-pacing.sh              # scan this directory's app sources
#   ./check-audio-pacing.sh --self-test  # prove each check can FAIL
#   ./check-audio-pacing.sh --help
#
# Exit 0 = every converted app feeds its bus.  Exit 1 = do not deploy.
# The last line always carries the counts as PACING-SUMMARY.
#
# ── What it cannot see ───────────────────────────────────────────────────────
# It reads text, not control flow, so three real defects pass it:
#
#   - an `audio_pump()` placed INSIDE `if (needs_redraw) { … }`, which services
#     the stream only on frames that drew.  That is the shape the pacing rule
#     exists to prevent and the gate is blind to it; `starve` at exit is what
#     catches it (audio_close() prints it — one line, every app).
#   - a mention of the identifier in a COMMENT counts as a call.  Deliberate:
#     stripping comments from C with grep is a worse bug than this one, and the
#     failure direction is a false PASS on a file somebody was writing prose about.
#   - a sleep that is not spelled with the FRAME_DELAY_* constants at all.
#
# So this bounds the conversion, and the device's own counters judge it.
# Rule and measurements: ../IMPROVEMENT_PLAN.md F1 Phase 5, and
# ../SYSTEM_ANALYSIS.md#34-audio gotcha 5 for the 66 ms figure.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    sed -n '2,44p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

# Scan one tree.  Prints a FAIL line per defect on stdout and echoes the three
# counts as the last line.  Kept a function so --self-test can drive it over a
# fixture directory rather than over the repo — a gate whose negative control
# runs a different code path proves nothing about the gate.
scan_dir() {
    local root=$1
    local f enable service active idle converted=0 ok=0 fail=0

    while IFS= read -r f; do
        enable=$(grep -c 'audio_cont_enable(\|audio_pump_enable(' "$f")
        service=$(grep -c 'audio_pump(' "$f")
        active=$(grep -c 'audio_pump_active(' "$f")
        idle=$(grep -c 'FRAME_DELAY_IDLE_US' "$f")

        if [ "$enable" -eq 0 ]; then
            # Not converted.  ⚠️ The MIRROR defect: a service call with no bus to
            # service is a no-op, so the sound silently stays on the old path.
            if [ "$service" -gt 0 ]; then
                echo "FAIL ${f#$root/}: calls audio_pump() but never enables a bus"
                fail=$((fail + 1))
            fi
            continue
        fi

        converted=$((converted + 1))
        local bad=0
        if [ "$service" -eq 0 ]; then
            echo "FAIL ${f#$root/}: enables the audio bus and never services it (no audio_pump())"
            bad=1
        fi
        if [ "$idle" -gt 0 ] && [ "$active" -eq 0 ]; then
            echo "FAIL ${f#$root/}: sleeps FRAME_DELAY_IDLE_US with no audio_pump_active() in the pacing"
            bad=1
        fi
        if [ "$bad" -eq 0 ]; then ok=$((ok + 1)); else fail=$((fail + 1)); fi
    done <<EOF
$(find "$root" -name '*.c' \
      -not -path '*/common/*' -not -path '*/tests/*' \
      -not -path '*/arm-deps/*' -not -path '*/build/*' \
      -not -path '*/sounds/*' | sort)
EOF

    echo "COUNTS $converted $ok $fail"
}

self_test() {
    local tmp rc=0 out
    tmp=$(mktemp -d) || return 1
    trap "rm -rf '$tmp'" EXIT

    mkdir -p "$tmp/good" "$tmp/nopump" "$tmp/nopace" "$tmp/plain" "$tmp/orphan"

    # 1. correct conversion — must PASS
    cat > "$tmp/good/good.c" <<'EOC'
int main(void){ audio_cont_enable(&a,true);
  while(1){ audio_pump(&a);
    usleep((drew || audio_pump_active(&a)) ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US); } }
EOC
    # 2. bus enabled, never serviced — must FAIL
    cat > "$tmp/nopump/nopump.c" <<'EOC'
int main(void){ audio_cont_enable(&a,true);
  while(1){ usleep(drew ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US); } }
EOC
    # 3. serviced, but the loop still idles at 100 ms — must FAIL
    cat > "$tmp/nopace/nopace.c" <<'EOC'
int main(void){ audio_pump_enable(&a,true);
  while(1){ audio_pump(&a);
    usleep(drew ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US); } }
EOC
    # 4. UNCONVERTED app — must PASS.  ⚠️ The load-bearing control: five games
    #    still look exactly like this and a gate that failed them would be
    #    switched off rather than obeyed.
    cat > "$tmp/plain/plain.c" <<'EOC'
int main(void){ audio_init(&a);
  while(1){ usleep(drew ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US); } }
EOC
    # 5. the mirror defect: serviced, nothing enabled — must FAIL
    cat > "$tmp/orphan/orphan.c" <<'EOC'
int main(void){ audio_init(&a);
  while(1){ audio_pump(&a); usleep(FRAME_DELAY_ACTIVE_US); } }
EOC

    out=$(scan_dir "$tmp")
    local expect_fail="nopump/nopump.c nopace/nopace.c orphan/orphan.c"
    local expect_pass="good/good.c plain/plain.c"

    echo "── self-test ────────────────────────────────────────────────────"
    for c in $expect_fail; do
        if echo "$out" | grep -q "FAIL $c"; then
            echo "  caught:     $c"
        else
            echo "  NOT CAUGHT: $c   <-- the gate has a hole"; rc=1
        fi
    done
    for c in $expect_pass; do
        if echo "$out" | grep -q "FAIL $c"; then
            echo "  FALSE HIT:  $c   <-- the gate is hostile"; rc=1
        else
            echo "  passed:     $c"
        fi
    done
    local counts
    counts=$(echo "$out" | grep '^COUNTS ')
    echo "  fixture counts: $counts (converted=3 expected: good, nopump, nopace)"
    [ "$counts" = "COUNTS 3 1 3" ] || { echo "  counts wrong <-- expected 'COUNTS 3 1 3'"; rc=1; }
    echo "── self-test $([ $rc -eq 0 ] && echo PASSED || echo FAILED) ─────────────────────────────────"
    return $rc
}

case "${1:-}" in
    --help|-h) usage; exit 0 ;;
    --self-test) self_test; exit $? ;;
    "") ;;
    *) echo "Unknown argument: $1"; usage; exit 2 ;;
esac

out=$(scan_dir "$HERE")
echo "$out" | grep '^FAIL ' && rc=1 || rc=0
read -r _ converted ok fail <<EOF2
$(echo "$out" | grep '^COUNTS ')
EOF2
if [ "$rc" -eq 0 ]; then
    echo "  ✓ Audio pacing: $ok/$converted converted app(s) feed their bus"
else
    echo "  ✗ Audio pacing: $fail of $converted converted app(s) will run dry"
fi
echo "PACING-SUMMARY converted=$converted ok=$ok fail=$fail"
exit $rc
