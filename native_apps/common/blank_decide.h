/* blank_decide.h — the screen-blanking timer's pure decision.
 *
 * hardware.c keeps the idle clock and the backlight; this says only what to do
 * next.  Header-only and static inline, so hardware.c — which ScummVM and
 * vnc_client link too — gains no object for their build lists.  Pure: no clock,
 * no file; tests/blank_decide_test.c drives it.
 */
#ifndef BLANK_DECIDE_H
#define BLANK_DECIDE_H

#include <stdbool.h>

typedef enum {
    BLANK_NONE,         /* nothing to do */
    BLANK_DO_BLANK,     /* idle long enough: backlight to 0 */
    BLANK_DO_WAKE,      /* restore the backlight; the input (if any) is delivered */
    BLANK_WAKE_SWALLOW  /* restore the backlight AND drop the input that woke it */
} BlankAction;

/* Choices offered by the settings page; 0 means never. */
#define BLANK_DEFAULT_MINUTES 10

/* idle_ms: time since the last input.  minutes: configured delay, <= 0 never.
 * blanked: the panel is dark because of us.  event: a waking-class input is
 * being read right now.  enabled: this process takes part at all. */
static inline BlankAction blank_decide(long long idle_ms, int minutes,
                                       bool blanked, bool event, bool enabled) {
    if (!enabled)
        return blanked ? BLANK_DO_WAKE : BLANK_NONE;
    if (blanked)
        return event ? BLANK_WAKE_SWALLOW : BLANK_NONE;
    if (event || minutes <= 0)
        return BLANK_NONE;
    return idle_ms >= (long long)minutes * 60000LL ? BLANK_DO_BLANK : BLANK_NONE;
}

#endif
