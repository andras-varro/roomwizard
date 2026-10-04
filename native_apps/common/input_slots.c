/*
 * input_slots — which player (P1..P4) each input device is.  The rules and the
 * identity rule are in input_slots.h; this file only implements them.
 */
#include "input_slots.h"

#include <string.h>

/* Length of phys without a trailing "/input<digits>": the part one physical
 * device's event nodes share. */
static size_t phys_stem_len(const char *p, size_t cap) {
    size_t n = strnlen(p, cap);
    size_t i = n;
    while (i > 0 && p[i - 1] >= '0' && p[i - 1] <= '9') i--;
    if (i == n) return n;                       /* no digits at the end */
    static const char suffix[] = "/input";
    size_t sl = sizeof(suffix) - 1;
    if (i >= sl && memcmp(p + i - sl, suffix, sl) == 0) return i - sl;
    return n;
}

bool input_ident_equal(const InputIdent *a, const InputIdent *b) {
    if (!a || !b) return false;
    if (a->bus != b->bus) return false;
    size_t ua = strnlen(a->uniq, sizeof(a->uniq));
    size_t ub = strnlen(b->uniq, sizeof(b->uniq));
    if (ua && ub)
        return ua == ub && memcmp(a->uniq, b->uniq, ua) == 0;
    if (a->vid != b->vid || a->pid != b->pid) return false;
    size_t pa = phys_stem_len(a->phys, sizeof(a->phys));
    size_t pb = phys_stem_len(b->phys, sizeof(b->phys));
    return pa == pb && memcmp(a->phys, b->phys, pa) == 0;
}

void input_slots_clear(InputSlotTable *t) {
    memset(t, 0, sizeof(*t));
}

void input_slots_begin_scan(InputSlotTable *t) {
    for (int i = 0; i < INPUT_SLOTS; i++) t->slot[i].present = false;
}

int input_slots_find(const InputSlotTable *t, const InputIdent *id) {
    for (int i = 0; i < INPUT_SLOTS; i++)
        if (t->slot[i].reserved && input_ident_equal(&t->slot[i].ident, id))
            return i;
    return -1;
}

static void take(InputSlotTable *t, int i, const InputIdent *id, bool present) {
    t->slot[i].ident    = *id;
    t->slot[i].reserved = true;
    t->slot[i].present  = present;
    t->slot[i].seen     = ++t->clock;
}

int input_slots_assign(InputSlotTable *t, const InputIdent *id) {
    if (!id) return -1;
    int i = input_slots_find(t, id);
    if (i >= 0) {                               /* its own slot, pinned or not */
        take(t, i, id, true);
        return i;
    }
    for (int j = 0; j < INPUT_SLOTS && i < 0; j++)
        if (!t->slot[j].reserved) i = j;
    if (i < 0) {
        /* Every slot reserved: evict the oldest-seen absent reservation that
         * is not pinned. */
        for (int j = 0; j < INPUT_SLOTS; j++)
            if (!t->slot[j].present && !t->slot[j].pinned &&
                (i < 0 || t->slot[j].seen < t->slot[i].seen))
                i = j;
        if (i < 0) return -1;                   /* present or pinned, all four */
    }
    take(t, i, id, true);
    t->slot[i].pinned = false;
    return i;
}

int input_slots_set(InputSlotTable *t, const InputIdent *id, int slot) {
    if (!id || slot < 0 || slot >= INPUT_SLOTS) return -1;
    bool present = true;
    int old = input_slots_find(t, id);
    if (old >= 0) {
        present = t->slot[old].present;
        memset(&t->slot[old], 0, sizeof(t->slot[old]));
    }
    take(t, slot, id, present);
    t->slot[slot].pinned = false;               /* set never pins; see the header */
    return slot;
}

int input_slots_pin(InputSlotTable *t, const InputIdent *id, int slot) {
    if (!t || !id || slot < 0 || slot >= INPUT_SLOTS) return -1;
    bool present = false;                       /* unknown device: absent */
    for (int i = 0; i < INPUT_SLOTS; i++) {
        if (t->slot[i].reserved && input_ident_equal(&t->slot[i].ident, id)) {
            present = present || t->slot[i].present;
            if (i != slot) memset(&t->slot[i], 0, sizeof(t->slot[i]));
        }
    }
    take(t, slot, id, present);
    t->slot[slot].pinned = true;
    return slot;
}

void input_slots_unpin(InputSlotTable *t, int slot) {
    if (!t || slot < 0 || slot >= INPUT_SLOTS) return;
    t->slot[slot].pinned = false;
}

/* ── Text form ─────────────────────────────────────────────────────────── */

/* A free-form field may hold no space or control character: config.c trims
 * values and a newline would end the line. */
static bool field_ok(const char *p, size_t len) {
    if (len == 0) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c <= ' ' || c == 0x7f) return false;
    }
    return true;
}

/* Append to out[*at..n), always NUL-terminated; false when it does not fit. */
static bool put(char *out, size_t n, size_t *at, const char *s, size_t len) {
    if (*at + len >= n) return false;
    memcpy(out + *at, s, len);
    *at += len;
    out[*at] = '\0';
    return true;
}

static bool put_hex4(char *out, size_t n, size_t *at, uint16_t v) {
    static const char hex[] = "0123456789abcdef";
    char b[5] = { ':', hex[(v >> 12) & 15], hex[(v >> 8) & 15],
                  hex[(v >> 4) & 15], hex[v & 15] };
    return put(out, n, at, b, sizeof(b));
}

bool input_ident_format(const InputIdent *id, char *out, size_t n) {
    if (!out || n == 0) return false;
    out[0] = '\0';
    if (!id) return false;
    size_t at = 0;
    size_t ul = strnlen(id->uniq, sizeof(id->uniq));
    bool ok;
    if (ul) {
        ok = ul < sizeof(id->uniq) && field_ok(id->uniq, ul) &&
             put(out, n, &at, "u", 1) &&
             put_hex4(out, n, &at, id->bus) && put(out, n, &at, ":", 1) &&
             put(out, n, &at, id->uniq, ul);
    } else {
        /* The stem, unless it is empty or would be stripped again on the way
         * back in — then the whole phys, whose stem is the same. */
        size_t pl = strnlen(id->phys, sizeof(id->phys));
        size_t sl = phys_stem_len(id->phys, sizeof(id->phys));
        if (sl == 0 || phys_stem_len(id->phys, sl) != sl) sl = pl;
        ok = sl < sizeof(id->phys) && field_ok(id->phys, sl) &&
             put(out, n, &at, "p", 1) &&
             put_hex4(out, n, &at, id->bus) && put_hex4(out, n, &at, id->vid) &&
             put_hex4(out, n, &at, id->pid) && put(out, n, &at, ":", 1) &&
             put(out, n, &at, id->phys, sl);
    }
    if (!ok) out[0] = '\0';
    return ok;
}

/* ":hhhh" at *s, advancing it; exactly four hex digits, either case. */
static bool get_hex4(const char **s, uint16_t *v) {
    const char *p = *s;
    if (*p++ != ':') return false;
    unsigned acc = 0;
    for (int i = 0; i < 4; i++, p++) {
        int d;
        if (*p >= '0' && *p <= '9')      d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else return false;
        acc = acc * 16 + (unsigned)d;
    }
    *v = (uint16_t)acc;
    *s = p;
    return true;
}

/* ":<field>" running to the end of the string, into dst[cap] with its NUL. */
static bool get_tail(const char *s, char *dst, size_t cap) {
    if (*s++ != ':') return false;
    size_t len = strnlen(s, cap);
    if (len >= cap || !field_ok(s, len)) return false;
    memcpy(dst, s, len);
    dst[len] = '\0';
    return true;
}

bool input_ident_parse(const char *s, InputIdent *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!s) return false;
    bool ok = false;
    const char *p = s + 1;
    if (s[0] == 'u')
        ok = get_hex4(&p, &out->bus) && get_tail(p, out->uniq, sizeof(out->uniq));
    else if (s[0] == 'p')
        ok = get_hex4(&p, &out->bus) && get_hex4(&p, &out->vid) &&
             get_hex4(&p, &out->pid) && get_tail(p, out->phys, sizeof(out->phys));
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}
