/*
 * App Launcher for RoomWizard
 *
 * Visual grid launcher that scans /opt/roomwizard/apps/*.app manifest files
 * and displays them as icon tiles.  Acts as the system shell — respawned
 * by the init script when an app exits.
 *
 * Manifest format (key=value text, one per line):
 *   name=Snake
 *   exec=/opt/games/snake
 *   icon=/opt/roomwizard/icons/snake.ppm   (optional — PPM P6 format)
 *   args=fb,touch                           (optional — default: fb,touch)
 *
 * Recognized args values:
 *   fb,touch  — pass framebuffer and touch device paths (default)
 *   fb        — pass framebuffer device path only
 *   touch     — pass touch device path only
 *   none      — launch with no arguments
 *
 * Grid: dynamic layout — 3×2 in landscape, 2×3 in portrait, with pagination.
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include "common/common.h"
#include "common/ppm.h"
#include "common/logger.h"
#include "common/gamepad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

/* ── Limits ─────────────────────────────────────────────────────────────── */

#define MAX_APPS        24
#define APPS_DIR        "/opt/roomwizard/apps"
#define RESCAN_INTERVAL_MS 5000

/* Post-launch cooldown: ignore ALL input (gamepad, touch, mouse) for this
 * many milliseconds after returning from a child process.  This prevents
 * auto-relaunch caused by stale/repeat key events, resistive touch noise,
 * or race conditions in the drain loop.  500 ms is long enough for any
 * held key to stop generating repeats, but short enough to feel instant. */
#define LAUNCH_COOLDOWN_MS 500

/* ── Grid geometry ──────────────────────────────────────────────────────── */

#define TILE_GAP_X      20
#define TILE_GAP_Y      20
#define ICON_SIZE       96
#define LABEL_SCALE     2

#define TITLE_H         50
#define DOTS_H          30

#define MAX_TILE_W      200
#define MAX_TILE_H      150

/* Dynamic grid variables — set by compute_grid_layout() */
static int grid_cols, grid_rows, apps_per_page;
static int tile_w, tile_h;
static int grid_content_w, grid_content_h;
static int grid_left, grid_top;

static void compute_grid_layout(Framebuffer *fb) {
    if (fb->portrait_mode) {
        grid_cols = 2;
        grid_rows = 3;
    } else {
        grid_cols = 3;
        grid_rows = 2;
    }
    apps_per_page = grid_cols * grid_rows;

    /* Calculate tile sizes to fit available space, capped at max */
    int max_w = (SCREEN_SAFE_WIDTH  - (grid_cols - 1) * TILE_GAP_X) / grid_cols;
    int avail_h = SCREEN_SAFE_HEIGHT - TITLE_H - DOTS_H;
    int max_h = (avail_h - (grid_rows - 1) * TILE_GAP_Y) / grid_rows;

    tile_w = (max_w < MAX_TILE_W) ? max_w : MAX_TILE_W;
    tile_h = (max_h < MAX_TILE_H) ? max_h : MAX_TILE_H;

    /* Derived: centre the grid within the safe area */
    grid_content_w = grid_cols * tile_w + (grid_cols - 1) * TILE_GAP_X;
    grid_content_h = grid_rows * tile_h + (grid_rows - 1) * TILE_GAP_Y;

    grid_left = SCREEN_SAFE_LEFT + (SCREEN_SAFE_WIDTH - grid_content_w) / 2;
    grid_top  = SCREEN_SAFE_TOP  + TITLE_H +
                (SCREEN_SAFE_HEIGHT - TITLE_H - DOTS_H - grid_content_h) / 2;
}

/* ── Colours ────────────────────────────────────────────────────────────── */

#define BG_COLOR        RGB(25, 25, 35)
#define TILE_BG         RGB(45, 45, 60)
#define TILE_BORDER     RGB(70, 70, 90)
#define TILE_HL_BG      RGB(65, 65, 85)
#define TILE_SEL_BORDER RGB(0, 220, 255)
#define TITLE_COLOR     RGB(200, 200, 220)
#define LABEL_COLOR     RGB(220, 220, 220)
#define DOT_ACTIVE      RGB(200, 200, 220)
#define DOT_INACTIVE    RGB(80, 80, 100)

static const uint32_t ICON_COLORS[] = {
    0xFF2980B9,  /* Blue        */
    0xFF27AE60,  /* Green       */
    0xFFC0392B,  /* Red         */
    0xFF8E44AD,  /* Purple      */
    0xFFF39C12,  /* Orange      */
    0xFF16A085,  /* Teal        */
    0xFFD35400,  /* Dark Orange */
    0xFF34495E,  /* Dark Slate  */
};
#define NUM_ICON_COLORS (sizeof(ICON_COLORS) / sizeof(ICON_COLORS[0]))

/* ── Argument-passing modes ─────────────────────────────────────────────── */

typedef enum {
    ARG_NONE     = 0,
    ARG_FB       = 1,
    ARG_TOUCH    = 2,
    ARG_FB_TOUCH = 3
} ArgMode;

/* ── App entry ──────────────────────────────────────────────────────────── */

typedef struct {
    char      name[64];
    char      exec_path[256];
    char      icon_path[256];
    ArgMode   args;
    uint32_t *icon_pixels;      /* Loaded & scaled to ICON_SIZE², or NULL */
    uint32_t  icon_color;       /* Auto-assigned colour for letter tile   */
} AppEntry;

/* ── Launcher state ─────────────────────────────────────────────────────── */

typedef struct {
    AppEntry    apps[MAX_APPS];
    int         app_count;
    int         current_page;
    int         total_pages;
    int         selected_app;       /* Absolute app index, or -1 for none */
    Framebuffer fb;
    TouchInput  touch;
    GamepadManager gamepad;
    InputState  input;
    uint32_t    last_rescan_ms;
    uint32_t    last_launch_return_ms;  /* Timestamp of last child-exit for cooldown */
    Logger      logger;
} Launcher;

/* ════════════════════════════════════════════════════════════════════════ */
/*  Manifest parsing                                                       */
/* ════════════════════════════════════════════════════════════════════════ */

static void trim_trailing(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                       s[len - 1] == ' '  || s[len - 1] == '\t'))
        s[--len] = '\0';
}

static ArgMode parse_args(const char *s) {
    if (!s || !*s)                return ARG_FB_TOUCH;
    if (strcmp(s, "none") == 0)   return ARG_NONE;
    ArgMode m = ARG_NONE;
    if (strstr(s, "fb"))    m |= ARG_FB;
    if (strstr(s, "touch")) m |= ARG_TOUCH;
    return m ? m : ARG_FB_TOUCH;
}

static uint32_t name_to_color(const char *name) {
    unsigned h = 0;
    for (const char *p = name; *p; p++)
        h = h * 31 + (unsigned char)*p;
    return ICON_COLORS[h % NUM_ICON_COLORS];
}

static int load_manifest(const char *path, AppEntry *app) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    memset(app, 0, sizeof(*app));
    app->args = ARG_FB_TOUCH;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        trim_trailing(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "name") == 0)
            strncpy(app->name, val, sizeof(app->name) - 1);
        else if (strcmp(key, "exec") == 0)
            strncpy(app->exec_path, val, sizeof(app->exec_path) - 1);
        else if (strcmp(key, "icon") == 0)
            strncpy(app->icon_path, val, sizeof(app->icon_path) - 1);
        else if (strcmp(key, "args") == 0)
            app->args = parse_args(val);
    }
    fclose(f);

    /* Must have both name and exec */
    if (!app->name[0] || !app->exec_path[0]) return -1;

    /* Verify executable exists and is runnable */
    struct stat st;
    if (stat(app->exec_path, &st) != 0 || !(st.st_mode & S_IXUSR)) return -1;

    app->icon_color = name_to_color(app->name);
    return 0;
}

static void load_icon(AppEntry *app) {
    if (!app->icon_path[0]) return;

    int w, h;
    uint32_t *raw = ppm_load(app->icon_path, &w, &h);
    if (!raw) return;

    if (w == ICON_SIZE && h == ICON_SIZE) {
        app->icon_pixels = raw;
    } else {
        app->icon_pixels = ppm_scale(raw, w, h, ICON_SIZE, ICON_SIZE);
        free(raw);
    }
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  App scanning                                                           */
/* ════════════════════════════════════════════════════════════════════════ */

static void free_icons(Launcher *l) {
    for (int i = 0; i < l->app_count; i++) {
        free(l->apps[i].icon_pixels);
        l->apps[i].icon_pixels = NULL;
    }
}

static int scan_apps(Launcher *l) {
    free_icons(l);
    l->app_count = 0;

    DIR *dir = opendir(APPS_DIR);
    if (!dir) {
        LOG_WARN(&l->logger, "Cannot open %s", APPS_DIR);
        l->total_pages = 1;
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && l->app_count < MAX_APPS) {
        const char *name = ent->d_name;
        int len = (int)strlen(name);
        if (len < 5 || strcmp(name + len - 4, ".app") != 0) continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", APPS_DIR, name);

        AppEntry app;
        if (load_manifest(path, &app) == 0) {
            load_icon(&app);
            l->apps[l->app_count++] = app;
            LOG_INFO(&l->logger, "Loaded: %-16s -> %s", app.name, app.exec_path);
        } else {
            LOG_WARN(&l->logger, "Skipped: %s", path);
        }
    }
    closedir(dir);

    /* Sort alphabetically by display name */
    for (int i = 0; i < l->app_count - 1; i++)
        for (int j = i + 1; j < l->app_count; j++)
            if (strcasecmp(l->apps[i].name, l->apps[j].name) > 0) {
                AppEntry tmp = l->apps[i];
                l->apps[i]  = l->apps[j];
                l->apps[j]  = tmp;
            }

    l->total_pages = (l->app_count + apps_per_page - 1) / apps_per_page;
    if (l->total_pages < 1) l->total_pages = 1;
    if (l->current_page >= l->total_pages) l->current_page = l->total_pages - 1;

    return l->app_count;
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Drawing                                                                */
/* ════════════════════════════════════════════════════════════════════════ */

static void tile_xy(int idx_on_page, int *x, int *y) {
    int col = idx_on_page % grid_cols;
    int row = idx_on_page / grid_cols;
    *x = grid_left + col * (tile_w + TILE_GAP_X);
    *y = grid_top  + row * (tile_h + TILE_GAP_Y);
}

static void draw_letter_icon(Framebuffer *fb, int x, int y,
                             char letter, uint32_t color) {
    fb_fill_rect(fb, x, y, ICON_SIZE, ICON_SIZE, color);
    fb_draw_rect(fb, x, y, ICON_SIZE, ICON_SIZE, COLOR_WHITE);

    /* Centre a single character (bitmap font: 8px base) */
    int scale   = 7;
    int char_w  = 8 * scale;
    int char_h  = 8 * scale;
    int lx = x + (ICON_SIZE - char_w) / 2;
    int ly = y + (ICON_SIZE - char_h) / 2;
    char s[2] = { letter, '\0' };
    fb_draw_text(fb, lx, ly, s, COLOR_WHITE, scale);
}

static void draw_ppm_icon(Framebuffer *fb, int x, int y,
                          const uint32_t *pixels) {
    for (int py = 0; py < ICON_SIZE; py++)
        for (int px = 0; px < ICON_SIZE; px++)
            fb_draw_pixel(fb, x + px, y + py,
                          pixels[py * ICON_SIZE + px]);
}

#define TILE_RADIUS 12

static void draw_tile(Framebuffer *fb, AppEntry *app,
                      int tx, int ty, int highlight) {
    /* Tile background (rounded) */
    fb_fill_rounded_rect(fb, tx, ty, tile_w, tile_h, TILE_RADIUS,
                         highlight ? TILE_HL_BG : TILE_BG);
    fb_draw_rounded_rect(fb, tx, ty, tile_w, tile_h, TILE_RADIUS, TILE_BORDER);

    /* Icon — centred horizontally, 8 px from top of tile */
    int icon_x = tx + (tile_w - ICON_SIZE) / 2;
    int icon_y = ty + 8;

    if (app->icon_pixels) {
        draw_ppm_icon(fb, icon_x, icon_y, app->icon_pixels);
    } else {
        char letter = app->name[0];
        if (letter >= 'a' && letter <= 'z') letter -= 32;
        draw_letter_icon(fb, icon_x, icon_y, letter, app->icon_color);
    }

    /* Label — centred below icon */
    int label_y = icon_y + ICON_SIZE + 10;
    text_draw_centered(fb, tx + tile_w / 2, label_y,
                       app->name, LABEL_COLOR, LABEL_SCALE);
}

static void draw_page_dots(Framebuffer *fb, int current, int total) {
    if (total <= 1) return;

    int dot_r   = 5;
    int dot_gap = 20;
    int total_w = total * dot_r * 2 + (total - 1) * (dot_gap - dot_r * 2);
    int sx = fb->width / 2 - total_w / 2;
    int y  = SCREEN_SAFE_BOTTOM - 15;

    for (int i = 0; i < total; i++) {
        int cx = sx + i * dot_gap;
        fb_fill_circle(fb, cx, y, dot_r,
                       (i == current) ? DOT_ACTIVE : DOT_INACTIVE);
    }
}

static void draw_page_arrows(Framebuffer *fb, Launcher *l) {
    /* Left arrow: ◀ */
    if (l->current_page > 0) {
        int ax = SCREEN_SAFE_LEFT + 10;
        int ay = grid_top + grid_content_h / 2 - 10;
        fb_draw_text(fb, ax, ay, "<", RGB(150, 150, 170), 3);
    }
    /* Right arrow: ▶ */
    if (l->current_page < l->total_pages - 1) {
        int ax = SCREEN_SAFE_RIGHT - 20;
        int ay = grid_top + grid_content_h / 2 - 10;
        fb_draw_text(fb, ax, ay, ">", RGB(150, 150, 170), 3);
    }
}

static void draw_selection_border(Framebuffer *fb, int tx, int ty) {
    int bw = 3;
    for (int b = 0; b < bw; b++) {
        fb_draw_rounded_rect(fb,
                             tx - bw + b, ty - bw + b,
                             tile_w + 2 * (bw - b), tile_h + 2 * (bw - b),
                             TILE_RADIUS + bw - b, TILE_SEL_BORDER);
    }
}

static void draw_launcher(Launcher *l) {
    fb_clear(&l->fb, BG_COLOR);

    /* Title */
    text_draw_centered(&l->fb, l->fb.width / 2, SCREEN_SAFE_TOP + 18,
                       "ROOMWIZARD", TITLE_COLOR, 4);

    /* Tiles for current page */
    int start = l->current_page * apps_per_page;
    int count = l->app_count - start;
    if (count > apps_per_page) count = apps_per_page;

    for (int i = 0; i < count; i++) {
        int tx, ty;
        tile_xy(i, &tx, &ty);
        int abs_idx = start + i;
        int hl = (abs_idx == l->selected_app) ? 1 : 0;
        draw_tile(&l->fb, &l->apps[abs_idx], tx, ty, hl);
        if (abs_idx == l->selected_app)
            draw_selection_border(&l->fb, tx, ty);
    }

    /* Empty-state message */
    if (l->app_count == 0) {
        text_draw_centered(&l->fb, l->fb.width / 2, 220,
                           "No apps installed", RGB(150, 150, 150), 3);
        text_draw_centered(&l->fb, l->fb.width / 2, 260,
                           "Deploy apps with build-and-deploy.sh",
                           RGB(100, 100, 100), 2);
    }

    /* Page arrows and dots */
    draw_page_arrows(&l->fb, l);
    draw_page_dots(&l->fb, l->current_page, l->total_pages);

    /* Input hint */
    if (l->input.gamepad_connected || l->input.keyboard_connected)
        fb_draw_text(&l->fb, SCREEN_SAFE_LEFT + 10, l->fb.height - 18,
                     "D-PAD: NAVIGATE  A/ENTER: LAUNCH", RGB(100, 100, 100), 1);

    fb_swap(&l->fb);
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Touch handling                                                         */
/* ════════════════════════════════════════════════════════════════════════ */

/*  Returns:  >= 0   app index to launch
 *            -1     nothing / page change (redraw)
 */
static int handle_touch(Launcher *l, int x, int y) {
    int start = l->current_page * apps_per_page;
    int count = l->app_count - start;
    if (count > apps_per_page) count = apps_per_page;

    /* Check each visible tile */
    for (int i = 0; i < count; i++) {
        int tx, ty;
        tile_xy(i, &tx, &ty);
        if (x >= tx && x < tx + tile_w && y >= ty && y < ty + tile_h) {
            /* Visual feedback: highlight tile briefly */
            draw_tile(&l->fb, &l->apps[start + i], tx, ty, 1);
            fb_swap(&l->fb);
            usleep(120000);
            return start + i;
        }
    }

    /* Pagination: left edge = previous, right edge = next */
    if (x < SCREEN_SAFE_LEFT + 50 && l->current_page > 0) {
        l->current_page--;
        return -1;
    }
    if (x > SCREEN_SAFE_RIGHT - 50 &&
        l->current_page < l->total_pages - 1) {
        l->current_page++;
        return -1;
    }

    return -1;
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Gamepad / keyboard navigation                                          */
/* ════════════════════════════════════════════════════════════════════════ */

/* Ensure selected_app is on the currently visible page; adjust page if not. */
static void ensure_selection_visible(Launcher *l) {
    if (l->selected_app < 0) return;
    int page = l->selected_app / apps_per_page;
    if (page != l->current_page)
        l->current_page = page;
}

/*  Returns:  >= 0  app index to launch
 *            -1    nothing (navigation only, or no input)
 */
static int handle_gamepad_input(Launcher *l) {
    InputState *inp = &l->input;

    /* If nothing is selected yet but a nav key is pressed, select first on page */
    if (l->selected_app < 0) {
        if (inp->buttons[BTN_ID_UP].pressed   || inp->buttons[BTN_ID_DOWN].pressed ||
            inp->buttons[BTN_ID_LEFT].pressed  || inp->buttons[BTN_ID_RIGHT].pressed) {
            l->selected_app = l->current_page * apps_per_page;
            return -1;
        }
    }

    int page_start = l->current_page * apps_per_page;
    int page_count = l->app_count - page_start;
    if (page_count > apps_per_page) page_count = apps_per_page;
    int idx_on_page = l->selected_app - page_start;  /* 0-based index within page */

    int col = idx_on_page % grid_cols;
    int row = idx_on_page / grid_cols;

    /* Navigate right */
    if (inp->buttons[BTN_ID_RIGHT].pressed) {
        if (l->selected_app + 1 < l->app_count) {
            l->selected_app++;
            ensure_selection_visible(l);
        }
    }
    /* Navigate left */
    if (inp->buttons[BTN_ID_LEFT].pressed) {
        if (l->selected_app > 0) {
            l->selected_app--;
            ensure_selection_visible(l);
        }
    }
    /* Navigate down */
    if (inp->buttons[BTN_ID_DOWN].pressed) {
        int target = l->selected_app + grid_cols;
        if (target < l->app_count) {
            l->selected_app = target;
            ensure_selection_visible(l);
        }
    }
    /* Navigate up */
    if (inp->buttons[BTN_ID_UP].pressed) {
        int target = l->selected_app - grid_cols;
        if (target >= 0) {
            l->selected_app = target;
            ensure_selection_visible(l);
        }
    }

    /* Select / launch */
    if (inp->buttons[BTN_ID_JUMP].pressed || inp->buttons[BTN_ID_ACTION].pressed) {
        if (l->selected_app >= 0 && l->selected_app < l->app_count)
            return l->selected_app;
    }

    return -1;
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  App launching                                                          */
/* ════════════════════════════════════════════════════════════════════════ */

static void launch_app(Launcher *l, int index,
                       const char *fb_dev, const char *touch_dev)
{
    AppEntry *app = &l->apps[index];
    LOG_INFO(&l->logger, "Launching: %s (%s)", app->name, app->exec_path);

    /* Fade out launcher before switching to app */
    fb_fade_out(&l->fb);

    /* Release framebuffer, touch, and gamepad so child gets exclusive access */
    gamepad_close(&l->gamepad);
    touch_close(&l->touch);
    fb_close(&l->fb);

    pid_t pid = fork();
    if (pid == 0) {
        /* ── Child process ────────────────────────────────────────── */
        switch (app->args) {
        case ARG_NONE:
            execl(app->exec_path, app->name, NULL);
            break;
        case ARG_FB:
            execl(app->exec_path, app->name, fb_dev, NULL);
            break;
        case ARG_TOUCH:
            execl(app->exec_path, app->name, touch_dev, NULL);
            break;
        case ARG_FB_TOUCH:
        default:
            execl(app->exec_path, app->name, fb_dev, touch_dev, NULL);
            break;
        }
        perror("execl failed");
        _exit(1);
    } else if (pid > 0) {
        /* ── Parent: wait for app to finish (EINTR-safe) ──────────── */
        int status;
        pid_t ret;
        do {
            ret = waitpid(pid, &status, 0);
        } while (ret == -1 && errno == EINTR);

        if (ret > 0) {
            if (WIFEXITED(status))
                LOG_INFO(&l->logger, "%s exited (code %d)", app->name, WEXITSTATUS(status));
            else if (WIFSIGNALED(status))
                LOG_WARN(&l->logger, "%s killed by signal %d", app->name, WTERMSIG(status));
            else
                LOG_WARN(&l->logger, "%s exited (raw status %d)", app->name, status);
        } else {
            LOG_WARN(&l->logger, "waitpid failed: %s", strerror(errno));
        }
    } else {
        perror("fork failed");
    }

    /* Re-acquire framebuffer (restore 32bpp in case child left 16bpp) */
    fb_set_bpp(fb_dev, 32);
    if (fb_init(&l->fb, fb_dev) != 0)
        LOG_ERROR(&l->logger, "Failed to re-initialise framebuffer");

    /* Recompute grid layout (screen dimensions may differ after child) */
    compute_grid_layout(&l->fb);

    /* Re-acquire touch */
    if (touch_init(&l->touch, touch_dev) == 0) {
        touch_set_screen_size(&l->touch, l->fb.width, l->fb.height);
        touch_drain_events(&l->touch);  /* Discard stale events from child */
    }

    /* Re-init gamepad after child exits */
    gamepad_init(&l->gamepad);

    /* Wait-for-release drain: prevent auto-relaunch from held Enter/A.
     *
     * After gamepad_init(), prev_held[] is zeroed.  If the launch button
     * (Enter or A) is still physically held, evdev key-repeat events keep
     * arriving.  Simply doing two dummy polls with separate zeroed
     * InputStates can leave prev_held in an inconsistent state: if the
     * second dummy poll sees no new events its InputState stays held=false,
     * flipping prev_held back to false — then the first REAL poll reads a
     * repeat event and sees a false "pressed" edge, re-launching the app.
     *
     * Fix: use a single persistent InputState across a polling loop so
     * that the held flag accurately tracks the physical key state (set by
     * key-down/repeat, cleared only by key-up).  Loop until all launch
     * buttons are released (or a 2-second safety timeout expires). */
    {
        InputState drain;
        memset(&drain, 0, sizeof(drain));
        int safety = 0;
        do {
            gamepad_poll(&l->gamepad, &drain, 0, 0, false);
            usleep(16000);  /* ~60 fps */
            safety++;
        } while ((drain.buttons[BTN_ID_JUMP].held ||
                  drain.buttons[BTN_ID_ACTION].held) && safety < 120);
    }
    /* Zero the main input state so no stale held flags carry over */
    memset(&l->input, 0, sizeof(l->input));

    /* Record return time — main loop will ignore ALL input for
     * LAUNCH_COOLDOWN_MS after this, as defense-in-depth against
     * stale events from any source (keyboard, touch, mouse). */
    l->last_launch_return_ms = get_time_ms();

    /* Re-scan manifests in case new apps were deployed while app ran */
    LOG_INFO(&l->logger, "Re-scanning apps...");
    scan_apps(l);
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Main                                                                   */
/* ════════════════════════════════════════════════════════════════════════ */

/* Graceful SIGTERM handling for clean shutdown from init script */
static volatile sig_atomic_t quit_flag = 0;
static void on_sigterm(int sig) { (void)sig; quit_flag = 1; }

int main(int argc, char *argv[]) {
    const char *fb_dev    = "/dev/fb0";
    const char *touch_dev = "/dev/input/touchscreen0";
    if (argc > 1) fb_dev    = argv[1];
    if (argc > 2) touch_dev = argv[2];

    printf("RoomWizard App Launcher\n");
    printf("=======================\n");

    /* Singleton guard — exit immediately if another instance is running */
    int lock_fd = acquire_instance_lock("app_launcher");
    if (lock_fd < 0) {
        fprintf(stderr, "app_launcher: another instance is already running\n");
        return 1;
    }

    signal(SIGTERM, on_sigterm);
    signal(SIGINT,  on_sigterm);

    Launcher launcher;
    memset(&launcher, 0, sizeof(launcher));

    /* Initialize logger */
    logger_init(&launcher.logger, "app_launcher", LOG_LEVEL_INFO, true);

    /* Ensure 32bpp — a previous process (e.g. ScummVM) may have left 16bpp */
    fb_set_bpp(fb_dev, 32);

    /* Framebuffer */
    if (fb_init(&launcher.fb, fb_dev) != 0) {
        LOG_ERROR(&launcher.logger, "Failed to initialise framebuffer");
        logger_close(&launcher.logger);
        return 1;
    }

    /* Touch */
    if (touch_init(&launcher.touch, touch_dev) != 0) {
        LOG_ERROR(&launcher.logger, "Failed to initialise touch input");
        fb_close(&launcher.fb);
        logger_close(&launcher.logger);
        return 1;
    }
    touch_set_screen_size(&launcher.touch,
                          launcher.fb.width, launcher.fb.height);

    /* Gamepad / keyboard / mouse */
    gamepad_init(&launcher.gamepad);
    memset(&launcher.input, 0, sizeof(launcher.input));
    launcher.last_rescan_ms = 0;
    launcher.last_launch_return_ms = 0;
    launcher.selected_app = -1;  /* No keyboard selection until user navigates */

    /* Compute grid layout based on screen dimensions */
    compute_grid_layout(&launcher.fb);

    /* Scan for installed apps */
    int count = scan_apps(&launcher);
    LOG_INFO(&launcher.logger, "Found %d app(s), %d page(s)", count, launcher.total_pages);

    /* ── Main loop — polling-based (~30 fps) ────────────────────── */
    while (!quit_flag) {
        /* Poll touch (non-blocking) */
        touch_poll(&launcher.touch);
        TouchState ts = touch_get_state(&launcher.touch);

        /* Poll gamepad / keyboard / mouse */
        gamepad_poll(&launcher.gamepad, &launcher.input,
                     ts.x, ts.y, ts.pressed);

        /* Periodic device rescan for hotplug */
        uint32_t now = get_time_ms();
        if (now - launcher.last_rescan_ms > RESCAN_INTERVAL_MS) {
            launcher.last_rescan_ms = now;
            gamepad_rescan(&launcher.gamepad);
        }

        /* ── Post-launch cooldown ──────────────────────────────────────
         * After returning from a child process, ignore ALL input for
         * LAUNCH_COOLDOWN_MS.  This is the primary defense against
         * auto-relaunch caused by:
         *   (a) Keyboard repeat events slipping through the drain loop
         *   (b) Resistive touchscreen noise generating a false press
         *   (c) Mouse button bounce after returning from child
         * The drain loop in launch_app() is kept as additional safety. */
        if (launcher.last_launch_return_ms &&
            (now - launcher.last_launch_return_ms) < LAUNCH_COOLDOWN_MS) {
            /* Still in cooldown — draw UI but skip all input handling */
            draw_launcher(&launcher);
            usleep(33333);
            continue;
        }

        /* Handle touch press */
        if (ts.pressed) {
            LOG_DEBUG(&launcher.logger, "Touch: (%d, %d)", ts.x, ts.y);
            int result = handle_touch(&launcher, ts.x, ts.y);
            if (result >= 0)
                launch_app(&launcher, result, fb_dev, touch_dev);
        }

        /* Handle mouse click */
        if (launcher.input.mouse_left_pressed) {
            LOG_DEBUG(&launcher.logger, "Mouse click: (%d, %d)",
                      launcher.input.mouse_x, launcher.input.mouse_y);
            int result = handle_touch(&launcher,
                                      launcher.input.mouse_x,
                                      launcher.input.mouse_y);
            if (result >= 0)
                launch_app(&launcher, result, fb_dev, touch_dev);
        }

        /* Handle gamepad / keyboard navigation */
        int gp_result = handle_gamepad_input(&launcher);
        if (gp_result >= 0)
            launch_app(&launcher, gp_result, fb_dev, touch_dev);

        /* Draw */
        draw_launcher(&launcher);

        usleep(33333);   /* ~30 fps */
    }

    /* Cleanup */
    LOG_INFO(&launcher.logger, "Shutting down launcher");
    logger_close(&launcher.logger);
    free_icons(&launcher);
    fb_clear(&launcher.fb, COLOR_BLACK);
    fb_swap(&launcher.fb);
    gamepad_close(&launcher.gamepad);
    touch_close(&launcher.touch);
    fb_close(&launcher.fb);
    return 0;
}
