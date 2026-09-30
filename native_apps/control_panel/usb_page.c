/* usb_page.c — control_panel's USB page: the bus list and the input testers.
 *
 * Opened from the home grid's USB tile, and the one home for everything the
 * panel does with USB: the list of every enumerated device (read by usb_bus.c),
 * RESCAN — which also revives a port that came up dead — and the full-screen
 * keyboard, mouse and gamepad testers.  Exposed only as cp_usb_page
 * (cp_page.h); its state lives in this file.  It owns no config keys.
 *
 * A static bus costs nothing: the list is read at startup and on RESCAN, and
 * input() returns CP_PAGE_REDRAW only when that reading, or the status line
 * under it, changed.
 */
#include "cp_page.h"
#include "cp_ui.h"
#include "usb_bus.h"
#include "../common/common.h"
#include "../common/input_scan.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
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
#define USB_COLOR_KEY_UP       RGB(50, 50, 60)
#define USB_COLOR_KEY_DN       RGB(0, 180, 255)
#define USB_COLOR_KEY_BD       RGB(80, 80, 100)
#define USB_COLOR_KEY_TXT      RGB(200, 200, 200)
#define USB_COLOR_STICK_BG     RGB(40, 40, 50)
#define USB_COLOR_STICK_DOT    RGB(0, 200, 255)
#define USB_COLOR_PAD_ON       RGB(0, 220, 100)
#define USB_COLOR_PAD_OFF      RGB(60, 60, 70)
#define USB_COLOR_TRIG_BG      RGB(40, 40, 40)
#define USB_COLOR_TRIG_FILL    RGB(255, 165, 0)
#define USB_COLOR_CURSOR_C     RGB(255, 255, 0)
#define USB_COLOR_MBTN_ON      RGB(0, 200, 80)
#define USB_COLOR_MBTN_OFF     RGB(60, 60, 70)
#define USB_COLOR_SCROLL       RGB(0, 180, 255)
#define USB_COLOR_LOG_BG       RGB(15, 15, 25)
#define USB_COLOR_LOG_TXT      RGB(150, 200, 150)

/* ── Types and state ────────────────────────────────────────────────────── */

#define MAX_USB_DEV     8
#define DEV_NAME_LEN    128
#define LOG_LINES       8
#define LOG_LINE_LEN    64
#define STATUS_MS       2000   /* how long a RESCAN result stays up */

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(x)        ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)          ((x) % BITS_PER_LONG)
#define BIT_LONG(x)     ((x) / BITS_PER_LONG)
#define test_bit(b, a)  ((a[BIT_LONG(b)] >> OFF(b)) & 1)

typedef enum { DEV_UNKNOWN, DEV_KEYBOARD, DEV_MOUSE, DEV_GAMEPAD } DevType;

typedef struct {
    char name[DEV_NAME_LEN]; char path[64];
    DevType type; int ev_num; bool connected;
} USBDev;

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
    int amin[MAX_USB_DEV][ABS_MAX+1], amax[MAX_USB_DEV][ABS_MAX+1];
    int dx, dy; bool btns[16]; int btn_cnt;
    int tl, tr;
} PadSt;

typedef enum { USB_SCR_MAIN, USB_SCR_KEYBOARD, USB_SCR_MOUSE, USB_SCR_GAMEPAD } USBScreen;

typedef struct {
    USBScreen    scr;
    USBDev       devs[MAX_USB_DEV];   /* evdev nodes the testers can open */
    int          dev_cnt;
    UsbBusDev    bus[USB_BUS_MAX];    /* everything enumerated — what the list shows */
    int          bus_cnt;
    int          kbd_idx, mou_idx, pad_idx;   /* first of each kind, -1 none */
    /* A tester opens EVERY node of its kind at once: a touchpad keyboard exposes
     * its own mouse node, so "the first mouse" is often not the one in the hand. */
    int          fds[MAX_USB_DEV];
    int          fd_dev[MAX_USB_DEV];  /* devs[] index behind each fd */
    int          fd_cnt;
    int          last_dev;             /* devs[] index of the last event, -1 none */
    KbdState     kbd;
    MouseSt      mou;
    PadSt        pad;
    bool         recover_queued;       /* RESCAN found nothing: re-probe full-screen */
    char         status_msg[64];       /* a RESCAN result; "" = none */
    uint32_t     status_time_ms;
} UsbState;

static UsbState usb_state;

static Button usb_btn_rescan, usb_btn_ktest, usb_btn_mtest, usb_btn_gtest;
static Button usb_btn_kback, usb_btn_mback, usb_btn_gback;

/* ── Key table ──────────────────────────────────────────────────────────── */
typedef struct { int code; const char *name, *sname; } KeyInfo;
static const KeyInfo usb_ktab[] = {
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

static const char *usb_key_name(int c) {
    for (int i=0; usb_ktab[i].name; i++) if (usb_ktab[i].code==c) return usb_ktab[i].name;
    return NULL;
}
static const char *usb_key_sname(int c) {
    for (int i=0; usb_ktab[i].name; i++) if (usb_ktab[i].code==c) return usb_ktab[i].sname;
    return NULL;
}

/* ── Keyboard layout ────────────────────────────────────────────────────── */
typedef struct { int col, row, w, code; } LKey;
static const LKey usb_kblayout[] = {
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

/* ── USB device helpers ─────────────────────────────────────────────────── */
/* Classification, the touchscreen exclusion and the /dev/input/event* walk are
 * common/input_scan.c's.  This list only says what is there — the testers
 * reopen by path — so every node input_scan() opened is closed again here. */
static void usb_scan_devices(UsbState *s) {
    static const int cap[INPUT_KIND_COUNT] = {
        [INPUT_KIND_KEYBOARD] = MAX_USB_DEV,
        [INPUT_KIND_MOUSE]    = MAX_USB_DEV,
        [INPUT_KIND_PAD]      = MAX_USB_DEV,
    };
    InputNode nodes[MAX_USB_DEV];
    int n = input_scan(nodes, 0, MAX_USB_DEV, cap);
    s->dev_cnt=0; s->kbd_idx=s->mou_idx=s->pad_idx=-1;
    for (int i=0; i<n; i++) {
        const InputNode *nd=&nodes[i];
        close(nd->fd);
        DevType t = nd->kind==INPUT_KIND_KEYBOARD ? DEV_KEYBOARD
                  : nd->kind==INPUT_KIND_MOUSE    ? DEV_MOUSE : DEV_GAMEPAD;
        USBDev *d=&s->devs[s->dev_cnt];
        snprintf(d->name,sizeof(d->name),"%s",nd->name[0] ? nd->name : "Unknown");
        snprintf(d->path,sizeof(d->path),"%.*s",(int)sizeof(nd->path),nd->path);
        d->ev_num=-1; sscanf(nd->path,"/dev/input/event%d",&d->ev_num);
        d->type=t; d->connected=true;
        /* Only "is there one" — the testers open every node of the kind. */
        if (t==DEV_KEYBOARD && s->kbd_idx<0) s->kbd_idx=s->dev_cnt;
        else if (t==DEV_MOUSE && s->mou_idx<0) s->mou_idx=s->dev_cnt;
        else if (t==DEV_GAMEPAD && s->pad_idx<0) s->pad_idx=s->dev_cnt;
        s->dev_cnt++;
    }
    s->bus_cnt = usb_bus_scan(USB_BUS_ROOT, s->bus, USB_BUS_MAX);
}

static void usb_close(UsbState *s);   /* defined below; used by the recovery path */

/* Bring a dead USB port back, then re-enumerate.
 *
 * ⚠️ Recovery is TWO halves and this app only ever had the second.
 * usb_scan_devices() re-open()s /dev/input/event*, so it finds a node the kernel
 * has already created and cannot create one. On a port that came up dead no
 * node exists, so tapping RESCAN could never satisfy the hint this very page
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
 * repaint until this returns) and close our own fd on the way in. */
static bool usb_recover_port(Framebuffer *fb, UsbState *s) {
    if (access(USB_HOST_INIT, X_OK) != 0) {
        snprintf(s->status_msg, sizeof(s->status_msg),
                 "USB-HOST SCRIPT NOT INSTALLED");
        s->status_time_ms = get_time_ms();
        return false;
    }

    usb_close(s);   /* the rebind would invalidate it anyway */

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
        snprintf(s->status_msg, sizeof(s->status_msg), "FORK FAILED");
        s->status_time_ms = get_time_ms();
        return false;
    }
    if (pid == 0) {
        execl(USB_HOST_INIT, "usb-host", "recover", (char *)NULL);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;   /* a signal must not orphan the child */

    usb_scan_devices(s);
    int found = usb_bus_peripherals(s->bus, s->bus_cnt);

    if (found > 0)
        snprintf(s->status_msg, sizeof(s->status_msg),
                 "PORT RECOVERED - %d DEVICE(S)", found);
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
        snprintf(s->status_msg, sizeof(s->status_msg),
                 "COULD NOT RUN USB-HOST");
    else
        snprintf(s->status_msg, sizeof(s->status_msg),
                 "STILL NOTHING - IS A DEVICE PLUGGED IN?");
    s->status_time_ms = get_time_ms();
    return found > 0;
}

/* Open every scanned node of kind t, all non-blocking; the tester drains each
 * one every frame, so any of two mice (or keyboards, or pads) drives it without
 * the operator having to pick one. Returns how many opened. */
static int usb_open_kind(UsbState *s, DevType t) {
    usb_close(s);
    for (int i=0; i<s->dev_cnt && s->fd_cnt<MAX_USB_DEV; i++) {
        if (s->devs[i].type!=t) continue;
        int fd=open(s->devs[i].path, O_RDONLY|O_NONBLOCK);
        if (fd<0) continue;
        s->fds[s->fd_cnt]=fd;
        s->fd_dev[s->fd_cnt]=i;
        s->fd_cnt++;
    }
    return s->fd_cnt;
}

static void usb_close(UsbState *s) {
    for (int k=0; k<s->fd_cnt; k++)
        if (s->fds[k]>=0) close(s->fds[k]);
    s->fd_cnt=0;
    s->last_dev=-1;
}

static void usb_load_axes(UsbState *s, int slot, int fd) {
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
static void usb_src_line(const UsbState *s, char *out, int max_w, int scale) {
    char raw[DEV_NAME_LEN+32];
    if (s->last_dev>=0 && s->last_dev<s->dev_cnt) {
        const USBDev *d=&s->devs[s->last_dev];
        snprintf(raw,sizeof(raw),"FROM EVENT%d: %s",d->ev_num,d->name);
    } else {
        snprintf(raw,sizeof(raw),"LISTENING ON %d NODE%s - USE ANY",
                 s->fd_cnt, s->fd_cnt==1?"":"S");
    }
    text_truncate(out, raw, max_w, scale);
}

static int usb_norm_axis(int v, int mn, int mx) {
    if (mx==mn) return 0;
    int mid=(mn+mx)/2, hr=(mx-mn)/2;
    if (!hr) return 0;
    int n=((v-mid)*1000)/hr;
    return n<-1000?-1000:n>1000?1000:n;
}

static int usb_norm_trig(int v, int mn, int mx) {
    if (mx==mn) return 0;
    int n=((v-mn)*1000)/(mx-mn);
    return n<0?0:n>1000?1000:n;
}

static void usb_add_log(KbdState *k, const char *m) {
    if (k->log_cnt>=LOG_LINES) {
        for(int i=0;i<LOG_LINES-1;i++) memcpy(k->log[i],k->log[i+1],LOG_LINE_LEN);
        k->log_cnt=LOG_LINES-1;
    }
    strncpy(k->log[k->log_cnt],m,LOG_LINE_LEN-1);
    k->log[k->log_cnt][LOG_LINE_LEN-1]=0;
    k->log_cnt++;
}

/* ── USB event processing ───────────────────────────────────────────────── */
static void usb_proc_kbd(UsbState *s) {
  for (int k=0; k<s->fd_cnt; k++) {
    int ev=s->devs[s->fd_dev[k]].ev_num;
    struct input_event e;
    while (read(s->fds[k],&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type!=EV_KEY || e.code>=KEY_MAX) continue;
        s->last_dev=s->fd_dev[k];
        const char *nm=usb_key_name(e.code);
        char nb[32]; if(!nm){snprintf(nb,32,"KEY_%d",e.code);nm=nb;}
        char lm[LOG_LINE_LEN];
        if (e.value==1) {
            s->kbd.held[e.code]=true; s->kbd.last_code=e.code;
            strncpy(s->kbd.last_name,nm,31); s->kbd.last_name[31]=0;
            snprintf(lm,sizeof(lm),"PRESS   %s (%d) EV%d",nm,e.code,ev);
            usb_add_log(&s->kbd,lm);
        } else if (e.value==0) {
            s->kbd.held[e.code]=false;
            snprintf(lm,sizeof(lm),"RELEASE %s (%d) EV%d",nm,e.code,ev);
            usb_add_log(&s->kbd,lm);
        } else if (e.value==2) {
            snprintf(lm,sizeof(lm),"REPEAT  %s (%d) EV%d",nm,e.code,ev);
            usb_add_log(&s->kbd,lm);
        }
    }
  }
}

static void usb_proc_mouse(const Framebuffer *fb, UsbState *s) {
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

static void usb_proc_pad(UsbState *s) {
  for (int k=0; k<s->fd_cnt; k++) {
    const int *mn=s->pad.amin[k], *mx=s->pad.amax[k];
    struct input_event e;
    while (read(s->fds[k],&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type==EV_ABS || e.type==EV_KEY) s->last_dev=s->fd_dev[k];
        if (e.type==EV_ABS) {
            int c=e.code;
            if (c>ABS_MAX) continue;
            if (c==ABS_X) s->pad.lx=usb_norm_axis(e.value,mn[c],mx[c]);
            else if (c==ABS_Y) s->pad.ly=usb_norm_axis(e.value,mn[c],mx[c]);
            else if (c==ABS_RX||c==ABS_Z) s->pad.rx=usb_norm_axis(e.value,mn[c],mx[c]);
            else if (c==ABS_RY||c==ABS_RZ) s->pad.ry=usb_norm_axis(e.value,mn[c],mx[c]);
            else if (c==ABS_HAT0X) s->pad.dx=e.value>0?1:e.value<0?-1:0;
            else if (c==ABS_HAT0Y) s->pad.dy=e.value>0?1:e.value<0?-1:0;
            else if (c==ABS_BRAKE) s->pad.tl=usb_norm_trig(e.value,mn[c],mx[c]);
            else if (c==ABS_GAS) s->pad.tr=usb_norm_trig(e.value,mn[c],mx[c]);
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

#define USB_BTN_W    130
#define USB_BTN_H    40
#define USB_BTN_GAP  10
#define USB_ROW_H    36
#define USB_ROW_Y0   36       /* the panel's title, above the first row */
#define USB_ID_X     140      /* the vid:pid column, in from the panel's right */
#define USB_ID_WIDE  "card 9  ffff:ffff"

static int  list_x, list_y, list_w, list_h;
static int  row_slots;        /* device rows that fit the panel */
static bool btn_grid;         /* the four buttons 2x2: one row does not fit */

/* The name column's width for a device, from its kind badge's width. */
static int name_width(const UsbBusDev *d) {
    return list_w - (text_measure_width(d->kind, 2) + 16) - 170;
}

/* Re-run whenever the logical screen changes (rebuild_ui()).  The buttons sit
 * at the bottom — one row where four fit, else 2x2 — and the list panel takes
 * what is left above them; a list longer than its rows ends in "+N MORE". */
static void usb_page_layout(void) {
    btn_grid = 4 * USB_BTN_W + 3 * USB_BTN_GAP > CONTENT_WIDTH;
    int bw      = btn_grid ? (CONTENT_WIDTH - USB_BTN_GAP) / 2 : USB_BTN_W;
    int cols    = btn_grid ? 2 : 4;
    int rows    = 4 / cols;
    int btn_top = CONTENT_Y + CONTENT_H - 55 - (rows - 1) * (USB_BTN_H + USB_BTN_GAP);
    int sx      = CONTENT_LEFT + (CONTENT_WIDTH - (cols * bw + (cols - 1) * USB_BTN_GAP)) / 2;

    Button *btns[4] = { &usb_btn_rescan, &usb_btn_ktest, &usb_btn_mtest, &usb_btn_gtest };
    static const char *labels[4] = { "RESCAN", "KBD TEST", "MOUSE TEST", "PAD TEST" };
    for (int i = 0; i < 4; i++) {
        int bx = sx + (i % cols) * (bw + USB_BTN_GAP);
        int by = btn_top + (i / cols) * (USB_BTN_H + USB_BTN_GAP);
        if (i == 0)
            button_init_full(btns[i], bx, by, bw, USB_BTN_H, labels[i],
                             BTN_COLOR_INFO, COLOR_WHITE, RGB(0,200,255), 2);
        else
            button_init_full(btns[i], bx, by, bw, USB_BTN_H, labels[i],
                             BTN_COLOR_PRIMARY, COLOR_WHITE, RGB(0,200,80), 2);
    }
    button_init_full(&usb_btn_kback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
    button_init_full(&usb_btn_mback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
    button_init_full(&usb_btn_gback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);

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
     * finger.  The bottom is the lowest button as placed; the right edge the
     * widest of the buttons, the panel and the vid:pid column at its widest.
     * Names are cut to their column, so what is reported is how many of THIS
     * unit's names were cut, and how many rows the panel has for its devices. */
    {
        const UsbState *s = &usb_state;
        int bottom = (usb_btn_gtest.y + usb_btn_gtest.height) - CONTENT_Y;
        int right  = list_x + list_w;
        for (int i = 0; i < 4; i++)
            if (btns[i]->x + btns[i]->width > right)
                right = btns[i]->x + btns[i]->width;
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
               "for %d device(s) (safe %dx%d, %s, %s)\n",
               verdict, bottom, CONTENT_H, right, CONTENT_RIGHT, clipped,
               row_slots, s->bus_cnt, SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT,
               CONTENT_WIDTH < 600 ? "portrait" : "landscape",
               btn_grid ? "2x2 buttons" : "one row");
    }
}

/* ── Draw: the device list ──────────────────────────────────────────────── */
static void usb_page_draw(Framebuffer *fb) {
    const UsbState *s = &usb_state;
    int lx=list_x, ly=list_y, lw=list_w, lh=list_h;

    fb_fill_rounded_rect(fb, lx, ly, lw, lh, 6, USB_COLOR_PANEL);
    fb_draw_rounded_rect(fb, lx, ly, lw, lh, 6, USB_COLOR_PANEL_BD);
    fb_draw_text(fb, lx+12, ly+10, "DETECTED USB DEVICES:", USB_COLOR_HDR, 2);

    if (s->bus_cnt==0) {
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
         * The testers below still key off the evdev scan.  All that fit; if
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

    usb_btn_ktest.bg_color = s->kbd_idx>=0 ? BTN_COLOR_PRIMARY : USB_COLOR_DIM;
    usb_btn_mtest.bg_color = s->mou_idx>=0 ? BTN_COLOR_PRIMARY : USB_COLOR_DIM;
    usb_btn_gtest.bg_color = s->pad_idx>=0 ? BTN_COLOR_PRIMARY : USB_COLOR_DIM;

    button_draw(fb, &usb_btn_rescan);
    button_draw(fb, &usb_btn_ktest);
    button_draw(fb, &usb_btn_mtest);
    button_draw(fb, &usb_btn_gtest);
}

/* ── USB Draw: Keyboard fullscreen ──────────────────────────────────────── */
static void draw_usb_kbd(Framebuffer *fb, UsbState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &usb_btn_kback);
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
    for (int i=0; usb_kblayout[i].code>=0; i++) {
        const LKey *k=&usb_kblayout[i];
        int x=kx+k->col*hu, y=ky+k->row*(kh+g), w=k->w*hu-g;
        bool h=(k->code<KEY_MAX)?s->kbd.held[k->code]:false;
        fb_fill_rounded_rect(fb,x,y,w,kh,3,h?USB_COLOR_KEY_DN:USB_COLOR_KEY_UP);
        fb_draw_rounded_rect(fb,x,y,w,kh,3,USB_COLOR_KEY_BD);
        const char *l=usb_key_sname(k->code);
        if (l) {
            int tw=text_measure_width(l,1);
            fb_draw_text(fb,x+(w-tw)/2,y+(kh-8)/2,l,h?COLOR_WHITE:USB_COLOR_KEY_TXT,1);
        }
    }
    int lx2=SCREEN_SAFE_LEFT+20, ly2=ky+5*(kh+g)+8;
    int lw2=SCREEN_SAFE_WIDTH-40, lh2=SCREEN_SAFE_BOTTOM-ly2-10;
    if (lh2<30) lh2=30;
    fb_fill_rounded_rect(fb,lx2,ly2,lw2,lh2,4,USB_COLOR_LOG_BG);
    fb_draw_rounded_rect(fb,lx2,ly2,lw2,lh2,4,USB_COLOR_PANEL_BD);
    fb_draw_text(fb,lx2+8,ly2+4,"EVENT LOG:",USB_COLOR_DIM,1);
    {
        int sx=lx2+8+text_measure_width("EVENT LOG:",1)+16;
        char src[256]; usb_src_line(s, src, lx2+lw2-8-sx, 1);
        fb_draw_text(fb,sx,ly2+4,src,COLOR_CYAN,1);
    }
    int lny=ly2+16, llh=13, ml=(lh2-20)/llh;
    if(ml>LOG_LINES) ml=LOG_LINES;
    if(ml<0) ml=0;
    int st=s->kbd.log_cnt-ml; if(st<0) st=0;
    for (int i=st; i<s->kbd.log_cnt; i++)
        fb_draw_text(fb,lx2+8,lny+(i-st)*llh,s->kbd.log[i],USB_COLOR_LOG_TXT,1);
}

/* ── USB Draw: Mouse fullscreen ─────────────────────────────────────────── */
static void draw_usb_mou(Framebuffer *fb, UsbState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &usb_btn_mback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "MOUSE TEST", COLOR_WHITE, 3);
    int sw=screen_base_width, sh=screen_base_height;
    int ax=SCREEN_SAFE_LEFT+20, ay=SCREEN_SAFE_TOP+55, aw=sw-240, ah=sh-ay-20;
    fb_fill_rounded_rect(fb,ax,ay,aw,ah,6,RGB(15,15,25));
    fb_draw_rounded_rect(fb,ax,ay,aw,ah,6,USB_COLOR_PANEL_BD);
    for(int gx=ax+50;gx<ax+aw;gx+=50) fb_draw_line(fb,gx,ay+1,gx,ay+ah-2,RGB(25,25,35));
    for(int gy=ay+50;gy<ay+ah;gy+=50) fb_draw_line(fb,ax+1,gy,ax+aw-2,gy,RGB(25,25,35));

    int cx=s->mou.cx, cy=s->mou.cy;
    int dx=cx<ax+2?ax+2:cx>=ax+aw-2?ax+aw-3:cx;
    int dy=cy<ay+2?ay+2:cy>=ay+ah-2?ay+ah-3:cy;
    fb_draw_line(fb,dx-15,dy,dx+15,dy,USB_COLOR_CURSOR_C);
    fb_draw_line(fb,dx,dy-15,dx,dy+15,USB_COLOR_CURSOR_C);
    fb_fill_circle(fb,dx,dy,3,USB_COLOR_CURSOR_C);

    int px=ax+aw+15, pw=sw-px-15, py=ay;
    fb_draw_text(fb,px,py,"POSITION",USB_COLOR_HDR,2);
    char ps[32];
    snprintf(ps,32,"X: %d",cx); fb_draw_text(fb,px,py+22,ps,COLOR_WHITE,2);
    snprintf(ps,32,"Y: %d",cy); fb_draw_text(fb,px,py+42,ps,COLOR_WHITE,2);

    int bsy=py+80;
    fb_draw_text(fb,px,bsy,"BUTTONS",USB_COLOR_HDR,2);
    struct { bool h; const char *l; int o; } mb[3]={{s->mou.bl,"L",0},{s->mou.bm,"M",45},{s->mou.br,"R",90}};
    for(int b=0;b<3;b++) {
        uint32_t c=mb[b].h?USB_COLOR_MBTN_ON:USB_COLOR_MBTN_OFF;
        int bx=px+20+mb[b].o, by2=bsy+40;
        fb_fill_circle(fb,bx,by2,14,c); fb_draw_circle(fb,bx,by2,14,USB_COLOR_PANEL_BD);
        fb_draw_text(fb,bx-4,by2+18,mb[b].l,COLOR_LABEL,2);
    }

    int ssy=bsy+95;
    fb_draw_text(fb,px,ssy,"SCROLL",USB_COLOR_HDR,2);
    snprintf(ps,32,"WHEEL: %d",s->mou.scroll);
    fb_draw_text(fb,px,ssy+22,ps,COLOR_WHITE,2);
    uint32_t now=get_time_ms();
    if (now-s->mou.scroll_t<300) fb_fill_circle(fb,px+pw-20,ssy+10,8,USB_COLOR_SCROLL);
    int brx=px, bry=ssy+45, brw=pw-10<20?20:pw-10, brh=20;
    fb_fill_rect(fb,brx,bry,brw,brh,RGB(40,40,40));
    int sv=s->mou.scroll; if(sv>50) sv=50; if(sv<-50) sv=-50;
    int ix=brx+brw/2+(sv*brw/100);
    fb_fill_rect(fb,ix-3,bry,6,brh,USB_COLOR_SCROLL);
    fb_draw_line(fb,brx+brw/2,bry,brx+brw/2,bry+brh,USB_COLOR_DIM);

    /* Which node moved the crosshair: every mouse node is open at once. */
    int sry=bry+brh+20;
    fb_draw_text(fb,px,sry,"SOURCE",USB_COLOR_HDR,2);
    if (s->last_dev>=0 && s->last_dev<s->dev_cnt) {
        const USBDev *d=&s->devs[s->last_dev];
        snprintf(ps,32,"EVENT%d",d->ev_num);
        fb_draw_text(fb,px,sry+22,ps,COLOR_WHITE,2);
        char nm[256]; text_truncate(nm, d->name, pw-5, 1);
        fb_draw_text(fb,px,sry+44,nm,COLOR_LABEL,1);
    } else {
        fb_draw_text(fb,px,sry+22,"NONE YET",USB_COLOR_DIM,2);
    }
    snprintf(ps,32,"%d MOUSE NODE%s OPEN",s->fd_cnt,s->fd_cnt==1?"":"S");
    fb_draw_text(fb,px,sry+58,ps,USB_COLOR_DIM,1);
    text_draw_centered(fb,sw/2,sh-15,"MOVE ANY MOUSE TO CONTROL CROSSHAIR",USB_COLOR_DIM,1);
}

/* ── USB Draw: Gamepad helpers ──────────────────────────────────────────── */
static void usb_draw_stick(Framebuffer *fb, int cx, int cy, int r,
                           int vx, int vy, const char *label) {
    fb_fill_circle(fb,cx,cy,r,USB_COLOR_STICK_BG);
    fb_draw_circle(fb,cx,cy,r,USB_COLOR_PANEL_BD);
    fb_draw_line(fb,cx-r,cy,cx+r,cy,RGB(40,40,55));
    fb_draw_line(fb,cx,cy-r,cx,cy+r,RGB(40,40,55));
    int dpx=cx+(vx*(r-6))/1000, dpy=cy+(vy*(r-6))/1000;
    fb_fill_circle(fb,dpx,dpy,6,USB_COLOR_STICK_DOT);
    fb_draw_circle(fb,dpx,dpy,6,COLOR_WHITE);
    text_draw_centered(fb,cx,cy+r+14,label,COLOR_LABEL,2);
}

static void usb_draw_dpad(Framebuffer *fb, int cx, int cy, int sz, int dx, int dy) {
    int arm=sz/3, hf=arm/2;
    fb_fill_rect(fb,cx-sz/2,cy-hf,sz,arm,RGB(50,50,60));
    fb_fill_rect(fb,cx-hf,cy-sz/2,arm,sz,RGB(50,50,60));
    if(dx<0) fb_fill_rect(fb,cx-sz/2,cy-hf,arm,arm,USB_COLOR_PAD_ON);
    if(dx>0) fb_fill_rect(fb,cx+sz/2-arm,cy-hf,arm,arm,USB_COLOR_PAD_ON);
    if(dy<0) fb_fill_rect(fb,cx-hf,cy-sz/2,arm,arm,USB_COLOR_PAD_ON);
    if(dy>0) fb_fill_rect(fb,cx-hf,cy+sz/2-arm,arm,arm,USB_COLOR_PAD_ON);
    fb_draw_rect(fb,cx-sz/2,cy-hf,sz,arm,USB_COLOR_PANEL_BD);
    fb_draw_rect(fb,cx-hf,cy-sz/2,arm,sz,USB_COLOR_PANEL_BD);
    text_draw_centered(fb,cx,cy+sz/2+14,"D-PAD",COLOR_LABEL,2);
}

/* ── USB Draw: Gamepad fullscreen ───────────────────────────────────────── */
static void draw_usb_pad(Framebuffer *fb, UsbState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &usb_btn_gback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "GAMEPAD TEST", COLOR_WHITE, 3);
    int sw=screen_base_width, sr=55;
    int lcx=SCREEN_SAFE_LEFT+30+sr, lcy=SCREEN_SAFE_TOP+80+sr;
    usb_draw_stick(fb,lcx,lcy,sr,s->pad.lx,s->pad.ly,"LEFT STICK");
    usb_draw_dpad(fb,lcx,lcy+sr+70,70,s->pad.dx,s->pad.dy);
    int rcx=sw-SCREEN_SAFE_LEFT-30-sr;
    usb_draw_stick(fb,rcx,lcy,sr,s->pad.rx,s->pad.ry,"RIGHT STICK");

    int bcnt=s->pad.btn_cnt<1?8:s->pad.btn_cnt;
    if(bcnt>16) bcnt=16;
    int cols=4, bsz=28, bgap=6;
    int bax=lcx+sr+40, bay=SCREEN_SAFE_TOP+75;
    for(int i=0;i<bcnt;i++) {
        int col=i%cols, row=i/cols;
        int bx=bax+col*(bsz+bgap)+bsz/2;
        int by=bay+row*(bsz+bgap)+bsz/2;
        uint32_t c=s->pad.btns[i]?USB_COLOR_PAD_ON:USB_COLOR_PAD_OFF;
        fb_fill_circle(fb,bx,by,bsz/2,c);
        fb_draw_circle(fb,bx,by,bsz/2,USB_COLOR_PANEL_BD);
        char bn[4]; snprintf(bn,4,"%d",i);
        int tw=text_measure_width(bn,1);
        fb_draw_text(fb,bx-tw/2,by-4,bn,COLOR_WHITE,1);
    }
    fb_draw_text(fb,bax,bay-16,"BUTTONS",USB_COLOR_HDR,2);

    int trw=150, trh=18, try2=SCREEN_SAFE_BOTTOM-80;
    int trlx=(sw/2)-trw-20, trrx=(sw/2)+20;
    fb_draw_text(fb,trlx,try2-16,"LT",COLOR_LABEL,1);
    fb_fill_rect(fb,trlx,try2,trw,trh,USB_COLOR_TRIG_BG);
    if(s->pad.tl>0) fb_fill_rect(fb,trlx,try2,(s->pad.tl*trw)/1000,trh,USB_COLOR_TRIG_FILL);
    fb_draw_rect(fb,trlx,try2,trw,trh,USB_COLOR_PANEL_BD);
    char tv[16]; snprintf(tv,16,"%d%%",s->pad.tl/10);
    fb_draw_text(fb,trlx+trw+8,try2+4,tv,COLOR_WHITE,1);

    fb_draw_text(fb,trrx,try2-16,"RT",COLOR_LABEL,1);
    fb_fill_rect(fb,trrx,try2,trw,trh,USB_COLOR_TRIG_BG);
    if(s->pad.tr>0) fb_fill_rect(fb,trrx,try2,(s->pad.tr*trw)/1000,trh,USB_COLOR_TRIG_FILL);
    fb_draw_rect(fb,trrx,try2,trw,trh,USB_COLOR_PANEL_BD);
    snprintf(tv,16,"%d%%",s->pad.tr/10);
    fb_draw_text(fb,trrx+trw+8,try2+4,tv,COLOR_WHITE,1);

    int rvx=bax, rvy=bay+(bcnt/cols+1)*(bsz+bgap)+20;
    fb_draw_text(fb,rvx,rvy,"RAW AXES",USB_COLOR_HDR,2);
    char rv[48];
    snprintf(rv,48,"LX:%+5d LY:%+5d",s->pad.lx,s->pad.ly);
    fb_draw_text(fb,rvx,rvy+20,rv,COLOR_LABEL,1);
    snprintf(rv,48,"RX:%+5d RY:%+5d",s->pad.rx,s->pad.ry);
    fb_draw_text(fb,rvx,rvy+34,rv,COLOR_LABEL,1);
    snprintf(rv,48,"DX:%+2d DY:%+2d",s->pad.dx,s->pad.dy);
    fb_draw_text(fb,rvx,rvy+48,rv,COLOR_LABEL,1);

    /* Every pad node is open at once; name the one that sent the last event. */
    char src[256]; usb_src_line(s, src, SCREEN_SAFE_WIDTH-20, 2);
    text_draw_centered(fb,sw/2,SCREEN_SAFE_BOTTOM-30,src,COLOR_CYAN,2);
}

/* ── Input ──────────────────────────────────────────────────────────────── */

static void usb_page_load(const Config *cfg) {
    (void)cfg;
    UsbState *s = &usb_state;
    s->scr = USB_SCR_MAIN;
    s->fd_cnt = 0;
    s->last_dev = -1;
    usb_scan_devices(s);
}

/* Every opening reads the bus afresh: a reading kept from startup listed
 * devices unplugged since, until RESCAN.  Only a read — the port re-probe
 * stays on an explicit RESCAN that finds nothing. */
static void usb_page_enter(void) {
    usb_state.status_msg[0] = '\0';
    usb_scan_devices(&usb_state);
}

/* Main screen only: a tester or a port re-probe is queued here and run by
 * usb_page_run_fullscreen().  A RESCAN repaints only when the reading changed;
 * a status line repaints when it appears and again when it times out. */
static CpPageResult usb_page_input(Config *cfg, int tx, int ty,
                                   bool touching, uint32_t now) {
    (void)cfg;
    UsbState *state = &usb_state;
    CpPageResult act = CP_PAGE_IDLE;

    if (state->status_msg[0] && now - state->status_time_ms > STATUS_MS) {
        state->status_msg[0] = '\0';
        act = CP_PAGE_REDRAW;
    }

    if (button_update(&usb_btn_rescan, tx, ty, touching, now)) {
        /* ~2 KB of reading to compare: off the stack. */
        static UsbBusDev prev_bus[USB_BUS_MAX];
        int prev_bus_cnt = state->bus_cnt, prev_dev_cnt = state->dev_cnt;
        memcpy(prev_bus, state->bus, sizeof(prev_bus));
        usb_scan_devices(state);
        /* An empty scan is the dead-port signature: when the port is unpowered
         * NOTHING enumerates, so finding nothing is exactly when a re-probe is
         * worth its few seconds. If something is already listed the port is live,
         * and a device plugged in later enumerates on its own (measured on .188
         * across gaps of 70-300 s) — so do not disturb a working bus. A hub
         * alone counts as empty: that is how a dead port looks behind one.
         * The re-probe blocks, so it runs full-screen. */
        if (usb_bus_peripherals(state->bus, state->bus_cnt) == 0) {
            state->recover_queued = true;
            return CP_PAGE_FULLSCREEN;
        }
        if (state->bus_cnt != prev_bus_cnt || state->dev_cnt != prev_dev_cnt ||
            memcmp(prev_bus, state->bus, sizeof(prev_bus)) != 0)
            act = CP_PAGE_REDRAW;
    }
    if (state->kbd_idx >= 0 &&
        button_update(&usb_btn_ktest, tx, ty, touching, now)) {
        memset(&state->kbd, 0, sizeof(state->kbd));
        if (usb_open_kind(state, DEV_KEYBOARD) > 0) {
            state->scr = USB_SCR_KEYBOARD;
            act = CP_PAGE_FULLSCREEN;
        }
    }
    if (state->mou_idx >= 0 &&
        button_update(&usb_btn_mtest, tx, ty, touching, now)) {
        memset(&state->mou, 0, sizeof(state->mou));
        if (usb_open_kind(state, DEV_MOUSE) > 0) {
            state->scr = USB_SCR_MOUSE;
            act = CP_PAGE_FULLSCREEN;
        }
    }
    if (state->pad_idx >= 0 &&
        button_update(&usb_btn_gtest, tx, ty, touching, now)) {
        memset(&state->pad, 0, sizeof(state->pad));
        state->pad.btn_cnt = 8;
        if (usb_open_kind(state, DEV_GAMEPAD) > 0) {
            for (int k = 0; k < state->fd_cnt; k++)
                usb_load_axes(state, k, state->fds[k]);
            state->scr = USB_SCR_GAMEPAD;
            act = CP_PAGE_FULLSCREEN;
        }
    }
    return act;
}

/* ── Full-screen: the testers, and the port re-probe ────────────────────── */

/* After input() returned CP_PAGE_FULLSCREEN.  Runs until the tester's BACK (or
 * a signal asking the panel to quit), then closes every node it opened: its
 * hardware is its own to clean up. */
static void usb_page_run_fullscreen(Framebuffer *fb, TouchInput *touch) {
    UsbState *state = &usb_state;
    if (state->recover_queued) {
        state->recover_queued = false;
        usb_recover_port(fb, state);
        return;
    }
    if (state->scr == USB_SCR_MOUSE) {
        state->mou.cx = (int)fb->width / 2;
        state->mou.cy = (int)fb->height / 2;
    }
    while (cp_running() && state->scr != USB_SCR_MAIN) {
        uint32_t now = get_time_ms();

        switch (state->scr) {
            case USB_SCR_KEYBOARD: usb_proc_kbd(state); break;
            case USB_SCR_MOUSE:    usb_proc_mouse(fb, state); break;
            case USB_SCR_GAMEPAD:  usb_proc_pad(state); break;
            default: break;
        }

        switch (state->scr) {
            case USB_SCR_KEYBOARD: draw_usb_kbd(fb, state); break;
            case USB_SCR_MOUSE:    draw_usb_mou(fb, state); break;
            case USB_SCR_GAMEPAD:  draw_usb_pad(fb, state); break;
            default: break;
        }
        fb_swap(fb);

        touch_poll(touch);
        TouchState ts = touch_get_state(touch);
        int tx = ts.x, ty = ts.y;
        bool touching = ts.pressed || ts.held;

        switch (state->scr) {
        case USB_SCR_KEYBOARD:
            if (button_update(&usb_btn_kback, tx, ty, touching, now)) {
                usb_close(state); state->scr = USB_SCR_MAIN;
            }
            break;
        case USB_SCR_MOUSE:
            if (button_update(&usb_btn_mback, tx, ty, touching, now)) {
                usb_close(state); state->scr = USB_SCR_MAIN;
            }
            break;
        case USB_SCR_GAMEPAD:
            if (button_update(&usb_btn_gback, tx, ty, touching, now)) {
                usb_close(state); state->scr = USB_SCR_MAIN;
            }
            break;
        default: break;
        }

        usleep(16000);
    }
    usb_close(state);   /* a signal can end the loop inside a tester */
    state->scr = USB_SCR_MAIN;
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
