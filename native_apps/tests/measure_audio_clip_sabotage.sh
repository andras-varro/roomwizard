#!/bin/bash
# Measure that audio_tone_test group I can FAIL.  Copies only; the tree is
# untouched — ⚠️ never restore a sabotage with git, it would destroy whatever fix
# is still uncommitted beside it.
#
# ⚠️ Read the output, do not count it.  A stanza whose pattern has rotted prints
# "NO-OP EDIT" rather than "0 failed", and "0 failed" from a real edit is a HOLE
# in the suite, not a pass.  Case 8 is why there is a content assertion at all:
# every OTHER check in group I reads a position, and a fill that replays the
# clip's head advances every position correctly.
cd /mnt/c/work/roomwizard/native_apps || exit 1
W=$(mktemp -d /tmp/clip.XXXXXX)
SRC="common/audio.c common/audio.h common/audio_gen.c common/audio_gen.h
     common/audio_out.c common/audio_out.h common/audio_wav.c common/audio_wav.h
     common/config.c common/config.h"
run() {
  local name="$1"; shift
  rm -rf "$W/c"; mkdir -p "$W/c/common"
  cp $SRC "$W/c/common/"
  ( cd "$W/c/common" && "$@" ) || { echo "$name: SABOTAGE DID NOT APPLY"; return; }
  if diff -q "$W/c/common/audio.c" common/audio.c >/dev/null && \
     diff -q "$W/c/common/audio.h" common/audio.h >/dev/null; then
     echo "$name: NO-OP EDIT — pattern rotted"; return; fi
  gcc -Wall -Wextra -Wno-unused-parameter -I "$W/c" -Itests/hostshim -o "$W/t" \
      tests/audio_tone_test.c "$W/c/common/audio.c" "$W/c/common/audio_gen.c" \
      "$W/c/common/audio_out.c" "$W/c/common/audio_wav.c" "$W/c/common/config.c" \
      -lm 2>"$W/cc.log" || {
      echo "$name: did not compile ($(head -1 "$W/cc.log"))"; return; }
  # ⚠️ stdbuf, because a sabotage that CRASHES loses everything libc had buffered
  # into the pipe, and then a caught sabotage reads as an undetected one.
  out=$(stdbuf -oL timeout 90 "$W/t" 2>&1); rc=$?
  n=$(printf '%s\n' "$out" | grep -c 'FAIL:')
  if [ "$rc" -ge 124 ] || [ "$rc" -gt 1 ]; then
      echo "$name: $n failed, then SIGNALLED/TIMED OUT (rc=$rc) — caught"
  else echo "$name: $n failed"; fi
  printf '%s\n' "$out" | grep 'FAIL:' | sed 's/^/      /' | head -5
}

# ── the cursor, which is the whole reason a clip is RAM-resident ────────────
run "1 a trigger does not rewind the cursor" \
    sed -i 's|        cv->pos  = 0;|        /* sabotage */|' audio.c
run "2 one clip voice only, so a retrigger is refused the way sfx must" \
    sed -i 's|#define AUDIO_CLIP_VOICES        4|#define AUDIO_CLIP_VOICES        1|' audio.h
run "8 the fill ignores the cursor and replays the clip's head" \
    sed -i 's|memcpy(dst, cv->clip->pcm + cv->pos,|memcpy(dst, cv->clip->pcm,|' audio.c

# ── the fallback, which is what a device with no sound files depends on ─────
run "7 audio_beep() has no note-table fallback" \
    sed -i 's|    if (audio_fx_play(audio, AUDIO_FX_BEEP)) return;|    audio_fx_play(audio, AUDIO_FX_BEEP); return;|' audio.c

# ── the guards ─────────────────────────────────────────────────────────────
run "3 a missing file is retried on every trigger" \
    sed -i 's|    if (!c->tried) {|    if (1) {|' audio.c
run "4 set_path frees the old PCM immediately instead of deferring" \
    sed -i 's|    audio->fx\[id\].reload = true;    /\* the free happens at the next trigger \*/|    free(audio->fx[id].pcm); audio->fx[id].pcm = NULL; audio->fx[id].tried = false;|' audio.c
run "5 the clip ceiling is dropped, so a bed can be RAM-loaded" \
    sed -i 's|w.frames > AUDIO_CLIP_MAX_FRAMES|w.frames < 0|' audio.c
run "6 the rate check is dropped, so a clip is pitch-shifted" \
    sed -i 's|    if (w.rate != audio->sample_rate) {|    if (0) {|' audio.c

rm -rf "$W"
