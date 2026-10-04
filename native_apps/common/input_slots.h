/*
 * input_slots — which player (P1..P4) each input device is.
 *
 * Pure: no I/O, no malloc, no evdev.  The caller reads each device's identity
 * (EVIOCGID's bus/vendor/product, EVIOCGUNIQ, EVIOCGPHYS) and hands it in; the
 * table answers with a slot.  A keyboard and a pad are both devices here.
 *
 * The rules:
 *
 *   - One device per slot.  A device keeps its slot for as long as the table
 *     lives, present or not.
 *   - A device that disconnects leaves its slot EMPTY BUT RESERVED for its
 *     identity.  When the same device comes back it gets that slot again,
 *     whatever event node it lands on and wherever it falls in the scan order.
 *   - A new device takes the first slot that holds no reservation.  When every
 *     slot is reserved it evicts the OLDEST reservation whose device is absent
 *     — oldest by when the device was last assigned, so a pad seen in every
 *     scan is never the one evicted.  When all four slots hold present devices
 *     it gets no slot (-1).
 *   - Reservations end on input_slots_clear() (the app exits) and on
 *     input_slots_set() (the Input page puts a device in a chosen slot, which
 *     evicts whatever was reserved there).
 *
 * Pinning — an operator's choice, which assign never overrides:
 *
 *   - input_slots_pin() puts a device in a slot and marks it PINNED.  A device
 *     lives in one slot, so any other slot holding the same identity, pinned
 *     or not, is released (a pad pinned to P1 and then pinned to P3 leaves P1
 *     free and unpinned); the target's previous holder is evicted, no swap.
 *     Presence carries over from the slot the device held; a device the table
 *     did not know is ABSENT (a pin loaded from the config at start-up names a
 *     pad that may not be plugged in).
 *   - assign never evicts a pinned slot — neither to reserve a new device nor
 *     as the oldest absent reservation — so an absent pinned device's slot is
 *     not handed out, and when the pinned device appears it lands there.
 *   - input_slots_unpin() drops the pin; a device holding the slot keeps it as
 *     an ordinary reservation.  input_slots_clear() drops every pin (gamepad
 *     reloads them from the config after init).
 *   - input_slots_set() never creates a pin, and the slot it writes is unpinned
 *     afterwards: setting a device into a pinned slot evicts the holder AND
 *     ends that pin, and setting a pinned device elsewhere ends its pin too.
 *
 * Text form — an identity as one config value (CONFIG_VAL_LEN, config.h):
 *
 *   u:<bus>:<uniq>                     when uniq is non-empty
 *   p:<bus>:<vid>:<pid>:<phys stem>    otherwise
 *
 * Numbers are exactly 4 hex digits (written lowercase, read in either case).
 * The phys stem is phys without its trailing "/input<N>" — the part the
 * identity rule compares — unless the stem is empty or would itself be
 * stripped again, in which case the whole phys is written.  The free-form
 * field must be non-empty and hold no space or control character, because
 * config.c trims values and a newline ends a line; an identity breaking that
 * has no text form.  parse(format(x)) is input_ident_equal to x for every x
 * whose format succeeds.
 *
 * Identity — when two readings are the same device:
 *
 *   - Both uniq non-empty: bus + uniq.  A Bluetooth pad's uniq is its own MAC;
 *     its phys is the ADAPTER's MAC, the same for every BT pad, so phys is
 *     ignored then.
 *   - Otherwise: bus, vid, pid and phys with a trailing "/input<N>" removed,
 *     so a keyboard's several event nodes (usb-musb-hdrc.1.auto-1.2/input0,
 *     .../input1) are one device, while two identical wired pads on different
 *     ports differ by phys.
 *
 * A scan is: input_slots_begin_scan(), then input_slots_assign() for every node
 * found.  assign is idempotent within a scan, so a device's second node gets
 * the slot its first node got.  Test: tests/input_slots_test.c.
 */
#ifndef INPUT_SLOTS_H
#define INPUT_SLOTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_SLOTS 4

typedef struct {
    uint16_t bus, vid, pid;
    char uniq[32];     /* EVIOCGUNIQ; "" when the device reports none */
    char phys[64];     /* EVIOCGPHYS */
} InputIdent;

typedef struct {
    InputIdent ident;
    bool     reserved; /* holds an identity, present or not */
    bool     present;  /* assigned during the current scan */
    bool     pinned;   /* operator's choice: assign never evicts it */
    uint32_t seen;     /* table clock at the last assign: eviction order */
} InputSlot;

typedef struct {
    InputSlot slot[INPUT_SLOTS];
    uint32_t  clock;
} InputSlotTable;

/* The identity rule above.  NULL never equals anything. */
bool input_ident_equal(const InputIdent *a, const InputIdent *b);

/* Forget every device and reservation. */
void input_slots_clear(InputSlotTable *t);

/* Mark every slot absent; reservations are kept. */
void input_slots_begin_scan(InputSlotTable *t);

/* The slot 0..INPUT_SLOTS-1 for this device, marking it present, or -1 when
 * all slots hold present devices. */
int input_slots_assign(InputSlotTable *t, const InputIdent *id);

/* Put the device in `slot`: its old slot (if any) is freed, any other
 * reservation in `slot` is evicted, presence carries over from the old slot
 * (a device the table did not know is marked present).  `slot` is unpinned
 * afterwards (see Pinning).  Returns slot, or -1 when slot is out of range. */
int input_slots_set(InputSlotTable *t, const InputIdent *id, int slot);

/* Put the device in `slot`, reserved and pinned (see Pinning).  Returns slot,
 * or -1 when slot is out of range or a pointer is NULL. */
int input_slots_pin(InputSlotTable *t, const InputIdent *id, int slot);

/* `slot` stops being pinned; its holder, if any, stays.  Out of range: no-op. */
void input_slots_unpin(InputSlotTable *t, int slot);

/* The slot reserved for this device, present or not, or -1. */
int input_slots_find(const InputSlotTable *t, const InputIdent *id);

/* The text form above into out[n].  False, with out = "" when n > 0, when it
 * does not fit, the identity has no text form, or a pointer is NULL. */
bool input_ident_format(const InputIdent *id, char *out, size_t n);

/* The inverse: false on anything malformed (wrong prefix, not 4 hex digits,
 * empty or overlong uniq/phys, a space or control character); *out is then
 * zeroed (when non-NULL). */
bool input_ident_parse(const char *s, InputIdent *out);

#endif /* INPUT_SLOTS_H */
