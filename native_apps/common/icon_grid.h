/*
 * icon_grid — the paged grid of icon tiles, shared by app_launcher and control_panel.
 *
 * One implementation of the layout, the tile, the page dots/chevrons and the hit-test,
 * so the launcher and any settings home screen cannot drift apart. The caller owns
 * its item list and its selection; this module only knows how many items a page
 * holds and where tile N of a page sits.
 *
 * Paging, not scrolling: 3x2 tiles per page in landscape, 2x3 in portrait. A tap in
 * the 50 px band inside either SCREEN_SAFE_* edge flips the page.
 *
 * Layout must be (re)computed after fb_init() AND touch_init() — the tiles are
 * hit-tested, so they live in SCREEN_SAFE_*, which is only right once the touch
 * inset is published — and again after any fb_set_bezel() or bpp change.
 */
#ifndef ICON_GRID_H
#define ICON_GRID_H

#include "framebuffer.h"
#include "ui_focus.h"
#include <stdbool.h>
#include <stdint.h>

#define ICON_GRID_ICON_SIZE 96      /* icons are drawn at exactly this size */

typedef struct {
    int cols, rows, per_page;
    int tile_w, tile_h;
    int content_w, content_h;       /* the tile block, gaps included */
    int left, top;                  /* its top-left corner */
    int exit_x, exit_y, exit_w, exit_h;   /* the exit button, top-right of the title band */
} IconGrid;

/* Derive the grid from the screen's current orientation and safe rectangle.
 * `top_reserve` is the band under SCREEN_SAFE_TOP kept for a title; the page
 * dots take a fixed band at the bottom. Prints one receipt line prefixed with
 * `tag` saying whether the last row lands inside the touchable rectangle. */
void icon_grid_layout(IconGrid *g, const Framebuffer *fb, int top_reserve,
                      const char *tag);

/* Pages needed for `count` items (at least 1). */
int  icon_grid_pages(const IconGrid *g, int count);

/* Top-left of tile `idx_on_page`. */
void icon_grid_tile_xy(const IconGrid *g, int idx_on_page, int *x, int *y);

/* One tile: the 96x96 `icon` if non-NULL, otherwise a `letter_color` square
 * carrying the label's first letter; the label centred underneath. */
void icon_grid_draw_tile(Framebuffer *fb, const IconGrid *g, int tx, int ty,
                         const char *label, const uint32_t *icon,
                         uint32_t letter_color, bool highlight);

/* The keyboard/gamepad selection ring around a tile. */
void icon_grid_draw_selection(Framebuffer *fb, const IconGrid *g, int tx, int ty);
/* The same ring around any rect — the one selection look, for keyboard focus
 * on widgets that are not tiles (control_panel's pages). */
void icon_grid_draw_ring(Framebuffer *fb, int x, int y, int w, int h);

/* Page dots (nothing when there is one page) and the edge chevrons. */
void icon_grid_draw_paging(Framebuffer *fb, const IconGrid *g, int page, int pages);

/* Index on the page of the tile under (x,y), or -1. `count_on_page` is how many
 * tiles the page actually shows. */
int  icon_grid_hit(const IconGrid *g, int count_on_page, int x, int y);

/* -1 = flip to the previous page, +1 = next, 0 = not a page-flip tap. */
int  icon_grid_page_hit(int x, int page, int pages);

/* Keyboard/pad movement over ABSOLUTE item indices, the launcher's model:
 * Left/Right step -1/+1 in reading order (off the end of a row onto the next,
 * off the end of a page onto the next page), Up/Down step -/+cols; a move that
 * would leave 0..count-1 does nothing (no wrap).  The page is the caller's to
 * follow (idx / per_page).  cur < 0 = nothing selected: returned unchanged —
 * where the first key lands is the caller's choice. */
int  icon_grid_nav(const IconGrid *g, int count, int cur, UiDir d);

/* As icon_grid_nav, plus the exit X (icon_grid_draw_exit) as a stop above the
 * grid: Up from the top row of `page` goes to ICON_GRID_NAV_EXIT and records
 * the tile in *from; Down from the X returns to *from when it is on `page`,
 * else to the page's first tile; any other key on the X stays there. */
#define ICON_GRID_NAV_EXIT (-2)
int  icon_grid_nav_exit(const IconGrid *g, int count, int page, int cur,
                        int *from, UiDir d);

/* The standard red-X exit button in the title band. The grid only draws it and
 * reports the tap; what exiting means is the caller's (the control panel just
 * exits). Needs `top_reserve` >= ICON_GRID_EXIT_H + 2. */
#define ICON_GRID_EXIT_W 55
#define ICON_GRID_EXIT_H 40
void icon_grid_draw_exit(Framebuffer *fb, const IconGrid *g);
bool icon_grid_exit_hit(const IconGrid *g, int x, int y);

/* Load a PPM icon, scaled to ICON_GRID_ICON_SIZE. NULL if it is missing or
 * unreadable (the tile then falls back to the letter). Free with free(). */
uint32_t *icon_grid_load_icon(const char *path);

/* A stable colour for a letter-tile, derived from the label. */
uint32_t icon_grid_letter_color(const char *label);

#endif
