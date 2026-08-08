#!/bin/sh
### BEGIN INIT INFO
# Provides:          usb-host
# Required-Start:    $local_fs
# Required-Stop:
# Default-Start:     2 3 4 5
# Default-Stop:
# Description:       Enable USB host mode via MUSB /dev/mem patching
### END INIT INFO
#
# SysV init script for USB host mode on RoomWizard.
# Installed to /etc/init.d/usb-host, symlinked from /etc/rc5.d/S90usb-host.
#
# The kernel reloads with original (broken) struct values on each boot,
# so this script re-applies the runtime patch every time.

ENABLE_SCRIPT="/usr/local/bin/enable-usb-host.sh"

case "$1" in
  start)
    echo "Enabling USB host mode..."
    if [ -x "$ENABLE_SCRIPT" ]; then
        "$ENABLE_SCRIPT"
    else
        echo "ERROR: $ENABLE_SCRIPT not found or not executable"
        exit 1
    fi
    ;;
  stop)
    echo "USB host mode cannot be stopped at runtime."
    ;;
  status)
    if [ -d /sys/bus/usb/devices/usb1 ]; then
        echo "USB host mode is active."
        lsusb 2>/dev/null
    else
        echo "USB host mode is NOT active."
    fi
    ;;
  restart)
    # enable-usb-host.sh is idempotent, so restart just calls start
    "$0" start
    ;;
  *)
    echo "Usage: $0 {start|stop|status|restart}"
    exit 1
    ;;
esac
