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

typedef struct {
    int      dsp_fd;          /**< /dev/dsp file descriptor (-1 = not open)      */
    int      sample_rate;     /**< Negotiated sample rate (typically 44100)        */
    bool     available;       /**< false if /dev/dsp could not be opened           */
    uint32_t sound_end_ms;    /**< Expected wall-clock end of current tone (ms)    */
} Audio;

/**
 * Initialise audio subsystem.
 *  - Drives GPIO12 HIGH (enables on-board speaker amplifier)
 *  - Opens /dev/dsp and configures 44100 Hz, stereo, S16LE
 * Returns 0 on success, -1 if hardware unavailable (game may continue).
 */
int  audio_init(Audio *audio);

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
 */
void audio_interrupt(Audio *audio);

/**
 * Play a blocking sine-wave tone through SPKR1.
 * Returns immediately when playback finishes.
 *
 * @param freq_hz     Frequency 20–8000 Hz
 * @param duration_ms Duration in milliseconds
 */
void audio_tone(Audio *audio, int freq_hz, int duration_ms);

/* ── Convenience sounds ────────────────────────────────────────────────────
 * Each call blocks for the sound's duration.  Layer calls for chord effects.
 */

/** Short 880 Hz blip (~80 ms)  — UI click, tile place, button press */
void audio_beep(Audio *audio);

/** Short 1320 Hz blip (~60 ms) — item collected, food eaten           */
void audio_blip(Audio *audio);

/** C5→E5→G5 ascending arpeggio (~440 ms) — score milestone, level up */
void audio_success(Audio *audio);

/** G4→E4→C4 descending tone  (~600 ms)   — game over, error          */
void audio_fail(Audio *audio);

#endif /* AUDIO_H */
