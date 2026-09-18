#pragma once

#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

// Dense per-context identity, stored thread-locally: -1 = no GL context
// current on this thread, 0 = the drawing thread's main context,
// 1..gl_worker_context_pool_size = a worker share context. This is the
// single source of truth for what the calling thread may do: the
// per-context container-object slots (VAOs, framebuffers) and the
// per-context {binding state, state tracker} pairs are keyed by it, and
// the guard macros below test it. Thread ids are the wrong key - the
// executor reuses threads across contexts.
//
// The slot count is the CONFIGURED pool size plus the main context, not the
// number of contexts actually created: context creation may fail entirely
// (headless / null window), and the main context's per-object slots are
// constructed before any pool context exists. Fixed by design - a dynamic
// pool size would invalidate every per-object slot array.
constexpr int gl_worker_context_pool_size = 4;
constexpr int gl_context_slot_count       = 1 + gl_worker_context_pool_size;

[[nodiscard]] auto get_gl_context_index() -> int;
void set_gl_context_index(int index);

[[nodiscard]] inline auto gl_thread_has_context() -> bool
{
    return get_gl_context_index() >= 0;
}

[[nodiscard]] inline auto gl_thread_is_main_context() -> bool
{
    return get_gl_context_index() == 0;
}

[[nodiscard]] inline auto gl_thread_is_worker_context() -> bool
{
    return get_gl_context_index() > 0;
}

} // namespace erhe::graphics

// A thread with any GL context current - main or worker - may pass. A
// thread that makes a context current without going through the worker API
// keeps index -1 and trips this guard rather than faulting in the driver.
#define ERHE_VERIFY_GL_THREAD_HAS_CONTEXT() \
    ERHE_VERIFY(::erhe::graphics::gl_thread_has_context())

// Only the main (drawing) context may pass. NOT a cache-protection guard -
// the binding caches are per-context - the operations behind it are
// main-only for other reasons: the window's default framebuffer / swapchain
// exists only on the main context; buffer mapping backs the main-thread
// frame-ring model (worker-mapped writes would need publication points of
// their own); GPU timers keep a main-context query ring; and worker-side
// rendering / readback have no publication protocol or call site yet. See
// doc/gl_worker_thread_contexts.md, "Context identity and guards".
#define ERHE_VERIFY_GL_THREAD_MAIN_CONTEXT() \
    ERHE_VERIFY(::erhe::graphics::gl_thread_is_main_context())
