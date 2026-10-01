/* touch_wizard.h — control_panel's touch calibration wizard (full screen).
 *
 * The one wizard that measures the screen geometry: the interior fit that
 * becomes line 1 of /etc/touch_calibration.conf, the edge sweep, and the
 * bezel against 2 px ladders.  EDGES runs the last two only.  It knows
 * nothing of control_panel's state: it takes the screen, the touch device and
 * the process's running flag, and hands back the message for the caller to
 * post.  cp_run_touch_tool() in control_panel.c is the caller, and it owns
 * posting that message and laying the UI out again afterwards — the logical
 * screen may have changed size under it.
 */
#ifndef TOUCH_WIZARD_H
#define TOUCH_WIZARD_H

#include "../common/framebuffer.h"
#include "../common/touch_input.h"
#include <stdbool.h>

typedef struct {
    char msg[64];   /* "" = nothing to post */
    bool saved;     /* the config was written: post msg in the success colour */
} TouchWizardResult;

/* Runs to completion, owning the screen and the touch device.  Returns early
 * once *running goes false (SIGINT/SIGTERM).  Always fills *out. */
void touch_wizard_run(Framebuffer *fb, TouchInput *touch, bool edges_only,
                      const volatile bool *running, TouchWizardResult *out);

#endif
