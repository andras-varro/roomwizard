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

#ifndef BACKENDS_MIXER_OSS_H
#define BACKENDS_MIXER_OSS_H

#include "backends/mixer/mixer.h"
#include <pthread.h>

/**
 * Audio mixer that outputs to /dev/dsp (OSS / ALSA OSS-compat layer).
 * Runs a dedicated audio thread that pulls mixed samples and writes them
 * to the device in real time.  Used by the RoomWizard backend which has
 * a TWL4030 codec accessible via the ALSA OSS compatibility shim.
 */
class OssMixerManager : public MixerManager {
public:
	OssMixerManager();
	~OssMixerManager() override;

	void init() override;
	void suspendAudio() override;
	int  resumeAudio() override;

	/** Audio thread entry-point (public so the C shim can call it). */
	void audioThread();

private:
	int      _fd;           ///< File descriptor for /dev/dsp
	uint32   _outputRate;   ///< Actual sample rate accepted by driver
	uint32   _samples;      ///< Frames per mixCallback call
	pthread_t _thread;
	volatile bool _threadRunning;
};

#endif // BACKENDS_MIXER_OSS_H
