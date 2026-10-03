/*
 * input_scan — evdev node discovery. See input_scan.h for the contract and
 * for where the classification rules come from.
 */
#include "input_scan.h"

#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

void input_caps_set(unsigned long *bits, int bit) {
    bits[bit / INPUT_SCAN_LONG_BITS] |= 1UL << (bit % INPUT_SCAN_LONG_BITS);
}

bool input_caps_test(const unsigned long *bits, int bit) {
    return (bits[bit / INPUT_SCAN_LONG_BITS] >> (bit % INPUT_SCAN_LONG_BITS)) & 1UL;
}

/* ── Pure layer ─────────────────────────────────────────────────────────── */

bool input_name_is_touchscreen(const char *name) {
    return name && (strstr(name, "panjit") || strstr(name, "Panjit") ||
                    strstr(name, "PANJIT"));
}

InputKind input_classify(const InputCaps *caps, const char *name) {
    if (!caps || input_name_is_touchscreen(name))
        return INPUT_KIND_NONE;

    bool has_key = input_caps_test(caps->ev, EV_KEY);
    bool has_abs = input_caps_test(caps->ev, EV_ABS);
    bool has_rel = input_caps_test(caps->ev, EV_REL);

    /* Pad: ABS_X + ABS_Y and a gamepad or joystick button. */
    if (has_abs && has_key &&
        input_caps_test(caps->abs, ABS_X) && input_caps_test(caps->abs, ABS_Y) &&
        (input_caps_test(caps->key, BTN_GAMEPAD) || input_caps_test(caps->key, BTN_SOUTH) ||
         input_caps_test(caps->key, BTN_A) || input_caps_test(caps->key, BTN_JOYSTICK)))
        return INPUT_KIND_PAD;

    /* Mouse: a relative pointing device with a left button. */
    if (has_rel && has_key &&
        input_caps_test(caps->rel, REL_X) && input_caps_test(caps->rel, REL_Y) &&
        input_caps_test(caps->key, BTN_LEFT))
        return INPUT_KIND_MOUSE;

    if (input_caps_is_keyboard(caps))
        return INPUT_KIND_KEYBOARD;

    return INPUT_KIND_NONE;
}

bool input_caps_is_keyboard(const InputCaps *caps) {
    if (!caps || !input_caps_test(caps->ev, EV_KEY))
        return false;
    static const int letter_keys[] = {
        KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
        KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
        KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M
    };
    int count = 0;
    for (int k = 0; k < (int)(sizeof(letter_keys) / sizeof(letter_keys[0])); k++)
        if (input_caps_test(caps->key, letter_keys[k])) count++;
    return count >= 20;
}

InputKind input_select(InputKind kind, const int held[INPUT_KIND_COUNT],
                       const int cap[INPUT_KIND_COUNT]) {
    if (kind <= INPUT_KIND_NONE || kind >= INPUT_KIND_COUNT)
        return INPUT_KIND_NONE;
    return (held[kind] < cap[kind]) ? kind : INPUT_KIND_NONE;
}

InputPadLayout input_pad_layout(const InputCaps *caps) {
    if (!caps)
        return INPUT_PAD_NATIVE;
    if (input_caps_test(caps->key, BTN_TL) && input_caps_test(caps->key, BTN_TR2) &&
        !input_caps_test(caps->key, BTN_SELECT) && !input_caps_test(caps->key, BTN_START))
        return INPUT_PAD_SEQUENTIAL;
    return INPUT_PAD_NATIVE;
}

int input_pad_key(InputPadLayout layout, int code) {
    /* HID Buttons 3..10 in the Xbox One S descriptor's order (see the header).
     * Buttons 1 and 2 already land on BTN_A and BTN_B. */
    static const int seq_native[] = {
        BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_THUMBL, BTN_THUMBR
    };
    if (layout == INPUT_PAD_SEQUENTIAL &&
        code >= BTN_GAMEPAD + 2 && code <= BTN_GAMEPAD + 9)
        return seq_native[code - (BTN_GAMEPAD + 2)];
    return code;
}

bool input_scan_holds(const InputNode *nodes, int n, const char *path) {
    for (int i = 0; i < n; i++)
        if (strcmp(nodes[i].path, path) == 0)
            return true;
    return false;
}

/* ── I/O layer ──────────────────────────────────────────────────────────── */

int input_read_caps(int fd, InputCaps *caps) {
    memset(caps, 0, sizeof(*caps));
    if (ioctl(fd, EVIOCGBIT(0, sizeof(caps->ev)), caps->ev) < 0)
        return -1;
    if (input_caps_test(caps->ev, EV_KEY))
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(caps->key)), caps->key);
    if (input_caps_test(caps->ev, EV_ABS))
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(caps->abs)), caps->abs);
    if (input_caps_test(caps->ev, EV_REL))
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof(caps->rel)), caps->rel);
    return 0;
}

bool input_name_excluded(const char *name, const char *const *list) {
    if (!name || !list) return false;
    for (; *list; list++)
        if (strstr(name, *list)) return true;
    return false;
}

int input_scan(InputNode *nodes, int n, int max, const int cap[INPUT_KIND_COUNT]) {
    return input_scan_with(nodes, n, max, cap, NULL);
}

/* No early exit: every node is visited, because every node of a wanted kind
 * is kept up to its cap. */
int input_scan_with(InputNode *nodes, int n, int max, const int cap[INPUT_KIND_COUNT],
                    const InputScanOpts *opts) {
    int held[INPUT_KIND_COUNT] = {0};
    for (int i = 0; i < n; i++)
        if (nodes[i].kind > INPUT_KIND_NONE && nodes[i].kind < INPUT_KIND_COUNT)
            held[nodes[i].kind]++;

    for (int i = 0; i < INPUT_SCAN_MAX_NODES && n < max; i++) {
        char path[INPUT_SCAN_PATH_LEN];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        if (input_scan_holds(nodes, n, path))
            continue;

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            if (errno != ENOENT && opts && opts->open_failed)
                opts->open_failed(path, errno, opts->ctx);
            continue;
        }

        char name[INPUT_SCAN_NAME_LEN];
        name[0] = '\0';
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        name[sizeof(name) - 1] = '\0';

        if (opts && input_name_excluded(name, opts->exclude_names)) {
            close(fd);
            continue;
        }

        InputCaps caps;
        InputKind kind = INPUT_KIND_NONE;
        if (input_read_caps(fd, &caps) == 0)
            kind = input_select(input_classify(&caps, name), held, cap);

        if (kind == INPUT_KIND_NONE) {
            close(fd);
            continue;
        }

        InputNode *nd = &nodes[n++];
        snprintf(nd->path, sizeof(nd->path), "%s", path);
        snprintf(nd->name, sizeof(nd->name), "%s", name);
        nd->fd = fd;
        nd->kind = kind;
        nd->keys = input_caps_is_keyboard(&caps);
        nd->pad_layout = (kind == INPUT_KIND_PAD) ? input_pad_layout(&caps)
                                                  : INPUT_PAD_NATIVE;
        held[kind]++;
    }
    return n;
}

int input_scan_drop(InputNode *nodes, int n, int i) {
    if (i < 0 || i >= n) return n;
    if (nodes[i].fd >= 0) close(nodes[i].fd);
    memmove(&nodes[i], &nodes[i + 1], (size_t)(n - i - 1) * sizeof(nodes[0]));
    return n - 1;
}

/* ── /etc/input_config.conf ─────────────────────────────────────────────── */

void input_config_defaults(InputConfig *cfg) {
    cfg->mouse_sensitivity    = 1.5f;
    cfg->mouse_acceleration   = 2.0f;
    cfg->mouse_low_threshold  = 3;
    cfg->mouse_high_threshold = 15;
    cfg->gamepad_deadzone     = 25;
    cfg->gamepad_btn_jump   = BTN_SOUTH;    /* 304 — A */
    cfg->gamepad_btn_run    = BTN_EAST;     /* 305 — B */
    cfg->gamepad_btn_action = BTN_WEST;     /* 308 — X */
    cfg->gamepad_btn_pause  = BTN_START;    /* 315 */
    cfg->gamepad_btn_back   = BTN_SELECT;   /* 314 */
    cfg->gamepad_btn_north  = BTN_NORTH;    /* 307 — Y */
    cfg->gamepad_btn_tl     = BTN_TL;       /* 310 */
    cfg->gamepad_btn_tr     = BTN_TR;       /* 311 */
    cfg->gamepad_hat_x      = ABS_HAT0X;    /* 16 */
    cfg->gamepad_hat_y      = ABS_HAT0Y;    /* 17 */
    cfg->gamepad_stick_lx   = ABS_X;        /* 0 */
    cfg->gamepad_stick_ly   = ABS_Y;        /* 1 */
    cfg->gamepad_stick_rx   = ABS_RX;       /* 3 */
    cfg->gamepad_stick_ry   = ABS_RY;       /* 4 */
}

static bool cfg_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Trim in place; returns the first non-space character. */
static char *cfg_trim(char *s) {
    while (cfg_space(*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && cfg_space(s[n - 1])) s[--n] = '\0';
    return s;
}

bool input_config_parse_line(InputConfig *cfg, const char *line) {
    char buf[256];
    if (!cfg || !line) return false;
    snprintf(buf, sizeof(buf), "%s", line);

    char *p = cfg_trim(buf);
    if (*p == '\0' || *p == '#') return false;
    char *eq = strchr(p, '=');
    if (!eq) return false;
    *eq = '\0';
    char *key = cfg_trim(p);
    char *val = cfg_trim(eq + 1);
    if (*key == '\0' || *val == '\0') return false;

    float *fv = strcmp(key, "mouse_sensitivity") == 0  ? &cfg->mouse_sensitivity
              : strcmp(key, "mouse_acceleration") == 0 ? &cfg->mouse_acceleration
              : NULL;
    if (fv) {
        float f = (float)atof(val);
        if (!(f > 0.1f && f < 20.0f)) return false;
        *fv = f;
        return true;
    }
    int v = atoi(val);
    if (strcmp(key, "mouse_low_threshold") == 0) {
        if (v < 0 || v >= 100) return false;
        cfg->mouse_low_threshold = v;
        return true;
    }
    if (strcmp(key, "mouse_high_threshold") == 0) {
        if (v < 1 || v >= 500) return false;
        cfg->mouse_high_threshold = v;
        return true;
    }
    if (strcmp(key, "gamepad_deadzone") == 0) {
        if (v < 0 || v > 100) return false;
        cfg->gamepad_deadzone = v;
        return true;
    }

    static const struct { const char *key; size_t off; } codes[] = {
#define CODE_KEY(f) { #f, offsetof(InputConfig, f) }
        CODE_KEY(gamepad_btn_jump),  CODE_KEY(gamepad_btn_run),
        CODE_KEY(gamepad_btn_action), CODE_KEY(gamepad_btn_pause),
        CODE_KEY(gamepad_btn_back),  CODE_KEY(gamepad_btn_north),
        CODE_KEY(gamepad_btn_tl),    CODE_KEY(gamepad_btn_tr),
        CODE_KEY(gamepad_hat_x),     CODE_KEY(gamepad_hat_y),
        CODE_KEY(gamepad_stick_lx),  CODE_KEY(gamepad_stick_ly),
        CODE_KEY(gamepad_stick_rx),  CODE_KEY(gamepad_stick_ry),
#undef CODE_KEY
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        if (strcmp(key, codes[i].key) == 0) {
            *(int *)((char *)cfg + codes[i].off) = v;
            return true;
        }
    }
    return false;   /* unknown key: ignored */
}

int input_config_load(InputConfig *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    int applied = 0;
    while (fgets(line, sizeof(line), f))
        if (input_config_parse_line(cfg, line)) applied++;
    fclose(f);
    return applied;
}

/* ── Hot-plug fingerprint ───────────────────────────────────────────────── */

unsigned long input_node_sig(void) {
    DIR *dir = opendir("/dev/input");
    if (!dir) return 0;
    unsigned long sig = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        int num;
        if (sscanf(de->d_name, "event%d", &num) != 1) continue;
        sig += ((unsigned long)(num + 1) * 2654435761UL) ^ (unsigned long)de->d_ino;
    }
    closedir(dir);
    return sig;
}

bool input_sig_gate_due(const InputSigGate *g, uint32_t now_ms) {
    return !g->have_time || (uint32_t)(now_ms - g->last_check_ms) >= INPUT_SIG_CHECK_MS;
}

bool input_sig_gate_feed(InputSigGate *g, uint32_t now_ms, unsigned long sig) {
    bool changed = g->have_sig && sig != g->sig;
    g->sig = sig;
    g->have_sig = true;
    g->last_check_ms = now_ms;
    g->have_time = true;
    return changed;
}

void input_sig_gate_baseline(InputSigGate *g, unsigned long sig) {
    g->sig = sig;
    g->have_sig = true;
}

bool input_sig_gate_poll(InputSigGate *g, uint32_t now_ms) {
    if (!input_sig_gate_due(g, now_ms)) return false;
    return input_sig_gate_feed(g, now_ms, input_node_sig());
}
