/*
 * audio_bed.h — a game's MUSIC BED as a playlist, with the four states and the
 * hold/resume rules that make a level change and a death sound right.
 *
 * ⚠️ **This is an EXTRACTION, not a new design.**  Every rule below shipped in
 * `platformer.c` first and was verified on `.188`; it moved here the moment a
 * second game wanted a bed, because ~140 lines of state machine copied twice is
 * where the two copies drift (`../IMPROVEMENT_PLAN.md` F1 Phase 5 ⑤).  The
 * beds for seven games are committed and deployed, and this is what lets them
 * reach a player.
 *
 * A game owns exactly three things and this file owns the rest:
 *   1. an `AudioBed` next to its `Audio`,
 *   2. one `audio_bed_init()` naming its config prefix, its bed files and how
 *      many there are,
 *   3. one `audio_bed_service(&bed, want_play, want_hold)` per loop iteration,
 *      beside `audio_pump()`, where the two bools ARE the game's own idea of
 *      "am I playing" and "am I dying/paused".  That predicate is the only part
 *      that cannot be shared: no two games spell their screens the same way.
 *
 * ⚠️ **want_hold versus want_stop is the whole point.**  HOLD releases the voice
 * but keeps the FILE open at its read position, so a respawn resumes the same
 * track mid-bar; STOP lets it go, and IDLE then starts the NEXT playlist entry.
 * That difference is exactly "the music continues" versus "the level restarts
 * its music" — the thing the operator's request turned on (2026-08-22).  A
 * fresh start ADVANCES the playlist and a resume does not, so losing a life
 * can never skip a track.
 */
#ifndef AUDIO_BED_H
#define AUDIO_BED_H

#include <stdbool.h>
#include "audio.h"
#include "config.h"

/* The array cap, not a game's track count — the most any game ships today is 6
 * (`music/brickbreaker[1-6]-mono.wav`, `music/officerunner[1-6]-mono.wav`).
 * A count above this is clamped and said so, rather than overrunning. */
#define AUDIO_BED_MAX_TRACKS   8

/* Where the beds live on the device, and the file-name shape the defaults are
 * derived from: <AUDIO_BED_DIR>/<name><n>-mono.wav.  ⚠️ Every committed bed
 * follows it and `check-sound-assets.sh` gates that it keeps doing so — a typo
 * in a game's bed name would otherwise ship a game that is simply silent, with
 * no error anywhere.  A file that CANNOT follow it is still reachable: name it
 * in `rw_config.conf`, which overrides every slot. */
#define AUDIO_BED_DIR          "/opt/sound"
#define AUDIO_BED_SUFFIX       "-mono.wav"

typedef enum {
    AUDIO_BED_IDLE,      /* nothing on the bus (nothing started, or a fade finished) */
    AUDIO_BED_PLAYING,   /* a voice is sounding                                      */
    AUDIO_BED_HELD,      /* paused: released, but the FILE is still open at its pos  */
    AUDIO_BED_STOPPING   /* released for good; waiting for the fade to finish        */
} AudioBedState;

typedef struct {
    Audio        *audio;
    char          tag[24];                                  /* log prefix + key prefix */
    char          track[AUDIO_BED_MAX_TRACKS][CONFIG_VAL_LEN];
    bool          failed[AUDIO_BED_MAX_TRACKS];             /* refused: per TRACK       */
    int           count;                                    /* non-empty slots          */
    int           next;                                     /* the playlist cursor      */
    AudioBedState state;
    bool          disabled;
} AudioBed;

/*
 * Read the playlist and say what it is.  Call once, after audio_init().
 *
 *   tag         the game's name — the log prefix, and the config key prefix:
 *               "<tag>_music" names track 1 and "<tag>_music2".."<tag>_musicN"
 *               the rest.  ⚠️ Track 1's key carries NO number, because
 *               `platformer_music` shipped before the playlist existed and is
 *               also the whole-game OFF SWITCH: set it empty and the game plays
 *               its effects in silence.
 *   bed_name    the file-name stem, which is often NOT the tag: platformer's
 *               beds are `officerunner<n>-mono.wav`.  The shipped default for
 *               slot n is <AUDIO_BED_DIR>/<bed_name><n><AUDIO_BED_SUFFIX>.
 *   track_count how many slots to look for, 1..AUDIO_BED_MAX_TRACKS.
 *
 * ⚠️ The defaults are DERIVED rather than passed in as a table, and that is what
 * makes adding a bed to a game three lines.  It also means the shipped default
 * IS the playlist, so `rw_config.conf` only has to exist in order to CHANGE it.
 */
void audio_bed_init(AudioBed *bed, Audio *audio, const char *tag,
                    const char *bed_name, int track_count);

/*
 * One transition per call, from the game's loop beside audio_pump().
 *
 * ⚠️ **Every path is gated on `audio_music_active()`** rather than on this
 * struct's own idea of what the bus is doing: a release takes frames to walk
 * down, `audio_music_resume()` is refused until it has, and PUMP: OFF can clear
 * the voice out from under us.  The state here tracks INTENT; the mixer is
 * asked about reality.
 *
 * want_play and want_hold are the game's predicates, evaluated fresh each call:
 *   want_play  the player is playing.  ⚠️ NOT during a level-complete overlay —
 *              dropping it there is the entire mechanism of the per-level track
 *              change, and it is what lets audio_success() play over
 *              near-silence.
 *   want_hold  paused, or dying.  A death holds the bed so the fail sound is
 *              heard over near-silence, and the respawn continues the track.
 * Neither true, and the bed stops for good; the next fresh start takes the next
 * entry in the playlist.
 */
void audio_bed_service(AudioBed *bed, bool want_play, bool want_hold);

#endif /* AUDIO_BED_H */
