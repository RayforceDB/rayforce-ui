// src/widget.c
#include "../include/rfui/widget.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// strdup is POSIX, not C standard - provide explicit declaration
extern char* strdup(const char* s);

// Buffer size for widget formatting
#define WIDGET_FORMAT_BUF_SIZE 256

const char* rfui_widget_type_name(rfui_widget_type_t type) {
    switch (type) {
        case RFUI_WIDGET_GRID:  return "grid";
        case RFUI_WIDGET_CHART: return "chart";
        case RFUI_WIDGET_TEXT:  return "text";
        case RFUI_WIDGET_ALERT: return "alert";
        default: return "unknown";
    }
}

rfui_widget_t* rfui_widget_create(rfui_widget_type_t type, const char* name) {
    if (!name) return NULL;

    rfui_widget_t* w = malloc(sizeof(rfui_widget_t));
    if (!w) return NULL;

    w->name = strdup(name);
    if (!w->name) {
        free(w);
        return NULL;
    }

    w->type = type;
    w->data = NULL;
    w->post_query = NULL;
    w->on_select = NULL;
    w->is_open = B8_TRUE;
    w->dock_id = 0;
    w->ui_state = NULL;
    w->render_data = NULL;
    w->num_rules = 0;
    w->num_overlays[0] = 0;
    w->num_overlays[1] = 0;
    w->overlay_front = 0;

    return w;
}

nil_t rfui_widget_destroy(rfui_widget_t* w) {
    if (!w) return;

    free(w->name);
    if (w->data) drop_obj(w->data);
    if (w->post_query) drop_obj(w->post_query);
    if (w->on_select) drop_obj(w->on_select);
    if (w->render_data) drop_obj(w->render_data);
    for (int i = 0; i < w->num_rules; i++) {
        if (w->rules[i].fn) drop_obj(w->rules[i].fn);
    }
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < w->num_overlays[b]; i++) {
            if (w->overlays[b][i].mask) drop_obj(w->overlays[b][i].mask);
        }
    }
    free(w->ui_state);
    free(w);
}

char* rfui_widget_format(rfui_widget_t* w) {
    if (!w) return NULL;

    char* buf = malloc(WIDGET_FORMAT_BUF_SIZE);
    if (!buf) return NULL;

    snprintf(buf, WIDGET_FORMAT_BUF_SIZE, "widget<%s:\"%s\">",
             rfui_widget_type_name(w->type), w->name);
    return buf;
}
