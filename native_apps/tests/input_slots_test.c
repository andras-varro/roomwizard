/* Host-side regression for common/input_slots.c: which player slot (P1..P4)
 * each input device gets, and keeps across a disconnect.
 *
 * The defect this guards: slots handed out in scan order, so a pad that drops
 * off Bluetooth and comes back — or that simply lands on another event node,
 * or is found before the keyboard this time — becomes another player.  Each
 * case below drives begin_scan/assign the way a rescan would, with InputIdent
 * fixtures that mirror what EVIOCGID/EVIOCGUNIQ/EVIOCGPHYS report: a USB
 * keyboard's two nodes (phys .../input0 and .../input1), two identical wired
 * pads on different ports, and BT pads whose phys is the adapter's MAC.
 * Cases 12-15 cover pinning (the operator's choice, never evicted by assign);
 * 16-18 the text form an assignment is persisted in, round trip and refusals.
 *
 * What this cannot see: the ioctls that fill an InputIdent on the device, and
 * whether a real BT pad's uniq is stable across a re-pair (inferred from the
 * kernel's hidp, not measured here).
 *
 * Build (this line is the CTEST_ROWS row in tests/run-all.sh):
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *       -o build/input_slots_test tests/input_slots_test.c common/input_slots.c -lm
 */
#include <stdio.h>
#include <string.h>
#include "input_slots.h"

static int g_pass, g_fail;

static void check(int cond, const char *what) {
    if (cond) { g_pass++; printf("  PASS  %s\n", what); }
    else      { g_fail++; printf("  FAIL  %s\n", what); }
}

/* ── Fixtures ───────────────────────────────────────────────────────────── */

#define BUS_USB 0x03
#define BUS_BT  0x05

static InputIdent ident(uint16_t bus, uint16_t vid, uint16_t pid,
                        const char *uniq, const char *phys) {
    InputIdent d;
    memset(&d, 0, sizeof(d));
    d.bus = bus; d.vid = vid; d.pid = pid;
    snprintf(d.uniq, sizeof(d.uniq), "%s", uniq);
    snprintf(d.phys, sizeof(d.phys), "%s", phys);
    return d;
}

/* A USB keyboard: two event nodes, one device. */
static InputIdent kbd_node(int n) {
    char phys[64];
    snprintf(phys, sizeof(phys), "usb-musb-hdrc.1.auto-1.2/input%d", n);
    return ident(BUS_USB, 0x046d, 0xc31c, "", phys);
}

/* A wired Xbox pad on hub port `port`. */
static InputIdent xpad_on(int port) {
    char phys[64];
    snprintf(phys, sizeof(phys), "usb-musb-hdrc.1.auto-1.%d/input0", port);
    return ident(BUS_USB, 0x045e, 0x028e, "", phys);
}

/* A BT pad: uniq is its own MAC, phys is the adapter's MAC. */
#define ADAPTER "00:1a:7d:da:71:13"
static InputIdent bt_pad(const char *mac) {
    return ident(BUS_BT, 0x045e, 0x02e0, mac, ADAPTER);
}

static void scan(InputSlotTable *t, const InputIdent *d, int n, int *out) {
    input_slots_begin_scan(t);
    for (int i = 0; i < n; i++) out[i] = input_slots_assign(t, &d[i]);
}

int main(void) {
    InputSlotTable t;
    int s[8];

    /* 1. First device P1, second P2. */
    input_slots_clear(&t);
    {
        InputIdent d[2] = { xpad_on(1), xpad_on(2) };
        scan(&t, d, 2, s);
        check(s[0] == 0 && s[1] == 1, "first device -> P1, second -> P2");
    }

    /* 2. The same device twice in one scan: the same slot. */
    input_slots_clear(&t);
    {
        InputIdent d[3] = { xpad_on(1), xpad_on(1), xpad_on(2) };
        scan(&t, d, 3, s);
        check(s[0] == 0 && s[1] == 0 && s[2] == 1,
              "same device twice in one scan -> same slot, next device P2");
    }

    /* 3. One keyboard's input0 and input1 nodes: one device. */
    input_slots_clear(&t);
    {
        InputIdent d[3] = { kbd_node(0), kbd_node(1), xpad_on(1) };
        scan(&t, d, 3, s);
        check(s[0] == 0 && s[1] == 0, "phys .../input0 and .../input1 -> same slot");
        check(s[2] == 1, "the pad after the two keyboard nodes -> P2");
        InputIdent other = ident(BUS_USB, 0x046d, 0xc31c, "", "usb-musb-hdrc.1.auto-1.3/input0");
        check(!input_ident_equal(&d[0], &other),
              "same vid:pid, phys differing before /inputN -> different devices");
    }

    /* 4. Absent but reserved; a new device skips the reserved slot. */
    input_slots_clear(&t);
    {
        InputIdent d[2] = { xpad_on(1), xpad_on(2) };
        scan(&t, d, 2, s);
        InputIdent only[1] = { xpad_on(2) };
        scan(&t, only, 1, s);
        check(s[0] == 1, "the remaining pad keeps P2");
        check(t.slot[0].reserved && !t.slot[0].present,
              "P1 is absent but still reserved after a scan without its pad");
        InputIdent fresh[2] = { xpad_on(2), xpad_on(3) };
        scan(&t, fresh, 2, s);
        check(s[1] == 2, "a new device skips the reserved P1 and takes P3");
        check(input_slots_find(&t, &d[0]) == 0, "find still names P1 for the absent pad");
    }

    /* 5. A reconnecting device gets its slot back, presented in another order. */
    input_slots_clear(&t);
    {
        InputIdent a = xpad_on(1), b = bt_pad("e4:17:d8:00:00:01"), k = kbd_node(0);
        InputIdent d1[3] = { a, b, k };
        scan(&t, d1, 3, s);                   /* a=P1 b=P2 k=P3 */
        InputIdent d2[2] = { a, k };
        scan(&t, d2, 2, s);                   /* b drops off */
        InputIdent d3[3] = { b, k, a };       /* b reconnects, found first */
        scan(&t, d3, 3, s);
        check(s[0] == 1 && s[1] == 2 && s[2] == 0,
              "reconnecting BT pad found first still gets P2; others unchanged");
        InputIdent k1 = kbd_node(1);          /* keyboard back on its other node only */
        InputIdent d4[3] = { k1, a, b };
        scan(&t, d4, 3, s);
        check(s[0] == 2, "keyboard back on another event node -> its P3");
    }

    /* 6. Identical vid:pid on different phys: different slots. */
    input_slots_clear(&t);
    {
        InputIdent d[2] = { xpad_on(1), xpad_on(2) };
        check(!input_ident_equal(&d[0], &d[1]), "identical wired pads on two ports are not equal");
        scan(&t, d, 2, s);
        check(s[0] != s[1] && s[0] >= 0 && s[1] >= 0,
              "identical vid:pid on different phys -> different slots");
    }

    /* 7. BT: uniq decides, phys (the adapter MAC) is ignored. */
    input_slots_clear(&t);
    {
        InputIdent p = bt_pad("e4:17:d8:00:00:01");
        InputIdent p_other_adapter = ident(BUS_BT, 0x045e, 0x02e0,
                                           "e4:17:d8:00:00:01", "5c:f3:70:00:00:99");
        InputIdent q = bt_pad("e4:17:d8:00:00:02");
        check(input_ident_equal(&p, &p_other_adapter), "BT: same uniq, different phys -> equal");
        check(!input_ident_equal(&p, &q), "BT: different uniq, same adapter phys -> not equal");
        InputIdent d[3] = { p, p_other_adapter, q };
        scan(&t, d, 3, s);
        check(s[0] == 0 && s[1] == 0, "BT: same uniq, different phys -> same slot");
        check(s[2] == 1, "BT: different uniq, same phys -> different slot");
    }

    /* 8. Five present devices: the fifth gets -1. */
    input_slots_clear(&t);
    {
        InputIdent d[5] = { xpad_on(1), xpad_on(2), xpad_on(3), xpad_on(4), kbd_node(0) };
        scan(&t, d, 5, s);
        check(s[0] == 0 && s[1] == 1 && s[2] == 2 && s[3] == 3, "four present devices -> P1..P4");
        check(s[4] == -1, "the fifth present device -> -1");
    }

    /* 9. Eviction: four reserved, all absent; a new device takes the oldest. */
    input_slots_clear(&t);
    {
        InputIdent d[4] = { xpad_on(1), xpad_on(2), xpad_on(3), xpad_on(4) };
        scan(&t, d, 4, s);                    /* all four reserved */
        InputIdent later[3] = { xpad_on(4), xpad_on(1), xpad_on(3) };
        scan(&t, later, 3, s);                /* P2's pad is now the oldest seen */
        scan(&t, d, 0, s);                    /* everything unplugged */
        InputIdent fresh = kbd_node(0);
        input_slots_begin_scan(&t);
        int got = input_slots_assign(&t, &fresh);
        check(got == 1, "all four absent: a new device evicts the oldest-seen reservation (P2)");
        check(input_slots_find(&t, &d[1]) == -1, "the evicted pad no longer has a reservation");
        check(input_slots_find(&t, &d[0]) == 0, "the other reservations survive");
        /* Present devices are never evicted: three present + the keyboard. */
        InputIdent mix[4] = { xpad_on(1), xpad_on(3), xpad_on(4), kbd_node(0) };
        scan(&t, mix, 4, s);
        InputIdent extra = xpad_on(2);
        check(input_slots_assign(&t, &extra) == -1, "four present: a returning pad gets -1, nobody evicted");
    }

    /* 10. set moves a device and frees its old slot. */
    input_slots_clear(&t);
    {
        InputIdent d[3] = { xpad_on(1), xpad_on(2), xpad_on(3) };
        scan(&t, d, 3, s);
        check(input_slots_set(&t, &d[0], 3) == 3, "set P1's pad into P4 returns 3");
        check(input_slots_find(&t, &d[0]) == 3 && t.slot[3].present, "it is in P4, still present");
        check(!t.slot[0].reserved, "its old slot P1 is free");
        check(input_slots_set(&t, &d[0], 1) == 1 && input_slots_find(&t, &d[1]) == -1,
              "set into P2 evicts P2's pad");
        InputIdent fresh = kbd_node(0);
        check(input_slots_assign(&t, &fresh) == 0, "a new device takes the freed P1");
        check(input_slots_set(&t, &fresh, 4) == -1 && input_slots_set(&t, &fresh, -1) == -1,
              "set out of range -> -1");
        check(input_slots_find(&t, &fresh) == 0, "and leaves the device where it was");
    }

    /* 11. clear forgets everything. */
    {
        input_slots_clear(&t);
        int any = 0;
        for (int i = 0; i < INPUT_SLOTS; i++)
            if (t.slot[i].reserved || t.slot[i].present) any++;
        check(any == 0, "clear: no slot reserved or present");
        InputIdent d[1] = { xpad_on(3) };
        scan(&t, d, 1, s);
        check(s[0] == 0, "after clear, a previously-reserved pad starts again at P1");
    }

    /* 12. pin moves a device out of its other slot; that slot reverts to auto. */
    input_slots_clear(&t);
    {
        InputIdent d[3] = { xpad_on(1), xpad_on(2), xpad_on(3) };
        scan(&t, d, 3, s);                    /* P1 P2 P3 */
        check(input_slots_pin(&t, &d[0], 0) == 0 && t.slot[0].pinned,
              "pin P1's pad to P1: returns 0, pinned");
        check(input_slots_pin(&t, &d[0], 2) == 2, "pin the same pad to P3 returns 2");
        check(input_slots_find(&t, &d[0]) == 2 && t.slot[2].pinned && t.slot[2].present,
              "it is in P3, pinned, still present");
        check(!t.slot[0].reserved && !t.slot[0].pinned, "its old pinned P1 is free and unpinned");
        check(input_slots_find(&t, &d[2]) == -1, "P3's previous holder is evicted, not swapped");
        int held = 0;
        for (int i = 0; i < INPUT_SLOTS; i++)
            if (t.slot[i].reserved && input_ident_equal(&t.slot[i].ident, &d[0])) held++;
        check(held == 1, "the pinned pad lives in exactly one slot");
        check(input_slots_pin(&t, &d[0], 4) == -1 && input_slots_pin(&t, &d[0], -1) == -1 &&
              input_slots_pin(&t, NULL, 1) == -1 && input_slots_pin(NULL, &d[0], 1) == -1,
              "pin: out of range or NULL -> -1");
        check(input_slots_find(&t, &d[0]) == 2, "and leaves the device where it was");
    }

    /* 13. A pinned slot survives a full table and a fifth device. */
    input_slots_clear(&t);
    {
        InputIdent d[4] = { xpad_on(1), xpad_on(2), xpad_on(3), xpad_on(4) };
        scan(&t, d, 4, s);
        input_slots_pin(&t, &d[0], 0);        /* P1 pinned (pin bumps its clock) */
        scan(&t, d + 1, 3, s);                /* so rescan the others: P1 now the oldest */
        scan(&t, d, 0, s);                    /* everything unplugged */
        check(t.slot[0].seen < t.slot[1].seen, "fixture: pinned P1 is the oldest-seen slot");
        InputIdent fifth = kbd_node(0);
        input_slots_begin_scan(&t);
        int got = input_slots_assign(&t, &fifth);
        check(got != 0, "all absent, P1 pinned and oldest: the new device does not take P1");
        check(got == 1, "it evicts the oldest UNPINNED reservation (P2)");
        check(input_slots_find(&t, &d[0]) == 0 && t.slot[0].pinned,
              "the pinned pad keeps P1");
        /* Three present + a pinned absent: a newcomer gets -1, never the pin. */
        InputIdent mix[3] = { fifth, xpad_on(3), xpad_on(4) };
        scan(&t, mix, 3, s);
        InputIdent other = xpad_on(5);
        check(input_slots_assign(&t, &other) == -1,
              "three present + pinned absent: a newcomer gets -1");
        check(input_slots_find(&t, &d[0]) == 0, "the pin is still there");
        check(input_slots_assign(&t, &d[0]) == 0 && t.slot[0].present && t.slot[0].pinned,
              "the pinned pad appears and lands in its pinned P1");
    }

    /* 14. A pin loaded before its device is seen: absent, slot not handed out. */
    input_slots_clear(&t);
    {
        InputIdent p = bt_pad("e4:17:d8:40:eb:ae");
        check(input_slots_pin(&t, &p, 0) == 0, "pin an unseen BT pad to P1");
        check(t.slot[0].reserved && t.slot[0].pinned && !t.slot[0].present,
              "an unseen pinned device is reserved but absent");
        InputIdent d[2] = { xpad_on(1), kbd_node(0) };
        scan(&t, d, 2, s);
        check(s[0] == 1 && s[1] == 2, "other devices skip the pinned P1 -> P2, P3");
        InputIdent d2[3] = { xpad_on(1), kbd_node(0), p };
        scan(&t, d2, 3, s);
        check(s[2] == 0, "the pinned pad arrives last and still gets P1");
    }

    /* 15. unpin keeps the holder as an ordinary reservation; set ends a pin. */
    input_slots_clear(&t);
    {
        InputIdent d[2] = { xpad_on(1), xpad_on(2) };
        scan(&t, d, 2, s);
        input_slots_pin(&t, &d[1], 3);
        input_slots_unpin(&t, 3);
        check(!t.slot[3].pinned && input_slots_find(&t, &d[1]) == 3 && t.slot[3].present,
              "unpin: P4's pad keeps P4, no longer pinned");
        input_slots_unpin(&t, 4);
        input_slots_unpin(&t, -1);
        check(input_slots_find(&t, &d[1]) == 3, "unpin out of range is a no-op");
        input_slots_pin(&t, &d[1], 3);
        check(input_slots_set(&t, &d[0], 3) == 3 && !t.slot[3].pinned &&
              input_slots_find(&t, &d[1]) == -1,
              "set into a pinned slot evicts the holder and ends the pin");
        input_slots_pin(&t, &d[0], 3);
        check(input_slots_set(&t, &d[0], 1) == 1 && !t.slot[1].pinned && !t.slot[3].reserved,
              "set a pinned device elsewhere: no pin anywhere");
        input_slots_pin(&t, &d[0], 2);
        input_slots_clear(&t);
        int pins = 0;
        for (int i = 0; i < INPUT_SLOTS; i++) if (t.slot[i].pinned) pins++;
        check(pins == 0, "clear drops every pin");
    }

    /* 16. Text form round trips. */
    {
        char buf[64];
        InputIdent bt = ident(BUS_BT, 0x045e, 0x02e0, "e4:17:d8:40:eb:ae", ADAPTER);
        check(input_ident_format(&bt, buf, sizeof(buf)) &&
              strcmp(buf, "u:0005:e4:17:d8:40:eb:ae") == 0, "format BT -> u:0005:e4:17:d8:40:eb:ae");
        InputIdent back;
        check(input_ident_parse(buf, &back) && input_ident_equal(&back, &bt),
              "parse(format(BT)) equals the BT ident");
        InputIdent usb = ident(BUS_USB, 0x045e, 0x028e, "", "usb-musb-hdrc.0.auto-1.3/input0");
        check(input_ident_format(&usb, buf, sizeof(buf)) &&
              strcmp(buf, "p:0003:045e:028e:usb-musb-hdrc.0.auto-1.3") == 0,
              "format USB -> p:0003:045e:028e:<phys stem>");
        check(input_ident_parse(buf, &back) && input_ident_equal(&back, &usb),
              "parse(format(USB)) equals the USB ident");
        InputIdent usb1 = ident(BUS_USB, 0x045e, 0x028e, "", "usb-musb-hdrc.0.auto-1.3/input1");
        check(input_ident_equal(&back, &usb1), "the parsed ident also equals the device's input1 node");
        check(input_ident_parse("P:0003:045E:028E:usb-musb-hdrc.0.auto-1.3", &back) == false,
              "parse: upper-case prefix is malformed");
        check(input_ident_parse("p:0003:045E:028E:usb-musb-hdrc.0.auto-1.3", &back) &&
              input_ident_equal(&back, &usb), "parse: upper-case hex digits accepted");
        /* A stem that would be stripped again, and a phys that is all suffix. */
        InputIdent odd = ident(BUS_USB, 1, 2, "", "x/input1/input2");
        check(input_ident_format(&odd, buf, sizeof(buf)) && input_ident_parse(buf, &back) &&
              input_ident_equal(&back, &odd), "round trip: phys whose stem ends /input<N>");
        InputIdent bare = ident(BUS_USB, 1, 2, "", "/input0");
        check(input_ident_format(&bare, buf, sizeof(buf)) && input_ident_parse(buf, &back) &&
              input_ident_equal(&back, &bare), "round trip: phys that is only /input<N>");
        /* Longest uniq and longest phys fit in a config value. */
        InputIdent lu = ident(BUS_BT, 0, 0, "0123456789abcdef0123456789abcde", "");
        InputIdent lp; memset(&lp, 0, sizeof(lp)); lp.bus = 0xffff; lp.vid = 0xabcd; lp.pid = 0x1234;
        memset(lp.phys, 'a', 46);
        check(input_ident_format(&lu, buf, sizeof(buf)) && input_ident_parse(buf, &back) &&
              input_ident_equal(&back, &lu), "round trip: 31-char uniq");
        check(input_ident_format(&lp, buf, sizeof(buf)) && strlen(buf) == 63 &&
              input_ident_parse(buf, &back) && input_ident_equal(&back, &lp),
              "round trip: 46-char phys, 63-char text");
        lp.phys[46] = 'a';
        check(!input_ident_format(&lp, buf, sizeof(buf)) && buf[0] == '\0',
              "64-char text does not fit in 64 bytes -> false, out = \"\"");
    }

    /* 17. format refuses what does not fit or has no text form. */
    {
        char buf[64];
        InputIdent bt = ident(BUS_BT, 0x045e, 0x02e0, "e4:17:d8:40:eb:ae", ADAPTER);
        strcpy(buf, "junk");
        check(!input_ident_format(&bt, buf, 24) && buf[0] == '\0',
              "format into 24 bytes (needs 25) -> false, out = \"\"");
        check(input_ident_format(&bt, buf, 25), "format into exactly 25 bytes fits");
        check(!input_ident_format(&bt, buf, 0) && !input_ident_format(NULL, buf, sizeof(buf)) &&
              !input_ident_format(&bt, NULL, 8), "format: n == 0 or NULL -> false");
        InputIdent nophys = ident(BUS_USB, 1, 2, "", "");
        check(!input_ident_format(&nophys, buf, sizeof(buf)), "format: no uniq and no phys -> false");
        InputIdent sp = ident(BUS_USB, 1, 2, "", "a b/input0");
        check(!input_ident_format(&sp, buf, sizeof(buf)), "format: a space in phys -> false");
        InputIdent full = bt;
        memset(full.uniq, 'a', sizeof(full.uniq));  /* 32 chars, no NUL: parse could not take it */
        check(!input_ident_format(&full, buf, sizeof(buf)), "format: unterminated 32-char uniq -> false");
    }

    /* 18. parse rejects malformed text. */
    {
        static const char *bad[] = {
            "x:0005:e4:17:d8:40:eb:ae",                 /* wrong prefix */
            "u:005:e4:17:d8:40:eb:ae",                  /* 3 hex digits */
            "u:00g5:e4:17",                             /* bad hex */
            "u:0005:",                                  /* empty uniq */
            "p:0003:045e:028e:",                        /* empty phys */
            "p:0003:045e:028e",                         /* missing phys field */
            "p:0003:045e-028e:usb-1",                   /* wrong separator */
            "u:0005:0123456789abcdef0123456789abcdef",  /* uniq of 32: overlong */
            "u:0005:e4 17",                             /* space */
            "",
            "u",
        };
        int rejected = 0, n = (int)(sizeof(bad) / sizeof(bad[0]));
        for (int i = 0; i < n; i++) {
            InputIdent o;
            memset(&o, 0x5a, sizeof(o));
            bool ok = input_ident_parse(bad[i], &o);
            if (ok) printf("        accepted: \"%s\"\n", bad[i]);
            else rejected++;
        }
        check(rejected == n, "parse rejects every malformed string");
        char longp[96];
        snprintf(longp, sizeof(longp), "p:0003:045e:028e:%064d", 0);
        InputIdent o;
        check(!input_ident_parse(longp, &o), "parse: phys of 64 chars is overlong");
        check(!input_ident_parse(NULL, &o) && !input_ident_parse("u:0005:ab", NULL),
              "parse: NULL -> false");
    }

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
