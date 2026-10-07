/* Host-side regression for vnc_client/vnc_pad.c — a game pad as the remote
 * pointer in a VNC session.
 *
 * Runs on the DEV MACHINE with native gcc; the module is pure arithmetic over
 * evdev values and a millisecond clock, so nothing here needs a device or a
 * server.  Build and run:
 *
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *       -I ../vnc_client -o build/vnc_pad_test tests/vnc_pad_test.c \
 *       ../vnc_client/vnc_pad.c common/ui_focus.c common/input_scan.c \
 *       && ./build/vnc_pad_test
 *
 * Groups: 1 the stick axis and its dead zone; 2 events into button levels,
 * wheel pulses and the d-pad; 3 motion is integrated over elapsed time, not
 * per call, carries its sub-pixel remainder and caps a stall; 4 Select's hold;
 * 5 the map is the shared parser's defaults; 6 the speed for a desktop width;
 * 7 the Settings screen's focus actions from pad and keyboard events.  8 keyboard typing into the keypads (vnc_key_char).
 * 10 a keyboard's Esc: tap goes to the remote, hold opens Settings.
 *
 * ⚠️ What it cannot see: which raw codes a real pad sends (input_pad_key()
 * and the scan are input_scan_test's), whether vnc_input.c feeds every event
 * and calls the motion step every loop, and what the server does with the
 * pointer events — those are checked on the panel in a live session.
 */
#include "vnc_pad.h"
#include <stdio.h>
#include <string.h>

static int fails, passes;

#define CHECK(cond, ...) do {                                   \
        if (cond) { passes++; }                                 \
        else { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
               printf(__VA_ARGS__); printf("\n"); }             \
    } while (0)

static VncPadMap default_map(void) {
    InputConfig cfg;
    VncPadMap m;
    input_config_defaults(&cfg);
    vnc_pad_map_from_config(&m, &cfg);
    return m;
}

static const VncPadRange XPAD = { -32768, 32767 };
static const VncPadRange HID  = { 0, 65535 };

static void group1_axis(void) {
    CHECK(vnc_pad_axis(0, XPAD, 25) == 0, "xpad centre is 0");
    CHECK(vnc_pad_axis(8000, XPAD, 25) == 0, "inside a 25%% dead zone is 0");
    CHECK(vnc_pad_axis(-8000, XPAD, 25) == 0, "inside, negative side, is 0");
    int just = vnc_pad_axis(8500, XPAD, 25);
    CHECK(just > 0 && just < 50, "just past the dead zone is small, got %d", just);
    CHECK(vnc_pad_axis(32767, XPAD, 25) == 1000, "full right is 1000, got %d",
          vnc_pad_axis(32767, XPAD, 25));
    CHECK(vnc_pad_axis(-32768, XPAD, 25) == -1000, "full left is -1000, got %d",
          vnc_pad_axis(-32768, XPAD, 25));
    CHECK(vnc_pad_axis(65535, HID, 25) == 1000 && vnc_pad_axis(0, HID, 25) == -1000,
          "an unsigned 0..65535 range spans -1000..1000");
    CHECK(vnc_pad_axis(32767, HID, 25) == 0 && vnc_pad_axis(32768, HID, 0) >= 0,
          "and rests at its midpoint, not at 0");
    int half = vnc_pad_axis(16384 + 8192, XPAD, 0);
    CHECK(half > 740 && half < 760, "no dead zone: 3/4 deflection ~750, got %d", half);
    VncPadRange empty = { 5, 5 };
    CHECK(vnc_pad_axis(5, empty, 25) == 0, "an empty range is 0");
    VncPadRange wide = { -1000000000, 1000000000 };
    CHECK(vnc_pad_axis(1000000000, wide, 0) == 1000, "a huge range does not overflow, got %d",
          vnc_pad_axis(1000000000, wide, 0));
}

static void group2_events(void) {
    VncPadMap m = default_map();
    VncPad p;
    vnc_pad_reset(&p, XPAD, XPAD, 0, 0);

    vnc_pad_event(&p, &m, EV_KEY, BTN_SOUTH, 1, 0);
    CHECK(p.buttons == VNC_PAD_BTN_LEFT, "A down is the left button, got %d", p.buttons);
    vnc_pad_event(&p, &m, EV_KEY, BTN_EAST, 1, 0);
    CHECK(p.buttons == (VNC_PAD_BTN_LEFT | VNC_PAD_BTN_RIGHT), "A+B held, got %d", p.buttons);
    vnc_pad_event(&p, &m, EV_KEY, BTN_SOUTH, 0, 0);
    CHECK(p.buttons == VNC_PAD_BTN_RIGHT, "A up leaves B held, got %d", p.buttons);
    vnc_pad_event(&p, &m, EV_KEY, BTN_SOUTH, 2, 0);
    CHECK(p.buttons == VNC_PAD_BTN_RIGHT, "a repeat (2) is not a press");
    vnc_pad_event(&p, &m, EV_KEY, BTN_EAST, 0, 0);
    CHECK(p.buttons == 0, "B up: nothing held");

    CHECK(vnc_pad_event(&p, &m, EV_KEY, BTN_TL, 1, 0) == VNC_PAD_WHEEL_UP, "LB press: wheel up");
    CHECK(vnc_pad_event(&p, &m, EV_KEY, BTN_TL, 0, 0) == 0, "LB release: no pulse");
    CHECK(vnc_pad_event(&p, &m, EV_KEY, BTN_TR, 1, 0) == VNC_PAD_WHEEL_DOWN, "RB press: wheel down");
    CHECK(vnc_pad_event(&p, &m, EV_KEY, BTN_TR, 2, 0) == 0, "RB repeat: no pulse");
    CHECK(p.buttons == 0, "shoulders are not held buttons");

    vnc_pad_event(&p, &m, EV_KEY, BTN_WEST, 1, 0);
    CHECK(p.buttons == 0 && !p.back.down, "X is unmapped");

    vnc_pad_event(&p, &m, EV_ABS, ABS_HAT0X, -1, 0);
    vnc_pad_event(&p, &m, EV_ABS, ABS_HAT0Y, 1, 0);
    CHECK(p.hat_x == -1 && p.hat_y == 1, "d-pad left+down");
    vnc_pad_event(&p, &m, EV_ABS, ABS_X, 1234, 0);
    vnc_pad_event(&p, &m, EV_ABS, ABS_Y, -99, 0);
    CHECK(p.raw_x == 1234 && p.raw_y == -99, "stick readings are stored raw");
    vnc_pad_event(&p, &m, EV_ABS, ABS_RX, 32767, 0);
    CHECK(p.raw_x == 1234, "the right stick does not move the pointer");
}

static void group3_motion(void) {
    VncPadMap m = default_map();
    VncPad a, b;
    int dx, dy, sx = 0, sy = 0;

    /* Full right stick at 1000 px/s: 100 ms is 100 px, however it is sliced. */
    vnc_pad_reset(&a, XPAD, XPAD, 32767, 0);
    vnc_pad_motion(&a, &m, 1000, 1000, &dx, &dy);
    CHECK(dx == 0 && dy == 0, "the first call only records the time");
    for (int t = 1010; t <= 1100; t += 10) {
        vnc_pad_motion(&a, &m, (uint32_t)t, 1000, &dx, &dy);
        sx += dx; sy += dy;
    }
    CHECK(sx == 100 && sy == 0, "10 x 10 ms at full right: 100 px, got %d,%d", sx, sy);

    vnc_pad_reset(&b, XPAD, XPAD, 32767, 0);
    vnc_pad_motion(&b, &m, 1000, 1000, &dx, &dy);
    vnc_pad_motion(&b, &m, 1100, 1000, &dx, &dy);
    CHECK(dx == 100, "one 100 ms step: the same 100 px, got %d", dx);

    /* Small deflection, slow speed: the carry is what moves it at all. */
    vnc_pad_reset(&a, XPAD, XPAD, 0, 0);
    vnc_pad_event(&a, &m, EV_ABS, ABS_HAT0Y, -1, 0);   /* d-pad up: 400 */
    vnc_pad_motion(&a, &m, 0, 300, &dx, &dy);
    sy = 0;
    for (int t = 5; t <= 1000; t += 5) {
        vnc_pad_motion(&a, &m, (uint32_t)t, 300, &dx, &dy);
        sy += dy;
    }
    CHECK(sy == -120, "d-pad up 1 s at 300 px/s x 0.4: -120 px in 5 ms steps, got %d", sy);

    /* A stall is capped, not integrated. */
    vnc_pad_reset(&a, XPAD, XPAD, 32767, 32767);
    vnc_pad_motion(&a, &m, 0, 1000, &dx, &dy);
    vnc_pad_motion(&a, &m, 5000, 1000, &dx, &dy);
    CHECK(dx == VNC_PAD_MAX_STEP_MS && dy == VNC_PAD_MAX_STEP_MS,
          "a 5 s gap moves only %d ms worth, got %d,%d", VNC_PAD_MAX_STEP_MS, dx, dy);

    /* Centred: nothing moves and the carry is dropped. */
    vnc_pad_reset(&a, XPAD, XPAD, 0, 0);
    vnc_pad_event(&a, &m, EV_ABS, ABS_HAT0X, 1, 0);
    vnc_pad_motion(&a, &m, 0, 300, &dx, &dy);
    vnc_pad_motion(&a, &m, 5, 300, &dx, &dy);          /* 0.6 px carried */
    vnc_pad_event(&a, &m, EV_ABS, ABS_HAT0X, 0, 5);
    vnc_pad_motion(&a, &m, 10, 300, &dx, &dy);
    CHECK(dx == 0 && a.carry_x == 0, "a release drops the carry, got %d / %d", dx, a.carry_x);
    vnc_pad_event(&a, &m, EV_ABS, ABS_HAT0X, 1, 10);
    vnc_pad_motion(&a, &m, 15, 300, &dx, &dy);
    CHECK(dx == 0, "so the next 0.6 px step does not round up to 1, got %d", dx);

    /* Inside the dead zone the stick is a rest. */
    vnc_pad_reset(&a, XPAD, XPAD, 7000, -7000);
    vnc_pad_motion(&a, &m, 0, 4000, &dx, &dy);
    vnc_pad_motion(&a, &m, 100, 4000, &dx, &dy);
    CHECK(dx == 0 && dy == 0, "a drifting stick inside the dead zone: no motion");

    /* The clock wraps. */
    vnc_pad_reset(&a, XPAD, XPAD, 32767, 0);
    vnc_pad_motion(&a, &m, 0xFFFFFFF6u, 1000, &dx, &dy);
    vnc_pad_motion(&a, &m, 40, 1000, &dx, &dy);
    CHECK(dx == 50, "50 ms across the uint32 wrap: 50 px, got %d", dx);

    /* Stick and d-pad add, and clamp at full deflection. */
    vnc_pad_reset(&a, XPAD, XPAD, 32767, 0);
    vnc_pad_event(&a, &m, EV_ABS, ABS_HAT0X, 1, 0);
    vnc_pad_motion(&a, &m, 0, 1000, &dx, &dy);
    vnc_pad_motion(&a, &m, 100, 1000, &dx, &dy);
    CHECK(dx == 100, "stick + d-pad clamp at full speed, got %d", dx);
}

static void group4_back(void) {
    VncPadMap m = default_map();
    VncPad p;
    int pm;
    vnc_pad_reset(&p, XPAD, XPAD, 0, 0);
    CHECK(!vnc_pad_back_exit(&p, 100, &pm) && pm == 0, "no hold: no exit");
    vnc_pad_event(&p, &m, EV_KEY, BTN_SELECT, 1, 1000);
    CHECK(!vnc_pad_back_exit(&p, 1000 + UI_HOLD_EXIT_MS - 1, &pm) && pm > 990,
          "1 ms short: no exit, progress %d", pm);
    vnc_pad_event(&p, &m, EV_KEY, BTN_SELECT, 2, 1500);
    CHECK(vnc_pad_back_exit(&p, 1000 + UI_HOLD_EXIT_MS, &pm) && pm == 1000,
          "held %d ms: exit (a repeat did not restart it)", UI_HOLD_EXIT_MS);
    vnc_pad_event(&p, &m, EV_KEY, BTN_SELECT, 0, 2600);
    CHECK(!vnc_pad_back_exit(&p, 9000, &pm) && pm == 0, "released: no exit");
    CHECK(p.buttons == 0, "Select is not a pointer button");
}

static void group5_map(void) {
    VncPadMap m = default_map();
    CHECK(m.left == BTN_SOUTH && m.right == BTN_EAST, "A/B");
    CHECK(m.back == BTN_SELECT, "Select");
    CHECK(m.wheel_up == BTN_TL && m.wheel_down == BTN_TR, "LB/RB");
    CHECK(m.hat_x == ABS_HAT0X && m.hat_y == ABS_HAT0Y, "d-pad axes");
    CHECK(m.stick_x == ABS_X && m.stick_y == ABS_Y, "left stick axes");
    InputConfig cfg;
    input_config_defaults(&cfg);
    CHECK(input_config_parse_line(&cfg, "gamepad_btn_jump=305"), "config line applies");
    vnc_pad_map_from_config(&m, &cfg);
    CHECK(m.left == 305, "the left button follows /etc/input_config.conf");
}

static void group6_speed(void) {
    CHECK(vnc_pad_speed_for_width(1920) == 1280, "1080p: 1280 px/s, got %d",
          vnc_pad_speed_for_width(1920));
    CHECK(vnc_pad_speed_for_width(0) == VNC_PAD_MIN_SPEED, "no desktop yet: the floor");
    CHECK(vnc_pad_speed_for_width(100000) == VNC_PAD_MAX_SPEED, "huge desktop: the ceiling");
}

static void group7_nav(void) {
    VncPadMap m = default_map();
    VncNavPad s;
    UiDir d;

    vnc_nav_pad_reset(&s, XPAD, XPAD);
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SOUTH, 1) == VNC_NAV_ACTIVATE, "A press activates");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SOUTH, 2) == VNC_NAV_NONE, "A repeat: nothing");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SOUTH, 1) == VNC_NAV_NONE,
          "a second press with no release between: nothing");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SOUTH, 0) == VNC_NAV_NONE, "A release: nothing");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SOUTH, 1) == VNC_NAV_ACTIVATE, "press again: activates");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_EAST, 1) == VNC_NAV_BACK, "B press is back");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SELECT, 1) == VNC_NAV_NONE, "Select: nothing");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SELECT, 0) == VNC_NAV_NONE, "Select up: nothing");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_WEST, 1) == VNC_NAV_NONE, "X: nothing");

    /* Held on the way in: seeded down, so neither its release nor a stray
     * press before that release acts. */
    vnc_nav_pad_reset(&s, XPAD, XPAD);
    vnc_nav_pad_seed(&s, &m, EV_KEY, BTN_SOUTH, 1);
    vnc_nav_pad_seed(&s, &m, EV_KEY, BTN_EAST, 1);
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SOUTH, 1) == VNC_NAV_NONE, "A seeded held: press ignored");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_EAST, 1) == VNC_NAV_NONE, "B seeded held: press ignored");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SOUTH, 0) == VNC_NAV_NONE, "its release: nothing");
    CHECK(vnc_nav_pad_event(&s, &m, EV_KEY, BTN_SOUTH, 1) == VNC_NAV_ACTIVATE, "then a real press acts");

    /* d-pad: one step per push into a direction. */
    vnc_nav_pad_reset(&s, XPAD, XPAD);
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_HAT0X, -1) == VNC_NAV_LEFT, "hat left");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_HAT0X, -1) == VNC_NAV_NONE, "hat left again: no step");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_HAT0X, 1) == VNC_NAV_RIGHT, "straight to right: steps");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_HAT0X, 0) == VNC_NAV_NONE, "centred: nothing");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_HAT0Y, -1) == VNC_NAV_UP, "hat up");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_HAT0Y, 0) == VNC_NAV_NONE, "up released");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_HAT0Y, 1) == VNC_NAV_DOWN, "hat down");
    vnc_nav_pad_reset(&s, XPAD, XPAD);
    vnc_nav_pad_seed(&s, &m, EV_ABS, ABS_HAT0X, 1);
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_HAT0X, 1) == VNC_NAV_NONE,
          "a d-pad held on the way in does not step");

    /* Left stick, with hysteresis. */
    vnc_nav_pad_reset(&s, XPAD, XPAD);
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_X, 8000) == VNC_NAV_NONE, "a quarter push: nothing");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_X, 30000) == VNC_NAV_RIGHT, "a full push steps right");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_X, 32767) == VNC_NAV_NONE, "held: no second step");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_X, 12000) == VNC_NAV_NONE,
          "eased back between the thresholds: still latched");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_X, 30000) == VNC_NAV_NONE,
          "so pushing again from there is no step");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_X, 0) == VNC_NAV_NONE, "at rest: unlatched");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_X, 30000) == VNC_NAV_RIGHT, "and the next push steps");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_X, -30000) == VNC_NAV_LEFT, "flicked across: steps left");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_Y, -30000) == VNC_NAV_UP, "stick up");
    CHECK(vnc_nav_pad_event(&s, &m, EV_ABS, ABS_RY, -30000) == VNC_NAV_NONE, "right stick: nothing");
    VncNavPad z;
    VncPadRange none = { 0, 0 };
    vnc_nav_pad_reset(&z, none, none);
    CHECK(vnc_nav_pad_event(&z, &m, EV_ABS, ABS_X, 30000) == VNC_NAV_NONE,
          "a pad with no stick range never steps from the stick");

    /* Keyboard. */
    CHECK(vnc_nav_key(KEY_UP, 1) == VNC_NAV_UP && vnc_nav_key(KEY_DOWN, 1) == VNC_NAV_DOWN &&
          vnc_nav_key(KEY_LEFT, 1) == VNC_NAV_LEFT && vnc_nav_key(KEY_RIGHT, 1) == VNC_NAV_RIGHT,
          "arrows move");
    CHECK(vnc_nav_key(KEY_RIGHT, 2) == VNC_NAV_RIGHT, "an arrow autorepeats");
    CHECK(vnc_nav_key(KEY_RIGHT, 0) == VNC_NAV_NONE, "an arrow release: nothing");
    CHECK(vnc_nav_key(KEY_ENTER, 1) == VNC_NAV_ACTIVATE && vnc_nav_key(KEY_KPENTER, 1) == VNC_NAV_ACTIVATE &&
          vnc_nav_key(KEY_SPACE, 1) == VNC_NAV_ACTIVATE, "Enter / keypad Enter / Space activate");
    CHECK(vnc_nav_key(KEY_ENTER, 2) == VNC_NAV_NONE, "Enter held across the open (repeats only): nothing");
    CHECK(vnc_nav_key(KEY_ESC, 1) == VNC_NAV_BACK, "Esc is back");
    CHECK(vnc_nav_key(KEY_ESC, 2) == VNC_NAV_NONE, "Esc repeat: nothing");
    CHECK(vnc_nav_key(KEY_A, 1) == VNC_NAV_NONE, "a letter: nothing");

    CHECK(vnc_nav_dir(VNC_NAV_LEFT, &d) && d == UI_DIR_LEFT, "LEFT is UI_DIR_LEFT");
    CHECK(vnc_nav_dir(VNC_NAV_DOWN, &d) && d == UI_DIR_DOWN, "DOWN is UI_DIR_DOWN");
    CHECK(!vnc_nav_dir(VNC_NAV_ACTIVATE, &d) && !vnc_nav_dir(VNC_NAV_NONE, &d), "no direction");
}

static char kc(int code, int shift, int mode, VncKeyChar *r) {
    char ch = 'Z';
    *r = vnc_key_char(code, shift, mode, &ch);
    return ch;
}

static void group8_keychar(void) {
    VncKeyChar r;
    char c;
    int i;
    static const int rowk[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
    static const int kpk[10]  = { KEY_KP0, KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP4, KEY_KP5, KEY_KP6, KEY_KP7, KEY_KP8, KEY_KP9 };

    /* digits: row and keypad, NUMERIC and FULL; never ALPHA */
    for (i = 0; i < 10; i++) {
        c = kc(rowk[i], 0, VNC_KP_NUMERIC, &r);
        CHECK(r == VNC_KC_CHAR && c == '0' + i, "NUMERIC row digit %d", i);
        c = kc(kpk[i], 0, VNC_KP_NUMERIC, &r);
        CHECK(r == VNC_KC_CHAR && c == '0' + i, "NUMERIC keypad digit %d", i);
        c = kc(kpk[i], 0, VNC_KP_FULL, &r);
        CHECK(r == VNC_KC_CHAR && c == '0' + i, "FULL keypad digit %d", i);
        c = kc(rowk[i], 0, VNC_KP_FULL, &r);
        CHECK(r == VNC_KC_CHAR && c == '0' + i, "FULL row digit %d", i);
        kc(rowk[i], 0, VNC_KP_ALPHA, &r);
        CHECK(r == VNC_KC_NONE, "ALPHA has no digit %d", i);
        kc(kpk[i], 0, VNC_KP_ALPHA, &r);
        CHECK(r == VNC_KC_NONE, "ALPHA has no keypad digit %d", i);
    }
    /* '.' and ':' */
    c = kc(KEY_DOT, 0, VNC_KP_NUMERIC, &r);
    CHECK(r == VNC_KC_CHAR && c == '.', "NUMERIC dot");
    c = kc(KEY_KPDOT, 0, VNC_KP_NUMERIC, &r);
    CHECK(r == VNC_KC_CHAR && c == '.', "NUMERIC keypad dot");
    c = kc(KEY_SEMICOLON, 1, VNC_KP_NUMERIC, &r);
    CHECK(r == VNC_KC_CHAR && c == ':', "NUMERIC colon is shift+;");
    kc(KEY_SEMICOLON, 0, VNC_KP_NUMERIC, &r);
    CHECK(r == VNC_KC_NONE, "NUMERIC bare semicolon: nothing");
    kc(KEY_SEMICOLON, 1, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_NONE, "FULL has no colon");
    kc(KEY_SEMICOLON, 1, VNC_KP_ALPHA, &r);
    CHECK(r == VNC_KC_NONE, "ALPHA has no colon");
    /* letters */
    kc(KEY_A, 0, VNC_KP_NUMERIC, &r);
    CHECK(r == VNC_KC_NONE, "NUMERIC rejects a letter");
    c = kc(KEY_A, 0, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_CHAR && c == 'a', "FULL a");
    c = kc(KEY_A, 1, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_CHAR && c == 'A', "FULL shift+a is A");
    c = kc(KEY_Z, 1, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_CHAR && c == 'Z', "FULL shift+z is Z");
    c = kc(KEY_Q, 0, VNC_KP_ALPHA, &r);
    CHECK(r == VNC_KC_CHAR && c == 'q', "ALPHA q");
    kc(KEY_Q, 1, VNC_KP_ALPHA, &r);
    CHECK(r == VNC_KC_NONE, "ALPHA has no upper case");
    /* symbols */
    c = kc(KEY_MINUS, 0, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_CHAR && c == '-', "FULL minus");
    c = kc(KEY_MINUS, 1, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_CHAR && c == '_', "FULL shift+minus is underscore");
    c = kc(KEY_MINUS, 1, VNC_KP_ALPHA, &r);
    CHECK(r == VNC_KC_CHAR && c == '_', "ALPHA shift+minus is underscore");
    c = kc(KEY_MINUS, 0, VNC_KP_ALPHA, &r);
    CHECK(r == VNC_KC_CHAR && c == '-', "ALPHA minus");
    kc(KEY_MINUS, 0, VNC_KP_NUMERIC, &r);
    CHECK(r == VNC_KC_NONE, "NUMERIC has no minus");
    c = kc(KEY_DOT, 0, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_CHAR && c == '.', "FULL dot");
    c = kc(KEY_DOT, 0, VNC_KP_ALPHA, &r);
    CHECK(r == VNC_KC_CHAR && c == '.', "ALPHA dot");
    c = kc(KEY_1, 1, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_CHAR && c == '!', "FULL shift+1 is bang");
    kc(KEY_2, 1, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_NONE, "FULL shift+2 (@) is not on the keypad");
    kc(KEY_1, 1, VNC_KP_NUMERIC, &r);
    CHECK(r == VNC_KC_NONE, "NUMERIC shift+1: nothing");
    kc(KEY_1, 1, VNC_KP_ALPHA, &r);
    CHECK(r == VNC_KC_NONE, "ALPHA shift+1: nothing");
    /* space is a char in ALPHA only */
    c = kc(KEY_SPACE, 0, VNC_KP_ALPHA, &r);
    CHECK(r == VNC_KC_CHAR && c == ' ', "ALPHA space");
    kc(KEY_SPACE, 0, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_NONE, "FULL space: nothing (stays nav activate)");
    kc(KEY_SPACE, 0, VNC_KP_NUMERIC, &r);
    CHECK(r == VNC_KC_NONE, "NUMERIC space: nothing");
    /* edit actions, every mode */
    for (i = 0; i < 3; i++) {
        kc(KEY_BACKSPACE, 0, i, &r);
        CHECK(r == VNC_KC_BACKSPACE, "backspace, mode %d", i);
        kc(KEY_ENTER, 0, i, &r);
        CHECK(r == VNC_KC_OK, "enter is OK, mode %d", i);
        kc(KEY_KPENTER, 0, i, &r);
        CHECK(r == VNC_KC_OK, "keypad enter is OK, mode %d", i);
        kc(KEY_ESC, 0, i, &r);
        CHECK(r == VNC_KC_CANCEL, "esc is CANCEL, mode %d", i);
    }
    /* unknown key, bad mode, NULL ch, ch cleared on non-char */
    c = kc(KEY_F1, 0, VNC_KP_FULL, &r);
    CHECK(r == VNC_KC_NONE && c == 0, "unknown key: NONE and ch cleared");
    kc(KEY_A, 0, 7, &r);
    CHECK(r == VNC_KC_NONE, "unknown mode types nothing");
    CHECK(vnc_key_char(KEY_A, 0, VNC_KP_FULL, NULL) == VNC_KC_CHAR, "NULL ch is allowed");
    /* nav is unchanged: Enter / Esc / Space still nav actions */
    CHECK(vnc_nav_key(KEY_ENTER, 1) == VNC_NAV_ACTIVATE && vnc_nav_key(KEY_ESC, 1) == VNC_NAV_BACK &&
          vnc_nav_key(KEY_SPACE, 1) == VNC_NAV_ACTIVATE, "vnc_nav_key unchanged");
}

static void group9_kp(void) {
    int sh = 0;
    char c = 0;
    char buf[9] = "";
    int cur = 0;

    /* shift tracking: any value of a shift key, never an edit itself */
    CHECK(vnc_kp_key(KEY_LEFTSHIFT, 1, &sh, VNC_KP_FULL, &c) == VNC_KC_NONE && sh == VNC_KP_SHIFT_L, "left shift down sets its bit");
    vnc_kp_key(KEY_RIGHTSHIFT, 1, &sh, VNC_KP_FULL, &c);
    CHECK(sh == (VNC_KP_SHIFT_L | VNC_KP_SHIFT_R), "both shifts");
    CHECK(vnc_kp_key(KEY_A, 1, &sh, VNC_KP_FULL, &c) == VNC_KC_CHAR && c == 'A', "shift held: A");
    vnc_kp_key(KEY_LEFTSHIFT, 0, &sh, VNC_KP_FULL, &c);
    CHECK(sh == VNC_KP_SHIFT_R, "releasing left leaves right");
    CHECK(vnc_kp_key(KEY_A, 1, &sh, VNC_KP_FULL, &c) == VNC_KC_CHAR && c == 'A', "right shift still holds");
    vnc_kp_key(KEY_RIGHTSHIFT, 0, &sh, VNC_KP_FULL, &c);
    CHECK(sh == 0 && vnc_kp_key(KEY_A, 1, &sh, VNC_KP_FULL, &c) == VNC_KC_CHAR && c == 'a', "both up: a");

    /* value filter */
    CHECK(vnc_kp_key(KEY_A, 2, &sh, VNC_KP_FULL, &c) == VNC_KC_CHAR, "char repeats");
    CHECK(vnc_kp_key(KEY_BACKSPACE, 2, &sh, VNC_KP_FULL, &c) == VNC_KC_BACKSPACE, "backspace repeats");
    CHECK(vnc_kp_key(KEY_A, 0, &sh, VNC_KP_FULL, &c) == VNC_KC_NONE && c == 0, "release types nothing");
    CHECK(vnc_kp_key(KEY_ENTER, 1, &sh, VNC_KP_FULL, &c) == VNC_KC_OK, "enter press is OK");
    CHECK(vnc_kp_key(KEY_ENTER, 2, &sh, VNC_KP_FULL, &c) == VNC_KC_NONE, "enter repeat is not OK");
    CHECK(vnc_kp_key(KEY_ESC, 2, &sh, VNC_KP_NUMERIC, &c) == VNC_KC_NONE, "esc repeat is not CANCEL");
    CHECK(vnc_kp_key(KEY_ESC, 1, &sh, VNC_KP_NUMERIC, &c) == VNC_KC_CANCEL, "esc press is CANCEL");

    /* shared insert / backspace */
    CHECK(vnc_kp_insert(buf, 9, &cur, 3, 'x') && vnc_kp_insert(buf, 9, &cur, 3, 'y') &&
          vnc_kp_insert(buf, 9, &cur, 3, 'z') && cur == 3 && strcmp(buf, "xyz") == 0, "inserts to max");
    CHECK(!vnc_kp_insert(buf, 9, &cur, 3, 'w') && cur == 3 && strcmp(buf, "xyz") == 0, "max refuses");
    cur = 8; memset(buf, 'q', 8); buf[8] = 0;
    CHECK(!vnc_kp_insert(buf, 9, &cur, 99, 'w') && cur == 8, "buffer capacity refuses past max");
    cur = 2; strcpy(buf, "ab");
    CHECK(vnc_kp_backspace(buf, &cur) && cur == 1 && strcmp(buf, "a") == 0, "backspace");
    CHECK(vnc_kp_backspace(buf, &cur) && !vnc_kp_backspace(buf, &cur) && cur == 0 && buf[0] == 0, "backspace on empty is a no-op");
}

/* A keyboard's Esc: a tap reaches the remote, a hold opens Settings. */
static void group10_esc(void) {
    UiHold h;
    int pm;
    memset(&h, 0, sizeof(h));
    CHECK(vnc_esc_event(&h, 0, 50) == VNC_ESC_NONE, "a release with no press: nothing");
    CHECK(vnc_esc_event(&h, 1, 1000) == VNC_ESC_NONE, "press: withheld, nothing yet");
    CHECK(h.down && !vnc_esc_hold_exit(&h, 1100, &pm) && pm > 0 && pm < 1000,
          "held 100 ms: progress %d, no exit", pm);
    CHECK(vnc_esc_event(&h, 0, 1100) == VNC_ESC_TAP, "short release: a tap for the remote");
    CHECK(!h.down && !vnc_esc_hold_exit(&h, 9000, &pm) && pm == 0, "released: no hold left");

    /* autorepeat neither starts nor restarts a hold */
    CHECK(vnc_esc_event(&h, 2, 2000) == VNC_ESC_NONE && !h.down,
          "a repeat with no press starts nothing");
    vnc_esc_event(&h, 1, 3000);
    CHECK(vnc_esc_event(&h, 2, 3000 + UI_HOLD_EXIT_MS - 10) == VNC_ESC_NONE,
          "a repeat is never a tap or an exit");
    CHECK(vnc_esc_hold_exit(&h, 3000 + UI_HOLD_EXIT_MS, &pm) && pm == 1000,
          "held %d ms across repeats: exit, the repeat did not restart it", UI_HOLD_EXIT_MS);

    /* a release after the hold is an exit, never a tap */
    CHECK(vnc_esc_event(&h, 0, 3000 + UI_HOLD_EXIT_MS + 200) == VNC_ESC_EXIT,
          "release after the hold: EXIT, not TAP");
    CHECK(vnc_esc_event(&h, 0, 9000) == VNC_ESC_NONE, "a second release: nothing");

    /* 1 ms short of the hold is still a tap */
    vnc_esc_event(&h, 1, 20000);
    CHECK(!vnc_esc_hold_exit(&h, 20000 + UI_HOLD_EXIT_MS - 1, &pm),
          "1 ms short: no exit");
    CHECK(vnc_esc_event(&h, 0, 20000 + UI_HOLD_EXIT_MS - 1) == VNC_ESC_TAP,
          "1 ms short, released: a tap");

    /* across the uint32 clock wrap */
    vnc_esc_event(&h, 1, 0xFFFFFF00u);
    CHECK(vnc_esc_hold_exit(&h, 0xFFFFFF00u + UI_HOLD_EXIT_MS, &pm),
          "a hold spanning the wrap still completes");
}

int main(void) {
    group1_axis();
    group2_events();
    group3_motion();
    group4_back();
    group5_map();
    group6_speed();
    group7_nav();
    group8_keychar();
    group9_kp();
    group10_esc();
    printf("vnc_pad_test: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
