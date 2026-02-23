#include "audio.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

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

/* ── Public API ─────────────────────────────────────────────────────────── */

int audio_init(Audio *audio)
{
    memset(audio, 0, sizeof(*audio));
    audio->dsp_fd      = -1;
    audio->available   = false;
    audio->sample_rate = TARGET_RATE;

    enable_amp();

    audio->dsp_fd = open(DSP_DEVICE, O_WRONLY);
    if (audio->dsp_fd < 0) {
        perror("audio: cannot open " DSP_DEVICE);
        return -1;
    }

    /* 16-bit signed little-endian */
    int fmt = AFMT_S16_LE;
    ioctl(audio->dsp_fd, SNDCTL_DSP_SETFMT, &fmt);

    /* Stereo output */
    int stereo = 1;
    ioctl(audio->dsp_fd, SNDCTL_DSP_STEREO, &stereo);

    /* Sample rate — driver may return a slightly adjusted value */
    int rate = TARGET_RATE;
    ioctl(audio->dsp_fd, SNDCTL_DSP_SPEED, &rate);
    audio->sample_rate = rate;

    audio->available = true;
    printf("audio: %s opened at %d Hz stereo S16LE\n", DSP_DEVICE, audio->sample_rate);
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

    /* Blocking write — returns once the kernel buffer drains to the DAC */
    ssize_t written = 0;
    ssize_t total   = (ssize_t)frames * 4;
    const uint8_t *ptr = (const uint8_t *)buf;
    while (written < total) {
        ssize_t r = write(audio->dsp_fd, ptr + written, (size_t)(total - written));
        if (r <= 0) break;
        written += r;
    }

    free(buf);
}

/* ── Convenience sounds ─────────────────────────────────────────────────── */

void audio_beep(Audio *audio)
{
    /* 880 Hz, 80 ms — UI click / tile place */
    audio_tone(audio, 880, 80);
}

void audio_blip(Audio *audio)
{
    /* 1320 Hz, 60 ms — item collected, food eaten */
    audio_tone(audio, 1320, 60);
}

void audio_success(Audio *audio)
{
    /* C5 → E5 → G5 ascending arpeggio — score milestone, level up */
    audio_tone(audio, 523, 120);
    audio_tone(audio, 659, 120);
    audio_tone(audio, 784, 220);
}

void audio_fail(Audio *audio)
{
    /* G4 → E4 → C4 descending — game over, error */
    audio_tone(audio, 392, 150);
    audio_tone(audio, 330, 150);
    audio_tone(audio, 262, 300);
}
