/*
 * Pressure Test - Check if ABS_PRESSURE values are reported
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <signal.h>

volatile int running = 1;

void signal_handler(int sig) {
    running = 0;
}

int main() {
    const char *device = "/dev/input/event0";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Pressure Test\n");
    printf("=============\n");
    printf("Touch the screen with varying pressure...\n");
    printf("Press Ctrl+C to exit\n\n");
    
    int fd = open(device, O_RDONLY);
    if (fd == -1) {
        perror("Error opening device");
        return 1;
    }
    
    struct input_event ev;
    int abs_x = -1, abs_y = -1, abs_pressure = -1;
    int frame_num = 0;
    
    while (running) {
        if (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            // Track values within frame
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X) {
                    abs_x = ev.value;
                } else if (ev.code == ABS_Y) {
                    abs_y = ev.value;
                } else if (ev.code == ABS_PRESSURE) {
                    abs_pressure = ev.value;
                }
            }
            
            // On SYN_REPORT, show frame summary
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                if (abs_x >= 0 || abs_y >= 0 || abs_pressure >= 0) {
                    frame_num++;
                    printf("Frame #%d: ", frame_num);
                    if (abs_x >= 0 && abs_y >= 0) {
                        printf("Position=(%d,%d) ", abs_x, abs_y);
                    }
                    if (abs_pressure >= 0) {
                        printf("Pressure=%d", abs_pressure);
                    } else {
                        printf("Pressure=NONE");
                    }
                    printf("\n");
                }
                
                // Reset frame tracking
                abs_x = -1;
                abs_y = -1;
                abs_pressure = -1;
            }
        }
    }
    
    printf("\nTest complete.\n");
    close(fd);
    return 0;
}
