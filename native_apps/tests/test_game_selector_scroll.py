#!/usr/bin/env python3
"""
Automated Test: Game Selector Scrolling

This script validates the game selector's scrolling functionality by:
1. Starting game_selector on the RoomWizard device
2. Capturing initial framebuffer screenshot
3. Injecting a touch event to scroll down
4. Capturing second screenshot
5. Comparing images to detect visual changes
6. Validating that scrolling occurred

Usage: python test_game_selector_scroll.py [device_ip]
"""

import subprocess
import time
import sys
from pathlib import Path
import numpy as np
from PIL import Image

# Configuration
DEVICE_IP = "192.168.50.73"
DEVICE_USER = "root"
FB_DEVICE = "/dev/fb0"
TOUCH_INJECT = "/opt/games/touch_inject"
GAME_SELECTOR = "/opt/games/game_selector"

# Screen dimensions
WIDTH = 800
HEIGHT = 480
# Auto-detect BPP based on file size
# RGB565 = 2 bytes, RGBA8888 = 4 bytes

# Touch coordinates for scroll down (bottom area, raw 0-4095 range)
# From game_selector debug: down arrow area is y between 300-400 (screen coords)
# Target middle of this area: y=350 screen
# Convert to raw: y=350 screen -> (350 * 4095) / 480 = ~2985 raw
SCROLL_DOWN_X = 2048  # Center (x=400 screen -> 2048 raw)
SCROLL_DOWN_Y = 2985  # Middle of down arrow detection area (y=350 screen)
TOUCH_DURATION = 200  # ms - longer duration to ensure detection


def run_ssh_command(command, capture_output=True):
    """Execute command on device via SSH"""
    ssh_cmd = ["ssh", f"{DEVICE_USER}@{DEVICE_IP}", command]
    
    if capture_output:
        result = subprocess.run(ssh_cmd, capture_output=True, text=True)
        return result.stdout, result.stderr, result.returncode
    else:
        subprocess.Popen(ssh_cmd)
        return None, None, 0


def capture_framebuffer(output_path):
    """Capture framebuffer from device and save locally"""
    print(f"Capturing framebuffer to {output_path}...")
    
    # Use subprocess with shell=True for proper redirection
    cmd = f'ssh {DEVICE_USER}@{DEVICE_IP} "cat {FB_DEVICE}"'
    result = subprocess.run(cmd, shell=True, capture_output=True)
    
    if result.returncode != 0:
        print(f"Error capturing framebuffer: {result.stderr.decode()}")
        return False
    
    # Write captured data to file
    with open(output_path, 'wb') as f:
        f.write(result.stdout)
    
    # Verify file size and detect format
    file_size = Path(output_path).stat().st_size
    print(f"Captured {file_size} bytes")
    
    return True


def convert_fb_to_png(raw_path, png_path):
    """Convert raw framebuffer to PNG (auto-detect RGB565 or RGBA8888)"""
    print(f"Converting {raw_path} to {png_path}...")
    
    # Read raw framebuffer data
    with open(raw_path, 'rb') as f:
        raw_data = f.read()
    
    file_size = len(raw_data)
    expected_rgb565 = WIDTH * HEIGHT * 2
    expected_rgba8888 = WIDTH * HEIGHT * 4
    
    # Auto-detect format
    if file_size == expected_rgba8888:
        print("Detected RGBA8888 format (32-bit)")
        # Convert RGBA8888 to RGB
        pixels = []
        for i in range(0, len(raw_data), 4):
            r = raw_data[i]
            g = raw_data[i+1]
            b = raw_data[i+2]
            # Skip alpha channel (raw_data[i+3])
            pixels.append((r, g, b))
    elif file_size == expected_rgb565:
        print("Detected RGB565 format (16-bit)")
        # Convert RGB565 to RGB888
        pixels = []
        for i in range(0, len(raw_data), 2):
            # Read 16-bit value (little-endian)
            pixel = raw_data[i] | (raw_data[i+1] << 8)
            
            # Extract RGB components (RGB565)
            r = ((pixel >> 11) & 0x1F) << 3  # 5 bits -> 8 bits
            g = ((pixel >> 5) & 0x3F) << 2   # 6 bits -> 8 bits
            b = (pixel & 0x1F) << 3          # 5 bits -> 8 bits
            
            pixels.append((r, g, b))
    else:
        print(f"Error: Unexpected framebuffer size {file_size}")
        print(f"Expected {expected_rgb565} (RGB565) or {expected_rgba8888} (RGBA8888)")
        return None
    
    # Create image
    img = Image.new('RGB', (WIDTH, HEIGHT))
    img.putdata(pixels)
    img.save(png_path)
    
    return img


def compare_images(img1, img2, threshold=0.01):
    """
    Compare two images and return difference metrics
    
    Returns:
        dict with 'changed_pixels', 'total_pixels', 'percent_changed', 'is_different'
    """
    print("Comparing images...")
    
    # Convert to numpy arrays
    arr1 = np.array(img1)
    arr2 = np.array(img2)
    
    # Calculate pixel-wise difference
    diff = np.abs(arr1.astype(int) - arr2.astype(int))
    
    # Count pixels with any color channel difference > 10 (to ignore minor noise)
    changed_mask = np.any(diff > 10, axis=2)
    changed_pixels = np.sum(changed_mask)
    total_pixels = WIDTH * HEIGHT
    percent_changed = (changed_pixels / total_pixels) * 100
    
    # Create difference visualization
    diff_img = Image.fromarray((changed_mask * 255).astype(np.uint8), mode='L')
    diff_img.save('test_diff.png')
    print(f"Difference map saved to test_diff.png")
    
    result = {
        'changed_pixels': changed_pixels,
        'total_pixels': total_pixels,
        'percent_changed': percent_changed,
        'is_different': percent_changed > (threshold * 100)
    }
    
    return result


def start_game_selector():
    """Start game_selector on device (non-blocking)"""
    print("Starting game_selector...")
    run_ssh_command(GAME_SELECTOR, capture_output=False)
    time.sleep(3)  # Wait longer for app to fully start and enter event loop


def stop_game_selector():
    """Stop game_selector on device and clear screen"""
    print("Stopping game_selector...")
    run_ssh_command("killall game_selector")
    time.sleep(0.5)
    
    # Force clear the framebuffer to ensure LCD updates
    print("Clearing framebuffer...")
    run_ssh_command("dd if=/dev/zero of=/dev/fb0 bs=1536000 count=1 2>/dev/null")
    time.sleep(0.2)


def inject_touch(x, y, duration):
    """Inject touch event on device"""
    print(f"Injecting touch at ({x}, {y}) for {duration}ms...")
    cmd = f"{TOUCH_INJECT} {x} {y} {duration}"
    stdout, stderr, returncode = run_ssh_command(cmd)
    
    if returncode != 0:
        print(f"Error injecting touch: {stderr}")
        return False
    
    print(f"Touch injected: {stdout.strip()}")
    time.sleep(0.5)  # Wait for UI to update
    return True


def main():
    """Main test execution"""
    print("=" * 60)
    print("Game Selector Scrolling Test")
    print("=" * 60)
    
    # Override device IP if provided
    if len(sys.argv) > 1:
        global DEVICE_IP
        DEVICE_IP = sys.argv[1]
        print(f"Using device IP: {DEVICE_IP}")
    
    try:
        # Step 1: Start game_selector
        start_game_selector()
        
        # Step 2: Capture initial screenshot
        if not capture_framebuffer("test_before.raw"):
            print("Failed to capture initial framebuffer")
            return False
        
        img_before = convert_fb_to_png("test_before.raw", "test_before.png")
        print("Initial screenshot saved to test_before.png")
        
        # Step 3: Inject scroll down touch
        if not inject_touch(SCROLL_DOWN_X, SCROLL_DOWN_Y, TOUCH_DURATION):
            print("Failed to inject touch event")
            return False
        
        # Step 4: Capture second screenshot
        if not capture_framebuffer("test_after.raw"):
            print("Failed to capture second framebuffer")
            return False
        
        img_after = convert_fb_to_png("test_after.raw", "test_after.png")
        print("Second screenshot saved to test_after.png")
        
        # Step 5: Compare images
        diff_result = compare_images(img_before, img_after)
        
        # Step 6: Report results
        print("\n" + "=" * 60)
        print("TEST RESULTS")
        print("=" * 60)
        print(f"Changed pixels: {diff_result['changed_pixels']:,} / {diff_result['total_pixels']:,}")
        print(f"Percent changed: {diff_result['percent_changed']:.2f}%")
        print(f"Scrolling detected: {'YES [OK]' if diff_result['is_different'] else 'NO [FAIL]'}")
        
        if diff_result['is_different']:
            print("\n[OK] TEST PASSED: Scrolling functionality works correctly!")
            print("  Visual changes detected in game list position.")
        else:
            print("\n[FAIL] TEST FAILED: No scrolling detected!")
            print("  Images are identical or changes too minor.")
        
        print("\nGenerated files:")
        print("  - test_before.png: Initial state")
        print("  - test_after.png: After scroll")
        print("  - test_diff.png: Difference map (white = changed pixels)")
        
        return diff_result['is_different']
        
    except Exception as e:
        print(f"\nTest failed with exception: {e}")
        import traceback
        traceback.print_exc()
        return False
        
    finally:
        # Cleanup: stop game_selector
        stop_game_selector()
        print("\nTest complete.")


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
