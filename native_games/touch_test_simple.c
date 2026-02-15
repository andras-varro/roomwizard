#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main() {
    int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        perror("Failed to open /dev/input/event0");
        return 1;
    }
    
    printf("Touch device opened successfully\n");
    printf("Waiting for touch events (touch the screen)...\n");
    
    struct input_event ev;
    int count = 0;
    
    for (int i = 0; i < 100; i++) {  // Poll for 10 seconds
        while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            count++;
            printf("Event: type=%d code=%d value=%d\n", ev.type, ev.code, ev.value);
            
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X) {
                    printf("  -> X coordinate: %d\n", ev.value);
                } else if (ev.code == ABS_Y) {
                    printf("  -> Y coordinate: %d\n", ev.value);
                }
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                printf("  -> Touch %s\n", ev.value ? "PRESSED" : "RELEASED");
            } else if (ev.type == EV_SYN) {
                printf("  -> Sync\n");
            }
        }
        usleep(100000);  // 100ms
    }
    
    printf("\nTotal events read: %d\n", count);
    close(fd);
    return 0;
}
