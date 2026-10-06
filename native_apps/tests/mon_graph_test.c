/* Host-side regression for the Monitor page's graph arithmetic
 * (control_panel/mon_graph.c).
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/mon_graph_test \
 *       tests/mon_graph_test.c control_panel/mon_graph.c && ./build/mon_graph_test
 *
 * What it asserts, and why:
 *   A  a value on the axis scales linearly to permille, and the two ends are
 *      exactly 0 and 1000 — the memory axis is 0..MemTotal, the temperature
 *      axis MON_TEMP_AXIS_LO_MC..MON_TEMP_AXIS_HI_MC;
 *   B  a value off either end clamps instead of drawing past the frame or
 *      going negative (a 30 C idle SoC, a 105 C one, used > total);
 *   C  an empty or inverted axis gives 0, so a missing MemTotal cannot divide
 *      by zero;
 *   D  a column is pm * ih / 1000 px, never taller than ih, never negative,
 *      and a non-zero sample keeps at least 1 px.
 */
#include "control_panel/mon_graph.h"

#include <stdio.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    int r;

    /* A */
    CHECK((r = mon_scale_pm(70000, MON_TEMP_AXIS_LO_MC, MON_TEMP_AXIS_HI_MC)) == 500, "A: 70 C mid-axis, got %d", r);
    CHECK((r = mon_scale_pm(MON_TEMP_AXIS_LO_MC, MON_TEMP_AXIS_LO_MC, MON_TEMP_AXIS_HI_MC)) == 0, "A: axis low end, got %d", r);
    CHECK((r = mon_scale_pm(MON_TEMP_AXIS_HI_MC, MON_TEMP_AXIS_LO_MC, MON_TEMP_AXIS_HI_MC)) == 1000, "A: axis high end, got %d", r);
    CHECK((r = mon_scale_pm(57000, 0, 228000)) == 250, "A: a quarter of MemTotal, got %d", r);

    /* B */
    CHECK((r = mon_scale_pm(30000, MON_TEMP_AXIS_LO_MC, MON_TEMP_AXIS_HI_MC)) == 0, "B: below axis, got %d", r);
    CHECK((r = mon_scale_pm(105000, MON_TEMP_AXIS_LO_MC, MON_TEMP_AXIS_HI_MC)) == 1000, "B: above axis, got %d", r);
    CHECK((r = mon_scale_pm(300000, 0, 228000)) == 1000, "B: used > total, got %d", r);
    CHECK((r = mon_scale_pm(-5, 0, 228000)) == 0, "B: negative, got %d", r);

    /* C */
    CHECK((r = mon_scale_pm(100, 0, 0)) == 0, "C: empty axis, got %d", r);
    CHECK((r = mon_scale_pm(100, 50, 10)) == 0, "C: inverted axis, got %d", r);

    /* D */
    CHECK((r = mon_bar_h(500, 94)) == 47, "D: half of 94, got %d", r);
    CHECK((r = mon_bar_h(1000, 94)) == 94, "D: full, got %d", r);
    CHECK((r = mon_bar_h(1500, 94)) == 94, "D: over-full clamps, got %d", r);
    CHECK((r = mon_bar_h(1, 94)) == 1, "D: tiny non-zero keeps 1 px, got %d", r);
    CHECK((r = mon_bar_h(0, 94)) == 0, "D: zero, got %d", r);
    CHECK((r = mon_bar_h(-20, 94)) == 0, "D: negative, got %d", r);
    CHECK((r = mon_bar_h(500, 0)) == 0, "D: no inner height, got %d", r);

    if (fails) { printf("mon_graph_test: %d FAILED\n", fails); return 1; }
    printf("mon_graph_test: all passed\n");
    return 0;
}
