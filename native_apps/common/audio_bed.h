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

/* The array cap, and now also how far audio_bed_init() looks for numbered keys —
 * the most any game ships today is 6 (`music/brickbreaker[1-6]-mono.wav`,
 * `music/officerunner[1-6]-mono.wav`).  ⚠️ A set file carrying a key past this
 * is IGNORED rather than overrunning, and says so: raising the cap is the fix,
 * not a per-game count, which is the parameter this change deleted. */
#define AUDIO_BED_MAX_TRACKS   8

/* ⚠️ AUDIO_BED_DIR / AUDIO_BED_SUFFIX used to live here, and the C side used to
 * DERIVE slot n's path as <dir>/<stem><n><suffix>.  Both are gone: the sound-set
 * file is now the ONE home for a game's bed paths and the convention is spelled
 * only in native_apps/sound-sets.sh.  ⚠️ **Do not teach this file the convention
 * again** — a derived fallback beside a file that already carries the paths is
 * the same fact in two places, which is what the operator's 2026-08-24 call
 * refused.  A game with no set file has no music, and that is the answer, not a
 * gap to fill.
 */

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
 *   tag   the game's name, and the ONLY thing a game declares.  It names both
 *         the set file (<CONFIG_SOUND_SET_DIR>/<tag><CONFIG_SOUND_SET_EXT>) and
 *         the keys inside it: "<tag>_music" is track 1 and
 *         "<tag>_music2".."<tag>_musicN" the rest.  ⚠️ Track 1's key carries NO
 *         number, because `platformer_music` shipped before the playlist existed
 *         and is also the whole-game OFF SWITCH: present but empty and the game
 *         plays its effects in silence.
 *
 * ⚠️ The track COUNT is not a parameter — it is however many numbered keys the
 * set file actually carries, up to AUDIO_BED_MAX_TRACKS.  That is what makes the
 * file the only home: a track is added or dropped by editing the file, with no
 * matching edit in C, and the two can therefore never disagree.  A gap ends
 * nothing and is skipped, so deleting a middle line is legal.
 *
 * ⚠️ No set file means NO MUSIC, reported as one log line and nothing worse.
 * Effects are unaffected — they are the mixer's, not the bed's.
 */
void audio_bed_init(AudioBed *bed, Audio *audio, const char *tag);

/*
 * The RESOLUTION half of the above, over an already-loaded Config: it reads
 * "<bed->tag>_music" and its numbered siblings out of `cfg` and fills the
 * playlist.  audio_bed_init() is this plus the file load, and nothing else calls
 * it in shipped code.
 *
 * It is split out for the same reason `audio_gen.c` is split out of `audio.c`: it
 * has NO path of its own, so `tests/audio_bed_test.c` can drive the real
 * resolution from a temp file instead of needing CONFIG_SOUND_SET_DIR to exist on
 * the host.  ⚠️ Keep the split — folding it back inline makes every rule below it
 * (absent vs empty, the gap, the cap) reachable only on a device.
 *
 * Expects `bed` already zeroed with `tag` set, exactly as audio_bed_init() leaves
 * it before the load.
 */
void audio_bed_set_playlist(AudioBed *bed, Config *cfg);

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
