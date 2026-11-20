# erhe_task

Stability: stable

## Purpose
The construction of Taskflow executors and the schedule points of Taskflow
work. `make_executor()` makes every executor of erhe and the editor, so each
worker thread is named and runs at the priority its executor asks for. The
spawn wrappers check the GL worker-context blocking invariant
(`doc/erhe/gl_worker_context_enforcement.md` proposal A): a thread holding one
of the worker GL context slots never spawns tasks, because parking on them
while holding the slot wedges the fixed-size pool. Every task spawn in erhe
and the editor goes through these wrappers; `scripts/check_task_spawns.py`
finds call sites that reach `tf::Executor` / `tf::Subflow` directly and
`tf::Executor` objects constructed outside `make_executor()`.

## Public API

### Executors (`erhe_task/executor.hpp`)
- `make_executor(const Executor_create_info&)` -- returns a
  `std::unique_ptr<tf::Executor>` with `thread_count` workers (more than
  zero). Its `tf::WorkerInterface` runs on each worker thread before the
  thread's scheduling loop: it names the thread `<name> <index>` for the
  profiler (`ERHE_PROFILE_THREAD_NAME`) and for the operating system, applies
  the `Worker_priority`, and records the outcome. The function returns once
  every worker has recorded it.
- `Executor_create_info` -- `name`, `thread_count`, `priority`.
- `Worker_priority` -- `normal` (the thread keeps the priority it starts
  with) or `lowered`.
- `get_worker_priority_report(executor)` -- a `Worker_priority_report`: the
  executor's name, the requested priority and one `Worker_thread_report` per
  worker (`priority_applied`, the platform `error_code` of a failed call, and
  the priority read back on the worker thread). The report of an executor not
  made by `make_executor()` is empty.
- `Executors` -- references to an application's foreground and background
  executor (`get_foreground()`, `get_background()`). `get_inherited()` returns
  the background executor on one of its workers and the foreground executor on
  any other thread, so work spawned on behalf of a task stays on that task's
  executor.

### Spawn wrappers (`erhe_task/task.hpp`)
- `verify_thread_may_spawn(location)` -- `ERHE_FATAL` (active in Release)
  naming the spawn site, the held slot and its acquire site when the calling
  thread holds a worker context slot.
- `verify_executor_may_spawn(target, location)` -- `ERHE_FATAL` (active in
  Release) naming the spawn site when the calling thread is a worker of a
  `Worker_priority::lowered` executor made by `make_executor()` and is not a
  worker of `target`.
- `spawn(executor, f)` -- `tf::Executor::silent_async`.
- `spawn_dependent(executor, f, first, last)` --
  `tf::Executor::silent_dependent_async`; returns the `tf::AsyncTask`.
- `run(executor, taskflow)` -- `tf::Executor::run` of a built `tf::Taskflow`;
  returns the `tf::Future<void>`.
- `emplace(subflow, f)` -- `tf::Subflow::emplace`, the checkable proxy for the
  join that schedules the children.

Each wrapper runs both checks and takes a defaulted `std::source_location`
for the report. `emplace` checks the subflow's executor, which is the
executor running the task that owns the subflow.
`tf::Taskflow::emplace` only builds a graph and is used directly.

### Logging (`erhe_task/task_log.hpp`)
- `initialize_logging()` -- creates `log_task` (`erhe.task.log_task`); an
  application that calls `make_executor()` calls it first.

## Executors
`Worker_priority::lowered` is set on the worker thread itself, once, when the
thread starts:

| Platform | Call | Read back |
|---|---|---|
| Linux, Android | `setpriority(PRIO_PROCESS, gettid(), 10)`: nice 10 for that one thread | `getpriority()` |
| macOS | `pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0)` | `pthread_get_qos_class_np()` |
| Windows | `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL)` | `GetThreadPriority()` |

Lowering a thread's own priority needs no privilege on any of them. A thread
that already runs at or below the lowered level is left as it is, so the call
never raises a priority. When the call fails, the worker runs at the priority
it started with and the first failure of the executor logs one warning on
`log_task`. A `normal` executor makes no call; its workers start at the
priority of the thread that constructed the executor (Linux, Android) or at
the platform's default thread priority.

The OS thread name is `pthread_setname_np` (Linux, Android, macOS) or
`SetThreadDescription` (Windows). Linux and Android hold at most 15
characters: a longer name is shortened before the index, which is kept
(`Background w 12`).

## Two executors
An application with work nobody waits for runs two executors: a foreground
executor at `normal` priority and a background executor at `lowered`
priority, both full width, and the operating system's scheduler arbitrates
between them. The code that starts a piece of work names its executor; tasks
spawned inside that work stay on it (`Executors::get_inherited()`). A
background worker spawns onto the background executor only. This is a
property of every `lowered` executor made by `make_executor()`: its worker
interface records the executor's priority in a `thread_local` of the worker
thread, and the spawn wrappers abort when such a worker targets an executor
it is not a worker of, so draining the foreground executor and then the
background executor leaves no task behind. The check takes no lock. A
foreground worker may spawn onto the background executor.

An exception that leaves a worker's scheduling loop is logged as an error on
`log_task` when the worker stops.

## Dependencies
- **erhe libraries:** `erhe::graphics` (private: the worker-context slot
  queries of `erhe_graphics/scoped_worker_context.hpp`), `erhe::log`,
  `erhe::profile`, `erhe::verify` (private)
- **External:** Taskflow, spdlog (public), fmt (private)

## Tests
`erhe_task_tests` (`src/erhe/task/test/`): the workers of a `lowered`
executor read back the lowered value, those of a `normal` executor the
priority they started with, and both report success. `get_inherited()`
returns the foreground executor on a thread that is not a worker and on a
foreground worker, the background executor on a background worker; a
background worker's spawn onto the foreground executor dies (a death test).

## Notes
- The check is a proxy for the invariant. It does not see a wait on tasks
  spawned before the context was acquired, blocking on non-task primitives,
  or non-Taskflow pools (BVH, geogram, xatlas); the Taskflow observer and the
  acquire watchdog of `doc/erhe/gl_worker_context_enforcement.md` cover part
  of that.

## Future work
- `doc/plans/worker_executors.md` (in progress): the Quest measurement of
  the background thread count.
