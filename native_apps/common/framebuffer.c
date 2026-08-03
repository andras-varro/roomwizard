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

// Runtime bezel margins (pixels hidden by the plastic bezel)
int screen_bezel_top    = FB_BEZEL_TOP_DEFAULT;
int screen_bezel_bottom = FB_BEZEL_BOTTOM_DEFAULT;
int screen_bezel_left   = FB_BEZEL_LEFT_DEFAULT;
int screen_bezel_right  = FB_BEZEL_RIGHT_DEFAULT;

// Panel and viewport geometry. The defaults form an IDENTITY viewport so any
// code that reads these before fb_init() gets plain panel coordinates.
int screen_panel_width  = 800;
int screen_panel_height = 480;
int screen_view_x = 0;
int screen_view_y = 0;

// Logical (visible) screen dimensions
int screen_base_width  = 800;
int screen_base_height = 480;

// Visible-but-not-touchable band at each logical edge. Zero until touch_input.c
// measures it, so an app that never initialises touch treats the whole logical
// screen as safe — which is also the right answer on an uncalibrated device.
int screen_touch_inset_top    = 0;
int screen_touch_inset_bottom = 0;
int screen_touch_inset_left   = 0;
int screen_touch_inset_right  = 0;

static int clamp_inset(const char *side, int v) {
    if (v < 0) return 0;
    if (v > FB_TOUCH_INSET_MAX) {
        // Loud, because the alternative is every UI in the system quietly
        // shrinking to fit a broken calibration.
        printf("Touch inset: %s %d px exceeds the %d px limit — clamped. "
               "The calibration is almost certainly wrong; recalibrate.\n",
               side, v, FB_TOUCH_INSET_MAX);
        return FB_TOUCH_INSET_MAX;
    }
    return v;
}

void fb_set_touch_inset(int top, int bottom, int left, int right) {
    screen_touch_inset_top    = clamp_inset("top", top);
    screen_touch_inset_bottom = clamp_inset("bottom", bottom);
    screen_touch_inset_left   = clamp_inset("left", left);
    screen_touch_inset_right  = clamp_inset("right", right);
}

void fb_load_bezel(void) {
    // No file, or no margin line in it, means "not configured" → defaults.
    // A margin line that IS present is honoured exactly, zeros included.
    screen_bezel_top    = FB_BEZEL_TOP_DEFAULT;
    screen_bezel_bottom = FB_BEZEL_BOTTOM_DEFAULT;
    screen_bezel_left   = FB_BEZEL_LEFT_DEFAULT;
    screen_bezel_right  = FB_BEZEL_RIGHT_DEFAULT;

    FILE *f = fopen("/etc/touch_calibration.conf", "r");
    if (!f) {
        printf("Bezel: no calibration file — using defaults T=%d B=%d L=%d R=%d\n",
               screen_bezel_top, screen_bezel_bottom,
               screen_bezel_left, screen_bezel_right);
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
                screen_bezel_top    = t;
                screen_bezel_bottom = b;
                screen_bezel_left   = l;
                screen_bezel_right  = r;
            }
            break;
        }
    }
    fclose(f);

    printf("Bezel: margins T=%d B=%d L=%d R=%d from /etc/touch_calibration.conf\n",
           screen_bezel_top, screen_bezel_bottom,
           screen_bezel_left, screen_bezel_right);
}

// Recompute the logical surface from the panel dims + current bezel margins and
// publish the result to both fb and the globals. Margins that would leave less
// than a sixteenth of an axis usable are rejected as a typo/misconfiguration.
static int fb_apply_viewport(Framebuffer *fb, int panel_w, int panel_h) {
    int lw = panel_w - screen_bezel_left - screen_bezel_right;
    int lh = panel_h - screen_bezel_top  - screen_bezel_bottom;

    if (screen_bezel_top < 0 || screen_bezel_bottom < 0 ||
        screen_bezel_left < 0 || screen_bezel_right < 0 ||
        lw < panel_w / 16 || lh < panel_h / 16) {
        return -1;
    }

    fb->view_x = screen_bezel_left;
    fb->view_y = screen_bezel_top;
    fb->width  = (uint32_t)lw;
    fb->height = (uint32_t)lh;

    screen_panel_width  = panel_w;
    screen_panel_height = panel_h;
    screen_view_x       = fb->view_x;
    screen_view_y       = fb->view_y;
    screen_base_width   = lw;
    screen_base_height  = lh;
    return 0;
}

// Paint the whole panel black, so the bezel bands are clean and nothing from a
// previously running app survives outside the logical surface.
static void fb_black_panel(Framebuffer *fb) {
    if (fb->buffer != MAP_FAILED && fb->buffer != NULL)
        memset(fb->buffer, 0, fb->screen_size);
}

int fb_set_bezel(Framebuffer *fb, int top, int bottom, int left, int right) {
    int old_t = screen_bezel_top,  old_b = screen_bezel_bottom;
    int old_l = screen_bezel_left, old_r = screen_bezel_right;

    screen_bezel_top    = top;
    screen_bezel_bottom = bottom;
    screen_bezel_left   = left;
    screen_bezel_right  = right;

    int panel_w = fb->portrait_mode ? (int)fb->phys_height : (int)fb->phys_width;
    int panel_h = fb->portrait_mode ? (int)fb->phys_width  : (int)fb->phys_height;

    if (fb_apply_viewport(fb, panel_w, panel_h) < 0) {
        screen_bezel_top    = old_t;
        screen_bezel_bottom = old_b;
        screen_bezel_left   = old_l;
        screen_bezel_right  = old_r;
        fb_apply_viewport(fb, panel_w, panel_h);
        return -1;
    }

    size_t new_size = (size_t)fb->width * fb->height * fb->bytes_per_pixel;
    if (new_size != fb->back_buffer_size) {
        uint32_t *nb = (uint32_t *)realloc(fb->back_buffer, new_size);
        if (nb == NULL) {
            screen_bezel_top    = old_t;
            screen_bezel_bottom = old_b;
            screen_bezel_left   = old_l;
            screen_bezel_right  = old_r;
            fb_apply_viewport(fb, panel_w, panel_h);
            return -1;
        }
        fb->back_buffer = nb;
        fb->back_buffer_size = new_size;
    }
    memset(fb->back_buffer, 0, fb->back_buffer_size);
    fb_black_panel(fb);
    return 0;
}

bool fb_is_portrait_mode(void) {
    return access("/opt/games/portrait.mode", F_OK) == 0;
}

// Full printable ASCII 5x7 bitmap font (ASCII 32–126, 95 glyphs)
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
    // ASCII 91–96: [ \ ] ^ _ `
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    // ASCII 97–122: lowercase a–z
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    // ASCII 123–126: { | } ~
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x10, 0x08, 0x08, 0x10, 0x08}, // ~
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

    // Load bezel margins from calibration config
    fb_load_bezel();

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

    // Panel dimensions in the app's orientation
    int panel_w, panel_h;
    if (fb->portrait_mode) {
        // Apps see swapped dimensions (e.g., 480x800 instead of 800x480)
        panel_w = fb->phys_height;
        panel_h = fb->phys_width;
        printf("Portrait mode: physical %dx%d -> virtual %dx%d\n",
               fb->phys_width, fb->phys_height, panel_w, panel_h);

        // Rotate the bezel margins into the virtual coordinate system (90 CCW):
        // physical left -> virtual top, right -> bottom, top -> right, bottom -> left
        int pt = screen_bezel_top, pb = screen_bezel_bottom;
        int pl = screen_bezel_left, pr = screen_bezel_right;
        screen_bezel_top    = pl;
        screen_bezel_bottom = pr;
        screen_bezel_left   = pb;
        screen_bezel_right  = pt;

        printf("Portrait mode: rotated bezel margins T=%d B=%d L=%d R=%d\n",
               screen_bezel_top, screen_bezel_bottom,
               screen_bezel_left, screen_bezel_right);
    } else {
        panel_w = fb->phys_width;
        panel_h = fb->phys_height;
    }

    // Shrink the drawing surface to the visible rectangle. Bad margins fall
    // back to no bezel rather than leaving the app with an unusable screen.
    if (fb_apply_viewport(fb, panel_w, panel_h) < 0) {
        fprintf(stderr, "Bezel margins T=%d B=%d L=%d R=%d unusable on a %dx%d panel"
                        " — ignoring them\n",
                screen_bezel_top, screen_bezel_bottom,
                screen_bezel_left, screen_bezel_right, panel_w, panel_h);
        screen_bezel_top = screen_bezel_bottom = 0;
        screen_bezel_left = screen_bezel_right = 0;
        fb_apply_viewport(fb, panel_w, panel_h);
    }

    fb->bytes_per_pixel = vinfo.bits_per_pixel / 8;
    fb->line_length = finfo.line_length;
    fb->screen_size = fb->line_length * fb->phys_height;  // Physical size for mmap
    fb->back_buffer_size = (size_t)fb->width * fb->height * fb->bytes_per_pixel;

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

    // Black the whole panel once: clears the bezel bands and any leftovers.
    fb_black_panel(fb);

    printf("Framebuffer initialized: %dx%d logical at (%d,%d) on a %dx%d panel%s, %d bpp\n",
           fb->width, fb->height, fb->view_x, fb->view_y, panel_w, panel_h,
           fb->portrait_mode ? " [portrait]" : "", vinfo.bits_per_pixel);
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
    if (!fb->double_buffering || fb->back_buffer == NULL)
        return;

    if (fb->portrait_mode) {
        // 90 CCW rotation from the logical surface into the panel, offset by the
        // viewport: logical (lx, ly) -> virtual (lx+view_x, ly+view_y)
        //                            -> physical (vy, phys_height-1-vx)
        // 32bpp only, like the rest of the drawing primitives.
        uint32_t lw = fb->width;
        uint32_t lh = fb->height;
        uint32_t pw = fb->phys_width;
        uint32_t ph_minus_1 = fb->phys_height - 1;

        for (uint32_t ly = 0; ly < lh; ly++) {
            uint32_t *src_row = fb->back_buffer + ly * lw;
            uint32_t px = ly + fb->view_y;   // Physical X = virtual Y
            for (uint32_t lx = 0; lx < lw; lx++) {
                uint32_t py = ph_minus_1 - (lx + fb->view_x);
                fb->buffer[py * pw + px] = src_row[lx];
            }
        }
        return;
    }

    if (fb->view_x == 0 && fb->view_y == 0 &&
        fb->width == fb->phys_width && fb->height == fb->phys_height &&
        fb->line_length == fb->width * fb->bytes_per_pixel) {
        // No bezel and no line padding: one straight copy
        memcpy(fb->buffer, fb->back_buffer, fb->back_buffer_size);
        return;
    }

    // Letterboxed: copy row by row into the visible rectangle. Byte-based so it
    // is correct at any bpp (ScummVM runs this path at 16bpp).
    const uint32_t bpp = fb->bytes_per_pixel;
    const uint32_t row_bytes = fb->width * bpp;
    const uint8_t *src = (const uint8_t *)fb->back_buffer;
    uint8_t *dst = (uint8_t *)fb->buffer
                 + (size_t)fb->view_y * fb->line_length
                 + (size_t)fb->view_x * bpp;

    for (uint32_t y = 0; y < fb->height; y++) {
        memcpy(dst, src, row_bytes);
        src += row_bytes;
        dst += fb->line_length;
    }
}

void fb_clear(Framebuffer *fb, uint32_t color) {
    /* Clear the back buffer if double buffering is enabled */
    uint32_t *target = fb->double_buffering ? fb->back_buffer : fb->buffer;
    uint32_t total = fb->width * fb->height;
    if (color == 0) {
        /* Fast path: memset for black (all-zero). Sized in bytes so it stays
         * inside the allocation at any bpp. */
        memset(target, 0, fb->double_buffering ? fb->back_buffer_size
                                               : fb->screen_size);
    } else {
        /* Per-pixel fill for non-zero colours */
        for (uint32_t i = 0; i < total; i++) {
            target[i] = color;
        }
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
        int idx = c - ' ';
        
        if (idx < 0 || idx >= 95) {
            offset_x += 6 * scale;
            text++;
            continue;
        }
        
        {
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
    /* The channel deltas MUST be signed. A descending gradient (bottom channel
     * darker than top) makes bottom < top, and an unsigned subtraction wraps to
     * ~4.29e9 instead of going negative. The division that follows destroys the
     * modular-arithmetic equivalence that would otherwise have rescued it, so
     * every row got a garbage colour rather than a ramp. */
    int tr = (int)((top_color >> 16) & 0xFF);
    int tg = (int)((top_color >>  8) & 0xFF);
    int tb = (int)( top_color        & 0xFF);
    int dr = (int)((bottom_color >> 16) & 0xFF) - tr;
    int dg = (int)((bottom_color >>  8) & 0xFF) - tg;
    int db = (int)( bottom_color        & 0xFF) - tb;
    int span = (h > 1) ? h - 1 : 1;
    for (int j = 0; j < h; j++) {
        /* j <= span, so |d*j/span| <= |d| and each channel stays between its
         * two endpoints — both already masked to 0..255. No clamp needed. */
        uint32_t r = (uint32_t)(tr + dr * j / span);
        uint32_t g = (uint32_t)(tg + dg * j / span);
        uint32_t b = (uint32_t)(tb + db * j / span);
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
