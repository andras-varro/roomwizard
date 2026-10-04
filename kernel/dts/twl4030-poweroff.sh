#!/bin/bash
# twl4030-poweroff.sh - Let `poweroff` ask the TWL4030 PMIC to switch the board off
#
# Usage: twl4030-poweroff.sh <dtb>      (edits <dtb> in place; run by kernel/build-image.sh)
#
# The vendor twl@48 node has no power child, so drivers/mfd/twl4030-power.c (built in,
# CONFIG_TWL4030_POWER=y) binds nothing, pm_power_off stays NULL, and kernel/reboot.c turns a
# power-off into a halt. This adds the child the upstream binding describes
# (Documentation/devicetree/bindings/mfd/twl4030-power.txt): twl-core's of_platform_populate
# instantiates it, and ti,system-power-controller makes the probe set pm_power_off to
# twl4030_power_off, which writes DEVOFF to P1_SW_EVENTS.
#
# The generic compatible "ti,twl4030-power" carries no match data, so the probe loads no
# sequencing scripts and configures no resources; its only other write is setting SEQ_OFFSYNC
# in CFG_P123_TRANSITION if that bit is clear. The -idle/-reset variants rewrite the PMIC's
# sleep and warm-reset scripts, which this board has never run with.
#
# A script over fdtput rather than a DTS patch, for the reason panel-dpi.sh gives.

set -euo pipefail

DTB="${1:?usage: twl4030-poweroff.sh <dtb>}"
[ -f "$DTB" ] || { echo "ERROR: ${DTB} not found." >&2; exit 1; }

TWL="/ocp/i2c@48070000/twl@48"
NODE="${TWL}/power"

[ "$(fdtget -t s "$DTB" "$TWL" compatible 2>/dev/null)" = "ti,twl4030" ] \
    || { echo "ERROR: ${DTB}: ${TWL} is missing or not ti,twl4030." >&2; exit 1; }
if fdtget -l "$DTB" "$TWL" | grep -qx power; then
    echo "ERROR: ${DTB} already has ${NODE}; run this on an unmodified vendor DTB." >&2
    exit 1
fi

fdtput -c "$DTB" "$NODE"
fdtput -t s "$DTB" "$NODE" compatible ti,twl4030-power
fdtput "$DTB" "$NODE" ti,system-power-controller

echo "  ${DTB}: ${NODE} -> ti,twl4030-power, ti,system-power-controller"
