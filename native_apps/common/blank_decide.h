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

/* Does one EV_ABS reading count as input?  A hat (range -1..1, pass dz_pct 0)
 * counts on any deflection and not on its return to 0; a stick counts only
 * once it is outside the dead zone around `center` (dz_pct percent of the
 * half-range), so rest jitter never holds the panel awake.  Same arithmetic as
 * gamepad.c's normalize_axis_calibrated(), which is what sets the pad's dead
 * zone. */
static inline bool blank_abs_is_input(int value, int min_val, int max_val,
                                      int center, int dz_pct) {
    int half = (max_val - min_val) / 2;
    if (half <= 0) return false;
    int off = value - center;
    if (off == 0) return false;
    int dz = (half * dz_pct) / 100;
    return !(off > -dz && off < dz);
}

#endif
