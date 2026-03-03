#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Sysfs paths for hardware control
#define RED_LED_PATH       "/sys/class/leds/red_led/brightness"
#define GREEN_LED_PATH     "/sys/class/leds/green_led/brightness"
#define BACKLIGHT_PATH     "/sys/class/leds/backlight/brightness"

// Internal helper to write brightness value to sysfs
static int write_brightness(const char *path, uint8_t brightness) {
    // Clamp brightness to 0-100
    if (brightness > 100) {
        brightness = 100;
    }
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
        return -1;
    }
    
    char buf[8];
    int len = snprintf(buf, sizeof(buf), "%d", brightness);
    
    ssize_t written = write(fd, buf, len);
    close(fd);
    
    if (written < 0) {
        fprintf(stderr, "Error writing to %s: %s\n", path, strerror(errno));
        return -1;
    }
    
    return 0;
}

// Internal helper to read brightness value from sysfs
static int read_brightness(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
        return -1;
    }
    
    char buf[8];
    ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    
    if (bytes_read < 0) {
        fprintf(stderr, "Error reading from %s: %s\n", path, strerror(errno));
        return -1;
    }
    
    buf[bytes_read] = '\0';
    return atoi(buf);
}

// Get path for LED
static const char* get_led_path(LEDColor led) {
    switch (led) {
        case LED_RED:
            return RED_LED_PATH;
        case LED_GREEN:
            return GREEN_LED_PATH;
        default:
            return NULL;
    }
}

int hw_init(void) {
    // Test access to all hardware control paths
    int errors = 0;
    
    // Test red LED
    if (access(RED_LED_PATH, W_OK) != 0) {
        fprintf(stderr, "Warning: Cannot access red LED at %s\n", RED_LED_PATH);
        errors++;
    }
    
    // Test green LED
    if (access(GREEN_LED_PATH, W_OK) != 0) {
        fprintf(stderr, "Warning: Cannot access green LED at %s\n", GREEN_LED_PATH);
        errors++;
    }
    
    // Test backlight
    if (access(BACKLIGHT_PATH, W_OK) != 0) {
        fprintf(stderr, "Warning: Cannot access backlight at %s\n", BACKLIGHT_PATH);
        errors++;
    }
    
    if (errors > 0) {
        fprintf(stderr, "Hardware control initialization completed with %d warnings\n", errors);
        fprintf(stderr, "Note: Root privileges may be required for hardware access\n");
    }
    
    return 0;  // Return success even with warnings
}

int hw_set_led(LEDColor led, uint8_t brightness) {
    const char *path = get_led_path(led);
    if (path == NULL) {
        fprintf(stderr, "Error: Invalid LED color\n");
        return -1;
    }
    
    return write_brightness(path, brightness);
}

int hw_get_led(LEDColor led) {
    const char *path = get_led_path(led);
    if (path == NULL) {
        fprintf(stderr, "Error: Invalid LED color\n");
        return -1;
    }
    
    return read_brightness(path);
}

int hw_set_red_led(uint8_t brightness) {
    return write_brightness(RED_LED_PATH, brightness);
}

int hw_set_green_led(uint8_t brightness) {
    return write_brightness(GREEN_LED_PATH, brightness);
}

int hw_set_leds(uint8_t red, uint8_t green) {
    int ret1 = hw_set_red_led(red);
    int ret2 = hw_set_green_led(green);
    
    // Return error if either failed
    return (ret1 < 0 || ret2 < 0) ? -1 : 0;
}

int hw_get_led_state(LEDState *state) {
    if (state == NULL) {
        fprintf(stderr, "Error: NULL state pointer\n");
        return -1;
    }
    
    int red = hw_get_led(LED_RED);
    int green = hw_get_led(LED_GREEN);
    
    if (red < 0 || green < 0) {
        return -1;
    }
    
    state->red_brightness = (uint8_t)red;
    state->green_brightness = (uint8_t)green;
    
    return 0;
}

int hw_leds_off(void) {
    return hw_set_leds(0, 0);
}

int hw_set_backlight(uint8_t brightness) {
    return write_brightness(BACKLIGHT_PATH, brightness);
}

int hw_get_backlight(void) {
    return read_brightness(BACKLIGHT_PATH);
}

int hw_pulse_led(LEDColor led, uint32_t duration_ms, uint8_t max_brightness) {
    const int steps = 20;
    const uint32_t step_delay_us = (duration_ms * 1000) / (2 * steps);
    
    // Fade in
    for (int i = 0; i <= steps; i++) {
        uint8_t brightness = (max_brightness * i) / steps;
        if (hw_set_led(led, brightness) < 0) {
            return -1;
        }
        usleep(step_delay_us);
    }
    
    // Fade out
    for (int i = steps; i >= 0; i--) {
        uint8_t brightness = (max_brightness * i) / steps;
        if (hw_set_led(led, brightness) < 0) {
            return -1;
        }
        usleep(step_delay_us);
    }
    
    return 0;
}

int hw_blink_led(LEDColor led, int count, uint32_t on_ms, uint32_t off_ms, uint8_t brightness) {
    for (int i = 0; i < count; i++) {
        // Turn on
        if (hw_set_led(led, brightness) < 0) {
            return -1;
        }
        usleep(on_ms * 1000);
        
        // Turn off
        if (hw_set_led(led, 0) < 0) {
            return -1;
        }
        
        // Don't wait after last blink
        if (i < count - 1) {
            usleep(off_ms * 1000);
        }
    }
    
    return 0;
}
