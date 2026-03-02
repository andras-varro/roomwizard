/**
 * Backlight Control Utility for RoomWizard
 * 
 * Simple command-line tool to set the backlight brightness.
 * 
 * Usage: ./backlight <percentage>
 *   percentage: 0-100 (0=off, 100=full brightness)
 * 
 * Examples:
 *   ./backlight 50    # Set backlight to 50%
 *   ./backlight 100   # Set backlight to full brightness
 *   ./backlight 0     # Turn off backlight
 */

#include "../common/hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(const char *prog) {
    printf("Usage: %s <percentage>\n", prog);
    printf("\n");
    printf("Set the backlight brightness to the specified percentage.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  percentage    Brightness level (0-100)\n");
    printf("                0   = backlight off\n");
    printf("                100 = full brightness\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s 50     # Set backlight to 50%%\n", prog);
    printf("  %s 100    # Set backlight to full brightness\n", prog);
    printf("  %s 0      # Turn off backlight\n", prog);
}

int main(int argc, char *argv[]) {
    // Check arguments
    if (argc != 2) {
        fprintf(stderr, "Error: Missing brightness argument\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    // Handle help flag
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    // Parse brightness value
    char *endptr;
    long brightness = strtol(argv[1], &endptr, 10);
    
    // Validate input
    if (*endptr != '\0') {
        fprintf(stderr, "Error: Invalid brightness value '%s' (must be a number)\n", argv[1]);
        return 1;
    }
    
    if (brightness < 0 || brightness > 100) {
        fprintf(stderr, "Error: Brightness must be between 0 and 100 (got %ld)\n", brightness);
        return 1;
    }
    
    // Initialize hardware control
    if (hw_init() < 0) {
        fprintf(stderr, "Warning: Hardware initialization reported issues\n");
        fprintf(stderr, "Continuing anyway - operation may fail\n");
    }
    
    // Set backlight
    if (hw_set_backlight((uint8_t)brightness) < 0) {
        fprintf(stderr, "Error: Failed to set backlight brightness\n");
        return 1;
    }
    
    printf("Backlight set to %ld%%\n", brightness);
    return 0;
}
