/*
 * Watchdog Feeder - Keeps the hardware watchdog fed
 * Runs in background to prevent system resets
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>

#define WATCHDOG_DEVICE "/dev/watchdog"
#define FEED_INTERVAL 30  // Feed every 30 seconds (timeout is 60s)

volatile bool running = true;

void signal_handler(int sig) {
    printf("Watchdog feeder stopping...\n");
    running = false;
}

int main() {
    int fd;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Open watchdog device
    fd = open(WATCHDOG_DEVICE, O_WRONLY);
    if (fd == -1) {
        perror("Failed to open watchdog device");
        fprintf(stderr, "Warning: Running without watchdog feeding\n");
        return 1;
    }
    
    printf("Watchdog feeder started (feeding every %d seconds)\n", FEED_INTERVAL);
    
    // Main loop - feed the watchdog
    while (running) {
        // Write any character to feed the watchdog
        if (write(fd, "1", 1) != 1) {
            perror("Failed to feed watchdog");
            break;
        }
        
        sleep(FEED_INTERVAL);
    }
    
    // Close watchdog (this may trigger a reset on some systems)
    // Writing 'V' before close disables the watchdog on some systems
    write(fd, "V", 1);
    close(fd);
    
    printf("Watchdog feeder stopped\n");
    return 0;
}
