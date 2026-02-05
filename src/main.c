// src/main.c
// Note: def.h is force-included via -include flag
#include "../include/rfui/rfui.h"
#include "../include/rfui/context.h"
#include "../include/rfui/message.h"
#include "../include/rfui/queue.h"
#include "../include/rfui/rayforce_thread.h"
#include "../include/rfui/ui.h"
#include "../deps/rayforce/core/thread.h"
#include "../deps/rayforce/core/ctx.h"  // For ray_ctx_create/destroy
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#if defined(_WIN32) && defined(DEBUG)
#include <windows.h>
#include <dbghelp.h>
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    fprintf(stderr, "CRASH at address: 0x%p, code: 0x%lx\n",
            ep->ExceptionRecord->ExceptionAddress,
            ep->ExceptionRecord->ExceptionCode);
    fprintf(stderr, "crash_handler at: %p\n", (raw_p)crash_handler);
    void* stack[32];
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);
    WORD frames = CaptureStackBackTrace(0, 32, stack, NULL);
    for (WORD i = 0; i < frames; i++) {
        DWORD64 addr = (DWORD64)stack[i];
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        if (SymFromAddr(process, addr, &disp, sym))
            fprintf(stderr, "  [%d] %s + 0x%llx\n", i, sym->Name, (unsigned long long)disp);
        else
            fprintf(stderr, "  [%d] 0x%p\n", i, stack[i]);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// Helper to duplicate string (portable version of strdup)
static char* rfui_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* dup = malloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

// Global state (g_ctx is non-static so ui.cpp can access it)
rfui_ctx_t* g_ctx = NULL;
static ray_thread_t g_ray_thread;

i32_t rfui_init(i32_t argc, str_p argv[]) {
    // Create context with command line arguments
    g_ctx = rfui_ctx_create(argc, argv);
    if (!g_ctx) {
        fprintf(stderr, "Failed to create rayforce-ui context\n");
        return -1;
    }

    // Initialize UI (GLFW/ImGui)
    if (rfui_ui_init() != 0) {
        fprintf(stderr, "Failed to initialize UI\n");
        rfui_ctx_destroy(g_ctx);
        g_ctx = NULL;
        return -1;
    }

    // Start Rayforce thread
    g_ray_thread = ray_thread_create(rfui_rayforce_thread, g_ctx);
    if (!g_ray_thread.handle) {
        fprintf(stderr, "Failed to create Rayforce thread\n");
        rfui_ui_destroy();
        rfui_ctx_destroy(g_ctx);
        g_ctx = NULL;
        return -1;
    }

    // Wait for Rayforce thread to signal ready
    rfui_ctx_wait_ready(g_ctx);

    // Create UI-thread VM (requires runtime to exist)
    // Must be called ON the UI thread (main) - enables direct drop_obj() calls
    g_ctx->ui_vm_ctx = ray_ctx_create();
    if (!g_ctx->ui_vm_ctx) {
        fprintf(stderr, "Warning: Failed to create UI VM context\n");
        // Continue anyway - will leak obj_p on failure (acceptable degradation)
    }

    return 0;
}

i32_t rfui_eval(const char* expr) {
    if (!g_ctx || !expr) {
        return -1;
    }

    // Create MSG_EVAL message with strdup'd expression
    rfui_ui_msg_t* msg = malloc(sizeof(rfui_ui_msg_t));
    if (!msg) {
        return -1;
    }

    msg->type = RFUI_MSG_EVAL;
    msg->expr = rfui_strdup(expr);
    msg->obj = NULL;
    msg->widget = NULL;

    if (!msg->expr) {
        free(msg);
        return -1;
    }

    // Push to ui_to_ray queue
    if (!rfui_queue_push(g_ctx->ui_to_ray, msg)) {
        free(msg->expr);
        free(msg);
        return -1;
    }

    // Wake Rayforce thread
    poll_waker_p waker = rfui_ctx_get_waker(g_ctx);
    if (waker) {
        poll_waker_wake(waker);
    }

    return 0;
}

i32_t rfui_run(nil_t) {
    if (!g_ctx) {
        return 1;
    }

    // Run the ImGui/GLFW main loop
    return rfui_ui_run();
}

nil_t rfui_destroy(nil_t) {
    if (!g_ctx) {
        return;
    }

    // 1. Destroy UI components first (widgets can now drop_obj directly via UI VM)
    rfui_ui_destroy();

    // 2. Destroy UI VM (pushes heap to pending merge, sets __VM = NULL)
    if (g_ctx->ui_vm_ctx) {
        ray_ctx_destroy(g_ctx->ui_vm_ctx);
        g_ctx->ui_vm_ctx = NULL;
    }

    // 3. Send MSG_QUIT to Rayforce thread
    rfui_ui_msg_t* quit_msg = malloc(sizeof(rfui_ui_msg_t));
    if (quit_msg) {
        quit_msg->type = RFUI_MSG_QUIT;
        quit_msg->expr = NULL;
        quit_msg->obj = NULL;
        quit_msg->widget = NULL;

        rfui_queue_push(g_ctx->ui_to_ray, quit_msg);

        // Wake the thread
        poll_waker_p waker = rfui_ctx_get_waker(g_ctx);
        if (waker) {
            poll_waker_wake(waker);
        }
    } else {
        // Malloc failed - set quit flag directly so Rayforce thread can still exit
        fprintf(stderr, "Warning: Failed to allocate quit message, setting quit flag directly\n");
        rfui_ctx_set_quit(g_ctx, 1);

        // Wake the thread so it can see the quit flag
        poll_waker_p waker = rfui_ctx_get_waker(g_ctx);
        if (waker) {
            poll_waker_wake(waker);
        }
    }

    // 4. Join Rayforce thread (runtime_destroy will drain pending heaps)
    int join_result = thread_join(g_ray_thread);
    if (join_result != 0) {
        fprintf(stderr, "Warning: thread_join failed with error %d\n", join_result);
    }

    // 5. Destroy context
    rfui_ctx_destroy(g_ctx);
    g_ctx = NULL;
}

i32_t main(i32_t argc, str_p argv[]) {
#if defined(_WIN32) && defined(DEBUG)
    SetUnhandledExceptionFilter(crash_handler);
#endif
    printf("rayforce-ui v" RFUI_VERSION "\n");

    if (rfui_init(argc, argv) != 0) {
        return -1;
    }

    // Note: REPL widget is created by Rayforce thread after runtime starts

    i32_t code = rfui_run();
    rfui_destroy();

    return code;
}
