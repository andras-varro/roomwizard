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
 * (a device the table did not know is marked present).  Returns slot, or -1
 * when slot is out of range. */
int input_slots_set(InputSlotTable *t, const InputIdent *id, int slot);

/* The slot reserved for this device, present or not, or -1. */
int input_slots_find(const InputSlotTable *t, const InputIdent *id);

#endif /* INPUT_SLOTS_H */
