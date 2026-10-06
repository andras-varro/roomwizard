/* system_page.c — control_panel's System page: system settings that otherwise
 * need SSH.  First part: the SSH mode (read-only), the clock and its RTC, and
 * a manual date/time set.
 *
 * Exposed only as cp_system_page (cp_page.h).  The SSH mode is read from
 * /etc/ssh/sshd_config on entry (sys_ssh_mode(), sys_settings.c); there is no
 * toggle, because a panel button cannot prove a key login exists and the unit
 * has no serial console.  The clock is read once a second.  The editor holds
 * year, month, day, hour and minute, each stepped by its own -/+ buttons
 * (sys_dt_step() keeps the day valid); SET asks first, then runs
 * `date -s "YYYY-MM-DD HH:MM:00"` and `hwclock -w` by fork/exec, the argument
 * built from validated integers.  The device has no timezone, so local time is
 * UTC.  The boot-time network sync (rdate) overrides a manual set.
 */
#include "cp_page.h"
#include "cp_ui.h"
#include "sys_settings.h"
#include "../common/common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SSHD_CONFIG      "/etc/ssh/sshd_config"
#define RTC_SINCE_EPOCH  "/sys/class/rtc/rtc0/since_epoch"
#define CLOCK_REFRESH_MS 1000
#define RTC_SYNC_SLACK_S 2          /* the two clocks are read a moment apart */
#define SYS_NOTE         "NETWORK TIME SYNC OVERRIDES THIS AT THE NEXT ONLINE BOOT"

/* ── State ──────────────────────────────────────────────────────────────── */

static SshMode ssh_mode = SSH_MODE_UNKNOWN;
static char    now_str[48];         /* "YYYY-MM-DD HH:MM:SS", or "UNKNOWN" */
static char    rtc_str[80];
static uint32_t rtc_color = COLOR_WHITE;
static uint32_t sampled_ms;

static SysDateTime edit;
static SysDateTime pending;         /* what the open confirmation will apply */

static void read_ssh_mode(void) {
    static char buf[16384];         /* a sshd_config is a few KB; a longer one is cut */
    size_t n = 0;
    FILE *f = fopen(SSHD_CONFIG, "r");
    if (f) {
        n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
    }
    buf[n] = '\0';
    ssh_mode = f ? sys_ssh_mode(buf) : SSH_MODE_UNKNOWN;
}

static void fmt_tm(const struct tm *tm, char *out, size_t len) {
    snprintf(out, len, "%04d-%02d-%02d %02d:%02d:%02d", tm->tm_year + 1900,
             tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/* The clock and the RTC; true when what is shown changed. */
static bool read_clock(void) {
    char old_now[sizeof(now_str)], old_rtc[sizeof(rtc_str)];
    memcpy(old_now, now_str, sizeof(now_str));
    memcpy(old_rtc, rtc_str, sizeof(rtc_str));

    time_t t = time(NULL);
    struct tm tm;
    if (localtime_r(&t, &tm)) fmt_tm(&tm, now_str, sizeof(now_str));
    else snprintf(now_str, sizeof(now_str), "UNKNOWN");

    char line[32];
    long rtc_s = 0;
    if (read_file_line(RTC_SINCE_EPOCH, line, sizeof(line)) == 0 &&
        sscanf(line, "%ld", &rtc_s) == 1 && rtc_s > 0) {
        time_t rt = (time_t)rtc_s;
        long diff = (long)(rt - t);
        if (diff < 0) diff = -diff;
        struct tm rtm;
        char hms[32] = "";
        if (localtime_r(&rt, &rtm))
            snprintf(hms, sizeof(hms), "%02d:%02d:%02d", rtm.tm_hour, rtm.tm_min, rtm.tm_sec);
        if (diff <= RTC_SYNC_SLACK_S) {
            snprintf(rtc_str, sizeof(rtc_str), "%s  SYNCED", hms);
            rtc_color = COLOR_GREEN;
        } else {
            snprintf(rtc_str, sizeof(rtc_str), "%s  OFF %ld S", hms, diff);
            rtc_color = COLOR_YELLOW;
        }
    } else {
        snprintf(rtc_str, sizeof(rtc_str), "NOT READABLE");
        rtc_color = COLOR_YELLOW;
    }
    return memcmp(old_now, now_str, sizeof(now_str)) != 0 ||
           memcmp(old_rtc, rtc_str, sizeof(rtc_str)) != 0;
}

static void edit_from_clock(void) {
    time_t t = time(NULL);
    struct tm tm;
    if (!localtime_r(&t, &tm)) return;
    edit = (SysDateTime){ tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                          tm.tm_hour, tm.tm_min };
    if (edit.year < SYS_YEAR_MIN) edit.year = SYS_YEAR_MIN;
    if (edit.year > SYS_YEAR_MAX) edit.year = SYS_YEAR_MAX;
    if (edit.day > sys_days_in_month(edit.year, edit.mon))
        edit.day = sys_days_in_month(edit.year, edit.mon);
}

static void system_page_load(const Config *cfg) {
    (void)cfg;
    read_ssh_mode();
    read_clock();
    edit_from_clock();
}

static void system_page_enter(void) {
    read_ssh_mode();
    read_clock();
    edit_from_clock();
    sampled_ms = get_time_ms();
}

/* ── Layout ─────────────────────────────────────────────────────────────── */

#define SYS_HEADER_H   26
#define SYS_ROW_H      28           /* draw_info_row()'s pitch */
#define SYS_GAP         6
#define SYS_FIELD_GAP   6
#define SYS_STEP_H     36
#define SYS_COL_MAX   110
#define SYS_SET_H      40
#define SYS_LABEL_H    18
#define SYS_VALUE_H    32

static const char *const field_names[SYS_DT_FIELDS] =
    { "YEAR", "MON", "DAY", "HOUR", "MIN" };

static Button minus_btn[SYS_DT_FIELDS], plus_btn[SYS_DT_FIELDS], set_btn;
static int  sec_ssh_y, sec_clock_y, sec_set_y;
static int  ssh_y, utc_y, rtc_y, label_y, value_y, col_w, value_scale, note_y;

static void system_page_layout(void) {
    int y = CONTENT_Y + 6;
    sec_ssh_y = y;    y += SYS_HEADER_H;
    ssh_y = y;        y += SYS_ROW_H;
    y += SYS_GAP;
    sec_clock_y = y;  y += SYS_HEADER_H;
    utc_y = y;        y += SYS_ROW_H;
    rtc_y = y;        y += SYS_ROW_H;
    y += SYS_GAP;
    sec_set_y = y;    y += SYS_HEADER_H;

    col_w = (CONTENT_WIDTH - (SYS_DT_FIELDS - 1) * SYS_FIELD_GAP) / SYS_DT_FIELDS;
    if (col_w > SYS_COL_MAX) col_w = SYS_COL_MAX;
    value_scale = col_w >= 80 ? 3 : 2;

    label_y = y;                      y += SYS_LABEL_H;
    int plus_y = y;                   y += SYS_STEP_H + 4;
    value_y = y;                      y += SYS_VALUE_H;
    int minus_y = y;                  y += SYS_STEP_H;
    int fields_right = CONTENT_LEFT + SYS_DT_FIELDS * col_w + (SYS_DT_FIELDS - 1) * SYS_FIELD_GAP;
    for (int i = 0; i < SYS_DT_FIELDS; i++) {
        int x = CONTENT_LEFT + i * (col_w + SYS_FIELD_GAP);
        button_init_full(&plus_btn[i], x, plus_y, col_w, SYS_STEP_H, "+",
                         RGB(80, 80, 80), COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);
        button_init_full(&minus_btn[i], x, minus_y, col_w, SYS_STEP_H, "-",
                         RGB(80, 80, 80), COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);
    }

    /* SET sits beside the fields where the width allows, else under them. */
    int beside_x = fields_right + 16;
    if (CONTENT_RIGHT - beside_x >= 100) {
        button_init_full(&set_btn, beside_x, value_y - 6, CONTENT_RIGHT - beside_x,
                         SYS_SET_H, "SET", RGB(0, 110, 60), COLOR_WHITE,
                         BTN_COLOR_HIGHLIGHT, 2);
    } else {
        y += 8;
        button_init_full(&set_btn, CONTENT_LEFT, y, CONTENT_WIDTH, SYS_SET_H, "SET",
                         RGB(0, 110, 60), COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 2);
        y += SYS_SET_H;
    }
    y += 8;
    note_y = y;
    y += 8;                            /* scale-1 text */

    /* ⚠️ THE RECEIPT, in the settings stack's shape.  Everything hangs off
     * CONTENT_Y, which comes from a per-unit touch inset, so a button pushed
     * past the touchable rect looks fine in a screenshot and is dead to a
     * finger.  The bottom is the note line (or the lowest button, whichever is
     * lower); the right edge is the widest of the SET button, the info rows at
     * their widest strings and the note, each measured as drawn. */
    {
        int bottom = y - CONTENT_Y;
        int low = (minus_btn[0].y + minus_btn[0].height) - CONTENT_Y;
        if (set_btn.y + set_btn.height - CONTENT_Y > low) low = set_btn.y + set_btn.height - CONTENT_Y;
        if (low > bottom) bottom = low;
        int right = set_btn.x + set_btn.width;
        int value_x = SCREEN_SAFE_WIDTH < 600 ? 150 : 270;     /* draw_info_row()'s column */
        int w = value_x + text_measure_width("2026-10-06 14:00:00", 2);
        if (w > right) right = w;
        w = value_x + text_measure_width("14:00:00  OFF 99999 S", 2);
        if (w > right) right = w;
        w = CONTENT_LEFT + text_measure_width(SYS_NOTE, 1);
        if (w > right) right = w;
        w = CONTENT_LEFT + 10 + text_measure_width("SSH: KEY ONLY", 2);
        if (w > right) right = w;
        const char *verdict = bottom > CONTENT_H     ? "⚠ PAST CONTENT BOTTOM"
                            : right  > CONTENT_RIGHT ? "⚠ PAST CONTENT RIGHT"
                            : "fits";
        printf("control_panel: system stack %s — bottom +%d of CONTENT_H %d, "
               "right %d of CONTENT_RIGHT %d, SET %s (safe %dx%d, %s)\n",
               verdict, bottom, CONTENT_H, right, CONTENT_RIGHT,
               set_btn.x > fields_right ? "beside" : "under",
               SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT,
               CONTENT_WIDTH < 600 ? "portrait" : "landscape");
    }
}

/* ── Draw ───────────────────────────────────────────────────────────────── */

static void system_page_draw(Framebuffer *fb) {
    static const struct { const char *text; uint32_t color; } ssh_text[] = {
        [SSH_MODE_UNKNOWN]  = { "UNKNOWN",  COLOR_ORANGE },
        [SSH_MODE_KEY_ONLY] = { "KEY ONLY", COLOR_GREEN },
        [SSH_MODE_PASSWORD] = { "PASSWORD", COLOR_YELLOW },
    };
    draw_section_header(fb, sec_ssh_y, "REMOTE ACCESS");
    draw_info_row(fb, ssh_y, "SSH:", ssh_text[ssh_mode].text, ssh_text[ssh_mode].color);

    draw_section_header(fb, sec_clock_y, "CLOCK (UTC)");
    draw_info_row(fb, utc_y, "UTC:", now_str, COLOR_WHITE);
    draw_info_row(fb, rtc_y, "RTC:", rtc_str, rtc_color);

    draw_section_header(fb, sec_set_y, "SET DATE AND TIME");
    char s[8];
    const int vals[SYS_DT_FIELDS] = { edit.year, edit.mon, edit.day, edit.hour, edit.min };
    for (int i = 0; i < SYS_DT_FIELDS; i++) {
        int cx = plus_btn[i].x + col_w / 2;
        text_draw_centered(fb, cx, label_y + 7, field_names[i], COLOR_LABEL, 2);
        snprintf(s, sizeof(s), i == SYS_DT_YEAR ? "%04d" : "%02d", vals[i]);
        text_draw_centered(fb, cx, value_y + 12, s, COLOR_WHITE, value_scale);
        button_draw(fb, &plus_btn[i]);
        button_draw(fb, &minus_btn[i]);
    }
    button_draw(fb, &set_btn);
    fb_draw_text(fb, CONTENT_LEFT, note_y, SYS_NOTE, COLOR_LABEL, 1);
}

/* ── Setting the clock ──────────────────────────────────────────────────── */

/* Runs argv (no shell) with output discarded; true if it exited 0. */
static bool run_cmd(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR) return false;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static void apply_clock(Config *cfg) {
    (void)cfg;
    char stamp[24];
    sys_dt_format(&pending, stamp, sizeof(stamp));
    if (!stamp[0]) { cp_status("DATE INVALID, NOT SET", false); return; }
    char *const date_argv[] = { "date", "-s", stamp, NULL };
    char *const hwc_argv[]  = { "hwclock", "-w", NULL };
    if (!run_cmd(date_argv))      { cp_status("DATE SET FAILED", false); return; }
    if (!run_cmd(hwc_argv))       { cp_status("CLOCK SET, RTC WRITE FAILED", false); }
    else                          cp_status("DATE AND RTC SET", true);
    read_clock();
}

/* ── Input ──────────────────────────────────────────────────────────────── */

static CpPageResult system_page_input(Config *cfg, int tx, int ty,
                                      bool touching, uint32_t now) {
    (void)cfg;
    CpPageResult act = CP_PAGE_IDLE;

    for (int i = 0; i < SYS_DT_FIELDS; i++) {
        if (button_update(&plus_btn[i], tx, ty, touching, now)) {
            sys_dt_step(&edit, i, +1);
            act = CP_PAGE_REDRAW;
        }
        if (button_update(&minus_btn[i], tx, ty, touching, now)) {
            sys_dt_step(&edit, i, -1);
            act = CP_PAGE_REDRAW;
        }
    }
    if (button_update(&set_btn, tx, ty, touching, now)) {
        char stamp[24], msg[48];
        pending = edit;
        sys_dt_format(&pending, stamp, sizeof(stamp));
        snprintf(msg, sizeof(msg), "%.19s UTC\nTHEN WRITES THE RTC", stamp);
        cp_confirm("SET DATE AND TIME", msg, "SET", apply_clock);
        return CP_PAGE_IDLE;
    }

    if (now - sampled_ms >= CLOCK_REFRESH_MS) {
        sampled_ms = now;
        if (read_clock()) act = CP_PAGE_REDRAW;
    }
    return act;
}

static int system_page_focusables(UiRect *out, int max) {
    int n = 0;
    for (int i = 0; i < SYS_DT_FIELDS; i++) {
        n = focus_add_button(out, n, max, &plus_btn[i]);
        n = focus_add_button(out, n, max, &minus_btn[i]);
    }
    return focus_add_button(out, n, max, &set_btn);
}

const CpPage cp_system_page = {
    .name       = "System",
    .icon       = NULL,
    .load       = system_page_load,
    .layout     = system_page_layout,
    .enter      = system_page_enter,
    .draw       = system_page_draw,
    .input      = system_page_input,
    .focusables = system_page_focusables,
};
