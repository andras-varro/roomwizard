/**
 * Hardware Configuration GUI for RoomWizard
 *
 * Touch-based settings tool for audio, LED, and display configuration.
 * Reads/writes persistent config via the common config library.
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include "common/hardware.h"
#include "common/common.h"
#include "common/audio.h"
#include "common/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

/* ── Screen constants ───────────────────────────────────────────────────── */

#define SCREEN_W 800
#define SCREEN_H 480

/* ── Color palette ──────────────────────────────────────────────────────── */

#define COLOR_SECTION_LINE   RGB(60, 60, 80)
#define COLOR_LABEL          RGB(200, 200, 200)
#define COLOR_BAR_BG         RGB(40, 40, 40)
#define COLOR_BAR_FILL       RGB(0, 180, 60)
#define COLOR_HEADER_BG      RGB(30, 30, 40)

/* ── Layout Y positions ────────────────────────────────────────────────── */

#define TITLE_Y              (SCREEN_SAFE_TOP + 5)
#define SECTION_AUDIO_Y      80
#define SECTION_LED_Y        155
#define SECTION_DISPLAY_Y    270
#define ACTION_BTN_Y         370

/* ── Brightness bar geometry ────────────────────────────────────────────── */

#define BAR_WIDTH  300
#define BAR_HEIGHT 20

/* ── Defaults ───────────────────────────────────────────────────────────── */

#define DEFAULT_AUDIO_ENABLED     true
#define DEFAULT_LED_ENABLED       true
#define DEFAULT_LED_BRIGHTNESS    100
#define DEFAULT_BACKLIGHT_BRIGHTNESS 100

/* ── Global state ───────────────────────────────────────────────────────── */

static volatile bool running = true;

static void signal_handler(int sig) {
    (void)sig;
    running = false;
}

/* ── Test functions (bypass config-gated APIs) ──────────────────────────── */

static void do_audio_test(void) {
    /* Bypass config-gated audio_init — open DSP directly for test */
    Audio test_audio;
    memset(&test_audio, 0, sizeof(test_audio));
    test_audio.dsp_fd = open("/dev/dsp", O_WRONLY | O_NONBLOCK);
    if (test_audio.dsp_fd < 0) return;
    test_audio.available = true;
    test_audio.sample_rate = 44100;

    /* Configure DSP */
    int rate = 44100;
    ioctl(test_audio.dsp_fd, SNDCTL_DSP_SPEED, &rate);
    int fmt = AFMT_S16_LE;
    ioctl(test_audio.dsp_fd, SNDCTL_DSP_SETFMT, &fmt);
    int stereo = 1;
    ioctl(test_audio.dsp_fd, SNDCTL_DSP_STEREO, &stereo);

    /* Enable amp */
    FILE *f = fopen("/sys/class/gpio/gpio12/direction", "w");
    if (f) { fputs("out", f); fclose(f); }
    f = fopen("/sys/class/gpio/gpio12/value", "w");
    if (f) { fputs("1", f); fclose(f); }

    /* Play test beep */
    audio_tone(&test_audio, 880, 200);
    usleep(250000);
    audio_tone(&test_audio, 1320, 200);

    close(test_audio.dsp_fd);
}

static void do_led_test(int brightness_pct) {
    /* Write directly to sysfs, bypassing config-gated hw_set_led */
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", brightness_pct);

    FILE *f;
    /* Red on */
    f = fopen("/sys/class/leds/red_led/brightness", "w");
    if (f) { fputs(buf, f); fclose(f); }
    /* Green on */
    f = fopen("/sys/class/leds/green_led/brightness", "w");
    if (f) { fputs(buf, f); fclose(f); }

    usleep(500000);  /* 500ms */

    /* Off */
    f = fopen("/sys/class/leds/red_led/brightness", "w");
    if (f) { fputs("0", f); fclose(f); }
    f = fopen("/sys/class/leds/green_led/brightness", "w");
    if (f) { fputs("0", f); fclose(f); }
}

static void apply_backlight(int brightness_pct) {
    /* Write directly to sysfs for live preview */
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", brightness_pct);
    FILE *f = fopen("/sys/class/backlight/pwm-backlight/brightness", "w");
    if (f) { fputs(buf, f); fclose(f); }
}

/* ── Drawing helpers ────────────────────────────────────────────────────── */

static void draw_section_header(Framebuffer *fb, int y, const char *title) {
    int left = SCREEN_SAFE_LEFT + 10;
    int right = SCREEN_SAFE_LEFT + SCREEN_SAFE_WIDTH - 10;

    /* Draw label */
    fb_draw_text(fb, left, y, title, COLOR_CYAN, 2);

    /* Draw horizontal line after the label text */
    int text_w = text_measure_width(title, 2);
    int line_x = left + text_w + 12;
    if (line_x < right) {
        int line_y = y + 8;  /* vertically center with text */
        fb_draw_line(fb, line_x, line_y, right, line_y, COLOR_SECTION_LINE);
    }
}

static void draw_brightness_bar(Framebuffer *fb, int x, int y, int value,
                                 int min_val, int max_val) {
    /* Background */
    fb_fill_rect(fb, x, y, BAR_WIDTH, BAR_HEIGHT, COLOR_BAR_BG);

    /* Filled portion */
    int range = max_val - min_val;
    int fill_w = 0;
    if (range > 0)
        fill_w = ((value - min_val) * BAR_WIDTH) / range;
    if (fill_w > BAR_WIDTH) fill_w = BAR_WIDTH;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > 0)
        fb_fill_rect(fb, x, y, fill_w, BAR_HEIGHT, COLOR_BAR_FILL);

    /* Percentage label */
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", value);
    fb_draw_text(fb, x + BAR_WIDTH + 10, y + 2, pct, COLOR_WHITE, 2);
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void) {
    /* Signal handling */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize framebuffer */
    Framebuffer fb;
    if (fb_init(&fb, "/dev/fb0") < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }

    /* Initialize touch input */
    TouchInput touch;
    if (touch_init(&touch, "/dev/input/event0") < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&fb);
        return 1;
    }

    /* Initialize hardware */
    hw_init();
    hw_set_backlight(100);

    /* ── Load config ────────────────────────────────────────────── */
    Config cfg;
    config_init(&cfg);
    config_load(&cfg);  /* OK if file missing — defaults used */

    /* UI state from config */
    bool audio_enabled    = config_get_bool(&cfg, "audio_enabled", DEFAULT_AUDIO_ENABLED);
    bool led_enabled      = config_get_bool(&cfg, "led_enabled", DEFAULT_LED_ENABLED);
    int  led_brightness   = config_get_int(&cfg, "led_brightness", DEFAULT_LED_BRIGHTNESS);
    int  backlight_brightness = config_get_int(&cfg, "backlight_brightness", DEFAULT_BACKLIGHT_BRIGHTNESS);

    /* Clamp values */
    if (led_brightness < 0)   led_brightness = 0;
    if (led_brightness > 100) led_brightness = 100;
    if (backlight_brightness < 20)  backlight_brightness = 20;
    if (backlight_brightness > 100) backlight_brightness = 100;

    /* ── Create UI elements ─────────────────────────────────────── */

    /* Toggle switches */
    ToggleSwitch audio_toggle;
    toggle_init(&audio_toggle, SCREEN_SAFE_LEFT + 15, SECTION_AUDIO_Y + 28,
                60, 28, "AUDIO ENABLED", audio_enabled);

    ToggleSwitch led_toggle;
    toggle_init(&led_toggle, SCREEN_SAFE_LEFT + 15, SECTION_LED_Y + 28,
                60, 28, "LED EFFECTS", led_enabled);

    /* LED brightness +/- buttons */
    int led_bar_x = SCREEN_SAFE_LEFT + 200;
    int led_bar_y = SECTION_LED_Y + 70;

    Button led_minus_btn;
    button_init_full(&led_minus_btn, led_bar_x - 55, led_bar_y - 5,
                     45, 30, "-", RGB(80, 80, 80), COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);

    Button led_plus_btn;
    button_init_full(&led_plus_btn, led_bar_x + BAR_WIDTH + 70, led_bar_y - 5,
                     45, 30, "+", RGB(80, 80, 80), COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);

    /* Backlight brightness +/- buttons */
    int bl_bar_x = SCREEN_SAFE_LEFT + 200;
    int bl_bar_y = SECTION_DISPLAY_Y + 35;

    Button bl_minus_btn;
    button_init_full(&bl_minus_btn, bl_bar_x - 55, bl_bar_y - 5,
                     45, 30, "-", RGB(80, 80, 80), COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);

    Button bl_plus_btn;
    button_init_full(&bl_plus_btn, bl_bar_x + BAR_WIDTH + 70, bl_bar_y - 5,
                     45, 30, "+", RGB(80, 80, 80), COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);

    /* Test buttons */
    Button test_audio_btn;
    button_init_full(&test_audio_btn,
                     SCREEN_SAFE_LEFT + SCREEN_SAFE_WIDTH - 110,
                     SECTION_AUDIO_Y + 25, 90, 34, "TEST",
                     RGB(0, 100, 140), COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);

    Button test_led_btn;
    button_init_full(&test_led_btn,
                     SCREEN_SAFE_LEFT + SCREEN_SAFE_WIDTH - 110,
                     SECTION_LED_Y + 25, 90, 34, "TEST",
                     RGB(0, 100, 140), COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);

    /* Action buttons */
    int action_center = SCREEN_SAFE_LEFT + SCREEN_SAFE_WIDTH / 2;

    Button save_btn;
    button_init_full(&save_btn, action_center - 230, ACTION_BTN_Y,
                     160, 50, "SAVE", BTN_COLOR_PRIMARY, COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 3);

    Button reset_btn;
    button_init_full(&reset_btn, action_center + 30, ACTION_BTN_Y,
                     220, 50, "RESET DEFAULTS", BTN_COLOR_DANGER, COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);

    /* Exit button (top-right) */
    Button exit_btn;
    button_init_full(&exit_btn,
                     SCREEN_SAFE_LEFT + SCREEN_SAFE_WIDTH - 65,
                     SCREEN_SAFE_TOP + 5, 55, 40, "X",
                     BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR, 2);

    /* Status message */
    char status_msg[64] = "";
    uint32_t status_time_ms = 0;

    /* ── Main loop ──────────────────────────────────────────────── */
    while (running) {
        uint32_t now = get_time_ms();

        /* Clear status message after 2 seconds */
        if (status_msg[0] && (now - status_time_ms > 2000)) {
            status_msg[0] = '\0';
        }

        /* ── Draw ───────────────────────────────────────────────── */
        fb_clear(&fb, COLOR_BLACK);

        /* Header */
        text_draw_centered(&fb, SCREEN_SAFE_LEFT + SCREEN_SAFE_WIDTH / 2,
                           TITLE_Y + 12, "SETTINGS", COLOR_WHITE, 3);

        /* Exit button */
        button_draw_exit(&fb, &exit_btn);

        /* ── AUDIO section ──────────────────────────────────────── */
        draw_section_header(&fb, SECTION_AUDIO_Y, "AUDIO");
        toggle_draw(&fb, &audio_toggle);
        button_draw(&fb, &test_audio_btn);

        /* ── LED section ────────────────────────────────────────── */
        draw_section_header(&fb, SECTION_LED_Y, "LEDS");
        toggle_draw(&fb, &led_toggle);
        button_draw(&fb, &test_led_btn);

        /* LED brightness row */
        fb_draw_text(&fb, SCREEN_SAFE_LEFT + 15, led_bar_y + 2,
                     "LED BRIGHTNESS", COLOR_LABEL, 2);
        button_draw(&fb, &led_minus_btn);
        draw_brightness_bar(&fb, led_bar_x, led_bar_y, led_brightness, 0, 100);
        button_draw(&fb, &led_plus_btn);

        /* ── DISPLAY section ────────────────────────────────────── */
        draw_section_header(&fb, SECTION_DISPLAY_Y, "DISPLAY");

        fb_draw_text(&fb, SCREEN_SAFE_LEFT + 15, bl_bar_y + 2,
                     "BACKLIGHT", COLOR_LABEL, 2);
        button_draw(&fb, &bl_minus_btn);
        draw_brightness_bar(&fb, bl_bar_x, bl_bar_y, backlight_brightness, 20, 100);
        button_draw(&fb, &bl_plus_btn);

        /* ── Action buttons ─────────────────────────────────────── */
        button_draw(&fb, &save_btn);
        button_draw(&fb, &reset_btn);

        /* ── Status message ─────────────────────────────────────── */
        if (status_msg[0]) {
            uint32_t status_color = COLOR_GREEN;
            if (strstr(status_msg, "DEFAULTS"))
                status_color = COLOR_CYAN;
            text_draw_centered(&fb, SCREEN_SAFE_LEFT + SCREEN_SAFE_WIDTH / 2,
                               ACTION_BTN_Y + 65, status_msg, status_color, 2);
        }

        fb_swap(&fb);

        /* ── Input ──────────────────────────────────────────────── */
        touch_poll(&touch);
        TouchState ts = touch_get_state(&touch);

        int tx = ts.x;
        int ty = ts.y;
        bool touching = ts.pressed || ts.held;

        /* Toggle switches */
        if (toggle_check_press(&audio_toggle, tx, ty, touching, now)) {
            audio_enabled = audio_toggle.state;
        }
        if (toggle_check_press(&led_toggle, tx, ty, touching, now)) {
            led_enabled = led_toggle.state;
        }

        /* LED brightness -/+ */
        if (button_update(&led_minus_btn, tx, ty, touching, now)) {
            led_brightness -= 10;
            if (led_brightness < 0) led_brightness = 0;
            do_led_test(led_brightness);  /* Brief flash at new brightness */
        }
        if (button_update(&led_plus_btn, tx, ty, touching, now)) {
            led_brightness += 10;
            if (led_brightness > 100) led_brightness = 100;
            do_led_test(led_brightness);  /* Brief flash at new brightness */
        }

        /* Backlight brightness -/+ */
        if (button_update(&bl_minus_btn, tx, ty, touching, now)) {
            backlight_brightness -= 10;
            if (backlight_brightness < 20) backlight_brightness = 20;
            apply_backlight(backlight_brightness);
        }
        if (button_update(&bl_plus_btn, tx, ty, touching, now)) {
            backlight_brightness += 10;
            if (backlight_brightness > 100) backlight_brightness = 100;
            apply_backlight(backlight_brightness);
        }

        /* Test buttons */
        if (button_update(&test_audio_btn, tx, ty, touching, now)) {
            do_audio_test();
        }
        if (button_update(&test_led_btn, tx, ty, touching, now)) {
            do_led_test(led_brightness);
        }

        /* Save button */
        if (button_update(&save_btn, tx, ty, touching, now)) {
            config_set_bool(&cfg, "audio_enabled", audio_enabled);
            config_set_bool(&cfg, "led_enabled", led_enabled);
            config_set_int(&cfg, "led_brightness", led_brightness);
            config_set_int(&cfg, "backlight_brightness", backlight_brightness);
            config_save(&cfg);
            snprintf(status_msg, sizeof(status_msg), "SAVED!");
            status_time_ms = now;
        }

        /* Reset button */
        if (button_update(&reset_btn, tx, ty, touching, now)) {
            config_clear(&cfg);
            audio_enabled = DEFAULT_AUDIO_ENABLED;
            led_enabled = DEFAULT_LED_ENABLED;
            led_brightness = DEFAULT_LED_BRIGHTNESS;
            backlight_brightness = DEFAULT_BACKLIGHT_BRIGHTNESS;
            /* Update toggle switch states */
            audio_toggle.state = audio_enabled;
            led_toggle.state = led_enabled;
            /* Apply backlight live */
            apply_backlight(backlight_brightness);
            snprintf(status_msg, sizeof(status_msg), "DEFAULTS RESTORED");
            status_time_ms = now;
        }

        /* Exit button */
        if (button_update(&exit_btn, tx, ty, touching, now)) {
            running = false;
        }

        usleep(16000);  /* ~60fps */
    }

    /* ── Cleanup ────────────────────────────────────────────────── */
    hw_leds_off();
    hw_reload_config();      /* Re-read config so exit applies saved values, not stale startup cache */
    hw_set_backlight(100);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    touch_close(&touch);
    fb_close(&fb);
    return 0;
}
