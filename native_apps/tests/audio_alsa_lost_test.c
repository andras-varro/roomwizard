/*
 * audio_alsa_lost_test.c — the ALSA backend must notice an unplugged USB card on
 * the very next service, even when the free-space query keeps answering.
 *
 * MEASURED on a unit with a probe holding plughw:1,0 (S16, 44100 Hz, 2 ch,
 * buffer 32768, period 2048): from the kernel's "USB disconnect" onward,
 * snd_pcm_avail_update() returned a FROZEN positive value (1543) with no error
 * for 25 s, while snd_pcm_avail() returned -ENODEV from the first sample and
 * snd_pcm_writei() returned -EBADFD.  A backend that sizes its writes from
 * avail_update sees a nearly full ring forever, so the pump asks for nothing,
 * writei is never called, and the loss never surfaces: silence, with no reopen.
 *
 * This compiles the REAL backend in common/audio_out.c (-DAUDIO_OUT_HAVE_ALSA)
 * against tests/alsa_stub/alsa/asoundlib.h, which only declares the ALSA calls;
 * every one is defined below, so the stub plays back exactly that transcript.
 *
 *   L1-L3  CONTROL: a healthy stream (avail_update == avail, positive) is
 *          serviced, written to, and NOT reported lost.
 *   U1-U3  the unplug transcript: one service call fails, reports the device
 *          lost and counts a refused service.
 *
 * Seen failing: against the pre-fix audio_out.c (avail_update in alsa_space)
 * U1-U3 fail and L1-L3 pass.
 *
 * Build (from native_apps/, as tests/run-all.sh does):
 *   gcc -Wall -Wextra -Wno-unused-parameter -I common -DAUDIO_OUT_HAVE_ALSA \
 *       -I tests/alsa_stub -o build/audio_alsa_lost_test \
 *       tests/audio_alsa_lost_test.c common/audio_out.c common/audio_gen.c -lm
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <alsa/asoundlib.h>
#include "../common/audio_out.h"

/* ── The stub device ────────────────────────────────────────────────────── */

#define STUB_RATE    44100u
#define STUB_CH      2u
#define STUB_PERIOD  2048ul
#define STUB_BUFFER  32768ul
#define STUB_FROZEN  1543      /* the measured frozen avail_update */

static struct {
    bool          unplugged;
    unsigned long queued;      /* frames in the ring */
    unsigned      writes;      /* successful writei calls */
} stub;

static int stub_pcm_token;

static long healthy_avail(void)
{
    return (long)(STUB_BUFFER - stub.queued);
}

int snd_pcm_open(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t s, int mode)
{
    *pcm = (snd_pcm_t *)&stub_pcm_token;
    return 0;
}
int snd_pcm_close(snd_pcm_t *pcm)   { return 0; }
int snd_pcm_prepare(snd_pcm_t *pcm) { return stub.unplugged ? -ENODEV : 0; }
int snd_pcm_resume(snd_pcm_t *pcm)  { return stub.unplugged ? -ENODEV : 0; }

snd_pcm_sframes_t snd_pcm_avail(snd_pcm_t *pcm)
{
    return stub.unplugged ? -ENODEV : healthy_avail();
}
snd_pcm_sframes_t snd_pcm_avail_update(snd_pcm_t *pcm)
{
    return stub.unplugged ? STUB_FROZEN : healthy_avail();
}
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buf, snd_pcm_uframes_t n)
{
    if (stub.unplugged) return -EBADFD;
    if (n > STUB_BUFFER - stub.queued) n = STUB_BUFFER - stub.queued;
    if (n == 0) return -EAGAIN;
    stub.queued += n;
    stub.writes++;
    return (snd_pcm_sframes_t)n;
}
const char *snd_strerror(int e)                 { return e < 0 ? strerror(-e) : strerror(e); }
int snd_pcm_format_width(snd_pcm_format_t f)    { return 16; }

int snd_pcm_hw_params_any(snd_pcm_t *p, snd_pcm_hw_params_t *h)                       { return 0; }
int snd_pcm_hw_params_set_access(snd_pcm_t *p, snd_pcm_hw_params_t *h, snd_pcm_access_t a) { return 0; }
int snd_pcm_hw_params_set_format(snd_pcm_t *p, snd_pcm_hw_params_t *h, snd_pcm_format_t f) { return 0; }
int snd_pcm_hw_params_set_rate_near(snd_pcm_t *p, snd_pcm_hw_params_t *h, unsigned *v, int *d)
{ *v = STUB_RATE; return 0; }
int snd_pcm_hw_params_set_channels_near(snd_pcm_t *p, snd_pcm_hw_params_t *h, unsigned *v)
{ *v = STUB_CH; return 0; }
int snd_pcm_hw_params_set_period_size_near(snd_pcm_t *p, snd_pcm_hw_params_t *h,
                                           snd_pcm_uframes_t *v, int *d)
{ *v = STUB_PERIOD; return 0; }
int snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t *p, snd_pcm_hw_params_t *h,
                                           snd_pcm_uframes_t *v)
{ *v = STUB_BUFFER; return 0; }
int snd_pcm_hw_params(snd_pcm_t *p, snd_pcm_hw_params_t *h) { return 0; }
int snd_pcm_hw_params_get_rate(const snd_pcm_hw_params_t *h, unsigned *v, int *d)
{ *v = STUB_RATE; return 0; }
int snd_pcm_hw_params_get_channels(const snd_pcm_hw_params_t *h, unsigned *v)
{ *v = STUB_CH; return 0; }
int snd_pcm_hw_params_get_period_size(const snd_pcm_hw_params_t *h, snd_pcm_uframes_t *v, int *d)
{ *v = STUB_PERIOD; return 0; }
int snd_pcm_hw_params_get_buffer_size(const snd_pcm_hw_params_t *h, snd_pcm_uframes_t *v)
{ *v = STUB_BUFFER; return 0; }

int snd_pcm_sw_params_current(snd_pcm_t *p, snd_pcm_sw_params_t *s) { return 0; }
int snd_pcm_sw_params_set_start_threshold(snd_pcm_t *p, snd_pcm_sw_params_t *s,
                                          snd_pcm_uframes_t v) { return 0; }
int snd_pcm_sw_params(snd_pcm_t *p, snd_pcm_sw_params_t *s) { return 0; }

/* ── Checks ─────────────────────────────────────────────────────────────── */

static int checks, failures;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) failures++;
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
}

int main(void)
{
    AudioOut out;
    setvbuf(stdout, NULL, _IONBF, 0);   /* keep the lines in step with the library's stderr */
    memset(&stub, 0, sizeof(stub));

    if (audio_out_open_alsa(&out, "plughw:1,0", 44100, 2) != 0) {
        printf("FAILED  the stub ALSA stream would not open\n");
        return 1;
    }

    printf("control: a healthy stream\n");
    /* The ring has been played down to one period: the pump has room to write. */
    stub.queued = STUB_PERIOD;
    unsigned before = stub.writes;
    long r = audio_out_service(&out);
    check(r >= 0, "L1 a healthy service call succeeds");
    check(stub.writes > before, "L2 a healthy service call writes to the device");
    check(!audio_out_device_lost(&out), "L3 a healthy stream is not reported lost");

    printf("unplug: avail_update frozen at %d, avail -ENODEV, writei -EBADFD\n",
           STUB_FROZEN);
    stub.unplugged = true;
    uint32_t refused_before = audio_out_refused(&out);
    errno = 0;
    r = audio_out_service(&out);
    check(r < 0, "U1 the first service after the unplug fails");
    check(audio_out_device_lost(&out), "U2 the first service after the unplug reports the device lost");
    check(audio_out_refused(&out) == refused_before + 1,
          "U3 the failed query is counted as a refused service");

    audio_out_close(&out);

    printf("\n%s  %d checks, %d failure(s)\n",
           failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
