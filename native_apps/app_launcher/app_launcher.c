/*
 * App Launcher for RoomWizard
 *
 * Visual grid launcher that scans the .app manifest files in /opt/roomwizard/apps
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
#include "common/icon_grid.h"
#include "common/logger.h"
#include "common/gamepad.h"
#include "common/hardware.h"
#include "common/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
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

/* ── Grid geometry: common/icon_grid.c, shared with device_tools' home screen ── */

#define TITLE_H         50
static IconGrid grid;

/* ── The MUSIC / EFFECTS toggles are NOT here ────────────────────────────────
 * They were, as a band between the last tile row and the page dots, and they
 * moved to device_tools' SETTINGS tab under the `AUDIO ENABLED` master they are
 * subordinate to.  Two reasons, in order: a games menu carrying settings widgets
 * is a games menu doing a settings tab's job, and this screen was then the only
 * games-side WRITER of two keys every game reads — so the launcher had to
 * re-read the file before each write to avoid reverting whatever device_tools
 * had set in the meantime.  One writer removes both problems.
 *
 * ⚠️ **Do not put them back here, and do not put them in a game's pause modal
 * either.** `MODAL_MAX_BUTTONS` is 4 and brick_breaker already uses all four;
 * `ModalDialog` has no widget slot; and a toggle that takes effect mid-run needs
 * a live setter for `Audio.music_on` (read once by `audio_init()`), a bed
 * stop/resume in the middle of a run, and seven writers of one key.
 */

/* Prints the layout receipt ("launcher: safe …") — see icon_grid_layout(). */
static void compute_grid_layout(Framebuffer *fb) {
    icon_grid_layout(&grid, fb, TITLE_H, "launcher");
}

/* ── Colours ────────────────────────────────────────────────────────────── */

#define BG_COLOR        RGB(25, 25, 35)
#define TITLE_COLOR     RGB(200, 200, 220)

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
    uint32_t *icon_pixels;      /* Loaded & scaled to ICON_GRID_ICON_SIZE², or NULL */
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
    bool        needs_redraw;       /* Dirty flag — skip rendering when false */
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

    app->icon_color = icon_grid_letter_color(app->name);
    return 0;
}

static void load_icon(AppEntry *app) {
    app->icon_pixels = icon_grid_load_icon(app->icon_path);
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

    l->total_pages = icon_grid_pages(&grid, l->app_count);
    if (l->current_page >= l->total_pages) l->current_page = l->total_pages - 1;

    return l->app_count;
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Drawing                                                                */
/* ════════════════════════════════════════════════════════════════════════ */

static void draw_tile(Framebuffer *fb, const AppEntry *app,
                      int tx, int ty, bool highlight) {
    icon_grid_draw_tile(fb, &grid, tx, ty, app->name, app->icon_pixels,
                        app->icon_color, highlight);
}

static void draw_launcher(Launcher *l) {
    fb_clear(&l->fb, BG_COLOR);

    /* Title */
    text_draw_centered(&l->fb, l->fb.width / 2, SCREEN_VISIBLE_TOP + 18,
                       "ROOMWIZARD", TITLE_COLOR, 4);

    /* Tiles for current page */
    int start = l->current_page * grid.per_page;
    int count = l->app_count - start;
    if (count > grid.per_page) count = grid.per_page;

    for (int i = 0; i < count; i++) {
        int tx, ty;
        icon_grid_tile_xy(&grid, i, &tx, &ty);
        int abs_idx = start + i;
        bool hl = (abs_idx == l->selected_app);
        draw_tile(&l->fb, &l->apps[abs_idx], tx, ty, hl);
        if (hl)
            icon_grid_draw_selection(&l->fb, &grid, tx, ty);
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
    icon_grid_draw_paging(&l->fb, &grid, l->current_page, l->total_pages);

    /* Input hint */
    if (l->input.gamepad_connected || l->input.keyboard_connected)
        fb_draw_text(&l->fb, SCREEN_VISIBLE_LEFT + 10, l->fb.height - 18,
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
    int start = l->current_page * grid.per_page;
    int count = l->app_count - start;
    if (count > grid.per_page) count = grid.per_page;

    int i = icon_grid_hit(&grid, count, x, y);
    if (i >= 0) {
        /* Visual feedback: highlight tile briefly */
        int tx, ty;
        icon_grid_tile_xy(&grid, i, &tx, &ty);
        draw_tile(&l->fb, &l->apps[start + i], tx, ty, true);
        fb_swap(&l->fb);
        usleep(120000);
        return start + i;
    }

    /* Pagination: left edge band = previous, right edge band = next */
    l->current_page += icon_grid_page_hit(x, l->current_page, l->total_pages);
    return -1;
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Gamepad / keyboard navigation                                          */
/* ════════════════════════════════════════════════════════════════════════ */

/* Ensure selected_app is on the currently visible page; adjust page if not. */
static void ensure_selection_visible(Launcher *l) {
    if (l->selected_app < 0) return;
    int page = l->selected_app / grid.per_page;
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
            l->selected_app = l->current_page * grid.per_page;
            return -1;
        }
    }

    /* Navigation is over absolute indices; ensure_selection_visible() flips the
       page when the selection leaves it, so there is no per-page bookkeeping. */

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
        int target = l->selected_app + grid.cols;
        if (target < l->app_count) {
            l->selected_app = target;
            ensure_selection_visible(l);
        }
    }
    /* Navigate up */
    if (inp->buttons[BTN_ID_UP].pressed) {
        int target = l->selected_app - grid.cols;
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
        /* argv[0] is the executable path, NOT app->name.  Passing the manifest's
         * display name made /proc/<pid>/cmdline read "VNC Client" while the
         * binary was /opt/vnc_client/vnc_client — which cost a session to
         * diagnose, because busybox `ps w` shows only processes with a TTY and
         * the cmdline is then the only thing left to walk.  (`comm` was always
         * right: the kernel takes it from the file being executed.)  Nothing
         * reads argv[0] except the usage text of three CLI tools. */
        switch (app->args) {
        case ARG_NONE:
            execl(app->exec_path, app->exec_path, NULL);
            break;
        case ARG_FB:
            execl(app->exec_path, app->exec_path, fb_dev, NULL);
            break;
        case ARG_TOUCH:
            execl(app->exec_path, app->exec_path, touch_dev, NULL);
            break;
        case ARG_FB_TOUCH:
        default:
            execl(app->exec_path, app->exec_path, fb_dev, touch_dev, NULL);
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
     * gamepad_init() has just cleared the manager, including the latched key
     * levels.  If the launch button (Enter or A) is still physically held,
     * evdev key-repeat events keep arriving, and the first REAL poll would see
     * a fresh press edge and immediately relaunch the app.  So poll until the
     * launch buttons read released (or a 2-second safety timeout expires).
     *
     * Since B2, held is a pure output of gamepad_poll() computed from state
     * inside the manager, so the InputState used here is arbitrary — it does
     * not have to be the same one the main loop uses. */
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
    /* ⚠️ **stdout is a FILE at boot, so it is block-buffered and the receipt below
     * does not arrive.** `/etc/init.d/roomwizard-app` redirects us into
     * /var/log/roomwizard/app_stdout.log; glibc then buffers 4 KB and the
     * compute_grid_layout() receipt — the one thing that has to be read BEFORE a
     * screenshot is trusted — sits in that buffer until the app exits or the
     * buffer fills.  Measured 2026-08-22 on `.188`: the log ended mid-word in
     * "Touch-safe area: lo" with the receipt nowhere in it.  `common/logger.c`
     * line-buffers its OWN file and so was never affected, which is what makes
     * this look like a missing printf rather than a buffering one. */
    setvbuf(stdout, NULL, _IOLBF, 0);

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

    /* Turn off LEDs — they stay yellow after boot from the firmware */
    hw_leds_off();
    LOG_INFO(&launcher.logger, "LEDs turned off");

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
    launcher.needs_redraw = true;  /* Force initial frame draw */

    /* Compute grid layout based on screen dimensions */
    compute_grid_layout(&launcher.fb);

    /* Scan for installed apps */
    int count = scan_apps(&launcher);
    LOG_INFO(&launcher.logger, "Found %d app(s), %d page(s)", count, launcher.total_pages);

    /* ── Main loop — polling-based, with dirty-flag rendering ───── */
    while (!quit_flag) {
        /* Save visual state for dirty detection */
        int old_selected = launcher.selected_app;
        int old_page     = launcher.current_page;
        int old_count    = launcher.app_count;
        bool old_gp_conn = launcher.input.gamepad_connected;
        bool old_kb_conn = launcher.input.keyboard_connected;

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
            /* Still in cooldown — draw once if needed, skip input */
            if (launcher.needs_redraw) {
                draw_launcher(&launcher);
                launcher.needs_redraw = false;
            }
            usleep(FRAME_DELAY_ACTIVE_US);
            continue;
        }

        if (ts.pressed) {
            /* Handle touch press */
            LOG_DEBUG(&launcher.logger, "Touch: (%d, %d)", ts.x, ts.y);
            int result = handle_touch(&launcher, ts.x, ts.y);
            if (result >= 0) {
                launch_app(&launcher, result, fb_dev, touch_dev);
                launcher.needs_redraw = true;  /* State changed after launch return */
            }
        }

        /* Handle mouse click */
        if (launcher.input.mouse_left_pressed) {
            LOG_DEBUG(&launcher.logger, "Mouse click: (%d, %d)",
                      launcher.input.mouse_x, launcher.input.mouse_y);
            int result = handle_touch(&launcher,
                                      launcher.input.mouse_x,
                                      launcher.input.mouse_y);
            if (result >= 0) {
                launch_app(&launcher, result, fb_dev, touch_dev);
                launcher.needs_redraw = true;  /* State changed after launch return */
            }
        }

        /* Handle gamepad / keyboard navigation */
        int gp_result = handle_gamepad_input(&launcher);
        if (gp_result >= 0) {
            launch_app(&launcher, gp_result, fb_dev, touch_dev);
            launcher.needs_redraw = true;  /* State changed after launch return */
        }

        /* Detect any visual state changes from input handling */
        if (launcher.selected_app != old_selected ||
            launcher.current_page != old_page     ||
            launcher.app_count    != old_count    ||
            launcher.input.gamepad_connected  != old_gp_conn ||
            launcher.input.keyboard_connected != old_kb_conn) {
            launcher.needs_redraw = true;
        }

        /* Conditional rendering — only redraw when UI state has changed */
        bool drew_frame = false;
        if (launcher.needs_redraw) {
            draw_launcher(&launcher);
            launcher.needs_redraw = false;
            drew_frame = true;
        }

        /* Adaptive sleep — longer idle sleep reduces CPU usage significantly.
         * ~30 fps when actively redrawing; ~10 fps polling when idle. */
        if (drew_frame) {
            usleep(FRAME_DELAY_ACTIVE_US);
        } else {
            usleep(FRAME_DELAY_IDLE_US);
        }
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
