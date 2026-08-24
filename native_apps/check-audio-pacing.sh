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
#
# ── Two more obligations, both of which SHIPPED broken ───────────────────────
# Same family — a rule about a game's audio that no counter and no screenshot
# can see, so it is arithmetic here instead of a rule somebody must remember.
#
#   4. **A bed must be serviced ABOVE the redraw block.**  SCREEN_GAME_OVER's
#      redraw calls gameover_update(), whose name entry is a BLOCKING sub-loop,
#      so a bed serviced *after* the block never sees the transition and plays
#      through the whole keyboard session — reported as "the end of game keeps
#      playing the music over the keyboard".  `want_play` was already correct;
#      only the POSITION was wrong, in all seven games at once, which is exactly
#      the kind of defect a per-file review keeps passing.  ⚠️ The check compares
#      the LAST `bed_service(` line against the first `if (needs_redraw) {` —
#      last, because platformer reaches the library through a wrapper whose
#      DEFINITION sits hundreds of lines above the loop and would otherwise
#      satisfy the check by accident.
#   5. **A game with a game-over screen must call audio_gameover().**  Six of the
#      seven played audio_fail() — the lost-a-LIFE sound — for the run ending
#      too, so nothing told a player which had happened.  Scoped to files calling
#      gameover_init(), which is exactly the seven games.
#      ⚠️ It CANNOT check the converse (that audio_fail() survives only where
#      lives exist): "has lives" is not a text property.  Today the invariant
#      holds — brick_breaker, frogger and platformer are the three with lives and
#      the only three still calling audio_fail() — but it is ear-and-review only.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    sed -n '2,70p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

# Scan one tree.  Prints a FAIL line per defect on stdout and echoes the three
# counts as the last line.  Kept a function so --self-test can drive it over a
# fixture directory rather than over the repo — a gate whose negative control
# runs a different code path proves nothing about the gate.
scan_dir() {
    local root=$1
    local f enable service active idle converted=0 ok=0 fail=0 unchecked=0

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

        # ── 4. a bed must be serviced ABOVE the redraw block ──────────────────
        # ⚠️ LAST match, not first: platformer reaches the library through a
        # bed_service() wrapper whose DEFINITION sits ~1600 lines above the loop,
        # and a first-match read would score that definition and pass the file
        # however badly the real call site was placed.
        local bedline rdrline
        bedline=$(grep -n 'bed_service(' "$f" | tail -1 | cut -d: -f1)
        if [ -n "$bedline" ]; then
            rdrline=$(grep -n 'if (needs_redraw) {' "$f" | head -1 | cut -d: -f1)
            if [ -z "$rdrline" ]; then
                # Not a pass and not a failure: the file has a bed and this gate
                # has no landmark to order it against.  Counted and printed, so a
                # skip cannot read as a green.
                echo "UNCHECKED ${f#$root/}: has a bed, but no 'if (needs_redraw) {' to order it against"
                unchecked=$((unchecked + 1))
            elif [ "$bedline" -gt "$rdrline" ]; then
                echo "FAIL ${f#$root/}: bed serviced at line $bedline, BELOW the redraw block at line $rdrline — the game-over screen's blocking name entry plays over the music"
                bad=1
            fi
        fi

        # ── 5. a game-over screen owes audio_gameover() ───────────────────────
        if grep -q 'gameover_init(' "$f" && ! grep -q 'audio_gameover(' "$f"; then
            echo "FAIL ${f#$root/}: has a game-over screen and never calls audio_gameover() — the RUN ending sounds like losing one life"
            bad=1
        fi

        if [ "$bad" -eq 0 ]; then ok=$((ok + 1)); else fail=$((fail + 1)); fi
    done <<EOF
$(find "$root" -name '*.c' \
      -not -path '*/common/*' -not -path '*/tests/*' \
      -not -path '*/arm-deps/*' -not -path '*/build/*' \
      -not -path '*/sounds/*' | sort)
EOF

    echo "COUNTS $converted $ok $fail $unchecked"
}

self_test() {
    local tmp rc=0 out
    tmp=$(mktemp -d) || return 1
    trap "rm -rf '$tmp'" EXIT

    mkdir -p "$tmp/good" "$tmp/nopump" "$tmp/nopace" "$tmp/plain" "$tmp/orphan" \
             "$tmp/bedlate" "$tmp/nogover" "$tmp/wrapper" "$tmp/wraplate" "$tmp/noland"

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
    # 6. the bed serviced BELOW the redraw block — must FAIL.  This is the shape
    #    that shipped in all seven games; note it is otherwise a CORRECT
    #    conversion, so it fails on the ordering alone.
    cat > "$tmp/bedlate/bedlate.c" <<'EOC'
int main(void){ audio_cont_enable(&a,true); gameover_init(&g); audio_gameover(&a);
  while(1){
    if (needs_redraw) { draw_all(); fb_swap(&fb); }
    audio_bed_service(&bed, playing, paused);
    audio_pump(&a);
    usleep((drew || audio_pump_active(&a)) ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US); } }
EOC
    # 7. a game-over screen with no audio_gameover() — must FAIL.  Six of seven
    #    games looked exactly like this, calling audio_fail() for the run.
    cat > "$tmp/nogover/nogover.c" <<'EOC'
int main(void){ audio_cont_enable(&a,true); gameover_init(&g); audio_fail(&a);
  while(1){
    audio_bed_service(&bed, playing, paused);
    if (needs_redraw) { draw_all(); fb_swap(&fb); }
    audio_pump(&a);
    usleep((drew || audio_pump_active(&a)) ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US); } }
EOC
    # 8. the wrapper case — must PASS.  ⚠️ The control for check 4's tail -1: the
    #    bed_service() DEFINITION is above the loop and the real call site is
    #    correctly placed, so a first-match read passes this for the wrong reason
    #    while a tail read passes it for the right one.  It is paired with
    #    fixture 9, which the first-match read gets WRONG.
    cat > "$tmp/wrapper/wrapper.c" <<'EOC'
static void bed_service(void){ audio_bed_service(&bed, playing, paused); }
int main(void){ audio_cont_enable(&a,true); gameover_init(&g); audio_gameover(&a);
  while(1){
    bed_service();
    if (needs_redraw) { draw_all(); fb_swap(&fb); }
    audio_pump(&a);
    usleep((drew || audio_pump_active(&a)) ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US); } }
EOC
    # 9. the wrapper case, MISPLACED — must FAIL.  A first-match read scores the
    #    definition on line 1 and calls this correct; only tail -1 sees it.
    cat > "$tmp/wraplate/wraplate.c" <<'EOC'
static void bed_service(void){ audio_bed_service(&bed, playing, paused); }
int main(void){ audio_cont_enable(&a,true); gameover_init(&g); audio_gameover(&a);
  while(1){
    if (needs_redraw) { draw_all(); fb_swap(&fb); }
    bed_service();
    audio_pump(&a);
    usleep((drew || audio_pump_active(&a)) ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US); } }
EOC
    # 10. a bed with NO redraw block — must be UNCHECKED, neither pass nor fail.
    cat > "$tmp/noland/noland.c" <<'EOC'
int main(void){ audio_cont_enable(&a,true);
  while(1){
    audio_bed_service(&bed, playing, paused);
    audio_pump(&a);
    usleep((drew || audio_pump_active(&a)) ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US); } }
EOC

    out=$(scan_dir "$tmp")
    local expect_fail="nopump/nopump.c nopace/nopace.c orphan/orphan.c bedlate/bedlate.c nogover/nogover.c wraplate/wraplate.c"
    local expect_pass="good/good.c plain/plain.c wrapper/wrapper.c"

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
    if echo "$out" | grep -q "UNCHECKED noland/noland.c"; then
        echo "  unchecked:  noland/noland.c (bed with no redraw landmark — reported, not passed)"
    else
        echo "  NOT REPORTED: noland/noland.c   <-- a skipped check is reading as a pass"; rc=1
    fi
    echo "  fixture counts: $counts"
    [ "$counts" = "COUNTS 8 3 6 1" ] || { echo "  counts wrong <-- expected 'COUNTS 8 3 6 1'"; rc=1; }
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
echo "$out" | grep '^UNCHECKED ' || true
echo "$out" | grep '^FAIL ' && rc=1 || rc=0
read -r _ converted ok fail unchecked <<EOF2
$(echo "$out" | grep '^COUNTS ')
EOF2
if [ "$rc" -eq 0 ]; then
    # ⚠️ "feed their bus" is no longer the whole of what passed — the same $ok
    # also carries the bed ordering and the game-over sound, so the wording says
    # obligations rather than naming one of them.
    echo "  ✓ Audio pacing: $ok/$converted converted app(s) meet every audio obligation"
else
    echo "  ✗ Audio pacing: $fail of $converted converted app(s) fail an audio obligation"
fi
echo "PACING-SUMMARY converted=$converted ok=$ok fail=$fail unchecked=$unchecked"
exit $rc
