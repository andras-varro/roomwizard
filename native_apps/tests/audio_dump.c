/*
 * audio_dump — render the shipped mix chain to a WAV file, so the digital
 * stream can be judged on a machine with a decent speaker.
 *
 * ⚠️ **This exists because there is no microphone on this device, so "it sounds
 * distorted" cannot be attributed to the digital chain or to the speaker by
 * listening on the panel alone.**  The operator's 2026-08-19 listen reported
 * distortion on overlapping sounds that was independent of the level, while
 * `/tmp/mix.log` from that same session showed `starve=0`, `lost=0`, `drop=0`
 * and `clip` peaking at 133 samples — i.e. no underrun, no dropped voice and
 * negligible clamping.  Those counters exonerate pacing and the clamp but say
 * nothing about what the speaker does with a sustained sine, which is the worst
 * possible signal for a small driver in a plastic enclosure.
 *
 * So: dump the exact bytes the shipped path hands the kernel, play them on a
 * host, and the ambiguity is gone.  Clean on a host means the generator and the
 * mixer are innocent and the panel's roughness is acoustic; rough on a host
 * means it is ours and the file says which sample.
 *
 * ⚠️ **The chain here must stay byte-identical to `audio_play_frames()` in
 * `../common/audio.c`** — `audio_mix_render()` (mono) → `audio_interleave()` →
 * `audio_attenuate()`.  A dump that skips the interleave or the master shift is
 * measuring something the device never played, which is worse than no dump.
 * The rate/format/channels below are what `.188` GRANTED, read from the session
 * log's `audio_out: /dev/dsp open, granted ...` line, not what we requested.
 *
 * Host build (this needs no device and no cross-compiler):
 *   gcc -O2 -Wall -Wextra -o /tmp/audio_dump native_apps/tests/audio_dump.c \
 *       native_apps/common/audio_gen.c -lm
 *   /tmp/audio_dump <outdir>
 *
 * Rules and the level model: ../CLAUDE.md → *Mixing*.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../common/audio_gen.h"

/* What /dev/dsp GRANTED on .188 — "granted 44100 Hz 16-bit 2 ch". */
#define DUMP_RATE      44100
#define DUMP_CHANNELS  2
#define DUMP_SHIFT     1      /* AUDIO_MASTER_SHIFT */
#define DUMP_VOL       96     /* AUDIO_VOICE_VOL — rung 3/6, the ear's pick */

/* Render in the same order of magnitude the pump uses, so a bug that depends on
 * the chunk boundary has a chance to show up here too. */
#define CHUNK_FRAMES   2048   /* the device period from the session log */

typedef struct { int freq; int ms; int delay_ms; } Note;

/* ---------------------------------------------------------------- WAV output */

static void put_le32(FILE *f, uint32_t v)
{
    fputc((int)( v        & 0xff), f);
    fputc((int)((v >>  8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f);
    fputc((int)((v >> 24) & 0xff), f);
}

static void put_le16(FILE *f, uint16_t v)
{
    fputc((int)( v       & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
}

/* A canonical 44-byte PCM header.  `data_bytes` is the payload that follows. */
static void wav_header(FILE *f, uint32_t data_bytes)
{
    uint32_t byte_rate   = (uint32_t)DUMP_RATE * DUMP_CHANNELS * 2u;
    uint16_t block_align = (uint16_t)(DUMP_CHANNELS * 2);

    fwrite("RIFF", 1, 4, f);
    put_le32(f, 36u + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put_le32(f, 16);                        /* fmt chunk size   */
    put_le16(f, 1);                         /* PCM              */
    put_le16(f, (uint16_t)DUMP_CHANNELS);
    put_le32(f, (uint32_t)DUMP_RATE);
    put_le32(f, byte_rate);
    put_le16(f, block_align);
    put_le16(f, 16);                        /* bits per sample  */
    fwrite("data", 1, 4, f);
    put_le32(f, data_bytes);
}

/* ------------------------------------------------------------ one scenario */

/*
 * Render `notes` through a real AudioMixer and write `path`.
 *
 * ⚠️ The knee is re-derived for the volume in use, exactly as
 * `audio_set_volume()` does on the device — a knee left at its init value while
 * the amplitude moves is the documented way to bend a LONE tone, and dumping
 * that would blame the mixer for a stale constant.
 */
static int dump_scenario(const char *path, const char *what,
                         const Note *notes, int count, int limit_mode)
{
    AudioMixer m;
    int16_t   *mono = NULL;
    int16_t   *ilv  = NULL;
    FILE      *f    = NULL;
    uint32_t   data_bytes = 0;
    long       total_frames = 0;
    int        i, rc = -1;

    audio_mix_init(&m, DUMP_RATE);
    audio_mix_set_limit(&m, limit_mode);
    audio_mix_set_knee(&m, audio_voice_peak(DUMP_VOL));

    for (i = 0; i < count; i++) {
        if (audio_mix_add(&m, notes[i].freq, notes[i].ms, notes[i].delay_ms,
                          audio_voice_peak(DUMP_VOL)) < 0) {
            fprintf(stderr, "%s: bus refused voice %d\n", what, i);
            return -1;
        }
    }

    mono = (int16_t *)malloc(sizeof(int16_t) * CHUNK_FRAMES);
    ilv  = (int16_t *)malloc((size_t)audio_bytes_for_frames(CHUNK_FRAMES,
                                                           DUMP_CHANNELS));
    f    = fopen(path, "wb");
    if (!mono || !ilv || !f) { fprintf(stderr, "%s: alloc/open failed\n", what); goto out; }

    wav_header(f, 0);                       /* patched once the size is known */

    for (;;) {
        long got = audio_mix_render(&m, mono, CHUNK_FRAMES);
        long bytes;
        if (got <= 0) break;

        /* The shipped order: interleave to the granted channel count, THEN the
         * one device attenuation.  Reversing these two changes nothing today at
         * shift 1, but it would the moment the shift stops being a power of the
         * channel count — keep them in the shipped order. */
        bytes = audio_interleave(mono, got, DUMP_CHANNELS, ilv);
        if (bytes <= 0) { fprintf(stderr, "%s: interleave failed\n", what); goto out; }
        audio_attenuate(ilv, got * (long)DUMP_CHANNELS, DUMP_SHIFT);

        if (fwrite(ilv, 1, (size_t)bytes, f) != (size_t)bytes) {
            fprintf(stderr, "%s: short write\n", what);
            goto out;
        }
        data_bytes   += (uint32_t)bytes;
        total_frames += got;
    }

    if (fseek(f, 0, SEEK_SET) != 0) { fprintf(stderr, "%s: seek failed\n", what); goto out; }
    wav_header(f, data_bytes);
    rc = 0;

    printf("  %-34s %7ld frames %8u bytes  clip=%u lim=%u  %s\n",
           what, total_frames, data_bytes,
           m.clipped, m.limited,
           (limit_mode == AUDIO_MIX_HARD) ? "HARD" : "SOFT");

out:
    if (f)    fclose(f);
    free(mono);
    free(ilv);
    return rc;
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    char path[512];
    int  bad = 0;

    /* The four cases the 2026-08-19 listen actually reported on, plus the CHORD
     * A/B whose LIM toggle the operator heard no difference from.  `audio_fail()`
     * is deliberately here too: 392/330/262 Hz is the band this speaker is worst
     * in, so if anything is acoustic rather than digital it is this one. */
    static const Note one440[]   = { { 440, 3000, 0 } };
    static const Note one1760[]  = { { 1760, 3000, 0 } };
    static const Note overlap[]  = { { 440, 3000, 0 }, { 880, 200, 1000 } };
    static const Note success[]  = { { 523, 120, 0 }, { 659, 120, 120 }, { 784, 220, 240 } };
    static const Note succ880[]  = { { 523, 120, 0 }, { 659, 120, 120 }, { 784, 220, 240 },
                                     { 880, 200, 60 } };
    static const Note chord[]    = { { 523, 400, 0 }, { 659, 400, 0 }, { 784, 400, 0 } };
    static const Note fail[]     = { { 392, 150, 0 }, { 330, 150, 150 }, { 262, 300, 300 } };

    printf("audio_dump: %d Hz, %d ch, shift %d, vol %d -> voice peak %d\n",
           DUMP_RATE, DUMP_CHANNELS, DUMP_SHIFT, DUMP_VOL,
           audio_voice_peak(DUMP_VOL));
    printf("            acoustic peak %d  (voice peak >> shift)\n\n",
           audio_voice_peak(DUMP_VOL) >> DUMP_SHIFT);

#define DUMP(file, label, arr, mode) \
    do { \
        snprintf(path, sizeof(path), "%s/%s", dir, (file)); \
        if (dump_scenario(path, (label), (arr), \
                          (int)(sizeof(arr) / sizeof((arr)[0])), (mode)) != 0) bad++; \
    } while (0)

    DUMP("01-single-440-3s.wav",     "single 440 Hz 3 s",        one440,  AUDIO_MIX_HARD);
    DUMP("02-single-1760-3s.wav",    "single 1760 Hz 3 s",       one1760, AUDIO_MIX_HARD);
    DUMP("03-overlap-440-880.wav",   "440 3 s + 880 at t=1 s",   overlap, AUDIO_MIX_HARD);
    DUMP("04-success.wav",           "SUCCESS arpeggio",         success, AUDIO_MIX_HARD);
    DUMP("05-success-plus-880.wav",  "SUCCESS + 880 overlap",    succ880, AUDIO_MIX_HARD);
    DUMP("06-chord-hard.wav",        "CHORD 523+659+784 HARD",   chord,   AUDIO_MIX_HARD);
    DUMP("07-chord-soft.wav",        "CHORD 523+659+784 SOFT",   chord,   AUDIO_MIX_SOFT);
    DUMP("08-fail-low-band.wav",     "FAIL 392/330/262 Hz",      fail,    AUDIO_MIX_HARD);

#undef DUMP

    if (bad) {
        fprintf(stderr, "\naudio_dump: %d scenario(s) FAILED\n", bad);
        return 1;
    }
    printf("\naudio_dump: 8 files written to %s\n", dir);
    return 0;
}
