/* Host-side regression for control_panel's USB bus list (control_panel/usb_bus.c).
 *
 * Runs on the DEV MACHINE with native gcc.  usb_bus_scan() takes its sysfs root
 * as a parameter, so this builds a fixture tree shaped like /sys/bus/usb/devices
 * on .188 with the Terminus hub and the C-Media dongle behind it (measured
 * 2026-09-27) and asserts what the tab would draw.
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/usb_bus_test \
 *       tests/usb_bus_test.c control_panel/usb_bus.c && ./build/usb_bus_test
 *
 * What it asserts, and why:
 *   A  the root hub (usb1) and every interface ("1-1:1.0") are skipped, so the
 *      list holds exactly the two devices, sorted by port;
 *   B  a class-00 composite takes its kind from its interfaces, and AUDIO beats
 *      the HID interface the dongle carries for its volume buttons — ranking by
 *      whichever interface readdir returns first or last would call a sound
 *      card "HID" in one order or the other — so the fixture has exactly two;
 *   C  the ALSA card comes from an interface's sound/cardN child;
 *   D  the hub is a hub, so peripherals counts 1 — the same "non-hub device"
 *      rule device-files/usb-host uses to decide a recover worked;
 *   E  a missing product string falls back to manufacturer, then "USB DEVICE";
 *   F  max truncates, and an unreadable root is 0 devices, not a crash.
 */
#include "control_panel/usb_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char R[256];

static void mk(const char *rel) {
    char p[512], *s;
    snprintf(p, sizeof(p), "%s/%s", R, rel);
    for (s = p + strlen(R) + 1; *s; s++)
        if (*s == '/') { *s = '\0'; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}

static void put(const char *dev, const char *attr, const char *val) {
    char p[512];
    mk(dev);
    snprintf(p, sizeof(p), "%s/%s/%s", R, dev, attr);
    FILE *f = fopen(p, "w");
    if (!f) { perror(p); exit(2); }
    fprintf(f, "%s\n", val);
    fclose(f);
}

int main(void) {
    snprintf(R, sizeof(R), "build/usb_bus_fixture_%d", (int)getpid());
    mkdir("build", 0755);
    mkdir(R, 0755);

    put("usb1", "idVendor", "1d6b");            put("usb1", "bDeviceClass", "09");
    put("1-0:1.0", "bInterfaceClass", "09");
    put("1-1", "idVendor", "1a40");             put("1-1", "idProduct", "0101");
    put("1-1", "bDeviceClass", "09");           put("1-1", "product", "USB 2.0 Hub");
    put("1-1:1.0", "bInterfaceClass", "09");
    put("1-1.1", "idVendor", "0d8c");           put("1-1.1", "idProduct", "0014");
    put("1-1.1", "bDeviceClass", "00");         put("1-1.1", "manufacturer", "C-Media Electronics Inc.");
    put("1-1.1", "product", "USB Audio Device");
    put("1-1.1:1.3", "bInterfaceClass", "03");
    put("1-1.1:1.0", "bInterfaceClass", "01");
    mk("1-1.1:1.0/sound/card1");

    UsbBusDev d[USB_BUS_MAX];
    int n = usb_bus_scan(R, d, USB_BUS_MAX);

    CHECK(n == 2, "A: %d devices, want 2 (usb1 and interfaces skipped)", n);
    if (n == 2) {
        CHECK(strcmp(d[0].port, "1-1") == 0 && strcmp(d[1].port, "1-1.1") == 0,
              "A: order %s,%s", d[0].port, d[1].port);
        CHECK(strcmp(d[0].kind, "HUB") == 0 && d[0].hub, "D: 1-1 kind %s", d[0].kind);
        CHECK(strcmp(d[1].kind, "AUDIO") == 0, "B: dongle kind %s, want AUDIO", d[1].kind);
        CHECK(d[1].card == 1, "C: dongle card %d, want 1", d[1].card);
        CHECK(d[0].card == -1, "C: hub card %d, want -1", d[0].card);
        CHECK(d[1].vid == 0x0d8c && d[1].pid == 0x0014, "A: %04x:%04x", d[1].vid, d[1].pid);
        CHECK(strcmp(d[1].name, "USB Audio Device") == 0, "E: name %s", d[1].name);
        CHECK(usb_bus_peripherals(d, n) == 1, "D: peripherals %d", usb_bus_peripherals(d, n));
    }

    put("1-1.2", "idVendor", "0b05");           put("1-1.2", "manufacturer", "ASUS");
    put("1-1.2:1.0", "bInterfaceClass", "e0");
    put("1-1.3", "idVendor", "1234");
    n = usb_bus_scan(R, d, USB_BUS_MAX);
    CHECK(n == 4, "E: %d devices, want 4", n);
    if (n == 4) {
        CHECK(strcmp(d[2].name, "ASUS") == 0 && strcmp(d[2].kind, "BT") == 0,
              "E: 1-1.2 %s/%s, want ASUS/BT", d[2].name, d[2].kind);
        CHECK(strcmp(d[3].name, "USB DEVICE") == 0 && strcmp(d[3].kind, "USB") == 0,
              "E: 1-1.3 %s/%s, want USB DEVICE/USB", d[3].name, d[3].kind);
    }

    CHECK(usb_bus_scan(R, d, 1) == 1, "F: max 1 not honoured");
    CHECK(usb_bus_scan("build/no_such_usb_root", d, USB_BUS_MAX) == 0, "F: missing root");

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", R);
    if (system(cmd) != 0) printf("warning: could not remove %s\n", R);

    printf("usb_bus_test: %s (%d failure(s))\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
