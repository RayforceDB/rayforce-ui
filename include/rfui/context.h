// include/rfui/context.h
#ifndef RFUI_CONTEXT_H
#define RFUI_CONTEXT_H

#include "../../deps/rayforce/core/rayforce.h"
#include "../../deps/rayforce/core/poll.h"
#include "../../deps/rayforce/core/thread.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default queue capacity for UI<->Rayforce communication
#define RFUI_QUEUE_CAPACITY 1024

// Forward declare ray_ctx_p (defined in deps/rayforce/core/ctx.h)
typedef struct ray_ctx_t *ray_ctx_p;

typedef struct rfui_ctx_t {
    // Command line args (for runtime_create)
    // NOTE: argv is a shallow copy - caller must ensure argv remains valid
    // for the entire lifetime of this context
    i32_t argc;
    str_p* argv;

    // Queues
    rfui_queue_p ui_to_ray;   // UI thread -> Rayforce thread
    rfui_queue_p ray_to_ui;   // Rayforce thread -> UI thread

    // Thread sync - protects ready, quit, and waker fields
    mutex_t ready_mutex;
    cond_t ready_cond;
    b8_t ready;                  // Rayforce thread signals when ready

    // Rayforce poll waker (set by Rayforce thread)
    // Access must be protected by ready_mutex
    poll_waker_p waker;

    // Exit flag - access must be protected by ready_mutex
    b8_t quit;

    // UI-thread Rayforce VM context (created after Rayforce thread ready)
    // Enables direct drop_obj() calls on UI thread
    ray_ctx_p ui_vm_ctx;
} rfui_ctx_t;

// Create a new context with command line arguments
// NOTE: argv is stored as a shallow copy - caller must ensure argv remains
// valid for the entire lifetime of this context (typically program lifetime)
rfui_ctx_t* rfui_ctx_create(i32_t argc, str_p argv[]);

// Destroy context and free all resources
nil_t rfui_ctx_destroy(rfui_ctx_t* ctx);

// Wait for Rayforce thread to signal ready (called by UI thread)
nil_t rfui_ctx_wait_ready(rfui_ctx_t* ctx);

// Signal that Rayforce thread is ready (called by Rayforce thread)
nil_t rfui_ctx_signal_ready(rfui_ctx_t* ctx);

// Thread-safe quit flag accessors
nil_t rfui_ctx_set_quit(rfui_ctx_t* ctx, b8_t quit);
b8_t rfui_ctx_get_quit(rfui_ctx_t* ctx);

// Thread-safe waker accessors
nil_t rfui_ctx_set_waker(rfui_ctx_t* ctx, poll_waker_p waker);
poll_waker_p rfui_ctx_get_waker(rfui_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // RFUI_CONTEXT_H
