/*
 * SameGame - Native C Implementation for RoomWizard
 * Puzzle game: remove groups of connected same-colored blocks
 * Optimized for 300MHz ARM with touchscreen
 * Direct framebuffer rendering with animations
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
#include "../common/highscore.h"
#include "../common/audio.h"

/* ========================================================================== */
/* CONSTANTS                                                                  */
/* ========================================================================== */

#define MAX_COLS 30
#define MAX_ROWS 25
#define NUM_COLORS 5
#define BLOCK_EMPTY 0
/* Colors stored as 1..NUM_COLORS in the grid */

#define TARGET_BLOCK_SIZE 30
#define HUD_HEIGHT 70
#define GRID_PADDING 4
#define PERFECT_CLEAR_BONUS 1000

/* Animation durations (ms) */
#define ANIM_REMOVE_DURATION  150
#define ANIM_FALL_DURATION    200
#define ANIM_SLIDE_DURATION   200
#define ANIM_SHAKE_DURATION   200

/* Frame timing */
#define FRAME_DELAY_US 16666  /* ~60 FPS (16.67ms) */

/* Block colors (accessible, distinct palette) */
#define BLOCK_COLOR_RED     RGB(220, 50, 50)
#define BLOCK_COLOR_BLUE    RGB(50, 100, 220)
#define BLOCK_COLOR_GREEN   RGB(50, 180, 50)
#define BLOCK_COLOR_YELLOW  RGB(230, 200, 30)
#define BLOCK_COLOR_PURPLE  RGB(170, 60, 200)

/* Darkened versions for block borders / depth */
#define BLOCK_COLOR_RED_DK     RGB(160, 30, 30)
#define BLOCK_COLOR_BLUE_DK    RGB(30, 70, 160)
#define BLOCK_COLOR_GREEN_DK   RGB(30, 130, 30)
#define BLOCK_COLOR_YELLOW_DK  RGB(170, 150, 10)
#define BLOCK_COLOR_PURPLE_DK  RGB(120, 30, 150)

/* Bright versions for highlight glow */
#define BLOCK_COLOR_RED_HI     RGB(255, 120, 120)
#define BLOCK_COLOR_BLUE_HI    RGB(120, 170, 255)
#define BLOCK_COLOR_GREEN_HI   RGB(120, 255, 120)
#define BLOCK_COLOR_YELLOW_HI  RGB(255, 245, 120)
#define BLOCK_COLOR_PURPLE_HI  RGB(220, 140, 255)

/* ========================================================================== */
/* ENUMERATIONS                                                               */
/* ========================================================================== */

typedef enum {
    SCREEN_WELCOME,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_GAME_OVER
} GameScreen;

typedef enum {
    ANIM_NONE,
    ANIM_REMOVING,
    ANIM_FALLING,
    ANIM_SLIDING
} AnimState;

/* ========================================================================== */
/* DATA STRUCTURES                                                            */
/* ========================================================================== */

typedef struct {
    /* Grid data — 0=empty, 1..5=color */
    int grid[MAX_COLS][MAX_ROWS];
    int cols;
    int rows;
    int block_size;
    int grid_x, grid_y;   /* top-left pixel of grid */

    /* Score */
    int score;
    int blocks_remaining;
    int total_blocks;

    /* Highlight (first-click selection) */
    bool highlight_active;
    bool highlight_map[MAX_COLS][MAX_ROWS];
    int highlight_count;
    int highlight_color;

    /* Animation */
    AnimState anim_state;
    uint32_t anim_start;
    /* For removal animation: store which blocks are being removed */
    bool removing_map[MAX_COLS][MAX_ROWS];
    int removing_count;
    /* For falling animation: store target row for each block */
    int fall_target[MAX_COLS][MAX_ROWS];  /* target row for block at [c][r], -1 = no move */
    int fall_origin[MAX_COLS][MAX_ROWS];  /* origin row */
    /* For sliding animation: store target column for each column */
    int slide_target[MAX_COLS];           /* target col for column c, -1 = no move */
    bool needs_slide;

    /* Screen shake */
    bool shake_active;
    uint32_t shake_start;
    int shake_intensity;

    /* Flash effect for single-block tap */
    bool flash_active;
    uint32_t flash_start;
    int flash_col, flash_row;

    /* Perfect clear */
    bool perfect_clear;
    uint32_t perfect_clear_start;
} SameGameState;

/* LED effect state */
typedef struct {
    bool active;
    int type;   /* 0=none, 1=remove, 2=big_remove, 3=perfect, 4=gameover */
    uint32_t start_time;
} LEDEffect;

/* ========================================================================== */
/* GLOBALS                                                                    */
/* ========================================================================== */

static Framebuffer fb;
static TouchInput touch;
static Audio audio;
static SameGameState game;
static LEDEffect led_effect;
static bool running = true;
static GameScreen current_screen = SCREEN_WELCOME;
static HighScoreTable hs_table;
static GameOverScreen gos;

/* UI Buttons */
static Button menu_button;
static Button exit_button;
static Button start_button;
static Button resume_button;
static Button exit_pause_button;

/* Color lookup tables */
static const uint32_t block_colors[NUM_COLORS] = {
    BLOCK_COLOR_RED, BLOCK_COLOR_BLUE, BLOCK_COLOR_GREEN,
    BLOCK_COLOR_YELLOW, BLOCK_COLOR_PURPLE
};
static const uint32_t block_colors_dark[NUM_COLORS] = {
    BLOCK_COLOR_RED_DK, BLOCK_COLOR_BLUE_DK, BLOCK_COLOR_GREEN_DK,
    BLOCK_COLOR_YELLOW_DK, BLOCK_COLOR_PURPLE_DK
};
static const uint32_t block_colors_highlight[NUM_COLORS] = {
    BLOCK_COLOR_RED_HI, BLOCK_COLOR_BLUE_HI, BLOCK_COLOR_GREEN_HI,
    BLOCK_COLOR_YELLOW_HI, BLOCK_COLOR_PURPLE_HI
};

/* ========================================================================== */
/* FORWARD DECLARATIONS                                                       */
/* ========================================================================== */

static void signal_handler(int sig);
static void init_layout(void);
static void init_game(void);
static void reset_game(void);
static void handle_input(void);
static void update_game(void);
static void draw_game(void);

static void flood_fill(int col, int row, int color, bool map[MAX_COLS][MAX_ROWS], int *count);
static void compute_highlight(int col, int row);
static void remove_highlighted(void);
static void apply_gravity(void);
static void apply_column_collapse(void);
static bool check_game_over(void);

static void start_remove_anim(void);
static void start_fall_anim(void);
static void start_slide_anim(void);
static void update_animations(void);
static void post_move_check(void);

static void start_shake(int group_size);
static void update_shake(void);
static void start_led_effect(int type);
static void update_led_effects(void);

static void draw_playing_field(void);
static void draw_block(int col, int row, bool highlighted, float alpha);
static void draw_hud(void);

static uint32_t color_lerp(uint32_t a, uint32_t b, float t);

/* ========================================================================== */
/* SIGNAL HANDLER                                                             */
/* ========================================================================== */

static void signal_handler(int sig) {
    (void)sig;
    running = false;
}

/* ========================================================================== */
/* UTILITY                                                                    */
/* ========================================================================== */

static uint32_t color_lerp(uint32_t a, uint32_t b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int rr = ar + (int)((br - ar) * t);
    int rg = ag + (int)((bg - ag) * t);
    int rb = ab + (int)((bb - ab) * t);
    return RGB(rr, rg, rb);
}

/* ========================================================================== */
/* LAYOUT INITIALIZATION                                                      */
/* ========================================================================== */

static void init_layout(void) {
    /* Calculate grid dimensions based on screen size */
    int safe_left = SCREEN_SAFE_LEFT;
    int safe_top = SCREEN_SAFE_TOP;
    int safe_w = SCREEN_SAFE_WIDTH;
    int safe_h = SCREEN_SAFE_HEIGHT;

    /* Usable area: safe area minus HUD at top and small padding */
    int usable_w = safe_w - GRID_PADDING * 2;
    int usable_h = safe_h - HUD_HEIGHT - GRID_PADDING * 2;

    /* Calculate block size and grid dimensions */
    int bs = TARGET_BLOCK_SIZE;
    int cols = usable_w / bs;
    int rows = usable_h / bs;

    /* Clamp to maximums */
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (cols < 5) cols = 5;
    if (rows < 5) rows = 5;

    /* Recalculate block size to fill available space nicely */
    int bs_w = usable_w / cols;
    int bs_h = usable_h / rows;
    bs = (bs_w < bs_h) ? bs_w : bs_h;
    if (bs < 10) bs = 10;

    game.cols = cols;
    game.rows = rows;
    game.block_size = bs;

    /* Center grid horizontally, position below HUD */
    int grid_total_w = cols * bs;
    int grid_total_h = rows * bs;
    game.grid_x = safe_left + (safe_w - grid_total_w) / 2;
    game.grid_y = safe_top + HUD_HEIGHT + (usable_h - grid_total_h) / 2 + GRID_PADDING;

    /* Initialize buttons */
    button_init(&menu_button, LAYOUT_MENU_BTN_X, LAYOUT_MENU_BTN_Y,
                BTN_MENU_WIDTH, BTN_MENU_HEIGHT, "",
                BTN_MENU_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&exit_button, LAYOUT_EXIT_BTN_X, LAYOUT_EXIT_BTN_Y,
                BTN_EXIT_WIDTH, BTN_EXIT_HEIGHT, "",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&start_button, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                fb.height / 2 + 40, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "TAP TO START",
                BTN_START_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&resume_button, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                fb.height / 2, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "RESUME",
                BTN_RESUME_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&exit_pause_button, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                fb.height / 2 + 80, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "EXIT",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
}

/* ========================================================================== */
/* GAME INITIALIZATION                                                        */
/* ========================================================================== */

static void init_game(void) {
    init_layout();

    /* Initialize LED state */
    led_effect.active = false;
    led_effect.type = 0;

    /* Load high scores */
    hs_init(&hs_table, "samegame");
    hs_load(&hs_table);

    reset_game();
}

static void reset_game(void) {
    int c, r;

    /* Fill grid with random colors (1..NUM_COLORS) */
    game.total_blocks = game.cols * game.rows;
    game.blocks_remaining = game.total_blocks;
    game.score = 0;

    for (c = 0; c < game.cols; c++) {
        for (r = 0; r < game.rows; r++) {
            game.grid[c][r] = (rand() % NUM_COLORS) + 1;
        }
    }

    /* Clear state */
    game.highlight_active = false;
    game.highlight_count = 0;
    game.highlight_color = 0;
    memset(game.highlight_map, 0, sizeof(game.highlight_map));
    memset(game.removing_map, 0, sizeof(game.removing_map));
    game.removing_count = 0;

    game.anim_state = ANIM_NONE;
    game.anim_start = 0;
    game.needs_slide = false;

    game.shake_active = false;
    game.shake_intensity = 0;

    game.flash_active = false;
    game.perfect_clear = false;

    /* Initialize fall/slide arrays */
    for (c = 0; c < MAX_COLS; c++) {
        game.slide_target[c] = -1;
        for (r = 0; r < MAX_ROWS; r++) {
            game.fall_target[c][r] = -1;
            game.fall_origin[c][r] = -1;
        }
    }
}

/* ========================================================================== */
/* FLOOD FILL                                                                 */
/* ========================================================================== */

static void flood_fill(int col, int row, int color,
                       bool map[MAX_COLS][MAX_ROWS], int *count) {
    /* Bounds check */
    if (col < 0 || col >= game.cols || row < 0 || row >= game.rows)
        return;
    /* Already visited or wrong color */
    if (map[col][row])
        return;
    if (game.grid[col][row] != color)
        return;

    map[col][row] = true;
    (*count)++;

    /* Recurse in 4 directions (no diagonals) */
    flood_fill(col - 1, row, color, map, count);
    flood_fill(col + 1, row, color, map, count);
    flood_fill(col, row - 1, color, map, count);
    flood_fill(col, row + 1, color, map, count);
}

/* ========================================================================== */
/* HIGHLIGHT (First-click selection)                                          */
/* ========================================================================== */

static void compute_highlight(int col, int row) {
    int color;

    /* Clear old highlight */
    memset(game.highlight_map, 0, sizeof(game.highlight_map));
    game.highlight_count = 0;
    game.highlight_active = false;
    game.highlight_color = 0;

    /* Bounds check */
    if (col < 0 || col >= game.cols || row < 0 || row >= game.rows)
        return;

    color = game.grid[col][row];
    if (color == BLOCK_EMPTY)
        return;

    /* Flood fill to find connected group */
    flood_fill(col, row, color, game.highlight_map, &game.highlight_count);

    if (game.highlight_count >= 2) {
        game.highlight_active = true;
        game.highlight_color = color;
        /* Audio: selection click */
        audio_beep(&audio);
    } else {
        /* Single block — can't remove. Flash it briefly. */
        memset(game.highlight_map, 0, sizeof(game.highlight_map));
        game.highlight_count = 0;
        game.flash_active = true;
        game.flash_start = get_time_ms();
        game.flash_col = col;
        game.flash_row = row;
    }
}

/* ========================================================================== */
/* BLOCK REMOVAL                                                              */
/* ========================================================================== */

static void remove_highlighted(void) {
    int c, r;
    int n = game.highlight_count;

    if (!game.highlight_active || n < 2)
        return;

    /* Calculate score: (n-1)^2 */
    int points = (n - 1) * (n - 1);
    game.score += points;

    /* Copy highlight map to removing map for animation */
    memcpy(game.removing_map, game.highlight_map, sizeof(game.removing_map));
    game.removing_count = n;

    /* Clear highlight */
    game.highlight_active = false;
    memset(game.highlight_map, 0, sizeof(game.highlight_map));
    game.highlight_count = 0;
    game.highlight_color = 0;

    /* Play removal sound — pitch varies with group size */
    audio_interrupt(&audio);
    int freq = 600 + n * 80;
    if (freq > 2400) freq = 2400;
    audio_tone(&audio, freq, 60);

    /* Screen shake for 3+ blocks */
    if (n >= 3) {
        start_shake(n);
    }

    /* LED effect */
    if (n >= 8) {
        start_led_effect(2);  /* Big removal */
    } else {
        start_led_effect(1);  /* Normal removal */
    }

    /* Start removal animation */
    start_remove_anim();
}

/* Actually remove blocks from grid (called after removal animation finishes) */
static void finalize_removal(void) {
    int c, r;

    for (c = 0; c < game.cols; c++) {
        for (r = 0; r < game.rows; r++) {
            if (game.removing_map[c][r]) {
                game.grid[c][r] = BLOCK_EMPTY;
                game.blocks_remaining--;
            }
        }
    }
    memset(game.removing_map, 0, sizeof(game.removing_map));
    game.removing_count = 0;
}

/* ========================================================================== */
/* GRAVITY (blocks fall down)                                                 */
/* ========================================================================== */

static void apply_gravity(void) {
    int c, r;

    /* Reset fall tracking */
    for (c = 0; c < MAX_COLS; c++)
        for (r = 0; r < MAX_ROWS; r++) {
            game.fall_target[c][r] = -1;
            game.fall_origin[c][r] = -1;
        }

    /* For each column, compact blocks downward */
    for (c = 0; c < game.cols; c++) {
        int write_row = game.rows - 1;  /* Bottom of column */

        /* Scan from bottom to top */
        for (r = game.rows - 1; r >= 0; r--) {
            if (game.grid[c][r] != BLOCK_EMPTY) {
                if (r != write_row) {
                    /* This block needs to fall from r to write_row */
                    game.fall_target[c][write_row] = game.grid[c][r];
                    game.fall_origin[c][write_row] = r;
                }
                write_row--;
            }
        }
    }
}

/* Finalize gravity — actually move blocks in grid */
static void finalize_gravity(void) {
    int c, r;

    for (c = 0; c < game.cols; c++) {
        int write_row = game.rows - 1;
        int temp[MAX_ROWS];
        int idx = 0;

        /* Collect non-empty blocks from bottom to top */
        for (r = game.rows - 1; r >= 0; r--) {
            if (game.grid[c][r] != BLOCK_EMPTY) {
                temp[idx++] = game.grid[c][r];
            }
        }

        /* Place them at the bottom of the column */
        for (r = game.rows - 1; r >= 0; r--) {
            int pos = game.rows - 1 - r;
            if (pos < idx) {
                game.grid[c][r] = temp[pos];
            } else {
                game.grid[c][r] = BLOCK_EMPTY;
            }
        }
    }
}

/* ========================================================================== */
/* COLUMN COLLAPSE (slide left to fill empty columns)                         */
/* ========================================================================== */

static void apply_column_collapse(void) {
    int c;

    /* Reset slide tracking */
    for (c = 0; c < MAX_COLS; c++)
        game.slide_target[c] = -1;

    game.needs_slide = false;

    /* Check which columns are empty (bottom cell is empty means whole column is empty
       after gravity has been applied) */
    int write_col = 0;

    for (c = 0; c < game.cols; c++) {
        /* Check if column has any blocks */
        bool has_blocks = false;
        for (int r = 0; r < game.rows; r++) {
            if (game.grid[c][r] != BLOCK_EMPTY) {
                has_blocks = true;
                break;
            }
        }

        if (has_blocks) {
            if (c != write_col) {
                game.slide_target[c] = write_col;
                game.needs_slide = true;
            }
            write_col++;
        }
    }
}

/* Finalize column collapse — actually rearrange columns in grid */
static void finalize_column_collapse(void) {
    int c, r;
    int temp_grid[MAX_COLS][MAX_ROWS];

    /* Initialize temp grid as empty */
    memset(temp_grid, 0, sizeof(temp_grid));

    int write_col = 0;
    for (c = 0; c < game.cols; c++) {
        /* Check if column has any blocks */
        bool has_blocks = false;
        for (r = 0; r < game.rows; r++) {
            if (game.grid[c][r] != BLOCK_EMPTY) {
                has_blocks = true;
                break;
            }
        }

        if (has_blocks) {
            for (r = 0; r < game.rows; r++) {
                temp_grid[write_col][r] = game.grid[c][r];
            }
            write_col++;
        }
    }

    /* Copy back */
    memcpy(game.grid, temp_grid, sizeof(game.grid));
}

/* ========================================================================== */
/* GAME OVER DETECTION                                                        */
/* ========================================================================== */

static bool check_game_over(void) {
    int c, r;

    for (c = 0; c < game.cols; c++) {
        for (r = 0; r < game.rows; r++) {
            int color = game.grid[c][r];
            if (color == BLOCK_EMPTY) continue;

            /* Check right neighbor */
            if (c + 1 < game.cols && game.grid[c + 1][r] == color)
                return false;
            /* Check down neighbor */
            if (r + 1 < game.rows && game.grid[c][r + 1] == color)
                return false;
        }
    }

    /* No adjacent same-colored pairs found */
    return true;
}

/* ========================================================================== */
/* ANIMATION SYSTEM                                                           */
/* ========================================================================== */

static void start_remove_anim(void) {
    game.anim_state = ANIM_REMOVING;
    game.anim_start = get_time_ms();
}

static void start_fall_anim(void) {
    apply_gravity();

    /* Check if any blocks actually need to fall */
    bool any_fall = false;
    for (int c = 0; c < game.cols && !any_fall; c++)
        for (int r = 0; r < game.rows && !any_fall; r++)
            if (game.fall_origin[c][r] >= 0)
                any_fall = true;

    if (any_fall) {
        game.anim_state = ANIM_FALLING;
        game.anim_start = get_time_ms();
    } else {
        /* Skip to column collapse */
        finalize_gravity();
        start_slide_anim();
    }
}

static void start_slide_anim(void) {
    apply_column_collapse();

    if (game.needs_slide) {
        game.anim_state = ANIM_SLIDING;
        game.anim_start = get_time_ms();
    } else {
        /* All animations complete — check game state */
        game.anim_state = ANIM_NONE;
        post_move_check();
    }
}

/* Called after all animations complete */
static void post_move_check(void) {
    /* Check for perfect clear */
    if (game.blocks_remaining == 0) {
        game.score += PERFECT_CLEAR_BONUS;
        game.perfect_clear = true;
        game.perfect_clear_start = get_time_ms();
        start_led_effect(3);  /* Perfect clear LED */
        audio_success(&audio);
    }

    /* Check for game over */
    if (game.blocks_remaining == 0 || check_game_over()) {
        /* Small delay before showing game over screen */
        uint32_t delay_until = get_time_ms() + (game.perfect_clear ? 1500 : 300);
        while (running && get_time_ms() < delay_until) {
            /* Keep rendering during delay */
            update_shake();
            update_led_effects();
            fb_clear(&fb, COLOR_BLACK);
            draw_playing_field();
            fb_swap(&fb);
            usleep(FRAME_DELAY_US);
        }

        if (!game.perfect_clear) {
            audio_fail(&audio);
            start_led_effect(4);  /* Game over LED */
        }

        current_screen = SCREEN_GAME_OVER;
        gameover_init(&gos, &fb, game.score, NULL, NULL, &hs_table, &touch);
    }
}

static void update_animations(void) {
    uint32_t now = get_time_ms();
    uint32_t elapsed;

    switch (game.anim_state) {
    case ANIM_REMOVING:
        elapsed = now - game.anim_start;
        if (elapsed >= ANIM_REMOVE_DURATION) {
            /* Removal animation finished — apply removal and start gravity */
            finalize_removal();
            start_fall_anim();
        }
        break;

    case ANIM_FALLING:
        elapsed = now - game.anim_start;
        if (elapsed >= ANIM_FALL_DURATION) {
            /* Falling animation finished — finalize positions and check columns */
            finalize_gravity();
            start_slide_anim();
        }
        break;

    case ANIM_SLIDING:
        elapsed = now - game.anim_start;
        if (elapsed >= ANIM_SLIDE_DURATION) {
            /* Sliding animation finished — finalize columns */
            finalize_column_collapse();
            game.anim_state = ANIM_NONE;
            post_move_check();
        }
        break;

    case ANIM_NONE:
    default:
        break;
    }
}

/* ========================================================================== */
/* SCREEN SHAKE                                                               */
/* ========================================================================== */

static void start_shake(int group_size) {
    game.shake_active = true;
    game.shake_start = get_time_ms();
    /* Intensity scales with group size, capped at 8 pixels */
    game.shake_intensity = group_size - 1;
    if (game.shake_intensity > 8) game.shake_intensity = 8;
    if (game.shake_intensity < 2) game.shake_intensity = 2;
}

static void update_shake(void) {
    if (!game.shake_active) {
        fb_clear_draw_offset(&fb);
        return;
    }

    uint32_t elapsed = get_time_ms() - game.shake_start;
    if (elapsed >= ANIM_SHAKE_DURATION) {
        game.shake_active = false;
        fb_clear_draw_offset(&fb);
        return;
    }

    /* Decay intensity over time */
    float progress = (float)elapsed / ANIM_SHAKE_DURATION;
    float decay = 1.0f - progress;
    int intensity = (int)(game.shake_intensity * decay);

    if (intensity > 0) {
        int dx = (rand() % (intensity * 2 + 1)) - intensity;
        int dy = (rand() % (intensity * 2 + 1)) - intensity;
        fb_set_draw_offset(&fb, dx, dy);
    } else {
        fb_clear_draw_offset(&fb);
    }
}

/* ========================================================================== */
/* LED EFFECTS                                                                */
/* ========================================================================== */

static void start_led_effect(int type) {
    led_effect.active = true;
    led_effect.type = type;
    led_effect.start_time = get_time_ms();
}

static void update_led_effects(void) {
    if (!led_effect.active) return;

    uint32_t elapsed = get_time_ms() - led_effect.start_time;

    switch (led_effect.type) {
    case 1:  /* Normal removal — quick green flash */
        if (elapsed < 100) {
            hw_set_leds(HW_LED_COLOR_GREEN);
        } else {
            hw_leds_off();
            led_effect.active = false;
        }
        break;

    case 2:  /* Big removal — yellow flash */
        if (elapsed < 200) {
            hw_set_leds(HW_LED_COLOR_YELLOW);
        } else {
            hw_leds_off();
            led_effect.active = false;
        }
        break;

    case 3:  /* Perfect clear — green/yellow alternating */
        {
            int phase = elapsed / 150;
            if (phase < 6) {
                if (phase % 2 == 0)
                    hw_set_leds(HW_LED_COLOR_GREEN);
                else
                    hw_set_leds(HW_LED_COLOR_YELLOW);
            } else {
                hw_leds_off();
                led_effect.active = false;
            }
        }
        break;

    case 4:  /* Game over — red pulsing */
        {
            int pulse = elapsed / 200;
            int phase = elapsed % 200;
            if (pulse < 3) {
                if (phase < 100)
                    hw_set_leds(HW_LED_COLOR_RED);
                else
                    hw_leds_off();
            } else {
                hw_leds_off();
                led_effect.active = false;
            }
        }
        break;

    default:
        led_effect.active = false;
        break;
    }
}

/* ========================================================================== */
/* DRAWING                                                                    */
/* ========================================================================== */

static void draw_block(int col, int row, bool highlighted, float alpha) {
    int color_idx;
    int bs = game.block_size;
    int px = game.grid_x + col * bs;
    int py = game.grid_y + row * bs;
    int inset = 1;  /* Gap between blocks */

    color_idx = game.grid[col][row];
    if (color_idx < 1 || color_idx > NUM_COLORS) return;
    color_idx--;  /* Convert to 0-based index */

    uint32_t base_color = block_colors[color_idx];
    uint32_t dark_color = block_colors_dark[color_idx];
    uint32_t hi_color = block_colors_highlight[color_idx];

    if (alpha < 1.0f) {
        /* Fade-out during removal animation */
        uint8_t a = (uint8_t)(alpha * 200);
        fb_fill_rect_alpha(&fb, px + inset, py + inset,
                           bs - inset * 2, bs - inset * 2, base_color, a);
        return;
    }

    if (highlighted) {
        /* Highlighted block: use bright color with glowing border */
        uint32_t now = get_time_ms();
        /* Pulse effect: oscillate brightness */
        float pulse = 0.5f + 0.5f * sinf((float)(now % 600) / 600.0f * 2.0f * 3.14159f);
        uint32_t fill = color_lerp(base_color, hi_color, pulse);

        fb_fill_rect(&fb, px + inset, py + inset,
                     bs - inset * 2, bs - inset * 2, fill);
        /* Bright outline */
        fb_draw_rect(&fb, px + inset, py + inset,
                     bs - inset * 2, bs - inset * 2, COLOR_WHITE);
    } else {
        /* Normal block: fill with slight 3D effect */
        fb_fill_rect(&fb, px + inset, py + inset,
                     bs - inset * 2, bs - inset * 2, base_color);

        /* Top/left highlight edge (lighter) */
        fb_fill_rect(&fb, px + inset, py + inset,
                     bs - inset * 2, 1, hi_color);
        fb_fill_rect(&fb, px + inset, py + inset,
                     1, bs - inset * 2, hi_color);

        /* Bottom/right shadow edge (darker) */
        fb_fill_rect(&fb, px + inset, py + bs - inset - 1,
                     bs - inset * 2, 1, dark_color);
        fb_fill_rect(&fb, px + bs - inset - 1, py + inset,
                     1, bs - inset * 2, dark_color);
    }
}

static void draw_hud(void) {
    /* Draw menu and exit buttons */
    draw_menu_button(&fb, &menu_button);
    draw_exit_button(&fb, &exit_button);

    /* Score — centered at top */
    char score_text[48];
    snprintf(score_text, sizeof(score_text), "SCORE: %d", game.score);
    text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 20, score_text, COLOR_WHITE, 2);

    /* Blocks remaining — below score */
    char remain_text[48];
    snprintf(remain_text, sizeof(remain_text), "BLOCKS: %d", game.blocks_remaining);
    text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 45, remain_text, COLOR_YELLOW, 2);
}

static void draw_grid_border(void) {
    int bw = game.cols * game.block_size;
    int bh = game.rows * game.block_size;
    fb_draw_rect(&fb, game.grid_x - 2, game.grid_y - 2, bw + 4, bh + 4, RGB(60, 60, 60));
    fb_draw_rect(&fb, game.grid_x - 1, game.grid_y - 1, bw + 2, bh + 2, RGB(100, 100, 100));
}

static void draw_playing_field(void) {
    int c, r;
    uint32_t now = get_time_ms();

    /* Draw HUD */
    draw_hud();

    /* Draw grid border */
    draw_grid_border();

    /* Draw grid background */
    fb_fill_rect(&fb, game.grid_x, game.grid_y,
                 game.cols * game.block_size, game.rows * game.block_size,
                 RGB(20, 20, 30));

    /* Calculate animation progress */
    float anim_progress = 0.0f;
    if (game.anim_state != ANIM_NONE && game.anim_start > 0) {
        uint32_t elapsed = now - game.anim_start;
        uint32_t duration = 0;
        switch (game.anim_state) {
        case ANIM_REMOVING: duration = ANIM_REMOVE_DURATION; break;
        case ANIM_FALLING:  duration = ANIM_FALL_DURATION; break;
        case ANIM_SLIDING:  duration = ANIM_SLIDE_DURATION; break;
        default: break;
        }
        if (duration > 0) {
            anim_progress = (float)elapsed / (float)duration;
            if (anim_progress > 1.0f) anim_progress = 1.0f;
        }
    }

    /* Draw blocks */
    switch (game.anim_state) {
    case ANIM_REMOVING:
        /* Draw non-removing blocks normally, removing blocks fade out */
        for (c = 0; c < game.cols; c++) {
            for (r = 0; r < game.rows; r++) {
                if (game.grid[c][r] == BLOCK_EMPTY) continue;
                if (game.removing_map[c][r]) {
                    /* Fading out */
                    float alpha = 1.0f - anim_progress;
                    draw_block(c, r, false, alpha);
                } else {
                    draw_block(c, r, false, 1.0f);
                }
            }
        }
        break;

    case ANIM_FALLING:
        /* Draw blocks sliding downward */
        {
            /* First, we need a snapshot of the grid AFTER removal but BEFORE gravity.
               The grid hasn't been modified yet during falling animation, so we draw
               based on fall_target and fall_origin data. */
            int bs = game.block_size;

            /* Draw blocks that are NOT falling in their current positions */
            for (c = 0; c < game.cols; c++) {
                for (r = 0; r < game.rows; r++) {
                    if (game.grid[c][r] == BLOCK_EMPTY) continue;

                    /* Check if this block is a falling destination */
                    bool is_falling_dest = (game.fall_origin[c][r] >= 0);
                    /* Check if this block will be moved (is source of a fall) */
                    bool is_source = false;
                    for (int dr = r + 1; dr < game.rows; dr++) {
                        if (game.fall_origin[c][dr] == r) {
                            is_source = true;
                            break;
                        }
                    }

                    if (is_falling_dest) {
                        /* Animate from origin to target */
                        int origin_r = game.fall_origin[c][r];
                        int origin_py = game.grid_y + origin_r * bs;
                        int target_py = game.grid_y + r * bs;
                        int px = game.grid_x + c * bs;

                        /* Ease-out interpolation */
                        float t = anim_progress;
                        t = 1.0f - (1.0f - t) * (1.0f - t);  /* Quadratic ease-out */
                        int current_py = origin_py + (int)((target_py - origin_py) * t);

                        int color_val = game.fall_target[c][r];
                        if (color_val >= 1 && color_val <= NUM_COLORS) {
                            int ci = color_val - 1;
                            int inset = 1;
                            fb_fill_rect(&fb, px + inset, current_py + inset,
                                         bs - inset * 2, bs - inset * 2,
                                         block_colors[ci]);
                            /* 3D edges */
                            fb_fill_rect(&fb, px + inset, current_py + inset,
                                         bs - inset * 2, 1, block_colors_highlight[ci]);
                            fb_fill_rect(&fb, px + inset, current_py + inset,
                                         1, bs - inset * 2, block_colors_highlight[ci]);
                            fb_fill_rect(&fb, px + inset, current_py + bs - inset - 1,
                                         bs - inset * 2, 1, block_colors_dark[ci]);
                            fb_fill_rect(&fb, px + bs - inset - 1, current_py + inset,
                                         1, bs - inset * 2, block_colors_dark[ci]);
                        }
                    } else if (!is_source) {
                        /* Static block — draw normally */
                        draw_block(c, r, false, 1.0f);
                    }
                    /* If is_source, the block is being animated at its destination,
                       don't draw it at old position */
                }
            }
        }
        break;

    case ANIM_SLIDING:
        /* Draw columns sliding left */
        {
            int bs = game.block_size;

            for (c = 0; c < game.cols; c++) {
                /* Check if this column has any blocks */
                bool has_blocks = false;
                for (r = 0; r < game.rows; r++) {
                    if (game.grid[c][r] != BLOCK_EMPTY) {
                        has_blocks = true;
                        break;
                    }
                }
                if (!has_blocks) continue;

                int target_c = game.slide_target[c];
                int src_px = game.grid_x + c * bs;
                int dest_px = (target_c >= 0) ? (game.grid_x + target_c * bs) : src_px;

                /* Ease-out interpolation */
                float t = anim_progress;
                t = 1.0f - (1.0f - t) * (1.0f - t);
                int current_px = src_px + (int)((dest_px - src_px) * t);

                for (r = 0; r < game.rows; r++) {
                    if (game.grid[c][r] == BLOCK_EMPTY) continue;

                    int color_val = game.grid[c][r];
                    if (color_val >= 1 && color_val <= NUM_COLORS) {
                        int ci = color_val - 1;
                        int inset = 1;
                        int py = game.grid_y + r * bs;

                        fb_fill_rect(&fb, current_px + inset, py + inset,
                                     bs - inset * 2, bs - inset * 2,
                                     block_colors[ci]);
                        fb_fill_rect(&fb, current_px + inset, py + inset,
                                     bs - inset * 2, 1, block_colors_highlight[ci]);
                        fb_fill_rect(&fb, current_px + inset, py + inset,
                                     1, bs - inset * 2, block_colors_highlight[ci]);
                        fb_fill_rect(&fb, current_px + inset, py + bs - inset - 1,
                                     bs - inset * 2, 1, block_colors_dark[ci]);
                        fb_fill_rect(&fb, current_px + bs - inset - 1, py + inset,
                                     1, bs - inset * 2, block_colors_dark[ci]);
                    }
                }
            }
        }
        break;

    case ANIM_NONE:
    default:
        /* Draw all blocks normally */
        for (c = 0; c < game.cols; c++) {
            for (r = 0; r < game.rows; r++) {
                if (game.grid[c][r] == BLOCK_EMPTY) continue;

                bool hl = game.highlight_active && game.highlight_map[c][r];
                draw_block(c, r, hl, 1.0f);
            }
        }
        break;
    }

    /* Flash effect for invalid single-block tap */
    if (game.flash_active) {
        uint32_t flash_elapsed = now - game.flash_start;
        if (flash_elapsed < 200) {
            int px = game.grid_x + game.flash_col * game.block_size;
            int py = game.grid_y + game.flash_row * game.block_size;
            uint8_t flash_alpha = (uint8_t)(180 * (1.0f - (float)flash_elapsed / 200.0f));
            fb_fill_rect_alpha(&fb, px, py, game.block_size, game.block_size,
                               COLOR_RED, flash_alpha);
        } else {
            game.flash_active = false;
        }
    }

    /* Perfect clear message */
    if (game.perfect_clear) {
        uint32_t pc_elapsed = now - game.perfect_clear_start;
        if (pc_elapsed < 1500) {
            /* Pulsing text */
            float pulse = 0.5f + 0.5f * sinf((float)(pc_elapsed % 500) / 500.0f * 2.0f * 3.14159f);
            uint32_t pc_color = color_lerp(COLOR_YELLOW, COLOR_WHITE, pulse);
            text_draw_centered(&fb, fb.width / 2, fb.height / 2,
                               "PERFECT CLEAR!", pc_color, 4);
            char bonus_text[32];
            snprintf(bonus_text, sizeof(bonus_text), "+%d BONUS!", PERFECT_CLEAR_BONUS);
            text_draw_centered(&fb, fb.width / 2, fb.height / 2 + 40,
                               bonus_text, COLOR_CYAN, 2);
        }
    }

    /* Highlight count indicator (show how many blocks selected) */
    if (game.highlight_active && game.highlight_count >= 2) {
        char hl_text[48];
        int points = (game.highlight_count - 1) * (game.highlight_count - 1);
        snprintf(hl_text, sizeof(hl_text), "%d BLOCKS (+%d PTS)",
                 game.highlight_count, points);
        /* Draw at bottom of grid area */
        int text_y = game.grid_y + game.rows * game.block_size + 8;
        if (text_y + 16 > (int)fb.height) text_y = (int)fb.height - 16;
        text_draw_centered(&fb, fb.width / 2, text_y, hl_text, COLOR_CYAN, 1);
    }
}

static void draw_game(void) {
    fb_clear(&fb, COLOR_BLACK);

    if (current_screen == SCREEN_WELCOME) {
        draw_welcome_screen(&fb, "SAMEGAME",
            "TAP GROUPS OF 2+ SAME-COLORED BLOCKS\n"
            "TAP AGAIN TO REMOVE THEM", &start_button);
        return;
    }

    /* Update screen shake before drawing */
    update_shake();

    /* Draw the playing field as background for all playing states */
    draw_playing_field();

    if (current_screen == SCREEN_PAUSED) {
        fb_clear_draw_offset(&fb);
        fb_fill_rect_alpha(&fb, 0, 0, fb.width, fb.height, COLOR_BLACK, 160);
        text_draw_centered(&fb, fb.width / 2, fb.height / 3, "PAUSED", COLOR_CYAN, 3);
        button_draw(&fb, &resume_button);
        button_draw(&fb, &exit_pause_button);
        return;
    }

    if (current_screen == SCREEN_GAME_OVER) {
        fb_clear_draw_offset(&fb);
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

    /* Reset draw offset after drawing playing field with shake */
    fb_clear_draw_offset(&fb);
}

/* ========================================================================== */
/* INPUT HANDLING                                                             */
/* ========================================================================== */

static void handle_input(void) {
    touch_poll(&touch);
    TouchState state = touch_get_state(&touch);
    uint32_t now = get_time_ms();

    /* Welcome screen */
    if (current_screen == SCREEN_WELCOME) {
        if (state.pressed) {
            bool touched = button_is_touched(&start_button, state.x, state.y);
            if (button_check_press(&start_button, touched, now)) {
                current_screen = SCREEN_PLAYING;
                hw_set_led(LED_GREEN, 100);
                usleep(100000);
                hw_leds_off();
            }
        }
        return;
    }

    /* Game over — handled in draw phase */
    if (current_screen == SCREEN_GAME_OVER) {
        return;
    }

    /* Pause screen */
    if (current_screen == SCREEN_PAUSED) {
        if (state.pressed) {
            bool resume_touched = button_is_touched(&resume_button, state.x, state.y);
            if (button_check_press(&resume_button, resume_touched, now)) {
                current_screen = SCREEN_PLAYING;
                return;
            }

            bool exit_touched = button_is_touched(&exit_pause_button, state.x, state.y);
            if (button_check_press(&exit_pause_button, exit_touched, now)) {
                fb_fade_out(&fb);
                running = false;
                return;
            }
        }
        return;
    }

    /* Playing screen */
    if (state.pressed) {
        /* Check exit button */
        bool exit_touched = button_is_touched(&exit_button, state.x, state.y);
        if (button_check_press(&exit_button, exit_touched, now)) {
            fb_fade_out(&fb);
            running = false;
            return;
        }

        /* Check menu button */
        bool menu_touched = button_is_touched(&menu_button, state.x, state.y);
        if (button_check_press(&menu_button, menu_touched, now)) {
            current_screen = SCREEN_PAUSED;
            return;
        }

        /* Ignore grid touches during animations */
        if (game.anim_state != ANIM_NONE)
            return;

        /* Convert touch position to grid coordinates */
        int touch_col = (state.x - game.grid_x) / game.block_size;
        int touch_row = (state.y - game.grid_y) / game.block_size;

        /* Bounds check */
        if (touch_col < 0 || touch_col >= game.cols ||
            touch_row < 0 || touch_row >= game.rows) {
            /* Tapped outside grid — clear highlight */
            if (game.highlight_active) {
                game.highlight_active = false;
                memset(game.highlight_map, 0, sizeof(game.highlight_map));
                game.highlight_count = 0;
                game.highlight_color = 0;
            }
            return;
        }

        /* Check if touched block is empty */
        if (game.grid[touch_col][touch_row] == BLOCK_EMPTY) {
            /* Clear highlight */
            if (game.highlight_active) {
                game.highlight_active = false;
                memset(game.highlight_map, 0, sizeof(game.highlight_map));
                game.highlight_count = 0;
                game.highlight_color = 0;
            }
            return;
        }

        /* Two-click mechanic */
        if (game.highlight_active && game.highlight_map[touch_col][touch_row]) {
            /* Second tap on highlighted block — remove the group */
            remove_highlighted();
        } else {
            /* First tap or tap on different block — compute new highlight */
            compute_highlight(touch_col, touch_row);
        }
    }
}

/* ========================================================================== */
/* GAME UPDATE                                                                */
/* ========================================================================== */

static void update_game(void) {
    if (current_screen != SCREEN_PLAYING)
        return;

    /* Update animations */
    update_animations();
    update_led_effects();
}

/* ========================================================================== */
/* MAIN                                                                       */
/* ========================================================================== */

int main(int argc, char *argv[]) {
    const char *fb_device = "/dev/fb0";
    const char *touch_device = "/dev/input/touchscreen0";

    if (argc > 1) fb_device = argv[1];
    if (argc > 2) touch_device = argv[2];

    /* Singleton guard — prevent duplicate instances */
    int lock_fd = acquire_instance_lock("samegame");
    if (lock_fd < 0) {
        fprintf(stderr, "samegame: another instance is already running\n");
        return 1;
    }

    /* Signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Hardware init */
    hw_init();
    hw_set_backlight(100);
    hw_leds_off();

    /* Audio init (non-fatal) */
    audio_init(&audio);

    /* Framebuffer init */
    if (fb_init(&fb, fb_device) < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }

    /* Touch init */
    if (touch_init(&touch, touch_device) < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&fb);
        return 1;
    }
    touch_set_screen_size(&touch, fb.width, fb.height);

    /* Seed RNG */
    srand(time(NULL));

    /* Initialize game */
    init_game();

    printf("SameGame started! Touch screen to play.\n");
    printf("Grid: %d x %d, block size: %d\n", game.cols, game.rows, game.block_size);

    /* Main game loop */
    while (running) {
        handle_input();
        update_game();
        draw_game();
        fb_swap(&fb);
        usleep(FRAME_DELAY_US);
    }

    /* Cleanup (reverse order) */
    hw_leds_off();
    hw_set_backlight(100);
    audio_close(&audio);
    touch_close(&touch);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    fb_close(&fb);

    printf("SameGame ended. Final score: %d\n", game.score);
    return 0;
}
