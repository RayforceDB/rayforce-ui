// src/context.c
#include <stdlib.h>
#include "../include/rfui/context.h"

rfui_ctx_t* rfui_ctx_create(i32_t argc, str_p argv[]) {
    rfui_ctx_t* ctx = calloc(1, sizeof(rfui_ctx_t));
    if (!ctx) return NULL;

    // Store command line arguments (shallow copy)
    // NOTE: Caller must ensure argv remains valid for context lifetime
    ctx->argc = argc;
    ctx->argv = argv;

    // Create queues
    ctx->ui_to_ray = rfui_queue_create(RFUI_QUEUE_CAPACITY);
    if (!ctx->ui_to_ray) {
        free(ctx);
        return NULL;
    }

    ctx->ray_to_ui = rfui_queue_create(RFUI_QUEUE_CAPACITY);
    if (!ctx->ray_to_ui) {
        rfui_queue_destroy(ctx->ui_to_ray);
        free(ctx);
        return NULL;
    }

    // Initialize thread synchronization primitives
    ctx->ready_mutex = mutex_create();
    ctx->ready_cond = cond_create();

    // Initialize state
    ctx->ready = B8_FALSE;
    ctx->quit = B8_FALSE;
    ctx->waker = NULL;
    ctx->ui_vm_ctx = NULL;

    return ctx;
}

nil_t rfui_ctx_destroy(rfui_ctx_t* ctx) {
    if (!ctx) return;

    // Destroy synchronization primitives
    cond_destroy(&ctx->ready_cond);
    mutex_destroy(&ctx->ready_mutex);

    // Destroy queues
    rfui_queue_destroy(ctx->ui_to_ray);
    rfui_queue_destroy(ctx->ray_to_ui);

    // Note: waker is owned by poll, so we don't destroy it here

    free(ctx);
}

nil_t rfui_ctx_wait_ready(rfui_ctx_t* ctx) {
    if (!ctx) return;

    mutex_lock(&ctx->ready_mutex);
    while (!ctx->ready) {
        cond_wait(&ctx->ready_cond, &ctx->ready_mutex);
    }
    mutex_unlock(&ctx->ready_mutex);
}

nil_t rfui_ctx_signal_ready(rfui_ctx_t* ctx) {
    if (!ctx) return;

    mutex_lock(&ctx->ready_mutex);
    ctx->ready = B8_TRUE;
    cond_signal(&ctx->ready_cond);
    mutex_unlock(&ctx->ready_mutex);
}

nil_t rfui_ctx_set_quit(rfui_ctx_t* ctx, b8_t quit) {
    if (!ctx) return;

    mutex_lock(&ctx->ready_mutex);
    ctx->quit = quit;
    mutex_unlock(&ctx->ready_mutex);
}

b8_t rfui_ctx_get_quit(rfui_ctx_t* ctx) {
    if (!ctx) return B8_TRUE;  // Safe default: quit if ctx is invalid

    mutex_lock(&ctx->ready_mutex);
    b8_t quit = ctx->quit;
    mutex_unlock(&ctx->ready_mutex);
    return quit;
}

nil_t rfui_ctx_set_waker(rfui_ctx_t* ctx, poll_waker_p waker) {
    if (!ctx) return;

    mutex_lock(&ctx->ready_mutex);
    ctx->waker = waker;
    mutex_unlock(&ctx->ready_mutex);
}

poll_waker_p rfui_ctx_get_waker(rfui_ctx_t* ctx) {
    if (!ctx) return NULL;

    mutex_lock(&ctx->ready_mutex);
    poll_waker_p waker = ctx->waker;
    mutex_unlock(&ctx->ready_mutex);
    return waker;
}
