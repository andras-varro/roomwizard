/*
 * Frogger Game - Native C Implementation
 * Optimized for 300MHz ARM with touchscreen
 * No browser overhead - direct framebuffer rendering
 * Features: LED effects, screen transitions, high scores
 * Supports keyboard, gamepad, and touch input via unified gamepad module
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
#include "../common/logger.h"
#include "../common/gamepad.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define NUM_ROWS          13
#define NUM_GOALS         5
#define HUD_HEIGHT        70
#define MAX_OBJECTS_PER_LANE 6
#define NUM_LANE_CONFIGS  10
#define HOP_DURATION      8       /* frames for hop animation */
#define TIMER_MAX         30.0f   /* seconds per life attempt */
#define DEATH_ANIM_FRAMES 12
#define INITIAL_LIVES     3
#define HOP_COOLDOWN_MS   200

/* ─── Color palette ─────────────────────────────────────────────────────── */

/* Environment */
#define COLOR_GRASS_DARK    RGB(34, 120, 34)
#define COLOR_GRASS_LIGHT   RGB(50, 160, 50)
#define COLOR_WATER_DARK    RGB(20, 50, 140)
#define COLOR_WATER_LIGHT   RGB(40, 80, 180)
#define COLOR_WATER_WAVE    RGB(60, 110, 200)
#define COLOR_ROAD_DARK     RGB(50, 50, 55)
#define COLOR_ROAD_LINE     RGB(200, 200, 60)

/* Frog */
#define COLOR_FROG_BODY     RGB(30, 180, 30)
#define COLOR_FROG_DARK     RGB(20, 120, 20)
#define COLOR_FROG_BELLY    RGB(150, 220, 100)
#define COLOR_FROG_EYE_W    RGB(255, 255, 255)
#define COLOR_FROG_EYE_B    RGB(0, 0, 0)

/* Vehicles */
#define COLOR_CAR_BLUE      RGB(40, 80, 200)
#define COLOR_CAR_YELLOW    RGB(220, 200, 40)
#define COLOR_TRUCK_PURPLE  RGB(120, 40, 160)
#define COLOR_TRUCK_ORANGE  RGB(220, 120, 30)
#define COLOR_RACE_CAR_CLR  RGB(255, 60, 60)
#define COLOR_WHEEL         RGB(30, 30, 30)
#define COLOR_WINDOW        RGB(150, 200, 240)

/* River objects */
#define COLOR_LOG_DARK      RGB(100, 60, 20)
#define COLOR_LOG_LIGHT     RGB(140, 90, 40)
#define COLOR_LOG_BARK      RGB(80, 45, 15)
#define COLOR_TURTLE_SHELL  RGB(50, 100, 50)
#define COLOR_TURTLE_DARK   RGB(30, 70, 30)
#define COLOR_TURTLE_HEAD   RGB(80, 140, 80)

/* Goal */
#define COLOR_LILYPAD       RGB(30, 140, 50)
#define COLOR_LILYPAD_LIGHT RGB(60, 180, 80)
#define COLOR_GOAL_FROG     RGB(80, 200, 80)

/* ═══════════════════════════════════════════════════════════════════════════
 * Data Structures
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    SCREEN_WELCOME,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_GAME_OVER
} GameScreen;

typedef enum {
    LANE_ROAD,
    LANE_RIVER
} LaneType;

typedef enum {
    OBJ_CAR,
    OBJ_TRUCK,
    OBJ_RACE_CAR,
    OBJ_LOG_SHORT,
    OBJ_LOG_MED,
    OBJ_LOG_LONG,
    OBJ_TURTLE_2,
    OBJ_TURTLE_3
} ObjectType;

typedef enum {
    DEATH_VEHICLE,
    DEATH_WATER,
    DEATH_TIMEOUT,
    DEATH_OFFSCREEN
} DeathType;

typedef struct {
    ObjectType type;
    float x;
    int width_cells;
    uint32_t color;
} LaneObject;

typedef struct {
    int row;
    LaneType type;
    int direction;   /* -1 = left, +1 = right */
    float speed;
    LaneObject objects[MAX_OBJECTS_PER_LANE];
    int object_count;
} Lane;

typedef struct {
    int col, row;
    float ride_offset;
    int target_col, target_row;
    float hop_progress;
    bool hopping;
    bool alive;
    int facing;  /* 0=up, 1=down, 2=left, 3=right */
} Frog;

typedef struct {
    int score, lives, level;
    bool goals_reached[NUM_GOALS];
    int goals_filled;
    float timer;
    int highest_row;
} GameStateData;

typedef struct {
    int row;
    LaneType type;
    ObjectType obj_type;
    int direction;
    float base_speed;
    int obj_width_cells;
    int spacing_cells;
    uint32_t color;
} LaneConfig;

typedef struct {
    bool active;
    int type;   /* 0=none, 1=goal, 2=death, 3=level_complete */
    uint32_t start_time;
} LEDEffect;

/* ═══════════════════════════════════════════════════════════════════════════
 * Global Variables (project convention)
 * ═══════════════════════════════════════════════════════════════════════════ */

Framebuffer fb;
TouchInput touch;
GamepadManager gamepad;
InputState input;
Audio audio;
HighScoreTable hs_table;
static GameOverScreen gos;
bool running = true;
GameScreen current_screen = SCREEN_WELCOME;
static uint32_t last_rescan_ms = 0;
#define RESCAN_INTERVAL_MS 5000

static Lane lanes[NUM_LANE_CONFIGS];
static Frog frog;
static GameStateData state;
static LEDEffect led_effect;

static int death_anim_frame = 0;
static bool death_anim_active = false;
static bool game_over_pending = false;
static uint32_t current_frame = 0;
static uint32_t last_frame_ms = 0;
static uint32_t last_hop_ms = 0;

/* Grid dimensions (computed once at init) */
static int cell_size;
static int num_cols;
static int grid_width;
static int grid_offset_x;
static int grid_offset_y;
static int goal_cols[NUM_GOALS];

/* UI Buttons */
Button menu_button;
Button exit_button;
Button start_button;
ModalDialog pause_dialog;

/* ─── Lane configuration table ──────────────────────────────────────────── */

static const LaneConfig lane_configs[NUM_LANE_CONFIGS] = {
    /* River lanes (rows 1-5) */
    { 1,  LANE_RIVER, OBJ_LOG_LONG,   +1, 0.8f, 4, 6, COLOR_LOG_LIGHT    },
    { 2,  LANE_RIVER, OBJ_TURTLE_3,   -1, 0.6f, 3, 5, COLOR_TURTLE_SHELL },
    { 3,  LANE_RIVER, OBJ_LOG_MED,    +1, 1.0f, 3, 5, COLOR_LOG_LIGHT    },
    { 4,  LANE_RIVER, OBJ_TURTLE_2,   -1, 0.7f, 2, 6, COLOR_TURTLE_SHELL },
    { 5,  LANE_RIVER, OBJ_LOG_SHORT,  +1, 0.9f, 2, 4, COLOR_LOG_LIGHT    },
    /* Road lanes (rows 7-11) */
    { 7,  LANE_ROAD,  OBJ_RACE_CAR,   -1, 2.0f, 1, 8, COLOR_RACE_CAR_CLR },
    { 8,  LANE_ROAD,  OBJ_CAR,        +1, 1.2f, 1, 6, COLOR_CAR_BLUE     },
    { 9,  LANE_ROAD,  OBJ_TRUCK,      -1, 0.8f, 2, 7, COLOR_TRUCK_PURPLE },
    { 10, LANE_ROAD,  OBJ_CAR,        +1, 1.4f, 1, 5, COLOR_CAR_YELLOW   },
    { 11, LANE_ROAD,  OBJ_TRUCK,      -1, 0.9f, 2, 6, COLOR_TRUCK_ORANGE },
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Function Prototypes
 * ═══════════════════════════════════════════════════════════════════════════ */

static void signal_handler(int sig);
static void init_game(void);
static void init_buttons(void);
static void init_lanes(void);
static void reset_game(void);
static void reset_frog_position(void);
static void compute_grid(void);
static void spawn_lane_objects(Lane *lane, const LaneConfig *cfg);
static void handle_input(void);
static void update_game(void);
static void update_lanes(void);
static void update_hop(void);
static void update_frog_ride(void);
static void update_timer(float dt);
static void update_death_animation(void);
static void check_road_collision(void);
static void check_river_collision(void);
static void check_goal_reached(void);
static void hop_frog(int drow, int dcol);
static void kill_frog(DeathType cause);
static void advance_level(void);
static int  lane_index_for_row(int row);
static void draw_all(void);
static void draw_playing_field(void);
static void draw_hud(void);
static void draw_timer_bar(void);
static void draw_life_icons(int x, int y);
static void draw_water_lane(int row);
static void draw_road_lane(int row);
static void draw_grass_lane(int row);
static void draw_goal_zone(void);
static void draw_frog_sprite(int screen_x, int screen_y);
static void draw_car(int sx, int sy, uint32_t color);
static void draw_truck(int sx, int sy, uint32_t color);
static void draw_race_car(int sx, int sy, uint32_t color);
static void draw_log_sprite(int sx, int sy, int width_cells);
static void draw_turtle_group(int sx, int sy, int count);
static void draw_death_splash(int screen_x, int screen_y, int frame);
static void draw_lane_objects(int lane_idx);
static void start_led_effect(int type);
static void update_led_effects(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * Signal Handler
 * ═══════════════════════════════════════════════════════════════════════════ */

static void signal_handler(int sig) {
    (void)sig;
    running = false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * LED Effects (non-blocking, same pattern as snake.c)
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
    case 1: /* Goal reached: green flash 200ms */
        if (elapsed < 200)
            hw_set_leds(HW_LED_COLOR_GREEN);
        else {
            hw_leds_off();
            led_effect.active = false;
        }
        break;

    case 2: /* Death: red pulse 3x200ms */
    {
        int pulse = (int)(elapsed / 200);
        int phase = (int)(elapsed % 200);
        if (pulse < 3)
            hw_set_red_led(phase < 100 ? 100 : 0);
        else {
            hw_leds_off();
            led_effect.active = false;
        }
    }
    break;

    case 3: /* Level complete: green/yellow alternation 600ms */
    {
        int phase = (int)((elapsed / 150) % 2);
        if (elapsed < 600) {
            if (phase == 0) hw_set_leds(HW_LED_COLOR_GREEN);
            else hw_set_leds(HW_LED_COLOR_YELLOW);
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Grid Computation
 * ═══════════════════════════════════════════════════════════════════════════ */

static void compute_grid(void) {
    int available_height = (int)fb.height - HUD_HEIGHT;
    cell_size = available_height / NUM_ROWS;
    if (cell_size < 8) cell_size = 8;
    num_cols = (int)fb.width / cell_size;
    if (num_cols < NUM_GOALS + 2) num_cols = NUM_GOALS + 2;
    grid_width = num_cols * cell_size;
    grid_offset_x = ((int)fb.width - grid_width) / 2;
    grid_offset_y = HUD_HEIGHT;

    /* Compute goal slot positions */
    if (num_cols <= NUM_GOALS * 2) {
        for (int i = 0; i < NUM_GOALS; i++)
            goal_cols[i] = (i * num_cols + num_cols / 2) / NUM_GOALS;
    } else {
        int spacing = num_cols / (NUM_GOALS + 1);
        for (int i = 0; i < NUM_GOALS; i++)
            goal_cols[i] = spacing * (i + 1);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Lane Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static int lane_index_for_row(int row) {
    for (int i = 0; i < NUM_LANE_CONFIGS; i++)
        if (lane_configs[i].row == row) return i;
    return -1;
}

static void spawn_lane_objects(Lane *lane, const LaneConfig *cfg) {
    int obj_px  = cfg->obj_width_cells * cell_size;
    int gap_px  = cfg->spacing_cells   * cell_size;
    int stride  = obj_px + gap_px;
    lane->object_count = 0;
    if (stride <= 0) return;

    for (float x = 0; x < grid_width + stride; x += (float)stride) {
        if (lane->object_count >= MAX_OBJECTS_PER_LANE) break;
        LaneObject *obj = &lane->objects[lane->object_count++];
        obj->type        = cfg->obj_type;
        obj->x           = x;
        obj->width_cells = cfg->obj_width_cells;
        obj->color       = cfg->color;
    }
}

static void init_lanes(void) {
    float speed_scale = (float)fb.width / 800.0f;
    if (speed_scale < 0.5f) speed_scale = 0.5f;

    for (int i = 0; i < NUM_LANE_CONFIGS; i++) {
        const LaneConfig *cfg = &lane_configs[i];
        Lane *lane      = &lanes[i];
        lane->row       = cfg->row;
        lane->type      = cfg->type;
        lane->direction = cfg->direction;
        lane->speed     = cfg->base_speed * speed_scale;
        spawn_lane_objects(lane, cfg);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Game Initialization
 * ═══════════════════════════════════════════════════════════════════════════ */

static void init_buttons(void) {
    button_init(&menu_button, 10, 10, BTN_MENU_WIDTH, BTN_MENU_HEIGHT, "",
                BTN_MENU_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&exit_button, (int)fb.width - BTN_EXIT_WIDTH - 10, 10,
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

static void reset_frog_position(void) {
    frog.row          = 12;
    frog.col          = num_cols / 2;
    frog.ride_offset  = 0;
    frog.hopping      = false;
    frog.alive        = true;
    frog.facing       = 0;
    frog.hop_progress = 0;
    frog.target_row   = frog.row;
    frog.target_col   = frog.col;
    state.timer       = TIMER_MAX;
    state.highest_row = 12;
}

static void reset_game(void) {
    state.score        = 0;
    state.lives        = INITIAL_LIVES;
    state.level        = 1;
    state.goals_filled = 0;
    for (int i = 0; i < NUM_GOALS; i++)
        state.goals_reached[i] = false;

    death_anim_active  = false;
    death_anim_frame   = 0;
    game_over_pending  = false;
    led_effect.active  = false;
    current_frame      = 0;

    init_lanes();
    reset_frog_position();
}

static void init_game(void) {
    compute_grid();
    init_buttons();
    hs_init(&hs_table, "frogger");
    hs_load(&hs_table);

    // Set up touch regions for gamepad module (4 directional zones around center)
    int sw = (int)fb.width;
    int sh = (int)fb.height;
    TouchRegion touch_regions[] = {
        /* Up: top band of play area */
        { 0, grid_offset_y, sw, (sh - grid_offset_y) / 3, BTN_ID_UP },
        /* Down: bottom band of play area */
        { 0, grid_offset_y + (sh - grid_offset_y) * 2 / 3, sw, (sh - grid_offset_y) / 3, BTN_ID_DOWN },
        /* Left: left third of screen */
        { 0, grid_offset_y, sw / 3, sh - grid_offset_y, BTN_ID_LEFT },
        /* Right: right third of screen */
        { sw * 2 / 3, grid_offset_y, sw / 3, sh - grid_offset_y, BTN_ID_RIGHT },
    };
    gamepad_set_touch_regions(&gamepad, touch_regions, 4);

    reset_game();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Level Progression
 * ═══════════════════════════════════════════════════════════════════════════ */

static void advance_level(void) {
    state.level++;
    state.score += 500;

    for (int i = 0; i < NUM_GOALS; i++)
        state.goals_reached[i] = false;
    state.goals_filled = 0;

    init_lanes();
    reset_frog_position();

    start_led_effect(3);
    audio_success(&audio);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Frog Movement
 * ═══════════════════════════════════════════════════════════════════════════ */

static void hop_frog(int drow, int dcol) {
    int new_row = frog.row + drow;
    int new_col = frog.col + dcol;

    if (new_col < 0 || new_col >= num_cols) return;
    if (new_row < 0 || new_row > 12) return;

    frog.target_row   = new_row;
    frog.target_col   = new_col;
    frog.hop_progress = 0.0f;
    frog.hopping      = true;
    frog.ride_offset  = 0;

    if      (drow < 0) frog.facing = 0;
    else if (drow > 0) frog.facing = 1;
    else if (dcol < 0) frog.facing = 2;
    else               frog.facing = 3;

    if (drow < 0 && new_row < state.highest_row) {
        state.score += 10;
        state.highest_row = new_row;
    }

    audio_beep(&audio);
    last_hop_ms = get_time_ms();
}

static void update_hop(void) {
    if (!frog.hopping) return;
    frog.hop_progress += 1.0f / HOP_DURATION;
    if (frog.hop_progress >= 1.0f) {
        frog.row          = frog.target_row;
        frog.col          = frog.target_col;
        frog.hop_progress = 0;
        frog.hopping      = false;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Object Movement
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_lanes(void) {
    float speed_mult = 1.0f + (state.level - 1) * 0.15f;
    if (speed_mult > 2.5f) speed_mult = 2.5f;

    for (int i = 0; i < NUM_LANE_CONFIGS; i++) {
        Lane *lane  = &lanes[i];
        float speed = lane->speed * speed_mult * lane->direction;

        for (int j = 0; j < lane->object_count; j++) {
            LaneObject *obj = &lane->objects[j];
            obj->x += speed;
            int obj_w = obj->width_cells * cell_size;

            if (lane->direction > 0 && obj->x > grid_width)
                obj->x -= grid_width + obj_w;
            else if (lane->direction < 0 && obj->x + obj_w < 0)
                obj->x += grid_width + obj_w;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Frog Riding on River Objects
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_frog_ride(void) {
    if (frog.hopping) return;
    if (frog.row < 1 || frog.row > 5) return;

    int idx = lane_index_for_row(frog.row);
    if (idx < 0) return;

    float speed_mult = 1.0f + (state.level - 1) * 0.15f;
    if (speed_mult > 2.5f) speed_mult = 2.5f;
    float speed = lanes[idx].speed * speed_mult * lanes[idx].direction;
    frog.ride_offset += speed;

    while (frog.ride_offset >= cell_size) {
        frog.col++;
        frog.ride_offset -= cell_size;
    }
    while (frog.ride_offset <= -cell_size) {
        frog.col--;
        frog.ride_offset += cell_size;
    }

    float frog_pixel_x = frog.col * cell_size + frog.ride_offset;
    if (frog_pixel_x < -cell_size || frog_pixel_x > grid_width)
        kill_frog(DEATH_OFFSCREEN);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Collision Detection
 * ═══════════════════════════════════════════════════════════════════════════ */

static void check_road_collision(void) {
    if (frog.row < 7 || frog.row > 11) return;
    if (frog.hopping || !frog.alive) return;

    int idx = lane_index_for_row(frog.row);
    if (idx < 0) return;
    Lane *lane = &lanes[idx];

    int p = cell_size / 8;
    if (p < 1) p = 1;
    float frog_x = frog.col * cell_size + frog.ride_offset + p;
    float frog_w = cell_size - p * 2;

    for (int i = 0; i < lane->object_count; i++) {
        float obj_x = lane->objects[i].x;
        float obj_w = lane->objects[i].width_cells * cell_size;
        if (frog_x < obj_x + obj_w && frog_x + frog_w > obj_x) {
            kill_frog(DEATH_VEHICLE);
            return;
        }
    }
}

static void check_river_collision(void) {
    if (frog.row < 1 || frog.row > 5) return;
    if (frog.hopping || !frog.alive) return;

    int idx = lane_index_for_row(frog.row);
    if (idx < 0) return;
    Lane *lane = &lanes[idx];

    int p = cell_size / 8;
    if (p < 1) p = 1;
    float frog_x = frog.col * cell_size + frog.ride_offset + p;
    float frog_w = cell_size - p * 2;
    bool on_object = false;

    for (int i = 0; i < lane->object_count; i++) {
        float obj_x = lane->objects[i].x;
        float obj_w = lane->objects[i].width_cells * cell_size;
        if (frog_x < obj_x + obj_w && frog_x + frog_w > obj_x) {
            on_object = true;
            break;
        }
    }

    if (!on_object)
        kill_frog(DEATH_WATER);
}

static void check_goal_reached(void) {
    if (frog.row != 0 || frog.hopping || !frog.alive) return;

    for (int i = 0; i < NUM_GOALS; i++) {
        if (frog.col == goal_cols[i] && !state.goals_reached[i]) {
            state.goals_reached[i] = true;
            state.goals_filled++;
            state.score += 50;
            state.score += (int)state.timer * 10;

            audio_success(&audio);
            start_led_effect(1);

            if (state.goals_filled >= NUM_GOALS)
                advance_level();
            else
                reset_frog_position();
            return;
        }
    }

    /* Landed on row 0 but NOT on a goal slot */
    kill_frog(DEATH_WATER);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Death and Respawn
 * ═══════════════════════════════════════════════════════════════════════════ */

static void kill_frog(DeathType cause) {
    (void)cause;  /* reserved for future death-type-specific animation */
    if (!frog.alive) return;
    frog.alive = false;
    state.lives--;

    start_led_effect(2);
    audio_fail(&audio);

    death_anim_frame  = 0;
    death_anim_active = true;

    if (state.lives <= 0)
        game_over_pending = true;
}

static void update_death_animation(void) {
    if (!death_anim_active) return;
    death_anim_frame++;

    if (death_anim_frame >= DEATH_ANIM_FRAMES) {
        death_anim_active = false;
        hw_leds_off();

        if (game_over_pending) {
            game_over_pending = false;
            current_screen = SCREEN_GAME_OVER;
            gameover_init(&gos, &fb, state.score, NULL, NULL,
                          &hs_table, &touch);
        } else {
            reset_frog_position();
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Timer
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_timer(float dt) {
    if (!frog.alive || frog.hopping) return;
    state.timer -= dt;
    if (state.timer <= 0) {
        state.timer = 0;
        kill_frog(DEATH_TIMEOUT);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Input Handling
 * ═══════════════════════════════════════════════════════════════════════════ */

static void handle_input(void) {
    touch_poll(&touch);
    TouchState ts = touch_get_state(&touch);
    uint32_t now = get_time_ms();

    // Poll gamepad/keyboard/touch through unified API
    gamepad_poll(&gamepad, &input, ts.x, ts.y, ts.pressed);

    // Periodic device rescan for hotplug support
    if (now - last_rescan_ms > RESCAN_INTERVAL_MS) {
        last_rescan_ms = now;
        gamepad_rescan(&gamepad);
    }

    // BTN_BACK always exits to launcher
    if (input.buttons[BTN_ID_BACK].pressed) {
        fb_fade_out(&fb);
        running = false;
        return;
    }

    /* ── Welcome screen ─────────────────────────────────────────────── */
    if (current_screen == SCREEN_WELCOME) {
        if (ts.pressed) {
            bool touched = button_is_touched(&start_button, ts.x, ts.y);
            if (button_check_press(&start_button, touched, now)) {
                current_screen = SCREEN_PLAYING;
                last_frame_ms  = get_time_ms();
                hw_set_led(LED_GREEN, 100);
                usleep(100000);
                hw_leds_off();
            }
        }
        // Gamepad/keyboard: start game
        if (input.buttons[BTN_ID_JUMP].pressed ||
            input.buttons[BTN_ID_ACTION].pressed ||
            input.buttons[BTN_ID_PAUSE].pressed) {
            current_screen = SCREEN_PLAYING;
            last_frame_ms  = get_time_ms();
            hw_set_led(LED_GREEN, 100);
            usleep(100000);
            hw_leds_off();
        }
        return;
    }

    /* ── Game over screen (handled by gameover_update in draw) ────── */
    if (current_screen == SCREEN_GAME_OVER) {
        // Allow gamepad restart
        if (input.buttons[BTN_ID_JUMP].pressed ||
            input.buttons[BTN_ID_ACTION].pressed) {
            reset_game();
            current_screen = SCREEN_PLAYING;
            last_frame_ms  = get_time_ms();
        }
        return;
    }

    /* ── Pause screen ────────────────────────────────────────────────── */
    if (current_screen == SCREEN_PAUSED) {
        // Gamepad: unpause with Pause button
        if (input.buttons[BTN_ID_PAUSE].pressed) {
            current_screen = SCREEN_PLAYING;
            last_frame_ms  = get_time_ms();
            return;
        }
        // Gamepad: resume with Jump/Action
        if (input.buttons[BTN_ID_JUMP].pressed ||
            input.buttons[BTN_ID_ACTION].pressed) {
            current_screen = SCREEN_PLAYING;
            last_frame_ms  = get_time_ms();
            return;
        }
        ModalDialogAction action = modal_dialog_update(&pause_dialog,
            ts.x, ts.y, ts.pressed, now);
        if (action == MODAL_ACTION_BTN0) {
            current_screen = SCREEN_PLAYING;
            last_frame_ms  = get_time_ms();
            return;
        }
        if (action == MODAL_ACTION_BTN1) {
            fb_fade_out(&fb);
            running = false;
            return;
        }
        return;
    }

    /* ── Playing screen ──────────────────────────────────────────────── */

    // Gamepad/keyboard: pause
    if (input.buttons[BTN_ID_PAUSE].pressed) {
        current_screen = SCREEN_PAUSED;
        modal_dialog_show(&pause_dialog);
        return;
    }

    // Touch: exit and menu buttons
    if (ts.pressed) {
        /* Exit button */
        if (button_is_touched(&exit_button, ts.x, ts.y) &&
            button_check_press(&exit_button, true, now)) {
            fb_fade_out(&fb);
            running = false;
            return;
        }
        /* Menu button */
        if (button_is_touched(&menu_button, ts.x, ts.y) &&
            button_check_press(&menu_button, true, now)) {
            current_screen = SCREEN_PAUSED;
            modal_dialog_show(&pause_dialog);
            return;
        }
        /* Direction input via touch (only in play area, with cooldown) */
        if (ts.y >= grid_offset_y && frog.alive && !death_anim_active) {
            if (now - last_hop_ms < HOP_COOLDOWN_MS) return;
            if (frog.hopping) return;

            int frog_sx = grid_offset_x + frog.col * cell_size
                          + cell_size / 2 + (int)frog.ride_offset;
            int frog_sy = grid_offset_y + frog.row * cell_size + cell_size / 2;

            int dx = ts.x - frog_sx;
            int dy = ts.y - frog_sy;
            int min_dist = cell_size / 2;
            if (abs(dx) < min_dist && abs(dy) < min_dist) return;

            if (abs(dx) > abs(dy)) {
                hop_frog(0, dx > 0 ? +1 : -1);
            } else {
                hop_frog(dy < 0 ? -1 : +1, 0);
            }
        }
    }

    // Gamepad/keyboard direction input (pressed edge, with hop cooldown)
    if (frog.alive && !death_anim_active && !frog.hopping) {
        if (now - last_hop_ms >= HOP_COOLDOWN_MS) {
            if (input.buttons[BTN_ID_UP].pressed) {
                hop_frog(-1, 0);
            } else if (input.buttons[BTN_ID_DOWN].pressed) {
                hop_frog(+1, 0);
            } else if (input.buttons[BTN_ID_LEFT].pressed) {
                hop_frog(0, -1);
            } else if (input.buttons[BTN_ID_RIGHT].pressed) {
                hop_frog(0, +1);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Game Update
 * ═══════════════════════════════════════════════════════════════════════════ */

static void update_game(void) {
    if (current_screen != SCREEN_PLAYING) return;

    current_frame++;
    uint32_t now = get_time_ms();
    float dt = (now - last_frame_ms) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    last_frame_ms = now;

    if (death_anim_active) {
        update_death_animation();
        return;
    }

    update_lanes();
    update_frog_ride();
    update_hop();
    update_timer(dt);

    if (!frog.hopping && frog.alive) {
        if (frog.row >= 7 && frog.row <= 11)
            check_road_collision();
        else if (frog.row >= 1 && frog.row <= 5)
            check_river_collision();
        else if (frog.row == 0)
            check_goal_reached();
    }

    update_led_effects();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sprite Drawing
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_frog_sprite(int sx, int sy) {
    int cs = cell_size;
    int p  = cs / 8;  if (p < 1) p = 1;
    int cx = sx + cs / 2;
    int bw = cs - p * 3;
    int bh = cs - p * 3;
    int bx = sx + (cs - bw) / 2;
    int by = sy + (cs - bh) / 2 + p / 2;

    fb_fill_rounded_rect(&fb, bx, by, bw, bh, p, COLOR_FROG_BODY);
    fb_fill_rect(&fb, bx + p, by + bh / 3, bw - p * 2, bh / 3, COLOR_FROG_BELLY);

    int lw = p * 2; if (lw < 2) lw = 2;
    int lh = p;     if (lh < 1) lh = 1;
    fb_fill_rect(&fb, bx - lw / 2,            by + p,          lw, lh, COLOR_FROG_DARK);
    fb_fill_rect(&fb, bx + bw - lw / 2,       by + p,          lw, lh, COLOR_FROG_DARK);
    fb_fill_rect(&fb, bx - lw / 2,            by + bh - p * 2, lw, lh, COLOR_FROG_DARK);
    fb_fill_rect(&fb, bx + bw - lw / 2,       by + bh - p * 2, lw, lh, COLOR_FROG_DARK);

    int er = cs > 40 ? 3 : 2;
    int pr = er > 2 ? 2 : 1;
    int ey = by + p;
    int elx = cx - bw / 4;
    int erx = cx + bw / 4;
    fb_fill_circle(&fb, elx, ey, er, COLOR_FROG_EYE_W);
    fb_fill_circle(&fb, erx, ey, er, COLOR_FROG_EYE_W);
    fb_fill_circle(&fb, elx, ey, pr, COLOR_FROG_EYE_B);
    fb_fill_circle(&fb, erx, ey, pr, COLOR_FROG_EYE_B);
}

static void draw_car(int sx, int sy, uint32_t color) {
    int cs = cell_size;
    int p  = cs / 8; if (p < 1) p = 1;
    int bx = sx + p, by = sy + p * 2;
    int bw = cs - p * 2, bh = cs - p * 4;
    if (bw < 2 || bh < 2) return;

    fb_fill_rounded_rect(&fb, bx, by, bw, bh, p / 2, color);

    uint32_t dk = RGB(((color >> 16) & 0xFF) * 3 / 4,
                      ((color >>  8) & 0xFF) * 3 / 4,
                      ( color        & 0xFF) * 3 / 4);
    fb_fill_rect(&fb, bx + p, by + p, bw - p * 2, bh / 3, dk);
    if (bw > p * 4)
        fb_fill_rect(&fb, bx + p * 2, by + p + 1, bw - p * 4, bh / 4, COLOR_WINDOW);

    int wr = p > 2 ? p : 2;
    fb_fill_circle(&fb, bx + p * 2,      by + bh, wr, COLOR_WHEEL);
    fb_fill_circle(&fb, bx + bw - p * 2, by + bh, wr, COLOR_WHEEL);
}

static void draw_truck(int sx, int sy, uint32_t color) {
    int cs = cell_size;
    int p  = cs / 8; if (p < 1) p = 1;
    int tw = cs * 2 - p * 2, th = cs - p * 4;
    int tx = sx + p, ty = sy + p * 2;
    if (tw < 4 || th < 2) return;

    int cargw = tw * 2 / 3;
    int cargx = tx + tw - cargw;
    fb_fill_rounded_rect(&fb, cargx, ty, cargw, th, p / 2, color);

    uint32_t dk = RGB(((color >> 16) & 0xFF) * 2 / 3,
                      ((color >>  8) & 0xFF) * 2 / 3,
                      ( color        & 0xFF) * 2 / 3);
    for (int i = 1; i < 3; i++) {
        int lx = cargx + (cargw * i) / 3;
        fb_draw_line(&fb, lx, ty + 2, lx, ty + th - 2, dk);
    }

    int cabw = tw - cargw;
    uint32_t cc = RGB(((color >> 16) & 0xFF) * 3 / 4,
                      ((color >>  8) & 0xFF) * 3 / 4,
                      ( color        & 0xFF) * 3 / 4);
    fb_fill_rounded_rect(&fb, tx, ty, cabw + 2, th, p / 2, cc);
    if (cabw > p * 2)
        fb_fill_rect(&fb, tx + p, ty + p, cabw - p * 2, th / 2, COLOR_WINDOW);

    int wr = p > 2 ? p : 2;
    fb_fill_circle(&fb, tx + p * 2,         ty + th, wr, COLOR_WHEEL);
    fb_fill_circle(&fb, tx + cabw,           ty + th, wr, COLOR_WHEEL);
    fb_fill_circle(&fb, cargx + cargw / 3,   ty + th, wr, COLOR_WHEEL);
    fb_fill_circle(&fb, cargx + cargw * 2/3, ty + th, wr, COLOR_WHEEL);
}

static void draw_race_car(int sx, int sy, uint32_t color) {
    int cs = cell_size;
    int p  = cs / 8; if (p < 1) p = 1;
    int bx = sx + p, by = sy + cs / 3;
    int bw = cs - p * 2, bh = cs / 2;
    if (bw < 2 || bh < 2) return;

    fb_fill_rounded_rect(&fb, bx, by, bw, bh, p, color);
    fb_draw_thick_line(&fb, bx + bw / 2, by + 2, bx + bw / 2, by + bh - 2,
                       2, COLOR_WHITE);
    int wr = p > 2 ? p : 2;
    fb_fill_circle(&fb, bx + p,      by + bh, wr, COLOR_WHEEL);
    fb_fill_circle(&fb, bx + bw - p, by + bh, wr, COLOR_WHEEL);
}

static void draw_log_sprite(int sx, int sy, int width_cells) {
    int cs = cell_size;
    int p  = cs / 8; if (p < 1) p = 1;
    int lw = width_cells * cs;
    int lh = cs - p * 3;
    int lx = sx;
    int ly = sy + p + p / 2;
    if (lw < 2 || lh < 4) return;

    fb_fill_rounded_rect(&fb, lx, ly, lw, lh, lh / 2, COLOR_LOG_LIGHT);

    for (int i = 1; i <= 3; i++) {
        int line_y = ly + (lh * i) / 4;
        fb_draw_line(&fb, lx + p * 2, line_y, lx + lw - p * 2, line_y,
                     COLOR_LOG_BARK);
    }

    int cr = lh / 2 - 1; if (cr < 1) cr = 1;
    fb_fill_circle(&fb, lx + cr + 1,      ly + lh / 2, cr, COLOR_LOG_DARK);
    fb_fill_circle(&fb, lx + lw - cr - 1, ly + lh / 2, cr, COLOR_LOG_DARK);
    if (cr > 2) {
        fb_draw_circle(&fb, lx + cr + 1,      ly + lh / 2, cr / 2, COLOR_LOG_BARK);
        fb_draw_circle(&fb, lx + lw - cr - 1, ly + lh / 2, cr / 2, COLOR_LOG_BARK);
    }
}

static void draw_turtle_group(int sx, int sy, int count) {
    int cs = cell_size;
    int p  = cs / 8; if (p < 1) p = 1;

    for (int i = 0; i < count; i++) {
        int tx  = sx + i * cs;
        int tcx = tx + cs / 2;
        int tcy = sy + cs / 2;
        int sr  = cs / 2 - p * 2; if (sr < 2) sr = 2;

        fb_fill_circle(&fb, tcx, tcy, sr, COLOR_TURTLE_SHELL);
        fb_draw_line(&fb, tcx - sr / 2, tcy, tcx + sr / 2, tcy, COLOR_TURTLE_DARK);
        fb_draw_line(&fb, tcx, tcy - sr / 2, tcx, tcy + sr / 2, COLOR_TURTLE_DARK);
        fb_fill_circle(&fb, tcx - 1, tcy - 1, sr / 3, COLOR_TURTLE_HEAD);
        fb_fill_circle(&fb, tcx + sr - 1, tcy - sr / 2, p + 1, COLOR_TURTLE_HEAD);
    }
}

static void draw_death_splash(int sx, int sy, int frame) {
    int cs = cell_size;
    int cx = sx + cs / 2, cy = sy + cs / 2;
    int r  = cs / 4 + frame * cs / 8;
    if (r > cs) r = cs;
    fb_fill_circle(&fb, cx, cy, r, COLOR_RED);
    fb_draw_line(&fb, cx - r / 2, cy - r / 2, cx + r / 2, cy + r / 2, COLOR_WHITE);
    fb_draw_line(&fb, cx + r / 2, cy - r / 2, cx - r / 2, cy + r / 2, COLOR_WHITE);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Lane Background Drawing
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_water_lane(int row) {
    int y = grid_offset_y + row * cell_size;
    int x = grid_offset_x;
    int w = grid_width;

    fb_fill_rect(&fb, x, y, w, cell_size, COLOR_WATER_DARK);

    int wy1 = y + cell_size / 3;
    int wy2 = y + cell_size * 2 / 3;
    int step = cell_size / 2; if (step < 4) step = 4;
    for (int wx = 0; wx < w; wx += step) {
        int off = ((int)(current_frame / 4) + wx / 8) % 6;
        fb_fill_rect(&fb, x + wx, wy1 + off - 3,
                     cell_size / 4, 2, COLOR_WATER_WAVE);
        fb_fill_rect(&fb, x + wx + cell_size / 4, wy2 - off + 3,
                     cell_size / 4, 2, COLOR_WATER_LIGHT);
    }
}

static void draw_road_lane(int row) {
    int y = grid_offset_y + row * cell_size;
    int x = grid_offset_x;
    int w = grid_width;

    fb_fill_rect(&fb, x, y, w, cell_size, COLOR_ROAD_DARK);

    int dash_y = y + cell_size / 2 - 1;
    int step = cell_size; if (step < 4) step = 4;
    for (int dx = 0; dx < w; dx += step)
        fb_fill_rect(&fb, x + dx + 2, dash_y, cell_size / 2 - 2, 2, COLOR_ROAD_LINE);
}

static void draw_grass_lane(int row) {
    int y = grid_offset_y + row * cell_size;
    int x = grid_offset_x;
    int w = grid_width;

    fb_fill_rect(&fb, x, y, w, cell_size, COLOR_GRASS_DARK);

    unsigned int seed = (unsigned int)(row * 1000);
    int dots = w / 8; if (dots < 1) dots = 1;
    for (int i = 0; i < dots; i++) {
        seed = seed * 1103515245u + 12345u;
        int ddx = (int)((seed >> 16) % (unsigned)w);
        seed = seed * 1103515245u + 12345u;
        int ddy = (int)((seed >> 16) % (unsigned)cell_size);
        fb_fill_rect(&fb, x + ddx, y + ddy, 2, 2, COLOR_GRASS_LIGHT);
    }
}

static void draw_goal_zone(void) {
    int y = grid_offset_y;
    int x = grid_offset_x;
    int w = grid_width;

    fb_fill_rect(&fb, x, y, w, cell_size, COLOR_WATER_DARK);

    for (int i = 0; i < NUM_GOALS; i++) {
        int px = grid_offset_x + goal_cols[i] * cell_size + cell_size / 2;
        int py = y + cell_size / 2;
        int pr = cell_size / 2 - 2; if (pr < 3) pr = 3;

        if (state.goals_reached[i]) {
            fb_fill_circle(&fb, px, py, pr, COLOR_LILYPAD);
            fb_fill_circle(&fb, px, py, pr / 2, COLOR_GOAL_FROG);
            fb_fill_circle(&fb, px - pr / 4, py - pr / 3, 2, COLOR_FROG_EYE_W);
            fb_fill_circle(&fb, px + pr / 4, py - pr / 3, 2, COLOR_FROG_EYE_W);
        } else {
            fb_fill_circle(&fb, px, py, pr, COLOR_LILYPAD);
            fb_draw_line(&fb, px, py, px - pr / 2, py - pr, COLOR_WATER_DARK);
            fb_draw_line(&fb, px, py, px + pr / 2, py - pr, COLOR_WATER_DARK);
            fb_fill_circle(&fb, px + 1, py + 1, pr / 3, COLOR_LILYPAD_LIGHT);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Draw Lane Objects
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_lane_objects(int lane_idx) {
    Lane *lane = &lanes[lane_idx];
    int y = grid_offset_y + lane->row * cell_size;

    for (int j = 0; j < lane->object_count; j++) {
        LaneObject *obj = &lane->objects[j];
        int ox = grid_offset_x + (int)obj->x;
        int ow = obj->width_cells * cell_size;

        if (ox + ow < grid_offset_x - cell_size ||
            ox > grid_offset_x + grid_width + cell_size)
            continue;

        switch (obj->type) {
        case OBJ_CAR:       draw_car(ox, y, obj->color);               break;
        case OBJ_TRUCK:     draw_truck(ox, y, obj->color);             break;
        case OBJ_RACE_CAR:  draw_race_car(ox, y, obj->color);          break;
        case OBJ_LOG_SHORT:
        case OBJ_LOG_MED:
        case OBJ_LOG_LONG:  draw_log_sprite(ox, y, obj->width_cells);  break;
        case OBJ_TURTLE_2:  draw_turtle_group(ox, y, 2);               break;
        case OBJ_TURTLE_3:  draw_turtle_group(ox, y, 3);               break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HUD Drawing
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_timer_bar(void) {
    int bx = grid_offset_x;
    int by = HUD_HEIGHT - 18;
    int bw = grid_width;
    int bh = 12;

    fb_fill_rect(&fb, bx, by, bw, bh, RGB(40, 40, 40));

    float ratio = state.timer / TIMER_MAX;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    int fill_w = (int)(bw * ratio);

    uint32_t color;
    if      (state.timer > 10.0f) color = COLOR_GREEN;
    else if (state.timer >  5.0f) color = COLOR_YELLOW;
    else                          color = COLOR_RED;

    fb_fill_rect(&fb, bx, by, fill_w, bh, color);
    fb_draw_rect(&fb, bx, by, bw, bh, COLOR_WHITE);
}

static void draw_life_icons(int x, int y) {
    for (int i = 0; i < state.lives && i < 5; i++) {
        int ix = x + i * 18;
        fb_fill_circle(&fb, ix + 6, y + 6, 5, COLOR_FROG_BODY);
        fb_fill_circle(&fb, ix + 3, y + 3, 1, COLOR_FROG_EYE_W);
        fb_fill_circle(&fb, ix + 9, y + 3, 1, COLOR_FROG_EYE_W);
    }
}

static void draw_hud(void) {
    fb_fill_rect(&fb, 0, 0, (int)fb.width, HUD_HEIGHT, RGB(20, 20, 30));
    draw_menu_button(&fb, &menu_button);
    draw_exit_button(&fb, &exit_button);

    /* Score */
    char buf[48];
    int txt_x;
    if ((int)fb.width < 600) {
        snprintf(buf, sizeof(buf), "SC:%d", state.score);
        txt_x = 90;
    } else {
        snprintf(buf, sizeof(buf), "SCORE: %d", state.score);
        txt_x = (int)fb.width / 2 - 100;
    }
    fb_draw_text(&fb, txt_x, 12, buf, COLOR_WHITE, 2);

    /* Level */
    if ((int)fb.width < 600) {
        snprintf(buf, sizeof(buf), "LV:%d", state.level);
        txt_x = (int)fb.width / 2 + 20;
    } else {
        snprintf(buf, sizeof(buf), "LVL: %d", state.level);
        txt_x = (int)fb.width / 2 + 80;
    }
    fb_draw_text(&fb, txt_x, 12, buf, COLOR_CYAN, 2);

    /* Lives */
    int lives_x = (int)fb.width - BTN_EXIT_WIDTH - 20 - state.lives * 18;
    if (lives_x < txt_x + 60) lives_x = txt_x + 60;
    draw_life_icons(lives_x, 30);

    /* Timer bar */
    draw_timer_bar();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Playing Field Drawing
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_playing_field(void) {
    /* 1. Draw lane backgrounds */
    for (int row = 0; row < NUM_ROWS; row++) {
        if (row == 0) {
            draw_goal_zone();
        } else if (row >= 1 && row <= 5) {
            draw_water_lane(row);
        } else if (row == 6 || row == 12) {
            draw_grass_lane(row);
        } else if (row >= 7 && row <= 11) {
            draw_road_lane(row);
        }
    }

    /* 2. Draw lane objects */
    for (int i = 0; i < NUM_LANE_CONFIGS; i++)
        draw_lane_objects(i);

    /* 3. Draw frog */
    if (death_anim_active) {
        /* Death animation at frog's position */
        int fx, fy;
        if (frog.hopping) {
            float t = frog.hop_progress;
            fx = grid_offset_x + (int)(frog.col * cell_size * (1 - t) +
                                       frog.target_col * cell_size * t
                                       + frog.ride_offset);
            fy = grid_offset_y + (int)(frog.row * cell_size * (1 - t) +
                                       frog.target_row * cell_size * t);
        } else {
            fx = grid_offset_x + frog.col * cell_size + (int)frog.ride_offset;
            fy = grid_offset_y + frog.row * cell_size;
        }
        draw_death_splash(fx, fy, death_anim_frame);
    } else if (frog.alive) {
        int fx, fy;
        if (frog.hopping) {
            float t = frog.hop_progress;
            int from_x = grid_offset_x + frog.col * cell_size;
            int from_y = grid_offset_y + frog.row * cell_size;
            int to_x   = grid_offset_x + frog.target_col * cell_size;
            int to_y   = grid_offset_y + frog.target_row * cell_size;
            fx = from_x + (int)((to_x - from_x) * t);
            fy = from_y + (int)((to_y - from_y) * t);
            /* Arc: lift the frog during mid-hop */
            int arc = (int)(sinf(t * 3.14159f) * cell_size / 3);
            fy -= arc;
        } else {
            fx = grid_offset_x + frog.col * cell_size + (int)frog.ride_offset;
            fy = grid_offset_y + frog.row * cell_size;
        }
        draw_frog_sprite(fx, fy);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main Draw Function
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_all(void) {
    fb_clear(&fb, COLOR_BLACK);

    /* ── Welcome screen ──────────────────────────────────────────────── */
    if (current_screen == SCREEN_WELCOME) {
        draw_welcome_screen(&fb, "FROGGER",
            "D-PAD/ARROWS: HOP\n"
            "CROSS ROAD AND RIVER\n"
            "REACH THE LILY PADS\n"
            "PRESS START OR TAP TO BEGIN",
            &start_button);
        return;
    }

    /* Draw the playing field as background for PLAYING, PAUSED, GAME_OVER */
    draw_hud();
    draw_playing_field();

    /* ── Pause overlay ───────────────────────────────────────────────── */
    if (current_screen == SCREEN_PAUSED) {
        modal_dialog_draw(&pause_dialog, &fb);
        return;
    }

    /* ── Game over overlay ───────────────────────────────────────────── */
    if (current_screen == SCREEN_GAME_OVER) {
        TouchState go_ts = touch_get_state(&touch);
        GameOverAction action = gameover_update(&gos, &fb,
                                                go_ts.x, go_ts.y, go_ts.pressed);
        switch (action) {
        case GAMEOVER_ACTION_RESTART:
            reset_game();
            current_screen = SCREEN_PLAYING;
            last_frame_ms  = get_time_ms();
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

    /* Virtual touch controls overlay when no physical controller */
    if (!input.gamepad_connected && !input.keyboard_connected)
        gamepad_draw_touch_controls(&fb, &input);

    /* Hint text at bottom */
    if (!input.gamepad_connected && !input.keyboard_connected)
        fb_draw_text(&fb, 10, (int)fb.height - 20,
                     "TAP DIRECTION TO HOP", RGB(100, 100, 100), 1);
    else
        fb_draw_text(&fb, 10, (int)fb.height - 20,
                     "ARROWS/D-PAD: HOP  ESC: PAUSE", RGB(100, 100, 100), 1);
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
    int lock_fd = acquire_instance_lock("frogger");
    (void)lock_fd;  /* held open for process lifetime */
    if (lock_fd < 0) {
        fprintf(stderr, "frogger: another instance is already running\n");
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

    printf("Frogger game started! Touch screen to play.\n");
    printf("Press Ctrl+C to exit.\n");

    /* ── Main game loop (~60 FPS) ────────────────────────────────────── */
    while (running) {
        handle_input();
        update_game();
        draw_all();
        fb_swap(&fb);
        usleep(16666);
    }

    /* Cleanup */
    hw_leds_off();
    hw_set_backlight(100);
    audio_close(&audio);
    gamepad_close(&gamepad);
    touch_close(&touch);
    fb_close(&fb);

    printf("Frogger game ended. Final score: %d\n", state.score);
    return 0;
}