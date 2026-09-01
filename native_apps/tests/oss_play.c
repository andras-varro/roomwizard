/*
 * oss_play.c — play a WAV file through /dev/dsp using the SHIPPED write path.
 *
 * Why this exists.  One suspected fault is the claim that the OSS path itself
 * corrupts audio: the same 440 Hz tone is mild through the vendor's `aplay`
 * (ALSA direct) and a full square through our `/dev/dsp` writes.  Every earlier
 * comparison changed TWO things at once — the samples came from our generator on
 * one side and from a file on the other — so the generator could never be fully
 * excluded by listening alone.  This probe closes that: it takes the SAME FILE
 * `aplay` takes and pushes it through `audio_interleave()` +
 * `audio_write_frames()`, the production code, with `configure_dsp()`'s exact
 * ioctl order.  A difference heard between
 *
 *     aplay      /tmp/F_0440_06000.wav
 *     /tmp/oss_play /tmp/F_0440_06000.wav
 *
 * is then the OSS path and nothing else.  ⚠️ Play at a peak inside the clean
 * acoustic zone (~6000, ../../SYSTEM_ANALYSIS.md#34-audio): at 18000 fault 1's
 * excursion-limited overdrive contaminates both sides and the A/B says nothing.
 *
 * `--dump=<file>` is the ears-free half: it sends the byte stream to a file
 * instead of the device, so the bytes our code hands the kernel can be measured
 * on the host (~/.claude/plans/f1-spectrum.py).  That separates "our userspace
 * corrupts it on ARM" from "the kernel shim does", which no listening test can.
 *
 * `--mode=pump` reproduces `audio_pump()`'s pacing — a lead-limited write per
 * simulated render frame — against `--mode=whole`, which reproduces
 * `audio_tone()`'s off-pump write.  Same samples, same device, only the pacing
 * differing, which is the other half of fault 2's hypothesis space.
 *
 * A probe, not an app: it is deliberately NOT in build-and-deploy.sh's
 * GAMES_BINARIES, exactly like oss_diag.c and ch_test.c.  Build and deploy by
 * hand:
 *
 *   arm-linux-gnueabihf-gcc -O2 -static -Wall -Wextra -I ../common \
 *       -o ../build/oss_play oss_play.c ../common/audio_gen.c -lm
 *   ../check-arm-safe.sh ../build/oss_play
 *   scp ../build/oss_play root@<ip>:/tmp/ && ssh root@<ip> chmod +x /tmp/oss_play
 */
#include "audio_gen.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/stat.h>

#define DSP_DEVICE "/dev/dsp"

/* ── the sink: identical to audio.c's dsp_write/dsp_wait ──────────────────── */

static int   g_fd    = -1;
static long  g_waits = 0;

static ssize_t sink_write(void *ctx, const void *buf, size_t nbytes, bool *again)
{
    (void)ctx;
    ssize_t r = write(g_fd, buf, nbytes);
    if (r < 0 && errno == EAGAIN) *again = true;
    return r;
}

static void sink_wait(void *ctx, int usec)
{
    (void)ctx;
    g_waits++;
    if (usec > 0) usleep((useconds_t)usec);
}

/* ── WAV reading ──────────────────────────────────────────────────────────────
 * Minimal RIFF walk.  Only what the stimuli generator emits is accepted —
 * PCM, 16-bit — and the file's own channel count is reduced to ONE by taking
 * the left channel, because the generator downstream of here is mono and
 * audio_interleave() is the single conversion point (../CLAUDE.md → Audio).
 */
static int16_t *read_wav_mono(const char *path, int *rate, long *frames)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror("oss_play: open wav"); return NULL; }

    char riff[12];
    if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) {
        fprintf(stderr, "oss_play: %s is not a RIFF/WAVE file\n", path);
        fclose(f);
        return NULL;
    }

    int      channels = 0, bits = 0;
    long     data_len = 0;
    long     data_pos = 0;
    unsigned char hdr[8];

    while (fread(hdr, 1, 8, f) == 8) {
        unsigned long sz = (unsigned long)hdr[4] | ((unsigned long)hdr[5] << 8)
                         | ((unsigned long)hdr[6] << 16) | ((unsigned long)hdr[7] << 24);
        if (!memcmp(hdr, "fmt ", 4)) {
            unsigned char fmt[16];
            if (sz < 16 || fread(fmt, 1, 16, f) != 16) break;
            channels = fmt[2] | (fmt[3] << 8);
            *rate    = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            bits     = fmt[14] | (fmt[15] << 8);
            if (sz > 16) fseek(f, (long)sz - 16, SEEK_CUR);
        } else if (!memcmp(hdr, "data", 4)) {
            data_pos = ftell(f);
            data_len = (long)sz;
            break;
        } else {
            fseek(f, (long)sz + ((long)sz & 1), SEEK_CUR);
        }
    }

    if (channels < 1 || bits != 16 || data_len <= 0) {
        fprintf(stderr, "oss_play: %s: need 16-bit PCM (got %d ch, %d bits, %ld bytes)\n",
                path, channels, bits, data_len);
        fclose(f);
        return NULL;
    }

    long n = data_len / (2 * channels);
    int16_t *ilv  = (int16_t *)malloc((size_t)data_len);
    int16_t *mono = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    if (!ilv || !mono) { free(ilv); free(mono); fclose(f); return NULL; }

    fseek(f, data_pos, SEEK_SET);
    if (fread(ilv, 1, (size_t)data_len, f) != (size_t)data_len) {
        fprintf(stderr, "oss_play: short read on %s\n", path);
        free(ilv); free(mono); fclose(f);
        return NULL;
    }
    fclose(f);

    for (long i = 0; i < n; i++) mono[i] = ilv[i * channels];   /* left channel */
    free(ilv);

    *frames = n;
    printf("oss_play: %s — %ld frames, %d Hz, %d ch in file (left channel taken)\n",
           path, n, *rate, channels);
    return mono;
}

/* ── the device, configured exactly as audio.c's configure_dsp() does ─────── */

static void configure(int fd, int *rate_out, int *ch_out)
{
    int rate = 44100;
    ioctl(fd, SNDCTL_DSP_SPEED, &rate);

    int fmt = AFMT_S16_LE;
    ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);

    int stereo = 1;
    ioctl(fd, SNDCTL_DSP_STEREO, &stereo);

    /* ⚠️ Both read back, as in production.  The granted numbers are the only
     * ones the byte arithmetic may use. */
    int actual = 0;
    *rate_out = (ioctl(fd, SOUND_PCM_READ_RATE, &actual) == 0 && actual > 0) ? actual : 44100;

    int ch = 0;
    *ch_out = (ioctl(fd, SOUND_PCM_READ_CHANNELS, &ch) == 0 && ch > 0) ? ch : 2;

    /* And the format, which production does NOT read back — the silent-failure
     * hole.  Printing it here is how this probe answers whether
     * that hole is live on this device. */
    int gfmt = 0;
    if (ioctl(fd, SOUND_PCM_READ_BITS, &gfmt) == 0)
        printf("oss_play: granted fmt readback = 0x%x (AFMT_S16_LE = 0x%x)\n",
               gfmt, AFMT_S16_LE);
    else
        printf("oss_play: SOUND_PCM_READ_BITS failed (errno=%d)\n", errno);
}

/* ── the two pacings ─────────────────────────────────────────────────────── */

/** audio_tone()'s off-pump write: the whole buffer, waiting as long as it takes. */
static const AudioWritePolicy POL_WHOLE = { 5000, 0, false };
/** audio_pump()'s write: never wait, the render loop owns the clock. */
static const AudioWritePolicy POL_PUMP  = { 1000, 0, true  };

static long play_whole(const int16_t *mono, long frames, int channels)
{
    long bytes = audio_bytes_for_frames(frames, channels);
    int16_t *ilv = (int16_t *)malloc((size_t)bytes);
    if (!ilv) return -1;
    if (audio_interleave(mono, frames, channels, ilv) != bytes) { free(ilv); return -1; }

    AudioSink sink = { sink_write, sink_wait, NULL };
    AudioWriteResult res;
    audio_write_frames(&sink, ilv, frames, channels, &POL_WHOLE, &res);
    free(ilv);

    printf("oss_play: whole — %ld/%ld frames, waits=%d, misaligned=%d, err=%d\n",
           res.frames_written, frames, res.waits, (int)res.misaligned, (int)res.sink_error);
    return res.frames_written;
}

/**
 * The pump's pacing, reproduced with the production arithmetic: measure the
 * ring, derive the lead from the DEVICE's period, write only up to it, then
 * sleep one render frame.  `frame_us` is the simulated render-loop delay —
 * FRAME_DELAY_ACTIVE_US (33333) by default.
 */
static long play_pump(const int16_t *mono, long frames, int rate, int channels, int frame_us)
{
    int frame_bytes = audio_frame_bytes(channels);
    long done = 0, starved = 0, lost = 0, iters = 0;
    int16_t *ilv = (int16_t *)malloc((size_t)audio_bytes_for_frames(frames, channels));
    if (!ilv) return -1;

    while (done < frames) {
        audio_buf_info info;
        if (ioctl(g_fd, SNDCTL_DSP_GETOSPACE, &info) < 0) { perror("GETOSPACE"); break; }

        long total_bytes = (long)info.fragstotal * (long)info.fragsize;
        long in_flight   = (total_bytes - (long)info.bytes) / frame_bytes;
        long space       = (long)info.bytes / frame_bytes;
        long period      = (long)info.fragsize / frame_bytes;
        long ring        = total_bytes / frame_bytes;
        long lead        = audio_pump_lead_frames(
                               audio_frames_for_ms(rate, AUDIO_PUMP_LEAD_MS),
                               period, AUDIO_PUMP_LEAD_PERIODS, ring);

        if (in_flight <= 0 && done > 0) starved++;

        long want = audio_pump_frames(lead, in_flight, space,
                                      (lead > 0) ? lead
                                                 : audio_frames_for_ms(rate, AUDIO_PUMP_CAP_MS));
        if (want > frames - done) want = frames - done;

        if (want > 0) {
            long bytes = audio_bytes_for_frames(want, channels);
            if (audio_interleave(mono + done, want, channels, ilv) != bytes) break;
            AudioSink sink = { sink_write, sink_wait, NULL };
            AudioWriteResult res;
            audio_write_frames(&sink, ilv, want, channels, &POL_PUMP, &res);
            if (res.frames_written < want) lost += want - res.frames_written;
            done += want;
            if (iters < 8)
                printf("oss_play: pump iter %ld — period=%ld lead=%ld in_flight=%ld "
                       "space=%ld want=%ld took=%ld\n",
                       iters, period, lead, in_flight, space, want, res.frames_written);
        }
        iters++;
        usleep((useconds_t)frame_us);
    }
    free(ilv);
    printf("oss_play: pump — %ld/%ld frames over %ld iters, starve=%ld lost=%ld\n",
           done, frames, iters, starved, lost);
    return done;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *wav = NULL, *dump = NULL, *mode = "whole";
    int frame_us = 33333;
    int tone_hz = 0, tone_ms = 2000, tone_peak = AUDIO_PEAK;

    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--dump=", 7))       dump = argv[i] + 7;
        else if (!strncmp(argv[i], "--mode=", 7))  mode = argv[i] + 7;
        else if (!strncmp(argv[i], "--frame-us=", 11)) frame_us = atoi(argv[i] + 11);
        else if (!strncmp(argv[i], "--tone=", 7))  tone_hz   = atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "--ms=", 5))    tone_ms   = atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "--peak=", 7))  tone_peak = atoi(argv[i] + 7);
        else if (argv[i][0] != '-')                wav  = argv[i];
        else { fprintf(stderr, "oss_play: unknown option %s\n", argv[i]); return 2; }
    }
    if (!wav && tone_hz <= 0) {
        fprintf(stderr, "usage: oss_play <file.wav> [--mode=whole|pump] "
                        "[--dump=<out.raw>] [--frame-us=N]\n"
                        "       oss_play --tone=<hz> [--ms=N] [--peak=N] ...\n");
        return 2;
    }

    int  file_rate = 44100;
    long frames    = 0;
    int16_t *mono  = NULL;

    if (tone_hz > 0) {
        /* ⚠️ The PRODUCTION generator, on the target.  audio_gen.c's THD figure
         * was measured on the host, where `double` and libm are not the device's;
         * this is the same call audio_tone() makes, so a dump of it is an ARM
         * measurement of the waveform rather than an x86 one. */
        frames = audio_frames_for_ms(file_rate, tone_ms);
        mono   = (int16_t *)malloc((size_t)frames * sizeof(int16_t));
        if (!mono) return 1;
        audio_render_tone(file_rate, tone_hz, tone_peak, mono, frames);
        printf("oss_play: generated %ld frames — %d Hz, peak %d, %d ms "
               "(audio_render_tone, the production generator)\n",
               frames, tone_hz, tone_peak, tone_ms);
    } else {
        mono = read_wav_mono(wav, &file_rate, &frames);
        if (!mono) return 1;
    }

    int rate = file_rate, channels = 2;

    if (dump) {
        /* Ears-free: the byte stream our code produces, without a device in the
         * way.  Interleaved at the channel count production would use. */
        g_fd = open(dump, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (g_fd < 0) { perror("oss_play: open dump"); free(mono); return 1; }
        printf("oss_play: DUMP mode — %s, %d Hz, %d ch, no device touched\n",
               dump, rate, channels);
        play_whole(mono, frames, channels);
        close(g_fd);
        free(mono);
        return 0;
    }

    /* GPIO12 HIGH unmutes SPKR1 — same poke audio.c's enable_amp() makes. */
    FILE *g = fopen("/sys/class/gpio/gpio12/direction", "w");
    if (g) { fputs("out", g); fclose(g); }
    g = fopen("/sys/class/gpio/gpio12/value", "w");
    if (g) { fputs("1", g); fclose(g); }

    g_fd = open(DSP_DEVICE, O_WRONLY | O_NONBLOCK);
    if (g_fd < 0) { perror("oss_play: open " DSP_DEVICE); free(mono); return 1; }

    configure(g_fd, &rate, &channels);
    printf("oss_play: device granted %d Hz, %d ch (file is %d Hz)%s\n",
           rate, channels, file_rate,
           (rate != file_rate) ? "  ⚠️ RATE MISMATCH — pitch will shift" : "");

    if (!strcmp(mode, "pump")) play_pump(mono, frames, rate, channels, frame_us);
    else                       play_whole(mono, frames, channels);

    /* Let the ring drain rather than truncating the tail on close(). */
    ioctl(g_fd, SNDCTL_DSP_SYNC, 0);
    close(g_fd);
    free(mono);
    return 0;
}
