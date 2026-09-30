/* monitor_page.c — control_panel's Monitor page: uptime, load, memory, storage.
 *
 * Opened from the home grid's Monitor tile, and the one home for the live
 * system figures.  Exposed only as
 * cp_monitor_page (cp_page.h); its state lives in this file.
 *
 * Display only: nothing is saved, so there is no reset_defaults.  The figures
 * are sampled on entry and then once a second from input(), which the main
 * loop calls every iteration; draw() paints the last sample and reads nothing,
 * so a repaint for any other reason costs no /proc reads.
 */
#include "cp_page.h"
#include "cp_ui.h"
#include "../common/common.h"

#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>

#define MON_REFRESH_MS 1000

/* ── Sampling ──────────────────────────────────────────────────────────── */

typedef struct {
    unsigned long total_kb, free_kb, available_kb;
    unsigned long buffers_kb, cached_kb;
    unsigned long swap_total_kb, swap_free_kb;
} MemInfo;

typedef struct {
    unsigned long total_kb, free_kb, used_kb;
    int valid;
} DiskInfo;

static const char *mount_points[] = {
    "/",
    "/home/root/data",
    "/home/root/log",
    "/home/root/backup"
};
#define NUM_MOUNT_POINTS ((int)(sizeof(mount_points) / sizeof(mount_points[0])))

static void read_meminfo(MemInfo *info) {
    memset(info, 0, sizeof(*info));
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        unsigned long val = 0;
        if (sscanf(line, "MemTotal: %lu kB", &val) == 1) info->total_kb = val;
        else if (sscanf(line, "MemFree: %lu kB", &val) == 1) info->free_kb = val;
        else if (sscanf(line, "MemAvailable: %lu kB", &val) == 1) info->available_kb = val;
        else if (sscanf(line, "Buffers: %lu kB", &val) == 1) info->buffers_kb = val;
        else if (sscanf(line, "Cached: %lu kB", &val) == 1) info->cached_kb = val;
        else if (sscanf(line, "SwapTotal: %lu kB", &val) == 1) info->swap_total_kb = val;
        else if (sscanf(line, "SwapFree: %lu kB", &val) == 1) info->swap_free_kb = val;
    }
    fclose(fp);
}

static int read_disk_usage(const char *mp, DiskInfo *info) {
    memset(info, 0, sizeof(*info));
    struct statvfs st;
    if (statvfs(mp, &st) != 0) { info->valid = 0; return -1; }
    info->total_kb = (unsigned long)((unsigned long long)st.f_blocks * st.f_frsize / 1024);
    info->free_kb  = (unsigned long)((unsigned long long)st.f_bfree  * st.f_frsize / 1024);
    info->used_kb  = info->total_kb - info->free_kb;
    info->valid    = 1;
    return 0;
}

/* The three averages only: the run-queue and last-PID fields that follow them
 * in /proc/loadavg do not fit a portrait row at scale 2. */
static void read_loadavg(char *buf, size_t len) {
    char raw[128], a[16], b[16], c[16];
    if (read_file_line("/proc/loadavg", raw, sizeof(raw)) < 0
        || sscanf(raw, "%15s %15s %15s", a, b, c) != 3)
        snprintf(buf, len, "N/A");
    else
        snprintf(buf, len, "%s %s %s", a, b, c);
}

/* Seconds as "Nd Nh Nm Ns"; the receipt formats its worst case through here. */
static void format_uptime(int t, char *buf, size_t len) {
    snprintf(buf, len, "%dd %dh %dm %ds",
             t / 86400, (t % 86400) / 3600, (t % 3600) / 60, t % 60);
}

static void read_uptime(char *buf, size_t len) {
    char raw[64];
    double secs = 0;
    if (read_file_line("/proc/uptime", raw, sizeof(raw)) < 0
        || sscanf(raw, "%lf", &secs) != 1)
        snprintf(buf, len, "N/A");
    else
        format_uptime((int)secs, buf, len);
}

static void format_bytes(unsigned long kb, char *buf, size_t len) {
    if (kb >= 1048576) snprintf(buf, len, "%.1f GB", (double)kb / 1048576.0);
    else if (kb >= 1024) snprintf(buf, len, "%.1f MB", (double)kb / 1024.0);
    else snprintf(buf, len, "%lu KB", kb);
}

/* The meter lines, one formatter each, shared by draw() and the receipt so the
 * width measured is the width drawn. */
static void fmt_used_of(const char *name, unsigned long used, unsigned long total,
                        char *buf, size_t len) {
    char u[32], t[32];
    format_bytes(used, u, sizeof(u));
    format_bytes(total, t, sizeof(t));
    snprintf(buf, len, "%s  USED %s OF %s", name, u, t);
}

static void fmt_ram_detail(unsigned long free_kb, unsigned long avail_kb,
                           unsigned long cache_kb, char *buf, size_t len) {
    char f[32], a[32], c[32];
    format_bytes(free_kb, f, sizeof(f));
    format_bytes(avail_kb, a, sizeof(a));
    format_bytes(cache_kb, c, sizeof(c));
    snprintf(buf, len, "FREE %s  AVAILABLE %s  BUF/CACHE %s", f, a, c);
}

static void fmt_disk(const char *mp, unsigned long used, unsigned long total,
                     unsigned long free_kb, char *buf, size_t len) {
    char u[32], t[32], f[32];
    format_bytes(used, u, sizeof(u));
    format_bytes(total, t, sizeof(t));
    format_bytes(free_kb, f, sizeof(f));
    snprintf(buf, len, "%s  %s / %s  (%s FREE)", mp, u, t, f);
}

static struct {
    char     uptime[64];
    char     load[64];
    MemInfo  mem;
    DiskInfo disk[NUM_MOUNT_POINTS];
} sample;
static uint32_t sampled_ms;

static void monitor_sample(void) {
    read_uptime(sample.uptime, sizeof(sample.uptime));
    read_loadavg(sample.load, sizeof(sample.load));
    read_meminfo(&sample.mem);
    for (int i = 0; i < NUM_MOUNT_POINTS; i++)
        read_disk_usage(mount_points[i], &sample.disk[i]);
}

/* Nothing to read from the Config; sampling here only means no frame can ever
 * show the zeroed struct. */
static void monitor_page_load(const Config *cfg) {
    (void)cfg;
    monitor_sample();
    sampled_ms = get_time_ms();
}

static void monitor_page_enter(void) {
    monitor_sample();
    sampled_ms = get_time_ms();
}

/* ── Layout ─────────────────────────────────────────────────────────────── */

#define MON_ROW_H      24   /* a scale-2 label/value row */
#define MON_HEADER_H   26   /* draw_section_header() and the space under it */
#define MON_LINE_H     12   /* a scale-1 text line and its gap to the next */
#define MON_BAR_H      16
#define MON_METER_GAP  10
#define MON_LOAD_LABEL "LOAD AVG:"   /* the wider label: values align after it */

static int  uptime_y, load_y, value_x;
static int  sec_mem_y, ram_y, swap_y, sec_disk_y;
static int  disk_y[NUM_MOUNT_POINTS];
static int  bar_x, bar_w;

/* A meter is `lines` scale-1 text lines, then its bar; returns the y after it. */
static int meter_bottom(int y, int lines) {
    return y + lines * MON_LINE_H + MON_BAR_H;
}

/* Re-run whenever the logical screen changes (rebuild_ui()).  One column in
 * both orientations: every row is one line of text or one bar, so the stack is
 * only as wide as its longest line, and landscape's shorter CONTENT_H is the
 * constraint the receipt below checks. */
static void monitor_page_layout(void) {
    int y = CONTENT_Y + 6;
    value_x  = CONTENT_LEFT + 10 + text_measure_width(MON_LOAD_LABEL, 2) + 12;
    uptime_y = y;                 y += MON_ROW_H;
    load_y   = y;                 y += MON_ROW_H + 6;

    sec_mem_y = y;                y += MON_HEADER_H;
    ram_y     = y;                y = meter_bottom(y, 2) + MON_METER_GAP;
    swap_y    = y;                y = meter_bottom(y, 1) + MON_METER_GAP;

    sec_disk_y = y;               y += MON_HEADER_H;
    for (int i = 0; i < NUM_MOUNT_POINTS; i++) {
        disk_y[i] = y;
        y = meter_bottom(y, 1);
        if (i < NUM_MOUNT_POINTS - 1) y += MON_METER_GAP;
    }

    bar_x = CONTENT_LEFT + 10;
    bar_w = CONTENT_WIDTH - 20;

    /* ⚠️ THE RECEIPT, in the settings stack's shape.  Everything here hangs off
     * CONTENT_Y, which comes from a per-unit touch inset, so a row pushed past
     * the content rect looks fine on one unit and clips on another.  The last
     * storage bar is the lowest thing drawn; the right edge is the widest line,
     * each formatted through the draw path's own formatter with worst-case
     * values (1048575 kB is format_bytes()'s longest, "1024.0 MB"). */
    {
        const unsigned long W = 1048575;
        char s[160];
        int bottom = y - CONTENT_Y;
        int right  = bar_x + bar_w;
        int w;

        format_uptime(999 * 86400 + 23 * 3600 + 59 * 60 + 59, s, sizeof(s));
        w = value_x + text_measure_width(s, 2);
        if (w > right) right = w;
        w = value_x + text_measure_width("99.99 99.99 99.99", 2);
        if (w > right) right = w;
        fmt_used_of("SWAP", W, W, s, sizeof(s));
        w = bar_x + text_measure_width(s, 1);
        if (w > right) right = w;
        fmt_ram_detail(W, W, W, s, sizeof(s));
        w = bar_x + text_measure_width(s, 1);
        if (w > right) right = w;
        for (int i = 0; i < NUM_MOUNT_POINTS; i++) {
            fmt_disk(mount_points[i], W, W, W, s, sizeof(s));
            w = bar_x + text_measure_width(s, 1);
            if (w > right) right = w;
        }
        const char *verdict = bottom > CONTENT_H     ? "⚠ PAST CONTENT BOTTOM"
                            : right  > CONTENT_RIGHT ? "⚠ PAST CONTENT RIGHT"
                            : "fits";
        printf("control_panel: monitor stack %s — bottom +%d of CONTENT_H %d, "
               "right %d of CONTENT_RIGHT %d (safe %dx%d, %s)\n",
               verdict, bottom, CONTENT_H, right, CONTENT_RIGHT,
               SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT,
               CONTENT_WIDTH < 600 ? "portrait" : "landscape");
    }
}

/* ── Draw and input ─────────────────────────────────────────────────────── */

static void draw_row(Framebuffer *fb, int y, const char *label, const char *value) {
    fb_draw_text(fb, CONTENT_LEFT + 10, y, label, COLOR_LABEL, 2);
    fb_draw_text(fb, value_x, y, value, COLOR_WHITE, 2);
}

static void monitor_page_draw(Framebuffer *fb) {
    const MemInfo *mi = &sample.mem;
    char s[160];

    draw_row(fb, uptime_y, "UPTIME:", sample.uptime);
    draw_row(fb, load_y, MON_LOAD_LABEL, sample.load);

    draw_section_header(fb, sec_mem_y, "MEMORY");
    unsigned long used = mi->total_kb - mi->free_kb - mi->buffers_kb - mi->cached_kb;
    if (used > mi->total_kb) used = 0;
    fmt_used_of("RAM", used, mi->total_kb, s, sizeof(s));
    fb_draw_text(fb, bar_x, ram_y, s, COLOR_WHITE, 1);
    fmt_ram_detail(mi->free_kb, mi->available_kb,
                   mi->buffers_kb + mi->cached_kb, s, sizeof(s));
    fb_draw_text(fb, bar_x, ram_y + MON_LINE_H, s, COLOR_LABEL, 1);
    draw_usage_bar(fb, bar_x, ram_y + 2 * MON_LINE_H, bar_w, MON_BAR_H,
                   used, mi->total_kb, "");

    if (mi->swap_total_kb == 0) {
        fb_draw_text(fb, bar_x, swap_y, "SWAP  NONE", COLOR_LABEL, 1);
    } else {
        unsigned long su = mi->swap_total_kb - mi->swap_free_kb;
        fmt_used_of("SWAP", su, mi->swap_total_kb, s, sizeof(s));
        fb_draw_text(fb, bar_x, swap_y, s, COLOR_WHITE, 1);
        draw_usage_bar(fb, bar_x, swap_y + MON_LINE_H, bar_w, MON_BAR_H,
                       su, mi->swap_total_kb, "");
    }

    draw_section_header(fb, sec_disk_y, "STORAGE");
    for (int i = 0; i < NUM_MOUNT_POINTS; i++) {
        const DiskInfo *di = &sample.disk[i];
        if (!di->valid) {
            snprintf(s, sizeof(s), "%s  N/A (NOT MOUNTED)", mount_points[i]);
            fb_draw_text(fb, bar_x, disk_y[i], s, RGB(200, 80, 80), 1);
            continue;
        }
        fmt_disk(mount_points[i], di->used_kb, di->total_kb, di->free_kb,
                 s, sizeof(s));
        fb_draw_text(fb, bar_x, disk_y[i], s, COLOR_WHITE, 1);
        draw_usage_bar(fb, bar_x, disk_y[i] + MON_LINE_H, bar_w, MON_BAR_H,
                       di->used_kb, di->total_kb, "");
    }
}

/* No widgets: the only thing that changes the screen is the clock. */
static CpPageResult monitor_page_input(Config *cfg, int tx, int ty,
                                       bool touching, uint32_t now) {
    (void)cfg; (void)tx; (void)ty; (void)touching;
    if (now - sampled_ms < MON_REFRESH_MS) return CP_PAGE_IDLE;
    monitor_sample();
    sampled_ms = now;
    return CP_PAGE_REDRAW;
}

const CpPage cp_monitor_page = {
    .name   = "Monitor",
    .icon   = "cp_monitor",
    .load   = monitor_page_load,
    .layout = monitor_page_layout,
    .enter  = monitor_page_enter,
    .draw   = monitor_page_draw,
    .input  = monitor_page_input,
};
