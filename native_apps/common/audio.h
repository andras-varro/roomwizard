#ifndef AUDIO_H
#define AUDIO_H

/**
 * Audio Library for RoomWizard native games
 *
 * Hardware path:
 *   TWL4030 HiFi DAC  →  HandsfreeL/R class-D amp  →  SPKR1
 *   Kernel interface:  /dev/dsp  (ALSA OSS compatibility layer)
 *   Amp enable:        GPIO12 sysfs (active HIGH)
 *
 * Prerequisites:
 *   /etc/init.d/audio-enable must have run to configure the ALSA
 *   HandsfreeL/R mixer path.  This is installed automatically by
 *   build-and-deploy.sh with the 'permanent' flag.
 *   audio_init() still drives GPIO12 directly so it works even when
 *   called from a dev shell before the boot service is active.
 *
 * Typical use:
 *   Audio audio;
 *   if (audio_init(&audio) == 0) {
 *       audio_beep(&audio);          // UI click sound
 *       audio_success(&audio);       // level-up fanfare
 *       audio_close(&audio);
 *   }
 *
 * If /dev/dsp cannot be opened audio_init() returns -1 and sets
 * audio.available = false.  All playback functions are silent no-ops
 * in that state, so games work without audio hardware.
 */

#include <stdint.h>
#include <stdbool.h>

#include "audio_gen.h"
#include "audio_out.h"

typedef struct {
    int      dsp_fd;          /**< /dev/dsp file descriptor (-1 = not open)      */
    int      sample_rate;     /**< Negotiated sample rate (read back, typ. 44100) */
    int      channels;        /**< Negotiated channel count (READ BACK, not a
                               *   literal).  Every byte count derives from this;
                               *   see audio_gen.h.  Never 0 on an open device.  */
    bool     available;       /**< false if /dev/dsp could not be opened           */
    bool     ch_warned;       /**< channel read-back already complained once       */
    uint32_t sound_end_ms;    /**< Expected wall-clock end of current tone (ms)    */
    /* ── streaming theremin state: ONE oscillator, in audio_gen.h ── */
    AudioOsc osc;             /**< phase + frequency glide + amplitude ramp        */
    bool     streaming;       /**< true while streaming audio chunks               */
    /* ── the mix bus: OPTIONAL, off unless audio_pump_enable() said so ── */
    AudioMixer mix;           /**< the voices; silent and unused when !pumping     */
    bool     pumping;         /**< audio_tone() enqueues instead of writing        */
    bool     keepalive;       /**< keep writing silence when the bus is idle       */
    int16_t *pump_buf;        /**< render scratch, allocated once on first pump    */
    long     pump_buf_frames; /**< its size                                       */
    uint32_t pump_starved;    /**< pumps that found the ring DRY with audio still
                               *   owed — one audible gap each, and the number
                               *   that separates "the mixer is wrong" from "the
                               *   render loop was too slow to feed it"           */
    uint32_t pump_lost;       /**< frames rendered (so the voices advanced past
                               *   them) that the device did not take.  Silent
                               *   data loss under WPOL_PUMP's stop_on_again       */
    int      pump_diag;       /**< bounded per-pump stderr trace: lines left.  Set
                               *   by audio_pump_enable(), so it is 40 lines per
                               *   PUMP: ON session and can never flood a log      */
    long     pump_lead;       /**< the lead audio_pump() LAST TARGETED, in frames,
                               *   0 until a pump has read the device's period.
                               *   ⚠️ Recorded because it is derived from fragsize
                               *   and is NOT AUDIO_PUMP_LEAD_MS — a diagnostic
                               *   that printed the constant said 80 ms while the
                               *   library was holding ~139                        */
    long     pump_period;     /**< the device period the lead was rounded to, in
                               *   frames.  Reported beside the lead so the panel
                               *   shows the arithmetic, not just its result        */
    /* ── the continuous stream: the device half moved out of this file ──────
     *
     * ⚠️ `cont` selects WHICH DEVICE HALF is open, and the two are mutually
     * exclusive: `dsp_fd` is this file's own fd, `out` is `audio_out`'s.  A
     * second concurrent open of /dev/dsp is EBUSY, so switching closes one
     * before opening the other — which is why the toggle is an instrument for
     * one panel and not something to flip inside a game.
     */
    AudioOut out;             /**< the one never-reset stream; valid iff cont      */
    bool     cont;            /**< the continuous stream owns the device           */
    bool     osc_stream;      /**< the theremin owns the fill callback             */
} Audio;

/**
 * Initialise audio subsystem.
 *  - Honours the `audio_enabled` config setting
 *  - Drives GPIO12 HIGH (enables on-board speaker amplifier)
 *  - Opens /dev/dsp, sets rate/format/channels and READS BACK rate + channels
 * Returns 0 on success, -1 if hardware unavailable (game may continue).
 */
int  audio_init(Audio *audio);

/**
 * The same, WITHOUT the config gate.
 *
 * For a hardware *test* that must be able to drive the speaker even when the
 * user has switched audio off in config — `device_tools` and `hardware_config`
 * both have such a tab, and both used to hand-roll the open, the three ioctls
 * and the GPIO12 poke themselves (../IMPROVEMENT_PLAN.md F1).  That duplication
 * is what this exists to remove; the bypass itself is deliberate, because
 * audio_init() would make the test obey the very setting it exists to test.
 *
 * ⚠️ Any other caller wants audio_init().  A struct filled by hand instead of
 * by one of these two leaves `channels` at 0 and goes SILENTLY mute.
 */
int  audio_init_unchecked(Audio *audio);

/**
 * Close /dev/dsp and release resources.
 * Safe to call even if audio_init() failed.
 */
void audio_close(Audio *audio);

/**
 * Flush any audio still queued in the kernel OSS ring buffer and
 * prepare for immediate playback of the next tone.
 *
 * Call this before audio_tone() when triggering a new sound that should
 * interrupt whatever is currently playing (e.g. rapid game events).
 * The convenience functions (audio_beep, audio_blip, audio_success,
 * audio_fail) call this internally — only needed for direct audio_tone()
 * callers.
 *
 * ⚠️ **With the pump enabled this becomes "stop all voices", and it no longer
 * resets the ring** — resetting the ring is precisely what makes mixing
 * impossible, so up to AUDIO_PUMP_LEAD_MS of already-written tail still sounds.
 * The signature is unchanged because ~23 call sites use it, most of them as
 * `audio_interrupt(); audio_tone();` — which on the pump means "replace what is
 * playing", the same intent, without the ~50 ms DAC settle after it.
 */
void audio_interrupt(Audio *audio);

/**
 * Play a sine-wave tone through SPKR1.
 *
 * Does NOT block for the sound's duration: the fd is O_NONBLOCK, so this
 * returns once the kernel ring has taken the samples and the DAC clocks them
 * out afterwards.  Only audio_interrupt()/the canned sounds ever sleep, and
 * never for more than 200 ms.
 *
 * With the pump enabled it does not write at all — it adds a voice and
 * audio_pump() writes it, summed with whatever else is sounding.
 *
 * @param freq_hz     Frequency 20–8000 Hz
 * @param duration_ms Duration in milliseconds (clamped to AUDIO_MAX_TONE_MS)
 */
void audio_tone(Audio *audio, int freq_hz, int duration_ms);

/* ── The mix bus: OPTIONAL, per-frame ──────────────────────────────────────
 *
 * Two sounds at once needs userspace to hold the audio and hand the device
 * small pieces of it, because you cannot mix into a buffer the kernel already
 * has.  This library does that from the render loop — never a thread: static
 * ARM plus pthread is the SIGSEGV-before-main() scar (../CLAUDE.md).
 *
 * ⚠️ **It is opt-in, and that is a safety property rather than a convenience.**
 * An app that never calls audio_pump_enable() takes exactly the code path it
 * takes today, byte for byte: audio_tone() renders the whole tone and writes
 * it.  So an unconverted binary sounds unchanged instead of going SILENT, which
 * is the failure this project describes as "does not error — it misparses", and
 * which nobody would notice until they played that game.
 *
 * Converting an app is three lines:
 *
 *     audio_init(&audio);
 *     audio_pump_enable(&audio, true);        // once, after init
 *     while (running) {
 *         ...
 *         audio_pump(&audio);                 // once per frame, next to fb_swap()
 *         usleep((drew || audio_pump_active(&audio)) ? FRAME_DELAY_ACTIVE_US
 *                                                    : FRAME_DELAY_IDLE_US);
 *     }
 *
 * ⚠️ **That last line matters.** The pump keeps only AUDIO_PUMP_LEAD_MS (80 ms)
 * of audio inside the device, so a loop that drops to FRAME_DELAY_IDLE_US
 * (100 ms) mid-sound starves it and you hear a gap.  audio_pump_active() is the
 * same idiom as gameover_needs_redraw(): the component is asked, because it is
 * the only thing that knows it still owes the device frames.
 */

/** Turn the mix bus on or off.  Turning it off silences every voice, so it is
 *  safe to toggle at runtime (`tests/audio_mix_test` does exactly that to A/B
 *  the two paths on one panel). */
void audio_pump_enable(Audio *audio, bool on);

/** Render and write whatever the device will take, up to the lead target.
 *  Call once per frame.  A no-op when the pump is off or the bus is silent. */
void audio_pump(Audio *audio);

/** True while the bus still owes the device audio — see the frame-pacing note
 *  above.  Always false when the pump is off, and always TRUE while keepalive
 *  is on, because a promise of continuous silence is also a promise of frames. */
bool audio_pump_active(const Audio *audio);

/** Voices occupying a slot right now (0..AUDIO_MAX_VOICES). */
int  audio_pump_voices(const Audio *audio);

/** Samples the summed bus could not store in an int16.
 *
 * ⚠️ **Under the default soft limiter this must read 0**, because the curve
 * asymptotes below full scale — so this is the negative control for the limiter
 * being engaged at all, not a loudness gauge.  It read **15402** on `.188`
 * 2026-08-15 with the hard clamp, which a panel heard as an overdriven square
 * wave (../IMPROVEMENT_PLAN.md F1 Phase 3). */
uint32_t audio_pump_clipped(const Audio *audio);

/** Samples the soft knee bent — the sum was louder than one voice can be.
 *  Expected to be large whenever sounds overlap; it is a level indicator, not a
 *  fault. */
uint32_t audio_pump_limited(const Audio *audio);

/** Pumps that found the ring dry while voices still owed audio: one audible gap
 *  each.  ⚠️ **This is the number that tells a mixing defect from a PACING one** —
 *  the pump holds only AUDIO_PUMP_LEAD_MS, so any render-loop iteration longer
 *  than that starves the device however correct the mix is. */
uint32_t audio_pump_starved(const Audio *audio);

/** Frames the bus rendered — advancing its voices past them — that the device
 *  refused to take.  WPOL_PUMP never blocks, so a full ring drops them; they are
 *  gone, and a non-zero count is a discontinuity in the waveform. */
uint32_t audio_pump_lost(const Audio *audio);

/** The lead the pump LAST TARGETED, in frames, and the device period it was
 *  rounded up to — both 0 until a pump call has read the period off the device.
 *
 * ⚠️ **A diagnostic must report what the library did, not what the header asked
 * for.**  `AUDIO_PUMP_LEAD_MS` is a *request*: `audio_pump_lead_frames()` floors
 * it at AUDIO_PUMP_LEAD_PERIODS whole periods and rounds up, which on the OSS
 * shim's 46 ms period makes the effective lead ~139 ms rather than 80.
 * `tests/audio_mix_test` printed the constant and so displayed `lead 80 ms` while
 * the library held nearly twice that — and it coloured its worst-frame warning
 * against the same wrong number.  Use these; `audio_ms_for_frames()` converts.
 *
 * They are a *measurement*, so they stay at 0 rather than falling back to the
 * constant: "not yet measured" and "80 ms" are different claims. */
long audio_pump_lead(const Audio *audio);
long audio_pump_period(const Audio *audio);

/** Choose how the summed bus leaves the mixer: AUDIO_MIX_SOFT (default) or
 *  AUDIO_MIX_HARD, the pre-limiter clamp, kept so a panel can A/B them. */
void audio_pump_set_limit(Audio *audio, int mode);

/** Sounds refused because all AUDIO_MAX_VOICES slots were busy.  A full bus
 *  never steals a playing voice; see audio_gen.h. */
uint32_t audio_pump_dropped(const Audio *audio);

/** Keep writing silence while the bus is idle, instead of writing nothing.
 *
 * ⚠️ **This exists to be MEASURED, and it is off by default.**  `audio.c`'s
 * ~60 ms minimum-tone rule is attributed to TWL4030 DAC start-up under the
 * SNDCTL_DSP_RESET regime; a stream that is never allowed to go idle would
 * remove it, and the "klack" heard between two Phase 0 playbacks is consistent
 * with that — but consistent-with is not measured.  It costs ~176 KB/s of
 * writes at 44100 Hz stereo, so it is not free either.  `audio_mix_test` has a
 * toggle for it beside a 5/10/20/40/60/100 ms tone row, which is what settles
 * the question by ear.  (../IMPROVEMENT_PLAN.md F1 Phase 3.) */
void audio_pump_set_keepalive(Audio *audio, bool on);

/* ── The continuous stream ──────────────────────────────────────────────────
 *
 * F1 defect 3's fix.  `audio_flush()` fires SNDCTL_DSP_RESET before every canned
 * sound, so every game sound is a full stream stop and start and every boundary
 * is a DAI teardown that clicks — the operator's *"every time there is a sound,
 * there is a click"*.  `common/audio_out.c` is the device half that never resets;
 * this switch chooses between it and the old path.
 *
 * ⚠️ **The old path is deliberately reachable, and it is the negative control.**
 * `tests/audio_mix_test` has a CONT toggle beside PUMP/KEEP/LIMIT for exactly
 * that reason: a click that survives CONT: ON is not the one this change removes.
 *
 * Three things follow from turning it on, all measured or derived rather than
 * assumed, and all of them visible on that panel:
 *
 *   - **The mix bus becomes the only sound source**, because a continuous stream
 *     needs something to fill it every service.  So CONT implies PUMP, and
 *     audio_pump_enable(a, false) while CONT is on is refused LOUDLY rather than
 *     leaving a stream nobody writes.
 *   - **audio_pump() becomes the service call** and audio_pump_active() is
 *     always true — the stream must be serviced whatever the bus is doing, and
 *     the ceiling for how often is audio_out_service_interval_us().
 *   - ⚠️ **audio_tone() defaults its delay to the CURRENT TAIL rather than 0.**
 *     Without that, `tetris.c:620-621`, `tetris.c:714-715` and `snake.c:317-318`
 *     — two audio_tone()s back to back with no audio_interrupt() between them —
 *     turn from two-note motifs into dyads, because today it is the ring that
 *     serialises them and a mix bus will not.
 *
 * Returns 0 on success, -1 if the device could not be handed over (in which case
 * the previous path is restored, so a failed toggle is not a silent mute).
 */
int  audio_cont_enable(Audio *audio, bool on);

/** True while the continuous stream owns the device. */
bool audio_cont_active(const Audio *audio);

/** How often audio_pump() must be called on the continuous stream, in
 *  microseconds, or 0 when nothing has measured the device yet.  Derived from
 *  REAL audio rather than the nominal lead — see audio_out.h. */
long audio_cont_service_interval_us(const Audio *audio);

/* ── Convenience sounds ────────────────────────────────────────────────────
 * Each first waits (≤200 ms) for whatever is still playing, then queues its
 * own tones and returns.  Layer calls for chord effects.
 *
 * On the pump they instead add one voice per note, each offset by the notes
 * before it — so `audio_success()` is still an ascending arpeggio and not a
 * chord, and it mixes with whatever else is sounding instead of discarding it.
 * All four signatures are unchanged; there are ~45 call sites.
 */

/** Short 880 Hz blip (~80 ms)  — UI click, tile place, button press */
void audio_beep(Audio *audio);

/** Short 1320 Hz blip (~60 ms) — item collected, food eaten           */
void audio_blip(Audio *audio);

/** C5→E5→G5 ascending arpeggio (~440 ms) — score milestone, level up */
void audio_success(Audio *audio);

/** G4→E4→C4 descending tone  (~600 ms)   — game over, error          */
void audio_fail(Audio *audio);

/* ── Streaming (theremin) API ──────────────────────────────────────────────
 * For continuous pitch-gliding audio driven by a touch loop.
 *
 * ⚠️ **This path is ALWAYS the continuous stream — it has no old-path branch.**
 * It used to bracket itself with two SNDCTL_DSP_RESETs (start and stop) and own
 * the ring through a chunk loop of its own, which is the same defect the canned
 * sounds have; and its only caller is `tests/audio_touch_test`, so there is no
 * shipped game whose sound would change under it.  audio_stream_start() therefore
 * enters continuous mode itself if it is not already on, and the two write
 * policies that existed only for this path are gone with it.
 */

/**
 * Begin streaming — takes over the fill callback with one gliding oscillator.
 *
 * ⚠️ Refused LOUDLY if the mix bus still has voices pending: a swap that went
 * quiet would leave those voices enqueued into a mixer nobody renders.
 */
void audio_stream_start(Audio *audio, int freq_hz);

/**
 * Service the stream — call every frame while the user is touching.
 * Non-blocking; equivalent to audio_pump() with the oscillator installed.
 */
void audio_stream_chunk(Audio *audio);

/**
 * Update the target frequency for smooth glissando.
 * Call whenever touch position changes.
 */
void audio_stream_set_freq(Audio *audio, int freq_hz);

/**
 * Stop streaming — removes the oscillator, then APPENDS its 20 ms fade-out so
 * the tone has a release rather than a cut.
 *
 * ⚠️ **The release lands one lead behind the finger** (~139 ms on the OSS shim),
 * because a continuous stream cannot un-write what is already queued and the
 * whole point is not to reset the ring.  That is a property of the design, not a
 * bug to fix here; the alternative is the click.
 */
void audio_stream_stop(Audio *audio);

#endif /* AUDIO_H */
