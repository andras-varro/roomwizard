#ifndef VNC_RENDERER_H
#define VNC_RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include "../native_games/common/framebuffer.h"
#include "config.h"

// VNC Renderer structure
typedef struct VNCRenderer {
    Framebuffer *fb;
    
    // Remote display dimensions
    int remote_width;
    int remote_height;
    
    // Scaling parameters
    ScalingMode scaling_mode;
    float scale;
    float scale_factor;  // Alias for scale
    int offset_x;
    int offset_y;
    int scaled_width;
    int scaled_height;
    
    // Touch feedback
    bool show_touch_feedback;
    int touch_x;
    int touch_y;
    uint32_t touch_time;
    
    // Performance tracking
    uint32_t frame_count;
    uint32_t last_fps_time;
    int current_fps;
} VNCRenderer;

// Initialize renderer
int vnc_renderer_init(VNCRenderer *renderer, Framebuffer *fb);

// Set remote display size and calculate scaling
void vnc_renderer_set_remote_size(VNCRenderer *renderer, int width, int height);

// Render a frame from VNC framebuffer
void vnc_renderer_render_frame(VNCRenderer *renderer, uint8_t *pixels, 
                                int width, int height, int bytes_per_pixel);

// Present the rendered frame (swap buffers)
void vnc_renderer_present(VNCRenderer *renderer);

// Draw touch feedback
void vnc_renderer_draw_touch_feedback(VNCRenderer *renderer, int x, int y);

// Cleanup renderer
void vnc_renderer_cleanup(VNCRenderer *renderer);

#endif // VNC_RENDERER_H
