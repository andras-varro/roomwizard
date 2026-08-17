#ifndef AUDIO_GEN_H
#define AUDIO_GEN_H

/**
 * audio_gen — the audio logic that has no device in it.
 *
 * Everything here is a pure function of its arguments: no fd, no ioctl, no
 * clock, no sysfs.  That is the whole point — it is the half of `audio.c` a
 * host regression can reach (`tests/audio_gen_test.c`), and the half that must
 * survive the OSS → ALSA port unchanged (../IMPROVEMENT_PLAN.md F1).
 *
 * The device half — open/configure/write/close plus the GPIO12 amp poke —
 * stays in `audio.c` and moves to `audio_dev.c` when it becomes tinyalsa.
 *
 * Two rules this file exists to enforce:
 *
 *   1. THE CHANNEL COUNT IS AN ARGUMENT, never a literal.  The device grants it
 *      (`hw:0,0` grants exactly 2 — measured, ../SYSTEM_ANALYSIS.md#34-audio);
 *      a caller that assumes is only accidentally right.  Every byte count here
 *      derives from a channel count handed in.
 *   2. A WRITE NEVER STOPS MID-FRAME.  The generator is mono and the device is
 *      interleaved stereo, so half a frame handed to the kernel swaps L and R
 *      for the rest of the stream — permanently, with no mono path underneath
 *      to absorb it.  `audio_write_frames()` is the only place that decides
 *      when to stop, and it stops on frame boundaries or says it could not.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

/** Bytes per sample: S16_LE everywhere, on both the OSS and the ALSA path. */
#define AUDIO_BYTES_PER_SAMPLE   2

/** Peak amplitude for synthesised waveforms (0–32767).
 *  ≈55 % of full scale.  The speaker SUMS L and R (measured), so two identical
 *  full-scale channels would drive it at double amplitude and distort. */
#define AUDIO_PEAK               18000

/** Longest tone this library will render.  A longer request is a caller bug:
 *  clamped here rather than overflowed, because `(long)rate * duration_ms` is a
 *  32-bit multiply on this target and 44100 × 49 000 exceeds INT32_MAX. */
#define AUDIO_MAX_TONE_MS        30000

/** Attack and release of a plain tone, in ms — removes the click at each end. */
#define AUDIO_ATTACK_MS          10
#define AUDIO_RELEASE_MS         20

/** Frequency smoothing factor per sample (higher = faster glide). */
#define AUDIO_FREQ_SMOOTH        0.15

/** Amplitude ramp per sample for the streaming fade-in. */
#define AUDIO_AMP_RAMP           0.0005

/** Extra attempts `audio_write_frames()` makes to finish a partial frame after
 *  its stop condition has already fired.  Bounded, because the alternative to
 *  giving up is hanging: 4 × the policy's wait is the worst case. */
#define AUDIO_ALIGN_TRIES        4

/** Voices the mix bus can hold at once.  A request past this is REFUSED and
 *  counted, never allowed to steal a playing voice — see `audio_mix_add()`. */
#define AUDIO_MAX_VOICES         8

/** Target queue depth the pump keeps inside the device, in ms.
 *
 *  This is the whole latency budget of the mix bus: audio already handed to the
 *  kernel cannot be mixed into, so a newly triggered sound starts at most this
 *  late.  It is also the only cushion against a slow render-loop iteration, so
 *  it must exceed a couple of frames at `FRAME_DELAY_ACTIVE_US` (33 ms). */
#define AUDIO_PUMP_LEAD_MS       80

/** Most audio one `audio_pump()` call will render, in ms.  Bounds the work a
 *  single render-loop iteration can be made to do. */
#define AUDIO_PUMP_CAP_MS        80

/** Whole device periods the pump must stay ahead by, at minimum.
 *
 * ⚠️ **A lead in MILLISECONDS is not enough, and this is measured.** The OSS shim
 * hands ALSA whole periods only — `appl_ptr` on `.188` moves 2048 → 4096 → 6144
 * and never lands between — while `GETOSPACE` counts the partial period it is
 * still staging.  So an 80 ms lead against a 2048-frame (46 ms) period is 1.7
 * periods, of which ALSA can *play* one: it drains that, the next is not complete
 * yet, and it goes `XRUN`.  The shim's recovery **discards** what was staged, so
 * every ~120 ms a crack, sounds cut short, and a tone chopped 8×/s that reads as
 * distortion.  Measured on `.188` 2026-08-15 by polling
 * `/proc/asound/card0/pcm0p/sub0/status` while keepalive ran; the operator
 * independently counted "20–25 cracks" in a 3 s drone, i.e. the same cadence.
 *
 * Three is the smallest depth that leaves a whole spare period *while* one is
 * being staged and one is playing.  ⚠️ **The lead is also the latency ceiling** —
 * at a 46 ms period this buys ~139 ms, which is the price of the OSS shim's
 * period size and is exactly what Phase 4 buys back (tinyalsa granted
 * `period_size=1024`, i.e. 23 ms, measured in Phase 0). */
#define AUDIO_PUMP_LEAD_PERIODS  3

/**
 * The lead the pump should actually target, in frames.
 *
 * PURE: the caller measures `period_frames` off the device (`SNDCTL_DSP_GETOSPACE`'s
 * `fragsize`, or an ALSA period) and passes it in.  `period_frames <= 0` means
 * nothing was measured, and then the ms figure is all there is to go on.
 *
 * Rounds UP to a whole period, because a target between two periods is what
 * produced the XRUN cycle above, and caps at half the ring so a deep lead on a
 * small buffer cannot ask for more cushion than the device can hold.
 */
long audio_pump_lead_frames(long lead_ms_frames, long period_frames,
                            int min_periods, long ring_frames);

/** Where the summed bus stops being linear, and where it asymptotes.
 *
 * ⚠️ **The knee is `AUDIO_PEAK` exactly, and that is the load-bearing choice.**
 * One voice can never exceed it, so a single sound is BYTE-IDENTICAL through the
 * limiter — the property `audio_render_tone()` parity depends on.  Two voices
 * reach 36000 and three reach 54000, which is what a panel heard as *"a
 * distorted square wave from an overdriven amplifier"* (measured on `.188`
 * 2026-08-15, `clip` 15402) when the only thing above the knee was a hard clamp.
 *
 * The ceiling is below 32767 on purpose: with the soft curve engaged the store
 * can no longer wrap, so **`clipped` must reach exactly 0** — that is the
 * negative control for this whole change, not a comfort margin.  It is also only
 * ~1.4× one voice, because `AUDIO_PEAK` is ≈55 % of full scale for an ACOUSTIC
 * reason (`SPKR1` sums L + R and distorts near full scale), so a limiter that
 * merely avoided int16 wrap by aiming at 32767 would still overdrive the
 * speaker. */
#define AUDIO_MIX_KNEE           AUDIO_PEAK
#define AUDIO_MIX_CEIL           26000

/* ── Frame and byte arithmetic ───────────────────────────────────────────── */

/** Bytes in one interleaved frame.  0 for a nonsensical channel count. */
int  audio_frame_bytes(int channels);

/** Frames in `duration_ms` at `rate`, computed 64-bit and clamped to
 *  AUDIO_MAX_TONE_MS.  0 for a non-positive rate or duration. */
long audio_frames_for_ms(int rate, int duration_ms);

/** Bytes for `frames` interleaved frames of `channels` channels. */
long audio_bytes_for_frames(long frames, int channels);

/** The inverse of `audio_frames_for_ms()`, and it exists so a DIAGNOSTIC can
 *  report the lead the pump actually took rather than the constant it asked for.
 *  ⚠️ It does **not** clamp: `audio_frames_for_ms()` clamps because a caller's
 *  *request* may be nonsense, while a frame count handed back by the device is a
 *  measurement — clamping it would hide exactly the surprise worth reporting.
 *  0 for a non-positive rate or frame count. */
long audio_ms_for_frames(int rate, long frames);

/* ── Clock arithmetic, without a clock ───────────────────────────────────── */

/**
 * `struct timeval` → ms, as `time_now_ms()` computes it.
 *
 * Only the low 22 bits of the seconds are used, so the result peaks at
 * 4,194,303,999 against UINT32_MAX 4,294,967,295 — it does NOT overflow.  It
 * wraps every ~48.5 days instead, and `audio_flush_wait_ms()`'s cap is what
 * bounds the consequence of a wrap to one short wait.
 */
uint32_t audio_ms_from_timeval(long tv_sec, long tv_usec);

/** How long to wait for the current sound to finish: 0 if it already has,
 *  otherwise `end - now` capped at `cap`.  The cap is load-bearing — across a
 *  clock wrap `end - now` is nonsense, and this is what makes it harmless. */
uint32_t audio_flush_wait_ms(uint32_t now, uint32_t end, uint32_t cap);

/* ── Tone envelope ──────────────────────────────────────────────────────── */

/** Attack/release lengths in frames, each clamped to half the tone so a short
 *  tone still has both, and both 0 for a 1-frame tone (no divide by zero). */
long audio_attack_frames(int rate, long frames);
long audio_release_frames(int rate, long frames);

/** Envelope at sample `i`: 0 → 1 over the attack, 1, then 1 → 0 over the
 *  release.  Continuous at both joins, including when they meet at frames/2. */
double audio_tone_env(long i, long frames, long attack, long release);

/** Render `frames` MONO samples of a fixed-frequency tone with that envelope. */
void audio_render_tone(int rate, int freq_hz, int peak, int16_t *mono, long frames);

/* ── The one gliding oscillator ─────────────────────────────────────────── */

/**
 * Streaming oscillator: phase accumulator + frequency glide + amplitude ramp.
 *
 * This is ONE implementation of what `audio.c` had in three places (the
 * stream prefill and the stream chunk were identical; the fade-out was a
 * variant that deliberately holds frequency and amplitude still).  The variant
 * is a MODE, not a second copy — deleting it would delete the fade.
 */
typedef enum {
    AUDIO_OSC_GLIDE    = 0,  /**< smooth freq toward target, ramp amp up to 1  */
    AUDIO_OSC_FADE_OUT = 1   /**< hold freq and amp, envelope 1 → 0 over the call */
} AudioOscMode;

typedef struct {
    int    rate;         /**< frames per second                               */
    double phase;        /**< radians, wrapped to [0, 2π)                     */
    double freq;         /**< current frequency (Hz)                          */
    double target_freq;  /**< glide target (Hz)                               */
    double amp;          /**< current amplitude, 0..1                         */
    double freq_smooth;  /**< glide factor per sample                         */
    double amp_ramp;     /**< amplitude ramp per sample                       */
    int    peak;         /**< peak amplitude at amp == 1                      */
} AudioOsc;

/** Start at `freq_hz` with amplitude 0 (so GLIDE fades in) and phase 0. */
void audio_osc_init(AudioOsc *o, int rate, double freq_hz, int peak);

/**
 * Render `frames` MONO samples, advancing the oscillator's state.
 *
 * Split calls are identical to one long call — that is what lets the caller
 * write in chunks of whatever the ring will take without a seam.
 */
void audio_osc_render(AudioOsc *o, AudioOscMode mode, int16_t *mono, long frames);

/* ── Mono → interleaved: the one conversion point ───────────────────────── */

/**
 * Duplicate `frames` mono samples into `channels` interleaved channels.
 *
 * The hardware is mono and the interface is not, so this is where the two meet
 * and nothing above it needs to know.  `SPKR1` sees L + R (measured), so
 * duplicating is correct and is the loudest of the options.
 *
 * `out` must hold `audio_bytes_for_frames(frames, channels)` bytes.  Returns
 * that byte count, or 0 if it could not.
 */
long audio_interleave(const int16_t *mono, long frames, int channels, int16_t *out);

/* ── The frame-aligned write loop ───────────────────────────────────────── */

/**
 * Where the bytes go.  `write` returns bytes accepted, or -1; on -1 it sets
 * `*again` when the sink was merely full (EAGAIN) rather than broken.  `wait`
 * may be NULL, and is the only thing in this file that is allowed to block.
 */
typedef struct {
    ssize_t (*write)(void *ctx, const void *buf, size_t nbytes, bool *again);
    void    (*wait)(void *ctx, int usec);
    void     *ctx;
} AudioSink;

typedef struct {
    int  wait_us;        /**< passed to `wait` between retries                */
    int  max_waits;      /**< <= 0 for unlimited                              */
    bool stop_on_again;  /**< give up at the first full sink instead of waiting */
} AudioWritePolicy;

typedef struct {
    long frames_written; /**< whole frames the sink took                      */
    long bytes_written;  /**< always frames_written * frame_bytes ...          */
    bool misaligned;     /**< ... unless this is true: a partial frame is in
                          *   the device and L/R are now swapped.  A fault to
                          *   report, never to ignore.                        */
    bool sink_error;     /**< the sink failed for a reason other than "full"  */
    int  waits;          /**< how many times `wait` was called                */
} AudioWriteResult;

/**
 * Write `frames` interleaved frames, stopping only on a frame boundary.
 *
 * `stop_on_again` and `max_waits` are honoured at frame boundaries only: mid
 * frame it keeps going (up to AUDIO_ALIGN_TRIES further attempts) because
 * stopping there is not a lesser evil, it is a permanent channel swap.
 */
void audio_write_frames(const AudioSink *sink, const void *buf, long frames,
                        int channels, const AudioWritePolicy *pol,
                        AudioWriteResult *res);

/* ── The mix bus ────────────────────────────────────────────────────────────
 *
 * You cannot mix into a buffer the kernel already holds.  Today's fire-and-
 * forget `audio_tone()` writes a whole tone at once, so a second sound arrives
 * too late to be summed and `SNDCTL_DSP_RESET` throws the first one away — one
 * sound at a time, by construction.  Real mixing needs userspace to hold the
 * audio and write it incrementally, which means a thread or a per-frame pump.
 *
 * It is a PUMP.  `native_apps` links no pthread at all, and static ARM plus
 * pthread is how you get `clock_gettime64` → `-ENOSYS` → SIGSEGV before
 * `main()` (../CLAUDE.md).  So `audio.c` renders from this bus once per frame,
 * next to `fb_swap()`.
 *
 * Everything here is still pure: no fd, no ioctl, no clock.  Time is counted in
 * FRAMES the caller has asked for, so the whole bus — summing, clipping, voice
 * lifetime, the arpeggio delays — is host-testable (`tests/audio_gen_test.c`).
 */

/**
 * One sounding tone.  Renderable INCREMENTALLY: `pos` and `phase` are all the
 * state, so N + M frames are identical to one N+M-frame render, which is what
 * lets the pump write whatever the device will take this frame without a seam.
 *
 * `delay` is what keeps a canned arpeggio an arpeggio.  `audio_success()` is
 * three notes, and three voices all starting now is a CHORD — a different sound
 * at 45 call sites.  Offsetting each note by the ones before it preserves what
 * the panel already hears while still mixing with everything else.
 */
typedef struct {
    bool   active;      /**< false = free slot                               */
    double phase;       /**< radians, wrapped to [0, 2π)                     */
    double phase_step;  /**< 2π · freq / rate                                */
    int    peak;        /**< this voice's amplitude — the per-voice gain      */
    long   delay;       /**< frames of silence still owed before it sounds    */
    long   frames;      /**< total length                                    */
    long   pos;         /**< frames sounded so far (0..frames)               */
    long   attack;      /**< envelope, in frames — same curve as a plain tone */
    long   release;
} AudioVoice;

typedef struct {
    int        rate;
    AudioVoice v[AUDIO_MAX_VOICES];
    uint32_t   dropped;  /**< adds refused because every slot was busy       */
    uint32_t   clipped;  /**< samples the summed bus drove past int16 range   */
    uint32_t   limited;  /**< samples the soft knee had to bend              */
    int        limit;    /**< AudioMixLimit — how the sum leaves the bus      */
} AudioMixer;

/**
 * What happens to a summed sample that is louder than one voice can be.
 *
 * `HARD` is what Phase 3 shipped and what the panel rejected; it stays reachable
 * because a fix with no negative control beside it is a claim, not a measurement
 * (`tests/audio_mix_test`'s LIMIT toggle is that control).
 */
typedef enum {
    AUDIO_MIX_SOFT = 0,  /**< linear to AUDIO_MIX_KNEE, then asymptotic to CEIL */
    AUDIO_MIX_HARD = 1   /**< clamp at int16 — a square wave at three voices    */
} AudioMixLimit;

/**
 * Map one summed sample to what the device should hear.  PURE — no mixer, no
 * counters, so a test can sweep it.
 *
 * `SOFT` is `y = K + (C-K)·u/(1+u)` with `u = (|x|-K)/(C-K)`: continuous in value
 * AND in slope at the knee (both are 1 there), monotone, odd-symmetric, and
 * bounded by `C` for any input — so it needs no clamp behind it and cannot wrap.
 * Below the knee it is the identity, which is what keeps one voice unchanged.
 */
int32_t audio_mix_limit(int32_t acc, int mode);

/** Choose the limiter.  Takes effect on the next rendered sample; it changes
 *  only samples ABOVE the knee, so switching it mid-tone cannot step a quiet
 *  one and the panel can A/B it while a drone is running. */
void audio_mix_set_limit(AudioMixer *m, int mode);

/** Reset the bus to silence at `rate`.  Clears the diagnostics too. */
void audio_mix_init(AudioMixer *m, int rate);

/**
 * Add one tone voice.  Returns its slot, or -1.
 *
 * ⚠️ **A full bus REFUSES and counts (`dropped`); it never steals a voice.**
 * Stealing the oldest is what a synth does, and it is wrong here: the longest
 * voice on this bus is the one thing a dropped UI beep must not be allowed to
 * cut — background music (../IMPROVEMENT_PLAN.md F19) is a 44 s voice, and a
 * missing blip is far cheaper than a chopped soundtrack.
 *
 * `peak <= 0` is a caller bug, refused WITHOUT counting a drop: `dropped` means
 * "the bus was full", which is the number worth watching.
 */
int  audio_mix_add(AudioMixer *m, int freq_hz, int duration_ms,
                   int delay_ms, int peak);

/** Silence every voice at once.  What `audio_interrupt()` becomes when pumping:
 *  it cannot un-write audio already inside the device, so up to
 *  AUDIO_PUMP_LEAD_MS of tail survives it.  That is the price of not resetting
 *  the ring, which is the very thing that makes mixing impossible. */
void audio_mix_stop_all(AudioMixer *m);

/** Voices occupying a slot — sounding or still waiting out their delay. */
int  audio_mix_active(const AudioMixer *m);

/** Frames until the bus falls silent (delay + remaining length, worst voice).
 *  0 means there is nothing to render, which is how the pump knows to write
 *  nothing at all rather than a buffer of silence. */
long audio_mix_pending(const AudioMixer *m);

/**
 * Render and sum `frames` MONO samples, advancing every voice.  Returns the
 * frames written, or 0 when the bus is silent — in which case `mono` is NOT
 * touched.
 *
 * The sum accumulates in `int32_t` and leaves the bus through
 * `audio_mix_limit()`, once, at the store — so the result does not depend on
 * which slot a voice happens to sit in.  Both outcomes count: `limited` for a
 * sample the soft knee bent, `clipped` for one the int16 store could not hold.
 * ⚠️ **Under `AUDIO_MIX_SOFT`, `clipped` must stay 0** — the curve is bounded by
 * `AUDIO_MIX_CEIL`, so a non-zero count there is a defect, not a loud sound.
 *
 * A single voice is BYTE-IDENTICAL to `audio_render_tone()` of the same tone —
 * so switching an app to the pump cannot change how one sound is heard.  That
 * survives the limiter because one voice cannot reach the knee.
 */
long audio_mix_render(AudioMixer *m, int16_t *mono, long frames);

/**
 * How many frames the pump should render this call.  Pure arithmetic; the
 * caller gets `in_flight`/`space` off the device and the two `_MS` constants
 * through `audio_frames_for_ms()`.
 *
 * ⚠️ **The pump targets a LEAD, it does not fill the ring.** An empty ~506 ms
 * OSS ring will happily accept 506 ms of audio, and then a tone triggered on
 * the next frame plays half a second late.  `lead` is therefore both the queue
 * depth and the latency ceiling, and this returns 0 whenever we are already
 * that far ahead.
 */
long audio_pump_frames(long lead, long in_flight, long space, long cap);

#endif /* AUDIO_GEN_H */
