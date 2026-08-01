/*
 * touch_raw — digitizer reach, measured with NO calibration and NO bezel.
 *
 * Why this exists
 * ---------------
 * TOUCH_REACH_INVESTIGATION.md asks whether the untouchable band at the panel
 * border is a physical sensor inset (H4) or an artifact of the 9-tap
 * calibration fit (H1).  Every number gathered so far was inferred *through* a
 * calibration, so it cannot separate the two.  This tool removes every layer of
 * interpretation between finger and pixel:
 *
 *   - no calibration : the raw range is reset to what EVIOCGABS reports, so
 *                      /etc/touch_calibration.conf line 1 has no effect here;
 *   - no bezel       : fb_set_bezel(fb,0,0,0,0) makes the drawing surface the
 *                      full 800x480 panel, so a drawn pixel IS a panel pixel;
 *   - no private math: the library is *configured* into an identity map rather
 *                      than bypassed, so what you see is what scale_coordinates()
 *                      produces (native_apps/CLAUDE.md: one implementation only).
 *
 * Two modes
 * ---------
 * LIVE     Free drag.  Cyan dot + full-screen crosshair (your finger covers the
 *          dot, not the crosshair), yellow trail, the extremes reached so far,
 *          and a PINNED flag the instant raw sticks at its hardware limit.
 *
 * TARGETS  Tap 11 marked crosshairs (median of 3 taps each), then press hard
 *          against each of the four bezel edges.  The tool then fits the axes
 *          from INTERIOR targets only and reports what that fit predicts at the
 *          panel edges — experiment E1.  If the interior fit reaches the edges,
 *          the sensor is fine and the 9-tap calibration is contaminated (H1).
 *
 * Log: /tmp/touch_raw.tsv  (.tsv, not .log — .gitignore drops *.log)
 *   L <ms> <raw_x> <raw_y> <panel_x> <panel_y> <pinmask>   live sample
 *   T <ms> <idx> <target_x> <target_y> <raw_x> <raw_y>     target tap
 *   B <ms> <edge> <raw_x> <raw_y>                          bezel press extreme
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
#include "../common/common.h"

#define LOG_PATH    "/tmp/touch_raw.tsv"
#define CALIB_PATH  "/etc/touch_calibration.conf"
#define TRAIL_MAX   6000
#define TAPS_PER_TARGET 3

/* Pin mask bits: raw sitting exactly on a hardware limit */
#define PIN_XMIN 1
#define PIN_XMAX 2
#define PIN_YMIN 4
#define PIN_YMAX 8

/* A target counts as "interior" along an axis when it is far enough from both
 * ends of that axis that any edge compression cannot reach it.  The 9-tap
 * calibration insets its crosshairs by only 40 px, which is the very thing H1
 * says is too close; these thresholds are deliberately far more conservative. */
#define INTERIOR_MARGIN_X 100
#define INTERIOR_MARGIN_Y  80

/* The interior fit "reaches the edge" if it predicts a raw value this close to
 * the hardware limit.  40 counts is ~8 px of panel — smaller than the effect
 * under test, larger than the run-to-run tap noise. */
#define REACH_TOL_RAW 40

typedef enum {
    MODE_LIVE = 0,
    MODE_TARGETS,
    MODE_BEZEL,
    MODE_SUMMARY
} Mode;

typedef struct { int px, py; } Target;

/* Interior cross + two off-axis points, then four edge probes.  Every target
 * feeds BOTH axis fits: an edge probe at (20,240) is an outlier on X but a
 * perfectly good interior sample on Y. */
static const Target TARGETS[] = {
    {150, 240}, {400, 240}, {650, 240},   /* interior X sweep */
    {400, 120}, {400, 360},               /* interior Y sweep */
    {200, 150}, {600, 330},               /* off-axis, both interior */
    { 20, 240}, {780, 240},               /* X edge probes */
    {400,  22}, {400, 458},               /* Y edge probes */
};
#define N_TARGETS ((int)(sizeof(TARGETS) / sizeof(TARGETS[0])))

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

static int  tap_raw_x[N_TARGETS][TAPS_PER_TARGET];
static int  tap_raw_y[N_TARGETS][TAPS_PER_TARGET];
static int  tgt_raw_x[N_TARGETS], tgt_raw_y[N_TARGETS];  /* median per target */
static int  edge_raw[4];                                  /* bezel-press extremes */
static bool edge_done[4];

static void trail_push(int x, int y) {
    trail[trail_head].x = x;
    trail[trail_head].y = y;
    trail_head = (trail_head + 1) % TRAIL_MAX;
    if (trail_n < TRAIL_MAX) trail_n++;
}

static int median3(int a, int b, int c) {
    if (a > b) { int t = a; a = b; b = t; }
    if (b > c) { int t = b; b = c; c = t; }
    if (a > b) { int t = a; a = b; b = t; }
    return b;
}

/* Panel coordinate predicted for a raw value by a fitted range — the same
 * arithmetic scale_coordinates() uses, so predictions and reality agree. */
static int predict_panel(int raw, int raw_at_0, int raw_at_max, int dim) {
    int span = raw_at_max - raw_at_0;
    if (span == 0) return 0;
    return (raw - raw_at_0) * (dim - 1) / span;
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

/* ---- calibration backup ------------------------------------------------- */
static int backup_calibration(char *out_path, size_t out_len) {
    for (int n = 1; n < 100; n++) {
        char path[256];
        snprintf(path, sizeof(path), "%s.bak%d", CALIB_PATH, n);
        if (access(path, F_OK) == 0) continue;

        FILE *src = fopen(CALIB_PATH, "rb");
        if (!src) return -1;
        FILE *dst = fopen(path, "wb");
        if (!dst) { fclose(src); return -1; }

        char buf[1024];
        size_t got;
        while ((got = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, got, dst);
        fclose(src);
        fclose(dst);

        snprintf(out_path, out_len, "%s", path);
        return 0;
    }
    return -1;
}

/* ---- summary ------------------------------------------------------------ */
typedef struct {
    int in0, in1;      /* interior-only fit: raw at panel 0 / panel dim-1 */
    int all0, all1;    /* all-point fit                                    */
    bool in_ok, all_ok;
    int dim;
} AxisFit;

typedef struct {
    AxisFit x, y;
    int  edge_panel[4];   /* bezel-press raw mapped through the interior fit */
    int  probe_resid[4];  /* edge-probe residual: predicted - drawn, in px   */
    bool verdict_h1;
    char verdict[96];
} Summary;

static void fit_axis(AxisFit *f, const int *raw, const int *pos, const bool *interior,
                     int n, int dim) {
    int ri[N_TARGETS], pi[N_TARGETS], ni = 0;
    for (int i = 0; i < n; i++) {
        if (!interior[i]) continue;
        ri[ni] = raw[i]; pi[ni] = pos[i]; ni++;
    }
    f->dim = dim;
    f->in_ok  = (ni >= 2) &&
                touch_fit_axis_range(ri, pi, ni, dim, &f->in0, &f->in1) == 0;
    f->all_ok = touch_fit_axis_range(raw, pos, n, dim, &f->all0, &f->all1) == 0;
    if (!f->in_ok)  { f->in0  = 0; f->in1  = 4095; }
    if (!f->all_ok) { f->all0 = 0; f->all1 = 4095; }
}

static void build_summary(Summary *s, int panel_w, int panel_h,
                          int hw_min_x, int hw_max_x, int hw_min_y, int hw_max_y) {
    int rx[N_TARGETS], ry[N_TARGETS], px[N_TARGETS], py[N_TARGETS];
    bool ix[N_TARGETS], iy[N_TARGETS];

    for (int i = 0; i < N_TARGETS; i++) {
        rx[i] = tgt_raw_x[i];  ry[i] = tgt_raw_y[i];
        px[i] = TARGETS[i].px; py[i] = TARGETS[i].py;
        ix[i] = (px[i] >= INTERIOR_MARGIN_X && px[i] <= panel_w - 1 - INTERIOR_MARGIN_X);
        iy[i] = (py[i] >= INTERIOR_MARGIN_Y && py[i] <= panel_h - 1 - INTERIOR_MARGIN_Y);
    }

    fit_axis(&s->x, rx, px, ix, N_TARGETS, panel_w);
    fit_axis(&s->y, ry, py, iy, N_TARGETS, panel_h);

    /* Where does a hard press against each bezel land, according to the fit
     * that never saw an edge sample?  This is the number the whole question
     * turns on. */
    s->edge_panel[0] = predict_panel(edge_done[0] ? edge_raw[0] : hw_min_y,
                                     s->y.in0, s->y.in1, panel_h);
    s->edge_panel[1] = predict_panel(edge_done[1] ? edge_raw[1] : hw_max_y,
                                     s->y.in0, s->y.in1, panel_h);
    s->edge_panel[2] = predict_panel(edge_done[2] ? edge_raw[2] : hw_min_x,
                                     s->x.in0, s->x.in1, panel_w);
    s->edge_panel[3] = predict_panel(edge_done[3] ? edge_raw[3] : hw_max_x,
                                     s->x.in0, s->x.in1, panel_w);

    /* Edge-probe residuals: does the interior fit still describe a target at
     * x=20 / y=22?  A large residual is compression reaching that far in. */
    s->probe_resid[0] = predict_panel(ry[9],  s->y.in0, s->y.in1, panel_h) - py[9];
    s->probe_resid[1] = predict_panel(ry[10], s->y.in0, s->y.in1, panel_h) - py[10];
    s->probe_resid[2] = predict_panel(rx[7],  s->x.in0, s->x.in1, panel_w) - px[7];
    s->probe_resid[3] = predict_panel(rx[8],  s->x.in0, s->x.in1, panel_w) - px[8];

    int d[4] = {
        s->x.in0 - hw_min_x, s->x.in1 - hw_max_x,
        s->y.in0 - hw_min_y, s->y.in1 - hw_max_y
    };
    int worst = 0;
    for (int i = 0; i < 4; i++) {
        int a = d[i] < 0 ? -d[i] : d[i];
        if (a > worst) worst = a;
    }
    s->verdict_h1 = (worst <= REACH_TOL_RAW);
    if (s->verdict_h1)
        snprintf(s->verdict, sizeof(s->verdict),
                 "H1: sensor reaches the edges (worst %d raw) - 9-tap fit is at fault",
                 worst);
    else
        snprintf(s->verdict, sizeof(s->verdict),
                 "H4: real inset - interior fit misses a limit by %d raw counts",
                 worst);
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
        fprintf(f, "%s\n%s%s\n", p, p, s->verdict);

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
    int hw_min_x = 0, hw_max_x = 4095, hw_min_y = 0, hw_max_y = 4095;
    struct input_absinfo abs_x, abs_y;
    if (ioctl(touch.fd, EVIOCGABS(ABS_X), &abs_x) == 0 &&
        ioctl(touch.fd, EVIOCGABS(ABS_Y), &abs_y) == 0) {
        hw_min_x = abs_x.minimum; hw_max_x = abs_x.maximum;
        hw_min_y = abs_y.minimum; hw_max_y = abs_y.maximum;
    }
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
        fflush(g_log);
    }

    printf("touch_raw: panel %dx%d, raw range X[%d..%d] Y[%d..%d] (calibration ignored)\n",
           W, H, hw_min_x, hw_max_x, hw_min_y, hw_max_y);
    printf("touch_raw: logging to %s\n", LOG_PATH);

    /* Buttons stay well clear of the border so the tool is still operable if
     * the inset turns out to be real. */
    Hit btn_exit    = { 60, 380, 170, 70, "EXIT",      RGB(120, 30, 30) };
    Hit btn_targets = { 570, 380, 170, 70, "TARGETS",  RGB(30, 80, 120) };
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

    Mode mode = MODE_LIVE;
    int  tgt_i = 0, tap_i = 0, edge_i = 0;
    bool edge_capturing = false;
    Summary summary;
    memset(&summary, 0, sizeof(summary));
    bool summary_ready = false;
    char apply_msg[128] = "";

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
                if (backup_calibration(bak, sizeof(bak)) == 0) {
                    /* Save the interior fit, then put the tool straight back to
                     * uncalibrated so what it displays never silently changes. */
                    touch.calib.bezel_top    = file_bez_t;
                    touch.calib.bezel_bottom = file_bez_b;
                    touch.calib.bezel_left   = file_bez_l;
                    touch.calib.bezel_right  = file_bez_r;
                    touch_set_raw_range(&touch, summary.x.in0, summary.x.in1,
                                        summary.y.in0, summary.y.in1);
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
                    tgt_raw_x[tgt_i] = median3(tap_raw_x[tgt_i][0],
                                               tap_raw_x[tgt_i][1],
                                               tap_raw_x[tgt_i][2]);
                    tgt_raw_y[tgt_i] = median3(tap_raw_y[tgt_i][0],
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

            uint32_t vc = summary.verdict_h1 ? COLOR_GREEN : COLOR_ORANGE;
            fb_draw_text(&fb, 20, y0 + 186, summary.verdict_h1 ? "H1" : "H4", vc, 3);
            fb_draw_text(&fb, 70, y0 + 194, summary.verdict, vc, 1);

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

            /* Readout panel */
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

            if (mode == MODE_LIVE) {
                fb_draw_text(&fb, 244, 100,
                             "DRAG INTO EVERY EDGE AND CORNER", COLOR_WHITE, 1);
                draw_hit(&fb, &btn_exit);
                draw_hit(&fb, &btn_targets);
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
