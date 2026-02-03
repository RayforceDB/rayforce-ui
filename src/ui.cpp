// src/ui.cpp
// ImGui/GLFW UI implementation for rayforce-ui

// Include C string functions before ImGui (ImGui uses memset, memcmp, strcmp)
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "ImGuiFileDialog.h"

#include "../include/rfui/theme.h"
#include "../include/rfui/logo.h"
#include "../include/rfui/icons.h"
#include "../include/rfui/version.h"
#include "embed_assets.h"

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>

// Make rayforce headers C++ compatible by redefining _Static_assert
// (rayforce uses C11 _Static_assert, C++ uses static_assert)
#define _Static_assert static_assert

// Include C headers for rayforce-ui
extern "C" {
#include "../include/rfui/ui.h"
#include "../include/rfui/context.h"
#include "../include/rfui/message.h"
#include "../include/rfui/queue.h"
#include "../include/rfui/widget_registry.h"
#include "../include/rfui/repl_renderer.h"
#include "../include/rfui/toast.h"
}

// Maximum messages to process per frame to avoid blocking rendering
#define MAX_MESSAGES_PER_FRAME 64

// GLFW error callback
static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Global state
static GLFWwindow* g_window = nullptr;
static const char* g_glsl_version = nullptr;
static bool g_initialized = false;
static char* g_ini_path = nullptr;  // ImGui layout persistence path

// Get config directory path, creating it if necessary
// Returns allocated string that caller must free, or nullptr on failure
static char* get_config_path(void) {
    const char* home = getenv("HOME");
    if (!home) return nullptr;

    // Use ~/.config/rfui/ as config directory
    const char* config_subdir = "/.config/rayforce-ui";
    const char* ini_filename = "/layout.ini";

    size_t dir_len = strlen(home) + strlen(config_subdir);
    size_t path_len = dir_len + strlen(ini_filename) + 1;

    // Create config directory if it doesn't exist
    char* dir_path = (char*)malloc(dir_len + 1);
    if (!dir_path) return nullptr;
    snprintf(dir_path, dir_len + 1, "%s%s", home, config_subdir);

    // Create ~/.config if needed
    char* config_dir = (char*)malloc(strlen(home) + strlen("/.config") + 1);
    if (config_dir) {
        snprintf(config_dir, strlen(home) + strlen("/.config") + 1, "%s/.config", home);
        mkdir(config_dir, 0755);  // Ignore error if exists
        free(config_dir);
    }

    // Create ~/.config/rayforce-ui if needed
    mkdir(dir_path, 0755);  // Ignore error if exists
    free(dir_path);

    // Return full path to ini file
    char* ini_path = (char*)malloc(path_len);
    if (!ini_path) return nullptr;
    snprintf(ini_path, path_len, "%s%s%s", home, config_subdir, ini_filename);

    return ini_path;
}

// External context (set by main.c)
extern "C" rfui_ctx_t* g_ctx;

extern "C" {

i32_t rfui_ui_init(nil_t) {
    if (g_initialized) {
        fprintf(stderr, "UI already initialized\n");
        return -1;
    }

    // Setup GLFW error callback
    glfwSetErrorCallback(glfw_error_callback);

    // Initialize GLFW
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    g_glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    g_glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    // GL 3.0 + GLSL 130
    g_glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    // Borderless main window — custom title bar replaces OS decoration
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    g_window = glfwCreateWindow(1280, 720, "Rayforce UI", nullptr, nullptr);
    if (g_window == nullptr) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1); // Enable vsync

    // Get HiDPI scale factor
    // On macOS Retina, content scale is 2.0 but GLFW already maps coordinates
    // to screen points — only fonts need the scale for sharp rendering.
    // On Windows/Linux, content scale reflects actual UI scaling (1.25, 1.5, etc.)
    // and both fonts and style need scaling.

    // Setup Dear ImGui and ImPlot contexts
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport

    // Set up persistent layout file (ImGui auto-saves on shutdown, auto-loads on startup)
    g_ini_path = get_config_path();
    if (g_ini_path) {
        io.IniFilename = g_ini_path;
    }
    // If get_config_path() fails, io.IniFilename remains "imgui.ini" (ImGui default)

    // Apply dashboard theme (replaces StyleColorsDark)
    rfui_theme_apply();

    // Load IBM Plex Sans Bold + FontAwesome as the main UI font (index 0)
    float font_size = 30.0f;
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)embed_IBMPlexSans_Bold_ttf, embed_IBMPlexSans_Bold_ttf_len, font_size, &font_cfg);

    static const ImWchar icon_ranges[] = { 0xf000, 0xf8ff, 0 };
    float icon_size = font_size * 0.75f;
    ImFontConfig icon_cfg;
    icon_cfg.MergeMode = true;
    icon_cfg.PixelSnapH = true;
    icon_cfg.GlyphMinAdvanceX = icon_size;
    icon_cfg.GlyphOffset.y = 2.0f;
    icon_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)embed_fa_solid_900_otf, embed_fa_solid_900_otf_len, icon_size, &icon_cfg, icon_ranges);

    // Large IBM Plex Sans for text/label widgets (index 1)
    float large_font_size = 64.0f;
    ImFontConfig large_cfg;
    large_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)embed_IBMPlexSans_Bold_ttf, embed_IBMPlexSans_Bold_ttf_len, large_font_size, &large_cfg);

    // Regular IBM Plex Sans for grid data cells (index 2)
    ImFontConfig regular_cfg;
    regular_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)embed_IBMPlexSans_Regular_ttf, embed_IBMPlexSans_Regular_ttf_len, font_size, &regular_cfg);

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init(g_glsl_version);

    // Load background logo and window icon from embedded data
    rfui_logo_init();
    rfui_icon_init(g_window);

    // Initialize widget registry
    rfui_registry_init();

    // Initialize REPL (renders directly in main window)
    rfui_repl_init();

    g_initialized = true;
    return 0;
}

i32_t rfui_ui_run(nil_t) {
    if (!g_initialized || !g_window) {
        fprintf(stderr, "UI not initialized\n");
        return -1;
    }

    if (!g_ctx) {
        fprintf(stderr, "Context not set\n");
        return -1;
    }

    ImVec4 clear_color = ImVec4(0.051f, 0.067f, 0.090f, 1.0f);

    // Main loop
    while (!glfwWindowShouldClose(g_window) && !rfui_ctx_get_quit(g_ctx)) {
        // Poll events and use timeout to avoid busy-waiting
        // Note: We always poll first, then process messages
        glfwWaitEventsTimeout(0.016); // ~60fps timeout

        bool main_minimized = glfwGetWindowAttrib(g_window, GLFW_ICONIFIED) != 0;

        // Process messages from ray_to_ui queue (limited per frame)
        // Note: rfui_queue_pop returns NULL if queue is empty or NULL,
        // so we don't need a separate empty check (avoids TOCTOU race)
        int messages_processed = 0;
        rfui_ray_msg_t* msg;
        if (g_ctx->ray_to_ui != nullptr) {
            while (messages_processed < MAX_MESSAGES_PER_FRAME &&
                   (msg = (rfui_ray_msg_t*)rfui_queue_pop(g_ctx->ray_to_ui)) != nullptr) {
                // TODO: Process message based on type
                // For now: just free message resources
                switch (msg->type) {
                    case RFUI_MSG_WIDGET_CREATED:
                        // Register widget in UI
                        if (msg->widget) {
                            rfui_registry_add(msg->widget);
                        }
                        break;
                    case RFUI_MSG_DRAW:
                        // Update widget render_data and queue old data for drop
                        if (msg->widget) {
                            // Swap overlay front/back buffers so UI reads newly built overlays
                            msg->widget->overlay_front = 1 - msg->widget->overlay_front;

                            obj_p old_data = rfui_registry_update_data(msg->widget, msg->data);
                            // Queue old data for drop in Rayforce thread (if not NULL)
                            if (old_data) {
                                rfui_ui_msg_t* drop_msg = (rfui_ui_msg_t*)malloc(sizeof(rfui_ui_msg_t));
                                if (drop_msg) {
                                    drop_msg->type = RFUI_MSG_DROP;
                                    drop_msg->obj = old_data;
                                    drop_msg->widget = nullptr;
                                    drop_msg->expr = nullptr;
                                    if (!rfui_queue_push(g_ctx->ui_to_ray, drop_msg)) {
                                        // Queue full - leak rather than crash
                                        // (drop_obj requires Rayforce thread runtime)
                                        free(drop_msg);
                                    } else {
                                        poll_waker_p waker = rfui_ctx_get_waker(g_ctx);
                                        if (waker) poll_waker_wake(waker);
                                    }
                                } else {
                                    // Malloc failed - leak (drop_obj requires Rayforce thread)
                                    (void)old_data;
                                }
                            }
                        } else if (msg->data) {
                            // No widget - queue data for drop directly
                            rfui_ui_msg_t* drop_msg = (rfui_ui_msg_t*)malloc(sizeof(rfui_ui_msg_t));
                            if (drop_msg) {
                                drop_msg->type = RFUI_MSG_DROP;
                                drop_msg->obj = msg->data;
                                drop_msg->widget = nullptr;
                                drop_msg->expr = nullptr;
                                if (!rfui_queue_push(g_ctx->ui_to_ray, drop_msg)) {
                                    // Queue push failed - fall back to direct drop
                                    // Leak (drop_obj requires Rayforce thread)
                                        (void)msg->data;
                                    free(drop_msg);
                                } else {
                                    poll_waker_p waker = rfui_ctx_get_waker(g_ctx);
                                    if (waker) poll_waker_wake(waker);
                                }
                            } else {
                                // Malloc failed - leak (drop_obj requires Rayforce thread)
                                (void)msg->data;
                            }
                        }
                        break;
                    case RFUI_MSG_ALERT:
                        // Toast notification — data is obj_p (TYPE_C8)
                        if (msg->data) {
                            rfui_toast_push_obj(msg->data);
                            // Queue obj_p for drop on Rayforce thread
                            rfui_ui_msg_t* drop_msg = (rfui_ui_msg_t*)malloc(sizeof(rfui_ui_msg_t));
                            if (drop_msg) {
                                drop_msg->type = RFUI_MSG_DROP;
                                drop_msg->obj = msg->data;
                                drop_msg->widget = nullptr;
                                drop_msg->expr = nullptr;
                                if (!rfui_queue_push(g_ctx->ui_to_ray, drop_msg)) {
                                    (void)msg->data;
                                    free(drop_msg);
                                } else {
                                    poll_waker_p waker = rfui_ctx_get_waker(g_ctx);
                                    if (waker) poll_waker_wake(waker);
                                }
                            }
                            msg->data = nullptr; // prevent double-drop
                        }
                        break;
                    case RFUI_MSG_RESULT:
                        // Display result in REPL — text is in msg->data (obj_p TYPE_C8)
                        if (msg->data) {
                            rfui_repl_add_result_obj(msg->data);
                        }
                        // Queue data for drop
                        if (msg->data) {
                            rfui_ui_msg_t* drop_msg = (rfui_ui_msg_t*)malloc(sizeof(rfui_ui_msg_t));
                            if (drop_msg) {
                                drop_msg->type = RFUI_MSG_DROP;
                                drop_msg->obj = msg->data;
                                drop_msg->widget = nullptr;
                                drop_msg->expr = nullptr;
                                if (!rfui_queue_push(g_ctx->ui_to_ray, drop_msg)) {
                                    // Queue push failed - fall back to direct drop
                                    // Leak (drop_obj requires Rayforce thread)
                                        (void)msg->data;
                                    free(drop_msg);
                                } else {
                                    poll_waker_p waker = rfui_ctx_get_waker(g_ctx);
                                    if (waker) poll_waker_wake(waker);
                                }
                            } else {
                                // Malloc failed - leak (drop_obj requires Rayforce thread)
                                (void)msg->data;
                            }
                        }
                        break;
                }

                free(msg);
                messages_processed++;
            }
        }

        // Single ImGui frame — viewports handle multi-window
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Logo watermark behind content
        rfui_logo_render();

        // Main window: custom title bar + REPL content
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->Pos);
            ImGui::SetNextWindowSize(vp->Size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.051f, 0.067f, 0.090f, 0.85f));
            ImGui::Begin("##repl", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar);

            // --- Custom title bar ---
            float title_h = ImGui::GetFrameHeight() + 6.0f;
            ImVec2 title_min = ImGui::GetCursorScreenPos();
            ImVec2 title_max(title_min.x + vp->Size.x, title_min.y + title_h);

            // Title bar background
            ImGui::GetWindowDrawList()->AddRectFilled(
                title_min, title_max,
                IM_COL32(22, 27, 34, 255));  // COL_SURFACE #161B22

            // App title (left side) — bolt in default font, text in Orbitron
            ImGui::SetCursorScreenPos(ImVec2(title_min.x + 12.0f,
                                              title_min.y + (title_h - ImGui::GetFontSize()) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.914f, 0.627f, 0.200f, 1.0f));
            ImGui::TextUnformatted("\xe2\x9a\xa1");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.545f, 0.580f, 0.620f, 1.0f));
            ImGui::TextUnformatted("Rayforce v" RFUI_VERSION);
            ImGui::PopStyleColor();

            // Title bar buttons: open | minimize | maximize | close
            float btn_w = title_h * 1.4f;
            float btn_x = title_max.x - btn_w * 4;
            bool maximized = glfwGetWindowAttrib(g_window, GLFW_MAXIMIZED) != 0;

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.545f, 0.580f, 0.620f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

            // Open script
            ImGui::SetCursorScreenPos(ImVec2(btn_x, title_min.y));
            if (ImGui::Button(ICON_FOLDER_OPEN "##open", ImVec2(btn_w, title_h))) {
                IGFD::FileDialogConfig config;
                config.path = "examples";
                ImGuiFileDialog::Instance()->OpenDialog("LoadScript", "Open Script", ".rfl", config);
            }

            // Minimize
            ImGui::SetCursorScreenPos(ImVec2(btn_x + btn_w, title_min.y));
            if (ImGui::Button(ICON_MINIMIZE "##min", ImVec2(btn_w, title_h))) {
                glfwIconifyWindow(g_window);
            }

            // Maximize / Restore
            ImGui::SetCursorScreenPos(ImVec2(btn_x + btn_w * 2, title_min.y));
            if (ImGui::Button(maximized ? ICON_RESTORE "##max" : ICON_MAXIMIZE "##max",
                              ImVec2(btn_w, title_h))) {
                if (maximized) glfwRestoreWindow(g_window);
                else glfwMaximizeWindow(g_window);
            }

            // Close (red on hover)
            ImGui::PopStyleColor(2);  // pop ButtonHovered, ButtonActive
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.973f, 0.318f, 0.286f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.973f, 0.318f, 0.286f, 0.8f));
            ImGui::SetCursorScreenPos(ImVec2(btn_x + btn_w * 3, title_min.y));
            if (ImGui::Button(ICON_CLOSE "##close", ImVec2(btn_w, title_h))) {
                glfwSetWindowShouldClose(g_window, GLFW_TRUE);
            }
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);

            // Title bar drag-to-move (only in the non-button area)
            ImGui::SetCursorScreenPos(title_min);
            ImGui::InvisibleButton("##titlebar_drag", ImVec2(btn_x - title_min.x, title_h));
            // Double-click to maximize/restore
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                if (maximized) glfwRestoreWindow(g_window);
                else glfwMaximizeWindow(g_window);
            }
            // Drag to move — use screen-absolute mouse position to avoid trembling
            static bool dragging = false;
            static int drag_screen_start_x = 0, drag_screen_start_y = 0;
            static int win_start_x = 0, win_start_y = 0;
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                // Compute screen-absolute cursor position
                double local_cx, local_cy;
                glfwGetCursorPos(g_window, &local_cx, &local_cy);
                int wx, wy;
                glfwGetWindowPos(g_window, &wx, &wy);
                int screen_cx = wx + (int)local_cx;
                int screen_cy = wy + (int)local_cy;

                if (!dragging) {
                    dragging = true;
                    drag_screen_start_x = screen_cx;
                    drag_screen_start_y = screen_cy;
                    win_start_x = wx;
                    win_start_y = wy;
                    // Un-maximize on drag start
                    if (maximized) {
                        glfwRestoreWindow(g_window);
                        int new_w, new_h;
                        glfwGetWindowSize(g_window, &new_w, &new_h);
                        win_start_x = screen_cx - new_w / 2;
                        win_start_y = screen_cy - (int)(title_h * 0.5f);
                        glfwSetWindowPos(g_window, win_start_x, win_start_y);
                        drag_screen_start_x = screen_cx;
                        drag_screen_start_y = screen_cy;
                    }
                }
                glfwSetWindowPos(g_window,
                                 win_start_x + (screen_cx - drag_screen_start_x),
                                 win_start_y + (screen_cy - drag_screen_start_y));
            } else {
                dragging = false;
            }

            // Separator line below title bar
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(title_min.x, title_max.y),
                ImVec2(title_max.x, title_max.y),
                IM_COL32(48, 54, 61, 255));  // COL_BORDER #30363D

            // --- File dialog ---
            if (ImGuiFileDialog::Instance()->Display("LoadScript",
                    ImGuiWindowFlags_NoCollapse, ImVec2(800, 500))) {
                if (ImGuiFileDialog::Instance()->IsOk()) {
                    std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
                    rfui_repl_load_file(path.c_str());
                }
                ImGuiFileDialog::Instance()->Close();
            }

            // --- REPL content below title bar ---
            ImGui::SetCursorScreenPos(ImVec2(title_min.x + 8.0f, title_max.y + 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
            rfui_repl_render();
            ImGui::PopStyleVar();

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

        // Render all widgets (each opens its own ImGui window / viewport)
        rfui_registry_render();

        // Toast overlay (rendered on top of widgets)
        rfui_toast_render();

        // Render
        ImGui::Render();

        // Draw main window (skip GL draw if minimized)
        if (!main_minimized) {
            int display_w, display_h;
            glfwGetFramebufferSize(g_window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                         clear_color.z * clear_color.w, clear_color.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(g_window);
        }

        // Multi-viewport: update and render platform windows (always, even if main minimized)
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_ctx = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_ctx);
        }
    }

    return 0;
}

nil_t rfui_ui_destroy(nil_t) {
    if (!g_initialized) {
        return;
    }

    // Destroy REPL state
    rfui_repl_destroy();

    // Destroy logo texture
    rfui_logo_destroy();

    // Destroy toast system
    rfui_toast_destroy();

    // Destroy widget registry (frees all widgets)
    rfui_registry_destroy();

    // Cleanup ImGui and ImPlot
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    // Free ini path (after DestroyContext, which saves the layout)
    if (g_ini_path) {
        free(g_ini_path);
        g_ini_path = nullptr;
    }

    // Cleanup GLFW
    if (g_window) {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    glfwTerminate();

    g_initialized = false;
}

b8_t rfui_ui_should_run(nil_t) {
    if (!g_initialized || !g_window) {
        return B8_FALSE;
    }
    return glfwWindowShouldClose(g_window) ? B8_FALSE : B8_TRUE;
}

nil_t rfui_ui_wake(nil_t) {
    glfwPostEmptyEvent();
}

} // extern "C"
