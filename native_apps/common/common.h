/**
 * Common UI Library for RoomWizard
 * 
 * Unified button system combining best features from game_common and ui_button.
 * Provides text truncation, auto-sizing, icon support, and screen templates.
 */

#ifndef COMMON_H
#define COMMON_H

#include "framebuffer.h"
#include "touch_input.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

// ============================================================================
// BUTTON SYSTEM
// ============================================================================

// Button visual states
typedef enum {
    BTN_STATE_NORMAL,
    BTN_STATE_HIGHLIGHTED,
    BTN_STATE_PRESSED
} ButtonVisualState;

// Button structure
typedef struct {
    // Position and size
    int x, y, width, height;
    
    // Text (with truncation support)
    char text[128];
    int max_text_width;  // 0 = no limit, >0 = truncate with "..."
    
    // Colors
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t highlight_color;
    uint32_t border_color;
    
    // Styling
    int text_scale;
    int border_width;
    
    // State management
    ButtonVisualState visual_state;
    bool was_pressed;
    uint32_t last_press_time_ms;
    uint32_t debounce_ms;
    
    // Optional icon callback
    void (*draw_icon)(Framebuffer *fb, int x, int y, int size, uint32_t color);
} Button;

// ============================================================================
// BUTTON MANAGEMENT
// ============================================================================

// Initialize button with all parameters
void button_init_full(Button *btn, int x, int y, int width, int height,
                      const char *text, uint32_t bg_color, uint32_t text_color,
                      uint32_t highlight_color, int text_scale);

// Initialize with defaults (common use case)
void button_init_simple(Button *btn, int x, int y, int width, int height,
                        const char *text);

// Backward compatibility macro - auto-determines text scale
#define button_init(btn, x, y, w, h, text, bg, txt, hl) \
    button_init_full(btn, x, y, w, h, text, bg, txt, hl, ((w) > 150) ? 3 : 2)

// Set text with automatic truncation if max_width exceeded
void button_set_text(Button *btn, const char *text);

// Set max text width (0 = no limit)
void button_set_max_text_width(Button *btn, int max_width);

// Set colors
void button_set_colors(Button *btn, uint32_t bg, uint32_t text, uint32_t highlight);

// Set border
void button_set_border(Button *btn, uint32_t color, int width);

// Set debounce time
void button_set_debounce(Button *btn, uint32_t ms);

// Set custom icon drawer
void button_set_icon(Button *btn, void (*draw_icon)(Framebuffer*, int, int, int, uint32_t));

// ============================================================================
// TOUCH HANDLING
// ============================================================================

// Check if touch is within button bounds
bool button_is_touched(Button *btn, int touch_x, int touch_y);

// Update button state with touch (returns true if pressed)
bool button_update(Button *btn, int touch_x, int touch_y, bool is_touching, uint32_t current_time_ms);

// Legacy API for compatibility (used by games)
bool button_check_press(Button *btn, bool currently_pressed, uint32_t current_time_ms);

// ============================================================================
// RENDERING
// ============================================================================

// Draw button with current state
void button_draw(Framebuffer *fb, Button *btn);

// Draw button with hamburger menu icon
void button_draw_menu(Framebuffer *fb, Button *btn);

// Draw button with X exit icon
void button_draw_exit(Framebuffer *fb, Button *btn);

// Backward compatibility aliases for old game_common API
#define draw_menu_button button_draw_menu
#define draw_exit_button button_draw_exit
#define draw_welcome_screen screen_draw_welcome
#define draw_game_over_screen screen_draw_game_over
#define draw_pause_screen screen_draw_pause

// ============================================================================
// TEXT UTILITIES
// ============================================================================

// Measure text width (8 pixels per character * scale)
int text_measure_width(const char *text, int scale);

// Measure text height (8 pixels * scale)
int text_measure_height(int scale);

// Draw centered text
void text_draw_centered(Framebuffer *fb, int center_x, int center_y,
                       const char *text, uint32_t color, int scale);

// Truncate text to fit width with ellipsis
void text_truncate(char *dest, const char *src, int max_width, int scale);

// Convert text to uppercase (for font compatibility)
void text_to_uppercase(char *dest, const char *src, size_t max_len);

// ============================================================================
// BUTTON AUTO-SIZING
// ============================================================================

// Calculate minimum width needed for text
int button_calc_min_width(const char *text, int scale, int padding);

// Auto-size button to fit text
void button_auto_size(Button *btn, int padding);

// ============================================================================
// SCREEN TEMPLATES
// ============================================================================

// Draw welcome screen with title and start button
void screen_draw_welcome(Framebuffer *fb, const char *game_title,
                        const char *instructions, Button *start_btn);

// Draw game over screen with score and restart button
void screen_draw_game_over(Framebuffer *fb, const char *message, int score,
                          Button *restart_btn);

// Draw pause screen with resume and exit buttons
void screen_draw_pause(Framebuffer *fb, Button *resume_btn, Button *exit_btn);

// ============================================================================
// ICON DRAWING HELPERS
// ============================================================================

// Draw hamburger menu icon (three horizontal lines)
void icon_draw_hamburger(Framebuffer *fb, int x, int y, int size, uint32_t color);

// Draw X icon (two diagonal lines)
void icon_draw_x(Framebuffer *fb, int x, int y, int size, uint32_t color);

// ============================================================================
// TIME UTILITIES
// ============================================================================

// Get current time in milliseconds
uint32_t get_time_ms(void);

// ============================================================================
// COLOR DEFINITIONS
// ============================================================================

// Common colors
#define COLOR_BLACK         RGB(0, 0, 0)
#define COLOR_WHITE         RGB(255, 255, 255)
#define COLOR_RED           RGB(255, 0, 0)
#define COLOR_GREEN         RGB(0, 255, 0)
#define COLOR_BLUE          RGB(0, 0, 255)
#define COLOR_CYAN          RGB(0, 255, 255)
#define COLOR_MAGENTA       RGB(255, 0, 255)
#define COLOR_YELLOW        RGB(255, 255, 0)

// Button colors
#define BTN_COLOR_PRIMARY       RGB(0, 150, 0)      // Green
#define BTN_COLOR_SECONDARY     RGB(100, 100, 100)  // Gray
#define BTN_COLOR_DANGER        RGB(200, 0, 0)      // Red
#define BTN_COLOR_WARNING       RGB(255, 165, 0)    // Orange
#define BTN_COLOR_INFO          RGB(0, 150, 200)    // Cyan
#define BTN_COLOR_HIGHLIGHT     RGB(255, 255, 100)  // Yellow

// Legacy aliases for compatibility with existing games
#define BTN_MENU_COLOR          BTN_COLOR_WARNING
#define BTN_EXIT_COLOR          BTN_COLOR_DANGER
#define BTN_START_COLOR         BTN_COLOR_PRIMARY
#define BTN_RESTART_COLOR       BTN_COLOR_PRIMARY
#define BTN_RESUME_COLOR        BTN_COLOR_PRIMARY
#define BTN_HIGHLIGHT_COLOR     BTN_COLOR_HIGHLIGHT

// Common button sizes
#define BTN_MENU_WIDTH  70
#define BTN_MENU_HEIGHT 50
#define BTN_EXIT_WIDTH  70
#define BTN_EXIT_HEIGHT 50
#define BTN_LARGE_WIDTH  220
#define BTN_LARGE_HEIGHT 60

// Debounce time in milliseconds
#define BTN_DEBOUNCE_MS 200

// ============================================================================
// LAYOUT HELPERS (using safe area constraints)
// ============================================================================

// Calculate centered X position within safe area
#define LAYOUT_CENTER_X(width) (SCREEN_SAFE_LEFT + (SCREEN_SAFE_WIDTH - (width)) / 2)

// Calculate centered Y position within safe area
#define LAYOUT_CENTER_Y(height) (SCREEN_SAFE_TOP + (SCREEN_SAFE_HEIGHT - (height)) / 2)

// Standard positions for common UI elements
#define LAYOUT_TITLE_Y          (SCREEN_SAFE_TOP + 20)      // Title text position
#define LAYOUT_MENU_BTN_X       (SCREEN_SAFE_LEFT + 10)     // Menu button (top-left)
#define LAYOUT_MENU_BTN_Y       (SCREEN_SAFE_TOP + 10)
#define LAYOUT_EXIT_BTN_X       (SCREEN_SAFE_RIGHT - BTN_EXIT_WIDTH - 10)  // Exit button (top-right)
#define LAYOUT_EXIT_BTN_Y       (SCREEN_SAFE_TOP + 10)
#define LAYOUT_BOTTOM_BTN_Y     (SCREEN_SAFE_BOTTOM - BTN_LARGE_HEIGHT - 20)  // Bottom buttons

#endif // COMMON_H
