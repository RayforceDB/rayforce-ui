// src/text_renderer.cpp
// Text widget renderer for displaying formatted Rayforce object output
//
// NOTE: All Rayforce obj_p formatting is done on the Rayforce thread
// (in fn_draw) before sending to the UI. The text renderer reads directly
// from widget->data (obj_p TYPE_C8) — zero-copy, no serialization.

#include <stdio.h>
#include <string.h>

#include "imgui.h"

// Make rayforce headers C++ compatible by redefining _Static_assert
#define _Static_assert static_assert

extern "C" {
#include "../include/rfui/text_renderer.h"
#include "../include/rfui/widget.h"
#include "../../deps/rayforce/core/rayforce.h"
}

extern "C" {

nil_t rfui_render_text(rfui_widget_t* widget) {
    if (widget == nullptr) return;

    // render_data is obj_p TYPE_C8 (pre-formatted on Rayforce thread)
    obj_p data = widget->render_data;
    if (!data || data->type != TYPE_C8) {
        ImGui::TextDisabled("No data");
        return;
    }

    const char* text = AS_C8(data);
    i64_t len = data->len;

    // Use large font for label display
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.Size > 1) {
        ImGui::PushFont(io.Fonts->Fonts[1]);
    }

    // Center text vertically and horizontally in available space
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 text_size = ImGui::CalcTextSize(text, text + len);
    ImVec2 cursor = ImGui::GetCursorPos();
    if (text_size.x < avail.x)
        ImGui::SetCursorPosX(cursor.x + (avail.x - text_size.x) * 0.5f);
    if (text_size.y < avail.y)
        ImGui::SetCursorPosY(cursor.y + (avail.y - text_size.y) * 0.5f);

    ImGui::TextUnformatted(text, text + len);

    if (io.Fonts->Fonts.Size > 1) {
        ImGui::PopFont();
    }
}

} // extern "C"
