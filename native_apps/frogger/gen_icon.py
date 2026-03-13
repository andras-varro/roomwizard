#!/usr/bin/env python3
"""Generate an 80x80 PPM (P6) splash icon for Frogger.

The icon shows a frog on a scene with road lanes at the bottom,
water at the top, and a recognizable green frog in the center.
"""

import struct
import os
import math

WIDTH = 80
HEIGHT = 80

# Scene colors
ROAD_COLOR   = (60, 60, 60)       # dark gray road
ROAD_STRIPE  = (220, 200, 50)     # yellow lane stripes
WATER_COLOR  = (30, 80, 160)      # blue water
WATER_LIGHT  = (50, 110, 190)     # lighter water ripple
GRASS_COLOR  = (30, 130, 40)      # green grass/safe zone
GRASS_DARK   = (25, 110, 35)      # darker grass accent
LOG_COLOR    = (130, 80, 30)      # brown log on water
LOG_LIGHT    = (160, 100, 45)     # log highlight

# Frog colors
FROG_BODY    = (40, 180, 40)      # bright green body
FROG_DARK    = (30, 140, 30)      # darker green shading
FROG_BELLY   = (80, 210, 80)      # lighter belly/highlight
EYE_WHITE    = (240, 240, 240)    # white of eye
EYE_PUPIL    = (20, 20, 20)       # black pupil
FROG_MOUTH   = (30, 120, 30)      # mouth line color


def dist(x1, y1, x2, y2):
    """Euclidean distance."""
    return math.sqrt((x1 - x2) ** 2 + (y1 - y2) ** 2)


def in_ellipse(px, py, cx, cy, rx, ry):
    """Check if point is inside an axis-aligned ellipse."""
    return ((px - cx) ** 2 / max(rx ** 2, 0.1) +
            (py - cy) ** 2 / max(ry ** 2, 0.1)) <= 1.0


def main():
    pixels = [ROAD_COLOR] * (WIDTH * HEIGHT)

    # --- Draw scene layers (top to bottom) ---
    # Layout (80 pixels tall):
    #  0-7:   top grass (goal zone)
    #  8-35:  water zone
    # 36-43:  middle grass (safe zone)
    # 44-71:  road zone
    # 72-79:  bottom grass (start zone)

    # Top grass (goal zone with lily pad slots)
    for y in range(0, 8):
        for x in range(WIDTH):
            pixels[y * WIDTH + x] = GRASS_COLOR if (x + y) % 7 != 0 else GRASS_DARK

    # Draw goal "slots" in top grass
    slot_positions = [10, 26, 42, 58, 74]
    for sx in slot_positions:
        for y in range(1, 7):
            for x in range(sx - 3, sx + 4):
                if 0 <= x < WIDTH:
                    pixels[y * WIDTH + x] = WATER_COLOR

    # Water zone
    for y in range(8, 36):
        for x in range(WIDTH):
            # Subtle wave pattern
            wave = int(2 * math.sin(x * 0.3 + y * 0.5))
            if (x + y + wave) % 11 < 2:
                pixels[y * WIDTH + x] = WATER_LIGHT
            else:
                pixels[y * WIDTH + x] = WATER_COLOR

    # Draw logs floating on water
    log_rows = [(12, 16), (20, 24), (28, 32)]
    log_configs = [
        [(5, 30)],          # row 1: one long log
        [(0, 18), (45, 70)],  # row 2: two logs
        [(10, 40), (55, 75)], # row 3: two logs
    ]
    for (ly1, ly2), logs in zip(log_rows, log_configs):
        for (lx1, lx2) in logs:
            for y in range(ly1, ly2):
                for x in range(lx1, lx2):
                    if 0 <= x < WIDTH:
                        if y == ly1 or y == ly2 - 1:
                            pixels[y * WIDTH + x] = LOG_LIGHT
                        else:
                            pixels[y * WIDTH + x] = LOG_COLOR
                            # Wood grain texture
                            if (x - lx1) % 6 == 0:
                                pixels[y * WIDTH + x] = LOG_LIGHT

    # Middle grass (safe zone)
    for y in range(36, 44):
        for x in range(WIDTH):
            pixels[y * WIDTH + x] = GRASS_COLOR if (x + y) % 9 != 0 else GRASS_DARK

    # Road zone
    for y in range(44, 72):
        for x in range(WIDTH):
            pixels[y * WIDTH + x] = ROAD_COLOR

    # Road lane stripes (dashed yellow lines)
    stripe_ys = [50, 57, 64]
    for sy in stripe_ys:
        for x in range(WIDTH):
            if (x // 6) % 2 == 0:  # dashed pattern
                for dy in range(2):
                    if 0 <= sy + dy < HEIGHT:
                        pixels[(sy + dy) * WIDTH + x] = ROAD_STRIPE

    # Bottom grass (start zone)
    for y in range(72, 80):
        for x in range(WIDTH):
            pixels[y * WIDTH + x] = GRASS_COLOR if (x + y) % 7 != 0 else GRASS_DARK

    # --- Draw the frog in the center ---
    frog_cx = 40  # center x
    frog_cy = 40  # center y (on the middle safe zone)

    # Back legs (drawn first, behind body)
    # Left back leg
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if in_ellipse(x, y, frog_cx - 12, frog_cy + 6, 6, 4):
                pixels[y * WIDTH + x] = FROG_DARK
            # Right back leg
            if in_ellipse(x, y, frog_cx + 12, frog_cy + 6, 6, 4):
                pixels[y * WIDTH + x] = FROG_DARK

    # Front legs
    for y in range(HEIGHT):
        for x in range(WIDTH):
            # Left front leg
            if in_ellipse(x, y, frog_cx - 10, frog_cy - 2, 5, 3):
                pixels[y * WIDTH + x] = FROG_DARK
            # Right front leg
            if in_ellipse(x, y, frog_cx + 10, frog_cy - 2, 5, 3):
                pixels[y * WIDTH + x] = FROG_DARK

    # Main body (large ellipse)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if in_ellipse(x, y, frog_cx, frog_cy, 10, 8):
                pixels[y * WIDTH + x] = FROG_BODY
                # Belly highlight (upper-center area of body)
                if in_ellipse(x, y, frog_cx, frog_cy - 1, 6, 4):
                    pixels[y * WIDTH + x] = FROG_BELLY

    # Head (slightly wider at top of body)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if in_ellipse(x, y, frog_cx, frog_cy - 7, 9, 5):
                pixels[y * WIDTH + x] = FROG_BODY

    # Eye bumps (protruding circles on top of head)
    left_eye_cx, left_eye_cy = frog_cx - 6, frog_cy - 11
    right_eye_cx, right_eye_cy = frog_cx + 6, frog_cy - 11

    for y in range(HEIGHT):
        for x in range(WIDTH):
            # Eye bump outlines (green circles)
            if dist(x, y, left_eye_cx, left_eye_cy) <= 4.5:
                pixels[y * WIDTH + x] = FROG_BODY
            if dist(x, y, right_eye_cx, right_eye_cy) <= 4.5:
                pixels[y * WIDTH + x] = FROG_BODY

            # White part of eyes
            if dist(x, y, left_eye_cx, left_eye_cy) <= 3.2:
                pixels[y * WIDTH + x] = EYE_WHITE
            if dist(x, y, right_eye_cx, right_eye_cy) <= 3.2:
                pixels[y * WIDTH + x] = EYE_WHITE

            # Pupils (slightly offset forward/center)
            if dist(x, y, left_eye_cx + 0.5, left_eye_cy + 0.5) <= 1.5:
                pixels[y * WIDTH + x] = EYE_PUPIL
            if dist(x, y, right_eye_cx - 0.5, right_eye_cy + 0.5) <= 1.5:
                pixels[y * WIDTH + x] = EYE_PUPIL

    # Mouth (simple curved line)
    for x in range(frog_cx - 5, frog_cx + 6):
        mouth_y = frog_cy - 3 + int(0.15 * (x - frog_cx) ** 2 * 0.3)
        if 0 <= x < WIDTH and 0 <= mouth_y < HEIGHT:
            pixels[mouth_y * WIDTH + x] = FROG_MOUTH

    # Nostrils (two small dots)
    for nx, ny in [(frog_cx - 2, frog_cy - 5), (frog_cx + 2, frog_cy - 5)]:
        if 0 <= nx < WIDTH and 0 <= ny < HEIGHT:
            pixels[ny * WIDTH + nx] = FROG_DARK

    # --- Write PPM P6 ---
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frogger.ppm")
    with open(out_path, "wb") as f:
        header = f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii")
        f.write(header)
        for r, g, b in pixels:
            f.write(struct.pack("BBB", r, g, b))

    print(f"Generated {out_path}  ({WIDTH}x{HEIGHT} P6 PPM)")


if __name__ == "__main__":
    main()
