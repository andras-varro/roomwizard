/*
 * fx_gen — render this repo's stock sound effects to WAV.  HOST ONLY: no
 * device, no cross-compiler, no framebuffer, no listen.
 *
 * ⚠️ **The effects are GENERATED rather than sourced, and the reason is not the
 * obvious one.**  Two sustained sines played together on this speaker
 * intermodulate, harshly, and the cause is ANALOG — below both userspaces, not
 * ours to fix ([§3.4](../../SYSTEM_ANALYSIS.md#34-audio); the hunt is CLOSED).
 * The rule it produced is about CONTENT rather
 * than source: an effect must be BROADBAND and TRANSIENT, whatever it is made
 * of — a WAV of a sine distorts exactly like a generated sine.  A downloaded or
 * AI-generated effect can be a sustained tone that lands straight back in that
 * band, and with **no microphone on this device** each such file would cost an
 * operator listen to rule out.  A generator cannot: the gate below decides
 * whether a file is broadband and transient before it is written, so the
 * property is CHECKED rather than intended.
 *
 * ⚠️ **The gate is the point of this file, not the synthesiser.**  Anyone may
 * re-roll a sound; nobody may ship one that fails fx_check().  `--self-test` is
 * its negative control: three signals that MUST be rejected, each for a
 * different reason, plus one that must pass.  A gate that has only ever been
 * seen passing is not evidence (../../CLAUDE.md → *Working style*).
 *
 * Output is **mono / 44100 / 16-bit**, which is the mix bus's internal format,
 * so nothing at runtime resamples or downmixes — and there is no resampler:
 * audio_music_start() and audio_sfx_play() REFUSE a rate mismatch loudly.
 * Verify a written file with `od -t x1 -N 48 <file>`: channels 0x0001 at byte
 * 22, rate 0x0000ac44 at 24, bits 0x0010 at 34.
 *
 * Samples are normalised to near FULL SCALE on purpose.  The mixer scales a
 * sample voice by `s * peak >> 15` (../common/audio_gen.c), i.e. `peak` is a
 * fraction of full scale, so a quiet file cannot be made loud again — the level
 * belongs to AUDIO_VOICE_VOL, in one place, and not to the asset.
 *
 * This is host code and only host code, so doubles, sin() and integer divide are
 * all free here.  ⚠️ **It must never enter GAMES_BINARIES** — nothing in this
 * file is Cortex-A8 safe by intent and none of it needs to be.
 *
 *   gcc -O2 -Wall -Wextra -o /tmp/fx_gen native_apps/sounds/fx_gen.c -lm
 *   /tmp/fx_gen --self-test          # the gate's controls; writes nothing
 *   /tmp/fx_gen native_apps/sounds   # write (or re-roll) the stock set
 *
 * Authoring rules: ../CLAUDE.md → *Audio*.
 */

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FX_RATE      44100
#define FX_CHANNELS  1
#define FX_BITS      16
#define FX_FULLSCALE 32767

/* The one hard duration bound.  "Transient" is not a feeling: an effect longer
 * than this has a sustained portion by definition, whatever its envelope. */
#define FX_MAX_MS    200

/* Peak the normaliser aims for.  Not 1.0: int16 rounding on the way out can
 * nudge a sample past the edge, and a clipped asset is a permanent defect where
 * a 0.3 dB quieter one is inaudible. */
#define FX_TARGET    0.97

#define FX_MAX_FRAMES ((long)FX_RATE * 4)   /* headroom for the self-test controls */

/* ─────────────────────────────────────────────────────── deterministic noise */

/*
 * xorshift32, seeded per effect.  ⚠️ NOT rand(): these files are COMMITTED, so
 * two runs of this program must produce byte-identical output or every re-roll
 * is an unreviewable diff.  A fixed seed is what makes `git diff` on a .wav mean
 * "somebody changed the recipe".
 */
typedef struct { uint32_t s; } FxRng;

static void rng_seed(FxRng *r, uint32_t seed) { r->s = seed ? seed : 0xa5a5a5a5u; }

static double rng_bipolar(FxRng *r)
{
    r->s ^= r->s << 13;
    r->s ^= r->s >> 17;
    r->s ^= r->s << 5;
    return (double)(int32_t)r->s / 2147483648.0;   /* [-1, 1) */
}

/* ──────────────────────────────────────────────────── state-variable bandpass */

/*
 * Chamberlin SVF, one pole pair, cutoff swept per sample.
 *
 * ⚠️ **q is the knob that can silently break the whole point of this file.**  A
 * high resonance turns a noise burst into a decaying TONE — the exact thing the
 * analog problem punishes — and it does so gradually, so it will not look wrong.
 * The gate is what catches it; keep q near 1 and let fx_check() arbitrate rather
 * than tuning by eye.
 *
 * Stability needs f = 2*sin(pi*fc/rate) < 1, i.e. fc below about rate/6, so the
 * cutoff is clamped to 7000 Hz (2*sin(pi*7000/44100) = 0.94) — well above
 * anything this speaker reproduces usefully anyway.
 */
typedef struct { double low, band; } FxSvf;

static double svf_bandpass(FxSvf *s, double in, double fc, double q, int rate)
{
    double f, high;

    if (fc < 40.0)   fc = 40.0;
    if (fc > 7000.0) fc = 7000.0;
    if (q  < 0.5)    q  = 0.5;

    f    = 2.0 * sin(M_PI * fc / (double)rate);
    high = in - s->low - (1.0 / q) * s->band;
    s->band += f * high;
    s->low  += f * s->band;
    return s->band;
}

/* ─────────────────────────────────────────────────────────── the effect spec */

typedef struct {
    const char *name;        /**< output basename, no extension                */
    const char *what;        /**< the game event, printed in the receipt       */
    int         ms;          /**< total duration; the gate rejects > FX_MAX_MS */
    double      f0, f1;      /**< bandpass centre, start → end (Hz)            */
    double      q;           /**< resonance; see the warning above             */
    double      decay;       /**< e-folds of amplitude over the duration       */
    double      attack_ms;   /**< linear rise; 0 would be a DC step            */
    double      body;        /**< 0..1 blend of a decaying sine at f0          */
    double      body_ms;     /**< how long that sine lasts                     */
    uint32_t    seed;
} FxSpec;

/*
 * Render one spec into `dst`, returning frames.  The signal is
 *
 *     bandpassed white noise * envelope   * (1 - body)
 *   + sine at f0 * its own faster envelope *      body
 *
 * `body` exists because pure noise has no pitch identity and a game wants a
 * brick to sound different from a paddle.  ⚠️ **It is also the one term that can
 * push a file back into the intermodulation band**, so it is deliberately short
 * (body_ms, always well under the total) and fractional: the sine dies while the
 * noise is still going, which is what keeps the spectrum broad where the energy
 * is.  The gate has the final say.
 */
static long fx_render(const FxSpec *sp, double *dst)
{
    FxRng  rng;
    FxSvf  svf = { 0.0, 0.0 };
    long   frames = (long)FX_RATE * sp->ms / 1000;
    long   body_frames = (long)((double)FX_RATE * sp->body_ms / 1000.0);
    long   attack = (long)((double)FX_RATE * sp->attack_ms / 1000.0);
    long   i;
    double phase  = 0.0;
    double dphase = 2.0 * M_PI * sp->f0 / (double)FX_RATE;
    double ratio  = (sp->f0 > 0.0) ? (sp->f1 / sp->f0) : 1.0;

    if (frames > FX_MAX_FRAMES) frames = FX_MAX_FRAMES;
    if (attack < 1) attack = 1;

    rng_seed(&rng, sp->seed);

    for (i = 0; i < frames; i++) {
        double t   = (double)i / (double)frames;          /* 0 → 1 */
        double fc  = sp->f0 * pow(ratio, t);
        double n   = svf_bandpass(&svf, rng_bipolar(&rng), fc, sp->q, FX_RATE);
        double env = exp(-sp->decay * t);
        double s;

        if (i < attack) env *= (double)i / (double)attack;

        s = n * env * (1.0 - sp->body);

        if (sp->body > 0.0 && i < body_frames) {
            double bt = (double)i / (double)body_frames;
            s += sin(phase) * (1.0 - bt) * (1.0 - bt) * sp->body;
            phase += dphase;
        }
        dst[i] = s;
    }

    /* A 2 ms linear out-fade so the file ENDS at zero.  The mixer envelopes a
     * sample voice too, but a released voice is faded over its remaining
     * frames — a file that simply stops mid-swing clicks when it runs dry on its
     * own, which is the normal way an effect ends. */
    {
        long fade = (long)FX_RATE * 2 / 1000;
        if (fade > frames) fade = frames;
        for (i = 0; i < fade; i++)
            dst[frames - 1 - i] *= (double)i / (double)fade;
    }
    return frames;
}

/* Peak-normalise to FX_TARGET.  Returns the pre-normalisation peak, so a spec
 * that renders near-silence (a filter that killed everything) is visible in the
 * receipt rather than being quietly amplified back up to full scale. */
static double fx_normalise(double *buf, long frames)
{
    double peak = 0.0, g;
    long   i;

    for (i = 0; i < frames; i++) {
        double a = fabs(buf[i]);
        if (a > peak) peak = a;
    }
    if (peak <= 1e-9) return peak;

    g = FX_TARGET / peak;
    for (i = 0; i < frames; i++) buf[i] *= g;
    return peak;
}

/* ──────────────────────────────────────────────────────────────── THE GATE */

/*
 * Three numbers, each answering one half of the shipped rule.
 *
 *   tail   RMS of the last quarter over RMS of the first quarter.  TRANSIENT
 *          means the energy is front-loaded; a sustained signal sits near 1.0.
 *   sfm    spectral flatness — geometric mean of the power spectrum over its
 *          arithmetic mean, taken at the loudest part of the file.  A sine is
 *          near 0, white noise near 1.  This is the BROADBAND half, and it is
 *          the one a short percussive SINE would otherwise sneak past.
 *   ms     duration.  Bounded because "transient" cannot be earned by envelope
 *          alone past a couple of hundred milliseconds.
 *
 * ⚠️ **The thresholds below are MEASURED against the four self-test controls,
 * not chosen.**  `--self-test` prints every number, so a threshold moved
 * without re-reading them is a change nobody checked.
 *
 * The sabotage that proves the gate can FAIL, measured 2026-08-20 — run it on a
 * COPY, never the tree:
 *
 *   cp fx_gen.c gen-sounds.sh /tmp/sab/ && cd /tmp/sab
 *   sed -i 's/#define FX_SFM_MIN     0.06/#define FX_SFM_MIN     0.00/' fx_gen.c
 *   ./gen-sounds.sh; echo "exit $?"; ls *.wav 2>/dev/null | wc -l
 *
 * Expected: the percussive-sine control is named WRONG, exit 1, and **0** files
 * written.  ⚠️ **The sustained-sine control still REJECTS under that sabotage**
 * (it fails on tail and duration), so a suite carrying only the obvious control
 * reads green while the gate is broken — which is why the short tonal one is
 * here.  Same shape as `../tests/measure_audio_*_sabotage.sh`.
 */
#define FX_DFT_N       512
#define FX_TAIL_MAX    0.35
#define FX_SFM_MIN     0.06

typedef struct {
    double tail, sfm;
    int    ms;
    int    ok_tail, ok_sfm, ok_ms;
} FxCheck;

static double rms(const double *b, long from, long n)
{
    double acc = 0.0;
    long i;
    if (n <= 0) return 0.0;
    for (i = 0; i < n; i++) acc += b[from + i] * b[from + i];
    return sqrt(acc / (double)n);
}

/*
 * Spectral flatness of one Hann-windowed FX_DFT_N window starting at the file's
 * peak sample.  A naive O(N^2) DFT: 262144 multiply-adds, which is nothing on a
 * host and needs no FFT dependency.
 *
 * ⚠️ **The window starts at the PEAK, not at frame 0.**  An effect with a 2 ms
 * attack has almost no energy in its first window, and a flatness measured on
 * near-silence is dominated by whatever the filter state happened to be — which
 * reads FLAT and would pass a tonal file (an assertion reading past what it
 * measures; ../../CLAUDE.md → *Working style*).
 */
static double spectral_flatness(const double *b, long frames)
{
    static double re[FX_DFT_N / 2 + 1], im[FX_DFT_N / 2 + 1];
    double  win[FX_DFT_N];
    long    peak_i = 0, start;
    double  peak = 0.0, sum = 0.0, logsum = 0.0;
    int     k, bins = 0;
    long    i;

    for (i = 0; i < frames; i++) {
        double a = fabs(b[i]);
        if (a > peak) { peak = a; peak_i = i; }
    }
    start = peak_i;
    if (start + FX_DFT_N > frames) start = frames - FX_DFT_N;
    if (start < 0) return 0.0;                  /* file shorter than a window */

    for (i = 0; i < FX_DFT_N; i++)
        win[i] = b[start + i] * 0.5
               * (1.0 - cos(2.0 * M_PI * (double)i / (double)(FX_DFT_N - 1)));

    for (k = 1; k <= FX_DFT_N / 2 - 1; k++) {   /* skip DC and Nyquist */
        double wr = 0.0, wi = 0.0;
        for (i = 0; i < FX_DFT_N; i++) {
            double ang = 2.0 * M_PI * (double)k * (double)i / (double)FX_DFT_N;
            wr += win[i] * cos(ang);
            wi -= win[i] * sin(ang);
        }
        re[k] = wr; im[k] = wi;
    }

    for (k = 1; k <= FX_DFT_N / 2 - 1; k++) {
        /* A power floor, or one empty bin drives the geometric mean to zero and
         * every signal measures as a pure tone. -100 dB relative to a unit-peak
         * signal is far below anything 16-bit output can carry. */
        double p = re[k] * re[k] + im[k] * im[k];
        if (p < 1e-10) p = 1e-10;
        sum    += p;
        logsum += log(p);
        bins++;
    }
    if (!bins || sum <= 0.0) return 0.0;
    return exp(logsum / (double)bins) / (sum / (double)bins);
}

static FxCheck fx_check(const double *buf, long frames)
{
    FxCheck c;
    long    q = frames / 4;
    double  head = rms(buf, 0, q);
    double  tail = rms(buf, frames - q, q);

    memset(&c, 0, sizeof c);
    c.tail = (head > 1e-9) ? (tail / head) : 1.0;
    c.sfm  = spectral_flatness(buf, frames);
    c.ms   = (int)(frames * 1000 / FX_RATE);

    c.ok_tail = (c.tail <= FX_TAIL_MAX);
    c.ok_sfm  = (c.sfm  >= FX_SFM_MIN);
    c.ok_ms   = (c.ms   <= FX_MAX_MS);
    return c;
}

static int fx_check_pass(const FxCheck *c)
{
    return c->ok_tail && c->ok_sfm && c->ok_ms;
}

static void fx_check_print(const char *label, const FxCheck *c)
{
    printf("  %-26s tail %5.3f %-3s  sfm %5.3f %-3s  %4d ms %-3s  %s\n",
           label,
           c->tail, c->ok_tail ? "ok" : "NO",
           c->sfm,  c->ok_sfm  ? "ok" : "NO",
           c->ms,   c->ok_ms   ? "ok" : "NO",
           fx_check_pass(c) ? "PASS" : "REJECT");
}

/* ─────────────────────────────────────────────────────────────── WAV output */

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

/*
 * A canonical 44-byte PCM header.  ⚠️ 44 is a WRITER's choice and never a
 * READER's assumption: audio_wav.c walks the chunks precisely because our own
 * music files put `data` at byte 164 (../common/audio_wav.h).  Writing the
 * simple layout here does not license reading it anywhere.
 */
static void wav_header(FILE *f, uint32_t data_bytes)
{
    uint32_t byte_rate   = (uint32_t)FX_RATE * FX_CHANNELS * (FX_BITS / 8);
    uint16_t block_align = (uint16_t)(FX_CHANNELS * (FX_BITS / 8));

    fwrite("RIFF", 1, 4, f);
    put_le32(f, 36u + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put_le32(f, 16);                      /* fmt chunk size */
    put_le16(f, 1);                       /* PCM            */
    put_le16(f, FX_CHANNELS);
    put_le32(f, FX_RATE);
    put_le32(f, byte_rate);
    put_le16(f, block_align);
    put_le16(f, FX_BITS);
    fwrite("data", 1, 4, f);
    put_le32(f, data_bytes);
}

static int wav_write(const char *path, const double *buf, long frames)
{
    FILE *f = fopen(path, "wb");
    long  i;

    if (!f) { fprintf(stderr, "fx_gen: cannot write %s\n", path); return -1; }
    wav_header(f, (uint32_t)(frames * (FX_BITS / 8) * FX_CHANNELS));
    for (i = 0; i < frames; i++) {
        double v = buf[i] * (double)FX_FULLSCALE;
        long   s = (long)(v >= 0.0 ? v + 0.5 : v - 0.5);
        if (s >  FX_FULLSCALE) s =  FX_FULLSCALE;
        if (s < -FX_FULLSCALE) s = -FX_FULLSCALE;
        put_le16(f, (uint16_t)(int16_t)s);
    }
    if (ferror(f)) { fclose(f); fprintf(stderr, "fx_gen: short write %s\n", path); return -1; }
    fclose(f);
    return 0;
}

/* ───────────────────────────────────────────────────────── the stock effect set */

/*
 * The set is sized by brick_breaker, because brick_breaker is the game whose
 * in-play sounds are inaudible today: its five audio_tone() calls are 20–40 ms,
 * under the floor a RESTARTED stream imposes, so the operator hears nothing
 * while the ball is moving (confirmed 2026-08-20).  Every other game's events
 * map onto the same ten names.
 *
 * ⚠️ The first four names shadow the canned sounds audio_beep() / audio_blip() /
 * audio_success() / audio_fail() carry today, which is what lets every game
 * upgrade without editing a call site.
 * The frequency beside each is the tone it REPLACES — it is the centre of the
 * noise band now, not a pitch that is sounded.
 */
static const FxSpec STOCK[] = {
 /* name        what                            ms    f0     f1    q  decay atk  body bodyms seed */
  { "click",    "audio_beep: menu tap, hop",     55,  900, 1500, 1.1, 5.0, 1.0, 0.15,  12, 0x1u },
  { "pickup",   "audio_blip: powerup, coin",     75, 1200, 3200, 1.2, 4.0, 1.0, 0.18,  16, 0x2u },
  { "success",  "audio_success: level clear",   190,  700, 4200, 1.1, 2.6, 2.0, 0.12,  30, 0x3u },
  { "fail",     "audio_fail: life lost",        200, 1800,  420, 1.0, 2.4, 2.0, 0.10,  30, 0x4u },
  { "knock",    "bb: ball hits paddle",          45,  900,  260, 0.7, 6.0, 1.0, 0.10,  10, 0x5u },
  { "tick",     "bb: brick destroyed",           35, 2000, 3400, 1.2, 6.5, 0.6, 0.20,   9, 0x6u },
  { "thud",     "bb: brick hit, not destroyed",  28,  800,  600, 1.2, 7.0, 0.6, 0.20,   8, 0x7u },
  { "sparkle",  "bb: bonus brick",               70, 2600, 5200, 1.2, 4.5, 0.6, 0.15,  14, 0x8u },
  { "burst",    "bb: explosive brick",          150,  600, 1400, 0.9, 3.2, 0.6, 0.10,  20, 0x9u },
  { "jump",     "platformer: jump, stomp",       80,  500, 3000, 0.8, 4.0, 1.0, 0.10,  14, 0xau },
};
#define STOCK_N ((int)(sizeof STOCK / sizeof STOCK[0]))

/* ───────────────────────────────────────────────── the gate's own controls */

/*
 * ⚠️ **These exist because a gate that has only ever been seen passing is not
 * evidence.**  Each control is rejected for a DIFFERENT reason, and the middle
 * one is the important one: a short percussive SINE passes the transient test
 * and is exactly the file a human would call "a nice snappy blip" while it lands
 * straight back in the intermodulation band.  If FX_SFM_MIN is ever raised or
 * lowered, it is that control that says whether the new value still works.
 */
typedef enum { CTL_SINE_LONG, CTL_SINE_SHORT, CTL_NOISE_LONG, CTL_NOISE_SHORT } CtlKind;

static long control_render(CtlKind k, double *dst)
{
    FxRng rng;
    long  frames, i;
    double dphase = 2.0 * M_PI * 440.0 / (double)FX_RATE;

    rng_seed(&rng, 0xc0ffeeu);

    switch (k) {
    case CTL_SINE_LONG:                          /* audio_tone(440, 3000) */
        frames = FX_RATE * 3;
        for (i = 0; i < frames; i++) dst[i] = 0.9 * sin(dphase * (double)i);
        return frames;

    case CTL_SINE_SHORT:                         /* the trap: snappy but tonal */
        frames = FX_RATE * 150 / 1000;
        for (i = 0; i < frames; i++)
            dst[i] = 0.9 * sin(dphase * (double)i)
                   * exp(-5.0 * (double)i / (double)frames);
        return frames;

    case CTL_NOISE_LONG:                         /* broadband but sustained */
        frames = FX_RATE * 2;
        for (i = 0; i < frames; i++) dst[i] = 0.9 * rng_bipolar(&rng);
        return frames;

    case CTL_NOISE_SHORT:                        /* the POSITIVE control */
    default:
        frames = FX_RATE * 60 / 1000;
        for (i = 0; i < frames; i++)
            dst[i] = 0.9 * rng_bipolar(&rng)
                   * exp(-5.0 * (double)i / (double)frames);
        return frames;
    }
}

static int self_test(void)
{
    static const struct {
        CtlKind     k;
        const char *label;
        int         want_pass;
        const char *why;
    } CTL[] = {
      { CTL_SINE_LONG,   "sustained 440 Hz sine",  0, "tonal AND sustained AND too long" },
      { CTL_SINE_SHORT,  "percussive 440 Hz sine", 0, "TONAL — the trap this gate exists for" },
      { CTL_NOISE_LONG,  "sustained white noise",  0, "broadband but SUSTAINED and too long" },
      { CTL_NOISE_SHORT, "percussive white noise", 1, "broadband and transient" },
    };
    double *buf = malloc(sizeof(double) * (size_t)FX_MAX_FRAMES);
    int i, bad = 0;

    if (!buf) { fprintf(stderr, "fx_gen: out of memory\n"); return 1; }

    printf("fx_gen --self-test: the gate on four controls\n");
    printf("  thresholds: tail <= %.2f, sfm >= %.2f, ms <= %d\n\n",
           FX_TAIL_MAX, FX_SFM_MIN, FX_MAX_MS);

    for (i = 0; i < (int)(sizeof CTL / sizeof CTL[0]); i++) {
        long    n = control_render(CTL[i].k, buf);
        FxCheck c = fx_check(buf, n);
        int     got = fx_check_pass(&c);

        fx_check_print(CTL[i].label, &c);
        if (got != CTL[i].want_pass) {
            printf("    ^^ WRONG: wanted %s (%s)\n",
                   CTL[i].want_pass ? "PASS" : "REJECT", CTL[i].why);
            bad++;
        } else {
            printf("    %-24s %s\n", CTL[i].want_pass ? "correctly passed" : "correctly rejected",
                   CTL[i].why);
        }
    }
    free(buf);

    if (bad) {
        printf("\nfx_gen --self-test: %d control(s) WRONG — the gate is not trustworthy\n", bad);
        return 1;
    }
    printf("\nfx_gen --self-test: 4/4 controls correct (1 pass, 3 reject)\n");
    return 0;
}

/* ───────────────────────────────────────────────────────────────────── main */

int main(int argc, char **argv)
{
    double *buf;
    char    path[512];
    int     i, bad = 0;
    const char *dir;

    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) return self_test();

    if (argc < 2) {
        fprintf(stderr,
                "usage: fx_gen <outdir>      write the stock set as mono/44100/16-bit WAV\n"
                "       fx_gen --self-test   run the gate against its four controls\n");
        return 2;
    }
    dir = argv[1];

    buf = malloc(sizeof(double) * (size_t)FX_MAX_FRAMES);
    if (!buf) { fprintf(stderr, "fx_gen: out of memory\n"); return 1; }

    printf("fx_gen: %d Hz, %d ch, %d-bit; gate tail <= %.2f, sfm >= %.2f, ms <= %d\n\n",
           FX_RATE, FX_CHANNELS, FX_BITS, FX_TAIL_MAX, FX_SFM_MIN, FX_MAX_MS);

    for (i = 0; i < STOCK_N; i++) {
        long    n    = fx_render(&STOCK[i], buf);
        double  peak = fx_normalise(buf, n);
        FxCheck c    = fx_check(buf, n);

        fx_check_print(STOCK[i].name, &c);
        printf("    %-24s pre-norm peak %.3f  %ld frames\n",
               STOCK[i].what, peak, n);

        /* ⚠️ A rejected spec is NOT written.  The whole argument for generating
         * rather than sourcing is that a file on disk has been checked, so a
         * failing sound must leave the previous (passing) one in place. */
        if (!fx_check_pass(&c)) {
            printf("    ^^ REJECTED, not written — retune the spec, never the threshold\n");
            bad++;
            continue;
        }
        if (peak < 0.01) {
            printf("    ^^ near-silent before normalisation — the filter ate it\n");
            bad++;
            continue;
        }
        snprintf(path, sizeof path, "%s/fx_%s.wav", dir, STOCK[i].name);
        if (wav_write(path, buf, n) != 0) bad++;
    }
    free(buf);

    if (bad) {
        fprintf(stderr, "\nfx_gen: %d of %d effects FAILED\n", bad, STOCK_N);
        return 1;
    }
    printf("\nfx_gen: %d effects written to %s/fx_*.wav\n", STOCK_N, dir);
    return 0;
}
