/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// These must come before any ScummVM header (forbidden.h is pulled in transitively).
#define FORBIDDEN_SYMBOL_EXCEPTION_unistd_h
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h
#define FORBIDDEN_SYMBOL_EXCEPTION_write

#include "backends/mixer/oss/oss-mixer.h"
#include "audio/mixer_intern.h"
#include "common/debug.h"

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/time.h>

// ---------------------------------------------------------------------------
// C-linkage thread shim
// ---------------------------------------------------------------------------
static void *ossAudioThreadShim(void *arg) {
	static_cast<OssMixerManager *>(arg)->audioThread();
	return nullptr;
}

// ---------------------------------------------------------------------------
// OssMixerManager
// ---------------------------------------------------------------------------

OssMixerManager::OssMixerManager()
	: MixerManager()
	, _fd(-1)
	, _outputRate(22050)
	, _samples(2048)
	, _threadRunning(false) {
}

OssMixerManager::~OssMixerManager() {
	if (_threadRunning) {
		_threadRunning = false;
		pthread_join(_thread, nullptr);
	}
	if (_fd >= 0) {
		close(_fd);
		_fd = -1;
	}
}

void OssMixerManager::init() {
	// Open non-blocking: blocking write() on this device stalls for the full
	// TWL4030 ALSA HW period (~506 ms) once the OSS ring fills, causing
	// long silent gaps.  O_NONBLOCK avoids that stall entirely.
	_fd = open("/dev/dsp", O_WRONLY | O_NONBLOCK);
	if (_fd < 0) {
		// No DSP device — fall back to silent mixer so ScummVM still works.
		warning("OssMixerManager: cannot open /dev/dsp (%d), audio disabled", _fd);
		_mixer = new Audio::MixerImpl(_outputRate, false, _samples);
		_mixer->setReady(true);
		return;
	}

	// ---------------------------------------------------------------
	// We deliberately do NOT call SNDCTL_DSP_SETFRAGMENT.
	//
	// Constraining the ring to 2×16384 bytes only provided 185 ms of
	// buffer.  On a loaded 300 MHz ARM, kernel scheduling jitter can
	// stretch usleep(5 ms) to 20-30 ms; 18 such sleeps = 360 ms >
	// 185 ms → ring empties → fragment-boundary underrun → Te-te-CLICK.
	//
	// With the default unconstrained ring (~500 ms) there is enough
	// headroom to absorb any realistic scheduling jitter.  We pace
	// writes with a wall-clock deadline instead of relying on write()
	// or EAGAIN blocking behaviour.

	// ---------------------------------------------------------------
	// ALSA OSS shim ioctl bugs (Linux 4.14.52, TWL4030):
	//  - SNDCTL_DSP_STEREO is silently ignored
	//  - SNDCTL_DSP_SPEED may reset format and/or channels
	//  - SNDCTL_DSP_SETFMT may reset speed
	//
	// The ioctl output values (written back to the variable) may also
	// not reflect the ACTUAL device state.  Trust only SOUND_PCM_READ_*
	// read-back ioctls for the final configuration.
	//
	// Strategy: set all three params, then read back the actual state.
	// Use the read-back rate for _outputRate so OPL sample-counting
	// matches the real playback rate.
	// ---------------------------------------------------------------

	int val;

	// Set speed FIRST (most likely to reset other params)
	val = 22050;
	ioctl(_fd, SNDCTL_DSP_SPEED, &val);

	// Set format AFTER speed
	val = AFMT_S16_LE;
	ioctl(_fd, SNDCTL_DSP_SETFMT, &val);

	// Set mono LAST
	val = 1;
	ioctl(_fd, SNDCTL_DSP_CHANNELS, &val);

	// Read back the ACTUAL device state with read-only ioctls.
	// These are more reliable than the set-ioctl output values.
	int actualRate = 0, actualBits = 0, actualChannels = 0;
	ioctl(_fd, SOUND_PCM_READ_RATE, &actualRate);
	ioctl(_fd, SOUND_PCM_READ_BITS, &actualBits);
	ioctl(_fd, SOUND_PCM_READ_CHANNELS, &actualChannels);

	debug("OssMixerManager: read-back: rate=%d bits=%d channels=%d",
	      actualRate, actualBits, actualChannels);

	// Use the read-back rate if sane; fall back to requested rate.
	// This is critical: if _outputRate doesn't match the real playback
	// rate, OPL sample-counting produces music at the wrong tempo.
	if (actualRate > 0 && actualRate <= 96000) {
		_outputRate = (uint32)actualRate;
	} else {
		_outputRate = 22050;
		debug("OssMixerManager: SOUND_PCM_READ_RATE returned %d, using 22050", actualRate);
	}

	if (actualBits != 16) {
		warning("OssMixerManager: device is %d-bit, expected 16-bit!", actualBits);
	}
	if (actualChannels != 1) {
		warning("OssMixerManager: device has %d channels, expected 1!", actualChannels);
	}

	debug("OssMixerManager: using rate=%u Hz, mono S16LE", _outputRate);

	_mixer = new Audio::MixerImpl(_outputRate, false, _samples);
	_mixer->setReady(true);

	_threadRunning = true;
	pthread_create(&_thread, nullptr, ossAudioThreadShim, this);

	// NOTE: SCHED_RR was previously used here to prevent audio-thread
	// starvation on the loaded 300 MHz ARM.  However, with the correct
	// mixCallback byte count (bufBytes) the audio thread now does 4x more
	// mixing work per cycle.  On single-core ARM, SCHED_RR causes the
	// RT-priority audio thread to starve the main thread during init,
	// producing a black screen.  The ~500 ms OSS ring buffer easily
	// absorbs 20-40 ms of SCHED_OTHER scheduling jitter, so RT priority
	// is unnecessary.

	debug("OssMixerManager: /dev/dsp ready at %u Hz mono, %u frames/buf", _outputRate, _samples);
}

void OssMixerManager::audioThread() {
	// 1 channel * 2 bytes per sample (mono)
	const int bufBytes = (int)(_samples * 2);
	// How long one mix buffer represents in microseconds.
	const long usPerBuf = (long)(1000000LL * _samples / _outputRate); // ~93 ms

	uint8 *buf = new uint8[bufBytes];

	// ── Query ring-buffer capacity ──────────────────────────────
	// SNDCTL_DSP_GETOSPACE tells us how much space the driver has.
	// At init (ring empty) the total fragstotal*fragsize = ring capacity.
	int ringBytes = 0;
	{
		audio_buf_info abi;
		if (ioctl(_fd, SNDCTL_DSP_GETOSPACE, &abi) == 0) {
			ringBytes = abi.fragstotal * abi.fragsize;
		debug("OssMixerManager: ring buffer: %d fragments x %d bytes = %d bytes (%.0f ms)",
		      abi.fragstotal, abi.fragsize, ringBytes,
		      1000.0 * ringBytes / (2.0 * _outputRate));
		} else {
			debug("OssMixerManager: GETOSPACE failed, cannot query ring size");
		}
	}

	// ── Pre-fill the ring with silence ──────────────────────────
	// Writing several silent buffers before starting the pacing loop
	// gives the ring ~200+ ms of headroom.  Without this, the first
	// scheduling hiccup can drain the ring to zero → ALSA XRUN →
	// audible breakup.
	{
		memset(buf, 0, bufBytes);
		int prefillBufs = 3; // 3 × 93 ms ≈ 280 ms of silence
		for (int i = 0; i < prefillBufs; i++) {
			int r = (int)write(_fd, buf, bufBytes);
			if (r <= 0) break;
		}
		debug("OssMixerManager: pre-filled %d silence buffers (%d ms)",
		      prefillBufs, (int)(prefillBufs * usPerBuf / 1000));
	}

	// ── Diagnostic counters ─────────────────────────────────────
	uint32 totalBufs     = 0;
	uint32 eagainCount   = 0;
	uint32 writeErrCount = 0;
	uint32 deadlineResets = 0;
	uint32 xrunDetected  = 0;
	struct timeval lastReport;
	gettimeofday(&lastReport, nullptr);

	// Wall-clock deadline: we advance this by usPerBuf after each write.
	struct timeval deadline;
	gettimeofday(&deadline, nullptr);

	while (_threadRunning) {
		if (_audioSuspended) {
			usleep(50000);
			gettimeofday(&deadline, nullptr); // reset after resume
			continue;
		}

		_mixer->mixCallback(buf, bufBytes);

		// Attenuate to ~50% volume to avoid distorting the small
		// RoomWizard speaker.  Arithmetic right-shift on signed int16
		// is exact −6 dB with no clipping risk.
		{
			int16 *s = (int16 *)buf;
			int n = bufBytes / 2;
			for (int i = 0; i < n; ++i)
				s[i] >>= 1;
		}

		// Write the buffer.  With O_NONBLOCK and the unconstrained ring
		// (~500 ms), write() almost always succeeds in 1-2 calls.
		// The EAGAIN path is a safety net for the rare case we somehow
		// overfill the ring (e.g. after resume).
		int written = 0;
		while (written < bufBytes && _threadRunning) {
			int r = (int)write(_fd, buf + written, bufBytes - written);
			if (r > 0) {
				written += r;
			} else if (r < 0 && errno == EAGAIN) {
				eagainCount++;
				usleep(2000); // ring full — wait 2 ms & retry
			} else {
				// write() returned 0 or a non-EAGAIN error.
				// Log it — silent drops cause audio breakup.
				writeErrCount++;
				if (writeErrCount <= 10 || (writeErrCount % 100) == 0) {
					warning("OssMixerManager: write() returned %d, errno=%d (%s), wrote %d/%d",
					        r, errno, strerror(errno), written, bufBytes);
				}
				usleep(10000);
				break;
			}
		}
		totalBufs++;

		// ── Check ring fill level & detect XRUN ─────────────────
		// If the ring is completely empty after we just wrote, it means
		// the hardware drained faster than we filled → XRUN territory.
		{
			audio_buf_info abi;
			if (ringBytes > 0 && ioctl(_fd, SNDCTL_DSP_GETOSPACE, &abi) == 0) {
				int freeBytes = abi.fragments * abi.fragsize;
				int filledBytes = ringBytes - freeBytes;
				// If less than one buffer worth of audio is queued,
				// we're dangerously close to underrun.
				if (filledBytes < bufBytes) {
					xrunDetected++;
					if (xrunDetected <= 5 || (xrunDetected % 50) == 0) {
						warning("OssMixerManager: ring near-empty! filled=%d/%d bytes (%.0f ms), xruns=%u",
						        filledBytes, ringBytes,
						        1000.0 * filledBytes / (2.0 * _outputRate),
						        xrunDetected);
					}
					// Emergency: write an extra buffer to prevent XRUN
					_mixer->mixCallback(buf, bufBytes);
					{
						int16 *s = (int16 *)buf;
						int n = bufBytes / 2;
						for (int i = 0; i < n; ++i)
							s[i] >>= 1;
					}
					write(_fd, buf, bufBytes);
				}
			}
		}

		// ── Periodic diagnostic report (~10 seconds) ────────────
		if ((totalBufs % 107) == 0) { // 107 × 93 ms ≈ 10 s
			struct timeval now;
			gettimeofday(&now, nullptr);
			int elapsed = (int)(now.tv_sec - lastReport.tv_sec);

			int filledBytes = 0;
			audio_buf_info abi;
			if (ringBytes > 0 && ioctl(_fd, SNDCTL_DSP_GETOSPACE, &abi) == 0) {
				filledBytes = ringBytes - (abi.fragments * abi.fragsize);
			}

			debug("OssMixerManager: [%ds] bufs=%u ring=%d/%d bytes eagain=%u err=%u reset=%u xrun=%u",
			      elapsed, totalBufs, filledBytes, ringBytes,
			      eagainCount, writeErrCount, deadlineResets, xrunDetected);
			lastReport = now;
		}

		// Advance deadline by exactly one buffer period.
		deadline.tv_usec += usPerBuf;
		if (deadline.tv_usec >= 1000000L) {
			deadline.tv_sec  += deadline.tv_usec / 1000000L;
			deadline.tv_usec  = deadline.tv_usec % 1000000L;
		}

		// Sleep until deadline.  This is the primary pacing mechanism:
		// the audio thread is idle for ~90 ms out of every 93 ms.
		struct timeval now;
		gettimeofday(&now, nullptr);
		long sleepUs = (long)(deadline.tv_sec  - now.tv_sec)  * 1000000L
		            + (long)(deadline.tv_usec - now.tv_usec);
		if (sleepUs > 500L && sleepUs < (usPerBuf * 4L)) {
			usleep((useconds_t)sleepUs);
		} else if (sleepUs <= 0L) {
			// Fell behind schedule — reset deadline to now.
			deadlineResets++;
			gettimeofday(&deadline, nullptr);
		}
	}

	delete[] buf;
}

void OssMixerManager::suspendAudio() {
	_audioSuspended = true;
}

int OssMixerManager::resumeAudio() {
	if (!_audioSuspended)
		return -2;
	_audioSuspended = false;
	return 0;
}
