#include "touch_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

// Scale raw touch coordinates to screen coordinates
static void scale_coordinates(TouchInput *touch, int *x, int *y) {
    // RoomWizard touch device reports 12-bit coordinates (0-4095)
    // Scale to screen resolution (800x480)
    const int TOUCH_MAX = 4095;
    
    *x = (*x * touch->screen_width) / TOUCH_MAX;
    *y = (*y * touch->screen_height) / TOUCH_MAX;
    
    // Clamp to screen bounds
    if (*x < 0) *x = 0;
    if (*x >= touch->screen_width) *x = touch->screen_width - 1;
    if (*y < 0) *y = 0;
    if (*y >= touch->screen_height) *y = touch->screen_height - 1;
}

int touch_init(TouchInput *touch, const char *device) {
    touch->fd = open(device, O_RDONLY | O_NONBLOCK);
    if (touch->fd == -1) {
        perror("Error opening touch device");
        return -1;
    }
    
    touch->state.x = 0;
    touch->state.y = 0;
    touch->state.pressed = false;
    touch->state.released = false;
    touch->state.held = false;
    touch->last_x = 0;
    touch->last_y = 0;
    touch->touching = false;
    
    // Initialize calibration
    touch->raw_min_x = 0;
    touch->raw_max_x = 0;
    touch->raw_min_y = 0;
    touch->raw_max_y = 0;
    touch->screen_width = 800;  // Default
    touch->screen_height = 480;  // Default
    touch->calibrated = false;
    
    printf("Touch input initialized: %s\n", device);
    return 0;
}

void touch_set_screen_size(TouchInput *touch, int width, int height) {
    touch->screen_width = width;
    touch->screen_height = height;
    printf("Touch screen size set to: %dx%d\n", width, height);
}

void touch_close(TouchInput *touch) {
    if (touch->fd != -1) {
        close(touch->fd);
    }
}

int touch_wait_for_press(TouchInput *touch, int *x, int *y) {
    // Wait for a touch press event (blocking)
    //
    // CRITICAL: Linux input events arrive in this order:
    //   1. ABS_X, ABS_Y (coordinates)
    //   2. BTN_TOUCH (press/release)
    //   3. SYN_REPORT (frame complete)
    //
    // We must capture coordinates BEFORE checking for press!
    
    struct input_event ev;
    int current_x = -1, current_y = -1;
    bool got_press = false;
    
    // Set to blocking mode temporarily
    int flags = fcntl(touch->fd, F_GETFL, 0);
    fcntl(touch->fd, F_SETFL, flags & ~O_NONBLOCK);
    
    while (1) {
        if (read(touch->fd, &ev, sizeof(ev)) == sizeof(ev)) {
            // Always capture coordinates first (they come before BTN_TOUCH)
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X) {
                    current_x = ev.value;
                } else if (ev.code == ABS_Y) {
                    current_y = ev.value;
                }
            }
            // Then check for touch events
            else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                if (ev.value == 0) {
                    // Release - ready for next press
                    touch->touching = false;
                    got_press = false;
                    // Don't clear coordinates here - they might be for the next touch
                } else if (ev.value == 1 && !touch->touching) {
                    // New press detected - mark it
                    touch->touching = true;
                    got_press = true;
                }
            }
            // Finally check for sync (end of event frame)
            else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                // If we got a press and have valid coordinates, return them
                if (got_press && current_x >= 0 && current_y >= 0) {
                    *x = current_x;
                    *y = current_y;
                    // Scale coordinates
                    scale_coordinates(touch, x, y);
                    // Restore non-blocking mode
                    fcntl(touch->fd, F_SETFL, flags);
                    // Reset for next call
                    current_x = -1;
                    current_y = -1;
                    return 0;
                }
                // Reset got_press after each frame
                got_press = false;
            }
        }
    }
    
    // Restore non-blocking mode
    fcntl(touch->fd, F_SETFL, flags);
    return -1;
}

int touch_poll(TouchInput *touch) {
    // Non-blocking poll for touch events
    //
    // CRITICAL: Process events in the order they arrive:
    //   1. ABS_X, ABS_Y (coordinates) - captured to last_x/last_y
    //   2. BTN_TOUCH (press/release) - uses already-captured coordinates
    //   3. SYN_REPORT (frame complete) - updates state
    
    struct input_event ev;
    int events_read = 0;
    
    // Reset transient states
    touch->state.pressed = false;
    touch->state.released = false;
    
    while (read(touch->fd, &ev, sizeof(ev)) == sizeof(ev)) {
        events_read++;
        
        // Always capture coordinates first (they arrive before BTN_TOUCH)
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) {
                touch->last_x = ev.value;
            } else if (ev.code == ABS_Y) {
                touch->last_y = ev.value;
            }
        }
        // Then process touch press/release (uses last_x/last_y captured above)
        else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1 && !touch->touching) {
                // Touch pressed
                touch->touching = true;
                touch->state.pressed = true;
                touch->state.held = true;
                int x = touch->last_x;
                int y = touch->last_y;
                scale_coordinates(touch, &x, &y);
                touch->state.x = x;
                touch->state.y = y;
            } else if (ev.value == 0 && touch->touching) {
                // Touch released
                touch->touching = false;
                touch->state.released = true;
                touch->state.held = false;
            }
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            // Update position if still touching
            if (touch->touching) {
                int x = touch->last_x;
                int y = touch->last_y;
                scale_coordinates(touch, &x, &y);
                touch->state.x = x;
                touch->state.y = y;
            }
        }
    }
    
    return events_read;
}

TouchState touch_get_state(TouchInput *touch) {
    return touch->state;
}
