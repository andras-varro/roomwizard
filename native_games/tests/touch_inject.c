/*
 * Touch Event Injection Utility
 * 
 * Injects synthetic touch events into the Linux input subsystem.
 * Used for automated testing of touch-based applications.
 * 
 * Usage: touch_inject <x> <y> [duration_ms]
 *   x, y: Touch coordinates (0-4095 raw range)
 *   duration_ms: How long to hold touch (default: 100ms)
 * 
 * Example: touch_inject 2048 2048 200  # Center tap for 200ms
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <string.h>
#include <time.h>

#define TOUCH_DEVICE "/dev/input/event0"

// Helper to create and write an input event
static void send_event(int fd, __u16 type, __u16 code, __s32 value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    
    // Note: timestamp is set by kernel, we just zero it
    ev.type = type;
    ev.code = code;
    ev.value = value;
    
    if (write(fd, &ev, sizeof(ev)) < 0) {
        perror("Failed to write event");
    }
}

// Inject a complete touch event sequence
void inject_touch(int x, int y, int duration_ms) {
    int fd = open(TOUCH_DEVICE, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open touch device");
        exit(1);
    }
    
    printf("Injecting touch at (%d, %d) for %dms\n", x, y, duration_ms);
    
    // Touch press sequence
    send_event(fd, EV_ABS, ABS_X, x);
    send_event(fd, EV_ABS, ABS_Y, y);
    send_event(fd, EV_KEY, BTN_TOUCH, 1);  // Press
    send_event(fd, EV_SYN, SYN_REPORT, 0);
    
    // Hold for duration
    usleep(duration_ms * 1000);
    
    // Touch release sequence
    send_event(fd, EV_KEY, BTN_TOUCH, 0);  // Release
    send_event(fd, EV_SYN, SYN_REPORT, 0);
    
    close(fd);
    printf("Touch event injected successfully\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <x> <y> [duration_ms]\n", argv[0]);
        fprintf(stderr, "  x, y: Touch coordinates (0-4095 raw range)\n");
        fprintf(stderr, "  duration_ms: Hold duration (default: 100ms)\n");
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s 2048 2048       # Center tap\n", argv[0]);
        fprintf(stderr, "  %s 400 400 200    # Top-left tap for 200ms\n", argv[0]);
        fprintf(stderr, "  %s 400 3800       # Bottom-left tap (scroll down area)\n", argv[0]);
        return 1;
    }
    
    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    int duration = (argc >= 4) ? atoi(argv[3]) : 100;
    
    // Validate coordinates
    if (x < 0 || x > 4095 || y < 0 || y > 4095) {
        fprintf(stderr, "Error: Coordinates must be in range 0-4095\n");
        return 1;
    }
    
    if (duration < 10 || duration > 5000) {
        fprintf(stderr, "Error: Duration must be between 10-5000ms\n");
        return 1;
    }
    
    inject_touch(x, y, duration);
    
    return 0;
}
