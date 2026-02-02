// include/rfui/widget.h
#ifndef RFUI_WIDGET_H
#define RFUI_WIDGET_H

#include "../../deps/rayforce/core/rayforce.h"

#define MAX_COLS 64
#define MAX_COL_RULES 8
#define MAX_OVERLAYS (MAX_COLS * MAX_COL_RULES)

typedef enum rfui_op_t {
    RFUI_OP_GT,      // >
    RFUI_OP_LT,      // <
    RFUI_OP_GE,      // >=
    RFUI_OP_LE,      // <=
    RFUI_OP_EQ,      // ==
    RFUI_OP_NE,      // !=
    RFUI_OP_IN,      // in
    RFUI_OP_WITHIN,  // within
} rfui_op_t;

typedef struct rfui_rule_t {
    i8_t op;            // rfui_op_t
    char value[256];    // operand ("800", "'AAPL", "'AAPL, 'MSFT")
    u32_t color;        // packed 0xRRGGBB, 0 = no color action
    obj_p fn;           // callback lambda or NULL (refcounted)
} rfui_rule_t;

typedef struct rfui_col_rules_t {
    rfui_rule_t rules[MAX_COL_RULES];
    int num_rules;
    i8_t sort_dir;      // 0=none, 1=asc, -1=desc
} rfui_col_rules_t;

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

    // Per-column rules (UI thread writes via MSG_SET_COL_RULES, Rayforce reads during eval)
    rfui_col_rules_t col_rules[MAX_COLS];

    // Double-buffered overlays: rayforce builds into back, UI reads from front.
    // Swapped on UI thread when processing RFUI_MSG_DRAW.
    rfui_color_overlay_t overlays[2][MAX_OVERLAYS];
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

// Get operator display string
const char* rfui_op_str(rfui_op_t op);

// Get operator Rayfall string (for expression generation)
const char* rfui_op_rayfall(rfui_op_t op);

#endif // RFUI_WIDGET_H
