/*
 * VNC Client for RoomWizard - v2.2 (Auto-Reconnect)
 *
 * v2.2 changes:
 *   - Auto-reconnect with exponential backoff on connection drop
 *   - On-screen reconnect UI with countdown, attempt counter, buttons
 *   - Touch [Cancel] to return to launcher, [Connect Now] to skip wait
 *
 * v2.1 changes:
 *   - Encoding negotiation: requests Tight/ZRLE instead of Raw
 *   - 16bpp RGB565 framebuffer (halves swap bandwidth)
 *   - Partial region updates (only re-render dirty VNC rectangles)
 *   - Frame-rate-capped presentation (30 fps)
 *   - Runtime config file support (/opt/vnc_client/vnc_client.conf)
 *   - Bilinear interpolation for smooth downscaling
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
#include "../native_apps/common/framebuffer.h"
#include "../native_apps/common/touch_input.h"
#include "../native_apps/common/hardware.h"
#include "../native_apps/common/logger.h"

static Logger g_logger;

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
        LOG_INFO(&g_logger, "Config file not found: %s (using defaults)", path);
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
            LOG_WARN(&g_logger, "Unknown config key '%s'", key);
        }
    }
    fclose(f);
    LOG_INFO(&g_logger, "Loaded %d settings from %s", count, path);
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
    LOG_INFO(&g_logger, "Received signal %d, shutting down...", sig);
    g_running = false;
}

/* ── Watchdog feeder thread ────────────────────────────────────────── */

static void *watchdog_thread_func(void *arg) {
    (void)arg;
    int wd_fd = open(WATCHDOG_DEVICE, O_WRONLY);
    if (wd_fd < 0) {
        LOG_WARN(&g_logger, "Could not open watchdog device");
        return NULL;
    }
    LOG_DEBUG(&g_logger, "Watchdog thread started (interval %ds)", WATCHDOG_FEED_INTERVAL_SEC);
    while (g_running) {
        write(wd_fd, "1", 1);
        sleep(WATCHDOG_FEED_INTERVAL_SEC);
    }
    close(wd_fd);
    LOG_DEBUG(&g_logger, "Watchdog thread stopped");
    return NULL;
}

/* ── LibVNCClient callbacks ────────────────────────────────────────── */

/*
 * Password callback for VNC authentication (type 2, VncAuth).
 * Returns a malloc'd string that LibVNCClient will free.
 */
static char *vnc_get_password(rfbClient *client) {
    (void)client;
    LOG_DEBUG(&g_logger, "VNC password requested, providing configured password");
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

static rfbClient *vnc_client_init(const char *host, int port, unsigned int connect_timeout) {
    /* 8 bits/sample, 3 samples (RGB), 4 bytes/pixel → 32bpp from server */
    rfbClient *client = rfbGetClient(8, 3, 4);
    if (!client) {
        LOG_ERROR(&g_logger, "Failed to create VNC client");
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

    /* Set connect timeout (default is 60s which freezes the UI) */
    client->connectTimeout = connect_timeout;

    /* Connect and negotiate */
    if (!rfbInitClient(client, NULL, NULL)) {
        LOG_ERROR(&g_logger, "Failed to connect to VNC server at %s:%d", host, port);
        return NULL;
    }

    LOG_INFO(&g_logger, "Remote desktop: %dx%d, %d bpp depth %d",
             client->width, client->height,
             client->format.bitsPerPixel, client->format.depth);
    LOG_DEBUG(&g_logger, "Encodings requested: %s", g_config.encodings);
    LOG_DEBUG(&g_logger, "Compress=%d Quality=%d", g_config.compress_level, g_config.quality_level);

    return client;
}

/* ── Reconnect UI helpers ──────────────────────────────────────────── */

/* Draw a centered text string at given Y position */
static void draw_centered_text(Framebuffer *fb, int y, const char *text,
                                uint16_t color, int scale) {
    int tw = vnc_renderer_text_width(text, scale);
    int x = (SCREEN_WIDTH - tw) / 2;
    if (x < 0) x = 0;
    vnc_renderer_draw_text(fb, x, y, text, color, scale);
}

/* Draw a button rectangle with centered label.
 * Returns true if (tx,ty) falls inside the button. */
static bool draw_button(Framebuffer *fb, int bx, int by, int bw, int bh,
                         const char *label, uint16_t bg, uint16_t fg,
                         int tx, int ty) {
    vnc_renderer_fill_rect(fb, bx, by, bw, bh, bg);
    /* 1-pixel border */
    vnc_renderer_fill_rect(fb, bx, by, bw, 1, fg);
    vnc_renderer_fill_rect(fb, bx, by + bh - 1, bw, 1, fg);
    vnc_renderer_fill_rect(fb, bx, by, 1, bh, fg);
    vnc_renderer_fill_rect(fb, bx + bw - 1, by, 1, bh, fg);
    /* Centered label (scale 2 → 12px char height) */
    int tw = vnc_renderer_text_width(label, 2);
    int lx = bx + (bw - tw) / 2;
    int ly = by + (bh - 14) / 2;
    vnc_renderer_draw_text(fb, lx, ly, label, fg, 2);
    /* Hit test */
    return (tx >= bx && tx < bx + bw && ty >= by && ty < by + bh);
}

/*
 * Show the reconnect UI screen and wait for countdown expiry, user
 * pressing [CONNECT NOW], or user pressing [CANCEL].
 *
 * Returns:
 *   1 = user pressed Connect Now (or countdown expired) → try reconnect
 *   0 = user pressed Cancel → exit to launcher
 *  -1 = g_running became false (signal)
 */
static int reconnect_ui(int attempt, int wait_seconds) {
    LOG_INFO(&g_logger, "Reconnect UI: attempt %d, waiting %ds", attempt, wait_seconds);
    hw_set_led(LED_RED, 50);
    hw_set_led(LED_GREEN, 0);

    struct timeval start;
    gettimeofday(&start, NULL);

    int remaining = wait_seconds;

    while (g_running && remaining >= 0) {
        /* ── Draw screen ──────────────────────────────────────── */
        vnc_renderer_clear_screen(&g_fb);

        draw_centered_text(&g_fb, 100, "CONNECTION LOST", RGB565_RED, 3);
        draw_centered_text(&g_fb, 160, "RECONNECTING...", RGB565_WHITE, 2);

        {
            char buf[64];
            snprintf(buf, sizeof(buf), "ATTEMPT: %d", attempt);
            draw_centered_text(&g_fb, 220, buf, RGB565_YELLOW, 2);
        }

        {
            char buf[64];
            snprintf(buf, sizeof(buf), "WAITING %ds", remaining);
            draw_centered_text(&g_fb, 270, buf, RGB565(160, 160, 160), 2);
        }

        /* Progress bar (full width = wait_seconds, shrinks as time passes) */
        {
            int bar_x = 100;
            int bar_w_max = SCREEN_WIDTH - 200;
            int bar_w = (remaining * bar_w_max) / (wait_seconds > 0 ? wait_seconds : 1);
            if (bar_w < 0) bar_w = 0;
            vnc_renderer_fill_rect(&g_fb, bar_x, 310, bar_w_max, 8,
                                   RGB565(40, 40, 40));
            vnc_renderer_fill_rect(&g_fb, bar_x, 310, bar_w, 8,
                                   RGB565_GREEN);
        }

        /* Poll touch — trigger buttons on finger-up (release),
         * not finger-down, to avoid accidental activations.        */
        int tx = -1, ty = -1;
        if (g_touch_ok) {
            touch_poll(&g_touch);
            TouchState ts = touch_get_state(&g_touch);
            if (ts.released) {
                tx = ts.x;
                ty = ts.y;
                LOG_DEBUG(&g_logger, "Touch released at (%d,%d)", tx, ty);
            }
        }

        /* Draw buttons and check hits */
        bool cancel_hit = draw_button(&g_fb,
            RECONNECT_BTN_CANCEL_X, RECONNECT_BTN_Y,
            RECONNECT_BTN_W, RECONNECT_BTN_H,
            "CANCEL", RGB565(40, 40, 40), RGB565_WHITE, tx, ty);

        bool connect_hit = draw_button(&g_fb,
            RECONNECT_BTN_CONNECT_X, RECONNECT_BTN_Y,
            RECONNECT_BTN_W, RECONNECT_BTN_H,
            "CONNECT NOW", RGB565(0, 60, 0), RGB565_GREEN, tx, ty);

        fb_swap(&g_fb);

        if (cancel_hit) {
            LOG_INFO(&g_logger, "Reconnect CANCEL hit at (%d,%d)", tx, ty);
            return 0;
        }
        if (connect_hit) {
            LOG_INFO(&g_logger, "Reconnect CONNECT NOW hit at (%d,%d)", tx, ty);
            return 1;
        }

        /* Update countdown */
        struct timeval now;
        gettimeofday(&now, NULL);
        int elapsed = (int)(now.tv_sec - start.tv_sec);
        remaining = wait_seconds - elapsed;

        /* ~30 fps UI refresh */
        usleep(33000);
    }

    if (!g_running) return -1;
    return 1;   /* countdown expired → try connecting */
}

/* ── VNC session: connect, run main loop, cleanup connection ───────── */

/* Returns:  0 = connection lost (eligible for reconnect)
 *          -1 = connection failed (never connected)
 *           1 = exit gesture (user wants to quit) */
static int vnc_session(const char *host, int port, int attempt) {
    /* ── Show "Connecting" splash ────────────────────────────── */
    vnc_renderer_clear_screen(&g_fb);
    if (attempt > 0) {
        draw_centered_text(&g_fb, 140, "RECONNECTING...", RGB565_YELLOW, 3);
        {
            char att[64];
            snprintf(att, sizeof(att), "ATTEMPT: %d", attempt);
            draw_centered_text(&g_fb, 190, att, RGB565_WHITE, 2);
        }
        {
            char addr[280];
            snprintf(addr, sizeof(addr), "%s:%d", host, port);
            draw_centered_text(&g_fb, 230, addr, RGB565_GREEN, 2);
        }
        draw_centered_text(&g_fb, 280, "WAITING FOR SERVER...", RGB565_GREY, 2);
    } else {
        draw_centered_text(&g_fb, 180, "CONNECTING TO VNC...", RGB565_WHITE, 3);
        {
            char addr[280];
            snprintf(addr, sizeof(addr), "%s:%d", host, port);
            draw_centered_text(&g_fb, 230, addr, RGB565_GREEN, 2);
        }
    }
    fb_swap(&g_fb);

    hw_set_led(LED_GREEN, 50);      /* dim green = connecting */

    /* ── Connect to VNC server ───────────────────────────────── */
    unsigned int ct = (attempt > 0) ? RECONNECT_CONNECT_TIMEOUT
                                    : RECONNECT_INITIAL_CONNECT_TIMEOUT;
    LOG_INFO(&g_logger, "Connecting to %s:%d (timeout %us) ...", host, port, ct);
    g_vnc_client = vnc_client_init(host, port, ct);
    if (!g_vnc_client) {
        vnc_renderer_clear_screen(&g_fb);
        draw_centered_text(&g_fb, 200, "CONNECTION FAILED!", RGB565_RED, 3);
        fb_swap(&g_fb);
        hw_set_led(LED_RED, 100);
        sleep(attempt > 0 ? 1 : 2);   /* shorter flash during reconnect */
        return -1;      /* never connected */
    }
    LOG_INFO(&g_logger, "Connected to VNC server");

    /* ── Initialize renderer ─────────────────────────────────── */
    if (vnc_renderer_init(&g_renderer, &g_fb) < 0) {
        LOG_ERROR(&g_logger, "Failed to initialize renderer");
        rfbClientCleanup(g_vnc_client);
        g_vnc_client = NULL;
        return -1;
    }
    vnc_renderer_set_remote_size(&g_renderer,
                                 g_vnc_client->width, g_vnc_client->height);

    /* ── Initialize input handler ────────────────────────────── */
    if (g_touch_ok) {
        if (vnc_input_init(&g_input, &g_touch, &g_renderer, g_vnc_client) < 0) {
            LOG_WARN(&g_logger, "Input handler init failed");
        }
    }

    /* Brief "Connected" splash */
    vnc_renderer_clear_screen(&g_fb);
    draw_centered_text(&g_fb, 200, "CONNECTED!", RGB565_GREEN, 3);
    fb_swap(&g_fb);
    usleep(500000);     /* 500 ms */

    /* Clear buffer for VNC content */
    vnc_renderer_clear_screen(&g_fb);
    fb_swap(&g_fb);

    /* ── Main event loop ─────────────────────────────────────── */
    LOG_DEBUG(&g_logger, "Entering main loop (target %d fps)", TARGET_FPS);
    hw_set_led(LED_GREEN, 100);     /* full green = connected */

    int session_result = 0;         /* default: connection lost */

    while (g_running) {
        int result = WaitForMessage(g_vnc_client, 10000);
        if (result < 0) {
            LOG_ERROR(&g_logger, "VNC connection lost");
            break;
        }

        if (result > 0) {
            if (!HandleRFBServerMessage(g_vnc_client)) {
                LOG_ERROR(&g_logger, "Error handling VNC message");
                break;
            }
        }

        /* Process touch input → VNC pointer events + exit gesture */
        if (g_touch_ok) {
            vnc_input_process(&g_input);

            if (vnc_input_exit_requested(&g_input)) {
                LOG_INFO(&g_logger, "Exit gesture detected, shutting down...");
                session_result = 1;     /* user quit */
                break;
            }

            /* Draw exit progress indicator while holding corner */
            float prog = vnc_input_exit_progress(&g_input);
            if (prog > 0.01f) {
                uint16_t *buf = (uint16_t *)g_fb.back_buffer;
                int bar_width = (int)(EXIT_ZONE_SIZE * prog);
                uint16_t color = (prog < 0.7f) ? RGB565_YELLOW : RGB565_RED;
                for (int y = 0; y < 3; y++)
                    for (int x = 0; x < bar_width && x < SCREEN_WIDTH; x++)
                        buf[y * SCREEN_WIDTH + x] = color;
                g_renderer.needs_present = true;
            }
        }

        vnc_renderer_present(&g_renderer);
    }

    LOG_INFO(&g_logger, "Session ended (result=%d)", session_result);

    /* Cleanup VNC connection (but NOT framebuffer/touch/watchdog) */
    if (g_vnc_client) {
        rfbClientCleanup(g_vnc_client);
        g_vnc_client = NULL;
    }
    vnc_input_cleanup(&g_input);
    /* Note: renderer is NOT cleaned up here (no 32bpp restore).
     * We stay in 16bpp for the reconnect UI. */

    return session_result;
}

/* ── Main application ──────────────────────────────────────────────── */

static int run_vnc_client(const char *host, int port) {
    int ret = -1;

#if USE_16BPP
    fb_set_bpp(FB_DEVICE, 16);
    DEBUG_PRINT("Switched to 16bpp RGB565");
#endif

    /* Initialize framebuffer */
    if (fb_init(&g_fb, FB_DEVICE) < 0) {
        LOG_ERROR(&g_logger, "Failed to initialize framebuffer");
        return -1;
    }
    LOG_INFO(&g_logger, "Framebuffer: %ux%u, %u bpp, size=%zu",
             g_fb.width, g_fb.height, g_fb.bytes_per_pixel * 8, g_fb.screen_size);

    /* Sanity-check: if the app_launcher (or another 32bpp app) ran before us,
     * fb_set_bpp should have fixed it.  Warn if it didn't. */
    if (g_fb.bytes_per_pixel != 2) {
        LOG_WARN(&g_logger, "Framebuffer is %ubpp, expected 16bpp! "
                 "Display may be corrupted.", g_fb.bytes_per_pixel * 8);
    }

    /* Initialize touch input (non-fatal if absent) */
    if (touch_init(&g_touch, TOUCH_DEVICE) < 0) {
        LOG_WARN(&g_logger, "Touch input not available");
    } else {
        g_touch_ok = true;
        LOG_INFO(&g_logger, "Touch input initialized");
    }

    /* Hardware subsystem */
    hw_init();
    hw_set_backlight(100);

    /* Start watchdog feeder (lives across reconnects) */
    if (pthread_create(&g_watchdog_thread, NULL, watchdog_thread_func, NULL) != 0) {
        LOG_WARN(&g_logger, "Failed to start watchdog thread");
    }

    /* ── Reconnect loop ──────────────────────────────────────────── */
    static const int backoff_delays[] = RECONNECT_DELAYS;
    int attempt = 0;

    while (g_running) {
        int result = vnc_session(host, port, attempt);

        if (result == 1) {
            /* User exit gesture — switch back to app_launcher and exit.
             * respawn.sh re-reads default-app each cycle, so it will
             * start the launcher instead of restarting us. */
            FILE *f = fopen(RESPAWN_CONFIG_FILE, "w");
            if (f) {
                fprintf(f, "%s\n", APP_LAUNCHER_PATH);
                fclose(f);
                LOG_INFO(&g_logger, "Wrote %s to %s",
                         APP_LAUNCHER_PATH, RESPAWN_CONFIG_FILE);
            }
            ret = 0;
            break;
        }

        if (!g_running) {
            ret = 0;    /* clean exit via signal */
            break;
        }

#if RECONNECT_ENABLED
        /* Reset attempt counter after a successful session (connection
         * was established then lost).  Failed connections (-1) keep
         * incrementing so the backoff grows while the server is down. */
        if (result == 0)
            attempt = 0;

        /* Connection lost or failed — enter reconnect loop */
        attempt++;
        LOG_INFO(&g_logger, "Connection lost/failed, reconnect attempt %d", attempt);

        if (RECONNECT_MAX_ATTEMPTS > 0 && attempt > RECONNECT_MAX_ATTEMPTS) {
            LOG_INFO(&g_logger, "Max reconnect attempts (%d) reached, exiting",
                     RECONNECT_MAX_ATTEMPTS);
            ret = -1;
            break;
        }

        /* Pick backoff delay (cap at last entry) */
        int delay_idx = attempt - 1;
        if (delay_idx >= RECONNECT_DELAYS_COUNT)
            delay_idx = RECONNECT_DELAYS_COUNT - 1;
        int wait_sec = backoff_delays[delay_idx];

        /* Drain any lingering touch events before showing UI */
        if (g_touch_ok)
            touch_drain_events(&g_touch);

        int ui_result = reconnect_ui(attempt, wait_sec);
        if (ui_result == 0) {
            /* User cancelled */
            LOG_INFO(&g_logger, "User cancelled reconnect");
            ret = 0;
            break;
        }
        if (ui_result < 0) {
            /* Signal received */
            break;
        }
        /* ui_result == 1 → try connecting again (top of loop) */

        /* Drain touch events so a lingering press from "Connect Now"
         * doesn't bleed into the next vnc_session. */
        if (g_touch_ok)
            touch_drain_events(&g_touch);
#else
        /* Reconnect disabled — exit on first disconnect */
        ret = 0;
        break;
#endif
    }

    /* ── Final cleanup ───────────────────────────────────────────── */
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
        printf("RoomWizard VNC Client v2.2\n\n");
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
    printf("║  RoomWizard VNC Client v2.2           ║\n");
    printf("║  Auto-Reconnect + Bilinear            ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");

    /* Initialize logger (file + stderr) */
    logger_init(&g_logger, "vnc_client", LOG_LEVEL_INFO, true);

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

    LOG_INFO(&g_logger, "Config: %s  Target: %s:%d  Encodings: %s  Compress: %d  Quality: %d",
             config_path, g_config.host, g_config.port,
             g_config.encodings, g_config.compress_level, g_config.quality_level);

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

    LOG_INFO(&g_logger, "VNC client exited (code %d)", ret);
    logger_close(&g_logger);
    printf("\nVNC client exited (code %d)\n", ret);
    return ret;
}
