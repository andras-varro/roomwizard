#ifndef VNC_SETTINGS_H
#define VNC_SETTINGS_H

#include "config.h"
#include "../native_apps/common/framebuffer.h"
#include "../native_apps/common/touch_input.h"

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

#endif /* VNC_SETTINGS_H */
