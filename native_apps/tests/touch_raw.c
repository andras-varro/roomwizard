/*
 * touch_raw — digitizer reach, measured with NO calibration and NO bezel.
 *
 * Why this exists
 * ---------------
 * Every number gathered before this tool was inferred *through* a calibration,
 * so it could not separate a physical sensor limit from an artifact of the fit.
 * This tool removes every layer of interpretation between finger and pixel:
 *
 *   - no calibration : the raw range is reset to what EVIOCGABS reports, so
 *                      /etc/touch_calibration.conf line 1 has no effect here;
 *   - no bezel       : fb_set_bezel(fb,0,0,0,0) makes the drawing surface the
 *                      full 800x480 panel, so a drawn pixel IS a panel pixel;
 *   - no private math: the library is *configured* into an identity map rather
 *                      than bypassed, so what you see is what scale_coordinates()
 *                      produces (native_apps/CLAUDE.md: one implementation only).
 *
 * Two separate questions have to be kept apart, and mixing them is what produced
 * every wrong conclusion so far:
 *
 *   1. What raw value does the physical edge emit?   -> SWEEP mode
 *   2. Where does raw first reach that value?        -> INSET mode
 *
 * A press against the bezel answers neither on its own: it reads raw 4095 at the
 * bottom whether saturation begins at the panel edge or 20 px inside it, and it
 * is the second case that makes a calibration curve's outer segment too steep.
 *
 * Four modes
 * ----------
 * LIVE     Free drag.  Cyan dot + full-screen crosshair (your finger covers the
 *          dot, not the crosshair), yellow trail, the extremes reached so far,
 *          and a PINNED flag the instant raw sticks at its hardware limit.
 *
 * SWEEP    Slide one finger along each edge in turn.  Keeps the extreme raw on
 *          the perpendicular axis per bucket along the edge, so coverage and
 *          corner-vs-middle differences are visible while you sweep.  Answers
 *          "what raw does the PHYSICAL edge emit?" — i.e. where the curve's
 *          endpoints belong.
 *
 * INSET    Tap a bar walked inward from each edge in known steps (0/10/20/35/55
 *          px from the innermost visible row).  Answers "WHERE does raw reach
 *          that extreme?" — i.e. the outer-band slope, and how far in the
 *          saturated flat band starts.  A sweep cannot see this, because it
 *          never leaves the edge; a bezel press cannot either, because it reads
 *          the same whether saturation begins at the edge or 20 px inside it.
 *
 * TARGETS  Tap 11 marked crosshairs (median of 3 taps each), then press hard
 *          against each of the four bezel edges.  The tool then fits the axes
 *          from INTERIOR targets only and reports what that fit predicts at the
 *          panel edges.
 *
 * The targets, the fit and the verdicts live in common/touch_calib.c, shared
 * with the Device Tools calibration wizard — this tool validates the very code
 * that wizard calibrates with, which it could not do with a private copy.
 *
 * Log: /tmp/touch_raw.tsv  (.tsv, not .log — .gitignore drops *.log)
 *   L <ms> <raw_x> <raw_y> <panel_x> <panel_y> <pinmask>   live sample
 *   T <ms> <idx> <target_x> <target_y> <raw_x> <raw_y>     target tap
 *   B <ms> <edge> <raw_x> <raw_y>                          bezel press extreme
 *   S <ms> <edge> <bucket> <raw_x> <raw_y>                 edge-sweep sample
 *   I <ms> <edge> <bar_panel> <tap> <raw_x> <raw_y>        inward-step tap
 *   # ...                                                  header / summary
 *
 * Landscape only.  Nothing is written outside /tmp unless you press APPLY on
 * the summary screen, which backs up the calibration first.
 *
 * Build: linked against native_apps/common (see build-and-deploy.sh).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#include "../common/framebuffer.h"
#include "../common/touch_input.h"
#include "../common/touch_calib.h"
#include "../common/common.h"

#define LOG_PATH    "/tmp/touch_raw.tsv"
#define CALIB_PATH  "/etc/touch_calibration.conf"
#define TRAIL_MAX   6000
#define TAPS_PER_TARGET TOUCH_CALIB_TAPS

/* Pin mask bits: raw sitting exactly on a hardware limit */
#define PIN_XMIN 1
#define PIN_XMAX 2
#define PIN_YMIN 4
#define PIN_YMAX 8

typedef enum {
    MODE_LIVE = 0,
    MODE_TARGETS,
    MODE_BEZEL,
    MODE_SWEEP,
    MODE_INSET,
    MODE_SUMMARY
} Mode;

/* ---- the two edge measurements ------------------------------------------ *
 * These answer two different questions, and the difference is the whole
 * reason both exist.
 *
 * SWEEP  What raw value does the PHYSICAL edge produce?  Slide one finger
 *        along an edge; the tool keeps the extreme raw on the perpendicular
 *        axis, per bucket along the edge, so a corner that reads differently
 *        from the middle shows up instead of averaging away.  This pins the
 *        curve's ENDPOINTS: if the top edge never emits raw_y 0, then storing
 *        "raw 0 == panel row 0" is simply false.
 *
 * INSET  WHERE does raw reach that extreme?  Tap a bar walked inward from the
 *        edge in known steps.  A sweep never leaves the edge, so it cannot see
 *        whether the extreme is first reached at the edge or 20 px inside it —
 *        and that distance is exactly what makes the outer segment of the
 *        calibration curve too steep or not.  This pins the SLOPE.
 *
 * A press against the bezel reads the same under either hypothesis, which is
 * why the pre-existing BEZEL phase could not settle it. */

/* The sweep accumulator itself — TouchCalibSweep, its bucket count, the edge
 * numbering convention and the reset/add/reached logic — lives in
 * common/touch_calib.c, shared with the Device Tools wizard's REACH step. This
 * tool used to carry a private copy; that is exactly the drift that put three
 * copies of the fit in the tree. */

/* Offsets inward from the innermost VISIBLE row/column of each edge. Rows under
 * the bezel are excluded on purpose: an invisible bar cannot be tapped, and
 * "press against the plastic" is already covered by the BEZEL phase. */
#define INSET_ROWS 5
#define INSET_TAPS 3
#define INSET_ACCEPT 60         /* px, perpendicular; buttons sit far outside this */
static const int INSET_OFFSET[INSET_ROWS] = { 0, 10, 20, 35, 55 };

/* The targets, the interior masks, the fit and the verdicts all live in
 * common/touch_calib.c — shared with the Device Tools calibration wizard, so
 * there is exactly one implementation of the thing this tool exists to
 * validate. */
#define TARGETS   TOUCH_CALIB_TARGETS
#define N_TARGETS TOUCH_CALIB_N_TARGETS
#define MAX_TARGETS 16   /* fixed-size arrays; N_TARGETS is 11 */

static const char *EDGE_NAME[4] = { "TOP", "BOTTOM", "LEFT", "RIGHT" };

typedef struct { int x, y; } Pt;

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

/* ---- monotonic clock, baselined at init -------------------------------- *
 * common.c's get_time_ms() multiplies a raw tv_sec by 1000, which overflows a
 * 32-bit long (CLAUDE.md).  Deltas survive the wrap, but the log wants an
 * honest elapsed time, so keep our own baseline and use it everywhere. */
static time_t g_t0;
static bool   g_t0_set = false;

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!g_t0_set) { g_t0 = ts.tv_sec; g_t0_set = true; }
    return (uint32_t)((ts.tv_sec - g_t0) * 1000 + ts.tv_nsec / 1000000);
}

/* ---- state -------------------------------------------------------------- */
static FILE *g_log = NULL;

static Pt  trail[TRAIL_MAX];
static int trail_n = 0, trail_head = 0;

static int  tap_raw_x[MAX_TARGETS][TAPS_PER_TARGET];
static int  tap_raw_y[MAX_TARGETS][TAPS_PER_TARGET];
static int  tgt_raw_x[MAX_TARGETS], tgt_raw_y[MAX_TARGETS];  /* median per target */
static int  edge_raw[4];                                     /* bezel-press extremes */
static bool edge_done[4];

static TouchCalibSweep g_sweep[4];
static int  inset_raw[4][INSET_ROWS][INSET_TAPS];
static int  inset_med[4][INSET_ROWS];   /* median raw per bar */
static int  inset_pos[4][INSET_ROWS];   /* panel coordinate the bar was drawn at */
static bool inset_have[4][INSET_ROWS];
static bool inset_done[4];

static void trail_push(int x, int y) {
    trail[trail_head].x = x;
    trail[trail_head].y = y;
    trail_head = (trail_head + 1) % TRAIL_MAX;
    if (trail_n < TRAIL_MAX) trail_n++;
}

/* ---- drawing helpers ---------------------------------------------------- */
static void dashed_hline(Framebuffer *fb, int y, uint32_t c) {
    for (int x = 0; x < (int)fb->width; x += 10)
        fb_draw_line(fb, x, y, x + 4, y, c);
}

static void text_boxed(Framebuffer *fb, int x, int y, int w, int h) {
    fb_fill_rect(fb, x, y, w, h, RGB(10, 10, 14));
    fb_draw_rect(fb, x, y, w, h, RGB(60, 60, 80));
}

/* Guide lines: coarse 100 px grid everywhere, plus a 10 px ladder inside the
 * outer 60 px of each edge.  Counting ladder rungs against the crosshair is how
 * you read off where the finger actually landed. */
static void draw_reference(Framebuffer *fb, int bezel_top, int bezel_bottom) {
    const int W = (int)fb->width, H = (int)fb->height;
    const uint32_t coarse = RGB(40, 40, 52);
    const uint32_t fine   = RGB(70, 70, 95);
    const uint32_t lbl    = RGB(110, 110, 145);
    char b[8];

    for (int x = 100; x < W; x += 100) fb_draw_line(fb, x, 0, x, H - 1, coarse);
    for (int y = 100; y < H; y += 100) fb_draw_line(fb, 0, y, W - 1, y, coarse);

    for (int d = 0; d <= 60; d += 10) {
        fb_draw_line(fb, d,         0, d,         H - 1, fine);   /* left   */
        fb_draw_line(fb, W - 1 - d, 0, W - 1 - d, H - 1, fine);   /* right  */
        fb_draw_line(fb, 0, d,         W - 1, d,         fine);   /* top    */
        fb_draw_line(fb, 0, H - 1 - d, W - 1, H - 1 - d, fine);   /* bottom */

        /* Stagger the left/right labels down a staircase — stacked at one y they
         * overlap into an unreadable smear at 8 px per character. */
        int step_i = d / 10;
        snprintf(b, sizeof(b), "%d", d);
        fb_draw_text(fb, d + 2,          H / 2 + 40 + step_i * 12, b, lbl, 1);
        fb_draw_text(fb, W - 30 - d,     H / 2 - 40 - step_i * 12, b, lbl, 1);
        fb_draw_text(fb, W / 2 + 60,     d + 2,                    b, lbl, 1);
        fb_draw_text(fb, W / 2 - 100,    H - 1 - d - 8,            b, lbl, 1);
    }

    /* The true panel border. Mostly under the plastic — that is the point. */
    fb_draw_rect(fb, 0, 0, W, H, RGB(150, 150, 150));

    /* Where the bezel is believed to start. Everything outside is invisible. */
    const uint32_t bez = RGB(150, 60, 0);
    dashed_hline(fb, bezel_top,          bez);
    dashed_hline(fb, H - 1 - bezel_bottom, bez);
    fb_draw_text(fb, 620, bezel_top + 3,        "BEZEL", bez, 1);
    fb_draw_text(fb, 620, H - 1 - bezel_bottom - 11, "BEZEL", bez, 1);
}

static void draw_crosshair(Framebuffer *fb, int x, int y, uint32_t c) {
    fb_draw_line(fb, 0, y, (int)fb->width - 1, y, c);
    fb_draw_line(fb, x, 0, x, (int)fb->height - 1, c);
}

/* Big target reticle for the tap phase */
static void draw_target(Framebuffer *fb, int x, int y, uint32_t c) {
    fb_draw_circle(fb, x, y, 22, c);
    fb_draw_circle(fb, x, y, 21, c);
    fb_draw_circle(fb, x, y, 8,  c);
    fb_draw_line(fb, x - 34, y, x + 34, y, c);
    fb_draw_line(fb, x, y - 34, x, y + 34, c);
    fb_fill_circle(fb, x, y, 2, c);
}

typedef struct { int x, y, w, h; const char *label; uint32_t col; } Hit;

static void draw_hit(Framebuffer *fb, const Hit *b) {
    fb_fill_rounded_rect(fb, b->x, b->y, b->w, b->h, 8, b->col);
    fb_draw_rounded_rect(fb, b->x, b->y, b->w, b->h, 8, COLOR_WHITE);
    int tw = text_measure_width(b->label, 2);
    fb_draw_text(fb, b->x + (b->w - tw) / 2, b->y + b->h / 2 - 8,
                 b->label, COLOR_WHITE, 2);
}

static bool hit_test(const Hit *b, int x, int y) {
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

/* A tap only counts for the current target if it is plausibly aimed at it.
 * Without this a fumbled tap anywhere on the panel enters the fit, and the fit
 * is the entire point of the exercise. The ABORT button is deliberately placed
 * further than this from every target. */
#define TAP_ACCEPT_RADIUS 120
static bool near_target(int x, int y, int i) {
    int dx = x - TARGETS[i].px, dy = y - TARGETS[i].py;
    return dx * dx + dy * dy <= TAP_ACCEPT_RADIUS * TAP_ACCEPT_RADIUS;
}

/* ---- edge geometry ------------------------------------------------------- *
 * Edge order is fixed by EDGE_NAME and matches touch_calib.c's: 0 TOP,
 * 1 BOTTOM, 2 LEFT, 3 RIGHT. TOP/BOTTOM measure raw_y and run ALONG x;
 * LEFT/RIGHT measure raw_x and run along y. TOP and LEFT look for the raw
 * MINIMUM, BOTTOM and RIGHT the maximum. touch_calib_sweep_edge_is_y() and
 * touch_calib_sweep_wants_min() are the only implementation of that. */

/* Panel coordinate of INSET bar k on edge e. Measured from the innermost row
 * the bezel still leaves visible, so every bar can actually be seen and hit. */
static int inset_bar_pos(int e, int k, int W, int H,
                         int bez_t, int bez_b, int bez_l, int bez_r) {
    switch (e) {
        case 0:  return bez_t + INSET_OFFSET[k];
        case 1:  return H - 1 - bez_b - INSET_OFFSET[k];
        case 2:  return bez_l + INSET_OFFSET[k];
        default: return W - 1 - bez_r - INSET_OFFSET[k];
    }
}

/* ---- edge reports ------------------------------------------------------- */

static void print_sweep(int hw_min_x, int hw_max_x, int hw_min_y, int hw_max_y) {
    FILE *out[2] = { stdout, g_log };
    for (int k = 0; k < 2; k++) {
        FILE *f = out[k];
        if (!f) continue;
        const char *p = (f == stdout) ? "" : "# ";
        fprintf(f, "%s\n%s=== EDGE SWEEP: what raw does the physical edge emit? ===\n", p, p);
        for (int e = 0; e < 4; e++) {
            const TouchCalibSweep *s = &g_sweep[e];
            if (!s->done && s->covered == 0) {
                fprintf(f, "%s%-6s  not swept\n", p, EDGE_NAME[e]);
                continue;
            }
            bool ymin = touch_calib_sweep_wants_min(e);
            int limit = touch_calib_sweep_edge_is_y(e) ? (ymin ? hw_min_y : hw_max_y)
                                     : (ymin ? hw_min_x : hw_max_x);
            int shortfall = ymin ? (s->extreme - limit) : (limit - s->extreme);
            fprintf(f, "%s%-6s  raw_%c %s %5d / %d   %+d counts from the limit   %s\n",
                    p, EDGE_NAME[e], touch_calib_sweep_edge_is_y(e) ? 'y' : 'x',
                    ymin ? "min" : "max", s->extreme, limit, -shortfall,
                    shortfall <= 0 ? "PINNED" : "NOT PINNED");
            fprintf(f, "%s        covered %d/%d buckets, %ld samples, %ld pinned\n",
                    p, s->covered, TOUCH_CALIB_SWEEP_BUCKETS, s->samples, s->pinned);
            fprintf(f, "%s        per-bucket extreme:", p);
            for (int i = 0; i < TOUCH_CALIB_SWEEP_BUCKETS; i++) {
                if (s->bucket_hit[i]) fprintf(f, " %d", s->bucket[i]);
                else                  fprintf(f, " -");
            }
            fprintf(f, "\n");
        }
        fflush(f);
    }
}

/* Where does raw reach the hardware limit, in panel pixels?
 *
 * The slope comes from the 11-target INTERIOR FIT, never from two adjacent
 * INSET bars. Adjacent bars are 10–20 px apart and each median carries ±80 raw
 * of tap noise, so a two-point slope is dominated by the noise: on the
 * reference capture that method printed "raw reached at panel 594, 614, 817
 * and -4" for a 480-row panel. The INSET table's own job is narrower and it
 * does it reliably — it says WHERE the reading goes flat, which is a
 * comparison, not a division.
 *
 * `fx`/`fy` are only read when have_fit; the TARGETS phase must have run. */
static void print_inset(int hw_min_x, int hw_max_x, int hw_min_y, int hw_max_y,
                        int panel_w, int panel_h,
                        const TouchAxisFit *fx, const TouchAxisFit *fy,
                        bool have_fit) {
    FILE *out[2] = { stdout, g_log };
    for (int k = 0; k < 2; k++) {
        FILE *f = out[k];
        if (!f) continue;
        const char *p = (f == stdout) ? "" : "# ";
        fprintf(f, "%s\n%s=== INWARD STEP: where does raw reach the limit? ===\n", p, p);
        for (int e = 0; e < 4; e++) {
            bool any = false;
            for (int i = 0; i < INSET_ROWS; i++) any |= inset_have[e][i];
            if (!any) { fprintf(f, "%s%-6s  not measured\n", p, EDGE_NAME[e]); continue; }

            bool ymin  = touch_calib_sweep_wants_min(e);
            int  limit = touch_calib_sweep_edge_is_y(e) ? (ymin ? hw_min_y : hw_max_y)
                                      : (ymin ? hw_min_x : hw_max_x);
            fprintf(f, "%s%-6s  raw_%c, limit %d %s\n", p, EDGE_NAME[e],
                    touch_calib_sweep_edge_is_y(e) ? 'y' : 'x', limit, ymin ? "(min)" : "(max)");
            for (int i = 0; i < INSET_ROWS; i++) {
                if (!inset_have[e][i]) {
                    fprintf(f, "%s        panel %4d   (skipped)\n", p, inset_pos[e][i]);
                    continue;
                }
                if (i == 0) {
                    fprintf(f, "%s        panel %4d   raw %5d\n",
                            p, inset_pos[e][i], inset_med[e][i]);
                } else if (inset_have[e][i - 1]) {
                    /* Bar-to-bar difference. Read this only as a flatness
                     * indicator — over a 10–20 px baseline with ±80 raw of tap
                     * noise the number itself is not a slope worth quoting. */
                    int dp = inset_pos[e][i - 1] - inset_pos[e][i];
                    int dr = inset_med[e][i - 1] - inset_med[e][i];
                    double sl = (dp != 0) ? (double)dr / (double)dp : 0.0;
                    fprintf(f, "%s        panel %4d   raw %5d   d %6.2f raw/px%s\n",
                            p, inset_pos[e][i], inset_med[e][i], sl,
                            (sl > -0.5 && sl < 0.5) ? "   <- FLAT (saturated)" : "");
                } else {
                    fprintf(f, "%s        panel %4d   raw %5d\n",
                            p, inset_pos[e][i], inset_med[e][i]);
                }
            }
            /* (a) Where clipping is observed — a comparison against the limit,
             * immune to tap noise. Bars run outermost (offset 0) to innermost. */
            int flat_to = -1, outermost = -1;
            for (int i = 0; i < INSET_ROWS; i++) {
                if (!inset_have[e][i]) continue;
                if (outermost < 0) outermost = i;
                bool sat = ymin ? (inset_med[e][i] <= limit)
                                : (inset_med[e][i] >= limit);
                if (!sat) break;
                flat_to = i;
            }
            if (flat_to >= 0)
                fprintf(f, "%s        clipped at raw %d from the edge out to panel %d\n",
                        p, limit, inset_pos[e][flat_to]);
            else if (outermost >= 0)
                fprintf(f, "%s        no bar read the limit; clipping (if any) starts "
                           "outside panel %d\n", p, inset_pos[e][outermost]);

            /* (b) Where the interior line says raw WOULD reach the limit,
             * anchored on the innermost measured bar (furthest from the edge, so
             * least likely to sit inside the flat band) and using the fit's
             * slope. Agreement between (a) and (b) is the cross-check. */
            int ia = -1;
            for (int i = INSET_ROWS - 1; i >= 0; i--)
                if (inset_have[e][i]) { ia = i; break; }

            if (!have_fit) {
                fprintf(f, "%s        (no slope: run TARGETS to fit the interior line)\n", p);
            } else if (ia >= 0) {
                const TouchAxisFit *fit = touch_calib_sweep_edge_is_y(e) ? fy : fx;
                int dim = touch_calib_sweep_edge_is_y(e) ? panel_h : panel_w;
                double sl = (dim > 1)
                          ? (double)(fit->in1 - fit->in0) / (double)(dim - 1) : 0.0;
                if (sl > 0.01) {
                    double at = (double)inset_pos[e][ia] +
                                ((double)limit - inset_med[e][ia]) / sl;
                    fprintf(f, "%s     => fit slope %.2f raw/px (11-target interior); "
                               "raw %d reached at panel %.0f\n", p, sl, limit, at);
                } else {
                    fprintf(f, "%s        (fit slope %.2f raw/px is not usable)\n", p, sl);
                }
            }
        }
        fflush(f);
    }
}

/* ---- summary ------------------------------------------------------------ */
typedef struct {
    TouchAxisFit x, y;
    int  edge_panel[4];   /* bezel-press raw mapped through the interior fit */
    int  probe_resid[4];  /* edge-probe residual: predicted - drawn, in px   */
    TouchAxisCurve cx, cy;  /* the three-segment curve the wizard would save */
    /* One report PER AXIS: how much edge compression the outer segments have to
     * absorb. Not a hardware verdict — the sensor reaches every edge. */
    bool reach_x, reach_y;
    char verdict_x[128], verdict_y[128];
} Summary;

static void build_summary(Summary *s, int panel_w, int panel_h,
                          int hw_min_x, int hw_max_x, int hw_min_y, int hw_max_y) {
    int rx[MAX_TARGETS], ry[MAX_TARGETS], px[MAX_TARGETS], py[MAX_TARGETS];
    bool ix[MAX_TARGETS], iy[MAX_TARGETS];

    for (int i = 0; i < N_TARGETS; i++) {
        rx[i] = tgt_raw_x[i];  ry[i] = tgt_raw_y[i];
        px[i] = TARGETS[i].px; py[i] = TARGETS[i].py;
        ix[i] = touch_calib_interior_x(px[i], panel_w);
        iy[i] = touch_calib_interior_y(py[i], panel_h);
    }

    touch_calib_fit(&s->x, rx, px, ix, N_TARGETS, panel_w);
    touch_calib_fit(&s->y, ry, py, iy, N_TARGETS, panel_h);
    touch_calib_curve_from_fit(&s->x, hw_min_x, hw_max_x, &s->cx);
    touch_calib_curve_from_fit(&s->y, hw_min_y, hw_max_y, &s->cy);

    /* Where does a hard press against each bezel land, according to the fitted
     * LINE that never saw an edge sample?  A press that reads well inside the
     * panel is the extrapolation overshooting, not the sensor falling short —
     * which is the distinction this tool exists to make. */
    s->edge_panel[0] = touch_calib_predict_panel(edge_done[0] ? edge_raw[0] : hw_min_y,
                                                 s->y.in0, s->y.in1, panel_h);
    s->edge_panel[1] = touch_calib_predict_panel(edge_done[1] ? edge_raw[1] : hw_max_y,
                                                 s->y.in0, s->y.in1, panel_h);
    s->edge_panel[2] = touch_calib_predict_panel(edge_done[2] ? edge_raw[2] : hw_min_x,
                                                 s->x.in0, s->x.in1, panel_w);
    s->edge_panel[3] = touch_calib_predict_panel(edge_done[3] ? edge_raw[3] : hw_max_x,
                                                 s->x.in0, s->x.in1, panel_w);

    /* Edge-probe residuals: does the interior fit still describe a target at
     * x=20 / y=22?  A large residual is compression reaching that far in. */
    s->probe_resid[0] = touch_calib_predict_panel(ry[TOUCH_CALIB_PROBE_YLO],
                            s->y.in0, s->y.in1, panel_h) - py[TOUCH_CALIB_PROBE_YLO];
    s->probe_resid[1] = touch_calib_predict_panel(ry[TOUCH_CALIB_PROBE_YHI],
                            s->y.in0, s->y.in1, panel_h) - py[TOUCH_CALIB_PROBE_YHI];
    s->probe_resid[2] = touch_calib_predict_panel(rx[TOUCH_CALIB_PROBE_XLO],
                            s->x.in0, s->x.in1, panel_w) - px[TOUCH_CALIB_PROBE_XLO];
    s->probe_resid[3] = touch_calib_predict_panel(rx[TOUCH_CALIB_PROBE_XHI],
                            s->x.in0, s->x.in1, panel_w) - px[TOUCH_CALIB_PROBE_XHI];

    s->reach_x = touch_calib_axis_verdict(&s->cx, hw_min_x, hw_max_x, "X",
                                          s->verdict_x, sizeof(s->verdict_x));
    s->reach_y = touch_calib_axis_verdict(&s->cy, hw_min_y, hw_max_y, "Y",
                                          s->verdict_y, sizeof(s->verdict_y));
}

/* Emit the summary to stdout and the log. Both, because the on-screen table is
 * the only thing you can read while standing at the device and the log is the
 * only thing you can re-analyse afterwards. */
static void print_summary(const Summary *s, int panel_w, int panel_h,
                          int hw_min_x, int hw_max_x, int hw_min_y, int hw_max_y) {
    FILE *out[2] = { stdout, g_log };
    for (int k = 0; k < 2; k++) {
        FILE *f = out[k];
        if (!f) continue;
        const char *p = (f == stdout) ? "" : "# ";
        fprintf(f, "%s\n%s=== touch_raw summary ===\n", p, p);
        fprintf(f, "%shardware raw range: X[%d..%d] Y[%d..%d]  panel %dx%d\n",
                p, hw_min_x, hw_max_x, hw_min_y, hw_max_y, panel_w, panel_h);
        fprintf(f, "%s\n%sAXIS X          interior-only   all-points\n", p, p);
        fprintf(f, "%s  raw @ panel 0      %7d       %7d\n", p, s->x.in0, s->x.all0);
        fprintf(f, "%s  raw @ panel %-4d   %7d       %7d\n",
                p, panel_w - 1, s->x.in1, s->x.all1);
        fprintf(f, "%s  bezel press LEFT  raw %5d -> panel %d\n",
                p, edge_raw[2], s->edge_panel[2]);
        fprintf(f, "%s  bezel press RIGHT raw %5d -> panel %d\n",
                p, edge_raw[3], s->edge_panel[3]);
        fprintf(f, "%s  edge-probe residual  x=20: %+d px   x=780: %+d px\n",
                p, s->probe_resid[2], s->probe_resid[3]);
        {   int lo, hi;
            touch_calib_reach(&s->cx, hw_min_x, hw_max_x, &lo, &hi);
            fprintf(f, "%s  curve raw %d %d %d %d\n", p,
                    s->cx.v0, s->cx.k_lo, s->cx.k_hi, s->cx.v1);
            fprintf(f, "%s  raw %d..%d covers panel %d..%d\n",
                    p, hw_min_x, hw_max_x, lo, hi);
            fprintf(f, "%s  %s\n", p, s->verdict_x);
        }
        fprintf(f, "%s\n%sAXIS Y          interior-only   all-points\n", p, p);
        fprintf(f, "%s  raw @ panel 0      %7d       %7d\n", p, s->y.in0, s->y.all0);
        fprintf(f, "%s  raw @ panel %-4d   %7d       %7d\n",
                p, panel_h - 1, s->y.in1, s->y.all1);
        fprintf(f, "%s  bezel press TOP    raw %5d -> panel %d\n",
                p, edge_raw[0], s->edge_panel[0]);
        fprintf(f, "%s  bezel press BOTTOM raw %5d -> panel %d\n",
                p, edge_raw[1], s->edge_panel[1]);
        fprintf(f, "%s  edge-probe residual  y=22: %+d px   y=458: %+d px\n",
                p, s->probe_resid[0], s->probe_resid[1]);
        {   int lo, hi;
            touch_calib_reach(&s->cy, hw_min_y, hw_max_y, &lo, &hi);
            fprintf(f, "%s  curve raw %d %d %d %d\n", p,
                    s->cy.v0, s->cy.k_lo, s->cy.k_hi, s->cy.v1);
            fprintf(f, "%s  raw %d..%d covers panel %d..%d\n",
                    p, hw_min_y, hw_max_y, lo, hi);
            fprintf(f, "%s  %s\n", p, s->verdict_y);
        }

        fprintf(f, "%s\n%sper-target medians (target_x target_y raw_x raw_y)\n", p, p);
        for (int i = 0; i < N_TARGETS; i++)
            fprintf(f, "%s  %4d %4d   %5d %5d\n", p,
                    TARGETS[i].px, TARGETS[i].py, tgt_raw_x[i], tgt_raw_y[i]);
        fflush(f);
    }
}

/* ---- main --------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    const char *fb_dev    = (argc > 1) ? argv[1] : "/dev/fb0";
    const char *touch_dev = (argc > 2) ? argv[2] : "/dev/input/event0";

    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);
    setvbuf(stdout, NULL, _IOLBF, 0);   /* progress must show over SSH, not on exit */

    int lock_fd = acquire_instance_lock("touch_raw");
    if (lock_fd < 0) return 1;

    /* This tool draws 32-bit pixels. Assert the depth rather than trusting
     * whoever ran last (ScummVM and vnc_client leave the panel at 16bpp). */
    fb_set_bpp(fb_dev, 32);

    Framebuffer fb;
    if (fb_init(&fb, fb_dev) < 0) {
        fprintf(stderr, "touch_raw: failed to init framebuffer\n");
        return 1;
    }
    if (fb.portrait_mode) {
        fprintf(stderr, "touch_raw: portrait mode is not supported "
                        "(remove /opt/games/portrait.mode)\n");
        fb_close(&fb);
        return 1;
    }

    /* Capture the bezel margins fb_init() loaded, then drop them: from here on
     * the drawing surface is the whole panel and drawn == panel coordinates. */
    const int file_bez_t = screen_bezel_top,  file_bez_b = screen_bezel_bottom;
    const int file_bez_l = screen_bezel_left, file_bez_r = screen_bezel_right;
    if (fb_set_bezel(&fb, 0, 0, 0, 0) < 0) {
        fprintf(stderr, "touch_raw: fb_set_bezel(0,0,0,0) failed\n");
        fb_close(&fb);
        return 1;
    }

    TouchInput touch;
    if (touch_init(&touch, touch_dev) < 0) {
        fprintf(stderr, "touch_raw: failed to init touch\n");
        fb_close(&fb);
        return 1;
    }

    /* touch_init() auto-loaded the stored calibration into raw_min/raw_max.
     * Re-read what the hardware itself declares and install that instead — the
     * whole point is to see the panel with the calibration taken out. */
    int hw_min_x, hw_max_x, hw_min_y, hw_max_y;
    touch_calib_hw_range(&touch, &hw_min_x, &hw_max_x, &hw_min_y, &hw_max_y);
    const int stored_x0 = touch.raw_min_x, stored_x1 = touch.raw_max_x;
    const int stored_y0 = touch.raw_min_y, stored_y1 = touch.raw_max_y;
    touch_set_raw_range(&touch, hw_min_x, hw_max_x, hw_min_y, hw_max_y);
    touch_set_screen_size(&touch, fb.width, fb.height);  /* after fb_set_bezel */

    const int W = (int)fb.width, H = (int)fb.height;

    g_log = fopen(LOG_PATH, "w");
    if (g_log) {
        fprintf(g_log, "# touch_raw — no calibration, no bezel\n");
        fprintf(g_log, "# panel %dx%d  drawn == panel coordinates\n", W, H);
        fprintf(g_log, "# hardware raw range (in use): X[%d..%d] Y[%d..%d]\n",
                hw_min_x, hw_max_x, hw_min_y, hw_max_y);
        fprintf(g_log, "# stored calibration (IGNORED): X[%d..%d] Y[%d..%d]\n",
                stored_x0, stored_x1, stored_y0, stored_y1);
        fprintf(g_log, "# stored bezel (IGNORED): T=%d B=%d L=%d R=%d\n",
                file_bez_t, file_bez_b, file_bez_l, file_bez_r);
        fprintf(g_log, "# L ms raw_x raw_y panel_x panel_y pinmask\n");
        fprintf(g_log, "# T ms idx target_x target_y raw_x raw_y\n");
        fprintf(g_log, "# B ms edge raw_x raw_y\n");
        fprintf(g_log, "# S ms edge bucket raw_x raw_y            edge-sweep sample\n");
        fprintf(g_log, "# I ms edge bar_panel tap raw_x raw_y     inward-step tap\n");
        fflush(g_log);
    }

    printf("touch_raw: panel %dx%d, raw range X[%d..%d] Y[%d..%d] (calibration ignored)\n",
           W, H, hw_min_x, hw_max_x, hw_min_y, hw_max_y);
    printf("touch_raw: logging to %s\n", LOG_PATH);

    /* Buttons stay well clear of the border so the tool is still operable if
     * the inset turns out to be real. */
    Hit btn_exit    = { 30, 380, 140, 70, "EXIT",      RGB(120, 30, 30) };
    Hit btn_sweep   = { 190, 380, 140, 70, "SWEEP",    RGB(30, 100, 100) };
    Hit btn_inset   = { 350, 380, 140, 70, "INSET",    RGB(100, 60, 120) };
    Hit btn_targets = { 600, 380, 170, 70, "TARGETS",  RGB(30, 80, 120) };
    /* NEXT/REDO sit mid-screen, not at the bottom: the bezel-press phase treats
     * the outer sixth of each axis as "a press against that edge", and a button
     * inside that band would have its own tap recorded as an edge extreme. */
    Hit btn_next    = { 470, 300, 150, 60, "NEXT",     RGB(30, 100, 60) };
    Hit btn_redo    = { 180, 300, 150, 60, "REDO",     RGB(110, 80, 20) };
    /* Two abort positions, because the two phases forbid different regions.
     * In TARGETS it must be >TAP_ACCEPT_RADIUS from every target; in BEZEL it
     * must be outside all four edge bands, and (700,410) is inside both the
     * RIGHT and BOTTOM ones. */
    Hit btn_abort   = { 700, 410, 90,  40, "ABORT",    RGB(90, 40, 40) };
    Hit btn_abort_b = { 350, 300, 100, 60, "ABORT",    RGB(90, 40, 40) };
    Hit btn_apply   = { 480, 400, 250, 60, "APPLY FIT", RGB(100, 60, 120) };
    Hit btn_done    = { 70, 400, 200, 60, "EXIT",      RGB(120, 30, 30) };
    /* SWEEP and INSET accept samples/taps within INSET_ACCEPT px of an edge or
     * bar, so their controls must sit clear of all four edges on BOTH axes —
     * one button row serves edges that run horizontally and vertically. The row
     * is also outside the outer-sixth bands the sweep samples from. */
    Hit btn_s_next  = { 200, 258, 130, 54, "NEXT",     RGB(30, 100, 60) };
    Hit btn_s_redo  = { 340, 258, 130, 54, "REDO",     RGB(110, 80, 20) };
    Hit btn_s_quit  = { 480, 258, 130, 54, "BACK",     RGB(90, 40, 40) };

    Mode mode = MODE_LIVE;
    int  tgt_i = 0, tap_i = 0, edge_i = 0;
    bool edge_capturing = false;
    int  sweep_e = 0;                       /* 4 == report screen */
    int  inset_e = 0, inset_k = 0, inset_t = 0;   /* 4 == report screen */
    Summary summary;
    memset(&summary, 0, sizeof(summary));
    bool summary_ready = false;
    char apply_msg[128] = "";

    for (int e = 0; e < 4; e++) {
        touch_calib_sweep_reset(&g_sweep[e], e);
        inset_done[e] = false;
        for (int k = 0; k < INSET_ROWS; k++) {
            inset_have[e][k] = false;
            inset_med[e][k]  = -1;
            inset_pos[e][k]  = inset_bar_pos(e, k, W, H, file_bez_t, file_bez_b,
                                             file_bez_l, file_bez_r);
        }
    }

    ModalDialog dlg;
    memset(&dlg, 0, sizeof(dlg));

    int last_raw_x = -1, last_raw_y = -1, last_px = -1, last_py = -1;
    int ext_px_min = W, ext_px_max = -1, ext_py_min = H, ext_py_max = -1;
    int ext_rx_min = 1 << 20, ext_rx_max = -(1 << 20);
    int ext_ry_min = 1 << 20, ext_ry_max = -(1 << 20);
    long samples = 0;
    uint32_t last_action_ms = 0;

    for (int i = 0; i < 4; i++) { edge_raw[i] = -1; edge_done[i] = false; }

    now_ms();   /* set the baseline before the first log line */

    while (!g_stop) {
        uint32_t t = now_ms();
        touch_poll(&touch);
        TouchState st = touch_get_state(&touch);

        int raw_x = touch.last_x, raw_y = touch.last_y;
        /* Only meaningful while a finger is down: last_x/last_y start at 0 and
         * persist after release, which would otherwise read as a permanent
         * X-MIN/Y-MIN pin before the panel has been touched at all. */
        int pin = 0;
        if (st.held) {
            if (raw_x <= hw_min_x) pin |= PIN_XMIN;
            if (raw_x >= hw_max_x) pin |= PIN_XMAX;
            if (raw_y <= hw_min_y) pin |= PIN_YMIN;
            if (raw_y >= hw_max_y) pin |= PIN_YMAX;
        }

        if (st.held) {
            if (raw_x != last_raw_x || raw_y != last_raw_y) {
                samples++;
                if (mode == MODE_LIVE) trail_push(st.x, st.y);
                if (st.x < ext_px_min) ext_px_min = st.x;
                if (st.x > ext_px_max) ext_px_max = st.x;
                if (st.y < ext_py_min) ext_py_min = st.y;
                if (st.y > ext_py_max) ext_py_max = st.y;
                if (raw_x < ext_rx_min) ext_rx_min = raw_x;
                if (raw_x > ext_rx_max) ext_rx_max = raw_x;
                if (raw_y < ext_ry_min) ext_ry_min = raw_y;
                if (raw_y > ext_ry_max) ext_ry_max = raw_y;
                if (g_log)
                    fprintf(g_log, "L %u %d %d %d %d %d\n",
                            t, raw_x, raw_y, st.x, st.y, pin);
            }
            last_raw_x = raw_x; last_raw_y = raw_y;
            last_px = st.x;     last_py = st.y;
        }

        bool debounced = (t - last_action_ms) > BTN_DEBOUNCE_MS;

        /* ---- modal takes all input while it is up ---- */
        if (modal_dialog_is_active(&dlg)) {
            ModalDialogAction a = modal_dialog_update(&dlg, st.x, st.y, st.pressed, t);
            if (a == MODAL_ACTION_BTN0) {
                char bak[256] = "";
                if (touch_calib_backup(CALIB_PATH, bak, sizeof(bak)) == 0) {
                    /* Save the fitted CURVE (interior line between the knots,
                     * outer segments landing on the emittable raw extremes), then
                     * put the tool straight back to uncalibrated so what it
                     * displays never silently changes. */
                    touch.calib.bezel_top    = file_bez_t;
                    touch.calib.bezel_bottom = file_bez_b;
                    touch.calib.bezel_left   = file_bez_l;
                    touch.calib.bezel_right  = file_bez_r;
                    touch_set_raw_curve(&touch,
                                        summary.cx.v0, summary.cx.k_lo,
                                        summary.cx.k_hi, summary.cx.v1,
                                        summary.cy.v0, summary.cy.k_lo,
                                        summary.cy.k_hi, summary.cy.v1);
                    int rc = touch_save_calibration(&touch, CALIB_PATH);
                    touch_set_raw_range(&touch, hw_min_x, hw_max_x, hw_min_y, hw_max_y);
                    snprintf(apply_msg, sizeof(apply_msg),
                             rc == 0 ? "APPLIED - backup %s" : "SAVE FAILED - backup %s",
                             bak);
                } else {
                    snprintf(apply_msg, sizeof(apply_msg), "BACKUP FAILED - nothing written");
                }
                if (g_log) { fprintf(g_log, "# %s\n", apply_msg); fflush(g_log); }
                printf("%s\n", apply_msg);
            }
            last_action_ms = t;
        }
        /* ---- LIVE ---- */
        else if (mode == MODE_LIVE) {
            if (st.pressed && debounced) {
                if (hit_test(&btn_exit, st.x, st.y)) break;
                if (hit_test(&btn_targets, st.x, st.y)) {
                    mode = MODE_TARGETS;
                    tgt_i = 0; tap_i = 0;
                    last_action_ms = t;
                }
                if (hit_test(&btn_sweep, st.x, st.y)) {
                    mode = MODE_SWEEP;
                    sweep_e = 0;
                    last_action_ms = t;
                }
                if (hit_test(&btn_inset, st.x, st.y)) {
                    mode = MODE_INSET;
                    inset_e = 0; inset_k = 0; inset_t = 0;
                    last_action_ms = t;
                }
            }
        }
        /* ---- EDGE SWEEP ---- *
         * Accumulate while the finger is down anywhere in the outer sixth of the
         * axis under test. Nothing is captured on lift: a sweep is the stroke
         * itself, and demanding a clean lift inside the band threw away the end
         * of every stroke. */
        else if (mode == MODE_SWEEP) {
            if (sweep_e < 4 && st.held) {
                bool relevant =
                    (sweep_e == 0 && st.y < H / 6) || (sweep_e == 1 && st.y > H * 5 / 6) ||
                    (sweep_e == 2 && st.x < W / 6) || (sweep_e == 3 && st.x > W * 5 / 6);
                if (relevant) {
                    bool is_y     = touch_calib_sweep_edge_is_y(sweep_e);
                    bool want_min = touch_calib_sweep_wants_min(sweep_e);
                    int  v        = is_y ? raw_y : raw_x;
                    int  along    = is_y ? st.x  : st.y;
                    int  span     = is_y ? W     : H;
                    int  hw_limit = is_y ? (want_min ? hw_min_y : hw_max_y)
                                         : (want_min ? hw_min_x : hw_max_x);

                    touch_calib_sweep_add(&g_sweep[sweep_e], sweep_e,
                                          v, along, span, hw_limit);

                    if (g_log) {
                        int bk = along * TOUCH_CALIB_SWEEP_BUCKETS / (span > 0 ? span : 1);
                        if (bk < 0) bk = 0;
                        if (bk >= TOUCH_CALIB_SWEEP_BUCKETS) bk = TOUCH_CALIB_SWEEP_BUCKETS - 1;
                        fprintf(g_log, "S %u %s %d %d %d\n",
                                t, EDGE_NAME[sweep_e], bk, raw_x, raw_y);
                    }
                }
            }
            if (st.pressed && debounced) {
                if (hit_test(&btn_s_quit, st.x, st.y)) {
                    mode = MODE_LIVE;
                    last_action_ms = t;
                } else if (hit_test(&btn_s_redo, st.x, st.y)) {
                    if (sweep_e < 4) touch_calib_sweep_reset(&g_sweep[sweep_e], sweep_e);
                    else { for (int e = 0; e < 4; e++) touch_calib_sweep_reset(&g_sweep[e], e);
                           sweep_e = 0; }
                    last_action_ms = t;
                } else if (hit_test(&btn_s_next, st.x, st.y) && sweep_e < 4) {
                    if (g_log) {
                        fprintf(g_log, "# SWEEP %s extreme %d covered %d/%d "
                                       "samples %ld pinned %ld\n",
                                EDGE_NAME[sweep_e], g_sweep[sweep_e].extreme,
                                g_sweep[sweep_e].covered, TOUCH_CALIB_SWEEP_BUCKETS,
                                g_sweep[sweep_e].samples, g_sweep[sweep_e].pinned);
                        fflush(g_log);
                    }
                    sweep_e++;
                    last_action_ms = t;
                    if (sweep_e >= 4)
                        print_sweep(hw_min_x, hw_max_x, hw_min_y, hw_max_y);
                }
            }
        }
        /* ---- INWARD STEP ---- */
        else if (mode == MODE_INSET) {
            if (st.pressed && debounced) {
                if (hit_test(&btn_s_quit, st.x, st.y)) {
                    mode = MODE_LIVE;
                    last_action_ms = t;
                } else if (hit_test(&btn_s_redo, st.x, st.y) && inset_e < 4) {
                    inset_t = 0;                     /* retake the current bar */
                    inset_have[inset_e][inset_k] = false;
                    last_action_ms = t;
                } else if (hit_test(&btn_s_next, st.x, st.y) && inset_e < 4) {
                    inset_t = 0;                     /* skip this bar */
                    inset_k++;
                    if (inset_k >= INSET_ROWS) {
                        inset_done[inset_e] = true;
                        inset_k = 0; inset_e++;
                        if (inset_e >= 4)
                            print_inset(hw_min_x, hw_max_x, hw_min_y, hw_max_y,
                                        W, H, &summary.x, &summary.y, summary_ready);
                    }
                    last_action_ms = t;
                } else if (inset_e < 4) {
                    int perp = touch_calib_sweep_edge_is_y(inset_e) ? st.y : st.x;
                    int bar  = inset_pos[inset_e][inset_k];
                    int d    = perp - bar;
                    if (d < 0) d = -d;
                    if (d <= INSET_ACCEPT) {
                        inset_raw[inset_e][inset_k][inset_t] =
                            touch_calib_sweep_edge_is_y(inset_e) ? raw_y : raw_x;
                        if (g_log)
                            fprintf(g_log, "I %u %s %d %d %d %d\n", t,
                                    EDGE_NAME[inset_e], bar, inset_t, raw_x, raw_y);
                        inset_t++;
                        last_action_ms = t;
                        if (inset_t >= INSET_TAPS) {
                            inset_med[inset_e][inset_k] = touch_calib_median3(
                                inset_raw[inset_e][inset_k][0],
                                inset_raw[inset_e][inset_k][1],
                                inset_raw[inset_e][inset_k][2]);
                            inset_have[inset_e][inset_k] = true;
                            if (g_log) {
                                fprintf(g_log, "# INSET %s panel %d median %d\n",
                                        EDGE_NAME[inset_e], bar,
                                        inset_med[inset_e][inset_k]);
                                fflush(g_log);
                            }
                            inset_t = 0;
                            inset_k++;
                            if (inset_k >= INSET_ROWS) {
                                inset_done[inset_e] = true;
                                inset_k = 0; inset_e++;
                                if (inset_e >= 4)
                                    print_inset(hw_min_x, hw_max_x,
                                                hw_min_y, hw_max_y, W, H,
                                                &summary.x, &summary.y,
                                                summary_ready);
                            }
                        }
                    }
                }
            }
        }
        /* ---- TARGETS (phase A) ---- */
        else if (mode == MODE_TARGETS) {
            if (st.pressed && debounced && hit_test(&btn_abort, st.x, st.y)) {
                mode = MODE_LIVE;
                last_action_ms = t;
            } else if (st.pressed && debounced && near_target(st.x, st.y, tgt_i)) {
                tap_raw_x[tgt_i][tap_i] = raw_x;
                tap_raw_y[tgt_i][tap_i] = raw_y;
                if (g_log)
                    fprintf(g_log, "T %u %d %d %d %d %d\n", t, tgt_i,
                            TARGETS[tgt_i].px, TARGETS[tgt_i].py, raw_x, raw_y);
                if (g_log) fflush(g_log);
                tap_i++;
                last_action_ms = t;
                if (tap_i >= TAPS_PER_TARGET) {
                    tgt_raw_x[tgt_i] = touch_calib_median3(tap_raw_x[tgt_i][0],
                                                           tap_raw_x[tgt_i][1],
                                                           tap_raw_x[tgt_i][2]);
                    tgt_raw_y[tgt_i] = touch_calib_median3(tap_raw_y[tgt_i][0],
                                                           tap_raw_y[tgt_i][1],
                                                           tap_raw_y[tgt_i][2]);
                    tap_i = 0;
                    tgt_i++;
                    if (tgt_i >= N_TARGETS) {
                        mode = MODE_BEZEL;
                        edge_i = 0;
                        edge_capturing = false;
                    }
                }
            }
        }
        /* ---- BEZEL PRESS (phase B) ---- */
        else if (mode == MODE_BEZEL) {
            if (st.held) {
                /* Only accept a press that is actually near the edge in
                 * question, so a stray tap on the NEXT button cannot pollute
                 * the extreme. */
                bool relevant =
                    (edge_i == 0 && st.y < H / 6)     || (edge_i == 1 && st.y > H * 5 / 6) ||
                    (edge_i == 2 && st.x < W / 6)     || (edge_i == 3 && st.x > W * 5 / 6);
                if (relevant) {
                    int v = (edge_i < 2) ? raw_y : raw_x;
                    bool want_min = (edge_i == 0 || edge_i == 2);
                    /* Keep the best value across EVERY press of this edge, not
                     * just the current one. Pressing flat against the plastic
                     * puts a tall contact patch half-on dead sensor, so the
                     * reported centroid scatters and people naturally press
                     * several times; resetting per press threw away the best
                     * attempt and carried forward whichever was last. REDO is
                     * the only thing that clears it. */
                    if (!edge_done[edge_i] && !edge_capturing) {
                        edge_raw[edge_i] = v;
                    } else if (want_min ? (v < edge_raw[edge_i]) : (v > edge_raw[edge_i])) {
                        edge_raw[edge_i] = v;
                    }
                    edge_capturing = true;
                }
            } else if (edge_capturing) {
                edge_capturing = false;
                edge_done[edge_i] = true;
                if (g_log) {
                    fprintf(g_log, "B %u %s %d %d\n", t, EDGE_NAME[edge_i],
                            edge_i >= 2 ? edge_raw[edge_i] : -1,
                            edge_i <  2 ? edge_raw[edge_i] : -1);
                    /* Flush: there are only a handful of these, so without it
                     * they sit in stdio's 4 KB buffer and the file looks as if
                     * the capture never happened. */
                    fflush(g_log);
                }
            }
            if (st.pressed && debounced) {
                if (hit_test(&btn_abort_b, st.x, st.y)) {
                    mode = MODE_LIVE;   /* no other way out of this phase */
                    last_action_ms = t;
                } else if (hit_test(&btn_redo, st.x, st.y)) {
                    edge_done[edge_i] = false;
                    edge_raw[edge_i]  = -1;
                    edge_capturing    = false;
                    last_action_ms = t;
                } else if (hit_test(&btn_next, st.x, st.y) && edge_done[edge_i]) {
                    edge_i++;
                    last_action_ms = t;
                    if (edge_i >= 4) {
                        build_summary(&summary, W, H,
                                      hw_min_x, hw_max_x, hw_min_y, hw_max_y);
                        print_summary(&summary, W, H,
                                      hw_min_x, hw_max_x, hw_min_y, hw_max_y);
                        summary_ready = true;
                        mode = MODE_SUMMARY;
                    }
                }
            }
        }
        /* ---- SUMMARY (phase C/D) ---- */
        else if (mode == MODE_SUMMARY) {
            if (st.pressed && debounced) {
                if (hit_test(&btn_done, st.x, st.y)) break;
                if (hit_test(&btn_apply, st.x, st.y) && summary_ready) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "X[%d..%d] Y[%d..%d]",
                             summary.x.in0, summary.x.in1,
                             summary.y.in0, summary.y.in1);
                    modal_dialog_init_confirm(&dlg, "WRITE INTERIOR FIT?", msg,
                                              "APPLY", RGB(30, 110, 60),
                                              "CANCEL", RGB(90, 90, 90));
                    modal_dialog_show(&dlg);
                    last_action_ms = t;
                }
            }
        }

        /* ================= render ================= */
        fb_clear(&fb, COLOR_BLACK);
        char b[96];

        if (mode == MODE_SUMMARY) {
            fb_draw_text(&fb, 20, 24, "TOUCH_RAW SUMMARY (NO CALIBRATION)",
                         COLOR_WHITE, 2);
            snprintf(b, sizeof(b), "HW RAW X[%d..%d] Y[%d..%d]  PANEL %dx%d",
                     hw_min_x, hw_max_x, hw_min_y, hw_max_y, W, H);
            fb_draw_text(&fb, 20, 50, b, COLOR_GRAY, 1);

            int y0 = 76;
            fb_draw_text(&fb, 20, y0, "AXIS            INTERIOR-ONLY   ALL-POINTS",
                         COLOR_YELLOW, 1);
            snprintf(b, sizeof(b), "X  RAW @ PANEL 0      %7d       %7d",
                     summary.x.in0, summary.x.all0);
            fb_draw_text(&fb, 20, y0 + 16, b, COLOR_WHITE, 1);
            snprintf(b, sizeof(b), "X  RAW @ PANEL %-4d   %7d       %7d",
                     W - 1, summary.x.in1, summary.x.all1);
            fb_draw_text(&fb, 20, y0 + 30, b, COLOR_WHITE, 1);
            snprintf(b, sizeof(b), "Y  RAW @ PANEL 0      %7d       %7d",
                     summary.y.in0, summary.y.all0);
            fb_draw_text(&fb, 20, y0 + 44, b, COLOR_WHITE, 1);
            snprintf(b, sizeof(b), "Y  RAW @ PANEL %-4d   %7d       %7d",
                     H - 1, summary.y.in1, summary.y.all1);
            fb_draw_text(&fb, 20, y0 + 58, b, COLOR_WHITE, 1);

            fb_draw_text(&fb, 20, y0 + 84,
                         "BEZEL PRESS -> PANEL PX UNDER THE INTERIOR FIT", COLOR_YELLOW, 1);
            snprintf(b, sizeof(b), "TOP raw %5d -> %4d      BOTTOM raw %5d -> %4d",
                     edge_raw[0], summary.edge_panel[0],
                     edge_raw[1], summary.edge_panel[1]);
            fb_draw_text(&fb, 20, y0 + 100, b, COLOR_CYAN, 1);
            snprintf(b, sizeof(b), "LEFT raw %5d -> %4d      RIGHT raw %5d -> %4d",
                     edge_raw[2], summary.edge_panel[2],
                     edge_raw[3], summary.edge_panel[3]);
            fb_draw_text(&fb, 20, y0 + 114, b, COLOR_CYAN, 1);

            fb_draw_text(&fb, 20, y0 + 140,
                         "EDGE-PROBE RESIDUAL (PREDICTED - DRAWN)", COLOR_YELLOW, 1);
            snprintf(b, sizeof(b), "X=20 %+d PX   X=780 %+d PX   Y=22 %+d PX   Y=458 %+d PX",
                     summary.probe_resid[2], summary.probe_resid[3],
                     summary.probe_resid[0], summary.probe_resid[1]);
            fb_draw_text(&fb, 20, y0 + 156, b, COLOR_WHITE, 1);

            /* Per axis, deliberately: this panel is H1 on X and H4 on Y, and a
             * single line cannot say that. */
            uint32_t cx = summary.reach_x ? COLOR_GREEN : COLOR_ORANGE;
            uint32_t cy = summary.reach_y ? COLOR_GREEN : COLOR_ORANGE;
            fb_draw_text(&fb, 20, y0 + 182, summary.verdict_x, cx, 1);
            fb_draw_text(&fb, 20, y0 + 198, summary.verdict_y, cy, 1);

            if (apply_msg[0])
                fb_draw_text(&fb, 20, y0 + 220, apply_msg, COLOR_MAGENTA, 1);

            draw_hit(&fb, &btn_done);
            draw_hit(&fb, &btn_apply);
        } else {
            draw_reference(&fb, file_bez_t, file_bez_b);

            if (mode == MODE_LIVE) {
                for (int i = 0; i < trail_n; i++)
                    fb_draw_pixel(&fb, trail[i].x, trail[i].y, COLOR_YELLOW);

                /* Extremes reached so far — the direct "how far can I get" answer */
                if (ext_px_max >= 0) {
                    uint32_t e = RGB(200, 0, 200);
                    fb_draw_line(&fb, ext_px_min, 0, ext_px_min, H - 1, e);
                    fb_draw_line(&fb, ext_px_max, 0, ext_px_max, H - 1, e);
                    fb_draw_line(&fb, 0, ext_py_min, W - 1, ext_py_min, e);
                    fb_draw_line(&fb, 0, ext_py_max, W - 1, ext_py_max, e);
                }
            }

            if (st.held && last_px >= 0)
                draw_crosshair(&fb, last_px, last_py, COLOR_CYAN);

            /* Pin bars, drawn inside the bezel line so they are actually visible */
            if (pin & PIN_XMIN) fb_fill_rect(&fb, 1, 20, 6, H - 40, COLOR_RED);
            if (pin & PIN_XMAX) fb_fill_rect(&fb, W - 7, 20, 6, H - 40, COLOR_RED);
            if (pin & PIN_YMIN) fb_fill_rect(&fb, 20, file_bez_t + 2, W - 40, 6, COLOR_RED);
            if (pin & PIN_YMAX) fb_fill_rect(&fb, 20, H - file_bez_b - 8, W - 40, 6, COLOR_RED);

            /* Readout panel. SWEEP and INSET draw their own, larger box in the
             * same place — two boxes at one position is an unreadable smear. */
            if (mode != MODE_SWEEP && mode != MODE_INSET) {
                text_boxed(&fb, 230, 120, 340, 130);
                snprintf(b, sizeof(b), "RAW   %5d %5d", last_raw_x, last_raw_y);
                fb_draw_text(&fb, 244, 130, b, COLOR_WHITE, 2);
                snprintf(b, sizeof(b), "PANEL %5d %5d", last_px, last_py);
                fb_draw_text(&fb, 244, 154, b, COLOR_CYAN, 2);
                snprintf(b, sizeof(b), "PX REACHED  X %d..%d  Y %d..%d",
                         ext_px_max < 0 ? 0 : ext_px_min, ext_px_max < 0 ? 0 : ext_px_max,
                         ext_py_max < 0 ? 0 : ext_py_min, ext_py_max < 0 ? 0 : ext_py_max);
                fb_draw_text(&fb, 244, 182, b, RGB(200, 0, 200), 1);
                snprintf(b, sizeof(b), "RAW REACHED X %d..%d  Y %d..%d",
                         ext_rx_max < 0 ? 0 : ext_rx_min, ext_rx_max < 0 ? 0 : ext_rx_max,
                         ext_ry_max < 0 ? 0 : ext_ry_min, ext_ry_max < 0 ? 0 : ext_ry_max);
                fb_draw_text(&fb, 244, 196, b, RGB(200, 0, 200), 1);
                snprintf(b, sizeof(b), "PINNED %s%s%s%s   SAMPLES %ld",
                         (pin & PIN_XMIN) ? "X-MIN " : "", (pin & PIN_XMAX) ? "X-MAX " : "",
                         (pin & PIN_YMIN) ? "Y-MIN " : "", (pin & PIN_YMAX) ? "Y-MAX " : "",
                         samples);
                fb_draw_text(&fb, 244, 214, b, pin ? COLOR_RED : COLOR_GRAY, 1);
                fb_draw_text(&fb, 244, 230, "NO CALIBRATION - NO BEZEL", COLOR_GRAY, 1);
            }

            if (mode == MODE_LIVE) {
                fb_draw_text(&fb, 244, 100,
                             "DRAG INTO EVERY EDGE AND CORNER", COLOR_WHITE, 1);
                draw_hit(&fb, &btn_exit);
                draw_hit(&fb, &btn_sweep);
                draw_hit(&fb, &btn_inset);
                draw_hit(&fb, &btn_targets);
            } else if (mode == MODE_SWEEP) {
                if (sweep_e < 4) {
                    const TouchCalibSweep *s = &g_sweep[sweep_e];
                    bool want_min = touch_calib_sweep_wants_min(sweep_e);
                    int  limit    = touch_calib_sweep_edge_is_y(sweep_e)
                                  ? (want_min ? hw_min_y : hw_max_y)
                                  : (want_min ? hw_min_x : hw_max_x);
                    bool touched  = (s->covered > 0);

                    /* Coverage cells laid along the edge under test, so a gap in
                     * the stroke is visible as a gap in the row. Green = that
                     * stretch of edge reached the hardware limit. */
                    for (int i = 0; i < TOUCH_CALIB_SWEEP_BUCKETS; i++) {
                        int cw, ch, cx2, cy2;
                        if (touch_calib_sweep_edge_is_y(sweep_e)) {
                            cw = W / TOUCH_CALIB_SWEEP_BUCKETS - 4;  ch = 16;
                            cx2 = i * (W / TOUCH_CALIB_SWEEP_BUCKETS) + 2;
                            cy2 = (sweep_e == 0) ? file_bez_t + 8
                                                 : H - 1 - file_bez_b - 24;
                        } else {
                            cw = 16;  ch = H / TOUCH_CALIB_SWEEP_BUCKETS - 4;
                            cy2 = i * (H / TOUCH_CALIB_SWEEP_BUCKETS) + 2;
                            cx2 = (sweep_e == 2) ? file_bez_l + 8
                                                 : W - 1 - file_bez_r - 24;
                        }
                        uint32_t col;
                        if (!s->bucket_hit[i])                       col = RGB(45, 45, 55);
                        else if (want_min ? (s->bucket[i] <= limit)
                                          : (s->bucket[i] >= limit)) col = COLOR_GREEN;
                        else                                         col = COLOR_ORANGE;
                        fb_fill_rect(&fb, cx2, cy2, cw, ch, col);
                        fb_draw_rect(&fb, cx2, cy2, cw, ch, RGB(90, 90, 110));
                    }

                    snprintf(b, sizeof(b),
                             "SWEEP THE %s EDGE - SLIDE ONE FINGER END TO END",
                             EDGE_NAME[sweep_e]);
                    text_boxed(&fb, 170, 120, 460, 160);
                    fb_draw_text(&fb, 184, 130, b, COLOR_YELLOW, 1);
                    snprintf(b, sizeof(b), "EDGE %d/4   COVERED %d/%d   %s",
                             sweep_e + 1, s->covered, TOUCH_CALIB_SWEEP_BUCKETS,
                             s->done ? "ENOUGH - PRESS NEXT" : "KEEP GOING");
                    fb_draw_text(&fb, 184, 150, b,
                                 s->done ? COLOR_GREEN : COLOR_GRAY, 1);
                    snprintf(b, sizeof(b), "RAW %5d %5d", last_raw_x, last_raw_y);
                    fb_draw_text(&fb, 184, 172, b, COLOR_WHITE, 2);
                    snprintf(b, sizeof(b), "EXTREME raw_%c %s  %d / %d",
                             touch_calib_sweep_edge_is_y(sweep_e) ? 'y' : 'x',
                             want_min ? "MIN" : "MAX",
                             touched ? s->extreme : 0, limit);
                    fb_draw_text(&fb, 184, 202, b,
                                 !touched ? COLOR_GRAY
                                 : (want_min ? (s->extreme <= limit)
                                             : (s->extreme >= limit))
                                   ? COLOR_GREEN : COLOR_ORANGE, 1);
                    snprintf(b, sizeof(b), "SAMPLES %ld   PINNED %ld",
                             s->samples, s->pinned);
                    fb_draw_text(&fb, 184, 222, b, COLOR_GRAY, 1);
                    fb_draw_text(&fb, 184, 242,
                                 "GREEN CELL = THAT STRETCH HIT THE LIMIT",
                                 COLOR_GRAY, 1);
                    fb_draw_text(&fb, 184, 258, "NO CALIBRATION - NO BEZEL",
                                 COLOR_GRAY, 1);
                } else {
                    fb_fill_rect(&fb, 40, 70, W - 80, 170, RGB(10, 10, 14));
                    fb_draw_rect(&fb, 40, 70, W - 80, 170, RGB(60, 60, 80));
                    fb_draw_text(&fb, 56, 80,
                                 "EDGE SWEEP - RAW AT THE PHYSICAL EDGE", COLOR_YELLOW, 1);
                    for (int e = 0; e < 4; e++) {
                        const TouchCalibSweep *s = &g_sweep[e];
                        bool want_min = touch_calib_sweep_wants_min(e);
                        int  limit = touch_calib_sweep_edge_is_y(e) ? (want_min ? hw_min_y : hw_max_y)
                                                  : (want_min ? hw_min_x : hw_max_x);
                        bool reached = s->covered &&
                                       (want_min ? (s->extreme <= limit)
                                                 : (s->extreme >= limit));
                        int shortfall = want_min ? (s->extreme - limit)
                                                : (limit - s->extreme);
                        if (!s->covered)
                            snprintf(b, sizeof(b), "%-6s  NOT SWEPT", EDGE_NAME[e]);
                        else
                            snprintf(b, sizeof(b),
                                     "%-6s  raw_%c %s %5d / %-5d  %+4d  %-10s %d/%d",
                                     EDGE_NAME[e], touch_calib_sweep_edge_is_y(e) ? 'y' : 'x',
                                     want_min ? "MIN" : "MAX", s->extreme, limit,
                                     -shortfall, reached ? "PINNED" : "NOT PINNED",
                                     s->covered, TOUCH_CALIB_SWEEP_BUCKETS);
                        fb_draw_text(&fb, 56, 104 + e * 18, b,
                                     !s->covered ? COLOR_GRAY
                                     : reached ? COLOR_GREEN : COLOR_ORANGE, 1);
                    }
                    fb_draw_text(&fb, 56, 190,
                                 "GREEN = THAT EDGE DRIVES RAW TO THE HARDWARE LIMIT",
                                 COLOR_GRAY, 1);
                    fb_draw_text(&fb, 56, 206,
                                 "FULL REPORT ON STDOUT AND /tmp/touch_raw.tsv",
                                 COLOR_GRAY, 1);
                }
                draw_hit(&fb, &btn_s_quit);
                draw_hit(&fb, &btn_s_redo);
                if (sweep_e < 4) draw_hit(&fb, &btn_s_next);
            } else if (mode == MODE_INSET) {
                if (inset_e < 4) {
                    int bar = inset_pos[inset_e][inset_k];
                    /* Thick bar, not a hairline: the finger has to cover it, and
                     * a 1 px line cannot be aimed at to better than the effect
                     * being measured. */
                    if (touch_calib_sweep_edge_is_y(inset_e))
                        fb_fill_rect(&fb, 0, bar - 3, W, 7, COLOR_GREEN);
                    else
                        fb_fill_rect(&fb, bar - 3, 0, 7, H, COLOR_GREEN);

                    snprintf(b, sizeof(b), "TAP THE GREEN BAR - %s EDGE",
                             EDGE_NAME[inset_e]);
                    text_boxed(&fb, 170, 120, 460, 160);
                    fb_draw_text(&fb, 184, 130, b, COLOR_YELLOW, 1);
                    snprintf(b, sizeof(b), "BAR %d/%d AT PANEL %s=%d   TAP %d/%d",
                             inset_k + 1, INSET_ROWS,
                             touch_calib_sweep_edge_is_y(inset_e) ? "Y" : "X", bar,
                             inset_t + 1, INSET_TAPS);
                    fb_draw_text(&fb, 184, 150, b, COLOR_GREEN, 1);
                    snprintf(b, sizeof(b), "RAW %5d %5d", last_raw_x, last_raw_y);
                    fb_draw_text(&fb, 184, 170, b, COLOR_WHITE, 2);
                    fb_draw_text(&fb, 184, 196,
                                 "NEXT SKIPS THIS BAR - REDO RESTARTS IT", COLOR_GRAY, 1);
                    /* Bars already measured on this edge, so drift is visible as
                     * you go rather than only in the final report. */
                    int ly = 214;
                    for (int k = 0; k < INSET_ROWS; k++) {
                        if (!inset_have[inset_e][k]) continue;
                        snprintf(b, sizeof(b), "PANEL %4d -> RAW %5d",
                                 inset_pos[inset_e][k], inset_med[inset_e][k]);
                        fb_draw_text(&fb, 184, ly, b, COLOR_CYAN, 1);
                        ly += 13;
                    }
                } else {
                    fb_fill_rect(&fb, 20, 50, W - 40, 200, RGB(10, 10, 14));
                    fb_draw_rect(&fb, 20, 50, W - 40, 200, RGB(60, 60, 80));
                    fb_draw_text(&fb, 34, 58,
                                 "INWARD STEP - WHERE RAW REACHES THE LIMIT",
                                 COLOR_YELLOW, 1);
                    int ry = 80;
                    for (int e = 0; e < 4; e++) {
                        /* Anchor on the INNERMOST measured bar and take the
                         * slope from the interior fit — never from two adjacent
                         * bars. Same reasoning as print_inset(): 10–20 px apart
                         * with ±80 raw of tap noise is not a slope. */
                        int ia = -1;
                        for (int k = INSET_ROWS - 1; k >= 0; k--)
                            if (inset_have[e][k]) { ia = k; break; }
                        if (ia < 0) {
                            snprintf(b, sizeof(b), "%-6s  NOT MEASURED", EDGE_NAME[e]);
                            fb_draw_text(&fb, 34, ry, b, COLOR_GRAY, 1);
                            ry += 28;
                            continue;
                        }
                        bool is_y     = touch_calib_sweep_edge_is_y(e);
                        bool want_min = touch_calib_sweep_wants_min(e);
                        int  limit = is_y ? (want_min ? hw_min_y : hw_max_y)
                                          : (want_min ? hw_min_x : hw_max_x);

                        /* Where clipping is observed: outermost run of bars that
                         * already read the limit. Noise-immune (a comparison). */
                        int flat_to = -1;
                        for (int k = 0; k < INSET_ROWS; k++) {
                            if (!inset_have[e][k]) continue;
                            bool sat = want_min ? (inset_med[e][k] <= limit)
                                                : (inset_med[e][k] >= limit);
                            if (!sat) break;
                            flat_to = k;
                        }

                        if (!summary_ready) {
                            if (flat_to >= 0)
                                snprintf(b, sizeof(b),
                                         "%-6s FLAT TO PANEL %d   (RUN TARGETS FOR A SLOPE)",
                                         EDGE_NAME[e], inset_pos[e][flat_to]);
                            else
                                snprintf(b, sizeof(b),
                                         "%-6s NO FLAT BAR   (RUN TARGETS FOR A SLOPE)",
                                         EDGE_NAME[e]);
                            fb_draw_text(&fb, 34, ry, b, COLOR_ORANGE, 1);
                        } else {
                            const TouchAxisFit *fit = is_y ? &summary.y : &summary.x;
                            int dim = is_y ? H : W;
                            double sl = (dim > 1)
                                      ? (double)(fit->in1 - fit->in0) / (double)(dim - 1)
                                      : 0.0;
                            if (sl <= 0.01) {
                                snprintf(b, sizeof(b), "%-6s  FIT SLOPE UNUSABLE - RETAKE",
                                         EDGE_NAME[e]);
                                fb_draw_text(&fb, 34, ry, b, COLOR_ORANGE, 1);
                            } else {
                                int at = (int)((double)inset_pos[e][ia] +
                                               ((double)limit - inset_med[e][ia]) / sl);
                                if (flat_to >= 0)
                                    snprintf(b, sizeof(b),
                                             "%-6s FIT %4.2f raw/px  RAW %4d AT PANEL %d"
                                             "  FLAT TO %d",
                                             EDGE_NAME[e], sl, limit, at,
                                             inset_pos[e][flat_to]);
                                else
                                    snprintf(b, sizeof(b),
                                             "%-6s FIT %4.2f raw/px  RAW %4d AT PANEL %d"
                                             "  NO FLAT BAR",
                                             EDGE_NAME[e], sl, limit, at);
                                fb_draw_text(&fb, 34, ry, b, COLOR_CYAN, 1);
                            }
                        }
                        /* One line of panel:raw pairs — five stacked lines per
                         * edge does not fit four edges on one screen. */
                        int n = 0;
                        b[0] = '\0';
                        for (int k = 0; k < INSET_ROWS && n < (int)sizeof(b) - 12; k++) {
                            if (!inset_have[e][k]) continue;
                            n += snprintf(b + n, sizeof(b) - n, "%d:%d  ",
                                          inset_pos[e][k], inset_med[e][k]);
                        }
                        fb_draw_text(&fb, 60, ry + 13, b, COLOR_WHITE, 1);
                        ry += 28;
                    }
                    fb_draw_text(&fb, 34, 196,
                                 "FLAT TO = LAST PANEL ROW STILL READING THE LIMIT;",
                                 COLOR_GRAY, 1);
                    fb_draw_text(&fb, 34, 210,
                                 "THAT IS WHERE THE CURVE ENDPOINT BELONGS",
                                 COLOR_GRAY, 1);
                    fb_draw_text(&fb, 34, 228,
                                 "FULL TABLE ON STDOUT AND /tmp/touch_raw.tsv",
                                 COLOR_GRAY, 1);
                }
                draw_hit(&fb, &btn_s_quit);
                draw_hit(&fb, &btn_s_redo);
                if (inset_e < 4) draw_hit(&fb, &btn_s_next);
            } else if (mode == MODE_TARGETS) {
                snprintf(b, sizeof(b), "TARGET %d/%d  (%d,%d)   TAP %d/%d",
                         tgt_i + 1, N_TARGETS, TARGETS[tgt_i].px, TARGETS[tgt_i].py,
                         tap_i + 1, TAPS_PER_TARGET);
                fb_draw_text(&fb, 244, 100, b, COLOR_GREEN, 1);
                draw_hit(&fb, &btn_abort);
                /* Last, so the readout box cannot bury a target that happens to
                 * sit behind it — (400,240) and (400,120) both do. */
                draw_target(&fb, TARGETS[tgt_i].px, TARGETS[tgt_i].py, COLOR_GREEN);
            } else if (mode == MODE_BEZEL) {
                snprintf(b, sizeof(b), "PRESS HARD AGAINST THE %s BEZEL AND HOLD",
                         EDGE_NAME[edge_i]);
                fb_draw_text(&fb, 200, 84, b, COLOR_YELLOW, 1);
                snprintf(b, sizeof(b), "EDGE %d/4   CAPTURED RAW %d",
                         edge_i + 1, edge_raw[edge_i]);
                fb_draw_text(&fb, 244, 100, b,
                             edge_done[edge_i] ? COLOR_GREEN : COLOR_GRAY, 1);
                fb_draw_text(&fb, 244, 116,
                             "PRESS, HOLD, THEN LIFT - THE LIFT IS WHAT CAPTURES",
                             COLOR_GRAY, 1);
                draw_hit(&fb, &btn_redo);
                draw_hit(&fb, &btn_abort_b);
                if (edge_done[edge_i]) draw_hit(&fb, &btn_next);
            }
        }

        if (modal_dialog_is_active(&dlg)) modal_dialog_draw(&dlg, &fb);

        fb_swap(&fb);
        usleep(FRAME_DELAY_ACTIVE_US / 4);   /* ~120 Hz: this is a tracking tool */
    }

    /* Always report the edge measurements, even on an early EXIT: a partial
     * sweep is still data, and losing it means tapping it all again. */
    print_sweep(hw_min_x, hw_max_x, hw_min_y, hw_max_y);
    print_inset(hw_min_x, hw_max_x, hw_min_y, hw_max_y, W, H,
                &summary.x, &summary.y, summary_ready);

    if (g_log) {
        fprintf(g_log, "# extremes: panel X %d..%d Y %d..%d   raw X %d..%d Y %d..%d\n",
                ext_px_min, ext_px_max, ext_py_min, ext_py_max,
                ext_rx_min, ext_rx_max, ext_ry_min, ext_ry_max);
        fclose(g_log);
    }
    printf("touch_raw: extremes reached — panel X %d..%d Y %d..%d, raw X %d..%d Y %d..%d\n",
           ext_px_min, ext_px_max, ext_py_min, ext_py_max,
           ext_rx_min, ext_rx_max, ext_ry_min, ext_ry_max);
    printf("touch_raw: log saved to %s\n", LOG_PATH);

    touch_close(&touch);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    fb_close(&fb);
    if (lock_fd >= 0) close(lock_fd);
    return 0;
}
