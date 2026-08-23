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
#include "highscore.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

/* --- Frame timing constants for dirty-flag rendering pattern --- */
#define FRAME_DELAY_ACTIVE_US  33333   /* ~30 fps when actively rendering */
#define FRAME_DELAY_IDLE_US   100000   /* ~10 fps polling when idle       */

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

// "Was this button just tapped?" — call EVERY frame, quiet frames included.
// Prefer this over button_check_press() at a touch call site: the
//   if (button_is_touched(...) && button_check_press(..., true, now))
// shape never passes false, so button_check_press()'s latch is never cleared and
// the button fires exactly once per process (see common.c, and
// tests/button_latch_test.c).
bool button_check_tap(Button *btn, const TouchState *ts, uint32_t current_time_ms);

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
#define draw_welcome_screen_warn screen_draw_welcome_warn
#define draw_game_over_screen screen_draw_game_over

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

// Draw welcome screen with title and start button.
//
// `instructions` may contain '\n'; each line is measured and centred
// individually (fb_draw_text does NOT interpret '\n' — passing a multi-line
// string straight to it renders one long line and mis-centres it).
//
// The function also *positions* `start_btn`: it is laid out below the measured
// instruction block, centred in the safe area and clamped to it, so the drawn
// rectangle and the hit-test rectangle can never disagree.  Callers no longer
// need to pick welcome-screen coordinates in button_init().
void screen_draw_welcome(Framebuffer *fb, const char *game_title,
                        const char *instructions, Button *start_btn);

// As screen_draw_welcome(), plus an optional amber `warning` block drawn below
// the instructions (also '\n'-splittable).  Pass NULL for no warning; then this
// is exactly screen_draw_welcome().  Used to tell the player that the game
// needs a USB keyboard or gamepad and none is connected.
void screen_draw_welcome_warn(Framebuffer *fb, const char *game_title,
                             const char *instructions, const char *warning,
                             Button *start_btn);

// Draw game over screen with score and restart button
void screen_draw_game_over(Framebuffer *fb, const char *message, int score,
                          Button *restart_btn);


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

// Welcome-screen text metrics (screen_draw_welcome*): instruction/warning text
// scale, the gap between wrapped lines, and the gap between blocks (title,
// instructions, warning, start button).
#define WELCOME_INST_SCALE  2
#define WELCOME_LINE_GAP    8
#define WELCOME_BLOCK_GAP   16

// ============================================================================
// SINGLETON INSTANCE LOCK
// ============================================================================

// Acquire an exclusive process lock via flock().  Returns a file descriptor
// (keep it open for the lifetime of the process) or -1 if another instance
// already holds the lock.  The lock is automatically released when the
// process exits or the fd is closed.
int acquire_instance_lock(const char *app_name);

// ============================================================================
// PER-FRAME SERVICE FOR BLOCKING SUB-LOOPS
// ============================================================================

/*
 * A BLOCKING SUB-LOOP IS A RENDER LOOP: it owes every per-frame service the
 * main loop owes.  keyboard_enter() and hs_drain_touches() each run their own
 * draw/poll/usleep loop while the game's loop is stopped, and until 2026-08-22
 * neither serviced the audio mix bus — so a `NEW HIGH SCORE!` keyboard froze a
 * fade and DEFERRED (not dropped) the game-over fanfare until it closed, which
 * the operator experiences as no game-over sound.  The mixer advances by frames
 * RENDERED, so a loop that renders nothing stops time for every voice.
 *
 * ⚠️ The slot holds ONE service and audio is its only registrant, deliberately:
 * the alternative was threading a `void (*tick)(void *)` through
 * keyboard_enter() → hs_enter_name() → gameover_init() and every game's call
 * site, where a new game silently opts out by forgetting it.  Registered by
 * `audio_open()` and cleared by `audio_close()` — never by a game — so this
 * header stays free of `audio.h` (the same separation `hw_led_pulse_update()`
 * keeps) and no app changed a line to get the fix.
 *
 * ⚠️ Any NEW loop that draws while the main loop is stopped owes a call to
 * ui_frame_service() beside its fb_swap().  `hw_pulse_led()` and
 * `hw_blink_led()` block too and do NOT call it: their waits are single long
 * usleeps rather than a loop, and (measured 2026-08-22) their only callers are
 * device_tools and hardware_test, neither of which runs a bed.
 */

// Install the per-frame service.  ctx is passed back verbatim.  fn == NULL
// clears the slot.
void ui_frame_service_set(void (*fn)(void *ctx), void *ctx);

// Run the installed service, if any.  Cheap and safe to call when nothing is
// registered; call it once per iteration of any loop that owns the screen.
void ui_frame_service(void);

// ============================================================================
// TOGGLE SWITCH CONTROL
// ============================================================================

typedef struct {
    int x, y;               // Position (top-left of track)
    int track_w, track_h;   // Track dimensions
    bool state;             // true = ON, false = OFF
    char label[64];         // Label text drawn to the right

    // Colors
    uint32_t on_color;      // Track color when ON
    uint32_t off_color;     // Track color when OFF
    uint32_t knob_color;    // Knob color
    uint32_t label_color;   // Label text color

    // State management
    bool was_pressed;
    uint32_t last_press_time_ms;
    uint32_t debounce_ms;
} ToggleSwitch;

// Initialize toggle switch with position, size, and label
void toggle_init(ToggleSwitch *sw, int x, int y, int track_w, int track_h,
                 const char *label, bool initial_state);

// Set colors (defaults: green ON, grey OFF, white knob, white label)
void toggle_set_colors(ToggleSwitch *sw, uint32_t on_color, uint32_t off_color,
                       uint32_t knob_color, uint32_t label_color);

// Check touch and toggle state. Returns true if state CHANGED this frame.
bool toggle_check_press(ToggleSwitch *sw, int touch_x, int touch_y,
                        bool is_pressed, uint32_t current_time_ms);

// Draw the toggle switch
void toggle_draw(Framebuffer *fb, ToggleSwitch *sw);

// ============================================================================
// UNIFIED GAME OVER SCREEN
// ============================================================================

typedef enum {
    GAMEOVER_STATE_CHECK,       // Initial: check if score qualifies for highscore
    GAMEOVER_STATE_NAME_ENTRY,  // Blocking name entry in progress
    GAMEOVER_STATE_DISPLAY,     // Show game over screen with scores/buttons
} GameOverState;

typedef enum {
    GAMEOVER_ACTION_NONE,
    GAMEOVER_ACTION_RESTART,
    GAMEOVER_ACTION_EXIT,
    GAMEOVER_ACTION_RESET_SCORES,
} GameOverAction;

typedef struct {
    GameOverState state;
    int score;
    char title[64];           // Custom title or auto-set based on highscore
    char info_line[64];       // Optional info (e.g., "LEVEL 5")
    HighScoreTable *hs_table; // NULL if no highscore support
    TouchInput *touch;        // Required for blocking hs_enter_name()
    bool hs_qualifies;        // Set during CHECK state
    Button restart_btn;
    Button exit_btn;
    Button reset_scores_btn;
    bool has_reset_scores;    // Show reset scores button?
    bool pending_draw;        // "I owe the screen a frame" — see gameover_needs_redraw()
    bool armed;               // Overlay has been on screen a frame; input allowed
} GameOverScreen;

// Initialize the game over screen. Call once when entering game-over state.
// hs_table can be NULL for games without highscore (like Pong simple mode).
// touch is required when hs_table is non-NULL (used for blocking name entry).
void gameover_init(GameOverScreen *gos, Framebuffer *fb,
                   int score, const char *title, const char *info_line,
                   HighScoreTable *hs_table, TouchInput *touch);

// Process the game over screen (handles name entry flow, renders, checks buttons).
// Returns the action taken (NONE if no button pressed yet).
GameOverAction gameover_update(GameOverScreen *gos, Framebuffer *fb,
                               int touch_x, int touch_y, bool touch_active);

// True while the component still owes the screen a frame, OR while there is
// touch input it has not been given a chance to act on.
//
// gameover_update() is a multi-frame state machine — it checks the highscore
// table, may run the blocking name-entry keyboard, and only draws once it
// reaches DISPLAY — but every game calls it from inside its *draw* function,
// which a dirty-flagged main loop runs only when needs_redraw is set. A loop
// that computes that flag without asking the component starves it: the overlay
// never appears until the player taps something. So OR this into the flag:
//
//   if (current_screen == SCREEN_GAME_OVER && gameover_needs_redraw(&gos))
//       needs_redraw = true;
//
// The input half matters just as much as the draw half: because our buttons are
// read inside the draw path, a frame the loop declines to run is also an input
// event we never see, and a fired button needs a not-touched frame before it can
// fire again. So this reports draw-pending OR input-pending OR re-arm-pending. Do
// not assume the caller has its own "redraw on input activity" branch — samegame
// did not, and its game-over buttons were dead.
bool gameover_needs_redraw(const GameOverScreen *gos);

// Draw the game over screen overlay (called each frame from gameover_update,
// but may also be called directly if needed).
void gameover_draw(GameOverScreen *gos, Framebuffer *fb);

// ============================================================================
// MODAL DIALOG
// Reusable modal overlay with title, message, and 1–4 configurable buttons.
// Follows the GameOverScreen lifecycle pattern (init/update/draw).
// ============================================================================

#define MODAL_MAX_BUTTONS 4

typedef enum {
    MODAL_ACTION_NONE = -1,   /* No button pressed */
    MODAL_ACTION_BTN0 = 0,    /* First button pressed */
    MODAL_ACTION_BTN1 = 1,    /* Second button pressed */
    MODAL_ACTION_BTN2 = 2,    /* Third button pressed */
    MODAL_ACTION_BTN3 = 3     /* Fourth button pressed */
} ModalDialogAction;

typedef struct {
    bool active;
    char title[64];
    char message[128];        /* Optional description line */
    uint8_t overlay_alpha;    /* 0=no overlay, 160-180=typical */
    int dialog_width;
    int dialog_height;
    uint32_t bg_color;        /* Dialog background */
    uint32_t border_color;    /* Dialog border */
    uint32_t title_color;     /* Title text color */
    uint32_t message_color;   /* Message text color */
    int button_count;         /* 1–4 */
    Button buttons[MODAL_MAX_BUTTONS];
} ModalDialog;

// Initialize modal dialog with title, message, and button count (1–4).
// After calling, configure each button with modal_dialog_set_button().
void modal_dialog_init(ModalDialog *dlg, const char *title, const char *message,
                       int button_count);

// Configure an individual button (index 0..button_count-1).
void modal_dialog_set_button(ModalDialog *dlg, int index, const char *text,
                             uint32_t bg_color, uint32_t text_color);

// Convenience: init a 2-button confirm/cancel dialog (wraps init + set_button).
void modal_dialog_init_confirm(ModalDialog *dlg, const char *title,
                               const char *message,
                               const char *confirm_text, uint32_t confirm_color,
                               const char *cancel_text, uint32_t cancel_color);

// Show/hide the dialog
void modal_dialog_show(ModalDialog *dlg);
void modal_dialog_hide(ModalDialog *dlg);
bool modal_dialog_is_active(ModalDialog *dlg);

// Draw the dialog overlay and contents
void modal_dialog_draw(ModalDialog *dlg, Framebuffer *fb);

// Process touch input; returns MODAL_ACTION_NONE (-1) if no button pressed,
// or the button index (0–3) if pressed.  Auto-hides the dialog on press.
ModalDialogAction modal_dialog_update(ModalDialog *dlg, int touch_x, int touch_y,
                                       bool touch_active, uint32_t now_ms);

#endif // COMMON_H
