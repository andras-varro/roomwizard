/*
 * touch_trace — touch calibration diagnostic
 *
 * Draws a reference grid + safe-area rectangle + a big CENTER "EXIT" button,
 * and renders the live finger trail using the *calibrated* coordinates that
 * every app sees (touch->state.x/y). Every sample is logged to
 * /tmp/touch_trace.log as:
 *
 *     raw_x raw_y cal_x cal_y est_x est_y
 *
 * where est_* is the pure per-axis linear estimate (ignores the affine),
 * shown on screen as a red crosshair so you can see what the affine changed.
 *
 * How to read it:
 *   - Drag your finger slowly along each grid line and along the green
 *     safe-area border.  The YELLOW trail should track the line under your
 *     finger.  If the trail is uniformly shifted/compressed but still
 *     straight  -> offset/scale error (e.g. the bezel remap).  If straight
 *     lines come out curved or fan-shaped -> keystone/pincushion, which an
 *     affine transform cannot fix (needs a homography).
 *   - The header of the log records the active calibration (raw range,
 *     affine coefficients, bezel margins) for offline analysis.
 *
 * Exit: tap the CENTER button (the panel centre is the most accurate region
 * even when calibration is off), or Ctrl-C over SSH.
 *
 * Build: linked against native_apps/common (see build-and-deploy.sh).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include "../common/framebuffer.h"
#include "../common/touch_input.h"

#define LOG_PATH   "/tmp/touch_trace.log"
#define GRID_STEP  80
#define TRAIL_MAX  8000

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

typedef struct { int x, y; } Pt;

static Pt   trail[TRAIL_MAX];
static int  trail_n = 0;      /* number of valid points (capped at TRAIL_MAX) */
static int  trail_head = 0;   /* ring-buffer write index */

static void trail_push(int x, int y) {
    trail[trail_head].x = x;
    trail[trail_head].y = y;
    trail_head = (trail_head + 1) % TRAIL_MAX;
    if (trail_n < TRAIL_MAX) trail_n++;
}

int main(void) {
    Framebuffer fb;
    TouchInput touch;

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    if (fb_init(&fb, "/dev/fb0") < 0) {
        fprintf(stderr, "Failed to init framebuffer\n");
        return 1;
    }
    if (touch_init(&touch, "/dev/input/event0") < 0) {
        fprintf(stderr, "Failed to init touch\n");
        fb_close(&fb);
        return 1;
    }
    touch_set_screen_size(&touch, fb.width, fb.height);

    const int W = fb.width, H = fb.height;

    /* Raw range for the pure-linear reference estimate */
    int rgx = touch.raw_max_x - touch.raw_min_x; if (rgx <= 0) rgx = 4095;
    int rgy = touch.raw_max_y - touch.raw_min_y; if (rgy <= 0) rgy = 4095;

    /* Log header: record the active calibration for offline analysis */
    FILE *log = fopen(LOG_PATH, "w");
    if (log) {
        fprintf(log, "# touch_trace  screen=%dx%d  portrait=%d\n",
                W, H, touch.portrait_mode);
        fprintf(log, "# raw_range X[%d..%d] Y[%d..%d]\n",
                touch.raw_min_x, touch.raw_max_x,
                touch.raw_min_y, touch.raw_max_y);
        fprintf(log, "# calib_enabled=%d raw_range X[%d..%d] Y[%d..%d]\n",
                touch.calib.enabled,
                touch.raw_min_x, touch.raw_max_x,
                touch.raw_min_y, touch.raw_max_y);
        fprintf(log, "# bezel(UI) T=%d B=%d L=%d R=%d\n",
                touch.calib.bezel_top, touch.calib.bezel_bottom,
                touch.calib.bezel_left, touch.calib.bezel_right);
        fprintf(log, "# columns: raw_x raw_y cal_x cal_y est_x est_y\n");
        fflush(log);
    }

    /* Center EXIT button geometry */
    const int bw = 160, bh = 90;
    const int bx = W / 2 - bw / 2;
    const int by = H / 2 - bh / 2;

    printf("touch_trace running — logging to %s. Tap CENTER or Ctrl-C to exit.\n", LOG_PATH);

    int last_raw_x = -1, last_raw_y = -1, last_cal_x = -1, last_cal_y = -1;
    int last_est_x = -1, last_est_y = -1;

    while (!g_stop) {
        touch_poll(&touch);
        TouchState st = touch_get_state(&touch);

        if (st.held) {
            int raw_x = touch.last_x, raw_y = touch.last_y;
            int cal_x = st.x,         cal_y = st.y;
            int est_x = (raw_x - touch.raw_min_x) * W / rgx;
            int est_y = (raw_y - touch.raw_min_y) * H / rgy;

            /* Only record/log when the sample actually changed */
            if (raw_x != last_raw_x || raw_y != last_raw_y ||
                cal_x != last_cal_x || cal_y != last_cal_y) {
                trail_push(cal_x, cal_y);
                if (log) {
                    fprintf(log, "%d %d %d %d %d %d\n",
                            raw_x, raw_y, cal_x, cal_y, est_x, est_y);
                    fflush(log);
                }
                last_raw_x = raw_x; last_raw_y = raw_y;
                last_cal_x = cal_x; last_cal_y = cal_y;
            }
            last_est_x = est_x; last_est_y = est_y;
        }

        /* Exit on a fresh press inside the center button */
        if (st.pressed &&
            st.x >= bx && st.x < bx + bw &&
            st.y >= by && st.y < by + bh) {
            break;
        }

        /* ---- render ---- */
        fb_clear(&fb, COLOR_BLACK);

        /* Grid */
        uint32_t grid = RGB(45, 45, 60);
        for (int x = 0; x <= W; x += GRID_STEP)
            fb_draw_line(&fb, x, 0, x, H - 1, grid);
        for (int y = 0; y <= H; y += GRID_STEP)
            fb_draw_line(&fb, 0, y, W - 1, y, grid);
        /* Grid coordinate labels (dim) */
        for (int x = 0; x <= W; x += GRID_STEP) {
            char b[12]; snprintf(b, sizeof(b), "%d", x);
            fb_draw_text(&fb, x + 2, 2, b, RGB(90, 90, 110), 1);
        }
        for (int y = 0; y <= H; y += GRID_STEP) {
            char b[12]; snprintf(b, sizeof(b), "%d", y);
            fb_draw_text(&fb, 2, y + 2, b, RGB(90, 90, 110), 1);
        }

        /* Safe-area rectangle (green) — what apps treat as usable */
        int sl = screen_safe_margin_left, st_ = screen_safe_margin_top;
        int sr = screen_safe_margin_right, sb = screen_safe_margin_bottom;
        fb_draw_rect(&fb, sl, st_, W - sl - sr, H - st_ - sb, COLOR_GREEN);

        /* Finger trail (calibrated coords, yellow) */
        for (int i = 0; i < trail_n; i++) {
            fb_fill_circle(&fb, trail[i].x, trail[i].y, 2, COLOR_YELLOW);
        }

        /* Current point markers */
        if (st.held) {
            /* calibrated (cyan) */
            fb_fill_circle(&fb, last_cal_x, last_cal_y, 5, COLOR_CYAN);
            /* pure-linear estimate (red crosshair) for comparison */
            for (int i = -10; i <= 10; i++) {
                fb_draw_pixel(&fb, last_est_x + i, last_est_y, COLOR_RED);
                fb_draw_pixel(&fb, last_est_x, last_est_y + i, COLOR_RED);
            }
        }

        /* Center EXIT button */
        fb_fill_rect(&fb, bx, by, bw, bh, RGB(150, 0, 150));
        fb_draw_rect(&fb, bx, by, bw, bh, COLOR_WHITE);
        fb_draw_text(&fb, W / 2 - 24, by + bh / 2 - 8, "EXIT", COLOR_WHITE, 2);

        /* Live readout (top, centered above the button area) */
        char rb[64];
        snprintf(rb, sizeof(rb), "RAW %4d,%4d", last_raw_x, last_raw_y);
        fb_draw_text(&fb, W / 2 - 150, 20, rb, COLOR_WHITE, 2);
        snprintf(rb, sizeof(rb), "CAL %4d,%4d", last_cal_x, last_cal_y);
        fb_draw_text(&fb, W / 2 - 150, 44, rb, COLOR_CYAN, 2);
        snprintf(rb, sizeof(rb), "LIN %4d,%4d", last_est_x, last_est_y);
        fb_draw_text(&fb, W / 2 + 20, 44, rb, COLOR_RED, 2);
        snprintf(rb, sizeof(rb), "samples:%d", trail_n);
        fb_draw_text(&fb, W / 2 - 150, 68, rb, COLOR_GRAY, 1);

        fb_swap(&fb);
        usleep(8000);
    }

    if (log) fclose(log);
    printf("touch_trace exiting — log saved to %s\n", LOG_PATH);

    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    touch_close(&touch);
    fb_close(&fb);
    return 0;
}
