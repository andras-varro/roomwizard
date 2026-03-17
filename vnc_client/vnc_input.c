#include "vnc_input.h"
#include "vnc_renderer.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

/* ── Bit-test helpers (same pattern as gamepad.c) ───────────────────────── */
#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(x)        ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)          ((x) % BITS_PER_LONG)
#define BIT_LONG(x)     ((x) / BITS_PER_LONG)
#define test_bit(b, a)  ((a[BIT_LONG(b)] >> OFF(b)) & 1)

/* ── Evdev scancode → X11 keysym lookup table ───────────────────────────── */
/* Letters are lowercase — VNC server uses Shift press/release for case.    */
static const uint32_t evdev_to_keysym[256] = {
    [1]   = 0xFF1B,  /* KEY_ESC → XK_Escape */
    [2]   = 0x0031,  /* KEY_1 → XK_1 */
    [3]   = 0x0032,  /* KEY_2 → XK_2 */
    [4]   = 0x0033,  /* KEY_3 */
    [5]   = 0x0034,  /* KEY_4 */
    [6]   = 0x0035,  /* KEY_5 */
    [7]   = 0x0036,  /* KEY_6 */
    [8]   = 0x0037,  /* KEY_7 */
    [9]   = 0x0038,  /* KEY_8 */
    [10]  = 0x0039,  /* KEY_9 */
    [11]  = 0x0030,  /* KEY_0 */
    [12]  = 0x002D,  /* KEY_MINUS → XK_minus */
    [13]  = 0x003D,  /* KEY_EQUAL → XK_equal */
    [14]  = 0xFF08,  /* KEY_BACKSPACE → XK_BackSpace */
    [15]  = 0xFF09,  /* KEY_TAB → XK_Tab */
    [16]  = 0x0071,  /* KEY_Q → XK_q */
    [17]  = 0x0077,  /* KEY_W → XK_w */
    [18]  = 0x0065,  /* KEY_E → XK_e */
    [19]  = 0x0072,  /* KEY_R → XK_r */
    [20]  = 0x0074,  /* KEY_T → XK_t */
    [21]  = 0x0079,  /* KEY_Y → XK_y */
    [22]  = 0x0075,  /* KEY_U → XK_u */
    [23]  = 0x0069,  /* KEY_I → XK_i */
    [24]  = 0x006F,  /* KEY_O → XK_o */
    [25]  = 0x0070,  /* KEY_P → XK_p */
    [26]  = 0x005B,  /* KEY_LEFTBRACE → XK_bracketleft */
    [27]  = 0x005D,  /* KEY_RIGHTBRACE → XK_bracketright */
    [28]  = 0xFF0D,  /* KEY_ENTER → XK_Return */
    [29]  = 0xFFE3,  /* KEY_LEFTCTRL → XK_Control_L */
    [30]  = 0x0061,  /* KEY_A → XK_a */
    [31]  = 0x0073,  /* KEY_S → XK_s */
    [32]  = 0x0064,  /* KEY_D → XK_d */
    [33]  = 0x0066,  /* KEY_F → XK_f */
    [34]  = 0x0067,  /* KEY_G → XK_g */
    [35]  = 0x0068,  /* KEY_H → XK_h */
    [36]  = 0x006A,  /* KEY_J → XK_j */
    [37]  = 0x006B,  /* KEY_K → XK_k */
    [38]  = 0x006C,  /* KEY_L → XK_l */
    [39]  = 0x003B,  /* KEY_SEMICOLON → XK_semicolon */
    [40]  = 0x0027,  /* KEY_APOSTROPHE → XK_apostrophe */
    [41]  = 0x0060,  /* KEY_GRAVE → XK_grave */
    [42]  = 0xFFE1,  /* KEY_LEFTSHIFT → XK_Shift_L */
    [43]  = 0x005C,  /* KEY_BACKSLASH → XK_backslash */
    [44]  = 0x007A,  /* KEY_Z → XK_z */
    [45]  = 0x0078,  /* KEY_X → XK_x */
    [46]  = 0x0063,  /* KEY_C → XK_c */
    [47]  = 0x0076,  /* KEY_V → XK_v */
    [48]  = 0x0062,  /* KEY_B → XK_b */
    [49]  = 0x006E,  /* KEY_N → XK_n */
    [50]  = 0x006D,  /* KEY_M → XK_m */
    [51]  = 0x002C,  /* KEY_COMMA → XK_comma */
    [52]  = 0x002E,  /* KEY_DOT → XK_period */
    [53]  = 0x002F,  /* KEY_SLASH → XK_slash */
    [54]  = 0xFFE2,  /* KEY_RIGHTSHIFT → XK_Shift_R */
    [55]  = 0xFFAA,  /* KEY_KPASTERISK → XK_KP_Multiply */
    [56]  = 0xFFE9,  /* KEY_LEFTALT → XK_Alt_L */
    [57]  = 0x0020,  /* KEY_SPACE → XK_space */
    [58]  = 0xFFE5,  /* KEY_CAPSLOCK → XK_Caps_Lock */
    [59]  = 0xFFBE,  /* KEY_F1 → XK_F1 */
    [60]  = 0xFFBF,  /* KEY_F2 */
    [61]  = 0xFFC0,  /* KEY_F3 */
    [62]  = 0xFFC1,  /* KEY_F4 */
    [63]  = 0xFFC2,  /* KEY_F5 */
    [64]  = 0xFFC3,  /* KEY_F6 */
    [65]  = 0xFFC4,  /* KEY_F7 */
    [66]  = 0xFFC5,  /* KEY_F8 */
    [67]  = 0xFFC6,  /* KEY_F9 */
    [68]  = 0xFFC7,  /* KEY_F10 */
    [87]  = 0xFFC8,  /* KEY_F11 */
    [88]  = 0xFFC9,  /* KEY_F12 */
    [71]  = 0xFFB7,  /* KEY_KP7 → XK_KP_7 */
    [72]  = 0xFFB8,  /* KEY_KP8 */
    [73]  = 0xFFB9,  /* KEY_KP9 */
    [74]  = 0xFFAD,  /* KEY_KPMINUS → XK_KP_Subtract */
    [75]  = 0xFFB4,  /* KEY_KP4 */
    [76]  = 0xFFB5,  /* KEY_KP5 */
    [77]  = 0xFFB6,  /* KEY_KP6 */
    [78]  = 0xFFAB,  /* KEY_KPPLUS → XK_KP_Add */
    [79]  = 0xFFB1,  /* KEY_KP1 */
    [80]  = 0xFFB2,  /* KEY_KP2 */
    [81]  = 0xFFB3,  /* KEY_KP3 */
    [82]  = 0xFFB0,  /* KEY_KP0 */
    [83]  = 0xFFAE,  /* KEY_KPDOT → XK_KP_Decimal */
    [96]  = 0xFF8D,  /* KEY_KPENTER → XK_KP_Enter */
    [97]  = 0xFFE4,  /* KEY_RIGHTCTRL → XK_Control_R */
    [98]  = 0xFFAF,  /* KEY_KPSLASH → XK_KP_Divide */
    [100] = 0xFFEA,  /* KEY_RIGHTALT → XK_Alt_R */
    [102] = 0xFF50,  /* KEY_HOME → XK_Home */
    [103] = 0xFF52,  /* KEY_UP → XK_Up */
    [104] = 0xFF55,  /* KEY_PAGEUP → XK_Page_Up */
    [105] = 0xFF51,  /* KEY_LEFT → XK_Left */
    [106] = 0xFF53,  /* KEY_RIGHT → XK_Right */
    [107] = 0xFF57,  /* KEY_END → XK_End */
    [108] = 0xFF54,  /* KEY_DOWN → XK_Down */
    [109] = 0xFF56,  /* KEY_PAGEDOWN → XK_Page_Down */
    [110] = 0xFF63,  /* KEY_INSERT → XK_Insert */
    [111] = 0xFFFF,  /* KEY_DELETE → XK_Delete */
    [125] = 0xFFEB,  /* KEY_LEFTMETA → XK_Super_L */
    [126] = 0xFFEC,  /* KEY_RIGHTMETA → XK_Super_R */
};

/* ── Internal device type for scanning ──────────────────────────────────── */
typedef enum { VDEV_UNKNOWN, VDEV_KEYBOARD, VDEV_MOUSE } VDevType;

/* ── Classify an evdev device ───────────────────────────────────────────── */
static VDevType classify_device(int fd) {
    unsigned long ev[NBITS(EV_MAX)] = {0};
    unsigned long kb[NBITS(KEY_MAX)] = {0};
    unsigned long rb[NBITS(REL_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0)
        return VDEV_UNKNOWN;

    bool has_key = test_bit(EV_KEY, ev);
    bool has_rel = test_bit(EV_REL, ev);

    if (has_key)
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kb)), kb);
    if (has_rel)
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rb)), rb);

    /* Mouse: has REL_X, REL_Y, BTN_LEFT — relative pointing device */
    if (has_rel && has_key &&
        test_bit(REL_X, rb) && test_bit(REL_Y, rb) &&
        test_bit(BTN_LEFT, kb))
        return VDEV_MOUSE;

    /* Keyboard: has ≥20 letter keys */
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
            return VDEV_KEYBOARD;
    }

    return VDEV_UNKNOWN;
}

/* ── Load mouse settings from /etc/input_config.conf ────────────────────── */
static void load_input_config(VNCInput *input) {
    FILE *f = fopen(INPUT_CONFIG_FILE, "r");
    if (!f) {
        DEBUG_PRINT("Input config not found: %s (using defaults)", INPUT_CONFIG_FILE);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* Skip blank lines and comments */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#')
            continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        /* Trim key */
        char *key = p;
        char *key_end = eq - 1;
        while (key_end > key && (*key_end == ' ' || *key_end == '\t'))
            *key_end-- = '\0';
        /* Trim value */
        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;

        if (strcmp(key, "mouse_sensitivity") == 0) {
            float v = (float)atof(val);
            if (v > 0.1f && v < 20.0f) input->mouse_sensitivity = v;
        } else if (strcmp(key, "mouse_acceleration") == 0) {
            float v = (float)atof(val);
            if (v > 0.1f && v < 20.0f) input->mouse_acceleration = v;
        } else if (strcmp(key, "mouse_low_threshold") == 0) {
            int v = atoi(val);
            if (v >= 0 && v < 100) input->mouse_low_threshold = v;
        } else if (strcmp(key, "mouse_high_threshold") == 0) {
            int v = atoi(val);
            if (v >= 1 && v < 500) input->mouse_high_threshold = v;
        }
        /* Other keys silently ignored (gamepad settings, etc.) */
    }

    fclose(f);
    DEBUG_PRINT("Loaded input config from %s", INPUT_CONFIG_FILE);
}

/* ── Get current time in milliseconds ───────────────────────────────────── */
static uint32_t get_ticks_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Scan /dev/input/event* for USB keyboard and mouse ──────────────────── */
void vnc_input_scan_devices(VNCInput *input) {
    char path[64];
    char name[128];

    for (int i = 0; i < MAX_INPUT_DEVICES; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        /* Read device name */
        name[0] = '\0';
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);

        /* Filter out Panjit touchscreen */
        if (strstr(name, "panjit") || strstr(name, "Panjit") ||
            strstr(name, "PANJIT") || strstr(name, "TouchScreen")) {
            close(fd);
            continue;
        }

        VDevType type = classify_device(fd);

        if (type == VDEV_KEYBOARD && input->keyboard_fd < 0) {
            input->keyboard_fd = fd;
            DEBUG_PRINT("USB keyboard found: '%s' at %s", name, path);
        } else if (type == VDEV_MOUSE && input->mouse_fd < 0) {
            input->mouse_fd = fd;
            DEBUG_PRINT("USB mouse found: '%s' at %s", name, path);
        } else {
            close(fd);
        }

        /* Stop early if both found */
        if (input->keyboard_fd >= 0 && input->mouse_fd >= 0)
            break;
    }

    input->last_device_scan = get_ticks_ms();
}

/* ── Close USB devices ──────────────────────────────────────────────────── */
void vnc_input_close_usb_devices(VNCInput *input) {
    if (input->keyboard_fd >= 0) {
        close(input->keyboard_fd);
        input->keyboard_fd = -1;
    }
    if (input->mouse_fd >= 0) {
        close(input->mouse_fd);
        input->mouse_fd = -1;
    }
}

/* ── Set remote desktop dimensions ──────────────────────────────────────── */
void vnc_input_set_remote_size(VNCInput *input, int width, int height) {
    if (!input) return;
    input->remote_width = width;
    input->remote_height = height;
    /* Center mouse in remote desktop */
    input->mouse_abs_x = width / 2;
    input->mouse_abs_y = height / 2;
    DEBUG_PRINT("Remote size set to %dx%d, mouse at (%d,%d)",
                width, height, input->mouse_abs_x, input->mouse_abs_y);
}

/* ── Initialize input handler ───────────────────────────────────────────── */
int vnc_input_init(VNCInput *input, TouchInput *touch, VNCRenderer *renderer, rfbClient *vnc_client) {
    if (!input || !touch || !renderer || !vnc_client) {
        return -1;
    }
    
    memset(input, 0, sizeof(VNCInput));
    input->touch = touch;
    input->renderer = renderer;
    input->vnc_client = vnc_client;
    input->button_mask = 0;
    input->was_pressed = false;

    /* USB device fds: -1 = not connected */
    input->keyboard_fd = -1;
    input->mouse_fd = -1;

    /* Mouse state */
    input->mouse_abs_x = 0;
    input->mouse_abs_y = 0;
    input->mouse_button_mask = 0;
    input->remote_width = 0;
    input->remote_height = 0;

    /* Mouse acceleration defaults */
    input->mouse_sensitivity = DEFAULT_MOUSE_SENSITIVITY;
    input->mouse_acceleration = DEFAULT_MOUSE_ACCELERATION;
    input->mouse_low_threshold = DEFAULT_MOUSE_LOW_THRESHOLD;
    input->mouse_high_threshold = DEFAULT_MOUSE_HIGH_THRESHOLD;

    /* Load mouse config from /etc/input_config.conf */
    load_input_config(input);

    /* Scan for USB keyboard and mouse */
    vnc_input_scan_devices(input);
    
    DEBUG_PRINT("Input handler initialized (keyboard=%s, mouse=%s)",
                input->keyboard_fd >= 0 ? "connected" : "none",
                input->mouse_fd >= 0 ? "connected" : "none");
    return 0;
}

/* ── Send pointer event to VNC server ───────────────────────────────────── */
void vnc_input_send_pointer(VNCInput *input, int x, int y, int button_mask) {
    if (!input || !input->vnc_client) return;
    
    SendPointerEvent(input->vnc_client, x, y, button_mask);
    
    DEBUG_PRINT("Pointer event: (%d,%d) buttons=%d", x, y, button_mask);
}

/* ── Send key event to VNC server ───────────────────────────────────────── */
void vnc_input_send_key(VNCInput *input, uint32_t key, bool down) {
    if (!input || !input->vnc_client) return;
    
    SendKeyEvent(input->vnc_client, key, down ? TRUE : FALSE);
    
    DEBUG_PRINT("Key event: key=0x%04X down=%d", key, down);
}

/* ── Poll USB keyboard and forward as VNC key events ────────────────────── */
static void poll_usb_keyboard(VNCInput *input) {
    if (input->keyboard_fd < 0) return;

    struct input_event ev;
    while (read(input->keyboard_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY && ev.code < 256) {
            uint32_t keysym = evdev_to_keysym[ev.code];
            if (keysym != 0) {
                /* ev.value: 1=press, 2=repeat, 0=release
                 * Forward repeats as key-down for VNC text entry */
                int down = (ev.value != 0);
                vnc_input_send_key(input, keysym, down);
            }
        }
    }

    /* Check for device disconnect */
    if (errno == ENODEV) {
        DEBUG_PRINT("USB keyboard disconnected");
        close(input->keyboard_fd);
        input->keyboard_fd = -1;
    }
}

/* ── Poll USB mouse and forward as VNC pointer events ───────────────────── */
static void poll_usb_mouse(VNCInput *input) {
    if (input->mouse_fd < 0) return;
    if (input->remote_width <= 0 || input->remote_height <= 0) return;

    struct input_event ev;
    int dx = 0, dy = 0;
    int buttons_changed = 0;

    while (read(input->mouse_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_REL) {
            if (ev.code == REL_X) {
                dx += ev.value;
            } else if (ev.code == REL_Y) {
                dy += ev.value;
            } else if (ev.code == REL_WHEEL) {
                /* Scroll: press+release button 4 (up) or 5 (down) */
                int btn = (ev.value > 0) ? 8 : 16;  /* rfbButton4Mask=8, rfbButton5Mask=16 */
                vnc_input_send_pointer(input, input->mouse_abs_x, input->mouse_abs_y,
                                       input->mouse_button_mask | btn);
                vnc_input_send_pointer(input, input->mouse_abs_x, input->mouse_abs_y,
                                       input->mouse_button_mask);
            }
        } else if (ev.type == EV_KEY) {
            int down = (ev.value != 0);
            int old_mask = input->mouse_button_mask;

            if (ev.code == BTN_LEFT) {
                if (down) input->mouse_button_mask |= 1;   /* rfbButton1Mask */
                else      input->mouse_button_mask &= ~1;
            } else if (ev.code == BTN_MIDDLE) {
                if (down) input->mouse_button_mask |= 2;   /* rfbButton2Mask */
                else      input->mouse_button_mask &= ~2;
            } else if (ev.code == BTN_RIGHT) {
                if (down) input->mouse_button_mask |= 4;   /* rfbButton3Mask */
                else      input->mouse_button_mask &= ~4;
            }

            if (old_mask != input->mouse_button_mask)
                buttons_changed = 1;
        }
    }

    /* Apply acceleration to accumulated movement */
    if (dx != 0 || dy != 0) {
        float speed = sqrtf((float)(dx * dx + dy * dy));
        float multiplier = 1.0f;

        if (speed >= (float)input->mouse_high_threshold)
            multiplier = input->mouse_sensitivity * input->mouse_acceleration;
        else if (speed >= (float)input->mouse_low_threshold)
            multiplier = input->mouse_sensitivity;

        input->mouse_abs_x += (int)(dx * multiplier);
        input->mouse_abs_y += (int)(dy * multiplier);

        /* Clamp to remote desktop bounds */
        if (input->mouse_abs_x < 0) input->mouse_abs_x = 0;
        if (input->mouse_abs_y < 0) input->mouse_abs_y = 0;
        if (input->mouse_abs_x >= input->remote_width)
            input->mouse_abs_x = input->remote_width - 1;
        if (input->mouse_abs_y >= input->remote_height)
            input->mouse_abs_y = input->remote_height - 1;
    }

    /* Send pointer event if anything changed */
    if (dx != 0 || dy != 0 || buttons_changed) {
        vnc_input_send_pointer(input, input->mouse_abs_x, input->mouse_abs_y,
                               input->mouse_button_mask);
    }

    /* Check for device disconnect */
    if (errno == ENODEV) {
        DEBUG_PRINT("USB mouse disconnected");
        close(input->mouse_fd);
        input->mouse_fd = -1;
    }
}

/* ── Process all input sources ──────────────────────────────────────────── */
void vnc_input_process(VNCInput *input) {
    if (!input || !input->touch || !input->renderer) return;

    /* ── Periodic device rescan (every DEVICE_SCAN_INTERVAL_MS) ──────── */
    if (input->keyboard_fd < 0 || input->mouse_fd < 0) {
        uint32_t now_ms = get_ticks_ms();
        if (now_ms - input->last_device_scan >= DEVICE_SCAN_INTERVAL_MS) {
            vnc_input_scan_devices(input);
        }
    }

    /* ── Poll USB keyboard ───────────────────────────────────────────── */
    poll_usb_keyboard(input);

    /* ── Poll USB mouse ──────────────────────────────────────────────── */
    poll_usb_mouse(input);

    /* ── Poll touch input (existing behavior, unchanged) ─────────────── */
    touch_poll(input->touch);
    
    TouchState state = touch_get_state(input->touch);
    
    if (state.held || state.pressed) {
        /*
         * Exit gesture: long-press in top-left corner.
         * Check raw screen coordinates (before VNC mapping) so the
         * exit zone works even if the touch lands in the letterbox border.
         */
        bool in_exit_zone = (state.x < EXIT_ZONE_SIZE && state.y < EXIT_ZONE_SIZE);

        if (in_exit_zone) {
            if (!input->exit_touching) {
                /* Entering exit zone — start timer */
                input->exit_touching = true;
                gettimeofday(&input->exit_touch_start, NULL);
                DEBUG_PRINT("Exit zone: touch started");
            } else {
                /* Already in exit zone — check elapsed time */
                struct timeval now;
                gettimeofday(&now, NULL);
                long elapsed_ms = (now.tv_sec  - input->exit_touch_start.tv_sec) * 1000L
                                + (now.tv_usec - input->exit_touch_start.tv_usec) / 1000L;
                input->exit_progress = (float)elapsed_ms / (float)EXIT_HOLD_MS;
                if (input->exit_progress > 1.0f) input->exit_progress = 1.0f;

                if (elapsed_ms >= EXIT_HOLD_MS) {
                    input->exit_requested = true;
                    DEBUG_PRINT("Exit gesture completed (%ld ms)", elapsed_ms);
                }
            }
            /* Don't send VNC pointer events while in exit zone */
            input->was_pressed = true;
            return;
        } else {
            /* Touch moved out of exit zone — reset */
            if (input->exit_touching) {
                input->exit_touching = false;
                input->exit_progress = 0.0f;
            }
        }

        /* Convert screen coordinates to remote coordinates */
        int remote_x, remote_y;
        if (vnc_renderer_screen_to_remote(input->renderer, state.x, state.y, 
                                         &remote_x, &remote_y)) {
            if (!input->was_pressed) {
                /* New press - button down */
                input->button_mask = rfbButton1Mask;
                vnc_input_send_pointer(input, remote_x, remote_y, input->button_mask);
                
                DEBUG_PRINT("Touch down at screen (%d,%d) -> remote (%d,%d)", 
                           state.x, state.y, remote_x, remote_y);
            } else if (remote_x != input->last_x || remote_y != input->last_y) {
                /* Drag - update position with button still down */
                vnc_input_send_pointer(input, remote_x, remote_y, input->button_mask);
                
                DEBUG_PRINT("Touch drag to remote (%d,%d)", remote_x, remote_y);
            }
            
            input->last_x = remote_x;
            input->last_y = remote_y;
        }
    } else {
        if (input->was_pressed) {
            /* Touch released - button up */
            input->button_mask = 0;
            vnc_input_send_pointer(input, input->last_x, input->last_y, input->button_mask);
            
            DEBUG_PRINT("Touch up at remote (%d,%d)", input->last_x, input->last_y);
        }
        /* Reset exit zone state on release */
        if (input->exit_touching) {
            input->exit_touching = false;
            input->exit_progress = 0.0f;
        }
    }
    
    input->was_pressed = (state.held || state.pressed);
}

bool vnc_input_exit_requested(VNCInput *input) {
    return input ? input->exit_requested : false;
}

float vnc_input_exit_progress(VNCInput *input) {
    return input ? input->exit_progress : 0.0f;
}

void vnc_input_cleanup(VNCInput *input) {
    if (!input) return;
    
    vnc_input_close_usb_devices(input);
    DEBUG_PRINT("Input handler cleanup");
}
