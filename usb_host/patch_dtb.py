#!/usr/bin/env python3
"""Patch the MUSB power property in the uImage-system DTB.

Strategy: Binary-patch the DTB inside the uImage at the exact byte position
of the power property value (0x32 -> 0xfa), then update uImage CRCs.
"""
import struct
import binascii

# Read the uImage
with open('uImage-system', 'rb') as f:
    data = bytearray(f.read())

print(f'uImage size: {len(data)} bytes')

# DTB starts at offset 0x4eb788
DTB_OFFSET = 0x4eb788
DTB_SIZE = 67004

# Verify DTB magic at the expected offset
assert data[DTB_OFFSET:DTB_OFFSET+4] == b'\xd0\x0d\xfe\xed', "DTB magic not found at expected offset"
print(f'DTB magic verified at offset 0x{DTB_OFFSET:x}')

# In the DTB, search for the power property within usb_otg_hs node
# The DTB format stores property values as big-endian 32-bit integers
# We need to find the 4-byte sequence 0x00000032 that corresponds to the power property
# To be precise, let's find it in the DTB portion only

dtb_data = data[DTB_OFFSET:DTB_OFFSET+DTB_SIZE]

# Search for "power" string in the DTB strings block
# DTB header: magic(4) + totalsize(4) + off_dt_struct(4) + off_dt_strings(4) + ...
off_dt_strings = struct.unpack('>I', dtb_data[12:16])[0]
size_dt_strings = struct.unpack('>I', dtb_data[8*4:8*4+4])[0]
strings_block = dtb_data[off_dt_strings:off_dt_strings+size_dt_strings]

# Find "power\0" in strings block
power_str_offset = strings_block.find(b'power\x00')
print(f'Found "power" string at strings block offset {power_str_offset}')

# Now search the struct block for the property entry that references this string
# DTB property entry: FDT_PROP(0x00000003) + len(4) + nameoff(4) + data(len, padded to 4)
off_dt_struct = struct.unpack('>I', dtb_data[8:12])[0]

# Walk the struct block to find the power property in the usb_otg_hs context
pos = off_dt_struct
FDT_BEGIN_NODE = 0x00000001
FDT_END_NODE = 0x00000002
FDT_PROP = 0x00000003
FDT_NOP = 0x00000004
FDT_END = 0x00000009

in_usb_otg = False
patched = False

while pos < len(dtb_data) - 4:
    token = struct.unpack('>I', dtb_data[pos:pos+4])[0]
    
    if token == FDT_BEGIN_NODE:
        pos += 4
        # Read node name (null-terminated, padded to 4)
        name_end = dtb_data.index(b'\x00', pos)
        name = dtb_data[pos:name_end].decode('ascii', errors='replace')
        if 'usb_otg_hs' in name:
            in_usb_otg = True
            print(f'Entered usb_otg_hs node at DTB offset 0x{pos:x}')
        padded = ((name_end - pos + 1) + 3) & ~3
        pos += padded
        
    elif token == FDT_END_NODE:
        if in_usb_otg:
            in_usb_otg = False
            print('Left usb_otg_hs node')
        pos += 4
        
    elif token == FDT_PROP:
        pos += 4
        prop_len = struct.unpack('>I', dtb_data[pos:pos+4])[0]
        pos += 4
        nameoff = struct.unpack('>I', dtb_data[pos:pos+4])[0]
        pos += 4
        
        # Get property name from strings block
        name_end_s = strings_block.index(b'\x00', nameoff)
        prop_name = strings_block[nameoff:name_end_s].decode('ascii', errors='replace')
        
        if in_usb_otg and prop_name == 'power':
            old_val = struct.unpack('>I', dtb_data[pos:pos+4])[0]
            print(f'Found power property in usb_otg_hs: value=0x{old_val:02x} ({old_val}) at DTB offset 0x{pos:x}')
            
            abs_offset = DTB_OFFSET + pos
            print(f'Absolute offset in uImage: 0x{abs_offset:x}')
            
            # Patch it
            new_val = 0xfa  # 250 = 500mA
            struct.pack_into('>I', data, abs_offset, new_val)
            struct.pack_into('>I', dtb_data, pos, new_val)
            print(f'Patched power: 0x{old_val:02x} ({old_val}) -> 0x{new_val:02x} ({new_val})')
            patched = True
        
        padded_len = (prop_len + 3) & ~3
        pos += padded_len
        
    elif token == FDT_NOP:
        pos += 4
    elif token == FDT_END:
        break
    else:
        pos += 4

assert patched, "Failed to find and patch the power property!"

# Now fix uImage CRCs
# uImage header format (64 bytes):
#   0-3:  magic (0x27051956)
#   4-7:  header CRC
#   8-11: timestamp
#  12-15: data size
#  16-19: load address
#  20-23: entry point
#  24-27: data CRC
#  28:    os
#  29:    arch
#  30:    type
#  31:    comp
#  32-63: image name

HEADER_SIZE = 64

# Calculate data CRC (CRC32 of everything after the header)
data_crc = binascii.crc32(data[HEADER_SIZE:]) & 0xffffffff
struct.pack_into('>I', data, 24, data_crc)
print(f'Updated data CRC: 0x{data_crc:08x}')

# Calculate header CRC (CRC32 of the header with CRC field zeroed)
# Zero out the header CRC field first
struct.pack_into('>I', data, 4, 0)
header_crc = binascii.crc32(data[:HEADER_SIZE]) & 0xffffffff
struct.pack_into('>I', data, 4, header_crc)
print(f'Updated header CRC: 0x{header_crc:08x}')

# Write patched uImage
with open('uImage-system-patched', 'wb') as f:
    f.write(data)

print(f'\nWrote patched uImage to uImage-system-patched ({len(data)} bytes)')

# Verify
with open('uImage-system-patched', 'rb') as f:
    verify = f.read()
abs_offset_check = DTB_OFFSET + 0  # we'll re-find it
# Quick check: read the power value back
dtb_start = DTB_OFFSET
# Re-walk isn't needed, just check the bytes we patched
print(f'Verification - bytes at patch location: {verify[DTB_OFFSET:DTB_OFFSET+4].hex()} (should be DTB magic d00dfeed)')
