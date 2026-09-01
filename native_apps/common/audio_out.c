#include "audio_out.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── The device half, and nothing else ───────────────────────────────────────
 *
 * Every frame count, byte count, envelope and write decision in here comes from
 * `audio_gen.c`, which has no device in it and is host-tested.  What is left is
 * the four things that genuinely need a device — configure-and-read-back, how
 * much room the queue has, the write, and the drain — behind a vtable so a host
 * regression can drive all of it with no fd.
 *
 * ⚠️ **There is no ring-reset ioctl anywhere below this line, and that is the
 * fix.** The steady-state count must be exactly zero, which is a grep-checkable
 * number rather than a rule to remember; the pattern is in
 * `tests/audio_out_test.c`'s header, written so that it cannot match its own
 * documentation.
 */

/* ── Write policies: two, over the one loop in audio_gen.c ───────────────────
 *
 * `audio_write_frames()` is still the only code that decides when to stop, and it
 * stops on a frame boundary or reports that it could not.  These are POLICIES over
 * it, not loops — the rule that four hand-rolled EAGAIN loops were collapsed into
 * (`native_apps/CLAUDE.md`), and it now has to hold across this library's clients
 * as well as inside one directory, or an emulator port satisfies it while adding
 * two loops in a second file.
 */

/** ⚠️ **The prefill BLOCKS and must still be BOUNDED.** It is the one call in
 *  this file allowed to sleep, and an unlimited `max_waits` against a device that
 *  will not take the prefill hangs `audio_out_open()` forever — measured, by
 *  `tests/audio_out_test.c` group D hanging the suite the first time it drove a
 *  device with a byte budget.  So its bound is derived per call from the length
 *  of what it is writing, exactly like mode 2's: see `blocking_policy()`. */

/** ⚠️ **The serviced policy's wait is 0, and that is load-bearing.** It is called
 *  from a render loop or an audio thread that believes the call is free, so it may
 *  not block — and `stop_on_again` alone does not achieve that: mid-frame
 *  `audio_write_frames()` ignores the stop condition and waits up to
 *  AUDIO_ALIGN_TRIES times, which at a 1000 µs interval is 4 ms of sleep inside a
 *  function documented as never sleeping.  `dsp_wait`-style waits already no-op at
 *  `usec <= 0`, so 0 makes the realignment a spin of four retries instead. */
static const AudioWritePolicy AOPOL_SERVICE = { 0, 0, true };

/* Mode 2's policy is built per call, because its bound is derived from the
 * length of what it is being asked to write — see `sync_policy()`. */

/* ── One per process ─────────────────────────────────────────────────────────
 *
 * ⚠️ A second concurrent open is refused *Device or resource busy* by the driver
 * itself (measured, ../SYSTEM_ANALYSIS.md#34-audio).  Catching it here turns a
 * confusing half-finished init into a named refusal, and makes the rule the
 * header states testable on the host.  Sequential open/close pairs are fine.
 */
static int g_live = 0;

/* ── Small helpers ──────────────────────────────────────────────────────── */

static ssize_t out_sink_write(void *ctx, const void *buf, size_t nbytes, bool *again)
{
    AudioOut *out = (AudioOut *)ctx;
    return out->dev->write(out->dev_ctx, buf, nbytes, again);
}

static void out_sink_wait(void *ctx, int usec)
{
    AudioOut *out = (AudioOut *)ctx;
    if (out->dev->wait) out->dev->wait(out->dev_ctx, usec);
}

static AudioSink sink_of(AudioOut *out)
{
    AudioSink s;
    s.write = out_sink_write;
    s.wait  = out_sink_wait;
    s.ctx   = out;
    return s;
}

/** Interleaved device scratch, grown once and kept.  A service runs every frame,
 *  so a malloc/free pair per call is the one allocation worth removing. */
static int16_t *scratch(AudioOut *out, long frames)
{
    if (frames <= 0 || out->channels <= 0) return NULL;
    if (out->buf && out->buf_frames >= frames) return out->buf;

    size_t n = (size_t)frames * (size_t)out->channels * sizeof(int16_t);
    int16_t *nb = (int16_t *)realloc(out->buf, n);
    if (!nb) return NULL;
    out->buf        = nb;
    out->buf_frames = frames;
    return nb;
}

/** How many frames of the in-flight report are the device over-stating.  Used
 *  only where a nominal figure would otherwise be presented as real audio: the
 *  service-interval ceiling and the drain's stop condition. */
static long ospace_slack(long period_frames)
{
    if (period_frames <= 0) return 0;
    return period_frames * AUDIO_OUT_OSPACE_SLACK_NUM / AUDIO_OUT_OSPACE_SLACK_DEN;
}

/**
 * Waits a blocking policy needs to see `frames` all the way out.
 *
 * Derived rather than constant: the device drains at hardware rate, so a buffer
 * longer than the ring needs about its own duration in waits.  A fixed bound would
 * truncate a long tone, and an unlimited one would hang a UI on a wedged device —
 * `AUDIO_MAX_TONE_MS` is 30 s, so both failures are reachable from one call site.
 */
static int derive_max_waits(int rate, long frames, int wait_us)
{
    long per_wait_ms = wait_us / 1000;
    if (per_wait_ms < 1) per_wait_ms = 1;

    long ms = audio_ms_for_frames(rate, frames);
    if (ms < 0) ms = 0;

    long n = ms / per_wait_ms + AUDIO_OUT_SYNC_WAIT_FLOOR;
    if (n > 100000) n = 100000;          /* a wedged device, not a long tone */
    return (int)n;
}

static AudioWritePolicy blocking_policy(int rate, long frames, int wait_us)
{
    AudioWritePolicy p;
    p.wait_us       = wait_us;
    p.max_waits     = derive_max_waits(rate, frames, wait_us);
    p.stop_on_again = false;
    return p;
}

/** The lead is a MEASUREMENT taken from the device's own period, so it is
 *  re-derived from every geometry read rather than cached from the request. */
static void derive_geometry(AudioOut *out, const AudioOutSpace *sp)
{
    out->period_frames = sp->period_frames;
    out->ring_frames   = sp->ring_frames;
    out->lead_frames   = audio_pump_lead_frames(
                             audio_frames_for_ms(out->rate, AUDIO_PUMP_LEAD_MS),
                             sp->period_frames, AUDIO_PUMP_LEAD_PERIODS,
                             sp->ring_frames);
}

/** The device attenuation stage — see audio_attenuate() in audio_gen.c, which is
 *  the one implementation of it and the reason a shift rather than a multiply. */
static void attenuate(int16_t *buf, long samples, int shift)
{
    audio_attenuate(buf, samples, shift);
}

/** One write of `frames` interleaved frames under `pol`, with the two faults
 *  that are never ignorable counted rather than returned. */
static long push(AudioOut *out, const int16_t *buf, long frames,
                 const AudioWritePolicy *pol)
{
    AudioSink sink = sink_of(out);
    AudioWriteResult res;
    audio_write_frames(&sink, buf, frames, out->channels, pol, &res);

    if (res.misaligned) {
        out->misaligned++;
        fprintf(stderr, "audio_out: a partial frame is in the device "
                        "(%ld of %ld bytes) — channels may be swapped\n",
                res.bytes_written, audio_bytes_for_frames(frames, out->channels));
    }
    if (res.sink_error) out->sink_errors++;

    out->last_frames = res.frames_written;
    if (res.frames_written < frames)
        out->lost += (uint32_t)(frames - res.frames_written);
    return res.frames_written;
}

/* ── Open ───────────────────────────────────────────────────────────────── */

int audio_out_open(AudioOut *out, const AudioOutDev *dev, void *dev_ctx,
                   int rate_req, int channels_req)
{
    if (!out || !dev || !dev->open || !dev->space || !dev->write || !dev->close)
        return -1;

    /* Zero first, so a caller that ignores the return value still holds a struct
     * every entry point reads as closed. */
    memset(out, 0, sizeof(*out));
    out->oss_fd = -1;

    if (g_live > 0) {
        fprintf(stderr, "audio_out: refused — one AudioOut is already open in "
                        "this process (a second concurrent open is EBUSY)\n");
        return -1;
    }

    out->dev     = dev;
    out->dev_ctx = dev_ctx;

    int rate = 0, bits = 0, channels = 0;
    if (dev->open(dev_ctx, rate_req, channels_req, &rate, &bits, &channels) != 0) {
        out->dev = NULL;
        return -1;
    }

    /* ⚠️ The GRANT is the only number the byte arithmetic may use.  A struct
     * filled from the request instead is the failure mode this library exists to
     * remove: a 0-channel byte count is 0, i.e. silently mute. */
    out->rate     = (rate > 0) ? rate : rate_req;
    out->bits     = bits;
    out->channels = channels;

    out->frame_bytes = audio_frame_bytes(out->channels);
    if (out->frame_bytes <= 0 || out->rate <= 0) {
        fprintf(stderr, "audio_out: device granted rate=%d channels=%d — unusable\n",
                out->rate, out->channels);
        dev->close(dev_ctx);
        out->dev = NULL;
        return -1;
    }

    /* Reading the width back closes a hole `audio.c` had and `oss-mixer.cpp`
     * already warned about: a device that quietly granted another width produces
     * noise with no diagnostic anywhere.  Warned, not refused — 16 is what every
     * measured configuration grants, so an unexpected width is a surprise to
     * report rather than a reason to leave the panel silent. */
    if (out->bits != AUDIO_BYTES_PER_SAMPLE * 8) {
        out->bits_warned = true;
        fprintf(stderr, "audio_out: device granted %d-bit samples, expected %d — "
                        "the stream will be written as S16_LE anyway\n",
                out->bits, AUDIO_BYTES_PER_SAMPLE * 8);
    }

    out->is_open = true;
    g_live++;

    /* Geometry, then the prefill.  Both need the grant, which is why neither can
     * be done before this point. */
    AudioOutSpace sp;
    memset(&sp, 0, sizeof(sp));
    if (dev->space(dev_ctx, out->frame_bytes, &sp) == 0)
        derive_geometry(out, &sp);

    if (out->lead_frames > 0) {
        int16_t *buf = scratch(out, out->lead_frames);
        if (buf) {
            memset(buf, 0, (size_t)out->lead_frames * (size_t)out->channels
                            * sizeof(int16_t));
            AudioWritePolicy pol = blocking_policy(out->rate, out->lead_frames,
                                                   AUDIO_OUT_PREFILL_WAIT_US);
            push(out, buf, out->lead_frames, &pol);
        }
        /* ⚠️ The prefill's frames are not "lost" audio — they are the silence the
         * stream is meant to start with — so do not let push()'s accounting read
         * as a defect on the very first write. */
        out->lost        = 0;
        out->last_frames = 0;
    }

    return 0;
}

/* ── Close, with a bounded drain ────────────────────────────────────────── */

void audio_out_close(AudioOut *out)
{
    if (!out) return;

    if (out->is_open && out->dev) {
        /* ⚠️ Drain, or the queued tail is discarded at exit — which on the two
         * Settings tabs is most of the tone they just played, because nothing
         * services them and the whole tone is still inside the device.
         *
         * The stop condition is the free-space slack rather than zero: the
         * device's in-flight figure over-reports by ~1.3 periods, so it never
         * reaches zero and a wait-for-zero loop would always run to its bound.
         * The bound itself is the ring's own duration plus a period — the queue
         * cannot be longer than the ring, so anything past that is a wedged
         * device, not a tail. */
        long budget_ms = audio_ms_for_frames(out->rate,
                                             out->ring_frames + out->period_frames);
        long max_polls = (budget_ms > 0)
                         ? (budget_ms * 1000) / AUDIO_OUT_DRAIN_WAIT_US + 1
                         : 0;
        long floor_frames = ospace_slack(out->period_frames);

        out->drain_waits = 0;
        for (long i = 0; i < max_polls; i++) {
            AudioOutSpace sp;
            memset(&sp, 0, sizeof(sp));
            if (out->dev->space(out->dev_ctx, out->frame_bytes, &sp) != 0) break;
            if (sp.in_flight <= floor_frames) break;
            out->drain_waits++;
            if (out->dev->wait) out->dev->wait(out->dev_ctx, AUDIO_OUT_DRAIN_WAIT_US);
        }

        out->dev->close(out->dev_ctx);
        g_live--;
        if (g_live < 0) g_live = 0;
    }

    free(out->buf);
    out->buf        = NULL;
    out->buf_frames = 0;
    out->is_open    = false;
    out->fill       = NULL;
    out->fill_ctx   = NULL;
    out->fill_owner = NULL;
    out->dev        = NULL;
    out->dev_ctx    = NULL;
    out->oss_fd     = -1;
}

/* ── The one fill callback ──────────────────────────────────────────────── */

int audio_out_set_fill(AudioOut *out, AudioOutFill fill, void *ctx,
                       const char *owner)
{
    if (!out || !out->is_open) return -1;
    out->fill       = fill;
    out->fill_ctx   = ctx;
    out->fill_owner = fill ? (owner ? owner : "unnamed") : NULL;
    return 0;
}

const char *audio_out_fill_owner(const AudioOut *out)
{
    return (out && out->fill) ? out->fill_owner : NULL;
}

void audio_out_set_shift(AudioOut *out, int shift)
{
    if (!out) return;
    if (shift < 0)  shift = 0;
    if (shift > 15) shift = 15;
    out->shift = shift;
}

/* ── Mode 1: serviced ───────────────────────────────────────────────────── */

long audio_out_service(AudioOut *out)
{
    if (!out || !out->is_open || !out->dev) return -1;

    AudioOutSpace sp;
    memset(&sp, 0, sizeof(sp));
    if (out->dev->space(out->dev_ctx, out->frame_bytes, &sp) != 0) {
        out->refused++;
        return -1;
    }
    out->services++;
    derive_geometry(out, &sp);

    /* ⚠️ On a stream that is never allowed to go idle, a dry queue is ALWAYS a
     * fault — one audible gap each — so this is unconditional.  It is also the
     * number that separates "the mixer is wrong" from "the loop that feeds it was
     * too slow", which is a distinction no derived figure can make. */
    if (sp.in_flight <= 0) out->starved++;

    long want = audio_pump_frames(out->lead_frames, sp.in_flight, sp.space,
                                  out->lead_frames);
    if (want <= 0) {
        out->last_frames = 0;
        return 0;
    }

    int16_t *buf = scratch(out, want);
    if (!buf) {
        out->refused++;
        return -1;
    }

    /* ⚠️ Zeroed before every fill.  A short fill is legal and `audio_mix_render()`
     * deliberately does not touch the buffer on a silent bus, so without this the
     * previous service's samples would be re-written as this one's tail. */
    long samples = want * (long)out->channels;
    memset(buf, 0, (size_t)samples * sizeof(int16_t));

    if (out->fill)
        out->fill(out->fill_ctx, buf, want, out->channels);

    attenuate(buf, samples, out->shift);

    return push(out, buf, want, &AOPOL_SERVICE);
}

/* ── Mode 2: synchronous ────────────────────────────────────────────────── */

long audio_out_write(AudioOut *out, const int16_t *mono, long frames)
{
    if (!out || !out->is_open || !out->dev) return -1;
    if (!mono || frames <= 0) return -1;

    /* ⚠️ Refused LOUDLY.  A quiet refusal here is the failure the one-callback
     * design exists to prevent: two writers interleaving frames into a stream
     * that has no mono path underneath to absorb a swap. */
    if (out->fill) {
        out->refused++;
        fprintf(stderr, "audio_out: synchronous write refused — \"%s\" owns the "
                        "fill callback (remove it first)\n",
                out->fill_owner ? out->fill_owner : "a client");
        return -1;
    }

    int16_t *buf = scratch(out, frames);
    if (!buf) return -1;

    long bytes = audio_bytes_for_frames(frames, out->channels);
    if (bytes <= 0 || audio_interleave(mono, frames, out->channels, buf) != bytes)
        return -1;

    attenuate(buf, frames * (long)out->channels, out->shift);

    AudioWritePolicy pol = blocking_policy(out->rate, frames, AUDIO_OUT_SYNC_WAIT_US);
    return push(out, buf, frames, &pol);
}

/* ── The service ceiling ────────────────────────────────────────────────── */

long audio_out_service_interval_us(const AudioOut *out)
{
    if (!out || out->lead_frames <= 0 || out->rate <= 0) return 0;

    /* Real audio, not the nominal lead: subtract what the device over-states. */
    long real_frames = out->lead_frames - ospace_slack(out->period_frames);
    if (real_frames <= 0) return 0;

    /* Half a period of margin.  The measured sweep left ~11 ms at a 66 ms
     * interval, so the margin is what keeps the published figure inside the
     * region that was seen working rather than at its edge. */
    long usable = real_frames - out->period_frames / 2;
    if (usable <= 0) usable = real_frames;

    long ms = audio_ms_for_frames(out->rate, usable);
    return (ms > 0) ? ms * 1000 : 0;
}

/* ── Accessors ──────────────────────────────────────────────────────────── */

int  audio_out_rate(const AudioOut *out)        { return out ? out->rate     : 0; }
int  audio_out_channels(const AudioOut *out)    { return out ? out->channels : 0; }
int  audio_out_bits(const AudioOut *out)        { return out ? out->bits     : 0; }
bool audio_out_is_open(const AudioOut *out)     { return out ? out->is_open  : false; }

long audio_out_lead(const AudioOut *out)        { return out ? out->lead_frames   : 0; }
long audio_out_period(const AudioOut *out)      { return out ? out->period_frames : 0; }
long audio_out_last_frames(const AudioOut *out) { return out ? out->last_frames   : 0; }

uint32_t audio_out_starved(const AudioOut *out)     { return out ? out->starved     : 0; }
uint32_t audio_out_lost(const AudioOut *out)        { return out ? out->lost        : 0; }
uint32_t audio_out_misaligned(const AudioOut *out)  { return out ? out->misaligned  : 0; }
uint32_t audio_out_sink_errors(const AudioOut *out) { return out ? out->sink_errors : 0; }
uint32_t audio_out_refused(const AudioOut *out)     { return out ? out->refused     : 0; }
uint32_t audio_out_services(const AudioOut *out)    { return out ? out->services    : 0; }
uint32_t audio_out_drain_waits(const AudioOut *out) { return out ? out->drain_waits : 0; }

/* ── The OSS backend ─────────────────────────────────────────────────────────
 *
 * The only device code below this header.  It is compiled out where the OSS
 * headers do not exist, so the host regression links this file with no device in
 * it at all — the same split `audio_gen.c` already has, one level down.
 */

#if defined(__has_include)
#  if __has_include(<sys/soundcard.h>)
#    define AUDIO_OUT_HAVE_OSS 1
#  endif
#endif

#ifdef AUDIO_OUT_HAVE_OSS

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#define GPIO12_DIRECTION  "/sys/class/gpio/gpio12/direction"
#define GPIO12_VALUE      "/sys/class/gpio/gpio12/value"
#define DSP_DEVICE        "/dev/dsp"

/** Drive GPIO12 HIGH to enable the on-board speaker amplifier (SPKR1).  Done
 *  here rather than by the caller so it works from a dev shell before
 *  `/etc/init.d/audio-enable` has run. */
static void enable_amp(void)
{
    FILE *f;
    f = fopen(GPIO12_DIRECTION, "w");
    if (f) { fputs("out", f); fclose(f); }
    f = fopen(GPIO12_VALUE, "w");
    if (f) { fputs("1",   f); fclose(f); }
}

static int oss_open(void *ctx, int rate_req, int channels_req,
                    int *rate_granted, int *bits_granted, int *channels_granted)
{
    int *fdp = (int *)ctx;

    enable_amp();

    /* ⚠️ O_NONBLOCK is not an optimisation.  A blocking write() on this device
     * stalls for a full hardware period once the queue fills, which turns every
     * subsequent sound into a late one; the write policies above follow the queue
     * at real-time pace instead. */
    *fdp = open(DSP_DEVICE, O_WRONLY | O_NONBLOCK);
    if (*fdp < 0) {
        perror("audio_out: cannot open " DSP_DEVICE);
        return -1;
    }

    /* ⚠️ SPEED → FMT → CHANNELS, then read all three back.  On this shim SPEED can
     * reset FMT and CHANNELS, FMT can reset SPEED, and the values the set-ioctls
     * write back do not necessarily describe the device — only the read-only
     * ioctls do (../SYSTEM_ANALYSIS.md#34-audio).
     *
     * ⚠️ SNDCTL_DSP_CHANNELS, never the deprecated STEREO ioctl: both grant
     * identically here (measured), and STEREO cannot express a request that is
     * not 1 or 2 channels. */
    int val = rate_req;
    ioctl(*fdp, SNDCTL_DSP_SPEED, &val);

    val = AFMT_S16_LE;
    ioctl(*fdp, SNDCTL_DSP_SETFMT, &val);

    val = channels_req;
    ioctl(*fdp, SNDCTL_DSP_CHANNELS, &val);

    int rate = 0, bits = 0, channels = 0;
    ioctl(*fdp, SOUND_PCM_READ_RATE,     &rate);
    ioctl(*fdp, SOUND_PCM_READ_BITS,     &bits);
    ioctl(*fdp, SOUND_PCM_READ_CHANNELS, &channels);

    /* A failed read-back leaves 0, and a 0-channel byte count is 0 — silently
     * mute.  Fall back to the request and say so; `hw:0,0` is stereo-only, so a
     * request is at least a number somebody chose. */
    if (rate <= 0) {
        fprintf(stderr, "audio_out: SOUND_PCM_READ_RATE gave %d (errno=%d) — "
                        "using the requested %d\n", rate, errno, rate_req);
        rate = rate_req;
    }
    if (channels <= 0) {
        fprintf(stderr, "audio_out: SOUND_PCM_READ_CHANNELS gave %d (errno=%d) — "
                        "using the requested %d\n", channels, errno, channels_req);
        channels = channels_req;
    }

    *rate_granted     = rate;
    *bits_granted     = bits;
    *channels_granted = channels;

    fprintf(stderr, "audio_out: %s open, granted %d Hz %d-bit %d ch "
                    "(requested %d Hz %d ch)\n",
            DSP_DEVICE, rate, bits, channels, rate_req, channels_req);
    return 0;
}

/* ⚠️ No SETFRAGMENT.  Constraining the ring is what removed the jitter buffer the
 * pacing depends on; the shim grants 2048 frames per period and 16 periods for
 * every rate and channel count measured, i.e. 743 ms at 44100 — NOT the
 * ~506 ms this repo believed for months. */
static int oss_space(void *ctx, int frame_bytes, AudioOutSpace *sp)
{
    int *fdp = (int *)ctx;
    audio_buf_info info;

    if (frame_bytes <= 0) return -1;
    if (ioctl(*fdp, SNDCTL_DSP_GETOSPACE, &info) < 0) return -1;

    long total = (long)info.fragstotal * (long)info.fragsize;
    sp->period_frames = (long)info.fragsize / frame_bytes;
    sp->ring_frames   = total / frame_bytes;
    sp->space         = (long)info.bytes / frame_bytes;
    sp->in_flight     = (total - (long)info.bytes) / frame_bytes;
    return 0;
}

static ssize_t oss_write(void *ctx, const void *buf, size_t nbytes, bool *again)
{
    int *fdp = (int *)ctx;
    ssize_t r = write(*fdp, buf, nbytes);
    if (r < 0 && errno == EAGAIN) *again = true;
    return r;
}

static void oss_wait(void *ctx, int usec)
{
    (void)ctx;
    if (usec > 0) usleep((useconds_t)usec);
}

static void oss_close(void *ctx)
{
    int *fdp = (int *)ctx;
    if (*fdp >= 0) { close(*fdp); *fdp = -1; }
}

static const AudioOutDev OSS_DEV = {
    oss_open, oss_space, oss_write, oss_wait, oss_close
};

int audio_out_open_oss(AudioOut *out, int rate_req, int channels_req)
{
    if (!out) return -1;
    /* The fd lives in the AudioOut so the vtable needs no allocation and no
     * static state — one per process is a rule about the DEVICE, not a reason to
     * keep the fd in a global. */
    int rc = audio_out_open(out, &OSS_DEV, &out->oss_fd, rate_req, channels_req);
    if (rc != 0) out->oss_fd = -1;   /* the memset left it 0, which reads as an fd */
    return rc;
}

#else  /* no OSS headers: the host */

int audio_out_open_oss(AudioOut *out, int rate_req, int channels_req)
{
    (void)rate_req; (void)channels_req;
    if (out) { memset(out, 0, sizeof(*out)); out->oss_fd = -1; }
    fprintf(stderr, "audio_out: built without OSS support\n");
    return -1;
}

#endif /* AUDIO_OUT_HAVE_OSS */
