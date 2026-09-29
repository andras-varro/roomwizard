/*
 * input_scan — evdev node discovery. See input_scan.h for the contract and
 * for where the classification rules come from.
 */
#include "input_scan.h"

#include <stdio.h>
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

    /* Keyboard: at least 20 of the 26 letter keys. */
    if (has_key) {
        static const int letter_keys[] = {
            KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
            KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
            KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M
        };
        int count = 0;
        for (int k = 0; k < (int)(sizeof(letter_keys) / sizeof(letter_keys[0])); k++)
            if (input_caps_test(caps->key, letter_keys[k])) count++;
        if (count >= 20)
            return INPUT_KIND_KEYBOARD;
    }

    return INPUT_KIND_NONE;
}

InputKind input_select(InputKind kind, const int held[INPUT_KIND_COUNT],
                       const int cap[INPUT_KIND_COUNT]) {
    if (kind <= INPUT_KIND_NONE || kind >= INPUT_KIND_COUNT)
        return INPUT_KIND_NONE;
    return (held[kind] < cap[kind]) ? kind : INPUT_KIND_NONE;
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

/* No early exit: every node is visited, because every node of a wanted kind
 * is kept up to its cap. */
int input_scan(InputNode *nodes, int n, int max, const int cap[INPUT_KIND_COUNT]) {
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
        if (fd < 0) continue;

        char name[INPUT_SCAN_NAME_LEN];
        name[0] = '\0';
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        name[sizeof(name) - 1] = '\0';

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
