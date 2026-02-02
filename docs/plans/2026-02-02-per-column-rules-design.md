# Per-Column Rules & Column Menu Design

## Overview

Replace the flat widget-level rules system with per-column rule management via dropdown menus in grid column headers. Rules are configured entirely from the UI using structured inputs (operator + value + color/callback), and the Rayforce thread generates Rayfall expressions at eval time.

## Column Header Dropdown

Each column header displays a chevron icon (▼) on the right. Clicking opens an ImGui popup scoped to that column.

```
┌─── Price ──────────────┐
│ ▲ Sort Ascending       │
│ ▼ Sort Descending      │
│ ─────────────────────  │
│ + Add Color Rule...    │
│ + Add Callback Rule... │
│ ─────────────────────  │
│ Active Rules:          │
│  ● (> Price 800) [🔴]  ✕│
│  ● (< Price 100) [🟢]  ✕│
└────────────────────────┘
```

- Sort sends a post-query (`xasc`/`xdesc`). One sort active at a time across all columns.
- "Add Color Rule..." / "Add Callback Rule..." open expression builder sub-popups.
- Active rules for this column listed with color swatch and delete button.

### Expression Builder

```
┌─── New Color Rule ─────┐
│ Price  [>]  [800    ]  │
│ Color: [████] (picker) │
│        [Add]  [Cancel] │
└────────────────────────┘
```

- Column name pre-filled, read-only.
- Operator combo box adapts the value input:
  - Standard ops (`>`, `<`, `>=`, `<=`, `==`, `!=`): single value field.
  - `in`: comma-separated multi-value field → `(in Col (list v1 v2 v3))`.
  - `within`: two fields (low/high) → `(within Col (list low high))`.
- Color picker for color rules; lambda selector for callback rules.

## Data Model

### Operator Enum

```c
typedef enum {
    RFUI_OP_GT,      // >
    RFUI_OP_LT,      // <
    RFUI_OP_GE,      // >=
    RFUI_OP_LE,      // <=
    RFUI_OP_EQ,      // ==
    RFUI_OP_NE,      // !=
    RFUI_OP_IN,      // in
    RFUI_OP_WITHIN,  // within
} rfui_op_t;
```

### Structs

```c
#define MAX_COLS 64
#define MAX_COL_RULES 8

typedef struct rfui_rule_t {
    i8_t op;            // rfui_op_t
    char value[256];    // operand ("800", "'AAPL", "'AAPL, 'MSFT")
    u32_t color;        // packed 0xRRGGBB, 0 = no color
    obj_p fn;           // callback lambda or NULL
} rfui_rule_t;

typedef struct rfui_col_rules_t {
    rfui_rule_t rules[MAX_COL_RULES];
    int num_rules;
    i8_t sort_dir;      // 0=none, 1=asc, -1=desc
} rfui_col_rules_t;
```

Widget struct replaces `rfui_rule_t rules[MAX_RULES]` / `int num_rules` with:

```c
rfui_col_rules_t col_rules[MAX_COLS];
```

## Thread Communication

### New Message: `RFUI_MSG_SET_COL_RULES`

When user adds/removes a rule or changes sort in the column menu:

1. UI thread builds `rfui_col_rules_t` snapshot for that column.
2. Sends `MSG_SET_COL_RULES` with `{ widget_ptr, col_idx, col_rules_copy }`.
3. Rayforce thread copies into `widget->col_rules[col_idx]`.
4. Next `fn_draw` picks up new rules during `evaluate_rules()`.

Sort uses existing `MSG_SET_POST_QUERY` to set `xasc`/`xdesc` expressions.

## Expression Generation

The Rayforce thread generates Rayfall expressions from structured rule data at eval time.

```
op=GT, column="Price", value="800"
→ "(fn [_d] (let Price (at _d 'Price)) (> Price 800))"

op=EQ, column="Sym", value="'AAPL"
→ "(fn [_d] (let Sym (at _d 'Sym)) (== Sym 'AAPL))"

op=IN, column="Sym", value="'AAPL, 'MSFT"
→ "(fn [_d] (let Sym (at _d 'Sym)) (in Sym (list 'AAPL 'MSFT)))"

op=WITHIN, column="Price", value="100, 500"
→ "(fn [_d] (let Price (at _d 'Price)) (within Price (list 100 500)))"
```

### Reworked `evaluate_rules()` Loop

```
for each column c (0..ncols):
    for each rule in col_rules[c]:
        1. Map op enum → operator string
        2. Build lambda: "(fn [_d] (let %s (at _d '%s)) (%s %s %s))"
        3. Parse → eval → boolean vector
        4. Color rule: store overlay with col_idx = c
        5. Callback rule: iterate mask, call fn for matching rows
```

## Files to Modify

| File | Changes |
|------|---------|
| `include/rfui/widget.h` | Add `rfui_op_t` enum, `rfui_col_rules_t` struct. Replace flat `rules[]` with `col_rules[MAX_COLS]`. Update `rfui_rule_t` to structured fields. |
| `include/rfui/message.h` | Add `RFUI_MSG_SET_COL_RULES` |
| `src/rayforce_thread.c` | Remove `fn_rule`. Rework `evaluate_rules()` to iterate per-column, generate expressions from structured data. Handle `MSG_SET_COL_RULES`. |
| `src/grid_renderer.cpp` | Remove gear-icon settings popup. Add chevron dropdown per column header. Implement expression builder sub-popups. Send `MSG_SET_COL_RULES` / `MSG_SET_POST_QUERY`. |
| `src/widget.c` | Update create/destroy for new rule storage. Cleanup `fn` refcounts per column. |
| `src/context.c` | Handle `MSG_SET_COL_RULES` in message dispatch |

## What Gets Removed

- `fn_rule` function and its registration (rules are UI-only now)
- Gear-icon settings popup in grid renderer
- `strstr`-based column detection in `evaluate_rules()`

## What Stays Unchanged

- Double-buffered overlay system
- `rfui_color_overlay_t` struct
- Grid cell rendering and overlay application
- Post-query mechanism (used for sort)
- Thread wake mechanisms (`glfwPostEmptyEvent` / `poll_waker_wake`)
