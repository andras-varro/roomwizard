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
#include <errno.h>
#include <linux/input.h>

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

/* USB tab colors (avoid clashing with existing COLOR_xxx names) */
#define USB_COLOR_PANEL        RGB(30, 30, 45)
#define USB_COLOR_PANEL_BD     RGB(50, 50, 70)
#define USB_COLOR_DIM          RGB(80, 80, 80)
#define USB_COLOR_HDR          COLOR_CYAN
#define USB_COLOR_CONN         RGB(0, 200, 80)
#define USB_COLOR_DISC         RGB(100, 100, 100)
#define USB_COLOR_KEY_UP       RGB(50, 50, 60)
#define USB_COLOR_KEY_DN       RGB(0, 180, 255)
#define USB_COLOR_KEY_BD       RGB(80, 80, 100)
#define USB_COLOR_KEY_TXT      RGB(200, 200, 200)
#define USB_COLOR_STICK_BG     RGB(40, 40, 50)
#define USB_COLOR_STICK_DOT    RGB(0, 200, 255)
#define USB_COLOR_PAD_ON       RGB(0, 220, 100)
#define USB_COLOR_PAD_OFF      RGB(60, 60, 70)
#define USB_COLOR_TRIG_BG      RGB(40, 40, 40)
#define USB_COLOR_TRIG_FILL    RGB(255, 165, 0)
#define USB_COLOR_CURSOR_C     RGB(255, 255, 0)
#define USB_COLOR_MBTN_ON      RGB(0, 200, 80)
#define USB_COLOR_MBTN_OFF     RGB(60, 60, 70)
#define USB_COLOR_SCROLL       RGB(0, 180, 255)
#define USB_COLOR_LOG_BG       RGB(15, 15, 25)
#define USB_COLOR_LOG_TXT      RGB(150, 200, 150)

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

/* ======================================================================
 * USB Constants & Types
 * ====================================================================== */

#define MAX_EV_DEV      16
#define MAX_USB_DEV     8
#define DEV_NAME_LEN    128
#define LOG_LINES       8
#define LOG_LINE_LEN    64

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(x)        ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)          ((x) % BITS_PER_LONG)
#define BIT_LONG(x)     ((x) / BITS_PER_LONG)
#define test_bit(b, a)  ((a[BIT_LONG(b)] >> OFF(b)) & 1)

typedef enum { DEV_UNKNOWN, DEV_KEYBOARD, DEV_MOUSE, DEV_GAMEPAD } DevType;

typedef struct {
    char name[DEV_NAME_LEN]; char path[64];
    DevType type; int ev_num; bool connected;
} USBDev;

typedef struct {
    bool held[KEY_MAX+1]; int last_code; char last_name[32];
    char log[LOG_LINES][LOG_LINE_LEN]; int log_cnt;
} KbdState;

typedef struct {
    int cx, cy; bool bl, bm, br;
    int scroll; uint32_t scroll_t;
} MouseSt;

typedef struct {
    int lx, ly, rx, ry;
    int amin[ABS_MAX+1], amax[ABS_MAX+1];
    int dx, dy; bool btns[16]; int btn_cnt;
    int tl, tr;
} PadSt;

typedef enum { USB_SCR_MAIN, USB_SCR_KEYBOARD, USB_SCR_MOUSE, USB_SCR_GAMEPAD } USBScreen;

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * State Machine
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

typedef enum {
    TAB_SETTINGS,
    TAB_DIAGNOSTICS,
    TAB_TESTS,
    TAB_CALIBRATION,
    TAB_USB,
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

static const char *tab_names[] = { "SETTINGS", "DIAGNOSTICS", "TESTS", "CALIBRATION", "USB" };

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
    /* USB Test tab state */
    USBScreen    usb_scr;
    USBDev       usb_devs[MAX_USB_DEV];
    int          usb_dev_cnt;
    int          usb_kbd_idx, usb_mou_idx, usb_pad_idx;
    int          usb_fd;
    KbdState     usb_kbd;
    MouseSt      usb_mou;
    PadSt        usb_pad;
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

/* USB */
static Button usb_btn_rescan, usb_btn_ktest, usb_btn_mtest, usb_btn_gtest;
static Button usb_btn_kback, usb_btn_mback, usb_btn_gback;

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
    static const char *short_labels[] = { "SET", "DIAG", "TEST", "CAL", "USB" };
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

/* Forward declaration — usb_close() defined in USB Tab section below */
static void usb_close(AppState *s);

static void handle_tab_bar_input(AppState *state, int tx, int ty,
                                 bool touching, uint32_t now) {
    for (int i = 0; i < TAB_COUNT; i++) {
        if (button_update(&tab_buttons[i], tx, ty, touching, now)) {
            ActiveTab prev_tab = state->active_tab;
            state->active_tab = (ActiveTab)i;
            if (i == TAB_DIAGNOSTICS)
                state->diag_needs_refresh = true;
            if (prev_tab == TAB_USB && state->active_tab != TAB_USB)
                usb_close(state);
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


/* ======================================================================
 * USB Tab  (ported from usb_test.c)
 * ====================================================================== */

/* ── Key table ──────────────────────────────────────────────────────────── */
typedef struct { int code; const char *name, *sname; } KeyInfo;
static const KeyInfo usb_ktab[] = {
    {KEY_ESC,"ESC","ESC"},{KEY_1,"1","1"},{KEY_2,"2","2"},{KEY_3,"3","3"},
    {KEY_4,"4","4"},{KEY_5,"5","5"},{KEY_6,"6","6"},{KEY_7,"7","7"},
    {KEY_8,"8","8"},{KEY_9,"9","9"},{KEY_0,"0","0"},{KEY_MINUS,"MINUS","-"},
    {KEY_EQUAL,"EQUAL","="},{KEY_BACKSPACE,"BKSP","BS"},
    {KEY_TAB,"TAB","TAB"},{KEY_Q,"Q","Q"},{KEY_W,"W","W"},{KEY_E,"E","E"},
    {KEY_R,"R","R"},{KEY_T,"T","T"},{KEY_Y,"Y","Y"},{KEY_U,"U","U"},
    {KEY_I,"I","I"},{KEY_O,"O","O"},{KEY_P,"P","P"},
    {KEY_LEFTBRACE,"LBRACE","["},{KEY_RIGHTBRACE,"RBRACE","]"},
    {KEY_ENTER,"ENTER","RET"},
    {KEY_CAPSLOCK,"CAPS","CAP"},{KEY_A,"A","A"},{KEY_S,"S","S"},
    {KEY_D,"D","D"},{KEY_F,"F","F"},{KEY_G,"G","G"},{KEY_H,"H","H"},
    {KEY_J,"J","J"},{KEY_K,"K","K"},{KEY_L,"L","L"},
    {KEY_SEMICOLON,"SEMI",";"},{KEY_APOSTROPHE,"APOS","'"},
    {KEY_BACKSLASH,"BSLASH","\\"},
    {KEY_LEFTSHIFT,"LSHIFT","SHF"},{KEY_Z,"Z","Z"},{KEY_X,"X","X"},
    {KEY_C,"C","C"},{KEY_V,"V","V"},{KEY_B,"B","B"},{KEY_N,"N","N"},
    {KEY_M,"M","M"},{KEY_COMMA,"COMMA",","},{KEY_DOT,"DOT","."},
    {KEY_SLASH,"SLASH","/"},{KEY_RIGHTSHIFT,"RSHIFT","SHF"},
    {KEY_LEFTCTRL,"LCTRL","CTL"},{KEY_LEFTALT,"LALT","ALT"},
    {KEY_SPACE,"SPACE","SPC"},{KEY_RIGHTALT,"RALT","ALT"},
    {KEY_RIGHTCTRL,"RCTRL","CTL"},
    {KEY_UP,"UP","UP"},{KEY_DOWN,"DOWN","DN"},{KEY_LEFT,"LEFT","LT"},
    {KEY_RIGHT,"RIGHT","RT"},
    {KEY_F1,"F1","F1"},{KEY_F2,"F2","F2"},{KEY_F3,"F3","F3"},
    {KEY_F4,"F4","F4"},{KEY_F5,"F5","F5"},{KEY_F6,"F6","F6"},
    {KEY_F7,"F7","F7"},{KEY_F8,"F8","F8"},{KEY_F9,"F9","F9"},
    {KEY_F10,"F10","F10"},{KEY_F11,"F11","F11"},{KEY_F12,"F12","F12"},
    {KEY_GRAVE,"GRAVE","`"},{KEY_DELETE,"DEL","DEL"},{KEY_HOME,"HOME","HOM"},
    {KEY_END,"END","END"},{KEY_PAGEUP,"PGUP","PGU"},
    {KEY_PAGEDOWN,"PGDN","PGD"},{KEY_INSERT,"INS","INS"},
    {0,NULL,NULL}
};

static const char *usb_key_name(int c) {
    for (int i=0; usb_ktab[i].name; i++) if (usb_ktab[i].code==c) return usb_ktab[i].name;
    return NULL;
}
static const char *usb_key_sname(int c) {
    for (int i=0; usb_ktab[i].name; i++) if (usb_ktab[i].code==c) return usb_ktab[i].sname;
    return NULL;
}

/* ── Keyboard layout ────────────────────────────────────────────────────── */
typedef struct { int col, row, w, code; } LKey;
static const LKey usb_kblayout[] = {
    {0,0,2,KEY_ESC},{2,0,2,KEY_1},{4,0,2,KEY_2},{6,0,2,KEY_3},{8,0,2,KEY_4},
    {10,0,2,KEY_5},{12,0,2,KEY_6},{14,0,2,KEY_7},{16,0,2,KEY_8},{18,0,2,KEY_9},
    {20,0,2,KEY_0},{22,0,2,KEY_MINUS},{24,0,2,KEY_EQUAL},{26,0,2,KEY_BACKSPACE},
    {0,1,2,KEY_TAB},{2,1,2,KEY_Q},{4,1,2,KEY_W},{6,1,2,KEY_E},{8,1,2,KEY_R},
    {10,1,2,KEY_T},{12,1,2,KEY_Y},{14,1,2,KEY_U},{16,1,2,KEY_I},{18,1,2,KEY_O},
    {20,1,2,KEY_P},{22,1,2,KEY_LEFTBRACE},{24,1,2,KEY_RIGHTBRACE},{26,1,2,KEY_ENTER},
    {0,2,3,KEY_CAPSLOCK},{3,2,2,KEY_A},{5,2,2,KEY_S},{7,2,2,KEY_D},{9,2,2,KEY_F},
    {11,2,2,KEY_G},{13,2,2,KEY_H},{15,2,2,KEY_J},{17,2,2,KEY_K},{19,2,2,KEY_L},
    {21,2,2,KEY_SEMICOLON},{23,2,2,KEY_APOSTROPHE},{25,2,3,KEY_BACKSLASH},
    {0,3,3,KEY_LEFTSHIFT},{3,3,2,KEY_Z},{5,3,2,KEY_X},{7,3,2,KEY_C},{9,3,2,KEY_V},
    {11,3,2,KEY_B},{13,3,2,KEY_N},{15,3,2,KEY_M},{17,3,2,KEY_COMMA},{19,3,2,KEY_DOT},
    {21,3,2,KEY_SLASH},{23,3,5,KEY_RIGHTSHIFT},
    {0,4,3,KEY_LEFTCTRL},{3,4,3,KEY_LEFTALT},{6,4,16,KEY_SPACE},
    {22,4,3,KEY_RIGHTALT},{25,4,3,KEY_RIGHTCTRL},
    {-1,-1,-1,-1}
};

/* ── USB device helpers ─────────────────────────────────────────────────── */
static DevType usb_classify(int fd) {
    unsigned long ev[NBITS(EV_MAX)]={0}, kb[NBITS(KEY_MAX)]={0},
                  rl[NBITS(REL_MAX)]={0}, ab[NBITS(ABS_MAX)]={0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev)<0) return DEV_UNKNOWN;
    bool hk=test_bit(EV_KEY,ev), hr=test_bit(EV_REL,ev), ha=test_bit(EV_ABS,ev);
    if (hk) ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kb)), kb);
    if (hr) ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rl)), rl);
    if (ha) ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ab)), ab);
    if (ha && hk && test_bit(ABS_X,ab) && test_bit(ABS_Y,ab) &&
        (test_bit(BTN_GAMEPAD,kb)||test_bit(BTN_SOUTH,kb)||
         test_bit(BTN_A,kb)||test_bit(BTN_JOYSTICK,kb)))
        return DEV_GAMEPAD;
    if (hr && hk && test_bit(REL_X,rl) && test_bit(REL_Y,rl) && test_bit(BTN_LEFT,kb))
        return DEV_MOUSE;
    if (hk) {
        static const int letter_keys[] = {
            KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
            KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
            KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M
        };
        int n=0;
        for(int k=0;k<(int)(sizeof(letter_keys)/sizeof(letter_keys[0]));k++)
            if(test_bit(letter_keys[k],kb)) n++;
        if(n>=20) return DEV_KEYBOARD;
    }
    return DEV_UNKNOWN;
}

static void usb_scan_devices(AppState *s) {
    s->usb_dev_cnt=0; s->usb_kbd_idx=s->usb_mou_idx=s->usb_pad_idx=-1;
    for (int i=0; i<MAX_EV_DEV && s->usb_dev_cnt<MAX_USB_DEV; i++) {
        char p[64]; snprintf(p,sizeof(p),"/dev/input/event%d",i);
        int fd=open(p, O_RDONLY|O_NONBLOCK); if(fd<0) continue;
        char nm[DEV_NAME_LEN]="Unknown";
        ioctl(fd, EVIOCGNAME(sizeof(nm)), nm);
        if (strstr(nm,"panjit")||strstr(nm,"Panjit")||strstr(nm,"PANJIT"))
            { close(fd); continue; }
        DevType t=usb_classify(fd); close(fd);
        if (t==DEV_UNKNOWN) continue;
        USBDev *d=&s->usb_devs[s->usb_dev_cnt];
        strncpy(d->name,nm,DEV_NAME_LEN-1); d->name[DEV_NAME_LEN-1]=0;
        strncpy(d->path,p,63); d->path[63]=0;
        d->type=t; d->ev_num=i; d->connected=true;
        if (t==DEV_KEYBOARD && s->usb_kbd_idx<0) s->usb_kbd_idx=s->usb_dev_cnt;
        else if (t==DEV_MOUSE && s->usb_mou_idx<0) s->usb_mou_idx=s->usb_dev_cnt;
        else if (t==DEV_GAMEPAD && s->usb_pad_idx<0) s->usb_pad_idx=s->usb_dev_cnt;
        s->usb_dev_cnt++;
    }
}

static int usb_open(AppState *s, int idx) {
    if (idx<0||idx>=s->usb_dev_cnt) return -1;
    if (s->usb_fd>=0) { close(s->usb_fd); s->usb_fd=-1; }
    s->usb_fd=open(s->usb_devs[idx].path, O_RDONLY|O_NONBLOCK);
    return s->usb_fd;
}

static void usb_close(AppState *s) {
    if (s->usb_fd>=0) { close(s->usb_fd); s->usb_fd=-1; }
}

static void usb_load_axes(AppState *s, int fd) {
    memset(s->usb_pad.amin,0,sizeof(s->usb_pad.amin));
    memset(s->usb_pad.amax,0,sizeof(s->usb_pad.amax));
    unsigned long ab[NBITS(ABS_MAX)]={0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ab)), ab)<0) return;
    for (int a=0; a<=ABS_MAX; a++) {
        if (test_bit(a,ab)) {
            struct input_absinfo inf;
            if (ioctl(fd, EVIOCGABS(a), &inf)==0)
                { s->usb_pad.amin[a]=inf.minimum; s->usb_pad.amax[a]=inf.maximum; }
        }
    }
}

static int usb_norm_axis(int v, int mn, int mx) {
    if (mx==mn) return 0;
    int mid=(mn+mx)/2, hr=(mx-mn)/2;
    if (!hr) return 0;
    int n=((v-mid)*1000)/hr;
    return n<-1000?-1000:n>1000?1000:n;
}

static int usb_norm_trig(int v, int mn, int mx) {
    if (mx==mn) return 0;
    int n=((v-mn)*1000)/(mx-mn);
    return n<0?0:n>1000?1000:n;
}

static void usb_add_log(KbdState *k, const char *m) {
    if (k->log_cnt>=LOG_LINES) {
        for(int i=0;i<LOG_LINES-1;i++) memcpy(k->log[i],k->log[i+1],LOG_LINE_LEN);
        k->log_cnt=LOG_LINES-1;
    }
    strncpy(k->log[k->log_cnt],m,LOG_LINE_LEN-1);
    k->log[k->log_cnt][LOG_LINE_LEN-1]=0;
    k->log_cnt++;
}

/* ── USB event processing ───────────────────────────────────────────────── */
static void usb_proc_kbd(AppState *s) {
    if (s->usb_fd<0) return;
    struct input_event e;
    while (read(s->usb_fd,&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type!=EV_KEY || e.code>=KEY_MAX) continue;
        const char *nm=usb_key_name(e.code);
        char nb[32]; if(!nm){snprintf(nb,32,"KEY_%d",e.code);nm=nb;}
        char lm[LOG_LINE_LEN];
        if (e.value==1) {
            s->usb_kbd.held[e.code]=true; s->usb_kbd.last_code=e.code;
            strncpy(s->usb_kbd.last_name,nm,31); s->usb_kbd.last_name[31]=0;
            snprintf(lm,sizeof(lm),"PRESS   %s (%d)",nm,e.code);
            usb_add_log(&s->usb_kbd,lm);
        } else if (e.value==0) {
            s->usb_kbd.held[e.code]=false;
            snprintf(lm,sizeof(lm),"RELEASE %s (%d)",nm,e.code);
            usb_add_log(&s->usb_kbd,lm);
        } else if (e.value==2) {
            snprintf(lm,sizeof(lm),"REPEAT  %s (%d)",nm,e.code);
            usb_add_log(&s->usb_kbd,lm);
        }
    }
}

static void usb_proc_mouse(AppState *s) {
    if (s->usb_fd<0) return;
    struct input_event e;
    while (read(s->usb_fd,&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type==EV_REL) {
            if (e.code==REL_X) {
                s->usb_mou.cx+=e.value;
                if(s->usb_mou.cx<0) s->usb_mou.cx=0;
                if(s->usb_mou.cx>=(int)g_fb->width) s->usb_mou.cx=(int)g_fb->width-1;
            } else if (e.code==REL_Y) {
                s->usb_mou.cy+=e.value;
                if(s->usb_mou.cy<0) s->usb_mou.cy=0;
                if(s->usb_mou.cy>=(int)g_fb->height) s->usb_mou.cy=(int)g_fb->height-1;
            } else if (e.code==REL_WHEEL) {
                s->usb_mou.scroll+=e.value; s->usb_mou.scroll_t=get_time_ms();
            }
        } else if (e.type==EV_KEY) {
            bool p=(e.value!=0);
            if (e.code==BTN_LEFT) s->usb_mou.bl=p;
            else if (e.code==BTN_MIDDLE) s->usb_mou.bm=p;
            else if (e.code==BTN_RIGHT) s->usb_mou.br=p;
        }
    }
}

static void usb_proc_pad(AppState *s) {
    if (s->usb_fd<0) return;
    struct input_event e;
    while (read(s->usb_fd,&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type==EV_ABS) {
            int c=e.code;
            if (c==ABS_X) s->usb_pad.lx=usb_norm_axis(e.value,s->usb_pad.amin[c],s->usb_pad.amax[c]);
            else if (c==ABS_Y) s->usb_pad.ly=usb_norm_axis(e.value,s->usb_pad.amin[c],s->usb_pad.amax[c]);
            else if (c==ABS_RX||c==ABS_Z) s->usb_pad.rx=usb_norm_axis(e.value,s->usb_pad.amin[c],s->usb_pad.amax[c]);
            else if (c==ABS_RY||c==ABS_RZ) s->usb_pad.ry=usb_norm_axis(e.value,s->usb_pad.amin[c],s->usb_pad.amax[c]);
            else if (c==ABS_HAT0X) s->usb_pad.dx=e.value>0?1:e.value<0?-1:0;
            else if (c==ABS_HAT0Y) s->usb_pad.dy=e.value>0?1:e.value<0?-1:0;
            else if (c==ABS_BRAKE) s->usb_pad.tl=usb_norm_trig(e.value,s->usb_pad.amin[c],s->usb_pad.amax[c]);
            else if (c==ABS_GAS) s->usb_pad.tr=usb_norm_trig(e.value,s->usb_pad.amin[c],s->usb_pad.amax[c]);
        } else if (e.type==EV_KEY) {
            int bi=-1;
            if (e.code>=BTN_GAMEPAD && e.code<BTN_GAMEPAD+16) bi=e.code-BTN_GAMEPAD;
            else if (e.code>=BTN_SOUTH && e.code<=BTN_THUMBR) bi=e.code-BTN_SOUTH;
            else if (e.code>=BTN_TRIGGER && e.code<BTN_TRIGGER+16) bi=e.code-BTN_TRIGGER;
            if (bi>=0 && bi<16) {
                s->usb_pad.btns[bi]=(e.value!=0);
                if (bi>=s->usb_pad.btn_cnt) s->usb_pad.btn_cnt=bi+1;
            }
            if (e.code==BTN_TL) s->usb_pad.tl=e.value?1000:0;
            if (e.code==BTN_TR) s->usb_pad.tr=e.value?1000:0;
        }
    }
}

static const char *usb_dtype_str(DevType t) {
    switch(t) { case DEV_KEYBOARD:return "KEYBOARD"; case DEV_MOUSE:return "MOUSE";
                case DEV_GAMEPAD:return "GAMEPAD"; default:return "UNKNOWN"; }
}

/* ── USB Draw: Main device list (inside tab content area) ────────────────── */
static void draw_usb(Framebuffer *fb, AppState *s) {
    int lx=CONTENT_LEFT, ly=CONTENT_Y+5;
    int lw=CONTENT_WIDTH, lh=CONTENT_H-70;

    fb_fill_rounded_rect(fb, lx, ly, lw, lh, 6, USB_COLOR_PANEL);
    fb_draw_rounded_rect(fb, lx, ly, lw, lh, 6, USB_COLOR_PANEL_BD);
    fb_draw_text(fb, lx+12, ly+10, "DETECTED USB DEVICES:", USB_COLOR_HDR, 2);

    if (s->usb_dev_cnt==0) {
        text_draw_centered(fb, CONTENT_LEFT+CONTENT_WIDTH/2, ly+lh/2-10,
                           "NO USB DEVICES DETECTED", USB_COLOR_DIM, 2);
        text_draw_centered(fb, CONTENT_LEFT+CONTENT_WIDTH/2, ly+lh/2+15,
                           "CONNECT A DEVICE AND TAP RESCAN", USB_COLOR_DIM, 2);
    } else {
        int ry0=ly+36, rh=36;
        for (int i=0; i<s->usb_dev_cnt && i<6; i++) {
            USBDev *d=&s->usb_devs[i]; int ry=ry0+i*rh;
            if (i%2==0) fb_fill_rect(fb, lx+4, ry, lw-8, rh-2, RGB(25,25,38));
            fb_fill_circle(fb, lx+20, ry+rh/2, 6,
                           d->connected?USB_COLOR_CONN:USB_COLOR_DISC);
            const char *ts=usb_dtype_str(d->type);
            uint32_t bc;
            switch(d->type) {
                case DEV_KEYBOARD: bc=RGB(0,150,200); break;
                case DEV_MOUSE: bc=RGB(200,150,0); break;
                case DEV_GAMEPAD: bc=RGB(0,180,80); break;
                default: bc=USB_COLOR_DIM; break;
            }
            int bw=text_measure_width(ts,2)+16;
            fb_fill_rounded_rect(fb, lx+36, ry+4, bw, rh-10, 4, bc);
            fb_draw_text(fb, lx+44, ry+10, ts, COLOR_WHITE, 2);
            char tn[48]; text_truncate(tn, d->name, lw-bw-170, 2);
            fb_draw_text(fb, lx+44+bw+10, ry+10, tn, COLOR_LABEL, 2);
            fb_draw_text(fb, lx+lw-140, ry+10, d->path, USB_COLOR_DIM, 1);
        }
    }

    int btn_y = CONTENT_Y + CONTENT_H - 55;
    int bw_btn=130, bh_btn=40, gap=10;
    (void)bh_btn;
    int total_w = 4*bw_btn + 3*gap;
    int sx = CONTENT_LEFT + (CONTENT_WIDTH - total_w)/2;

    usb_btn_rescan.x = sx;               usb_btn_rescan.y = btn_y;
    usb_btn_ktest.x = sx+bw_btn+gap;     usb_btn_ktest.y = btn_y;
    usb_btn_mtest.x = sx+2*(bw_btn+gap); usb_btn_mtest.y = btn_y;
    usb_btn_gtest.x = sx+3*(bw_btn+gap); usb_btn_gtest.y = btn_y;

    usb_btn_ktest.bg_color = s->usb_kbd_idx>=0 ? BTN_COLOR_PRIMARY : USB_COLOR_DIM;
    usb_btn_mtest.bg_color = s->usb_mou_idx>=0 ? BTN_COLOR_PRIMARY : USB_COLOR_DIM;
    usb_btn_gtest.bg_color = s->usb_pad_idx>=0 ? BTN_COLOR_PRIMARY : USB_COLOR_DIM;

    button_draw(fb, &usb_btn_rescan);
    button_draw(fb, &usb_btn_ktest);
    button_draw(fb, &usb_btn_mtest);
    button_draw(fb, &usb_btn_gtest);
}

/* ── USB Draw: Keyboard fullscreen ──────────────────────────────────────── */
static void draw_usb_kbd(Framebuffer *fb, AppState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &usb_btn_kback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "KEYBOARD TEST", COLOR_WHITE, 3);
    char inf[80];
    if (s->usb_kbd.last_code>0)
        snprintf(inf,sizeof(inf),"LAST KEY: %s (CODE %d)", s->usb_kbd.last_name, s->usb_kbd.last_code);
    else snprintf(inf,sizeof(inf),"PRESS ANY KEY ON USB KEYBOARD");
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+52, inf, COLOR_CYAN, 2);

    int kx=SCREEN_SAFE_LEFT+20, ky=SCREEN_SAFE_TOP+72, hu=26, kh=32, g=2;
    for (int i=0; usb_kblayout[i].code>=0; i++) {
        const LKey *k=&usb_kblayout[i];
        int x=kx+k->col*hu, y=ky+k->row*(kh+g), w=k->w*hu-g;
        bool h=(k->code<KEY_MAX)?s->usb_kbd.held[k->code]:false;
        fb_fill_rounded_rect(fb,x,y,w,kh,3,h?USB_COLOR_KEY_DN:USB_COLOR_KEY_UP);
        fb_draw_rounded_rect(fb,x,y,w,kh,3,USB_COLOR_KEY_BD);
        const char *l=usb_key_sname(k->code);
        if (l) {
            int tw=text_measure_width(l,1);
            fb_draw_text(fb,x+(w-tw)/2,y+(kh-8)/2,l,h?COLOR_WHITE:USB_COLOR_KEY_TXT,1);
        }
    }
    int lx2=SCREEN_SAFE_LEFT+20, ly2=ky+5*(kh+g)+8;
    int lw2=SCREEN_SAFE_WIDTH-40, lh2=SCREEN_SAFE_BOTTOM-ly2-10;
    if (lh2<30) lh2=30;
    fb_fill_rounded_rect(fb,lx2,ly2,lw2,lh2,4,USB_COLOR_LOG_BG);
    fb_draw_rounded_rect(fb,lx2,ly2,lw2,lh2,4,USB_COLOR_PANEL_BD);
    fb_draw_text(fb,lx2+8,ly2+4,"EVENT LOG:",USB_COLOR_DIM,1);
    int lny=ly2+16, llh=13, ml=(lh2-20)/llh;
    if(ml>LOG_LINES) ml=LOG_LINES; if(ml<0) ml=0;
    int st=s->usb_kbd.log_cnt-ml; if(st<0) st=0;
    for (int i=st; i<s->usb_kbd.log_cnt; i++)
        fb_draw_text(fb,lx2+8,lny+(i-st)*llh,s->usb_kbd.log[i],USB_COLOR_LOG_TXT,1);
}

/* ── USB Draw: Mouse fullscreen ─────────────────────────────────────────── */
static void draw_usb_mou(Framebuffer *fb, AppState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &usb_btn_mback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "MOUSE TEST", COLOR_WHITE, 3);
    int sw=screen_base_width, sh=screen_base_height;
    int ax=SCREEN_SAFE_LEFT+20, ay=SCREEN_SAFE_TOP+55, aw=sw-240, ah=sh-ay-20;
    fb_fill_rounded_rect(fb,ax,ay,aw,ah,6,RGB(15,15,25));
    fb_draw_rounded_rect(fb,ax,ay,aw,ah,6,USB_COLOR_PANEL_BD);
    for(int gx=ax+50;gx<ax+aw;gx+=50) fb_draw_line(fb,gx,ay+1,gx,ay+ah-2,RGB(25,25,35));
    for(int gy=ay+50;gy<ay+ah;gy+=50) fb_draw_line(fb,ax+1,gy,ax+aw-2,gy,RGB(25,25,35));

    int cx=s->usb_mou.cx, cy=s->usb_mou.cy;
    int dx=cx<ax+2?ax+2:cx>=ax+aw-2?ax+aw-3:cx;
    int dy=cy<ay+2?ay+2:cy>=ay+ah-2?ay+ah-3:cy;
    fb_draw_line(fb,dx-15,dy,dx+15,dy,USB_COLOR_CURSOR_C);
    fb_draw_line(fb,dx,dy-15,dx,dy+15,USB_COLOR_CURSOR_C);
    fb_fill_circle(fb,dx,dy,3,USB_COLOR_CURSOR_C);

    int px=ax+aw+15, pw=sw-px-15, py=ay;
    fb_draw_text(fb,px,py,"POSITION",USB_COLOR_HDR,2);
    char ps[32];
    snprintf(ps,32,"X: %d",cx); fb_draw_text(fb,px,py+22,ps,COLOR_WHITE,2);
    snprintf(ps,32,"Y: %d",cy); fb_draw_text(fb,px,py+42,ps,COLOR_WHITE,2);

    int bsy=py+80;
    fb_draw_text(fb,px,bsy,"BUTTONS",USB_COLOR_HDR,2);
    struct { bool h; const char *l; int o; } mb[3]={{s->usb_mou.bl,"L",0},{s->usb_mou.bm,"M",45},{s->usb_mou.br,"R",90}};
    for(int b=0;b<3;b++) {
        uint32_t c=mb[b].h?USB_COLOR_MBTN_ON:USB_COLOR_MBTN_OFF;
        int bx=px+20+mb[b].o, by2=bsy+40;
        fb_fill_circle(fb,bx,by2,14,c); fb_draw_circle(fb,bx,by2,14,USB_COLOR_PANEL_BD);
        fb_draw_text(fb,bx-4,by2+18,mb[b].l,COLOR_LABEL,2);
    }

    int ssy=bsy+95;
    fb_draw_text(fb,px,ssy,"SCROLL",USB_COLOR_HDR,2);
    snprintf(ps,32,"WHEEL: %d",s->usb_mou.scroll);
    fb_draw_text(fb,px,ssy+22,ps,COLOR_WHITE,2);
    uint32_t now=get_time_ms();
    if (now-s->usb_mou.scroll_t<300) fb_fill_circle(fb,px+pw-20,ssy+10,8,USB_COLOR_SCROLL);
    int brx=px, bry=ssy+45, brw=pw-10<20?20:pw-10, brh=20;
    fb_fill_rect(fb,brx,bry,brw,brh,RGB(40,40,40));
    int sv=s->usb_mou.scroll; if(sv>50) sv=50; if(sv<-50) sv=-50;
    int ix=brx+brw/2+(sv*brw/100);
    fb_fill_rect(fb,ix-3,bry,6,brh,USB_COLOR_SCROLL);
    fb_draw_line(fb,brx+brw/2,bry,brx+brw/2,bry+brh,USB_COLOR_DIM);
    text_draw_centered(fb,sw/2,sh-15,"MOVE MOUSE TO CONTROL CROSSHAIR",USB_COLOR_DIM,1);
}

/* ── USB Draw: Gamepad helpers ──────────────────────────────────────────── */
static void usb_draw_stick(Framebuffer *fb, int cx, int cy, int r,
                           int vx, int vy, const char *label) {
    fb_fill_circle(fb,cx,cy,r,USB_COLOR_STICK_BG);
    fb_draw_circle(fb,cx,cy,r,USB_COLOR_PANEL_BD);
    fb_draw_line(fb,cx-r,cy,cx+r,cy,RGB(40,40,55));
    fb_draw_line(fb,cx,cy-r,cx,cy+r,RGB(40,40,55));
    int dpx=cx+(vx*(r-6))/1000, dpy=cy+(vy*(r-6))/1000;
    fb_fill_circle(fb,dpx,dpy,6,USB_COLOR_STICK_DOT);
    fb_draw_circle(fb,dpx,dpy,6,COLOR_WHITE);
    text_draw_centered(fb,cx,cy+r+14,label,COLOR_LABEL,2);
}

static void usb_draw_dpad(Framebuffer *fb, int cx, int cy, int sz, int dx, int dy) {
    int arm=sz/3, hf=arm/2;
    fb_fill_rect(fb,cx-sz/2,cy-hf,sz,arm,RGB(50,50,60));
    fb_fill_rect(fb,cx-hf,cy-sz/2,arm,sz,RGB(50,50,60));
    if(dx<0) fb_fill_rect(fb,cx-sz/2,cy-hf,arm,arm,USB_COLOR_PAD_ON);
    if(dx>0) fb_fill_rect(fb,cx+sz/2-arm,cy-hf,arm,arm,USB_COLOR_PAD_ON);
    if(dy<0) fb_fill_rect(fb,cx-hf,cy-sz/2,arm,arm,USB_COLOR_PAD_ON);
    if(dy>0) fb_fill_rect(fb,cx-hf,cy+sz/2-arm,arm,arm,USB_COLOR_PAD_ON);
    fb_draw_rect(fb,cx-sz/2,cy-hf,sz,arm,USB_COLOR_PANEL_BD);
    fb_draw_rect(fb,cx-hf,cy-sz/2,arm,sz,USB_COLOR_PANEL_BD);
    text_draw_centered(fb,cx,cy+sz/2+14,"D-PAD",COLOR_LABEL,2);
}

/* ── USB Draw: Gamepad fullscreen ───────────────────────────────────────── */
static void draw_usb_pad(Framebuffer *fb, AppState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &usb_btn_gback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "GAMEPAD TEST", COLOR_WHITE, 3);
    int sw=screen_base_width, sr=55;
    int lcx=SCREEN_SAFE_LEFT+30+sr, lcy=SCREEN_SAFE_TOP+80+sr;
    usb_draw_stick(fb,lcx,lcy,sr,s->usb_pad.lx,s->usb_pad.ly,"LEFT STICK");
    usb_draw_dpad(fb,lcx,lcy+sr+70,70,s->usb_pad.dx,s->usb_pad.dy);
    int rcx=sw-SCREEN_SAFE_LEFT-30-sr;
    usb_draw_stick(fb,rcx,lcy,sr,s->usb_pad.rx,s->usb_pad.ry,"RIGHT STICK");

    int bcnt=s->usb_pad.btn_cnt<1?8:s->usb_pad.btn_cnt;
    if(bcnt>16) bcnt=16;
    int cols=4, bsz=28, bgap=6;
    int bax=lcx+sr+40, bay=SCREEN_SAFE_TOP+75;
    for(int i=0;i<bcnt;i++) {
        int col=i%cols, row=i/cols;
        int bx=bax+col*(bsz+bgap)+bsz/2;
        int by=bay+row*(bsz+bgap)+bsz/2;
        uint32_t c=s->usb_pad.btns[i]?USB_COLOR_PAD_ON:USB_COLOR_PAD_OFF;
        fb_fill_circle(fb,bx,by,bsz/2,c);
        fb_draw_circle(fb,bx,by,bsz/2,USB_COLOR_PANEL_BD);
        char bn[4]; snprintf(bn,4,"%d",i);
        int tw=text_measure_width(bn,1);
        fb_draw_text(fb,bx-tw/2,by-4,bn,COLOR_WHITE,1);
    }
    fb_draw_text(fb,bax,bay-16,"BUTTONS",USB_COLOR_HDR,2);

    int trw=150, trh=18, try2=SCREEN_SAFE_BOTTOM-80;
    int trlx=(sw/2)-trw-20, trrx=(sw/2)+20;
    fb_draw_text(fb,trlx,try2-16,"LT",COLOR_LABEL,1);
    fb_fill_rect(fb,trlx,try2,trw,trh,USB_COLOR_TRIG_BG);
    if(s->usb_pad.tl>0) fb_fill_rect(fb,trlx,try2,(s->usb_pad.tl*trw)/1000,trh,USB_COLOR_TRIG_FILL);
    fb_draw_rect(fb,trlx,try2,trw,trh,USB_COLOR_PANEL_BD);
    char tv[16]; snprintf(tv,16,"%d%%",s->usb_pad.tl/10);
    fb_draw_text(fb,trlx+trw+8,try2+4,tv,COLOR_WHITE,1);

    fb_draw_text(fb,trrx,try2-16,"RT",COLOR_LABEL,1);
    fb_fill_rect(fb,trrx,try2,trw,trh,USB_COLOR_TRIG_BG);
    if(s->usb_pad.tr>0) fb_fill_rect(fb,trrx,try2,(s->usb_pad.tr*trw)/1000,trh,USB_COLOR_TRIG_FILL);
    fb_draw_rect(fb,trrx,try2,trw,trh,USB_COLOR_PANEL_BD);
    snprintf(tv,16,"%d%%",s->usb_pad.tr/10);
    fb_draw_text(fb,trrx+trw+8,try2+4,tv,COLOR_WHITE,1);

    int rvx=bax, rvy=bay+(bcnt/cols+1)*(bsz+bgap)+20;
    fb_draw_text(fb,rvx,rvy,"RAW AXES",USB_COLOR_HDR,2);
    char rv[48];
    snprintf(rv,48,"LX:%+5d LY:%+5d",s->usb_pad.lx,s->usb_pad.ly);
    fb_draw_text(fb,rvx,rvy+20,rv,COLOR_LABEL,1);
    snprintf(rv,48,"RX:%+5d RY:%+5d",s->usb_pad.rx,s->usb_pad.ry);
    fb_draw_text(fb,rvx,rvy+34,rv,COLOR_LABEL,1);
    snprintf(rv,48,"DX:%+2d DY:%+2d",s->usb_pad.dx,s->usb_pad.dy);
    fb_draw_text(fb,rvx,rvy+48,rv,COLOR_LABEL,1);

    text_draw_centered(fb,sw/2,SCREEN_SAFE_BOTTOM-15,
                       "USE GAMEPAD CONTROLS TO TEST",USB_COLOR_DIM,1);
}

/* ── USB Tab: UI creation ───────────────────────────────────────────────── */
static void create_usb_ui(void) {
    int bw=130, bh=40;
    button_init_full(&usb_btn_rescan, 0, 0, bw, bh, "RESCAN",
                     BTN_COLOR_INFO, COLOR_WHITE, RGB(0,200,255), 2);
    button_init_full(&usb_btn_ktest, 0, 0, bw, bh, "KBD TEST",
                     BTN_COLOR_PRIMARY, COLOR_WHITE, RGB(0,200,80), 2);
    button_init_full(&usb_btn_mtest, 0, 0, bw, bh, "MOUSE TEST",
                     BTN_COLOR_PRIMARY, COLOR_WHITE, RGB(0,200,80), 2);
    button_init_full(&usb_btn_gtest, 0, 0, bw, bh, "PAD TEST",
                     BTN_COLOR_PRIMARY, COLOR_WHITE, RGB(0,200,80), 2);
    button_init_full(&usb_btn_kback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
    button_init_full(&usb_btn_mback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
    button_init_full(&usb_btn_gback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
}

/* ── USB Tab: input handling (tab content area, main screen only) ────────── */
static void handle_usb_input(AppState *state, int tx, int ty,
                             bool touching, uint32_t now) {
    if (state->usb_scr != USB_SCR_MAIN) return;

    if (button_update(&usb_btn_rescan, tx, ty, touching, now))
        usb_scan_devices(state);

    if (state->usb_kbd_idx >= 0 &&
        button_update(&usb_btn_ktest, tx, ty, touching, now)) {
        memset(&state->usb_kbd, 0, sizeof(state->usb_kbd));
        if (usb_open(state, state->usb_kbd_idx) >= 0)
            state->usb_scr = USB_SCR_KEYBOARD;
    }
    if (state->usb_mou_idx >= 0 &&
        button_update(&usb_btn_mtest, tx, ty, touching, now)) {
        memset(&state->usb_mou, 0, sizeof(state->usb_mou));
        state->usb_mou.cx = (int)g_fb->width / 2;
        state->usb_mou.cy = (int)g_fb->height / 2;
        if (usb_open(state, state->usb_mou_idx) >= 0)
            state->usb_scr = USB_SCR_MOUSE;
    }
    if (state->usb_pad_idx >= 0 &&
        button_update(&usb_btn_gtest, tx, ty, touching, now)) {
        memset(&state->usb_pad, 0, sizeof(state->usb_pad));
        state->usb_pad.btn_cnt = 8;
        if (usb_open(state, state->usb_pad_idx) >= 0) {
            usb_load_axes(state, state->usb_fd);
            state->usb_scr = USB_SCR_GAMEPAD;
        }
    }
}

/* ── USB Tab: fullscreen runner (keyboard/mouse/gamepad test screens) ────── */
static void run_usb_fullscreen(Framebuffer *fb, TouchInput *touch, AppState *state) {
    while (running && state->usb_scr != USB_SCR_MAIN) {
        uint32_t now = get_time_ms();

        switch (state->usb_scr) {
            case USB_SCR_KEYBOARD: usb_proc_kbd(state); break;
            case USB_SCR_MOUSE:    usb_proc_mouse(state); break;
            case USB_SCR_GAMEPAD:  usb_proc_pad(state); break;
            default: break;
        }

        switch (state->usb_scr) {
            case USB_SCR_KEYBOARD: draw_usb_kbd(fb, state); break;
            case USB_SCR_MOUSE:    draw_usb_mou(fb, state); break;
            case USB_SCR_GAMEPAD:  draw_usb_pad(fb, state); break;
            default: break;
        }
        fb_swap(fb);

        touch_poll(touch);
        TouchState ts = touch_get_state(touch);
        int tx = ts.x, ty = ts.y;
        bool touching = ts.pressed || ts.held;

        switch (state->usb_scr) {
        case USB_SCR_KEYBOARD:
            if (button_update(&usb_btn_kback, tx, ty, touching, now)) {
                usb_close(state); state->usb_scr = USB_SCR_MAIN;
            }
            break;
        case USB_SCR_MOUSE:
            if (button_update(&usb_btn_mback, tx, ty, touching, now)) {
                usb_close(state); state->usb_scr = USB_SCR_MAIN;
            }
            break;
        case USB_SCR_GAMEPAD:
            if (button_update(&usb_btn_gback, tx, ty, touching, now)) {
                usb_close(state); state->usb_scr = USB_SCR_MAIN;
            }
            break;
        default: break;
        }

        usleep(16000);
    }
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
    } else if (state->active_tab == TAB_USB) {
        run_usb_fullscreen(fb, touch, state);
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
    state.usb_scr = USB_SCR_MAIN;
    state.usb_fd = -1;
    state.usb_kbd_idx = state.usb_mou_idx = state.usb_pad_idx = -1;

    create_tab_bar();
    create_settings_ui(&state);
    create_diagnostics_ui();
    create_tests_ui();
    create_calibration_ui();
    create_usb_ui();
    usb_scan_devices(&state);

    while (running) {
        uint32_t now = get_time_ms();

        if (state.status_msg[0] && now - state.status_time_ms > 2000)
            state.status_msg[0] = '\0';

        bool fullscreen = (state.active_tab == TAB_TESTS && state.test_sub == TEST_RUNNING)
                       || (state.active_tab == TAB_CALIBRATION && state.calib_sub != CALIB_IDLE)
                       || (state.active_tab == TAB_USB && state.usb_scr != USB_SCR_MAIN);

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
            case TAB_USB:         draw_usb(&fb, &state);         break;
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
                case TAB_USB:         handle_usb_input(&state, tx, ty, touching, now);      break;
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
