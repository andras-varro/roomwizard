/* mon_graph.c — see mon_graph.h. */
#include "mon_graph.h"

int mon_scale_pm(long long v, long long lo, long long hi) {
    if (hi <= lo || v <= lo) return 0;
    if (v >= hi) return 1000;
    return (int)((v - lo) * 1000 / (hi - lo));
}

int mon_bar_h(int pm, int ih) {
    if (ih <= 0 || pm <= 0) return 0;
    if (pm >= 1000) return ih;
    int h = pm * ih / 1000;
    return h < 1 ? 1 : h;
}
