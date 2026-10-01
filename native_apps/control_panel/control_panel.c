/**
 * Control Panel — Unified Hardware App for RoomWizard
 *
 * Opens on an icon grid (the home view); each tile opens a page:
 *   Audio        — enable, music/effects, output device, the TEST chime and
 *                  the MIX BUS TEST launch (audio_page.c); grid-only
 *   Display      — backlight, orientation, what is visible and touchable,
 *                  SCREEN EDGES and the display tests (display_page.c);
 *                  grid-only
 *   LED          — enable, brightness and the LED tests (led_page.c); a
 *                  grid-only page with no tab of its own
 *   Monitor      — live uptime, load, memory and storage (monitor_page.c);
 *                  grid-only too
 *   Information  — what this unit is (info_page.c); grid-only
 *   Network      — gateway, DNS and every interface (network_page.c); grid-only
 *   USB          — the bus list and RESCAN (usb_page.c); grid-only
 *   Input        — the keyboard/mouse/pad testers, on any bus (input_page.c);
 *                  grid-only
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
#include "../common/ui_layout.h"
#include "../common/icon_grid.h"
#include "cp_ui.h"
#include "cp_page.h"

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

#define COLOR_TAB_BG         RGB(30, 30, 45)
#define COLOR_TAB_ACTIVE     RGB(50, 50, 70)
#define COLOR_TAB_INACTIVE   RGB(35, 35, 50)
#define COLOR_SECTION_LINE   RGB(60, 60, 80)
#define COLOR_HEADER_TEXT    COLOR_CYAN
#define COLOR_DATA           COLOR_WHITE
#define COLOR_BAR_BG         RGB(40, 40, 40)
#define COLOR_BAR_FILL       RGB(0, 180, 60)
#define COLOR_BAR_WARN       COLOR_YELLOW
#define COLOR_BAR_CRIT       COLOR_RED

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Layout Constants
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/* TAB_BAR_H, CONTENT_*, BAR_WIDTH/BAR_HEIGHT and COLOR_LABEL live in cp_ui.h,
 * which the page modules share. */

#define TAB_BTN_W         150
#define TAB_BTN_H         40
#define TAB_BTN_SPACING   4
#define BACK_BTN_W        55

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
    TAB_TESTS,
    TAB_COUNT,
    /* Views with no tab button go after TAB_COUNT: every loop over the tab bar
     * stops there, so none of these can index tab_names[] or tab_buttons[]. */
    TAB_HOME,    /* the icon grid */
    TAB_PAGE     /* a CpPage module (cp_page.h), AppState.page says which —
                    reached from its home tile only; the tab bar shows BACK
                    and the page's name */
} ActiveTab;

typedef enum {
    TEST_MENU_VIEW,
    TEST_RUNNING
} TestSubState;

typedef enum {
    CONFIRM_NONE,
    CONFIRM_PAGE             /* a page's cp_confirm(): its on_ok runs on OK */
} ConfirmAction;

/* Indexed only below TAB_COUNT — TAB_HOME and TAB_PAGE have no tab button. */
static const char *tab_names[TAB_COUNT] = { "TESTS" };

/* The home grid, and the page registry: a row with a page opens that CpPage,
 * and takes its label and icon from it (one name, one home); every page named
 * here is loaded, laid out and reset through it.  The other rows still open the
 * tab that carries their settings, and move to a page of their own one icon at
 * a time, deleting the duplicate as each lands.
 * Bluetooth has no page yet, so no tile. icon NULL = the grid's letter tile. */
typedef struct {
    const char   *label;       /* NULL when page is set */
    const char   *icon;        /* basename under /opt/roomwizard/icons/, no .ppm */
    ActiveTab     tab;         /* TAB_PAGE when page is set */
    const CpPage *page;
} HomeItem;

static const HomeItem home_items[] = {
    { .tab = TAB_PAGE, .page = &cp_audio_page },
    { .tab = TAB_PAGE, .page = &cp_display_page },
    { .tab = TAB_PAGE, .page = &cp_led_page },
    { .tab = TAB_PAGE, .page = &cp_usb_page },
    { .tab = TAB_PAGE, .page = &cp_input_page },
    { .tab = TAB_PAGE, .page = &cp_network_page },
    { .tab = TAB_PAGE, .page = &cp_monitor_page },
    { .tab = TAB_PAGE, .page = &cp_info_page },
};
#define HOME_ITEM_COUNT ((int)(sizeof(home_items) / sizeof(home_items[0])))
#define HOME_TITLE_H    50

/* The Tests tab: name and routine in one row, and the count derived from the
 * table, so the button pressed and the routine run cannot drift the way a name
 * list beside a bare-index switch could.  It is empty, and no home tile opens
 * the tab: the LED tests are on the LED page (led_page.c), the backlight ramp
 * and the test patterns on the Display page (display_page.c), the multi-touch
 * test on the Input page (input_page.c). */
static const struct {
    const char *name;
    void      (*run)(Framebuffer *, TouchInput *);
} tests[] = {
};
#define NUM_TESTS ((int)(sizeof(tests) / sizeof(tests[0])))

typedef struct {
    ActiveTab     active_tab;
    const CpPage *page;               /* the open page when active_tab == TAB_PAGE */
    bool          page_dirty;         /* the page asked to be repainted */
    bool          page_fullscreen;    /* its input() queued a full-screen run */
    char          status_msg[64];
    uint32_t      status_time_ms;
    uint32_t      status_hold_ms;     /* how long it shows; 0 = STATUS_HOLD_MS */
    bool          status_ok;          /* a page's message: success or failure colour */
    int           home_page;         /* page of the home grid */
    TestSubState  test_sub;
    int           test_selected;
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

static Button tab_buttons[TAB_COUNT];
static Button back_btn;          /* the tab bar's BACK to the home grid */

/* Tests */
static UILayout test_layout;
static Button test_buttons[NUM_TESTS];

/* The panel's one confirmation dialog: every page's cp_confirm() (cp_page.h)
 * opens this same instance.  While
 * confirm_action is not CONFIRM_NONE main() draws it over everything and routes
 * all input to it — the tab bar and the page included, so BACK under the
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
 * Tab Bar
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static void create_tab_bar(void) {
    int tab_y = SCREEN_SAFE_TOP + 2;

    /* Dynamic tab width: fit all tabs + exit button within safe area */
    int back_total = BACK_BTN_W + 20;           /* back button + margins */
    int tab_area_w = SCREEN_SAFE_WIDTH - back_total - 20; /* 10px left + 10px gap */
    int num_tabs = TAB_COUNT;
    int tab_w = (tab_area_w - (num_tabs - 1) * TAB_BTN_SPACING) / num_tabs;
    if (tab_w > TAB_BTN_W) tab_w = TAB_BTN_W;  /* cap at original max */
    if (tab_w < 60) tab_w = 60;                 /* minimum usable width */

    /* Use abbreviated labels when tabs are narrow */
    static const char *short_labels[] = { "TEST" };
    const char **labels = (tab_w < 120) ? short_labels : tab_names;

    for (int i = 0; i < TAB_COUNT; i++) {
        int tab_x = SCREEN_SAFE_LEFT + back_total + i * (tab_w + TAB_BTN_SPACING);
        button_init_full(&tab_buttons[i], tab_x, tab_y,
                         tab_w, TAB_BTN_H, labels[i],
                         COLOR_TAB_INACTIVE, COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
    }
    /* BACK on the left, where the full-screen testers keep theirs: the top-right
     * corner is the home grid's exit X, and a double tap there must not both
     * leave the page and quit. */
    button_init_full(&back_btn,
                     SCREEN_SAFE_LEFT + 10, tab_y,
                     BACK_BTN_W, TAB_BTN_H, "<",
                     COLOR_TAB_INACTIVE, COLOR_WHITE,
                     BTN_HIGHLIGHT_COLOR, 3);
}

/* A page reached from its home tile only (past TAB_HOME) is a module of its
 * own: its bar carries BACK and the page's title, never the old tabs. */
static bool is_page(ActiveTab t) { return t > TAB_HOME; }

/* The title is the page's name, which is also its tile's label. */
static const char *page_title(const AppState *state) {
    return state->page ? state->page->name : "";
}

/* A tile's label and icon: the page's own when the row names one. */
static const char *home_label(const HomeItem *it) {
    return it->page ? it->page->name : it->label;
}
static const char *home_icon(const HomeItem *it) {
    return it->page ? it->page->icon : it->icon;
}

static void draw_tab_bar(Framebuffer *fb, AppState *state) {
    fb_fill_rect(fb, SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP,
                 SCREEN_SAFE_WIDTH, TAB_BAR_H, COLOR_TAB_BG);
    if (is_page(state->active_tab) && state->status_msg[0]) {
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
    } else if (is_page(state->active_tab)) {
        text_draw_centered(fb, fb->width / 2, back_btn.y + back_btn.height / 2,
                           page_title(state), COLOR_WHITE, 3);
    } else for (int i = 0; i < TAB_COUNT; i++) {
        tab_buttons[i].bg_color = (i == (int)state->active_tab)
                                  ? COLOR_TAB_ACTIVE : COLOR_TAB_INACTIVE;
        button_draw(fb, &tab_buttons[i]);
        if (i == (int)state->active_tab) {
            int bx = tab_buttons[i].x;
            int bw = tab_buttons[i].width;
            int by = tab_buttons[i].y + tab_buttons[i].height;
            fb_fill_rect(fb, bx, by - 3, bw, 3, COLOR_CYAN);
        }
    }
    button_draw(fb, &back_btn);
    fb_draw_line(fb, SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP + TAB_BAR_H,
                 SCREEN_SAFE_RIGHT, SCREEN_SAFE_TOP + TAB_BAR_H,
                 COLOR_SECTION_LINE);
}

/* The one place a view changes, so entering and leaving keep their side
 * effects whether the tab bar, the home grid or BACK asked. */
static void set_view(AppState *state, ActiveTab tab, const CpPage *page) {
    ActiveTab prev_tab = state->active_tab;
    const CpPage *prev_page = state->page;
    if (prev_page && prev_page != page && prev_page->leave)
        prev_page->leave();
    /* A message belongs to the view that posted it: the tabs and the page bar
     * draw the one status_msg, so a page's BACKUP line must not follow BACK
     * onto a tab.  The view change repaints anyway. */
    if (tab != prev_tab || page != prev_page) {
        state->status_msg[0]  = '\0';
        state->status_hold_ms = 0;
    }
    state->active_tab = tab;
    state->page = page;
    state->page_fullscreen = false;
    if (page && page != prev_page) {
        if (page->enter) page->enter();
        state->page_dirty = true;
    }
}

static void set_tab(AppState *state, ActiveTab tab) { set_view(state, tab, NULL); }
static void set_page(AppState *state, const CpPage *page) {
    set_view(state, TAB_PAGE, page);
}

static void handle_tab_bar_input(AppState *state, int tx, int ty,
                                 bool touching, uint32_t now) {
    if (!is_page(state->active_tab))
        for (int i = 0; i < TAB_COUNT; i++)
            if (button_update(&tab_buttons[i], tx, ty, touching, now))
                set_tab(state, (ActiveTab)i);
    if (button_update(&back_btn, tx, ty, touching, now))
        set_tab(state, TAB_HOME);
}

/* ── Home grid ─────────────────────────────────────────────────────────────── */

static IconGrid  home_grid;
static uint32_t *home_icons[HOME_ITEM_COUNT];
static int       home_press = -2;   /* tile index pressed, -1 = exit X, -2 = none */

static void home_load_icons(void) {
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        const char *icon = home_icon(&home_items[i]);
        if (!icon) continue;
        char path[128];
        snprintf(path, sizeof(path), "/opt/roomwizard/icons/%s.ppm", icon);
        home_icons[i] = icon_grid_load_icon(path);
    }
}

static int home_count_on_page(int page) {
    int n = HOME_ITEM_COUNT - page * home_grid.per_page;
    return n > home_grid.per_page ? home_grid.per_page : n;
}

static void draw_home(Framebuffer *fb, AppState *state) {
    int pages = icon_grid_pages(&home_grid, HOME_ITEM_COUNT);
    if (state->home_page >= pages) state->home_page = pages - 1;

    text_draw_centered(fb, fb->width / 2, SCREEN_SAFE_TOP + 14, "CONTROL PANEL",
                       COLOR_WHITE, 3);
    icon_grid_draw_exit(fb, &home_grid);

    int start = state->home_page * home_grid.per_page;
    int n = home_count_on_page(state->home_page);
    for (int i = 0; i < n; i++) {
        const HomeItem *it = &home_items[start + i];
        int x, y;
        icon_grid_tile_xy(&home_grid, i, &x, &y);
        icon_grid_draw_tile(fb, &home_grid, x, y, home_label(it), home_icons[start + i],
                            icon_grid_letter_color(home_label(it)), false);
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
                                                   icon_grid_pages(&home_grid, HOME_ITEM_COUNT));
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
    const HomeItem *it = &home_items[start + pressed];
    if (it->page) { set_page(state, it->page); return; }
    set_tab(state, it->tab);
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
    for (int i = 0; i < HOME_ITEM_COUNT; i++)
        if (home_items[i].page && home_items[i].page->reset_defaults)
            home_items[i].page->reset_defaults(cfg);
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
 * Tests Tab  (from hardware_test_gui.c)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static void create_tests_ui(void) {
    int test_cols, test_item_w;
    if (CONTENT_WIDTH < 600) {
        /* Portrait mode: 2 columns with items sized to fit */
        test_cols = 2;
        test_item_w = (CONTENT_WIDTH - 40 - 8) / 2;
    } else {
        /* Landscape mode: 5 columns */
        test_cols = 5;
        test_item_w = 140;
    }
    ui_layout_init_grid(&test_layout, CONTENT_WIDTH, CONTENT_H,
                        test_cols, test_item_w, 70, 8, 16, 10, 60, 10, 20);
    ui_layout_update(&test_layout, NUM_TESTS);
    for (int i = 0; i < NUM_TESTS; i++)
        button_init_full(&test_buttons[i], 0, 0, test_item_w, 70, tests[i].name,
                         RGB(34,34,34), COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);
}

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

/* â”€â”€ Test dispatch â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void run_test(Framebuffer *fb, TouchInput *touch, int test_id) {
    if (test_id >= 0 && test_id < NUM_TESTS)
        tests[test_id].run(fb, touch);
}

static void draw_test_menu(Framebuffer *fb, AppState *state) {
    text_draw_centered(fb, CONTENT_LEFT + CONTENT_WIDTH / 2,
                       CONTENT_Y + 20, "HARDWARE TESTS", COLOR_WHITE, 3);
    for (int i = 0; i < NUM_TESTS; i++) {
        int x, y, w, h;
        if (ui_layout_get_item_position(&test_layout, i, &x, &y, &w, &h)) {
            test_buttons[i].x = CONTENT_LEFT + x;
            test_buttons[i].y = CONTENT_Y + y;
            test_buttons[i].width = w;
            test_buttons[i].height = h;
            test_buttons[i].visual_state = (i == state->test_selected)
                ? BTN_STATE_HIGHLIGHTED : BTN_STATE_NORMAL;
            button_draw(fb, &test_buttons[i]);
        }
    }
}

static void handle_test_menu_input(AppState *state, int tx, int ty,
                                   bool touching, uint32_t now) {
    (void)now;
    if (!touching) return;
    int lx = tx - CONTENT_LEFT;
    int ly = ty - CONTENT_Y;
    int item = ui_layout_get_item_at_position(&test_layout, lx, ly);
    if (item >= 0 && item < NUM_TESTS) {
        state->test_selected = item;
        state->test_sub = TEST_RUNNING;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Screen geometry — why one wizard measures it
 * ══════════════════════════════════════════════════════════════════════════ */

/* Calibration and edge measurement are landscape-only; the portrait toggle that
 * makes them refuse is on the Display page (display_page.c).
 *
 * Geometry is measured by ONE wizard (run_calib_wizard) that writes both lines
 * of /etc/touch_calibration.conf. It replaced two separate flows that could be
 * — and were — measured against contradictory assumptions:
 *
 *   - a 9-tap calibration whose crosshairs sat 40 px in, inside the band where
 *     raw compresses, so the fit slope came out shallow and invented a
 *     horizontal inset that does not exist; and
 *   - a bezel adjuster that drew its reference frame on the *logical* edge,
 *     i.e. measured the bezel through the bezel.
 *
 * The wizard fixes both: it fits from interior targets only, and it runs with
 * the bezel zeroed so a drawn pixel is a panel pixel. The fit itself lives in
 * common/touch_calib.c, shared with the touch_raw diagnostic. */

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
 * Calibration wizard  (full screen)
 * ══════════════════════════════════════════════════════════════════════════ */

/* Runs on the raw panel: fb_set_bezel(0,0,0,0), so a drawn pixel is a panel
 * pixel — the premise that made the touch_raw diagnostic trustworthy. Both
 * config lines are measured against it, which is what stops them contradicting
 * each other.
 *
 * Nothing is written until the operator has (a) accepted a fit that passed the
 * sanity gate and (b) confirmed, live on the new mapping, that the screen still
 * responds. Any timeout at any step puts everything back. */

typedef enum {
    WIZ_TAP,        /* tap the interior targets                     */
    WIZ_CHECK,      /* review the fit; accept / redo / reset        */
    WIZ_REACH,      /* sweep the four edges: what raw do they emit? */
    WIZ_EDGES,      /* measure the bezel against 2 px ladders       */
    WIZ_REPORT,     /* visible vs touchable                         */
    WIZ_CONFIRM,    /* live on the new mapping; keep or auto-revert */
    WIZ_EXIT
} WizStep;

#define WIZ_MAX_TARGETS  16          /* TOUCH_CALIB_N_TARGETS is 11 */
#define WIZ_TAP_RADIUS   120         /* a tap further off was not aimed at the target */
#define WIZ_IDLE_MS      60000       /* abandoned wizard reverts and exits */
#define WIZ_CONFIRM_MS   20000       /* "does it still work?" countdown */
#define WIZ_LADDER       44          /* depth of the 2 px edge ladders */
#define WIZ_BEZ_MAX      64          /* a margin larger than this is a misconfiguration */

/* WIZ_REACH treats a finger anywhere in the outer sixth of an axis as a sample
 * for that edge. Its own buttons therefore have to sit outside all four bands, on
 * BOTH axes — one button row serves edges that run horizontally and vertically.
 * The touch_raw diagnostic solved this at the same row. */
#define WIZ_SWEEP_BAND_DIV 6

/* -- bezel stepper: geometry and drawing kept apart, because the input pass
 *    needs the hit rects on a frame where nothing is drawn ----------------- */

#define BEZ_BTN_W 56
#define BEZ_BTN_H 48
#define BEZ_VAL_W 56

typedef struct { int minus_x, plus_x, y; } BezStepper;

static BezStepper bez_stepper_geom(int cx, int cy) {
    BezStepper s;
    s.y       = cy - BEZ_BTN_H / 2;
    s.minus_x = cx - BEZ_VAL_W / 2 - BEZ_BTN_W;
    s.plus_x  = cx + BEZ_VAL_W / 2;
    return s;
}

/* Draw "LABEL / [-] value [+]" centred on (cx, cy). */
static void draw_bez_stepper(Framebuffer *fb, int cx, int cy,
                             const char *label, int value) {
    BezStepper s = bez_stepper_geom(cx, cy);

    text_draw_centered(fb, cx, s.y - 22, label, COLOR_LABEL, 2);

    fb_fill_rect(fb, s.minus_x, s.y, BEZ_BTN_W, BEZ_BTN_H, RGB(60, 60, 90));
    fb_draw_rect(fb, s.minus_x, s.y, BEZ_BTN_W, BEZ_BTN_H, COLOR_WHITE);
    text_draw_centered(fb, s.minus_x + BEZ_BTN_W / 2, s.y + BEZ_BTN_H / 2,
                       "-", COLOR_WHITE, 3);

    fb_fill_rect(fb, s.plus_x, s.y, BEZ_BTN_W, BEZ_BTN_H, RGB(60, 60, 90));
    fb_draw_rect(fb, s.plus_x, s.y, BEZ_BTN_W, BEZ_BTN_H, COLOR_WHITE);
    text_draw_centered(fb, s.plus_x + BEZ_BTN_W / 2, s.y + BEZ_BTN_H / 2,
                       "+", COLOR_WHITE, 3);

    char v[8];
    snprintf(v, sizeof(v), "%d", value);
    text_draw_centered(fb, cx, s.y + BEZ_BTN_H / 2, v, COLOR_DATA, 3);
}

/* Where the four steppers sit, in panel coordinates. One function so the input
 * and render passes cannot drift apart. */
static void wiz_stepper_positions(int W, int H, int *cx, int *cy) {
    cx[0] = W / 2;       cy[0] = H / 2 - 110;   /* TOP    */
    cx[1] = W / 2;       cy[1] = H / 2 + 110;   /* BOTTOM */
    cx[2] = W / 2 - 190; cy[2] = H / 2;         /* LEFT   */
    cx[3] = W / 2 + 190; cy[3] = H / 2;         /* RIGHT  */
}

/* -- wizard buttons ------------------------------------------------------- */

typedef struct { int x, y, w, h; const char *label; uint32_t col; } WizBtn;

static void wiz_draw_btn(Framebuffer *fb, const WizBtn *b) {
    fb_fill_rounded_rect(fb, b->x, b->y, b->w, b->h, 8, b->col);
    fb_draw_rounded_rect(fb, b->x, b->y, b->w, b->h, 8, COLOR_WHITE);
    int tw = text_measure_width(b->label, 2);
    fb_draw_text(fb, b->x + (b->w - tw) / 2, b->y + b->h / 2 - 8, b->label, COLOR_WHITE, 2);
}

static bool wiz_hit(const WizBtn *b, int x, int y) {
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

/* Numbered 2 px ladder along one panel edge, plus the band the current margin
 * would hide. The operator does not count rungs — they raise the margin until
 * the yellow line clears the plastic. The ladder is there so "just clear"
 * becomes a number they can read back and sanity-check. */
static void wiz_draw_edge_ladder(Framebuffer *fb, int edge, int margin) {
    const int W = (int)fb->width, H = (int)fb->height;
    const uint32_t band = RGB(70, 0, 0), fine = RGB(70, 70, 95),
                   coarse = RGB(130, 130, 170), line = COLOR_YELLOW;
    char b[8];

    if (edge == 0 || edge == 1) {                       /* TOP / BOTTOM */
        int sgn  = (edge == 0) ? 1 : -1;
        int base = (edge == 0) ? 0 : H - 1;
        if (margin > 0)
            fb_fill_rect(fb, 0, (edge == 0) ? 0 : H - margin, W, margin, band);
        for (int d = 0; d <= WIZ_LADDER; d += 2) {
            int y = base + sgn * d;
            bool lab = (d % 10 == 0);
            fb_draw_line(fb, 0, y, lab ? 120 : 60, y, lab ? coarse : fine);
            fb_draw_line(fb, W - 1 - (lab ? 120 : 60), y, W - 1, y, lab ? coarse : fine);
            if (lab) {
                snprintf(b, sizeof(b), "%d", d);
                fb_draw_text(fb, 126, y - 3, b, coarse, 1);
                fb_draw_text(fb, W - 146, y - 3, b, coarse, 1);
            }
        }
        fb_draw_line(fb, 0, base + sgn * margin, W - 1, base + sgn * margin, line);
    } else {                                            /* LEFT / RIGHT */
        int sgn  = (edge == 2) ? 1 : -1;
        int base = (edge == 2) ? 0 : W - 1;
        if (margin > 0)
            fb_fill_rect(fb, (edge == 2) ? 0 : W - margin, 0, margin, H, band);
        for (int d = 0; d <= WIZ_LADDER; d += 2) {
            int x = base + sgn * d;
            bool lab = (d % 10 == 0);
            fb_draw_line(fb, x, 0, x, lab ? 90 : 45, lab ? coarse : fine);
            fb_draw_line(fb, x, H - 1 - (lab ? 90 : 45), x, H - 1, lab ? coarse : fine);
            if (lab) {
                snprintf(b, sizeof(b), "%d", d);
                fb_draw_text(fb, x - 3, 96, b, coarse, 1);
            }
        }
        fb_draw_line(fb, base + sgn * margin, 0, base + sgn * margin, H - 1, line);
    }
}

static void wiz_draw_target(Framebuffer *fb, int x, int y, uint32_t c) {
    fb_draw_circle(fb, x, y, 22, c);
    fb_draw_circle(fb, x, y, 21, c);
    fb_draw_circle(fb, x, y, 8, c);
    fb_draw_line(fb, x - 34, y, x + 34, y, c);
    fb_draw_line(fb, x, y - 34, x, y + 34, c);
    fb_fill_circle(fb, x, y, 2, c);
}

static void run_calib_wizard(Framebuffer *fb, TouchInput *touch,
                             bool edges_only) {
    /* fb_init() rotates the margins into virtual space in portrait, so values
     * measured here would be saved rotated and re-rotated on the next start.
     * Calibration is landscape-only by design. */
    if (fb->portrait_mode) {
        fb_clear(fb, COLOR_BLACK);
        text_draw_centered(fb, (int)fb->width / 2, (int)fb->height / 2 - 20,
                           "CALIBRATE IN LANDSCAPE MODE", COLOR_YELLOW, 3);
        text_draw_centered(fb, (int)fb->width / 2, (int)fb->height / 2 + 20,
                           "TURN PORTRAIT OFF AND RELAUNCH", COLOR_YELLOW, 2);
        fb_swap(fb);
        sleep(3);
        return;
    }

    /* Everything needed to put the device back exactly as it was. */
    const int entry_rx0 = touch->raw_min_x, entry_rx1 = touch->raw_max_x;
    const int entry_ry0 = touch->raw_min_y, entry_ry1 = touch->raw_max_y;
    const int entry_kxl = touch->raw_knot_lo_x, entry_kxh = touch->raw_knot_hi_x;
    const int entry_kyl = touch->raw_knot_lo_y, entry_kyh = touch->raw_knot_hi_y;
    const int entry_gx0 = touch->reach_min_x, entry_gx1 = touch->reach_max_x;
    const int entry_gy0 = touch->reach_min_y, entry_gy1 = touch->reach_max_y;
    const int entry_bt = screen_bezel_top,  entry_bb = screen_bezel_bottom;
    const int entry_bl = screen_bezel_left, entry_br = screen_bezel_right;

    int hw_x0, hw_x1, hw_y0, hw_y1;
    touch_calib_hw_range(touch, &hw_x0, &hw_x1, &hw_y0, &hw_y1);

    if (fb_set_bezel(fb, 0, 0, 0, 0) < 0) {
        return;
    }
    touch_set_screen_size(touch, (int)fb->width, (int)fb->height);

    const int W = (int)fb->width, H = (int)fb->height;

    /* NOTE which mapping is in force. Throughout TAP, CHECK, EDGES and REPORT
     * the *entry* calibration stays installed, so every button on those screens
     * is hit-tested through a mapping already known to work. The fitted range
     * goes live only at WIZ_CONFIRM, behind a countdown. The old flow did the
     * opposite — it hit-tested ACCEPT/REDO through the new fit, so a bad fit
     * left neither of them pressable. */

    int tap_rx[WIZ_MAX_TARGETS][TOUCH_CALIB_TAPS];
    int tap_ry[WIZ_MAX_TARGETS][TOUCH_CALIB_TAPS];
    int med_rx[WIZ_MAX_TARGETS], med_ry[WIZ_MAX_TARGETS];
    memset(tap_rx, 0, sizeof(tap_rx));
    memset(tap_ry, 0, sizeof(tap_ry));
    memset(med_rx, 0, sizeof(med_rx));
    memset(med_ry, 0, sizeof(med_ry));

    TouchAxisFit fx, fy;
    TouchAxisCurve cvx, cvy;
    memset(&fx, 0, sizeof(fx));
    memset(&fy, 0, sizeof(fy));
    memset(&cvx, 0, sizeof(cvx));
    memset(&cvy, 0, sizeof(cvy));
    char verdict_x[128] = "", verdict_y[128] = "";
    bool reach_x = false, reach_y = false, fit_sane = false;

    /* Working values: the curve that will be written — endpoints plus the two
     * interior knots per axis. Start from what is in force, and on the full path
     * let the fit replace them at ACCEPT. */
    int new_rx0 = entry_rx0, new_rx1 = entry_rx1;
    int new_ry0 = entry_ry0, new_ry1 = entry_ry1;
    int new_kxl = entry_kxl, new_kxh = entry_kxh;
    int new_kyl = entry_kyl, new_kyh = entry_kyh;
    int bez_t = entry_bt, bez_b = entry_bb, bez_l = entry_bl, bez_r = entry_br;

    /* Measured edge reach: what raw the four physical edges actually emit. This
     * is what separates "the fit extrapolates past raw 4095" from "the sensor
     * stops responding 30 px before the edge" — the two look identical from a
     * bezel press, and confusing them is what kept the endpoint bug alive across
     * three sessions. Starts optimistic (every edge reaches the hardware limit)
     * and is replaced by WIZ_REACH's sweep. */
    TouchCalibSweep sweep[4];
    for (int e = 0; e < 4; e++) touch_calib_sweep_reset(&sweep[e], e);
    int new_gx0 = entry_gx0, new_gx1 = entry_gx1;
    int new_gy0 = entry_gy0, new_gy1 = entry_gy1;

    WizStep step = edges_only ? WIZ_EDGES : WIZ_TAP;
    int tgt_i = 0, tap_i = 0;
    bool saved = false;
    char msg[64] = "";   /* sized to AppState::status_msg, which it is copied into */

    uint32_t last_action = get_time_ms();
    uint32_t confirm_start = 0;

    /* The bottom row stops at y=440, clear of the ~449 panel row where the
     * digitiser stops on this hardware — a control the sensor cannot reach is
     * exactly the failure this wizard exists to prevent. */
    WizBtn b_cancel = { 40,  384, 150, 56, "CANCEL", RGB(110, 40, 40) };
    WizBtn b_redo   = { 210, 384, 150, 56, "REDO",   RGB(110, 80, 20) };
    WizBtn b_reset  = { 380, 384, 150, 56, "RESET",  RGB(90, 60, 110) };
    WizBtn b_next   = { 600, 384, 160, 56, "ACCEPT", RGB(30, 110, 60) };
    /* TAP owns the bottom row (a target sits at y=458), so its abort is inset
     * and placed further than WIZ_TAP_RADIUS from every target. */
    WizBtn b_abort  = { 700, 400, 90,  44, "STOP",   RGB(110, 40, 40) };
    /* REACH cannot use the row above: y=384..440 is inside the BOTTOM sweep band
     * (y > H*5/6 == 400), so pressing CANCEL would record its own tap as a bottom
     * edge extreme. This row sits clear of all four bands on both axes —
     * x in [133,666), y in [80,400). */
    WizBtn b_sw_cancel = { 200, 258, 130, 54, "CANCEL", RGB(110, 40, 40) };
    WizBtn b_sw_redo   = { 340, 258, 130, 54, "REDO",   RGB(110, 80, 20) };
    WizBtn b_sw_next   = { 480, 258, 130, 54, "NEXT",   RGB(30, 110, 60) };

    touch_drain_events(touch);

    while (running && step != WIZ_EXIT) {
        uint32_t now = get_time_ms();
        touch_poll(touch);
        TouchState st = touch_get_state(touch);
        const int raw_x = touch->last_x, raw_y = touch->last_y;
        bool press = st.pressed && (now - last_action) > BTN_DEBOUNCE_MS;

        /* Abandoned at any step: put everything back rather than leave a
         * half-applied geometry on a wall-mounted screen. */
        uint32_t idle_limit = (step == WIZ_CONFIRM) ? WIZ_CONFIRM_MS : WIZ_IDLE_MS;
        if (now - last_action > idle_limit) {
            snprintf(msg, sizeof(msg), "TIMED OUT - NOTHING CHANGED");
            break;
        }

        /* ------------------------- input ------------------------- */
        if (step == WIZ_TAP) {
            if (press && wiz_hit(&b_abort, st.x, st.y)) {
                break;
            } else if (press) {
                int dx = st.x - TOUCH_CALIB_TARGETS[tgt_i].px;
                int dy = st.y - TOUCH_CALIB_TARGETS[tgt_i].py;
                if (dx * dx + dy * dy <= WIZ_TAP_RADIUS * WIZ_TAP_RADIUS) {
                    /* Record RAW. The installed mapping is irrelevant to the
                     * data, which is what lets the old one stay in force. */
                    tap_rx[tgt_i][tap_i] = raw_x;
                    tap_ry[tgt_i][tap_i] = raw_y;
                    last_action = now;
                    if (++tap_i >= TOUCH_CALIB_TAPS) {
                        med_rx[tgt_i] = touch_calib_median3(tap_rx[tgt_i][0],
                                                            tap_rx[tgt_i][1],
                                                            tap_rx[tgt_i][2]);
                        med_ry[tgt_i] = touch_calib_median3(tap_ry[tgt_i][0],
                                                            tap_ry[tgt_i][1],
                                                            tap_ry[tgt_i][2]);
                        tap_i = 0;
                        if (++tgt_i >= TOUCH_CALIB_N_TARGETS) {
                            /* Fit in PANEL coordinates from INTERIOR targets
                             * only. The interior restriction is the entire
                             * correction over the old 9-tap fit. */
                            int px[WIZ_MAX_TARGETS], py[WIZ_MAX_TARGETS];
                            bool ix[WIZ_MAX_TARGETS], iy[WIZ_MAX_TARGETS];
                            for (int i = 0; i < TOUCH_CALIB_N_TARGETS; i++) {
                                px[i] = TOUCH_CALIB_TARGETS[i].px;
                                py[i] = TOUCH_CALIB_TARGETS[i].py;
                                ix[i] = touch_calib_interior_x(px[i], W);
                                iy[i] = touch_calib_interior_y(py[i], H);
                            }
                            touch_calib_fit(&fx, med_rx, px, ix, TOUCH_CALIB_N_TARGETS, W);
                            touch_calib_fit(&fy, med_ry, py, iy, TOUCH_CALIB_N_TARGETS, H);
                            /* Turn each fitted line into the curve that gets
                             * written: knots on the line, endpoints AT the line.
                             * Endpoints outside 0..4095 are correct and are left
                             * alone — clamping them is what tilted the outer
                             * segments and made the cursor run ahead of the
                             * finger near the bottom edge. */
                            touch_calib_curve_from_fit(&fx, hw_x0, hw_x1, &cvx);
                            touch_calib_curve_from_fit(&fy, hw_y0, hw_y1, &cvy);
                            reach_x = touch_calib_axis_verdict(&cvx, hw_x0, hw_x1,
                                                "X", verdict_x, sizeof(verdict_x));
                            reach_y = touch_calib_axis_verdict(&cvy, hw_y0, hw_y1,
                                                "Y", verdict_y, sizeof(verdict_y));
                            fit_sane = fx.in_ok && fy.in_ok &&
                                touch_calib_range_sane(fx.in0, fx.in1, hw_x0, hw_x1) &&
                                touch_calib_range_sane(fy.in0, fy.in1, hw_y0, hw_y1);
                            step = WIZ_CHECK;
                        }
                    }
                }
            }
        } else if (step == WIZ_CHECK) {
            if (press) {
                last_action = now;
                if (wiz_hit(&b_cancel, st.x, st.y)) {
                    break;
                } else if (wiz_hit(&b_redo, st.x, st.y)) {
                    tgt_i = tap_i = 0;
                    step = WIZ_TAP;
                } else if (wiz_hit(&b_reset, st.x, st.y)) {
                    /* Fall back to what the hardware declares — a plain linear
                     * map over the emittable range. Imprecise in the middle, but
                     * always usable; the point is that there is a way out. Skip
                     * the edge sweep too: RESET is for getting out of here, and
                     * the hardware range is the right assumption to pair with a
                     * hardware-range map. */
                    new_rx0 = hw_x0; new_rx1 = hw_x1;
                    new_ry0 = hw_y0; new_ry1 = hw_y1;
                    new_kxl = new_kxh = 0;   /* no curve: plain linear map */
                    new_kyl = new_kyh = 0;
                    new_gx0 = hw_x0; new_gx1 = hw_x1;
                    new_gy0 = hw_y0; new_gy1 = hw_y1;
                    step = WIZ_EDGES;
                } else if (wiz_hit(&b_next, st.x, st.y) && fit_sane) {
                    new_rx0 = cvx.v0; new_rx1 = cvx.v1;
                    new_kxl = cvx.k_lo; new_kxh = cvx.k_hi;
                    new_ry0 = cvy.v0; new_ry1 = cvy.v1;
                    new_kyl = cvy.k_lo; new_kyh = cvy.k_hi;
                    step = WIZ_REACH;
                }
            }
        } else if (step == WIZ_REACH) {
            /* Accumulate while the finger is DOWN anywhere in the outer sixth of
             * an axis. Nothing is captured on lift: a sweep is the stroke itself,
             * and demanding a clean lift inside the band throws away the end of
             * every stroke. All four edges are live at once — a stroke along the
             * top cannot produce samples in the bottom band, so there is no need
             * to walk the operator through them one at a time. */
            if (st.held) {
                const int band_x = W / WIZ_SWEEP_BAND_DIV;
                const int band_y = H / WIZ_SWEEP_BAND_DIV;
                if (st.y < band_y)
                    touch_calib_sweep_add(&sweep[0], 0, raw_y, st.x, W, hw_y0);
                if (st.y >= H - band_y)
                    touch_calib_sweep_add(&sweep[1], 1, raw_y, st.x, W, hw_y1);
                if (st.x < band_x)
                    touch_calib_sweep_add(&sweep[2], 2, raw_x, st.y, H, hw_x0);
                if (st.x >= W - band_x)
                    touch_calib_sweep_add(&sweep[3], 3, raw_x, st.y, H, hw_x1);
            }
            if (press) {
                last_action = now;
                if (wiz_hit(&b_sw_cancel, st.x, st.y)) {
                    break;
                } else if (wiz_hit(&b_sw_redo, st.x, st.y)) {
                    for (int e = 0; e < 4; e++) touch_calib_sweep_reset(&sweep[e], e);
                } else if (wiz_hit(&b_sw_next, st.x, st.y)) {
                    /* An edge that was not swept falls back to the hardware limit
                     * — the optimistic assumption, which is what an unmeasured
                     * edge deserves. A swept edge that fell SHORT of the limit
                     * widens the reported dead band, which is the honest
                     * direction to be wrong in. */
                    new_gy0 = touch_calib_sweep_extreme_or(&sweep[0], 0, hw_y0);
                    new_gy1 = touch_calib_sweep_extreme_or(&sweep[1], 1, hw_y1);
                    new_gx0 = touch_calib_sweep_extreme_or(&sweep[2], 2, hw_x0);
                    new_gx1 = touch_calib_sweep_extreme_or(&sweep[3], 3, hw_x1);
                    step = WIZ_EDGES;
                }
            }
        } else if (step == WIZ_EDGES) {
            if (press) {
                last_action = now;
                if (wiz_hit(&b_cancel, st.x, st.y)) {
                    break;
                } else if (wiz_hit(&b_next, st.x, st.y)) {
                    step = WIZ_REPORT;
                } else {
                    /* 1 px per tap: the ladder is 2 px and every app's drawing
                     * surface derives from these four numbers, so they are
                     * worth getting right to the pixel. */
                    int cx[4], cy[4];
                    int *vals[4] = { &bez_t, &bez_b, &bez_l, &bez_r };
                    wiz_stepper_positions(W, H, cx, cy);
                    for (int i = 0; i < 4; i++) {
                        BezStepper s = bez_stepper_geom(cx[i], cy[i]);
                        if (st.y < s.y || st.y >= s.y + BEZ_BTN_H) continue;
                        if (st.x >= s.minus_x && st.x < s.minus_x + BEZ_BTN_W) {
                            if (*vals[i] > 0) (*vals[i])--;
                        } else if (st.x >= s.plus_x && st.x < s.plus_x + BEZ_BTN_W) {
                            if (*vals[i] < WIZ_BEZ_MAX) (*vals[i])++;
                        }
                    }
                }
            }
        } else if (step == WIZ_REPORT) {
            if (press) {
                last_action = now;
                if (wiz_hit(&b_cancel, st.x, st.y)) {
                    break;
                } else if (wiz_hit(&b_redo, st.x, st.y)) {
                    step = WIZ_EDGES;
                } else if (wiz_hit(&b_next, st.x, st.y)) {
                    /* Go live on the new geometry WITHOUT writing anything, and
                     * make the operator prove the screen still responds. The
                     * measured reach goes live too, so the trial has the same
                     * touch-safe inset the saved config would. */
                    touch_set_raw_curve(touch, new_rx0, new_kxl, new_kxh, new_rx1,
                                               new_ry0, new_kyl, new_kyh, new_ry1);
                    touch_set_edge_reach(touch, new_gx0, new_gx1, new_gy0, new_gy1);
                    fb_set_bezel(fb, bez_t, bez_b, bez_l, bez_r);
                    touch_set_screen_size(touch, (int)fb->width, (int)fb->height);
                    confirm_start = now;
                    step = WIZ_CONFIRM;
                }
            }
        } else if (step == WIZ_CONFIRM) {
            /* Geometry changed under us, so the button box is recomputed from
             * the live dimensions rather than the entry-time ones. */
            const int cw = (int)fb->width, ch = (int)fb->height;
            WizBtn keep = { cw / 2 - 150, ch / 2 + 10, 300, 70,
                            "KEEP THESE", RGB(30, 110, 60) };
            if (press && wiz_hit(&keep, st.x, st.y)) {
                char bak[256] = "";
                touch_calib_backup(CALIB_FILE, bak, sizeof(bak));
                touch->calib.bezel_top    = bez_t;
                touch->calib.bezel_bottom = bez_b;
                touch->calib.bezel_left   = bez_l;
                touch->calib.bezel_right  = bez_r;
                saved = (touch_save_calibration(touch, CALIB_FILE) == 0);
                snprintf(msg, sizeof(msg),
                         saved ? "SAVED - PREVIOUS CONFIG BACKED UP"
                               : "SAVE FAILED - RUN AS ROOT");
                step = WIZ_EXIT;
            }
        }
        if (step == WIZ_EXIT) break;

        /* ------------------------- render ------------------------- */
        fb_clear(fb, COLOR_BLACK);
        char b[96];

        if (step == WIZ_TAP) {
            fb_draw_text(fb, 20, 20, "TAP THE CENTRE OF EACH TARGET", COLOR_WHITE, 2);
            fb_draw_text(fb, 20, 44,
                         "TARGETS SIT WELL INSIDE THE EDGES ON PURPOSE - "
                         "RAW COMPRESSES NEAR THE BORDER", COLOR_GRAY, 1);
            snprintf(b, sizeof(b), "TARGET %d/%d   TAP %d/%d",
                     tgt_i + 1, TOUCH_CALIB_N_TARGETS, tap_i + 1, TOUCH_CALIB_TAPS);
            fb_draw_text(fb, 20, 62, b, COLOR_GREEN, 2);
            for (int i = tgt_i + 1; i < TOUCH_CALIB_N_TARGETS; i++)
                fb_draw_circle(fb, TOUCH_CALIB_TARGETS[i].px,
                               TOUCH_CALIB_TARGETS[i].py, 6, RGB(55, 55, 75));
            wiz_draw_btn(fb, &b_abort);
            /* Last, so nothing can bury the target that is being aimed at. */
            wiz_draw_target(fb, TOUCH_CALIB_TARGETS[tgt_i].px,
                            TOUCH_CALIB_TARGETS[tgt_i].py, COLOR_GREEN);

        } else if (step == WIZ_CHECK) {
            fb_draw_text(fb, 20, 18, "CALIBRATION RESULT", COLOR_WHITE, 3);
            snprintf(b, sizeof(b), "HARDWARE RAW  X[%d..%d]  Y[%d..%d]",
                     hw_x0, hw_x1, hw_y0, hw_y1);
            fb_draw_text(fb, 20, 50, b, COLOR_GRAY, 1);

            fb_draw_text(fb, 20, 76, "AXIS   CURVE  RAW AT 0 / 1-4 / 3-4 / MAX     REACHES",
                         COLOR_YELLOW, 1);
            int lo, hi;
            touch_calib_reach(&cvx, hw_x0, hw_x1, &lo, &hi);
            snprintf(b, sizeof(b), "X  %5d %5d %5d %5d      %4d ..%4d",
                     cvx.v0, cvx.k_lo, cvx.k_hi, cvx.v1, lo, hi);
            fb_draw_text(fb, 20, 94, b, COLOR_WHITE, 2);
            touch_calib_reach(&cvy, hw_y0, hw_y1, &lo, &hi);
            snprintf(b, sizeof(b), "Y  %5d %5d %5d %5d      %4d ..%4d",
                     cvy.v0, cvy.k_lo, cvy.k_hi, cvy.v1, lo, hi);
            fb_draw_text(fb, 20, 118, b, COLOR_WHITE, 2);

            /* Per axis, and about the FIT rather than the hardware: the sensor
             * reaches every edge, so what these lines report is how much edge
             * compression the outer segments had to bend around. */
            fb_draw_text(fb, 20, 150, verdict_x, reach_x ? COLOR_GREEN : COLOR_CYAN, 1);
            fb_draw_text(fb, 20, 166, verdict_y, reach_y ? COLOR_GREEN : COLOR_CYAN, 1);

            /* The edge probes never entered the fit, so their residual against
             * the fitted LINE is the only honest check on it. Large values here
             * are the edge compression itself, which the curve then corrects. */
            int r_xlo = touch_calib_predict_panel(med_rx[TOUCH_CALIB_PROBE_XLO],
                            fx.in0, fx.in1, W) - TOUCH_CALIB_TARGETS[TOUCH_CALIB_PROBE_XLO].px;
            int r_xhi = touch_calib_predict_panel(med_rx[TOUCH_CALIB_PROBE_XHI],
                            fx.in0, fx.in1, W) - TOUCH_CALIB_TARGETS[TOUCH_CALIB_PROBE_XHI].px;
            int r_ylo = touch_calib_predict_panel(med_ry[TOUCH_CALIB_PROBE_YLO],
                            fy.in0, fy.in1, H) - TOUCH_CALIB_TARGETS[TOUCH_CALIB_PROBE_YLO].py;
            int r_yhi = touch_calib_predict_panel(med_ry[TOUCH_CALIB_PROBE_YHI],
                            fy.in0, fy.in1, H) - TOUCH_CALIB_TARGETS[TOUCH_CALIB_PROBE_YHI].py;
            snprintf(b, sizeof(b), "EDGE-PROBE ERROR VS FITTED LINE   X %+d / %+d PX   Y %+d / %+d PX",
                     r_xlo, r_xhi, r_ylo, r_yhi);
            fb_draw_text(fb, 20, 190, b, COLOR_CYAN, 1);

            if (fit_sane)
                fb_draw_text(fb, 20, 214,
                             "ACCEPT KEEPS THIS FIT. NOTHING IS WRITTEN YET.",
                             COLOR_GRAY, 1);
            else
                fb_draw_text(fb, 20, 214,
                             "FIT REJECTED - IT BARELY OVERLAPS THE HARDWARE RANGE. "
                             "REDO, OR RESET.", COLOR_RED, 1);

            b_next.label = "ACCEPT";
            b_redo.label = "REDO";
            wiz_draw_btn(fb, &b_cancel);
            wiz_draw_btn(fb, &b_redo);
            wiz_draw_btn(fb, &b_reset);
            if (fit_sane) wiz_draw_btn(fb, &b_next);

        } else if (step == WIZ_REACH) {
            const int band_x = W / WIZ_SWEEP_BAND_DIV;
            const int band_y = H / WIZ_SWEEP_BAND_DIV;
            int all_done = 0;
            for (int e = 0; e < 4; e++) if (sweep[e].done) all_done++;

            /* Coverage cells laid ALONG each edge, so a gap in the stroke shows up
             * as a gap in the row rather than as a number that quietly never
             * arrives. Green = that stretch drove raw all the way to the limit. */
            for (int e = 0; e < 4; e++) {
                bool is_y     = touch_calib_sweep_edge_is_y(e);
                bool want_min = touch_calib_sweep_wants_min(e);
                int  limit    = is_y ? (want_min ? hw_y0 : hw_y1)
                                     : (want_min ? hw_x0 : hw_x1);
                for (int i = 0; i < TOUCH_CALIB_SWEEP_BUCKETS; i++) {
                    int cw2, ch2, cx2, cy2;
                    if (is_y) {
                        cw2 = W / TOUCH_CALIB_SWEEP_BUCKETS - 4;  ch2 = 14;
                        cx2 = i * (W / TOUCH_CALIB_SWEEP_BUCKETS) + 2;
                        cy2 = (e == 0) ? 6 : H - 20;
                    } else {
                        cw2 = 14;  ch2 = H / TOUCH_CALIB_SWEEP_BUCKETS - 4;
                        cy2 = i * (H / TOUCH_CALIB_SWEEP_BUCKETS) + 2;
                        cx2 = (e == 2) ? 6 : W - 20;
                    }
                    uint32_t col;
                    if (!sweep[e].bucket_hit[i])                     col = RGB(45, 45, 55);
                    else if (want_min ? (sweep[e].bucket[i] <= limit)
                                      : (sweep[e].bucket[i] >= limit)) col = COLOR_GREEN;
                    else                                             col = COLOR_ORANGE;
                    fb_fill_rect(fb, cx2, cy2, cw2, ch2, col);
                    fb_draw_rect(fb, cx2, cy2, cw2, ch2, RGB(90, 90, 110));
                }
            }
            /* The bands samples are taken from, so it is obvious where to slide. */
            fb_draw_line(fb, 0, band_y, W - 1, band_y, RGB(60, 60, 80));
            fb_draw_line(fb, 0, H - 1 - band_y, W - 1, H - 1 - band_y, RGB(60, 60, 80));
            fb_draw_line(fb, band_x, 0, band_x, H - 1, RGB(60, 60, 80));
            fb_draw_line(fb, W - 1 - band_x, 0, W - 1 - band_x, H - 1, RGB(60, 60, 80));

            fb_fill_rect(fb, 150, 76, W - 300, 170, RGB(10, 10, 14));
            fb_draw_rect(fb, 150, 76, W - 300, 170, RGB(60, 60, 80));
            fb_draw_text(fb, 164, 86, "SLIDE ONE FINGER ALONG EACH EDGE", COLOR_WHITE, 2);
            fb_draw_text(fb, 164, 108,
                         "THIS ASKS WHAT RAW THE PHYSICAL EDGE EMITS. A BEZEL PRESS "
                         "CANNOT TELL YOU:", COLOR_GRAY, 1);
            fb_draw_text(fb, 164, 122,
                         "IT READS THE SAME WHETHER THE SENSOR STOPS AT THE EDGE OR "
                         "30 PX INSIDE IT.", COLOR_GRAY, 1);

            int ry2 = 144;
            for (int e = 0; e < 4; e++) {
                bool is_y     = touch_calib_sweep_edge_is_y(e);
                bool want_min = touch_calib_sweep_wants_min(e);
                int  limit    = is_y ? (want_min ? hw_y0 : hw_y1)
                                     : (want_min ? hw_x0 : hw_x1);
                static const char *nm[4] = { "TOP", "BOTTOM", "LEFT", "RIGHT" };
                if (sweep[e].covered == 0)
                    snprintf(b, sizeof(b), "%-7s NOT SWEPT", nm[e]);
                else
                    snprintf(b, sizeof(b), "%-7s raw_%c %s %5d / %-5d  %2d/%d CELLS  %s",
                             nm[e], is_y ? 'y' : 'x', want_min ? "MIN" : "MAX",
                             sweep[e].extreme, limit,
                             sweep[e].covered, TOUCH_CALIB_SWEEP_BUCKETS,
                             touch_calib_sweep_reached(&sweep[e], e, limit)
                                 ? "REACHES" : "FALLS SHORT");
                fb_draw_text(fb, 164, ry2, b,
                             sweep[e].covered == 0 ? COLOR_GRAY
                             : touch_calib_sweep_reached(&sweep[e], e, limit)
                                 ? COLOR_GREEN : COLOR_ORANGE, 1);
                ry2 += 14;
            }
            snprintf(b, sizeof(b), "%d/4 EDGES SWEPT - %s", all_done,
                     all_done == 4 ? "PRESS NEXT"
                                   : "NEXT ASSUMES THE REST REACH THE LIMIT");
            fb_draw_text(fb, 164, ry2 + 6, b,
                         all_done == 4 ? COLOR_GREEN : COLOR_YELLOW, 1);

            b_sw_next.label = (all_done == 4) ? "NEXT" : "SKIP";
            wiz_draw_btn(fb, &b_sw_cancel);
            wiz_draw_btn(fb, &b_sw_redo);
            wiz_draw_btn(fb, &b_sw_next);

        } else if (step == WIZ_EDGES) {
            for (int e = 0; e < 4; e++) {
                int m = (e == 0) ? bez_t : (e == 1) ? bez_b : (e == 2) ? bez_l : bez_r;
                wiz_draw_edge_ladder(fb, e, m);
            }
            fb_draw_text(fb, 150, 58,
                         "RAISE EACH EDGE UNTIL ITS YELLOW LINE CLEARS THE PLASTIC",
                         COLOR_WHITE, 1);
            fb_draw_text(fb, 150, 74,
                         "THE DARK BAND IS WHAT GETS HIDDEN. THIS SCREEN IGNORES THE "
                         "CURRENT MARGINS, SO WHAT YOU SEE IS THE WHOLE PANEL.",
                         COLOR_GRAY, 1);

            int cx[4], cy[4];
            wiz_stepper_positions(W, H, cx, cy);
            draw_bez_stepper(fb, cx[0], cy[0], "TOP", bez_t);
            draw_bez_stepper(fb, cx[1], cy[1], "BOTTOM", bez_b);
            draw_bez_stepper(fb, cx[2], cy[2], "LEFT", bez_l);
            draw_bez_stepper(fb, cx[3], cy[3], "RIGHT", bez_r);
            snprintf(b, sizeof(b), "VISIBLE %dx%d",
                     W - bez_l - bez_r, H - bez_t - bez_b);
            text_draw_centered(fb, W / 2, H / 2, b, COLOR_DATA, 2);

            b_next.label = "NEXT";
            wiz_draw_btn(fb, &b_cancel);
            wiz_draw_btn(fb, &b_next);

        } else if (step == WIZ_REPORT) {
            fb_draw_text(fb, 20, 18, "VISIBLE VS TOUCHABLE", COLOR_WHITE, 3);

            TouchAxisCurve nx = { .v0 = new_rx0, .k_lo = new_kxl,
                                  .k_hi = new_kxh, .v1 = new_rx1,
                                  .overshoot_lo = 0, .overshoot_hi = 0, .dim = W };
            TouchAxisCurve ny = { .v0 = new_ry0, .k_lo = new_kyl,
                                  .k_hi = new_kyh, .v1 = new_ry1,
                                  .overshoot_lo = 0, .overshoot_hi = 0, .dim = H };

            /* Reach from the MEASURED edge extremes, not from the hardware limits:
             * if the sweep showed an edge never drives raw all the way, the band it
             * cannot address is wider than the curve alone implies. */
            int rx0, rx1, ry0, ry1;
            touch_calib_reach(&nx, new_gx0, new_gx1, &rx0, &rx1);
            touch_calib_reach(&ny, new_gy0, new_gy1, &ry0, &ry1);

            snprintf(b, sizeof(b), "VISIBLE    PANEL X %d..%d   Y %d..%d",
                     bez_l, W - 1 - bez_r, bez_t, H - 1 - bez_b);
            fb_draw_text(fb, 20, 58, b, COLOR_CYAN, 2);
            snprintf(b, sizeof(b), "TOUCHABLE  PANEL X %d..%d   Y %d..%d",
                     rx0, rx1, ry0, ry1);
            fb_draw_text(fb, 20, 84, b, COLOR_CYAN, 2);

            /* The inset: rows and columns you can SEE and DRAW ON but cannot
             * PRESS. On this hardware it is not zero and is not supposed to be —
             * the digitiser saturates before the panel edge. An earlier revision
             * demanded zero here and told the operator to REDO; it only ever read
             * zero because the endpoint clamp forced it to, and that clamp was the
             * bug. Amber only once the band is too wide to be the sensor. */
            int in_t, in_b, in_l, in_r;
            touch_calib_inset_from_reach(ry0, ry1, bez_t, H - bez_t - bez_b,
                                         &in_t, &in_b);
            touch_calib_inset_from_reach(rx0, rx1, bez_l, W - bez_l - bez_r,
                                         &in_l, &in_r);
            int worst = in_t;
            if (in_b > worst) worst = in_b;
            if (in_l > worst) worst = in_l;
            if (in_r > worst) worst = in_r;

            snprintf(b, sizeof(b),
                     "TOUCH-SAFE INSET   TOP %d  BOTTOM %d  LEFT %d  RIGHT %d",
                     in_t, in_b, in_l, in_r);
            fb_draw_text(fb, 20, 120, b,
                         worst > DISP_INSET_SUSPECT ? COLOR_ORANGE : COLOR_GREEN, 2);

            if (worst > DISP_INSET_SUSPECT)
                fb_draw_text(fb, 20, 148,
                             "THAT IS MORE THAN THIS PANEL SHOULD LOSE. RE-SWEEP THE "
                             "EDGES, OR REDO THE TAPS.", COLOR_ORANGE, 1);
            else if (worst > 0)
                fb_draw_text(fb, 20, 148,
                             "NORMAL - THE SENSOR SATURATES BEFORE THE EDGE. THE BAND "
                             "IS STILL DRAWABLE: USE IT FOR A STATUS OR SCORE ROW.",
                             COLOR_GRAY, 1);
            else
                fb_draw_text(fb, 20, 148,
                             "EVERY VISIBLE PIXEL CAN BE TOUCHED.", COLOR_GRAY, 1);

            snprintf(b, sizeof(b), "WILL WRITE   raw X %d %d %d %d",
                     new_rx0, new_kxl, new_kxh, new_rx1);
            fb_draw_text(fb, 20, 172, b, COLOR_WHITE, 1);
            snprintf(b, sizeof(b), "             raw Y %d %d %d %d   bezel %d %d %d %d",
                     new_ry0, new_kyl, new_kyh, new_ry1, bez_t, bez_b, bez_l, bez_r);
            fb_draw_text(fb, 20, 186, b, COLOR_WHITE, 1);
            snprintf(b, sizeof(b), "             reach X %d %d  Y %d %d",
                     new_gx0, new_gx1, new_gy0, new_gy1);
            fb_draw_text(fb, 20, 200, b, COLOR_WHITE, 1);
            fb_draw_text(fb, 20, 216,
                         "NEXT SWITCHES TO THE NEW MAPPING SO YOU CAN TRY IT BEFORE "
                         "ANYTHING IS SAVED.", COLOR_GRAY, 1);

            b_redo.label = "EDGES";
            b_next.label = "NEXT";
            wiz_draw_btn(fb, &b_cancel);
            wiz_draw_btn(fb, &b_redo);
            wiz_draw_btn(fb, &b_next);

        } else if (step == WIZ_CONFIRM) {
            const int cw = (int)fb->width, ch = (int)fb->height;
            uint32_t elapsed = now - confirm_start;
            int left = (elapsed >= WIZ_CONFIRM_MS)
                     ? 0 : (int)((WIZ_CONFIRM_MS - elapsed) / 1000);

            /* Frame on the logical edge: if any of it is under the plastic the
             * margins are still too small, which is the last thing worth
             * catching before this gets written. */
            fb_draw_rect(fb, 0, 0, cw, ch, COLOR_CYAN);
            fb_draw_rect(fb, 1, 1, cw - 2, ch - 2, COLOR_CYAN);

            text_draw_centered(fb, cw / 2, 50, "DOES THE NEW MAPPING WORK?",
                               COLOR_WHITE, 3);
            text_draw_centered(fb, cw / 2, 88,
                               "THE CYAN FRAME SHOULD BE FULLY VISIBLE", COLOR_GRAY, 2);
            text_draw_centered(fb, cw / 2, 114,
                               "IF YOU CANNOT PRESS KEEP, JUST WAIT - IT REVERTS BY ITSELF",
                               COLOR_GRAY, 1);
            snprintf(b, sizeof(b), "REVERTING IN %d", left);
            text_draw_centered(fb, cw / 2, 152, b,
                               left <= 5 ? COLOR_RED : COLOR_YELLOW, 3);

            WizBtn keep = { cw / 2 - 150, ch / 2 + 10, 300, 70,
                            "KEEP THESE", RGB(30, 110, 60) };
            wiz_draw_btn(fb, &keep);
        }

        fb_swap(fb);
        usleep(FRAME_DELAY_ACTIVE_US);
    }

    /* ------------------------- teardown ------------------------- */
    if (!saved) {
        /* Nothing was written, so nothing may be left applied — including the
         * measured reach, which changes every app's touch-safe inset. */
        touch_set_raw_curve(touch, entry_rx0, entry_kxl, entry_kxh, entry_rx1,
                                   entry_ry0, entry_kyl, entry_kyh, entry_ry1);
        touch_set_edge_reach(touch, entry_gx0, entry_gx1, entry_gy0, entry_gy1);
        fb_set_bezel(fb, entry_bt, entry_bb, entry_bl, entry_br);
    }
    touch_set_screen_size(touch, (int)fb->width, (int)fb->height);
    touch_drain_events(touch);

    if (msg[0]) {
        cp_status(msg, saved);
    }
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

/* Lay out every tab's widgets. Re-run whenever the logical screen size changes,
 * since all of them derive their geometry from SCREEN_SAFE_*. */
static void rebuild_ui(AppState *state) {
    create_tab_bar();
    create_tests_ui();
    /* Each prints its "control_panel: <page> stack …" receipt. */
    for (int i = 0; i < HOME_ITEM_COUNT; i++)
        if (home_items[i].page) home_items[i].page->layout();
    /* Prints the "control_panel home: safe …" receipt — see icon_grid_layout(). */
    icon_grid_layout(&home_grid, g_fb, HOME_TITLE_H, "control_panel home");
}

void cp_run_touch_tool(Framebuffer *fb, TouchInput *touch, int mode) {
    if (mode == CP_TOUCH_DIAGNOSTIC)
        run_touch_diagnostic(fb, touch);
    else
        run_calib_wizard(fb, touch, mode == CP_TOUCH_EDGES);
    /* The logical screen may have resized under the UI - the tab bar, every
     * page and the home grid are laid out from SCREEN_SAFE_*.  A page's
     * run_fullscreen() is followed by no rebuild of main()'s, so it is here. */
    rebuild_ui(g_state);
}

static void run_current_fullscreen_mode(Framebuffer *fb, TouchInput *touch,
                                        AppState *state) {
    if (state->active_tab == TAB_TESTS) {
        run_test(fb, touch, state->test_selected);
        state->test_sub = TEST_MENU_VIEW;
        hw_leds_off();
    } else if (state->active_tab == TAB_PAGE) {
        state->page_fullscreen = false;
        if (state->page->run_fullscreen)
            state->page->run_fullscreen(fb, touch);
    }
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

    state.active_tab = TAB_HOME;
    for (int i = 0; i < HOME_ITEM_COUNT; i++)
        if (home_items[i].page) home_items[i].page->load(&state.cfg);
    state.test_sub = TEST_MENU_VIEW;
    state.test_selected = -1;

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

        bool fullscreen = (state.active_tab == TAB_TESTS && state.test_sub == TEST_RUNNING)
                       || (state.active_tab == TAB_PAGE && state.page_fullscreen);

        if (fullscreen) {
            run_current_fullscreen_mode(&fb, &touch, &state);
            needs_redraw = true;  /* redraw after returning from fullscreen */
            continue;
        }

        /* --- Render only when visual state changed --- */
        if (needs_redraw) {
            fb_clear(&fb, COLOR_BG);
            if (state.active_tab != TAB_HOME)
                draw_tab_bar(&fb, &state);

            switch (state.active_tab) {
                case TAB_HOME:        draw_home(&fb, &state);        break;
                case TAB_TESTS:       draw_test_menu(&fb, &state);   break;
                case TAB_PAGE:        state.page->draw(&fb);         break;
                default: break;
            }

            /* Draw confirmation dialog overlay on top of everything */
            if (state.confirm_action != CONFIRM_NONE) {
                modal_dialog_draw(&confirm_dialog, &fb);
            }

            fb_swap(&fb);
            needs_redraw = false;
        }

        /* --- Save visual state before input handling --- */
        ActiveTab     prev_tab       = state.active_tab;
        char          prev_status0   = state.status_msg[0];
        /* A new message replacing one still shown keeps status_msg[0] non-zero,
         * so the time it was set is what says the text changed. */
        uint32_t      prev_status_t  = state.status_time_ms;
        int           prev_home_page = state.home_page;
        TestSubState  prev_test_sub  = state.test_sub;
        int           prev_test_sel  = state.test_selected;
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
        } else if (state.active_tab == TAB_HOME) {
            handle_home_input(&state, &ts);
        } else {
            handle_tab_bar_input(&state, tx, ty, touching, now);

            switch (state.active_tab) {
                case TAB_TESTS:       handle_test_menu_input(&state, tx, ty, touching, now); break;
                case TAB_PAGE: {
                    /* A page's visual state is its own, so it says when it
                     * changed; a queued full-screen run repaints too, exactly
                     * as a changed tab field would. */
                    CpPageResult r = state.page->input(&state.cfg, tx, ty,
                                                       touching, now);
                    if (r == CP_PAGE_FULLSCREEN) state.page_fullscreen = true;
                    if (r != CP_PAGE_IDLE)       state.page_dirty = true;
                    break;
                }
                default: break;
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
            prev_tab       != state.active_tab     ||
            prev_status0   != state.status_msg[0]   ||
            prev_status_t  != state.status_time_ms  ||
            prev_home_page != state.home_page       ||
            prev_test_sub  != state.test_sub        ||
            prev_test_sel  != state.test_selected   ||
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
        bool page_busy = state.active_tab == TAB_PAGE && state.page &&
                         state.page->busy && state.page->busy();
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
