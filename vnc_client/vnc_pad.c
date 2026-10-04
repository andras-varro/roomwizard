#include "vnc_pad.h"

#include <string.h>

void vnc_pad_map_from_config(VncPadMap *m, const InputConfig *cfg) {
    m->left         = cfg->gamepad_btn_jump;    /* A */
    m->right        = cfg->gamepad_btn_run;     /* B */
    m->wheel_up     = cfg->gamepad_btn_tl;
    m->wheel_down   = cfg->gamepad_btn_tr;
    m->back         = cfg->gamepad_btn_back;
    m->hat_x        = cfg->gamepad_hat_x;
    m->hat_y        = cfg->gamepad_hat_y;
    m->stick_x      = cfg->gamepad_stick_lx;
    m->stick_y      = cfg->gamepad_stick_ly;
    m->deadzone_pct = cfg->gamepad_deadzone;
}

void vnc_pad_reset(VncPad *p, VncPadRange rx, VncPadRange ry, int x, int y) {
    memset(p, 0, sizeof(*p));
    p->range_x = rx;
    p->range_y = ry;
    p->raw_x = x;
    p->raw_y = y;
}

int vnc_pad_axis(int raw, VncPadRange r, int deadzone_pct) {
    long long span = (long long)r.max - r.min;
    if (span < 2 || span > 0x7FFFFFFFLL) return 0;
    int half = (int)(span / 2);
    if (deadzone_pct < 0)   deadzone_pct = 0;
    if (deadzone_pct > 100) deadzone_pct = 100;

    int center = r.min + half;
    int offset = raw - center;
    int mag = offset < 0 ? -offset : offset;
    int dz = (half * deadzone_pct) / 100;
    if (mag <= dz) return 0;

    int eff = half - dz;
    if (eff <= 0) return 0;
    /* (mag-dz)*1000 fits an int for any range up to 2^21; a wider one is
     * scaled the other way round rather than overflowing. */
    int v = (eff < (1 << 21)) ? ((mag - dz) * 1000) / eff
                              : (mag - dz) / (eff / 1000);
    if (v > 1000) v = 1000;
    return offset < 0 ? -v : v;
}

static int sign3(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

int vnc_pad_event(VncPad *p, const VncPadMap *m, int type, int code, int value,
                  uint32_t now) {
    if (type == EV_KEY) {
        if (value == 2) return 0;   /* autorepeat is never a new press */
        bool down = value != 0;
        int bit = 0;
        if (code == m->left)       bit = VNC_PAD_BTN_LEFT;
        else if (code == m->right) bit = VNC_PAD_BTN_RIGHT;

        if (bit) {
            if (down) p->buttons |= bit;
            else      p->buttons &= ~bit;
        } else if (code == m->back) {
            ui_hold_key(&p->back, value, now);
        } else if (code == m->wheel_up && down) {
            return VNC_PAD_WHEEL_UP;
        } else if (code == m->wheel_down && down) {
            return VNC_PAD_WHEEL_DOWN;
        }
    } else if (type == EV_ABS) {
        if (code == m->stick_x)      p->raw_x = value;
        else if (code == m->stick_y) p->raw_y = value;
        else if (code == m->hat_x)   p->hat_x = sign3(value);
        else if (code == m->hat_y)   p->hat_y = sign3(value);
    }
    return 0;
}

static int clamp1000(int v) {
    return v > 1000 ? 1000 : (v < -1000 ? -1000 : v);
}

void vnc_pad_motion(VncPad *p, const VncPadMap *m, uint32_t now, int speed,
                    int *dx, int *dy) {
    *dx = 0;
    *dy = 0;
    if (!p->have_time) {
        p->have_time = true;
        p->last_ms = now;
        return;
    }
    uint32_t elapsed = now - p->last_ms;     /* wrap-safe */
    p->last_ms = now;
    if (elapsed > VNC_PAD_MAX_STEP_MS) elapsed = VNC_PAD_MAX_STEP_MS;

    if (speed < VNC_PAD_MIN_SPEED) speed = VNC_PAD_MIN_SPEED;
    if (speed > VNC_PAD_MAX_SPEED) speed = VNC_PAD_MAX_SPEED;

    int vx = clamp1000(vnc_pad_axis(p->raw_x, p->range_x, m->deadzone_pct)
                       + p->hat_x * VNC_PAD_DPAD_NORM);
    int vy = clamp1000(vnc_pad_axis(p->raw_y, p->range_y, m->deadzone_pct)
                       + p->hat_y * VNC_PAD_DPAD_NORM);

    /* vx/1000 of speed px/s for elapsed ms: vx*speed*elapsed millionths of a
     * pixel.  At most 1000*4000*100 = 4e8, inside an int. */
    if (vx == 0) p->carry_x = 0;
    else         p->carry_x += vx * speed * (int)elapsed;
    if (vy == 0) p->carry_y = 0;
    else         p->carry_y += vy * speed * (int)elapsed;

    *dx = p->carry_x / 1000000;     /* constant divisor: no idiv needed */
    *dy = p->carry_y / 1000000;
    p->carry_x -= *dx * 1000000;
    p->carry_y -= *dy * 1000000;
}

bool vnc_pad_back_exit(const VncPad *p, uint32_t now, int *permille) {
    bool exit = false;
    int pm = ui_hold_progress(p->back.down, now, p->back.start_ms,
                              UI_HOLD_EXIT_MS, &exit);
    if (permille) *permille = pm;
    return exit;
}

int vnc_pad_speed_for_width(int remote_w) {
    int s = remote_w * 2 / 3;
    if (s < VNC_PAD_MIN_SPEED) s = VNC_PAD_MIN_SPEED;
    if (s > VNC_PAD_MAX_SPEED) s = VNC_PAD_MAX_SPEED;
    return s;
}

/* ── Focus navigation ───────────────────────────────────────────────────── */

void vnc_nav_pad_reset(VncNavPad *s, VncPadRange rx, VncPadRange ry) {
    memset(s, 0, sizeof(*s));
    s->range_x = rx;
    s->range_y = ry;
}

/* A key acts on its press from up; the level is kept either way. */
static bool nav_press(bool *down, int value) {
    if (value == 2) return false;
    bool edge = (value == 1 && !*down);
    *down = (value != 0);
    return edge;
}

/* An axis acts on entering a new non-zero direction. */
static bool nav_axis(int *dir, int now) {
    if (now == *dir) return false;
    *dir = now;
    return now != 0;
}

/* The stick's latched direction after a reading: pushed past ON, back below
 * OFF, or unchanged in between. */
static int nav_stick(int latched, int raw, VncPadRange r) {
    int a = vnc_pad_axis(raw, r, 0);
    if (a >= VNC_NAV_STICK_ON)  return 1;
    if (a <= -VNC_NAV_STICK_ON) return -1;
    if (a < VNC_NAV_STICK_OFF && a > -VNC_NAV_STICK_OFF) return 0;
    return latched;
}

static VncNav nav_update(VncNavPad *s, const VncPadMap *m, int type, int code,
                         int value) {
    if (type == EV_KEY) {
        if (code == m->left)
            return nav_press(&s->a_down, value) ? VNC_NAV_ACTIVATE : VNC_NAV_NONE;
        if (code == m->right)
            return nav_press(&s->b_down, value) ? VNC_NAV_BACK : VNC_NAV_NONE;
    } else if (type == EV_ABS) {
        if (code == m->hat_x) {
            if (nav_axis(&s->hat_x, sign3(value)))
                return s->hat_x < 0 ? VNC_NAV_LEFT : VNC_NAV_RIGHT;
        } else if (code == m->hat_y) {
            if (nav_axis(&s->hat_y, sign3(value)))
                return s->hat_y < 0 ? VNC_NAV_UP : VNC_NAV_DOWN;
        } else if (code == m->stick_x) {
            if (nav_axis(&s->stick_x, nav_stick(s->stick_x, value, s->range_x)))
                return s->stick_x < 0 ? VNC_NAV_LEFT : VNC_NAV_RIGHT;
        } else if (code == m->stick_y) {
            if (nav_axis(&s->stick_y, nav_stick(s->stick_y, value, s->range_y)))
                return s->stick_y < 0 ? VNC_NAV_UP : VNC_NAV_DOWN;
        }
    }
    return VNC_NAV_NONE;
}

void vnc_nav_pad_seed(VncNavPad *s, const VncPadMap *m, int type, int code,
                      int value) {
    (void)nav_update(s, m, type, code, value);
}

VncNav vnc_nav_pad_event(VncNavPad *s, const VncPadMap *m, int type, int code,
                         int value) {
    return nav_update(s, m, type, code, value);
}

VncNav vnc_nav_key(int code, int value) {
    if (value == 1 || value == 2) {
        switch (code) {
        case KEY_UP:    return VNC_NAV_UP;
        case KEY_DOWN:  return VNC_NAV_DOWN;
        case KEY_LEFT:  return VNC_NAV_LEFT;
        case KEY_RIGHT: return VNC_NAV_RIGHT;
        default: break;
        }
    }
    if (value == 1) {
        switch (code) {
        case KEY_ENTER: case KEY_KPENTER: case KEY_SPACE: return VNC_NAV_ACTIVATE;
        case KEY_ESC:   return VNC_NAV_BACK;
        default: break;
        }
    }
    return VNC_NAV_NONE;
}

bool vnc_nav_dir(VncNav a, UiDir *d) {
    switch (a) {
    case VNC_NAV_UP:    *d = UI_DIR_UP;    return true;
    case VNC_NAV_DOWN:  *d = UI_DIR_DOWN;  return true;
    case VNC_NAV_LEFT:  *d = UI_DIR_LEFT;  return true;
    case VNC_NAV_RIGHT: *d = UI_DIR_RIGHT; return true;
    default:            return false;
    }
}
