/* usb_bus.h — every enumerated USB device, read from sysfs.
 *
 * control_panel's USB tab used to list only what evdev could classify as a
 * keyboard, mouse or pad, so a sound card, a Bluetooth dongle, a hub or a stick
 * was invisible while it worked.  This lists the bus itself.  The root is a
 * parameter so a host test can point it at a fixture tree.
 */
#ifndef USB_BUS_H
#define USB_BUS_H

#include <stdbool.h>

#define USB_BUS_ROOT "/sys/bus/usb/devices"
#define USB_BUS_MAX  8

typedef struct {
    char     port[16];   /* sysfs name, "1-1.1" */
    char     name[48];   /* product string, else manufacturer, else "USB DEVICE" */
    char     kind[8];    /* HUB, AUDIO, BT, STORAGE, HID, VENDOR, USB */
    unsigned vid, pid;
    int      card;       /* ALSA card number, -1 if none */
    bool     hub;
} UsbBusDev;

/* Fills out[] sorted by port name, root hubs (usbN) and interfaces skipped.
 * Returns the count, at most max; 0 if root cannot be opened. */
int usb_bus_scan(const char *root, UsbBusDev *out, int max);

/* Devices that are not hubs — what "a device is attached" means. */
int usb_bus_peripherals(const UsbBusDev *d, int n);

#endif
