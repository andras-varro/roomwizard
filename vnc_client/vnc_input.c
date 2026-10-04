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

/* vnc_pad.c spells the RFB button bits out so it needs no libvncclient
 * header; these keep the two spellings from drifting apart. */
_Static_assert(VNC_PAD_BTN_LEFT   == rfbButton1Mask, "pad left button bit");
_Static_assert(VNC_PAD_BTN_RIGHT  == rfbButton3Mask, "pad right button bit");
_Static_assert(VNC_PAD_WHEEL_UP   == rfbButton4Mask, "pad wheel-up bit");
_Static_assert(VNC_PAD_WHEEL_DOWN == rfbButton5Mask, "pad wheel-down bit");

/* ── Load mouse and pad settings from /etc/input_config.conf ────────────── */
/* The file is parsed by input_config_load() (common/input_scan.c), the one
 * parser every component calls; the mouse fields and the pad's button and
 * axis codes are used here. */
static void load_input_config(VNCInput *input) {
    InputConfig cfg;
    input_config_defaults(&cfg);
    int applied = input_config_load(&cfg, INPUT_CONFIG_PATH);
    if (applied < 0) {
        DEBUG_PRINT("Input config not found: %s (using defaults)", INPUT_CONFIG_PATH);
    } else {
        DEBUG_PRINT("Loaded input config from %s (%d settings)", INPUT_CONFIG_PATH, applied);
    }
    input->mouse_sensitivity    = cfg.mouse_sensitivity;
    input->mouse_acceleration   = cfg.mouse_acceleration;
    input->mouse_low_threshold  = cfg.mouse_low_threshold;
    input->mouse_high_threshold = cfg.mouse_high_threshold;
    vnc_pad_map_from_config(&input->pad_map, &cfg);
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

/* ── OR of the button levels of every mouse and pad node ────────────────── */
static int usb_mouse_buttons(const VNCInput *input) {
    int mask = 0;
    for (int i = 0; i < input->usb_node_count; i++) {
        if (input->usb_nodes[i].kind == INPUT_KIND_MOUSE)
            mask |= input->usb_node_buttons[i];
        else if (input->usb_nodes[i].kind == INPUT_KIND_PAD)
            mask |= input->usb_node_pad[i].buttons;
    }
    return mask;
}

/* Read one axis's range and current value; an axis the pad lacks reads as
 * an empty range, which vnc_pad_axis() treats as centred. */
static VncPadRange pad_axis_info(int fd, int axis, int *value) {
    struct input_absinfo ai;
    VncPadRange r = { 0, 0 };
    *value = 0;
    if (axis >= 0 && axis <= ABS_MAX && ioctl(fd, EVIOCGABS(axis), &ai) == 0) {
        r.min = ai.minimum;
        r.max = ai.maximum;
        *value = ai.value;
    }
    return r;
}

/* A freshly opened pad node: its stick ranges, where the stick and d-pad are
 * now, and which pointer buttons are already held (a button held across a
 * rescan produces no press event).  Select is deliberately NOT seeded: a
 * session reconnecting while it is still held from the hold that opened
 * Settings would otherwise open Settings again. */
static void seed_pad(VNCInput *input, int i, uint32_t now) {
    const InputNode *nd = &input->usb_nodes[i];
    const VncPadMap *m = &input->pad_map;
    VncPad *p = &input->usb_node_pad[i];
    int x, y, hx, hy;
    VncPadRange rx = pad_axis_info(nd->fd, m->stick_x, &x);
    VncPadRange ry = pad_axis_info(nd->fd, m->stick_y, &y);
    vnc_pad_reset(p, rx, ry, x, y);
    pad_axis_info(nd->fd, m->hat_x, &hx);
    pad_axis_info(nd->fd, m->hat_y, &hy);
    vnc_pad_event(p, m, EV_ABS, m->hat_x, hx, now);
    vnc_pad_event(p, m, EV_ABS, m->hat_y, hy, now);

    unsigned long keys[INPUT_SCAN_NLONGS(KEY_MAX + 1)];
    memset(keys, 0, sizeof(keys));
    if (ioctl(nd->fd, EVIOCGKEY(sizeof(keys)), keys) < 0) return;
    /* Look each level up by its RAW code, then translate (input_scan.h). */
    for (int raw = BTN_MISC; raw <= BTN_GEAR_UP; raw++) {
        if (!input_caps_test(keys, raw)) continue;
        int code = input_pad_key(nd->pad_layout, raw);
        if (code == m->left || code == m->right)
            vnc_pad_event(p, m, EV_KEY, code, 1, now);
    }
}

/* Current button level of a freshly opened mouse node: a button held across
 * a rescan produces no press event, so ask the kernel. */
static int seed_mouse_buttons(int fd) {
    unsigned long keys[INPUT_SCAN_NLONGS(KEY_MAX + 1)];
    memset(keys, 0, sizeof(keys));
    if (ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) < 0) return 0;
    return (input_caps_test(keys, BTN_LEFT)   ? rfbButton1Mask : 0) |
           (input_caps_test(keys, BTN_MIDDLE) ? rfbButton2Mask : 0) |
           (input_caps_test(keys, BTN_RIGHT)  ? rfbButton3Mask : 0);
}

/* Close entry i and keep usb_node_buttons[] and usb_node_pad[] index-aligned
 * with usb_nodes[]. */
static void drop_usb_node(VNCInput *input, int i) {
    int n = input->usb_node_count;
    memmove(&input->usb_node_buttons[i], &input->usb_node_buttons[i + 1],
            (size_t)(n - i - 1) * sizeof(input->usb_node_buttons[0]));
    memmove(&input->usb_node_pad[i], &input->usb_node_pad[i + 1],
            (size_t)(n - i - 1) * sizeof(input->usb_node_pad[0]));
    input->usb_node_count = input_scan_drop(input->usb_nodes, n, i);
}

/* ── Scan /dev/input/event* for USB keyboards, mice and game pads ───────── */
/* Every keyboard, mouse and pad is opened, up to VNC_MAX_PER_KIND each;
 * nodes already held are left alone, so this is also the hotplug rescan.
 * input_classify() gives each node exactly one kind, so a pad is never also
 * read as a keyboard or a mouse. */
void vnc_input_scan_devices(VNCInput *input) {
    static const int cap[INPUT_KIND_COUNT] = {
        [INPUT_KIND_KEYBOARD] = VNC_MAX_PER_KIND,
        [INPUT_KIND_MOUSE]    = VNC_MAX_PER_KIND,
        [INPUT_KIND_PAD]      = VNC_MAX_PER_KIND,
    };
    static const char *const kind_name[INPUT_KIND_COUNT] = {
        [INPUT_KIND_NONE] = "?", [INPUT_KIND_KEYBOARD] = "keyboard",
        [INPUT_KIND_MOUSE] = "mouse", [INPUT_KIND_PAD] = "game pad",
    };
    uint32_t now = get_ticks_ms();
    int before = input->usb_node_count;

    /* Fingerprint first, so a node that appears during the walk differs
     * from this baseline and the next check catches it. */
    input_sig_gate_baseline(&input->node_gate, input_node_sig());

    input->usb_node_count = input_scan(input->usb_nodes, before,
                                       VNC_MAX_USB_NODES, cap);

    for (int i = before; i < input->usb_node_count; i++) {
        const InputNode *nd = &input->usb_nodes[i];
        input->usb_node_buttons[i] =
            (nd->kind == INPUT_KIND_MOUSE) ? seed_mouse_buttons(nd->fd) : 0;
        memset(&input->usb_node_pad[i], 0, sizeof(input->usb_node_pad[i]));
        if (nd->kind == INPUT_KIND_PAD)
            seed_pad(input, i, now);
        DEBUG_PRINT("USB %s found: '%s' at %s",
                    kind_name[nd->kind], nd->name, nd->path);
    }
    (void)kind_name;    /* DEBUG_PRINT may compile to nothing */
    input->mouse_button_mask = usb_mouse_buttons(input);
}

/* ── Close USB devices ──────────────────────────────────────────────────── */
void vnc_input_close_usb_devices(VNCInput *input) {
    while (input->usb_node_count > 0)
        drop_usb_node(input, input->usb_node_count - 1);
    input->mouse_button_mask = 0;
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

    /* USB nodes: none held yet (the memset above) */
    input->usb_node_count = 0;

    /* Mouse state */
    input->mouse_abs_x = 0;
    input->mouse_abs_y = 0;
    input->mouse_button_mask = 0;
    input->remote_width = 0;
    input->remote_height = 0;

    /* Mouse acceleration: the shared defaults, then /etc/input_config.conf */
    load_input_config(input);

    /* Scan for USB keyboard and mouse */
    vnc_input_scan_devices(input);
    
    DEBUG_PRINT("Input handler initialized (%d USB input node(s))",
                input->usb_node_count);
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

/* Forward one EV_KEY as a VNC key event, if it is a key with a keysym. */
static void forward_usb_key(VNCInput *input, const struct input_event *ev) {
    if (ev->code < 256) {
        uint32_t keysym = evdev_to_keysym[ev->code];
        if (keysym != 0) {
            /* ev.value: 1=press, 2=repeat, 0=release
             * Forward repeats as key-down for VNC text entry */
            vnc_input_send_key(input, keysym, ev->value != 0);
        }
    }
}

/* ── Poll one USB keyboard node and forward as VNC key events ───────────── */
/* Returns false if the node has gone away (read fails with ENODEV). */
static bool poll_usb_keyboard(VNCInput *input, int fd) {
    struct input_event ev;
    ssize_t r;

    errno = 0;
    while ((r = read(fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev))
        if (ev.type == EV_KEY)
            forward_usb_key(input, &ev);
    return !(r < 0 && errno == ENODEV);
}

/* Move the one pointer (mice and pads share it), clamped to the desktop. */
static void move_pointer(VNCInput *input, int dx, int dy) {
    input->mouse_abs_x += dx;
    input->mouse_abs_y += dy;
    if (input->mouse_abs_x < 0) input->mouse_abs_x = 0;
    if (input->mouse_abs_y < 0) input->mouse_abs_y = 0;
    if (input->mouse_abs_x >= input->remote_width)
        input->mouse_abs_x = input->remote_width - 1;
    if (input->mouse_abs_y >= input->remote_height)
        input->mouse_abs_y = input->remote_height - 1;
}

/* ── Release every pointer button the server holds down ─────────────────── */
/* Before leaving the session for Settings, and when a touch drag enters the
 * exit zone: a press with no matching release leaves the remote button stuck
 * down, mid-drag, for whatever the server's next client is. */
static void release_remote_buttons(VNCInput *input) {
    if (input->button_mask) {
        input->button_mask = 0;
        vnc_input_send_pointer(input, input->last_x, input->last_y, 0);
    }
    if (input->mouse_button_mask) {
        input->mouse_button_mask = 0;
        vnc_input_send_pointer(input, input->mouse_abs_x, input->mouse_abs_y, 0);
    }
}

/* ── Poll one game pad node ─────────────────────────────────────────────── */
/* Events update the node's VncPad; motion and the button mask are sent once
 * per loop by pad_step(), because a held stick produces no events.  Key codes
 * are translated to native before vnc_pad_event() sees them, so the map read
 * from /etc/input_config.conf applies to a SEQUENTIAL pad too.  Returns false
 * if the node has gone away. */
static bool poll_usb_pad(VNCInput *input, int idx, uint32_t now) {
    const InputNode *nd = &input->usb_nodes[idx];
    VncPad *p = &input->usb_node_pad[idx];
    struct input_event ev;
    ssize_t r;

    errno = 0;
    while ((r = read(nd->fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        int code = ev.code;
        if (ev.type == EV_KEY)
            code = input_pad_key(nd->pad_layout, code);
        else if (ev.type != EV_ABS)
            continue;
        int wheel = vnc_pad_event(p, &input->pad_map, ev.type, code, ev.value, now);
        if (wheel) {
            /* One wheel click: press+release, as the mouse wheel does */
            vnc_input_send_pointer(input, input->mouse_abs_x, input->mouse_abs_y,
                                   input->mouse_button_mask | wheel);
            vnc_input_send_pointer(input, input->mouse_abs_x, input->mouse_abs_y,
                                   input->mouse_button_mask);
        }
    }
    return !(r < 0 && errno == ENODEV);
}

/* ── Once per loop: pad motion, pad buttons, Select's hold ──────────────── */
static void pad_step(VNCInput *input, uint32_t now) {
    int sum_dx = 0, sum_dy = 0, best = 0;
    bool any_pad = false, exit = false;
    int speed = vnc_pad_speed_for_width(input->remote_width);

    for (int i = 0; i < input->usb_node_count; i++) {
        if (input->usb_nodes[i].kind != INPUT_KIND_PAD) continue;
        VncPad *p = &input->usb_node_pad[i];
        int dx, dy, pm;
        any_pad = true;
        vnc_pad_motion(p, &input->pad_map, now, speed, &dx, &dy);
        sum_dx += dx;
        sum_dy += dy;
        if (vnc_pad_back_exit(p, now, &pm)) exit = true;
        if (pm > best) best = pm;
    }
    input->pad_exit_progress = (float)best / 1000.0f;
    if (!any_pad) return;

    int old_mask = input->mouse_button_mask;
    input->mouse_button_mask = usb_mouse_buttons(input);
    if (sum_dx != 0 || sum_dy != 0)
        move_pointer(input, sum_dx, sum_dy);
    if (sum_dx != 0 || sum_dy != 0 || old_mask != input->mouse_button_mask)
        vnc_input_send_pointer(input, input->mouse_abs_x, input->mouse_abs_y,
                               input->mouse_button_mask);

    if (exit && !input->exit_requested) {
        release_remote_buttons(input);
        input->exit_requested = true;
        DEBUG_PRINT("Exit: pad Select held %d ms", UI_HOLD_EXIT_MS);
    }
}

/* ── Poll one USB mouse node and forward as VNC pointer events ──────────── */
/* Every mouse moves the one pointer; the button mask sent is the OR across
 * mice (usb_node_buttons[]).  Returns false if the node has gone away. */
static bool poll_usb_mouse(VNCInput *input, int idx) {
    int fd = input->usb_nodes[idx].fd;
    struct input_event ev;
    ssize_t r;
    int dx = 0, dy = 0;
    int old_mask = input->mouse_button_mask;

    errno = 0;
    while ((r = read(fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
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
            int bit = 0;
            if (ev.code == BTN_LEFT)        bit = rfbButton1Mask;
            else if (ev.code == BTN_MIDDLE) bit = rfbButton2Mask;
            else if (ev.code == BTN_RIGHT)  bit = rfbButton3Mask;

            if (bit) {
                if (ev.value != 0) input->usb_node_buttons[idx] |= bit;
                else               input->usb_node_buttons[idx] &= ~bit;
                input->mouse_button_mask = usb_mouse_buttons(input);
            } else {
                forward_usb_key(input, &ev);   /* a keyboard+touchpad combo node */
            }
        }
    }
    bool alive = !(r < 0 && errno == ENODEV);

    /* Apply acceleration to accumulated movement */
    if (dx != 0 || dy != 0) {
        float speed = sqrtf((float)(dx * dx + dy * dy));
        float multiplier = 1.0f;

        if (speed >= (float)input->mouse_high_threshold)
            multiplier = input->mouse_sensitivity * input->mouse_acceleration;
        else if (speed >= (float)input->mouse_low_threshold)
            multiplier = input->mouse_sensitivity;

        move_pointer(input, (int)(dx * multiplier), (int)(dy * multiplier));
    }

    /* Send pointer event if anything changed */
    if (dx != 0 || dy != 0 || old_mask != input->mouse_button_mask) {
        vnc_input_send_pointer(input, input->mouse_abs_x, input->mouse_abs_y,
                               input->mouse_button_mask);
    }
    return alive;
}

/* ── Poll every USB keyboard, mouse and pad node ────────────────────────── */
static void poll_usb_devices(VNCInput *input) {
    bool have_remote = input->remote_width > 0 && input->remote_height > 0;
    uint32_t now = get_ticks_ms();

    for (int i = 0; i < input->usb_node_count; ) {
        const InputNode *nd = &input->usb_nodes[i];
        bool alive = true;

        if (nd->kind == INPUT_KIND_KEYBOARD)
            alive = poll_usb_keyboard(input, nd->fd);
        else if (nd->kind == INPUT_KIND_MOUSE && have_remote)
            alive = poll_usb_mouse(input, i);
        else if (nd->kind == INPUT_KIND_PAD && have_remote)
            alive = poll_usb_pad(input, i, now);

        if (alive) { i++; continue; }

        DEBUG_PRINT("USB %s disconnected: %s",
                    nd->kind == INPUT_KIND_MOUSE ? "mouse" :
                    nd->kind == INPUT_KIND_PAD   ? "game pad" : "keyboard", nd->path);
        bool was_mouse = (nd->kind == INPUT_KIND_MOUSE ||
                          nd->kind == INPUT_KIND_PAD);
        drop_usb_node(input, i);

        /* A button held on the unplugged mouse or pad is released by its
         * leaving. */
        if (was_mouse) {
            int old_mask = input->mouse_button_mask;
            input->mouse_button_mask = usb_mouse_buttons(input);
            if (old_mask != input->mouse_button_mask && have_remote)
                vnc_input_send_pointer(input, input->mouse_abs_x, input->mouse_abs_y,
                                       input->mouse_button_mask);
        }
    }
}

/* ── Process all input sources ──────────────────────────────────────────── */
void vnc_input_process(VNCInput *input) {
    if (!input || !input->touch || !input->renderer) return;

    /* ── Hot-plug rescan, gated on the /dev/input fingerprint ────────── */
    /* Every INPUT_SIG_CHECK_MS the directory is listed (no device opened);
     * the incremental scan runs only if it changed.  Not gated on a free
     * slot: a second keyboard or mouse can arrive while one of each is
     * already held.  Held nodes are skipped, not reopened, and a node that
     * left is dropped by its read's ENODEV in poll_usb_devices(). */
    if (input_sig_gate_poll(&input->node_gate, get_ticks_ms()))
        vnc_input_scan_devices(input);

    /* ── Poll USB keyboards, mice and pads ───────────────────────────── */
    poll_usb_devices(input);
    if (input->remote_width > 0 && input->remote_height > 0)
        pad_step(input, get_ticks_ms());
    if (input->exit_requested) return;   /* a pad's Select hold completed */

    /* ── Poll touch input (existing behavior, unchanged) ─────────────── */
    touch_poll(input->touch);
    
    TouchState state = touch_get_state(input->touch);
    
    if (state.held || state.pressed) {
        /*
         * Exit gesture: long-press in the top-left corner.
         * Check raw screen coordinates (before VNC mapping) so the
         * exit zone works even if the touch lands in the letterbox border.
         * Anchored to SCREEN_SAFE_* — the zone has to be pressable, and the
         * top-left few rows/columns are visible but unreachable on a swept
         * panel.  It follows the safe rect, NOT the content rect, so it stays
         * reachable whichever content_area the user picked.
         */
        bool in_exit_zone = (state.x < SCREEN_SAFE_LEFT + EXIT_ZONE_SIZE &&
                             state.y < SCREEN_SAFE_TOP  + EXIT_ZONE_SIZE);

        if (in_exit_zone) {
            if (!input->exit_touching) {
                /* Entering exit zone — start timer */
                input->exit_touching = true;
                gettimeofday(&input->exit_touch_start, NULL);
                DEBUG_PRINT("Exit zone: touch started");
                /* A drag that slid in here still holds the remote button. */
                release_remote_buttons(input);
            } else {
                /* Already in exit zone — check elapsed time */
                struct timeval now;
                gettimeofday(&now, NULL);
                long elapsed_ms = (now.tv_sec  - input->exit_touch_start.tv_sec) * 1000L
                                + (now.tv_usec - input->exit_touch_start.tv_usec) / 1000L;
                input->exit_progress = (float)elapsed_ms / (float)EXIT_HOLD_MS;
                if (input->exit_progress > 1.0f) input->exit_progress = 1.0f;

                if (elapsed_ms >= EXIT_HOLD_MS) {
                    release_remote_buttons(input);
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

/* Whichever exit hold is further along: the corner's or a pad's Select. */
float vnc_input_exit_progress(VNCInput *input) {
    if (!input) return 0.0f;
    return input->pad_exit_progress > input->exit_progress
         ? input->pad_exit_progress : input->exit_progress;
}

void vnc_input_cleanup(VNCInput *input) {
    if (!input) return;

    vnc_input_close_usb_devices(input);
    DEBUG_PRINT("Input handler cleanup");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Settings-screen input: keyboards and pads as focus actions
 * ═══════════════════════════════════════════════════════════════════════════ */

#define VNC_NAV_MAX_NODES  (2 * VNC_MAX_PER_KIND)

/* A freshly opened pad node: its stick ranges, and every axis and button
 * level as it is now, seeded so none of it acts — A held from the tap that
 * opened Settings, Select from the hold, a d-pad already pushed. */
static void seed_nav_pad(VncNavInput *nav, int i) {
    const InputNode *nd = &nav->nodes[i];
    const VncPadMap *m = &nav->map;
    VncNavPad *s = &nav->pad[i];
    int x, y, hx, hy;
    VncPadRange rx = pad_axis_info(nd->fd, m->stick_x, &x);
    VncPadRange ry = pad_axis_info(nd->fd, m->stick_y, &y);
    vnc_nav_pad_reset(s, rx, ry);
    vnc_nav_pad_seed(s, m, EV_ABS, m->stick_x, x);
    vnc_nav_pad_seed(s, m, EV_ABS, m->stick_y, y);
    pad_axis_info(nd->fd, m->hat_x, &hx);
    pad_axis_info(nd->fd, m->hat_y, &hy);
    vnc_nav_pad_seed(s, m, EV_ABS, m->hat_x, hx);
    vnc_nav_pad_seed(s, m, EV_ABS, m->hat_y, hy);

    unsigned long keys[INPUT_SCAN_NLONGS(KEY_MAX + 1)];
    memset(keys, 0, sizeof(keys));
    if (ioctl(nd->fd, EVIOCGKEY(sizeof(keys)), keys) < 0) return;
    for (int raw = BTN_MISC; raw <= BTN_GEAR_UP; raw++)
        if (input_caps_test(keys, raw))
            vnc_nav_pad_seed(s, m, EV_KEY, input_pad_key(nd->pad_layout, raw), 1);
}

static void nav_scan(VncNavInput *nav) {
    static const int cap[INPUT_KIND_COUNT] = {
        [INPUT_KIND_KEYBOARD] = VNC_MAX_PER_KIND,
        [INPUT_KIND_PAD]      = VNC_MAX_PER_KIND,
    };
    int before = nav->count;
    input_sig_gate_baseline(&nav->gate, input_node_sig());
    nav->count = input_scan(nav->nodes, before, VNC_NAV_MAX_NODES, cap);
    for (int i = before; i < nav->count; i++) {
        memset(&nav->pad[i], 0, sizeof(nav->pad[i]));
        if (nav->nodes[i].kind == INPUT_KIND_PAD)
            seed_nav_pad(nav, i);
        DEBUG_PRINT("Settings input: %s at %s", nav->nodes[i].name, nav->nodes[i].path);
    }
}

static void nav_drop(VncNavInput *nav, int i) {
    memmove(&nav->pad[i], &nav->pad[i + 1],
            (size_t)(nav->count - i - 1) * sizeof(nav->pad[0]));
    nav->count = input_scan_drop(nav->nodes, nav->count, i);
}

void vnc_nav_open(VncNavInput *nav) {
    InputConfig cfg;
    memset(nav, 0, sizeof(*nav));
    input_config_defaults(&cfg);
    (void)input_config_load(&cfg, INPUT_CONFIG_PATH);
    vnc_pad_map_from_config(&nav->map, &cfg);
    nav_scan(nav);
}

int vnc_nav_poll(VncNavInput *nav, VncNav *out, int max) {
    int n = 0;
    if (input_sig_gate_poll(&nav->gate, get_ticks_ms()))
        nav_scan(nav);

    for (int i = 0; i < nav->count; ) {
        const InputNode *nd = &nav->nodes[i];
        struct input_event ev;
        ssize_t r;

        errno = 0;
        while ((r = read(nd->fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
            VncNav a = VNC_NAV_NONE;
            if (nd->kind == INPUT_KIND_KEYBOARD) {
                if (ev.type == EV_KEY)
                    a = vnc_nav_key(ev.code, ev.value);
            } else if (ev.type == EV_KEY) {
                a = vnc_nav_pad_event(&nav->pad[i], &nav->map, EV_KEY,
                                      input_pad_key(nd->pad_layout, ev.code), ev.value);
            } else if (ev.type == EV_ABS) {
                a = vnc_nav_pad_event(&nav->pad[i], &nav->map, EV_ABS,
                                      ev.code, ev.value);
            }
            if (a != VNC_NAV_NONE && n < max)
                out[n++] = a;
        }
        if (r < 0 && errno == ENODEV) {
            DEBUG_PRINT("Settings input gone: %s", nd->path);
            nav_drop(nav, i);
            continue;
        }
        i++;
    }
    return n;
}

void vnc_nav_close(VncNavInput *nav) {
    while (nav->count > 0)
        nav_drop(nav, nav->count - 1);
}
