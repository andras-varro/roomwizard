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

// Runtime safe area margins
int screen_safe_margin_left   = SCREEN_SAFE_MARGIN_LEFT_DEFAULT;
int screen_safe_margin_right  = SCREEN_SAFE_MARGIN_RIGHT_DEFAULT;
int screen_safe_margin_top    = SCREEN_SAFE_MARGIN_TOP_DEFAULT;
int screen_safe_margin_bottom = SCREEN_SAFE_MARGIN_BOTTOM_DEFAULT;

// Runtime screen base dimensions (defaults safe for pre-fb_init usage)
int screen_base_width  = 800;
int screen_base_height = 480;

void fb_load_safe_area(void) {
    // Reset to defaults
    screen_safe_margin_left   = SCREEN_SAFE_MARGIN_LEFT_DEFAULT;
    screen_safe_margin_right  = SCREEN_SAFE_MARGIN_RIGHT_DEFAULT;
    screen_safe_margin_top    = SCREEN_SAFE_MARGIN_TOP_DEFAULT;
    screen_safe_margin_bottom = SCREEN_SAFE_MARGIN_BOTTOM_DEFAULT;

    FILE *f = fopen("/etc/touch_calibration.conf", "r");
    if (!f) {
        printf("Safe area: no calibration file — using defaults (%d,%d,%d,%d)\n",
               screen_safe_margin_left, screen_safe_margin_top,
               screen_safe_margin_right, screen_safe_margin_bottom);
        return;
    }

    char line[256];
    int data_lines = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        data_lines++;
        if (data_lines == 2) {
            int t, b, l, r;
            if (sscanf(line, "%d %d %d %d", &t, &b, &l, &r) == 4) {
                screen_safe_margin_top    = t;
                screen_safe_margin_bottom = b;
                screen_safe_margin_left   = l;
                screen_safe_margin_right  = r;
            }
            break;
        }
    }
    fclose(f);

    printf("Safe area: loaded margins L=%d T=%d R=%d B=%d from /etc/touch_calibration.conf\n",
           screen_safe_margin_left, screen_safe_margin_top,
           screen_safe_margin_right, screen_safe_margin_bottom);
}

bool fb_is_portrait_mode(void) {
    return access("/opt/games/portrait.mode", F_OK) == 0;
}

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

int fb_set_bpp(const char *device, int bpp) {
    int fd = open(device, O_RDWR);
    if (fd < 0) return -1;

    struct fb_var_screeninfo vinfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        close(fd);
        return -1;
    }

    vinfo.bits_per_pixel = bpp;
    int ret = ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo);
    close(fd);
    return ret;
}

int fb_init(Framebuffer *fb, const char *device) {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    // Load bezel/safe-area margins from calibration config
    fb_load_safe_area();

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
    
    fb->phys_width = vinfo.xres;
    fb->phys_height = vinfo.yres;
    fb->portrait_mode = fb_is_portrait_mode();

    if (fb->portrait_mode) {
        // Apps see swapped dimensions (e.g., 480x800 instead of 800x480)
        fb->width = fb->phys_height;
        fb->height = fb->phys_width;
        printf("Portrait mode: physical %dx%d -> virtual %dx%d\n",
               fb->phys_width, fb->phys_height, fb->width, fb->height);
    } else {
        fb->width = fb->phys_width;
        fb->height = fb->phys_height;
    }

    // Update global base dimensions so SCREEN_SAFE_* macros adapt
    screen_base_width  = fb->width;
    screen_base_height = fb->height;

    if (fb->portrait_mode) {
        // Rotate safe area margins to match virtual coordinate system (90 CCW)
        // Physical left -> virtual top, physical right -> virtual bottom
        // Physical top -> virtual right, physical bottom -> virtual left
        int phys_top = screen_safe_margin_top;
        int phys_bottom = screen_safe_margin_bottom;
        int phys_left = screen_safe_margin_left;
        int phys_right = screen_safe_margin_right;

        screen_safe_margin_top = phys_left;
        screen_safe_margin_bottom = phys_right;
        screen_safe_margin_left = phys_bottom;
        screen_safe_margin_right = phys_top;

        printf("Portrait mode: rotated safe margins T=%d B=%d L=%d R=%d\n",
               screen_safe_margin_top, screen_safe_margin_bottom,
               screen_safe_margin_left, screen_safe_margin_right);
    }

    fb->bytes_per_pixel = vinfo.bits_per_pixel / 8;
    fb->line_length = finfo.line_length;
    fb->screen_size = fb->line_length * fb->phys_height;  // Physical size for mmap
    fb->back_buffer_size = fb->width * fb->height * fb->bytes_per_pixel;  // Virtual size for back buffer
    
    fb->buffer = (uint32_t *)mmap(0, fb->screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->buffer == MAP_FAILED) {
        perror("Error mapping framebuffer device to memory");
        close(fb->fd);
        return -1;
    }
    
    // Allocate back buffer for double buffering
    fb->back_buffer = (uint32_t *)malloc(fb->back_buffer_size);
    if (fb->back_buffer == NULL) {
        perror("Error allocating back buffer");
        munmap(fb->buffer, fb->screen_size);
        close(fb->fd);
        return -1;
    }
    fb->double_buffering = true;
    
    fb->draw_offset_x = 0;
    fb->draw_offset_y = 0;

    printf("Framebuffer initialized: %dx%d%s, %d bpp (double buffering enabled)\n",
           fb->width, fb->height, fb->portrait_mode ? " [portrait]" : "",
           vinfo.bits_per_pixel);
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
        if (fb->portrait_mode) {
            // 90 CCW rotation: virtual (vx, vy) -> physical (vy, phys_height-1-vx)
            // Iterate back_buffer row-by-row for cache-friendly reads
            uint32_t vw = fb->width;    // Virtual width (e.g., 480)
            uint32_t vh = fb->height;   // Virtual height (e.g., 800)
            uint32_t pw = fb->phys_width;  // Physical width (e.g., 800)
            uint32_t ph_minus_1 = fb->phys_height - 1;  // e.g., 479

            for (uint32_t vy = 0; vy < vh; vy++) {
                uint32_t *src_row = fb->back_buffer + vy * vw;
                uint32_t px = vy;  // Physical X = virtual Y
                for (uint32_t vx = 0; vx < vw; vx++) {
                    uint32_t py = ph_minus_1 - vx;  // Physical Y = phys_height-1-vx
                    fb->buffer[py * pw + px] = src_row[vx];
                }
            }
        } else {
            // Normal landscape: straight copy
            memcpy(fb->buffer, fb->back_buffer, fb->screen_size);
        }
    }
}

void fb_clear(Framebuffer *fb, uint32_t color) {
    // Clear the back buffer if double buffering is enabled
    uint32_t *target = fb->double_buffering ? fb->back_buffer : fb->buffer;
    for (uint32_t i = 0; i < fb->width * fb->height; i++) {
        target[i] = color;
    }
}

void fb_set_draw_offset(Framebuffer *fb, int dx, int dy) {
    fb->draw_offset_x = dx;
    fb->draw_offset_y = dy;
}

void fb_clear_draw_offset(Framebuffer *fb) {
    fb->draw_offset_x = 0;
    fb->draw_offset_y = 0;
}

void fb_draw_pixel(Framebuffer *fb, int x, int y, uint32_t color) {
    if (x >= 0 && x < (int)fb->width && y >= 0 && y < (int)fb->height) {
        // Draw to back buffer if double buffering is enabled
        uint32_t *target = fb->double_buffering ? fb->back_buffer : fb->buffer;
        target[y * fb->width + x] = color;
    }
}

void fb_draw_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color) {
    x += fb->draw_offset_x;
    y += fb->draw_offset_y;
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
    x += fb->draw_offset_x;
    y += fb->draw_offset_y;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            fb_draw_pixel(fb, x + i, y + j, color);
        }
    }
}

void fb_draw_circle(Framebuffer *fb, int cx, int cy, int radius, uint32_t color) {
    cx += fb->draw_offset_x;
    cy += fb->draw_offset_y;
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
    cx += fb->draw_offset_x;
    cy += fb->draw_offset_y;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius) {
                fb_draw_pixel(fb, cx + x, cy + y, color);
            }
        }
    }
}

// Draw filled rounded rectangle
void fb_fill_rounded_rect(Framebuffer *fb, int x, int y, int w, int h, int r, uint32_t color) {
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    // Central rectangle
    fb_fill_rect(fb, x + r, y, w - 2 * r, h, color);
    // Left and right rectangles
    fb_fill_rect(fb, x, y + r, r, h - 2 * r, color);
    fb_fill_rect(fb, x + w - r, y + r, r, h - 2 * r, color);
    // Four corner circles
    fb_fill_circle(fb, x + r,     y + r,     r, color);
    fb_fill_circle(fb, x + w - r - 1, y + r,     r, color);
    fb_fill_circle(fb, x + r,     y + h - r - 1, r, color);
    fb_fill_circle(fb, x + w - r - 1, y + h - r - 1, r, color);
}

// Draw rounded rectangle outline
void fb_draw_rounded_rect(Framebuffer *fb, int x, int y, int w, int h, int r, uint32_t color) {
    x += fb->draw_offset_x;
    y += fb->draw_offset_y;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    // Top and bottom edges
    for (int i = r; i < w - r; i++) {
        fb_draw_pixel(fb, x + i, y, color);
        fb_draw_pixel(fb, x + i, y + h - 1, color);
    }
    // Left and right edges
    for (int i = r; i < h - r; i++) {
        fb_draw_pixel(fb, x, y + i, color);
        fb_draw_pixel(fb, x + w - 1, y + i, color);
    }
    // Four corner arcs (Bresenham circle)
    int cx, cy, err;
    cx = r; cy = 0; err = 0;
    while (cx >= cy) {
        fb_draw_pixel(fb, x + r - cx,     y + r - cy,     color); // TL
        fb_draw_pixel(fb, x + r - cy,     y + r - cx,     color);
        fb_draw_pixel(fb, x + w-1-r + cx, y + r - cy,     color); // TR
        fb_draw_pixel(fb, x + w-1-r + cy, y + r - cx,     color);
        fb_draw_pixel(fb, x + r - cx,     y + h-1-r + cy, color); // BL
        fb_draw_pixel(fb, x + r - cy,     y + h-1-r + cx, color);
        fb_draw_pixel(fb, x + w-1-r + cx, y + h-1-r + cy, color); // BR
        fb_draw_pixel(fb, x + w-1-r + cy, y + h-1-r + cx, color);
        if (err <= 0) { cy++; err += 2*cy + 1; }
        if (err > 0)  { cx--; err -= 2*cx + 1; }
    }
}

void fb_draw_text(Framebuffer *fb, int x, int y, const char *text, uint32_t color, int scale) {
    x += fb->draw_offset_x;
    y += fb->draw_offset_y;
    int offset_x = 0;
    
    while (*text) {
        char c = *text;
        int idx = -1;
        
        // Map lowercase to uppercase (font only has ' ' through 'Z')
        if (c >= 'a' && c <= 'z') c -= 32;
        
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

/* -- Line drawing (Bresenham) ------------------------------------------- */
void fb_draw_line(Framebuffer *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    x0 += fb->draw_offset_x; y0 += fb->draw_offset_y;
    x1 += fb->draw_offset_x; y1 += fb->draw_offset_y;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fb_draw_pixel(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* -- Thick line --------------------------------------------------------- */
void fb_draw_thick_line(Framebuffer *fb, int x0, int y0, int x1, int y1,
                        int thickness, uint32_t color) {
    int half = thickness / 2;
    for (int d = -half; d <= half; d++) {
        int dx = abs(x1 - x0), dy = abs(y1 - y0);
        if (dx >= dy) {
            fb_draw_line(fb, x0, y0 + d, x1, y1 + d, color);
        } else {
            fb_draw_line(fb, x0 + d, y0, x1 + d, y1, color);
        }
    }
}

/* -- Alpha-blended pixel ------------------------------------------------ */
void fb_draw_pixel_alpha(Framebuffer *fb, int x, int y, uint32_t color, uint8_t alpha) {
    x += fb->draw_offset_x;
    y += fb->draw_offset_y;
    if (x < 0 || x >= (int)fb->width || y < 0 || y >= (int)fb->height) return;
    uint32_t *target = fb->double_buffering ? fb->back_buffer : fb->buffer;
    uint32_t dst = target[y * fb->width + x];
    uint32_t sr = (color >> 16) & 0xFF, sg = (color >> 8) & 0xFF, sb = color & 0xFF;
    uint32_t dr = (dst   >> 16) & 0xFF, dg = (dst   >> 8) & 0xFF, db = dst   & 0xFF;
    uint32_t a = alpha, ia = 255 - alpha;
    uint32_t r = (sr * a + dr * ia) / 255;
    uint32_t g = (sg * a + dg * ia) / 255;
    uint32_t b = (sb * a + db * ia) / 255;
    target[y * fb->width + x] = (r << 16) | (g << 8) | b;
}

/* -- Alpha-blended filled rect ------------------------------------------ */
void fb_fill_rect_alpha(Framebuffer *fb, int x, int y, int w, int h,
                        uint32_t color, uint8_t alpha) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_draw_pixel_alpha(fb, x + i, y + j, color, alpha);
}

/* -- Vertical gradient filled rect -------------------------------------- */
void fb_fill_rect_gradient(Framebuffer *fb, int x, int y, int w, int h,
                           uint32_t top_color, uint32_t bottom_color) {
    x += fb->draw_offset_x;
    y += fb->draw_offset_y;
    uint32_t tr = (top_color >> 16) & 0xFF, tg = (top_color >> 8) & 0xFF, tb = top_color & 0xFF;
    uint32_t br = (bottom_color >> 16) & 0xFF, bg = (bottom_color >> 8) & 0xFF, bb = bottom_color & 0xFF;
    for (int j = 0; j < h; j++) {
        uint32_t r = tr + (br - tr) * j / (h > 1 ? h - 1 : 1);
        uint32_t g = tg + (bg - tg) * j / (h > 1 ? h - 1 : 1);
        uint32_t b = tb + (bb - tb) * j / (h > 1 ? h - 1 : 1);
        uint32_t c = (r << 16) | (g << 8) | b;
        for (int i = 0; i < w; i++)
            fb_draw_pixel(fb, x + i, y + j, c);
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

/* ======================================================================
 * Sprite Blitting Functions
 * ====================================================================== */

void fb_blit_sprite(Framebuffer *fb, const uint32_t *src_pixels, int src_w,
                    int sx, int sy, int dx, int dy, int w, int h,
                    uint32_t color_key) {
    int off_x = fb->draw_offset_x;
    int off_y = fb->draw_offset_y;
    int fb_w  = (int)fb->width;
    int fb_h  = (int)fb->height;
    uint32_t *target = fb->double_buffering ? fb->back_buffer : fb->buffer;

    for (int row = 0; row < h; row++) {
        int dest_y = dy + off_y + row;
        if (dest_y < 0) continue;
        if (dest_y >= fb_h) break;

        int src_row_offset = (sy + row) * src_w + sx;

        for (int col = 0; col < w; col++) {
            int dest_x = dx + off_x + col;
            if (dest_x < 0) continue;
            if (dest_x >= fb_w) break;

            uint32_t pixel = src_pixels[src_row_offset + col];
            if (pixel == color_key) continue;

            target[dest_y * fb_w + dest_x] = pixel;
        }
    }
}

void fb_blit_sprite_flipped(Framebuffer *fb, const uint32_t *src_pixels, int src_w,
                            int sx, int sy, int dx, int dy, int w, int h,
                            uint32_t color_key) {
    int off_x = fb->draw_offset_x;
    int off_y = fb->draw_offset_y;
    int fb_w  = (int)fb->width;
    int fb_h  = (int)fb->height;
    uint32_t *target = fb->double_buffering ? fb->back_buffer : fb->buffer;

    for (int row = 0; row < h; row++) {
        int dest_y = dy + off_y + row;
        if (dest_y < 0) continue;
        if (dest_y >= fb_h) break;

        int src_row_offset = (sy + row) * src_w;

        for (int col = 0; col < w; col++) {
            int dest_x = dx + off_x + col;
            if (dest_x < 0) continue;
            if (dest_x >= fb_w) break;

            /* Read source pixels right-to-left (horizontal flip) */
            uint32_t pixel = src_pixels[src_row_offset + (sx + w - 1 - col)];
            if (pixel == color_key) continue;

            target[dest_y * fb_w + dest_x] = pixel;
        }
    }
}

void fb_blit_sprite_scaled(Framebuffer *fb, const uint32_t *src_pixels, int src_w,
                           int sx, int sy, int sw, int sh,
                           int dx, int dy, int dw, int dh,
                           uint32_t color_key) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

    int off_x = fb->draw_offset_x;
    int off_y = fb->draw_offset_y;
    int fb_w  = (int)fb->width;
    int fb_h  = (int)fb->height;
    uint32_t *target = fb->double_buffering ? fb->back_buffer : fb->buffer;

    for (int j = 0; j < dh; j++) {
        int dest_y = dy + off_y + j;
        if (dest_y < 0) continue;
        if (dest_y >= fb_h) break;

        /* Nearest-neighbor: map dest row to source row */
        int src_y = sy + (j * sh) / dh;

        for (int i = 0; i < dw; i++) {
            int dest_x = dx + off_x + i;
            if (dest_x < 0) continue;
            if (dest_x >= fb_w) break;

            /* Nearest-neighbor: map dest col to source col */
            int src_x = sx + (i * sw) / dw;

            uint32_t pixel = src_pixels[src_y * src_w + src_x];
            if (pixel == color_key) continue;

            target[dest_y * fb_w + dest_x] = pixel;
        }
    }
}
