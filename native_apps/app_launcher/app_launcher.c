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
 * Grid: 3 columns × 2 rows = 6 per page, with pagination via edge touch.
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include "common/common.h"
#include "common/ppm.h"
#include "common/logger.h"
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

/* ── Grid geometry ──────────────────────────────────────────────────────── */

#define GRID_COLS       3
#define GRID_ROWS       2
#define APPS_PER_PAGE   (GRID_COLS * GRID_ROWS)

#define TILE_W          200
#define TILE_H          150
#define TILE_GAP_X      20
#define TILE_GAP_Y      20
#define ICON_SIZE       96
#define LABEL_SCALE     2

#define TITLE_H         50
#define DOTS_H          30

/* Derived: centre the 3×2 grid within the safe area */
#define GRID_CONTENT_W  (GRID_COLS * TILE_W + (GRID_COLS - 1) * TILE_GAP_X)
#define GRID_CONTENT_H  (GRID_ROWS * TILE_H + (GRID_ROWS - 1) * TILE_GAP_Y)

#define GRID_LEFT       (SCREEN_SAFE_LEFT + (SCREEN_SAFE_WIDTH - GRID_CONTENT_W) / 2)
#define GRID_TOP        (SCREEN_SAFE_TOP + TITLE_H + \
                         (SCREEN_SAFE_HEIGHT - TITLE_H - DOTS_H - GRID_CONTENT_H) / 2)

/* ── Colours ────────────────────────────────────────────────────────────── */

#define BG_COLOR        RGB(25, 25, 35)
#define TILE_BG         RGB(45, 45, 60)
#define TILE_BORDER     RGB(70, 70, 90)
#define TILE_HL_BG      RGB(65, 65, 85)
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
    Framebuffer fb;
    TouchInput  touch;
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

    l->total_pages = (l->app_count + APPS_PER_PAGE - 1) / APPS_PER_PAGE;
    if (l->total_pages < 1) l->total_pages = 1;
    if (l->current_page >= l->total_pages) l->current_page = l->total_pages - 1;

    return l->app_count;
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Drawing                                                                */
/* ════════════════════════════════════════════════════════════════════════ */

static void tile_xy(int idx_on_page, int *x, int *y) {
    int col = idx_on_page % GRID_COLS;
    int row = idx_on_page / GRID_COLS;
    *x = GRID_LEFT + col * (TILE_W + TILE_GAP_X);
    *y = GRID_TOP  + row * (TILE_H + TILE_GAP_Y);
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
    fb_fill_rounded_rect(fb, tx, ty, TILE_W, TILE_H, TILE_RADIUS,
                         highlight ? TILE_HL_BG : TILE_BG);
    fb_draw_rounded_rect(fb, tx, ty, TILE_W, TILE_H, TILE_RADIUS, TILE_BORDER);

    /* Icon — centred horizontally, 8 px from top of tile */
    int icon_x = tx + (TILE_W - ICON_SIZE) / 2;
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
    text_draw_centered(fb, tx + TILE_W / 2, label_y,
                       app->name, LABEL_COLOR, LABEL_SCALE);
}

static void draw_page_dots(Framebuffer *fb, int current, int total) {
    if (total <= 1) return;

    int dot_r   = 5;
    int dot_gap = 20;
    int total_w = total * dot_r * 2 + (total - 1) * (dot_gap - dot_r * 2);
    int sx = 400 - total_w / 2;
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
        int ay = GRID_TOP + GRID_CONTENT_H / 2 - 10;
        fb_draw_text(fb, ax, ay, "<", RGB(150, 150, 170), 3);
    }
    /* Right arrow: ▶ */
    if (l->current_page < l->total_pages - 1) {
        int ax = SCREEN_SAFE_RIGHT - 20;
        int ay = GRID_TOP + GRID_CONTENT_H / 2 - 10;
        fb_draw_text(fb, ax, ay, ">", RGB(150, 150, 170), 3);
    }
}

static void draw_launcher(Launcher *l) {
    fb_clear(&l->fb, BG_COLOR);

    /* Title */
    text_draw_centered(&l->fb, 400, SCREEN_SAFE_TOP + 18,
                       "ROOMWIZARD", TITLE_COLOR, 4);

    /* Tiles for current page */
    int start = l->current_page * APPS_PER_PAGE;
    int count = l->app_count - start;
    if (count > APPS_PER_PAGE) count = APPS_PER_PAGE;

    for (int i = 0; i < count; i++) {
        int tx, ty;
        tile_xy(i, &tx, &ty);
        draw_tile(&l->fb, &l->apps[start + i], tx, ty, 0);
    }

    /* Empty-state message */
    if (l->app_count == 0) {
        text_draw_centered(&l->fb, 400, 220,
                           "No apps installed", RGB(150, 150, 150), 3);
        text_draw_centered(&l->fb, 400, 260,
                           "Deploy apps with build-and-deploy.sh",
                           RGB(100, 100, 100), 2);
    }

    /* Page arrows and dots */
    draw_page_arrows(&l->fb, l);
    draw_page_dots(&l->fb, l->current_page, l->total_pages);

    fb_swap(&l->fb);
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Touch handling                                                         */
/* ════════════════════════════════════════════════════════════════════════ */

/*  Returns:  >= 0   app index to launch
 *            -1     nothing / page change (redraw)
 */
static int handle_touch(Launcher *l, int x, int y) {
    int start = l->current_page * APPS_PER_PAGE;
    int count = l->app_count - start;
    if (count > APPS_PER_PAGE) count = APPS_PER_PAGE;

    /* Check each visible tile */
    for (int i = 0; i < count; i++) {
        int tx, ty;
        tile_xy(i, &tx, &ty);
        if (x >= tx && x < tx + TILE_W && y >= ty && y < ty + TILE_H) {
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
/*  App launching                                                          */
/* ════════════════════════════════════════════════════════════════════════ */

static void launch_app(Launcher *l, int index,
                       const char *fb_dev, const char *touch_dev)
{
    AppEntry *app = &l->apps[index];
    LOG_INFO(&l->logger, "Launching: %s (%s)", app->name, app->exec_path);

    /* Fade out launcher before switching to app */
    fb_fade_out(&l->fb);

    /* Release BOTH framebuffer and touch so child gets exclusive access.
     * This prevents the parent from accidentally drawing while blocked,
     * and avoids two processes holding the fb mmap simultaneously.       */
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

    /* Re-acquire touch */
    if (touch_init(&l->touch, touch_dev) == 0) {
        touch_set_screen_size(&l->touch, l->fb.width, l->fb.height);
        touch_drain_events(&l->touch);  /* Discard stale events from child */
    }

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

    /* Scan for installed apps */
    int count = scan_apps(&launcher);
    LOG_INFO(&launcher.logger, "Found %d app(s), %d page(s)", count, launcher.total_pages);

    /* ── Main loop ──────────────────────────────────────────────── */
    while (!quit_flag) {
        draw_launcher(&launcher);

        int x, y;
        if (touch_wait_for_press(&launcher.touch, &x, &y) == 0) {
            if (quit_flag) break;
            LOG_DEBUG(&launcher.logger, "Touch: (%d, %d)", x, y);
            int result = handle_touch(&launcher, x, y);
            if (result >= 0)
                launch_app(&launcher, result, fb_dev, touch_dev);
        }

        usleep(50000);   /* 50 ms debounce */
    }

    /* Cleanup */
    LOG_INFO(&launcher.logger, "Shutting down launcher");
    logger_close(&launcher.logger);
    free_icons(&launcher);
    fb_clear(&launcher.fb, COLOR_BLACK);
    fb_swap(&launcher.fb);
    touch_close(&launcher.touch);
    fb_close(&launcher.fb);
    return 0;
}
