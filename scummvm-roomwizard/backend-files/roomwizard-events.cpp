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

#include "backends/platform/roomwizard/roomwizard-events.h"
#include "backends/platform/roomwizard/roomwizard.h"
#include "backends/platform/roomwizard/roomwizard-graphics.h"
#include "common/system.h"
#include "common/textconsole.h"
#include <stdlib.h>

RoomWizardEventSource::RoomWizardEventSource()
	: _touchInput(nullptr),
	  _touchInitialized(false),
	  _touchPhase(TOUCH_NONE),
	  _buttonDownSent(false),
	  _lastTouchX(0),
	  _lastTouchY(0),
	  _touchStartTime(0),
	  _gameWidth(320),
	  _gameHeight(200),
	  _gameOffsetX(0),
	  _gameOffsetY(0),
	  _pendingHead(0),
	  _pendingCount(0) {

	memset(_cornerTaps, 0, sizeof(_cornerTaps));
	memset(_pending,    0, sizeof(_pending));
	initTouch();
}

RoomWizardEventSource::~RoomWizardEventSource() {
	closeTouch();
}

void RoomWizardEventSource::initTouch() {
	if (_touchInitialized)
		return;

	_touchInput = (TouchInput *)malloc(sizeof(TouchInput));
	if (!_touchInput) {
		warning("Failed to allocate touch input structure");
		return;
	}

	if (touch_init(_touchInput, "/dev/input/event0") < 0) {
		warning("Failed to initialize touch input - device may not be available");
		free(_touchInput);
		_touchInput = nullptr;
		return;
	}

	// Set screen size for coordinate scaling
	touch_set_screen_size(_touchInput, 800, 480);

	// Load touch/bezel calibration if available
	if (touch_load_calibration(_touchInput, "/etc/touch_calibration.conf") == 0) {
		touch_enable_calibration(_touchInput, true);
		warning("RoomWizard: touch calibration loaded (bezel T=%d B=%d L=%d R=%d)",
		        _touchInput->calib.bezel_top, _touchInput->calib.bezel_bottom,
		        _touchInput->calib.bezel_left, _touchInput->calib.bezel_right);
	} else {
		warning("RoomWizard: no calibration file, using defaults");
	}

	_touchInitialized = true;
	warning("RoomWizard touch input initialized");
}

void RoomWizardEventSource::closeTouch() {
	if (_touchInitialized && _touchInput) {
		touch_close(_touchInput);
		free(_touchInput);
		_touchInput = nullptr;
		_touchInitialized = false;
	}
}

// ---------------------------------------------------------------------------
// Gesture detection
// ---------------------------------------------------------------------------

RoomWizardEventSource::Corner RoomWizardEventSource::cornerFor(int x, int y) const {
	const int ZONE = 80; // px from edge
	if (y > 480 - ZONE) {
		if (x < ZONE)       return CORNER_BL;
		if (x > 800 - ZONE) return CORNER_BR;
	}
	return CORNER_COUNT; // not in a corner
}

void RoomWizardEventSource::pushEvent(const Common::Event &e) {
	if (_pendingCount >= MAX_PENDING)
		return;
	int slot = (_pendingHead + _pendingCount) % MAX_PENDING;
	_pending[slot] = e;
	_pendingCount++;
}

void RoomWizardEventSource::pushKeyEvent(Common::KeyCode kc, byte flags) {
	Common::Event down, up;
	down.type = Common::EVENT_KEYDOWN;
	down.kbd.keycode = kc;
	down.kbd.flags   = flags;
	down.kbd.ascii   = 0;
	up = down;
	up.type = Common::EVENT_KEYUP;
	pushEvent(down);
	pushEvent(up);
}

void RoomWizardEventSource::checkGestures(int touchX, int touchY, uint32 now) {
	Corner c = cornerFor(touchX, touchY);
	if (c == CORNER_COUNT)
		return;

	const uint32 WINDOW = 1200; // ms for triple-tap
	CornerTaps &ct = _cornerTaps[c];

	// Expire taps older than the window
	while (ct.count > 0) {
		uint32 oldest = ct.timestamps[(ct.count == 3) ? 0 : 0];
		// find actual oldest in ring (simplified: count <= 3, just check first)
		if (now - ct.timestamps[0] > WINDOW) {
			// shift timestamps down
			for (int i = 0; i < ct.count - 1; i++)
				ct.timestamps[i] = ct.timestamps[i + 1];
			ct.count--;
		} else {
			break;
		}
	}

	// Record this tap
	if (ct.count < 3)
		ct.timestamps[ct.count] = now;
	else {
		// shift and overwrite last slot
		ct.timestamps[0] = ct.timestamps[1];
		ct.timestamps[1] = ct.timestamps[2];
		ct.timestamps[2] = now;
	}
	ct.count = (ct.count < 3) ? ct.count + 1 : 3;

	// Check for triple-tap within window
	if (ct.count == 3 && (now - ct.timestamps[0]) <= WINDOW) {
		ct.count = 0; // reset so it doesn't fire again immediately
		warning("Gesture: triple-tap corner %d", (int)c);

		if (c == CORNER_BR) {
			// Bottom-right: open Global Main Menu (Ctrl+F5)
			warning("Gesture: opening GMM via Ctrl+F5");
			pushKeyEvent(Common::KEYCODE_F5, Common::KBD_CTRL);
		} else if (c == CORNER_BL) {
			// Bottom-left: show virtual keyboard
			warning("Gesture: opening virtual keyboard");
			OSystem_RoomWizard *system = dynamic_cast<OSystem_RoomWizard *>(g_system);
			if (system)
				system->showVirtualKeyboard();
		}
	}
}

void RoomWizardEventSource::getBezelMargins(int &top, int &bottom, int &left, int &right) const {
	if (_touchInitialized && _touchInput) {
		top    = _touchInput->calib.bezel_top;
		bottom = _touchInput->calib.bezel_bottom;
		left   = _touchInput->calib.bezel_left;
		right  = _touchInput->calib.bezel_right;
	} else {
		top = bottom = left = right = 0;
	}
}

void RoomWizardEventSource::setGameScreenSize(int width, int height, int offsetX, int offsetY) {
	_gameWidth = width;
	_gameHeight = height;
	_gameOffsetX = offsetX;
	_gameOffsetY = offsetY;
	
	debug("Game screen size set to %dx%d at offset (%d, %d)", width, height, offsetX, offsetY);
}

void RoomWizardEventSource::transformCoordinates(int touchX, int touchY, int &gameX, int &gameY) {
	// Check if overlay (GUI) is visible - if so, use full screen coordinates
	OSystem_RoomWizard *system = dynamic_cast<OSystem_RoomWizard *>(g_system);
	if (system && system->getGraphicsManager() && 
	    ((RoomWizardGraphicsManager *)system->getGraphicsManager())->isOverlayVisible()) {
		// Overlay uses full 800x480 resolution - no transformation needed
		gameX = touchX;
		gameY = touchY;
		
		// Clamp to overlay bounds
		if (gameX < 0) gameX = 0;
		if (gameY < 0) gameY = 0;
		if (gameX >= 800) gameX = 799;
		if (gameY >= 480) gameY = 479;
	} else {
		// Transform from framebuffer coordinates to game coordinates using bezel-aware scaling.
		// Reverse the mapping applied in blitGameSurfaceToFramebuffer / getScalingInfo.
		int scaledW = _gameWidth, scaledH = _gameHeight;
		int fbOffsetX = 0, fbOffsetY = 0;

		if (system && system->getGraphicsManager()) {
			((RoomWizardGraphicsManager *)system->getGraphicsManager())
				->getScalingInfo(scaledW, scaledH, fbOffsetX, fbOffsetY);
		}

		// Remove framebuffer offset then scale back to game coords
		int relX = touchX - fbOffsetX;
		int relY = touchY - fbOffsetY;

		if (scaledW > 0 && scaledH > 0) {
			gameX = relX * _gameWidth  / scaledW;
			gameY = relY * _gameHeight / scaledH;
		} else {
			gameX = relX;
			gameY = relY;
		}

		// Clamp to game bounds
		if (gameX < 0) gameX = 0;
		if (gameY < 0) gameY = 0;
		if (gameX >= _gameWidth)  gameX = _gameWidth  - 1;
		if (gameY >= _gameHeight) gameY = _gameHeight - 1;
	}
}

bool RoomWizardEventSource::pollEvent(Common::Event &event) {
	// Drain any synthetic events queued by gesture detection
	if (_pendingCount > 0) {
		event = _pending[_pendingHead];
		_pendingHead = (_pendingHead + 1) % MAX_PENDING;
		_pendingCount--;
		return true;
	}

	if (!_touchInitialized || !_touchInput) {
		return false;
	}

	// Poll touch input (non-blocking) - updates internal state
	int poll_result = touch_poll(_touchInput);
	TouchState state = touch_get_state(_touchInput);
	uint32 currentTime = g_system->getMillis();
	
	// Debug: Log touch state changes
	static bool lastPressed = false;
	static bool lastHeld = false;
	static bool lastReleased = false;
	if (state.pressed != lastPressed || state.held != lastHeld || state.released != lastReleased) {
		warning("Touch state: pressed=%d held=%d released=%d phase=%d", 
		        state.pressed, state.held, state.released, _touchPhase);
		lastPressed = state.pressed;
		lastHeld = state.held;
		lastReleased = state.released;
	}
	
	// State machine: Process touch events in proper order
	// Hardware sends: ABS_X/Y → BTN_TOUCH → SYN_REPORT
	// We must generate: MOUSEMOVE → LBUTTONDOWN → [MOUSEMOVE...] → LBUTTONUP
	
	switch (_touchPhase) {
		case TOUCH_NONE:
			// Waiting for touch to start
			if (state.pressed || state.held) {
				// Touch just started - capture coordinates and time
				_lastTouchX = state.x;
				_lastTouchY = state.y;
				_touchStartTime = currentTime;
				_touchPhase = TOUCH_PRESSED;
				_buttonDownSent = false;

				// Check for corner gesture (triple-tap) before processing as normal touch
				checkGestures(state.x, state.y, currentTime);
				if (_pendingCount > 0) {
					// Gesture fired - drain first event now, return it
					event = _pending[_pendingHead];
					_pendingHead = (_pendingHead + 1) % MAX_PENDING;
					_pendingCount--;
					_touchPhase = TOUCH_NONE; // don't generate a click for the gesture tap
					return true;
				}
				
				warning("TOUCH_NONE -> TOUCH_PRESSED: sending MOUSEMOVE at (%d,%d)", state.x, state.y);
				
				// Add touch point for visual feedback
				OSystem_RoomWizard *system = dynamic_cast<OSystem_RoomWizard *>(g_system);
				if (system && system->getGraphicsManager()) {
					((RoomWizardGraphicsManager *)system->getGraphicsManager())->addTouchPoint(state.x, state.y);
				}
				
				// Send MOUSEMOVE first
				// NOTE: Do NOT call warpMouse() here - it calls purgeMouseEvents() which
				// clears the event queue and causes LBUTTONDOWN to be lost!
				int gameX, gameY;
				transformCoordinates(state.x, state.y, gameX, gameY);
				event.type = Common::EVENT_MOUSEMOVE;
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				return true;
			}
			break;
			
		case TOUCH_PRESSED:
			// Just pressed - need to send LBUTTONDOWN after MOUSEMOVE
			if (!_buttonDownSent && state.held) {
				_buttonDownSent = true;
				_touchPhase = TOUCH_HELD;
				
				int gameX, gameY;
				transformCoordinates(_lastTouchX, _lastTouchY, gameX, gameY);
				
				warning("TOUCH_PRESSED -> TOUCH_HELD: sending LBUTTONDOWN at (%d,%d)", gameX, gameY);
				
				event.type = Common::EVENT_LBUTTONDOWN;
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				// NOTE: Do NOT call warpMouse() - it purges the event queue!
				return true;
			}
			// If touch released before we sent button down, skip to NONE
			if (state.released || !state.held) {
				warning("TOUCH_PRESSED -> TOUCH_NONE: touch released too fast");
				_touchPhase = TOUCH_NONE;
			}
			break;
			
		case TOUCH_HELD:
			// Touch is being held
			if (state.released || !state.held) {
				// Touch released - send button up immediately
				uint32 touchDuration = currentTime - _touchStartTime;
				_touchPhase = TOUCH_NONE;
				
				warning("TOUCH_HELD -> TOUCH_NONE: sending LBUTTONUP (duration=%dms)", touchDuration);
				
				int gameX, gameY;
				transformCoordinates(_lastTouchX, _lastTouchY, gameX, gameY);
				
				// Check for long press (right-click)
				if (touchDuration >= LONG_PRESS_TIME) {
					event.type = Common::EVENT_RBUTTONUP;
				} else {
					event.type = Common::EVENT_LBUTTONUP;
				}
				
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				return true;
			}
			
			// Check for movement while held (drag)
			if (state.held && (state.x != _lastTouchX || state.y != _lastTouchY)) {
				_lastTouchX = state.x;
				_lastTouchY = state.y;
				
				int gameX, gameY;
				transformCoordinates(state.x, state.y, gameX, gameY);
				event.type = Common::EVENT_MOUSEMOVE;
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				// NOTE: Do NOT call warpMouse() - it purges the event queue!
				return true;
			}
			break;
	}

	return false;
}
