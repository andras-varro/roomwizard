#include "game_common.h"
#include <string.h>
#include <stdio.h>

// Get current time in milliseconds
uint32_t get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// Initialize button
void button_init(Button *btn, int x, int y, int w, int h, const char *text,
                 uint32_t bg_color, uint32_t text_color, uint32_t highlight_color) {
    btn->x = x;
    btn->y = y;
    btn->width = w;
    btn->height = h;
    btn->text = text;
    btn->bg_color = bg_color;
    btn->text_color = text_color;
    btn->highlight_color = highlight_color;
    btn->state.was_pressed = false;
    btn->state.last_press_time_ms = 0;
    btn->state.debounce_ms = BTN_DEBOUNCE_MS;
    btn->state.is_highlighted = false;
}

// Check if touch is within button bounds
bool button_is_touched(Button *btn, int touch_x, int touch_y) {
    return (touch_x >= btn->x && touch_x < btn->x + btn->width &&
            touch_y >= btn->y && touch_y < btn->y + btn->height);
}

// Update button state with debouncing
bool button_check_press(Button *btn, bool currently_pressed, uint32_t current_time_ms) {
    btn->state.is_highlighted = currently_pressed;
    
    if (currently_pressed && !btn->state.was_pressed) {
        uint32_t time_since_last = current_time_ms - btn->state.last_press_time_ms;
        if (time_since_last > btn->state.debounce_ms) {
            btn->state.was_pressed = true;
            btn->state.last_press_time_ms = current_time_ms;
            return true;  // Button was pressed
        }
    } else if (!currently_pressed) {
        btn->state.was_pressed = false;
    }
    
    return false;  // Button not pressed or debounced
}

// Convert text to uppercase for font compatibility
static void to_uppercase(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] && i < max_len - 1) {
        dest[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
        i++;
    }
    dest[i] = '\0';
}

// Draw button with current state (highlighted if pressed)
void button_draw(Framebuffer *fb, Button *btn) {
    // Choose color based on state
    uint32_t bg = btn->state.is_highlighted ? btn->highlight_color : btn->bg_color;
    
    // Draw button background
    fb_fill_rect(fb, btn->x, btn->y, btn->width, btn->height, bg);
    
    // Draw button border (double border for depth)
    fb_draw_rect(fb, btn->x, btn->y, btn->width, btn->height, COLOR_WHITE);
    fb_draw_rect(fb, btn->x + 1, btn->y + 1, btn->width - 2, btn->height - 2, COLOR_WHITE);
    
    // Draw text centered
    if (btn->text && btn->text[0]) {
        char upper_text[256];
        to_uppercase(upper_text, btn->text, sizeof(upper_text));
        
        int text_len = strlen(upper_text);
        int scale = 2;  // Default scale
        
        // Adjust scale based on button size
        if (btn->width > 150) scale = 3;
        
        int text_width = text_len * 6 * scale;
        int text_x = btn->x + (btn->width - text_width) / 2;
        int text_y = btn->y + (btn->height - 7 * scale) / 2;
        
        fb_draw_text(fb, text_x, text_y, upper_text, btn->text_color, scale);
    }
}

// Draw menu button (hamburger icon)
void draw_menu_button(Framebuffer *fb, Button *btn) {
    // Choose color based on state
    uint32_t bg = btn->state.is_highlighted ? btn->highlight_color : btn->bg_color;
    
    // Draw button background
    fb_fill_rect(fb, btn->x, btn->y, btn->width, btn->height, bg);
    
    // Draw button border
    fb_draw_rect(fb, btn->x, btn->y, btn->width, btn->height, COLOR_WHITE);
    fb_draw_rect(fb, btn->x + 1, btn->y + 1, btn->width - 2, btn->height - 2, COLOR_WHITE);
    
    // Draw hamburger icon (three horizontal lines)
    int icon_width = 40;
    int icon_height = 4;
    int icon_spacing = 8;
    int icon_x = btn->x + (btn->width - icon_width) / 2;
    int icon_y = btn->y + (btn->height - (3 * icon_height + 2 * icon_spacing)) / 2;
    
    fb_fill_rect(fb, icon_x, icon_y, icon_width, icon_height, COLOR_WHITE);
    fb_fill_rect(fb, icon_x, icon_y + icon_height + icon_spacing, icon_width, icon_height, COLOR_WHITE);
    fb_fill_rect(fb, icon_x, icon_y + 2 * (icon_height + icon_spacing), icon_width, icon_height, COLOR_WHITE);
}

// Draw exit button (X icon)
void draw_exit_button(Framebuffer *fb, Button *btn) {
    // Choose color based on state
    uint32_t bg = btn->state.is_highlighted ? btn->highlight_color : btn->bg_color;
    
    // Draw button background
    fb_fill_rect(fb, btn->x, btn->y, btn->width, btn->height, bg);
    
    // Draw button border
    fb_draw_rect(fb, btn->x, btn->y, btn->width, btn->height, COLOR_WHITE);
    fb_draw_rect(fb, btn->x + 1, btn->y + 1, btn->width - 2, btn->height - 2, COLOR_WHITE);
    
    // Draw X icon (two diagonal lines)
    int icon_size = 30;
    int icon_x = btn->x + (btn->width - icon_size) / 2;
    int icon_y = btn->y + (btn->height - icon_size) / 2;
    int thickness = 4;
    
    // Draw X using thick lines
    for (int i = 0; i < icon_size; i++) {
        for (int t = 0; t < thickness; t++) {
            // Top-left to bottom-right
            fb_draw_pixel(fb, icon_x + i, icon_y + i + t, COLOR_WHITE);
            // Top-right to bottom-left
            fb_draw_pixel(fb, icon_x + icon_size - i, icon_y + i + t, COLOR_WHITE);
        }
    }
}

// Draw welcome screen with title and start button
void draw_welcome_screen(Framebuffer *fb, const char *game_title, 
                        const char *instructions, Button *start_btn) {
    fb_clear(fb, COLOR_BLACK);
    
    // Draw title
    char upper_title[256];
    to_uppercase(upper_title, game_title, sizeof(upper_title));
    int title_x = fb->width / 2 - (strlen(upper_title) * 6 * 4) / 2;
    fb_draw_text(fb, title_x, 80, upper_title, COLOR_CYAN, 4);
    
    // Draw instructions
    if (instructions && instructions[0]) {
        char upper_inst[512];
        to_uppercase(upper_inst, instructions, sizeof(upper_inst));
        int inst_x = fb->width / 2 - (strlen(upper_inst) * 6) / 2;
        fb_draw_text(fb, inst_x, 200, upper_inst, COLOR_WHITE, 1);
    }
    
    // Draw start button
    button_draw(fb, start_btn);
}

// Draw game over screen with score and restart button
void draw_game_over_screen(Framebuffer *fb, const char *message, int score,
                          Button *restart_btn) {
    fb_clear(fb, COLOR_BLACK);
    
    // Draw message
    char upper_msg[256];
    to_uppercase(upper_msg, message, sizeof(upper_msg));
    int msg_x = fb->width / 2 - (strlen(upper_msg) * 6 * 3) / 2;
    fb_draw_text(fb, msg_x, fb->height / 3, upper_msg, COLOR_RED, 3);
    
    // Draw score
    char score_text[64];
    snprintf(score_text, sizeof(score_text), "SCORE: %d", score);
    int score_x = fb->width / 2 - (strlen(score_text) * 6 * 2) / 2;
    fb_draw_text(fb, score_x, fb->height / 2 - 30, score_text, COLOR_WHITE, 2);
    
    // Draw restart button
    button_draw(fb, restart_btn);
}

// Draw pause screen
void draw_pause_screen(Framebuffer *fb, Button *resume_btn) {
    // Draw semi-transparent overlay (just draw text for now)
    int pause_x = fb->width / 2 - 60;
    fb_draw_text(fb, pause_x, fb->height / 3, "PAUSED", COLOR_CYAN, 3);
    
    // Draw resume button
    button_draw(fb, resume_btn);
}
