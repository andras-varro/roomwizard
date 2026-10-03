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
