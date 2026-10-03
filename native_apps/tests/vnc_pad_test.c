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
 * 5 the map is the shared parser's defaults; 6 the speed for a desktop width.
 *
 * ⚠️ What it cannot see: which raw codes a real pad sends (input_pad_key()
 * and the scan are input_scan_test's), whether vnc_input.c feeds every event
 * and calls the motion step every loop, and what the server does with the
 * pointer events — those are checked on the panel in a live session.
 */
#include "vnc_pad.h"
#include <stdio.h>

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

int main(void) {
    group1_axis();
    group2_events();
    group3_motion();
    group4_back();
    group5_map();
    group6_speed();
    printf("vnc_pad_test: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
