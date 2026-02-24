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
#include <sched.h>
#include <errno.h>
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
		_mixer = new Audio::MixerImpl(_outputRate, true, _samples);
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

	// 16-bit signed little-endian
	int fmt = AFMT_S16_LE;
	ioctl(_fd, SNDCTL_DSP_SETFMT, &fmt);

	// Stereo
	int stereo = 1;
	ioctl(_fd, SNDCTL_DSP_STEREO, &stereo);

	// 22050 Hz — avoids expensive non-integer SRC from 44100→48000 in the
	// ALSA OSS shim, halves OPL synthesis workload, and suits SCUMM-era content.
	int rate = 22050;
	ioctl(_fd, SNDCTL_DSP_SPEED, &rate);
	_outputRate = (uint32)rate;

	_mixer = new Audio::MixerImpl(_outputRate, true, _samples);
	_mixer->setReady(true);

	_threadRunning = true;
	pthread_create(&_thread, nullptr, ossAudioThreadShim, this);

	// Elevate the audio thread to soft real-time so it preempts the main
	// game thread the instant its usleep() expires.  Without this, on a
	// loaded 300 MHz ARM the main thread (75% CPU) starves the audio thread
	// for 20-40 ms past its wakeup deadline → ring underrun → sha-sha stutter.
	// SCHED_RR at priority 10 is well below kernel IRQ handlers but above
	// all SCHED_OTHER tasks.  Requires root (ScummVM runs as root here).
	struct sched_param sp;
	sp.sched_priority = 10;
	if (pthread_setschedparam(_thread, SCHED_RR, &sp) != 0)
		warning("OssMixerManager: could not set SCHED_RR (not running as root?)");

	debug("OssMixerManager: /dev/dsp ready at %u Hz, %u frames/buf", _outputRate, _samples);
}

void OssMixerManager::audioThread() {
	// 2 channels * 2 bytes per sample
	const int bufBytes = (int)(_samples * 4);
	// How long one mix buffer represents in microseconds.
	const long usPerBuf = (long)(1000000LL * _samples / _outputRate); // ~93 ms

	uint8 *buf = new uint8[bufBytes];

	// Wall-clock deadline: we advance this by usPerBuf after each write.
	// The audio thread sleeps until the deadline, ensuring the hardware
	// always has exactly one buffer worth of data queued regardless of
	// how long mixCallback or write() took.
	struct timeval deadline;
	gettimeofday(&deadline, nullptr);

	while (_threadRunning) {
		if (_audioSuspended) {
			usleep(50000);
			gettimeofday(&deadline, nullptr); // reset after resume
			continue;
		}

		_mixer->mixCallback(buf, _samples);

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
				usleep(2000); // ring unexpectedly full — wait 2 ms & retry
			} else {
				usleep(10000);
				break;
			}
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
			// Fell behind schedule (CPU overloaded).
			// Reset deadline to now so we don’t try to catch up.
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
