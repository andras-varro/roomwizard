/* input_page.c — control_panel's Input page: the touch tools and the
 * keyboard, mouse and pad testers.
 *
 * Opened from the home grid's Input tile, and the one home for testing an
 * input device, whatever bus it arrives on: the testers open evdev nodes by
 * path, so a Bluetooth keyboard needs no second copy of them.  The USB bus
 * itself (the device list, RESCAN, port recovery) stays on the USB page.
 * Exposed only as cp_input_page (cp_page.h); its state lives in this file.
 * It owns no config keys.
 *
 * The touch tools (MULTI-TOUCH so far) head the page; the testers sit under
 * their own section header.  Each tester's button is disabled while no node of
 * its kind is present.  A static device set costs nothing: /dev/input is
 * listed once a second while the page is open, the nodes are classified again
 * only when that listing changed, and input() returns CP_PAGE_REDRAW only when
 * the count of some kind did.
 */
#include "cp_page.h"
#include "cp_ui.h"
#include "../common/common.h"
#include "../common/input_scan.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define INPUT_COLOR_PANEL        RGB(30, 30, 45)
#define INPUT_COLOR_PANEL_BD     RGB(50, 50, 70)
#define INPUT_COLOR_DIM          RGB(80, 80, 80)
#define INPUT_COLOR_HDR          COLOR_CYAN
#define INPUT_COLOR_CONN         RGB(0, 200, 80)
#define INPUT_COLOR_KEY_UP       RGB(50, 50, 60)
#define INPUT_COLOR_KEY_DN       RGB(0, 180, 255)
#define INPUT_COLOR_KEY_BD       RGB(80, 80, 100)
#define INPUT_COLOR_KEY_TXT      RGB(200, 200, 200)
#define INPUT_COLOR_STICK_BG     RGB(40, 40, 50)
#define INPUT_COLOR_STICK_DOT    RGB(0, 200, 255)
#define INPUT_COLOR_PAD_ON       RGB(0, 220, 100)
#define INPUT_COLOR_PAD_OFF      RGB(60, 60, 70)
#define INPUT_COLOR_TRIG_BG      RGB(40, 40, 40)
#define INPUT_COLOR_TRIG_FILL    RGB(255, 165, 0)
#define INPUT_COLOR_CURSOR_C     RGB(255, 255, 0)
#define INPUT_COLOR_MBTN_ON      RGB(0, 200, 80)
#define INPUT_COLOR_MBTN_OFF     RGB(60, 60, 70)
#define INPUT_COLOR_SCROLL       RGB(0, 180, 255)
#define INPUT_COLOR_LOG_BG       RGB(15, 15, 25)
#define INPUT_COLOR_LOG_TXT      RGB(150, 200, 150)

/* ── Types and state ────────────────────────────────────────────────────── */

#define MAX_INPUT_DEV     8
#define DEV_NAME_LEN    128
#define LOG_LINES       8
#define LOG_LINE_LEN    64
#define POLL_MS         1000   /* how often /dev/input is listed for a hot plug */

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(x)        ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)          ((x) % BITS_PER_LONG)
#define BIT_LONG(x)     ((x) / BITS_PER_LONG)
#define test_bit(b, a)  ((a[BIT_LONG(b)] >> OFF(b)) & 1)

typedef enum { DEV_UNKNOWN, DEV_KEYBOARD, DEV_MOUSE, DEV_GAMEPAD } DevType;

typedef struct {
    char name[DEV_NAME_LEN]; char path[64];
    DevType type; int ev_num; bool connected;
} InputDev;

typedef struct {
    bool held[KEY_MAX+1]; int last_code; char last_name[32];
    char log[LOG_LINES][LOG_LINE_LEN]; int log_cnt;
} KbdState;

typedef struct {
    int cx, cy; bool bl, bm, br;
    int scroll; uint32_t scroll_t;
} MouseSt;

typedef struct {
    int lx, ly, rx, ry;
    /* Per open slot (see fds[]): two pads can report different ranges. */
    int amin[MAX_INPUT_DEV][ABS_MAX+1], amax[MAX_INPUT_DEV][ABS_MAX+1];
    int dx, dy; bool btns[16]; int btn_cnt;
    int tl, tr;
} PadSt;

typedef enum { INPUT_SCR_MAIN, INPUT_SCR_KEYBOARD, INPUT_SCR_MOUSE, INPUT_SCR_GAMEPAD,
               INPUT_SCR_MULTITOUCH } InputScreen;

typedef struct {
    InputScreen  scr;
    InputDev     devs[MAX_INPUT_DEV];   /* evdev nodes the testers can open */
    int          dev_cnt;
    int          kbd_idx, mou_idx, pad_idx;   /* first of each kind, -1 none */
    int          kind_cnt[3];           /* keyboards, mice, pads: what a redraw keys off */
    /* A tester opens EVERY node of its kind at once: a touchpad keyboard exposes
     * its own mouse node, so "the first mouse" is often not the one in the hand. */
    int          fds[MAX_INPUT_DEV];
    int          fd_dev[MAX_INPUT_DEV];  /* devs[] index behind each fd */
    int          fd_cnt;
    int          last_dev;             /* devs[] index of the last event, -1 none */
    KbdState     kbd;
    MouseSt      mou;
    PadSt        pad;
    unsigned long node_sig;            /* /dev/input's listing when last scanned */
    uint32_t     poll_ms;              /* when it was last listed; 0 = list now */
} InputState;

static InputState input_state;

static Button input_btn_ktest, input_btn_mtest, input_btn_gtest;
static Button input_btn_kback, input_btn_mback, input_btn_gback;
static Button input_btn_multitouch;

/* ── Key table ──────────────────────────────────────────────────────────── */
typedef struct { int code; const char *name, *sname; } KeyInfo;
static const KeyInfo input_ktab[] = {
    {KEY_ESC,"ESC","ESC"},{KEY_1,"1","1"},{KEY_2,"2","2"},{KEY_3,"3","3"},
    {KEY_4,"4","4"},{KEY_5,"5","5"},{KEY_6,"6","6"},{KEY_7,"7","7"},
    {KEY_8,"8","8"},{KEY_9,"9","9"},{KEY_0,"0","0"},{KEY_MINUS,"MINUS","-"},
    {KEY_EQUAL,"EQUAL","="},{KEY_BACKSPACE,"BKSP","BS"},
    {KEY_TAB,"TAB","TAB"},{KEY_Q,"Q","Q"},{KEY_W,"W","W"},{KEY_E,"E","E"},
    {KEY_R,"R","R"},{KEY_T,"T","T"},{KEY_Y,"Y","Y"},{KEY_U,"U","U"},
    {KEY_I,"I","I"},{KEY_O,"O","O"},{KEY_P,"P","P"},
    {KEY_LEFTBRACE,"LBRACE","["},{KEY_RIGHTBRACE,"RBRACE","]"},
    {KEY_ENTER,"ENTER","RET"},
    {KEY_CAPSLOCK,"CAPS","CAP"},{KEY_A,"A","A"},{KEY_S,"S","S"},
    {KEY_D,"D","D"},{KEY_F,"F","F"},{KEY_G,"G","G"},{KEY_H,"H","H"},
    {KEY_J,"J","J"},{KEY_K,"K","K"},{KEY_L,"L","L"},
    {KEY_SEMICOLON,"SEMI",";"},{KEY_APOSTROPHE,"APOS","'"},
    {KEY_BACKSLASH,"BSLASH","\\"},
    {KEY_LEFTSHIFT,"LSHIFT","SHF"},{KEY_Z,"Z","Z"},{KEY_X,"X","X"},
    {KEY_C,"C","C"},{KEY_V,"V","V"},{KEY_B,"B","B"},{KEY_N,"N","N"},
    {KEY_M,"M","M"},{KEY_COMMA,"COMMA",","},{KEY_DOT,"DOT","."},
    {KEY_SLASH,"SLASH","/"},{KEY_RIGHTSHIFT,"RSHIFT","SHF"},
    {KEY_LEFTCTRL,"LCTRL","CTL"},{KEY_LEFTALT,"LALT","ALT"},
    {KEY_SPACE,"SPACE","SPC"},{KEY_RIGHTALT,"RALT","ALT"},
    {KEY_RIGHTCTRL,"RCTRL","CTL"},
    {KEY_UP,"UP","UP"},{KEY_DOWN,"DOWN","DN"},{KEY_LEFT,"LEFT","LT"},
    {KEY_RIGHT,"RIGHT","RT"},
    {KEY_F1,"F1","F1"},{KEY_F2,"F2","F2"},{KEY_F3,"F3","F3"},
    {KEY_F4,"F4","F4"},{KEY_F5,"F5","F5"},{KEY_F6,"F6","F6"},
    {KEY_F7,"F7","F7"},{KEY_F8,"F8","F8"},{KEY_F9,"F9","F9"},
    {KEY_F10,"F10","F10"},{KEY_F11,"F11","F11"},{KEY_F12,"F12","F12"},
    {KEY_GRAVE,"GRAVE","`"},{KEY_DELETE,"DEL","DEL"},{KEY_HOME,"HOME","HOM"},
    {KEY_END,"END","END"},{KEY_PAGEUP,"PGUP","PGU"},
    {KEY_PAGEDOWN,"PGDN","PGD"},{KEY_INSERT,"INS","INS"},
    {0,NULL,NULL}
};

static const char *input_key_name(int c) {
    for (int i=0; input_ktab[i].name; i++) if (input_ktab[i].code==c) return input_ktab[i].name;
    return NULL;
}
static const char *input_key_sname(int c) {
    for (int i=0; input_ktab[i].name; i++) if (input_ktab[i].code==c) return input_ktab[i].sname;
    return NULL;
}

/* ── Keyboard layout ────────────────────────────────────────────────────── */
typedef struct { int col, row, w, code; } LKey;
static const LKey input_kblayout[] = {
    {0,0,2,KEY_ESC},{2,0,2,KEY_1},{4,0,2,KEY_2},{6,0,2,KEY_3},{8,0,2,KEY_4},
    {10,0,2,KEY_5},{12,0,2,KEY_6},{14,0,2,KEY_7},{16,0,2,KEY_8},{18,0,2,KEY_9},
    {20,0,2,KEY_0},{22,0,2,KEY_MINUS},{24,0,2,KEY_EQUAL},{26,0,2,KEY_BACKSPACE},
    {0,1,2,KEY_TAB},{2,1,2,KEY_Q},{4,1,2,KEY_W},{6,1,2,KEY_E},{8,1,2,KEY_R},
    {10,1,2,KEY_T},{12,1,2,KEY_Y},{14,1,2,KEY_U},{16,1,2,KEY_I},{18,1,2,KEY_O},
    {20,1,2,KEY_P},{22,1,2,KEY_LEFTBRACE},{24,1,2,KEY_RIGHTBRACE},{26,1,2,KEY_ENTER},
    {0,2,3,KEY_CAPSLOCK},{3,2,2,KEY_A},{5,2,2,KEY_S},{7,2,2,KEY_D},{9,2,2,KEY_F},
    {11,2,2,KEY_G},{13,2,2,KEY_H},{15,2,2,KEY_J},{17,2,2,KEY_K},{19,2,2,KEY_L},
    {21,2,2,KEY_SEMICOLON},{23,2,2,KEY_APOSTROPHE},{25,2,3,KEY_BACKSLASH},
    {0,3,3,KEY_LEFTSHIFT},{3,3,2,KEY_Z},{5,3,2,KEY_X},{7,3,2,KEY_C},{9,3,2,KEY_V},
    {11,3,2,KEY_B},{13,3,2,KEY_N},{15,3,2,KEY_M},{17,3,2,KEY_COMMA},{19,3,2,KEY_DOT},
    {21,3,2,KEY_SLASH},{23,3,5,KEY_RIGHTSHIFT},
    {0,4,3,KEY_LEFTCTRL},{3,4,3,KEY_LEFTALT},{6,4,16,KEY_SPACE},
    {22,4,3,KEY_RIGHTALT},{25,4,3,KEY_RIGHTCTRL},
    {-1,-1,-1,-1}
};

/* ── Device helpers ─────────────────────────────────────────────────────── */
/* Classification, the touchscreen exclusion and the /dev/input/event* walk are
 * common/input_scan.c's.  This list only says what is there — the testers
 * reopen by path — so every node input_scan() opened is closed again here. */
static void input_scan_devices(InputState *s) {
    static const int cap[INPUT_KIND_COUNT] = {
        [INPUT_KIND_KEYBOARD] = MAX_INPUT_DEV,
        [INPUT_KIND_MOUSE]    = MAX_INPUT_DEV,
        [INPUT_KIND_PAD]      = MAX_INPUT_DEV,
    };
    InputNode nodes[MAX_INPUT_DEV];
    int n = input_scan(nodes, 0, MAX_INPUT_DEV, cap);
    s->dev_cnt=0; s->kbd_idx=s->mou_idx=s->pad_idx=-1;
    memset(s->kind_cnt, 0, sizeof(s->kind_cnt));
    for (int i=0; i<n; i++) {
        const InputNode *nd=&nodes[i];
        close(nd->fd);
        DevType t = nd->kind==INPUT_KIND_KEYBOARD ? DEV_KEYBOARD
                  : nd->kind==INPUT_KIND_MOUSE    ? DEV_MOUSE : DEV_GAMEPAD;
        InputDev *d=&s->devs[s->dev_cnt];
        snprintf(d->name,sizeof(d->name),"%s",nd->name[0] ? nd->name : "Unknown");
        snprintf(d->path,sizeof(d->path),"%.*s",(int)sizeof(nd->path),nd->path);
        d->ev_num=-1; sscanf(nd->path,"/dev/input/event%d",&d->ev_num);
        d->type=t; d->connected=true;
        /* Only "is there one" — the testers open every node of the kind. */
        if (t==DEV_KEYBOARD && s->kbd_idx<0) s->kbd_idx=s->dev_cnt;
        else if (t==DEV_MOUSE && s->mou_idx<0) s->mou_idx=s->dev_cnt;
        else if (t==DEV_GAMEPAD && s->pad_idx<0) s->pad_idx=s->dev_cnt;
        s->kind_cnt[t-DEV_KEYBOARD]++;
        s->dev_cnt++;
    }
}

/* A cheap fingerprint of /dev/input's event nodes — each name and inode, so a
 * node removed and recreated under the same number still changes it.  Listing
 * a directory opens no device; classifying (input_scan) opens every node, so
 * it runs only when this changed. */
static unsigned long input_node_sig(void) {
    DIR *dir = opendir("/dev/input");
    if (!dir) return 0;
    unsigned long sig = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        int num;
        if (sscanf(de->d_name, "event%d", &num) != 1) continue;
        sig += ((unsigned long)(num + 1) * 2654435761UL) ^ (unsigned long)de->d_ino;
    }
    closedir(dir);
    return sig;
}

static void input_close(InputState *s);   /* defined below */

/* Open every scanned node of kind t, all non-blocking; the tester drains each
 * one every frame, so any of two mice (or keyboards, or pads) drives it without
 * the operator having to pick one. Returns how many opened. */
static int input_open_kind(InputState *s, DevType t) {
    input_close(s);
    for (int i=0; i<s->dev_cnt && s->fd_cnt<MAX_INPUT_DEV; i++) {
        if (s->devs[i].type!=t) continue;
        int fd=open(s->devs[i].path, O_RDONLY|O_NONBLOCK);
        if (fd<0) continue;
        s->fds[s->fd_cnt]=fd;
        s->fd_dev[s->fd_cnt]=i;
        s->fd_cnt++;
    }
    return s->fd_cnt;
}

static void input_close(InputState *s) {
    for (int k=0; k<s->fd_cnt; k++)
        if (s->fds[k]>=0) close(s->fds[k]);
    s->fd_cnt=0;
    s->last_dev=-1;
}

static void input_load_axes(InputState *s, int slot, int fd) {
    memset(s->pad.amin[slot],0,sizeof(s->pad.amin[slot]));
    memset(s->pad.amax[slot],0,sizeof(s->pad.amax[slot]));
    unsigned long ab[NBITS(ABS_MAX)]={0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ab)), ab)<0) return;
    for (int a=0; a<=ABS_MAX; a++) {
        if (test_bit(a,ab)) {
            struct input_absinfo inf;
            if (ioctl(fd, EVIOCGABS(a), &inf)==0)
                { s->pad.amin[slot][a]=inf.minimum; s->pad.amax[slot][a]=inf.maximum; }
        }
    }
}

/* One line naming the node behind the last event, node first so a truncated
 * product name still says which /dev/input/event* it was. out must hold 256
 * bytes (text_truncate copies up to that much). */
static void input_src_line(const InputState *s, char *out, int max_w, int scale) {
    char raw[DEV_NAME_LEN+32];
    if (s->last_dev>=0 && s->last_dev<s->dev_cnt) {
        const InputDev *d=&s->devs[s->last_dev];
        snprintf(raw,sizeof(raw),"FROM EVENT%d: %s",d->ev_num,d->name);
    } else {
        snprintf(raw,sizeof(raw),"LISTENING ON %d NODE%s - USE ANY",
                 s->fd_cnt, s->fd_cnt==1?"":"S");
    }
    text_truncate(out, raw, max_w, scale);
}

static int input_norm_axis(int v, int mn, int mx) {
    if (mx==mn) return 0;
    int mid=(mn+mx)/2, hr=(mx-mn)/2;
    if (!hr) return 0;
    int n=((v-mid)*1000)/hr;
    return n<-1000?-1000:n>1000?1000:n;
}

static int input_norm_trig(int v, int mn, int mx) {
    if (mx==mn) return 0;
    int n=((v-mn)*1000)/(mx-mn);
    return n<0?0:n>1000?1000:n;
}

static void input_add_log(KbdState *k, const char *m) {
    if (k->log_cnt>=LOG_LINES) {
        for(int i=0;i<LOG_LINES-1;i++) memcpy(k->log[i],k->log[i+1],LOG_LINE_LEN);
        k->log_cnt=LOG_LINES-1;
    }
    strncpy(k->log[k->log_cnt],m,LOG_LINE_LEN-1);
    k->log[k->log_cnt][LOG_LINE_LEN-1]=0;
    k->log_cnt++;
}

/* ── Event processing ───────────────────────────────────────────────────── */
static void input_proc_kbd(InputState *s) {
  for (int k=0; k<s->fd_cnt; k++) {
    int ev=s->devs[s->fd_dev[k]].ev_num;
    struct input_event e;
    while (read(s->fds[k],&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type!=EV_KEY || e.code>=KEY_MAX) continue;
        s->last_dev=s->fd_dev[k];
        const char *nm=input_key_name(e.code);
        char nb[32]; if(!nm){snprintf(nb,32,"KEY_%d",e.code);nm=nb;}
        char lm[LOG_LINE_LEN];
        if (e.value==1) {
            s->kbd.held[e.code]=true; s->kbd.last_code=e.code;
            strncpy(s->kbd.last_name,nm,31); s->kbd.last_name[31]=0;
            snprintf(lm,sizeof(lm),"PRESS   %s (%d) EV%d",nm,e.code,ev);
            input_add_log(&s->kbd,lm);
        } else if (e.value==0) {
            s->kbd.held[e.code]=false;
            snprintf(lm,sizeof(lm),"RELEASE %s (%d) EV%d",nm,e.code,ev);
            input_add_log(&s->kbd,lm);
        } else if (e.value==2) {
            snprintf(lm,sizeof(lm),"REPEAT  %s (%d) EV%d",nm,e.code,ev);
            input_add_log(&s->kbd,lm);
        }
    }
  }
}

static void input_proc_mouse(const Framebuffer *fb, InputState *s) {
  for (int k=0; k<s->fd_cnt; k++) {
    struct input_event e;
    while (read(s->fds[k],&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type==EV_REL || e.type==EV_KEY) s->last_dev=s->fd_dev[k];
        if (e.type==EV_REL) {
            if (e.code==REL_X) {
                s->mou.cx+=e.value;
                if(s->mou.cx<0) s->mou.cx=0;
                if(s->mou.cx>=(int)fb->width) s->mou.cx=(int)fb->width-1;
            } else if (e.code==REL_Y) {
                s->mou.cy+=e.value;
                if(s->mou.cy<0) s->mou.cy=0;
                if(s->mou.cy>=(int)fb->height) s->mou.cy=(int)fb->height-1;
            } else if (e.code==REL_WHEEL) {
                s->mou.scroll+=e.value; s->mou.scroll_t=get_time_ms();
            }
        } else if (e.type==EV_KEY) {
            bool p=(e.value!=0);
            if (e.code==BTN_LEFT) s->mou.bl=p;
            else if (e.code==BTN_MIDDLE) s->mou.bm=p;
            else if (e.code==BTN_RIGHT) s->mou.br=p;
        }
    }
  }
}

static void input_proc_pad(InputState *s) {
  for (int k=0; k<s->fd_cnt; k++) {
    const int *mn=s->pad.amin[k], *mx=s->pad.amax[k];
    struct input_event e;
    while (read(s->fds[k],&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type==EV_ABS || e.type==EV_KEY) s->last_dev=s->fd_dev[k];
        if (e.type==EV_ABS) {
            int c=e.code;
            if (c>ABS_MAX) continue;
            if (c==ABS_X) s->pad.lx=input_norm_axis(e.value,mn[c],mx[c]);
            else if (c==ABS_Y) s->pad.ly=input_norm_axis(e.value,mn[c],mx[c]);
            else if (c==ABS_RX||c==ABS_Z) s->pad.rx=input_norm_axis(e.value,mn[c],mx[c]);
            else if (c==ABS_RY||c==ABS_RZ) s->pad.ry=input_norm_axis(e.value,mn[c],mx[c]);
            else if (c==ABS_HAT0X) s->pad.dx=e.value>0?1:e.value<0?-1:0;
            else if (c==ABS_HAT0Y) s->pad.dy=e.value>0?1:e.value<0?-1:0;
            else if (c==ABS_BRAKE) s->pad.tl=input_norm_trig(e.value,mn[c],mx[c]);
            else if (c==ABS_GAS) s->pad.tr=input_norm_trig(e.value,mn[c],mx[c]);
        } else if (e.type==EV_KEY) {
            int bi=-1;
            if (e.code>=BTN_GAMEPAD && e.code<BTN_GAMEPAD+16) bi=e.code-BTN_GAMEPAD;
            else if (e.code>=BTN_SOUTH && e.code<=BTN_THUMBR) bi=e.code-BTN_SOUTH;
            else if (e.code>=BTN_TRIGGER && e.code<BTN_TRIGGER+16) bi=e.code-BTN_TRIGGER;
            if (bi>=0 && bi<16) {
                s->pad.btns[bi]=(e.value!=0);
                if (bi>=s->pad.btn_cnt) s->pad.btn_cnt=bi+1;
            }
            if (e.code==BTN_TL) s->pad.tl=e.value?1000:0;
            if (e.code==BTN_TR) s->pad.tr=e.value?1000:0;
        }
    }
  }
}

/* ── Layout ─────────────────────────────────────────────────────────────── */

#define INPUT_BTN_H       40
#define INPUT_BTN_GAP     10
#define INPUT_BTN_MAX_W   200
/* The top band, for the touch tools: a section header and one row of
 * buttons (26 + 44), then the gap a section leaves before the next (20). */
#define INPUT_TOUCH_H     90

static Button *const test_btns[3] = { &input_btn_ktest, &input_btn_mtest, &input_btn_gtest };
static const char *const test_labels[3] = { "KBD TEST", "MOUSE TEST", "PAD TEST" };

/* The touch tools' row: four slots, so each button keeps its place as the rest
 * join.  A NULL slot is not placed, drawn or counted. */
enum { TOUCH_SLOT_CALIB, TOUCH_SLOT_DIAG, TOUCH_SLOT_MULTI, TOUCH_SLOT_RESET, TOUCH_SLOTS };
static Button *const touch_btns[TOUCH_SLOTS] = {
    [TOUCH_SLOT_MULTI] = &input_btn_multitouch,
};
static const char *const touch_labels[TOUCH_SLOTS] = {
    [TOUCH_SLOT_MULTI] = "MULTI-TOUCH",
};

static int sec_touch_y;       /* the touch tools' section header */
static int touch_scale;       /* their row's text scale, see layout */
static int sec_test_y;        /* the testers' section header */
static int count_y;           /* the "N FOUND" line under each button */
static int test_scale;        /* the row's text scale, see layout */

/* Re-run whenever the logical screen changes (rebuild_ui()).  Each row shares
 * its width out among its slots — a quarter of the content for the touch tools,
 * a third for the testers — up to INPUT_BTN_MAX_W, centred under its header. */
static void input_page_layout(void) {
    sec_touch_y = CONTENT_Y + 2;
    {
        int by = sec_touch_y + 26;
        int bw = (CONTENT_WIDTH - (TOUCH_SLOTS - 1) * INPUT_BTN_GAP) / TOUCH_SLOTS;
        if (bw > INPUT_BTN_MAX_W) bw = INPUT_BTN_MAX_W;
        int sx = CONTENT_LEFT + (CONTENT_WIDTH - (TOUCH_SLOTS * bw
                                 + (TOUCH_SLOTS - 1) * INPUT_BTN_GAP)) / 2;
        /* As the testers' row: one scale for the whole row. */
        touch_scale = 2;
        for (int i = 0; i < TOUCH_SLOTS; i++)
            if (touch_labels[i] && text_measure_width(touch_labels[i], 2) > bw - 8)
                touch_scale = 1;
        for (int i = 0; i < TOUCH_SLOTS; i++)
            if (touch_btns[i])
                button_init_full(touch_btns[i], sx + i * (bw + INPUT_BTN_GAP), by, bw,
                                 INPUT_BTN_H, touch_labels[i],
                                 BTN_COLOR_PRIMARY, COLOR_WHITE, RGB(0,200,80), touch_scale);
    }
    sec_test_y = CONTENT_Y + 2 + INPUT_TOUCH_H;
    int by = sec_test_y + 26;
    int bw = (CONTENT_WIDTH - 2 * INPUT_BTN_GAP) / 3;
    if (bw > INPUT_BTN_MAX_W) bw = INPUT_BTN_MAX_W;
    int sx = CONTENT_LEFT + (CONTENT_WIDTH - (3 * bw + 2 * INPUT_BTN_GAP)) / 2;
    /* Scale 2 where it fits; a portrait third is too narrow for "MOUSE TEST"
     * at scale 2, so the whole row drops to 1 rather than one button alone. */
    test_scale = 2;
    for (int i = 0; i < 3; i++)
        if (text_measure_width(test_labels[i], 2) > bw - 8) test_scale = 1;
    for (int i = 0; i < 3; i++)
        button_init_full(test_btns[i], sx + i * (bw + INPUT_BTN_GAP), by, bw,
                         INPUT_BTN_H, test_labels[i],
                         BTN_COLOR_PRIMARY, COLOR_WHITE, RGB(0,200,80), test_scale);
    count_y = by + INPUT_BTN_H + 8;
    button_init_full(&input_btn_kback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
    button_init_full(&input_btn_mback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
    button_init_full(&input_btn_gback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);

    /* ⚠️ THE RECEIPT, in the settings stack's shape.  Everything hangs off
     * CONTENT_Y, which comes from a per-unit touch inset, so a button pushed
     * past the touchable rect looks perfect in a screenshot and is dead to a
     * finger.  The bottom is the count line under the buttons; the right edge
     * the last button as placed in either row.  A label wider than its button
     * is cut, and both rows' labels are counted. */
    {
        int bottom = count_y + 8 - CONTENT_Y;
        int right  = input_btn_gtest.x + input_btn_gtest.width;
        int clipped = 0;
        for (int i = 0; i < 3; i++)
            if (text_measure_width(test_labels[i], test_scale) > test_btns[i]->width - 8)
                clipped++;
        for (int i = 0; i < TOUCH_SLOTS; i++) {
            if (!touch_btns[i]) continue;
            if (touch_btns[i]->x + touch_btns[i]->width > right)
                right = touch_btns[i]->x + touch_btns[i]->width;
            if (text_measure_width(touch_labels[i], touch_scale) > touch_btns[i]->width - 8)
                clipped++;
        }
        const char *verdict = bottom > CONTENT_H     ? "⚠ PAST CONTENT BOTTOM"
                            : right  > CONTENT_RIGHT ? "⚠ PAST CONTENT RIGHT"
                            : "fits";
        printf("control_panel: input stack %s — bottom +%d of CONTENT_H %d, "
               "right %d of CONTENT_RIGHT %d, %d label(s) cut, touch band %d px "
               "at scale %d, testers at scale %d (safe %dx%d, %s)\n",
               verdict, bottom, CONTENT_H, right, CONTENT_RIGHT, clipped,
               INPUT_TOUCH_H, touch_scale, test_scale,
               SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT,
               CONTENT_WIDTH < 600 ? "portrait" : "landscape");
    }
}

/* ── Draw: the page ─────────────────────────────────────────────────────── */

/* Derived from the scan every time, never cached: a tester's button is
 * disabled exactly while no node of its kind is present.  Button.disabled
 * makes it grey and button_update() ignores it, so input() needs no guard. */
static void input_sync_disabled(void) {
    const InputState *s = &input_state;
    input_btn_ktest.disabled = s->kbd_idx < 0;
    input_btn_mtest.disabled = s->mou_idx < 0;
    input_btn_gtest.disabled = s->pad_idx < 0;
}

static void input_page_draw(Framebuffer *fb) {
    const InputState *s = &input_state;
    input_sync_disabled();
    draw_section_header(fb, sec_touch_y, "TOUCH");
    for (int i = 0; i < TOUCH_SLOTS; i++)
        if (touch_btns[i]) button_draw(fb, touch_btns[i]);
    draw_section_header(fb, sec_test_y, "KEYBOARD / MOUSE / PAD");
    for (int i = 0; i < 3; i++) {
        const Button *b = test_btns[i];
        button_draw(fb, test_btns[i]);
        char cnt[24];
        if (s->kind_cnt[i] > 0) snprintf(cnt, sizeof(cnt), "%d FOUND", s->kind_cnt[i]);
        else                    snprintf(cnt, sizeof(cnt), "NONE FOUND");
        fb_draw_text(fb, b->x + (b->width - text_measure_width(cnt, 1)) / 2, count_y,
                     cnt, s->kind_cnt[i] > 0 ? COLOR_LABEL : COLOR_DISABLED, 1);
    }
}


/* ── Draw: Keyboard fullscreen ──────────────────────────────────────── */
static void draw_kbd_test(Framebuffer *fb, InputState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &input_btn_kback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "KEYBOARD TEST", COLOR_WHITE, 3);
    char inf[96];
    if (s->kbd.last_code>0 && s->last_dev>=0)
        snprintf(inf,sizeof(inf),"LAST KEY: %s (CODE %d) - EVENT%d", s->kbd.last_name,
                 s->kbd.last_code, s->devs[s->last_dev].ev_num);
    else if (s->kbd.last_code>0)
        snprintf(inf,sizeof(inf),"LAST KEY: %s (CODE %d)", s->kbd.last_name, s->kbd.last_code);
    else snprintf(inf,sizeof(inf),"PRESS ANY KEY ON ANY USB KEYBOARD");
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+52, inf, COLOR_CYAN, 2);

    int kx=SCREEN_SAFE_LEFT+20, ky=SCREEN_SAFE_TOP+72, hu=26, kh=32, g=2;
    for (int i=0; input_kblayout[i].code>=0; i++) {
        const LKey *k=&input_kblayout[i];
        int x=kx+k->col*hu, y=ky+k->row*(kh+g), w=k->w*hu-g;
        bool h=(k->code<KEY_MAX)?s->kbd.held[k->code]:false;
        fb_fill_rounded_rect(fb,x,y,w,kh,3,h?INPUT_COLOR_KEY_DN:INPUT_COLOR_KEY_UP);
        fb_draw_rounded_rect(fb,x,y,w,kh,3,INPUT_COLOR_KEY_BD);
        const char *l=input_key_sname(k->code);
        if (l) {
            int tw=text_measure_width(l,1);
            fb_draw_text(fb,x+(w-tw)/2,y+(kh-8)/2,l,h?COLOR_WHITE:INPUT_COLOR_KEY_TXT,1);
        }
    }
    int lx2=SCREEN_SAFE_LEFT+20, ly2=ky+5*(kh+g)+8;
    int lw2=SCREEN_SAFE_WIDTH-40, lh2=SCREEN_SAFE_BOTTOM-ly2-10;
    if (lh2<30) lh2=30;
    fb_fill_rounded_rect(fb,lx2,ly2,lw2,lh2,4,INPUT_COLOR_LOG_BG);
    fb_draw_rounded_rect(fb,lx2,ly2,lw2,lh2,4,INPUT_COLOR_PANEL_BD);
    fb_draw_text(fb,lx2+8,ly2+4,"EVENT LOG:",INPUT_COLOR_DIM,1);
    {
        int sx=lx2+8+text_measure_width("EVENT LOG:",1)+16;
        char src[256]; input_src_line(s, src, lx2+lw2-8-sx, 1);
        fb_draw_text(fb,sx,ly2+4,src,COLOR_CYAN,1);
    }
    int lny=ly2+16, llh=13, ml=(lh2-20)/llh;
    if(ml>LOG_LINES) ml=LOG_LINES;
    if(ml<0) ml=0;
    int st=s->kbd.log_cnt-ml; if(st<0) st=0;
    for (int i=st; i<s->kbd.log_cnt; i++)
        fb_draw_text(fb,lx2+8,lny+(i-st)*llh,s->kbd.log[i],INPUT_COLOR_LOG_TXT,1);
}

/* ── Draw: Mouse fullscreen ─────────────────────────────────────────── */
static void draw_mou_test(Framebuffer *fb, InputState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &input_btn_mback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "MOUSE TEST", COLOR_WHITE, 3);
    int sw=screen_base_width, sh=screen_base_height;
    int ax=SCREEN_SAFE_LEFT+20, ay=SCREEN_SAFE_TOP+55, aw=sw-240, ah=sh-ay-20;
    fb_fill_rounded_rect(fb,ax,ay,aw,ah,6,RGB(15,15,25));
    fb_draw_rounded_rect(fb,ax,ay,aw,ah,6,INPUT_COLOR_PANEL_BD);
    for(int gx=ax+50;gx<ax+aw;gx+=50) fb_draw_line(fb,gx,ay+1,gx,ay+ah-2,RGB(25,25,35));
    for(int gy=ay+50;gy<ay+ah;gy+=50) fb_draw_line(fb,ax+1,gy,ax+aw-2,gy,RGB(25,25,35));

    int cx=s->mou.cx, cy=s->mou.cy;
    int dx=cx<ax+2?ax+2:cx>=ax+aw-2?ax+aw-3:cx;
    int dy=cy<ay+2?ay+2:cy>=ay+ah-2?ay+ah-3:cy;
    fb_draw_line(fb,dx-15,dy,dx+15,dy,INPUT_COLOR_CURSOR_C);
    fb_draw_line(fb,dx,dy-15,dx,dy+15,INPUT_COLOR_CURSOR_C);
    fb_fill_circle(fb,dx,dy,3,INPUT_COLOR_CURSOR_C);

    int px=ax+aw+15, pw=sw-px-15, py=ay;
    fb_draw_text(fb,px,py,"POSITION",INPUT_COLOR_HDR,2);
    char ps[32];
    snprintf(ps,32,"X: %d",cx); fb_draw_text(fb,px,py+22,ps,COLOR_WHITE,2);
    snprintf(ps,32,"Y: %d",cy); fb_draw_text(fb,px,py+42,ps,COLOR_WHITE,2);

    int bsy=py+80;
    fb_draw_text(fb,px,bsy,"BUTTONS",INPUT_COLOR_HDR,2);
    struct { bool h; const char *l; int o; } mb[3]={{s->mou.bl,"L",0},{s->mou.bm,"M",45},{s->mou.br,"R",90}};
    for(int b=0;b<3;b++) {
        uint32_t c=mb[b].h?INPUT_COLOR_MBTN_ON:INPUT_COLOR_MBTN_OFF;
        int bx=px+20+mb[b].o, by2=bsy+40;
        fb_fill_circle(fb,bx,by2,14,c); fb_draw_circle(fb,bx,by2,14,INPUT_COLOR_PANEL_BD);
        fb_draw_text(fb,bx-4,by2+18,mb[b].l,COLOR_LABEL,2);
    }

    int ssy=bsy+95;
    fb_draw_text(fb,px,ssy,"SCROLL",INPUT_COLOR_HDR,2);
    snprintf(ps,32,"WHEEL: %d",s->mou.scroll);
    fb_draw_text(fb,px,ssy+22,ps,COLOR_WHITE,2);
    uint32_t now=get_time_ms();
    if (now-s->mou.scroll_t<300) fb_fill_circle(fb,px+pw-20,ssy+10,8,INPUT_COLOR_SCROLL);
    int brx=px, bry=ssy+45, brw=pw-10<20?20:pw-10, brh=20;
    fb_fill_rect(fb,brx,bry,brw,brh,RGB(40,40,40));
    int sv=s->mou.scroll; if(sv>50) sv=50; if(sv<-50) sv=-50;
    int ix=brx+brw/2+(sv*brw/100);
    fb_fill_rect(fb,ix-3,bry,6,brh,INPUT_COLOR_SCROLL);
    fb_draw_line(fb,brx+brw/2,bry,brx+brw/2,bry+brh,INPUT_COLOR_DIM);

    /* Which node moved the crosshair: every mouse node is open at once. */
    int sry=bry+brh+20;
    fb_draw_text(fb,px,sry,"SOURCE",INPUT_COLOR_HDR,2);
    if (s->last_dev>=0 && s->last_dev<s->dev_cnt) {
        const InputDev *d=&s->devs[s->last_dev];
        snprintf(ps,32,"EVENT%d",d->ev_num);
        fb_draw_text(fb,px,sry+22,ps,COLOR_WHITE,2);
        char nm[256]; text_truncate(nm, d->name, pw-5, 1);
        fb_draw_text(fb,px,sry+44,nm,COLOR_LABEL,1);
    } else {
        fb_draw_text(fb,px,sry+22,"NONE YET",INPUT_COLOR_DIM,2);
    }
    snprintf(ps,32,"%d MOUSE NODE%s OPEN",s->fd_cnt,s->fd_cnt==1?"":"S");
    fb_draw_text(fb,px,sry+58,ps,INPUT_COLOR_DIM,1);
    text_draw_centered(fb,sw/2,sh-15,"MOVE ANY MOUSE TO CONTROL CROSSHAIR",INPUT_COLOR_DIM,1);
}

/* ── Draw: Gamepad helpers ──────────────────────────────────────────── */
static void input_draw_stick(Framebuffer *fb, int cx, int cy, int r,
                           int vx, int vy, const char *label) {
    fb_fill_circle(fb,cx,cy,r,INPUT_COLOR_STICK_BG);
    fb_draw_circle(fb,cx,cy,r,INPUT_COLOR_PANEL_BD);
    fb_draw_line(fb,cx-r,cy,cx+r,cy,RGB(40,40,55));
    fb_draw_line(fb,cx,cy-r,cx,cy+r,RGB(40,40,55));
    int dpx=cx+(vx*(r-6))/1000, dpy=cy+(vy*(r-6))/1000;
    fb_fill_circle(fb,dpx,dpy,6,INPUT_COLOR_STICK_DOT);
    fb_draw_circle(fb,dpx,dpy,6,COLOR_WHITE);
    text_draw_centered(fb,cx,cy+r+14,label,COLOR_LABEL,2);
}

static void input_draw_dpad(Framebuffer *fb, int cx, int cy, int sz, int dx, int dy) {
    int arm=sz/3, hf=arm/2;
    fb_fill_rect(fb,cx-sz/2,cy-hf,sz,arm,RGB(50,50,60));
    fb_fill_rect(fb,cx-hf,cy-sz/2,arm,sz,RGB(50,50,60));
    if(dx<0) fb_fill_rect(fb,cx-sz/2,cy-hf,arm,arm,INPUT_COLOR_PAD_ON);
    if(dx>0) fb_fill_rect(fb,cx+sz/2-arm,cy-hf,arm,arm,INPUT_COLOR_PAD_ON);
    if(dy<0) fb_fill_rect(fb,cx-hf,cy-sz/2,arm,arm,INPUT_COLOR_PAD_ON);
    if(dy>0) fb_fill_rect(fb,cx-hf,cy+sz/2-arm,arm,arm,INPUT_COLOR_PAD_ON);
    fb_draw_rect(fb,cx-sz/2,cy-hf,sz,arm,INPUT_COLOR_PANEL_BD);
    fb_draw_rect(fb,cx-hf,cy-sz/2,arm,sz,INPUT_COLOR_PANEL_BD);
    text_draw_centered(fb,cx,cy+sz/2+14,"D-PAD",COLOR_LABEL,2);
}

/* ── Draw: Gamepad fullscreen ───────────────────────────────────────── */
static void draw_pad_test(Framebuffer *fb, InputState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &input_btn_gback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "GAMEPAD TEST", COLOR_WHITE, 3);
    int sw=screen_base_width, sr=55;
    int lcx=SCREEN_SAFE_LEFT+30+sr, lcy=SCREEN_SAFE_TOP+80+sr;
    input_draw_stick(fb,lcx,lcy,sr,s->pad.lx,s->pad.ly,"LEFT STICK");
    input_draw_dpad(fb,lcx,lcy+sr+70,70,s->pad.dx,s->pad.dy);
    int rcx=sw-SCREEN_SAFE_LEFT-30-sr;
    input_draw_stick(fb,rcx,lcy,sr,s->pad.rx,s->pad.ry,"RIGHT STICK");

    int bcnt=s->pad.btn_cnt<1?8:s->pad.btn_cnt;
    if(bcnt>16) bcnt=16;
    int cols=4, bsz=28, bgap=6;
    int bax=lcx+sr+40, bay=SCREEN_SAFE_TOP+75;
    for(int i=0;i<bcnt;i++) {
        int col=i%cols, row=i/cols;
        int bx=bax+col*(bsz+bgap)+bsz/2;
        int by=bay+row*(bsz+bgap)+bsz/2;
        uint32_t c=s->pad.btns[i]?INPUT_COLOR_PAD_ON:INPUT_COLOR_PAD_OFF;
        fb_fill_circle(fb,bx,by,bsz/2,c);
        fb_draw_circle(fb,bx,by,bsz/2,INPUT_COLOR_PANEL_BD);
        char bn[4]; snprintf(bn,4,"%d",i);
        int tw=text_measure_width(bn,1);
        fb_draw_text(fb,bx-tw/2,by-4,bn,COLOR_WHITE,1);
    }
    fb_draw_text(fb,bax,bay-16,"BUTTONS",INPUT_COLOR_HDR,2);

    int trw=150, trh=18, try2=SCREEN_SAFE_BOTTOM-80;
    int trlx=(sw/2)-trw-20, trrx=(sw/2)+20;
    fb_draw_text(fb,trlx,try2-16,"LT",COLOR_LABEL,1);
    fb_fill_rect(fb,trlx,try2,trw,trh,INPUT_COLOR_TRIG_BG);
    if(s->pad.tl>0) fb_fill_rect(fb,trlx,try2,(s->pad.tl*trw)/1000,trh,INPUT_COLOR_TRIG_FILL);
    fb_draw_rect(fb,trlx,try2,trw,trh,INPUT_COLOR_PANEL_BD);
    char tv[16]; snprintf(tv,16,"%d%%",s->pad.tl/10);
    fb_draw_text(fb,trlx+trw+8,try2+4,tv,COLOR_WHITE,1);

    fb_draw_text(fb,trrx,try2-16,"RT",COLOR_LABEL,1);
    fb_fill_rect(fb,trrx,try2,trw,trh,INPUT_COLOR_TRIG_BG);
    if(s->pad.tr>0) fb_fill_rect(fb,trrx,try2,(s->pad.tr*trw)/1000,trh,INPUT_COLOR_TRIG_FILL);
    fb_draw_rect(fb,trrx,try2,trw,trh,INPUT_COLOR_PANEL_BD);
    snprintf(tv,16,"%d%%",s->pad.tr/10);
    fb_draw_text(fb,trrx+trw+8,try2+4,tv,COLOR_WHITE,1);

    int rvx=bax, rvy=bay+(bcnt/cols+1)*(bsz+bgap)+20;
    fb_draw_text(fb,rvx,rvy,"RAW AXES",INPUT_COLOR_HDR,2);
    char rv[48];
    snprintf(rv,48,"LX:%+5d LY:%+5d",s->pad.lx,s->pad.ly);
    fb_draw_text(fb,rvx,rvy+20,rv,COLOR_LABEL,1);
    snprintf(rv,48,"RX:%+5d RY:%+5d",s->pad.rx,s->pad.ry);
    fb_draw_text(fb,rvx,rvy+34,rv,COLOR_LABEL,1);
    snprintf(rv,48,"DX:%+2d DY:%+2d",s->pad.dx,s->pad.dy);
    fb_draw_text(fb,rvx,rvy+48,rv,COLOR_LABEL,1);

    /* Every pad node is open at once; name the one that sent the last event. */
    char src[256]; input_src_line(s, src, SCREEN_SAFE_WIDTH-20, 2);
    text_draw_centered(fb,sw/2,SCREEN_SAFE_BOTTOM-30,src,COLOR_CYAN,2);
}

/* ── Input ──────────────────────────────────────────────────────────────── */

static void input_page_load(const Config *cfg) {
    (void)cfg;
    InputState *s = &input_state;
    s->scr = INPUT_SCR_MAIN;
    s->fd_cnt = 0;
    s->last_dev = -1;
    s->node_sig = input_node_sig();
    input_scan_devices(s);
}

/* A device plugged in or pulled while the page was closed shows on opening:
 * the next input() lists /dev/input at once and classifies afresh. */
static void input_page_enter(void) {
    input_state.poll_ms = 0;
    input_state.node_sig = ~0UL;
}

/* Main screen only: a tester is queued here and run by
 * input_page_run_fullscreen().  The hot-plug poll repaints only when the count
 * of some kind changed — a node the testers would not open changes nothing. */
static CpPageResult input_page_input(Config *cfg, int tx, int ty,
                                     bool touching, uint32_t now) {
    (void)cfg;
    InputState *state = &input_state;
    CpPageResult act = CP_PAGE_IDLE;

    if (state->poll_ms == 0 || now - state->poll_ms >= POLL_MS) {
        state->poll_ms = now;
        unsigned long sig = input_node_sig();
        if (sig != state->node_sig) {
            int prev[3];
            memcpy(prev, state->kind_cnt, sizeof(prev));
            state->node_sig = sig;
            input_scan_devices(state);
            if (memcmp(prev, state->kind_cnt, sizeof(prev)) != 0)
                act = CP_PAGE_REDRAW;
        }
    }

    input_sync_disabled();
    if (button_update(&input_btn_multitouch, tx, ty, touching, now)) {
        state->scr = INPUT_SCR_MULTITOUCH;
        act = CP_PAGE_FULLSCREEN;
    }
    if (button_update(&input_btn_ktest, tx, ty, touching, now)) {
        memset(&state->kbd, 0, sizeof(state->kbd));
        if (input_open_kind(state, DEV_KEYBOARD) > 0) {
            state->scr = INPUT_SCR_KEYBOARD;
            act = CP_PAGE_FULLSCREEN;
        }
    }
    if (button_update(&input_btn_mtest, tx, ty, touching, now)) {
        memset(&state->mou, 0, sizeof(state->mou));
        if (input_open_kind(state, DEV_MOUSE) > 0) {
            state->scr = INPUT_SCR_MOUSE;
            act = CP_PAGE_FULLSCREEN;
        }
    }
    if (button_update(&input_btn_gtest, tx, ty, touching, now)) {
        memset(&state->pad, 0, sizeof(state->pad));
        state->pad.btn_cnt = 8;
        if (input_open_kind(state, DEV_GAMEPAD) > 0) {
            for (int k = 0; k < state->fd_cnt; k++)
                input_load_axes(state, k, state->fds[k]);
            state->scr = INPUT_SCR_GAMEPAD;
            act = CP_PAGE_FULLSCREEN;
        }
    }
    return act;
}

/* ── Full-screen: the multi-touch test ──────────────────────────────────── */

/* The band along the top edge whose right-hand 100 px is the exit tap. */
#define MT_EXIT_H 36

/* Multi-touch: one dot per MT slot, read straight off the evdev fd because
 * TouchInput tracks a single pointer. Slots arrive only from a driver that
 * reports ABS_MT_SLOT; the legacy BTN_TOUCH still drives the exit tap. */
#define MT_SLOTS 2
static void test_multitouch(Framebuffer *fb, TouchInput *touch) {
    static const uint32_t slot_col[MT_SLOTS] = { RGB(255,200,0), RGB(0,200,255) };
    int calib_ok = (touch_load_calibration(touch, CALIB_FILE) == 0);
    if (calib_ok) touch_enable_calibration(touch, true);
    int rx[MT_SLOTS] = {0}, ry[MT_SLOTS] = {0};
    bool on[MT_SLOTS] = {false};
    int slot = 0, lx = 0, ly = 0, max_fingers = 0;
    bool seen_mt = false, running = true;

    touch_drain_events(touch);
    while (running) {
        fb_clear(fb, RGB(20,20,30));
        char hdr[96]; snprintf(hdr, sizeof(hdr),
            "Multi-touch  |  MT slots: %s  |  max fingers: %d  |  Calib: %s",
            seen_mt ? "yes" : "none yet", max_fingers, calib_ok ? "ON" : "OFF");
        fb_draw_text(fb, 4, 2, hdr, COLOR_WHITE, 1);
        fb_draw_text(fb, fb->width - 160, 2, "[EXIT: top-right]", RGB(180,80,80), 1);
        int fingers = 0;
        for (int i = 0; i < MT_SLOTS; i++) {
            if (!on[i]) continue;
            int x = rx[i], y = ry[i];
            touch_map_raw(touch, &x, &y);
            fb_fill_circle(fb, x, y, 28, slot_col[i]);
            char lbl[48]; snprintf(lbl, sizeof(lbl), "slot %d raw(%d,%d) scr(%d,%d)",
                                   i, rx[i], ry[i], x, y);
            fb_draw_text(fb, 4, 16 + 12 * i, lbl, slot_col[i], 1);
            fingers++;
        }
        if (fingers > max_fingers) max_fingers = fingers;
        fb_swap(fb);

        struct pollfd pfd = { .fd = touch->fd, .events = POLLIN };
        if (poll(&pfd, 1, 16) <= 0) continue;
        struct input_event ev;
        while (read(touch->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_ABS) {
                switch (ev.code) {
                case ABS_MT_SLOT: slot = ev.value; seen_mt = true; break;
                case ABS_MT_TRACKING_ID:
                    if (slot >= 0 && slot < MT_SLOTS) on[slot] = ev.value >= 0;
                    break;
                case ABS_MT_POSITION_X: if (slot >= 0 && slot < MT_SLOTS) rx[slot] = ev.value; break;
                case ABS_MT_POSITION_Y: if (slot >= 0 && slot < MT_SLOTS) ry[slot] = ev.value; break;
                case ABS_X: lx = ev.value; break;
                case ABS_Y: ly = ev.value; break;
                }
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 1) {
                int x = lx, y = ly;
                touch_map_raw(touch, &x, &y);
                if (x > (int)fb->width - 100 && y < MT_EXIT_H) running = false;
            }
            if (poll(&pfd, 1, 0) <= 0) break;
        }
    }
    touch_drain_events(touch);
    touch_enable_calibration(touch, false);
}

/* ── Full-screen: the testers ───────────────────────────────────────────── */

/* After input() returned CP_PAGE_FULLSCREEN.  Runs until the tester's BACK (or
 * a signal asking the panel to quit), then closes every node it opened: its
 * hardware is its own to clean up. */
static void input_page_run_fullscreen(Framebuffer *fb, TouchInput *touch) {
    InputState *state = &input_state;
    if (state->scr == INPUT_SCR_MULTITOUCH) {   /* its own loop and exit tap */
        test_multitouch(fb, touch);
        state->scr = INPUT_SCR_MAIN;
        return;
    }
    if (state->scr == INPUT_SCR_MOUSE) {
        state->mou.cx = (int)fb->width / 2;
        state->mou.cy = (int)fb->height / 2;
    }
    while (cp_running() && state->scr != INPUT_SCR_MAIN) {
        uint32_t now = get_time_ms();

        switch (state->scr) {
            case INPUT_SCR_KEYBOARD: input_proc_kbd(state); break;
            case INPUT_SCR_MOUSE:    input_proc_mouse(fb, state); break;
            case INPUT_SCR_GAMEPAD:  input_proc_pad(state); break;
            default: break;
        }

        switch (state->scr) {
            case INPUT_SCR_KEYBOARD: draw_kbd_test(fb, state); break;
            case INPUT_SCR_MOUSE:    draw_mou_test(fb, state); break;
            case INPUT_SCR_GAMEPAD:  draw_pad_test(fb, state); break;
            default: break;
        }
        fb_swap(fb);

        touch_poll(touch);
        TouchState ts = touch_get_state(touch);
        int tx = ts.x, ty = ts.y;
        bool touching = ts.pressed || ts.held;

        switch (state->scr) {
        case INPUT_SCR_KEYBOARD:
            if (button_update(&input_btn_kback, tx, ty, touching, now)) {
                input_close(state); state->scr = INPUT_SCR_MAIN;
            }
            break;
        case INPUT_SCR_MOUSE:
            if (button_update(&input_btn_mback, tx, ty, touching, now)) {
                input_close(state); state->scr = INPUT_SCR_MAIN;
            }
            break;
        case INPUT_SCR_GAMEPAD:
            if (button_update(&input_btn_gback, tx, ty, touching, now)) {
                input_close(state); state->scr = INPUT_SCR_MAIN;
            }
            break;
        default: break;
        }

        usleep(16000);
    }
    input_close(state);   /* a signal can end the loop inside a tester */
    state->scr = INPUT_SCR_MAIN;
}

const CpPage cp_input_page = {
    .name           = "Input",
    .icon           = "cp_input",
    .load           = input_page_load,
    .layout         = input_page_layout,
    .enter          = input_page_enter,
    .draw           = input_page_draw,
    .input          = input_page_input,
    .run_fullscreen = input_page_run_fullscreen,
};
