#!/usr/bin/env python3
"""Generate an 80x80 PPM (P6) splash icon for SameGame.

The icon shows a grid of colored blocks with some missing (removed)
to suggest gameplay, on a dark background.
"""

import struct
import os
import random

WIDTH = 80
HEIGHT = 80

# Game colors
RED    = (220, 50, 50)
BLUE   = (50, 100, 220)
GREEN  = (50, 180, 50)
YELLOW = (230, 200, 30)
PURPLE = (170, 60, 200)
COLORS = [RED, BLUE, GREEN, YELLOW, PURPLE]

BG_COLOR = (25, 25, 35)        # dark background
GRID_BG  = (15, 15, 22)        # slightly darker grid background
BORDER   = (60, 60, 80)        # subtle grid border

def main():
    random.seed(42)  # deterministic output

    pixels = [BG_COLOR] * (WIDTH * HEIGHT)

    # Grid parameters
    cols, rows = 5, 5
    block_size = 10
    gap = 2
    grid_w = cols * (block_size + gap) - gap  # 58
    grid_h = rows * (block_size + gap) - gap  # 58
    ox = (WIDTH - grid_w) // 2    # offset x to center
    oy = (HEIGHT - grid_h) // 2 + 2  # offset y, nudge down slightly

    # Build a random board with some blocks removed to suggest gameplay
    board = [[random.choice(COLORS) for _ in range(cols)] for _ in range(rows)]

    # Create some "connected group" removals for realism
    # Remove a cluster in top-right area
    board[0][3] = None
    board[0][4] = None
    board[1][4] = None
    # Remove a cluster in middle-left
    board[2][0] = None
    board[3][0] = None
    board[2][1] = None
    # Remove one more isolated gap
    board[4][2] = None
    board[1][2] = None

    # Draw grid background (slightly darker rectangle behind the grid)
    pad = 3
    for y in range(oy - pad, oy + grid_h + pad):
        for x in range(ox - pad, ox + grid_w + pad):
            if 0 <= x < WIDTH and 0 <= y < HEIGHT:
                pixels[y * WIDTH + x] = GRID_BG

    # Draw blocks
    for r in range(rows):
        for c in range(cols):
            color = board[r][c]
            bx = ox + c * (block_size + gap)
            by = oy + r * (block_size + gap)

            if color is None:
                # Empty cell — leave as grid background
                continue

            # Draw filled block with slight highlight (top-left) and shadow (bottom-right)
            for dy in range(block_size):
                for dx in range(block_size):
                    px = bx + dx
                    py = by + dy
                    if 0 <= px < WIDTH and 0 <= py < HEIGHT:
                        cr, cg, cb = color
                        # Top/left edge highlight
                        if dy == 0 or dx == 0:
                            cr = min(255, cr + 50)
                            cg = min(255, cg + 50)
                            cb = min(255, cb + 50)
                        # Bottom/right edge shadow
                        elif dy == block_size - 1 or dx == block_size - 1:
                            cr = max(0, cr - 40)
                            cg = max(0, cg - 40)
                            cb = max(0, cb - 40)
                        pixels[py * WIDTH + px] = (cr, cg, cb)

    # Write PPM P6
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "samegame.ppm")
    with open(out_path, "wb") as f:
        header = f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii")
        f.write(header)
        for r, g, b in pixels:
            f.write(struct.pack("BBB", r, g, b))

    print(f"Generated {out_path}  ({WIDTH}x{HEIGHT} P6 PPM)")


if __name__ == "__main__":
    main()
