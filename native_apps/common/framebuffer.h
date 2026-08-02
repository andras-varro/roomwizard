#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // For size_t

typedef struct {
    int fd;
    uint32_t *buffer;        // Front buffer (mapped to screen), PANEL sized
    uint32_t *back_buffer;   // Back buffer (for double buffering), LOGICAL sized
    uint32_t width;          // Logical (visible) width  — what apps draw into
    uint32_t height;         // Logical (visible) height — what apps draw into
    uint32_t line_length;
    uint32_t bytes_per_pixel;
    size_t screen_size;
    bool double_buffering;   // Enable/disable double buffering
    int draw_offset_x;      // Draw offset X (for screen shake etc.)
    int draw_offset_y;      // Draw offset Y (for screen shake etc.)
    bool portrait_mode;      // Portrait mode active (90° rotation in fb_swap)
    uint32_t phys_width;     // Physical framebuffer width (from hardware)
    uint32_t phys_height;    // Physical framebuffer height (from hardware)
    size_t back_buffer_size; // Back buffer size (logical dims × bpp)
    int view_x;              // Logical surface origin within the panel (bezel left)
    int view_y;              // Logical surface origin within the panel (bezel top)
} Framebuffer;

// ---------------------------------------------------------------------------
// Bezel viewport
//
// The plastic bezel hides a band of pixels at the panel edges. Apps never see
// those pixels: fb_init() shrinks the drawing surface to the visible rectangle
// and fb_swap() places it on the panel at (view_x, view_y), leaving the hidden
// bands black. fb->width / fb->height are therefore the LOGICAL (visible) size,
// and every drawing primitive works in logical coordinates.
//
// touch_input.c consumes the same globals to translate a panel touch back into
// logical space, so drawing and touch always share one coordinate system.
// ---------------------------------------------------------------------------

// Bezel margins used when the config file has no margin line
#define FB_BEZEL_TOP_DEFAULT    15
#define FB_BEZEL_BOTTOM_DEFAULT 15
#define FB_BEZEL_LEFT_DEFAULT    0
#define FB_BEZEL_RIGHT_DEFAULT   0

// Runtime bezel margins in pixels (loaded from /etc/touch_calibration.conf).
// Stored in the app's orientation: fb_init() rotates them in portrait mode.
extern int screen_bezel_top;
extern int screen_bezel_bottom;
extern int screen_bezel_left;
extern int screen_bezel_right;

// Full panel dimensions in the app's orientation (800x480, swapped in portrait)
extern int screen_panel_width;
extern int screen_panel_height;

// Logical surface origin within the panel (== bezel left/top after rotation)
extern int screen_view_x;
extern int screen_view_y;

// Logical (visible) screen dimensions — set by fb_init(), default 800x480
extern int screen_base_width;
extern int screen_base_height;

// ---------------------------------------------------------------------------
// Two rectangles, not one: visible and touch-safe
//
// The whole logical screen is DRAWABLE. It is not all PRESSABLE. The digitiser
// saturates before the physical panel edge — measured on RW09, raw pins at 4095
// from panel row ~450 of 480 and at 0 from row ~30 — so a band at each end of Y
// is visible but cannot be addressed by a finger. X reaches both edges. How wide
// the band is depends on the panel and the calibration, so it is MEASURED at
// runtime, never assumed: touch_input.c maps the raw hardware corners through the
// live map and calls fb_set_touch_inset(). An uncalibrated device gets zero.
//
//   SCREEN_VISIBLE_*  the full logical screen. Backgrounds, borders, titles,
//                     status and score rows, game playfields — anything the user
//                     only has to SEE. The band is good screen area; use it.
//   SCREEN_SAFE_*     visible AND touchable. Buttons, toggles, tab bars, touch
//                     grids — anything the user has to PRESS.
//
// Drawing primitives are unchanged: they take logical coordinates and fb->width /
// fb->height remain the full logical size.
//
// SCREEN_SAFE_* is only correct after touch_init() (or a later
// touch_set_screen_size() / touch_set_raw_curve()), so compute layout after both
// fb_init() and touch_init() — which is the documented app lifecycle anyway. The
// macros read the globals at each use, so a layout recomputed after
// fb_set_bezel() picks up the new values.
// ---------------------------------------------------------------------------

// Logical pixels at each edge that are visible but not touchable. Set by
// touch_input.c via fb_set_touch_inset(); zero until then, so an app that never
// initialises touch sees the whole logical screen as safe.
extern int screen_touch_inset_top;
extern int screen_touch_inset_bottom;
extern int screen_touch_inset_left;
extern int screen_touch_inset_right;

// Largest inset that will be believed. A calibration that puts more than this out
// of reach is broken, and silently shrinking every UI to match it hides the fault
// where a loud warning finds it. ~10% of the short axis.
#define FB_TOUCH_INSET_MAX 48

// Publish the measured inset. Values are clamped to [0, FB_TOUCH_INSET_MAX] and a
// clamp is reported on stdout — see the note above.
void fb_set_touch_inset(int top, int bottom, int left, int right);

#define SCREEN_VISIBLE_LEFT   0
#define SCREEN_VISIBLE_TOP    0
#define SCREEN_VISIBLE_RIGHT  (screen_base_width)
#define SCREEN_VISIBLE_BOTTOM (screen_base_height)
#define SCREEN_VISIBLE_WIDTH  (SCREEN_VISIBLE_RIGHT - SCREEN_VISIBLE_LEFT)
#define SCREEN_VISIBLE_HEIGHT (SCREEN_VISIBLE_BOTTOM - SCREEN_VISIBLE_TOP)

#define SCREEN_SAFE_LEFT   (screen_touch_inset_left)
#define SCREEN_SAFE_TOP    (screen_touch_inset_top)
#define SCREEN_SAFE_RIGHT  (screen_base_width  - screen_touch_inset_right)
#define SCREEN_SAFE_BOTTOM (screen_base_height - screen_touch_inset_bottom)
#define SCREEN_SAFE_WIDTH  (SCREEN_SAFE_RIGHT - SCREEN_SAFE_LEFT)
#define SCREEN_SAFE_HEIGHT (SCREEN_SAFE_BOTTOM - SCREEN_SAFE_TOP)

// Load bezel margins from the calibration config file.
// Called automatically by fb_init().
void fb_load_bezel(void);

// Check if portrait mode is enabled (flag file /opt/games/portrait.mode exists)
bool fb_is_portrait_mode(void);

// Initialize framebuffer
int fb_init(Framebuffer *fb, const char *device);

// Close framebuffer
void fb_close(Framebuffer *fb);

// Swap buffers (present back buffer to screen)
void fb_swap(Framebuffer *fb);

// Clear screen with color
void fb_clear(Framebuffer *fb, uint32_t color);

// Set draw offset — applied automatically to all drawing primitives
void fb_set_draw_offset(Framebuffer *fb, int dx, int dy);

// Clear draw offset (reset to 0,0)
void fb_clear_draw_offset(Framebuffer *fb);

// Draw pixel
void fb_draw_pixel(Framebuffer *fb, int x, int y, uint32_t color);

// Draw rectangle
void fb_draw_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color);

// Draw filled rectangle
void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color);

// Draw circle
void fb_draw_circle(Framebuffer *fb, int cx, int cy, int radius, uint32_t color);

// Draw filled circle
void fb_fill_circle(Framebuffer *fb, int cx, int cy, int radius, uint32_t color);

// Draw text (simple bitmap font — lowercase mapped to uppercase automatically)
void fb_draw_text(Framebuffer *fb, int x, int y, const char *text, uint32_t color, int scale);

// Draw filled rounded rectangle (r = corner radius)
void fb_fill_rounded_rect(Framebuffer *fb, int x, int y, int w, int h, int r, uint32_t color);

// Draw rounded rectangle outline (r = corner radius)
void fb_draw_rounded_rect(Framebuffer *fb, int x, int y, int w, int h, int r, uint32_t color);

// Draw a line between two points (Bresenham's algorithm)
void fb_draw_line(Framebuffer *fb, int x0, int y0, int x1, int y1, uint32_t color);

// Draw a filled rectangle with a vertical gradient (top_color → bottom_color)
void fb_fill_rect_gradient(Framebuffer *fb, int x, int y, int w, int h,
                           uint32_t top_color, uint32_t bottom_color);

// Draw a pixel with alpha blending (alpha 0-255, 255 = opaque)
void fb_draw_pixel_alpha(Framebuffer *fb, int x, int y, uint32_t color, uint8_t alpha);

// Draw a filled rectangle with alpha blending
void fb_fill_rect_alpha(Framebuffer *fb, int x, int y, int w, int h,
                        uint32_t color, uint8_t alpha);

// Draw a thick line (thickness in pixels)
void fb_draw_thick_line(Framebuffer *fb, int x0, int y0, int x1, int y1,
                        int thickness, uint32_t color);

// Screen transition effects
void fb_fade_out(Framebuffer *fb);
void fb_fade_in(Framebuffer *fb);

// RGB color helper
#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

// Set framebuffer bits-per-pixel (e.g. 16 or 32). Must be called BEFORE fb_init.
int fb_set_bpp(const char *device, int bpp);

// Change the bezel margins on a live framebuffer: resizes the logical surface,
// republishes the globals and re-blacks the panel. Returns 0 on success, -1 if
// the margins leave no usable area (in which case fb is left unchanged).
// Callers that also own a TouchInput must follow up with touch_set_viewport().
int fb_set_bezel(Framebuffer *fb, int top, int bottom, int left, int right);

// Common colors
#define COLOR_BLACK   RGB(0, 0, 0)
#define COLOR_WHITE   RGB(255, 255, 255)
#define COLOR_RED     RGB(255, 0, 0)
#define COLOR_GREEN   RGB(0, 255, 0)
#define COLOR_BLUE    RGB(0, 0, 255)
#define COLOR_YELLOW  RGB(255, 255, 0)
#define COLOR_CYAN    RGB(0, 255, 255)
#define COLOR_MAGENTA RGB(255, 0, 255)
#define COLOR_ORANGE  RGB(255, 165, 0)
#define COLOR_PURPLE  RGB(128, 0, 128)
#define COLOR_GRAY    RGB(128, 128, 128)

// Sprite transparency color key (magenta)
#define SPRITE_TRANSPARENT RGB(255, 0, 255)

// Blit a rectangular region from a source pixel buffer to the framebuffer
// src_pixels: source pixel array (uint32_t ARGB), src_w: total source image width
// sx, sy: source rectangle top-left in source image
// dx, dy: destination position on framebuffer
// w, h: dimensions of the rectangle to copy
// color_key: transparent color (pixels matching this are skipped), use 0xFFFFFFFF to disable
void fb_blit_sprite(Framebuffer *fb, const uint32_t *src_pixels, int src_w,
                    int sx, int sy, int dx, int dy, int w, int h,
                    uint32_t color_key);

// Same as fb_blit_sprite but horizontally flipped
void fb_blit_sprite_flipped(Framebuffer *fb, const uint32_t *src_pixels, int src_w,
                            int sx, int sy, int dx, int dy, int w, int h,
                            uint32_t color_key);

// Blit with scaling (nearest-neighbor) — for zoom effects or different tile sizes
void fb_blit_sprite_scaled(Framebuffer *fb, const uint32_t *src_pixels, int src_w,
                           int sx, int sy, int sw, int sh,
                           int dx, int dy, int dw, int dh,
                           uint32_t color_key);

#endif
