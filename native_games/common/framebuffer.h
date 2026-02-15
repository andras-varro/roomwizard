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
} Framebuffer;

// Physical screen safe area constraints
// The RoomWizard has a physical bezel that obscures the edges of the LCD
// Framebuffer: 800x480, Visible area: ~720x420
#define SCREEN_SAFE_MARGIN_LEFT   40
#define SCREEN_SAFE_MARGIN_RIGHT  40
#define SCREEN_SAFE_MARGIN_TOP    30
#define SCREEN_SAFE_MARGIN_BOTTOM 30

#define SCREEN_SAFE_LEFT   (SCREEN_SAFE_MARGIN_LEFT)
#define SCREEN_SAFE_RIGHT  (800 - SCREEN_SAFE_MARGIN_RIGHT)
#define SCREEN_SAFE_TOP    (SCREEN_SAFE_MARGIN_TOP)
#define SCREEN_SAFE_BOTTOM (480 - SCREEN_SAFE_MARGIN_BOTTOM)
#define SCREEN_SAFE_WIDTH  (SCREEN_SAFE_RIGHT - SCREEN_SAFE_LEFT)
#define SCREEN_SAFE_HEIGHT (SCREEN_SAFE_BOTTOM - SCREEN_SAFE_TOP)

// Initialize framebuffer
int fb_init(Framebuffer *fb, const char *device);

// Close framebuffer
void fb_close(Framebuffer *fb);

// Swap buffers (present back buffer to screen)
void fb_swap(Framebuffer *fb);

// Clear screen with color
void fb_clear(Framebuffer *fb, uint32_t color);

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

// Draw text (simple bitmap font)
void fb_draw_text(Framebuffer *fb, int x, int y, const char *text, uint32_t color, int scale);

// Screen transition effects
void fb_fade_out(Framebuffer *fb);
void fb_fade_in(Framebuffer *fb);

// RGB color helper
#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

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
