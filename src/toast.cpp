// src/toast.cpp
// Ephemeral toast notifications via ImGuiNotify.
// Alerts arrive via (alert ...) builtin → RFUI_MSG_ALERT → rfui_toast_push_alert.

#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiNotify.hpp"

extern "C" {
#include "../include/rfui/toast.h"
}

extern "C" {

void rfui_toast_push(const char* text) {
    rfui_toast_push_alert(text, 0, 0);
}

void rfui_toast_push_alert(const char* text, int type, int ms) {
    if (!text) return;

    // Map int type → ImGuiToastType
    ImGuiToastType toast_type;
    switch (type) {
        case 1:  toast_type = ImGuiToastType::Success; break;
        case 2:  toast_type = ImGuiToastType::Warning; break;
        case 3:  toast_type = ImGuiToastType::Error;   break;
        default: toast_type = ImGuiToastType::Info;    break;
    }

    int duration = (ms > 0) ? ms : 3000;

    ImGuiToast toast(toast_type, duration);
    toast.setContent("%s", text);
    ImGui::InsertNotification(toast);
}

void rfui_toast_render(void) {
    ImGui::RenderNotifications();
}

void rfui_toast_destroy(void) {
    // no-op — ImGuiNotify manages its own state via ImGui globals
}

} // extern "C"
