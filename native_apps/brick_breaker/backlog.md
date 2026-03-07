# Brick Breaker — Development Backlog

> **Purpose:** This file tracks planned features and provides project context so any new session can immediately start implementing backlog items without needing to re-read all source files.

---

## Project Context

### Target Device
- **Steelcase RoomWizard** — embedded Linux, 600 MHz ARMv7 Cortex-A8, 234 MB RAM
- **Display:** 800×480 framebuffer (`/dev/fb0`), resistive single-touch
- **Safe area:** ~720×420 visible (bezel covers edges); governed by `SCREEN_SAFE_*` macros from `common.h`

### Source File
- **Main source:** [`brick_breaker.c`](brick_breaker.c) (~1300 lines)
- **Build:** `native_apps/Makefile` — `make brick_breaker`
- **Cross-compile:** `arm-linux-gnueabihf-gcc` from WSL, deploy via SCP

### Common Libraries (in `native_apps/common/`)
| Library | Purpose |
|---------|---------|
| `framebuffer.c/.h` | Double-buffered RGB888 rendering to `/dev/fb0` |
| `touch_input.c/.h` | Touch events (12-bit raw → 800×480 mapping) |
| `hardware.c/.h` | LED/backlight control via sysfs |
| `common.c/.h` | Unified UI (buttons, screens, safe area macros, `RGB()` macro) |
| `ui_layout.c/.h` | Layouts + ScrollableList widget |
| `audio.c/.h` | OSS audio (beeps, tones) via `/dev/dsp` |
| `highscore.c/.h` | Persistent high score table (file: `"brick_breaker"`) |

### Color System
- **RGB888** as `uint32_t` via `RGB(r,g,b)` macro
- Framebuffer library handles any conversion to device format

### Current Game Architecture

#### Play Area
| Constant | Value | Notes |
|----------|-------|-------|
| `AREA_X` | `SCREEN_SAFE_LEFT` | Left edge |
| `AREA_Y` | `SCREEN_SAFE_TOP + 65` | Top (65px for HUD+buttons) |
| `AREA_W` | `SCREEN_SAFE_WIDTH` | Width |
| `AREA_H` | `SCREEN_SAFE_HEIGHT - 65` | Height |

#### Bricks
| Constant | Value |
|----------|-------|
| `BRICK_COLS` | 12 |
| `BRICK_ROWS` | 8 (max) |
| `BRICK_PAD` | 3 px gap |
| `BRICK_H` | 20 px |
| `MAX_BRICKS` | 96 |

Brick width is dynamic: `(AREA_W - BRICK_PAD * (BRICK_COLS + 1)) / BRICK_COLS`
Vertical offset: 40px from top of play area.

Brick positioning:
```c
b->x = AREA_X + BRICK_PAD + c * (brick_w + BRICK_PAD);
b->y = AREA_Y + BRICK_PAD + 40 + r * (BRICK_H + BRICK_PAD);
```

**Color palette:** `ROW_COLORS` provides 8 rainbow colors for single-hit bricks (one per row). Each `Brick` struct has a `color_index` field used for base color when `max_health == 1`; multi-hit bricks still use health-based coloring.

#### Brick Types
- **Normal:** health 1–6, color based on health (green→yellow→orange→red→purple→cyan)
- **Indestructible:** health = -1, grey with diagonal stripes, only fireball can destroy
- **Bonus:** gold with star pattern, 2× score
- **Explosive:** orange-red with X pattern, destroys adjacent bricks on hit

#### Brick Colors (by health, top/bottom gradient)
| Health | Color | Top RGB | Bottom RGB |
|--------|-------|---------|------------|
| 1 | Green | (80,220,80) | (40,140,40) |
| 2 | Yellow | (255,255,80) | (180,160,30) |
| 3 | Orange | (255,160,50) | (180,90,20) |
| 4 | Red | (255,60,60) | (160,30,30) |
| 5 | Purple | (180,100,255) | (120,50,180) |
| 6 | Cyan | (0,200,255) | (0,120,180) |

#### Ball
| Constant | Value |
|----------|-------|
| `BALL_RADIUS` | 6 px |
| `MAX_BALLS` | 8 |
| `BALL_BASE_SPEED` | 4.0 px/frame |
| `BALL_MAX_SPEED` | 11.0 px/frame |
| `BALL_SPEED_INC` | 0.25 per brick hit |
| `TRAIL_LEN` | 6 (fireball trail circular buffer) |
| `SPEED_MULTS[5]` | Speed stepping array for speed power-up levels |

#### Paddle
| Constant | Value |
|----------|-------|
| `PADDLE_Y` | `AREA_Y + AREA_H - 28` |
| `PADDLE_HEIGHT` | 14 px |
| `PADDLE_BASE_W` | 100 px |
| `PADDLE_WIDTHS[5]` | `{40, 70, 100, 140, 180}` (indexed by paddle_size_level, -2 to +2) |

#### Power-Up System
| Type | Symbol | Color | Effect |
|------|--------|-------|--------|
| `PU_WIDEN` | W | Green | Paddle size +1 step |
| `PU_NARROW` | N | Red | Paddle size -1 step |
| `PU_FIREBALL` | F | Orange | Activates fireball mode |
| `PU_NORMALBALL` | B | White | Deactivates fireball mode |
| `PU_SPEED_UP` | > | Magenta | Ball speed +1 step |
| `PU_SLOW_DOWN` | < | Yellow | Ball speed -1 step |
| `PU_EXTRA_LIFE` | + | Pink | +1 life |
| `PU_LOSE_LIFE` | - | Dark Red | -1 life (won't kill on last life) |
| `PU_MULTIBALL` | M | Blue | +2 balls per active ball |

Power-ups fall from destroyed bricks (30% chance), 18×18 px, fall at 2 px/frame. Effects are **permanent until ball lost or level complete** (no more 12s timers). Paddle size and ball speed use a stacking level system from -2 to +2; opposing power-ups cancel one step of each other.

#### Levels
10 levels with 6 patterns: Standard, Checkerboard, Pyramid, Diamond, Border, Stripes.
Levels define: rows, pattern, speed_mult (1.0–1.4×), per-row health, special brick chance (from level 3+), indestructible chance (from level 5+).

#### Particles
- `MAX_PARTICLES` = 150
- 6 particles per brick destruction, 4 per explosive chain
- Gravity: `dy += 0.15f`, lifetime: 12–22 frames
- Rendered as 2×2 px white squares with fadeout
- Fireball destruction spawns red/yellow/orange fire-colored particles

#### Screen Shake
- `shake_x`, `shake_y`, `shake_frames`, `shake_intensity` fields in `GameState`
- Shake offset applied only to rendering, not to game physics
- Single explosion: ~8 frames, 3px amplitude
- Cascading explosions increase intensity and duration

#### Cascading Explosions
- Queue-based chain detonation: when an explosive brick detonates another explosive brick, it is queued and processed iteratively
- Each cascade step increases screen shake intensity

#### Screen States
`WELCOME` → `PLAYING` → `PAUSED` / `LEVEL_COMPLETE` / `GAME_OVER`

Pause menu has three buttons: **RESUME**, **RETIRE** (saves score and ends game), **EXIT**.

#### Main Loop
~60 FPS (`usleep(16667)`): handle_input → update_game → draw → fb_swap

---

## Backlog

### BB-08: Framebuffer-Level Draw Offset for Screen Shake ✅ DONE
**Priority:** Medium | **Complexity:** Medium | **Scope:** Cross-cutting (framebuffer library + brick_breaker refactor) | **Status:** DONE

Moved screen shake coordinate offset from game-level to the shared framebuffer library. Added `draw_offset_x`/`draw_offset_y` fields to `Framebuffer` struct with `fb_set_draw_offset()`/`fb_clear_draw_offset()` API. Offset applied in 9 leaf drawing primitives (`fb_fill_rect`, `fb_fill_circle`, `fb_draw_text`, `fb_draw_line`, `fb_fill_rect_gradient`, `fb_draw_pixel_alpha`, `fb_draw_rect`, `fb_draw_circle`, `fb_draw_rounded_rect`). Refactored brick_breaker to remove manual `(int sx, int sy)` parameters from 6 draw functions, replacing with `fb_set_draw_offset()`/`fb_clear_draw_offset()` calls in `draw_game_screen()`. Fully backward-compatible — offset defaults to (0,0).

---

## Implementation Order

All backlog items have been implemented (BB-01 through BB-08).

---

## Completed Items

### BB-01: Colorful Standard Bricks ✅ DONE
**Priority:** Low | **Complexity:** Small | **Status:** DONE

Added `color_index` field to `Brick` struct. Single-hit bricks use row-based rainbow colors via `ROW_COLORS` palette; multi-hit bricks retain health-based coloring so damage state remains visible.

---

### BB-02: Adjust Brick Dimensions (Shorter & Thicker) ✅ DONE
**Priority:** Low | **Complexity:** Small | **Status:** DONE

Increased `BRICK_H` from 16 to 20 and `BRICK_COLS` from 10 to 12. Reduced vertical offset from 80px to 40px to accommodate the larger bricks. Implemented together with BB-03.

---

### BB-03: More Rows of Bricks ✅ DONE
**Priority:** Medium | **Complexity:** Medium | **Status:** DONE

Increased `BRICK_ROWS` from 6 to 8, `MAX_BRICKS` from 60 to 96. Level 1 now starts with 5 rows, scaling up to 8 rows in later levels. Updated all `LevelDef` entries accordingly.

---

### BB-04: Retire Button (Save & Quit) ✅ DONE
**Priority:** Medium | **Complexity:** Small | **Status:** DONE

Added RETIRE button to the pause menu between RESUME and EXIT. On press, submits current score via `highscore_submit()` and transitions to the GAME_OVER screen.

---

### BB-05: Revamped Power-Up System (Paired & Permanent) ✅ DONE
**Priority:** High | **Complexity:** Large | **Status:** DONE

Replaced 12-second timer system with permanent effects (cleared on ball loss or level complete). Added opposing pairs: Widen/Narrow paddle (5-step stacking), Fireball/NormalBall toggle, SpeedUp/SlowDown (5-step stacking), ExtraLife/LoseLife (safe on last life), and standalone Multiball. 30% drop chance from destroyed bricks.

---

### BB-06: Enhanced Explosions with Screen Shake ✅ DONE
**Priority:** Medium | **Complexity:** Medium | **Status:** DONE

Added `shake_x/y`, `shake_frames`, `shake_intensity` to `GameState`. Shake offset applied to rendering only, not physics. Queue-based cascading explosion system: each chain step increases shake intensity and duration. Single explosion ~8 frames at 3px amplitude.

---

### BB-07: Fireball Visual Effects ✅ DONE
**Priority:** Low | **Complexity:** Medium | **Status:** DONE

Added 6-position circular trail buffer to `Ball` struct, rendered as fading orange-to-red dots behind the fireball. Brick destruction spawns fire-colored particles (red/yellow/orange) instead of white. Increased `MAX_PARTICLES` from 40 to 150.

---

### BB-08: Framebuffer-Level Draw Offset for Screen Shake ✅ DONE
**Priority:** Medium | **Complexity:** Medium | **Status:** DONE

Moved screen shake coordinate offset from game-level to the shared framebuffer library. Added `draw_offset_x`/`draw_offset_y` fields to `Framebuffer` struct with `fb_set_draw_offset()`/`fb_clear_draw_offset()` API. Offset applied in 9 leaf drawing primitives. Refactored brick_breaker to remove manual `(int sx, int sy)` parameters from 6 draw functions (`draw_bricks`, `draw_paddle`, `draw_balls`, `draw_powerups`, `draw_particles`, `draw_play_area_border`), replacing with `fb_set_draw_offset()`/`fb_clear_draw_offset()` calls in `draw_game_screen()`. HUD rendering remains unaffected (drawn after offset cleared). Fully backward-compatible — offset defaults to (0,0).
