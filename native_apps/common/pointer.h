/*
 * pointer — the mouse pointer on the two home screens (app_launcher and the
 * Control Panel): one arrow, moved by gamepad.c's relative motion, clamped to
 * SCREEN_SAFE_* (a click must land where a finger could), and painted without
 * repainting the screen.
 *
 * Show/hide is the focus ring's rule turned round: mouse motion or a click
 * shows it, a real finger press or a nav key hides it.  It starts hidden at
 * the centre of the safe rectangle.
 *
 * A left click IS a touch: press, hold and release are written into the app's
 * TouchState at the pointer position (pointer_route_touch()), so every widget
 * that acts on a touch release over its own rect works with no code of its own.
 * A real finger always wins over the mouse.
 *
 * Drawing — the back buffer stays clean.  pointer_paint() draws the arrow into
 * the back buffer over a saved copy of what is under it, copies the old and
 * the new rectangle to the panel with fb_swap_rect(), and puts the saved pixels
 * back.  So the back buffer never holds the arrow, an erase is a plain
 * fb_swap_rect() of the old rectangle, and a full repaint by the app needs only
 * pointer_invalidate() after its fb_swap() before the next pointer_paint().
 * It assumes the back buffer equals the screen between frames, which is true of
 * any loop that draws only in its repaint step.  Nothing is drawn when nothing
 * moved, so an idle screen costs nothing.
 *
 * The model (pointer_model_*, pointer_route_touch) is pure, for
 * tests/pointer_test.c.
 */
#ifndef POINTER_H
#define POINTER_H

#include "framebuffer.h"
#include "touch_input.h"
#include "gamepad.h"
#include <stdbool.h>
#include <stdint.h>

/* Hotspot bounds, right/bottom exclusive. */
typedef struct {
    int left, top, right, bottom;
} PointerBounds;

typedef struct {
    int  x, y;          /* the hotspot (the arrow's tip), logical pixels */
    bool shown;
    bool down;          /* a left press went to the app; its release is owed */
} PointerModel;

/* One frame of input for the model. */
typedef struct {
    int  dx, dy;                        /* this poll's motion */
    bool left_pressed, left_released, left_held;
    bool touch_press;                   /* a real finger went down */
    bool nav_key;                       /* an arrow, Enter or Back key */
} PointerInput;

/* What the click hands to the app's touch path this frame. */
typedef struct {
    bool pressed, released, held;
} PointerTouch;

void pointer_model_init(PointerModel *m, PointerBounds b);
PointerTouch pointer_model_step(PointerModel *m, const PointerInput *in,
                                PointerBounds b);

/* Writes the click into *ts at the pointer and returns true — unless the
 * mouse has no edge or level this frame, or a real finger is active (ts
 * already pressed, held or released), in which case *ts is left alone. */
bool pointer_route_touch(const PointerModel *m, PointerTouch t, TouchState *ts);

/* ── The on-screen pointer ──────────────────────────────────────────────── */

#define POINTER_SPRITE_W 12
#define POINTER_SPRITE_H 19

typedef struct {
    PointerModel m;
    bool on_screen;             /* the panel shows it at (sx, sy) */
    int  sx, sy;
    uint8_t under[POINTER_SPRITE_W * POINTER_SPRITE_H * 4];   /* save-under, raw pixels */
} Pointer;

/* Hidden, at the centre of SCREEN_SAFE_* — call after fb_init() and
 * touch_init(). */
void pointer_init(Pointer *p);

/* One frame: feed this poll's mouse state, whether a real finger went down
 * and whether a nav key was used; routes a click into *ts (see
 * pointer_route_touch).  Clamps to the current SCREEN_SAFE_*, so a bezel
 * change needs no call.  Call before the app reads *ts. */
void pointer_update(Pointer *p, const InputState *in, bool touch_press,
                    bool nav_key, TouchState *ts);

/* Something else owned the input (a full-screen run, a child app) and the
 * caller has drained the touch and gamepad events it left: forget the click
 * in progress too.  Otherwise the press that started the run keeps `down`
 * set, its release having gone to the drain, and the first frame back with
 * the left button reading down (still held, or its release lost) is routed
 * as a held touch over the same button — which a rebuilt button takes as a
 * new press and starts the run again.  The position is kept. */
void pointer_drain(Pointer *p);

/* The app has just fb_swap()ed a full frame, which the arrow is not in. */
void pointer_invalidate(Pointer *p);

/* Bring the panel up to date: erase, move or draw the arrow with
 * fb_swap_rect() of its rectangles only.  Returns whether it touched the
 * panel (nothing changed: false, nothing drawn). */
bool pointer_paint(Pointer *p, Framebuffer *fb);

#endif /* POINTER_H */
