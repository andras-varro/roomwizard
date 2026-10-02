/* usb_page.c — control_panel's USB page: the bus list and port recovery.
 *
 * Opened from the home grid's USB tile, and the one home for everything the
 * panel does with the USB bus: the list of every enumerated device (read by
 * usb_bus.c) and RESCAN — which also revives a port that came up dead.  The
 * keyboard, mouse and gamepad testers are the Input page's (input_page.c): they
 * test a device, whatever bus it is on.  Exposed only as cp_usb_page
 * (cp_page.h); its state lives in this file.  It owns no config keys.
 *
 * A static bus costs nothing: the list is read at startup, once per opening
 * (after the page has painted, re-probing the port if it finds it empty, as
 * RESCAN does) and on RESCAN, and input() returns CP_PAGE_REDRAW only when
 * that reading, or the status line under it, changed.
 */
#include "cp_page.h"
#include "cp_ui.h"
#include "usb_bus.h"
#include "../common/common.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* The init script that owns USB host mode, including the MUSB driver re-probe
 * that revives a port which came up dead. The RESCAN button forks this rather
 * than writing MUSB sysfs from here — one copy of the mechanism, and it is the
 * copy that carries the warnings. */
#define USB_HOST_INIT     "/etc/init.d/usb-host"

#define USB_COLOR_PANEL        RGB(30, 30, 45)
#define USB_COLOR_PANEL_BD     RGB(50, 50, 70)
#define USB_COLOR_DIM          RGB(80, 80, 80)
#define USB_COLOR_HDR          COLOR_CYAN
#define USB_COLOR_CONN         RGB(0, 200, 80)

/* ── Types and state ────────────────────────────────────────────────────── */

#define STATUS_MS       2000   /* how long an empty-list result stays up */

typedef struct {
    UsbBusDev    bus[USB_BUS_MAX];    /* everything enumerated — what the list shows */
    int          bus_cnt;
    bool         scan_pending;         /* enter() painted first: input() scans */
    bool         recover_queued;       /* a scan found nothing: re-probe full-screen */
    bool         recover_once;         /* ...one attempt, not the script's default */
    char         status_msg[64];       /* an empty-list result; "" = none */
    uint32_t     status_time_ms;
} UsbState;

static UsbState usb_state;

static Button usb_btn_rescan;

/* ── USB device helpers ─────────────────────────────────────────────────── */
static void usb_scan_bus(UsbState *s) {
    s->bus_cnt = usb_bus_scan(USB_BUS_ROOT, s->bus, USB_BUS_MAX);
}

/* Bring a dead USB port back, then re-enumerate.
 *
 * ⚠️ Recovery is TWO halves and this app only ever had the second.
 * usb_scan_bus() reads the devices the kernel has already enumerated and
 * cannot make it enumerate one. On a port that came up dead nothing is
 * enumerated, so tapping RESCAN could never satisfy the hint this very page
 * prints — "CONNECT A DEVICE AND TAP RESCAN".
 * The missing half is a MUSB driver re-probe.
 *
 * ⚠️ The rebind is NOT reimplemented here. /etc/init.d/usb-host owns it, along
 * with the retry and every warning about which sysfs writes are silent no-ops on
 * this SoC; a second copy of those paths in C is how the two drift apart. This
 * forks that script and reads its exit status — 0 means it enumerated something.
 *
 * ⚠️ It blocks for several seconds, and a rebind invalidates every open USB fd.
 * So paint a waiting screen first (the app is single-threaded and will not
 * repaint until this returns).  The Input page's testers hold no USB fd by
 * then: each closes its nodes on the way out.
 *
 * once = a single rebind attempt (RECOVER_TRIES=1, the script's own knob)
 * instead of its default retries.  Opening the page asks for one, because it
 * runs on every opening that finds the socket empty and an empty socket
 * exhausts every attempt; RESCAN is an explicit ask and keeps the retries. */
/* A result line.  The empty list shows it centred, where the hint goes; with
 * devices listed it goes to the page bar (cp_status), because right-aligned on
 * the list's title row it overran "DETECTED USB DEVICES:" in portrait. */
static void usb_status(UsbState *s, bool ok, const char *fmt, ...) {
    char m[sizeof(s->status_msg)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof(m), fmt, ap);
    va_end(ap);
    if (s->bus_cnt > 0) {
        s->status_msg[0] = '\0';
        cp_status(m, ok);
    } else {
        snprintf(s->status_msg, sizeof(s->status_msg), "%s", m);
        s->status_time_ms = get_time_ms();
    }
}

static bool usb_recover_port(Framebuffer *fb, UsbState *s, bool once) {
    if (access(USB_HOST_INIT, X_OK) != 0) {
        usb_status(s, false, "USB-HOST SCRIPT NOT INSTALLED");
        return false;
    }

    fb_clear(fb, COLOR_BLACK);
    text_draw_centered(fb, (int)fb->width / 2,
                       (int)fb->height / 2 - 20,
                       "RE-PROBING USB CONTROLLER", COLOR_WHITE, 3);
    text_draw_centered(fb, (int)fb->width / 2,
                       (int)fb->height / 2 + 20,
                       "THIS TAKES A FEW SECONDS", USB_COLOR_DIM, 2);
    fb_swap(fb);

    pid_t pid = fork();
    if (pid < 0) {
        usb_status(s, false, "FORK FAILED");
        return false;
    }
    if (pid == 0) {
        if (once) setenv("RECOVER_TRIES", "1", 1);
        execl(USB_HOST_INIT, "usb-host", "recover", (char *)NULL);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;   /* a signal must not orphan the child */

    usb_scan_bus(s);
    int found = usb_bus_peripherals(s->bus, s->bus_cnt);

    if (found > 0)
        usb_status(s, true, "PORT RECOVERED - %d DEVICE(S)", found);
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
        usb_status(s, false, "COULD NOT RUN USB-HOST");
    else
        usb_status(s, false, "STILL NOTHING - IS A DEVICE PLUGGED IN?");
    return found > 0;
}

/* ── Layout ─────────────────────────────────────────────────────────────── */

#define USB_BTN_W    130
#define USB_BTN_H    40
#define USB_ROW_H    36
#define USB_ROW_Y0   36       /* the panel's title, above the first row */
#define USB_ID_X     140      /* the vid:pid column, in from the panel's right */
#define USB_ID_WIDE  "card 9  ffff:ffff"

static int  list_x, list_y, list_w, list_h;
static int  row_slots;        /* device rows that fit the panel */

/* The name column's width for a device, from its kind badge's width. */
static int name_width(const UsbBusDev *d) {
    return list_w - (text_measure_width(d->kind, 2) + 16) - 170;
}

/* Re-run whenever the logical screen changes (rebuild_ui()).  RESCAN sits
 * centred at the bottom and the list panel takes what is left above it; a
 * list longer than its rows ends in "+N MORE". */
static void usb_page_layout(void) {
    int btn_top = CONTENT_Y + CONTENT_H - 55;
    button_init_full(&usb_btn_rescan, CONTENT_LEFT + (CONTENT_WIDTH - USB_BTN_W) / 2,
                     btn_top, USB_BTN_W, USB_BTN_H, "RESCAN",
                     BTN_COLOR_INFO, COLOR_WHITE, RGB(0,200,255), 2);

    list_x = CONTENT_LEFT;
    list_y = CONTENT_Y + 5;
    list_w = CONTENT_WIDTH;
    list_h = btn_top - 10 - list_y;
    row_slots = (list_h - USB_ROW_Y0 - 4) / USB_ROW_H;
    if (row_slots > USB_BUS_MAX) row_slots = USB_BUS_MAX;
    if (row_slots < 1) row_slots = 1;

    /* ⚠️ THE RECEIPT, in the settings stack's shape.  Everything hangs off
     * CONTENT_Y, which comes from a per-unit touch inset, so a button pushed
     * past the touchable rect looks perfect in a screenshot and is dead to a
     * finger.  The bottom is RESCAN as placed; the right edge the widest of
     * RESCAN, the panel and the vid:pid column at its widest.
     * Names are cut to their column, so what is reported is how many of THIS
     * unit's names were cut, and how many rows the panel has for its devices. */
    {
        const UsbState *s = &usb_state;
        int bottom = (usb_btn_rescan.y + usb_btn_rescan.height) - CONTENT_Y;
        int right  = list_x + list_w;
        if (usb_btn_rescan.x + usb_btn_rescan.width > right)
            right = usb_btn_rescan.x + usb_btn_rescan.width;
        int id_right = list_x + list_w - USB_ID_X + text_measure_width(USB_ID_WIDE, 1);
        if (id_right > right) right = id_right;
        int clipped = 0;
        for (int i = 0; i < s->bus_cnt; i++)
            if (text_measure_width(s->bus[i].name, 2) > name_width(&s->bus[i]))
                clipped++;
        const char *verdict = bottom > CONTENT_H     ? "⚠ PAST CONTENT BOTTOM"
                            : right  > CONTENT_RIGHT ? "⚠ PAST CONTENT RIGHT"
                            : "fits";
        printf("control_panel: usb stack %s — bottom +%d of CONTENT_H %d, "
               "right %d of CONTENT_RIGHT %d, %d value(s) cut, %d device slot(s) "
               "for %d device(s) (safe %dx%d, %s)\n",
               verdict, bottom, CONTENT_H, right, CONTENT_RIGHT, clipped,
               row_slots, s->bus_cnt, SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT,
               CONTENT_WIDTH < 600 ? "portrait" : "landscape");
    }
}

/* ── Draw: the device list ──────────────────────────────────────────────── */
static void usb_page_draw(Framebuffer *fb) {
    const UsbState *s = &usb_state;
    int lx=list_x, ly=list_y, lw=list_w, lh=list_h;

    fb_fill_rounded_rect(fb, lx, ly, lw, lh, 6, USB_COLOR_PANEL);
    fb_draw_rounded_rect(fb, lx, ly, lw, lh, 6, USB_COLOR_PANEL_BD);
    fb_draw_text(fb, lx+12, ly+10, "DETECTED USB DEVICES:", USB_COLOR_HDR, 2);

    if (s->scan_pending) {
        /* enter() painted first; the next input() scans (usb_page_enter()). */
        text_draw_centered(fb, CONTENT_LEFT+CONTENT_WIDTH/2, ly+lh/2,
                           "SCANNING...", USB_COLOR_DIM, 2);
    } else if (s->bus_cnt==0) {
        text_draw_centered(fb, CONTENT_LEFT+CONTENT_WIDTH/2, ly+lh/2-10,
                           "NO USB DEVICES DETECTED", USB_COLOR_DIM, 2);
        /* A recovery attempt that found nothing must not look identical to one
         * that never ran. Show the result where the hint goes — the hint has
         * served its purpose by then. */
        if (s->status_msg[0])
            text_draw_centered(fb, CONTENT_LEFT+CONTENT_WIDTH/2, ly+lh/2+15,
                               s->status_msg, COLOR_YELLOW, 2);
        else
            text_draw_centered(fb, CONTENT_LEFT+CONTENT_WIDTH/2, ly+lh/2+15,
                               "CONNECT A DEVICE AND TAP RESCAN", USB_COLOR_DIM, 2);
    } else {
        int ry0=ly+USB_ROW_Y0, rh=USB_ROW_H;
        /* The bus, not the evdev nodes: a sound card, a BT dongle or a hub has
         * no keyboard/mouse/pad node and used to be invisible while it worked.
         * The Input page's testers key off the evdev scan.  All that fit; if
         * some do not, the last row says how many are missing. */
        int shown = s->bus_cnt <= row_slots ? s->bus_cnt : row_slots - 1;
        for (int i=0; i<shown; i++) {
            const UsbBusDev *d=&s->bus[i]; int ry=ry0+i*rh;
            if (i%2==0) fb_fill_rect(fb, lx+4, ry, lw-8, rh-2, RGB(25,25,38));
            fb_fill_circle(fb, lx+20, ry+rh/2, 6, USB_COLOR_CONN);
            uint32_t bc=USB_COLOR_DIM;
            if      (!strcmp(d->kind,"AUDIO"))   bc=RGB(200,150,0);
            else if (!strcmp(d->kind,"BT"))      bc=RGB(0,120,220);
            else if (!strcmp(d->kind,"HID"))     bc=RGB(0,150,200);
            else if (!strcmp(d->kind,"STORAGE")) bc=RGB(0,180,80);
            int bw=text_measure_width(d->kind,2)+16;
            fb_fill_rounded_rect(fb, lx+36, ry+4, bw, rh-10, 4, bc);
            fb_draw_text(fb, lx+44, ry+10, d->kind, COLOR_WHITE, 2);
            char tn[48]; text_truncate(tn, d->name, name_width(d), 2);
            fb_draw_text(fb, lx+44+bw+10, ry+10, tn, COLOR_LABEL, 2);
            char id[32];
            if (d->card >= 0) snprintf(id, sizeof(id), "card %d  %04x:%04x", d->card, d->vid, d->pid);
            else              snprintf(id, sizeof(id), "%04x:%04x", d->vid, d->pid);
            fb_draw_text(fb, lx+lw-USB_ID_X, ry+14, id, USB_COLOR_DIM, 1);
        }
        if (shown < s->bus_cnt) {
            char more[24];
            snprintf(more, sizeof(more), "+%d MORE", s->bus_cnt - shown);
            fb_draw_text(fb, lx+44, ry0+shown*rh+10, more, COLOR_LABEL, 2);
        }
    }

    button_draw(fb, &usb_btn_rescan);
}

/* ── Input ──────────────────────────────────────────────────────────────── */

static void usb_page_load(const Config *cfg) {
    (void)cfg;
    usb_scan_bus(&usb_state);
}

/* Two dead-port signatures.  An empty scan: when the port is unpowered NOTHING
 * enumerates (a hub alone counts as empty — that is how a dead port looks
 * behind one).  And musb out of host mode, which leaves every device listed —
 * usb_port_dead().  Otherwise the port is live, and a device plugged in later
 * enumerates on its own (measured on .188 across gaps of 70-300 s) — so do not
 * disturb a working bus.  RESCAN and opening the page both decide by this, so
 * the page does on opening what RESCAN would do. */
static bool usb_port_looks_dead(const UsbState *s) {
    return usb_port_dead(s->bus, s->bus_cnt, USB_MUSB_MODE);
}

/* Every opening reads the bus afresh: a reading kept from startup listed
 * devices unplugged since, until RESCAN.  Not here, though — the scan takes a
 * second or two and the page would not paint until it returned.  enter() only
 * marks it pending; draw() shows SCANNING and the next input() scans. */
static void usb_page_enter(void) {
    usb_state.status_msg[0] = '\0';
    usb_state.scan_pending = true;
}

/* Main screen only: a port re-probe is queued here and run by
 * usb_page_run_fullscreen().  A RESCAN repaints only when the reading changed;
 * a status line repaints when it appears and again when it times out. */
static CpPageResult usb_page_input(Config *cfg, int tx, int ty,
                                   bool touching, uint32_t now) {
    (void)cfg;
    UsbState *state = &usb_state;
    CpPageResult act = CP_PAGE_IDLE;

    /* The opening scan, one call after enter(): the page has painted SCANNING
     * by now.  An empty reading queues the same re-probe RESCAN would, once
     * per opening — enter() is the only thing that sets scan_pending — and
     * with one attempt, not three (usb_recover_port()). */
    if (state->scan_pending) {
        state->scan_pending = false;
        usb_scan_bus(state);
        if (usb_port_looks_dead(state)) {
            state->recover_queued = true;
            state->recover_once = true;
            return CP_PAGE_FULLSCREEN;
        }
        return CP_PAGE_REDRAW;
    }

    if (state->status_msg[0] && now - state->status_time_ms > STATUS_MS) {
        state->status_msg[0] = '\0';
        act = CP_PAGE_REDRAW;
    }

    if (button_update(&usb_btn_rescan, tx, ty, touching, now)) {
        /* ~2 KB of reading to compare: off the stack. */
        static UsbBusDev prev_bus[USB_BUS_MAX];
        int prev_bus_cnt = state->bus_cnt;
        memcpy(prev_bus, state->bus, sizeof(prev_bus));
        usb_scan_bus(state);
        if (usb_port_looks_dead(state)) {
            state->recover_queued = true;
            state->recover_once = false;
            return CP_PAGE_FULLSCREEN;
        }
        /* Always say something: an unchanged reading used to repaint nothing,
         * so the tap looked ignored. */
        bool changed = state->bus_cnt != prev_bus_cnt ||
                       memcmp(prev_bus, state->bus, sizeof(prev_bus)) != 0;
        if (changed)
            usb_status(state, true, "RESCANNED - %d DEVICE(S)", state->bus_cnt);
        else
            usb_status(state, true, "RESCANNED - NO CHANGE");
        act = CP_PAGE_REDRAW;
    }
    return act;
}

/* ── Full-screen: the port re-probe ─────────────────────────────────────── */

/* After input() returned CP_PAGE_FULLSCREEN: the re-probe it queued, which
 * paints its own waiting screen (usb_recover_port()). */
static void usb_page_run_fullscreen(Framebuffer *fb, TouchInput *touch) {
    (void)touch;
    UsbState *state = &usb_state;
    if (state->recover_queued) {
        state->recover_queued = false;
        usb_recover_port(fb, state, state->recover_once);
    }
}

const CpPage cp_usb_page = {
    .name           = "USB",
    .icon           = "cp_usb",
    .load           = usb_page_load,
    .layout         = usb_page_layout,
    .enter          = usb_page_enter,
    .draw           = usb_page_draw,
    .input          = usb_page_input,
    .run_fullscreen = usb_page_run_fullscreen,
};
