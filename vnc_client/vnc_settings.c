/*
 * VNC Settings GUI — touch-driven settings screen
 *
 * Renders directly to the 800×480 RGB565 framebuffer using existing
 * rendering primitives.  Allows viewing and editing VNC connection
 * parameters without SSH access.
 *
 * See SETTINGS_GUI_DESIGN.md for full layout specification.
 *
 * Keypad modes:
 *   NUMERIC — 0-9, dot, colon (for HOST and PORT)
 *   FULL    — letters (with shift), digits, symbols (for PASSWORD)
 *   ALPHA   — letters, digits, space (for ENCODINGS)
 */

#include "vnc_settings.h"
#include "vnc_renderer.h"
#include "config.h"
#include "../native_apps/common/framebuffer.h"
#include "../native_apps/common/touch_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

/* ── Layout constants (from SETTINGS_GUI_DESIGN.md §1) ─────────────── */

#define TITLE_BAR_H      40
#define ROW_COUNT         6
#define ROW_H             48
#define ROW_GAP           6        /* vertical gap between row top edges: ROW_H + ROW_GAP = 54 */
#define FIRST_ROW_Y       56

/* Row internal layout */
#define LABEL_X           20
#define VALUE_FIELD_X     160
#define VALUE_FIELD_W     520
#define VALUE_TEXT_X      168      /* 8px padding inside value field */
#define ROW_BTN_X         700
#define ROW_BTN_W         80

/* +/- buttons for COMPRESS and QUALITY rows */
#define PM_BTN_W          60
#define PM_BTN_H          38

/* Status line */
#define STATUS_Y          392

/* Action buttons */
#define ACT_BTN_Y         424
#define ACT_BTN_H         48
#define ACT_BTN_BACK_X    20
#define ACT_BTN_EXIT_X    300
#define ACT_BTN_SAVE_X    580
#define ACT_BTN_W         200

/* ── Numeric keypad overlay (original, anchored at Y=240) ──────────── */
#define KP_Y              240
#define KP_H              240      /* 480 - 240 */
#define KP_INPUT_X        20
#define KP_INPUT_Y        244
#define KP_INPUT_W        760
#define KP_INPUT_H        36
#define KP_KEY_W          80
#define KP_KEY_H          48
#define KP_MAX_LEN        255

/* ── Full/Alpha keypad overlay (larger, nearly full screen) ────────── */
#define FKP_X             50
#define FKP_Y             40
#define FKP_W             700
#define FKP_H             420

/* Full/Alpha key grid sizing */
#define FKP_COLS          10
#define FKP_KEY_W         66
#define FKP_KEY_H         44
#define FKP_KEY_PAD       4       /* padding between keys */

/* Input field within full keypad overlay */
#define FKP_INPUT_X       (FKP_X + 10)
#define FKP_INPUT_Y       (FKP_Y + 30)
#define FKP_INPUT_W       (FKP_W - 20)
#define FKP_INPUT_H       36

/* First row of letter keys Y position */
#define FKP_KEYS_Y        (FKP_INPUT_Y + FKP_INPUT_H + 10)

/* ── Keypad button geometry for NUMERIC mode ───────────────────────── */

typedef struct {
    const char *label;
    int x, y, w, h;
} KeypadButton;

/* Row 1: digits 1-5 */
/* Row 2: digits 6-0 */
/* Row 3: . : CLEAR */
/* BACKSPACE spans rows 1-2 on the right */
/* Row 4: CANCEL  OK */
static const KeypadButton kp_buttons[] = {
    /* Row 1: digits */
    { "1",    20, 288,  80, 48 },
    { "2",   110, 288,  80, 48 },
    { "3",   200, 288,  80, 48 },
    { "4",   290, 288,  80, 48 },
    { "5",   380, 288,  80, 48 },
    /* Row 2: digits */
    { "6",    20, 342,  80, 48 },
    { "7",   110, 342,  80, 48 },
    { "8",   200, 342,  80, 48 },
    { "9",   290, 342,  80, 48 },
    { "0",   380, 342,  80, 48 },
    /* Row 3: . : CLEAR */
    { ".",    20, 396,  80, 48 },
    { ":",   110, 396,  80, 48 },
    { "CLR", 200, 396, 170, 48 },
    /* BACKSPACE (tall, spanning rows 1-2) */
    { "<-",  520, 288, 140, 102 },
    /* Bottom row: CANCEL, OK */
    { "CANCEL", 200, 450, 160, 28 },
    { "OK",     440, 450, 160, 28 },
};
#define KP_NUM_BUTTONS  (sizeof(kp_buttons) / sizeof(kp_buttons[0]))

/* ── Keypad mode enum ──────────────────────────────────────────────── */

typedef enum {
    KEYPAD_NUMERIC,    /* 0-9, dot, colon — for HOST and PORT */
    KEYPAD_FULL,       /* Letters (with shift), digits, symbols — for PASSWORD */
    KEYPAD_ALPHA,      /* Letters, digits, space — for ENCODINGS */
} KeypadMode;

/* ── State ─────────────────────────────────────────────────────────── */

typedef enum {
    SCREEN_MAIN,
    SCREEN_KEYPAD
} SettingsScreen;

typedef struct {
    VNCConfig       working;        /* Working copy of config */
    VNCConfig      *live;           /* Pointer to the live config */
    Framebuffer    *fb;
    TouchInput     *touch;
    const char     *config_path;
    SettingsScreen  screen;
    int             editing_field;  /* 0=HOST, 1=PORT, 2=PASSWORD, 3=ENCODINGS */
    char            keypad_buf[KP_MAX_LEN + 1];
    int             keypad_cursor;
    int             keypad_max_len; /* max input length for current field */
    KeypadMode      keypad_mode;
    bool            shift_active;   /* For FULL mode: uppercase vs lowercase */
    int             save_error;     /* >0 = show error countdown (frames) */
} SettingsState;

/* Forward declarations */
static void draw_main_screen(SettingsState *st);
static void draw_keypad(SettingsState *st);
static void draw_keypad_full(SettingsState *st);
static int  handle_main_touch(SettingsState *st, int tx, int ty);
static int  handle_keypad_touch(SettingsState *st, int tx, int ty);
static int  handle_keypad_full_touch(SettingsState *st, int tx, int ty);
static int  save_config_file(const VNCConfig *cfg, const char *path);

/* ── Helper: hit test ──────────────────────────────────────────────── */

static bool hit_rect(int tx, int ty, int rx, int ry, int rw, int rh) {
    return tx >= rx && tx < rx + rw && ty >= ry && ty < ry + rh;
}

/* ── Helper: draw a button with border and centered label ──────────── */

static void draw_button(Framebuffer *fb, int bx, int by, int bw, int bh,
                        const char *label, uint16_t bg, uint16_t fg, int scale) {
    vnc_renderer_fill_rect(fb, bx, by, bw, bh, bg);
    /* 1-pixel border */
    vnc_renderer_fill_rect(fb, bx, by, bw, 1, fg);
    vnc_renderer_fill_rect(fb, bx, by + bh - 1, bw, 1, fg);
    vnc_renderer_fill_rect(fb, bx, by, 1, bh, fg);
    vnc_renderer_fill_rect(fb, bx + bw - 1, by, 1, bh, fg);
    /* Centered label */
    int tw = vnc_renderer_text_width(label, scale);
    int lx = bx + (bw - tw) / 2;
    /* Vertical centering: scale 2 = 16px char height, scale 1 = 8px */
    int char_h = 8 * scale;
    int ly = by + (bh - char_h) / 2;
    vnc_renderer_draw_text(fb, lx, ly, label, fg, scale);
}

/* ── Row Y position helper ─────────────────────────────────────────── */

static int row_y(int row) {
    /* Design doc: row 0 at y=56, each row is ROW_H + ROW_GAP = 54px apart */
    return FIRST_ROW_Y + row * (ROW_H + ROW_GAP);
}

/* ── Draw the main settings screen ─────────────────────────────────── */

static void draw_main_screen(SettingsState *st) {
    Framebuffer *fb = st->fb;

    vnc_renderer_clear_screen(fb);

    /* ── Title bar ──────────────────────────────────────────── */
    vnc_renderer_fill_rect(fb, 0, 0, SCREEN_WIDTH, TITLE_BAR_H, RGB565(0, 0, 100));
    {
        const char *title = "VNC SETTINGS";
        int tw = vnc_renderer_text_width(title, 3);
        vnc_renderer_draw_text(fb, (SCREEN_WIDTH - tw) / 2, 8, title, RGB565_WHITE, 3);
    }

    /* ── Settings rows ──────────────────────────────────────── */
    const char *labels[] = { "HOST", "PORT", "PASSWORD", "ENCODINGS", "COMPRESS", "QUALITY" };

    for (int i = 0; i < ROW_COUNT; i++) {
        int ry = row_y(i);

        /* Alternating row background for readability */
        uint16_t row_bg = (i % 2 == 0) ? RGB565(15, 15, 15) : RGB565(25, 25, 25);
        vnc_renderer_fill_rect(fb, 0, ry, SCREEN_WIDTH, ROW_H, row_bg);

        /* Label column */
        vnc_renderer_draw_text(fb, LABEL_X, ry + 16, labels[i], RGB565_WHITE, 2);

        /* Value field background */
        vnc_renderer_fill_rect(fb, VALUE_FIELD_X, ry, VALUE_FIELD_W, ROW_H, RGB565(30, 30, 30));

        char val_str[256];
        switch (i) {
        case 0: /* HOST — editable via keypad */
            snprintf(val_str, sizeof(val_str), "%.42s", st->working.host);
            vnc_renderer_draw_text(fb, VALUE_TEXT_X, ry + 16, val_str, RGB565_YELLOW, 2);
            draw_button(fb, ROW_BTN_X, ry, ROW_BTN_W, ROW_H,
                        "[>]", RGB565(0, 60, 120), RGB565_WHITE, 2);
            break;

        case 1: /* PORT — editable via keypad */
            snprintf(val_str, sizeof(val_str), "%d", st->working.port);
            vnc_renderer_draw_text(fb, VALUE_TEXT_X, ry + 16, val_str, RGB565_YELLOW, 2);
            draw_button(fb, ROW_BTN_X, ry, ROW_BTN_W, ROW_H,
                        "[>]", RGB565(0, 60, 120), RGB565_WHITE, 2);
            break;

        case 2: /* PASSWORD — editable via FULL keypad */
            vnc_renderer_draw_text(fb, VALUE_TEXT_X, ry + 16, "********", RGB565_YELLOW, 2);
            draw_button(fb, ROW_BTN_X, ry, ROW_BTN_W, ROW_H,
                        "[>]", RGB565(0, 60, 120), RGB565_WHITE, 2);
            break;

        case 3: /* ENCODINGS — editable via ALPHA keypad */
            snprintf(val_str, sizeof(val_str), "%.42s", st->working.encodings);
            vnc_renderer_draw_text(fb, VALUE_TEXT_X, ry + 16, val_str, RGB565_YELLOW, 2);
            draw_button(fb, ROW_BTN_X, ry, ROW_BTN_W, ROW_H,
                        "[>]", RGB565(0, 60, 120), RGB565_WHITE, 2);
            break;

        case 4: /* COMPRESS — +/- buttons (1-9) */
            snprintf(val_str, sizeof(val_str), "%d", st->working.compress_level);
            vnc_renderer_draw_text(fb, VALUE_TEXT_X, ry + 16, val_str, RGB565_YELLOW, 2);
            {
                /* [-] button at X=620 */
                int minus_x = 620;
                int plus_x  = 700;
                draw_button(fb, minus_x, ry + 5, PM_BTN_W, PM_BTN_H,
                            "-", RGB565(0, 80, 80), RGB565_WHITE, 2);
                draw_button(fb, plus_x, ry + 5, PM_BTN_W, PM_BTN_H,
                            "+", RGB565(0, 80, 80), RGB565_WHITE, 2);
            }
            break;

        case 5: /* QUALITY — +/- buttons (1-9) */
            snprintf(val_str, sizeof(val_str), "%d", st->working.quality_level);
            vnc_renderer_draw_text(fb, VALUE_TEXT_X, ry + 16, val_str, RGB565_YELLOW, 2);
            {
                int minus_x = 620;
                int plus_x  = 700;
                draw_button(fb, minus_x, ry + 5, PM_BTN_W, PM_BTN_H,
                            "-", RGB565(0, 80, 80), RGB565_WHITE, 2);
                draw_button(fb, plus_x, ry + 5, PM_BTN_W, PM_BTN_H,
                            "+", RGB565(0, 80, 80), RGB565_WHITE, 2);
            }
            break;
        }
    }

    /* ── Status / hint line ──────────────────────────────────── */
    {
        const char *hint = "TOUCH ANY ROW TO EDIT  |  LONG FIELDS: SCROLL RIGHT";
        int tw = vnc_renderer_text_width(hint, 1);
        vnc_renderer_draw_text(fb, (SCREEN_WIDTH - tw) / 2, STATUS_Y, hint,
                               RGB565_GREY, 1);
    }

    /* Show save error if any */
    if (st->save_error > 0) {
        const char *err = "SAVE FAILED — config file not writable";
        int tw = vnc_renderer_text_width(err, 2);
        vnc_renderer_draw_text(fb, (SCREEN_WIDTH - tw) / 2, STATUS_Y - 20, err,
                               RGB565_RED, 2);
    }

    /* ── Action buttons ──────────────────────────────────────── */
    draw_button(fb, ACT_BTN_BACK_X, ACT_BTN_Y, ACT_BTN_W, ACT_BTN_H,
                "BACK", RGB565(60, 60, 60), RGB565_WHITE, 2);

    draw_button(fb, ACT_BTN_EXIT_X, ACT_BTN_Y, ACT_BTN_W, ACT_BTN_H,
                "EXIT", RGB565(160, 0, 0), RGB565_RED, 2);

    draw_button(fb, ACT_BTN_SAVE_X, ACT_BTN_Y, ACT_BTN_W, ACT_BTN_H,
                "SAVE & RECONNECT", RGB565(0, 100, 0), RGB565_GREEN, 2);

    fb_swap(fb);
}

/* ── Handle touch on main screen ───────────────────────────────────── */
/*
 * Returns:
 *   -1              no action (continue loop)
 *   SETTINGS_BACK   user pressed Back
 *   SETTINGS_EXIT   user pressed Exit
 *   SETTINGS_SAVE   user pressed Save & Reconnect
 *   99              switch to keypad screen
 */
static int handle_main_touch(SettingsState *st, int tx, int ty) {
    /* ── Action buttons ────────────────────────────────────── */
    if (hit_rect(tx, ty, ACT_BTN_BACK_X, ACT_BTN_Y, ACT_BTN_W, ACT_BTN_H))
        return SETTINGS_BACK;
    if (hit_rect(tx, ty, ACT_BTN_EXIT_X, ACT_BTN_Y, ACT_BTN_W, ACT_BTN_H))
        return SETTINGS_EXIT;
    if (hit_rect(tx, ty, ACT_BTN_SAVE_X, ACT_BTN_Y, ACT_BTN_W, ACT_BTN_H))
        return SETTINGS_SAVE;

    /* ── Row touches ───────────────────────────────────────── */
    for (int i = 0; i < ROW_COUNT; i++) {
        int ry = row_y(i);
        if (ty < ry || ty >= ry + ROW_H)
            continue;

        switch (i) {
        case 0: /* HOST — open NUMERIC keypad */
            st->editing_field = 0;
            st->keypad_mode = KEYPAD_NUMERIC;
            st->keypad_max_len = (int)(sizeof(st->working.host) - 1);
            strncpy(st->keypad_buf, st->working.host, KP_MAX_LEN);
            st->keypad_buf[KP_MAX_LEN] = '\0';
            st->keypad_cursor = (int)strlen(st->keypad_buf);
            st->shift_active = false;
            st->screen = SCREEN_KEYPAD;
            return 99;

        case 1: /* PORT — open NUMERIC keypad */
            st->editing_field = 1;
            st->keypad_mode = KEYPAD_NUMERIC;
            st->keypad_max_len = 5; /* max 65535 */
            snprintf(st->keypad_buf, KP_MAX_LEN + 1, "%d", st->working.port);
            st->keypad_cursor = (int)strlen(st->keypad_buf);
            st->shift_active = false;
            st->screen = SCREEN_KEYPAD;
            return 99;

        case 2: /* PASSWORD — open FULL keypad */
            st->editing_field = 2;
            st->keypad_mode = KEYPAD_FULL;
            st->keypad_max_len = 8; /* VncAuth limit */
            strncpy(st->keypad_buf, st->working.password, KP_MAX_LEN);
            st->keypad_buf[KP_MAX_LEN] = '\0';
            st->keypad_cursor = (int)strlen(st->keypad_buf);
            st->shift_active = false;
            st->screen = SCREEN_KEYPAD;
            return 99;

        case 3: /* ENCODINGS — open ALPHA keypad */
            st->editing_field = 3;
            st->keypad_mode = KEYPAD_ALPHA;
            st->keypad_max_len = (int)(sizeof(st->working.encodings) - 1);
            strncpy(st->keypad_buf, st->working.encodings, KP_MAX_LEN);
            st->keypad_buf[KP_MAX_LEN] = '\0';
            st->keypad_cursor = (int)strlen(st->keypad_buf);
            st->shift_active = false;
            st->screen = SCREEN_KEYPAD;
            return 99;

        case 4: /* COMPRESS +/- */
            {
                int minus_x = 620;
                int plus_x  = 700;
                if (hit_rect(tx, ty, minus_x, ry + 5, PM_BTN_W, PM_BTN_H)) {
                    if (st->working.compress_level > 1)
                        st->working.compress_level--;
                }
                if (hit_rect(tx, ty, plus_x, ry + 5, PM_BTN_W, PM_BTN_H)) {
                    if (st->working.compress_level < 9)
                        st->working.compress_level++;
                }
            }
            break;

        case 5: /* QUALITY +/- */
            {
                int minus_x = 620;
                int plus_x  = 700;
                if (hit_rect(tx, ty, minus_x, ry + 5, PM_BTN_W, PM_BTN_H)) {
                    if (st->working.quality_level > 1)
                        st->working.quality_level--;
                }
                if (hit_rect(tx, ty, plus_x, ry + 5, PM_BTN_W, PM_BTN_H)) {
                    if (st->working.quality_level < 9)
                        st->working.quality_level++;
                }
            }
            break;

        default:
            break;
        }
        break;  /* only one row can match */
    }

    return -1;
}

/* ── Draw NUMERIC keypad overlay (original layout) ─────────────────── */

static void draw_keypad(SettingsState *st) {
    Framebuffer *fb = st->fb;

    /* Dim the entire screen as overlay background */
    vnc_renderer_fill_rect(fb, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGB565(10, 10, 10));

    /* Keypad panel background */
    vnc_renderer_fill_rect(fb, 0, KP_Y, SCREEN_WIDTH, KP_H, RGB565(20, 20, 20));
    /* Top border line */
    vnc_renderer_fill_rect(fb, 0, KP_Y, SCREEN_WIDTH, 2, RGB565_WHITE);

    /* Title above input field */
    const char *title = (st->editing_field == 0) ? "EDIT HOST" : "EDIT PORT";
    vnc_renderer_draw_text(fb, KP_INPUT_X, KP_Y + 1, title, RGB565_YELLOW, 1);

    /* Input field */
    vnc_renderer_fill_rect(fb, KP_INPUT_X, KP_INPUT_Y, KP_INPUT_W, KP_INPUT_H,
                           RGB565(0, 0, 40));
    /* Border */
    vnc_renderer_fill_rect(fb, KP_INPUT_X, KP_INPUT_Y, KP_INPUT_W, 1, RGB565_GREEN);
    vnc_renderer_fill_rect(fb, KP_INPUT_X, KP_INPUT_Y + KP_INPUT_H - 1, KP_INPUT_W, 1,
                           RGB565_GREEN);
    vnc_renderer_fill_rect(fb, KP_INPUT_X, KP_INPUT_Y, 1, KP_INPUT_H, RGB565_GREEN);
    vnc_renderer_fill_rect(fb, KP_INPUT_X + KP_INPUT_W - 1, KP_INPUT_Y, 1, KP_INPUT_H,
                           RGB565_GREEN);

    /* Input text */
    vnc_renderer_draw_text(fb, KP_INPUT_X + 8, KP_INPUT_Y + 10,
                           st->keypad_buf, RGB565_GREEN, 2);

    /* Blinking cursor (always shown — no real blink needed) */
    {
        int cx = KP_INPUT_X + 8 + vnc_renderer_text_width(st->keypad_buf, 2);
        vnc_renderer_fill_rect(fb, cx, KP_INPUT_Y + 8, 2, 20, RGB565_GREEN);
    }

    /* ── Draw keypad buttons ────────────────────────────────── */
    for (int i = 0; i < (int)KP_NUM_BUTTONS; i++) {
        const KeypadButton *btn = &kp_buttons[i];

        /* Choose colors per button type */
        uint16_t bg, fg;
        if (strcmp(btn->label, "OK") == 0) {
            bg = RGB565(0, 100, 0);
            fg = RGB565_GREEN;
        } else if (strcmp(btn->label, "CANCEL") == 0) {
            bg = RGB565(160, 0, 0);
            fg = RGB565_RED;
        } else if (strcmp(btn->label, "<-") == 0 || strcmp(btn->label, "CLR") == 0) {
            bg = RGB565(80, 60, 0);
            fg = RGB565_YELLOW;
        } else {
            bg = RGB565(50, 50, 50);
            fg = RGB565_WHITE;
        }

        draw_button(fb, btn->x, btn->y, btn->w, btn->h, btn->label, bg, fg, 2);
    }

    fb_swap(fb);
}

/* ── Handle touch on NUMERIC keypad ────────────────────────────────── */
/*
 * Returns:
 *   -1  no action (key pressed, or miss — keep keypad open)
 *    0  CANCEL — discard, return to main screen
 *    1  OK — commit value, return to main screen
 */
static int handle_keypad_touch(SettingsState *st, int tx, int ty) {
    for (int i = 0; i < (int)KP_NUM_BUTTONS; i++) {
        const KeypadButton *btn = &kp_buttons[i];
        if (!hit_rect(tx, ty, btn->x, btn->y, btn->w, btn->h))
            continue;

        const char *k = btn->label;

        /* OK — commit edit */
        if (strcmp(k, "OK") == 0) {
            if (st->editing_field == 0) {
                /* HOST: reject empty */
                if (st->keypad_buf[0] != '\0') {
                    strncpy(st->working.host, st->keypad_buf,
                            sizeof(st->working.host) - 1);
                    st->working.host[sizeof(st->working.host) - 1] = '\0';
                }
            } else {
                /* PORT: clamp to 1-65535 */
                int p = atoi(st->keypad_buf);
                if (p < 1) p = 1;
                if (p > 65535) p = 65535;
                st->working.port = p;
            }
            st->screen = SCREEN_MAIN;
            return 1;
        }

        /* CANCEL — discard */
        if (strcmp(k, "CANCEL") == 0) {
            st->screen = SCREEN_MAIN;
            return 0;
        }

        /* BACKSPACE */
        if (strcmp(k, "<-") == 0) {
            if (st->keypad_cursor > 0)
                st->keypad_buf[--st->keypad_cursor] = '\0';
            return -1;
        }

        /* CLEAR */
        if (strcmp(k, "CLR") == 0) {
            st->keypad_buf[0] = '\0';
            st->keypad_cursor = 0;
            return -1;
        }

        /* Digit or punctuation (single char labels: 0-9, '.', ':') */
        if (strlen(k) == 1) {
            char ch = k[0];

            /* PORT field: only allow digits */
            if (st->editing_field == 1 && !isdigit((unsigned char)ch))
                return -1;

            /* Buffer overflow protection */
            if (st->keypad_cursor >= st->keypad_max_len)
                return -1;

            st->keypad_buf[st->keypad_cursor++] = ch;
            st->keypad_buf[st->keypad_cursor] = '\0';
            return -1;
        }

        return -1;
    }

    return -1;  /* touch missed all buttons */
}

/* ── FULL / ALPHA keypad: letter grid definitions ──────────────────── */

/* Letters A-Z laid out in 10-column rows */
static const char full_letters_upper[26] = {
    'A','B','C','D','E','F','G','H','I','J',
    'K','L','M','N','O','P','Q','R','S','T',
    'U','V','W','X','Y','Z'
};
static const char full_letters_lower[26] = {
    'a','b','c','d','e','f','g','h','i','j',
    'k','l','m','n','o','p','q','r','s','t',
    'u','v','w','x','y','z'
};

/* FULL mode row 3 symbols (positions 26-29): .  -  _  ! */
static const char full_symbols[4] = { '.', '-', '_', '!' };

/* FULL mode row 4 digits (positions 30-39): 0-9 */
static const char full_digits[10] = { '0','1','2','3','4','5','6','7','8','9' };

/* ALPHA mode row 3 symbols (positions 26-29): _  -  .  (space) */
static const char alpha_symbols[4] = { '_', '-', '.', ' ' };

/* ── Helper: get key grid position for full/alpha modes ────────────── */

static void fkp_key_pos(int index, int *kx, int *ky) {
    int col = index % FKP_COLS;
    int row = index / FKP_COLS;
    *kx = FKP_X + 10 + col * (FKP_KEY_W + FKP_KEY_PAD);
    *ky = FKP_KEYS_Y + row * (FKP_KEY_H + FKP_KEY_PAD);
}

/* ── Draw FULL/ALPHA keypad overlay ────────────────────────────────── */

static void draw_keypad_full(SettingsState *st) {
    Framebuffer *fb = st->fb;
    bool is_full = (st->keypad_mode == KEYPAD_FULL);

    /* Dim entire screen */
    vnc_renderer_fill_rect(fb, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGB565(10, 10, 10));

    /* Overlay panel background */
    vnc_renderer_fill_rect(fb, FKP_X, FKP_Y, FKP_W, FKP_H, RGB565(20, 20, 20));
    /* Border */
    vnc_renderer_fill_rect(fb, FKP_X, FKP_Y, FKP_W, 2, RGB565_WHITE);
    vnc_renderer_fill_rect(fb, FKP_X, FKP_Y + FKP_H - 2, FKP_W, 2, RGB565_WHITE);
    vnc_renderer_fill_rect(fb, FKP_X, FKP_Y, 2, FKP_H, RGB565_WHITE);
    vnc_renderer_fill_rect(fb, FKP_X + FKP_W - 2, FKP_Y, 2, FKP_H, RGB565_WHITE);

    /* Title */
    const char *title;
    switch (st->editing_field) {
    case 2:  title = "EDIT PASSWORD (MAX 8 CHARS)"; break;
    case 3:  title = "EDIT ENCODINGS"; break;
    default: title = "EDIT FIELD"; break;
    }
    vnc_renderer_draw_text(fb, FKP_X + 10, FKP_Y + 8, title, RGB565_YELLOW, 2);

    /* Input field */
    vnc_renderer_fill_rect(fb, FKP_INPUT_X, FKP_INPUT_Y, FKP_INPUT_W, FKP_INPUT_H,
                           RGB565(0, 0, 40));
    /* Border */
    vnc_renderer_fill_rect(fb, FKP_INPUT_X, FKP_INPUT_Y, FKP_INPUT_W, 1, RGB565_GREEN);
    vnc_renderer_fill_rect(fb, FKP_INPUT_X, FKP_INPUT_Y + FKP_INPUT_H - 1, FKP_INPUT_W, 1,
                           RGB565_GREEN);
    vnc_renderer_fill_rect(fb, FKP_INPUT_X, FKP_INPUT_Y, 1, FKP_INPUT_H, RGB565_GREEN);
    vnc_renderer_fill_rect(fb, FKP_INPUT_X + FKP_INPUT_W - 1, FKP_INPUT_Y, 1, FKP_INPUT_H,
                           RGB565_GREEN);

    /* Input text — for PASSWORD show actual text in keypad edit mode */
    vnc_renderer_draw_text(fb, FKP_INPUT_X + 8, FKP_INPUT_Y + 10,
                           st->keypad_buf, RGB565_GREEN, 2);

    /* Cursor */
    {
        int cx = FKP_INPUT_X + 8 + vnc_renderer_text_width(st->keypad_buf, 2);
        vnc_renderer_fill_rect(fb, cx, FKP_INPUT_Y + 8, 2, 20, RGB565_GREEN);
    }

    /* ── Draw letter keys (rows 0-2: A-Z + symbols) ─────────── */
    int total_letter_keys;
    if (is_full) {
        /* FULL: 26 letters + 4 symbols = 30 keys (3 rows) */
        total_letter_keys = 30;
    } else {
        /* ALPHA: 26 letters + 4 symbols = 30 keys (3 rows) */
        total_letter_keys = 30;
    }

    for (int idx = 0; idx < total_letter_keys; idx++) {
        int kx, ky;
        fkp_key_pos(idx, &kx, &ky);

        char label[4];
        uint16_t bg = RGB565(50, 50, 50);
        uint16_t fg = RGB565_WHITE;

        if (idx < 26) {
            /* Letter key */
            if (st->shift_active)
                label[0] = full_letters_upper[idx];
            else
                label[0] = full_letters_lower[idx];
            label[1] = '\0';
        } else {
            /* Symbol keys (index 26-29) */
            int sym_idx = idx - 26;
            if (is_full) {
                label[0] = full_symbols[sym_idx];
                label[1] = '\0';
            } else {
                if (alpha_symbols[sym_idx] == ' ') {
                    strcpy(label, "SPC");
                } else {
                    label[0] = alpha_symbols[sym_idx];
                    label[1] = '\0';
                }
            }
            bg = RGB565(60, 50, 30);
            fg = RGB565_YELLOW;
        }

        draw_button(fb, kx, ky, FKP_KEY_W, FKP_KEY_H, label, bg, fg, 2);
    }

    /* ── FULL mode: draw digit row (row 3, keys 30-39) ───────── */
    if (is_full) {
        for (int d = 0; d < 10; d++) {
            int kx, ky;
            fkp_key_pos(30 + d, &kx, &ky);

            char label[2] = { full_digits[d], '\0' };
            draw_button(fb, kx, ky, FKP_KEY_W, FKP_KEY_H, label,
                        RGB565(50, 50, 50), RGB565_WHITE, 2);
        }
    }

    /* ── Action row ──────────────────────────────────────────── */
    int action_row_idx = is_full ? 4 : 3;  /* rows start at 0 */
    int action_y = FKP_KEYS_Y + action_row_idx * (FKP_KEY_H + FKP_KEY_PAD);

    if (is_full) {
        /* FULL mode action row: SHIFT, DEL, CLR, CANCEL, OK */
        int ax = FKP_X + 10;
        int btn_w = 120;
        int btn_pad = 8;

        /* SHIFT */
        uint16_t shift_bg = st->shift_active ? RGB565(0, 80, 120) : RGB565(60, 60, 60);
        uint16_t shift_fg = st->shift_active ? RGB565(100, 200, 255) : RGB565_WHITE;
        draw_button(fb, ax, action_y, btn_w, FKP_KEY_H, "SHIFT", shift_bg, shift_fg, 2);
        ax += btn_w + btn_pad;

        /* DEL */
        draw_button(fb, ax, action_y, 80, FKP_KEY_H, "DEL",
                    RGB565(80, 60, 0), RGB565_YELLOW, 2);
        ax += 80 + btn_pad;

        /* CLR */
        draw_button(fb, ax, action_y, 80, FKP_KEY_H, "CLR",
                    RGB565(80, 60, 0), RGB565_YELLOW, 2);
        ax += 80 + btn_pad;

        /* CANCEL */
        draw_button(fb, ax, action_y, btn_w, FKP_KEY_H, "CANCEL",
                    RGB565(160, 0, 0), RGB565_RED, 2);
        ax += btn_w + btn_pad;

        /* OK */
        draw_button(fb, ax, action_y, btn_w, FKP_KEY_H, "OK",
                    RGB565(0, 100, 0), RGB565_GREEN, 2);
    } else {
        /* ALPHA mode action row: DEL, CLR, CANCEL, OK */
        int ax = FKP_X + 10;
        int btn_w = 130;
        int btn_pad = 12;

        /* DEL */
        draw_button(fb, ax, action_y, 100, FKP_KEY_H, "DEL",
                    RGB565(80, 60, 0), RGB565_YELLOW, 2);
        ax += 100 + btn_pad;

        /* CLR */
        draw_button(fb, ax, action_y, 100, FKP_KEY_H, "CLR",
                    RGB565(80, 60, 0), RGB565_YELLOW, 2);
        ax += 100 + btn_pad;

        /* CANCEL */
        draw_button(fb, ax, action_y, btn_w, FKP_KEY_H, "CANCEL",
                    RGB565(160, 0, 0), RGB565_RED, 2);
        ax += btn_w + btn_pad;

        /* OK */
        draw_button(fb, ax, action_y, btn_w, FKP_KEY_H, "OK",
                    RGB565(0, 100, 0), RGB565_GREEN, 2);
    }

    fb_swap(fb);
}

/* ── Handle touch on FULL/ALPHA keypad ─────────────────────────────── */
/*
 * Returns:
 *   -1  no action (key pressed, or miss — keep keypad open)
 *    0  CANCEL — discard, return to main screen
 *    1  OK — commit value, return to main screen
 */
static int handle_keypad_full_touch(SettingsState *st, int tx, int ty) {
    bool is_full = (st->keypad_mode == KEYPAD_FULL);
    int total_letter_keys = 30; /* 26 letters + 4 symbols */

    /* ── Test letter/symbol/digit key hits ───────────────────── */
    for (int idx = 0; idx < total_letter_keys; idx++) {
        int kx, ky;
        fkp_key_pos(idx, &kx, &ky);

        if (!hit_rect(tx, ty, kx, ky, FKP_KEY_W, FKP_KEY_H))
            continue;

        char ch;
        if (idx < 26) {
            ch = st->shift_active ? full_letters_upper[idx] : full_letters_lower[idx];
        } else {
            int sym_idx = idx - 26;
            ch = is_full ? full_symbols[sym_idx] : alpha_symbols[sym_idx];
        }

        /* Buffer overflow protection */
        if (st->keypad_cursor >= st->keypad_max_len)
            return -1;

        st->keypad_buf[st->keypad_cursor++] = ch;
        st->keypad_buf[st->keypad_cursor] = '\0';
        return -1;
    }

    /* ── FULL mode: test digit key hits (row 3) ──────────────── */
    if (is_full) {
        for (int d = 0; d < 10; d++) {
            int kx, ky;
            fkp_key_pos(30 + d, &kx, &ky);

            if (!hit_rect(tx, ty, kx, ky, FKP_KEY_W, FKP_KEY_H))
                continue;

            if (st->keypad_cursor >= st->keypad_max_len)
                return -1;

            st->keypad_buf[st->keypad_cursor++] = full_digits[d];
            st->keypad_buf[st->keypad_cursor] = '\0';
            return -1;
        }
    }

    /* ── Test action row buttons ─────────────────────────────── */
    int action_row_idx = is_full ? 4 : 3;
    int action_y = FKP_KEYS_Y + action_row_idx * (FKP_KEY_H + FKP_KEY_PAD);

    if (is_full) {
        /* FULL mode action row layout: SHIFT(120), DEL(80), CLR(80), CANCEL(120), OK(120) */
        int ax = FKP_X + 10;
        int btn_w = 120;
        int btn_pad = 8;

        /* SHIFT */
        if (hit_rect(tx, ty, ax, action_y, btn_w, FKP_KEY_H)) {
            st->shift_active = !st->shift_active;
            return -1;
        }
        ax += btn_w + btn_pad;

        /* DEL */
        if (hit_rect(tx, ty, ax, action_y, 80, FKP_KEY_H)) {
            if (st->keypad_cursor > 0)
                st->keypad_buf[--st->keypad_cursor] = '\0';
            return -1;
        }
        ax += 80 + btn_pad;

        /* CLR */
        if (hit_rect(tx, ty, ax, action_y, 80, FKP_KEY_H)) {
            st->keypad_buf[0] = '\0';
            st->keypad_cursor = 0;
            return -1;
        }
        ax += 80 + btn_pad;

        /* CANCEL */
        if (hit_rect(tx, ty, ax, action_y, btn_w, FKP_KEY_H)) {
            st->screen = SCREEN_MAIN;
            return 0;
        }
        ax += btn_w + btn_pad;

        /* OK */
        if (hit_rect(tx, ty, ax, action_y, btn_w, FKP_KEY_H)) {
            /* Commit PASSWORD */
            strncpy(st->working.password, st->keypad_buf,
                    sizeof(st->working.password) - 1);
            st->working.password[sizeof(st->working.password) - 1] = '\0';
            st->screen = SCREEN_MAIN;
            return 1;
        }
    } else {
        /* ALPHA mode action row layout: DEL(100), CLR(100), CANCEL(130), OK(130) */
        int ax = FKP_X + 10;
        int btn_w = 130;
        int btn_pad = 12;

        /* DEL */
        if (hit_rect(tx, ty, ax, action_y, 100, FKP_KEY_H)) {
            if (st->keypad_cursor > 0)
                st->keypad_buf[--st->keypad_cursor] = '\0';
            return -1;
        }
        ax += 100 + btn_pad;

        /* CLR */
        if (hit_rect(tx, ty, ax, action_y, 100, FKP_KEY_H)) {
            st->keypad_buf[0] = '\0';
            st->keypad_cursor = 0;
            return -1;
        }
        ax += 100 + btn_pad;

        /* CANCEL */
        if (hit_rect(tx, ty, ax, action_y, btn_w, FKP_KEY_H)) {
            st->screen = SCREEN_MAIN;
            return 0;
        }
        ax += btn_w + btn_pad;

        /* OK */
        if (hit_rect(tx, ty, ax, action_y, btn_w, FKP_KEY_H)) {
            /* Commit ENCODINGS */
            strncpy(st->working.encodings, st->keypad_buf,
                    sizeof(st->working.encodings) - 1);
            st->working.encodings[sizeof(st->working.encodings) - 1] = '\0';
            st->screen = SCREEN_MAIN;
            return 1;
        }
    }

    return -1;  /* touch missed all buttons */
}

/* ── Save config file ──────────────────────────────────────────────── */

static int save_config_file(const VNCConfig *cfg, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "# VNC Client Configuration (written by settings GUI)\n\n");
    fprintf(f, "host = %s\n", cfg->host);
    fprintf(f, "port = %d\n", cfg->port);
    fprintf(f, "password = %s\n", cfg->password);
    fprintf(f, "encodings = %s\n", cfg->encodings);
    fprintf(f, "compress_level = %d\n", cfg->compress_level);
    fprintf(f, "quality_level = %d\n", cfg->quality_level);

    fclose(f);
    return 0;
}

/* ── Main entry point ──────────────────────────────────────────────── */

int vnc_settings_run(VNCConfig *config, Framebuffer *fb, TouchInput *touch,
                     const char *config_path) {
    SettingsState st;
    memset(&st, 0, sizeof(st));

    /* Copy config to working buffer */
    memcpy(&st.working, config, sizeof(VNCConfig));
    st.live = config;
    st.fb = fb;
    st.touch = touch;
    st.config_path = config_path;
    st.screen = SCREEN_MAIN;
    st.save_error = 0;
    st.keypad_mode = KEYPAD_NUMERIC;
    st.shift_active = false;

    /* Drain any pending touch events */
    if (touch)
        touch_drain_events(touch);

    while (1) {
        /* Draw current screen */
        if (st.screen == SCREEN_MAIN) {
            draw_main_screen(&st);
        } else {
            /* Keypad screen — choose layout based on mode */
            if (st.keypad_mode == KEYPAD_NUMERIC)
                draw_keypad(&st);
            else
                draw_keypad_full(&st);
        }

        /* Poll touch input */
        int tx = -1, ty = -1;
        if (touch) {
            touch_poll(touch);
            TouchState ts = touch_get_state(touch);
            if (ts.released) {
                tx = ts.x;
                ty = ts.y;
            }
        }

        /* Decrement save error display counter */
        if (st.save_error > 0)
            st.save_error--;

        if (tx >= 0 && ty >= 0) {
            if (st.screen == SCREEN_MAIN) {
                int result = handle_main_touch(&st, tx, ty);

                if (result == SETTINGS_BACK)
                    return SETTINGS_BACK;

                if (result == SETTINGS_EXIT)
                    return SETTINGS_EXIT;

                if (result == SETTINGS_SAVE) {
                    /* Attempt to save config file */
                    if (save_config_file(&st.working, config_path) < 0) {
                        /* Show error for ~2 seconds (60 frames at 30fps) */
                        st.save_error = 60;
                    } else {
                        /* Copy working config back to live */
                        memcpy(config, &st.working, sizeof(VNCConfig));
                        return SETTINGS_SAVE;
                    }
                }
                /* result == 99 means switched to keypad, will redraw next loop */
                /* result == -1 means no action or +/- button pressed */
            } else {
                /* Keypad screen — dispatch to correct handler */
                if (st.keypad_mode == KEYPAD_NUMERIC)
                    handle_keypad_touch(&st, tx, ty);
                else
                    handle_keypad_full_touch(&st, tx, ty);
                /* Keypad handler manages its own screen transitions */
            }
        }

        /* ~30 fps */
        usleep(33000);
    }
}
