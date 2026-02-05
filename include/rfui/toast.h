// include/rfui/toast.h
#ifndef RFUI_TOAST_H
#define RFUI_TOAST_H

#ifdef __cplusplus
extern "C" {
#endif

// Push info toast with default duration
void rfui_toast_push(const char* text);

// Push toast with severity and duration (type: 0=info,1=success,2=warn,3=error; ms: 0=default 3000)
void rfui_toast_push_alert(const char* text, int type, int ms);

// Render all active toasts (call each frame)
void rfui_toast_render(void);

// Cleanup toast system (no-op)
void rfui_toast_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // RFUI_TOAST_H
