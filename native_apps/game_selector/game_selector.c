/*
 * Game Selector for RoomWizard
 * Displays a menu of available games and launches selected game
 * Uses centralized Button widget from common library
 * Supports keyboard, gamepad, and touch input via unified gamepad module
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include "common/common.h"
#include "common/hardware.h"
#include "common/gamepad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_GAMES 10
#define GAME_DIR "/opt/games"
#define BUTTON_HEIGHT 80
#define BUTTON_MARGIN 20
#define EXIT_BUTTON_HEIGHT 60
#define RESCAN_INTERVAL_MS 5000

/* Selection highlight border */
#define HIGHLIGHT_COLOR     RGB(0, 220, 255)
#define HIGHLIGHT_BORDER    3

typedef struct {
    char name[256];
    char path[512];
} GameEntry;

typedef struct {
    GameEntry games[MAX_GAMES];
    Button game_buttons[MAX_GAMES];  // Button widgets for each game
    Button exit_button;              // Exit button widget
    int game_count;
    int selected_game;               // Currently highlighted game index (-1 = exit button)
    int scroll_offset;               // For scrolling through games
    Framebuffer fb;
    TouchInput touch;
    GamepadManager gamepad;
    InputState input;
    uint32_t last_rescan_ms;
} GameSelector;

// Scan directory for executable games
int scan_games(GameSelector *selector) {
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    
    selector->game_count = 0;
    
    dir = opendir(GAME_DIR);
    if (!dir) {
        perror("Error opening games directory");
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL && selector->game_count < MAX_GAMES) {
        // Skip hidden-filesystem entries and the selector itself
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, "game_selector") == 0) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", GAME_DIR, entry->d_name);

        // Skip non-executables (covers .noargs, .hidden, data files, etc.)
        if (stat(full_path, &st) != 0 || !(st.st_mode & S_IXUSR))
            continue;

        // Skip entries with a <name>.hidden marker file alongside the binary.
        // Create the marker: touch /opt/games/<name>.hidden && chmod 644 /opt/games/<name>.hidden
        char hidden_path[600];
        snprintf(hidden_path, sizeof(hidden_path), "%s/%s.hidden", GAME_DIR, entry->d_name);
        struct stat hidden_st;
        if (stat(hidden_path, &hidden_st) == 0) {
            printf("Skipping hidden entry: %s\n", entry->d_name);
            continue;
        }

        strncpy(selector->games[selector->game_count].name, entry->d_name, 255);
        strncpy(selector->games[selector->game_count].path, full_path, 511);
        printf("Added game: %s\n", entry->d_name);
        selector->game_count++;
    }
    
    closedir(dir);
    return selector->game_count;
}

// Compute max visible buttons given the current screen layout
static int get_max_visible(GameSelector *selector) {
    int start_y = SCREEN_SAFE_TOP + 80;
    int available_height = SCREEN_SAFE_BOTTOM - start_y - EXIT_BUTTON_HEIGHT - 3 * BUTTON_MARGIN;
    return available_height / (BUTTON_HEIGHT + BUTTON_MARGIN);
}

// Initialize button positions for current scroll state
void update_button_positions(GameSelector *selector) {
    int start_y = SCREEN_SAFE_TOP + 80;  // Start below title
    int button_width = SCREEN_SAFE_WIDTH - 2 * BUTTON_MARGIN;
    int max_visible = get_max_visible(selector);
    
    // Update game button positions
    for (int i = 0; i < max_visible && (i + selector->scroll_offset) < selector->game_count; i++) {
        int game_idx = i + selector->scroll_offset;
        int y = start_y + i * (BUTTON_HEIGHT + BUTTON_MARGIN);
        uint32_t bg_color = RGB(40, 40, 80);
        
        button_init_full(&selector->game_buttons[game_idx],
                        SCREEN_SAFE_LEFT + BUTTON_MARGIN, y, button_width, BUTTON_HEIGHT,
                        selector->games[game_idx].name,
                        bg_color, COLOR_WHITE, BTN_HIGHLIGHT_COLOR, 3);
    }
    
    // Update exit button position (using safe area)
    int exit_y = SCREEN_SAFE_BOTTOM - EXIT_BUTTON_HEIGHT - BUTTON_MARGIN;
    button_init_full(&selector->exit_button,
                    SCREEN_SAFE_LEFT + BUTTON_MARGIN, exit_y, button_width, EXIT_BUTTON_HEIGHT,
                    "EXIT TO SYSTEM",
                    RGB(80, 20, 20), COLOR_WHITE, BTN_HIGHLIGHT_COLOR, 3);
}

// Draw selection highlight border around a button
static void draw_selection_highlight(GameSelector *selector, Button *btn) {
    int x = btn->x - HIGHLIGHT_BORDER;
    int y = btn->y - HIGHLIGHT_BORDER;
    int w = btn->width + 2 * HIGHLIGHT_BORDER;
    int h = btn->height + 2 * HIGHLIGHT_BORDER;
    
    // Draw multiple-pixel-thick border
    for (int b = 0; b < HIGHLIGHT_BORDER; b++) {
        fb_draw_rect(&selector->fb, x + b, y + b, w - 2 * b, h - 2 * b, HIGHLIGHT_COLOR);
    }
}

// Draw the game selector menu
void draw_menu(GameSelector *selector) {
    fb_clear(&selector->fb, COLOR_BLACK);
    
    // Draw title (using safe area)
    text_draw_centered(&selector->fb, selector->fb.width / 2, LAYOUT_TITLE_Y, "ROOMWIZARD GAMES", COLOR_WHITE, 4);
    
    int start_y = SCREEN_SAFE_TOP + 80;  // Start below title
    int max_visible = get_max_visible(selector);
    
    // Calculate scroll limits
    int max_scroll = selector->game_count - max_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (selector->scroll_offset > max_scroll) selector->scroll_offset = max_scroll;
    if (selector->scroll_offset < 0) selector->scroll_offset = 0;
    
    // Update button positions for current scroll state
    update_button_positions(selector);
    
    // Draw up arrow if scrolled down
    if (selector->scroll_offset > 0) {
        int arrow_x = selector->fb.width / 2 - 20;
        int arrow_y = start_y - 35;
        fb_draw_text(&selector->fb, arrow_x, arrow_y, "^^^", COLOR_CYAN, 3);
        fb_draw_text(&selector->fb, arrow_x - 30, arrow_y + 5, "TAP TO SCROLL UP", COLOR_CYAN, 1);
    }
    
    // Draw visible game buttons using Button widget
    for (int i = 0; i < max_visible && (i + selector->scroll_offset) < selector->game_count; i++) {
        int game_idx = i + selector->scroll_offset;
        button_draw(&selector->fb, &selector->game_buttons[game_idx]);
        
        // Draw selection highlight if this game is selected
        if (game_idx == selector->selected_game) {
            draw_selection_highlight(selector, &selector->game_buttons[game_idx]);
        }
    }
    
    // Draw down arrow if more games below
    if (selector->scroll_offset < max_scroll) {
        int arrow_y = start_y + max_visible * (BUTTON_HEIGHT + BUTTON_MARGIN);
        int arrow_x = selector->fb.width / 2 - 20;
        fb_draw_text(&selector->fb, arrow_x, arrow_y + 10, "vvv", COLOR_CYAN, 3);
        fb_draw_text(&selector->fb, arrow_x - 40, arrow_y + 35, "TAP TO SCROLL DOWN", COLOR_CYAN, 1);
    }
    
    // Draw exit button using Button widget
    button_draw(&selector->fb, &selector->exit_button);
    
    // Draw selection highlight on exit button if selected
    if (selector->selected_game == -1) {
        draw_selection_highlight(selector, &selector->exit_button);
    }
    
    // Draw input hint at bottom
    if (selector->input.gamepad_connected || selector->input.keyboard_connected)
        fb_draw_text(&selector->fb, SCREEN_SAFE_LEFT + 10, selector->fb.height - 18,
                     "D-PAD: NAVIGATE  A/ENTER: SELECT  BACK: EXIT", RGB(100, 100, 100), 1);
    
    // Swap buffers for double buffering
    fb_swap(&selector->fb);
}

// Handle touch input (returns game index, -1 for none/scroll, -2 for exit)
int handle_touch(GameSelector *selector, int x, int y, uint32_t current_time) {
    int start_y = SCREEN_SAFE_TOP + 80;
    int available_height = selector->fb.height - start_y - EXIT_BUTTON_HEIGHT - 3 * BUTTON_MARGIN;
    int max_visible = available_height / (BUTTON_HEIGHT + BUTTON_MARGIN);
    
    printf("Touch handler: x=%d, y=%d\n", x, y);
    printf("  max_visible=%d, scroll_offset=%d, game_count=%d\n", 
           max_visible, selector->scroll_offset, selector->game_count);
    
    // Check up arrow area (entire area above first button)
    if (y < start_y && selector->scroll_offset > 0) {
        printf("  -> Scrolling UP\n");
        selector->scroll_offset--;
        return -1;  // Redraw needed
    }
    
    // Check game buttons (visible ones) using button_update()
    for (int i = 0; i < max_visible && (i + selector->scroll_offset) < selector->game_count; i++) {
        int game_idx = i + selector->scroll_offset;
        if (button_update(&selector->game_buttons[game_idx], x, y, true, current_time)) {
            printf("  -> HIT button %d (%s)!\n", game_idx, selector->games[game_idx].name);
            selector->selected_game = game_idx;  // Update selection on touch
            return game_idx;  // Return game index
        }
    }
    
    // Check down arrow area (between last button and exit button)
    int last_button_bottom = start_y + max_visible * (BUTTON_HEIGHT + BUTTON_MARGIN);
    int exit_y = selector->fb.height - EXIT_BUTTON_HEIGHT - BUTTON_MARGIN;
    int max_scroll = selector->game_count - max_visible;
    if (max_scroll < 0) max_scroll = 0;
    
    printf("  Checking down arrow: y between %d and %d\n", last_button_bottom, exit_y);
    if (y > last_button_bottom && y < exit_y && selector->scroll_offset < max_scroll) {
        printf("  -> Scrolling DOWN\n");
        selector->scroll_offset++;
        return -1;  // Redraw needed
    }
    
    // Check exit button using button_update()
    printf("  Checking exit button: y between %d and %d\n", exit_y, exit_y + EXIT_BUTTON_HEIGHT);
    if (button_update(&selector->exit_button, x, y, true, current_time)) {
        printf("  -> HIT exit button!\n");
        return -2;  // Exit signal
    }
    
    printf("  -> No button hit\n");
    return -1;  // No button touched
}

// Ensure the selected game is visible (adjust scroll if needed)
static void ensure_selection_visible(GameSelector *selector) {
    int max_visible = get_max_visible(selector);
    int max_scroll = selector->game_count - max_visible;
    if (max_scroll < 0) max_scroll = 0;
    
    if (selector->selected_game >= 0) {
        // Scroll down if selection is below visible range
        if (selector->selected_game >= selector->scroll_offset + max_visible) {
            selector->scroll_offset = selector->selected_game - max_visible + 1;
        }
        // Scroll up if selection is above visible range
        if (selector->selected_game < selector->scroll_offset) {
            selector->scroll_offset = selector->selected_game;
        }
    }
    
    // Clamp scroll
    if (selector->scroll_offset > max_scroll) selector->scroll_offset = max_scroll;
    if (selector->scroll_offset < 0) selector->scroll_offset = 0;
}

// Handle gamepad/keyboard navigation
// Returns: game index to launch (>=0), -1 for nothing, -2 for exit
static int handle_gamepad_input(GameSelector *selector) {
    InputState *inp = &selector->input;
    
    // Navigate down
    if (inp->buttons[BTN_ID_DOWN].pressed) {
        if (selector->selected_game < selector->game_count - 1) {
            selector->selected_game++;
        } else {
            // Wrap to exit button
            selector->selected_game = -1;
        }
        ensure_selection_visible(selector);
    }
    
    // Navigate up
    if (inp->buttons[BTN_ID_UP].pressed) {
        if (selector->selected_game == -1) {
            // From exit button, go to last game
            selector->selected_game = selector->game_count - 1;
        } else if (selector->selected_game > 0) {
            selector->selected_game--;
        }
        ensure_selection_visible(selector);
    }
    
    // Select / launch
    if (inp->buttons[BTN_ID_JUMP].pressed ||
        inp->buttons[BTN_ID_ACTION].pressed) {
        if (selector->selected_game >= 0 && selector->selected_game < selector->game_count) {
            return selector->selected_game;
        } else if (selector->selected_game == -1) {
            return -2;  // Exit
        }
    }
    
    // Back button → exit
    if (inp->buttons[BTN_ID_BACK].pressed) {
        return -2;
    }
    
    return -1;  // Nothing
}

// Launch a game and wait for it to exit
int launch_game(GameSelector *selector, int game_index, const char *fb_dev, const char *touch_dev) {
    if (game_index < 0 || game_index >= selector->game_count) {
        return -1;
    }
    
    printf("Launching game: %s\n", selector->games[game_index].name);
    
    // Check for a <name>.noargs marker: if present, launch without device paths.
    // This allows non-native executables (e.g. ScummVM) that open devices
    // themselves to be launched cleanly without unexpected arguments.
    char noargs_path[600];
    snprintf(noargs_path, sizeof(noargs_path), "%s/%s.noargs",
             GAME_DIR, selector->games[game_index].name);
    struct stat noargs_st;
    int use_no_args = (stat(noargs_path, &noargs_st) == 0);
    printf("  .noargs marker %s: %s\n", noargs_path, use_no_args ? "found" : "not found");
    
    // Clear screen before launching
    fb_clear(&selector->fb, COLOR_BLACK);
    fb_swap(&selector->fb);
    
    // Close gamepad, touch, and framebuffer so game has exclusive access
    gamepad_close(&selector->gamepad);
    touch_close(&selector->touch);
    fb_close(&selector->fb);
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - execute game, optionally with device paths
        if (use_no_args) {
            execl(selector->games[game_index].path, selector->games[game_index].name, NULL);
        } else {
            execl(selector->games[game_index].path, selector->games[game_index].name,
                  fb_dev, touch_dev, NULL);
        }
        perror("Failed to execute game");
        exit(1);
    } else if (pid > 0) {
        // Parent process - wait for game to exit (EINTR-safe)
        int status;
        pid_t ret;
        do {
            ret = waitpid(pid, &status, 0);
        } while (ret == -1 && errno == EINTR);
        printf("Game exited with status: %d\n", (ret > 0) ? WEXITSTATUS(status) : -1);
        
        // Re-acquire framebuffer (restore 32bpp in case game left 16bpp)
        fb_set_bpp(fb_dev, 32);
        if (fb_init(&selector->fb, fb_dev) != 0) {
            fprintf(stderr, "Failed to re-initialise framebuffer\n");
        }
        
        // Reopen touch input after game exits
        if (touch_init(&selector->touch, touch_dev) == 0) {
            touch_set_screen_size(&selector->touch, selector->fb.width, selector->fb.height);
            touch_drain_events(&selector->touch);  /* Discard stale events */
        }

        // Re-init gamepad after game exits
        gamepad_init(&selector->gamepad);

        hw_reload_config();  /* Re-read config in case child app changed it */
        hw_set_backlight(100);  /* Restore configured backlight after game exits */
        
        return 0;
    } else {
        perror("Failed to fork");
        return -1;
    }
}

int main(int argc, char *argv[]) {
    GameSelector selector;
    const char *fb_device = "/dev/fb0";
    const char *touch_device = "/dev/input/touchscreen0";
    
    // Override devices from command line if provided
    if (argc > 1) fb_device = argv[1];
    if (argc > 2) touch_device = argv[2];
    
    printf("RoomWizard Game Selector\n");
    printf("========================\n");

    /* Singleton guard — exit immediately if another instance is running */
    int lock_fd = acquire_instance_lock("game_selector");
    if (lock_fd < 0) {
        fprintf(stderr, "game_selector: another instance is already running\n");
        return 1;
    }
    
    // Initialize framebuffer
    if (fb_init(&selector.fb, fb_device) != 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    
    // Initialize touch input
    if (touch_init(&selector.touch, touch_device) != 0) {
        fprintf(stderr, "Failed to initialize touch input\n");
        fb_close(&selector.fb);
        return 1;
    }
    
    // Set screen size for coordinate scaling
    touch_set_screen_size(&selector.touch, selector.fb.width, selector.fb.height);

    // Initialize gamepad (keyboard, gamepad, mouse)
    gamepad_init(&selector.gamepad);
    memset(&selector.input, 0, sizeof(selector.input));
    selector.last_rescan_ms = 0;
    
    // Scan for available games
    if (scan_games(&selector) <= 0) {
        fprintf(stderr, "No games found in %s\n", GAME_DIR);
        fb_draw_text(&selector.fb, 50, 50, "No games found!", COLOR_RED, 3);
        sleep(3);
        gamepad_close(&selector.gamepad);
        touch_close(&selector.touch);
        fb_close(&selector.fb);
        return 1;
    }
    
    printf("Found %d games\n", selector.game_count);
    for (int i = 0; i < selector.game_count; i++) {
        printf("  %d. %s\n", i+1, selector.games[i].name);
    }
    
    selector.selected_game = 0;
    selector.scroll_offset = 0;  // Initialize scroll position
    
    // Main loop — polling-based (~60fps)
    int running = 1;
    while (running) {
        // Poll touch input (non-blocking)
        touch_poll(&selector.touch);
        TouchState state = touch_get_state(&selector.touch);

        // Poll gamepad/keyboard through unified API
        gamepad_poll(&selector.gamepad, &selector.input,
                     state.x, state.y, state.pressed);

        // Periodic device rescan for hotplug support
        uint32_t current_time = get_time_ms();
        if (current_time - selector.last_rescan_ms > RESCAN_INTERVAL_MS) {
            selector.last_rescan_ms = current_time;
            gamepad_rescan(&selector.gamepad);
        }

        // Handle touch input
        if (state.pressed) {
            printf("Touch at: (%d,%d)\n", state.x, state.y);
            int result = handle_touch(&selector, state.x, state.y, current_time);
            printf("Result: %d\n", result);
            
            if (result >= 0) {
                // Launch game with device paths
                launch_game(&selector, result, fb_device, touch_device);
                // Game exited, continue loop to redraw menu
            } else if (result == -2) {
                // Exit button pressed
                printf("Exiting game selector\n");
                running = 0;
            }
        }

        // Handle gamepad/keyboard navigation
        int gp_result = handle_gamepad_input(&selector);
        if (gp_result >= 0) {
            // Launch selected game
            launch_game(&selector, gp_result, fb_device, touch_device);
            // Game exited, continue loop to redraw menu
        } else if (gp_result == -2) {
            printf("Exiting game selector (gamepad)\n");
            running = 0;
        }

        // Draw the menu
        draw_menu(&selector);
        
        usleep(16667);  // ~60 FPS
    }
    
    // Cleanup
    hw_set_backlight(100);
    fb_clear(&selector.fb, COLOR_BLACK);
    fb_swap(&selector.fb);  // Make sure the clear is visible
    gamepad_close(&selector.gamepad);
    touch_close(&selector.touch);
    fb_close(&selector.fb);
    
    return 0;
}
