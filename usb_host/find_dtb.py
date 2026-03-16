#!/usr/bin/env python3
import struct

data = open('uImage-system', 'rb').read()
magic = b'\xd0\x0d\xfe\xed'
offset = 0
count = 0
while True:
    pos = data.find(magic, offset)
    if pos == -1:
        break
    size = struct.unpack('>I', data[pos+4:pos+8])[0]
    print(f'DTB found at offset 0x{pos:x} ({pos}), size={size} bytes')
    count += 1
    # Only extract the real DTB (reasonable size)
    if size < 1000000 and size > 1000:
        with open('original.dtb', 'wb') as f:
            f.write(data[pos:pos+size])
        print(f'  -> Extracted {size} bytes to original.dtb')
        # Save offset info for later reassembly
        with open('dtb_info.txt', 'w') as f:
            f.write(f'dtb_offset={pos}\n')
            f.write(f'dtb_size={size}\n')
            f.write(f'uimage_size={len(data)}\n')
    offset = pos + 1
print(f'Total DTBs found: {count}')
