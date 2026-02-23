/*
 * oss_diag.c - Diagnose what the ALSA OSS compat shim actually negotiates.
 * Prints every ioctl result so we know exactly what parameters are in effect.
 * Plays a 440 Hz tone for 3 seconds and measures write() block time.
 *
 * Build: arm-linux-gnueabihf-gcc -O2 -static oss_diag.c -o build/oss_diag -lm
 * Run on device: /tmp/oss_diag
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/time.h>
#include <stdint.h>
#include <errno.h>

static long usec_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000000L + tv.tv_usec;
}

int main(void) {
    /* Test 1: O_NONBLOCK behaviour */
    printf("=== O_NONBLOCK write test ===\n");
    int fdnb = open("/dev/dsp", O_WRONLY | O_NONBLOCK);
    if (fdnb < 0) { perror("open O_NONBLOCK"); }
    else {
        int frag2 = (2 << 16) | 14;
        ioctl(fdnb, SNDCTL_DSP_SETFRAGMENT, &frag2);
        int fmt2 = AFMT_S16_LE; ioctl(fdnb, SNDCTL_DSP_SETFMT, &fmt2);
        int ch2 = 2;            ioctl(fdnb, SNDCTL_DSP_CHANNELS, &ch2);
        int r2 = 44100;         ioctl(fdnb, SNDCTL_DSP_SPEED, &r2);

        int16_t *nb_buf = (int16_t*)malloc(16384);
        memset(nb_buf, 0, 16384);
        long eagain_count = 0, ok_count = 0;
        int nb_written = 0;
        /* Try to push 4 buffers worth — should fill ring then get EAGAIN */
        for (int i = 0; i < 4; i++) {
            int w = 0;
            while (w < 16384) {
                long t0 = usec_now();
                int rc = (int)write(fdnb, (char*)nb_buf + w, 16384 - w);
                long el = usec_now() - t0;
                if (rc > 0) {
                    printf("  O_NONBLOCK write[%d]+%d: %d bytes in %ld us\n", i, w, rc, el);
                    w += rc; ok_count++;
                } else {
                    printf("  O_NONBLOCK write[%d]+%d: rc=%d errno=%d (%s) in %ld us\n",
                           i, w, rc, errno, strerror(errno), el);
                    if (errno == EAGAIN || errno == EWOULDBLOCK) { eagain_count++; break; }
                    else break;
                }
            }
            nb_written += w;
        }
        printf("  Result: %ld ok writes, %ld EAGAIN, %d bytes total\n\n",
               ok_count, eagain_count, nb_written);
        free(nb_buf);
        close(fdnb);
    }

    /* Test 2: blocking behaviour (original) */
    printf("=== Blocking write test (original) ===\n");
    int fd = open("/dev/dsp", O_WRONLY);
    if (fd < 0) { perror("open /dev/dsp"); return 1; }
    printf("Opened /dev/dsp fd=%d\n", fd);

    /* --- SETFRAGMENT first (before any format ioctl) --- */
    /* Try various sizes and print what we get back */
    int frag_req = (2 << 16) | 14;  /* 2 frags * 2^14 = 2*16384 = 32768 bytes */
    int frag_set = frag_req;
    if (ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &frag_set) < 0)
        perror("SNDCTL_DSP_SETFRAGMENT failed");
    printf("SETFRAGMENT: requested 0x%08x (frags=%d size=2^%d=%d bytes)\n",
           frag_req, (frag_req >> 16) & 0x7fff, frag_req & 0xffff,
           1 << (frag_req & 0xffff));
    /* note: frag_set value after ioctl may or may not be updated by driver */

    /* --- Format --- */
    int fmt = AFMT_S16_LE;
    if (ioctl(fd, SNDCTL_DSP_SETFMT, &fmt) < 0) perror("SNDCTL_DSP_SETFMT");
    printf("SETFMT:      requested AFMT_S16_LE=0x%x, got 0x%x (%s)\n",
           AFMT_S16_LE, fmt, (fmt == AFMT_S16_LE) ? "OK" : "MISMATCH");

    /* --- Channels (use CHANNELS not deprecated STEREO) --- */
    int channels = 2;
    if (ioctl(fd, SNDCTL_DSP_CHANNELS, &channels) < 0) perror("SNDCTL_DSP_CHANNELS");
    printf("CHANNELS:    requested 2, got %d (%s)\n",
           channels, (channels == 2) ? "OK" : "MISMATCH");

    /* --- Sample rate --- */
    int rate = 44100;
    if (ioctl(fd, SNDCTL_DSP_SPEED, &rate) < 0) perror("SNDCTL_DSP_SPEED");
    printf("SPEED:       requested 44100, got %d (%s)\n",
           rate, (abs(rate - 44100) < 100) ? "OK" : (abs(rate - 48000) < 100) ? "rounded to 48000" : "UNEXPECTED");

    /* --- Query actual buffer state --- */
    audio_buf_info ospace;
    if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &ospace) < 0) {
        perror("SNDCTL_DSP_GETOSPACE");
    } else {
        printf("\nGETOSPACE (after format setup):\n");
        printf("  fragments:  %d\n",  ospace.fragments);
        printf("  fragstotal: %d\n",  ospace.fragstotal);
        printf("  fragsize:   %d bytes (%d frames)\n",
               ospace.fragsize, ospace.fragsize / (2 * sizeof(int16_t)));
        printf("  bytes free: %d\n",  ospace.bytes);
        printf("  total ring: %d bytes = %.1f ms at %d Hz stereo S16\n",
               ospace.fragstotal * ospace.fragsize,
               1000.0 * (ospace.fragstotal * ospace.fragsize) / (rate * 2 * 2),
               rate);
    }

    /* --- Synthesise a 440 Hz tone and measure write() blocking --- */
    const int SAMPLES = 4096;
    const int BUF_BYTES = SAMPLES * 2 * sizeof(int16_t);
    int16_t *buf = (int16_t *)malloc(BUF_BYTES);

    printf("\nPlaying 440 Hz tone, %d-frame buffer (%d bytes = %.1f ms) ...\n",
           SAMPLES, BUF_BYTES, 1000.0 * SAMPLES / rate);

    int phase = 0;
    int total_writes = (int)((float)rate / SAMPLES * 3.0f);  /* 3 seconds */
    long blocked_count = 0;

    for (int i = 0; i < total_writes; i++) {
        /* fill buffer with sine wave */
        for (int s = 0; s < SAMPLES; s++) {
            int16_t v = (int16_t)(18000.0 * sin(2.0 * M_PI * 440.0 * phase / rate));
            buf[s * 2 + 0] = v;
            buf[s * 2 + 1] = v;
            phase = (phase + 1) % rate;
        }

        long t0 = usec_now();
        int written = 0;
        while (written < BUF_BYTES) {
            int r = (int)write(fd, (char*)buf + written, BUF_BYTES - written);
            if (r <= 0) { fprintf(stderr, "write error: %s\n", strerror(errno)); break; }
            written += r;
        }
        long elapsed = usec_now() - t0;

        /* count writes that actually blocked (>1ms) */
        if (elapsed > 1000) blocked_count++;

        /* print first 6 + last 2 timing samples */
        if (i < 6 || i >= total_writes - 2) {
            printf("  write[%2d]: %ld us %s\n", i, elapsed,
                   elapsed > 1000 ? "(BLOCKED)" : "");
        } else if (i == 6) {
            printf("  ...\n");
        }
    }

    printf("\nBlocking writes: %ld / %d (%.0f%%)\n",
           blocked_count, total_writes,
           100.0 * blocked_count / total_writes);
    printf("Expected: ~50%% blocking for 2-fragment ring\n");

    /* Final GETOSPACE */
    if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &ospace) == 0) {
        printf("\nGETOSPACE (after playback):\n");
        printf("  fragsize=%d fragstotal=%d bytes_free=%d\n",
               ospace.fragsize, ospace.fragstotal, ospace.bytes);
    }

    free(buf);
    close(fd);
    printf("Done.\n");
    return 0;
}
