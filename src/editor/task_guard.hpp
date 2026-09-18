#pragma once

namespace tf {
    class Executor;
}

namespace editor {

// Proposal B of doc/erhe/gl_worker_context_enforcement.md: a tf::ObserverInterface
// that aborts when a task is co-run onto a thread that parked while holding a
// worker GL context slot. Attached to the executor, so a call site that skips
// the erhe::task spawn wrappers (proposal A) cannot bypass it; it catches the
// two shapes A misses (waiting on tasks spawned before the context was
// acquired, and direct-to-executor spawns).
//
// Debug builds only (before-landing item 5): the check is a proxy - it
// detects holding + parked, not holding + waiting on work that needs a slot -
// so it can abort on a harmless park, and a registered observer adds a
// virtual call plus WorkerView/TaskView temporaries on every task entry and
// exit. No-op when NDEBUG is defined.
void attach_gl_context_task_guard(tf::Executor& executor);

} // namespace editor
