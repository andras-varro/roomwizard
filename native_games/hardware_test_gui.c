/**
 * Hardware Test GUI for RoomWizard
 * 
 * Touch-based hardware diagnostic tool
 * Tests LED and backlight control with visual feedback
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include "common/hardware.h"
#include "common/game_common.h"
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

// Draw test menu
void draw_test_menu(Framebuffer *fb, Button *exit_btn, int selected) {
    fb_clear(fb, COLOR_BLACK);
    
    // Title
    fb_draw_text(fb, 400, 30, "Hardware Test", COLOR_WHITE, 3);
    
    // Exit button
    draw_exit_button(fb, exit_btn);
    
    // Test buttons
    const char *test_names[] = {
        "Red LED Test",
        "Green LED Test",
        "Both LEDs Test",
        "Backlight Test",
        "Pulse Effect",
        "Blink Effect",
        "Color Cycle"
    };
    
    int y = 100;
    for (int i = 0; i < 7; i++) {
        uint32_t color = (i == selected) ? COLOR_YELLOW : COLOR_WHITE;
        uint32_t bg = (i == selected) ? RGB(51, 51, 51) : RGB(34, 34, 34);
        
        // Button background
        fb_draw_rect(fb, 200, y, 400, 50, bg);
        
        // Button text
        fb_draw_text(fb, 400, y + 25, test_names[i], color, 2);
        
        y += 60;
    }
    
    fb_swap(fb);
}

// Draw test screen with progress bar
void draw_test_screen(Framebuffer *fb, const char *title, const char *status, int progress) {
    fb_clear(fb, COLOR_BLACK);
    
    // Title
    fb_draw_text(fb, 400, 50, title, COLOR_WHITE, 3);
    
    // Status text
    if (status) {
        fb_draw_text(fb, 400, 150, status, COLOR_CYAN, 2);
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
        fb_draw_text(fb, 400, bar_y + 20, pct, COLOR_WHITE, 2);
    }
    
    // Instructions
    fb_draw_text(fb, 400, 400, "Touch screen to return to menu", RGB(136, 136, 136), 2);
    
    fb_swap(fb);
}

// Run red LED test
void test_red_led(Framebuffer *fb, TouchInput *touch) {
    for (int i = 0; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "Brightness: %d%%", i);
        draw_test_screen(fb, "Red LED Test", status, i);
        
        hw_set_red_led(i);
        usleep(50000);  // 50ms
        
        // Check for touch to exit
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_set_red_led(0);
            return;
        }
    }
    
    // Hold at full brightness
    for (int i = 0; i < 20; i++) {
        usleep(50000);
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_set_red_led(0);
            return;
        }
    }
    
    // Fade down
    for (int i = 100; i >= 0; i -= 5) {
        char status[64];
        snprintf(status, sizeof(status), "Brightness: %d%%", i);
        draw_test_screen(fb, "Red LED Test", status, 100 - i);
        
        hw_set_red_led(i);
        usleep(50000);
        
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_set_red_led(0);
            return;
        }
    }
    
    hw_set_red_led(0);
    draw_test_screen(fb, "Red LED Test", "Complete!", 100);
    
    // Wait for touch to continue
    int x, y;
    while (!touch_wait_for_press(touch, &x, &y)) {
        usleep(10000);
    }
}

// Run green LED test
void test_green_led(Framebuffer *fb, TouchInput *touch) {
    for (int i = 0; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "Brightness: %d%%", i);
        draw_test_screen(fb, "Green LED Test", status, i);
        
        hw_set_green_led(i);
        usleep(50000);
        
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_set_green_led(0);
            return;
        }
    }
    
    for (int i = 0; i < 20; i++) {
        usleep(50000);
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_set_green_led(0);
            return;
        }
    }
    
    for (int i = 100; i >= 0; i -= 5) {
        char status[64];
        snprintf(status, sizeof(status), "Brightness: %d%%", i);
        draw_test_screen(fb, "Green LED Test", status, 100 - i);
        
        hw_set_green_led(i);
        usleep(50000);
        
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_set_green_led(0);
            return;
        }
    }
    
    hw_set_green_led(0);
    draw_test_screen(fb, "Green LED Test", "Complete!", 100);
    
    int x, y;
    while (!touch_wait_for_press(touch, &x, &y)) {
        usleep(10000);
    }
}

// Run both LEDs test
void test_both_leds(Framebuffer *fb, TouchInput *touch) {
    // Ramp up both
    for (int i = 0; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "Both LEDs: %d%%", i);
        draw_test_screen(fb, "Both LEDs Test", status, i);
        
        hw_set_leds(i, i);
        usleep(50000);
        
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_leds_off();
            return;
        }
    }
    
    // Hold
    for (int i = 0; i < 20; i++) {
        usleep(50000);
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_leds_off();
            return;
        }
    }
    
    // Alternate
    for (int cycle = 0; cycle < 5; cycle++) {
        draw_test_screen(fb, "Both LEDs Test", "Red only", 50);
        hw_set_leds(100, 0);
        for (int i = 0; i < 10; i++) {
            usleep(50000);
            int x, y;
            if (touch_wait_for_press(touch, &x, &y)) {
                hw_leds_off();
                return;
            }
        }
        
        draw_test_screen(fb, "Both LEDs Test", "Green only", 50);
        hw_set_leds(0, 100);
        for (int i = 0; i < 10; i++) {
            usleep(50000);
            int x, y;
            if (touch_wait_for_press(touch, &x, &y)) {
                hw_leds_off();
                return;
            }
        }
    }
    
    hw_leds_off();
    draw_test_screen(fb, "Both LEDs Test", "Complete!", 100);
    
    int x, y;
    while (!touch_wait_for_press(touch, &x, &y)) {
        usleep(10000);
    }
}

// Run backlight test
void test_backlight(Framebuffer *fb, TouchInput *touch) {
    int original = hw_get_backlight();
    
    // Dim down
    for (int i = 100; i >= 20; i -= 5) {
        char status[64];
        snprintf(status, sizeof(status), "Brightness: %d%%", i);
        draw_test_screen(fb, "Backlight Test", status, 100 - i);
        
        hw_set_backlight(i);
        usleep(50000);
        
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_set_backlight(original);
            return;
        }
    }
    
    // Brighten up
    for (int i = 20; i <= 100; i += 5) {
        char status[64];
        snprintf(status, sizeof(status), "Brightness: %d%%", i);
        draw_test_screen(fb, "Backlight Test", status, i);
        
        hw_set_backlight(i);
        usleep(50000);
        
        int x, y;
        if (touch_wait_for_press(touch, &x, &y)) {
            hw_set_backlight(original);
            return;
        }
    }
    
    hw_set_backlight(original);
    draw_test_screen(fb, "Backlight Test", "Complete!", 100);
    
    int x, y;
    while (!touch_wait_for_press(touch, &x, &y)) {
        usleep(10000);
    }
}

// Run pulse test
void test_pulse(Framebuffer *fb, TouchInput *touch) {
    draw_test_screen(fb, "Pulse Effect", "Pulsing green LED...", 50);
    hw_pulse_led(LED_GREEN, 3000, 100);
    
    draw_test_screen(fb, "Pulse Effect", "Complete!", 100);
    int x, y;
    while (!touch_wait_for_press(touch, &x, &y)) {
        usleep(10000);
    }
}

// Run blink test
void test_blink(Framebuffer *fb, TouchInput *touch) {
    draw_test_screen(fb, "Blink Effect", "Blinking red LED...", 50);
    hw_blink_led(LED_RED, 10, 200, 200, 100);
    
    draw_test_screen(fb, "Blink Effect", "Complete!", 100);
    int x, y;
    while (!touch_wait_for_press(touch, &x, &y)) {
        usleep(10000);
    }
}

// Run color cycle test
void test_colors(Framebuffer *fb, TouchInput *touch) {
    const char *color_names[] = {"Red", "Orange", "Yellow", "Green", "Off"};
    const struct { uint8_t r; uint8_t g; } colors[] = {
        {100, 0},    // Red
        {100, 50},   // Orange
        {100, 100},  // Yellow
        {0, 100},    // Green
        {0, 0}       // Off
    };
    
    for (int i = 0; i < 5; i++) {
        draw_test_screen(fb, "Color Cycle", color_names[i], (i * 100) / 4);
        hw_set_leds(colors[i].r, colors[i].g);
        
        for (int j = 0; j < 20; j++) {
            usleep(50000);
            int x, y;
            if (touch_wait_for_press(touch, &x, &y)) {
                hw_leds_off();
                return;
            }
        }
    }
    
    draw_test_screen(fb, "Color Cycle", "Complete!", 100);
    int x, y;
    while (!touch_wait_for_press(touch, &x, &y)) {
        usleep(10000);
    }
}

int main(void) {
    // Initialize framebuffer
    Framebuffer fb;
    if (fb_init(&fb, "/dev/fb0") < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    
    // Initialize touch input
    TouchInput touch;
    if (touch_init(&touch, "/dev/input/event0") < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&fb);
        return 1;
    }
    
    // Initialize hardware
    if (hw_init() < 0) {
        fprintf(stderr, "Warning: Hardware initialization issues\n");
    }
    
    // Exit button
    Button exit_btn;
    button_init(&exit_btn, 730, 10, BTN_EXIT_WIDTH, BTN_EXIT_HEIGHT, "X",
                BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
    
    TestState state = TEST_MENU;
    int selected = 0;
    bool running = true;
    
    while (running) {
        if (state == TEST_MENU) {
            draw_test_menu(&fb, &exit_btn, selected);
            
            // Wait for touch
            int x, y;
            if (touch_wait_for_press(&touch, &x, &y)) {
                uint32_t now = get_time_ms();
                
                // Check exit button
                if (button_check_press(&exit_btn, button_is_touched(&exit_btn, x, y), now)) {
                    running = false;
                    continue;
                }
                
                // Check test buttons
                int button_y = 100;
                for (int i = 0; i < 7; i++) {
                    if (x >= 200 && x <= 600 && y >= button_y && y < button_y + 50) {
                        selected = i;
                        state = TEST_LED_RED + i;  // Enum values are sequential
                        break;
                    }
                    button_y += 60;
                }
            }
        } else {
            // Run selected test
            switch (state) {
                case TEST_LED_RED:
                    test_red_led(&fb, &touch);
                    break;
                case TEST_LED_GREEN:
                    test_green_led(&fb, &touch);
                    break;
                case TEST_LED_BOTH:
                    test_both_leds(&fb, &touch);
                    break;
                case TEST_BACKLIGHT:
                    test_backlight(&fb, &touch);
                    break;
                case TEST_PULSE:
                    test_pulse(&fb, &touch);
                    break;
                case TEST_BLINK:
                    test_blink(&fb, &touch);
                    break;
                case TEST_COLORS:
                    test_colors(&fb, &touch);
                    break;
                default:
                    break;
            }
            
            // Return to menu
            state = TEST_MENU;
            hw_leds_off();
        }
        
        usleep(10000);
    }
    
    // Cleanup
    hw_leds_off();
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    
    touch_close(&touch);
    fb_close(&fb);
    
    return 0;
}
