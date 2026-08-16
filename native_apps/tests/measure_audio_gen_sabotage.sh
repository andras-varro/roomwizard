#!/bin/bash
# Measure that audio_gen_test group L/M can FAIL.  Copies only; the tree is untouched.
cd /mnt/c/work/roomwizard/native_apps || exit 1
W=$(mktemp -d /tmp/lim.XXXXXX)
run() {
  local name="$1"; shift
  rm -rf "$W/c"; mkdir -p "$W/c"
  cp common/audio_gen.c common/audio_gen.h "$W/c/"
  ( cd "$W/c" && "$@" ) || { echo "$name: SABOTAGE DID NOT APPLY"; return; }
  if ! diff -q "$W/c/audio_gen.c" common/audio_gen.c >/dev/null || \
     ! diff -q "$W/c/audio_gen.h" common/audio_gen.h >/dev/null; then :; else
     echo "$name: NO-OP EDIT — pattern rotted"; return; fi
  gcc -Wall -Wextra -Wno-unused-parameter -I "$W/c" -o "$W/t" \
      tests/audio_gen_test.c "$W/c/audio_gen.c" -lm 2>"$W/cc.log" || {
      echo "$name: did not compile ($(head -1 "$W/cc.log"))"; return; }
  out=$(timeout 60 "$W/t" 2>&1)
  n=$(printf '%s\n' "$out" | grep -c 'FAIL:')
  echo "$name: $n failed"
  printf '%s\n' "$out" | grep 'FAIL:' | sed 's/^/      /' | head -6
}
run "1 knee below one voice's peak (12000)" \
    sed -i 's/#define AUDIO_MIX_KNEE           AUDIO_PEAK/#define AUDIO_MIX_KNEE           12000/' audio_gen.h
run "2 ceiling outside int16 (40000)" \
    sed -i 's/#define AUDIO_MIX_CEIL           26000/#define AUDIO_MIX_CEIL           40000/' audio_gen.h
run "3 no knee at all: y = K + span*u (linear, unbounded)" \
    sed -i 's|span \* (u / (1.0 + u))|span * u|' audio_gen.c
run "4 counters swapped: the knee counts as a clip" \
    sed -i 's/if (m->limit == AUDIO_MIX_HARD) m->clipped++;/if (m->limit == AUDIO_MIX_HARD) m->limited++;/' audio_gen.c
run "5 default mode is the rejected HARD clamp" \
    sed -i 's/m->limit = AUDIO_MIX_SOFT;/m->limit = AUDIO_MIX_HARD;/' audio_gen.c
run "6 truncate instead of round (the lost LSB at the knee)" \
    sed -i 's/int32_t v = (int32_t)(out + 0.5);/int32_t v = (int32_t)out;/' audio_gen.c
run "7 soft path ignores the sign of the sum" \
    sed -i 's/return (acc < 0) ? -v : v;/return v;/' audio_gen.c
rm -rf "$W"
