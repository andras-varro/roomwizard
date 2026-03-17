#ifndef VNC_INPUT_H
#define VNC_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <rfb/rfbclient.h>
#include "../native_apps/common/touch_input.h"

// Forward declaration
typedef struct VNCRenderer VNCRenderer;

typedef struct {
    TouchInput *touch;
    VNCRenderer *renderer;
    rfbClient *vnc_client;
    
    // Last known touch state
    int last_x;
    int last_y;
    bool was_pressed;
    
    // Touch button state
    int button_mask;

    // Exit gesture: long-press top-left corner
    struct timeval exit_touch_start; // when corner touch began
    bool exit_touching;              // currently touching exit zone
    bool exit_requested;             // set true when hold completes
    float exit_progress;             // 0.0-1.0 for visual feedback

    // USB keyboard evdev fd (-1 if not connected)
    int keyboard_fd;

    // USB mouse evdev fd (-1 if not connected)
    int mouse_fd;

    // Mouse absolute position in remote desktop coordinates
    int mouse_abs_x;
    int mouse_abs_y;

    // Mouse button mask (VNC rfbButton1Mask | rfbButton2Mask | rfbButton3Mask)
    int mouse_button_mask;

    // Remote desktop dimensions (for mouse clamping)
    int remote_width;
    int remote_height;

    // Mouse acceleration settings
    float mouse_sensitivity;    // Default 1.5
    float mouse_acceleration;   // Default 2.0
    int mouse_low_threshold;    // Default 3
    int mouse_high_threshold;   // Default 15

    // Device rescan timer
    uint32_t last_device_scan;
} VNCInput;

// Initialize input handler
int vnc_input_init(VNCInput *input, TouchInput *touch, VNCRenderer *renderer, rfbClient *vnc_client);

// Process touch input and send to VNC server
void vnc_input_process(VNCInput *input);

// Send pointer event to VNC server
void vnc_input_send_pointer(VNCInput *input, int x, int y, int button_mask);

// Send key event to VNC server
void vnc_input_send_key(VNCInput *input, uint32_t key, bool down);

// Scan /dev/input/event* for USB keyboard and mouse
void vnc_input_scan_devices(VNCInput *input);

// Close USB keyboard and mouse fds
void vnc_input_close_usb_devices(VNCInput *input);

// Set remote desktop dimensions (for mouse coordinate clamping)
void vnc_input_set_remote_size(VNCInput *input, int width, int height);

// Check if exit was requested (long-press in corner)
bool vnc_input_exit_requested(VNCInput *input);

// Get exit gesture progress (0.0 to 1.0) for visual feedback
float vnc_input_exit_progress(VNCInput *input);

// Cleanup
void vnc_input_cleanup(VNCInput *input);

#endif // VNC_INPUT_H
