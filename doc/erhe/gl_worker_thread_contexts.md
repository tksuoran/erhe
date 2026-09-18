# OpenGL worker-thread GL contexts

Stability: stable

The GL worker-context subsystem lets taskflow workers create and use shared GL
objects on the OpenGL backend. Without it, a worker reaching
`glCreateBuffers` with no current context faults inside the driver - which any
glTF load on the OpenGL build reached, through
`deferred_finalize_mesh_items` -> `prepare_geometry_buffer_mesh` ->
`Mesh_memory::allocate_vertex_buffer_range`. Vulkan and the procedural default
scene have no such rule.

The GL spec grounding for the cross-context rules is transcribed in
`doc/reference/gl_spec_section_5.md` (the "Shared Objects and Multiple Contexts"
chapter); consult it when touching publication or teardown.

## Requirements

What the subsystem must provide:

1. Worker threads can create and operate on **shared** GL objects -- buffers,
   textures (including pixel upload), samplers, programs -- via DSA, safely
   and concurrently with main-thread rendering.
2. **Container** objects (VAOs, framebuffers; per-context by GL's rules) are
   reached only through explicit per-object accessors that give the calling
   thread an instance on its *own* context.
3. Anything a worker may not do **asserts loudly at the call site**
   (`ERHE_VERIFY`-backed, on in Release too).
4. Baseline (prerequisite): OpenGL 4.5 + DSA mandatory, no macOS OpenGL
   support, compute shaders / SSBOs strictly required on every backend.
   There are no non-DSA fallbacks, so DSA-cleanness is structural: no code
   path exists that binds through the caches to emulate a DSA call.
5. The API is backend-shared code: a no-op on the main thread and on every
   backend with no per-thread context concept (Vulkan, Metal, null).
6. When no worker context can exist (headless / null window, or a creation
   failure), `Device::supports_worker_contexts()` returns false and every
   GPU-touching worker call site takes a **budgeted** main-thread fallback --
   never unbounded work inline in `Editor::tick`.

## Design

### The invariant

A worker may never touch mutable state shared with the drawing thread. Since
the binding caches became per-context, the only shared mutable state left is
the GL objects themselves, governed by the publication rules below.

- **Per-context GL state on the worker's own context is fine**: pixel-store
  parameters, the pixel-unpack binding, its own cache pair. The stronger
  reading -- "a worker may never change a binding" -- is WRONG: it would
  forbid all texture upload and make `blit_framebuffer` look impossible.
  Both are worker-legal.
- **Container objects are per-context instances, never migrated.** A worker
  gets its own VAO / FBO through the accessors. (Queries are also unshared,
  for a different spec reason; `Gpu_timer` is main-thread-only, below.)

### Context identity and guards

`erhe_graphics/gl/gl_context_index.hpp`: one thread-local **context index**
is the single source of truth for what the calling thread may do -- `-1` =
no GL context current, `0` = the main (drawing) context, `1..pool size` = a
worker share context. The same index keys the per-object container slots
and the per-context cache pairs, so permission and identity cannot drift
apart -- deliberately ONE thread-local, not a parallel role enum kept in
sync by hand. It is set in exactly three places: `Device_impl`'s
constructor and `Device_impl::on_thread_enter()` set 0; worker-slot acquire
sets the slot index and release resets to -1. A thread that makes a context
current without going through the API keeps -1 and trips the first guard
rather than faulting in the driver.

- `ERHE_VERIFY_GL_THREAD_HAS_CONTEXT()` (index >= 0): shared-object
  creators (`Device_impl::create_buffer/texture/texture_view/renderbuffer/
  sampler/program/shader`), `Buffer_impl::allocate_storage`, the container
  creators reached through accessors, the blit encoder's upload / copy
  methods.
- `ERHE_VERIFY_GL_THREAD_MAIN_CONTEXT()` (index == 0): buffer mapping
  (`map_bytes` family), readback / pack paths, render / compute encoder
  construction, `Gpu_timer`. **These are NOT cache-protection guards** --
  the caches are per-context -- each encodes a different reason the
  operation is main-only: the window's default framebuffer / swapchain
  exists only on the main context; mapping backs the main-thread frame-ring
  model (worker-mapped writes would need publication points of their own);
  `Gpu_timer` keeps a main-context query ring; and worker-side rendering /
  readback have no publication protocol or call site yet (see Future work).
- `allocate_storage` asserts that a worker only ever allocates
  **non-persistent** buffers -- it calls `map_bytes` for persistent mappings,
  which is main-only. The permission is narrowed, not the guard holed.
- `Blit_command_encoder` is guarded per method, not at construction: upload /
  copy take `HAS_CONTEXT`; `blit_framebuffer` takes `HAS_CONTEXT` and
  requires a `Scoped_framebuffer` for each of its two render passes; readback
  keeps `MAIN_CONTEXT`.

### The API

`scoped_worker_context.hpp`, `scoped_container_access.hpp`:

- `Scoped_worker_context(Device&)` -- grants the calling worker a share
  context and the right to create / operate on shared objects via DSA. Does
  NOT by itself grant container-object access. **Re-entrant**: nested
  construction on one thread refcounts and keeps the same context (only the
  outermost scope acquires and releases); a no-op on the main thread, and
  acquire asserts the calling thread has no context current. Blocking: with
  all pool contexts in use, construction blocks until one is released
  (mutex + condition-variable free-slot list on `Device_impl`).
- `Scoped_vertex_input_state(Device&, const Vertex_input_state&)` /
  `Scoped_framebuffer(Device&, const Render_pass&)` -- ensure the object has
  a GL instance on the calling thread's current context and yield its
  name(s). Cheap and idempotent after first use on a given context. Both
  take **const** references: every adoption site holds only a const pointer
  (`blit_framebuffer` takes `const Render_pass&`), which is well-formed
  because adoption mutates only the atomic per-context slot, never the
  logical object -- slots and the adoption mutex are `mutable`.

**Rejected: tiered contexts** (a `limited` / `full` pair). Container-object
access is a property of the *work*, not the context -- a tier cannot say
"this task may touch *this* framebuffer" -- and a tier cannot distinguish
per-context state (legal) from shared state (not). Tried and reverted; do
not re-propose.

The pool acquire is a plain free-slot list under one mutex with a
condition variable -- the correct shape for a 4-entry pool acquired around
globally serialized work. Do not replace it with a lock-free queue plus a
condition-variable predicate: there is no exact `empty()` to write a
predicate against, and a notify outside the mutex loses wakeups (see
Traps).

### Per-context container instances

- **Context identity**: a dense index assigned at context creation (main =
  0, pool entries 1..N), stored thread-locally.
- **Fixed slot array per object**, sized `1 + configured pool size` (the
  constant 4, NOT the number of contexts actually created -- creation can
  fail and the main instance predates the pool), each slot an
  `std::atomic<unsigned int>` GL name, 0 = not created.
- **The populated fast path is a relaxed atomic load of your own slot, no
  lock.** First-use adoption and cross-slot enumeration (the destruction
  walk) take a per-object mutex -- the mutex is what stops an adoption
  racing the destruction walk into a silent leak. Relaxed ordering is
  sufficient on both sides: no context ever uses another context's name, so
  there is no second datum to order.
- **Adoption points** (never in `gl_name()`, which is per-draw and would
  hide GPU object creation behind a getter): the three pipeline-bind sites
  `Render_command_encoder_impl::set_render_pipeline`,
  `set_render_pipeline_state` and its override-shader-stages overload; and
  `Render_pass::start_render_pass` -- the backend-neutral wrapper, not the
  impl, which has no owner back-pointer. `end_render_pass` needs no
  accessor: it reads a slot the start side populated on the same context.
- **Deferred per-context delete queue**: a per-context GL object must be
  deleted on its own context, so destroying a `Vertex_input_state` /
  `Render_pass` queues the other contexts' instances; each context drains
  its own queue at the next acquire and at teardown; the main context drains
  at an explicit per-frame point (it never re-acquires).
- **`Gpu_timer_impl` is excluded** from the accessor mechanism and is
  main-thread-only: its per-context state is a ring of four queries, and
  nothing wants worker-side GPU timing. Its migration hooks and
  `m_owner_thread` were deleted like the others'; `s_all_gpu_timers` +
  mutex stay because `end_frame` walks the registry to poll -- that is not
  migration.
- **The default vertex input state** (substituted for VAO-less draws inside
  the const per-draw path) is per-context and **eager**: the main context's
  instance is created in `Device::Device`'s *body* (while `Device_impl`'s
  constructor runs, `Device::m_impl` is still null, so it cannot be created
  earlier), each pool context's instance as the last step of creating that
  context, while it is current. The pool is *declared* in `Device_impl` (for
  destruction order against the per-context objects) but *populated* from
  `Device::Device`'s body, after the main instance exists.
- Worker-context `Gl_vertex_array` / `Gl_framebuffer` instances carry a
  **null `binding_state`** -- their destructors must not write another
  context's cache, and container names are per-context so the scrub would be
  wrong anyway.
- `Render_pass_impl::m_draw_buffers` is computed once in the constructor
  (context-independent), and `reset()` does not clear it -- `create()` /
  `reset()` run per context under this design, and a shared member either
  hoists (context-independent) or moves into the slot (per-context).

### Per-context binding caches and the scrub queue

`Gl_binding_state` and `OpenGL_state_tracker` describe a GL *context's*
state; they are per-context **wired pairs** keyed by the context index (the
tracker holds its own binding-state and device pointers).
`Vertex_input_state_tracker`'s cached draw state (`m_last_state`,
`m_attributes`, `m_bindings`) and the active-render-pass slot are
per-context with them. Hot-path access resolves the pair once per command
encoder / render pass and passes it down rather than a TLS hit per call.

**Shared-object deletion needs a cross-context scrub.** Deleting a shared
object (buffer, texture, sampler, program, renderbuffer) scrubs only the
deleting context's cache directly; every other context gets an entry on its
**per-context scrub queue**. Three properties of the drain are load-bearing:

- It issues **real `glBind*` unbinds**, then updates the cache. GL
  auto-unbinds a deleted object only in the context that deleted it; a drain
  that merely zeroes the cache entry leaves the orphan bound and the next
  bind-to-0 elided -- a silent wrong draw.
- Entries carry an **epoch / generation**: shared names are freed for reuse
  the instant `glDelete*` runs (spec 5.1.3), so a name rebound to a new
  object between enqueue and drain must NOT be scrubbed.
- The **main context has an explicit drain point** in
  `Device_impl::wait_frame()` / `begin_frame` -- "drain on next
  make-current" never fires for the one context that draws.
- **Bind elision is suspended while scrubs are pending.** Between a
  delete's enqueue and this context's drain, the cache may hold a deleted
  object's name that GL has recycled, so an equal name is not proof the
  object is bound -- an elided rebind would leave the orphan attached (a
  wrong-draw window), record no epoch, and let the drain unbind the live
  object. Each `Gl_binding_state` therefore carries a pending-scrub count
  (enqueue increments, drain decrements, bind paths read it relaxed);
  while nonzero the five shared-object bind paths bind for real and bump
  the slot epoch, which is what lets the drain spare the rebound name.

### Cross-context publication

GL share contexts do not synchronize automatically. The spec conditions
cross-context visibility on **completion** -- `FenceSync` in the producing
context plus `WaitSync` in the consuming one; a flush alone is WGL/GLX
folklore, not a guarantee. The dangerous ordering is *use-before-release*:
a worker enqueues a buffer's name into `Buffer_transfer_queue` while still
holding its context, and the main thread may copy into that name on the very
next tick.

**Mechanism: publication-point fencing, the sync travels with the object.**

- Producer (worker), at each publication point: `fence_sync` then `flush`
  (the flush is what submits the fence -- `glWaitSync` does not flush the
  producing context, so an unflushed fence may never signal). The sync +
  consumed flag live on the impl (`Gl_publication_sync` on `Buffer_impl` /
  `Texture_impl`); null for main-created objects. A later publication on the
  same context may replace an unconsumed sync.
- Consumer (main thread), before first use: `wait_publication()` --
  server-side `glWaitSync`, delete, mark consumed. Once per object, not per
  use -- one consumer per object: the first wait consumes the sync, so a
  handoff chain longer than producer-to-one-consumer needs re-publication.
  The waits sit in `upload_to_buffer`, `set_sampled_image`,
  `set_storage_image`, `Texture_heap` allocate, and every blit-encoder
  method, which waits on each buffer / texture / attachment it reads or
  writes (a null-check no-op for main-created objects) -- so blit-path
  consumption of a worker-created object is ordered without relying on an
  upload having happened first.
- Publication points: buffer storage (`allocate_storage`, once per pool
  block -- rare), texture storage, each texture upload **method** (once per
  method call, not per `*_sub_image_*` inside the mip / cube-face loops),
  and a worker `blit_framebuffer`'s destination. **This is a rule about
  producers, not a fixed list**: every operation legalized for workers that
  writes a shared object is a publication point.
- The spec's rule 4 (consumer must attach / re-attach): vertex / index
  buffers are satisfied by the per-draw `set_vertex_buffer` /
  `set_index_buffer` re-issue -- **load-bearing: if those attaches are ever
  cached or elided as redundant, rule 4 silently breaks**. The first main
  touch of a worker buffer is a DSA copy with no bind at all -- a known gap
  between the spec's letter and DSA-era reality, covered in practice by the
  WaitSync ordering and recorded as such. Textures: a worker may only
  create-and-upload a texture the main context has not yet bound; the first
  main bind after the wait is the attach. Texture views: the re-attach must
  be of whichever view the consumer samples through.
- **Reverse direction** (worker consuming main-written shared data -- the
  staging buffer feeding a worker texture upload, a blit source the main
  context rendered): rules are symmetric, so the handoff must carry a fence
  created on the **main** context, waited on by the worker.
  `Buffer_impl::publish_for_handoff()` is that fence (fence-then-flush on
  the current context, any thread); the blit encoder's per-method source
  waits are the consuming half. Exercised by the worker-context tests.

### Pool, creation and lifetime

- **Fixed pool of 4, created eagerly on the main thread at startup.** Lazy
  creation is unimplementable: SDL's share-context path make-currents the
  main context first (i.e. *steals it* from whichever thread holds it),
  creates a window, mutates a global non-atomically, and registers an event
  watch -- all main-thread-only. Sizing at `num_workers` is pointless: the
  GL work behind these contexts is globally serialized under
  `buffer_mesh_allocation_mutex()`. Caveat: pool size and *scope width* are
  one decision -- 4 contexts only give full throughput while scopes stay
  narrow (around the allocation), because a scope hoisted over expensive CPU
  work caps that work at 4 concurrent tasks.
- **Context lifetime fixes that predate the pool** (would have been
  pool-scale bugs): `SDL_RemoveEventWatch` in `~Context_window` (was a
  shutdown use-after-free), `SDL_GL_DestroyContext` actually called, and the
  GL debug callback installed **per context**
  (`Device_impl::install_gl_debug_callback()`) -- `glDebugMessageCallback`
  is per-context state, so without this every worker GL error is silently
  discarded.
- **Teardown constraints** (spec 5.1.1): the main context must outlive the
  pool (shared objects survive only while the share list is non-empty -- the
  current member ordering satisfies this; do not reorder). Workers must be
  quiesced and pending publication syncs drained / consumed before pool
  contexts are destroyed, and no pool context may still be current on any
  worker at `~Context_window`.
- A share-context creation failure aborts via `Context_window`'s verify
  rather than degrading to a smaller pool.
- When the pool cannot exist at all (headless / null window):
  `supports_worker_contexts()` is false and call sites branch to their
  budgeted main-thread fallback.

### The worker call sites

GPU-allocating worker tasks, all covered:

1. `deferred_finalize_mesh_items` (async_raytrace_kickoff_operation.cpp) --
   the site with the original repro; one task per mesh.
2. `Gltf_load_task::start_build`'s async lambda --
   `build_imported_buffer_meshes`, the same crash one code path over.
3. `Lightmap_partitioner::process_piece` -- NOT `process_region`; the scope
   sits in the piece so region tasks park in `join()` holding no context
   (the subflow-steal trap below). The serial path also reaches
   `process_piece` on the main thread, exercising the no-op behaviour in
   production.
4. Geometry-graph evaluation (`geometry_graph_window.cpp` async ->
   `Geometry_output_node::evaluate` / `build_preview_primitive` ->
   `make_renderable_mesh`).
5. Mesh operations constructed on workers (`Operations::async_mesh_operation`
   -> operation constructors -> `make_renderable_mesh`; CSG likewise).

`async_for_nodes_with_mesh` gained `op_builds_gpu_meshes` (default true): it
wraps the op in a `Scoped_worker_context` and runs it inline on main when
`!supports_worker_contexts()`. Audited CPU-only, no context needed:
`Asset_browser` glTF scan, `Texture_file_loader` decode (pixels only; upload
is on main), `Lightmap_streamer` tile read, the BVH TLAS build,
`Gltf_load_task`'s scan. The list is not proven complete -- site-by-site
auditing has missed worker call sites before; the structural check is that
every `silent_async` / `silent_dependent_async` / `subflow->emplace` in
`src/editor` is accounted for.

## Verification

`Worker_context_test` / `Worker_context_gl_test` in
`erhe_graphics_gpu_tests` (`src/erhe/graphics/test/test_worker_context.cpp`
and `test_worker_context_gl.cpp`, the latter OpenGL-only) cover, as
repeatable tests runnable on the OpenGL `build_tests` tree and under ASAN
(`build_tests_asan`):

- **Worker GL errors are observable** -- a deliberate worker-side GL error
  reaches the environment's message list through the per-context debug
  callback, which routes into `Device::device_message` (errors fail the
  owning test; in the editor they hit the fatal device-error handler).
- **Buffers prepared on workers, consumed on main**: round trip,
  8-threads-on-a-4-context-pool contention, nested-scope refcounting, the
  main-thread no-op, and context-index tracking across scopes.
- **Worker texture create + upload** (both directions): worker-produced
  staging via `init_data`, and the reverse handoff -- main-written staging
  fenced with `Buffer_impl::publish_for_handoff()` and consumed by a
  worker upload; plus worker `generate_mipmaps` and `fill_buffer`.
- **The per-object accessors**: one `Vertex_input_state` adopted on two
  contexts, per-context idempotence, main-thread destruction queueing the
  worker context's VAO + FBO (observed via the
  `get_pending_container_delete_count` test hook) and the re-acquire
  drain, and a worker `blit_framebuffer` between two accessor-held passes
  read back on main.
- **The scrub queue**: worker-side delete of a main-bound buffer leaves
  the orphan bound until the `wait_frame` drain issues a REAL unbind
  (verified with a raw GL binding query), and a name rebound after the
  enqueue survives the drain -- the name IS recycled on this driver, so
  the epoch guard and the pending-scrub elision suspension are exercised
  for real.
- **The guard**: a death test proves off-scope worker creation dies on
  `HAS_CONTEXT` instead of faulting in the driver.

## Future work

- [GL worker contexts](../plans/gl_worker_contexts.md) - the verification the
  automated tests do not cover, the `parse_gltf` nested flows, the dormant
  `Vertex_input_state::set()`, the optional gate collapse, and worker-side
  rendering.

## Traps

Each of these cost a review or debugging round. Do not rediscover them.

- **The naive fix corrupts main rendering.** Wiring the old draw-capable
  `Scoped_gl_context` into a worker loads the scene and then fails
  `ERHE_VERIFY(vao != 0)` on the main thread: its enter / exit hooks write
  what were then shared caches. The worker API never runs those hooks.
- **Fence-then-flush, in that order, and the flush is mandatory** --
  `glWaitSync` does not flush the producing context; an unflushed fence may
  never signal and the wait is indefinite.
- **Rule-4 attach rides on the per-draw re-issue** of `set_vertex_buffer` /
  `set_index_buffer`. Caching or eliding those as redundant silently breaks
  cross-context visibility.
- **Never compare container-object GL names across contexts** -- name
  spaces are per-context; equal names are expected, not a bug. Check the
  per-context map instead.
- **The scrub drain must issue real unbinds and honor the epoch.** Editing
  only the cache leaves the orphan bound (next bind-to-0 elided); scrubbing
  without the epoch unbinds a live object that received the recycled name.
  Names recycle the instant `glDelete*` runs, not at last use.
- **Cache elision is unsound while scrubs are pending**: rebinding a
  recycled name during the delete-to-drain window compares equal to the
  stale cache entry, so an elided bind attaches nothing, records no
  epoch, and makes the epoch guard above unreachable exactly when it is
  needed. Elision must stay suspended while the pending-scrub count is
  nonzero -- and any future cache that elides by name comparison has the
  same hole. Reviews did not catch this; the scrub tests did.
- **The main context never re-acquires** -- any "on next make-current"
  hook never fires for it. It needs explicit per-frame drain points.
- **Adoption never goes in `gl_name()`** (per-draw; hides object creation
  behind a getter), and there are FOUR adoption points, not one --
  converting only `set_render_pipeline` breaks the two
  `set_render_pipeline_state` paths and `Render_pass::start_render_pass`.
- **`Device::m_impl` is null while `Device_impl`'s constructor body runs**
  -- anything reaching `Device::get_impl()` (e.g. creating the default
  vertex input state, populating the pool) must run from `Device::Device`'s
  body, not from `Device_impl`'s constructor. And the pool stays *declared*
  in `Device_impl` for destruction order; only its population moves.
- **Size per-object slot arrays from the configured pool constant**, never
  from contexts-created-so-far: creation can fail entirely, and the main
  instance is constructed before any pool context exists.
- **A shared member written by a per-context `create()` / cleared by
  `reset()` is a race** (the `m_draw_buffers` lesson): hoist
  context-independent members to the constructor AND remove the clear from
  `reset()`, or the bug returns from the other side.
- **SDL share-context creation steals the current context** (it
  make-currents the main context before setting the share attribute) and is
  main-thread-only -- pool creation is eager, on the main thread, at
  startup. Full stop.
- **Do not "fix" a condition-variable predicate over a lock-free queue** --
  the old provider's busy spin becomes a lost-wakeup deadlock that looks
  *idle* instead of busy. Use a semaphore or a plain deque under the mutex.
- **Subflow scope placement cuts both ways** (the lightmap case): scope in
  the parent task and stolen children have no context; scope in every
  parent against a pool of 4 and `join()` parks all of them holding
  contexts. Scope in the leaf that allocates. Taskflow's co-run during
  `join()` can execute arbitrary queued tasks on the parked thread, which
  is what the save / restore re-entrancy design defends against.
- **`ERHE_VERIFY` is on in Release** -- a guard placed on a destruction
  path aborts shipping runs. Audit worker-side destruction before guarding
  it; prefer log-once over an abort on an unaudited path.
- **"No assert fired" is not evidence when the guard is unreachable** (the
  `Scene_builder` lesson: the constructor pre-registration made the guarded
  path unreachable; the invariant is asserted where it actually holds, in
  `get_vertex_input_from_vertex_format` -- miss path main-only AND the
  vector frozen after construction).
- **Do not trust derived lists in documents; re-derive from code.**
  Hand-enumerated branch and call-site lists drift and miss sites. `grep`
  the identifier, enumerate the async entry points, count the `#if`
  ranges.
