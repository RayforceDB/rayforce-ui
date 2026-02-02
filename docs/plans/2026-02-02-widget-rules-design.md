# Widget Rules: Trigger System for Column Values

## Overview

A general-purpose rule/trigger system for widgets. Rules are Rayfall expressions evaluated against widget data, producing boolean masks that drive actions (cell coloring, callbacks). Rules are defined via the Rayfall API only — no UI-based rule creation.

This establishes a reusable pattern: "Rayfall expression bound to widget data" — the same pipeline will power future UI-driven filters, group-bys, and computed columns.

## Rayfall API

```clj
;; Color rule: highlight cells where price > 150 in red
(rule grid1 {expr: "(> price 150)" color: 0xFF5149})

;; Callback rule: fire lambda when sym matches
(rule grid1 {expr: "(== sym 'AAPL)" fn: (fn [row] (println "match:" row))})

;; Combined: color + callback
(rule grid1 {expr: "(>= size 10000)" color: 0x3FB950 fn: (fn [row] (draw alert row))})

;; Compound expressions — full Rayfall language available
(rule grid1 {expr: "(and (> price 100) (== exchange 'NYSE))" color: 0xD29922})
```

Registration:
```c
ray_register_fn("rule", 2, fn_rule);  // (rule widget opts-dict)
```

## Data Model

### Rule struct (on widget, rayforce side)

```c
#define MAX_RULES 16

typedef struct rfui_rule_t {
    char expr[256];      // Rayfall expression string
    obj_p fn;            // callback lambda or NULL_OBJ (refcounted)
    u32_t color;         // packed RGBA, 0 = no color action
} rfui_rule_t;
```

### Color overlay (sent to UI with draw data)

```c
typedef struct rfui_color_overlay_t {
    i64_t col_idx;       // column index in the table
    obj_p mask;          // boolean vector, same length as data rows (refcounted)
    u32_t color;         // packed RGBA
} rfui_color_overlay_t;
```

### Widget additions

```c
// Added to rfui_widget_t:
rfui_rule_t rules[MAX_RULES];
int num_rules;
rfui_color_overlay_t overlays[MAX_RULES];
int num_overlays;
```

## Data Flow

### Rule registration (`fn_rule`, rayforce thread)

1. Parse opts dict: extract `expr` (string), `color` (int), `fn` (lambda)
2. `ref_obj` the fn if present
3. Append to `widget->rules[widget->num_rules++]`

### Draw-time evaluation (`fn_draw`, rayforce thread)

When `(draw widget data)` is called and `widget->num_rules > 0`:

1. Drop previous overlay masks (`drop_obj` each)
2. For each rule:
   a. `parse(rule->expr)` → AST
   b. Bind column names from the data table into the eval environment
   c. `eval(ast)` → boolean vector (rayforce ops are vectorized)
   d. If rule has `color` → store `{col_idx, mask, color}` in `overlays[]`, `ref_obj(mask)`
   e. If rule has `fn` → iterate mask, call lambda for each true row
3. Send data + overlays to UI via queue

### UI rendering (grid_renderer.cpp)

For each visible cell at `(col_idx, row)`:

```cpp
for (int oi = 0; oi < num_overlays; oi++) {
    if (overlays[oi].col_idx == col_idx) {
        // mask is a boolean vector — direct array access
        if (AS_I8(overlays[oi].mask)[row]) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(overlays[oi].color));
            cell_colored = true;
            break;
        }
    }
}
```

Single boolean array lookup per rule — fast, no string comparison.

## Cleanup

- `widget_drop`: `drop_obj` all rule fns, all overlay masks
- Each `draw`: `drop_obj` previous overlay masks before rebuilding

## Removed Code

The existing UI-side color rule system is removed:
- `color_rule_t` struct
- `grid_ui_state_t.color_rules[]` and `num_rules`
- Settings popup "Color Rules" section (Add Rule button, column combo, value input, color picker)
- `rule_col_idx[]` precomputation
- `strcmp` cell matching in render loop

The Settings button/popup can remain for other future settings.

## Files Modified

| File | Change |
|------|--------|
| `include/rfui/widget.h` | Add `rfui_rule_t`, `rfui_color_overlay_t`, fields on `rfui_widget_t` |
| `src/widget.c` | Register `rule` fn, implement `fn_rule`, cleanup in `widget_drop` |
| `src/rayforce_thread.c` | In draw path: parse/eval rules, build overlays, fire callbacks |
| `src/grid_renderer.cpp` | Replace strcmp matching with overlay mask lookup, remove color rule UI |

## Future Reuse

The parse → bind columns → eval → boolean mask pipeline is reusable for:
- **Filters:** UI builds `(where: (> price 100))` → same eval, used to filter rows
- **Group-bys:** UI builds `(by: sym)` → eval produces grouping
- **Computed columns:** UI builds `(* spread: (- ask bid))` → eval produces new column
- **Alerts:** Rules with callbacks can trigger notifications, sounds, or cross-widget updates

All compile to Rayfall expressions and pass through the same rayforce eval machinery.

## Testing

```clj
(set g (widget {type: 'grid name: "test"}))
(draw g (table {price: (100 200 50 300) sym: ('A 'B 'A 'C)}))
(rule g {expr: "(> price 150)" color: 0xFF5149})
(rule g {expr: "(== sym 'A)" color: 0x3FB950})
```

Expected: rows with price > 150 in red, rows with sym A in green.
