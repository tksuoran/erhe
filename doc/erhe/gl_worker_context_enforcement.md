# Enforcing the GL worker-context blocking invariant

Stability: mostly stable

Companion to `doc/erhe/gl_worker_thread_contexts.md`, which describes the GL
worker-context subsystem itself. This document covers only how one of its
rules is kept from being broken by future code, and what each mechanism does
and does not catch.

**Checking the Taskflow citations below**: they are against the CPM pin, not
against a stale `build/_deps/taskflow-src` copy in some build tree, whose line
numbers are wildly different.

## The invariant

> **A thread holding a worker GL context must never block on work that may
> itself need a worker GL context.**

That is the real constraint, and it is not mechanically checkable. Two
checkable approximations are enforced, A and B. **Both are proxies**, and
neither is a superset of the real condition: each has false positives (it
fires where no deadlock is possible) AND false negatives. The gap for each is
recorded with it, so a later reader does not mistake a guard for the rule.

The enforced form of A is the stricter, simpler statement:

> **A thread holding a worker GL context must not spawn tasks.**

## Why: the deadlock shape

`Scoped_worker_context` acquires a slot from a pool of share contexts. The
size is a **compile-time constant**, `gl_worker_context_pool_size = 4`
(`gl_context_index.hpp`), and is fixed by design: a dynamic pool size would
invalidate every per-object slot array. Raising it is not a configuration
change and not a mitigation.

Acquisition **blocks**: `Device_impl::acquire_worker_context_slot` waits on a
condition variable. The scope is a no-op on the main thread, and is
**re-entrant per thread** - `t_worker_context_depth++ > 0` reuses the slot the
thread already holds, so only the outermost scope acquires and releases.

Re-entrancy is per *thread*, and that is exactly why nesting taskflows under a
scope is dangerous: a stolen task runs on a *different* thread, so it needs a
*different* pool slot. Hold-and-wait follows:

1. N parent tasks each take a scope and hold a slot;
2. each parent blocks in `join()` / `wait()` on child tasks;
3. children run on other threads and each construct their own scope;
4. with N == pool size, no slot can ever be released.

Step 2 needs one refinement, because Taskflow co-runs children on the parked
parent: a permanent wedge requires the parents' remaining children to have
been *stolen* by workers that are themselves now blocked in `acquire`, leaving
the parents with empty local queues and nothing to co-run. That configuration
is reachable, and once reached it is permanent.

`doc/erhe/gl_worker_thread_contexts.md` states the placement rule in its Traps:
scope in the parent task and stolen children have no context; scope in every
parent against a pool of 4 and `join()` parks all of them holding contexts;
**scope in the leaf that allocates**. `Lightmap_partitioner` is the worked
example - the scope sits in `process_piece`, not `process_region`, so region
tasks park holding nothing.

**The failure mode splits the threads into two populations, and confusing them
misdirects triage.** Threads parked in `Subflow::join` are inside
`_corun_until` (`executor.hpp`), which **spins**: it re-explores and calls
`std::this_thread::yield()` past `MAX_STEALS`, never sleeps, so each burns
close to a full core. Only the threads blocked in
`acquire_worker_context_slot`'s condition variable are genuinely idle. The
process therefore looks BUSY while making no progress: "hung but idle" is the
wrong signature to look for, and
`doc/erhe/gl_worker_thread_contexts.md`'s warning about a deadlock that looks idle
rather than busy describes the acquire side only.

### What the re-entrancy refcount does and does not buy

When Taskflow co-runs a queued task onto a parked thread that holds a slot,
that task's own scope hits `depth++ > 0` and shares the parent's slot: no new
acquisition, no block, and the per-context caches stay coherent because both
tasks are on the same context. Co-run is therefore *safe*.

A and B are a deliberate tightening on top of that: parking while holding a
context is **forbidden**, and the refcount remains as belt-and-braces rather
than as a licence to rely on it.

## What runs under a scope today

Every `Scoped_worker_context` in the tree (tests excluded):

| scope site | encloses |
| --- | --- |
| `assets/gltf_load_task.cpp` | `build_imported_buffer_meshes` |
| `geometry_graph/geometry_graph_window.cpp` | `evaluate_if_dirty` |
| `items.cpp` | `op(parameters)` (an arbitrary mesh operation) |
| `operations/async_raytrace_kickoff_operation.cpp` | `prepare_geometry_buffer_mesh` |
| `renderers/lightmap_partitioner.cpp` | `make_renderable_mesh` |

No scope has a **taskflow** spawn beneath it, so the GL pool cannot deadlock
against itself. For four of the five that is a closed question; for
`items.cpp` it is inference from inspecting today's callers, because `op` is
an arbitrary caller-supplied mesh operation and a future one could spawn. That
open set is the strongest argument for D.

That is a narrower statement than "the code under a scope is single-threaded",
which is **false**: two other thread pools and a process-global mutex are
reached from inside scopes.

- **The BVH pool.** `Bvh_geometry::commit()` dispatches to a process-wide
  singleton `bvh::v2::ThreadPool` + `ParallelExecutor` and blocks on it. It is
  reached inside two scopes: `items.cpp` -> `op` -> `make_raytrace()` (the
  mesh, geometry, merge and paint operations), including a route a
  `make_raytrace` grep does not find - `Operations::make_raytrace`
  (`operations_window.cpp`) dispatches with the **default**
  `op_builds_gpu_meshes = true` and its worker op calls
  `prepare_real_raytrace()`, so geometry conversion plus BVH build run under
  the scope although the op builds no GPU meshes at all; and
  `geometry_graph_window.cpp` -> `evaluate_if_dirty` ->
  `geometry_output_node.cpp`. **Not**
  `transform/mesh_component_transform.cpp`, despite looking like the same
  pattern: those sit in `commit` / `fork_group` / `extrude_group`, whose only
  callers are the gizmo drag path and MCP, both main-thread and under no
  scope.
- **Geogram's internal `parallel_for` pool**, plus the global
  `geogram_lock()` recursive mutex that `Geometry::process()` takes, reached
  under `items.cpp` through `mesh_operation.cpp`.

Neither pool needs GL slots, so neither can deadlock the GL pool. The
codebase is nonetheless **inconsistent** about it, and the inconsistency looks
unintentional: `async_raytrace_kickoff_operation.cpp` and
`lightmap_partitioner.cpp` deliberately keep raytrace work *outside* the
scope, while `items.cpp` and `geometry_graph_window.cpp` hold a slot across an
entire mesh operation. That violates the throughput caveat in
`doc/erhe/gl_worker_thread_contexts.md` (a scope hoisted over expensive CPU work
caps that work at 4 concurrent tasks) and is worth fixing on its own; see the
plan.

### The `erhe_raytrace` spawn seam

`Bvh_scene::commit()` can spawn (`bvh_scene.cpp`), from a library *below* the
editor. It is a **latent** gap rather than a live one, for two independent
reasons, and it takes both:

- every demonstrated caller of `Bvh_scene::commit()` runs on the main thread.
  The route through `Mesh::update_rt_primitives` has roughly thirty-five call
  sites across brushes, the scene builder, the geometry graph, the operations,
  the previews and the XR visualizations; every one checked is main-thread,
  and the one genuinely worker-side route (`parse_primitive` ->
  `Mesh::add_primitive` in the parallel mesh flow) builds no raytrace geometry
  at parse time. **Treat that as a spot-check, not an enumeration**;
- and even if a worker caller appeared, a per-primitive `Bvh_scene` attaches
  exactly one geometry while `start_tlas_build` bails below
  `k_min_tlas_children = 4`, so it would not reach the spawn anyway.

**Warning to the next editor**: the thread-affinity half of this flipped three
times across reviews. The trap is that `deferred_finalize_mesh_items` IS
dispatched onto a worker, which makes the whole function look worker-side,
while the `update_rt_primitives` calls are in its phase-B commit lambda, which
is not. Check whether a line is inside a `scene_commit_queue->enqueue` before
concluding anything about its thread.

## A. Spawn-site guard

`erhe::task` (`src/erhe/task`) wraps every scheduling form, and each wrapper
asserts that the calling thread holds no worker context. Every scheduling site
in `src/editor`, `erhe_gltf` and `erhe_raytrace` goes through them. The check
is active in Release, in the style of the `ERHE_VERIFY_GL_THREAD_*` guards,
and the failure report names both the spawn site and the acquire site of the
held scope.

**Graph construction is not scheduling**: `tf::Taskflow::emplace` only appends
to a graph, and the work is scheduled later by `executor.run`. Guarding
`emplace` would false-positive on the legitimate "build the graph on a
context-holding thread, hand it to a non-holding thread to run" pattern, and
guarding `run` already covers the `tf::Taskflow` case. `tf::Subflow::emplace`
is different: `tf::Subflow` derives from `FlowBuilder`, so it looks like graph
construction, but its real schedule point is `Subflow::join` (or the implicit
join). It is guarded at `emplace` as a **proxy** for that join, which is sound
only because emplace and join always occur in one task body.

| form | guard? | why |
| --- | --- | --- |
| `silent_async` | yes | schedules immediately |
| `silent_dependent_async` | yes | schedules once its predecessors finish, not immediately |
| `executor.run(taskflow)` | yes | the schedule point for a built graph |
| `subflow->emplace` | yes | proxy for the `join()` that schedules it |
| `taskflow.emplace` | no | graph construction, schedules nothing |

The `erhe_raytrace` seam is closed by construction rather than by a wrapper:
`set_task_spawner(std::function<void(std::function<void()>)>)`
(`raytrace_executor.hpp`) replaces an injected `tf::Executor*`, and the editor
injects a lambda that routes through `erhe::task::spawn`. So the library's one
spawn is guarded without a graphics dependency in `erhe_raytrace`, which no
longer links Taskflow at all. Its tests inject a plain-`silent_async` spawner.

- **Catches**: spawn-then-join while holding - the documented trap.
- **Misses**: acquiring a context and then waiting on tasks spawned *before*
  acquisition; blocking on a non-task primitive (condition variable, future)
  that transitively needs a context; any call site reaching `tf::Executor`
  directly; and every non-taskflow pool (BVH, geogram, xatlas), which never
  touches the wrapper at all.
- **Deliberately stricter than the invariant**: a fire-and-forget spawn while
  holding a context cannot deadlock, because the parent never waits. It is
  forbidden anyway, because "did you also wait on it" is not something the
  guard can see.
- **Unstated dependency**: A guards the spawn, never the wait, so it only
  works while spawn and wait live in the same scope. True at every site today;
  it stops being true the moment someone spawns in one function and waits in
  another.
- **Cost**: one thread-local read per spawn.

The backend-neutral accessors A and B need -
`thread_holds_worker_context()`, `thread_worker_context_slot()` and
`thread_worker_context_acquire_site()` - live in
`erhe_graphics/scoped_worker_context.{hpp,cpp}` and are constant off OpenGL.
They exist because `gl_context_index.{cpp,hpp}` are compiled only inside the
OpenGL branch of `src/erhe/graphics/CMakeLists.txt`, while both mechanisms are
called from code that also builds for Vulkan / Metal / null.
`Scoped_worker_context` records its construction site (a defaulted
`std::source_location`) thread-locally on the outermost worker scope and
passes it to the acquire, so the watchdog can name holders.

## B. Taskflow observer

`src/editor/task_guard.{hpp,cpp}` implements `tf::ObserverInterface` and is
attached in `editor.cpp` right after the executor is created, in **Debug
builds only**: the check is a proxy that can abort on a harmless park, and a
registered observer costs on every task entry and exit. It is a no-op under
`NDEBUG`.

The check is that a worker does not begin task B while running task A unless A
blocked. The intra-thread nesting paths all funnel through `_corun_until`,
reached from `Subflow::join`, `Runtime::corun` / `corun_all`,
`Executor::corun`, `Executor::corun_until` and `tf::TaskGroup` - every one a
blocking call made from inside a task. So a *nested* `on_entry` is an in-band
signal that the outer task parked:

```cpp
// Namespace scope in a .cpp, and BEFORE the class: a declaration placed after
// it would not be found by unqualified lookup in the inline member bodies. A
// class member would work spelled `inline static thread_local`; the plain
// `static thread_local` spelling needs an out-of-line definition.
thread_local int t_task_depth = 0;

class Gl_context_task_guard : public tf::ObserverInterface
{
public:
    void set_up(std::size_t) override {}
    void on_entry(tf::WorkerView, tf::TaskView task_view) override
    {
        if ((t_task_depth++ > 0) && erhe::graphics::thread_holds_worker_context()) {
            ERHE_FATAL(
                "task '%s' co-ran on a thread parked while holding GL worker context slot %d",
                task_view.name().c_str(),
                erhe::graphics::thread_worker_context_slot()
            );
        }
    }
    void on_exit(tf::WorkerView, tf::TaskView) override { --t_task_depth; }
};
```

The observer runs **on the executing worker**, so it can read the thread-local
context state; and it is attached to the executor, so a call site that skips
A's wrapper cannot bypass it.

**Node-kind coverage is effectively complete.** `Executor::_invoke`'s switch
dispatches ten node kinds. Eight fire the observer: static, subflow,
condition, multi-condition, async and dependent-async (whose prologue and
epilogue are in `executor.hpp`), plus `Node::RUNTIME` /
`Node::NONPREEMPTIVE_RUNTIME` and the `void(Runtime&)` variants, whose
prologue and epilogue live in `taskflow/core/runtime.hpp` - which is why
grepping `executor.hpp` alone understates the coverage. Only `Node::MODULE`
and `Node::ADOPTED_MODULE` are unobserved, and neither can be the outer frame
B needs: that path schedules the subgraph and returns `true` to preempt, so
the worker frame unwinds rather than parking in-frame, and it can never co-run
a nested task onto itself.

`_invoke_subflow_task` matters most: the lightmap region task is emplaced with
a `tf::Subflow&` parameter, so its prologue and epilogue sit around the
subflow's work, and the nested `join()` co-run runs strictly inside an
observed outer task - exactly what B needs for the case this document exists
for. `_corun_until` runs local-queue and stolen tasks through `_invoke`, so
co-run on a parked thread also fires `on_entry`.

Preemption paths fire the prologue only on first entry, and
`TF_EXECUTOR_EXCEPTION_HANDLER` catches inside the prologue / epilogue window,
so the depth counter does not drift - except in a build with
`TF_DISABLE_EXCEPTION_HANDLING`, where the macro degrades to a bare
`code_block;` and an escaping exception would skip the epilogue. That is not
the pin's default.

- **Catches**: a context-holding task that blocked *and* got a task co-run
  onto it - including the two shapes A misses (waiting on tasks spawned before
  acquisition; spawns that bypassed A's wrapper).
- **Also a proxy, not direct observation.** It detects *holding + parked*, not
  *holding + waiting on work that needs a slot*. A parent parked on children
  that never touch GL is harmless and still aborts. That is why it is
  Debug-only.
- **Structurally blind to the wedged state.** A thread stuck in
  `acquire_worker_context_slot`'s `condition_variable::wait` runs no tasks, so
  there is no `on_entry` to observe. Likewise `tf::Future` derives from
  `std::future` and adds no `wait()` override, so `executor.run(tf).wait()`
  parks the calling thread with **no co-run at all**. That applies to the
  nested flows in `gltf_fastgltf.cpp`, which run on a worker; it does not
  apply to `make_brushes` in `scene_builder.cpp` or to
  `Executor::wait_for_all`, which park the MAIN thread, and the main thread
  holds context index 0 and never a pool slot. This is a structural hole, and
  E is what covers it.
- **Reports on the victim thread**: the stack shows the co-run, not who
  parked, which is what the recorded acquire site is for. `TaskView::name()`
  returns an empty string for unnamed async tasks, but naming is available
  (`silent_async(tf::TaskParams{"name"}, f)`), so an unnamed spawn site is a
  fixable omission, not a limitation.
- **Cost**: `_observer_prologue` iterates the `_observers` set
  unconditionally, per task, whether or not any observer is registered.
  Registering one adds, per task entry AND exit, a virtual call plus the
  `WorkerView` / `TaskView` temporaries.

## A and B together

Complementary, not redundant:

- A fails fast **at the offending spawn**, with the guilty stack - what you
  want while writing code.
- B catches what A misses and cannot be bypassed - what you want in CI and
  under load.

Neither observes the real deadlock. **E does**, and on the evidence above it
carries more of the weight than its billing suggests.

## D. CI check that spawns go through the wrapper

`scripts/check_task_spawns.py`, run as the `task-spawn-guard` job in
`.github/workflows/build.yml`. It scans `src/editor` and `src/erhe` with
comments stripped for the scheduling forms - `silent_async`,
`silent_dependent_async`, `executor->run` / `executor.run`, and
`subflow->emplace` - outside the wrapper, and exempts `src/erhe/task` and
`*/test/*` (tests run their own executors with no shared device).
`taskflow.emplace` is deliberately NOT in the list (graph construction, see
A); `subflow->emplace` deliberately is, because it is the lightmap shape that
motivates this document.

Without D, A decays from construction into documentation, and
`doc/erhe/gl_worker_thread_contexts.md` warns in its Traps: do not trust derived
lists in documents, re-derive from code.

## E. Acquire watchdog

In `Device_impl::acquire_worker_context_slot` (`gl_device.cpp`): 10-second
`wait_for` slices; each timeout with every slot held logs, at error level to
`log_threads`, the requesting site and the per-slot holders (thread id plus
acquire site, kept in `m_worker_context_slot_holders` under the pool mutex).
It keeps waiting rather than aborting, because a long wait can also be
legitimate contention.

This observes the **actual** condition rather than a proxy, including
everything A and B miss - the `run().wait()` sites, condition-variable waits,
and any future non-taskflow blocking - and it converts the wedge into a report
that names the four holders. That matters more than it sounds, given the
signature above: the co-running threads look busy, so without E nothing points
at the context pool at all.

## Relationship to `parse_gltf`

`doc/erhe/gl_worker_thread_contexts.md` lists nested taskflows inside `parse_gltf`
as future work. This document answers half of it: **no GL scope crosses those
flows** (parse is CPU-only, and `erhe_gltf` contains no
`Scoped_worker_context`), so they cannot deadlock the GL pool. What is
genuinely open there is a different resource - see the plan.

## Future work

- [GL worker contexts](../plans/gl_worker_contexts.md) - narrowing the slot
  scopes (F), the `parse_gltf` worker-pool park, lock ordering, the scheduler
  facade (C) and a non-blocking acquire.
