/*
 * ui_focus — see ui_focus.h.  All arithmetic is on DOUBLED centres (2x + w),
 * so no division appears anywhere: this runs on a Cortex-A8 with no hardware
 * divide, and halving would also lose the odd pixel.
 */
#include "ui_focus.h"
#include <stddef.h>

static int c2x(const UiRect *r) { return 2 * r->x + r->w; }
static int c2y(const UiRect *r) { return 2 * r->y + r->h; }
static int iabs(int v) { return v < 0 ? -v : v; }

/* Doubled distance along d's axis from a to b (positive = b lies that way). */
static int along(const UiRect *a, const UiRect *b, UiDir d) {
    switch (d) {
    case UI_DIR_UP:    return c2y(a) - c2y(b);
    case UI_DIR_DOWN:  return c2y(b) - c2y(a);
    case UI_DIR_LEFT:  return c2x(a) - c2x(b);
    default:           return c2x(b) - c2x(a);
    }
}

static bool vertical(UiDir d) { return d == UI_DIR_UP || d == UI_DIR_DOWN; }

/* Doubled off-axis distance, cut to a quarter when the two rectangles overlap
 * across the axis (same row for LEFT/RIGHT, same column for UP/DOWN): an
 * overlapping neighbour is "in line" even when the centres are not. */
static bool in_line(const UiRect *a, const UiRect *b, UiDir d) {
    if (vertical(d)) return a->x < b->x + b->w && b->x < a->x + a->w;
    return a->y < b->y + b->h && b->y < a->y + a->h;
}

static int off_axis(const UiRect *a, const UiRect *b, UiDir d) {
    int o = vertical(d) ? iabs(c2x(a) - c2x(b)) : iabs(c2y(a) - c2y(b));
    return in_line(a, b, d) ? o >> 2 : o;
}

/* b lies in direction d from a: its NEAR edge is strictly past a's centre.  A centre
 * test alone let a wide button one row down count as "right of" a small one
 * whose centre it barely passed, and a row a few pixels out of line count as
 * "below" its neighbour. */
static bool beyond(const UiRect *a, const UiRect *b, UiDir d) {
    switch (d) {
    case UI_DIR_UP:    return 2 * (b->y + b->h) < c2y(a);
    case UI_DIR_DOWN:  return 2 * b->y > c2y(a);
    case UI_DIR_LEFT:  return 2 * (b->x + b->w) < c2x(a);
    default:           return 2 * b->x > c2x(a);
    }
}

int ui_focus_step(const UiRect *r, int n, int cur, UiDir d) {
    if (n <= 0 || cur < 0 || cur >= n) return -1;
    /* Two passes: in line first (sharing cur's row for LEFT/RIGHT, its column
     * for UP/DOWN), and only when nothing in line lies that way, anything in
     * the half-plane.  A plain distance score let a widget one row down and
     * half the width away beat the far end of cur's own row. */
    for (int pass = 0; pass < 2; pass++) {
        int best = -1, best_score = 0;
        for (int i = 0; i < n; i++) {
            if (i == cur) continue;
            if (pass == 0 && !in_line(&r[cur], &r[i], d)) continue;
            int p = along(&r[cur], &r[i], d);
            if (!beyond(&r[cur], &r[i], d)) continue;
            int score = p + 2 * off_axis(&r[cur], &r[i], d);
            if (best < 0 || score < best_score) { best = i; best_score = score; }
        }
        if (best >= 0) return best;
    }
    return -1;
}

int ui_focus_wrap(const UiRect *r, int n, const UiRect *from, UiDir d) {
    if (n <= 0) return -1;
    /* Alignment first: when any rectangle shares `from`'s row (LEFT/RIGHT) or
     * column (UP/DOWN), only those are candidates — RIGHT off the end of a row
     * lands at that row's start, never on a nearer-looking widget above it. */
    bool any_inline = false;
    for (int i = 0; i < n && !any_inline; i++)
        any_inline = in_line(from, &r[i], d);
    /* The far edge, in doubled centres: for RIGHT the smallest centre x (the
     * column a wrap lands in), for LEFT the largest, and so on. */
    bool forward = (d == UI_DIR_DOWN || d == UI_DIR_RIGHT);
    int edge = 0, have_edge = 0;
    for (int i = 0; i < n; i++) {
        if (any_inline && !in_line(from, &r[i], d)) continue;
        int c = vertical(d) ? c2y(&r[i]) : c2x(&r[i]);
        if (!have_edge || (forward ? c < edge : c > edge)) { edge = c; have_edge = 1; }
    }
    int best = -1, best_score = 0;
    for (int i = 0; i < n; i++) {
        if (any_inline && !in_line(from, &r[i], d)) continue;
        int c = vertical(d) ? c2y(&r[i]) : c2x(&r[i]);
        int p = forward ? c - edge : edge - c;      /* 0 at the far edge */
        int score = p + 2 * off_axis(from, &r[i], d);
        if (best < 0 || score < best_score) { best = i; best_score = score; }
    }
    return best;
}

int ui_focus_first(const UiRect *r, int n) {
    if (n <= 0) return -1;
    int best = 0, best_score = 4 * c2y(&r[0]) + c2x(&r[0]);
    for (int i = 1; i < n; i++) {
        int score = 4 * c2y(&r[i]) + c2x(&r[i]);
        if (score < best_score) { best = i; best_score = score; }
    }
    return best;
}

int ui_focus_move(const UiRect *r, int n, int cur, UiDir d) {
    if (n <= 0) return -1;
    if (cur < 0 || cur >= n) return ui_focus_first(r, n);
    int s = ui_focus_step(r, n, cur, d);
    return s >= 0 ? s : ui_focus_wrap(r, n, &r[cur], d);
}

int ui_focus_nearest(const UiRect *r, int n, const UiRect *old) {
    if (n <= 0) return -1;
    int best = 0;
    long long best_d = -1;
    for (int i = 0; i < n; i++) {
        long long dx = c2x(&r[i]) - c2x(old), dy = c2y(&r[i]) - c2y(old);
        long long dd = dx * dx + dy * dy;
        if (best_d < 0 || dd < best_d) { best = i; best_d = dd; }
    }
    return best;
}

void ui_tap_begin(UiTap *t, const UiRect *r) {
    t->phase = 1;
    t->x = r->x + (r->w >> 1);
    t->y = r->y + (r->h >> 1);
}

bool ui_tap_frame(UiTap *t, bool real_touching, int *x, int *y,
                  bool *touching, bool *pressed, bool *released) {
    if (t->phase == 0) return false;
    if (real_touching) { t->phase = 0; return false; }
    *x = t->x;
    *y = t->y;
    if (t->phase == 1) {
        *touching = true;  *pressed = true;  *released = false;
        t->phase = 2;
    } else {
        *touching = false; *pressed = false; *released = true;
        t->phase = 0;
    }
    return true;
}
