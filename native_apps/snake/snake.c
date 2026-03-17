/*
 * Snake Game - Native C Implementation
 * Optimized for 300MHz ARM with touchscreen
 * No browser overhead - direct framebuffer rendering
 * Features: LED effects, screen transitions
 * Supports keyboard, gamepad, and touch input via unified gamepad module
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

#define GRID_SIZE 20
#define MAX_SNAKE_LENGTH 400
#define INITIAL_SPEED 150000  // microseconds per frame

typedef enum {
    SCREEN_WELCOME,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_GAME_OVER
} GameScreen;

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
GamepadManager gamepad;
InputState input;
Snake snake;
Food food;
GameState game;
LEDEffect led_effect;
int cell_size;
int grid_offset_x;
int grid_offset_y;
bool running = true;
GameScreen current_screen = SCREEN_WELCOME;
HighScoreTable hs_table;
static GameOverScreen gos;
Audio audio;
static uint32_t last_rescan_ms = 0;
#define RESCAN_INTERVAL_MS 5000

// UI Buttons
Button menu_button;
Button exit_button;
Button start_button;
ModalDialog pause_dialog;

// Function prototypes
void init_game();
void reset_game();
void spawn_food();
void update_snake();
void draw_playing_field();
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
    
    // Initialize LED effect state
    led_effect.active = false;
    led_effect.type = 0;
    
    hs_init(&hs_table, "snake");
    hs_load(&hs_table);
    game.high_score = hs_table.count > 0 ? hs_table.entries[0].score : 0;

    // Set up touch regions for gamepad module (4 directional zones + action buttons)
    int grid_cx = grid_offset_x + (GRID_SIZE * cell_size) / 2;
    int grid_cy = grid_offset_y + (GRID_SIZE * cell_size) / 2;
    int half_w = (GRID_SIZE * cell_size) / 2;
    int half_h = (GRID_SIZE * cell_size) / 2;
    TouchRegion touch_regions[] = {
        /* Up zone: top half of grid, center strip */
        { grid_cx - half_w, grid_offset_y, half_w * 2, half_h, BTN_ID_UP },
        /* Down zone: bottom half of grid, center strip */
        { grid_cx - half_w, grid_cy, half_w * 2, half_h, BTN_ID_DOWN },
        /* Left zone: left half of grid */
        { grid_offset_x, grid_offset_y, half_w, half_h * 2, BTN_ID_LEFT },
        /* Right zone: right half of grid */
        { grid_cx, grid_offset_y, half_w, half_h * 2, BTN_ID_RIGHT },
    };
    gamepad_set_touch_regions(&gamepad, touch_regions, 4);

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
        current_screen = SCREEN_GAME_OVER;
        gameover_init(&gos, &fb, game.score, NULL, NULL, &hs_table, &touch);
        audio_fail(&audio);   // Game-over sound (~600ms)
        start_led_effect(3);  // Start death effect
        return;
    }

    // Check self collision
    for (int i = 0; i < snake.length; i++) {
        if (snake.body[i].x == new_head.x && snake.body[i].y == new_head.y) {
            game.game_over = true;
            current_screen = SCREEN_GAME_OVER;
            gameover_init(&gos, &fb, game.score, NULL, NULL, &hs_table, &touch);
            audio_fail(&audio);   // Game-over sound (~600ms)
            start_led_effect(3);  // Start death effect
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
        audio_interrupt(&audio);
        audio_tone(&audio, 1800, 60);  // ti-
            audio_tone(&audio, 2400, 60);  // -ti
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
        // Gamepad/keyboard: start game with Jump, Action, or Pause
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
            fb_fade_out(&fb);
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
            fb_fade_out(&fb);
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
        
        // Touch controls for direction (relative to snake head)
        Point head = snake.body[0];
        int head_screen_x = grid_offset_x + head.x * cell_size + cell_size / 2;
        int head_screen_y = grid_offset_y + head.y * cell_size + cell_size / 2;
        
        int dx = state.x - head_screen_x;
        int dy = state.y - head_screen_y;
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

    // Gamepad/keyboard direction input (use pressed edge, not held, to prevent rapid turns)
    if (input.buttons[BTN_ID_UP].pressed && snake.direction != DIR_DOWN) {
        snake.next_direction = DIR_UP;
    } else if (input.buttons[BTN_ID_DOWN].pressed && snake.direction != DIR_UP) {
        snake.next_direction = DIR_DOWN;
    } else if (input.buttons[BTN_ID_LEFT].pressed && snake.direction != DIR_RIGHT) {
        snake.next_direction = DIR_LEFT;
    } else if (input.buttons[BTN_ID_RIGHT].pressed && snake.direction != DIR_LEFT) {
        snake.next_direction = DIR_RIGHT;
    }
}

void draw_playing_field() {
    // Draw HUD
    char score_text[32];
    snprintf(score_text, sizeof(score_text), "SCORE: %d", game.score);
    text_draw_centered(&fb, fb.width / 2, 28, score_text, COLOR_WHITE, 2);
    
    char high_text[32];
    snprintf(high_text, sizeof(high_text), "HIGH: %d", game.high_score);
    text_draw_centered(&fb, fb.width / 2, 53, high_text, COLOR_YELLOW, 2);
    
    // Draw menu and exit buttons
    draw_menu_button(&fb, &menu_button);
    draw_exit_button(&fb, &exit_button);
    
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
    if (!input.gamepad_connected && !input.keyboard_connected)
        fb_draw_text(&fb, 10, fb.height - 25, "TAP DIRECTION TO MOVE", RGB(100, 100, 100), 1);
    else
        fb_draw_text(&fb, 10, fb.height - 25, "ARROWS/D-PAD: MOVE  ESC: PAUSE", RGB(100, 100, 100), 1);
}

void draw_game() {
    // Clear screen
    fb_clear(&fb, COLOR_BLACK);
    
    // Handle welcome screen
    if (current_screen == SCREEN_WELCOME) {
        draw_welcome_screen(&fb, "SNAKE",
            "D-PAD/ARROWS: MOVE\n"
            "EAT FOOD TO GROW\n"
            "PRESS START OR TAP TO BEGIN", &start_button);
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
    
    // Allow custom devices
    if (argc > 1) fb_device = argv[1];
    if (argc > 2) touch_device = argv[2];
    
    // Singleton guard — prevent duplicate instances
    int lock_fd = acquire_instance_lock("snake");
    if (lock_fd < 0) {
        fprintf(stderr, "snake: another instance is already running\n");
        return 1;
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize hardware control
    hw_init();
    hw_set_backlight(100);
    hw_leds_off();  // Start with LEDs off
    audio_init(&audio);  // Initialize audio (non-fatal if unavailable)
    
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
    
    // Gamepad init
    gamepad_init(&gamepad);

    // Game loop
    while (running) {
        handle_input();
        if (current_screen == SCREEN_PLAYING)
            update_snake();
        update_led_effects();  // Update LED effects (non-blocking)
        draw_game();
        fb_swap(&fb);  // Present back buffer to screen
        
        usleep(game.speed);
    }
    
    // Cleanup
    hw_leds_off();
    audio_close(&audio);
    gamepad_close(&gamepad);
    touch_close(&touch);
    fb_close(&fb);
    
    printf("Snake game ended. Final score: %d\n", game.score);
    return 0;
}
