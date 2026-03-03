/*
 * Brick Breaker — Native C Implementation for RoomWizard
 *
 * Features:
 *   - Touch-controlled paddle (drag to move, tap to launch)
 *   - 10 levels with varied brick patterns and increasing difficulty
 *   - 6 power-ups: Wide, Narrow, Multi-Ball, Slow, Fireball, Extra Life
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
#define AREA_Y      (SCREEN_SAFE_TOP + 50)      /* leave room for HUD */
#define AREA_W      SCREEN_SAFE_WIDTH
#define AREA_H      (SCREEN_SAFE_HEIGHT - 50)

/* Paddle */
#define PADDLE_Y        (AREA_Y + AREA_H - 28)
#define PADDLE_HEIGHT   14
#define PADDLE_BASE_W   100
#define PADDLE_MIN_W    50
#define PADDLE_MAX_W    180

/* Ball */
#define BALL_RADIUS     6
#define MAX_BALLS       8
#define BALL_BASE_SPEED 4.0f
#define BALL_MAX_SPEED  9.0f
#define BALL_SPEED_INC  0.15f

/* Bricks */
#define BRICK_COLS      10
#define BRICK_ROWS      6
#define BRICK_PAD       3
#define BRICK_H         16
#define MAX_BRICKS      (BRICK_COLS * BRICK_ROWS)

/* Power-ups */
#define MAX_POWERUPS    8
#define POWERUP_SIZE    18
#define POWERUP_SPEED   2
#define POWERUP_CHANCE  30      /* percent */
#define POWERUP_DUR_MS  12000   /* timed power-up duration */

/* Lives and levels */
#define STARTING_LIVES  5
#define MAX_LEVELS      10

/* Particles */
#define MAX_PARTICLES   40

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
    PU_NONE = 0,
    PU_WIDE,        /* wider paddle */
    PU_NARROW,      /* narrower paddle (penalty) */
    PU_MULTIBALL,   /* spawn 2 extra balls */
    PU_SLOW,        /* slow all balls down */
    PU_FIREBALL,    /* ball smashes through bricks without bouncing */
    PU_LIFE         /* +1 life */
} PowerUpType;

typedef struct {
    float x, y;
    float dx, dy;
    float speed;
    bool  active;
    bool  stuck;     /* stuck on paddle until launched */
    bool  fireball;
} Ball;

typedef struct {
    int  x, y, w, h;
    int  health;     /* 0 = destroyed */
    int  max_health;
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
    /* Active power-up timers (0 = inactive) */
    uint32_t wide_end;
    uint32_t narrow_end;
    uint32_t slow_end;
    uint32_t fireball_end;
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

/* UI buttons */
static Button btn_menu, btn_exit;
static Button btn_start, btn_restart;
static Button btn_resume, btn_exit_pause;
static Button btn_next_level;

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
    { RGB(255, 255, 80),  RGB(180, 160, 30) },   /* health 2 — yellow */
    { RGB(255, 160, 50),  RGB(180, 90, 20)  },   /* health 3 — orange */
    { RGB(255, 60, 60),   RGB(160, 30, 30)  },   /* health 4 — red */
};

/* Power-up visual config (color, letter) */
static const struct { uint32_t color; char symbol; const char *name; } pu_info[] = {
    [PU_WIDE]     = { RGB(0, 255, 120),  'W', "WIDE"     },
    [PU_NARROW]   = { RGB(255, 50, 50),  'N', "NARROW"   },
    [PU_MULTIBALL]= { RGB(0, 150, 255),  'M', "MULTI"    },
    [PU_SLOW]     = { RGB(255, 255, 60),  'S', "SLOW"    },
    [PU_FIREBALL] = { RGB(255, 140, 0),  'F', "FIRE"     },
    [PU_LIFE]     = { RGB(255, 80, 200), '+', "LIFE"     },
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
    int         health[BRICK_ROWS];  /* per-row starting health */
    BrickPattern pattern;
    float       speed_mult;          /* ball speed multiplier */
} LevelDef;

static const LevelDef levels[MAX_LEVELS] = {
    { 3, {1,1,1,0,0,0},       PAT_STANDARD,     1.0f },
    { 4, {1,1,2,1,0,0},       PAT_STANDARD,     1.0f },
    { 4, {2,1,2,1,0,0},       PAT_CHECKERBOARD, 1.05f },
    { 5, {2,2,1,2,1,0},       PAT_PYRAMID,      1.1f },
    { 5, {2,2,2,2,1,0},       PAT_DIAMOND,      1.15f },
    { 5, {3,2,2,2,1,0},       PAT_BORDER,       1.2f },
    { 6, {3,2,2,2,2,1},       PAT_STRIPES,      1.25f },
    { 6, {3,3,2,2,2,1},       PAT_CHECKERBOARD, 1.3f },
    { 6, {3,3,3,3,2,1},       PAT_PYRAMID,      1.35f },
    { 6, {4,4,3,3,2,1},       PAT_DIAMOND,      1.4f },
};

/* ══════════════════════════════════════════════════════════════════════════
 *  Helpers
 * ══════════════════════════════════════════════════════════════════════════ */

static void signal_handler(int sig) { (void)sig; running = false; }

static float randf(void) { return (float)rand() / (float)RAND_MAX; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ── Particles ───────────────────────────────────────────────────────── */

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
        uint8_t alpha = (uint8_t)(255 * p->life / 22);
        fb_draw_pixel_alpha(&fb, (int)p->x, (int)p->y, p->color, alpha);
        fb_draw_pixel_alpha(&fb, (int)p->x + 1, (int)p->y, p->color, alpha);
        fb_draw_pixel_alpha(&fb, (int)p->x, (int)p->y + 1, p->color, alpha);
        fb_draw_pixel_alpha(&fb, (int)p->x + 1, (int)p->y + 1, p->color, alpha);
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
    int brick_w = (AREA_W - BRICK_PAD * (BRICK_COLS + 1)) / BRICK_COLS;
    game.brick_count = 0;
    game.bricks_left = 0;

    for (int r = 0; r < lv->rows && r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!pattern_has_brick(lv->pattern, r, c, lv->rows, BRICK_COLS))
                continue;

            Brick *b = &game.bricks[game.brick_count];
            b->x = AREA_X + BRICK_PAD + c * (brick_w + BRICK_PAD);
            b->y = AREA_Y + BRICK_PAD + r * (BRICK_H + BRICK_PAD);
            b->w = brick_w;
            b->h = BRICK_H;
            b->health = lv->health[r];
            b->max_health = b->health;
            int ci = clampi(b->health - 1, 0, 3);
            b->color_top = brick_colors[ci][0];
            b->color_bot = brick_colors[ci][1];
            game.brick_count++;
            game.bricks_left++;
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
    b->fireball = (game.fx.fireball_end > frame_time_ms);
    return b;
}

static void reset_ball_on_paddle(void) {
    game.ball_count = 0;
    Ball *b = add_ball(game.paddle_x, PADDLE_Y - BALL_RADIUS - 1, 0, 0, 0);
    if (b) {
        const LevelDef *lv = &levels[clampi(game.level - 1, 0, MAX_LEVELS - 1)];
        b->speed = BALL_BASE_SPEED * lv->speed_mult;
        b->stuck = true;
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
    if ((rand() % 100) >= POWERUP_CHANCE) return;

    /* Pick random type (1..6) */
    PowerUpType type = (PowerUpType)(1 + rand() % 6);

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
    case PU_WIDE:
        game.paddle_w = PADDLE_MAX_W;
        game.fx.wide_end = frame_time_ms + POWERUP_DUR_MS;
        game.fx.narrow_end = 0;  /* cancel any narrow */
        break;
    case PU_NARROW:
        game.paddle_w = PADDLE_MIN_W;
        game.fx.narrow_end = frame_time_ms + POWERUP_DUR_MS;
        game.fx.wide_end = 0;
        break;
    case PU_MULTIBALL: {
        /* Clone each active non-stuck ball into 2 new ones */
        int n = game.ball_count;
        for (int i = 0; i < n && game.ball_count < MAX_BALLS - 1; i++) {
            Ball *src = &game.balls[i];
            if (!src->active || src->stuck) continue;
            float angle_off = 0.4f;
            float cs = cosf(angle_off), sn = sinf(angle_off);
            add_ball(src->x, src->y,
                     src->dx * cs - src->dy * sn,
                     src->dx * sn + src->dy * cs,
                     src->speed);
            add_ball(src->x, src->y,
                     src->dx * cs + src->dy * sn,
                    -src->dx * sn + src->dy * cs,
                     src->speed);
        }
        audio_blip(&audio);
        break;
    }
    case PU_SLOW:
        game.fx.slow_end = frame_time_ms + POWERUP_DUR_MS;
        for (int i = 0; i < game.ball_count; i++) {
            Ball *b = &game.balls[i];
            if (!b->active) continue;
            b->speed *= 0.55f;
            float mag = sqrtf(b->dx * b->dx + b->dy * b->dy);
            if (mag > 0.01f) {
                b->dx = (b->dx / mag) * b->speed;
                b->dy = (b->dy / mag) * b->speed;
            }
        }
        break;
    case PU_FIREBALL:
        game.fx.fireball_end = frame_time_ms + POWERUP_DUR_MS;
        for (int i = 0; i < game.ball_count; i++)
            game.balls[i].fireball = true;
        break;
    case PU_LIFE:
        game.lives++;
        audio_blip(&audio);
        break;
    default:
        break;
    }

    /* Brief LED + beep */
    if (type != PU_MULTIBALL && type != PU_LIFE)
        audio_beep(&audio);

    usleep(50000);
    hw_leds_off();
}

static void expire_effects(void) {
    if (game.fx.wide_end && frame_time_ms >= game.fx.wide_end) {
        game.paddle_w = PADDLE_BASE_W;
        game.fx.wide_end = 0;
    }
    if (game.fx.narrow_end && frame_time_ms >= game.fx.narrow_end) {
        game.paddle_w = PADDLE_BASE_W;
        game.fx.narrow_end = 0;
    }
    if (game.fx.slow_end && frame_time_ms >= game.fx.slow_end) {
        const LevelDef *lv = &levels[clampi(game.level - 1, 0, MAX_LEVELS - 1)];
        for (int i = 0; i < game.ball_count; i++) {
            Ball *b = &game.balls[i];
            if (!b->active) continue;
            b->speed = BALL_BASE_SPEED * lv->speed_mult;
            float mag = sqrtf(b->dx * b->dx + b->dy * b->dy);
            if (mag > 0.01f) {
                b->dx = (b->dx / mag) * b->speed;
                b->dy = (b->dy / mag) * b->speed;
            }
        }
        game.fx.slow_end = 0;
    }
    if (game.fx.fireball_end && frame_time_ms >= game.fx.fireball_end) {
        for (int i = 0; i < game.ball_count; i++)
            game.balls[i].fireball = false;
        game.fx.fireball_end = 0;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Game state management
 * ══════════════════════════════════════════════════════════════════════════ */

static void init_level(void) {
    /* Reset power-ups and effects */
    memset(game.powerups, 0, sizeof(game.powerups));
    memset(game.particles, 0, sizeof(game.particles));
    memset(&game.fx, 0, sizeof(game.fx));
    game.paddle_w = PADDLE_BASE_W;
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
            continue;
        }

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

            /* Hit! */
            if (b->fireball) {
                br->health = 0;
            } else {
                br->health--;
                /* Update colours for new health */
                int ci = clampi(br->health - 1, 0, 3);
                br->color_top = brick_colors[ci][0];
                br->color_bot = brick_colors[ci][1];
            }

            if (br->health <= 0) {
                game.bricks_left--;
                game.score += 10 * game.level;
                spawn_particles((float)(br->x + br->w / 2),
                                (float)(br->y + br->h / 2),
                                br->color_top, 6);
                spawn_powerup((float)(br->x + br->w / 2),
                              (float)(br->y + br->h / 2));
                audio_interrupt(&audio);
                audio_tone(&audio, 1200, 25);
            } else {
                audio_interrupt(&audio);
                audio_tone(&audio, 800, 20);
            }

            /* Bounce (skip if fireball) */
            if (!b->fireball) {
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
    expire_effects();
    update_balls();
    update_powerups();
    update_particles();

    /* All balls lost? */
    if (game.ball_count == 0 && game.ball_launched) {
        game.lives--;
        hw_set_led(LED_RED, 100);
        audio_fail(&audio);
        usleep(300000);
        hw_leds_off();

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

    /* Level clear? */
    if (game.bricks_left <= 0) {
        game.screen = SCREEN_LEVEL_COMPLETE;
        game.score += game.lives * 50;  /* bonus */
        hw_set_led(LED_GREEN, 100);
        audio_success(&audio);
        usleep(200000);
        hw_leds_off();
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Drawing
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_hud(void) {
    char buf[64];

    /* Level */
    snprintf(buf, sizeof(buf), "LV %d", game.level);
    fb_draw_text(&fb, AREA_X + 4, SCREEN_SAFE_TOP + 8, buf, HUD_COLOR, 2);

    /* Score (centred) */
    snprintf(buf, sizeof(buf), "%d", game.score);
    int sw = (int)strlen(buf) * 12;
    fb_draw_text(&fb, AREA_X + AREA_W / 2 - sw / 2, SCREEN_SAFE_TOP + 8, buf, HUD_COLOR, 2);

    /* Hearts for lives */
    for (int i = 0; i < game.lives && i < 9; i++) {
        int hx = AREA_X + AREA_W - 20 - i * 20;
        int hy = SCREEN_SAFE_TOP + 10;
        fb_fill_circle(&fb, hx, hy + 3, 5, HEART_COLOR);
        fb_fill_circle(&fb, hx + 7, hy + 3, 5, HEART_COLOR);
        fb_fill_rect(&fb, hx - 4, hy + 3, 15, 7, HEART_COLOR);
    }

    /* Active effect indicators */
    int ey = SCREEN_SAFE_TOP + 30;
    if (game.fx.wide_end > frame_time_ms) {
        int secs = (game.fx.wide_end - frame_time_ms) / 1000 + 1;
        snprintf(buf, sizeof(buf), "WIDE %ds", secs);
        fb_draw_text(&fb, AREA_X + 4, ey, buf, pu_info[PU_WIDE].color, 1);
        ey += 12;
    }
    if (game.fx.narrow_end > frame_time_ms) {
        int secs = (game.fx.narrow_end - frame_time_ms) / 1000 + 1;
        snprintf(buf, sizeof(buf), "NARROW %ds", secs);
        fb_draw_text(&fb, AREA_X + 4, ey, buf, pu_info[PU_NARROW].color, 1);
        ey += 12;
    }
    if (game.fx.slow_end > frame_time_ms) {
        int secs = (game.fx.slow_end - frame_time_ms) / 1000 + 1;
        snprintf(buf, sizeof(buf), "SLOW %ds", secs);
        fb_draw_text(&fb, AREA_X + 4, ey, buf, pu_info[PU_SLOW].color, 1);
        ey += 12;
    }
    if (game.fx.fireball_end > frame_time_ms) {
        int secs = (game.fx.fireball_end - frame_time_ms) / 1000 + 1;
        snprintf(buf, sizeof(buf), "FIRE %ds", secs);
        fb_draw_text(&fb, AREA_X + 4, ey, buf, pu_info[PU_FIREBALL].color, 1);
    }
}

static void draw_bricks(void) {
    for (int i = 0; i < game.brick_count; i++) {
        Brick *b = &game.bricks[i];
        if (b->health <= 0) continue;

        /* Gradient fill */
        fb_fill_rect_gradient(&fb, b->x, b->y, b->w, b->h,
                              b->color_top, b->color_bot);

        /* Subtle highlight on top edge */
        fb_fill_rect_alpha(&fb, b->x + 1, b->y, b->w - 2, 2,
                           COLOR_WHITE, 60);

        /* Bottom shadow */
        fb_fill_rect_alpha(&fb, b->x + 1, b->y + b->h - 2, b->w - 2, 2,
                           COLOR_BLACK, 80);

        /* Health number for multi-hit bricks */
        if (b->health > 1 && b->health < 100) {
            char num[8];
            snprintf(num, sizeof(num), "%d", b->health);
            int tw = (int)strlen(num) * 6;
            fb_draw_text(&fb, b->x + b->w / 2 - tw / 2,
                         b->y + b->h / 2 - 3, num, COLOR_WHITE, 1);
        }
    }
}

static void draw_paddle(void) {
    int px = (int)game.paddle_x - game.paddle_w / 2;

    /* Glow under paddle */
    fb_fill_rect_alpha(&fb, px + 4, PADDLE_Y + PADDLE_HEIGHT,
                       game.paddle_w - 8, 4, PADDLE_GLOW, 80);

    /* Main paddle body — gradient */
    fb_fill_rect_gradient(&fb, px, PADDLE_Y, game.paddle_w, PADDLE_HEIGHT,
                          PADDLE_COLOR, PADDLE_GLOW);

    /* Highlight on top */
    fb_fill_rect_alpha(&fb, px + 2, PADDLE_Y, game.paddle_w - 4, 2,
                       COLOR_WHITE, 100);
}

static void draw_balls(void) {
    for (int i = 0; i < game.ball_count; i++) {
        Ball *b = &game.balls[i];
        if (!b->active) continue;

        uint32_t col = b->fireball ? BALL_FIRE : BALL_COLOR;

        /* Glow behind ball */
        fb_fill_circle(&fb, (int)b->x, (int)b->y, BALL_RADIUS + 3,
                       b->fireball ? RGB(80, 40, 0) : RGB(40, 40, 60));

        /* Ball body */
        fb_fill_circle(&fb, (int)b->x, (int)b->y, BALL_RADIUS, col);

        /* Specular highlight */
        fb_fill_circle(&fb, (int)b->x - 2, (int)b->y - 2, 2, COLOR_WHITE);
    }
}

static void draw_powerups(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        PowerUp *pu = &game.powerups[i];
        if (!pu->active || pu->type == PU_NONE) continue;

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

    draw_play_area_border();
    draw_bricks();
    draw_paddle();
    draw_balls();
    draw_powerups();
    draw_particles();
    draw_hud();

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
    text_draw_centered(&fb, fb.width / 2, fb.height / 2 - 60,
                       "PAUSED", COLOR_CYAN, 4);
    button_draw(&fb, &btn_resume);
    button_draw(&fb, &btn_exit_pause);
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
        if (st.pressed) {
            if (button_check_press(&btn_resume, button_is_touched(&btn_resume, st.x, st.y), now)) {
                game.screen = SCREEN_PLAYING;
                audio_beep(&audio);
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
    audio_init(&audio);
    srand(time(NULL));

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
                fb.height / 2, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "RESUME",
                BTN_RESUME_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&btn_exit_pause, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                fb.height / 2 + 80, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "EXIT",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    button_init(&btn_next_level, fb.width / 2 - BTN_LARGE_WIDTH / 2,
                fb.height / 2 + 60, BTN_LARGE_WIDTH, BTN_LARGE_HEIGHT, "NEXT LEVEL",
                BTN_START_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);

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
    audio_close(&audio);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    fb_close(&fb);

    printf("Brick Breaker ended.\n");
    return 0;
}
