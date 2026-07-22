#include "touch_input.h"
#include "framebuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

// Default calibration file path
#define TOUCH_CALIB_FILE "/etc/touch_calibration.conf"

// Map raw touch coordinates to screen coordinates.
//
// Single per-axis LINEAR map from the raw range to the FULL physical screen:
//   screen = (raw - raw_min) * (dim-1) / (raw_max - raw_min)
// then, in portrait mode, rotate physical→virtual and clamp.
//
// This is the whole model. No affine, no bilinear corner offsets, no bezel
// remap — the panel is linear (a traced border is a straight-edged rectangle),
// and the raw range already spans the full screen, so scale+offset per axis is
// accurate everywhere and reaches every edge. Applying bezel margins here would
// double-correct an already-correct coordinate; bezel is a drawing concern only.
static void scale_coordinates(TouchInput *touch, int *x, int *y) {
    // Hardware panel is always 800×480; in portrait the app-visible dims are
    // swapped. Map into physical space first, then rotate to virtual at the end.
    int phys_w, phys_h;
    if (touch->portrait_mode) {
        phys_w = touch->screen_height;  // physical width  = virtual height (800)
        phys_h = touch->screen_width;   // physical height = virtual width  (480)
    } else {
        phys_w = touch->screen_width;
        phys_h = touch->screen_height;
    }

    int range_x = touch->raw_max_x - touch->raw_min_x;
    int range_y = touch->raw_max_y - touch->raw_min_y;
    if (range_x <= 0) { touch->raw_min_x = 0; range_x = 4095; }
    if (range_y <= 0) { touch->raw_min_y = 0; range_y = 4095; }

    // Clamp raw into the calibrated range so a touch slightly past the sampled
    // edge still lands exactly on the screen border rather than overshooting.
    long rx = (long)*x - touch->raw_min_x;
    long ry = (long)*y - touch->raw_min_y;
    if (rx < 0) rx = 0; if (rx > range_x) rx = range_x;
    if (ry < 0) ry = 0; if (ry > range_y) ry = range_y;

    int mx = (int)(rx * (phys_w - 1) / range_x);
    int my = (int)(ry * (phys_h - 1) / range_y);

    // Portrait mode: rotate physical coords → virtual coords (90° CCW)
    if (touch->portrait_mode) {
        int px = mx, py = my;
        mx = phys_h - 1 - py;
        my = px;
    }

    // Clamp to virtual screen bounds
    if (mx < 0) mx = 0;
    if (mx >= touch->screen_width)  mx = touch->screen_width - 1;
    if (my < 0) my = 0;
    if (my >= touch->screen_height) my = touch->screen_height - 1;

    *x = mx;
    *y = my;
}

int touch_init(TouchInput *touch, const char *device) {
    touch->fd = open(device, O_RDONLY | O_NONBLOCK);
    if (touch->fd == -1) {
        perror("Error opening touch device");
        printf("ERROR: Failed to open %s, fd=%d\n", device, touch->fd);
        return -1;
    }

    printf("Touch device opened successfully: %s (fd=%d)\n", device, touch->fd);

    touch->state.x = 0;
    touch->state.y = 0;
    touch->state.pressed = false;
    touch->state.released = false;
    touch->state.held = false;
    touch->last_x = 0;
    touch->last_y = 0;
    touch->touching = false;

    // Query the hardware coordinate range via EVIOCGABS. This is the default
    // raw→screen mapping (works out of the box on this panel); calibration
    // overrides it with a measured edge-drag range.
    struct input_absinfo abs_x, abs_y;
    if (ioctl(touch->fd, EVIOCGABS(ABS_X), &abs_x) == 0 &&
        ioctl(touch->fd, EVIOCGABS(ABS_Y), &abs_y) == 0) {
        touch->raw_min_x = abs_x.minimum;
        touch->raw_max_x = abs_x.maximum;
        touch->raw_min_y = abs_y.minimum;
        touch->raw_max_y = abs_y.maximum;
        printf("Touch hardware range: X [%d..%d], Y [%d..%d]\n",
               abs_x.minimum, abs_x.maximum, abs_y.minimum, abs_y.maximum);
    } else {
        touch->raw_min_x = 0; touch->raw_max_x = 4095;
        touch->raw_min_y = 0; touch->raw_max_y = 4095;
        printf("Touch EVIOCGABS failed, using default 0-4095 range\n");
    }
    touch->screen_width  = screen_base_width;   // 800×480 default (fb globals)
    touch->screen_height = screen_base_height;
    touch->calibrated = false;

    touch->calib.enabled = false;
    touch->calib.bezel_top = 0;
    touch->calib.bezel_bottom = 0;
    touch->calib.bezel_left = 0;
    touch->calib.bezel_right = 0;

    // Detect portrait mode
    touch->portrait_mode = (access("/opt/games/portrait.mode", F_OK) == 0);

    // Auto-load calibration so all apps get the measured range (if calibrated).
    if (touch_load_calibration(touch, TOUCH_CALIB_FILE) == 0) {
        touch_enable_calibration(touch, true);
        printf("Touch auto-loaded calibration from %s\n", TOUCH_CALIB_FILE);
    }

    printf("Touch input initialized: %s\n", device);
    return 0;
}

void touch_set_screen_size(TouchInput *touch, int width, int height) {
    touch->screen_width = width;
    touch->screen_height = height;
    printf("Touch screen size set to: %dx%d\n", width, height);
}

void touch_close(TouchInput *touch) {
    if (touch->fd != -1) {
        close(touch->fd);
    }
}

int touch_wait_for_press_raw(TouchInput *touch, int *raw_x, int *raw_y) {
    // Wait for a touch press event (blocking), returning RAW hardware coords.
    //
    // CRITICAL: Linux input events arrive in this order:
    //   1. ABS_X, ABS_Y (coordinates)
    //   2. BTN_TOUCH (press/release)
    //   3. SYN_REPORT (frame complete)
    // We must capture coordinates BEFORE checking for press.

    struct input_event ev;
    int current_x = -1, current_y = -1;
    bool got_press = false;

    int flags = fcntl(touch->fd, F_GETFL, 0);
    fcntl(touch->fd, F_SETFL, flags & ~O_NONBLOCK);

    while (1) {
        if (read(touch->fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X) current_x = ev.value;
                else if (ev.code == ABS_Y) current_y = ev.value;
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                if (ev.value == 0) {
                    touch->touching = false;
                    got_press = false;
                } else if (ev.value == 1 && !touch->touching) {
                    touch->touching = true;
                    got_press = true;
                }
            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                if (got_press && current_x >= 0 && current_y >= 0) {
                    *raw_x = current_x;
                    *raw_y = current_y;
                    fcntl(touch->fd, F_SETFL, flags);
                    return 0;
                }
                got_press = false;
            }
        }
    }

    fcntl(touch->fd, F_SETFL, flags);
    return -1;
}

int touch_wait_for_press(TouchInput *touch, int *x, int *y) {
    // Blocking press returning SCALED/calibrated screen coordinates.
    if (touch_wait_for_press_raw(touch, x, y) < 0)
        return -1;
    scale_coordinates(touch, x, y);
    return 0;
}

int touch_poll(TouchInput *touch) {
    // Non-blocking poll. Process events in arrival order:
    //   1. ABS_X, ABS_Y → last_x/last_y (RAW)
    //   2. BTN_TOUCH    → press/release (uses last_x/last_y)
    //   3. SYN_REPORT   → update calibrated state.x/y

    struct input_event ev;
    int events_read = 0;

    touch->state.pressed = false;
    touch->state.released = false;

    while (read(touch->fd, &ev, sizeof(ev)) == sizeof(ev)) {
        events_read++;

        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) touch->last_x = ev.value;
            else if (ev.code == ABS_Y) touch->last_y = ev.value;
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1 && !touch->touching) {
                touch->touching = true;
                touch->state.pressed = true;
                touch->state.held = true;
                int x = touch->last_x, y = touch->last_y;
                scale_coordinates(touch, &x, &y);
                touch->state.x = x;
                touch->state.y = y;
            } else if (ev.value == 0 && touch->touching) {
                touch->touching = false;
                touch->state.released = true;
                touch->state.held = false;
            }
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            if (touch->touching) {
                int x = touch->last_x, y = touch->last_y;
                scale_coordinates(touch, &x, &y);
                touch->state.x = x;
                touch->state.y = y;
            }
        }
    }

    return events_read;
}

TouchState touch_get_state(TouchInput *touch) {
    return touch->state;
}

void touch_set_raw_range(TouchInput *touch,
                         int min_x, int max_x, int min_y, int max_y) {
    // Guard against degenerate ranges (would divide badly / invert axes).
    if (max_x - min_x < 16) { min_x = 0; max_x = 4095; }
    if (max_y - min_y < 16) { min_y = 0; max_y = 4095; }
    touch->raw_min_x = min_x; touch->raw_max_x = max_x;
    touch->raw_min_y = min_y; touch->raw_max_y = max_y;
    touch->calib.enabled = true;
    touch->calibrated = true;
    printf("Touch raw range set: X [%d..%d] Y [%d..%d]\n",
           min_x, max_x, min_y, max_y);
}

void touch_enable_calibration(TouchInput *touch, bool enable) {
    touch->calib.enabled = enable;
    printf("Touch calibration %s\n", enable ? "enabled" : "disabled");
}

int touch_fit_axis_range(const int *raw, const int *scr, int n, int dim,
                         int *raw_at_0, int *raw_at_max) {
    if (n < 2) return -1;
    double Sr = 0, Ss = 0, Srr = 0, Srs = 0;
    for (int i = 0; i < n; i++) {
        double r = raw[i], s = scr[i];
        Sr += r; Ss += s; Srr += r * r; Srs += r * s;
    }
    double denom = (double)n * Srr - Sr * Sr;
    if (denom < 1e-6 && denom > -1e-6) return -1;   // collinear-in-raw / degenerate
    double m = ((double)n * Srs - Sr * Ss) / denom; // screen = m*raw + b
    if (m > -1e-9 && m < 1e-9) return -1;
    double b = (Ss - m * Sr) / (double)n;
    double r0 = (0.0 - b) / m;
    double r1 = ((double)(dim - 1) - b) / m;
    *raw_at_0   = (int)(r0 + (r0 >= 0 ? 0.5 : -0.5));
    *raw_at_max = (int)(r1 + (r1 >= 0 ? 0.5 : -0.5));
    return 0;
}

int touch_save_calibration(TouchInput *touch, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open calibration file for writing");
        return -1;
    }

    fprintf(f, "# RoomWizard touch calibration\n");
    fprintf(f, "# Line 1: raw range  min_x max_x min_y max_y  (raw touch mapped linearly to full screen)\n");
    fprintf(f, "%d %d %d %d\n",
            touch->raw_min_x, touch->raw_max_x,
            touch->raw_min_y, touch->raw_max_y);
    fprintf(f, "# Line 2: UI margins (drawing/layout only)  top bottom left right\n");
    fprintf(f, "%d %d %d %d\n",
            touch->calib.bezel_top, touch->calib.bezel_bottom,
            touch->calib.bezel_left, touch->calib.bezel_right);

    fclose(f);
    printf("Calibration saved to: %s  (range X[%d..%d] Y[%d..%d])\n", filename,
           touch->raw_min_x, touch->raw_max_x, touch->raw_min_y, touch->raw_max_y);
    return 0;
}

int touch_load_calibration(TouchInput *touch, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("No calibration file found: %s (using hardware range)\n", filename);
        return -1;
    }

    char line[256];
    bool got_range = false;
    bool got_margins = false;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        if (!got_range) {
            // Line 1: raw range (min_x max_x min_y max_y)
            int v[4];
            if (sscanf(line, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4) {
                // Reuse the guarded setter (rejects degenerate ranges).
                touch_set_raw_range(touch, v[0], v[1], v[2], v[3]);
                got_range = true;
                printf("Calibration loaded from: %s\n", filename);
            }
        } else if (!got_margins) {
            // Line 2: UI margins (top bottom left right)
            int m[4];
            if (sscanf(line, "%d %d %d %d", &m[0], &m[1], &m[2], &m[3]) == 4) {
                touch->calib.bezel_top    = m[0];
                touch->calib.bezel_bottom = m[1];
                touch->calib.bezel_left   = m[2];
                touch->calib.bezel_right  = m[3];
                got_margins = true;
                printf("  UI margins: T=%d B=%d L=%d R=%d\n", m[0], m[1], m[2], m[3]);
            }
        }
    }

    fclose(f);

    if (!got_range) {
        fprintf(stderr, "Failed to parse calibration file: %s\n", filename);
        return -1;
    }
    if (!got_margins)
        printf("  No UI margins found (using 0)\n");

    touch->calib.enabled = true;
    return 0;
}

void touch_drain_events(TouchInput *touch) {
    // Read and discard all pending events (call after reopening the device so
    // stale events don't trigger a spurious press in a menu).
    struct input_event ev;
    int flags = fcntl(touch->fd, F_GETFL, 0);
    fcntl(touch->fd, F_SETFL, flags | O_NONBLOCK);

    int drained = 0;
    while (read(touch->fd, &ev, sizeof(ev)) == sizeof(ev))
        drained++;

    fcntl(touch->fd, F_SETFL, flags);

    touch->touching = false;
    touch->state.pressed  = false;
    touch->state.released = false;
    touch->state.held     = false;

    if (drained > 0)
        printf("touch_drain_events: discarded %d stale events\n", drained);
}
