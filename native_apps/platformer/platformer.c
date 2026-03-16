/*
 * Office Runner — Mario-style side-scrolling platformer
 * Native C implementation for RoomWizard embedded device (300 MHz ARM)
 *
 * Features:
 *   - Tile-based scrolling levels with camera system
 *   - Physics engine with coyote time, jump buffering, variable jump height
 *   - Procedurally drawn sprites (no PPM assets needed)
 *   - Enemy AI with stomp-kill mechanic
 *   - Gamepad, keyboard, and touch input via unified gamepad module
 *   - High scores, LED effects, audio feedback
 *   - Welcome, pause (modal dialog), level complete, game-over screens
 *
 * Follows project conventions established in frogger.c.
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
#include "../common/gamepad.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Tile System Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define TILE_EMPTY      0
#define TILE_SOLID      1
#define TILE_PLATFORM   2
#define TILE_HAZARD     3
#define TILE_COIN       4
#define TILE_GOAL       5
#define TILE_SPAWN      6
#define TILE_ENEMY      7

#define TILE_SIZE       16
#define MAX_LEVEL_WIDTH  120
#define MAX_LEVEL_HEIGHT 30

/* ═══════════════════════════════════════════════════════════════════════════
 * Physics Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define GRAVITY          0.55f
#define JUMP_VELOCITY   -8.5f
#define JUMP_CUT         0.4f
#define MAX_FALL_SPEED   11.0f
#define WALK_SPEED       3.0f
#define RUN_SPEED        5.0f
#define ACCELERATION     0.5f
#define DECELERATION     0.3f
#define COYOTE_TIME      5
#define JUMP_BUFFER_TIME 6
#define STOMP_BOUNCE    -6.0f

/* ═══════════════════════════════════════════════════════════════════════════
 * Game Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define MAX_ENEMIES      20
#define MAX_LEVELS       3
#define INITIAL_LIVES    3
#define HUD_HEIGHT       40
#define DEATH_ANIM_FRAMES 20
#define INVINCIBLE_TIME  60
#define COIN_SCORE       100
#define STOMP_SCORE      200
#define LEVEL_BONUS      1000
#define ENEMY_WALKER     0

/* ═══════════════════════════════════════════════════════════════════════════
 * Color Palette
 * ═══════════════════════════════════════════════════════════════════════════ */

#define COLOR_SKY_TOP       RGB(135, 206, 235)
#define COLOR_SKY_BOTTOM    RGB(100, 180, 220)
#define COLOR_GROUND        RGB(139, 90, 43)
#define COLOR_GROUND_TOP    RGB(76, 153, 0)
#define COLOR_GROUND_DARK   RGB(100, 65, 30)
#define COLOR_GROUND_LINE   RGB(80, 50, 20)
#define COLOR_PLATFORM_BG   RGB(160, 120, 80)
#define COLOR_PLATFORM_TOP  RGB(180, 140, 100)
#define COLOR_PLATFORM_LINE RGB(120, 85, 55)
#define COLOR_HAZARD_BG     RGB(200, 50, 30)
#define COLOR_HAZARD_STRIPE RGB(255, 200, 0)
#define COLOR_SKIN          RGB(255, 210, 170)
#define COLOR_HAIR          RGB(60, 40, 20)
#define COLOR_SHIRT         RGB(50, 100, 180)
#define COLOR_SHIRT_DARK    RGB(35, 70, 130)
#define COLOR_TIE           RGB(200, 40, 40)
#define COLOR_PANTS         RGB(50, 50, 60)
#define COLOR_SHOES         RGB(30, 30, 30)
#define COLOR_ENEMY_BODY    RGB(180, 180, 180)
#define COLOR_ENEMY_DARK    RGB(120, 120, 120)
#define COLOR_ENEMY_EYE     RGB(255, 60, 60)
#define COLOR_ENEMY_PAPER   RGB(240, 240, 230)
#define COLOR_COIN_OUTER    RGB(255, 200, 0)
#define COLOR_COIN_INNER    RGB(255, 230, 100)
#define COLOR_COIN_SHINE    RGB(255, 255, 200)
#define COLOR_FLAG_POLE     RGB(180, 180, 180)
#define COLOR_FLAG_CLOTH    RGB(0, 180, 0)
#define COLOR_HUD_BG        RGB(20, 20, 30)

/* ═══════════════════════════════════════════════════════════════════════════
 * Data Structures
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    SCREEN_WELCOME,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_LEVEL_COMPLETE,
    SCREEN_GAME_OVER
} GameScreen;

typedef enum {
    PSTATE_IDLE,
    PSTATE_RUNNING,
    PSTATE_JUMPING,
    PSTATE_FALLING,
    PSTATE_DYING
} PlayerState;

typedef struct {
    float x, y;
    float vx, vy;
    int width, height;
    bool on_ground;
    bool facing_right;
    int anim_frame;
    int anim_timer;
    PlayerState state;
    int lives;
    int coyote_frames;
    int jump_buffer;
    bool jump_held;
    float spawn_x, spawn_y;
    int death_timer;
    int invincible_timer;
} Player;

typedef struct {
    float x, y;
    float vx;
    int width, height;
    bool alive;
    bool facing_right;
    int anim_frame, anim_timer;
    int type;
} Enemy;

typedef struct {
    float x, y;
    float target_x, target_y;
    int level_width_px;
    int level_height_px;
} Camera;

typedef struct {
    bool active;
    int type;
    uint32_t start_time;
} LEDEffect;

/* ═══════════════════════════════════════════════════════════════════════════
 * Global State
 * ═══════════════════════════════════════════════════════════════════════════ */

static Framebuffer fb;
static TouchInput touch;
static Audio audio;
static GamepadManager gamepad;
static InputState input;
static HighScoreTable hs_table;
static GameOverScreen gos;
static bool running = true;
static GameScreen current_screen = SCREEN_WELCOME;

static uint8_t level_tiles[MAX_LEVEL_HEIGHT][MAX_LEVEL_WIDTH];
static bool    coins_collected[MAX_LEVEL_HEIGHT][MAX_LEVEL_WIDTH];
static int     level_width, level_height;
static int     current_level;
static int     total_coins, collected_coins;

static Player  player;
static Enemy   enemies[MAX_ENEMIES];
static int     enemy_count;
static Camera  camera;

static int     score;
static int     game_lives;
static uint32_t current_frame;
static uint32_t level_complete_timer;
static LEDEffect led_effect;

static Button menu_button;
static Button exit_button;
static Button start_button;
static ModalDialog pause_dialog;

/* ═══════════════════════════════════════════════════════════════════════════
 * Forward Declarations (for functions referenced before definition)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void player_die(void);
static void draw_coin(int sx, int sy);
static void draw_goal(int sx, int sy);

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal Handler
 * ═══════════════════════════════════════════════════════════════════════════ */

static void signal_handler(int sig) {
    (void)sig;
    running = false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * LED Effects
 * ═══════════════════════════════════════════════════════════════════════════ */

static void start_led_effect(int type) {
    led_effect.active = true;
    led_effect.type = type;
    led_effect.start_time = get_time_ms();
}

static void update_led_effects(void) {
    if (!led_effect.active) return;
    uint32_t elapsed = get_time_ms() - led_effect.start_time;

    switch (led_effect.type) {
    case 1: /* Coin: yellow 150ms */
        if (elapsed < 150) hw_set_leds(HW_LED_COLOR_YELLOW);
        else { hw_leds_off(); led_effect.active = false; }
        break;
    case 2: /* Stomp: green 200ms */
        if (elapsed < 200) hw_set_leds(HW_LED_COLOR_GREEN);
        else { hw_leds_off(); led_effect.active = false; }
        break;
    case 3: /* Death: red pulse */
    {
        int pulse = (int)(elapsed / 200);
        int phase = (int)(elapsed % 200);
        if (pulse < 3) hw_set_red_led(phase < 100 ? 100 : 0);
        else { hw_leds_off(); led_effect.active = false; }
    }
    break;
    case 4: /* Level complete */
    {
        int phase = (int)((elapsed / 150) % 2);
        if (elapsed < 800) {
            if (phase == 0) hw_set_leds(HW_LED_COLOR_GREEN);
            else hw_set_leds(HW_LED_COLOR_YELLOW);
        } else { hw_leds_off(); led_effect.active = false; }
    }
    break;
    default:
        led_effect.active = false;
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Audio Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void play_jump_sound(void)           { audio_beep(&audio); }
static void play_coin_sound(void)           { audio_blip(&audio); }
static void play_stomp_sound(void)          { audio_beep(&audio); }
static void play_death_sound(void)          { audio_fail(&audio); }
static void play_level_complete_sound(void) { audio_success(&audio); }

/* ═══════════════════════════════════════════════════════════════════════════
 * Tile Queries
 * ═══════════════════════════════════════════════════════════════════════════ */

static int tile_at(int tx, int ty) {
    if (tx < 0 || tx >= level_width || ty < 0 || ty >= level_height)
        return TILE_EMPTY;
    return level_tiles[ty][tx];
}

static bool tile_is_solid(int tx, int ty) {
    return tile_at(tx, ty) == TILE_SOLID;
}

static bool tile_is_platform(int tx, int ty) {
    return tile_at(tx, ty) == TILE_PLATFORM;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Level Building Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void level_fill(int x1, int y1, int x2, int y2, uint8_t tile) {
    for (int y = y1; y <= y2 && y < MAX_LEVEL_HEIGHT; y++)
        for (int x = x1; x <= x2 && x < MAX_LEVEL_WIDTH; x++)
            if (x >= 0 && y >= 0)
                level_tiles[y][x] = tile;
}

static void level_platform(int x, int y, int len, uint8_t tile) {
    for (int i = 0; i < len && (x + i) < MAX_LEVEL_WIDTH; i++)
        if (x + i >= 0 && y >= 0 && y < MAX_LEVEL_HEIGHT)
            level_tiles[y][x + i] = tile;
}

static void level_coins(int x, int y, int count) {
    for (int i = 0; i < count && (x + i) < MAX_LEVEL_WIDTH; i++)
        if (x + i >= 0 && y >= 0 && y < MAX_LEVEL_HEIGHT)
            level_tiles[y][x + i] = TILE_COIN;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Level 1 — Tutorial (100 tiles wide)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void build_level_1(void) {
    level_width = 100;
    level_height = 30;
    memset(level_tiles, TILE_EMPTY, sizeof(level_tiles));
    memset(coins_collected, 0, sizeof(coins_collected));

    /* Ground rows 28-29 */
    level_fill(0, 28, 99, 29, TILE_SOLID);

    /* Gaps */
    level_fill(18, 28, 20, 29, TILE_EMPTY);
    level_fill(38, 28, 41, 29, TILE_EMPTY);
    level_fill(60, 28, 62, 29, TILE_EMPTY);
    level_fill(78, 28, 81, 29, TILE_EMPTY);

    /* Spawn */
    level_tiles[27][2] = TILE_SPAWN;

    /* Low platforms */
    level_platform(10, 25, 4, TILE_PLATFORM);
    level_platform(22, 25, 5, TILE_PLATFORM);
    level_platform(35, 24, 3, TILE_PLATFORM);
    level_platform(45, 25, 4, TILE_PLATFORM);
    level_platform(55, 24, 5, TILE_PLATFORM);
    level_platform(65, 25, 4, TILE_PLATFORM);
    level_platform(75, 24, 3, TILE_PLATFORM);
    level_platform(85, 25, 4, TILE_PLATFORM);

    /* Higher platforms */
    level_platform(14, 22, 3, TILE_PLATFORM);
    level_platform(30, 21, 4, TILE_PLATFORM);
    level_platform(50, 22, 3, TILE_PLATFORM);
    level_platform(70, 21, 4, TILE_PLATFORM);

    /* Step platforms over gaps */
    level_platform(17, 26, 2, TILE_PLATFORM);
    level_platform(20, 26, 2, TILE_PLATFORM);
    level_platform(37, 26, 2, TILE_PLATFORM);
    level_platform(41, 26, 2, TILE_PLATFORM);
    level_platform(59, 26, 2, TILE_PLATFORM);
    level_platform(62, 26, 2, TILE_PLATFORM);
    level_platform(77, 26, 2, TILE_PLATFORM);
    level_platform(81, 26, 2, TILE_PLATFORM);

    /* Ground coins */
    level_coins(5, 27, 3);
    level_coins(13, 27, 3);
    level_coins(24, 27, 3);
    level_coins(43, 27, 3);
    level_coins(53, 27, 3);
    level_coins(67, 27, 3);
    level_coins(84, 27, 3);
    level_coins(90, 27, 3);

    /* Platform coins */
    level_coins(11, 24, 2);
    level_coins(23, 24, 3);
    level_coins(36, 23, 2);
    level_coins(46, 24, 2);
    level_coins(56, 23, 3);
    level_coins(66, 24, 2);
    level_coins(76, 23, 2);
    level_coins(86, 24, 2);

    /* High platform coins */
    level_coins(15, 21, 2);
    level_coins(31, 20, 2);
    level_coins(51, 21, 2);
    level_coins(71, 20, 2);

    /* Enemies */
    level_tiles[27][15] = TILE_ENEMY;
    level_tiles[27][30] = TILE_ENEMY;
    level_tiles[27][48] = TILE_ENEMY;
    level_tiles[27][70] = TILE_ENEMY;
    level_tiles[27][88] = TILE_ENEMY;

    /* Goal */
    level_tiles[27][97] = TILE_GOAL;
    level_tiles[26][97] = TILE_GOAL;
    level_tiles[25][97] = TILE_GOAL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Level 2 — Intermediate (110 tiles wide)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void build_level_2(void) {
    level_width = 110;
    level_height = 30;
    memset(level_tiles, TILE_EMPTY, sizeof(level_tiles));
    memset(coins_collected, 0, sizeof(coins_collected));

    level_fill(0, 28, 109, 29, TILE_SOLID);

    /* Gaps */
    level_fill(12, 28, 15, 29, TILE_EMPTY);
    level_fill(25, 28, 29, 29, TILE_EMPTY);
    level_fill(40, 28, 44, 29, TILE_EMPTY);
    level_fill(55, 28, 58, 29, TILE_EMPTY);
    level_fill(68, 28, 73, 29, TILE_EMPTY);
    level_fill(85, 28, 89, 29, TILE_EMPTY);

    /* Hazard section */
    level_fill(48, 28, 52, 28, TILE_HAZARD);

    level_tiles[27][2] = TILE_SPAWN;

    /* Staircase */
    level_platform(10, 26, 3, TILE_PLATFORM);
    level_platform(15, 25, 3, TILE_PLATFORM);
    level_platform(19, 24, 3, TILE_PLATFORM);

    /* Bridge */
    level_platform(24, 26, 2, TILE_PLATFORM);
    level_platform(29, 26, 2, TILE_PLATFORM);

    /* Vertical challenge */
    level_platform(33, 25, 3, TILE_PLATFORM);
    level_platform(36, 22, 3, TILE_PLATFORM);
    level_platform(33, 19, 3, TILE_PLATFORM);

    /* Hazard bypass */
    level_platform(47, 25, 2, TILE_PLATFORM);
    level_platform(50, 23, 2, TILE_PLATFORM);
    level_platform(53, 25, 2, TILE_PLATFORM);

    /* More platforms */
    level_platform(58, 26, 3, TILE_PLATFORM);
    level_platform(62, 25, 3, TILE_PLATFORM);
    level_platform(66, 24, 3, TILE_PLATFORM);

    level_platform(69, 26, 2, TILE_PLATFORM);
    level_platform(72, 26, 2, TILE_PLATFORM);

    level_platform(76, 25, 4, TILE_PLATFORM);
    level_platform(82, 24, 3, TILE_PLATFORM);
    level_platform(86, 26, 2, TILE_PLATFORM);
    level_platform(89, 26, 2, TILE_PLATFORM);
    level_platform(92, 25, 5, TILE_PLATFORM);
    level_platform(100, 25, 4, TILE_PLATFORM);

    /* Coins */
    level_coins(5, 27, 4);
    level_coins(11, 25, 2);
    level_coins(16, 24, 2);
    level_coins(20, 23, 2);
    level_coins(34, 24, 2);
    level_coins(37, 21, 2);
    level_coins(34, 18, 2);
    level_coins(48, 24, 1);
    level_coins(51, 22, 1);
    level_coins(59, 25, 2);
    level_coins(63, 24, 2);
    level_coins(67, 23, 2);
    level_coins(77, 24, 3);
    level_coins(83, 23, 2);
    level_coins(93, 24, 4);
    level_coins(101, 24, 3);

    /* Enemies */
    level_tiles[27][8] = TILE_ENEMY;
    level_tiles[27][20] = TILE_ENEMY;
    level_tiles[27][35] = TILE_ENEMY;
    level_tiles[27][60] = TILE_ENEMY;
    level_tiles[27][76] = TILE_ENEMY;
    level_tiles[27][95] = TILE_ENEMY;
    level_tiles[24][34] = TILE_ENEMY;
    level_tiles[24][93] = TILE_ENEMY;

    level_tiles[27][107] = TILE_GOAL;
    level_tiles[26][107] = TILE_GOAL;
    level_tiles[25][107] = TILE_GOAL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Level 3 — Hard (115 tiles wide)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void build_level_3(void) {
    level_width = 115;
    level_height = 30;
    memset(level_tiles, TILE_EMPTY, sizeof(level_tiles));
    memset(coins_collected, 0, sizeof(coins_collected));

    /* Sparse ground with gaps */
    level_fill(0, 28, 9, 29, TILE_SOLID);
    level_fill(14, 28, 22, 29, TILE_SOLID);
    level_fill(28, 28, 35, 29, TILE_SOLID);
    level_fill(42, 28, 50, 29, TILE_SOLID);
    level_fill(57, 28, 62, 29, TILE_SOLID);
    level_fill(70, 28, 75, 29, TILE_SOLID);
    level_fill(82, 28, 88, 29, TILE_SOLID);
    level_fill(95, 28, 100, 29, TILE_SOLID);
    level_fill(107, 28, 114, 29, TILE_SOLID);

    /* Hazard pits */
    level_fill(10, 28, 13, 29, TILE_HAZARD);
    level_fill(36, 28, 41, 29, TILE_HAZARD);
    level_fill(63, 28, 69, 29, TILE_HAZARD);
    level_fill(89, 28, 94, 29, TILE_HAZARD);

    level_tiles[27][2] = TILE_SPAWN;

    /* Platforms */
    level_platform(8, 26, 2, TILE_PLATFORM);
    level_platform(11, 24, 2, TILE_PLATFORM);
    level_platform(14, 22, 3, TILE_PLATFORM);
    level_platform(19, 20, 3, TILE_PLATFORM);
    level_platform(15, 26, 2, TILE_PLATFORM);

    level_platform(23, 25, 2, TILE_PLATFORM);
    level_platform(26, 26, 2, TILE_PLATFORM);

    level_platform(35, 26, 2, TILE_PLATFORM);
    level_platform(38, 24, 2, TILE_PLATFORM);
    level_platform(41, 26, 2, TILE_PLATFORM);

    level_platform(44, 25, 4, TILE_PLATFORM);
    level_platform(46, 22, 3, TILE_PLATFORM);
    level_platform(44, 19, 4, TILE_PLATFORM);
    level_platform(48, 17, 3, TILE_PLATFORM);

    level_platform(52, 25, 2, TILE_PLATFORM);
    level_platform(55, 24, 2, TILE_PLATFORM);

    level_platform(62, 26, 2, TILE_PLATFORM);
    level_platform(65, 24, 2, TILE_PLATFORM);
    level_platform(68, 26, 2, TILE_PLATFORM);

    level_platform(73, 25, 3, TILE_PLATFORM);
    level_platform(77, 23, 3, TILE_PLATFORM);
    level_platform(80, 25, 2, TILE_PLATFORM);

    level_platform(88, 26, 2, TILE_PLATFORM);
    level_platform(91, 24, 2, TILE_PLATFORM);
    level_platform(94, 26, 2, TILE_PLATFORM);

    level_platform(98, 25, 3, TILE_PLATFORM);
    level_platform(102, 23, 3, TILE_PLATFORM);
    level_platform(105, 25, 2, TILE_PLATFORM);

    /* Coins */
    level_coins(4, 27, 3);
    level_coins(9, 25, 1);
    level_coins(12, 23, 1);
    level_coins(15, 21, 2);
    level_coins(20, 19, 2);
    level_coins(30, 27, 3);
    level_coins(36, 25, 1);
    level_coins(39, 23, 1);
    level_coins(45, 24, 2);
    level_coins(47, 21, 2);
    level_coins(45, 18, 3);
    level_coins(49, 16, 2);
    level_coins(53, 24, 1);
    level_coins(56, 23, 1);
    level_coins(58, 27, 3);
    level_coins(63, 25, 1);
    level_coins(66, 23, 1);
    level_coins(72, 27, 2);
    level_coins(74, 24, 2);
    level_coins(78, 22, 2);
    level_coins(84, 27, 3);
    level_coins(89, 25, 1);
    level_coins(92, 23, 1);
    level_coins(97, 27, 2);
    level_coins(99, 24, 2);
    level_coins(103, 22, 2);
    level_coins(109, 27, 3);

    /* Enemies */
    level_tiles[27][6] = TILE_ENEMY;
    level_tiles[27][17] = TILE_ENEMY;
    level_tiles[27][32] = TILE_ENEMY;
    level_tiles[27][46] = TILE_ENEMY;
    level_tiles[27][59] = TILE_ENEMY;
    level_tiles[27][73] = TILE_ENEMY;
    level_tiles[27][85] = TILE_ENEMY;
    level_tiles[27][98] = TILE_ENEMY;
    level_tiles[27][110] = TILE_ENEMY;
    level_tiles[24][45] = TILE_ENEMY;
    level_tiles[22][78] = TILE_ENEMY;

    level_tiles[27][112] = TILE_GOAL;
    level_tiles[26][112] = TILE_GOAL;
    level_tiles[25][112] = TILE_GOAL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Level Loading & Entity Spawning
 * ═══════════════════════════════════════════════════════════════════════════ */

static void spawn_enemies_from_tiles(void) {
    enemy_count = 0;
    total_coins = 0;
    collected_coins = 0;

    for (int y = 0; y < level_height; y++) {
        for (int x = 0; x < level_width; x++) {
            if (level_tiles[y][x] == TILE_ENEMY && enemy_count < MAX_ENEMIES) {
                Enemy *e = &enemies[enemy_count++];
                e->x = (float)(x * TILE_SIZE);
                e->y = (float)(y * TILE_SIZE);
                e->vx = 1.0f;
                e->width = TILE_SIZE - 2;
                e->height = TILE_SIZE - 2;
                e->alive = true;
                e->facing_right = (rand() % 2) ? true : false;
                if (!e->facing_right) e->vx = -e->vx;
                e->anim_frame = 0;
                e->anim_timer = 0;
                e->type = ENEMY_WALKER;
                level_tiles[y][x] = TILE_EMPTY;
            }
            if (level_tiles[y][x] == TILE_COIN)
                total_coins++;
        }
    }
}

static void reset_player_to_spawn(void) {
    player.x = player.spawn_x;
    player.y = player.spawn_y;
    player.vx = 0;
    player.vy = 0;
    player.on_ground = false;
    player.facing_right = true;
    player.anim_frame = 0;
    player.anim_timer = 0;
    player.state = PSTATE_IDLE;
    player.coyote_frames = 0;
    player.jump_buffer = 0;
    player.jump_held = false;
    player.death_timer = 0;
    player.invincible_timer = INVINCIBLE_TIME;
}

static void load_level(int level_num) {
    switch (level_num) {
    case 0: build_level_1(); break;
    case 1: build_level_2(); break;
    case 2: build_level_3(); break;
    default: build_level_1(); break;
    }

    /* Find spawn */
    player.spawn_x = 2.0f * TILE_SIZE;
    player.spawn_y = 27.0f * TILE_SIZE;
    for (int y = 0; y < level_height; y++) {
        for (int x = 0; x < level_width; x++) {
            if (level_tiles[y][x] == TILE_SPAWN) {
                player.spawn_x = (float)(x * TILE_SIZE);
                player.spawn_y = (float)(y * TILE_SIZE);
                level_tiles[y][x] = TILE_EMPTY;
            }
        }
    }

    player.width = 14;
    player.height = 28;
    player.lives = game_lives;

    spawn_enemies_from_tiles();
    reset_player_to_spawn();
    player.invincible_timer = 0;

    camera.level_width_px = level_width * TILE_SIZE;
    camera.level_height_px = level_height * TILE_SIZE;
    camera.x = player.x - (float)fb.width / 2.0f;
    camera.y = player.y - (float)fb.height / 2.0f;
    camera.target_x = camera.x;
    camera.target_y = camera.y;

    if (camera.x < 0) camera.x = 0;
    if (camera.y < 0) camera.y = 0;
    float max_cx = (float)(camera.level_width_px - (int)fb.width);
    float max_cy = (float)(camera.level_height_px - (int)fb.height);
    if (max_cx < 0) max_cx = 0;
    if (max_cy < 0) max_cy = 0;
    if (camera.x > max_cx) camera.x = max_cx;
    if (camera.y > max_cy) camera.y = max_cy;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Game Init
 * ═══════════════════════════════════════════════════════════════════════════ */

static void init_buttons(void) {
    button_init(&menu_button, LAYOUT_MENU_BTN_X, LAYOUT_MENU_BTN_Y,
                BTN_MENU_WIDTH, BTN_MENU_HEIGHT, "",
                BTN_MENU_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&exit_button, LAYOUT_EXIT_BTN_X, LAYOUT_EXIT_BTN_Y,
                BTN_EXIT_WIDTH, BTN_EXIT_HEIGHT, "",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&start_button,
                (int)fb.width / 2 - BTN_LARGE_WIDTH / 2,
                (int)fb.height / 2 + 40,
                BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT,
                "TAP TO START",
                BTN_START_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);

    modal_dialog_init(&pause_dialog, "PAUSED", NULL, 2);
    modal_dialog_set_button(&pause_dialog, 0, "RESUME", BTN_COLOR_PRIMARY, COLOR_WHITE);
    modal_dialog_set_button(&pause_dialog, 1, "EXIT", BTN_COLOR_DANGER, COLOR_WHITE);
}

static void reset_game(void) {
    score = 0;
    game_lives = INITIAL_LIVES;
    current_level = 0;
    current_frame = 0;
    level_complete_timer = 0;
    led_effect.active = false;
    load_level(current_level);
}

static void init_game(void) {
    init_buttons();
    hs_init(&hs_table, "platformer");
    hs_load(&hs_table);
    reset_game();

    TouchRegion touch_regions[] = {
        { 10,  (int)fb.height - 120, 60, 60, BTN_ID_LEFT  },
        { 130, (int)fb.height - 120, 60, 60, BTN_ID_RIGHT },
        { 70,  (int)fb.height - 180, 60, 60, BTN_ID_UP    },
        { 70,  (int)fb.height - 60,  60, 60, BTN_ID_DOWN  },
        { (int)fb.width - 130, (int)fb.height - 120, 60, 60, BTN_ID_JUMP },
        { (int)fb.width - 70,  (int)fb.height - 120, 60, 60, BTN_ID_RUN  },
    };
    gamepad_set_touch_regions(&gamepad, touch_regions, 6);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Collision Resolution
 * ═══════════════════════════════════════════════════════════════════════════ */

static void resolve_collisions_x(void) {
    int left_tile   = (int)player.x / TILE_SIZE;
    int right_tile  = (int)(player.x + player.width - 1) / TILE_SIZE;
    int top_tile    = (int)(player.y + 2) / TILE_SIZE;
    int bottom_tile = (int)(player.y + player.height - 2) / TILE_SIZE;

    for (int ty = top_tile; ty <= bottom_tile; ty++) {
        for (int tx = left_tile; tx <= right_tile; tx++) {
            if (tile_is_solid(tx, ty)) {
                if (player.vx > 0) {
                    player.x = (float)(tx * TILE_SIZE) - player.width;
                } else if (player.vx < 0) {
                    player.x = (float)((tx + 1) * TILE_SIZE);
                }
                player.vx = 0;
                return;
            }
        }
    }
}

static void resolve_collisions_y(void) {
    int left_tile   = (int)(player.x + 1) / TILE_SIZE;
    int right_tile  = (int)(player.x + player.width - 2) / TILE_SIZE;
    int top_tile    = (int)player.y / TILE_SIZE;
    int bottom_tile = (int)(player.y + player.height - 1) / TILE_SIZE;

    bool was_on_ground = player.on_ground;
    player.on_ground = false;

    for (int ty = top_tile; ty <= bottom_tile; ty++) {
        for (int tx = left_tile; tx <= right_tile; tx++) {
            int t = tile_at(tx, ty);

            if (t == TILE_SOLID) {
                if (player.vy > 0) {
                    player.y = (float)(ty * TILE_SIZE) - player.height;
                    player.vy = 0;
                    player.on_ground = true;
                } else if (player.vy < 0) {
                    player.y = (float)((ty + 1) * TILE_SIZE);
                    player.vy = 0;
                }
                return;
            }

            if (t == TILE_PLATFORM && player.vy > 0) {
                float platform_top = (float)(ty * TILE_SIZE);
                float feet_prev = player.y + player.height - player.vy;
                if (feet_prev <= platform_top + 2.0f) {
                    player.y = platform_top - player.height;
                    player.vy = 0;
                    player.on_ground = true;
                    return;
                }
            }
        }
    }

    if (was_on_ground && !player.on_ground && player.vy >= 0)
        player.coyote_frames = COYOTE_TIME;
}

static void check_tile_interactions(void) {
    int left_tile   = (int)(player.x + 2) / TILE_SIZE;
    int right_tile  = (int)(player.x + player.width - 3) / TILE_SIZE;
    int top_tile    = (int)(player.y + 2) / TILE_SIZE;
    int bottom_tile = (int)(player.y + player.height - 3) / TILE_SIZE;

    for (int ty = top_tile; ty <= bottom_tile; ty++) {
        for (int tx = left_tile; tx <= right_tile; tx++) {
            int t = tile_at(tx, ty);

            if (t == TILE_HAZARD) {
                player_die();
                return;
            }
            if (t == TILE_COIN && !coins_collected[ty][tx]) {
                coins_collected[ty][tx] = true;
                collected_coins++;
                score += COIN_SCORE;
                play_coin_sound();
                start_led_effect(1);
            }
            if (t == TILE_GOAL) {
                current_screen = SCREEN_LEVEL_COMPLETE;
                level_complete_timer = get_time_ms();
                score += LEVEL_BONUS;
                play_level_complete_sound();
                start_led_effect(4);
                return;
            }
        }
    }

    if (player.y > (float)(camera.level_height_px + TILE_SIZE))
        player_die();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Player Death
 * ═══════════════════════════════════════════════════════════════════════════ */

static void player_die(void) {
    if (player.state == PSTATE_DYING) return;
    if (player.invincible_timer > 0) return;

    player.state = PSTATE_DYING;
    player.death_timer = DEATH_ANIM_FRAMES;
    player.vx = 0;
    player.vy = JUMP_VELOCITY * 0.6f;
    game_lives--;
    player.lives = game_lives;
    play_death_sound();
    start_led_effect(3);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Enemy Update
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_enemies(void) {
    for (int i = 0; i < enemy_count; i++) {
        Enemy *e = &enemies[i];
        if (!e->alive) continue;

        e->anim_timer++;
        if (e->anim_timer >= 10) {
            e->anim_timer = 0;
            e->anim_frame = (e->anim_frame + 1) % 2;
        }

        e->x += e->vx;
        e->facing_right = (e->vx > 0);

        /* Wall check */
        int front_tx;
        if (e->vx > 0) front_tx = (int)(e->x + e->width) / TILE_SIZE;
        else front_tx = (int)(e->x) / TILE_SIZE;
        int foot_ty = (int)(e->y + e->height - 1) / TILE_SIZE;

        if (tile_is_solid(front_tx, foot_ty)) {
            e->vx = -e->vx;
            e->facing_right = (e->vx > 0);
        }

        /* Ledge check */
        int check_x;
        if (e->vx > 0) check_x = (int)(e->x + e->width) / TILE_SIZE;
        else check_x = (int)(e->x) / TILE_SIZE;
        int below_ty = foot_ty + 1;

        bool ground_ahead = tile_is_solid(check_x, below_ty) ||
                            tile_is_platform(check_x, below_ty);
        if (!ground_ahead && below_ty < level_height) {
            e->vx = -e->vx;
            e->facing_right = (e->vx > 0);
        }

        if (e->x < 0) { e->x = 0; e->vx = fabsf(e->vx); }
        if (e->x + e->width > camera.level_width_px) {
            e->x = (float)(camera.level_width_px - e->width);
            e->vx = -fabsf(e->vx);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Enemy Collisions
 * ═══════════════════════════════════════════════════════════════════════════ */

static void check_enemy_collisions(void) {
    if (player.state == PSTATE_DYING) return;

    for (int i = 0; i < enemy_count; i++) {
        Enemy *e = &enemies[i];
        if (!e->alive) continue;

        if (player.x + player.width <= e->x || player.x >= e->x + e->width) continue;
        if (player.y + player.height <= e->y || player.y >= e->y + e->height) continue;

        float player_feet = player.y + player.height;
        float enemy_mid = e->y + (float)e->height / 2.0f;

        if (player.vy > 0 && player_feet < enemy_mid + 4.0f) {
            e->alive = false;
            player.vy = STOMP_BOUNCE;
            player.on_ground = false;
            score += STOMP_SCORE;
            play_stomp_sound();
            start_led_effect(2);
        } else {
            player_die();
            return;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Player Update
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_player(void) {
    if (player.state == PSTATE_DYING) {
        player.death_timer--;
        player.vy += GRAVITY;
        player.y += player.vy;
        if (player.death_timer <= 0) {
            if (game_lives <= 0) {
                current_screen = SCREEN_GAME_OVER;
                gameover_init(&gos, &fb, score, NULL, NULL,
                              &hs_table, &touch);
            } else {
                reset_player_to_spawn();
            }
        }
        return;
    }

    if (player.invincible_timer > 0) player.invincible_timer--;
    if (player.coyote_frames > 0) player.coyote_frames--;
    if (player.jump_buffer > 0) player.jump_buffer--;

    /* Horizontal input */
    float target_speed = 0;
    bool moving = false;

    if (input.buttons[BTN_ID_LEFT].held) {
        bool run = input.buttons[BTN_ID_RUN].held;
        target_speed = -(run ? RUN_SPEED : WALK_SPEED);
        player.facing_right = false;
        moving = true;
    } else if (input.buttons[BTN_ID_RIGHT].held) {
        bool run = input.buttons[BTN_ID_RUN].held;
        target_speed = (run ? RUN_SPEED : WALK_SPEED);
        player.facing_right = true;
        moving = true;
    }

    if (moving) {
        if (fabsf(player.vx) < fabsf(target_speed)) {
            player.vx += (target_speed > 0 ? ACCELERATION : -ACCELERATION);
            if (fabsf(player.vx) > fabsf(target_speed))
                player.vx = target_speed;
        } else {
            if (player.vx > target_speed + 0.1f) player.vx -= DECELERATION;
            else if (player.vx < target_speed - 0.1f) player.vx += DECELERATION;
            else player.vx = target_speed;
        }
    } else {
        if (player.vx > DECELERATION) player.vx -= DECELERATION;
        else if (player.vx < -DECELERATION) player.vx += DECELERATION;
        else player.vx = 0;
    }

    /* Jump */
    if (input.buttons[BTN_ID_JUMP].pressed)
        player.jump_buffer = JUMP_BUFFER_TIME;

    bool can_jump = player.on_ground || player.coyote_frames > 0;
    if (player.jump_buffer > 0 && can_jump) {
        player.vy = JUMP_VELOCITY;
        player.on_ground = false;
        player.coyote_frames = 0;
        player.jump_buffer = 0;
        player.jump_held = true;
        play_jump_sound();
    }

    if (player.jump_held && input.buttons[BTN_ID_JUMP].released) {
        player.jump_held = false;
        if (player.vy < 0) player.vy *= JUMP_CUT;
    }
    if (player.on_ground) player.jump_held = false;

    /* Gravity */
    player.vy += GRAVITY;
    if (player.vy > MAX_FALL_SPEED) player.vy = MAX_FALL_SPEED;

    /* Move X + resolve */
    player.x += player.vx;
    if (player.x < 0) { player.x = 0; player.vx = 0; }
    if (player.x + player.width > camera.level_width_px) {
        player.x = (float)(camera.level_width_px - player.width);
        player.vx = 0;
    }
    resolve_collisions_x();

    /* Move Y + resolve */
    player.y += player.vy;
    resolve_collisions_y();

    check_tile_interactions();
    if (player.state == PSTATE_DYING) return;
    check_enemy_collisions();
    if (player.state == PSTATE_DYING) return;

    /* Animation */
    player.anim_timer++;
    if (player.anim_timer >= 8) {
        player.anim_timer = 0;
        player.anim_frame = (player.anim_frame + 1) % 2;
    }

    if (!player.on_ground)
        player.state = (player.vy < 0) ? PSTATE_JUMPING : PSTATE_FALLING;
    else if (fabsf(player.vx) > 0.5f)
        player.state = PSTATE_RUNNING;
    else
        player.state = PSTATE_IDLE;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Camera Update
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_camera(void) {
    int sw = (int)fb.width;
    int sh = (int)fb.height;

    float dead_zone_x = (float)sw * 0.35f;
    float psx = player.x - camera.x;

    if (psx < dead_zone_x)
        camera.target_x = player.x - dead_zone_x;
    else if (psx > (float)sw - dead_zone_x)
        camera.target_x = player.x - ((float)sw - dead_zone_x);

    float dead_zone_y = (float)sh * 0.30f;
    float psy = player.y - camera.y;

    if (psy < dead_zone_y)
        camera.target_y = player.y - dead_zone_y;
    else if (psy > (float)sh - dead_zone_y)
        camera.target_y = player.y - ((float)sh - dead_zone_y);

    /* Smooth interpolation */
    camera.x += (camera.target_x - camera.x) * 0.12f;
    camera.y += (camera.target_y - camera.y) * 0.12f;

    /* Clamp */
    float max_cx = (float)(camera.level_width_px - sw);
    float max_cy = (float)(camera.level_height_px - sh);
    if (max_cx < 0) max_cx = 0;
    if (max_cy < 0) max_cy = 0;
    if (camera.x < 0) camera.x = 0;
    if (camera.y < 0) camera.y = 0;
    if (camera.x > max_cx) camera.x = max_cx;
    if (camera.y > max_cy) camera.y = max_cy;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Game Update
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_game(void) {
    if (current_screen != SCREEN_PLAYING) return;
    current_frame++;
    update_player();
    update_enemies();
    update_camera();
    update_led_effects();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sprite Drawing — Tiles
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_single_tile(int tx, int ty, int sx, int sy) {
    int t = tile_at(tx, ty);

    /* Check if coin collected */
    if (t == TILE_COIN && coins_collected[ty][tx])
        return;

    switch (t) {
    case TILE_SOLID:
        /* Ground block: brown body with green top strip */
        fb_fill_rect(&fb, sx, sy, TILE_SIZE, TILE_SIZE, COLOR_GROUND);
        fb_fill_rect(&fb, sx, sy, TILE_SIZE, 3, COLOR_GROUND_TOP);
        /* Brick pattern */
        fb_draw_line(&fb, sx, sy + TILE_SIZE / 2, sx + TILE_SIZE, sy + TILE_SIZE / 2,
                     COLOR_GROUND_LINE);
        fb_draw_line(&fb, sx + TILE_SIZE / 2, sy + 3, sx + TILE_SIZE / 2, sy + TILE_SIZE / 2,
                     COLOR_GROUND_LINE);
        fb_draw_line(&fb, sx + TILE_SIZE / 4, sy + TILE_SIZE / 2,
                     sx + TILE_SIZE / 4, sy + TILE_SIZE, COLOR_GROUND_LINE);
        fb_draw_line(&fb, sx + TILE_SIZE * 3 / 4, sy + TILE_SIZE / 2,
                     sx + TILE_SIZE * 3 / 4, sy + TILE_SIZE, COLOR_GROUND_LINE);
        break;

    case TILE_PLATFORM:
        /* Thinner wooden platform */
        fb_fill_rect(&fb, sx, sy, TILE_SIZE, TILE_SIZE / 2 + 2, COLOR_PLATFORM_BG);
        fb_fill_rect(&fb, sx, sy, TILE_SIZE, 3, COLOR_PLATFORM_TOP);
        fb_draw_rect(&fb, sx, sy, TILE_SIZE, TILE_SIZE / 2 + 2, COLOR_PLATFORM_LINE);
        /* Wood grain */
        fb_draw_line(&fb, sx + 2, sy + 4, sx + TILE_SIZE - 2, sy + 4, COLOR_PLATFORM_LINE);
        break;

    case TILE_HAZARD:
        /* Red with yellow warning stripes */
        fb_fill_rect(&fb, sx, sy, TILE_SIZE, TILE_SIZE, COLOR_HAZARD_BG);
        for (int i = 0; i < TILE_SIZE; i += 6) {
            fb_fill_rect(&fb, sx + i, sy, 3, 3, COLOR_HAZARD_STRIPE);
            fb_fill_rect(&fb, sx + i + 3, sy + TILE_SIZE - 3, 3, 3, COLOR_HAZARD_STRIPE);
        }
        /* Spikes */
        for (int i = 2; i < TILE_SIZE - 2; i += 5) {
            fb_draw_line(&fb, sx + i, sy + 6, sx + i + 2, sy + 2, COLOR_HAZARD_STRIPE);
            fb_draw_line(&fb, sx + i + 2, sy + 2, sx + i + 4, sy + 6, COLOR_HAZARD_STRIPE);
        }
        break;

    case TILE_COIN:
        draw_coin(sx, sy);
        break;

    case TILE_GOAL:
        draw_goal(sx, sy);
        break;

    default:
        break;
    }
}

static void draw_coin(int sx, int sy) {
    int cx = sx + TILE_SIZE / 2;
    int cy = sy + TILE_SIZE / 2;
    int r = TILE_SIZE / 3;

    /* Animate: pulse size */
    int anim = (int)(current_frame / 8) % 4;
    if (anim == 1 || anim == 3) r -= 1;

    fb_fill_circle(&fb, cx, cy, r, COLOR_COIN_OUTER);
    fb_fill_circle(&fb, cx, cy, r - 2, COLOR_COIN_INNER);

    /* Dollar sign or shine */
    if (anim < 2) {
        fb_draw_line(&fb, cx, cy - r + 2, cx, cy + r - 2, COLOR_COIN_OUTER);
    }
    /* Sparkle */
    fb_draw_pixel(&fb, cx - 1, cy - r + 1, COLOR_COIN_SHINE);
}

static void draw_goal(int sx, int sy) {
    /* Flag pole */
    int pole_x = sx + TILE_SIZE / 2;
    fb_draw_line(&fb, pole_x, sy, pole_x, sy + TILE_SIZE, COLOR_FLAG_POLE);
    fb_draw_line(&fb, pole_x + 1, sy, pole_x + 1, sy + TILE_SIZE, COLOR_FLAG_POLE);

    /* Flag cloth (triangular) */
    int flag_top = sy + 2;
    int flag_h = TILE_SIZE / 2;
    for (int row = 0; row < flag_h; row++) {
        int w = flag_h - row;
        fb_fill_rect(&fb, pole_x + 2, flag_top + row, w, 1, COLOR_FLAG_CLOTH);
    }

    /* Pole ball top */
    fb_fill_circle(&fb, pole_x, sy, 2, COLOR_COIN_OUTER);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sprite Drawing — Player (Office Worker, 14x28)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_player_idle(int sx, int sy, bool right) {
    (void)right;
    int w = player.width;

    /* Head (circle) */
    fb_fill_circle(&fb, sx + w / 2, sy + 4, 5, COLOR_SKIN);
    /* Hair */
    fb_fill_rect(&fb, sx + w / 2 - 4, sy, 8, 3, COLOR_HAIR);

    /* Eyes */
    int eye_off = right ? 2 : -2;
    fb_draw_pixel(&fb, sx + w / 2 + eye_off - 1, sy + 3, COLOR_BLACK);
    fb_draw_pixel(&fb, sx + w / 2 + eye_off + 1, sy + 3, COLOR_BLACK);

    /* Body/shirt */
    fb_fill_rect(&fb, sx + 2, sy + 10, w - 4, 10, COLOR_SHIRT);
    /* Tie */
    fb_fill_rect(&fb, sx + w / 2 - 1, sy + 10, 2, 7, COLOR_TIE);

    /* Arms */
    fb_fill_rect(&fb, sx, sy + 11, 2, 7, COLOR_SHIRT_DARK);
    fb_fill_rect(&fb, sx + w - 2, sy + 11, 2, 7, COLOR_SHIRT_DARK);

    /* Pants */
    fb_fill_rect(&fb, sx + 3, sy + 20, 4, 5, COLOR_PANTS);
    fb_fill_rect(&fb, sx + w - 7, sy + 20, 4, 5, COLOR_PANTS);

    /* Shoes */
    fb_fill_rect(&fb, sx + 2, sy + 25, 5, 3, COLOR_SHOES);
    fb_fill_rect(&fb, sx + w - 7, sy + 25, 5, 3, COLOR_SHOES);
}

static void draw_player_running(int sx, int sy, bool right, int frame) {
    int w = player.width;

    /* Head */
    fb_fill_circle(&fb, sx + w / 2, sy + 4, 5, COLOR_SKIN);
    fb_fill_rect(&fb, sx + w / 2 - 4, sy, 8, 3, COLOR_HAIR);

    int eye_off = right ? 2 : -2;
    fb_draw_pixel(&fb, sx + w / 2 + eye_off - 1, sy + 3, COLOR_BLACK);
    fb_draw_pixel(&fb, sx + w / 2 + eye_off + 1, sy + 3, COLOR_BLACK);

    /* Body */
    fb_fill_rect(&fb, sx + 2, sy + 10, w - 4, 10, COLOR_SHIRT);
    fb_fill_rect(&fb, sx + w / 2 - 1, sy + 10, 2, 7, COLOR_TIE);

    /* Animated arms */
    if (frame == 0) {
        fb_fill_rect(&fb, sx - 1, sy + 11, 3, 6, COLOR_SHIRT_DARK);
        fb_fill_rect(&fb, sx + w - 2, sy + 13, 3, 6, COLOR_SHIRT_DARK);
    } else {
        fb_fill_rect(&fb, sx - 1, sy + 13, 3, 6, COLOR_SHIRT_DARK);
        fb_fill_rect(&fb, sx + w - 2, sy + 11, 3, 6, COLOR_SHIRT_DARK);
    }

    /* Animated legs */
    if (frame == 0) {
        fb_fill_rect(&fb, sx + 2, sy + 20, 4, 4, COLOR_PANTS);
        fb_fill_rect(&fb, sx + w - 5, sy + 21, 4, 3, COLOR_PANTS);
        fb_fill_rect(&fb, sx + 1, sy + 24, 5, 3, COLOR_SHOES);
        fb_fill_rect(&fb, sx + w - 6, sy + 24, 5, 3, COLOR_SHOES);
    } else {
        fb_fill_rect(&fb, sx + 2, sy + 21, 4, 3, COLOR_PANTS);
        fb_fill_rect(&fb, sx + w - 5, sy + 20, 4, 4, COLOR_PANTS);
        fb_fill_rect(&fb, sx + 1, sy + 24, 5, 3, COLOR_SHOES);
        fb_fill_rect(&fb, sx + w - 6, sy + 24, 5, 3, COLOR_SHOES);
    }
}

static void draw_player_jumping(int sx, int sy, bool right) {
    int w = player.width;

    /* Head */
    fb_fill_circle(&fb, sx + w / 2, sy + 4, 5, COLOR_SKIN);
    fb_fill_rect(&fb, sx + w / 2 - 4, sy, 8, 3, COLOR_HAIR);
    int eye_off = right ? 2 : -2;
    fb_draw_pixel(&fb, sx + w / 2 + eye_off - 1, sy + 3, COLOR_BLACK);
    fb_draw_pixel(&fb, sx + w / 2 + eye_off + 1, sy + 3, COLOR_BLACK);

    /* Body */
    fb_fill_rect(&fb, sx + 2, sy + 10, w - 4, 10, COLOR_SHIRT);
    fb_fill_rect(&fb, sx + w / 2 - 1, sy + 10, 2, 7, COLOR_TIE);

    /* Arms up */
    fb_fill_rect(&fb, sx, sy + 8, 2, 5, COLOR_SHIRT_DARK);
    fb_fill_rect(&fb, sx + w - 2, sy + 8, 2, 5, COLOR_SHIRT_DARK);

    /* Legs tucked */
    fb_fill_rect(&fb, sx + 2, sy + 20, 4, 4, COLOR_PANTS);
    fb_fill_rect(&fb, sx + w - 6, sy + 20, 4, 4, COLOR_PANTS);
    fb_fill_rect(&fb, sx + 1, sy + 24, 5, 3, COLOR_SHOES);
    fb_fill_rect(&fb, sx + w - 6, sy + 24, 5, 3, COLOR_SHOES);
}

static void draw_player_dying(int sx, int sy, int timer) {
    int w = player.width;
    int r = 3 + (DEATH_ANIM_FRAMES - timer) * 2;
    int cx = sx + w / 2;
    int cy = sy + player.height / 2;

    fb_fill_circle(&fb, cx, cy, r, COLOR_RED);
    if (r > 5) {
        fb_draw_line(&fb, cx - r/2, cy - r/2, cx + r/2, cy + r/2, COLOR_WHITE);
        fb_draw_line(&fb, cx + r/2, cy - r/2, cx - r/2, cy + r/2, COLOR_WHITE);
    }
}

static void draw_player_sprite(void) {
    int sx = (int)player.x - (int)camera.x;
    int sy = (int)player.y - (int)camera.y;

    /* Off-screen cull */
    if (sx + player.width < -16 || sx > (int)fb.width + 16) return;
    if (sy + player.height < -16 || sy > (int)fb.height + 16) return;

    /* Dying */
    if (player.state == PSTATE_DYING) {
        draw_player_dying(sx, sy, player.death_timer);
        return;
    }

    /* Invincibility blink */
    if (player.invincible_timer > 0 && (current_frame / 3) % 2)
        return;

    switch (player.state) {
    case PSTATE_IDLE:
    case PSTATE_FALLING:
        draw_player_idle(sx, sy, player.facing_right);
        break;
    case PSTATE_RUNNING:
        draw_player_running(sx, sy, player.facing_right, player.anim_frame);
        break;
    case PSTATE_JUMPING:
        draw_player_jumping(sx, sy, player.facing_right);
        break;
    default:
        draw_player_idle(sx, sy, player.facing_right);
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sprite Drawing — Enemies
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_enemy_walker(Enemy *e, int sx, int sy) {
    int w = e->width, h = e->height;

    /* Body — printer/box shape */
    fb_fill_rect(&fb, sx + 1, sy + 2, w - 2, h - 4, RGB(180,180,180));
    fb_fill_rect(&fb, sx + 1, sy + 2, w - 2, 3, RGB(120,120,120));

    /* Paper sticking out top */
    fb_fill_rect(&fb, sx + 3, sy, w - 6, 4, RGB(240,240,230));
    if (e->anim_frame)
        fb_fill_rect(&fb, sx + 4, sy - 2, w - 8, 3, RGB(240,240,230));

    /* Eyes */
    int ex1 = sx + w / 3, ex2 = sx + w * 2 / 3;
    fb_fill_circle(&fb, ex1, sy + h / 2, 2, RGB(255,60,60));
    fb_fill_circle(&fb, ex2, sy + h / 2, 2, RGB(255,60,60));

    /* Mouth */
    fb_draw_line(&fb, sx + w / 3, sy + h * 2 / 3,
                 sx + w * 2 / 3, sy + h * 2 / 3, RGB(40,40,40));

    /* Feet */
    int fo = e->anim_frame ? 1 : 0;
    fb_fill_rect(&fb, sx + 2, sy + h - 2 - fo, 3, 2, RGB(120,120,120));
    fb_fill_rect(&fb, sx + w - 5, sy + h - 2 + fo - 1, 3, 2, RGB(120,120,120));
}

static void draw_enemies_all(void) {
    for (int i = 0; i < enemy_count; i++) {
        Enemy *e = &enemies[i];
        if (!e->alive) continue;

        int sx = (int)e->x - (int)camera.x;
        int sy = (int)e->y - (int)camera.y;

        if (sx + e->width < -16 || sx > (int)fb.width + 16) continue;
        if (sy + e->height < -16 || sy > (int)fb.height + 16) continue;

        draw_enemy_walker(e, sx, sy);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tile Rendering
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_tiles(void) {
    int sx_start = (int)camera.x / TILE_SIZE;
    int sy_start = (int)camera.y / TILE_SIZE;
    int sx_end = sx_start + (int)fb.width / TILE_SIZE + 2;
    int sy_end = sy_start + (int)fb.height / TILE_SIZE + 2;

    if (sx_start < 0) sx_start = 0;
    if (sy_start < 0) sy_start = 0;
    if (sx_end > level_width) sx_end = level_width;
    if (sy_end > level_height) sy_end = level_height;

    for (int ty = sy_start; ty < sy_end; ty++) {
        for (int tx = sx_start; tx < sx_end; tx++) {
            if (level_tiles[ty][tx] != TILE_EMPTY) {
                int scrx = tx * TILE_SIZE - (int)camera.x;
                int scry = ty * TILE_SIZE - (int)camera.y;
                draw_single_tile(tx, ty, scrx, scry);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HUD Drawing
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_hud(void) {
    fb_fill_rect(&fb, 0, 0, (int)fb.width, HUD_HEIGHT, RGB(20,20,30));
    draw_menu_button(&fb, &menu_button);
    draw_exit_button(&fb, &exit_button);

    char buf[48];
    snprintf(buf, sizeof(buf), "SCORE: %d", score);
    fb_draw_text(&fb, 90, 12, buf, COLOR_WHITE, 2);

    snprintf(buf, sizeof(buf), "LEVEL %d", current_level + 1);
    int tw = text_measure_width(buf, 2);
    fb_draw_text(&fb, (int)fb.width / 2 - tw / 2, 12, buf, COLOR_CYAN, 2);

    /* Lives icons */
    int lx = (int)fb.width - BTN_EXIT_WIDTH - 20 - game_lives * 16;
    if (lx < (int)fb.width / 2 + 60) lx = (int)fb.width / 2 + 60;
    for (int i = 0; i < game_lives && i < 5; i++) {
        int ix = lx + i * 16;
        fb_fill_circle(&fb, ix + 6, 28, 5, COLOR_SHIRT);
        fb_fill_circle(&fb, ix + 6, 23, 3, COLOR_SKIN);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main Draw Function
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_all(void) {
    /* Welcome screen */
    if (current_screen == SCREEN_WELCOME) {
        fb_clear(&fb, COLOR_BLACK);
        draw_welcome_screen(&fb, "OFFICE RUNNER",
            "D-PAD: MOVE   A: JUMP   B: RUN\n"
            "STOMP ENEMIES FROM ABOVE\n"
            "COLLECT COINS   REACH THE FLAG",
            &start_button);
        return;
    }

    /* Sky background */
    fb_fill_rect_gradient(&fb, 0, 0, (int)fb.width, (int)fb.height,
                          RGB(135,206,235), RGB(100,180,220));

    /* Game world */
    draw_tiles();
    draw_enemies_all();
    draw_player_sprite();
    draw_hud();

    /* Touch controls overlay */
    if (!input.gamepad_connected && !input.keyboard_connected)
        gamepad_draw_touch_controls(&fb, &input);

    /* Pause overlay */
    if (current_screen == SCREEN_PAUSED) {
        modal_dialog_draw(&pause_dialog, &fb);
        return;
    }

    /* Level complete overlay */
    if (current_screen == SCREEN_LEVEL_COMPLETE) {
        fb_fill_rect_alpha(&fb, 0, 0, (int)fb.width, (int)fb.height,
                           COLOR_BLACK, 140);
        char buf[64];
        snprintf(buf, sizeof(buf), "LEVEL %d COMPLETE!", current_level + 1);
        text_draw_centered(&fb, (int)fb.width / 2, (int)fb.height / 2 - 40,
                          buf, COLOR_WHITE, 3);
        snprintf(buf, sizeof(buf), "SCORE: %d   COINS: %d/%d",
                 score, collected_coins, total_coins);
        text_draw_centered(&fb, (int)fb.width / 2, (int)fb.height / 2 + 10,
                          buf, COLOR_YELLOW, 2);
        if (current_level < MAX_LEVELS - 1)
            text_draw_centered(&fb, (int)fb.width / 2, (int)fb.height / 2 + 50,
                              "GET READY...", COLOR_WHITE, 2);
        else
            text_draw_centered(&fb, (int)fb.width / 2, (int)fb.height / 2 + 50,
                              "YOU WIN! CONGRATULATIONS!", COLOR_GREEN, 2);
        return;
    }

    /* Game over */
    if (current_screen == SCREEN_GAME_OVER) {
        touch_poll(&touch);
        TouchState ts = touch_get_state(&touch);
        GameOverAction act = gameover_update(&gos, &fb,
                                             ts.x, ts.y, ts.pressed);
        switch (act) {
        case GAMEOVER_ACTION_RESTART:
            reset_game();
            current_screen = SCREEN_PLAYING;
            break;
        case GAMEOVER_ACTION_EXIT:
            running = false;
            break;
        case GAMEOVER_ACTION_RESET_SCORES:
            break;
        case GAMEOVER_ACTION_NONE:
        default:
            break;
        }
        return;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Input Handling
 * ═══════════════════════════════════════════════════════════════════════════ */

static void handle_input(void) {
    touch_poll(&touch);
    TouchState ts = touch_get_state(&touch);
    gamepad_poll(&gamepad, &input, ts.x, ts.y, ts.pressed);
    uint32_t now = get_time_ms();

    switch (current_screen) {
    case SCREEN_WELCOME:
        if (ts.pressed && button_is_touched(&start_button, ts.x, ts.y)
            && button_check_press(&start_button, true, now)) {
            current_screen = SCREEN_PLAYING;
        }
        if (input.buttons[BTN_ID_JUMP].pressed ||
            input.buttons[BTN_ID_ACTION].pressed ||
            input.buttons[BTN_ID_PAUSE].pressed) {
            current_screen = SCREEN_PLAYING;
        }
        break;

    case SCREEN_PLAYING:
        if (input.buttons[BTN_ID_PAUSE].pressed) {
            current_screen = SCREEN_PAUSED;
            modal_dialog_show(&pause_dialog);
            return;
        }
        if (ts.pressed) {
            if (button_is_touched(&exit_button, ts.x, ts.y) &&
                button_check_press(&exit_button, true, now)) {
                fb_fade_out(&fb);
                running = false;
                return;
            }
            if (button_is_touched(&menu_button, ts.x, ts.y) &&
                button_check_press(&menu_button, true, now)) {
                current_screen = SCREEN_PAUSED;
                modal_dialog_show(&pause_dialog);
                return;
            }
        }
        break;

    case SCREEN_PAUSED:
        if (input.buttons[BTN_ID_PAUSE].pressed) {
            current_screen = SCREEN_PLAYING;
            modal_dialog_hide(&pause_dialog);
            break;
        }
        {
            ModalDialogAction act = modal_dialog_update(&pause_dialog,
                ts.x, ts.y, ts.pressed, now);
            if (act == MODAL_ACTION_BTN0) {
                current_screen = SCREEN_PLAYING;
                break;
            }
            if (act == MODAL_ACTION_BTN1) {
                fb_fade_out(&fb);
                running = false;
                break;
            }
        }
        break;

    case SCREEN_LEVEL_COMPLETE:
    {
        uint32_t elapsed = now - level_complete_timer;
        if (elapsed > 2500) {
            if (current_level < MAX_LEVELS - 1) {
                current_level++;
                load_level(current_level);
                current_screen = SCREEN_PLAYING;
            } else {
                current_screen = SCREEN_GAME_OVER;
                gameover_init(&gos, &fb, score, "YOU WIN!", NULL,
                              &hs_table, &touch);
            }
        }
    }
    break;

    case SCREEN_GAME_OVER:
        /* Handled in draw_all via gameover_update */
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main Entry Point
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    const char *fb_device    = "/dev/fb0";
    const char *touch_device = "/dev/input/touchscreen0";

    if (argc > 1) fb_device    = argv[1];
    if (argc > 2) touch_device = argv[2];

    /* Singleton guard */
    int lock_fd = acquire_instance_lock("platformer");
    (void)lock_fd;
    if (lock_fd < 0) {
        fprintf(stderr, "platformer: another instance is already running\n");
        return 1;
    }

    /* Signal handlers */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* Hardware init */
    hw_init();
    hw_set_backlight(100);
    hw_leds_off();
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

    /* Gamepad init */
    gamepad_init(&gamepad);

    /* Seed RNG */
    srand(time(NULL));

    /* Initialize game */
    init_game();

    printf("Office Runner started! Press Ctrl+C to exit.\n");

    /* Main loop (~30 FPS) */
    while (running) {
        handle_input();
        update_game();
        draw_all();
        fb_swap(&fb);
        usleep(33000);
    }

    /* Cleanup */
    hw_leds_off();
    hw_set_backlight(100);
    audio_close(&audio);
    gamepad_close(&gamepad);
    touch_close(&touch);
    fb_close(&fb);

    printf("Office Runner ended. Final score: %d\n", score);
    return 0;
}