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

// Touch calibration.
//
// Model: a per-axis LINEAR map from the raw digitiser range to the full screen:
//   screen_x = (raw_x - raw_min_x) * (W-1) / (raw_max_x - raw_min_x)
//   screen_y = (raw_y - raw_min_y) * (H-1) / (raw_max_y - raw_min_y)
// The raw range lives in TouchInput (raw_min_x..raw_max_y); it defaults to the
// hardware-reported EVIOCGABS range and is overridden by calibration.
//
// The panel is linear (verified with touch_trace: a traced border comes out as a
// straight-edged rectangle, no keystone/shear), so this single scale+offset per
// axis is sufficient and reaches every edge by construction. There is deliberately
// NO affine, NO bilinear corner correction, and the bezel margins are NOT applied
// to touch coordinates (doing so double-corrects an already-accurate signal).
//
// bezel_* are kept only as a DRAWING concern: apps (and ScummVM) read them to keep
// UI / centre the game surface off the physical bezel. They never move a touch.
typedef struct {
    bool enabled;
    // UI-only obstruction margins (pixels from edge). Drawing/layout use only.
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
    // Raw digitiser range → mapped linearly onto the full screen.
    // Defaults from EVIOCGABS; overridden by calibration (edge-drag).
    int raw_min_x, raw_max_x;
    int raw_min_y, raw_max_y;
    int screen_width, screen_height;
    bool calibrated;
    TouchCalibration calib;
    bool portrait_mode;      // Portrait mode active (coordinate rotation)
} TouchInput;

// Initialize touch input
int touch_init(TouchInput *touch, const char *device);

// Close touch input
void touch_close(TouchInput *touch);

// Poll for touch events (non-blocking)
int touch_poll(TouchInput *touch);

// Wait for touch press (blocking), returns SCALED/calibrated screen coordinates
int touch_wait_for_press(TouchInput *touch, int *x, int *y);

// Wait for touch press (blocking), returning RAW hardware coordinates (no scaling).
// Used by the calibration routine to gather the raw range at the screen edges.
int touch_wait_for_press_raw(TouchInput *touch, int *raw_x, int *raw_y);

// Set screen dimensions for coordinate scaling
void touch_set_screen_size(TouchInput *touch, int width, int height);

// Get current touch state
TouchState touch_get_state(TouchInput *touch);

// Install a raw range (the calibration result). The raw span [min..max] per axis
// is mapped linearly onto the full screen. Marks calibration enabled.
void touch_set_raw_range(TouchInput *touch,
                         int min_x, int max_x, int min_y, int max_y);

// Least-squares fit screen = m*raw + b over n (raw,screen) points for one axis,
// then return the raw values that map to screen 0 and screen (dim-1). This yields
// the raw range that best fits the sampled points, robust to a single noisy sample
// (unlike anchoring to the raw extremes). Returns 0 on success, -1 if degenerate
// (n<2 or near-zero slope). All math is double (no hardware integer divide).
int touch_fit_axis_range(const int *raw, const int *scr, int n, int dim,
                         int *raw_at_0, int *raw_at_max);

// Enable/disable calibration mapping (kept for API stability; scaling always
// applies the linear raw→screen map, defaulting to the EVIOCGABS range).
void touch_enable_calibration(TouchInput *touch, bool enable);

// Save calibration (raw range + UI bezel margins) to file
int touch_save_calibration(TouchInput *touch, const char *filename);

// Load calibration from file
int touch_load_calibration(TouchInput *touch, const char *filename);

// Drain all pending events from the touch device (call after reopen)
void touch_drain_events(TouchInput *touch);

#endif
