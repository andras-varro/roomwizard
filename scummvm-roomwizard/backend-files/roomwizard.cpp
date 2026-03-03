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
#define FORBIDDEN_SYMBOL_EXCEPTION_getenv
#define FORBIDDEN_SYMBOL_EXCEPTION_fopen
#define FORBIDDEN_SYMBOL_EXCEPTION_fclose
#define FORBIDDEN_SYMBOL_EXCEPTION_fprintf
#define FORBIDDEN_SYMBOL_EXCEPTION_fflush
#define FORBIDDEN_SYMBOL_EXCEPTION_fseek
#define FORBIDDEN_SYMBOL_EXCEPTION_ftell
#define FORBIDDEN_SYMBOL_EXCEPTION_setvbuf
#define FORBIDDEN_SYMBOL_EXCEPTION_mkdir
#define FORBIDDEN_SYMBOL_EXCEPTION_rename

#include "backends/platform/roomwizard/roomwizard.h"
#include "backends/platform/roomwizard/roomwizard-graphics.h"
#include "backends/platform/roomwizard/roomwizard-events.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "backends/events/default/default-events.h"
#include "backends/mixer/oss/oss-mixer.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "common/archive.h"
#include "common/fs.h"
#include "base/main.h"
#include "common/scummsys.h"
#include "common/config-manager.h"
#ifdef ENABLE_VKEYBD
#include "backends/vkeybd/virtual-keyboard.h"
#endif

#include "backends/mutex/null/null-mutex.h"
#include <unistd.h>

// Timer callbacks are pumped cooperatively from delayMillis() and pollEvent()
// via DefaultTimerManager::checkTimers(10).  No background thread is used —
// any real pthread + real mutex causes a deadlock with the SCHED_RR audio
// thread on this single-core ARM during ScummVM init.  NullMutexInternal is
// safe here because all timer callbacks run exclusively on the main thread.

#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

// _logFile is void* in the header to avoid leaking <stdio.h>.
// Cast helper for use in this file.
#define LOG_FP  ((FILE *)_logFile)

// Debug mode: set ROOMWIZARD_DEBUG=1 on the device to enable visual touch
// feedback and verbose touch-state logging.
bool rwDebugMode() {
	static bool checked = false;
	static bool enabled = false;
	if (!checked) {
		checked = true;
		const char *v = getenv("ROOMWIZARD_DEBUG");
		enabled = (v && v[0] != '\0' && v[0] != '0');
		if (enabled)
			debug("RoomWizard: debug mode enabled (ROOMWIZARD_DEBUG=%s)", v);
	}
	return enabled;
}

// Cached pointer — avoids dynamic_cast<OSystem_RoomWizard*>(g_system) on every poll
static OSystem_RoomWizard *s_rwSystem = nullptr;
OSystem_RoomWizard *rwSystem() { return s_rwSystem; }

OSystem_RoomWizard::OSystem_RoomWizard()
	: _eventSource(nullptr)
	, _logFile(nullptr)
	, _logBytes(0)
#ifdef ENABLE_VKEYBD
	, _vkbd(nullptr)
#endif
{
	s_rwSystem = this;

	// Set up filesystem factory
	_fsFactory = new POSIXFilesystemFactory();

	// Initialize start time
	gettimeofday(&_startTime, 0);
}

OSystem_RoomWizard::~OSystem_RoomWizard() {
	if (_logFile) {
		fprintf(LOG_FP, "=== ScummVM shutting down (pid %d) ===\n", (int)getpid());
		fclose(LOG_FP);
		_logFile = nullptr;
	}
	delete _eventSource;
}

void OSystem_RoomWizard::initBackend() {
	// Initialize file logger
	mkdir("/var/log/roomwizard", 0755);
	_logFile = fopen("/var/log/roomwizard/scummvm.log", "a");
	if (_logFile) {
		setvbuf(LOG_FP, nullptr, _IOLBF, 0);  // line-buffered for crash safety
		fseek(LOG_FP, 0, SEEK_END);
		_logBytes = ftell(LOG_FP);
		fprintf(LOG_FP, "=== ScummVM started (pid %d) ===\n", (int)getpid());
	}

	// Create graphics manager
	_graphicsManager = new RoomWizardGraphicsManager();
	
	// Create event source
	_eventSource = new RoomWizardEventSource();
	
	// Create event manager
	_eventManager = new DefaultEventManager(_eventSource);
	
	// Create timer manager — pumped cooperatively from delayMillis/pollEvent.
	_timerManager = new DefaultTimerManager();
	
	// Create save file manager
	_savefileManager = new DefaultSaveFileManager();
	
	// OSS mixer — drives /dev/dsp (ALSA OSS compat, TWL4030 codec)
	_mixerManager = new OssMixerManager();
	_mixerManager->init();
	
	// Register /opt/games as extrapath so vkeybd_small.zip and other data
	// files are discoverable via loadKeyboardPack (which checks extrapath
	// before SearchMan). Also add to SearchMan as a fallback.
	if (!ConfMan.hasKey("extrapath"))
		ConfMan.set("extrapath", "/opt/games");
	{
		Common::FSNode dataDir("/opt/games");
		if (dataDir.isDirectory())
			SearchMan.addDirectory(dataDir.getPath(), dataDir);
	}

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
	// Pump timer callbacks every 10 ms so OPL/MIDI sequencers advance
	// without a background thread (which deadlocks on single-core ARM).
	if (_timerManager)
		static_cast<DefaultTimerManager *>(_timerManager)->checkTimers(10);
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
	// Restore framebuffer to 32bpp before exiting so the app launcher (or any
	// respawned app) finds a clean 32bpp framebuffer.  closeFramebuffer() blanks
	// the screen, unmaps the fb, and calls fb_set_bpp("/dev/fb0", 32).
	if (_graphicsManager)
		((RoomWizardGraphicsManager *)_graphicsManager)->closeFramebuffer();
	exit(0);
}

static const char *logTypeTag(LogMessageType::Type type) {
	switch (type) {
	case LogMessageType::kInfo:    return "INFO ";
	case LogMessageType::kDebug:   return "DEBUG";
	case LogMessageType::kWarning: return "WARN ";
	case LogMessageType::kError:   return "ERROR";
	default:                       return "?????";
	}
}

void OSystem_RoomWizard::logRotate() {
	if (!_logFile || _logBytes < 256 * 1024)
		return;
	fclose(LOG_FP);
	rename("/var/log/roomwizard/scummvm.log",
	       "/var/log/roomwizard/scummvm.log.1");
	_logFile = fopen("/var/log/roomwizard/scummvm.log", "a");
	_logBytes = 0;
	if (_logFile)
		setvbuf(LOG_FP, nullptr, _IOLBF, 0);
}

void OSystem_RoomWizard::logMessage(LogMessageType::Type type, const char *message) {
	if (type == LogMessageType::kDebug && !rwDebugMode())
		return;

	// Write to log file with timestamp
	if (_logFile) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		struct tm tm;
		localtime_r(&ts.tv_sec, &tm);
		char timebuf[32];
		strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

		// Strip trailing newline from message (we add our own)
		size_t len = strlen(message);
		while (len > 0 && (message[len - 1] == '\n' || message[len - 1] == '\r'))
			len--;

		int n = fprintf(LOG_FP, "%s.%03ld [%s] scummvm        %.*s\n",
		                timebuf, ts.tv_nsec / 1000000,
		                logTypeTag(type),
		                (int)len, message);
		if (n > 0)
			_logBytes += n;
		logRotate();
	}

	// Also write to stderr/stdout for interactive use
	FILE *output = (type == LogMessageType::kInfo || type == LogMessageType::kDebug) ? stdout : stderr;
	fputs(message, output);
	fflush(output);
}

void OSystem_RoomWizard::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	// Add any system-specific archive paths here if needed
}

bool OSystem_RoomWizard::hasFeature(Feature f) {
#ifdef ENABLE_VKEYBD
	if (f == kFeatureVirtualKeyboard)
		return true;
#endif
	return ModularGraphicsBackend::hasFeature(f);
}

void OSystem_RoomWizard::setFeatureState(Feature f, bool enable) {
#ifdef ENABLE_VKEYBD
	if (f == kFeatureVirtualKeyboard) {
		if (enable)
			showVirtualKeyboard();
		return;
	}
#endif
	ModularGraphicsBackend::setFeatureState(f, enable);
}

bool OSystem_RoomWizard::getFeatureState(Feature f) {
#ifdef ENABLE_VKEYBD
	if (f == kFeatureVirtualKeyboard)
		return _vkbd && _vkbd->isLoaded();
#endif
	return ModularGraphicsBackend::getFeatureState(f);
}

void OSystem_RoomWizard::showVirtualKeyboard() {
#ifdef ENABLE_VKEYBD
	if (!_vkbd) {
		_vkbd = new Common::VirtualKeyboard();
		// vkeybd_roomwizard is a 2x-scaled (640x480) version of vkeybd_small,
		// sized for the RoomWizard 800x480 display.
		if (!_vkbd->loadKeyboardPack("vkeybd_roomwizard") &&
		    !_vkbd->loadKeyboardPack("vkeybd_small") &&
		    !_vkbd->loadKeyboardPack("vkeybd_default")) {
			warning("RoomWizard: failed to load vkeybd pack (deploy vkeybd_roomwizard.zip to /opt/games/)");
			delete _vkbd;
			_vkbd = nullptr;
			return;
		}
	}
	_vkbd->show();
	debug("RoomWizard: virtual keyboard shown");
#endif
}

// Factory function
OSystem *OSystem_RoomWizard_create() {
	return new OSystem_RoomWizard();
}

// Main entry point
int main(int argc, char *argv[]) {
	// Create the backend
	g_system = OSystem_RoomWizard_create();
	assert(g_system);

	// Invoke ScummVM main
	int res = scummvm_main(argc, argv);
	
	// Cleanup
	g_system->destroy();
	
	return res;
}
