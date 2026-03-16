#!/usr/bin/env python3
"""Generate a 64x64 PPM (P6) splash icon for Office Runner (platformer).

The icon shows a side-scrolling platformer scene with sky, ground,
a platform block, a small office-worker player, and a coin.
"""

import struct
import os
import math

WIDTH = 64
HEIGHT = 64

# Scene colors
SKY_COLOR    = (100, 180, 240)    # sky blue
SKY_LIGHT    = (130, 200, 250)    # lighter sky near horizon
GROUND_COLOR = (60, 140, 40)      # green grass
GROUND_DARK  = (45, 110, 30)      # darker grass stripe
DIRT_COLOR   = (120, 80, 40)      # brown dirt under grass

# Platform block colors
PLAT_COLOR   = (140, 100, 55)     # brown brick
PLAT_LIGHT   = (170, 125, 70)     # brick highlight
PLAT_DARK    = (100, 70, 35)      # brick shadow
PLAT_LINE    = (90, 60, 30)       # mortar line

# Player (office worker) colors
SHIRT_COLOR  = (50, 100, 200)     # blue shirt
PANTS_COLOR  = (50, 50, 60)       # dark pants
SKIN_COLOR   = (230, 185, 140)    # skin tone
HAIR_COLOR   = (60, 40, 25)       # dark hair
SHOE_COLOR   = (30, 30, 30)       # black shoes

# Coin colors
COIN_COLOR   = (240, 210, 50)     # gold coin
COIN_LIGHT   = (255, 240, 100)    # coin highlight
COIN_DARK    = (200, 170, 30)     # coin shadow


def dist(x1, y1, x2, y2):
    """Euclidean distance."""
    return math.sqrt((x1 - x2) ** 2 + (y1 - y2) ** 2)


def main():
    pixels = [SKY_COLOR] * (WIDTH * HEIGHT)

    # --- Sky gradient ---
    for y in range(0, 50):
        for x in range(WIDTH):
            # Slight gradient: lighter near bottom of sky
            t = y / 50.0
            r = int(SKY_COLOR[0] * (1 - t * 0.3) + SKY_LIGHT[0] * (t * 0.3))
            g = int(SKY_COLOR[1] * (1 - t * 0.3) + SKY_LIGHT[1] * (t * 0.3))
            b = int(SKY_COLOR[2] * (1 - t * 0.3) + SKY_LIGHT[2] * (t * 0.3))
            pixels[y * WIDTH + x] = (r, g, b)

    # --- Simple cloud puffs ---
    cloud_centers = [(15, 8), (20, 7), (25, 9), (48, 12), (53, 11), (44, 13)]
    for cx, cy in cloud_centers:
        for y in range(max(0, cy - 4), min(HEIGHT, cy + 5)):
            for x in range(max(0, cx - 5), min(WIDTH, cx + 6)):
                d = dist(x, y, cx, cy)
                if d <= 4.0:
                    pixels[y * WIDTH + x] = (240, 245, 255)
                elif d <= 5.0:
                    # Soft edge
                    bg = pixels[y * WIDTH + x]
                    pixels[y * WIDTH + x] = (
                        min(255, (bg[0] + 240) // 2),
                        min(255, (bg[1] + 245) // 2),
                        min(255, (bg[2] + 255) // 2),
                    )

    # --- Ground (bottom 14 rows: grass on top, dirt below) ---
    for y in range(50, 64):
        for x in range(WIDTH):
            if y == 50:
                # Grass top edge — bright green
                pixels[y * WIDTH + x] = (80, 180, 50)
            elif y <= 52:
                # Grass layer
                if (x + y) % 5 == 0:
                    pixels[y * WIDTH + x] = GROUND_DARK
                else:
                    pixels[y * WIDTH + x] = GROUND_COLOR
            else:
                # Dirt layer
                pixels[y * WIDTH + x] = DIRT_COLOR
                if (x + y) % 7 == 0:
                    pixels[y * WIDTH + x] = (110, 70, 35)

    # --- Platform block (floating, mid-air) ---
    # A brick platform at roughly y=34..39, x=30..50
    plat_x1, plat_x2 = 28, 52
    plat_y1, plat_y2 = 33, 39

    for y in range(plat_y1, plat_y2):
        for x in range(plat_x1, plat_x2):
            if 0 <= x < WIDTH and 0 <= y < HEIGHT:
                # Brick pattern with mortar lines
                brick_w = 6
                brick_h = 3
                # Offset every other row
                row = (y - plat_y1)
                offset = (brick_w // 2) if (row // brick_h) % 2 == 1 else 0
                bx = (x - plat_x1 + offset) % brick_w
                by = (y - plat_y1) % brick_h

                if by == 0 or bx == 0:
                    # Mortar line
                    pixels[y * WIDTH + x] = PLAT_LINE
                elif by == 1 and bx == 1:
                    # Highlight corner
                    pixels[y * WIDTH + x] = PLAT_LIGHT
                else:
                    pixels[y * WIDTH + x] = PLAT_COLOR

    # Platform top highlight
    for x in range(plat_x1, plat_x2):
        if 0 <= x < WIDTH:
            pixels[plat_y1 * WIDTH + x] = PLAT_LIGHT

    # Platform bottom shadow
    for x in range(plat_x1, plat_x2):
        if 0 <= x < WIDTH and plat_y2 < HEIGHT:
            pixels[(plat_y2) * WIDTH + x] = PLAT_DARK

    # --- Player figure (office worker) ---
    # Standing on the ground, at x~14, feet at y=49
    px, py_feet = 14, 49  # center x, feet y

    # Shoes (2px tall, 4px wide)
    for dy in range(0, 2):
        for dx in range(-2, 2):
            sx, sy = px + dx, py_feet - dy
            if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
                pixels[sy * WIDTH + sx] = SHOE_COLOR

    # Pants/legs (4px tall, 4px wide)
    for dy in range(2, 6):
        for dx in range(-2, 2):
            sx, sy = px + dx, py_feet - dy
            if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
                pixels[sy * WIDTH + sx] = PANTS_COLOR

    # Shirt/torso (5px tall, 5px wide)
    for dy in range(6, 11):
        for dx in range(-2, 3):
            sx, sy = px + dx, py_feet - dy
            if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
                pixels[sy * WIDTH + sx] = SHIRT_COLOR
                # Lighter center stripe (tie area)
                if dx == 0:
                    pixels[sy * WIDTH + sx] = (200, 50, 50)  # red tie

    # Arms (extend from torso sides) — one arm up (running pose)
    # Left arm (back, down)
    for dy in range(7, 10):
        sx, sy = px - 3, py_feet - dy
        if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
            pixels[sy * WIDTH + sx] = SHIRT_COLOR
    # Left hand
    sx, sy = px - 3, py_feet - 7
    if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
        pixels[sy * WIDTH + sx] = SKIN_COLOR

    # Right arm (forward, up)
    for dy in range(9, 12):
        sx, sy = px + 3, py_feet - dy
        if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
            pixels[sy * WIDTH + sx] = SHIRT_COLOR
    # Right hand
    sx, sy = px + 3, py_feet - 12
    if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
        pixels[sy * WIDTH + sx] = SKIN_COLOR

    # Neck (skin, 2px)
    for dx in range(-1, 2):
        sx, sy = px + dx, py_feet - 11
        if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
            pixels[sy * WIDTH + sx] = SKIN_COLOR

    # Head (circle, radius ~3)
    head_cx, head_cy = px, py_feet - 14
    for y in range(max(0, head_cy - 4), min(HEIGHT, head_cy + 5)):
        for x in range(max(0, head_cx - 4), min(WIDTH, head_cx + 5)):
            d = dist(x, y, head_cx, head_cy)
            if d <= 3.0:
                pixels[y * WIDTH + x] = SKIN_COLOR
            # Hair on top
            if d <= 3.0 and y <= head_cy - 1:
                pixels[y * WIDTH + x] = HAIR_COLOR

    # Eyes (single pixel each)
    ex1, ey = head_cx + 1, head_cy
    if 0 <= ex1 < WIDTH and 0 <= ey < HEIGHT:
        pixels[ey * WIDTH + ex1] = (30, 30, 30)

    # --- Coin (floating in the air, near the platform) ---
    coin_cx, coin_cy = 40, 27
    for y in range(max(0, coin_cy - 5), min(HEIGHT, coin_cy + 6)):
        for x in range(max(0, coin_cx - 5), min(WIDTH, coin_cx + 6)):
            d = dist(x, y, coin_cx, coin_cy)
            if d <= 3.5:
                if d <= 1.5:
                    # Inner highlight
                    pixels[y * WIDTH + x] = COIN_LIGHT
                elif x < coin_cx and y < coin_cy:
                    # Top-left highlight
                    pixels[y * WIDTH + x] = COIN_LIGHT
                elif x > coin_cx and y > coin_cy:
                    # Bottom-right shadow
                    pixels[y * WIDTH + x] = COIN_DARK
                else:
                    pixels[y * WIDTH + x] = COIN_COLOR

    # Coin "$" symbol (simple vertical line + 2 horizontal)
    if 0 <= coin_cx < WIDTH:
        for dy in [-1, 0, 1]:
            cy = coin_cy + dy
            if 0 <= cy < HEIGHT:
                pixels[cy * WIDTH + coin_cx] = COIN_DARK
    for dx in [-1, 1]:
        cx = coin_cx + dx
        if 0 <= cx < WIDTH:
            if 0 <= coin_cy - 1 < HEIGHT:
                pixels[(coin_cy - 1) * WIDTH + cx] = COIN_DARK
            if 0 <= coin_cy + 1 < HEIGHT:
                pixels[(coin_cy + 1) * WIDTH + cx] = COIN_DARK

    # --- Write PPM P6 ---
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "platformer.ppm")
    with open(out_path, "wb") as f:
        header = f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii")
        f.write(header)
        for r, g, b in pixels:
            f.write(struct.pack("BBB", r, g, b))

    print(f"Generated {out_path}  ({WIDTH}x{HEIGHT} P6 PPM)")


if __name__ == "__main__":
    main()
