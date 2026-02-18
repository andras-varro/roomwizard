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

#ifndef BACKENDS_PLATFORM_ROOMWIZARD_H
#define BACKENDS_PLATFORM_ROOMWIZARD_H

#include "common/scummsys.h"
#include "backends/modular-backend.h"

// Forward declarations
class RoomWizardGraphicsManager;
class RoomWizardEventSource;
namespace Common { class VirtualKeyboard; }

class OSystem_RoomWizard : public ModularMixerBackend, public ModularGraphicsBackend {
public:
	OSystem_RoomWizard();
	virtual ~OSystem_RoomWizard();

	virtual void initBackend() override;

	virtual Common::MutexInternal *createMutex() override;
	virtual uint32 getMillis(bool skipRecord = false) override;
	virtual void delayMillis(uint msecs) override;
	virtual void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override;

	virtual void quit() override;

	virtual void logMessage(LogMessageType::Type type, const char *message) override;

	virtual void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override;

	// Virtual keyboard support (requires build with --enable-vkeybd)
	virtual bool hasFeature(Feature f) override;
	virtual void setFeatureState(Feature f, bool enable) override;
	virtual bool getFeatureState(Feature f) override;
	void showVirtualKeyboard();

	// Access to event source for bezel margin queries
	RoomWizardEventSource *getEventSource() const { return _eventSource; }

private:
	RoomWizardEventSource *_eventSource;
	timeval _startTime;
#ifdef ENABLE_VKEYBD
	Common::VirtualKeyboard *_vkbd;
#endif
};

#endif
