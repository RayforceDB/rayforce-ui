// include/rfui/toast.h
#ifndef RFUI_TOAST_H
#define RFUI_TOAST_H

#ifdef __cplusplus
extern "C" {
#endif

// Push a toast notification (text is copied internally)
void rfui_toast_push(const char* text);

// Push from obj_p (TYPE_C8) — zero-copy read, copies to internal storage
void rfui_toast_push_obj(void* obj);

// Render all active toasts (call each frame after registry render)
void rfui_toast_render(void);

// Cleanup toast system
void rfui_toast_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // RFUI_TOAST_H
