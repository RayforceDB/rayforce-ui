// include/rfui/ansi_text.h
// Shared ANSI escape sequence text rendering for ImGui.
// Include from .cpp files only (uses ImGui types).
#pragma once

#include "imgui.h"

// Standard ANSI 8-color palette
static const ImVec4 ansi_colors[8] = {
    ImVec4(0.0f,   0.0f,   0.0f,   1.0f),  // 0 black
    ImVec4(0.804f, 0.141f, 0.114f, 1.0f),  // 1 red
    ImVec4(0.247f, 0.725f, 0.314f, 1.0f),  // 2 green
    ImVec4(0.824f, 0.600f, 0.133f, 1.0f),  // 3 yellow
    ImVec4(0.345f, 0.651f, 1.000f, 1.0f),  // 4 blue
    ImVec4(0.737f, 0.549f, 1.000f, 1.0f),  // 5 magenta
    ImVec4(0.224f, 0.824f, 0.753f, 1.0f),  // 6 cyan
    ImVec4(0.902f, 0.929f, 0.953f, 1.0f),  // 7 white
};

// Bright ANSI colors (90-97)
static const ImVec4 ansi_bright[8] = {
    ImVec4(0.545f, 0.580f, 0.620f, 1.0f),  // 0 bright black (gray)
    ImVec4(0.973f, 0.318f, 0.286f, 1.0f),  // 1 bright red
    ImVec4(0.341f, 0.894f, 0.400f, 1.0f),  // 2 bright green
    ImVec4(0.941f, 0.769f, 0.290f, 1.0f),  // 3 bright yellow
    ImVec4(0.475f, 0.753f, 1.000f, 1.0f),  // 4 bright blue
    ImVec4(0.847f, 0.694f, 1.000f, 1.0f),  // 5 bright magenta
    ImVec4(0.388f, 0.922f, 0.855f, 1.0f),  // 6 bright cyan
    ImVec4(1.000f, 1.000f, 1.000f, 1.0f),  // 7 bright white
};

// Render text with ANSI escape sequence support
// Handles: ESC[0m (reset), ESC[1m (bold), ESC[3m (dim),
//          ESC[30-37m, ESC[90-97m (fg colors), ESC[38;5;Nm (256-color)
static inline void render_ansi_text(const char* text, ImVec4 default_color) {
    if (!text || !*text) return;

    ImVec4 current_color = default_color;
    bool bold = false;
    bool has_custom_color = false;
    const char* p = text;

    while (*p) {
        const char* span_start = p;
        while (*p && !(*p == '\033' && *(p + 1) == '[')) p++;

        if (p > span_start) {
            const char* seg = span_start;
            while (seg < p) {
                const char* nl = seg;
                while (nl < p && *nl != '\n') nl++;
                if (nl > seg) {
                    ImGui::PushStyleColor(ImGuiCol_Text, current_color);
                    ImGui::TextUnformatted(seg, nl);
                    ImGui::PopStyleColor();
                    if (nl < p || *p) ImGui::SameLine(0, 0);
                }
                if (nl < p && *nl == '\n') { ImGui::NewLine(); nl++; }
                seg = nl;
            }
        }

        if (!*p) break;

        p += 2;  // skip ESC[
        while (*p) {
            int code = 0;
            bool has_num = false;
            while (*p >= '0' && *p <= '9') { code = code * 10 + (*p - '0'); has_num = true; p++; }
            if (!has_num) code = 0;

            if (code == 0) {
                current_color = default_color; bold = false; has_custom_color = false;
            } else if (code == 1) {
                bold = true;
                if (!has_custom_color) current_color = default_color;
            } else if (code == 2 || code == 3) {
                current_color.w = 0.7f;
            } else if (code >= 30 && code <= 37) {
                current_color = bold ? ansi_bright[code - 30] : ansi_colors[code - 30];
                has_custom_color = true;
            } else if (code == 39) {
                current_color = default_color; has_custom_color = false;
            } else if (code >= 90 && code <= 97) {
                current_color = ansi_bright[code - 90]; has_custom_color = true;
            } else if (code == 38) {
                if (*p == ';') {
                    p++;
                    int mode = 0;
                    while (*p >= '0' && *p <= '9') { mode = mode * 10 + (*p - '0'); p++; }
                    if (mode == 5 && *p == ';') {
                        p++;
                        int idx = 0;
                        while (*p >= '0' && *p <= '9') { idx = idx * 10 + (*p - '0'); p++; }
                        if (idx < 8) current_color = ansi_colors[idx];
                        else if (idx < 16) current_color = ansi_bright[idx - 8];
                        else if (idx < 232) {
                            int v = idx - 16;
                            current_color = ImVec4((v/36)/5.0f, ((v%36)/6)/5.0f, (v%6)/5.0f, 1.0f);
                        } else {
                            float gray = (float)(8 + (idx - 232) * 10) / 255.0f;
                            current_color = ImVec4(gray, gray, gray, 1.0f);
                        }
                        has_custom_color = true;
                    } else if (mode == 2 && *p == ';') {
                        int rgb[3] = {0, 0, 0};
                        for (int i = 0; i < 3 && *p == ';'; i++) {
                            p++;
                            while (*p >= '0' && *p <= '9') { rgb[i] = rgb[i] * 10 + (*p - '0'); p++; }
                        }
                        current_color = ImVec4(rgb[0]/255.0f, rgb[1]/255.0f, rgb[2]/255.0f, 1.0f);
                        has_custom_color = true;
                    }
                }
            }

            if (*p == ';') { p++; }
            else if (*p == 'm') { p++; break; }
            else { while (*p && *p != 'm') p++; if (*p == 'm') p++; break; }
        }
    }
}
