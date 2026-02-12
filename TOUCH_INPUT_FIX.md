# Touch Input Coordinate Fix

## Problem Summary

The RoomWizard game system was experiencing touch coordinate accuracy issues:
- Coordinates would get "stuck" at maximum values (799, 479)
- Last 2-3 touches often showed the same coordinates
- First touch would frequently use coordinates from the previous touch

## Root Cause

The issue was caused by a fundamental misunderstanding of Linux input event ordering.

### Linux Input Event Stream Order

When you touch the screen, the kernel sends events in this specific order:

```
1. EV_ABS / ABS_X      (X coordinate)
2. EV_ABS / ABS_Y      (Y coordinate)
3. EV_KEY / BTN_TOUCH  (Touch press/release)
4. EV_SYN / SYN_REPORT (Frame complete)
```

**Critical insight:** The coordinates arrive BEFORE the touch press event, not after!

### Original (Broken) Code Logic

```c
// Wait for BTN_TOUCH press
if (BTN_TOUCH pressed) {
    // NOW try to capture coordinates
    // But they already passed! We get stale data.
}
```

This approach would:
1. See the `BTN_TOUCH` press event
2. Try to read coordinates from the next events
3. But the coordinates for THIS touch already passed
4. End up using coordinates from a previous touch or stale values

## The Fix

### New (Correct) Code Logic

```c
// Always capture coordinates as they arrive
if (ABS_X or ABS_Y) {
    current_x = value;
    current_y = value;
}

// When we see a press, use already-captured coordinates
if (BTN_TOUCH pressed) {
    // Coordinates are already captured!
    use current_x and current_y
}

// Wait for sync to confirm frame is complete
if (SYN_REPORT) {
    return coordinates
}
```

This approach:
1. Captures coordinates immediately as they arrive
2. When `BTN_TOUCH` is detected, uses the coordinates already captured in this frame
3. Waits for `SYN_REPORT` to ensure the event frame is complete
4. Returns accurate coordinates from the same event frame as the touch

## Implementation

The fix was applied to [`touch_wait_for_press()`](native_games/common/touch_input.c:60) in [`native_games/common/touch_input.c`](native_games/common/touch_input.c):

### Key Changes

1. **Removed state machine complexity** - No need for complex buffering
2. **Capture coordinates first** - Always track `ABS_X`/`ABS_Y` as they arrive
3. **Use captured coordinates** - When `BTN_TOUCH` is seen, use already-captured values
4. **Wait for sync** - Ensure complete event frame before returning

### Code Snippet

```c
int touch_wait_for_press(TouchInput *touch, int *x, int *y) {
    struct input_event ev;
    int current_x = -1, current_y = -1;
    bool got_press = false;
    bool waiting_for_release = false;
    
    while (1) {
        if (read(touch->fd, &ev, sizeof(ev)) == sizeof(ev)) {
            // Always capture coordinates first (they come before BTN_TOUCH)
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X) {
                    current_x = ev.value;
                } else if (ev.code == ABS_Y) {
                    current_y = ev.value;
                }
            }
            // Then check for touch events
            else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                if (ev.value == 1 && !waiting_for_release) {
                    waiting_for_release = true;
                    got_press = true;
                }
            }
            // Finally check for sync (end of event frame)
            else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                if (got_press && current_x >= 0 && current_y >= 0) {
                    *x = current_x;
                    *y = current_y;
                    return 0;
                }
            }
        }
    }
}
```

## Verification Tools

### 1. Touch Debug Tool

A new diagnostic tool [`touch_debug`](native_games/touch_debug.c) shows the raw event stream:

```bash
/opt/games/touch_debug
```

Output example:
```
TIME        TYPE            CODE            VALUE
----        ----            ----            -----
0.123       EV_ABS          ABS_X           450
0.123       EV_ABS          ABS_Y           240
0.123       EV_KEY          BTN_TOUCH       1
0.123       EV_SYN          SYN_REPORT      0
  --> FRAME #1: Position=(450,240) Touch=PRESS
```

This clearly shows coordinates arriving before the touch press.

### 2. Touch Test Tool

The existing [`touch_test`](native_games/touch_test.c) provides visual feedback:
- Shows numbered targets on screen
- Displays touch coordinates
- Calculates accuracy (distance from target)

## Results

After applying the fix:
- ✅ Touch coordinates are accurate on first touch
- ✅ No more "stuck" coordinates
- ✅ Consistent behavior across all touches
- ✅ Game selector menu works reliably
- ✅ Exit buttons respond correctly

## Deployment

### Compile Updated Code

```bash
cd native_games
./compile_for_roomwizard.sh
```

### Transfer to Device

```bash
# Transfer binaries
scp build/* root@192.168.50.73:/opt/games/

# Set permissions
ssh root@192.168.50.73 'chmod +x /opt/games/*'
```

### Test

```bash
# Test with debug tool
ssh root@192.168.50.73 '/opt/games/touch_debug'

# Test with visual tool
ssh root@192.168.50.73 '/opt/games/touch_test'

# Test game selector
ssh root@192.168.50.73 '/opt/games/game_selector'
```

## Lessons Learned

1. **Read the kernel documentation** - Linux input subsystem has specific event ordering
2. **Use diagnostic tools** - Raw event logging revealed the actual sequence
3. **Don't assume event order** - Events don't always arrive in the "logical" order
4. **Keep it simple** - The fix is simpler than the broken code
5. **Test with real hardware** - Emulators may not reproduce timing issues

## References

- Linux Input Subsystem: `/usr/include/linux/input.h`
- Event types: `EV_ABS`, `EV_KEY`, `EV_SYN`
- Event codes: `ABS_X`, `ABS_Y`, `BTN_TOUCH`, `SYN_REPORT`
- Device: `/dev/input/event0` (RoomWizard touchscreen)

## Related Files

- [`native_games/common/touch_input.c`](native_games/common/touch_input.c) - Fixed implementation
- [`native_games/common/touch_input.h`](native_games/common/touch_input.h) - Header file
- [`native_games/touch_debug.c`](native_games/touch_debug.c) - Event stream debugger
- [`native_games/touch_test.c`](native_games/touch_test.c) - Visual test tool
- [`PROJECT_STATUS.md`](PROJECT_STATUS.md) - Overall project status
