/*
 * button_latch_test.c — the pause-menu-dies-after-one-use bug, on the host.
 *
 * Reported from the panel 2026-08-10: Office Runner stopped responding to the
 * hamburger menu mid-session, while the red EXIT button in the same HUD row
 * still worked.  Cause is not in either button — it is in how the caller asks.
 *
 *   button_check_press(btn, currently_pressed, now)  derives the press EDGE
 *   itself, from btn->was_pressed.  It clears that latch on exactly one kind of
 *   call: one where currently_pressed is FALSE.  A caller that only ever calls
 *   it with true therefore fires once per process and is dead thereafter.
 *
 * platformer.c and frogger.c both had:
 *     if (button_is_touched(&menu_button, ts.x, ts.y) &&
 *         button_check_press(&menu_button, true, now))        <-- short-circuit
 * so the false call never happens.  brick_breaker.c and tetris.c pass the value
 * instead, which is why their pause menus keep working — but only because their
 * players tap elsewhere constantly, which is what produces the false call.
 *
 * EXIT hid the whole thing: it only ever needs to fire once, because it ends the
 * process.  Same defect, no symptom.
 *
 * Group A is the negative control on this harness: it drives the OLD idiom and
 * asserts the reported symptom reproduces.  Without it, a green suite would not
 * distinguish "the fix works" from "the harness cannot see the bug".
 *
 * Build (host gcc, from native_apps/):
 *   gcc -Wall -Wextra -Wno-unused-parameter -I common -o build/button_latch_test \
 *       tests/button_latch_test.c common/common.c common/framebuffer.c \
 *       common/touch_input.c common/hardware.c common/config.c \
 *       common/highscore.c common/keyboard.c common/audio.c common/audio_gen.c \
 *       common/audio_out.c common/audio_wav.c -lm && \
 *   ./build/button_latch_test
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "../common/common.h"

static int failures = 0;
static int checks   = 0;

static void check(bool cond, const char *what) {
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL: %s\n", what);
    } else {
        printf("  ok:   %s\n", what);
    }
}

/* A button at a known rect, debounce left at whatever button_init sets. */
static void make_button(Button *b) {
    memset(b, 0, sizeof(*b));
    button_init(b, 10, 10, 70, 40, "", 0x808080, 0xFFFFFF, 0xFFFF00);
}

/* ── The two call idioms, as the games write them ──────────────────────── */

/* Pre-fix: only ever called with true, and only when the finger is inside.
 * Returns whether the tap fired. */
static bool tap_old_idiom(Button *b, const TouchState *ts, uint32_t now) {
    if (ts->pressed && button_is_touched(b, ts->x, ts->y))
        return button_check_press(b, true, now);
    return false;   /* no call at all — the latch is never cleared */
}

/* Post-fix: called every frame with a LEVEL signal.  Gating on pressed||held
 * matters: touch_input.c leaves state.x/y at the last coordinate after release,
 * so button_is_touched() alone stays true with no finger on the panel and the
 * latch would never clear either. */
static bool tap_helper(Button *b, const TouchState *ts, uint32_t now) {
    return button_check_tap(b, ts, now);
}

/* Frame helpers.  A real tap is one `pressed` frame then quiet frames; the
 * quiet frames keep the last coordinate, as the driver does. */
static TouchState frame_down(int x, int y) {
    TouchState ts; memset(&ts, 0, sizeof(ts));
    ts.x = x; ts.y = y; ts.pressed = true; ts.held = true;
    return ts;
}
static TouchState frame_quiet(int x, int y) {
    TouchState ts; memset(&ts, 0, sizeof(ts));
    ts.x = x; ts.y = y;             /* coords retained, nothing pressed */
    return ts;
}

#define IN_X  40
#define IN_Y  25
#define OUT_X 400
#define OUT_Y 300

int main(void) {
    uint32_t now = 100000;          /* well past any debounce baseline */
    const uint32_t QUIET = 400;     /* > BTN_DEBOUNCE_MS between taps */

    printf("\n=== A. the reported symptom, old idiom (must reproduce) ===\n");
    {
        Button b; make_button(&b);
        TouchState d = frame_down(IN_X, IN_Y);
        check(tap_old_idiom(&b, &d, now), "1st tap on the button fires");

        /* Finger lifts: the caller makes no call, because it short-circuits. */
        TouchState q = frame_quiet(IN_X, IN_Y);
        (void)tap_old_idiom(&b, &q, now + 10);

        now += QUIET;
        TouchState d2 = frame_down(IN_X, IN_Y);
        check(tap_old_idiom(&b, &d2, now) == false,
              "2nd tap is SWALLOWED — this is the panel report, reproduced");
        check(b.was_pressed == true,
              "the latch is still set, with no finger on the panel");
    }

    printf("\n=== B. same sequence through button_check_tap (must work) ===\n");
    {
        Button b; make_button(&b);
        now = 200000;
        int fired = 0;
        for (int tap = 0; tap < 4; tap++) {
            TouchState d = frame_down(IN_X, IN_Y);
            if (tap_helper(&b, &d, now)) fired++;
            /* two quiet frames, as the main loop would deliver */
            TouchState q = frame_quiet(IN_X, IN_Y);
            tap_helper(&b, &q, now + 10);
            tap_helper(&b, &q, now + 20);
            now += QUIET;
        }
        check(fired == 4, "4 separate taps fire 4 times");
    }

    printf("\n=== C. negative control: debounce must still work ===\n");
    {
        Button b; make_button(&b);
        now = 300000;
        int fired = 0;
        TouchState d = frame_down(IN_X, IN_Y);
        if (tap_helper(&b, &d, now)) fired++;
        TouchState q = frame_quiet(IN_X, IN_Y);
        tap_helper(&b, &q, now + 5);            /* release */
        TouchState d2 = frame_down(IN_X, IN_Y); /* bounce, 20 ms later */
        if (tap_helper(&b, &d2, now + 20)) fired++;
        check(fired == 1, "a 20 ms bounce fires once, not twice");
    }

    printf("\n=== D. retained coordinates must not hold the latch ===\n");
    {
        Button b; make_button(&b);
        now = 400000;
        TouchState d = frame_down(IN_X, IN_Y);
        check(tap_helper(&b, &d, now), "tap fires");
        /* Quiet frames with x/y STILL inside the rect — the real driver state.
         * A fix that tested button_is_touched() alone would stay latched here. */
        TouchState q = frame_quiet(IN_X, IN_Y);
        for (int i = 0; i < 5; i++) tap_helper(&b, &q, now + 10 + i);
        check(b.was_pressed == false,
              "latch cleared despite x/y still inside the button");
        now += QUIET;
        TouchState d2 = frame_down(IN_X, IN_Y);
        check(tap_helper(&b, &d2, now), "next tap fires");
    }

    printf("\n=== E. a tap elsewhere must not fire this button ===\n");
    {
        Button b; make_button(&b);
        now = 500000;
        TouchState d = frame_down(OUT_X, OUT_Y);
        check(tap_helper(&b, &d, now) == false, "tap outside does not fire");
        check(b.was_pressed == false, "and does not set the latch");
    }

    printf("\n%s  %d checks, %d failure(s)\n",
           failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
