#!/usr/bin/env python3
"""
Convert 32-bit BGRA framebuffer dump to PNG
Usage: python3 fb_to_png_32bit.py input.raw output.png
"""

import sys
import struct
from PIL import Image

def convert_fb_to_png(input_file, output_file, width=800, height=480):
    """Convert 32-bit BGRA framebuffer to PNG"""
    
    # Read raw framebuffer data
    with open(input_file, 'rb') as f:
        data = f.read()
    
    # Expected size for 32-bit (4 bytes per pixel)
    expected_size = width * height * 4
    actual_size = len(data)
    
    print(f"Expected size: {expected_size} bytes")
    print(f"Actual size: {actual_size} bytes")
    
    if actual_size < expected_size:
        print(f"Warning: File is smaller than expected. Padding with zeros.")
        data += b'\x00' * (expected_size - actual_size)
    elif actual_size > expected_size:
        print(f"Warning: File is larger than expected. Truncating.")
        data = data[:expected_size]
    
    # Create image
    img = Image.new('RGB', (width, height))
    pixels = img.load()
    
    # Convert BGRA to RGB
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 4
            # Read BGRA (little-endian)
            b = data[offset]
            g = data[offset + 1]
            r = data[offset + 2]
            # a = data[offset + 3]  # alpha channel (ignored)
            
            pixels[x, y] = (r, g, b)
    
    # Save as PNG
    img.save(output_file, 'PNG')
    print(f"Saved screenshot to {output_file}")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python3 fb_to_png_32bit.py input.raw output.png")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    convert_fb_to_png(input_file, output_file)
