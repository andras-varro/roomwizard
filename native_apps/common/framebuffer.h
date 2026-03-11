#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // For size_t

typedef struct {
    int fd;
    uint32_t *buffer;        // Front buffer (mapped to screen)
    uint32_t *back_buffer;   // Back buffer (for double buffering)
    uint32_t width;
    uint32_t height;
    uint32_t line_length;
    uint32_t bytes_per_pixel;
    size_t screen_size;
    bool double_buffering;   // Enable/disable double buffering
    int draw_offset_x;      // Draw offset X (for screen shake etc.)
    int draw_offset_y;      // Draw offset Y (for screen shake etc.)
    bool portrait_mode;      // Portrait mode active (90° rotation in fb_swap)
    uint32_t phys_width;     // Physical framebuffer width (from hardware)
    uint32_t phys_height;    // Physical framebuffer height (from hardware)
    size_t back_buffer_size; // Back buffer size (may differ from screen_size in portrait)
} Framebuffer;

// Default safe area margins (used if config file is missing)
#define SCREEN_SAFE_MARGIN_LEFT_DEFAULT   0
#define SCREEN_SAFE_MARGIN_RIGHT_DEFAULT  0
#define SCREEN_SAFE_MARGIN_TOP_DEFAULT    0
#define SCREEN_SAFE_MARGIN_BOTTOM_DEFAULT 0

// Runtime safe area margins (loaded from /etc/touch_calibration.conf)
extern int screen_safe_margin_left;
extern int screen_safe_margin_right;
extern int screen_safe_margin_top;
extern int screen_safe_margin_bottom;

// Runtime screen base dimensions (set by fb_init(), default 800x480)
extern int screen_base_width;
extern int screen_base_height;

// Safe area bounds (now computed from runtime variables)
#define SCREEN_SAFE_LEFT   (screen_safe_margin_left)
#define SCREEN_SAFE_RIGHT  (screen_base_width - screen_safe_margin_right)
#define SCREEN_SAFE_TOP    (screen_safe_margin_top)
#define SCREEN_SAFE_BOTTOM (screen_base_height - screen_safe_margin_bottom)
#define SCREEN_SAFE_WIDTH  (SCREEN_SAFE_RIGHT - SCREEN_SAFE_LEFT)
#define SCREEN_SAFE_HEIGHT (SCREEN_SAFE_BOTTOM - SCREEN_SAFE_TOP)

// Load safe area margins from calibration config file
// Called automatically by fb_init()
void fb_load_safe_area(void);

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

#endif
