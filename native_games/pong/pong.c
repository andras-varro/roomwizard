/*
 * Pong Game - Native C Implementation
 * Single player vs AI
 * Touch to move paddle
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <math.h>
#include "../common/framebuffer.h"
#include "../common/touch_input.h"
#include "../common/game_common.h"
#include "../common/hardware.h"

#define PADDLE_WIDTH 15
#define PADDLE_HEIGHT 80
#define BALL_SIZE 12
#define WINNING_SCORE 11

typedef enum {
    SCREEN_WELCOME,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_GAME_OVER
} GameScreen;

typedef struct {
    float x, y;
    float vx, vy;
} Ball;

typedef struct {
    float y;
    int score;
} Paddle;

typedef struct {
    Paddle player;
    Paddle ai;
    Ball ball;
    bool game_over;
    bool paused;
    int winner;  // 0 = none, 1 = player, 2 = AI
    int difficulty;  // 1 = easy, 2 = medium, 3 = hard
} GameState;

// Global variables
Framebuffer fb;
TouchInput touch;
GameState game;
bool running = true;
int play_area_width;
int play_area_height;
int offset_x;
int offset_y;
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
void reset_ball();
void update_game();
void update_ai();
void draw_game();
void handle_input();
void signal_handler(int sig);

void signal_handler(int sig) {
    running = false;
}

void init_game() {
    play_area_width = fb.width - 40;
    play_area_height = fb.height - 120;
    offset_x = 20;
    offset_y = 80;
    
    game.difficulty = 2;  // Medium by default
    
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
    game.player.y = play_area_height / 2 - PADDLE_HEIGHT / 2;
    game.player.score = 0;
    game.ai.y = play_area_height / 2 - PADDLE_HEIGHT / 2;
    game.ai.score = 0;
    game.game_over = false;
    game.paused = false;
    game.winner = 0;
    
    reset_ball();
}

void reset_ball() {
    game.ball.x = play_area_width / 2;
    game.ball.y = play_area_height / 2;
    
    // Random angle between -45 and 45 degrees
    float angle = ((rand() % 90) - 45) * M_PI / 180.0;
    float speed = 5.0;
    
    // Random direction (left or right)
    int direction = (rand() % 2) ? 1 : -1;
    
    game.ball.vx = cos(angle) * speed * direction;
    game.ball.vy = sin(angle) * speed;
}

void update_ai() {
    // AI difficulty affects reaction speed
    float ai_speed = 3.0 + game.difficulty;
    float target_y = game.ball.y - PADDLE_HEIGHT / 2;
    
    // Add some randomness to make it beatable
    if (game.difficulty < 3) {
        target_y += (rand() % 20) - 10;
    }
    
    if (game.ai.y < target_y - 5) {
        game.ai.y += ai_speed;
    } else if (game.ai.y > target_y + 5) {
        game.ai.y -= ai_speed;
    }
    
    // Keep AI paddle in bounds
    if (game.ai.y < 0) game.ai.y = 0;
    if (game.ai.y > play_area_height - PADDLE_HEIGHT) {
        game.ai.y = play_area_height - PADDLE_HEIGHT;
    }
}

void update_game() {
    if (game.game_over || game.paused) return;
    
    // Update ball position
    game.ball.x += game.ball.vx;
    game.ball.y += game.ball.vy;
    
    // Ball collision with top/bottom walls
    if (game.ball.y <= 0 || game.ball.y >= play_area_height - BALL_SIZE) {
        game.ball.vy = -game.ball.vy;
        game.ball.y = (game.ball.y <= 0) ? 0 : play_area_height - BALL_SIZE;
    }
    
    // Ball collision with player paddle (left)
    if (game.ball.x <= PADDLE_WIDTH && 
        game.ball.y + BALL_SIZE >= game.player.y && 
        game.ball.y <= game.player.y + PADDLE_HEIGHT) {
        
        game.ball.vx = -game.ball.vx * 1.05;  // Speed up slightly
        game.ball.x = PADDLE_WIDTH;
        
        // Add spin based on where ball hits paddle
        float hit_pos = (game.ball.y + BALL_SIZE / 2 - game.player.y) / PADDLE_HEIGHT;
        game.ball.vy += (hit_pos - 0.5) * 3;
        
        // Green flash on player hit
        hw_set_led(LED_GREEN, 100);
        usleep(50000);  // 50ms
        hw_leds_off();
    }
    
    // Ball collision with AI paddle (right)
    if (game.ball.x + BALL_SIZE >= play_area_width - PADDLE_WIDTH && 
        game.ball.y + BALL_SIZE >= game.ai.y && 
        game.ball.y <= game.ai.y + PADDLE_HEIGHT) {
        
        game.ball.vx = -game.ball.vx * 1.05;
        game.ball.x = play_area_width - PADDLE_WIDTH - BALL_SIZE;
        
        float hit_pos = (game.ball.y + BALL_SIZE / 2 - game.ai.y) / PADDLE_HEIGHT;
        game.ball.vy += (hit_pos - 0.5) * 3;
        
        // Red flash on AI hit
        hw_set_led(LED_RED, 100);
        usleep(50000);  // 50ms
        hw_leds_off();
    }
    
    // Ball out of bounds - score
    if (game.ball.x < 0) {
        game.ai.score++;
        // Red flash for AI scoring
        hw_set_led(LED_RED, 100);
        usleep(200000);  // 200ms
        hw_leds_off();
        
        if (game.ai.score >= WINNING_SCORE) {
            game.game_over = true;
            game.winner = 2;
            current_screen = SCREEN_GAME_OVER;
            // Red pulse for loss
            for (int i = 0; i < 3; i++) {
                hw_set_led(LED_RED, 100);
                usleep(200000);
                hw_leds_off();
                usleep(200000);
            }
        } else {
            reset_ball();
        }
    } else if (game.ball.x > play_area_width) {
        game.player.score++;
        // Green flash for player scoring
        hw_set_led(LED_GREEN, 100);
        usleep(200000);  // 200ms
        hw_leds_off();
        
        if (game.player.score >= WINNING_SCORE) {
            game.game_over = true;
            game.winner = 1;
            current_screen = SCREEN_GAME_OVER;
            // Green pulse for win
            for (int i = 0; i < 3; i++) {
                hw_set_led(LED_GREEN, 100);
                usleep(200000);
                hw_leds_off();
                usleep(200000);
            }
        } else {
            reset_ball();
        }
    }
    
    // Update AI
    update_ai();
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
    
    // Move player paddle to touch Y position
    if ((state.held || state.pressed) && !game.game_over && !game.paused) {
        int relative_y = state.y - offset_y;
        game.player.y = relative_y - PADDLE_HEIGHT / 2;
        
        // Keep paddle in bounds
        if (game.player.y < 0) game.player.y = 0;
        if (game.player.y > play_area_height - PADDLE_HEIGHT) {
            game.player.y = play_area_height - PADDLE_HEIGHT;
        }
    }
}

void draw_game() {
    fb_clear(&fb, COLOR_BLACK);
    
    // Handle welcome screen
    if (current_screen == SCREEN_WELCOME) {
        draw_welcome_screen(&fb, "PONG", 
            "TOUCH TO MOVE PADDLE\n"
            "FIRST TO 11 WINS", &start_button);
        return;
    }
    
    // Handle game over screen
    if (current_screen == SCREEN_GAME_OVER) {
        const char *message = (game.winner == 1) ? "YOU WIN!" : "AI WINS!";
        draw_game_over_screen(&fb, message, game.player.score, &restart_button);
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
    char player_score[16];
    snprintf(player_score, sizeof(player_score), "%d", game.player.score);
    fb_draw_text(&fb, fb.width / 3, 20, player_score, COLOR_GREEN, 4);
    
    char ai_score[16];
    snprintf(ai_score, sizeof(ai_score), "%d", game.ai.score);
    fb_draw_text(&fb, fb.width * 2 / 3, 20, ai_score, COLOR_RED, 4);
    
    // Draw menu and exit buttons
    draw_menu_button(&fb, &menu_button);
    draw_exit_button(&fb, &exit_button);
    
    // Draw play area border
    fb_draw_rect(&fb, offset_x - 2, offset_y - 2,
                 play_area_width + 4, play_area_height + 4, COLOR_WHITE);
    
    // Draw center line
    for (int y = 0; y < play_area_height; y += 20) {
        fb_fill_rect(&fb, offset_x + play_area_width / 2 - 2, offset_y + y, 4, 10, COLOR_GRAY);
    }
    
    // Draw player paddle (left, green)
    fb_fill_rect(&fb, offset_x + 5, offset_y + (int)game.player.y, 
                 PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_GREEN);
    
    // Draw AI paddle (right, red)
    fb_fill_rect(&fb, offset_x + play_area_width - PADDLE_WIDTH - 5, 
                 offset_y + (int)game.ai.y, PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_RED);
    
    // Draw ball
    fb_fill_circle(&fb, offset_x + (int)game.ball.x + BALL_SIZE / 2, 
                   offset_y + (int)game.ball.y + BALL_SIZE / 2, 
                   BALL_SIZE / 2, COLOR_WHITE);
    
    // Draw controls hint
    fb_draw_text(&fb, 10, fb.height - 25, "TOUCH TO MOVE PADDLE", RGB(100, 100, 100), 1);
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
    
    printf("Pong game started!\n");
    
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
    
    printf("Pong ended.\n");
    return 0;
}
