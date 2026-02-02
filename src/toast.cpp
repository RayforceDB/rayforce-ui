// src/toast.cpp
// Alert panel — dockable window showing timestamped alert messages.
// Alerts arrive via (alert "text") builtin → RFUI_MSG_ALERT → rfui_toast_push.

#include <vector>
#include <string>
#include <time.h>

#include "imgui.h"
#include "../include/rfui/icons.h"
#include "../include/rfui/ansi_text.h"

extern "C" {
#include "../include/rfui/toast.h"
#include "../../deps/rayforce/core/rayforce.h"
}

struct alert_entry_t {
    std::string text;
};

static std::vector<alert_entry_t> g_alerts;
static int  g_max_alerts = 200;
static bool g_auto_scroll = true;
static int  g_prev_count = 0;
static bool g_panel_open = false;
static bool g_muted = false;

extern "C" {

void rfui_toast_push(const char* text) {
    if (!text || g_muted) return;

    // Prepend timestamp
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    alert_entry_t e;
    e.text = std::string("[") + ts + "] " + text;

    if ((int)g_alerts.size() >= g_max_alerts) {
        g_alerts.erase(g_alerts.begin());
    }
    g_alerts.push_back(std::move(e));

    // Auto-open panel on first alert
    g_panel_open = true;
}

void rfui_toast_push_obj(void* obj) {
    obj_p o = (obj_p)obj;
    if (!o || o->type != TYPE_C8 || g_muted) return;

    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    alert_entry_t e;
    e.text = std::string("[") + ts + "] " + std::string(AS_C8(o), o->len);

    if ((int)g_alerts.size() >= g_max_alerts) {
        g_alerts.erase(g_alerts.begin());
    }
    g_alerts.push_back(std::move(e));
    g_panel_open = true;
}

void rfui_toast_render(void) {
    if (!g_panel_open) return;

    char label[64];
    snprintf(label, sizeof(label), "%s Alerts", ICON_BELL);

    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 150), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    ImGui::Begin(label, &g_panel_open);

    // Toolbar
    if (ImGui::SmallButton(ICON_ERASER " Clear")) {
        g_alerts.clear();
    }
    ImGui::SameLine();
    if (g_muted) {
        if (ImGui::SmallButton(ICON_BELL " Unmute")) g_muted = false;
    } else {
        if (ImGui::SmallButton(ICON_BELL " Mute")) g_muted = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%d)", (int)g_alerts.size());

    // Message list
    ImGui::BeginChild("##alert_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImVec4 ts_color   = ImVec4(0.545f, 0.580f, 0.620f, 1.0f);  // gray timestamp
    ImVec4 bell_color = ImVec4(0.914f, 0.627f, 0.200f, 1.0f);  // accent gold bell
    ImVec4 text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    for (const auto& e : g_alerts) {
        // Draw gold left border
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos, ImVec2(pos.x + 3.0f, pos.y + ImGui::GetTextLineHeight()),
            IM_COL32(233, 160, 51, 255));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);

        // Bell icon in gold
        ImGui::PushStyleColor(ImGuiCol_Text, bell_color);
        ImGui::TextUnformatted(ICON_BELL);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 4);

        // Timestamp in gray, message body with ANSI colors
        // e.text is "[HH:MM:SS] message..."
        const char* t = e.text.c_str();
        const char* bracket_end = strchr(t, ']');
        if (bracket_end) {
            ImGui::PushStyleColor(ImGuiCol_Text, ts_color);
            ImGui::TextUnformatted(t, bracket_end + 1);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 4);
            render_ansi_text(bracket_end + 2, text_color);
        } else {
            render_ansi_text(t, text_color);
        }
        ImGui::NewLine();
    }

    // Auto-scroll on new messages
    if (g_auto_scroll && (int)g_alerts.size() != g_prev_count) {
        ImGui::SetScrollHereY(1.0f);
    }
    g_prev_count = (int)g_alerts.size();

    ImGui::EndChild();
    ImGui::End();
}

void rfui_toast_destroy(void) {
    g_alerts.clear();
    g_panel_open = false;
    g_prev_count = 0;
}

} // extern "C"
