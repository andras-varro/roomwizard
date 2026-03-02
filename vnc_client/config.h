#ifndef VNC_CONFIG_H
#define VNC_CONFIG_H

// VNC Server Configuration
#define VNC_DEFAULT_HOST "192.168.50.56"
#define VNC_DEFAULT_PORT 5900
#define VNC_PASSWORD ""

// Device Paths
#define FB_DEVICE "/dev/fb0"
#define TOUCH_DEVICE "/dev/input/event0"
#define WATCHDOG_DEVICE "/dev/watchdog"

// Watchdog Configuration
#define WATCHDOG_FEED_INTERVAL_SEC 30

// Display Configuration
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

// Scaling Modes
typedef enum {
    SCALING_LETTERBOX,  // Maintain aspect ratio with black bars
    SCALING_STRETCH,    // Stretch to fill screen
    SCALING_CROP        // Crop to fit
} ScalingMode;

#define DEFAULT_SCALING_MODE SCALING_LETTERBOX

// Visual Feedback
#define SHOW_REMOTE_CURSOR 1
#define TOUCH_FEEDBACK_RADIUS 10
#define TOUCH_FEEDBACK_COLOR 0xFF0000  // Red
#define TOUCH_FEEDBACK_DURATION_MS 200  // 200ms

// Performance Settings
#define TARGET_FPS 30
#define ENABLE_DOUBLE_BUFFER 1

// Debug Settings
#define DEBUG_ENABLED 1

#if DEBUG_ENABLED
#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) do {} while(0)
#endif

// Display Settings
#define SHOW_FPS_COUNTER 1
#define SHOW_CONNECTION_STATUS 1

#endif // VNC_CONFIG_H
