/*
 * Tetris Game - Native C Implementation
 * Optimized for 300MHz ARM with touchscreen
 * Touch controls: Tap left/right to move, tap center to rotate, swipe down to drop
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
#include "../common/game_common.h"
#include "../common/hardware.h"

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

// Global variables
Framebuffer fb;
TouchInput touch;
GameState game;
int cell_size;
int board_offset_x;
int board_offset_y;
bool running = true;
int last_touch_x = -1;
int last_touch_y = -1;
Button menu_button;
Button exit_button;
Button start_button;
Button restart_button;
Button resume_button;
Button exit_pause_button;
GameScreen current_screen = SCREEN_WELCOME;

// Function prototypes
void init_game();
void reset_game();
void spawn_piece(Piece *piece);
bool check_collision(Piece *piece, int dx, int dy, int new_rotation);
void lock_piece();
void clear_lines();
void update_game();
void draw_game();
void handle_input();
void signal_handler(int sig);

void signal_handler(int sig) {
    running = false;
}

void init_game() {
    // Calculate board dimensions
    int usable_height = fb.height - 100;
    cell_size = usable_height / BOARD_HEIGHT;
    if (cell_size > 30) cell_size = 30;
    
    board_offset_x = 20;
    board_offset_y = 80;
    
    game.high_score = 0;
    
    // Initialize buttons
    button_init(&menu_button, 10, 10, BTN_MENU_WIDTH, BTN_MENU_HEIGHT, "",
                BTN_MENU_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&exit_button, fb.width - BTN_EXIT_WIDTH - 10, 10, 
                BTN_EXIT_WIDTH, BTN_EXIT_HEIGHT, "",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&start_button, fb.width / 2 - BTN_LARGE_WIDTH / 2, 
                fb.height / 2 + 40, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "TAP TO START",
                BTN_START_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&restart_button, fb.width / 2 - BTN_LARGE_WIDTH / 2, 
                fb.height * 2 / 3, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "RESTART",
                BTN_RESTART_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&resume_button, fb.width / 2 - BTN_LARGE_WIDTH / 2, 
                fb.height / 2, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "RESUME",
                BTN_RESUME_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&exit_pause_button, fb.width / 2 - BTN_LARGE_WIDTH / 2, 
                fb.height / 2 + 80, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "EXIT",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    
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
        if (game.score > game.high_score) {
            game.high_score = game.score;
        }
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
        
        // LED effects for line clears
        if (lines == 4) {
            // Tetris! Rainbow cycle (yellow/orange effect)
            hw_set_leds(HW_LED_COLOR_YELLOW);
            usleep(200000);  // 200ms
            hw_leds_off();
        } else if (lines >= 2) {
            // Multi-line clear - green flash
            hw_set_led(LED_GREEN, 100);
            usleep(150000);  // 150ms
            hw_leds_off();
        } else {
            // Single line - brief green
            hw_set_led(LED_GREEN, 100);
            usleep(100000);  // 100ms
            hw_leds_off();
        }
    }
}

void update_game() {
    if (game.game_over || game.paused) return;
    
    game.drop_counter++;
    if (game.drop_counter >= game.drop_speed) {
        game.drop_counter = 0;
        
        if (!check_collision(&game.current, 0, 1, game.current.rotation)) {
            game.current.y++;
        } else {
            lock_piece();
        }
    }
    
    // Check for game over
    if (game.game_over) {
        current_screen = SCREEN_GAME_OVER;
        if (game.score > game.high_score) {
            game.high_score = game.score;
        }
        // Red pulse on game over
        for (int i = 0; i < 3; i++) {
            hw_set_led(LED_RED, 100);
            usleep(200000);  // 200ms
            hw_leds_off();
            usleep(200000);  // 200ms
        }
    }
}

void handle_input() {
    touch_poll(&touch);
    TouchState state = touch_get_state(&touch);
    uint32_t current_time = get_time_ms();
    
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
        return;
    }
    
    // Handle game over screen
    if (current_screen == SCREEN_GAME_OVER) {
        if (state.pressed) {
            bool touched = button_is_touched(&restart_button, state.x, state.y);
            if (button_check_press(&restart_button, touched, current_time)) {
                reset_game();
                current_screen = SCREEN_PLAYING;
                hw_set_led(LED_GREEN, 100);
                usleep(100000);  // 100ms
                hw_leds_off();
            }
        }
        return;
    }
    
    // Handle pause screen
    if (current_screen == SCREEN_PAUSED) {
        if (state.pressed) {
            bool resume_touched = button_is_touched(&resume_button, state.x, state.y);
            if (button_check_press(&resume_button, resume_touched, current_time)) {
                current_screen = SCREEN_PLAYING;
                game.paused = false;
                return;
            }
            
            bool exit_touched = button_is_touched(&exit_pause_button, state.x, state.y);
            if (button_check_press(&exit_pause_button, exit_touched, current_time)) {
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
        }
        return;
    }
    
    // Playing screen - check menu and exit buttons
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
            return;
        }
    }
    
    if (state.pressed && !game.game_over && !game.paused) {
        int tx = state.x;
        int ty = state.y;
        
        // Game controls
        int board_right = board_offset_x + BOARD_WIDTH * cell_size;
        
        if (tx < board_offset_x - 10) {
            // Left side - move left
            if (!check_collision(&game.current, -1, 0, game.current.rotation)) {
                game.current.x--;
            }
        } else if (tx > board_right + 10) {
            // Right side - move right
            if (!check_collision(&game.current, 1, 0, game.current.rotation)) {
                game.current.x++;
            }
        } else if (ty > fb.height - 80) {
            // Bottom - hard drop
            while (!check_collision(&game.current, 0, 1, game.current.rotation)) {
                game.current.y++;
                game.score += 2;
            }
            lock_piece();
        } else {
            // Center - rotate
            int new_rotation = (game.current.rotation + 1) % 4;
            if (!check_collision(&game.current, 0, 0, new_rotation)) {
                game.current.rotation = new_rotation;
            }
        }
        
        last_touch_x = tx;
        last_touch_y = ty;
    }
}

void draw_game() {
    fb_clear(&fb, COLOR_BLACK);
    
    // Handle welcome screen
    if (current_screen == SCREEN_WELCOME) {
        draw_welcome_screen(&fb, "TETRIS", 
            "TAP LEFT/RIGHT: MOVE\n"
            "TAP CENTER: ROTATE\n"
            "TAP BOTTOM: DROP", &start_button);
        return;
    }
    
    // Handle game over screen
    if (current_screen == SCREEN_GAME_OVER) {
        draw_game_over_screen(&fb, "GAME OVER", game.score, &restart_button);
        return;
    }
    
    // Handle pause screen
    if (current_screen == SCREEN_PAUSED) {
        // Draw pause screen with both resume and exit buttons
        fb_draw_text(&fb, fb.width / 2 - 60, fb.height / 3, "PAUSED", COLOR_CYAN, 3);
        button_draw(&fb, &resume_button);
        button_draw(&fb, &exit_pause_button);
        return;
    }
    
    // Draw HUD
    char score_text[32];
    snprintf(score_text, sizeof(score_text), "SCORE:%d", game.score);
    fb_draw_text(&fb, fb.width / 2 - 60, 15, score_text, COLOR_WHITE, 2);
    
    char level_text[32];
    snprintf(level_text, sizeof(level_text), "LVL:%d", game.level);
    fb_draw_text(&fb, fb.width / 2 - 40, 40, level_text, COLOR_CYAN, 2);
    
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

int main(int argc, char *argv[]) {
    const char *fb_device = "/dev/fb0";
    const char *touch_device = "/dev/input/touchscreen0";
    
    if (argc > 1) fb_device = argv[1];
    if (argc > 2) touch_device = argv[2];
    
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
    
    touch_close(&touch);
    hw_leds_off();
    fb_close(&fb);
    
    printf("Tetris ended. Final score: %d\n", game.score);
    return 0;
}
