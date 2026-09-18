# GL worker contexts: outstanding work

Status: proposed

Extends `doc/gl_worker_thread_contexts.md` and
`doc/gl_worker_context_enforcement.md`.

## F. Narrow the slot scopes

Two `Scoped_worker_context` scopes hold one of the four GL slots across a
whole mesh operation - raytrace build and geogram work included - at
`items.cpp` (the arbitrary mesh operation `op`) and
`geometry_graph_window.cpp` (`evaluate_if_dirty`). Narrow them to the
GPU-mesh build only, the shape `async_raytrace_kickoff_operation.cpp` and
`lightmap_partitioner.cpp` already have. `Operations::make_raytrace`
(`operations_window.cpp`) takes the default `op_builds_gpu_meshes = true` yet
builds no GPU meshes; pass `false` as the deferred finalize does, so its scope
disappears.

This is a throughput fix, not an enforcement gap: while a wide scope is held,
three slots serve every other worker. Verify with a geometry-graph evaluation
and a mesh operation on a large mesh under the acquire watchdog (proposal E) -
no wait report - and with `logs/log.txt` showing no `Scoped_worker_context`
held longer than the GPU upload. Locate the scopes by their
`Scoped_worker_context` constructor calls.

## Nested taskflows inside `parse_gltf` park the calling thread

`gltf_fastgltf.cpp` calls `executor.run(taskflow).wait()` from a task already
running on that executor, and `tf::Future` derives from `std::future`, so the
worker parks with no co-run. That is hold-and-wait on the WORKER pool rather
than the GL pool: no GL slot is involved, so proposals A, B and E are all
blind to it. `corun()` instead of `wait()` is the usual answer.

The flows are gated on `Gltf_parse_arguments::parallel`, which defaults to
true, so the main-thread parse callers that never set it run them too and park
the main thread.

## Lock ordering is consistent but unenforced

Held while holding a slot today: `geogram_lock()`, the mesh-memory allocation
mutex, and the BVH pool. None closes a cycle, because every path acquires the
GL slot first. That ordering is unenforced - one refactor away from inversion,
for example a thread holding `geogram_lock` that enters a
`Scoped_worker_context`. The invariant already forbids it; none of A, B, D or
E would detect it.

## C. Hide `tf::Executor` behind a scheduler facade

Would make proposal A a compile error rather than an assert. It needs a
decision about the sub-editor seams: `parse_gltf` takes `tf::Executor&` in its
arguments. The `erhe_raytrace` seam is already resolved by construction
(`set_task_spawner`).

## Non-blocking acquire

`try_acquire` plus the budgeted main-thread fallback that the
no-worker-contexts requirement already defines. It *eliminates* the deadlock
class instead of policing it, but the blocking acquire is deliberate, so this
is a design decision rather than an enforcement mechanism.

## Verification the automated tests do not cover

- The mesh-edit call-site remainder: CSG, geometry-graph evaluation, and a
  lightmap partition run (parallel path, and serial for the main-thread no-op)
  on the GL build, drivable over the editor MCP server. The Catmull-Clark half
  is done.
- Guards on a full glTF load: a few hundred frames with no assert fired, plus
  forcing a worker-side failure path (`create_new_block` returning false) - the
  happy path exercises no error paths.
- Editor-level clean shutdown under ASan. The test environment's device and
  populated-pool teardown runs clean; the editor's own shutdown ordering is a
  separate check.
- Fence mutation checks: remove a producer fence or a consumer wait and observe
  breakage. The fences are exercised end to end, but not mutation-tested, and
  this driver may mask a missing fence.
- Whether xatlas (reached through geogram `PACK_XATLAS` under a mesh-operation
  scope) spawns its own threads. It would be a third non-taskflow pool under a
  scope, not a new class of problem.

## Subsystem cleanups

- **Optional gate collapse** (required by nothing): the remaining
  constant-true capability gates (`use_texture_view`, `use_clear_texture`,
  `use_base_instance`, `use_debug_output` / `use_debug_groups`,
  `use_clip_control`, `use_solid_wireframe`, `use_multi_draw_indirect_core`,
  `primitive_restart_fixed_index`) and the GLSL-version emulation for
  `glsl_version < 420/430`. Bindless textures stay conditional (an extension,
  not 4.5 core), as does the `GL_ARB_shading_language_packing` polyfill.
- **`Vertex_input_state::set()` is dormant and silently broken** under
  per-context instances: reconfiguring in place would need to re-run
  `update()` on every context's VAO. It has no caller in the tree. Delete it,
  or give it an explicit invalidation rule (clear every slot under the
  adoption mutex; contexts re-adopt on next use).
- **`Programs`' shader compile / link taskflow** is commented out; reviving it
  needs a worker context.
- **Worker-side rendering / compute** is structurally possible - per-context
  caches, per-context active-render-pass slot - but has no call site. The
  moment one lands, every shared object it writes needs the per-object
  publication sync and the consumer re-attach: the publication-point set grows
  with every newly legalized producer.
