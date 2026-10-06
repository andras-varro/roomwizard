/* mon_graph.h — the Monitor page's history-graph arithmetic.
 *
 * Pure: integers in, integers out, so a host test drives it directly.
 * monitor_page.c owns the one graph widget that draws CPU, memory and SoC
 * temperature; every series reaches it as permille of its own axis, so the
 * widget needs no per-series code and one CpuHistory ring holds each series.
 */
#ifndef MON_GRAPH_H
#define MON_GRAPH_H

/* The temperature graph's fixed axis, millidegrees C.  Fixed rather than
 * auto-ranged so a bar's height means the same temperature on every visit. */
#define MON_TEMP_AXIS_LO_MC  40000
#define MON_TEMP_AXIS_HI_MC 100000

/* v on the axis lo..hi as permille, clamped to 0..1000.  An empty or inverted
 * axis (hi <= lo) gives 0, so a missing MemTotal draws nothing rather than
 * dividing by zero. */
int mon_scale_pm(long long v, long long lo, long long hi);

/* A column's height in px for permille pm in an inner height of ih px:
 * pm * ih / 1000, at least 1 for any pm > 0 so a non-zero sample stays
 * visible, never more than ih, and 0 when ih <= 0. */
int mon_bar_h(int pm, int ih);

#endif
