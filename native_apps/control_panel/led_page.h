/* led_page.h — control_panel's LED page: enable, brightness, and the LED tests.
 *
 * Opened from the home grid's LED tile, and the one home for everything about
 * the two indicator LEDs; Settings and the Tests tab no longer carry any of it.
 *
 * ⚠️ There is no SAVE button, on purpose.  The toggle and -/+ write the config
 * file the moment they change and reload common/hardware.c's cache, because the
 * tests on the same screen drive the LEDs through hw_set_led(), which is gated
 * and scaled by the SAVED values — a page whose tests ignored its own switches
 * until a SAVE would contradict itself on screen.
 *
 * The page owns its widgets and layout; the caller owns the state, passes it in
 * explicitly, and runs a test full-screen when led_page_input() asks for one.
 */
#ifndef LED_PAGE_H
#define LED_PAGE_H

#include "../common/framebuffer.h"
#include "../common/touch_input.h"
#include "../common/config.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool enabled;       /* config key led_enabled */
    int  brightness;    /* config key led_brightness, 0..100 */
} LedPageState;

typedef enum {
    LED_PAGE_NONE,      /* nothing happened */
    LED_PAGE_CHANGED,   /* enable or brightness changed, and is already saved */
    LED_PAGE_RUN_TEST   /* *test holds the test to run full-screen */
} LedPageAction;

/* Read both values from cfg, through config.c's helpers, so the default shown
 * here is the one hardware.c resolves on a file without the keys. */
void led_page_load(LedPageState *s, const Config *cfg);

/* Lay out from CONTENT_* / SCREEN_SAFE_* and print the "led stack" receipt.
 * Re-run whenever the logical screen changes (rebuild_ui()). */
void led_page_create(void);

void led_page_draw(Framebuffer *fb, const LedPageState *s);

/* cfg is the caller's in-memory Config: it is kept in step with what is
 * written, so a later whole-file save from another page cannot put back the
 * value this page replaced. */
LedPageAction led_page_input(LedPageState *s, Config *cfg, int tx, int ty,
                             bool touching, uint32_t now, int *test);

/* Blocking, full-screen; the caller turns the LEDs off and drains touch after. */
void led_page_run_test(int test, Framebuffer *fb, TouchInput *touch);

/* For Settings' RESET DEFAULTS, called AFTER config_clear(cfg): removes the two
 * keys from the file as well, reloads hardware.c's cache, and reads s back from
 * the cleared cfg — so this page, the file and the LEDs agree on the default. */
void led_page_reset_defaults(LedPageState *s, const Config *cfg);

#endif
