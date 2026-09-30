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

    /* Optional.  The control panel's RESET DEFAULTS calls it on every page,
     * AFTER config_clear(cfg). */
    void (*reset_defaults)(Config *cfg);
} CpPage;

extern const CpPage cp_led_page;       /* led_page.c */
extern const CpPage cp_monitor_page;   /* monitor_page.c */
extern const CpPage cp_info_page;      /* info_page.c */
extern const CpPage cp_network_page;   /* network_page.c */

#endif
