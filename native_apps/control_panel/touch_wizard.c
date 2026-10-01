/* touch_wizard.c — see touch_wizard.h. */
#include "touch_wizard.h"
#include "cp_ui.h"
#include "../common/touch_calib.h"
#include "../common/common.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define COLOR_DATA           COLOR_WHITE

/* ══════════════════════════════════════════════════════════════════════════
 * Screen geometry — why one wizard measures it
 * ══════════════════════════════════════════════════════════════════════════ */

/* Calibration and edge measurement are landscape-only; the portrait toggle that
 * makes them refuse is on the Display page (display_page.c).
 *
 * Geometry is measured by ONE wizard (touch_wizard_run) that writes both lines
 * of /etc/touch_calibration.conf. It replaced two separate flows that could be
 * — and were — measured against contradictory assumptions:
 *
 *   - a 9-tap calibration whose crosshairs sat 40 px in, inside the band where
 *     raw compresses, so the fit slope came out shallow and invented a
 *     horizontal inset that does not exist; and
 *   - a bezel adjuster that drew its reference frame on the *logical* edge,
 *     i.e. measured the bezel through the bezel.
 *
 * The wizard fixes both: it fits from interior targets only, and it runs with
 * the bezel zeroed so a drawn pixel is a panel pixel. The fit itself lives in
 * common/touch_calib.c, shared with the touch_raw diagnostic. */

/* ══════════════════════════════════════════════════════════════════════════
 * Calibration wizard  (full screen)
 * ══════════════════════════════════════════════════════════════════════════ */

/* Runs on the raw panel: fb_set_bezel(0,0,0,0), so a drawn pixel is a panel
 * pixel — the premise that made the touch_raw diagnostic trustworthy. Both
 * config lines are measured against it, which is what stops them contradicting
 * each other.
 *
 * Nothing is written until the operator has (a) accepted a fit that passed the
 * sanity gate and (b) confirmed, live on the new mapping, that the screen still
 * responds. Any timeout at any step puts everything back. */

typedef enum {
    WIZ_TAP,        /* tap the interior targets                     */
    WIZ_CHECK,      /* review the fit; accept / redo / reset        */
    WIZ_REACH,      /* sweep the four edges: what raw do they emit? */
    WIZ_EDGES,      /* measure the bezel against 2 px ladders       */
    WIZ_REPORT,     /* visible vs touchable                         */
    WIZ_CONFIRM,    /* live on the new mapping; keep or auto-revert */
    WIZ_EXIT
} WizStep;

#define WIZ_MAX_TARGETS  16          /* TOUCH_CALIB_N_TARGETS is 11 */
#define WIZ_TAP_RADIUS   120         /* a tap further off was not aimed at the target */
#define WIZ_IDLE_MS      60000       /* abandoned wizard reverts and exits */
#define WIZ_CONFIRM_MS   20000       /* "does it still work?" countdown */
#define WIZ_LADDER       44          /* depth of the 2 px edge ladders */
#define WIZ_BEZ_MAX      64          /* a margin larger than this is a misconfiguration */

/* WIZ_REACH treats a finger anywhere in the outer sixth of an axis as a sample
 * for that edge. Its own buttons therefore have to sit outside all four bands, on
 * BOTH axes — one button row serves edges that run horizontally and vertically.
 * The touch_raw diagnostic solved this at the same row. */
#define WIZ_SWEEP_BAND_DIV 6

/* -- bezel stepper: geometry and drawing kept apart, because the input pass
 *    needs the hit rects on a frame where nothing is drawn ----------------- */

#define BEZ_BTN_W 56
#define BEZ_BTN_H 48
#define BEZ_VAL_W 56

typedef struct { int minus_x, plus_x, y; } BezStepper;

static BezStepper bez_stepper_geom(int cx, int cy) {
    BezStepper s;
    s.y       = cy - BEZ_BTN_H / 2;
    s.minus_x = cx - BEZ_VAL_W / 2 - BEZ_BTN_W;
    s.plus_x  = cx + BEZ_VAL_W / 2;
    return s;
}

/* Draw "LABEL / [-] value [+]" centred on (cx, cy). */
static void draw_bez_stepper(Framebuffer *fb, int cx, int cy,
                             const char *label, int value) {
    BezStepper s = bez_stepper_geom(cx, cy);

    text_draw_centered(fb, cx, s.y - 22, label, COLOR_LABEL, 2);

    fb_fill_rect(fb, s.minus_x, s.y, BEZ_BTN_W, BEZ_BTN_H, RGB(60, 60, 90));
    fb_draw_rect(fb, s.minus_x, s.y, BEZ_BTN_W, BEZ_BTN_H, COLOR_WHITE);
    text_draw_centered(fb, s.minus_x + BEZ_BTN_W / 2, s.y + BEZ_BTN_H / 2,
                       "-", COLOR_WHITE, 3);

    fb_fill_rect(fb, s.plus_x, s.y, BEZ_BTN_W, BEZ_BTN_H, RGB(60, 60, 90));
    fb_draw_rect(fb, s.plus_x, s.y, BEZ_BTN_W, BEZ_BTN_H, COLOR_WHITE);
    text_draw_centered(fb, s.plus_x + BEZ_BTN_W / 2, s.y + BEZ_BTN_H / 2,
                       "+", COLOR_WHITE, 3);

    char v[8];
    snprintf(v, sizeof(v), "%d", value);
    text_draw_centered(fb, cx, s.y + BEZ_BTN_H / 2, v, COLOR_DATA, 3);
}

/* Where the four steppers sit, in panel coordinates. One function so the input
 * and render passes cannot drift apart. */
static void wiz_stepper_positions(int W, int H, int *cx, int *cy) {
    cx[0] = W / 2;       cy[0] = H / 2 - 110;   /* TOP    */
    cx[1] = W / 2;       cy[1] = H / 2 + 110;   /* BOTTOM */
    cx[2] = W / 2 - 190; cy[2] = H / 2;         /* LEFT   */
    cx[3] = W / 2 + 190; cy[3] = H / 2;         /* RIGHT  */
}

/* -- wizard buttons ------------------------------------------------------- */

typedef struct { int x, y, w, h; const char *label; uint32_t col; } WizBtn;

static void wiz_draw_btn(Framebuffer *fb, const WizBtn *b) {
    fb_fill_rounded_rect(fb, b->x, b->y, b->w, b->h, 8, b->col);
    fb_draw_rounded_rect(fb, b->x, b->y, b->w, b->h, 8, COLOR_WHITE);
    int tw = text_measure_width(b->label, 2);
    fb_draw_text(fb, b->x + (b->w - tw) / 2, b->y + b->h / 2 - 8, b->label, COLOR_WHITE, 2);
}

static bool wiz_hit(const WizBtn *b, int x, int y) {
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

/* Numbered 2 px ladder along one panel edge, plus the band the current margin
 * would hide. The operator does not count rungs — they raise the margin until
 * the yellow line clears the plastic. The ladder is there so "just clear"
 * becomes a number they can read back and sanity-check. */
static void wiz_draw_edge_ladder(Framebuffer *fb, int edge, int margin) {
    const int W = (int)fb->width, H = (int)fb->height;
    const uint32_t band = RGB(70, 0, 0), fine = RGB(70, 70, 95),
                   coarse = RGB(130, 130, 170), line = COLOR_YELLOW;
    char b[8];

    if (edge == 0 || edge == 1) {                       /* TOP / BOTTOM */
        int sgn  = (edge == 0) ? 1 : -1;
        int base = (edge == 0) ? 0 : H - 1;
        if (margin > 0)
            fb_fill_rect(fb, 0, (edge == 0) ? 0 : H - margin, W, margin, band);
        for (int d = 0; d <= WIZ_LADDER; d += 2) {
            int y = base + sgn * d;
            bool lab = (d % 10 == 0);
            fb_draw_line(fb, 0, y, lab ? 120 : 60, y, lab ? coarse : fine);
            fb_draw_line(fb, W - 1 - (lab ? 120 : 60), y, W - 1, y, lab ? coarse : fine);
            if (lab) {
                snprintf(b, sizeof(b), "%d", d);
                fb_draw_text(fb, 126, y - 3, b, coarse, 1);
                fb_draw_text(fb, W - 146, y - 3, b, coarse, 1);
            }
        }
        fb_draw_line(fb, 0, base + sgn * margin, W - 1, base + sgn * margin, line);
    } else {                                            /* LEFT / RIGHT */
        int sgn  = (edge == 2) ? 1 : -1;
        int base = (edge == 2) ? 0 : W - 1;
        if (margin > 0)
            fb_fill_rect(fb, (edge == 2) ? 0 : W - margin, 0, margin, H, band);
        for (int d = 0; d <= WIZ_LADDER; d += 2) {
            int x = base + sgn * d;
            bool lab = (d % 10 == 0);
            fb_draw_line(fb, x, 0, x, lab ? 90 : 45, lab ? coarse : fine);
            fb_draw_line(fb, x, H - 1 - (lab ? 90 : 45), x, H - 1, lab ? coarse : fine);
            if (lab) {
                snprintf(b, sizeof(b), "%d", d);
                fb_draw_text(fb, x - 3, 96, b, coarse, 1);
            }
        }
        fb_draw_line(fb, base + sgn * margin, 0, base + sgn * margin, H - 1, line);
    }
}

static void wiz_draw_target(Framebuffer *fb, int x, int y, uint32_t c) {
    fb_draw_circle(fb, x, y, 22, c);
    fb_draw_circle(fb, x, y, 21, c);
    fb_draw_circle(fb, x, y, 8, c);
    fb_draw_line(fb, x - 34, y, x + 34, y, c);
    fb_draw_line(fb, x, y - 34, x, y + 34, c);
    fb_fill_circle(fb, x, y, 2, c);
}

void touch_wizard_run(Framebuffer *fb, TouchInput *touch, bool edges_only,
                      const volatile bool *running, TouchWizardResult *out) {
    out->msg[0] = '\0';
    out->saved  = false;

    /* fb_init() rotates the margins into virtual space in portrait, so values
     * measured here would be saved rotated and re-rotated on the next start.
     * Calibration is landscape-only by design. */
    if (fb->portrait_mode) {
        fb_clear(fb, COLOR_BLACK);
        text_draw_centered(fb, (int)fb->width / 2, (int)fb->height / 2 - 20,
                           "CALIBRATE IN LANDSCAPE MODE", COLOR_YELLOW, 3);
        text_draw_centered(fb, (int)fb->width / 2, (int)fb->height / 2 + 20,
                           "TURN PORTRAIT OFF AND RELAUNCH", COLOR_YELLOW, 2);
        fb_swap(fb);
        sleep(3);
        return;
    }

    /* Everything needed to put the device back exactly as it was. */
    const int entry_rx0 = touch->raw_min_x, entry_rx1 = touch->raw_max_x;
    const int entry_ry0 = touch->raw_min_y, entry_ry1 = touch->raw_max_y;
    const int entry_kxl = touch->raw_knot_lo_x, entry_kxh = touch->raw_knot_hi_x;
    const int entry_kyl = touch->raw_knot_lo_y, entry_kyh = touch->raw_knot_hi_y;
    const int entry_gx0 = touch->reach_min_x, entry_gx1 = touch->reach_max_x;
    const int entry_gy0 = touch->reach_min_y, entry_gy1 = touch->reach_max_y;
    const int entry_bt = screen_bezel_top,  entry_bb = screen_bezel_bottom;
    const int entry_bl = screen_bezel_left, entry_br = screen_bezel_right;

    int hw_x0, hw_x1, hw_y0, hw_y1;
    touch_calib_hw_range(touch, &hw_x0, &hw_x1, &hw_y0, &hw_y1);

    if (fb_set_bezel(fb, 0, 0, 0, 0) < 0) {
        return;
    }
    touch_set_screen_size(touch, (int)fb->width, (int)fb->height);

    const int W = (int)fb->width, H = (int)fb->height;

    /* NOTE which mapping is in force. Throughout TAP, CHECK, EDGES and REPORT
     * the *entry* calibration stays installed, so every button on those screens
     * is hit-tested through a mapping already known to work. The fitted range
     * goes live only at WIZ_CONFIRM, behind a countdown. The old flow did the
     * opposite — it hit-tested ACCEPT/REDO through the new fit, so a bad fit
     * left neither of them pressable. */

    int tap_rx[WIZ_MAX_TARGETS][TOUCH_CALIB_TAPS];
    int tap_ry[WIZ_MAX_TARGETS][TOUCH_CALIB_TAPS];
    int med_rx[WIZ_MAX_TARGETS], med_ry[WIZ_MAX_TARGETS];
    memset(tap_rx, 0, sizeof(tap_rx));
    memset(tap_ry, 0, sizeof(tap_ry));
    memset(med_rx, 0, sizeof(med_rx));
    memset(med_ry, 0, sizeof(med_ry));

    TouchAxisFit fx, fy;
    TouchAxisCurve cvx, cvy;
    memset(&fx, 0, sizeof(fx));
    memset(&fy, 0, sizeof(fy));
    memset(&cvx, 0, sizeof(cvx));
    memset(&cvy, 0, sizeof(cvy));
    char verdict_x[128] = "", verdict_y[128] = "";
    bool reach_x = false, reach_y = false, fit_sane = false;

    /* Working values: the curve that will be written — endpoints plus the two
     * interior knots per axis. Start from what is in force, and on the full path
     * let the fit replace them at ACCEPT. */
    int new_rx0 = entry_rx0, new_rx1 = entry_rx1;
    int new_ry0 = entry_ry0, new_ry1 = entry_ry1;
    int new_kxl = entry_kxl, new_kxh = entry_kxh;
    int new_kyl = entry_kyl, new_kyh = entry_kyh;
    int bez_t = entry_bt, bez_b = entry_bb, bez_l = entry_bl, bez_r = entry_br;

    /* Measured edge reach: what raw the four physical edges actually emit. This
     * is what separates "the fit extrapolates past raw 4095" from "the sensor
     * stops responding 30 px before the edge" — the two look identical from a
     * bezel press, and confusing them is what kept the endpoint bug alive across
     * three sessions. Starts optimistic (every edge reaches the hardware limit)
     * and is replaced by WIZ_REACH's sweep. */
    TouchCalibSweep sweep[4];
    for (int e = 0; e < 4; e++) touch_calib_sweep_reset(&sweep[e], e);
    int new_gx0 = entry_gx0, new_gx1 = entry_gx1;
    int new_gy0 = entry_gy0, new_gy1 = entry_gy1;

    WizStep step = edges_only ? WIZ_EDGES : WIZ_TAP;
    int tgt_i = 0, tap_i = 0;
    bool saved = false;
    char msg[64] = "";   /* sized to AppState::status_msg, which it is copied into */

    uint32_t last_action = get_time_ms();
    uint32_t confirm_start = 0;

    /* The bottom row stops at y=440, clear of the ~449 panel row where the
     * digitiser stops on this hardware — a control the sensor cannot reach is
     * exactly the failure this wizard exists to prevent. */
    WizBtn b_cancel = { 40,  384, 150, 56, "CANCEL", RGB(110, 40, 40) };
    WizBtn b_redo   = { 210, 384, 150, 56, "REDO",   RGB(110, 80, 20) };
    WizBtn b_reset  = { 380, 384, 150, 56, "RESET",  RGB(90, 60, 110) };
    WizBtn b_next   = { 600, 384, 160, 56, "ACCEPT", RGB(30, 110, 60) };
    /* TAP owns the bottom row (a target sits at y=458), so its abort is inset
     * and placed further than WIZ_TAP_RADIUS from every target. */
    WizBtn b_abort  = { 700, 400, 90,  44, "STOP",   RGB(110, 40, 40) };
    /* REACH cannot use the row above: y=384..440 is inside the BOTTOM sweep band
     * (y > H*5/6 == 400), so pressing CANCEL would record its own tap as a bottom
     * edge extreme. This row sits clear of all four bands on both axes —
     * x in [133,666), y in [80,400). */
    WizBtn b_sw_cancel = { 200, 258, 130, 54, "CANCEL", RGB(110, 40, 40) };
    WizBtn b_sw_redo   = { 340, 258, 130, 54, "REDO",   RGB(110, 80, 20) };
    WizBtn b_sw_next   = { 480, 258, 130, 54, "NEXT",   RGB(30, 110, 60) };

    touch_drain_events(touch);

    while (*running && step != WIZ_EXIT) {
        uint32_t now = get_time_ms();
        touch_poll(touch);
        TouchState st = touch_get_state(touch);
        const int raw_x = touch->last_x, raw_y = touch->last_y;
        bool press = st.pressed && (now - last_action) > BTN_DEBOUNCE_MS;

        /* Abandoned at any step: put everything back rather than leave a
         * half-applied geometry on a wall-mounted screen. */
        uint32_t idle_limit = (step == WIZ_CONFIRM) ? WIZ_CONFIRM_MS : WIZ_IDLE_MS;
        if (now - last_action > idle_limit) {
            snprintf(msg, sizeof(msg), "TIMED OUT - NOTHING CHANGED");
            break;
        }

        /* ------------------------- input ------------------------- */
        if (step == WIZ_TAP) {
            if (press && wiz_hit(&b_abort, st.x, st.y)) {
                break;
            } else if (press) {
                int dx = st.x - TOUCH_CALIB_TARGETS[tgt_i].px;
                int dy = st.y - TOUCH_CALIB_TARGETS[tgt_i].py;
                if (dx * dx + dy * dy <= WIZ_TAP_RADIUS * WIZ_TAP_RADIUS) {
                    /* Record RAW. The installed mapping is irrelevant to the
                     * data, which is what lets the old one stay in force. */
                    tap_rx[tgt_i][tap_i] = raw_x;
                    tap_ry[tgt_i][tap_i] = raw_y;
                    last_action = now;
                    if (++tap_i >= TOUCH_CALIB_TAPS) {
                        med_rx[tgt_i] = touch_calib_median3(tap_rx[tgt_i][0],
                                                            tap_rx[tgt_i][1],
                                                            tap_rx[tgt_i][2]);
                        med_ry[tgt_i] = touch_calib_median3(tap_ry[tgt_i][0],
                                                            tap_ry[tgt_i][1],
                                                            tap_ry[tgt_i][2]);
                        tap_i = 0;
                        if (++tgt_i >= TOUCH_CALIB_N_TARGETS) {
                            /* Fit in PANEL coordinates from INTERIOR targets
                             * only. The interior restriction is the entire
                             * correction over the old 9-tap fit. */
                            int px[WIZ_MAX_TARGETS], py[WIZ_MAX_TARGETS];
                            bool ix[WIZ_MAX_TARGETS], iy[WIZ_MAX_TARGETS];
                            for (int i = 0; i < TOUCH_CALIB_N_TARGETS; i++) {
                                px[i] = TOUCH_CALIB_TARGETS[i].px;
                                py[i] = TOUCH_CALIB_TARGETS[i].py;
                                ix[i] = touch_calib_interior_x(px[i], W);
                                iy[i] = touch_calib_interior_y(py[i], H);
                            }
                            touch_calib_fit(&fx, med_rx, px, ix, TOUCH_CALIB_N_TARGETS, W);
                            touch_calib_fit(&fy, med_ry, py, iy, TOUCH_CALIB_N_TARGETS, H);
                            /* Turn each fitted line into the curve that gets
                             * written: knots on the line, endpoints AT the line.
                             * Endpoints outside 0..4095 are correct and are left
                             * alone — clamping them is what tilted the outer
                             * segments and made the cursor run ahead of the
                             * finger near the bottom edge. */
                            touch_calib_curve_from_fit(&fx, hw_x0, hw_x1, &cvx);
                            touch_calib_curve_from_fit(&fy, hw_y0, hw_y1, &cvy);
                            reach_x = touch_calib_axis_verdict(&cvx, hw_x0, hw_x1,
                                                "X", verdict_x, sizeof(verdict_x));
                            reach_y = touch_calib_axis_verdict(&cvy, hw_y0, hw_y1,
                                                "Y", verdict_y, sizeof(verdict_y));
                            fit_sane = fx.in_ok && fy.in_ok &&
                                touch_calib_range_sane(fx.in0, fx.in1, hw_x0, hw_x1) &&
                                touch_calib_range_sane(fy.in0, fy.in1, hw_y0, hw_y1);
                            step = WIZ_CHECK;
                        }
                    }
                }
            }
        } else if (step == WIZ_CHECK) {
            if (press) {
                last_action = now;
                if (wiz_hit(&b_cancel, st.x, st.y)) {
                    break;
                } else if (wiz_hit(&b_redo, st.x, st.y)) {
                    tgt_i = tap_i = 0;
                    step = WIZ_TAP;
                } else if (wiz_hit(&b_reset, st.x, st.y)) {
                    /* Fall back to what the hardware declares — a plain linear
                     * map over the emittable range. Imprecise in the middle, but
                     * always usable; the point is that there is a way out. Skip
                     * the edge sweep too: RESET is for getting out of here, and
                     * the hardware range is the right assumption to pair with a
                     * hardware-range map. */
                    new_rx0 = hw_x0; new_rx1 = hw_x1;
                    new_ry0 = hw_y0; new_ry1 = hw_y1;
                    new_kxl = new_kxh = 0;   /* no curve: plain linear map */
                    new_kyl = new_kyh = 0;
                    new_gx0 = hw_x0; new_gx1 = hw_x1;
                    new_gy0 = hw_y0; new_gy1 = hw_y1;
                    step = WIZ_EDGES;
                } else if (wiz_hit(&b_next, st.x, st.y) && fit_sane) {
                    new_rx0 = cvx.v0; new_rx1 = cvx.v1;
                    new_kxl = cvx.k_lo; new_kxh = cvx.k_hi;
                    new_ry0 = cvy.v0; new_ry1 = cvy.v1;
                    new_kyl = cvy.k_lo; new_kyh = cvy.k_hi;
                    step = WIZ_REACH;
                }
            }
        } else if (step == WIZ_REACH) {
            /* Accumulate while the finger is DOWN anywhere in the outer sixth of
             * an axis. Nothing is captured on lift: a sweep is the stroke itself,
             * and demanding a clean lift inside the band throws away the end of
             * every stroke. All four edges are live at once — a stroke along the
             * top cannot produce samples in the bottom band, so there is no need
             * to walk the operator through them one at a time. */
            if (st.held) {
                const int band_x = W / WIZ_SWEEP_BAND_DIV;
                const int band_y = H / WIZ_SWEEP_BAND_DIV;
                if (st.y < band_y)
                    touch_calib_sweep_add(&sweep[0], 0, raw_y, st.x, W, hw_y0);
                if (st.y >= H - band_y)
                    touch_calib_sweep_add(&sweep[1], 1, raw_y, st.x, W, hw_y1);
                if (st.x < band_x)
                    touch_calib_sweep_add(&sweep[2], 2, raw_x, st.y, H, hw_x0);
                if (st.x >= W - band_x)
                    touch_calib_sweep_add(&sweep[3], 3, raw_x, st.y, H, hw_x1);
            }
            if (press) {
                last_action = now;
                if (wiz_hit(&b_sw_cancel, st.x, st.y)) {
                    break;
                } else if (wiz_hit(&b_sw_redo, st.x, st.y)) {
                    for (int e = 0; e < 4; e++) touch_calib_sweep_reset(&sweep[e], e);
                } else if (wiz_hit(&b_sw_next, st.x, st.y)) {
                    /* An edge that was not swept falls back to the hardware limit
                     * — the optimistic assumption, which is what an unmeasured
                     * edge deserves. A swept edge that fell SHORT of the limit
                     * widens the reported dead band, which is the honest
                     * direction to be wrong in. */
                    new_gy0 = touch_calib_sweep_extreme_or(&sweep[0], 0, hw_y0);
                    new_gy1 = touch_calib_sweep_extreme_or(&sweep[1], 1, hw_y1);
                    new_gx0 = touch_calib_sweep_extreme_or(&sweep[2], 2, hw_x0);
                    new_gx1 = touch_calib_sweep_extreme_or(&sweep[3], 3, hw_x1);
                    step = WIZ_EDGES;
                }
            }
        } else if (step == WIZ_EDGES) {
            if (press) {
                last_action = now;
                if (wiz_hit(&b_cancel, st.x, st.y)) {
                    break;
                } else if (wiz_hit(&b_next, st.x, st.y)) {
                    step = WIZ_REPORT;
                } else {
                    /* 1 px per tap: the ladder is 2 px and every app's drawing
                     * surface derives from these four numbers, so they are
                     * worth getting right to the pixel. */
                    int cx[4], cy[4];
                    int *vals[4] = { &bez_t, &bez_b, &bez_l, &bez_r };
                    wiz_stepper_positions(W, H, cx, cy);
                    for (int i = 0; i < 4; i++) {
                        BezStepper s = bez_stepper_geom(cx[i], cy[i]);
                        if (st.y < s.y || st.y >= s.y + BEZ_BTN_H) continue;
                        if (st.x >= s.minus_x && st.x < s.minus_x + BEZ_BTN_W) {
                            if (*vals[i] > 0) (*vals[i])--;
                        } else if (st.x >= s.plus_x && st.x < s.plus_x + BEZ_BTN_W) {
                            if (*vals[i] < WIZ_BEZ_MAX) (*vals[i])++;
                        }
                    }
                }
            }
        } else if (step == WIZ_REPORT) {
            if (press) {
                last_action = now;
                if (wiz_hit(&b_cancel, st.x, st.y)) {
                    break;
                } else if (wiz_hit(&b_redo, st.x, st.y)) {
                    step = WIZ_EDGES;
                } else if (wiz_hit(&b_next, st.x, st.y)) {
                    /* Go live on the new geometry WITHOUT writing anything, and
                     * make the operator prove the screen still responds. The
                     * measured reach goes live too, so the trial has the same
                     * touch-safe inset the saved config would. */
                    touch_set_raw_curve(touch, new_rx0, new_kxl, new_kxh, new_rx1,
                                               new_ry0, new_kyl, new_kyh, new_ry1);
                    touch_set_edge_reach(touch, new_gx0, new_gx1, new_gy0, new_gy1);
                    fb_set_bezel(fb, bez_t, bez_b, bez_l, bez_r);
                    touch_set_screen_size(touch, (int)fb->width, (int)fb->height);
                    confirm_start = now;
                    step = WIZ_CONFIRM;
                }
            }
        } else if (step == WIZ_CONFIRM) {
            /* Geometry changed under us, so the button box is recomputed from
             * the live dimensions rather than the entry-time ones. */
            const int cw = (int)fb->width, ch = (int)fb->height;
            WizBtn keep = { cw / 2 - 150, ch / 2 + 10, 300, 70,
                            "KEEP THESE", RGB(30, 110, 60) };
            if (press && wiz_hit(&keep, st.x, st.y)) {
                char bak[256] = "";
                touch_calib_backup(CALIB_FILE, bak, sizeof(bak));
                touch->calib.bezel_top    = bez_t;
                touch->calib.bezel_bottom = bez_b;
                touch->calib.bezel_left   = bez_l;
                touch->calib.bezel_right  = bez_r;
                saved = (touch_save_calibration(touch, CALIB_FILE) == 0);
                snprintf(msg, sizeof(msg),
                         saved ? "SAVED - PREVIOUS CONFIG BACKED UP"
                               : "SAVE FAILED - RUN AS ROOT");
                step = WIZ_EXIT;
            }
        }
        if (step == WIZ_EXIT) break;

        /* ------------------------- render ------------------------- */
        fb_clear(fb, COLOR_BLACK);
        char b[96];

        if (step == WIZ_TAP) {
            fb_draw_text(fb, 20, 20, "TAP THE CENTRE OF EACH TARGET", COLOR_WHITE, 2);
            fb_draw_text(fb, 20, 44,
                         "TARGETS SIT WELL INSIDE THE EDGES ON PURPOSE - "
                         "RAW COMPRESSES NEAR THE BORDER", COLOR_GRAY, 1);
            snprintf(b, sizeof(b), "TARGET %d/%d   TAP %d/%d",
                     tgt_i + 1, TOUCH_CALIB_N_TARGETS, tap_i + 1, TOUCH_CALIB_TAPS);
            fb_draw_text(fb, 20, 62, b, COLOR_GREEN, 2);
            for (int i = tgt_i + 1; i < TOUCH_CALIB_N_TARGETS; i++)
                fb_draw_circle(fb, TOUCH_CALIB_TARGETS[i].px,
                               TOUCH_CALIB_TARGETS[i].py, 6, RGB(55, 55, 75));
            wiz_draw_btn(fb, &b_abort);
            /* Last, so nothing can bury the target that is being aimed at. */
            wiz_draw_target(fb, TOUCH_CALIB_TARGETS[tgt_i].px,
                            TOUCH_CALIB_TARGETS[tgt_i].py, COLOR_GREEN);

        } else if (step == WIZ_CHECK) {
            fb_draw_text(fb, 20, 18, "CALIBRATION RESULT", COLOR_WHITE, 3);
            snprintf(b, sizeof(b), "HARDWARE RAW  X[%d..%d]  Y[%d..%d]",
                     hw_x0, hw_x1, hw_y0, hw_y1);
            fb_draw_text(fb, 20, 50, b, COLOR_GRAY, 1);

            fb_draw_text(fb, 20, 76, "AXIS   CURVE  RAW AT 0 / 1-4 / 3-4 / MAX     REACHES",
                         COLOR_YELLOW, 1);
            int lo, hi;
            touch_calib_reach(&cvx, hw_x0, hw_x1, &lo, &hi);
            snprintf(b, sizeof(b), "X  %5d %5d %5d %5d      %4d ..%4d",
                     cvx.v0, cvx.k_lo, cvx.k_hi, cvx.v1, lo, hi);
            fb_draw_text(fb, 20, 94, b, COLOR_WHITE, 2);
            touch_calib_reach(&cvy, hw_y0, hw_y1, &lo, &hi);
            snprintf(b, sizeof(b), "Y  %5d %5d %5d %5d      %4d ..%4d",
                     cvy.v0, cvy.k_lo, cvy.k_hi, cvy.v1, lo, hi);
            fb_draw_text(fb, 20, 118, b, COLOR_WHITE, 2);

            /* Per axis, and about the FIT rather than the hardware: the sensor
             * reaches every edge, so what these lines report is how much edge
             * compression the outer segments had to bend around. */
            fb_draw_text(fb, 20, 150, verdict_x, reach_x ? COLOR_GREEN : COLOR_CYAN, 1);
            fb_draw_text(fb, 20, 166, verdict_y, reach_y ? COLOR_GREEN : COLOR_CYAN, 1);

            /* The edge probes never entered the fit, so their residual against
             * the fitted LINE is the only honest check on it. Large values here
             * are the edge compression itself, which the curve then corrects. */
            int r_xlo = touch_calib_predict_panel(med_rx[TOUCH_CALIB_PROBE_XLO],
                            fx.in0, fx.in1, W) - TOUCH_CALIB_TARGETS[TOUCH_CALIB_PROBE_XLO].px;
            int r_xhi = touch_calib_predict_panel(med_rx[TOUCH_CALIB_PROBE_XHI],
                            fx.in0, fx.in1, W) - TOUCH_CALIB_TARGETS[TOUCH_CALIB_PROBE_XHI].px;
            int r_ylo = touch_calib_predict_panel(med_ry[TOUCH_CALIB_PROBE_YLO],
                            fy.in0, fy.in1, H) - TOUCH_CALIB_TARGETS[TOUCH_CALIB_PROBE_YLO].py;
            int r_yhi = touch_calib_predict_panel(med_ry[TOUCH_CALIB_PROBE_YHI],
                            fy.in0, fy.in1, H) - TOUCH_CALIB_TARGETS[TOUCH_CALIB_PROBE_YHI].py;
            snprintf(b, sizeof(b), "EDGE-PROBE ERROR VS FITTED LINE   X %+d / %+d PX   Y %+d / %+d PX",
                     r_xlo, r_xhi, r_ylo, r_yhi);
            fb_draw_text(fb, 20, 190, b, COLOR_CYAN, 1);

            if (fit_sane)
                fb_draw_text(fb, 20, 214,
                             "ACCEPT KEEPS THIS FIT. NOTHING IS WRITTEN YET.",
                             COLOR_GRAY, 1);
            else
                fb_draw_text(fb, 20, 214,
                             "FIT REJECTED - IT BARELY OVERLAPS THE HARDWARE RANGE. "
                             "REDO, OR RESET.", COLOR_RED, 1);

            b_next.label = "ACCEPT";
            b_redo.label = "REDO";
            wiz_draw_btn(fb, &b_cancel);
            wiz_draw_btn(fb, &b_redo);
            wiz_draw_btn(fb, &b_reset);
            if (fit_sane) wiz_draw_btn(fb, &b_next);

        } else if (step == WIZ_REACH) {
            const int band_x = W / WIZ_SWEEP_BAND_DIV;
            const int band_y = H / WIZ_SWEEP_BAND_DIV;
            int all_done = 0;
            for (int e = 0; e < 4; e++) if (sweep[e].done) all_done++;

            /* Coverage cells laid ALONG each edge, so a gap in the stroke shows up
             * as a gap in the row rather than as a number that quietly never
             * arrives. Green = that stretch drove raw all the way to the limit. */
            for (int e = 0; e < 4; e++) {
                bool is_y     = touch_calib_sweep_edge_is_y(e);
                bool want_min = touch_calib_sweep_wants_min(e);
                int  limit    = is_y ? (want_min ? hw_y0 : hw_y1)
                                     : (want_min ? hw_x0 : hw_x1);
                for (int i = 0; i < TOUCH_CALIB_SWEEP_BUCKETS; i++) {
                    int cw2, ch2, cx2, cy2;
                    if (is_y) {
                        cw2 = W / TOUCH_CALIB_SWEEP_BUCKETS - 4;  ch2 = 14;
                        cx2 = i * (W / TOUCH_CALIB_SWEEP_BUCKETS) + 2;
                        cy2 = (e == 0) ? 6 : H - 20;
                    } else {
                        cw2 = 14;  ch2 = H / TOUCH_CALIB_SWEEP_BUCKETS - 4;
                        cy2 = i * (H / TOUCH_CALIB_SWEEP_BUCKETS) + 2;
                        cx2 = (e == 2) ? 6 : W - 20;
                    }
                    uint32_t col;
                    if (!sweep[e].bucket_hit[i])                     col = RGB(45, 45, 55);
                    else if (want_min ? (sweep[e].bucket[i] <= limit)
                                      : (sweep[e].bucket[i] >= limit)) col = COLOR_GREEN;
                    else                                             col = COLOR_ORANGE;
                    fb_fill_rect(fb, cx2, cy2, cw2, ch2, col);
                    fb_draw_rect(fb, cx2, cy2, cw2, ch2, RGB(90, 90, 110));
                }
            }
            /* The bands samples are taken from, so it is obvious where to slide. */
            fb_draw_line(fb, 0, band_y, W - 1, band_y, RGB(60, 60, 80));
            fb_draw_line(fb, 0, H - 1 - band_y, W - 1, H - 1 - band_y, RGB(60, 60, 80));
            fb_draw_line(fb, band_x, 0, band_x, H - 1, RGB(60, 60, 80));
            fb_draw_line(fb, W - 1 - band_x, 0, W - 1 - band_x, H - 1, RGB(60, 60, 80));

            fb_fill_rect(fb, 150, 76, W - 300, 170, RGB(10, 10, 14));
            fb_draw_rect(fb, 150, 76, W - 300, 170, RGB(60, 60, 80));
            fb_draw_text(fb, 164, 86, "SLIDE ONE FINGER ALONG EACH EDGE", COLOR_WHITE, 2);
            fb_draw_text(fb, 164, 108,
                         "THIS ASKS WHAT RAW THE PHYSICAL EDGE EMITS. A BEZEL PRESS "
                         "CANNOT TELL YOU:", COLOR_GRAY, 1);
            fb_draw_text(fb, 164, 122,
                         "IT READS THE SAME WHETHER THE SENSOR STOPS AT THE EDGE OR "
                         "30 PX INSIDE IT.", COLOR_GRAY, 1);

            int ry2 = 144;
            for (int e = 0; e < 4; e++) {
                bool is_y     = touch_calib_sweep_edge_is_y(e);
                bool want_min = touch_calib_sweep_wants_min(e);
                int  limit    = is_y ? (want_min ? hw_y0 : hw_y1)
                                     : (want_min ? hw_x0 : hw_x1);
                static const char *nm[4] = { "TOP", "BOTTOM", "LEFT", "RIGHT" };
                if (sweep[e].covered == 0)
                    snprintf(b, sizeof(b), "%-7s NOT SWEPT", nm[e]);
                else
                    snprintf(b, sizeof(b), "%-7s raw_%c %s %5d / %-5d  %2d/%d CELLS  %s",
                             nm[e], is_y ? 'y' : 'x', want_min ? "MIN" : "MAX",
                             sweep[e].extreme, limit,
                             sweep[e].covered, TOUCH_CALIB_SWEEP_BUCKETS,
                             touch_calib_sweep_reached(&sweep[e], e, limit)
                                 ? "REACHES" : "FALLS SHORT");
                fb_draw_text(fb, 164, ry2, b,
                             sweep[e].covered == 0 ? COLOR_GRAY
                             : touch_calib_sweep_reached(&sweep[e], e, limit)
                                 ? COLOR_GREEN : COLOR_ORANGE, 1);
                ry2 += 14;
            }
            snprintf(b, sizeof(b), "%d/4 EDGES SWEPT - %s", all_done,
                     all_done == 4 ? "PRESS NEXT"
                                   : "NEXT ASSUMES THE REST REACH THE LIMIT");
            fb_draw_text(fb, 164, ry2 + 6, b,
                         all_done == 4 ? COLOR_GREEN : COLOR_YELLOW, 1);

            b_sw_next.label = (all_done == 4) ? "NEXT" : "SKIP";
            wiz_draw_btn(fb, &b_sw_cancel);
            wiz_draw_btn(fb, &b_sw_redo);
            wiz_draw_btn(fb, &b_sw_next);

        } else if (step == WIZ_EDGES) {
            for (int e = 0; e < 4; e++) {
                int m = (e == 0) ? bez_t : (e == 1) ? bez_b : (e == 2) ? bez_l : bez_r;
                wiz_draw_edge_ladder(fb, e, m);
            }
            fb_draw_text(fb, 150, 58,
                         "RAISE EACH EDGE UNTIL ITS YELLOW LINE CLEARS THE PLASTIC",
                         COLOR_WHITE, 1);
            fb_draw_text(fb, 150, 74,
                         "THE DARK BAND IS WHAT GETS HIDDEN. THIS SCREEN IGNORES THE "
                         "CURRENT MARGINS, SO WHAT YOU SEE IS THE WHOLE PANEL.",
                         COLOR_GRAY, 1);

            int cx[4], cy[4];
            wiz_stepper_positions(W, H, cx, cy);
            draw_bez_stepper(fb, cx[0], cy[0], "TOP", bez_t);
            draw_bez_stepper(fb, cx[1], cy[1], "BOTTOM", bez_b);
            draw_bez_stepper(fb, cx[2], cy[2], "LEFT", bez_l);
            draw_bez_stepper(fb, cx[3], cy[3], "RIGHT", bez_r);
            snprintf(b, sizeof(b), "VISIBLE %dx%d",
                     W - bez_l - bez_r, H - bez_t - bez_b);
            text_draw_centered(fb, W / 2, H / 2, b, COLOR_DATA, 2);

            b_next.label = "NEXT";
            wiz_draw_btn(fb, &b_cancel);
            wiz_draw_btn(fb, &b_next);

        } else if (step == WIZ_REPORT) {
            fb_draw_text(fb, 20, 18, "VISIBLE VS TOUCHABLE", COLOR_WHITE, 3);

            TouchAxisCurve nx = { .v0 = new_rx0, .k_lo = new_kxl,
                                  .k_hi = new_kxh, .v1 = new_rx1,
                                  .overshoot_lo = 0, .overshoot_hi = 0, .dim = W };
            TouchAxisCurve ny = { .v0 = new_ry0, .k_lo = new_kyl,
                                  .k_hi = new_kyh, .v1 = new_ry1,
                                  .overshoot_lo = 0, .overshoot_hi = 0, .dim = H };

            /* Reach from the MEASURED edge extremes, not from the hardware limits:
             * if the sweep showed an edge never drives raw all the way, the band it
             * cannot address is wider than the curve alone implies. */
            int rx0, rx1, ry0, ry1;
            touch_calib_reach(&nx, new_gx0, new_gx1, &rx0, &rx1);
            touch_calib_reach(&ny, new_gy0, new_gy1, &ry0, &ry1);

            snprintf(b, sizeof(b), "VISIBLE    PANEL X %d..%d   Y %d..%d",
                     bez_l, W - 1 - bez_r, bez_t, H - 1 - bez_b);
            fb_draw_text(fb, 20, 58, b, COLOR_CYAN, 2);
            snprintf(b, sizeof(b), "TOUCHABLE  PANEL X %d..%d   Y %d..%d",
                     rx0, rx1, ry0, ry1);
            fb_draw_text(fb, 20, 84, b, COLOR_CYAN, 2);

            /* The inset: rows and columns you can SEE and DRAW ON but cannot
             * PRESS. On this hardware it is not zero and is not supposed to be —
             * the digitiser saturates before the panel edge. An earlier revision
             * demanded zero here and told the operator to REDO; it only ever read
             * zero because the endpoint clamp forced it to, and that clamp was the
             * bug. Amber only once the band is too wide to be the sensor. */
            int in_t, in_b, in_l, in_r;
            touch_calib_inset_from_reach(ry0, ry1, bez_t, H - bez_t - bez_b,
                                         &in_t, &in_b);
            touch_calib_inset_from_reach(rx0, rx1, bez_l, W - bez_l - bez_r,
                                         &in_l, &in_r);
            int worst = in_t;
            if (in_b > worst) worst = in_b;
            if (in_l > worst) worst = in_l;
            if (in_r > worst) worst = in_r;

            snprintf(b, sizeof(b),
                     "TOUCH-SAFE INSET   TOP %d  BOTTOM %d  LEFT %d  RIGHT %d",
                     in_t, in_b, in_l, in_r);
            fb_draw_text(fb, 20, 120, b,
                         worst > DISP_INSET_SUSPECT ? COLOR_ORANGE : COLOR_GREEN, 2);

            if (worst > DISP_INSET_SUSPECT)
                fb_draw_text(fb, 20, 148,
                             "THAT IS MORE THAN THIS PANEL SHOULD LOSE. RE-SWEEP THE "
                             "EDGES, OR REDO THE TAPS.", COLOR_ORANGE, 1);
            else if (worst > 0)
                fb_draw_text(fb, 20, 148,
                             "NORMAL - THE SENSOR SATURATES BEFORE THE EDGE. THE BAND "
                             "IS STILL DRAWABLE: USE IT FOR A STATUS OR SCORE ROW.",
                             COLOR_GRAY, 1);
            else
                fb_draw_text(fb, 20, 148,
                             "EVERY VISIBLE PIXEL CAN BE TOUCHED.", COLOR_GRAY, 1);

            snprintf(b, sizeof(b), "WILL WRITE   raw X %d %d %d %d",
                     new_rx0, new_kxl, new_kxh, new_rx1);
            fb_draw_text(fb, 20, 172, b, COLOR_WHITE, 1);
            snprintf(b, sizeof(b), "             raw Y %d %d %d %d   bezel %d %d %d %d",
                     new_ry0, new_kyl, new_kyh, new_ry1, bez_t, bez_b, bez_l, bez_r);
            fb_draw_text(fb, 20, 186, b, COLOR_WHITE, 1);
            snprintf(b, sizeof(b), "             reach X %d %d  Y %d %d",
                     new_gx0, new_gx1, new_gy0, new_gy1);
            fb_draw_text(fb, 20, 200, b, COLOR_WHITE, 1);
            fb_draw_text(fb, 20, 216,
                         "NEXT SWITCHES TO THE NEW MAPPING SO YOU CAN TRY IT BEFORE "
                         "ANYTHING IS SAVED.", COLOR_GRAY, 1);

            b_redo.label = "EDGES";
            b_next.label = "NEXT";
            wiz_draw_btn(fb, &b_cancel);
            wiz_draw_btn(fb, &b_redo);
            wiz_draw_btn(fb, &b_next);

        } else if (step == WIZ_CONFIRM) {
            const int cw = (int)fb->width, ch = (int)fb->height;
            uint32_t elapsed = now - confirm_start;
            int left = (elapsed >= WIZ_CONFIRM_MS)
                     ? 0 : (int)((WIZ_CONFIRM_MS - elapsed) / 1000);

            /* Frame on the logical edge: if any of it is under the plastic the
             * margins are still too small, which is the last thing worth
             * catching before this gets written. */
            fb_draw_rect(fb, 0, 0, cw, ch, COLOR_CYAN);
            fb_draw_rect(fb, 1, 1, cw - 2, ch - 2, COLOR_CYAN);

            text_draw_centered(fb, cw / 2, 50, "DOES THE NEW MAPPING WORK?",
                               COLOR_WHITE, 3);
            text_draw_centered(fb, cw / 2, 88,
                               "THE CYAN FRAME SHOULD BE FULLY VISIBLE", COLOR_GRAY, 2);
            text_draw_centered(fb, cw / 2, 114,
                               "IF YOU CANNOT PRESS KEEP, JUST WAIT - IT REVERTS BY ITSELF",
                               COLOR_GRAY, 1);
            snprintf(b, sizeof(b), "REVERTING IN %d", left);
            text_draw_centered(fb, cw / 2, 152, b,
                               left <= 5 ? COLOR_RED : COLOR_YELLOW, 3);

            WizBtn keep = { cw / 2 - 150, ch / 2 + 10, 300, 70,
                            "KEEP THESE", RGB(30, 110, 60) };
            wiz_draw_btn(fb, &keep);
        }

        fb_swap(fb);
        usleep(FRAME_DELAY_ACTIVE_US);
    }

    /* ------------------------- teardown ------------------------- */
    if (!saved) {
        /* Nothing was written, so nothing may be left applied — including the
         * measured reach, which changes every app's touch-safe inset. */
        touch_set_raw_curve(touch, entry_rx0, entry_kxl, entry_kxh, entry_rx1,
                                   entry_ry0, entry_kyl, entry_kyh, entry_ry1);
        touch_set_edge_reach(touch, entry_gx0, entry_gx1, entry_gy0, entry_gy1);
        fb_set_bezel(fb, entry_bt, entry_bb, entry_bl, entry_br);
    }
    touch_set_screen_size(touch, (int)fb->width, (int)fb->height);
    touch_drain_events(touch);

    if (msg[0]) {
        snprintf(out->msg, sizeof(out->msg), "%s", msg);
        out->saved = saved;
    }
}
