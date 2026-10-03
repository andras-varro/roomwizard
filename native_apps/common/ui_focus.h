/*
 * ui_focus — keyboard / pad focus over a set of on-screen rectangles.
 *
 * Pure arithmetic: no framebuffer, no widget type, no common.c, so any
 * component (control_panel today; vnc_client could link it) can use it.  The
 * caller hands in the rectangles of whatever is focusable THIS frame — computed
 * from the same values its hit-test uses — and gets back an index.
 *
 * Movement is spatial: the nearest centre in the pressed direction, with an
 * off-axis penalty, so a misaligned layout still moves the way the eye expects.
 * When nothing lies in that direction, focus WRAPS to the rectangle farthest
 * the opposite way, preferring the one best aligned with where focus was.
 *
 * Activation is a synthetic tap (UiTap): one frame touching at the focused
 * rectangle's centre, the next frame released at the same point — so a
 * widget that acts on the press edge and one that acts on the release both
 * see a complete tap through their ordinary touch path.
 */
#ifndef UI_FOCUS_H
#define UI_FOCUS_H

#include <stdbool.h>

typedef struct { int x, y, w, h; } UiRect;

typedef enum { UI_DIR_UP, UI_DIR_DOWN, UI_DIR_LEFT, UI_DIR_RIGHT } UiDir;

/* The neighbour of r[cur] in direction d, or -1 when none lies that way (and
 * when cur is out of range or n <= 0).  No wrap. */
int ui_focus_step(const UiRect *r, int n, int cur, UiDir d);

/* The wrap target for a move from `from` in direction d: the rectangle
 * farthest in the opposite direction, best aligned with `from` off-axis.
 * `from` need not be one of r (a page flip wraps from the old page's rect).
 * May return the index of `from` itself.  -1 only when n <= 0. */
int ui_focus_wrap(const UiRect *r, int n, const UiRect *from, UiDir d);

/* step, else wrap.  cur < 0 (or out of range) = nothing focused yet: returns
 * ui_focus_first().  n <= 0 returns -1. */
int ui_focus_move(const UiRect *r, int n, int cur, UiDir d);

/* The top-left-most rectangle: where focus lands first.  -1 when n <= 0. */
int ui_focus_first(const UiRect *r, int n);

/* Re-pick for a list that changed under the focus: the rectangle whose centre
 * is nearest `old`'s.  -1 when n <= 0. */
int ui_focus_nearest(const UiRect *r, int n, const UiRect *old);

/* ── Synthetic tap ──────────────────────────────────────────────────────── */

typedef struct {
    int phase;      /* 0 idle, 1 press frame next, 2 release frame next */
    int x, y;
} UiTap;

/* Queue a tap at the centre of *r. */
void ui_tap_begin(UiTap *t, const UiRect *r);

/* Call once per frame after reading the real touch.  While a tap is queued it
 * overwrites the frame's touch: phase 1 gives touching + pressed at the
 * centre, phase 2 gives not touching + released at the same point.  A real
 * finger down (real_touching) cancels it and wins.  Returns true when it
 * overwrote the outputs this frame. */
bool ui_tap_frame(UiTap *t, bool real_touching, int *x, int *y,
                  bool *touching, bool *pressed, bool *released);

#endif /* UI_FOCUS_H */
