/* blank_decide_test.c — host test for common/blank_decide.h.
 *
 *   cd native_apps && gcc -Wall -Wextra -I common -o build/blank_decide_test \
 *       tests/blank_decide_test.c && ./build/blank_decide_test
 */
#include <stdio.h>
#include "blank_decide.h"

static int fails;
#define EXPECT(name, got, want) do { if ((got) != (want)) { \
    printf("FAIL %s: got %d want %d\n", name, (int)(got), (int)(want)); fails++; } } while (0)

int main(void) {
    /* not yet idle long enough */
    EXPECT("short idle", blank_decide(59999, 1, false, false, true), BLANK_NONE);
    /* exactly at the limit */
    EXPECT("at limit", blank_decide(60000, 1, false, false, true), BLANK_DO_BLANK);
    EXPECT("ten min", blank_decide(600000, 10, false, false, true), BLANK_DO_BLANK);
    EXPECT("ten min short", blank_decide(599999, 10, false, false, true), BLANK_NONE);
    /* never */
    EXPECT("never", blank_decide(99999999LL, 0, false, false, true), BLANK_NONE);
    EXPECT("negative", blank_decide(99999999LL, -3, false, false, true), BLANK_NONE);
    /* input while awake never blanks, however old the idle reading */
    EXPECT("event awake", blank_decide(999999, 1, false, true, true), BLANK_NONE);
    /* blanked: the waking input is swallowed; silence stays dark */
    EXPECT("wake swallow", blank_decide(0, 1, true, true, true), BLANK_WAKE_SWALLOW);
    EXPECT("stay dark", blank_decide(999999, 1, true, false, true), BLANK_NONE);
    /* blanked with the setting changed to never: still wakes only on input */
    EXPECT("never stays dark", blank_decide(0, 0, true, false, true), BLANK_NONE);
    EXPECT("never wakes on input", blank_decide(0, 0, true, true, true), BLANK_WAKE_SWALLOW);
    /* opted-out process: never blanks, restores a dark panel, swallows nothing */
    EXPECT("off idle", blank_decide(99999999LL, 1, false, false, false), BLANK_NONE);
    EXPECT("off event", blank_decide(0, 1, false, true, false), BLANK_NONE);
    EXPECT("off restores", blank_decide(0, 1, true, true, false), BLANK_DO_WAKE);
    /* 24 h does not overflow */
    EXPECT("big", blank_decide(86400000LL, 1440, false, false, true), BLANK_DO_BLANK);
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("blank_decide_test: all passed\n");
    return 0;
}
