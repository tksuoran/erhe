#pragma once

#include <source_location>

namespace erhe::graphics {

class Device;

// Backend-neutral queries for the calling thread's worker-context state,
// callable from code that also builds for Vulkan / Metal / null (where they
// are constant). They exist for the blocking-invariant enforcement in
// doc/gl_worker_context_enforcement.md: the spawn guard and the taskflow
// observer both run in cross-backend code and may not include the
// OpenGL-only gl_context_index.hpp.
//
// True when the calling thread holds a worker GL share context slot.
// Always false on the main thread and on non-OpenGL backends.
[[nodiscard]] auto thread_holds_worker_context() -> bool;
// The pool slot the calling thread holds: 1..pool size, or -1 when none.
[[nodiscard]] auto thread_worker_context_slot() -> int;
// Where the calling thread's outermost live Scoped_worker_context was
// constructed; nullptr when the thread holds no slot. For diagnostics: the
// enforcement observer fires on the victim thread, and this names the scope
// that parked while holding.
[[nodiscard]] auto thread_worker_context_acquire_site() -> const std::source_location*;

// Grants the calling worker thread a GL share context: create and operate on
// SHARED objects (buffers, textures, samplers) via DSA. Does NOT by itself
// grant access to any container object - take a Scoped_vertex_input_state or
// Scoped_framebuffer for that (scoped_container_access.hpp). No-op on the
// main thread and on every backend that needs no per-thread context (Vulkan,
// Metal, null).
//
// RE-ENTRANT: nested construction on one thread refcounts and keeps the same
// context current; only the outermost scope acquires and releases. Scopes
// are stack objects, so destruction is strictly LIFO per thread.
//
// Blocking: with all pool contexts in use, construction blocks until one is
// released. Callers must check Device::supports_worker_contexts() first on
// configurations where the pool may not exist (GL with no window) - see
// gl-worker-thread-contexts.md, "Pool, creation and lifetime"; constructing a scope on a
// worker thread with no pool is an error (asserts).
class Scoped_worker_context final
{
public:
    explicit Scoped_worker_context(Device& device, std::source_location location = std::source_location::current());
    ~Scoped_worker_context() noexcept;
    Scoped_worker_context(const Scoped_worker_context&) = delete;
    void operator=       (const Scoped_worker_context&) = delete;
    Scoped_worker_context(Scoped_worker_context&&)      = delete;
    void operator=       (Scoped_worker_context&&)      = delete;

private:
    Device& m_device;
    // Pool context slot held by THIS scope: >= 1 only for the outermost
    // scope on a worker thread; -1 for the main-thread no-op, for nested
    // scopes, and on backends with no per-thread context.
    int     m_slot{-1};
    // True when this scope participated in the thread-local depth count
    // (worker-thread scopes only, outermost and nested alike).
    bool    m_counted{false};
};

} // namespace erhe::graphics
