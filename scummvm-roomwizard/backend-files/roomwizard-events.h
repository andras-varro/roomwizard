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

#ifndef BACKENDS_EVENTS_ROOMWIZARD_H
#define BACKENDS_EVENTS_ROOMWIZARD_H

#include "common/events.h"

// Include C headers directly
extern "C" {
#include "touch_input.h"
}

class RoomWizardEventSource : public Common::EventSource {
public:
	RoomWizardEventSource();
	virtual ~RoomWizardEventSource();

	bool pollEvent(Common::Event &event) override;
	
	/**
	 * Disable keymapper processing for touch events.
	 * Touch input should pass through directly without remapping.
	 * This ensures LBUTTONDOWN/UP events reach the GUI/engine.
	 */
	bool allowMapping() const override { return false; }

	// Set game screen dimensions for coordinate transformation
	void setGameScreenSize(int width, int height, int offsetX, int offsetY);
	
	// Get bezel margins from touch calibration
	void getBezelMargins(int &top, int &bottom, int &left, int &right) const;

private:
	TouchInput *_touchInput;
	bool _touchInitialized;

	// Touch state machine
	enum TouchPhase {
		TOUCH_NONE,
		TOUCH_PRESSED,    // Just pressed, need to send MOUSEMOVE + LBUTTONDOWN
		TOUCH_HELD        // Held down, send MOUSEMOVE on position change or LBUTTONUP on release
	};
	
	TouchPhase _touchPhase;
	bool _buttonDownSent;
	bool _waitForRelease;    // set after gesture or context switch; blocks input until finger lifts
	bool _prevOverlayVisible; // tracks overlay state to detect GMM open/close transitions
	int _lastTouchX;
	int _lastTouchY;
	uint32 _touchStartTime;

	// Game screen transformation
	int _gameWidth;
	int _gameHeight;
	int _gameOffsetX;
	int _gameOffsetY;

	// Long press for right-click
	static const uint32 LONG_PRESS_TIME = 500; // milliseconds

	// -------------------------------------------------------
	// Gesture detection
	// -------------------------------------------------------
	// Corner zones (on 800x480 screen):
	//   Bottom-left  (x<80, y>400) triple-tap  → Virtual Keyboard
	//   Bottom-right (x>720, y>400) triple-tap → Global Main Menu (Ctrl+F5)
	enum Corner { CORNER_BL = 0, CORNER_BR = 1, CORNER_COUNT = 2 };
	struct CornerTaps {
		uint32 timestamps[3]; // ring buffer of last 3 tap times
		int    count;         // how many taps accumulated
	};
	CornerTaps _cornerTaps[CORNER_COUNT];

	// Pending synthetic events queued by gesture detection
	static const int MAX_PENDING = 8;
	Common::Event _pending[MAX_PENDING];
	int           _pendingHead;
	int           _pendingCount;

	void   checkGestures(int touchX, int touchY, uint32 now);
	Corner cornerFor(int x, int y) const; // returns CORNER_COUNT if not in any corner
	void   pushEvent(const Common::Event &e);
	void   pushKeyEvent(Common::KeyCode kc, byte flags);

	// Helper methods
	void initTouch();
	void closeTouch();
	void transformCoordinates(int touchX, int touchY, int &gameX, int &gameY);
};

#endif
