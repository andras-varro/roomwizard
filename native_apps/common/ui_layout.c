/**
 * UI Layout Manager Implementation
 */

#include "ui_layout.h"
#include <string.h>

// Initialize grid layout
void ui_layout_init_grid(UILayout *layout, int screen_width, int screen_height,
                        int columns, int item_width, int item_height,
                        int spacing_x, int spacing_y,
                        int margin_left, int margin_top, int margin_right, int margin_bottom) {
    layout->type = LAYOUT_GRID;
    layout->screen_width = screen_width;
    layout->screen_height = screen_height;
    
    layout->config.grid.columns = columns;
    layout->config.grid.item_width = item_width;
    layout->config.grid.item_height = item_height;
    layout->config.grid.spacing_x = spacing_x;
    layout->config.grid.spacing_y = spacing_y;
    layout->config.grid.margin_left = margin_left;
    layout->config.grid.margin_top = margin_top;
    layout->config.grid.margin_right = margin_right;
    layout->config.grid.margin_bottom = margin_bottom;
    
    layout->scroll_offset = 0;
    layout->total_items = 0;
    layout->visible_items = 0;
    layout->can_scroll_up = false;
    layout->can_scroll_down = false;
}

// Initialize list layout
void ui_layout_init_list(UILayout *layout, int screen_width, int screen_height,
                        int item_height, int spacing,
                        int margin_left, int margin_top, int margin_right, int margin_bottom) {
    layout->type = LAYOUT_LIST;
    layout->screen_width = screen_width;
    layout->screen_height = screen_height;
    
    layout->config.list.item_height = item_height;
    layout->config.list.spacing = spacing;
    layout->config.list.margin_left = margin_left;
    layout->config.list.margin_top = margin_top;
    layout->config.list.margin_right = margin_right;
    layout->config.list.margin_bottom = margin_bottom;
    
    // Calculate visible items
    int available_height = screen_height - margin_top - margin_bottom;
    layout->config.list.visible_items = available_height / (item_height + spacing);
    
    layout->scroll_offset = 0;
    layout->total_items = 0;
    layout->visible_items = 0;
    layout->can_scroll_up = false;
    layout->can_scroll_down = false;
}

// Update layout with item count
void ui_layout_update(UILayout *layout, int total_items) {
    layout->total_items = total_items;
    
    if (layout->type == LAYOUT_GRID) {
        GridLayoutConfig *cfg = &layout->config.grid;
        
        // Calculate rows needed
        int rows = (total_items + cfg->columns - 1) / cfg->columns;
        
        // Calculate available height
        int available_height = layout->screen_height - cfg->margin_top - cfg->margin_bottom;
        
        // Calculate visible rows
        int visible_rows = available_height / (cfg->item_height + cfg->spacing_y);
        
        // Calculate visible items
        layout->visible_items = visible_rows * cfg->columns;
        
        // Update scroll state
        layout->can_scroll_up = layout->scroll_offset > 0;
        layout->can_scroll_down = (layout->scroll_offset + visible_rows) < rows;
        
    } else if (layout->type == LAYOUT_LIST) {
        ListLayoutConfig *cfg = &layout->config.list;
        
        layout->visible_items = cfg->visible_items;
        
        // Update scroll state
        layout->can_scroll_up = layout->scroll_offset > 0;
        layout->can_scroll_down = (layout->scroll_offset + cfg->visible_items) < total_items;
    }
}

// Get position for item at index
bool ui_layout_get_item_position(UILayout *layout, int index, int *x, int *y, int *width, int *height) {
    if (index < 0 || index >= layout->total_items) {
        return false;
    }
    
    if (layout->type == LAYOUT_GRID) {
        GridLayoutConfig *cfg = &layout->config.grid;
        
        int row = index / cfg->columns;
        int col = index % cfg->columns;
        
        // Adjust for scroll
        row -= layout->scroll_offset;
        
        // Check if visible
        int available_height = layout->screen_height - cfg->margin_top - cfg->margin_bottom;
        int visible_rows = available_height / (cfg->item_height + cfg->spacing_y);
        
        if (row < 0 || row >= visible_rows) {
            return false;  // Not visible
        }
        
        // Calculate position
        *x = cfg->margin_left + col * (cfg->item_width + cfg->spacing_x);
        *y = cfg->margin_top + row * (cfg->item_height + cfg->spacing_y);
        *width = cfg->item_width;
        *height = cfg->item_height;
        
        // Check if item fits on screen
        if (*x + *width > layout->screen_width - cfg->margin_right) {
            return false;  // Doesn't fit
        }
        
        return true;
        
    } else if (layout->type == LAYOUT_LIST) {
        ListLayoutConfig *cfg = &layout->config.list;
        
        // Adjust for scroll
        int visible_index = index - layout->scroll_offset;
        
        // Check if visible
        if (visible_index < 0 || visible_index >= cfg->visible_items) {
            return false;
        }
        
        // Calculate position
        *x = cfg->margin_left;
        *y = cfg->margin_top + visible_index * (cfg->item_height + cfg->spacing);
        *width = layout->screen_width - cfg->margin_left - cfg->margin_right;
        *height = cfg->item_height;
        
        return true;
    }
    
    return false;
}

// Check if item at index is visible
bool ui_layout_is_item_visible(UILayout *layout, int index) {
    int x, y, w, h;
    return ui_layout_get_item_position(layout, index, &x, &y, &w, &h);
}

// Scroll up
bool ui_layout_scroll_up(UILayout *layout) {
    if (!layout->can_scroll_up) {
        return false;
    }
    
    layout->scroll_offset--;
    ui_layout_update(layout, layout->total_items);
    return true;
}

// Scroll down
bool ui_layout_scroll_down(UILayout *layout) {
    if (!layout->can_scroll_down) {
        return false;
    }
    
    layout->scroll_offset++;
    ui_layout_update(layout, layout->total_items);
    return true;
}

// Get item index at touch position
int ui_layout_get_item_at_position(UILayout *layout, int touch_x, int touch_y) {
    for (int i = 0; i < layout->total_items; i++) {
        int x, y, w, h;
        if (ui_layout_get_item_position(layout, i, &x, &y, &w, &h)) {
            if (touch_x >= x && touch_x < x + w &&
                touch_y >= y && touch_y < y + h) {
                return i;
            }
        }
    }
    return -1;
}

// Draw scroll indicators
void ui_layout_draw_scroll_indicators(Framebuffer *fb, UILayout *layout) {
    if (layout->can_scroll_up) {
        // Draw up arrow at top center
        int arrow_x = layout->screen_width / 2;
        int arrow_y = 10;
        int arrow_size = 20;
        
        // Simple triangle pointing up
        for (int i = 0; i < arrow_size; i++) {
            int width = (i * 2) + 1;
            int x_start = arrow_x - i;
            fb_draw_rect(fb, x_start, arrow_y + arrow_size - i, width, 1, COLOR_WHITE);
        }
    }
    
    if (layout->can_scroll_down) {
        // Draw down arrow at bottom center
        int arrow_x = layout->screen_width / 2;
        int arrow_y = layout->screen_height - 30;
        int arrow_size = 20;
        
        // Simple triangle pointing down
        for (int i = 0; i < arrow_size; i++) {
            int width = (i * 2) + 1;
            int x_start = arrow_x - i;
            fb_draw_rect(fb, x_start, arrow_y + i, width, 1, COLOR_WHITE);
        }
    }
}

// Helper: Calculate grid layout dimensions
void ui_layout_grid_calculate(GridLayoutConfig *config, int screen_width, int screen_height,
                              int total_items, int *out_rows, int *out_cols) {
    *out_cols = config->columns;
    *out_rows = (total_items + config->columns - 1) / config->columns;
}

// Helper: Calculate list layout dimensions
void ui_layout_list_calculate(ListLayoutConfig *config, int screen_height,
                              int total_items, int *out_visible) {
    int available_height = screen_height - config->margin_top - config->margin_bottom;
    *out_visible = available_height / (config->item_height + config->spacing);
    if (*out_visible > total_items) {
        *out_visible = total_items;
    }
}

// ============================================================================
// SCROLLABLE LIST WIDGET IMPLEMENTATION
// ============================================================================

// Initialize scrollable list
void scrollable_list_init(ScrollableList *list, int screen_width, int screen_height,
                         int item_height, int spacing,
                         int margin_left, int margin_top, int margin_right, int margin_bottom) {
    ui_layout_init_list(&list->layout, screen_width, screen_height,
                       item_height, spacing, margin_left, margin_top, margin_right, margin_bottom);
    
    list->items = NULL;
    list->item_count = 0;
    list->selected_index = -1;
    
    // Default colors
    list->bg_color = RGB(40, 40, 80);
    list->selected_color = RGB(0, 0, 255);
    list->text_color = RGB(255, 255, 255);
    list->border_color = RGB(255, 255, 255);
    list->text_scale = 3;
    
    list->custom_draw = NULL;
    list->user_data = NULL;
    
    // Calculate scroll indicator areas - these define the Y boundaries of the list content area
    // Scroll up indicator appears at top of list area
    // Scroll down indicator appears at bottom of list area
    list->scroll_up_y = margin_top;
    list->scroll_down_y = screen_height - margin_bottom;
}

// Set items
void scrollable_list_set_items(ScrollableList *list, char **items, int count) {
    list->items = items;
    list->item_count = count;
    ui_layout_update(&list->layout, count);
}

// Set colors
void scrollable_list_set_colors(ScrollableList *list, uint32_t bg, uint32_t selected,
                               uint32_t text, uint32_t border) {
    list->bg_color = bg;
    list->selected_color = selected;
    list->text_color = text;
    list->border_color = border;
}

// Set text scale
void scrollable_list_set_text_scale(ScrollableList *list, int scale) {
    list->text_scale = scale;
}

// Set custom draw callback
void scrollable_list_set_custom_draw(ScrollableList *list, ListItemDrawCallback callback, void *user_data) {
    list->custom_draw = callback;
    list->user_data = user_data;
}

// Default item drawing
static void default_draw_item(Framebuffer *fb, int x, int y, int width, int height,
                              const char *text, bool is_selected,
                              uint32_t bg_color, uint32_t selected_color,
                              uint32_t text_color, uint32_t border_color, int text_scale) {
    // Draw background
    uint32_t bg = is_selected ? selected_color : bg_color;
    fb_fill_rect(fb, x, y, width, height, bg);
    
    // Draw border
    fb_draw_rect(fb, x, y, width, height, border_color);
    fb_draw_rect(fb, x+1, y+1, width-2, height-2, border_color);
    
    // Convert text to uppercase
    char upper_text[256];
    int i = 0;
    while (text[i] && i < 255) {
        upper_text[i] = (text[i] >= 'a' && text[i] <= 'z') ? text[i] - 32 : text[i];
        i++;
    }
    upper_text[i] = '\0';
    
    // Draw text centered
    int text_len = strlen(upper_text);
    int text_width = text_len * 8 * text_scale;
    int text_x = x + (width - text_width) / 2;
    int text_y = y + (height - 8 * text_scale) / 2;
    
    fb_draw_text(fb, text_x, text_y, upper_text, text_color, text_scale);
}

// Draw the list
void scrollable_list_draw(Framebuffer *fb, ScrollableList *list) {
    // Calculate the actual list content area
    int list_top = list->layout.config.list.margin_top;
    int list_bottom = list->scroll_down_y;
    
    // Draw visible items
    for (int i = 0; i < list->item_count; i++) {
        int x, y, w, h;
        if (ui_layout_get_item_position(&list->layout, i, &x, &y, &w, &h)) {
            bool is_selected = (i == list->selected_index);
            
            if (list->custom_draw) {
                // Use custom drawing
                list->custom_draw(fb, x, y, w, h, list->items[i], is_selected, list->user_data);
            } else {
                // Use default drawing
                default_draw_item(fb, x, y, w, h, list->items[i], is_selected,
                                list->bg_color, list->selected_color,
                                list->text_color, list->border_color, list->text_scale);
            }
        }
    }
    
    // Draw scroll up indicator if needed (in the margin area ABOVE the list)
    if (list->layout.can_scroll_up) {
        int arrow_x = list->layout.screen_width / 2 - 20;
        int arrow_y = list_top - 50;  // 50px above list area
        fb_draw_text(fb, arrow_x, arrow_y, "^^^", RGB(0, 255, 255), 3);
        fb_draw_text(fb, arrow_x - 60, arrow_y + 30, "TAP ABOVE TO SCROLL UP", RGB(0, 255, 255), 1);
    }
    
    // Draw scroll down indicator if needed (in the margin area BELOW the list)
    if (list->layout.can_scroll_down) {
        int arrow_x = list->layout.screen_width / 2 - 20;
        int arrow_y = list_bottom + 5;  // Just below list area
        fb_draw_text(fb, arrow_x, arrow_y, "vvv", RGB(0, 255, 255), 3);
        fb_draw_text(fb, arrow_x - 70, arrow_y + 30, "TAP BELOW TO SCROLL DOWN", RGB(0, 255, 255), 1);
    }
}

// Handle touch input
int scrollable_list_handle_touch(ScrollableList *list, int touch_x, int touch_y) {
    // First check if touch is on an actual item
    int item = ui_layout_get_item_at_position(&list->layout, touch_x, touch_y);
    if (item >= 0) {
        list->selected_index = item;
        return item;
    }
    
    // Calculate list boundaries
    int list_top = list->layout.config.list.margin_top;
    int list_bottom = list->scroll_down_y;
    
    // Check if touch is in the scroll up area (above the list)
    if (list->layout.can_scroll_up && touch_y < list_top) {
        ui_layout_scroll_up(&list->layout);
        return -1;  // Scrolled
    }
    
    // Check if touch is in the scroll down area (below the list)
    if (list->layout.can_scroll_down && touch_y >= list_bottom) {
        ui_layout_scroll_down(&list->layout);
        return -1;  // Scrolled
    }
    
    return -2;  // Outside list
}

// Get selected item index
int scrollable_list_get_selected(ScrollableList *list) {
    return list->selected_index;
}

// Set selected item index
void scrollable_list_set_selected(ScrollableList *list, int index) {
    if (index >= -1 && index < list->item_count) {
        list->selected_index = index;
    }
}
