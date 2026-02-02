// src/logo.cpp
// Background logo watermark using nanosvg + OpenGL texture

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "imgui.h"
#include <GLFW/glfw3.h>

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#else
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#define NANOSVG_IMPLEMENTATION
#include "../deps/nanosvg/src/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../deps/nanosvg/src/nanosvgrast.h"

#include "embed_assets.h"

extern "C" {
#include "../include/rfui/logo.h"
}

static GLuint g_logo_texture = 0;
static int g_logo_w = 0;
static int g_logo_h = 0;

extern "C" {

int rfui_logo_init(void) {
    char* svg_copy = strdup((const char*)embed_logo_svg);
    if (!svg_copy) return -1;
    NSVGimage* image = nsvgParse(svg_copy, "px", 96.0f);
    free(svg_copy);
    if (!image) {
        fprintf(stderr, "logo: failed to parse embedded logo SVG\n");
        return -1;
    }

    // Rasterize at a reasonable size (max 512px wide for a watermark)
    float scale = 512.0f / image->width;
    g_logo_w = (int)(image->width * scale);
    g_logo_h = (int)(image->height * scale);

    unsigned char* pixels = (unsigned char*)malloc(g_logo_w * g_logo_h * 4);
    if (!pixels) {
        nsvgDelete(image);
        return -1;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        free(pixels);
        nsvgDelete(image);
        return -1;
    }

    nsvgRasterize(rast, image, 0, 0, scale, pixels, g_logo_w, g_logo_h, g_logo_w * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    // Create OpenGL texture
    glGenTextures(1, &g_logo_texture);
    glBindTexture(GL_TEXTURE_2D, g_logo_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_logo_w, g_logo_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    free(pixels);
    return 0;
}

void rfui_logo_render(void) {
    if (g_logo_texture == 0) return;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center = vp->GetCenter();

    // Scale logo to fit ~40% of viewport width, maintaining aspect ratio
    float target_w = vp->Size.x * 0.4f;
    float scale = target_w / (float)g_logo_w;
    float w = (float)g_logo_w * scale;
    float h = (float)g_logo_h * scale;

    ImVec2 p0(center.x - w * 0.5f, center.y - h * 0.5f);
    ImVec2 p1(center.x + w * 0.5f, center.y + h * 0.5f);

    // Subtle watermark effect
    bg->AddImage((ImTextureID)(intptr_t)g_logo_texture, p0, p1,
                 ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 100));
}

void rfui_logo_destroy(void) {
    if (g_logo_texture) {
        glDeleteTextures(1, &g_logo_texture);
        g_logo_texture = 0;
    }
}

unsigned int rfui_logo_texture(void) {
    return g_logo_texture;
}

void rfui_logo_size(int* w, int* h) {
    if (w) *w = g_logo_w;
    if (h) *h = g_logo_h;
}

int rfui_icon_init(void* glfw_window) {
    char* svg_copy = strdup((const char*)embed_icon_svg);
    if (!svg_copy) return -1;
    NSVGimage* image = nsvgParse(svg_copy, "px", 96.0f);
    free(svg_copy);
    if (!image) {
        fprintf(stderr, "icon: failed to parse embedded icon SVG\n");
        return -1;
    }

    // Rasterize at standard icon sizes
    int sizes[] = {64, 32, 16};
    GLFWimage icons[3];
    unsigned char* buffers[3] = {};

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        return -1;
    }

    int count = 0;
    for (int s = 0; s < 3; s++) {
        int sz = sizes[s];
        float scale = (float)sz / image->width;
        unsigned char* px = (unsigned char*)malloc(sz * sz * 4);
        if (!px) continue;
        nsvgRasterize(rast, image, 0, 0, scale, px, sz, sz, sz * 4);
        icons[count].width = sz;
        icons[count].height = sz;
        icons[count].pixels = px;
        buffers[count] = px;
        count++;
    }

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    if (count > 0) {
        glfwSetWindowIcon((GLFWwindow*)glfw_window, count, icons);
    }

    for (int i = 0; i < count; i++) {
        free(buffers[i]);
    }

    return count > 0 ? 0 : -1;
}

} // extern "C"
