#ifndef VNC_INPUT_H
#define VNC_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <rfb/rfbclient.h>
#include "../native_games/common/touch_input.h"

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
    
    // Button state
    int button_mask;

    // Exit gesture: long-press top-left corner
    struct timeval exit_touch_start; // when corner touch began
    bool exit_touching;              // currently touching exit zone
    bool exit_requested;             // set true when hold completes
    float exit_progress;             // 0.0-1.0 for visual feedback
} VNCInput;

// Initialize input handler
int vnc_input_init(VNCInput *input, TouchInput *touch, VNCRenderer *renderer, rfbClient *vnc_client);

// Process touch input and send to VNC server
void vnc_input_process(VNCInput *input);

// Send pointer event to VNC server
void vnc_input_send_pointer(VNCInput *input, int x, int y, int button_mask);

// Send key event to VNC server
void vnc_input_send_key(VNCInput *input, uint32_t key, bool down);

// Check if exit was requested (long-press in corner)
bool vnc_input_exit_requested(VNCInput *input);

// Get exit gesture progress (0.0 to 1.0) for visual feedback
float vnc_input_exit_progress(VNCInput *input);

// Cleanup
void vnc_input_cleanup(VNCInput *input);

#endif // VNC_INPUT_H
