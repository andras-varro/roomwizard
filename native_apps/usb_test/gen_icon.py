#!/usr/bin/env python3
"""Generate a 64x64 PPM (P6) icon for the USB Test app.

The icon shows a stylized USB-A plug shape with a USB trident symbol,
on a dark navy background.
"""

import struct
import os
import math

WIDTH = 64
HEIGHT = 64

# Colors
BG_COLOR   = (0x1a, 0x1a, 0x3a)   # dark navy background
PLUG_BODY  = (180, 180, 190)       # light grey plug body
PLUG_DARK  = (120, 120, 130)       # darker grey for plug shadow/outline
PLUG_METAL = (200, 200, 210)       # metal connector tip
PIN_COLOR  = (255, 215, 0)         # gold pins
CABLE_COLOR= (60, 60, 70)          # dark cable
CYAN       = (0, 220, 255)         # cyan for USB trident symbol
WHITE      = (255, 255, 255)

def set_pixel(pixels, x, y, color):
    """Set pixel if within bounds."""
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        pixels[y * WIDTH + x] = color

def draw_rect(pixels, x0, y0, w, h, color):
    """Draw a filled rectangle."""
    for dy in range(h):
        for dx in range(w):
            set_pixel(pixels, x0 + dx, y0 + dy, color)

def draw_circle(pixels, cx, cy, r, color):
    """Draw a filled circle."""
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                set_pixel(pixels, x, y, color)

def draw_line_v(pixels, x, y0, y1, color, thickness=1):
    """Draw a vertical line."""
    for y in range(min(y0, y1), max(y0, y1) + 1):
        for t in range(thickness):
            set_pixel(pixels, x + t, y, color)

def draw_line_h(pixels, y, x0, x1, color, thickness=1):
    """Draw a horizontal line."""
    for x in range(min(x0, x1), max(x0, x1) + 1):
        for t in range(thickness):
            set_pixel(pixels, x, y + t, color)

def main():
    pixels = [BG_COLOR] * (WIDTH * HEIGHT)

    # --- Draw USB-A plug shape ---
    # The plug is drawn vertically, connector facing up

    # Cable coming from bottom
    cable_w = 6
    cable_x = (WIDTH - cable_w) // 2
    draw_rect(pixels, cable_x, 52, cable_w, 12, CABLE_COLOR)

    # Plug body (the plastic housing)
    body_w = 22
    body_h = 20
    body_x = (WIDTH - body_w) // 2
    body_y = 32
    # Shadow/outline
    draw_rect(pixels, body_x - 1, body_y - 1, body_w + 2, body_h + 2, PLUG_DARK)
    # Body fill
    draw_rect(pixels, body_x, body_y, body_w, body_h, PLUG_BODY)
    # Highlight on top edge
    draw_line_h(pixels, body_y, body_x, body_x + body_w - 1, (210, 210, 220))

    # Metal connector (the part that goes into the port)
    metal_w = 20
    metal_h = 14
    metal_x = (WIDTH - metal_w) // 2
    metal_y = body_y - metal_h
    # Metal outline
    draw_rect(pixels, metal_x - 1, metal_y - 1, metal_w + 2, metal_h + 2, (100, 100, 110))
    # Metal fill
    draw_rect(pixels, metal_x, metal_y, metal_w, metal_h, PLUG_METAL)
    # Inner cavity (darker area inside the connector)
    inner_w = 16
    inner_h = 10
    inner_x = (WIDTH - inner_w) // 2
    inner_y = metal_y + 2
    draw_rect(pixels, inner_x, inner_y, inner_w, inner_h, (40, 40, 55))

    # Gold pins inside the connector (4 pins like USB-A)
    pin_w = 2
    pin_h = 6
    pin_y = inner_y + 2
    pin_positions = [inner_x + 2, inner_x + 5, inner_x + 9, inner_x + 12]
    for px in pin_positions:
        draw_rect(pixels, px, pin_y, pin_w, pin_h, PIN_COLOR)

    # --- Draw USB trident symbol above the plug ---
    # This is the iconic USB symbol: a vertical line with three branches
    # ending in circle, square, and triangle

    cx = WIDTH // 2  # center x
    trident_top = 3
    trident_bottom = metal_y - 3

    # Main vertical stem
    draw_line_v(pixels, cx, trident_top + 4, trident_bottom, CYAN, thickness=2)

    # Circle at top center (the "ground" symbol dot)
    draw_circle(pixels, cx, trident_top + 2, 2, CYAN)

    # Left branch - ends with a square
    branch_y = trident_top + 6
    # Angled line going left (draw as stepped diagonal)
    left_end_x = cx - 10
    for i in range(10):
        set_pixel(pixels, cx - i, branch_y + i // 2, CYAN)
        set_pixel(pixels, cx - i - 1, branch_y + i // 2, CYAN)
    # Vertical drop on left
    left_branch_bottom = branch_y + 5
    draw_line_v(pixels, left_end_x, left_branch_bottom, left_branch_bottom + 4, CYAN, thickness=2)
    # Small square at end
    sq_size = 3
    draw_rect(pixels, left_end_x - 1, left_branch_bottom + 5, sq_size + 1, sq_size + 1, CYAN)

    # Right branch - ends with a triangle
    right_end_x = cx + 9
    for i in range(10):
        set_pixel(pixels, cx + i, branch_y + i // 2, CYAN)
        set_pixel(pixels, cx + i + 1, branch_y + i // 2, CYAN)
    # Vertical drop on right
    right_branch_bottom = branch_y + 5
    draw_line_v(pixels, right_end_x, right_branch_bottom, right_branch_bottom + 4, CYAN, thickness=2)
    # Small triangle at end (pointing down)
    tri_y = right_branch_bottom + 5
    for row in range(4):
        for col in range(-row, row + 1):
            set_pixel(pixels, right_end_x + col, tri_y + row, CYAN)

    # --- Write PPM P6 ---
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "usb_test.ppm")
    with open(out_path, "wb") as f:
        header = f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii")
        f.write(header)
        for r, g, b in pixels:
            f.write(struct.pack("BBB", r, g, b))

    print(f"Generated {out_path}  ({WIDTH}x{HEIGHT} P6 PPM)")


if __name__ == "__main__":
    main()
