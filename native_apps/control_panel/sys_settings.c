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

/* ── Rewriting sshd_config, and the lockout guard ───────────────────────── */

typedef struct { char *p; size_t cap, len; bool over; } OutBuf;

static void ob_put(OutBuf *o, const char *s, size_t n) {
    if (o->len + n + 1 > o->cap) { o->over = true; return; }
    memcpy(o->p + o->len, s, n);
    o->len += n;
}

static void ob_line(OutBuf *o, const char *s, size_t n) {
    while (n && (s[n - 1] == '\r')) n--;          /* LF only */
    ob_put(o, s, n);
    ob_put(o, "\n", 1);
}

/* Which directive an input line is, using split_line() on a copy:
 * 1 PasswordAuthentication, 2 Match, 0 anything else (incl. comments). */
static int line_kind(const char *s, size_t n) {
    char line[256];
    size_t c = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
    memcpy(line, s, c);
    line[c] = '\0';
    char *kw, *val;
    if (!split_line(line, &kw, &val)) return 0;
    if (strcasecmp(kw, "PasswordAuthentication") == 0) return 1;
    if (strcasecmp(kw, "Match") == 0) return 2;
    return 0;
}

int sys_ssh_set_password_auth(const char *text, bool password_yes,
                              char *out, size_t out_len) {
    if (!text || !out || out_len < 2) return -1;
    OutBuf o = { out, out_len, 0, false };
    const char *want = password_yes ? "PasswordAuthentication yes"
                                    : "PasswordAuthentication no";
    bool seen = false, in_match = false;
    const char *p = text;
    while (*p) {
        size_t n = strcspn(p, "\n");
        int k = in_match ? 0 : line_kind(p, n);
        if (k == 2) {
            if (!seen) { ob_line(&o, want, strlen(want)); seen = true; }
            in_match = true;
        }
        if (k == 1) { ob_line(&o, want, strlen(want)); seen = true; }
        else        ob_line(&o, p, n);
        p += n;
        if (*p == '\n') p++;
    }
    if (!seen) ob_line(&o, want, strlen(want));
    if (o.over) return -1;
    out[o.len] = '\0';
    return (int)o.len;
}

static bool is_b64(char c) {
    return isalnum((unsigned char)c) || c == '+' || c == '/' || c == '=';
}

static bool key_type(const char *t, size_t n) {
    return (n > 4 && strncmp(t, "ssh-", 4) == 0) ||
           (n > 11 && strncmp(t, "ecdsa-sha2-", 11) == 0) ||
           (n > 3 && strncmp(t, "sk-", 3) == 0);
}

bool sys_authkeys_plausible(const char *text) {
    if (!text) return false;
    const char *p = text;
    while (*p) {
        size_t n = strcspn(p, "\n");
        const char *e = p + n;
        const char *s = p;
        p = *e == '\n' ? e + 1 : e;
        while (s < e && isspace((unsigned char)*s)) s++;
        if (s == e || *s == '#') continue;
        /* tokens: [options] type blob [comment] */
        while (s < e) {
            const char *t = s;
            while (s < e && !isspace((unsigned char)*s)) s++;
            size_t tn = (size_t)(s - t);
            while (s < e && isspace((unsigned char)*s)) s++;
            if (!key_type(t, tn)) continue;
            const char *b = s;
            while (s < e && is_b64(*s)) s++;
            if ((size_t)(s - b) >= 20 && (s == e || isspace((unsigned char)*s))) return true;
            break;
        }
    }
    return false;
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

/* ── Timezone ───────────────────────────────────────────────────────────── */

static const char *const tz_zones[] = {
    "UTC", "America/New_York", "America/Chicago", "America/Denver",
    "America/Los_Angeles", "Europe/London", "Europe/Berlin",
    "Europe/Budapest", "Asia/Tokyo", "Australia/Sydney",
};
#define TZ_N ((int)(sizeof(tz_zones) / sizeof(tz_zones[0])))

int sys_tz_count(void) { return TZ_N; }

const char *sys_tz_name(int idx) {
    return idx >= 0 && idx < TZ_N ? tz_zones[idx] : NULL;
}

int sys_tz_from_link(const char *target) {
    if (!target) return -1;
    const char *p = strstr(target, "zoneinfo/");
    if (!p) return -1;
    p += strlen("zoneinfo/");
    for (int i = 0; i < TZ_N; i++)
        if (strcmp(p, tz_zones[i]) == 0) return i;
    return -1;
}

int sys_tz_step(int idx, int dir) {
    if (idx < 0 || idx >= TZ_N) return dir < 0 ? TZ_N - 1 : 0;
    return (idx + (dir < 0 ? TZ_N - 1 : 1)) % TZ_N;
}
