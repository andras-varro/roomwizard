/*
 * Game Selector for RoomWizard
 * Displays a menu of available games and launches selected game
 * Includes exit button to return to selector
 */

#include "common/framebuffer.h"
#include "common/touch_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define MAX_GAMES 10
#define GAME_DIR "/opt/games"
#define BUTTON_HEIGHT 80
#define BUTTON_MARGIN 20
#define EXIT_BUTTON_HEIGHT 60

typedef struct {
    char name[256];
    char path[512];
} GameEntry;

typedef struct {
    GameEntry games[MAX_GAMES];
    int game_count;
    int selected_game;
    int scroll_offset;  // For scrolling through games
    Framebuffer fb;
    TouchInput touch;
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
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, "game_selector") == 0) continue;
        if (strcmp(entry->d_name, "watchdog_feeder") == 0) continue;
        if (strcmp(entry->d_name, "touch_test") == 0) continue;
        if (strcmp(entry->d_name, "touch_debug") == 0) continue;
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", GAME_DIR, entry->d_name);
        
        if (stat(full_path, &st) == 0 && (st.st_mode & S_IXUSR)) {
            strncpy(selector->games[selector->game_count].name, entry->d_name, 255);
            strncpy(selector->games[selector->game_count].path, full_path, 511);
            printf("Added game: %s\n", entry->d_name);
            selector->game_count++;
        }
    }
    
    closedir(dir);
    return selector->game_count;
}

// Draw a button
void draw_button(Framebuffer *fb, int x, int y, int w, int h, const char *text, 
                 uint32_t bg_color, uint32_t text_color) {
    // Draw button background
    fb_fill_rect(fb, x, y, w, h, bg_color);
    
    // Draw button border
    fb_draw_rect(fb, x, y, w, h, COLOR_WHITE);
    fb_draw_rect(fb, x+1, y+1, w-2, h-2, COLOR_WHITE);
    
    // Convert text to uppercase for font compatibility
    char upper_text[256];
    int i = 0;
    while (text[i] && i < 255) {
        upper_text[i] = (text[i] >= 'a' && text[i] <= 'z') ? text[i] - 32 : text[i];
        i++;
    }
    upper_text[i] = '\0';
    
    // Draw text centered
    int text_len = strlen(upper_text);
    int text_width = text_len * 6 * 3;  // 6 pixels per char, scale 3
    int text_x = x + (w - text_width) / 2;
    int text_y = y + (h - 7 * 3) / 2;  // 7 pixels height, scale 3
    
    fb_draw_text(fb, text_x, text_y, upper_text, text_color, 3);
}

// Draw the game selector menu
void draw_menu(GameSelector *selector) {
    fb_clear(&selector->fb, COLOR_BLACK);
    
    // Draw title
    fb_draw_text(&selector->fb, 20, 20, "ROOMWIZARD GAMES", COLOR_WHITE, 4);
    
    int start_y = 100;
    int button_width = selector->fb.width - 2 * BUTTON_MARGIN;
    int available_height = selector->fb.height - start_y - EXIT_BUTTON_HEIGHT - 3 * BUTTON_MARGIN;
    int max_visible = available_height / (BUTTON_HEIGHT + BUTTON_MARGIN);
    
    // Calculate scroll limits
    int max_scroll = selector->game_count - max_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (selector->scroll_offset > max_scroll) selector->scroll_offset = max_scroll;
    if (selector->scroll_offset < 0) selector->scroll_offset = 0;
    
    // Draw up arrow if scrolled down
    if (selector->scroll_offset > 0) {
        int arrow_x = selector->fb.width / 2 - 20;
        int arrow_y = start_y - 35;
        fb_draw_text(&selector->fb, arrow_x, arrow_y, "^^^", COLOR_CYAN, 3);
        fb_draw_text(&selector->fb, arrow_x - 30, arrow_y + 5, "TAP TO SCROLL UP", COLOR_CYAN, 1);
    }
    
    // Draw visible game buttons
    for (int i = 0; i < max_visible && (i + selector->scroll_offset) < selector->game_count; i++) {
        int game_idx = i + selector->scroll_offset;
        int y = start_y + i * (BUTTON_HEIGHT + BUTTON_MARGIN);
        uint32_t bg_color = (game_idx == selector->selected_game) ? COLOR_BLUE : RGB(40, 40, 80);
        
        draw_button(&selector->fb, BUTTON_MARGIN, y, button_width, BUTTON_HEIGHT,
                   selector->games[game_idx].name, bg_color, COLOR_WHITE);
    }
    
    // Draw down arrow if more games below
    if (selector->scroll_offset < max_scroll) {
        int arrow_y = start_y + max_visible * (BUTTON_HEIGHT + BUTTON_MARGIN);
        int arrow_x = selector->fb.width / 2 - 20;
        fb_draw_text(&selector->fb, arrow_x, arrow_y + 10, "vvv", COLOR_CYAN, 3);
        fb_draw_text(&selector->fb, arrow_x - 40, arrow_y + 35, "TAP TO SCROLL DOWN", COLOR_CYAN, 1);
    }
    
    // Draw exit button at bottom
    int exit_y = selector->fb.height - EXIT_BUTTON_HEIGHT - BUTTON_MARGIN;
    draw_button(&selector->fb, BUTTON_MARGIN, exit_y, button_width, EXIT_BUTTON_HEIGHT,
               "EXIT TO SYSTEM", RGB(80, 20, 20), COLOR_WHITE);
    
    // Swap buffers for double buffering
    fb_swap(&selector->fb);
}

// Check if touch is within button bounds
int is_touch_in_button(int touch_x, int touch_y, int btn_x, int btn_y, int btn_w, int btn_h) {
    return (touch_x >= btn_x && touch_x < btn_x + btn_w &&
            touch_y >= btn_y && touch_y < btn_y + btn_h);
}

// Handle touch input
int handle_touch(GameSelector *selector, int x, int y) {
    int start_y = 100;
    int button_width = selector->fb.width - 2 * BUTTON_MARGIN;
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
    
    // Check game buttons (visible ones)
    for (int i = 0; i < max_visible && (i + selector->scroll_offset) < selector->game_count; i++) {
        int btn_y = start_y + i * (BUTTON_HEIGHT + BUTTON_MARGIN);
        if (is_touch_in_button(x, y, BUTTON_MARGIN, btn_y, button_width, BUTTON_HEIGHT)) {
            int game_idx = i + selector->scroll_offset;
            printf("  -> HIT button %d (%s)!\n", game_idx, selector->games[game_idx].name);
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
    
    // Check exit button
    printf("  Checking exit button: y between %d and %d\n", exit_y, exit_y + EXIT_BUTTON_HEIGHT);
    if (is_touch_in_button(x, y, BUTTON_MARGIN, exit_y, button_width, EXIT_BUTTON_HEIGHT)) {
        printf("  -> HIT exit button!\n");
        return -2;  // Exit signal
    }
    
    printf("  -> No button hit\n");
    return -1;  // No button touched
}

// Launch a game and wait for it to exit
int launch_game(GameSelector *selector, int game_index, const char *fb_dev, const char *touch_dev) {
    if (game_index < 0 || game_index >= selector->game_count) {
        return -1;
    }
    
    printf("Launching game: %s\n", selector->games[game_index].name);
    
    // Clear screen before launching
    fb_clear(&selector->fb, COLOR_BLACK);
    
    // Close our touch input so game has exclusive access
    touch_close(&selector->touch);
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - execute game with device paths
        execl(selector->games[game_index].path, selector->games[game_index].name,
              fb_dev, touch_dev, NULL);
        perror("Failed to execute game");
        exit(1);
    } else if (pid > 0) {
        // Parent process - wait for game to exit
        int status;
        waitpid(pid, &status, 0);
        printf("Game exited with status: %d\n", WEXITSTATUS(status));
        
        // Reopen touch input after game exits
        if (touch_init(&selector->touch, touch_dev) == 0) {
            touch_set_screen_size(&selector->touch, selector->fb.width, selector->fb.height);
        }
        
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
    
    // Scan for available games
    if (scan_games(&selector) <= 0) {
        fprintf(stderr, "No games found in %s\n", GAME_DIR);
        fb_draw_text(&selector.fb, 50, 50, "No games found!", COLOR_RED, 3);
        sleep(3);
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
    
    // Main loop
    int running = 1;
    while (running) {
        draw_menu(&selector);
        
        // Wait for touch input (coordinates are now auto-scaled by touch library)
        int x, y;
        if (touch_wait_for_press(&selector.touch, &x, &y) == 0) {
            printf("Touch at: (%d,%d)\n", x, y);
            int result = handle_touch(&selector, x, y);
            printf("Result: %d\n", result);
            
            if (result >= 0) {
                // Launch game with device paths
                launch_game(&selector, result, fb_device, touch_device);
                // Game exited, redraw menu
            } else if (result == -2) {
                // Exit button pressed
                printf("Exiting game selector\n");
                running = 0;
            }
        }
        
        usleep(50000);  // 50ms delay
    }
    
    // Cleanup
    fb_clear(&selector.fb, COLOR_BLACK);
    touch_close(&selector.touch);
    fb_close(&selector.fb);
    
    return 0;
}
