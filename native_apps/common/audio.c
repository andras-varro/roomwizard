#include "audio.h"

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
