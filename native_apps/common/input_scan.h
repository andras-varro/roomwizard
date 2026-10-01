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

/* One node found by input_scan(). fd is open O_RDONLY|O_NONBLOCK. */
typedef struct {
    char      path[INPUT_SCAN_PATH_LEN];
    int       fd;
    InputKind kind;
    bool      keys;   /* also carries a keyboard: a keyboard+touchpad combo is one
                         node, classified MOUSE, whose reader must forward its keys */
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

#endif /* INPUT_SCAN_H */
