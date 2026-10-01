/**
 * Control Panel — Unified Hardware App for RoomWizard
 *
 * Two views: the home icon grid, and one open page (a CpPage module, cp_page.h)
 * under a bar carrying BACK and the page's name.  Each tile opens a page:
 *   Audio        — enable, music/effects, output device, the TEST chime and
 *                  the MIX BUS TEST launch (audio_page.c)
 *   Display      — backlight, orientation, what is visible and touchable,
 *                  SCREEN EDGES and the display tests (display_page.c)
 *   LED          — enable, brightness and the LED tests (led_page.c)
 *   USB          — the bus list and RESCAN (usb_page.c)
 *   Bluetooth    — power, scan, pair/trust/connect/remove, agent prompts,
 *                  USE FOR AUDIO (bluetooth_page.c, over bt_ctl.c)
 *   Input        — the keyboard/mouse/pad testers, on any bus (input_page.c)
 *   Network      — gateway, DNS and every interface (network_page.c)
 *   Monitor      — live uptime, load, memory and storage (monitor_page.c)
 *   Information  — what this unit is (info_page.c)
 */

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Includes & Constants
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

#include "../common/framebuffer.h"
#include "../common/touch_input.h"
#include "../common/touch_calib.h"
#include "../common/hardware.h"
#include "../common/common.h"
#include "../common/config.h"
#include "../common/icon_grid.h"
#include "cp_ui.h"
#include "cp_page.h"
#include "touch_wizard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/fb.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <linux/input.h>
#include <poll.h>

/* SCREEN_W / SCREEN_H removed — use screen_base_width / screen_base_height
   runtime globals (from framebuffer.h) or fb->width / fb->height instead. */

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Color Palette
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

#define COLOR_PAGE_BAR_BG    RGB(30, 30, 45)
#define COLOR_BACK_BTN       RGB(35, 35, 50)
#define COLOR_SECTION_LINE   RGB(60, 60, 80)
#define COLOR_HEADER_TEXT    COLOR_CYAN
#define COLOR_BAR_BG         RGB(40, 40, 40)
#define COLOR_BAR_FILL       RGB(0, 180, 60)
#define COLOR_BAR_WARN       COLOR_YELLOW
#define COLOR_BAR_CRIT       COLOR_RED

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Layout Constants
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/* TAB_BAR_H, CONTENT_*, BAR_WIDTH/BAR_HEIGHT and COLOR_LABEL live in cp_ui.h,
 * which the page modules share. */

#define BACK_BTN_W        55
#define BACK_BTN_H        40

/* How long a status message stays up.  A page's message is held longer: the
 * one that exists names a file path, which a 2 s flash does not let anyone
 * read. */
#define STATUS_HOLD_MS       2000
#define STATUS_PAGE_HOLD_MS  6000

/* The old 40 px calibration-target inset lived here. It is gone on purpose:
 * targets that close to the edge sit inside the band where raw compresses, and
 * fitting through them is what produced a phantom horizontal inset for months.
 * Target geometry now comes from common/touch_calib.h. */
/* The uncalibrated diagnostic, launched from the Input page. Deployed by
 * build-and-deploy.sh with no manifest, so the launcher does not show it —
 * its DIAGNOSTIC button is the discoverable route to it. */
#define TOUCH_DIAG_PATH   "/opt/games/touch_raw"

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * State Machine
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

typedef enum {
    CONFIRM_NONE,
    CONFIRM_PAGE             /* a page's cp_confirm(): its on_ok runs on OK */
} ConfirmAction;

/* The home grid, and the page registry: one tile per CpPage, in this order,
 * taking its label and icon from the page (one name, one home); every page
 * named here is loaded, laid out and reset through it.  A page's icon NULL =
 * the grid's letter tile. */
static const CpPage *const home_pages[] = {
    &cp_audio_page,
    &cp_display_page,
    &cp_led_page,
    &cp_usb_page,
    &cp_bluetooth_page,
    &cp_input_page,
    &cp_network_page,
    &cp_monitor_page,
    &cp_info_page,
};
#define HOME_PAGE_COUNT ((int)(sizeof(home_pages) / sizeof(home_pages[0])))
#define HOME_TITLE_H    50

typedef struct {
    const CpPage *page;               /* the open page; NULL = the home grid */
    bool          page_dirty;         /* the page asked to be repainted */
    bool          page_fullscreen;    /* its input() queued a full-screen run */
    char          status_msg[64];
    uint32_t      status_time_ms;
    uint32_t      status_hold_ms;     /* how long it shows; 0 = STATUS_HOLD_MS */
    bool          status_ok;          /* a page's message: success or failure colour */
    int           home_page;         /* page of the home grid */
    Config        cfg;
    ConfirmAction confirm_action;
} AppState;

/* â”€â”€ Globals â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static volatile bool running = true;
static Framebuffer  *g_fb    = NULL;
static TouchInput   *g_touch = NULL;
static AppState     *g_state = NULL;   /* for cp_status() / cp_reset_all_defaults() */

static void signal_handler(int sig) {
    (void)sig;
    running = false;
}

bool cp_running(void) { return running; }

/* â”€â”€ UI Elements â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static Button back_btn;          /* the page bar's BACK to the home grid */

/* The panel's one confirmation dialog: every page's cp_confirm() (cp_page.h)
 * opens this same instance.  While
 * confirm_action is not CONFIRM_NONE main() draws it over everything and routes
 * all input to it — the page bar and the page included, so BACK under the
 * overlay cannot leave the page with the question still open. */
static ModalDialog  confirm_dialog;
static CpConfirmFn  confirm_on_ok;   /* CONFIRM_PAGE: run on OK, then NULL */

static void confirm_open(AppState *state, ConfirmAction action, const char *title,
                         const char *message, const char *ok_text, CpConfirmFn on_ok) {
    modal_dialog_init_confirm(&confirm_dialog, title, message,
                              ok_text, BTN_COLOR_DANGER,
                              "CANCEL", RGB(100, 100, 100));
    modal_dialog_show(&confirm_dialog);
    confirm_on_ok = on_ok;
    state->confirm_action = action;
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Shared Drawing Helpers
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

void draw_section_header(Framebuffer *fb, int y, const char *title) {
    int left = CONTENT_LEFT;
    int right = CONTENT_RIGHT;
    fb_draw_text(fb, left, y, title, COLOR_HEADER_TEXT, 2);
    int text_w = text_measure_width(title, 2);
    int line_x = left + text_w + 12;
    if (line_x < right) {
        fb_draw_line(fb, line_x, y + 8, right, y + 8, COLOR_SECTION_LINE);
    }
}

void draw_brightness_bar(Framebuffer *fb, int x, int y, int value,
                         int min_val, int max_val, int bar_width, bool active) {
    fb_fill_rect(fb, x, y, bar_width, BAR_HEIGHT, COLOR_BAR_BG);
    int range = max_val - min_val;
    int fill_w = (range > 0) ? ((value - min_val) * bar_width) / range : 0;
    if (fill_w > bar_width) fill_w = bar_width;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > 0)
        fb_fill_rect(fb, x, y, fill_w, BAR_HEIGHT,
                     active ? COLOR_BAR_FILL : COLOR_DISABLED);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", value);
    fb_draw_text(fb, x + bar_width + 10, y + 2, pct,
                 active ? COLOR_WHITE : COLOR_DISABLED, 2);
}

bool fit_value(const char *src, int x, int scale, char *out, size_t len) {
    snprintf(out, len, "%s", src);
    int room = CONTENT_RIGHT - x;
    if (text_measure_width(out, scale) <= room) return false;
    size_t n = strlen(out);
    while (n > 2 && text_measure_width(out, scale) > room) {
        out[--n] = '\0';
        out[n - 1] = '.';
        out[n - 2] = '.';
    }
    return true;
}

int draw_info_row(Framebuffer *fb, int y, const char *label,
                  const char *value, uint32_t value_color) {
    int value_x = CONTENT_LEFT + (CONTENT_WIDTH < 600 ? 150 : 270);
    fb_draw_text(fb, CONTENT_LEFT + 10, y, label, COLOR_LABEL, 2);
    fb_draw_text(fb, value_x, y, value, value_color, 2);
    return y + 28;
}

void draw_usage_bar(Framebuffer *fb, int x, int y, int width,
                    int height, unsigned long used,
                    unsigned long total, const char *label) {
    if (label && label[0])
        fb_draw_text(fb, x, y - 18, label, COLOR_LABEL, 2);
    fb_fill_rect(fb, x, y, width, height, COLOR_BAR_BG);
    int fill = (total > 0) ? (int)((unsigned long long)used * width / total) : 0;
    if (fill > width) fill = width;
    uint32_t color;
    if (width > 0) {
        int pct_fill = fill * 100 / width;
        if (pct_fill > 90) color = COLOR_BAR_CRIT;
        else if (pct_fill > 70) color = COLOR_BAR_WARN;
        else color = COLOR_GREEN;
    } else {
        color = COLOR_GREEN;
    }
    if (fill > 0)
        fb_fill_rect(fb, x, y, fill, height, color);
    int percent = (total > 0) ? (int)((unsigned long long)used * 100 / total) : 0;
    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    text_draw_centered(fb, x + width / 2, y + height / 2, pct, COLOR_WHITE, 1);
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Page Bar
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static void create_page_bar(void) {
    /* BACK on the left, where the full-screen testers keep theirs: the top-right
     * corner is the home grid's exit X, and a double tap there must not both
     * leave the page and quit. */
    button_init_full(&back_btn,
                     SCREEN_SAFE_LEFT + 10, SCREEN_SAFE_TOP + 2,
                     BACK_BTN_W, BACK_BTN_H, "<",
                     COLOR_BACK_BTN, COLOR_WHITE,
                     BTN_HIGHLIGHT_COLOR, 3);
}

/* Drawn over an open page only: BACK, and the page's name — which is also its
 * tile's label — or the page's status line while one shows. */
static void draw_page_bar(Framebuffer *fb, AppState *state) {
    fb_fill_rect(fb, SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP,
                 SCREEN_SAFE_WIDTH, TAB_BAR_H, COLOR_PAGE_BAR_BG);
    if (state->status_msg[0]) {
        /* A page's status line takes the title's place while it shows: the
         * page owns the whole content rect, so the bar is the one spot that is
         * free in both orientations.  Scale 2 where it fits between BACK and
         * the right edge, else scale 1, cut with ".." if even that is too wide. */
        int left = back_btn.x + back_btn.width + 10;
        int scale = (text_measure_width(state->status_msg, 2) <= CONTENT_RIGHT - left) ? 2 : 1;
        char cut[sizeof(state->status_msg)];
        fit_value(state->status_msg, left, scale, cut, sizeof(cut));
        text_draw_centered(fb, (left + CONTENT_RIGHT) / 2,
                           back_btn.y + back_btn.height / 2, cut,
                           state->status_ok ? COLOR_GREEN : COLOR_ORANGE, scale);
    } else {
        text_draw_centered(fb, fb->width / 2, back_btn.y + back_btn.height / 2,
                           state->page->name, COLOR_WHITE, 3);
    }
    button_draw(fb, &back_btn);
    fb_draw_line(fb, SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP + TAB_BAR_H,
                 SCREEN_SAFE_RIGHT, SCREEN_SAFE_TOP + TAB_BAR_H,
                 COLOR_SECTION_LINE);
}

/* The one place a view changes — page NULL is the home grid — so entering and
 * leaving keep their side effects whether a home tile or BACK asked. */
static void set_view(AppState *state, const CpPage *page) {
    const CpPage *prev_page = state->page;
    if (prev_page && prev_page != page && prev_page->leave)
        prev_page->leave();
    /* A message belongs to the page that posted it: the page bar draws the one
     * status_msg, so a page's BACKUP line must not follow BACK out of it.  The
     * view change repaints anyway. */
    if (page != prev_page) {
        state->status_msg[0]  = '\0';
        state->status_hold_ms = 0;
    }
    state->page = page;
    state->page_fullscreen = false;
    if (page && page != prev_page) {
        if (page->enter) page->enter();
        state->page_dirty = true;
    }
}

static void handle_page_bar_input(AppState *state, int tx, int ty,
                                  bool touching, uint32_t now) {
    if (button_update(&back_btn, tx, ty, touching, now))
        set_view(state, NULL);
}

/* ── Home grid ─────────────────────────────────────────────────────────────── */

static IconGrid  home_grid;
static uint32_t *home_icons[HOME_PAGE_COUNT];
static int       home_press = -2;   /* tile index pressed, -1 = exit X, -2 = none */

static void home_load_icons(void) {
    for (int i = 0; i < HOME_PAGE_COUNT; i++) {
        const char *icon = home_pages[i]->icon;
        if (!icon) continue;
        char path[128];
        snprintf(path, sizeof(path), "/opt/roomwizard/icons/%s.ppm", icon);
        home_icons[i] = icon_grid_load_icon(path);
    }
}

static int home_count_on_page(int page) {
    int n = HOME_PAGE_COUNT - page * home_grid.per_page;
    return n > home_grid.per_page ? home_grid.per_page : n;
}

static void draw_home(Framebuffer *fb, AppState *state) {
    int pages = icon_grid_pages(&home_grid, HOME_PAGE_COUNT);
    if (state->home_page >= pages) state->home_page = pages - 1;

    text_draw_centered(fb, fb->width / 2, SCREEN_SAFE_TOP + 14, "CONTROL PANEL",
                       COLOR_WHITE, 3);
    icon_grid_draw_exit(fb, &home_grid);

    int start = state->home_page * home_grid.per_page;
    int n = home_count_on_page(state->home_page);
    for (int i = 0; i < n; i++) {
        const char *label = home_pages[start + i]->name;
        int x, y;
        icon_grid_tile_xy(&home_grid, i, &x, &y);
        icon_grid_draw_tile(fb, &home_grid, x, y, label, home_icons[start + i],
                            icon_grid_letter_color(label), false);
    }
    icon_grid_draw_paging(fb, &home_grid, state->home_page, pages);
}

/* A tap acts on RELEASE, and only on the target it was pressed on (the page-flip
 * bands act on the press, as in the launcher) — acting on
 * the press would leave the release to the page just opened, which sees a
 * finger lifting over whatever widget sits where the tile was. */
static void handle_home_input(AppState *state, const TouchState *ts) {
    int start = state->home_page * home_grid.per_page;
    if (ts->pressed) {
        int tile = icon_grid_hit(&home_grid, home_count_on_page(state->home_page),
                                 ts->x, ts->y);
        home_press = icon_grid_exit_hit(&home_grid, ts->x, ts->y) ? -1
                   : tile >= 0 ? tile : -2;
        if (home_press == -2)
            state->home_page += icon_grid_page_hit(ts->x, state->home_page,
                                                   icon_grid_pages(&home_grid, HOME_PAGE_COUNT));
    }
    /* No else: a quick tap delivers press and release in the same poll. */
    if (!ts->released || home_press == -2) return;

    int pressed = home_press;
    home_press = -2;
    if (pressed == -1) {
        if (icon_grid_exit_hit(&home_grid, ts->x, ts->y)) running = false;
        return;
    }
    if (icon_grid_hit(&home_grid, home_count_on_page(state->home_page),
                      ts->x, ts->y) != pressed)
        return;
    set_view(state, home_pages[start + pressed]);
}

/* ── RESET DEFAULTS: the one implementation (cp_page.h), pressed on the
 * Information page ─────────────────────────────────────────────────────── */

/* Copies the config file to "<path>.bak-YYYYmmdd-HHMMSS" before anything is
 * reset.  Timestamped so a second reset cannot overwrite the real backup with
 * the defaults the first one produced, and opened O_EXCL — two resets inside
 * one second take the next "-N" suffix rather than truncating the first.
 * Returns 1 with the backup's path in out, 0 when there is no file to back up
 * (nothing can be lost), -1 on failure with the reason in out. */
static int config_backup(const Config *cfg, char *out, size_t len) {
    int in = open(cfg->filepath, O_RDONLY);
    if (in < 0) {
        if (errno == ENOENT) { out[0] = '\0'; return 0; }
        snprintf(out, len, "READ %s", strerror(errno));
        return -1;
    }
    char stamp[32];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);

    char path[sizeof(cfg->filepath) + 40];
    int fd = -1;
    for (int n = 0; n < 10 && fd < 0; n++) {
        if (n == 0) snprintf(path, sizeof(path), "%s.bak-%s", cfg->filepath, stamp);
        else        snprintf(path, sizeof(path), "%s.bak-%s-%d", cfg->filepath, stamp, n);
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd < 0 && errno != EEXIST) break;
    }
    if (fd < 0) {
        snprintf(out, len, "CREATE %s", strerror(errno));
        close(in);
        return -1;
    }

    /* Every byte, and fsync before success is claimed: a backup that exists
     * only in the page cache is not one if the unit loses power next. */
    char buf[1024];
    ssize_t r;
    int err = 0;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        for (ssize_t off = 0; off < r; ) {
            ssize_t w = write(fd, buf + off, (size_t)(r - off));
            if (w < 0) { if (errno == EINTR) continue; err = errno; break; }
            off += w;
        }
        if (err) break;
    }
    if (r < 0 && !err) err = errno;
    if (!err && fsync(fd) < 0) err = errno;
    if (close(fd) < 0 && !err) err = errno;
    close(in);
    if (err) {
        unlink(path);   /* a partial copy must not pass for the backup */
        snprintf(out, len, "WRITE %s", strerror(err));
        return -1;
    }
    snprintf(out, len, "%s", path);
    return 1;
}

void cp_status(const char *msg, bool ok) {
    if (!g_state) return;
    snprintf(g_state->status_msg, sizeof(g_state->status_msg), "%s", msg);
    g_state->status_time_ms = get_time_ms();
    g_state->status_hold_ms = STATUS_PAGE_HOLD_MS;
    g_state->status_ok      = ok;
}

void cp_confirm(const char *title, const char *message, const char *ok_text,
                CpConfirmFn on_ok) {
    if (!g_state) return;
    confirm_open(g_state, CONFIRM_PAGE, title, message, ok_text, on_ok);
}

int cp_reset_all_defaults(Config *cfg, char *msg, size_t len) {
    char where[200];   /* config_backup() writes a path of up to 168 bytes */
    int b = config_backup(cfg, where, sizeof(where));
    if (b < 0) {
        /* Nothing is reset: the operator is told, and the settings stay. */
        fprintf(stderr, "control_panel: RESET DEFAULTS refused, backup failed: %s\n", where);
        snprintf(msg, len, "RESET FAILED: BACKUP %s", where);
        return -1;
    }
    if (b > 0) printf("control_panel: RESET DEFAULTS, config backed up to %s\n", where);
    else       printf("control_panel: RESET DEFAULTS, no config file to back up\n");

    /* Every page reads its defaults back off the cleared Config (the Audio
     * page through config.c's helpers, which is what common/audio.c reads).
     * A page with no SAVE (LED, Display) must also show what hardware.c
     * drives, so its reset_defaults() removes its own keys from the file and
     * reloads the cache, so page, file and hardware all land on config.c's
     * default together. */
    config_clear(cfg);
    for (int i = 0; i < HOME_PAGE_COUNT; i++)
        if (home_pages[i]->reset_defaults)
            home_pages[i]->reset_defaults(cfg);
    /* RESET writes the cleared file: the button is nowhere near a SAVE, and
     * the backup above is what makes the write safe.  Games then resolve every
     * key through config.c's defaults, the same values shown here. */
    config_save(cfg);
    if (b > 0) snprintf(msg, len, "BACKUP: %s", where);
    else       snprintf(msg, len, "DEFAULTS RESTORED - NO FILE TO BACK UP");
    return 0;
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * File helper (read_file_line, shared through cp_ui.h)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

int read_file_line(const char *path, char *buf, size_t len) {
    FILE *fp = fopen(path, "r");
    if (!fp) { buf[0] = '\0'; return -1; }
    if (!fgets(buf, (int)len, fp)) { buf[0] = '\0'; fclose(fp); return -1; }
    fclose(fp);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return 0;
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Full-screen test helpers (draw_test_screen, check_touch, shared through
 * cp_ui.h for the pages' test routines)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

void draw_test_screen(Framebuffer *fb, const char *title,
                      const char *status, int progress) {
    fb_clear(fb, COLOR_BLACK);
    text_draw_centered(fb, fb->width / 2, 50, title, COLOR_WHITE, 3);
    if (status) text_draw_centered(fb, fb->width / 2, 150, status, COLOR_CYAN, 2);
    if (progress >= 0) {
        int bw = (fb->width < 600) ? ((int)fb->width - 40) : 600;
        int bh = 40, bx = (fb->width - bw) / 2, by = 220;
        fb_draw_rect(fb, bx, by, bw, bh, RGB(51,51,51));
        fb_draw_rect(fb, bx, by, (bw * progress) / 100, bh, COLOR_GREEN);
        char p[16]; snprintf(p, sizeof(p), "%d%%", progress);
        text_draw_centered(fb, fb->width / 2, by + 20, p, COLOR_WHITE, 2);
    }
    text_draw_centered(fb, fb->width / 2, fb->height - 80, "TOUCH TO RETURN", RGB(136,136,136), 2);
    fb_swap(fb);
}

bool check_touch(TouchInput *touch, int *x, int *y) {
    if (touch_poll(touch) > 0) {
        TouchState ts = touch_get_state(touch);
        if (ts.pressed) { *x = ts.x; *y = ts.y; return true; }
    }
    return false;
}

static void rebuild_ui(AppState *state);

/* Put both config lines back to the compiled-in defaults. The raw range comes
 * from the hardware rather than from a fit, so this always yields a usable —
 * if imprecise — screen. Reachable from the Input page, so a wedged
 * calibration never requires SSH to undo. */
void cp_reset_touch_geometry(void) {
    TouchInput *touch = g_touch;
    if (!touch || !g_state) return;
    char bak[256] = "";
    touch_calib_backup(CALIB_FILE, bak, sizeof(bak));

    int hx0, hx1, hy0, hy1;
    touch_calib_hw_range(touch, &hx0, &hx1, &hy0, &hy1);
    touch_set_raw_range(touch, hx0, hx1, hy0, hy1);

    touch->calib.bezel_top    = FB_BEZEL_TOP_DEFAULT;
    touch->calib.bezel_bottom = FB_BEZEL_BOTTOM_DEFAULT;
    touch->calib.bezel_left   = FB_BEZEL_LEFT_DEFAULT;
    touch->calib.bezel_right  = FB_BEZEL_RIGHT_DEFAULT;

    bool ok = (touch_save_calibration(touch, CALIB_FILE) == 0);
    if (ok && g_fb) {
        fb_set_bezel(g_fb, FB_BEZEL_TOP_DEFAULT, FB_BEZEL_BOTTOM_DEFAULT,
                     FB_BEZEL_LEFT_DEFAULT, FB_BEZEL_RIGHT_DEFAULT);
        touch_set_screen_size(touch, (int)g_fb->width, (int)g_fb->height);
    }
    cp_status(ok ? "SCREEN GEOMETRY RESET" : "RESET FAILED - RUN AS ROOT", ok);
    rebuild_ui(g_state);   /* the bezel just changed the logical size */
}

/* ══════════════════════════════════════════════════════════════════════════
 * Touch diagnostic — hand the screen to /opt/games/touch_raw
 * ══════════════════════════════════════════════════════════════════════════ */

/* touch_raw is the only tool that shows the panel with NO calibration and NO
 * bezel, so a drawn pixel is a panel pixel. That is what makes it the independent
 * cross-check on the wizard: its SWEEP and INSET modes measure the digitiser's
 * reach by a completely different method from the interior fit, and on RW09 the
 * two agreed to the pixel (panel 30 / 450). It is not folded in here because
 * folding it in would mean control_panel carrying its own uncalibrated mode — and
 * because a separate binary cannot be broken by a bug in this one.
 *
 * Launched rather than linked, following app_launcher's pattern. The child owns
 * the framebuffer and the touch device while it runs; this process is blocked in
 * waitpid() and draws nothing.
 *
 * touch_raw's APPLY writes /etc/touch_calibration.conf, so on return the
 * in-memory calibration here may be stale. Reload everything rather than assume:
 * a wrong inset silently misplaces every button in the app. */
static void run_touch_diagnostic(Framebuffer *fb, TouchInput *touch) {
    if (access(TOUCH_DIAG_PATH, X_OK) != 0) {
        cp_status("TOUCH_RAW NOT INSTALLED", false);
        return;
    }

    fb_clear(fb, COLOR_BLACK);
    text_draw_centered(fb, (int)fb->width / 2, (int)fb->height / 2,
                       "STARTING TOUCH DIAGNOSTIC", COLOR_WHITE, 3);
    fb_swap(fb);

    pid_t pid = fork();
    if (pid < 0) {
        cp_status("FORK FAILED", false);
        return;
    }
    if (pid == 0) {
        execl(TOUCH_DIAG_PATH, "touch_raw", FB_DEVICE, TOUCH_DEVICE, (char *)NULL);
        perror("execl touch_raw");
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;   /* a signal must not orphan the child */

    /* The child may have rewritten the config, and it certainly left the
     * framebuffer with its own bezel and depth. Rebuild from the file. */
    fb_set_bpp(FB_DEVICE, 32);
    fb_load_bezel();
    fb_set_bezel(fb, screen_bezel_top, screen_bezel_bottom,
                     screen_bezel_left, screen_bezel_right);
    touch_load_calibration(touch, CALIB_FILE);
    touch_set_screen_size(touch, (int)fb->width, (int)fb->height);
    touch_drain_events(touch);

    bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    cp_status(ok ? "DIAGNOSTIC DONE - GEOMETRY RELOADED"
                 : "DIAGNOSTIC EXITED WITH AN ERROR", ok);
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Full-Screen Mode Handler
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/* Lay out the page bar, every page and the home grid. Re-run whenever the
 * logical screen size changes, since all of them derive their geometry from
 * SCREEN_SAFE_*. */
static void rebuild_ui(AppState *state) {
    create_page_bar();
    /* Each prints its "control_panel: <page> stack …" receipt. */
    for (int i = 0; i < HOME_PAGE_COUNT; i++)
        home_pages[i]->layout();
    /* Prints the "control_panel home: safe …" receipt — see icon_grid_layout(). */
    icon_grid_layout(&home_grid, g_fb, HOME_TITLE_H, "control_panel home");
}

void cp_run_touch_tool(Framebuffer *fb, TouchInput *touch, int mode) {
    if (mode == CP_TOUCH_DIAGNOSTIC)
        run_touch_diagnostic(fb, touch);
    else {
        TouchWizardResult r;
        touch_wizard_run(fb, touch, mode == CP_TOUCH_EDGES, &running, &r);
        if (r.msg[0])
            cp_status(r.msg, r.saved);
    }
    /* The logical screen may have resized under the UI - the page bar, every
     * page and the home grid are laid out from SCREEN_SAFE_*.  A page's
     * run_fullscreen() is followed by no rebuild of main()'s, so it is here. */
    rebuild_ui(g_state);
}

static void run_current_fullscreen_mode(Framebuffer *fb, TouchInput *touch,
                                        AppState *state) {
    state->page_fullscreen = false;
    if (state->page->run_fullscreen)
        state->page->run_fullscreen(fb, touch);
    /* Drain any lingering touch events (press/release) left in the input
     * buffer by the full-screen mode.  Without this, the stale release
     * (or held) event is picked up by the main-loop's touch_poll() and
     * immediately re-triggers the test/calibration that just exited.    */
    touch_drain_events(touch);
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Main Loop
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

int main(void) {
    /* ⚠️ FIRST, before any printf. Launched from the launcher, stdout is
     * /var/log/roomwizard/app_stdout.log — a FILE, so glibc block-buffers 4 KB
     * and a receipt printed by a process that is then killed never arrives at
     * all.  Every page's layout receipt is exactly that shape.
     * ../CLAUDE.md → App lifecycle carries the measurement. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    int lock_fd = acquire_instance_lock("control_panel");
    if (lock_fd < 0) return 1;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    hw_init();
    hw_set_backlight(100);

    /* Pin the depth rather than inherit it: /dev/fb0 keeps whatever ran last
     * (ScummVM and the VNC session leave 16bpp). See fb_set_bpp. */
    fb_set_bpp(FB_DEVICE, 32);

    Framebuffer fb;
    if (fb_init(&fb, FB_DEVICE) < 0) {
        fprintf(stderr, "control_panel: failed to init framebuffer\n");
        return 1;
    }
    g_fb = &fb;

    TouchInput touch;
    if (touch_init(&touch, TOUCH_DEVICE) < 0) {
        fprintf(stderr, "control_panel: failed to init touch input\n");
        fb_close(&fb);
        return 1;
    }
    g_touch = &touch;

    AppState state;
    memset(&state, 0, sizeof(state));
    g_state = &state;
    config_init(&state.cfg);
    config_load(&state.cfg);

    /* state.page is NULL: the home grid is the startup view. */
    for (int i = 0; i < HOME_PAGE_COUNT; i++)
        home_pages[i]->load(&state.cfg);

    rebuild_ui(&state);
    home_load_icons();   /* the home grid is the startup view */

    bool needs_redraw = true;  /* first frame always draws */

    while (running) {
        uint32_t now = get_time_ms();

        /* Status message timeout — visual change, and exactly one repaint: the
         * clear is what makes the test false on every later iteration. */
        uint32_t hold = state.status_hold_ms ? state.status_hold_ms : STATUS_HOLD_MS;
        if (state.status_msg[0] && now - state.status_time_ms > hold) {
            state.status_msg[0] = '\0';
            state.status_hold_ms = 0;
            needs_redraw = true;
        }

        if (state.page && state.page_fullscreen) {
            run_current_fullscreen_mode(&fb, &touch, &state);
            needs_redraw = true;  /* redraw after returning from fullscreen */
            continue;
        }

        /* --- Render only when visual state changed --- */
        if (needs_redraw) {
            fb_clear(&fb, COLOR_BG);
            if (state.page) {
                draw_page_bar(&fb, &state);
                state.page->draw(&fb);
            } else {
                draw_home(&fb, &state);
            }

            /* Draw confirmation dialog overlay on top of everything */
            if (state.confirm_action != CONFIRM_NONE) {
                modal_dialog_draw(&confirm_dialog, &fb);
            }

            fb_swap(&fb);
            needs_redraw = false;
        }

        /* --- Save visual state before input handling --- */
        const CpPage *prev_page      = state.page;
        char          prev_status0   = state.status_msg[0];
        /* A new message replacing one still shown keeps status_msg[0] non-zero,
         * so the time it was set is what says the text changed. */
        uint32_t      prev_status_t  = state.status_time_ms;
        int           prev_home_page = state.home_page;
        ConfirmAction prev_confirm   = state.confirm_action;

        touch_poll(&touch);
        TouchState ts = touch_get_state(&touch);
        int tx = ts.x, ty = ts.y;
        bool touching = ts.pressed || ts.held;

        /* When confirmation dialog is active, only handle dialog input */
        if (state.confirm_action != CONFIRM_NONE) {
            ModalDialogAction action =
                modal_dialog_update(&confirm_dialog, tx, ty, touching, now);
            if (action == MODAL_ACTION_BTN0) {
                /* Cleared first: on_ok may itself open another question. */
                CpConfirmFn on_ok = confirm_on_ok;
                confirm_on_ok = NULL;
                state.confirm_action = CONFIRM_NONE;
                if (on_ok) on_ok(&state.cfg);
                state.page_dirty = true;
            } else if (action == MODAL_ACTION_BTN1) {
                confirm_on_ok = NULL;
                state.confirm_action = CONFIRM_NONE;
            }
        } else if (!state.page) {
            handle_home_input(&state, &ts);
        } else {
            handle_page_bar_input(&state, tx, ty, touching, now);
            /* BACK may just have closed the page; otherwise it has the input.
             * A page's visual state is its own, so it says when it changed; a
             * queued full-screen run repaints too. */
            if (state.page) {
                CpPageResult r = state.page->input(&state.cfg, tx, ty,
                                                   touching, now);
                if (r == CP_PAGE_FULLSCREEN) state.page_fullscreen = true;
                if (r != CP_PAGE_IDLE)       state.page_dirty = true;
            }
        }

        /* --- Detect visual state changes after input ---
         * ⚠️ Only a change repaints: a static screen must cost nothing (the
         * dirty-flag rule, ../CLAUDE.md → Rendering).  A live view asks for its
         * own repaint on a timer — a CpPage by returning CP_PAGE_REDRAW — and
         * never by adding an always-true term here: one such term (set at
         * startup, never cleared) once repainted every iteration, ~40 % CPU
         * sitting on the static home grid.  The touch edges are the widgets'
         * pressed/released feedback, which lives in each Button, not in
         * AppState; button_take_dirty() is the rest of it — a button whose look
         * moved with no edge here (a slide-off while held, a release consumed
         * by a full-screen run).  It is not always-true: it reports a change
         * of visual_state, and settled buttons change nothing.  Read into a
         * local first so the || chain cannot short-circuit past the clear. */
        bool btn_look = button_take_dirty();
        if (ts.pressed || ts.released || btn_look    ||
            prev_page      != state.page            ||
            prev_status0   != state.status_msg[0]   ||
            prev_status_t  != state.status_time_ms  ||
            prev_home_page != state.home_page       ||
            prev_confirm   != state.confirm_action  ||
            state.page_dirty) {
            needs_redraw = true;
        }
        state.page_dirty = false;

        /* Adaptive sleep: faster polling when a redraw is pending, or while the
         * open page says it has something to service every frame — the Audio
         * page's bus, pumped from its input(), which FRAME_DELAY_IDLE_US would
         * starve.  busy() reports live state (cp_page.h), so a static page
         * still idles at the cheap rate. */
        bool page_busy = state.page && state.page->busy && state.page->busy();
        usleep((needs_redraw || page_busy)
               ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US);
    }

    /* A page open at exit cleans up its own hardware (the Audio page's bus). */
    if (state.page && state.page->leave)
        state.page->leave();
    hw_leds_off();
    hw_reload_config();
    hw_set_backlight(100);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    touch_close(&touch);
    fb_close(&fb);
    return 0;
}
