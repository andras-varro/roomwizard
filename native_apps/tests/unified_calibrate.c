/*
 * Unified Calibration Utility  (9-crosshair tap + least-squares)
 *
 * Calibration is the raw→PANEL stage of the touch map (see common/touch_input.h).
 * We tap 9 known crosshairs, capture the raw reading at each, and least-squares
 * fit a line per axis. The fit is extrapolated out to panel 0 and panel dim-1
 * (touch_fit_axis_range), so tapping the visible (inset) targets still reaches
 * the true corners on a linear panel — no edge-drag needed. A summary then shows
 * each target (green square) and where the calibrated touch lands (red dot) with
 * the max error; ACCEPT saves, REDO re-taps.
 *
 * Targets are drawn in logical coordinates (that is what the user can see and
 * reach) but converted to panel coordinates before fitting, so the bezel never
 * gets baked into the calibration.
 *
 * Saves to /etc/touch_calibration.conf: raw range on line 1, bezel margins on
 * line 2 (round-tripped unchanged — adjust them in Device Tools → Set Screen).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "../common/framebuffer.h"
#include "../common/touch_input.h"

#define CALIB_FILE "/etc/touch_calibration.conf"
#define TAP_INSET  40      // targets this many px from the edge (clear the bezel)

static void draw_crosshair(Framebuffer *fb, int x, int y, uint32_t color) {
    const int s = 20;
    fb_draw_line(fb, x - s, y, x + s, y, color);
    fb_draw_line(fb, x, y - s, x, y + s, color);
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            fb_draw_pixel(fb, x + dx, y + dy, color);
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int build_targets(Framebuffer *fb, int *tx, int *ty) {
    int W = fb->width, H = fb->height, I = TAP_INSET;
    int px[9] = { I, W-I, W-I, I,   W/2, W/2, W/2, I,   W-I };
    int py[9] = { I, I,   H-I, H-I, H/2, I,   H-I, H/2, H/2 };
    for (int i = 0; i < 9; i++) { tx[i] = px[i]; ty[i] = py[i]; }
    return 9;
}

// One calibration pass: tap 9 crosshairs, fit, show summary.
// Returns 1=accept, 0=redo, -1=abort.
static int calibrate_once(Framebuffer *fb, TouchInput *touch) {
    const int W = fb->width, H = fb->height;
    int tx[9], ty[9];
    int NP = build_targets(fb, tx, ty);
    int rawx[9], rawy[9];

    // Rough map for feedback while tapping; the fit replaces it.
    touch_set_raw_range(touch, 0, 4095, 0, 4095);
    touch_drain_events(touch);

    for (int i = 0; i < NP; i++) {
        fb_clear(fb, COLOR_BLACK);
        fb_draw_text(fb, W/2 - 120, 24, "TAP EACH CROSS", COLOR_CYAN, 3);
        char c[32]; snprintf(c, sizeof(c), "%d / %d", i + 1, NP);
        fb_draw_text(fb, W/2 - 24, 60, c, COLOR_WHITE, 2);
        for (int j = i + 1; j < NP; j++)
            draw_crosshair(fb, tx[j], ty[j], RGB(55,55,75));
        draw_crosshair(fb, tx[i], ty[i], COLOR_WHITE);
        fb_swap(fb);

        int rx, ry;
        if (touch_wait_for_press_raw(touch, &rx, &ry) < 0) return -1;
        rawx[i] = rx; rawy[i] = ry;

        fb_clear(fb, COLOR_BLACK);
        draw_crosshair(fb, tx[i], ty[i], COLOR_GREEN);
        fb_swap(fb);
        usleep(120000);
    }

    // Calibration maps raw onto the PANEL, so fit against panel coordinates:
    // shift the (logical) targets by the viewport origin and use the panel dims.
    int pnx[9], pny[9];
    for (int i = 0; i < NP; i++) {
        pnx[i] = tx[i] + fb->view_x;
        pny[i] = ty[i] + fb->view_y;
    }

    // Per-axis least-squares; extrapolate to panel 0 and dim-1 for full reach.
    int mnx, mxx, mny, mxy;
    if (touch_fit_axis_range(rawx, pnx, NP, screen_panel_width,  &mnx, &mxx) == 0 &&
        touch_fit_axis_range(rawy, pny, NP, screen_panel_height, &mny, &mxy) == 0) {
        touch_set_raw_range(touch, mnx, mxx, mny, mxy);
    }

    int max_err = 0;
    for (int i = 0; i < NP; i++) {
        int lx = rawx[i], ly = rawy[i];
        touch_map_raw(touch, &lx, &ly);   // the production path, logical coords
        int ex = lx - tx[i], ey = ly - ty[i];
        int err = (ex < 0 ? -ex : ex) + (ey < 0 ? -ey : ey);
        if (err > max_err) max_err = err;
    }

    // Summary + ACCEPT / REDO
    const int bw = 200, bh = 70, gap = 40, ay = H/2 + 40;
    const int accept_x = W/2 - bw - gap/2, redo_x = W/2 + gap/2;
    touch_drain_events(touch);
    while (1) {
        fb_clear(fb, COLOR_BLACK);
        for (int i = 0; i < NP; i++) {
            int lx = rawx[i], ly = rawy[i];
            touch_map_raw(touch, &lx, &ly);
            fb_draw_rect(fb, tx[i]-6, ty[i]-6, 12, 12, COLOR_GREEN);
            fb_draw_line(fb, tx[i], ty[i], lx, ly, RGB(80,80,40));
            fb_fill_circle(fb, lx, ly, 3, COLOR_RED);
        }
        fb_draw_text(fb, W/2 - 150, H/2 - 80,
                     max_err <= 12 ? "LOOKS GOOD" : "GOOD ENOUGH?",
                     max_err <= 12 ? COLOR_GREEN : COLOR_ORANGE, 3);
        char s[64]; snprintf(s, sizeof(s), "MAX ERROR ~%d px", max_err);
        fb_draw_text(fb, W/2 - 110, H/2 - 40, s, COLOR_WHITE, 2);

        fb_fill_rect(fb, accept_x, ay, bw, bh, RGB(0,120,0));
        fb_draw_rect(fb, accept_x, ay, bw, bh, COLOR_WHITE);
        fb_draw_text(fb, accept_x + bw/2 - 42, ay + bh/2 - 8, "ACCEPT", COLOR_WHITE, 2);
        fb_fill_rect(fb, redo_x, ay, bw, bh, RGB(120,60,0));
        fb_draw_rect(fb, redo_x, ay, bw, bh, COLOR_WHITE);
        fb_draw_text(fb, redo_x + bw/2 - 24, ay + bh/2 - 8, "REDO", COLOR_WHITE, 2);
        fb_swap(fb);

        int px, py;
        if (touch_wait_for_press(touch, &px, &py) < 0) return -1;
        if (in_rect(px, py, accept_x, ay, bw, bh)) return 1;
        if (in_rect(px, py, redo_x, ay, bw, bh)) return 0;
    }
}

int main(void) {
    Framebuffer fb;
    TouchInput touch;

    printf("=== Unified Calibration (9-crosshair tap) ===\n");

    /* The common draw helpers write one uint32 per pixel, so the framebuffer
     * must be 32bpp — whatever ran last may have left it at 16. */
    fb_set_bpp("/dev/fb0", 32);

    if (fb_init(&fb, "/dev/fb0") < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    if (touch_init(&touch, "/dev/input/event0") < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&fb);
        return 1;
    }
    touch_set_screen_size(&touch, fb.width, fb.height);

    int accepted = 0;
    while (!accepted) {
        int r = calibrate_once(&fb, &touch);
        if (r < 0) break;
        accepted = r;   // 1 accept, 0 redo
    }

    if (accepted && touch_save_calibration(&touch, CALIB_FILE) == 0) {
        fb_clear(&fb, COLOR_BLACK);
        fb_draw_text(&fb, fb.width/2 - 200, fb.height/2 - 20,
                     "CALIBRATION SAVED", COLOR_GREEN, 3);
        fb_swap(&fb);
        sleep(2);
    } else if (accepted) {
        fb_clear(&fb, COLOR_BLACK);
        fb_draw_text(&fb, fb.width/2 - 160, fb.height/2 - 20,
                     "SAVE FAILED (run as root)", COLOR_RED, 2);
        fb_swap(&fb);
        sleep(2);
    }

    touch_close(&touch);
    fb_close(&fb);
    printf("Calibration complete.\n");
    return 0;
}
