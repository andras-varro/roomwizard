#ifndef VNC_INPUT_H
#define VNC_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <rfb/rfbclient.h>
#include "../native_apps/common/touch_input.h"
#include "../native_apps/common/input_scan.h"

// Per-kind limit on USB input nodes held open (same as common/gamepad.c).
#define VNC_MAX_PER_KIND   4
#define VNC_MAX_USB_NODES  (2 * VNC_MAX_PER_KIND)

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

    // Every USB keyboard and mouse node held open (common/input_scan.c).
    // All keyboards type into the one session; all mice move the one pointer.
    InputNode usb_nodes[VNC_MAX_USB_NODES];
    int usb_node_count;

    // Button level per usb_nodes[] entry (rfbButton1..3 bits), so the mask
    // sent is the OR across mice: releasing a button on one mouse must not
    // release it for another that is still holding it.  Kept index-aligned
    // with usb_nodes[] when an entry is dropped.
    int usb_node_buttons[VNC_MAX_USB_NODES];

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

    // Hot-plug check (common/input_scan.h): rescan when /dev/input changes
    InputSigGate node_gate;
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
