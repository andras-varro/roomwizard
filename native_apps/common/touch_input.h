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
// 1. CALIBRATION — raw digitiser value to PANEL pixel, a per-axis PIECEWISE
//    LINEAR map: three segments joined at two knots, fixed at panel dim/4 and
//    3*dim/4. Four raw values per axis define it — the raw readings at panel
//    0, dim/4, 3*dim/4 and dim-1:
//
//      raw_min .. raw_knot_lo   ->  panel      0 .. dim/4      (outer segment)
//      raw_knot_lo .. raw_knot_hi -> panel dim/4 .. 3*dim/4    (fitted interior)
//      raw_knot_hi .. raw_max   ->  panel 3*dim/4 .. dim-1     (outer segment)
//
//    Why three segments and not one: the format allows for interior curvature if
//    a panel ever shows it. On the reference unit it does not — the measured
//    residuals are ±80 raw with no consistent sign, i.e. tap noise — so the knots
//    land on the fitted line and the stored curve IS a straight line. The format
//    is kept anyway: it costs nothing, and changing the field count on line 1 is
//    the one edit that breaks a component still linking a stale touch_input.o,
//    silently (its 4-number sscanf succeeds on the first four of eight).
//
//    What the panel DOES do is saturate before the physical edge. Measured on
//    RW09, raw pins at 4095 from panel row ~450 of 480 and at 0 from row ~30, so
//    the fitted line's endpoints legitimately fall outside 0..4095 (the
//    measured-good Y fit is -279..4382). Those endpoints are stored as fitted and
//    must NOT be clamped into the emittable range: clamping asserts that raw 4095
//    is emitted at panel 479, which tilts the upper outer segment and makes the
//    reported position run ahead of the finger across the whole bottom quarter.
//    The price of the honest map is that the saturated bands genuinely cannot be
//    pressed — see SCREEN_SAFE_* vs SCREEN_VISIBLE_* in framebuffer.h. They are
//    still fully drawable.
//
//    A single linear map is the special case where the knots sit exactly on the
//    line joining the endpoints, so nothing needs a separate code path — see
//    touch_knots_on_line(). Still no affine term, no rotation, no shear and no
//    bilinear corner correction: each axis is an independent monotone 1-D curve.
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
    // Stage 1: raw digitiser curve → panel. Defaults from EVIOCGABS.
    // raw_min/raw_max are the endpoints; raw_knot_lo/hi are the interior knots
    // at panel dim/4 and 3*dim/4. Knots on the endpoint line == a linear map.
    int raw_min_x, raw_max_x;
    int raw_min_y, raw_max_y;
    int raw_knot_lo_x, raw_knot_hi_x;
    int raw_knot_lo_y, raw_knot_hi_y;
    // What the DIGITISER declares via EVIOCGABS, kept separately because loading
    // a calibration overwrites raw_min/raw_max with the fitted curve's endpoints
    // — which legitimately fall outside the emittable range.
    int hw_min_x, hw_max_x;
    int hw_min_y, hw_max_y;
    // What the physical EDGES actually emit, measured by the calibration wizard's
    // edge sweep and stored on line 3 of the config. Defaults to the hw range,
    // which is the optimistic assumption. Pushing these through the curve is what
    // gives the touch-safe inset: the panel band between an edge and the row where
    // its raw value stops changing cannot be addressed at all.
    int reach_min_x, reach_max_x;
    int reach_min_y, reach_max_y;
    int panel_width, panel_height;   // full panel, app orientation
    // Stage 2: panel → logical
    int view_x, view_y;              // logical surface origin within the panel
    int screen_width, screen_height; // logical (visible) size
    bool calibrated;
    TouchCalibration calib;
    bool portrait_mode;      // Portrait mode active (coordinate rotation)
} TouchInput;

// Panel positions of the two interior knots, for a given panel extent. Fixed
// fractions so the file format needs only raw values. They land on real
// calibration targets on this panel: X 200/600 of 800, Y 120/360 of 480.
#define TOUCH_KNOT_LO(dim) ((dim) / 4)
#define TOUCH_KNOT_HI(dim) (((dim) * 3) / 4)

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

// The canonical stage-1 axis map: one raw value → panel pixel, piecewise over
// the three segments defined by v0 (panel 0), k_lo (panel dim/4), k_hi (panel
// 3*dim/4) and v1 (panel dim-1).
//
// Deliberately does NOT clamp: predicting where a raw value lands, including
// off the panel, is how the wizard reports reach. scale_coordinates() clamps
// around it. Everything that needs to know where a raw value maps must call
// this — the fit, the reach readout and the live mapping cannot be allowed to
// drift apart, which they did when the arithmetic existed in several places.
int touch_map_axis_panel(long raw, int v0, int k_lo, int k_hi, int v1, int dim);

// Get current touch state
TouchState touch_get_state(TouchInput *touch);

// Install a raw range (the calibration result). The raw span [min..max] per axis
// is mapped LINEARLY onto the full panel — the knots are placed on the endpoint
// line, so this is the degenerate one-segment case. Marks calibration enabled.
void touch_set_raw_range(TouchInput *touch,
                         int min_x, int max_x, int min_y, int max_y);

// Install a piecewise curve: per axis the raw readings at panel 0, dim/4,
// 3*dim/4 and dim-1. Values must be strictly increasing per axis; pass
// k_lo == k_hi == 0 for a plain linear map, and a non-monotone axis falls back
// to that. Endpoints outside the emittable raw range are expected and correct —
// do not clamp them (see the header comment above). Marks calibration enabled.
void touch_set_raw_curve(TouchInput *touch,
                         int x0, int xk_lo, int xk_hi, int x1,
                         int y0, int yk_lo, int yk_hi, int y1);

// Record what the physical edges actually emit, as measured by the calibration
// wizard's edge sweep, and recompute the touch-safe inset from it. Defaults to
// the EVIOCGABS range, which assumes every edge drives raw all the way. A
// degenerate range is rejected rather than applied.
void touch_set_edge_reach(TouchInput *touch,
                          int x_lo, int x_hi, int y_lo, int y_hi);

// Knot raw values that put the piecewise map exactly on the straight line from
// v0 to v1 over a panel extent of `dim`. Use this to migrate a legacy
// endpoint-only calibration without shifting its interior mapping. Note the knot
// positions are dim/4 and 3*dim/4 of a (dim-1) span, so this is NOT v0 + span/4.
//
// For a plain linear map, prefer the k_lo == k_hi == 0 sentinel — it is exact by
// construction and needs no panel extent.
void touch_knots_on_line(int v0, int v1, int dim, int *k_lo, int *k_hi);

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

// Save calibration to file. Line 1 carries eight numbers (the four raw values per
// axis), line 2 the bezel margins, line 3 the measured edge reach.
int touch_save_calibration(TouchInput *touch, const char *filename);

// Load calibration from file. Line 1 accepts either form:
//   8 numbers  x0 xk_lo xk_hi x1  y0 yk_lo yk_hi y1   (piecewise, current)
//   4 numbers  min_x max_x min_y max_y                (legacy, endpoints only)
// A legacy line is migrated in place: the knots are placed on its line so the
// mapping it described is reproduced exactly. Nothing is clamped — a legacy file
// carries no edge measurement, so there is no basis for moving its endpoints, and
// the revision that pulled them into 0..4095 is what tilted the outer segments.
// An optional keyword line "reach x_lo x_hi y_lo y_hi" carries the measured edge
// extremes; absent, every edge is assumed to reach the hardware limit.
int touch_load_calibration(TouchInput *touch, const char *filename);

// Drain all pending events from the touch device (call after reopen)
void touch_drain_events(TouchInput *touch);

#endif
