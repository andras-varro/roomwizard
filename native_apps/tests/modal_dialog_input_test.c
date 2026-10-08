/*
 * modal_dialog_input_test.c — pad / keyboard on a ModalDialog, on the host.
 *
 * Reported from the panel 2026-10-08: no game's pause dialog could be driven
 * by the controller.  Each game read A and Start as "resume" and left the rest
 * to touch, so EXIT (and brick_breaker's RETIRE) were touch-only.
 * modal_dialog_input() is the one answer for every dialog: D-pad moves the
 * focus, A / Enter presses it, Start / Esc and Select / Backspace answer the
 * caller's `cancel`.
 *
 * Build (host gcc, from native_apps/): the link line is button_latch_test's —
 * see tests/run-all.sh.
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "../common/common.h"

static int pass, fail;
#define CHECK(cond, msg) do { if (cond) pass++; else { fail++; \
    printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static InputState press(int id) {
    InputState in;
    memset(&in, 0, sizeof(in));
    if (id >= 0) in.buttons[id].pressed = true;
    return in;
}

static void open_dialog(ModalDialog *d, int n) {
    modal_dialog_init(d, "PAUSED", NULL, n);
    for (int i = 0; i < n; i++)
        modal_dialog_set_button(d, i, "B", COLOR_WHITE, COLOR_BLACK);
    modal_dialog_set_focus(d, 0);
    modal_dialog_show(d);
}

int main(void) {
    ModalDialog d;
    InputState in;

    /* The reported symptom: EXIT (button 1) is reachable with the pad. */
    open_dialog(&d, 2);
    in = press(BTN_ID_RIGHT);
    CHECK(modal_dialog_input(&d, &in, 0) == MODAL_ACTION_NONE, "a move answers nothing");
    CHECK(d.focus == 1 && d.active, "RIGHT moves the focus to EXIT");
    in = press(BTN_ID_JUMP);
    CHECK(modal_dialog_input(&d, &in, 0) == MODAL_ACTION_BTN1, "A presses EXIT");
    CHECK(!d.active, "an answer hides the dialog");

    /* ACTION (Enter) presses too; focus 0 is RESUME. */
    open_dialog(&d, 2);
    in = press(BTN_ID_ACTION);
    CHECK(modal_dialog_input(&d, &in, 0) == MODAL_ACTION_BTN0, "Enter presses RESUME");

    /* Start / Esc and Select / Backspace answer `cancel`, whatever is focused. */
    open_dialog(&d, 2);
    modal_dialog_set_focus(&d, 1);
    in = press(BTN_ID_PAUSE);
    CHECK(modal_dialog_input(&d, &in, 0) == MODAL_ACTION_BTN0, "Start answers cancel, not the focus");
    open_dialog(&d, 2);
    modal_dialog_set_focus(&d, 1);
    in = press(BTN_ID_BACK);
    CHECK(modal_dialog_input(&d, &in, 0) == MODAL_ACTION_BTN0, "BACK answers cancel");

    /* Three stacked buttons (brick_breaker): DOWN steps, UP wraps. */
    open_dialog(&d, 3);
    in = press(BTN_ID_DOWN);
    modal_dialog_input(&d, &in, 0);
    CHECK(d.focus == 1, "DOWN reaches RETIRE");
    in = press(BTN_ID_UP); modal_dialog_input(&d, &in, 0);
    in = press(BTN_ID_UP); modal_dialog_input(&d, &in, 0);
    CHECK(d.focus == 2, "UP from the top wraps to EXIT");

    /* No focus set: the first call adopts button 0, so the frame shows. */
    modal_dialog_init(&d, "X", NULL, 2);
    modal_dialog_show(&d);
    in = press(-1);
    CHECK(modal_dialog_input(&d, &in, 0) == MODAL_ACTION_NONE && d.focus == 0,
          "no focus adopts button 0 and answers nothing");

    /* Held is not pressed: a button still down from the press that opened the
     * dialog must not answer it. */
    open_dialog(&d, 2);
    memset(&in, 0, sizeof(in));
    in.buttons[BTN_ID_JUMP].held = true;
    in.buttons[BTN_ID_PAUSE].held = true;
    CHECK(modal_dialog_input(&d, &in, 0) == MODAL_ACTION_NONE && d.active,
          "held buttons answer nothing");

    /* An inactive dialog and an out-of-range cancel are refused. */
    open_dialog(&d, 2);
    modal_dialog_hide(&d);
    in = press(BTN_ID_JUMP);
    CHECK(modal_dialog_input(&d, &in, 0) == MODAL_ACTION_NONE, "inactive dialog ignores input");
    open_dialog(&d, 2);
    in = press(BTN_ID_PAUSE);
    CHECK(modal_dialog_input(&d, &in, 5) == MODAL_ACTION_NONE && d.active,
          "out-of-range cancel answers nothing and keeps the dialog");
    CHECK(modal_dialog_input(&d, NULL, 0) == MODAL_ACTION_NONE, "NULL input is refused");

    printf("modal_dialog_input_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
