/*
 * High Score System — implementation.
 * See highscore.h for API documentation.
 */

#include "highscore.h"
#include "keyboard.h"
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

/* ── Touch drain (public — used by gameover_update) ──────────────────────── */

void hs_drain_touches(TouchInput *touch) {
    /* Phase 1: Wait for the finger to physically lift (held → false).
     * Cap at 2 s in case of hardware fault. */
    uint32_t start = get_time_ms();
    while (get_time_ms() - start < 2000) {
        touch_poll(touch);
        TouchState s = touch_get_state(touch);
        if (!s.held) break;
        usleep(10000);   /* 10 ms */
    }

    /* Phase 2: 300 ms settling — absorb any bounce/drag events that follow
     * the physical lift on a resistive screen. */
    start = get_time_ms();
    while (get_time_ms() - start < 300) {
        touch_poll(touch);
        usleep(10000);   /* 10 ms */
    }
}

/* ── Name entry (now delegates to generic keyboard module) ───────────────── */

void hs_enter_name(Framebuffer *fb, TouchInput *touch,
                   char *name_buf, int score) {
    char title[64];
    snprintf(title, sizeof(title), "NEW HIGH SCORE: %d", score);

    char buf[HS_NAME_LEN];
    memset(buf, 0, sizeof(buf));

    int result = keyboard_enter(fb, touch, title, buf, HS_NAME_LEN - 1,
                                KB_LAYOUT_ALPHA);

    if (result == KB_RESULT_OK && buf[0] != '\0') {
        strncpy(name_buf, buf, HS_NAME_LEN - 1);
        name_buf[HS_NAME_LEN - 1] = '\0';
    } else {
        strncpy(name_buf, "PLAYER", HS_NAME_LEN - 1);
        name_buf[HS_NAME_LEN - 1] = '\0';
    }
}
