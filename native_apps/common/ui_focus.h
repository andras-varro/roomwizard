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
#include <stdint.h>

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

/* ── Hold to exit ───────────────────────────────────────────────────────── */
/* A screen that shows every key — the Input page's testers — cannot give Esc
 * (or a pad's Select/Start) a meaning on a short press, so leaving it by key
 * takes a HOLD of UI_HOLD_EXIT_MS.  The clock is the caller's (get_time_ms()
 * when it read the event), not the evdev timestamp, which is CLOCK_REALTIME. */

#define UI_HOLD_EXIT_MS 1500

typedef struct {
    bool     down;       /* the exit key is held */
    uint32_t start_ms;   /* when its press (value 1) was read */
} UiHold;

/* Feed one EV_KEY value of an exit key, read at now.  1 starts a hold (a
 * second 1 while already down keeps the first start), 0 ends it, and 2 —
 * autorepeat — changes nothing: a repeat is never a new press, not even when
 * the press itself was lost to an evdev buffer overrun. */
void ui_hold_key(UiHold *h, int value, uint32_t now);

/* Progress of a hold in permille, 0..1000; *exit (may be NULL) says the hold
 * has lasted hold_ms.  Not down: 0 and no exit.  Wrap-safe in the uint32_t
 * millisecond clock.  hold_ms 0 exits at once on any hold. */
int ui_hold_progress(bool down, uint32_t now, uint32_t start, uint32_t hold_ms,
                     bool *exit);

#endif /* UI_FOCUS_H */
