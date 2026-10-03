/* Host-side regression for common/ui_focus.c — keyboard/pad focus movement.
 *
 * Runs on the DEV MACHINE with native gcc; the module is pure arithmetic over
 * rectangles, so nothing here needs a device.  Build and run:
 *
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *       -o build/ui_focus_test tests/ui_focus_test.c common/ui_focus.c \
 *       && ./build/ui_focus_test
 *
 * The fixtures are the SHAPES the Control Panel hands in, written out by hand:
 * the LED page in landscape (toggle, -/+ on one row, a 3x2 test grid, the page
 * bar's BACK) and in portrait (-/+ stacked under the label, a 2x3 grid); a
 * plain 3x2 grid; one rectangle; a row whose centres are a few pixels out of
 * line; and a device list that shrinks under the focus.
 *
 * ⚠️ What it cannot see: whether those rectangles are the ones the pages
 * really hit-test (that is each page's focusables(), checked on the panel),
 * and whether the ring is drawn.  It only proves the choice of index.
 *
 * Groups 8-9 are the testers' hold-to-exit timing (ui_hold_*).  They cannot
 * see whether a tester feeds it the right key codes, nor the bar it draws.
 * Groups 10-11 are the mouse tester's LEFT+RIGHT chord (ui_chord_button) and
 * the one timer it shares with the exit keys (ui_hold_either) — the same
 * blindness: which evdev codes reach them is input_page.c's, on the panel.
 */
#include "ui_focus.h"
#include <stdio.h>

static int fails;

static void expect(const char *what, int got, int want) {
    if (got != want) {
        printf("  FAIL %-52s got %d want %d\n", what, got, want);
        fails++;
    }
}

/* LED page, landscape 800x480: 0 BACK, 1 toggle, 2 minus, 3 plus, 4-9 tests. */
static const UiRect led_land[] = {
    {  10,   5, 100, 40 },   /* 0 BACK (page bar)            */
    {  25,  72, 170, 38 },   /* 1 LED toggle hit box          */
    { 220, 128,  50, 40 },   /* 2 -                           */
    { 600, 128,  50, 40 },   /* 3 +                           */
    {  20, 200, 243, 48 },   /* 4 test r0c0 */
    { 278, 200, 243, 48 },   /* 5 test r0c1 */
    { 536, 200, 243, 48 },   /* 6 test r0c2 */
    {  20, 262, 243, 48 },   /* 7 test r1c0 */
    { 278, 262, 243, 48 },   /* 8 test r1c1 */
    { 536, 262, 243, 48 },   /* 9 test r1c2 */
};
/* LED page, portrait 480x800: -/+ drop under the label; tests 2 columns. */
static const UiRect led_port[] = {
    {  10,   5, 100, 40 },   /* 0 BACK   */
    {  25,  72, 170, 38 },   /* 1 toggle */
    {  10, 160,  50, 40 },   /* 2 -      */
    { 410, 160,  50, 40 },   /* 3 +      */
    {  20, 230, 215, 48 },   /* 4 r0c0 */
    { 245, 230, 215, 48 },   /* 5 r0c1 */
    {  20, 292, 215, 48 },   /* 6 r1c0 */
    { 245, 292, 215, 48 },   /* 7 r1c1 */
    {  20, 354, 215, 48 },   /* 8 r2c0 */
    { 245, 354, 215, 48 },   /* 9 r2c1 */
};
/* 3x2 grid of equal tiles. */
static const UiRect grid[] = {
    { 0, 0, 100, 100 }, { 120, 0, 100, 100 }, { 240, 0, 100, 100 },
    { 0, 120, 100, 100 }, { 120, 120, 100, 100 }, { 240, 120, 100, 100 },
};

int main(void) {
    printf("group 1  each direction's target, LED landscape\n");
    expect("toggle DOWN -> test r0c0 (in line below)", ui_focus_step(led_land, 10, 1, UI_DIR_DOWN), 4);
    expect("toggle RIGHT -> minus",                  ui_focus_step(led_land, 10, 1, UI_DIR_RIGHT), 2);
    expect("minus RIGHT -> plus",                    ui_focus_step(led_land, 10, 2, UI_DIR_RIGHT), 3);
    expect("plus LEFT -> minus",                     ui_focus_step(led_land, 10, 3, UI_DIR_LEFT), 2);
    expect("minus DOWN -> test r0c0 (aligned col)",  ui_focus_step(led_land, 10, 2, UI_DIR_DOWN), 4);
    expect("plus DOWN -> test r0c2",                 ui_focus_step(led_land, 10, 3, UI_DIR_DOWN), 6);
    expect("test r0c1 UP -> minus/plus row",         ui_focus_step(led_land, 10, 5, UI_DIR_UP), 2);
    expect("test r1c1 UP -> r0c1",                   ui_focus_step(led_land, 10, 8, UI_DIR_UP), 5);
    expect("test r0c0 RIGHT -> r0c1",                ui_focus_step(led_land, 10, 4, UI_DIR_RIGHT), 5);
    expect("toggle UP -> BACK",                      ui_focus_step(led_land, 10, 1, UI_DIR_UP), 0);

    printf("group 2  each direction's target, LED portrait\n");
    expect("toggle DOWN -> minus",                   ui_focus_step(led_port, 10, 1, UI_DIR_DOWN), 2);
    expect("minus RIGHT -> plus",                    ui_focus_step(led_port, 10, 2, UI_DIR_RIGHT), 3);
    expect("plus DOWN -> r0c1",                      ui_focus_step(led_port, 10, 3, UI_DIR_DOWN), 5);
    expect("r1c0 DOWN -> r2c0",                      ui_focus_step(led_port, 10, 6, UI_DIR_DOWN), 8);
    expect("r2c1 LEFT -> r2c0",                      ui_focus_step(led_port, 10, 9, UI_DIR_LEFT), 8);

    printf("group 3  wrap at each edge\n");
    expect("grid r0c2 RIGHT wraps -> r0c0",          ui_focus_move(grid, 6, 2, UI_DIR_RIGHT), 0);
    expect("grid r1c0 LEFT wraps -> r1c2",           ui_focus_move(grid, 6, 3, UI_DIR_LEFT), 5);
    expect("grid r0c1 UP wraps -> r1c1",             ui_focus_move(grid, 6, 1, UI_DIR_UP), 4);
    expect("grid r1c2 DOWN wraps -> r0c2",           ui_focus_move(grid, 6, 5, UI_DIR_DOWN), 2);
    expect("LED land r1c2 DOWN wraps -> plus (column)",   ui_focus_move(led_land, 10, 9, UI_DIR_DOWN), 3);
    expect("LED land BACK UP wraps -> r1c0",         ui_focus_move(led_land, 10, 0, UI_DIR_UP), 7);
    expect("LED land plus RIGHT wraps -> minus row", ui_focus_move(led_land, 10, 3, UI_DIR_RIGHT), 2);
    expect("step reports no candidate at an edge",   ui_focus_step(grid, 6, 2, UI_DIR_RIGHT), -1);
    {
        UiRect from = { 400, 120, 100, 100 };   /* a rect off to the right, row 1 */
        expect("wrap from a rect not in the list",   ui_focus_wrap(grid, 6, &from, UI_DIR_RIGHT), 3);
    }

    printf("group 4  start (cur=-1), empty and single\n");
    expect("grid cur=-1 -> top-left",                ui_focus_move(grid, 6, -1, UI_DIR_DOWN), 0);
    expect("LED land cur=-1 -> BACK (top-left)",     ui_focus_move(led_land, 10, -1, UI_DIR_RIGHT), 0);
    expect("cur out of range -> first",              ui_focus_move(grid, 6, 9, UI_DIR_LEFT), 0);
    expect("n=0 move -> -1",                         ui_focus_move(grid, 0, -1, UI_DIR_UP), -1);
    expect("n=0 first -> -1",                        ui_focus_first(grid, 0), -1);
    expect("n=0 nearest -> -1",                      ui_focus_nearest(grid, 0, &grid[0]), -1);
    expect("n=0 step -> -1",                         ui_focus_step(grid, 0, 0, UI_DIR_UP), -1);
    {
        UiRect one[] = { { 300, 200, 80, 40 } };
        expect("single RIGHT stays",                 ui_focus_move(one, 1, 0, UI_DIR_RIGHT), 0);
        expect("single UP stays",                    ui_focus_move(one, 1, 0, UI_DIR_UP), 0);
        expect("single cur=-1 -> 0",                 ui_focus_move(one, 1, -1, UI_DIR_LEFT), 0);
    }

    printf("group 5  list shrinking 5 -> 2, re-pick by nearest centre\n");
    {
        UiRect rows5[5], rows2[2];
        for (int i = 0; i < 5; i++) rows5[i] = (UiRect){ 20, 100 + 44 * i, 700, 40 };
        rows2[0] = rows5[0];
        rows2[1] = rows5[1];
        expect("row 4 DOWN wraps -> row 0",          ui_focus_move(rows5, 5, 4, UI_DIR_DOWN), 0);
        expect("focus on row 4, shrink -> row 1",    ui_focus_nearest(rows2, 2, &rows5[4]), 1);
        expect("focus on row 0, shrink -> row 0",    ui_focus_nearest(rows2, 2, &rows5[0]), 0);
        expect("row 1 of 2 DOWN wraps -> row 0",     ui_focus_move(rows2, 2, 1, UI_DIR_DOWN), 0);
    }

    printf("group 6  misaligned layout\n");
    {
        /* One visual row whose centres wander by a few px, a taller widget in it,
         * and a second row underneath.  DOWN must never move along the row. */
        UiRect mis[] = {
            {  20, 100,  90, 32 },   /* 0 */
            { 130, 104,  90, 32 },   /* 1  4 px lower   */
            { 240,  96, 120, 44 },   /* 2  taller        */
            {  60, 180, 200, 40 },   /* 3  row 2, under 0/1 */
            { 300, 184, 120, 40 },   /* 4  row 2, under 2   */
        };
        expect("0 DOWN is row 2, not 1 (4 px lower)", ui_focus_step(mis, 5, 0, UI_DIR_DOWN), 3);
        expect("0 RIGHT -> 1",                        ui_focus_step(mis, 5, 0, UI_DIR_RIGHT), 1);
        expect("1 RIGHT -> 2 (taller)",               ui_focus_step(mis, 5, 1, UI_DIR_RIGHT), 2);
        expect("2 DOWN -> 4 (overlapping column)",    ui_focus_step(mis, 5, 2, UI_DIR_DOWN), 4);
        expect("4 UP -> 2",                           ui_focus_step(mis, 5, 4, UI_DIR_UP), 2);
        expect("2 RIGHT wraps -> 0 (same row)",       ui_focus_move(mis, 5, 2, UI_DIR_RIGHT), 0);
        expect("4 RIGHT wraps -> 3",                  ui_focus_move(mis, 5, 4, UI_DIR_RIGHT), 3);
    }

    printf("group 7  synthetic tap: press frame, then release frame, same point\n");
    {
        UiTap t = { 0, 0, 0 };
        int x = 7, y = 7; bool touching = false, pr = false, rl = false;
        expect("idle tap leaves the frame alone",
               ui_tap_frame(&t, false, &x, &y, &touching, &pr, &rl), 0);
        expect("idle: x untouched", x, 7);
        UiRect r = { 100, 50, 41, 20 };
        ui_tap_begin(&t, &r);
        expect("frame 1 overrides", ui_tap_frame(&t, false, &x, &y, &touching, &pr, &rl), 1);
        expect("frame 1 x = centre", x, 120);
        expect("frame 1 y = centre", y, 60);
        expect("frame 1 touching", touching, 1);
        expect("frame 1 pressed edge", pr, 1);
        expect("frame 1 no release", rl, 0);
        x = y = 0;
        expect("frame 2 overrides", ui_tap_frame(&t, false, &x, &y, &touching, &pr, &rl), 1);
        expect("frame 2 same x", x, 120);
        expect("frame 2 same y", y, 60);
        expect("frame 2 not touching", touching, 0);
        expect("frame 2 no press", pr, 0);
        expect("frame 2 released edge", rl, 1);
        expect("frame 3 idle again", ui_tap_frame(&t, false, &x, &y, &touching, &pr, &rl), 0);
        ui_tap_begin(&t, &r);
        expect("a real finger cancels the tap",
               ui_tap_frame(&t, true, &x, &y, &touching, &pr, &rl), 0);
        expect("and it stays cancelled",
               ui_tap_frame(&t, false, &x, &y, &touching, &pr, &rl), 0);
    }

    printf("group 8  hold to exit: progress and the exit flag\n");
    {
        bool ex = true;
        expect("not down: 0",            ui_hold_progress(false, 5000, 1000, 1500, &ex), 0);
        expect("not down: no exit",      ex, 0);
        expect("just pressed: 0",        ui_hold_progress(true, 1000, 1000, 1500, &ex), 0);
        expect("just pressed: no exit",  ex, 0);
        expect("half way: 500",          ui_hold_progress(true, 1750, 1000, 1500, &ex), 500);
        expect("half way: no exit",      ex, 0);
        expect("1 ms short: 999",        ui_hold_progress(true, 2499, 1000, 1500, &ex), 999);
        expect("1 ms short: no exit",    ex, 0);
        expect("at 1500: 1000",          ui_hold_progress(true, 2500, 1000, 1500, &ex), 1000);
        expect("at 1500: exit",          ex, 1);
        expect("long past: 1000",        ui_hold_progress(true, 90000, 1000, 1500, &ex), 1000);
        expect("long past: exit",        ex, 1);
        /* The millisecond clock wraps every ~49 days; a hold across it. */
        expect("across the wrap: 500",   ui_hold_progress(true, 350u, 0xFFFFFE70u, 1500, &ex), 500);  /* 400 + 350 */
        expect("across the wrap: no exit", ex, 0);
        expect("NULL exit pointer ok",   ui_hold_progress(true, 1750, 1000, 1500, NULL), 500);
        expect("hold_ms 0 exits at once", ui_hold_progress(true, 7, 7, 0, &ex), 1000);
        expect("hold_ms 0: exit",        ex, 1);
        expect("the constant is 1.5 s",  UI_HOLD_EXIT_MS, 1500);
    }

    printf("group 9  hold to exit: evdev values 1 / 2 / 0\n");
    {
        UiHold h = { false, 0 };
        ui_hold_key(&h, 2, 100);
        expect("a lone repeat starts nothing", h.down, 0);
        ui_hold_key(&h, 1, 1000);
        expect("press starts the hold",        h.down, 1);
        expect("press records its time",       (int)h.start_ms, 1000);
        ui_hold_key(&h, 2, 1300);
        ui_hold_key(&h, 2, 1333);
        expect("repeats keep it down",         h.down, 1);
        expect("repeats do not restart it",    (int)h.start_ms, 1000);
        ui_hold_key(&h, 1, 1400);
        expect("a second press keeps the first start", (int)h.start_ms, 1000);
        ui_hold_key(&h, 0, 1450);
        expect("release ends the hold",        h.down, 0);
        expect("released: progress 0",
               ui_hold_progress(h.down, 9000, h.start_ms, UI_HOLD_EXIT_MS, NULL), 0);
        ui_hold_key(&h, 2, 1500);
        expect("a repeat after release is no press", h.down, 0);
        ui_hold_key(&h, 1, 2000);
        expect("a new press starts afresh",    (int)h.start_ms, 2000);
        /* A short press: down then up well inside 1.5 s never exits. */
        bool ex = true;
        ui_hold_progress(h.down, 2200, h.start_ms, UI_HOLD_EXIT_MS, &ex);
        expect("short press mid-hold: no exit", ex, 0);
        ui_hold_key(&h, 0, 2200);
        ui_hold_progress(h.down, 4000, h.start_ms, UI_HOLD_EXIT_MS, &ex);
        expect("short press after release: no exit", ex, 0);
    }

    printf("group 10 mouse chord: LEFT+RIGHT together, either release resets\n");
    {
        UiChord c = { false, false, { false, 0 } };
        ui_chord_button(&c, false, 1, 100);
        expect("left alone: shown",             c.left, 1);
        expect("left alone: no hold",           c.hold.down, 0);
        ui_chord_button(&c, false, 2, 150);
        expect("left repeat: still no hold",    c.hold.down, 0);
        bool ex = true;
        ui_hold_progress(c.hold.down, 9000, c.hold.start_ms, UI_HOLD_EXIT_MS, &ex);
        expect("left held long: no exit",       ex, 0);
        ui_chord_button(&c, true, 1, 1000);
        expect("right joins: hold starts",      c.hold.down, 1);
        expect("starts when BOTH are down",     (int)c.hold.start_ms, 1000);
        ui_chord_button(&c, true, 2, 1200);
        ui_chord_button(&c, false, 1, 1300);
        expect("repeat / second press: same start", (int)c.hold.start_ms, 1000);
        ui_chord_button(&c, false, 0, 1400);
        expect("left released: hold ends",      c.hold.down, 0);
        expect("right still shown",             c.right, 1);
        ui_chord_button(&c, false, 2, 1450);
        expect("a stray 2 is no press",         c.left, 0);
        expect("a stray 2 starts nothing",      c.hold.down, 0);
        ui_chord_button(&c, false, 1, 2000);
        expect("left back: hold restarts",      (int)c.hold.start_ms, 2000);
        ui_hold_progress(c.hold.down, 3500, c.hold.start_ms, UI_HOLD_EXIT_MS, &ex);
        expect("both held 1.5 s: exit",         ex, 1);
        ui_chord_button(&c, true, 0, 3000);
        expect("right released: hold ends",     c.hold.down, 0);
        UiChord r = { false, false, { false, 0 } };
        ui_chord_button(&r, true, 1, 50);
        expect("right alone: shown",            r.right, 1);
        expect("right alone: no hold",          r.hold.down, 0);
    }

    printf("group 11 two triggers, one timer\n");
    {
        UiHold off = { false, 0 }, k = { true, 1000 }, m = { true, 1600 };
        UiHold e = ui_hold_either(&off, &off);
        expect("neither: up",                   e.down, 0);
        e = ui_hold_either(&k, &off);
        expect("key only: down",                e.down, 1);
        expect("key only: its start",           (int)e.start_ms, 1000);
        e = ui_hold_either(&off, &m);
        expect("chord only: down",              e.down, 1);
        expect("chord only: its start",         (int)e.start_ms, 1600);
        e = ui_hold_either(&k, &m);
        expect("both: the earlier start",       (int)e.start_ms, 1000);
        e = ui_hold_either(&m, &k);
        expect("both, swapped: the earlier",    (int)e.start_ms, 1000);
        UiHold w1 = { true, 0xFFFFFF00u }, w2 = { true, 0x40u };
        e = ui_hold_either(&w2, &w1);
        expect("across the wrap: the earlier",  e.start_ms == 0xFFFFFF00u, 1);
    }

    printf("\n%s (%d failure%s)\n", fails ? "REGRESSION" : "ALL PASS",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
