/*
 * vnc_key_sync — which keys a keyboard reader must release after SYN_DROPPED.
 *
 * Header-only and free of libvncclient, so a host test links it
 * (native_apps/tests/vnc_pad_test.c).  vnc_input.c keeps one VncKeyHeld per
 * keyboard node: the codes it has sent to the remote as down.  When the kernel
 * overflows the node's event ring (common/input_scan.h, InputSynDrop) a
 * release among the discarded events is gone, and the remote would keep the
 * key down for as long as the session lasts; EVIOCGKEY gives the kernel's own
 * level, and every tracked key it reports up is sent up.
 */
#ifndef VNC_KEY_SYNC_H
#define VNC_KEY_SYNC_H

#include <stdbool.h>
#include "../native_apps/common/input_scan.h"

/* Codes below this have a keysym slot (vnc_input.c's evdev_to_keysym[]). */
#define VNC_KEYS_TRACKED 256

typedef struct {
    unsigned long bits[INPUT_SCAN_NLONGS(VNC_KEYS_TRACKED)];
} VncKeyHeld;

static inline void vnc_keys_mark(VncKeyHeld *h, int code, bool down) {
    if (code < 0 || code >= VNC_KEYS_TRACKED) return;
    unsigned long m = 1UL << (code % INPUT_SCAN_LONG_BITS);
    if (down) h->bits[code / INPUT_SCAN_LONG_BITS] |= m;
    else      h->bits[code / INPUT_SCAN_LONG_BITS] &= ~m;
}

static inline bool vnc_keys_held(const VncKeyHeld *h, int code) {
    if (code < 0 || code >= VNC_KEYS_TRACKED) return false;
    return (h->bits[code / INPUT_SCAN_LONG_BITS] >> (code % INPUT_SCAN_LONG_BITS)) & 1UL;
}

/* PURE. The tracked keys the kernel's level (an EVIOCGKEY bitmap, KEY_MAX+1
 * bits) reports up: written to out[] (at most max codes, ascending), count
 * returned.  Each is also cleared from `h`, so a second call finds nothing.
 * A key the kernel holds down that was never tracked is left alone. */
static inline int vnc_keys_to_release(VncKeyHeld *h, const unsigned long *kernel,
                                      int *out, int max) {
    int n = 0;
    for (int code = 0; code < VNC_KEYS_TRACKED && n < max; code++) {
        if (!vnc_keys_held(h, code) || input_caps_test(kernel, code)) continue;
        out[n++] = code;
        vnc_keys_mark(h, code, false);
    }
    return n;
}

#endif /* VNC_KEY_SYNC_H */
