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

// Calibration data for resistive touchscreen
// Stores offset corrections for each corner to fix non-linearity
typedef struct {
    // Corner offsets in pixels (applied after linear scaling)
    int top_left_x, top_left_y;
    int top_right_x, top_right_y;
    int bottom_left_x, bottom_left_y;
    int bottom_right_x, bottom_right_y;
    bool enabled;
    // Bezel obstruction margins (pixels from edge)
    int bezel_top;
    int bezel_bottom;
    int bezel_left;
    int bezel_right;
} TouchCalibration;

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
    TouchCalibration calib;
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

// Set calibration offsets (in pixels, applied after linear scaling)
void touch_set_calibration(TouchInput *touch, 
                           int tl_x, int tl_y,    // top-left offset
                           int tr_x, int tr_y,    // top-right offset
                           int bl_x, int bl_y,    // bottom-left offset
                           int br_x, int br_y);   // bottom-right offset

// Enable/disable calibration
void touch_enable_calibration(TouchInput *touch, bool enable);

// Save calibration to file
int touch_save_calibration(TouchInput *touch, const char *filename);

// Load calibration from file
int touch_load_calibration(TouchInput *touch, const char *filename);

#endif
