/* usb_bus.c — see usb_bus.h. */
#include "usb_bus.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_attr(const char *root, const char *dev, const char *attr,
                      char *buf, size_t n) {
    char p[256];
    snprintf(p, sizeof(p), "%s/%s/%s", root, dev, attr);
    FILE *f = fopen(p, "r");
    if (!f) return false;
    bool ok = fgets(buf, (int)n, f) != NULL;
    fclose(f);
    if (!ok) return false;
    buf[strcspn(buf, "\r\n")] = '\0';
    return buf[0] != '\0';
}

static unsigned read_hex(const char *root, const char *dev, const char *attr) {
    char b[16];
    return read_attr(root, dev, attr, b, sizeof(b)) ? (unsigned)strtoul(b, NULL, 16) : 0;
}

/* The ALSA card an interface registered, from its sound/cardN child. */
static int iface_card(const char *root, const char *iface) {
    char p[256];
    snprintf(p, sizeof(p), "%s/%s/sound", root, iface);
    DIR *d = opendir(p);
    if (!d) return -1;
    int card = -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (strncmp(e->d_name, "card", 4) == 0 && e->d_name[4] >= '0' && e->d_name[4] <= '9') {
            card = atoi(e->d_name + 4);
            break;
        }
    closedir(d);
    return card;
}

/* Rank a USB class code; the device shows the most telling one it carries.
 * A composite audio dongle also exposes HID for its volume buttons, and a
 * class-00 device only says what it is through its interfaces. */
static int class_rank(unsigned c, const char **kind) {
    switch (c) {
    case 0x09: *kind = "HUB";     return 6;
    case 0x01: *kind = "AUDIO";   return 5;
    case 0xe0: *kind = "BT";      return 4;
    case 0x08: *kind = "STORAGE"; return 3;
    case 0x03: *kind = "HID";     return 2;
    case 0xff: *kind = "VENDOR";  return 1;
    default:   return 0;
    }
}

static int by_port(const void *a, const void *b) {
    return strcmp(((const UsbBusDev *)a)->port, ((const UsbBusDev *)b)->port);
}

int usb_bus_scan(const char *root, UsbBusDev *out, int max) {
    DIR *d = opendir(root);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        const char *nm = e->d_name;
        /* Devices are "<bus>-<port>[.<port>]"; usbN is a root hub and
         * anything with a ':' is an interface. */
        if (nm[0] < '0' || nm[0] > '9' || strchr(nm, ':') || !strchr(nm, '-'))
            continue;
        size_t len = strlen(nm);
        if (len >= sizeof(out[n].port)) continue;

        UsbBusDev *u = &out[n];
        memset(u, 0, sizeof(*u));
        memcpy(u->port, nm, len + 1);
        nm = u->port;
        u->vid  = read_hex(root, nm, "idVendor");
        u->pid  = read_hex(root, nm, "idProduct");
        u->card = -1;
        if (!read_attr(root, nm, "product", u->name, sizeof(u->name)) &&
            !read_attr(root, nm, "manufacturer", u->name, sizeof(u->name)))
            snprintf(u->name, sizeof(u->name), "USB DEVICE");

        const char *kind = "USB";
        int best = class_rank(read_hex(root, nm, "bDeviceClass"), &kind);
        char prefix[sizeof(u->port) + 1];
        snprintf(prefix, sizeof(prefix), "%s:", u->port);
        DIR *d2 = opendir(root);
        struct dirent *i;
        while (d2 && (i = readdir(d2)) != NULL) {
            if (strncmp(i->d_name, prefix, strlen(prefix)) != 0) continue;
            const char *k = kind;
            int r = class_rank(read_hex(root, i->d_name, "bInterfaceClass"), &k);
            if (r > best) { best = r; kind = k; }
            if (u->card < 0) u->card = iface_card(root, i->d_name);
        }
        if (d2) closedir(d2);
        snprintf(u->kind, sizeof(u->kind), "%s", kind);
        u->hub = strcmp(kind, "HUB") == 0;
        n++;
    }
    closedir(d);
    qsort(out, (size_t)n, sizeof(*out), by_port);
    return n;
}

int usb_bus_peripherals(const UsbBusDev *d, int n) {
    int c = 0;
    for (int i = 0; i < n; i++)
        if (!d[i].hub) c++;
    return c;
}

/* After a VBUS_ERROR musb drops to b_idle and no disconnect is ever reported:
 * sysfs goes on listing every device while none of them answers (measured on
 * .188), so the count alone calls that port live. */
bool usb_port_dead(const UsbBusDev *d, int n, const char *mode_path) {
    if (usb_bus_peripherals(d, n) == 0) return true;
    char m[16] = "";
    FILE *f = fopen(mode_path, "r");
    if (!f) return false;
    bool ok = fgets(m, sizeof(m), f) != NULL;
    fclose(f);
    return ok && m[0] == 'b' && m[1] == '_';
}

/* Field by field, not memcmp: scan() zeroes each entry, but a comparison that
 * leans on padding would turn a compiler change into a repaint every tick. */
bool usb_bus_same(const UsbBusDev *a, int na, const UsbBusDev *b, int nb) {
    if (na != nb) return false;
    for (int i = 0; i < na; i++)
        if (strcmp(a[i].port, b[i].port) != 0 || strcmp(a[i].name, b[i].name) != 0 ||
            strcmp(a[i].kind, b[i].kind) != 0 || a[i].vid != b[i].vid ||
            a[i].pid != b[i].pid || a[i].card != b[i].card)
            return false;
    return true;
}
