/*
 * Brick Breaker — Native C Implementation for RoomWizard
 *
 * Features:
 *   - Touch-controlled paddle (drag to move, tap to launch)
 *   - 10 levels with varied brick patterns and increasing difficulty
 *   - 9 power-ups in opposing pairs with permanent stacking behavior
 *   - Gradient bricks, glowing ball, smooth 60 FPS rendering
 *   - High-score leaderboard, audio feedback, LED effects
 *
 * Hardware: 800×480 framebuffer, resistive touchscreen, OSS audio, LEDs
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

/* ══════════════════════════════════════════════════════════════════════════
 *  Constants
 * ══════════════════════════════════════════════════════════════════════════ */

/* Play area within the safe zone */
#define AREA_X      SCREEN_SAFE_LEFT
#define AREA_Y      (SCREEN_SAFE_TOP + 65)      /* leave room for buttons (50px) + small gap */
#define AREA_W      SCREEN_SAFE_WIDTH
#define AREA_H      (SCREEN_SAFE_HEIGHT - 65)

/* Paddle */
#define PADDLE_Y        (AREA_Y + AREA_H - 28)
#define PADDLE_HEIGHT   14
#define PADDLE_BASE_W   100

/* Stepped paddle widths: index 0=narrow(-2), 1=small(-1), 2=normal(0), 3=large(+1), 4=larger(+2) */
#define PADDLE_LEVELS 5
static const int PADDLE_WIDTHS[PADDLE_LEVELS] = { 40, 70, 100, 140, 180 };

/* Stepped ball speed multipliers: 0=very slow(-2), 1=slow(-1), 2=normal(0), 3=fast(+1), 4=very fast(+2) */
#define SPEED_LEVELS 5
static const float SPEED_MULTS[SPEED_LEVELS] = { 0.55f, 0.75f, 1.0f, 1.25f, 1.5f };

/* Ball */
#define BALL_RADIUS     6
#define MAX_BALLS       8
#define BALL_BASE_SPEED 4.0f
#define BALL_MAX_SPEED  11.0f
#define BALL_SPEED_INC  0.25f  /* Increased from 0.15f for faster progression */

/* Bricks -- compile-time maximums (for array sizing) and orientation presets */
#define BRICK_COLS_LANDSCAPE  12
#define BRICK_COLS_PORTRAIT   8    /* fewer cols in portrait for wider, more playable bricks */
#define BRICK_ROWS_BASE       8    /* base max rows (landscape) -- level defs use this */
#define BRICK_MAX_COLS        12
#define BRICK_MAX_ROWS        16
#define BRICK_PAD       3
#define BRICK_H         20
#define MAX_BRICKS      (BRICK_MAX_COLS * BRICK_MAX_ROWS)  /* 192 */

/* Runtime brick grid dimensions (set by init_brick_layout after fb_init) */
static int brick_cols = BRICK_COLS_LANDSCAPE;
static int brick_rows = BRICK_ROWS_BASE;

/* Power-ups */
#define MAX_POWERUPS    8
#define POWERUP_SIZE    18
#define POWERUP_SPEED   2
#define POWERUP_CHANCE  30      /* percent */

/* Lives and levels */
#define STARTING_LIVES  5
#define MAX_LEVELS      10

/* Particles */
#define MAX_PARTICLES   150

/* Fireball trail */
#define TRAIL_LEN       6

/* ══════════════════════════════════════════════════════════════════════════
 *  Types
 * ══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    SCREEN_WELCOME,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_LEVEL_COMPLETE,
    SCREEN_GAME_OVER
} GameScreen;

typedef enum {
    BRICK_NORMAL = 0,      /* Regular brick */
    BRICK_INDESTRUCTIBLE,  /* Can only be destroyed with fireball */
    BRICK_BONUS,           /* Gives bonus points (2x) */
    BRICK_EXPLOSIVE        /* Destroys adjacent bricks when hit */
} BrickType;

typedef enum {
    PU_WIDEN,        /* W - make paddle wider (one step) */
    PU_NARROW,       /* N - make paddle narrower (one step) */
    PU_FIREBALL,     /* F - activate fireball mode */
    PU_NORMALBALL,   /* B - deactivate fireball, return to normal */
    PU_SPEED_UP,     /* > - speed up ball (one step) */
    PU_SLOW_DOWN,    /* < - slow down ball (one step) */
    PU_EXTRA_LIFE,   /* + - gain one life */
    PU_LOSE_LIFE,    /* - - lose one life (but NOT if on last life!) */
    PU_MULTIBALL,    /* M - spawn extra balls */
    NUM_POWERUP_TYPES
} PowerUpType;

typedef struct {
    float x, y;
    float dx, dy;
    float speed;
    bool  active;
    bool  stuck;     /* stuck on paddle until launched */
    bool  fireball;
    /* Fireball visual trail (circular buffer of recent positions) */
    float trail_x[TRAIL_LEN], trail_y[TRAIL_LEN];
    int   trail_idx;  /* next write position in circular buffer */
} Ball;

typedef struct {
    int  x, y, w, h;
    int  health;     /* 0 = destroyed, -1 = indestructible */
    int  max_health;
    BrickType type;  /* brick special type */
    int  color_index; /* row index for colorful rendering */
    uint32_t color_top;
    uint32_t color_bot;
} Brick;

typedef struct {
    float x, y;
    PowerUpType type;
    bool  active;
} PowerUp;

typedef struct {
    float x, y;
    float dx, dy;
    int   life;     /* frames remaining */
    uint32_t color;
} Particle;

typedef struct {
    int paddle_level;    /* -2 to +2, starts at 0 (index into PADDLE_WIDTHS is paddle_level + 2) */
    int speed_level;     /* -2 to +2, starts at 0 (index into SPEED_MULTS is speed_level + 2) */
    int fireball;        /* 0 or 1 */
} ActiveEffects;

typedef struct {
    int         score;
    int         lives;
    int         level;        /* 1-based */
    int         bricks_left;
    int         paddle_w;
    float       paddle_x;    /* centre X */
    Ball        balls[MAX_BALLS];
    int         ball_count;
    Brick       bricks[MAX_BRICKS];
    int         brick_count;
    PowerUp     powerups[MAX_POWERUPS];
    Particle    particles[MAX_PARTICLES];
    ActiveEffects fx;
    GameScreen  screen;
    bool        ball_launched;
    /* Screen shake */
    float       shake_x, shake_y;       /* current offset to apply to rendering */
    int         shake_frames;           /* frames remaining */
    float       shake_intensity;        /* current max amplitude in pixels */

    /* Level clear animation cooldown */
    int         clear_cooldown;         /* frames remaining before LEVEL_COMPLETE transition */

    /* Deferred explosion chain */
    int         pending_exp[MAX_BRICKS]; /* brick indices waiting to detonate */
    int         pending_exp_timer[MAX_BRICKS]; /* frames until each detonation */
    int         pending_exp_count;
} GameState;

/* ══════════════════════════════════════════════════════════════════════════
 *  Globals
 * ══════════════════════════════════════════════════════════════════════════ */

static Framebuffer  fb;
static TouchInput   touch;
static Audio        audio;
static GameState    game;
static HighScoreTable hs;
static bool         running = true;
static uint32_t     frame_time_ms;  /* updated each frame */
static bool test_mode = false;     /* --test: special test levels */

/* UI buttons */
static Button btn_menu, btn_exit;
static Button btn_start, btn_restart;
static Button btn_resume, btn_retire, btn_exit_pause;
static Button btn_next_level;
static ToggleSwitch toggle_test;

/* ── colour palette ──────────────────────────────────────────────────── */
#define BG_COLOR        RGB(8, 12, 30)
#define HUD_COLOR       RGB(200, 200, 200)
#define HEART_COLOR     RGB(255, 60, 80)
#define PADDLE_COLOR    RGB(0, 220, 255)
#define PADDLE_GLOW     RGB(0, 100, 140)
#define BALL_COLOR      RGB(255, 255, 255)
#define BALL_FIRE       RGB(255, 140, 0)

/* Brick colour sets per health [top, bottom] */
static const uint32_t brick_colors[][2] = {
    { RGB(80, 220, 80),   RGB(40, 140, 40)  },  /* health 1 — green */
    { RGB(255, 255, 80),  RGB(180, 160, 30) },  /* health 2 — yellow */
    { RGB(255, 160, 50),  RGB(180, 90, 20)  },  /* health 3 — orange */
    { RGB(255, 60, 60),   RGB(160, 30, 30)  },  /* health 4 — red */
    { RGB(180, 100, 255), RGB(120, 50, 180) },  /* health 5 — purple */
    { RGB(0, 200, 255),   RGB(0, 120, 180)  },  /* health 6 — cyan */
};

/* Row-based color palette for single-hit bricks (rainbow gradient) */
static const uint32_t ROW_COLORS[][2] = {
    { RGB(255, 60, 60),   RGB(180, 30, 30) },   /* 0: Red */
    { RGB(255, 140, 40),  RGB(180, 80, 20) },   /* 1: Orange */
    { RGB(255, 220, 40),  RGB(180, 160, 20) },  /* 2: Yellow */
    { RGB(80, 220, 80),   RGB(40, 140, 40) },   /* 3: Green */
    { RGB(40, 200, 200),  RGB(20, 130, 130) },  /* 4: Teal */
    { RGB(60, 140, 255),  RGB(30, 80, 180) },   /* 5: Blue */
    { RGB(160, 100, 255), RGB(100, 50, 180) },  /* 6: Purple */
    { RGB(255, 100, 200), RGB(180, 50, 130) },  /* 7: Pink */
};
#define NUM_ROW_COLORS 8

/* Special brick colors */
#define BRICK_INDESTRUCTIBLE_TOP  RGB(80, 80, 80)
#define BRICK_INDESTRUCTIBLE_BOT  RGB(50, 50, 50)
#define BRICK_BONUS_TOP           RGB(255, 215, 0)
#define BRICK_BONUS_BOT           RGB(200, 160, 0)
#define BRICK_EXPLOSIVE_TOP       RGB(255, 100, 0)
#define BRICK_EXPLOSIVE_BOT       RGB(180, 50, 0)

/* Power-up visual config (color, letter) per type */
static const struct { uint32_t color; char symbol; const char *name; } pu_info[] = {
    [PU_WIDEN]      = { RGB(80, 220, 80),   'W', "WIDEN"   },
    [PU_NARROW]     = { RGB(255, 60, 60),   'N', "NARROW"  },
    [PU_FIREBALL]   = { RGB(255, 140, 0),   'F', "FIRE"    },
    [PU_NORMALBALL] = { RGB(200, 200, 200), 'B', "NORMAL"  },
    [PU_SPEED_UP]   = { RGB(0, 200, 255),   '>', "SPD UP"  },
    [PU_SLOW_DOWN]  = { RGB(255, 255, 80),  '<', "SPD DN"  },
    [PU_EXTRA_LIFE] = { RGB(255, 100, 200), '+', "LIFE+"   },
    [PU_LOSE_LIFE]  = { RGB(180, 60, 180),  '-', "LIFE-"   },
    [PU_MULTIBALL]  = { RGB(60, 140, 255),  'M', "MULTI"   },
};

/* ══════════════════════════════════════════════════════════════════════════
 *  Level definitions
 * ══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    PAT_STANDARD,
    PAT_CHECKERBOARD,
    PAT_PYRAMID,
    PAT_DIAMOND,
    PAT_BORDER,
    PAT_STRIPES,
} BrickPattern;

typedef struct {
    int         rows;
    int         health[BRICK_ROWS_BASE];  /* per-row starting health (base/landscape layout) */
    BrickPattern pattern;
    float       speed_mult;          /* ball speed multiplier */
} LevelDef;

static const LevelDef levels[MAX_LEVELS] = {
    { 5, {1,1,1,1,1,0,0,0},             PAT_STANDARD,     1.0f },
    { 5, {1,1,2,1,1,0,0,0},             PAT_STANDARD,     1.0f },
    { 6, {2,1,2,1,1,1,0,0},             PAT_CHECKERBOARD, 1.05f },
    { 6, {2,2,1,2,1,1,0,0},             PAT_PYRAMID,      1.1f },
    { 7, {2,2,2,2,1,1,1,0},             PAT_DIAMOND,      1.15f },
    { 7, {3,2,2,2,1,1,1,0},             PAT_BORDER,       1.2f },
    { 7, {3,2,2,2,2,1,1,0},             PAT_STRIPES,      1.25f },
    { 8, {3,3,2,2,2,1,1,1},             PAT_CHECKERBOARD, 1.3f },
    { 8, {3,3,3,3,2,2,1,1},             PAT_PYRAMID,      1.35f },
    { 8, {4,4,3,3,2,2,1,1},             PAT_DIAMOND,      1.4f },
};

/* ══════════════════════════════════════════════════════════════════════════
 *  Helpers
 * ══════════════════════════════════════════════════════════════════════════ */

static void signal_handler(int sig) { (void)sig; running = false; }

static float randf(void) { return (float)rand() / (float)RAND_MAX; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ── Particles ───────────────────────────────────────────────────────── */

/* Called after fb_init() to compute brick_cols and brick_rows from screen size.
 * In landscape (800x480): brick_cols=12, brick_rows=8 (unchanged from original)
 * In portrait  (480x800): brick_cols=8  (wider bricks), brick_rows=~14 (fill top 45%) */
static void init_brick_layout(void) {
    if (fb.portrait_mode) {
        brick_cols = BRICK_COLS_PORTRAIT;
        /* Calculate rows to fill ~45% of the play area height */
        int available_brick_height = (int)(AREA_H * 0.45f);
        brick_rows = available_brick_height / (BRICK_H + BRICK_PAD);
        if (brick_rows < BRICK_ROWS_BASE) brick_rows = BRICK_ROWS_BASE;
        if (brick_rows > BRICK_MAX_ROWS) brick_rows = BRICK_MAX_ROWS;
    } else {
        brick_cols = BRICK_COLS_LANDSCAPE;
        brick_rows = BRICK_ROWS_BASE;
    }
    printf("Brick layout: %dx%d (portrait=%d, AREA_H=%d)\n",
           brick_cols, brick_rows, fb.portrait_mode, AREA_H);
}

static void spawn_particles(float cx, float cy, uint32_t color, int count) {
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < MAX_PARTICLES; j++) {
            if (game.particles[j].life <= 0) {
                Particle *p = &game.particles[j];
                p->x = cx;
                p->y = cy;
                p->dx = (randf() - 0.5f) * 6.0f;
                p->dy = (randf() - 0.5f) * 6.0f;
                p->life = 12 + (rand() % 10);
                p->color = color;
                break;
            }
        }
    }
}

static void update_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &game.particles[i];
        if (p->life <= 0) continue;
        p->x += p->dx;
        p->y += p->dy;
        p->dy += 0.15f;  /* gravity */
        p->life--;
    }
}

static void draw_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &game.particles[i];
        if (p->life <= 0) continue;
        int px = (int)p->x;
        int py = (int)p->y;
        uint8_t alpha = (uint8_t)(255 * p->life / 22);
        fb_draw_pixel_alpha(&fb, px, py, p->color, alpha);
        fb_draw_pixel_alpha(&fb, px + 1, py, p->color, alpha);
        fb_draw_pixel_alpha(&fb, px, py + 1, p->color, alpha);
        fb_draw_pixel_alpha(&fb, px + 1, py + 1, p->color, alpha);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Brick layout
 * ══════════════════════════════════════════════════════════════════════════ */

static bool pattern_has_brick(BrickPattern pat, int row, int col, int rows, int cols) {
    switch (pat) {
    case PAT_STANDARD:
        return true;
    case PAT_CHECKERBOARD:
        return (row + col) % 2 == 0;
    case PAT_PYRAMID: {
        int bricks_in_row = cols - row * 2;
        if (bricks_in_row <= 0) return false;
        int start = (cols - bricks_in_row) / 2;
        return col >= start && col < start + bricks_in_row;
    }
    case PAT_DIAMOND: {
        int mid_row = rows / 2;
        int dist = abs(row - mid_row);
        int half = (cols / 2) - dist;
        if (half < 1) half = 1;
        int start = cols / 2 - half;
        int end   = cols / 2 + half;
        return col >= start && col < end;
    }
    case PAT_BORDER:
        return row == 0 || row == rows - 1 || col == 0 || col == cols - 1;
    case PAT_STRIPES:
        return col % 3 != 1;  /* every 3rd column gap */
    }
    return true;
}

static void create_bricks(void) {
    const LevelDef *lv = &levels[clampi(game.level - 1, 0, MAX_LEVELS - 1)];
    int brick_w = (AREA_W - BRICK_PAD * (brick_cols + 1)) / brick_cols;
    game.brick_count = 0;
    game.bricks_left = 0;
    
    /* Scale the level's row count proportionally to the available brick_rows.
     * In landscape: brick_rows == BRICK_ROWS_BASE (8), so scaled_rows == lv->rows (unchanged).
     * In portrait:  brick_rows is larger (~14), so levels get proportionally more rows. */
    int scaled_rows = lv->rows * brick_rows / BRICK_ROWS_BASE;
    if (scaled_rows > brick_rows) scaled_rows = brick_rows;
    if (scaled_rows < lv->rows) scaled_rows = lv->rows;  /* never fewer than base */

    /* Add larger vertical offset - creates space above bricks for ball bouncing */
    int vertical_offset = 40;  /* pixels from top of play area - allows ball to go behind/above bricks */

    for (int r = 0; r < scaled_rows && r < brick_rows; r++) {
        for (int c = 0; c < brick_cols; c++) {
            if (!pattern_has_brick(lv->pattern, r, c, scaled_rows, brick_cols))
                continue;

            /* Map this row back to the base health pattern.
             * Stretches the original health[] across the scaled row count. */
            int base_r = r * lv->rows / scaled_rows;
            if (base_r >= lv->rows) base_r = lv->rows - 1;

            Brick *b = &game.bricks[game.brick_count];
            b->x = AREA_X + BRICK_PAD + c * (brick_w + BRICK_PAD);
            b->y = AREA_Y + BRICK_PAD + vertical_offset + r * (BRICK_H + BRICK_PAD);
            b->w = brick_w;
            b->h = BRICK_H;
            b->health = lv->health[base_r];
            b->max_health = b->health;
            b->color_index = r;
            
            /* Determine brick type - special bricks on higher levels
             * Level 3-6: 15% chance; Level 7+: 25% chance
             * Among special bricks, explosive is weighted ~50% */
            b->type = BRICK_NORMAL;
            int special_chance = (game.level >= 7) ? 25 : 15;
            if (game.level >= 3 && (rand() % 100) < special_chance) {
                int roll = rand() % 100;
                if (roll < 15 && game.level >= 5) {
                    /* 15% of specials: indestructible (level 5+) */
                    b->type = BRICK_INDESTRUCTIBLE;
                    b->health = -1;  /* Special marker for indestructible */
                    b->color_top = BRICK_INDESTRUCTIBLE_TOP;
                    b->color_bot = BRICK_INDESTRUCTIBLE_BOT;
                } else if (roll < 40) {
                    /* 25% of specials: bonus */
                    b->type = BRICK_BONUS;
                    b->color_top = BRICK_BONUS_TOP;
                    b->color_bot = BRICK_BONUS_BOT;
                } else {
                    /* ~60% of specials: explosive */
                    b->type = BRICK_EXPLOSIVE;
                    b->color_top = BRICK_EXPLOSIVE_TOP;
                    b->color_bot = BRICK_EXPLOSIVE_BOT;
                }
            } else {
                /* Normal brick - use health-based colors */
                int ci = clampi(b->health - 1, 0, 5);
                b->color_top = brick_colors[ci][0];
                b->color_bot = brick_colors[ci][1];
            }

            /* ── Test mode level overrides ────────────────────── */
            if (test_mode) {
                if (game.level == 1) {
                    /* Test level 1: ALL bricks are explosive */
                    b->type = BRICK_EXPLOSIVE;
                    b->health = 1;
                    b->max_health = 1;
                    b->color_top = BRICK_EXPLOSIVE_TOP;
                    b->color_bot = BRICK_EXPLOSIVE_BOT;
                } else if (game.level == 2) {
                    /* Test level 2: normal bricks, but power-ups guaranteed (handled in spawn_powerup) */
                    /* Keep normal brick type */
                }
            }

            game.brick_count++;
            /* Only count non-indestructible bricks for level completion */
            if (b->type != BRICK_INDESTRUCTIBLE) {
                game.bricks_left++;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Effects helpers
 * ══════════════════════════════════════════════════════════════════════════ */

/* Reset all active effects to default (called on ball lost / level complete) */
static void reset_effects(void) {
    game.fx.paddle_level = 0;
    game.fx.speed_level = 0;
    game.fx.fireball = 0;
}

/* Apply paddle width from current effect level */
static void apply_paddle_width(void) {
    int idx = clampi(game.fx.paddle_level + 2, 0, PADDLE_LEVELS - 1);
    game.paddle_w = PADDLE_WIDTHS[idx];
}

/* Get current speed multiplier from effect level */
static float get_speed_mult(void) {
    int idx = clampi(game.fx.speed_level + 2, 0, SPEED_LEVELS - 1);
    return SPEED_MULTS[idx];
}

/* Normalize all active ball speeds to current base * level_mult * effect_mult */
static void normalize_ball_speeds(void) {
    const LevelDef *lv = &levels[clampi(game.level - 1, 0, MAX_LEVELS - 1)];
    float effect_mult = get_speed_mult();
    for (int i = 0; i < game.ball_count; i++) {
        Ball *b = &game.balls[i];
        if (!b->active || b->stuck) continue;
        /* Recompute speed with new multiplier, preserving per-brick increments */
        float base = b->speed / lv->speed_mult;  /* remove old level mult */
        /* Re-apply with effect multiplier */
        b->speed = base * lv->speed_mult * effect_mult;
        float mag = sqrtf(b->dx * b->dx + b->dy * b->dy);
        if (mag > 0.01f) {
            b->dx = (b->dx / mag) * b->speed;
            b->dy = (b->dy / mag) * b->speed;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Ball management
 * ══════════════════════════════════════════════════════════════════════════ */

static Ball *add_ball(float x, float y, float dx, float dy, float speed) {
    if (game.ball_count >= MAX_BALLS) return NULL;
    Ball *b = &game.balls[game.ball_count++];
    b->x = x; b->y = y;
    b->dx = dx; b->dy = dy;
    b->speed = speed;
    b->active = true;
    b->stuck = false;
    b->fireball = (game.fx.fireball != 0);
    /* Initialize trail positions to starting position */
    for (int t = 0; t < TRAIL_LEN; t++) {
        b->trail_x[t] = x;
        b->trail_y[t] = y;
    }
    b->trail_idx = 0;
    return b;
}

static void reset_ball_on_paddle(void) {
    game.ball_count = 0;
    float start_x = game.paddle_x;
    float start_y = PADDLE_Y - BALL_RADIUS - 1;
    Ball *b = add_ball(start_x, start_y, 0, 0, 0);
    if (b) {
        const LevelDef *lv = &levels[clampi(game.level - 1, 0, MAX_LEVELS - 1)];
        b->speed = BALL_BASE_SPEED * lv->speed_mult * get_speed_mult();
        b->stuck = true;
        /* Re-initialize trail to paddle position (add_ball already did this) */
    }
    game.ball_launched = false;
}

static void launch_ball(void) {
    if (game.ball_launched) return;
    for (int i = 0; i < game.ball_count; i++) {
        Ball *b = &game.balls[i];
        if (b->stuck) {
            float angle = -M_PI / 2.0f + (randf() - 0.5f) * 0.6f;
            b->dx = cosf(angle) * b->speed;
            b->dy = sinf(angle) * b->speed;
            b->stuck = false;
            game.ball_launched = true;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Power-up management
 * ══════════════════════════════════════════════════════════════════════════ */

static void spawn_powerup(float x, float y) {
    /* In test mode level 2+: every brick drops a power-up */
    if (!test_mode || game.level < 2) {
        if ((rand() % 100) >= POWERUP_CHANCE) return;
    }

    /* Weighted random selection among 9 types.
     * Positive (widen, fireball, slow_down, extra_life, multiball): ~13% each = 65%
     * Negative (narrow, normalball, speed_up, lose_life): ~8-9% each = 35%
     * Using weights summing to 100:
     *   PU_WIDEN=13, PU_NARROW=9, PU_FIREBALL=13, PU_NORMALBALL=9,
     *   PU_SPEED_UP=9, PU_SLOW_DOWN=13, PU_EXTRA_LIFE=13, PU_LOSE_LIFE=8, PU_MULTIBALL=13
     */
    static const struct { PowerUpType type; int weight; } pu_weights[] = {
        { PU_WIDEN,      13 },
        { PU_NARROW,      9 },
        { PU_FIREBALL,   13 },
        { PU_NORMALBALL,  9 },
        { PU_SPEED_UP,    9 },
        { PU_SLOW_DOWN,  13 },
        { PU_EXTRA_LIFE, 13 },
        { PU_LOSE_LIFE,   8 },
        { PU_MULTIBALL,  13 },
    };
    static const int total_weight = 100;

    int roll = rand() % total_weight;
    PowerUpType type = PU_WIDEN;  /* fallback */
    int cumulative = 0;
    for (int i = 0; i < (int)(sizeof(pu_weights) / sizeof(pu_weights[0])); i++) {
        cumulative += pu_weights[i].weight;
        if (roll < cumulative) {
            type = pu_weights[i].type;
            break;
        }
    }

    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!game.powerups[i].active) {
            game.powerups[i] = (PowerUp){ x, y, type, true };
            return;
        }
    }
}

static void apply_powerup(PowerUpType type) {
    hw_set_led(LED_GREEN, 100);

    switch (type) {
    case PU_WIDEN:
        if (game.fx.paddle_level < 2) game.fx.paddle_level++;
        apply_paddle_width();
        break;
    case PU_NARROW:
        if (game.fx.paddle_level > -2) game.fx.paddle_level--;
        apply_paddle_width();
        break;
    case PU_FIREBALL:
        game.fx.fireball = 1;
        for (int i = 0; i < game.ball_count; i++)
            game.balls[i].fireball = true;
        break;
    case PU_NORMALBALL:
        game.fx.fireball = 0;
        for (int i = 0; i < game.ball_count; i++)
            game.balls[i].fireball = false;
        break;
    case PU_SPEED_UP:
        if (game.fx.speed_level < 2) game.fx.speed_level++;
        normalize_ball_speeds();
        break;
    case PU_SLOW_DOWN:
        if (game.fx.speed_level > -2) game.fx.speed_level--;
        normalize_ball_speeds();
        break;
    case PU_EXTRA_LIFE:
        game.lives++;
        audio_blip(&audio);
        break;
    case PU_LOSE_LIFE:
        if (game.lives > 1) game.lives--;
        break;
    case PU_MULTIBALL: {
        /* Spawn 2 new balls moving upward at different angles */
        int n = game.ball_count;
        for (int i = 0; i < n && game.ball_count < MAX_BALLS - 1; i++) {
            Ball *src = &game.balls[i];
            if (!src->active || src->stuck) continue;
            
            /* Spawn balls moving upward at -60° and -120° (30° left/right of vertical) */
            float angle1 = -M_PI / 2.0f - 0.5f;  /* upward-left */
            float angle2 = -M_PI / 2.0f + 0.5f;  /* upward-right */
            
            add_ball(src->x, src->y,
                     cosf(angle1) * src->speed,
                     sinf(angle1) * src->speed,
                     src->speed);
            add_ball(src->x, src->y,
                     cosf(angle2) * src->speed,
                     sinf(angle2) * src->speed,
                     src->speed);
        }
        audio_blip(&audio);
        break;
    }
    default:
        break;
    }

    /* Brief LED + beep */
    if (type != PU_MULTIBALL && type != PU_EXTRA_LIFE)
        audio_beep(&audio);

    usleep(50000);
    hw_leds_off();
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Game state management
 * ══════════════════════════════════════════════════════════════════════════ */

static void init_level(void) {
    /* Reset power-ups, effects, and screen shake */
    memset(game.powerups, 0, sizeof(game.powerups));
    memset(game.particles, 0, sizeof(game.particles));
    game.shake_x = 0; game.shake_y = 0;
    game.shake_frames = 0; game.shake_intensity = 0;
    game.clear_cooldown = 0;
    game.pending_exp_count = 0;
    reset_effects();
    apply_paddle_width();
    game.paddle_x = AREA_X + AREA_W / 2;
    game.ball_count = 0;

    create_bricks();
    reset_ball_on_paddle();
}

static void reset_game(void) {
    game.score = 0;
    game.lives = STARTING_LIVES;
    game.level = 1;
    game.screen = SCREEN_PLAYING;
    init_level();
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Physics / update
 * ══════════════════════════════════════════════════════════════════════════ */

/* Queue a brick for deferred detonation (chain explosion with delay) */
static void queue_deferred_explosion(int brick_idx, int delay_frames) {
    if (game.pending_exp_count >= MAX_BRICKS) return;
    /* Don't queue if already pending */
    for (int i = 0; i < game.pending_exp_count; i++) {
        if (game.pending_exp[i] == brick_idx) return;
    }
    game.pending_exp[game.pending_exp_count] = brick_idx;
    game.pending_exp_timer[game.pending_exp_count] = delay_frames;
    game.pending_exp_count++;
}

/* Process one deferred detonation: destroy the brick and its non-explosive neighbors,
 * queue any adjacent explosive bricks for further chain reaction */
static void detonate_brick(int brick_idx) {
    Brick *br = &game.bricks[brick_idx];
    if (br->health <= 0) return;  /* Already destroyed (e.g., by ball in the meantime) */

    br->health = 0;
    if (br->type != BRICK_INDESTRUCTIBLE) {
        game.bricks_left--;
    }
    game.score += 15 * game.level;

    /* Explosion particles */
    spawn_particles((float)(br->x + br->w / 2),
                    (float)(br->y + br->h / 2),
                    BRICK_EXPLOSIVE_TOP, 6);
    spawn_powerup((float)(br->x + br->w / 2),
                  (float)(br->y + br->h / 2));

    /* Shake — logarithmic accumulation */
    if (game.shake_frames < 3) {
        game.shake_frames = 10;
        game.shake_intensity = 3.0f;
    } else {
        game.shake_frames += 3;
        float headroom = 8.0f - game.shake_intensity;
        if (headroom > 0.2f)
            game.shake_intensity += headroom * 0.3f;
        if (game.shake_frames > 30)
            game.shake_frames = 30;
    }

    /* Destroy adjacent bricks */
    for (int k = 0; k < game.brick_count; k++) {
        Brick *adj = &game.bricks[k];
        if (adj->health <= 0) continue;
        if (k == brick_idx) continue;

        int adx = abs((adj->x + adj->w/2) - (br->x + br->w/2));
        int ady = abs((adj->y + adj->h/2) - (br->y + br->h/2));
        if (adx < br->w * 1.5f && ady < br->h * 1.5f) {
            if (adj->type == BRICK_INDESTRUCTIBLE) continue;

            if (adj->type == BRICK_EXPLOSIVE) {
                /* Chain: queue with delay for visual ripple */
                queue_deferred_explosion(k, 5);
            } else {
                /* Non-explosive neighbor: destroy immediately */
                adj->health = 0;
                game.bricks_left--;
                game.score += 10 * game.level;
                spawn_particles((float)(adj->x + adj->w / 2),
                               (float)(adj->y + adj->h / 2),
                               adj->color_top, 4);
                spawn_powerup((float)(adj->x + adj->w / 2),
                              (float)(adj->y + adj->h / 2));
            }
        }
    }

}

static void update_balls(void) {
    int paddle_left  = (int)game.paddle_x - game.paddle_w / 2;
    int paddle_right = paddle_left + game.paddle_w;

    for (int i = 0; i < game.ball_count; i++) {
        Ball *b = &game.balls[i];
        if (!b->active) continue;

        /* Stuck ball follows paddle */
        if (b->stuck) {
            b->x = game.paddle_x;
            b->y = PADDLE_Y - BALL_RADIUS - 1;
            /* Update trail to paddle position while stuck */
            for (int t = 0; t < TRAIL_LEN; t++) {
                b->trail_x[t] = b->x;
                b->trail_y[t] = b->y;
            }
            continue;
        }

        /* Push current position into trail buffer before moving */
        b->trail_x[b->trail_idx] = b->x;
        b->trail_y[b->trail_idx] = b->y;
        b->trail_idx = (b->trail_idx + 1) % TRAIL_LEN;

        b->x += b->dx;
        b->y += b->dy;

        /* Wall collisions */
        if (b->x - BALL_RADIUS < AREA_X) {
            b->x = AREA_X + BALL_RADIUS;
            b->dx = fabsf(b->dx);
        }
        if (b->x + BALL_RADIUS > AREA_X + AREA_W) {
            b->x = AREA_X + AREA_W - BALL_RADIUS;
            b->dx = -fabsf(b->dx);
        }
        if (b->y - BALL_RADIUS < AREA_Y) {
            b->y = AREA_Y + BALL_RADIUS;
            b->dy = fabsf(b->dy);
        }

        /* Paddle collision */
        if (b->dy > 0 &&
            b->y + BALL_RADIUS >= PADDLE_Y &&
            b->y - BALL_RADIUS <= PADDLE_Y + PADDLE_HEIGHT &&
            b->x >= paddle_left && b->x <= paddle_right) {

            b->y = PADDLE_Y - BALL_RADIUS;
            /* Angle depends on where ball hit the paddle */
            float hit = (b->x - paddle_left) / (float)game.paddle_w;  /* 0..1 */
            float angle = (hit - 0.5f) * 1.2f;  /* -0.6 .. +0.6 rad */
            /* Ensure ball always goes upward */
            float out_angle = -M_PI / 2.0f + angle;
            b->dx = cosf(out_angle) * b->speed;
            b->dy = sinf(out_angle) * b->speed;

            audio_interrupt(&audio);
            audio_tone(&audio, 600, 30);
        }

        /* Bottom — ball lost */
        if (b->y - BALL_RADIUS > PADDLE_Y + PADDLE_HEIGHT + 20) {
            b->active = false;
        }

        /* Brick collisions */
        for (int j = 0; j < game.brick_count; j++) {
            Brick *br = &game.bricks[j];
            if (br->health <= 0) continue;

            /* AABB check */
            if (b->x + BALL_RADIUS <= br->x || b->x - BALL_RADIUS >= br->x + br->w ||
                b->y + BALL_RADIUS <= br->y || b->y - BALL_RADIUS >= br->y + br->h)
                continue;

            /* Hit! Handle special brick types */
            bool brick_destroyed = false;
            
            if (br->type == BRICK_INDESTRUCTIBLE) {
                /* Indestructible bricks can only be destroyed by fireball */
                if (b->fireball) {
                    br->health = 0;
                    brick_destroyed = true;
                    game.score += 50 * game.level;  /* Bonus for destroying indestructible */
                }
                /* Otherwise just bounce off */
            } else if (br->type == BRICK_BONUS) {
                /* Bonus brick - double points */
                br->health = 0;
                brick_destroyed = true;
                game.score += 20 * game.level;  /* 2x normal points */
                audio_interrupt(&audio);
                audio_tone(&audio, 1500, 30);  /* Higher pitch for bonus */
            } else if (br->type == BRICK_EXPLOSIVE) {
                /* Explosive brick — immediate detonation + deferred chain */
                br->health = 0;
                brick_destroyed = true;
                game.score += 15 * game.level;
                
                /* Initial shake */
                game.shake_frames = 10;
                game.shake_intensity = 3.0f;
                
                /* Destroy adjacent non-explosive bricks immediately,
                 * queue adjacent explosive bricks for deferred chain reaction */
                for (int k = 0; k < game.brick_count; k++) {
                    if (k == j) continue;
                    Brick *adj = &game.bricks[k];
                    if (adj->health <= 0) continue;
                    
                    int adx = abs((adj->x + adj->w/2) - (br->x + br->w/2));
                    int ady = abs((adj->y + adj->h/2) - (br->y + br->h/2));
                    if (adx < br->w * 1.5f && ady < br->h * 1.5f) {
                        if (adj->type == BRICK_INDESTRUCTIBLE) continue;
                        
                        if (adj->type == BRICK_EXPLOSIVE) {
                            /* Defer chain reaction — explodes 5 frames later */
                            queue_deferred_explosion(k, 5);
                        } else {
                            /* Non-explosive neighbor: destroy now */
                            adj->health = 0;
                            game.bricks_left--;
                            game.score += 10 * game.level;
                            spawn_particles((float)(adj->x + adj->w / 2),
                                          (float)(adj->y + adj->h / 2),
                                          adj->color_top, 4);
                            spawn_powerup((float)(adj->x + adj->w / 2),
                                         (float)(adj->y + adj->h / 2));
                        }
                    }
                }
                
                audio_interrupt(&audio);
                audio_tone(&audio, 1000, 40);
            } else {
                /* Normal brick */
                if (b->fireball) {
                    br->health = 0;
                    brick_destroyed = true;
                } else {
                    br->health--;
                    /* Update colours for new health */
                    int ci = clampi(br->health - 1, 0, 5);
                    br->color_top = brick_colors[ci][0];
                    br->color_bot = brick_colors[ci][1];
                    if (br->health <= 0) {
                        brick_destroyed = true;
                    }
                }
                game.score += 10 * game.level;
            }

            if (brick_destroyed) {
                if (br->type != BRICK_INDESTRUCTIBLE) {
                    game.bricks_left--;
                }
                /* Fireball destruction: fire-colored particles, more of them */
                if (b->fireball) {
                    /* Fire-colored particles for fireball destruction */
                    uint32_t fc[] = {
                        RGB(255, 80, 0),
                        RGB(255, 200, 0),
                        RGB(255, 140, 0),
                        RGB(255, 50, 20)
                    };
                    for (int pi = 0; pi < 8; pi++) {
                        uint32_t pc = fc[rand() % 4];
                        spawn_particles((float)(br->x + br->w / 2),
                                        (float)(br->y + br->h / 2),
                                        pc, 1);
                    }
                } else {
                    spawn_particles((float)(br->x + br->w / 2),
                                    (float)(br->y + br->h / 2),
                                    br->color_top, 6);
                }
                spawn_powerup((float)(br->x + br->w / 2),
                              (float)(br->y + br->h / 2));
                audio_interrupt(&audio);
                audio_tone(&audio, 1200, 25);
            } else if (br->type != BRICK_INDESTRUCTIBLE) {
                audio_interrupt(&audio);
                audio_tone(&audio, 800, 20);
            }

            /* Bounce (skip if fireball or hit indestructible without fireball) */
            if (!b->fireball || br->type == BRICK_INDESTRUCTIBLE) {
                float overlap_l = (b->x + BALL_RADIUS) - br->x;
                float overlap_r = (br->x + br->w) - (b->x - BALL_RADIUS);
                float overlap_t = (b->y + BALL_RADIUS) - br->y;
                float overlap_b = (br->y + br->h) - (b->y - BALL_RADIUS);
                float min_o = overlap_l;
                if (overlap_r < min_o) min_o = overlap_r;
                if (overlap_t < min_o) min_o = overlap_t;
                if (overlap_b < min_o) min_o = overlap_b;

                if (min_o == overlap_l || min_o == overlap_r)
                    b->dx = -b->dx;
                else
                    b->dy = -b->dy;
            }

            /* Speed up slightly */
            if (b->speed < BALL_MAX_SPEED) {
                b->speed += BALL_SPEED_INC;
                float mag = sqrtf(b->dx * b->dx + b->dy * b->dy);
                if (mag > 0.01f) {
                    b->dx = (b->dx / mag) * b->speed;
                    b->dy = (b->dy / mag) * b->speed;
                }
            }

            break;  /* one brick per frame per ball */
        }
    }

    /* Remove inactive balls */
    int alive = 0;
    for (int i = 0; i < game.ball_count; i++) {
        if (game.balls[i].active) {
            if (alive != i) game.balls[alive] = game.balls[i];
            alive++;
        }
    }
    game.ball_count = alive;
}

static void update_powerups(void) {
    int paddle_left  = (int)game.paddle_x - game.paddle_w / 2;
    int paddle_right = paddle_left + game.paddle_w;

    for (int i = 0; i < MAX_POWERUPS; i++) {
        PowerUp *pu = &game.powerups[i];
        if (!pu->active) continue;

        pu->y += POWERUP_SPEED;

        /* Caught by paddle */
        if (pu->y + POWERUP_SIZE / 2 >= PADDLE_Y &&
            pu->x >= paddle_left && pu->x <= paddle_right) {
            apply_powerup(pu->type);
            pu->active = false;
            continue;
        }

        /* Off screen */
        if (pu->y > AREA_Y + AREA_H + POWERUP_SIZE) {
            pu->active = false;
        }
    }
}

static void update_game(void) {
    if (game.screen != SCREEN_PLAYING) return;

    frame_time_ms = get_time_ms();

    /* Continuously apply paddle width from effect level */
    apply_paddle_width();

    update_balls();
    update_powerups();
    update_particles();

    /* Process deferred explosions (chain reaction with visual delay) */
    for (int i = 0; i < game.pending_exp_count; ) {
        game.pending_exp_timer[i]--;
        if (game.pending_exp_timer[i] <= 0) {
            int brick_idx = game.pending_exp[i];
            /* Remove from queue (swap with last) */
            game.pending_exp[i] = game.pending_exp[game.pending_exp_count - 1];
            game.pending_exp_timer[i] = game.pending_exp_timer[game.pending_exp_count - 1];
            game.pending_exp_count--;
            /* Detonate */
            detonate_brick(brick_idx);
            /* Don't increment i — swapped element needs checking */
        } else {
            i++;
        }
    }

    /* Screen shake update */
    if (game.shake_frames > 0) {
        game.shake_frames--;
        float t = game.shake_intensity * ((float)game.shake_frames / 30.0f);
        game.shake_x = ((rand() % 200) / 100.0f - 1.0f) * t;
        game.shake_y = ((rand() % 200) / 100.0f - 1.0f) * t;
        if (game.shake_frames <= 0) {
            game.shake_x = 0;
            game.shake_y = 0;
            game.shake_intensity = 0;
        }
    }

    /* All balls lost? (skip during level clear cooldown) */
    if (game.ball_count == 0 && game.ball_launched && game.clear_cooldown == 0) {
        game.lives--;
        hw_set_led(LED_RED, 100);
        audio_fail(&audio);
        usleep(300000);
        hw_leds_off();

        /* Reset effects on ball lost */
        reset_effects();
        apply_paddle_width();

        if (game.lives <= 0) {
            game.screen = SCREEN_GAME_OVER;
            /* Check high score */
            if (hs_qualifies(&hs, game.score) >= 0) {
                hs_insert(&hs, "PLAYER", game.score);
                hs_save(&hs);
            }
        } else {
            reset_ball_on_paddle();
        }
    }

    /* Level clear? — Start cooldown to let explosions/particles play out */
    if (game.bricks_left <= 0 && game.clear_cooldown == 0 && game.screen == SCREEN_PLAYING && game.pending_exp_count == 0) {
        game.clear_cooldown = 90;  /* ~1.5 seconds at 60 FPS */
        audio_success(&audio);
        hw_set_led(LED_GREEN, 100);
    }

    /* Level clear cooldown — keep rendering particles/shake, then transition */
    if (game.clear_cooldown > 0) {
        game.clear_cooldown--;
        if (game.clear_cooldown <= 0) {
            game.screen = SCREEN_LEVEL_COMPLETE;
            game.score += game.lives * 50;  /* bonus */
            reset_effects();
            apply_paddle_width();
            hw_leds_off();
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Drawing
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_hud(void) {
    char buf[64];

    /* Level - positioned next to menu button */
    snprintf(buf, sizeof(buf), "LV %d", game.level);
    fb_draw_text(&fb, AREA_X + 90, SCREEN_SAFE_TOP + 22, buf, HUD_COLOR, 1);
    if (test_mode) {
        fb_draw_text(&fb, AREA_X + 90, SCREEN_SAFE_TOP + 12, "TEST", RGB(255, 255, 0), 1);
    }

    /* Score (centred between buttons) */
    snprintf(buf, sizeof(buf), "%d", game.score);
    int sw = (int)strlen(buf) * 12;
    fb_draw_text(&fb, AREA_X + AREA_W / 2 - sw / 2, SCREEN_SAFE_TOP + 20, buf, HUD_COLOR, 2);

    /* Hearts for lives - positioned before exit button, compact */
    for (int i = 0; i < game.lives && i < 9; i++) {
        int hx = AREA_X + AREA_W - 90 - i * 14;  /* smaller spacing */
        int hy = SCREEN_SAFE_TOP + 22;
        /* Smaller hearts */
        fb_fill_circle(&fb, hx, hy + 2, 4, HEART_COLOR);
        fb_fill_circle(&fb, hx + 5, hy + 2, 4, HEART_COLOR);
        fb_fill_rect(&fb, hx - 3, hy + 2, 11, 5, HEART_COLOR);
    }

    /* Active effect indicators — compact status at top of play area */
    int ey = AREA_Y + 5;
    if (game.fx.paddle_level != 0) {
        snprintf(buf, sizeof(buf), "PAD%+d", game.fx.paddle_level);
        uint32_t color = game.fx.paddle_level > 0 ? RGB(80, 220, 80) : RGB(255, 60, 60);
        fb_draw_text(&fb, AREA_X + 4, ey, buf, color, 1);
        ey += 12;
    }
    if (game.fx.speed_level != 0) {
        snprintf(buf, sizeof(buf), "SPD%+d", game.fx.speed_level);
        uint32_t color = game.fx.speed_level < 0 ? RGB(255, 255, 80) : RGB(0, 200, 255);
        fb_draw_text(&fb, AREA_X + 4, ey, buf, color, 1);
        ey += 12;
    }
    if (game.fx.fireball) {
        fb_draw_text(&fb, AREA_X + 4, ey, "FIRE", RGB(255, 140, 0), 1);
    }
}

static void draw_bricks(void) {
    for (int i = 0; i < game.brick_count; i++) {
        Brick *b = &game.bricks[i];
        if (b->health <= 0) continue;

        int bx = b->x;
        int by = b->y;

        /* Determine colors: use row-based rainbow for single-hit normal bricks */
        uint32_t ctop = b->color_top;
        uint32_t cbot = b->color_bot;
        if (b->type == BRICK_NORMAL && b->max_health == 1) {
            int ci = b->color_index % NUM_ROW_COLORS;
            ctop = ROW_COLORS[ci][0];
            cbot = ROW_COLORS[ci][1];
        }

        /* Gradient fill */
        fb_fill_rect_gradient(&fb, bx, by, b->w, b->h, ctop, cbot);

        /* Subtle highlight on top edge */
        fb_fill_rect_alpha(&fb, bx + 1, by, b->w - 2, 2,
                           COLOR_WHITE, 60);

        /* Bottom shadow */
        fb_fill_rect_alpha(&fb, bx + 1, by + b->h - 2, b->w - 2, 2,
                           COLOR_BLACK, 80);

        /* Special brick indicators */
        if (b->type == BRICK_INDESTRUCTIBLE) {
            /* Diagonal stripes pattern for indestructible */
            for (int s = 0; s < b->w + b->h; s += 6) {
                int x1 = bx + s;
                int y1 = by;
                int x2 = bx;
                int y2 = by + s;
                if (x1 < bx + b->w && y2 < by + b->h) {
                    fb_draw_line(&fb, x1, y1, x2, y2, RGB(120, 120, 120));
                }
            }
        } else if (b->type == BRICK_BONUS) {
            /* Star/sparkle effect for bonus bricks */
            int cx = bx + b->w / 2;
            int cy = by + b->h / 2;
            fb_draw_line(&fb, cx - 4, cy, cx + 4, cy, COLOR_WHITE);
            fb_draw_line(&fb, cx, cy - 4, cx, cy + 4, COLOR_WHITE);
        } else if (b->type == BRICK_EXPLOSIVE) {
            /* X pattern for explosive bricks */
            fb_draw_line(&fb, bx + 2, by + 2, bx + b->w - 2, by + b->h - 2, RGB(255, 255, 0));
            fb_draw_line(&fb, bx + b->w - 2, by + 2, bx + 2, by + b->h - 2, RGB(255, 255, 0));
        }

        /* Health number for multi-hit bricks */
        if (b->health > 1 && b->health < 100 && b->type == BRICK_NORMAL) {
            char num[8];
            snprintf(num, sizeof(num), "%d", b->health);
            int tw = (int)strlen(num) * 6;
            fb_draw_text(&fb, bx + b->w / 2 - tw / 2,
                         by + b->h / 2 - 3, num, COLOR_WHITE, 1);
        }
    }
}

static void draw_paddle(void) {
    int px = (int)game.paddle_x - game.paddle_w / 2;
    int py = PADDLE_Y;

    /* Glow under paddle */
    fb_fill_rect_alpha(&fb, px + 4, py + PADDLE_HEIGHT,
                       game.paddle_w - 8, 4, PADDLE_GLOW, 80);

    /* Main paddle body — gradient */
    fb_fill_rect_gradient(&fb, px, py, game.paddle_w, PADDLE_HEIGHT,
                          PADDLE_COLOR, PADDLE_GLOW);

    /* Highlight on top */
    fb_fill_rect_alpha(&fb, px + 2, py, game.paddle_w - 4, 2,
                       COLOR_WHITE, 100);
}

static void draw_balls(void) {
    for (int i = 0; i < game.ball_count; i++) {
        Ball *b = &game.balls[i];
        if (!b->active) continue;

        int bx = (int)b->x;
        int by = (int)b->y;
        uint32_t col = b->fireball ? BALL_FIRE : BALL_COLOR;

        /* Fiery trail for fireball balls (drawn BEFORE ball so ball is on top) */
        if (b->fireball && !b->stuck) {
            for (int t = 0; t < TRAIL_LEN; t++) {
                /* Read from oldest to newest in circular buffer.
                 * trail_idx points to next write = oldest entry.
                 * So oldest is at trail_idx, newest is at trail_idx - 1. */
                int idx = (b->trail_idx + t) % TRAIL_LEN;
                float age = (float)t / (float)(TRAIL_LEN - 1);  /* 0=oldest, 1=newest */

                /* Radius: oldest=1, newest=4, linearly interpolated */
                int radius = 1 + (int)(age * 3.0f);

                /* Color gradient: oldest=dark red, newest=bright yellow-orange */
                uint8_t r, g, bb_c;
                if (age < 0.5f) {
                    /* Dark red (180,30,0) -> orange (255,100,0) */
                    float f = age * 2.0f;
                    r = (uint8_t)(180 + f * (255 - 180));
                    g = (uint8_t)(30  + f * (100 - 30));
                    bb_c = 0;
                } else {
                    /* Orange (255,100,0) -> bright yellow-orange (255,200,50) */
                    float f = (age - 0.5f) * 2.0f;
                    r = 255;
                    g = (uint8_t)(100 + f * (200 - 100));
                    bb_c = (uint8_t)(0 + f * 50);
                }

                int tx = (int)b->trail_x[idx];
                int ty = (int)b->trail_y[idx];
                fb_fill_circle(&fb, tx, ty, radius, RGB(r, g, bb_c));
            }
        }

        /* Glow behind ball */
        fb_fill_circle(&fb, bx, by, BALL_RADIUS + 3,
                       b->fireball ? RGB(80, 40, 0) : RGB(40, 40, 60));

        /* Ball body */
        fb_fill_circle(&fb, bx, by, BALL_RADIUS, col);

        /* Specular highlight */
        fb_fill_circle(&fb, bx - 2, by - 2, 2, COLOR_WHITE);
    }
}

static void draw_powerups(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        PowerUp *pu = &game.powerups[i];
        if (!pu->active) continue;

        int px = (int)pu->x - POWERUP_SIZE / 2;
        int py = (int)pu->y - POWERUP_SIZE / 2;

        /* Box with border */
        fb_fill_rounded_rect(&fb, px, py, POWERUP_SIZE, POWERUP_SIZE, 4,
                             pu_info[pu->type].color);
        fb_draw_rounded_rect(&fb, px, py, POWERUP_SIZE, POWERUP_SIZE, 4,
                             COLOR_WHITE);

        /* Letter */
        char sym[2] = { pu_info[pu->type].symbol, 0 };
        fb_draw_text(&fb, px + POWERUP_SIZE / 2 - 3, py + POWERUP_SIZE / 2 - 4,
                     sym, COLOR_WHITE, 1);
    }
}

static void draw_play_area_border(void) {
    /* Subtle border around play area */
    fb_draw_rect(&fb, AREA_X - 1, AREA_Y - 1, AREA_W + 2, AREA_H + 2,
                 RGB(40, 50, 80));
}

static void draw_game_screen(void) {
    fb_clear(&fb, BG_COLOR);

    /* Screen shake offset — applied via framebuffer draw offset */
    fb_set_draw_offset(&fb, (int)game.shake_x, (int)game.shake_y);
    draw_play_area_border();
    draw_bricks();
    draw_paddle();
    draw_balls();
    draw_powerups();
    draw_particles();
    fb_clear_draw_offset(&fb);
    draw_hud();  /* HUD does NOT shake */

    button_draw_menu(&fb, &btn_menu);
    button_draw_exit(&fb, &btn_exit);

    /* Launch hint */
    if (!game.ball_launched) {
        text_draw_centered(&fb, AREA_X + AREA_W / 2, PADDLE_Y + 30,
                           "TAP TO LAUNCH", RGB(120, 120, 160), 2);
    }
}

/* ── Overlay screens ─────────────────────────────────────────────────── */

static void draw_welcome(void) {
    fb_clear(&fb, BG_COLOR);

    /* Title */
    text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 60,
                       "BRICK BREAKER", RGB(0, 220, 255), 4);

    /* Decorative line */
    fb_fill_rect_gradient(&fb, AREA_X + 80, SCREEN_SAFE_TOP + 100, AREA_W - 160, 3,
                          RGB(0, 220, 255), RGB(255, 80, 200));

    /* Instructions */
    text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 130,
                       "DRAG TO MOVE PADDLE", RGB(160, 160, 180), 2);
    text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 155,
                       "TAP TO LAUNCH BALL", RGB(160, 160, 180), 2);
    text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 180,
                       "CATCH POWER-UPS!", RGB(160, 160, 180), 2);

    /* High score */
    if (hs.count > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "HIGH SCORE: %d", hs.entries[0].score);
        text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 220,
                           buf, RGB(255, 255, 100), 2);
    }

    button_draw(&fb, &btn_start);
}

static void draw_paused(void) {
    draw_game_screen();  /* game as background */
    fb_fill_rect_alpha(&fb, 0, 0, fb.width, fb.height, COLOR_BLACK, 160);
    text_draw_centered(&fb, fb.width / 2, fb.height / 2 - 80,
                       "PAUSED", COLOR_CYAN, 4);
    button_draw(&fb, &btn_resume);
    button_draw(&fb, &btn_retire);
    button_draw(&fb, &btn_exit_pause);
    toggle_draw(&fb, &toggle_test);
}

static void draw_level_complete(void) {
    draw_game_screen();
    fb_fill_rect_alpha(&fb, 0, 0, fb.width, fb.height, COLOR_BLACK, 140);
    text_draw_centered(&fb, fb.width / 2, fb.height / 2 - 70,
                       "LEVEL COMPLETE!", RGB(0, 255, 100), 4);
    char buf[64];
    snprintf(buf, sizeof(buf), "SCORE: %d", game.score);
    text_draw_centered(&fb, fb.width / 2, fb.height / 2 - 20,
                       buf, COLOR_WHITE, 2);
    snprintf(buf, sizeof(buf), "BONUS: %d", game.lives * 50);
    text_draw_centered(&fb, fb.width / 2, fb.height / 2 + 10,
                       buf, COLOR_YELLOW, 2);
    button_draw(&fb, &btn_next_level);
}

static void draw_game_over(void) {
    fb_clear(&fb, BG_COLOR);
    fb_fill_rect_alpha(&fb, 0, 0, fb.width, fb.height, COLOR_BLACK, 100);

    text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 60,
                       "GAME OVER", COLOR_RED, 4);

    char buf[64];
    snprintf(buf, sizeof(buf), "FINAL SCORE: %d", game.score);
    text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 120,
                       buf, COLOR_WHITE, 3);
    snprintf(buf, sizeof(buf), "LEVEL REACHED: %d", game.level);
    text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 160,
                       buf, HUD_COLOR, 2);

    if (hs.count > 0 && game.score >= hs.entries[0].score) {
        text_draw_centered(&fb, fb.width / 2, SCREEN_SAFE_TOP + 195,
                           "NEW HIGH SCORE!", COLOR_YELLOW, 2);
    }

    /* Leaderboard */
    int ly = SCREEN_SAFE_TOP + 230;
    text_draw_centered(&fb, fb.width / 2, ly, "TOP SCORES", RGB(180, 180, 200), 2);
    ly += 22;
    for (int i = 0; i < hs.count && i < 5; i++) {
        snprintf(buf, sizeof(buf), "%d. %-10s %d", i + 1, hs.entries[i].name, hs.entries[i].score);
        text_draw_centered(&fb, fb.width / 2, ly, buf, RGB(140, 140, 160), 1);
        ly += 14;
    }

    button_draw(&fb, &btn_restart);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Input handling
 * ══════════════════════════════════════════════════════════════════════════ */

static void handle_input(void) {
    touch_poll(&touch);
    TouchState st = touch_get_state(&touch);
    uint32_t now = get_time_ms();

    switch (game.screen) {
    case SCREEN_WELCOME:
        if (st.pressed) {
            if (button_check_press(&btn_start, button_is_touched(&btn_start, st.x, st.y), now)) {
                reset_game();
                audio_beep(&audio);
            }
        }
        break;

    case SCREEN_PLAYING:
        /* Exit */
        if (st.pressed) {
            if (button_check_press(&btn_exit, button_is_touched(&btn_exit, st.x, st.y), now)) {
                running = false;
                return;
            }
            /* Menu → pause */
            if (button_check_press(&btn_menu, button_is_touched(&btn_menu, st.x, st.y), now)) {
                game.screen = SCREEN_PAUSED;
                return;
            }
        }

        /* Move paddle with touch */
        if (st.held || st.pressed) {
            game.paddle_x = clampf((float)st.x,
                                   AREA_X + game.paddle_w / 2.0f,
                                   AREA_X + AREA_W - game.paddle_w / 2.0f);
        }

        /* Launch on tap */
        if (st.pressed && !game.ball_launched) {
            launch_ball();
        }
        break;

    case SCREEN_PAUSED:
        /* Test mode toggle */
        if (toggle_check_press(&toggle_test, st.x, st.y, st.pressed, now)) {
            test_mode = toggle_test.state;
            audio_beep(&audio);
        }
        if (st.pressed) {
            if (button_check_press(&btn_resume, button_is_touched(&btn_resume, st.x, st.y), now)) {
                game.screen = SCREEN_PLAYING;
                audio_beep(&audio);
            }
            if (button_check_press(&btn_retire, button_is_touched(&btn_retire, st.x, st.y), now)) {
                /* Save score to high-score table */
                if (hs_qualifies(&hs, game.score) >= 0) {
                    hs_insert(&hs, "PLAYER", game.score);
                    hs_save(&hs);
                }
                game.screen = SCREEN_GAME_OVER;
                audio_tone(&audio, 600, 100);
            }
            if (button_check_press(&btn_exit_pause, button_is_touched(&btn_exit_pause, st.x, st.y), now)) {
                running = false;
            }
        }
        break;

    case SCREEN_LEVEL_COMPLETE:
        if (st.pressed) {
            if (button_check_press(&btn_next_level, button_is_touched(&btn_next_level, st.x, st.y), now)) {
                if (game.level < MAX_LEVELS) {
                    game.level++;
                    init_level();
                    game.screen = SCREEN_PLAYING;
                } else {
                    /* Beat all levels! */
                    game.screen = SCREEN_GAME_OVER;
                    if (hs_qualifies(&hs, game.score) >= 0) {
                        hs_insert(&hs, "PLAYER", game.score);
                        hs_save(&hs);
                    }
                }
                audio_beep(&audio);
            }
        }
        break;

    case SCREEN_GAME_OVER:
        if (st.pressed) {
            if (button_check_press(&btn_restart, button_is_touched(&btn_restart, st.x, st.y), now)) {
                reset_game();
                audio_beep(&audio);
            }
        }
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    const char *fb_device = "/dev/fb0";
    const char *touch_device = "/dev/input/touchscreen0";

    /* Check for --test flag */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) {
            test_mode = true;
            printf("TEST MODE ENABLED\n");
        }
    }

    if (argc > 1) fb_device = argv[1];
    if (argc > 2) touch_device = argv[2];

    // Singleton guard — prevent duplicate instances
    int lock_fd = acquire_instance_lock("brick_breaker");
    if (lock_fd < 0) {
        fprintf(stderr, "brick_breaker: another instance is already running\n");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (fb_init(&fb, fb_device) < 0) {
        fprintf(stderr, "Failed to initialise framebuffer\n");
        return 1;
    }
    if (touch_init(&touch, touch_device) < 0) {
        fprintf(stderr, "Failed to initialise touch input\n");
        fb_close(&fb);
        return 1;
    }

    hw_init();
    hw_set_backlight(100);
    audio_init(&audio);
    srand(time(NULL));

    /* Compute brick grid dimensions based on screen orientation */
    init_brick_layout();

    /* High score */
    hs_init(&hs, "brick_breaker");
    hs_load(&hs);

    /* UI buttons */
    button_init(&btn_menu, LAYOUT_MENU_BTN_X, LAYOUT_MENU_BTN_Y,
                BTN_MENU_WIDTH, BTN_MENU_HEIGHT, "",
                BTN_MENU_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&btn_exit, LAYOUT_EXIT_BTN_X, LAYOUT_EXIT_BTN_Y,
                BTN_EXIT_WIDTH, BTN_EXIT_HEIGHT, "",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&btn_start, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                SCREEN_SAFE_BOTTOM - BTN_LARGE_HEIGHT - 30,
                BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "START",
                BTN_START_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&btn_restart, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                SCREEN_SAFE_BOTTOM - BTN_LARGE_HEIGHT - 30,
                BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "PLAY AGAIN",
                BTN_RESTART_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&btn_resume, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                fb.height / 2 - 10, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "RESUME",
                BTN_RESUME_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&btn_retire, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                fb.height / 2 + 60, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "RETIRE",
                RGB(200, 170, 40), COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&btn_exit_pause, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                fb.height / 2 + 130, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "EXIT",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&btn_next_level, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                fb.height / 2 + 60, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "NEXT LEVEL",
                BTN_START_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);

    /* Test mode toggle switch — positioned at bottom-left of pause overlay */
    toggle_init(&toggle_test, SCREEN_SAFE_LEFT + 20, SCREEN_SAFE_BOTTOM - 40,
                50, 26, "TEST MODE", test_mode);

    game.screen = SCREEN_WELCOME;
    frame_time_ms = get_time_ms();

    printf("Brick Breaker started!\n");

    /* ── main loop @ 60 FPS ──────────────────────────────────────────── */
    while (running) {
        frame_time_ms = get_time_ms();

        handle_input();
        update_game();

        switch (game.screen) {
        case SCREEN_WELCOME:        draw_welcome();        break;
        case SCREEN_PLAYING:        draw_game_screen();    break;
        case SCREEN_PAUSED:         draw_paused();         break;
        case SCREEN_LEVEL_COMPLETE: draw_level_complete(); break;
        case SCREEN_GAME_OVER:      draw_game_over();      break;
        }

        fb_swap(&fb);
        usleep(16667);  /* ~60 FPS */
    }

    /* Clean up */
    touch_close(&touch);
    hw_leds_off();
    hw_set_backlight(100);
    audio_close(&audio);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    fb_close(&fb);

    printf("Brick Breaker ended.\n");
    return 0;
}
