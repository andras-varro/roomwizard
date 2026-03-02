#ifndef VNC_INPUT_H
#define VNC_INPUT_H

#include <stdint.h>
#include <stdbool.h>
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
} VNCInput;

// Initialize input handler
int vnc_input_init(VNCInput *input, TouchInput *touch, VNCRenderer *renderer, rfbClient *vnc_client);

// Process touch input and send to VNC server
void vnc_input_process(VNCInput *input);

// Send pointer event to VNC server
void vnc_input_send_pointer(VNCInput *input, int x, int y, int button_mask);

// Send key event to VNC server
void vnc_input_send_key(VNCInput *input, uint32_t key, bool down);

// Cleanup
void vnc_input_cleanup(VNCInput *input);

#endif // VNC_INPUT_H
