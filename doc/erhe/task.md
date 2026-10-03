# erhe_task

Stability: stable

## Purpose
The schedule points of Taskflow work, wrapped so each one checks the GL
worker-context blocking invariant (`doc/erhe/gl_worker_context_enforcement.md`
proposal A): a thread holding one of the worker GL context slots never spawns
tasks, because parking on them while holding the slot wedges the fixed-size
pool. Every task spawn in erhe and the editor goes through these wrappers;
`scripts/check_task_spawns.py` finds call sites that reach `tf::Executor` /
`tf::Subflow` directly.

## Public API
- `verify_thread_may_spawn(location)` -- `ERHE_FATAL` (active in Release)
  naming the spawn site, the held slot and its acquire site when the calling
  thread holds a worker context slot.
- `spawn(executor, f)` -- `tf::Executor::silent_async`.
- `spawn_dependent(executor, f, first, last)` --
  `tf::Executor::silent_dependent_async`; returns the `tf::AsyncTask`.
- `run(executor, taskflow)` -- `tf::Executor::run` of a built `tf::Taskflow`;
  returns the `tf::Future<void>`.
- `emplace(subflow, f)` -- `tf::Subflow::emplace`, the checkable proxy for the
  join that schedules the children.

Each wrapper takes a defaulted `std::source_location` for the report.
`tf::Taskflow::emplace` only builds a graph and is used directly.

## Dependencies
- **erhe libraries:** `erhe::graphics` (private: the worker-context slot
  queries of `erhe_graphics/scoped_worker_context.hpp`), `erhe::verify`
  (private)
- **External:** Taskflow (public)

## Notes
- The check is a proxy for the invariant. It does not see a wait on tasks
  spawned before the context was acquired, blocking on non-task primitives,
  or non-Taskflow pools (BVH, geogram, xatlas); the Taskflow observer and the
  acquire watchdog of `doc/erhe/gl_worker_context_enforcement.md` cover part
  of that.
