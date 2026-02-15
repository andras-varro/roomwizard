/**
 * Hardware Test GUI for RoomWizard
 * 
 * Touch-based hardware diagnostic tool using UI framework
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include "common/hardware.h"
#include "common/common.h"
#include "common/ui_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Test states
typedef enum {
    TEST_MENU,
    TEST_LED_RED,
    TEST_LED_GREEN,
    TEST_LED_BOTH,
    TEST_BACKLIGHT,
    TEST_PULSE,
    TEST_BLINK,
    TEST_COLORS
} TestState;

// Test names
const char *test_names[] = {
    "RED LED",
    "GREEN LED",
    "BOTH LEDS",
    "BACKLIGHT",
    "PULSE",
    "BLINK",
    "COLORS"
};

// Draw test menu using UI framework
void draw_test_menu(Framebuffer *fb, UILayout *layout, Button *buttons, Button *exit_btn, int selected) {
    fb_clear(fb, COLOR_BLACK);
    
    // Title
    text_draw_centered(fb, 400, 30, "HARDWARE TEST", COLOR_WHITE, 3);
    
    // Draw exit button
    button_draw_exit(fb, exit_btn);
    
    // Draw test buttons using layout
    for (int i = 0; i < 7; i++) {
        int x, y, w, h;
        if (ui_layout_get_item_position(layout, i, &x, &y, &w, &h)) {
            // Update button position
            buttons[i].x = x;
            buttons[i].y = y;
            buttons[i].width = w;
            buttons[i].height = h;
            
            // Highlight selected
            if (i == selected) {
                buttons[i].visual_state = BTN_STATE_HIGHLIGHTED;
            } else {
                buttons[i].visual_state = BTN_STATE_NORMAL;
            }
            
            button_draw(fb, &buttons[i]);
        }
    }
    
    // Draw scroll indicators if needed
    ui_layout_draw_scroll_indicators(fb, layout);
    
    fb_swap(fb);
}

// Draw test screen with progress bar
void draw_test_screen(Framebuffer *fb, const char *title, const char *status, int progress) {
    fb_clear(fb, COLOR_BLACK);
    
    // Title
    text_draw_centered(fb, 400, 50, title, COLOR_WHITE, 3);
    
    // Status text
    if (status) {
        text_draw_centered(fb, 400, 150, status, COLOR_CYAN, 2);
    }
    
    // Progress bar
    if (progress >= 0) {
        int bar_width = 600;
        int bar_height = 40;
        int bar_x = (800 - bar_width) / 2;
        int bar_y = 220;
        
        // Background
        fb_draw_rect(fb, bar_x, bar_y, bar_width, bar_height, RGB(51, 51, 51));
        
        // Progress fill
        int fill_width = (bar_width * progress) / 100;
        fb_draw_rect(fb, bar_x, bar_y, fill_width, bar_height, COLOR_GREEN);
        
        // Percentage text
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", progress);
        text_draw_centered(fb, 400, bar_y + 20, pct, COLOR_WHITE, 2);
    }
    
    // Instructions
    text_draw_centered(fb, 400, 400, "TOUCH TO RETURN", RGB(136, 136, 136), 2);
    
    fb_swap(fb);
}

// Check if touch occurred (non-blocking)
bool check_touch(TouchInput *touch, int *x, int *y) {
    if (touch_poll(touch) > 0) {
        TouchState state = touch_get_state(touch);
        if (state.pressed) {
            *x = state.x;
            *y = state.y;
            return true;
        }
    }
    return false;
}

// Run red LED test
void test_red_led(Framebuffer *fb, TouchInput *touch) {
    for (int i = 0; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "RED LED TEST", status, i);
        hw_set_red_led(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_red_led(0);
            return;
        }
    }
    for (int i = 0; i < 20; i++) {
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_red_led(0);
            return;
        }
    }
    for (int i = 100; i >= 0; i -= 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "RED LED TEST", status, 100 - i);
        hw_set_red_led(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_red_led(0);
            return;
        }
    }
    hw_set_red_led(0);
    draw_test_screen(fb, "RED LED TEST", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run green LED test
void test_green_led(Framebuffer *fb, TouchInput *touch) {
    for (int i = 0; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "GREEN LED TEST", status, i);
        hw_set_green_led(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_green_led(0);
            return;
        }
    }
    for (int i = 0; i < 20; i++) {
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_green_led(0);
            return;
        }
    }
    for (int i = 100; i >= 0; i -= 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "GREEN LED TEST", status, 100 - i);
        hw_set_green_led(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_green_led(0);
            return;
        }
    }
    hw_set_green_led(0);
    draw_test_screen(fb, "GREEN LED TEST", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run both LEDs test
void test_both_leds(Framebuffer *fb, TouchInput *touch) {
    for (int i = 0; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "BOTH LEDS: %d%%", i);
        draw_test_screen(fb, "BOTH LEDS TEST", status, i);
        hw_set_leds(i, i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_leds_off();
            return;
        }
    }
    for (int i = 0; i < 20; i++) {
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_leds_off();
            return;
        }
    }
    for (int cycle = 0; cycle < 5; cycle++) {
        draw_test_screen(fb, "BOTH LEDS TEST", "RED ONLY", 50);
        hw_set_leds(100, 0);
        for (int i = 0; i < 10; i++) {
            usleep(50000);
            int x, y;
            if (check_touch(touch, &x, &y)) {
                hw_leds_off();
                return;
            }
        }
        draw_test_screen(fb, "BOTH LEDS TEST", "GREEN ONLY", 50);
        hw_set_leds(0, 100);
        for (int i = 0; i < 10; i++) {
            usleep(50000);
            int x, y;
            if (check_touch(touch, &x, &y)) {
                hw_leds_off();
                return;
            }
        }
    }
    hw_leds_off();
    draw_test_screen(fb, "BOTH LEDS TEST", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run backlight test
void test_backlight(Framebuffer *fb, TouchInput *touch) {
    int original = hw_get_backlight();
    for (int i = 100; i >= 20; i -= 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "BACKLIGHT TEST", status, 100 - i);
        hw_set_backlight(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_backlight(original);
            return;
        }
    }
    for (int i = 20; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "BRIGHTNESS: %d%%", i);
        draw_test_screen(fb, "BACKLIGHT TEST", status, i);
        hw_set_backlight(i);
        usleep(50000);
        int x, y;
        if (check_touch(touch, &x, &y)) {
            hw_set_backlight(original);
            return;
        }
    }
    hw_set_backlight(original);
    draw_test_screen(fb, "BACKLIGHT TEST", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run pulse test
void test_pulse(Framebuffer *fb, TouchInput *touch) {
    draw_test_screen(fb, "PULSE EFFECT", "PULSING GREEN LED...", 50);
    hw_pulse_led(LED_GREEN, 3000, 100);
    draw_test_screen(fb, "PULSE EFFECT", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run blink test
void test_blink(Framebuffer *fb, TouchInput *touch) {
    draw_test_screen(fb, "BLINK EFFECT", "BLINKING RED LED...", 50);
    hw_blink_led(LED_RED, 10, 200, 200, 100);
    draw_test_screen(fb, "BLINK EFFECT", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

// Run color cycle test
void test_colors(Framebuffer *fb, TouchInput *touch) {
    const char *color_names[] = {"RED", "ORANGE", "YELLOW", "GREEN", "OFF"};
    const struct { uint8_t r; uint8_t g; } colors[] = {
        {100, 0}, {100, 50}, {100, 100}, {0, 100}, {0, 0}
    };
    for (int i = 0; i < 5; i++) {
        draw_test_screen(fb, "COLOR CYCLE", color_names[i], (i * 100) / 4);
        hw_set_leds(colors[i].r, colors[i].g);
        for (int j = 0; j < 20; j++) {
            usleep(50000);
            int x, y;
            if (check_touch(touch, &x, &y)) {
                hw_leds_off();
                return;
            }
        }
    }
    draw_test_screen(fb, "COLOR CYCLE", "COMPLETE!", 100);
    int x, y;
    while (!check_touch(touch, &x, &y)) usleep(10000);
}

int main(void) {
    Framebuffer fb;
    if (fb_init(&fb, "/dev/fb0") < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    
    TouchInput touch;
    if (touch_init(&touch, "/dev/input/event0") < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&fb);
        return 1;
    }
    
    if (hw_init() < 0) {
        fprintf(stderr, "Warning: Hardware initialization issues\n");
    }
    
    // Create UI layout (grid: 4 columns, 2 rows for 7 items)
    // Screen: 800x480, margins: left=10, top=80, right=10, bottom=20
    // Item size: 180x80, spacing: 10x20
    // Width calc: 10 + (180*4) + (10*3) + 10 = 10 + 720 + 30 + 10 = 770 (fits in 800)
    UILayout layout;
    ui_layout_init_grid(&layout, 800, 480, 4, 180, 80, 10, 20, 10, 80, 10, 20);
    ui_layout_update(&layout, 7);
    
    // Create test buttons using unified Button type
    Button buttons[7];
    for (int i = 0; i < 7; i++) {
        button_init_full(&buttons[i], 0, 0, 180, 80, test_names[i],
                        RGB(34, 34, 34), COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);
    }
    
    // Create exit button
    Button exit_btn;
    button_init_full(&exit_btn, 730, 10, 60, 50, "X",
                    BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR, 2);
    
    TestState state = TEST_MENU;
    int selected = 0;
    bool running = true;
    uint32_t last_time = 0;
    
    while (running) {
        if (state == TEST_MENU) {
            draw_test_menu(&fb, &layout, buttons, &exit_btn, selected);
            
            int x, y;
            if (check_touch(&touch, &x, &y)) {
                uint32_t now = last_time + 200;  // Simple debounce
                last_time = now;
                
                // Check exit button
                if (button_check_press(&exit_btn, button_is_touched(&exit_btn, x, y), now)) {
                    running = false;
                    continue;
                }
                
                // Check test buttons
                int item = ui_layout_get_item_at_position(&layout, x, y);
                if (item >= 0 && item < 7) {
                    selected = item;
                    state = TEST_LED_RED + item;
                    usleep(200000);
                }
            }
        } else {
            // Run selected test
            switch (state) {
                case TEST_LED_RED: test_red_led(&fb, &touch); break;
                case TEST_LED_GREEN: test_green_led(&fb, &touch); break;
                case TEST_LED_BOTH: test_both_leds(&fb, &touch); break;
                case TEST_BACKLIGHT: test_backlight(&fb, &touch); break;
                case TEST_PULSE: test_pulse(&fb, &touch); break;
                case TEST_BLINK: test_blink(&fb, &touch); break;
                case TEST_COLORS: test_colors(&fb, &touch); break;
                default: break;
            }
            state = TEST_MENU;
            hw_leds_off();
            usleep(200000);
        }
        usleep(10000);
    }
    
    hw_leds_off();
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    touch_close(&touch);
    fb_close(&fb);
    return 0;
}
