// src/widget_registry.cpp
// Widget registry implementation for UI-side widget tracking

#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <cfloat>  // FLT_MAX

#include "imgui.h"
#include "../include/rfui/icons.h"

// Make rayforce headers C++ compatible by redefining _Static_assert
#define _Static_assert static_assert

extern "C" {
#include "../include/rfui/widget_registry.h"
#include "../include/rfui/widget.h"
#include "../include/rfui/grid_renderer.h"
#include "../include/rfui/chart_renderer.h"
#include "../include/rfui/text_renderer.h"
}


// Global widget storage
static std::vector<rfui_widget_t*> g_widgets;

extern "C" {

nil_t rfui_registry_init(nil_t) {
    // Nothing to do for std::vector - it's already initialized
    g_widgets.clear();
}

nil_t rfui_registry_destroy(nil_t) {
    // Free all widgets
    // NOTE: Thread safety consideration - registry_destroy is called AFTER UI loop
    // exits but BEFORE Rayforce thread is joined. This means drop_obj calls in
    // widget_destroy happen from the UI thread, not the Rayforce thread.
    // TODO: This is known technical debt. For proper thread safety, widgets with
    // render_data should queue drops to Rayforce thread before destruction.
    // Current approach is acceptable for shutdown but not ideal.
    for (rfui_widget_t* widget : g_widgets) {
        if (widget != nullptr) {
            // Free type-specific ui_state
            // Note: Grid ui_state uses C++ (unordered_map) so would need delete,
            // but at shutdown the process is exiting anyway. Just null the ptr.
            if (widget->ui_state && widget->type != RFUI_WIDGET_GRID) {
                free(widget->ui_state);
            }
            widget->ui_state = nullptr;

            // Null out Rayforce obj_p fields before destroy — the Rayforce
            // thread (and its heap) is already joined/gone at this point,
            // so drop_obj would segfault on freed heap memory.
            widget->data = nullptr;
            widget->post_query = nullptr;
            widget->on_select = nullptr;
            widget->render_data = nullptr;
            for (int c = 0; c < MAX_COLS; c++) {
                for (int i = 0; i < widget->col_rules[c].num_rules; i++) {
                    widget->col_rules[c].rules[i].fn = nullptr;
                }
            }
            for (int b = 0; b < 2; b++) {
                for (int i = 0; i < widget->num_overlays[b]; i++) {
                    widget->overlays[b][i].mask = nullptr;
                }
            }
        }
        rfui_widget_destroy(widget);
    }
    g_widgets.clear();
}

nil_t rfui_registry_add(rfui_widget_t* widget) {
    if (widget == nullptr) {
        return;
    }
    g_widgets.push_back(widget);
}

// Render widget (shared logic for both render paths)
static void render_widget(rfui_widget_t* widget) {
    if (widget == nullptr || !widget->is_open) {
        return;
    }

        // Set minimum size constraints for usability
        ImGui::SetNextWindowSizeConstraints(ImVec2(400, 300), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

        // Build icon-prefixed window label
        char window_label[256];
        const char* icon = "";
        switch (widget->type) {
            case RFUI_WIDGET_GRID:  icon = ICON_TABLE " ";      break;
            case RFUI_WIDGET_CHART: icon = ICON_CHART_LINE " "; break;
            case RFUI_WIDGET_TEXT:  icon = ICON_FILE_LINES " "; break;
            default: break;
        }
        snprintf(window_label, sizeof(window_label), "%s%s", icon, widget->name);

        // Begin widget window with close button
        ImGui::Begin(window_label, (bool*)&widget->is_open);

        // Render based on widget type
        switch (widget->type) {
            case RFUI_WIDGET_GRID:
                rfui_render_grid(widget);
                break;
            case RFUI_WIDGET_CHART:
                rfui_render_chart(widget);
                break;
            case RFUI_WIDGET_TEXT:
                rfui_render_text(widget);
                break;
            default:
                ImGui::TextDisabled("Unknown widget type: %d", widget->type);
                break;
        }

    ImGui::End();
}

nil_t rfui_registry_render(nil_t) {
    for (rfui_widget_t* widget : g_widgets) {
        render_widget(widget);
    }
}

obj_p rfui_registry_update_data(rfui_widget_t* widget, obj_p new_data) {
    if (widget == nullptr) {
        return nullptr;
    }

    // Check for same data - avoid use-after-free if caller passes same object
    if (new_data == widget->render_data) {
        return nullptr;  // Same data, no swap needed
    }

    // Store old data
    obj_p old_data = widget->render_data;

    // Set new data
    widget->render_data = new_data;

    // Return old data for caller to queue for drop
    return old_data;
}

rfui_widget_t* rfui_registry_find_by_type(rfui_widget_type_t type) {
    for (rfui_widget_t* widget : g_widgets) {
        if (widget != nullptr && widget->type == type) {
            return widget;
        }
    }
    return nullptr;
}

} // extern "C"
