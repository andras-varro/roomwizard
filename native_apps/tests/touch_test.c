/*
 * Touch Diagnostic Tool
 * Shows raw and scaled touch coordinates with visual feedback
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>

volatile int running = 1;

void signal_handler(int sig) {
    running = 0;
}

int main(int argc, char *argv[]) {
    Framebuffer fb;
    TouchInput touch;
    const char *fb_device = "/dev/fb0";
    const char *touch_device = "/dev/input/event0";
    
    if (argc > 1) fb_device = argv[1];
    if (argc > 2) touch_device = argv[2];
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Touch Diagnostic Tool\n");
    printf("=====================\n");
    printf("FB: %s\n", fb_device);
    printf("Touch: %s\n", touch_device);
    printf("\n");
    
    if (fb_init(&fb, fb_device) != 0) {
        fprintf(stderr, "Failed to init framebuffer\n");
        return 1;
    }
    
    if (touch_init(&touch, touch_device) != 0) {
        fprintf(stderr, "Failed to init touch\n");
        fb_close(&fb);
        return 1;
    }
    
    // DON'T set screen size - we want to see raw coordinates
    printf("Screen: %dx%d\n", fb.width, fb.height);
    printf("\nTouch the screen at these locations:\n");
    printf("1. Top-left corner\n");
    printf("2. Top-right corner\n");
    printf("3. Bottom-left corner\n");
    printf("4. Bottom-right corner\n");
    printf("5. Center\n");
    printf("\nPress Ctrl+C to exit\n\n");
    
    // Draw grid on screen
    fb_clear(&fb, COLOR_BLACK);
    
    // Draw crosshairs at corners and center
    int cx = fb.width / 2;
    int cy = fb.height / 2;
    
    // Top-left
    fb_fill_circle(&fb, 50, 50, 20, COLOR_RED);
    fb_draw_text(&fb, 80, 45, "1", COLOR_WHITE, 2);
    
    // Top-right
    fb_fill_circle(&fb, fb.width - 50, 50, 20, COLOR_GREEN);
    fb_draw_text(&fb, fb.width - 100, 45, "2", COLOR_WHITE, 2);
    
    // Bottom-left
    fb_fill_circle(&fb, 50, fb.height - 50, 20, COLOR_BLUE);
    fb_draw_text(&fb, 80, fb.height - 55, "3", COLOR_WHITE, 2);
    
    // Bottom-right
    fb_fill_circle(&fb, fb.width - 50, fb.height - 50, 20, COLOR_YELLOW);
    fb_draw_text(&fb, fb.width - 100, fb.height - 55, "4", COLOR_WHITE, 2);
    
    // Center
    fb_fill_circle(&fb, cx, cy, 20, COLOR_MAGENTA);
    fb_draw_text(&fb, cx + 30, cy - 5, "5", COLOR_WHITE, 2);
    
    // Present initial screen
    fb_swap(&fb);
    
    int touch_count = 0;
    
    while (running) {
        int x, y;
        if (touch_wait_for_press(&touch, &x, &y) == 0) {
            touch_count++;
            
            printf("Touch #%d: RAW(%d, %d)\n", touch_count, x, y);
            
            // Draw a small circle where touched (using raw coordinates)
            fb_fill_circle(&fb, x, y, 5, COLOR_WHITE);
            
            // Present updated screen
            fb_swap(&fb);
            
            // Show which target was closest
            int dist_tl = (x-50)*(x-50) + (y-50)*(y-50);
            int dist_tr = (x-(fb.width-50))*(x-(fb.width-50)) + (y-50)*(y-50);
            int dist_bl = (x-50)*(x-50) + (y-(fb.height-50))*(y-(fb.height-50));
            int dist_br = (x-(fb.width-50))*(x-(fb.width-50)) + (y-(fb.height-50))*(y-(fb.height-50));
            int dist_c = (x-cx)*(x-cx) + (y-cy)*(y-cy);
            
            int min_dist = dist_tl;
            int target = 1;
            if (dist_tr < min_dist) { min_dist = dist_tr; target = 2; }
            if (dist_bl < min_dist) { min_dist = dist_bl; target = 3; }
            if (dist_br < min_dist) { min_dist = dist_br; target = 4; }
            if (dist_c < min_dist) { min_dist = dist_c; target = 5; }
            
            printf("  Closest to target: %d (distance: %d pixels)\n", target, (int)sqrt(min_dist));
            printf("\n");
        }
        
        usleep(10000);
    }
    
    printf("\nTouch test complete. %d touches recorded.\n", touch_count);
    
    touch_close(&touch);
    fb_close(&fb);
    
    return 0;
}
