/*
 * audio_touch_test — Continuous Theremin
 *
 * Purpose: verify streaming audio with a continuous-pitch theremin.
 *
 * Screen layout:
 *   The play area is a colour-coded pad — touch and slide to play.
 *   X axis (left→right): 200 Hz → 2000 Hz (continuous glissando)
 *
 *   Touch down  → sound begins with fade-in
 *   Touch held  → pitch follows finger X position smoothly
 *   Touch up    → sound fades out and stops
 *
 *   A frequency bar below the pad shows the current pitch.
 *   Twinkling stars in the margins.  Exit button top-right.
 *
 * Run on device:
 *   /opt/games/audio_touch_test /dev/fb0 /dev/input/touchscreen0
 *
 * Build (from native_apps/):
 *   arm-linux-gnueabihf-gcc -O2 -static -I. \
 *     tests/audio_touch_test.c \
 *     common/audio.c common/touch_input.c common/framebuffer.c \
 *     common/hardware.c common/common.c \
 *     -o build/audio_touch_test -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>

#include "../common/audio.h"
#include "../common/touch_input.h"
#include "../common/framebuffer.h"
#include "../common/hardware.h"
#include "../common/common.h"
#include "../common/logger.h"

static Logger logger;

/* ── layout ──────────────────────────────────────────────────────────────── */
#define SCREEN_W   800
#define SCREEN_H   480

/* Play-area rectangle */
#define PAD_X      (SCREEN_SAFE_LEFT + 5)
#define PAD_Y      70
#define PAD_W      (SCREEN_SAFE_WIDTH - 10)
#define PAD_H      310

/* Frequency mapping */
#define FREQ_MIN   200
#define FREQ_MAX   2000

/* Number of visual column colour bands */
#define NUM_COLS   16

/* ── state ───────────────────────────────────────────────────────────────── */
static volatile bool running = true;
static void sig_handler(int s) { (void)s; running = false; }

typedef struct {
    int      x, y;       /* screen coords of current touch */
    int      freq;       /* current frequency */
    bool     active;     /* currently touching/playing */
} TapState;

/* ── colour helpers ──────────────────────────────────────────────────────── */

/* Map column index → rainbow hue (indigo at 0, red at NUM_COLS-1) */
static uint32_t col_color(int col)
{
    float t   = (float)col / (NUM_COLS - 1);
    float hue = 260.0f - t * 260.0f;   /* 260 (indigo) → 0 (red) */
    float s = 0.85f, v = 0.55f;
    float c = v * s;
    float xc = c * (1.0f - fabsf(fmodf(hue / 60.0f, 2.0f) - 1.0f));
    float m  = v - c;
    float r = 0, g = 0, b = 0;
    if      (hue < 60)  { r=c;  g=xc; b=0;  }
    else if (hue < 120) { r=xc; g=c;  b=0;  }
    else if (hue < 180) { r=0;  g=c;  b=xc; }
    else if (hue < 240) { r=0;  g=xc; b=c;  }
    else if (hue < 300) { r=xc; g=0;  b=c;  }
    else                { r=c;  g=0;  b=xc; }
    return RGB((int)((r+m)*255), (int)((g+m)*255), (int)((b+m)*255));
}

/* Linear blend between two colours (t 0=a, 255=b) */
static uint32_t blend(uint32_t a, uint32_t b, int t)
{
    int ra=(a>>16)&0xff, ga=(a>>8)&0xff, ba=a&0xff;
    int rb=(b>>16)&0xff, gb=(b>>8)&0xff, bb=b&0xff;
    return RGB(ra+(rb-ra)*t/255, ga+(gb-ga)*t/255, ba+(bb-ba)*t/255);
}

/* ── mapping ─────────────────────────────────────────────────────────────── */

static int freq_from_x(int x)
{
    int rel = x - PAD_X;
    if (rel < 0) rel = 0;
    if (rel > PAD_W) rel = PAD_W;
    return FREQ_MIN + (rel * (FREQ_MAX - FREQ_MIN)) / PAD_W;
}

static bool in_pad(int x, int y)
{
    return x >= PAD_X && x < PAD_X + PAD_W &&
           y >= PAD_Y && y < PAD_Y + PAD_H;
}

/* ── draw routines ───────────────────────────────────────────────────────── */

static void draw_stars(Framebuffer *fb, uint32_t t)
{
    /* Twinkling dots in the side margins */
    static const int sx[] = { 15, 750, 25, 770, 10, 780, 18, 760, 12, 775 };
    static const int sy[] = { 50, 50, 150, 120, 260, 200, 380, 350, 440, 430 };
    for (int i = 0; i < 10; i++) {
        uint32_t tt = t / 400 + i * 137;
        int bright = 80 + (int)(sinf((float)tt * 0.7f) * 60.0f);
        if (bright < 20) bright = 20;
        uint32_t c = RGB(bright, bright, bright);
        fb_draw_pixel(fb, sx[i],     sy[i],     c);
        fb_draw_pixel(fb, sx[i]+1,   sy[i],     c);
        fb_draw_pixel(fb, sx[i],     sy[i]+1,   c);
    }
}

static void draw_header(Framebuffer *fb)
{
    const char *title = "THEREMIN";
    int tw = text_measure_width(title, 3);
    fb_draw_text(fb, SCREEN_W/2 - tw/2, SCREEN_SAFE_TOP + 8,
                 title, RGB(255, 200, 80), 3);

    fb_draw_text(fb, PAD_X,                  PAD_Y - 18,
                 "< LOW",  RGB(100, 100, 255), 1);
    const char *rlab = "HIGH >";
    fb_draw_text(fb, PAD_X + PAD_W - text_measure_width(rlab, 1),
                 PAD_Y - 18, rlab, RGB(255, 80, 80), 1);
    const char *flab = "FREQUENCY";
    fb_draw_text(fb, PAD_X + PAD_W/2 - text_measure_width(flab, 1)/2,
                 PAD_Y - 18, flab, RGB(160, 160, 160), 1);
}

static void draw_pad(Framebuffer *fb, const TapState *tap)
{
    /* Colour bands with smooth vertical gradient */
    for (int c = 0; c < NUM_COLS; c++) {
        int bx  = PAD_X + (c * PAD_W) / NUM_COLS;
        int bx2 = PAD_X + ((c+1) * PAD_W) / NUM_COLS;
        uint32_t base = col_color(c);
        int col_w = bx2 - bx - 1;
        for (int row_y = 0; row_y < PAD_H; row_y++) {
            int blend_factor = (row_y * 56) / PAD_H;
            uint32_t colour = blend(base, COLOR_BLACK, blend_factor);
            fb_fill_rect(fb, bx, PAD_Y + row_y, col_w, 1, colour);
        }
    }

    /* Pad border */
    fb_draw_rect(fb, PAD_X-2, PAD_Y-2, PAD_W+4, PAD_H+4, COLOR_WHITE);

    /* While playing: vertical indicator line + dot at touch position */
    if (tap->active) {
        /* Vertical indicator line spanning full pad height */
        uint32_t line_col = RGB(255, 255, 255);
        for (int ly = PAD_Y; ly < PAD_Y + PAD_H; ly++) {
            /* Semi-transparent: blend with a dimmer shade away from touch Y */
            int dist = abs(ly - tap->y);
            int alpha = 255 - (dist * 200 / PAD_H);
            if (alpha < 40) alpha = 40;
            uint32_t c = blend(line_col, COLOR_BLACK, 255 - alpha);
            fb_draw_pixel(fb, tap->x, ly, c);
            fb_draw_pixel(fb, tap->x - 1, ly, blend(c, COLOR_BLACK, 80));
            fb_draw_pixel(fb, tap->x + 1, ly, blend(c, COLOR_BLACK, 80));
        }

        /* Dot at current touch position */
        fb_fill_circle(fb, tap->x, tap->y, 12, COLOR_WHITE);
        fb_draw_circle(fb, tap->x, tap->y, 14, RGB(200, 200, 255));
        fb_draw_circle(fb, tap->x, tap->y, 15, RGB(150, 150, 200));
    }
}

static void draw_freq_bar(Framebuffer *fb, const TapState *tap)
{
    int bar_y = PAD_Y + PAD_H + 14;
    int bar_h = 22;

    /* Track */
    fb_fill_rect(fb, PAD_X,   bar_y,   PAD_W,   bar_h,   RGB(20, 20, 20));
    fb_draw_rect(fb, PAD_X-1, bar_y-1, PAD_W+2, bar_h+2, RGB(60, 60, 60));

    if (!tap->active) return;

    int fill    = (tap->freq - FREQ_MIN) * PAD_W / (FREQ_MAX - FREQ_MIN);
    int col_idx = (tap->freq - FREQ_MIN) * (NUM_COLS-1) / (FREQ_MAX - FREQ_MIN);
    if (col_idx < 0) col_idx = 0;
    if (col_idx >= NUM_COLS) col_idx = NUM_COLS - 1;
    fb_fill_rect(fb, PAD_X, bar_y, fill, bar_h, col_color(col_idx));

    /* Sine squiggle on top of the bar */
    for (int px = 0; px < fill - 1; px++) {
        float phase = (float)px / fill * 6.0f * 3.14159f;
        int bump_y  = bar_y + bar_h/2 - (int)(sinf(phase) * (bar_h/3));
        fb_draw_pixel(fb, PAD_X + px, bump_y,     COLOR_WHITE);
        fb_draw_pixel(fb, PAD_X + px, bump_y + 1, RGB(200, 200, 200));
    }
}

static void draw_info(Framebuffer *fb, const TapState *tap)
{
    if (tap->active) {
        char info[64];
        snprintf(info, sizeof(info), "FREQ: %4d HZ", tap->freq);
        int iw = text_measure_width(info, 2);
        fb_draw_text(fb, SCREEN_W/2 - iw/2, PAD_Y + PAD_H + 42,
                     info, COLOR_YELLOW, 2);
    } else {
        const char *hint = "TOUCH AND SLIDE TO PLAY";
        int hw = text_measure_width(hint, 2);
        fb_draw_text(fb, SCREEN_W/2 - hw/2, PAD_Y + PAD_H + 42,
                     hint, RGB(120, 120, 120), 2);
    }
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *fb_dev    = (argc > 1) ? argv[1] : "/dev/fb0";
    const char *touch_dev = (argc > 2) ? argv[2] : "/dev/input/touchscreen0";

    logger_init(&logger, "audio_touch_test", LOG_LEVEL_DEBUG, true);

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    Framebuffer fb;
    if (fb_init(&fb, fb_dev) < 0) {
        LOG_ERROR(&logger, "Cannot open framebuffer: %s", fb_dev); return 1;
    }

    TouchInput touch;
    if (touch_init(&touch, touch_dev) < 0) {
        LOG_ERROR(&logger, "Cannot open touch: %s", touch_dev);
        fb_close(&fb); return 1;
    }
    touch_set_screen_size(&touch, SCREEN_W, SCREEN_H);

    Audio audio;
    if (audio_init(&audio) != 0) {
        LOG_ERROR(&logger, "Cannot open audio");
        touch_close(&touch); fb_close(&fb); return 1;
    }
    audio.logger = &logger;  /* wire up logger for streaming diagnostics */

    /* Exit button — top-right */
    Button exit_btn;
    button_init_full(&exit_btn,
                     SCREEN_W - BTN_EXIT_WIDTH - screen_safe_margin_right,
                     SCREEN_SAFE_TOP + 5,
                     BTN_EXIT_WIDTH, BTN_EXIT_HEIGHT,
                     "", BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR, 2);

    TapState tap      = { 0 };
    bool was_playing   = false;
    int  last_log_freq = 0;         /* for throttled debug output */

    LOG_INFO(&logger, "THEREMIN  fb=%s  touch=%s", fb_dev, touch_dev);

    while (running) {
        touch_poll(&touch);
        TouchState s   = touch_get_state(&touch);
        uint32_t   now = get_time_ms();

        /* ── exit button (always checked) ── */
        if (s.pressed) {
            bool exit_touched = button_is_touched(&exit_btn, s.x, s.y);
            if (button_check_press(&exit_btn, exit_touched, now))
                running = false;
        }

        /* ── theremin logic ── */
        if (s.held && in_pad(s.x, s.y)) {
            int freq = freq_from_x(s.x);

            if (!was_playing) {
                /* Touch just started */
                audio_stream_start(&audio, freq);
                was_playing = true;

                LOG_INFO(&logger, "PLAY freq=%dHz", freq);
                last_log_freq = freq;
            } else {
                /* Finger moved or held — update frequency */
                audio_stream_set_freq(&audio, freq);

                /* Throttled debug: only log when frequency changes >50 Hz */
                if (abs(freq - last_log_freq) > 50) {
                    LOG_DEBUG(&logger, "SLIDE freq=%dHz", freq);
                    last_log_freq = freq;
                }
            }

            /* Feed audio chunk every frame */
            audio_stream_chunk(&audio);

            /* Update visual state */
            tap.x      = s.x;
            tap.y      = s.y;
            tap.freq   = freq;
            tap.active = true;
        } else {
            if (was_playing) {
                /* Touch released or moved outside pad */
                LOG_DEBUG(&logger, "stopping stream...");
                audio_stream_stop(&audio);
                was_playing = false;

                LOG_INFO(&logger, "STOP");
            }
            tap.active = false;
        }

        /* ── render ── */
        fb_clear(&fb, RGB(8, 8, 18));
        draw_stars(&fb, now);
        draw_header(&fb);
        draw_pad(&fb, &tap);
        draw_freq_bar(&fb, &tap);
        draw_info(&fb, &tap);
        draw_exit_button(&fb, &exit_btn);
        fb_swap(&fb);

        /* Frame pacing: streaming write provides some throttling,
         * but add a small sleep to prevent busy-spinning.
         * IMPORTANT: every path MUST sleep to yield CPU and prevent
         * starving other processes or triggering the hardware watchdog. */
        if (!was_playing)
            usleep(16000);  /* 16 ms idle — ~60 fps */
        else
            usleep(1000);   /* 1 ms active — keep feeding audio */
    }

    /* Clean shutdown */
    if (was_playing)
        audio_stream_stop(&audio);

    fb_fade_out(&fb);
    audio_close(&audio);
    touch_close(&touch);
    fb_close(&fb);
    logger_close(&logger);
    return 0;
}
