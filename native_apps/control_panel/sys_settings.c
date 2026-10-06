/* sys_settings.c — see sys_settings.h. */
#include "sys_settings.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* One line's keyword and value, in place: kw / val point into line,
 * NUL-terminated.  false for a blank or comment line. */
static bool split_line(char *line, char **kw, char **val) {
    char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p || *p == '#') return false;
    *kw = p;
    while (*p && !isspace((unsigned char)*p) && *p != '=') p++;
    if (*p) *p++ = '\0';
    while (*p && (isspace((unsigned char)*p) || *p == '=')) p++;
    if (*p == '"') p++;
    *val = p;
    char *e = p + strlen(p);
    while (e > p && (isspace((unsigned char)e[-1]) || e[-1] == '"')) e--;
    *e = '\0';
    return true;
}

SshMode sys_ssh_mode(const char *text) {
    if (!text) return SSH_MODE_UNKNOWN;
    int pw = -1;        /* -1 absent, 0 no, 1 yes, 2 other */
    int root = -1;      /* -1 absent, 1 yes, 2 other */
    const char *p = text;
    while (*p) {
        char line[256];
        size_t n = strcspn(p, "\n");
        size_t c = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
        memcpy(line, p, c);
        line[c] = '\0';
        p += n;
        if (*p == '\n') p++;

        char *kw, *val;
        if (!split_line(line, &kw, &val)) continue;
        if (strcasecmp(kw, "Match") == 0) break;
        if (strcasecmp(kw, "PasswordAuthentication") == 0 && pw == -1)
            pw = strcasecmp(val, "no") == 0 ? 0 : strcasecmp(val, "yes") == 0 ? 1 : 2;
        else if (strcasecmp(kw, "PermitRootLogin") == 0 && root == -1)
            root = strcasecmp(val, "yes") == 0 ? 1 : 2;
    }
    if (pw == 0) return SSH_MODE_KEY_ONLY;
    if (pw == 1 && root == 1) return SSH_MODE_PASSWORD;
    return SSH_MODE_UNKNOWN;
}

int sys_days_in_month(int y, int m) {
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12) return 0;
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}

static int wrap(int v, int lo, int hi) {
    return v > hi ? lo : v < lo ? hi : v;
}

void sys_dt_step(SysDateTime *dt, int field, int dir) {
    dir = dir < 0 ? -1 : 1;
    switch (field) {
    case SYS_DT_YEAR:
        dt->year += dir;
        if (dt->year < SYS_YEAR_MIN) dt->year = SYS_YEAR_MIN;
        if (dt->year > SYS_YEAR_MAX) dt->year = SYS_YEAR_MAX;
        break;
    case SYS_DT_MON:  dt->mon  = wrap(dt->mon + dir, 1, 12); break;
    case SYS_DT_DAY:  dt->day  = wrap(dt->day + dir, 1, sys_days_in_month(dt->year, dt->mon)); break;
    case SYS_DT_HOUR: dt->hour = wrap(dt->hour + dir, 0, 23); break;
    case SYS_DT_MIN:  dt->min  = wrap(dt->min + dir, 0, 59); break;
    default: break;
    }
    int dim = sys_days_in_month(dt->year, dt->mon);
    if (dim && dt->day > dim) dt->day = dim;
}

bool sys_dt_valid(const SysDateTime *dt) {
    return dt->year >= SYS_YEAR_MIN && dt->year <= SYS_YEAR_MAX &&
           dt->mon >= 1 && dt->mon <= 12 &&
           dt->day >= 1 && dt->day <= sys_days_in_month(dt->year, dt->mon) &&
           dt->hour >= 0 && dt->hour <= 23 && dt->min >= 0 && dt->min <= 59;
}

void sys_dt_format(const SysDateTime *dt, char *out, size_t len) {
    if (!len) return;
    if (!sys_dt_valid(dt)) { out[0] = '\0'; return; }
    snprintf(out, len, "%04d-%02d-%02d %02d:%02d:00",
             dt->year, dt->mon, dt->day, dt->hour, dt->min);
}
