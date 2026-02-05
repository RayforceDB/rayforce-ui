// src/rayforce_thread.c
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../deps/rayforce/core/runtime.h"
#include "../deps/rayforce/core/poll.h"
#include "../deps/rayforce/core/symbols.h"
#include "../deps/rayforce/core/ops.h"
#include "../deps/rayforce/core/util.h"
#include "../deps/rayforce/core/dynlib.h"  // For ext_t (external object structure)
#include "../deps/rayforce/core/io.h"      // For ray_load (script execution)
#include "../deps/rayforce/core/lambda.h"  // For lambda_call
#include "../deps/rayforce/core/eval.h"    // For vm_stack_push/peek
#include "../include/rfui/context.h"
#include "../include/rfui/message.h"
#include "../include/rfui/queue.h"
#include "../include/rfui/widget.h"
#include "../include/rfui/rayforce_thread.h"
#include <GLFW/glfw3.h>

// Thread-local context for rayforce-ui functions
static __thread rfui_ctx_t* g_ctx = NULL;

// Forward declarations
static void on_ui_message(raw_p data);
static void evaluate_rules(rfui_widget_t* w, obj_p final_data);

// Process a single UI message
static void process_ui_message(rfui_ctx_t* ctx, rfui_ui_msg_t* msg) {
    if (!msg) return;

    switch (msg->type) {
        case RFUI_MSG_EVAL:
            if (msg->expr) {
                obj_p result = eval_str(msg->expr);

                // Format result as obj_p string — pass directly to UI
                obj_p fmt = NULL;
                if (result) {
                    fmt = obj_fmt(result, B8_TRUE);
                    drop_obj(result);
                }

                if (fmt && fmt->type == TYPE_C8) {
                    rfui_ray_msg_t* reply = (rfui_ray_msg_t*)malloc(sizeof(rfui_ray_msg_t));
                    if (reply) {
                        reply->type = RFUI_MSG_RESULT;
                        reply->widget = NULL;
                        reply->data = fmt;

                        if (rfui_queue_push(ctx->ray_to_ui, reply)) {
                            glfwPostEmptyEvent();
                        } else {
                            drop_obj(fmt);
                            free(reply);
                        }
                    } else {
                        drop_obj(fmt);
                    }
                } else {
                    if (fmt) drop_obj(fmt);
                }

                free(msg->expr);
            }
            break;

        case RFUI_MSG_SET_POST_QUERY:
            if (!msg->widget) {
                // Null widget - nothing to do
                if (msg->expr) free(msg->expr);
                break;
            }
            if (msg->expr) {
                // Parse the expression string into an obj_p
                obj_p query = parse_str(msg->expr);
                if (query && !IS_ERR(query)) {
                    // Drop old post_query if exists
                    if (msg->widget->post_query) {
                        drop_obj(msg->widget->post_query);
                    }
                    msg->widget->post_query = query;
                } else {
                    // Parse failed - drop the error result if any
                    if (query) drop_obj(query);
                }
                free(msg->expr);
            }
            break;

        case RFUI_MSG_DROP:
            // Drop obj_p after render
            if (msg->obj) {
                drop_obj(msg->obj);
            }
            break;

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

        case RFUI_MSG_QUIT:
            // Set quit flag
            rfui_ctx_set_quit(ctx, B8_TRUE);
            // Exit the poll loop
            if (runtime_get()) {
                poll_exit(runtime_get()->poll, 0);
            }
            break;

        default:
            // Unknown message type - ignore
            break;
    }

    // Free the message struct
    free(msg);
}

// Waker callback - called when UI thread wakes the Rayforce thread
static void on_ui_message(raw_p data) {
    rfui_ctx_t* ctx = (rfui_ctx_t*)data;
    rfui_ui_msg_t* msg;

    if (!ctx) return;

    // Capture poll pointer at start to avoid use-after-free if runtime changes
    poll_p poll = runtime_get() ? runtime_get()->poll : NULL;
    (void)poll;  // Currently unused but captured for safety

    // Drain the queue and process all pending messages
    // Check quit flag to exit early on shutdown
    while (!rfui_ctx_get_quit(ctx) &&
           (msg = (rfui_ui_msg_t*)rfui_queue_pop(ctx->ui_to_ray)) != NULL) {
        process_ui_message(ctx, msg);
    }
}

// Register rayforce-ui extension types (stub for now)
static void register_rfui_types(void) {
    // Widget type uses TYPE_EXT (external) which is built-in
    // No custom type registration needed
}

// Helper: Map symbol string to widget type
static b8_t widget_type_from_symbol(i64_t sym_id, rfui_widget_type_t* out_type) {
    const char* type_str = str_from_symbol(sym_id);
    if (!type_str) return B8_FALSE;

    if (strcmp(type_str, "grid") == 0) {
        *out_type = RFUI_WIDGET_GRID;
        return B8_TRUE;
    } else if (strcmp(type_str, "chart") == 0) {
        *out_type = RFUI_WIDGET_CHART;
        return B8_TRUE;
    } else if (strcmp(type_str, "text") == 0) {
        *out_type = RFUI_WIDGET_TEXT;
        return B8_TRUE;
    }
    return B8_FALSE;
}

// Drop function for widget external objects
// Note: We don't actually free the widget here because the UI thread owns it
// The UI will free it when the widget is closed/destroyed
static nil_t widget_drop(raw_p ptr) {
    UNUSED(ptr);
    // Widget is owned by UI thread, do not free here
}

// fn_widget: (widget {type: 'grid name: "myname"})
// Takes a dict with 'type and 'name keys, returns external object wrapping widget
static obj_p fn_widget(obj_p* x, i64_t n) {
    if (n != 1) {
        return ray_err("widget: expects 1 argument (config dict)");
    }

    // Fail fast: check context before doing any work
    if (!g_ctx) {
        return ray_err("widget: no rayforce-ui context available");
    }

    obj_p config = x[0];
    if (config->type != TYPE_DICT) {
        return ray_err("widget: argument must be a dict");
    }

    // Extract 'type symbol from config
    obj_p type_val = at_sym(config, "type", 4);
    if (!type_val || type_val->type != -TYPE_SYMBOL) {
        if (type_val) drop_obj(type_val);
        return ray_err("widget: missing or invalid 'type (expected symbol)");
    }

    // Extract 'name string from config
    obj_p name_val = at_sym(config, "name", 4);
    if (!name_val || name_val->type != TYPE_C8) {
        drop_obj(type_val);
        if (name_val) drop_obj(name_val);
        return ray_err("widget: missing or invalid 'name (expected string)");
    }

    // Map symbol to widget type
    rfui_widget_type_t wtype;
    if (!widget_type_from_symbol(type_val->i64, &wtype)) {
        drop_obj(type_val);
        drop_obj(name_val);
        return ray_err("widget: unknown type (expected 'grid, 'chart, or 'text)");
    }
    drop_obj(type_val);

    // Get the name string (null-terminated, stack buffer)
    char name_buf[128];
    i64_t nlen = name_val->len < (i64_t)sizeof(name_buf) - 1 ? name_val->len : (i64_t)sizeof(name_buf) - 1;
    memcpy(name_buf, AS_C8(name_val), nlen);
    name_buf[nlen] = '\0';
    drop_obj(name_val);

    // Create widget
    rfui_widget_t* w = rfui_widget_create(wtype, name_buf);

    if (!w) {
        return ray_err("widget: failed to create widget");
    }

    // Send WIDGET_CREATED message to UI
    rfui_ray_msg_t* msg = malloc(sizeof(rfui_ray_msg_t));
    if (!msg) {
        rfui_widget_destroy(w);
        return ray_err("widget: failed to allocate message");
    }
    msg->type = RFUI_MSG_WIDGET_CREATED;
    msg->widget = w;
    msg->data = NULL;

    rfui_queue_push(g_ctx->ray_to_ui, msg);
    glfwPostEmptyEvent();  // Wake UI thread

    // Return external object wrapping widget pointer
    // The widget_drop function is a no-op since UI owns the widget
    return external(w, widget_drop);
}

// fn_draw: (draw widget data)
// widget is external object, data is the data to render
// Returns the widget for chaining
//
// KNOWN TECHNICAL DEBT: Widget lifecycle race condition
// The widget pointer validity relies on the assumption that the UI thread
// will not destroy the widget while we hold a reference to it. This is safe
// under the current design where widgets live for the session lifetime and
// are only destroyed during shutdown. A proper fix would require either:
// - Widget IDs with lookup validation on the UI side, or
// - Reference counting with atomic operations
// For now, we assume widgets are never destroyed mid-session.
static obj_p fn_draw(obj_p* x, i64_t n) {
    if (n != 2) {
        return ray_err("draw: expects 2 arguments (widget, data)");
    }

    obj_p widget_obj = x[0];
    obj_p data = x[1];

    if (widget_obj->type != TYPE_EXT) {
        return ray_err("draw: first argument must be a widget");
    }

    // Extract widget pointer from external object
    // TYPE_EXT stores data in ext_t structure: { raw_p ptr; nil_t (*drop)(raw_p); }
    ext_p ext = (ext_p)AS_C8(widget_obj);
    rfui_widget_t* w = (rfui_widget_t*)ext->ptr;
    if (!w) {
        return ray_err("draw: widget is null");
    }

    // Check we have context
    if (!g_ctx) {
        return ray_err("draw: no rayforce-ui context available");
    }

    // Apply post_query transformation if widget has one
    // The post_query is a lambda/function that takes data as argument.
    // We build a call expression (post_query data) and evaluate it.
    obj_p final_data;
    if (w->post_query) {
        // Clone the post_query and data for the call expression
        obj_p query_clone = clone_obj(w->post_query);
        if (!query_clone) {
            return ray_err("draw: failed to clone post_query");
        }
        obj_p data_clone = clone_obj(data);
        if (!data_clone) {
            drop_obj(query_clone);
            return ray_err("draw: failed to clone data for post_query");
        }

        // Build call expression: (post_query data)
        // vn_list creates a 2-element list that, when evaluated, calls the function
        obj_p call_expr = vn_list(2, query_clone, data_clone);
        if (!call_expr) {
            drop_obj(query_clone);
            drop_obj(data_clone);
            return ray_err("draw: failed to build call expression");
        }

        // eval_obj consumes the call expression and returns the result
        obj_p result = eval_obj(call_expr);
        if (result && !IS_ERR(result)) {
            final_data = result;
        } else {
            // Query failed, fall back to original data
            if (result) drop_obj(result);
            final_data = clone_obj(data);
            if (!final_data) {
                return ray_err("draw: failed to clone data after query error");
            }
        }
    } else {
        // No post_query, clone data directly
        final_data = clone_obj(data);
        if (!final_data) {
            return ray_err("draw: failed to clone data");
        }
    }

    // Evaluate rules against final_data (produces overlays on widget)
    evaluate_rules(w, final_data);

    // Send DRAW message to UI
    rfui_ray_msg_t* msg = malloc(sizeof(rfui_ray_msg_t));
    if (!msg) {
        drop_obj(final_data);
        return ray_err("draw: failed to allocate message");
    }
    msg->type = RFUI_MSG_DRAW;
    msg->widget = w;

    // For text/alert widgets, pre-format on Rayforce thread
    if (w->type == RFUI_WIDGET_TEXT) {
        obj_p fmt = obj_fmt(final_data, B8_TRUE);
        drop_obj(final_data);
        msg->data = fmt;  // obj_p string, UI reads directly then drops
    } else {
        msg->data = final_data;
    }

    rfui_queue_push(g_ctx->ray_to_ui, msg);
    glfwPostEmptyEvent();  // Wake UI thread

    // Return widget for chaining
    return clone_obj(widget_obj);
}

// Helper: map alert type symbol to integer (0=info, 1=success, 2=warn, 3=error)
static i8_t alert_type_from_symbol(i64_t sym_id) {
    const char* s = str_from_symbol(sym_id);
    if (!s) return 0;
    if (strcmp(s, "info") == 0)  return 0;
    if (strcmp(s, "ok") == 0)    return 1;
    if (strcmp(s, "warn") == 0)  return 2;
    if (strcmp(s, "error") == 0) return 3;
    return 0;  // default to info for unknown
}

// fn_alert: (alert "message") or (alert {type: 'error text: "fail" duration: 5000})
// Sends a toast notification to the UI — no widget needed
static obj_p fn_alert(obj_p* x, i64_t n) {
    if (n != 1) {
        return ray_err("alert: expects 1 argument (string or dict)");
    }
    if (!g_ctx) {
        return ray_err("alert: no rayforce-ui context available");
    }

    obj_p val = x[0];
    if (!val) return atom(-TYPE_NULL);

    obj_p text_obj = NULL;
    i8_t  alert_type = 0;      // default: info
    i32_t alert_duration = 0;  // default: 0 means 3000ms

    if (val->type == TYPE_DICT) {
        // Extract 'text key (required)
        obj_p text_val = at_sym(val, "text", 4);
        if (!text_val) {
            return ray_err("alert: dict missing 'text key");
        }
        if (text_val->type == TYPE_C8) {
            text_obj = text_val;  // takes ownership
        } else {
            text_obj = obj_fmt(text_val, B8_TRUE);
            drop_obj(text_val);
        }

        // Extract 'type key (optional symbol)
        obj_p type_val = at_sym(val, "type", 4);
        if (type_val) {
            if (type_val->type == -TYPE_SYMBOL) {
                alert_type = alert_type_from_symbol(type_val->i64);
            }
            drop_obj(type_val);
        }

        // Extract 'duration key (optional integer)
        obj_p dur_val = at_sym(val, "duration", 8);
        if (dur_val) {
            if (dur_val->type == TYPE_I64) {
                alert_duration = (i32_t)dur_val->i64;
            }
            drop_obj(dur_val);
        }
    } else if (val->type == TYPE_C8) {
        text_obj = clone_obj(val);
    } else if (IS_ERR(val)) {
        text_obj = obj_fmt(val, B8_FALSE);
    } else {
        text_obj = obj_fmt(val, B8_TRUE);
    }

    if (!text_obj || text_obj->type != TYPE_C8) {
        if (text_obj) drop_obj(text_obj);
        return atom(-TYPE_NULL);
    }

    rfui_ray_msg_t* msg = malloc(sizeof(rfui_ray_msg_t));
    if (!msg) {
        drop_obj(text_obj);
        return atom(-TYPE_NULL);
    }
    msg->type = RFUI_MSG_ALERT;
    msg->widget = NULL;
    msg->data = text_obj;
    msg->alert_type = alert_type;
    msg->alert_duration = alert_duration;

    rfui_queue_push(g_ctx->ray_to_ui, msg);
    glfwPostEmptyEvent();

    return atom(-TYPE_NULL);
}

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

        // Check column type for auto-quoting symbol values
        obj_p col_data = AS_LIST(vals)[c];
        i8_t is_symbol_col = (col_data && col_data->type == TYPE_SYMBOL);

        for (int ri = 0; ri < cr->num_rules; ri++) {
            rfui_rule_t* rule = &cr->rules[ri];
            if (!rule->value[0]) continue;

            char lambda_buf[4096];
            int written;

            if (rule->op == -1) {
                // Raw expression rule — wrap user expression as lambda over table
                written = snprintf(lambda_buf, sizeof(lambda_buf),
                    "(fn [_d] %s)", rule->value);
            } else {
                const char* op_str = op_to_str(rule->op);
                if (!op_str) continue;

                // Auto-quote symbol values if user didn't already
                char val_buf[512];
                if (is_symbol_col && rule->value[0] != '\'') {
                    snprintf(val_buf, sizeof(val_buf), "'%s", rule->value);
                } else {
                    snprintf(val_buf, sizeof(val_buf), "%s", rule->value);
                }

                // Build lambda: "(fn [_d] (let COL (at _d 'COL)) (OP COL VALUE))"
                if (rule->op == RFUI_OP_IN || rule->op == RFUI_OP_WITHIN) {
                    written = snprintf(lambda_buf, sizeof(lambda_buf),
                        "(fn [_d] (let %s (at _d '%s)) (%s %s (list %s)))",
                        col_name, col_name, op_str, col_name, val_buf);
                } else {
                    written = snprintf(lambda_buf, sizeof(lambda_buf),
                        "(fn [_d] (let %s (at _d '%s)) (%s %s %s))",
                        col_name, col_name, op_str, col_name, val_buf);
                }
            }
            if (written >= (int)sizeof(lambda_buf)) continue;  // truncated

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

// Macro to register a function into the runtime's function dict
// Based on REGISTER_FN from env.c but adapted for external registration
#define RFUI_REGISTER_FN(functions, name, fn_type, flags, fn_ptr)   \
    {                                                                  \
        i64_t _k = symbols_intern(name, strlen(name));                \
        push_raw(&AS_LIST(functions)[0], &_k);                        \
        obj_p _o = atom(-(fn_type));                                  \
        _o->attrs = (flags) | ATTR_PROTECTED;                         \
        _o->i64 = (i64_t)(fn_ptr);                                    \
        push_raw(&AS_LIST(functions)[1], &_o);                        \
    }

// Register rayforce-ui functions with Rayforce runtime
static void register_rfui_functions(void) {
    runtime_p rt = runtime_get();
    if (!rt) return;

    obj_p functions = rt->env.functions;

    // Register widget function: (widget {type: 'grid name: "name"}) -> external
    RFUI_REGISTER_FN(functions, "widget", TYPE_VARY, FN_NONE, fn_widget);

    // Register draw function: (draw widget data) -> widget
    RFUI_REGISTER_FN(functions, "draw", TYPE_VARY, FN_NONE, fn_draw);

    // Register alert function: (alert "message") -> null
    RFUI_REGISTER_FN(functions, "alert", TYPE_VARY, FN_NONE, fn_alert);
}

void* rfui_rayforce_thread(void* arg) {
    rfui_ctx_t* ctx = (rfui_ctx_t*)arg;
    runtime_p runtime;
    poll_waker_p waker;

    if (!ctx) {
        return NULL;
    }

    // Step 1: Create Rayforce runtime
    runtime = runtime_create(ctx->argc, ctx->argv);
    if (!runtime) {
        // Signal ready anyway so UI thread doesn't hang
        rfui_ctx_set_quit(ctx, B8_TRUE);
        rfui_ctx_signal_ready(ctx);
        return NULL;
    }

    // Step 2: Set thread-local context for rayforce-ui functions
    g_ctx = ctx;

    // Step 3: Register rayforce-ui extension types
    register_rfui_types();

    // Step 4: Register rayforce-ui functions (widget, draw)
    register_rfui_functions();

    // Step 5: Create poll waker for UI messages
    waker = poll_waker_create(runtime->poll, on_ui_message, ctx);
    if (!waker) {
        g_ctx = NULL;
        runtime_destroy();
        rfui_ctx_set_quit(ctx, B8_TRUE);
        rfui_ctx_signal_ready(ctx);
        return NULL;
    }

    // Step 6: Store waker in context and signal ready
    // Signal ready BEFORE loading script so UI loop can start
    // while potentially slow script operations (IPC, file I/O) execute
    rfui_ctx_set_waker(ctx, waker);
    rfui_ctx_signal_ready(ctx);

    // Step 7: Load script file if provided via command line
    // This may take time (e.g., synchronous IPC calls) but UI is now responsive
    {
        obj_p file_arg = runtime_get_arg("file");
        if (!is_null(file_arg)) {
            obj_p res = ray_load(file_arg);
            drop_obj(file_arg);
            if (IS_ERR(res)) {
                // Print error but continue - GUI still runs
                obj_p fmt = obj_fmt(res, B8_TRUE);
                if (fmt) {
                    fprintf(stderr, "Script error: %.*s\n", (i32_t)fmt->len, AS_C8(fmt));
                    drop_obj(fmt);
                }
            }
            drop_obj(res);
        }
    }

    // Step 8: Run poll loop (blocks until exit)
    runtime_run();

    // Cleanup
    g_ctx = NULL;
    // Waker is a separate allocation - must be explicitly destroyed
    rfui_ctx_set_waker(ctx, NULL);
    poll_waker_destroy(waker);
    runtime_destroy();

    return NULL;
}
