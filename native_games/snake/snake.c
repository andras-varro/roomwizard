/*
 * Snake Game - Native C Implementation
 * Optimized for 300MHz ARM with touchscreen
 * No browser overhead - direct framebuffer rendering
 * Features: LED effects, screen transitions
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

#define GRID_SIZE 20
#define MAX_SNAKE_LENGTH 400
#define INITIAL_SPEED 150000  // microseconds per frame

typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} Direction;

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    Point body[MAX_SNAKE_LENGTH];
    int length;
    Direction direction;
    Direction next_direction;
} Snake;

typedef struct {
    Point position;
    bool active;
} Food;

typedef struct {
    int score;
    int high_score;
    bool game_over;
    bool paused;
    int speed;
} GameState;

// LED effect state
typedef struct {
    bool active;
    int type;  // 0=none, 1=food, 2=grow, 3=death
    uint32_t start_time;
    int step;
} LEDEffect;

// Global variables
Framebuffer fb;
TouchInput touch;
Snake snake;
Food food;
GameState game;
LEDEffect led_effect;
int cell_size;
int grid_offset_x;
int grid_offset_y;
bool running = true;

// UI Buttons
Button menu_btn, exit_btn, restart_btn, resume_btn;

// Function prototypes
void init_game();
void reset_game();
void spawn_food();
void update_snake();
void draw_game();
void handle_input();
void signal_handler(int sig);
void start_led_effect(int type);
void update_led_effects();

void signal_handler(int sig) {
    running = false;
}

void start_led_effect(int type) {
    led_effect.active = true;
    led_effect.type = type;
    led_effect.start_time = get_time_ms();
    led_effect.step = 0;
}

void update_led_effects() {
    if (!led_effect.active) return;
    
    uint32_t current_time = get_time_ms();
    uint32_t elapsed = current_time - led_effect.start_time;
    
    switch (led_effect.type) {
        case 1:  // Food effect - quick green flash (100ms)
            if (elapsed < 100) {
                hw_set_leds(HW_LED_COLOR_GREEN);
            } else {
                hw_set_leds(HW_LED_COLOR_OFF);
                led_effect.active = false;
            }
            break;
            
        case 2:  // Grow effect - rainbow cycle (150ms)
            if (elapsed < 150) {
                int length_mod = snake.length % 6;
                switch (length_mod) {
                    case 0: hw_set_leds(100, 0); break;    // Red
                    case 1: hw_set_leds(100, 50); break;   // Orange
                    case 2: hw_set_leds(100, 100); break;  // Yellow
                    case 3: hw_set_leds(0, 100); break;    // Green
                    case 4: hw_set_leds(50, 100); break;   // Cyan-ish
                    case 5: hw_set_leds(100, 30); break;   // Red-orange
                }
            } else {
                hw_set_leds(HW_LED_COLOR_OFF);
                led_effect.active = false;
            }
            break;
            
        case 3:  // Death effect - red pulsing (3 pulses, 600ms total)
            {
                int pulse_num = elapsed / 200;  // Each pulse is 200ms
                int pulse_phase = elapsed % 200;
                
                if (pulse_num < 3) {
                    if (pulse_phase < 100) {
                        hw_set_red_led(100);  // On for 100ms
                    } else {
                        hw_set_red_led(0);    // Off for 100ms
                    }
                } else {
                    hw_set_red_led(0);
                    led_effect.active = false;
                }
            }
            break;
    }
}

void init_game() {
    // Calculate grid dimensions
    int usable_width = fb.width - 40;
    int usable_height = fb.height - 120;
    cell_size = (usable_width < usable_height ? usable_width : usable_height) / GRID_SIZE;
    grid_offset_x = (fb.width - (GRID_SIZE * cell_size)) / 2;
    grid_offset_y = 80;
    
    // Initialize buttons
    button_init(&menu_btn, 10, 10, BTN_MENU_WIDTH, BTN_MENU_HEIGHT, "||", 
                BTN_MENU_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&exit_btn, fb.width - BTN_EXIT_WIDTH - 10, 10, BTN_EXIT_WIDTH, BTN_EXIT_HEIGHT, "X",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&restart_btn, fb.width / 2 - BTN_LARGE_WIDTH / 2, fb.height * 2 / 3, 
                BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "RESTART", BTN_RESTART_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&resume_btn, fb.width / 2 - 75, fb.height / 2, 150, 50, "RESUME",
                BTN_RESUME_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    
    // Initialize LED effect state
    led_effect.active = false;
    led_effect.type = 0;
    
    game.high_score = 0;
    reset_game();
}

void reset_game() {
    // Initialize snake in the middle
    snake.length = 3;
    snake.direction = DIR_RIGHT;
    snake.next_direction = DIR_RIGHT;
    
    int start_x = GRID_SIZE / 2;
    int start_y = GRID_SIZE / 2;
    
    for (int i = 0; i < snake.length; i++) {
        snake.body[i].x = start_x - i;
        snake.body[i].y = start_y;
    }
    
    game.score = 0;
    game.game_over = false;
    game.paused = false;
    game.speed = INITIAL_SPEED;
    
    spawn_food();
}

void spawn_food() {
    bool valid = false;
    
    while (!valid) {
        food.position.x = rand() % GRID_SIZE;
        food.position.y = rand() % GRID_SIZE;
        
        valid = true;
        for (int i = 0; i < snake.length; i++) {
            if (snake.body[i].x == food.position.x && snake.body[i].y == food.position.y) {
                valid = false;
                break;
            }
        }
    }
    
    food.active = true;
}

void update_snake() {
    if (game.game_over || game.paused) return;
    
    // Update direction
    snake.direction = snake.next_direction;
    
    // Calculate new head position
    Point new_head = snake.body[0];
    
    switch (snake.direction) {
        case DIR_UP:    new_head.y--; break;
        case DIR_DOWN:  new_head.y++; break;
        case DIR_LEFT:  new_head.x--; break;
        case DIR_RIGHT: new_head.x++; break;
    }
    
    // Check wall collision
    if (new_head.x < 0 || new_head.x >= GRID_SIZE || 
        new_head.y < 0 || new_head.y >= GRID_SIZE) {
        game.game_over = true;
        if (game.score > game.high_score) {
            game.high_score = game.score;
        }
        start_led_effect(3);  // Start death effect (non-blocking)
        return;
    }
    
    // Check self collision
    for (int i = 0; i < snake.length; i++) {
        if (snake.body[i].x == new_head.x && snake.body[i].y == new_head.y) {
            game.game_over = true;
            if (game.score > game.high_score) {
                game.high_score = game.score;
            }
            start_led_effect(3);  // Start death effect (non-blocking)
            return;
        }
    }
    
    // Move snake body
    for (int i = snake.length - 1; i > 0; i--) {
        snake.body[i] = snake.body[i - 1];
    }
    snake.body[0] = new_head;
    
    // Check food collision
    if (new_head.x == food.position.x && new_head.y == food.position.y) {
        game.score += 10;
        start_led_effect(1);  // Start food effect (non-blocking)
        
        if (snake.length < MAX_SNAKE_LENGTH) {
            snake.length++;
            start_led_effect(2);  // Start grow effect (non-blocking)
        }
        // Increase speed slightly
        if (game.speed > 50000) {
            game.speed -= 5000;
        }
        spawn_food();
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
    uint32_t current_time = get_time_ms();
    
    if (state.pressed) {
        int tx = state.x;
        int ty = state.y;
        
        if (game.game_over) {
            // Check restart button
            if (button_is_touched(&restart_btn, tx, ty)) {
                if (button_check_press(&restart_btn, true, current_time)) {
                    reset_game();
                }
            }
        } else if (game.paused) {
            // Check resume button
            if (button_is_touched(&resume_btn, tx, ty)) {
                if (button_check_press(&resume_btn, true, current_time)) {
                    game.paused = false;
                }
            }
        } else {
            // Check menu button
            if (button_is_touched(&menu_btn, tx, ty)) {
                if (button_check_press(&menu_btn, true, current_time)) {
                    game.paused = true;
                }
            }
            // Check exit button
            else if (button_is_touched(&exit_btn, tx, ty)) {
                if (button_check_press(&exit_btn, true, current_time)) {
                    running = false;
                    return;
                }
            } else {
                // Touch controls for direction
                Point head = snake.body[0];
                int head_screen_x = grid_offset_x + head.x * cell_size + cell_size / 2;
                int head_screen_y = grid_offset_y + head.y * cell_size + cell_size / 2;
                
                int dx = tx - head_screen_x;
                int dy = ty - head_screen_y;
                
                if (abs(dx) > abs(dy)) {
                    // Horizontal movement
                    if (dx > 0 && snake.direction != DIR_LEFT) {
                        snake.next_direction = DIR_RIGHT;
                    } else if (dx < 0 && snake.direction != DIR_RIGHT) {
                        snake.next_direction = DIR_LEFT;
                    }
                } else {
                    // Vertical movement
                    if (dy > 0 && snake.direction != DIR_UP) {
                        snake.next_direction = DIR_DOWN;
                    } else if (dy < 0 && snake.direction != DIR_DOWN) {
                        snake.next_direction = DIR_UP;
                    }
                }
            }
        }
    } else {
        // Reset button states when not pressed
        button_check_press(&menu_btn, false, current_time);
        button_check_press(&exit_btn, false, current_time);
        button_check_press(&restart_btn, false, current_time);
        button_check_press(&resume_btn, false, current_time);
    }
}

void draw_game() {
    // Clear screen
    fb_clear(&fb, COLOR_BLACK);
    
    // Draw HUD
    char score_text[32];
    snprintf(score_text, sizeof(score_text), "SCORE: %d", game.score);
    fb_draw_text(&fb, fb.width / 2 - 60, 20, score_text, COLOR_WHITE, 2);
    
    char high_text[32];
    snprintf(high_text, sizeof(high_text), "HIGH: %d", game.high_score);
    fb_draw_text(&fb, fb.width / 2 - 60, 45, high_text, COLOR_YELLOW, 2);
    
    // Draw menu and exit buttons
    button_draw(&fb, &menu_btn);
    button_draw(&fb, &exit_btn);
    
    if (game.game_over) {
        // Draw game over screen using common framework
        char msg[64];
        if (game.score == game.high_score && game.score > 0) {
            snprintf(msg, sizeof(msg), "GAME OVER - NEW HIGH SCORE!");
        } else {
            snprintf(msg, sizeof(msg), "GAME OVER");
        }
        draw_game_over_screen(&fb, msg, game.score, &restart_btn);
        return;
    }
    
    if (game.paused) {
        draw_pause_screen(&fb, &resume_btn);
        return;
    }
    
    // Draw grid border
    fb_draw_rect(&fb, grid_offset_x - 2, grid_offset_y - 2, 
                 GRID_SIZE * cell_size + 4, GRID_SIZE * cell_size + 4, COLOR_WHITE);
    
    // Draw snake
    for (int i = 0; i < snake.length; i++) {
        int x = grid_offset_x + snake.body[i].x * cell_size;
        int y = grid_offset_y + snake.body[i].y * cell_size;
        
        uint32_t color = (i == 0) ? COLOR_GREEN : RGB(0, 200, 0);
        fb_fill_rect(&fb, x + 1, y + 1, cell_size - 2, cell_size - 2, color);
    }
    
    // Draw food
    if (food.active) {
        int fx = grid_offset_x + food.position.x * cell_size;
        int fy = grid_offset_y + food.position.y * cell_size;
        fb_fill_circle(&fb, fx + cell_size / 2, fy + cell_size / 2, cell_size / 2 - 2, COLOR_RED);
    }
    
    // Draw controls hint
    fb_draw_text(&fb, 10, fb.height - 25, "TAP DIRECTION TO MOVE", RGB(100, 100, 100), 1);
}

int main(int argc, char *argv[]) {
    const char *fb_device = "/dev/fb0";
    const char *touch_device = "/dev/input/touchscreen0";
    
    // Allow custom devices
    if (argc > 1) fb_device = argv[1];
    if (argc > 2) touch_device = argv[2];
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize hardware control
    hw_init();
    hw_leds_off();  // Start with LEDs off
    
    // Initialize framebuffer
    if (fb_init(&fb, fb_device) < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    
    // Initialize touch input
    if (touch_init(&touch, touch_device) < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&fb);
        return 1;
    }
    
    // Set screen size for coordinate scaling
    touch_set_screen_size(&touch, fb.width, fb.height);
    
    // Seed random number generator
    srand(time(NULL));
    
    // Initialize game
    init_game();
    
    printf("Snake game started! Touch screen to play.\n");
    printf("Press Ctrl+C to exit.\n");
    
    // Game loop
    while (running) {
        handle_input();
        update_snake();
        update_led_effects();  // Update LED effects (non-blocking)
        draw_game();
        fb_swap(&fb);  // Present back buffer to screen
        
        usleep(game.speed);
    }
    
    // Fade out screen on exit
    fb_fade_out(&fb);
    
    // Cleanup
    hw_leds_off();
    touch_close(&touch);
    fb_close(&fb);
    
    printf("Snake game ended. Final score: %d\n", game.score);
    return 0;
}
