#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int x;
    int y;
    bool pressed;
    bool released;
    bool held;
} TouchState;

typedef struct {
    int fd;
    TouchState state;
    int last_x;
    int last_y;
    bool touching;
    // Calibration for coordinate scaling
    int raw_min_x, raw_max_x;
    int raw_min_y, raw_max_y;
    int screen_width, screen_height;
    bool calibrated;
} TouchInput;

// Initialize touch input
int touch_init(TouchInput *touch, const char *device);

// Close touch input
void touch_close(TouchInput *touch);

// Poll for touch events (non-blocking)
int touch_poll(TouchInput *touch);

// Wait for touch press (blocking)
int touch_wait_for_press(TouchInput *touch, int *x, int *y);

// Set screen dimensions for coordinate scaling
void touch_set_screen_size(TouchInput *touch, int width, int height);

// Get current touch state
TouchState touch_get_state(TouchInput *touch);

#endif
