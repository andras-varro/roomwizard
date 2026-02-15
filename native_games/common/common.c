/**
 * Common UI Library Implementation for RoomWizard
 */

#include "common.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// ============================================================================
// TIME UTILITIES
// ============================================================================

uint32_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
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
    // Update visual state
    if (currently_pressed) {
        btn->visual_state = BTN_STATE_HIGHLIGHTED;
    } else {
        btn->visual_state = BTN_STATE_NORMAL;
    }
    
    // Check for press with debouncing
    if (currently_pressed && !btn->was_pressed) {
        uint32_t time_since_last = current_time_ms - btn->last_press_time_ms;
        if (time_since_last > btn->debounce_ms) {
            btn->was_pressed = true;
            btn->last_press_time_ms = current_time_ms;
            return true;  // Button was pressed
        }
    } else if (!currently_pressed) {
        btn->was_pressed = false;
    }
    
    return false;  // Button not pressed or debounced
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

void screen_draw_pause(Framebuffer *fb, Button *resume_btn, Button *exit_btn) {
    // Draw pause text (centered in safe area)
    int pause_width = 6 * 8 * 3;  // "PAUSED" = 6 chars
    int pause_x = LAYOUT_CENTER_X(pause_width);
    int pause_y = SCREEN_SAFE_TOP + (SCREEN_SAFE_HEIGHT / 3);
    fb_draw_text(fb, pause_x, pause_y, "PAUSED", COLOR_CYAN, 3);
    
    // Draw buttons
    button_draw(fb, resume_btn);
    button_draw(fb, exit_btn);
}
