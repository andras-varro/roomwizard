# Hardware Control - Quick Reference

## Quick Start

```c
#include "common/hardware.h"

int main() {
    hw_init();  // Initialize (optional)
    
    // Your code here
    
    hw_leds_off();  // Clean up
    return 0;
}
```

## LED Control

```c
// Individual LEDs
hw_set_red_led(100);      // Red at 100%
hw_set_green_led(50);     // Green at 50%

// Both LEDs
hw_set_leds(75, 25);      // Red 75%, Green 25%
hw_leds_off();            // Turn off both

// Predefined colors
hw_set_leds(HW_LED_COLOR_RED);     // (100, 0)
hw_set_leds(HW_LED_COLOR_GREEN);   // (0, 100)
hw_set_leds(HW_LED_COLOR_YELLOW);  // (100, 100)
hw_set_leds(HW_LED_COLOR_ORANGE);  // (100, 50)
hw_set_leds(HW_LED_COLOR_OFF);     // (0, 0)
```

## Backlight Control

```c
hw_set_backlight(80);     // Set to 80%
int level = hw_get_backlight();  // Read current level
hw_set_backlight(0);      // Turn off (screen blank)
```

## LED Effects

```c
// Pulse (fade in/out)
hw_pulse_led(LED_GREEN, 2000, 100);  // 2 sec, full brightness

// Blink
hw_blink_led(LED_RED, 5, 200, 200, 100);  // 5 blinks, 200ms on/off
```

## Read LED State

```c
int red = hw_get_led(LED_RED);
int green = hw_get_led(LED_GREEN);

LEDState state;
hw_get_led_state(&state);
printf("Red: %d%%, Green: %d%%\n", 
       state.red_brightness, state.green_brightness);
```

## Common Patterns

### Status Indicator
```c
hw_set_leds(HW_LED_COLOR_GREEN);   // Ready
hw_set_leds(HW_LED_COLOR_YELLOW);  // Busy
hw_set_leds(HW_LED_COLOR_RED);     // Error
```

### Game Events
```c
// Game start
hw_pulse_led(LED_GREEN, 500, 100);

// Game over
hw_blink_led(LED_RED, 3, 200, 200, 100);

// Level up
hw_set_leds(HW_LED_COLOR_YELLOW);
usleep(200000);
hw_set_leds(HW_LED_COLOR_GREEN);
```

### Screen Blanking
```c
int saved = hw_get_backlight();
hw_set_backlight(0);        // Blank
// ... wait ...
hw_set_backlight(saved);    // Restore
```

## Testing

```bash
# Build
./compile_for_roomwizard.sh

# Transfer to device
scp build/hardware_test root@<ip>:/opt/games/

# Run tests
./hardware_test all        # All tests
./hardware_test leds       # LEDs only
./hardware_test backlight  # Backlight only
./hardware_test pulse      # Pulse effect
./hardware_test blink      # Blink effect
./hardware_test colors     # Color cycle
```

## Hardware Paths

```
/sys/class/leds/red_led/brightness      (0-100)
/sys/class/leds/green_led/brightness    (0-100)
/sys/class/leds/backlight/brightness    (0-100)
```

## Return Values

- `0` = Success
- `-1` = Error (check stderr for details)

## Notes

- All brightness values: 0-100 (percentage)
- Pulse and blink functions are **blocking**
- Functions are **not thread-safe** (use mutex if needed)
- Requires write access to sysfs (usually root)
- Very fast (< 1ms per operation)
- Safe to call frequently

## See Also

- [`HARDWARE_CONTROL.md`](HARDWARE_CONTROL.md) - Full documentation
- [`hardware.h`](common/hardware.h) - API header
- [`hardware_test.c`](hardware_test.c) - Test program
