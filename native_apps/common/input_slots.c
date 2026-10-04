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
    if (i < 0) {
        for (int j = 0; j < INPUT_SLOTS && i < 0; j++)
            if (!t->slot[j].reserved) i = j;
    }
    if (i < 0) {
        /* Every slot reserved: evict the oldest-seen absent reservation. */
        for (int j = 0; j < INPUT_SLOTS; j++)
            if (!t->slot[j].present && (i < 0 || t->slot[j].seen < t->slot[i].seen))
                i = j;
        if (i < 0) return -1;                   /* four present devices */
    }
    take(t, i, id, true);
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
    return slot;
}
