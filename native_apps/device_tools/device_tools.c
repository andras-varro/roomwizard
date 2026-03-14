/**
 * Device Tools â€” Unified Hardware App for RoomWizard
 *
 * Consolidates four standalone hardware utilities into a single tab-based GUI:
 *   Tab 1: Settings     â€” Audio, LED, backlight configuration
 *   Tab 2: Diagnostics  â€” System/memory/storage/hardware/config info
 *   Tab 3: Tests        â€” Interactive LED, display, audio, and touch tests
 *   Tab 4: Calibration  â€” Touch calibration + bezel margin adjustment
 *
 * See DESIGN.md for full architecture specification.
 */

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Includes & Constants
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

#include "../common/framebuffer.h"
#include "../common/touch_input.h"
#include "../common/hardware.h"
#include "../common/common.h"
#include "../common/config.h"
#include "../common/ui_layout.h"
#include "../common/audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/statvfs.h>
#include <linux/fb.h>
#include <math.h>

/* SCREEN_W / SCREEN_H removed — use screen_base_width / screen_base_height
   runtime globals (from framebuffer.h) or fb->width / fb->height instead. */

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Color Palette
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

#define COLOR_BG             RGB(20, 20, 30)
#define COLOR_TAB_BG         RGB(30, 30, 45)
#define COLOR_TAB_ACTIVE     RGB(50, 50, 70)
#define COLOR_TAB_INACTIVE   RGB(35, 35, 50)
#define COLOR_SECTION_LINE   RGB(60, 60, 80)
#define COLOR_LABEL          RGB(180, 180, 180)
#define COLOR_HEADER_TEXT    COLOR_CYAN
#define COLOR_DATA           COLOR_WHITE
#define COLOR_BAR_BG         RGB(40, 40, 40)
#define COLOR_BAR_FILL       RGB(0, 180, 60)
#define COLOR_BAR_WARN       COLOR_YELLOW
#define COLOR_BAR_CRIT       COLOR_RED
#define COLOR_PAGE_IND       RGB(120, 120, 120)

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Layout Constants
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

#define TAB_BAR_H         44
#define TAB_DIVIDER_H     2
#define CONTENT_Y         (SCREEN_SAFE_TOP + TAB_BAR_H + TAB_DIVIDER_H)
#define CONTENT_H         (SCREEN_SAFE_HEIGHT - TAB_BAR_H - TAB_DIVIDER_H)
#define CONTENT_LEFT      (SCREEN_SAFE_LEFT + 10)
#define CONTENT_RIGHT     (SCREEN_SAFE_RIGHT - 10)
#define CONTENT_WIDTH     (CONTENT_RIGHT - CONTENT_LEFT)

#define TAB_BTN_W         150
#define TAB_BTN_H         40
#define TAB_BTN_SPACING   4
#define EXIT_BTN_W        55

#define BAR_WIDTH  300
#define BAR_HEIGHT 20

#define DEFAULT_AUDIO_ENABLED        true
#define DEFAULT_LED_ENABLED          true
#define DEFAULT_LED_BRIGHTNESS       100
#define DEFAULT_BACKLIGHT_BRIGHTNESS 100

#define CALIB_MARGIN      40
#define CALIB_MARGIN_STEP 5
#define CALIB_FILE        "/etc/touch_calibration.conf"
#define PORTRAIT_FLAG_FILE  "/opt/games/portrait.mode"

#define TZ_COLS   8
#define TZ_ROWS   6
/* TZ_CELL_W / TZ_CELL_H removed — computed as local variables from
   screen_base_width / screen_base_height at runtime in each function. */
#define TZ_HEADER 36

#define NUM_TESTS 10
#define NUM_MOUNT_POINTS 4

static const char *mount_points[] = {
    "/",
    "/home/root/data",
    "/home/root/log",
    "/home/root/backup"
};

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * State Machine
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

typedef enum {
    TAB_SETTINGS,
    TAB_DIAGNOSTICS,
    TAB_TESTS,
    TAB_CALIBRATION,
    TAB_COUNT
} ActiveTab;

typedef enum {
    DIAG_SYSTEM,
    DIAG_MEMORY,
    DIAG_STORAGE,
    DIAG_HARDWARE,
    DIAG_CONFIG,
    DIAG_PAGE_COUNT
} DiagPage;

typedef enum {
    TEST_MENU_VIEW,
    TEST_RUNNING
} TestSubState;

typedef enum {
    CALIB_IDLE,
    CALIB_PHASE1,
    CALIB_PHASE2,
    CALIB_DONE
} CalibSubState;

typedef enum {
    CONFIRM_NONE,
    CONFIRM_SHUTDOWN,
    CONFIRM_REBOOT
} ConfirmAction;

static const char *tab_names[] = { "SETTINGS", "DIAGNOSTICS", "TESTS", "CALIBRATION" };

static const char *test_names[] = {
    "RED LED", "GREEN LED", "BOTH LEDS", "BACKLIGHT", "PULSE",
    "BLINK", "COLORS", "TOUCH ZONE", "DISPLAY", "AUDIO"
};

static const char *diag_page_titles[] = {
    "SYSTEM INFO", "MEMORY", "STORAGE", "HARDWARE", "CONFIGURATION"
};

typedef struct {
    unsigned long total_kb, free_kb, available_kb;
    unsigned long buffers_kb, cached_kb;
    unsigned long swap_total_kb, swap_free_kb;
} MemInfo;

typedef struct {
    unsigned long total_kb, free_kb, used_kb;
    int valid;
} DiskInfo;

typedef struct {
    ActiveTab     active_tab;
    bool          audio_enabled;
    bool          led_enabled;
    int           led_brightness;
    int           backlight_brightness;
    bool          portrait_mode;
    char          status_msg[64];
    uint32_t      status_time_ms;
    DiagPage      diag_page;
    bool          diag_needs_refresh;
    TestSubState  test_sub;
    int           test_selected;
    CalibSubState calib_sub;
    int           calib_corner;
    int           calib_offsets_x[4];
    int           calib_offsets_y[4];
    int           bezel_top, bezel_bottom, bezel_left, bezel_right;
    Config        cfg;
    ConfirmAction confirm_action;
} AppState;

/* â”€â”€ Globals â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static volatile bool running = true;
static Framebuffer  *g_fb    = NULL;
static TouchInput   *g_touch = NULL;

static void signal_handler(int sig) {
    (void)sig;
    running = false;
}

/* â”€â”€ UI Elements â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static Button tab_buttons[TAB_COUNT];
static Button exit_btn;

/* Settings */
static ToggleSwitch audio_toggle;
static ToggleSwitch led_toggle;
static Button test_audio_btn, test_led_btn;
static Button led_minus_btn, led_plus_btn;
static Button bl_minus_btn, bl_plus_btn;
static Button save_btn, reset_btn;
static ToggleSwitch portrait_toggle;
static Button shutdown_btn, reboot_btn;
static ModalDialog shutdown_dialog;
static ModalDialog reboot_dialog;

/* Diagnostics */
static Button diag_prev_btn, diag_next_btn;

/* Tests */
static UILayout test_layout;
static Button test_buttons[NUM_TESTS];

/* Calibration */
static Button calib_start_btn;

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Shared Drawing Helpers
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static void draw_section_header(Framebuffer *fb, int y, const char *title) {
    int left = CONTENT_LEFT;
    int right = CONTENT_RIGHT;
    fb_draw_text(fb, left, y, title, COLOR_HEADER_TEXT, 2);
    int text_w = text_measure_width(title, 2);
    int line_x = left + text_w + 12;
    if (line_x < right) {
        fb_draw_line(fb, line_x, y + 8, right, y + 8, COLOR_SECTION_LINE);
    }
}

static void draw_brightness_bar(Framebuffer *fb, int x, int y, int value,
                                 int min_val, int max_val, int bar_width) {
    fb_fill_rect(fb, x, y, bar_width, BAR_HEIGHT, COLOR_BAR_BG);
    int range = max_val - min_val;
    int fill_w = (range > 0) ? ((value - min_val) * bar_width) / range : 0;
    if (fill_w > bar_width) fill_w = bar_width;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > 0)
        fb_fill_rect(fb, x, y, fill_w, BAR_HEIGHT, COLOR_BAR_FILL);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", value);
    fb_draw_text(fb, x + bar_width + 10, y + 2, pct, COLOR_WHITE, 2);
}

static int draw_info_row(Framebuffer *fb, int y, const char *label,
                         const char *value, uint32_t value_color) {
    int value_x = CONTENT_LEFT + (CONTENT_WIDTH < 600 ? 150 : 270);
    fb_draw_text(fb, CONTENT_LEFT + 10, y, label, COLOR_LABEL, 2);
    fb_draw_text(fb, value_x, y, value, value_color, 2);
    return y + 28;
}

static void draw_usage_bar(Framebuffer *fb, int x, int y, int width,
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
    int exit_total = EXIT_BTN_W + 20;           /* exit button + margins */
    int tab_area_w = SCREEN_SAFE_WIDTH - exit_total - 20; /* 10px left + 10px gap */
    int num_tabs = TAB_COUNT;
    int tab_w = (tab_area_w - (num_tabs - 1) * TAB_BTN_SPACING) / num_tabs;
    if (tab_w > TAB_BTN_W) tab_w = TAB_BTN_W;  /* cap at original max */
    if (tab_w < 60) tab_w = 60;                 /* minimum usable width */

    /* Use abbreviated labels when tabs are narrow */
    static const char *short_labels[] = { "SET", "DIAG", "TESTS", "CALIB" };
    const char **labels = (tab_w < 120) ? short_labels : tab_names;

    for (int i = 0; i < TAB_COUNT; i++) {
        int tab_x = SCREEN_SAFE_LEFT + 10 + i * (tab_w + TAB_BTN_SPACING);
        button_init_full(&tab_buttons[i], tab_x, tab_y,
                         tab_w, TAB_BTN_H, labels[i],
                         COLOR_TAB_INACTIVE, COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
    }
    button_init_full(&exit_btn,
                     SCREEN_SAFE_RIGHT - EXIT_BTN_W - 10, tab_y,
                     EXIT_BTN_W, TAB_BTN_H, "X",
                     BTN_EXIT_COLOR, COLOR_WHITE,
                     BTN_HIGHLIGHT_COLOR, 2);
}

static void draw_tab_bar(Framebuffer *fb, AppState *state) {
    fb_fill_rect(fb, SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP,
                 SCREEN_SAFE_WIDTH, TAB_BAR_H, COLOR_TAB_BG);
    for (int i = 0; i < TAB_COUNT; i++) {
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
    button_draw_exit(fb, &exit_btn);
    fb_draw_line(fb, SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP + TAB_BAR_H,
                 SCREEN_SAFE_RIGHT, SCREEN_SAFE_TOP + TAB_BAR_H,
                 COLOR_SECTION_LINE);
}

static void handle_tab_bar_input(AppState *state, int tx, int ty,
                                 bool touching, uint32_t now) {
    for (int i = 0; i < TAB_COUNT; i++) {
        if (button_update(&tab_buttons[i], tx, ty, touching, now)) {
            state->active_tab = (ActiveTab)i;
            if (i == TAB_DIAGNOSTICS)
                state->diag_needs_refresh = true;
        }
    }
    if (button_update(&exit_btn, tx, ty, touching, now))
        running = false;
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Settings Tab  (from hardware_config.c)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static void do_audio_test(void) {
    Audio test_audio;
    memset(&test_audio, 0, sizeof(test_audio));
    test_audio.dsp_fd = open("/dev/dsp", O_WRONLY | O_NONBLOCK);
    if (test_audio.dsp_fd < 0) return;
    test_audio.available = true;
    test_audio.sample_rate = 44100;
    int rate = 44100;
    ioctl(test_audio.dsp_fd, SNDCTL_DSP_SPEED, &rate);
    int fmt = AFMT_S16_LE;
    ioctl(test_audio.dsp_fd, SNDCTL_DSP_SETFMT, &fmt);
    int stereo = 1;
    ioctl(test_audio.dsp_fd, SNDCTL_DSP_STEREO, &stereo);
    FILE *f = fopen("/sys/class/gpio/gpio12/direction", "w");
    if (f) { fputs("out", f); fclose(f); }
    f = fopen("/sys/class/gpio/gpio12/value", "w");
    if (f) { fputs("1", f); fclose(f); }
    audio_tone(&test_audio, 880, 200);
    usleep(250000);
    audio_tone(&test_audio, 1320, 200);
    close(test_audio.dsp_fd);
}

static void do_led_test(int brightness_pct) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", brightness_pct);
    FILE *f;
    f = fopen("/sys/class/leds/red_led/brightness", "w");
    if (f) { fputs(buf, f); fclose(f); }
    f = fopen("/sys/class/leds/green_led/brightness", "w");
    if (f) { fputs(buf, f); fclose(f); }
    usleep(500000);
    f = fopen("/sys/class/leds/red_led/brightness", "w");
    if (f) { fputs("0", f); fclose(f); }
    f = fopen("/sys/class/leds/green_led/brightness", "w");
    if (f) { fputs("0", f); fclose(f); }
}

static void apply_backlight(int brightness_pct) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", brightness_pct);
    FILE *f = fopen("/sys/class/backlight/pwm-backlight/brightness", "w");
    if (f) { fputs(buf, f); fclose(f); }
}

/* ── System Action Functions ──────────────────────────────────────────── */

static void execute_system_action(ConfirmAction action) {
    /* Clean up hardware */
    hw_leds_off();

    /* Show status message on screen */
    fb_clear(g_fb, COLOR_BLACK);
    const char *msg = (action == CONFIRM_SHUTDOWN)
                      ? "SHUTTING DOWN..." : "REBOOTING...";
    text_draw_centered(g_fb, g_fb->width / 2, g_fb->height / 2, msg, COLOR_YELLOW, 3);
    fb_swap(g_fb);

    /* Flush filesystem and execute */
    sync();
    if (action == CONFIRM_SHUTDOWN)
        system("shutdown -h now");
    else
        system("reboot");

    /* Wait for system to act (in case system() returns immediately) */
    sleep(30);
    exit(0);
}

static void create_settings_ui(AppState *state) {
    int portrait = (CONTENT_WIDTH < 600);
    int sec_audio_y = CONTENT_Y + 2;
    int sec_led_y   = CONTENT_Y + 52;
    int sec_disp_y  = CONTENT_Y + (portrait ? 175 : 135);
    int action_y    = CONTENT_Y + (portrait ? 310 : 225);
    int led_bar_y   = sec_led_y + 50;
    int bl_bar_y    = sec_disp_y + 23;

    toggle_init(&audio_toggle, CONTENT_LEFT + 5, sec_audio_y + 20,
                60, 28, "AUDIO ENABLED", state->audio_enabled);
    toggle_init(&led_toggle, CONTENT_LEFT + 5, sec_led_y + 20,
                60, 28, "LED EFFECTS", state->led_enabled);

    if (portrait) {
        /* Portrait stacked layout: [-] [bar] [+] value on row below label */
        int bar_w = CONTENT_WIDTH - 170;
        if (bar_w < 80) bar_w = 80;
        int led_ctrl_y = led_bar_y + 25;
        int bl_ctrl_y  = bl_bar_y + 25;

        button_init_full(&led_minus_btn, CONTENT_LEFT, led_ctrl_y - 5,
                         45, 30, "-", RGB(80, 80, 80), COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
        button_init_full(&led_plus_btn, CONTENT_LEFT + 55 + bar_w + 10, led_ctrl_y - 5,
                         45, 30, "+", RGB(80, 80, 80), COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
        button_init_full(&bl_minus_btn, CONTENT_LEFT, bl_ctrl_y - 5,
                         45, 30, "-", RGB(80, 80, 80), COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
        button_init_full(&bl_plus_btn, CONTENT_LEFT + 55 + bar_w + 10, bl_ctrl_y - 5,
                         45, 30, "+", RGB(80, 80, 80), COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
    } else {
        /* Landscape layout: label + [-] [bar] [+] on same row */
        int led_bar_x = CONTENT_LEFT + 190;
        int bl_bar_x  = CONTENT_LEFT + 190;
        button_init_full(&led_minus_btn, led_bar_x - 55, led_bar_y - 5,
                         45, 30, "-", RGB(80, 80, 80), COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
        button_init_full(&led_plus_btn, led_bar_x + BAR_WIDTH + 70, led_bar_y - 5,
                         45, 30, "+", RGB(80, 80, 80), COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
        button_init_full(&bl_minus_btn, bl_bar_x - 55, bl_bar_y - 5,
                         45, 30, "-", RGB(80, 80, 80), COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
        button_init_full(&bl_plus_btn, bl_bar_x + BAR_WIDTH + 70, bl_bar_y - 5,
                         45, 30, "+", RGB(80, 80, 80), COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
    }

    toggle_init(&portrait_toggle, CONTENT_LEFT + 5, sec_disp_y + (portrait ? 75 : 53),
                60, 28, "PORTRAIT MODE", state->portrait_mode);

    button_init_full(&test_audio_btn, CONTENT_RIGHT - 100, sec_audio_y + 18,
                     90, 30, "TEST", BTN_COLOR_INFO, COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);
    button_init_full(&test_led_btn, CONTENT_RIGHT - 100, sec_led_y + 18,
                     90, 30, "TEST", BTN_COLOR_INFO, COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);

    int center_x = CONTENT_LEFT + CONTENT_WIDTH / 2;
    if (portrait) {
        /* Portrait: stack buttons vertically, centered */
        button_init_full(&save_btn, center_x - 70, action_y,
                         140, 40, "SAVE", BTN_COLOR_PRIMARY, COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 3);
        button_init_full(&reset_btn, center_x - 90, action_y + 50,
                         180, 40, "RESET DEFAULTS", BTN_COLOR_DANGER, COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
    } else {
        button_init_full(&save_btn, center_x - 200, action_y,
                         140, 40, "SAVE", BTN_COLOR_PRIMARY, COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 3);
        button_init_full(&reset_btn, center_x + 10, action_y,
                         180, 40, "RESET DEFAULTS", BTN_COLOR_DANGER, COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
    }

    /* SYSTEM section — Shut Down and Reboot buttons below status message */
    int system_y = action_y + (portrait ? 130 : 70);
    int sys_btn_w = 140, sys_btn_h = 38;
    int sys_gap = 20;
    int sys_total_w = sys_btn_w * 2 + sys_gap;
    int sys_x_start = center_x - sys_total_w / 2;

    button_init_full(&shutdown_btn, sys_x_start, system_y + 20,
                     sys_btn_w, sys_btn_h, "SHUT DOWN",
                     BTN_COLOR_DANGER, COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);
    button_init_full(&reboot_btn, sys_x_start + sys_btn_w + sys_gap, system_y + 20,
                     sys_btn_w, sys_btn_h, "REBOOT",
                     BTN_COLOR_WARNING, COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);

    /* Confirmation dialogs */
    modal_dialog_init_confirm(&shutdown_dialog, "SHUT DOWN DEVICE?",
                              "THE DEVICE WILL POWER OFF.",
                              "SHUT DOWN", BTN_COLOR_DANGER,
                              "CANCEL", RGB(100, 100, 100));

    modal_dialog_init_confirm(&reboot_dialog, "REBOOT DEVICE?",
                              "THE DEVICE WILL RESTART.",
                              "REBOOT", BTN_COLOR_WARNING,
                              "CANCEL", RGB(100, 100, 100));
}

static void draw_settings(Framebuffer *fb, AppState *state) {
    int portrait = (CONTENT_WIDTH < 600);
    int sec_audio_y = CONTENT_Y + 2;
    int sec_led_y   = CONTENT_Y + 52;
    int sec_disp_y  = CONTENT_Y + (portrait ? 175 : 135);
    int action_y    = CONTENT_Y + (portrait ? 310 : 225);
    int led_bar_y   = sec_led_y + 50;
    int bl_bar_y    = sec_disp_y + 23;

    draw_section_header(fb, sec_audio_y, "AUDIO");
    toggle_draw(fb, &audio_toggle);
    button_draw(fb, &test_audio_btn);

    draw_section_header(fb, sec_led_y, "LEDS");
    toggle_draw(fb, &led_toggle);
    button_draw(fb, &test_led_btn);

    fb_draw_text(fb, CONTENT_LEFT + 5, led_bar_y + 2,
                 "LED BRIGHTNESS", COLOR_LABEL, 2);
    button_draw(fb, &led_minus_btn);
    if (portrait) {
        int bar_w = CONTENT_WIDTH - 170;
        if (bar_w < 80) bar_w = 80;
        int led_ctrl_y = led_bar_y + 25;
        draw_brightness_bar(fb, CONTENT_LEFT + 55, led_ctrl_y,
                            state->led_brightness, 0, 100, bar_w);
    } else {
        draw_brightness_bar(fb, CONTENT_LEFT + 190, led_bar_y,
                            state->led_brightness, 0, 100, BAR_WIDTH);
    }
    button_draw(fb, &led_plus_btn);

    draw_section_header(fb, sec_disp_y, "DISPLAY");
    fb_draw_text(fb, CONTENT_LEFT + 5, bl_bar_y + 2,
                 "BACKLIGHT", COLOR_LABEL, 2);
    button_draw(fb, &bl_minus_btn);
    if (portrait) {
        int bar_w = CONTENT_WIDTH - 170;
        if (bar_w < 80) bar_w = 80;
        int bl_ctrl_y = bl_bar_y + 25;
        draw_brightness_bar(fb, CONTENT_LEFT + 55, bl_ctrl_y,
                            state->backlight_brightness, 20, 100, bar_w);
    } else {
        draw_brightness_bar(fb, CONTENT_LEFT + 190, bl_bar_y,
                            state->backlight_brightness, 20, 100, BAR_WIDTH);
    }
    button_draw(fb, &bl_plus_btn);

    toggle_draw(fb, &portrait_toggle);
    if (portrait_toggle.state) {
        int note_y = sec_disp_y + (portrait ? 88 : 80);
        fb_draw_text(fb, CONTENT_LEFT + 5, note_y,
                     "TAKES EFFECT ON NEXT APP LAUNCH", RGB(255, 200, 80), 1);
    }

    button_draw(fb, &save_btn);
    button_draw(fb, &reset_btn);

    if (state->status_msg[0]) {
        uint32_t sc = COLOR_GREEN;
        if (strstr(state->status_msg, "DEFAULTS")) sc = COLOR_CYAN;
        int status_y = portrait ? action_y + 100 : action_y + 50;
        text_draw_centered(fb, CONTENT_LEFT + CONTENT_WIDTH / 2,
                           status_y, state->status_msg, sc, 2);
    }

    /* SYSTEM section */
    int system_y = action_y + (portrait ? 130 : 70);
    draw_section_header(fb, system_y, "SYSTEM");
    button_draw(fb, &shutdown_btn);
    button_draw(fb, &reboot_btn);
}

static void handle_settings_input(AppState *state, int tx, int ty,
                                  bool touching, uint32_t now) {
    if (toggle_check_press(&audio_toggle, tx, ty, touching, now))
        state->audio_enabled = audio_toggle.state;
    if (toggle_check_press(&led_toggle, tx, ty, touching, now))
        state->led_enabled = led_toggle.state;
    if (toggle_check_press(&portrait_toggle, tx, ty, touching, now)) {
        state->portrait_mode = portrait_toggle.state;
    }

    if (button_update(&led_minus_btn, tx, ty, touching, now)) {
        state->led_brightness -= 10;
        if (state->led_brightness < 0) state->led_brightness = 0;
        do_led_test(state->led_brightness);
    }
    if (button_update(&led_plus_btn, tx, ty, touching, now)) {
        state->led_brightness += 10;
        if (state->led_brightness > 100) state->led_brightness = 100;
        do_led_test(state->led_brightness);
    }
    if (button_update(&bl_minus_btn, tx, ty, touching, now)) {
        state->backlight_brightness -= 10;
        if (state->backlight_brightness < 20) state->backlight_brightness = 20;
        apply_backlight(state->backlight_brightness);
    }
    if (button_update(&bl_plus_btn, tx, ty, touching, now)) {
        state->backlight_brightness += 10;
        if (state->backlight_brightness > 100) state->backlight_brightness = 100;
        apply_backlight(state->backlight_brightness);
    }
    if (button_update(&test_audio_btn, tx, ty, touching, now))
        do_audio_test();
    if (button_update(&test_led_btn, tx, ty, touching, now))
        do_led_test(state->led_brightness);

    if (button_update(&save_btn, tx, ty, touching, now)) {
        config_set_bool(&state->cfg, "audio_enabled", state->audio_enabled);
        config_set_bool(&state->cfg, "led_enabled", state->led_enabled);
        config_set_int(&state->cfg, "led_brightness", state->led_brightness);
        config_set_int(&state->cfg, "backlight_brightness", state->backlight_brightness);
        config_save(&state->cfg);
        /* Create or remove portrait mode flag file */
        if (state->portrait_mode) {
            FILE *pf = fopen(PORTRAIT_FLAG_FILE, "w");
            if (pf) { fprintf(pf, "1\n"); fclose(pf); }
        } else {
            unlink(PORTRAIT_FLAG_FILE);
        }
        /* Apply backlight immediately */
        apply_backlight(state->backlight_brightness);
        /* Build status feedback */
        if (state->portrait_mode) {
            snprintf(state->status_msg, sizeof(state->status_msg),
                     "SAVED! PORTRAIT ON NEXT LAUNCH");
        } else {
            snprintf(state->status_msg, sizeof(state->status_msg),
                     "SETTINGS SAVED AND APPLIED");
        }
        state->status_time_ms = now;
    }
    if (button_update(&reset_btn, tx, ty, touching, now)) {
        config_clear(&state->cfg);
        state->audio_enabled = DEFAULT_AUDIO_ENABLED;
        state->led_enabled = DEFAULT_LED_ENABLED;
        state->led_brightness = DEFAULT_LED_BRIGHTNESS;
        state->backlight_brightness = DEFAULT_BACKLIGHT_BRIGHTNESS;
        audio_toggle.state = state->audio_enabled;
        led_toggle.state = state->led_enabled;
        apply_backlight(state->backlight_brightness);
        state->portrait_mode = false;
        portrait_toggle.state = false;
        unlink(PORTRAIT_FLAG_FILE);
        snprintf(state->status_msg, sizeof(state->status_msg), "DEFAULTS RESTORED");
        state->status_time_ms = now;
    }

    /* System action buttons */
    if (button_update(&shutdown_btn, tx, ty, touching, now)) {
        state->confirm_action = CONFIRM_SHUTDOWN;
        modal_dialog_show(&shutdown_dialog);
    }
    if (button_update(&reboot_btn, tx, ty, touching, now)) {
        state->confirm_action = CONFIRM_REBOOT;
        modal_dialog_show(&reboot_dialog);
    }
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Diagnostics Tab  (from hardware_diag.c)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static int read_file_line(const char *path, char *buf, size_t len) {
    FILE *fp = fopen(path, "r");
    if (!fp) { buf[0] = '\0'; return -1; }
    if (!fgets(buf, (int)len, fp)) { buf[0] = '\0'; fclose(fp); return -1; }
    fclose(fp);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return 0;
}

static int read_sysfs_int(const char *path) {
    char buf[64];
    if (read_file_line(path, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

static void read_meminfo(MemInfo *info) {
    memset(info, 0, sizeof(*info));
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        unsigned long val = 0;
        if (sscanf(line, "MemTotal: %lu kB", &val) == 1) info->total_kb = val;
        else if (sscanf(line, "MemFree: %lu kB", &val) == 1) info->free_kb = val;
        else if (sscanf(line, "MemAvailable: %lu kB", &val) == 1) info->available_kb = val;
        else if (sscanf(line, "Buffers: %lu kB", &val) == 1) info->buffers_kb = val;
        else if (sscanf(line, "Cached: %lu kB", &val) == 1) info->cached_kb = val;
        else if (sscanf(line, "SwapTotal: %lu kB", &val) == 1) info->swap_total_kb = val;
        else if (sscanf(line, "SwapFree: %lu kB", &val) == 1) info->swap_free_kb = val;
    }
    fclose(fp);
}

static int read_disk_usage(const char *mp, DiskInfo *info) {
    memset(info, 0, sizeof(*info));
    struct statvfs st;
    if (statvfs(mp, &st) != 0) { info->valid = 0; return -1; }
    info->total_kb = (unsigned long)((unsigned long long)st.f_blocks * st.f_frsize / 1024);
    info->free_kb  = (unsigned long)((unsigned long long)st.f_bfree  * st.f_frsize / 1024);
    info->used_kb  = info->total_kb - info->free_kb;
    info->valid    = 1;
    return 0;
}

static void read_cpuinfo(char *model, size_t mlen, char *clk, size_t clen) {
    model[0] = clk[0] = '\0';
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) { snprintf(model, mlen, "N/A"); snprintf(clk, clen, "N/A"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (model[0] == '\0') {
            if (strncmp(line, "Hardware", 8) == 0 || strncmp(line, "model name", 10) == 0) {
                char *c = strchr(line, ':');
                if (c) { c++; while (*c == ' ' || *c == '\t') c++;
                    char *nl = strchr(c, '\n'); if (nl) *nl = '\0';
                    snprintf(model, mlen, "%s", c); }
            }
        }
        if (clk[0] == '\0' && strncmp(line, "BogoMIPS", 8) == 0) {
            char *c = strchr(line, ':');
            if (c) { c++; while (*c == ' ' || *c == '\t') c++;
                char *nl = strchr(c, '\n'); if (nl) *nl = '\0';
                snprintf(clk, clen, "%s BogoMIPS", c); }
        }
    }
    fclose(fp);
    if (model[0] == '\0') snprintf(model, mlen, "Unknown");
    if (clk[0] == '\0') snprintf(clk, clen, "Unknown");
}

static void read_loadavg(char *buf, size_t len) {
    if (read_file_line("/proc/loadavg", buf, len) < 0)
        snprintf(buf, len, "N/A");
}

static void format_bytes(unsigned long kb, char *buf, size_t len) {
    if (kb >= 1048576) snprintf(buf, len, "%.1f GB", (double)kb / 1048576.0);
    else if (kb >= 1024) snprintf(buf, len, "%.1f MB", (double)kb / 1024.0);
    else snprintf(buf, len, "%lu KB", kb);
}

static void create_diagnostics_ui(void) {
    int nav_y = CONTENT_Y + CONTENT_H - 55;
    button_init_full(&diag_prev_btn, CONTENT_LEFT, nav_y,
                     120, 45, "< PREV", RGB(60, 60, 80), COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);
    button_init_full(&diag_next_btn, CONTENT_RIGHT - 120, nav_y,
                     120, 45, "NEXT >", RGB(60, 60, 80), COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);
}

static void draw_diag_system(Framebuffer *fb) {
    int y = CONTENT_Y + 30;
    char kernel[256];
    if (read_file_line("/proc/version", kernel, sizeof(kernel)) < 0)
        snprintf(kernel, sizeof(kernel), "N/A");
    { char sv[128]; int sp = 0; size_t i;
      for (i = 0; i < sizeof(sv)-1 && kernel[i]; i++) {
          sv[i] = kernel[i];
          if (kernel[i] == ' ') { sp++; if (sp >= 3) { sv[i] = '\0'; break; } }
      } sv[i] = '\0';
      y = draw_info_row(fb, y, "KERNEL:", sv, COLOR_DATA); }
    y += 4;
    { char raw[64];
      if (read_file_line("/proc/uptime", raw, sizeof(raw)) == 0) {
          double secs = 0; sscanf(raw, "%lf", &secs);
          int t = (int)secs; char fmt[64];
          snprintf(fmt, sizeof(fmt), "%dd %dh %dm %ds",
                   t/86400, (t%86400)/3600, (t%3600)/60, t%60);
          y = draw_info_row(fb, y, "UPTIME:", fmt, COLOR_DATA);
      } else { y = draw_info_row(fb, y, "UPTIME:", "N/A", COLOR_DATA); } }
    y += 4;
    { char m[128], c[128]; read_cpuinfo(m, sizeof(m), c, sizeof(c));
      y = draw_info_row(fb, y, "CPU:", m, COLOR_DATA); y += 4;
      y = draw_info_row(fb, y, "CLOCK:", c, COLOR_DATA); }
    y += 4;
    { char la[128]; read_loadavg(la, sizeof(la));
      y = draw_info_row(fb, y, "LOAD AVG:", la, COLOR_DATA); }
    y += 12;
    fb_draw_line(fb, CONTENT_LEFT, y, CONTENT_RIGHT, y, COLOR_SECTION_LINE);
    y += 12;
    { char hn[128];
      if (read_file_line("/etc/hostname", hn, sizeof(hn)) < 0)
          snprintf(hn, sizeof(hn), "N/A");
      draw_info_row(fb, y, "HOSTNAME:", hn, COLOR_DATA); }
}

static void draw_diag_memory(Framebuffer *fb) {
    MemInfo mi; read_meminfo(&mi);
    unsigned long used = mi.total_kb - mi.free_kb - mi.buffers_kb - mi.cached_kb;
    if (used > mi.total_kb) used = 0;
    int y = CONTENT_Y + 30;
    { char b[64];
      format_bytes(mi.total_kb, b, sizeof(b)); y = draw_info_row(fb, y, "RAM TOTAL:", b, COLOR_DATA);
      format_bytes(used, b, sizeof(b)); y = draw_info_row(fb, y, "RAM USED:", b, COLOR_YELLOW);
      format_bytes(mi.free_kb, b, sizeof(b)); y = draw_info_row(fb, y, "RAM FREE:", b, COLOR_GREEN);
      format_bytes(mi.available_kb, b, sizeof(b)); y = draw_info_row(fb, y, "AVAILABLE:", b, COLOR_GREEN);
      format_bytes(mi.buffers_kb + mi.cached_kb, b, sizeof(b)); y = draw_info_row(fb, y, "BUF/CACHE:", b, COLOR_LABEL); }
    y += 8;
    draw_usage_bar(fb, CONTENT_LEFT+30, y+18, CONTENT_WIDTH-60, 22, used, mi.total_kb, "RAM USAGE:");
    y += 58;
    fb_draw_line(fb, CONTENT_LEFT, y, CONTENT_RIGHT, y, COLOR_SECTION_LINE); y += 12;
    { char b[64]; unsigned long su = mi.swap_total_kb - mi.swap_free_kb;
      format_bytes(mi.swap_total_kb, b, sizeof(b)); y = draw_info_row(fb, y, "SWAP TOTAL:", b, COLOR_DATA);
      format_bytes(su, b, sizeof(b)); y = draw_info_row(fb, y, "SWAP USED:", b, COLOR_YELLOW);
      format_bytes(mi.swap_free_kb, b, sizeof(b)); y = draw_info_row(fb, y, "SWAP FREE:", b, COLOR_GREEN);
      y += 8;
      draw_usage_bar(fb, CONTENT_LEFT+30, y+18, CONTENT_WIDTH-60, 22, su, mi.swap_total_kb, "SWAP USAGE:"); }
}

static void draw_diag_storage(Framebuffer *fb) {
    int y = CONTENT_Y + 30;
    for (int i = 0; i < NUM_MOUNT_POINTS; i++) {
        DiskInfo di; read_disk_usage(mount_points[i], &di);
        fb_draw_text(fb, CONTENT_LEFT+10, y, mount_points[i], COLOR_HEADER_TEXT, 2); y += 22;
        if (di.valid) {
            char ts[32], us[32], fs[32];
            format_bytes(di.total_kb, ts, sizeof(ts));
            format_bytes(di.used_kb, us, sizeof(us));
            format_bytes(di.free_kb, fs, sizeof(fs));
            char det[128]; snprintf(det, sizeof(det), "%s USED / %s TOTAL  (%s FREE)", us, ts, fs);
            fb_draw_text(fb, CONTENT_LEFT+30, y, det, COLOR_LABEL, 1); y += 14;
            draw_usage_bar(fb, CONTENT_LEFT+30, y+4, CONTENT_WIDTH-60, 18, di.used_kb, di.total_kb, "");
            y += 30;
        } else {
            fb_draw_text(fb, CONTENT_LEFT+30, y, "N/A (NOT MOUNTED)", RGB(200,80,80), 2); y += 28;
        }
        if (i < NUM_MOUNT_POINTS - 1) {
            y += 4; fb_draw_line(fb, CONTENT_LEFT, y, CONTENT_RIGHT, y, COLOR_SECTION_LINE); y += 8;
        }
    }
}

static void draw_diag_hardware(Framebuffer *fb) {
    int y = CONTENT_Y + 30;
    fb_draw_text(fb, CONTENT_LEFT+10, y, "LED BRIGHTNESS", COLOR_HEADER_TEXT, 2); y += 24;
    fb_draw_line(fb, CONTENT_LEFT, y, CONTENT_RIGHT, y, COLOR_SECTION_LINE); y += 8;
    { char b[32]; int rv = read_sysfs_int("/sys/class/leds/red_led/brightness");
      snprintf(b, sizeof(b), rv >= 0 ? "%d" : "N/A", rv);
      y = draw_info_row(fb, y, "RED LED:", b, (rv > 0) ? COLOR_RED : COLOR_LABEL);
      int gv = read_sysfs_int("/sys/class/leds/green_led/brightness");
      snprintf(b, sizeof(b), gv >= 0 ? "%d" : "N/A", gv);
      y = draw_info_row(fb, y, "GREEN LED:", b, (gv > 0) ? COLOR_GREEN : COLOR_LABEL); }
    y += 4;
    { char b[32]; int bl = read_sysfs_int("/sys/class/leds/backlight/brightness");
      snprintf(b, sizeof(b), bl >= 0 ? "%d" : "N/A", bl);
      y = draw_info_row(fb, y, "BACKLIGHT:", b, COLOR_DATA); }
    y += 8;
    fb_draw_text(fb, CONTENT_LEFT+10, y, "FRAMEBUFFER", COLOR_HEADER_TEXT, 2); y += 24;
    fb_draw_line(fb, CONTENT_LEFT, y, CONTENT_RIGHT, y, COLOR_SECTION_LINE); y += 8;
    { char b[64];
      snprintf(b, sizeof(b), "%ux%u", g_fb->width, g_fb->height);
      y = draw_info_row(fb, y, "RESOLUTION:", b, COLOR_DATA);
      snprintf(b, sizeof(b), "%u", g_fb->bytes_per_pixel * 8);
      y = draw_info_row(fb, y, "BPP:", b, COLOR_DATA);
      snprintf(b, sizeof(b), "%u bytes", g_fb->line_length);
      y = draw_info_row(fb, y, "STRIDE:", b, COLOR_DATA);
      snprintf(b, sizeof(b), "%zu bytes", g_fb->screen_size);
      y = draw_info_row(fb, y, "FB SIZE:", b, COLOR_DATA);
      snprintf(b, sizeof(b), "%s", g_fb->double_buffering ? "YES" : "NO");
      y = draw_info_row(fb, y, "DOUBLE BUF:", b, g_fb->double_buffering ? COLOR_GREEN : COLOR_YELLOW); }
}

static void draw_diag_config(Framebuffer *fb) {
    int y = CONTENT_Y + 30;
    fb_draw_text(fb, CONTENT_LEFT+10, y, "CONFIG FILE", COLOR_HEADER_TEXT, 2); y += 22;
    fb_draw_text(fb, CONTENT_LEFT+10, y, CONFIG_FILE_PATH, COLOR_LABEL, 1); y += 16;
    fb_draw_line(fb, CONTENT_LEFT, y, CONTENT_RIGHT, y, COLOR_SECTION_LINE); y += 10;

    Config cfg; config_init(&cfg);
    if (config_load(&cfg) < 0) {
        fb_draw_text(fb, CONTENT_LEFT+30, y, "CONFIG FILE NOT FOUND", RGB(200,80,80), 2); y += 24;
        fb_draw_text(fb, CONTENT_LEFT+30, y, "(USING DEFAULTS)", COLOR_LABEL, 2); y += 28;
    } else {
        static const struct { const char *key; const char *lbl; const char *def; } kk[] = {
            {"audio_enabled","AUDIO ENABLED:","1 (default)"},
            {"led_enabled","LED ENABLED:","1 (default)"},
            {"led_brightness","LED BRIGHTNESS:","100 (default)"},
            {"backlight_brightness","BACKLIGHT:","100 (default)"},
        };
        int nk = sizeof(kk)/sizeof(kk[0]);
        for (int i = 0; i < nk; i++) {
            const char *v = config_get(&cfg, kk[i].key, NULL);
            y = draw_info_row(fb, y, kk[i].lbl, v ? v : kk[i].def, v ? COLOR_DATA : COLOR_LABEL);
        }
        bool has_extra = false;
        for (int i = 0; i < cfg.count; i++) {
            bool known = false;
            for (int k = 0; k < nk; k++)
                if (strcmp(cfg.entries[i].key, kk[k].key) == 0) { known = true; break; }
            if (!known) {
                if (!has_extra) { y += 4; fb_draw_text(fb, CONTENT_LEFT+10, y, "OTHER SETTINGS:", COLOR_HEADER_TEXT, 1); y += 14; has_extra = true; }
                if (y < (int)(CONTENT_Y + CONTENT_H - 80)) {
                    char lb[196]; snprintf(lb, sizeof(lb), "%s = %s", cfg.entries[i].key, cfg.entries[i].value);
                    fb_draw_text(fb, CONTENT_LEFT+20, y, lb, COLOR_LABEL, 1); y += 14;
                }
            }
        }
    }
    y += 8; fb_draw_line(fb, CONTENT_LEFT, y, CONTENT_RIGHT, y, COLOR_SECTION_LINE); y += 10;
    fb_draw_text(fb, CONTENT_LEFT+10, y, "SYSTEM", COLOR_HEADER_TEXT, 2); y += 28;
    { char da[256];
      if (read_file_line("/opt/roomwizard/default-app", da, sizeof(da)) == 0) {
          /* Truncate if value won't fit in portrait mode */
          int val_x = CONTENT_LEFT + (CONTENT_WIDTH < 600 ? 150 : 270);
          int max_px = CONTENT_RIGHT - val_x - 10;
          int scale = 2;
          int max_chars = max_px / (6 * scale);
          if (max_chars < 3) max_chars = 3;
          if ((int)strlen(da) > max_chars) {
              da[max_chars - 2] = '.';
              da[max_chars - 1] = '.';
              da[max_chars] = '\0';
          }
          y = draw_info_row(fb, y, "DEFAULT APP:", da, COLOR_DATA);
      } else y = draw_info_row(fb, y, "DEFAULT APP:", "(NOT SET)", COLOR_LABEL); }
    { if (access(CALIB_FILE, 0) == 0) y = draw_info_row(fb, y, "CALIBRATED:", "YES", COLOR_GREEN);
      else y = draw_info_row(fb, y, "CALIBRATED:", "NO", COLOR_YELLOW); }
}

static void draw_diagnostics(Framebuffer *fb, AppState *state) {
    fb_draw_text(fb, CONTENT_LEFT+10, CONTENT_Y+5,
                 diag_page_titles[state->diag_page], COLOR_HEADER_TEXT, 2);
    char pi[16]; snprintf(pi, sizeof(pi), "PAGE %d/%d", state->diag_page+1, DIAG_PAGE_COUNT);
    fb_draw_text(fb, CONTENT_RIGHT-120, CONTENT_Y+5, pi, COLOR_PAGE_IND, 2);

    switch (state->diag_page) {
        case DIAG_SYSTEM:  draw_diag_system(fb);  break;
        case DIAG_MEMORY:  draw_diag_memory(fb);  break;
        case DIAG_STORAGE: draw_diag_storage(fb);  break;
        case DIAG_HARDWARE:draw_diag_hardware(fb); break;
        case DIAG_CONFIG:  draw_diag_config(fb);   break;
        default: break;
    }
    if (state->diag_page > 0) button_draw(fb, &diag_prev_btn);
    if (state->diag_page < DIAG_PAGE_COUNT - 1)
        button_set_text(&diag_next_btn, "NEXT >");
    else
        button_set_text(&diag_next_btn, "DONE");
    button_draw(fb, &diag_next_btn);
}

static void handle_diag_input(AppState *state, int tx, int ty,
                              bool touching, uint32_t now) {
    if (state->diag_page > 0 && button_update(&diag_prev_btn, tx, ty, touching, now)) {
        state->diag_page = (DiagPage)((int)state->diag_page - 1);
        state->diag_needs_refresh = true;
    }
    if (button_update(&diag_next_btn, tx, ty, touching, now)) {
        int next = (int)state->diag_page + 1;
        if (next >= DIAG_PAGE_COUNT) next = 0;
        state->diag_page = (DiagPage)next;
        state->diag_needs_refresh = true;
    }
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
        button_init_full(&test_buttons[i], 0, 0, test_item_w, 70, test_names[i],
                         RGB(34,34,34), COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);
}

static void draw_test_screen(Framebuffer *fb, const char *title,
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

static bool check_touch(TouchInput *touch, int *x, int *y) {
    if (touch_poll(touch) > 0) {
        TouchState ts = touch_get_state(touch);
        if (ts.pressed) { *x = ts.x; *y = ts.y; return true; }
    }
    return false;
}

static void test_red_led(Framebuffer *fb, TouchInput *touch) {
    int x, y;
    for (int i = 0; i <= 100; i += 5) {
        char s[64]; snprintf(s, sizeof(s), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "RED LED TEST", s, i);
        hw_set_red_led(i); usleep(50000);
        if (check_touch(touch, &x, &y)) { hw_set_red_led(0); return; }
    }
    for (int i = 0; i < 20; i++) { usleep(50000); if (check_touch(touch, &x, &y)) { hw_set_red_led(0); return; } }
    for (int i = 100; i >= 0; i -= 5) {
        char s[64]; snprintf(s, sizeof(s), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "RED LED TEST", s, 100 - i);
        hw_set_red_led(i); usleep(50000);
        if (check_touch(touch, &x, &y)) { hw_set_red_led(0); return; }
    }
    hw_set_red_led(0);
    draw_test_screen(fb, "RED LED TEST", "COMPLETE!", 100);
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

static void test_green_led(Framebuffer *fb, TouchInput *touch) {
    int x, y;
    for (int i = 0; i <= 100; i += 5) {
        char s[64]; snprintf(s, sizeof(s), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "GREEN LED TEST", s, i);
        hw_set_green_led(i); usleep(50000);
        if (check_touch(touch, &x, &y)) { hw_set_green_led(0); return; }
    }
    for (int i = 0; i < 20; i++) { usleep(50000); if (check_touch(touch, &x, &y)) { hw_set_green_led(0); return; } }
    for (int i = 100; i >= 0; i -= 5) {
        char s[64]; snprintf(s, sizeof(s), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "GREEN LED TEST", s, 100 - i);
        hw_set_green_led(i); usleep(50000);
        if (check_touch(touch, &x, &y)) { hw_set_green_led(0); return; }
    }
    hw_set_green_led(0);
    draw_test_screen(fb, "GREEN LED TEST", "COMPLETE!", 100);
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

static void test_both_leds(Framebuffer *fb, TouchInput *touch) {
    int x, y;
    for (int i = 0; i <= 100; i += 5) {
        char s[64]; snprintf(s, sizeof(s), "BOTH LEDS: %d%%", i);
        draw_test_screen(fb, "BOTH LEDS TEST", s, i);
        hw_set_leds(i, i); usleep(50000);
        if (check_touch(touch, &x, &y)) { hw_leds_off(); return; }
    }
    for (int i = 0; i < 20; i++) { usleep(50000); if (check_touch(touch, &x, &y)) { hw_leds_off(); return; } }
    for (int c = 0; c < 5; c++) {
        draw_test_screen(fb, "BOTH LEDS TEST", "RED ONLY", 50);
        hw_set_leds(100, 0);
        for (int i = 0; i < 10; i++) { usleep(50000); if (check_touch(touch, &x, &y)) { hw_leds_off(); return; } }
        draw_test_screen(fb, "BOTH LEDS TEST", "GREEN ONLY", 50);
        hw_set_leds(0, 100);
        for (int i = 0; i < 10; i++) { usleep(50000); if (check_touch(touch, &x, &y)) { hw_leds_off(); return; } }
    }
    hw_leds_off();
    draw_test_screen(fb, "BOTH LEDS TEST", "COMPLETE!", 100);
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

static void test_backlight_run(Framebuffer *fb, TouchInput *touch) {
    int original = hw_get_backlight();
    int x, y;
    for (int i = 100; i >= 20; i -= 5) {
        char s[64]; snprintf(s, sizeof(s), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "BACKLIGHT TEST", s, 100 - i);
        hw_set_backlight(i); usleep(50000);
        if (check_touch(touch, &x, &y)) { hw_set_backlight(original); return; }
    }
    for (int i = 20; i <= 100; i += 5) {
        char s[64]; snprintf(s, sizeof(s), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "BACKLIGHT TEST", s, i);
        hw_set_backlight(i); usleep(50000);
        if (check_touch(touch, &x, &y)) { hw_set_backlight(original); return; }
    }
    hw_set_backlight(original);
    draw_test_screen(fb, "BACKLIGHT TEST", "COMPLETE!", 100);
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

static void test_pulse(Framebuffer *fb, TouchInput *touch) {
    draw_test_screen(fb, "PULSE EFFECT", "PULSING GREEN LED...", 50);
    hw_pulse_led(LED_GREEN, 3000, 100);
    draw_test_screen(fb, "PULSE EFFECT", "COMPLETE!", 100);
    int x, y; while (!check_touch(touch, &x, &y)) usleep(10000);
}

static void test_blink(Framebuffer *fb, TouchInput *touch) {
    draw_test_screen(fb, "BLINK EFFECT", "BLINKING RED LED...", 50);
    hw_blink_led(LED_RED, 10, 200, 200, 100);
    draw_test_screen(fb, "BLINK EFFECT", "COMPLETE!", 100);
    int x, y; while (!check_touch(touch, &x, &y)) usleep(10000);
}

static void test_colors(Framebuffer *fb, TouchInput *touch) {
    const char *cnames[] = {"RED", "ORANGE", "YELLOW", "GREEN", "OFF"};
    const struct { uint8_t r; uint8_t g; } cols[] = {
        {100,0},{100,50},{100,100},{0,100},{0,0}
    };
    int x, y;
    for (int i = 0; i < 5; i++) {
        draw_test_screen(fb, "COLOR CYCLE", cnames[i], (i * 100) / 4);
        hw_set_leds(cols[i].r, cols[i].g);
        for (int j = 0; j < 20; j++) {
            usleep(50000);
            if (check_touch(touch, &x, &y)) { hw_leds_off(); return; }
        }
    }
    draw_test_screen(fb, "COLOR CYCLE", "COMPLETE!", 100);
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

static void test_touch_zone(Framebuffer *fb, TouchInput *touch) {
    int tz_cell_w = fb->width / TZ_COLS;
    int tz_cell_h = fb->height / TZ_ROWS;
    bool hit[TZ_ROWS][TZ_COLS];
    memset(hit, 0, sizeof(hit));
    int hit_count = 0, total_cells = TZ_ROWS * TZ_COLS;
    int calib_ok = (touch_load_calibration(touch, CALIB_FILE) == 0);
    if (calib_ok) touch_enable_calibration(touch, true);
    int last_raw_x = 0, last_raw_y = 0, last_cal_x = 0, last_cal_y = 0;
    bool test_running = true;

    while (test_running) {
        fb_clear(fb, RGB(20,20,30));
        char hdr[128]; snprintf(hdr, sizeof(hdr),
            "Touch Zone  %d/%d  |  HW X[%d..%d] Y[%d..%d]  |  Calib: %s",
            hit_count, total_cells, touch->raw_min_x, touch->raw_max_x,
            touch->raw_min_y, touch->raw_max_y, calib_ok ? "ON" : "OFF");
        fb_draw_text(fb, 4, 2, hdr, COLOR_WHITE, 1);
        char val[80]; snprintf(val, sizeof(val), "Last: raw(%d,%d) -> screen(%d,%d)",
            last_raw_x, last_raw_y, last_cal_x, last_cal_y);
        fb_draw_text(fb, 4, 14, val, COLOR_CYAN, 1);
        fb_draw_text(fb, fb->width - 160, 2, "[EXIT: top-right]", RGB(180,80,80), 1);

        for (int r = 0; r < TZ_ROWS; r++) {
            for (int c = 0; c < TZ_COLS; c++) {
                int cx = c * tz_cell_w, cy = TZ_HEADER + r * tz_cell_h;
                int cw = tz_cell_w - 2, ch = tz_cell_h - 2;
                uint32_t bg = hit[r][c] ? RGB(20,120,40) : RGB(80,30,30);
                fb_fill_rect(fb, cx+1, cy+1, cw, ch, bg);
                fb_draw_rect(fb, cx+1, cy+1, cw, ch, RGB(70,70,90));
                char lbl[8]; snprintf(lbl, sizeof(lbl), "%d,%d", c, r);
                fb_draw_text(fb, cx+4, cy+4, lbl, RGB(150,150,150), 1);
            }
        }
        if (last_cal_x > 0 || last_cal_y > 0) {
            fb_draw_line(fb, last_cal_x-12, last_cal_y, last_cal_x+12, last_cal_y, COLOR_YELLOW);
            fb_draw_line(fb, last_cal_x, last_cal_y-12, last_cal_x, last_cal_y+12, COLOR_YELLOW);
        }
        int bar_w = ((fb->width - 20) * hit_count) / total_cells;
        fb_fill_rect(fb, 10, fb->height - 10, bar_w, 6,
                     (hit_count == total_cells) ? COLOR_GREEN : COLOR_CYAN);
        fb_swap(fb);

        int x, y;
        if (touch_wait_for_press(touch, &x, &y) == 0) {
            last_cal_x = x; last_cal_y = y;
            last_raw_x = touch->state.x; last_raw_y = touch->state.y;
            if (x > fb->width - 100 && y < TZ_HEADER) { test_running = false; break; }
            int gc = x / tz_cell_w, gr = (y - TZ_HEADER) / tz_cell_h;
            if (gc >= 0 && gc < TZ_COLS && gr >= 0 && gr < TZ_ROWS) {
                if (!hit[gr][gc]) { hit[gr][gc] = true; hit_count++; }
            }
        }
        usleep(16000);
    }
    touch_enable_calibration(touch, false);
}

static void draw_display_page(Framebuffer *fb, const char *title,
                              const char *footer) {
    fb_draw_text(fb, 4, 2, title, COLOR_WHITE, 2);
    fb_draw_text(fb, fb->width / 3, fb->height - 20, footer, RGB(140,140,140), 1);
}

static void test_display(Framebuffer *fb, TouchInput *touch) {
    int page = 0;
    const int pages = 6;
    bool disp_running = true;
    int x, y;
    struct fb_var_screeninfo vinfo;
    ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo);

    while (disp_running) {
        fb_clear(fb, COLOR_BLACK);
        switch (page) {
        case 0: {
            draw_display_page(fb, "DISPLAY INFO", "tap -> next | top-right -> exit");
            char buf[96]; int row = 60;
            #define INFO_LINE(fmt, ...) \
                snprintf(buf, sizeof(buf), fmt, __VA_ARGS__); \
                fb_draw_text(fb, 40, row, buf, COLOR_CYAN, 2); row += 30;
            INFO_LINE("Resolution:  %dx%d", fb->width, fb->height);
            INFO_LINE("BPP:         %d", vinfo.bits_per_pixel);
            INFO_LINE("Line length: %d bytes", fb->line_length);
            INFO_LINE("Screen size: %d bytes", (int)fb->screen_size);
            INFO_LINE("Bytes/pixel: %d", fb->bytes_per_pixel);
            INFO_LINE("Safe area:   (%d,%d)-(%d,%d)",
                       SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP,
                       SCREEN_SAFE_RIGHT, SCREEN_SAFE_BOTTOM);
            INFO_LINE("Double buf:  %s", fb->double_buffering ? "yes" : "no");
            #undef INFO_LINE
            break;
        }
        case 1: {
            draw_display_page(fb, "COLOR BARS", "tap -> next");
            int bw = fb->width / 4;
            fb_fill_rect(fb, 0*bw, 40, bw, fb->height - 80, RGB(255,0,0));
            fb_fill_rect(fb, 1*bw, 40, bw, fb->height - 80, RGB(0,255,0));
            fb_fill_rect(fb, 2*bw, 40, bw, fb->height - 80, RGB(0,0,255));
            fb_fill_rect(fb, 3*bw, 40, bw, fb->height - 80, RGB(255,255,255));
            break;
        }
        case 2: {
            draw_display_page(fb, "GRADIENT", "tap -> next");
            for (int col = 0; col < (int)fb->width; col++) {
                uint8_t v = (col * 255) / (fb->width - 1);
                fb_fill_rect(fb, col, 50, 1, fb->height - 100, RGB(v,v,v));
            }
            break;
        }
        case 3: {
            draw_display_page(fb, "PIXEL GRID", "tap -> next");
            for (int gx = 0; gx < (int)fb->width; gx += 2)
                fb_fill_rect(fb, gx, 40, 1, fb->height - 80, RGB(200,200,200));
            for (int gy = 40; gy < (int)fb->height - 40; gy += 2)
                fb_fill_rect(fb, 0, gy, fb->width, 1, RGB(200,200,200));
            break;
        }
        case 4: {
            draw_display_page(fb, "SAFE AREA", "tap -> next");
            fb_draw_rect(fb, 0, 0, fb->width, fb->height, COLOR_RED);
            fb_draw_rect(fb, 1, 1, fb->width - 2, fb->height - 2, COLOR_RED);
            fb_draw_rect(fb, SCREEN_SAFE_LEFT, SCREEN_SAFE_TOP,
                         SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT, COLOR_GREEN);
            fb_draw_rect(fb, SCREEN_SAFE_LEFT+1, SCREEN_SAFE_TOP+1,
                         SCREEN_SAFE_WIDTH-2, SCREEN_SAFE_HEIGHT-2, COLOR_GREEN);
            { char buf[32];
            snprintf(buf, sizeof(buf), "L=%d", SCREEN_SAFE_LEFT);
            fb_draw_text(fb, SCREEN_SAFE_LEFT+4, 240, buf, COLOR_GREEN, 1);
            snprintf(buf, sizeof(buf), "R=%d", SCREEN_SAFE_RIGHT);
            fb_draw_text(fb, SCREEN_SAFE_RIGHT-40, 240, buf, COLOR_GREEN, 1);
            snprintf(buf, sizeof(buf), "T=%d", SCREEN_SAFE_TOP);
            fb_draw_text(fb, 370, SCREEN_SAFE_TOP+4, buf, COLOR_GREEN, 1);
            snprintf(buf, sizeof(buf), "B=%d", SCREEN_SAFE_BOTTOM);
            fb_draw_text(fb, 370, SCREEN_SAFE_BOTTOM-16, buf, COLOR_GREEN, 1); }
            break;
        }
        case 5: {
            draw_display_page(fb, "ALPHA BLEND", "tap -> exit");
            fb_fill_rect(fb, 100, 80, 300, 300, COLOR_RED);
            fb_fill_rect(fb, 400, 80, 300, 300, COLOR_BLUE);
            fb_fill_rect_alpha(fb, 200, 150, 400, 200, RGB(0,255,0), 128);
            fb_draw_text(fb, 300, 250, "alpha=128", COLOR_WHITE, 2);
            break;
        }
        }
        fb_swap(fb);
        while (1) {
            if (touch_wait_for_press(touch, &x, &y) == 0) {
                if (x > (int)fb->width - 100 && y < 40) { disp_running = false; break; }
                page++;
                if (page >= pages) disp_running = false;
                break;
            }
            usleep(16000);
        }
    }
}

static void test_audio_diag(Framebuffer *fb, TouchInput *touch) {
    Audio audio;
    int audio_ok = (audio_init(&audio) == 0);
    const int freqs[] = { 200, 400, 600, 800, 1000, 1500, 2000, 3000 };
    const int nfreqs = sizeof(freqs) / sizeof(freqs[0]);
    int played = 0;
    bool aud_running = true;
    int x, y;

    while (aud_running && played <= nfreqs) {
        fb_clear(fb, RGB(20,20,30));
        fb_draw_text(fb, 4, 2, "AUDIO DIAGNOSTIC", COLOR_WHITE, 3);
        fb_draw_text(fb, fb->width - 160, 4, "[EXIT]", RGB(180,80,80), 2);
        if (!audio_ok) {
            fb_draw_text(fb, 100, 120, "ERROR: /dev/dsp not available", COLOR_RED, 2);
            fb_draw_text(fb, 100, 160, "Audio subsystem failed to init.", COLOR_YELLOW, 2);
            fb_swap(fb);
            while (!check_touch(touch, &x, &y)) usleep(10000);
            break;
        }
        for (int i = 0; i < nfreqs; i++) {
            char buf[48]; int row_y = 70 + i * 42; uint32_t col;
            if (i < played) {
                snprintf(buf, sizeof(buf), "%5d Hz   DONE", freqs[i]); col = COLOR_GREEN;
            } else if (i == played && played < nfreqs) {
                snprintf(buf, sizeof(buf), "%5d Hz   PLAYING ...", freqs[i]); col = COLOR_YELLOW;
            } else {
                snprintf(buf, sizeof(buf), "%5d Hz   ---", freqs[i]); col = RGB(100,100,100);
            }
            fb_draw_text(fb, 100, row_y, buf, col, 2);
        }
        if (played >= nfreqs)
            fb_draw_text(fb, 200, 420, "ALL DONE - tap to exit", COLOR_CYAN, 2);
        else
            fb_draw_text(fb, 200, 420, "tap to skip/next", RGB(120,120,120), 2);
        fb_swap(fb);

        if (played < nfreqs) {
            audio_interrupt(&audio);
            audio_tone(&audio, freqs[played], 300);
            played++;
            for (int w = 0; w < 10; w++) {
                usleep(30000);
                if (check_touch(touch, &x, &y)) {
                    if (x > (int)fb->width - 100 && y < 40) { aud_running = false; break; }
                }
            }
        } else {
            while (1) {
                if (touch_wait_for_press(touch, &x, &y) == 0) {
                    aud_running = false; break;
                }
                usleep(16000);
            }
        }
    }
    if (audio_ok) audio_close(&audio);
}

/* â”€â”€ Test dispatch â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void run_test(Framebuffer *fb, TouchInput *touch, int test_id) {
    switch (test_id) {
        case 0: test_red_led(fb, touch);       break;
        case 1: test_green_led(fb, touch);     break;
        case 2: test_both_leds(fb, touch);     break;
        case 3: test_backlight_run(fb, touch);  break;
        case 4: test_pulse(fb, touch);         break;
        case 5: test_blink(fb, touch);         break;
        case 6: test_colors(fb, touch);        break;
        case 7: test_touch_zone(fb, touch);    break;
        case 8: test_display(fb, touch);       break;
        case 9: test_audio_diag(fb, touch);    break;
        default: break;
    }
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

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Calibration Tab  (from unified_calibrate.c)
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static void create_calibration_ui(void) {
    int btn_x = CONTENT_LEFT + (CONTENT_WIDTH - 250) / 2;
    int btn_y = CONTENT_Y + 160;
    button_init_full(&calib_start_btn, btn_x, btn_y,
                     250, 55, "START CALIBRATION",
                     BTN_COLOR_PRIMARY, COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);
}

static void draw_calibration(Framebuffer *fb, AppState *state) {
    (void)state;
    text_draw_centered(fb, CONTENT_LEFT + CONTENT_WIDTH / 2,
                       CONTENT_Y + 20, "TOUCH CALIBRATION", COLOR_WHITE, 3);

    int y = CONTENT_Y + 60;
    if (access(CALIB_FILE, 0) == 0)
        y = draw_info_row(fb, y, "STATUS:", "CALIBRATED", COLOR_GREEN);
    else
        y = draw_info_row(fb, y, "STATUS:", "NOT CALIBRATED", COLOR_YELLOW);
    y = draw_info_row(fb, y, "FILE:", CALIB_FILE, COLOR_LABEL);

    if (access(CALIB_FILE, 0) == 0) {
        TouchInput tmp; memset(&tmp, 0, sizeof(tmp));
        if (touch_load_calibration(&tmp, CALIB_FILE) == 0) {
            char margins[64];
            snprintf(margins, sizeof(margins), "T:%d  B:%d  L:%d  R:%d",
                     tmp.calib.bezel_top, tmp.calib.bezel_bottom,
                     tmp.calib.bezel_left, tmp.calib.bezel_right);
            y = draw_info_row(fb, y, "MARGINS:", margins, COLOR_DATA);
        }
    }

    button_draw(fb, &calib_start_btn);

    int note_y = CONTENT_Y + 240;
    fb_draw_text(fb, CONTENT_LEFT + 20, note_y,
                 "Note: Calibration requires touching screen", COLOR_LABEL, 2);
    fb_draw_text(fb, CONTENT_LEFT + 20, note_y + 24,
                 "corners. The process takes about 30 seconds.", COLOR_LABEL, 2);
}

static void handle_calib_input(AppState *state, int tx, int ty,
                               bool touching, uint32_t now) {
    if (button_update(&calib_start_btn, tx, ty, touching, now)) {
        state->calib_sub = CALIB_PHASE1;
        state->calib_corner = 0;
        memset(state->calib_offsets_x, 0, sizeof(state->calib_offsets_x));
        memset(state->calib_offsets_y, 0, sizeof(state->calib_offsets_y));
    }
}

/* â”€â”€ Calibration Phase 1: 4-corner touch calibration (full-screen) â”€â”€â”€â”€â”€ */

static void draw_crosshair(Framebuffer *fb, int x, int y, uint32_t color) {
    int size = 20;
    fb_draw_line(fb, x - size, y, x + size, y, color);
    fb_draw_line(fb, x, y - size, x, y + size, color);
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            fb_draw_pixel(fb, x + dx, y + dy, color);
}

static void run_calib_phase1(Framebuffer *fb, TouchInput *touch, AppState *state) {
    struct { int x, y; const char *name; } points[] = {
        {CALIB_MARGIN, CALIB_MARGIN, "Top-Left"},
        {(int)fb->width - CALIB_MARGIN, CALIB_MARGIN, "Top-Right"},
        {CALIB_MARGIN, (int)fb->height - CALIB_MARGIN, "Bottom-Left"},
        {(int)fb->width - CALIB_MARGIN, (int)fb->height - CALIB_MARGIN, "Bottom-Right"}
    };

    for (int i = state->calib_corner; i < 4; i++) {
        fb_clear(fb, COLOR_BLACK);
        draw_crosshair(fb, points[i].x, points[i].y, COLOR_WHITE);
        char msg[50]; snprintf(msg, sizeof(msg), "TAP CROSSHAIR %d/4", i + 1);
        fb_draw_text(fb, 250, 20, msg, COLOR_CYAN, 2);
        fb_swap(fb);

        int tx, ty;
        if (touch_wait_for_press(touch, &tx, &ty) < 0) {
            state->calib_sub = CALIB_IDLE;
            return;
        }
        state->calib_offsets_x[i] = points[i].x - tx;
        state->calib_offsets_y[i] = points[i].y - ty;

        fb_clear(fb, COLOR_BLACK);
        draw_crosshair(fb, points[i].x, points[i].y, COLOR_GREEN);
        draw_crosshair(fb, tx, ty, COLOR_RED);
        fb_swap(fb);
        sleep(1);
        state->calib_corner = i + 1;
    }

    touch_set_calibration(touch,
        state->calib_offsets_x[0], state->calib_offsets_y[0],
        state->calib_offsets_x[1], state->calib_offsets_y[1],
        state->calib_offsets_x[2], state->calib_offsets_y[2],
        state->calib_offsets_x[3], state->calib_offsets_y[3]);

    state->calib_sub = CALIB_PHASE2;
    state->bezel_top = 35;
    state->bezel_bottom = 35;
    state->bezel_left = 35;
    state->bezel_right = 35;
}

/* â”€â”€ Calibration Phase 2: Bezel margin adjustment (full-screen) â”€â”€â”€â”€â”€â”€â”€â”€ */

static void run_calib_phase2(Framebuffer *fb, TouchInput *touch, AppState *state) {
    touch_set_calibration(touch,
        touch->calib.top_left_x, touch->calib.top_left_y,
        touch->calib.top_right_x, touch->calib.top_right_y,
        touch->calib.bottom_left_x, touch->calib.bottom_left_y,
        touch->calib.bottom_right_x, touch->calib.bottom_right_y);
    touch_enable_calibration(touch, true);

    int zone_size = 80;
    bool done = false;

    while (!done && running) {
        fb_clear(fb, COLOR_BLACK);
        int *top = &state->bezel_top, *bot = &state->bezel_bottom;
        int *lft = &state->bezel_left, *rgt = &state->bezel_right;

        /* Draw checkerboard overlay on margins */
        for (int yy = 0; yy < *top; yy++)
            for (int xx = 0; xx < (int)fb->width; xx++)
                if ((xx/10 + yy/10) % 2 == 0) fb_draw_pixel(fb, xx, yy, COLOR_RED);
        for (int yy = (int)fb->height - *bot; yy < (int)fb->height; yy++)
            for (int xx = 0; xx < (int)fb->width; xx++)
                if ((xx/10 + yy/10) % 2 == 0) fb_draw_pixel(fb, xx, yy, COLOR_RED);
        for (int yy = *top; yy < (int)fb->height - *bot; yy++) {
            for (int xx = 0; xx < *lft; xx++)
                if ((xx/10 + yy/10) % 2 == 0) fb_draw_pixel(fb, xx, yy, COLOR_RED);
            for (int xx = (int)fb->width - *rgt; xx < (int)fb->width; xx++)
                if ((xx/10 + yy/10) % 2 == 0) fb_draw_pixel(fb, xx, yy, COLOR_RED);
        }

        /* Safe area outline (thick) */
        for (int t = 0; t < 5; t++)
            fb_draw_rect(fb, *lft + t, *top + t,
                         (int)fb->width - *lft - *rgt - 2*t,
                         (int)fb->height - *top - *bot - 2*t, COLOR_GREEN);

        int cx = (int)fb->width / 2, cy = (int)fb->height / 2;

        /* TOP +/- zones */
        int top_y = *top / 2;
        int tp_x = cx - zone_size - 10, tm_x = cx + 10;
        fb_fill_rect(fb, tp_x, top_y - zone_size/2, zone_size, zone_size, RGB(0,80,0));
        fb_draw_rect(fb, tp_x, top_y - zone_size/2, zone_size, zone_size, COLOR_CYAN);
        fb_draw_text(fb, tp_x + 25, top_y - 15, "+", COLOR_WHITE, 3);
        fb_fill_rect(fb, tm_x, top_y - zone_size/2, zone_size, zone_size, RGB(80,0,0));
        fb_draw_rect(fb, tm_x, top_y - zone_size/2, zone_size, zone_size, COLOR_CYAN);
        fb_draw_text(fb, tm_x + 30, top_y - 15, "-", COLOR_WHITE, 3);

        /* BOTTOM +/- zones */
        int bot_y = (int)fb->height - *bot / 2;
        fb_fill_rect(fb, tp_x, bot_y - zone_size/2, zone_size, zone_size, RGB(0,80,0));
        fb_draw_rect(fb, tp_x, bot_y - zone_size/2, zone_size, zone_size, COLOR_CYAN);
        fb_draw_text(fb, tp_x + 25, bot_y - 15, "+", COLOR_WHITE, 3);
        fb_fill_rect(fb, tm_x, bot_y - zone_size/2, zone_size, zone_size, RGB(80,0,0));
        fb_draw_rect(fb, tm_x, bot_y - zone_size/2, zone_size, zone_size, COLOR_CYAN);
        fb_draw_text(fb, tm_x + 30, bot_y - 15, "-", COLOR_WHITE, 3);

        /* LEFT +/- zones */
        int left_x = *lft / 2;
        int lp_y = cy - zone_size - 10, lm_y = cy + 10;
        fb_fill_rect(fb, left_x - zone_size/2, lp_y, zone_size, zone_size, RGB(0,80,0));
        fb_draw_rect(fb, left_x - zone_size/2, lp_y, zone_size, zone_size, COLOR_CYAN);
        fb_draw_text(fb, left_x - 15, lp_y + 25, "+", COLOR_WHITE, 3);
        fb_fill_rect(fb, left_x - zone_size/2, lm_y, zone_size, zone_size, RGB(80,0,0));
        fb_draw_rect(fb, left_x - zone_size/2, lm_y, zone_size, zone_size, COLOR_CYAN);
        fb_draw_text(fb, left_x - 10, lm_y + 25, "-", COLOR_WHITE, 3);

        /* RIGHT +/- zones */
        int right_x = (int)fb->width - *rgt / 2;
        fb_fill_rect(fb, right_x - zone_size/2, lp_y, zone_size, zone_size, RGB(0,80,0));
        fb_draw_rect(fb, right_x - zone_size/2, lp_y, zone_size, zone_size, COLOR_CYAN);
        fb_draw_text(fb, right_x - 15, lp_y + 25, "+", COLOR_WHITE, 3);
        fb_fill_rect(fb, right_x - zone_size/2, lm_y, zone_size, zone_size, RGB(80,0,0));
        fb_draw_rect(fb, right_x - zone_size/2, lm_y, zone_size, zone_size, COLOR_CYAN);
        fb_draw_text(fb, right_x - 10, lm_y + 25, "-", COLOR_WHITE, 3);

        /* Center EXIT button */
        int esz = 120;
        fb_fill_rect(fb, cx - esz/2, cy - esz/2, esz, esz, RGB(0,100,0));
        fb_draw_rect(fb, cx - esz/2, cy - esz/2, esz, esz, COLOR_WHITE);
        fb_draw_text(fb, cx - 35, cy - 15, "EXIT", COLOR_WHITE, 2);

        fb_swap(fb);

        int tx, ty;
        if (touch_wait_for_press(touch, &tx, &ty) < 0) { done = true; break; }

        /* Check which zone was touched */
        if (tx >= tp_x && tx < tp_x + zone_size &&
            ty >= top_y - zone_size/2 && ty < top_y + zone_size/2) {
            *top += CALIB_MARGIN_STEP; if (*top > (int)fb->height/3) *top = (int)fb->height/3;
        } else if (tx >= tm_x && tx < tm_x + zone_size &&
                   ty >= top_y - zone_size/2 && ty < top_y + zone_size/2) {
            *top -= CALIB_MARGIN_STEP; if (*top < 0) *top = 0;
        } else if (tx >= tp_x && tx < tp_x + zone_size &&
                   ty >= bot_y - zone_size/2 && ty < bot_y + zone_size/2) {
            *bot += CALIB_MARGIN_STEP; if (*bot > (int)fb->height/3) *bot = (int)fb->height/3;
        } else if (tx >= tm_x && tx < tm_x + zone_size &&
                   ty >= bot_y - zone_size/2 && ty < bot_y + zone_size/2) {
            *bot -= CALIB_MARGIN_STEP; if (*bot < 0) *bot = 0;
        } else if (tx >= left_x - zone_size/2 && tx < left_x + zone_size/2 &&
                   ty >= lp_y && ty < lp_y + zone_size) {
            *lft += CALIB_MARGIN_STEP; if (*lft > (int)fb->width/3) *lft = (int)fb->width/3;
        } else if (tx >= left_x - zone_size/2 && tx < left_x + zone_size/2 &&
                   ty >= lm_y && ty < lm_y + zone_size) {
            *lft -= CALIB_MARGIN_STEP; if (*lft < 0) *lft = 0;
        } else if (tx >= right_x - zone_size/2 && tx < right_x + zone_size/2 &&
                   ty >= lp_y && ty < lp_y + zone_size) {
            *rgt += CALIB_MARGIN_STEP; if (*rgt > (int)fb->width/3) *rgt = (int)fb->width/3;
        } else if (tx >= right_x - zone_size/2 && tx < right_x + zone_size/2 &&
                   ty >= lm_y && ty < lm_y + zone_size) {
            *rgt -= CALIB_MARGIN_STEP; if (*rgt < 0) *rgt = 0;
        } else if (tx >= cx - esz/2 && tx < cx + esz/2 &&
                   ty >= cy - esz/2 && ty < cy + esz/2) {
            done = true;
        }
        usleep(200000);
    }
    state->calib_sub = CALIB_DONE;
}

/* â”€â”€ Calibration Done: save and show result (full-screen) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void run_calib_done(Framebuffer *fb, TouchInput *touch, AppState *state) {
    (void)touch;
    touch->calib.bezel_top    = state->bezel_top;
    touch->calib.bezel_bottom = state->bezel_bottom;
    touch->calib.bezel_left   = state->bezel_left;
    touch->calib.bezel_right  = state->bezel_right;

    if (touch_save_calibration(touch, CALIB_FILE) == 0) {
        fb_clear(fb, COLOR_BLACK);
        text_draw_centered(fb, fb->width / 2, 180, "CALIBRATION SAVED!", COLOR_GREEN, 3);
        char summary[100];
        snprintf(summary, sizeof(summary), "T:%d B:%d L:%d R:%d",
                 state->bezel_top, state->bezel_bottom,
                 state->bezel_left, state->bezel_right);
        text_draw_centered(fb, fb->width / 2, 250, summary, COLOR_YELLOW, 2);
        fb_swap(fb);
        sleep(2);
        fb_load_safe_area();
    } else {
        fb_clear(fb, COLOR_BLACK);
        text_draw_centered(fb, fb->width / 2, 200, "SAVE FAILED - RUN AS ROOT", COLOR_RED, 3);
        fb_swap(fb);
        sleep(3);
    }
    state->calib_sub = CALIB_IDLE;
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * Full-Screen Mode Handler
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

static void run_current_fullscreen_mode(Framebuffer *fb, TouchInput *touch,
                                        AppState *state) {
    if (state->active_tab == TAB_TESTS) {
        run_test(fb, touch, state->test_selected);
        state->test_sub = TEST_MENU_VIEW;
        hw_leds_off();
    } else if (state->active_tab == TAB_CALIBRATION) {
        switch (state->calib_sub) {
            case CALIB_PHASE1: run_calib_phase1(fb, touch, state); break;
            case CALIB_PHASE2: run_calib_phase2(fb, touch, state); break;
            case CALIB_DONE:   run_calib_done(fb, touch, state);   break;
            default: break;
        }
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
    int lock_fd = acquire_instance_lock("device_tools");
    if (lock_fd < 0) return 1;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    hw_init();
    hw_set_backlight(100);

    Framebuffer fb;
    if (fb_init(&fb, "/dev/fb0") < 0) {
        fprintf(stderr, "device_tools: failed to init framebuffer\n");
        return 1;
    }
    g_fb = &fb;

    TouchInput touch;
    if (touch_init(&touch, "/dev/input/event0") < 0) {
        fprintf(stderr, "device_tools: failed to init touch input\n");
        fb_close(&fb);
        return 1;
    }
    g_touch = &touch;

    AppState state;
    memset(&state, 0, sizeof(state));
    config_init(&state.cfg);
    config_load(&state.cfg);

    state.active_tab = TAB_SETTINGS;
    state.audio_enabled = config_get_bool(&state.cfg, "audio_enabled", DEFAULT_AUDIO_ENABLED);
    state.led_enabled = config_get_bool(&state.cfg, "led_enabled", DEFAULT_LED_ENABLED);
    state.led_brightness = config_get_int(&state.cfg, "led_brightness", DEFAULT_LED_BRIGHTNESS);
    state.backlight_brightness = config_get_int(&state.cfg, "backlight_brightness", DEFAULT_BACKLIGHT_BRIGHTNESS);
    if (state.led_brightness < 0)   state.led_brightness = 0;
    if (state.led_brightness > 100) state.led_brightness = 100;
    if (state.backlight_brightness < 20)  state.backlight_brightness = 20;
    if (state.backlight_brightness > 100) state.backlight_brightness = 100;
    state.portrait_mode = (access(PORTRAIT_FLAG_FILE, F_OK) == 0);
    state.diag_page = DIAG_SYSTEM;
    state.diag_needs_refresh = true;
    state.test_sub = TEST_MENU_VIEW;
    state.test_selected = -1;
    state.calib_sub = CALIB_IDLE;

    create_tab_bar();
    create_settings_ui(&state);
    create_diagnostics_ui();
    create_tests_ui();
    create_calibration_ui();

    while (running) {
        uint32_t now = get_time_ms();

        if (state.status_msg[0] && now - state.status_time_ms > 2000)
            state.status_msg[0] = '\0';

        bool fullscreen = (state.active_tab == TAB_TESTS && state.test_sub == TEST_RUNNING)
                       || (state.active_tab == TAB_CALIBRATION && state.calib_sub != CALIB_IDLE);

        if (fullscreen) {
            run_current_fullscreen_mode(&fb, &touch, &state);
            continue;
        }

        fb_clear(&fb, COLOR_BG);
        draw_tab_bar(&fb, &state);

        switch (state.active_tab) {
            case TAB_SETTINGS:    draw_settings(&fb, &state);    break;
            case TAB_DIAGNOSTICS: draw_diagnostics(&fb, &state); break;
            case TAB_TESTS:       draw_test_menu(&fb, &state);   break;
            case TAB_CALIBRATION: draw_calibration(&fb, &state); break;
            default: break;
        }

        /* Draw confirmation dialog overlay on top of everything */
        if (state.confirm_action == CONFIRM_SHUTDOWN) {
            modal_dialog_draw(&shutdown_dialog, &fb);
        } else if (state.confirm_action == CONFIRM_REBOOT) {
            modal_dialog_draw(&reboot_dialog, &fb);
        }

        fb_swap(&fb);

        touch_poll(&touch);
        TouchState ts = touch_get_state(&touch);
        int tx = ts.x, ty = ts.y;
        bool touching = ts.pressed || ts.held;

        /* When confirmation dialog is active, only handle dialog input */
        if (state.confirm_action != CONFIRM_NONE) {
            ModalDialog *active_dlg = (state.confirm_action == CONFIRM_SHUTDOWN)
                                      ? &shutdown_dialog : &reboot_dialog;
            ModalDialogAction action = modal_dialog_update(active_dlg, tx, ty, touching, now);
            if (action == MODAL_ACTION_BTN0) {
                execute_system_action(state.confirm_action);
            } else if (action == MODAL_ACTION_BTN1) {
                state.confirm_action = CONFIRM_NONE;
            }
        } else {
            handle_tab_bar_input(&state, tx, ty, touching, now);

            switch (state.active_tab) {
                case TAB_SETTINGS:    handle_settings_input(&state, tx, ty, touching, now); break;
                case TAB_DIAGNOSTICS: handle_diag_input(&state, tx, ty, touching, now);     break;
                case TAB_TESTS:       handle_test_menu_input(&state, tx, ty, touching, now); break;
                case TAB_CALIBRATION: handle_calib_input(&state, tx, ty, touching, now);    break;
                default: break;
            }
        }

        usleep(16000);
    }

    hw_leds_off();
    hw_reload_config();
    hw_set_backlight(100);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    touch_close(&touch);
    fb_close(&fb);
    return 0;
}
