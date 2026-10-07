#ifndef VNC_SETTINGS_H
#define VNC_SETTINGS_H

#include "config.h"
#include "../native_apps/common/framebuffer.h"
#include "../native_apps/common/touch_input.h"
#include "../native_apps/common/ui_focus.h"

/* Return values from vnc_settings_run() */
#define SETTINGS_BACK   0   /* User pressed Back — resume VNC session */
#define SETTINGS_SAVE   1   /* User pressed Save & Reconnect — config updated, reconnect */
#define SETTINGS_EXIT   2   /* User pressed Exit to Launcher */

/*
 * Run the settings GUI screen.
 * Blocks until the user makes a choice.
 *
 * @param config      Pointer to the live VNCConfig (modified in-place on SAVE)
 * @param fb          Framebuffer for drawing
 * @param touch       Touch input device (may be NULL if touch unavailable)
 * @param config_path Path to the config file to save to
 * @return            SETTINGS_BACK, SETTINGS_SAVE, or SETTINGS_EXIT
 */
int vnc_settings_run(VNCConfig *config, Framebuffer *fb, TouchInput *touch,
                     const char *config_path);

/* The focus ring (3 px cyan outline just inside f) and the mouse arrow (hot
 * spot at its tip, x,y), drawn into the RGB565 back buffer.  Settings and the
 * reconnect screen share them so the two screens look the same. */
void vnc_draw_focus_ring(Framebuffer *fb, const UiRect *f);
void vnc_draw_pointer(Framebuffer *fb, int x, int y);

#endif /* VNC_SETTINGS_H */
