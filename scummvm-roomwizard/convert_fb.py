#!/usr/bin/env python3
import struct
from PIL import Image

# Read raw framebuffer (800x480 @ 32-bit ARGB)
with open('bezel_test.raw', 'rb') as f:
    data = f.read()

# Convert to RGB
width, height = 800, 480
pixels = []
for y in range(height):
    row = []
    for x in range(width):
        offset = (y * width + x) * 4
        # Read as 32-bit little-endian (BGRA on device)
        pixel = struct.unpack('<I', data[offset:offset+4])[0]
        r = (pixel >> 16) & 0xFF
        g = (pixel >> 8) & 0xFF
        b = pixel & 0xFF
        row.append((r, g, b))
    pixels.append(row)

# Create image
img = Image.new('RGB', (width, height))
for y in range(height):
    for x in range(width):
        img.putpixel((x, y), pixels[y][x])

img.save('bezel_test.png')
print("Converted bezel_test.raw to bezel_test.png")
