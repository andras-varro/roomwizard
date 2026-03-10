#include "audio.h"
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

/* ── Hardware constants ─────────────────────────────────────────────────── */

#define GPIO12_DIRECTION  "/sys/class/gpio/gpio12/direction"
#define GPIO12_VALUE      "/sys/class/gpio/gpio12/value"
#define DSP_DEVICE        "/dev/dsp"

/** Sample rate requested from the OSS driver.
 *  The ALSA OSS shim SRCs internally to the TWL4030's native 48000 Hz. */
#define TARGET_RATE       44100

/** Peak amplitude for synthesised waveforms (0–32767). */
#define AMPLITUDE         18000

/** Streaming chunk size in milliseconds (~10 ms of audio per chunk) */
#define STREAM_CHUNK_MS     10

/** Peak amplitude for streaming (same as tone amplitude) */
#define STREAM_AMPLITUDE    18000

/** Frequency smoothing factor per sample (higher = faster response) */
#define FREQ_SMOOTH         0.15

/** Amplitude ramp speed per sample for fade-in/fade-out */
#define AMP_RAMP_SPEED      0.0005

/** Maximum chunk/fade frames to prevent VLA stack overflow (200ms at 44100 Hz) */
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

/** Millisecond wall clock (monotonic). */
static uint32_t time_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* Avoid epoch overflow on 32-bit ARM: use low 22 bits of tv_sec (~49 days). */
    return (uint32_t)((tv.tv_sec & 0x3FFFFF) * 1000U + (uint32_t)(tv.tv_usec / 1000));
}

/**
 * (Re)configure DSP format/rate/channels on an already-open fd.
 *
 * ALSA OSS shim quirk on Linux 4.14.52 / TWL4030:
 *   SNDCTL_DSP_SPEED may reset FMT and CHANNELS.
 *   Set order must be SPEED → FMT → CHANNELS so the last two survive.
 * The SNDCTL_DSP_STEREO ioctl is silently ignored on this hardware;
 * the device stays mono but both L+R channels carry the same sample
 * so stereo writes work correctly.
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
    uint32_t now = time_now_ms();
    uint32_t end = audio->sound_end_ms;
    if (end > now) {
        uint32_t wait = end - now;
        if (wait > FLUSH_MAX_WAIT_MS) wait = FLUSH_MAX_WAIT_MS;
        usleep(wait * 1000U);
    }
    /* Reset the ring to prevent tone mixing. */
    ioctl(audio->dsp_fd, SNDCTL_DSP_RESET, 0);
    configure_dsp(audio);
    audio->sound_end_ms = 0;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int audio_init(Audio *audio)
{
    memset(audio, 0, sizeof(*audio));
    audio->dsp_fd       = -1;
    audio->available    = false;
    audio->sample_rate  = TARGET_RATE;
    audio->sound_end_ms = 0;
    audio->phase        = 0.0;
    audio->current_freq = 0.0;
    audio->target_freq  = 0.0;
    audio->amplitude    = 0.0;
    audio->streaming    = false;
    audio->logger       = NULL;

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

    enable_amp();

    /*
     * O_NONBLOCK is critical: a blocking write() stalls for the full
     * ALSA HW period (~506 ms) once the OSS ring fills, causing every
     * subsequent rapid sound event to play hundreds of ms late.
     * With O_NONBLOCK, write() returns EAGAIN when the ring is full and
     * we sleep 5 ms and retry, following the ring at real-time pace.
     */
    audio->dsp_fd = open(DSP_DEVICE, O_WRONLY | O_NONBLOCK);
    if (audio->dsp_fd < 0) {
        perror("audio: cannot open " DSP_DEVICE);
        return -1;
    }

    configure_dsp(audio);

    audio->available = true;
    printf("audio: %s opened at %d Hz S16LE (O_NONBLOCK)\n",
           DSP_DEVICE, audio->sample_rate);
    return 0;
}

void audio_close(Audio *audio)
{
    if (audio->dsp_fd >= 0) {
        close(audio->dsp_fd);
        audio->dsp_fd = -1;
    }
    audio->available = false;
}

void audio_interrupt(Audio *audio)
{
    if (!audio->available || audio->dsp_fd < 0) return;
    audio_flush(audio);
}

void audio_tone(Audio *audio, int freq_hz, int duration_ms)
{
    if (!audio->available || audio->dsp_fd < 0) return;
    if (freq_hz <= 0 || duration_ms <= 0)        return;

    int     frames    = (int)((long)audio->sample_rate * duration_ms / 1000);
    int16_t *buf      = (int16_t *)malloc((size_t)frames * 4); /* stereo S16LE */
    if (!buf) return;

    /* 10 ms attack, 20 ms release — removes clicks at tone boundaries */
    int attack_frames  = audio->sample_rate * 10 / 1000;
    int release_frames = audio->sample_rate * 20 / 1000;
    if (attack_frames  > frames / 2) attack_frames  = frames / 2;
    if (release_frames > frames / 2) release_frames = frames / 2;

    double phase_step = 2.0 * M_PI * freq_hz / audio->sample_rate;
    double phase      = 0.0;

    for (int i = 0; i < frames; i++) {
        double env = 1.0;
        if (i < attack_frames)
            env = (double)i / attack_frames;
        else if (i >= frames - release_frames)
            env = (double)(frames - 1 - i) / release_frames;

        int16_t sample   = (int16_t)(AMPLITUDE * env * sin(phase));
        buf[i * 2]     = sample;   /* L */
        buf[i * 2 + 1] = sample;   /* R */

        phase += phase_step;
        if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
    }

    /*
     * Non-blocking write loop.
     * write() returns EAGAIN when the OSS ring is momentarily full;
     * sleep 5 ms and retry so the ring drains at the hardware rate.
     */
    ssize_t written = 0;
    ssize_t total   = (ssize_t)frames * 4;
    const uint8_t *ptr = (const uint8_t *)buf;
    while (written < total) {
        ssize_t r = write(audio->dsp_fd, ptr + written, (size_t)(total - written));
        if (r > 0) {
            written += r;
        } else if (r < 0 && errno == EAGAIN) {
            usleep(5000);   /* 5 ms — wait for OSS ring to drain a fragment */
        } else {
            break;          /* real error, give up */
        }
    }

    free(buf);

    /* Record when this tone is expected to finish so audio_flush() can
     * wait before discarding it on the next interrupt call. */
    audio->sound_end_ms = time_now_ms() + (uint32_t)duration_ms;
}

/* ── Streaming (theremin) API ───────────────────────────────────────────── */

void audio_stream_start(Audio *audio, int freq_hz)
{
    if (!audio->available || audio->dsp_fd < 0) return;

    /* Reset DSP and configure with default OSS buffer.
     * Note: SNDCTL_DSP_SETFRAGMENT is unreliable on the TWL4030 ALSA OSS
     * shim (Linux 4.14.52) — it can leave the DSP in a bad state where
     * writes silently fail.  We use the default ~500ms OSS buffer instead. */
    if (ioctl(audio->dsp_fd, SNDCTL_DSP_RESET, 0) < 0) {
        fprintf(stderr, "audio: SNDCTL_DSP_RESET failed in stream_start (errno=%d)\n", errno);
        /* Continue anyway — configure_dsp will re-set params */
    }
    configure_dsp(audio);

    /* Initialize streaming state */
    audio->phase        = 0.0;
    audio->current_freq = (double)freq_hz;
    audio->target_freq  = (double)freq_hz;
    audio->amplitude    = 0.0;   /* will fade in */
    audio->streaming    = true;

    /* Pre-fill ~200ms of audio in blocking mode to prime the OSS ring buffer
     * past the TWL4030 DAC startup latency.  Without this, the first few
     * streaming chunks may be silently discarded, causing an audible gap. */
    {
        int prefill_frames = audio->sample_rate * 200 / 1000;
        int16_t *prefill = (int16_t *)malloc((size_t)prefill_frames * 2 * sizeof(int16_t));
        if (prefill) {
            for (int i = 0; i < prefill_frames; i++) {
                audio->current_freq += (audio->target_freq - audio->current_freq) * FREQ_SMOOTH;
                if (audio->amplitude < 1.0) {
                    audio->amplitude += AMP_RAMP_SPEED;
                    if (audio->amplitude > 1.0) audio->amplitude = 1.0;
                }
                double phase_step = 2.0 * M_PI * audio->current_freq / audio->sample_rate;
                int16_t sample = (int16_t)(STREAM_AMPLITUDE * audio->amplitude * sin(audio->phase));
                prefill[i * 2]     = sample;
                prefill[i * 2 + 1] = sample;
                audio->phase += phase_step;
                if (audio->phase > 2.0 * M_PI) audio->phase -= 2.0 * M_PI;
            }

            /* Blocking write loop — retry on EAGAIN to ensure the full
             * prefill reaches the driver before we return to the main loop. */
            ssize_t total   = (ssize_t)prefill_frames * 4;
            ssize_t written = 0;
            const uint8_t *ptr = (const uint8_t *)prefill;
            while (written < total) {
                ssize_t r = write(audio->dsp_fd, ptr + written, (size_t)(total - written));
                if (r > 0) {
                    written += r;
                } else if (r < 0 && errno == EAGAIN) {
                    usleep(1000);
                } else {
                    break;
                }
            }
            free(prefill);
        }
    }

    fprintf(stderr, "audio: stream start at %d Hz (rate=%d)\n", freq_hz, audio->sample_rate);
}

void audio_stream_set_freq(Audio *audio, int freq_hz)
{
    if (!audio->streaming) return;
    audio->target_freq = (double)freq_hz;
}

void audio_stream_chunk(Audio *audio)
{
    if (!audio->available || !audio->streaming) return;

    /* Query available space in the OSS ring buffer */
    audio_buf_info info;
    if (ioctl(audio->dsp_fd, SNDCTL_DSP_GETOSPACE, &info) < 0)
        return;

    int chunk_frames = audio->sample_rate * STREAM_CHUNK_MS / 1000;
    int chunk_bytes  = chunk_frames * 2 * (int)sizeof(int16_t); /* stereo S16LE */

    /* Write as many small chunks as fit in the available buffer space.
     * Cap at some reasonable max to avoid spending too long here. */
    int max_chunks = 8;  /* at most 80ms of audio per call */
    int chunks_written = 0;

    while (info.bytes >= chunk_bytes && chunks_written < max_chunks) {
        int16_t buf[MAX_CHUNK_FRAMES * 2];

        /* Generate one small chunk with current frequency */
        for (int i = 0; i < chunk_frames; i++) {
            /* Smooth frequency toward target */
            audio->current_freq += (audio->target_freq - audio->current_freq) * FREQ_SMOOTH;

            /* Ramp amplitude */
            if (audio->amplitude < 1.0) {
                audio->amplitude += AMP_RAMP_SPEED;
                if (audio->amplitude > 1.0) audio->amplitude = 1.0;
            }

            double phase_step = 2.0 * M_PI * audio->current_freq / audio->sample_rate;
            int16_t sample = (int16_t)(STREAM_AMPLITUDE * audio->amplitude * sin(audio->phase));
            buf[i * 2]     = sample;
            buf[i * 2 + 1] = sample;

            audio->phase += phase_step;
            if (audio->phase > 2.0 * M_PI)
                audio->phase -= 2.0 * M_PI;
        }

        /* Blocking write for this small chunk — it fits, so it should succeed */
        ssize_t total = (ssize_t)chunk_bytes;
        ssize_t written = 0;
        uint8_t *ptr = (uint8_t *)buf;
        while (written < total) {
            ssize_t r = write(audio->dsp_fd, ptr + written, (size_t)(total - written));
            if (r > 0) {
                written += r;
            } else if (r < 0 && (errno == EAGAIN || errno == EINTR)) {
                break;  /* shouldn't happen since we checked space, but be safe */
            } else {
                break;  /* error */
            }
        }

        if (written < total) {
            /* Partial write — shouldn't happen with space check, but stop writing */
            break;
        }

        info.bytes -= (int)written;
        chunks_written++;
    }
}

void audio_stream_stop(Audio *audio)
{
    if (!audio->available || !audio->streaming) return;

    /* Write a short fade-out chunk to avoid click/pop */
    int fade_frames = audio->sample_rate * 20 / 1000;  /* 20 ms fade-out */
    /* Sanity clamp: prevent VLA stack overflow if sample_rate is corrupted */
    if (fade_frames < 1) fade_frames = 1;
    if (fade_frames > MAX_CHUNK_FRAMES) {
        fprintf(stderr, "audio: fade_frames=%d exceeds max, clamping to %d (sample_rate=%d)\n",
                fade_frames, MAX_CHUNK_FRAMES, audio->sample_rate);
        fade_frames = MAX_CHUNK_FRAMES;
    }
    int16_t buf[fade_frames * 2];

    for (int i = 0; i < fade_frames; i++) {
        double env = 1.0 - (double)i / fade_frames;
        double phase_step = 2.0 * M_PI * audio->current_freq / audio->sample_rate;
        int16_t sample = (int16_t)(STREAM_AMPLITUDE * env * audio->amplitude * sin(audio->phase));
        buf[i * 2]     = sample;
        buf[i * 2 + 1] = sample;

        audio->phase += phase_step;
        if (audio->phase > 2.0 * M_PI) audio->phase -= 2.0 * M_PI;
    }

    /* Best-effort blocking write for fade-out — bail after max retries
     * to prevent infinite hang if DSP stops accepting data */
    ssize_t total   = (ssize_t)(fade_frames * 4);
    ssize_t written = 0;
    const uint8_t *ptr = (const uint8_t *)buf;
    int retries = 0;
    const int max_retries = 100;  /* 100 × 2ms = 200ms max */


    while (written < total) {
        ssize_t r = write(audio->dsp_fd, ptr + written, (size_t)(total - written));
        if (r > 0) {
            written += r;
            retries = 0;  /* reset on progress */
        } else if (r == 0) {
            fprintf(stderr, "audio: stream_stop write() returned 0, skipping fade-out\n");
            break;
        } else if (errno == EAGAIN) {
            retries++;
            if (retries >= max_retries) {
                fprintf(stderr, "audio: stream_stop fade-out timed out after %d retries (%d/%d bytes written)\n",
                        max_retries, (int)written, (int)total);
                break;
            }
            usleep(2000);
        } else {
            fprintf(stderr, "audio: stream_stop write() error (errno=%d: %s), skipping fade-out\n",
                    errno, strerror(errno));
            break;
        }
    }

    /* Reset DSP to flush remaining buffer */
    if (ioctl(audio->dsp_fd, SNDCTL_DSP_RESET, 0) < 0) {
        fprintf(stderr, "audio: SNDCTL_DSP_RESET failed in stream_stop (errno=%d)\n", errno);
    }
    configure_dsp(audio);

    audio->streaming    = false;
    audio->amplitude    = 0.0;
    audio->sound_end_ms = 0;

    fprintf(stderr, "audio: stream stop complete\n");
}

/* ── Convenience sounds ─────────────────────────────────────────────────── */

void audio_beep(Audio *audio)
{
    if (!audio->available) return;
    audio_flush(audio);             /* discard any queued audio first */
    /* 880 Hz, 80 ms — UI click / tile place */
    audio_tone(audio, 880, 80);
}

void audio_blip(Audio *audio)
{
    if (!audio->available) return;
    audio_flush(audio);
    /* 1320 Hz, 60 ms — item collected, food eaten */
    audio_tone(audio, 1320, 60);
}

void audio_success(Audio *audio)
{
    if (!audio->available) return;
    audio_flush(audio);
    /* C5 → E5 → G5 ascending arpeggio — score milestone, level up */
    audio_tone(audio, 523, 120);
    audio_tone(audio, 659, 120);
    audio_tone(audio, 784, 220);
}

void audio_fail(Audio *audio)
{
    if (!audio->available) return;
    audio_flush(audio);
    /* G4 → E4 → C4 descending — game over, error */
    audio_tone(audio, 392, 150);
    audio_tone(audio, 330, 150);
    audio_tone(audio, 262, 300);
}
