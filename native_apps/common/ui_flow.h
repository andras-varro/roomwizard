/**
 * Flow (wrap) layout for fixed-size buttons, plus the one label-fit rule.
 *
 * Items keep their size; a new row starts only when the next item would
 * overflow the available width.  Pure integer math, no framebuffer, so the
 * host test can reach every branch.
 */
#ifndef UI_FLOW_H
#define UI_FLOW_H

#include "ui_focus.h"   /* UiRect { x, y, w, h } */

/* Horizontal space kept clear on EACH side of a label inside a button, beyond
 * the border: button_draw -> text_draw_centered puts the text origin at
 * centre - strlen*6*scale/2, and the ink is strlen*6*scale - scale wide
 * (text_measure_width counts a trailing scale-px glyph gap that holds no ink). */
#define UI_LABEL_PAD 4

/* Ink width of label at scale: strlen*6*scale - scale; 0 for empty/NULL. */
int ui_label_ink_width(const char *label, int scale);

/* 1 when the label's ink plus UI_LABEL_PAD on both sides fits button_w.
 * The single rule: layout and receipt must both call this, never repeat the
 * arithmetic.  NULL/empty label always fits. */
int ui_label_fits(const char *label, int scale, int button_w);

/* Place n items of item_w x item_h left to right, wrapping when the next item
 * would end past x0 + avail_w.  Row gap gap_y, column gap gap_x.  max_cols <= 0
 * means no column cap.  Always at least one item per row, so avail_w < item_w
 * yields one column that overflows (the caller's receipt should flag it).
 *
 * Rows are LEFT-aligned at x0 (matches display_page's grid, where columns
 * start at CONTENT_LEFT) unless centre_rows is nonzero, which centres each
 * row's block inside [x0, x0+avail_w).
 *
 * Writes out[0..n-1] and returns the rows used (0 when n <= 0). */
int ui_flow_place(int n, int item_w, int item_h, int gap_x, int gap_y,
                  int x0, int y0, int avail_w, int max_cols,
                  int centre_rows, UiRect *out);

#endif
