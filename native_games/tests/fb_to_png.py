#!/usr/bin/env python3
"""Convert RoomWizard framebuffer (RGB565) to PNG"""
import struct
from PIL import Image

# Read raw framebuffer
with open('framebuffer_capture.raw', 'rb') as f:
    data = f.read()

# RoomWizard: 800x480, RGB565 (little-endian)
width, height = 800, 480
pixels = []

for i in range(0, len(data), 2):
    if i + 1 >= len(data):
        break
    # Read 16-bit RGB565 value (little-endian)
    rgb565 = struct.unpack('<H', data[i:i+2])[0]
    
    # Extract RGB components
    r = ((rgb565 >> 11) & 0x1F) << 3  # 5 bits -> 8 bits
    g = ((rgb565 >> 5) & 0x3F) << 2   # 6 bits -> 8 bits
    b = (rgb565 & 0x1F) << 3          # 5 bits -> 8 bits
    
    pixels.append((r, g, b))

# Create image
img = Image.new('RGB', (width, height))
img.putdata(pixels[:width*height])
img.save('framebuffer_screenshot.png')
print(f"Screenshot saved: framebuffer_screenshot.png ({width}x{height})")
