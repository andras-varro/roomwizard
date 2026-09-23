#!/bin/bash
# panel-dpi.sh - Rebind the vendor DTB's LCD node to the stock omapfb panel-dpi driver
#
# Usage: panel-dpi.sh <dtb>      (edits <dtb> in place; run by kernel/build-image.sh)
#
# The vendor /display node is compatible "sharp,lq070y3lg4a", a driver the vanilla tree
# does not have, so nothing binds the panel. Stock omapfb panel-dpi claims it once the node
# says "panel-dpi" and carries a panel-timing subnode. The timing and polarity values below
# are the vendor driver's (SYSTEM_ANALYSIS.md#32-display, "Panel timings"). panel-dpi drives
# only enable-gpios, so the LVDS and backlight lines become gpio hogs, and the LCD pinmux the
# vendor driver requested moves to its pin controller's own default state.
#
# It is a script over fdtput rather than a patch to a decompiled DTS so that the repo carries
# only our values: the vendor device tree is not ours to publish. Every node and phandle is
# looked up in <dtb>, and a missing one fails the run.

set -euo pipefail

DTB="${1:?usage: panel-dpi.sh <dtb>}"
[ -f "$DTB" ] || { echo "ERROR: ${DTB} not found." >&2; exit 1; }

GPIO1="/ocp/gpio@48310000"
PINMUX="/ocp/pinmux@480025d8"
LCD_PINS="${PINMUX}/pinmux_lcd_pins"
PANEL="/display"

# gpio1 line numbers (bank-relative): power-down/enable, LVDS transmitter, backlight.
GPIO_ENABLE=14
GPIO_LVDS=15
GPIO_BACKLIGHT=19

get() { fdtget -t u "$DTB" "$@" || { echo "ERROR: ${DTB}: no $*" >&2; exit 1; }; }

[ "$(fdtget -t s "$DTB" "$PANEL" compatible)" != "panel-dpi" ] \
    || { echo "ERROR: ${DTB} is already panel-dpi; run this on an unmodified vendor DTB." >&2; exit 1; }
GPIO1_PH=$(get "$GPIO1" phandle)
LCD_PINS_PH=$(get "$LCD_PINS" phandle)

# The line numbers above must be the ones the vendor node already names, or this is another board.
check() {
    [ "$(get "$PANEL" "$1")" = "$GPIO1_PH $2 0" ] \
        || { echo "ERROR: ${PANEL} $1 is not gpio1 line $2 on this DTB." >&2; exit 1; }
}
check pwrdn-gpios "$GPIO_ENABLE"
check lvds-gpios "$GPIO_LVDS"
check backlight-gpios "$GPIO_BACKLIGHT"
[ "$(get "$PANEL" pinctrl-0)" = "$LCD_PINS_PH" ] \
    || { echo "ERROR: ${PANEL} pinctrl-0 is not ${LCD_PINS}." >&2; exit 1; }

# LVDS transmitter and backlight: held high from gpio1's probe.
hog() {
    fdtput -c "$DTB" "${GPIO1}/$1"
    fdtput "$DTB" "${GPIO1}/$1" gpio-hog
    fdtput -t u "$DTB" "${GPIO1}/$1" gpios "$2" 0
    fdtput "$DTB" "${GPIO1}/$1" output-high
    fdtput -t s "$DTB" "${GPIO1}/$1" line-name "$3"
}
hog lcd-lvds-hog "$GPIO_LVDS" lcd-lvds
hog lcd-backlight-hog "$GPIO_BACKLIGHT" lcd-backlight

# The LCD pinmux, as a hog of the pin controller instead of a request by the panel driver.
fdtput -t s "$DTB" "$PINMUX" pinctrl-names default
fdtput -t u "$DTB" "$PINMUX" pinctrl-0 "$LCD_PINS_PH"

fdtput -t s "$DTB" "$PANEL" compatible panel-dpi
fdtput -d "$DTB" "$PANEL" pinctrl-names
fdtput -d "$DTB" "$PANEL" pinctrl-0
fdtput -t u "$DTB" "$PANEL" enable-gpios "$GPIO1_PH" "$GPIO_ENABLE" 0

T="${PANEL}/panel-timing"
fdtput -c "$DTB" "$T"
fdtput -t u "$DTB" "$T" clock-frequency 33230770
fdtput -t u "$DTB" "$T" hactive 800
fdtput -t u "$DTB" "$T" vactive 480
fdtput -t u "$DTB" "$T" hsync-len 128
fdtput -t u "$DTB" "$T" hfront-porch 40
fdtput -t u "$DTB" "$T" hback-porch 88
fdtput -t u "$DTB" "$T" vsync-len 9
fdtput -t u "$DTB" "$T" vfront-porch 9
fdtput -t u "$DTB" "$T" vback-porch 26
fdtput -t u "$DTB" "$T" hsync-active 0
fdtput -t u "$DTB" "$T" vsync-active 0
fdtput -t u "$DTB" "$T" de-active 1
fdtput -t u "$DTB" "$T" pixelclk-active 1
# The vendor samples sync on the falling edge. Vanilla 4.14 omapfb parses this and then
# overwrites it; kernel/patches/omapdss-honour-syncclk-active.patch makes it take effect.
fdtput -t u "$DTB" "$T" syncclk-active 0

echo "  ${DTB}: ${PANEL} -> panel-dpi (gpio1 phandle ${GPIO1_PH}, lcd pinmux phandle ${LCD_PINS_PH})"
