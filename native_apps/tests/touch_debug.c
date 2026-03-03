/*
 * Touch Event Stream Debugger
 * Shows raw Linux input events in real-time to diagnose touch issues
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <signal.h>
#include <time.h>

volatile int running = 1;

void signal_handler(int sig) {
    running = 0;
}

const char* event_type_name(int type) {
    switch(type) {
        case EV_SYN: return "EV_SYN";
        case EV_KEY: return "EV_KEY";
        case EV_REL: return "EV_REL";
        case EV_ABS: return "EV_ABS";
        case EV_MSC: return "EV_MSC";
        default: return "UNKNOWN";
    }
}

const char* event_code_name(int type, int code) {
    if (type == EV_ABS) {
        switch(code) {
            case ABS_X: return "ABS_X";
            case ABS_Y: return "ABS_Y";
            case ABS_PRESSURE: return "ABS_PRESSURE";
            default: return "ABS_?";
        }
    } else if (type == EV_KEY) {
        switch(code) {
            case BTN_TOUCH: return "BTN_TOUCH";
            default: return "BTN_?";
        }
    } else if (type == EV_SYN) {
        switch(code) {
            case SYN_REPORT: return "SYN_REPORT";
            default: return "SYN_?";
        }
    }
    return "?";
}

int main(int argc, char *argv[]) {
    const char *device = "/dev/input/event0";
    
    if (argc > 1) {
        device = argv[1];
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Touch Event Stream Debugger\n");
    printf("============================\n");
    printf("Device: %s\n", device);
    printf("\nTouch the screen to see event stream...\n");
    printf("Press Ctrl+C to exit\n\n");
    printf("%-12s %-15s %-15s %s\n", "TIME", "TYPE", "CODE", "VALUE");
    printf("%-12s %-15s %-15s %s\n", "----", "----", "----", "-----");
    
    int fd = open(device, O_RDONLY);
    if (fd == -1) {
        perror("Error opening device");
        return 1;
    }
    
    struct input_event ev;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int frame_num = 0;
    int abs_x = -1, abs_y = -1;
    int btn_touch = -1;
    
    while (running) {
        if (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            // Track values within frame
            if (ev.type == EV_ABS && ev.code == ABS_X) {
                abs_x = ev.value;
            } else if (ev.type == EV_ABS && ev.code == ABS_Y) {
                abs_y = ev.value;
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                btn_touch = ev.value;
            }
            
            // On SYN_REPORT, show frame summary (only if frame had data)
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                // Only show frames with actual touch data
                if (abs_x >= 0 || abs_y >= 0 || btn_touch >= 0) {
                    struct timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    double elapsed = (now.tv_sec - start.tv_sec) +
                                   (now.tv_nsec - start.tv_nsec) / 1000000000.0;
                    
                    frame_num++;
                    printf("\n%8.3f  FRAME #%d: ", elapsed, frame_num);
                    if (abs_x >= 0 && abs_y >= 0) {
                        printf("Position=(%d,%d) ", abs_x, abs_y);
                    }
                    if (btn_touch >= 0) {
                        printf("Touch=%s", btn_touch ? "PRESS" : "RELEASE");
                    }
                    printf("\n");
                }
                
                // Reset frame tracking
                abs_x = -1;
                abs_y = -1;
                btn_touch = -1;
            }
        }
    }
    
    printf("\nDebug session complete.\n");
    close(fd);
    return 0;
}
