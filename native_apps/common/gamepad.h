/**
 * Gamepad Input Module for RoomWizard
 *
 * Reusable input abstraction that merges gamepad (Xbox 360 via evdev),
 * keyboard, mouse, and touch into a unified API.  Used by all native games.
 *
 * Hardware: TI AM335x, Linux 4.14.52, evdev input subsystem.
 *
 * Mouse support added for USB mice — provides absolute cursor position
 * updated from relative movements, with configurable acceleration.
 *
 * Gamepad button mapping allows remapping evdev codes for clone controllers.
 *
 * Configuration persistence via /etc/input_config.conf (key=value format).
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

/* Axis dead zone (for analog sticks) — legacy default, now configurable */
#define GAMEPAD_DEADZONE 200

/* Axis range (normalized to +/-1000) */
#define GAMEPAD_AXIS_MAX 1000

/* Maximum touch regions */
#define GAMEPAD_MAX_TOUCH_REGIONS 10

/* Default input config file path */
#define GAMEPAD_CONFIG_PATH "/etc/input_config.conf"

/* Default screen dimensions for mouse bounds */
#define GAMEPAD_DEFAULT_SCREEN_W 800
#define GAMEPAD_DEFAULT_SCREEN_H 480

/* Default mouse acceleration parameters */
#define GAMEPAD_MOUSE_SENSITIVITY_DEFAULT   1.5f
#define GAMEPAD_MOUSE_ACCEL_DEFAULT         2.0f
#define GAMEPAD_MOUSE_LOW_THRESHOLD_DEFAULT  3
#define GAMEPAD_MOUSE_HIGH_THRESHOLD_DEFAULT 15

/* Default dead zone as percentage of axis range (0-100) */
#define GAMEPAD_DEADZONE_PCT_DEFAULT 20

/* Maximum axes tracked for calibration */
#define GAMEPAD_MAX_AXES 8

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

/**
 * Configurable gamepad button mapping.
 *
 * Maps abstract game actions to evdev button/axis codes.
 * Defaults match a genuine Xbox 360 controller.  Override for
 * clone/knockoff controllers that use different codes.
 */
typedef struct {
    /* Face buttons — evdev key codes */
    int btn_jump;       /* Default: BTN_SOUTH (304) — A on Xbox */
    int btn_run;        /* Default: BTN_EAST  (305) — B on Xbox */
    int btn_action;     /* Default: BTN_WEST  (308) — X on Xbox */
    int btn_pause;      /* Default: BTN_START (315) */
    int btn_back;       /* Default: BTN_SELECT (314) */

    /* D-pad axes */
    int hat_x_axis;     /* Default: ABS_HAT0X (16) */
    int hat_y_axis;     /* Default: ABS_HAT0Y (17) */

    /* Analog stick axes */
    int stick_x_axis;   /* Default: ABS_X  (0) — left stick X */
    int stick_y_axis;   /* Default: ABS_Y  (1) — left stick Y */
    int stick_rx_axis;  /* Default: ABS_RX (3) — right stick X */
    int stick_ry_axis;  /* Default: ABS_RY (4) — right stick Y */
} GamepadButtonMap;

/**
 * Per-axis calibration data for dead zone and center offset.
 */
typedef struct {
    int center;         /* Measured center value (some cheap pads don't center at 0) */
    int deadzone_pct;   /* Dead zone as percentage of half-range (0-100) */
} AxisCalibration;

/**
 * Mouse acceleration parameters.
 */
typedef struct {
    float sensitivity;      /* Base sensitivity multiplier (default 1.5) */
    float acceleration;     /* Extra acceleration factor for fast moves (default 2.0) */
    int   low_threshold;    /* Speed below this = 1:1 precision (default 3) */
    int   high_threshold;   /* Speed above this = extra acceleration (default 15) */
} MouseAccelConfig;

/* Unified input state */
typedef struct {
    ButtonState buttons[BTN_ID_COUNT];
    int axis_lx;    /* Left stick X: -1000 to +1000 */
    int axis_ly;    /* Left stick Y: -1000 to +1000 */
    int axis_rx;    /* Right stick X: -1000 to +1000 */
    int axis_ry;    /* Right stick Y: -1000 to +1000 */
    bool gamepad_connected;
    bool keyboard_connected;

    /* Mouse cursor state (absolute position on screen, updated by relative movements) */
    int mouse_x, mouse_y;              /* Current cursor position (0..screen_w-1, 0..screen_h-1) */
    int mouse_connected;               /* Whether a mouse device is detected */
    int mouse_left_held,   mouse_left_pressed,   mouse_left_released;
    int mouse_right_held,  mouse_right_pressed,  mouse_right_released;
    int mouse_middle_held, mouse_middle_pressed,  mouse_middle_released;
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
    int mouse_fd;

    /* Internal previous-frame state for edge detection (abstract buttons) */
    bool prev_held[BTN_ID_COUNT];

    /* Internal previous-frame state for mouse button edge detection */
    bool prev_mouse_left;
    bool prev_mouse_right;
    bool prev_mouse_middle;

    /* Axis calibration data (up to GAMEPAD_MAX_AXES axes) */
    int axis_min[GAMEPAD_MAX_AXES];
    int axis_max[GAMEPAD_MAX_AXES];
    int axis_flat[GAMEPAD_MAX_AXES];

    /* Per-axis center calibration and configurable dead zone */
    AxisCalibration axis_calib[GAMEPAD_MAX_AXES];

    /* Configurable dead zone percentage (0-100), applied uniformly unless
     * per-axis overrides are set via axis_calib[].deadzone_pct */
    int deadzone_pct;

    /* Touch regions for virtual controls */
    TouchRegion touch_regions[GAMEPAD_MAX_TOUCH_REGIONS];
    int touch_region_count;

    /* Configurable button mapping for gamepad */
    GamepadButtonMap button_map;

    /* Mouse state */
    int mouse_x, mouse_y;                  /* Accumulated absolute position */
    int mouse_screen_w, mouse_screen_h;    /* Bounds for clamping */
    MouseAccelConfig mouse_accel;          /* Acceleration parameters */
} GamepadManager;

/**
 * Initialize the gamepad manager — scans /dev/input/event* for gamepad,
 * keyboard, and mouse.  Automatically loads config from GAMEPAD_CONFIG_PATH
 * if the file exists.
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

/* ── Mouse API ──────────────────────────────────────────────────────────── */

/**
 * Set the screen bounds used for clamping the mouse cursor position.
 * Default is 800×480 (RoomWizard native resolution).
 */
void gamepad_set_mouse_bounds(GamepadManager *gp, int width, int height);

/**
 * Warp the mouse cursor to an absolute screen position.
 * Clamped to current screen bounds.
 */
void gamepad_set_mouse_position(GamepadManager *gp, int x, int y);

/* ── Button Mapping API ─────────────────────────────────────────────────── */

/**
 * Override the gamepad button mapping.  Pass NULL to reset to defaults.
 */
void gamepad_set_button_map(GamepadManager *gp, const GamepadButtonMap *map);

/**
 * Return a GamepadButtonMap initialized with Xbox 360 defaults.
 */
GamepadButtonMap gamepad_get_default_button_map(void);

/* ── Configuration Persistence ──────────────────────────────────────────── */

/**
 * Load input configuration from a key=value file.
 * Missing keys get sensible defaults.  Unknown keys are ignored.
 * Returns 0 on success, -1 if the file could not be opened (defaults apply).
 */
int gamepad_load_config(GamepadManager *gp, const char *path);

/**
 * Save current input configuration to a key=value file.
 * Returns 0 on success, -1 on write error.
 */
int gamepad_save_config(const GamepadManager *gp, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* GAMEPAD_H */
