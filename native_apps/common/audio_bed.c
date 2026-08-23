/*
 * audio_bed.c — the music-bed playlist and its four states.  See audio_bed.h
 * for what a game owns and what this file owns; the rules are the ones that
 * shipped in platformer.c and were verified on `.188` (2026-08-22).
 */
#include "audio_bed.h"

#include <stdio.h>
#include <string.h>

void audio_bed_init(AudioBed *bed, Audio *audio, const char *tag,
                    const char *bed_name, int track_count)
{
    if (!bed) return;
    memset(bed, 0, sizeof(*bed));
    bed->audio = audio;
    bed->state = AUDIO_BED_IDLE;
    snprintf(bed->tag, sizeof bed->tag, "%s", tag ? tag : "game");

    if (track_count < 1) track_count = 1;
    if (track_count > AUDIO_BED_MAX_TRACKS) {
        printf("%s: %d bed tracks asked for, %d is the cap — the rest are ignored\n",
               bed->tag, track_count, AUDIO_BED_MAX_TRACKS);
        track_count = AUDIO_BED_MAX_TRACKS;
    }

    Config cfg;
    config_init(&cfg);
    config_load(&cfg);                     /* silent when the file is absent */

    for (int i = 0; i < track_count; i++) {
        /* Slot 0's key is the historic one and carries the off switch, so it has
         * no number — see the header. */
        char key[CONFIG_KEY_LEN];
        if (i == 0) snprintf(key, sizeof key, "%s_music", bed->tag);
        else        snprintf(key, sizeof key, "%s_music%d", bed->tag, i + 1);

        char def[CONFIG_VAL_LEN];
        snprintf(def, sizeof def, "%s/%s%d%s", AUDIO_BED_DIR,
                 bed_name ? bed_name : bed->tag, i + 1, AUDIO_BED_SUFFIX);

        const char *p = config_get(&cfg, key, def);
        if (i == 0 && !p[0]) {
            /* ⚠️ Track 1 EMPTY means silence for the whole game, deliberately:
             * it preserves `<tag>_music=` as the off switch it already was.  A
             * per-track reading would have broken that, since every other slot
             * has a non-empty default too. */
            bed->disabled = true;
            printf("%s: music bed disabled by %s (%s=)\n",
                   bed->tag, CONFIG_FILE_PATH, key);
            return;
        }
        if (!p[0]) continue;               /* a gap ends nothing; it is skipped */
        snprintf(bed->track[bed->count], sizeof bed->track[0], "%s", p);
        bed->count++;
    }
    printf("%s: music playlist — %d track(s), first %s\n",
           bed->tag, bed->count, bed->count ? bed->track[0] : "(none)");
}

void audio_bed_service(AudioBed *bed, bool want_play, bool want_hold)
{
    if (!bed || !bed->audio || bed->disabled || bed->count == 0) return;

    switch (bed->state) {
    case AUDIO_BED_IDLE: {
        if (!want_play || audio_music_active(bed->audio)) break;
        /* Walk the playlist from the cursor, skipping entries that already
         * failed.  ⚠️ Bounded by ONE lap: without the counter a playlist whose
         * every entry is missing would spin here for the life of the process. */
        int tried = 0;
        while (tried < bed->count && bed->failed[bed->next]) {
            bed->next = (bed->next + 1) % bed->count;
            tried++;
        }
        if (tried >= bed->count) {          /* every track refused — stop asking */
            bed->disabled = true;
            break;
        }
        int t = bed->next;
        if (audio_music_start(bed->audio, bed->track[t], true)) {
            bed->state = AUDIO_BED_PLAYING;
            bed->next  = (t + 1) % bed->count;      /* the NEXT fresh start */
            break;
        }
        /* A missing file, a rate mismatch or a bus that never came up are all
         * permanent for THIS TRACK, and retrying per frame would fill
         * /var/log/roomwizard/app_stdout.log with one refusal per 33 ms.
         * ⚠️ Per track rather than per process, so one bad path in the config
         * does not silence the tracks that are fine — and the MUSIC toggle being
         * off is reported as itself rather than as a missing file. */
        bed->failed[t] = true;
        if (!audio_music_enabled(bed->audio)) {
            bed->disabled = true;           /* one line, not one per track */
            printf("%s: music is off (music_enabled=false) — effects and play continue\n",
                   bed->tag);
        } else {
            printf("%s: no music bed at %s — effects and play continue\n",
                   bed->tag, bed->track[t]);
        }
        break;
    }

    case AUDIO_BED_PLAYING:
        if (want_play) {
            /* 200 loop passes ran out (~2.4 h), or PUMP: OFF cleared the bus:
             * re-arm from IDLE rather than stay silent for the session. */
            if (!audio_music_active(bed->audio)) bed->state = AUDIO_BED_IDLE;
        } else if (want_hold) {
            if (audio_music_pause(bed->audio)) bed->state = AUDIO_BED_HELD;
        } else {
            audio_music_stop(bed->audio);
            bed->state = AUDIO_BED_STOPPING;
        }
        break;

    case AUDIO_BED_HELD:
        if (want_hold) break;
        if (!want_play) { bed->state = AUDIO_BED_STOPPING; break; } /* died out, or quit */
        if (audio_music_active(bed->audio)) break;                  /* release still fading */
        if (audio_music_resume(bed->audio)) { bed->state = AUDIO_BED_PLAYING; break; }
        /* ⚠️ IDLE, not disabled: a refused resume loses the held file, but the
         * playlist is intact and the next level can start a fresh voice.  Marking
         * the track failed here would silence a game for one recoverable hiccup. */
        bed->state = AUDIO_BED_IDLE;
        printf("%s: music bed could not resume — starting fresh at the next level\n",
               bed->tag);
        break;

    case AUDIO_BED_STOPPING:
        if (!audio_music_active(bed->audio)) bed->state = AUDIO_BED_IDLE;
        break;
    }
}
