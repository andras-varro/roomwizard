/*
 * Tetris Game - Native C Implementation
 * Optimized for 300MHz ARM with touchscreen
 * Touch controls: Tap left/right to move, tap center to rotate, swipe down to drop
 * Supports keyboard, gamepad, and touch input via unified gamepad module
 * Keyboard/gamepad features DAS (Delayed Auto Shift) for left/right movement
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include "../common/framebuffer.h"
#include "../common/touch_input.h"
#include "../common/common.h"
#include "../common/hardware.h"
#include "../common/highscore.h"
#include "../common/audio.h"
#include "../common/gamepad.h"

#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20
#define NUM_TETROMINOS 7

typedef enum {
    SCREEN_WELCOME,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_GAME_OVER
} GameScreen;

typedef enum {
    PIECE_I, PIECE_O, PIECE_T, PIECE_S, PIECE_Z, PIECE_J, PIECE_L
} PieceType;

typedef struct {
    int x, y;
    PieceType type;
    int rotation;
} Piece;

typedef struct {
    int board[BOARD_HEIGHT][BOARD_WIDTH];
    Piece current;
    Piece next;
    int score;
    int high_score;
    int lines_cleared;
    int level;
    bool game_over;
    bool paused;
    int drop_speed;
    int drop_counter;
} GameState;

// Tetromino shapes (4 rotations each)
const int tetrominos[NUM_TETROMINOS][4][4][4] = {
    // I piece
    {
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
        {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}
    },
    // O piece
    {
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
    },
    // T piece
    {
        {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    // S piece
    {
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
        {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    // Z piece
    {
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}
    },
    // J piece
    {
        {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}
    },
    // L piece
    {
        {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}
    }
};

const uint32_t piece_colors[NUM_TETROMINOS] = {
    RGB(0, 255, 255),   // I - Cyan
    RGB(255, 255, 0),   // O - Yellow
    RGB(128, 0, 128),   // T - Purple
    RGB(0, 255, 0),     // S - Green
    RGB(255, 0, 0),     // Z - Red
    RGB(0, 0, 255),     // J - Blue
    RGB(255, 165, 0)    // L - Orange
};

/* DAS (Delayed Auto Shift) constants */
#define DAS_INITIAL_DELAY_MS 200   /* ms before auto-repeat starts */
#define DAS_REPEAT_INTERVAL_MS 50  /* ms between auto-repeat moves */

/* DAS state for a single direction */
typedef struct {
    bool active;           /* Currently in DAS for this direction */
    uint32_t hold_start;   /* Timestamp when key was first held */
    uint32_t last_repeat;  /* Timestamp of last auto-repeat fire */
} DASState;

// Global variables
Framebuffer fb;
TouchInput touch;
GamepadManager gamepad;
InputState input;
GameState game;
int cell_size;
int board_offset_x;
int board_offset_y;
bool running = true;
bool portrait_mode = false;
int last_touch_x = -1;
int last_touch_y = -1;
Button menu_button;
Button exit_button;
Button start_button;
ModalDialog pause_dialog;
GameScreen current_screen = SCREEN_WELCOME;
HighScoreTable hs_table;
static GameOverScreen gos;
Audio audio;
static uint32_t last_rescan_ms = 0;
#define RESCAN_INTERVAL_MS 5000
static DASState das_left  = {false, 0, 0};
static DASState das_right = {false, 0, 0};
static bool soft_drop_active = false;

// Function prototypes
void init_game();
void reset_game();
void spawn_piece(Piece *piece);
bool check_collision(Piece *piece, int dx, int dy, int new_rotation);
void lock_piece();
void clear_lines();
void update_game();
void draw_playing_field();
void draw_game();
void handle_input();
void signal_handler(int sig);

void signal_handler(int sig) {
    running = false;
}

void init_game() {
    portrait_mode = fb.portrait_mode;
    
    // Calculate board dimensions using safe area bounds
    int hud_height = 55;  // Space for score/level/next piece at top
    int available_h = SCREEN_SAFE_HEIGHT - hud_height;
    
    cell_size = available_h / BOARD_HEIGHT;
    
    // In portrait mode, allow larger cells (up to what fits); in landscape cap at 30
    if (!portrait_mode && cell_size > 30) cell_size = 30;
    
    int board_h = BOARD_HEIGHT * cell_size;
    int board_w = BOARD_WIDTH * cell_size;
    
    // Center board horizontally in safe area
    board_offset_x = SCREEN_SAFE_LEFT + (SCREEN_SAFE_WIDTH - board_w) / 2;
    
    // Center board vertically in available space below HUD
    board_offset_y = SCREEN_SAFE_TOP + hud_height + (available_h - board_h) / 2;
    
    hs_init(&hs_table, "tetris");
    hs_load(&hs_table);
    game.high_score = hs_table.count > 0 ? hs_table.entries[0].score : 0;

    // Initialize buttons
    button_init(&menu_button, 10, 10, BTN_MENU_WIDTH, BTN_MENU_HEIGHT, "",
                BTN_MENU_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&exit_button, fb.width - BTN_EXIT_WIDTH - 10, 10, 
                BTN_EXIT_WIDTH, BTN_EXIT_HEIGHT, "",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&start_button, fb.width / 2 - BTN_LARGE_WIDTH / 2, 
                fb.height / 2 + 40, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "TAP TO START",
                BTN_START_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    modal_dialog_init(&pause_dialog, "PAUSED", NULL, 2);
    modal_dialog_set_button(&pause_dialog, 0, "RESUME", BTN_COLOR_PRIMARY, COLOR_WHITE);
    modal_dialog_set_button(&pause_dialog, 1, "EXIT", BTN_COLOR_DANGER, COLOR_WHITE);
    
    reset_game();
}

void reset_game() {
    memset(game.board, 0, sizeof(game.board));
    game.score = 0;
    game.lines_cleared = 0;
    game.level = 1;
    game.game_over = false;
    game.paused = false;
    game.drop_speed = 60;  // Frames per drop
    game.drop_counter = 0;
    
    spawn_piece(&game.current);
    spawn_piece(&game.next);
}

void spawn_piece(Piece *piece) {
    piece->type = rand() % NUM_TETROMINOS;
    piece->rotation = 0;
    piece->x = BOARD_WIDTH / 2 - 2;
    piece->y = 0;
}

bool check_collision(Piece *piece, int dx, int dy, int new_rotation) {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (tetrominos[piece->type][new_rotation][y][x]) {
                int board_x = piece->x + x + dx;
                int board_y = piece->y + y + dy;
                
                if (board_x < 0 || board_x >= BOARD_WIDTH || 
                    board_y >= BOARD_HEIGHT) {
                    return true;
                }
                
                if (board_y >= 0 && game.board[board_y][board_x]) {
                    return true;
                }
            }
        }
    }
    return false;
}

void lock_piece() {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (tetrominos[game.current.type][game.current.rotation][y][x]) {
                int board_y = game.current.y + y;
                int board_x = game.current.x + x;
                if (board_y >= 0 && board_y < BOARD_HEIGHT) {
                    game.board[board_y][board_x] = game.current.type + 1;
                }
            }
        }
    }
    
    clear_lines();
    
    game.current = game.next;
    spawn_piece(&game.next);
    
    if (check_collision(&game.current, 0, 0, game.current.rotation)) {
        game.game_over = true;
        current_screen = SCREEN_GAME_OVER;
        // Initialize unified game over screen with level info
        char info_line[64];
        snprintf(info_line, sizeof(info_line), "LEVEL %d", game.level);
        gameover_init(&gos, &fb, game.score, NULL, info_line, &hs_table, &touch);
        // Red pulse + fail sound
        for (int i = 0; i < 3; i++) {
            hw_set_led(LED_RED, 100);
            usleep(200000);
            hw_leds_off();
            usleep(200000);
        }
        audio_fail(&audio);  // Descending game-over tone (~600ms)
    }
}

void clear_lines() {
    int lines = 0;
    
    for (int y = BOARD_HEIGHT - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (game.board[y][x] == 0) {
                full = false;
                break;
            }
        }
        
        if (full) {
            lines++;
            // Move all lines above down
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < BOARD_WIDTH; x++) {
                    game.board[yy][x] = game.board[yy - 1][x];
                }
            }
            // Clear top line
            for (int x = 0; x < BOARD_WIDTH; x++) {
                game.board[0][x] = 0;
            }
            y++; // Check this line again
        }
    }
    
    if (lines > 0) {
        game.lines_cleared += lines;
        game.score += (lines * lines * 100);  // Bonus for multiple lines
        game.level = 1 + game.lines_cleared / 10;
        game.drop_speed = 60 - (game.level * 3);
        if (game.drop_speed < 10) game.drop_speed = 10;
        
        // LED + audio effects for line clears
        if (lines == 4) {
            // Tetris! Fanfare + yellow flash
            hw_set_leds(HW_LED_COLOR_YELLOW);
            audio_success(&audio);  // Ascending arpeggio (~440ms)
            hw_leds_off();
        } else if (lines >= 2) {
            // Multi-line clear
            hw_set_led(LED_GREEN, 100);
            audio_blip(&audio);     // Short blip (~60ms)
            hw_leds_off();
        } else {
            // Single line
            hw_set_led(LED_GREEN, 100);
            audio_blip(&audio);     // Short blip (~60ms)
            hw_leds_off();
        }
    }
}

void update_game() {
    // Only process game logic when actually playing (Issue #9)
    if (current_screen != SCREEN_PLAYING) return;
    if (game.game_over || game.paused) return;
    
    game.drop_counter++;

    // Soft drop: when BTN_DOWN is held, drop faster (every 3 frames instead of normal speed)
    int effective_speed = soft_drop_active ? 3 : game.drop_speed;

    if (game.drop_counter >= effective_speed) {
        game.drop_counter = 0;
        
        if (!check_collision(&game.current, 0, 1, game.current.rotation)) {
            game.current.y++;
            if (soft_drop_active)
                game.score += 1;  // Soft drop bonus
        } else {
            lock_piece();
        }
    }
}

/* Helper: process DAS for a direction. Returns true if a move should fire this frame. */
static bool das_update(DASState *das, bool held, bool pressed, uint32_t now) {
    if (pressed) {
        /* First press — always fire, start DAS timer */
        das->active = true;
        das->hold_start = now;
        das->last_repeat = now;
        return true;
    }
    if (!held) {
        /* Released — cancel DAS */
        das->active = false;
        return false;
    }
    /* Key is still held — check for auto-repeat */
    if (!das->active) return false;
    uint32_t hold_duration = now - das->hold_start;
    if (hold_duration >= DAS_INITIAL_DELAY_MS) {
        uint32_t since_repeat = now - das->last_repeat;
        if (since_repeat >= DAS_REPEAT_INTERVAL_MS) {
            das->last_repeat = now;
            return true;
        }
    }
    return false;
}

void handle_input() {
    touch_poll(&touch);
    TouchState state = touch_get_state(&touch);
    uint32_t current_time = get_time_ms();

    // Poll gamepad/keyboard/touch through unified API
    gamepad_poll(&gamepad, &input, state.x, state.y, state.pressed);

    // Periodic device rescan for hotplug support
    if (current_time - last_rescan_ms > RESCAN_INTERVAL_MS) {
        last_rescan_ms = current_time;
        gamepad_rescan(&gamepad);
    }

    // BTN_BACK always exits to launcher
    if (input.buttons[BTN_ID_BACK].pressed) {
        fb_fade_out(&fb);
        running = false;
        return;
    }

    // Handle welcome screen
    if (current_screen == SCREEN_WELCOME) {
        if (state.pressed) {
            bool touched = button_is_touched(&start_button, state.x, state.y);
            if (button_check_press(&start_button, touched, current_time)) {
                current_screen = SCREEN_PLAYING;
                hw_set_led(LED_GREEN, 100);
                usleep(100000);  // 100ms
                hw_leds_off();
            }
        }
        // Gamepad/keyboard: start game
        if (input.buttons[BTN_ID_JUMP].pressed ||
            input.buttons[BTN_ID_ACTION].pressed ||
            input.buttons[BTN_ID_PAUSE].pressed) {
            current_screen = SCREEN_PLAYING;
            hw_set_led(LED_GREEN, 100);
            usleep(100000);
            hw_leds_off();
        }
        return;
    }
    
    // Handle game over screen — gameover_update() manages buttons in draw phase
    if (current_screen == SCREEN_GAME_OVER) {
        // Allow gamepad restart
        if (input.buttons[BTN_ID_JUMP].pressed ||
            input.buttons[BTN_ID_ACTION].pressed) {
            reset_game();
            game.high_score = hs_table.count > 0 ? hs_table.entries[0].score : 0;
            current_screen = SCREEN_PLAYING;
        }
        return;
    }
    
    // Handle pause screen
    if (current_screen == SCREEN_PAUSED) {
        // Gamepad: unpause with Pause button
        if (input.buttons[BTN_ID_PAUSE].pressed) {
            current_screen = SCREEN_PLAYING;
            game.paused = false;
            return;
        }
        // Gamepad: resume with Jump/Action
        if (input.buttons[BTN_ID_JUMP].pressed ||
            input.buttons[BTN_ID_ACTION].pressed) {
            current_screen = SCREEN_PLAYING;
            game.paused = false;
            return;
        }
        ModalDialogAction action = modal_dialog_update(&pause_dialog,
            state.x, state.y, state.pressed, current_time);
        if (action == MODAL_ACTION_BTN0) {
            current_screen = SCREEN_PLAYING;
            game.paused = false;
            return;
        }
        if (action == MODAL_ACTION_BTN1) {
            // Fade out effect
            for (int i = 0; i < 3; i++) {
                hw_set_led(LED_RED, 100);
                usleep(100000);  // 100ms
                hw_leds_off();
                usleep(100000);  // 100ms
            }
            running = false;
            return;
        }
        return;
    }

    // Playing screen — gamepad/keyboard: pause
    if (input.buttons[BTN_ID_PAUSE].pressed) {
        current_screen = SCREEN_PAUSED;
        game.paused = true;
        modal_dialog_show(&pause_dialog);
        return;
    }

    // Playing screen - check menu and exit buttons (touch)
    if (state.pressed) {
        // Check exit button (top-right)
        bool exit_touched = button_is_touched(&exit_button, state.x, state.y);
        if (button_check_press(&exit_button, exit_touched, current_time)) {
            // Fade out effect
            for (int i = 0; i < 3; i++) {
                hw_set_led(LED_RED, 100);
                usleep(100000);  // 100ms
                hw_leds_off();
                usleep(100000);  // 100ms
            }
            running = false;
            return;
        }
        
        // Check menu button (top-left)
        bool menu_touched = button_is_touched(&menu_button, state.x, state.y);
        if (button_check_press(&menu_button, menu_touched, current_time)) {
            current_screen = SCREEN_PAUSED;
            game.paused = true;
            modal_dialog_show(&pause_dialog);
            return;
        }
    }
    
    // Touch game controls
    if (state.pressed && !game.game_over && !game.paused) {
        int tx = state.x;
        int ty = state.y;
        
        // Game controls
        int board_right = board_offset_x + BOARD_WIDTH * cell_size;
        
        if (ty > fb.height - 80) {
            // Bottom — hard drop (check this FIRST for both modes)
            while (!check_collision(&game.current, 0, 1, game.current.rotation)) {
                game.current.y++;
                game.score += 2;
            }
            audio_interrupt(&audio);
            audio_tone(&audio, 500, 60);
            audio_tone(&audio, 250, 70);
            lock_piece();
        } else if (portrait_mode) {
            // Portrait touch zones: wide center (60%) for rotation, narrow edges (20%) + margins for movement
            int board_width_px = BOARD_WIDTH * cell_size;
            int left_zone_edge = board_offset_x + board_width_px / 5;       // 20% from left board edge
            int right_zone_edge = board_offset_x + board_width_px * 4 / 5;  // 20% from right board edge
            
            if (tx < left_zone_edge) {
                // Left 20% of board or outside left edge — move left
                if (!check_collision(&game.current, -1, 0, game.current.rotation)) {
                    game.current.x--;
                    audio_interrupt(&audio);
                    audio_tone(&audio, 880, 60);
                }
            } else if (tx > right_zone_edge) {
                // Right 20% of board or outside right edge — move right
                if (!check_collision(&game.current, 1, 0, game.current.rotation)) {
                    game.current.x++;
                    audio_interrupt(&audio);
                    audio_tone(&audio, 880, 60);
                }
            } else {
                // Center 60% of board — rotate
                int new_rotation = (game.current.rotation + 1) % 4;
                if (!check_collision(&game.current, 0, 0, new_rotation)) {
                    game.current.rotation = new_rotation;
                }
            }
        } else {
            // Landscape touch zones (existing logic)
            if (tx < board_offset_x - 10) {
                // Left side — move left
                if (!check_collision(&game.current, -1, 0, game.current.rotation)) {
                    game.current.x--;
                    audio_interrupt(&audio);
                    audio_tone(&audio, 880, 60);
                }
            } else if (tx > board_right + 10) {
                // Right side — move right
                if (!check_collision(&game.current, 1, 0, game.current.rotation)) {
                    game.current.x++;
                    audio_interrupt(&audio);
                    audio_tone(&audio, 880, 60);
                }
            } else {
                // Center — rotate
                int new_rotation = (game.current.rotation + 1) % 4;
                if (!check_collision(&game.current, 0, 0, new_rotation)) {
                    game.current.rotation = new_rotation;
                }
            }
        }
        
        last_touch_x = tx;
        last_touch_y = ty;
    }

    // Gamepad/keyboard controls (only while playing and not paused)
    if (!game.game_over && !game.paused && current_screen == SCREEN_PLAYING) {
        // Left/Right with DAS (Delayed Auto Shift)
        if (das_update(&das_left, input.buttons[BTN_ID_LEFT].held,
                       input.buttons[BTN_ID_LEFT].pressed, current_time)) {
            if (!check_collision(&game.current, -1, 0, game.current.rotation)) {
                game.current.x--;
                audio_interrupt(&audio);
                audio_tone(&audio, 880, 60);
            }
        }
        if (das_update(&das_right, input.buttons[BTN_ID_RIGHT].held,
                       input.buttons[BTN_ID_RIGHT].pressed, current_time)) {
            if (!check_collision(&game.current, 1, 0, game.current.rotation)) {
                game.current.x++;
                audio_interrupt(&audio);
                audio_tone(&audio, 880, 60);
            }
        }

        // Rotate: BTN_UP or BTN_JUMP (pressed edge only)
        if (input.buttons[BTN_ID_UP].pressed ||
            input.buttons[BTN_ID_JUMP].pressed) {
            int new_rotation = (game.current.rotation + 1) % 4;
            if (!check_collision(&game.current, 0, 0, new_rotation)) {
                game.current.rotation = new_rotation;
            }
        }

        // Counter-clockwise rotate: BTN_RUN (optional)
        if (input.buttons[BTN_ID_RUN].pressed) {
            int new_rotation = (game.current.rotation + 3) % 4;
            if (!check_collision(&game.current, 0, 0, new_rotation)) {
                game.current.rotation = new_rotation;
            }
        }

        // Soft drop: BTN_DOWN held accelerates drop
        soft_drop_active = input.buttons[BTN_ID_DOWN].held;

        // Hard drop: BTN_ACTION (pressed edge only)
        if (input.buttons[BTN_ID_ACTION].pressed) {
            while (!check_collision(&game.current, 0, 1, game.current.rotation)) {
                game.current.y++;
                game.score += 2;
            }
            audio_interrupt(&audio);
            audio_tone(&audio, 500, 60);
            audio_tone(&audio, 250, 70);
            lock_piece();
        }
    }
}

// Draw the playing field (board, falling piece, HUD) — used as background
// for PLAYING, PAUSED, and GAME_OVER screens
void draw_playing_field() {
    // Draw HUD — centered text using text_draw_centered()
    int center_x = fb.width / 2;
    
    char score_text[32];
    snprintf(score_text, sizeof(score_text), "SCORE:%d", game.score);
    text_draw_centered(&fb, center_x, 15, score_text, COLOR_WHITE, 2);
    
    char level_text[32];
    snprintf(level_text, sizeof(level_text), "LVL:%d", game.level);
    text_draw_centered(&fb, center_x, 40, level_text, COLOR_CYAN, 2);
    
    // Draw menu and exit buttons
    draw_menu_button(&fb, &menu_button);
    draw_exit_button(&fb, &exit_button);
    
    // Draw board border
    fb_draw_rect(&fb, board_offset_x - 2, board_offset_y - 2,
                 BOARD_WIDTH * cell_size + 4, BOARD_HEIGHT * cell_size + 4, COLOR_WHITE);
    
    // Draw locked pieces
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (game.board[y][x]) {
                int px = board_offset_x + x * cell_size;
                int py = board_offset_y + y * cell_size;
                uint32_t color = piece_colors[game.board[y][x] - 1];
                fb_fill_rect(&fb, px + 1, py + 1, cell_size - 2, cell_size - 2, color);
            }
        }
    }
    
    // Draw current piece
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (tetrominos[game.current.type][game.current.rotation][y][x]) {
                int px = board_offset_x + (game.current.x + x) * cell_size;
                int py = board_offset_y + (game.current.y + y) * cell_size;
                uint32_t color = piece_colors[game.current.type];
                fb_fill_rect(&fb, px + 1, py + 1, cell_size - 2, cell_size - 2, color);
            }
        }
    }
    
    // Draw next piece preview
    if (portrait_mode) {
        // Portrait: draw NEXT preview to the left of the exit button (avoids overlap)
        int preview_size = cell_size / 3;
        if (preview_size < 6) preview_size = 6;
        int preview_block_w = 4 * preview_size;
        int next_x = exit_button.x - preview_block_w - 8;
        int next_y = exit_button.y + (BTN_EXIT_HEIGHT - 4 * preview_size) / 2;
        int label_w = text_measure_width("NEXT:", 1);
        fb_draw_text(&fb, next_x - label_w - 4, next_y, "NEXT:", COLOR_WHITE, 1);
        
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (tetrominos[game.next.type][0][y][x]) {
                    int px = next_x + x * preview_size;
                    int py = next_y + y * preview_size;
                    uint32_t color = piece_colors[game.next.type];
                    fb_fill_rect(&fb, px, py, preview_size - 1, preview_size - 1, color);
                }
            }
        }
        
        // Draw controls hint below the board (centered on play area)
        int hint_y = board_offset_y + BOARD_HEIGHT * cell_size + 10;
        text_draw_centered(&fb, board_offset_x + (BOARD_WIDTH * cell_size) / 2, hint_y,
                          "L/R: MOVE  CENTER: ROTATE  BOTTOM: DROP",
                          RGB(100, 100, 100), 1);
    } else {
        // Landscape: side panel to the right of the board
        int next_x = board_offset_x + BOARD_WIDTH * cell_size + 20;
        int next_y = board_offset_y + 20;
        fb_draw_text(&fb, next_x, next_y - 20, "NEXT:", COLOR_WHITE, 2);
        
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (tetrominos[game.next.type][0][y][x]) {
                    int px = next_x + x * (cell_size / 2);
                    int py = next_y + y * (cell_size / 2);
                    uint32_t color = piece_colors[game.next.type];
                    fb_fill_rect(&fb, px, py, cell_size / 2 - 1, cell_size / 2 - 1, color);
                }
            }
        }
        
        // Draw controls hint
        fb_draw_text(&fb, 10, fb.height - 60, "L/R: MOVE", RGB(100, 100, 100), 1);
        fb_draw_text(&fb, 10, fb.height - 45, "CENTER: ROTATE", RGB(100, 100, 100), 1);
        fb_draw_text(&fb, 10, fb.height - 30, "BOTTOM: DROP", RGB(100, 100, 100), 1);
    }
}

void draw_game() {
    fb_clear(&fb, COLOR_BLACK);
    
    // Handle welcome screen (Issue #10: draw each instruction line centered)
    if (current_screen == SCREEN_WELCOME) {
        // Title
        int center_x = fb.width / 2;
        text_draw_centered(&fb, center_x, SCREEN_SAFE_TOP + 50, "TETRIS", COLOR_CYAN, 4);
        
        // Instructions — each line individually centered
        int line_height = text_measure_height(1);
        int inst_y = LAYOUT_CENTER_Y(4 * line_height) + 20;
        text_draw_centered(&fb, center_x, inst_y, "L/R: MOVE  UP/A: ROTATE", COLOR_WHITE, 1);
        text_draw_centered(&fb, center_x, inst_y + line_height + 4, "DOWN: SOFT DROP  X: HARD DROP", COLOR_WHITE, 1);
        text_draw_centered(&fb, center_x, inst_y + 2 * (line_height + 4), "TAP LEFT/RIGHT/CENTER/BOTTOM", COLOR_WHITE, 1);
        text_draw_centered(&fb, center_x, inst_y + 3 * (line_height + 4), "PRESS START OR TAP TO BEGIN", COLOR_WHITE, 1);
        
        // Start button
        button_draw(&fb, &start_button);
        return;
    }
    
    // Draw the playing field as background (used by PLAYING, PAUSED, GAME_OVER)
    draw_playing_field();
    
    // Handle pause screen overlay (Issue #11: semi-transparent overlay)
    if (current_screen == SCREEN_PAUSED) {
        modal_dialog_draw(&pause_dialog, &fb);
        return;
    }
    
    // Handle game over screen overlay (unified GameOverScreen component)
    if (current_screen == SCREEN_GAME_OVER) {
        TouchState go_st = touch_get_state(&touch);
        GameOverAction action = gameover_update(&gos, &fb,
                                                go_st.x, go_st.y, go_st.pressed);
        switch (action) {
        case GAMEOVER_ACTION_RESTART:
            reset_game();
            game.high_score = hs_table.count > 0 ? hs_table.entries[0].score : 0;
            current_screen = SCREEN_PLAYING;
            break;
        case GAMEOVER_ACTION_EXIT:
            running = false;
            break;
        case GAMEOVER_ACTION_RESET_SCORES:
            /* Handled internally by the component */
            game.high_score = 0;
            break;
        case GAMEOVER_ACTION_NONE:
        default:
            break;
        }
        return;
    }
}

int main(int argc, char *argv[]) {
    const char *fb_device = "/dev/fb0";
    const char *touch_device = "/dev/input/touchscreen0";
    
    if (argc > 1) fb_device = argv[1];
    if (argc > 2) touch_device = argv[2];
    
    // Singleton guard — prevent duplicate instances
    int lock_fd = acquire_instance_lock("tetris");
    if (lock_fd < 0) {
        fprintf(stderr, "tetris: another instance is already running\n");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (fb_init(&fb, fb_device) < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    
    if (touch_init(&touch, touch_device) < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&fb);
        return 1;
    }
    
    // Initialize hardware control
    hw_init();
    hw_set_backlight(100);
    audio_init(&audio);  // Initialize audio (non-fatal if unavailable)

    // Gamepad init
    gamepad_init(&gamepad);
    
    srand(time(NULL));
    init_game();
    
    printf("Tetris game started!\n");
    
    // Game loop (60 FPS)
    while (running) {
        handle_input();
        update_game();
        draw_game();
        fb_swap(&fb);  // Present back buffer to screen
        
        usleep(16667);  // ~60 FPS
    }
    
    gamepad_close(&gamepad);
    touch_close(&touch);
    hw_leds_off();
    hw_set_backlight(100);
    audio_close(&audio);
    fb_close(&fb);
    
    printf("Tetris ended. Final score: %d\n", game.score);
    return 0;
}
