/* Host-side regression: up to four pads plus keyboards, each in its own player
 * slot (P1..P4), and gamepad_poll() still the OR of all of them.
 *
 * gamepad.c used to open ONE pad and latch every pad, keyboard and hat event
 * into one held_latched[], so a second player could not exist.  It now asks
 * input_slots.c (the pure P1..P4 table) for a slot per device, keyed by the
 * device's EVIOCGID / EVIOCGUNIQ / EVIOCGPHYS identity, and keeps the latched
 * levels, axes and edges per slot.  This proves:
 *   - two pads' presses land in their own gamepad_player() slots;
 *   - gamepad_poll()'s merged state sees both (any-device);
 *   - a keyboard's two event nodes are one player;
 *   - a pad removed and re-added via rescan keeps its slot, even when it comes
 *     back later in the scan order, while another pad stays in its own and a
 *     new pad takes a slot nobody reserved.
 *
 * Build (this line is the CTEST_ROWS row in tests/run-all.sh):
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *       -o build/gamepad_slots_test tests/gamepad_slots_test.c \
 *       common/gamepad.c common/input_slots.c common/framebuffer.c \
 *       common/hardware.c common/config.c common/touch_input.c -lm
 *
 * The seams are gamepad_rescan_hold_test.c's: input_scan.c is compiled INTO
 * this file with its input_scan() renamed away, so this file's input_scan()
 * hands back the fake devices on fresh temp-file fds; and ioctl() is defined
 * here, so the identity ioctls a regular file cannot answer (EVIOCGID,
 * EVIOCGUNIQ, EVIOCGPHYS), plus EVIOCGKEY and EVIOCGABS, come from the fake
 * device.  No test-only code in gamepad.c.
 *
 * CONTROLS.  The merged rows ("any-device sees ...") pass against the one-pad
 * library for the first pad; every per-player "not held" row is the negative
 * control for the "held" row beside it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/input.h>
#include "gamepad.h"
#include "input_scan.h"

#define input_scan input_scan_real_unused
#include "input_scan.c"
#undef input_scan

static int fails = 0;

static void expect_bool(const char *what, bool got, bool want) {
    if (got != want) {
        printf("  FAIL %-58s got %s want %s\n", what,
               got ? "true" : "false", want ? "true" : "false");
        fails++;
    } else {
        printf("  ok   %-58s %s\n", what, got ? "true" : "false");
    }
}

static void expect_int(const char *what, int got, int want) {
    if (got != want) {
        printf("  FAIL %-58s got %d want %d\n", what, got, want);
        fails++;
    } else {
        printf("  ok   %-58s %d\n", what, got);
    }
}

/* ── The fake devices ──────────────────────────────────────────────────── */
typedef struct {
    InputKind     kind;
    const char   *name;
    uint16_t      bus, vid, pid;
    const char   *uniq, *phys;
    bool          present;
    unsigned long keys[INPUT_SCAN_NLONGS(KEY_MAX + 1)];  /* what EVIOCGKEY reports */
    int           fd;
} FakeDev;

/* Two identical wired pads on different ports, one Bluetooth pad (its phys is
 * the adapter's), and a keyboard with two event nodes. */
enum { PAD_A, PAD_B, PAD_C, KBD0, KBD1, DEV_COUNT };
static FakeDev g_dev[DEV_COUNT];
static int g_order[DEV_COUNT];   /* scan order: event-node order on a real box */

static void devs_reset(void) {
    memset(g_dev, 0, sizeof(g_dev));
    g_dev[PAD_A] = (FakeDev){ INPUT_KIND_PAD, "pad A", 3, 0x045e, 0x028e, "",
                              "usb-musb-hdrc.1.auto-1.1/input0", true, {0}, -1 };
    g_dev[PAD_B] = (FakeDev){ INPUT_KIND_PAD, "pad B", 3, 0x045e, 0x028e, "",
                              "usb-musb-hdrc.1.auto-1.2/input0", true, {0}, -1 };
    g_dev[PAD_C] = (FakeDev){ INPUT_KIND_PAD, "pad C", 5, 0x045e, 0x02e0,
                              "e4:17:d8:00:00:01", "00:1a:7d:da:71:13", false, {0}, -1 };
    g_dev[KBD0]  = (FakeDev){ INPUT_KIND_KEYBOARD, "kbd", 3, 0x046d, 0xc31c, "",
                              "usb-musb-hdrc.1.auto-1.3/input0", true, {0}, -1 };
    g_dev[KBD1]  = (FakeDev){ INPUT_KIND_KEYBOARD, "kbd consumer", 3, 0x046d, 0xc31c, "",
                              "usb-musb-hdrc.1.auto-1.3/input1", true, {0}, -1 };
    for (int i = 0; i < DEV_COUNT; i++) g_order[i] = i;
}

static FakeDev *dev_for_fd(int fd) {
    for (int i = 0; i < DEV_COUNT; i++)
        if (fd >= 0 && g_dev[i].fd == fd) return &g_dev[i];
    return NULL;
}

static int fake_fd(void) {
    char path[] = "/tmp/rw_slots_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); exit(2); }
    unlink(path);
    return fd;
}

/* ── Seam 1: input_scan() ─────────────────────────────────────────────── */
int input_scan(InputNode *nodes, int n, int max, const int cap[INPUT_KIND_COUNT]) {
    int held[INPUT_KIND_COUNT] = {0};
    for (int i = 0; i < DEV_COUNT; i++) g_dev[i].fd = -1;   /* closed by gamepad_close() */
    for (int o = 0; o < DEV_COUNT && n < max; o++) {
        int i = g_order[o];
        FakeDev *d = &g_dev[i];
        if (!d->present || held[d->kind] >= cap[d->kind]) continue;
        held[d->kind]++;
        d->fd = fake_fd();
        memset(&nodes[n], 0, sizeof(nodes[n]));
        snprintf(nodes[n].path, sizeof(nodes[n].path), "/dev/input/event%d", i);
        snprintf(nodes[n].name, sizeof(nodes[n].name), "%s", d->name);
        nodes[n].fd = d->fd;
        nodes[n].kind = d->kind;
        n++;
    }
    return n;
}

/* ── Seam 2: ioctl() ───────────────────────────────────────────────────── */
static int put_str(void *arg, size_t len, const char *s) {
    if (!s || !*s) { errno = ENOENT; return -1; }
    size_t n = strlen(s) + 1;
    if (n > len) n = len;
    memcpy(arg, s, n);
    return (int)n;
}

int ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    FakeDev *d = dev_for_fd(fd);
    if (!d) return (int)syscall(SYS_ioctl, fd, req, arg);

    if (req == EVIOCGID) {
        struct input_id *id = arg;
        memset(id, 0, sizeof(*id));
        id->bustype = d->bus; id->vendor = d->vid; id->product = d->pid;
        return 0;
    }
    if (_IOC_TYPE(req) == 'E' && _IOC_NR(req) == _IOC_NR(EVIOCGUNIQ(0)))
        return put_str(arg, _IOC_SIZE(req), d->uniq);
    if (_IOC_TYPE(req) == 'E' && _IOC_NR(req) == _IOC_NR(EVIOCGPHYS(0)))
        return put_str(arg, _IOC_SIZE(req), d->phys);
    if (_IOC_TYPE(req) == 'E' && _IOC_NR(req) == _IOC_NR(EVIOCGKEY(0))) {
        size_t len = _IOC_SIZE(req);
        if (len > sizeof(d->keys)) len = sizeof(d->keys);
        memset(arg, 0, _IOC_SIZE(req));
        memcpy(arg, d->keys, len);
        return (int)len;
    }
    if (_IOC_TYPE(req) == 'E' && _IOC_NR(req) >= 0x40 && _IOC_NR(req) < 0x40 + ABS_CNT) {
        int axis = (int)_IOC_NR(req) - 0x40;
        struct input_absinfo *ai = arg;
        memset(ai, 0, sizeof(*ai));
        if (axis == ABS_HAT0X || axis == ABS_HAT0Y) { ai->minimum = -1; ai->maximum = 1; }
        else { ai->minimum = -32768; ai->maximum = 32767; }
        return 0;
    }
    errno = ENOTTY;
    return -1;
}

/* ── Driving it ────────────────────────────────────────────────────────── */
static void deliver(FakeDev *d, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ev;
    if (d->fd < 0) return;   /* the library did not open it: a FAIL row, not an abort */
    memset(&ev, 0, sizeof(ev));
    ev.type = type; ev.code = code; ev.value = value;
    if (ftruncate(d->fd, 0) < 0 || lseek(d->fd, 0, SEEK_SET) < 0 ||
        write(d->fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev) ||
        lseek(d->fd, 0, SEEK_SET) < 0) { perror("deliver"); exit(2); }
}

static void press(FakeDev *d, int code, bool down) {
    if (down) input_caps_set(d->keys, code);
    else d->keys[code / INPUT_SCAN_LONG_BITS] &= ~(1UL << (code % INPUT_SCAN_LONG_BITS));
    deliver(d, EV_KEY, (uint16_t)code, down ? 1 : 0);
}

static bool p_held(const GamepadManager *gm, int slot, ButtonId b) {
    return gamepad_player(gm, slot)->buttons[b].held;
}

int main(void) {
    printf("gamepad slots: one player per device, any-device merge kept\n");
    GamepadManager gm; InputState st;
    devs_reset();
    gamepad_init(&gm);
    memset(&st, 0, sizeof(st));
    gamepad_poll(&gm, &st, 0, 0, false);

    printf("\n1. two pads and a two-node keyboard, each its own player\n");
    expect_bool("any-device: a pad connected", st.gamepad_connected, true);
    expect_int("present mask: P1 P2 P3", gamepad_player_mask(&gm), 0x7);
    expect_bool("P1 has a pad", gamepad_player(&gm, 0)->gamepad_connected, true);
    expect_bool("P2 has a pad", gamepad_player(&gm, 1)->gamepad_connected, true);
    expect_bool("P3 has the keyboard", gamepad_player(&gm, 2)->keyboard_connected, true);
    expect_bool("P3 has no pad", gamepad_player(&gm, 2)->gamepad_connected, false);
    expect_bool("P4 is empty", gamepad_player(&gm, 3)->gamepad_connected ||
                               gamepad_player(&gm, 3)->keyboard_connected, false);

    press(&g_dev[PAD_A], BTN_SOUTH, true);
    press(&g_dev[PAD_B], BTN_EAST, true);
    deliver(&g_dev[KBD1], EV_KEY, KEY_ENTER, 1);   /* the keyboard's SECOND node */
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("P1: pad A's JUMP held", p_held(&gm, 0, BTN_ID_JUMP), true);
    expect_bool("P1: pad A's JUMP pressed edge", gamepad_player(&gm, 0)->buttons[BTN_ID_JUMP].pressed, true);
    expect_bool("P1: pad B's RUN not held", p_held(&gm, 0, BTN_ID_RUN), false);
    expect_bool("P2: pad B's RUN held", p_held(&gm, 1, BTN_ID_RUN), true);
    expect_bool("P2: pad A's JUMP not held", p_held(&gm, 1, BTN_ID_JUMP), false);
    expect_bool("P3: the keyboard's ENTER is ACTION", p_held(&gm, 2, BTN_ID_ACTION), true);
    expect_bool("P1: the keyboard's ENTER not held", p_held(&gm, 0, BTN_ID_ACTION), false);
    expect_bool("any-device sees JUMP", st.buttons[BTN_ID_JUMP].held, true);
    expect_bool("any-device sees RUN", st.buttons[BTN_ID_RUN].held, true);
    expect_bool("any-device sees ACTION", st.buttons[BTN_ID_ACTION].held, true);
    expect_bool("any-device JUMP pressed edge", st.buttons[BTN_ID_JUMP].pressed, true);

    deliver(&g_dev[PAD_B], EV_ABS, ABS_X, -32768);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_int("P2: stick X", gamepad_player(&gm, 1)->axis_lx, -1000);
    expect_int("P1: stick X untouched", gamepad_player(&gm, 0)->axis_lx, 0);
    expect_bool("P2: stick LEFT", p_held(&gm, 1, BTN_ID_LEFT), true);
    expect_bool("P1: no stick LEFT", p_held(&gm, 0, BTN_ID_LEFT), false);
    expect_bool("any-device sees stick LEFT", st.buttons[BTN_ID_LEFT].held, true);
    expect_int("any-device stick X", st.axis_lx, -1000);
    expect_bool("P1: no repeated press edge", gamepad_player(&gm, 0)->buttons[BTN_ID_JUMP].pressed, false);

    printf("\n2. pad A unplugged: pad B stays P2 and keeps its held RUN\n");
    g_dev[PAD_A].present = false;
    gamepad_rescan(&gm);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_int("present mask: P2 P3", gamepad_player_mask(&gm), 0x6);
    expect_bool("P1 has no pad", gamepad_player(&gm, 0)->gamepad_connected, false);
    expect_bool("P1: JUMP released", p_held(&gm, 0, BTN_ID_JUMP), false);
    expect_bool("P1: JUMP release edge", gamepad_player(&gm, 0)->buttons[BTN_ID_JUMP].released, true);
    expect_bool("P2: RUN still held (seeded per slot)", p_held(&gm, 1, BTN_ID_RUN), true);
    expect_bool("P2: no spurious release edge", gamepad_player(&gm, 1)->buttons[BTN_ID_RUN].released, false);
    expect_bool("any-device: a pad still connected", st.gamepad_connected, true);

    printf("\n3. a new pad takes the free slot, not A's reserved one\n");
    g_dev[PAD_C].present = true;
    gamepad_rescan(&gm);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_int("present mask: P2 P3 P4", gamepad_player_mask(&gm), 0xe);
    expect_bool("P4 has pad C", gamepad_player(&gm, 3)->gamepad_connected, true);

    printf("\n4. pad A back, LAST in the scan order: P1 again\n");
    g_dev[PAD_A].present = true;
    int order[DEV_COUNT] = { PAD_C, KBD1, PAD_B, KBD0, PAD_A };
    memcpy(g_order, order, sizeof(order));
    gamepad_rescan(&gm);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_int("present mask: all four", gamepad_player_mask(&gm), 0xf);
    press(&g_dev[PAD_A], BTN_WEST, true);
    press(&g_dev[PAD_C], BTN_START, true);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("P1: pad A's ACTION", p_held(&gm, 0, BTN_ID_ACTION), true);
    expect_bool("P4: pad C's PAUSE", p_held(&gm, 3, BTN_ID_PAUSE), true);
    expect_bool("P4: pad A's ACTION not held", p_held(&gm, 3, BTN_ID_ACTION), false);
    expect_bool("P2: still pad B's RUN", p_held(&gm, 1, BTN_ID_RUN), true);
    expect_bool("P1: pad B's RUN not held", p_held(&gm, 0, BTN_ID_RUN), false);
    expect_bool("any-device sees PAUSE", st.buttons[BTN_ID_PAUSE].held, true);
    expect_bool("any-device sees ACTION", st.buttons[BTN_ID_ACTION].held, true);

    printf("\n5. out of range, and app exit clears the reservations\n");
    expect_bool("slot 4 is empty", gamepad_player(&gm, 4)->gamepad_connected, false);
    expect_bool("slot -1 is empty", gamepad_player(&gm, -1)->buttons[BTN_ID_RUN].held, false);
    gamepad_close(&gm);
    devs_reset();
    g_dev[PAD_A].present = false;            /* B first, with A's reservation gone */
    gamepad_init(&gm);
    gamepad_poll(&gm, &st, 0, 0, false);
    expect_bool("after re-init pad B is P1", gamepad_player(&gm, 0)->gamepad_connected, true);
    gamepad_close(&gm);

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
