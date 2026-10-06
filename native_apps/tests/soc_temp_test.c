/* Host-side regression for the Monitor page's SoC temperature row
 * (control_panel/soc_temp.c).
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/soc_temp_test \
 *       tests/soc_temp_test.c control_panel/soc_temp.c && ./build/soc_temp_test
 *
 * What it asserts, and why:
 *   A  the shape .188 gives under ti-soc-thermal ("73000\n", 2026-10-05) parses
 *      to 73000 millidegrees, and a negative reading keeps its sign;
 *   B  anything that is not one integer refuses, so the row hides rather than
 *      showing a wrong figure: empty text, a read error's leftovers, a unit
 *      suffix, a decimal point, two numbers;
 *   C  a value outside SOC_TEMP_MIN_MC..SOC_TEMP_MAX_MC refuses, including one
 *      long enough to overflow a 32-bit long if accumulated blindly;
 *   D  formatting rounds to the nearest whole degree, half away from zero,
 *      on both sides of zero.
 */
#include "control_panel/soc_temp.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int fmt_is(int mc, const char *want) {
    char b[32];
    soc_temp_format(mc, b, sizeof(b));
    if (strcmp(b, want) != 0) { printf("  format(%d) = \"%s\", want \"%s\"\n", mc, b, want); return 0; }
    return 1;
}

int main(void) {
    int mc = 12345;

    /* A */
    CHECK(soc_temp_parse("73000\n", &mc) == 0 && mc == 73000, "A: .188's shape, got %d", mc);
    CHECK(soc_temp_parse("-4000", &mc) == 0 && mc == -4000, "A: negative, got %d", mc);
    CHECK(soc_temp_parse("0", &mc) == 0 && mc == 0, "A: zero, got %d", mc);

    /* B */
    mc = 777;
    CHECK(soc_temp_parse("", &mc) == -1, "B: empty");
    CHECK(soc_temp_parse("\n", &mc) == -1, "B: newline only");
    CHECK(soc_temp_parse(NULL, &mc) == -1, "B: NULL");
    CHECK(soc_temp_parse("-", &mc) == -1, "B: bare minus");
    CHECK(soc_temp_parse("73000 mC", &mc) == -1, "B: unit suffix");
    CHECK(soc_temp_parse("73.5", &mc) == -1, "B: decimal point");
    CHECK(soc_temp_parse("73000 74000", &mc) == -1, "B: two numbers");
    CHECK(soc_temp_parse(" 73000", &mc) == -1, "B: leading space");
    CHECK(mc == 777, "B: a refused parse must not write *mc, got %d", mc);

    /* C */
    CHECK(soc_temp_parse("200000", &mc) == 0, "C: upper bound accepted");
    CHECK(soc_temp_parse("200001", &mc) == -1, "C: past upper bound");
    CHECK(soc_temp_parse("-60000", &mc) == 0, "C: lower bound accepted");
    CHECK(soc_temp_parse("-60001", &mc) == -1, "C: past lower bound");
    CHECK(soc_temp_parse("99999999999999999999", &mc) == -1, "C: overflow-length digits");
    CHECK(soc_temp_parse("4294967369", &mc) == -1, "C: wraps to 73 in 32 bits");

    /* D */
    CHECK(fmt_is(73000, "73 C"), "D: whole");
    CHECK(fmt_is(72499, "72 C"), "D: below half");
    CHECK(fmt_is(72500, "73 C"), "D: half rounds up");
    CHECK(fmt_is(-2500, "-3 C"), "D: negative half away from zero");
    CHECK(fmt_is(-499, "0 C"), "D: small negative is zero");
    CHECK(fmt_is(125000, "125 C"), "D: table top");

    printf("soc_temp_test: %s (%d failure(s))\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
