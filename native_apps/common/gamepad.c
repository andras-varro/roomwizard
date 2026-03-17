/**
 * Gamepad Input Module for RoomWizard — Implementation
 *
 * Scans evdev devices for gamepad, keyboard, and mouse, reads events via
 * non-blocking reads, normalizes axes, and provides unified input state
 * with edge detection.  Touch regions map screen areas to virtual buttons.
 *
 * Mouse support: reads REL_X/REL_Y relative movements and accumulates them
 * into an absolute cursor position with configurable acceleration.
 *
 * Gamepad button mapping: remappable evdev codes for clone controllers.
 *
 * Config persistence: key=value file at /etc/input_config.conf.
 *
 * Reference patterns extracted from native_apps/usb_test/usb_test.c.
 */

#include "gamepad.h"
#include "framebuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <errno.h>

/* ── Bit-test helpers (same pattern as usb_test.c) ──────────────────────── */
#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(x)        ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)          ((x) % BITS_PER_LONG)
#define BIT_LONG(x)     ((x) / BITS_PER_LONG)
#define test_bit(b, a)  ((a[BIT_LONG(b)] >> OFF(b)) & 1)

/* ── Internal device type for scanning ──────────────────────────────────── */
typedef enum { GDEV_UNKNOWN, GDEV_KEYBOARD, GDEV_GAMEPAD, GDEV_MOUSE } GDevType;

/* ── Forward declarations ───────────────────────────────────────────────── */
static void apply_defaults(GamepadManager *gm);

/* ── Return Xbox 360 default button map ─────────────────────────────────── */
GamepadButtonMap gamepad_get_default_button_map(void) {
    GamepadButtonMap map;
    map.btn_jump     = BTN_SOUTH;   /* 304 — A */
    map.btn_run      = BTN_EAST;    /* 305 — B */
    map.btn_action   = BTN_WEST;    /* 308 — X */
    map.btn_pause    = BTN_START;   /* 315 */
    map.btn_back     = BTN_SELECT;  /* 314 */
    map.hat_x_axis   = ABS_HAT0X;  /* 16  */
    map.hat_y_axis   = ABS_HAT0Y;  /* 17  */
    map.stick_x_axis = ABS_X;      /* 0   */
    map.stick_y_axis = ABS_Y;      /* 1   */
    map.stick_rx_axis = ABS_RX;    /* 3   */
    map.stick_ry_axis = ABS_RY;    /* 4   */
    return map;
}

/* ── Classify an evdev device ───────────────────────────────────────────── */
static GDevType classify_device(int fd) {
    unsigned long ev[NBITS(EV_MAX)] = {0};
    unsigned long kb[NBITS(KEY_MAX)] = {0};
    unsigned long ab[NBITS(ABS_MAX)] = {0};
    unsigned long rb[NBITS(REL_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0)
        return GDEV_UNKNOWN;

    bool has_key = test_bit(EV_KEY, ev);
    bool has_abs = test_bit(EV_ABS, ev);
    bool has_rel = test_bit(EV_REL, ev);

    if (has_key)
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kb)), kb);
    if (has_abs)
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ab)), ab);
    if (has_rel)
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rb)), rb);

    /* Gamepad: has ABS_X, ABS_Y + gamepad buttons */
    if (has_abs && has_key && test_bit(ABS_X, ab) && test_bit(ABS_Y, ab) &&
        (test_bit(BTN_GAMEPAD, kb) || test_bit(BTN_SOUTH, kb) ||
         test_bit(BTN_A, kb) || test_bit(BTN_JOYSTICK, kb)))
        return GDEV_GAMEPAD;

    /* Mouse: has REL_X, REL_Y, BTN_LEFT — relative pointing device */
    if (has_rel && has_key &&
        test_bit(REL_X, rb) && test_bit(REL_Y, rb) &&
        test_bit(BTN_LEFT, kb))
        return GDEV_MOUSE;

    /* Keyboard: has enough letter keys */
    if (has_key) {
        static const int letter_keys[] = {
            KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
            KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
            KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M
        };
        int count = 0;
        for (int k = 0; k < (int)(sizeof(letter_keys) / sizeof(letter_keys[0])); k++)
            if (test_bit(letter_keys[k], kb)) count++;
        if (count >= 20)
            return GDEV_KEYBOARD;
    }

    return GDEV_UNKNOWN;
}

/* ── Axis index mapping ─────────────────────────────────────────────────── */
/* We track 8 axes: indices 0-7 correspond to the evdev axis codes listed
 * in the axes[] array used by load_axis_calibration.  This helper returns
 * the internal index for a given evdev axis code, or -1 if not tracked. */
static const int g_tracked_axes[] = {
    /* 0 */ 0 /* ABS_X */,
    /* 1 */ 1 /* ABS_Y */,
    /* 2 */ 2 /* ABS_Z */,
    /* 3 */ 3 /* ABS_RX */,
    /* 4 */ 4 /* ABS_RY */,
    /* 5 */ 5 /* ABS_RZ */,
    /* 6 */ 16 /* ABS_HAT0X */,
    /* 7 */ 17 /* ABS_HAT0Y */
};

static int axis_to_index(int evdev_code) {
    for (int i = 0; i < GAMEPAD_MAX_AXES; i++) {
        if (g_tracked_axes[i] == evdev_code)
            return i;
    }
    return -1;
}

/* ── Load axis calibration data from EVIOCGABS ──────────────────────────── */
static void load_axis_calibration(GamepadManager *gm) {
    if (gm->gamepad_fd < 0) return;

    /* Axes we care about: ABS_X(0), ABS_Y(1), ABS_Z(2), ABS_RX(3), ABS_RY(4), ABS_RZ(5), ABS_HAT0X(16), ABS_HAT0Y(17) */
    static const int axes[] = { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y };

    for (int i = 0; i < GAMEPAD_MAX_AXES; i++) {
        struct input_absinfo info;
        if (ioctl(gm->gamepad_fd, EVIOCGABS(axes[i]), &info) == 0) {
            gm->axis_min[i]  = info.minimum;
            gm->axis_max[i]  = info.maximum;
            gm->axis_flat[i] = info.flat;

            /* Auto-calibrate center from initial reported value */
            gm->axis_calib[i].center = info.value;
        } else {
            gm->axis_min[i]  = 0;
            gm->axis_max[i]  = 0;
            gm->axis_flat[i] = 0;
            gm->axis_calib[i].center = 0;
        }
        /* Apply global dead zone default unless already configured */
        if (gm->axis_calib[i].deadzone_pct <= 0)
            gm->axis_calib[i].deadzone_pct = gm->deadzone_pct;
    }
}

/* ── Normalize axis value to -1000..+1000 with configurable dead zone ──── */
static int normalize_axis_calibrated(int value, int min_val, int max_val,
                                     int center, int deadzone_pct) {
    if (max_val == min_val) return 0;

    int half_range = (max_val - min_val) / 2;
    if (half_range == 0) return 0;

    /* Use calibrated center instead of simple midpoint */
    int offset = value - center;

    /* Compute dead zone in raw axis units */
    int dz = (half_range * deadzone_pct) / 100;

    /* Apply dead zone */
    if (offset > -dz && offset < dz)
        return 0;

    /* Remove dead zone from effective range */
    int effective_range = half_range - dz;
    if (effective_range <= 0) return 0;

    if (offset > 0)
        offset -= dz;
    else
        offset += dz;

    int normalized = (offset * GAMEPAD_AXIS_MAX) / effective_range;
    if (normalized < -GAMEPAD_AXIS_MAX) normalized = -GAMEPAD_AXIS_MAX;
    if (normalized >  GAMEPAD_AXIS_MAX) normalized =  GAMEPAD_AXIS_MAX;
    return normalized;
}

/* ── Legacy normalize (for backward compat in edge cases) ───────────────── */
static int normalize_axis(int value, int min_val, int max_val) {
    if (max_val == min_val) return 0;
    int mid = (min_val + max_val) / 2;
    int half_range = (max_val - min_val) / 2;
    if (half_range == 0) return 0;
    int normalized = ((value - mid) * GAMEPAD_AXIS_MAX) / half_range;
    if (normalized < -GAMEPAD_AXIS_MAX) normalized = -GAMEPAD_AXIS_MAX;
    if (normalized >  GAMEPAD_AXIS_MAX) normalized =  GAMEPAD_AXIS_MAX;
    return normalized;
}

/* ── Scan /dev/input/event* for gamepad, keyboard, and mouse ────────────── */
static void scan_devices(GamepadManager *gm) {
    char path[64];
    char name[128];

    for (int i = 0; i < GAMEPAD_MAX_DEVICES; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        /* Read device name */
        name[0] = '\0';
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);

        /* Filter out Panjit touchscreen (same as usb_test.c) */
        if (strstr(name, "panjit") || strstr(name, "Panjit") || strstr(name, "PANJIT")) {
            close(fd);
            continue;
        }

        GDevType type = classify_device(fd);

        if (type == GDEV_GAMEPAD && gm->gamepad_fd < 0) {
            gm->gamepad_fd = fd;
            load_axis_calibration(gm);
            printf("gamepad: found gamepad '%s' at %s\n", name, path);
        } else if (type == GDEV_KEYBOARD && gm->keyboard_fd < 0) {
            gm->keyboard_fd = fd;
            printf("gamepad: found keyboard '%s' at %s\n", name, path);
        } else if (type == GDEV_MOUSE && gm->mouse_fd < 0) {
            gm->mouse_fd = fd;
            printf("gamepad: found mouse '%s' at %s\n", name, path);
        } else {
            close(fd);
        }

        /* Stop early if all three found */
        if (gm->gamepad_fd >= 0 && gm->keyboard_fd >= 0 && gm->mouse_fd >= 0)
            break;
    }
}

/* ── Apply sensible defaults to all configurable fields ─────────────────── */
static void apply_defaults(GamepadManager *gm) {
    gm->button_map = gamepad_get_default_button_map();

    gm->mouse_screen_w = GAMEPAD_DEFAULT_SCREEN_W;
    gm->mouse_screen_h = GAMEPAD_DEFAULT_SCREEN_H;
    gm->mouse_x = gm->mouse_screen_w / 2;
    gm->mouse_y = gm->mouse_screen_h / 2;

    gm->mouse_accel.sensitivity   = GAMEPAD_MOUSE_SENSITIVITY_DEFAULT;
    gm->mouse_accel.acceleration  = GAMEPAD_MOUSE_ACCEL_DEFAULT;
    gm->mouse_accel.low_threshold = GAMEPAD_MOUSE_LOW_THRESHOLD_DEFAULT;
    gm->mouse_accel.high_threshold = GAMEPAD_MOUSE_HIGH_THRESHOLD_DEFAULT;

    gm->deadzone_pct = GAMEPAD_DEADZONE_PCT_DEFAULT;

    for (int i = 0; i < GAMEPAD_MAX_AXES; i++) {
        gm->axis_calib[i].center = 0;
        gm->axis_calib[i].deadzone_pct = gm->deadzone_pct;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration File I/O
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Trim leading and trailing whitespace in-place. Returns pointer. */
static char *cfg_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = '\0';
    return s;
}

int gamepad_load_config(GamepadManager *gp, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("gamepad: no config file at %s (using defaults)\n", path);
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *trimmed = cfg_trim(line);

        /* Skip blank lines and comments */
        if (*trimmed == '\0' || *trimmed == '#')
            continue;

        /* Find '=' separator */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = cfg_trim(trimmed);
        char *val = cfg_trim(eq + 1);

        if (*key == '\0' || *val == '\0') continue;

        /* Mouse settings */
        if (strcmp(key, "mouse_sensitivity") == 0) {
            float v = (float)atof(val);
            if (v > 0.1f && v < 20.0f) gp->mouse_accel.sensitivity = v;
        } else if (strcmp(key, "mouse_acceleration") == 0) {
            float v = (float)atof(val);
            if (v > 0.1f && v < 20.0f) gp->mouse_accel.acceleration = v;
        } else if (strcmp(key, "mouse_low_threshold") == 0) {
            int v = atoi(val);
            if (v >= 0 && v < 100) gp->mouse_accel.low_threshold = v;
        } else if (strcmp(key, "mouse_high_threshold") == 0) {
            int v = atoi(val);
            if (v >= 1 && v < 500) gp->mouse_accel.high_threshold = v;
        }
        /* Gamepad dead zone */
        else if (strcmp(key, "gamepad_deadzone") == 0) {
            int v = atoi(val);
            if (v >= 0 && v <= 100) {
                gp->deadzone_pct = v;
                for (int i = 0; i < GAMEPAD_MAX_AXES; i++)
                    gp->axis_calib[i].deadzone_pct = v;
            }
        }
        /* Gamepad button mapping */
        else if (strcmp(key, "gamepad_btn_jump") == 0)   { gp->button_map.btn_jump   = atoi(val); }
        else if (strcmp(key, "gamepad_btn_run") == 0)    { gp->button_map.btn_run    = atoi(val); }
        else if (strcmp(key, "gamepad_btn_action") == 0) { gp->button_map.btn_action = atoi(val); }
        else if (strcmp(key, "gamepad_btn_pause") == 0)  { gp->button_map.btn_pause  = atoi(val); }
        else if (strcmp(key, "gamepad_btn_back") == 0)   { gp->button_map.btn_back   = atoi(val); }
        else if (strcmp(key, "gamepad_hat_x") == 0)      { gp->button_map.hat_x_axis = atoi(val); }
        else if (strcmp(key, "gamepad_hat_y") == 0)      { gp->button_map.hat_y_axis = atoi(val); }
        else if (strcmp(key, "gamepad_stick_lx") == 0)   { gp->button_map.stick_x_axis = atoi(val); }
        else if (strcmp(key, "gamepad_stick_ly") == 0)   { gp->button_map.stick_y_axis = atoi(val); }
        else if (strcmp(key, "gamepad_stick_rx") == 0)   { gp->button_map.stick_rx_axis = atoi(val); }
        else if (strcmp(key, "gamepad_stick_ry") == 0)   { gp->button_map.stick_ry_axis = atoi(val); }
        /* Unknown keys are silently ignored */
    }

    fclose(f);
    printf("gamepad: loaded config from %s\n", path);
    return 0;
}

int gamepad_save_config(const GamepadManager *gp, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("gamepad: failed to save config");
        return -1;
    }

    fprintf(f, "# RoomWizard Input Configuration\n");
    fprintf(f, "# Auto-generated — edit carefully or use device tools\n\n");

    fprintf(f, "# Mouse settings\n");
    fprintf(f, "mouse_sensitivity=%.1f\n", gp->mouse_accel.sensitivity);
    fprintf(f, "mouse_acceleration=%.1f\n", gp->mouse_accel.acceleration);
    fprintf(f, "mouse_low_threshold=%d\n", gp->mouse_accel.low_threshold);
    fprintf(f, "mouse_high_threshold=%d\n\n", gp->mouse_accel.high_threshold);

    fprintf(f, "# Gamepad dead zone (percentage, 0-100)\n");
    fprintf(f, "gamepad_deadzone=%d\n\n", gp->deadzone_pct);

    GamepadButtonMap defaults = gamepad_get_default_button_map();

    fprintf(f, "# Gamepad button mapping (evdev codes)\n");
    fprintf(f, "# Uncomment and change to remap buttons for your controller\n");

    /* Write button mappings — comment out if still at defaults */
    if (gp->button_map.btn_jump != defaults.btn_jump)
        fprintf(f, "gamepad_btn_jump=%d\n", gp->button_map.btn_jump);
    else
        fprintf(f, "#gamepad_btn_jump=%d\n", gp->button_map.btn_jump);

    if (gp->button_map.btn_run != defaults.btn_run)
        fprintf(f, "gamepad_btn_run=%d\n", gp->button_map.btn_run);
    else
        fprintf(f, "#gamepad_btn_run=%d\n", gp->button_map.btn_run);

    if (gp->button_map.btn_action != defaults.btn_action)
        fprintf(f, "gamepad_btn_action=%d\n", gp->button_map.btn_action);
    else
        fprintf(f, "#gamepad_btn_action=%d\n", gp->button_map.btn_action);

    if (gp->button_map.btn_pause != defaults.btn_pause)
        fprintf(f, "gamepad_btn_pause=%d\n", gp->button_map.btn_pause);
    else
        fprintf(f, "#gamepad_btn_pause=%d\n", gp->button_map.btn_pause);

    if (gp->button_map.btn_back != defaults.btn_back)
        fprintf(f, "gamepad_btn_back=%d\n", gp->button_map.btn_back);
    else
        fprintf(f, "#gamepad_btn_back=%d\n", gp->button_map.btn_back);

    if (gp->button_map.hat_x_axis != defaults.hat_x_axis)
        fprintf(f, "gamepad_hat_x=%d\n", gp->button_map.hat_x_axis);
    else
        fprintf(f, "#gamepad_hat_x=%d\n", gp->button_map.hat_x_axis);

    if (gp->button_map.hat_y_axis != defaults.hat_y_axis)
        fprintf(f, "gamepad_hat_y=%d\n", gp->button_map.hat_y_axis);
    else
        fprintf(f, "#gamepad_hat_y=%d\n", gp->button_map.hat_y_axis);

    if (gp->button_map.stick_x_axis != defaults.stick_x_axis)
        fprintf(f, "gamepad_stick_lx=%d\n", gp->button_map.stick_x_axis);
    else
        fprintf(f, "#gamepad_stick_lx=%d\n", gp->button_map.stick_x_axis);

    if (gp->button_map.stick_y_axis != defaults.stick_y_axis)
        fprintf(f, "gamepad_stick_ly=%d\n", gp->button_map.stick_y_axis);
    else
        fprintf(f, "#gamepad_stick_ly=%d\n", gp->button_map.stick_y_axis);

    if (gp->button_map.stick_rx_axis != defaults.stick_rx_axis)
        fprintf(f, "gamepad_stick_rx=%d\n", gp->button_map.stick_rx_axis);
    else
        fprintf(f, "#gamepad_stick_rx=%d\n", gp->button_map.stick_rx_axis);

    if (gp->button_map.stick_ry_axis != defaults.stick_ry_axis)
        fprintf(f, "gamepad_stick_ry=%d\n", gp->button_map.stick_ry_axis);
    else
        fprintf(f, "#gamepad_stick_ry=%d\n", gp->button_map.stick_ry_axis);

    fclose(f);
    printf("gamepad: saved config to %s\n", path);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

int gamepad_init(GamepadManager *gm) {
    memset(gm, 0, sizeof(*gm));
    gm->gamepad_fd  = -1;
    gm->keyboard_fd = -1;
    gm->mouse_fd    = -1;
    gm->touch_region_count = 0;

    /* Apply sensible defaults before loading config */
    apply_defaults(gm);

    /* Attempt to load persistent config (overrides defaults for any keys present) */
    gamepad_load_config(gm, GAMEPAD_CONFIG_PATH);

    scan_devices(gm);

    printf("gamepad: init complete (gamepad=%s, keyboard=%s, mouse=%s)\n",
           gm->gamepad_fd >= 0 ? "connected" : "none",
           gm->keyboard_fd >= 0 ? "connected" : "none",
           gm->mouse_fd >= 0 ? "connected" : "none");
    return 0;
}

void gamepad_close(GamepadManager *gm) {
    if (gm->gamepad_fd >= 0) {
        close(gm->gamepad_fd);
        gm->gamepad_fd = -1;
    }
    if (gm->keyboard_fd >= 0) {
        close(gm->keyboard_fd);
        gm->keyboard_fd = -1;
    }
    if (gm->mouse_fd >= 0) {
        close(gm->mouse_fd);
        gm->mouse_fd = -1;
    }
}

void gamepad_rescan(GamepadManager *gm) {
    /* Close existing devices and re-scan */
    gamepad_close(gm);
    memset(gm->prev_held, 0, sizeof(gm->prev_held));
    gm->prev_mouse_left = false;
    gm->prev_mouse_right = false;
    gm->prev_mouse_middle = false;
    scan_devices(gm);
}

void gamepad_set_touch_regions(GamepadManager *gm, TouchRegion *regions, int count) {
    if (count > GAMEPAD_MAX_TOUCH_REGIONS)
        count = GAMEPAD_MAX_TOUCH_REGIONS;
    for (int i = 0; i < count; i++)
        gm->touch_regions[i] = regions[i];
    gm->touch_region_count = count;
}

/* ── Mouse API ──────────────────────────────────────────────────────────── */

void gamepad_set_mouse_bounds(GamepadManager *gp, int width, int height) {
    if (width > 0) gp->mouse_screen_w = width;
    if (height > 0) gp->mouse_screen_h = height;
    /* Clamp current position to new bounds */
    if (gp->mouse_x >= gp->mouse_screen_w)
        gp->mouse_x = gp->mouse_screen_w - 1;
    if (gp->mouse_y >= gp->mouse_screen_h)
        gp->mouse_y = gp->mouse_screen_h - 1;
    if (gp->mouse_x < 0) gp->mouse_x = 0;
    if (gp->mouse_y < 0) gp->mouse_y = 0;
}

void gamepad_set_mouse_position(GamepadManager *gp, int x, int y) {
    gp->mouse_x = x;
    gp->mouse_y = y;
    /* Clamp */
    if (gp->mouse_x < 0) gp->mouse_x = 0;
    if (gp->mouse_y < 0) gp->mouse_y = 0;
    if (gp->mouse_x >= gp->mouse_screen_w)
        gp->mouse_x = gp->mouse_screen_w - 1;
    if (gp->mouse_y >= gp->mouse_screen_h)
        gp->mouse_y = gp->mouse_screen_h - 1;
}

/* ── Button Mapping API ─────────────────────────────────────────────────── */

void gamepad_set_button_map(GamepadManager *gp, const GamepadButtonMap *map) {
    if (map)
        gp->button_map = *map;
    else
        gp->button_map = gamepad_get_default_button_map();
}

/* ── Read gamepad events (using configurable button map) ────────────────── */
static void poll_gamepad(GamepadManager *gm, InputState *state) {
    if (gm->gamepad_fd < 0) return;

    struct input_event ev;
    GamepadButtonMap *m = &gm->button_map;

    while (read(gm->gamepad_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {

        if (ev.type == EV_ABS) {
            int code = ev.code;

            /* Left stick X — use configurable axis code */
            if (code == m->stick_x_axis) {
                int idx = axis_to_index(code);
                if (idx >= 0) {
                    state->axis_lx = normalize_axis_calibrated(ev.value,
                        gm->axis_min[idx], gm->axis_max[idx],
                        gm->axis_calib[idx].center,
                        gm->axis_calib[idx].deadzone_pct);
                } else {
                    /* Fallback: unknown index, use basic normalize */
                    state->axis_lx = normalize_axis(ev.value, -32768, 32767);
                }
            }
            /* Left stick Y */
            else if (code == m->stick_y_axis) {
                int idx = axis_to_index(code);
                if (idx >= 0) {
                    state->axis_ly = normalize_axis_calibrated(ev.value,
                        gm->axis_min[idx], gm->axis_max[idx],
                        gm->axis_calib[idx].center,
                        gm->axis_calib[idx].deadzone_pct);
                } else {
                    state->axis_ly = normalize_axis(ev.value, -32768, 32767);
                }
            }
            /* Right stick X — accept ABS_Z or the mapped rx axis */
            else if (code == m->stick_rx_axis || code == ABS_Z) {
                int idx = axis_to_index(code);
                if (idx >= 0) {
                    state->axis_rx = normalize_axis_calibrated(ev.value,
                        gm->axis_min[idx], gm->axis_max[idx],
                        gm->axis_calib[idx].center,
                        gm->axis_calib[idx].deadzone_pct);
                } else {
                    state->axis_rx = normalize_axis(ev.value, -32768, 32767);
                }
            }
            /* Right stick Y — accept ABS_RZ or the mapped ry axis */
            else if (code == m->stick_ry_axis || code == ABS_RZ) {
                int idx = axis_to_index(code);
                if (idx >= 0) {
                    state->axis_ry = normalize_axis_calibrated(ev.value,
                        gm->axis_min[idx], gm->axis_max[idx],
                        gm->axis_calib[idx].center,
                        gm->axis_calib[idx].deadzone_pct);
                } else {
                    state->axis_ry = normalize_axis(ev.value, -32768, 32767);
                }
            }
            /* D-pad horizontal */
            else if (code == m->hat_x_axis) {
                state->buttons[BTN_ID_LEFT].held  = (ev.value < 0);
                state->buttons[BTN_ID_RIGHT].held = (ev.value > 0);
            }
            /* D-pad vertical */
            else if (code == m->hat_y_axis) {
                state->buttons[BTN_ID_UP].held   = (ev.value < 0);
                state->buttons[BTN_ID_DOWN].held = (ev.value > 0);
            }
        } else if (ev.type == EV_KEY) {
            bool down = (ev.value != 0);

            if ((int)ev.code == m->btn_jump)
                state->buttons[BTN_ID_JUMP].held = down;
            else if ((int)ev.code == m->btn_run)
                state->buttons[BTN_ID_RUN].held = down;
            else if ((int)ev.code == m->btn_action)
                state->buttons[BTN_ID_ACTION].held = down;
            else if ((int)ev.code == m->btn_pause)
                state->buttons[BTN_ID_PAUSE].held = down;
            else if ((int)ev.code == m->btn_back)
                state->buttons[BTN_ID_BACK].held = down;
        }
    }

    /* Left stick → D-pad merging (use computed deadzone) */
    if (state->axis_lx < -GAMEPAD_DEADZONE)
        state->buttons[BTN_ID_LEFT].held = true;
    else if (state->axis_lx > GAMEPAD_DEADZONE)
        state->buttons[BTN_ID_RIGHT].held = true;

    if (state->axis_ly < -GAMEPAD_DEADZONE)
        state->buttons[BTN_ID_UP].held = true;
    else if (state->axis_ly > GAMEPAD_DEADZONE)
        state->buttons[BTN_ID_DOWN].held = true;
}

/* ── Read keyboard events ───────────────────────────────────────────────── */
static void poll_keyboard(GamepadManager *gm, InputState *state) {
    if (gm->keyboard_fd < 0) return;

    struct input_event ev;
    while (read(gm->keyboard_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type != EV_KEY) continue;

        bool down = (ev.value != 0); /* value 1 = press, 2 = repeat, 0 = release */

        switch (ev.code) {
            case KEY_UP:
            case KEY_W:
                state->buttons[BTN_ID_UP].held = down;
                break;
            case KEY_DOWN:
            case KEY_S:
                state->buttons[BTN_ID_DOWN].held = down;
                break;
            case KEY_LEFT:
            case KEY_A:
                state->buttons[BTN_ID_LEFT].held = down;
                break;
            case KEY_RIGHT:
            case KEY_D:
                state->buttons[BTN_ID_RIGHT].held = down;
                break;
            case KEY_SPACE:
                state->buttons[BTN_ID_JUMP].held = down;
                break;
            case KEY_LEFTSHIFT:
            case KEY_RIGHTSHIFT:
                state->buttons[BTN_ID_RUN].held = down;
                break;
            case KEY_ENTER:
                state->buttons[BTN_ID_ACTION].held = down;
                break;
            case KEY_ESC:
                state->buttons[BTN_ID_PAUSE].held = down;
                break;
            case KEY_BACKSPACE:
                state->buttons[BTN_ID_BACK].held = down;
                break;
            default:
                break;
        }
    }
}

/* ── Read mouse events with acceleration ────────────────────────────────── */
static void poll_mouse(GamepadManager *gm, InputState *state) {
    if (gm->mouse_fd < 0) return;

    struct input_event ev;
    int accum_dx = 0, accum_dy = 0;
    bool left_held = state->mouse_left_held ? true : false;
    bool right_held = state->mouse_right_held ? true : false;
    bool middle_held = state->mouse_middle_held ? true : false;

    while (read(gm->mouse_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {

        if (ev.type == EV_REL) {
            if (ev.code == REL_X)
                accum_dx += ev.value;
            else if (ev.code == REL_Y)
                accum_dy += ev.value;
        } else if (ev.type == EV_KEY) {
            bool down = (ev.value != 0);

            if (ev.code == BTN_LEFT)
                left_held = down;
            else if (ev.code == BTN_RIGHT)
                right_held = down;
            else if (ev.code == BTN_MIDDLE)
                middle_held = down;
        }
        /* EV_SYN ignored — we batch all events in the read loop */
    }

    /* Apply mouse acceleration to accumulated delta */
    if (accum_dx != 0 || accum_dy != 0) {
        float speed = sqrtf((float)(accum_dx * accum_dx + accum_dy * accum_dy));
        float multiplier;

        if (speed < (float)gm->mouse_accel.low_threshold) {
            /* Precision mode — 1:1 mapping */
            multiplier = 1.0f;
        } else if (speed < (float)gm->mouse_accel.high_threshold) {
            /* Medium speed — apply base sensitivity */
            multiplier = gm->mouse_accel.sensitivity;
        } else {
            /* Fast movement — extra acceleration */
            multiplier = gm->mouse_accel.sensitivity * gm->mouse_accel.acceleration;
        }

        int final_dx = (int)(accum_dx * multiplier);
        int final_dy = (int)(accum_dy * multiplier);

        gm->mouse_x += final_dx;
        gm->mouse_y += final_dy;

        /* Clamp to screen bounds */
        if (gm->mouse_x < 0) gm->mouse_x = 0;
        if (gm->mouse_y < 0) gm->mouse_y = 0;
        if (gm->mouse_x >= gm->mouse_screen_w)
            gm->mouse_x = gm->mouse_screen_w - 1;
        if (gm->mouse_y >= gm->mouse_screen_h)
            gm->mouse_y = gm->mouse_screen_h - 1;
    }

    /* Update mouse button state */
    state->mouse_left_held   = left_held ? 1 : 0;
    state->mouse_right_held  = right_held ? 1 : 0;
    state->mouse_middle_held = middle_held ? 1 : 0;

    /* Update cursor position in state */
    state->mouse_x = gm->mouse_x;
    state->mouse_y = gm->mouse_y;
}

/* ── Apply touch regions ────────────────────────────────────────────────── */
static void poll_touch(GamepadManager *gm, InputState *state,
                       int touch_x, int touch_y, bool touch_active) {
    if (!touch_active || gm->touch_region_count <= 0) return;

    for (int i = 0; i < gm->touch_region_count; i++) {
        TouchRegion *r = &gm->touch_regions[i];
        if (r->button < 0 || r->button >= BTN_ID_COUNT) continue;

        if (touch_x >= r->x && touch_x < r->x + r->w &&
            touch_y >= r->y && touch_y < r->y + r->h) {
            state->buttons[r->button].held = true;
        }
    }
}

/* ── Edge detection (abstract buttons) ──────────────────────────────────── */
static void compute_edges(GamepadManager *gm, InputState *state) {
    for (int i = 0; i < BTN_ID_COUNT; i++) {
        bool now  = state->buttons[i].held;
        bool prev = gm->prev_held[i];
        state->buttons[i].pressed  = (now && !prev);
        state->buttons[i].released = (!now && prev);
        gm->prev_held[i] = now;
    }
}

/* ── Edge detection (mouse buttons) ─────────────────────────────────────── */
static void compute_mouse_edges(GamepadManager *gm, InputState *state) {
    bool left_now   = state->mouse_left_held ? true : false;
    bool right_now  = state->mouse_right_held ? true : false;
    bool middle_now = state->mouse_middle_held ? true : false;

    state->mouse_left_pressed    = (left_now && !gm->prev_mouse_left) ? 1 : 0;
    state->mouse_left_released   = (!left_now && gm->prev_mouse_left) ? 1 : 0;
    state->mouse_right_pressed   = (right_now && !gm->prev_mouse_right) ? 1 : 0;
    state->mouse_right_released  = (!right_now && gm->prev_mouse_right) ? 1 : 0;
    state->mouse_middle_pressed  = (middle_now && !gm->prev_mouse_middle) ? 1 : 0;
    state->mouse_middle_released = (!middle_now && gm->prev_mouse_middle) ? 1 : 0;

    gm->prev_mouse_left   = left_now;
    gm->prev_mouse_right  = right_now;
    gm->prev_mouse_middle = middle_now;
}

void gamepad_poll(GamepadManager *gm, InputState *state,
                  int touch_x, int touch_y, bool touch_active) {
    /* Clear button held states (will be re-set by each source) */
    /* NOTE: We do NOT clear held here — gamepad/keyboard set them via events,
     * and D-pad hat events only fire on change.  Instead, we let the event
     * handlers maintain held state directly (key up/down).  Touch is additive. */

    state->gamepad_connected  = (gm->gamepad_fd >= 0);
    state->keyboard_connected = (gm->keyboard_fd >= 0);
    state->mouse_connected    = (gm->mouse_fd >= 0) ? 1 : 0;

    /* Read from each input source */
    poll_gamepad(gm, state);
    poll_keyboard(gm, state);
    poll_mouse(gm, state);
    poll_touch(gm, state, touch_x, touch_y, touch_active);

    /* Compute pressed/released edges */
    compute_edges(gm, state);
    compute_mouse_edges(gm, state);
}

/* ── Draw virtual touch controls ────────────────────────────────────────── */
void gamepad_draw_touch_controls(void *fb_ptr, InputState *state) {
    Framebuffer *fb = (Framebuffer *)fb_ptr;
    if (!fb || !state) return;

    /* Only show touch controls if no physical controllers connected */
    if (state->gamepad_connected || state->keyboard_connected) return;

    int sw = (int)fb->width;
    int sh = (int)fb->height;

    /* D-pad: bottom-left */
    int dpad_cx = 80;
    int dpad_cy = sh - 100;
    int dpad_sz = 45;
    uint32_t col_off = RGB(60, 60, 80);
    uint32_t col_on  = RGB(0, 200, 255);

    /* Up */
    fb_fill_rect(fb, dpad_cx - dpad_sz/2, dpad_cy - dpad_sz*2, dpad_sz, dpad_sz,
                 state->buttons[BTN_ID_UP].held ? col_on : col_off);
    /* Down */
    fb_fill_rect(fb, dpad_cx - dpad_sz/2, dpad_cy + dpad_sz, dpad_sz, dpad_sz,
                 state->buttons[BTN_ID_DOWN].held ? col_on : col_off);
    /* Left */
    fb_fill_rect(fb, dpad_cx - dpad_sz*2, dpad_cy - dpad_sz/2, dpad_sz, dpad_sz,
                 state->buttons[BTN_ID_LEFT].held ? col_on : col_off);
    /* Right */
    fb_fill_rect(fb, dpad_cx + dpad_sz, dpad_cy - dpad_sz/2, dpad_sz, dpad_sz,
                 state->buttons[BTN_ID_RIGHT].held ? col_on : col_off);

    /* Action buttons: bottom-right */
    int btn_cx = sw - 80;
    int btn_cy = sh - 100;
    int btn_r  = 25;

    /* Jump (A) — bottom */
    fb_fill_circle(fb, btn_cx, btn_cy + 35, btn_r,
                   state->buttons[BTN_ID_JUMP].held ? col_on : col_off);
    fb_draw_text(fb, btn_cx - 3, btn_cy + 31, "A", COLOR_WHITE, 1);

    /* Run (B) — right */
    fb_fill_circle(fb, btn_cx + 40, btn_cy, btn_r,
                   state->buttons[BTN_ID_RUN].held ? col_on : col_off);
    fb_draw_text(fb, btn_cx + 37, btn_cy - 4, "B", COLOR_WHITE, 1);

    /* Action (X) — left */
    fb_fill_circle(fb, btn_cx - 40, btn_cy, btn_r,
                   state->buttons[BTN_ID_ACTION].held ? col_on : col_off);
    fb_draw_text(fb, btn_cx - 43, btn_cy - 4, "X", COLOR_WHITE, 1);

    (void)sw; /* suppress unused warning when only dpad drawn */
}
