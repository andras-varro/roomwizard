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
#include "../common/common.h"
#include "../common/hardware.h"
#include "../common/audio.h"

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
Audio audio;
int play_area_width;
int play_area_height;
int offset_x;
int offset_y;
Button menu_button;
Button exit_button;
Button start_button;
ModalDialog pause_dialog;
GameScreen current_screen = SCREEN_WELCOME;
bool portrait_mode = false;
static HighScoreTable hs_table;
static GameOverScreen gos;

// Function prototypes
void init_game();
void reset_game();
void reset_ball();
void update_game();
void update_ai();
void draw_game();
void handle_input();
void signal_handler(int sig);
static void enter_game_over(void);

void signal_handler(int sig) {
    running = false;
}

void init_game() {
    portrait_mode = fb.portrait_mode;
    
    play_area_width = fb.width - 40;
    play_area_height = fb.height - 120;
    offset_x = 20;
    offset_y = 80;
    
    game.difficulty = 2;
    
    // Initialize highscore
    hs_init(&hs_table, "pong");
    hs_load(&hs_table);
    
    // Initialize buttons — positions use fb.width/fb.height so they auto-adapt
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
    if (portrait_mode) {
        // Paddles move horizontally in portrait — use .y field for X position
        game.player.y = play_area_width / 2 - PADDLE_HEIGHT / 2;
        game.ai.y = play_area_width / 2 - PADDLE_HEIGHT / 2;
    } else {
        game.player.y = play_area_height / 2 - PADDLE_HEIGHT / 2;
        game.ai.y = play_area_height / 2 - PADDLE_HEIGHT / 2;
    }
    game.player.score = 0;
    game.ai.score = 0;
    game.game_over = false;
    game.paused = false;
    game.winner = 0;
    
    reset_ball();
}

void reset_ball() {
    game.ball.x = play_area_width / 2;
    game.ball.y = play_area_height / 2;
    
    float angle = ((rand() % 90) - 45) * M_PI / 180.0;
    float speed = 5.0;
    
    if (portrait_mode) {
        // Ball moves primarily up/down in portrait
        int direction = (rand() % 2) ? 1 : -1;
        game.ball.vx = sin(angle) * speed;      // Small horizontal component
        game.ball.vy = cos(angle) * speed * direction;  // Primary vertical movement
    } else {
        // Ball moves primarily left/right in landscape
        int direction = (rand() % 2) ? 1 : -1;
        game.ball.vx = cos(angle) * speed * direction;
        game.ball.vy = sin(angle) * speed;
    }
}

// Helper: initialize the unified game over screen when a match ends
static void enter_game_over(void) {
    char info[64];
    bool player_won = (game.winner == 1);
    if (player_won)
        snprintf(info, sizeof(info), "YOU WIN! %d - %d", game.player.score, game.ai.score);
    else
        snprintf(info, sizeof(info), "AI WINS %d - %d", game.ai.score, game.player.score);
    gameover_init(&gos, &fb, game.player.score,
                  player_won ? "YOU WIN!" : "AI WINS!", info, &hs_table, &touch);
}

void update_ai() {
    float ai_speed = 3.0 + game.difficulty;
    float target;
    float max_pos;
    
    if (portrait_mode) {
        // AI tracks ball X position (horizontal)
        target = game.ball.x - PADDLE_HEIGHT / 2;
        max_pos = play_area_width - PADDLE_HEIGHT;
    } else {
        // AI tracks ball Y position (vertical)
        target = game.ball.y - PADDLE_HEIGHT / 2;
        max_pos = play_area_height - PADDLE_HEIGHT;
    }
    
    if (game.difficulty < 3) {
        target += (rand() % 20) - 10;
    }
    
    if (game.ai.y < target - 5) {
        game.ai.y += ai_speed;
    } else if (game.ai.y > target + 5) {
        game.ai.y -= ai_speed;
    }
    
    if (game.ai.y < 0) game.ai.y = 0;
    if (game.ai.y > max_pos) game.ai.y = max_pos;
}

void update_game() {
    if (current_screen != SCREEN_PLAYING) return;
    if (game.game_over || game.paused) return;
    
    game.ball.x += game.ball.vx;
    game.ball.y += game.ball.vy;
    
    if (portrait_mode) {
        // === PORTRAIT MODE COLLISIONS ===
        
        // Ball collision with left/right walls (bounce)
        if (game.ball.x <= 0 || game.ball.x >= play_area_width - BALL_SIZE) {
            game.ball.vx = -game.ball.vx;
            game.ball.x = (game.ball.x <= 0) ? 0 : play_area_width - BALL_SIZE;
            audio_interrupt(&audio);
            audio_tone(&audio, 2000, 60);
        }
        
        // Ball collision with player paddle (bottom)
        if (game.ball.y + BALL_SIZE >= play_area_height - PADDLE_WIDTH &&
            game.ball.x + BALL_SIZE >= game.player.y &&
            game.ball.x <= game.player.y + PADDLE_HEIGHT) {
            
            game.ball.vy = -game.ball.vy * 1.05;
            game.ball.y = play_area_height - PADDLE_WIDTH - BALL_SIZE;
            
            float hit_pos = (game.ball.x + BALL_SIZE / 2 - game.player.y) / PADDLE_HEIGHT;
            game.ball.vx += (hit_pos - 0.5) * 3;
            
            hw_set_led(LED_GREEN, 100);
            audio_interrupt(&audio);
            audio_tone(&audio, 440, 90);
            hw_leds_off();
        }
        
        // Ball collision with AI paddle (top)
        if (game.ball.y <= PADDLE_WIDTH &&
            game.ball.x + BALL_SIZE >= game.ai.y &&
            game.ball.x <= game.ai.y + PADDLE_HEIGHT) {
            
            game.ball.vy = -game.ball.vy * 1.05;
            game.ball.y = PADDLE_WIDTH;
            
            float hit_pos = (game.ball.x + BALL_SIZE / 2 - game.ai.y) / PADDLE_HEIGHT;
            game.ball.vx += (hit_pos - 0.5) * 3;
            
            hw_set_led(LED_RED, 100);
            audio_interrupt(&audio);
            audio_tone(&audio, 440, 90);
            hw_leds_off();
        }
        
        // Ball out top = player scores
        if (game.ball.y < 0) {
            game.player.score++;
            hw_set_led(LED_GREEN, 100);
            audio_blip(&audio);
            hw_leds_off();
            
            if (game.player.score >= WINNING_SCORE) {
                game.game_over = true;
                game.winner = 1;
                current_screen = SCREEN_GAME_OVER;
                enter_game_over();
                for (int i = 0; i < 3; i++) {
                    hw_set_led(LED_GREEN, 100);
                    usleep(200000);
                    hw_leds_off();
                    usleep(200000);
                }
                audio_success(&audio);
            } else {
                reset_ball();
            }
        }
        // Ball out bottom = AI scores
        else if (game.ball.y > play_area_height) {
            game.ai.score++;
            hw_set_led(LED_RED, 100);
            audio_blip(&audio);
            hw_leds_off();
            
            if (game.ai.score >= WINNING_SCORE) {
                game.game_over = true;
                game.winner = 2;
                current_screen = SCREEN_GAME_OVER;
                enter_game_over();
                for (int i = 0; i < 3; i++) {
                    hw_set_led(LED_RED, 100);
                    usleep(200000);
                    hw_leds_off();
                    usleep(200000);
                }
                audio_fail(&audio);
            } else {
                reset_ball();
            }
        }
        
    } else {
        // === LANDSCAPE MODE COLLISIONS (existing code, unchanged) ===
        
        // Ball collision with top/bottom walls
        if (game.ball.y <= 0 || game.ball.y >= play_area_height - BALL_SIZE) {
            game.ball.vy = -game.ball.vy;
            game.ball.y = (game.ball.y <= 0) ? 0 : play_area_height - BALL_SIZE;
            audio_interrupt(&audio);
            audio_tone(&audio, 2000, 60);
        }
        
        // Ball collision with player paddle (left)
        if (game.ball.x <= PADDLE_WIDTH &&
            game.ball.y + BALL_SIZE >= game.player.y &&
            game.ball.y <= game.player.y + PADDLE_HEIGHT) {
            
            game.ball.vx = -game.ball.vx * 1.05;
            game.ball.x = PADDLE_WIDTH;
            
            float hit_pos = (game.ball.y + BALL_SIZE / 2 - game.player.y) / PADDLE_HEIGHT;
            game.ball.vy += (hit_pos - 0.5) * 3;
            
            hw_set_led(LED_GREEN, 100);
            audio_interrupt(&audio);
            audio_tone(&audio, 440, 90);
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
            
            hw_set_led(LED_RED, 100);
            audio_interrupt(&audio);
            audio_tone(&audio, 440, 90);
            hw_leds_off();
        }
        
        // Ball out of bounds - score
        if (game.ball.x < 0) {
            game.ai.score++;
            hw_set_led(LED_RED, 100);
            audio_blip(&audio);
            hw_leds_off();
            
            if (game.ai.score >= WINNING_SCORE) {
                game.game_over = true;
                game.winner = 2;
                current_screen = SCREEN_GAME_OVER;
                enter_game_over();
                for (int i = 0; i < 3; i++) {
                    hw_set_led(LED_RED, 100);
                    usleep(200000);
                    hw_leds_off();
                    usleep(200000);
                }
                audio_fail(&audio);
            } else {
                reset_ball();
            }
        } else if (game.ball.x > play_area_width) {
            game.player.score++;
            hw_set_led(LED_GREEN, 100);
            audio_blip(&audio);
            hw_leds_off();
            
            if (game.player.score >= WINNING_SCORE) {
                game.game_over = true;
                game.winner = 1;
                current_screen = SCREEN_GAME_OVER;
                enter_game_over();
                for (int i = 0; i < 3; i++) {
                    hw_set_led(LED_GREEN, 100);
                    usleep(200000);
                    hw_leds_off();
                    usleep(200000);
                }
                audio_success(&audio);
            } else {
                reset_ball();
            }
        }
    }
    
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
                reset_game();
                current_screen = SCREEN_PLAYING;
                hw_set_led(LED_GREEN, 100);
                usleep(100000);  // 100ms
                hw_leds_off();
            }
        }
        return;
    }
    
    // Handle game over screen — gameover_update() manages buttons in draw phase
    if (current_screen == SCREEN_GAME_OVER) {
        return;
    }
    
    // Handle pause screen
    if (current_screen == SCREEN_PAUSED) {
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
            modal_dialog_show(&pause_dialog);
            return;
        }
    }
    
    // Move player paddle to touch position
    if ((state.held || state.pressed) && !game.game_over && !game.paused) {
        if (portrait_mode) {
            // Portrait: move paddle horizontally using touch X
            int relative_x = state.x - offset_x;
            game.player.y = relative_x - PADDLE_HEIGHT / 2;
            
            if (game.player.y < 0) game.player.y = 0;
            if (game.player.y > play_area_width - PADDLE_HEIGHT) {
                game.player.y = play_area_width - PADDLE_HEIGHT;
            }
        } else {
            // Landscape: move paddle vertically using touch Y
            int relative_y = state.y - offset_y;
            game.player.y = relative_y - PADDLE_HEIGHT / 2;
            
            if (game.player.y < 0) game.player.y = 0;
            if (game.player.y > play_area_height - PADDLE_HEIGHT) {
                game.player.y = play_area_height - PADDLE_HEIGHT;
            }
        }
    }
}

// Draw the playing field (paddles, ball, scores, borders) — used as
// background for PLAYING, PAUSED, and GAME_OVER screens.
static void draw_playing_field(void) {
    // Draw HUD scores — both at top with labels, using text_draw_centered
    if (portrait_mode) {
        // Portrait: AI score on left, Player score on right, both at top
        char ai_score[16];
        snprintf(ai_score, sizeof(ai_score), "AI: %d", game.ai.score);
        text_draw_centered(&fb, fb.width / 3, 35, ai_score, COLOR_RED, 3);
        
        char player_score[16];
        snprintf(player_score, sizeof(player_score), "YOU: %d", game.player.score);
        text_draw_centered(&fb, fb.width * 2 / 3, 35, player_score, COLOR_GREEN, 3);
    } else {
        // Landscape: player score left, AI score right
        char player_score[16];
        snprintf(player_score, sizeof(player_score), "YOU: %d", game.player.score);
        text_draw_centered(&fb, fb.width / 3, 35, player_score, COLOR_GREEN, 3);
        
        char ai_score[16];
        snprintf(ai_score, sizeof(ai_score), "AI: %d", game.ai.score);
        text_draw_centered(&fb, fb.width * 2 / 3, 35, ai_score, COLOR_RED, 3);
    }
    
    // Draw menu and exit buttons
    draw_menu_button(&fb, &menu_button);
    draw_exit_button(&fb, &exit_button);
    
    // Draw play area border
    fb_draw_rect(&fb, offset_x - 2, offset_y - 2,
                 play_area_width + 4, play_area_height + 4, COLOR_WHITE);
    
    if (portrait_mode) {
        // Portrait: horizontal center line
        for (int x = 0; x < play_area_width; x += 20) {
            fb_fill_rect(&fb, offset_x + x, offset_y + play_area_height / 2 - 2, 10, 4, RGB(128, 128, 128));
        }
        
        // Draw AI paddle (top, red) — horizontal paddle
        fb_fill_rect(&fb, offset_x + (int)game.ai.y, offset_y + 5,
                     PADDLE_HEIGHT, PADDLE_WIDTH, COLOR_RED);
        
        // Draw player paddle (bottom, green) — horizontal paddle
        fb_fill_rect(&fb, offset_x + (int)game.player.y,
                     offset_y + play_area_height - PADDLE_WIDTH - 5,
                     PADDLE_HEIGHT, PADDLE_WIDTH, COLOR_GREEN);
        
        // Draw controls hint (centered)
        text_draw_centered(&fb, fb.width / 2, fb.height - 18,
                          "TOUCH TO MOVE PADDLE", RGB(100, 100, 100), 1);
    } else {
        // Landscape: vertical center line
        for (int y = 0; y < play_area_height; y += 20) {
            fb_fill_rect(&fb, offset_x + play_area_width / 2 - 2, offset_y + y, 4, 10, RGB(128, 128, 128));
        }
        
        // Draw player paddle (left, green)
        fb_fill_rect(&fb, offset_x + 5, offset_y + (int)game.player.y,
                     PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_GREEN);
        
        // Draw AI paddle (right, red)
        fb_fill_rect(&fb, offset_x + play_area_width - PADDLE_WIDTH - 5,
                     offset_y + (int)game.ai.y, PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_RED);
        
        // Draw controls hint (centered)
        text_draw_centered(&fb, fb.width / 2, fb.height - 18,
                          "TOUCH TO MOVE PADDLE", RGB(100, 100, 100), 1);
    }
    
    // Draw ball (same for both orientations)
    fb_fill_circle(&fb, offset_x + (int)game.ball.x + BALL_SIZE / 2,
                   offset_y + (int)game.ball.y + BALL_SIZE / 2,
                   BALL_SIZE / 2, COLOR_WHITE);
}

void draw_game() {
    fb_clear(&fb, COLOR_BLACK);
    
    // Handle welcome screen — custom drawing for properly centered multi-line text
    if (current_screen == SCREEN_WELCOME) {
        text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 70, "PONG", COLOR_CYAN, 4);
        text_draw_centered(&fb, fb.width / 2, fb.height / 2 - 10,
                          "TOUCH TO MOVE PADDLE", COLOR_WHITE, 1);
        text_draw_centered(&fb, fb.width / 2, fb.height / 2 + 10,
                          "FIRST TO 11 WINS", COLOR_WHITE, 1);
        button_draw(&fb, &start_button);
        return;
    }
    
    // Draw the playing field as background (used by PLAYING, PAUSED, GAME_OVER)
    draw_playing_field();
    
    // Handle pause screen overlay
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
            current_screen = SCREEN_PLAYING;
            break;
        case GAMEOVER_ACTION_EXIT:
            running = false;
            break;
        case GAMEOVER_ACTION_RESET_SCORES:
            /* Handled internally by the component */
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
    int lock_fd = acquire_instance_lock("pong");
    if (lock_fd < 0) {
        fprintf(stderr, "pong: another instance is already running\n");
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
    hw_set_backlight(100);
    audio_close(&audio);
    fb_close(&fb);
    
    printf("Pong ended.\n");
    return 0;
}
