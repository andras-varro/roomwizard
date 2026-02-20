/*
 * High Score System — implementation.
 * See highscore.h for API documentation.
 */

#include "highscore.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Internal helpers ───────────────────────────────────────────────────── */

static void hs_filepath(const HighScoreTable *t, char *buf, size_t n) {
    snprintf(buf, n, "%s/%s.hig", HS_DATA_DIR, t->game_name);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void hs_init(HighScoreTable *t, const char *game_name) {
    memset(t, 0, sizeof(*t));
    strncpy(t->game_name, game_name, sizeof(t->game_name) - 1);
}

void hs_load(HighScoreTable *t) {
    char path[128];
    hs_filepath(t, path, sizeof(path));
    t->count = 0;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char stored_name[HS_NAME_LEN + 2];
    int  score;
    while (t->count < HS_MAX_ENTRIES &&
           fscanf(f, "%11s %d\n", stored_name, &score) == 2) {
        strncpy(t->entries[t->count].name, stored_name, HS_NAME_LEN - 1);
        t->entries[t->count].name[HS_NAME_LEN - 1] = '\0';
        /* Underscores → spaces for display */
        for (int j = 0; t->entries[t->count].name[j]; j++)
            if (t->entries[t->count].name[j] == '_')
                t->entries[t->count].name[j] = ' ';
        t->entries[t->count].score = score;
        t->count++;
    }
    fclose(f);
}

void hs_save(const HighScoreTable *t) {
    mkdir(HS_DATA_DIR, 0755);   /* ensure dir exists; ok if already there */
    char path[128];
    hs_filepath(t, path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f) return;

    for (int i = 0; i < t->count; i++) {
        /* Spaces → underscores for file storage (fscanf token safety) */
        char safe[HS_NAME_LEN];
        strncpy(safe, t->entries[i].name, HS_NAME_LEN - 1);
        safe[HS_NAME_LEN - 1] = '\0';
        for (int j = 0; safe[j]; j++)
            if (safe[j] == ' ') safe[j] = '_';
        fprintf(f, "%s %d\n", safe, t->entries[i].score);
    }
    fclose(f);
}

void hs_reset(HighScoreTable *t) {
    t->count = 0;
    hs_save(t);
}

/* ── Logic ───────────────────────────────────────────────────────────────── */

int hs_qualifies(const HighScoreTable *t, int score) {
    if (score <= 0) return -1;
    for (int i = 0; i < t->count; i++) {
        if (score > t->entries[i].score) return i;
    }
    if (t->count < HS_MAX_ENTRIES) return t->count;  /* append slot */
    return -1;
}

void hs_insert(HighScoreTable *t, const char *name, int score) {
    int rank = hs_qualifies(t, score);
    if (rank < 0) return;

    int last = (t->count < HS_MAX_ENTRIES) ? t->count : HS_MAX_ENTRIES - 1;
    for (int i = last; i > rank; i--)
        t->entries[i] = t->entries[i - 1];

    strncpy(t->entries[rank].name, name, HS_NAME_LEN - 1);
    t->entries[rank].name[HS_NAME_LEN - 1] = '\0';
    t->entries[rank].score = score;
    if (t->count < HS_MAX_ENTRIES) t->count++;
}

/* ── Rendering ───────────────────────────────────────────────────────────── */

int hs_draw(Framebuffer *fb, const HighScoreTable *t, int x, int y, int w) {
    const int row_h  = 34;
    int start_y = y;

    /* Title */
    int tw = text_measure_width("HIGH SCORES", 2);
    fb_draw_text(fb, x + (w - tw) / 2, y, "HIGH SCORES", COLOR_YELLOW, 2);
    y += 26;

    /* Divider */
    fb_fill_rect(fb, x, y, w, 2, COLOR_YELLOW);
    y += 8;

    if (t->count == 0) {
        int ew = text_measure_width("NO SCORES YET", 2);
        fb_draw_text(fb, x + (w - ew) / 2, y + 10, "NO SCORES YET",
                     RGB(100, 100, 100), 2);
        return (y + 40) - start_y;
    }

    for (int i = 0; i < t->count; i++) {
        uint32_t col;
        switch (i) {
            case 0:  col = COLOR_YELLOW;         break;  /* Gold   */
            case 1:  col = RGB(192, 192, 192);   break;  /* Silver */
            case 2:  col = RGB(180, 120,  60);   break;  /* Bronze */
            default: col = COLOR_WHITE;          break;
        }

        /* Rank number */
        char rank_s[4];
        snprintf(rank_s, sizeof(rank_s), "%d.", i + 1);
        fb_draw_text(fb, x + 4, y + i * row_h, rank_s, col, 2);

        /* Name */
        fb_draw_text(fb, x + 36, y + i * row_h,
                     t->entries[i].name, col, 2);

        /* Score — right-aligned */
        char score_s[16];
        snprintf(score_s, sizeof(score_s), "%d", t->entries[i].score);
        int sw = text_measure_width(score_s, 2);
        fb_draw_text(fb, x + w - sw - 4, y + i * row_h, score_s, col, 2);
    }

    return (y + t->count * row_h) - start_y;
}

/* ── Name entry ──────────────────────────────────────────────────────────── */

#define KB_COLS 9
#define KB_ROWS 3

static const char kb_letters[KB_ROWS][KB_COLS] = {
    { 'A','B','C','D','E','F','G','H','I' },
    { 'J','K','L','M','N','O','P','Q','R' },
    { 'S','T','U','V','W','X','Y','Z',' ' },
};

void hs_drain_touches(TouchInput *touch) {
    /* Poll and discard for 200 ms to flush the residual tap from name entry */
    uint32_t start = get_time_ms();
    while (get_time_ms() - start < 200) {
        touch_poll(touch);
        usleep(10000);
    }
}

void hs_enter_name(Framebuffer *fb, TouchInput *touch,
                   char *name_buf, int score) {
    memset(name_buf, 0, HS_NAME_LEN);
    int  name_len = 0;
    bool confirmed = false;

    /* ── Layout ──────────────────────────────────────────────────────────── */
    /* Keyboard occupies the lower portion of the screen */
    int safe_l = SCREEN_SAFE_LEFT;
    int safe_w = SCREEN_SAFE_WIDTH;            /* 720 px */
    int btn_w  = safe_w / KB_COLS;            /* 80 px  */
    int btn_h  = 52;
    int kb_y   = 175;   /* letter row 0 starts here; header above */

    /* Letter buttons */
    Button letter_btns[KB_ROWS][KB_COLS];
    for (int r = 0; r < KB_ROWS; r++) {
        for (int c = 0; c < KB_COLS; c++) {
            char label[3];
            label[0] = (kb_letters[r][c] == ' ') ? '_' : kb_letters[r][c];
            label[1] = '\0';
            button_init_full(&letter_btns[r][c],
                             safe_l + c * btn_w, kb_y + r * btn_h,
                             btn_w - 3, btn_h - 3,
                             label,
                             RGB(30, 30, 70), COLOR_WHITE, RGB(80, 80, 200),
                             2);
        }
    }

    /* Action row: DEL | CLEAR | OK */
    int action_y = kb_y + KB_ROWS * btn_h + 5;
    int aw = safe_w / 3;
    Button btn_del, btn_clear, btn_ok;
    button_init_full(&btn_del,   safe_l,         action_y, aw - 4, btn_h,
                     "DEL",   RGB(80, 40, 0),  COLOR_WHITE, RGB(200, 100, 0), 2);
    button_init_full(&btn_clear, safe_l + aw,    action_y, aw - 4, btn_h,
                     "CLEAR", RGB(60, 0,  0),  COLOR_WHITE, RGB(200,   0, 0), 2);
    button_init_full(&btn_ok,    safe_l + 2*aw,  action_y, aw - 4, btn_h,
                     "OK",    RGB(0,  70, 0),  COLOR_WHITE, RGB(0,   180, 0), 2);

    uint32_t last_press = 0;

    while (!confirmed) {
        /* ── Draw ──────────────────────────────────────────────────────── */
        fb_clear(fb, COLOR_BLACK);

        /* Header — score context */
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "NEW HIGH SCORE: %d", score);
        int hw = text_measure_width(hdr, 2);
        fb_draw_text(fb, fb->width / 2 - hw / 2, 38, hdr, COLOR_YELLOW, 2);

        int pw = text_measure_width("ENTER YOUR NAME:", 2);
        fb_draw_text(fb, fb->width / 2 - pw / 2, 68,
                     "ENTER YOUR NAME:", COLOR_WHITE, 2);

        /* Name input box */
        int box_x = SCREEN_SAFE_LEFT + 20;
        int box_w = SCREEN_SAFE_WIDTH - 40;
        fb_fill_rect(fb, box_x, 98, box_w, 52, RGB(20, 20, 20));
        fb_draw_rect(fb, box_x, 98, box_w, 52, COLOR_CYAN);

        /* Current name with underscore cursor */
        char display[HS_NAME_LEN + 2];
        strncpy(display, name_buf, HS_NAME_LEN);
        int dlen = (int)strlen(display);
        if (dlen < HS_NAME_LEN - 1) { display[dlen] = '_'; display[dlen+1] = '\0'; }
        int nw = text_measure_width(display, 3);
        fb_draw_text(fb, box_x + (box_w - nw) / 2, 108, display, COLOR_CYAN, 3);

        /* Hint */
        fb_draw_text(fb, safe_l, kb_y - 18, "TAP _ FOR SPACE",
                     RGB(80, 80, 80), 1);

        /* Keyboard */
        for (int r = 0; r < KB_ROWS; r++)
            for (int c = 0; c < KB_COLS; c++)
                button_draw(fb, &letter_btns[r][c]);
        button_draw(fb, &btn_del);
        button_draw(fb, &btn_clear);
        button_draw(fb, &btn_ok);

        fb_swap(fb);

        /* ── Input ─────────────────────────────────────────────────────── */
        touch_poll(touch);
        TouchState state = touch_get_state(touch);
        uint32_t   now   = get_time_ms();

        if (state.pressed && (now - last_press) > 180) {
            bool handled = false;

            /* Letter / space keys */
            for (int r = 0; r < KB_ROWS && !handled; r++) {
                for (int c = 0; c < KB_COLS && !handled; c++) {
                    if (button_is_touched(&letter_btns[r][c],
                                         state.x, state.y)) {
                        if (name_len < HS_NAME_LEN - 1) {
                            name_buf[name_len++] = kb_letters[r][c];
                            name_buf[name_len]   = '\0';
                        }
                        last_press = now;
                        handled = true;
                    }
                }
            }

            /* Action buttons */
            if (!handled) {
                if (button_is_touched(&btn_del, state.x, state.y)) {
                    if (name_len > 0) name_buf[--name_len] = '\0';
                    last_press = now;
                } else if (button_is_touched(&btn_clear, state.x, state.y)) {
                    name_len = 0;
                    memset(name_buf, 0, HS_NAME_LEN);
                    last_press = now;
                } else if (button_is_touched(&btn_ok, state.x, state.y)) {
                    if (name_len > 0) confirmed = true;
                    last_press = now;
                }
            }
        }

        usleep(16000);  /* ~60 fps */
    }

    /* Trim trailing spaces; default to "???" if somehow empty */
    int l = (int)strlen(name_buf);
    while (l > 0 && name_buf[l - 1] == ' ') name_buf[--l] = '\0';
    if (l == 0) strncpy(name_buf, "???", HS_NAME_LEN);
}
