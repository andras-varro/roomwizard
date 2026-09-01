/* oss_keepalive.c — Probe for the continuous-stream unification.
 *
 * ONE question, objectively, with no ear involved: on a stream that is never reset,
 * HOW OFTEN must a render loop call the service function before the device runs dry?
 *
 * Why it decides a design question. `audio_pump_active()` returns true whenever
 * keepalive is on, and `native_apps/CLAUDE.md` → *Rendering* puts that in the frame-pacing
 * decision — so a permanently fed stream pins EVERY audio-holding app to
 * FRAME_DELAY_ACTIVE_US (33 ms), including the static UIs (`device_tools`,
 * `hardware_config`, `hardware_test`) whose whole premise is that an idle screen costs
 * nothing.  If a 100 ms service interval is dry-free the static UIs keep
 * FRAME_DELAY_IDLE_US; if it is not, `audio_pump_active()` is load-bearing and the
 * shared library owes those tabs the synchronous write mode instead.
 *
 * Writes SILENCE only, and deliberately does not touch GPIO12, so the amp stays off:
 * no sound, no listener, and the reading is a device state rather than an opinion.
 *
 * ── The two witnesses, and why they are the only ones ──────────────────────────
 * An XRUN under the OSS shim is INVISIBLE to us.  `snd_pcm_oss_write3()`
 * (../usb_host/linux-4.14.52/sound/core/oss/pcm_oss.c:1220-1247) sees the XRUN state,
 * calls `snd_pcm_oss_prepare()`, retries the transfer, and returns SUCCESS to
 * userspace — so no `write()` return code will ever show one, and the transient `XRUN`
 * state is gone before a poll can see it.  `xrun_debug` and `xrun_injection` are behind
 * `CONFIG_SND_PCM_XRUN_DEBUG`, which is NOT SET in this kernel
 * (../usb_host/device_config:2730), and `hw_ptr_error()` compiles to a no-op
 * (pcm_lib.c:185) so nothing reaches dmesg either.  That leaves exactly two, both read
 * from `/proc/asound/card0/pcm0p/sub0/status`:
 *
 *   1. `avail_max` — LATCHING, and cleared by our own read.  `snd_pcm_update_state()`
 *      keeps a running max (pcm_lib.c:198-199); `snd_pcm_status()` copies it out and
 *      zeroes it (pcm_native.c:923-925), and the proc reader calls that (pcm.c:469).  No
 *      prepare, reset or start path clears it.  So each read yields the worst free-space
 *      figure SINCE THE PREVIOUS READ, and `avail_max == buffer_size` means the ring went
 *      fully empty inside that window.  ⚠️ It is stolen by any second reader — this probe
 *      must be the only thing polling, and nothing else may hold the card.
 *   2. `appl_ptr` GOING BACKWARDS.  The shim's recovery prepares the substream, which
 *      lands `hw_ptr` at 0 (pcm_lib.c:1697) and then `appl_ptr = hw_ptr` (pcm_native.c:1651).
 *      ⚠️ It witnesses a PREPARE, not uniquely an XRUN — `SNDCTL_DSP_RESET`, `SYNC`,
 *      `SETTRIGGER` and any param change do it too (pcm_oss.c:1567, :1715, :2083, :1073).
 *      This probe issues none of those after the stream starts, which is what makes the
 *      rewind definitive here.  It is not definitive in a program that resets.
 *
 * ── The sweep is its own positive control ─────────────────────────────────────
 * The lead is three device periods, ~139 ms at 44100/stereo.  A 200 ms service interval
 * therefore CANNOT keep the ring fed: it must show a dry window.  If row 200 comes back
 * clean, the instrument is broken and every clean row above it is worthless — so the
 * verdict at the end says exactly that rather than leaving it to be remembered.
 *
 * The rows run clean-to-dirty on ONE never-reset stream, so contamination can only flow
 * from a safe interval into a worse one, never the other way.
 *
 * Links the production arithmetic (`audio_gen.c`) rather than restating it, so what it
 * measures is what the library will do.  Standalone probe, NOT in build-and-deploy.sh
 * (same class as oss_geom.c, ch_test.c, oss_diag.c, oss_play.c).
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -Wall -Wextra -O2 -static -I common \
 *       -o build/oss_keepalive tests/oss_keepalive.c common/audio_gen.c -lm
 */

#include "audio_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/time.h>

#define DSP_DEVICE   "/dev/dsp"
#define PCM_STATUS   "/proc/asound/card0/pcm0p/sub0/status"
#define PCM_HWPARAMS "/proc/asound/card0/pcm0p/sub0/hw_params"

/** Seconds each row runs.  Long enough that a 200 ms interval gets ~30 services. */
#define ROW_SECONDS  6

/** The stall injected in the composite row, in ms.  This is the WORST FRAME the mix-bus
 *  tool actually measured on `.188`, not a round number. */
#define WORST_FRAME_MS  107

/* ── The device, exactly as the library drives it ────────────────────────────── */

static int dsp_fd = -1;

static ssize_t dsp_write(void *ctx, const void *buf, size_t nbytes, bool *again)
{
    (void)ctx;
    ssize_t r = write(dsp_fd, buf, nbytes);
    if (r < 0 && errno == EAGAIN) *again = true;
    return r;
}

static void dsp_wait(void *ctx, int usec)
{
    (void)ctx;
    if (usec > 0) usleep((useconds_t)usec);
}

/** WPOL_PUMP: called from a render loop, so it may never wait. */
static const AudioWritePolicy WPOL_PUMP    = { 1000, 0, true  };
/** WPOL_PREFILL: open-time, blocking on purpose — it primes the DAC. */
static const AudioWritePolicy WPOL_PREFILL = { 1000, 0, false };

/* ── /proc parsing ──────────────────────────────────────────────────────────── */

typedef struct {
    char state[16];
    long avail;
    long avail_max;
    long hw_ptr;
    long appl_ptr;
    bool valid;      /**< false when the file said `closed` or could not be read */
} PcmStatus;

/** Value after the first ':' on the line introduced by `key`.  The status file pads
 *  every key to 12 chars except `state:`, so keying off the bare name and then finding
 *  the colon works for all of them (pcm.c:474-485). */
static bool proc_field(const char *text, const char *key, long *out)
{
    const char *p = strstr(text, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    *out = strtol(p + 1, NULL, 10);
    return true;
}

/** ⚠️ Reading this file CLEARS avail_max (pcm.c:469 → pcm_native.c:925), so call it
 *  exactly once per service or the witness is spent on a poll nobody attributed. */
static void read_status(PcmStatus *st)
{
    char buf[1024];
    memset(st, 0, sizeof(*st));

    int fd = open(PCM_STATUS, O_RDONLY);
    if (fd < 0) return;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    if (strncmp(buf, "closed", 6) == 0) return;   /* substream not open */

    const char *s = strstr(buf, "state:");
    if (s) {
        s += 6;
        while (*s == ' ') s++;
        size_t i = 0;
        while (s[i] && s[i] != '\n' && i < sizeof(st->state) - 1) { st->state[i] = s[i]; i++; }
        st->state[i] = '\0';
    }
    proc_field(buf, "avail_max", &st->avail_max);
    proc_field(buf, "avail ",    &st->avail);      /* trailing space: not avail_max */
    proc_field(buf, "hw_ptr",    &st->hw_ptr);
    proc_field(buf, "appl_ptr",  &st->appl_ptr);
    st->valid = true;
}

/** ALSA's own buffer_size, so `avail_max == buffer_size` is compared against the number
 *  the KERNEL uses rather than one derived from GETOSPACE through our frame size. */
static long proc_buffer_size(void)
{
    char buf[1024];
    long v = 0;
    int fd = open(PCM_HWPARAMS, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    if (!proc_field(buf, "buffer_size", &v)) return 0;
    return v;
}

static long now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* Baselined by the caller against a start value — never multiplied from epoch,
     * which overflows a 32-bit long (../CLAUDE.md). */
    return (long)tv.tv_sec * 1000000L + (long)tv.tv_usec;
}

/* ── One row of the sweep ────────────────────────────────────────────────────── */

typedef struct {
    int  interval_ms;
    int  stall_ms;          /**< a stall injected once per second, or 0 */
    long services;
    long frames_written;
    long min_in_flight;     /**< the shallowest queue seen BEFORE a write */
    long worst_avail_max;   /**< the emptiest the ring got between two polls */
    long dry_windows;       /**< polls where avail_max reached buffer_size */
    long appl_rewinds;      /**< prepares, i.e. XRUN recoveries here */
    long non_running;       /**< polls whose state was not RUNNING */
    long short_writes;      /**< the device took fewer frames than we offered */
    long misaligned;        /**< a partial frame reached the device — a real fault */
    long sink_errors;
    long waits;
    /** ⚠️ How much MORE queue GETOSPACE claims than the kernel does, at the same
     *  instant: `in_flight` from GETOSPACE minus `buffer_size - avail` from
     *  /proc.  The shim counts the period it is still STAGING as already queued,
     *  so the pump's effective
     *  lead is the nominal one MINUS this.  It is the only number that explains
     *  why a 100 ms service interval starves a 139 ms lead. */
    long worst_stage_gap;
    long stage_gap_samples;
} Row;

static void run_row(Row *row, int rate, int channels, long lead, long period,
                    long ring, long buffer_size, int16_t *zeros)
{
    int  frame_bytes = audio_frame_bytes(channels);
    long start       = now_us();
    long deadline_us = (long)ROW_SECONDS * 1000000L;
    long next_stall  = 1000000L;   /* first injected stall at t+1 s */
    long prev_appl   = -1;

    row->min_in_flight  = ring;      /* so the first sample can only lower it */
    row->worst_avail_max = 0;

    for (;;) {
        long elapsed = now_us() - start;
        if (elapsed >= deadline_us) break;

        /* ── the witnesses, read ONCE per service, BEFORE the write ─────────────
         * Before, so the kernel's `avail` and GETOSPACE's `bytes` describe the same
         * instant and their difference is measurable.  avail_max's meaning is
         * unaffected: it is still "the worst since the previous read". */
        PcmStatus st;
        read_status(&st);
        if (st.valid) {
            if (st.avail_max > row->worst_avail_max) row->worst_avail_max = st.avail_max;
            if (buffer_size > 0 && st.avail_max >= buffer_size) row->dry_windows++;
            if (strcmp(st.state, "RUNNING") != 0) row->non_running++;
            if (prev_appl >= 0 && st.appl_ptr < prev_appl) row->appl_rewinds++;
            prev_appl = st.appl_ptr;
        }

        /* ── the service call, exactly as the library's serviced mode will do it ── */
        audio_buf_info info;
        if (ioctl(dsp_fd, SNDCTL_DSP_GETOSPACE, &info) < 0) {
            row->sink_errors++;
            break;
        }
        long total_bytes = (long)info.fragstotal * (long)info.fragsize;
        long in_flight   = (total_bytes - (long)info.bytes) / frame_bytes;
        long space       = (long)info.bytes / frame_bytes;

        if (in_flight < row->min_in_flight) row->min_in_flight = in_flight;

        /* What the two sources disagree by.  Only meaningful while the stream is
         * RUNNING — after a recovery the kernel's pointers are 0 and the comparison
         * is between two different notions of "now". */
        if (st.valid && buffer_size > 0 && strcmp(st.state, "RUNNING") == 0) {
            long kernel_in_flight = buffer_size - st.avail;
            long gap = in_flight - kernel_in_flight;
            if (gap > row->worst_stage_gap) row->worst_stage_gap = gap;
            row->stage_gap_samples++;
        }

        long want = audio_pump_frames(lead, in_flight, space, lead);
        if (want > 0) {
            AudioSink sink = { dsp_write, dsp_wait, NULL };
            AudioWriteResult res;
            audio_write_frames(&sink, zeros, want, channels, &WPOL_PUMP, &res);
            row->frames_written += res.frames_written;
            row->waits          += res.waits;
            if (res.frames_written < want) row->short_writes++;
            if (res.misaligned)            row->misaligned++;
            if (res.sink_error)            row->sink_errors++;
        }
        row->services++;

        /* ── the injected worst frame, if this row has one ─────────────────────── */
        if (row->stall_ms > 0 && (now_us() - start) >= next_stall) {
            usleep((useconds_t)row->stall_ms * 1000U);
            next_stall += 1000000L;
        }

        usleep((useconds_t)row->interval_ms * 1000U);
    }

    (void)period;
    (void)rate;
}

static void print_row(const Row *r, int rate, long lead, long buffer_size)
{
    char label[32];
    if (r->stall_ms > 0)
        snprintf(label, sizeof(label), "%d + %dms stall", r->interval_ms, r->stall_ms);
    else
        snprintf(label, sizeof(label), "%d", r->interval_ms);

    long min_ms  = (rate > 0) ? audio_ms_for_frames(rate, r->min_in_flight)   : 0;
    long worst_ms = (rate > 0) ? audio_ms_for_frames(rate, r->worst_avail_max) : 0;
    long gap_ms   = (rate > 0) ? audio_ms_for_frames(rate, r->worst_stage_gap) : 0;

    /* True content at the emptiest moment the KERNEL saw, which is the number the DAC
     * experienced — `min in_flight` is what GETOSPACE claimed instead. */
    long true_ms = (rate > 0 && buffer_size > 0)
                   ? audio_ms_for_frames(rate, buffer_size - r->worst_avail_max) : 0;

    printf("  %-16s %6ld %7ld (%4ld ms) %8ld (%4ld ms) %5ld %5ld %5ld %5ld %8ld (%3ld ms)\n",
           label, r->services,
           r->min_in_flight, min_ms,
           r->worst_avail_max, true_ms,
           r->dry_windows, r->appl_rewinds, r->non_running, r->short_writes,
           r->worst_stage_gap, gap_ms);

    (void)worst_ms;
    if (r->misaligned)
        printf("      *** %ld MISALIGNED writes — a partial frame is in the device, "
               "L/R are swapped for the rest of the stream\n", r->misaligned);
    if (r->sink_errors)
        printf("      *** %ld sink errors\n", r->sink_errors);
    (void)lead;
}

int main(void)
{
    printf("oss_keepalive — how often a never-reset stream must be serviced.  "
           "Silence only; no listener needed.\n");

    /* ⚠️ avail_max is clear-on-read and this probe is the only legitimate reader.
     * Anything else holding the card makes every row meaningless rather than wrong. */
    dsp_fd = open(DSP_DEVICE, O_WRONLY | O_NONBLOCK);
    if (dsp_fd < 0) {
        printf("  open(%s) FAILED errno=%d (%s)\n", DSP_DEVICE, errno, strerror(errno));
        printf("  ^ EBUSY means another process holds the device. Not a probe defect.\n");
        return 1;
    }

    /* SPEED -> FMT -> CHANNELS, then believe only the read-backs.  CHANNELS rather than
     * the deprecated STEREO (measured: both grant identically here). */
    int rate = 44100;
    ioctl(dsp_fd, SNDCTL_DSP_SPEED, &rate);
    int fmt = AFMT_S16_LE;
    ioctl(dsp_fd, SNDCTL_DSP_SETFMT, &fmt);
    int ch = 2;
    ioctl(dsp_fd, SNDCTL_DSP_CHANNELS, &ch);

    int got_rate = 0, got_bits = 0, got_chan = 0;
    ioctl(dsp_fd, SOUND_PCM_READ_RATE,     &got_rate);
    ioctl(dsp_fd, SOUND_PCM_READ_BITS,     &got_bits);
    ioctl(dsp_fd, SOUND_PCM_READ_CHANNELS, &got_chan);
    printf("  granted: rate=%d bits=%d channels=%d\n", got_rate, got_bits, got_chan);
    if (got_rate <= 0 || got_chan <= 0 || got_bits != 16) {
        printf("  *** unusable grant — stopping rather than measuring nonsense\n");
        close(dsp_fd);
        return 1;
    }

    int  frame_bytes = audio_frame_bytes(got_chan);
    audio_buf_info info;
    if (ioctl(dsp_fd, SNDCTL_DSP_GETOSPACE, &info) < 0) {
        printf("  GETOSPACE FAILED errno=%d\n", errno);
        close(dsp_fd);
        return 1;
    }
    long ring   = ((long)info.fragstotal * (long)info.fragsize) / frame_bytes;
    long period = (long)info.fragsize / frame_bytes;
    long lead   = audio_pump_lead_frames(audio_frames_for_ms(got_rate, AUDIO_PUMP_LEAD_MS),
                                         period, AUDIO_PUMP_LEAD_PERIODS, ring);

    printf("  geometry: period=%ld fr (%ld ms) | ring=%ld fr (%ld ms) | "
           "lead=%ld fr (%ld ms) at %d periods\n",
           period, audio_ms_for_frames(got_rate, period),
           ring,   audio_ms_for_frames(got_rate, ring),
           lead,   audio_ms_for_frames(got_rate, lead), AUDIO_PUMP_LEAD_PERIODS);

    /* One buffer of silence, big enough for the deepest single write (the lead). */
    int16_t *zeros = (int16_t *)calloc((size_t)ring * (size_t)got_chan, sizeof(int16_t));
    if (!zeros) { printf("  out of memory\n"); close(dsp_fd); return 1; }

    /* Prefill to the lead, blocking on purpose — this is the one place the library
     * sleeps, and it is what starts the substream. */
    {
        AudioSink sink = { dsp_write, dsp_wait, NULL };
        AudioWriteResult res;
        audio_write_frames(&sink, zeros, lead, got_chan, &WPOL_PREFILL, &res);
        printf("  prefill: %ld of %ld frames, waits=%d%s\n",
               res.frames_written, lead, res.waits,
               res.misaligned ? "  *** MISALIGNED" : "");
    }

    long buffer_size = proc_buffer_size();
    printf("  ALSA buffer_size=%ld frames (from hw_params) — a poll reporting "
           "avail_max >= this is a DRY window\n", buffer_size);
    if (buffer_size <= 0)
        printf("  *** buffer_size unreadable: the dry-window column cannot be trusted\n");

    /* Clear the latch once so the first row is not charged with the pre-prefill
     * emptiness, which is expected rather than a fault. */
    { PcmStatus st; read_status(&st); }

    /* ⚠️ Clean-to-dirty on ONE never-reset stream, and 150/200 are the POSITIVE
     * CONTROLS: both exceed the lead, so both MUST show dry windows. */
    Row rows[] = {
        { .interval_ms =  33 },
        { .interval_ms =  66 },
        { .interval_ms = 100 },
        { .interval_ms = 100, .stall_ms = WORST_FRAME_MS },
        { .interval_ms = 150 },
        { .interval_ms = 200 },
    };
    const int nrows = (int)(sizeof(rows) / sizeof(rows[0]));

    printf("\n  %-16s %6s %19s %20s %5s %5s %5s %5s %8s\n",
           "interval (ms)", "svcs", "min in_flight",
           "worst avail_max", "dry", "rewnd", "!run", "short", "stage gap");
    printf("  %-16s %6s %19s %20s %5s %5s %5s %5s %8s\n",
           "", "", "(what GETOSPACE", "(true content left)", "", "", "", "",
           "(over-claim)");
    printf("  %-16s %6s %19s\n", "", "", " claimed)");
    for (int i = 0; i < nrows; i++) {
        run_row(&rows[i], got_rate, got_chan, lead, period, ring, buffer_size, zeros);
        print_row(&rows[i], got_rate, lead, buffer_size);
        fflush(stdout);
    }

    /* ── Verdict ─────────────────────────────────────────────────────────────── */
    const Row *r200 = &rows[nrows - 1];
    const Row *r150 = &rows[nrows - 2];
    bool control_fired = (r200->dry_windows > 0 || r200->appl_rewinds > 0 ||
                          r150->dry_windows > 0 || r150->appl_rewinds > 0);

    printf("\n  POSITIVE CONTROL: 150 ms and 200 ms both exceed the %ld ms lead, so at "
           "least one MUST be dirty.\n", audio_ms_for_frames(got_rate, lead));
    if (control_fired) {
        printf("  -> fired. The clean rows above are therefore evidence.\n");
    } else {
        printf("  -> *** DID NOT FIRE. The instrument saw nothing where starvation is\n");
        printf("     arithmetically certain, so EVERY row above is worthless. Do not\n");
        printf("     read a clean 100 ms row out of this run. Check that nothing else\n");
        printf("     holds the card and that %s exists.\n", PCM_STATUS);
    }

    printf("\n  D16 reads off the 100 ms rows: a clean one means a static UI keeps\n");
    printf("  FRAME_DELAY_IDLE_US; a dirty one means audio_pump_active() is load-bearing\n");
    printf("  and the two Settings tabs need the synchronous write mode.\n");

    printf("\n  ⚠️ TWO LIMITS OF THIS INSTRUMENT, so a reader does not over-read it:\n");
    printf("  · `rewnd` misses a PERFECTLY PERIODIC recovery. It fires on appl_ptr going\n");
    printf("    BACKWARDS, and a row whose every service writes exactly the lead resets to\n");
    printf("    the same value each cycle — equal, not less. `dry` is the witness to trust;\n");
    printf("    a row with dry > 0 and rewnd == 0 is starving, not recovering cleanly.\n");
    printf("  · `stage gap` is only sampled while the state is RUNNING, so a badly\n");
    printf("    starved row samples it less often than a clean one.\n");

    free(zeros);
    close(dsp_fd);
    printf("\ndone\n");
    return 0;
}
