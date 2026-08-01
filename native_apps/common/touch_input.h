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

// A touch is mapped to app coordinates in two independent stages.
//
// 1. CALIBRATION — raw digitiser value to PANEL pixel, a per-axis linear map:
//      panel_x = (raw_x - raw_min_x) * (panel_w-1) / (raw_max_x - raw_min_x)
//      panel_y = (raw_y - raw_min_y) * (panel_h-1) / (raw_max_y - raw_min_y)
//    The raw range lives in TouchInput (raw_min_x..raw_max_y), defaults to the
//    hardware-reported EVIOCGABS range, and is overridden by calibration.
//    The panel is linear — a traced border comes out as a straight-edged
//    rectangle, no keystone or shear — so scale+offset per axis is accurate
//    everywhere and reaches every edge. There is no affine term and no bilinear
//    corner correction.
//
// 2. BEZEL VIEWPORT — panel pixel to LOGICAL pixel, a translation by the
//    viewport origin (see framebuffer.h). The bezel hides a band of panel
//    pixels; the drawing surface is the rectangle inside them, so a touch is
//    shifted by the same origin the framebuffer draws at. Touch and drawing
//    therefore share one coordinate system, and apps see neither stage.
//
// The two stages must not be conflated: calibration maps onto the WHOLE panel,
// bezel included, and is unaffected by the bezel margins.
typedef struct {
    bool enabled;
    // Bezel margins (pixels hidden at each panel edge). Authoritative copy is
    // in framebuffer.c; these are the values as loaded from / saved to file.
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
    // Stage 1: raw digitiser range → panel. Defaults from EVIOCGABS.
    int raw_min_x, raw_max_x;
    int raw_min_y, raw_max_y;
    int panel_width, panel_height;   // full panel, app orientation
    // Stage 2: panel → logical
    int view_x, view_y;              // logical surface origin within the panel
    int screen_width, screen_height; // logical (visible) size
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

// Set the logical (visible) screen dimensions. Also refreshes the panel and
// viewport geometry from the framebuffer globals, so calling this after
// fb_init() is enough to keep touch and drawing in the same space.
void touch_set_screen_size(TouchInput *touch, int width, int height);

// Set the bezel viewport explicitly: the full panel size and the origin of the
// logical surface within it. Use after fb_set_bezel() to keep touch aligned.
void touch_set_viewport(TouchInput *touch, int panel_w, int panel_h,
                        int view_x, int view_y);

// Map a raw digitiser reading to logical screen coordinates, in place. This is
// the same path touch_poll() uses, exposed so calibration can validate against
// the real mapping instead of reimplementing it.
void touch_map_raw(TouchInput *touch, int *x, int *y);

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
