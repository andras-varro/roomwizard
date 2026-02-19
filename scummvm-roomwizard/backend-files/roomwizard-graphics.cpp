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

// Forbidden symbol exceptions for file I/O used in loadBezelMargins()
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_fopen
#define FORBIDDEN_SYMBOL_EXCEPTION_fclose
#define FORBIDDEN_SYMBOL_EXCEPTION_fgets
#define FORBIDDEN_SYMBOL_EXCEPTION_sscanf
#define FORBIDDEN_SYMBOL_EXCEPTION_fprintf

#include "backends/platform/roomwizard/roomwizard-graphics.h"
#include "backends/platform/roomwizard/roomwizard.h"
#include "backends/platform/roomwizard/roomwizard-events.h"
#include "common/rect.h"
#include "common/textconsole.h"
#include "common/system.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

RoomWizardGraphicsManager::RoomWizardGraphicsManager()
	: _fb(nullptr),
	  _fbInitialized(false),
	  _screenWidth(0),
	  _screenHeight(0),
	  _screenFormat(Graphics::PixelFormat::createFormatCLUT8()),
	  _screenDirty(false),
	  _paletteDirty(false),
	  _cursorX(0),
	  _cursorY(0),
	  _cursorHotspotX(0),
	  _cursorHotspotY(0),
	  _cursorKeyColor(0),
	  _cursorVisible(false),
	  _cursorDontScale(false),
	  _cursorPaletteEnabled(false),
	  _overlayVisible(false),
	  _shakeXOffset(0),
	  _shakeYOffset(0),
	  _bezelTop(0),
	  _bezelBottom(0),
	  _bezelLeft(0),
	  _bezelRight(0),
	  _touchPointIndex(0) {
	
	memset(_palette, 0, sizeof(_palette));
	memset(_cursorPalette, 0, sizeof(_cursorPalette));
	memset(_touchPoints, 0, sizeof(_touchPoints));
}

RoomWizardGraphicsManager::~RoomWizardGraphicsManager() {
	closeFramebuffer();
	_gameSurface.free();
	_cursorSurface.free();
	_overlaySurface.free();
}

void RoomWizardGraphicsManager::initFramebuffer() {
	if (_fbInitialized)
		return;

	_fb = (Framebuffer *)malloc(sizeof(Framebuffer));
	if (!_fb) {
		warning("Failed to allocate framebuffer structure");
		return;
	}

	if (fb_init(_fb, "/dev/fb0") < 0) {
		warning("Failed to initialize framebuffer - device may not be available");
		free(_fb);
		_fb = nullptr;
		return;
	}

	_fbInitialized = true;
	debug("RoomWizard framebuffer initialized: %dx%d", _fb->width, _fb->height);
	
	// Load bezel calibration (best-effort; zero margins if not present)
	loadBezelMargins();
}

void RoomWizardGraphicsManager::closeFramebuffer() {
	if (_fbInitialized && _fb) {
		fb_close(_fb);
		free(_fb);
		_fb = nullptr;
		_fbInitialized = false;
	}
}

void RoomWizardGraphicsManager::loadBezelMargins() {
	// Parse /etc/touch_calibration.conf for bezel margins.
	// File format (lines starting with # are comments):
	//   Line 1: tl_x tl_y tr_x tr_y bl_x bl_y br_x br_y   (touch offsets)
	//   Line 2: bezel_top bezel_bottom bezel_left bezel_right
	_bezelTop = _bezelBottom = _bezelLeft = _bezelRight = 0;

	FILE *f = fopen("/etc/touch_calibration.conf", "r");
	if (!f) {
		debug("RoomWizard: no calibration file - using zero bezel margins");
		return;
	}

	char line[256];
	int dataLines = 0;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		dataLines++;
		if (dataLines == 2) {
			// Second data line contains bezel margins
			int t, b, l, r;
			if (sscanf(line, "%d %d %d %d", &t, &b, &l, &r) == 4) {
				_bezelTop    = t;
				_bezelBottom = b;
				_bezelLeft   = l;
				_bezelRight  = r;
				debug("RoomWizard: bezel margins loaded T=%d B=%d L=%d R=%d",
				      _bezelTop, _bezelBottom, _bezelLeft, _bezelRight);
			}
			break;
		}
	}
	fclose(f);
}

void RoomWizardGraphicsManager::getScalingInfo(int &scaledWidth, int &scaledHeight,
                                                int &offsetX, int &offsetY) const {
	// The safe display area is the framebuffer minus bezel obstructions.
	// We scale the game to fill this area preserving aspect ratio.
	int safeW = 800 - _bezelLeft - _bezelRight;
	int safeH = 480 - _bezelTop  - _bezelBottom;

	if (_screenWidth == 0 || _screenHeight == 0) {
		scaledWidth  = safeW;
		scaledHeight = safeH;
		offsetX = _bezelLeft;
		offsetY = _bezelTop;
		return;
	}

	// Scale to fill safe area, maintaining aspect ratio
	int scaleX = safeW * 256 / _screenWidth;   // fixed-point *256
	int scaleY = safeH * 256 / _screenHeight;
	int scale  = (scaleX < scaleY) ? scaleX : scaleY;  // use smaller to fit

	scaledWidth  = (_screenWidth  * scale) / 256;
	scaledHeight = (_screenHeight * scale) / 256;

	// Center within the safe area, then offset by bezel
	offsetX = _bezelLeft + (safeW - scaledWidth)  / 2;
	offsetY = _bezelTop  + (safeH - scaledHeight) / 2;
}

bool RoomWizardGraphicsManager::hasFeature(OSystem::Feature f) const {
	return (f == OSystem::kFeatureCursorPalette);
}

void RoomWizardGraphicsManager::setFeatureState(OSystem::Feature f, bool enable) {
	// No dynamic features to enable/disable
}

bool RoomWizardGraphicsManager::getFeatureState(OSystem::Feature f) const {
	return false;
}

#ifdef USE_RGB_COLOR
Graphics::PixelFormat RoomWizardGraphicsManager::getScreenFormat() const {
	return _screenFormat;
}

Common::List<Graphics::PixelFormat> RoomWizardGraphicsManager::getSupportedFormats() const {
	Common::List<Graphics::PixelFormat> list;
	// RoomWizard framebuffer is 32-bit ARGB
	list.push_back(Graphics::PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24)); // ARGB8888
	list.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));  // RGB565
	list.push_back(Graphics::PixelFormat::createFormatCLUT8());          // CLUT8
	return list;
}
#endif

void RoomWizardGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	debug("initSize: %dx%d", width, height);
	
	initFramebuffer();
	
	_screenWidth = width;
	_screenHeight = height;
	
	if (format) {
		_screenFormat = *format;
	} else {
		_screenFormat = Graphics::PixelFormat::createFormatCLUT8();
	}

	// Allocate game surface
	_gameSurface.create(width, height, _screenFormat);
	
	// Allocate overlay surface (same size as framebuffer for GUI)
	_overlaySurface.create(800, 480, Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
	// Pre-fill with transparent key so any stale pixels stay transparent
	{ uint16 *p = (uint16 *)_overlaySurface.getPixels(); for (int i = 0; i < 800*480; i++) p[i] = 0xF81F; }
	
	// Notify event source of new game resolution (needed for coordinate transform)
	{
		int scaledW, scaledH, offX, offY;
		getScalingInfo(scaledW, scaledH, offX, offY);
		OSystem_RoomWizard *system = dynamic_cast<OSystem_RoomWizard *>(g_system);
		if (system && system->getEventSource()) {
			system->getEventSource()->setGameScreenSize(width, height, offX, offY);
		}
	}

	_screenDirty = true;
}

int RoomWizardGraphicsManager::getScreenChangeID() const {
	return 0;
}

void RoomWizardGraphicsManager::beginGFXTransaction() {
	// No transaction support needed
}

OSystem::TransactionError RoomWizardGraphicsManager::endGFXTransaction() {
	return OSystem::kTransactionSuccess;
}

int16 RoomWizardGraphicsManager::getHeight() const {
	return _screenHeight;
}

int16 RoomWizardGraphicsManager::getWidth() const {
	return _screenWidth;
}

void RoomWizardGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	if (start + num > 256) {
		warning("setPalette: Invalid palette range");
		return;
	}

	memcpy(&_palette[start * 3], colors, num * 3);
	_paletteDirty = true;
	_screenDirty = true;
}

void RoomWizardGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	if (start + num > 256) {
		warning("grabPalette: Invalid palette range");
		return;
	}

	memcpy(colors, &_palette[start * 3], num * 3);
}

uint32 RoomWizardGraphicsManager::convertColor(uint32 color, const Graphics::PixelFormat &srcFormat) {
	if (srcFormat.bytesPerPixel == 1) {
		// CLUT8 - use palette
		byte r = _palette[color * 3 + 0];
		byte g = _palette[color * 3 + 1];
		byte b = _palette[color * 3 + 2];
		return (0xFF << 24) | (r << 16) | (g << 8) | b;
	} else {
		// Convert from source format to ARGB8888
		byte r, g, b, a;
		srcFormat.colorToARGB(color, a, r, g, b);
		return (a << 24) | (r << 16) | (g << 8) | b;
	}
}

void RoomWizardGraphicsManager::copyRectToSurface(Graphics::Surface &dst, const void *buf, 
                                                   int pitch, int x, int y, int w, int h,
                                                   const Graphics::PixelFormat &srcFormat) {
	if (x < 0 || y < 0 || x + w > dst.w || y + h > dst.h) {
		warning("copyRectToSurface: Rectangle out of bounds");
		return;
	}

	const byte *src = (const byte *)buf;
	
	if (srcFormat == dst.format) {
		// Direct copy
		for (int row = 0; row < h; row++) {
			memcpy((byte *)dst.getBasePtr(x, y + row), src + row * pitch, w * dst.format.bytesPerPixel);
		}
	} else {
		// Format conversion needed
		for (int row = 0; row < h; row++) {
			for (int col = 0; col < w; col++) {
				uint32 srcColor;
				if (srcFormat.bytesPerPixel == 1) {
					srcColor = src[row * pitch + col];
				} else if (srcFormat.bytesPerPixel == 2) {
					srcColor = *(const uint16 *)(src + row * pitch + col * 2);
				} else {
					srcColor = *(const uint32 *)(src + row * pitch + col * 4);
				}
				
				uint32 dstColor = convertColor(srcColor, srcFormat);
				
				if (dst.format.bytesPerPixel == 4) {
					*(uint32 *)dst.getBasePtr(x + col, y + row) = dstColor;
				} else if (dst.format.bytesPerPixel == 2) {
					byte r = (dstColor >> 16) & 0xFF;
					byte g = (dstColor >> 8) & 0xFF;
					byte b = dstColor & 0xFF;
					*(uint16 *)dst.getBasePtr(x + col, y + row) = 
						((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
				}
			}
		}
	}
}

void RoomWizardGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	if (!_gameSurface.getPixels()) {
		warning("copyRectToScreen: Game surface not initialized");
		return;
	}

	copyRectToSurface(_gameSurface, buf, pitch, x, y, w, h, _screenFormat);
	_screenDirty = true;
}

Graphics::Surface *RoomWizardGraphicsManager::lockScreen() {
	return &_gameSurface;
}

void RoomWizardGraphicsManager::unlockScreen() {
	_screenDirty = true;
}

void RoomWizardGraphicsManager::fillScreen(uint32 col) {
	if (!_gameSurface.getPixels())
		return;

	uint32 color = convertColor(col, _screenFormat);
	
	if (_gameSurface.format.bytesPerPixel == 4) {
		uint32 *pixels = (uint32 *)_gameSurface.getPixels();
		for (int i = 0; i < _gameSurface.w * _gameSurface.h; i++) {
			pixels[i] = color;
		}
	} else if (_gameSurface.format.bytesPerPixel == 2) {
		uint16 *pixels = (uint16 *)_gameSurface.getPixels();
		uint16 color16 = (uint16)color;
		for (int i = 0; i < _gameSurface.w * _gameSurface.h; i++) {
			pixels[i] = color16;
		}
	} else {
		byte *pixels = (byte *)_gameSurface.getPixels();
		for (int i = 0; i < _gameSurface.w * _gameSurface.h; i++) {
			pixels[i] = (byte)col;
		}
	}
	
	_screenDirty = true;
}

void RoomWizardGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	if (!_gameSurface.getPixels())
		return;

	uint32 color = convertColor(col, _screenFormat);
	
	for (int y = r.top; y < r.bottom; y++) {
		if (_gameSurface.format.bytesPerPixel == 4) {
			uint32 *row = (uint32 *)_gameSurface.getBasePtr(r.left, y);
			for (int x = 0; x < r.width(); x++) {
				row[x] = color;
			}
		} else if (_gameSurface.format.bytesPerPixel == 2) {
			uint16 *row = (uint16 *)_gameSurface.getBasePtr(r.left, y);
			uint16 color16 = (uint16)color;
			for (int x = 0; x < r.width(); x++) {
				row[x] = color16;
			}
		} else {
			byte *row = (byte *)_gameSurface.getBasePtr(r.left, y);
			for (int x = 0; x < r.width(); x++) {
				row[x] = (byte)col;
			}
		}
	}
	
	_screenDirty = true;
}

void RoomWizardGraphicsManager::blitGameSurfaceToFramebuffer() {
	if (!_fbInitialized || !_fb || !_gameSurface.getPixels())
		return;

	// Compute bezel-aware scaled region
	int scaledW, scaledH, offsetX, offsetY;
	getScalingInfo(scaledW, scaledH, offsetX, offsetY);

	// Apply shake offset
	offsetX += _shakeXOffset;
	offsetY += _shakeYOffset;

	// Clear framebuffer to black
	fb_clear(_fb, 0x000000);

	// Scale game surface → framebuffer using nearest-neighbour
	for (int dy = 0; dy < scaledH; dy++) {
		int fbY = offsetY + dy;
		if (fbY < 0 || fbY >= 480)
			continue;

		// Map destination row back to source row
		int srcY = (dy * (int)_screenHeight) / scaledH;
		if (srcY >= (int)_screenHeight) srcY = _screenHeight - 1;

		for (int dx = 0; dx < scaledW; dx++) {
			int fbX = offsetX + dx;
			if (fbX < 0 || fbX >= 800)
				continue;

			int srcX = (dx * (int)_screenWidth) / scaledW;
			if (srcX >= (int)_screenWidth) srcX = _screenWidth - 1;

			uint32 color;
			if (_screenFormat.bytesPerPixel == 1) {
				byte index = *(const byte *)_gameSurface.getBasePtr(srcX, srcY);
				byte r = _palette[index * 3 + 0];
				byte g = _palette[index * 3 + 1];
				byte b = _palette[index * 3 + 2];
				color = (r << 16) | (g << 8) | b;
			} else if (_screenFormat.bytesPerPixel == 2) {
				uint16 pixel = *(const uint16 *)_gameSurface.getBasePtr(srcX, srcY);
				byte r, g, b, a;
				_screenFormat.colorToARGB(pixel, a, r, g, b);
				color = (r << 16) | (g << 8) | b;
			} else {
				uint32 pixel = *(const uint32 *)_gameSurface.getBasePtr(srcX, srcY);
				byte r, g, b, a;
				_screenFormat.colorToARGB(pixel, a, r, g, b);
				color = (r << 16) | (g << 8) | b;
			}

			_fb->back_buffer[fbY * 800 + fbX] = color;
		}
	}
}

void RoomWizardGraphicsManager::drawCursor() {
	if (!_cursorVisible || !_cursorSurface.getPixels() || !_fb)
		return;

	// Calculate centering offsets only if overlay is NOT visible
	int offsetX = 0;
	int offsetY = 0;
	
	if (!_overlayVisible) {
		// Game mode - apply bezel-aware scaled offset
		int scaledW, scaledH;
		getScalingInfo(scaledW, scaledH, offsetX, offsetY);
	}
	// Overlay mode - no offset, cursor coordinates are already in screen space

	int cursorX = _cursorX + offsetX - _cursorHotspotX;
	int cursorY = _cursorY + offsetY - _cursorHotspotY;

	for (int y = 0; y < _cursorSurface.h; y++) {
		int fbY = cursorY + y;
		if (fbY < 0 || fbY >= 480)
			continue;

		for (int x = 0; x < _cursorSurface.w; x++) {
			int fbX = cursorX + x;
			if (fbX < 0 || fbX >= 800)
				continue;

			uint32 pixel;
			if (_cursorSurface.format.bytesPerPixel == 1) {
				byte index = *(const byte *)_cursorSurface.getBasePtr(x, y);
				if (index == _cursorKeyColor)
					continue;

				byte *pal = _cursorPaletteEnabled ? _cursorPalette : _palette;
				byte r = pal[index * 3 + 0];
				byte g = pal[index * 3 + 1];
				byte b = pal[index * 3 + 2];
				pixel = (0xFF << 24) | (r << 16) | (g << 8) | b;  // Add alpha channel
			} else {
				pixel = *(const uint32 *)_cursorSurface.getBasePtr(x, y);
				if (pixel == _cursorKeyColor)
					continue;

				byte r, g, b, a;
				_cursorSurface.format.colorToARGB(pixel, a, r, g, b);
				pixel = (0xFF << 24) | (r << 16) | (g << 8) | b;  // Add alpha channel
			}

			_fb->back_buffer[fbY * 800 + fbX] = pixel;
		}
	}
}

void RoomWizardGraphicsManager::updateScreen() {
	if (!_fbInitialized || !_fb)
		return;

	if (_overlayVisible) {
		// Always draw the game surface first as background so the overlay
		// (e.g. virtual keyboard) appears on top of the game rather than
		// replacing it.
		blitGameSurfaceToFramebuffer();
		_screenDirty = false;

		// Composite overlay on top: treat 0xF81F (magenta, clear-key) pixels as
		// transparent so only the actual overlay bitmap is visible.  Black and
		// all other real colours are composited opaquely, which fixes the GMM
		// background and the VKB text input field showing as transparent.
		for (int y = 0; y < 480; y++) {
			for (int x = 0; x < 800; x++) {
				uint16 pixel = *(const uint16 *)_overlaySurface.getBasePtr(x, y);
				if (pixel == 0xF81F)
					continue; // transparent clear-key – keep game pixel underneath
				byte r = ((pixel >> 11) & 0x1F) << 3;
				byte g = ((pixel >> 5) & 0x3F) << 2;
				byte b = (pixel & 0x1F) << 3;
				_fb->back_buffer[y * 800 + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
			}
		}
	} else if (_screenDirty) {
		blitGameSurfaceToFramebuffer();
		_screenDirty = false;
	}

	// Draw touch feedback (debug mode only: set ROOMWIZARD_DEBUG=1)
	if (rwDebugMode())
		drawTouchFeedback();

	// Draw cursor on top
	drawCursor();

	// Swap buffers
	fb_swap(_fb);
}

void RoomWizardGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
	_shakeXOffset = shakeXOffset;
	_shakeYOffset = shakeYOffset;
	_screenDirty = true;
}

void RoomWizardGraphicsManager::setFocusRectangle(const Common::Rect& rect) {
	// Not implemented
}

void RoomWizardGraphicsManager::clearFocusRectangle() {
	// Not implemented
}

// Overlay methods
void RoomWizardGraphicsManager::showOverlay(bool inGUI) {
	_overlayVisible = true;
}

void RoomWizardGraphicsManager::hideOverlay() {
	_overlayVisible = false;
	_screenDirty = true;
}

bool RoomWizardGraphicsManager::isOverlayVisible() const {
	return _overlayVisible;
}

Graphics::PixelFormat RoomWizardGraphicsManager::getOverlayFormat() const {
	return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
}

void RoomWizardGraphicsManager::clearOverlay() {
	if (_overlaySurface.getPixels()) {
		// Fill with the transparent clear-key (0xF81F, magenta) rather than
		// zero/black.  This lets true black and dark-grey GUI backgrounds
		// render opaquely while still allowing the game to show through any
		// uncovered overlay area (e.g. around the VKB).
		uint16 *p = (uint16 *)_overlaySurface.getPixels();
		for (int i = 0; i < 800 * 480; i++)
			p[i] = 0xF81F;
	}
}

void RoomWizardGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	surface.copyFrom(_overlaySurface);
}

void RoomWizardGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	if (!_overlaySurface.getPixels())
		return;

	copyRectToSurface(_overlaySurface, buf, pitch, x, y, w, h, getOverlayFormat());
}

int16 RoomWizardGraphicsManager::getOverlayHeight() const {
	return 480;
}

int16 RoomWizardGraphicsManager::getOverlayWidth() const {
	return 800;
}

// Mouse cursor methods
bool RoomWizardGraphicsManager::showMouse(bool visible) {
	bool wasVisible = _cursorVisible;
	_cursorVisible = visible;
	return wasVisible;
}

void RoomWizardGraphicsManager::warpMouse(int x, int y) {
	_cursorX = x;
	_cursorY = y;
}

void RoomWizardGraphicsManager::setMouseCursor(const void *buf, uint w, uint h, 
                                                int hotspotX, int hotspotY, 
                                                uint32 keycolor, bool dontScale,
                                                const Graphics::PixelFormat *format,
                                                const byte *mask) {
	_cursorHotspotX = hotspotX;
	_cursorHotspotY = hotspotY;
	_cursorKeyColor = keycolor;
	_cursorDontScale = dontScale;

	Graphics::PixelFormat cursorFormat = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
	
	_cursorSurface.create(w, h, cursorFormat);
	memcpy(_cursorSurface.getPixels(), buf, w * h * cursorFormat.bytesPerPixel);
}

void RoomWizardGraphicsManager::setCursorPalette(const byte *colors, uint start, uint num) {
	if (start + num > 256) {
		warning("setCursorPalette: Invalid palette range");
		return;
	}

	memcpy(&_cursorPalette[start * 3], colors, num * 3);
	_cursorPaletteEnabled = true;
}

void RoomWizardGraphicsManager::addTouchPoint(int x, int y) {
	uint32 currentTime = g_system->getMillis();
	
	// Add new touch point
	_touchPoints[_touchPointIndex].x = x;
	_touchPoints[_touchPointIndex].y = y;
	_touchPoints[_touchPointIndex].timestamp = currentTime;
	_touchPoints[_touchPointIndex].active = true;
	
	_touchPointIndex = (_touchPointIndex + 1) % MAX_TOUCH_POINTS;
}

void RoomWizardGraphicsManager::drawTouchFeedback() {
	if (!_fb)
		return;
	
	uint32 currentTime = g_system->getMillis();
	const uint32 FADE_TIME = 1000; // 1 second fade
	
	// Draw circles for recent touch points
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (!_touchPoints[i].active)
			continue;
		
		uint32 age = currentTime - _touchPoints[i].timestamp;
		if (age > FADE_TIME) {
			_touchPoints[i].active = false;
			continue;
		}
		
		// Calculate alpha based on age (fade out)
		int alpha = 255 - (age * 255 / FADE_TIME);
		
		// Draw circle at touch point
		int cx = _touchPoints[i].x;
		int cy = _touchPoints[i].y;
		int radius = 20;
		
		// Draw filled circle
		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {
				if (dx * dx + dy * dy <= radius * radius) {
					int px = cx + dx;
					int py = cy + dy;
					
					if (px >= 0 && px < 800 && py >= 0 && py < 480) {
						// Blend red circle with existing pixel
						uint32 existing = _fb->back_buffer[py * 800 + px];
						byte er = (existing >> 16) & 0xFF;
						byte eg = (existing >> 8) & 0xFF;
						byte eb = existing & 0xFF;
						
						byte r = ((255 * alpha) + (er * (255 - alpha))) / 255;
						byte g = ((0 * alpha) + (eg * (255 - alpha))) / 255;
						byte b = ((0 * alpha) + (eb * (255 - alpha))) / 255;
						
						_fb->back_buffer[py * 800 + px] = (r << 16) | (g << 8) | b;
					}
				}
			}
		}
	}
}
