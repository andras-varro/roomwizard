#!/bin/sh
echo "=== Input devices ==="
ls -la /dev/input/
echo ""
echo "=== Event device names ==="
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    p="/sys/class/input/event${i}"
    if [ -d "$p" ]; then
        n=$(cat "$p/device/name" 2>/dev/null)
        echo "event${i}: ${n}"
    fi
done
echo ""
echo "=== Capabilities per event device ==="
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    p="/sys/class/input/event${i}"
    if [ -d "$p" ]; then
        n=$(cat "$p/device/name" 2>/dev/null)
        echo "--- event${i}: ${n} ---"
        echo "  EV:  $(cat $p/device/capabilities/ev 2>/dev/null)"
        echo "  KEY: $(cat $p/device/capabilities/key 2>/dev/null)"
        echo "  ABS: $(cat $p/device/capabilities/abs 2>/dev/null)"
        echo "  REL: $(cat $p/device/capabilities/rel 2>/dev/null)"
    fi
done
echo ""
echo "=== usb_test binary ==="
ls -lh /opt/games/usb_test
