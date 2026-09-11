/* Host-side regression for gamepad.c's held-state model: an event-driven source
 * (pad button, D-pad hat, keyboard) keeps its level in GamepadManager.held_latched[]
 * because a key-up may be many frames away, while an absolute-position source
 * (touch region, analog stick) is rebuilt every poll so it cannot latch.  Both
 * used to write the caller's InputState and only the first kind was ever cleared,
 * which left a virtual D-pad asserted for the life of the process.
 *
 * Runs on the DEV MACHINE with native gcc, not on the device:
 *
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *       -o build/gamepad_latch_test tests/gamepad_latch_test.c \
 *       common/gamepad.c common/framebuffer.c common/hardware.c \
 *       common/config.c common/touch_input.c -lm && ./build/gamepad_latch_test
 *
 * The bug: `.held` used to be *stored* in the caller's InputState, written
 * directly by every source and cleared by none.  For the event-driven sources
 * (keys, D-pad hat) that was correct — a key-up event clears it.  For the two
 * sources that report an absolute position it was not: a touch region and the
 * analog-stick→D-pad merge both set `.held = true` and nothing ever set it
 * false, so one tap on a virtual pad ran the player left forever, and a `.pressed`
 * reader saw its first tap and then nothing (confirmed on RW09 2026-08-02 in
 * frogger: the zones stayed highlighted and the frog jumped on its own).
 *
 * The fix splits the two kinds of source: event-driven level state lives in
 * GamepadManager.held_latched[], position-reporting sources go into a per-frame
 * array, and `state->buttons[i].held` becomes a pure output = latched||derived.
 *
 * WHY THIS IS TESTABLE ON THE HOST AT ALL, given there is no /dev/uinput on the
 * device and touch cannot be synthesised there (see CLAUDE.md): gamepad_poll()
 * takes the touch coordinate as a plain argument, and the evdev sources are just
 * read(2) on an fd.  So a temp file full of struct input_event, assigned to
 * gm.gamepad_fd, drives the real poll_gamepad() — read() returns each event and
 * then 0 at EOF, exactly like a non-blocking evdev fd going quiet.  No device,
 * no kernel support, no human at the panel.  What still needs the panel is only
 * whether a *game* feels right; the state machine is covered here.
 *
 * NOT run by build-and-deploy.sh — that cross-compiles for ARM, this is host gcc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include "gamepad.h"
#include "framebuffer.h"

static int fails = 0;

static void expect_bool(const char *what, bool got, bool want) {
    if (got != want) {
        printf("  FAIL %-52s got %s want %s\n", what,
               got ? "true" : "false", want ? "true" : "false");
        fails++;
    } else {
        printf("  ok   %-52s %s\n", what, got ? "true" : "false");
    }
}

static void expect_int(const char *what, int got, int want) {
    if (got != want) { printf("  FAIL %-52s got %d want %d\n", what, got, want); fails++; }
    else             { printf("  ok   %-52s %d\n", what, got); }
}

/* ── A fake evdev fd: a temp file holding a sequence of input_events ─────────
 *
 * poll_gamepad()/poll_keyboard() loop until read() returns something other than
 * sizeof(struct input_event); at EOF a regular file returns 0, which ends the
 * loop the same way EAGAIN does on a real non-blocking evdev fd. */
static int fake_fd(void) {
    char path[] = "/tmp/rw_gamepad_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); exit(2); }
    unlink(path);           /* keep it only as long as the fd lives */
    return fd;
}

static void feed(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) { perror("write"); exit(2); }
}

/* Make everything written so far readable by the next poll. */
static void rewind_fd(int fd) {
    if (lseek(fd, 0, SEEK_SET) < 0) { perror("lseek"); exit(2); }
}

/* Manager with no real devices: gamepad_init() scans /dev/input and finds
 * nothing on the host (or finds the dev box's own keyboard, which we then
 * discard), so force every fd closed and start from a known state. */
static void manager_init(GamepadManager *gm) {
    gamepad_init(gm);
    gamepad_close(gm);              /* drop whatever the host happened to have */
    memset(gm->held_latched, 0, sizeof(gm->held_latched));
    memset(gm->prev_held, 0, sizeof(gm->prev_held));
}

/* A real pad reports its axis range via EVIOCGABS, which only happens in
 * scan_devices().  A fake fd has no ioctls, so plant the range by hand —
 * without it normalize_axis_calibrated() sees min == max and returns 0. */
static void fake_stick_calibration(GamepadManager *gm) {
    for (int i = 0; i < GAMEPAD_MAX_AXES; i++) {
        gm->axis_min[i] = -32768;
        gm->axis_max[i] =  32767;
        gm->axis_calib[i].center = 0;
    }
}

/* ═══ 1. Touch regions must not latch ═══════════════════════════════════════ */
static void test_touch_region_releases(void) {
    printf("\nTouch region: asserted while inside, cleared on lift\n");

    GamepadManager gm;
    InputState st;
    manager_init(&gm);
    memset(&st, 0, sizeof(st));

    TouchRegion r = { 100, 100, 80, 60, BTN_ID_LEFT };
    gamepad_set_touch_regions(&gm, &r, 1);

    /* Finger down inside the region. */
    gamepad_poll(&gm, &st, 120, 120, true);
    expect_bool("in-region: LEFT held", st.buttons[BTN_ID_LEFT].held, true);
    expect_bool("in-region: LEFT pressed edge", st.buttons[BTN_ID_LEFT].pressed, true);

    /* Still down, same place — level stays, edge does not repeat. */
    gamepad_poll(&gm, &st, 120, 120, true);
    expect_bool("still down: LEFT held", st.buttons[BTN_ID_LEFT].held, true);
    expect_bool("still down: no repeated press edge", st.buttons[BTN_ID_LEFT].pressed, false);

    /* THIS is the latch the suite exists for: held used to stay true forever here. */
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("lifted: LEFT no longer held", st.buttons[BTN_ID_LEFT].held, false);
    expect_bool("lifted: LEFT released edge", st.buttons[BTN_ID_LEFT].released, true);

    /* And a second tap must produce a second press edge — the frogger/snake
     * symptom was "first tap works, every later tap is dead". */
    gamepad_poll(&gm, &st, 120, 120, true);
    expect_bool("second tap: LEFT pressed edge again", st.buttons[BTN_ID_LEFT].pressed, true);

    /* Dragging out of the region without lifting also clears it. */
    gamepad_poll(&gm, &st, 400, 400, true);
    expect_bool("dragged out: LEFT no longer held", st.buttons[BTN_ID_LEFT].held, false);

    gamepad_close(&gm);
}

/* ═══ 2. Keys and the hat must still latch across quiet frames ══════════════
 * The other half of the bug: the naive fix is to clear held at the top of every
 * poll, which breaks these — a key-down event arrives once and the key-up may be
 * hundreds of frames later. */
static void test_key_and_hat_latch(void) {
    printf("\nKeys and D-pad hat: level persists between events\n");

    GamepadManager gm;
    InputState st;
    manager_init(&gm);
    memset(&st, 0, sizeof(st));

    int fd = fake_fd();
    gm.gamepad_fd = fd;

    /* A press, then two polls with no events at all. */
    feed(fd, EV_KEY, BTN_SOUTH, 1);
    rewind_fd(fd);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("key down: JUMP held", st.buttons[BTN_ID_JUMP].held, true);

    gamepad_poll(&gm, &st, 0, 0, false);   /* nothing to read */
    expect_bool("no events: JUMP still held", st.buttons[BTN_ID_JUMP].held, true);

    /* A caller that zeroes its InputState between polls must not lose it —
     * this is what lets app_launcher's drain loop use a throwaway state. */
    memset(&st, 0, sizeof(st));
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("zeroed InputState: JUMP still held", st.buttons[BTN_ID_JUMP].held, true);

    /* Release. */
    if (ftruncate(fd, 0) < 0) { perror("ftruncate"); exit(2); }
    rewind_fd(fd);
    feed(fd, EV_KEY, BTN_SOUTH, 0);
    rewind_fd(fd);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("key up: JUMP released", st.buttons[BTN_ID_JUMP].held, false);

    /* Same for the hat, which reports a direction as an EV_ABS value. */
    if (ftruncate(fd, 0) < 0) { perror("ftruncate"); exit(2); }
    rewind_fd(fd);
    feed(fd, EV_ABS, ABS_HAT0X, -1);
    rewind_fd(fd);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("hat left: LEFT held", st.buttons[BTN_ID_LEFT].held, true);

    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("hat left, no events: LEFT still held", st.buttons[BTN_ID_LEFT].held, true);

    if (ftruncate(fd, 0) < 0) { perror("ftruncate"); exit(2); }
    rewind_fd(fd);
    feed(fd, EV_ABS, ABS_HAT0X, 0);
    rewind_fd(fd);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("hat centred: LEFT cleared", st.buttons[BTN_ID_LEFT].held, false);

    gamepad_close(&gm);
}

/* ═══ 3. The analog stick must not latch either ═════════════════════════════ */
static void test_stick_releases(void) {
    printf("\nAnalog stick: direction follows the stick back to centre\n");

    GamepadManager gm;
    InputState st;
    manager_init(&gm);
    memset(&st, 0, sizeof(st));

    int fd = fake_fd();
    gm.gamepad_fd = fd;
    fake_stick_calibration(&gm);

    /* Full left deflection. */
    feed(fd, EV_ABS, ABS_X, -32768);
    rewind_fd(fd);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_int ("stick left: axis_lx normalized", st.axis_lx, -1000);
    expect_bool("stick left: LEFT held", st.buttons[BTN_ID_LEFT].held, true);

    /* Back to centre.  Pre-fix, LEFT stayed held for the life of the process. */
    if (ftruncate(fd, 0) < 0) { perror("ftruncate"); exit(2); }
    rewind_fd(fd);
    feed(fd, EV_ABS, ABS_X, 0);
    rewind_fd(fd);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_int ("stick centred: axis_lx", st.axis_lx, 0);
    expect_bool("stick centred: LEFT cleared", st.buttons[BTN_ID_LEFT].held, false);
    expect_bool("stick centred: LEFT released edge", st.buttons[BTN_ID_LEFT].released, true);

    gamepad_close(&gm);
}

/* ═══ 4. Unplugged while deflected / while a key is down ════════════════════ */
static void test_unplug_clears(void) {
    printf("\nUnplug: no source left to report a release\n");

    GamepadManager gm;
    InputState st;
    manager_init(&gm);
    memset(&st, 0, sizeof(st));
    fake_stick_calibration(&gm);

    /* A stick deflected at the moment the pad vanishes: the axes are stale in
     * the InputState and no further EV_ABS will ever arrive, so poll_gamepad's
     * fd < 0 branch has to zero them or the merge asserts LEFT forever. */
    st.axis_lx = -1000;
    gamepad_poll(&gm, &st, 0, 0, false);       /* gamepad_fd is -1 */
    expect_int ("no pad: axis_lx zeroed", st.axis_lx, 0);
    expect_bool("no pad: LEFT not asserted", st.buttons[BTN_ID_LEFT].held, false);

    /* A key held at unplug time: its key-up will never arrive, so a rescan has
     * to drop the latched level. */
    int fd = fake_fd();
    gm.gamepad_fd = fd;
    feed(fd, EV_KEY, BTN_SOUTH, 1);
    rewind_fd(fd);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("before rescan: JUMP held", st.buttons[BTN_ID_JUMP].held, true);

    gamepad_rescan(&gm);                        /* closes fds, rescans host */
    gamepad_close(&gm);                         /* discard anything it found */
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("after rescan: JUMP cleared", st.buttons[BTN_ID_JUMP].held, false);

    gamepad_close(&gm);
}

/* ═══ 5. Sources combine rather than overwrite ══════════════════════════════ */
static void test_sources_or_together(void) {
    printf("\nCombination: a latched key and a touch region both count\n");

    GamepadManager gm;
    InputState st;
    manager_init(&gm);
    memset(&st, 0, sizeof(st));

    int fd = fake_fd();
    gm.gamepad_fd = fd;

    TouchRegion r = { 0, 0, 50, 50, BTN_ID_JUMP };
    gamepad_set_touch_regions(&gm, &r, 1);

    feed(fd, EV_KEY, BTN_SOUTH, 1);            /* JUMP down on the pad */
    rewind_fd(fd);
    gamepad_poll(&gm, &st, 10, 10, true);      /* and a finger in the JUMP region */
    expect_bool("both sources: JUMP held", st.buttons[BTN_ID_JUMP].held, true);

    /* Lift the finger — the physically held key must keep it asserted. */
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("finger lifted, key down: JUMP still held", st.buttons[BTN_ID_JUMP].held, true);

    /* Release the key while the finger is back down — the region keeps it. */
    if (ftruncate(fd, 0) < 0) { perror("ftruncate"); exit(2); }
    rewind_fd(fd);
    feed(fd, EV_KEY, BTN_SOUTH, 0);
    rewind_fd(fd);
    gamepad_poll(&gm, &st, 10, 10, true);
    expect_bool("key up, finger down: JUMP still held", st.buttons[BTN_ID_JUMP].held, true);

    /* Both gone. */
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("both gone: JUMP cleared", st.buttons[BTN_ID_JUMP].held, false);

    gamepad_close(&gm);
}

/* ═══ 6. Mouse bounds follow the logical surface ════════════════════════════ */
/* gamepad_init() used to plant a compile-time 800x480 in mouse_screen_w/h, so a
 * component running a smaller surface — a game on a 400x240 fb1 that a scaling
 * DSS overlay upscales to fill the panel — let a USB mouse range over twice the
 * surface and clamped it at coordinates no pixel occupies. Six of the nine call
 * sites never called gamepad_set_mouse_bounds() to correct it, and the two that
 * actually feed mouse_x/y into hit-testing (app_launcher, game_selector) re-init
 * the manager after every child exits, which re-runs apply_defaults() and would
 * discard a startup-only call. So the bounds are taken from the framebuffer's
 * own logical size instead, which no call site can forget.
 *
 * ⚠️ The 800x480 case is the NEGATIVE CONTROL and it cannot fail on this defect:
 * screen_base_width/height default to exactly the constants that were there
 * before, so at full size the change is byte-identical and every ratio is the
 * identity. The 400x227 cases carry the whole assertion. Measured against the
 * pre-fix source: 8 failures, every one of them on the small surface, and the
 * clearest of them is the symptom itself — a warp to 799,479 landed there
 * instead of on the surface's last pixel at 399,226. */
static void test_mouse_bounds_follow_surface(void) {
    printf("\n6. mouse bounds come from the framebuffer, not a constant\n");

    int save_w = screen_base_width, save_h = screen_base_height;
    GamepadManager gm;

    /* The control: a full-size surface must behave exactly as before. */
    screen_base_width = 800; screen_base_height = 480;
    gamepad_init(&gm); gamepad_close(&gm);
    expect_int("800x480: bounds width",   gm.mouse_screen_w, 800);
    expect_int("800x480: bounds height",  gm.mouse_screen_h, 480);
    expect_int("800x480: cursor centred", gm.mouse_x, 400);

    /* The defect: .188's measured 400x240 fb1 less its scaled bezel. */
    screen_base_width = 400; screen_base_height = 227;
    gamepad_init(&gm); gamepad_close(&gm);
    expect_int("400x227: bounds width",   gm.mouse_screen_w, 400);
    expect_int("400x227: bounds height",  gm.mouse_screen_h, 227);
    expect_int("400x227: cursor centred", gm.mouse_x, 200);
    expect_int("400x227: cursor centred y", gm.mouse_y, 113);

    /* The symptom itself: a warp past the surface must land on its last pixel,
     * not on a coordinate the smaller surface has no room for. */
    gamepad_set_mouse_position(&gm, 799, 479);
    expect_int("400x227: warp clamps x", gm.mouse_x, 399);
    expect_int("400x227: warp clamps y", gm.mouse_y, 226);

    /* Survives the re-init that the launcher apps perform on every child exit. */
    gamepad_init(&gm); gamepad_close(&gm);
    expect_int("re-init keeps the surface width",  gm.mouse_screen_w, 400);
    expect_int("re-init keeps the surface height", gm.mouse_screen_h, 227);

    /* The fallback: globals that were never published leave the constants. */
    screen_base_width = 0; screen_base_height = 0;
    gamepad_init(&gm); gamepad_close(&gm);
    expect_int("unset globals fall back to 800", gm.mouse_screen_w,
               GAMEPAD_DEFAULT_SCREEN_W);
    expect_int("unset globals fall back to 480", gm.mouse_screen_h,
               GAMEPAD_DEFAULT_SCREEN_H);

    screen_base_width = save_w; screen_base_height = save_h;
}

int main(void) {
    printf("gamepad held-state regression (latched events vs per-frame positions)\n");

    test_touch_region_releases();
    test_key_and_hat_latch();
    test_stick_releases();
    test_unplug_clears();
    test_sources_or_together();
    test_mouse_bounds_follow_surface();

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
