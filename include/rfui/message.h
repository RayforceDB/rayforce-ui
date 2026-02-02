// include/rfui/message.h
#ifndef RFUI_MESSAGE_H
#define RFUI_MESSAGE_H

#include "../../deps/rayforce/core/rayforce.h"

// Forward declaration
struct rfui_widget_t;

// UI → Rayforce message types
typedef enum rfui_ui_msg_type_t {
    RFUI_MSG_EVAL,           // Evaluate expression
    RFUI_MSG_SET_POST_QUERY, // Set widget post_query
    RFUI_MSG_SET_COL_RULES,  // Set per-column rules
    RFUI_MSG_DROP,           // Drop obj_p after render
    RFUI_MSG_QUIT            // Shutdown
} rfui_ui_msg_type_t;

// Rayforce → UI message types
typedef enum rfui_ray_msg_type_t {
    RFUI_MSG_WIDGET_CREATED, // New widget panel
    RFUI_MSG_DRAW,           // Widget data update
    RFUI_MSG_RESULT,         // REPL result
    RFUI_MSG_ALERT           // Toast notification
} rfui_ray_msg_type_t;

// UI → Rayforce message
typedef struct rfui_ui_msg_t {
    rfui_ui_msg_type_t type;
    char* expr;                      // Expression string (owned, must free)
    obj_p obj;                       // Object to drop
    struct rfui_widget_t* widget;  // Target widget
    i64_t col_idx;                   // Target column index
    struct rfui_col_rules_t* col_rules;  // Column rules snapshot (owned, must free)
} rfui_ui_msg_t;

// Rayforce → UI message
typedef struct rfui_ray_msg_t {
    rfui_ray_msg_type_t type;
    struct rfui_widget_t* widget;  // Target widget
    obj_p data;                      // Data for rendering
} rfui_ray_msg_t;

#endif // RFUI_MESSAGE_H
