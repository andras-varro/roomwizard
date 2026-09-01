#!/bin/bash
# Measure the LOW-FREQUENCY LIMIT of the RoomWizard's speaker, by ear, in pairs.
#
# ── Why this exists ─────────────────────────────────────────────────────────
# The first panel listen (2026-08-20, `.188`) produced five ear results
# that sort perfectly by frequency: 800–1500 Hz tones audible at 20 ms, 600 Hz
# inaudible at 30 ms, 500 Hz faint at 60 ms, `audio_fail()`'s 392/330/262 Hz
# silent.  That says the ~20 mm metal-can SPKR1 rolls off hard somewhere between
# 600 and 800 Hz — which decides every note table in the tree, so it is worth a
# number rather than a bracket.
#
# ── Method: each test tone is PAIRED with a reference it can be missing beside ─
# ⚠️ **A silent tone must identify itself.**  `tests/audio_phase_test.sh` solves
# that with N marker clicks and a count; this pairs every test tone with a
# 1000 Hz reference instead, so the ear task is "did I hear ONE beep or TWO"
# rather than "was that the fourth or the fifth".  The reference IS the marker,
# and it also catches the failure where the whole run was inaudible for an
# unrelated reason — no pair sounding at all is a broken setup, not a result.
#
# ⚠️ **The tones are 400 ms, and that is deliberate.**  At 40 ms this would
# measure the start-of-stream floor instead of the speaker
# (../../SYSTEM_ANALYSIS.md#34-audio gotcha 6): every `aplay` invocation restarts
# the stream, where anything under ~60 ms is inaudible whatever its pitch.  400 ms
# puts duration out of the comparison so frequency is the only variable.
#
# ⚠️ **It plays through the vendor's `aplay` at `hw:0,0`, not through our stack.**
# That is the point — the claim under test is about the SPEAKER, so the mixer, the
# generator, the limiter and the pump are all taken out of the loop.  Stereo
# 44100/16-bit with both channels identical, because SPKR1 sums L + R (measured,
# ../../SYSTEM_ANALYSIS.md#34-audio) and that is also what our write path emits.
#
# Usage:   ./tests/speaker_band.sh <ip> [--keep]
#          --keep leaves /tmp/band on the device for a second run.
#
# Needs python3 on the host (WSL — Git Bash's python3 is the App Execution Alias
# and does not exist, ../../CLAUDE.md).  Nothing is cross-compiled.
#
# What to write down: the LOWEST frequency where you hear TWO beeps, and whether
# any pair gave you ONE.  ⚠️ Ascending order is deliberate but it biases
# adaptation the same way a loud-to-quiet level walk does — so if the answer
# matters to a decision, run it once more with --descending and see if the
# boundary moves.

set -uo pipefail

IP=${1:-}
KEEP=0
DESC=0
for a in "$@"; do
    case "$a" in
        --keep)       KEEP=1 ;;
        --descending) DESC=1 ;;
    esac
done
if [ -z "$IP" ] || [ "$IP" = "--help" ]; then
    sed -n '2,44p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
fi

FREQS="200 250 300 350 400 500 600 700 800 1000 1300"
[ "$DESC" -eq 1 ] && FREQS=$(echo "$FREQS" | tr ' ' '\n' | sort -rn | tr '\n' ' ')
REF=1000
STAGE=$(mktemp -d)
trap "rm -rf '$STAGE'" EXIT

echo "── generating 400 ms stereo sines (host) ──"
python3 - "$STAGE" $REF $FREQS <<'PY'
import math, struct, sys, wave
out = sys.argv[1]
rate, ms, peak = 44100, 400, 0.97
def render(f, path):
    n = int(rate * ms / 1000)
    fr = bytearray()
    for i in range(n):
        # 5 ms raised-cosine ends: a hard start on a 200 Hz sine is a CLICK, and a
        # click is broadband — it would be audible at every frequency and would
        # therefore answer the question with itself.
        env = 1.0
        e = int(rate * 0.005)
        if i < e:          env = 0.5 - 0.5 * math.cos(math.pi * i / e)
        elif i > n - e:    env = 0.5 - 0.5 * math.cos(math.pi * (n - i) / e)
        s = int(peak * env * 32767 * math.sin(2 * math.pi * f * i / rate))
        fr += struct.pack('<hh', s, s)          # L = R: SPKR1 sums them
    w = wave.open(path, 'wb')
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(bytes(fr)); w.close()
for f in sys.argv[2:]:
    render(int(f), "%s/t%s.wav" % (out, f))
PY
ls "$STAGE" | wc -l | xargs echo "  files:"

echo "── staging on $IP ──"
ssh "root@$IP" 'mkdir -p /tmp/band' || exit 1
scp -q "$STAGE"/*.wav "root@$IP:/tmp/band/" || exit 1

echo "── listen: each pair is 1000 Hz REFERENCE then the TEST tone ──"
for f in $FREQS; do
    echo "  $f Hz"
    ssh "root@$IP" "aplay -D hw:0,0 /tmp/band/t$REF.wav >/dev/null 2>&1; sleep 0.4; aplay -D hw:0,0 /tmp/band/t$f.wav >/dev/null 2>&1; sleep 1.4"
done
echo "── done.  Lowest frequency where you heard TWO beeps? ──"
[ "$KEEP" -eq 1 ] || ssh "root@$IP" 'rm -rf /tmp/band'
