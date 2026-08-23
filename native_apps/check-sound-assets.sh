#!/bin/bash
# Gate: every sounds/fx_*.wav is in the mixer's format AND has its energy where
# this speaker can radiate it.
#
# Run from native_apps/build-and-deploy.sh, beside check-arm-safe.sh and
# check-audio-pacing.sh.  Standalone:
#     ./check-sound-assets.sh [<dir>]          # default: sounds/
#
# ── WHY THIS EXISTS ─────────────────────────────────────────────────────────
# Two defects that a listen catches late, a screenshot never catches, and each of
# which has already cost this project real time:
#
#  1. ⚠️ **A wrong sample rate is refused in SILENCE.**  clip_load() in
#     common/audio.c rejects any clip that is not 44100 / mono / 16-bit, logs one
#     line, and the game falls back to its note table — so the game sounds
#     UNCHANGED and reads as "the new file did nothing" rather than as an error.
#     Measured: a sourced effect arrived 48000 Hz stereo and would have changed
#     nothing on the panel (../native_apps/CLAUDE.md → *Sound assets*).
#  2. ⚠️ **Energy below the speaker's ~700 Hz knee is not quiet, it is absent**
#     ([../SYSTEM_ANALYSIS.md#34-audio](../SYSTEM_ANALYSIS.md#34-audio)).  A file
#     can be peak-normalised to −0.3 dBFS, look perfect in every byte-level check,
#     and be inaudible at viewing distance.
#
# It REPLACES fx_gen.c's spectral-flatness gate, which enforced the wrong property
# and rejected every change that restored pitch — the reversal and its numbers are
# in ../IMPROVEMENT_PLAN.md F1 Phase 5 ③.
#
# ⚠️ **WHAT THIS GATE CANNOT HEAR, stated so nobody reads a green run as more than
# it is:** in-band energy is an AUDIBILITY property, not an identity one.  The
# retired generated set would have PASSED this gate — it was broadband, so it met
# the band requirement by accident while sounding like white noise.  Whether an
# effect sounds like the thing it is named after is still ear-only, and still the
# operator's call on the panel.
#
# ── THE INSTRUMENT, AND WHY IT VALIDATES ITSELF FIRST ───────────────────────
# ⚠️ **A UNIFORM reading across every input is a SILENCED instrument, not a clean
# result**, and this measurement has produced exactly that twice:
#   · `ffmpeg -v error` suppresses astats' own report, so a band check printed the
#     same verdict for all ten files.
#   · `ebur128` needs 400 ms / 3 s windows these short effects do not have, and
#     emitted nothing at all.
#   · A grep window of `-A8` after astats' "Overall" line misses "RMS level dB",
#     which is 10 lines further down — an empty capture that reads as 0 dB.
# So the two OPPOSED controls below run BEFORE any asset is measured, and a run
# whose controls do not separate reports the INSTRUMENT as broken and exits
# non-zero without judging a single file.  A gate that cannot be seen failing is
# not evidence of anything (../tests/CLAUDE.md).
#
# Measured on this host, ffmpeg 4.2.7, `highpass=f=700` at its default 2 poles:
#   200 Hz sine → delta −21.82 dB      2000 Hz sine → delta −0.07 dB
# ⚠️ **The pair recorded in the plan as "−43.4 dB vs −0.1 dB" was TWO DIFFERENT
# QUANTITIES**: −43.4 is the 200 Hz control's ABSOLUTE filtered RMS (measured here
# as −42.89) and −0.1 is the 2 kHz control's DELTA.  A gate asserting both as
# deltas would have failed its own control forever.  Both numbers below are deltas.

set -u

DIR="${1:-sounds}"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

# ── thresholds ──────────────────────────────────────────────────────────────
# DELTA = (RMS after a 700 Hz high-pass) − (RMS of the file).  0 dB means all of
# the energy is above the knee; a large negative means most of it is never
# radiated.  IN-BAND is that filtered RMS in absolute dBFS — "is what survives
# the speaker loud enough to carry over a music bed".
#
# The FAIL lines are set where a file is broken rather than merely weak, so the
# gate does not block the deliberate quiet ones (fx_tick is a tick).  The WARN
# lines are where the current worst two sit, so they stay visible without turning
# every build red: measured 2026-08-22, `knock` −3.05 dB / −21.66 dBFS and `thud`
# −4.01 dB / −20.40 dBFS are the two queued for regeneration.
DELTA_FAIL=-6.0
DELTA_WARN=-2.5
INBAND_FAIL=-26.0
INBAND_WARN=-20.0

# Format the mixer grants.  Anything else is refused in silence by clip_load().
WANT_RATE=44100
WANT_CH=1
WANT_FMT=s16

# ── the instrument ──────────────────────────────────────────────────────────
if ! command -v ffmpeg >/dev/null 2>&1 || ! command -v ffprobe >/dev/null 2>&1; then
    echo -e "${YELLOW}  ! sound assets: ffmpeg/ffprobe not installed — check SKIPPED${NC}"
    echo "SOUND-SUMMARY checked=0 ok=0 warn=0 fail=0 skipped=all (no ffmpeg)"
    exit 0
fi

# ⚠️ No `-v error` and no `-loglevel warning`: astats reports at INFO, so quieting
# ffmpeg quiets the measurement.  `tail -1` takes astats' "Overall" block, which is
# printed after the per-channel one.
rms_of() {
    ffmpeg -hide_banner -i "$1" -af "$2" -f null - 2>&1 \
        | grep "RMS level dB" | tail -1 | sed 's/.*RMS level dB: //'
}

# delta for one file, or the empty string if either read came back blank
delta_of() {
    local raw filt
    raw=$(rms_of "$1" astats)
    filt=$(rms_of "$1" "highpass=f=700,astats")
    [ -n "$raw" ] && [ -n "$filt" ] || return 1
    awk -v a="$filt" -v b="$raw" 'BEGIN { printf "%.2f", a - b }'
    return 0
}

inband_of() { rms_of "$1" "highpass=f=700,astats"; }

lt() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a < b) }'; }

# ── controls, first, opposed ────────────────────────────────────────────────
# 200 Hz must read deeply negative and 2 kHz must read ~0.  Either one alone can
# be satisfied by a broken instrument (a filter that removes everything passes the
# first; one that removes nothing passes the second), which is why they are a PAIR.
CTL_DIR=$(mktemp -d)
trap 'rm -rf "$CTL_DIR"' EXIT

ctl_delta() {
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "sine=frequency=$1:duration=1:sample_rate=44100" \
        -c:a pcm_s16le "$CTL_DIR/ctl.wav" 2>/dev/null
    delta_of "$CTL_DIR/ctl.wav"
}

CTL_LOW=$(ctl_delta 200)  || CTL_LOW=""
CTL_HIGH=$(ctl_delta 2000) || CTL_HIGH=""

if [ -z "$CTL_LOW" ] || [ -z "$CTL_HIGH" ]; then
    echo -e "${RED}  ✗ sound assets: the CONTROLS produced no reading — the measurement is broken, not the files${NC}"
    echo "SOUND-SUMMARY checked=0 ok=0 warn=0 fail=0 instrument=BROKEN (empty control)"
    exit 1
fi
# Expected −21.8 and −0.07; the windows are wide because the point is separation,
# not reproducing a decimal.  ⚠️ If these ever fire, do NOT relax them — read what
# changed in the pipeline, because every asset verdict in this file depends on it.
if ! lt "$CTL_LOW" -15.0 || lt "$CTL_HIGH" -1.0; then
    echo -e "${RED}  ✗ sound assets: controls did not separate (200 Hz ${CTL_LOW} dB, 2 kHz ${CTL_HIGH} dB)${NC}"
    echo -e "${RED}    expected 200 Hz < -15 and 2 kHz > -1 — the instrument is wrong, no file was judged${NC}"
    echo "SOUND-SUMMARY checked=0 ok=0 warn=0 fail=0 instrument=BROKEN (no separation)"
    exit 1
fi

# ── the assets ──────────────────────────────────────────────────────────────
shopt -s nullglob
FILES=("$DIR"/fx_*.wav)
if [ ${#FILES[@]} -eq 0 ]; then
    echo -e "${RED}  ✗ sound assets: no $DIR/fx_*.wav found${NC}"
    echo "SOUND-SUMMARY checked=0 ok=0 warn=0 fail=0 (no files)"
    exit 1
fi

n_ok=0; n_warn=0; n_fail=0
printf "  %-16s %9s %9s  %s\n" FILE "IN-BAND" "DELTA" "FORMAT"

for f in "${FILES[@]}"; do
    base=$(basename "$f")
    # ⚠️ ONE ffprobe call PER FIELD, deliberately.  Asking for three entries at
    # once returns them in the STREAM's order, not the order you listed — so
    # `stream=sample_rate,channels,sample_fmt` with `nk=1` comes back as
    # `s16 / 44100 / 1` and a positional read calls every correct file wrong.
    # Measured 2026-08-22: the first version of this gate failed all eleven files
    # for "wrong format" while printing their correct format on the same line.
    # Three named calls cost three exec()s and cannot be misread.
    probe1() { ffprobe -v error -select_streams a:0 \
                   -show_entries "stream=$1" -of default=nw=1:nk=1 "$2" 2>/dev/null; }
    rate=$(probe1 sample_rate "$f")
    chan=$(probe1 channels    "$f")
    fmt=$(probe1  sample_fmt  "$f")

    fmt_msg="${rate:-?}/${chan:-?}/${fmt:-?}"
    bad_fmt=0
    [ "$rate" = "$WANT_RATE" ] || bad_fmt=1
    [ "$chan" = "$WANT_CH" ]   || bad_fmt=1
    [ "$fmt"  = "$WANT_FMT" ]  || bad_fmt=1

    d=$(delta_of "$f") || d=""
    ib=$(inband_of "$f")

    if [ -z "$d" ] || [ -z "$ib" ]; then
        printf "  %-16s %9s %9s  %s\n" "$base" "-" "-" "$fmt_msg"
        echo -e "${RED}    ✗ $base: no reading — treat as a FAIL, not as a pass${NC}"
        n_fail=$((n_fail + 1))
        continue
    fi

    verdict=ok
    reasons=""
    if [ $bad_fmt -eq 1 ]; then
        verdict=fail
        reasons="$reasons wrong format (want $WANT_RATE/$WANT_CH/$WANT_FMT — clip_load() refuses this in SILENCE);"
    fi
    if lt "$d" "$DELTA_FAIL"; then
        verdict=fail
        reasons="$reasons band $d dB below the $DELTA_FAIL floor (energy is under the knee);"
    elif lt "$d" "$DELTA_WARN"; then
        [ "$verdict" = fail ] || verdict=warn
        reasons="$reasons band $d dB is weak (< $DELTA_WARN);"
    fi
    if lt "$ib" "$INBAND_FAIL"; then
        verdict=fail
        reasons="$reasons in-band $ib dBFS below the $INBAND_FAIL floor;"
    elif lt "$ib" "$INBAND_WARN"; then
        [ "$verdict" = fail ] || verdict=warn
        reasons="$reasons in-band $ib dBFS is quiet (< $INBAND_WARN);"
    fi

    printf "  %-16s %9s %9s  %s\n" "$base" "$ib" "$d" "$fmt_msg"
    case "$verdict" in
        ok)   n_ok=$((n_ok + 1)) ;;
        warn) n_warn=$((n_warn + 1))
              echo -e "${YELLOW}    ! $base:$reasons${NC}" ;;
        fail) n_fail=$((n_fail + 1))
              echo -e "${RED}    ✗ $base:$reasons${NC}" ;;
    esac
done

# ── the CLAIM check: is anything in C actually going to PLAY this file? ──────
#
# ⚠️ **This is the check whose absence shipped the bug.**  Five of the eleven
# effects were sourced, gated, format-correct, deployed to /opt/sound — and named
# by nothing in `common/audio.c`, so `brick_breaker` went on playing generated
# tones at the five sites `sounds/prompts.md` had already assigned those files to.
# Everything above measures a file's FORMAT and its ENERGY; no measurement of a
# WAV can see that no caller exists.  Reported by ear on `.188` 2026-08-23.
#
# Two directions, exactly as ./check-bed-files.sh does for the music beds:
#   orphan  — a committed fx_*.wav that FX_DEFAULT_PATH never names
#   missing — an FX_DEFAULT_PATH entry with no committed file behind it
#
# Negative control, and it takes one command — see the gate fail without
# touching shipped source:
#   cp sounds/fx_click.wav sounds/fx_decoy.wav && ./check-sound-assets.sh; \
#     rm sounds/fx_decoy.wav
AUDIO_C="$(dirname "$0")/common/audio.c"
n_orphan=0; n_missing=0
if [ ! -f "$AUDIO_C" ]; then
    echo -e "${YELLOW}    ! claim check skipped: $AUDIO_C not found${NC}"
else
    for f in "${FILES[@]}"; do
        base=$(basename "$f")
        grep -q "\"/opt/sound/$base\"" "$AUDIO_C" || {
            n_orphan=$((n_orphan + 1))
            echo -e "${RED}    ✗ $base: no AudioFxId names it — nothing can play it${NC}"
        }
    done
    # The other direction: a name pointing at a file we do not ship.
    while read -r want; do
        [ -n "$want" ] || continue
        [ -f "$DIR/$want" ] || {
            n_missing=$((n_missing + 1))
            echo -e "${RED}    ✗ $want: named in audio.c, not committed in $DIR${NC}"
        }
    done < <(grep -o '"/opt/sound/fx_[a-z_]*\.wav"' "$AUDIO_C" \
             | tr -d '"' | sed 's|.*/||' | sort -u)
fi

total=$((n_ok + n_warn + n_fail))
echo "SOUND-SUMMARY checked=$total ok=$n_ok warn=$n_warn fail=$n_fail" \
     "orphan=$n_orphan missing=$n_missing" \
     "ctl200=$CTL_LOW ctl2k=$CTL_HIGH"

if [ $n_orphan -gt 0 ] || [ $n_missing -gt 0 ]; then
    echo -e "${RED}  x Sound assets: $n_orphan unplayable, $n_missing named but absent${NC}"
    exit 1
fi
if [ $n_fail -gt 0 ]; then
    echo -e "${RED}  ✗ Sound assets: $n_fail of $total unusable${NC}"
    exit 1
fi
if [ $n_warn -gt 0 ]; then
    echo -e "${GREEN}  ✓ Sound assets: $total in format, $n_warn weak in-band (not a blocker)${NC}"
else
    echo -e "${GREEN}  ✓ Sound assets: $total in format and above the knee${NC}"
fi
exit 0
