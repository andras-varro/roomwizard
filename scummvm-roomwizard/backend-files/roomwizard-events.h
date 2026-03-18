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
	 * Allow keymapper processing for events from this source.
	 * ScummVM engines and GUI rely on the keymapper to convert keyboard
	 * and gamepad events into actions.  Touch events (MOUSEMOVE,
	 * LBUTTONDOWN/UP) are not keymapped anyway, so returning true is safe.
	 */
	bool allowMapping() const override { return true; }

	// Set game screen dimensions for coordinate transformation
	void setGameScreenSize(int width, int height, int offsetX, int offsetY);
	
	// Get bezel margins from touch calibration
	void getBezelMargins(int &top, int &bottom, int &left, int &right) const;

private:
	// -------------------------------------------------------
	// Touch input (existing)
	// -------------------------------------------------------
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
	bool _longPressFired;    // true after 500ms LBUTTONUP+RBUTTONDOWN sent
	bool _waitForRelease;    // set after gesture or context switch; blocks input until finger lifts
	bool _prevOverlayVisible; // tracks overlay state to detect GMM open/close transitions
	int _lastTouchX;
	int _lastTouchY;
	uint32 _touchStartTime;

	// Game screen transformation
	int _gameWidth;
	int _gameHeight;

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

	// Pending synthetic events queued by gesture detection and multi-event input
	static const int MAX_PENDING = 16;
	Common::Event _pending[MAX_PENDING];
	int           _pendingHead;
	int           _pendingCount;

	void   checkGestures(int touchX, int touchY, uint32 now);
	Corner cornerFor(int x, int y) const; // returns CORNER_COUNT if not in any corner
	void   pushEvent(const Common::Event &e);

	// Touch helper methods
	void initTouch();
	void closeTouch();
	bool pollTouch(Common::Event &event);

	// Coordinate transformation (shared by touch, mouse, gamepad)
	void transformCoordinates(int touchX, int touchY, int &gameX, int &gameY);

	// -------------------------------------------------------
	// USB input device scanning and management
	// -------------------------------------------------------
	enum DeviceType {
		DEV_UNKNOWN = 0,
		DEV_KEYBOARD,
		DEV_MOUSE,
		DEV_GAMEPAD
	};

	int _keyboardFd;
	int _mouseFd;
	int _gamepadFd;

	void scanInputDevices();         // Scan /dev/input/event* for USB devices
	DeviceType classifyDevice(int fd); // Classify as keyboard/mouse/gamepad
	void closeInputDevices();        // Close all USB device fds

	// Periodic rescan timer
	uint32 _lastDeviceScan;
	static const uint32 DEVICE_SCAN_INTERVAL = 5000; // 5 seconds
	// BUG-INPUT-004 FIX: Increased from 16 to 32 — USB keyboards/mice are often
	// assigned event numbers >= 16 when built-in devices occupy the lower slots.
	static const int MAX_EVDEV_DEVICES = 32;

	// -------------------------------------------------------
	// USB Keyboard support
	// -------------------------------------------------------
	bool pollKeyboard(Common::Event &event);

	// Keyboard modifier state tracking
	byte _modifierFlags;  // Current modifier state (KBD_SHIFT, KBD_CTRL, KBD_ALT)

	// Map Linux KEY_* scancode to ScummVM keycode + ascii
	struct KeyMapping {
		Common::KeyCode keycode;
		uint16 ascii;           // unshifted ascii value (0 if not printable)
		uint16 shiftAscii;      // shifted ascii value (0 if same or not printable)
	};
	static KeyMapping mapLinuxKey(int linuxKeyCode, byte modifiers);

	// -------------------------------------------------------
	// USB Mouse support
	// -------------------------------------------------------
	bool pollMouse(Common::Event &event);

	// Mouse state
	int _mouseX, _mouseY;           // Current cursor position in screen coords (0..799, 0..479)
	int _prevMouseButtons;           // Previous button state for edge detection

	// Mouse acceleration config (loaded from /etc/input_config.conf)
	float _mouseSensitivity;         // default 1.5
	float _mouseAcceleration;        // default 2.0
	int   _mouseLowThreshold;        // default 3
	int   _mouseHighThreshold;       // default 15

	// -------------------------------------------------------
	// USB Gamepad support
	// -------------------------------------------------------
	bool pollGamepad(Common::Event &event);

	// Gamepad state
	int _gamepadAxisX, _gamepadAxisY;       // Current analog stick values (raw)
	int _gamepadHatX, _gamepadHatY;         // Current D-pad hat values (-1, 0, +1)
	int _gamepadAxisMin, _gamepadAxisMax;    // Axis range from EVIOCGABS
	int _gamepadAxisCenter;                  // Center calibration
	int _prevGamepadButtons;                 // Previous button state for edge detection
	uint32 _lastCursorMove;                  // For cursor movement rate limiting

	// Gamepad cursor movement constants
	static const int GAMEPAD_CURSOR_SPEED = 5;    // Max pixels per poll at full deflection
	static const int GAMEPAD_DEADZONE_PCT = 20;    // Percentage of axis range
	static const uint32 GAMEPAD_CURSOR_INTERVAL = 16; // ~60 Hz cursor movement

	// Gamepad button mapping (configurable for clone controllers)
	struct GamepadBtnMap {
		int btnSouth;    // A — left click (default BTN_SOUTH / 304)
		int btnEast;     // B — right click (default BTN_EAST / 305)
		int btnWest;     // X — space/skip cutscene (default BTN_WEST / 308)
		int btnNorth;    // Y — escape/skip dialog (default BTN_NORTH / 307)
		int btnStart;    // Start — Ctrl+F5 GMM (default BTN_START / 315)
		int btnSelect;   // Select — F5 save/load (default BTN_SELECT / 314)
		int btnTL;       // Left bumper — period/skip text (default BTN_TL / 310)
		int btnTR;       // Right bumper — escape (default BTN_TR / 311)
		int hatXAxis;    // D-pad X axis (default ABS_HAT0X / 16)
		int hatYAxis;    // D-pad Y axis (default ABS_HAT0Y / 17)
		int stickXAxis;  // Left stick X (default ABS_X / 0)
		int stickYAxis;  // Left stick Y (default ABS_Y / 1)
	};
	GamepadBtnMap _gamepadMap;

	void initDefaultGamepadMap();
	void loadGamepadAxisCalibration();

	// -------------------------------------------------------
	// Config file loading
	// -------------------------------------------------------
	void loadInputConfig();
};

#endif
