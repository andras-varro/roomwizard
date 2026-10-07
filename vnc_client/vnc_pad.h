/*
 * vnc_pad — a game pad as the remote pointer, as pure arithmetic.
 *
 * vnc_input.c reads the pad nodes (input_scan() classifies them PAD) and
 * hands every event and every loop iteration in here; nothing in this file
 * opens a device or talks to the server, so a host test drives all of it
 * (native_apps/tests/vnc_pad_test.c).
 *
 *   - left stick and d-pad move the pointer: speed proportional to the
 *     deflection beyond the dead zone, integrated over elapsed milliseconds
 *     (not per frame), with the sub-pixel remainder carried between calls;
 *   - A is the left button and B the right, as held LEVELS, so a drag works;
 *   - LB / RB are one wheel click up / down per press;
 *   - Select held UI_HOLD_EXIT_MS is the same request as the exit-corner hold.
 *
 * Every code is a NATIVE one (common/input_scan.h, InputPadLayout): the caller
 * translates a raw EV_KEY code with input_pad_key() before feeding it.
 */
#ifndef VNC_PAD_H
#define VNC_PAD_H

#include <stdbool.h>
#include <stdint.h>
#include "../native_apps/common/input_scan.h"
#include "../native_apps/common/ui_focus.h"

/* The RFB pointer button bits (rfbButton1/3/4/5Mask).  Spelled out so this
 * file needs no libvncclient header; vnc_input.c asserts they agree. */
#define VNC_PAD_BTN_LEFT    1
#define VNC_PAD_BTN_RIGHT   4
#define VNC_PAD_WHEEL_UP    8
#define VNC_PAD_WHEEL_DOWN  16

/* A d-pad direction counts as this much stick deflection (of 1000): slower
 * than a full stick, for placing the pointer precisely. */
#define VNC_PAD_DPAD_NORM   400

/* A gap longer than this between two motion calls (a slow server message, a
 * stall) is integrated as this long, so the pointer never jumps. */
#define VNC_PAD_MAX_STEP_MS 100

/* Bounds on the full-deflection speed, in remote pixels per second.  The
 * upper one also keeps the carry arithmetic inside an int. */
#define VNC_PAD_MIN_SPEED   300
#define VNC_PAD_MAX_SPEED   4000

/* Which native codes mean what — copied once from /etc/input_config.conf. */
typedef struct {
    int left, right;            /* pointer buttons: A, B */
    int wheel_up, wheel_down;   /* LB, RB */
    int back;                   /* Select: the hold that opens Settings */
    int hat_x, hat_y;           /* d-pad axes */
    int stick_x, stick_y;       /* left stick axes */
    int deadzone_pct;           /* of the stick's half-range */
} VncPadMap;

/* One axis's range, as EVIOCGABS reports it. */
typedef struct { int min, max; } VncPadRange;

/* One pad node's state.  Zeroed is a valid, idle pad with no stick range. */
typedef struct {
    VncPadRange range_x, range_y;
    int      raw_x, raw_y;      /* last stick reading, raw units */
    int      hat_x, hat_y;      /* -1, 0, +1 */
    int      buttons;           /* VNC_PAD_BTN_* held */
    UiHold   back;              /* Select's hold */
    int      carry_x, carry_y;  /* sub-pixel remainder, in 1e-6 px */
    uint32_t last_ms;
    bool     have_time;
} VncPad;

/* PURE. The map from the shared parser's fields. */
void vnc_pad_map_from_config(VncPadMap *m, const InputConfig *cfg);

/* PURE. Reset a pad to idle with the given stick ranges, the stick resting at
 * raw (x, y).  Clears buttons, hold and carry. */
void vnc_pad_reset(VncPad *p, VncPadRange rx, VncPadRange ry, int x, int y);

/* PURE. A raw axis reading as -1000..+1000 around the range's midpoint, 0
 * inside the dead zone, with the dead zone taken out of the scale so motion
 * starts from zero at its edge.  0 for an empty range. */
int vnc_pad_axis(int raw, VncPadRange r, int deadzone_pct);

/* PURE. Feed one event (EV_KEY with a NATIVE code, or EV_ABS) read at now.
 * Returns a wheel bit to pulse (VNC_PAD_WHEEL_*) on a shoulder press, else 0.
 * A key value of 2 (autorepeat) changes nothing. */
int vnc_pad_event(VncPad *p, const VncPadMap *m, int type, int code, int value,
                  uint32_t now);

/* PURE. Whole pixels the pointer moves since the previous call, at full
 * deflection speed `speed` px/s (clamped to the bounds above).  The first call
 * only records the time.  A centred stick and d-pad drop the carry, so a rest
 * never leaks a pixel into the next move. */
void vnc_pad_motion(VncPad *p, const VncPadMap *m, uint32_t now, int speed,
                    int *dx, int *dy);

/* PURE. Progress of Select's hold in permille (0 when not held); true once it
 * has lasted UI_HOLD_EXIT_MS. */
bool vnc_pad_back_exit(const VncPad *p, uint32_t now, int *permille);

/* PURE. The full-deflection speed for a remote desktop `remote_w` pixels wide:
 * a full stick crosses it in about 1.5 s. */
int vnc_pad_speed_for_width(int remote_w);

/* ── Focus navigation (the Settings screen) ─────────────────────────────── */
/*
 * Outside a session a pad does not move a pointer: it moves a focus ring
 * (common/ui_focus.h).  The d-pad and the left stick are one step per push,
 * A activates, B goes back.  A USB keyboard's arrows, Enter/Space and Esc mean
 * the same.  Every action fires on a PRESS EDGE seen on this node since it
 * was opened, so a button already held when the screen opened — Select from
 * the hold that opened Settings, A from a tap that landed on its way in —
 * does nothing, not on release and not on a stray repeated press.
 */
typedef enum {
    VNC_NAV_NONE = 0,
    VNC_NAV_UP, VNC_NAV_DOWN, VNC_NAV_LEFT, VNC_NAV_RIGHT,
    VNC_NAV_ACTIVATE,
    VNC_NAV_BACK,
} VncNav;

/* The stick counts as pushed past this deflection (of 1000) and as back at
 * rest below the second; the gap keeps a stick resting near the threshold
 * from stepping twice. */
#define VNC_NAV_STICK_ON   500
#define VNC_NAV_STICK_OFF  250

/* One pad node's navigation state.  Zeroed is idle with no stick range. */
typedef struct {
    VncPadRange range_x, range_y;
    int  hat_x, hat_y;          /* -1, 0, +1: the d-pad as last reported */
    int  stick_x, stick_y;      /* -1, 0, +1: the stick's latched direction */
    bool a_down, b_down;        /* held, as this node's events last said */
} VncNavPad;

/* PURE. Idle, with the given stick ranges. */
void vnc_nav_pad_reset(VncNavPad *s, VncPadRange rx, VncPadRange ry);

/* PURE. Record a level read at open (EVIOCGKEY / EVIOCGABS) as if its event
 * had arrived, without acting on it.  A key seeded down needs a release
 * before its next press counts. */
void vnc_nav_pad_seed(VncNavPad *s, const VncPadMap *m, int type, int code,
                      int value);

/* PURE. One event (EV_KEY with a NATIVE code, or EV_ABS) as an action.  A key
 * acts on value 1 from up only; 0 releases it; 2 does nothing.  An axis acts
 * when it moves into a new non-zero direction. */
VncNav vnc_nav_pad_event(VncNavPad *s, const VncPadMap *m, int type, int code,
                         int value);

/* PURE. One USB keyboard EV_KEY as an action.  Arrows act on press and on
 * autorepeat; Enter, keypad Enter and Space activate and Esc goes back, on
 * press only — so a key held across the open never fires. */
VncNav vnc_nav_key(int code, int value);

/* PURE. The focus direction of a move action; false for any other action. */
bool vnc_nav_dir(VncNav a, UiDir *d);

/* ── Typing into the Settings keypads ───────────────────────────────────── */
/*
 * A physical keyboard may type exactly what the on-screen keypad of the same
 * mode can produce (vnc_settings.c): the mode values below equal its
 * KEYPAD_NUMERIC / KEYPAD_FULL / KEYPAD_ALPHA, in that order.
 *
 *   NUMERIC  0-9 (row or keypad), '.', ':' (shift+;)            host, port
 *   FULL     a-z, A-Z (shift), 0-9, '.', '-', '_' (shift+-), '!' (shift+1)
 *   ALPHA    a-z only (no shift key on that keypad), '_' '-' '.' ' '
 *            (no digit keys there)                              encodings
 *
 * Backspace, Enter / keypad Enter and Esc are mode-independent.
 */
typedef enum { VNC_KP_NUMERIC = 0, VNC_KP_FULL = 1, VNC_KP_ALPHA = 2 } VncKeypadMode;

typedef enum {
    VNC_KC_NONE = 0,    /* not a keypad key in this mode */
    VNC_KC_CHAR,        /* append *ch */
    VNC_KC_BACKSPACE,   /* the keypad's <- / DEL */
    VNC_KC_OK,          /* Enter, keypad Enter: the keypad's OK */
    VNC_KC_CANCEL       /* Esc: the keypad's CANCEL */
} VncKeyChar;

/* PURE. One USB keyboard key (evdev code, shift held) as a keypad edit; *ch is
 * set for VNC_KC_CHAR and 0 otherwise (ch may be NULL).  Stateless: the caller
 * passes only press (value 1) and, for CHAR / BACKSPACE, autorepeat (value 2);
 * OK / CANCEL on press only.  Caller order inside a keypad: ask this first and
 * act on anything but NONE; only on NONE fall back to vnc_nav_key().  Enter and
 * Esc come back here as OK / CANCEL, so a keypad never sees them as nav
 * ACTIVATE / BACK; Space is a char in ALPHA only, and elsewhere stays nav. */
VncKeyChar vnc_key_char(int code, int shift, int mode, char *ch);

#endif /* VNC_PAD_H */
