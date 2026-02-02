// include/rfui/widget.h
#ifndef RFUI_WIDGET_H
#define RFUI_WIDGET_H

#include "../../deps/rayforce/core/rayforce.h"

#define MAX_RULES 16

typedef struct rfui_rule_t {
    char expr[256];      // Rayfall expression: "(> Price 31.50)"
    obj_p fn;            // callback lambda or NULL (refcounted)
    u32_t color;         // packed RGBA, 0 = no color action
} rfui_rule_t;

typedef struct rfui_color_overlay_t {
    i64_t col_idx;       // column index in table (-1 = whole row)
    obj_p mask;          // boolean vector (refcounted)
    u32_t color;         // packed RGBA
} rfui_color_overlay_t;

typedef enum rfui_widget_type_t {
    RFUI_WIDGET_GRID,
    RFUI_WIDGET_CHART,
    RFUI_WIDGET_TEXT,
    RFUI_WIDGET_ALERT
} rfui_widget_type_t;

typedef struct rfui_widget_t {
    rfui_widget_type_t type;
    char* name;
    obj_p data;           // Base data from draw()
    obj_p post_query;     // Expression applied before render
    obj_p on_select;      // Callback function

    // UI state (UI thread only)
    b8_t is_open;
    u32_t dock_id;
    raw_p ui_state;       // Type-specific UI state
    obj_p render_data;    // Current data for rendering

    // Rules (Rayforce thread writes, UI thread reads overlays after draw msg)
    rfui_rule_t rules[MAX_RULES];
    int num_rules;

    // Double-buffered overlays: rayforce builds into back, UI reads from front.
    // Swapped on UI thread when processing RFUI_MSG_DRAW.
    rfui_color_overlay_t overlays[2][MAX_RULES];
    int num_overlays[2];
    int overlay_front;  // 0 or 1 — index UI reads from
} rfui_widget_t;

// Create widget struct (called from Rayforce thread)
rfui_widget_t* rfui_widget_create(rfui_widget_type_t type, const char* name);

// Destroy widget struct
nil_t rfui_widget_destroy(rfui_widget_t* w);

// Format widget for display
char* rfui_widget_format(rfui_widget_t* w);

// Get type name
const char* rfui_widget_type_name(rfui_widget_type_t type);

#endif // RFUI_WIDGET_H
