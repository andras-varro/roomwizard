/*
 * audio_gen_test.c — the audio logic, on the host, with no device in it.
 *
 * F1 Phase 2 splits `common/audio.c` into a pure generator (`audio_gen.c`) and
 * the device half that stays behind.  Before this file existed, no audio logic
 * was host-testable at all: every buffer-fill was welded to its write().
 *
 * GROUP A IS THE NEGATIVE CONTROL and it drives the OLD idioms, transcribed
 * verbatim from the shipped file (`audio.c` as of 2026-08-15, 502 lines).  Two
 * of the four defects an earlier draft of the plan promised are NOT IN THE
 * SOURCE — `sound_end_ms` is already N for an N-ms tone, and `time_now_ms()`
 * does not overflow — so they are pinned in group F as measured-absent rather
 * than asserted as bugs.  What group A does reproduce:
 *
 *   A1  a short write abandons the stream MID-FRAME.  The generator is mono and
 *       the device is interleaved stereo, so 6 bytes of a 4-byte-frame stream
 *       leaves L and R swapped for every sample after it — permanently, with no
 *       mono path underneath to absorb the half frame (hw:0,0 is stereo-only,
 *       measured).  Three loops have this shape: audio.c:239-248, :374-388,
 *       :430-451.
 *   A2  `(long)sample_rate * duration_ms` is a 32-BIT multiply on armhf.
 *       ⚠️ This host has sizeof(long) == 8, so the overflow CANNOT be observed
 *       here — it is MODELLED by truncating to int32, which is what the target
 *       does.  A test that just wrote the expression would pass and prove
 *       nothing.
 *   A3  the byte count is `frames * 4` with the 4 spelled out: the channel count
 *       is a literal that no code reads back from the device.  Phase 0 measured
 *       hw:0,0 granting exactly 2, so today the arithmetic is ACCIDENTALLY
 *       right — and cannot express a device that grants anything else.
 *   A4  the freq/ramp/phase generator is duplicated as 2 + 1: the stream prefill
 *       (:287-299) and the stream chunk (:350-368) are identical, and the
 *       fade-out (:410-419) is a different generator.  Group C pins the new
 *       oscillator against all three, because collapsing them must keep the
 *       fade REACHABLE, not delete it.
 *   A5  the shipped path CANNOT HOLD TWO SOUNDS.  `audio_beep()` is
 *       `audio_flush(); audio_tone();` and the flush issues SNDCTL_DSP_RESET, so
 *       a beep during a drone leaves the beep alone — zero frames of the drone
 *       survive.  Not a bug; the shape of the code, and what Phase 3's mix bus
 *       has to beat.
 *   A6  three notes queued with no start offset are a CHORD, not an arpeggio.
 *       `audio_success()` is three notes at 45 call sites, so the voice `delay`
 *       is what keeps them sounding the way the panel already hears them.
 *
 * Groups I/J/K are Phase 3's mix bus: the sum and its single clamp (including
 * that SLOT ORDER cannot change the mix), voice lifetime (frees on its last
 * sample, silence past its end, delays, a full bus that refuses rather than
 * steals) and the pump's pacing — which targets a LEAD and must never simply
 * write the ~506 ms an empty OSS ring would accept.
 *
 * Measured against seven sabotaged copies of `audio_gen.c` (2026-08-15), all
 * seven caught: int16 accumulator **5** · voice freed a frame late **3** · stale
 * scratch past a voice end **4** · delay ignored **3** · pump fills the space
 * instead of a lead **5** · full bus steals slot 0 **3** · positive clamp dropped
 * **1**.  ⚠️ Two of those numbers were earned the hard way: with
 * AUDIO_PUMP_CAP_MS equal to AUDIO_PUMP_LEAD_MS the "empty ring" check passes
 * against a space-filling pump *because the cap catches it by accident*, so K
 * also asks with the cap out of the way; and the first clamp check read
 * `peak_abs(...) == 32767`, which is false for a correct clamp because
 * `abs(-32768)` is 32768.
 *
 * Build (host gcc, from native_apps/):
 *   gcc -Wall -Wextra -Wno-unused-parameter -I common -o build/audio_gen_test \
 *       tests/audio_gen_test.c common/audio_gen.c -lm && \
 *   ./build/audio_gen_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "../common/audio_gen.h"

static int failures = 0;
static int checks   = 0;

static void check(bool cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("  FAIL: %s\n", what); }
    else       { printf("  ok:   %s\n", what); }
}

/* ── A sink whose behaviour the test dictates ───────────────────────────── */

typedef struct {
    uint8_t  got[65536];
    long     len;         /* bytes accepted so far                            */
    long     accept_upto; /* accept until this many bytes, then stall (-1 = all) */
    int      chunk;       /* max bytes accepted per call (0 = whatever is asked) */
    int      stalls;      /* remaining stalls before accepting again           */
    bool     hard_error;  /* stall as a real error rather than EAGAIN          */
    int      calls;
    int      waits;
} Sink;

static ssize_t sink_write(void *ctx, const void *buf, size_t n, bool *again)
{
    Sink *s = (Sink *)ctx;
    s->calls++;

    bool stalled = (s->accept_upto >= 0 && s->len >= s->accept_upto);
    if (stalled && s->stalls != 0) {
        if (s->stalls > 0) s->stalls--;
        if (s->hard_error) { *again = false; return -1; }
        *again = true;
        return -1;
    }
    if (stalled && s->stalls == 0 && s->accept_upto >= 0) {
        /* stalls exhausted: the sink recovers and accepts from here on */
        s->accept_upto = -1;
    }

    size_t take = n;
    if (s->chunk > 0 && take > (size_t)s->chunk) take = (size_t)s->chunk;
    if (s->accept_upto >= 0 && s->len + (long)take > s->accept_upto)
        take = (size_t)(s->accept_upto - s->len);
    if (take == 0) { *again = true; return -1; }

    memcpy(s->got + s->len, buf, take);
    s->len += (long)take;
    return (ssize_t)take;
}

static void sink_wait(void *ctx, int usec) { (void)usec; ((Sink *)ctx)->waits++; }

static void sink_reset(Sink *s)
{
    memset(s, 0, sizeof(*s));
    s->accept_upto = -1;
}

static AudioSink sink_of(Sink *s)
{
    AudioSink as;
    as.write = sink_write;
    as.wait  = sink_wait;
    as.ctx   = s;
    return as;
}

/* ── Group A: the shipped idioms, transcribed ───────────────────────────── */

/* audio.c:374-388 — the streaming chunk write loop, verbatim in shape.
 * Returns bytes handed to the device. */
static long old_write_loop(Sink *s, const uint8_t *buf, long total)
{
    long written = 0;
    while (written < total) {
        bool again = false;
        ssize_t r = sink_write(s, buf + written, (size_t)(total - written), &again);
        if (r > 0) {
            written += (long)r;
        } else if (r < 0 && again) {
            break;    /* "shouldn't happen since we checked space, but be safe" */
        } else {
            break;    /* error */
        }
    }
    return written;
}

/* audio.c:203 as the TARGET evaluates it: `long` is 32-bit on armhf, so the
 * product wraps before the divide.  Modelled, not observed — see the header. */
static int32_t old_frames_for_ms(int rate, int duration_ms)
{
    int64_t exact   = (int64_t)rate * (int64_t)duration_ms;
    int32_t wrapped = (int32_t)(uint32_t)(uint64_t)exact;   /* 32-bit truncation */
    return wrapped / 1000;
}

/* audio.c:204/:237/:339/:371 — the byte count, with the channel count spelled
 * into the constant.  No argument, so no device can change it. */
static long old_bytes_for_frames(long frames) { return frames * 4; }

/* audio.c:350-368 — the streaming chunk generator. */
static void old_chunk_gen(double *phase, double *freq, double target,
                          double *amp, int rate, int16_t *mono, long frames)
{
    for (long i = 0; i < frames; i++) {
        *freq += (target - *freq) * 0.15;
        if (*amp < 1.0) { *amp += 0.0005; if (*amp > 1.0) *amp = 1.0; }
        double phase_step = 2.0 * M_PI * *freq / rate;
        mono[i] = (int16_t)(18000 * *amp * sin(*phase));
        *phase += phase_step;
        if (*phase > 2.0 * M_PI) *phase -= 2.0 * M_PI;
    }
}

/* audio.c:287-299 — the streaming PREFILL generator.  Identical body. */
static void old_prefill_gen(double *phase, double *freq, double target,
                            double *amp, int rate, int16_t *mono, long frames)
{
    for (long i = 0; i < frames; i++) {
        *freq += (target - *freq) * 0.15;
        if (*amp < 1.0) { *amp += 0.0005; if (*amp > 1.0) *amp = 1.0; }
        double phase_step = 2.0 * M_PI * *freq / rate;
        mono[i] = (int16_t)(18000 * *amp * sin(*phase));
        *phase += phase_step;
        if (*phase > 2.0 * M_PI) *phase -= 2.0 * M_PI;
    }
}

/* audio.c:410-419 — the fade-out generator.  A DIFFERENT one: fade envelope,
 * no frequency smoothing, no amplitude ramp. */
static void old_fade_gen(double *phase, double freq, double amp,
                         int rate, int16_t *mono, long frames)
{
    for (long i = 0; i < frames; i++) {
        double env = 1.0 - (double)i / frames;
        double phase_step = 2.0 * M_PI * freq / rate;
        mono[i] = (int16_t)(18000 * env * amp * sin(*phase));
        *phase += phase_step;
        if (*phase > 2.0 * M_PI) *phase -= 2.0 * M_PI;
    }
}

/* audio.c:216-229 — the tone generator. */
static void old_tone_gen(int rate, int freq_hz, int16_t *mono, long frames)
{
    long attack  = rate * 10 / 1000;
    long release = rate * 20 / 1000;
    if (attack  > frames / 2) attack  = frames / 2;
    if (release > frames / 2) release = frames / 2;

    double phase_step = 2.0 * M_PI * freq_hz / rate;
    double phase      = 0.0;
    for (long i = 0; i < frames; i++) {
        double env = 1.0;
        if (i < attack)                  env = (double)i / attack;
        else if (i >= frames - release)  env = (double)(frames - 1 - i) / release;
        mono[i] = (int16_t)(18000 * env * sin(phase));
        phase += phase_step;
        if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
    }
}

/* audio.c pre-pump, as a whole: ONE SOUND AT A TIME, by construction.
 *
 * `audio_beep()` is `audio_flush(); audio_tone();` and `audio_flush()` issues
 * SNDCTL_DSP_RESET, which DISCARDS whatever is still queued.  A tone is written
 * whole in one call, so there is no state in which two sounds coexist — not as a
 * bug, as the shape of the code.  That is what F1 Phase 3 exists to change, and
 * this is the model it has to beat.
 */
#define OLD_RING_CAP 65536
typedef struct { int16_t buf[OLD_RING_CAP]; long len; } OldRing;

static void old_ring_reset(OldRing *r) { r->len = 0; }      /* SNDCTL_DSP_RESET */

static void old_ring_tone(OldRing *r, int rate, int freq, long frames)
{
    if (frames > OLD_RING_CAP - r->len) frames = OLD_RING_CAP - r->len;
    if (frames <= 0) return;
    old_tone_gen(rate, freq, r->buf + r->len, frames);      /* written whole */
    r->len += frames;
}

/* Any non-zero sample in [from, to) — "is this sound in there at all". */
static bool has_signal(const int16_t *b, long from, long to)
{
    for (long i = from; i < to; i++) if (b[i] != 0) return true;
    return false;
}

static int peak_abs(const int16_t *b, long n)
{
    int p = 0;
    for (long i = 0; i < n; i++) if (abs(b[i]) > p) p = abs(b[i]);
    return p;
}

/* ── main ───────────────────────────────────────────────────────────────── */

#define RATE 44100
#define CH   2
#define FB   (CH * AUDIO_BYTES_PER_SAMPLE)

int main(void)
{
    static int16_t a[16384], b[16384], mono[16384], inter[32768];
    static OldRing ring;
    Sink s;

    printf("\n=== A. the OLD idioms, transcribed (the defects must reproduce) ===\n");
    {
        /* A1: a sink that takes 1.5 frames and then stalls forever. */
        uint8_t src[64];
        for (int i = 0; i < 64; i++) src[i] = (uint8_t)i;

        sink_reset(&s); s.accept_upto = 6; s.stalls = -1;   /* -1 = stall forever */
        long w = old_write_loop(&s, src, 8 * FB);
        check(w == 6, "old loop hands the device 6 of 32 bytes");
        check((w % FB) != 0,
              "and STOPS MID-FRAME — L/R swapped from here on, reported as nothing");

        sink_reset(&s); s.accept_upto = 6; s.stalls = -1; s.hard_error = true;
        w = old_write_loop(&s, src, 8 * FB);
        check((w % FB) != 0, "same on a hard error, not just on a full ring");

        /* A2: the 32-bit multiply. */
        int64_t exact = (int64_t)RATE * 49000 / 1000;
        check(old_frames_for_ms(RATE, 49000) != (int32_t)exact,
              "old frames expression is wrong at 49 s (32-bit product wraps)");
        check(old_frames_for_ms(RATE, 49000) < 0,
              "and NEGATIVE, so the malloc/loop that follows it is undefined");
        check(old_frames_for_ms(RATE, 300) == 13230,
              "no caller reaches it today — 300 ms is exact");

        /* A3: the channel count is not an argument anywhere. */
        check(old_bytes_for_frames(100) == audio_bytes_for_frames(100, 2),
              "old byte count agrees with the device's 2 channels — accidentally");
        check(old_bytes_for_frames(100) != audio_bytes_for_frames(100, 1),
              "and cannot express any other channel count: the 4 is a literal");

        /* A4: 2 + 1, not 3. */
        double p1 = 0, f1 = 300, m1 = 0, p2 = 0, f2 = 300, m2 = 0;
        old_chunk_gen(&p1, &f1, 900, &m1, RATE, a, 512);
        old_prefill_gen(&p2, &f2, 900, &m2, RATE, b, 512);
        check(memcmp(a, b, 512 * sizeof(int16_t)) == 0,
              "prefill and chunk generators are byte-identical — a true duplicate");
        p2 = 0; f2 = 300; m2 = 1.0;
        old_fade_gen(&p2, f2, m2, RATE, b, 512);
        check(memcmp(a, b, 512 * sizeof(int16_t)) != 0,
              "the fade generator is a DIFFERENT one — collapsing must keep it");

        /* A5: the shipped path cannot hold two sounds AT ALL.  A 200 ms drone is
         * queued, then a beep arrives — audio_flush()'s RESET throws the drone
         * away, and what the DAC gets is the beep alone. */
        memset(&ring, 0, sizeof(ring));
        long drone_frames = audio_frames_for_ms(RATE, 200);
        long beep_frames  = audio_frames_for_ms(RATE, 80);
        old_ring_tone(&ring, RATE, 220, drone_frames);
        check(ring.len == drone_frames, "drone queued: 200 ms in the ring");
        old_ring_reset(&ring);                       /* what audio_beep() does first */
        old_ring_tone(&ring, RATE, 880, beep_frames);
        check(ring.len == beep_frames,
              "a beep during the drone leaves the BEEP ALONE — 0 frames of drone survive");

        /* A6: three notes queued at once, with no per-voice start offset, are a
         * CHORD.  audio_success() is an arpeggio, so the delay field is what
         * keeps 45 call sites sounding the way they already do. */
        AudioMixer chord; audio_mix_init(&chord, RATE);
        audio_mix_add(&chord, 523, 120, 0, AUDIO_PEAK);   /* no delays: all at once */
        audio_mix_add(&chord, 659, 120, 0, AUDIO_PEAK);
        audio_mix_add(&chord, 784, 220, 0, AUDIO_PEAK);
        audio_mix_render(&chord, a, 2048);
        check(chord.limited > 0,
              "three notes with no start offsets exceed one voice's peak — they "
              "are sounding together");

        AudioMixer arp; audio_mix_init(&arp, RATE);
        audio_mix_add(&arp, 523, 120,   0, AUDIO_PEAK);
        audio_mix_add(&arp, 659, 120, 120, AUDIO_PEAK);
        audio_mix_add(&arp, 784, 220, 240, AUDIO_PEAK);
        audio_mix_render(&arp, b, 2048);
        check(arp.limited == 0 && arp.clipped == 0 &&
              peak_abs(b, 2048) <= AUDIO_PEAK,
              "the same three with offsets are an ARPEGGIO — one voice at a time");
    }

    printf("\n=== B. frame and byte arithmetic (channels is an ARGUMENT) ===\n");
    {
        check(audio_frame_bytes(2) == 4 && audio_frame_bytes(1) == 2,
              "frame bytes track the channel count");
        check(audio_frame_bytes(0) == 0 && audio_frame_bytes(-1) == 0,
              "a nonsense channel count yields 0, not a negative stride");
        check(audio_bytes_for_frames(100, 2) == 400 &&
              audio_bytes_for_frames(100, 1) == 200,
              "byte count follows the channel count the device granted");
        check(audio_frames_for_ms(RATE, 300) == 13230, "300 ms at 44100 is exact");
        check(audio_frames_for_ms(48000, 10) == 480, "10 ms at 48000 is exact");
        check(audio_frames_for_ms(RATE, 49000) ==
              (long)((int64_t)RATE * AUDIO_MAX_TONE_MS / 1000),
              "a 49 s request clamps to AUDIO_MAX_TONE_MS instead of overflowing");
        check(audio_frames_for_ms(RATE, 49000) > 0, "and stays positive");
        check(audio_frames_for_ms(0, 100) == 0 && audio_frames_for_ms(RATE, 0) == 0 &&
              audio_frames_for_ms(RATE, -5) == 0,
              "junk in gives 0 frames, not a huge allocation");

        /* The inverse, which exists so a diagnostic can report the lead the pump
         * TOOK rather than the constant it asked for. */
        check(audio_ms_for_frames(RATE, 13230) == 300, "13230 frames is 300 ms back");
        check(audio_ms_for_frames(RATE, audio_frames_for_ms(RATE, 250)) == 250,
              "a round trip through both is exact at 250 ms");
        /* ⚠️ The negative case must be BIG enough to discriminate.  `-5` frames
         * divides to 0 with or without the guard, so a check written that way
         * passes against an implementation that has no guard at all — measured, by
         * deleting it.  One second of negative frames does not. */
        check(audio_ms_for_frames(0, 4410) == 0 && audio_ms_for_frames(RATE, 0) == 0 &&
              audio_ms_for_frames(RATE, -5) == 0 &&
              audio_ms_for_frames(RATE, -RATE) == 0,
              "junk in gives 0 ms, not a division by zero or a negative duration");
        /* ⚠️ It must NOT clamp the way its inverse does: the inverse clamps a
         * caller's *request*, this reports a *measurement*, and a clamp here would
         * hide the surprise worth reporting. */
        check(audio_ms_for_frames(RATE, (long)RATE * 60) == 60000 &&
              60000 > AUDIO_MAX_TONE_MS,
              "a frame count past AUDIO_MAX_TONE_MS reports its real duration");
        /* And the number the panel was getting wrong: the effective lead on the
         * measured device is ~139 ms, not the 80 ms AUDIO_PUMP_LEAD_MS asks for. */
        check(audio_ms_for_frames(RATE, 3 * 2048) == 139 &&
              audio_ms_for_frames(RATE, 3 * 2048) != AUDIO_PUMP_LEAD_MS,
              "3 periods of 2048 read back as 139 ms, which the constant is not");
    }

    printf("\n=== C. the ONE oscillator reproduces all three shipped loops ===\n");
    {
        /* GLIDE == the prefill and chunk generators, sample for sample. */
        double p = 0, f = 300, m = 0;
        old_chunk_gen(&p, &f, 900, &m, RATE, a, 1024);

        AudioOsc o; audio_osc_init(&o, RATE, 300, AUDIO_PEAK);
        o.target_freq = 900;
        audio_osc_render(&o, AUDIO_OSC_GLIDE, b, 1024);
        check(memcmp(a, b, 1024 * sizeof(int16_t)) == 0,
              "GLIDE is byte-identical to the shipped stream generator");

        /* Split calls == one long call: a chunk boundary must not seam. */
        AudioOsc o2; audio_osc_init(&o2, RATE, 300, AUDIO_PEAK);
        o2.target_freq = 900;
        audio_osc_render(&o2, AUDIO_OSC_GLIDE, mono, 441);
        audio_osc_render(&o2, AUDIO_OSC_GLIDE, mono + 441, 583);
        check(memcmp(a, mono, 1024 * sizeof(int16_t)) == 0,
              "441 + 583 frames equal one 1024-frame call — no seam at a chunk edge");

        /* FADE_OUT == the shipped fade loop, from the same state. */
        double pf = 1.234, ff = 512.0, mf = 0.8;
        old_fade_gen(&pf, ff, mf, RATE, a, 882);
        AudioOsc o3; audio_osc_init(&o3, RATE, ff, AUDIO_PEAK);
        o3.phase = 1.234; o3.amp = 0.8;
        audio_osc_render(&o3, AUDIO_OSC_FADE_OUT, b, 882);
        check(memcmp(a, b, 882 * sizeof(int16_t)) == 0,
              "FADE_OUT is byte-identical to the shipped fade-out generator");
        check(fabs(o3.phase - pf) < 1e-12, "and leaves the same phase behind");
        check(o3.freq == ff && o3.amp == 0.8,
              "FADE_OUT holds frequency and amplitude — the variant survives");

        /* The fade really fades. */
        check(b[0] != 0 && abs(b[881]) < abs(b[0]) / 8,
              "fade-out ends near silence");
    }

    printf("\n=== D. tone envelope ===\n");
    {
        old_tone_gen(RATE, 880, a, 3528);            /* 80 ms, as audio_beep asks */
        audio_render_tone(RATE, 880, AUDIO_PEAK, b, 3528);
        check(memcmp(a, b, 3528 * sizeof(int16_t)) == 0,
              "the tone generator is byte-identical to the shipped one");

        check(b[0] == 0, "first sample is silence — no click at the attack");
        int peak = 0;
        for (long i = 0; i < 3528; i++) if (abs(b[i]) > peak) peak = abs(b[i]);
        check(peak <= AUDIO_PEAK, "peak never exceeds the amplitude constant");
        check(peak > AUDIO_PEAK * 9 / 10, "and does reach it (the envelope opens)");

        long at = audio_attack_frames(RATE, 3528);
        long rl = audio_release_frames(RATE, 3528);
        check(at == 441 && rl == 882, "10 ms attack / 20 ms release at 44100");
        check(fabs(audio_tone_env(at, 3528, at, rl) - 1.0) < 1e-12,
              "envelope is 1.0 at the end of the attack — continuous");
        check(audio_tone_env(3527, 3528, at, rl) == 0.0,
              "and 0.0 on the last sample");

        /* The joins when they MEET: a 2*attack-frame tone. */
        long fr = 2 * at;
        long at2 = audio_attack_frames(RATE, fr), rl2 = audio_release_frames(RATE, fr);
        double lo = audio_tone_env(at2 - 1, fr, at2, rl2);
        double hi = audio_tone_env(at2,     fr, at2, rl2);
        check(fabs(hi - lo) < 1e-12,
              "attack and release meeting mid-tone is continuous, not a step");

        /* Degenerate lengths: the divide-by-zero cases. */
        check(audio_attack_frames(RATE, 1) == 0 && audio_release_frames(RATE, 1) == 0,
              "a 1-frame tone has no attack and no release");
        check(audio_tone_env(0, 1, 0, 0) == 1.0,
              "and its single sample is not a division by zero");
        audio_render_tone(RATE, 880, AUDIO_PEAK, b, 2);
        check(b[0] == 0 && b[1] == 0,
              "a 2-frame tone renders two silent samples (both ends are edges)");
    }

    printf("\n=== E. mono -> interleaved, the one conversion point ===\n");
    {
        for (int i = 0; i < 8; i++) mono[i] = (int16_t)(1000 + i);
        long n = audio_interleave(mono, 8, 2, inter);
        check(n == 8 * 4, "8 stereo frames are 32 bytes");
        bool dup = true;
        for (int i = 0; i < 8; i++)
            if (inter[i * 2] != mono[i] || inter[i * 2 + 1] != mono[i]) dup = false;
        check(dup, "each mono sample lands in BOTH channels (SPKR1 sums L+R)");
        n = audio_interleave(mono, 8, 1, inter);
        check(n == 16 && inter[3] == mono[3],
              "a 1-channel device gets a plain copy, same code path");
        check(audio_interleave(mono, 8, 0, inter) == 0 &&
              audio_interleave(NULL, 8, 2, inter) == 0,
              "junk in writes nothing");
    }

    printf("\n=== F. measured-absent: two claims the plan made that are FALSE ===\n");
    {
        /* `sound_end_ms` for an N-ms tone is N.  One non-zero writer, audio.c:254,
         * `now + duration_ms`.  Never was 2N. */
        uint32_t now = 1000, end = now + 80;
        check(end - now == 80, "an 80 ms tone ends 80 ms out, not 160");

        /* `time_now_ms()` does not overflow: the mask keeps the peak under
         * UINT32_MAX.  It WRAPS every ~48.5 days, which is a different thing. */
        uint32_t peak = audio_ms_from_timeval(0x3FFFFF, 999999);
        check(peak == 4194303999U, "the clock peaks at 4,194,303,999");
        check(peak < UINT32_MAX, "which is inside UINT32_MAX — no overflow");
        check(audio_ms_from_timeval(0x400000, 0) == 0,
              "the next second wraps to 0 (~48.5 day period)");
        check(audio_ms_from_timeval(1786000000L, 500000) ==
              audio_ms_from_timeval(1786000000L & 0x3FFFFF, 500000),
              "a real epoch value is masked, not multiplied whole");
    }

    printf("\n=== G. flush wait, including across the clock wrap ===\n");
    {
        check(audio_flush_wait_ms(1000, 1080, 200) == 80, "80 ms still to run");
        check(audio_flush_wait_ms(1080, 1000, 200) == 0, "already finished: no wait");
        check(audio_flush_wait_ms(1000, 1000, 200) == 0, "exactly finished: no wait");
        check(audio_flush_wait_ms(1000, 9000, 200) == 200, "a long tone is capped");
        /* Wrap: end recorded just under the mask, now just after it wrapped. */
        check(audio_flush_wait_ms(50, 4194303000U, 200) == 200,
              "across a wrap the nonsense delta is capped at 200 ms — one short wait");
    }

    printf("\n=== H. the write loop stops on frame boundaries, or says so ===\n");
    {
        uint8_t src[4096];
        for (int i = 0; i < 4096; i++) src[i] = (uint8_t)(i & 0xFF);
        AudioWritePolicy pol; memset(&pol, 0, sizeof(pol));
        pol.wait_us = 5000; pol.max_waits = 0; pol.stop_on_again = false;
        AudioWriteResult r;
        AudioSink as;

        sink_reset(&s); as = sink_of(&s);
        audio_write_frames(&as, src, 256, CH, &pol, &r);
        check(r.frames_written == 256 && r.bytes_written == 1024 && !r.misaligned,
              "a willing sink takes all 256 frames");
        check(memcmp(s.got, src, 1024) == 0, "and the bytes arrive unaltered");

        /* Odd granularity: 3 bytes per call is never frame-aligned. */
        sink_reset(&s); s.chunk = 3; as = sink_of(&s);
        audio_write_frames(&as, src, 64, CH, &pol, &r);
        check(r.frames_written == 64 && !r.misaligned,
              "3-bytes-per-call still completes every frame");
        check(memcmp(s.got, src, 256) == 0, "and in order");

        /* The A1 case: stalls mid-frame, forever.  We cannot un-write the 6
         * bytes — but we must not report success, and we must have tried. */
        sink_reset(&s); s.accept_upto = 6; s.stalls = -1; as = sink_of(&s);
        audio_write_frames(&as, src, 8, CH, &pol, &r);
        check(r.misaligned, "a permanently stalled sink mid-frame is REPORTED");
        check(r.frames_written == 1,
              "and the frame count is floored, never rounded up over a half frame");
        check(s.calls > 2, "the remainder of the frame was retried, not abandoned");
        check(r.waits <= 2 * AUDIO_ALIGN_TRIES,
              "bounded: with an UNLIMITED policy it still stops retrying mid-frame");

        /* Stalls mid-frame and then recovers: the frame must complete. */
        sink_reset(&s); s.accept_upto = 6; s.stalls = 2; as = sink_of(&s);
        audio_write_frames(&as, src, 8, CH, &pol, &r);
        check(!r.misaligned && r.frames_written == 8,
              "a sink that recovers gets all 8 frames, alignment intact");

        /* stop_on_again is honoured — but only at a frame boundary. */
        pol.stop_on_again = true;
        sink_reset(&s); s.accept_upto = 8; s.stalls = -1; as = sink_of(&s);
        audio_write_frames(&as, src, 8, CH, &pol, &r);
        check(r.frames_written == 2 && !r.misaligned && r.waits == 0,
              "full ring on a frame boundary: stop at once, 2 whole frames in");

        sink_reset(&s); s.accept_upto = 6; s.stalls = 1; as = sink_of(&s);
        audio_write_frames(&as, src, 8, CH, &pol, &r);
        check(!r.misaligned,
              "full ring MID-frame: it waits anyway rather than swap L/R");

        /* max_waits, and a hard error. */
        pol.stop_on_again = false; pol.max_waits = 3;
        sink_reset(&s); s.accept_upto = 8; s.stalls = -1; as = sink_of(&s);
        audio_write_frames(&as, src, 8, CH, &pol, &r);
        check(r.waits == 3 && r.frames_written == 2 && !r.misaligned,
              "max_waits bounds the retry and leaves the stream aligned");

        sink_reset(&s); s.accept_upto = 8; s.stalls = -1; s.hard_error = true;
        as = sink_of(&s);
        audio_write_frames(&as, src, 8, CH, &pol, &r);
        check(r.sink_error && r.frames_written == 2 && !r.misaligned,
              "a hard error is reported, and stops on a frame boundary");

        /* Nothing in, nothing out. */
        sink_reset(&s); as = sink_of(&s);
        audio_write_frames(&as, src, 0, CH, &pol, &r);
        check(r.frames_written == 0 && !r.misaligned && s.calls == 0,
              "0 frames writes nothing at all");
        audio_write_frames(&as, src, 8, 0, &pol, &r);
        check(r.frames_written == 0 && s.calls == 0,
              "a 0-channel device writes nothing rather than dividing by zero");
    }

    printf("\n=== I. the mix bus: one voice, the sum, and the clamp ===\n");
    {
        /* A single voice must be BYTE-IDENTICAL to a plain tone, or switching an
         * app to the pump changes how every existing sound is heard. */
        long fr = audio_frames_for_ms(RATE, 80);
        audio_render_tone(RATE, 880, AUDIO_PEAK, a, fr);

        AudioMixer m; audio_mix_init(&m, RATE);
        check(audio_mix_add(&m, 880, 80, 0, AUDIO_PEAK) == 0,
              "the first voice lands in slot 0");
        check(audio_mix_render(&m, b, fr) == fr, "and renders its whole length");
        check(memcmp(a, b, (size_t)fr * sizeof(int16_t)) == 0,
              "ONE voice is byte-identical to audio_render_tone() — nothing sounds new");

        /* Incremental render: the pump writes whatever the ring will take. */
        audio_mix_init(&m, RATE);
        audio_mix_add(&m, 880, 80, 0, AUDIO_PEAK);
        audio_mix_render(&m, b,       100);
        audio_mix_render(&m, b + 100, fr - 100);
        check(memcmp(a, b, (size_t)fr * sizeof(int16_t)) == 0,
              "100 + rest equals one full render — a pump boundary does not seam");

        /* Two quiet voices sum pointwise, with no clamping in the way. */
        audio_mix_init(&m, RATE);
        audio_mix_add(&m, 440, 50, 0, 8000);
        audio_mix_render(&m, a, 1024);
        audio_mix_init(&m, RATE);
        audio_mix_add(&m, 1100, 50, 0, 6000);
        audio_mix_render(&m, b, 1024);
        audio_mix_init(&m, RATE);
        audio_mix_add(&m, 440,  50, 0, 8000);
        audio_mix_add(&m, 1100, 50, 0, 6000);
        audio_mix_render(&m, mono, 1024);
        bool summed = true;
        for (long i = 0; i < 1024; i++)
            if (mono[i] != (int16_t)(a[i] + b[i])) summed = false;
        check(summed, "two voices are the POINTWISE SUM of the two rendered alone");
        check(m.clipped == 0, "and 8000 + 6000 needs no clamping");

        /* The clamp, which is now the NON-default mode and has to be asked for.
         * AUDIO_PEAK is ~55 % of full scale, so two identical loud voices exceed
         * int16 by ~10 % — and a panel heard three of them as a square wave
         * (../IMPROVEMENT_PLAN.md F1 Phase 3), which is why SOFT is the default. */
        audio_mix_init(&m, RATE);
        audio_mix_set_limit(&m, AUDIO_MIX_HARD);
        audio_mix_add(&m, 880, 80, 0, AUDIO_PEAK);
        audio_mix_add(&m, 880, 80, 0, AUDIO_PEAK);
        audio_mix_render(&m, b, fr);
        check(m.clipped > 0, "two voices at AUDIO_PEAK DO clip under HARD — measured, not assumed");
        bool pos_pinned = false, neg_pinned = false;
        for (long i = 0; i < fr; i++) {
            if (b[i] ==  32767) pos_pinned = true;
            if (b[i] == -32768) neg_pinned = true;
        }
        check(pos_pinned, "clamped to the int16 ceiling, not wrapped");
        check(neg_pinned, "and to the floor on the negative half — symmetric");

        /* ⚠️ The clamp happens ONCE, after the whole sum.  Accumulating into
         * int16 instead would make the result depend on which slot a voice sits
         * in, so the same three sounds would mix differently run to run. */
        AudioMixer f, r;
        audio_mix_init(&f, RATE);
        audio_mix_set_limit(&f, AUDIO_MIX_HARD);
        audio_mix_add(&f, 523, 100, 0, AUDIO_PEAK);
        audio_mix_add(&f, 661, 100, 0, AUDIO_PEAK);
        audio_mix_add(&f, 787, 100, 0, AUDIO_PEAK);
        audio_mix_render(&f, a, 2048);
        audio_mix_init(&r, RATE);
        audio_mix_set_limit(&r, AUDIO_MIX_HARD);
        audio_mix_add(&r, 787, 100, 0, AUDIO_PEAK);
        audio_mix_add(&r, 661, 100, 0, AUDIO_PEAK);
        audio_mix_add(&r, 523, 100, 0, AUDIO_PEAK);
        audio_mix_render(&r, b, 2048);
        check(f.clipped > 0,
              "three loud voices clip — so this check is exercising the clamp");
        check(memcmp(a, b, 2048 * sizeof(int16_t)) == 0,
              "and SLOT ORDER does not change the mix: one clamp, after the sum");
    }

    printf("\n=== J. voice lifetime ===\n");
    {
        AudioMixer m; audio_mix_init(&m, RATE);
        audio_mix_add(&m, 880, 10, 0, AUDIO_PEAK);       /* 441 frames */
        check(audio_mix_active(&m) == 1 && audio_mix_pending(&m) == 441,
              "a 10 ms voice is 441 frames pending at 44100");

        audio_mix_render(&m, a, 440);
        check(audio_mix_active(&m) == 1 && audio_mix_pending(&m) == 1,
              "one frame short: still alive, exactly 1 frame owed");
        audio_mix_render(&m, a, 1);
        check(audio_mix_active(&m) == 0 && audio_mix_pending(&m) == 0,
              "the voice frees itself on its LAST sample, not a render later");

        /* A voice that ends mid-buffer: the tail must be silence, not stale
         * scratch — the pump reuses one buffer for the life of the process. */
        audio_mix_init(&m, RATE);
        audio_mix_add(&m, 880, 10, 0, AUDIO_PEAK);
        for (long i = 0; i < 600; i++) a[i] = 12345;      /* poison */
        check(audio_mix_render(&m, a, 600) == 600, "renders the frames asked for");
        check(has_signal(a, 0, 441), "the voice is in the first 441 frames");
        check(!has_signal(a, 441, 600),
              "and the 159 frames past its end are SILENCE, not last frame's audio");
        check(audio_mix_active(&m) == 0, "the slot is free again");

        /* Delay: silence first, then the note.  This is what makes an arpeggio. */
        audio_mix_init(&m, RATE);
        audio_mix_add(&m, 880, 10, 20, AUDIO_PEAK);      /* 882 delay + 441 */
        check(audio_mix_pending(&m) == 882 + 441,
              "pending counts the delay as well as the length");
        audio_mix_render(&m, a, 1400);
        check(!has_signal(a, 0, 882), "the delayed voice is silent for its 882 frames");
        check(has_signal(a, 882, 1323), "then sounds for 441");
        check(!has_signal(a, 1323, 1400), "then stops");

        /* A silent bus does not touch the buffer — that is how the pump tells
         * "nothing to say" from "a buffer of silence", which is the difference
         * between writing nothing and keeping the DAC clocked. */
        audio_mix_init(&m, RATE);
        for (long i = 0; i < 64; i++) a[i] = 999;
        check(audio_mix_render(&m, a, 64) == 0, "an idle bus renders 0 frames");
        check(a[0] == 999 && a[63] == 999, "and leaves the caller's buffer alone");

        /* A full bus REFUSES.  It must not steal a playing voice: the longest
         * one is the thing a dropped blip must never cut (F19's music). */
        audio_mix_init(&m, RATE);
        for (int i = 0; i < AUDIO_MAX_VOICES; i++)
            check(audio_mix_add(&m, 440 + 50 * i, 100, 0, AUDIO_PEAK) == i,
                  "slots fill in order");
        check(audio_mix_add(&m, 990, 100, 0, AUDIO_PEAK) == -1,
              "the ninth sound is refused");
        check(m.dropped == 1, "and counted as a drop");
        check(audio_mix_active(&m) == AUDIO_MAX_VOICES,
              "the eight already sounding are untouched — nothing was stolen");

        /* Refusals that are caller bugs are NOT drops: `dropped` has to keep
         * meaning "the bus was full" to be worth watching. */
        audio_mix_init(&m, RATE);
        check(audio_mix_add(&m, 880, 100, 0, 0) == -1 &&
              audio_mix_add(&m, 880, 100, 0, -1) == -1,
              "a zero or negative peak is refused");
        check(audio_mix_add(&m, 0, 100, 0, AUDIO_PEAK) == -1 &&
              audio_mix_add(&m, 880, 0, 0, AUDIO_PEAK) == -1,
              "so are a zero frequency and a zero duration");
        check(m.dropped == 0, "and none of those counts as a drop");

        AudioMixer bad; audio_mix_init(&bad, 0);
        check(audio_mix_add(&bad, 880, 100, 0, AUDIO_PEAK) == -1,
              "a mixer with no rate cannot take a voice");

        /* stop_all is what audio_interrupt() becomes on the pump. */
        audio_mix_init(&m, RATE);
        audio_mix_add(&m, 880, 300, 0, AUDIO_PEAK);
        audio_mix_add(&m, 440, 300, 0, AUDIO_PEAK);
        audio_mix_stop_all(&m);
        check(audio_mix_active(&m) == 0 && audio_mix_pending(&m) == 0,
              "stop_all silences every voice at once");
        check(audio_mix_render(&m, a, 64) == 0, "and the bus then renders nothing");
    }

    printf("\n=== K. pump pacing: a LEAD, never 'fill the ring' ===\n");
    {
        long lead = audio_frames_for_ms(RATE, AUDIO_PUMP_LEAD_MS);
        long cap  = audio_frames_for_ms(RATE, AUDIO_PUMP_CAP_MS);
        check(lead == 3528 && cap == 3528,
              "80 ms of lead and cap is 3528 frames at 44100");

        /* ⚠️ THE TRAP.  An empty ~506 ms OSS ring will accept 22317 frames, and
         * a pump that writes them puts every sound triggered on the next frame
         * half a second late.  The lead is the latency ceiling. */
        check(audio_pump_frames(lead, 0, 22317, cap) == 3528,
              "an EMPTY ring gets 80 ms, not the 506 ms it would accept");
        /* ⚠️ With the cap equal to the lead, the line above passes even for a
         * pump that just writes the free space — the cap catches it by accident.
         * Take the cap out of the way so only the lead can bound the answer. */
        check(audio_pump_frames(lead, 0, 22317, 22317) == 3528,
              "with the cap out of the way it is still 80 ms — the LEAD is the bound");

        check(audio_pump_frames(lead, lead, 22317, cap) == 0,
              "already a full lead ahead: render nothing this frame");
        check(audio_pump_frames(lead, lead + 500, 22317, cap) == 0,
              "further ahead than that: still nothing, never negative");
        check(audio_pump_frames(lead, 3000, 22317, cap) == 528,
              "part-drained: top the lead back up, no more");

        check(audio_pump_frames(lead, 0, 200, cap) == 200,
              "a nearly full ring caps at the space it actually has");
        check(audio_pump_frames(lead, 0, 22317, 441) == 441,
              "and the per-call cap bounds the work one frame may do");
        check(audio_pump_frames(lead, 0, 22317, 0) == 3528,
              "cap 0 means unbounded, so the lead decides");

        check(audio_pump_frames(0, 0, 22317, cap) == 0 &&
              audio_pump_frames(-1, 0, 22317, cap) == 0,
              "no lead target, nothing to do");
        check(audio_pump_frames(lead, 0, 0, cap) == 0 &&
              audio_pump_frames(lead, 0, -8, cap) == 0,
              "no space, nothing written — the pump never blocks to make room");
        check(audio_pump_frames(lead, -9999, 22317, cap) == 3528,
              "a nonsense in-flight read-back cannot inflate the request past the cap");
    }

    printf("\n=== L. the soft limiter: what the panel rejected, and why this fixes it ===\n");
    {
        /* The defect this group exists for, stated as arithmetic: three voices at
         * AUDIO_PEAK sum to 54000 against int16's 32767, so the hard clamp
         * flattens 65 % of the peak — heard on `.188` 2026-08-15 as "a distorted
         * square wave from an overdriven amplifier", with clip = 15402. */
        check(3 * AUDIO_PEAK > 32767 && 2 * AUDIO_PEAK > 32767,
              "two AND three voices at AUDIO_PEAK both exceed int16 — the report is "
              "arithmetically expected, not a mystery");

        /* Below the knee the limiter is the identity, in BOTH modes.  This is what
         * keeps one voice byte-identical to audio_render_tone(). */
        check(audio_mix_limit(0, AUDIO_MIX_SOFT) == 0 &&
              audio_mix_limit(1234, AUDIO_MIX_SOFT) == 1234 &&
              audio_mix_limit(-1234, AUDIO_MIX_SOFT) == -1234,
              "quiet samples pass through the soft curve untouched");
        check(audio_mix_limit(AUDIO_MIX_KNEE, AUDIO_MIX_SOFT) == AUDIO_MIX_KNEE &&
              audio_mix_limit(-AUDIO_MIX_KNEE, AUDIO_MIX_SOFT) == -AUDIO_MIX_KNEE,
              "and the knee itself is exactly on the identity — no step there");
        check(AUDIO_MIX_KNEE == AUDIO_PEAK,
              "the knee IS one voice's peak, so a single voice can never reach the bend");

        /* Continuity of slope, not just of value: one sample past the knee must
         * move by about one, or the curve has a corner an ear hears as a click. */
        int32_t at   = audio_mix_limit(AUDIO_MIX_KNEE, AUDIO_MIX_SOFT);
        int32_t past = audio_mix_limit(AUDIO_MIX_KNEE + 1, AUDIO_MIX_SOFT);
        check(past - at == 1, "slope is 1 at the knee — the join is smooth, not a corner");

        /* Monotone, odd, and bounded for every input a full bus can produce. */
        bool mono_ok = true, odd_ok = true, bounded = true;
        int32_t prev = audio_mix_limit(0, AUDIO_MIX_SOFT);
        for (int32_t x = 1; x <= AUDIO_MAX_VOICES * AUDIO_PEAK + 1000; x += 7) {
            int32_t y = audio_mix_limit(x, AUDIO_MIX_SOFT);
            if (y < prev)                        mono_ok = false;
            if (audio_mix_limit(-x, AUDIO_MIX_SOFT) != -y) odd_ok = false;
            if (y > AUDIO_MIX_CEIL)              bounded = false;
            prev = y;
        }
        check(mono_ok, "monotone all the way to eight voices at full peak");
        check(odd_ok,  "odd-symmetric: the negative half is not treated differently");
        check(bounded, "bounded by AUDIO_MIX_CEIL for every reachable sum");
        check(AUDIO_MIX_CEIL < 32767,
              "and that ceiling is INSIDE int16, which is what makes clip == 0 provable");
        check(AUDIO_MIX_CEIL < 2 * AUDIO_PEAK,
              "the ceiling is also below two voices' arithmetic sum — the limiter "
              "protects the SPEAKER, not just the int16 store");

        /* The hard mode is unchanged, and is the negative control. */
        check(audio_mix_limit(54000, AUDIO_MIX_HARD) == 32767 &&
              audio_mix_limit(-54000, AUDIO_MIX_HARD) == -32768,
              "HARD still clamps at the int16 rails — the rejected behaviour is intact");
        check(audio_mix_limit(54000, AUDIO_MIX_SOFT) < 32767 &&
              audio_mix_limit(54000, AUDIO_MIX_SOFT) > AUDIO_PEAK,
              "SOFT maps the same sum below the rail but still LOUDER than one voice");

        /* And now the two mixes, end to end. */
        long fr = audio_frames_for_ms(RATE, 100);
        AudioMixer m; audio_mix_init(&m, RATE);
        check(m.limit == AUDIO_MIX_SOFT, "SOFT is the DEFAULT — an app gets the fix without asking");
        audio_mix_add(&m, 523, 100, 0, AUDIO_PEAK);
        audio_mix_add(&m, 659, 100, 0, AUDIO_PEAK);
        audio_mix_add(&m, 784, 100, 0, AUDIO_PEAK);
        audio_mix_render(&m, b, fr);
        check(m.clipped == 0,
              "THE CHECK: three voices at AUDIO_PEAK now clip ZERO samples");
        check(m.limited > 0, "the knee did the work instead, and says so");
        check(peak_abs(b, fr) <= AUDIO_MIX_CEIL,
              "no sample past the ceiling — nothing pinned at the rail");

        /* A single voice through the default path is still bit-for-bit the old
         * sound: the fix must not change 45 call sites' one-shot beeps. */
        audio_render_tone(RATE, 880, AUDIO_PEAK, a, fr);
        audio_mix_init(&m, RATE);
        audio_mix_add(&m, 880, 100, 0, AUDIO_PEAK);
        audio_mix_render(&m, b, fr);
        check(memcmp(a, b, (size_t)fr * sizeof(int16_t)) == 0 &&
              m.limited == 0 && m.clipped == 0,
              "one voice under the limiter is STILL byte-identical to a plain tone");

        /* Eight voices — the full bus — is the worst case that can reach it. */
        audio_mix_init(&m, RATE);
        for (int i = 0; i < AUDIO_MAX_VOICES; i++)
            audio_mix_add(&m, 300 + 100 * i, 100, 0, AUDIO_PEAK);
        audio_mix_render(&m, b, fr);
        check(m.clipped == 0 && peak_abs(b, fr) <= AUDIO_MIX_CEIL,
              "a FULL BUS of eight loud voices still clips nothing");

        /* Switching mode mid-render is what the panel's LIMIT toggle does. */
        audio_mix_init(&m, RATE);
        audio_mix_add(&m, 523, 100, 0, AUDIO_PEAK);
        audio_mix_add(&m, 659, 100, 0, AUDIO_PEAK);
        audio_mix_render(&m, b, 512);
        uint32_t soft_clip = m.clipped;
        audio_mix_set_limit(&m, AUDIO_MIX_HARD);
        audio_mix_render(&m, b, 512);
        check(soft_clip == 0 && m.clipped > 0,
              "the A/B toggle takes effect mid-tone — soft half clean, hard half clipping");
        audio_mix_set_limit(&m, 99);
        check(m.limit == AUDIO_MIX_SOFT,
              "an unrecognised mode resolves to SOFT rather than to the rejected clamp");
    }

    printf("\n=== M. the lead is whole PERIODS, because the shim only moves in periods ===\n");
    {
        /* The measured device: 44100 Hz, fragsize 8192 B = 2048 frames, 16 frags
         * = 32768 frames of ring (`.188`, 2026-08-15). */
        const long P = 2048, RING = 32768;
        long ms80 = audio_frames_for_ms(RATE, AUDIO_PUMP_LEAD_MS);   /* 3528 */

        check(ms80 > P && ms80 < 2 * P,
              "80 ms is 1.7 periods on this device — between two, which is the defect");
        long lead = audio_pump_lead_frames(ms80, P, AUDIO_PUMP_LEAD_PERIODS, RING);
        check(lead == 3 * P,
              "so the pump targets THREE whole periods (6144 frames, ~139 ms) instead");
        check(lead % P == 0, "and the target is always a whole number of periods");

        check(audio_pump_lead_frames(ms80, 0, 3, RING) == ms80 &&
              audio_pump_lead_frames(ms80, -8, 3, RING) == ms80,
              "an unmeasured period falls back to the ms figure rather than to 0");
        check(audio_pump_lead_frames(ms80, P, 0, RING) == ms80,
              "min_periods 0 disables the floor — the ms figure again");

        check(audio_pump_lead_frames(4096, P, 1, RING) == 4096,
              "a lead already on a period boundary is left alone");
        check(audio_pump_lead_frames(4097, P, 1, RING) == 6144,
              "one frame over rounds UP, never down — down is the XRUN case");
        check(audio_pump_lead_frames(441, P, 3, RING) == 3 * P,
              "a 10 ms lead cannot undercut the period floor");
        check(audio_pump_lead_frames(20000, P, 3, 200000) == 20480,
              "a genuinely deeper ms lead wins, rounded up to a period");

        check(audio_pump_lead_frames(ms80, P, 3, 8192) == 4096,
              "half the ring caps it — never ask for cushion the device cannot hold");
        check(audio_pump_lead_frames(ms80, P, 3, 2048) == P,
              "and on a ring smaller than that, one period rather than zero");
        check(audio_pump_lead_frames(0, P, 3, RING) == 3 * P &&
              audio_pump_lead_frames(-5, P, 3, RING) == 3 * P,
              "a nonsense ms figure still yields a usable, period-aligned lead");

        /* The lead is the latency ceiling, so the number is a decision, not a
         * detail: it must stay well inside the ring and well under a second. */
        check(lead < RING / 2 && lead < RATE,
              "3 periods is far inside the ring and well under a second of latency");
    }

    printf("\n%s  %d checks, %d failure(s)\n",
           failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
