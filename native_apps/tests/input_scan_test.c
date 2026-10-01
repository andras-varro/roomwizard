/* Host-side regression for common/input_scan.c's pure layer: classifying an
 * evdev node from its capability bits and name, and choosing which nodes a
 * scan keeps.
 *
 * The defect this guards: a scanner that keeps only the FIRST keyboard and
 * the FIRST mouse, so a second mouse (a touchpad keyboard's pointer node plus
 * a separate receiver, say) is closed and does nothing.  input_scan() keeps a
 * node when input_select() says so, fed the running per-kind count, so the
 * selection cases below replay that loop over a list of synthetic nodes built
 * as InputCaps fixtures — the same classify-then-select composition, with no
 * ioctl.  What this cannot see: the open()/EVIOCGBIT walk itself, and the
 * rescan's skip-what-is-held path through real device nodes (only
 * input_scan_holds() is exercised).
 *
 * Build (this line is the CTEST_ROWS row in tests/run-all.sh):
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *       -o build/input_scan_test tests/input_scan_test.c common/input_scan.c -lm
 */
#include <stdio.h>
#include <string.h>
#include "input_scan.h"

static int g_pass, g_fail;

static void check(int cond, const char *what) {
    if (cond) { g_pass++; printf("  PASS  %s\n", what); }
    else      { g_fail++; printf("  FAIL  %s\n", what); }
}

/* ── Fixtures ───────────────────────────────────────────────────────────── */

static InputCaps caps_keyboard(int letters) {
    static const int keys[] = {
        KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
        KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
        KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M
    };
    InputCaps c;
    memset(&c, 0, sizeof(c));
    input_caps_set(c.ev, EV_KEY);
    for (int i = 0; i < letters && i < 26; i++)
        input_caps_set(c.key, keys[i]);
    input_caps_set(c.key, KEY_ENTER);
    return c;
}

static InputCaps caps_mouse(void) {
    InputCaps c;
    memset(&c, 0, sizeof(c));
    input_caps_set(c.ev, EV_KEY);
    input_caps_set(c.ev, EV_REL);
    input_caps_set(c.rel, REL_X);
    input_caps_set(c.rel, REL_Y);
    input_caps_set(c.rel, REL_WHEEL);
    input_caps_set(c.key, BTN_LEFT);
    input_caps_set(c.key, BTN_RIGHT);
    return c;
}

static InputCaps caps_pad(void) {
    InputCaps c;
    memset(&c, 0, sizeof(c));
    input_caps_set(c.ev, EV_KEY);
    input_caps_set(c.ev, EV_ABS);
    input_caps_set(c.abs, ABS_X);
    input_caps_set(c.abs, ABS_Y);
    input_caps_set(c.key, BTN_SOUTH);
    return c;
}

/* What the built-in panel reports: absolute X/Y and BTN_TOUCH. */
static InputCaps caps_touchscreen(void) {
    InputCaps c;
    memset(&c, 0, sizeof(c));
    input_caps_set(c.ev, EV_KEY);
    input_caps_set(c.ev, EV_ABS);
    input_caps_set(c.abs, ABS_X);
    input_caps_set(c.abs, ABS_Y);
    input_caps_set(c.key, BTN_TOUCH);
    return c;
}

typedef struct { InputCaps caps; const char *name; } FakeNode;

/* Replay input_scan()'s keep/close decision over nodes[] in order.
 * kept[] receives the index of every kept node; returns how many. */
static int replay_scan(const FakeNode *nodes, int n, const int cap[INPUT_KIND_COUNT],
                       int held[INPUT_KIND_COUNT], int *kept) {
    int k = 0;
    for (int i = 0; i < n; i++) {
        InputKind kind = input_select(input_classify(&nodes[i].caps, nodes[i].name),
                                      held, cap);
        if (kind == INPUT_KIND_NONE) continue;
        held[kind]++;
        kept[k++] = i;
    }
    return k;
}

int main(void) {
    printf("input_scan_test\n");

    /* ── Classification ─────────────────────────────────────────────── */
    InputCaps kb = caps_keyboard(26), kb19 = caps_keyboard(19), kb20 = caps_keyboard(20);
    InputCaps ms = caps_mouse(), pad = caps_pad(), ts = caps_touchscreen();
    InputCaps empty;
    memset(&empty, 0, sizeof(empty));

    check(input_classify(&kb, "USB Keyboard") == INPUT_KIND_KEYBOARD, "full keyboard is a keyboard");
    check(input_classify(&kb20, "k") == INPUT_KIND_KEYBOARD, "20 letter keys is a keyboard");
    check(input_classify(&kb19, "k") == INPUT_KIND_NONE, "19 letter keys is not a keyboard");
    check(input_classify(&ms, "USB Mouse") == INPUT_KIND_MOUSE, "REL_X+REL_Y+BTN_LEFT is a mouse");
    check(input_classify(&pad, "Xbox 360 pad") == INPUT_KIND_PAD, "ABS_X+ABS_Y+BTN_SOUTH is a pad");
    check(input_classify(&ts, "some touch panel") == INPUT_KIND_NONE,
          "ABS_X+ABS_Y+BTN_TOUCH (touchscreen caps) is nothing");
    check(input_classify(&empty, "x") == INPUT_KIND_NONE, "no capabilities is nothing");
    check(input_classify(NULL, "x") == INPUT_KIND_NONE, "NULL caps is nothing");
    check(input_classify(&kb, NULL) == INPUT_KIND_KEYBOARD, "NULL name does not crash");

    /* Pad precedence over mouse, as in gamepad.c: a node with both. */
    InputCaps both = caps_pad();
    input_caps_set(both.ev, EV_REL);
    input_caps_set(both.rel, REL_X);
    input_caps_set(both.rel, REL_Y);
    input_caps_set(both.key, BTN_LEFT);
    check(input_classify(&both, "combo") == INPUT_KIND_PAD, "pad rule is tested before mouse rule");

    /* A Bluetooth keyboard with a touchpad is ONE node with both sets of bits:
     * it stays a MOUSE (one kind per node), and carries a keyboard besides. */
    InputCaps kbms = caps_keyboard(26);
    input_caps_set(kbms.ev, EV_REL);
    input_caps_set(kbms.rel, REL_X);
    input_caps_set(kbms.rel, REL_Y);
    input_caps_set(kbms.key, BTN_LEFT);
    check(input_classify(&kbms, "BT Keyboard 5.1") == INPUT_KIND_MOUSE, "keyboard+touchpad node is a mouse");
    check(input_caps_is_keyboard(&kbms), "keyboard+touchpad node also carries a keyboard");
    check(input_caps_is_keyboard(&kb), "a plain keyboard carries a keyboard");
    check(!input_caps_is_keyboard(&ms), "a plain mouse carries no keyboard");
    check(!input_caps_is_keyboard(&kb19), "19 letter keys carry no keyboard");
    check(!input_caps_is_keyboard(&pad), "a pad carries no keyboard");
    check(!input_caps_is_keyboard(NULL), "NULL caps carry no keyboard");

    /* Touchscreen by name, whatever its bits say. */
    check(input_classify(&kb, "Panjit TouchScreen") == INPUT_KIND_NONE, "\"Panjit\" name is excluded");
    check(input_classify(&ms, "panjit ts") == INPUT_KIND_NONE, "\"panjit\" name is excluded");
    check(input_classify(&pad, "PANJIT") == INPUT_KIND_NONE, "\"PANJIT\" name is excluded");
    check(input_name_is_touchscreen("Logitech USB Receiver") == false, "ordinary name is not the touchscreen");

    /* ── Selection over a scan ──────────────────────────────────────── */
    const int cap_vnc[INPUT_KIND_COUNT] = { [INPUT_KIND_KEYBOARD] = 4, [INPUT_KIND_MOUSE] = 4 };
    int kept[16];

    {   /* touchscreen, two mice, a keyboard, a pad: the unit-188 shape */
        FakeNode nodes[] = {
            { caps_touchscreen(), "Panjit TouchScreen" },
            { caps_mouse(),       "Logitech USB Receiver Mouse" },
            { caps_keyboard(26),  "Logitech USB Receiver" },
            { caps_mouse(),       "Keyboard With Touchpad Mouse" },
            { caps_pad(),         "Xbox 360 pad" },
        };
        int held[INPUT_KIND_COUNT] = {0};
        int k = replay_scan(nodes, 5, cap_vnc, held, kept);
        check(held[INPUT_KIND_MOUSE] == 2, "two mice: BOTH are kept");
        check(held[INPUT_KIND_KEYBOARD] == 1, "the keyboard is kept");
        check(held[INPUT_KIND_PAD] == 0, "pad cap 0: pad is not kept");
        check(k == 3 && kept[0] == 1 && kept[1] == 2 && kept[2] == 3,
              "kept nodes are exactly 1,2,3 in node order (touchscreen 0 never)");
    }
    {   /* two keyboards */
        FakeNode nodes[] = { { caps_keyboard(26), "a" }, { caps_keyboard(26), "b" } };
        int held[INPUT_KIND_COUNT] = {0};
        replay_scan(nodes, 2, cap_vnc, held, kept);
        check(held[INPUT_KIND_KEYBOARD] == 2, "two keyboards: BOTH are kept");
    }
    {   /* six mice against a cap of 4 */
        FakeNode nodes[6];
        for (int i = 0; i < 6; i++) { nodes[i].caps = caps_mouse(); nodes[i].name = "m"; }
        int held[INPUT_KIND_COUNT] = {0};
        int k = replay_scan(nodes, 6, cap_vnc, held, kept);
        check(k == 4 && kept[3] == 3, "six mice, cap 4: the first four are kept");
    }
    {   /* rescan: four mice already held, so a fifth is refused */
        FakeNode nodes[] = { { caps_mouse(), "late" }, { caps_keyboard(26), "k" } };
        int held[INPUT_KIND_COUNT] = { [INPUT_KIND_MOUSE] = 4 };
        int k = replay_scan(nodes, 2, cap_vnc, held, kept);
        check(k == 1 && kept[0] == 1, "rescan: held mice count against the cap");
    }
    {   /* pad cap 1 (gamepad.c's one-pad rule) */
        const int cap_pad1[INPUT_KIND_COUNT] = { [INPUT_KIND_PAD] = 1 };
        FakeNode nodes[] = { { caps_pad(), "p1" }, { caps_pad(), "p2" }, { caps_mouse(), "m" } };
        int held[INPUT_KIND_COUNT] = {0};
        int k = replay_scan(nodes, 3, cap_pad1, held, kept);
        check(k == 1 && kept[0] == 0, "pad cap 1: first pad only; unwanted mouse refused");
    }
    check(input_select(INPUT_KIND_NONE, (int[INPUT_KIND_COUNT]){0}, cap_vnc) == INPUT_KIND_NONE,
          "NONE is never selected");
    check(input_select(INPUT_KIND_COUNT, (int[INPUT_KIND_COUNT]){0}, cap_vnc) == INPUT_KIND_NONE,
          "out-of-range kind is never selected");

    /* ── Rescan bookkeeping ─────────────────────────────────────────── */
    InputNode held_nodes[3];
    memset(held_nodes, 0, sizeof(held_nodes));
    const char *paths[3] = { "/dev/input/event2", "/dev/input/event5", "/dev/input/event7" };
    for (int i = 0; i < 3; i++) {
        snprintf(held_nodes[i].path, sizeof(held_nodes[i].path), "%s", paths[i]);
        held_nodes[i].fd = -1;   /* no real fd: drop must not close anything */
        held_nodes[i].kind = INPUT_KIND_MOUSE;
    }
    check(input_scan_holds(held_nodes, 3, "/dev/input/event5"), "held path is recognised");
    check(!input_scan_holds(held_nodes, 3, "/dev/input/event1"), "unheld path is not");
    check(!input_scan_holds(held_nodes, 3, "/dev/input/event50"), "prefix of a longer path is not a match");
    int n = input_scan_drop(held_nodes, 3, 1);
    check(n == 2 && strcmp(held_nodes[0].path, paths[0]) == 0 &&
          strcmp(held_nodes[1].path, paths[2]) == 0, "drop removes one entry, keeps order");
    check(input_scan_drop(held_nodes, n, 5) == n, "drop out of range is a no-op");

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
