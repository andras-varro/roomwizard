/* monitor_page.c — control_panel's Monitor page: uptime, load, SoC temperature,
 * CPU history, memory, storage.
 *
 * Opened from the home grid's Monitor tile, and the one home for the live
 * system figures.  Exposed only as
 * cp_monitor_page (cp_page.h); its state lives in this file.
 *
 * Display only: nothing is saved, so there is no reset_defaults.  The figures
 * are sampled on entry and then once a second from input(), which the main
 * loop calls every iteration; draw() paints the last sample and reads nothing,
 * so a repaint for any other reason costs no /proc reads.
 *
 * The CPU graph rides the same once-a-second sample: one point per sample, so
 * it adds no repaint of its own.  The math and the ring are cpu_load.c's.
 * On entry the graph is pre-filled from rwmond's published history when the
 * daemon is running (cpu_seed_from_daemon); without it the graph starts empty.
 *
 * The SoC TEMP row exists only when a thermal zone reads (soc_temp.h): on a
 * kernel without the bandgap driver it is not drawn at all, and the rows
 * below move up — an absent figure is removed, not greyed.
 */
#include "cp_page.h"
#include "cp_ui.h"
#include "cpu_load.h"
#include "soc_temp.h"
#include "../sysmon/mon_ring.h"
#include "../common/common.h"

#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define MON_REFRESH_MS 1000
/* The CPU graph's span in minutes: one bar per refresh, CPU_HIST_N bars. */
#define MON_GRAPH_SPAN_MIN (CPU_HIST_N * MON_REFRESH_MS / 60000)

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

/* The graph's caption, permille in; -1 = no sample yet.  The panel's own cost
 * is shown here and kept out of the bars, so the graph is the rest of the
 * device (cpu_load.h). */
static void fmt_cpu(int others_pm, int self_pm, char *buf, size_t len) {
    if (others_pm < 0 || self_pm < 0)
        snprintf(buf, len, "CPU --  LAST %d MIN  (PANEL --, NOT GRAPHED)", MON_GRAPH_SPAN_MIN);
    else
        snprintf(buf, len, "CPU %d%%  LAST %d MIN  (PANEL %d%%, NOT GRAPHED)",
                 (others_pm + 5) / 10, MON_GRAPH_SPAN_MIN, (self_pm + 5) / 10);
}

static struct {
    char     uptime[64];
    char     load[64];
    MemInfo  mem;
    DiskInfo disk[NUM_MOUNT_POINTS];
} sample;
static uint32_t sampled_ms;

/* CPU: the last snap, the history, and the newest shares (-1 = none yet). */
static CpuSnap    cpu_prev;
static CpuHistory cpu_hist;
static int        cpu_others_pm = -1, cpu_self_pm = -1;

static void cpu_snap(CpuSnap *s) {
    char stat[256], self[512], up[64];
    s->valid = read_file_line("/proc/stat", stat, sizeof(stat)) == 0
            && read_file_line("/proc/self/stat", self, sizeof(self)) == 0
            && read_file_line("/proc/uptime", up, sizeof(up)) == 0
            && cpu_parse_stat(stat, &s->busy) == 0
            && cpu_parse_self(self, &s->self) == 0
            && cpu_parse_uptime(up, &s->wall_cs) == 0;
}

/* One point per call: the share since the previous call. */
static void cpu_sample(void) {
    static long clk_tck;
    CpuSnap cur;
    if (clk_tck <= 0) clk_tck = sysconf(_SC_CLK_TCK);
    cpu_snap(&cur);
    if (clk_tck > 0
        && cpu_share(&cpu_prev, &cur, (unsigned)clk_tck,
                     &cpu_others_pm, &cpu_self_pm) == 0)
        cpu_hist_push(&cpu_hist, cpu_others_pm);
    cpu_prev = cur;
}

/* rwmond's ring (sysmon/mon_ring.h), if the daemon is running: its CPU
 * column pre-fills the graph so the page opens on the last two minutes.  A
 * file older than 3 s means the daemon is gone, and the page then starts empty
 * and samples for itself exactly as without it.  The daemon's figure excludes
 * its own ticks where this page excludes the panel's; both are the same
 * wall-time share (cpu_load.h). */
static void cpu_seed_from_daemon(unsigned long long now_cs) {
    static char text[MON_RING_TEXT_MAX];
    static MonRing ring;
    unsigned long long file_cs;
    unsigned flags;
    FILE *f = fopen(MON_RING_PATH, "r");
    if (!f) return;
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    if (mon_ring_parse(text, &ring, &file_cs, &flags) != 0
        || !mon_ring_fresh(file_cs, now_cs, 300))
        return;
    for (int i = 0; i < ring.count; i++) {
        int pm = mon_ring_get(&ring, i)->cpu_pm;
        if (pm != MON_ABSENT) cpu_hist_push(&cpu_hist, pm);
    }
}

/* The page was closed, so nothing sampled the gap: start the graph afresh
 * rather than draw one point averaged over however long that was — from
 * rwmond's history when it is fresh, else empty. */
static void cpu_restart(void) {
    cpu_hist_clear(&cpu_hist);
    cpu_others_pm = cpu_self_pm = -1;
    cpu_snap(&cpu_prev);
    if (cpu_prev.valid) cpu_seed_from_daemon(cpu_prev.wall_cs);
}

/* SoC temperature: which thermal zone, and its newest reading. */
#define MON_MAX_ZONES 8
static int  temp_zone = -2;      /* -2 not looked up yet, -1 none, else N */
static int  temp_mc;
static bool temp_ok;

static int temp_read_zone(int n, int *mc) {
    char path[64], raw[32];
    snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", n);
    return read_file_line(path, raw, sizeof(raw)) == 0
        && soc_temp_parse(raw, mc) == 0 ? 0 : -1;
}

/* Whether the row exists.  The zone is looked up once — the first whose temp
 * reads and parses — because the sensor driver is built into the image, so
 * the answer cannot change within a boot; layout() and the sampler both ask. */
static bool temp_present(void) {
    if (temp_zone == -2) {
        temp_zone = -1;
        for (int n = 0; n < MON_MAX_ZONES; n++)
            if (temp_read_zone(n, &temp_mc) == 0) { temp_zone = n; break; }
    }
    return temp_zone >= 0;
}

static void monitor_sample(void) {
    read_uptime(sample.uptime, sizeof(sample.uptime));
    read_loadavg(sample.load, sizeof(sample.load));
    temp_ok = temp_present() && temp_read_zone(temp_zone, &temp_mc) == 0;
    read_meminfo(&sample.mem);
    for (int i = 0; i < NUM_MOUNT_POINTS; i++)
        read_disk_usage(mount_points[i], &sample.disk[i]);
}

/* Nothing to read from the Config; sampling here only means no frame can ever
 * show the zeroed struct. */
static void monitor_page_load(const Config *cfg) {
    (void)cfg;
    monitor_sample();
    cpu_restart();
    sampled_ms = get_time_ms();
}

static void monitor_page_enter(void) {
    monitor_sample();
    cpu_restart();
    sampled_ms = get_time_ms();
}

/* ── Layout ─────────────────────────────────────────────────────────────── */

#define MON_ROW_H      24   /* a scale-2 label/value row */
#define MON_HEADER_H   26   /* draw_section_header() and the space under it */
#define MON_LINE_H     12   /* a scale-1 text line and its gap to the next */
#define MON_BAR_H      16
#define MON_METER_GAP  10
#define MON_LOAD_LABEL "LOAD AVG:"
#define MON_TEMP_LABEL "SOC TEMP:"   /* values align after the wider of these */
#define MON_GRAPH_MIN_H 24  /* below this a CPU graph is a smear */
#define MON_GRAPH_MAX_H 96  /* past this it only takes room from nothing */
#define MON_SIDE_MIN_W 240  /* graph beside UPTIME/LOAD only if this wide */
#define MON_FIT_SLACK   4   /* left under the last bar when growing the graph */

static int  uptime_y, load_y, temp_y, value_x;
static int  mon_rows;   /* scale-2 rows at the top: 2, or 3 with SOC TEMP */
static int  sec_mem_y, ram_y, swap_y, sec_disk_y;
static int  disk_y[NUM_MOUNT_POINTS];
static int  bar_x, bar_w;
static int  cpu_x, cpu_label_y, graph_y, graph_w, graph_h;
static bool cpu_beside;

/* A meter is `lines` scale-1 text lines, then its bar; returns the y after it. */
static int meter_bottom(int y, int lines) {
    return y + lines * MON_LINE_H + MON_BAR_H;
}

/* MEMORY and STORAGE from y down; returns the y under the last storage bar. */
static int place_meters(int y) {
    sec_mem_y = y;                y += MON_HEADER_H;
    ram_y     = y;                y = meter_bottom(y, 2) + MON_METER_GAP;
    swap_y    = y;                y = meter_bottom(y, 1) + MON_METER_GAP;

    sec_disk_y = y;               y += MON_HEADER_H;
    for (int i = 0; i < NUM_MOUNT_POINTS; i++) {
        disk_y[i] = y;
        y = meter_bottom(y, 1);
        if (i < NUM_MOUNT_POINTS - 1) y += MON_METER_GAP;
    }
    return y;
}

/* UPTIME, LOAD, SOC TEMP (if present) and the CPU graph, with the graph grown
 * by `grow` px past its minimum; then the meters.  Returns the stack's bottom
 * y.  The graph goes BESIDE the scale-2 rows when the room right of their
 * widest value is at least MON_SIDE_MIN_W (landscape), else UNDER them, full
 * width (portrait). */
static int place_stack(int grow) {
    int y = CONTENT_Y + 6;
    uptime_y = y;
    load_y   = y + MON_ROW_H;
    temp_y   = y + 2 * MON_ROW_H;   /* drawn only when mon_rows is 3 */
    if (cpu_beside) {
        /* The rows' block is as tall as the graph and its label need. */
        int block = mon_rows * MON_ROW_H + grow;
        cpu_label_y = uptime_y;
        graph_y = cpu_label_y + MON_LINE_H + 2;
        graph_h = block - (MON_LINE_H + 2);
        y += block + 6;
    } else {
        y += mon_rows * MON_ROW_H + 6;
        cpu_label_y = y;
        graph_y = y + MON_LINE_H + 2;
        graph_h = MON_GRAPH_MIN_H + grow;
        y = graph_y + graph_h + MON_METER_GAP;
    }
    return place_meters(y);
}

/* Re-run whenever the logical screen changes (rebuild_ui()).  One column of
 * meters in both orientations: every row is one line of text or one bar, so
 * the stack is only as wide as its longest line, and landscape's shorter
 * CONTENT_H is the constraint the receipt below checks.  The CPU graph takes
 * whatever height is left, between MON_GRAPH_MIN_H and MON_GRAPH_MAX_H. */
static void monitor_page_layout(void) {
    mon_rows = temp_present() ? 3 : 2;
    int label_w = text_measure_width(MON_LOAD_LABEL, 2);
    if (mon_rows == 3 && text_measure_width(MON_TEMP_LABEL, 2) > label_w)
        label_w = text_measure_width(MON_TEMP_LABEL, 2);
    value_x  = CONTENT_LEFT + 10 + label_w + 12;
    bar_x = CONTENT_LEFT + 10;
    bar_w = CONTENT_WIDTH - 20;

    /* Where the scale-2 values end: their worst cases, as the receipt uses. */
    char wv[64];
    format_uptime(999 * 86400 + 23 * 3600 + 59 * 60 + 59, wv, sizeof(wv));
    int values_right = value_x + text_measure_width(wv, 2);
    int w2 = value_x + text_measure_width("99.99 99.99 99.99", 2);
    if (w2 > values_right) values_right = w2;
    int side_x = values_right + 20;
    cpu_beside = (bar_x + bar_w) - side_x >= MON_SIDE_MIN_W;
    cpu_x   = cpu_beside ? side_x : bar_x;
    graph_w = (bar_x + bar_w) - cpu_x;

    /* Two passes: the minimum graph, then grow it into the spare height.  In
     * the beside case the minimum is what the two rows already give it. */
    int min_grow = 0;
    int bottom   = place_stack(min_grow) - CONTENT_Y;
    int max_grow = cpu_beside ? MON_GRAPH_MAX_H - (mon_rows * MON_ROW_H - MON_LINE_H - 2)
                              : MON_GRAPH_MAX_H - MON_GRAPH_MIN_H;
    int grow = CONTENT_H - MON_FIT_SLACK - bottom;
    if (grow > max_grow) grow = max_grow;
    if (grow < 0) grow = 0;
    int y = place_stack(grow);

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
        if (mon_rows == 3) {
            soc_temp_format(SOC_TEMP_MIN_MC, s, sizeof(s));
            w = value_x + text_measure_width(s, 2);
            if (w > right) right = w;
        }
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
        fmt_cpu(1000, 1000, s, sizeof(s));
        w = cpu_x + text_measure_width(s, 1);
        if (w > right) right = w;
        const char *verdict = bottom > CONTENT_H     ? "⚠ PAST CONTENT BOTTOM"
                            : right  > CONTENT_RIGHT ? "⚠ PAST CONTENT RIGHT"
                            : graph_h < MON_GRAPH_MIN_H ? "⚠ CPU GRAPH TOO SHORT"
                            : "fits";
        printf("control_panel: monitor stack %s — bottom +%d of CONTENT_H %d, "
               "right %d of CONTENT_RIGHT %d, cpu graph %dx%d %s, soc temp %s (safe %dx%d, %s)\n",
               verdict, bottom, CONTENT_H, right, CONTENT_RIGHT,
               graph_w, graph_h, cpu_beside ? "beside" : "under",
               mon_rows == 3 ? "shown" : "absent",
               SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT,
               CONTENT_WIDTH < 600 ? "portrait" : "landscape");
    }
}

/* ── Draw and input ─────────────────────────────────────────────────────── */

static void draw_row(Framebuffer *fb, int y, const char *label, const char *value) {
    fb_draw_text(fb, CONTENT_LEFT + 10, y, label, COLOR_LABEL, 2);
    fb_draw_text(fb, value_x, y, value, COLOR_WHITE, 2);
}

/* The CPU history: one column per held sample, newest at the right edge, so
 * a fresh page fills from the right.  Coloured by level like a usage bar, with
 * a dim rule at 50 %.  Reads nothing — the samples are cpu_sample()'s. */
static void draw_cpu_graph(Framebuffer *fb) {
    char s[96];
    fmt_cpu(cpu_others_pm, cpu_self_pm, s, sizeof(s));
    fb_draw_text(fb, cpu_x, cpu_label_y, s, COLOR_WHITE, 1);

    fb_fill_rect(fb, cpu_x, graph_y, graph_w, graph_h, RGB(8, 8, 14));
    fb_draw_rect(fb, cpu_x, graph_y, graph_w, graph_h, RGB(70, 70, 90));
    int ix = cpu_x + 1, iy = graph_y + 1, iw = graph_w - 2, ih = graph_h - 2;
    if (iw <= 0 || ih <= 0) return;
    fb_fill_rect(fb, ix, iy + ih / 2, iw, 1, RGB(45, 45, 60));

    int first = CPU_HIST_N - cpu_hist.count;   /* slot of the oldest sample */
    for (int i = 0; i < cpu_hist.count; i++) {
        int pm   = cpu_hist_get(&cpu_hist, i);
        int slot = first + i;
        int x0 = ix + slot * iw / CPU_HIST_N;
        int x1 = ix + (slot + 1) * iw / CPU_HIST_N;
        int h  = pm * ih / 1000;
        if (pm > 0 && h == 0) h = 1;           /* a non-zero sample stays visible */
        if (h <= 0 || x1 <= x0) continue;
        uint32_t c = pm >= 800 ? RGB(210, 70, 60)
                   : pm >= 500 ? RGB(220, 170, 40)
                   :             RGB(60, 180, 90);
        fb_fill_rect(fb, x0, iy + ih - h, x1 - x0, h, c);
    }
}

static void monitor_page_draw(Framebuffer *fb) {
    const MemInfo *mi = &sample.mem;
    char s[160];

    draw_row(fb, uptime_y, "UPTIME:", sample.uptime);
    draw_row(fb, load_y, MON_LOAD_LABEL, sample.load);
    if (mon_rows == 3) {
        /* The zone exists; a single failed read shows "--", not a stale figure. */
        if (temp_ok) soc_temp_format(temp_mc, s, sizeof(s));
        else         snprintf(s, sizeof(s), "--");
        draw_row(fb, temp_y, MON_TEMP_LABEL, s);
    }
    draw_cpu_graph(fb);

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

/* No widgets: the only thing that changes the screen is the clock.  The CPU
 * point is taken on the same tick, so the graph costs no repaint of its own. */
static CpPageResult monitor_page_input(Config *cfg, int tx, int ty,
                                       bool touching, uint32_t now) {
    (void)cfg; (void)tx; (void)ty; (void)touching;
    if (now - sampled_ms < MON_REFRESH_MS) return CP_PAGE_IDLE;
    monitor_sample();
    cpu_sample();
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
