#!/usr/bin/env python3
"""Convert RoomWizard framebuffer to PNG (supports both RGB565 and 32-bit RGB)"""
import struct
import sys
from PIL import Image

def convert_fb_to_png(input_file, output_file, format='auto'):
    """
    Convert framebuffer to PNG
    
    Args:
        input_file: Path to raw framebuffer file
        output_file: Path to output PNG file
        format: 'rgb565', 'rgb32', or 'auto' (detect from file size)
    """
    # Read raw framebuffer
    with open(input_file, 'rb') as f:
        data = f.read()
    
    width, height = 800, 480
    expected_rgb565 = width * height * 2  # 16-bit
    expected_rgb32 = width * height * 4   # 32-bit
    
    # Auto-detect format
    if format == 'auto':
        if len(data) == expected_rgb565:
            format = 'rgb565'
        elif len(data) >= expected_rgb32:
            format = 'rgb32'
        else:
            print(f"Warning: Unexpected file size {len(data)} bytes")
            format = 'rgb32'  # Default to 32-bit
    
    pixels = []
    
    if format == 'rgb565':
        # Native games format: RGB565 (16-bit)
        for i in range(0, len(data), 2):
            if i + 1 >= len(data):
                break
            rgb565 = struct.unpack('<H', data[i:i+2])[0]
            r = ((rgb565 >> 11) & 0x1F) << 3  # 5 bits -> 8 bits
            g = ((rgb565 >> 5) & 0x3F) << 2   # 6 bits -> 8 bits
            b = (rgb565 & 0x1F) << 3          # 5 bits -> 8 bits
            pixels.append((r, g, b))
    else:
        # ScummVM format: RGB in 32-bit (lower 24 bits)
        for i in range(0, len(data), 4):
            if i + 3 >= len(data):
                break
            pixel = struct.unpack('<I', data[i:i+4])[0]
            r = (pixel >> 16) & 0xFF
            g = (pixel >> 8) & 0xFF
            b = pixel & 0xFF
            pixels.append((r, g, b))
    
    # Create and save image
    img = Image.new('RGB', (width, height))
    img.putdata(pixels[:width*height])
    img.save(output_file)
    print(f"Screenshot saved: {output_file} ({width}x{height}, {format})")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: fb_to_png.py <input_file> [output_file] [format]")
        print("  format: 'rgb565', 'rgb32', or 'auto' (default)")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else 'framebuffer_screenshot.png'
    format_type = sys.argv[3] if len(sys.argv) > 3 else 'auto'
    
    convert_fb_to_png(input_file, output_file, format_type)
