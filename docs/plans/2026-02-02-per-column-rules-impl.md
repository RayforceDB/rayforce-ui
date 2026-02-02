# Per-Column Rules Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the flat widget-level rules system with per-column rule management via dropdown menus in grid column headers.

**Architecture:** Rules move from a flat `rules[MAX_RULES]` array to per-column `col_rules[MAX_COLS]` structs. Each rule stores structured data (op enum + value string + color) instead of raw Rayfall expressions. The UI builds rules via column header dropdowns; the Rayforce thread generates and evaluates Rayfall expressions at draw time.

**Tech Stack:** C (Rayforce thread, structs), C++ (ImGui grid renderer), Dear ImGui popups/combos/inputs.

**Design doc:** `docs/plans/2026-02-02-per-column-rules-design.md`

---

### Task 1: Update Data Model — widget.h structs

**Files:**
- Modify: `include/rfui/widget.h`

**Step 1: Replace rule structs in widget.h**

Replace the entire contents of `include/rfui/widget.h` with:

```c
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
```

**Step 2: Build to verify struct compiles**

Run: `make 2>&1 | head -30`
Expected: Compilation errors in files still referencing old `rules[]` / `num_rules` fields — that's expected and fixed in later tasks.

**Step 3: Commit**

```bash
git add include/rfui/widget.h
git commit -m "refactor(widget): replace flat rules array with per-column col_rules"
```

---

### Task 2: Update widget.c — lifecycle and helpers

**Files:**
- Modify: `src/widget.c`

**Step 1: Update widget.c**

Replace the entire contents of `src/widget.c` with:

```c
// src/widget.c
#include "../include/rfui/widget.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// strdup is POSIX, not C standard - provide explicit declaration
extern char* strdup(const char* s);

// Buffer size for widget formatting
#define WIDGET_FORMAT_BUF_SIZE 256

const char* rfui_widget_type_name(rfui_widget_type_t type) {
    switch (type) {
        case RFUI_WIDGET_GRID:  return "grid";
        case RFUI_WIDGET_CHART: return "chart";
        case RFUI_WIDGET_TEXT:  return "text";
        case RFUI_WIDGET_ALERT: return "alert";
        default: return "unknown";
    }
}

const char* rfui_op_str(rfui_op_t op) {
    switch (op) {
        case RFUI_OP_GT:     return ">";
        case RFUI_OP_LT:     return "<";
        case RFUI_OP_GE:     return ">=";
        case RFUI_OP_LE:     return "<=";
        case RFUI_OP_EQ:     return "==";
        case RFUI_OP_NE:     return "!=";
        case RFUI_OP_IN:     return "in";
        case RFUI_OP_WITHIN: return "within";
        default: return "?";
    }
}

const char* rfui_op_rayfall(rfui_op_t op) {
    switch (op) {
        case RFUI_OP_GT:     return ">";
        case RFUI_OP_LT:     return "<";
        case RFUI_OP_GE:     return ">=";
        case RFUI_OP_LE:     return "<=";
        case RFUI_OP_EQ:     return "==";
        case RFUI_OP_NE:     return "!=";
        case RFUI_OP_IN:     return "in";
        case RFUI_OP_WITHIN: return "within";
        default: return "?";
    }
}

rfui_widget_t* rfui_widget_create(rfui_widget_type_t type, const char* name) {
    if (!name) return NULL;

    rfui_widget_t* w = malloc(sizeof(rfui_widget_t));
    if (!w) return NULL;

    w->name = strdup(name);
    if (!w->name) {
        free(w);
        return NULL;
    }

    w->type = type;
    w->data = NULL;
    w->post_query = NULL;
    w->on_select = NULL;
    w->is_open = B8_TRUE;
    w->dock_id = 0;
    w->ui_state = NULL;
    w->render_data = NULL;
    memset(w->col_rules, 0, sizeof(w->col_rules));
    w->num_overlays[0] = 0;
    w->num_overlays[1] = 0;
    w->overlay_front = 0;

    return w;
}

nil_t rfui_widget_destroy(rfui_widget_t* w) {
    if (!w) return;

    free(w->name);
    if (w->data) drop_obj(w->data);
    if (w->post_query) drop_obj(w->post_query);
    if (w->on_select) drop_obj(w->on_select);
    if (w->render_data) drop_obj(w->render_data);
    for (int c = 0; c < MAX_COLS; c++) {
        for (int i = 0; i < w->col_rules[c].num_rules; i++) {
            if (w->col_rules[c].rules[i].fn) drop_obj(w->col_rules[c].rules[i].fn);
        }
    }
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < w->num_overlays[b]; i++) {
            if (w->overlays[b][i].mask) drop_obj(w->overlays[b][i].mask);
        }
    }
    free(w->ui_state);
    free(w);
}

char* rfui_widget_format(rfui_widget_t* w) {
    if (!w) return NULL;

    char* buf = malloc(WIDGET_FORMAT_BUF_SIZE);
    if (!buf) return NULL;

    snprintf(buf, WIDGET_FORMAT_BUF_SIZE, "widget<%s:\"%s\">",
             rfui_widget_type_name(w->type), w->name);
    return buf;
}
```

**Step 2: Build to check**

Run: `make 2>&1 | head -30`

**Step 3: Commit**

```bash
git add src/widget.c
git commit -m "refactor(widget): update lifecycle for per-column rules, add op helpers"
```

---

### Task 3: Add message type and icons

**Files:**
- Modify: `include/rfui/message.h`
- Modify: `include/rfui/icons.h`

**Step 1: Add MSG_SET_COL_RULES to message.h**

In `include/rfui/message.h`, add `RFUI_MSG_SET_COL_RULES` to the UI msg enum (after `RFUI_MSG_SET_POST_QUERY`, line 13):

```c
    RFUI_MSG_SET_COL_RULES,  // Set per-column rules
```

Add a `col_idx` field to `rfui_ui_msg_t` (after the `widget` field, line 31):

```c
    i64_t col_idx;                   // Target column index
    struct rfui_col_rules_t* col_rules;  // Column rules snapshot (owned, must free)
```

**Step 2: Add sort/chevron icons to icons.h**

Add after the existing `ICON_FILTER` line (line 30) in `include/rfui/icons.h`:

```c
#define ICON_SORT_UP     "\xef\x83\x9e"  // f0de - fa-sort-up (caret-up)
#define ICON_SORT_DOWN   "\xef\x83\x9d"  // f0dd - fa-sort-down (caret-down)
#define ICON_CHEVRON_DN  "\xef\x81\xb8"  // f078 - fa-chevron-down
```

**Step 3: Build**

Run: `make 2>&1 | head -30`

**Step 4: Commit**

```bash
git add include/rfui/message.h include/rfui/icons.h
git commit -m "feat(msg): add MSG_SET_COL_RULES and sort/chevron icons"
```

---

### Task 4: Handle MSG_SET_COL_RULES in rayforce_thread.c

**Files:**
- Modify: `src/rayforce_thread.c`

**Step 1: Add MSG_SET_COL_RULES case to process_ui_message**

In `process_ui_message()` (line 29), add a new case before the `default:` case (line 107):

```c
        case RFUI_MSG_SET_COL_RULES:
            if (msg->widget && msg->col_rules && msg->col_idx >= 0 && msg->col_idx < MAX_COLS) {
                // Drop old fn refcounts for this column
                rfui_col_rules_t* old = &msg->widget->col_rules[msg->col_idx];
                for (int i = 0; i < old->num_rules; i++) {
                    if (old->rules[i].fn) drop_obj(old->rules[i].fn);
                }
                // Copy new rules
                memcpy(old, msg->col_rules, sizeof(rfui_col_rules_t));
            }
            if (msg->col_rules) free(msg->col_rules);
            break;
```

**Step 2: Remove fn_rule function**

Delete the entire `fn_rule` function (lines 395-473) and its registration at line 610:

```c
    RFUI_REGISTER_FN(functions, "rule", TYPE_VARY, FN_NONE, fn_rule);
```

Remove the line above.

**Step 3: Rework evaluate_rules()**

Replace the entire `evaluate_rules()` function (lines 475-582) with:

```c
// Map op enum to Rayfall operator string
static const char* op_to_str(i8_t op) {
    switch (op) {
        case RFUI_OP_GT:     return ">";
        case RFUI_OP_LT:     return "<";
        case RFUI_OP_GE:     return ">=";
        case RFUI_OP_LE:     return "<=";
        case RFUI_OP_EQ:     return "==";
        case RFUI_OP_NE:     return "!=";
        case RFUI_OP_IN:     return "in";
        case RFUI_OP_WITHIN: return "within";
        default: return NULL;
    }
}

static void evaluate_rules(rfui_widget_t* w, obj_p final_data) {
    if (!w) return;

    // Write to back buffer (opposite of what UI reads)
    int back = 1 - w->overlay_front;

    // Drop previous back-buffer masks
    for (int i = 0; i < w->num_overlays[back]; i++) {
        if (w->overlays[back][i].mask) drop_obj(w->overlays[back][i].mask);
    }
    w->num_overlays[back] = 0;

    if (!final_data || final_data->type != TYPE_TABLE) return;

    obj_p keys = AS_LIST(final_data)[0];
    obj_p vals = AS_LIST(final_data)[1];
    if (!keys || !vals) return;
    i64_t ncols = keys->len;

    for (i64_t c = 0; c < ncols && c < MAX_COLS; c++) {
        rfui_col_rules_t* cr = &w->col_rules[c];
        if (cr->num_rules == 0) continue;

        const char* col_name = str_from_symbol(AS_SYMBOL(keys)[c]);
        if (!col_name) continue;

        for (int ri = 0; ri < cr->num_rules; ri++) {
            rfui_rule_t* rule = &cr->rules[ri];
            const char* op_str = op_to_str(rule->op);
            if (!op_str) continue;
            if (!rule->value[0]) continue;

            // Build lambda: "(fn [_d] (let COL (at _d 'COL)) (OP COL VALUE))"
            // For in/within: "(fn [_d] (let COL (at _d 'COL)) (in COL (list V1 V2 ...)))"
            char lambda_buf[2048];
            if (rule->op == RFUI_OP_IN || rule->op == RFUI_OP_WITHIN) {
                snprintf(lambda_buf, sizeof(lambda_buf),
                    "(fn [_d] (let %s (at _d '%s)) (%s %s (list %s)))",
                    col_name, col_name, op_str, col_name, rule->value);
            } else {
                snprintf(lambda_buf, sizeof(lambda_buf),
                    "(fn [_d] (let %s (at _d '%s)) (%s %s %s))",
                    col_name, col_name, op_str, col_name, rule->value);
            }

            obj_p lambda = parse_str(lambda_buf);
            if (!lambda || IS_ERR(lambda)) {
                if (lambda) drop_obj(lambda);
                continue;
            }

            obj_p data_clone = clone_obj(final_data);
            if (!data_clone) { drop_obj(lambda); continue; }
            obj_p call = vn_list(2, lambda, data_clone);
            if (!call) { drop_obj(lambda); drop_obj(data_clone); continue; }

            obj_p result = eval_obj(call);
            if (!result || IS_ERR(result)) {
                if (result) drop_obj(result);
                continue;
            }

            if (result->type == TYPE_B8) {
                if (rule->color && w->num_overlays[back] < MAX_OVERLAYS) {
                    rfui_color_overlay_t* ov = &w->overlays[back][w->num_overlays[back]++];
                    ov->col_idx = c;
                    ov->mask = result;
                    ov->color = rule->color;
                } else if (rule->fn) {
                    // Callback: call fn with cell value for each matching row
                    obj_p col_vec = AS_LIST(vals)[c];
                    if (col_vec) {
                        b8_t* mask_data = AS_B8(result);
                        for (i64_t row = 0; row < result->len; row++) {
                            if (!mask_data[row]) continue;
                            obj_p cell = at_idx(col_vec, row);
                            if (!cell) continue;
                            vm_stack_push(cell);
                            obj_p r = lambda_call(rule->fn, vm_stack_peek(0), 1);
                            if (r && !IS_ERR(r)) drop_obj(r);
                        }
                    }
                    drop_obj(result);
                } else {
                    drop_obj(result);
                }
            } else {
                drop_obj(result);
            }
        }
    }
}
```

**Step 4: Build**

Run: `make 2>&1 | head -30`
Expected: Should compile (grid_renderer.cpp errors from old rules[] references will be fixed in Task 5).

**Step 5: Commit**

```bash
git add src/rayforce_thread.c
git commit -m "refactor(rules): per-column evaluate_rules, remove fn_rule"
```

---

### Task 5: Column Header Dropdown UI

**Files:**
- Modify: `src/grid_renderer.cpp`

This is the largest task. Replace the settings popup and add per-column dropdown menus.

**Step 1: Update grid_ui_state_t**

Replace the `grid_ui_state_t` struct (line 27-30) with:

```cpp
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
```

**Step 2: Add helper to send column rules**

Add after `send_post_query()` (after line 69):

```cpp
// Helper to send MSG_SET_COL_RULES to Rayforce thread
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
```

**Step 3: Add operator names array**

Add near the top of grid_renderer.cpp (after the includes, around line 24):

```cpp
static const char* op_names[] = { ">", "<", ">=", "<=", "==", "!=", "in", "within" };
static const int op_count = 8;
```

**Step 4: Remove the old Settings popup**

Delete lines 312-375 (the entire `if (ImGui::SmallButton(ICON_GEAR " Settings"))` block and its `BeginPopup`/`EndPopup`).

**Step 5: Replace `ImGui::TableHeadersRow()` with custom headers**

Replace line 449 (`ImGui::TableHeadersRow();`) with a custom header row that includes chevron dropdowns. Replace from line 446 (`ImGui::TableSetupScrollFreeze`) through line 449 with:

```cpp
        ImGui::TableSetupScrollFreeze(0, 1);

        // Custom header row with dropdown chevrons
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (i64_t ci = 0; ci < ncols; ci++) {
            ImGui::TableSetColumnIndex((int)ci);
            const char* col_name = str_from_symbol(AS_SYMBOL(keys)[ci]);
            if (!col_name) col_name = "?";

            // Header text
            ImGui::Text("%s", col_name);

            // Sort indicator
            if (ci < MAX_COLS && widget->col_rules[ci].sort_dir != 0) {
                ImGui::SameLine();
                ImGui::TextDisabled(widget->col_rules[ci].sort_dir > 0 ? ICON_SORT_UP : ICON_SORT_DOWN);
            }

            // Rule count indicator
            if (ci < MAX_COLS && widget->col_rules[ci].num_rules > 0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.0f, 1.0f), ICON_PALETTE);
            }

            // Chevron dropdown button
            ImGui::SameLine();
            ImGui::PushID((int)ci);
            char popup_id[32];
            snprintf(popup_id, sizeof(popup_id), "col_menu_%d", (int)ci);
            if (ImGui::SmallButton(ICON_CHEVRON_DN)) {
                ImGui::OpenPopup(popup_id);
            }

            // Column popup menu
            if (ImGui::BeginPopup(popup_id)) {
                ImGui::Text("%s", col_name);
                ImGui::Separator();

                // Sort ascending
                if (ImGui::Selectable(ICON_SORT_UP " Sort Ascending")) {
                    // Clear other column sorts
                    for (i64_t j = 0; j < ncols && j < MAX_COLS; j++)
                        widget->col_rules[j].sort_dir = 0;
                    widget->col_rules[ci].sort_dir = 1;
                    // Send post-query
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

                // Add color rule
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
                        ImGui::OpenPopup("RuleBuilder");
                    }
                }

                ImGui::Separator();

                // Active rules for this column
                if (ci < MAX_COLS && widget->col_rules[ci].num_rules > 0) {
                    ImGui::TextDisabled("Active Rules:");
                    rfui_col_rules_t* cr = &widget->col_rules[ci];
                    for (int ri = 0; ri < cr->num_rules; ri++) {
                        ImGui::PushID(ri);
                        rfui_rule_t* r = &cr->rules[ri];
                        // Display rule summary
                        const char* op_s = (r->op >= 0 && r->op < op_count) ? op_names[r->op] : "?";
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
                            // Delete rule: shift remaining down
                            for (int j = ri; j < cr->num_rules - 1; j++)
                                cr->rules[j] = cr->rules[j + 1];
                            cr->num_rules--;
                            send_col_rules(widget, ci);
                            ri--;
                        }
                        ImGui::PopID();
                    }
                }

                // Rule builder sub-popup
                if (ImGui::BeginPopup("RuleBuilder")) {
                    if (ui_state && ui_state->editing_col == (int)ci) {
                        ImGui::Text("New Color Rule — %s", col_name);
                        ImGui::Separator();

                        // Operator combo
                        ImGui::SetNextItemWidth(80);
                        if (ImGui::BeginCombo("##op", op_names[ui_state->builder_op])) {
                            for (int oi = 0; oi < op_count; oi++) {
                                if (ImGui::Selectable(op_names[oi], ui_state->builder_op == oi))
                                    ui_state->builder_op = oi;
                            }
                            ImGui::EndCombo();
                        }

                        // Value input(s)
                        ImGui::SameLine();
                        if (ui_state->builder_op == RFUI_OP_WITHIN) {
                            ImGui::SetNextItemWidth(80);
                            ImGui::InputText("##v1", ui_state->builder_value, sizeof(ui_state->builder_value));
                            ImGui::SameLine();
                            ImGui::Text("to");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(80);
                            ImGui::InputText("##v2", ui_state->builder_value2, sizeof(ui_state->builder_value2));
                        } else if (ui_state->builder_op == RFUI_OP_IN) {
                            ImGui::SetNextItemWidth(200);
                            ImGui::InputText("##vals", ui_state->builder_value, sizeof(ui_state->builder_value));
                            ImGui::TextDisabled("Comma-separated: 'AAPL 'MSFT 'GOOG");
                        } else {
                            ImGui::SetNextItemWidth(120);
                            ImGui::InputText("##val", ui_state->builder_value, sizeof(ui_state->builder_value));
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
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel")) {
                            ui_state->editing_col = -1;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
```

**Step 6: Update ui_state initialization**

Replace lines 282-290 (the ui_state init block) with:

```cpp
    grid_ui_state_t* ui_state = (grid_ui_state_t*)widget->ui_state;
    if (!ui_state) {
        ui_state = (grid_ui_state_t*)calloc(1, sizeof(grid_ui_state_t));
        if (ui_state) {
            ui_state->selected_row = -1;
            ui_state->editing_col = -1;
            widget->ui_state = ui_state;
        }
    }
```

**Step 7: Build and verify**

Run: `make 2>&1`
Expected: Clean build, no errors.

**Step 8: Commit**

```bash
git add src/grid_renderer.cpp
git commit -m "feat(grid): per-column dropdown menus with sort and color rule builder"
```

---

### Task 6: Verify with simulator example

**Files:**
- Modify: `examples/simulator.rfl` (if it exists, otherwise use manual REPL testing)

**Step 1: Check for existing test examples**

Run: `ls examples/`

**Step 2: Build and run**

Run: `make && ./rayforce-ui examples/simulator.rfl`

**Step 3: Manual verification**

1. Click the chevron (▼) on the Price column header
2. Select "Add Color Rule..."
3. Set operator to `>`, value to `800`, pick a red color, click Add
4. Verify only Price cells > 800 are colored red (not entire rows)
5. Click chevron on Sym column, add rule: `==`, value `'AAPL`, green color
6. Verify only Sym cells with AAPL are colored green
7. Test sort ascending/descending from column menu
8. Test deleting a rule via the ✕ button

**Step 4: Commit any fixes**

```bash
git add -A
git commit -m "fix: address issues found during per-column rules testing"
```

---

## Summary

| Task | Description | Files |
|------|-------------|-------|
| 1 | Data model: new structs in widget.h | `include/rfui/widget.h` |
| 2 | Widget lifecycle + op helpers | `src/widget.c` |
| 3 | Message type + icons | `include/rfui/message.h`, `include/rfui/icons.h` |
| 4 | Rayforce thread: MSG handler + evaluate_rules rewrite | `src/rayforce_thread.c` |
| 5 | Grid renderer: column dropdown UI | `src/grid_renderer.cpp` |
| 6 | Manual verification with simulator | - |
