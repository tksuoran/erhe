#pragma once

#include "erhe_profile/profile.hpp"

#include <functional>
#include <mutex>
#include <vector>

namespace editor {

// Cross-thread inbox for scene mutations produced by async work.
//
// tf::Executor workers (deferred glTF finalize, make-raytrace) prepare their
// results without touching the live scene, then enqueue() a commit closure
// that applies the prepared results. The main thread applies every queued
// commit in one place - Editor::tick() calls flush() first thing, before any
// other work of the frame reads or edits scenes - so raytrace instance
// lists, mesh primitives and Buffer_meshes only ever change on the main
// thread and never under the feet of the hover / render / MCP code that
// runs later in the same tick.
//
// Commits enqueued while flush() runs (by a commit itself, or by a worker
// finishing mid-flush) are applied on the next tick.
class Scene_commit_queue
{
public:
    using Commit = std::function<void()>;

    // Any thread.
    void enqueue(Commit commit);

    // Main thread, once per tick.
    void flush();

    // Shutdown: drop pending commits without applying them.
    void clear();

    // Any thread. Commits enqueued but not yet applied - a stale-data guard
    // input alongside App_context::pending/running_async_ops and
    // Operation_stack::get_queued_count().
    [[nodiscard]] auto get_pending_count() -> std::size_t;

private:
    ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    std::vector<Commit> m_pending;
};

}
