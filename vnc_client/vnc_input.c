#include "vnc_input.h"
#include "vnc_renderer.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

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
        /*
         * Exit gesture: long-press in top-left corner.
         * Check raw screen coordinates (before VNC mapping) so the
         * exit zone works even if the touch lands in the letterbox border.
         */
        bool in_exit_zone = (state.x < EXIT_ZONE_SIZE && state.y < EXIT_ZONE_SIZE);

        if (in_exit_zone) {
            if (!input->exit_touching) {
                /* Entering exit zone — start timer */
                input->exit_touching = true;
                gettimeofday(&input->exit_touch_start, NULL);
                DEBUG_PRINT("Exit zone: touch started");
            } else {
                /* Already in exit zone — check elapsed time */
                struct timeval now;
                gettimeofday(&now, NULL);
                long elapsed_ms = (now.tv_sec  - input->exit_touch_start.tv_sec) * 1000L
                                + (now.tv_usec - input->exit_touch_start.tv_usec) / 1000L;
                input->exit_progress = (float)elapsed_ms / (float)EXIT_HOLD_MS;
                if (input->exit_progress > 1.0f) input->exit_progress = 1.0f;

                if (elapsed_ms >= EXIT_HOLD_MS) {
                    input->exit_requested = true;
                    DEBUG_PRINT("Exit gesture completed (%ld ms)", elapsed_ms);
                }
            }
            /* Don't send VNC pointer events while in exit zone */
            input->was_pressed = true;
            return;
        } else {
            /* Touch moved out of exit zone — reset */
            if (input->exit_touching) {
                input->exit_touching = false;
                input->exit_progress = 0.0f;
            }
        }

        // Convert screen coordinates to remote coordinates
        int remote_x, remote_y;
        if (vnc_renderer_screen_to_remote(input->renderer, state.x, state.y, 
                                         &remote_x, &remote_y)) {
            // Check if this is a new press or continued press
            if (!input->was_pressed) {
                // New press - button down
                input->button_mask = rfbButton1Mask;  // Left mouse button
                vnc_input_send_pointer(input, remote_x, remote_y, input->button_mask);
                
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
    } else {
        if (input->was_pressed) {
            // Touch released - button up
            input->button_mask = 0;
            vnc_input_send_pointer(input, input->last_x, input->last_y, input->button_mask);
            
            DEBUG_PRINT("Touch up at remote (%d,%d)", input->last_x, input->last_y);
        }
        /* Reset exit zone state on release */
        if (input->exit_touching) {
            input->exit_touching = false;
            input->exit_progress = 0.0f;
        }
    }
    
    input->was_pressed = (state.held || state.pressed);
}

bool vnc_input_exit_requested(VNCInput *input) {
    return input ? input->exit_requested : false;
}

float vnc_input_exit_progress(VNCInput *input) {
    return input ? input->exit_progress : 0.0f;
}

void vnc_input_cleanup(VNCInput *input) {
    if (!input) return;
    
    DEBUG_PRINT("Input handler cleanup");
    // Nothing to free currently
}
