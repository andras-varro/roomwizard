/*
 * input_scan — evdev node discovery shared by every component that reads
 * USB keyboards, mice and pads.
 *
 * Two layers, deliberately separable:
 *   - PURE: input_classify() and input_select() take capability bits, a name
 *     and counts, and do no I/O, so a host test can drive every rule.
 *   - I/O:  input_read_caps() and input_scan() do the open()/ioctl() walk.
 *
 * The rules are the ones common/gamepad.c applies: a pad is ABS_X+ABS_Y plus
 * a gamepad/joystick button, a mouse is REL_X+REL_Y+BTN_LEFT, a keyboard has
 * at least 20 of the 26 letter keys, tested in that order; a name containing
 * "panjit" (any of the three case forms the vendor uses) is the built-in
 * touchscreen, which touch_input.c reads, and is never classified as anything.
 */
#ifndef INPUT_SCAN_H
#define INPUT_SCAN_H

#include <stdbool.h>
#include <linux/input.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nodes /dev/input/event0 .. event(INPUT_SCAN_MAX_NODES-1) are visited. */
#define INPUT_SCAN_MAX_NODES 32
#define INPUT_SCAN_NAME_LEN  128
#define INPUT_SCAN_PATH_LEN  32

typedef enum {
    INPUT_KIND_NONE = 0,   /* unclassified, or the touchscreen: never opened */
    INPUT_KIND_KEYBOARD,
    INPUT_KIND_MOUSE,
    INPUT_KIND_PAD,
    INPUT_KIND_COUNT
} InputKind;

#define INPUT_SCAN_LONG_BITS  (sizeof(unsigned long) * 8)
#define INPUT_SCAN_NLONGS(x)  ((((x) - 1) / INPUT_SCAN_LONG_BITS) + 1)

/* A node's capability bits, as the EVIOCGBIT ioctls report them. */
typedef struct {
    unsigned long ev[INPUT_SCAN_NLONGS(EV_MAX + 1)];
    unsigned long key[INPUT_SCAN_NLONGS(KEY_MAX + 1)];
    unsigned long abs[INPUT_SCAN_NLONGS(ABS_MAX + 1)];
    unsigned long rel[INPUT_SCAN_NLONGS(REL_MAX + 1)];
} InputCaps;

/* Set / test one bit in one of InputCaps' arrays (tests build fixtures with it). */
void input_caps_set(unsigned long *bits, int bit);
bool input_caps_test(const unsigned long *bits, int bit);

/*
 * Which button codes a pad's EV_KEY events carry.  Every consumer, and every
 * default and /etc/input_config.conf entry, speaks NATIVE: the codes xpad
 * reports for an Xbox 360 pad (A 0x130, B 0x131, X 0x133, Y 0x134, LB 0x136,
 * RB 0x137, Select 0x13a, Start 0x13b, LS 0x13d, RS 0x13e).
 *
 * SEQUENTIAL is a pad driven by hid-generic, which maps HID Button n to
 * BTN_GAMEPAD + (n-1) — so the codes follow the HID descriptor's button order
 * and not the button's meaning.  The 8BitDo Pro 2 in its Bluetooth X mode
 * (it enumerates as an Xbox One S, 045e:02e0, which kernel 4.14 has no
 * dedicated driver for) orders them A,B,X,Y,LB,RB,View,Menu,LS,RS: its key
 * bitmap is 0x130..0x139 plus KEY_MENU, and Select/Start arrive as 0x136/0x137,
 * the codes every consumer reads as LB/RB.
 *
 * Zero is NATIVE, so a zeroed InputNode (and a caller that never asks) is the
 * identity.
 */
typedef enum {
    INPUT_PAD_NATIVE = 0,
    INPUT_PAD_SEQUENTIAL,
} InputPadLayout;

/* One node found by input_scan(). fd is open O_RDONLY|O_NONBLOCK. */
typedef struct {
    char      path[INPUT_SCAN_PATH_LEN];
    int       fd;
    InputKind kind;
    bool      keys;   /* also carries a keyboard: a keyboard+touchpad combo is one
                         node, classified MOUSE, whose reader must forward its keys */
    InputPadLayout pad_layout;   /* input_pad_layout() of a PAD node; NATIVE otherwise */
    char      name[INPUT_SCAN_NAME_LEN];
} InputNode;

/* PURE. True for the built-in touchscreen's name. NULL is not a touchscreen. */
bool input_name_is_touchscreen(const char *name);

/* PURE. The keyboard rule alone: at least 20 of the 26 letter keys. */
bool input_caps_is_keyboard(const InputCaps *caps);

/* PURE. What a node is, from its capability bits and its name. */
InputKind input_classify(const InputCaps *caps, const char *name);

/*
 * PURE. Whether a node of `kind` is kept, given how many of each kind are
 * already held (`held[kind]`) and the per-kind limit (`cap[kind]`; 0 means
 * "that kind is not wanted"). Returns `kind`, or INPUT_KIND_NONE to close it.
 * Every node of a kind is kept up to its cap — not just the first.
 */
InputKind input_select(InputKind kind, const int held[INPUT_KIND_COUNT],
                       const int cap[INPUT_KIND_COUNT]);

/*
 * PURE. A pad's layout from its key capability bits: SEQUENTIAL when it has
 * BTN_TL and BTN_TR2 (HID Buttons 7 and 10) but neither BTN_SELECT nor
 * BTN_START, which a pad with that many buttons under the native layout always
 * has.  NATIVE for anything else, NULL included.
 */
InputPadLayout input_pad_layout(const InputCaps *caps);

/*
 * PURE. The NATIVE code for a raw EV_KEY `code` read from a pad of `layout`.
 * Translate at read time — and look a level read with EVIOCGKEY up by its raw
 * code, then translate — so every mapping stays in native codes.  Identity for
 * NATIVE, and for every code the layout does not move.
 */
int input_pad_key(InputPadLayout layout, int code);

/* PURE. True if `path` is already one of nodes[0..n-1] (a rescan skips it). */
bool input_scan_holds(const InputNode *nodes, int n, const char *path);

/* I/O. Fill `caps` from an open evdev fd. Returns 0, or -1 if EVIOCGBIT fails. */
int input_read_caps(int fd, InputCaps *caps);

/*
 * I/O. Walk /dev/input/event0..31 and APPEND every node that input_select()
 * keeps to nodes[], after the `n` entries already there, up to `max` entries
 * in total. Nodes already in nodes[0..n-1] are skipped without being opened,
 * and they count against the caps — so calling it again is a rescan that
 * adds only what is new. Every visited node that is not kept is closed.
 * Returns the new entry count.
 */
int input_scan(InputNode *nodes, int n, int max, const int cap[INPUT_KIND_COUNT]);

/*
 * What input_scan_with() adds to input_scan(). Every field may be NULL, and a
 * NULL opts is exactly input_scan(). ScummVM is the caller that uses them.
 */
typedef struct {
    /* NULL-terminated substrings: a node whose name contains any of them is
     * closed unclassified, as the touchscreen is. */
    const char *const *exclude_names;
    /* Called with open()'s errno for a node that exists but cannot be opened. */
    void (*open_failed)(const char *path, int err, void *ctx);
    void *ctx;
} InputScanOpts;

/* PURE. True if `name` contains any substring in the NULL-terminated `list`. */
bool input_name_excluded(const char *name, const char *const *list);

/* I/O. input_scan() with the extras in `opts`. */
int input_scan_with(InputNode *nodes, int n, int max, const int cap[INPUT_KIND_COUNT],
                    const InputScanOpts *opts);

/* Close nodes[i].fd and remove entry i, preserving order. Returns the new count. */
int input_scan_drop(InputNode *nodes, int n, int i);


/* ═══════════════════════════════════════════════════════════════════════════
 * /etc/input_config.conf — the one parser.  common/gamepad.c (every native
 * app), vnc_client and ScummVM each copy the fields they use out of an
 * InputConfig; none of them parses the file itself.
 *
 * Format: one `key=value` per line; blank lines and lines starting with '#'
 * are skipped, whitespace around key and value is trimmed (CR included, so a
 * file saved with CRLF endings parses), and a line with no '=', an empty key,
 * an empty value, an unknown key or an out-of-range number changes nothing.
 * Every field is named after its key.
 * ═══════════════════════════════════════════════════════════════════════════ */
#define INPUT_CONFIG_PATH "/etc/input_config.conf"

typedef struct {
    float mouse_sensitivity;     /* accepted 0.1 < v < 20 */
    float mouse_acceleration;    /* accepted 0.1 < v < 20 */
    int   mouse_low_threshold;   /* accepted 0 <= v < 100 */
    int   mouse_high_threshold;  /* accepted 1 <= v < 500 */
    int   gamepad_deadzone;      /* percent of half-range, accepted 0..100 */
    /* Native button codes (see InputPadLayout) and evdev axis codes; any
     * integer is accepted, as every earlier copy of this parser did. */
    int   gamepad_btn_jump, gamepad_btn_run, gamepad_btn_action;
    int   gamepad_btn_pause, gamepad_btn_back;
    int   gamepad_btn_north, gamepad_btn_tl, gamepad_btn_tr;  /* ScummVM only */
    int   gamepad_hat_x, gamepad_hat_y;
    int   gamepad_stick_lx, gamepad_stick_ly;
    int   gamepad_stick_rx, gamepad_stick_ry;                 /* native only */
} InputConfig;

/* PURE. The defaults every component starts from, before the file. */
void input_config_defaults(InputConfig *cfg);

/* PURE. Apply one line of the file to `cfg`. True only if a field was set. */
bool input_config_parse_line(InputConfig *cfg, const char *line);

/*
 * I/O. Apply every line of `path` on top of what `cfg` already holds — so seed
 * it with input_config_defaults() (or the caller's current values) first.
 * Logs nothing.  Returns the number of lines applied, or -1 if `path` could
 * not be opened (the caller logs "using defaults").
 */
int input_config_load(InputConfig *cfg, const char *path);

/* ═══════════════════════════════════════════════════════════════════════════
 * Hot-plug detection — the one gate every reader's rescan sits behind.
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * I/O. A fingerprint of /dev/input's event nodes: each eventN name and its
 * inode number.  Listing a directory opens no device, which is why it can run
 * every second where classifying (input_scan) cannot.  The inode is what makes
 * it enough on its own: devtmpfs gives every node it creates a fresh inode
 * number, so a device unplugged and replugged between two checks — same
 * eventN, same count — still changes the fingerprint.  0 if /dev/input cannot
 * be listed.
 */
unsigned long input_node_sig(void);

/* How often a reader compares input_node_sig() with its last value. */
#define INPUT_SIG_CHECK_MS 1000

/* One reader's view of the fingerprint.  Zero-initialised means "no check
 * yet": the first check is due at once, and only records a baseline. */
typedef struct {
    uint32_t      last_check_ms;
    unsigned long sig;
    bool          have_sig;    /* sig holds a baseline */
    bool          have_time;   /* last_check_ms holds a real time */
} InputSigGate;

/* PURE. True if the next check is due at `now_ms` (wrap-safe). */
bool input_sig_gate_due(const InputSigGate *g, uint32_t now_ms);

/*
 * PURE. Record the fingerprint `sig` observed at `now_ms`. True if it differs
 * from the baseline — the caller rescans; false for the first one ever fed.
 */
bool input_sig_gate_feed(InputSigGate *g, uint32_t now_ms, unsigned long sig);

/* PURE. Make `sig` the baseline without consuming a check (after a full scan). */
void input_sig_gate_baseline(InputSigGate *g, unsigned long sig);

/* I/O. If a check is due, take input_node_sig() and feed it. True: rescan. */
bool input_sig_gate_poll(InputSigGate *g, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_SCAN_H */
