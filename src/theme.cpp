// src/theme.cpp
// Professional analytical dashboard theme (Bloomberg/Grafana-inspired)

#include "imgui.h"
#include "implot.h"

extern "C" {
#include "../include/rfui/theme.h"
}

// Color palette
#define COL_BG ImVec4(0.051f, 0.067f, 0.090f, 1.0f)       // #0D1117
#define COL_SURFACE ImVec4(0.086f, 0.106f, 0.133f, 1.0f)  // #161B22
#define COL_BORDER ImVec4(0.188f, 0.212f, 0.239f, 1.0f)   // #30363D
#define COL_TEXT ImVec4(0.902f, 0.929f, 0.953f, 1.0f)     // #E6EDF3
#define COL_TEXT_DIM ImVec4(0.545f, 0.580f, 0.620f, 1.0f) // #8B949E
#define COL_ACCENT ImVec4(0.914f, 0.627f, 0.200f, 1.0f) // #E9A033 (brand gold)
#define COL_ACCENT_HVR ImVec4(0.941f, 0.722f, 0.353f, 1.0f) // #F0B85A
#define COL_GREEN ImVec4(0.247f, 0.725f, 0.314f, 1.0f)      // #3FB950
#define COL_RED ImVec4(0.973f, 0.318f, 0.286f, 1.0f)        // #F85149
#define COL_ORANGE ImVec4(0.824f, 0.600f, 0.133f, 1.0f)     // #D29922

// Derived colors
#define COL_HEADER_BG ImVec4(0.110f, 0.129f, 0.157f, 1.0f) // #1C2128
#define COL_SEL_ROW ImVec4(0.914f, 0.627f, 0.200f, 0.20f)  // #E9A03333
#define COL_GRID_LINE ImVec4(0.129f, 0.149f, 0.176f, 1.0f) // #21262D

extern "C" {

void rfui_theme_apply(void) {
  // --- ImGui Style Vars ---
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.FrameRounding = 0.0f;
  style.GrabRounding = 0.0f;
  style.PopupRounding = 0.0f;
  style.ScrollbarRounding = 0.0f;
  style.TabRounding = 0.0f;
  style.WindowBorderSize = 3.0f;
  style.FrameBorderSize = 1.0f;
  style.FramePadding = ImVec2(8, 4);
  style.ItemSpacing = ImVec2(8, 4);
  style.ItemInnerSpacing = ImVec2(4, 4);
  style.WindowPadding = ImVec2(8, 8);
  style.ScrollbarSize = 10.0f;
  style.GrabMinSize = 10.0f;
  style.TabBarBorderSize = 0.0f;
  style.TabBorderSize = 1.0f;
  style.TabBarOverlineSize = 0.0f;
  style.DockingSeparatorSize = 1.0f;
  style.WindowMinSize = ImVec2(300, 200);

  // --- ImGui Colors ---
  ImVec4 *c = style.Colors;

  c[ImGuiCol_Text] = COL_TEXT;
  c[ImGuiCol_TextDisabled] = COL_TEXT_DIM;
  c[ImGuiCol_WindowBg] = COL_BG;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = COL_SURFACE;
  c[ImGuiCol_Border] = COL_BORDER;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.13f, 0.11f, 1.0f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.14f, 0.10f, 1.0f);
  c[ImGuiCol_TitleBg] = COL_BG;
  c[ImGuiCol_TitleBgActive] = COL_SURFACE;
  c[ImGuiCol_TitleBgCollapsed] = COL_BG;
  c[ImGuiCol_MenuBarBg] = COL_SURFACE;
  c[ImGuiCol_ScrollbarBg] = COL_BG;
  c[ImGuiCol_ScrollbarGrab] = COL_BORDER;
  c[ImGuiCol_ScrollbarGrabHovered] = COL_TEXT_DIM;
  c[ImGuiCol_ScrollbarGrabActive] = COL_TEXT;
  c[ImGuiCol_CheckMark] = COL_ACCENT;
  c[ImGuiCol_SliderGrab] = ImVec4(0.70f, 0.50f, 0.18f, 1.0f);
  c[ImGuiCol_SliderGrabActive] = COL_ACCENT;
  c[ImGuiCol_Button] = ImVec4(0.18f, 0.14f, 0.08f, 1.0f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.20f, 0.10f, 1.0f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.24f, 0.12f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.18f, 0.14f, 0.08f, 1.0f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.20f, 0.10f, 1.0f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.24f, 0.12f, 1.0f);
  c[ImGuiCol_Separator] = COL_BORDER;
  c[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.30f, 0.15f, 1.0f);
  c[ImGuiCol_SeparatorActive] = ImVec4(0.70f, 0.50f, 0.18f, 1.0f);
  c[ImGuiCol_ResizeGrip] = COL_BORDER;
  c[ImGuiCol_ResizeGripHovered] = COL_ACCENT;
  c[ImGuiCol_ResizeGripActive] = COL_ACCENT_HVR;
  c[ImGuiCol_Tab] = ImVec4(0.13f, 0.11f, 0.09f, 1.0f);
  c[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.17f, 0.10f, 1.0f);
  c[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.17f, 0.10f, 1.0f);
  c[ImGuiCol_TabSelectedOverline] = COL_ACCENT;
  c[ImGuiCol_TabDimmed] = COL_BG;
  c[ImGuiCol_TabDimmedSelected] = ImVec4(0.14f, 0.12f, 0.10f, 1.0f);
  c[ImGuiCol_DockingPreview] = COL_ACCENT;
  c[ImGuiCol_DockingEmptyBg] = COL_BG;
  c[ImGuiCol_PlotLines] = COL_ACCENT;
  c[ImGuiCol_PlotLinesHovered] = COL_ACCENT_HVR;
  c[ImGuiCol_PlotHistogram] = COL_GREEN;
  c[ImGuiCol_PlotHistogramHovered] = COL_ACCENT_HVR;
  c[ImGuiCol_TableHeaderBg] = COL_HEADER_BG;
  c[ImGuiCol_TableBorderStrong] = COL_BORDER;
  c[ImGuiCol_TableBorderLight] = COL_GRID_LINE;
  c[ImGuiCol_TableRowBg] = COL_BG;
  c[ImGuiCol_TableRowBgAlt] = COL_SURFACE;
  c[ImGuiCol_TextSelectedBg] = COL_SEL_ROW;
  c[ImGuiCol_DragDropTarget] = COL_ACCENT;
  c[ImGuiCol_NavHighlight] = COL_ACCENT;
  c[ImGuiCol_NavWindowingHighlight] = COL_ACCENT;
  c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.5f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);

  // --- ImPlot Theme ---
  ImPlotStyle &pstyle = ImPlot::GetStyle();
  pstyle.PlotBorderSize = 0.0f;
  pstyle.MinorAlpha = 0.15f;
  pstyle.FillAlpha = 0.35f;
  pstyle.PlotPadding = ImVec2(8, 8);
  pstyle.LabelPadding = ImVec2(4, 4);

  ImVec4 *pc = pstyle.Colors;
  pc[ImPlotCol_PlotBg] = COL_BG;
  pc[ImPlotCol_PlotBorder] = COL_BORDER;
  pc[ImPlotCol_LegendBg] = COL_SURFACE;
  pc[ImPlotCol_LegendBorder] = COL_BORDER;
  pc[ImPlotCol_LegendText] = COL_TEXT;
  pc[ImPlotCol_TitleText] = COL_TEXT;
  pc[ImPlotCol_InlayText] = COL_TEXT;
  pc[ImPlotCol_FrameBg] = COL_BG;
  pc[ImPlotCol_AxisText] = COL_TEXT_DIM;
  pc[ImPlotCol_AxisGrid] = COL_GRID_LINE;
  pc[ImPlotCol_Selection] = COL_ACCENT;
  pc[ImPlotCol_Crosshairs] = COL_TEXT_DIM;

  // Custom colormap for chart series
  static const ImVec4 colormap[] = {
      ImVec4(0.345f, 0.651f, 1.000f, 1.0f), // #58A6FF
      ImVec4(0.247f, 0.725f, 0.314f, 1.0f), // #3FB950
      ImVec4(0.824f, 0.600f, 0.133f, 1.0f), // #D29922
      ImVec4(0.973f, 0.318f, 0.286f, 1.0f), // #F85149
      ImVec4(0.737f, 0.549f, 1.000f, 1.0f), // #BC8CFF
      ImVec4(0.224f, 0.824f, 0.753f, 1.0f), // #39D2C0
      ImVec4(1.000f, 0.482f, 0.447f, 1.0f), // #FF7B72
      ImVec4(0.475f, 0.753f, 1.000f, 1.0f), // #79C0FF
  };
  ImPlotColormap cmap = ImPlot::AddColormap("rayforce-ui", colormap, 8);
  pstyle.Colormap = cmap;
}

} // extern "C"
