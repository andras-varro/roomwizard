/**
 * Gamepad Input Module for RoomWizard — Implementation
 *
 * Scans evdev devices for gamepad and keyboard, reads events via
 * non-blocking reads, normalizes axes, and provides unified input state
 * with edge detection.  Touch regions map screen areas to virtual buttons.
 *
 * Reference patterns extracted from native_apps/usb_test/usb_test.c.
 */

#include "gamepad.h"
#include "framebuffer.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
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
typedef enum { GDEV_UNKNOWN, GDEV_KEYBOARD, GDEV_GAMEPAD } GDevType;

/* ── Classify an evdev device ───────────────────────────────────────────── */
static GDevType classify_device(int fd) {
    unsigned long ev[NBITS(EV_MAX)] = {0};
    unsigned long kb[NBITS(KEY_MAX)] = {0};
    unsigned long ab[NBITS(ABS_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0)
        return GDEV_UNKNOWN;

    bool has_key = test_bit(EV_KEY, ev);
    bool has_abs = test_bit(EV_ABS, ev);

    if (has_key)
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kb)), kb);
    if (has_abs)
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ab)), ab);

    /* Gamepad: has ABS_X, ABS_Y + gamepad buttons */
    if (has_abs && has_key && test_bit(ABS_X, ab) && test_bit(ABS_Y, ab) &&
        (test_bit(BTN_GAMEPAD, kb) || test_bit(BTN_SOUTH, kb) ||
         test_bit(BTN_A, kb) || test_bit(BTN_JOYSTICK, kb)))
        return GDEV_GAMEPAD;

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

/* ── Load axis calibration data from EVIOCGABS ──────────────────────────── */
static void load_axis_calibration(GamepadManager *gm) {
    if (gm->gamepad_fd < 0) return;

    /* Axes we care about: ABS_X(0), ABS_Y(1), ABS_Z(2), ABS_RX(3), ABS_RY(4), ABS_RZ(5), ABS_HAT0X(16), ABS_HAT0Y(17) */
    static const int axes[] = { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y };

    for (int i = 0; i < 8; i++) {
        struct input_absinfo info;
        if (ioctl(gm->gamepad_fd, EVIOCGABS(axes[i]), &info) == 0) {
            gm->axis_min[i]  = info.minimum;
            gm->axis_max[i]  = info.maximum;
            gm->axis_flat[i] = info.flat;
        } else {
            gm->axis_min[i]  = 0;
            gm->axis_max[i]  = 0;
            gm->axis_flat[i] = 0;
        }
    }
}

/* ── Normalize axis value to -1000..+1000 ───────────────────────────────── */
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

/* ── Scan /dev/input/event* for gamepad and keyboard ────────────────────── */
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
        } else {
            close(fd);
        }

        /* Stop early if both found */
        if (gm->gamepad_fd >= 0 && gm->keyboard_fd >= 0)
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

int gamepad_init(GamepadManager *gm) {
    memset(gm, 0, sizeof(*gm));
    gm->gamepad_fd  = -1;
    gm->keyboard_fd = -1;
    gm->touch_region_count = 0;

    scan_devices(gm);

    printf("gamepad: init complete (gamepad=%s, keyboard=%s)\n",
           gm->gamepad_fd >= 0 ? "connected" : "none",
           gm->keyboard_fd >= 0 ? "connected" : "none");
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
}

void gamepad_rescan(GamepadManager *gm) {
    /* Close existing devices and re-scan */
    gamepad_close(gm);
    memset(gm->prev_held, 0, sizeof(gm->prev_held));
    scan_devices(gm);
}

void gamepad_set_touch_regions(GamepadManager *gm, TouchRegion *regions, int count) {
    if (count > GAMEPAD_MAX_TOUCH_REGIONS)
        count = GAMEPAD_MAX_TOUCH_REGIONS;
    for (int i = 0; i < count; i++)
        gm->touch_regions[i] = regions[i];
    gm->touch_region_count = count;
}

/* ── Read gamepad events ────────────────────────────────────────────────── */
static void poll_gamepad(GamepadManager *gm, InputState *state) {
    if (gm->gamepad_fd < 0) return;

    struct input_event ev;
    while (read(gm->gamepad_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {

        if (ev.type == EV_ABS) {
            int code = ev.code;

            if (code == ABS_X) {
                state->axis_lx = normalize_axis(ev.value,
                    gm->axis_min[0], gm->axis_max[0]);
            } else if (code == ABS_Y) {
                state->axis_ly = normalize_axis(ev.value,
                    gm->axis_min[1], gm->axis_max[1]);
            } else if (code == ABS_Z || code == ABS_RX) {
                state->axis_rx = normalize_axis(ev.value,
                    gm->axis_min[code == ABS_Z ? 2 : 3],
                    gm->axis_max[code == ABS_Z ? 2 : 3]);
            } else if (code == ABS_RZ || code == ABS_RY) {
                state->axis_ry = normalize_axis(ev.value,
                    gm->axis_min[code == ABS_RZ ? 5 : 4],
                    gm->axis_max[code == ABS_RZ ? 5 : 4]);
            } else if (code == ABS_HAT0X) {
                /* D-pad horizontal */
                state->buttons[BTN_ID_LEFT].held  = (ev.value < 0);
                state->buttons[BTN_ID_RIGHT].held = (ev.value > 0);
            } else if (code == ABS_HAT0Y) {
                /* D-pad vertical */
                state->buttons[BTN_ID_UP].held   = (ev.value < 0);
                state->buttons[BTN_ID_DOWN].held = (ev.value > 0);
            }
        } else if (ev.type == EV_KEY) {
            bool down = (ev.value != 0);

            switch (ev.code) {
                case BTN_SOUTH: /* A button */
                    state->buttons[BTN_ID_JUMP].held = down;
                    break;
                case BTN_EAST: /* B button */
                    state->buttons[BTN_ID_RUN].held = down;
                    break;
                case BTN_WEST: /* X button */
                    state->buttons[BTN_ID_ACTION].held = down;
                    break;
                case BTN_START:
                    state->buttons[BTN_ID_PAUSE].held = down;
                    break;
                case BTN_SELECT:
                    state->buttons[BTN_ID_BACK].held = down;
                    break;
                default:
                    break;
            }
        }
    }

    /* Left stick → D-pad merging */
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

/* ── Edge detection ─────────────────────────────────────────────────────── */
static void compute_edges(GamepadManager *gm, InputState *state) {
    for (int i = 0; i < BTN_ID_COUNT; i++) {
        bool now  = state->buttons[i].held;
        bool prev = gm->prev_held[i];
        state->buttons[i].pressed  = (now && !prev);
        state->buttons[i].released = (!now && prev);
        gm->prev_held[i] = now;
    }
}

void gamepad_poll(GamepadManager *gm, InputState *state,
                  int touch_x, int touch_y, bool touch_active) {
    /* Clear button held states (will be re-set by each source) */
    /* NOTE: We do NOT clear held here — gamepad/keyboard set them via events,
     * and D-pad hat events only fire on change.  Instead, we let the event
     * handlers maintain held state directly (key up/down).  Touch is additive. */

    state->gamepad_connected  = (gm->gamepad_fd >= 0);
    state->keyboard_connected = (gm->keyboard_fd >= 0);

    /* Read from each input source */
    poll_gamepad(gm, state);
    poll_keyboard(gm, state);
    poll_touch(gm, state, touch_x, touch_y, touch_active);

    /* Compute pressed/released edges */
    compute_edges(gm, state);
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
