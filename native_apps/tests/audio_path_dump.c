/*
 * audio_path_dump — the DELIVERED bytes of the real continuous path, on the host
 *
 * F1 phase 8's first step, and the answer to a question three phases of mixer work
 * could not settle: when two voices overlap, the operator hears harsh noise that a
 * LOUDER single voice does not produce.  Measured on RW .188, 2026-08-19:
 *
 *   - lone 440 Hz at LVL 6/6 (acoustic peak 16383): "clearly not a sine, somewhat
 *     distorted" — the amplifier's own ceiling.
 *   - 440 Hz + 880 Hz at LVL 3/6 (acoustic peak 12287, i.e. 25 % LOWER): far worse,
 *     "noise".  440 + CHORD (523/659/784, none a harmonic of 440) is worse still.
 *
 * So the variable is TWO LIVE VOICES, and it is independent of level (the worse case
 * is the quieter one) and of harmonic relationship (inharmonic is no better).  Two
 * candidates survive: intermodulation in the speaker, which no host test can see, and
 * something in the DELIVERY, which no test has ever looked at.
 *
 * ⚠️ **Why "the mixer is exonerated" does not cover this.**  That result came from
 * `tests/audio_dump.c`, which does NOT link `common/audio.c` — it hand-transcribes the
 * chain out of `audio_gen.c` primitives.  What it proved byte-identical ARM-vs-host was
 * the ARITHMETIC.  It never covered the per-service CHUNKING, the bus state carried
 * across chunk boundaries, or the interleave — and a per-voice phase error that
 * re-applies on every chunk would sound like exactly what the operator describes, while
 * hitting the ADDED voice and leaving the RUNNING one intact.  That is the asymmetry
 * first reported: *"distorted 880Hz, 440Hz seems to be not distorted."*
 *
 * This file therefore links the shipped `common/audio.c` and drives
 * `audio_cont_fill_mix()` — the PRODUCTION fill, exported rather than copied, because a
 * re-implemented fill would reproduce `audio_dump.c`'s hole exactly.  The only thing
 * swapped out is the device: a file-backed `AudioOutDev` in place of `/dev/dsp`, so the
 * bytes captured are the bytes `write()` would have been handed.
 *
 * ⚠️ **A new detector has no pre-fix source to fail against**, so group C is not
 * optional: it injects a known discontinuity and requires the detector to fire.  A
 * clean reading from an unvalidated instrument is not a measurement.  Group A is the
 * single-voice control — a detector that false-positives on one clean sine cannot be
 * trusted to acquit two.
 *
 * It also writes a WAV, which is the point for the operator: no microphone exists on
 * this device (nothing here can be recorded acoustically), but this WAV can be played
 * on a PC.  Clean WAV -> the digital chain is innocent and the roughness is the
 * speaker.  Dirty WAV -> the fault is here, host-side, findable in minutes.
 *
 * Build and run (host gcc, from native_apps/):
 *   gcc -Wall -Wextra -Wno-unused-parameter -I. -Itests/hostshim \
 *       -o build/audio_path_dump tests/audio_path_dump.c \
 *       common/audio.c common/audio_gen.c common/audio_out.c common/audio_wav.c common/config.c -lm && \
 *   ./build/audio_path_dump build/
 *
 * ⚠️ `-Itests/hostshim` is host-only.  The cross toolchain has the real
 * `<sys/soundcard.h>`, so leaving it off is what makes an ARM build compile the same
 * headers the shipped build does.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>

#include "common/common.h"
#include "common/audio.h"
#include "common/audio_gen.h"
#include "common/audio_out.h"

static int failures = 0;
static int checks   = 0;

static void check(bool cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("  FAIL: %s\n", what); }
    else       { printf("  ok:   %s\n", what); }
}

#define RATE            44100
#define REQ_CHANNELS    2       /* what the native path asks for and is granted */

/* The device's own geometry, from `/tmp/mix.log` on RW .188 2026-08-19:
 *   lead=6144fr/139ms period=2048fr
 * Modelling the REAL period matters: the whole suspicion is about what happens at a
 * chunk boundary, so a test that used one giant buffer would have no boundaries. */
#define DEV_PERIOD      2048
#define DEV_RING        6144

/* ── The file-backed device ──────────────────────────────────────────────────
 *
 * A virtual DAC with a frame clock.  `wait()` and the driver loop advance it; every
 * `write()` is appended verbatim.  It reports the granted geometry the OMAP codec
 * grants, so `audio_out.c` takes exactly the path it takes on the panel. */
typedef struct {
    int16_t *pcm;          /* captured interleaved frames                       */
    long     frames;       /* frames captured                                   */
    long     cap;          /* capacity in frames                                */
    long     played;       /* frames the virtual DAC has consumed               */
    int      channels;     /* granted                                           */
    int      writes;       /* how many write() calls                            */
    long     short_writes; /* writes the device did not take whole              */
} FileDev;

static int fd_open(void *ctx, int rate_req, int channels_req,
                   int *rate_granted, int *bits_granted, int *channels_granted)
{
    FileDev *d = (FileDev *)ctx;
    d->channels       = channels_req;
    *rate_granted     = RATE;
    *bits_granted     = 16;
    *channels_granted = channels_req;
    return 0;
}

static int fd_space(void *ctx, int frame_bytes, AudioOutSpace *sp)
{
    FileDev *d = (FileDev *)ctx;
    long in_flight = d->frames - d->played;
    if (in_flight < 0) in_flight = 0;

    sp->period_frames = DEV_PERIOD;
    sp->ring_frames   = DEV_RING;
    sp->in_flight     = in_flight;
    sp->space         = DEV_RING - in_flight;
    if (sp->space < 0) sp->space = 0;
    return 0;
}

static ssize_t fd_write(void *ctx, const void *buf, size_t nbytes, bool *again)
{
    FileDev *d = (FileDev *)ctx;
    long frame_bytes = (long)sizeof(int16_t) * d->channels;
    long want        = (long)nbytes / frame_bytes;

    /* Refuse what will not fit, exactly as a full ring does — this is the path
     * that must never steal a voice, and a test that always accepts never sees it. */
    long in_flight = d->frames - d->played;
    long room      = DEV_RING - in_flight;
    if (room <= 0) { if (again) *again = true; return -1; }
    if (want > room) { want = room; d->short_writes++; }

    if (d->frames + want > d->cap) {
        long ncap = (d->cap ? d->cap * 2 : RATE) + want;
        int16_t *np = (int16_t *)realloc(d->pcm,
                          (size_t)ncap * (size_t)d->channels * sizeof(int16_t));
        if (!np) { if (again) *again = false; return -1; }
        d->pcm = np; d->cap = ncap;
    }
    memcpy(d->pcm + d->frames * d->channels, buf,
           (size_t)want * (size_t)frame_bytes);
    d->frames += want;
    d->writes++;
    if (again) *again = false;
    return (ssize_t)(want * frame_bytes);
}

static void fd_wait(void *ctx, int usec)
{
    FileDev *d = (FileDev *)ctx;
    d->played += (long)((double)usec * RATE / 1e6);
}

static void fd_close(void *ctx) { (void)ctx; }

static const AudioOutDev file_dev = {
    fd_open, fd_space, fd_write, fd_wait, fd_close
};

/* ── The signal checks ───────────────────────────────────────────────────────
 *
 * A sum of sines has a bounded sample-to-sample slope: |dx| <= sum over voices of
 * A_k * 2*pi*f_k / rate.  A phase restart at a chunk boundary — the failure mode this
 * file exists to look for — steps by up to 2*A, an order of magnitude more.  So the
 * threshold does not need to be tight to be decisive, and it is derived rather than
 * written down so a volume change cannot silently invalidate it. */
static double slope_bound(const int *freqs, int nfreq, double amp_per_voice)
{
    double b = 0.0;
    for (int i = 0; i < nfreq; i++)
        b += amp_per_voice * 2.0 * M_PI * (double)freqs[i] / (double)RATE;
    return b;
}

/* Largest |x[i+1]-x[i]| over [from,to), skipping `skip` frames either side of each
 * boundary in `bounds` — a tone that starts or ends mid-cycle steps legitimately, and
 * that is envelope behaviour rather than a delivery defect. */
static long max_slope(const int16_t *mono, long from, long to,
                      const long *bounds, int nbounds, long skip, long *at)
{
    long worst = 0;
    if (at) *at = -1;
    for (long i = from; i + 1 < to; i++) {
        bool near = false;
        for (int b = 0; b < nbounds; b++)
            if (i >= bounds[b] - skip && i <= bounds[b] + skip) { near = true; break; }
        if (near) continue;
        long d = (long)mono[i + 1] - (long)mono[i];
        if (d < 0) d = -d;
        if (d > worst) { worst = d; if (at) *at = i; }
    }
    return worst;
}

static void write_wav(const char *path, const int16_t *mono, long frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  (could not write %s)\n", path); return; }

    uint32_t data_bytes = (uint32_t)(frames * 2);
    uint32_t riff       = 36 + data_bytes;
    uint32_t byte_rate  = RATE * 2;
    uint16_t one = 1, bits = 16, align = 2;
    uint32_t fmt_len = 16, rate = RATE;

    /* A canonical 44-byte header, which is what a WRITER may assume.  ⚠️ A READER
     * may not: `/opt/sound/officerunner1-mono.wav` has `data` at byte 164 because ffmpeg
     * wrote a LIST/INFO chunk, while the vendor effects have it at 36.  The sample
     * voice must walk the chunks — see F1 phase 8. */
    fwrite("RIFF", 1, 4, f);      fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);  fwrite(&fmt_len, 4, 1, f);
    fwrite(&one, 2, 1, f);        fwrite(&one, 2, 1, f);
    fwrite(&rate, 4, 1, f);       fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f);      fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);      fwrite(&data_bytes, 4, 1, f);
    fwrite(mono, 2, (size_t)frames, f);
    fclose(f);
    printf("  wrote %s  (%ld frames, %.2f s)\n", path, frames,
           (double)frames / RATE);
}

/* ── The run ─────────────────────────────────────────────────────────────────── */

typedef struct {
    FileDev dev;
    Audio   a;
} Rig;

/* An Audio on the continuous stream with no /dev/dsp behind it.  `dsp_fd` is -1
 * because CONT owns the device — audio_live() reads audio_out_is_open() in that
 * state, and our AudioOut really is open. */
static int rig_start(Rig *r, int vol)
{
    memset(r, 0, sizeof(*r));

    if (audio_out_open(&r->a.out, &file_dev, &r->dev, RATE, REQ_CHANNELS) != 0) {
        printf("  FAIL: audio_out_open on the file device\n");
        failures++;
        return -1;
    }

    r->a.dsp_fd       = -1;
    r->a.available    = true;
    r->a.cont         = true;
    r->a.pumping      = true;
    r->a.sample_rate  = audio_out_rate(&r->a.out);
    r->a.channels     = audio_out_channels(&r->a.out);
    r->a.vol          = vol;
    r->a.master_shift = AUDIO_MASTER_SHIFT;
    r->a.last_tone_slot = -1;
    r->a.last_tone_gen  = 0;

    audio_mix_init(&r->a.mix, r->a.sample_rate);
    audio_mix_set_limit(&r->a.mix, AUDIO_MIX_HARD);      /* the shipped default */
    audio_mix_set_knee(&r->a.mix, audio_voice_peak(vol));

    audio_out_set_shift(&r->a.out, r->a.master_shift);
    audio_out_set_fill(&r->a.out, audio_cont_fill_mix, &r->a, "mix bus");
    return 0;
}

/* Service the stream for `ms`, at the render loop's own cadence, and let the virtual
 * DAC drain at real time — which is what makes the chunk boundaries land where they
 * land on the panel. */
static void rig_run(Rig *r, long ms)
{
    long steps = ms * 1000 / FRAME_DELAY_ACTIVE_US;
    for (long i = 0; i < steps; i++) {
        audio_out_service(&r->a.out);
        r->dev.played += (long)((double)FRAME_DELAY_ACTIVE_US * RATE / 1e6);
    }
}

/* Channel 0 of the captured interleaved stream. */
static int16_t *demux(const FileDev *d, long *n)
{
    int16_t *mono = (int16_t *)malloc((size_t)d->frames * sizeof(int16_t));
    if (!mono) { *n = 0; return NULL; }
    for (long i = 0; i < d->frames; i++) mono[i] = d->pcm[i * d->channels];
    *n = d->frames;
    return mono;
}

static bool channels_identical(const FileDev *d)
{
    if (d->channels < 2) return true;
    for (long i = 0; i < d->frames; i++)
        for (int c = 1; c < d->channels; c++)
            if (d->pcm[i * d->channels + c] != d->pcm[i * d->channels]) return false;
    return true;
}

static void report(const Rig *r, const char *label)
{
    printf("     %s: writes=%d short=%ld frames=%ld clip=%lu lim=%lu "
           "starve=%lu lost=%lu misalign=%lu refused=%lu\n",
           label, r->dev.writes, r->dev.short_writes, r->dev.frames,
           (unsigned long)audio_pump_clipped(&r->a),
           (unsigned long)audio_pump_limited(&r->a),
           (unsigned long)audio_out_starved(&r->a.out),
           (unsigned long)audio_out_lost(&r->a.out),
           (unsigned long)audio_out_misaligned(&r->a.out),
           (unsigned long)audio_out_refused(&r->a.out));
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "";
    char path[512];
    /* LVL 3/6 — the rung the level walk settled on and the one the operator heard
     * the noise at.  Named as the shipped constant so a change to the default
     * follows this test rather than silently invalidating it. */
    const int vol = AUDIO_VOICE_VOL;

    printf("audio_path_dump — the delivered bytes of the continuous path\n");
    printf("  vol=%d voice peak=%d shift=%d period=%d ring=%d\n\n",
           vol, audio_voice_peak(vol), AUDIO_MASTER_SHIFT, DEV_PERIOD, DEV_RING);

    /* Post-shift amplitude of ONE voice, which is what both slope bounds derive
     * from.  The fill sums voices and audio_out shifts the sum. */
    double amp1 = (double)audio_voice_peak(vol) / (double)(1 << AUDIO_MASTER_SHIFT);

    /* ── A. one voice: the detector's control ─────────────────────────────── */
    printf("A. one voice, 440 Hz 3 s — the detector must NOT fire on a clean sine\n");
    {
        Rig r;
        if (rig_start(&r, vol) == 0) {
            audio_tone(&r.a, 440, 3000);
            rig_run(&r, 3400);

            long n; int16_t *mono = demux(&r.dev, &n);
            report(&r, "A");
            check(n > RATE * 2, "A delivered more than 2 s of audio");
            check(channels_identical(&r.dev), "A both channels identical");

            if (mono && n > 0) {
                int    f1[1]     = { 440 };
                double bound     = slope_bound(f1, 1, amp1);
                long   bounds[2] = { 0, (long)(3.0 * RATE) };
                long   at        = -1;
                long   worst     = max_slope(mono, 0, n, bounds, 2, RATE / 500, &at);
                printf("     A max slope %ld, analytic bound %.0f (x2 = %.0f)\n",
                       worst, bound, bound * 2);
                check((double)worst <= bound * 2.0,
                      "A one voice has no discontinuity");
                snprintf(path, sizeof(path), "%s01-lone-440.wav", dir);
                write_wav(path, mono, n);
            }
            free(mono);
            audio_out_close(&r.a.out);
            free(r.dev.pcm);
        }
    }

    /* ── B. two voices: the case the operator hears as noise ──────────────── */
    printf("\nB. 440 Hz 3 s with 880 Hz 200 ms tapped over it — THE CASE\n");
    {
        Rig r;
        if (rig_start(&r, vol) == 0) {
            audio_tone(&r.a, 440, 3000);
            rig_run(&r, 1000);

            /* ⚠️ A REAL sleep, and it is load-bearing.  `rig_run()` advances a
             * VIRTUAL DAC clock, but audio_tone()'s chaining guard reads the wall
             * clock (`audio_ms_from_timeval()`), and this loop runs in
             * microseconds — so without this the tap looks like the second note of
             * a motif, chains onto the drone's tail, and plays at 3.0 s.  Measured:
             * it did exactly that, and group B passed with the two voices never
             * once overlapping.  50 ms clears AUDIO_TONE_CHAIN_MS with margin
             * without naming it (see audio_tone_test.c on why not to name it). */
            usleep(50000);

            long tap_at = r.dev.frames;       /* where the tap really lands */
            audio_tone(&r.a, 880, 200);
            int voices_after = audio_pump_voices(&r.a);
            rig_run(&r, 2400);

            long n; int16_t *mono = demux(&r.dev, &n);
            report(&r, "B");
            check(channels_identical(&r.dev), "B both channels identical");

            /* ⚠️ THE control for this group: two voices must actually be live.  A
             * chained tap, a stolen voice or a refused add all leave one, and every
             * signal check below would then pass while measuring nothing. */
            printf("     B voices live after the tap: %d, tap at frame %ld (%.3f s)\n",
                   voices_after, tap_at, (double)tap_at / RATE);
            check(voices_after == 2, "B the tap really overlaps the drone (2 voices)");

            if (mono && n > 0) {
                int    f1[1]  = { 440 };
                int    f2[2]  = { 440, 880 };
                double bound1 = slope_bound(f1, 1, amp1);
                double bound2 = slope_bound(f2, 2, amp1);

                /* The overlap window only: from the tap to the end of its 200 ms,
                 * skipping 1 ms either side of the onset and the release. */
                long w0 = tap_at, w1 = tap_at + RATE / 5;
                if (w1 > n) w1 = n;
                long bounds[2] = { w0, w1 };
                long at    = -1;
                long worst = max_slope(mono, w0, w1, bounds, 2, RATE / 1000, &at);

                printf("     B overlap window [%.3f..%.3f s] max slope %ld; "
                       "one-voice bound %.0f, two-voice bound %.0f (x2 = %.0f)"
                       "  worst at %.3f s\n",
                       (double)w0 / RATE, (double)w1 / RATE, worst,
                       bound1, bound2, bound2 * 2, (double)at / RATE);

                /* Above the ONE-voice bound is what proves the sum really contains
                 * both voices; below twice the TWO-voice bound is what says the sum
                 * is continuous.  Either alone can be satisfied by an artefact. */
                check((double)worst > bound1,
                      "B the window really holds two summed voices");
                check((double)worst <= bound2 * 2.0,
                      "B two voices have no discontinuity");
                check(audio_pump_clipped(&r.a) == 0,
                      "B nothing clipped (2 x 96 <= 256, so none may)");

                snprintf(path, sizeof(path), "%s02-440-plus-880.wav", dir);
                write_wav(path, mono, n);
            }
            free(mono);
            audio_out_close(&r.a.out);
            free(r.dev.pcm);
        }
    }

    /* ── C. the instrument's negative control ────────────────────────────── */
    printf("\nC. the detector itself — a known discontinuity MUST be caught\n");
    {
        long  n     = RATE;
        int16_t *s  = (int16_t *)malloc((size_t)n * sizeof(int16_t));
        if (s) {
            for (long i = 0; i < n; i++)
                s[i] = (int16_t)(amp1 * sin(2.0 * M_PI * 440.0 * i / RATE));

            int    f1[1] = { 440 };
            double bound = slope_bound(f1, 1, amp1);
            long   none[1] = { -1 };
            long   clean = max_slope(s, 0, n, none, 0, 0, NULL);
            check((double)clean <= bound * 2.0,
                  "C the synthetic clean sine passes");

            /* One sample displaced — the smallest thing a phase restart could do. */
            s[n / 2] = (int16_t)(s[n / 2] + 20000);
            long at = -1;
            long dirty = max_slope(s, 0, n, none, 0, 0, &at);
            printf("     C clean %ld, sabotaged %ld at frame %ld, bound %.0f\n",
                   clean, dirty, at, bound);
            check((double)dirty > bound * 2.0,
                  "C the sabotaged sine is CAUGHT (detector can fire)");
            free(s);
        }
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    if (!failures)
        printf("Play 01-lone-440.wav and 02-440-plus-880.wav on a PC: if 02 is "
               "clean there, the roughness is the speaker.\n");
    return failures ? 1 : 0;
}
