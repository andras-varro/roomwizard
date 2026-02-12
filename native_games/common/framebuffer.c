#include "framebuffer.h"
#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

// Simple 5x7 bitmap font
static const uint8_t font_5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

int fb_init(Framebuffer *fb, const char *device) {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    
    fb->fd = open(device, O_RDWR);
    if (fb->fd == -1) {
        perror("Error opening framebuffer device");
        return -1;
    }
    
    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Error reading fixed information");
        close(fb->fd);
        return -1;
    }
    
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable information");
        close(fb->fd);
        return -1;
    }
    
    fb->width = vinfo.xres;
    fb->height = vinfo.yres;
    fb->bytes_per_pixel = vinfo.bits_per_pixel / 8;
    fb->line_length = finfo.line_length;
    fb->screen_size = fb->line_length * fb->height;
    
    fb->buffer = (uint32_t *)mmap(0, fb->screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->buffer == MAP_FAILED) {
        perror("Error mapping framebuffer device to memory");
        close(fb->fd);
        return -1;
    }
    
    // Allocate back buffer for double buffering
    fb->back_buffer = (uint32_t *)malloc(fb->screen_size);
    if (fb->back_buffer == NULL) {
        perror("Error allocating back buffer");
        munmap(fb->buffer, fb->screen_size);
        close(fb->fd);
        return -1;
    }
    fb->double_buffering = true;
    
    printf("Framebuffer initialized: %dx%d, %d bpp (double buffering enabled)\n", 
           fb->width, fb->height, vinfo.bits_per_pixel);
    return 0;
}

void fb_close(Framebuffer *fb) {
    if (fb->back_buffer != NULL) {
        free(fb->back_buffer);
    }
    if (fb->buffer != MAP_FAILED) {
        munmap(fb->buffer, fb->screen_size);
    }
    if (fb->fd != -1) {
        close(fb->fd);
    }
}

void fb_swap(Framebuffer *fb) {
    if (fb->double_buffering && fb->back_buffer != NULL) {
        // Copy back buffer to front buffer (screen)
        memcpy(fb->buffer, fb->back_buffer, fb->screen_size);
    }
}

void fb_clear(Framebuffer *fb, uint32_t color) {
    // Clear the back buffer if double buffering is enabled
    uint32_t *target = fb->double_buffering ? fb->back_buffer : fb->buffer;
    for (uint32_t i = 0; i < fb->width * fb->height; i++) {
        target[i] = color;
    }
}

void fb_draw_pixel(Framebuffer *fb, int x, int y, uint32_t color) {
    if (x >= 0 && x < (int)fb->width && y >= 0 && y < (int)fb->height) {
        // Draw to back buffer if double buffering is enabled
        uint32_t *target = fb->double_buffering ? fb->back_buffer : fb->buffer;
        target[y * fb->width + x] = color;
    }
}

void fb_draw_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < w; i++) {
        fb_draw_pixel(fb, x + i, y, color);
        fb_draw_pixel(fb, x + i, y + h - 1, color);
    }
    for (int i = 0; i < h; i++) {
        fb_draw_pixel(fb, x, y + i, color);
        fb_draw_pixel(fb, x + w - 1, y + i, color);
    }
}

void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            fb_draw_pixel(fb, x + i, y + j, color);
        }
    }
}

void fb_draw_circle(Framebuffer *fb, int cx, int cy, int radius, uint32_t color) {
    int x = radius;
    int y = 0;
    int err = 0;
    
    while (x >= y) {
        fb_draw_pixel(fb, cx + x, cy + y, color);
        fb_draw_pixel(fb, cx + y, cy + x, color);
        fb_draw_pixel(fb, cx - y, cy + x, color);
        fb_draw_pixel(fb, cx - x, cy + y, color);
        fb_draw_pixel(fb, cx - x, cy - y, color);
        fb_draw_pixel(fb, cx - y, cy - x, color);
        fb_draw_pixel(fb, cx + y, cy - x, color);
        fb_draw_pixel(fb, cx + x, cy - y, color);
        
        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

void fb_fill_circle(Framebuffer *fb, int cx, int cy, int radius, uint32_t color) {
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius) {
                fb_draw_pixel(fb, cx + x, cy + y, color);
            }
        }
    }
}

void fb_draw_text(Framebuffer *fb, int x, int y, const char *text, uint32_t color, int scale) {
    int offset_x = 0;
    
    while (*text) {
        char c = *text;
        int idx = -1;
        
        if (c >= ' ' && c <= 'Z') {
            idx = c - ' ';
        }
        
        if (idx >= 0 && idx < 91) {
            for (int col = 0; col < 5; col++) {
                uint8_t column = font_5x7[idx][col];
                for (int row = 0; row < 7; row++) {
                    if (column & (1 << row)) {
                        for (int sy = 0; sy < scale; sy++) {
                            for (int sx = 0; sx < scale; sx++) {
                                fb_draw_pixel(fb, x + offset_x + col * scale + sx, 
                                            y + row * scale + sy, color);
                            }
                        }
                    }
                }
            }
        }
        
        offset_x += 6 * scale;
        text++;
    }
}

void fb_fade_out(Framebuffer *fb) {
    // Fade out backlight smoothly, then clear screen
    #include "../common/hardware.h"
    
    for (int i = 100; i >= 0; i -= 5) {
        hw_set_backlight(i);
        usleep(30000);  // 30ms per step = 600ms total
    }
    
    // Clear screen to black
    fb_clear(fb, COLOR_BLACK);
    fb_swap(fb);
    
    // Restore backlight for next app
    hw_set_backlight(100);
}

void fb_fade_in(Framebuffer *fb) {
    // Fade in backlight smoothly from black
    #include "../common/hardware.h"
    
    // Start with backlight off
    hw_set_backlight(0);
    
    // Fade in
    for (int i = 0; i <= 100; i += 5) {
        hw_set_backlight(i);
        usleep(30000);  // 30ms per step = 600ms total
    }
}
