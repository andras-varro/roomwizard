#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

/**
 * audio_out — the output device half, and the ONE stream in this repo.
 *
 * The device is opened once, configured once, prefilled with silence and then
 * never reset and never reconfigured until shutdown.  That is the whole point:
 * a click is a stream transition, and every canned sound used to be a full stop
 * and start of the DAI (`audio.c`'s per-sound ring reset).  Native ALSA clicks
 * at a transition too, so no userspace API avoids it — only not stopping the
 * stream does.
 *
 * This is not a design.  `scummvm-roomwizard/backend-files/oss-mixer.cpp` has
 * run exactly this — one open, prefill, deadline pacing, an arithmetic-shift
 * attenuation immediately before `write()`, no reset — on this device since
 * 2026-08-03, and Full Throttle's video, MIDI, speech and effects all play well
 * (operator, 2026-08-18).  This file is that architecture moved into shared
 * code, so there is ONE implementation to maintain when the next emulator port
 * arrives rather than a third copy of open/configure/prefill/pace/attenuate.
 *
 * Everything arithmetic stays in `audio_gen.c` and is unmodified: the lead, the
 * frame counts, the write loop, the mono→interleaved expansion.  What is here is
 * the four things that need a device — configure-and-read-back, how much room the
 * queue has, the write, and the drain — plus the two policies over
 * `audio_write_frames()` that this file's two write modes need.
 *
 * ── ONE `AudioOut` PER PROCESS ────────────────────────────────────────────────
 *
 * ⚠️ A second concurrent open of the device is refused *Device or resource busy*
 * (measured, ../SYSTEM_ANALYSIS.md#34-audio), so `audio_out_open*()` refuses a
 * second LIVE instance itself rather than letting the driver produce a confusing
 * EBUSY halfway through an init.  Sequential open/close pairs are fine, which is
 * what `device_tools` relies on: it holds two `Audio` objects (`device_tools.c`
 * `do_audio_test()` and `test_audio_diag()`), neither long-lived and neither
 * reachable from inside the other's loop.
 *
 * ── THE THREADING CONTRACT ───────────────────────────────────────────────────
 *
 * There is no mutex in here, deliberately, because `native_apps` links no
 * pthread at all — static ARM plus pthread is the `clock_gettime64` →
 * SIGSEGV-before-`main()` scar (../CLAUDE.md) — and a `SCHED_RR` audio thread
 * starves this single 600 MHz core to a black screen.  So:
 *
 *   - `audio_out_open*()`, `audio_out_close()`, `audio_out_set_fill()` and
 *     `audio_out_set_shift()` are for ONE thread, and it must be the same thread
 *     for all of them.
 *   - `audio_out_service()` may run on a DIFFERENT thread from that one (ScummVM
 *     opens on its main thread and services from its audio thread), but only ever
 *     one thread at a time, and never concurrently with `set_fill`/`close`.
 *   - `audio_out_write()` — mode 2 — must run on the servicing thread, or on the
 *     owning thread while nothing is servicing.  It is refused outright while a
 *     fill callback is installed, which is what makes the common case safe by
 *     construction rather than by discipline.
 *   - The fill callback must not call back into any `audio_out_*` function.
 *   - The accessors are reads of one word and safe from anywhere; they are
 *     diagnostics, not synchronisation.
 *
 * ── THE TWO WRITE MODES, AND WHY BOTH ────────────────────────────────────────
 *
 * ⚠️ A service-driven library alone would SILENTLY MUTE two shipped Settings
 * tabs.  `hardware_config.c` and `device_tools.c` play their speaker test tones
 * with **no render loop at all** — init, tone, `usleep`, tone, close — so nothing
 * would ever call `audio_out_service()` and the tones would sit in a callback
 * that is never invoked.  Measured objectively, not inferred.
 *
 *   mode 1, SERVICED    `audio_out_service()` from a render loop or an audio
 *                       thread.  Targets a lead; never sleeps.
 *   mode 2, SYNCHRONOUS `audio_out_write()` pushes a whole buffer with a bounded
 *                       blocking policy.  Costs whole-buffer CPU, which is free
 *                       on a static UI, and the stream still never resets.
 *
 * Both go through the same never-reset stream, and `audio_out_close()` DRAINS in
 * either mode — bounded — or the queued tail is thrown away at exit.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "audio_gen.h"

/* ── Constants ──────────────────────────────────────────────────────────── */

/** What the driver's free-space report over-states the queue by, as a fraction
 *  of one period.
 *
 * ⚠️ **The device's free-space report OVER-STATES what is still queued, and this
 * is measured, not inferred.** On `.188` 2026-08-18 the OSS shim's `GETOSPACE`
 * disagreed with the kernel's own `buffer_size − avail` read at the same instant
 * by 2650–2670 frames on every `RUNNING` sample — ~60 ms at 44100, ~1.3 of the
 * shim's 2048-frame period.  That single number explains the whole interval sweep:
 * a NOMINAL 139 ms lead is ~79 ms of real audio, which is why a 66 ms service
 * interval survives with ~11 ms to spare and a 100 ms one starves ~2.5×/s.
 *
 * It is expressed as a fraction of the PERIOD because periods are the only unit
 * the shim moves in, and its period is 2048 frames at every rate and channel
 * count tested.  ⚠️ The mechanism — how much is the staged period and how much
 * a stale `hw_ptr` — is NOT established, so treat the fraction as the shape of
 * the measurement rather than as a law, and re-measure it on tinyalsa rather
 * than carrying it across.
 *
 * ⚠️ **It is deliberately NOT subtracted from the in-flight figure.** Subtracting
 * it would make the library write more, deepening the queue and the onset latency
 * to fix a starvation the measurement says does not happen at the cadence the
 * library asks for.  It is used in exactly two places, both of which would
 * otherwise be a nominal number presented as audio: the service-interval ceiling
 * below, and the close-time drain's stop condition.  */
#define AUDIO_OUT_OSPACE_SLACK_NUM   13
#define AUDIO_OUT_OSPACE_SLACK_DEN   10

/** Retry interval for the two blocking policies, in microseconds.  The serviced
 *  policy's is **0** — see `audio_out_service()`. */
#define AUDIO_OUT_PREFILL_WAIT_US    1000
#define AUDIO_OUT_SYNC_WAIT_US       5000

/** Waits mode 2 is allowed beyond the ones its own length justifies.  The bound
 *  is derived from the buffer being written (see `audio_out_write()`), so a long
 *  tone is not truncated; this is only the floor for a short one. */
#define AUDIO_OUT_SYNC_WAIT_FLOOR    64

/** Poll interval while draining at close, in microseconds. */
#define AUDIO_OUT_DRAIN_WAIT_US      2000

/* ── The device, behind a vtable ─────────────────────────────────────────────
 *
 * Injectable for exactly one reason: it is what lets `tests/audio_out_test.c`
 * drive this file on the host with **no fd in the test at all** — a simulated
 * ring that can be told to over-report, to stall, to accept half a frame, or to
 * grant a channel count nobody asked for.  The OSS implementation lives in
 * `audio_out.c` behind `audio_out_open_oss()` and is the only device code in the
 * repo below this header.
 */

typedef struct {
    long period_frames;  /**< one device period (OSS `fragsize`)              */
    long ring_frames;    /**< total capacity                                  */
    long in_flight;      /**< frames the device still holds — see the slack
                          *   constant above before believing this number     */
    long space;          /**< frames it will accept right now                 */
} AudioOutSpace;

/**
 * `open` must report what the device GRANTED, never what was asked for.
 *
 * ⚠️ Reading the granted **bits** back is not optional and closes a latent hole:
 * `oss-mixer.cpp` already warns when the device is not 16-bit and `audio.c`
 * never looked, so a device that quietly granted another width would have
 * produced noise with no diagnostic anywhere.  All three read-backs go through
 * this one call so no client can forget one.
 */
typedef struct {
    int  (*open)(void *ctx, int rate_req, int channels_req,
                 int *rate_granted, int *bits_granted, int *channels_granted);
    int  (*space)(void *ctx, int frame_bytes, AudioOutSpace *sp);
    ssize_t (*write)(void *ctx, const void *buf, size_t nbytes, bool *again);
    void (*wait)(void *ctx, int usec);
    void (*close)(void *ctx);
} AudioOutDev;

/**
 * Fill `frames` interleaved frames of `channels` channels into `buf`.
 *
 * ⚠️ **The buffer arrives ZEROED and a short fill is legal**: return the frames
 * actually produced and the remainder plays as the silence that is already
 * there.  `audio_mix_render()` deliberately does not touch the buffer on a silent
 * bus, which is exactly how stale scratch would otherwise leak into the stream.
 *
 * ⚠️ `channels` is an argument because the device grants it and no caller may
 * assume it (../CLAUDE.md, `audio_gen.h`).  The native client renders mono and
 * expands with `audio_interleave()`; ScummVM's hands `buf` straight to
 * `MixerImpl::mixCallback()`, which is why this speaks device frames rather than
 * mono samples.
 *
 * Must not call any `audio_out_*` function.
 */
typedef long (*AudioOutFill)(void *ctx, int16_t *buf, long frames, int channels);

/* ── The stream ─────────────────────────────────────────────────────────── */

typedef struct {
    const AudioOutDev *dev;
    void       *dev_ctx;
    int         oss_fd;        /**< the OSS backend's own ctx; -1 otherwise    */
    bool        is_open;

    /* What the device GRANTED.  Never what was requested. */
    int         rate;
    int         bits;
    int         channels;
    int         frame_bytes;
    bool        bits_warned;   /**< a non-16-bit grant was reported once       */

    /* Geometry, re-derived on every service because it is a MEASUREMENT: 0
     * until one has been taken, never a fallback to the requested constant. */
    long        period_frames;
    long        ring_frames;
    long        lead_frames;

    /* Post-fill attenuation, as a shift count.  See audio_out_set_shift(). */
    int         shift;

    AudioOutFill fill;
    void        *fill_ctx;
    const char  *fill_owner;

    int16_t    *buf;           /**< interleaved device scratch, grown once     */
    long        buf_frames;

    /* Diagnostics.  Each means ONE thing — see the accessors. */
    uint32_t    starved;
    uint32_t    lost;
    uint32_t    misaligned;
    uint32_t    sink_errors;
    uint32_t    refused;
    uint32_t    services;
    uint32_t    drain_waits;
    long        last_frames;
} AudioOut;

/**
 * Open a device behind an arbitrary vtable, configure it, read the grant back,
 * derive the lead from the device's own period and prefill that much silence.
 *
 * Returns 0, or -1 if the device could not be opened, granted a nonsensical
 * channel count, or another `AudioOut` is already live in this process.
 *
 * ⚠️ The prefill BLOCKS, and it is the one place in this file that is allowed to.
 * Without it the first sound is written into an empty ring and the first
 * scheduling hiccup drains it before the DAC has started.
 */
int  audio_out_open(AudioOut *out, const AudioOutDev *dev, void *dev_ctx,
                    int rate_req, int channels_req);

/**
 * The same, on `/dev/dsp`: `O_NONBLOCK`, SPEED → FMT → CHANNELS, then all three
 * read back with the read-only ioctls.
 *
 * ⚠️ **`channels_req` is a per-client argument and must stay 1 for ScummVM.**
 * Forcing stereo doubles its mixer's work and its byte count on a core already
 * at ~32 % with Full Throttle.  The native path asks for 2 and is granted 2; the
 * speaker sums L + R, so that is also the louder of the two (measured).
 */
int  audio_out_open_oss(AudioOut *out, int rate_req, int channels_req);

/**
 * Drain what is queued — bounded — then close.  Safe on a struct that was never
 * opened or whose open failed.
 *
 * The bound is the ring's own duration plus a period, and the stop condition is
 * the free-space slack rather than zero, because the device's in-flight figure
 * never reaches zero while it is over-reporting.  So a drained stream returns
 * immediately and a full one costs at most one ring.
 */
void audio_out_close(AudioOut *out);

/**
 * Install, replace or (with `fill == NULL`) remove the fill callback.
 *
 * ⚠️ **The one installed callback is what makes two writers impossible by
 * construction**, and it needs no reset to switch — which is the whole reason
 * the theremin and the mix bus can share one never-reset stream.  `owner` is a
 * static string naming the installer; it exists so the layer above can keep its
 * refusal LOUD.  A swap that went quiet would let `audio_tone()` during the
 * theremin enqueue into a mixer nobody renders, and the sound would simply
 * vanish.
 *
 * ⚠️ A caller with a release to write — `audio_stream_stop()`'s 20 ms
 * `AUDIO_OSC_FADE_OUT` — must write it BEFORE swapping itself out, or the fade
 * lands behind the whole lead and the oscillator sounds at full amplitude
 * through the gap.
 *
 * Returns 0, or -1 if the stream is not open.
 */
int  audio_out_set_fill(AudioOut *out, AudioOutFill fill, void *ctx,
                        const char *owner);

/** Who installed the current callback, or NULL if none is installed. */
const char *audio_out_fill_owner(const AudioOut *out);

/**
 * Attenuate by `shift` bits immediately before every write, post-fill.
 *
 * ⚠️ **An arithmetic SHIFT, never a gain multiply.** `-1 >> 1 == -1`, so
 * ScummVM's existing `>>1` is reproduced bit for bit; any rounding multiply
 * changes bits and the port would no longer be the thing that was verified on
 * the panel.  `shift = 0` is the identity and is what the native path uses,
 * because loudness is held IDENTICAL in this change — the level question is a
 * separate ear-verified follow-up, and confounding it with the stream change is
 * a mistake this work has already paid for twice.
 *
 * ⚠️ And this is not "one acoustic ceiling for every client": §3.4 measured that
 * a single global scalar is the wrong SHAPE, because the clean ceiling falls with
 * pitch.  At `shift = 0` it is a parameter, nothing more.
 */
void audio_out_set_shift(AudioOut *out, int shift);

/**
 * Render one service's worth and write it.  Returns frames written, or -1.
 *
 * ⚠️ **It never sleeps and never spawns a thread.** Its write policy's wait is
 * **0**, which is what makes that claim true rather than nearly true: a 1000 µs
 * wait would let `audio_write_frames()`'s bounded mid-frame realignment spend
 * `AUDIO_ALIGN_TRIES` × 1 ms inside a call the render loop believes is free.
 *
 * ⚠️ **It targets the LEAD; it never fills the free space.** An empty 743 ms ring
 * will happily accept 743 ms of audio, and then the next sound plays three
 * quarters of a second late.
 *
 * ⚠️ **The return value is the pacing signal.** A caller advancing a fixed
 * per-buffer deadline while this writes a VARIABLE frame count has two pacing
 * models and neither bounds the queue — `oss-mixer.cpp`'s deadline is exactly
 * that shape.  Advance by what was written, or drop the deadline.
 *
 * ⚠️ **A silent bus still writes silence**, which is the entire fix: an idle
 * stream is a stream transition, and a transition is the click.  So this returns
 * a positive count on a silent bus, and `0` only when the queue is already at the
 * lead.
 */
long audio_out_service(AudioOut *out);

/**
 * Mode 2: push `frames` MONO samples with a bounded blocking policy.
 *
 * For a caller with no render loop and no audio thread.  Returns the frames the
 * device took, or -1.  Refused — loudly, and counted — while a fill callback is
 * installed, because that is the two-writer case the callback exists to make
 * impossible.
 *
 * The blocking bound is derived from the buffer's own duration, so a long tone
 * waits as long as a long tone needs and only a permanently wedged device is
 * given up on.  It is a bound rather than an unlimited wait because the
 * alternative to giving up is hanging a UI.
 */
long audio_out_write(AudioOut *out, const int16_t *mono, long frames);

/**
 * The longest a caller may go between services, in microseconds.
 *
 * ⚠️ **Derived from REAL audio, not from the nominal lead.** The device's
 * in-flight figure over-reports (see `AUDIO_OUT_OSPACE_SLACK_NUM`), so the
 * nominal lead the arithmetic targets is ~1.3 periods more than the audio that
 * actually exists.  This subtracts that and keeps half a period of margin.  At
 * 44100 with the shim's 2048-frame period and a 3-period lead it comes out under
 * the 66 ms that was measured to survive with ~11 ms to spare, and well under the
 * 100 ms that starves ~2.5×/s.
 *
 * ⚠️ **This is why `audio_pump_active()` stays in the frame-pacing decision.**
 * `FRAME_DELAY_IDLE_US` is 100 000 — above this figure at every configuration
 * measured — so a render loop that drops to idle while the stream is live starves
 * it, and 100 ms alone is enough: the composite row showed a 107 ms worst frame
 * added nothing to the damage.
 *
 * 0 until a service or an open has measured the geometry.
 */
long audio_out_service_interval_us(const AudioOut *out);

/* ── Accessors: what the device GRANTED, and what the library DID ────────── */

int  audio_out_rate(const AudioOut *out);
int  audio_out_channels(const AudioOut *out);
/** The granted sample width.  16 everywhere measured; anything else is warned
 *  about once and reported here, because a silent width surprise is noise with
 *  no diagnostic. */
int  audio_out_bits(const AudioOut *out);
bool audio_out_is_open(const AudioOut *out);

/** The lead the last service TARGETED, in frames, and the device period it was
 *  rounded up to.  Both 0 until measured — "not yet measured" and "the constant"
 *  are different claims, and a diagnostic that printed
 *  `AUDIO_PUMP_LEAD_MS` once displayed 80 ms while the library held ~139. */
long audio_out_lead(const AudioOut *out);
long audio_out_period(const AudioOut *out);
/** Frames the last service or write handed to the device. */
long audio_out_last_frames(const AudioOut *out);

/** Services that found the queue DRY.  ⚠️ On a stream that is never allowed to
 *  go idle, dry is ALWAYS a fault — one audible gap each — and it is what
 *  separates a pacing problem from a mixing one. */
uint32_t audio_out_starved(const AudioOut *out);
/** Frames rendered — so the client's voices advanced past them — that the device
 *  refused.  They are gone, not deferred, and the waveform has a step where they
 *  were.  The serviced policy may not block, so this is the price of that; it is
 *  counted so the price is measured rather than assumed zero. */
uint32_t audio_out_lost(const AudioOut *out);
/** Writes that left a PARTIAL FRAME in the device.  ⚠️ Never ignorable: with an
 *  interleaved grant and no mono path underneath, half a frame swaps L and R for
 *  the rest of the stream, permanently. */
uint32_t audio_out_misaligned(const AudioOut *out);
/** Writes that failed for a reason other than "the queue is full". */
uint32_t audio_out_sink_errors(const AudioOut *out);
/** Calls refused: a mode-2 write against an installed callback, or a service
 *  with no geometry.  Counted so a silently mute client is diagnosable. */
uint32_t audio_out_refused(const AudioOut *out);
/** Services that got as far as looking at the device. */
uint32_t audio_out_services(const AudioOut *out);
/** Poll iterations the last close spent draining. */
uint32_t audio_out_drain_waits(const AudioOut *out);

#endif /* AUDIO_OUT_H */
