/* bt_ctl.c — see bt_ctl.h.  The parser half is pure and host-tested by
 * tests/bt_ctl_test.c; the process half is POSIX only and compiles on the
 * host too, so the test links this file unmodified.
 *
 * Text format sources: bluetoothctl 5.66 output captured on the device, and
 * the 5.66 tarball we build from (client/main.c, client/agent.c,
 * src/shared/shell.[ch]).  From the source: every colour is wrapped in
 * readline's \001...\002 ignore markers, which a printf (not a prompt) emits
 * raw; agent questions are readline PROMPTS ("[agent] <question> "), so they
 * end without a newline and are redrawn after every asynchronous line;
 * "[agent] Passkey: NNNNNN" is a printed line whose COLOR_OFF follows the
 * newline.
 */
#include "bt_ctl.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* small helpers                                                        */
/* ------------------------------------------------------------------ */

static void copy_str(char *dst, size_t dstsz, const char *src)
{
    size_t n = strlen(src);
    if (n >= dstsz) n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/* "AA:BB:CC:DD:EE:FF" at p, followed by NUL or a space. */
static bool is_addr(const char *p)
{
    int i, col = 0;                     /* col counts 0,1,2 — no modulo on the no-divide core */
    for (i = 0; i < 17; i++) {
        if (col == 2) { if (p[i] != ':') return false; col = 0; continue; }
        if (!is_hex(p[i])) return false;
        col++;
    }
    return p[17] == '\0' || p[17] == ' ';
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
    *e = '\0';
    return s;
}

/* The name bluetoothctl shows for a device with none: the address, dashed. */
static bool is_dashed_addr(const char *name, const char *addr)
{
    int i;
    for (i = 0; i < 17; i++) {
        char want = addr[i] == ':' ? '-' : addr[i];
        if (name[i] != want) return false;
    }
    return name[17] == '\0';
}

/* Strip CSI sequences (ESC [ params final), other two-byte ESC sequences,
 * readline's \001/\002 markers and other control bytes; apply backspaces.
 * An escape cut off by the end of the buffer is dropped whole. */
static void clean(const char *in, size_t n, char *out, size_t outsz)
{
    size_t i = 0, o = 0;
    while (i < n) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0x1b) {
            i++;
            if (i < n && in[i] == '[') {
                i++;
                while (i < n && !((unsigned char)in[i] >= 0x40 && (unsigned char)in[i] <= 0x7e)) i++;
                if (i < n) i++;          /* the final byte */
            } else if (i < n) {
                i++;
            }
            continue;
        }
        i++;
        if (c == '\b') { if (o > 0) o--; continue; }
        if (c == '\t') { if (o + 1 < outsz) out[o++] = '\t'; continue; }
        if (c < 0x20 || c == 0x7f) continue;
        if (o + 1 < outsz) out[o++] = (char)c;
    }
    out[o] = '\0';
}

/* Strip leading shell prompts "[<no ]>]# " (the name can be a connected
 * device's).  The agent prompt "[agent] " has no '#', so it survives. */
static char *strip_prompts(char *t, bool *had)
{
    *had = false;
    for (;;) {
        char *p = t;
        while (*p == ' ') p++;
        if (*p != '[') return t;
        char *q = strchr(p + 1, ']');
        if (!q || q[1] != '#') return t;
        t = q + 2;
        if (*t == ' ') t++;
        *had = true;
    }
}

/* ------------------------------------------------------------------ */
/* state                                                                */
/* ------------------------------------------------------------------ */

void bt_state_init(BtState *s)
{
    memset(s, 0, sizeof(*s));
    s->pid = 0;
    s->to_child = -1;
    s->from_child = -1;
    s->prompt = BT_PROMPT_NONE;
}

bool bt_queue(BtState *s, const char *cmd)
{
    size_t n = strlen(cmd);
    if (s->outqlen + n + 1 > sizeof(s->outq)) return false;
    memcpy(s->outq + s->outqlen, cmd, n);
    s->outq[s->outqlen + n] = '\n';
    s->outqlen += n + 1;
    return true;
}

BtDevice *bt_find(BtState *s, const char *addr)
{
    int i;
    for (i = 0; i < s->ndev; i++)
        if (strncmp(s->dev[i].addr, addr, 17) == 0) return &s->dev[i];
    return NULL;
}

void bt_split_lists(const BtState *s, int *mine, int *nmine, int *found, int *nfound)
{
    int i, m = 0, f = 0;
    for (i = 0; i < s->ndev; i++)
        if (s->dev[i].paired && s->dev[i].connected) mine[m++] = i;
    for (i = 0; i < s->ndev; i++) {
        const BtDevice *d = &s->dev[i];
        if (d->paired && !d->connected) mine[m++] = i;
        else if (!d->paired && d->seen)  found[f++] = i;
    }
    *nmine = m;
    *nfound = f;
}

int bt_list_rows(const int *mine, int nmine, const int *found, int nfound,
                 int rows_fit, int *rows)
{
    int i, n = 0;
    int pos = 0;            /* row within its page, counted: no runtime divide */
    if (rows_fit < 1) rows_fit = 1;
    for (i = 0; i < nmine; i++) {
        rows[n++] = mine[i];
        if (++pos == rows_fit) pos = 0;
    }
    if (nmine > 0 && nfound > 0 && pos != 0) {
        /* A header with no room for a row under it would be orphaned. */
        rows[n++] = pos + 1 < rows_fit ? BT_ROW_FOUND_HDR : BT_ROW_BLANK;
    }
    for (i = 0; i < nfound; i++) rows[n++] = found[i];
    return n;
}

static void remove_device(BtState *s, const char *addr);

/* Find or add; *added says which.  A full table gives up its oldest entry
 * that is neither paired nor seen — a cache entry the page does not list —
 * so a scan in a crowded room still reaches new devices.  NULL only if every
 * entry is listed. */
static BtDevice *get_device(BtState *s, const char *addr, bool *added)
{
    BtDevice *d = bt_find(s, addr);
    *added = false;
    if (d) return d;
    if (s->ndev >= BT_MAX_DEVICES) {
        int i;
        for (i = 0; i < s->ndev; i++)
            if (!s->dev[i].paired && !s->dev[i].seen) break;
        if (i == s->ndev) return NULL;
        char victim[BT_ADDR_LEN];
        memcpy(victim, s->dev[i].addr, BT_ADDR_LEN);
        remove_device(s, victim);
    }
    d = &s->dev[s->ndev++];
    memset(d, 0, sizeof(*d));
    memcpy(d->addr, addr, 17);
    d->addr[17] = '\0';
    *added = true;
    s->changed = true;
    return d;
}

static void remove_device(BtState *s, const char *addr)
{
    int idx;
    for (idx = 0; idx < s->ndev; idx++)
        if (strncmp(s->dev[idx].addr, addr, 17) == 0) break;
    if (idx == s->ndev) return;
    memmove(&s->dev[idx], &s->dev[idx + 1], (size_t)(s->ndev - idx - 1) * sizeof(BtDevice));
    s->ndev--;
    if (strncmp(s->info_addr, addr, 17) == 0) s->info_addr[0] = '\0';
    s->changed = true;
}

static void queue_info(BtState *s, const char *addr)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "info %.17s", addr);
    bt_queue(s, cmd);
}

/* A newly listed device: add it and ask for its details. */
static BtDevice *list_device(BtState *s, const char *addr, const char *name)
{
    bool added;
    BtDevice *d = get_device(s, addr, &added);
    if (!d) return NULL;
    if (added) queue_info(s, d->addr);
    if (name && *name && d->name[0] == '\0') {
        copy_str(d->name, sizeof(d->name), name);
        s->changed = true;
    }
    return d;
}

static void set_bool(BtState *s, bool *field, const char *val)
{
    bool v = strcmp(val, "yes") == 0;
    if (*field != v) { *field = v; s->changed = true; }
}

static void set_text(BtState *s, char *field, size_t sz, const char *val)
{
    if (strncmp(field, val, sz - 1) != 0) { copy_str(field, sz, val); s->changed = true; }
}

/* One "Key: value" property of a device (from an info block or a [CHG]). */
static void apply_device_kv(BtState *s, BtDevice *d, const char *key, const char *val)
{
    if      (!strcmp(key, "Name"))      set_text(s, d->name, sizeof(d->name), val);
    else if (!strcmp(key, "Alias")) {
        if (d->name[0] == '\0' || is_dashed_addr(d->name, d->addr))
            set_text(s, d->name, sizeof(d->name), val);
    }
    else if (!strcmp(key, "Icon"))      set_text(s, d->icon, sizeof(d->icon), val);
    else if (!strcmp(key, "Paired"))    set_bool(s, &d->paired, val);
    else if (!strcmp(key, "Bonded"))    set_bool(s, &d->bonded, val);
    else if (!strcmp(key, "Trusted"))   set_bool(s, &d->trusted, val);
    else if (!strcmp(key, "Connected")) {
        set_bool(s, &d->connected, val);
        if (d->connected) copy_str(s->last_conn_addr, sizeof(s->last_conn_addr), d->addr);
    }
    else if (!strcmp(key, "RSSI")) {
        if (!d->seen) { d->seen = true; s->changed = true; }
    }
}

static void apply_ctrl_kv(BtState *s, const char *key, const char *val)
{
    if      (!strcmp(key, "Powered"))     set_bool(s, &s->powered, val);
    else if (!strcmp(key, "Discovering")) set_bool(s, &s->discovering, val);
}

/* Split "Key: value" in place.  False if there is no ": ". */
static bool split_kv(char *t, char **key, char **val)
{
    char *c = strstr(t, ": ");
    if (!c) {
        /* "Value:" with nothing after it (a hex dump follows) */
        size_t n = strlen(t);
        if (n && t[n - 1] == ':') { t[n - 1] = '\0'; *key = t; *val = t + n; return true; }
        return false;
    }
    *c = '\0';
    *key = t;
    *val = trim(c + 2);
    return true;
}

static void set_prompt(BtState *s, BtPromptKind k, const char *code)
{
    if (!code) code = "";
    if (s->prompt != k || strcmp(s->prompt_code, code) != 0) {
        s->prompt = k;
        copy_str(s->prompt_code, sizeof(s->prompt_code), code);
        if (k == BT_PROMPT_NONE) {
            s->prompt_addr[0] = '\0';
        } else {
            s->prompt_seq++;
            copy_str(s->prompt_addr, sizeof(s->prompt_addr),
                     s->pairing_addr[0] ? s->pairing_addr : s->last_conn_addr);
        }
        s->changed = true;
    }
}

/* "[CHG] Device ADDR Paired:/Bonded:" — the pairing is over whoever started
 * it.  A displayed passkey/PIN for that device (or for an unknown one) has
 * nothing left to show. */
static void pairing_ended(BtState *s, const char *addr)
{
    copy_str(s->pair_ev_addr, sizeof(s->pair_ev_addr), addr);
    s->pair_seq++;
    s->changed = true;
    if ((s->prompt == BT_PROMPT_DISPLAY_PASSKEY || s->prompt == BT_PROMPT_DISPLAY_PIN) &&
        (!s->prompt_addr[0] || !strncmp(s->prompt_addr, addr, 17)))
        set_prompt(s, BT_PROMPT_NONE, NULL);
    if (!strncmp(s->pairing_addr, addr, 17)) s->pairing_addr[0] = '\0';
}

/* A normal shell prompt is back: any agent QUESTION is over.  Display
 * prompts (passkey/PIN) are printed lines, followed by a normal prompt
 * redraw, so they survive this and clear on the pairing result instead. */
static void normal_prompt_seen(BtState *s)
{
    if (s->prompt == BT_PROMPT_REQUEST_PIN || s->prompt == BT_PROMPT_CONFIRM ||
        s->prompt == BT_PROMPT_AUTHORIZE)
        set_prompt(s, BT_PROMPT_NONE, NULL);
}

static void copy_digits(char *dst, size_t sz, const char *src)
{
    size_t o = 0;
    while (*src == ' ') src++;
    while (*src >= '0' && *src <= '9' && o + 1 < sz) dst[o++] = *src++;
    dst[o] = '\0';
}

/* What the page (or an operator) can type at a question: yes, no, or a code. */
static bool is_answer(const char *t)
{
    const char *p = t;
    if (!strcmp(t, "yes") || !strcmp(t, "no")) return true;
    if (!*p) return false;
    while (*p >= '0' && *p <= '9') p++;
    return *p == '\0';
}

static void handle_line(BtState *s, char *t);

/* Text after "[agent] ".  Idempotent: runs on every redraw of the partial
 * line.  `complete` is false for the partial line, whose trailing text may be
 * an answer or an asynchronous line still arriving. */
static void handle_agent(BtState *s, const char *t, bool complete)
{
    char code[16];
    const char *end = NULL;
    BtPromptKind k = BT_PROMPT_NONE;

    if (starts(t, "Passkey:")) {
        copy_digits(code, sizeof(code), t + 8);
        set_prompt(s, BT_PROMPT_DISPLAY_PASSKEY, code);
        return;
    }
    if (starts(t, "PIN code:")) {
        const char *p = t + 9;
        while (*p == ' ') p++;
        copy_str(code, sizeof(code), p);
        set_prompt(s, BT_PROMPT_DISPLAY_PIN, trim(code));
        return;
    }
    code[0] = '\0';
    if (starts(t, "Enter PIN code:")) {
        k = BT_PROMPT_REQUEST_PIN; end = t + 15;
    } else if (starts(t, "Enter passkey") && (end = strstr(t, "):")) != NULL) {
        k = BT_PROMPT_REQUEST_PIN; end += 2;
    } else if (starts(t, "Confirm passkey ") && (end = strstr(t, "(yes/no):")) != NULL) {
        copy_digits(code, sizeof(code), t + 16);
        k = BT_PROMPT_CONFIRM; end += 9;
    } else if ((starts(t, "Accept pairing") || starts(t, "Authorize service ")) &&
               (end = strstr(t, "(yes/no):")) != NULL) {
        k = BT_PROMPT_AUTHORIZE; end += 9;
    }
    if (k == BT_PROMPT_NONE) return;
    while (*end == ' ') end++;
    char rest[sizeof(s->line) + 1];
    copy_str(rest, sizeof(rest), end);
    char *r = trim(rest);
    if (!*r) { set_prompt(s, k, code); return; }
    /* The echoed answer: it has been answered. */
    if (is_answer(r)) { set_prompt(s, BT_PROMPT_NONE, NULL); return; }
    if (!complete) return;             /* decided when the line completes */
    /* Anything else is an asynchronous line bt_shell_printf() wrote straight
     * after the question: while a question is open shell.c skips
     * rl_save_prompt(), so the prompt is not cleared first.  The question is
     * still open — shell.c redraws it next — so it stays set (or is set, if
     * this is the first we saw of it), and the text is a line of its own. */
    set_prompt(s, k, code);
    handle_line(s, r);
}

static bool is_echo(const char *t)
{
    static const char *const words[] = {
        "devices", "show", "info", "scan", "pair", "pairable", "connect",
        "disconnect", "remove", "trust", "untrust", "block", "unblock",
        "default-agent", "agent", "power", "discoverable", "quit", "exit",
        "yes", "no", "cancel-pairing", "list", "select", NULL
    };
    int i;
    for (i = 0; words[i]; i++) {
        size_t n = strlen(words[i]);
        if (strncmp(t, words[i], n) == 0 && (t[n] == '\0' || t[n] == ' ')) return true;
    }
    return false;
}

static void set_result(BtState *s, const char *text, bool ok)
{
    copy_str(s->result, sizeof(s->result), text);
    s->result_ok = ok;
    s->result_new = true;
    s->changed = true;
}

/* Lines that report a command's outcome.  `clear` ends a pairing's agent prompt. */
static bool handle_result(BtState *s, const char *t)
{
    static const struct { const char *prefix; bool ok; bool clear; } R[] = {
        { "Pairing successful",        true,  true  },
        { "Failed to pair",            false, true  },
        { "Cancel pairing successful", true,  true  },
        { "Failed to cancel pairing",  false, false },
        { "Connection successful",     true,  false },
        { "Failed to connect",         false, false },
        { "Successful disconnected",   true,  false },
        { "Failed to disconnect",      false, false },
        { "Device has been removed",   true,  false },
        { "Failed to remove device",   false, false },
        { "Failed to start discovery", false, false },
        { "Failed to stop discovery",  false, false },
        { "Failed to set ",            false, false },
        { NULL, false, false }
    };
    int i;
    for (i = 0; R[i].prefix; i++) {
        if (starts(t, R[i].prefix)) {
            set_result(s, t, R[i].ok);
            if (R[i].clear) {
                set_prompt(s, BT_PROMPT_NONE, NULL);
                s->pairing_addr[0] = '\0';
            }
            return true;
        }
    }
    /* "Changing <x> succeeded" — but not the parser's own opening `pairable on` */
    if (starts(t, "Changing ")) {
        size_t n = strlen(t);
        if (!starts(t, "Changing pairable") && n > 10 && !strcmp(t + n - 10, " succeeded"))
            set_result(s, t, true);
        return true;
    }
    return false;
}

/* "[NEW] ", "[CHG] ", "[DEL] " — t points past the tag. */
static void handle_event(BtState *s, char tag, char *t)
{
    if (starts(t, "Device ") && is_addr(t + 7)) {
        char addr[BT_ADDR_LEN];
        char *rest = t + 7 + 17;
        memcpy(addr, t + 7, 17);
        addr[17] = '\0';
        if (*rest == ' ') rest++;
        if (tag == 'N') {
            BtDevice *d = list_device(s, addr, rest);
            if (d && s->discovering && !d->seen) { d->seen = true; s->changed = true; }
        } else if (tag == 'D') {
            remove_device(s, addr);
        } else {
            char *key, *val;
            if (!split_kv(rest, &key, &val)) return;
            bool added;
            BtDevice *d = get_device(s, addr, &added);
            if (!d) return;
            if (added) queue_info(s, d->addr);
            apply_device_kv(s, d, key, val);
            if (!strcmp(key, "Paired") || !strcmp(key, "Bonded")) pairing_ended(s, d->addr);
        }
    } else if (starts(t, "Controller ") && is_addr(t + 11) && tag == 'C') {
        char *key, *val, *rest = t + 11 + 17;
        if (*rest == ' ') rest++;
        if (split_kv(rest, &key, &val)) apply_ctrl_kv(s, key, val);
    }
}

/* One complete, cleaned line (a \r- or \n-terminated segment). */
static void handle_line(BtState *s, char *t)
{
    bool had_prompt;
    t = strip_prompts(t, &had_prompt);

    if (starts(t, "[agent] ")) {
        s->info_addr[0] = '\0'; s->info_ctrl = false;
        handle_agent(s, t + 8, true);
        return;
    }
    bool indented = (*t == '\t' || *t == ' ');
    char *u = trim(t);
    if (*u == '\0') {                  /* a bare prompt redraw, or spaces */
        if (had_prompt) normal_prompt_seen(s);
        return;
    }
    if (is_echo(u)) return;

    if (indented) {                    /* a field of the open block, or a hex dump line */
        char *key, *val;
        if (!split_kv(u, &key, &val)) return;
        if (s->info_ctrl) {
            apply_ctrl_kv(s, key, val);
        } else if (s->info_addr[0]) {
            BtDevice *d = bt_find(s, s->info_addr);
            if (d) apply_device_kv(s, d, key, val);
        }
        return;
    }

    /* any non-indented line closes a block */
    s->info_addr[0] = '\0';
    s->info_ctrl = false;

    if (starts(u, "[NEW] ") || starts(u, "[CHG] ") || starts(u, "[DEL] ")) {
        handle_event(s, u[1], u + 6);
        return;
    }

    if (starts(u, "Device ") && is_addr(u + 7)) {
        char addr[BT_ADDR_LEN];
        char *rest = u + 7 + 17;
        memcpy(addr, u + 7, 17);
        addr[17] = '\0';
        if (*rest == ' ') rest++;
        if (!strcmp(rest, "(public)") || !strcmp(rest, "(random)")) {
            bool added;
            BtDevice *d = get_device(s, addr, &added);
            if (!d) return;
            copy_str(s->info_addr, sizeof(s->info_addr), d->addr);
            if (!d->info_loaded) { d->info_loaded = true; s->changed = true; }
        } else if (!strcmp(rest, "not available")) {
            set_result(s, u, false);
        } else {
            list_device(s, addr, rest);
        }
        return;
    }
    if (starts(u, "Controller ") && is_addr(u + 11)) {
        const char *rest = u + 11 + 17;
        if (!strcmp(rest, " (public)") || !strcmp(rest, " (random)")) s->info_ctrl = true;
        return;
    }
    if (!strcmp(u, "Agent registered") || !strcmp(u, "Agent is already registered")) {
        if (!s->agent_ready) {
            bt_queue(s, "default-agent");
            s->agent_ready = true;
            s->changed = true;
        }
        return;
    }
    if (starts(u, "Attempting to pair with ") && is_addr(u + 24)) {
        copy_str(s->pairing_addr, sizeof(s->pairing_addr), u + 24);
        s->pairing_addr[17] = '\0';
        return;
    }
    if (!strcmp(u, "Request canceled") || !strcmp(u, "Agent released")) {
        set_prompt(s, BT_PROMPT_NONE, NULL);
        return;
    }
    handle_result(s, u);
}

/* The pending partial line: only agent questions and the prompt reset. */
static void handle_partial(BtState *s)
{
    char buf[sizeof(s->line) + 1];
    bool had_prompt;
    clean(s->line, s->linelen, buf, sizeof(buf));
    char *t = strip_prompts(buf, &had_prompt);
    if (starts(t, "[agent] ")) {
        handle_agent(s, t + 8, false);
        return;
    }
    if (had_prompt && *trim(t) == '\0') normal_prompt_seen(s);
}

void bt_parse_feed(BtState *s, const char *buf, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r') {
            char line[sizeof(s->line) + 1];
            clean(s->line, s->linelen, line, sizeof(line));
            s->linelen = 0;
            handle_line(s, line);
            continue;
        }
        if (s->linelen + 1 >= sizeof(s->line)) {       /* overlong: treat as a line */
            char line[sizeof(s->line) + 1];
            clean(s->line, s->linelen, line, sizeof(line));
            s->linelen = 0;
            handle_line(s, line);
        }
        s->line[s->linelen++] = c;
    }
    if (s->linelen) handle_partial(s);
}

/* ------------------------------------------------------------------ */
/* the child process                                                    */
/* ------------------------------------------------------------------ */

static void close_fd(int *fd)
{
    if (*fd >= 0) close(*fd);
    *fd = -1;
}

/* The child is gone (or going): drop everything that depended on it. */
static void closed_down(BtState *s)
{
    close_fd(&s->to_child);
    close_fd(&s->from_child);
    s->pid = 0;
    s->agent_ready = false;
    s->prompt = BT_PROMPT_NONE;
    s->prompt_code[0] = '\0';
    s->prompt_addr[0] = '\0';
    s->pairing_addr[0] = '\0';
    s->outqlen = 0;
    s->linelen = 0;
    s->changed = true;
}

/* Write what the pipe takes.  SIGPIPE is blocked around the write and a
 * pending one consumed, so a dead child is an EPIPE, not the end of the
 * control panel — without changing the process-wide disposition. */
static void flush_outq(BtState *s)
{
    sigset_t pipe_set, old;
    if (s->to_child < 0 || s->outqlen == 0) return;
    sigemptyset(&pipe_set);
    sigaddset(&pipe_set, SIGPIPE);
    sigprocmask(SIG_BLOCK, &pipe_set, &old);
    while (s->outqlen > 0) {
        ssize_t w = write(s->to_child, s->outq, s->outqlen);
        if (w > 0) {
            memmove(s->outq, s->outq + w, s->outqlen - (size_t)w);
            s->outqlen -= (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && errno == EPIPE) {
            struct timespec zero = { 0, 0 };
            sigtimedwait(&pipe_set, NULL, &zero);
            close_fd(&s->to_child);
            s->outqlen = 0;
        }
        break;                          /* EAGAIN: keep the remainder */
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
}

bool bt_ctl_start(BtState *s)
{
    int in[2], out[2];
    pid_t pid;

    bt_state_init(s);
    if (pipe(in) < 0) return false;
    if (pipe(out) < 0) { close(in[0]); close(in[1]); return false; }

    pid = fork();
    if (pid < 0) {
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        return false;
    }
    if (pid == 0) {
        int fd;
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(out[1], 2);
        for (fd = 3; fd < 256; fd++) close(fd);   /* framebuffer, evdev, ... */
        signal(SIGPIPE, SIG_DFL);
        execl("/usr/bin/bluetoothctl", "bluetoothctl", "--agent", "DisplayYesNo", (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    s->pid = pid;
    s->to_child = in[1];
    s->from_child = out[0];
    fcntl(s->to_child, F_SETFD, FD_CLOEXEC);
    fcntl(s->from_child, F_SETFD, FD_CLOEXEC);
    fcntl(s->to_child, F_SETFL, fcntl(s->to_child, F_GETFL) | O_NONBLOCK);
    fcntl(s->from_child, F_SETFL, fcntl(s->from_child, F_GETFL) | O_NONBLOCK);

    bt_queue(s, "pairable on");
    bt_queue(s, "show");
    bt_queue(s, "devices");
    flush_outq(s);
    return true;
}

/* Drain the read end.  Returns false on EOF. */
static bool drain(BtState *s)
{
    char buf[512];
    int guard;
    if (s->from_child < 0) return false;
    for (guard = 0; guard < 64; guard++) {
        ssize_t r = read(s->from_child, buf, sizeof(buf));
        if (r > 0) { bt_parse_feed(s, buf, (size_t)r); continue; }
        if (r == 0) { close_fd(&s->from_child); return false; }
        if (errno == EINTR) continue;
        break;                          /* EAGAIN */
    }
    return true;
}

static bool reap(BtState *s, int flags)
{
    int status;
    if (s->pid <= 0) return true;
    pid_t r = waitpid(s->pid, &status, flags);
    if (r == s->pid || (r < 0 && errno == ECHILD)) { closed_down(s); return true; }
    return false;
}

bool bt_ctl_poll(BtState *s)
{
    bool before = s->changed, got;
    s->changed = false;
    if (s->pid > 0) {
        drain(s);
        flush_outq(s);
        reap(s, WNOHANG);
    }
    got = s->changed;
    s->changed = before || got;
    return got;
}

void bt_ctl_send(BtState *s, const char *fmt, ...)
{
    char cmd[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    if (s->to_child < 0) return;
    bt_queue(s, cmd);
    flush_outq(s);
}

bool bt_ctl_running(const BtState *s)
{
    return s->pid > 0;
}

void bt_ctl_stop(BtState *s)
{
    int i;
    if (s->pid <= 0) { closed_down(s); return; }

    bt_queue(s, "quit");
    flush_outq(s);
    for (i = 0; i < 6; i++) {           /* up to ~300 ms for a clean exit */
        if (s->from_child >= 0) {
            struct pollfd p = { s->from_child, POLLIN, 0 };
            poll(&p, 1, 50);
            drain(s);
        } else {
            struct timespec ts = { 0, 50L * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
        if (reap(s, WNOHANG)) return;
    }
    kill(s->pid, SIGTERM);
    for (i = 0; i < 4; i++) {           /* ~200 ms more, then no more asking */
        struct timespec ts = { 0, 50L * 1000 * 1000 };
        if (reap(s, WNOHANG)) return;
        nanosleep(&ts, NULL);
    }
    kill(s->pid, SIGKILL);
    reap(s, 0);
}
