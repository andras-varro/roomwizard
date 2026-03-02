#include "vnc_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

// Helper function to get current time in milliseconds
static uint32_t get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// Helper function to calculate min
static inline float min_float(float a, float b) {
    return (a < b) ? a : b;
}

int vnc_renderer_init(VNCRenderer *renderer, Framebuffer *fb) {
    if (!renderer || !fb) {
        return -1;
    }
    
    memset(renderer, 0, sizeof(VNCRenderer));
    renderer->fb = fb;
    renderer->scaling_mode = DEFAULT_SCALING_MODE;
    renderer->last_fps_time = get_time_ms();
    
    DEBUG_PRINT("Renderer initialized: %dx%d screen", SCREEN_WIDTH, SCREEN_HEIGHT);
    return 0;
}

void vnc_renderer_set_remote_size(VNCRenderer *renderer, int width, int height) {
    if (!renderer) return;
    
    renderer->remote_width = width;
    renderer->remote_height = height;
    
    // Calculate scaling based on mode
    switch (renderer->scaling_mode) {
        case SCALING_LETTERBOX: {
            // Maintain aspect ratio, add black bars if needed
            float scale_x = (float)SCREEN_WIDTH / width;
            float scale_y = (float)SCREEN_HEIGHT / height;
            renderer->scale_factor = min_float(scale_x, scale_y);
            
            renderer->scaled_width = (int)(width * renderer->scale_factor);
            renderer->scaled_height = (int)(height * renderer->scale_factor);
            renderer->offset_x = (SCREEN_WIDTH - renderer->scaled_width) / 2;
            renderer->offset_y = (SCREEN_HEIGHT - renderer->scaled_height) / 2;
            break;
        }
        
        case SCALING_STRETCH: {
            // Stretch to fill entire screen
            renderer->scale_factor = 1.0f;  // Not used in stretch mode
            renderer->scaled_width = SCREEN_WIDTH;
            renderer->scaled_height = SCREEN_HEIGHT;
            renderer->offset_x = 0;
            renderer->offset_y = 0;
            break;
        }
        
        case SCALING_CROP: {
            // Crop to fit (not implemented yet, fallback to letterbox)
            float scale_x = (float)SCREEN_WIDTH / width;
            float scale_y = (float)SCREEN_HEIGHT / height;
            renderer->scale_factor = min_float(scale_x, scale_y);
            
            renderer->scaled_width = (int)(width * renderer->scale_factor);
            renderer->scaled_height = (int)(height * renderer->scale_factor);
            renderer->offset_x = (SCREEN_WIDTH - renderer->scaled_width) / 2;
            renderer->offset_y = (SCREEN_HEIGHT - renderer->scaled_height) / 2;
            break;
        }
    }
    
    DEBUG_PRINT("Remote size: %dx%d, Scale: %.2f, Scaled: %dx%d, Offset: (%d,%d)",
                width, height, renderer->scale_factor,
                renderer->scaled_width, renderer->scaled_height,
                renderer->offset_x, renderer->offset_y);
}

void vnc_renderer_render_frame(VNCRenderer *renderer, uint8_t *pixels, 
                               int width, int height, int bytes_per_pixel) {
    if (!renderer || !renderer->fb || !pixels) return;
    
    // Clear screen with black (for letterbox bars)
    fb_clear(renderer->fb, COLOR_BLACK);
    
    // Render based on scaling mode
    if (renderer->scaling_mode == SCALING_STRETCH) {
        // Direct stretch rendering
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                // Map screen coordinates to remote coordinates
                int remote_x = (x * width) / SCREEN_WIDTH;
                int remote_y = (y * height) / SCREEN_HEIGHT;
                
                // Get pixel from remote buffer
                int remote_idx = (remote_y * width + remote_x) * bytes_per_pixel;
                uint32_t color;
                
                if (bytes_per_pixel == 4) {
                    // RGBA or BGRA
                    uint8_t r = pixels[remote_idx];
                    uint8_t g = pixels[remote_idx + 1];
                    uint8_t b = pixels[remote_idx + 2];
                    color = RGB(r, g, b);
                } else if (bytes_per_pixel == 3) {
                    // RGB
                    uint8_t r = pixels[remote_idx];
                    uint8_t g = pixels[remote_idx + 1];
                    uint8_t b = pixels[remote_idx + 2];
                    color = RGB(r, g, b);
                } else {
                    color = COLOR_WHITE;  // Fallback
                }
                
                fb_draw_pixel(renderer->fb, x, y, color);
            }
        }
    } else {
        // Letterbox rendering with scaling
        for (int y = 0; y < renderer->scaled_height; y++) {
            for (int x = 0; x < renderer->scaled_width; x++) {
                // Map scaled coordinates to remote coordinates
                int remote_x = (int)((float)x / renderer->scale_factor);
                int remote_y = (int)((float)y / renderer->scale_factor);
                
                // Bounds check
                if (remote_x >= width || remote_y >= height) continue;
                
                // Get pixel from remote buffer
                int remote_idx = (remote_y * width + remote_x) * bytes_per_pixel;
                uint32_t color;
                
                if (bytes_per_pixel == 4) {
                    uint8_t r = pixels[remote_idx];
                    uint8_t g = pixels[remote_idx + 1];
                    uint8_t b = pixels[remote_idx + 2];
                    color = RGB(r, g, b);
                } else if (bytes_per_pixel == 3) {
                    uint8_t r = pixels[remote_idx];
                    uint8_t g = pixels[remote_idx + 1];
                    uint8_t b = pixels[remote_idx + 2];
                    color = RGB(r, g, b);
                } else {
                    color = COLOR_WHITE;
                }
                
                // Draw to screen with offset
                fb_draw_pixel(renderer->fb, 
                            x + renderer->offset_x, 
                            y + renderer->offset_y, 
                            color);
            }
        }
    }
    
    // Update frame counter
    renderer->frame_count++;
}

void vnc_renderer_update(VNCRenderer *renderer, uint8_t *pixels, 
                        int x, int y, int w, int h, int bytes_per_pixel) {
    // For now, just do a full frame update
    // TODO: Optimize to only update the specified region
    vnc_renderer_render_frame(renderer, pixels, 
                              renderer->remote_width, 
                              renderer->remote_height, 
                              bytes_per_pixel);
}

bool vnc_renderer_screen_to_remote(VNCRenderer *renderer, int screen_x, int screen_y,
                                   int *remote_x, int *remote_y) {
    if (!renderer || !remote_x || !remote_y) return false;
    
    // Check if touch is within scaled display area
    int rel_x = screen_x - renderer->offset_x;
    int rel_y = screen_y - renderer->offset_y;
    
    if (rel_x < 0 || rel_x >= renderer->scaled_width ||
        rel_y < 0 || rel_y >= renderer->scaled_height) {
        return false;  // Touch is in letterbox area
    }
    
    // Convert to remote coordinates
    if (renderer->scaling_mode == SCALING_STRETCH) {
        *remote_x = (screen_x * renderer->remote_width) / SCREEN_WIDTH;
        *remote_y = (screen_y * renderer->remote_height) / SCREEN_HEIGHT;
    } else {
        *remote_x = (int)((float)rel_x / renderer->scale_factor);
        *remote_y = (int)((float)rel_y / renderer->scale_factor);
    }
    
    // Bounds check
    if (*remote_x < 0) *remote_x = 0;
    if (*remote_y < 0) *remote_y = 0;
    if (*remote_x >= renderer->remote_width) *remote_x = renderer->remote_width - 1;
    if (*remote_y >= renderer->remote_height) *remote_y = renderer->remote_height - 1;
    
    return true;
}

void vnc_renderer_show_touch(VNCRenderer *renderer, int x, int y) {
    if (!renderer) return;
    
    renderer->touch_x = x;
    renderer->touch_y = y;
    renderer->touch_time = get_time_ms();
    renderer->show_touch_feedback = true;
}

void vnc_renderer_update_touch_feedback(VNCRenderer *renderer) {
    if (!renderer || !renderer->show_touch_feedback) return;
    
    uint32_t current_time = get_time_ms();
    if (current_time - renderer->touch_time > TOUCH_FEEDBACK_DURATION_MS) {
        renderer->show_touch_feedback = false;
        return;
    }
    
    // Draw touch feedback circle
    fb_draw_circle(renderer->fb, renderer->touch_x, renderer->touch_y, 
                   10, TOUCH_FEEDBACK_COLOR);
}

void vnc_renderer_draw_status(VNCRenderer *renderer, const char *status) {
    if (!renderer || !renderer->fb || !status) return;
    
    #if SHOW_CONNECTION_STATUS
    // Draw status text at top of screen
    fb_draw_text(renderer->fb, 10, 10, status, COLOR_WHITE, 2);
    #endif
}

void vnc_renderer_draw_fps(VNCRenderer *renderer) {
    if (!renderer || !renderer->fb) return;
    
    #if SHOW_FPS_COUNTER
    uint32_t current_time = get_time_ms();
    uint32_t elapsed = current_time - renderer->last_fps_time;
    
    if (elapsed >= 1000) {  // Update FPS every second
        renderer->current_fps = (float)renderer->frame_count / (elapsed / 1000.0f);
        renderer->frame_count = 0;
        renderer->last_fps_time = current_time;
    }
    
    // Draw FPS counter at top right
    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", renderer->current_fps);
    fb_draw_text(renderer->fb, SCREEN_WIDTH - 150, 10, fps_text, COLOR_GREEN, 2);
    #endif
}

void vnc_renderer_present(VNCRenderer *renderer) {
    if (!renderer || !renderer->fb) return;
    
    // Update touch feedback if active
    vnc_renderer_update_touch_feedback(renderer);
    
    // Draw FPS counter
    vnc_renderer_draw_fps(renderer);
    
    // Swap buffers to present to screen
    fb_swap(renderer->fb);
}

void vnc_renderer_cleanup(VNCRenderer *renderer) {
    if (!renderer) return;
    
    DEBUG_PRINT("Renderer cleanup");
    // Nothing to free currently, but keep for future use
}
