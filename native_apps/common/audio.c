#include "audio.h"
#include "audio_gen.h"
#include "config.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/time.h>

/* ── Hardware constants ─────────────────────────────────────────────────────
 * The device half of the audio library.  Everything that is arithmetic rather
 * than I/O lives in audio_gen.c, which is what tests/audio_gen_test.c drives —
 * so the amplitude, the envelope lengths, the glide constants and every byte
 * count are defined THERE and used here.  Do not re-spell one of them.
 */

#define GPIO12_DIRECTION  "/sys/class/gpio/gpio12/direction"
#define GPIO12_VALUE      "/sys/class/gpio/gpio12/value"
#define DSP_DEVICE        "/dev/dsp"

/** Sample rate requested from the OSS driver.
 *  The ALSA OSS shim SRCs internally to the TWL4030's native 48000 Hz. */
#define TARGET_RATE       44100

/** Channel count assumed only when the read-back fails.  `hw:0,0` is
 *  stereo-only (measured, ../SYSTEM_ANALYSIS.md#34-audio), so 2 is the right
 *  fallback — but it is a fallback, not the model. */
#define FALLBACK_CHANNELS   2

/** Streaming chunk size in milliseconds (~10 ms of audio per chunk) */
#define STREAM_CHUNK_MS     10

/** Ceiling on a single chunk/fade allocation (200 ms at 44100 Hz).  It exists
 *  so a corrupted sample_rate cannot ask for an absurd buffer; it was a VLA
 *  stack-overflow guard when these buffers were on the stack. */
#define MAX_CHUNK_FRAMES    8820

/* ── Internal helpers ───────────────────────────────────────────────────── */

/** Drive GPIO12 HIGH to enable the on-board speaker amplifier (SPKR1). */
static void enable_amp(void)
{
    FILE *f;
    f = fopen(GPIO12_DIRECTION, "w");
    if (f) { fputs("out", f); fclose(f); }
    f = fopen(GPIO12_VALUE, "w");
    if (f) { fputs("1",   f); fclose(f); }
}

/** Millisecond wall clock (monotonic).  The arithmetic — including why the
 *  22-bit mask wraps every ~48.5 days rather than overflowing — is
 *  audio_ms_from_timeval()'s, so a host test can reach it without a clock. */
static uint32_t time_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return audio_ms_from_timeval((long)tv.tv_sec, (long)tv.tv_usec);
}

/**
 * (Re)configure DSP format/rate/channels on an already-open fd.
 *
 * ALSA OSS shim quirk on Linux 4.14.52 / TWL4030:
 *   SNDCTL_DSP_SPEED may reset FMT and CHANNELS.
 *   Set order must be SPEED → FMT → CHANNELS so the last two survive.
 *
 * SNDCTL_DSP_STEREO is silently ignored on this hardware, which is exactly why
 * both the rate and the CHANNEL COUNT are read back afterwards: what the
 * driver granted is the only number the byte arithmetic may use.  Before F1
 * the channel count was the literal `2` in all four write paths, and `hw:0,0`
 * happening to grant 2 made that accidentally right.
 */
static void configure_dsp(Audio *audio)
{
    int rate = TARGET_RATE;
    ioctl(audio->dsp_fd, SNDCTL_DSP_SPEED, &rate);

    int fmt = AFMT_S16_LE;
    ioctl(audio->dsp_fd, SNDCTL_DSP_SETFMT, &fmt);

    int stereo = 1;
    ioctl(audio->dsp_fd, SNDCTL_DSP_STEREO, &stereo);

    /* Read back the real rate the driver settled on */
    int actual = 0;
    if (ioctl(audio->dsp_fd, SOUND_PCM_READ_RATE, &actual) == 0 && actual > 0)
        audio->sample_rate = actual;
    else
        audio->sample_rate = TARGET_RATE;

    /* And the real channel count.  Warned about ONCE per Audio: this function
     * runs on every audio_flush(), i.e. before every canned sound. */
    int channels = 0;
    if (ioctl(audio->dsp_fd, SOUND_PCM_READ_CHANNELS, &channels) == 0 && channels > 0) {
        audio->channels = channels;
    } else {
        audio->channels = FALLBACK_CHANNELS;
        if (!audio->ch_warned) {
            fprintf(stderr, "audio: SOUND_PCM_READ_CHANNELS failed (errno=%d) — "
                            "assuming %d channels\n", errno, FALLBACK_CHANNELS);
            audio->ch_warned = true;
        }
    }
}

/* ── The one write path ──────────────────────────────────────────────────────
 * Four hand-rolled EAGAIN loops used to live in this file, each with its own
 * retry interval, its own give-up rule and its own way of abandoning a chunk
 * mid-frame.  They are now four POLICIES over audio_write_frames(), which is
 * the only code that decides when to stop — and it stops on a frame boundary
 * or reports that it could not (../IMPROVEMENT_PLAN.md F1).
 */

static ssize_t dsp_write(void *ctx, const void *buf, size_t nbytes, bool *again)
{
    Audio *audio = (Audio *)ctx;
    ssize_t r = write(audio->dsp_fd, buf, nbytes);
    if (r < 0 && errno == EAGAIN) *again = true;
    return r;
}

static void dsp_wait(void *ctx, int usec)
{
    (void)ctx;
    if (usec > 0) usleep((useconds_t)usec);
}

/** A tone: the ring will drain at hardware rate, so wait as long as it takes. */
static const AudioWritePolicy WPOL_TONE    = { 5000, 0, false };
/** The stream prefill: same, at a finer interval — it primes the DAC. */
static const AudioWritePolicy WPOL_PREFILL = { 1000, 0, false };
/** A stream chunk: the caller checked GETOSPACE first, so a full ring means
 *  "next frame", not "stall the render loop".  wait_us applies only to the
 *  bounded mid-frame realignment, which never has to happen here. */
static const AudioWritePolicy WPOL_CHUNK   = { 1000, 0, true  };
/** The fade-out: worth waiting for, but bounded — 100 × 2 ms = 200 ms. */
static const AudioWritePolicy WPOL_FADE    = { 2000, 100, false };
/** The pump: called from the render loop, so it may never wait.  Identical to
 *  WPOL_CHUNK today by design — the plan asked for a fifth POLICY rather than a
 *  fifth loop, and it is named separately because its reason for never blocking
 *  is different (a stalled render loop drops frames, not just audio) and Phase 4
 *  may well give it a period-sized wait once tinyalsa is underneath. */
static const AudioWritePolicy WPOL_PUMP    = { 1000, 0, true  };

/**
 * Interleave `frames` mono samples up to the negotiated channel count and hand
 * them to the device under `pol`.  Returns the bytes the device took, or -1 if
 * it could not even try.
 */
static long write_mono(Audio *audio, const int16_t *mono, long frames,
                       const AudioWritePolicy *pol, const char *what)
{
    long bytes = audio_bytes_for_frames(frames, audio->channels);
    if (bytes <= 0) return -1;

    int16_t *ilv = (int16_t *)malloc((size_t)bytes);
    if (!ilv) return -1;
    if (audio_interleave(mono, frames, audio->channels, ilv) != bytes) {
        free(ilv);
        return -1;
    }

    AudioSink sink = { dsp_write, dsp_wait, audio };
    AudioWriteResult res;
    audio_write_frames(&sink, ilv, frames, audio->channels, pol, &res);
    free(ilv);

    if (res.misaligned)
        fprintf(stderr, "audio: %s left a partial frame in the device "
                        "(%ld of %ld bytes) — channels may be swapped\n",
                what, res.bytes_written, bytes);

    return res.bytes_written;
}

/**
 * Wait for the current tone to finish playing before the next sound.
 *
 * With O_NONBLOCK we write data into the OSS ring nearly instantly, then
 * return.  The ring drains to the DAC at hardware rate in real time.
 * To avoid two sounds overlapping (or a new sound overwriting a short one
 * that hasn't been clocked out yet) we simply wait until sound_end_ms.
 *
 * Resets the OSS ring via SNDCTL_DSP_RESET before each new sound so that
 * back-to-back rapid taps don't mix: without a reset the previous tone's
 * PCM data still in the kernel ring would append to the new tone and both
 * would play together.
 *
 * Trade-off: SNDCTL_DSP_RESET causes ~50 ms of TWL4030 DAC pipeline
 * startup latency, so any tone shorter than ~60 ms will be silent.  We
 * therefore enforce a minimum tone duration of 60 ms everywhere in the
 * game sound effects.
 *
 * Cap: if the previous tone would take longer than FLUSH_MAX_WAIT_MS to
 * finish we bail early so the game loop is not stalled too long.
 */
#define FLUSH_MAX_WAIT_MS  200

static void audio_flush(Audio *audio)
{
    uint32_t wait = audio_flush_wait_ms(time_now_ms(), audio->sound_end_ms,
                                        FLUSH_MAX_WAIT_MS);
    if (wait > 0) usleep(wait * 1000U);
    /* Reset the ring to prevent tone mixing. */
    ioctl(audio->dsp_fd, SNDCTL_DSP_RESET, 0);
    configure_dsp(audio);
    audio->sound_end_ms = 0;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

/** Everything both entry points do: amp on, open, configure, read back. */
static int audio_open(Audio *audio)
{
    memset(audio, 0, sizeof(*audio));
    audio->dsp_fd      = -1;
    audio->available   = false;
    audio->sample_rate = TARGET_RATE;
    audio->channels    = FALLBACK_CHANNELS;
    audio->streaming   = false;

    enable_amp();

    /*
     * O_NONBLOCK is critical: a blocking write() stalls for the full
     * ALSA HW period (~506 ms) once the OSS ring fills, causing every
     * subsequent rapid sound event to play hundreds of ms late.
     * With O_NONBLOCK, write() returns EAGAIN when the ring is full and
     * the write policies above sleep and retry, following the ring at
     * real-time pace.
     */
    audio->dsp_fd = open(DSP_DEVICE, O_WRONLY | O_NONBLOCK);
    if (audio->dsp_fd < 0) {
        perror("audio: cannot open " DSP_DEVICE);
        return -1;
    }

    configure_dsp(audio);
    audio->available = true;
    return 0;
}

int audio_init(Audio *audio)
{
    /* Zero first, so a caller that ignores the return value still holds a
     * struct every playback function reads as unavailable. */
    memset(audio, 0, sizeof(*audio));
    audio->dsp_fd      = -1;
    audio->available   = false;
    audio->sample_rate = TARGET_RATE;
    audio->channels    = FALLBACK_CHANNELS;

    /* Check config — honour "audio_enabled" setting */
    {
        Config cfg;
        config_init(&cfg);
        config_load(&cfg);   /* silent if file missing */
        if (!config_audio_enabled(&cfg)) {
            printf("audio: disabled by config (%s)\n", CONFIG_FILE_PATH);
            audio->available = false;
            return 0;   /* success — games continue without sound */
        }
    }

    if (audio_open(audio) < 0) return -1;

    printf("audio: %s opened at %d Hz %d ch S16LE (O_NONBLOCK)\n",
           DSP_DEVICE, audio->sample_rate, audio->channels);
    return 0;
}

int audio_init_unchecked(Audio *audio)
{
    return audio_open(audio);
}

void audio_close(Audio *audio)
{
    if (audio->dsp_fd >= 0) {
        close(audio->dsp_fd);
        audio->dsp_fd = -1;
    }
    free(audio->pump_buf);
    audio->pump_buf        = NULL;
    audio->pump_buf_frames = 0;
    audio->pumping         = false;
    audio->available       = false;
}

/* ── The mix bus, device side ────────────────────────────────────────────────
 * The voices, the sum, the clamp and the pacing arithmetic are all in
 * audio_gen.c and host-tested.  What is here is the three things that need a
 * device: how much room the ring has, the scratch buffer, and the write.
 */

void audio_pump_enable(Audio *audio, bool on)
{
    if (!audio) return;
    if (on) {
        if (!audio->pumping) {
            /* Re-init clears the voices AND the diagnostics, so each PUMP: ON
             * session counts from zero — which is what makes an A/B on the panel
             * readable.  The limiter CHOICE is not a diagnostic and survives:
             * toggling the pump must not silently undo an operator's LIMIT
             * setting mid-comparison. */
            int keep_limit = audio->mix.limit;
            audio_mix_init(&audio->mix, audio->sample_rate);
            audio_mix_set_limit(&audio->mix, keep_limit);
            audio->pump_starved = 0;
            audio->pump_lost    = 0;
            audio->pump_diag    = 40;
            /* The lead is a MEASUREMENT taken from the device inside the pump, so
             * a new session starts without one rather than carrying the last
             * session's forward — the panel says "not measured yet" until a pump
             * has actually looked. */
            audio->pump_lead    = 0;
            audio->pump_period  = 0;
            audio->pumping      = true;
        }
        return;
    }
    /* Off: silence the bus, or its voices would simply never be rendered
     * again.  Whatever is already inside the device still plays out. */
    audio_mix_stop_all(&audio->mix);
    audio->pumping = false;
}

void audio_pump_set_keepalive(Audio *audio, bool on)
{
    if (audio) audio->keepalive = on;
}

bool audio_pump_active(const Audio *audio)
{
    if (!audio || !audio->pumping) return false;
    /* Keepalive counts as active: it is a promise of continuous silence, and a
     * render loop that drops to FRAME_DELAY_IDLE_US (100 ms) while the lead is
     * 80 ms starves the device — which would defeat the very thing keepalive is
     * there to measure.  Callers must not have to know that. */
    if (audio->keepalive) return true;
    return audio_mix_pending(&audio->mix) > 0;
}

int audio_pump_voices(const Audio *audio)
{
    return audio ? audio_mix_active(&audio->mix) : 0;
}

uint32_t audio_pump_clipped(const Audio *audio) { return audio ? audio->mix.clipped : 0; }
uint32_t audio_pump_dropped(const Audio *audio) { return audio ? audio->mix.dropped : 0; }
uint32_t audio_pump_limited(const Audio *audio) { return audio ? audio->mix.limited : 0; }
uint32_t audio_pump_starved(const Audio *audio) { return audio ? audio->pump_starved : 0; }
uint32_t audio_pump_lost(const Audio *audio)    { return audio ? audio->pump_lost    : 0; }
long     audio_pump_lead(const Audio *audio)    { return audio ? audio->pump_lead    : 0; }
long     audio_pump_period(const Audio *audio)  { return audio ? audio->pump_period  : 0; }

void audio_pump_set_limit(Audio *audio, int mode)
{
    if (audio) audio_mix_set_limit(&audio->mix, mode);
}

/** Scratch for one pump call, allocated once and kept.  The pump runs every
 *  frame, so a malloc/free pair per call is the one allocation in this library
 *  worth removing.  (write_mono()'s interleave buffer is the other one; it
 *  belongs to the write path Phase 4 rewrites.) */
static int16_t *pump_scratch(Audio *audio, long frames)
{
    if (frames <= 0) return NULL;
    if (audio->pump_buf && audio->pump_buf_frames >= frames) return audio->pump_buf;

    int16_t *nb = (int16_t *)realloc(audio->pump_buf, (size_t)frames * sizeof(int16_t));
    if (!nb) return NULL;
    audio->pump_buf        = nb;
    audio->pump_buf_frames = frames;
    return nb;
}

void audio_pump(Audio *audio)
{
    if (!audio->available || audio->dsp_fd < 0 || !audio->pumping) return;

    long pending = audio_mix_pending(&audio->mix);
    if (pending <= 0 && !audio->keepalive) return;

    int frame_bytes = audio_frame_bytes(audio->channels);
    if (frame_bytes <= 0) return;

    /* What the ring holds versus what it will take.  in_flight is the reason
     * this is not just "write into the free space": an empty ~506 ms OSS ring
     * would accept half a second of audio and put every sound triggered after
     * it half a second late. */
    audio_buf_info info;
    if (ioctl(audio->dsp_fd, SNDCTL_DSP_GETOSPACE, &info) < 0) return;

    long total_bytes = (long)info.fragstotal * (long)info.fragsize;
    long in_flight   = (total_bytes - (long)info.bytes) / frame_bytes;
    long space       = (long)info.bytes / frame_bytes;

    /* ⚠️ Measured BEFORE we write, and only when audio was owed: the ring being
     * empty between sounds is normal, the ring being empty while a voice is still
     * sounding is a gap the listener hears.  This is what distinguishes "the
     * mixer is wrong" from "this render loop cannot feed an 80 ms lead". */
    if (pending > 0 && in_flight <= 0) audio->pump_starved++;

    /* ⚠️ The lead is derived from the DEVICE's period, not from the ms constant
     * alone.  A lead that is 1.7 periods deep leaves ALSA one playable period and
     * a partial one it cannot see, which XRUNs every ~120 ms and DISCARDS the
     * staged audio — measured on `.188`, and the reason a mixed sound read as a
     * chopped square wave rather than as a level problem.  `fragsize` is the OSS
     * name for that period; Phase 4 reads it from tinyalsa instead. */
    long period = (long)info.fragsize / frame_bytes;
    long ring   = total_bytes / frame_bytes;
    long lead   = audio_pump_lead_frames(
                      audio_frames_for_ms(audio->sample_rate, AUDIO_PUMP_LEAD_MS),
                      period, AUDIO_PUMP_LEAD_PERIODS, ring);

    /* ⚠️ Publish both, because nothing else can: `lead` is a local derived from
     * the device's own fragsize, and a diagnostic that reconstructed it from
     * AUDIO_PUMP_LEAD_MS reported 80 ms against an effective ~139.  See
     * audio_pump_lead() in audio.h. */
    audio->pump_lead   = lead;
    audio->pump_period = period;

    /* ⚠️ A bounded trace of what the ring actually reported — 40 lines per
     * PUMP: ON session, so it cannot flood a log, and it prints the RAW ioctl
     * fields beside our derived ones.  It exists because `starve` climbing ~6 per
     * 200 ms tone (measured on `.188` 2026-08-15) has two candidate mechanisms
     * that no derived number can separate: a render loop too slow to feed an
     * 80 ms lead, or a `bytes`/`fragstotal` pair that does not mean what the pump
     * reads it to mean.  Guessing between them is what this repo calls theorising
     * from sysfs. */
    if (audio->pump_diag > 0) {
        audio->pump_diag--;
        fprintf(stderr, "pump: %s bytes=%d frag=%d/%d in_flight=%ld space=%ld "
                        "pending=%ld period=%ld lead=%ld\n",
                (pending > 0 && in_flight <= 0) ? "STARVED" : "ok",
                info.bytes, info.fragsize, info.fragstotal,
                in_flight, space, pending, period, lead);
    }

    long want = audio_pump_frames(
        lead,
        in_flight, space,
        (lead > 0) ? lead : audio_frames_for_ms(audio->sample_rate, AUDIO_PUMP_CAP_MS));
    if (want <= 0) return;

    /* Never render past the end of the last voice — unless keepalive says the
     * stream must not be allowed to go idle at all. */
    if (!audio->keepalive && want > pending) want = pending;

    int16_t *mono = pump_scratch(audio, want);
    if (!mono) return;

    if (audio_mix_render(&audio->mix, mono, want) <= 0) {
        if (!audio->keepalive) return;         /* nothing to say */
        memset(mono, 0, (size_t)want * sizeof(int16_t));
    }

    long taken = write_mono(audio, mono, want, &WPOL_PUMP, "pump");

    /* ⚠️ Count what the device refused.  The voices have already advanced past
     * those frames, so they are not deferred — they are gone, and the waveform
     * has a step where they were.  WPOL_PUMP may not block (a stalled render loop
     * drops frames, not just audio), so this is the price of that; counting it is
     * what makes it a measured price rather than an assumed-zero one. */
    long taken_frames = (taken > 0) ? taken / frame_bytes : 0;
    if (taken_frames < want) audio->pump_lost += (uint32_t)(want - taken_frames);
}
void audio_interrupt(Audio *audio)
{
    if (!audio->available || audio->dsp_fd < 0) return;

    /* On the pump this is "stop all voices": no ring reset, because the reset is
     * exactly what makes mixing impossible, and no sleep, because there is
     * nothing to wait for.  Up to AUDIO_PUMP_LEAD_MS of tail survives it. */
    if (audio->pumping) {
        audio_mix_stop_all(&audio->mix);
        return;
    }
    audio_flush(audio);
}

void audio_tone(Audio *audio, int freq_hz, int duration_ms)
{
    if (!audio->available || audio->dsp_fd < 0) return;
    if (freq_hz <= 0 || duration_ms <= 0)        return;

    /* ⚠️ The two paths are a BRANCH, not "enqueue and also write immediately".
     * The plan sketched the latter; it cannot work.  A bounded immediate write
     * truncates any tone longer than the lead (every tone here is), and an
     * unbounded one hands the whole tone to the kernel — which is the very thing
     * that makes it unmixable.  Branching instead means an app that never calls
     * audio_pump_enable() takes today's path byte for byte, which is a stronger
     * guarantee than "degrades gracefully". */
    if (audio->pumping) {
        audio_mix_add(&audio->mix, freq_hz, duration_ms, 0, AUDIO_PEAK);
        return;
    }

    /* Frames are computed 64-bit and clamped: (long)rate * duration_ms is a
     * 32-bit multiply on this target and overflows past ~48.7 s. */
    long frames = audio_frames_for_ms(audio->sample_rate, duration_ms);
    if (frames <= 0) return;

    /* Mono, single-sample — the generator never knows how many channels the
     * device wants.  write_mono() is the one conversion point. */
    int16_t *mono = (int16_t *)malloc((size_t)frames * sizeof(int16_t));
    if (!mono) return;

    audio_render_tone(audio->sample_rate, freq_hz, AUDIO_PEAK, mono, frames);
    write_mono(audio, mono, frames, &WPOL_TONE, "tone");
    free(mono);

    /* Record when this tone is expected to finish so audio_flush() can
     * wait before discarding it on the next interrupt call. */
    audio->sound_end_ms = time_now_ms() + (uint32_t)duration_ms;
}

/* ── Streaming (theremin) API ───────────────────────────────────────────── */

void audio_stream_start(Audio *audio, int freq_hz)
{
    if (!audio->available || audio->dsp_fd < 0) return;

    /* The theremin owns the ring while it streams, so it and the mix bus cannot
     * both be writing.  Refuse loudly rather than let two writers interleave
     * frames into one device. */
    if (audio->pumping) {
        fprintf(stderr, "audio: stream_start refused — the mix bus is pumping "
                        "(call audio_pump_enable(a, false) first)\n");
        return;
    }

    /* Reset DSP and configure with default OSS buffer.
     * Note: SNDCTL_DSP_SETFRAGMENT is unreliable on the TWL4030 ALSA OSS
     * shim (Linux 4.14.52) — it can leave the DSP in a bad state where
     * writes silently fail.  We use the default ~500ms OSS buffer instead. */
    if (ioctl(audio->dsp_fd, SNDCTL_DSP_RESET, 0) < 0) {
        fprintf(stderr, "audio: SNDCTL_DSP_RESET failed in stream_start (errno=%d)\n", errno);
        /* Continue anyway — configure_dsp will re-set params */
    }
    configure_dsp(audio);

    /* Initialize streaming state — one oscillator, amplitude 0 so it fades in */
    audio_osc_init(&audio->osc, audio->sample_rate, (double)freq_hz, AUDIO_PEAK);
    audio->streaming = true;

    /* Pre-fill ~200ms of audio to prime the OSS ring buffer past the TWL4030
     * DAC startup latency.  Without this, the first few streaming chunks may
     * be silently discarded, causing an audible gap. */
    {
        long prefill_frames = audio_frames_for_ms(audio->sample_rate, 200);
        int16_t *mono = (prefill_frames > 0)
                        ? (int16_t *)malloc((size_t)prefill_frames * sizeof(int16_t))
                        : NULL;
        if (mono) {
            audio_osc_render(&audio->osc, AUDIO_OSC_GLIDE, mono, prefill_frames);
            write_mono(audio, mono, prefill_frames, &WPOL_PREFILL, "stream prefill");
            free(mono);
        }
    }

    fprintf(stderr, "audio: stream start at %d Hz (rate=%d, %d ch)\n",
            freq_hz, audio->sample_rate, audio->channels);
}

void audio_stream_set_freq(Audio *audio, int freq_hz)
{
    if (!audio->streaming) return;
    audio->osc.target_freq = (double)freq_hz;
}

void audio_stream_chunk(Audio *audio)
{
    if (!audio->available || !audio->streaming) return;

    /* Query available space in the OSS ring buffer */
    audio_buf_info info;
    if (ioctl(audio->dsp_fd, SNDCTL_DSP_GETOSPACE, &info) < 0)
        return;

    long chunk_frames = audio_frames_for_ms(audio->sample_rate, STREAM_CHUNK_MS);
    if (chunk_frames < 1) return;
    if (chunk_frames > MAX_CHUNK_FRAMES) chunk_frames = MAX_CHUNK_FRAMES;
    long chunk_bytes = audio_bytes_for_frames(chunk_frames, audio->channels);
    if (chunk_bytes <= 0) return;

    /* One allocation for the whole call.  This was a 35 KB buffer declared
     * INSIDE the loop below, for a ~1.7 KB chunk (../IMPROVEMENT_PLAN.md F1). */
    int16_t *mono = (int16_t *)malloc((size_t)chunk_frames * sizeof(int16_t));
    if (!mono) return;

    /* Write as many small chunks as fit in the available buffer space.
     * Cap at some reasonable max to avoid spending too long here. */
    int max_chunks = 8;  /* at most 80ms of audio per call */
    int chunks_written = 0;

    while (info.bytes >= chunk_bytes && chunks_written < max_chunks) {
        audio_osc_render(&audio->osc, AUDIO_OSC_GLIDE, mono, chunk_frames);

        /* WPOL_CHUNK stops at the first full ring rather than waiting: the
         * space check above says it should fit, and stalling the render loop
         * is worse than skipping a chunk. */
        long written = write_mono(audio, mono, chunk_frames, &WPOL_CHUNK, "stream chunk");
        if (written < chunk_bytes) break;   /* partial or failed — stop writing */

        info.bytes -= (int)written;
        chunks_written++;
    }

    free(mono);
}

void audio_stream_stop(Audio *audio)
{
    if (!audio->available || !audio->streaming) return;

    /* Write a short fade-out to avoid a click/pop.  AUDIO_OSC_FADE_OUT holds
     * frequency and amplitude still and envelopes 1 → 0 over exactly this
     * call — a glide or a ramp during a fade-out fights the fade. */
    long fade_frames = audio_frames_for_ms(audio->sample_rate, 20);
    if (fade_frames < 1) fade_frames = 1;
    if (fade_frames > MAX_CHUNK_FRAMES) {
        fprintf(stderr, "audio: fade_frames=%ld exceeds max, clamping to %d (sample_rate=%d)\n",
                fade_frames, MAX_CHUNK_FRAMES, audio->sample_rate);
        fade_frames = MAX_CHUNK_FRAMES;
    }

    int16_t *mono = (int16_t *)malloc((size_t)fade_frames * sizeof(int16_t));
    if (mono) {
        audio_osc_render(&audio->osc, AUDIO_OSC_FADE_OUT, mono, fade_frames);
        write_mono(audio, mono, fade_frames, &WPOL_FADE, "stream fade-out");
        free(mono);
    }

    /* Reset DSP to flush remaining buffer */
    if (ioctl(audio->dsp_fd, SNDCTL_DSP_RESET, 0) < 0) {
        fprintf(stderr, "audio: SNDCTL_DSP_RESET failed in stream_stop (errno=%d)\n", errno);
    }
    configure_dsp(audio);

    audio->streaming    = false;
    audio->osc.amp      = 0.0;
    audio->sound_end_ms = 0;

    fprintf(stderr, "audio: stream stop complete\n");
}

/* ── Convenience sounds ─────────────────────────────────────────────────────
 * Four note tables and ONE sequencer.  The tables exist because the pump needs
 * each note's start offset, which the old shape — three bare audio_tone() calls
 * relying on the ring to serialise them — cannot express: three voices added at
 * once are a chord, and audio_success() is meant to be an arpeggio.  Off the
 * pump the sequencer is exactly the old code, flush included.
 */

typedef struct { int freq; int ms; } AudioNote;

static void play_sequence(Audio *audio, const AudioNote *notes, int count)
{
    if (!audio->available) return;

    if (audio->pumping) {
        int delay = 0;
        for (int i = 0; i < count; i++) {
            audio_mix_add(&audio->mix, notes[i].freq, notes[i].ms, delay, AUDIO_PEAK);
            delay += notes[i].ms;
        }
        return;
    }

    audio_flush(audio);                 /* discard any queued audio first */
    for (int i = 0; i < count; i++)
        audio_tone(audio, notes[i].freq, notes[i].ms);
}

/** 880 Hz, 80 ms — UI click / tile place */
void audio_beep(Audio *audio)
{
    static const AudioNote s[] = { { 880, 80 } };
    play_sequence(audio, s, 1);
}

/** 1320 Hz, 60 ms — item collected, food eaten */
void audio_blip(Audio *audio)
{
    static const AudioNote s[] = { { 1320, 60 } };
    play_sequence(audio, s, 1);
}

/** C5 → E5 → G5 ascending arpeggio — score milestone, level up */
void audio_success(Audio *audio)
{
    static const AudioNote s[] = { { 523, 120 }, { 659, 120 }, { 784, 220 } };
    play_sequence(audio, s, 3);
}

/** G4 → E4 → C4 descending — game over, error */
void audio_fail(Audio *audio)
{
    static const AudioNote s[] = { { 392, 150 }, { 330, 150 }, { 262, 300 } };
    play_sequence(audio, s, 3);
}
