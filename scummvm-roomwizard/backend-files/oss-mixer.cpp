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
#include <sys/ioctl.h>
#include <sys/soundcard.h>

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
	, _outputRate(44100)
	, _samples(4096)
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
	// Open non-blocking so write() returns EAGAIN instead of stalling for the
	// TWL4030's ~506 ms ALSA hardware period (see SYSTEM_ANALYSIS.md).
	_fd = open("/dev/dsp", O_WRONLY | O_NONBLOCK);
	if (_fd < 0) {
		// No DSP device — fall back to silent mixer so ScummVM still works.
		warning("OssMixerManager: cannot open /dev/dsp (%d), audio disabled", _fd);
		_mixer = new Audio::MixerImpl(_outputRate, true, _samples);
		_mixer->setReady(true);
		return;
	}

	// ---------------------------------------------------------------
	// Fragment sizing MUST be the very first ioctl on this device.
	// The ALSA OSS compat shim silently ignores SNDCTL_DSP_SETFRAGMENT
	// once the stream is configured (which happens implicitly on the
	// first format/rate ioctl).  Without this, the kernel ring defaults
	// to ~64 KB and write() never blocks — causing a CPU-spinning audio
	// thread that starves the main loop.
	//
	// _samples = 4096 frames * 4 bytes = 16384 bytes per mix buffer.
	// 2 fragments * 2^14 bytes = 32768 bytes ring (2 x mix buffer).
	// In steady state, every other write() blocks for ~93 ms, giving
	// the audio thread a long sleep and leaving the CPU to the game.
	int frag = (2 << 16) | 14;   // 2 fragments of 16384 bytes each
	ioctl(_fd, SNDCTL_DSP_SETFRAGMENT, &frag);

	// 16-bit signed little-endian
	int fmt = AFMT_S16_LE;
	ioctl(_fd, SNDCTL_DSP_SETFMT, &fmt);

	// Stereo
	int stereo = 1;
	ioctl(_fd, SNDCTL_DSP_STEREO, &stereo);

	// 44100 Hz — the ALSA OSS shim will SRC to the hw rate (48000) if needed.
	int rate = 44100;
	ioctl(_fd, SNDCTL_DSP_SPEED, &rate);
	_outputRate = (uint32)rate;  // driver may return a slightly adjusted rate

	_mixer = new Audio::MixerImpl(_outputRate, true, _samples);
	_mixer->setReady(true);

	_threadRunning = true;
	pthread_create(&_thread, nullptr, ossAudioThreadShim, this);

	debug("OssMixerManager: /dev/dsp ready at %u Hz, %u frames/buf", _outputRate, _samples);
}

void OssMixerManager::audioThread() {
	// 2 channels * 2 bytes per sample
	const int bufBytes = (int)(_samples * 4);
	uint8 *buf = new uint8[bufBytes];

	while (_threadRunning) {
		if (_audioSuspended) {
			usleep(50000);
			continue;
		}

		_mixer->mixCallback(buf, _samples);

		// Non-blocking write loop: copy data to the OSS software ring a chunk
		// at a time, sleeping 5 ms on EAGAIN (ring full).  This decouples our
		// write cadence from the underlying ALSA HW period (~506 ms on the
		// TWL4030), which would otherwise block write() for that duration and
		// cause 321 ms gaps of silence between each 185 ms burst of audio.
		// The OSS software ring drains continuously at the hardware sample rate
		// regardless of the ALSA period size.
		int written = 0;
		while (written < bufBytes && _threadRunning) {
			int r = (int)write(_fd, buf + written, bufBytes - written);
			if (r > 0) {
				written += r;
			} else if (r < 0 && errno == EAGAIN) {
				// Ring is full — sleep 5 ms and retry.  One 4096-frame
				// fragment drains in ~93 ms, so we'll spin here at most
				// ~18 times before space appears.
				usleep(5000);
			} else {
				// Hard error — back off briefly to avoid a spin.
				usleep(10000);
				break;
			}
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
