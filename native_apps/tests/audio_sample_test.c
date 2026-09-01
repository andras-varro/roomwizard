/*
 * tests/audio_sample_test.c — the sample voice, from the RIFF walk to the bus.
 *
 * The mix bus needs a voice that plays a file, and the two things most
 * likely to be wrong about it are invisible on the panel: WHERE the PCM starts,
 * and whether a streamed voice survives being rendered in pieces.  Both are
 * pinned here, on the host, before anything is deployed.
 *
 * ⚠️ **The `data` offset is the defect this file exists to catch, and the vendor
 * effects cannot catch it.**  A reader that assumes the canonical 44-byte header
 * is CORRECT on `/opt/sound/asl_*.wav` (`data` ID at 36, PCM at 44) and wrong on
 * `/opt/sound/officerunner1-mono.wav` (164 and 172 — ffmpeg wrote a LIST/INFO chunk
 * holding an encoder version string).  So a suite that tested only the effects
 * would pass while the music played 128 bytes of ASCII as audio.  Group A builds
 * both shapes and asserts the offsets measured on `.188` 2026-08-20.
 *
 * The fixtures are written by this file, so there is nothing to stage and no
 * device to reach.  Nothing here opens /dev/dsp: the mixer is `audio_gen.c`,
 * which has no fd, no ioctl and no clock, and that is exactly why it can be
 * tested this way.
 *
 * Build and run (host gcc, from native_apps/):
 *   gcc -Wall -Wextra -Wno-unused-parameter -I. \
 *       -o build/audio_sample_test tests/audio_sample_test.c \
 *       common/audio_wav.c common/audio_gen.c -lm && \
 *   ./build/audio_sample_test
 *
 * ⚠️ No `-Itests/hostshim` here, and no `common/audio.c`: this suite links only
 * the two files that have no device half, so it needs no OSS shim at all.  The
 * ARM build is the same line with the cross compiler plus `-O2 -static`.
 *
 * Cross-check on the device:
 *   arm-linux-gnueabihf-gcc -Wall -Wextra -Wno-unused-parameter -O2 -static -I. \
 *       -o build/audio_sample_test_arm tests/audio_sample_test.c \
 *       common/audio_wav.c common/audio_gen.c -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common/audio_wav.h"
#include "common/audio_gen.h"

static int checks = 0, failures = 0;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("  FAIL  %s\n", what); }
    else                   printf("  ok    %s\n", what);
}

#define RATE 44100

/* ── fixtures ──────────────────────────────────────────────────────────────
 * Written little-endian byte by byte rather than by `fwrite`-ing a struct, so
 * the fixture's layout is stated here rather than inherited from the host's
 * padding and endianness.  (`tests/audio_path_dump.c`'s writer does the raw
 * form; it is fine on two LE hosts and this is not relying on that.)
 */
static void put32(FILE *f, unsigned long v)
{
    fputc((int)(v & 0xff), f);        fputc((int)((v >> 8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f); fputc((int)((v >> 24) & 0xff), f);
}

static void put16(FILE *f, unsigned int v)
{
    fputc((int)(v & 0xff), f); fputc((int)((v >> 8) & 0xff), f);
}

/* `info_bytes` > 0 writes a LIST/INFO chunk of that payload size before `data`,
 * which is what ffmpeg did to the music.  0 gives the canonical 44-byte file. */
static void write_fixture(const char *path, const int16_t *pcm, long frames,
                          int channels, long info_bytes)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  (cannot write %s)\n", path); return; }

    long data_bytes = frames * 2 * channels;
    long list_total = (info_bytes > 0) ? (8 + info_bytes + (info_bytes & 1)) : 0;

    fwrite("RIFF", 1, 4, f);
    put32(f, (unsigned long)(4 + 24 + list_total + 8 + data_bytes));
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);  put32(f, 16);
    put16(f, 1);                                  /* PCM                      */
    put16(f, (unsigned int)channels);
    put32(f, RATE);
    put32(f, (unsigned long)(RATE * 2 * channels));
    put16(f, (unsigned int)(2 * channels));
    put16(f, 16);                                 /* bits                     */

    if (info_bytes > 0) {
        fwrite("LIST", 1, 4, f);  put32(f, (unsigned long)info_bytes);
        fwrite("INFO", 1, 4, f);                  /* 4 of the payload         */
        /* The rest is the kind of thing that was really in there: text.  If a
         * reader mistakes this for PCM it is loud and obvious — which is the
         * point of using text rather than zeros. */
        for (long i = 4; i < info_bytes; i++) fputc('A' + (int)(i % 26), f);
        /* ⚠️ RIFF pads an odd-sized chunk to an even boundary, and the pad byte
         * is NOT counted in the chunk's own size.  A reader that skips by size
         * alone lands one byte short and reads "ata" plus the pad as the next
         * chunk ID — so an odd fixture is the only thing that can catch a
         * dropped pad.  Measured: without this the sabotage sweep's case 3
         * changed the reader and nothing failed. */
        if (info_bytes & 1) fputc(0, f);
    }

    fwrite("data", 1, 4, f);  put32(f, (unsigned long)data_bytes);
    fwrite(pcm, 2, (size_t)(frames * channels), f);
    fclose(f);
}

/* A ramp, so any frame's expected value is a function of its index alone and a
 * misaligned read is visible rather than merely different. */
static void fill_ramp(int16_t *pcm, long n, int step)
{
    for (long i = 0; i < n; i++) pcm[i] = (int16_t)(i * step);
}

int main(void)
{
    static int16_t pcm[4096];
    static int16_t got[8192];
    static int16_t vbuf[2048];
    AudioWav w;

    printf("audio_sample_test — the sample voice\n\n");

    /* ── A. the chunk walk ───────────────────────────────────────────────── */
    printf("A. the RIFF walk finds `data` where it really is\n");
    {
        fill_ramp(pcm, 1000, 7);
        write_fixture("/tmp/as_plain.wav", pcm, 1000, 1, 0);
        write_fixture("/tmp/as_list.wav",  pcm, 1000, 1, 120);

        /* The vendor shape.  36 is the chunk ID, 44 the first PCM byte. */
        check(audio_wav_open(&w, "/tmp/as_plain.wav", false), "canonical file opens");
        check(w.data_pos == 44, "canonical: PCM starts at 44");
        check(w.frames == 1000, "canonical: 1000 frames");
        check(w.rate == RATE && w.channels == 1, "canonical: 44100 / 1 ch");
        audio_wav_close(&w);

        /* The music shape.  A 120-byte LIST payload puts `data`'s ID at
         * 36 + 8 + 120 = 164 and its PCM at 172 — the offsets measured on the
         * device.  ⚠️ This is the check that a 44-byte assumption fails. */
        check(audio_wav_open(&w, "/tmp/as_list.wav", false), "LIST/INFO file opens");
        check(w.data_pos == 172, "LIST/INFO: PCM starts at 172, not 44");
        check(w.frames == 1000, "LIST/INFO: 1000 frames");

        /* And the payoff, stated as audio rather than as an offset: the first
         * frame read is the ramp's first frame, not two bytes of the text. */
        long n = audio_wav_read(&w, got, 4);
        check(n == 4 && got[0] == 0 && got[1] == 7 && got[2] == 14,
              "LIST/INFO: first frames are PCM, not the version string");
        audio_wav_close(&w);

        /* Negative control for the whole group: the same assertion against a
         * reader that DID assume 44.  If this passes, group A proves nothing. */
        FILE *f = fopen("/tmp/as_list.wav", "rb");
        int16_t naive[4] = { 0, 0, 0, 0 };
        if (f && fseek(f, 44, SEEK_SET) == 0) {
            if (fread(naive, 2, 4, f) != 4) naive[0] = -1;
        }
        if (f) fclose(f);
        check(!(naive[0] == 0 && naive[1] == 7),
              "control: a 44-byte reader gets the WRONG bytes on this file");

        /* An ODD-sized chunk, which is the only shape that exercises the pad.
         * 121 bytes of payload puts `data`'s ID at 36 + 8 + 121 + 1 = 166 and
         * its PCM at 174; a reader that skips by size alone lands at 165. */
        write_fixture("/tmp/as_odd.wav", pcm, 1000, 1, 121);
        check(audio_wav_open(&w, "/tmp/as_odd.wav", false), "odd-sized chunk file opens");
        check(w.data_pos == 174, "odd chunk: PCM at 174 — the RIFF pad byte is skipped");
        long odd_n = audio_wav_read(&w, got, 3);
        check(odd_n == 3 && got[0] == 0 && got[1] == 7,
              "odd chunk: the frames after the pad are PCM");
        audio_wav_close(&w);

        /* 8-bit is refused rather than misread.  `hw:0,0` is S16_LE only, so a
         * reader that accepted this would hand the device half a sample rate's
         * worth of noise. */
        FILE *e = fopen("/tmp/as_8bit.wav", "wb");
        if (e) {
            fwrite("RIFF", 1, 4, e); put32(e, 36 + 100);
            fwrite("WAVE", 1, 4, e);
            fwrite("fmt ", 1, 4, e); put32(e, 16);
            put16(e, 1); put16(e, 1); put32(e, RATE);
            put32(e, RATE); put16(e, 1); put16(e, 8);      /* 8 bits */
            fwrite("data", 1, 4, e); put32(e, 100);
            for (int i = 0; i < 100; i++) fputc(i & 0xff, e);
            fclose(e);
        }
        check(!audio_wav_open(&w, "/tmp/as_8bit.wav", false),
              "an 8-bit file is REFUSED, not read as 16-bit");
    }

    /* ── B. over-claimed data size ───────────────────────────────────────── */
    printf("\nB. a header that over-claims is clamped to the file\n");
    {
        /* Truncate a good file mid-data.  `frames` must describe the bytes that
         * exist, or the bus keeps a voice alive over audio nobody has. */
        write_fixture("/tmp/as_trunc.wav", pcm, 1000, 1, 0);
        FILE *f = fopen("/tmp/as_trunc.wav", "rb");
        static unsigned char raw[8192];
        size_t len = f ? fread(raw, 1, sizeof(raw), f) : 0;
        if (f) fclose(f);
        f = fopen("/tmp/as_trunc.wav", "wb");
        if (f) { fwrite(raw, 1, len - 400, f); fclose(f); }   /* lose 200 frames */

        check(audio_wav_open(&w, "/tmp/as_trunc.wav", false), "truncated file opens");
        check(w.frames == 800, "truncated: 800 frames, not the header's 1000");
        long n = audio_wav_read(&w, got, 1000);
        check(n == 800, "truncated: the read ends where the file does");
        audio_wav_close(&w);
    }

    /* ── C. streaming: pieces must equal the whole ───────────────────────── */
    printf("\nC. an incremental read is byte-identical to one big read\n");
    {
        check(audio_wav_open(&w, "/tmp/as_list.wav", false), "reopen for streaming");
        long total = 0;
        /* Deliberately awkward sizes, none a divisor of 1000 and one larger than
         * the internal batch, so a batching bug cannot hide behind alignment. */
        const long sizes[6] = { 1, 3, 97, 1024, 1500, 700 };
        for (int i = 0; i < 6 && total < 1000; i++) {
            long n = audio_wav_read(&w, got + total, sizes[i]);
            total += n;
            if (n == 0) break;
        }
        check(total == 1000, "streamed reads deliver every frame exactly once");

        int same = 1;
        for (long i = 0; i < total; i++)
            if (got[i] != (int16_t)(i * 7)) { same = 0; break; }
        check(same, "streamed frames are in order and unmodified");

        long past = audio_wav_read(&w, got, 16);
        check(past == 0, "a drained non-looping reader returns 0, not garbage");
        audio_wav_close(&w);

        /* Control: the ramp comparison must be capable of failing. */
        got[500] = (int16_t)(500 * 7 + 1);
        int still = 1;
        for (long i = 0; i < 1000; i++)
            if (got[i] != (int16_t)(i * 7)) { still = 0; break; }
        check(!still, "control: the order check notices one displaced frame");
    }

    /* ── D. looping ──────────────────────────────────────────────────────── */
    printf("\nD. a bed loops, and says how often\n");
    {
        check(audio_wav_open(&w, "/tmp/as_list.wav", true), "reopen with loop");
        long n = audio_wav_read(&w, got, 2500);
        check(n == 2500, "loop: a read longer than the file is satisfied");
        check(w.loops == 2, "loop: wrapped twice for 2.5 passes");
        check(got[0] == 0 && got[1000] == 0 && got[2000] == 0,
              "loop: each pass restarts at the first PCM frame");
        check(got[999] == (int16_t)(999 * 7), "loop: the last frame of a pass is present");
        audio_wav_close(&w);
    }

    /* ── E. stereo downmix ───────────────────────────────────────────────── */
    printf("\nE. a stereo file is averaged, not half-dropped\n");
    {
        /* L = +1000 constant, R = -1000 constant.  Averaging gives 0; taking the
         * left channel gives 1000.  The two answers cannot be confused. */
        for (long i = 0; i < 200; i++) { pcm[i * 2] = 1000; pcm[i * 2 + 1] = -1000; }
        write_fixture("/tmp/as_stereo.wav", pcm, 200, 2, 0);

        check(audio_wav_open(&w, "/tmp/as_stereo.wav", false), "stereo file opens");
        check(w.channels == 2 && w.frames == 200, "stereo: 2 ch, 200 frames");
        long n = audio_wav_read(&w, got, 200);
        check(n == 200, "stereo: all frames read");
        int averaged = 1, left_only = 1;
        for (long i = 0; i < n; i++) {
            if (got[i] != 0)    averaged  = 0;
            if (got[i] != 1000) left_only = 0;
        }
        check(averaged && !left_only, "stereo: (L+R)/2, not the left channel");
        audio_wav_close(&w);
        fill_ramp(pcm, 1000, 7);
    }

    /* ── F. the voice on the bus ─────────────────────────────────────────── */
    printf("\nF. the sample voice renders through the mixer\n");
    {
        AudioMixer m;
        audio_mix_init(&m, RATE);
        check(audio_wav_open(&w, "/tmp/as_list.wav", false), "reopen for the bus");

        int slot = audio_mix_add_sample(&m, audio_wav_fill, &w, vbuf, 2048,
                                        w.frames, AUDIO_PEAK);
        check(slot == 0, "sample voice takes a slot");
        check(audio_mix_active(&m) == 1, "one voice active");
        check(audio_mix_pending(&m) == 1000,
              "pending reports the FILE's length — the render early-out needs it");

        /* Render in two pieces and require the same output as one pass: the
         * incremental-render property `AudioVoice` promises, now over a source
         * that is pulled rather than computed. */
        static int16_t out_a[1200], out_b[1200];
        long a1 = audio_mix_render(&m, out_a, 400);
        long a2 = audio_mix_render(&m, out_a + 400, 600);
        check(a1 == 400 && a2 == 600, "split render produces every frame");
        check(audio_mix_active(&m) == 0, "the voice freed itself at its last frame");
        audio_wav_close(&w);

        audio_mix_init(&m, RATE);
        check(audio_wav_open(&w, "/tmp/as_list.wav", false), "reopen for one-shot");
        audio_mix_add_sample(&m, audio_wav_fill, &w, vbuf, 2048, w.frames, AUDIO_PEAK);
        long b1 = audio_mix_render(&m, out_b, 1000);
        check(b1 == 1000, "single render produces every frame");
        audio_wav_close(&w);

        check(memcmp(out_a, out_b, 1000 * sizeof(int16_t)) == 0,
              "400+600 frames are byte-identical to one 1000-frame render");

        /* And it is really the file, attenuated by `peak`, not silence.  The
         * ramp is monotone under a constant gain, so a rising output is the
         * signature of the PCM arriving in order. */
        check(out_b[900] > out_b[100] && out_b[100] > 0,
              "the rendered bus follows the source ramp");
    }

    /* ── G. the full-bus rule, from the bed's side ───────────────────────── */
    printf("\nG. a full bus refuses a blip and never steals the bed\n");
    {
        AudioMixer m;
        audio_mix_init(&m, RATE);
        check(audio_wav_open(&w, "/tmp/as_list.wav", true), "reopen looping bed");

        int bed = audio_mix_add_sample(&m, audio_wav_fill, &w, vbuf, 2048,
                                       RATE * 60L, AUDIO_PEAK);
        check(bed == 0, "the bed is slot 0");
        uint32_t bed_gen = audio_mix_voice_gen(&m, bed);

        /* Fill the remaining seven slots, then ask for an eighth blip. */
        for (int i = 0; i < AUDIO_MAX_VOICES - 1; i++)
            check(audio_mix_add(&m, 440 + 40 * i, 500, 0, AUDIO_PEAK) == i + 1,
                  "a tone fills a free slot beside the bed");
        check(audio_mix_add(&m, 990, 100, 0, AUDIO_PEAK) == -1,
              "the eighth blip is REFUSED on a full bus");
        check(m.dropped == 1, "and the refusal is counted");
        check(audio_mix_voice_gen(&m, bed) == bed_gen,
              "the bed was NOT stolen — same slot, same generation");

        /* Two voices really do overlap: render a block and require it to differ
         * from the bed alone.  Without this the group proves only bookkeeping. */
        static int16_t mixed[512];
        long n = audio_mix_render(&m, mixed, 512);
        check(n == 512, "the full bus renders");
        audio_wav_close(&w);
    }

    /* ── H. release rather than cut ──────────────────────────────────────── */
    printf("\nH. STOP fades the bed out instead of cutting it\n");
    {
        AudioMixer m;
        audio_mix_init(&m, RATE);

        /* ⚠️ A CONSTANT source, not the ramp.  The claim under test is that the
         * ENVELOPE shapes the tail, so the source must contribute no shape of
         * its own — against the ramp, "no step" was satisfied by the ramp's own
         * gentle slope and a missing envelope went undetected (sabotage case 16,
         * measured 2026-08-20). */
        for (long i = 0; i < 1000; i++) pcm[i] = 8000;
        write_fixture("/tmp/as_flat.wav", pcm, 1000, 1, 0);
        check(audio_wav_open(&w, "/tmp/as_flat.wav", true), "reopen flat source for release");

        int bed = audio_mix_add_sample(&m, audio_wav_fill, &w, vbuf, 2048,
                                       RATE * 60L, AUDIO_PEAK);
        uint32_t gen = audio_mix_voice_gen(&m, bed);
        static int16_t out[8192];
        audio_mix_render(&m, out, 2000);                  /* past the attack */

        check(audio_mix_release_voice(&m, bed, gen), "release arms on the live voice");
        check(!audio_mix_release_voice(&m, bed, gen + 999),
              "control: a stale generation cannot stop somebody else's voice");

        long left = audio_mix_pending(&m);
        check(left > 0 && left <= audio_release_frames(RATE, RATE * 60L) + 1,
              "release shortens the voice to about one release, not to zero");

        /* Render EXACTLY the fade.  ⚠️ Not `left + 8`: `audio_mix_render()`
         * returns `frames` whatever happened, so trailing frames past the
         * voice's end are silence and reading out[n-1] there is 0 for free —
         * which is how the earlier version of this check passed with the
         * envelope deleted. */
        long n = audio_mix_render(&m, out, left);
        check(n == left, "the fade renders every frame it owed");
        check(audio_mix_active(&m) == 0, "the voice freed itself at the ramp's end");
        check(out[left - 1] == 0, "the FADE's last sample is exactly 0");

        /* It starts loud and it only ever gets quieter: that is the envelope,
         * and a flat source cannot produce it by accident. */
        check(out[0] > 2000, "the fade starts near the voice's amplitude");
        int monotone = 1;
        for (long i = 1; i < left; i++)
            if (out[i] > out[i - 1]) { monotone = 0; break; }
        check(monotone, "the fade decreases every frame — it is a ramp, not a cut");

        int worst = 0;
        for (long i = 1; i < left; i++) {
            int d = abs((int)out[i] - (int)out[i - 1]);
            if (d > worst) worst = d;
        }
        check(worst < 400, "the fade has no step — worst adjacent delta is small");
        audio_wav_close(&w);
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
