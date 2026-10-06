/* soc_temp.c — see soc_temp.h. */
#include "soc_temp.h"

#include <ctype.h>
#include <stdio.h>

int soc_temp_parse(const char *text, int *mc) {
    if (!text) return -1;
    const char *p = text;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (!isdigit((unsigned char)*p)) return -1;
    /* int, not long: the same 32 bits on the host test and on the device, so
     * the overflow guard below is exercised by the host test too. */
    int v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        if (v > 1000000) return -1;   /* stop long before int overflows */
        p++;
    }
    while (*p) {
        if (!isspace((unsigned char)*p)) return -1;
        p++;
    }
    if (neg) v = -v;
    if (v < SOC_TEMP_MIN_MC || v > SOC_TEMP_MAX_MC) return -1;
    *mc = v;
    return 0;
}

void soc_temp_format(int mc, char *buf, size_t len) {
    int deg = mc >= 0 ? (mc + 500) / 1000 : -((-mc + 500) / 1000);
    snprintf(buf, len, "%d C", deg);
}
