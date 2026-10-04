/*
 * pointer — see pointer.h.
 */
#include "pointer.h"
#include "common.h"
#include <string.h>

/* ── The model (pure) ───────────────────────────────────────────────────── */

static int clamp_axis(int v, int lo, int hi_excl) {
    if (v >= hi_excl) v = hi_excl - 1;
    if (v < lo) v = lo;              /* last: an empty range pins to lo */
    return v;
}

void pointer_model_init(PointerModel *m, PointerBounds b) {
    m->x = b.left + (b.right - b.left) / 2;
    m->y = b.top  + (b.bottom - b.top) / 2;
    m->x = clamp_axis(m->x, b.left, b.right);
    m->y = clamp_axis(m->y, b.top, b.bottom);
    m->shown = false;
    m->down  = false;
}

PointerTouch pointer_model_step(PointerModel *m, const PointerInput *in,
                                PointerBounds b) {
    PointerTouch t = { false, false, false };

    /* Re-clamp every frame: the bounds are the live SCREEN_SAFE_*, which a
     * bezel change or a calibration moves under us. */
    m->x = clamp_axis(m->x + in->dx, b.left, b.right);
    m->y = clamp_axis(m->y + in->dy, b.top, b.bottom);
    if (in->dx || in->dy) m->shown = true;

    if (in->touch_press) {
        /* A real finger wins: it hides the pointer, and a click it lands on
         * (or interrupts) is dropped, release included. */
        m->down = false;
    } else {
        if (in->left_pressed) {
            t.pressed = true;
            m->down   = true;
            m->shown  = true;        /* a click is mouse use too */
        }
        if (m->down && in->left_released) {
            t.released = true;
            m->down    = false;
        }
        t.held = m->down && in->left_held;
    }

    if (in->touch_press || in->nav_key) m->shown = false;
    return t;
}

bool pointer_route_touch(const PointerModel *m, PointerTouch t, TouchState *ts) {
    if (!t.pressed && !t.released && !t.held) return false;
    if (ts->pressed || ts->held || ts->released) return false;
    ts->x        = m->x;
    ts->y        = m->y;
    ts->pressed  = t.pressed;
    ts->released = t.released;
    ts->held     = t.held;
    return true;
}

/* ── The on-screen pointer ──────────────────────────────────────────────── */

/* 'X' outline, '.' fill, ' ' transparent; the tip (0,0) is the hotspot. */
static const char *const ARROW[POINTER_SPRITE_H] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.........X ",
    "X......XXXXX",
    "X...X..X    ",
    "X..XX..X    ",
    "X.X  X..X   ",
    "XX   X..X   ",
    "X     X..X  ",
    "      X..X  ",
    "       XX   ",
};

static PointerBounds safe_bounds(void) {
    PointerBounds b = { SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP,
                        SCREEN_SAFE_RIGHT, SCREEN_SAFE_BOTTOM };
    return b;
}

void pointer_init(Pointer *p) {
    memset(p, 0, sizeof(*p));
    pointer_model_init(&p->m, safe_bounds());
}

void pointer_update(Pointer *p, const InputState *in, bool touch_press,
                    bool nav_key, TouchState *ts) {
    PointerInput pi = { 0 };
    pi.dx            = in->mouse_dx;
    pi.dy            = in->mouse_dy;
    pi.left_pressed  = in->mouse_left_pressed  != 0;
    pi.left_released = in->mouse_left_released != 0;
    pi.left_held     = in->mouse_left_held     != 0;
    pi.touch_press   = touch_press;
    pi.nav_key       = nav_key;
    PointerTouch t = pointer_model_step(&p->m, &pi, safe_bounds());
    pointer_route_touch(&p->m, t, ts);
}

void pointer_drain(Pointer *p) {
    p->m.down = false;
}

void pointer_invalidate(Pointer *p) {
    p->on_screen = false;
}

/* The arrow's rectangle at (x,y), clipped to the surface.  False if empty. */
static bool clip_rect(const Framebuffer *fb, int x, int y, int *w, int *h) {
    *w = POINTER_SPRITE_W;
    *h = POINTER_SPRITE_H;
    if (x + *w > (int)fb->width)  *w = (int)fb->width  - x;
    if (y + *h > (int)fb->height) *h = (int)fb->height - y;
    return x >= 0 && y >= 0 && *w > 0 && *h > 0;
}

/* Copy the arrow's rectangle between the back buffer and p->under. */
static void under_copy(Pointer *p, Framebuffer *fb, int x, int y, int w, int h,
                       bool save) {
    const size_t bpp = fb->bytes_per_pixel;
    uint8_t *back = (uint8_t *)fb->back_buffer;
    for (int r = 0; r < h; r++) {
        uint8_t *row = back + ((size_t)(y + r) * fb->width + (size_t)x) * bpp;
        uint8_t *sav = p->under + (size_t)r * POINTER_SPRITE_W * bpp;
        if (save) memcpy(sav, row, (size_t)w * bpp);
        else      memcpy(row, sav, (size_t)w * bpp);
    }
}

bool pointer_paint(Pointer *p, Framebuffer *fb) {
    if (!fb->double_buffering || !fb->back_buffer || fb->bytes_per_pixel > 4)
        return false;
    const bool want = p->m.shown;
    if (!want && !p->on_screen) return false;
    if (want && p->on_screen && p->sx == p->m.x && p->sy == p->m.y)
        return false;

    int nw = 0, nh = 0;
    const bool draw = want && clip_rect(fb, p->m.x, p->m.y, &nw, &nh);
    if (!draw && !p->on_screen) return false;   /* off the surface: nothing */
    if (draw) {
        under_copy(p, fb, p->m.x, p->m.y, nw, nh, true);
        for (int r = 0; r < nh; r++)
            for (int c = 0; c < nw; c++) {
                char k = ARROW[r][c];
                if (k == 'X')
                    fb_draw_pixel(fb, p->m.x + c, p->m.y + r, COLOR_BLACK);
                else if (k == '.')
                    fb_draw_pixel(fb, p->m.x + c, p->m.y + r, COLOR_WHITE);
            }
    }
    /* The old rectangle shows the clean back buffer (plus any overlap with the
     * new arrow, which is already in it), then the new one shows the arrow. */
    if (p->on_screen)
        fb_swap_rect(fb, p->sx, p->sy, POINTER_SPRITE_W, POINTER_SPRITE_H);
    if (draw) {
        fb_swap_rect(fb, p->m.x, p->m.y, nw, nh);
        under_copy(p, fb, p->m.x, p->m.y, nw, nh, false);
    }
    p->on_screen = draw;
    p->sx = p->m.x;
    p->sy = p->m.y;
    return true;
}
