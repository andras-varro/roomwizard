#!/bin/bash
# musb-power.sh - Give the USB host port a 500 mA VBUS budget instead of the vendor's 100 mA
#
# Usage: musb-power.sh <dtb>      (edits <dtb> in place; run by kernel/build-image.sh)
#
# omap2430.c reads the usb_otg_hs node's `power` into pdata->power at probe, and
# musb_host_setup() sets hcd->power_budget = 2 * power mA (musb_host.c), which the hub driver
# takes as the root hub's bus_mA and so as every child's budget. The vendor DTB says 0x32
# (100 mA), so a device asking for more is refused a configuration ("insufficient available
# bus power"). 0xfa is 500 mA, the USB 2.0 port maximum and the driver's own default when the
# property is absent; the vendor-kernel byte patch set the same value and was run that way.
#
# `mode` stays 3 (dual role): our image's musb patches are written against the ID-pin path
# that dual role runs, and the byte patch's optional mode change was never the tested state.
#
# A script over fdtput rather than a DTS patch, for the reason panel-dpi.sh gives.

set -euo pipefail

DTB="${1:?usage: musb-power.sh <dtb>}"
[ -f "$DTB" ] || { echo "ERROR: ${DTB} not found." >&2; exit 1; }

NODE="/ocp@68000000/usb_otg_hs@480ab000"
VENDOR=0x32
WANTED=0xfa

[ "$(fdtget -t s "$DTB" "$NODE" compatible 2>/dev/null)" = "ti,omap3-musb" ] \
    || { echo "ERROR: ${DTB}: ${NODE} is missing or not ti,omap3-musb." >&2; exit 1; }
old=$(fdtget -t u "$DTB" "$NODE" power)
if [ "$old" -ne $((VENDOR)) ]; then
    echo "ERROR: ${DTB}: ${NODE} power is ${old}, expected $((VENDOR)); run this on an unmodified vendor DTB." >&2
    exit 1
fi

fdtput -t u "$DTB" "$NODE" power $((WANTED))
[ "$(fdtget -t u "$DTB" "$NODE" power)" -eq $((WANTED)) ] \
    || { echo "ERROR: ${DTB}: power did not read back as $((WANTED))." >&2; exit 1; }

echo "  ${DTB}: ${NODE} power $((VENDOR)) -> $((WANTED)) ($((WANTED * 2)) mA)"
