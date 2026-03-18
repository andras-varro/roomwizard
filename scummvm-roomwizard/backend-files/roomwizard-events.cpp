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

// Forbidden symbol exceptions needed for file I/O (config loading) and
// device scanning (open/read/close/ioctl).
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_fopen
#define FORBIDDEN_SYMBOL_EXCEPTION_fclose
#define FORBIDDEN_SYMBOL_EXCEPTION_fgets
#define FORBIDDEN_SYMBOL_EXCEPTION_unistd_h

#include "backends/platform/roomwizard/roomwizard-events.h"
#include "backends/platform/roomwizard/roomwizard.h"
#include "backends/platform/roomwizard/roomwizard-graphics.h"
#include "backends/timer/default/default-timer.h"
#include "common/system.h"
#include "common/textconsole.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <errno.h>
#include <stdio.h>

// =========================================================================
// Bit-test helpers for evdev capability queries (same pattern as gamepad.c)
// =========================================================================
#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(x)        ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)          ((x) % BITS_PER_LONG)
#define BIT_LONG(x)     ((x) / BITS_PER_LONG)
#define test_bit(b, a)  ((a[BIT_LONG(b)] >> OFF(b)) & 1)

// =========================================================================
// Constructor / Destructor
// =========================================================================

RoomWizardEventSource::RoomWizardEventSource()
	: _touchInput(nullptr),
	  _touchInitialized(false),
	  _touchPhase(TOUCH_NONE),
	  _buttonDownSent(false),
	  _longPressFired(false),
	  _waitForRelease(false),
	  _prevOverlayVisible(false),
	  _lastTouchX(0),
	  _lastTouchY(0),
	  _touchStartTime(0),
	  _gameWidth(320),
	  _gameHeight(200),
	  _pendingHead(0),
	  _pendingCount(0),
	  // USB devices
	  _keyboardFd(-1),
	  _mouseFd(-1),
	  _gamepadFd(-1),
	  _lastDeviceScan(0),
	  // Keyboard
	  _modifierFlags(0),
	  // Mouse
	  _mouseX(400),
	  _mouseY(240),
	  _prevMouseButtons(0),
	  _mouseSensitivity(1.5f),
	  _mouseAcceleration(2.0f),
	  _mouseLowThreshold(3),
	  _mouseHighThreshold(15),
	  // Gamepad
	  _gamepadAxisX(0),
	  _gamepadAxisY(0),
	  _gamepadHatX(0),
	  _gamepadHatY(0),
	  _gamepadAxisMin(-32768),
	  _gamepadAxisMax(32767),
	  _gamepadAxisCenter(0),
	  _prevGamepadButtons(0),
	  _lastCursorMove(0) {

	memset(_cornerTaps, 0, sizeof(_cornerTaps));
	memset(_pending,    0, sizeof(_pending));

	initDefaultGamepadMap();
	loadInputConfig();
	initTouch();
	scanInputDevices();
}

RoomWizardEventSource::~RoomWizardEventSource() {
	closeTouch();
	closeInputDevices();
}

// =========================================================================
// Touch initialization (unchanged from original)
// =========================================================================

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
		debug("RoomWizard: touch calibration loaded (bezel T=%d B=%d L=%d R=%d)",
		      _touchInput->calib.bezel_top, _touchInput->calib.bezel_bottom,
		      _touchInput->calib.bezel_left, _touchInput->calib.bezel_right);
	} else {
		debug("RoomWizard: no calibration file, using defaults");
	}

	_touchInitialized = true;
	debug("RoomWizard touch input initialized");
}

void RoomWizardEventSource::closeTouch() {
	if (_touchInitialized && _touchInput) {
		touch_close(_touchInput);
		free(_touchInput);
		_touchInput = nullptr;
		_touchInitialized = false;
	}
}

// =========================================================================
// USB Input Device Scanning and Management
// =========================================================================

RoomWizardEventSource::DeviceType RoomWizardEventSource::classifyDevice(int fd) {
	unsigned long ev[NBITS(EV_MAX)] = {0};
	unsigned long kb[NBITS(KEY_MAX)] = {0};
	unsigned long ab[NBITS(ABS_MAX)] = {0};
	unsigned long rb[NBITS(REL_MAX)] = {0};

	if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0)
		return DEV_UNKNOWN;

	bool has_key = test_bit(EV_KEY, ev);
	bool has_abs = test_bit(EV_ABS, ev);
	bool has_rel = test_bit(EV_REL, ev);

	if (has_key)
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kb)), kb);
	if (has_abs)
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ab)), ab);
	if (has_rel)
		ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rb)), rb);

	// Gamepad: has ABS_X, ABS_Y + gamepad buttons
	if (has_abs && has_key && test_bit(ABS_X, ab) && test_bit(ABS_Y, ab) &&
	    (test_bit(BTN_GAMEPAD, kb) || test_bit(BTN_SOUTH, kb) ||
	     test_bit(BTN_A, kb) || test_bit(BTN_JOYSTICK, kb)))
		return DEV_GAMEPAD;

	// Mouse: has REL_X, REL_Y, BTN_LEFT
	if (has_rel && has_key &&
	    test_bit(REL_X, rb) && test_bit(REL_Y, rb) &&
	    test_bit(BTN_LEFT, kb))
		return DEV_MOUSE;

	// Keyboard: has >= 20 letter keys (A-Z)
	if (has_key) {
		static const int letter_keys[] = {
			KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
			KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
			KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M
		};
		int count = 0;
		for (int k = 0; k < (int)(sizeof(letter_keys) / sizeof(letter_keys[0])); k++)
			if (test_bit(letter_keys[k], kb)) count++;
		if (count >= 20)
			return DEV_KEYBOARD;
	}

	return DEV_UNKNOWN;
}

void RoomWizardEventSource::scanInputDevices() {
	char path[64];
	char name[128];

	for (int i = 0; i < MAX_EVDEV_DEVICES; i++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", i);

		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			// BUG-INPUT-004 FIX: Log permission errors so we can diagnose
			// "no input" issues caused by /dev/input/event* permissions.
			if (errno == EACCES || errno == EPERM) {
				warning("RoomWizard: cannot open %s — permission denied (run as root or add to 'input' group)", path);
			}
			continue;
		}

		name[0] = '\0';
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);

		// Filter out Panjit touchscreen (already handled by touch code)
		if (strstr(name, "panjit") || strstr(name, "Panjit") || strstr(name, "PANJIT") ||
		    strstr(name, "TouchScreen") || strstr(name, "touchscreen")) {
			close(fd);
			continue;
		}

		DeviceType type = classifyDevice(fd);

		if (type == DEV_GAMEPAD && _gamepadFd < 0) {
			_gamepadFd = fd;
			loadGamepadAxisCalibration();
			warning("RoomWizard: Detected gamepad '%s' at %s", name, path);
		} else if (type == DEV_KEYBOARD && _keyboardFd < 0) {
			_keyboardFd = fd;
			warning("RoomWizard: Detected keyboard '%s' at %s", name, path);
		} else if (type == DEV_MOUSE && _mouseFd < 0) {
			_mouseFd = fd;
			warning("RoomWizard: Detected mouse '%s' at %s", name, path);
		} else {
			close(fd);
		}

		if (_gamepadFd >= 0 && _keyboardFd >= 0 && _mouseFd >= 0)
			break;
	}

	// BUG-INPUT-004 FIX: Log a summary after scanning so we know what was
	// found (or not found).  This is essential for diagnosing "no input" issues.
	warning("RoomWizard: device scan complete — keyboard=%s mouse=%s gamepad=%s",
	      (_keyboardFd >= 0 ? "YES" : "no"),
	      (_mouseFd >= 0    ? "YES" : "no"),
	      (_gamepadFd >= 0  ? "YES" : "no"));
}

void RoomWizardEventSource::closeInputDevices() {
	if (_keyboardFd >= 0) { close(_keyboardFd); _keyboardFd = -1; }
	if (_mouseFd >= 0)    { close(_mouseFd);    _mouseFd = -1; }
	if (_gamepadFd >= 0)  { close(_gamepadFd);  _gamepadFd = -1; }
}

void RoomWizardEventSource::loadGamepadAxisCalibration() {
	if (_gamepadFd < 0) return;

	struct input_absinfo info;
	if (ioctl(_gamepadFd, EVIOCGABS(_gamepadMap.stickXAxis), &info) == 0) {
		_gamepadAxisMin = info.minimum;
		_gamepadAxisMax = info.maximum;
		// BUG-INPUT-004 FIX: Use computed midpoint instead of info.value.
		// info.value is the stick position at the moment of the ioctl call;
		// if the stick is not perfectly centered the cursor will drift.
		// Same fix pattern as BUG-INPUT-003 in native_apps gamepad.c.
		_gamepadAxisCenter = (info.minimum + info.maximum) / 2;
		debug("RoomWizard: gamepad axis range [%d..%d], center=%d (computed midpoint)",
		      _gamepadAxisMin, _gamepadAxisMax, _gamepadAxisCenter);
	}
}

// =========================================================================
// Gesture detection (unchanged from original)
// =========================================================================

RoomWizardEventSource::Corner RoomWizardEventSource::cornerFor(int x, int y) const {
	const int ZONE = 80;
	if (y > 480 - ZONE) {
		if (x < ZONE)       return CORNER_BL;
		if (x > 800 - ZONE) return CORNER_BR;
	}
	return CORNER_COUNT;
}

void RoomWizardEventSource::pushEvent(const Common::Event &e) {
	if (_pendingCount >= MAX_PENDING)
		return;
	int slot = (_pendingHead + _pendingCount) % MAX_PENDING;
	_pending[slot] = e;
	_pendingCount++;
}

void RoomWizardEventSource::checkGestures(int touchX, int touchY, uint32 now) {
	Corner c = cornerFor(touchX, touchY);
	if (c == CORNER_COUNT)
		return;

	const uint32 WINDOW = 1200;
	CornerTaps &ct = _cornerTaps[c];

	while (ct.count > 0) {
		if (now - ct.timestamps[0] > WINDOW) {
			for (int i = 0; i < ct.count - 1; i++)
				ct.timestamps[i] = ct.timestamps[i + 1];
			ct.count--;
		} else {
			break;
		}
	}

	if (ct.count < 3)
		ct.timestamps[ct.count] = now;
	else {
		ct.timestamps[0] = ct.timestamps[1];
		ct.timestamps[1] = ct.timestamps[2];
		ct.timestamps[2] = now;
	}
	ct.count = (ct.count < 3) ? ct.count + 1 : 3;

	if (ct.count == 3 && (now - ct.timestamps[0]) <= WINDOW) {
		ct.count = 0;
		_waitForRelease = true;
		debug("Gesture: triple-tap corner %d", (int)c);

		if (c == CORNER_BR) {
			debug("Gesture: opening GMM via Ctrl+F5");
			Common::Event ctrlDown, f5Down, f5Up, ctrlUp;
			ctrlDown.type        = Common::EVENT_KEYDOWN;
			ctrlDown.kbd.keycode = Common::KEYCODE_LCTRL;
			ctrlDown.kbd.flags   = Common::KBD_CTRL;
			ctrlDown.kbd.ascii   = 0;
			f5Down.type          = Common::EVENT_KEYDOWN;
			f5Down.kbd.keycode   = Common::KEYCODE_F5;
			f5Down.kbd.flags     = Common::KBD_CTRL;
			f5Down.kbd.ascii     = 0;
			f5Up = f5Down;
			f5Up.type            = Common::EVENT_KEYUP;
			ctrlUp = ctrlDown;
			ctrlUp.type          = Common::EVENT_KEYUP;
			ctrlUp.kbd.flags     = 0;
			pushEvent(ctrlDown);
			pushEvent(f5Down);
			pushEvent(f5Up);
			pushEvent(ctrlUp);
		} else if (c == CORNER_BL) {
			debug("Gesture: opening virtual keyboard");
			OSystem_RoomWizard *sys = rwSystem();
			if (sys)
				sys->showVirtualKeyboard();
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
	debug("Game screen size set to %dx%d at offset (%d, %d)", width, height, offsetX, offsetY);
}

void RoomWizardEventSource::transformCoordinates(int touchX, int touchY, int &gameX, int &gameY) {
	OSystem_RoomWizard *system = rwSystem();
	if (system && system->getGraphicsManager() &&
	    ((RoomWizardGraphicsManager *)system->getGraphicsManager())->isOverlayVisible()) {
		gameX = touchX;
		gameY = touchY;
		if (gameX < 0) gameX = 0;
		if (gameY < 0) gameY = 0;
		if (gameX >= 800) gameX = 799;
		if (gameY >= 480) gameY = 479;
	} else {
		int scaledW = _gameWidth, scaledH = _gameHeight;
		int fbOffsetX = 0, fbOffsetY = 0;
		if (system && system->getGraphicsManager()) {
			((RoomWizardGraphicsManager *)system->getGraphicsManager())
				->getScalingInfo(scaledW, scaledH, fbOffsetX, fbOffsetY);
		}
		int relX = touchX - fbOffsetX;
		int relY = touchY - fbOffsetY;
		if (scaledW > 0 && scaledH > 0) {
			gameX = relX * _gameWidth  / scaledW;
			gameY = relY * _gameHeight / scaledH;
		} else {
			gameX = relX;
			gameY = relY;
		}
		if (gameX < 0) gameX = 0;
		if (gameY < 0) gameY = 0;
		if (gameX >= _gameWidth)  gameX = _gameWidth  - 1;
		if (gameY >= _gameHeight) gameY = _gameHeight - 1;
	}
}

// =========================================================================
// Config file loading
// =========================================================================

void RoomWizardEventSource::loadInputConfig() {
	FILE *f = fopen("/etc/input_config.conf", "r");
	if (!f) {
		debug("RoomWizard: no input config at /etc/input_config.conf (using defaults)");
		return;
	}

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';

		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0' || *p == '#') continue;

		char *eq = strchr(p, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = p;
		char *val = eq + 1;

		// Trim trailing whitespace from key
		char *end = eq - 1;
		while (end > key && (*end == ' ' || *end == '\t'))
			*end-- = '\0';

		// Mouse settings
		if (strcmp(key, "mouse_sensitivity") == 0) {
			float v = (float)atof(val);
			if (v > 0.1f && v < 20.0f) _mouseSensitivity = v;
		} else if (strcmp(key, "mouse_acceleration") == 0) {
			float v = (float)atof(val);
			if (v > 0.1f && v < 20.0f) _mouseAcceleration = v;
		} else if (strcmp(key, "mouse_low_threshold") == 0) {
			int v = atoi(val);
			if (v >= 0 && v < 100) _mouseLowThreshold = v;
		} else if (strcmp(key, "mouse_high_threshold") == 0) {
			int v = atoi(val);
			if (v >= 1 && v < 500) _mouseHighThreshold = v;
		}
		// Gamepad button mapping (same keys as gamepad.c)
		else if (strcmp(key, "gamepad_btn_jump") == 0)   { _gamepadMap.btnSouth  = atoi(val); }
		else if (strcmp(key, "gamepad_btn_run") == 0)    { _gamepadMap.btnEast   = atoi(val); }
		else if (strcmp(key, "gamepad_btn_action") == 0) { _gamepadMap.btnWest   = atoi(val); }
		else if (strcmp(key, "gamepad_btn_pause") == 0)  { _gamepadMap.btnStart  = atoi(val); }
		else if (strcmp(key, "gamepad_btn_back") == 0)   { _gamepadMap.btnSelect = atoi(val); }
		else if (strcmp(key, "gamepad_hat_x") == 0)      { _gamepadMap.hatXAxis  = atoi(val); }
		else if (strcmp(key, "gamepad_hat_y") == 0)      { _gamepadMap.hatYAxis  = atoi(val); }
		else if (strcmp(key, "gamepad_stick_lx") == 0)   { _gamepadMap.stickXAxis = atoi(val); }
		else if (strcmp(key, "gamepad_stick_ly") == 0)   { _gamepadMap.stickYAxis = atoi(val); }
		// ScummVM-specific additional gamepad mappings
		else if (strcmp(key, "gamepad_btn_north") == 0)  { _gamepadMap.btnNorth  = atoi(val); }
		else if (strcmp(key, "gamepad_btn_tl") == 0)     { _gamepadMap.btnTL     = atoi(val); }
		else if (strcmp(key, "gamepad_btn_tr") == 0)     { _gamepadMap.btnTR     = atoi(val); }
	}

	fclose(f);
	debug("RoomWizard: loaded input config (mouse sens=%.1f accel=%.1f thresh=%d/%d)",
	      _mouseSensitivity, _mouseAcceleration, _mouseLowThreshold, _mouseHighThreshold);
}

void RoomWizardEventSource::initDefaultGamepadMap() {
	_gamepadMap.btnSouth  = BTN_SOUTH;
	_gamepadMap.btnEast   = BTN_EAST;
	_gamepadMap.btnWest   = BTN_WEST;
	_gamepadMap.btnNorth  = BTN_NORTH;
	_gamepadMap.btnStart  = BTN_START;
	_gamepadMap.btnSelect = BTN_SELECT;
	_gamepadMap.btnTL     = BTN_TL;
	_gamepadMap.btnTR     = BTN_TR;
	_gamepadMap.hatXAxis  = ABS_HAT0X;
	_gamepadMap.hatYAxis  = ABS_HAT0Y;
	_gamepadMap.stickXAxis = ABS_X;
	_gamepadMap.stickYAxis = ABS_Y;
}

// =========================================================================
// Keyboard: Linux KEY_* to ScummVM KEYCODE_* mapping
// =========================================================================

RoomWizardEventSource::KeyMapping RoomWizardEventSource::mapLinuxKey(int linuxKeyCode, byte modifiers) {
	KeyMapping m;
	m.keycode = Common::KEYCODE_INVALID;
	m.ascii = 0;
	m.shiftAscii = 0;

	switch (linuxKeyCode) {
	case 1:  m.keycode = Common::KEYCODE_ESCAPE;    m.ascii = 27;  break;
	case 2:  m.keycode = Common::KEYCODE_1; m.ascii = '1'; m.shiftAscii = '!'; break;
	case 3:  m.keycode = Common::KEYCODE_2; m.ascii = '2'; m.shiftAscii = '@'; break;
	case 4:  m.keycode = Common::KEYCODE_3; m.ascii = '3'; m.shiftAscii = '#'; break;
	case 5:  m.keycode = Common::KEYCODE_4; m.ascii = '4'; m.shiftAscii = '$'; break;
	case 6:  m.keycode = Common::KEYCODE_5; m.ascii = '5'; m.shiftAscii = '%'; break;
	case 7:  m.keycode = Common::KEYCODE_6; m.ascii = '6'; m.shiftAscii = '^'; break;
	case 8:  m.keycode = Common::KEYCODE_7; m.ascii = '7'; m.shiftAscii = '&'; break;
	case 9:  m.keycode = Common::KEYCODE_8; m.ascii = '8'; m.shiftAscii = '*'; break;
	case 10: m.keycode = Common::KEYCODE_9; m.ascii = '9'; m.shiftAscii = '('; break;
	case 11: m.keycode = Common::KEYCODE_0; m.ascii = '0'; m.shiftAscii = ')'; break;
	case 12: m.keycode = Common::KEYCODE_MINUS;  m.ascii = '-'; m.shiftAscii = '_'; break;
	case 13: m.keycode = Common::KEYCODE_EQUALS; m.ascii = '='; m.shiftAscii = '+'; break;
	case 14: m.keycode = Common::KEYCODE_BACKSPACE; m.ascii = 8;  break;
	case 15: m.keycode = Common::KEYCODE_TAB;       m.ascii = 9;  break;
	case 16: m.keycode = Common::KEYCODE_q; m.ascii = 'q'; m.shiftAscii = 'Q'; break;
	case 17: m.keycode = Common::KEYCODE_w; m.ascii = 'w'; m.shiftAscii = 'W'; break;
	case 18: m.keycode = Common::KEYCODE_e; m.ascii = 'e'; m.shiftAscii = 'E'; break;
	case 19: m.keycode = Common::KEYCODE_r; m.ascii = 'r'; m.shiftAscii = 'R'; break;
	case 20: m.keycode = Common::KEYCODE_t; m.ascii = 't'; m.shiftAscii = 'T'; break;
	case 21: m.keycode = Common::KEYCODE_y; m.ascii = 'y'; m.shiftAscii = 'Y'; break;
	case 22: m.keycode = Common::KEYCODE_u; m.ascii = 'u'; m.shiftAscii = 'U'; break;
	case 23: m.keycode = Common::KEYCODE_i; m.ascii = 'i'; m.shiftAscii = 'I'; break;
	case 24: m.keycode = Common::KEYCODE_o; m.ascii = 'o'; m.shiftAscii = 'O'; break;
	case 25: m.keycode = Common::KEYCODE_p; m.ascii = 'p'; m.shiftAscii = 'P'; break;
	case 26: m.keycode = Common::KEYCODE_LEFTBRACKET;  m.ascii = '['; m.shiftAscii = '{'; break;
	case 27: m.keycode = Common::KEYCODE_RIGHTBRACKET; m.ascii = ']'; m.shiftAscii = '}'; break;
	case 28: m.keycode = Common::KEYCODE_RETURN; m.ascii = 13; break;
	case 29: m.keycode = Common::KEYCODE_LCTRL;  break;
	case 30: m.keycode = Common::KEYCODE_a; m.ascii = 'a'; m.shiftAscii = 'A'; break;
	case 31: m.keycode = Common::KEYCODE_s; m.ascii = 's'; m.shiftAscii = 'S'; break;
	case 32: m.keycode = Common::KEYCODE_d; m.ascii = 'd'; m.shiftAscii = 'D'; break;
	case 33: m.keycode = Common::KEYCODE_f; m.ascii = 'f'; m.shiftAscii = 'F'; break;
	case 34: m.keycode = Common::KEYCODE_g; m.ascii = 'g'; m.shiftAscii = 'G'; break;
	case 35: m.keycode = Common::KEYCODE_h; m.ascii = 'h'; m.shiftAscii = 'H'; break;
	case 36: m.keycode = Common::KEYCODE_j; m.ascii = 'j'; m.shiftAscii = 'J'; break;
	case 37: m.keycode = Common::KEYCODE_k; m.ascii = 'k'; m.shiftAscii = 'K'; break;
	case 38: m.keycode = Common::KEYCODE_l; m.ascii = 'l'; m.shiftAscii = 'L'; break;
	case 39: m.keycode = Common::KEYCODE_SEMICOLON; m.ascii = ';'; m.shiftAscii = ':'; break;
	case 40: m.keycode = Common::KEYCODE_QUOTE;     m.ascii = '\''; m.shiftAscii = '"'; break;
	case 41: m.keycode = Common::KEYCODE_BACKQUOTE; m.ascii = '`'; m.shiftAscii = '~'; break;
	case 42: m.keycode = Common::KEYCODE_LSHIFT; break;
	case 43: m.keycode = Common::KEYCODE_BACKSLASH; m.ascii = '\\'; m.shiftAscii = '|'; break;
	case 44: m.keycode = Common::KEYCODE_z; m.ascii = 'z'; m.shiftAscii = 'Z'; break;
	case 45: m.keycode = Common::KEYCODE_x; m.ascii = 'x'; m.shiftAscii = 'X'; break;
	case 46: m.keycode = Common::KEYCODE_c; m.ascii = 'c'; m.shiftAscii = 'C'; break;
	case 47: m.keycode = Common::KEYCODE_v; m.ascii = 'v'; m.shiftAscii = 'V'; break;
	case 48: m.keycode = Common::KEYCODE_b; m.ascii = 'b'; m.shiftAscii = 'B'; break;
	case 49: m.keycode = Common::KEYCODE_n; m.ascii = 'n'; m.shiftAscii = 'N'; break;
	case 50: m.keycode = Common::KEYCODE_m; m.ascii = 'm'; m.shiftAscii = 'M'; break;
	case 51: m.keycode = Common::KEYCODE_COMMA;  m.ascii = ','; m.shiftAscii = '<'; break;
	case 52: m.keycode = Common::KEYCODE_PERIOD; m.ascii = '.'; m.shiftAscii = '>'; break;
	case 53: m.keycode = Common::KEYCODE_SLASH;  m.ascii = '/'; m.shiftAscii = '?'; break;
	case 54: m.keycode = Common::KEYCODE_RSHIFT; break;
	case 56: m.keycode = Common::KEYCODE_LALT; break;
	case 57: m.keycode = Common::KEYCODE_SPACE; m.ascii = ' '; break;
	case 58: m.keycode = Common::KEYCODE_CAPSLOCK; break;
	case 59: m.keycode = Common::KEYCODE_F1;  m.ascii = Common::ASCII_F1;  break;
	case 60: m.keycode = Common::KEYCODE_F2;  m.ascii = Common::ASCII_F2;  break;
	case 61: m.keycode = Common::KEYCODE_F3;  m.ascii = Common::ASCII_F3;  break;
	case 62: m.keycode = Common::KEYCODE_F4;  m.ascii = Common::ASCII_F4;  break;
	case 63: m.keycode = Common::KEYCODE_F5;  m.ascii = Common::ASCII_F5;  break;
	case 64: m.keycode = Common::KEYCODE_F6;  m.ascii = Common::ASCII_F6;  break;
	case 65: m.keycode = Common::KEYCODE_F7;  m.ascii = Common::ASCII_F7;  break;
	case 66: m.keycode = Common::KEYCODE_F8;  m.ascii = Common::ASCII_F8;  break;
	case 67: m.keycode = Common::KEYCODE_F9;  m.ascii = Common::ASCII_F9;  break;
	case 68: m.keycode = Common::KEYCODE_F10; m.ascii = Common::ASCII_F10; break;
	case 69: m.keycode = Common::KEYCODE_NUMLOCK;  break;
	case 70: m.keycode = Common::KEYCODE_SCROLLOCK; break;
	case 71: m.keycode = Common::KEYCODE_KP7; m.ascii = '7'; break;
	case 72: m.keycode = Common::KEYCODE_KP8; m.ascii = '8'; break;
	case 73: m.keycode = Common::KEYCODE_KP9; m.ascii = '9'; break;
	case 74: m.keycode = Common::KEYCODE_KP_MINUS; m.ascii = '-'; break;
	case 75: m.keycode = Common::KEYCODE_KP4; m.ascii = '4'; break;
	case 76: m.keycode = Common::KEYCODE_KP5; m.ascii = '5'; break;
	case 77: m.keycode = Common::KEYCODE_KP6; m.ascii = '6'; break;
	case 78: m.keycode = Common::KEYCODE_KP_PLUS; m.ascii = '+'; break;
	case 79: m.keycode = Common::KEYCODE_KP1; m.ascii = '1'; break;
	case 80: m.keycode = Common::KEYCODE_KP2; m.ascii = '2'; break;
	case 81: m.keycode = Common::KEYCODE_KP3; m.ascii = '3'; break;
	case 82: m.keycode = Common::KEYCODE_KP0; m.ascii = '0'; break;
	case 83: m.keycode = Common::KEYCODE_KP_PERIOD; m.ascii = '.'; break;
	case 87: m.keycode = Common::KEYCODE_F11; m.ascii = Common::ASCII_F11; break;
	case 88: m.keycode = Common::KEYCODE_F12; m.ascii = Common::ASCII_F12; break;
	case 96: m.keycode = Common::KEYCODE_KP_ENTER;  m.ascii = 13;  break;
	case 97: m.keycode = Common::KEYCODE_RCTRL; break;
	case 98: m.keycode = Common::KEYCODE_KP_DIVIDE; m.ascii = '/'; break;
	case 100: m.keycode = Common::KEYCODE_RALT; break;
	case 102: m.keycode = Common::KEYCODE_HOME;     break;
	case 103: m.keycode = Common::KEYCODE_UP;    break;
	case 104: m.keycode = Common::KEYCODE_PAGEUP;   break;
	case 105: m.keycode = Common::KEYCODE_LEFT;  break;
	case 106: m.keycode = Common::KEYCODE_RIGHT; break;
	case 107: m.keycode = Common::KEYCODE_END;      break;
	case 108: m.keycode = Common::KEYCODE_DOWN;  break;
	case 109: m.keycode = Common::KEYCODE_PAGEDOWN; break;
	case 110: m.keycode = Common::KEYCODE_INSERT;   break;
	case 111: m.keycode = Common::KEYCODE_DELETE; m.ascii = 127; break;
	case 125: m.keycode = Common::KEYCODE_LSUPER; break;
	case 126: m.keycode = Common::KEYCODE_RSUPER; break;
	default: break;
	}

	// Apply shift to ascii
	if ((modifiers & Common::KBD_SHIFT) && m.shiftAscii != 0)
		m.ascii = m.shiftAscii;

	return m;
}

// =========================================================================
// Keyboard polling
// =========================================================================

bool RoomWizardEventSource::pollKeyboard(Common::Event &event) {
	if (_keyboardFd < 0) return false;

	// BUG-INPUT-004 FIX: Clear errno before the read loop so stale values
	// (e.g. EAGAIN from a previous poll) don't trigger false disconnect detection.
	errno = 0;

	struct input_event ev;
	while (read(_keyboardFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
		if (ev.type != EV_KEY) continue;

		// value: 0=release, 1=press, 2=auto-repeat (ignored — ScummVM repeats internally)
		if (ev.value == 2) continue;
		bool pressed = (ev.value == 1);

		// Update modifier state
		switch (ev.code) {
		case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
			if (pressed) _modifierFlags |= Common::KBD_SHIFT;
			else         _modifierFlags &= ~Common::KBD_SHIFT;
			break;
		case KEY_LEFTCTRL: case KEY_RIGHTCTRL:
			if (pressed) _modifierFlags |= Common::KBD_CTRL;
			else         _modifierFlags &= ~Common::KBD_CTRL;
			break;
		case KEY_LEFTALT: case KEY_RIGHTALT:
			if (pressed) _modifierFlags |= Common::KBD_ALT;
			else         _modifierFlags &= ~Common::KBD_ALT;
			break;
		default: break;
		}

		KeyMapping km = mapLinuxKey(ev.code, _modifierFlags);
		if (km.keycode == Common::KEYCODE_INVALID) continue;

		event.type = pressed ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
		event.kbdRepeat = false;
		event.kbd.keycode = km.keycode;
		event.kbd.ascii = km.ascii;
		event.kbd.flags = _modifierFlags;
		return true;
	}

	// Check for disconnect
	if (errno == ENODEV || errno == EBADF) {
		debug("RoomWizard: keyboard disconnected");
		close(_keyboardFd);
		_keyboardFd = -1;
		_modifierFlags = 0;
	}
	return false;
}

// =========================================================================
// Mouse polling with 3-tier acceleration
// =========================================================================

bool RoomWizardEventSource::pollMouse(Common::Event &event) {
	if (_mouseFd < 0) return false;

	// BUG-INPUT-004 FIX: Clear errno before read loop — stale errno from
	// prior system calls could cause false disconnect detection below.
	errno = 0;

	struct input_event ev;
	int accumDx = 0, accumDy = 0;
	int buttonChanges = 0;
	int buttonState = _prevMouseButtons;

	while (read(_mouseFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
		if (ev.type == EV_REL) {
			if (ev.code == REL_X) accumDx += ev.value;
			else if (ev.code == REL_Y) accumDy += ev.value;
		} else if (ev.type == EV_KEY) {
			int bit = -1;
			if (ev.code == BTN_LEFT)   bit = 0;
			if (ev.code == BTN_RIGHT)  bit = 1;
			if (ev.code == BTN_MIDDLE) bit = 2;
			if (bit >= 0) {
				if (ev.value) buttonState |=  (1 << bit);
				else          buttonState &= ~(1 << bit);
				buttonChanges |= (1 << bit);
			}
		}
	}

	if (errno == ENODEV || errno == EBADF) {
		debug("RoomWizard: mouse disconnected");
		close(_mouseFd);
		_mouseFd = -1;
		return false;
	}

	// BUG-INPUT-004 FIX: Helper to flush accumulated mouse movement into the
	// pending event queue before returning a button event.  Without this,
	// movement accumulated during the same read batch as a button change would
	// be lost because the local accumDx/accumDy variables are discarded on return.
	auto flushMouseMovement = [&]() {
		if (accumDx == 0 && accumDy == 0) return;
		float speed = sqrtf((float)(accumDx * accumDx + accumDy * accumDy));
		float multiplier;
		if (speed < (float)_mouseLowThreshold)
			multiplier = 1.0f;
		else if (speed < (float)_mouseHighThreshold)
			multiplier = _mouseSensitivity;
		else
			multiplier = _mouseSensitivity * _mouseAcceleration;
		int mx = _mouseX + (int)(accumDx * multiplier);
		int my = _mouseY + (int)(accumDy * multiplier);
		if (mx < 0) mx = 0;
		if (mx >= 800) mx = 799;
		if (my < 0) my = 0;
		if (my >= 480) my = 479;
		_mouseX = mx;
		_mouseY = my;
		int gmx, gmy;
		transformCoordinates(_mouseX, _mouseY, gmx, gmy);
		Common::Event moveEv;
		moveEv.type = Common::EVENT_MOUSEMOVE;
		moveEv.mouse.x = gmx;
		moveEv.mouse.y = gmy;
		pushEvent(moveEv);
		accumDx = 0;
		accumDy = 0;
	};

	// Button changes have priority
	for (int bit = 0; bit < 3; bit++) {
		if (!(buttonChanges & (1 << bit))) continue;

		bool nowDown = (buttonState & (1 << bit)) != 0;
		bool wasDown = (_prevMouseButtons & (1 << bit)) != 0;
		if (nowDown == wasDown) continue;

		if (bit == 2) {
			// Middle click -> Ctrl+F5 (GMM)
			if (nowDown) {
				Common::Event ctrlDown, f5Down, f5Up, ctrlUp;
				ctrlDown.type        = Common::EVENT_KEYDOWN;
				ctrlDown.kbd.keycode = Common::KEYCODE_LCTRL;
				ctrlDown.kbd.flags   = Common::KBD_CTRL;
				ctrlDown.kbd.ascii   = 0;
				f5Down.type          = Common::EVENT_KEYDOWN;
				f5Down.kbd.keycode   = Common::KEYCODE_F5;
				f5Down.kbd.flags     = Common::KBD_CTRL;
				f5Down.kbd.ascii     = 0;
				f5Up = f5Down;
				f5Up.type            = Common::EVENT_KEYUP;
				ctrlUp = ctrlDown;
				ctrlUp.type          = Common::EVENT_KEYUP;
				ctrlUp.kbd.flags     = 0;
				pushEvent(ctrlDown);
				pushEvent(f5Down);
				pushEvent(f5Up);
				pushEvent(ctrlUp);
			}
			// BUG-INPUT-004 FIX: Only update bit 2 so other button changes
			// in the same batch are not masked.
			if (nowDown) _prevMouseButtons |= (1 << 2);
			else         _prevMouseButtons &= ~(1 << 2);
			continue;
		}

		int gameX, gameY;
		transformCoordinates(_mouseX, _mouseY, gameX, gameY);

		if (bit == 0)
			event.type = nowDown ? Common::EVENT_LBUTTONDOWN : Common::EVENT_LBUTTONUP;
		else
			event.type = nowDown ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP;

		event.mouse.x = gameX;
		event.mouse.y = gameY;
		// BUG-INPUT-004 FIX: Only update the specific bit so other button
		// changes in the same batch are not masked.
		if (nowDown) _prevMouseButtons |= (1 << bit);
		else         _prevMouseButtons &= ~(1 << bit);
		// BUG-INPUT-004 FIX: Flush any accumulated movement before returning
		// the button event, so movement is not lost.
		flushMouseMovement();
		return true;
	}
	_prevMouseButtons = buttonState;

	// Movement with 3-tier acceleration (same model as gamepad.c)
	if (accumDx != 0 || accumDy != 0) {
		float speed = sqrtf((float)(accumDx * accumDx + accumDy * accumDy));
		float multiplier;

		if (speed < (float)_mouseLowThreshold)
			multiplier = 1.0f;
		else if (speed < (float)_mouseHighThreshold)
			multiplier = _mouseSensitivity;
		else
			multiplier = _mouseSensitivity * _mouseAcceleration;

		_mouseX += (int)(accumDx * multiplier);
		_mouseY += (int)(accumDy * multiplier);

		if (_mouseX < 0) _mouseX = 0;
		if (_mouseY < 0) _mouseY = 0;
		if (_mouseX >= 800) _mouseX = 799;
		if (_mouseY >= 480) _mouseY = 479;

		int gameX, gameY;
		transformCoordinates(_mouseX, _mouseY, gameX, gameY);
		event.type = Common::EVENT_MOUSEMOVE;
		event.mouse.x = gameX;
		event.mouse.y = gameY;
		return true;
	}

	return false;
}

// =========================================================================
// Gamepad polling
// =========================================================================

bool RoomWizardEventSource::pollGamepad(Common::Event &event) {
	if (_gamepadFd < 0) return false;

	// BUG-INPUT-004 FIX: Clear errno before read loop — stale errno from
	// prior system calls could cause false disconnect detection below.
	errno = 0;

	struct input_event ev;
	int buttonChanges = 0;
	int buttonState = _prevGamepadButtons;
	// Bit assignments: 0=South(A), 1=East(B), 2=West(X), 3=North(Y),
	//                  4=Start, 5=Select, 6=TL, 7=TR

	while (read(_gamepadFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
		if (ev.type == EV_ABS) {
			if (ev.code == _gamepadMap.stickXAxis)      _gamepadAxisX = ev.value;
			else if (ev.code == _gamepadMap.stickYAxis)  _gamepadAxisY = ev.value;
			else if (ev.code == _gamepadMap.hatXAxis)    _gamepadHatX  = ev.value;
			else if (ev.code == _gamepadMap.hatYAxis)    _gamepadHatY  = ev.value;
		} else if (ev.type == EV_KEY) {
			int bit = -1;
			if ((int)ev.code == _gamepadMap.btnSouth)  bit = 0;
			if ((int)ev.code == _gamepadMap.btnEast)   bit = 1;
			if ((int)ev.code == _gamepadMap.btnWest)   bit = 2;
			if ((int)ev.code == _gamepadMap.btnNorth)  bit = 3;
			if ((int)ev.code == _gamepadMap.btnStart)  bit = 4;
			if ((int)ev.code == _gamepadMap.btnSelect) bit = 5;
			if ((int)ev.code == _gamepadMap.btnTL)     bit = 6;
			if ((int)ev.code == _gamepadMap.btnTR)     bit = 7;

			if (bit >= 0) {
				if (ev.value) buttonState |=  (1 << bit);
				else          buttonState &= ~(1 << bit);
				buttonChanges |= (1 << bit);
			}
		}
	}

	if (errno == ENODEV || errno == EBADF) {
		debug("RoomWizard: gamepad disconnected");
		close(_gamepadFd);
		_gamepadFd = -1;
		return false;
	}

	// Process button changes
	for (int bit = 0; bit < 8; bit++) {
		if (!(buttonChanges & (1 << bit))) continue;
		bool nowDown = (buttonState & (1 << bit)) != 0;
		bool wasDown = (_prevGamepadButtons & (1 << bit)) != 0;
		if (nowDown == wasDown) continue;

		// BUG-INPUT-004 FIX: Only update the specific bit so other button
		// changes in the same batch are not masked.
		if (nowDown) _prevGamepadButtons |= (1 << bit);
		else         _prevGamepadButtons &= ~(1 << bit);

		switch (bit) {
		case 0: { // South (A) -> left click
			int gx, gy;
			transformCoordinates(_mouseX, _mouseY, gx, gy);
			event.type = nowDown ? Common::EVENT_LBUTTONDOWN : Common::EVENT_LBUTTONUP;
			event.mouse.x = gx;
			event.mouse.y = gy;
			return true;
		}
		case 1: { // East (B) -> right click
			int gx, gy;
			transformCoordinates(_mouseX, _mouseY, gx, gy);
			event.type = nowDown ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP;
			event.mouse.x = gx;
			event.mouse.y = gy;
			return true;
		}
		case 2: // West (X) -> Space (skip cutscene)
			event.type = nowDown ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
			event.kbdRepeat = false;
			event.kbd.keycode = Common::KEYCODE_SPACE;
			event.kbd.ascii = ' ';
			event.kbd.flags = 0;
			return true;
		case 3: // North (Y) -> Escape (skip dialog)
			event.type = nowDown ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
			event.kbdRepeat = false;
			event.kbd.keycode = Common::KEYCODE_ESCAPE;
			event.kbd.ascii = 27;
			event.kbd.flags = 0;
			return true;
		case 4: // Start -> Ctrl+F5 (GMM)
			if (nowDown) {
				Common::Event ctrlDown, f5Down, f5Up, ctrlUp;
				ctrlDown.type        = Common::EVENT_KEYDOWN;
				ctrlDown.kbd.keycode = Common::KEYCODE_LCTRL;
				ctrlDown.kbd.flags   = Common::KBD_CTRL;
				ctrlDown.kbd.ascii   = 0;
				f5Down.type          = Common::EVENT_KEYDOWN;
				f5Down.kbd.keycode   = Common::KEYCODE_F5;
				f5Down.kbd.flags     = Common::KBD_CTRL;
				f5Down.kbd.ascii     = 0;
				f5Up = f5Down;
				f5Up.type            = Common::EVENT_KEYUP;
				ctrlUp = ctrlDown;
				ctrlUp.type          = Common::EVENT_KEYUP;
				ctrlUp.kbd.flags     = 0;
				pushEvent(f5Down);
				pushEvent(f5Up);
				pushEvent(ctrlUp);
				event = ctrlDown;
				return true;
			}
			break;
		case 5: // Select -> F5 (save/load)
			event.type = nowDown ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
			event.kbdRepeat = false;
			event.kbd.keycode = Common::KEYCODE_F5;
			event.kbd.ascii = Common::ASCII_F5;
			event.kbd.flags = 0;
			return true;
		case 6: // Left bumper -> Period (skip text)
			event.type = nowDown ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
			event.kbdRepeat = false;
			event.kbd.keycode = Common::KEYCODE_PERIOD;
			event.kbd.ascii = '.';
			event.kbd.flags = 0;
			return true;
		case 7: // Right bumper -> Escape
			event.type = nowDown ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
			event.kbdRepeat = false;
			event.kbd.keycode = Common::KEYCODE_ESCAPE;
			event.kbd.ascii = 27;
			event.kbd.flags = 0;
			return true;
		}
	}
	_prevGamepadButtons = buttonState;

	// Cursor movement from analog stick + D-pad
	uint32 now = 0;
	if (g_system) now = g_system->getMillis();
	if (now - _lastCursorMove < GAMEPAD_CURSOR_INTERVAL)
		return false;

	int moveX = 0, moveY = 0;

	// Analog stick
	{
		int halfRange = (_gamepadAxisMax - _gamepadAxisMin) / 2;
		if (halfRange > 0) {
			int offX = _gamepadAxisX - _gamepadAxisCenter;
			int offY = _gamepadAxisY - _gamepadAxisCenter;
			int dz = (halfRange * GAMEPAD_DEADZONE_PCT) / 100;

			if (offX > -dz && offX < dz) offX = 0;
			if (offY > -dz && offY < dz) offY = 0;

			int effRange = halfRange - dz;
			if (effRange > 0) {
				if (offX > 0) offX -= dz; else if (offX < 0) offX += dz;
				if (offY > 0) offY -= dz; else if (offY < 0) offY += dz;
				moveX = (offX * GAMEPAD_CURSOR_SPEED) / effRange;
				moveY = (offY * GAMEPAD_CURSOR_SPEED) / effRange;
			}
		}
	}

	// D-pad
	if (_gamepadHatX < 0) moveX -= GAMEPAD_CURSOR_SPEED;
	if (_gamepadHatX > 0) moveX += GAMEPAD_CURSOR_SPEED;
	if (_gamepadHatY < 0) moveY -= GAMEPAD_CURSOR_SPEED;
	if (_gamepadHatY > 0) moveY += GAMEPAD_CURSOR_SPEED;

	// Clamp
	if (moveX < -GAMEPAD_CURSOR_SPEED) moveX = -GAMEPAD_CURSOR_SPEED;
	if (moveX >  GAMEPAD_CURSOR_SPEED) moveX =  GAMEPAD_CURSOR_SPEED;
	if (moveY < -GAMEPAD_CURSOR_SPEED) moveY = -GAMEPAD_CURSOR_SPEED;
	if (moveY >  GAMEPAD_CURSOR_SPEED) moveY =  GAMEPAD_CURSOR_SPEED;

	if (moveX != 0 || moveY != 0) {
		_lastCursorMove = now;
		_mouseX += moveX;
		_mouseY += moveY;

		if (_mouseX < 0) _mouseX = 0;
		if (_mouseY < 0) _mouseY = 0;
		if (_mouseX >= 800) _mouseX = 799;
		if (_mouseY >= 480) _mouseY = 479;

		int gx, gy;
		transformCoordinates(_mouseX, _mouseY, gx, gy);
		event.type = Common::EVENT_MOUSEMOVE;
		event.mouse.x = gx;
		event.mouse.y = gy;
		return true;
	}

	return false;
}

// =========================================================================
// Touch polling — extracted from original pollEvent() without behavior changes
// =========================================================================

bool RoomWizardEventSource::pollTouch(Common::Event &event) {
	if (!_touchInitialized || !_touchInput)
		return false;

	// Poll touch input (non-blocking)
	touch_poll(_touchInput);
	TouchState state = touch_get_state(_touchInput);

	// After gesture or context switch, wait for finger lift
	if (_waitForRelease) {
		if (state.released || (!state.pressed && !state.held)) {
			_waitForRelease = false;
			_touchPhase = TOUCH_NONE;
			debug("RoomWizard: touch lockout cleared, input re-enabled");
		}
		return false;
	}

	// Detect overlay visibility transition
	{
		OSystem_RoomWizard *sysChk = rwSystem();
		bool overlayNow = (sysChk && sysChk->getGraphicsManager() &&
			((RoomWizardGraphicsManager *)sysChk->getGraphicsManager())->isOverlayVisible());
		if (overlayNow != _prevOverlayVisible) {
			_prevOverlayVisible = overlayNow;
			debug("RoomWizard: overlay transition -> %s", overlayNow ? "shown" : "hidden");
			if (_touchPhase == TOUCH_HELD) {
				_touchPhase = TOUCH_NONE;
				_waitForRelease = true;
				int gameX, gameY;
				transformCoordinates(_lastTouchX, _lastTouchY, gameX, gameY);
				event.type = Common::EVENT_LBUTTONUP;
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				debug("RoomWizard: overlay transition LBUTTONUP injected at (%d,%d)", gameX, gameY);
				return true;
			} else if (_touchPhase == TOUCH_PRESSED) {
				_touchPhase = TOUCH_NONE;
				_waitForRelease = true;
			}
		}
	}

	uint32 currentTime = g_system->getMillis();

	// Debug logging
	if (rwDebugMode()) {
		static bool lastPressed = false;
		static bool lastHeld = false;
		static bool lastReleased = false;
		if (state.pressed != lastPressed || state.held != lastHeld || state.released != lastReleased) {
			debug("Touch state: pressed=%d held=%d released=%d phase=%d",
			      state.pressed, state.held, state.released, _touchPhase);
			lastPressed = state.pressed;
			lastHeld = state.held;
			lastReleased = state.released;
		}
	}

	switch (_touchPhase) {
		case TOUCH_NONE:
			if (state.pressed || state.held) {
				_lastTouchX = state.x;
				_lastTouchY = state.y;
				_touchStartTime = currentTime;
				_touchPhase = TOUCH_PRESSED;
				_buttonDownSent = false;
				_longPressFired = false;

				checkGestures(state.x, state.y, currentTime);
				if (_pendingCount > 0) {
					event = _pending[_pendingHead];
					_pendingHead = (_pendingHead + 1) % MAX_PENDING;
					_pendingCount--;
					_touchPhase = TOUCH_NONE;
					return true;
				}

				if (cornerFor(state.x, state.y) != CORNER_COUNT) {
					_waitForRelease = true;
					_touchPhase = TOUCH_NONE;
					return false;
				}

				if (rwDebugMode()) {
					debug("TOUCH_NONE -> TOUCH_PRESSED: sending MOUSEMOVE at (%d,%d)", state.x, state.y);
					OSystem_RoomWizard *system = rwSystem();
					if (system && system->getGraphicsManager())
						((RoomWizardGraphicsManager *)system->getGraphicsManager())->addTouchPoint(state.x, state.y);
				}

				int gameX, gameY;
				transformCoordinates(state.x, state.y, gameX, gameY);
				event.type = Common::EVENT_MOUSEMOVE;
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				return true;
			}
			break;

		case TOUCH_PRESSED:
			if (!_buttonDownSent && state.held) {
				_buttonDownSent = true;
				_touchPhase = TOUCH_HELD;

				int gameX, gameY;
				transformCoordinates(_lastTouchX, _lastTouchY, gameX, gameY);
				debug("TOUCH_PRESSED -> TOUCH_HELD: sending LBUTTONDOWN at (%d,%d)", gameX, gameY);
				event.type = Common::EVENT_LBUTTONDOWN;
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				return true;
			}
			if (!_buttonDownSent && (state.released || !state.held)) {
				int gameX, gameY;
				transformCoordinates(_lastTouchX, _lastTouchY, gameX, gameY);
				debug("TOUCH_PRESSED: quick tap at (%d,%d) - sending LBUTTONDOWN+UP", gameX, gameY);

				Common::Event upEvent;
				upEvent.type = Common::EVENT_LBUTTONUP;
				upEvent.mouse.x = gameX;
				upEvent.mouse.y = gameY;
				pushEvent(upEvent);

				_buttonDownSent = true;
				_touchPhase = TOUCH_NONE;
				event.type = Common::EVENT_LBUTTONDOWN;
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				return true;
			}
			break;

		case TOUCH_HELD:
			if (state.released || !state.held) {
				_touchPhase = TOUCH_NONE;
				int gameX, gameY;
				transformCoordinates(_lastTouchX, _lastTouchY, gameX, gameY);

				if (_longPressFired) {
					debug("TOUCH_HELD -> TOUCH_NONE: sending RBUTTONUP (long-press release)");
					event.type = Common::EVENT_RBUTTONUP;
				} else {
					debug("TOUCH_HELD -> TOUCH_NONE: sending LBUTTONUP (duration=%dms)",
					      currentTime - _touchStartTime);
					event.type = Common::EVENT_LBUTTONUP;
				}

				event.mouse.x = gameX;
				event.mouse.y = gameY;
				return true;
			}

			if (!_longPressFired && (currentTime - _touchStartTime) >= LONG_PRESS_TIME) {
				_longPressFired = true;
				int gameX, gameY;
				transformCoordinates(_lastTouchX, _lastTouchY, gameX, gameY);
				debug("TOUCH_HELD: long-press at (%d,%d) - LBUTTONUP + queue RBUTTONDOWN", gameX, gameY);

				Common::Event rbDown;
				rbDown.type = Common::EVENT_RBUTTONDOWN;
				rbDown.mouse.x = gameX;
				rbDown.mouse.y = gameY;
				pushEvent(rbDown);

				event.type = Common::EVENT_LBUTTONUP;
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				return true;
			}

			if (state.held && (state.x != _lastTouchX || state.y != _lastTouchY)) {
				_lastTouchX = state.x;
				_lastTouchY = state.y;
				int gameX, gameY;
				transformCoordinates(state.x, state.y, gameX, gameY);
				event.type = Common::EVENT_MOUSEMOVE;
				event.mouse.x = gameX;
				event.mouse.y = gameY;
				return true;
			}
			break;
	}

	return false;
}

// =========================================================================
// Main pollEvent — orchestrates all input sources
// =========================================================================

bool RoomWizardEventSource::pollEvent(Common::Event &event) {
	// Pump timer callbacks (OPL sequencer, iMUSE, etc.) on every event poll.
	// checkTimers is gated to fire at most every 10 ms, so calling it here
	// (potentially hundreds of times/sec) is safe and low-overhead.
	if (g_system) {
		DefaultTimerManager *tm =
		    static_cast<DefaultTimerManager *>(g_system->getTimerManager());
		if (tm) tm->checkTimers(10);
	}

	// Drain any synthetic events queued by gesture detection or multi-event input
	if (_pendingCount > 0) {
		// While draining, keep polling touch hardware so we can detect release
		if (_touchInitialized && _touchInput)
			touch_poll(_touchInput);
		event = _pending[_pendingHead];
		_pendingHead = (_pendingHead + 1) % MAX_PENDING;
		_pendingCount--;
		return true;
	}

	// Periodic device rescan (every 5 seconds) to detect hotplug
	if (g_system) {
		uint32 now = g_system->getMillis();
		if (now - _lastDeviceScan > DEVICE_SCAN_INTERVAL) {
			_lastDeviceScan = now;
			// Only rescan for devices that aren't currently open
			if (_keyboardFd < 0 || _mouseFd < 0 || _gamepadFd < 0)
				scanInputDevices();
		}
	}

	// Poll all input sources — return first event found
	if (pollKeyboard(event)) return true;
	if (pollMouse(event))    return true;
	if (pollGamepad(event))  return true;

	// Existing touch handling (moved to pollTouch without behavior changes)
	return pollTouch(event);
}
