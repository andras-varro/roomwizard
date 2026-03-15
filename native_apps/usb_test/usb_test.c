/**
 * USB Device Test — Interactive USB Input Device Tester for RoomWizard
 *
 * Screens: Main (device list), Keyboard, Mouse, Gamepad test views.
 * Hardware: TI AM335x, 800x480 fb, Linux 4.14.52, evdev input subsystem.
 */

#include "../common/framebuffer.h"
#include "../common/touch_input.h"
#include "../common/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <math.h>

/* ── Colors ─────────────────────────────────────────────────────────────── */
#define COLOR_BG           RGB(20, 20, 30)
#define COLOR_PANEL        RGB(30, 30, 45)
#define COLOR_PANEL_BD     RGB(50, 50, 70)
#define COLOR_LABEL        RGB(180, 180, 180)
#define COLOR_DIM          RGB(80, 80, 80)
#define COLOR_HDR          COLOR_CYAN
#define COLOR_CONN         RGB(0, 200, 80)
#define COLOR_DISC         RGB(100, 100, 100)
#define COLOR_KEY_UP       RGB(50, 50, 60)
#define COLOR_KEY_DN       RGB(0, 180, 255)
#define COLOR_KEY_BD       RGB(80, 80, 100)
#define COLOR_KEY_TXT      RGB(200, 200, 200)
#define COLOR_STICK_BG     RGB(40, 40, 50)
#define COLOR_STICK_DOT    RGB(0, 200, 255)
#define COLOR_PAD_ON       RGB(0, 220, 100)
#define COLOR_PAD_OFF      RGB(60, 60, 70)
#define COLOR_TRIG_BG      RGB(40, 40, 40)
#define COLOR_TRIG_FILL    RGB(255, 165, 0)
#define COLOR_CURSOR_C     RGB(255, 255, 0)
#define COLOR_MBTN_ON      RGB(0, 200, 80)
#define COLOR_MBTN_OFF     RGB(60, 60, 70)
#define COLOR_SCROLL       RGB(0, 180, 255)
#define COLOR_LOG_BG       RGB(15, 15, 25)
#define COLOR_LOG_TXT      RGB(150, 200, 150)

/* ── Constants ──────────────────────────────────────────────────────────── */
#define MAX_EV_DEV      16
#define MAX_USB_DEV     8
#define DEV_NAME_LEN    128
#define LOG_LINES       8
#define LOG_LINE_LEN    64

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(x)        ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)          ((x) % BITS_PER_LONG)
#define BIT_LONG(x)     ((x) / BITS_PER_LONG)
#define test_bit(b, a)  ((a[BIT_LONG(b)] >> OFF(b)) & 1)

/* ── Types ──────────────────────────────────────────────────────────────── */
typedef enum { DEV_UNKNOWN, DEV_KEYBOARD, DEV_MOUSE, DEV_GAMEPAD } DevType;
typedef enum { SCR_MAIN, SCR_KEYBOARD, SCR_MOUSE, SCR_GAMEPAD } AppScreen;

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
    int amin[ABS_MAX+1], amax[ABS_MAX+1];
    int dx, dy; bool btns[16]; int btn_cnt;
    int tl, tr;
} PadSt;

typedef struct {
    AppScreen scr;
    USBDev devs[MAX_USB_DEV]; int dev_cnt;
    int kbd_idx, mou_idx, pad_idx, usb_fd;
    KbdState kbd; MouseSt mou; PadSt pad;
} AppState;

/* ── Globals ────────────────────────────────────────────────────────────── */
static volatile bool running = true;
static Framebuffer *g_fb = NULL;

static void sig_handler(int s) { (void)s; running = false; }

/* ── Key table ──────────────────────────────────────────────────────────── */
typedef struct { int code; const char *name, *sname; } KeyInfo;
static const KeyInfo ktab[] = {
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

static const char *key_name(int c) {
    for (int i=0; ktab[i].name; i++) if (ktab[i].code==c) return ktab[i].name;
    return NULL;
}
static const char *key_sname(int c) {
    for (int i=0; ktab[i].name; i++) if (ktab[i].code==c) return ktab[i].sname;
    return NULL;
}

/* ── Keyboard layout ────────────────────────────────────────────────────── */
typedef struct { int col, row, w, code; } LKey;
static const LKey kblayout[] = {
    /* Row 0 */
    {0,0,2,KEY_ESC},{2,0,2,KEY_1},{4,0,2,KEY_2},{6,0,2,KEY_3},{8,0,2,KEY_4},
    {10,0,2,KEY_5},{12,0,2,KEY_6},{14,0,2,KEY_7},{16,0,2,KEY_8},{18,0,2,KEY_9},
    {20,0,2,KEY_0},{22,0,2,KEY_MINUS},{24,0,2,KEY_EQUAL},{26,0,2,KEY_BACKSPACE},
    /* Row 1 */
    {0,1,2,KEY_TAB},{2,1,2,KEY_Q},{4,1,2,KEY_W},{6,1,2,KEY_E},{8,1,2,KEY_R},
    {10,1,2,KEY_T},{12,1,2,KEY_Y},{14,1,2,KEY_U},{16,1,2,KEY_I},{18,1,2,KEY_O},
    {20,1,2,KEY_P},{22,1,2,KEY_LEFTBRACE},{24,1,2,KEY_RIGHTBRACE},{26,1,2,KEY_ENTER},
    /* Row 2 */
    {0,2,3,KEY_CAPSLOCK},{3,2,2,KEY_A},{5,2,2,KEY_S},{7,2,2,KEY_D},{9,2,2,KEY_F},
    {11,2,2,KEY_G},{13,2,2,KEY_H},{15,2,2,KEY_J},{17,2,2,KEY_K},{19,2,2,KEY_L},
    {21,2,2,KEY_SEMICOLON},{23,2,2,KEY_APOSTROPHE},{25,2,3,KEY_BACKSLASH},
    /* Row 3 */
    {0,3,3,KEY_LEFTSHIFT},{3,3,2,KEY_Z},{5,3,2,KEY_X},{7,3,2,KEY_C},{9,3,2,KEY_V},
    {11,3,2,KEY_B},{13,3,2,KEY_N},{15,3,2,KEY_M},{17,3,2,KEY_COMMA},{19,3,2,KEY_DOT},
    {21,3,2,KEY_SLASH},{23,3,5,KEY_RIGHTSHIFT},
    /* Row 4 */
    {0,4,3,KEY_LEFTCTRL},{3,4,3,KEY_LEFTALT},{6,4,16,KEY_SPACE},
    {22,4,3,KEY_RIGHTALT},{25,4,3,KEY_RIGHTCTRL},
    {-1,-1,-1,-1}
};

/* ── Device scanning ────────────────────────────────────────────────────── */
static DevType classify(int fd) {
    unsigned long ev[NBITS(EV_MAX)]={0}, kb[NBITS(KEY_MAX)]={0},
                  rl[NBITS(REL_MAX)]={0}, ab[NBITS(ABS_MAX)]={0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev)<0) return DEV_UNKNOWN;
    bool hk=test_bit(EV_KEY,ev), hr=test_bit(EV_REL,ev), ha=test_bit(EV_ABS,ev);
    if (hk) ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kb)), kb);
    if (hr) ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rl)), rl);
    if (ha) ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ab)), ab);
    if (ha && hk && test_bit(ABS_X,ab) && test_bit(ABS_Y,ab) &&
        (test_bit(BTN_GAMEPAD,kb)||test_bit(BTN_SOUTH,kb)||
         test_bit(BTN_A,kb)||test_bit(BTN_JOYSTICK,kb)))
        return DEV_GAMEPAD;
    if (hr && hk && test_bit(REL_X,rl) && test_bit(REL_Y,rl) && test_bit(BTN_LEFT,kb))
        return DEV_MOUSE;
    if (hk) {
        static const int letter_keys[] = {
            KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
            KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
            KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M
        };
        int n=0;
        for(int k=0;k<(int)(sizeof(letter_keys)/sizeof(letter_keys[0]));k++)
            if(test_bit(letter_keys[k],kb)) n++;
        if(n>=20) return DEV_KEYBOARD;
    }
    return DEV_UNKNOWN;
}

static void scan_devices(AppState *s) {
    s->dev_cnt=0; s->kbd_idx=s->mou_idx=s->pad_idx=-1;
    for (int i=0; i<MAX_EV_DEV && s->dev_cnt<MAX_USB_DEV; i++) {
        char p[64]; snprintf(p,sizeof(p),"/dev/input/event%d",i);
        int fd=open(p, O_RDONLY|O_NONBLOCK); if(fd<0) continue;
        char nm[DEV_NAME_LEN]="Unknown";
        ioctl(fd, EVIOCGNAME(sizeof(nm)), nm);
        if (strstr(nm,"panjit")||strstr(nm,"Panjit")||strstr(nm,"PANJIT"))
            { close(fd); continue; }
        DevType t=classify(fd); close(fd);
        if (t==DEV_UNKNOWN) continue;
        USBDev *d=&s->devs[s->dev_cnt];
        strncpy(d->name,nm,DEV_NAME_LEN-1); d->name[DEV_NAME_LEN-1]=0;
        strncpy(d->path,p,63); d->path[63]=0;
        d->type=t; d->ev_num=i; d->connected=true;
        if (t==DEV_KEYBOARD && s->kbd_idx<0) s->kbd_idx=s->dev_cnt;
        else if (t==DEV_MOUSE && s->mou_idx<0) s->mou_idx=s->dev_cnt;
        else if (t==DEV_GAMEPAD && s->pad_idx<0) s->pad_idx=s->dev_cnt;
        s->dev_cnt++;
    }
}

/* ── USB device open/close ──────────────────────────────────────────────── */
static int open_usb(AppState *s, int idx) {
    if (idx<0||idx>=s->dev_cnt) return -1;
    if (s->usb_fd>=0) { close(s->usb_fd); s->usb_fd=-1; }
    s->usb_fd=open(s->devs[idx].path, O_RDONLY|O_NONBLOCK);
    return s->usb_fd;
}
static void close_usb(AppState *s) {
    if (s->usb_fd>=0) { close(s->usb_fd); s->usb_fd=-1; }
}

/* ── Axis helpers ───────────────────────────────────────────────────────── */
static void load_axes(AppState *s, int fd) {
    memset(s->pad.amin,0,sizeof(s->pad.amin));
    memset(s->pad.amax,0,sizeof(s->pad.amax));
    unsigned long ab[NBITS(ABS_MAX)]={0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ab)), ab)<0) return;
    for (int a=0; a<=ABS_MAX; a++) {
        if (test_bit(a,ab)) {
            struct input_absinfo inf;
            if (ioctl(fd, EVIOCGABS(a), &inf)==0)
                { s->pad.amin[a]=inf.minimum; s->pad.amax[a]=inf.maximum; }
        }
    }
}

static int norm_axis(int v, int mn, int mx) {
    if (mx==mn) return 0;
    int mid=(mn+mx)/2, hr=(mx-mn)/2;
    if (!hr) return 0;
    int n=((v-mid)*1000)/hr;
    return n<-1000?-1000:n>1000?1000:n;
}
static int norm_trig(int v, int mn, int mx) {
    if (mx==mn) return 0;
    int n=((v-mn)*1000)/(mx-mn);
    return n<0?0:n>1000?1000:n;
}

/* ── Event log ──────────────────────────────────────────────────────────── */
static void add_log(KbdState *k, const char *m) {
    if (k->log_cnt>=LOG_LINES) {
        for(int i=0;i<LOG_LINES-1;i++) memcpy(k->log[i],k->log[i+1],LOG_LINE_LEN);
        k->log_cnt=LOG_LINES-1;
    }
    strncpy(k->log[k->log_cnt],m,LOG_LINE_LEN-1);
    k->log[k->log_cnt][LOG_LINE_LEN-1]=0;
    k->log_cnt++;
}

/* ── Event processing ───────────────────────────────────────────────────── */
static void proc_kbd(AppState *s) {
    if (s->usb_fd<0) return;
    struct input_event e;
    while (read(s->usb_fd,&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type!=EV_KEY || e.code>=KEY_MAX) continue;
        const char *nm=key_name(e.code);
        char nb[32]; if(!nm){snprintf(nb,32,"KEY_%d",e.code);nm=nb;}
        char lm[LOG_LINE_LEN];
        if (e.value==1) {
            s->kbd.held[e.code]=true; s->kbd.last_code=e.code;
            strncpy(s->kbd.last_name,nm,31); s->kbd.last_name[31]=0;
            snprintf(lm,sizeof(lm),"PRESS   %s (%d)",nm,e.code);
            add_log(&s->kbd,lm);
        } else if (e.value==0) {
            s->kbd.held[e.code]=false;
            snprintf(lm,sizeof(lm),"RELEASE %s (%d)",nm,e.code);
            add_log(&s->kbd,lm);
        } else if (e.value==2) {
            snprintf(lm,sizeof(lm),"REPEAT  %s (%d)",nm,e.code);
            add_log(&s->kbd,lm);
        }
    }
}

static void proc_mouse(AppState *s) {
    if (s->usb_fd<0) return;
    struct input_event e;
    while (read(s->usb_fd,&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type==EV_REL) {
            if (e.code==REL_X) {
                s->mou.cx+=e.value;
                if(s->mou.cx<0) s->mou.cx=0;
                if(s->mou.cx>=(int)g_fb->width) s->mou.cx=(int)g_fb->width-1;
            } else if (e.code==REL_Y) {
                s->mou.cy+=e.value;
                if(s->mou.cy<0) s->mou.cy=0;
                if(s->mou.cy>=(int)g_fb->height) s->mou.cy=(int)g_fb->height-1;
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

static void proc_pad(AppState *s) {
    if (s->usb_fd<0) return;
    struct input_event e;
    while (read(s->usb_fd,&e,sizeof(e))==(ssize_t)sizeof(e)) {
        if (e.type==EV_ABS) {
            int c=e.code;
            if (c==ABS_X) s->pad.lx=norm_axis(e.value,s->pad.amin[c],s->pad.amax[c]);
            else if (c==ABS_Y) s->pad.ly=norm_axis(e.value,s->pad.amin[c],s->pad.amax[c]);
            else if (c==ABS_RX||c==ABS_Z) s->pad.rx=norm_axis(e.value,s->pad.amin[c],s->pad.amax[c]);
            else if (c==ABS_RY||c==ABS_RZ) s->pad.ry=norm_axis(e.value,s->pad.amin[c],s->pad.amax[c]);
            else if (c==ABS_HAT0X) s->pad.dx=e.value>0?1:e.value<0?-1:0;
            else if (c==ABS_HAT0Y) s->pad.dy=e.value>0?1:e.value<0?-1:0;
            else if (c==ABS_BRAKE) s->pad.tl=norm_trig(e.value,s->pad.amin[c],s->pad.amax[c]);
            else if (c==ABS_GAS) s->pad.tr=norm_trig(e.value,s->pad.amin[c],s->pad.amax[c]);
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

/* ── Device type string ─────────────────────────────────────────────────── */
static const char *dtype_str(DevType t) {
    switch(t) { case DEV_KEYBOARD:return "KEYBOARD"; case DEV_MOUSE:return "MOUSE";
                case DEV_GAMEPAD:return "GAMEPAD"; default:return "UNKNOWN"; }
}

/* ── UI Buttons ─────────────────────────────────────────────────────────── */
static Button btn_rescan, btn_exit, btn_ktest, btn_mtest, btn_gtest;
static Button btn_kback, btn_mback, btn_gback;

static void create_ui(void) {
    int sw=screen_base_width, bw=160, bh=45;
    button_init_full(&btn_exit, sw-90, SCREEN_SAFE_TOP+8, 80, 40, "EXIT",
                     BTN_COLOR_DANGER, COLOR_WHITE, RGB(255,80,80), 2);
    button_init_full(&btn_rescan, sw-210, SCREEN_SAFE_TOP+8, 110, 40, "RESCAN",
                     BTN_COLOR_INFO, COLOR_WHITE, RGB(0,200,255), 2);
    int by=SCREEN_SAFE_BOTTOM-bh-15, tw=3*bw+30, sx=(sw-tw)/2;
    button_init_full(&btn_ktest, sx, by, bw, bh, "KEYBOARD TEST",
                     BTN_COLOR_PRIMARY, COLOR_WHITE, RGB(0,200,80), 2);
    button_init_full(&btn_mtest, sx+bw+15, by, bw, bh, "MOUSE TEST",
                     BTN_COLOR_PRIMARY, COLOR_WHITE, RGB(0,200,80), 2);
    button_init_full(&btn_gtest, sx+2*(bw+15), by, bw, bh, "GAMEPAD TEST",
                     BTN_COLOR_PRIMARY, COLOR_WHITE, RGB(0,200,80), 2);
    button_init_full(&btn_kback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
    button_init_full(&btn_mback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
    button_init_full(&btn_gback, SCREEN_SAFE_LEFT+10, SCREEN_SAFE_TOP+8,
                     90, 40, "< BACK", BTN_COLOR_WARNING, COLOR_WHITE, RGB(255,200,0), 2);
}

/* ── Draw: Main Screen ──────────────────────────────────────────────────── */
static void draw_main(Framebuffer *fb, AppState *s) {
    fb_clear(fb, COLOR_BG);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "USB DEVICE TESTER", COLOR_WHITE, 3);
    button_draw(fb, &btn_rescan); button_draw(fb, &btn_exit);

    int lx=SCREEN_SAFE_LEFT+20, ly=SCREEN_SAFE_TOP+60;
    int lw=SCREEN_SAFE_WIDTH-40, lh=SCREEN_SAFE_HEIGHT-140;
    fb_fill_rounded_rect(fb, lx, ly, lw, lh, 6, COLOR_PANEL);
    fb_draw_rounded_rect(fb, lx, ly, lw, lh, 6, COLOR_PANEL_BD);
    fb_draw_text(fb, lx+12, ly+10, "DETECTED USB DEVICES:", COLOR_HDR, 2);

    if (s->dev_cnt==0) {
        text_draw_centered(fb, screen_base_width/2, ly+lh/2-10,
                           "NO USB DEVICES DETECTED", COLOR_DIM, 2);
        text_draw_centered(fb, screen_base_width/2, ly+lh/2+15,
                           "CONNECT A DEVICE AND TAP RESCAN", COLOR_DIM, 2);
    } else {
        int ry0=ly+36, rh=36;
        for (int i=0; i<s->dev_cnt && i<6; i++) {
            USBDev *d=&s->devs[i]; int ry=ry0+i*rh;
            if (i%2==0) fb_fill_rect(fb, lx+4, ry, lw-8, rh-2, RGB(25,25,38));
            fb_fill_circle(fb, lx+20, ry+rh/2, 6,
                           d->connected?COLOR_CONN:COLOR_DISC);
            const char *ts=dtype_str(d->type);
            uint32_t bc;
            switch(d->type) {
                case DEV_KEYBOARD: bc=RGB(0,150,200); break;
                case DEV_MOUSE: bc=RGB(200,150,0); break;
                case DEV_GAMEPAD: bc=RGB(0,180,80); break;
                default: bc=COLOR_DIM; break;
            }
            int bw=text_measure_width(ts,2)+16;
            fb_fill_rounded_rect(fb, lx+36, ry+4, bw, rh-10, 4, bc);
            fb_draw_text(fb, lx+44, ry+10, ts, COLOR_WHITE, 2);
            char tn[48]; text_truncate(tn, d->name, lw-bw-170, 2);
            fb_draw_text(fb, lx+44+bw+10, ry+10, tn, COLOR_LABEL, 2);
            fb_draw_text(fb, lx+lw-140, ry+10, d->path, COLOR_DIM, 1);
        }
    }
    btn_ktest.bg_color = s->kbd_idx>=0 ? BTN_COLOR_PRIMARY : COLOR_DIM;
    btn_mtest.bg_color = s->mou_idx>=0 ? BTN_COLOR_PRIMARY : COLOR_DIM;
    btn_gtest.bg_color = s->pad_idx>=0 ? BTN_COLOR_PRIMARY : COLOR_DIM;
    button_draw(fb, &btn_ktest);
    button_draw(fb, &btn_mtest);
    button_draw(fb, &btn_gtest);
}

/* ── Draw: Keyboard ─────────────────────────────────────────────────────── */
static void draw_kbd(Framebuffer *fb, AppState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &btn_kback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "KEYBOARD TEST", COLOR_WHITE, 3);
    char inf[80];
    if (s->kbd.last_code>0)
        snprintf(inf,sizeof(inf),"LAST KEY: %s (CODE %d)", s->kbd.last_name, s->kbd.last_code);
    else snprintf(inf,sizeof(inf),"PRESS ANY KEY ON USB KEYBOARD");
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+52, inf, COLOR_CYAN, 2);

    int kx=SCREEN_SAFE_LEFT+20, ky=SCREEN_SAFE_TOP+72, hu=26, kh=32, g=2;
    for (int i=0; kblayout[i].code>=0; i++) {
        const LKey *k=&kblayout[i];
        int x=kx+k->col*hu, y=ky+k->row*(kh+g), w=k->w*hu-g;
        bool h=(k->code<KEY_MAX)?s->kbd.held[k->code]:false;
        fb_fill_rounded_rect(fb,x,y,w,kh,3,h?COLOR_KEY_DN:COLOR_KEY_UP);
        fb_draw_rounded_rect(fb,x,y,w,kh,3,COLOR_KEY_BD);
        const char *l=key_sname(k->code);
        if (l) {
            int tw=text_measure_width(l,1);
            fb_draw_text(fb,x+(w-tw)/2,y+(kh-8)/2,l,h?COLOR_WHITE:COLOR_KEY_TXT,1);
        }
    }
    /* Event log */
    int lx2=SCREEN_SAFE_LEFT+20, ly2=ky+5*(kh+g)+8;
    int lw2=SCREEN_SAFE_WIDTH-40, lh2=SCREEN_SAFE_BOTTOM-ly2-10;
    if (lh2<30) lh2=30;
    fb_fill_rounded_rect(fb,lx2,ly2,lw2,lh2,4,COLOR_LOG_BG);
    fb_draw_rounded_rect(fb,lx2,ly2,lw2,lh2,4,COLOR_PANEL_BD);
    fb_draw_text(fb,lx2+8,ly2+4,"EVENT LOG:",COLOR_DIM,1);
    int lny=ly2+16, llh=13, ml=(lh2-20)/llh;
    if(ml>LOG_LINES) ml=LOG_LINES; if(ml<0) ml=0;
    int st=s->kbd.log_cnt-ml; if(st<0) st=0;
    for (int i=st; i<s->kbd.log_cnt; i++)
        fb_draw_text(fb,lx2+8,lny+(i-st)*llh,s->kbd.log[i],COLOR_LOG_TXT,1);
}

/* ── Draw: Mouse ────────────────────────────────────────────────────────── */
static void draw_mou(Framebuffer *fb, AppState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &btn_mback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "MOUSE TEST", COLOR_WHITE, 3);
    int sw=screen_base_width, sh=screen_base_height;
    int ax=SCREEN_SAFE_LEFT+20, ay=SCREEN_SAFE_TOP+55, aw=sw-240, ah=sh-ay-20;
    fb_fill_rounded_rect(fb,ax,ay,aw,ah,6,RGB(15,15,25));
    fb_draw_rounded_rect(fb,ax,ay,aw,ah,6,COLOR_PANEL_BD);
    for(int gx=ax+50;gx<ax+aw;gx+=50) fb_draw_line(fb,gx,ay+1,gx,ay+ah-2,RGB(25,25,35));
    for(int gy=ay+50;gy<ay+ah;gy+=50) fb_draw_line(fb,ax+1,gy,ax+aw-2,gy,RGB(25,25,35));

    int cx=s->mou.cx, cy=s->mou.cy;
    int dx=cx<ax+2?ax+2:cx>=ax+aw-2?ax+aw-3:cx;
    int dy=cy<ay+2?ay+2:cy>=ay+ah-2?ay+ah-3:cy;
    fb_draw_line(fb,dx-15,dy,dx+15,dy,COLOR_CURSOR_C);
    fb_draw_line(fb,dx,dy-15,dx,dy+15,COLOR_CURSOR_C);
    fb_fill_circle(fb,dx,dy,3,COLOR_CURSOR_C);

    int px=ax+aw+15, pw=sw-px-15, py=ay;
    fb_draw_text(fb,px,py,"POSITION",COLOR_HDR,2);
    char ps[32];
    snprintf(ps,32,"X: %d",cx); fb_draw_text(fb,px,py+22,ps,COLOR_WHITE,2);
    snprintf(ps,32,"Y: %d",cy); fb_draw_text(fb,px,py+42,ps,COLOR_WHITE,2);

    int bsy=py+80;
    fb_draw_text(fb,px,bsy,"BUTTONS",COLOR_HDR,2);
    struct { bool h; const char *l; int o; } mb[3]={{s->mou.bl,"L",0},{s->mou.bm,"M",45},{s->mou.br,"R",90}};
    for(int b=0;b<3;b++) {
        uint32_t c=mb[b].h?COLOR_MBTN_ON:COLOR_MBTN_OFF;
        int bx=px+20+mb[b].o, by2=bsy+40;
        fb_fill_circle(fb,bx,by2,14,c); fb_draw_circle(fb,bx,by2,14,COLOR_PANEL_BD);
        fb_draw_text(fb,bx-4,by2+18,mb[b].l,COLOR_LABEL,2);
    }

    int ssy=bsy+95;
    fb_draw_text(fb,px,ssy,"SCROLL",COLOR_HDR,2);
    snprintf(ps,32,"WHEEL: %d",s->mou.scroll);
    fb_draw_text(fb,px,ssy+22,ps,COLOR_WHITE,2);
    uint32_t now=get_time_ms();
    if (now-s->mou.scroll_t<300) fb_fill_circle(fb,px+pw-20,ssy+10,8,COLOR_SCROLL);
    int brx=px, bry=ssy+45, brw=pw-10<20?20:pw-10, brh=20;
    fb_fill_rect(fb,brx,bry,brw,brh,RGB(40,40,40));
    int sv=s->mou.scroll; if(sv>50) sv=50; if(sv<-50) sv=-50;
    int ix=brx+brw/2+(sv*brw/100);
    fb_fill_rect(fb,ix-3,bry,6,brh,COLOR_SCROLL);
    fb_draw_line(fb,brx+brw/2,bry,brx+brw/2,bry+brh,COLOR_DIM);
    text_draw_centered(fb,sw/2,sh-15,"MOVE MOUSE TO CONTROL CROSSHAIR",COLOR_DIM,1);
}

/* ── Draw: Gamepad ──────────────────────────────────────────────────────── */
static void draw_stick(Framebuffer *fb, int cx, int cy, int r,
                       int vx, int vy, const char *label) {
    fb_fill_circle(fb,cx,cy,r,COLOR_STICK_BG);
    fb_draw_circle(fb,cx,cy,r,COLOR_PANEL_BD);
    fb_draw_line(fb,cx-r,cy,cx+r,cy,RGB(40,40,55));
    fb_draw_line(fb,cx,cy-r,cx,cy+r,RGB(40,40,55));
    int dpx=cx+(vx*(r-6))/1000, dpy=cy+(vy*(r-6))/1000;
    fb_fill_circle(fb,dpx,dpy,6,COLOR_STICK_DOT);
    fb_draw_circle(fb,dpx,dpy,6,COLOR_WHITE);
    text_draw_centered(fb,cx,cy+r+14,label,COLOR_LABEL,2);
}

static void draw_dpad(Framebuffer *fb, int cx, int cy, int sz, int dx, int dy) {
    int arm=sz/3, hf=arm/2;
    fb_fill_rect(fb,cx-sz/2,cy-hf,sz,arm,RGB(50,50,60));
    fb_fill_rect(fb,cx-hf,cy-sz/2,arm,sz,RGB(50,50,60));
    if(dx<0) fb_fill_rect(fb,cx-sz/2,cy-hf,arm,arm,COLOR_PAD_ON);
    if(dx>0) fb_fill_rect(fb,cx+sz/2-arm,cy-hf,arm,arm,COLOR_PAD_ON);
    if(dy<0) fb_fill_rect(fb,cx-hf,cy-sz/2,arm,arm,COLOR_PAD_ON);
    if(dy>0) fb_fill_rect(fb,cx-hf,cy+sz/2-arm,arm,arm,COLOR_PAD_ON);
    fb_draw_rect(fb,cx-sz/2,cy-hf,sz,arm,COLOR_PANEL_BD);
    fb_draw_rect(fb,cx-hf,cy-sz/2,arm,sz,COLOR_PANEL_BD);
    text_draw_centered(fb,cx,cy+sz/2+14,"D-PAD",COLOR_LABEL,2);
}

static void draw_pad(Framebuffer *fb, AppState *s) {
    fb_clear(fb, COLOR_BG);
    button_draw(fb, &btn_gback);
    text_draw_centered(fb, screen_base_width/2, SCREEN_SAFE_TOP+22,
                       "GAMEPAD TEST", COLOR_WHITE, 3);
    int sw=screen_base_width, sr=55;
    int lcx=SCREEN_SAFE_LEFT+30+sr, lcy=SCREEN_SAFE_TOP+80+sr;
    draw_stick(fb,lcx,lcy,sr,s->pad.lx,s->pad.ly,"LEFT STICK");
    draw_dpad(fb,lcx,lcy+sr+70,70,s->pad.dx,s->pad.dy);
    int rcx=sw-SCREEN_SAFE_LEFT-30-sr;
    draw_stick(fb,rcx,lcy,sr,s->pad.rx,s->pad.ry,"RIGHT STICK");

    /* Button grid */
    int bcnt=s->pad.btn_cnt<1?8:s->pad.btn_cnt;
    if(bcnt>16) bcnt=16;
    int cols=4, bsz=28, bgap=6;
    int bax=lcx+sr+40, bay=SCREEN_SAFE_TOP+75;
    for(int i=0;i<bcnt;i++) {
        int col=i%cols, row=i/cols;
        int bx=bax+col*(bsz+bgap)+bsz/2;
        int by=bay+row*(bsz+bgap)+bsz/2;
        uint32_t c=s->pad.btns[i]?COLOR_PAD_ON:COLOR_PAD_OFF;
        fb_fill_circle(fb,bx,by,bsz/2,c);
        fb_draw_circle(fb,bx,by,bsz/2,COLOR_PANEL_BD);
        char bn[4]; snprintf(bn,4,"%d",i);
        int tw=text_measure_width(bn,1);
        fb_draw_text(fb,bx-tw/2,by-4,bn,COLOR_WHITE,1);
    }
    fb_draw_text(fb,bax,bay-16,"BUTTONS",COLOR_HDR,2);

    /* Triggers */
    int trw=150, trh=18, try2=SCREEN_SAFE_BOTTOM-80;
    int trlx=(sw/2)-trw-20, trrx=(sw/2)+20;
    fb_draw_text(fb,trlx,try2-16,"LT",COLOR_LABEL,1);
    fb_fill_rect(fb,trlx,try2,trw,trh,COLOR_TRIG_BG);
    if(s->pad.tl>0) fb_fill_rect(fb,trlx,try2,(s->pad.tl*trw)/1000,trh,COLOR_TRIG_FILL);
    fb_draw_rect(fb,trlx,try2,trw,trh,COLOR_PANEL_BD);
    char tv[16]; snprintf(tv,16,"%d%%",s->pad.tl/10);
    fb_draw_text(fb,trlx+trw+8,try2+4,tv,COLOR_WHITE,1);

    fb_draw_text(fb,trrx,try2-16,"RT",COLOR_LABEL,1);
    fb_fill_rect(fb,trrx,try2,trw,trh,COLOR_TRIG_BG);
    if(s->pad.tr>0) fb_fill_rect(fb,trrx,try2,(s->pad.tr*trw)/1000,trh,COLOR_TRIG_FILL);
    fb_draw_rect(fb,trrx,try2,trw,trh,COLOR_PANEL_BD);
    snprintf(tv,16,"%d%%",s->pad.tr/10);
    fb_draw_text(fb,trrx+trw+8,try2+4,tv,COLOR_WHITE,1);

    /* Raw axis values */
    int rvx=bax, rvy=bay+(bcnt/cols+1)*(bsz+bgap)+20;
    fb_draw_text(fb,rvx,rvy,"RAW AXES",COLOR_HDR,2);
    char rv[48];
    snprintf(rv,48,"LX:%+5d LY:%+5d",s->pad.lx,s->pad.ly);
    fb_draw_text(fb,rvx,rvy+20,rv,COLOR_LABEL,1);
    snprintf(rv,48,"RX:%+5d RY:%+5d",s->pad.rx,s->pad.ry);
    fb_draw_text(fb,rvx,rvy+34,rv,COLOR_LABEL,1);
    snprintf(rv,48,"DX:%+2d DY:%+2d",s->pad.dx,s->pad.dy);
    fb_draw_text(fb,rvx,rvy+48,rv,COLOR_LABEL,1);

    text_draw_centered(fb,sw/2,SCREEN_SAFE_BOTTOM-15,
                       "USE GAMEPAD CONTROLS TO TEST",COLOR_DIM,1);
}

/* ── Screen transitions ─────────────────────────────────────────────────── */
static void enter_kbd_test(AppState *s) {
    memset(&s->kbd, 0, sizeof(s->kbd));
    if (open_usb(s, s->kbd_idx)>=0)
        s->scr = SCR_KEYBOARD;
}

static void enter_mou_test(AppState *s) {
    memset(&s->mou, 0, sizeof(s->mou));
    s->mou.cx = (int)g_fb->width / 2;
    s->mou.cy = (int)g_fb->height / 2;
    if (open_usb(s, s->mou_idx)>=0)
        s->scr = SCR_MOUSE;
}

static void enter_pad_test(AppState *s) {
    memset(&s->pad, 0, sizeof(s->pad));
    s->pad.btn_cnt = 8;
    if (open_usb(s, s->pad_idx)>=0) {
        load_axes(s, s->usb_fd);
        s->scr = SCR_GAMEPAD;
    }
}

static void go_back(AppState *s) {
    close_usb(s);
    s->scr = SCR_MAIN;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[]) {
    const char *fb_dev = (argc>1) ? argv[1] : "/dev/fb0";
    const char *touch_dev = (argc>2) ? argv[2] : "/dev/input/event0";

    int lock_fd = acquire_instance_lock("usb_test");
    if (lock_fd < 0) { fprintf(stderr,"usb_test: another instance running\n"); return 1; }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    Framebuffer fb;
    if (fb_init(&fb, fb_dev) < 0) {
        fprintf(stderr, "usb_test: framebuffer init failed\n"); return 1;
    }
    g_fb = &fb;

    TouchInput touch;
    if (touch_init(&touch, touch_dev) < 0) {
        fprintf(stderr, "usb_test: touch init failed\n");
        fb_close(&fb); return 1;
    }
    touch_set_screen_size(&touch, fb.width, fb.height);

    AppState state;
    memset(&state, 0, sizeof(state));
    state.scr = SCR_MAIN;
    state.usb_fd = -1;
    state.kbd_idx = state.mou_idx = state.pad_idx = -1;

    create_ui();
    scan_devices(&state);

    while (running) {
        uint32_t now = get_time_ms();

        /* Process USB input */
        switch (state.scr) {
            case SCR_KEYBOARD: proc_kbd(&state); break;
            case SCR_MOUSE:    proc_mouse(&state); break;
            case SCR_GAMEPAD:  proc_pad(&state); break;
            default: break;
        }

        /* Draw */
        switch (state.scr) {
            case SCR_MAIN:     draw_main(&fb, &state); break;
            case SCR_KEYBOARD: draw_kbd(&fb, &state); break;
            case SCR_MOUSE:    draw_mou(&fb, &state); break;
            case SCR_GAMEPAD:  draw_pad(&fb, &state); break;
        }
        fb_swap(&fb);

        /* Touch input */
        touch_poll(&touch);
        TouchState ts = touch_get_state(&touch);
        int tx=ts.x, ty=ts.y;
        bool tp=ts.pressed;

        switch (state.scr) {
        case SCR_MAIN:
            if (button_check_press(&btn_exit, tp && button_is_touched(&btn_exit,tx,ty), now))
                running = false;
            if (button_check_press(&btn_rescan, tp && button_is_touched(&btn_rescan,tx,ty), now))
                scan_devices(&state);
            if (state.kbd_idx>=0 &&
                button_check_press(&btn_ktest, tp && button_is_touched(&btn_ktest,tx,ty), now))
                enter_kbd_test(&state);
            if (state.mou_idx>=0 &&
                button_check_press(&btn_mtest, tp && button_is_touched(&btn_mtest,tx,ty), now))
                enter_mou_test(&state);
            if (state.pad_idx>=0 &&
                button_check_press(&btn_gtest, tp && button_is_touched(&btn_gtest,tx,ty), now))
                enter_pad_test(&state);
            break;
        case SCR_KEYBOARD:
            if (button_check_press(&btn_kback, tp && button_is_touched(&btn_kback,tx,ty), now))
                go_back(&state);
            break;
        case SCR_MOUSE:
            if (button_check_press(&btn_mback, tp && button_is_touched(&btn_mback,tx,ty), now))
                go_back(&state);
            break;
        case SCR_GAMEPAD:
            if (button_check_press(&btn_gback, tp && button_is_touched(&btn_gback,tx,ty), now))
                go_back(&state);
            break;
        }

        usleep(16000); /* ~60fps */
    }

    close_usb(&state);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    touch_close(&touch);
    fb_close(&fb);
    return 0;
}
