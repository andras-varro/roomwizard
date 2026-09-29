/*
 * audio_carry_test — the music bed SURVIVES an output-device move, on the host
 *
 * A device move — stream_recover() after audio_out_device_lost(), stream_move()
 * after audio_out_usb_returned() — closes the stream and reopens it through
 * stream_open(), and stream_open() calls bus_reset(), which wipes every voice.
 * With nothing more, audio_music_active() went false on the new bus, and a bed
 * state machine read that as "track ended" and started the NEXT track from frame
 * 0: SameGame went A -> B on a USB DAC unplug.  The music voice's AudioWav is not
 * touched by the reset, so the fix re-arms it on the new bus at its own read
 * position.  This asserts the observable half of that:
 *
 *   A  after a simulated device loss during playback, audio_music_active() is
 *      still true, the voice reads the SAME open file, and its read position is
 *      where it was — then it keeps ADVANCING on the new bus
 *   B  (control) the move really happened: exactly one reopen, and it was the
 *      loss that caused it — without this, A passes on a stream never closed
 *   C  (control) a new device that grants a rate the file does not have must NOT
 *      carry it (there is no resampler) — so active can read false here
 *   D  (control) audio_music_stop() plus enough pumping reads inactive, so the
 *      instrument behind A can say "false" at all
 *
 * ⚠️ **The device is FAKED at one seam, and it is a LINK seam, not a source edit.**
 * `-Wl,--wrap=audio_out_open_resolved` sends audio.c's one open call to
 * __wrap_audio_out_open_resolved() below, which opens the REAL generic layer
 * (`audio_out_open()`) over an injected AudioOutDev — the same vtable
 * audio_out_test.c drives.  Everything above it is shipped code: audio_pump(), the
 * ENODEV → audio_out_device_lost() latch in audio_out.c, stream_recover(),
 * stream_open(), bus_reset().  The loss is injected the way a real unplug arrives:
 * the fake's `space` and `write` start failing with ENODEV.
 *
 * ⚠️ **What this cannot see.**  (1) stream_move() — the USB-RETURN path — is not
 * driven: it needs audio_out_usb_returned() true, which reads /dev/dsp1 and the
 * wall clock inside audio_out.c; it reaches the same stream_open(), so the carry
 * rule is shared, but that call path is untested here.  (2) The real OSS/ALSA
 * open, fallback and GPIO12 are behind the wrapped symbol.  (3) Whether the bed
 * sounds seamless is an ear question — the read position says the same file
 * continues, not that no audible gap occurs across the reopen.  (4)
 * audio_bed_service() is not run; A asserts the precondition it reads
 * (audio_music_active()), not its transition.
 *
 * Build and run (host gcc, from native_apps/):
 *   gcc -Wall -Wextra -Wno-unused-parameter -I. -Itests/hostshim \
 *       -Wl,--wrap=audio_out_open_resolved \
 *       -o build/audio_carry_test tests/audio_carry_test.c \
 *       common/audio.c common/audio_gen.c common/audio_out.c common/audio_wav.c \
 *       common/config.c -lm && ./build/audio_carry_test
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/audio.h"
#include "common/audio_out.h"

static int failures = 0;
static int checks   = 0;

static void check(bool ok, const char *what)
{
    checks++;
    if (ok) printf("    ok   %s\n", what);
    else  { failures++; printf("    FAIL: %s\n", what); }
}

/* ── The fake device ────────────────────────────────────────────────────── */

static struct {
    bool gone;          /* the device has been unplugged: space/write fail ENODEV */
    int  grant_rate;    /* what the NEXT open grants                              */
    int  opens;
    int  closes;
    long bytes;
} g_fake;

static int fake_open(void *ctx, int rate_req, int ch_req,
                     int *rate, int *bits, int *ch)
{
    (void)ctx; (void)rate_req;
    *rate = g_fake.grant_rate;
    *bits = 16;
    *ch   = ch_req;
    return 0;
}

static int fake_space(void *ctx, int frame_bytes, AudioOutSpace *sp)
{
    (void)ctx; (void)frame_bytes;
    if (g_fake.gone) { errno = ENODEV; return -1; }
    sp->period_frames = 1024;
    sp->ring_frames   = 8192;
    sp->in_flight     = 0;       /* drained between pumps: every service renders a lead */
    sp->space         = 8192;
    return 0;
}

static ssize_t fake_write(void *ctx, const void *buf, size_t n, bool *again)
{
    (void)ctx; (void)buf;
    *again = false;
    if (g_fake.gone) { errno = ENODEV; return -1; }
    g_fake.bytes += (long)n;
    return (ssize_t)n;
}

static void fake_wait(void *ctx, int usec) { (void)ctx; (void)usec; }
static void fake_close(void *ctx)          { (void)ctx; g_fake.closes++; }

static const AudioOutDev FAKE_DEV = {
    fake_open, fake_space, fake_write, fake_wait, fake_close
};

/* audio.c's only device open.  A reopen is a NEW device, so it comes back present. */
int __wrap_audio_out_open_resolved(AudioOut *out, int rate_req, int channels_req,
                                   const char **path_out);
int __wrap_audio_out_open_resolved(AudioOut *out, int rate_req, int channels_req,
                                   const char **path_out)
{
    g_fake.gone = false;
    g_fake.opens++;
    if (audio_out_open(out, &FAKE_DEV, NULL, rate_req, channels_req) != 0) return -1;
    /* open_path stays NULL, so audio_out_usb_returned() never probes /dev/dsp1. */
    if (path_out) *path_out = "fake-dsp";
    return 0;
}

/* ── The WAV fixture ────────────────────────────────────────────────────── */

static void put32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((int)((v >> (8 * i)) & 0xff), f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }

/** 16-bit mono PCM at `rate`, `frames` long, a quiet square wave. */
static bool write_wav(const char *path, int rate, long frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint32_t data = (uint32_t)(frames * 2);
    fwrite("RIFF", 1, 4, f); put32(f, 36 + data); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, (uint32_t)rate); put32(f, (uint32_t)rate * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, data);
    for (long i = 0; i < frames; i++) put16(f, (uint16_t)(((i / 50) & 1) ? 3000 : -3000));
    return fclose(f) == 0;
}

static void pump_n(Audio *a, int n) { for (int i = 0; i < n; i++) audio_pump(a); }

int main(void)
{
    printf("audio_carry_test — the music bed across an output-device move\n\n");

    char dir[] = "/tmp/audio_carry_XXXXXX";
    if (!mkdtemp(dir)) { printf("    FAIL: mkdtemp\n"); return 1; }
    char wav44[256], wav22[256];
    snprintf(wav44, sizeof wav44, "%s/bed44.wav", dir);
    snprintf(wav22, sizeof wav22, "%s/bed22.wav", dir);
    if (!write_wav(wav44, 44100, 44100L * 10) || !write_wav(wav22, 22050, 22050L * 10)) {
        printf("    FAIL: cannot write the WAV fixtures under %s\n", dir);
        return 1;
    }

    /* ── A + B: loss at the same rate carries the bed ─────────────────────── */
    printf("  A  device lost mid-bed: the same file keeps playing\n");
    memset(&g_fake, 0, sizeof g_fake);
    g_fake.grant_rate = 44100;
    Audio a;
    check(audio_init_unchecked(&a) == 0 && a.available, "A: the fake stream opens");
    check(audio_music_start(&a, wav44, false), "A: the bed starts");
    pump_n(&a, 10);
    FILE *f_before   = a.music.wav.f;
    long  pos_before = a.music.wav.pos;
    check(pos_before > 0, "A: the bed has advanced before the loss");

    g_fake.gone = true;
    audio_pump(&a);                                /* service fails ENODEV → latch */
    bool latched = audio_out_device_lost(&a.out);
    audio_pump(&a);                                /* stream_recover → reopen      */

    printf("  B  (control) the move really happened\n");
    check(latched, "B: audio_out_device_lost() latched from the fake's ENODEV");
    check(g_fake.opens == 2 && g_fake.closes == 1,
          "B: exactly one close and one reopen");
    check(!a.reopening && audio_out_is_open(&a.out), "B: the reopen succeeded");

    check(audio_music_active(&a), "A: audio_music_active() is still true after the move");
    check(a.music.wav.f == f_before, "A: it is the SAME open file, not a reopen");
    check(a.music.wav.pos >= pos_before && a.music.wav.pos != 0,
          "A: the read position was not reset to 0");
    long pos_after_move = a.music.wav.pos;
    pump_n(&a, 10);
    check(a.music.wav.pos > pos_after_move,
          "A: the bed keeps ADVANCING on the new bus");
    printf("    (pos %ld before the loss, %ld after the move, %ld ten pumps on)\n",
           pos_before, pos_after_move, a.music.wav.pos);

    /* ── D: the instrument can read false ─────────────────────────────────── */
    printf("  D  (control) a stopped bed reads inactive\n");
    audio_music_stop(&a);
    pump_n(&a, 50);
    check(!audio_music_active(&a), "D: audio_music_stop() + pumping reads inactive");
    audio_close(&a);

    /* ── C: a rate mismatch on the new device is NOT carried ──────────────── */
    printf("  C  (control) a new device at another rate does not carry the bed\n");
    memset(&g_fake, 0, sizeof g_fake);
    g_fake.grant_rate = 22050;
    Audio c;
    check(audio_init_unchecked(&c) == 0 && c.available, "C: the fake stream opens at 22050");
    check(audio_music_start(&c, wav22, false), "C: a 22050 bed starts");
    pump_n(&c, 5);
    g_fake.gone = true;
    g_fake.grant_rate = 44100;                     /* the replacement grants 44100 */
    pump_n(&c, 2);
    check(g_fake.opens == 2 && c.sample_rate == 44100, "C: reopened at 44100");
    check(!audio_music_active(&c), "C: the 22050 bed is NOT carried onto 44100");
    audio_close(&c);

    unlink(wav44); unlink(wav22); rmdir(dir);

    printf("\n%s  %d check(s), %d failure(s)\n",
           failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
