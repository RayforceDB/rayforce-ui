// src/grid_renderer.cpp
// Grid widget renderer using ImGui with virtualization for Rayforce tables

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "imgui.h"
#include "../include/rfui/icons.h"

// Make rayforce headers C++ compatible by redefining _Static_assert
#define _Static_assert static_assert

extern "C" {
#include "../include/rfui/grid_renderer.h"
#include "../include/rfui/widget.h"
#include "../include/rfui/context.h"
#include "../include/rfui/message.h"
#include "../include/rfui/queue.h"
#include "../deps/rayforce/core/poll.h"

// Global context declared in main.c
extern rfui_ctx_t* g_ctx;
}

static const char* op_names[] = { ">", "<", ">=", "<=", "==", "!=", "in", "within" };
static const int op_count = 8;

// UI state for grid selection (stored in widget->ui_state)
typedef struct grid_ui_state_t {
    int selected_row;       // -1 = no selection
    int editing_col;        // column index for rule builder popup, -1 = none
    bool adding_color_rule; // true = color rule, false = callback rule
    // Rule builder state
    int builder_op;         // rfui_op_t
    char builder_value[256];
    char builder_value2[64]; // second value for 'within'
    float builder_color[3]; // RGB
} grid_ui_state_t;

// Helper to send MSG_SET_POST_QUERY to Rayforce thread
static void send_post_query(rfui_widget_t* widget, const char* expr) {
    if (!g_ctx || !widget) return;

    // Allocate message
    rfui_ui_msg_t* msg = (rfui_ui_msg_t*)malloc(sizeof(rfui_ui_msg_t));
    if (!msg) return;

    // Duplicate expression string
    char* expr_copy = nullptr;
    if (expr) {
        size_t len = strlen(expr);
        expr_copy = (char*)malloc(len + 1);
        if (!expr_copy) {
            free(msg);
            return;
        }
        memcpy(expr_copy, expr, len + 1);
    }

    msg->type = RFUI_MSG_SET_POST_QUERY;
    msg->expr = expr_copy;
    msg->obj = nullptr;
    msg->widget = widget;

    // Push to queue
    if (!rfui_queue_push(g_ctx->ui_to_ray, msg)) {
        if (expr_copy) free(expr_copy);
        free(msg);
        return;
    }

    // Wake Rayforce thread
    poll_waker_p waker = rfui_ctx_get_waker(g_ctx);
    if (waker) {
        poll_waker_wake(waker);
    }
}

static void send_col_rules(rfui_widget_t* widget, i64_t col_idx) {
    if (!g_ctx || !widget || col_idx < 0 || col_idx >= MAX_COLS) return;

    rfui_ui_msg_t* msg = (rfui_ui_msg_t*)malloc(sizeof(rfui_ui_msg_t));
    if (!msg) return;

    rfui_col_rules_t* snapshot = (rfui_col_rules_t*)malloc(sizeof(rfui_col_rules_t));
    if (!snapshot) { free(msg); return; }
    memcpy(snapshot, &widget->col_rules[col_idx], sizeof(rfui_col_rules_t));
    // Clear fn pointers in snapshot — UI doesn't own them
    for (int i = 0; i < snapshot->num_rules; i++) {
        snapshot->rules[i].fn = nullptr;
    }

    msg->type = RFUI_MSG_SET_COL_RULES;
    msg->expr = nullptr;
    msg->obj = nullptr;
    msg->widget = widget;
    msg->col_idx = col_idx;
    msg->col_rules = snapshot;

    if (!rfui_queue_push(g_ctx->ui_to_ray, msg)) {
        free(snapshot);
        free(msg);
        return;
    }

    poll_waker_p waker = rfui_ctx_get_waker(g_ctx);
    if (waker) poll_waker_wake(waker);
}

// Build a filter expression for the selected row
// Expression: {[x] (take 1 (drop ROW_INDEX x))}
// This creates a lambda that takes data and returns just the selected row
static char* build_row_filter_expr(int row_index) {
    // Max length: "{[x] (take 1 (drop 999999999 x))}" = ~40 chars
    char* buf = (char*)malloc(64);
    if (!buf) return nullptr;
    snprintf(buf, 64, "{[x] (take 1 (drop %d x))}", row_index);
    return buf;
}

// Helper function to render a single cell based on column type
static void render_cell(obj_p col, i64_t row) {
    if (col == nullptr || row < 0 || row >= col->len) {
        ImGui::Text("?");
        return;
    }

    switch (col->type) {
        case TYPE_I64:
            ImGui::Text("%lld", (long long)AS_I64(col)[row]);
            break;
        case TYPE_I32:
            ImGui::Text("%d", AS_I32(col)[row]);
            break;
        case TYPE_I16:
            ImGui::Text("%d", (int)AS_I16(col)[row]);
            break;
        case TYPE_F64: {
            f64_t val = AS_F64(col)[row];
            // Check for NaN (null value in Rayforce)
            if (val != val) {
                ImGui::TextDisabled("null");
            } else {
                ImGui::Text("%.6g", val);
            }
            break;
        }
        case TYPE_SYMBOL: {
            i64_t sid = AS_SYMBOL(col)[row];
            const char* str = str_from_symbol(sid);
            if (str) {
                ImGui::Text("%s", str);
            } else {
                ImGui::TextDisabled("null");
            }
            break;
        }
        case TYPE_B8:
            ImGui::Text("%s", AS_B8(col)[row] ? "true" : "false");
            break;
        case TYPE_U8:
            ImGui::Text("%u", (unsigned)AS_U8(col)[row]);
            break;
        case TYPE_C8: {
            // Single character or string
            char c = AS_C8(col)[row];
            if (c >= 32 && c < 127) {
                ImGui::Text("%c", c);
            } else {
                ImGui::Text("0x%02x", (unsigned char)c);
            }
            break;
        }
        case TYPE_DATE: {
            // Date stored as i32 (days since epoch)
            i32_t d = AS_DATE(col)[row];
            if (d == NULL_I32) {
                ImGui::TextDisabled("null");
            } else {
                // Simple date format: YYYY.MM.DD
                // Days since 2000-01-01
                ImGui::Text("%d", d);
            }
            break;
        }
        case TYPE_TIME: {
            // Time stored as i32 (milliseconds since midnight)
            i32_t t = AS_TIME(col)[row];
            if (t == NULL_I32) {
                ImGui::TextDisabled("null");
            } else {
                int ms = t % 1000;
                int sec = (t / 1000) % 60;
                int min = (t / 60000) % 60;
                int hr = t / 3600000;
                ImGui::Text("%02d:%02d:%02d.%03d", hr, min, sec, ms);
            }
            break;
        }
        case TYPE_TIMESTAMP: {
            // Timestamp stored as i64 (nanoseconds since epoch)
            i64_t ts = AS_TIMESTAMP(col)[row];
            if (ts == NULL_I64) {
                ImGui::TextDisabled("null");
            } else {
                // Display as raw value for now
                ImGui::Text("%lld", (long long)ts);
            }
            break;
        }
        case TYPE_GUID: {
            // GUID is 16 bytes
            guid_t* g = &AS_GUID(col)[row];
            ImGui::Text("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                (*g)[0], (*g)[1], (*g)[2], (*g)[3],
                (*g)[4], (*g)[5], (*g)[6], (*g)[7],
                (*g)[8], (*g)[9], (*g)[10], (*g)[11],
                (*g)[12], (*g)[13], (*g)[14], (*g)[15]);
            break;
        }
        case TYPE_LIST: {
            // Nested list - show type indicator
            obj_p item = AS_LIST(col)[row];
            if (item) {
                ImGui::TextDisabled("[%s:%lld]", type_name(item->type), (long long)item->len);
            } else {
                ImGui::TextDisabled("null");
            }
            break;
        }
        default:
            ImGui::TextDisabled("<%s>", type_name(col->type));
            break;
    }
}

extern "C" {

// Note: render_data lifetime is managed by the widget registry and must remain
// valid during render. The caller is responsible for ensuring render_data
// points to valid memory throughout the widget's lifecycle.
nil_t rfui_render_grid(rfui_widget_t* widget) {
    if (widget == nullptr) {
        return;
    }
    obj_p table = widget->render_data;

    // Check if we have valid table data
    if (table == nullptr || table->type != TYPE_TABLE) {
        ImGui::TextDisabled("No table data");
        return;
    }

    // Table structure: list of [keys, vals]
    // keys: symbol vector of column names
    // vals: list of column vectors
    if (table->len < 2) {
        ImGui::TextDisabled("Invalid table structure");
        return;
    }

    obj_p keys = AS_LIST(table)[0];  // Symbol vector of column names
    obj_p vals = AS_LIST(table)[1];  // List of column vectors

    if (keys == nullptr || vals == nullptr) {
        ImGui::TextDisabled("Table has null keys or values");
        return;
    }

    // Validate types
    if (keys->type != TYPE_SYMBOL) {
        ImGui::TextDisabled("Table keys must be symbols (got %s)", type_name(keys->type));
        return;
    }

    if (vals->type != TYPE_LIST) {
        ImGui::TextDisabled("Table values must be a list (got %s)", type_name(vals->type));
        return;
    }

    i64_t ncols = keys->len;
    if (ncols == 0) {
        ImGui::TextDisabled("Table has no columns");
        return;
    }

    // Check column count matches
    if (vals->len != ncols) {
        ImGui::TextDisabled("Column count mismatch: %lld keys vs %lld values",
            (long long)ncols, (long long)vals->len);
        return;
    }

    // Get row count from first column
    obj_p first_col = AS_LIST(vals)[0];
    if (first_col == nullptr) {
        ImGui::TextDisabled("First column is null");
        return;
    }
    i64_t nrows = first_col->len;
    if (nrows == 0) {
        ImGui::TextDisabled("Empty table (0 rows)");
        return;
    }

    // Validate all columns have the same length
    for (i64_t col_idx = 1; col_idx < ncols; col_idx++) {
        obj_p col = AS_LIST(vals)[col_idx];
        if (col == nullptr) {
            ImGui::TextDisabled("Column %lld is null", (long long)col_idx);
            return;
        }
        if (col->len != nrows) {
            ImGui::TextDisabled("Column %lld length mismatch: %lld vs %lld",
                (long long)col_idx, (long long)col->len, (long long)nrows);
            return;
        }
    }

    // Initialize UI state if needed
    grid_ui_state_t* ui_state = (grid_ui_state_t*)widget->ui_state;
    if (!ui_state) {
        ui_state = (grid_ui_state_t*)calloc(1, sizeof(grid_ui_state_t));
        if (ui_state) {
            ui_state->selected_row = -1;
            ui_state->editing_col = -1;
            widget->ui_state = ui_state;
        }
    }

    // Snapshot overlay front buffer for this frame
    int ov_front = widget->overlay_front;
    int ov_count = widget->num_overlays[ov_front];

    // Display table info with secondary text color
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.545f, 0.580f, 0.620f, 1.0f));
    if (ui_state && ui_state->selected_row >= 0) {
        ImGui::Text("Rows: %lld  Columns: %lld  Selected: %d",
                    (long long)nrows, (long long)ncols, ui_state->selected_row);
        ImGui::SameLine();
        ImGui::PopStyleColor();
        if (ImGui::SmallButton(ICON_ERASER " Clear")) {
            ui_state->selected_row = -1;
            send_post_query(widget, nullptr);
        }
    } else {
        ImGui::Text("Rows: %lld  Columns: %lld", (long long)nrows, (long long)ncols);
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Create ImGui table with virtualization
    ImGuiTableFlags table_flags =
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable |
        ImGuiTableFlags_Sortable |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;  // Required for specifying column widths

    // Use available content region for table
    ImVec2 outer_size = ImVec2(0.0f, 0.0f);

    if (ImGui::BeginTable("##grid", (int)ncols, table_flags, outer_size)) {
        // Setup columns with headers
        for (i64_t col_idx = 0; col_idx < ncols; col_idx++) {
            i64_t sym_id = AS_SYMBOL(keys)[col_idx];
            const char* col_name = str_from_symbol(sym_id);
            if (!col_name) col_name = "<invalid>";

            // Get column type for sizing hints
            obj_p col = AS_LIST(vals)[col_idx];
            ImGuiTableColumnFlags col_flags = ImGuiTableColumnFlags_None;

            // Set initial column width based on type
            float init_width = 0.0f;  // 0 = auto
            if (col) {
                switch (col->type) {
                    case TYPE_B8:
                        init_width = 50.0f;
                        break;
                    case TYPE_I16:
                    case TYPE_I32:
                        init_width = 80.0f;
                        break;
                    case TYPE_I64:
                    case TYPE_TIMESTAMP:
                        init_width = 120.0f;
                        break;
                    case TYPE_F64:
                        init_width = 100.0f;
                        break;
                    case TYPE_DATE:
                        init_width = 90.0f;
                        break;
                    case TYPE_TIME:
                        init_width = 100.0f;
                        break;
                    case TYPE_SYMBOL:
                    case TYPE_C8:
                        init_width = 120.0f;
                        break;
                    case TYPE_GUID:
                        init_width = 280.0f;
                        break;
                    default:
                        init_width = 100.0f;
                        break;
                }
            }

            ImGui::TableSetupColumn(col_name ? col_name : "?", col_flags, init_width);
        }

        // Freeze header row
        ImGui::TableSetupScrollFreeze(0, 1);

        // Standard header row — right-click opens per-column context menu
        ImGui::TableHeadersRow();

        // Per-column context menus (right-click on header)
        for (i64_t ci = 0; ci < ncols && ci < MAX_COLS; ci++) {
            ImGui::PushID((int)ci);
            const char* col_name = str_from_symbol(AS_SYMBOL(keys)[ci]);
            if (!col_name) col_name = "?";

            char popup_id[32];
            snprintf(popup_id, sizeof(popup_id), "col_menu_%d", (int)ci);

            // Open context menu on right-click of column header
            if (ImGui::TableSetColumnIndex((int)ci)) {
                // Check if this column header was right-clicked
            }
            // Use TableGetColumnFlags to detect header hover + right-click
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup(popup_id);
            }

            if (ImGui::BeginPopup(popup_id)) {
                ImGui::Text("%s", col_name);
                ImGui::Separator();

                // Sort ascending
                if (ImGui::Selectable(ICON_SORT_UP " Sort Ascending")) {
                    for (i64_t j = 0; j < ncols && j < MAX_COLS; j++)
                        widget->col_rules[j].sort_dir = 0;
                    widget->col_rules[ci].sort_dir = 1;
                    char sort_expr[256];
                    snprintf(sort_expr, sizeof(sort_expr), "(fn [_d] (xasc ['%s] _d))", col_name);
                    send_post_query(widget, sort_expr);
                }

                // Sort descending
                if (ImGui::Selectable(ICON_SORT_DOWN " Sort Descending")) {
                    for (i64_t j = 0; j < ncols && j < MAX_COLS; j++)
                        widget->col_rules[j].sort_dir = 0;
                    widget->col_rules[ci].sort_dir = -1;
                    char sort_expr[256];
                    snprintf(sort_expr, sizeof(sort_expr), "(fn [_d] (xdesc ['%s] _d))", col_name);
                    send_post_query(widget, sort_expr);
                }

                // Clear sort
                if (widget->col_rules[ci].sort_dir != 0) {
                    if (ImGui::Selectable(ICON_XMARK " Clear Sort")) {
                        widget->col_rules[ci].sort_dir = 0;
                        send_post_query(widget, nullptr);
                    }
                }

                ImGui::Separator();

                // Add color rule — sets state and closes popup; builder opens next frame
                if (widget->col_rules[ci].num_rules < MAX_COL_RULES) {
                    if (ImGui::Selectable(ICON_PLUS " Add Color Rule...")) {
                        if (ui_state) {
                            ui_state->editing_col = (int)ci;
                            ui_state->adding_color_rule = true;
                            ui_state->builder_op = RFUI_OP_GT;
                            ui_state->builder_value[0] = '\0';
                            ui_state->builder_value2[0] = '\0';
                            ui_state->builder_color[0] = 1.0f;
                            ui_state->builder_color[1] = 0.32f;
                            ui_state->builder_color[2] = 0.29f;
                        }
                        // Selectable closes the popup; builder window opens below
                    }
                }

                ImGui::Separator();

                // Active rules for this column
                if (widget->col_rules[ci].num_rules > 0) {
                    ImGui::TextDisabled("Active Rules:");
                    rfui_col_rules_t* cr = &widget->col_rules[ci];
                    for (int ri = 0; ri < cr->num_rules; ri++) {
                        ImGui::PushID(ri);
                        rfui_rule_t* r = &cr->rules[ri];
                        const char* op_s = (r->op >= 0 && r->op < op_count) ? op_names[(int)r->op] : "?";
                        ImGui::Text("  %s %s %s", col_name, op_s, r->value);
                        if (r->color) {
                            ImGui::SameLine();
                            float rc = ((r->color >> 16) & 0xFF) / 255.0f;
                            float gc = ((r->color >> 8) & 0xFF) / 255.0f;
                            float bc = (r->color & 0xFF) / 255.0f;
                            ImGui::ColorButton("##clr", ImVec4(rc, gc, bc, 1.0f),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(12, 12));
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton(ICON_XMARK)) {
                            for (int j = ri; j < cr->num_rules - 1; j++)
                                cr->rules[j] = cr->rules[j + 1];
                            cr->num_rules--;
                            send_col_rules(widget, ci);
                            ri--;
                        }
                        ImGui::PopID();
                    }
                }

                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        // Rule builder window — rendered outside any popup context
        if (ui_state && ui_state->editing_col >= 0 && ui_state->editing_col < (int)ncols
            && ui_state->editing_col < MAX_COLS) {
            i64_t ci = ui_state->editing_col;
            const char* col_name = str_from_symbol(AS_SYMBOL(keys)[ci]);
            if (!col_name) col_name = "?";

            char win_title[64];
            snprintf(win_title, sizeof(win_title), "Add Rule - %s###RuleBuilder", col_name);

            bool open = true;
            ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(win_title, &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
                // Operator combo
                ImGui::SetNextItemWidth(80);
                if (ImGui::BeginCombo("Op", op_names[ui_state->builder_op])) {
                    for (int oi = 0; oi < op_count; oi++) {
                        if (ImGui::Selectable(op_names[oi], ui_state->builder_op == oi))
                            ui_state->builder_op = oi;
                    }
                    ImGui::EndCombo();
                }

                // Value input(s)
                if (ui_state->builder_op == RFUI_OP_WITHIN) {
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputText("From", ui_state->builder_value, sizeof(ui_state->builder_value));
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputText("To", ui_state->builder_value2, sizeof(ui_state->builder_value2));
                } else if (ui_state->builder_op == RFUI_OP_IN) {
                    ImGui::SetNextItemWidth(200);
                    ImGui::InputText("Values", ui_state->builder_value, sizeof(ui_state->builder_value));
                    ImGui::TextDisabled("Space-separated: 'AAPL 'MSFT 'GOOG");
                } else {
                    ImGui::SetNextItemWidth(140);
                    ImGui::InputText("Value", ui_state->builder_value, sizeof(ui_state->builder_value));
                }

                // Color picker
                ImGui::ColorEdit3("Color", ui_state->builder_color,
                    ImGuiColorEditFlags_NoInputs);

                // Add / Cancel
                if (ImGui::Button("Add")) {
                    rfui_col_rules_t* cr = &widget->col_rules[ci];
                    if (cr->num_rules < MAX_COL_RULES) {
                        rfui_rule_t* nr = &cr->rules[cr->num_rules++];
                        nr->op = (i8_t)ui_state->builder_op;
                        if (ui_state->builder_op == RFUI_OP_WITHIN) {
                            snprintf(nr->value, sizeof(nr->value), "%s %s",
                                ui_state->builder_value, ui_state->builder_value2);
                        } else {
                            strncpy(nr->value, ui_state->builder_value, sizeof(nr->value) - 1);
                            nr->value[sizeof(nr->value) - 1] = '\0';
                        }
                        nr->color = ((u32_t)(ui_state->builder_color[0] * 255) << 16) |
                                    ((u32_t)(ui_state->builder_color[1] * 255) << 8) |
                                    (u32_t)(ui_state->builder_color[2] * 255);
                        nr->fn = nullptr;
                        send_col_rules(widget, ci);
                    }
                    ui_state->editing_col = -1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ui_state->editing_col = -1;
                }
            }
            ImGui::End();
            if (!open) ui_state->editing_col = -1;
        }

        // Use ListClipper for virtualized row rendering
        ImGuiListClipper clipper;
        clipper.Begin((int)nrows);

        // Cache column pointers for performance (avoid repeated AS_LIST dereference)
        obj_p* cols = AS_LIST(vals);

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                ImGui::TableNextRow();

                // Check if this row is selected
                bool is_selected = (ui_state && ui_state->selected_row == row);

                // Render each cell in the row
                for (i64_t col_idx = 0; col_idx < ncols; col_idx++) {
                    ImGui::TableSetColumnIndex((int)col_idx);

                    obj_p col = cols[col_idx];
                    if (col == nullptr) {
                        ImGui::TextDisabled("null");
                        continue;
                    }

                    // Verify row is within column bounds
                    if (row >= col->len) {
                        ImGui::TextDisabled("OOB");
                        continue;
                    }

                    // For the first column, add a selectable that spans all columns
                    if (col_idx == 0) {
                        // Create unique ID for this row's selectable
                        char selectable_id[32];
                        snprintf(selectable_id, sizeof(selectable_id), "##row%d", row);

                        // Selectable spans all columns and is drawn behind cell content
                        if (ImGui::Selectable(selectable_id, is_selected,
                                              ImGuiSelectableFlags_SpanAllColumns |
                                              ImGuiSelectableFlags_AllowOverlap)) {
                            // Row was clicked
                            if (ui_state) {
                                if (ui_state->selected_row == row) {
                                    // Clicking same row deselects
                                    ui_state->selected_row = -1;
                                    send_post_query(widget, nullptr);
                                } else {
                                    // Select new row
                                    ui_state->selected_row = row;
                                    char* expr = build_row_filter_expr(row);
                                    if (expr) {
                                        send_post_query(widget, expr);
                                        free(expr);
                                    }
                                }
                            }
                        }
                        ImGui::SameLine();
                    }

                    // Check overlay rules for this cell
                    bool cell_colored = false;
                    for (int oi = 0; oi < ov_count; oi++) {
                        rfui_color_overlay_t* ov = &widget->overlays[ov_front][oi];
                        if (ov->col_idx != -1 && ov->col_idx != col_idx) continue;
                        if (ov->mask && row < ov->mask->len) {
                            if (AS_B8(ov->mask)[row]) {
                                u32_t c = ov->color;
                                float r = ((c >> 16) & 0xFF) / 255.0f;
                                float g = ((c >> 8) & 0xFF) / 255.0f;
                                float b = (c & 0xFF) / 255.0f;
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(r, g, b, 1.0f));
                                cell_colored = true;
                                break;
                            }
                        }
                    }

                    // Render cell with zero-copy direct buffer access
                    render_cell(col, (i64_t)row);

                    if (cell_colored)
                        ImGui::PopStyleColor();
                }
            }
        }

        clipper.End();
        ImGui::EndTable();
    }
}

} // extern "C"
