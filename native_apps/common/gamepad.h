/**
 * Gamepad Input Module for RoomWizard
 *
 * Reusable input abstraction that merges gamepad (Xbox 360 via evdev),
 * keyboard, and touch into a unified API.  Used by all native games.
 *
 * Hardware: TI AM335x, Linux 4.14.52, evdev input subsystem.
 */

#ifndef GAMEPAD_H
#define GAMEPAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Maximum devices to scan */
#define GAMEPAD_MAX_DEVICES 16

/* Axis dead zone (for analog sticks) */
#define GAMEPAD_DEADZONE 200

/* Axis range (normalized to +/-1000) */
#define GAMEPAD_AXIS_MAX 1000

/* Maximum touch regions */
#define GAMEPAD_MAX_TOUCH_REGIONS 10

/* Button state with edge detection */
typedef struct {
    bool held;      /* Currently held down */
    bool pressed;   /* Just pressed this frame (edge) */
    bool released;  /* Just released this frame (edge) */
} ButtonState;

/* Abstract input buttons (unified across gamepad/keyboard/touch) */
typedef enum {
    BTN_ID_UP,
    BTN_ID_DOWN,
    BTN_ID_LEFT,
    BTN_ID_RIGHT,
    BTN_ID_JUMP,      /* Gamepad A/South, Keyboard Space, Touch jump button */
    BTN_ID_RUN,       /* Gamepad B/East, Keyboard Shift */
    BTN_ID_ACTION,    /* Gamepad X/West, Keyboard Enter */
    BTN_ID_PAUSE,     /* Gamepad Start, Keyboard Escape */
    BTN_ID_BACK,      /* Gamepad Select/Back, Keyboard Backspace */
    BTN_ID_COUNT
} ButtonId;

/* Unified input state */
typedef struct {
    ButtonState buttons[BTN_ID_COUNT];
    int axis_lx;    /* Left stick X: -1000 to +1000 */
    int axis_ly;    /* Left stick Y: -1000 to +1000 */
    int axis_rx;    /* Right stick X: -1000 to +1000 */
    int axis_ry;    /* Right stick Y: -1000 to +1000 */
    bool gamepad_connected;
    bool keyboard_connected;
} InputState;

/* Touch button region — maps a screen area to an abstract button */
typedef struct {
    int x, y, w, h;
    ButtonId button;
} TouchRegion;

/* Gamepad manager (holds evdev fds and internal state) */
typedef struct {
    int gamepad_fd;
    int keyboard_fd;
    /* Internal previous-frame state for edge detection */
    bool prev_held[BTN_ID_COUNT];
    /* Axis calibration data (up to 8 axes) */
    int axis_min[8];
    int axis_max[8];
    int axis_flat[8];
    /* Touch regions for virtual controls */
    TouchRegion touch_regions[GAMEPAD_MAX_TOUCH_REGIONS];
    int touch_region_count;
} GamepadManager;

/**
 * Initialize the gamepad manager — scans /dev/input/event* for gamepad and keyboard.
 * Returns 0 on success (even if no devices found — they can be hot-plugged).
 */
int gamepad_init(GamepadManager *gm);

/**
 * Close all open device fds.
 */
void gamepad_close(GamepadManager *gm);

/**
 * Poll all input devices and update the input state.
 * Call once per frame before reading state.
 * Also accepts touch coordinates for virtual button mapping.
 */
void gamepad_poll(GamepadManager *gm, InputState *state,
                  int touch_x, int touch_y, bool touch_active);

/**
 * Re-scan for devices (call periodically or on hotplug).
 */
void gamepad_rescan(GamepadManager *gm);

/**
 * Configure touch button regions for virtual controls.
 * These define screen areas that map to abstract buttons.
 */
void gamepad_set_touch_regions(GamepadManager *gm, TouchRegion *regions, int count);

/**
 * Draw virtual touch controls on screen (if no gamepad/keyboard detected).
 * This is optional — games can draw their own or skip.
 * fb parameter is cast internally to Framebuffer*.
 */
void gamepad_draw_touch_controls(void *fb, InputState *state);

#ifdef __cplusplus
}
#endif

#endif /* GAMEPAD_H */
