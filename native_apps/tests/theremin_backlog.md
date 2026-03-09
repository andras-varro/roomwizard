# Theremin Streaming Audio - Backlog

## Goal
Transform the tap-a-theremin (`native_apps/tests/audio_touch_test.c`) from a tap-to-play-tone app into a continuous theremin where:
- Sound plays as long as the user touches the screen
- Pitch changes seamlessly as the user drags/swipes
- Sound stops when the user lifts their finger

## What Was Done

### Files Modified
1. **`native_apps/common/audio.h`** — Added streaming fields to `Audio` struct (`phase`, `current_freq`, `target_freq`, `amplitude`, `streaming`, `logger`) and 4 new function declarations (`audio_stream_start`, `audio_stream_chunk`, `audio_stream_set_freq`, `audio_stream_stop`)
2. **`native_apps/common/audio.c`** — Implemented the 4 streaming functions. Uses `configure_dsp()` (standard config, not custom fragments). Added freeze safeguards (max retry counts, VLA size clamps).
3. **`native_apps/tests/audio_touch_test.c`** — Completely rewritten as continuous theremin. Uses logger API. Touch-down starts stream, touch-move updates freq, touch-up stops stream.
4. **`native_apps/build-and-deploy.sh`** — Added `common/logger.c` to the audio_touch_test compile command

### Architecture
- **Streaming model**: `audio_stream_start()` resets DSP and initializes state → main loop calls `audio_stream_chunk()` each frame to write ~10ms of audio → `audio_stream_set_freq()` updates target frequency → `audio_stream_stop()` writes fade-out and resets DSP
- **Phase accumulator** persists across chunks in `Audio.phase` field — prevents clicks at chunk boundaries
- **Frequency interpolation**: per-sample smoothing via `FREQ_SMOOTH = 0.002` factor — `current_freq` moves toward `target_freq` each sample
- **Amplitude ramping**: `AMP_RAMP_SPEED = 0.0005` for fade-in; explicit 20ms linear fade-out in `audio_stream_stop()`
- **Non-blocking writes**: `audio_stream_chunk()` uses `O_NONBLOCK` write, breaks on EAGAIN to keep main loop responsive
- **Freeze safeguards**: All write loops have max retry counts, VLA sizes clamped to `MAX_CHUNK_FRAMES = 4410`

## Current Status: NOT WORKING

### Symptoms
1. **No sound produced** — the app displays correctly (title, rainbow pad, exit button), touch is detected (visual indicator follows finger), but no audible tone is heard
2. **Click on startup** — a single click is heard, likely from the GPIO12 amp enable in `audio_init()`
3. **One freeze incident** — caused complete panel freeze requiring hardware watchdog reset (freeze safeguards were added afterward, untested)
4. Dragging visual works but since no sound plays, can't verify pitch changes

### Attempted Fixes (didn't resolve)
1. Removed `SNDCTL_DSP_SETFRAGMENT` (thought the custom fragment config was putting DSP in bad state) — reverted to standard `configure_dsp()`
2. Added freeze safeguards (max retries, VLA clamps)
3. Added diagnostic logging via logger API

## Root Cause Hypotheses (Not Yet Tested)

### Hypothesis 1: SNDCTL_DSP_RESET breaks streaming
The `audio_stream_start()` calls `SNDCTL_DSP_RESET` before starting. On the TWL4030/ALSA OSS shim (Linux 4.14.52), this ioctl has a documented ~50ms DAC pipeline startup latency. The existing `audio_tone()` works because it pre-fills a large buffer that covers this startup delay. The streaming chunks (~10ms each) may be too small — the first several chunks may be silently discarded during DAC startup.

**Test**: Add a dummy silence buffer write (~100ms) immediately after `SNDCTL_DSP_RESET` + `configure_dsp()` in `audio_stream_start()` to prime the DAC pipeline before real audio streaming begins.

### Hypothesis 2: Non-blocking write always gets EAGAIN
If the OSS ring buffer is in a state where it never accepts data (e.g., after a problematic RESET), every `write()` returns EAGAIN and `audio_stream_chunk()` breaks immediately without writing anything. Check stderr logs — if we see only EAGAIN messages and no "wrote X bytes", this is the cause.

**Test**: SSH into the device, run the app manually from terminal, watch stderr output for the diagnostic messages already added.

### Hypothesis 3: The write succeeds but audio route is broken
The `SNDCTL_DSP_RESET` + `configure_dsp()` reconfigure may work for writes but leave the ALSA mixer route (HandsfreeL/R → SPKR1) disconnected. The existing `audio_tone()` works because it's called without a RESET (via `audio_interrupt` which does RESET + configure together).

**Test**: Try removing the `SNDCTL_DSP_RESET` call from `audio_stream_start()` entirely. Just call `configure_dsp()` on the already-open fd. The first `audio_stream_start()` after `audio_init()` should work because init already configured things.

### Hypothesis 4: Timing issue — chunks too fast
With 1ms sleep in the main loop, `audio_stream_chunk()` is called ~1000 times/sec, each generating 10ms of audio (10x real-time). The OSS ring buffer fills up immediately, and subsequent writes all get EAGAIN. This IS the expected behavior (ring fills, then write paces to real-time). But if the ring buffer configuration is wrong, it might never start draining.

**Test**: Increase main loop sleep to 10ms (matching chunk size) so audio generation matches real-time 1:1.

### Hypothesis 5: The known-working audio_tone() approach could be adapted
Instead of the full streaming rewrite, a simpler approach might work: use the existing `audio_tone()` but with very short durations (30-50ms) in a non-blocking thread, or just accept the ~30ms granularity for pitch updates. The `audio_interrupt()` + `audio_tone()` pattern already works on this hardware.

**Test**: Revert to the original approach but with shorter tone durations and the swipe detection from the first attempt.

## Debugging Steps for Next Session

### Step 1: Get diagnostic output
SSH to device, run manually:
```bash
/opt/games/audio_touch_test /dev/fb0 /dev/input/touchscreen0
```
Watch stderr for streaming diagnostic messages. Key things to look for:
- "audio: stream start at XXX Hz" — confirms start was called
- "audio: stream chunk EAGAIN" — ring buffer full (expected occasionally, but NOT every call)
- "audio: stream chunk wrote XXX bytes" — confirms data actually reached the driver
- "audio: stream stop" — confirms stop was called

### Step 2: Compare with working audio_tone()
The existing `audio_test.c` plays tones that definitely work. Compare:
- Does `audio_test` also call `SNDCTL_DSP_RESET` before tones? (Yes, via `audio_flush()`)
- What's different? `audio_tone()` writes a LARGE buffer (60ms+ = 10584+ bytes) in a blocking loop. The streaming writes tiny 10ms chunks (~1764 bytes) non-blocking.

### Step 3: Try the simple fix first
In `audio_stream_start()`, after RESET + configure, write a priming buffer of silence (~50-100ms worth of zeros) using a blocking write loop (like audio_tone does). This primes the DAC pipeline. Then start streaming real audio chunks.

### Step 4: If still broken, try without RESET
Remove `SNDCTL_DSP_RESET` from `audio_stream_start()`. The DSP was already configured by `audio_init()`. Just initialize the streaming state and start writing chunks immediately.

### Step 5: If still broken, fall back to short-tone approach
Abandon the streaming architecture. Instead:
- Use a tight loop: `audio_interrupt()` + `audio_tone(freq, 40)` with 40ms tones
- Accept 40ms pitch update granularity
- Don't try O_NONBLOCK streaming — use the known-working blocking pattern
- This was the original "swipe" approach which didn't work because the main loop blocked during tones. But it could work if we accept the display freeze during tones (or use a separate thread).

## Key Files Reference
- `native_apps/common/audio.h` — Audio struct + API declarations
- `native_apps/common/audio.c` — Audio implementation (streaming functions at bottom)
- `native_apps/tests/audio_touch_test.c` — Theremin app
- `native_apps/common/logger.h` / `logger.c` — Logging API used by theremin
- `native_apps/build-and-deploy.sh` — Build script (line ~79 for audio_touch_test)
- `native_apps/tests/audio_test.c` — Working reference for audio_tone() usage

## Hardware Context
- RoomWizard panel: OMAP3530 ARM9, Linux 4.14.52
- Audio: TWL4030 HiFi DAC → HandsfreeL/R class-D amp → SPKR1
- OSS interface: `/dev/dsp` via ALSA OSS compatibility layer
- Amp enable: GPIO12 sysfs (active HIGH)
- Touchscreen: resistive, `/dev/input/touchscreen0`
- Display: 800×480 framebuffer `/dev/fb0`
- Hardware watchdog: will reset if app freezes the system

## Bug Fix: Touch/Swipe Not Tracked After Initial Touch (2026-03-09)

### Bug
After the first touch on the theremin pad, swiping did not work — the touch indicator showed the original touch point briefly and did not follow swipe movement. Audio played only for a single frame then stopped.

### Root Cause
The main theremin logic condition on line 292 used `s.pressed`, which is a **transient one-frame flag** — it is only `true` on the single frame when `BTN_TOUCH` fires. During a drag/swipe, only `ABS_X`/`ABS_Y` events arrive (no repeated `BTN_TOUCH`), so `pressed` resets to `false` on every subsequent frame. This caused the theremin to immediately stop audio and hide the indicator after the first frame of a touch.

### Fix
One-line change in `native_apps/tests/audio_touch_test.c`:
```c
// Before (broken):
if (s.pressed && in_pad(s.x, s.y)) {

// After (fixed):
if (s.held && in_pad(s.x, s.y)) {
```

`s.held` is a **persistent flag** that stays `true` from touch-down through the entire drag until release, which is the correct semantics for a continuous theremin instrument. The press frame is also covered because `touch_poll()` sets both `held` and `pressed` simultaneously on touch-down.

### Reference
The brick breaker game (`native_apps/brick_breaker/brick_breaker.c`, line 1475) already uses the correct pattern: `st.held || st.pressed` for continuous touch tracking during paddle movement.

## Bug Fix: Audio Stuttering — "tu-tu-tu" Instead of Continuous Tone (2026-03-09)

### Bug
When touching the theremin pad, the speaker produces a stuttering "tu-tu-tu-tu" pattern instead of a smooth continuous tone. The pitch changes work (different stutter pitch when dragging) but the sound is never continuous.

### Root Cause
**Audio underrun (DAC starvation).** The main loop takes ~30ms per iteration (dominated by framebuffer rendering) but `audio_stream_chunk()` only generated 10ms of audio per call (`STREAM_CHUNK_MS = 10`). The DAC consumes audio faster than the app produces it, causing repeated buffer underruns that manifest as the "tu-tu-tu" stuttering pattern. Additionally, the TWL4030 DAC has ~50ms startup latency after `SNDCTL_DSP_RESET`, so the first several chunks were silently discarded.

### Fixes Applied (3 changes to `native_apps/common/audio.c`)

**Fix A — Increase chunk size (primary fix):**
- Changed `STREAM_CHUNK_MS` from 10 to 80
- Changed `MAX_CHUNK_FRAMES` from 4410 to 8820 (headroom for 200ms at 44100 Hz)
- Each call to `audio_stream_chunk()` now generates 80ms of audio, so even with a 30–50ms render loop, the DAC buffer stays fed (production rate ~80ms/30ms ≈ 2.7× real-time)

**Fix B — Pre-fill buffer at stream start (secondary fix):**
- Added a pre-fill loop in `audio_stream_start()` after `audio->streaming = true`
- Generates ~200ms of audio using the same sine wave / frequency smoothing / amplitude ramping logic as `audio_stream_chunk()`
- Writes in blocking mode (retries on EAGAIN with `usleep(1000)`) to ensure the full prefill reaches the driver
- Primes the OSS ring buffer past the TWL4030 DAC startup latency
- Graceful fallback: only executes if `malloc()` succeeds

**Fix C — Phase save/restore on failed writes (polish fix):**
- In `audio_stream_chunk()`, saves `phase`, `current_freq`, and `amplitude` before sample generation
- After the write loop, if `written == 0` (buffer was full, nothing accepted), restores the saved values
- Ensures the next call regenerates the same data with correct phase continuity, preventing clicks/pops after a full-buffer event

### Expected Result
- Continuous smooth tone when touching the theremin pad
- Responsive pitch changes when dragging across the pad
- No audible gaps, stuttering, or clicks during sustained touch
- Clean fade-in on touch-down and fade-out on touch-up

### Files Changed
- `native_apps/common/audio.c` — All three fixes (streaming functions only; non-streaming `audio_tone()` API unchanged)

### Safety Note
These changes only affect the `audio_stream_*` functions used by the theremin. The `audio_tone()`, `audio_beep()`, `audio_blip()`, `audio_success()`, `audio_fail()`, and `audio_play_buffer()` functions used by brick_breaker, pong, snake, tetris, etc. are completely unchanged.

## Bug Fix: Clicking/Cracking + Pitch Lag (2026-03-09)

### Bugs
1. **Clicking/cracking after a few seconds** — even when holding still on one spot, audible clicks/cracks appear after a few seconds of continuous play
2. **Pitch response lag** — swiping right then left, the pitch keeps going up before going down; there's a noticeable 100–200ms delay before pitch direction changes

### Root Cause

**Clicking/cracking:** The 80ms chunk size caused the ring buffer to overflow quickly (80ms of audio produced every ~30ms frame = 2.7× real-time). When the buffer was nearly full, partial writes occurred — the `write()` succeeded for only part of the chunk, but the phase accumulator state had already advanced for the FULL chunk. The phase save/restore logic only caught the `written == 0` case (total rejection), not partial writes where some bytes were accepted but the phase had already moved past them. Result: phase discontinuity at the boundary → audible click.

**Pitch lag:** With 80ms chunks, the frequency at the time of generation was "baked in" for the entire 80ms duration. Combined with the very slow `FREQ_SMOOTH = 0.002` (only 0.2% movement toward target per sample), pitch changes lagged 100–200ms behind finger movement. The frequency was essentially decided once at chunk generation time and didn't respond to target changes until the next chunk was generated.

### Fix Applied (3 changes to `native_apps/common/audio.c`)

**Approach change:** Instead of one large 80ms chunk per frame, write **multiple small 10ms chunks** per frame until the ring buffer is sufficiently full. This gives:
- **No underrun**: multiple chunks per frame keeps the buffer fed
- **Low latency**: each 10ms chunk picks up the latest frequency
- **No partial writes**: small chunks fit cleanly when we check space first

**Fix A — Revert chunk size to 10ms:**
- Changed `STREAM_CHUNK_MS` from 80 back to 10
- `MAX_CHUNK_FRAMES` kept at 8820 (harmless headroom)

**Fix B — Multi-write `audio_stream_chunk()` with GETOSPACE:**
- Complete rewrite of `audio_stream_chunk()` using `SNDCTL_DSP_GETOSPACE` ioctl
- Queries available space in the OSS ring buffer before each write
- Generates and writes one 10ms chunk at a time in a loop (up to 8 chunks = 80ms max per call)
- Each chunk picks up the latest `target_freq`, so pitch changes are reflected within 10ms
- Stops when buffer is sufficiently full (no overflow, no partial writes)
- Phase is continuous because each chunk is fully written before generating the next
- **Removed the phase save/restore logic** — no longer needed since we check available space before writing, preventing partial writes entirely

**Fix C — Faster frequency smoothing:**
- Changed `FREQ_SMOOTH` from 0.002 to 0.15
- Frequency now moves 15% toward target each sample — time constant of ~0.15ms at 44100 Hz
- Pitch changes are nearly instantaneous instead of lagging by 100–200ms

### Key Design Details
- `audio_buf_info` struct comes from `<sys/soundcard.h>` (already included)
- `SNDCTL_DSP_GETOSPACE` returns `.bytes` = number of bytes that can be written without blocking
- Cap of 8 chunks per call prevents spending too long in the audio function and stalling the render loop
- Pre-fill in `audio_stream_start()` is unchanged — still needed for DAC startup priming

### Expected Result
- No clicking or cracking during sustained tones
- Pitch responds within ~10ms of finger movement
- Clean continuous tone with no gaps or stuttering
- Pre-fill still handles DAC startup latency

### Files Changed
- `native_apps/common/audio.c` — `STREAM_CHUNK_MS`, `FREQ_SMOOTH`, and `audio_stream_chunk()` (streaming only; non-streaming API unchanged)
