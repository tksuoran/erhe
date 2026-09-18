#pragma once

#include <taskflow/taskflow.hpp>

#include <source_location>
#include <utility>

namespace erhe::task {

// Spawn-site guard for the GL worker-context blocking invariant - proposal A
// of doc/gl_worker_context_enforcement.md. All task SCHEDULING goes through
// these wrappers, which assert that the calling thread holds no worker GL
// context: a thread holding one of the four pool slots must never spawn
// tasks, because parking on them while holding wedges the fixed-size pool
// (and a fire-and-forget spawn is forbidden too - whether the spawner also
// waits is not something the guard can see).
//
// Graph CONSTRUCTION is not scheduling: tf::Taskflow::emplace only appends
// to a graph and is deliberately not wrapped - run() is the schedule point
// and is. tf::Subflow::emplace IS wrapped: its schedule point is the join
// that always follows in the same task body, and the emplace is the
// checkable proxy for it.
//
// The guard is a proxy, not the invariant. It misses: waiting on tasks
// spawned before the context was acquired, blocking on non-task primitives,
// call sites reaching tf::Executor directly (a CI grep polices those), and
// non-taskflow pools (BVH, geogram, xatlas). The taskflow observer (B) and
// the acquire watchdog (E) cover part of that gap.

// Aborts when the calling thread holds a worker GL context slot. Active in
// Release, like the ERHE_VERIFY_GL_THREAD_* guards.
void verify_thread_may_spawn(const std::source_location& location);

// executor.silent_async: schedules immediately.
template <typename F>
void spawn(
    tf::Executor&              executor,
    F&&                        f,
    const std::source_location location = std::source_location::current()
)
{
    verify_thread_may_spawn(location);
    executor.silent_async(std::forward<F>(f));
}

// executor.silent_dependent_async: schedules once the predecessors finish -
// the join counter is armed at call time, so the call is the schedule point.
template <typename F, typename I>
[[nodiscard]] auto spawn_dependent(
    tf::Executor&              executor,
    F&&                        f,
    I                          first,
    I                          last,
    const std::source_location location = std::source_location::current()
) -> tf::AsyncTask
{
    verify_thread_may_spawn(location);
    return executor.silent_dependent_async(std::forward<F>(f), first, last);
}

// executor.run: the schedule point for a built tf::Taskflow.
[[nodiscard]] inline auto run(
    tf::Executor&              executor,
    tf::Taskflow&              taskflow,
    const std::source_location location = std::source_location::current()
) -> tf::Future<void>
{
    verify_thread_may_spawn(location);
    return executor.run(taskflow);
}

// subflow.emplace: proxy for the Subflow::join that schedules the children.
template <typename F>
auto emplace(
    tf::Subflow&               subflow,
    F&&                        f,
    const std::source_location location = std::source_location::current()
) -> tf::Task
{
    verify_thread_may_spawn(location);
    return subflow.emplace(std::forward<F>(f));
}

} // namespace erhe::task
