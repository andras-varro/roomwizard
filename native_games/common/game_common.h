#ifndef GAME_COMMON_H
#define GAME_COMMON_H

#include "framebuffer.h"
#include "touch_input.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

// Button state for debouncing
typedef struct {
    bool was_pressed;
    uint32_t last_press_time_ms;
    uint32_t debounce_ms;
    bool is_highlighted;  // Visual feedback
} ButtonState;

// Button definition
typedef struct {
    int x, y, width, height;
    const char *text;
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t highlight_color;
    ButtonState state;
} Button;

// Get current time in milliseconds
uint32_t get_time_ms();

// Initialize button
void button_init(Button *btn, int x, int y, int w, int h, const char *text,
                 uint32_t bg_color, uint32_t text_color, uint32_t highlight_color);

// Check if touch is within button bounds
bool button_is_touched(Button *btn, int touch_x, int touch_y);

// Update button state with debouncing
bool button_check_press(Button *btn, bool currently_pressed, uint32_t current_time_ms);

// Draw button with current state (highlighted if pressed)
void button_draw(Framebuffer *fb, Button *btn);

// Draw menu button (hamburger icon or text)
void draw_menu_button(Framebuffer *fb, Button *btn);

// Draw exit button (X icon)
void draw_exit_button(Framebuffer *fb, Button *btn);

// Draw welcome screen with title and start button
void draw_welcome_screen(Framebuffer *fb, const char *game_title, 
                        const char *instructions, Button *start_btn);

// Draw game over screen with score and restart button
void draw_game_over_screen(Framebuffer *fb, const char *message, int score,
                          Button *restart_btn);

// Draw pause screen
void draw_pause_screen(Framebuffer *fb, Button *resume_btn);

// Common button colors
#define BTN_MENU_COLOR      RGB(255, 165, 0)  // Orange
#define BTN_EXIT_COLOR      RGB(200, 0, 0)    // Dark red
#define BTN_START_COLOR     RGB(0, 200, 0)    // Green
#define BTN_RESTART_COLOR   RGB(0, 200, 0)    // Green
#define BTN_RESUME_COLOR    RGB(0, 200, 0)    // Green
#define BTN_HIGHLIGHT_COLOR RGB(255, 255, 100) // Yellow highlight

// Common button sizes
#define BTN_MENU_WIDTH  70
#define BTN_MENU_HEIGHT 50
#define BTN_EXIT_WIDTH  70
#define BTN_EXIT_HEIGHT 50
#define BTN_LARGE_WIDTH  200
#define BTN_LARGE_HEIGHT 60

// Debounce time in milliseconds
#define BTN_DEBOUNCE_MS 200

#endif
