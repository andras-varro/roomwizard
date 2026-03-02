/*
 * VNC Client for RoomWizard - v2.1 (Phase 2 Optimized + Config File)
 *
 * Major changes from v1.0:
 *   - Encoding negotiation: requests Tight/ZRLE instead of Raw
 *   - 16bpp RGB565 framebuffer (halves swap bandwidth)
 *   - Partial region updates (only re-render dirty VNC rectangles)
 *   - Frame-rate-capped presentation (30 fps)
 *   - Runtime config file support (/opt/vnc_client/vnc_client.conf)
 *   - Bilinear interpolation for smooth downscaling
 *
 * Expected improvement: CPU 99% → 15-30%, frame rate <1 → 15-30 fps
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <ctype.h>
#include <rfb/rfbclient.h>

#include "config.h"
#include "vnc_renderer.h"
#include "vnc_input.h"
#include "../native_games/common/framebuffer.h"
#include "../native_games/common/touch_input.h"
#include "../native_games/common/hardware.h"

/* ── Runtime configuration ─────────────────────────────────────────── */

typedef struct {
    char host[256];
    int  port;
    char password[256];
    char encodings[256];
    int  compress_level;
    int  quality_level;
} VNCConfig;

static VNCConfig g_config;

/* Trim leading/trailing whitespace in-place */
static char *str_trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/*
 * Parse a config file with key=value lines.
 * Lines starting with # are comments; empty lines are ignored.
 * Supported keys: host, port, password, encodings, compress_level, quality_level
 */
static void load_config_file(VNCConfig *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        DEBUG_PRINT("Config file not found: %s (using defaults)", path);
        return;
    }

    char line[512];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *trimmed = str_trim(line);
        if (*trimmed == '\0' || *trimmed == '#')
            continue;

        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = str_trim(trimmed);
        char *val = str_trim(eq + 1);

        if (strcmp(key, "host") == 0) {
            strncpy(cfg->host, val, sizeof(cfg->host) - 1);
            count++;
        } else if (strcmp(key, "port") == 0) {
            cfg->port = atoi(val);
            count++;
        } else if (strcmp(key, "password") == 0) {
            strncpy(cfg->password, val, sizeof(cfg->password) - 1);
            count++;
        } else if (strcmp(key, "encodings") == 0) {
            strncpy(cfg->encodings, val, sizeof(cfg->encodings) - 1);
            count++;
        } else if (strcmp(key, "compress_level") == 0) {
            cfg->compress_level = atoi(val);
            count++;
        } else if (strcmp(key, "quality_level") == 0) {
            cfg->quality_level = atoi(val);
            count++;
        } else {
            fprintf(stderr, "Warning: unknown config key '%s'\n", key);
        }
    }
    fclose(f);
    printf("Loaded %d settings from %s\n", count, path);
}

/* ── Global state ──────────────────────────────────────────────────── */

static volatile bool g_running = true;
static Framebuffer   g_fb;
static TouchInput    g_touch;
static VNCRenderer   g_renderer;
static VNCInput      g_input;
static rfbClient    *g_vnc_client = NULL;
static pthread_t     g_watchdog_thread;
static bool          g_touch_ok = false;

/* ── Signal handler ────────────────────────────────────────────────── */

static void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    g_running = false;
}

/* ── Watchdog feeder thread ────────────────────────────────────────── */

static void *watchdog_thread_func(void *arg) {
    (void)arg;
    int wd_fd = open(WATCHDOG_DEVICE, O_WRONLY);
    if (wd_fd < 0) {
        fprintf(stderr, "Warning: Could not open watchdog device\n");
        return NULL;
    }
    DEBUG_PRINT("Watchdog thread started (interval %ds)", WATCHDOG_FEED_INTERVAL_SEC);
    while (g_running) {
        write(wd_fd, "1", 1);
        sleep(WATCHDOG_FEED_INTERVAL_SEC);
    }
    close(wd_fd);
    DEBUG_PRINT("Watchdog thread stopped");
    return NULL;
}

/* ── LibVNCClient callbacks ────────────────────────────────────────── */

/*
 * Password callback for VNC authentication (type 2, VncAuth).
 * Returns a malloc'd string that LibVNCClient will free.
 */
static char *vnc_get_password(rfbClient *client) {
    (void)client;
    DEBUG_PRINT("VNC password requested, providing configured password");
    return strdup(g_config.password);
}

/*
 * Called for each dirty rectangle the server sends.
 * With compressed encoding (Tight/ZRLE), these are typically small regions.
 * We convert + scale only the dirty pixels into the back buffer.
 */
static void vnc_fb_update(rfbClient *client, int x, int y, int w, int h) {
    int bpp = client->format.bitsPerPixel / 8;
    vnc_renderer_update_region(&g_renderer,
                               (const uint8_t *)client->frameBuffer,
                               x, y, w, h, bpp);
}

static rfbBool vnc_cursor_pos(rfbClient *client, int x, int y) {
    (void)client; (void)x; (void)y;
    return TRUE;
}

/* ── VNC client initialization with encoding negotiation ───────────── */

static rfbClient *vnc_client_init(const char *host, int port) {
    /* 8 bits/sample, 3 samples (RGB), 4 bytes/pixel → 32bpp from server */
    rfbClient *client = rfbGetClient(8, 3, 4);
    if (!client) {
        fprintf(stderr, "Failed to create VNC client\n");
        return NULL;
    }

    /* Callbacks */
    client->GotFrameBufferUpdate = vnc_fb_update;
    client->HandleCursorPos      = vnc_cursor_pos;

    /* Password callback - always set, LibVNCClient calls it if server requires auth */
    client->GetPassword = vnc_get_password;

    /* Connection parameters */
    client->serverHost = strdup(host);
    client->serverPort = port;

    /*
     * ★ Phase 2.1: Encoding negotiation (biggest single optimization)
     *
     * Request compressed encodings in preference order.
     * Tight encoding with JPEG typically reduces bandwidth 10-100×
     * compared to Raw (1.5 MB/frame → 10-150 KB/frame).
     */
    client->appData.encodingsString = g_config.encodings;
    client->appData.compressLevel   = g_config.compress_level;
    client->appData.qualityLevel    = g_config.quality_level;

    /* Connect and negotiate */
    if (!rfbInitClient(client, NULL, NULL)) {
        fprintf(stderr, "Failed to connect to VNC server at %s:%d\n", host, port);
        return NULL;
    }

    printf("  Remote desktop: %dx%d\n", client->width, client->height);
    printf("  Pixel format: %d bpp, depth %d\n",
           client->format.bitsPerPixel, client->format.depth);
    DEBUG_PRINT("Encodings requested: %s", g_config.encodings);
    DEBUG_PRINT("Compress=%d Quality=%d", g_config.compress_level, g_config.quality_level);

    return client;
}

/* ── Main application ──────────────────────────────────────────────── */

static int run_vnc_client(const char *host, int port) {
    int ret = -1;

#if USE_16BPP
    /* Switch framebuffer to 16bpp RGB565 BEFORE fb_init.
     * Halves memcpy cost in fb_swap: 768 KB vs 1.5 MB per frame.
     * (Same technique as the ScummVM RoomWizard backend) */
    fb_set_bpp(FB_DEVICE, 16);
    DEBUG_PRINT("Switched to 16bpp RGB565");
#endif

    /* Initialize framebuffer */
    if (fb_init(&g_fb, FB_DEVICE) < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return -1;
    }
    DEBUG_PRINT("Framebuffer: %ux%u, %u bpp, size=%zu",
                g_fb.width, g_fb.height, g_fb.bytes_per_pixel * 8, g_fb.screen_size);

    /* Initialize touch input (non-fatal if absent) */
    if (touch_init(&g_touch, TOUCH_DEVICE) < 0) {
        fprintf(stderr, "Warning: Touch input not available\n");
    } else {
        g_touch_ok = true;
        DEBUG_PRINT("Touch input initialized");
    }

    /* Hardware subsystem */
    hw_init();
    hw_set_backlight(100);
    hw_set_led(LED_GREEN, 50);      /* dim green = connecting */

    /* ── Show "Connecting" splash (16bpp text) ───────────────────── */
    vnc_renderer_clear_screen(&g_fb);
    vnc_renderer_draw_text(&g_fb, 180, 180, "CONNECTING TO VNC...", RGB565_WHITE, 3);
    {
        char addr[280];
        snprintf(addr, sizeof(addr), "%s:%d", host, port);
        vnc_renderer_draw_text(&g_fb, 200, 230, addr, RGB565_GREEN, 2);
    }
    vnc_renderer_draw_text(&g_fb, 140, 300,
        "ENCODINGS: ", RGB565(128, 128, 128), 1);
    fb_swap(&g_fb);

    /* ── Connect to VNC server ───────────────────────────────────── */
    printf("Connecting to %s:%d ...\n", host, port);
    g_vnc_client = vnc_client_init(host, port);
    if (!g_vnc_client) {
        vnc_renderer_clear_screen(&g_fb);
        vnc_renderer_draw_text(&g_fb, 130, 200, "CONNECTION FAILED!", RGB565_RED, 3);
        fb_swap(&g_fb);
        hw_set_led(LED_RED, 100);
        sleep(3);
        goto cleanup;
    }
    printf("Connected!\n\n");

    /* ── Initialize renderer ─────────────────────────────────────── */
    if (vnc_renderer_init(&g_renderer, &g_fb) < 0) {
        fprintf(stderr, "Failed to initialize renderer\n");
        goto cleanup;
    }
    vnc_renderer_set_remote_size(&g_renderer,
                                 g_vnc_client->width, g_vnc_client->height);

    /* ── Initialize input handler ────────────────────────────────── */
    if (g_touch_ok) {
        if (vnc_input_init(&g_input, &g_touch, &g_renderer, g_vnc_client) < 0) {
            fprintf(stderr, "Warning: Input handler init failed\n");
            g_touch_ok = false;
        }
    }

    /* ── Start watchdog feeder ───────────────────────────────────── */
    if (pthread_create(&g_watchdog_thread, NULL, watchdog_thread_func, NULL) != 0) {
        fprintf(stderr, "Warning: Failed to start watchdog thread\n");
    }

    /* Brief "Connected" splash */
    vnc_renderer_clear_screen(&g_fb);
    vnc_renderer_draw_text(&g_fb, 250, 200, "CONNECTED!", RGB565_GREEN, 3);
    fb_swap(&g_fb);
    usleep(500000);     /* 500 ms */

    /* Clear buffer for VNC content */
    vnc_renderer_clear_screen(&g_fb);
    fb_swap(&g_fb);

    /* ── Main event loop ─────────────────────────────────────────── */
    DEBUG_PRINT("Entering main loop (target %d fps)", TARGET_FPS);
    hw_set_led(LED_GREEN, 100);     /* full green = connected */

    while (g_running) {
        /*
         * WaitForMessage: block up to 10 ms for VNC data.
         * Keeps touch polling at ~100 Hz while being kind to the CPU
         * when there are no screen updates.
         */
        int result = WaitForMessage(g_vnc_client, 10000);
        if (result < 0) {
            fprintf(stderr, "VNC connection lost\n");
            break;
        }

        if (result > 0) {
            /*
             * Process the VNC message.  This calls vnc_fb_update() for each
             * dirty rectangle, which converts + scales only the changed pixels
             * into the back buffer.  With Tight encoding, updates are small
             * and efficient.
             *
             * After processing, LibVNCClient automatically sends an
             * incremental framebuffer update request for the next frame.
             */
            if (!HandleRFBServerMessage(g_vnc_client)) {
                fprintf(stderr, "Error handling VNC message\n");
                break;
            }
        }

        /* Process touch input → VNC pointer events + exit gesture */
        if (g_touch_ok) {
            vnc_input_process(&g_input);

            /* Check for exit gesture (long-press top-left corner) */
            if (vnc_input_exit_requested(&g_input)) {
                printf("Exit gesture detected, shutting down...\n");
                break;
            }

            /* Draw exit progress indicator while holding corner */
            float prog = vnc_input_exit_progress(&g_input);
            if (prog > 0.01f) {
                /*
                 * Draw a small progress arc/bar in the top-left corner.
                 * Just a thin horizontal bar that fills left-to-right.
                 * It's drawn directly into the back buffer and will be
                 * overwritten by the next VNC update, so non-persistent.
                 */
                uint16_t *buf = (uint16_t *)g_fb.back_buffer;
                int bar_width = (int)(EXIT_ZONE_SIZE * prog);
                uint16_t color = (prog < 0.7f) ? RGB565_YELLOW : RGB565_RED;
                for (int y = 0; y < 3; y++)
                    for (int x = 0; x < bar_width && x < SCREEN_WIDTH; x++)
                        buf[y * SCREEN_WIDTH + x] = color;
                g_renderer.needs_present = true;
            }
        }

        /*
         * Present frame to screen (rate-capped, only if dirty).
         * If no VNC updates arrived and no new data since last swap,
         * this is a no-op — saves the entire fb_swap memcpy.
         */
        vnc_renderer_present(&g_renderer);
    }

    ret = 0;
    DEBUG_PRINT("Main loop exited cleanly");

cleanup:
    if (g_vnc_client) {
        rfbClientCleanup(g_vnc_client);
        g_vnc_client = NULL;
    }

    vnc_input_cleanup(&g_input);
    vnc_renderer_cleanup(&g_renderer);

    hw_set_led(LED_GREEN, 0);
    hw_set_led(LED_RED, 0);

    if (g_touch_ok)
        touch_close(&g_touch);
    fb_close(&g_fb);

    /* Stop watchdog thread */
    g_running = false;
    if (g_watchdog_thread)
        pthread_join(g_watchdog_thread, NULL);

    return ret;
}

/* ── Entry point ───────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    /* --help */
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("RoomWizard VNC Client v2.1\n\n");
        printf("Usage: vnc_client [OPTIONS] [host [port]]\n\n");
        printf("Options:\n");
        printf("  --config <path>   Config file (default: %s)\n", VNC_CONFIG_FILE);
        printf("  --host <ip>       VNC server host\n");
        printf("  --port <port>     VNC server port\n");
        printf("  --password <pw>   VNC password\n");
        printf("  --help, -h        Show this help\n\n");
        printf("Config file format: key = value (one per line, # for comments)\n");
        printf("Keys: host, port, password, encodings, compress_level, quality_level\n\n");
        printf("Exit: long-press top-left corner for 3 seconds\n");
        return 0;
    }

    printf("╔═══════════════════════════════════════╗\n");
    printf("║  RoomWizard VNC Client v2.1           ║\n");
    printf("║  Phase 2: Optimized + Bilinear        ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");

    /* 1. Compile-time defaults */
    strncpy(g_config.host, VNC_DEFAULT_HOST, sizeof(g_config.host) - 1);
    g_config.port = VNC_DEFAULT_PORT;
    strncpy(g_config.password, VNC_DEFAULT_PASSWORD, sizeof(g_config.password) - 1);
    strncpy(g_config.encodings, VNC_ENCODINGS, sizeof(g_config.encodings) - 1);
    g_config.compress_level = VNC_COMPRESS_LEVEL;
    g_config.quality_level  = VNC_QUALITY_LEVEL;

    /* 2. Config file overrides */
    const char *config_path = VNC_CONFIG_FILE;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            config_path = argv[i + 1];
            break;
        }
    }
    load_config_file(&g_config, config_path);

    /* 3. Command-line overrides (legacy positional: host [port]) */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) { i++; continue; }
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            strncpy(g_config.host, argv[++i], sizeof(g_config.host) - 1);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_config.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            strncpy(g_config.password, argv[++i], sizeof(g_config.password) - 1);
        } else if (argv[i][0] != '-') {
            /* Legacy positional: first non-flag arg is host, second is port */
            strncpy(g_config.host, argv[i], sizeof(g_config.host) - 1);
            if (i + 1 < argc && argv[i + 1][0] != '-')
                g_config.port = atoi(argv[++i]);
        }
    }

    printf("Config:    %s\n", config_path);
    printf("Target:    %s:%d\n", g_config.host, g_config.port);
    printf("Encodings: %s\n", g_config.encodings);
    printf("Compress:  %d  Quality: %d\n", g_config.compress_level, g_config.quality_level);
    printf("Display:   %s %dx%d\n", USE_16BPP ? "16bpp RGB565" : "32bpp ARGB",
           SCREEN_WIDTH, SCREEN_HEIGHT);
    printf("Exit:      hold top-left corner %d ms\n\n", EXIT_HOLD_MS);

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    int ret = run_vnc_client(g_config.host, g_config.port);

    printf("\nVNC client exited (code %d)\n", ret);
    return ret;
}
