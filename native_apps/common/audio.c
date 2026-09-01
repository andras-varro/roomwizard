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

/** How recently the preceding tone must have been ISSUED for the next one to
 *  chain behind it — half a frame.  The reasoning, and the two ways it goes
 *  wrong at 0 and at unbounded, are at the one place that reads it: the mixing
 *  branch of audio_tone(). */
#define AUDIO_TONE_CHAIN_MS 16

/** Channel count assumed only when the read-back fails.  `hw:0,0` is
 *  stereo-only (measured, ../SYSTEM_ANALYSIS.md#34-audio), so 2 is the right
 *  fallback — but it is a fallback, not the model. */
#define FALLBACK_CHANNELS   2

/** Ceiling on a single fade allocation (200 ms at 44100 Hz).  It exists so a
 *  corrupted sample_rate cannot ask for an absurd buffer; it was a VLA
 *  stack-overflow guard when these buffers were on the stack. */
#define MAX_CHUNK_FRAMES    8820

/** Read-ahead for one streaming sample voice, in frames (~93 ms at 44100).
 *
 *  ⚠️ **It sets how often the SD read is entered, not how much RAM the bed
 *  costs.**  Measured on `.188` 2026-08-20: the card delivers 11.4 MB/s and a
 *  mono 44.1 kHz bed needs 88.2 KB/s, so throughput is 0.77 % and irrelevant;
 *  what is untested is per-read LATENCY inside the render loop, and a buffer
 *  comfortably larger than the device's 2048-frame period is what absorbs it.
 *  The counter to watch on the panel is `starve`. */
#define AUDIO_SAMPLE_BUF_FRAMES  4096

/** How many passes `loop` asks for.  ⚠️ **Not a sentinel and not "forever":
 *  `audio_mix_add_sample()` needs a real length or `audio_mix_render()`'s
 *  `audio_mix_pending() <= 0` early-out never renders the voice at all
 *  (audio_gen.h).  200 passes of a 44 s track is ~2.4 h, which outlasts any
 *  session, and `sample_total_frames()` clamps the product so a 32-bit `long`
 *  cannot overflow into a negative length. */
#define AUDIO_MUSIC_LOOP_PASSES  200

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

/** Is there a device to write to, on whichever of the two paths is live?
 *
 * ⚠️ `dsp_fd >= 0` was the test everywhere in this file, and on the continuous
 * stream `dsp_fd` is -1 by design — the fd belongs to `audio_out` instead.  A
 * missed conversion of that test does not error: it makes the call a silent
 * no-op, which is exactly the failure mode this project describes as "does not
 * error — it misparses". */
static bool audio_live(const Audio *audio)
{
    if (!audio || !audio->available) return false;
    return audio->cont ? audio_out_is_open(&audio->out) : (audio->dsp_fd >= 0);
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
 * driver granted is the only number the byte arithmetic may use.  A hardcoded
 * `2` in the write paths is only accidentally right, because `hw:0,0` happens
 * to grant 2.
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

/* ── The one write path, and what is left of it ───────────────────────────────
 * Four hand-rolled EAGAIN loops used to live in this file, each with its own
 * retry interval, its own give-up rule and its own way of abandoning a chunk
 * mid-frame.  They became four POLICIES over audio_write_frames(), which is the
 * only code that decides when to stop.
 *
 * ⚠️ **Two of those five policies are now GONE, and so is a third**, because the
 * paths that needed them moved to `audio_out.c`:
 *   - `WPOL_CHUNK` and `WPOL_PREFILL` were the theremin's chunk loop and its
 *     200 ms prime.  The theremin is on the continuous stream, which prefills at
 *     open and is serviced rather than chunked.
 *   - `WPOL_FADE` was the fade-out, and the ONLY user of `max_waits` in this
 *     file.  `audio_out`'s `blocking_policy()` derives that bound from the length
 *     of what it is writing instead of carrying a constant, which is what stops a
 *     long tone being truncated and a wedged device hanging a UI.
 * The two that remain belong to the OLD path, which survives on purpose as the
 * negative control for the click — see audio_cont_enable() in audio.h.
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
/** The pump: called from the render loop, so it may never wait.  On the
 *  continuous stream this policy's counterpart is `AOPOL_SERVICE`, whose wait is
 *  0 rather than 1000 — the difference is measured by `audio_out_test` D13c, and
 *  it is what makes "never sleeps" true rather than nearly true. */
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

    /* ⚠️ The SAME device stage the continuous path gets, immediately before the
     * write and after the interleave — otherwise the CONT toggle changes the
     * loudness as well as the architecture and its A/B answers neither. */
    audio_attenuate(ilv, frames * (long)audio->channels, audio->master_shift);

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

/** Amp on, open, configure, read back — WITHOUT touching the rest of the struct.
 *  ⚠️ Split out from audio_open() because audio_cont_enable() takes the device
 *  back this way, and a memset there would drop the mix bus, the scratch buffer
 *  (leaking it) and the toggle that asked for the switch. */
static int dsp_reopen(Audio *audio)
{
    enable_amp();

    /*
     * O_NONBLOCK is critical: a blocking write() stalls for the full
     * ALSA HW period once the OSS ring fills, causing every subsequent rapid
     * sound event to play hundreds of ms late.  With O_NONBLOCK, write()
     * returns EAGAIN when the ring is full and the write policies above sleep
     * and retry, following the ring at real-time pace.
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

/** The level defaults, in one place because three entry points memset this
 *  struct and a zeroed `vol` is SILENCE while a zeroed `master_shift` is twice
 *  the intended loudness — two different silent failures from one omission. */
static void level_defaults(Audio *audio)
{
    audio->vol            = AUDIO_VOICE_VOL;
    audio->master_shift   = AUDIO_MASTER_SHIFT;
    audio->last_tone_slot = -1;
    audio->last_tone_gen  = 0;
    audio->last_tone_ms   = 0;
    /* ⚠️ -1, because a zeroed slot is SLOT 0 — i.e. a valid handle onto whatever
     * voice happens to be there.  Every entry point memsets the struct, so this
     * is the only place the two sample voices become "idle" rather than "aimed at
     * somebody else's tone". */
    audio->music.slot     = -1;
    audio->sfx.slot       = -1;
    /* The clip voices, same -1, but for a WEAKER reason than the two above and it
     * is worth stating rather than implying: a zeroed clip voice reads slot 0 —
     * which on a game with a bed IS the bed — but its `gen` is 0 and
     * `audio_mix_voice_gen()` never issues 0, so the pair already answers "not
     * live" and the voice is correctly seen as free.  The -1 is what keeps
     * `slot >= 0` meaning "has been armed" for anything that reads it later. */
    for (int i = 0; i < AUDIO_CLIP_VOICES; i++) audio->fxv[i].slot = -1;
    /* ⚠️ Same family, and it is the LIMITER that had this bug: bus_reset() keeps
     * `mix.limit` across a session so a panel A/B is not undone mid-comparison,
     * but AUDIO_MIX_SOFT is 0 — so on a never-armed bus that zero masqueraded as
     * an operator's choice and every app's first session ran SOFT.  At the
     * shipped volume, with the two voices a game actually sums, HARD applies no
     * nonlinearity at all while SOFT's knee (which tracks ONE voice's peak) bends
     * every two-voice sum for nothing.  Set here rather than in bus_reset() so
     * the carry-across keeps working.  tests/audio_tone_test.c group F. */
    audio->mix.limit      = AUDIO_MIX_HARD;
}

/* The clip bank's three lifecycle hooks, defined with the rest of it further
 * down: audio_open() sets the default paths, audio_init() applies the config
 * overrides on top, and audio_close() frees the PCM.  Declared here rather than
 * moving 200 lines up, so the clip code stays beside the canned sounds it backs. */
static void fx_defaults(Audio *audio);
static void fx_config_apply(Audio *audio, Config *cfg);
static void clip_bank_discard(Audio *audio);

/** Release one sample voice's OS resources.  Called only from audio_close():
 *  see the note there for why stopping a voice must not do this. */
static void sample_discard(AudioSampleVoice *sv)
{
    audio_wav_close(&sv->wav);
    free(sv->buf);
    sv->buf        = NULL;
    sv->buf_frames = 0;
    sv->slot       = -1;
    sv->gen        = 0;
}

/** Everything both entry points do: amp on, open, configure, read back. */
/* ── The per-frame service a blocking sub-loop owes (see common.h) ────────── */

/* ⚠️ The setter is declared WEAK rather than by including "common.h", because
 * audio.c must stay linkable WITHOUT common.o: tests/audio_tone_test.c,
 * audio_gen_test.c, audio_sample_test.c, audio_out_test.c and the four
 * measure_audio_*_sabotage.sh harnesses all compile audio.c on the host with no
 * UI code at all, deliberately (`native_apps/CLAUDE.md` → *Audio*).  Absent
 * common.o the symbol resolves to 0, nothing registers, and the tests behave
 * exactly as they did before.  Every shipped binary links common.o explicitly
 * (COMMON_OBJ in build-and-deploy.sh — objects, never an archive, so a weak
 * reference does resolve). */
extern void ui_frame_service_set(void (*fn)(void *ctx), void *ctx) __attribute__((weak));

static void audio_frame_service(void *ctx)
{
    audio_pump((Audio *)ctx);
}

static int audio_open(Audio *audio)
{
    memset(audio, 0, sizeof(*audio));
    audio->dsp_fd      = -1;
    audio->available   = false;
    audio->sample_rate = TARGET_RATE;
    audio->channels    = FALLBACK_CHANNELS;
    audio->streaming   = false;
    /* ⚠️ TRUE here, and lowered ONLY by audio_init() below.  audio_open() is also
     * audio_init_unchecked()'s whole body, so a hardware speaker test gets a
     * struct with both toggles up — which is the point of that bypass. */
    audio->music_on    = true;
    audio->effects_on  = true;
    level_defaults(audio);
    fx_defaults(audio);

    /* ⚠️ Registered HERE and not in audio_init(), because audio_init_unchecked()
     * is audio_open()'s whole body and a speaker test blocks the same way a game
     * does.  One process opens one Audio; a second open replaces the slot, which
     * is correct — the newest struct is the one being pumped. */
    if (ui_frame_service_set) ui_frame_service_set(audio_frame_service, audio);

    return dsp_reopen(audio);
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

    /* Check config — honour "audio_enabled" setting.  ⚠️ The same load also
     * carries the four clip paths, applied AFTER audio_open() because that
     * memsets the struct — so this Config outlives the gate it was opened for. */
    Config cfg;
    config_init(&cfg);
    config_load(&cfg);   /* silent if file missing */
    if (!config_audio_enabled(&cfg)) {
        printf("audio: disabled by config (%s)\n", CONFIG_FILE_PATH);
        audio->available = false;
        return 0;   /* success — games continue without sound */
    }

    if (audio_open(audio) < 0) return -1;
    fx_config_apply(audio, &cfg);

    /* The two games-menu toggles, applied AFTER audio_open() for the same reason
     * fx_config_apply() is: that call memsets the struct and raises both.  ⚠️ They
     * are NOT a second `audio_enabled` — the device is open either way, so a game
     * whose music is off still mixes its effects, and the pump still runs. */
    audio->music_on   = config_music_enabled(&cfg);
    audio->effects_on = config_effects_enabled(&cfg);
    if (!audio->music_on || !audio->effects_on)
        printf("audio: music %s, effects %s (%s)\n",
               audio->music_on ? "on" : "OFF",
               audio->effects_on ? "on" : "OFF", CONFIG_FILE_PATH);

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
    /* ⚠️ **The counters are the diagnosis, and this is the one place every app
     * reports them.**  One line, from the LIBRARY rather than from a game's own
     * idea of what it enabled — the same reason `audio_get_volume()` exists — and
     * only when a bus was actually running, so the two hardware tabs that never
     * pump stay silent.  It is what separates a PACING fault from a mixing one on
     * a device with no microphone: `starve` is one audible gap each, and one per
     * bed start is expected (a fresh stream's first service legitimately finds
     * `in_flight` 0).  ⚠️ `services` comes from `audio_out` and is 0 off the
     * continuous path, where nothing counts them. */
    if (audio->cont || audio->pumping) {
        fprintf(stderr, "audio: bus closed — cont=%d services=%u starve=%u "
                        "lost=%u drop=%u lim=%u clip=%u lead=%ldms period=%ldms\n",
                audio->cont ? 1 : 0,
                audio->cont ? audio_out_services(&audio->out) : 0u,
                audio->pump_starved, audio->pump_lost,
                audio->mix.dropped, audio->mix.limited, audio->mix.clipped,
                audio_ms_for_frames(audio->sample_rate, audio->pump_lead),
                audio_ms_for_frames(audio->sample_rate, audio->pump_period));
    }

    /* ⚠️ The continuous stream DRAINS on close, bounded — otherwise the queued
     * tail is discarded, which on a Settings speaker test is most of the tone
     * that was just played.  audio_out_close() is the only implementation. */
    if (audio->cont) {
        audio_out_close(&audio->out);
        audio->cont       = false;
        audio->osc_stream = false;
    }
    if (audio->dsp_fd >= 0) {
        close(audio->dsp_fd);
        audio->dsp_fd = -1;
    }
    free(audio->pump_buf);
    audio->pump_buf        = NULL;
    audio->pump_buf_frames = 0;
    /* ⚠️ The two sample voices hold a `FILE *` and a `malloc`, and this is the
     * ONLY place either is released — audio_music_stop() deliberately does not,
     * because the mixer is still pulling from them through the release.  A tool
     * that starts a bed on every session and never closes leaks one fd per run. */
    sample_discard(&audio->music);
    sample_discard(&audio->sfx);
    clip_bank_discard(audio);
    audio->pumping         = false;
    audio->streaming       = false;
    audio->available       = false;
    /* ⚠️ Cleared, because the registered ctx is usually a `main()` STACK
     * address: a blocking sub-loop entered after audio_close() would otherwise
     * pump a dead struct. */
    if (ui_frame_service_set) ui_frame_service_set(NULL, NULL);
}

/* ── The mix bus, device side ────────────────────────────────────────────────
 * The voices, the sum, the clamp and the pacing arithmetic are all in
 * audio_gen.c and host-tested.  What is here is the three things that need a
 * device: how much room the ring has, the scratch buffer, and the write.
 */

/** Start a mix-bus session: clear the voices AND the diagnostics, so each
 *  session counts from zero — which is what makes an A/B on the panel readable.
 *  The limiter CHOICE is not a diagnostic and survives: toggling must not
 *  silently undo an operator's LIMIT setting mid-comparison. */
static void bus_reset(Audio *audio)
{
    int keep_limit = audio->mix.limit;
    audio_mix_init(&audio->mix, audio->sample_rate);
    audio_mix_set_limit(&audio->mix, keep_limit);
    /* ⚠️ The knee follows the VOLUME, and a fresh bus would otherwise carry
     * audio_gen.h's default while the voices are quieter — a limiter knee'd
     * above one voice is inert, below it bends a lone tone. */
    audio_mix_set_knee(&audio->mix, audio_voice_peak(audio->vol));
    audio->last_tone_slot = -1;
    audio->last_tone_gen  = 0;
    audio->last_tone_ms   = 0;
    audio->pump_starved = 0;
    audio->pump_lost    = 0;
    audio->pump_diag    = 40;
    /* The lead is a MEASUREMENT taken from the device, so a new session starts
     * without one rather than carrying the last session's forward — the panel
     * says "not measured yet" until something has actually looked. */
    audio->pump_lead    = 0;
    audio->pump_period  = 0;
    audio->pumping      = true;
}

void audio_pump_enable(Audio *audio, bool on)
{
    if (!audio) return;
    if (on) {
        if (!audio->pumping) bus_reset(audio);
        return;
    }
    /* ⚠️ Refused LOUDLY on the continuous stream, which needs a fill every
     * service: a stream nobody writes goes idle, and an idle stream is the
     * transition this whole change exists to remove.  The voices are still
     * silenced, so the operator's intent ("stop the sound") is honoured. */
    if (audio->cont) {
        audio_mix_stop_all(&audio->mix);
        fprintf(stderr, "audio: pump_enable(false) refused — the continuous "
                        "stream needs a writer; voices silenced instead (call "
                        "audio_cont_enable(a, false) to take the device back)\n");
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
    if (!audio) return false;
    /* ⚠️ Unconditionally true on the continuous stream: the stream must be
     * serviced whatever the bus is doing, and the ceiling on how long a frame may
     * take is audio_cont_service_interval_us() — which is measured, and below
     * FRAME_DELAY_IDLE_US.  A loop that idles at 100 ms starves this device ~2.5
     * times a second on its own (measured on `.188`). */
    if (audio->cont) return true;
    if (!audio->pumping) return false;
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

void audio_set_volume(Audio *audio, int vol)
{
    if (!audio) return;
    if (vol < 1)               vol = 1;
    if (vol > AUDIO_VOL_UNITY) vol = AUDIO_VOL_UNITY;
    audio->vol = vol;
    /* The knee's whole job is that ONE voice passes unchanged, so it moves with
     * the amplitude rather than being a constant beside it. */
    audio_mix_set_knee(&audio->mix, audio_voice_peak(vol));
}

int audio_get_volume(const Audio *audio) { return audio ? audio->vol : 0; }

void audio_set_master_shift(Audio *audio, int shift)
{
    if (!audio) return;
    if (shift < 0)  shift = 0;
    if (shift > 15) shift = 15;
    audio->master_shift = shift;
    /* ⚠️ Both paths, from one field.  audio_out holds its own copy because
     * ScummVM sets it without an `Audio` at all; this keeps them equal whenever
     * the stream is the live half. */
    if (audio->cont) audio_out_set_shift(&audio->out, shift);
}

int audio_get_master_shift(const Audio *audio) { return audio ? audio->master_shift : 0; }

/** Scratch for one pump call, allocated once and kept.  The pump runs every
 *  frame, so a malloc/free pair per call is the one allocation in this library
 *  worth removing.  (write_mono()'s interleave buffer is the other one; it
 *  belongs to the write path.) */
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

/* ── The continuous stream ───────────────────────────────────────────────────
 * `audio_out.c` owns the fd, the geometry, the prefill, the attenuation stage and
 * the bounded drain.  What belongs in THIS file is only which mono source fills
 * it — so there is one implementation of the device half, and ScummVM's adapter
 * becomes another fill rather than another loop.
 */

/** The mix bus as a fill: render mono, expand to the granted channel count.
 *
 * ⚠️ Returning 0 on a silent bus is CORRECT and costs nothing —
 * `audio_out_service()` has already zeroed the buffer, so a silent bus writes
 * SILENCE rather than writing nothing.  That is the whole fix: a stream allowed
 * to go idle is a stream transition, and a transition is the click.  It also
 * makes the old `keepalive` toggle structural rather than optional here. */
long audio_cont_fill_mix(void *ctx, int16_t *buf, long frames, int channels)
{
    Audio *audio = (Audio *)ctx;
    int16_t *mono = pump_scratch(audio, frames);
    if (!mono) return 0;

    long n = audio_mix_render(&audio->mix, mono, frames);
    if (n <= 0) return 0;
    audio_interleave(mono, n, channels, buf);
    return n;
}

/** The theremin as a fill: one gliding oscillator, the same stream, no reset. */
static long cont_fill_osc(void *ctx, int16_t *buf, long frames, int channels)
{
    Audio *audio = (Audio *)ctx;
    int16_t *mono = pump_scratch(audio, frames);
    if (!mono) return 0;

    audio_osc_render(&audio->osc, AUDIO_OSC_GLIDE, mono, frames);
    audio_interleave(mono, frames, channels, buf);
    return frames;
}

int audio_cont_enable(Audio *audio, bool on)
{
    if (!audio) return -1;
    if (on == audio->cont) return 0;

    if (on) {
        if (!audio->available) return -1;

        /* ⚠️ One at a time.  A second concurrent open of /dev/dsp is refused
         * *Device or resource busy* by the driver, so this file's fd closes
         * before audio_out's opens — and if that fails, the old path comes back
         * rather than leaving the panel silent. */
        if (audio->dsp_fd >= 0) { close(audio->dsp_fd); audio->dsp_fd = -1; }

        if (audio_out_open_oss(&audio->out, TARGET_RATE, FALLBACK_CHANNELS) != 0) {
            fprintf(stderr, "audio: the continuous stream could not take the "
                            "device — restoring the old path\n");
            if (dsp_reopen(audio) < 0) audio->available = false;
            return -1;
        }

        /* ⚠️ The GRANT, not the request: `audio.sample_rate` is the one field a
         * caller outside common/ reads, and every byte count in this file derives
         * from `audio.channels`. */
        audio->sample_rate = audio_out_rate(&audio->out);
        audio->channels    = audio_out_channels(&audio->out);
        audio->cont        = true;
        audio->osc_stream  = false;

        bus_reset(audio);              /* CONT implies PUMP — see audio.h */
        audio_out_set_shift(&audio->out, audio->master_shift);
        audio_out_set_fill(&audio->out, audio_cont_fill_mix, audio, "mix bus");
        return 0;
    }

    /* Off: drain what is queued, then take the device back the old way. */
    audio_out_close(&audio->out);
    audio->cont       = false;
    audio->osc_stream = false;
    audio->pumping    = false;
    audio->streaming  = false;
    if (dsp_reopen(audio) < 0) { audio->available = false; return -1; }
    /* The grant may differ from what audio_out was given, so re-read rather than
     * carrying the stream's numbers into the old path. */
    configure_dsp(audio);
    return 0;
}

bool audio_cont_active(const Audio *audio) { return audio ? audio->cont : false; }

long audio_cont_service_interval_us(const Audio *audio)
{
    return (audio && audio->cont) ? audio_out_service_interval_us(&audio->out) : 0;
}

/** One service of the continuous stream, with the library's counters mirrored
 *  into the ones every existing diagnostic and panel already reads.  ⚠️ Mirrored
 *  rather than duplicated: `audio_out` is the only thing counting, so the two can
 *  never disagree — which a second set of increments here would allow. */
static void cont_service(Audio *audio)
{
    audio_out_service(&audio->out);
    audio->pump_lead    = audio_out_lead(&audio->out);
    audio->pump_period  = audio_out_period(&audio->out);
    audio->pump_starved = audio_out_starved(&audio->out);
    audio->pump_lost    = audio_out_lost(&audio->out);
}

void audio_pump(Audio *audio)
{
    if (!audio) return;
    if (audio->cont) { cont_service(audio); return; }
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
     * name for that period; tinyalsa calls it `period_size`. */
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
    if (!audio_live(audio)) return;

    /* On the bus this is "stop all voices": no ring reset, because the reset is
     * exactly what makes mixing impossible, and no sleep, because there is
     * nothing to wait for.  Whatever is already inside the device still plays —
     * up to AUDIO_PUMP_LEAD_MS on the pump, one lead (~139 ms) on the continuous
     * stream, which cannot un-write what it has already queued.
     *
     * ⚠️ `cont` is tested as well as `pumping` even though CONT implies PUMP: the
     * implication is enforced in audio_cont_enable()/audio_pump_enable() and this
     * must not go quiet if either of those ever grows a path that breaks it.  The
     * cost of the redundant test is nothing; the cost of the missed one is a
     * SNDCTL_DSP_RESET on an fd that is -1, and then silence. */
    if (audio->cont || audio->pumping) {
        audio_mix_stop_all(&audio->mix);
        return;
    }
    audio_flush(audio);
}

void audio_tone(Audio *audio, int freq_hz, int duration_ms)
{
    if (!audio || !audio->available)             return;
    /* The EFFECTS toggle, at the one place every tone in the project passes
     * through — play_sequence() calls this, so the four canned sounds' note-table
     * fallbacks are covered by this line and not by four of their own. */
    if (!audio->effects_on)                      return;
    /* ⚠️ Not `audio_live()`: on the continuous stream this function does not touch
     * the device at all — it adds a voice — so it must work while `dsp_fd` is -1
     * by design, and it must NOT be gated on `audio_out_is_open()` either, since a
     * voice added to the bus is rendered by whoever services next. */
    if (!audio->cont && audio->dsp_fd < 0)       return;
    if (freq_hz <= 0 || duration_ms <= 0)        return;

    /* ⚠️ The two paths are a BRANCH, not "enqueue and also write immediately".
     * The plan sketched the latter; it cannot work.  A bounded immediate write
     * truncates any tone longer than the lead (every tone here is), and an
     * unbounded one hands the whole tone to the kernel — which is the very thing
     * that makes it unmixable.  Branching instead means an app that never calls
     * audio_pump_enable() takes today's path byte for byte, which is a stronger
     * guarantee than "degrades gracefully". */
    if (audio->pumping) {
        /* ⚠️ **The delay defaults to the tail of the PRECEDING TONE** — but only
         * while that tone is RECENT.  Two rules, and each one exists because the
         * other alone was heard to be wrong on the panel.
         *
         * Chaining, first: it is the kernel ring that serialises two back-to-back
         * audio_tone() calls today, and a mix bus will not.  `tetris/tetris.c:620-621`,
         * `tetris/tetris.c:714-715` and `snake/snake.c:317-318` each play two notes
         * with no audio_interrupt() between them, so at delay 0 all three turn from
         * two-note motifs into DYADS.  `AudioVoice.delay` already exists for exactly
         * this — it is what makes audio_success() an arpeggio.
         *
         * ⚠️ **And recency, because chaining unconditionally is how mixing became a
         * QUEUE.**  A tap has no relationship to whatever last happened to make a
         * sound, and with no gate it inherited that sound's whole remaining tail:
         * four spaced taps over one 3 s drone put the last one 3800 ms out, since
         * each tap also chains behind the tap before it (measured,
         * `../tests/audio_tone_test.c` group C).  The operator heard precisely that
         * — *"the audio is still serialized"* — while `audio_success()` overlapped
         * the same drone correctly, because play_sequence() calls audio_mix_add()
         * directly and never sets `last_tone_slot`.  That asymmetry, canned sounds
         * mixing while a plain tone queues, is this defect's fingerprint.
         *
         * ⚠️ **AUDIO_TONE_CHAIN_MS is HALF A FRAME, and the gap it splits is not
         * close.**  A motif's two calls are consecutive statements — microseconds
         * apart, same frame.  An independent tap is a frame away at least:
         * FRAME_DELAY_ACTIVE_US is 33333 (`common.h`), and snake's gameplay frame
         * is 150 ms falling to 50 (`snake.c:26`).  Half a frame is the midpoint of
         * µs and 33 ms, so both sides keep an order of magnitude of margin.  The
         * stamp is the ISSUE time and not the end time, so a third note still
         * chains behind the second rather than being cut loose by the first.
         *
         * ⚠️ And the tail must NOT be `audio_mix_pending()`: that is the worst voice
         * on the whole bus, so one 3 s drone pushed six later taps behind it on the
         * panel — the same symptom this gate fixes, by the other mechanism.
         * audio_mix_voice_pending() names one voice by (slot, generation), so a
         * freed or reused slot reads 0 rather than borrowing whatever moved in.
         *
         * The ~23 `audio_interrupt(); audio_tone();` sites are unaffected either way:
         * the interrupt stops every voice, so the tail it reads is 0 and the tone
         * still starts immediately.  That is the property that keeps "overlapping
         * sounds mix" from silently becoming "overlapping sounds queue". */
        uint32_t now    = time_now_ms();
        bool     recent = (uint32_t)(now - audio->last_tone_ms) <= AUDIO_TONE_CHAIN_MS;
        /* ⚠️ That subtraction is uint32 over a clock audio_ms_from_timeval() masks to
         * 22 bits, so it is not a 2^32 wrap: across the ~48.5-day rollover the delta
         * reads ENORMOUS rather than tiny, and the guard therefore fails CLOSED —
         * one 16 ms window per 48.5 days in which a motif's second note starts on
         * time instead of after its first.  It can never fail the other way, which
         * is the direction that would queue a tap. */
        long     tail_ms = recent
                         ? audio_ms_for_frames(audio->sample_rate,
                                               audio_mix_voice_pending(&audio->mix,
                                                                       audio->last_tone_slot,
                                                                       audio->last_tone_gen))
                         : 0;
        int slot = audio_mix_add(&audio->mix, freq_hz, duration_ms,
                                 (tail_ms > 0) ? (int)tail_ms : 0,
                                 audio_voice_peak(audio->vol));
        if (slot >= 0) {
            audio->last_tone_slot = slot;
            audio->last_tone_gen  = audio_mix_voice_gen(&audio->mix, slot);
            audio->last_tone_ms   = now;
        }
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

    audio_render_tone(audio->sample_rate, freq_hz, audio_voice_peak(audio->vol), mono, frames);
    write_mono(audio, mono, frames, &WPOL_TONE, "tone");
    free(mono);

    /* Record when this tone is expected to finish so audio_flush() can
     * wait before discarding it on the next interrupt call. */
    audio->sound_end_ms = time_now_ms() + (uint32_t)duration_ms;
}

/* ── Streaming (theremin) API ─────────────────────────────────────────────────
 * ⚠️ **Always the continuous stream — there is no old-path branch here.**  This
 * path used to bracket itself with two SNDCTL_DSP_RESETs and own the ring through
 * a chunk loop of its own, which clicked twice per gesture; and its only caller
 * is `tests/audio_touch_test`, so no shipped game's sound changes.
 * audio_stream_start() therefore enters continuous mode itself, and the two write
 * policies that existed only for this path (WPOL_CHUNK, WPOL_PREFILL) are gone.
 */

void audio_stream_start(Audio *audio, int freq_hz)
{
    if (!audio || !audio->available) return;

    /* ⚠️ Refused LOUDLY while the bus still owes audio.  The refusal used to be
     * "the pump is on"; it cannot be that any more, because CONT implies PUMP and
     * this path now turns CONT on itself.  What actually breaks is a QUIET swap:
     * one installed callback means the oscillator replaces the mixer, so voices
     * still pending would sit in a mixer nobody renders and simply vanish. */
    long pending = audio_mix_pending(&audio->mix);
    if (pending > 0) {
        fprintf(stderr, "audio: stream_start refused — the mix bus still owes "
                        "%ld frames and the oscillator would replace it "
                        "(call audio_interrupt() first)\n", pending);
        return;
    }

    /* Enter continuous mode if the caller has not.  A failure here is reported by
     * audio_cont_enable(), which restores the old path rather than leaving the
     * panel silent — so returning is all that is left to do. */
    if (!audio->cont && audio_cont_enable(audio, true) != 0) return;

    /* One oscillator, amplitude 0 so it fades in.  No reset and no 200 ms prime of
     * its own: the stream was prefilled with silence at open and is never reset,
     * which is the whole point of it. */
    audio_osc_init(&audio->osc, audio->sample_rate, (double)freq_hz, audio_voice_peak(audio->vol));
    audio->streaming  = true;
    audio->osc_stream = true;
    audio_out_set_fill(&audio->out, cont_fill_osc, audio, "theremin");

    fprintf(stderr, "audio: stream start at %d Hz (rate=%d, %d ch, continuous)\n",
            freq_hz, audio->sample_rate, audio->channels);
}

void audio_stream_set_freq(Audio *audio, int freq_hz)
{
    if (!audio->streaming) return;
    audio->osc.target_freq = (double)freq_hz;
}

void audio_stream_chunk(Audio *audio)
{
    if (!audio || !audio->streaming) return;
    if (!audio_live(audio))          return;

    /* The stream is serviced, not chunked: one service writes whatever the lead
     * is short by, through the oscillator fill installed at start.  There is no
     * GETOSPACE here and no chunk loop — audio_out_service() owns both. */
    cont_service(audio);
}

void audio_stream_stop(Audio *audio)
{
    if (!audio || !audio->streaming) return;
    if (!audio_live(audio)) {
        audio->streaming  = false;
        audio->osc_stream = false;
        return;
    }

    /* ⚠️ **Remove the fill FIRST, then APPEND the fade.**  The other order cannot
     * work: the fade has to go through audio_out_write() (mode 2), which is
     * refused while a callback is installed — and a fade rendered *through* the
     * fill would be stretched to whatever that service happened to ask for, or
     * skipped entirely, because a queue already at the lead is asked for ZERO
     * frames.
     *
     * ⚠️ The release therefore lands one lead (~139 ms) behind the finger, which is
     * a property of a stream that is never reset rather than a bug: the alternative
     * is the reset, and the reset is the click. */
    audio_out_set_fill(&audio->out, NULL, NULL, NULL);
    audio->osc_stream = false;

    /* AUDIO_OSC_FADE_OUT holds frequency and amplitude still and envelopes 1 → 0
     * over exactly this call — a glide or a ramp during a fade-out fights it. */
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
        audio_out_write(&audio->out, mono, fade_frames);
        free(mono);
    }

    /* ⚠️ The stream must never be left without a fill — a service with no callback
     * writes silence, which is correct, but then audio_tone() would enqueue into a
     * mixer nobody renders.  The mix bus is the default owner and takes it back. */
    audio_out_set_fill(&audio->out, audio_cont_fill_mix, audio, "mix bus");

    audio->streaming    = false;
    audio->osc.amp      = 0.0;
    audio->sound_end_ms = 0;

    fprintf(stderr, "audio: stream stop — 20 ms fade appended, stream still open\n");
}

/* ── Recorded clips in RAM: what the four canned sounds are MADE of ──────────
 *
 * ⚠️ **The point is AUDIBILITY, and the change is CONTENT** — see audio.h's
 * convenience-sounds block for the band measurement that makes a pure tone the
 * wrong signal on this speaker.  No pitch is retuned and no level is raised
 * in the same edit, so an ear verdict names one variable.
 *
 * ⚠️ **The cursor is why these live in RAM.**  `audio_sfx_play()`'s one streaming
 * voice must refuse a retrigger while it sounds, because its `AudioWav` IS the
 * live voice's `ctx`.  A clip's PCM is shared and read-only, so each trigger
 * carries its own position and AUDIO_CLIP_VOICES of them can overlap.
 *
 * The mixer is untouched by all of this: a clip is an `AudioVoiceFill` like the
 * bed's, so everything the bus already guarantees — the full-bus refusal, the
 * envelope, the self-free at `pos >= frames` — applies unchanged.
 */

/** Where each name's clip lives when nothing overrides it, and the sweep each
 *  one carries.  ⚠️ Every one of the four is inside the speaker's passband; four
 *  of the OTHER six stock clips dip below the knee, so if a raw audio_tone() site
 *  ever gets a name here, retune the spec — never the threshold. */
static const char *const FX_DEFAULT_PATH[AUDIO_FX_COUNT] = {
    "/opt/sound/fx_click.wav",     /* BEEP     900 → 1500 Hz */
    "/opt/sound/fx_pickup.wav",    /* BLIP    1200 → 3200 Hz */
    "/opt/sound/fx_success.wav",   /* SUCCESS  700 → 4200 Hz */
    "/opt/sound/fx_fail.wav",      /* FAIL    1800 →  420 Hz */
    /* GAMEOVER: its own sourced clip, added by the operator 2026-08-22 for this
     * id.  ⚠️ Chosen over `fx_burst` on a MEASUREMENT, not on the name: in-band
     * RMS −10.61 dBFS at delta −0.93 dB makes it the loudest-in-band file of the
     * eleven, where burst is −18.61 / −2.71.  The most important sound in a game
     * should not be one of its quietest (`../check-sound-assets.sh` is what
     * re-measures this). */
    "/opt/sound/fx_gameover.wav",  /* GAMEOVER */
    /* The six during-play names.  ⚠️ **This column is `../sounds/prompts.md`'s
     * "game event" column, not a choice made here** — each file was sourced for
     * the site it now backs, and each site went on playing a generated tone
     * until 2026-08-23.  Four of these six dip below the knee as SOURCED
     * (knock ends 260, thud 800→600, burst starts 600, jump starts 500 Hz);
     * that is an argument for re-sourcing the FILE, and the note fallbacks
     * beside them are what got retuned. */
    "/opt/sound/fx_knock.wav",     /* KNOCK    ball off the paddle             */
    "/opt/sound/fx_thud.wav",      /* THUD     brick hit, not destroyed        */
    "/opt/sound/fx_tick.wav",      /* TICK     brick destroyed                 */
    "/opt/sound/fx_sparkle.wav",   /* SPARKLE  bonus brick                     */
    "/opt/sound/fx_burst.wav",     /* BURST    explosive brick                 */
    "/opt/sound/fx_jump.wav"       /* JUMP     jump / stomp                    */
};

/** The config keys that override them, and the word each refusal uses. */
static const char *const FX_CONFIG_KEY[AUDIO_FX_COUNT] = {
    "fx_beep", "fx_blip", "fx_success", "fx_fail", "fx_gameover",
    "fx_knock", "fx_thud", "fx_tick", "fx_sparkle", "fx_burst", "fx_jump"
};
static const char *const FX_NAME[AUDIO_FX_COUNT] = {
    "beep", "blip", "success", "fail", "gameover",
    "knock", "thud", "tick", "sparkle", "burst", "jump"
};

/** Defaults into `fx_path`.  Called from audio_open(), so BOTH entry points get
 *  them — a hardware tab that bypasses the config gate still gets clip content. */
static void fx_defaults(Audio *audio)
{
    for (int i = 0; i < AUDIO_FX_COUNT; i++)
        snprintf(audio->fx_path[i], AUDIO_FX_PATH_MAX, "%s", FX_DEFAULT_PATH[i]);
}

/** Config overrides, applied AFTER audio_open() because that memsets the struct.
 *  A key that is present but EMPTY is a deliberate "use the note table". */
static void fx_config_apply(Audio *audio, Config *cfg)
{
    for (int i = 0; i < AUDIO_FX_COUNT; i++) {
        const char *p = config_get(cfg, FX_CONFIG_KEY[i], NULL);
        if (p) snprintf(audio->fx_path[i], AUDIO_FX_PATH_MAX, "%s", p);
    }
}

/** `AudioVoiceFill` over RAM: the whole mechanism, and it is a memcpy.  A short
 *  return is how the clip ends, which the bus already treats as a normal exit. */
static long clip_fill(void *ctx, int16_t *dst, long frames)
{
    AudioClipVoice *cv = (AudioClipVoice *)ctx;
    if (!cv || !cv->clip || !cv->clip->pcm) return 0;

    long left = cv->clip->frames - cv->pos;
    if (left <= 0) return 0;
    if (frames > left) frames = left;

    memcpy(dst, cv->clip->pcm + cv->pos, (size_t)frames * sizeof(int16_t));
    cv->pos += frames;
    return frames;
}

/** How many voices are still reading `c`.  ⚠️ Asked of the MIXER, not of a flag
 *  of ours: a voice frees itself at `pos >= frames` and nothing tells us. */
static int clip_refs(const Audio *audio, const AudioClip *c)
{
    int n = 0;
    for (int i = 0; i < AUDIO_CLIP_VOICES; i++) {
        const AudioClipVoice *cv = &audio->fxv[i];
        if (cv->clip != c || cv->slot < 0) continue;
        if (audio_mix_voice_pending(&audio->mix, cv->slot, cv->gen) > 0) n++;
    }
    return n;
}

/**
 * Read a whole file into `c`.  Returns true iff `c->pcm` came out non-NULL.
 *
 * Goes through audio_wav.c rather than reading the header here, and that is
 * load-bearing: `data` is NOT at byte 44 in our own files, and a reader that
 * assumes it plays an encoder version string as audio (audio_wav.h).  It also
 * averages a multi-channel file down instead of picking the left channel.
 */
static bool clip_load(Audio *audio, AudioClip *c, const char *path, const char *name)
{
    AudioWav w;
    if (!audio_wav_open(&w, path, false)) {
        fprintf(stderr, "audio: fx %s has no clip at %s — the note table plays instead\n",
                name, path);
        return false;
    }
    /* Same refusal as the bed's, same reason: there is no resampler, and playing
     * 22050 material at 44100 is a pitch bug that sounds like a bad recording. */
    if (w.rate != audio->sample_rate) {
        fprintf(stderr, "audio: fx %s refused — %s is %d Hz, the device granted %d, "
                        "and there is no resampler\n", name, path, w.rate, audio->sample_rate);
        audio_wav_close(&w);
        return false;
    }
    if (w.frames <= 0 || w.frames > AUDIO_CLIP_MAX_FRAMES) {
        /* ⚠️ The guard that stops a MUSIC path being RAM-loaded inside a tap. */
        fprintf(stderr, "audio: fx %s refused — %s is %ld frames, the clip ceiling is "
                        "%ld (a bed STREAMS; audio_music_start() is its path)\n",
                name, path, w.frames, AUDIO_CLIP_MAX_FRAMES);
        audio_wav_close(&w);
        return false;
    }

    int16_t *pcm = (int16_t *)malloc((size_t)w.frames * sizeof(int16_t));
    if (!pcm) {
        fprintf(stderr, "audio: fx %s out of memory for %ld frames\n", name, w.frames);
        audio_wav_close(&w);
        return false;
    }

    /* Loop rather than one read: audio_wav_read() is allowed to return short, and
     * a partial file must end the clip at what is really there. */
    long got = 0;
    while (got < w.frames) {
        long n = audio_wav_read(&w, pcm + got, w.frames - got);
        if (n <= 0) break;
        got += n;
    }
    audio_wav_close(&w);

    if (got <= 0) {
        fprintf(stderr, "audio: fx %s read nothing from %s\n", name, path);
        free(pcm);
        return false;
    }
    c->pcm    = pcm;
    c->frames = got;
    fprintf(stderr, "audio: fx %s loaded %s — %ld of %ld frames, %d Hz, %ld bytes in RAM\n",
            name, path, got, w.frames, w.rate, (long)((size_t)got * sizeof(int16_t)));
    return true;
}

/** The clip for `id`, loading it on first use, or NULL to mean "play the notes". */
static AudioClip *clip_ready(Audio *audio, AudioFxId id)
{
    AudioClip *c = &audio->fx[id];

    if (c->reload) {
        /* The path changed.  Free the old PCM here — the one moment we know no
         * voice is reading it — and fall back to the notes for this one trigger
         * if one still is. */
        if (clip_refs(audio, c) > 0) return NULL;
        free(c->pcm);
        c->pcm    = NULL;
        c->frames = 0;
        c->tried  = false;
        c->reload = false;
    }

    if (!c->tried) {
        c->tried = true;                       /* set BEFORE the load: a miss is
                                                * permanent, so a failure must not
                                                * be retried on the next tap */
        const char *path = audio->fx_path[id];
        if (path && *path) clip_load(audio, c, path, FX_NAME[id]);
    }
    return c->pcm ? c : NULL;
}

bool audio_fx_play(Audio *audio, AudioFxId id)
{
    if (!audio || id < 0 || id >= AUDIO_FX_COUNT) return false;
    /* ⚠️ The EFFECTS toggle returns false here rather than "played", so the caller
     * falls through to its note table — which audio_tone() then refuses too.  Both
     * halves must be gated: gating only the clip would make an OFF toggle swap
     * every effect for a tone instead of silencing it. */
    if (!audio->effects_on) return false;
    /* Off the bus a sample voice cannot exist at all, and that is precisely when
     * the note table must run — so this is a QUIET false, not a complaint. */
    if (!audio->available || !audio->pumping) return false;

    AudioClip *c = clip_ready(audio, id);
    if (!c) return false;

    for (int i = 0; i < AUDIO_CLIP_VOICES; i++) {
        AudioClipVoice *cv = &audio->fxv[i];
        if (cv->slot >= 0 && audio_mix_voice_pending(&audio->mix, cv->slot, cv->gen) > 0)
            continue;                          /* this one is still sounding */

        if (!cv->buf) {
            cv->buf = (int16_t *)malloc((size_t)AUDIO_CLIP_VOICE_BUF_FRAMES * sizeof(int16_t));
            if (!cv->buf) return false;
            cv->buf_frames = AUDIO_CLIP_VOICE_BUF_FRAMES;
        }

        /* ⚠️ The cursor is rewound HERE, before the add — the mixer pulls on the
         * first rendered frame and would otherwise resume where the last trigger
         * of this voice stopped. */
        cv->clip = c;
        cv->pos  = 0;

        int slot = audio_mix_add_sample(&audio->mix, clip_fill, cv,
                                        cv->buf, cv->buf_frames,
                                        c->frames, audio_voice_peak(audio->vol));
        if (slot < 0) return false;             /* full bus: refuses, never steals */
        cv->slot = slot;
        cv->gen  = audio_mix_voice_gen(&audio->mix, slot);
        return true;
    }
    /* All AUDIO_CLIP_VOICES sounding.  Not a fault and not counted: the bus's own
     * drop counter is for the bus, and this is our own pool being busy. */
    return false;
}

void audio_fx_set_path(Audio *audio, AudioFxId id, const char *path)
{
    if (!audio || id < 0 || id >= AUDIO_FX_COUNT) return;
    if (!path) path = "";
    if (strcmp(audio->fx_path[id], path) == 0) return;   /* safe to call per frame */

    snprintf(audio->fx_path[id], AUDIO_FX_PATH_MAX, "%s", path);
    audio->fx[id].reload = true;    /* the free happens at the next trigger */
}

/** Release the clip bank.  Called only from audio_close(), same reason as
 *  sample_discard(): while a voice is pending the mixer is still reading this. */
static void clip_bank_discard(Audio *audio)
{
    for (int i = 0; i < AUDIO_FX_COUNT; i++) {
        free(audio->fx[i].pcm);
        audio->fx[i].pcm    = NULL;
        audio->fx[i].frames = 0;
        audio->fx[i].tried  = false;
        audio->fx[i].reload = false;
    }
    for (int i = 0; i < AUDIO_CLIP_VOICES; i++) {
        free(audio->fxv[i].buf);
        audio->fxv[i].buf        = NULL;
        audio->fxv[i].buf_frames = 0;
        audio->fxv[i].clip       = NULL;
        audio->fxv[i].slot       = -1;
    }
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
            audio_mix_add(&audio->mix, notes[i].freq, notes[i].ms, delay, audio_voice_peak(audio->vol));
            delay += notes[i].ms;
        }
        return;
    }

    audio_flush(audio);                 /* discard any queued audio first */
    for (int i = 0; i < count; i++)
        audio_tone(audio, notes[i].freq, notes[i].ms);
}

/** 880 Hz, 80 ms — UI click / tile place.  Clip: fx_click, 900→1500 Hz. */
void audio_beep(Audio *audio)
{
    static const AudioNote s[] = { { 880, 80 } };
    if (audio_fx_play(audio, AUDIO_FX_BEEP)) return;
    play_sequence(audio, s, 1);
}

/** 1320 Hz, 60 ms — item collected, food eaten.  Clip: fx_pickup, 1200→3200 Hz. */
void audio_blip(Audio *audio)
{
    static const AudioNote s[] = { { 1320, 60 } };
    if (audio_fx_play(audio, AUDIO_FX_BLIP)) return;
    play_sequence(audio, s, 1);
}

/** C5 → E5 → G5 ascending arpeggio — score milestone, level up.
 *  Clip: fx_success, 700→4200 Hz — which also answers the arpeggio-not-chord
 *  question by removing it: the clip is one sound, not three notes. */
void audio_success(Audio *audio)
{
    static const AudioNote s[] = { { 523, 120 }, { 659, 120 }, { 784, 220 } };
    if (audio_fx_play(audio, AUDIO_FX_SUCCESS)) return;
    play_sequence(audio, s, 3);
}

/** G4 → E4 → C4 descending — game over, error.  ⚠️ **All three notes are BELOW
 *  the speaker's knee** and this is the sound the operator reported inaudible
 *  under a music bed on 2026-08-21.  Clip: fx_fail, 1800→420 Hz. */
void audio_fail(Audio *audio)
{
    static const AudioNote s[] = { { 392, 150 }, { 330, 150 }, { 262, 300 } };
    if (audio_fx_play(audio, AUDIO_FX_FAIL)) return;
    play_sequence(audio, s, 3);
}

/** The run is over — a different sound from losing one life, because they were
 *  the same one and a player could not hear which had happened (operator,
 *  2026-08-22).  ⚠️ Every note is ABOVE the ~700 Hz knee, unlike audio_fail()'s
 *  three: this fallback had no shipped history to preserve, so it was tuned to
 *  the speaker from the start rather than inheriting a musical descent that the
 *  hardware cannot radiate.  Clip: fx_gameover — its OWN sourced file, and the
 *  loudest-in-band of the eleven; the "fx_burst" this line used to name was the
 *  candidate it was chosen OVER, on the measurement recorded above. */
void audio_gameover(Audio *audio)
{
    static const AudioNote s[] = { { 1046, 140 }, { 880, 140 }, { 784, 140 }, { 740, 260 } };
    if (audio_fx_play(audio, AUDIO_FX_GAMEOVER)) return;
    play_sequence(audio, s, 4);
}

/* ── The six during-play effects ─────────────────────────────────────────────
 *
 * ⚠️ **Each of these replaced a raw `audio_tone()` at a site whose FILE was
 * already sourced and already deployed** — `../sounds/prompts.md` names the
 * event for every one of the eleven, and these six named nothing in C, so the
 * panel played a generated pitch and the recording was dead weight in
 * `/opt/sound`.  Reported by ear on `.188` 2026-08-23; invisible to every gate,
 * because `../check-sound-assets.sh` measured each file's format and its energy
 * and nothing asked whether a name pointed at it.  It does now.
 *
 * ⚠️ **The note tables are RETUNED, not transplanted.**  `brick_breaker` played
 * 600 / 800 / 1000 / 1200 / 1500 Hz and the first two are at or under the ~700 Hz
 * knee, so a device with no clip files heard the two most frequent effects
 * barely or not at all.  The order is preserved because it is the information a
 * player gets — a paddle bounce stays below a thud stays below a break — while
 * every pitch is lifted to the ≥880 Hz band this speaker actually radiates
 * ([§3.4](../SYSTEM_ANALYSIS.md#34-audio)).  ⚠️ Durations are UNCHANGED: they
 * are what the sites' feel was tuned on, and length is not the audibility axis.
 */

/** Ball off the paddle.  Was a 600 Hz tone — under the knee.  Clip: fx_knock. */
void audio_knock(Audio *audio)
{
    static const AudioNote s[] = { { 880, 30 } };
    if (audio_fx_play(audio, AUDIO_FX_KNOCK)) return;
    play_sequence(audio, s, 1);
}

/** A hit that did NOT destroy its target.  Was 800 Hz.  Clip: fx_thud. */
void audio_thud(Audio *audio)
{
    static const AudioNote s[] = { { 988, 20 } };
    if (audio_fx_play(audio, AUDIO_FX_THUD)) return;
    play_sequence(audio, s, 1);
}

/** A brick destroyed.  Was 1200 Hz — already in band, lifted for order.
 *  Clip: fx_tick. */
void audio_tick(Audio *audio)
{
    static const AudioNote s[] = { { 1319, 25 } };
    if (audio_fx_play(audio, AUDIO_FX_TICK)) return;
    play_sequence(audio, s, 1);
}

/** A bonus brick or rare reward.  Was 1500 Hz.  Clip: fx_sparkle. */
void audio_sparkle(Audio *audio)
{
    static const AudioNote s[] = { { 1568, 30 } };
    if (audio_fx_play(audio, AUDIO_FX_SPARKLE)) return;
    play_sequence(audio, s, 1);
}

/** An explosion or chain detonation.  Was 1000 Hz.  Clip: fx_burst. */
void audio_burst(Audio *audio)
{
    static const AudioNote s[] = { { 1175, 40 } };
    if (audio_fx_play(audio, AUDIO_FX_BURST)) return;
    play_sequence(audio, s, 1);
}

/** A jump or stomp.  ⚠️ Its site called `audio_beep()` — a UI tap — so this one
 *  replaces a WRONG NAME rather than a raw tone, and the fallback keeps beep's
 *  in-band character.  Clip: fx_jump. */
void audio_jump(Audio *audio)
{
    static const AudioNote s[] = { { 1046, 40 } };
    if (audio_fx_play(audio, AUDIO_FX_JUMP)) return;
    play_sequence(audio, s, 1);
}

/* ── Recorded PCM: the bed and the sample effect ─────────────────────────────
 *
 * ⚠️ **This is the DEVICE half of the sample voice and it owns the fd — the
 * mixer never does.**  `audio_gen.c` has no `read()`, no `open()` and no clock;
 * it pulls through an `AudioVoiceFill`, and the thing on the far end of that
 * callback is `common/audio_wav.c` with its `FILE *` living in the
 * `AudioSampleVoice` below.  That split is what keeps the mixer host-testable
 * with no shim.
 *
 * Shaped after play_sequence(), NOT after audio_tone(): there is no
 * `last_tone_*` recency state here and therefore no wall-clock branch, so a
 * harness with a clock of its own cannot make this code take a different path
 * from the one being claimed — the trap that cost an earlier test a rewrite.
 */

/** Ceiling on a looping bed's declared length, in frames.  Safely under a
 *  32-bit `long`'s 2147483647, with room for the mixer's own arithmetic. */
#define AUDIO_SAMPLE_MAX_TOTAL   1500000000L

/** What to declare as the voice's length.  ⚠️ **A loop is a large FINITE total,
 *  not a sentinel**: `audio_mix_render()` early-outs on
 *  `audio_mix_pending() <= 0`, so a voice that under-reports is never rendered
 *  at all, and one that overflows to negative is refused outright — both silent. */
static long sample_total_frames(long frames, bool loop)
{
    if (frames <= 0) return 0;
    if (!loop)       return frames;
    /* 64-bit multiply on purpose: `frames * 200` is a 32-bit multiply on this
     * target, and a track long enough wraps NEGATIVE rather than saturating. */
    long long total = (long long)frames * AUDIO_MUSIC_LOOP_PASSES;
    if (total > AUDIO_SAMPLE_MAX_TOTAL) total = AUDIO_SAMPLE_MAX_TOTAL;
    return (long)total;
}

/** Does this voice still owe frames?  ⚠️ **Asked of the MIXER by (slot,
 *  generation), never of a flag here.**  PUMP: OFF runs bus_reset(), which
 *  clears every voice without telling this file — a local `playing` bool would
 *  then be stuck true forever and refuse every restart.  A freed or reused slot
 *  reads 0 through the generation, so this cannot borrow somebody else's voice. */
static bool sample_live(const Audio *audio, const AudioSampleVoice *sv)
{
    return sv->slot >= 0 &&
           audio_mix_voice_pending(&audio->mix, sv->slot, sv->gen) > 0;
}

/*
 * Hand `sv`'s ALREADY-OPEN file to the bus.  `how` is the word the receipt uses.
 *
 * ⚠️ **Split out of sample_start() because a RESUME must not reopen.**
 * audio_music_resume() re-arms a voice over the same `AudioWav` at its own read
 * position, and reopening would restart the track from the top — which is the
 * whole difference between "pause" and "stop".  `path` is NULL for that case;
 * `AudioWav` does not remember where it came from.
 */
static bool sample_arm(Audio *audio, AudioSampleVoice *sv, const char *what,
                       const char *how, const char *path)
{
    /* Allocated once and kept: the buffer sets how often the SD read is entered,
     * and reallocating it per start would put a malloc in a tap's path. */
    if (!sv->buf) {
        sv->buf = (int16_t *)malloc((size_t)AUDIO_SAMPLE_BUF_FRAMES * sizeof(int16_t));
        if (!sv->buf) {
            fprintf(stderr, "audio: %s out of memory for %d read-ahead frames\n",
                    what, AUDIO_SAMPLE_BUF_FRAMES);
            audio_wav_close(&sv->wav);
            return false;
        }
        sv->buf_frames = AUDIO_SAMPLE_BUF_FRAMES;
    }

    /* ⚠️ What is LEFT of the file, not all of it.  On a fresh open `pos` is 0 and
     * the two are the same expression; a RESUMED voice that declared the whole
     * file would outlive its data and the mixer would pad the tail with silence. */
    long total = sample_total_frames(sv->wav.frames - sv->wav.pos, sv->wav.loop);
    int  slot  = audio_mix_add_sample(&audio->mix, audio_wav_fill, &sv->wav,
                                      sv->buf, sv->buf_frames, total,
                                      audio_voice_peak(audio->vol));
    if (slot < 0) {
        /* ⚠️ The full bus REFUSES and never steals (audio_gen.h) — one mechanism
         * for both voice kinds, which is what keeps a UI blip from cutting the bed.
         * Reported so a refused bed is distinguishable from a missing file. */
        fprintf(stderr, "audio: %s refused by the bus — %d of %d voices sounding "
                        "(the full bus refuses, it never steals)\n",
                what, audio_pump_voices(audio), AUDIO_MAX_VOICES);
        audio_wav_close(&sv->wav);
        return false;
    }

    sv->slot = slot;
    sv->gen  = audio_mix_voice_gen(&audio->mix, slot);
    sv->held = false;
    fprintf(stderr, "audio: %s %s %s — %ld frames in file, %ld consumed, "
                    "%ld declared, %d Hz %d ch, loop=%d, slot %d gen %lu\n",
            what, how, path ? path : "(the held file)", sv->wav.frames,
            sv->wav.pos, total, sv->wav.rate, sv->wav.channels,
            (int)sv->wav.loop, slot, (unsigned long)sv->gen);
    return true;
}

/** The one start path for both sample voices.  Every refusal is LOUD: on the
 *  panel a silent no-op reads as "the file is broken", which is the wrong repair. */
static bool sample_start(Audio *audio, AudioSampleVoice *sv, const char *path,
                         bool loop, const char *what)
{
    if (!audio->available || !path || !*path) return false;

    /* ⚠️ Refused off the bus rather than routed round it.  There IS no other path:
     * the old one renders a whole tone and hands it to the kernel, so a 44 s bed
     * sent that way is unmixable and uninterruptible by construction.  `cont` is
     * not tested separately because CONT implies PUMP, enforced in
     * audio_cont_enable(). */
    if (!audio->pumping) {
        fprintf(stderr, "audio: %s refused — the mix bus is OFF and a sample voice "
                        "exists only on the bus (audio_pump_enable() first)\n", what);
        return false;
    }

    /* ⚠️ One AudioWav per voice, and it IS the live voice's `ctx`: reopening it
     * under the mixer would make the sound jump rather than retrigger. */
    if (sample_live(audio, sv)) {
        fprintf(stderr, "audio: %s refused — the previous %s still owes %ld frames "
                        "and its file is that voice's ctx\n", what, what,
                audio_mix_voice_pending(&audio->mix, sv->slot, sv->gen));
        return false;
    }

    audio_wav_close(&sv->wav);          /* a restart must not leak the old fd */
    if (!audio_wav_open(&sv->wav, path, loop)) {
        fprintf(stderr, "audio: %s cannot open %s\n", what, path);
        return false;
    }

    /* ⚠️ No resampler, and no plan for one.  Playing 22050 material at 44100 is a
     * pitch bug that sounds like a bad recording — the hardest audio fault to
     * attribute — so failing is cheaper than guessing. */
    if (sv->wav.rate != audio->sample_rate) {
        fprintf(stderr, "audio: %s refused — %s is %d Hz, the device granted %d, "
                        "and there is no resampler\n",
                what, path, sv->wav.rate, audio->sample_rate);
        audio_wav_close(&sv->wav);
        return false;
    }

    return sample_arm(audio, sv, what, "start", path);
}

/*
 * Release the bed's voice but keep its file open where it is.
 *
 * ⚠️ Deliberately the SAME release as audio_music_stop() rather than a mixer
 * "paused" flag: a held voice would go on occupying a slot the full bus refuses
 * over, and there is no second mechanism to keep in step with this one.
 */
bool audio_music_pause(Audio *audio)
{
    if (!audio) return false;
    if (!audio_music_active(audio)) {
        fprintf(stderr, "audio: music pause — nothing is sounding\n");
        return false;
    }
    audio_music_stop(audio);       /* release, and it does NOT close the file */
    audio->music.held = true;
    return true;
}

bool audio_music_resume(Audio *audio)
{
    if (!audio) return false;
    if (!audio->music.held || !audio->music.wav.f) {
        fprintf(stderr, "audio: music resume refused — no bed is held "
                        "(audio_music_pause() first, or start one)\n");
        return false;
    }
    /* ⚠️ The release must have FINISHED.  The mixer still pulls from this exact
     * AudioWav while the envelope walks down, so re-arming now would give the bus
     * two voices reading one file — and the pump is what finishes it, which a
     * caller that stopped servicing the bus while "paused" never does. */
    if (audio_music_active(audio)) {
        fprintf(stderr, "audio: music resume refused — the previous release still "
                        "owes %ld frames; keep pumping and retry\n",
                audio_mix_voice_pending(&audio->mix, audio->music.slot,
                                        audio->music.gen));
        return false;
    }
    if (!audio->pumping) {
        fprintf(stderr, "audio: music resume refused — the mix bus is OFF\n");
        return false;
    }
    return sample_arm(audio, &audio->music, "music", "resume", NULL);
}

bool audio_music_start(Audio *audio, const char *path, bool loop)
{
    if (!audio) return false;
    /* The MUSIC toggle.  Quiet, because a game's bed state machine reads this as
     * "no bed" and stops asking — audio_music_enabled() is what it should print. */
    if (!audio->music_on) return false;
    return sample_start(audio, &audio->music, path, loop, "music");
}

void audio_music_stop(Audio *audio)
{
    if (!audio || audio->music.slot < 0) return;
    /* ⚠️ Release, not cut.  audio_mix_release_voice() shortens the voice to
     * `pos + release` so the existing envelope walks it to exactly 0; clearing
     * `active` instead steps the bus by the bed's whole amplitude, which is a
     * click.  The file and the buffer stay — the mixer pulls from them until the
     * fade ends, and audio_close() is what releases them. */
    if (audio_mix_release_voice(&audio->mix, audio->music.slot, audio->music.gen))
        fprintf(stderr, "audio: music stop — release armed on slot %d, "
                        "%ld frames into pass %ld\n",
                audio->music.slot, audio->music.wav.pos, audio->music.wav.loops);
    else
        fprintf(stderr, "audio: music stop — slot %d was already gone\n",
                audio->music.slot);
}

bool audio_music_active(const Audio *audio)
{
    return audio ? sample_live(audio, &audio->music) : false;
}

bool audio_sfx_play(Audio *audio, const char *path)
{
    if (!audio) return false;
    if (!audio->effects_on) return false;   /* the EFFECTS toggle */
    return sample_start(audio, &audio->sfx, path, false, "sfx");
}

bool audio_music_enabled(const Audio *audio)
{
    return audio && audio->music_on;
}

bool audio_effects_enabled(const Audio *audio)
{
    return audio && audio->effects_on;
}
