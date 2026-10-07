/* Host regression for common/ui_flow.c.
 *
 *   cd native_apps && gcc -Wall -Wextra -I common -o build/ui_flow_test \
 *       tests/ui_flow_test.c common/ui_flow.c && ./build/ui_flow_test
 *
 * New file, so "failing before" = no such symbol.  Negative control: the old
 * rule strlen*6*scale > bw-8 is evaluated alongside and must DISAGREE with
 * ui_label_fits for the 14-char scale-1 label at bw 91 (ink 83 + 8 = 91). */
#include <stdio.h>
#include <string.h>
#include "ui_flow.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static int old_fits(const char *l, int s, int bw) { return !((int)strlen(l) * 6 * s > bw - 8); }

int main(void) {
    UiRect r[8];
    const char *rg = "RESET GEOMETRY";            /* 14 chars */

    CHECK(ui_label_ink_width(rg, 1) == 83, "ink %d", ui_label_ink_width(rg, 1));
    CHECK(ui_label_ink_width(rg, 2) == 166, "ink2");
    CHECK(ui_label_fits(rg, 1, 91) == 1, "fits at 91");
    CHECK(ui_label_fits(rg, 1, 90) == 0, "not at 90");
    CHECK(ui_label_fits(rg, 2, 174) == 1 && ui_label_fits(rg, 2, 173) == 0, "scale 2 edge");
    CHECK(ui_label_fits("", 2, 0) == 1 && ui_label_fits(NULL, 2, 0) == 1, "empty");
    /* negative control: old rule wrongly rejects at 91 */
    CHECK(old_fits(rg, 1, 91) == 0, "control: old rule should reject 91");
    CHECK(old_fits(rg, 1, 91) != ui_label_fits(rg, 1, 91), "control disagree");

    /* n=0 / bad out */
    CHECK(ui_flow_place(0, 10, 5, 2, 2, 0, 0, 100, 0, 0, r) == 0, "n=0");
    CHECK(ui_flow_place(3, 10, 5, 2, 2, 0, 0, 100, 0, 0, NULL) == 0, "null out");

    /* exact fit: 3*100+2*10 = 320 -> 3 cols, 1 row */
    CHECK(ui_flow_place(3, 100, 30, 10, 8, 20, 50, 320, 0, 0, r) == 1, "exact 1 row");
    CHECK(r[2].x == 20 + 220 && r[2].y == 50 && r[2].w == 100 && r[2].h == 30, "exact r2");
    /* overflow by 1px: 319 -> 2 cols, 2 rows */
    CHECK(ui_flow_place(3, 100, 30, 10, 8, 20, 50, 319, 0, 0, r) == 2, "319 wraps");
    CHECK(r[2].x == 20 && r[2].y == 50 + 38, "wrapped item row 2: %d,%d", r[2].x, r[2].y);
    /* max_cols cap */
    CHECK(ui_flow_place(5, 40, 10, 0, 0, 0, 0, 1000, 3, 0, r) == 2, "cap 3 -> 2 rows");
    CHECK(r[3].x == 0 && r[3].y == 10, "cap wrap");
    /* avail smaller than one item: one column, rows == n, left at x0 */
    CHECK(ui_flow_place(3, 100, 10, 5, 5, 7, 0, 50, 0, 1, r) == 3, "narrow rows");
    CHECK(r[1].x == 7 && r[1].y == 15, "narrow pos");
    /* centred: row of 2 in 320: block 210, off 55 */
    CHECK(ui_flow_place(2, 100, 10, 10, 0, 0, 0, 320, 0, 1, r) == 1 && r[0].x == 55 && r[1].x == 165, "centred");
    /* centred partial last row: 3 in 2-col 230 -> last alone off 65 */
    CHECK(ui_flow_place(3, 100, 10, 10, 0, 0, 0, 230, 0, 1, r) == 2 && r[2].x == 65, "centred last");

    printf("%s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
