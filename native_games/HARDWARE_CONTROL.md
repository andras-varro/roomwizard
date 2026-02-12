# Hardware Control Library for RoomWizard

This library provides control over the RoomWizard's LED indicators and backlight through a simple C API.

## Hardware Capabilities

The RoomWizard device has the following controllable hardware:

- **Red LED** - Status indicator (0-100% brightness)
- **Green LED** - Status indicator (0-100% brightness)  
- **Backlight** - Display backlight (0-100% brightness)

All hardware is controlled via Linux sysfs at:
- `/sys/class/leds/red_led/brightness`
- `/sys/class/leds/green_led/brightness`
- `/sys/class/leds/backlight/brightness`

## Quick Start

### Include the Header

```c
#include "common/hardware.h"
```

### Initialize Hardware Control

```c
int main() {
    // Initialize hardware subsystem (optional but recommended)
    if (hw_init() < 0) {
        fprintf(stderr, "Warning: Hardware initialization issues\n");
    }
    
    // Your code here...
    
    return 0;
}
```

### Basic LED Control

```c
// Set individual LEDs
hw_set_red_led(100);    // Red at full brightness
hw_set_green_led(50);   // Green at half brightness

// Set both LEDs at once
hw_set_leds(75, 25);    // Red at 75%, green at 25%

// Turn off all LEDs
hw_leds_off();

// Use predefined colors
hw_set_leds(HW_LED_COLOR_RED);     // Red only
hw_set_leds(HW_LED_COLOR_GREEN);   // Green only
hw_set_leds(HW_LED_COLOR_YELLOW);  // Both at 100%
hw_set_leds(HW_LED_COLOR_ORANGE);  // Red 100%, green 50%
hw_set_leds(HW_LED_COLOR_OFF);     // Both off
```

### Backlight Control

```c
// Set backlight brightness
hw_set_backlight(80);   // 80% brightness

// Get current backlight level
int brightness = hw_get_backlight();
printf("Backlight: %d%%\n", brightness);

// Dim for screen blanking
hw_set_backlight(0);    // Turn off backlight
```

### LED Effects

```c
// Pulse green LED (fade in/out)
hw_pulse_led(LED_GREEN, 2000, 100);  // 2 second pulse at full brightness

// Blink red LED
hw_blink_led(LED_RED, 5, 200, 200, 100);  // 5 blinks, 200ms on/off, full brightness
```

### Reading LED State

```c
// Get individual LED brightness
int red = hw_get_led(LED_RED);
int green = hw_get_led(LED_GREEN);

// Get both LEDs at once
LEDState state;
if (hw_get_led_state(&state) == 0) {
    printf("Red: %d%%, Green: %d%%\n", 
           state.red_brightness, state.green_brightness);
}
```

## API Reference

### Initialization

#### `int hw_init(void)`
Initialize hardware control subsystem. Tests access to all hardware control paths.

**Returns:** 0 on success, -1 on error (with warnings printed)

**Note:** This function is optional but recommended. It will warn about access issues but won't fail completely, allowing graceful degradation.

### LED Control

#### `int hw_set_led(LEDColor led, uint8_t brightness)`
Set individual LED brightness.

**Parameters:**
- `led`: `LED_RED` or `LED_GREEN`
- `brightness`: 0-100 (0=off, 100=full brightness)

**Returns:** 0 on success, -1 on error

#### `int hw_set_red_led(uint8_t brightness)`
Set red LED brightness (convenience function).

**Returns:** 0 on success, -1 on error

#### `int hw_set_green_led(uint8_t brightness)`
Set green LED brightness (convenience function).

**Returns:** 0 on success, -1 on error

#### `int hw_set_leds(uint8_t red, uint8_t green)`
Set both LEDs at once.

**Parameters:**
- `red`: Red LED brightness 0-100
- `green`: Green LED brightness 0-100

**Returns:** 0 on success, -1 on error

#### `int hw_leds_off(void)`
Turn off all LEDs (convenience function, equivalent to `hw_set_leds(0, 0)`).

**Returns:** 0 on success, -1 on error

### LED Reading

#### `int hw_get_led(LEDColor led)`
Get individual LED brightness.

**Parameters:**
- `led`: `LED_RED` or `LED_GREEN`

**Returns:** Brightness value 0-100, or -1 on error

#### `int hw_get_led_state(LEDState *state)`
Get current state of both LEDs.

**Parameters:**
- `state`: Pointer to `LEDState` structure to fill

**Returns:** 0 on success, -1 on error

**Example:**
```c
LEDState state;
if (hw_get_led_state(&state) == 0) {
    printf("Red: %d%%, Green: %d%%\n", 
           state.red_brightness, state.green_brightness);
}
```

### Backlight Control

#### `int hw_set_backlight(uint8_t brightness)`
Set backlight brightness.

**Parameters:**
- `brightness`: 0-100 (0=off, 100=full brightness)

**Returns:** 0 on success, -1 on error

#### `int hw_get_backlight(void)`
Get current backlight brightness.

**Returns:** Brightness value 0-100, or -1 on error

### LED Effects

#### `int hw_pulse_led(LEDColor led, uint32_t duration_ms, uint8_t max_brightness)`
Pulse an LED with fade in/out effect.

**Parameters:**
- `led`: `LED_RED` or `LED_GREEN`
- `duration_ms`: Total duration of pulse in milliseconds
- `max_brightness`: Peak brightness 0-100

**Returns:** 0 on success, -1 on error

**Note:** This is a blocking function that will take `duration_ms` to complete.

#### `int hw_blink_led(LEDColor led, int count, uint32_t on_ms, uint32_t off_ms, uint8_t brightness)`
Blink an LED.

**Parameters:**
- `led`: `LED_RED` or `LED_GREEN`
- `count`: Number of blinks
- `on_ms`: Time LED is on (milliseconds)
- `off_ms`: Time LED is off (milliseconds)
- `brightness`: LED brightness when on (0-100)

**Returns:** 0 on success, -1 on error

**Note:** This is a blocking function.

### Predefined Colors

Use these macros with `hw_set_leds()`:

```c
HW_LED_COLOR_OFF        // Both LEDs off (0, 0)
HW_LED_COLOR_RED        // Red only (100, 0)
HW_LED_COLOR_GREEN      // Green only (0, 100)
HW_LED_COLOR_YELLOW     // Both at full (100, 100)
HW_LED_COLOR_ORANGE     // Red full, green half (100, 50)
```

## Usage Examples

### Status Indicator

```c
// Show different states with LED colors
void show_status(int status) {
    switch (status) {
        case STATUS_IDLE:
            hw_set_leds(HW_LED_COLOR_GREEN);
            break;
        case STATUS_BUSY:
            hw_set_leds(HW_LED_COLOR_YELLOW);
            break;
        case STATUS_ERROR:
            hw_set_leds(HW_LED_COLOR_RED);
            break;
        case STATUS_OFF:
            hw_set_leds(HW_LED_COLOR_OFF);
            break;
    }
}
```

### Loading Animation

```c
void loading_animation(void) {
    for (int i = 0; i < 3; i++) {
        hw_pulse_led(LED_GREEN, 1000, 100);
        usleep(100000);  // 100ms pause
    }
}
```

### Error Blink

```c
void signal_error(void) {
    // Blink red LED 3 times rapidly
    hw_blink_led(LED_RED, 3, 150, 150, 100);
}
```

### Screen Blanking

```c
void blank_screen(void) {
    // Save current backlight level
    int saved_brightness = hw_get_backlight();
    
    // Turn off backlight
    hw_set_backlight(0);
    
    // ... wait for user input ...
    
    // Restore backlight
    hw_set_backlight(saved_brightness);
}
```

### Game State Indicators

```c
void game_start(void) {
    // Green pulse on game start
    hw_pulse_led(LED_GREEN, 500, 100);
}

void game_over(void) {
    // Red blink on game over
    hw_blink_led(LED_RED, 2, 300, 300, 100);
}

void level_up(void) {
    // Yellow flash
    hw_set_leds(HW_LED_COLOR_YELLOW);
    usleep(200000);
    hw_set_leds(HW_LED_COLOR_GREEN);
}
```

## Testing

A test program is provided to verify hardware control functionality:

```bash
# Compile
./compile_for_roomwizard.sh

# Transfer to device
scp build/hardware_test root@<roomwizard-ip>:/opt/games/

# Run tests on device
ssh root@<roomwizard-ip>
cd /opt/games
./hardware_test all        # Run all tests
./hardware_test leds       # Test LEDs only
./hardware_test backlight  # Test backlight only
./hardware_test pulse      # Pulse green LED
./hardware_test blink      # Blink red LED
./hardware_test colors     # Cycle through colors
```

## Permissions

Hardware control requires write access to sysfs paths. On the RoomWizard:

- Root access: Full control
- Regular users: May need permissions configured

If running as non-root, you may need to set permissions:

```bash
# Allow all users to control LEDs (run as root)
chmod 666 /sys/class/leds/*/brightness
```

## Integration with Games

To add hardware control to your game:

1. Include the header:
   ```c
   #include "common/hardware.h"
   ```

2. Initialize in your main function:
   ```c
   hw_init();
   ```

3. Use LED indicators for game events:
   ```c
   // Game start
   hw_set_leds(HW_LED_COLOR_GREEN);
   
   // Game over
   hw_blink_led(LED_RED, 3, 200, 200, 100);
   
   // Pause
   hw_set_leds(HW_LED_COLOR_YELLOW);
   ```

4. Clean up on exit:
   ```c
   hw_leds_off();
   ```

## Compilation

The hardware library is automatically included when using the standard build system:

```bash
# Using compile script (recommended)
./compile_for_roomwizard.sh

# Using Makefile
make

# Manual compilation
arm-linux-gnueabihf-gcc -O2 -static \
    your_game.c \
    common/hardware.c \
    common/framebuffer.c \
    common/touch_input.c \
    -o your_game -lm
```

## Troubleshooting

### LEDs don't respond
- Check permissions on `/sys/class/leds/*/brightness`
- Verify you're running as root or have proper permissions
- Check if sysfs paths exist: `ls -l /sys/class/leds/`

### Backlight control doesn't work
- Some devices may use different paths
- Check: `find /sys -name "*backlight*"`
- Verify write permissions

### Hardware test fails
- Run with verbose output: `./hardware_test 2>&1 | tee test.log`
- Check error messages for specific issues
- Verify device is RoomWizard hardware

## Performance Notes

- LED control is very fast (< 1ms per operation)
- Backlight changes are immediate
- Pulse and blink functions are blocking
- No significant CPU overhead
- Safe to call frequently (e.g., every frame)

## Thread Safety

The hardware control functions are **not thread-safe**. If using multiple threads:

- Use mutex locks around hardware calls
- Or designate one thread for hardware control
- Avoid simultaneous LED updates from different threads

## See Also

- [`framebuffer.h`](common/framebuffer.h) - Display rendering
- [`touch_input.h`](common/touch_input.h) - Touch input handling
- [`game_common.h`](common/game_common.h) - Common game utilities
- [`analysis.md`](../analysis.md) - Hardware analysis details
