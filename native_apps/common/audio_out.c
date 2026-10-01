#include "audio_out.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

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
    ssize_t r = out->dev->write(out->dev_ctx, buf, nbytes, again);
    /* ⚠️ Read errno HERE, immediately after the backend returned: push() only
     * learns "sink_error" after audio_write_frames() has possibly made more calls
     * and push() itself has printed, and either can overwrite it.  One check in
     * the generic layer covers every backend, including the host test's fake. */
    if (r < 0 && !*again && errno == ENODEV) out->device_lost = true;
    return r;
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
        /* A gone device usually fails HERE first — the free-space query precedes
         * every write — and a service that never reaches a write would otherwise
         * never learn the device is lost. */
        if (errno == ENODEV) out->device_lost = true;
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
bool audio_out_device_lost(const AudioOut *out)
{
    return out && out->is_open && out->device_lost;
}

/* ── What a failed ALSA call means ───────────────────────────────────────────
 * Pure, and outside every guard, so the host test reaches the unplug branch
 * with no libasound.  Accepts either sign; ALSA hands back negative errnos. */
AudioOutErr audio_out_alsa_classify(int err)
{
    if (err < 0) err = -err;
    switch (err) {
    case EAGAIN:   return AO_ERR_AGAIN;
    case EPIPE:    return AO_ERR_XRUN;
    case ESTRPIPE: return AO_ERR_SUSPEND;
    /* EBADFD is the state a PCM is left in once its card has gone — every call
     * after the first ENODEV answers it, so it is the same "reopen" case. */
    case ENODEV:
    case EBADFD:   return AO_ERR_LOST;
    default:       return AO_ERR_OTHER;
    }
}

/* ── Which device, and the amp that belongs to one of them ──────────────────
 * ONE home for "which /dev/dsp*", and it sits OUTSIDE the ALSA guard below on
 * purpose.  Three callers resolve through here — this file's opener,
 * audio.c (which sets the preference and logs the path), and ScummVM's mixer —
 * and the ALSA backend is the only part inside that guard, so a host build with
 * no libasound must still link the resolution.  That is also what lets
 * tests/audio_out_test.c drive it with no sound card present.  The OSS nodes
 * are still what is resolved: /dev/dspN existing is how a card is seen, and
 * audio_out_device_pcm() maps the node to its ALSA PCM.
 *
 * ⚠️ The preference is a file-static, not a field on AudioOut.  audio.c sets
 * it before audio_open(), and audio_open() memsets the struct — a field would
 * be cleared by the very call that needs to read it.  One audio device per
 * process is a fact about the hardware, so one static is the honest shape.
 */

#define AUDIO_DEV_ONBOARD "/dev/dsp"      /* TWL4030, the panel speaker */
#define AUDIO_DEV_USB     "/dev/dsp1"     /* ALSA card 1, a USB DAC     */

/** "onboard" | "usb" | "auto".  Defaults to auto: a USB DAC when one is plugged in,
 *  the panel speaker otherwise. */
static char audio_dev_pref[16] = "auto";

void audio_out_set_device_pref(const char *pref)
{
    if (!pref || !*pref) pref = "auto";
    snprintf(audio_dev_pref, sizeof(audio_dev_pref), "%s", pref);
}

const char *audio_out_device_pref(void)
{
    return audio_dev_pref;
}

bool audio_out_usb_present(void)
{
    return access(AUDIO_DEV_USB, W_OK) == 0;
}

const char *audio_out_device_for(const char *pref, bool usb_present)
{
    bool want_usb = (pref && strcmp(pref, "usb")  == 0);
    bool prefer   = (pref && strcmp(pref, "auto") == 0);

    if (!want_usb && !prefer) return AUDIO_DEV_ONBOARD;
    if (usb_present) return AUDIO_DEV_USB;

    /* ⚠️ Both non-onboard settings fall back rather than opening a path that is
     * not there: a games panel that has gone mute with no explanation is worse
     * than one on the wrong speaker.  The two differ only in whether the
     * fallback is reported — "auto" means unplugging is expected, "usb" was an
     * explicit request that could not be honoured, so it says so once. */
    if (want_usb) {
        static bool said = false;
        if (!said) {
            fprintf(stderr, "audio_out: %s requested but absent — using %s\n",
                    AUDIO_DEV_USB, AUDIO_DEV_ONBOARD);
            said = true;
        }
    }
    return AUDIO_DEV_ONBOARD;
}

/* The settings page's list (audio_out.h).  `needs_usb` is the whole
 * availability rule today; a Bluetooth row brings its own flag and input. */
static const struct {
    const char *name;
    const char *label;
    bool        needs_usb;
} audio_out_choices[AUDIO_OUT_CHOICE_COUNT] = {
    [AUDIO_OUT_CHOICE_ONBOARD] = { "onboard", "ONBOARD", false },
    [AUDIO_OUT_CHOICE_USB]     = { "usb",     "USB",     true  },
    [AUDIO_OUT_CHOICE_AUTO]    = { "auto",    "AUTO",    false },
};

static int choice_valid(int c)
{
    return (c >= 0 && c < AUDIO_OUT_CHOICE_COUNT) ? c : AUDIO_OUT_CHOICE_ONBOARD;
}

const char *audio_out_choice_name(int choice)
{
    return audio_out_choices[choice_valid(choice)].name;
}

const char *audio_out_choice_label(int choice)
{
    return audio_out_choices[choice_valid(choice)].label;
}

int audio_out_choice_of(const char *name)
{
    for (int c = 0; name && c < AUDIO_OUT_CHOICE_COUNT; c++)
        if (strcmp(name, audio_out_choices[c].name) == 0) return c;
    return AUDIO_OUT_CHOICE_ONBOARD;
}

bool audio_out_choice_available(int choice, bool usb_present)
{
    if (choice < 0 || choice >= AUDIO_OUT_CHOICE_COUNT) return false;
    return !audio_out_choices[choice].needs_usb || usb_present;
}

int audio_out_choice_shown(int saved, bool usb_present)
{
    if (saved < 0 || saved >= AUDIO_OUT_CHOICE_COUNT) return AUDIO_OUT_CHOICE_ONBOARD;
    return audio_out_choice_available(saved, usb_present) ? saved
                                                          : AUDIO_OUT_CHOICE_AUTO;
}

int audio_out_choice_next(int shown, bool usb_present)
{
    int c = choice_valid(shown);
    /* A wrap by comparison, not `%`: this file runs on a core with no divide. */
    for (int i = 0; i < AUDIO_OUT_CHOICE_COUNT; i++) {
        if (++c == AUDIO_OUT_CHOICE_COUNT) c = 0;
        if (audio_out_choice_available(c, usb_present)) return c;
    }
    return AUDIO_OUT_CHOICE_AUTO;   /* unreachable while AUTO needs nothing */
}

/** Card 1 was present and would not open — see audio_out_open_resolved().
 *  Process-global for the preference's reason: one audio device per process. */
static bool usb_refused = false;

/** Is card 1 there AND usable?  Seeing it absent clears a refusal, so the next
 *  plug is a fresh card. */
static bool usb_usable(void)
{
    bool present = audio_out_usb_present();
    if (!present) usb_refused = false;
    return present && !usb_refused;
}

const char *audio_out_device_path(void)
{
    return audio_out_device_for(audio_dev_pref, usb_usable());
}

/* The ALSA name for the same device.  OSS minor N is ALSA card N here, because
 * the OSS nodes are the kernel's emulation over those very cards.  Only the two
 * nodes the resolver can return are named; anything else is onboard, the same
 * fallback it has. */
const char *audio_out_device_pcm(const char *path)
{
    if (path && strcmp(path, AUDIO_DEV_USB) == 0) return "plughw:1,0";
    return "plughw:0,0";
}

bool audio_out_device_is_onboard(void)
{
    return strcmp(audio_out_device_path(), AUDIO_DEV_ONBOARD) == 0;
}

#define GPIO12_DIRECTION  "/sys/class/gpio/gpio12/direction"
#define GPIO12_VALUE      "/sys/class/gpio/gpio12/value"

void audio_out_enable_amp(void)
{
    FILE *f;
    f = fopen(GPIO12_DIRECTION, "w");
    if (f) { fputs("out", f); fclose(f); }
    f = fopen(GPIO12_VALUE, "w");
    if (f) { fputs("1",   f); fclose(f); }
}

/* ── The ALSA backend ────────────────────────────────────────────────────────
 *
 * The one device backend.  Compiled only with an explicit -DAUDIO_OUT_HAVE_ALSA,
 * which both device build paths pass together with libasound's include and link
 * flags.  Not detected with __has_include: a host that merely has libasound's
 * headers installed would start failing to link every host regression that
 * compiles this file.  Without the define the host gets a stub that refuses to
 * open, which is all the host regressions need — they drive audio_out_open()
 * through their own AudioOutDev.
 *
 * The context is one static, not a field on AudioOut: one stream per process is
 * already enforced (g_live), and a static keeps <alsa/asoundlib.h> out of the
 * header every client includes.
 */

#ifdef AUDIO_OUT_HAVE_ALSA

#include <alsa/asoundlib.h>

/* Requests, not demands: the grant is read back and used.  Forcing tiny periods
 * is what trades a jitter buffer for latency, and the pacing depends on the
 * jitter buffer: constraining the ring is what removed it when the kernel's OSS
 * emulation was the backend.  The values are what that OSS shim granted on its
 * own for every rate and channel count measured, 2048 x 16 (743 ms at 44100 —
 * NOT the ~506 ms this repo believed for months): the lead is three periods, and at
 * 1024 frames it fell to 92 ms and SameGame's slow frames starved the stream
 * 71 times in one session where the OSS lead of ~139 ms had covered them. */
#define ALSA_PERIOD_REQ   2048
#define ALSA_PERIODS_REQ  16

typedef struct {
    snd_pcm_t         *pcm;
    const char        *name;
    int                frame_bytes;
    snd_pcm_uframes_t  period;
    snd_pcm_uframes_t  buffer;
    bool               lost_said;
    unsigned           write_xruns;   /* underruns met by writei — counted nowhere else */
} AlsaCtx;

static AlsaCtx g_alsa;

/** An XRUN or a suspend is recovered in place; a lost device is reported once
 *  and handed up as ENODEV.  Returns 0 if the caller may retry. */
static int alsa_recover(AlsaCtx *a, int err)
{
    switch (audio_out_alsa_classify(err)) {
    case AO_ERR_XRUN:
        return snd_pcm_prepare(a->pcm) < 0 ? -1 : 0;
    case AO_ERR_SUSPEND:
        if (snd_pcm_resume(a->pcm) < 0 && snd_pcm_prepare(a->pcm) < 0) return -1;
        return 0;
    case AO_ERR_LOST:
        if (!a->lost_said) {
            fprintf(stderr, "audio_out: %s is gone (%s)\n", a->name, snd_strerror(err));
            a->lost_said = true;
        }
        errno = ENODEV;
        return -1;
    default:
        errno = (err < 0) ? -err : EIO;
        return -1;
    }
}

static int alsa_open(void *ctx, int rate_req, int channels_req,
                     int *rate_granted, int *bits_granted, int *channels_granted)
{
    AlsaCtx *a = (AlsaCtx *)ctx;
    int err;

    /* ⚠️ GPIO12 is the TWL4030's speaker amp and belongs to card 0 alone.  Poking
     * it while a USB DAC is the sink unmutes a speaker nothing is feeding, so it
     * is decided from the PCM actually being opened, not done unconditionally. */
    if (strcmp(a->name, audio_out_device_pcm(AUDIO_DEV_ONBOARD)) == 0)
        audio_out_enable_amp();

    /* ⚠️ Non-blocking is not an optimisation.  A blocking write stalls for a full
     * hardware period once the queue fills, which turns every subsequent sound
     * into a late one; the write policies follow the queue at real-time pace. */
    err = snd_pcm_open(&a->pcm, a->name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err < 0) {
        fprintf(stderr, "audio_out: cannot open %s: %s\n", a->name, snd_strerror(err));
        a->pcm = NULL;
        return -1;
    }

    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    unsigned int rate = (unsigned int)rate_req, ch = (unsigned int)channels_req;
    snd_pcm_uframes_t period = ALSA_PERIOD_REQ;
    snd_pcm_uframes_t buffer = ALSA_PERIOD_REQ * ALSA_PERIODS_REQ;
    int dir = 0;

    if ((err = snd_pcm_hw_params_any(a->pcm, hw)) < 0 ||
        (err = snd_pcm_hw_params_set_access(a->pcm, hw,
                                            SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
        (err = snd_pcm_hw_params_set_format(a->pcm, hw, SND_PCM_FORMAT_S16_LE)) < 0 ||
        (err = snd_pcm_hw_params_set_rate_near(a->pcm, hw, &rate, &dir)) < 0 ||
        (err = snd_pcm_hw_params_set_channels_near(a->pcm, hw, &ch)) < 0)
        goto fail;
    /* Geometry is a request: a refusal here leaves the device's own choice,
     * which the read-back below reports. */
    dir = 0;
    snd_pcm_hw_params_set_period_size_near(a->pcm, hw, &period, &dir);
    snd_pcm_hw_params_set_buffer_size_near(a->pcm, hw, &buffer);
    if ((err = snd_pcm_hw_params(a->pcm, hw)) < 0) goto fail;

    /* The GRANT, read back from the installed parameters — never the request. */
    dir = 0;
    snd_pcm_hw_params_get_rate(hw, &rate, &dir);
    snd_pcm_hw_params_get_channels(hw, &ch);
    dir = 0;
    snd_pcm_hw_params_get_period_size(hw, &period, &dir);
    snd_pcm_hw_params_get_buffer_size(hw, &buffer);

    /* Start at one period queued, so the prefill's silence is what starts the
     * stream — and what restarts it after an XRUN's prepare. */
    snd_pcm_sw_params_t *sw;
    snd_pcm_sw_params_alloca(&sw);
    if ((err = snd_pcm_sw_params_current(a->pcm, sw)) < 0 ||
        (err = snd_pcm_sw_params_set_start_threshold(a->pcm, sw, period)) < 0 ||
        (err = snd_pcm_sw_params(a->pcm, sw)) < 0)
        goto fail;

    a->period      = period;
    a->buffer      = buffer;
    a->frame_bytes = (int)ch * AUDIO_BYTES_PER_SAMPLE;
    a->lost_said   = false;
    a->write_xruns = 0;

    *rate_granted     = (int)rate;
    *bits_granted     = snd_pcm_format_width(SND_PCM_FORMAT_S16_LE);
    *channels_granted = (int)ch;

    fprintf(stderr, "audio_out: %s open (alsa), granted %u Hz %d-bit %u ch, "
                    "period %lu, buffer %lu frames (requested %d Hz %d ch)\n",
            a->name, rate, *bits_granted, ch, (unsigned long)period,
            (unsigned long)buffer, rate_req, channels_req);
    return 0;

fail:
    fprintf(stderr, "audio_out: cannot configure %s: %s\n", a->name, snd_strerror(err));
    snd_pcm_close(a->pcm);
    a->pcm = NULL;
    return -1;
}

static int alsa_space(void *ctx, int frame_bytes, AudioOutSpace *sp)
{
    AlsaCtx *a = (AlsaCtx *)ctx;
    if (!a->pcm || frame_bytes <= 0) return -1;

    /* ⚠️ snd_pcm_avail(), never snd_pcm_avail_update().  Measured on this
     * device's alsa-lib with a USB card unplugged under a live plughw stream:
     * avail_update does not sync with the hardware, and from the disconnect on it
     * returned a frozen positive count with no error for 25 s, while avail
     * returned -ENODEV from the first call.  That frozen count reads as a full
     * ring, so the pump asks for nothing, writei — whose EBADFD was the only
     * other report of the loss — is never reached, and the stream goes silent
     * with no reopen.  avail syncs first, which also removes avail_update's lag
     * (measured up to ~1.8k frames, under one period, on a healthy stream). */
    snd_pcm_sframes_t avail = snd_pcm_avail(a->pcm);
    if (avail < 0) {
        if (alsa_recover(a, (int)avail) != 0) return -1;
        avail = snd_pcm_avail(a->pcm);
        if (avail < 0) {
            alsa_recover(a, (int)avail);   /* for its errno and its one report */
            return -1;
        }
    }

    long ring = (long)a->buffer;
    if (avail > ring) avail = ring;   /* an unreported XRUN reads past the ring */
    sp->period_frames = (long)a->period;
    sp->ring_frames   = ring;
    sp->space         = (long)avail;
    sp->in_flight     = ring - (long)avail;
    return 0;
}

static ssize_t alsa_write(void *ctx, const void *buf, size_t nbytes, bool *again)
{
    AlsaCtx *a = (AlsaCtx *)ctx;
    if (!a->pcm || a->frame_bytes <= 0) { errno = EBADF; return -1; }

    /* ALSA moves whole frames only, so no partial frame can ever be left in the
     * device and audio_write_frames()' realignment is never entered. */
    snd_pcm_uframes_t frames = (snd_pcm_uframes_t)(nbytes / (size_t)a->frame_bytes);
    if (frames == 0) return 0;

    /* One retry after a recovered XRUN or suspend: a freshly prepared ring is
     * empty, so a second failure is not another underrun. */
    for (int attempt = 0; attempt < 2; attempt++) {
        snd_pcm_sframes_t r = snd_pcm_writei(a->pcm, buf, frames);
        if (r >= 0) return (ssize_t)r * a->frame_bytes;
        if (audio_out_alsa_classify((int)r) == AO_ERR_AGAIN) {
            *again = true;
            errno  = EAGAIN;
            return -1;
        }
        /* ⚠️ The service's dry-queue count only sees a ring found empty at the
         * space query; one that runs dry between that query and this write is
         * recovered here and retried, and is audible all the same — so say it. */
        if (audio_out_alsa_classify((int)r) == AO_ERR_XRUN && ++a->write_xruns <= 200)
            fprintf(stderr, "audio_out: %s underran at the write (%u this stream)\n",
                    a->name, a->write_xruns);
        if (alsa_recover(a, (int)r) != 0) return -1;
    }
    errno = EIO;
    return -1;
}

/* usleep, not snd_pcm_wait(): the serviced policy's wait of 0 must stay a
 * no-op, and snd_pcm_wait() can return early, which the blocking policies'
 * wait-count bound does not expect. */
static void alsa_wait(void *ctx, int usec)
{
    (void)ctx;
    if (usec > 0) usleep((useconds_t)usec);
}

/* The generic layer has already drained to the slack; nothing more here. */
static void alsa_close(void *ctx)
{
    AlsaCtx *a = (AlsaCtx *)ctx;
    if (a->pcm) { snd_pcm_close(a->pcm); a->pcm = NULL; }
}

static const AudioOutDev ALSA_DEV = {
    alsa_open, alsa_space, alsa_write, alsa_wait, alsa_close
};

int audio_out_open_alsa(AudioOut *out, const char *pcm, int rate_req, int channels_req)
{
    if (!out) return -1;
    if (!pcm || !*pcm) pcm = audio_out_device_pcm(NULL);
    /* Refuse before touching g_alsa: a second live open must not clobber the
     * first stream's context, and audio_out_open() would refuse it only after. */
    if (g_alsa.pcm) {
        memset(out, 0, sizeof(*out));
        fprintf(stderr, "audio_out: refused — an ALSA stream is already open\n");
        return -1;
    }
    memset(&g_alsa, 0, sizeof(g_alsa));
    g_alsa.name = pcm;
    return audio_out_open(out, &ALSA_DEV, &g_alsa, rate_req, channels_req);
}

#else  /* built without -DAUDIO_OUT_HAVE_ALSA: the host */

/* ⚠️ ALSA is the only device backend, so an ARM object without it is a build that
 * would ship silent — a ScummVM config.mk left over from before the ALSA appends
 * is exactly that.  Refuse it here rather than on the panel. */
#if defined(__arm__)
#error "audio_out.c needs -DAUDIO_OUT_HAVE_ALSA (and alsa-lib) on the device build"
#endif

int audio_out_open_alsa(AudioOut *out, const char *pcm, int rate_req, int channels_req)
{
    (void)pcm; (void)rate_req; (void)channels_req;
    if (out) memset(out, 0, sizeof(*out));
    fprintf(stderr, "audio_out: built without ALSA support\n");
    return -1;
}

#endif /* AUDIO_OUT_HAVE_ALSA */

/* ── The opener ─────────────────────────────────────────────────────────── */

int audio_out_open_default(AudioOut *out, const char *path,
                           int rate_req, int channels_req)
{
    return audio_out_open_alsa(out, audio_out_device_pcm(path),
                               rate_req, channels_req);
}

const char *audio_out_backend_name(void)
{
    return "alsa";
}

/* ── Reopen, fallback and replug ────────────────────────────────────────────
 * Both stream owners (audio.c and ScummVM's mixer) open through here and poll
 * audio_out_usb_returned(), so a DAC that comes and goes is handled one way. */

/** The name the log lines use: the ALSA PCM the node maps to — the same name
 *  both owners print at open. */
static const char *device_label(const char *path)
{
    return audio_out_device_pcm(path);
}

/** Millisecond clock for the probe's rate limit; audio.c's time_now_ms() is the
 *  same arithmetic, audio_ms_from_timeval(). */
static uint32_t probe_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return audio_ms_from_timeval((long)tv.tv_sec, (long)tv.tv_usec);
}

static int open_on(AudioOut *out, const char *path, int rate_req, int channels_req)
{
    if (audio_out_open_default(out, path, rate_req, channels_req) != 0) return -1;
    out->open_path       = strcmp(path, AUDIO_DEV_USB) == 0 ? AUDIO_DEV_USB
                                                             : AUDIO_DEV_ONBOARD;
    out->reprobe_last_ms = probe_now_ms();
    out->usb_seen        = false;
    return 0;
}

int audio_out_open_resolved(AudioOut *out, int rate_req, int channels_req,
                            const char **path_out)
{
    const char *path = audio_out_device_path();
    if (open_on(out, path, rate_req, channels_req) != 0) {
        if (strcmp(path, AUDIO_DEV_USB) != 0) return -1;
        /* ⚠️ Refuse it, then resolve again: the second resolution is the panel,
         * and the reopen below goes to the PCM that resolution names. */
        usb_refused = true;
        fprintf(stderr, "audio_out: %s will not open — using %s until it is "
                "replugged\n", device_label(AUDIO_DEV_USB),
                device_label(AUDIO_DEV_ONBOARD));
        path = audio_out_device_path();
        if (open_on(out, path, rate_req, channels_req) != 0) return -1;
    }
    if (path_out) *path_out = out->open_path;
    return 0;
}

bool audio_out_reprobe_due(const char *pref, const char *open_path,
                           uint32_t now_ms, uint32_t last_ms)
{
    if (!pref || !open_path) return false;
    if (strcmp(open_path, AUDIO_DEV_ONBOARD) != 0) return false;
    if (strcmp(pref, "usb") != 0 && strcmp(pref, "auto") != 0) return false;
    return (uint32_t)(now_ms - last_ms) >= AUDIO_OUT_REPROBE_MS;
}

bool audio_out_usb_returned(AudioOut *out)
{
    if (!out || !out->is_open || out->device_lost) return false;

    uint32_t now = probe_now_ms();
    if (!audio_out_reprobe_due(audio_dev_pref, out->open_path, now,
                               out->reprobe_last_ms))
        return false;
    out->reprobe_last_ms = now;

    /* Two consecutive sightings: the node can exist before the card will open,
     * and a move that fails there costs the card a refusal until it is
     * unplugged again. */
    bool usable = usb_usable();
    bool before = out->usb_seen;
    out->usb_seen = usable;
    if (!usable || !before) return false;

    fprintf(stderr, "audio_out: %s is back — leaving %s\n",
            device_label(AUDIO_DEV_USB), device_label(out->open_path));
    return true;
}
