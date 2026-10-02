/* bluetooth_page.c — control_panel's Bluetooth page: power, scan, the device
 * list, pair / trust / connect / disconnect / remove, the agent's prompts, and
 * USE FOR AUDIO.
 *
 * Opened from the home grid's Bluetooth tile.  Everything goes through
 * bt_ctl.h: one bluetoothctl child, started by enter() and stopped by leave(),
 * polled from input() — which the panel calls once per main-loop iteration
 * whether or not anything was touched, so it is the page's tick.  No thread.
 * Exposed only as cp_bluetooth_page (cp_page.h).  It owns no config keys:
 * USE FOR AUDIO writes the Audio page's OUT, pinned to the selected headset's
 * address, through cp_audio_set_output(), the one writer of both keys; REMOVE
 * on the pinned headset unpins it the same way.
 *
 * Only a change repaints: input() returns CP_PAGE_REDRAW when bt_ctl_poll()
 * reports one or a widget moved, never on a timer.
 */
#include "cp_page.h"
#include "cp_ui.h"
#include "bt_ctl.h"
#include "../common/common.h"
#include "../common/audio_out.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define BT_COLOR_ROW      RGB(30, 30, 45)
#define BT_COLOR_ROW_BD   RGB(50, 50, 70)
#define BT_COLOR_SEL      RGB(30, 70, 120)
#define BT_COLOR_CONN     RGB(0, 200, 80)
#define BT_COLOR_PAIRED   COLOR_CYAN
#define BT_COLOR_NEW      COLOR_YELLOW
#define BT_COLOR_OVERLAY  RGB(15, 15, 25)

#define BT_SCAN_MS        30000   /* a scan stops itself after this */
#define BT_OP_TIMEOUT_MS  45000   /* a pair/connect with no result stops counting as busy */

/* ── State ─────────────────────────────────────────────────────────────── */

static BtState bt;

static char     sel_addr[BT_ADDR_LEN];     /* the selected device, "" = none */
static char     pair_addr[BT_ADDR_LEN];    /* PAIR pressed for it; trust+connect follow success */
static char     remove_addr[BT_ADDR_LEN];  /* what the REMOVE confirmation is about */
static bool     op_pending;                /* a pair/connect/disconnect awaits its result */
static uint32_t op_t0;
static uint32_t scan_t0;                   /* when discovering was first seen; 0 = not */
static bool     scan_off_sent;
static int      list_page;
static bool     prev_touch;
static bool     shown_running;

/* The agent overlay.  REQUEST_PIN is answered by the page with a code it then
 * has to KEEP showing — the operator types it on the keyboard after bluetoothctl
 * has taken the reply — so it is held here until a result arrives, CANCEL, or a
 * different prompt.  ov_dismissed hides a prompt already answered while the
 * parser still reports it; a new question (bt.prompt_seq) un-hides, even one
 * identical to the last. */
static BtPromptKind shown_prompt;
static unsigned shown_prompt_seq;
static unsigned shown_pair_seq;
static bool     ov_dismissed;
static bool     pin_replied;
static bool     pin_hold;
static char     pin_code[8];
static char     pin_addr[BT_ADDR_LEN];     /* the device pin_code is for, "" = unknown */
static int      shown_overlay;

enum { OV_NONE, OV_CODE, OV_CONFIRM, OV_AUTHORIZE };

/* ── Layout ─────────────────────────────────────────────────────────────── */

static ToggleSwitch power_tg;
static Button scan_btn, prev_btn, next_btn, retry_btn;
static Button pair_btn, conn_btn, trust_btn, remove_btn, audio_btn;
static Button ov_yes_btn, ov_no_btn, ov_cancel_btn;

#define BT_TOP_Y     (CONTENT_Y + 8)
#define BT_HDR_Y     (BT_TOP_Y + 44)
#define BT_LIST_Y    (BT_HDR_Y + 22)
#define BT_ROW_H     40                  /* 36 px of row + 4 px gap */
#define BT_BTN_H     36
#define BT_GAP       10
#define BT_ACT_B_Y   (CONTENT_Y + CONTENT_H - BT_BTN_H - 4)
#define BT_ACT_A_Y   (BT_ACT_B_Y - BT_BTN_H - 8)
#define BT_TRACK_W   60
#define BT_TRACK_H   28

static int rows_fit;          /* device rows between the header and action row A */
static int right_col_x;       /* the status/kind column */

/* ⚠️ button_draw() centres and neither pads nor clips, so a box is sized from
 * the WIDEST label it can carry at its scale, plus 8 px a side. */
static int label_box(const char *a, const char *b, int scale) {
    int w = text_measure_width(a, scale);
    if (b && text_measure_width(b, scale) > w) w = text_measure_width(b, scale);
    return w + 16;
}

static void btn(Button *b, int x, int y, int w, int h, const char *t,
                uint32_t bg, int scale) {
    button_init_full(b, x, y, w, h, t, bg, COLOR_WHITE, BTN_COLOR_HIGHLIGHT, scale);
}

/* A toggle's hit box right edge, as toggle_check_press() computes it. */
static int toggle_right(const ToggleSwitch *t) {
    return t->x - 5 + t->track_w + text_measure_width(t->label, 1) + 20;
}

/* Action row A's buttons that apply, left to right.  The same placement for
 * draw and input, so the box hit-tested is the box painted.  Returns the right
 * edge of the last one. */
static int place_actions(Button **out, int *n, const BtDevice *d) {
    Button *order[4];
    int k = 0;
    *n = 0;
    if (!d) return CONTENT_LEFT;
    if (!d->paired) order[k++] = &pair_btn;
    if (d->paired) {
        order[k++] = &conn_btn;
        order[k++] = &trust_btn;
    }
    order[k++] = &remove_btn;
    int x = CONTENT_LEFT + 5;
    for (int i = 0; i < k; i++) {
        order[i]->x = x;
        x += order[i]->width + BT_GAP;
        out[(*n)++] = order[i];
    }
    return x - BT_GAP;
}

static void bt_page_layout(void) {
    toggle_init(&power_tg, CONTENT_LEFT + 5, BT_TOP_Y + 2, BT_TRACK_W, BT_TRACK_H,
                "POWER", false);
    btn(&scan_btn, toggle_right(&power_tg) + 20, BT_TOP_Y,
        label_box("SCAN", "STOP SCAN", 2), 32, "SCAN", BTN_COLOR_INFO, 2);
    btn(&next_btn, CONTENT_RIGHT - 44, BT_TOP_Y, 44, 32, ">", BTN_COLOR_SECONDARY, 2);
    btn(&prev_btn, CONTENT_RIGHT - 44 - BT_GAP - 44, BT_TOP_Y, 44, 32, "<",
        BTN_COLOR_SECONDARY, 2);
    btn(&retry_btn, CONTENT_LEFT + 5, BT_TOP_Y + 40, label_box("RETRY", NULL, 2),
        BT_BTN_H, "RETRY", BTN_COLOR_INFO, 2);

    btn(&pair_btn, 0, BT_ACT_A_Y, label_box("PAIR", NULL, 2), BT_BTN_H, "PAIR",
        BTN_COLOR_PRIMARY, 2);
    btn(&conn_btn, 0, BT_ACT_A_Y, label_box("CONNECT", "DISCONNECT", 2), BT_BTN_H,
        "CONNECT", BTN_COLOR_INFO, 2);
    btn(&trust_btn, 0, BT_ACT_A_Y, label_box("TRUST", "UNTRUST", 2), BT_BTN_H,
        "TRUST", BTN_COLOR_INFO, 2);
    btn(&remove_btn, 0, BT_ACT_A_Y, label_box("REMOVE", NULL, 2), BT_BTN_H,
        "REMOVE", BTN_COLOR_DANGER, 2);
    btn(&audio_btn, CONTENT_LEFT + 5, BT_ACT_B_Y, label_box("USE FOR AUDIO", NULL, 2),
        BT_BTN_H, "USE FOR AUDIO", BTN_COLOR_INFO, 2);

    int cx = CONTENT_LEFT + CONTENT_WIDTH / 2;
    int oy = CONTENT_Y + CONTENT_H - 64;
    btn(&ov_yes_btn, cx - 150, oy, 140, 48, "YES", BTN_COLOR_PRIMARY, 3);
    btn(&ov_no_btn, cx + 10, oy, 140, 48, "NO", BTN_COLOR_DANGER, 3);
    btn(&ov_cancel_btn, cx - 70, oy, 140, 48, "CANCEL", BTN_COLOR_DANGER, 3);

    int col_w = label_box("CONNECTED", "KEYBOARD", 1) - 16;
    right_col_x = CONTENT_RIGHT - 8 - col_w;

    rows_fit = (BT_ACT_A_Y - 8 - BT_LIST_Y) / BT_ROW_H;   /* constant divisor */
    if (rows_fit < 1) rows_fit = 1;

    /* ⚠️ THE RECEIPT.  Everything hangs off CONTENT_Y, which comes from a
     * per-unit touch inset.  The bottom is action row B; the widest row is
     * action row A with a paired device's three buttons, or row B with its
     * "OUT: BLUETOOTH" note, or the top row's SCAN and paging buttons meeting. */
    {
        BtDevice probe;
        Button *acts[4];
        int n;
        memset(&probe, 0, sizeof(probe));
        probe.paired = true;
        int right = place_actions(acts, &n, &probe);
        int rb = audio_btn.x + audio_btn.width + 12 +
                 text_measure_width("OUT: BLUETOOTH", 2);
        if (rb > right) right = rb;
        int bottom = (audio_btn.y + audio_btn.height) - CONTENT_Y;
        bool top_clash = scan_btn.x + scan_btn.width + BT_GAP > prev_btn.x;
        const char *verdict = bottom > CONTENT_H     ? "⚠ PAST CONTENT BOTTOM"
                            : right  > CONTENT_RIGHT ? "⚠ PAST CONTENT RIGHT"
                            : top_clash              ? "⚠ SCAN MEETS PAGING"
                            : "fits";
        printf("control_panel: bluetooth stack %s — %d device rows of %d px, "
               "bottom +%d of CONTENT_H %d, right %d of CONTENT_RIGHT %d "
               "(safe %dx%d, %s)\n",
               verdict, rows_fit, BT_ROW_H, bottom, CONTENT_H, right,
               CONTENT_RIGHT, SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT,
               CONTENT_WIDTH < 600 ? "portrait" : "landscape");
    }
}

/* ── Derived state ──────────────────────────────────────────────────────── */

/* Paired, or seen in range since the page opened.  An unpaired cache entry
 * that is not in range is not offered — absent, not greyed. */
static int build_vis(int *vis) {
    int n = 0;
    for (int i = 0; i < bt.ndev; i++)
        if (bt.dev[i].paired || bt.dev[i].seen) vis[n++] = i;
    return n;
}

static int page_count(int nvis) {
    int pages = 1;
    for (int n = nvis; n > rows_fit; n -= rows_fit) pages++;   /* no divide */
    return pages;
}

/* The selected device if it is still listed; a vanished one is deselected. */
static BtDevice *selected(void) {
    if (!sel_addr[0]) return NULL;
    BtDevice *d = bt_find(&bt, sel_addr);
    if (!d || !(d->paired || d->seen)) {
        sel_addr[0] = '\0';
        return NULL;
    }
    return d;
}

static bool is_audio(const BtDevice *d) {
    return strncmp(d->icon, "audio-", 6) == 0;
}

static const char *kind_tag(const char *icon) {
    if (!icon[0])                                return "";
    if (!strcmp(icon, "audio-headset") ||
        !strcmp(icon, "audio-headphones"))       return "HEADSET";
    if (!strncmp(icon, "audio-", 6))             return "AUDIO";
    if (!strcmp(icon, "input-keyboard"))         return "KEYBOARD";
    if (!strcmp(icon, "input-mouse"))            return "MOUSE";
    if (!strcmp(icon, "input-gaming"))           return "PAD";
    if (!strcmp(icon, "input-tablet"))           return "TABLET";
    if (!strncmp(icon, "phone", 5))              return "PHONE";
    if (!strcmp(icon, "computer"))               return "COMPUTER";
    return "DEVICE";
}

static int overlay_kind(void) {
    if (!bt_ctl_running(&bt)) return OV_NONE;
    if (pin_hold)             return OV_CODE;
    if (ov_dismissed)         return OV_NONE;
    switch (bt.prompt) {
    case BT_PROMPT_DISPLAY_PASSKEY:
    case BT_PROMPT_DISPLAY_PIN:  return OV_CODE;
    case BT_PROMPT_CONFIRM:      return OV_CONFIRM;
    case BT_PROMPT_AUTHORIZE:    return OV_AUTHORIZE;
    default:                     return OV_NONE;
    }
}

/* Widget state from the BtState, every frame, never cached. */
static void sync_widgets(const BtDevice *d) {
    power_tg.state    = bt.powered;
    scan_btn.disabled = !bt.powered;
    button_set_text(&scan_btn, bt.discovering ? "STOP SCAN" : "SCAN");
    /* One pair/connect at a time: a second PAIR would retarget pair_addr. */
    pair_btn.disabled = !bt.powered || op_pending;
    conn_btn.disabled = !bt.powered;
    if (d) {
        button_set_text(&conn_btn, d->connected ? "DISCONNECT" : "CONNECT");
        button_set_text(&trust_btn, d->trusted ? "UNTRUST" : "TRUST");
    }
}

static bool audio_offered(const BtDevice *d) {
    return d && d->connected && is_audio(d);
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

static void reset_page_state(void) {
    sel_addr[0] = pair_addr[0] = remove_addr[0] = '\0';
    op_pending = false;
    scan_t0 = 0;
    scan_off_sent = false;
    list_page = 0;
    shown_prompt = BT_PROMPT_NONE;
    shown_prompt_seq = shown_pair_seq = 0;      /* bt_state_init() zeroes both counters too */
    pin_addr[0] = '\0';
    ov_dismissed = pin_replied = pin_hold = false;
    shown_overlay = OV_NONE;
}

static void bt_page_start(void) {
    bt_state_init(&bt);
    reset_page_state();
    srand(get_time_ms() ^ (uint32_t)getpid());   /* the REQUEST_PIN code */
    if (!bt_ctl_start(&bt))
        fprintf(stderr, "control_panel: bluetoothctl did not start\n");
    shown_running = bt_ctl_running(&bt);
}

static void bt_page_load(const Config *cfg) { bt_state_init(&bt); }
static void bt_page_enter(void) { bt_page_start(); }
static void bt_page_leave(void) { bt_ctl_stop(&bt); }

/* Live: the child is up and something is in flight that input() must service
 * promptly — a scan filling the list, a pair/connect awaiting its result, or a
 * prompt the operator is answering. */
static bool bt_page_busy(void) {
    return bt_ctl_running(&bt) &&
           (bt.discovering || op_pending || bt.prompt != BT_PROMPT_NONE ||
            overlay_kind() != OV_NONE);
}

/* ── Draw ───────────────────────────────────────────────────────────────── */

static void draw_overlay(Framebuffer *fb, int kind) {
    int cx = CONTENT_LEFT + CONTENT_WIDTH / 2;
    fb_fill_rect(fb, CONTENT_LEFT, CONTENT_Y + 4, CONTENT_WIDTH, CONTENT_H - 8,
                 BT_COLOR_OVERLAY);
    fb_draw_rect(fb, CONTENT_LEFT, CONTENT_Y + 4, CONTENT_WIDTH, CONTENT_H - 8,
                 BT_COLOR_ROW_BD);

    const char *l1 = "", *l3 = "", *code = "";
    if (kind == OV_CODE) {
        l1 = "TYPE THIS ON THE KEYBOARD";
        l3 = "THEN PRESS ENTER";
        code = pin_hold ? pin_code : bt.prompt_code;
    } else if (kind == OV_CONFIRM) {
        l1 = "DOES THE DEVICE SHOW";
        l3 = "THIS NUMBER?";
        code = bt.prompt_code;
    } else {
        l1 = "ALLOW THIS DEVICE?";
    }
    text_draw_centered(fb, cx, CONTENT_Y + 40, l1, COLOR_WHITE, 2);
    if (code[0])
        text_draw_centered(fb, cx, CONTENT_Y + 110, code, COLOR_YELLOW, 6);
    if (l3[0])
        text_draw_centered(fb, cx, CONTENT_Y + 170, l3, COLOR_WHITE, 2);

    const char *who = pair_addr[0] ? pair_addr : pin_hold ? pin_addr : bt.prompt_addr;
    const BtDevice *d = who[0] ? bt_find(&bt, who) : NULL;
    if (d && d->name[0]) who = d->name;
    if (who[0]) {
        char cut[64];
        snprintf(cut, sizeof(cut), "%.30s", who);
        text_draw_centered(fb, cx, CONTENT_Y + 200, cut, COLOR_LABEL, 2);
    }

    if (kind == OV_CODE) {
        button_draw(fb, &ov_cancel_btn);
    } else {
        button_draw(fb, &ov_yes_btn);
        button_draw(fb, &ov_no_btn);
    }
}

static void draw_row(Framebuffer *fb, int y, const BtDevice *d, bool sel) {
    int h = BT_ROW_H - 4;
    fb_fill_rect(fb, CONTENT_LEFT, y, CONTENT_WIDTH, h, sel ? BT_COLOR_SEL : BT_COLOR_ROW);
    fb_draw_rect(fb, CONTENT_LEFT, y, CONTENT_WIDTH, h, BT_COLOR_ROW_BD);

    /* fit_value() cuts at CONTENT_RIGHT; shifting x by the column's distance
     * from it makes the cut land at the status column instead. */
    char cut[64];
    int nx = CONTENT_LEFT + 10;
    int limit = right_col_x - 10;
    fit_value(d->name[0] ? d->name : d->addr, nx + (CONTENT_RIGHT - limit), 2,
              cut, sizeof(cut));
    fb_draw_text(fb, nx, y + (BT_ROW_H - 4 - 14) / 2, cut, COLOR_WHITE, 2);

    const char *st = d->connected ? "CONNECTED" : d->paired ? "PAIRED" : "NEW";
    uint32_t sc = d->connected ? BT_COLOR_CONN : d->paired ? BT_COLOR_PAIRED : BT_COLOR_NEW;
    fb_draw_text(fb, right_col_x, y + 6, st, sc, 1);
    fb_draw_text(fb, right_col_x, y + 20, kind_tag(d->icon), COLOR_LABEL, 1);
}

static void bt_page_draw(Framebuffer *fb) {
    bt.changed = false;
    shown_running = bt_ctl_running(&bt);
    shown_overlay = overlay_kind();

    if (!shown_running) {
        fb_draw_text(fb, CONTENT_LEFT + 5, BT_TOP_Y + 8, "BLUETOOTHCTL NOT RUNNING",
                     COLOR_ORANGE, 2);
        button_draw(fb, &retry_btn);
        return;
    }
    if (shown_overlay != OV_NONE) {
        draw_overlay(fb, shown_overlay);
        return;
    }

    BtDevice *d = selected();
    sync_widgets(d);
    toggle_draw(fb, &power_tg);
    button_draw(fb, &scan_btn);

    int vis[BT_MAX_DEVICES];
    int nvis = build_vis(vis);
    int pages = page_count(nvis);
    if (list_page >= pages) list_page = pages - 1;
    if (pages > 1) {
        button_draw(fb, &prev_btn);
        button_draw(fb, &next_btn);
    }

    char hdr[32];
    if (pages > 1) snprintf(hdr, sizeof(hdr), "DEVICES %d/%d", list_page + 1, pages);
    else           snprintf(hdr, sizeof(hdr), "DEVICES");
    draw_section_header(fb, BT_HDR_Y, hdr);

    int first = list_page * rows_fit;
    for (int r = 0; r < rows_fit && first + r < nvis; r++) {
        const BtDevice *rd = &bt.dev[vis[first + r]];
        draw_row(fb, BT_LIST_Y + r * BT_ROW_H, rd, rd == d);
    }
    if (nvis == 0)
        fb_draw_text(fb, CONTENT_LEFT + 10, BT_LIST_Y + 10,
                     bt.powered ? "NO DEVICES - TAP SCAN" : "POWER IS OFF",
                     COLOR_LABEL, 2);

    Button *acts[4];
    int n;
    place_actions(acts, &n, d);
    for (int i = 0; i < n; i++) button_draw(fb, acts[i]);

    if (audio_offered(d)) {
        int saved = cp_audio_output();
        bool mine = audio_out_bt_is_pinned(saved, cp_audio_bt_addr(), d->addr);
        int tx = CONTENT_LEFT + 5;
        if (!mine) {
            button_draw(fb, &audio_btn);
            tx = audio_btn.x + audio_btn.width + 12;
        }
        /* BLUETOOTH pinned to another headset is not this one's audio: say so,
         * rather than a green "BLUETOOTH" beside a device that plays nothing. */
        char out[40];
        if (saved == AUDIO_OUT_CHOICE_BT && !mine && cp_audio_bt_addr()[0])
            snprintf(out, sizeof(out), "OUT: OTHER BT");
        else
            snprintf(out, sizeof(out), "OUT: %s", audio_out_choice_label(saved));
        fb_draw_text(fb, tx, BT_ACT_B_Y + (BT_BTN_H - 14) / 2, out,
                     mine ? BT_COLOR_CONN : COLOR_LABEL, 2);
    }
}

/* ── Input ──────────────────────────────────────────────────────────────── */

static void on_remove_ok(Config *cfg) {
    if (!remove_addr[0]) return;
    bt_ctl_send(&bt, "remove %s", remove_addr);
    /* A forgotten headset is no longer anybody's preference: unpin it, and
     * leave OUT as it is. */
    if (strcasecmp(remove_addr, cp_audio_bt_addr()) == 0)
        cp_audio_set_output(cfg, cp_audio_output(), "");
    if (!strcmp(sel_addr, remove_addr)) sel_addr[0] = '\0';
    remove_addr[0] = '\0';
}

static void start_op(uint32_t now) {
    op_pending = true;
    op_t0 = now;
}

/* The agent's side: answer a REQUEST_PIN, track prompt changes. */
static void service_prompt(void) {
    BtPromptKind p = bt.prompt;
    bool fresh_q = p != BT_PROMPT_NONE && bt.prompt_seq != shown_prompt_seq;
    if (p != shown_prompt || fresh_q) {
        ov_dismissed = false;
        if (p != BT_PROMPT_NONE && p != BT_PROMPT_REQUEST_PIN) pin_hold = false;
        if (p != BT_PROMPT_REQUEST_PIN || fresh_q) pin_replied = false;
        shown_prompt = p;
        shown_prompt_seq = bt.prompt_seq;
    }
    if (p == BT_PROMPT_REQUEST_PIN && !pin_replied) {
        /* % by a constant: no runtime divide (Cortex-A8 has none). */
        snprintf(pin_code, sizeof(pin_code), "%06d", (int)(rand() % 1000000));
        snprintf(pin_addr, sizeof(pin_addr), "%s", pair_addr[0] ? pair_addr : bt.prompt_addr);
        bt_ctl_send(&bt, "%s", pin_code);
        pin_replied = true;
        pin_hold = true;
    }
}

/* [CHG] Paired:/Bonded: — the one ending a REMOTE-started pairing prints
 * (it gets no "Pairing successful").  Ends a held PIN for that device, or
 * for an unknown one; and the moment pair_addr is paired, trust + connect it,
 * once: pair_addr is cleared as they are sent.  Reads the device's flag, not
 * the event, because several events can land in one poll. */
static void service_pairing(uint32_t now) {
    if (bt.pair_seq == shown_pair_seq) return;
    shown_pair_seq = bt.pair_seq;
    if (pin_hold && (!pin_addr[0] || !strcmp(pin_addr, bt.pair_ev_addr))) pin_hold = false;
    const BtDevice *d = pair_addr[0] ? bt_find(&bt, pair_addr) : NULL;
    if (d && d->paired) {
        /* Keyboards and pads reconnect on their own only when trusted. */
        bt_ctl_send(&bt, "trust %s", pair_addr);
        bt_ctl_send(&bt, "connect %s", pair_addr);
        pair_addr[0] = '\0';
        start_op(now);
    }
}

/* The results that end what op_pending waits for: a pair, a connect or a
 * disconnect (and so the auto sequence's final connect). */
static bool ends_op(const char *r) {
    static const char *const P[] = {
        "Pairing successful", "Failed to pair", "Cancel pairing successful",
        "Connection successful", "Failed to connect",
        "Successful disconnected", "Failed to disconnect", NULL
    };
    for (int i = 0; P[i]; i++)
        if (!strncmp(r, P[i], strlen(P[i]))) return true;
    return strstr(r, " not available") != NULL;
}

static void service_result(void) {
    if (!bt.result_new) return;
    bt.result_new = false;
    cp_status(bt.result, bt.result_ok);
    if (!ends_op(bt.result)) return;     /* a trust or remove result ends nothing in flight */
    pin_hold = false;
    /* A pair this page started ends here if it failed; on success,
     * service_pairing() has already sent trust + connect (Paired: yes
     * precedes "Pairing successful"), or will on the [CHG] still to come —
     * so a success is not an end of the op. */
    if (strstr(bt.result, "Pairing successful") && pair_addr[0]) return;
    if (pair_addr[0] && (strstr(bt.result, "Failed to pair") ||
                         strstr(bt.result, "Cancel pairing successful")))
        pair_addr[0] = '\0';
    if (!strstr(bt.result, "Pairing successful")) op_pending = false;
}

static CpPageResult bt_page_input(Config *cfg, int tx, int ty,
                                  bool touching, uint32_t now) {
    bool redraw = false;
    bool press = touching && !prev_touch;
    prev_touch = touching;

    if (bt_ctl_running(&bt) && bt_ctl_poll(&bt)) redraw = true;
    if (bt_ctl_running(&bt) != shown_running) redraw = true;

    if (!bt_ctl_running(&bt)) {
        if (button_update(&retry_btn, tx, ty, touching, now)) {
            bt_page_start();
            redraw = true;
        }
        return redraw ? CP_PAGE_REDRAW : CP_PAGE_IDLE;
    }

    service_prompt();
    service_pairing(now);                /* before the result: Paired: yes precedes it */
    service_result();

    /* A scan stops itself; discovering started elsewhere is timed from when
     * this page first saw it. */
    if (bt.discovering) {
        if (!scan_t0) scan_t0 = now | 1;
        else if (!scan_off_sent && now - scan_t0 > BT_SCAN_MS) {
            bt_ctl_send(&bt, "scan off");
            scan_off_sent = true;
        }
    } else {
        scan_t0 = 0;
        scan_off_sent = false;
    }
    if (op_pending && now - op_t0 > BT_OP_TIMEOUT_MS) op_pending = false;

    int ov = overlay_kind();
    if (ov != shown_overlay) redraw = true;

    if (ov != OV_NONE) {
        if (ov == OV_CODE) {
            if (button_update(&ov_cancel_btn, tx, ty, touching, now)) {
                /* A bare `cancel-pairing` is refused ("Missing device address
                 * argument") unless a device is selected in bluetoothctl, so
                 * with no known address the overlay is only dismissed. */
                const char *who = pair_addr[0] ? pair_addr
                                : pin_hold    ? pin_addr : bt.prompt_addr;
                if (who[0]) bt_ctl_send(&bt, "cancel-pairing %s", who);
                pair_addr[0] = '\0';
                pin_hold = false;
                ov_dismissed = true;
                op_pending = false;
                redraw = true;
            }
        } else {
            bool yes = button_update(&ov_yes_btn, tx, ty, touching, now);
            bool no  = !yes && button_update(&ov_no_btn, tx, ty, touching, now);
            if (yes || no) {
                bt_ctl_send(&bt, "%s", yes ? "yes" : "no");
                ov_dismissed = true;
                redraw = true;
            }
        }
        return redraw ? CP_PAGE_REDRAW : CP_PAGE_IDLE;
    }

    BtDevice *d = selected();
    sync_widgets(d);

    if (toggle_check_press(&power_tg, tx, ty, touching, now)) {
        bt_ctl_send(&bt, "power %s", power_tg.state ? "on" : "off");
        power_tg.state = bt.powered;     /* the parser reports the outcome */
        redraw = true;
    }
    if (button_update(&scan_btn, tx, ty, touching, now)) {
        bt_ctl_send(&bt, "scan %s", bt.discovering ? "off" : "on");
        redraw = true;
    }

    int vis[BT_MAX_DEVICES];
    int nvis = build_vis(vis);
    int pages = page_count(nvis);
    if (list_page >= pages) { list_page = pages - 1; redraw = true; }
    if (pages > 1) {
        if (button_update(&prev_btn, tx, ty, touching, now)) {
            list_page = list_page > 0 ? list_page - 1 : pages - 1;
            redraw = true;
        }
        if (button_update(&next_btn, tx, ty, touching, now)) {
            list_page = list_page + 1 < pages ? list_page + 1 : 0;
            redraw = true;
        }
    }

    if (press && tx >= CONTENT_LEFT && tx < CONTENT_RIGHT) {
        int first = list_page * rows_fit;
        for (int r = 0; r < rows_fit && first + r < nvis; r++) {
            int y = BT_LIST_Y + r * BT_ROW_H;
            if (ty >= y && ty < y + BT_ROW_H - 4) {
                const BtDevice *rd = &bt.dev[vis[first + r]];
                if (strcmp(sel_addr, rd->addr)) {
                    snprintf(sel_addr, sizeof(sel_addr), "%s", rd->addr);
                    redraw = true;
                }
                break;
            }
        }
    }

    d = selected();
    sync_widgets(d);
    Button *acts[4];
    int n;
    place_actions(acts, &n, d);
    for (int i = 0; d && i < n; i++) {
        if (!button_update(acts[i], tx, ty, touching, now)) continue;
        redraw = true;
        if (acts[i] == &pair_btn) {
            snprintf(pair_addr, sizeof(pair_addr), "%s", d->addr);
            bt_ctl_send(&bt, "pair %s", d->addr);
            start_op(now);
        } else if (acts[i] == &conn_btn) {
            bt_ctl_send(&bt, "%s %s", d->connected ? "disconnect" : "connect", d->addr);
            start_op(now);
        } else if (acts[i] == &trust_btn) {
            bt_ctl_send(&bt, "%s %s", d->trusted ? "untrust" : "trust", d->addr);
        } else if (acts[i] == &remove_btn) {
            char msg[80];
            snprintf(remove_addr, sizeof(remove_addr), "%s", d->addr);
            snprintf(msg, sizeof(msg), "FORGET %.24s?\nIT MUST PAIR AGAIN.",
                     d->name[0] ? d->name : d->addr);
            cp_confirm("REMOVE DEVICE", msg, "REMOVE", on_remove_ok);
            redraw = false;              /* the dialog appearing repaints */
        }
        break;
    }

    /* Pins the output to THIS headset's address, not to "whichever sink
     * BlueALSA picks" — with two A2DP sinks connected that was the most
     * recently connected one, whatever was selected here. */
    if (audio_offered(d) &&
        !audio_out_bt_is_pinned(cp_audio_output(), cp_audio_bt_addr(), d->addr) &&
        button_update(&audio_btn, tx, ty, touching, now)) {
        cp_audio_set_output(cfg, AUDIO_OUT_CHOICE_BT, d->addr);
        cp_status("AUDIO OUT: THIS HEADSET", true);
        redraw = true;
    }

    return redraw ? CP_PAGE_REDRAW : CP_PAGE_IDLE;
}

const CpPage cp_bluetooth_page = {
    .name   = "Bluetooth",
    .icon   = "cp_bluetooth",
    .load   = bt_page_load,
    .layout = bt_page_layout,
    .enter  = bt_page_enter,
    .leave  = bt_page_leave,
    .draw   = bt_page_draw,
    .input  = bt_page_input,
    .busy   = bt_page_busy,
};
