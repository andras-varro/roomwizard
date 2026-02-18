/*
 * Touch Calibration Utility
 * 
 * Displays crosshairs at screen corners and measures touch accuracy.
 * Calculates calibration offsets to correct resistive touchscreen errors.
 * 
 * Usage: touch_calibrate
 *   - Touch each crosshair when prompted
 *   - Outputs calibration values for touch_set_calibration()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../common/framebuffer.h"
#include "../common/touch_input.h"

#define MARGIN 40  // Pixels from edge for calibration points

// Draw a crosshair at the specified position
void draw_crosshair(Framebuffer *fb, int x, int y, uint16_t color) {
    int size = 20;
    // Horizontal line
    for (int i = -size; i <= size; i++) {
        fb_draw_pixel(fb, x + i, y, color);
    }
    // Vertical line
    for (int i = -size; i <= size; i++) {
        fb_draw_pixel(fb, x, y + i, color);
    }
    // Center dot
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            fb_draw_pixel(fb, x + dx, y + dy, color);
        }
    }
}

// Draw text instruction
void draw_text(Framebuffer *fb, int x, int y, const char *text, uint16_t color) {
    // Simple text rendering - just display message
    printf("%s\n", text);
}

int main(int argc, char *argv[]) {
    Framebuffer fb;
    TouchInput touch;
    
    printf("=== Touch Calibration Utility ===\n");
    printf("This will measure touchscreen accuracy at corners\n\n");
    
    // Initialize framebuffer
    if (fb_init(&fb, "/dev/fb0") < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    
    // Initialize touch input
    if (touch_init(&touch, "/dev/input/event0") < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&fb);
        return 1;
    }
    touch_set_screen_size(&touch, fb.width, fb.height);
    
    // Define calibration points (corners with margin)
    struct {
        int x, y;
        const char *name;
    } points[] = {
        {MARGIN, MARGIN, "Top-Left"},
        {fb.width - MARGIN, MARGIN, "Top-Right"},
        {MARGIN, fb.height - MARGIN, "Bottom-Left"},
        {fb.width - MARGIN, fb.height - MARGIN, "Bottom-Right"}
    };
    
    int offsets_x[4] = {0};
    int offsets_y[4] = {0};
    
    // Clear screen
    fb_clear(&fb, 0x0000);  // Black
    fb_swap(&fb);
    
    // Calibrate each corner
    for (int i = 0; i < 4; i++) {
        // Clear and draw crosshair
        fb_clear(&fb, 0x0000);
        draw_crosshair(&fb, points[i].x, points[i].y, 0xFFFF);  // White
        
        // Draw instruction text (centered at top)
        char msg[100];
        snprintf(msg, sizeof(msg), "Touch the %s crosshair", points[i].name);
        
        fb_swap(&fb);
        
        printf("\n[%d/4] %s corner at (%d, %d)\n", i+1, points[i].name, points[i].x, points[i].y);
        printf("Touch the crosshair...\n");
        
        // Wait for touch
        int touch_x, touch_y;
        if (touch_wait_for_press(&touch, &touch_x, &touch_y) < 0) {
            fprintf(stderr, "Failed to read touch\n");
            continue;
        }
        
        // Calculate offset (error)
        offsets_x[i] = points[i].x - touch_x;
        offsets_y[i] = points[i].y - touch_y;
        
        printf("Touched at: (%d, %d)\n", touch_x, touch_y);
        printf("Error: X=%+d, Y=%+d pixels\n", offsets_x[i], offsets_y[i]);
        
        // Visual feedback
        fb_clear(&fb, 0x0000);
        draw_crosshair(&fb, points[i].x, points[i].y, 0x07E0);  // Green - target
        draw_crosshair(&fb, touch_x, touch_y, 0xF800);  // Red - actual touch
        fb_swap(&fb);
        
        sleep(1);
    }
    
    // Display results
    fb_clear(&fb, 0x0000);
    fb_swap(&fb);
    
    printf("\n=== Calibration Results ===\n");
    printf("Corner errors measured:\n");
    printf("  Top-Left:     X=%+3d, Y=%+3d\n", offsets_x[0], offsets_y[0]);
    printf("  Top-Right:    X=%+3d, Y=%+3d\n", offsets_x[1], offsets_y[1]);
    printf("  Bottom-Left:  X=%+3d, Y=%+3d\n", offsets_x[2], offsets_y[2]);
    printf("  Bottom-Right: X=%+3d, Y=%+3d\n", offsets_x[3], offsets_y[3]);
    
    printf("\nCalibration code:\n");
    printf("touch_set_calibration(&touch,\n");
    printf("    %d, %d,  // top-left\n", offsets_x[0], offsets_y[0]);
    printf("    %d, %d,  // top-right\n", offsets_x[1], offsets_y[1]);
    printf("    %d, %d,  // bottom-left\n", offsets_x[2], offsets_y[2]);
    printf("    %d, %d); // bottom-right\n", offsets_x[3], offsets_y[3]);
    printf("touch_enable_calibration(&touch, true);\n");
    
    // Calculate average error
    int avg_error_x = (abs(offsets_x[0]) + abs(offsets_x[1]) + abs(offsets_x[2]) + abs(offsets_x[3])) / 4;
    int avg_error_y = (abs(offsets_y[0]) + abs(offsets_y[1]) + abs(offsets_y[2]) + abs(offsets_y[3])) / 4;
    printf("\nAverage error: X=%d, Y=%d pixels\n", avg_error_x, avg_error_y);
    
    // Save calibration to file
    touch_set_calibration(&touch,
        offsets_x[0], offsets_y[0],
        offsets_x[1], offsets_y[1],
        offsets_x[2], offsets_y[2],
        offsets_x[3], offsets_y[3]);
    
    const char *calib_file = "/etc/touch_calibration.conf";
    if (touch_save_calibration(&touch, calib_file) == 0) {
        printf("\n✓ Calibration saved to: %s\n", calib_file);
        printf("  Applications will automatically load this calibration on startup\n");
    } else {
        fprintf(stderr, "\n✗ Failed to save calibration file\n");
        fprintf(stderr, "  You may need to run as root: sudo %s\n", argv[0]);
    }
    
    // Cleanup
    touch_close(&touch);
    fb_close(&fb);
    
    printf("\nCalibration complete!\n");
    return 0;
}
