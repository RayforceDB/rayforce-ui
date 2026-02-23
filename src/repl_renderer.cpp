// src/repl_renderer.cpp
// Terminal-style REPL renderer — renders directly into the main window

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "../include/rfui/icons.h"
#include "../include/rfui/syntax.h"
#include "ImGuiFileDialog.h"
#include "imgui.h"

// Scrollback limits
static const int MAX_HISTORY_SIZE = 1000;
static const int MAX_OUTPUT_LINES = 10000;
static const int MAX_CONSOLE_LINES = 10000;

#define _Static_assert static_assert

extern "C" {
#include "../include/rfui/repl_renderer.h"
#include "../include/rfui/rfui.h"
}

// Line type for terminal display
enum LineType {
  LINE_INPUT,  // "> expression" - user input
  LINE_RESULT, // result of evaluation
  LINE_ERROR   // error message
};

struct terminal_line_t {
  std::string text;
  LineType type;
};

// Console log levels
enum LogLevel { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };

struct console_line_t {
  std::string text;
  LogLevel level;
};

// REPL state
struct repl_state_t {
  char input_buf[4096];
  std::vector<std::string> history;   // Command history for up/down
  std::vector<terminal_line_t> lines; // Terminal output lines
  int history_pos;
  bool scroll_to_bottom;
  std::string saved_input;

  // Console tab
  std::vector<console_line_t> console_lines;
  bool console_scroll_to_bottom;

  // Click-to-focus (deferred one frame)
  bool focus_next_frame;

  // Autocomplete
  bool show_autocomplete;
  std::vector<std::string> completions;
  int autocomplete_idx;
  std::string completion_prefix;
  int completion_prefix_start; // byte offset of prefix in input_buf
  ImVec2 last_input_pos;       // Saved position of input field for overlay
};

// Module-level REPL state (singleton — one REPL per application)
static repl_state_t *g_repl = nullptr;

// ANSI color tables and render_ansi_text() from shared header
#include "../include/rfui/ansi_text.h"


static bool is_word_char_ac(char c) {
  return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '?' ||
         c == '!';
}

// Extract the word before cursor position
static void extract_prefix(const char *buf, int cursor_pos, std::string &prefix,
                           int &prefix_start) {
  int start = cursor_pos;
  while (start > 0 && is_word_char_ac(buf[start - 1])) {
    start--;
  }
  prefix.assign(buf + start, cursor_pos - start);
  prefix_start = start;
}

// Build filtered completion list from prefix
static void update_completions(repl_state_t *state, const char *buf,
                               int cursor_pos) {
  std::string prefix;
  int prefix_start;
  extract_prefix(buf, cursor_pos, prefix, prefix_start);

  state->completion_prefix = prefix;
  state->completion_prefix_start = prefix_start;
  state->completions.clear();
  state->autocomplete_idx = 0;

  if (prefix.empty()) {
    state->show_autocomplete = false;
    return;
  }

  int plen = (int)prefix.size();
  const char **kws = rfui_get_keywords();
  for (int i = 0; kws[i]; i++) {
    if (strncmp(kws[i], prefix.c_str(), plen) == 0 &&
        (int)strlen(kws[i]) > plen)
      state->completions.push_back(kws[i]);
  }
  const char **bis = rfui_get_builtins();
  for (int i = 0; bis[i]; i++) {
    if (strncmp(bis[i], prefix.c_str(), plen) == 0 &&
        (int)strlen(bis[i]) > plen)
      state->completions.push_back(bis[i]);
  }

  std::sort(state->completions.begin(), state->completions.end());
  state->show_autocomplete = !state->completions.empty();
}

// Apply selected completion into the input buffer
static void apply_completion(ImGuiInputTextCallbackData *data,
                             repl_state_t *state) {
  if (state->completions.empty())
    return;
  int idx = state->autocomplete_idx;
  if (idx < 0 || idx >= (int)state->completions.size())
    idx = 0;

  const std::string &word = state->completions[idx];
  int prefix_len = (int)state->completion_prefix.size();
  int start = state->completion_prefix_start;

  // Replace prefix with full word
  data->DeleteChars(start, prefix_len);
  data->InsertChars(start, word.c_str());

  state->show_autocomplete = false;
  state->completions.clear();
}

// Input callback handling history, completion, and always (for arrow keys)
static int input_callback(ImGuiInputTextCallbackData *data) {
  repl_state_t *state = (repl_state_t *)data->UserData;
  if (!state)
    return 0;

  if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
    // Tab pressed
    if (state->show_autocomplete && !state->completions.empty()) {
      apply_completion(data, state);
    } else {
      update_completions(state, data->Buf, data->CursorPos);
    }
    return 0;
  }

  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
    // If autocomplete popup is open, use arrows to navigate it
    if (state->show_autocomplete && !state->completions.empty()) {
      if (data->EventKey == ImGuiKey_UpArrow) {
        if (state->autocomplete_idx > 0)
          state->autocomplete_idx--;
      } else if (data->EventKey == ImGuiKey_DownArrow) {
        if (state->autocomplete_idx < (int)state->completions.size() - 1)
          state->autocomplete_idx++;
      }
      return 0;
    }

    // Normal history navigation
    int history_size = (int)state->history.size();
    if (history_size == 0)
      return 0;

    if (state->history_pos == -1) {
      state->saved_input = std::string(data->Buf, data->BufTextLen);
    }

    if (data->EventKey == ImGuiKey_UpArrow) {
      if (state->history_pos == -1) {
        state->history_pos = history_size - 1;
      } else if (state->history_pos > 0) {
        state->history_pos--;
      }
    } else if (data->EventKey == ImGuiKey_DownArrow) {
      if (state->history_pos != -1) {
        state->history_pos++;
        if (state->history_pos >= history_size) {
          state->history_pos = -1;
        }
      }
    }

    const char *new_text = (state->history_pos == -1)
                               ? state->saved_input.c_str()
                               : state->history[state->history_pos].c_str();

    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, new_text);
    return 0;
  }

  if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
    // Text changed — update completions
    update_completions(state, data->Buf, data->CursorPos);
    return 0;
  }

  if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
    // Dismiss autocomplete on Escape
    if (state->show_autocomplete && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      state->show_autocomplete = false;
      state->completions.clear();
    }
    return 0;
  }

  return 0;
}

// Render the autocomplete popup
static void render_autocomplete_popup(repl_state_t *state, ImVec2 input_pos) {
  if (!state->show_autocomplete || state->completions.empty())
    return;

  ImGui::SetNextWindowPos(
      ImVec2(input_pos.x, input_pos.y + ImGui::GetTextLineHeightWithSpacing()));
  ImGui::SetNextWindowSize(ImVec2(200, 0)); // auto-height

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImVec4(0.118f, 0.137f, 0.161f, 0.95f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.188f, 0.212f, 0.239f, 1.0f));

  if (ImGui::Begin("##autocomplete", nullptr, flags)) {
    int max_show = 8;
    int count = (int)state->completions.size();
    int show = count < max_show ? count : max_show;

    for (int i = 0; i < show; i++) {
      bool selected = (i == state->autocomplete_idx);
      if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        // Highlight background
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size(ImGui::GetContentRegionAvail().x,
                    ImGui::GetTextLineHeightWithSpacing());
        ImGui::GetWindowDrawList()->AddRectFilled(
            p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(88, 166, 255, 60),
            3.0f);
      }
      ImGui::TextUnformatted(state->completions[i].c_str());
      if (selected) {
        ImGui::PopStyleColor();
      }
    }
    if (count > max_show) {
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(0.545f, 0.580f, 0.620f, 1.0f));
      ImGui::Text("  +%d more", count - max_show);
      ImGui::PopStyleColor();
    }
  }
  ImGui::End();

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

extern "C" {

nil_t rfui_repl_init(nil_t) {
  if (g_repl)
    return; // Already initialized

  g_repl = new repl_state_t();
  g_repl->input_buf[0] = '\0';
  g_repl->history_pos = -1;
  g_repl->scroll_to_bottom = true;
  g_repl->console_scroll_to_bottom = false;
  g_repl->focus_next_frame = false;
  g_repl->show_autocomplete = false;
  g_repl->autocomplete_idx = 0;
  g_repl->autocomplete_idx = 0;
  g_repl->completion_prefix_start = 0;
  g_repl->last_input_pos = ImVec2(0, 0);
}

nil_t rfui_repl_render(nil_t) {
  if (!g_repl)
    return;

  repl_state_t *state = g_repl;

  // Colors (matched to theme palette)
  ImVec4 prompt_color(0.247f, 0.725f, 0.314f, 1.0f); // #3FB950
  ImVec4 result_color(0.902f, 0.929f, 0.953f, 1.0f); // #E6EDF3
  ImVec4 error_color(0.973f, 0.318f, 0.286f, 1.0f);  // #F85149

  bool want_focus = ImGui::IsWindowAppearing() || state->scroll_to_bottom;
  bool mouse_clicked = ImGui::IsMouseClicked(0);

  // Use monospace font for REPL (font index 3 — DejaVu Sans Mono)
  ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[3]);

  // Tab bar: REPL + Console
  if (ImGui::BeginTabBar("##repl_tabs")) {
    if (ImGui::BeginTabItem("REPL")) {
      // Single scrollable region for entire terminal
      // Zero vertical item spacing so box-drawing chars (│╭╰) connect seamlessly
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));
      ImGui::BeginChild("##terminal", ImVec2(0, 0), false,
                        ImGuiWindowFlags_HorizontalScrollbar);

      // Display all previous lines (with ANSI escape sequence support)
      for (const terminal_line_t &line : state->lines) {
        ImVec4 base_color;
        switch (line.type) {
        case LINE_INPUT:
          base_color = prompt_color;
          break;
        case LINE_ERROR:
          base_color = error_color;
          break;
        default:
          base_color = result_color;
          break;
        }
        render_ansi_text(line.text.c_str(), base_color);
      }

      // Subtle separator between output and input
      if (!state->lines.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Separator,
                              ImVec4(0.188f, 0.212f, 0.239f, 1.0f));
        ImGui::Separator();
        ImGui::PopStyleColor();
      }

      // Current input line: prompt + input field on same line
      ImGui::PushStyleColor(ImGuiCol_Text, prompt_color);
      ImGui::TextUnformatted(ICON_PROMPT " ");
      ImGui::PopStyleColor();
      ImGui::SameLine(0, 0);

      // Make input field blend with terminal (no frame, no border, no
      // highlight) Text color is transparent — we overlay syntax-highlighted
      // text on top
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(0, 0, 0, 0)); // transparent text

      // Focus input: apply deferred focus from previous frame's click,
      // or immediate focus on window appear / scroll-to-bottom.
      if (want_focus || state->focus_next_frame) {
        ImGui::SetKeyboardFocusHere();
        state->focus_next_frame = false;
      }
      // Detect click this frame → set flag for NEXT frame (deferred,
      // because ImGui overrides focus when processing the child click).
      // Use ChildWindows (not RootAndChildWindows) so floating/docked
      // windows above the REPL don't get blocked from moving.
      if (mouse_clicked &&
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        state->focus_next_frame = true;
      }

      ImGui::SetNextItemWidth(-1);
      ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                  ImGuiInputTextFlags_CallbackHistory |
                                  ImGuiInputTextFlags_CallbackCompletion |
                                  ImGuiInputTextFlags_CallbackEdit |
                                  ImGuiInputTextFlags_CallbackAlways;

      bool enter_pressed = ImGui::InputText("##input", state->input_buf,
                                            sizeof(state->input_buf), flags,
                                            input_callback, state);

      // Record input position for autocomplete popup
      ImVec2 input_pos = ImGui::GetItemRectMin();

      // Overlay syntax-highlighted text on top of the invisible InputText
      if (state->input_buf[0] != '\0') {
        ImVec2 text_pos = ImGui::GetItemRectMin();
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImFont *font = ImGui::GetFont();
        float font_size = ImGui::GetFontSize();

        rfui_token_t tokens[512];
        int ntok = rfui_tokenize(state->input_buf, tokens, 512);

        for (int t = 0; t < ntok; t++) {
          const char *tok_start = state->input_buf + tokens[t].start;
          const char *tok_end = tok_start + tokens[t].len;
          ImVec4 color = rfui_token_color(tokens[t].type);

          // Compute X offset: measure text up to this token's start
          float x_off = 0.0f;
          if (tokens[t].start > 0) {
            x_off =
                font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, state->input_buf,
                                    state->input_buf + tokens[t].start)
                    .x;
          }

          draw_list->AddText(
              font, font_size, ImVec2(text_pos.x + x_off, text_pos.y),
              ImGui::ColorConvertFloat4ToU32(color), tok_start, tok_end);
        }
      }

      ImGui::PopStyleColor(6);
      ImGui::PopStyleVar(2);

      // Handle Enter — dismiss autocomplete and submit
      if (enter_pressed && state->input_buf[0] != '\0') {
        state->show_autocomplete = false;
        state->completions.clear();

        std::string input(state->input_buf);

        // Add to command history
        if (state->history.empty() || state->history.back() != input) {
          if ((int)state->history.size() >= MAX_HISTORY_SIZE) {
            state->history.erase(state->history.begin());
          }
          state->history.push_back(input);
        }

        // Add input line to terminal
        if ((int)state->lines.size() >= MAX_OUTPUT_LINES) {
          state->lines.erase(state->lines.begin());
        }
        state->lines.push_back(
            {std::string(ICON_PROMPT " ") + input, LINE_INPUT});

        // Evaluate
        rfui_eval(state->input_buf);

        // Clear
        state->input_buf[0] = '\0';
        state->history_pos = -1;
        state->saved_input.clear();
        state->scroll_to_bottom = true;
      }

      // Auto-scroll
      if (state->scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0f);
        state->scroll_to_bottom = false;
      }

      // Save input position for overlay phase
      state->last_input_pos = input_pos;

      ImGui::EndChild();
      ImGui::PopStyleVar(); // ItemSpacing

      // Autocomplete popup moved to rfui_repl_render_overlay()

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Console")) {
      // Optional clear button
      if (ImGui::SmallButton("Clear")) {
        state->console_lines.clear();
      }
      ImGui::Separator();

      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));
      ImGui::BeginChild("##console", ImVec2(0, 0), false,
                        ImGuiWindowFlags_HorizontalScrollbar);

      for (const console_line_t &line : state->console_lines) {
        ImVec4 color;
        switch (line.level) {
        case LOG_DEBUG:
          color = ImVec4(0.545f, 0.580f, 0.620f, 1.0f);
          break; // gray
        case LOG_WARN:
          color = ImVec4(0.941f, 0.769f, 0.290f, 1.0f);
          break; // yellow
        case LOG_ERROR:
          color = ImVec4(0.973f, 0.318f, 0.286f, 1.0f);
          break; // red
        default:
          color = ImVec4(0.902f, 0.929f, 0.953f, 1.0f);
          break; // white
        }
        render_ansi_text(line.text.c_str(), color);
      }

      if (state->console_scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0f);
        state->console_scroll_to_bottom = false;
      }

      ImGui::EndChild();
      ImGui::PopStyleVar(); // ItemSpacing
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::PopFont();
}

nil_t rfui_repl_render_overlay(nil_t) {
  if (!g_repl)
    return;
  render_autocomplete_popup(g_repl, g_repl->last_input_pos);
}

nil_t rfui_repl_add_result_text(const char *text) {
  if (!g_repl || !text)
    return;

  // Determine if error (starts with "!" or "error")
  LineType type = LINE_RESULT;
  if (text[0] == '!' || strncmp(text, "error", 5) == 0) {
    type = LINE_ERROR;
  }

  if ((int)g_repl->lines.size() >= MAX_OUTPUT_LINES) {
    g_repl->lines.erase(g_repl->lines.begin());
  }
  g_repl->lines.push_back({std::string(text), type});
  g_repl->scroll_to_bottom = true;
}

nil_t rfui_repl_add_result_obj(obj_p obj) {
  if (!g_repl || !obj || obj->type != TYPE_C8)
    return;
  const char *text = AS_C8(obj);
  i64_t len = obj->len;

  LineType type = LINE_RESULT;
  if (len > 0 &&
      (text[0] == '!' || (len >= 5 && memcmp(text, "error", 5) == 0))) {
    type = LINE_ERROR;
  }

  if ((int)g_repl->lines.size() >= MAX_OUTPUT_LINES) {
    g_repl->lines.erase(g_repl->lines.begin());
  }
  g_repl->lines.push_back({std::string(text, len), type});
  g_repl->scroll_to_bottom = true;
}

nil_t rfui_repl_add_log(const char *text, int level) {
  if (!g_repl || !text)
    return;

  LogLevel lvl = LOG_INFO;
  if (level <= 0)
    lvl = LOG_DEBUG;
  else if (level == 1)
    lvl = LOG_INFO;
  else if (level == 2)
    lvl = LOG_WARN;
  else
    lvl = LOG_ERROR;

  if ((int)g_repl->console_lines.size() >= MAX_CONSOLE_LINES) {
    g_repl->console_lines.erase(g_repl->console_lines.begin());
  }
  g_repl->console_lines.push_back({std::string(text), lvl});
  g_repl->console_scroll_to_bottom = true;
}

nil_t rfui_repl_load_file(const char *path) {
  if (!g_repl || !path)
    return;

  // Normalize backslashes to forward slashes for Rayfall
  char norm[4096];
  snprintf(norm, sizeof(norm), "%s", path);
  for (char *p = norm; *p; p++) {
    if (*p == '\\')
      *p = '/';
  }

  char expr[4096];
  snprintf(expr, sizeof(expr), "(load \"%s\")", norm);

  // Show in REPL history
  if ((int)g_repl->lines.size() >= MAX_OUTPUT_LINES) {
    g_repl->lines.erase(g_repl->lines.begin());
  }
  g_repl->lines.push_back({std::string(ICON_PROMPT " ") + expr, LINE_INPUT});
  g_repl->scroll_to_bottom = true;

  rfui_eval(expr);
}

nil_t rfui_repl_destroy(nil_t) {
  if (!g_repl)
    return;

  delete g_repl;
  g_repl = nullptr;
}

} // extern "C"
