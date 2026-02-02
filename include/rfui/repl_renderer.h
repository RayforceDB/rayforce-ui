// include/rfui/repl_renderer.h
#ifndef RFUI_REPL_RENDERER_H
#define RFUI_REPL_RENDERER_H

#include "../../deps/rayforce/core/rayforce.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize REPL state (call once at startup)
nil_t rfui_repl_init(nil_t);

// Render REPL content (call each frame, inside main window)
nil_t rfui_repl_render(nil_t);

// Add result text to REPL output (called when MSG_RESULT received)
nil_t rfui_repl_add_result_text(const char* text);

// Add result from obj_p (TYPE_C8) — zero-copy read
nil_t rfui_repl_add_result_obj(obj_p obj);

// Load a script file via REPL (shows in history, evaluates)
nil_t rfui_repl_load_file(const char* path);

// Add a log message to the Console tab
// level: 0=debug(gray), 1=info(white), 2=warn(yellow), 3=error(red)
nil_t rfui_repl_add_log(const char* text, int level);

// Destroy REPL state
nil_t rfui_repl_destroy(nil_t);

#ifdef __cplusplus
}
#endif

#endif // RFUI_REPL_RENDERER_H
