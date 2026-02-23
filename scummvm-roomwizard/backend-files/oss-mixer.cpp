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
	, _samples(1024)
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
	_fd = open("/dev/dsp", O_WRONLY);
	if (_fd < 0) {
		// No DSP device — fall back to silent mixer so ScummVM still works.
		warning("OssMixerManager: cannot open /dev/dsp (%d), audio disabled", _fd);
		_mixer = new Audio::MixerImpl(_outputRate, true, _samples);
		_mixer->setReady(true);
		return;
	}

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
			usleep(10000);
			continue;
		}
		_mixer->mixCallback(buf, _samples);

		int written = 0;
		while (written < bufBytes) {
			int r = (int)write(_fd, buf + written, bufBytes - written);
			if (r <= 0) break;
			written += r;
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
