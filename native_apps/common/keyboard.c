/*
 * Generic on-screen keyboard module for RoomWizard native apps.
 * Uses the 32bpp native_apps drawing API (framebuffer.h / common.h).
 *
 * See keyboard.h for the public API.
 */

#include "keyboard.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ── Layout definitions ──────────────────────────────────────────────────── */

/* KB_LAYOUT_ALPHA: 9 cols × 3 rows (matches original highscore keyboard) */
static const char *alpha_keys[] = {
    "ABCDEFGHI",
    "JKLMNOPQR",
    "STUVWXYZ_",   /* _ displayed as _ on key, stored as space */
    NULL
};

/* KB_LAYOUT_ALPHANUM: 10 cols × 4 rows */
static const char *alphanum_keys[] = {
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ.-_ ",
    "0123456789",
    NULL
};

/* KB_LAYOUT_FULL: 10 cols × 4 rows, upper case */
static const char *full_upper_keys[] = {
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ.-_!",
    "0123456789",
    NULL
};

/* KB_LAYOUT_FULL: 10 cols × 4 rows, lower case */
static const char *full_lower_keys[] = {
    "abcdefghij",
    "klmnopqrst",
    "uvwxyz.-_!",
    "0123456789",
    NULL
};

/* KB_LAYOUT_NUMERIC: 4 cols × 4 rows (space = empty slot, not rendered) */
static const char *numeric_keys[] = {
    "123.",
    "456:",
    "789 ",
    " 0  ",
    NULL
};

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Count rows in a NULL-terminated key array */
static int kb_row_count(const char **keys) {
    int n = 0;
    while (keys[n]) n++;
    return n;
}

/* Column count = length of first row string */
static int kb_col_count(const char **keys) {
    return keys[0] ? (int)strlen(keys[0]) : 0;
}

/* Wait for finger lift + 300 ms settle (matches hs_drain_touches pattern) */
static void kb_drain_touches(TouchInput *touch) {
    uint32_t start = get_time_ms();
    while (get_time_ms() - start < 2000) {
        touch_poll(touch);
        TouchState s = touch_get_state(touch);
        if (!s.held) break;
        usleep(10000);
    }
    start = get_time_ms();
    while (get_time_ms() - start < 300) {
        touch_poll(touch);
        usleep(10000);
    }
}

/* ── Main entry point ────────────────────────────────────────────────────── */

int keyboard_enter(Framebuffer *fb, TouchInput *touch, const char *title,
                   char *buf, int max_len, KeyboardLayout layout) {

    /* ── Select key layout ───────────────────────────────────────────── */
    const char **keys      = alpha_keys;
    const char **alt_keys  = NULL;          /* for FULL shift toggle */
    bool         shifted   = true;          /* FULL starts upper */
    bool         is_alpha  = false;         /* ALPHA _ → space mapping */

    switch (layout) {
    case KB_LAYOUT_ALPHA:
        keys     = alpha_keys;
        is_alpha = true;
        break;
    case KB_LAYOUT_ALPHANUM:
        keys = alphanum_keys;
        break;
    case KB_LAYOUT_FULL:
        keys     = full_upper_keys;
        alt_keys = full_lower_keys;
        break;
    case KB_LAYOUT_NUMERIC:
        keys = numeric_keys;
        break;
    }

    int rows = kb_row_count(keys);
    int cols = kb_col_count(keys);

    /* ── Working buffer ──────────────────────────────────────────────── */
    char work[max_len + 1];
    strncpy(work, buf, max_len);
    work[max_len] = '\0';
    int cursor = (int)strlen(work);

    /* ── Geometry ────────────────────────────────────────────────────── */
    int safe_l = SCREEN_SAFE_LEFT;
    int safe_w = SCREEN_SAFE_WIDTH;

    int btn_w  = safe_w / cols;
    int btn_h  = 52;
    int kb_y   = 120;                          /* top of key grid */
    int action_y = kb_y + rows * btn_h + 10;   /* action button row */

    /* Clamp action_y so it doesn't run off-screen */
    if (action_y > 390) action_y = 390;

    /* ── Build letter buttons ────────────────────────────────────────── */
    Button letter_btns[rows][cols];
    for (int r = 0; r < rows; r++) {
        const char *row_str = keys[r];
        for (int c = 0; c < cols; c++) {
            char ch = row_str[c];
            char label[2] = { ch, '\0' };

            /* For NUMERIC layout, spaces are empty slots — skip init */
            if (layout == KB_LAYOUT_NUMERIC && ch == ' ') {
                memset(&letter_btns[r][c], 0, sizeof(Button));
                continue;
            }

            button_init_full(&letter_btns[r][c],
                             safe_l + c * btn_w, kb_y + r * btn_h,
                             btn_w - 3, btn_h - 3,
                             label,
                             RGB(30, 30, 70), COLOR_WHITE, RGB(80, 80, 200),
                             2);
        }
    }

    /* ── Action buttons ──────────────────────────────────────────────── */
    bool has_shift = (layout == KB_LAYOUT_FULL);
    int n_action = has_shift ? 5 : 4;          /* DEL CLEAR [SHIFT] CANCEL OK */
    int aw = safe_w / n_action;

    const char *del_label = (layout == KB_LAYOUT_NUMERIC) ? "<-" : "DEL";

    Button btn_del, btn_clear, btn_shift, btn_cancel, btn_ok;
    int col_idx = 0;

    button_init_full(&btn_del, safe_l + col_idx * aw, action_y, aw - 4, btn_h,
                     del_label,
                     RGB(80, 40, 0), COLOR_WHITE, RGB(200, 100, 0), 2);
    col_idx++;

    button_init_full(&btn_clear, safe_l + col_idx * aw, action_y, aw - 4, btn_h,
                     "CLEAR",
                     RGB(60, 0, 0), COLOR_WHITE, RGB(200, 0, 0), 2);
    col_idx++;

    if (has_shift) {
        button_init_full(&btn_shift, safe_l + col_idx * aw, action_y, aw - 4, btn_h,
                         "SHIFT",
                         RGB(30, 30, 80), COLOR_WHITE, RGB(100, 100, 220), 2);
        col_idx++;
    }

    button_init_full(&btn_cancel, safe_l + col_idx * aw, action_y, aw - 4, btn_h,
                     "CANCEL",
                     RGB(80, 0, 0), COLOR_WHITE, RGB(200, 0, 0), 2);
    col_idx++;

    button_init_full(&btn_ok, safe_l + col_idx * aw, action_y, aw - 4, btn_h,
                     "OK",
                     RGB(0, 70, 0), COLOR_WHITE, RGB(0, 180, 0), 2);

    /* ── Drain stale touches ─────────────────────────────────────────── */
    kb_drain_touches(touch);

    /* ── Main loop ───────────────────────────────────────────────────── */
    uint32_t last_press = 0;

    for (;;) {
        /* ── Draw ────────────────────────────────────────────────────── */
        fb_clear(fb, COLOR_BLACK);

        /* Title */
        int tw = text_measure_width(title, 2);
        fb_draw_text(fb, fb->width / 2 - tw / 2, 10, title, COLOR_YELLOW, 2);

        /* Input field box */
        int box_x = safe_l + 20;
        int box_w = safe_w - 40;
        fb_fill_rect(fb, box_x, 50, box_w, 52, RGB(20, 20, 20));
        fb_draw_rect(fb, box_x, 50, box_w, 52, COLOR_CYAN);

        /* Current text with underscore cursor */
        char display[max_len + 2];
        strncpy(display, work, max_len);
        display[cursor] = '\0';           /* safety — cursor tracks length */
        int dlen = (int)strlen(display);
        if (dlen < max_len) {
            display[dlen]     = '_';
            display[dlen + 1] = '\0';
        }
        int nw = text_measure_width(display, 3);
        fb_draw_text(fb, box_x + (box_w - nw) / 2, 60, display, COLOR_CYAN, 3);

        /* Hint for ALPHA layout */
        if (is_alpha) {
            fb_draw_text(fb, safe_l, kb_y - 18, "TAP _ FOR SPACE",
                         RGB(80, 80, 80), 1);
        }

        /* Key grid */
        const char **cur_keys = keys;
        if (has_shift && !shifted)
            cur_keys = alt_keys;

        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                char ch = cur_keys[r][c];
                if (layout == KB_LAYOUT_NUMERIC && ch == ' ')
                    continue;   /* empty slot */

                /* Update label to reflect current shift state */
                char label[2] = { ch, '\0' };
                strncpy(letter_btns[r][c].text, label, sizeof(letter_btns[r][c].text) - 1);

                button_draw(fb, &letter_btns[r][c]);
            }
        }

        /* Action buttons */
        button_draw(fb, &btn_del);
        button_draw(fb, &btn_clear);
        if (has_shift)
            button_draw(fb, &btn_shift);
        button_draw(fb, &btn_cancel);
        button_draw(fb, &btn_ok);

        fb_swap(fb);

        /* ── Input ───────────────────────────────────────────────────── */
        touch_poll(touch);
        TouchState state = touch_get_state(touch);
        uint32_t   now   = get_time_ms();

        if (state.pressed && (now - last_press) > 180) {
            bool handled = false;

            /* Check letter/symbol keys */
            for (int r = 0; r < rows && !handled; r++) {
                for (int c = 0; c < cols && !handled; c++) {
                    char ch = cur_keys[r][c];
                    if (layout == KB_LAYOUT_NUMERIC && ch == ' ')
                        continue;

                    if (button_is_touched(&letter_btns[r][c],
                                          state.x, state.y)) {
                        if (cursor < max_len) {
                            /* ALPHA layout: _ key stores space */
                            char store = ch;
                            if (is_alpha && ch == '_')
                                store = ' ';
                            work[cursor++] = store;
                            work[cursor]   = '\0';
                        }
                        last_press = now;
                        handled = true;
                    }
                }
            }

            /* Action buttons */
            if (!handled) {
                if (button_is_touched(&btn_del, state.x, state.y)) {
                    if (cursor > 0) work[--cursor] = '\0';
                    last_press = now;
                } else if (button_is_touched(&btn_clear, state.x, state.y)) {
                    cursor = 0;
                    memset(work, 0, max_len + 1);
                    last_press = now;
                } else if (has_shift &&
                           button_is_touched(&btn_shift, state.x, state.y)) {
                    shifted = !shifted;
                    /* Swap key pointer for next frame redraw */
                    keys = shifted ? full_upper_keys : full_lower_keys;
                    last_press = now;
                } else if (button_is_touched(&btn_cancel, state.x, state.y)) {
                    return KB_RESULT_CANCEL;   /* buf unchanged */
                } else if (button_is_touched(&btn_ok, state.x, state.y)) {
                    /* Trim trailing spaces */
                    int l = (int)strlen(work);
                    while (l > 0 && work[l - 1] == ' ') work[--l] = '\0';

                    strncpy(buf, work, max_len);
                    buf[max_len] = '\0';
                    return KB_RESULT_OK;
                }
            }
        }

        usleep(16000);   /* ~60 fps */
    }
}
