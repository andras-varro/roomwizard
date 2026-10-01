/* cp_page.h — the one interface a control_panel page implements.
 *
 * A page is a compiled-in applet: one module, one const CpPage, one row in
 * control_panel.c's home grid naming it.  control_panel.c holds no per-page
 * code — it opens a page from its tile, draws its title bar (BACK and the
 * page's name), and dispatches layout, draw, input and full-screen runs
 * through this struct.  The page owns its state, its widgets and its layout,
 * in the content rectangle cp_ui.h defines, drawn with cp_ui.h's helpers.
 *
 * Adding a page: a new .c exposing a const CpPage, its extern below, a row in
 * home_items[], and the file on control_panel's line in build-and-deploy.sh.
 */
#ifndef CP_PAGE_H
#define CP_PAGE_H

#include "../common/framebuffer.h"
#include "../common/touch_input.h"
#include "../common/config.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    CP_PAGE_IDLE,        /* nothing visible changed */
    CP_PAGE_REDRAW,      /* something visible changed: repaint the page */
    CP_PAGE_FULLSCREEN   /* input() queued a full-screen run: call run_fullscreen */
} CpPageResult;

typedef struct CpPage {
    const char *name;   /* home tile label AND page title — one name */
    const char *icon;   /* basename under /opt/roomwizard/icons/, no .ppm; NULL = letter tile */

    /* Once at startup, from the control panel's loaded Config. */
    void (*load)(const Config *cfg);

    /* Lay out from CONTENT_* / SCREEN_SAFE_* and print the page's
     * "control_panel: <page> stack ..." receipt.  Called from rebuild_ui(),
     * i.e. again whenever the logical screen changes. */
    void (*layout)(void);

    void (*enter)(void);                      /* optional (NULL) */
    void (*leave)(void);                      /* optional (NULL) */

    void (*draw)(Framebuffer *fb);

    /* cfg is the control panel's in-memory Config; a page that writes keys
     * keeps it in step, so a later whole-file save elsewhere cannot put back a
     * value the page replaced. */
    CpPageResult (*input)(Config *cfg, int tx, int ty, bool touching, uint32_t now);

    /* Optional.  Blocking, full-screen: runs whatever input() queued and cleans
     * up its own hardware.  The caller only drains touch and repaints after. */
    void (*run_fullscreen)(Framebuffer *fb, TouchInput *touch);

    /* Optional.  cp_reset_all_defaults() (below) calls it on every page,
     * AFTER config_clear(cfg). */
    void (*reset_defaults)(Config *cfg);

    /* Optional.  Asked once per main-loop iteration while the page is open:
     * true while it holds something input() must service at the active frame
     * rate (an open audio stream, which FRAME_DELAY_IDLE_US would starve), and
     * the loop then sleeps FRAME_DELAY_ACTIVE_US.  It reports live state —
     * never a constant true, which would spin a static page at full rate. */
    bool (*busy)(void);
} CpPage;

extern const CpPage cp_led_page;       /* led_page.c */
extern const CpPage cp_monitor_page;   /* monitor_page.c */
extern const CpPage cp_info_page;      /* info_page.c */
extern const CpPage cp_network_page;   /* network_page.c */
extern const CpPage cp_usb_page;       /* usb_page.c */
extern const CpPage cp_input_page;     /* input_page.c */
extern const CpPage cp_audio_page;     /* audio_page.c */
extern const CpPage cp_display_page;   /* display_page.c */

/* Implemented in control_panel.c, for pages. */

/* Posts msg on the page's status line — it takes the title bar's place for a
 * few seconds, green when ok, orange when not.  The panel repaints for it and
 * once more when it expires; the page need not return CP_PAGE_REDRAW for it. */
void cp_status(const char *msg, bool ok);

/* Asks before doing something: the panel's one confirmation dialog (the Touch
 * tab's RESET GEOMETRY uses it too), ok_text and CANCEL side by side, drawn
 * over the whole screen.  While it is up it takes all input — the title bar's
 * BACK and the page's input() included.  OK calls on_ok with the panel's
 * Config and repaints the page; CANCEL does nothing.  Returns at once: call it
 * from input() and return CP_PAGE_IDLE, the dialog appearing repaints.
 * Common's ModalDialog fixes the box at 420x200 px, which fits the 421 px
 * portrait width; title is scale 3 (at most 22 characters to fit), message
 * scale 2 (at most 33 a line), and a '\n' in message starts a second line. */
typedef void (*CpConfirmFn)(Config *cfg);
void cp_confirm(const char *title, const char *message, const char *ok_text,
                CpConfirmFn on_ok);

/* The global RESET DEFAULTS, pressed on the Information page.  First copies
 * the config file to a timestamped "<path>.bak-YYYYmmdd-HHMMSS" beside it; if
 * that copy fails nothing is reset and -1 comes back.  Otherwise it clears
 * cfg, calls every page's reset_defaults (the Display page's re-applies the
 * backlight), saves the cleared file and returns 0.  Either way msg (len bytes) gets
 * the line to show: "BACKUP: <path>", a note when there was no file to copy
 * (nothing to lose, so it resets), or "RESET FAILED: BACKUP <reason>".
 * /etc/touch_calibration.conf is not touched. */
int cp_reset_all_defaults(Config *cfg, char *msg, size_t len);

#endif
