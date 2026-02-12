/**
 * Hardware Test Program for RoomWizard
 * 
 * Tests LED and backlight control functionality
 * 
 * Usage: ./hardware_test [test_mode]
 *   test_mode:
 *     all      - Run all tests (default)
 *     leds     - Test LED control only
 *     backlight - Test backlight control only
 *     pulse    - Pulse green LED
 *     blink    - Blink red LED
 *     colors   - Cycle through LED colors
 */

#include "common/hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_leds(void) {
    printf("\n=== LED Control Test ===\n");
    
    printf("Testing red LED...\n");
    for (int i = 0; i <= 100; i += 25) {
        printf("  Red brightness: %d%%\n", i);
        hw_set_red_led(i);
        usleep(500000);  // 500ms
    }
    hw_set_red_led(0);
    
    printf("Testing green LED...\n");
    for (int i = 0; i <= 100; i += 25) {
        printf("  Green brightness: %d%%\n", i);
        hw_set_green_led(i);
        usleep(500000);  // 500ms
    }
    hw_set_green_led(0);
    
    printf("Testing both LEDs...\n");
    hw_set_leds(50, 50);
    printf("  Both at 50%%\n");
    sleep(1);
    
    hw_leds_off();
    printf("  LEDs off\n");
}

void test_backlight(void) {
    printf("\n=== Backlight Control Test ===\n");
    
    int original = hw_get_backlight();
    printf("Current backlight: %d%%\n", original);
    
    printf("Dimming backlight...\n");
    for (int i = 100; i >= 0; i -= 20) {
        printf("  Backlight: %d%%\n", i);
        hw_set_backlight(i);
        usleep(500000);  // 500ms
    }
    
    printf("Restoring backlight...\n");
    for (int i = 0; i <= 100; i += 20) {
        printf("  Backlight: %d%%\n", i);
        hw_set_backlight(i);
        usleep(500000);  // 500ms
    }
    
    // Restore original brightness
    hw_set_backlight(original);
    printf("Backlight restored to %d%%\n", original);
}

void test_pulse(void) {
    printf("\n=== LED Pulse Test ===\n");
    printf("Pulsing green LED (2 seconds)...\n");
    hw_pulse_led(LED_GREEN, 2000, 100);
    printf("Done\n");
}

void test_blink(void) {
    printf("\n=== LED Blink Test ===\n");
    printf("Blinking red LED (5 times)...\n");
    hw_blink_led(LED_RED, 5, 200, 200, 100);
    printf("Done\n");
}

void test_colors(void) {
    printf("\n=== LED Color Test ===\n");
    
    printf("Red...\n");
    hw_set_leds(HW_LED_COLOR_RED);
    sleep(1);
    
    printf("Green...\n");
    hw_set_leds(HW_LED_COLOR_GREEN);
    sleep(1);
    
    printf("Yellow...\n");
    hw_set_leds(HW_LED_COLOR_YELLOW);
    sleep(1);
    
    printf("Orange...\n");
    hw_set_leds(HW_LED_COLOR_ORANGE);
    sleep(1);
    
    printf("Off...\n");
    hw_set_leds(HW_LED_COLOR_OFF);
}

void test_state(void) {
    printf("\n=== LED State Test ===\n");
    
    // Set some values
    hw_set_leds(75, 25);
    
    // Read back state
    LEDState state;
    if (hw_get_led_state(&state) == 0) {
        printf("Current LED state:\n");
        printf("  Red: %d%%\n", state.red_brightness);
        printf("  Green: %d%%\n", state.green_brightness);
    } else {
        printf("Failed to read LED state\n");
    }
    
    hw_leds_off();
}

void print_usage(const char *prog) {
    printf("Usage: %s [test_mode]\n", prog);
    printf("\nTest modes:\n");
    printf("  all       - Run all tests (default)\n");
    printf("  leds      - Test LED control only\n");
    printf("  backlight - Test backlight control only\n");
    printf("  pulse     - Pulse green LED\n");
    printf("  blink     - Blink red LED\n");
    printf("  colors    - Cycle through LED colors\n");
    printf("  state     - Test LED state reading\n");
}

int main(int argc, char *argv[]) {
    printf("RoomWizard Hardware Test\n");
    printf("========================\n");
    
    // Initialize hardware control
    if (hw_init() < 0) {
        fprintf(stderr, "Warning: Hardware initialization reported issues\n");
        fprintf(stderr, "Continuing anyway - some tests may fail\n");
    }
    
    const char *test_mode = "all";
    if (argc > 1) {
        test_mode = argv[1];
    }
    
    if (strcmp(test_mode, "help") == 0 || strcmp(test_mode, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (strcmp(test_mode, "all") == 0) {
        test_leds();
        test_backlight();
        test_pulse();
        test_blink();
        test_colors();
        test_state();
    } else if (strcmp(test_mode, "leds") == 0) {
        test_leds();
    } else if (strcmp(test_mode, "backlight") == 0) {
        test_backlight();
    } else if (strcmp(test_mode, "pulse") == 0) {
        test_pulse();
    } else if (strcmp(test_mode, "blink") == 0) {
        test_blink();
    } else if (strcmp(test_mode, "colors") == 0) {
        test_colors();
    } else if (strcmp(test_mode, "state") == 0) {
        test_state();
    } else {
        fprintf(stderr, "Unknown test mode: %s\n", test_mode);
        print_usage(argv[0]);
        return 1;
    }
    
    printf("\n=== All tests complete ===\n");
    
    // Ensure LEDs are off at exit
    hw_leds_off();
    
    return 0;
}
