/**
 * Hardware Test GUI for RoomWizard
 * 
 * Touch-based hardware diagnostic tool using UI framework.
 * Includes LED tests, display tests, touch zone diagnostics, and audio tests.
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include "common/hardware.h"
#include "common/common.h"
#include "common/audio.h"
#include "common/ui_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

/* ── Test states ────────────────────────────────────────────────────────── */

typedef enum {
    TEST_MENU,
    /* LED / backlight tests */
    TEST_LED_RED,
    TEST_LED_GREEN,
    TEST_LED_BOTH,
    TEST_BACKLIGHT,
    TEST_PULSE,
    TEST_BLINK,
    TEST_COLORS,
    /* New diagnostic tests */
    TEST_TOUCH_ZONE,
    TEST_DISPLAY,
    TEST_AUDIO_DIAG
} TestState;

#define NUM_TESTS 10

const char *test_names[] = {
    "RED LED",
    "GREEN LED",
    "BOTH LEDS",
    "BACKLIGHT",
    "PULSE",
    "BLINK",
    "COLORS",
    "TOUCH ZONE",
    "DISPLAY",
    "AUDIO DIAG"
};

// Draw test menu using UI framework
void draw_test_menu(Framebuffer *fb, UILayout *layout, Button *buttons, Button *exit_btn, int selected) {
    fb_clear(fb, COLOR_BLACK);
    
    // Title
    text_draw_centered(fb, 400, 30, "HARDWARE TEST", COLOR_WHITE, 3);
    
    // Draw exit button
    button_draw_exit(fb, exit_btn);
    
    // Draw test buttons using layout
    for (int i = 0; i < NUM_TESTS; i++) {
        int x, y, w, h;
        if (ui_layout_get_item_position(layout, i, &x, &y, &w, &h)) {
            // Update button position
            buttons[i].x = x;
            buttons[i].y = y;
            buttons[i].width = w;
            buttons[i].height = h;
            
            // Highlight selected
            if (i == selected) {
                buttons[i].visual_state = BTN_STATE_HIGHLIGHTED;
            } else {
                buttons[i].visual_state = BTN_STATE_NORMAL;
            }
            
            button_draw(fb, &buttons[i]);
        }
    }
    
    // Draw scroll indicators if needed
    ui_layout_draw_scroll_indicators(fb, layout);
    
    fb_swap(fb);
}

// Draw test screen with progress bar
void draw_test_screen(Framebuffer *fb, const char *title, const char *status, int progress) {
    fb_clear(fb, COLOR_BLACK);
    
    // Title
    text_draw_centered(fb, 400, 50, title, COLOR_WHITE, 3);
    
    // Status text
    if (status) {
        text_draw_centered(fb, 400, 150, status, COLOR_CYAN, 2);
    }
    
    // Progress bar
    if (progress >= 0) {
        int bar_width = 600;
        int bar_height = 40;
        int bar_x = (800 - bar_width) / 2;
        int bar_y = 220;
        
        // Background
        fb_draw_rect(fb, bar_x, bar_y, bar_width, bar_height, RGB(51, 51, 51));
        
        // Progress fill
        int fill_width = (bar_width * progress) / 100;
        fb_draw_rect(fb, bar_x, bar_y, fill_width, bar_height, COLOR_GREEN);
        
        // Percentage text
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", progress);
        text_draw_centered(fb, 400, bar_y + 20, pct, COLOR_WHITE, 2);
    }
    
    // Instructions
    text_draw_centered(fb, 400, 400, "TOUCH TO RETURN", RGB(136, 136, 136), 2);
    
    fb_swap(fb);
}

// Check if touch occurred (non-blocking)
bool check_touch(TouchInput *touch, int *x, int *y) {
    if (touch_poll(touch) > 0) {
        TouchState state = touch_get_state(touch);
        if (state.pressed) {
            *x = state.x;
            *y = state.y;
            return true;
        }
    }
    return false;
}

// Run red LED test
void test_red_led(Framebuffer *fb, TouchInput *touch) {
    for (int i = 0; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "RED LED TEST", status, i);
        hw_set_red_led(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_red_led(0);
            return;
        }
    }
    for (int i = 0; i < 20; i++) {
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_red_led(0);
            return;
        }
    }
    for (int i = 100; i >= 0; i -= 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "RED LED TEST", status, 100 - i);
        hw_set_red_led(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_red_led(0);
            return;
        }
    }
    hw_set_red_led(0);
    draw_test_screen(fb, "RED LED TEST", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run green LED test
void test_green_led(Framebuffer *fb, TouchInput *touch) {
    for (int i = 0; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "GREEN LED TEST", status, i);
        hw_set_green_led(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_green_led(0);
            return;
        }
    }
    for (int i = 0; i < 20; i++) {
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_green_led(0);
            return;
        }
    }
    for (int i = 100; i >= 0; i -= 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "GREEN LED TEST", status, 100 - i);
        hw_set_green_led(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_green_led(0);
            return;
        }
    }
    hw_set_green_led(0);
    draw_test_screen(fb, "GREEN LED TEST", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run both LEDs test
void test_both_leds(Framebuffer *fb, TouchInput *touch) {
    for (int i = 0; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "BOTH LEDS: %d%%", i);
        draw_test_screen(fb, "BOTH LEDS TEST", status, i);
        hw_set_leds(i, i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_leds_off();
            return;
        }
    }
    for (int i = 0; i < 20; i++) {
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_leds_off();
            return;
        }
    }
    for (int cycle = 0; cycle < 5; cycle++) {
        draw_test_screen(fb, "BOTH LEDS TEST", "RED ONLY", 50);
        hw_set_leds(100, 0);
        for (int i = 0; i < 10; i++) {
            usleep(50000);
            int x, y;
            if (check_touch(touch, &x, &y)) {
                hw_leds_off();
                return;
            }
        }
        draw_test_screen(fb, "BOTH LEDS TEST", "GREEN ONLY", 50);
        hw_set_leds(0, 100);
        for (int i = 0; i < 10; i++) {
            usleep(50000);
            int x, y;
            if (check_touch(touch, &x, &y)) {
                hw_leds_off();
                return;
            }
        }
    }
    hw_leds_off();
    draw_test_screen(fb, "BOTH LEDS TEST", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run backlight test
void test_backlight(Framebuffer *fb, TouchInput *touch) {
    int original = hw_get_backlight();
    for (int i = 100; i >= 20; i -= 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "BACKLIGHT TEST", status, 100 - i);
        hw_set_backlight(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_backlight(original);
            return;
        }
    }
    for (int i = 20; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "BACKLIGHT TEST", status, i);
        hw_set_backlight(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_backlight(original);
            return;
        }
    }
    hw_set_backlight(original);
    draw_test_screen(fb, "BACKLIGHT TEST", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run pulse test
void test_pulse(Framebuffer *fb, TouchInput *touch) {
    draw_test_screen(fb, "PULSE EFFECT", "PULSING GREEN LED...", 50);
    hw_pulse_led(LED_GREEN, 3000, 100);
    draw_test_screen(fb, "PULSE EFFECT", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run blink test
void test_blink(Framebuffer *fb, TouchInput *touch) {
    draw_test_screen(fb, "BLINK EFFECT", "BLINKING RED LED...", 50);
    hw_blink_led(LED_RED, 10, 200, 200, 100);
    draw_test_screen(fb, "BLINK EFFECT", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run color cycle test
void test_colors(Framebuffer *fb, TouchInput *touch) {
    const char *color_names[] = {"RED", "ORANGE", "YELLOW", "GREEN", "OFF"};
    const struct { uint8_t r; uint8_t g; } colors[] = {
        {100, 0}, {100, 50}, {100, 100}, {0, 100}, {0, 0}
    };
    for (int i = 0; i < 5; i++) {
        draw_test_screen(fb, "COLOR CYCLE", color_names[i], (i * 100) / 4);
        hw_set_leds(colors[i].r, colors[i].g);
        for (int j = 0; j < 20; j++) {
            usleep(50000);
            int x, y;
            if (check_touch(touch, &x, &y)) {
                hw_leds_off();
                return;
            }
        }
    }
    draw_test_screen(fb, "COLOR CYCLE", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Touch Zone Diagnostic                                                  */
/*                                                                         */
/*  Full-screen grid.  Tap each cell to mark it green.  Shows raw values,  */
/*  calibration state, and hardware range.  Unreachable zones stay red.     */
/* ════════════════════════════════════════════════════════════════════════ */

#define TZ_COLS 8
#define TZ_ROWS 6
#define TZ_CELL_W (800 / TZ_COLS)   /* 100 */
#define TZ_CELL_H (480 / TZ_ROWS)   /* 80  */
#define TZ_HEADER 36                 /* px reserved for info line at top */
#define CALIB_FILE "/etc/touch_calibration.conf"

static void test_touch_zone(Framebuffer *fb, TouchInput *touch) {
    /* Track which cells have been touched */
    bool hit[TZ_ROWS][TZ_COLS];
    memset(hit, 0, sizeof(hit));
    int hit_count = 0;
    int total_cells = TZ_ROWS * TZ_COLS;

    /* Load calibration if available */
    int calib_ok = (touch_load_calibration(touch, CALIB_FILE) == 0);
    if (calib_ok) touch_enable_calibration(touch, true);

    /* Remember last raw+scaled values for display */
    int last_raw_x = 0, last_raw_y = 0;
    int last_cal_x = 0, last_cal_y = 0;

    /* ── Draw loop ─────────────────────────────────────────────── */
    bool running = true;
    while (running) {
        /* ── Draw ──────────────────────────────────────────────── */
        fb_clear(fb, RGB(20, 20, 30));

        /* Info header */
        char hdr[128];
        snprintf(hdr, sizeof(hdr),
                 "Touch Zone  %d/%d  |  HW X[%d..%d] Y[%d..%d]  |  Calib: %s",
                 hit_count, total_cells,
                 touch->raw_min_x, touch->raw_max_x,
                 touch->raw_min_y, touch->raw_max_y,
                 calib_ok ? "ON" : "OFF");
        fb_draw_text(fb, 4, 2, hdr, COLOR_WHITE, 1);

        char val[80];
        snprintf(val, sizeof(val), "Last: raw(%d,%d) -> screen(%d,%d)",
                 last_raw_x, last_raw_y, last_cal_x, last_cal_y);
        fb_draw_text(fb, 4, 14, val, COLOR_CYAN, 1);

        /* Exit hint */
        fb_draw_text(fb, 640, 2, "[EXIT: top-right]", RGB(180,80,80), 1);

        /* Grid cells */
        for (int r = 0; r < TZ_ROWS; r++) {
            for (int c = 0; c < TZ_COLS; c++) {
                int cx = c * TZ_CELL_W;
                int cy = TZ_HEADER + r * TZ_CELL_H;
                int cw = TZ_CELL_W - 2;
                int ch = TZ_CELL_H - 2;

                uint32_t bg;
                if (hit[r][c])
                    bg = RGB(20, 120, 40);   /* green = touched */
                else
                    bg = RGB(80, 30, 30);    /* red = untouched */

                fb_fill_rect(fb, cx + 1, cy + 1, cw, ch, bg);
                fb_draw_rect(fb, cx + 1, cy + 1, cw, ch, RGB(70, 70, 90));

                /* Cell label (col,row) */
                char lbl[8];
                snprintf(lbl, sizeof(lbl), "%d,%d", c, r);
                fb_draw_text(fb, cx + 4, cy + 4, lbl, RGB(150,150,150), 1);
            }
        }

        /* Draw crosshair at last calibrated position */
        if (last_cal_x > 0 || last_cal_y > 0) {
            fb_draw_line(fb, last_cal_x - 12, last_cal_y, last_cal_x + 12, last_cal_y, COLOR_YELLOW);
            fb_draw_line(fb, last_cal_x, last_cal_y - 12, last_cal_x, last_cal_y + 12, COLOR_YELLOW);
        }

        /* Completion bar */
        int bar_w = (780 * hit_count) / total_cells;
        fb_fill_rect(fb, 10, 480 - 10, bar_w, 6,
                     (hit_count == total_cells) ? COLOR_GREEN : COLOR_CYAN);

        fb_swap(fb);

        /* ── Input ─────────────────────────────────────────────── */
        int x, y;
        if (touch_wait_for_press(touch, &x, &y) == 0) {
            last_cal_x = x;
            last_cal_y = y;
            /* Save the raw values that were used before scaling
             * (already applied — we approximate from the struct) */
            last_raw_x = touch->state.x;
            last_raw_y = touch->state.y;

            /* Exit zone: top-right 100×36 */
            if (x > 700 && y < TZ_HEADER) {
                running = false;
                break;
            }

            /* Map calibrated coords to grid cell */
            int gc = x / TZ_CELL_W;
            int gr = (y - TZ_HEADER) / TZ_CELL_H;
            if (gc >= 0 && gc < TZ_COLS && gr >= 0 && gr < TZ_ROWS) {
                if (!hit[gr][gc]) {
                    hit[gr][gc] = true;
                    hit_count++;
                }
            }
        }
        usleep(16000);
    }

    /* Disable calibration for other tests that don't expect it */
    touch_enable_calibration(touch, false);
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Display / Framebuffer Diagnostic                                       */
/*                                                                         */
/*  Shows current fb settings, then parades through:                       */
/*    1. Solid colour fills (R, G, B, W)                                   */
/*    2. Gradient ramp                                                     */
/*    3. 1-px grid pattern (aliasing stress test)                          */
/*    4. Safe-area boundary box                                            */
/* ════════════════════════════════════════════════════════════════════════ */

static void draw_display_page(Framebuffer *fb, const char *title,
                              const char *footer) {
    fb_draw_text(fb, 4, 2, title, COLOR_WHITE, 2);
    fb_draw_text(fb, 260, 460, footer, RGB(140,140,140), 1);
}

static void test_display(Framebuffer *fb, TouchInput *touch) {
    int page = 0;
    const int pages = 6;
    bool running = true;
    int x, y;

    /* Read current fb settings for info page */
    struct fb_var_screeninfo vinfo;
    ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo);

    while (running) {
        fb_clear(fb, COLOR_BLACK);

        switch (page) {
        case 0: { /* Info page */
            draw_display_page(fb, "DISPLAY INFO", "tap -> next | top-right -> exit");
            char buf[96];
            int row = 60;
            #define INFO_LINE(fmt, ...) \
                snprintf(buf, sizeof(buf), fmt, __VA_ARGS__); \
                fb_draw_text(fb, 40, row, buf, COLOR_CYAN, 2); row += 30;

            INFO_LINE("Resolution:  %dx%d", fb->width, fb->height);
            INFO_LINE("BPP:         %d", vinfo.bits_per_pixel);
            INFO_LINE("Line length: %d bytes", fb->line_length);
            INFO_LINE("Screen size: %d bytes", fb->screen_size);
            INFO_LINE("Bytes/pixel: %d", fb->bytes_per_pixel);
            INFO_LINE("Safe area:   (%d,%d)-(%d,%d)",
                       SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP,
                       SCREEN_SAFE_RIGHT, SCREEN_SAFE_BOTTOM);
            INFO_LINE("Double buf:  %s", fb->double_buffering ? "yes" : "no");
            #undef INFO_LINE
            break;
        }
        case 1: { /* Solid colour bars */
            draw_display_page(fb, "COLOR BARS", "tap -> next");
            int bw = 800 / 4;
            fb_fill_rect(fb, 0*bw, 40, bw, 400, RGB(255,0,0));
            fb_fill_rect(fb, 1*bw, 40, bw, 400, RGB(0,255,0));
            fb_fill_rect(fb, 2*bw, 40, bw, 400, RGB(0,0,255));
            fb_fill_rect(fb, 3*bw, 40, bw, 400, RGB(255,255,255));
            break;
        }
        case 2: { /* Horizontal gradient */
            draw_display_page(fb, "GRADIENT", "tap -> next");
            for (int col = 0; col < 800; col++) {
                uint8_t v = (col * 255) / 799;
                fb_fill_rect(fb, col, 50, 1, 380, RGB(v, v, v));
            }
            break;
        }
        case 3: { /* 1-pixel grid */
            draw_display_page(fb, "PIXEL GRID", "tap -> next");
            for (int gx = 0; gx < 800; gx += 2)
                fb_fill_rect(fb, gx, 40, 1, 400, RGB(200,200,200));
            for (int gy = 40; gy < 440; gy += 2)
                fb_fill_rect(fb, 0, gy, 800, 1, RGB(200,200,200));
            break;
        }
        case 4: { /* Safe-area boundary */
            draw_display_page(fb, "SAFE AREA", "tap -> next");
            /* Outer edge = screen boundary (red) */
            fb_draw_rect(fb, 0, 0, 800, 480, COLOR_RED);
            fb_draw_rect(fb, 1, 1, 798, 478, COLOR_RED);
            /* Inner edge = safe area (green) */
            fb_draw_rect(fb, SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP,
                         SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT, COLOR_GREEN);
            fb_draw_rect(fb, SCREEN_SAFE_LEFT+1, SCREEN_SAFE_TOP+1,
                         SCREEN_SAFE_WIDTH-2, SCREEN_SAFE_HEIGHT-2, COLOR_GREEN);
            /* Labels */
            char buf[32];
            snprintf(buf, sizeof(buf), "L=%d", SCREEN_SAFE_LEFT);
            fb_draw_text(fb, SCREEN_SAFE_LEFT + 4, 240, buf, COLOR_GREEN, 1);
            snprintf(buf, sizeof(buf), "R=%d", SCREEN_SAFE_RIGHT);
            fb_draw_text(fb, SCREEN_SAFE_RIGHT - 40, 240, buf, COLOR_GREEN, 1);
            snprintf(buf, sizeof(buf), "T=%d", SCREEN_SAFE_TOP);
            fb_draw_text(fb, 370, SCREEN_SAFE_TOP + 4, buf, COLOR_GREEN, 1);
            snprintf(buf, sizeof(buf), "B=%d", SCREEN_SAFE_BOTTOM);
            fb_draw_text(fb, 370, SCREEN_SAFE_BOTTOM - 16, buf, COLOR_GREEN, 1);
            break;
        }
        case 5: { /* Alpha / transparency */
            draw_display_page(fb, "ALPHA BLEND", "tap -> exit");
            fb_fill_rect(fb, 100, 80, 300, 300, COLOR_RED);
            fb_fill_rect(fb, 400, 80, 300, 300, COLOR_BLUE);
            fb_fill_rect_alpha(fb, 200, 150, 400, 200, RGB(0,255,0), 128);
            fb_draw_text(fb, 300, 250, "alpha=128", COLOR_WHITE, 2);
            break;
        }
        }

        fb_swap(fb);

        /* Wait for tap */
        while (1) {
            if (touch_wait_for_press(touch, &x, &y) == 0) {
                /* Exit zone: top-right */
                if (x > 700 && y < 40) { running = false; break; }
                page++;
                if (page >= pages) running = false;
                break;
            }
            usleep(16000);
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Audio Diagnostic                                                       */
/*                                                                         */
/*  Plays a sweep of frequencies and measures write timing.                 */
/* ════════════════════════════════════════════════════════════════════════ */

static void test_audio_diag(Framebuffer *fb, TouchInput *touch) {
    Audio audio;
    int audio_ok = (audio_init(&audio) == 0);

    const int freqs[] = { 200, 400, 600, 800, 1000, 1500, 2000, 3000 };
    const int nfreqs = sizeof(freqs) / sizeof(freqs[0]);
    int played = 0;
    bool running = true;
    int x, y;

    while (running && played <= nfreqs) {
        fb_clear(fb, RGB(20, 20, 30));
        fb_draw_text(fb, 4, 2, "AUDIO DIAGNOSTIC", COLOR_WHITE, 3);
        fb_draw_text(fb, 640, 4, "[EXIT]", RGB(180,80,80), 2);

        if (!audio_ok) {
            fb_draw_text(fb, 100, 120, "ERROR: /dev/dsp not available", COLOR_RED, 2);
            fb_draw_text(fb, 100, 160, "Audio subsystem failed to init.", COLOR_YELLOW, 2);
            fb_swap(fb);
            while (!check_touch(touch, &x, &y)) usleep(10000);
            break;
        }

        /* Draw frequency list with status */
        for (int i = 0; i < nfreqs; i++) {
            char buf[48];
            int row_y = 70 + i * 42;
            uint32_t col;

            if (i < played) {
                snprintf(buf, sizeof(buf), "%5d Hz   DONE", freqs[i]);
                col = COLOR_GREEN;
            } else if (i == played && played < nfreqs) {
                snprintf(buf, sizeof(buf), "%5d Hz   PLAYING ...", freqs[i]);
                col = COLOR_YELLOW;
            } else {
                snprintf(buf, sizeof(buf), "%5d Hz   ---", freqs[i]);
                col = RGB(100,100,100);
            }
            fb_draw_text(fb, 100, row_y, buf, col, 2);
        }

        if (played >= nfreqs) {
            fb_draw_text(fb, 200, 420, "ALL DONE - tap to exit", COLOR_CYAN, 2);
        } else {
            fb_draw_text(fb, 200, 420, "tap to skip/next", RGB(120,120,120), 2);
        }

        fb_swap(fb);

        if (played < nfreqs) {
            /* Play current frequency (non-blocking visual, blocking tone) */
            audio_interrupt(&audio);
            audio_tone(&audio, freqs[played], 300);
            played++;
            /* Brief pause, also check for tap */
            for (int w = 0; w < 10; w++) {
                usleep(30000);
                if (check_touch(touch, &x, &y)) {
                    if (x > 700 && y < 40) { running = false; break; }
                }
            }
        } else {
            /* Done — wait for exit tap */
            while (1) {
                if (touch_wait_for_press(touch, &x, &y) == 0) {
                    running = false;
                    break;
                }
                usleep(16000);
            }
        }
    }

    if (audio_ok) audio_close(&audio);
}

int main(void) {
    Framebuffer fb;
    if (fb_init(&fb, "/dev/fb0") < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    
    TouchInput touch;
    if (touch_init(&touch, "/dev/input/event0") < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&fb);
        return 1;
    }
    
    if (hw_init() < 0) {
        fprintf(stderr, "Warning: Hardware initialization issues\n");
    }
    
    // Create UI layout – 5 columns to fit NUM_TESTS (10) items in 2 rows
    // Item size: 150x80, spacing: 6x20
    // Width calc: 10 + (150*5) + (6*4) + 10 = 10 + 750 + 24 + 10 = 794 (fits in 800)
    UILayout layout;
    ui_layout_init_grid(&layout, 800, 480, 5, 150, 80, 6, 20, 10, 80, 10, 20);
    ui_layout_update(&layout, NUM_TESTS);
    
    // Create test buttons using unified Button type
    Button buttons[NUM_TESTS];
    for (int i = 0; i < NUM_TESTS; i++) {
        button_init_full(&buttons[i], 0, 0, 150, 80, test_names[i],
                        RGB(34, 34, 34), COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);
    }
    
    // Create exit button
    Button exit_btn;
    button_init_full(&exit_btn, 730, 10, 60, 50, "X",
                    BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR, 2);
    
    TestState state = TEST_MENU;
    int selected = 0;
    bool running = true;
    uint32_t last_time = 0;
    
    while (running) {
        if (state == TEST_MENU) {
            draw_test_menu(&fb, &layout, buttons, &exit_btn, selected);
            
            int x, y;
            if (check_touch(&touch, &x, &y)) {
                uint32_t now = last_time + 200;  // Simple debounce
                last_time = now;
                
                // Check exit button
                if (button_check_press(&exit_btn, button_is_touched(&exit_btn, x, y), now)) {
                    running = false;
                    continue;
                }
                
                // Check test buttons
                int item = ui_layout_get_item_at_position(&layout, x, y);
                if (item >= 0 && item < NUM_TESTS) {
                    selected = item;
                    state = TEST_LED_RED + item;
                    usleep(200000);
                }
            }
        } else {
            // Run selected test
            switch (state) {
                case TEST_LED_RED:     test_red_led(&fb, &touch); break;
                case TEST_LED_GREEN:   test_green_led(&fb, &touch); break;
                case TEST_LED_BOTH:    test_both_leds(&fb, &touch); break;
                case TEST_BACKLIGHT:   test_backlight(&fb, &touch); break;
                case TEST_PULSE:       test_pulse(&fb, &touch); break;
                case TEST_BLINK:       test_blink(&fb, &touch); break;
                case TEST_COLORS:      test_colors(&fb, &touch); break;
                case TEST_TOUCH_ZONE:  test_touch_zone(&fb, &touch); break;
                case TEST_DISPLAY:     test_display(&fb, &touch); break;
                case TEST_AUDIO_DIAG:  test_audio_diag(&fb, &touch); break;
                default: break;
            }
            state = TEST_MENU;
            hw_leds_off();
            usleep(200000);
        }
        usleep(10000);
    }
    
    hw_leds_off();
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    touch_close(&touch);
    fb_close(&fb);
    return 0;
}
