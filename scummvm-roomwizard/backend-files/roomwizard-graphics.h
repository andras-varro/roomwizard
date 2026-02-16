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

#ifndef BACKENDS_GRAPHICS_ROOMWIZARD_H
#define BACKENDS_GRAPHICS_ROOMWIZARD_H

#include "backends/graphics/graphics.h"
#include "graphics/surface.h"

// Include C headers directly
extern "C" {
#include "framebuffer.h"
}

class RoomWizardGraphicsManager : public GraphicsManager {
public:
	RoomWizardGraphicsManager();
	virtual ~RoomWizardGraphicsManager();

	// GraphicsManager API
	bool hasFeature(OSystem::Feature f) const override;
	void setFeatureState(OSystem::Feature f, bool enable) override;
	bool getFeatureState(OSystem::Feature f) const override;

#ifdef USE_RGB_COLOR
	Graphics::PixelFormat getScreenFormat() const override;
	Common::List<Graphics::PixelFormat> getSupportedFormats() const override;
#endif

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = NULL) override;
	int getScreenChangeID() const override;

	void beginGFXTransaction() override;
	OSystem::TransactionError endGFXTransaction() override;

	int16 getHeight() const override;
	int16 getWidth() const override;
	
	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;
	
	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override;
	void unlockScreen() override;
	
	void fillScreen(uint32 col) override;
	void fillScreen(const Common::Rect &r, uint32 col) override;
	void updateScreen() override;
	
	void setShakePos(int shakeXOffset, int shakeYOffset) override;
	void setFocusRectangle(const Common::Rect& rect) override;
	void clearFocusRectangle() override;

	// Overlay
	void showOverlay(bool inGUI) override;
	void hideOverlay() override;
	bool isOverlayVisible() const override;
	Graphics::PixelFormat getOverlayFormat() const override;
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	int16 getOverlayHeight() const override;
	int16 getOverlayWidth() const override;

	// Mouse cursor
	bool showMouse(bool visible) override;
	void warpMouse(int x, int y) override;
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, 
	                    uint32 keycolor, bool dontScale = false, 
	                    const Graphics::PixelFormat *format = NULL, const byte *mask = NULL) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

private:
	// Framebuffer
	Framebuffer *_fb;
	bool _fbInitialized;

	// Screen properties
	uint _screenWidth;
	uint _screenHeight;
	Graphics::PixelFormat _screenFormat;
	Graphics::Surface _gameSurface;
	bool _screenDirty;

	// Palette for CLUT8 mode
	byte _palette[256 * 3];
	bool _paletteDirty;

	// Cursor
	Graphics::Surface _cursorSurface;
	int _cursorX, _cursorY;
	int _cursorHotspotX, _cursorHotspotY;
	uint32 _cursorKeyColor;
	bool _cursorVisible;
	bool _cursorDontScale;
	byte _cursorPalette[256 * 3];
	bool _cursorPaletteEnabled;

	// Overlay
	Graphics::Surface _overlaySurface;
	bool _overlayVisible;

	// Shake offset
	int _shakeXOffset;
	int _shakeYOffset;

	// Touch feedback visualization
	struct TouchPoint {
		int x, y;
		uint32 timestamp;
		bool active;
	};
	static const int MAX_TOUCH_POINTS = 10;
	TouchPoint _touchPoints[MAX_TOUCH_POINTS];
	int _touchPointIndex;

	// Helper methods
	void initFramebuffer();
	void closeFramebuffer();
	void blitGameSurfaceToFramebuffer();
	void drawCursor();
	void drawTouchFeedback();
	uint32 convertColor(uint32 color, const Graphics::PixelFormat &srcFormat);
	void copyRectToSurface(Graphics::Surface &dst, const void *buf, int pitch,
	                       int x, int y, int w, int h, const Graphics::PixelFormat &srcFormat);

public:
	// Touch feedback API
	void addTouchPoint(int x, int y);
};

#endif
