#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Hardware Control Library for RoomWizard
 * 
 * Provides control over:
 * - LED indicators (red and green)
 * - Backlight brightness
 * 
 * All brightness values are 0-100 (percentage)
 */

// LED color enumeration
typedef enum {
    LED_RED = 0,
    LED_GREEN = 1
} LEDColor;

// LED state structure (for combined control)
typedef struct {
    uint8_t red_brightness;    // 0-100
    uint8_t green_brightness;  // 0-100
} LEDState;

/**
 * Initialize hardware control subsystem
 * Returns: 0 on success, -1 on error
 */
int hw_init(void);

/**
 * Set individual LED brightness
 * @param led: LED_RED or LED_GREEN
 * @param brightness: 0-100 (0=off, 100=full brightness)
 * Returns: 0 on success, -1 on error
 */
int hw_set_led(LEDColor led, uint8_t brightness);

/**
 * Get individual LED brightness
 * @param led: LED_RED or LED_GREEN
 * Returns: brightness value 0-100, or -1 on error
 */
int hw_get_led(LEDColor led);

/**
 * Set red LED brightness
 * @param brightness: 0-100
 * Returns: 0 on success, -1 on error
 */
int hw_set_red_led(uint8_t brightness);

/**
 * Set green LED brightness
 * @param brightness: 0-100
 * Returns: 0 on success, -1 on error
 */
int hw_set_green_led(uint8_t brightness);

/**
 * Set both LEDs at once
 * @param red: Red LED brightness 0-100
 * @param green: Green LED brightness 0-100
 * Returns: 0 on success, -1 on error
 */
int hw_set_leds(uint8_t red, uint8_t green);

/**
 * Get current LED state
 * @param state: Pointer to LEDState structure to fill
 * Returns: 0 on success, -1 on error
 */
int hw_get_led_state(LEDState *state);

/**
 * Turn off all LEDs
 * Returns: 0 on success, -1 on error
 */
int hw_leds_off(void);

/**
 * Set backlight brightness
 * @param brightness: 0-100 (0=off, 100=full brightness)
 * Returns: 0 on success, -1 on error
 */
int hw_set_backlight(uint8_t brightness);

/**
 * Get backlight brightness
 * Returns: brightness value 0-100, or -1 on error
 */
int hw_get_backlight(void);

/**
 * Pulse an LED (fade in/out effect)
 * @param led: LED_RED or LED_GREEN
 * @param duration_ms: Total duration of pulse in milliseconds
 * @param max_brightness: Peak brightness 0-100
 * Returns: 0 on success, -1 on error
 * 
 * Note: This is a blocking function
 */
int hw_pulse_led(LEDColor led, uint32_t duration_ms, uint8_t max_brightness);

/**
 * Blink an LED
 * @param led: LED_RED or LED_GREEN
 * @param count: Number of blinks
 * @param on_ms: Time LED is on (milliseconds)
 * @param off_ms: Time LED is off (milliseconds)
 * @param brightness: LED brightness when on (0-100)
 * Returns: 0 on success, -1 on error
 * 
 * Note: This is a blocking function
 */
int hw_blink_led(LEDColor led, int count, uint32_t on_ms, uint32_t off_ms, uint8_t brightness);

/**
 * Set LED color by mixing red and green
 * Predefined color combinations:
 * - Red only: (100, 0)
 * - Green only: (0, 100)
 * - Yellow/Orange: (100, 100) or (100, 50)
 * - Off: (0, 0)
 */
#define HW_LED_COLOR_OFF        0,   0
#define HW_LED_COLOR_RED        100, 0
#define HW_LED_COLOR_GREEN      0,   100
#define HW_LED_COLOR_YELLOW     100, 100
#define HW_LED_COLOR_ORANGE     100, 50

#endif // HARDWARE_H
