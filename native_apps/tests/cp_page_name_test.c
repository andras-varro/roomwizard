/* Host regression for control_panel's `<page>` argument lookup.
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/cp_page_name_test \
 *       tests/cp_page_name_test.c control_panel/cp_page_name.c && ./build/cp_page_name_test
 *
 * cp_page_find() is a new function, so "failing before the change" means the
 * file does not compile or link against the pre-change tree (no such symbol).
 * To see the assertions fail, compile against a stub that returns -1. */
#include <stdio.h>
#include "control_panel/cp_page_name.h"

static const char *const N[] = { "Audio", "Display", "LED", "USB", "Bluetooth",
                                 "Input", "Network", "Monitor", "Information" };
#define NN ((int)(sizeof(N) / sizeof(N[0])))
static int fails;
#define EXPECT(arg, want) do { int g = cp_page_find(N, NN, arg); \
    if (g != (want)) { printf("FAIL %s: got %d want %d\n", arg ? arg : "NULL", g, want); fails++; } } while (0)

int main(void) {
    EXPECT("monitor", 7);
    EXPECT("MONITOR", 7);
    EXPECT("usb", 3);
    EXPECT("led", 2);
    EXPECT("info", 8);        /* unique prefix */
    EXPECT("information", 8);
    EXPECT("bl", 4);
    EXPECT("in", -1);         /* Input / Information: ambiguous */
    EXPECT("nope", -1);
    EXPECT("monitors", -1);   /* longer than the name */
    EXPECT("", -1);
    EXPECT(NULL, -1);
    if (cp_page_find(NULL, 0, "x") != -1) { printf("FAIL null table\n"); fails++; }
    printf("%s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
