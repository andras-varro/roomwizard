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

// Define forbidden symbol exceptions BEFORE including any headers
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_stdout
#define FORBIDDEN_SYMBOL_EXCEPTION_stderr
#define FORBIDDEN_SYMBOL_EXCEPTION_fputs
#define FORBIDDEN_SYMBOL_EXCEPTION_exit
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h
#define FORBIDDEN_SYMBOL_EXCEPTION_unistd_h
#define FORBIDDEN_SYMBOL_EXCEPTION_write

#include "backends/platform/roomwizard/roomwizard.h"
#include "backends/platform/roomwizard/roomwizard-graphics.h"
#include "backends/platform/roomwizard/roomwizard-events.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "backends/events/default/default-events.h"
#include "backends/mixer/null/null-mixer.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "base/main.h"
#include "common/scummsys.h"
#include "common/config-manager.h"

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

OSystem_RoomWizard::OSystem_RoomWizard()
	: _eventSource(nullptr) {
	
	// Set up filesystem factory
	_fsFactory = new POSIXFilesystemFactory();
	
	// Initialize start time
	gettimeofday(&_startTime, 0);
}

OSystem_RoomWizard::~OSystem_RoomWizard() {
	delete _eventSource;
}

void OSystem_RoomWizard::initBackend() {
	// Create graphics manager
	_graphicsManager = new RoomWizardGraphicsManager();
	
	// Create event source
	_eventSource = new RoomWizardEventSource();
	
	// Create event manager
	_eventManager = new DefaultEventManager(_eventSource);
	
	// Create timer manager
	_timerManager = new DefaultTimerManager();
	
	// Create save file manager
	_savefileManager = new DefaultSaveFileManager();
	
	// Create mixer manager (null - no audio for now)
	_mixerManager = new NullMixerManager();
	_mixerManager->init();
	
	// Call parent init
	ModularGraphicsBackend::initBackend();
	
	debug("RoomWizard backend initialized");
}

Common::MutexInternal *OSystem_RoomWizard::createMutex() {
	return new NullMutexInternal();
}

uint32 OSystem_RoomWizard::getMillis(bool skipRecord) {
	timeval curTime;
	gettimeofday(&curTime, 0);
	
	return (uint32)(((curTime.tv_sec - _startTime.tv_sec) * 1000) +
	                ((curTime.tv_usec - _startTime.tv_usec) / 1000));
}

void OSystem_RoomWizard::delayMillis(uint msecs) {
	usleep(msecs * 1000);
}

void OSystem_RoomWizard::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	time_t curTime = time(0);
	struct tm t = *localtime(&curTime);
	td.tm_sec = t.tm_sec;
	td.tm_min = t.tm_min;
	td.tm_hour = t.tm_hour;
	td.tm_mday = t.tm_mday;
	td.tm_mon = t.tm_mon;
	td.tm_year = t.tm_year;
	td.tm_wday = t.tm_wday;
}

void OSystem_RoomWizard::quit() {
	exit(0);
}

void OSystem_RoomWizard::logMessage(LogMessageType::Type type, const char *message) {
	FILE *output = (type == LogMessageType::kInfo || type == LogMessageType::kDebug) ? stdout : stderr;
	fputs(message, output);
	fflush(output);
}

void OSystem_RoomWizard::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	// Add any system-specific archive paths here if needed
}

// Factory function
OSystem *OSystem_RoomWizard_create() {
	return new OSystem_RoomWizard();
}

// Main entry point
int main(int argc, char *argv[]) {
	// Early debug - write directly to avoid any C++ issues
	const char *msg = "RoomWizard: main() started\n";
	write(2, msg, 28);
	
	// Create the backend
	g_system = OSystem_RoomWizard_create();
	assert(g_system);
	
	write(2, "RoomWizard: backend created\n", 29);
	
	// Invoke ScummVM main
	int res = scummvm_main(argc, argv);
	
	// Cleanup
	g_system->destroy();
	
	return res;
}
