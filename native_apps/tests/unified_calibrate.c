/*
 * Unified Calibration Utility
 * 
 * Visual calibration tool for:
 * 1. Touch accuracy (4-corner calibration)
 * 2. Bezel obstruction margins (adjustable safe area)
 * 
 * Usage: unified_calibrate
 *   - Follow on-screen instructions
 *   - Touch calibration: tap crosshairs at corners
 *   - Bezel configuration: tap +/- zones to adjust margins
 *   - Saves to /etc/touch_calibration.conf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "../common/framebuffer.h"
#include "../common/touch_input.h"

#define MARGIN 40  // Pixels from edge for calibration points
#define MARGIN_STEP 5  // Pixels to adjust per tap

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

// Draw a filled rectangle
void draw_rect(Framebuffer *fb, int x, int y, int w, int h, uint16_t color) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < fb->width && py >= 0 && py < fb->height) {
                fb_draw_pixel(fb, px, py, color);
            }
        }
    }
}

// Draw a rectangle outline
void draw_rect_outline(Framebuffer *fb, int x, int y, int w, int h, uint16_t color, int thickness) {
    // Top and bottom
    for (int t = 0; t < thickness; t++) {
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px >= 0 && px < fb->width) {
                if (y + t >= 0 && y + t < fb->height)
                    fb_draw_pixel(fb, px, y + t, color);
                if (y + h - 1 - t >= 0 && y + h - 1 - t < fb->height)
                    fb_draw_pixel(fb, px, y + h - 1 - t, color);
            }
        }
    }
    // Left and right
    for (int t = 0; t < thickness; t++) {
        for (int dy = 0; dy < h; dy++) {
            int py = y + dy;
            if (py >= 0 && py < fb->height) {
                if (x + t >= 0 && x + t < fb->width)
                    fb_draw_pixel(fb, x + t, py, color);
                if (x + w - 1 - t >= 0 && x + w - 1 - t < fb->width)
                    fb_draw_pixel(fb, x + w - 1 - t, py, color);
            }
        }
    }
}

// Phase 1: Touch calibration (4 corners)
int calibrate_touch(Framebuffer *fb, TouchInput *touch, int offsets_x[4], int offsets_y[4]) {
    // Define calibration points (corners with margin)
    struct {
        int x, y;
        const char *name;
    } points[] = {
        {MARGIN, MARGIN, "Top-Left"},
        {fb->width - MARGIN, MARGIN, "Top-Right"},
        {MARGIN, fb->height - MARGIN, "Bottom-Left"},
        {fb->width - MARGIN, fb->height - MARGIN, "Bottom-Right"}
    };
    
    printf("\n=== Phase 1: Touch Calibration ===\n");
    
    // Calibrate each corner
    for (int i = 0; i < 4; i++) {
        // Clear and draw crosshair
        fb_clear(fb, COLOR_BLACK);
        draw_crosshair(fb, points[i].x, points[i].y, COLOR_WHITE);
        
        // Draw instruction text
        char msg[50];
        snprintf(msg, sizeof(msg), "TAP CROSSHAIR %d/4", i+1);
        fb_draw_text(fb, 250, 20, msg, COLOR_CYAN, 2);
        
        fb_swap(fb);
        
        printf("[%d/4] %s corner at (%d, %d)\n", i+1, points[i].name, points[i].x, points[i].y);
        printf("Touch the crosshair...\n");
        
        // Wait for touch
        int touch_x, touch_y;
        if (touch_wait_for_press(touch, &touch_x, &touch_y) < 0) {
            fprintf(stderr, "Failed to read touch\n");
            return -1;
        }
        
        // Calculate offset (error)
        offsets_x[i] = points[i].x - touch_x;
        offsets_y[i] = points[i].y - touch_y;
        
        printf("Touched at: (%d, %d)\n", touch_x, touch_y);
        printf("Error: X=%+d, Y=%+d pixels\n", offsets_x[i], offsets_y[i]);
        
        // Visual feedback
        fb_clear(fb, COLOR_BLACK);
        draw_crosshair(fb, points[i].x, points[i].y, COLOR_GREEN);  // Target
        draw_crosshair(fb, touch_x, touch_y, COLOR_RED);  // Actual touch
        fb_swap(fb);
        
        sleep(1);
    }
    
    // Display results
    printf("\nTouch calibration complete!\n");
    printf("Corner errors:\n");
    printf("  TL: X=%+3d, Y=%+3d\n", offsets_x[0], offsets_y[0]);
    printf("  TR: X=%+3d, Y=%+3d\n", offsets_x[1], offsets_y[1]);
    printf("  BL: X=%+3d, Y=%+3d\n", offsets_x[2], offsets_y[2]);
    printf("  BR: X=%+3d, Y=%+3d\n", offsets_x[3], offsets_y[3]);
    
    return 0;
}

// Phase 2: Bezel configuration (visual adjustment)
int configure_bezel(Framebuffer *fb, TouchInput *touch, int *top, int *bottom, int *left, int *right) {
    printf("\n=== Phase 2: Bezel Configuration ===\n");
    printf("Tap +/- zones to adjust bezel margins\n");
    printf("Tap EXIT to finish\n");
    
    // Apply touch calibration for accurate input
    touch_set_calibration(touch,
        touch->calib.top_left_x, touch->calib.top_left_y,
        touch->calib.top_right_x, touch->calib.top_right_y,
        touch->calib.bottom_left_x, touch->calib.bottom_left_y,
        touch->calib.bottom_right_x, touch->calib.bottom_right_y);
    touch_enable_calibration(touch, true);
    
    // Initial margins (estimate based on known ~30-40px obstruction)
    *top = 35;
    *bottom = 35;
    *left = 35;
    *right = 35;
    
    bool done = false;
    int zone_size = 80;  // Size of +/- zones
    
    while (!done) {
        fb_clear(fb, COLOR_BLACK);
        
        // Draw bezel obstruction overlay (checkerboard pattern)
        for (int y = 0; y < *top; y++) {
            for (int x = 0; x < fb->width; x++) {
                if ((x/10 + y/10) % 2 == 0) fb_draw_pixel(fb, x, y, COLOR_RED);
            }
        }
        for (int y = fb->height - *bottom; y < fb->height; y++) {
            for (int x = 0; x < fb->width; x++) {
                if ((x/10 + y/10) % 2 == 0) fb_draw_pixel(fb, x, y, COLOR_RED);
            }
        }
        for (int y = *top; y < fb->height - *bottom; y++) {
            for (int x = 0; x < *left; x++) {
                if ((x/10 + y/10) % 2 == 0) fb_draw_pixel(fb, x, y, COLOR_RED);
            }
            for (int x = fb->width - *right; x < fb->width; x++) {
                if ((x/10 + y/10) % 2 == 0) fb_draw_pixel(fb, x, y, COLOR_RED);
            }
        }
        
        // Draw safe area outline (thick green border)
        draw_rect_outline(fb, *left, *top, 
                         fb->width - *left - *right, 
                         fb->height - *top - *bottom,
                         COLOR_GREEN, 5);
        
        // Calculate zone positions
        int center_x = fb->width / 2;
        int center_y = fb->height / 2;
        
        // TOP zones (+ on left, - on right)
        int top_y = *top / 2;
        int top_plus_x = center_x - zone_size - 10;
        int top_minus_x = center_x + 10;
        draw_rect(fb, top_plus_x, top_y - zone_size/2, zone_size, zone_size, RGB(0, 80, 0));
        draw_rect_outline(fb, top_plus_x, top_y - zone_size/2, zone_size, zone_size, COLOR_CYAN, 2);
        fb_draw_text(fb, top_plus_x + 25, top_y - 15, "+", COLOR_WHITE, 3);
        
        draw_rect(fb, top_minus_x, top_y - zone_size/2, zone_size, zone_size, RGB(80, 0, 0));
        draw_rect_outline(fb, top_minus_x, top_y - zone_size/2, zone_size, zone_size, COLOR_CYAN, 2);
        fb_draw_text(fb, top_minus_x + 30, top_y - 15, "-", COLOR_WHITE, 3);
        
        // BOTTOM zones
        int bottom_y = fb->height - *bottom / 2;
        draw_rect(fb, top_plus_x, bottom_y - zone_size/2, zone_size, zone_size, RGB(0, 80, 0));
        draw_rect_outline(fb, top_plus_x, bottom_y - zone_size/2, zone_size, zone_size, COLOR_CYAN, 2);
        fb_draw_text(fb, top_plus_x + 25, bottom_y - 15, "+", COLOR_WHITE, 3);
        
        draw_rect(fb, top_minus_x, bottom_y - zone_size/2, zone_size, zone_size, RGB(80, 0, 0));
        draw_rect_outline(fb, top_minus_x, bottom_y - zone_size/2, zone_size, zone_size, COLOR_CYAN, 2);
        fb_draw_text(fb, top_minus_x + 30, bottom_y - 15, "-", COLOR_WHITE, 3);
        
        // LEFT zones
        int left_x = *left / 2;
        int left_plus_y = center_y - zone_size - 10;
        int left_minus_y = center_y + 10;
        draw_rect(fb, left_x - zone_size/2, left_plus_y, zone_size, zone_size, RGB(0, 80, 0));
        draw_rect_outline(fb, left_x - zone_size/2, left_plus_y, zone_size, zone_size, COLOR_CYAN, 2);
        fb_draw_text(fb, left_x - 15, left_plus_y + 25, "+", COLOR_WHITE, 3);
        
        draw_rect(fb, left_x - zone_size/2, left_minus_y, zone_size, zone_size, RGB(80, 0, 0));
        draw_rect_outline(fb, left_x - zone_size/2, left_minus_y, zone_size, zone_size, COLOR_CYAN, 2);
        fb_draw_text(fb, left_x - 10, left_minus_y + 25, "-", COLOR_WHITE, 3);
        
        // RIGHT zones
        int right_x = fb->width - *right / 2;
        draw_rect(fb, right_x - zone_size/2, left_plus_y, zone_size, zone_size, RGB(0, 80, 0));
        draw_rect_outline(fb, right_x - zone_size/2, left_plus_y, zone_size, zone_size, COLOR_CYAN, 2);
        fb_draw_text(fb, right_x - 15, left_plus_y + 25, "+", COLOR_WHITE, 3);
        
        draw_rect(fb, right_x - zone_size/2, left_minus_y, zone_size, zone_size, RGB(80, 0, 0));
        draw_rect_outline(fb, right_x - zone_size/2, left_minus_y, zone_size, zone_size, COLOR_CYAN, 2);
        fb_draw_text(fb, right_x - 10, left_minus_y + 25, "-", COLOR_WHITE, 3);
        
        // Center EXIT button
        int exit_size = 120;
        draw_rect(fb, center_x - exit_size/2, center_y - exit_size/2, exit_size, exit_size, RGB(0, 100, 0));
        draw_rect_outline(fb, center_x - exit_size/2, center_y - exit_size/2, exit_size, exit_size, COLOR_WHITE, 3);
        fb_draw_text(fb, center_x - 35, center_y - 15, "EXIT", COLOR_WHITE, 2);
        
        // Draw margin value indicators (yellow bars)
        for (int i = 0; i < *top / 5 && i < 20; i++) {
            draw_rect(fb, *left + 10 + i*6, *top + 10, 4, 20, COLOR_YELLOW);
        }
        for (int i = 0; i < *bottom / 5 && i < 20; i++) {
            draw_rect(fb, *left + 10 + i*6, fb->height - *bottom - 30, 4, 20, COLOR_YELLOW);
        }
        for (int i = 0; i < *left / 5 && i < 20; i++) {
            draw_rect(fb, *left + 10, *top + 40 + i*6, 20, 4, COLOR_YELLOW);
        }
        for (int i = 0; i < *right / 5 && i < 20; i++) {
            draw_rect(fb, fb->width - *right - 30, *top + 40 + i*6, 20, 4, COLOR_YELLOW);
        }
        
        fb_swap(fb);
        
        // Wait for touch
        int touch_x, touch_y;
        if (touch_wait_for_press(touch, &touch_x, &touch_y) < 0) {
            fprintf(stderr, "Failed to read touch\n");
            return -1;
        }
        
        printf("Touch at: (%d, %d)\n", touch_x, touch_y);
        
        // Check which zone was touched
        // TOP +
        if (touch_x >= top_plus_x && touch_x < top_plus_x + zone_size &&
            touch_y >= top_y - zone_size/2 && touch_y < top_y + zone_size/2) {
            *top += MARGIN_STEP;
            if (*top > fb->height / 3) *top = fb->height / 3;
            printf("Top margin: %d\n", *top);
        }
        // TOP -
        else if (touch_x >= top_minus_x && touch_x < top_minus_x + zone_size &&
                 touch_y >= top_y - zone_size/2 && touch_y < top_y + zone_size/2) {
            *top -= MARGIN_STEP;
            if (*top < 0) *top = 0;
            printf("Top margin: %d\n", *top);
        }
        // BOTTOM +
        else if (touch_x >= top_plus_x && touch_x < top_plus_x + zone_size &&
                 touch_y >= bottom_y - zone_size/2 && touch_y < bottom_y + zone_size/2) {
            *bottom += MARGIN_STEP;
            if (*bottom > fb->height / 3) *bottom = fb->height / 3;
            printf("Bottom margin: %d\n", *bottom);
        }
        // BOTTOM -
        else if (touch_x >= top_minus_x && touch_x < top_minus_x + zone_size &&
                 touch_y >= bottom_y - zone_size/2 && touch_y < bottom_y + zone_size/2) {
            *bottom -= MARGIN_STEP;
            if (*bottom < 0) *bottom = 0;
            printf("Bottom margin: %d\n", *bottom);
        }
        // LEFT +
        else if (touch_x >= left_x - zone_size/2 && touch_x < left_x + zone_size/2 &&
                 touch_y >= left_plus_y && touch_y < left_plus_y + zone_size) {
            *left += MARGIN_STEP;
            if (*left > fb->width / 3) *left = fb->width / 3;
            printf("Left margin: %d\n", *left);
        }
        // LEFT -
        else if (touch_x >= left_x - zone_size/2 && touch_x < left_x + zone_size/2 &&
                 touch_y >= left_minus_y && touch_y < left_minus_y + zone_size) {
            *left -= MARGIN_STEP;
            if (*left < 0) *left = 0;
            printf("Left margin: %d\n", *left);
        }
        // RIGHT +
        else if (touch_x >= right_x - zone_size/2 && touch_x < right_x + zone_size/2 &&
                 touch_y >= left_plus_y && touch_y < left_plus_y + zone_size) {
            *right += MARGIN_STEP;
            if (*right > fb->width / 3) *right = fb->width / 3;
            printf("Right margin: %d\n", *right);
        }
        // RIGHT -
        else if (touch_x >= right_x - zone_size/2 && touch_x < right_x + zone_size/2 &&
                 touch_y >= left_minus_y && touch_y < left_minus_y + zone_size) {
            *right -= MARGIN_STEP;
            if (*right < 0) *right = 0;
            printf("Right margin: %d\n", *right);
        }
        // EXIT button
        else if (touch_x >= center_x - exit_size/2 && touch_x < center_x + exit_size/2 &&
                 touch_y >= center_y - exit_size/2 && touch_y < center_y + exit_size/2) {
            done = true;
            printf("Configuration complete!\n");
        }
        
        usleep(200000);  // 200ms debounce
    }
    
    printf("\nBezel margins configured:\n");
    printf("  Top: %d, Bottom: %d, Left: %d, Right: %d\n", *top, *bottom, *left, *right);
    
    return 0;
}

int main(int argc, char *argv[]) {
    Framebuffer fb;
    TouchInput touch;
    
    printf("=== Unified Calibration Utility ===\n");
    printf("This will calibrate touch accuracy and bezel obstruction\n\n");
    
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
    
    // Phase 1: Touch calibration
    int offsets_x[4] = {0};
    int offsets_y[4] = {0};
    
    if (calibrate_touch(&fb, &touch, offsets_x, offsets_y) < 0) {
        fprintf(stderr, "Touch calibration failed\n");
        touch_close(&touch);
        fb_close(&fb);
        return 1;
    }
    
    // Apply touch calibration
    touch_set_calibration(&touch,
        offsets_x[0], offsets_y[0],
        offsets_x[1], offsets_y[1],
        offsets_x[2], offsets_y[2],
        offsets_x[3], offsets_y[3]);
    
    // Phase 2: Bezel configuration
    int bezel_top = 0, bezel_bottom = 0, bezel_left = 0, bezel_right = 0;
    
    if (configure_bezel(&fb, &touch, &bezel_top, &bezel_bottom, &bezel_left, &bezel_right) < 0) {
        fprintf(stderr, "Bezel configuration failed\n");
        touch_close(&touch);
        fb_close(&fb);
        return 1;
    }
    
    // Update calibration structure with bezel margins
    touch.calib.bezel_top = bezel_top;
    touch.calib.bezel_bottom = bezel_bottom;
    touch.calib.bezel_left = bezel_left;
    touch.calib.bezel_right = bezel_right;
    
    // Save complete calibration
    const char *calib_file = "/etc/touch_calibration.conf";
    if (touch_save_calibration(&touch, calib_file) == 0) {
        printf("\n✓ Calibration saved to: %s\n", calib_file);
        printf("  Applications will automatically load this configuration\n");
        
        // Display final summary
        fb_clear(&fb, COLOR_BLACK);
        
        // Draw success message
        fb_draw_text(&fb, 200, 180, "CALIBRATION SAVED!", COLOR_GREEN, 3);
        
        // Draw margin values
        char summary[100];
        snprintf(summary, sizeof(summary), "T:%d B:%d L:%d R:%d", bezel_top, bezel_bottom, bezel_left, bezel_right);
        fb_draw_text(&fb, 250, 250, summary, COLOR_YELLOW, 2);
        
        fb_swap(&fb);
        sleep(3);
    } else {
        fprintf(stderr, "\n✗ Failed to save calibration file\n");
        fprintf(stderr, "  You may need to run as root: sudo %s\n", argv[0]);
        
        fb_clear(&fb, COLOR_BLACK);
        fb_draw_text(&fb, 150, 200, "ERROR: FAILED TO SAVE!", COLOR_RED, 2);
        fb_draw_text(&fb, 200, 250, "RUN AS ROOT", COLOR_YELLOW, 2);
        fb_swap(&fb);
        sleep(3);
    }
    
    // Cleanup
    touch_close(&touch);
    fb_close(&fb);
    
    printf("\nCalibration complete!\n");
    return 0;
}
