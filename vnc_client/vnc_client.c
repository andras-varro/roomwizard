#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <rfb/rfbclient.h>

#include "config.h"
#include "vnc_renderer.h"
#include "vnc_input.h"
#include "../native_games/common/framebuffer.h"
#include "../native_games/common/touch_input.h"
#include "../native_games/common/hardware.h"

// Global state
static volatile bool g_running = true;
static Framebuffer g_fb;
static TouchInput g_touch;
static VNCRenderer g_renderer;
static VNCInput g_input;
static rfbClient *g_vnc_client = NULL;
static pthread_t g_watchdog_thread;

// Signal handler for clean shutdown
static void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    g_running = false;
}

// Watchdog feeder thread
static void* watchdog_thread_func(void *arg) {
    int watchdog_fd = open(WATCHDOG_DEVICE, O_WRONLY);
    if (watchdog_fd < 0) {
        fprintf(stderr, "Warning: Could not open watchdog device\n");
        return NULL;
    }
    
    DEBUG_PRINT("Watchdog thread started");
    
    while (g_running) {
        // Feed the watchdog
        write(watchdog_fd, "1", 1);
        sleep(WATCHDOG_FEED_INTERVAL_SEC);
    }
    
    close(watchdog_fd);
    DEBUG_PRINT("Watchdog thread stopped");
    return NULL;
}

// LibVNCClient callback: framebuffer update
static void vnc_framebuffer_update(rfbClient *client, int x, int y, int w, int h) {
    // Get the framebuffer data
    uint8_t *pixels = (uint8_t*)client->frameBuffer;
    int width = client->width;
    int height = client->height;
    int bytes_per_pixel = client->format.bitsPerPixel / 8;
    
    // Render the updated region
    vnc_renderer_render_frame(&g_renderer, pixels, width, height, bytes_per_pixel);
}

// LibVNCClient callback: handle messages
static rfbBool vnc_handle_cursor_pos(rfbClient *client, int x, int y) {
    // Cursor position update (optional)
    return TRUE;
}

// Initialize VNC client
static rfbClient* vnc_client_init(const char *host, int port) {
    rfbClient *client = rfbGetClient(8, 3, 4);  // 8 bits per sample, 3 samples, 4 bytes per pixel
    if (!client) {
        fprintf(stderr, "Failed to create VNC client\n");
        return NULL;
    }
    
    // Set up callbacks
    client->GotFrameBufferUpdate = vnc_framebuffer_update;
    client->HandleCursorPos = vnc_handle_cursor_pos;
    
    // Set connection parameters
    client->serverHost = strdup(host);
    client->serverPort = port;
    
    // Set password if configured
    if (strlen(VNC_PASSWORD) > 0) {
        // Password will be prompted if needed
        client->GetPassword = NULL;
    }
    
    // Initialize connection
    if (!rfbInitClient(client, NULL, NULL)) {
        fprintf(stderr, "Failed to initialize VNC client\n");
        return NULL;
    }
    
    DEBUG_PRINT("VNC client initialized: %dx%d", client->width, client->height);
    
    return client;
}

// Main application loop
static int run_vnc_client(const char *host, int port) {
    int ret = -1;
    
    // Initialize framebuffer
    if (fb_init(&g_fb, FB_DEVICE) < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return -1;
    }
    DEBUG_PRINT("Framebuffer initialized");
    
    // Initialize touch input
    if (touch_init(&g_touch, TOUCH_DEVICE) < 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&g_fb);
        return -1;
    }
    DEBUG_PRINT("Touch input initialized");
    
    // Initialize hardware (LEDs, backlight)
    hw_init();
    hw_set_backlight(100);
    hw_set_led(LED_GREEN, 50);  // Green LED to indicate running
    
    // Clear screen
    fb_clear(&g_fb, COLOR_BLACK);
    fb_draw_text(&g_fb, 200, 200, "Connecting to VNC...", COLOR_WHITE, 3);
    fb_swap(&g_fb);
    
    // Initialize VNC client
    g_vnc_client = vnc_client_init(host, port);
    if (!g_vnc_client) {
        fprintf(stderr, "Failed to connect to VNC server\n");
        fb_clear(&g_fb, COLOR_BLACK);
        fb_draw_text(&g_fb, 150, 200, "Connection Failed!", COLOR_RED, 3);
        fb_swap(&g_fb);
        sleep(3);
        goto cleanup;
    }
    
    // Initialize renderer
    if (vnc_renderer_init(&g_renderer, &g_fb) < 0) {
        fprintf(stderr, "Failed to initialize renderer\n");
        goto cleanup;
    }
    vnc_renderer_set_remote_size(&g_renderer, g_vnc_client->width, g_vnc_client->height);
    
    // Initialize input handler
    if (vnc_input_init(&g_input, &g_touch, &g_renderer, g_vnc_client) < 0) {
        fprintf(stderr, "Failed to initialize input handler\n");
        goto cleanup;
    }
    
    // Start watchdog thread
    if (pthread_create(&g_watchdog_thread, NULL, watchdog_thread_func, NULL) != 0) {
        fprintf(stderr, "Warning: Failed to start watchdog thread\n");
    }
    
    // Show connected status
    fb_clear(&g_fb, COLOR_BLACK);
    fb_draw_text(&g_fb, 250, 200, "Connected!", COLOR_GREEN, 3);
    fb_swap(&g_fb);
    sleep(1);
    
    // Main loop
    DEBUG_PRINT("Entering main loop");
    hw_set_led(LED_GREEN, 100);  // Full green = connected
    
    while (g_running) {
        // Process VNC messages (non-blocking with timeout)
        int result = WaitForMessage(g_vnc_client, 10000);  // 10ms timeout
        if (result < 0) {
            fprintf(stderr, "VNC connection lost\n");
            break;
        }
        
        if (result > 0) {
            // Handle VNC messages
            if (!HandleRFBServerMessage(g_vnc_client)) {
                fprintf(stderr, "Error handling VNC message\n");
                break;
            }
        }
        
        // Process touch input
        vnc_input_process(&g_input);
        
        // Present frame to screen
        vnc_renderer_present(&g_renderer);
        
        // Small delay to prevent CPU spinning
        usleep(1000);  // 1ms
    }
    
    ret = 0;
    DEBUG_PRINT("Main loop exited");
    
cleanup:
    // Cleanup
    if (g_vnc_client) {
        rfbClientCleanup(g_vnc_client);
        g_vnc_client = NULL;
    }
    
    vnc_input_cleanup(&g_input);
    vnc_renderer_cleanup(&g_renderer);
    
    hw_set_led(LED_GREEN, 0);
    hw_set_led(LED_RED, 0);
    
    touch_close(&g_touch);
    fb_close(&g_fb);
    
    // Wait for watchdog thread
    if (g_watchdog_thread) {
        pthread_join(g_watchdog_thread, NULL);
    }
    
    return ret;
}

int main(int argc, char *argv[]) {
    const char *host = VNC_DEFAULT_HOST;
    int port = VNC_DEFAULT_PORT;
    
    printf("RoomWizard VNC Client v1.0\n");
    printf("==========================\n\n");
    
    // Parse command line arguments
    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
    }
    
    printf("Connecting to %s:%d\n", host, port);
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Run the VNC client
    int ret = run_vnc_client(host, port);
    
    printf("\nVNC client exited\n");
    return ret;
}
