#!/bin/bash
# Gate: every music bed a game ASKS for exists as a committed file, and every
# committed bed reaches some game.
#
# Run from build-and-deploy.sh, beside check-arm-safe.sh, check-audio-pacing.sh
# and check-sound-assets.sh.  Standalone:
#     ./check-bed-files.sh
#
# ── WHY THIS EXISTS ─────────────────────────────────────────────────────────
# A bed is named by CONVENTION — audio_bed_init() derives slot n's default path
# as /opt/sound/<name><n>-mono.wav (common/audio_bed.h) — and ⚠️ **a wrong name
# or a count one too high is refused in SILENCE.** audio_bed_service() marks the
# track failed, prints one line into a log nobody reads at play time, and the
# game plays its effects with no music: exactly what a game with no bed at all
# sounds like.  Nothing else in the tree can see the difference.
#
# The second half is the state this repo was actually in on 2026-08-22: 24 beds
# committed and deployed, of which 18 reached NO game because only platformer had
# a bed consumer.  That is a warning rather than a failure — staging files before
# the code that plays them is legitimate — but it must be visible.
#
# ⚠️ WHAT THIS CANNOT SEE: whether the device has the file. The gate reads the
# repo's music/ directory, which is what build-and-deploy.sh uploads; a device
# whose /opt/sound was hand-managed can still be missing one, and a path named in
# rw_config.conf is outside the convention by design and is not checked at all.
# It is also a TEXT match, so a commented-out audio_bed_init() counts as a game —
# which is how its own negative control is built (a throwaway .c under the tree
# with a bogus name makes it exit 1, no shipped file touched).
set -u

RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'; NC='\033[0m'
cd "$(dirname "$0")"

MUSIC_DIR="music"
n_games=0; n_missing=0; n_want=0
claimed=""

# Literal args only — the C side is required to spell them out for exactly this
# reason (see the comment above platformer's call).
matches="$(grep -rhoE 'audio_bed_init\(&bed, &audio, "[^"]+", "[^"]+", *[0-9]+' \
            --include='*.c' . 2>/dev/null || true)"

if [ -z "$matches" ]; then
    echo "BED-SUMMARY games=0 want=0 missing=0 orphan=0"
    echo -e "${YELLOW}  ! No audio_bed_init() call found — no game has a bed${NC}"
    exit 0
fi

while IFS= read -r m; do
    [ -n "$m" ] || continue
    tag="$( printf '%s' "$m" | sed -E 's/.*"([^"]+)", *"([^"]+)", *([0-9]+)$/\1/')"
    name="$(printf '%s' "$m" | sed -E 's/.*"([^"]+)", *"([^"]+)", *([0-9]+)$/\2/')"
    cnt="$( printf '%s' "$m" | sed -E 's/.*"([^"]+)", *"([^"]+)", *([0-9]+)$/\3/')"
    n_games=$((n_games + 1))
    miss=""
    i=1
    while [ "$i" -le "$cnt" ]; do
        f="$MUSIC_DIR/${name}${i}-mono.wav"
        n_want=$((n_want + 1))
        if [ -f "$f" ]; then claimed="$claimed $(basename "$f")"
        else miss="$miss ${name}${i}-mono.wav"; n_missing=$((n_missing + 1)); fi
        i=$((i + 1))
    done
    printf "  %-16s %d track(s) as %s<n>-mono.wav%s\n" "$tag" "$cnt" "$name" \
        "$([ -n "$miss" ] && echo "  MISSING:$miss")"
done <<< "$matches"

# Beds that reach nothing.  ⚠️ Counted against the CLAIMED list rather than
# against a per-game name, so a file claimed by two games counts once.
n_orphan=0; orphans=""
if ls "$MUSIC_DIR"/*.wav >/dev/null 2>&1; then
    for f in "$MUSIC_DIR"/*.wav; do
        b="$(basename "$f")"
        case " $claimed " in *" $b "*) ;; *) n_orphan=$((n_orphan + 1)); orphans="$orphans $b";; esac
    done
fi

echo "BED-SUMMARY games=$n_games want=$n_want missing=$n_missing orphan=$n_orphan"
if [ "$n_orphan" -gt 0 ]; then
    echo -e "${YELLOW}  ! $n_orphan committed bed(s) reach no game:$orphans${NC}"
fi
if [ "$n_missing" -gt 0 ]; then
    echo -e "${RED}  ✗ Music beds: $n_missing file(s) a game asks for are not in $MUSIC_DIR/${NC}"
    exit 1
fi
echo -e "${GREEN}  ✓ Music beds: $n_want file(s) for $n_games game(s), all present${NC}"
exit 0
