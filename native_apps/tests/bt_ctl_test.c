/* Host-side regression for the Bluetooth page's bluetoothctl parser
 * (control_panel/bt_ctl.c, bt_parse_feed() — the pure half; the process
 * half is linked but never called).
 *
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I. \
 *       -o build/bt_ctl_test tests/bt_ctl_test.c control_panel/bt_ctl.c && ./build/bt_ctl_test
 *
 * The inputs are the literal noisy byte strings bluetoothctl 5.66 writes to a
 * pipe: ANSI colour (with readline's \001/\002 markers where the 5.66 source
 * emits them from a printf), the "[bluetooth]# " prompt redrawn before an
 * asynchronous line with "\r<spaces>\r", backspaces in the echo of a command,
 * and agent questions that are PROMPTS with no trailing newline.
 *
 * What it asserts:
 *   A  the startup banner and a command echo add nothing;
 *   B  a `devices` reply adds each device once, keeps the dashed-address name
 *      of an unnamed one, and queues exactly one `info` per new device;
 *   C  tab-indented fields belong to the device whose "Device ADDR (public)"
 *      header opened the block, a prompt redraw does not close it, and any
 *      non-indented line does; Alias names a device only without a Name;
 *      "Device X not available" is a failed result, not a device;
 *   D  a `show` reply's fields set the controller's Powered/Discovering;
 *   E  [NEW]/[CHG]/[DEL] events, coloured and behind a prompt redraw
 *      (including a connected device's name as the prompt): seen only from an
 *      RSSI or a [NEW] while discovering; [DEL] removes and keeps order;
 *   F  `default-agent` is queued only by "Agent registered", once;
 *   G  DisplayPasskey / DisplayPinCode set a display prompt that survives the
 *      normal-prompt redraw after it and ends on the pairing result;
 *   H  RequestConfirmation / RequestAuthorization / AuthorizeService /
 *      RequestPinCode detected in the PARTIAL line, idempotent across redraws,
 *      ended by the echoed answer, a normal prompt, or "Request canceled";
 *   I  result lines set result/result_ok/result_new; the opening
 *      `pairable on` does not;
 *   K  prompt_seq rises once per question begun — not per redraw — so a second
 *      identical question in one read is distinguishable from the first;
 *   L  an asynchronous line bt_shell_printf() appends to an open question is
 *      parsed as a line of its own, the question staying open unless that line
 *      ends it; only yes/no/digits count as the echoed answer;
 *   M  prompt_addr comes from "Attempting to pair with" or the last device to
 *      connect; [CHG] Paired:/Bonded: bumps pair_seq and ends a displayed
 *      passkey/PIN for that device, or for an unknown one, never another's;
 *   N  a full device table evicts its oldest entry that is neither paired nor
 *      seen, and refuses when every entry is listed;
 *   J  every two-chunk split of a noisy stream, and a one-byte-at-a-time feed,
 *      give the same state as one feed — splitting lines and escapes.
 */
#include "control_panel/bt_ctl.h"

#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define A1 "90:7A:58:B9:C6:F5"
#define A2 "04:69:F8:B3:83:37"
#define A3 "11:22:33:44:55:66"
#define CA "A0:AD:9F:70:DD:CA"

/* What bluetoothctl prints, as captured.  P is the prompt; RD the redraw that
 * precedes an asynchronous line. */
#define P   "\x1b[0;94m[bluetooth]\x1b[0m# "
#define RD  P "\r                        \r"
#define NEW "[\x1b[0;92mNEW\x1b[0m] "
#define CHG "[\x1b[0;93mCHG\x1b[0m] "
#define DEL "[\x1b[0;91mDEL\x1b[0m] "
#define AGENT "\001\x1b[0;91m\002[agent]\001\x1b[0m\002 "
#define QPROMPT(q) "\r                        \r\x1b[1;39m[agent] " q " \x1b[0m"

static void feed(BtState *s, const char *str) { bt_parse_feed(s, str, strlen(str)); }

static void fresh(BtState *s) { bt_state_init(s); }

static void clear_outq(BtState *s) { s->outqlen = 0; }

static bool outq_is(BtState *s, const char *want)
{
    return s->outqlen == strlen(want) && memcmp(s->outq, want, s->outqlen) == 0;
}

/* ---- state comparison for J ---- */
static bool same_state(const BtState *a, const BtState *b)
{
    int i;
    if (a->ndev != b->ndev || a->powered != b->powered || a->discovering != b->discovering ||
        a->agent_ready != b->agent_ready || a->prompt != b->prompt ||
        strcmp(a->prompt_code, b->prompt_code) || strcmp(a->result, b->result) ||
        a->result_ok != b->result_ok || a->outqlen != b->outqlen ||
        memcmp(a->outq, b->outq, a->outqlen))
        return false;
    for (i = 0; i < a->ndev; i++) {
        const BtDevice *x = &a->dev[i], *y = &b->dev[i];
        if (strcmp(x->addr, y->addr) || strcmp(x->name, y->name) || strcmp(x->icon, y->icon) ||
            x->paired != y->paired || x->bonded != y->bonded || x->trusted != y->trusted ||
            x->connected != y->connected || x->seen != y->seen || x->info_loaded != y->info_loaded)
            return false;
    }
    return true;
}

static const char STARTUP[] =
    "Waiting to connect to bluetoothd...\r" P "            \b\b\b\b\b\b\b\b\b\b\bdevices\n";

static const char DEVICES[] =
    "Device " A1 " WI-C310\n"
    "Device " A2 " 04-69-F8-B3-83-37\n" P;

static const char INFO1[] =
    RD "Device " A1 " (public)\n"
    "\tName: WI-C310\n"
    "\tAlias: WI-C310 alias\n"
    "\tClass: 0x00240404\n"
    "\tIcon: audio-headset\n"
    "\tPaired: yes\n"
    "\tBonded: yes\n"
    RD "\tTrusted: yes\n"                      /* a redraw mid-block must not close it */
    "\tBlocked: no\n"
    "\tConnected: no\n"
    "\tUUID: Audio Sink                (0000110b-0000-1000-8000-00805f9b34fb)\n"
    "\tManufacturerData Key: 0x0006\n"
    "\tManufacturerData Value:\n"
    "  01 09 20 02 4a 3b                                ..J;            \n" P;

static const char INFO2[] =
    "Device " A2 " (random)\n"
    "\tAlias: Pad Two\n"
    "\tPaired: no\n"
    "\tConnected: yes\n"
    "Discovery started\n"
    "\tPaired: yes\n" P;                       /* after the block: belongs to nobody */

static const char EVENTS[] =
    RD CHG "Controller " CA " Discovering: yes\n"
    RD NEW "Device " A3 " Xbox Wireless Controller\n"
    RD CHG "Device " A2 " RSSI: -68\n"
    "\x1b[0;94m[WI-C310]\x1b[0m# \r                        \r" CHG "Device " A1 " Connected: yes\n"
    RD CHG "Device " A3 " Name: Xbox Pad\n"
    RD CHG "Device " A3 " Paired: yes\n" P;

int main(void)
{
    static BtState s, t;
    int i;

    /* A */
    fresh(&s);
    feed(&s, STARTUP);
    CHECK(s.ndev == 0, "A: startup added %d device(s)", s.ndev);
    CHECK(!s.result_new && s.outqlen == 0, "A: startup set a result or queued '%.*s'", (int)s.outqlen, s.outq);
    feed(&s, "scan on\n" P "show\n" P "info " A1 "\n" P);
    CHECK(s.ndev == 0 && !s.result_new, "A: command echoes were parsed as output");

    /* B */
    feed(&s, DEVICES);
    CHECK(s.ndev == 2, "B: devices reply gave %d devices, want 2", s.ndev);
    CHECK(s.ndev == 2 && !strcmp(s.dev[0].addr, A1) && !strcmp(s.dev[0].name, "WI-C310"),
          "B: dev0 '%s' '%s'", s.dev[0].addr, s.dev[0].name);
    CHECK(s.ndev == 2 && !strcmp(s.dev[1].name, "04-69-F8-B3-83-37"), "B: unnamed device name '%s'", s.dev[1].name);
    CHECK(outq_is(&s, "info " A1 "\ninfo " A2 "\n"), "B: outq '%.*s'", (int)s.outqlen, s.outq);
    CHECK(s.changed, "B: changed not set");
    feed(&s, DEVICES);
    CHECK(s.ndev == 2 && outq_is(&s, "info " A1 "\ninfo " A2 "\n"), "B: a repeated reply re-added or re-queued");
    CHECK(!s.dev[0].info_loaded, "B: info_loaded before any info reply");

    /* C */
    feed(&s, INFO1);
    BtDevice *d1 = bt_find(&s, A1), *d2 = bt_find(&s, A2);
    CHECK(d1 && d1->info_loaded, "C: info header did not mark info_loaded");
    CHECK(d1 && !strcmp(d1->name, "WI-C310"), "C: Alias overrode Name: '%s'", d1 ? d1->name : "");
    CHECK(d1 && !strcmp(d1->icon, "audio-headset"), "C: icon '%s'", d1 ? d1->icon : "");
    CHECK(d1 && d1->paired && d1->bonded, "C: Paired/Bonded not applied");
    CHECK(d1 && d1->trusted, "C: a field after a mid-block prompt redraw was lost");
    CHECK(d1 && !d1->connected, "C: Connected: no read as yes");
    CHECK(d2 && !d2->paired && !d2->connected && !d2->info_loaded, "C: dev1 polluted by dev0's block");
    feed(&s, INFO2);
    d2 = bt_find(&s, A2);
    CHECK(d2 && !strcmp(d2->name, "Pad Two"), "C: Alias did not replace the dashed name: '%s'", d2 ? d2->name : "");
    CHECK(d2 && d2->connected, "C: dev1 Connected: yes not applied");
    CHECK(d2 && !d2->paired, "C: an indented line after the block closed was attributed");
    CHECK(d1 && d1->paired, "C: dev0 Paired lost");
    CHECK(s.info_addr[0] == '\0', "C: block still open after a non-indented line: '%s'", s.info_addr);
    s.result_new = false;
    feed(&s, "Device 01:02:03:04:05:06 not available\n" P);
    CHECK(s.ndev == 2, "C: 'not available' added a device (%d)", s.ndev);
    CHECK(s.result_new && !s.result_ok, "C: 'not available' not a failed result");

    /* D */
    feed(&s, RD "Controller " CA " (public)\n\tName: rw09\n\tAlias: rw09\n\tPowered: yes\n"
             "\tDiscovering: no\n\tPairable: yes\n" P);
    CHECK(s.powered && !s.discovering, "D: show reply powered=%d discovering=%d", s.powered, s.discovering);
    CHECK(!strcmp(bt_find(&s, A1)->name, "WI-C310") && !strcmp(bt_find(&s, A2)->name, "Pad Two"),
          "D: controller fields leaked into a device");
    feed(&s, RD CHG "Controller " CA " Powered: no\n" P);
    CHECK(!s.powered, "D: [CHG] Powered: no ignored");
    feed(&s, RD CHG "Controller " CA " Powered: yes\n" P);

    /* E */
    clear_outq(&s);
    feed(&s, RD NEW "Device 77:77:77:77:77:77 Early\n" P);   /* before discovery: not seen */
    BtDevice *e = bt_find(&s, "77:77:77:77:77:77");
    CHECK(e && !e->seen, "E: [NEW] while not discovering marked seen");
    feed(&s, EVENTS);
    CHECK(s.discovering, "E: Discovering: yes ignored");
    BtDevice *d3 = bt_find(&s, A3);
    CHECK(d3 != NULL, "E: [NEW] device not added");
    CHECK(d3 && d3->seen, "E: [NEW] while discovering not seen");
    CHECK(d3 && !strcmp(d3->name, "Xbox Pad"), "E: Name CHG gave '%s'", d3 ? d3->name : "");
    CHECK(d3 && d3->paired, "E: Paired CHG ignored");
    CHECK(bt_find(&s, A2)->seen, "E: RSSI CHG did not mark seen");
    CHECK(!bt_find(&s, A1)->seen, "E: device with no RSSI marked seen");
    CHECK(bt_find(&s, A1)->connected, "E: CHG behind a device-name prompt lost");
    CHECK(outq_is(&s, "info 77:77:77:77:77:77\ninfo " A3 "\n"), "E: [NEW] info queue '%.*s'", (int)s.outqlen, s.outq);
    CHECK(s.ndev == 4, "E: ndev %d want 4", s.ndev);
    feed(&s, RD DEL "Device 77:77:77:77:77:77 Early\n" P);
    CHECK(s.ndev == 3 && !bt_find(&s, "77:77:77:77:77:77"), "E: [DEL] did not remove");
    CHECK(s.ndev == 3 && !strcmp(s.dev[0].addr, A1) && !strcmp(s.dev[1].addr, A2) && !strcmp(s.dev[2].addr, A3),
          "E: [DEL] broke first-seen order");

    /* F */
    fresh(&s);
    feed(&s, "No agent is registered\n" P);
    CHECK(s.outqlen == 0 && !s.agent_ready, "F: default-agent queued before registration");
    feed(&s, RD "Agent registered\n" P);
    CHECK(s.agent_ready && outq_is(&s, "default-agent\n"), "F: 'Agent registered' queued '%.*s'", (int)s.outqlen, s.outq);
    feed(&s, "Agent registered\n" P "Default agent request successful\n" P);
    CHECK(outq_is(&s, "default-agent\n"), "F: default-agent queued twice");

    /* G */
    fresh(&s);
    feed(&s, RD "Attempting to pair with " A1 "\n" P);
    feed(&s, "\r                        \r" AGENT "Passkey: \001\x1b[1;30m\002\001\x1b[1;37m\002" "123456\n\001\x1b[0m\002" P);
    CHECK(s.prompt == BT_PROMPT_DISPLAY_PASSKEY && !strcmp(s.prompt_code, "123456"),
          "G: passkey prompt %d '%s'", s.prompt, s.prompt_code);
    feed(&s, "\r                        \r" AGENT "Passkey: \001\x1b[1;30m\002" "12\001\x1b[1;37m\002" "3456\n\001\x1b[0m\002" P);
    CHECK(s.prompt == BT_PROMPT_DISPLAY_PASSKEY && !strcmp(s.prompt_code, "123456"),
          "G: passkey with entered digits '%s'", s.prompt_code);
    feed(&s, RD CHG "Device " A1 " RSSI: -60\n" P);
    CHECK(s.prompt == BT_PROMPT_DISPLAY_PASSKEY, "G: display passkey cleared by a normal prompt redraw");
    feed(&s, RD "Pairing successful\n" P);
    CHECK(s.prompt == BT_PROMPT_NONE && s.prompt_code[0] == '\0', "G: pairing result did not clear the prompt");
    CHECK(s.result_new && s.result_ok && !strcmp(s.result, "Pairing successful"), "G: result '%s'", s.result);
    feed(&s, "\r                        \r" AGENT "PIN code: 0000\n" P);
    CHECK(s.prompt == BT_PROMPT_DISPLAY_PIN && !strcmp(s.prompt_code, "0000"), "G: PIN '%s'", s.prompt_code);
    feed(&s, "Failed to pair: org.bluez.Error.AuthenticationFailed\n" P);
    CHECK(s.prompt == BT_PROMPT_NONE && !s.result_ok &&
          !strcmp(s.result, "Failed to pair: org.bluez.Error.AuthenticationFailed"), "G: failed pair '%s'", s.result);

    /* H */
    fresh(&s);
    feed(&s, DEVICES);
    feed(&s, RD "Request confirmation\n" P QPROMPT("Confirm passkey 012345 (yes/no):"));
    CHECK(s.prompt == BT_PROMPT_CONFIRM && !strcmp(s.prompt_code, "012345"),
          "H: no-newline confirm prompt %d '%s'", s.prompt, s.prompt_code);
    s.changed = false;
    feed(&s, "\r                        \r" CHG "Device " A2 " RSSI: -60\n" QPROMPT("Confirm passkey 012345 (yes/no):"));
    CHECK(s.prompt == BT_PROMPT_CONFIRM && !strcmp(s.prompt_code, "012345"), "H: confirm lost across a redraw");
    feed(&s, QPROMPT("Confirm passkey 012345 (yes/no):"));
    CHECK(s.prompt == BT_PROMPT_CONFIRM, "H: a repeated redraw changed the prompt");
    feed(&s, "yes\n");                 /* no prompt after it: the answer alone must end it */
    CHECK(s.prompt == BT_PROMPT_NONE, "H: the echoed answer did not end the prompt (%d)", s.prompt);
    feed(&s, P);
    feed(&s, RD "Request authorization\n" P QPROMPT("Accept pairing (yes/no):"));
    CHECK(s.prompt == BT_PROMPT_AUTHORIZE, "H: Accept pairing not AUTHORIZE (%d)", s.prompt);
    feed(&s, "\r                        \r" P);
    CHECK(s.prompt == BT_PROMPT_NONE, "H: a normal prompt did not replace the question");
    feed(&s, RD "Authorize service\n" P QPROMPT("Authorize service 0000110d-0000-1000-8000-00805f9b34fb (yes/no):"));
    CHECK(s.prompt == BT_PROMPT_AUTHORIZE, "H: Authorize service not AUTHORIZE (%d)", s.prompt);
    feed(&s, "\r                        \rRequest canceled\n" P);
    CHECK(s.prompt == BT_PROMPT_NONE, "H: 'Request canceled' did not clear");
    feed(&s, RD "Request PIN code\n" P QPROMPT("Enter PIN code:"));
    CHECK(s.prompt == BT_PROMPT_REQUEST_PIN, "H: Enter PIN code not REQUEST_PIN (%d)", s.prompt);
    feed(&s, "1234\n");
    CHECK(s.prompt == BT_PROMPT_NONE, "H: answered PIN request still open");
    feed(&s, P);

    /* I */
    {
        static const struct { const char *line; bool ok; } R[] = {
            { "Connection successful", true },
            { "Failed to connect: org.bluez.Error.Failed br-connection-create-socket", false },
            { "Successful disconnected", true },
            { "Device has been removed", true },
            { "Changing " A1 " trust succeeded", true },
            { "Failed to start discovery: org.bluez.Error.NotReady", false },
        };
        for (i = 0; i < (int)(sizeof(R) / sizeof(R[0])); i++) {
            char buf[256];
            fresh(&s);
            snprintf(buf, sizeof(buf), RD "%s\n" P, R[i].line);
            feed(&s, buf);
            CHECK(s.result_new && s.result_ok == R[i].ok && !strcmp(s.result, R[i].line),
                  "I: '%s' -> new=%d ok=%d '%s'", R[i].line, s.result_new, s.result_ok, s.result);
        }
        fresh(&s);
        feed(&s, "Changing pairable on succeeded\n" P "Discovery started\n" P "Attempting to connect to " A1 "\n" P);
        CHECK(!s.result_new, "I: a non-result line set result '%s'", s.result);
    }

    /* K */
    {
        unsigned q1, q2;
        fresh(&s);
        feed(&s, DEVICES);
        feed(&s, RD "Request authorization\n" P QPROMPT("Accept pairing (yes/no):"));
        q1 = s.prompt_seq;
        CHECK(s.prompt == BT_PROMPT_AUTHORIZE && q1 > 0, "K: a new question did not bump prompt_seq (%u)", q1);
        feed(&s, QPROMPT("Accept pairing (yes/no):"));
        CHECK(s.prompt_seq == q1, "K: a redraw bumped prompt_seq %u -> %u", q1, s.prompt_seq);
        /* answered, and the same question again, in one read */
        feed(&s, "yes\n" P RD "Request authorization\n" P QPROMPT("Accept pairing (yes/no):"));
        CHECK(s.prompt == BT_PROMPT_AUTHORIZE && s.prompt_seq == q1 + 1,
              "K: a second identical question in one burst: prompt %d seq %u want %u", s.prompt, s.prompt_seq, q1 + 1);
        q2 = s.prompt_seq;
        /* two reads: a question is answered only after a read has shown it */
        feed(&s, "no\n" P RD "Request confirmation\n" P QPROMPT("Confirm passkey 111111 (yes/no):"));
        feed(&s, "yes\n" P RD "Request confirmation\n" P QPROMPT("Confirm passkey 111111 (yes/no):"));
        CHECK(s.prompt == BT_PROMPT_CONFIRM && s.prompt_seq == q2 + 2,
              "K: two confirms of the same code: seq %u want %u", s.prompt_seq, q2 + 2);
    }

    /* L */
    {
        static const char LTAIL[] =
            RD "Request confirmation\n" P QPROMPT("Confirm passkey 123456 (yes/no):")
            "Failed to pair: org.bluez.Error.AuthenticationTimeout\n" P;
        fresh(&s);
        feed(&s, DEVICES);
        feed(&s, LTAIL);
        CHECK(s.result_new && !s.result_ok &&
              !strcmp(s.result, "Failed to pair: org.bluez.Error.AuthenticationTimeout"),
              "L: a result appended to an open question was lost: new=%d '%s'", s.result_new, s.result);
        CHECK(s.prompt == BT_PROMPT_NONE, "L: the appended failure did not end the question (%d)", s.prompt);
        int bad = 0, n = (int)strlen(LTAIL);
        for (i = 1; i < n; i++) {
            fresh(&t);
            feed(&t, DEVICES);
            bt_parse_feed(&t, LTAIL, (size_t)i);
            bt_parse_feed(&t, LTAIL + i, (size_t)(n - i));
            if (!same_state(&s, &t)) bad++;
        }
        CHECK(bad == 0, "L: %d of %d splits of the appended-result stream diverged", bad, n - 1);

        unsigned q;
        fresh(&s);
        feed(&s, DEVICES);
        feed(&s, RD "Request confirmation\n" P QPROMPT("Confirm passkey 123456 (yes/no):"));
        q = s.prompt_seq;
        feed(&s, CHG "Device " A2 " Connected: yes\n" QPROMPT("Confirm passkey 123456 (yes/no):"));
        CHECK(bt_find(&s, A2) && bt_find(&s, A2)->connected, "L: an event appended to an open question was lost");
        CHECK(s.prompt == BT_PROMPT_CONFIRM && s.prompt_seq == q,
              "L: an appended event ended or restarted the question: prompt %d seq %u was %u", s.prompt, s.prompt_seq, q);
        feed(&s, "123456\n" P);
        CHECK(s.prompt == BT_PROMPT_NONE, "L: an all-digit answer did not end the question");
    }

    /* M */
    fresh(&s);
    feed(&s, DEVICES);
    feed(&s, RD CHG "Device " A2 " Connected: yes\n" P);          /* remote-initiated: it connects first */
    feed(&s, "\r                        \r" AGENT "Passkey: \001\x1b[1;37m\002" "654321\n\001\x1b[0m\002" P);
    CHECK(s.prompt == BT_PROMPT_DISPLAY_PASSKEY && !strcmp(s.prompt_addr, A2),
          "M: remote passkey prompt %d addr '%s' want " A2, s.prompt, s.prompt_addr);
    feed(&s, RD CHG "Device " A1 " Bonded: yes\n" P);
    CHECK(s.prompt == BT_PROMPT_DISPLAY_PASSKEY, "M: another device's Bonded ended the prompt");
    feed(&s, RD CHG "Device " A2 " Paired: yes\n" P);
    CHECK(s.prompt == BT_PROMPT_NONE && s.prompt_addr[0] == '\0',
          "M: Paired: yes for the prompt's device did not end it (%d)", s.prompt);
    CHECK(s.pair_seq == 2 && !strcmp(s.pair_ev_addr, A2), "M: pair event seq %u addr '%s'", s.pair_seq, s.pair_ev_addr);
    fresh(&s);
    feed(&s, DEVICES);
    feed(&s, RD "Attempting to pair with " A1 "\n" P);
    feed(&s, RD CHG "Device " A2 " Connected: yes\n" P);
    feed(&s, RD "Request confirmation\n" P QPROMPT("Confirm passkey 222222 (yes/no):"));
    CHECK(s.prompt == BT_PROMPT_CONFIRM && !strcmp(s.prompt_addr, A1),
          "M: a local pair's prompt addr '%s' want " A1, s.prompt_addr);
    fresh(&s);
    feed(&s, "\r                        \r" AGENT "PIN code: 0000\n" P);
    CHECK(s.prompt == BT_PROMPT_DISPLAY_PIN && s.prompt_addr[0] == '\0', "M: unknown-device PIN addr '%s'", s.prompt_addr);
    feed(&s, RD CHG "Device " A3 " Paired: yes\n" P);
    CHECK(s.prompt == BT_PROMPT_NONE, "M: Paired: yes did not end a prompt for an unknown device");

    /* N */
    {
        char buf[96];
        fresh(&s);
        for (i = 0; i < BT_MAX_DEVICES; i++) {
            snprintf(buf, sizeof(buf), "Device 00:00:00:00:00:%02X Dev%d\n", i, i);
            feed(&s, buf);
        }
        feed(&s, RD CHG "Device 00:00:00:00:00:00 Paired: yes\n" RD CHG "Device 00:00:00:00:00:01 RSSI: -50\n" P);
        feed(&s, "Device 00:00:00:00:00:F0 Newcomer\n" P);
        CHECK(s.ndev == BT_MAX_DEVICES && bt_find(&s, "00:00:00:00:00:F0"), "N: a full table refused a new device");
        CHECK(bt_find(&s, "00:00:00:00:00:00") && bt_find(&s, "00:00:00:00:00:01"),
              "N: a paired or seen device was evicted");
        CHECK(!bt_find(&s, "00:00:00:00:00:02"), "N: the oldest unlisted device was not the one evicted");
        for (i = 0; i < s.ndev; i++) s.dev[i].seen = true;
        feed(&s, "Device 00:00:00:00:00:F1 Late\n" P);
        CHECK(s.ndev == BT_MAX_DEVICES && !bt_find(&s, "00:00:00:00:00:F1"), "N: a listed device was evicted");
    }

    /* J */
    {
        static char all[4096];
        int n;
        snprintf(all, sizeof(all), "%s%s%s%s%s%s%s", STARTUP, DEVICES, INFO1, INFO2, EVENTS,
                 "Agent registered\n" RD DEL "Device " A3 " Xbox Pad\n",
                 RD "Request confirmation\n" P QPROMPT("Confirm passkey 999999 (yes/no):"));
        n = (int)strlen(all);
        fresh(&s);
        bt_parse_feed(&s, all, (size_t)n);
        CHECK(s.ndev == 2 && s.prompt == BT_PROMPT_CONFIRM && s.agent_ready,
              "J: reference feed ndev=%d prompt=%d", s.ndev, s.prompt);
        int bad = 0;
        for (i = 1; i < n; i++) {
            fresh(&t);
            bt_parse_feed(&t, all, (size_t)i);
            bt_parse_feed(&t, all + i, (size_t)(n - i));
            if (!same_state(&s, &t)) { if (!bad) printf("FAIL: J: first divergent split at byte %d\n", i); bad++; }
        }
        CHECK(bad == 0, "J: %d of %d two-chunk splits diverged", bad, n - 1);
        fresh(&t);
        for (i = 0; i < n; i++) bt_parse_feed(&t, all + i, 1);
        CHECK(same_state(&s, &t), "J: one-byte-at-a-time feed diverged");
    }

    printf("bt_ctl_test: %s (%d checks, %d failure(s))\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
