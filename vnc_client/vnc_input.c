#include "vnc_input.h"
#include "vnc_renderer.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

int vnc_input_init(VNCInput *input, TouchInput *touch, VNCRenderer *renderer, rfbClient *vnc_client) {
    if (!input || !touch || !renderer || !vnc_client) {
        return -1;
    }
    
    memset(input, 0, sizeof(VNCInput));
    input->touch = touch;
    input->renderer = renderer;
    input->vnc_client = vnc_client;
    input->button_mask = 0;
    input->was_pressed = false;
    
    DEBUG_PRINT("Input handler initialized");
    return 0;
}

void vnc_input_send_pointer(VNCInput *input, int x, int y, int button_mask) {
    if (!input || !input->vnc_client) return;
    
    // Send pointer event to VNC server
    SendPointerEvent(input->vnc_client, x, y, button_mask);
    
    DEBUG_PRINT("Pointer event: (%d,%d) buttons=%d", x, y, button_mask);
}

void vnc_input_send_key(VNCInput *input, uint32_t key, bool down) {
    if (!input || !input->vnc_client) return;
    
    // Send key event to VNC server
    SendKeyEvent(input->vnc_client, key, down ? TRUE : FALSE);
    
    DEBUG_PRINT("Key event: key=%u down=%d", key, down);
}

void vnc_input_process(VNCInput *input) {
    if (!input || !input->touch || !input->renderer) return;
    
    // Poll for touch events
    touch_poll(input->touch);
    
    // Get current touch state
    TouchState state = touch_get_state(input->touch);
    
    if (state.held || state.pressed) {
        // Convert screen coordinates to remote coordinates
        int remote_x, remote_y;
        if (vnc_renderer_screen_to_remote(input->renderer, state.x, state.y, 
                                         &remote_x, &remote_y)) {
            // Check if this is a new press or continued press
            if (!input->was_pressed) {
                // New press - button down
                input->button_mask = rfbButton1Mask;  // Left mouse button
                vnc_input_send_pointer(input, remote_x, remote_y, input->button_mask);
                
                // Show touch feedback
                vnc_renderer_show_touch(input->renderer, state.x, state.y);
                
                DEBUG_PRINT("Touch down at screen (%d,%d) -> remote (%d,%d)", 
                           state.x, state.y, remote_x, remote_y);
            } else if (remote_x != input->last_x || remote_y != input->last_y) {
                // Drag - update position with button still down
                vnc_input_send_pointer(input, remote_x, remote_y, input->button_mask);
                
                DEBUG_PRINT("Touch drag to remote (%d,%d)", remote_x, remote_y);
            }
            
            input->last_x = remote_x;
            input->last_y = remote_y;
        }
    } else if (input->was_pressed) {
        // Touch released - button up
        input->button_mask = 0;
        vnc_input_send_pointer(input, input->last_x, input->last_y, input->button_mask);
        
        DEBUG_PRINT("Touch up at remote (%d,%d)", input->last_x, input->last_y);
    }
    
    input->was_pressed = (state.held || state.pressed);
}

void vnc_input_cleanup(VNCInput *input) {
    if (!input) return;
    
    DEBUG_PRINT("Input handler cleanup");
    // Nothing to free currently
}
