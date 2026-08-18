/*
 * audio_out_test.c — the one output stream, on the host, with no fd in it.
 *
 * `common/audio_out.c` is the device half of the audio library: it opens the
 * device once, reads the GRANT back, prefills silence, and then never resets and
 * never reconfigures until shutdown.  That is F1 defect 3's fix — the click is a
 * stream transition, and every canned sound used to be a full stop and start.
 * This drives all of it through the injectable `AudioOutDev` vtable, so the whole
 * file is reachable on the host and no test here opens anything.
 *
 * GROUP A IS THE NEGATIVE CONTROL and it drives the OLD idiom, transcribed from
 * the shipped file (`audio.c` as of 2026-08-18, 710 lines):
 *
 *   A1  a ring reset before EVERY sound.  `audio_flush()` (audio.c:206-215) fires
 *       it and re-runs `configure_dsp()`, and `play_sequence()` (:679) calls it
 *       before each canned sound — so a game with N sounds performs N stream
 *       transitions, which is what the operator hears as "every time there is a
 *       sound, there is a click".
 *   A2  the ~60 ms floor.  ⚠️ **MODELLED, not measured here**: the fake device
 *       swallows a start-up run of frames after each transition, which is the
 *       mechanism `audio.c:196-199` attributes the rule to.  A 30 ms tone through
 *       the old idiom is therefore inaudible and the same tone through the stream
 *       is not.  The frame count is a stand-in for ~50 ms of DAC pipeline start-up;
 *       nothing in the tree ever clamped 60 ms, it is prose only.
 *
 * Group B is the GRANT: every byte count in the library derives from what the
 * device granted, never from what was asked for, and a 0-channel grant is refused
 * rather than turned into a 0-byte write.  Group C is the lead, in whole device
 * periods.  Group D is the serviced mode, including the two counters that mean
 * one thing each.  Group E is the single fill callback and the loud refusal that
 * keeps two writers impossible.  Group F is the attenuation, swept over every
 * int16 at shift 0 and shift 1 — `-1 >> 1 == -1` is why it is a shift and not a
 * gain multiply, and it is what keeps ScummVM bit-identical to what was verified
 * on the panel.  Group G is the synchronous mode the two Settings tabs need,
 * which have no render loop at all.  Group H is the bounded drain.  Group I is
 * the published service ceiling, which is derived from REAL audio rather than the
 * nominal lead.  Group J is one stream per process.
 *
 * ⚠️ **This file is NEW, so "seen failing against the pre-change source" cannot
 * mean compiling it against an older `audio_out.c` — there is none.**  The
 * equivalent evidence is `measure_audio_out_sabotage.sh`, which breaks one stated
 * rule at a time in a COPY and counts what notices.  Measured 2026-08-18:
 *
 *     sabotage                                      failed  caught by
 *      1  prefill dropped, the stream starts empty     5     A3b D6 D7 D8 D14
 *      2  scratch not zeroed before the fill           1     D4b
 *      3  service fills the free space, not the lead   8     D1 D5 D6 D7 D8 D11 …
 *      4  lead from the ms constant, not the period    8     C2 C3 C4 C5 D5 D11 …
 *      5  channels from the request, not the grant     8     B2 B3 B4 B5 B6 B7 …
 *                                                            then SIGFPE
 *      6  attenuation as a divide, not a shift         1     F2
 *      7  mode 2 not refused against a callback        1     E4
 *      8  drain waits for zero, not the slack          1     H4
 *      9  serviced policy given a non-zero wait        1     D13c
 *     10  CONTROL: audio_out_starved() returns 0       1     D10
 *
 * ⚠️ **Case 10 is the control and its count is the one to read first**: a single
 * lying accessor must fail exactly ONE check in ONE group.  A cascade there would
 * mean the suite is coupled and every other count above is inflated.
 *
 * ⚠️ **Three stanzas read 0 on the first run and only one of the three was telling
 * the truth, so reading the sweep is part of running it:**
 *
 *   - **case 5 was a LOST BUFFER, not a pass.**  `out=$(…)` makes the child's
 *     stdout a pipe, libc switches to full buffering, and the SIGFPE discarded
 *     eight `FAIL:` lines it had already printed.  The sweep now line-buffers the
 *     child and reports the signal — without both, a sabotage that CRASHES this
 *     suite is indistinguishable from one the suite cannot see.
 *   - **case 2 was a hole in this file.**  `D4` ran after three SILENT services,
 *     so the scratch was already zero and it passed with the `memset` deleted.
 *     Split into `D4a` (dirty the scratch first) and `D4b` (the real check).
 *   - **case 9 was a hole in this file.**  Nothing separated a wait from a SLEEP,
 *     so the load-bearing `wait_us == 0` was untested.  The fake now counts
 *     `waits_slept` only at `usec > 0`, and `D13b`/`D13c` drive the one path that
 *     can break it: a permanent mid-frame stall, where the policy's stop condition
 *     deliberately does not apply.
 *
 * Case 1 still fails four pacing checks in group D, but only as a side-effect of
 * the queue being empty; `A3b` is the check that states the property itself.
 *
 * What HAS been seen failing, on 2026-08-18, is three real defects — two in the
 * suite and one in the library, all found by running it:
 *
 *   - ⚠️ **the prefill policy was UNBOUNDED and hung `audio_out_open()` forever**
 *     against a device that would not take the prefill (`max_waits = 0` means
 *     unlimited, and `stop_on_again` is false because a prefill is worth waiting
 *     for).  The suite did not report it — it hung, which is a test result.  The
 *     fix derives the bound from the buffer's own length; `D12b` is the case.
 *   - the one-stream-per-process guard turned every later group's open into a
 *     refusal, because group D re-opened six times without closing.  A cascade of
 *     28 failures from one leak.
 *   - two expectations were wrong rather than the code: a transition count that
 *     included an open that never happened, and a drain asserted to reach zero
 *     when it deliberately stops at the over-report slack.
 *
 * Needs no sudo, no device and no network.
 *
 * Build (host gcc, from native_apps/):
 *   gcc -Wall -Wextra -Wno-unused-parameter -I common -o build/audio_out_test \
 *       tests/audio_out_test.c common/audio_out.c common/audio_gen.c -lm && \
 *   ./build/audio_out_test
 *
 * The zero that must stay zero is checked by grep rather than by a case here,
 * because it is an absence: the steady-state path performs no ring reset at all.
 *   grep -c 'SNDCTL_DSP_RE[S]ET' native_apps/common/audio_out.c   -> 0
 * The bracket is deliberate — an unbracketed pattern would match this line and
 * the gate would count its own documentation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "../common/audio_gen.h"
#include "../common/audio_out.h"

static int failures = 0;
static int checks   = 0;

static void check(bool cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("  FAIL: %s\n", what); }
    else       { printf("  ok:   %s\n", what); }
}

/* ── A device whose behaviour the test dictates ──────────────────────────────
 *
 * A simulated queue, plus the two things about the real one that no derived
 * number can stand in for: it can OVER-REPORT what it is holding (measured
 * 2650–2670 frames on `.188` 2026-08-18, ~1.3 of its 2048-frame period), and it
 * can swallow a run of frames after a transition, which is the mechanism the
 * ~60 ms minimum-tone rule is attributed to.
 */

#define FAKE_CAP  (1 << 19)

typedef struct {
    /* What it grants, regardless of what is asked for. */
    int  grant_rate, grant_bits, grant_channels;
    bool fail_open;

    int  fb;                /* frame bytes — the test sets this to match       */
    long period, ring;      /* frames                                          */
    long qbytes;            /* bytes it is holding                             */
    long over_report;       /* frames it ADDS to in_flight and hides from space */
    bool drain_all;         /* every space() empties the queue first            */
    long drain_per_space;   /* otherwise, frames drained per space() call       */
    bool fail_space;

    int  chunk;             /* max bytes taken per write (0 = whatever is asked) */
    long accept_upto;       /* -1 = unlimited; else total bytes it will ever take */
    bool hard_error;        /* stall as a real error rather than "full"          */

    long startup_frames;    /* frames swallowed after a transition (MODELLED)    */
    long swallow_left;

    /* Observations. */
    int  opens, closes, transitions, spaces, writes, waits, waits_slept;
    long audible;
    uint8_t got[FAKE_CAP];
    long len;
} Fake;

static void fake_transition(Fake *f)
{
    f->transitions++;
    f->swallow_left = f->startup_frames;
    f->qbytes       = 0;
}

static int fake_open(void *ctx, int rate_req, int ch_req,
                     int *rate, int *bits, int *ch)
{
    Fake *f = (Fake *)ctx;
    if (f->fail_open) return -1;
    f->opens++;
    fake_transition(f);
    /* A grant of 0 means "the read-back failed"; the library must not turn that
     * into a 0-byte write.  Anything else is what the device chose, and it is
     * deliberately allowed to differ from the request. */
    *rate = f->grant_rate ? f->grant_rate : rate_req;
    *bits = f->grant_bits;
    *ch   = f->grant_channels;
    (void)ch_req;
    return 0;
}

static int fake_space(void *ctx, int frame_bytes, AudioOutSpace *sp)
{
    Fake *f = (Fake *)ctx;
    f->spaces++;
    if (f->fail_space || frame_bytes <= 0) return -1;

    if (f->drain_all) f->qbytes = 0;
    else {
        f->qbytes -= f->drain_per_space * f->fb;
        if (f->qbytes < 0) f->qbytes = 0;
    }

    long ring_bytes = f->ring * f->fb;
    long held       = f->qbytes / f->fb;

    sp->period_frames = f->period;
    sp->ring_frames   = f->ring;
    /* in_flight + space == ring always, which is the shape of the real ioctl:
     * over-reporting the queue is under-reporting the free space. */
    sp->in_flight     = held + f->over_report;
    sp->space         = (ring_bytes - f->qbytes) / f->fb - f->over_report;
    if (sp->space < 0) sp->space = 0;
    return 0;
}

static ssize_t fake_write(void *ctx, const void *buf, size_t n, bool *again)
{
    Fake *f = (Fake *)ctx;
    f->writes++;

    if (f->accept_upto >= 0 && f->len >= f->accept_upto) {
        if (f->hard_error) { *again = false; return -1; }
        *again = true;
        return -1;
    }

    long room = f->ring * f->fb - f->qbytes;
    if (room <= 0) { *again = true; return -1; }

    long take = (long)n;
    if (take > room) take = room;
    if (f->chunk > 0 && take > f->chunk) take = f->chunk;
    if (f->accept_upto >= 0 && f->len + take > f->accept_upto)
        take = f->accept_upto - f->len;
    if (take <= 0) { *again = true; return -1; }
    if (f->len + take > FAKE_CAP) take = FAKE_CAP - f->len;
    if (take <= 0) { *again = true; return -1; }

    memcpy(f->got + f->len, buf, (size_t)take);
    f->len    += take;
    f->qbytes += take;

    /* The modelled DAC: whole frames only, and the run after a transition is
     * never heard. */
    long frames = take / f->fb;
    if (f->swallow_left >= frames) { f->swallow_left -= frames; }
    else { f->audible += frames - f->swallow_left; f->swallow_left = 0; }

    return (ssize_t)take;
}

static void fake_wait(void *ctx, int usec)
{
    Fake *f = (Fake *)ctx;
    f->waits++;
    /* ⚠️ Counting a wait and counting a SLEEP are different measurements, and only
     * the second one can test "service() never sleeps".  Every real backend's wait
     * (`oss_wait()`, `dsp_wait`) is a no-op at `usec <= 0`, and the serviced policy
     * passes 0 — so the call still arrives here and must not be counted as sleep.
     * Without this split the non-zero-wait sabotage passed the whole suite. */
    if (usec > 0) f->waits_slept++;
    /* A wait is when the hardware makes progress, so the queue drains here too —
     * otherwise a bounded blocking policy could never finish and the drain loop
     * would always run to its bound. */
    if (!f->drain_all) {
        f->qbytes -= f->drain_per_space * f->fb;
        if (f->qbytes < 0) f->qbytes = 0;
    }
}

static void fake_close(void *ctx) { ((Fake *)ctx)->closes++; }

static const AudioOutDev FAKE_DEV = {
    fake_open, fake_space, fake_write, fake_wait, fake_close
};

/** 44100 / 2 channels / 2048-frame period / 16-period ring — the configuration
 *  `native_apps` is actually granted (F1 Phase 0, measured on `.188`). */
static void fake_reset(Fake *f)
{
    memset(f, 0, sizeof(*f));
    f->grant_rate     = 44100;
    f->grant_bits     = 16;
    f->grant_channels = 2;
    f->fb             = 2 * AUDIO_BYTES_PER_SAMPLE;
    f->period         = 2048;
    f->ring           = 32768;
    f->accept_upto    = -1;
    f->drain_all      = true;
}

/* ── Fills the test installs ────────────────────────────────────────────── */

typedef struct {
    int16_t value;      /* every sample takes this value                      */
    long    produce;    /* frames to claim; -1 = all of them                  */
    int     calls;
    long    last_frames;
    int     last_channels;
    bool    poison;     /* write a sentinel into the frames it does NOT claim  */
    long    seq;        /* running counter for the shift sweep                */
} FillCtl;

static long fill_const(void *ctx, int16_t *buf, long frames, int channels)
{
    FillCtl *c = (FillCtl *)ctx;
    c->calls++;
    c->last_frames   = frames;
    c->last_channels = channels;

    long claim = (c->produce < 0 || c->produce > frames) ? frames : c->produce;
    for (long i = 0; i < claim * channels; i++) buf[i] = c->value;
    if (c->poison)
        for (long i = claim * channels; i < frames * channels; i++) buf[i] = 999;
    return claim;
}

/** Emits successive int16 values so one sweep can cover the whole range. */
static long fill_ramp(void *ctx, int16_t *buf, long frames, int channels)
{
    FillCtl *c = (FillCtl *)ctx;
    c->calls++;
    c->last_frames = frames;
    for (long i = 0; i < frames * channels; i++)
        buf[i] = (int16_t)(int32_t)(-32768 + ((c->seq++) & 0xFFFF));
    return frames;
}

/** A fill that touches nothing at all — `audio_mix_render()` on a silent bus. */
static long fill_silent(void *ctx, int16_t *buf, long frames, int channels)
{
    FillCtl *c = (FillCtl *)ctx;
    (void)buf; (void)frames; (void)channels;
    c->calls++;
    return 0;
}

/* ── Buffer predicates ──────────────────────────────────────────────────── */

static bool all_zero(const uint8_t *b, long from, long to)
{
    for (long i = from; i < to; i++) if (b[i]) return false;
    return true;
}

static long count_nonzero_samples(const uint8_t *b, long bytes)
{
    const int16_t *s = (const int16_t *)b;
    long n = 0;
    for (long i = 0; i < bytes / 2; i++) if (s[i]) n++;
    return n;
}

/* ── Group A: the shipped idiom, transcribed ─────────────────────────────────
 *
 * Transcribed, not called: `audio_flush()` (audio.c:206-215) resets the ring and
 * re-runs `configure_dsp()`, and `play_sequence()` (:679) calls it before every
 * canned sound.  `audio_tone()` (:490-524) then writes the whole tone.
 */
static long old_play_tone(Fake *f, int rate, int freq, int ms, int16_t *mono,
                          int16_t *ilv)
{
    fake_transition(f);                       /* audio_flush(): reset + configure */
    long frames = audio_frames_for_ms(rate, ms);
    audio_render_tone(rate, freq, AUDIO_PEAK, mono, frames);
    audio_interleave(mono, frames, f->fb / AUDIO_BYTES_PER_SAMPLE, ilv);
    bool again = false;
    long bytes = frames * f->fb;
    long done  = 0;
    while (done < bytes) {
        ssize_t r = fake_write(f, (const uint8_t *)ilv + done, (size_t)(bytes - done),
                               &again);
        if (r <= 0) break;
        done += r;
    }
    return frames;
}

/* ── main ───────────────────────────────────────────────────────────────── */

#define RATE 44100

int main(void)
{
    static int16_t mono[1 << 17];
    static int16_t ilv[1 << 18];
    static Fake f;
    FillCtl fc;
    AudioOut out;

    printf("\n=== A. the OLD idiom, transcribed (the defects must reproduce) ===\n");
    {
        /* ~50 ms of DAC pipeline start-up at 44100, MODELLED as swallowed frames. */
        const long startup = audio_frames_for_ms(RATE, 50);

        fake_reset(&f);
        f.startup_frames = startup;
        f.drain_all      = true;
        old_play_tone(&f, RATE, 880, 80, mono, ilv);
        old_play_tone(&f, RATE, 660, 80, mono, ilv);
        old_play_tone(&f, RATE, 523, 80, mono, ilv);
        check(f.transitions == 3,
              "A1 old idiom: three sounds cost three stream transitions — one "
              "click each, which is the operator's complaint");

        fake_reset(&f);
        f.startup_frames = startup;
        long want = old_play_tone(&f, RATE, 880, 30, mono, ilv);
        check(want > 0 && f.audible == 0,
              "A2 old idiom: a 30 ms tone after a reset is ENTIRELY swallowed by "
              "the modelled DAC start-up — the ~60 ms floor, reproduced");

        /* The same device, driven by the library instead. */
        fake_reset(&f);
        f.startup_frames = startup;
        check(audio_out_open(&out, &FAKE_DEV, &f, RATE, 2) == 0,
              "A3 the stream opens on the same fake device");
        check(f.len == audio_out_lead(&out) * f.fb && f.len > 0 &&
              all_zero(f.got, 0, f.len),
              "A3b and the open PREFILLS that lead with silence — a stream started "
              "empty starts with the very transition this design removes.  Added "
              "because the sabotage sweep caught a dropped prefill only through its "
              "pacing side-effects, which is a coincidence rather than a check");
        long t_after_open = f.transitions;
        for (int i = 0; i < 20; i++) audio_out_service(&out);
        fc.value = 4000; fc.produce = -1; fc.calls = 0; fc.poison = false; fc.seq = 0;
        audio_out_set_fill(&out, fill_const, &fc, "test");
        for (int i = 0; i < 20; i++) audio_out_service(&out);
        audio_out_set_fill(&out, NULL, NULL, NULL);
        for (int i = 0; i < 20; i++) audio_out_service(&out);
        check(f.transitions == t_after_open,
              "A4 the stream: 60 services and two callback swaps cost ZERO further "
              "transitions — the device is configured exactly once, at open");
        check(f.opens == 1,
              "A5 and the device is opened exactly once, so nothing reconfigured it");
        audio_out_close(&out);
    }

    printf("\n=== B. the GRANT, never the request ===\n");
    {
        fake_reset(&f);
        f.grant_rate = 22050; f.grant_channels = 1; f.fb = 1 * AUDIO_BYTES_PER_SAMPLE;
        check(audio_out_open(&out, &FAKE_DEV, &f, 44100, 2) == 0,
              "B1 a device granting 22050 mono against a 44100 stereo request opens");
        check(audio_out_rate(&out) == 22050 && audio_out_channels(&out) == 1,
              "B2 the library reports the GRANT, not the request");
        fc.value = 1000; fc.produce = -1; fc.calls = 0; fc.poison = false;
        audio_out_set_fill(&out, fill_const, &fc, "test");
        f.len = 0;
        long got = audio_out_service(&out);
        check(fc.last_channels == 1,
              "B3 the fill callback is TOLD the granted channel count — it is an "
              "argument, never a literal");
        check(got > 0 && f.len == got * 1 * AUDIO_BYTES_PER_SAMPLE,
              "B4 and every byte count follows the grant: one sample per frame");
        audio_out_close(&out);

        fake_reset(&f);
        f.grant_channels = 0;             /* the read-back failed */
        check(audio_out_open(&out, &FAKE_DEV, &f, RATE, 2) != 0,
              "B5 a 0-channel grant is REFUSED — a 0-channel byte count is 0, i.e. "
              "silently mute, which is the failure this library exists to remove");
        check(!audio_out_is_open(&out) && f.closes == 1,
              "B6 and the refused open closes the device rather than leaking it");

        fake_reset(&f);
        f.grant_bits = 8;
        check(audio_out_open(&out, &FAKE_DEV, &f, RATE, 2) == 0 &&
              audio_out_bits(&out) == 8,
              "B7 a non-16-bit grant is reported rather than swallowed — the hole "
              "audio.c:85 left and oss-mixer.cpp:147-152 already warned about");
        audio_out_close(&out);

        fake_reset(&f);
        f.fail_open = true;
        check(audio_out_open(&out, &FAKE_DEV, &f, RATE, 2) != 0 &&
              !audio_out_is_open(&out),
              "B8 a failed open leaves a struct every entry point reads as closed");
    }

    printf("\n=== C. the lead, in whole DEVICE PERIODS ===\n");
    {
        fake_reset(&f);
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        check(audio_out_period(&out) == 2048,
              "C1 the period is read off the device, not assumed");
        check(audio_out_lead(&out) == 3 * 2048,
              "C2 the lead is AUDIO_PUMP_LEAD_PERIODS whole periods (6144 frames, "
              "~139 ms at 44100) — NOT the 80 ms the ms constant asks for");
        check(audio_out_lead(&out) % audio_out_period(&out) == 0,
              "C3 and it lands on a period boundary, which is what a lead between "
              "two periods XRUNed for");
        audio_out_close(&out);

        /* ScummVM's configuration: 22050 mono, same 2048-frame period. */
        fake_reset(&f);
        f.grant_rate = 22050; f.grant_channels = 1; f.fb = AUDIO_BYTES_PER_SAMPLE;
        audio_out_open(&out, &FAKE_DEV, &f, 22050, 1);
        check(audio_out_lead(&out) == 3 * 2048,
              "C4 the same rule at 22050 mono gives 6144 frames = ~278 ms, which is "
              "within 2 ms of what oss-mixer.cpp already prefills and holds — one "
              "shared constant reproduces both clients");
        audio_out_close(&out);

        /* A device with a tiny ring: the lead may not exceed half of it. */
        fake_reset(&f);
        f.period = 256; f.ring = 1024;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        check(audio_out_lead(&out) == 512,
              "C5 a small ring caps the lead at half of it, so the stream is not "
              "asked for more cushion than the device can hold");
        audio_out_close(&out);
    }

    printf("\n=== D. mode 1: serviced, and it targets a LEAD ===\n");
    {
        /* A silent bus still writes.  This is the whole fix: an idle stream is a
         * stream transition, and a transition is the click. */
        fake_reset(&f);
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        fc.calls = 0;
        audio_out_set_fill(&out, fill_silent, &fc, "silent");
        f.len = 0;
        long n = audio_out_service(&out);
        check(n == audio_out_lead(&out),
              "D1 on a silent bus the service writes exactly the lead — never 0 "
              "bytes, because a stream allowed to go idle is a transition");
        check(f.len == n * f.fb && all_zero(f.got, 0, f.len),
              "D2 and what it wrote is silence, all of it");
        check(fc.calls == 1,
              "D3 the fill callback was still asked, once");

        /* ⚠️ Stale scratch must not leak, and the ORDER below is the entire test.
         * A loud fill claiming EVERY frame runs first, so the scratch is full of
         * non-zero samples; only then does a fill claim 100 of them and touch
         * nothing else, which is exactly `audio_mix_render()` on a silent bus.
         * Until 2026-08-18 this ran after three SILENT services, so the scratch
         * was already zero and the check passed with the `memset` deleted —
         * measured, by the sabotage sweep reporting 0 for that stanza. */
        fc.value = 7000; fc.produce = -1; fc.poison = false; fc.calls = 0;
        audio_out_set_fill(&out, fill_const, &fc, "loud");
        f.len = 0;
        n = audio_out_service(&out);
        check(n > 0 && count_nonzero_samples(f.got, f.len) == n * 2,
              "D4a a fill claiming every frame fills every frame — which is what "
              "leaves the scratch dirty for D4b to find");

        fc.value = 7000; fc.produce = 100; fc.poison = false; fc.calls = 0;
        audio_out_set_fill(&out, fill_const, &fc, "half");
        f.len = 0;
        n = audio_out_service(&out);
        check(n > 100 && count_nonzero_samples(f.got, f.len) == 100 * 2,
              "D4b a SHORT fill's remainder is silence — the library zeroes the "
              "buffer before every fill, because audio_mix_render() deliberately "
              "does not touch it on a silent bus");

        /* Never fills the free space. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = true;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        fc.value = 1000; fc.produce = -1; fc.calls = 0;
        audio_out_set_fill(&out, fill_const, &fc, "const");
        f.len = 0;
        n = audio_out_service(&out);
        check(n == 6144,
              "D5 an EMPTY 32768-frame ring gets the lead, not 743 ms of audio — "
              "filling the space is what puts the next sound three quarters of a "
              "second late");

        /* Already at the lead: nothing owed. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 0;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        long before = f.len;
        n = audio_out_service(&out);
        check(n == 0 && f.len == before,
              "D6 a queue already at the lead is written nothing at all, and 0 is "
              "a legitimate return rather than an error");

        /* Partial drain: exactly the shortfall. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 500;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        fc.value = 1000; fc.produce = -1; fc.calls = 0;
        audio_out_set_fill(&out, fill_const, &fc, "const");
        f.len = 0;
        n = audio_out_service(&out);
        check(n == 500,
              "D7 a queue 500 frames short of the lead is written exactly 500 "
              "frames — lead minus in_flight, not the free space");

        /* The over-report must not change what is written: it is a property of
         * the device's report, and the library deliberately does not subtract it
         * from in_flight. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 500; f.over_report = 2662;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        fc.calls = 0;
        audio_out_set_fill(&out, fill_const, &fc, "const");
        f.len = 0;
        n = audio_out_service(&out);
        check(n == 0,
              "D8 a device over-reporting its queue by 1.3 periods is BELIEVED for "
              "pacing — the slack is spent on the published service ceiling and the "
              "drain, never on writing deeper and adding onset latency");

        /* Starvation: a dry queue on a never-idle stream is always a fault. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = true;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        check(audio_out_starved(&out) == 0,
              "D9 a fresh stream has starved 0 times");
        audio_out_service(&out);
        audio_out_service(&out);
        check(audio_out_starved(&out) == 2,
              "D10 a queue found DRY counts a starve every time — one audible gap "
              "each, and the number that separates pacing from mixing");

        /* Lost frames: the device refuses and the fill has already advanced.
         * ⚠️ The byte budget is set AFTER the open, because a device that refuses
         * the PREFILL is a different case — and it is the one that hung this suite
         * the first time it was run, before the prefill policy was bounded. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = true;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        f.accept_upto = f.len + 4 * f.fb;
        fc.value = 1000; fc.produce = -1; fc.calls = 0;
        audio_out_set_fill(&out, fill_const, &fc, "const");
        long lost0 = audio_out_lost(&out);
        n = audio_out_service(&out);
        check(n == 4 && audio_out_lost(&out) - lost0 == (uint32_t)(6144 - 4),
              "D11 frames the device refused are COUNTED as lost — the fill has "
              "advanced past them, so they are gone rather than deferred");
        check(audio_out_misaligned(&out) == 0,
              "D12 and it stopped on a frame boundary: no partial frame in the "
              "device, so L and R are not swapped for the rest of the stream");

        /* And the prefill is bounded on the same device. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 0; f.accept_upto = 12;
        check(audio_out_open(&out, &FAKE_DEV, &f, RATE, 2) == 0,
              "D12b a device that will not take the PREFILL still returns from "
              "open — the one blocking call in the file is bounded, and an "
              "unbounded one hung this suite before it was");
        audio_out_close(&out);

        /* A chunking device that takes half a frame at a time still leaves whole
         * frames behind. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = true; f.chunk = 3;      /* not a multiple of 4-byte frames */
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        fc.value = 1000; fc.produce = -1; fc.calls = 0;
        audio_out_set_fill(&out, fill_const, &fc, "const");
        f.len = 0;
        n = audio_out_service(&out);
        check(f.len % f.fb == 0 && audio_out_misaligned(&out) == 0,
              "D13 a device taking 3 bytes at a time is left holding whole frames "
              "only — audio_write_frames() is still the only code that stops");

        /* ⚠️ A device that stalls MID-FRAME and never recovers is the one place the
         * "service() never sleeps" claim can be broken, and nothing tested it: the
         * sabotage sweep gave the non-zero-wait policy 0 failures.  6 bytes at a
         * 4-byte frame leaves half a frame in the device, and mid-frame the policy's
         * stop condition does NOT apply — `audio_write_frames()` waits
         * AUDIO_ALIGN_TRIES times whatever the caller asked for, which at a 1000 us
         * interval is 4 ms of sleep inside a render loop. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = true;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        f.accept_upto = f.len + 6;             /* 1.5 frames, then EAGAIN forever */
        fc.value = 1000; fc.produce = -1; fc.calls = 0;
        /* The prefill is the one call in the library allowed to sleep, so its waits
         * are not the measurement — zero the counters after the open, not before. */
        f.waits = 0; f.waits_slept = 0;
        audio_out_set_fill(&out, fill_const, &fc, "const");
        n = audio_out_service(&out);
        check(n == 1 && audio_out_misaligned(&out) == 1,
              "D13b a permanent MID-FRAME stall is REPORTED, not retried forever: "
              "one whole frame written, the half frame counted, and the loop bounded "
              "by AUDIO_ALIGN_TRIES — hanging the render loop is worse than the "
              "channel swap it is trying to avoid");
        check(f.waits > 0 && f.waits_slept == 0,
              "D13c and the realignment it just did SPUN rather than slept — the "
              "serviced policy's wait_us is 0, which is what makes \"service() never "
              "sleeps\" true rather than nearly true");

        /* The return value is the pacing signal. */
        audio_out_close(&out);
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 700;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        fc.value = 1000; fc.produce = -1; fc.calls = 0;
        audio_out_set_fill(&out, fill_const, &fc, "const");
        long a = audio_out_service(&out);
        f.drain_per_space = 1300;
        long b = audio_out_service(&out);
        check(a == 700 && b == 1300 && a != b,
              "D14 service() returns a VARIABLE frame count, which is why a caller "
              "advancing a fixed per-buffer deadline has two pacing models and "
              "neither bounds the queue");
        audio_out_close(&out);

        fake_reset(&f);
        f.fail_space = true;
        check(audio_out_open(&out, &FAKE_DEV, &f, RATE, 2) == 0,
              "D15 a device whose geometry cannot be read still opens");
        check(audio_out_service(&out) == -1 && audio_out_refused(&out) >= 1,
              "D16 but a service against it is refused and counted, not silently "
              "written into nowhere");
        audio_out_close(&out);
    }

    printf("\n=== E. one fill callback, and the refusal stays LOUD ===\n");
    {
        fake_reset(&f);
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        check(audio_out_fill_owner(&out) == NULL,
              "E1 a fresh stream has no fill owner");
        fc.value = 1000; fc.produce = -1; fc.calls = 0;
        audio_out_set_fill(&out, fill_const, &fc, "mixbus");
        check(audio_out_fill_owner(&out) != NULL &&
              strcmp(audio_out_fill_owner(&out), "mixbus") == 0,
              "E2 the owner is named, so the layer above can keep its refusal loud "
              "instead of letting a swap go quiet");

        FillCtl fc2; memset(&fc2, 0, sizeof(fc2));
        fc2.value = 2000; fc2.produce = -1;
        audio_out_set_fill(&out, fill_const, &fc2, "theremin");
        long t = f.transitions;
        f.len = 0;
        audio_out_service(&out);
        check(fc.calls == 0 && fc2.calls == 1 && f.transitions == t,
              "E3 a swap replaces the writer with no reset and no reconfigure — "
              "which is what lets the theremin and the mix bus share one stream");

        long r0 = audio_out_refused(&out);
        audio_render_tone(RATE, 880, AUDIO_PEAK, mono, 1000);
        check(audio_out_write(&out, mono, 1000) == -1 &&
              audio_out_refused(&out) == r0 + 1,
              "E4 a synchronous write against an installed callback is REFUSED and "
              "counted — the two-writer case the one callback exists to prevent");

        audio_out_set_fill(&out, NULL, NULL, NULL);
        check(audio_out_fill_owner(&out) == NULL,
              "E5 removing the callback clears the owner");
        f.len = 0;
        check(audio_out_write(&out, mono, 1000) == 1000,
              "E6 and with nobody servicing, the synchronous write goes through");
        audio_out_close(&out);
    }

    printf("\n=== F. attenuation is a SHIFT, swept over every int16 ===\n");
    {
        for (int shift = 0; shift <= 1; shift++) {
            fake_reset(&f);
            f.period = 64; f.ring = 4096; f.drain_all = true;
            audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
            audio_out_set_shift(&out, shift);
            memset(&fc, 0, sizeof(fc));
            audio_out_set_fill(&out, fill_ramp, &fc, "ramp");

            long mismatches = 0, seen = 0;
            while (seen < 65536) {
                f.len = 0;
                long got = audio_out_service(&out);
                if (got <= 0) break;
                const int16_t *s = (const int16_t *)f.got;
                for (long i = 0; i < got * 2; i++) {
                    int16_t src = (int16_t)(int32_t)(-32768 + ((seen + i) & 0xFFFF));
                    int16_t exp = (int16_t)(src >> shift);
                    if (s[i] != exp) mismatches++;
                }
                seen += got * 2;
            }
            if (shift == 0)
                check(seen >= 65536 && mismatches == 0,
                      "F1 shift 0 is the IDENTITY over all 65536 int16 values — "
                      "which is what keeps loudness unchanged in this change");
            else
                check(seen >= 65536 && mismatches == 0,
                      "F2 shift 1 matches oss-mixer.cpp:241-244's `>>1` for all "
                      "65536 int16 values, so ScummVM stays bit-identical");
            audio_out_close(&out);
        }

        /* The two values a rounding multiply gets wrong. */
        check((int16_t)(-1 >> 1) == -1 && (int16_t)(-32768 >> 1) == -16384,
              "F3 the two values that separate a shift from a gain multiply: "
              "-1 >> 1 == -1 (a multiply rounds it to 0) and -32768 >> 1 == -16384");

        /* Applied post-fill, so the fill never sees it. */
        fake_reset(&f);
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        audio_out_set_shift(&out, 1);
        fc.value = 8000; fc.produce = -1; fc.calls = 0; fc.poison = false;
        audio_out_set_fill(&out, fill_const, &fc, "const");
        f.len = 0;
        long n = audio_out_service(&out);
        const int16_t *s = (const int16_t *)f.got;
        check(n > 0 && s[0] == 4000 && s[n * 2 - 1] == 4000,
              "F4 the attenuation is a library stage AFTER the fill and immediately "
              "before the write, so a client renders at its own peak");
        audio_out_close(&out);
    }

    printf("\n=== G. mode 2: synchronous, for the two tabs with NO render loop ===\n");
    {
        /* hardware_config.c:77-85 and device_tools.c:484-489: init, tone, usleep,
         * tone, close.  Nothing would ever service them. */
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 2048;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        long frames = audio_frames_for_ms(RATE, 200);
        audio_render_tone(RATE, 880, AUDIO_PEAK, mono, frames);
        f.len = 0;
        long n = audio_out_write(&out, mono, frames);
        check(n == frames && f.len == frames * f.fb,
              "G1 a whole 200 ms tone reaches the device with NO service loop — "
              "otherwise both Settings speaker tests are silently mute");
        check(f.transitions == 1,
              "G2 and it cost no stream transition: the tone is written into the "
              "same never-reset stream");
        audio_out_close(&out);

        /* Longer than the ring: the bound is derived from the tone, so it is not
         * truncated. */
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 4096;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        frames = audio_frames_for_ms(RATE, 2000);        /* 2 s > the 743 ms ring */
        audio_render_tone(RATE, 440, AUDIO_PEAK, mono, frames);
        f.len = 0;
        n = audio_out_write(&out, mono, frames);
        check(n == frames,
              "G3 a tone LONGER than the ring is not truncated — mode 2's wait "
              "bound is derived from the buffer's own duration, not a constant");
        audio_out_close(&out);

        /* A wedged device: bounded, not hanging. */
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 0;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        f.accept_upto = f.len + 8;
        frames = audio_frames_for_ms(RATE, 100);
        audio_render_tone(RATE, 880, AUDIO_PEAK, mono, frames);
        n = audio_out_write(&out, mono, frames);
        check(n == 2 && audio_out_misaligned(&out) == 0,
              "G4 a permanently full device is GIVEN UP ON at a frame boundary — "
              "bounded, because the alternative to giving up is hanging a UI");
        audio_out_close(&out);

        fake_reset(&f);
        check(audio_out_write(&out, mono, 100) == -1,
              "G5 a synchronous write on a closed stream is refused, not a crash");
    }

    printf("\n=== H. close DRAINS, bounded ===\n");
    {
        /* A device that drains: the tail gets out. */
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 2048;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        long frames = audio_frames_for_ms(RATE, 200);
        audio_render_tone(RATE, 880, AUDIO_PEAK, mono, frames);
        audio_out_write(&out, mono, frames);
        long q_before = f.qbytes;
        audio_out_close(&out);
        long slack_bytes = 2048 * 13 / 10 * f.fb;
        check(q_before > slack_bytes && f.qbytes <= slack_bytes &&
              audio_out_drain_waits(&out) > 0,
              "H1 close waits for the queued tail to play out, down to the slack — "
              "without it, exiting discards most of what the Settings tab just "
              "played");
        check(f.closes == 1, "H2 and then it closes the device exactly once");

        /* A device that NEVER drains: bounded by the ring, not forever. */
        fake_reset(&f);
        f.drain_all = false; f.drain_per_space = 0;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        audio_render_tone(RATE, 880, AUDIO_PEAK, mono, 4096);
        audio_out_write(&out, mono, 4096);
        audio_out_close(&out);
        long ring_ms = audio_ms_for_frames(RATE, 32768 + 2048);
        check(audio_out_drain_waits(&out) > 0 &&
              (long)audio_out_drain_waits(&out) <=
                  (ring_ms * 1000) / AUDIO_OUT_DRAIN_WAIT_US + 1,
              "H3 a device that never drains costs at most one ring's duration — "
              "the queue cannot be longer than the ring, so anything past that is "
              "a wedged device rather than a tail");

        /* The stop condition is the slack, not zero — an over-reporting device
         * never reads empty, so a wait-for-zero loop would always run to its
         * bound at every process exit. */
        fake_reset(&f);
        f.drain_all = true; f.over_report = 2662;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        audio_out_close(&out);
        check(audio_out_drain_waits(&out) == 0,
              "H4 an EMPTY but over-reporting device drains in zero waits: the stop "
              "condition is the 1.3-period slack, because in_flight never reads 0");

        fake_reset(&f);
        memset(&out, 0, sizeof(out));
        audio_out_close(&out);
        check(f.closes == 0,
              "H5 closing a struct that was never opened touches no device");
    }

    printf("\n=== I. the published service ceiling, from REAL audio ===\n");
    {
        fake_reset(&f);
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        long us = audio_out_service_interval_us(&out);
        check(us > 33333,
              "I1 the ceiling is above FRAME_DELAY_ACTIVE_US (33 ms), which the "
              "sweep measured safe with about a period of margin");
        check(us < 66000,
              "I2 and below the 66 ms that survived with only ~11 ms of true "
              "content left — the published figure sits inside the region that was "
              "seen working, not at its edge");
        check(us < 100000,
              "I3 and far below FRAME_DELAY_IDLE_US (100 ms), which starves ~2.5x/s "
              "ON ITS OWN — this is why audio_pump_active() must stay in the "
              "frame-pacing decision");
        long nominal_us = audio_ms_for_frames(RATE, audio_out_lead(&out)) * 1000;
        check(nominal_us > 130000 && us < nominal_us / 2,
              "I4 the nominal lead is ~139 ms and the ceiling is less than half of "
              "it — a nominal number is never published as if it were audio");
        audio_out_close(&out);

        fake_reset(&f);
        f.fail_space = true;
        audio_out_open(&out, &FAKE_DEV, &f, RATE, 2);
        check(audio_out_service_interval_us(&out) == 0,
              "I5 with no geometry measured the ceiling is 0, not a constant — "
              "\"not measured\" and \"55 ms\" are different claims");
        audio_out_close(&out);
    }

    printf("\n=== J. one stream per process ===\n");
    {
        static Fake g;
        AudioOut a, b;
        fake_reset(&f);
        fake_reset(&g);
        check(audio_out_open(&a, &FAKE_DEV, &f, RATE, 2) == 0,
              "J1 the first stream opens");
        check(audio_out_open(&b, &FAKE_DEV, &g, RATE, 2) != 0 && g.opens == 0,
              "J2 a second CONCURRENT open is refused before it reaches the device "
              "— the driver answers EBUSY, and a named refusal beats a half-done "
              "init");
        audio_out_close(&a);
        check(audio_out_open(&b, &FAKE_DEV, &g, RATE, 2) == 0,
              "J3 but SEQUENTIAL open/close pairs are fine, which is what "
              "device_tools' two short-lived Audio objects rely on");
        audio_out_close(&b);
    }

    printf("\n%s  %d checks, %d failure(s)\n",
           failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
