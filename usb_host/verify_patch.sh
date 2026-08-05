#!/bin/bash
cd /mnt/c/work/roomwizard/usb_host

echo "=== VERIFY PATCHED IMAGE ==="
mkimage -l uImage-system-patched 2>&1

echo "=== VERIFY ORIGINAL ==="
mkimage -l uImage-system 2>&1

echo "=== EXTRACT PATCHED DTB ==="
python3 -c "
import struct
data = open('uImage-system-patched', 'rb').read()
dtb_off = 0x4eb788
dtb_size = struct.unpack('>I', data[dtb_off+4:dtb_off+8])[0]
dtb = data[dtb_off:dtb_off+dtb_size]
open('patched.dtb', 'wb').write(dtb)
print('Extracted patched DTB: %d bytes' % dtb_size)
"

echo "=== DECOMPILE AND CHECK POWER ==="
dtc -I dtb -O dts patched.dtb 2>/dev/null | grep -B5 'power = '
