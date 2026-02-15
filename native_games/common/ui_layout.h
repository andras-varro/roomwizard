/**
 * UI Layout Manager for RoomWizard
 * 
 * Provides layout managers for:
 * - Grid layout (auto-calculate positions, handle overflow)
 * - List layout (scrollable vertical list)
 * - Flow layout (auto-wrap items)
 */

#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "framebuffer.h"
#include <stdint.h>
#include <stdbool.h>

// Layout types
typedef enum {
    LAYOUT_GRID,
    LAYOUT_LIST,
    LAYOUT_FLOW
} LayoutType;

// Grid layout configuration
typedef struct {
    int columns;              // Number of columns
    int item_width;           // Width of each item
    int item_height;          // Height of each item
    int spacing_x;            // Horizontal spacing
    int spacing_y;            // Vertical spacing
    int margin_left;          // Left margin
    int margin_top;           // Top margin
    int margin_right;         // Right margin
    int margin_bottom;        // Bottom margin
} GridLayoutConfig;

// List layout configuration
typedef struct {
    int item_height;          // Height of each item
    int spacing;              // Spacing between items
    int margin_left;          // Left margin
    int margin_top;           // Top margin
    int margin_right;         // Right margin
    int margin_bottom;        // Bottom margin
    int visible_items;        // Number of items visible at once
} ListLayoutConfig;

// Layout manager
typedef struct {
    LayoutType type;
    int screen_width;
    int screen_height;
    
    union {
        GridLayoutConfig grid;
        ListLayoutConfig list;
    } config;
    
    // Scrolling state
    int scroll_offset;        // Current scroll position
    int total_items;          // Total number of items
    int visible_items;        // Number of items currently visible
    bool can_scroll_up;       // Can scroll up
    bool can_scroll_down;     // Can scroll down
} UILayout;

// Initialize grid layout
void ui_layout_init_grid(UILayout *layout, int screen_width, int screen_height,
                        int columns, int item_width, int item_height,
                        int spacing_x, int spacing_y,
                        int margin_left, int margin_top, int margin_right, int margin_bottom);

// Initialize list layout
void ui_layout_init_list(UILayout *layout, int screen_width, int screen_height,
                        int item_height, int spacing,
                        int margin_left, int margin_top, int margin_right, int margin_bottom);

// Update layout with item count
void ui_layout_update(UILayout *layout, int total_items);

// Get position for item at index
bool ui_layout_get_item_position(UILayout *layout, int index, int *x, int *y, int *width, int *height);

// Check if item at index is visible
bool ui_layout_is_item_visible(UILayout *layout, int index);

// Scroll up (returns true if scrolled)
bool ui_layout_scroll_up(UILayout *layout);

// Scroll down (returns true if scrolled)
bool ui_layout_scroll_down(UILayout *layout);

// Get item index at touch position (returns -1 if none)
int ui_layout_get_item_at_position(UILayout *layout, int touch_x, int touch_y);

// Draw scroll indicators
void ui_layout_draw_scroll_indicators(Framebuffer *fb, UILayout *layout);

// Helper: Calculate grid layout dimensions
void ui_layout_grid_calculate(GridLayoutConfig *config, int screen_width, int screen_height,
                              int total_items, int *out_rows, int *out_cols);

// Helper: Calculate list layout dimensions
void ui_layout_list_calculate(ListLayoutConfig *config, int screen_height,
                              int total_items, int *out_visible);

// ============================================================================
// SCROLLABLE LIST WIDGET
// ============================================================================

// Scrollable list item callback
typedef void (*ListItemDrawCallback)(Framebuffer *fb, int x, int y, int width, int height,
                                     const char *text, bool is_selected, void *user_data);

// Scrollable list widget
typedef struct {
    UILayout layout;
    char **items;              // Array of item strings
    int item_count;            // Number of items
    int selected_index;        // Currently selected item (-1 = none)
    
    // Styling
    uint32_t bg_color;
    uint32_t selected_color;
    uint32_t text_color;
    uint32_t border_color;
    int text_scale;
    
    // Custom drawing (optional)
    ListItemDrawCallback custom_draw;
    void *user_data;
    
    // Scroll indicator areas
    int scroll_up_y;           // Y position of scroll up area
    int scroll_down_y;         // Y position of scroll down area
} ScrollableList;

// Initialize scrollable list
void scrollable_list_init(ScrollableList *list, int screen_width, int screen_height,
                         int item_height, int spacing,
                         int margin_left, int margin_top, int margin_right, int margin_bottom);

// Set items (copies pointers, does not copy strings)
void scrollable_list_set_items(ScrollableList *list, char **items, int count);

// Set colors
void scrollable_list_set_colors(ScrollableList *list, uint32_t bg, uint32_t selected,
                               uint32_t text, uint32_t border);

// Set text scale
void scrollable_list_set_text_scale(ScrollableList *list, int scale);

// Set custom draw callback
void scrollable_list_set_custom_draw(ScrollableList *list, ListItemDrawCallback callback, void *user_data);

// Draw the list
void scrollable_list_draw(Framebuffer *fb, ScrollableList *list);

// Handle touch input (returns item index if clicked, -1 if scroll/none, -2 if outside)
int scrollable_list_handle_touch(ScrollableList *list, int touch_x, int touch_y);

// Get selected item index
int scrollable_list_get_selected(ScrollableList *list);

// Set selected item index
void scrollable_list_set_selected(ScrollableList *list, int index);

#endif
