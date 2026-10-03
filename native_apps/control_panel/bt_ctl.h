/* bt_ctl.h — the Bluetooth page's one channel to bluetoothd.
 *
 * bluetoothd is reachable only over D-Bus, and native_apps links neither
 * glib nor libdbus.  So the page runs ONE `bluetoothctl --agent DisplayYesNo`
 * child for as long as it is open, writes commands to its stdin and parses
 * its stdout line by line.  Measured on the device (BlueZ 5.66, which we
 * build ourselves, so the text format is pinned): a one-shot command costs
 * ~130 ms, piped stdin works with no tty, and the agent registers
 * ASYNCHRONOUSLY — a `default-agent` sent before "Agent registered" is
 * answered "No agent is registered" — so the parser sends it on that line.
 *
 * Two halves.  bt_parse_feed() is pure: bytes in, BtState updated, follow-up
 * commands appended to s->outq.  It is what the host test drives.  The
 * bt_ctl_* calls own the child process: fork/exec on pipes, non-blocking
 * reads polled from the page's input(), no thread (native_apps links no
 * pthread).
 *
 * The stream is noisy: ANSI colour, the prompt "[bluetooth]# " (or
 * "[<device name>]# " while one is connected) redrawn with spaces and
 * backspaces, and command echoes.  Agent questions arrive as a PROMPT with no
 * trailing newline, so the parser also inspects the pending partial line.
 */
#ifndef BT_CTL_H
#define BT_CTL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define BT_MAX_DEVICES 32          /* when full, the oldest entry neither paired nor seen gives way */
#define BT_ADDR_LEN    18          /* "AA:BB:CC:DD:EE:FF" + NUL */

typedef struct {
    char addr[BT_ADDR_LEN];
    char name[64];
    char icon[32];        /* BlueZ Icon: "audio-headset", "input-keyboard", "input-gaming", ... ("" if none) */
    bool paired;
    bool bonded;
    bool trusted;
    bool connected;
    bool seen;            /* reported an RSSI since the page opened (in range now) */
    bool info_loaded;     /* an `info` reply has been parsed for it */
} BtDevice;

/* An agent request waiting on the operator (or on the page). */
typedef enum {
    BT_PROMPT_NONE,
    BT_PROMPT_DISPLAY_PASSKEY,  /* "Passkey: NNNNNN" — type it on the keyboard, then Enter; no reply */
    BT_PROMPT_DISPLAY_PIN,      /* "PIN code: XXXX" — same, legacy pairing; no reply */
    BT_PROMPT_REQUEST_PIN,      /* "Enter PIN code:" — the page answers with a code it then displays */
    BT_PROMPT_CONFIRM,          /* "Confirm passkey NNNNNN (yes/no):" — numeric comparison */
    BT_PROMPT_AUTHORIZE         /* "Accept pairing (yes/no):" or "Authorize service ... (yes/no):" */
} BtPromptKind;

typedef struct {
    /* child process */
    pid_t pid;
    int   to_child;           /* write end of its stdin, -1 when closed */
    int   from_child;         /* read end of its stdout+stderr, non-blocking, -1 when closed */

    /* controller */
    bool agent_ready;         /* "Agent registered" seen and default-agent sent */
    bool powered;
    bool discovering;

    /* devices, in first-seen order */
    BtDevice dev[BT_MAX_DEVICES];
    int      ndev;

    /* pending agent request */
    BtPromptKind prompt;
    char         prompt_code[16];   /* the passkey / PIN to show, "" if none */
    /* Bumped each time a prompt BEGINS (a different kind or code, or the same
     * question again after it ended), so the page can tell a second identical
     * question in one burst from a redraw of the first. */
    unsigned     prompt_seq;
    /* The device the prompt is about, "" if not known.  No agent line carries
     * one (client/agent.c prints none of the device paths it receives), so it
     * is inferred: the device of the last "Attempting to pair with ADDR", else
     * the last device reported "[CHG] ... Connected: yes" — a remote-initiated
     * pairing connects first. */
    char         prompt_addr[BT_ADDR_LEN];

    /* "[CHG] Device ADDR Paired:" or "Bonded:" seen: pair_seq is bumped and
     * pair_ev_addr names the device.  A pairing the REMOTE started prints no
     * "Pairing successful", so this is the one ending both kinds share. */
    unsigned     pair_seq;
    char         pair_ev_addr[BT_ADDR_LEN];

    /* the outcome of the last command that reports one: "Pairing successful",
     * "Failed to pair: org.bluez.Error.AuthenticationFailed", "Connection
     * successful", "Failed to connect: ...", "... removed", ... */
    char result[96];
    bool result_ok;
    bool result_new;          /* set by the parser, cleared by the page once shown */

    bool changed;             /* anything visible changed; the page clears it after a repaint */

    /* parser internals */
    char info_addr[BT_ADDR_LEN];  /* the device an `info` reply's tab-indented fields belong to, "" outside one */
    bool info_ctrl;               /* inside a `show` reply ("Controller ADDR (public)"): fields are the adapter's */
    char pairing_addr[BT_ADDR_LEN];   /* "Attempting to pair with ADDR" until that pairing's result */
    char last_conn_addr[BT_ADDR_LEN]; /* the last device that went Connected: yes */
    char line[512];
    size_t linelen;
    char outq[1024];          /* commands queued by the parser or bt_ctl_send(), '\n'-terminated */
    size_t outqlen;
} BtState;

/* ---- pure parser (host-tested) ---- */

/* Zero the state; descriptors -1, pid 0. */
void bt_state_init(BtState *s);

/* Feed raw bytes as read from bluetoothctl.  Complete lines update state;
 * the trailing partial line is kept in s->line and also checked for an agent
 * prompt.  Queues, into s->outq: `default-agent` on "Agent registered";
 * `info <addr>` for each device first listed by a `devices` reply or a
 * [NEW] event. */
void bt_parse_feed(BtState *s, const char *buf, size_t n);

/* Append one command line ('\n' added) to s->outq.  False if it does not fit. */
bool bt_queue(BtState *s, const char *cmd);

/* The device with this address, or NULL. */
BtDevice *bt_find(BtState *s, const char *addr);

/* ---- the page's two lists (pure, host-tested) ---- */

#define BT_ROW_FOUND_HDR (-1)      /* a row holding the FOUND list's header */
#define BT_ROW_BLANK     (-2)      /* an empty row: FOUND starts on the next page */
#define BT_MAX_ROWS      (BT_MAX_DEVICES + 1)

/* Partition s->dev[] into the page's two lists, as indices into it.  mine:
 * the paired devices, the connected ones first, each group in first-seen
 * order — so a device moves up when it connects and back when it drops, and
 * nothing else reorders.  found: unpaired devices seen in range since the page
 * opened.  An unpaired device not in range is in neither — absent, not greyed.
 * Both arrays need BT_MAX_DEVICES slots. */
void bt_split_lists(const BtState *s, int *mine, int *nmine, int *found, int *nfound);

/* The rows the page shows, paged rows_fit at a time: mine, then found.  Each
 * row is a dev[] index or BT_ROW_*.  Where found starts mid-page a
 * BT_ROW_FOUND_HDR row precedes it; where it would start a page — or a header
 * would be a page's last row — it starts the next page instead (padded with
 * BT_ROW_BLANK), whose own header names it.  So no page begins with a header
 * row, and the list a page starts in is the list of its first row.  rows needs
 * BT_MAX_ROWS slots; returns how many it filled. */
int bt_list_rows(const int *mine, int nmine, const int *found, int nfound,
                 int rows_fit, int *rows);

/* ---- the child process (device only) ---- */

/* Start bluetoothctl and queue the opening commands (`show`, `devices`).
 * False if it could not be started; s is left closed. */
bool bt_ctl_start(BtState *s);

/* Read whatever is available, parse it, flush s->outq.  Never blocks.
 * Returns true if s->changed was set by this call.  Notices the child
 * exiting and closes down. */
bool bt_ctl_poll(BtState *s);

/* Queue a command and flush it (printf-style). */
void bt_ctl_send(BtState *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Is the child running? */
bool bt_ctl_running(const BtState *s);

/* `quit`, then SIGTERM if it lingers, then reap.  Safe on a closed state. */
void bt_ctl_stop(BtState *s);

#endif
