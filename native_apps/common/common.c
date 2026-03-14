/**
 * Common UI Library Implementation for RoomWizard
 */

#include "common.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

// ============================================================================
// TIME UTILITIES
// ============================================================================

uint32_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// ============================================================================
// SINGLETON INSTANCE LOCK
// ============================================================================

int acquire_instance_lock(const char *app_name) {
    char lock_path[256];
    snprintf(lock_path, sizeof(lock_path), "/tmp/.%s.lock", app_name);

    int fd = open(lock_path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        perror("acquire_instance_lock: open");
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        /* Another instance holds the lock */
        close(fd);
        return -1;
    }

    /* Write our PID for diagnostics */
    char pid_str[16];
    int len = snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    if (ftruncate(fd, 0) == 0) {
        ssize_t w = write(fd, pid_str, len);
        (void)w;  /* Best-effort; failure is non-fatal */
    }

    return fd;  /* Caller must keep fd open — lock held until close/exit */
}

// ============================================================================
// TEXT UTILITIES
// ============================================================================

void text_to_uppercase(char *dest, const char *src, size_t max_len) {
    size_t i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++) {
        dest[i] = toupper((unsigned char)src[i]);
    }
    dest[i] = '\0';
}

int text_measure_width(const char *text, int scale) {
    return strlen(text) * 6 * scale;  // 5x7 font + 1px spacing = 6px per char
}

int text_measure_height(int scale) {
    return 7 * scale;  // 5x7 font height
}

void text_draw_centered(Framebuffer *fb, int center_x, int center_y,
                       const char *text, uint32_t color, int scale) {
    int text_width = text_measure_width(text, scale);
    int text_height = text_measure_height(scale);
    int x = center_x - (text_width / 2);
    int y = center_y - (text_height / 2);
    fb_draw_text(fb, x, y, text, color, scale);
}

void text_truncate(char *dest, const char *src, int max_width, int scale) {
    // Convert to uppercase first
    char upper[256];
    text_to_uppercase(upper, src, sizeof(upper));
    
    int full_width = text_measure_width(upper, scale);
    
    if (max_width <= 0 || full_width <= max_width) {
        // No truncation needed
        strcpy(dest, upper);
        return;
    }
    
    // Need to truncate with "..."
    int ellipsis_width = text_measure_width("...", scale);
    int available_width = max_width - ellipsis_width;
    
    if (available_width <= 0) {
        strcpy(dest, "...");
        return;
    }
    
    // Calculate how many characters fit
    int char_width = 8 * scale;
    int max_chars = available_width / char_width;
    
    if (max_chars <= 0) {
        strcpy(dest, "...");
        return;
    }
    
    // Copy characters and add ellipsis
    strncpy(dest, upper, max_chars);
    dest[max_chars] = '\0';
    strcat(dest, "...");
}

// ============================================================================
// BUTTON AUTO-SIZING
// ============================================================================

int button_calc_min_width(const char *text, int scale, int padding) {
    int text_width = text_measure_width(text, scale);
    return text_width + (padding * 2);
}

void button_auto_size(Button *btn, int padding) {
    btn->width = button_calc_min_width(btn->text, btn->text_scale, padding);
}

// ============================================================================
// ICON DRAWING HELPERS
// ============================================================================

void icon_draw_hamburger(Framebuffer *fb, int x, int y, int size, uint32_t color) {
    int icon_width = size;
    int icon_height = size / 10;
    if (icon_height < 3) icon_height = 3;
    int icon_spacing = size / 5;
    if (icon_spacing < 6) icon_spacing = 6;
    
    int icon_x = x - icon_width / 2;
    int icon_y = y - (3 * icon_height + 2 * icon_spacing) / 2;
    
    fb_fill_rect(fb, icon_x, icon_y, icon_width, icon_height, color);
    fb_fill_rect(fb, icon_x, icon_y + icon_height + icon_spacing, icon_width, icon_height, color);
    fb_fill_rect(fb, icon_x, icon_y + 2 * (icon_height + icon_spacing), icon_width, icon_height, color);
}

void icon_draw_x(Framebuffer *fb, int x, int y, int size, uint32_t color) {
    int icon_size = size;
    int icon_x = x - icon_size / 2;
    int icon_y = y - icon_size / 2;
    int thickness = size / 8;
    if (thickness < 3) thickness = 3;
    
    // Draw X using thick lines
    for (int i = 0; i < icon_size; i++) {
        for (int t = 0; t < thickness; t++) {
            // Top-left to bottom-right
            fb_draw_pixel(fb, icon_x + i, icon_y + i + t, color);
            // Top-right to bottom-left
            fb_draw_pixel(fb, icon_x + icon_size - i, icon_y + i + t, color);
        }
    }
}

// ============================================================================
// BUTTON MANAGEMENT
// ============================================================================

void button_init_full(Button *btn, int x, int y, int width, int height,
                      const char *text, uint32_t bg_color, uint32_t text_color,
                      uint32_t highlight_color, int text_scale) {
    btn->x = x;
    btn->y = y;
    btn->width = width;
    btn->height = height;
    
    // Convert text to uppercase and store
    text_to_uppercase(btn->text, text, sizeof(btn->text));
    btn->max_text_width = 0;  // No limit by default
    
    btn->bg_color = bg_color;
    btn->text_color = text_color;
    btn->highlight_color = highlight_color;
    btn->border_color = text_color;
    btn->text_scale = text_scale;
    btn->border_width = 2;
    
    btn->visual_state = BTN_STATE_NORMAL;
    btn->was_pressed = false;
    btn->last_press_time_ms = 0;
    btn->debounce_ms = BTN_DEBOUNCE_MS;
    
    btn->draw_icon = NULL;
}

void button_init_simple(Button *btn, int x, int y, int width, int height,
                        const char *text) {
    button_init_full(btn, x, y, width, height, text,
                     BTN_COLOR_PRIMARY, COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);
}

void button_set_text(Button *btn, const char *text) {
    if (btn->max_text_width > 0) {
        text_truncate(btn->text, text, btn->max_text_width, btn->text_scale);
    } else {
        text_to_uppercase(btn->text, text, sizeof(btn->text));
    }
}

void button_set_max_text_width(Button *btn, int max_width) {
    btn->max_text_width = max_width;
    // Re-apply text with new constraint
    if (btn->text[0] != '\0') {
        char temp[128];
        strcpy(temp, btn->text);
        button_set_text(btn, temp);
    }
}

void button_set_colors(Button *btn, uint32_t bg, uint32_t text, uint32_t highlight) {
    btn->bg_color = bg;
    btn->text_color = text;
    btn->highlight_color = highlight;
}

void button_set_border(Button *btn, uint32_t color, int width) {
    btn->border_color = color;
    btn->border_width = width;
}

void button_set_debounce(Button *btn, uint32_t ms) {
    btn->debounce_ms = ms;
}

void button_set_icon(Button *btn, void (*draw_icon)(Framebuffer*, int, int, int, uint32_t)) {
    btn->draw_icon = draw_icon;
}

// ============================================================================
// TOUCH HANDLING
// ============================================================================

bool button_is_touched(Button *btn, int touch_x, int touch_y) {
    return (touch_x >= btn->x && touch_x < btn->x + btn->width &&
            touch_y >= btn->y && touch_y < btn->y + btn->height);
}

bool button_update(Button *btn, int touch_x, int touch_y, bool is_touching, uint32_t current_time_ms) {
    bool is_touched = button_is_touched(btn, touch_x, touch_y);
    bool pressed = false;
    
    if (is_touching && is_touched) {
        // Touch is on button
        if (!btn->was_pressed) {
            // Check debounce
            if (current_time_ms - btn->last_press_time_ms > btn->debounce_ms) {
                btn->was_pressed = true;
                btn->last_press_time_ms = current_time_ms;
                btn->visual_state = BTN_STATE_PRESSED;
                pressed = true;
            }
        } else {
            btn->visual_state = BTN_STATE_PRESSED;
        }
    } else if (is_touching && !is_touched) {
        // Touch is elsewhere
        btn->visual_state = BTN_STATE_NORMAL;
    } else {
        // No touch
        btn->was_pressed = false;
        if (is_touched) {
            btn->visual_state = BTN_STATE_HIGHLIGHTED;
        } else {
            btn->visual_state = BTN_STATE_NORMAL;
        }
    }
    
    return pressed;
}

bool button_check_press(Button *btn, bool currently_pressed, uint32_t current_time_ms) {
    // Legacy API for compatibility with existing games
    if (currently_pressed) {
        if (!btn->was_pressed) {
            uint32_t time_since_last = current_time_ms - btn->last_press_time_ms;
            if (time_since_last > btn->debounce_ms) {
                // Leading edge — fire and highlight briefly this frame
                btn->was_pressed = true;
                btn->last_press_time_ms = current_time_ms;
                btn->visual_state = BTN_STATE_HIGHLIGHTED;
                return true;
            }
            // Still within debounce window from a previous bounce — look normal
            btn->visual_state = BTN_STATE_NORMAL;
        } else {
            // Already fired; finger still down — show normal so button never
            // appears stuck/yellow. Resets to was_pressed=false on release.
            btn->visual_state = BTN_STATE_NORMAL;
        }
    } else {
        // Finger lifted — ready for next press
        btn->was_pressed = false;
        btn->visual_state = BTN_STATE_NORMAL;
    }

    return false;
}

// ============================================================================
// RENDERING
// ============================================================================

void button_draw(Framebuffer *fb, Button *btn) {
    // Determine colors based on state
    uint32_t bg = btn->bg_color;
    uint32_t text = btn->text_color;
    uint32_t border = btn->border_color;
    
    if (btn->visual_state == BTN_STATE_HIGHLIGHTED) {
        border = btn->highlight_color;
        text = btn->highlight_color;
    } else if (btn->visual_state == BTN_STATE_PRESSED) {
        bg = btn->highlight_color;
        text = btn->bg_color;
        border = btn->highlight_color;
    }
    
    // Draw background
    fb_fill_rect(fb, btn->x, btn->y, btn->width, btn->height, bg);
    
    // Draw border
    for (int i = 0; i < btn->border_width; i++) {
        // Top
        fb_draw_rect(fb, btn->x + i, btn->y + i, btn->width - 2*i, 1, border);
        // Bottom
        fb_draw_rect(fb, btn->x + i, btn->y + btn->height - 1 - i, btn->width - 2*i, 1, border);
        // Left
        fb_draw_rect(fb, btn->x + i, btn->y + i, 1, btn->height - 2*i, border);
        // Right
        fb_draw_rect(fb, btn->x + btn->width - 1 - i, btn->y + i, 1, btn->height - 2*i, border);
    }
    
    // Draw icon or text
    int center_x = btn->x + btn->width / 2;
    int center_y = btn->y + btn->height / 2;
    
    if (btn->draw_icon) {
        // Draw custom icon
        int icon_size = (btn->height < btn->width ? btn->height : btn->width) - 20;
        btn->draw_icon(fb, center_x, center_y, icon_size, text);
    } else if (btn->text[0] != '\0') {
        // Draw centered text
        text_draw_centered(fb, center_x, center_y, btn->text, text, btn->text_scale);
    }
}

void button_draw_menu(Framebuffer *fb, Button *btn) {
    // Temporarily set icon and draw
    void (*old_icon)(Framebuffer*, int, int, int, uint32_t) = btn->draw_icon;
    btn->draw_icon = icon_draw_hamburger;
    button_draw(fb, btn);
    btn->draw_icon = old_icon;
}

void button_draw_exit(Framebuffer *fb, Button *btn) {
    // Temporarily set icon and draw
    void (*old_icon)(Framebuffer*, int, int, int, uint32_t) = btn->draw_icon;
    btn->draw_icon = icon_draw_x;
    button_draw(fb, btn);
    btn->draw_icon = old_icon;
}

// ============================================================================
// SCREEN TEMPLATES
// ============================================================================

void screen_draw_welcome(Framebuffer *fb, const char *game_title,
                        const char *instructions, Button *start_btn) {
    fb_clear(fb, COLOR_BLACK);
    
    // Draw title (using safe area)
    char upper_title[256];
    text_to_uppercase(upper_title, game_title, sizeof(upper_title));
    int title_width = strlen(upper_title) * 8 * 4;
    int title_x = LAYOUT_CENTER_X(title_width);
    int title_y = SCREEN_SAFE_TOP + 50;  // 50px from safe top
    fb_draw_text(fb, title_x, title_y, upper_title, COLOR_CYAN, 4);
    
    // Draw instructions (centered in safe area)
    if (instructions && instructions[0]) {
        char upper_inst[512];
        text_to_uppercase(upper_inst, instructions, sizeof(upper_inst));
        int inst_width = strlen(upper_inst) * 8;
        int inst_x = LAYOUT_CENTER_X(inst_width);
        int inst_y = LAYOUT_CENTER_Y(8) + 20;  // Slightly below center
        fb_draw_text(fb, inst_x, inst_y, upper_inst, COLOR_WHITE, 1);
    }
    
    // Draw start button
    button_draw(fb, start_btn);
}

void screen_draw_game_over(Framebuffer *fb, const char *message, int score,
                          Button *restart_btn) {
    fb_clear(fb, COLOR_BLACK);
    
    // Draw message (centered in safe area)
    char upper_msg[256];
    text_to_uppercase(upper_msg, message, sizeof(upper_msg));
    int msg_width = strlen(upper_msg) * 8 * 3;
    int msg_x = LAYOUT_CENTER_X(msg_width);
    int msg_y = SCREEN_SAFE_TOP + (SCREEN_SAFE_HEIGHT / 3);
    fb_draw_text(fb, msg_x, msg_y, upper_msg, COLOR_RED, 3);
    
    // Draw score (centered in safe area)
    char score_text[64];
    snprintf(score_text, sizeof(score_text), "SCORE: %d", score);
    int score_width = strlen(score_text) * 8 * 2;
    int score_x = LAYOUT_CENTER_X(score_width);
    int score_y = LAYOUT_CENTER_Y(16) - 30;
    fb_draw_text(fb, score_x, score_y, score_text, COLOR_WHITE, 2);
    
    // Draw restart button
    button_draw(fb, restart_btn);
}

// ============================================================================
// MODAL DIALOG
// ============================================================================

void modal_dialog_init(ModalDialog *dlg, const char *title, const char *message,
                       int button_count) {
    memset(dlg, 0, sizeof(ModalDialog));
    dlg->active = false;

    if (title) snprintf(dlg->title, sizeof(dlg->title), "%s", title);
    if (message) snprintf(dlg->message, sizeof(dlg->message), "%s", message);

    /* Clamp button count */
    if (button_count < 1) button_count = 1;
    if (button_count > MODAL_MAX_BUTTONS) button_count = MODAL_MAX_BUTTONS;
    dlg->button_count = button_count;

    /* Default visual settings */
    dlg->overlay_alpha = 180;
    dlg->dialog_width = 420;
    dlg->bg_color = RGB(30, 30, 45);
    dlg->border_color = COLOR_WHITE;
    dlg->title_color = COLOR_YELLOW;
    dlg->message_color = RGB(180, 180, 180);

    /* Auto-calculate dialog height based on button count */
    if (button_count <= 2)
        dlg->dialog_height = 200;
    else if (button_count == 3)
        dlg->dialog_height = 260;
    else
        dlg->dialog_height = 310;
}

void modal_dialog_set_button(ModalDialog *dlg, int index, const char *text,
                             uint32_t bg_color, uint32_t text_color) {
    if (index < 0 || index >= dlg->button_count) return;
    button_init_full(&dlg->buttons[index], 0, 0, 150, 50,
                     text ? text : "", bg_color, text_color,
                     BTN_COLOR_HIGHLIGHT, 2);
}

void modal_dialog_init_confirm(ModalDialog *dlg, const char *title,
                               const char *message,
                               const char *confirm_text, uint32_t confirm_color,
                               const char *cancel_text, uint32_t cancel_color) {
    modal_dialog_init(dlg, title, message, 2);
    modal_dialog_set_button(dlg, 0, confirm_text ? confirm_text : "CONFIRM",
                            confirm_color, COLOR_WHITE);
    modal_dialog_set_button(dlg, 1, cancel_text ? cancel_text : "CANCEL",
                            cancel_color, COLOR_WHITE);
}

void modal_dialog_show(ModalDialog *dlg) {
    dlg->active = true;
}

void modal_dialog_hide(ModalDialog *dlg) {
    dlg->active = false;
}

bool modal_dialog_is_active(ModalDialog *dlg) {
    return dlg->active;
}

void modal_dialog_draw(ModalDialog *dlg, Framebuffer *fb) {
    if (!dlg->active) return;

    int dw = dlg->dialog_width;
    int dh = dlg->dialog_height;
    int n  = dlg->button_count;

    /* Semi-transparent overlay */
    if (dlg->overlay_alpha > 0) {
        fb_fill_rect_alpha(fb, 0, 0, fb->width, fb->height,
                           COLOR_BLACK, dlg->overlay_alpha);
    }

    /* Centered dialog box */
    int dx = (fb->width - dw) / 2;
    int dy = (fb->height - dh) / 2;

    fb_fill_rect(fb, dx, dy, dw, dh, dlg->bg_color);
    fb_draw_rect(fb, dx, dy, dw, dh, dlg->border_color);

    /* Title text — centered, scale 3 */
    if (dlg->title[0]) {
        text_draw_centered(fb, fb->width / 2, dy + 45, dlg->title,
                           dlg->title_color, 3);
    }

    /* Message text — centered, scale 2 */
    if (dlg->message[0]) {
        text_draw_centered(fb, fb->width / 2, dy + 90, dlg->message,
                           dlg->message_color, 2);
    }

    /* Position and draw buttons */
    if (n == 2) {
        /* Side-by-side layout (preserves original 2-button look) */
        int btn_w = 150, btn_h = 50;
        int btn_gap = 30;
        int total_btn_w = btn_w * 2 + btn_gap;
        int btn_x = (fb->width - total_btn_w) / 2;
        int btn_y = dy + dh - btn_h - 25;

        dlg->buttons[0].x = btn_x;
        dlg->buttons[0].y = btn_y;
        dlg->buttons[0].width = btn_w;
        dlg->buttons[0].height = btn_h;

        dlg->buttons[1].x = btn_x + btn_w + btn_gap;
        dlg->buttons[1].y = btn_y;
        dlg->buttons[1].width = btn_w;
        dlg->buttons[1].height = btn_h;
    } else {
        /* Vertically stacked layout (1, 3, or 4 buttons) */
        int btn_w = 200, btn_h = 44;
        int btn_gap = 8;
        int total_h = n * btn_h + (n - 1) * btn_gap;
        int btn_x = (fb->width - btn_w) / 2;
        int btn_y_start = dy + dh - total_h - 20;

        for (int i = 0; i < n; i++) {
            dlg->buttons[i].x = btn_x;
            dlg->buttons[i].y = btn_y_start + i * (btn_h + btn_gap);
            dlg->buttons[i].width = btn_w;
            dlg->buttons[i].height = btn_h;
        }
    }

    for (int i = 0; i < n; i++) {
        button_draw(fb, &dlg->buttons[i]);
    }
}

ModalDialogAction modal_dialog_update(ModalDialog *dlg, int touch_x, int touch_y,
                                       bool touch_active, uint32_t now_ms) {
    if (!dlg->active) return MODAL_ACTION_NONE;

    for (int i = 0; i < dlg->button_count; i++) {
        if (button_update(&dlg->buttons[i], touch_x, touch_y, touch_active, now_ms)) {
            dlg->active = false;
            return (ModalDialogAction)i;
        }
    }

    return MODAL_ACTION_NONE;
}

// ============================================================================
// TOGGLE SWITCH CONTROL
// ============================================================================

void toggle_init(ToggleSwitch *sw, int x, int y, int track_w, int track_h,
                 const char *label, bool initial_state) {
    sw->x = x;
    sw->y = y;
    sw->track_w = track_w;
    sw->track_h = track_h;
    sw->state = initial_state;
    text_to_uppercase(sw->label, label, sizeof(sw->label));

    // Default colors
    sw->on_color = RGB(0, 180, 60);
    sw->off_color = RGB(100, 100, 100);
    sw->knob_color = COLOR_WHITE;
    sw->label_color = RGB(200, 200, 200);

    sw->was_pressed = false;
    sw->last_press_time_ms = 0;
    sw->debounce_ms = 300;
}

void toggle_set_colors(ToggleSwitch *sw, uint32_t on_color, uint32_t off_color,
                       uint32_t knob_color, uint32_t label_color) {
    sw->on_color = on_color;
    sw->off_color = off_color;
    sw->knob_color = knob_color;
    sw->label_color = label_color;
}

bool toggle_check_press(ToggleSwitch *sw, int touch_x, int touch_y,
                        bool is_pressed, uint32_t current_time_ms) {
    // Generous hit area: track + label area + some padding
    int hit_w = sw->track_w + text_measure_width(sw->label, 1) + 20;
    int hit_h = sw->track_h + 10;
    int hit_x = sw->x - 5;
    int hit_y = sw->y - 5;

    bool in_bounds = (touch_x >= hit_x && touch_x < hit_x + hit_w &&
                      touch_y >= hit_y && touch_y < hit_y + hit_h);

    if (is_pressed && in_bounds) {
        if (!sw->was_pressed &&
            (current_time_ms - sw->last_press_time_ms > sw->debounce_ms)) {
            sw->was_pressed = true;
            sw->last_press_time_ms = current_time_ms;
            sw->state = !sw->state;
            return true;  // State changed
        }
    } else if (!is_pressed) {
        sw->was_pressed = false;
    }

    return false;
}

void toggle_draw(Framebuffer *fb, ToggleSwitch *sw) {
    uint32_t track_color = sw->state ? sw->on_color : sw->off_color;
    int r = sw->track_h / 2;  // Corner radius = half height for pill shape

    // Draw track (pill shape)
    fb_fill_rounded_rect(fb, sw->x, sw->y, sw->track_w, sw->track_h, r, track_color);

    // Draw knob
    int knob_r = r - 2;
    int knob_cx, knob_cy;
    knob_cy = sw->y + sw->track_h / 2;
    if (sw->state) {
        knob_cx = sw->x + sw->track_w - r;  // Right side
    } else {
        knob_cx = sw->x + r;                 // Left side
    }
    fb_fill_circle(fb, knob_cx, knob_cy, knob_r, sw->knob_color);

    // Draw label to the right of the track
    int label_x = sw->x + sw->track_w + 8;
    int label_y = sw->y + (sw->track_h - 7) / 2;  // Vertically center (7px font height)
    fb_draw_text(fb, label_x, label_y, sw->label, sw->label_color, 1);
}

// ============================================================================
// UNIFIED GAME OVER SCREEN
// ============================================================================

void gameover_init(GameOverScreen *gos, Framebuffer *fb,
                   int score, const char *title, const char *info_line,
                   HighScoreTable *hs_table, TouchInput *touch) {
    (void)fb;  // Reserved for future use (e.g., pre-render calculations)
    memset(gos, 0, sizeof(*gos));
    gos->state = GAMEOVER_STATE_CHECK;
    gos->score = score;
    gos->hs_table = hs_table;
    gos->touch = touch;
    gos->hs_qualifies = false;
    gos->has_reset_scores = (hs_table != NULL);

    // Store title (default to "GAME OVER" if NULL/empty)
    if (title && title[0]) {
        text_to_uppercase(gos->title, title, sizeof(gos->title));
    } else {
        snprintf(gos->title, sizeof(gos->title), "GAME OVER");
    }

    // Store optional info line
    if (info_line && info_line[0]) {
        text_to_uppercase(gos->info_line, info_line, sizeof(gos->info_line));
    } else {
        gos->info_line[0] = '\0';
    }

    // Calculate button layout — vertical stack in the lower portion of screen
    int btn_x = LAYOUT_CENTER_X(BTN_LARGE_WIDTH);
    int btn_gap = 15;  // Gap between buttons

    if (gos->has_reset_scores) {
        // 3 buttons: RESTART, RESET SCORES, EXIT
        int total_height = 3 * BTN_LARGE_HEIGHT + 2 * btn_gap;
        int start_y = SCREEN_SAFE_BOTTOM - total_height - 15;

        button_init(&gos->restart_btn, btn_x, start_y,
                    BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT,
                    "RESTART", BTN_COLOR_PRIMARY, COLOR_WHITE, BTN_COLOR_HIGHLIGHT);

        button_init(&gos->reset_scores_btn, btn_x, start_y + BTN_LARGE_HEIGHT + btn_gap,
                    BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT,
                    "RESET SCORES", BTN_COLOR_WARNING, COLOR_WHITE, BTN_COLOR_HIGHLIGHT);

        button_init(&gos->exit_btn, btn_x, start_y + 2 * (BTN_LARGE_HEIGHT + btn_gap),
                    BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT,
                    "EXIT", BTN_COLOR_DANGER, COLOR_WHITE, BTN_COLOR_HIGHLIGHT);
    } else {
        // 2 buttons: RESTART, EXIT
        int total_height = 2 * BTN_LARGE_HEIGHT + btn_gap;
        int start_y = SCREEN_SAFE_BOTTOM - total_height - 15;

        button_init(&gos->restart_btn, btn_x, start_y,
                    BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT,
                    "RESTART", BTN_COLOR_PRIMARY, COLOR_WHITE, BTN_COLOR_HIGHLIGHT);

        button_init(&gos->exit_btn, btn_x, start_y + BTN_LARGE_HEIGHT + btn_gap,
                    BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT,
                    "EXIT", BTN_COLOR_DANGER, COLOR_WHITE, BTN_COLOR_HIGHLIGHT);
    }
}

GameOverAction gameover_update(GameOverScreen *gos, Framebuffer *fb,
                               int touch_x, int touch_y, bool touch_active) {
    // ── CHECK state: determine if score qualifies for highscore ──
    if (gos->state == GAMEOVER_STATE_CHECK) {
        if (gos->hs_table != NULL) {
            gos->hs_qualifies = (hs_qualifies(gos->hs_table, gos->score) >= 0);
            if (gos->hs_qualifies) {
                snprintf(gos->title, sizeof(gos->title), "NEW HIGH SCORE!");
                gos->state = GAMEOVER_STATE_NAME_ENTRY;
            } else {
                gos->state = GAMEOVER_STATE_DISPLAY;
            }
        } else {
            gos->state = GAMEOVER_STATE_DISPLAY;
        }
        return GAMEOVER_ACTION_NONE;
    }

    // ── NAME_ENTRY state: blocking keyboard for player name ──
    if (gos->state == GAMEOVER_STATE_NAME_ENTRY) {
        char hs_name[HS_NAME_LEN];
        hs_enter_name(fb, gos->touch, hs_name, gos->score);
        hs_insert(gos->hs_table, hs_name, gos->score);
        hs_save(gos->hs_table);
        hs_drain_touches(gos->touch);

        // Clear button press state so no spurious fire on first frame
        gos->restart_btn.was_pressed = false;
        gos->restart_btn.last_press_time_ms = 0;
        gos->exit_btn.was_pressed = false;
        gos->exit_btn.last_press_time_ms = 0;
        gos->reset_scores_btn.was_pressed = false;
        gos->reset_scores_btn.last_press_time_ms = 0;

        gos->state = GAMEOVER_STATE_DISPLAY;
        return GAMEOVER_ACTION_NONE;
    }

    // ── DISPLAY state: draw screen and handle button presses ──
    gameover_draw(gos, fb);

    if (!touch_active)
        return GAMEOVER_ACTION_NONE;

    uint32_t now = get_time_ms();

    // Check RESTART button
    bool restart_touched = button_is_touched(&gos->restart_btn, touch_x, touch_y);
    if (button_check_press(&gos->restart_btn, restart_touched, now)) {
        return GAMEOVER_ACTION_RESTART;
    }

    // Check EXIT button
    bool exit_touched = button_is_touched(&gos->exit_btn, touch_x, touch_y);
    if (button_check_press(&gos->exit_btn, exit_touched, now)) {
        return GAMEOVER_ACTION_EXIT;
    }

    // Check RESET SCORES button (only if highscore enabled)
    if (gos->has_reset_scores) {
        bool reset_touched = button_is_touched(&gos->reset_scores_btn, touch_x, touch_y);
        if (button_check_press(&gos->reset_scores_btn, reset_touched, now)) {
            hs_reset(gos->hs_table);
            hs_save(gos->hs_table);
            return GAMEOVER_ACTION_RESET_SCORES;
        }
    }

    return GAMEOVER_ACTION_NONE;
}

void gameover_draw(GameOverScreen *gos, Framebuffer *fb) {
    // Semi-transparent black overlay
    fb_fill_rect_alpha(fb, 0, 0, fb->width, fb->height, COLOR_BLACK, 160);

    int center_x = fb->width / 2;

    // Title — upper portion of screen
    int title_y = SCREEN_SAFE_TOP + SCREEN_SAFE_HEIGHT / 5;
    text_draw_centered(fb, center_x, title_y, gos->title, COLOR_YELLOW, 4);

    // Score — below title
    char score_buf[64];
    snprintf(score_buf, sizeof(score_buf), "SCORE: %d", gos->score);
    int score_y = title_y + text_measure_height(4) + 20;
    text_draw_centered(fb, center_x, score_y, score_buf, COLOR_WHITE, 3);

    // Optional info line — below score
    int info_y = score_y + text_measure_height(3) + 12;
    if (gos->info_line[0]) {
        text_draw_centered(fb, center_x, info_y, gos->info_line, COLOR_CYAN, 2);
        info_y += text_measure_height(2) + 12;
    }

    // Highscore leaderboard (if table exists and has entries)
    if (gos->hs_table != NULL && gos->hs_table->count > 0) {
        int hs_width = SCREEN_SAFE_WIDTH - 40;
        int hs_x = LAYOUT_CENTER_X(hs_width);
        hs_draw(fb, gos->hs_table, hs_x, info_y, hs_width);
    }

    // Buttons
    button_draw(fb, &gos->restart_btn);
    button_draw(fb, &gos->exit_btn);
    if (gos->has_reset_scores) {
        button_draw(fb, &gos->reset_scores_btn);
    }
}
