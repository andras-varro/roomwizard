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

#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20
#define NUM_TETROMINOS 7

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
void draw_button(int x, int y, int w, int h, const char *text, uint32_t color);
bool check_button(int x, int y, int bx, int by, int bw, int bh);
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
}

void draw_button(int x, int y, int w, int h, const char *text, uint32_t color) {
    fb_fill_rect(&fb, x, y, w, h, color);
    fb_draw_rect(&fb, x, y, w, h, COLOR_WHITE);
    
    int text_len = strlen(text);
    int text_x = x + (w - text_len * 6 * 2) / 2;
    int text_y = y + (h - 7 * 2) / 2;
    fb_draw_text(&fb, text_x, text_y, text, COLOR_WHITE, 2);
}

bool check_button(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

void handle_input() {
    touch_poll(&touch);
    TouchState state = touch_get_state(&touch);
    
    if (state.pressed) {
        int tx = state.x;
        int ty = state.y;
        
        if (game.game_over) {
            int btn_w = 200, btn_h = 50;
            int btn_x = fb.width / 2 - btn_w / 2;
            int btn_y = fb.height * 2 / 3;
            if (check_button(tx, ty, btn_x, btn_y, btn_w, btn_h)) {
                reset_game();
            }
        } else if (game.paused) {
            int btn_w = 150, btn_h = 50;
            int btn_x = fb.width / 2 - btn_w / 2;
            int btn_y = fb.height / 2;
            if (check_button(tx, ty, btn_x, btn_y, btn_w, btn_h)) {
                game.paused = false;
            }
        } else {
            // Check pause button
            if (check_button(tx, ty, 10, 10, 60, 40)) {
                game.paused = true;
            } else {
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
            }
        }
        
        last_touch_x = tx;
        last_touch_y = ty;
    }
}

void draw_game() {
    fb_clear(&fb, COLOR_BLACK);
    
    // Draw HUD
    char score_text[32];
    snprintf(score_text, sizeof(score_text), "SCORE:%d", game.score);
    fb_draw_text(&fb, fb.width / 2 - 60, 15, score_text, COLOR_WHITE, 2);
    
    char level_text[32];
    snprintf(level_text, sizeof(level_text), "LVL:%d", game.level);
    fb_draw_text(&fb, fb.width / 2 - 40, 40, level_text, COLOR_CYAN, 2);
    
    // Draw pause button
    draw_button(10, 10, 60, 40, "||", COLOR_ORANGE);
    
    if (game.game_over) {
        fb_draw_text(&fb, fb.width / 2 - 80, fb.height / 3, "GAME OVER", COLOR_RED, 3);
        
        char final_score[32];
        snprintf(final_score, sizeof(final_score), "SCORE: %d", game.score);
        fb_draw_text(&fb, fb.width / 2 - 60, fb.height / 2 - 30, final_score, COLOR_WHITE, 2);
        
        draw_button(fb.width / 2 - 100, fb.height * 2 / 3, 200, 50, "RESTART", COLOR_GREEN);
        return;
    }
    
    if (game.paused) {
        fb_draw_text(&fb, fb.width / 2 - 60, fb.height / 3, "PAUSED", COLOR_CYAN, 3);
        draw_button(fb.width / 2 - 75, fb.height / 2, 150, 50, "RESUME", COLOR_GREEN);
        return;
    }
    
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
    fb_close(&fb);
    
    printf("Tetris ended. Final score: %d\n", game.score);
    return 0;
}
