/**
 * Hardware Diagnostics GUI for RoomWizard
 *
 * Multi-page touchscreen tool displaying system information:
 *   Page 1: System  — kernel version, uptime, CPU, load average
 *   Page 2: Memory  — RAM and swap usage with bar charts
 *   Page 3: Storage — disk space per mounted partition
 *   Page 4: Hardware — LED brightness, backlight, framebuffer info
 *   Page 5: Config  — current config file settings
 *
 * Navigation: tap anywhere to advance page, top-right corner (x>700, y<40)
 * to exit.
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include "common/hardware.h"
#include "common/common.h"
#include "common/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/statvfs.h>

/* ── Screen constants ───────────────────────────────────────────────────── */

#define SCREEN_W 800
#define SCREEN_H 480

/* ── Page definitions ───────────────────────────────────────────────────── */

#define PAGE_SYSTEM   0
#define PAGE_MEMORY   1
#define PAGE_STORAGE  2
#define PAGE_HARDWARE 3
#define PAGE_CONFIG   4
#define NUM_PAGES     5

static const char *page_titles[] = {
    "SYSTEM INFO",
    "MEMORY",
    "STORAGE",
    "HARDWARE",
    "CONFIGURATION"
};

/* ── Color palette ──────────────────────────────────────────────────────── */

#define COLOR_BG            RGB(20, 20, 30)
#define COLOR_HEADER        COLOR_CYAN
#define COLOR_DATA          COLOR_WHITE
#define COLOR_LABEL         RGB(180, 180, 180)
#define COLOR_SECTION_LINE  RGB(60, 60, 80)
#define COLOR_BAR_BG        RGB(40, 40, 40)
#define COLOR_PAGE_IND      RGB(120, 120, 120)

/* ── Data structures ────────────────────────────────────────────────────── */

typedef struct {
    unsigned long total_kb;
    unsigned long free_kb;
    unsigned long available_kb;
    unsigned long buffers_kb;
    unsigned long cached_kb;
    unsigned long swap_total_kb;
    unsigned long swap_free_kb;
} MemInfo;

typedef struct {
    unsigned long total_kb;
    unsigned long free_kb;
    unsigned long used_kb;
    int valid;  /* 1 if statvfs succeeded */
} DiskInfo;

/* ── Mount points to check ──────────────────────────────────────────────── */

static const char *mount_points[] = {
    "/",
    "/home/root/data",
    "/home/root/log",
    "/home/root/backup"
};
#define NUM_MOUNT_POINTS 4

/* ── Global state ───────────────────────────────────────────────────────── */

static volatile bool running = true;

static void signal_handler(int sig) {
    (void)sig;
    running = false;
}

/* ════════════════════════════════════════════════════════════════════════
 * DATA COLLECTION HELPERS
 * ════════════════════════════════════════════════════════════════════════ */

/**
 * Read a single line from a file path into buf.
 * Returns 0 on success, -1 on failure.
 */
static int read_file_line(const char *path, char *buf, size_t len) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        buf[0] = '\0';
        return -1;
    }
    if (!fgets(buf, (int)len, fp)) {
        buf[0] = '\0';
        fclose(fp);
        return -1;
    }
    fclose(fp);
    /* Strip trailing newline */
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';
    return 0;
}

/**
 * Read an integer value from a sysfs file.
 * Returns the value, or -1 on failure.
 */
static int read_sysfs_int(const char *path) {
    char buf[64];
    if (read_file_line(path, buf, sizeof(buf)) < 0)
        return -1;
    return atoi(buf);
}

/**
 * Parse /proc/meminfo into a MemInfo structure.
 */
static void read_meminfo(MemInfo *info) {
    memset(info, 0, sizeof(*info));
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        unsigned long val = 0;
        if (sscanf(line, "MemTotal: %lu kB", &val) == 1)
            info->total_kb = val;
        else if (sscanf(line, "MemFree: %lu kB", &val) == 1)
            info->free_kb = val;
        else if (sscanf(line, "MemAvailable: %lu kB", &val) == 1)
            info->available_kb = val;
        else if (sscanf(line, "Buffers: %lu kB", &val) == 1)
            info->buffers_kb = val;
        else if (sscanf(line, "Cached: %lu kB", &val) == 1)
            info->cached_kb = val;
        else if (sscanf(line, "SwapTotal: %lu kB", &val) == 1)
            info->swap_total_kb = val;
        else if (sscanf(line, "SwapFree: %lu kB", &val) == 1)
            info->swap_free_kb = val;
    }
    fclose(fp);
}

/**
 * Read disk usage for a mount point using statvfs().
 * Returns 0 on success, -1 on failure (info->valid set accordingly).
 */
static int read_disk_usage(const char *mount_point, DiskInfo *info) {
    memset(info, 0, sizeof(*info));
    struct statvfs st;
    if (statvfs(mount_point, &st) != 0) {
        info->valid = 0;
        return -1;
    }
    info->total_kb = (unsigned long)((unsigned long long)st.f_blocks * st.f_frsize / 1024);
    info->free_kb  = (unsigned long)((unsigned long long)st.f_bfree  * st.f_frsize / 1024);
    info->used_kb  = info->total_kb - info->free_kb;
    info->valid    = 1;
    return 0;
}

/**
 * Parse /proc/cpuinfo for model and clock strings.
 * Looks for "Hardware :" / "model name" and "BogoMIPS".
 */
static void read_cpuinfo(char *model, size_t model_len,
                         char *clock, size_t clock_len) {
    model[0] = '\0';
    clock[0] = '\0';

    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        snprintf(model, model_len, "N/A");
        snprintf(clock, clock_len, "N/A");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        /* CPU model — try "Hardware" first (ARM), then "model name" (x86) */
        if (model[0] == '\0') {
            char *colon;
            if (strncmp(line, "Hardware", 8) == 0 ||
                strncmp(line, "model name", 10) == 0) {
                colon = strchr(line, ':');
                if (colon) {
                    colon++; /* skip ':' */
                    while (*colon == ' ' || *colon == '\t') colon++;
                    /* trim newline */
                    char *nl = strchr(colon, '\n');
                    if (nl) *nl = '\0';
                    snprintf(model, model_len, "%s", colon);
                }
            }
        }
        /* Clock speed from BogoMIPS */
        if (clock[0] == '\0' && strncmp(line, "BogoMIPS", 8) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                char *nl = strchr(colon, '\n');
                if (nl) *nl = '\0';
                snprintf(clock, clock_len, "%s BogoMIPS", colon);
            }
        }
    }
    fclose(fp);

    if (model[0] == '\0') snprintf(model, model_len, "Unknown");
    if (clock[0] == '\0') snprintf(clock, clock_len, "Unknown");
}

/**
 * Read /proc/loadavg into a human-readable string.
 */
static void read_loadavg(char *buf, size_t len) {
    if (read_file_line("/proc/loadavg", buf, len) < 0)
        snprintf(buf, len, "N/A");
}

/**
 * Format kilobytes into a human-readable string (KB, MB, GB).
 */
static void format_bytes(unsigned long kb, char *buf, size_t len) {
    if (kb >= 1048576)  /* >= 1 GB */
        snprintf(buf, len, "%.1f GB", (double)kb / 1048576.0);
    else if (kb >= 1024)  /* >= 1 MB */
        snprintf(buf, len, "%.1f MB", (double)kb / 1024.0);
    else
        snprintf(buf, len, "%lu KB", kb);
}

/* ════════════════════════════════════════════════════════════════════════
 * UI DRAWING HELPERS
 * ════════════════════════════════════════════════════════════════════════ */

/**
 * Draw a labeled info row.
 * Returns the Y position for the next row.
 */
static int draw_info_row(Framebuffer *fb, int y, const char *label,
                         const char *value, uint32_t value_color) {
    fb_draw_text(fb, SCREEN_SAFE_LEFT + 20, y, label, COLOR_LABEL, 2);
    fb_draw_text(fb, SCREEN_SAFE_LEFT + 280, y, value, value_color, 2);
    return y + 28;
}

/**
 * Draw a usage bar with label and percentage.
 */
static void draw_usage_bar(Framebuffer *fb, int x, int y, int width,
                           int height, unsigned long used,
                           unsigned long total, const char *label) {
    /* Label above bar */
    fb_draw_text(fb, x, y - 18, label, RGB(200, 200, 200), 2);

    /* Background bar */
    fb_fill_rect(fb, x, y, width, height, COLOR_BAR_BG);

    /* Filled portion */
    int fill = (total > 0) ? (int)((unsigned long long)used * width / total) : 0;
    if (fill > width) fill = width;

    uint32_t color;
    if (width > 0) {
        int pct_fill = fill * 100 / width;
        if (pct_fill > 90)
            color = COLOR_RED;
        else if (pct_fill > 70)
            color = COLOR_YELLOW;
        else
            color = COLOR_GREEN;
    } else {
        color = COLOR_GREEN;
    }
    if (fill > 0)
        fb_fill_rect(fb, x, y, fill, height, color);

    /* Percentage text centered on bar */
    int percent = (total > 0) ? (int)((unsigned long long)used * 100 / total) : 0;
    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    text_draw_centered(fb, x + width / 2, y + height / 2, pct, COLOR_WHITE, 1);
}

/**
 * Draw page header: background, title, and exit zone indicator.
 */
static void draw_page_header(Framebuffer *fb, int page) {
    fb_clear(fb, COLOR_BG);

    /* Header background */
    fb_fill_rect(fb, 0, 0, SCREEN_W, 50, RGB(30, 30, 50));

    /* Title */
    text_draw_centered(fb, SCREEN_W / 2, 25, page_titles[page], COLOR_HEADER, 3);

    /* Exit zone indicator (top-right corner) */
    fb_fill_rect(fb, 720, 2, 76, 36, RGB(80, 20, 20));
    text_draw_centered(fb, 758, 20, "EXIT", RGB(200, 100, 100), 2);
}

/**
 * Draw page indicator at the bottom.
 */
static void draw_page_indicator(Framebuffer *fb, int page) {
    char ind[64];
    snprintf(ind, sizeof(ind), "PAGE %d/%d - TAP TO CONTINUE", page + 1, NUM_PAGES);
    text_draw_centered(fb, SCREEN_W / 2, SCREEN_H - 20, ind, COLOR_PAGE_IND, 1);

    /* Divider above indicator */
    fb_draw_line(fb, SCREEN_SAFE_LEFT + 20, SCREEN_H - 38,
                 SCREEN_SAFE_RIGHT - 20, SCREEN_H - 38, COLOR_SECTION_LINE);
}

/* ════════════════════════════════════════════════════════════════════════
 * PAGE DRAWING FUNCTIONS
 * ════════════════════════════════════════════════════════════════════════ */

/**
 * Page 1: System — kernel version, uptime, CPU model, clock, load average.
 */
static void draw_page_system(Framebuffer *fb) {
    draw_page_header(fb, PAGE_SYSTEM);

    int y = SCREEN_SAFE_TOP + 70;

    /* Kernel version */
    char kernel[256];
    if (read_file_line("/proc/version", kernel, sizeof(kernel)) < 0)
        snprintf(kernel, sizeof(kernel), "N/A");
    /* Truncate to first three words (e.g. "Linux version X.Y.Z") */
    {
        char short_ver[128];
        int spaces = 0;
        size_t i;
        for (i = 0; i < sizeof(short_ver) - 1 && kernel[i]; i++) {
            short_ver[i] = kernel[i];
            if (kernel[i] == ' ') {
                spaces++;
                if (spaces >= 3) { short_ver[i] = '\0'; break; }
            }
        }
        short_ver[i] = '\0';
        y = draw_info_row(fb, y, "KERNEL:", short_ver, COLOR_DATA);
    }

    y += 4;

    /* Uptime */
    {
        char raw[64];
        if (read_file_line("/proc/uptime", raw, sizeof(raw)) == 0) {
            double secs = 0;
            sscanf(raw, "%lf", &secs);
            int total = (int)secs;
            int days  = total / 86400;
            int hours = (total % 86400) / 3600;
            int mins  = (total % 3600) / 60;
            int s     = total % 60;
            char fmt[64];
            snprintf(fmt, sizeof(fmt), "%dd %dh %dm %ds", days, hours, mins, s);
            y = draw_info_row(fb, y, "UPTIME:", fmt, COLOR_DATA);
        } else {
            y = draw_info_row(fb, y, "UPTIME:", "N/A", COLOR_DATA);
        }
    }

    y += 4;

    /* CPU model and clock */
    {
        char model[128], clock[128];
        read_cpuinfo(model, sizeof(model), clock, sizeof(clock));
        y = draw_info_row(fb, y, "CPU:", model, COLOR_DATA);
        y += 4;
        y = draw_info_row(fb, y, "CLOCK:", clock, COLOR_DATA);
    }

    y += 4;

    /* Load average */
    {
        char loadavg[128];
        read_loadavg(loadavg, sizeof(loadavg));
        y = draw_info_row(fb, y, "LOAD AVG:", loadavg, COLOR_DATA);
    }

    /* Section divider */
    y += 12;
    fb_draw_line(fb, SCREEN_SAFE_LEFT + 20, y,
                 SCREEN_SAFE_RIGHT - 20, y, COLOR_SECTION_LINE);

    /* Additional info: hostname */
    y += 12;
    {
        char hostname[128];
        if (read_file_line("/etc/hostname", hostname, sizeof(hostname)) < 0)
            snprintf(hostname, sizeof(hostname), "N/A");
        y = draw_info_row(fb, y, "HOSTNAME:", hostname, COLOR_DATA);
    }

    draw_page_indicator(fb, PAGE_SYSTEM);
    fb_swap(fb);
}

/**
 * Page 2: Memory — RAM and swap with usage bars.
 */
static void draw_page_memory(Framebuffer *fb) {
    draw_page_header(fb, PAGE_MEMORY);

    MemInfo mi;
    read_meminfo(&mi);

    unsigned long used_kb = mi.total_kb - mi.free_kb - mi.buffers_kb - mi.cached_kb;
    if (used_kb > mi.total_kb) used_kb = 0; /* underflow guard */

    int y = SCREEN_SAFE_TOP + 70;

    /* RAM info rows */
    {
        char buf[64];
        format_bytes(mi.total_kb, buf, sizeof(buf));
        y = draw_info_row(fb, y, "RAM TOTAL:", buf, COLOR_DATA);

        format_bytes(used_kb, buf, sizeof(buf));
        y = draw_info_row(fb, y, "RAM USED:", buf, COLOR_YELLOW);

        format_bytes(mi.free_kb, buf, sizeof(buf));
        y = draw_info_row(fb, y, "RAM FREE:", buf, COLOR_GREEN);

        format_bytes(mi.available_kb, buf, sizeof(buf));
        y = draw_info_row(fb, y, "AVAILABLE:", buf, COLOR_GREEN);

        format_bytes(mi.buffers_kb + mi.cached_kb, buf, sizeof(buf));
        y = draw_info_row(fb, y, "BUF/CACHE:", buf, COLOR_LABEL);
    }

    /* RAM usage bar */
    y += 8;
    draw_usage_bar(fb, SCREEN_SAFE_LEFT + 40, y + 18, SCREEN_SAFE_WIDTH - 80,
                   22, used_kb, mi.total_kb, "RAM USAGE:");
    y += 58;

    /* Divider */
    fb_draw_line(fb, SCREEN_SAFE_LEFT + 20, y,
                 SCREEN_SAFE_RIGHT - 20, y, COLOR_SECTION_LINE);
    y += 12;

    /* Swap info */
    {
        char buf[64];
        format_bytes(mi.swap_total_kb, buf, sizeof(buf));
        y = draw_info_row(fb, y, "SWAP TOTAL:", buf, COLOR_DATA);

        unsigned long swap_used = mi.swap_total_kb - mi.swap_free_kb;
        format_bytes(swap_used, buf, sizeof(buf));
        y = draw_info_row(fb, y, "SWAP USED:", buf, COLOR_YELLOW);

        format_bytes(mi.swap_free_kb, buf, sizeof(buf));
        y = draw_info_row(fb, y, "SWAP FREE:", buf, COLOR_GREEN);
    }

    /* Swap usage bar */
    y += 8;
    {
        unsigned long swap_used = mi.swap_total_kb - mi.swap_free_kb;
        draw_usage_bar(fb, SCREEN_SAFE_LEFT + 40, y + 18,
                       SCREEN_SAFE_WIDTH - 80, 22,
                       swap_used, mi.swap_total_kb, "SWAP USAGE:");
    }

    draw_page_indicator(fb, PAGE_MEMORY);
    fb_swap(fb);
}

/**
 * Page 3: Storage — disk space per mounted partition.
 */
static void draw_page_storage(Framebuffer *fb) {
    draw_page_header(fb, PAGE_STORAGE);

    int y = SCREEN_SAFE_TOP + 70;

    for (int i = 0; i < NUM_MOUNT_POINTS; i++) {
        DiskInfo di;
        read_disk_usage(mount_points[i], &di);

        /* Mount point label */
        fb_draw_text(fb, SCREEN_SAFE_LEFT + 20, y, mount_points[i], COLOR_HEADER, 2);
        y += 22;

        if (di.valid) {
            char total_str[32], used_str[32], free_str[32];
            format_bytes(di.total_kb, total_str, sizeof(total_str));
            format_bytes(di.used_kb, used_str, sizeof(used_str));
            format_bytes(di.free_kb, free_str, sizeof(free_str));

            char detail[128];
            snprintf(detail, sizeof(detail), "%s USED / %s TOTAL  (%s FREE)",
                     used_str, total_str, free_str);
            fb_draw_text(fb, SCREEN_SAFE_LEFT + 40, y, detail, COLOR_LABEL, 1);
            y += 14;

            /* Usage bar */
            draw_usage_bar(fb, SCREEN_SAFE_LEFT + 40, y + 4,
                           SCREEN_SAFE_WIDTH - 80, 18,
                           di.used_kb, di.total_kb, "");
            y += 30;
        } else {
            fb_draw_text(fb, SCREEN_SAFE_LEFT + 40, y, "N/A (NOT MOUNTED)",
                         RGB(200, 80, 80), 2);
            y += 28;
        }

        /* Divider between partitions */
        if (i < NUM_MOUNT_POINTS - 1) {
            y += 4;
            fb_draw_line(fb, SCREEN_SAFE_LEFT + 20, y,
                         SCREEN_SAFE_RIGHT - 20, y, COLOR_SECTION_LINE);
            y += 8;
        }
    }

    draw_page_indicator(fb, PAGE_STORAGE);
    fb_swap(fb);
}

/**
 * Page 4: Hardware — LEDs, backlight, framebuffer info.
 */
static void draw_page_hardware(Framebuffer *fb) {
    draw_page_header(fb, PAGE_HARDWARE);

    int y = SCREEN_SAFE_TOP + 70;

    /* Section: LED Brightness */
    fb_draw_text(fb, SCREEN_SAFE_LEFT + 20, y, "LED BRIGHTNESS", COLOR_HEADER, 2);
    y += 24;
    fb_draw_line(fb, SCREEN_SAFE_LEFT + 20, y,
                 SCREEN_SAFE_RIGHT - 20, y, COLOR_SECTION_LINE);
    y += 8;

    {
        int red_val = read_sysfs_int("/sys/class/leds/red_led/brightness");
        int green_val = read_sysfs_int("/sys/class/leds/green_led/brightness");
        char buf[32];

        if (red_val >= 0)
            snprintf(buf, sizeof(buf), "%d", red_val);
        else
            snprintf(buf, sizeof(buf), "N/A");
        y = draw_info_row(fb, y, "RED LED:", buf,
                          (red_val > 0) ? COLOR_RED : COLOR_LABEL);

        if (green_val >= 0)
            snprintf(buf, sizeof(buf), "%d", green_val);
        else
            snprintf(buf, sizeof(buf), "N/A");
        y = draw_info_row(fb, y, "GREEN LED:", buf,
                          (green_val > 0) ? COLOR_GREEN : COLOR_LABEL);
    }

    y += 4;

    /* Backlight */
    {
        int bl_val = read_sysfs_int("/sys/class/leds/backlight/brightness");
        char buf[32];
        if (bl_val >= 0)
            snprintf(buf, sizeof(buf), "%d", bl_val);
        else
            snprintf(buf, sizeof(buf), "N/A");
        y = draw_info_row(fb, y, "BACKLIGHT:", buf, COLOR_DATA);
    }

    y += 8;

    /* Section: Framebuffer Info */
    fb_draw_text(fb, SCREEN_SAFE_LEFT + 20, y, "FRAMEBUFFER", COLOR_HEADER, 2);
    y += 24;
    fb_draw_line(fb, SCREEN_SAFE_LEFT + 20, y,
                 SCREEN_SAFE_RIGHT - 20, y, COLOR_SECTION_LINE);
    y += 8;

    {
        char buf[64];

        snprintf(buf, sizeof(buf), "%ux%u", fb->width, fb->height);
        y = draw_info_row(fb, y, "RESOLUTION:", buf, COLOR_DATA);

        snprintf(buf, sizeof(buf), "%u", fb->bytes_per_pixel * 8);
        y = draw_info_row(fb, y, "BPP:", buf, COLOR_DATA);

        snprintf(buf, sizeof(buf), "%u bytes", fb->line_length);
        y = draw_info_row(fb, y, "STRIDE:", buf, COLOR_DATA);

        snprintf(buf, sizeof(buf), "%zu bytes", fb->screen_size);
        y = draw_info_row(fb, y, "FB SIZE:", buf, COLOR_DATA);

        snprintf(buf, sizeof(buf), "%s", fb->double_buffering ? "YES" : "NO");
        y = draw_info_row(fb, y, "DOUBLE BUF:", buf,
                          fb->double_buffering ? COLOR_GREEN : COLOR_YELLOW);
    }

    draw_page_indicator(fb, PAGE_HARDWARE);
    fb_swap(fb);
}

/**
 * Page 5: Config — current settings from rw_config.conf and system config.
 */
static void draw_page_config(Framebuffer *fb) {
    draw_page_header(fb, PAGE_CONFIG);

    int y = SCREEN_SAFE_TOP + 70;

    /* Section: Config file settings */
    fb_draw_text(fb, SCREEN_SAFE_LEFT + 20, y, "CONFIG FILE", COLOR_HEADER, 2);
    y += 22;
    fb_draw_text(fb, SCREEN_SAFE_LEFT + 20, y, CONFIG_FILE_PATH, COLOR_LABEL, 1);
    y += 16;
    fb_draw_line(fb, SCREEN_SAFE_LEFT + 20, y,
                 SCREEN_SAFE_RIGHT - 20, y, COLOR_SECTION_LINE);
    y += 10;

    /* Load config */
    Config cfg;
    config_init(&cfg);
    if (config_load(&cfg) < 0) {
        fb_draw_text(fb, SCREEN_SAFE_LEFT + 40, y,
                     "CONFIG FILE NOT FOUND", RGB(200, 80, 80), 2);
        y += 24;
        fb_draw_text(fb, SCREEN_SAFE_LEFT + 40, y,
                     "(USING DEFAULTS)", COLOR_LABEL, 2);
        y += 28;
    } else {
        /* Display known config keys with their values */
        static const struct { const char *key; const char *label; const char *defval; } known_keys[] = {
            { "audio_enabled",         "AUDIO ENABLED:",     "1 (default)" },
            { "led_enabled",           "LED ENABLED:",       "1 (default)" },
            { "led_brightness",        "LED BRIGHTNESS:",    "100 (default)" },
            { "backlight_brightness",  "BACKLIGHT:",         "100 (default)" },
        };
        int nkeys = sizeof(known_keys) / sizeof(known_keys[0]);

        for (int i = 0; i < nkeys; i++) {
            const char *val = config_get(&cfg, known_keys[i].key, NULL);
            if (val) {
                y = draw_info_row(fb, y, known_keys[i].label, val, COLOR_DATA);
            } else {
                y = draw_info_row(fb, y, known_keys[i].label, known_keys[i].defval, COLOR_LABEL);
            }
        }

        /* Show any extra/unknown keys in the file */
        bool has_extra = false;
        for (int i = 0; i < cfg.count; i++) {
            bool is_known = false;
            for (int k = 0; k < nkeys; k++) {
                if (strcmp(cfg.entries[i].key, known_keys[k].key) == 0) {
                    is_known = true;
                    break;
                }
            }
            if (!is_known) {
                if (!has_extra) {
                    y += 4;
                    fb_draw_text(fb, SCREEN_SAFE_LEFT + 20, y, "OTHER SETTINGS:", COLOR_HEADER, 1);
                    y += 14;
                    has_extra = true;
                }
                if (y < SCREEN_H - 60) {
                    char line[196];
                    snprintf(line, sizeof(line), "%s = %s",
                             cfg.entries[i].key, cfg.entries[i].value);
                    fb_draw_text(fb, SCREEN_SAFE_LEFT + 30, y, line, COLOR_LABEL, 1);
                    y += 14;
                }
            }
        }
    }

    /* Section divider */
    y += 8;
    fb_draw_line(fb, SCREEN_SAFE_LEFT + 20, y,
                 SCREEN_SAFE_RIGHT - 20, y, COLOR_SECTION_LINE);
    y += 10;

    /* Section: System configuration */
    fb_draw_text(fb, SCREEN_SAFE_LEFT + 20, y, "SYSTEM", COLOR_HEADER, 2);
    y += 28;

    /* Default app (auto-launch) */
    {
        char default_app[256];
        if (read_file_line("/opt/roomwizard/default-app", default_app, sizeof(default_app)) == 0) {
            y = draw_info_row(fb, y, "DEFAULT APP:", default_app, COLOR_DATA);
        } else {
            y = draw_info_row(fb, y, "DEFAULT APP:", "(NOT SET)", COLOR_LABEL);
        }
    }

    /* Touch calibration */
    {
        char calib_status[32];
        if (access("/etc/touch_calibration.conf", 0) == 0) {
            snprintf(calib_status, sizeof(calib_status), "YES");
            y = draw_info_row(fb, y, "CALIBRATED:", calib_status, COLOR_GREEN);
        } else {
            snprintf(calib_status, sizeof(calib_status), "NO");
            y = draw_info_row(fb, y, "CALIBRATED:", calib_status, COLOR_YELLOW);
        }
    }

    draw_page_indicator(fb, PAGE_CONFIG);
    fb_swap(fb);
}

/* ════════════════════════════════════════════════════════════════════════
 * MAIN
 * ════════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* ── Singleton guard ────────────────────────────────────────────── */
    int lock_fd = acquire_instance_lock("hardware_diag");
    if (lock_fd < 0) return 1;

    /* ── Signal handlers ────────────────────────────────────────────── */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* ── Hardware init + configured backlight ───────────────────────── */
    hw_init();
    hw_set_backlight(100);  /* Scales to configured percentage */

    /* ── Framebuffer init ───────────────────────────────────────────── */
    Framebuffer fb;
    if (fb_init(&fb, "/dev/fb0") != 0) {
        fprintf(stderr, "hardware_diag: failed to init framebuffer\n");
        return 1;
    }

    /* ── Touch init ─────────────────────────────────────────────────── */
    TouchInput touch;
    if (touch_init(&touch, "/dev/input/event0") != 0) {
        fprintf(stderr, "hardware_diag: failed to init touch input\n");
        fb_close(&fb);
        return 1;
    }

    /* ── Main loop — page-based navigation ──────────────────────────── */
    int current_page = 0;
    bool need_redraw = true;
    bool was_touching = false;

    while (running) {
        /* Draw current page if needed */
        if (need_redraw) {
            switch (current_page) {
                case PAGE_SYSTEM:   draw_page_system(&fb);   break;
                case PAGE_MEMORY:   draw_page_memory(&fb);   break;
                case PAGE_STORAGE:  draw_page_storage(&fb);  break;
                case PAGE_HARDWARE: draw_page_hardware(&fb); break;
                case PAGE_CONFIG:   draw_page_config(&fb);   break;
            }
            need_redraw = false;
        }

        /* Poll touch */
        touch_poll(&touch);
        TouchState ts = touch_get_state(&touch);

        if (ts.pressed && !was_touching) {
            /* Check exit zone: top-right corner (x > 700, y < 40) */
            if (ts.x > 700 && ts.y < 40) {
                running = false;
            } else {
                /* Advance page */
                current_page++;
                if (current_page >= NUM_PAGES)
                    current_page = 0;
                need_redraw = true;
            }
        }

        was_touching = ts.pressed || ts.held;

        /* Adaptive frame delay — 30fps when rendering, 10fps when idle */
        usleep(need_redraw ? FRAME_DELAY_ACTIVE_US : FRAME_DELAY_IDLE_US);
    }

    /* ── Cleanup (reverse order) ────────────────────────────────────── */
    hw_leds_off();
    hw_set_backlight(100);  /* Restore configured brightness on exit */
    touch_close(&touch);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    fb_close(&fb);

    return 0;
}
