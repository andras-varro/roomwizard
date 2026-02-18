#include "touch_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

// Scale raw touch coordinates to screen coordinates with calibration
static void scale_coordinates(TouchInput *touch, int *x, int *y) {
    // RoomWizard touch device reports 12-bit coordinates (0-4095)
    // Scale to screen resolution (800x480)
    const int TOUCH_MAX = 4095;
    
    // Linear scaling first
    *x = (*x * touch->screen_width) / TOUCH_MAX;
    *y = (*y * touch->screen_height) / TOUCH_MAX;
    
    // Apply calibration if enabled
    if (touch->calib.enabled) {
        // Bilinear interpolation of corner offsets
        // Calculate normalized position (0.0 to 1.0)
        float norm_x = (float)*x / touch->screen_width;
        float norm_y = (float)*y / touch->screen_height;
        
        // Interpolate X offset
        float top_offset_x = touch->calib.top_left_x * (1.0f - norm_x) + 
                             touch->calib.top_right_x * norm_x;
        float bottom_offset_x = touch->calib.bottom_left_x * (1.0f - norm_x) + 
                                touch->calib.bottom_right_x * norm_x;
        float offset_x = top_offset_x * (1.0f - norm_y) + bottom_offset_x * norm_y;
        
        // Interpolate Y offset
        float left_offset_y = touch->calib.top_left_y * (1.0f - norm_y) + 
                              touch->calib.bottom_left_y * norm_y;
        float right_offset_y = touch->calib.top_right_y * (1.0f - norm_y) + 
                               touch->calib.bottom_right_y * norm_y;
        float offset_y = left_offset_y * (1.0f - norm_x) + right_offset_y * norm_x;
        
        // Apply offsets
        *x += (int)offset_x;
        *y += (int)offset_y;
    }
    
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
        printf("ERROR: Failed to open %s, fd=%d\n", device, touch->fd);
        return -1;
    }
    
    printf("Touch device opened successfully: %s (fd=%d)\n", device, touch->fd);
    
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
    
    // Initialize calibration offsets to zero (no correction)
    touch->calib.top_left_x = 0;
    touch->calib.top_left_y = 0;
    touch->calib.top_right_x = 0;
    touch->calib.top_right_y = 0;
    touch->calib.bottom_left_x = 0;
    touch->calib.bottom_left_y = 0;
    touch->calib.bottom_right_x = 0;
    touch->calib.bottom_right_y = 0;
    touch->calib.enabled = false;
    // Initialize bezel margins to zero (no obstruction)
    touch->calib.bezel_top = 0;
    touch->calib.bezel_bottom = 0;
    touch->calib.bezel_left = 0;
    touch->calib.bezel_right = 0;
    
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

void touch_set_calibration(TouchInput *touch,
                           int tl_x, int tl_y,
                           int tr_x, int tr_y,
                           int bl_x, int bl_y,
                           int br_x, int br_y) {
    touch->calib.top_left_x = tl_x;
    touch->calib.top_left_y = tl_y;
    touch->calib.top_right_x = tr_x;
    touch->calib.top_right_y = tr_y;
    touch->calib.bottom_left_x = bl_x;
    touch->calib.bottom_left_y = bl_y;
    touch->calib.bottom_right_x = br_x;
    touch->calib.bottom_right_y = br_y;
    
    printf("Touch calibration set: TL(%d,%d) TR(%d,%d) BL(%d,%d) BR(%d,%d)\n",
           tl_x, tl_y, tr_x, tr_y, bl_x, bl_y, br_x, br_y);
}

void touch_enable_calibration(TouchInput *touch, bool enable) {
    touch->calib.enabled = enable;
    printf("Touch calibration %s\n", enable ? "enabled" : "disabled");
}

int touch_save_calibration(TouchInput *touch, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open calibration file for writing");
        return -1;
    }
    
    // Write calibration data in simple text format
    fprintf(f, "# Touch Calibration Data\n");
    fprintf(f, "# Format: top_left_x top_left_y top_right_x top_right_y bottom_left_x bottom_left_y bottom_right_x bottom_right_y\n");
    fprintf(f, "%d %d %d %d %d %d %d %d\n",
            touch->calib.top_left_x, touch->calib.top_left_y,
            touch->calib.top_right_x, touch->calib.top_right_y,
            touch->calib.bottom_left_x, touch->calib.bottom_left_y,
            touch->calib.bottom_right_x, touch->calib.bottom_right_y);
    fprintf(f, "# Bezel Obstruction Margins (pixels from edge)\n");
    fprintf(f, "# Format: bezel_top bezel_bottom bezel_left bezel_right\n");
    fprintf(f, "%d %d %d %d\n",
            touch->calib.bezel_top, touch->calib.bezel_bottom,
            touch->calib.bezel_left, touch->calib.bezel_right);
    
    fclose(f);
    printf("Calibration saved to: %s\n", filename);
    return 0;
}

int touch_load_calibration(TouchInput *touch, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        // Not an error if file doesn't exist - just means no calibration yet
        printf("No calibration file found: %s (using defaults)\n", filename);
        return -1;
    }
    
    // Skip comment lines and parse data lines
    char line[256];
    int line_num = 0;
    bool got_touch_calib = false;
    bool got_bezel_margins = false;
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != '#' && line[0] != '\n') {
            if (!got_touch_calib) {
                // Parse touch calibration values (first data line)
                int values[8];
                if (sscanf(line, "%d %d %d %d %d %d %d %d",
                          &values[0], &values[1], &values[2], &values[3],
                          &values[4], &values[5], &values[6], &values[7]) == 8) {
                    touch->calib.top_left_x = values[0];
                    touch->calib.top_left_y = values[1];
                    touch->calib.top_right_x = values[2];
                    touch->calib.top_right_y = values[3];
                    touch->calib.bottom_left_x = values[4];
                    touch->calib.bottom_left_y = values[5];
                    touch->calib.bottom_right_x = values[6];
                    touch->calib.bottom_right_y = values[7];
                    got_touch_calib = true;
                    printf("Calibration loaded from: %s\n", filename);
                    printf("  TL(%d,%d) TR(%d,%d) BL(%d,%d) BR(%d,%d)\n",
                           values[0], values[1], values[2], values[3],
                           values[4], values[5], values[6], values[7]);
                }
            } else if (!got_bezel_margins) {
                // Parse bezel margins (second data line)
                int margins[4];
                if (sscanf(line, "%d %d %d %d",
                          &margins[0], &margins[1], &margins[2], &margins[3]) == 4) {
                    touch->calib.bezel_top = margins[0];
                    touch->calib.bezel_bottom = margins[1];
                    touch->calib.bezel_left = margins[2];
                    touch->calib.bezel_right = margins[3];
                    got_bezel_margins = true;
                    printf("  Bezel margins: T=%d B=%d L=%d R=%d\n",
                           margins[0], margins[1], margins[2], margins[3]);
                }
            }
        }
    }
    
    fclose(f);
    
    if (!got_touch_calib) {
        fprintf(stderr, "Failed to parse calibration file: %s\n", filename);
        return -1;
    }
    
    // Bezel margins are optional (backward compatibility)
    if (!got_bezel_margins) {
        printf("  No bezel margins found (using defaults: 0)\n");
    }
    
    return 0;
}
