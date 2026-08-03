#include "hardware.h"
#include "config.h"
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

/* ── Config cache ───────────────────────────────────────────────────────── */

static bool     hw_config_loaded = false;
static bool     hw_led_enabled = true;
static int      hw_led_brightness_pct = 100;
static int      hw_backlight_brightness_pct = 100;

static void hw_load_config(void) {
    if (hw_config_loaded) return;
    hw_config_loaded = true;

    Config cfg;
    config_init(&cfg);
    if (config_load(&cfg) == 0) {
        hw_led_enabled = config_led_enabled(&cfg);
        hw_led_brightness_pct = config_led_brightness(&cfg);
        hw_backlight_brightness_pct = config_backlight_brightness(&cfg);
    }
}

void hw_reload_config(void) {
    hw_config_loaded = false;
    hw_load_config();
}

/** Scale a 0-100 brightness value by a percentage factor. */
static uint8_t hw_scale_brightness(uint8_t brightness, int pct) {
    if (pct >= 100) return brightness;
    if (pct <= 0) return 0;
    return (uint8_t)((int)brightness * pct / 100);
}

/**
 * Inverse of hw_scale_brightness(): raw sysfs duty cycle -> setter space.
 *
 * The setters take "percent of the user's configured maximum" and scale down
 * before writing, so a getter that returned the raw sysfs value put the two
 * halves of the API in different units. Every save/restore pair then multiplied
 * the panel by pct/100 again on each run — three sweeps at 50% left the
 * backlight at 12% (IMPROVEMENT_PLAN B9). Returning setter space makes
 * hw_set_backlight(hw_get_backlight()) a no-op, which is what callers assume.
 *
 * A raw value larger than the configured maximum can only come from something
 * that bypassed the API, so it clamps to 100 rather than reporting >100.
 */
static int hw_unscale_brightness(int raw, int pct) {
    if (raw < 0) return raw;          /* propagate the read error unchanged */
    if (pct >= 100) return raw;
    if (pct <= 0) return 0;
    int v = raw * 100 / pct;
    return (v > 100) ? 100 : v;
}

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
    hw_load_config();
    if (!hw_led_enabled) return 0;  /* LEDs disabled by config */
    brightness = hw_scale_brightness(brightness, hw_led_brightness_pct);

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

    hw_load_config();
    return hw_unscale_brightness(read_brightness(path), hw_led_brightness_pct);
}

int hw_set_red_led(uint8_t brightness) {
    return hw_set_led(LED_RED, brightness);
}

int hw_set_green_led(uint8_t brightness) {
    return hw_set_led(LED_GREEN, brightness);
}

int hw_set_leds(uint8_t red, uint8_t green) {
    int ret1 = hw_set_led(LED_RED, red);
    int ret2 = hw_set_led(LED_GREEN, green);
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
    /* Always write 0 — cleanup must work regardless of config */
    int ret1 = write_brightness(RED_LED_PATH, 0);
    int ret2 = write_brightness(GREEN_LED_PATH, 0);
    return (ret1 < 0 || ret2 < 0) ? -1 : 0;
}

int hw_set_backlight(uint8_t brightness) {
    hw_load_config();
    brightness = hw_scale_brightness(brightness, hw_backlight_brightness_pct);

    return write_brightness(BACKLIGHT_PATH, brightness);
}

int hw_set_backlight_raw(uint8_t brightness) {
    /* Deliberately unscaled — see the header.  Used by the settings sliders,
     * which are choosing the scale factor itself, so hw_set_backlight() would
     * apply the previous factor on top and preview the wrong brightness. */
    return write_brightness(BACKLIGHT_PATH, brightness);
}

int hw_get_backlight(void) {
    hw_load_config();
    return hw_unscale_brightness(read_brightness(BACKLIGHT_PATH),
                                 hw_backlight_brightness_pct);
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

/* ── Non-blocking LED pulse (see hardware.h) ─────────────────────────────── */

void hw_led_pulse_start(LedPulse *p, LEDColor led, int flashes,
                        uint32_t half_period_ms, uint32_t now_ms) {
    if (!p) return;
    p->led            = led;
    p->flashes        = flashes;
    p->half_period_ms = half_period_ms;
    p->start_ms       = now_ms;
    p->phase          = -1;
    p->active         = (flashes > 0 && half_period_ms > 0);
    /* Light it on the frame it was started, so the flourish is visible even if
     * the caller exits before the next update. */
    hw_led_pulse_update(p, now_ms);
}

void hw_led_pulse_update(LedPulse *p, uint32_t now_ms) {
    if (!p || !p->active) return;

    /* Unsigned subtraction, so this stays correct across the ms counter's wrap. */
    uint32_t elapsed = now_ms - p->start_ms;
    /* Phase 0 = first on, 1 = first off, 2 = second on ... 2*flashes = done.
     * A runtime divisor compiles to a call to __aeabi_uidiv on Cortex-A8, which
     * is fine — what would not be is a hardware udiv instruction. */
    uint32_t phase = elapsed / p->half_period_ms;

    if (phase >= (uint32_t)(p->flashes * 2)) {
        p->active = false;
        hw_set_led(p->led, 0);
        return;
    }
    if ((int)phase == p->phase) return;   /* nothing changed this frame */

    p->phase = (int)phase;
    hw_set_led(p->led, (phase & 1u) ? 0 : 100);
}

bool hw_led_pulse_active(const LedPulse *p) {
    return p && p->active;
}

void hw_led_pulse_stop(LedPulse *p) {
    if (!p) return;
    p->active = false;
    p->phase  = -1;
    hw_set_led(p->led, 0);
}
