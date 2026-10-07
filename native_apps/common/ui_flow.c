#include <string.h>
#include "ui_flow.h"

int ui_label_ink_width(const char *label, int scale) {
    int n;
    if (!label || !label[0]) return 0;
    n = (int)strlen(label);
    return n * 6 * scale - scale;
}

int ui_label_fits(const char *label, int scale, int button_w) {
    if (!label || !label[0]) return 1;
    return ui_label_ink_width(label, scale) + 2 * UI_LABEL_PAD <= button_w;
}

int ui_flow_place(int n, int item_w, int item_h, int gap_x, int gap_y,
                  int x0, int y0, int avail_w, int max_cols,
                  int centre_rows, UiRect *out) {
    int cols = 1, i, rows, start, k, count, row_w, off;
    if (n <= 0 || !out) return 0;
    /* columns that fit: 1 + as many (item_w+gap_x) steps as remain; no divide */
    while ((cols + 1) * item_w + cols * gap_x <= avail_w &&
           (max_cols <= 0 || cols < max_cols))
        cols++;
    rows = 0;
    for (start = 0; start < n; start += cols) {
        count = n - start < cols ? n - start : cols;
        row_w = count * item_w + (count - 1) * gap_x;
        off = 0;
        if (centre_rows && avail_w > row_w) off = (avail_w - row_w) / 2;
        for (k = 0; k < count; k++) {
            i = start + k;
            out[i].x = x0 + off + k * (item_w + gap_x);
            out[i].y = y0 + rows * (item_h + gap_y);
            out[i].w = item_w;
            out[i].h = item_h;
        }
        rows++;
    }
    return rows;
}
