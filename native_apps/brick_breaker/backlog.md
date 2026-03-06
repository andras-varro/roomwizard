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

(All items completed — see Completed Items below)

---

## Implementation Order (Suggested)

| Order | Item | Rationale |
|-------|------|-----------|
| 1 | BB-02 + BB-03 | Brick dimensions + more rows — do together since they affect layout |
| 2 | BB-01 | Colorful bricks — quick visual win after layout changes |
| 3 | BB-04 | Retire button — simple addition to pause menu |
| 4 | BB-05 | Power-up overhaul — largest change, benefits from stable brick layout |
| 5 | BB-06 | Explosions + shake — builds on power-up and brick changes |
| 6 | BB-07 | Fireball effects — polish item, do last |

---

## Completed Items

### BB-01: Colorful Standard Bricks ✅ DONE
**Priority:** Low | **Complexity:** Small | **Status:** DONE

Currently, standard bricks get their color solely from their health value. Single-hit bricks (health=1) are always green. Make standard bricks more visually diverse by assigning colors based on position (row, column) or random assignment, not just health.

**Implementation notes:**
- The `BRICK_COLORS` array at line ~188 maps health→color. For single-hit bricks, they always get index 0 (green).
- Approach: assign a `color_index` to each brick at creation time (in `init_level()` around line 336) based on row position or a pattern, independent of health.
- Keep multi-hit bricks colored by health (player needs to see damage state).
- The `draw_brick()` function at line ~860 uses `BRICK_COLORS[b->health - 1]` — modify to use `b->color_index` for the base color when `max_health == 1`, and health-based color when `max_health > 1`.
- May need to add a `color_index` field to the `Brick` struct (line ~110).
- Consider a broader palette (e.g., row-based rainbow: red, orange, yellow, green, blue, purple).

---

### BB-02: Adjust Brick Dimensions (Shorter & Thicker) ✅ DONE
**Priority:** Low | **Complexity:** Small | **Status:** DONE

Change brick proportions from the current ~wide/thin look to shorter and thicker. Target: reduce width and increase height.

**Implementation notes:**
- Current: `BRICK_H = 16`, `BRICK_COLS = 10`, width is dynamic (~69px with typical safe area).
- The user described current bricks as "19×4mm on screenshot" and wants "15×5mm" — roughly 80% width, 125% height.
- Approach: Increase `BRICK_H` from 16 to 20 (or so) and either increase `BRICK_COLS` to 12 or add extra padding — experiment to find the best feel.
- The vertical offset (80px from play area top) may need adjustment to accommodate thicker bricks with more rows.
- This interacts with BB-03 (more rows) — implement together.

---

### BB-03: More Rows of Bricks ✅ DONE
**Priority:** Medium | **Complexity:** Medium | **Status:** DONE

Add more brick rows for increased challenge. Target: at least 5 rows in early levels, more in later ones. User asks "would 5 rows fit?" — currently early levels use 3–4 rows, max is 6.

**Implementation notes:**
- `BRICK_ROWS` is currently 6, `MAX_BRICKS` = 60 (10×6). Increase to 8 rows → `MAX_BRICKS` = 80.
- Vertical space check: play area height = `SCREEN_SAFE_HEIGHT - 65`. With `BRICK_H=20` and `BRICK_PAD=3`, 8 rows = 8×23 = 184px. Plus 80px offset = 264px. Play area is ~355px, leaving ~91px for paddle area — sufficient.
- Update `LevelDef` entries (line ~235): make level 1 start with 5 rows, scale up to 8 rows for later levels.
- Increase `BRICK_ROWS` constant and `MAX_BRICKS`.
- Update the `bricks[]` array size in `GameState`.
- **Depends on:** BB-02 (brick dimensions) — implement together.

---

### BB-04: Retire Button (Save & Quit) ✅ DONE
**Priority:** Medium | **Complexity:** Small | **Status:** DONE

Add a "Retire" button to the pause menu that ends the game and lets the user save their current score to the high score table.

**Implementation notes:**
- The pause screen (drawn in `draw_paused()` around line ~1055) currently has RESUME and EXIT buttons.
- Add a RETIRE button between them.
- On RETIRE: save current score via `highscore_submit("brick_breaker", game.score)`, then transition to `GAME_OVER` screen (or a dedicated retirement screen showing final score + leaderboard).
- Touch handling for the pause screen is in `handle_input()` around line ~1160.

---

### BB-05: Revamped Power-Up System (Paired & Permanent) ✅ DONE
**Priority:** High | **Complexity:** Large | **Status:** DONE

Overhaul power-ups to be **permanent until level complete or ball lost** (instead of 12s timer). Add power-ups in opposing pairs with stacking/canceling behavior:

**Pairs:**
1. **Widen (W) / Narrow (N) paddle:**
   - 5 paddle sizes: narrow → normal → large → larger → (cap)
   - W: narrow→normal→large→larger→stays (no more growing)
   - N: larger→large→normal→narrow→stays (no more shrinking)
   - W and N cancel one step of each other
   
2. **Fireball (F) / Normal ball (B):**
   - F activates fireball mode
   - B (new) deactivates fireball, returns to normal bouncing

3. **Speed-up (>) / Slow-down (<) ball:**
   - Stacking speed modifiers, similar to paddle width steps
   - Currently only slow exists (`PU_SLOW`); add speed-up
   
4. **Extra life (+) / Lose life (-):**
   - Extra life: +1 (already exists)
   - Lose life: -1, but if on last life, don't kill the player (that's unfair!)
   
5. **Multiball (M):**
   - No opposite needed, just multiball alone

**Implementation notes:**
- Remove `POWERUP_DUR_MS` timer system. Instead, clear effects on ball_lost and level_complete.
- Change `ActiveEffects` from timestamp-based to state-based (paddle_size_level = -2..+2, fireball bool, speed_level = -2..+2).
- Paddle widths: define 5 levels e.g., [40, 70, 100, 140, 180] indexed by `paddle_size_level` (-2 to +2).
- Ball speed: define multipliers e.g., [0.5, 0.75, 1.0, 1.25, 1.5] indexed by `speed_level`.
- Update `PowerUpType` enum — add `PU_NORMAL_BALL`, `PU_SPEED_UP`, `PU_LOSE_LIFE`. Remove the current `PU_NARROW` as penalty-from-pickup and replace with the new paired system.
- Update `spawn_powerup()`, `collect_powerup()`, `expire_effects()`, effect display, and drawing code.
- Remove countdown display for timed effects; maybe show current power-up state icons instead.
- The lose-life power-up needs a safety check: `if (game.lives > 1) game.lives--; // don't kill on last life`

---

### BB-06: Enhanced Explosions with Screen Shake ✅ DONE
**Priority:** Medium | **Complexity:** Medium | **Status:** DONE

Make explosions more dramatic:
1. **More explosive bricks** — increase chance of explosive bricks in levels
2. **Screen shake on explosion** — offset the entire rendered frame by a few pixels for several frames
3. **Cascading shake** — if an explosive brick detonates another explosive brick, increase the shake intensity

**Implementation notes:**
- Screen shake: add `shake_x`, `shake_y`, `shake_frames` to `GameState`. When drawing, offset all coordinates by shake values. Decrease shake over time (dampening).
- Single explosion: shake_frames = ~8, amplitude = 3px
- Cascade: each additional explosion in the same chain adds to shake_frames and amplitude (e.g., +4 frames, +2px per cascade)
- The explosion chain logic is at line ~663. Currently it's a single-pass adjacency check. For cascading, need to queue newly-detonated explosive bricks and process them iteratively.
- Ball must still animate normally during shake — apply shake offset only to rendering, not to game physics coordinates.
- Increase `BRICK_EXPLOSIVE` chance in level definitions (currently ~10% from level 3+).

---

### BB-07: Fireball Visual Effects ✅ DONE
**Priority:** Low | **Complexity:** Medium | **Status:** DONE

Add visual flair to fireball mode:
1. **Fiery trail** — render a few dots/circles at previous ball positions (position history buffer)
2. **Fiery brick destruction particles** — when a fireball destroys a brick, spawn red/yellow/orange particles instead of plain white

**Implementation notes:**
- Trail: add a circular buffer of last ~6 ball positions to the `Ball` struct. Each frame, push current position. Draw trail dots with decreasing size and fading from orange to dark red.
- Fire particles: in the brick destruction code (around line ~700), check `ball->fireball` and spawn particles with fire colors `RGB(255,80,0)`, `RGB(255,200,0)`, `RGB(255,140,0)` instead of `b->color_top`.
- Need to increase `MAX_PARTICLES` from 40 to ~80 to handle the extra trail + fire particles.
- Trail rendering should go in `draw_ball()` (line ~996), drawn before the ball itself so the ball appears on top.
