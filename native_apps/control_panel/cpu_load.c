/* cpu_load.c — see cpu_load.h. */
#include "cpu_load.h"

#include <stdio.h>
#include <string.h>

int cpu_parse_stat(const char *text, unsigned long long *busy) {
    unsigned long long f[8] = {0};
    if (!text || strncmp(text, "cpu ", 4) != 0) return -1;
    /* user nice system idle iowait irq softirq steal; older kernels stop
     * earlier, so the first four are the minimum. */
    int n = sscanf(text + 4, "%llu %llu %llu %llu %llu %llu %llu %llu",
                   &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7]);
    if (n < 4) return -1;
    *busy = f[0] + f[1] + f[2] + f[5] + f[6] + f[7];
    return 0;
}

int cpu_parse_self(const char *text, unsigned long long *ticks) {
    const char *p = text ? strrchr(text, ')') : NULL;
    unsigned long long ut, st;
    if (!p) return -1;
    /* After ")": field 3 (state) onwards; utime and stime are 14 and 15. */
    if (sscanf(p + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu",
               &ut, &st) != 2)
        return -1;
    *ticks = ut + st;
    return 0;
}

int cpu_parse_uptime(const char *text, unsigned long long *cs) {
    unsigned long long whole = 0;
    const char *p = text;
    int digits = 0;
    if (!p) return -1;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { whole = whole * 10 + (unsigned)(*p++ - '0'); digits++; }
    if (!digits) return -1;
    unsigned frac = 0;
    if (*p == '.') {
        p++;
        if (*p >= '0' && *p <= '9') { frac = (unsigned)(*p++ - '0') * 10; }
        if (*p >= '0' && *p <= '9') { frac += (unsigned)(*p - '0'); }
    }
    *cs = whole * 100 + frac;
    return 0;
}

static int permille(unsigned long long ticks, unsigned long long wall_cs,
                    unsigned clk_tck) {
    /* ticks / clk_tck seconds over wall_cs / 100 seconds, x 1000. */
    unsigned long long pm = ticks * 100000ULL / (wall_cs * clk_tck);
    return pm > 1000 ? 1000 : (int)pm;
}

int cpu_share(const CpuSnap *prev, const CpuSnap *cur, unsigned clk_tck,
              int *others_pm, int *self_pm) {
    if (!prev || !cur || !prev->valid || !cur->valid || clk_tck == 0) return -1;
    if (cur->wall_cs <= prev->wall_cs || cur->busy < prev->busy
        || cur->self < prev->self)
        return -1;
    unsigned long long wall = cur->wall_cs - prev->wall_cs;
    unsigned long long busy = cur->busy - prev->busy;
    unsigned long long self = cur->self - prev->self;
    /* The two counters are read microseconds apart and tick independently, so
     * the panel can show one tick more than the system; others floors at 0. */
    unsigned long long others = busy > self ? busy - self : 0;
    *others_pm = permille(others, wall, clk_tck);
    *self_pm   = permille(self, wall, clk_tck);
    return 0;
}

void cpu_hist_clear(CpuHistory *h) {
    memset(h, 0, sizeof(*h));
}

void cpu_hist_push(CpuHistory *h, int pm) {
    if (pm < 0) pm = 0;
    if (pm > 1000) pm = 1000;
    h->v[h->head] = (short)pm;
    h->head = (h->head + 1) % CPU_HIST_N;
    if (h->count < CPU_HIST_N) h->count++;
}

int cpu_hist_get(const CpuHistory *h, int i) {
    if (i < 0 || i >= h->count) return -1;
    int start = (h->head - h->count + CPU_HIST_N) % CPU_HIST_N;
    return h->v[(start + i) % CPU_HIST_N];
}
