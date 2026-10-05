/* cpu_load.h — system CPU busy share and its history, for the Monitor page.
 *
 * Pure: every function takes the text of a /proc file, never a path, so a host
 * test drives it with fixture strings.  monitor_page.c does the reading.
 *
 * ⚠️ The denominator is WALL time, never /proc/stat's grand total.  This
 * kernel runs NO_HZ_IDLE with tick accounting, so idle ticks are not sampled
 * while the CPU sleeps and the grand total comes up short — the quieter the
 * system, the shorter — which makes busy/(busy+idle) inflate every reading.
 * Busy ticks over the /proc/uptime delta is the honest share
 * (SYSTEM_ANALYSIS.md#34-audio, the PIO-cost paragraph).
 *
 * The control panel's own ticks (/proc/self/stat utime+stime) are taken out of
 * the system figure and reported beside it, so the graph shows what the rest
 * of the device costs and the panel's repaint cost is labelled, not hidden.
 */
#ifndef CPU_LOAD_H
#define CPU_LOAD_H

/* One reading: cumulative busy ticks system-wide, the panel's own cumulative
 * ticks, and /proc/uptime in centiseconds.  valid = all three parsed. */
typedef struct {
    unsigned long long busy, self, wall_cs;
    int valid;
} CpuSnap;

/* The "cpu " line of /proc/stat: busy = user + nice + system + irq + softirq
 * + steal (idle and iowait are not busy).  0, or -1 on anything else. */
int cpu_parse_stat(const char *text, unsigned long long *busy);

/* /proc/self/stat: utime + stime (fields 14 and 15), counted after the last
 * ')' because the comm field may itself hold spaces or parentheses. */
int cpu_parse_self(const char *text, unsigned long long *ticks);

/* /proc/uptime's first field in centiseconds, without floating point. */
int cpu_parse_uptime(const char *text, unsigned long long *cs);

/* Shares over the window prev..cur, in permille of wall time, clamped to
 * 0..1000.  others = system busy minus the panel's own; self = the panel's.
 * clk_tck is the unit of the tick counters (USER_HZ, sysconf(_SC_CLK_TCK)).
 * Returns 0, or -1 when either snap is invalid, the window is empty, or a
 * counter ran backwards — no sample, rather than a wrong one. */
int cpu_share(const CpuSnap *prev, const CpuSnap *cur, unsigned clk_tck,
              int *others_pm, int *self_pm);

/* The last CPU_HIST_N samples, oldest first by index. */
#define CPU_HIST_N 120

typedef struct {
    short v[CPU_HIST_N];
    int   head;    /* where the next push lands */
    int   count;   /* 0..CPU_HIST_N */
} CpuHistory;

void cpu_hist_clear(CpuHistory *h);
void cpu_hist_push(CpuHistory *h, int permille);
/* i = 0 is the oldest held sample, count - 1 the newest; -1 out of range. */
int  cpu_hist_get(const CpuHistory *h, int i);

#endif
