# Asynchronous asset loading: design record

Stability: mostly stable

The design behind [`async_asset_loading.md`](async_asset_loading.md), which
describes the pipeline as it runs. This document states the rules the pipeline
obeys and why each is the shape it is; its numbered sections are cited from
source comments and keep their numbers.

The goal the design answers: a glTF load never blocks the main loop. A load is
a task owned by `Asset_manager`, advanced a bounded amount every frame, so the
editor keeps ticking, stays interactive and keeps presenting frames while a
scene streams in.

## 1. What the design builds on

Three mechanisms carry the load, rather than being replaced by it:

- `Mesh_memory` - builders enqueue vertex and index bytes from any thread;
  `Mesh_memory::flush(command_buffer)` records them into the frame's command
  buffer once per frame, with frame-completion-gated frees. The pools are
  finite and `make_renderable_mesh` can fail; it is the transfer queue that is
  unbounded, which is what 2.5 and 2.6 address.
- `Scene_commit_queue` - the cross-thread inbox: workers prepare detached
  results, the main thread applies them at the top of `Editor::tick()`.
- `App_context::get_async_in_flight_count()` - the idle accounting (pending
  and running async operations, the operation-stack queue, the commit queue),
  exposed to MCP as `get_async_status`.

`Image_transfer` is the outlier: in `blocking_drain` mode it owns a private
64 MiB staging ring and its own command buffer, flushed with
`submit_command_buffer_and_wait`, because its uploads happen outside any
frame. 2.6 says who may still use that mode.

## 2. Architecture

### 2.1 Task model

```
editor/assets/asset_load_task.hpp
  class Asset_load_task {
      virtual auto tick(Asset_load_tick_context&) -> Task_state;  // main thread
      virtual void cancel();                                      // sets cancel_requested
      auto get_progress() const -> float;
      auto get_state()    const -> Task_state; // queued/running/ready/done/failed/cancelled
  };
```

`Asset_manager::tick(Asset_load_tick_context&)` runs from `Editor::tick()`
immediately after `m_scene_commit_queue.flush()`, so worker results land
first, and with `App_context::current_command_buffer` available.

`Asset_load_tick_context` carries the frame's shared budget (2.4), the command
buffer, the executor and the device. Budget is handed out round-robin so N
concurrent loads degrade gracefully instead of multiplying per-frame cost.

`Asset_manager::queue_load` returns an `Asset_load_handle` carrying state,
progress and error.

### 2.2 Phases of `Gltf_load_task`

| # | Phase | Thread | Bounded by |
|---|-------|--------|------------|
| 0 | `read` - open file, read bytes | worker | IO bytes/frame |
| 1a | `parse` - fastgltf parse; detached construction only (2.3) | worker | one task; main thread polls |
| 1b | `bind` - anything main-thread-bound: asset-reference resolution, prefab / external-asset resolution, content-library entries | main, in tick | items/frame |
| 2 | `decode` - image decode/transcode, accessor to triangle soup | workers | max concurrent, max decoded bytes in flight |
| 3a | `build` - `Buffer_mesh` construction + `Mesh_memory` enqueue; collision shapes | workers | as 2 |
| 3b | `residency` - create `Texture`s, record texture copies, record buffer transfers | main, in tick | GPU upload bytes/frame, items/frame, time slice |
| 4 | `publish` - insert node tree, attach library entries, attach collision shapes | main, in tick | items/frame |
| 5 | `finalize` - geometry edges, smooth normals, exact raytrace | workers | the deferred finalize path |

Phases 2/3a and 3b overlap: decode and build keep running while residency
drains what is ready. That is what makes a load a stream instead of a stall.

`Buffer_mesh` construction stays on workers (3a), not the main thread, because
only the recording of transfers has to be main-thread.

### 2.3 Threading invariants

1. Only the main thread touches `erhe::graphics::Device`, creates GPU objects
   or records commands, and only from `Asset_manager::tick`.
2. Workers build detached data only. Concretely, these are main-thread-bound
   and therefore belong to phase 1b / 4, not 1a:
   - `Asset_manager` asserts main thread (the private `verify_main_thread()`),
     so asset-reference resolution cannot run on a worker.
   - `Prefab_library` is main-thread-only (a contract, not an assert;
     `load_template` also asserts a live frame) and mutates the content
     library under a mutex it takes itself. `get_or_load` is deliberately
     reentrant: `m_active_load_stack` is cycle detection across nested loads.
     Only `Prefab_library::reload` is non-reentrant
     (`ERHE_VERIFY(m_active_load_stack.empty())`).
   - Mesh-primitive mutation reaches `assert_main_thread()`.
   - Phase 1a builds into an unhosted structure, not a live
     `erhe::scene::Scene`. Host-less item hierarchy and attach operations
     serialize on the process-wide `Item_host::orphan_item_host_mutex` (via
     `resolve_item_host_mutex`; construction itself is not serialized), which
     caps how much parallelism phase 1a can get.
3. Results cross threads through the task's result queues or
   `Scene_commit_queue`.
4. A task is never destroyed while workers it spawned are in flight;
   `cancel()` sets a flag and the manager reaps when the in-flight count hits
   zero.

### 2.4 Budgets

`Load_config` (`src/editor/config/definitions/load_config.py`) is versioned
codegen; the fields and their defaults are listed in
`async_asset_loading.md`, "Budget". Two rules decide how they interact:

- `load_time_slice_ms` is checked between items and wins; the byte budget is
  the upper bound within that slice. `gpu_upload_bytes_per_frame` defaults to
  4 MiB rather than something larger because a memcpy into staging of that
  size is already a meaningful fraction of a 4 ms slice.
- `max_decoded_bytes_in_flight` is what keeps memory bounded: without it a
  fast decoder queues an entire scene's decompressed pixels waiting for upload
  budget. It is a standing cap on the tick context rather than a per-frame
  allowance, and it must throttle worker scheduling, not just buffer results.
  A task suspended on a child must not hold decode budget while it waits;
  that, plus phase ordering (1b precedes 2), is what keeps the global cap from
  deadlocking against dependency suspension.

### 2.5 Upload ordering: tickets and watermarks

A partial drain of a `Buffer_transfer_queue` breaks the invariant that
"enqueued" implies "uploaded by end of frame", and publish would then be able
to draw a mesh whose bytes are still queued. Two gates are required:

1. Publish gate. `enqueue` returns a monotonically increasing ticket; a
   budgeted drain records the highest ticket it drained as a watermark. A mesh
   may only be published once every ticket it depends on is at or below the
   watermark. The gate applies to the loader's publish only, because the
   budgeted drain is a separate entry point (2.6); full-drain callers keep
   "enqueued implies uploaded this frame".
2. Free gate. `Mesh_memory::flush` frees retired pool ranges from a
   frame-completion handler. A freed range can be re-allocated and re-enqueued
   while an older transfer targeting the same bytes is still queued, so the
   stale write would land last. A pool free therefore additionally waits until
   the queue has drained past the freed range's last write ticket. The pools
   are shared between the two queues of 2.6, so the gate is evaluated against
   the loader queue's watermark: the interactive queue is always current at
   flush time, but a range retired by the interactive path can still have a
   pending loader write. Net effect with no loader traffic: frees land one
   flush later - strictly safer, never earlier.

### 2.6 GPU upload path

- Every blocking-drain `Image_transfer` owns a 64 MiB staging ring, so N
  concurrent tasks would multiply it. In `frame_recording` mode all tasks
  share the device's ring (`Device::allocate_ring_buffer_entry`) and no
  private ring is allocated at all. `upload_into_frame` records copies into
  the frame's command buffer and returns "budget exhausted" so the task
  resumes next frame; ring ranges are reclaimed by frame completion, because
  the frame index advances during loading.
- `Device::submit_command_buffer_and_wait` stays, and so does the
  `blocking_drain` mode, for callers that have no frame loop running:
  `src/example` parses a glTF in its constructor with a hand-rolled init
  command buffer before its loop starts. `xr/controller_visualization.cpp` is
  a deliberate exemption that loads from a live tick - a small controller GLB
  already in memory - so a CPU-only `parse_gltf` has to give that caller the
  residency drain loop, and `submit_command_buffer_and_wait` keeps an
  editor-side caller.
- Oversize single images upload mip level by mip level across frames; only a
  single level larger than the whole budget takes a one-shot dedicated staging
  buffer, released on frame completion.
- The loader gets its own transfer queue; existing flushes are untouched. A
  partial drain of the shared queue is not viable, for two independent
  reasons. Callers build a mesh and then draw it in the same command buffer
  with no ticket check (`rendertarget_mesh`, `brush_preview`, `scene_builder`,
  and `example`'s init command buffer, which is ended, submitted and waited on
  before rendering). And `Editor::tick` drains the queue unconditionally once
  per tick - deliberately, so uploads enqueued during a hidden tick do not sit
  in the queue until the next rendered frame - at a point after the
  `Asset_manager::tick` slot, so a budgeted drain would be followed by a full
  drain of everything the workers queued since, making
  `gpu_upload_bytes_per_frame` a no-op for all vertex and index data.

  So `Mesh_memory` has a second transfer queue for loader traffic, selected by
  the task. The rule is: only traffic whose publish honours the watermark
  (2.5) may use the loader queue. `make_import_build_info` takes a queue
  selector; because `Mesh_memory` itself is the `Vertex_buffer_sink` /
  `Index_buffer_sink` and `Buffer_info` holds a sink reference rather than a
  queue handle, the selector means a second sink adapter, not merely a
  parameter. Everything that publishes immediately keeps the interactive queue
  - a load with `async_gltf_load` off, and `controller_visualization`.
  `Mesh_memory::flush` keeps full-drain semantics on the interactive queue for
  every existing caller; `Mesh_memory::flush_budgeted(command_buffer, budget)`
  drains only the loader queue and is called only from `Asset_manager::tick`.
  Tickets and watermarks are per-queue.

  (Rejected alternative: make the editor's per-tick drain the budgeted one. It
  would silently delay non-loader uploads enqueued during hidden ticks, which
  is exactly what that call exists to prevent.)

A consequence worth naming: minting dozens of command buffers inside one
never-advancing device frame does not happen, and neither does the load-time
queue pressure it caused on Metal.

### 2.7 Visibility policy while loading

- Textures use the existing per-frame indirection, not a clear.
  `Material_texture_sampler::texture_reference` is a
  `std::shared_ptr<erhe::graphics::Texture_reference>` resolved every frame in
  `Material_buffer::update`, and a null resolve already yields
  `invalid_texture_handle`, so a pending reference that returns null (or a
  shared dummy created once at init) carries the material through loading with
  no render-side change.

  The wrapper must be temporary, not permanent. The per-frame resolve is the
  render path only; several consumers depend on the stored object's dynamic
  type and `shared_ptr` identity rather than on `get_referenced_texture()`:
  the content library dynamic-casts the reference to `Item_base` for the combo
  preview name and compares it by identity against the library entry, the
  clipboard dynamic-casts it to pin textures on copy, and the MCP scene query
  dynamic-casts it to `Texture` / `Graph_texture` for `texture_id`. A
  permanently installed wrapper would show "(unnamed)", never match the
  library entry, stop pinning on copy, and report a null texture id forever.
  (The glTF export path is safe: it goes through `get_referenced_texture()`.)

  Ordering invariant, because publish consumes the textures too: phase 4
  attaches content-library entries built from `gltf_data.images` as
  `shared_ptr<erhe::graphics::Texture>`, so phase 3b creates the `Texture`
  object for every image before publish - creation is cheap, only the pixel
  copies may lag under the byte budget. The library entry, the
  `Gltf_source_reference` and `Gltf_data::images` always hold the real
  `Texture`; a pending wrapper lives only in
  `Material_texture_sampler::texture_reference`, and residency replaces it
  with the real texture, which is one main-thread slot assignment. Between
  publish and that swap the four consumers above see the wrapper; that window
  is bounded by the upload budget and is accepted.

  Creating the texture and clearing it to a solid color is not implementable
  here: clearing needs `use_clear_texture` or the render-pass fallback, which
  would require render-target usage on every imported texture (they are
  created `sampled | transfer_dst`), and it is impossible for the
  block-compressed images this parser produces (`KHR_texture_basisu`,
  `MSFT_texture_dds`, `EXT_texture_webp`).
- Meshes: a node is published once its fill `Buffer_mesh` is resident and past
  the watermark (2.5). Edge lines and the exact raytrace arrive through the
  deferred finalize path.
- Publish granularity: the node tree is published once, when structure and
  fill meshes are resident - not per mesh. `max_publish_items_per_frame` is
  therefore a slice of one atomic publish, not a licence to interleave: the
  scene mutation happens under one operation, and a tree that exceeds the
  slice overruns the frame rather than losing atomicity. That keeps undo/redo
  atomic (2.8) and stops the scene mutating under the tools mid-import.
- Mid-stream pool exhaustion: `make_renderable_mesh` can fail. With
  publish-once this fails the whole load cleanly before publish, which is one
  more reason to keep publish-once.

### 2.8 Operations, undo, and record adoption

Operations stay synchronous and cheap; only loading is async. The completion
callback uses the existing cross-thread inbox
`Operation_stack::queue_from_thread`, not a new mechanism.

Two orderings make this more than "operation takes ready data":

- `Scene_root` before parse. `open_scene_gltf` must read the `ERHE_scene`
  payload (`enable_physics`) before the `Scene_root` can be constructed. So
  the task owns the parse and the handle exposes the parsed scene-settings
  payload; the completion callback constructs the `Scene_root` and then hands
  the parse over.
- Record adoption. `make_import_gltf_operation` may adopt an `Asset_manager`
  container record instead of parsing (`take_adopted_parse`), and that
  requires the `Scene_root` to be registered first, because
  `Scene_open_operation::execute` registers the scene and then builds and
  executes the import. The task therefore checks for an adoptable record at
  queue time and, when one exists, skips phases 0-3 entirely and completes
  immediately; the phases apply to a fresh parse only. The decision is
  re-validated at completion, because a record can be courtesy-unloaded
  between queue time and completion, in which case the task falls back to a
  fresh parse.

`resolve_material_asset_references` and
`acquire_import_materials_as_references` call `Asset_manager` and so run in
phase 1b on the main thread, before publish.

### 2.9 `Asset_reference` has a pending state

`Asset_resolve_state` carries `pending` beside `unresolved`, `resolved` and
`failed`, and `Asset_manager::acquire_or_pending` reports
`Asset_acquire_state::pending` with a null item for a container that is
loading. Without it, "not loaded yet" would latch as a permanent failure,
because `resolve()` latches `failed` for file-scope keys when `acquire`
returns null. Only a real load failure latches; a retry cadence asks
`needs_resolve()` rather than comparing against `unresolved`, and
`reset_resolution()` stays the manual escape hatch.

Two call-site rules follow:

- The parse-time substitution paths (`resolve_material_asset_references`,
  `acquire_import_materials_as_references`) call `Asset_manager::acquire`
  directly, not through `Asset_reference`, and on a null acquire the latter
  keeps the imported definition. They must distinguish pending from failure
  and suspend the task instead, or a not-yet-loaded container silently turns
  references into copies. Because 1b is a suspend point, the substitution step
  is re-run from the start when the child settles: it is idempotent (it
  re-derives keys from the parse and substitutes), whereas resuming
  mid-iteration would need to remember which materials were already
  substituted.
- `acquire` keeps a synchronous load-or-fail variant for the user-facing verbs
  (`reference_material_into_scene`, `debug_acquire` and its MCP caller), which
  want a load-or-fail answer now, beside the pending-returning variant used by
  `Asset_reference` and the substitution sites.

The pending path does not fire while `get_or_load_container` is synchronous;
see the plan named under "Future work".

### 2.10 Nested loads

`Prefab_library::load_template` is a second complete loader: it constructs its
own `Image_transfer`, calls `parse_gltf` and `finalize_imported_meshes`, then
recurses through `resolve_external_assets` into `get_or_load`.
`Asset_manager::get_or_load_container` is the other. Both are synchronous, and
the rules that hold because of that:

- Container loads are leaf loads: `get_or_load_container` deliberately does
  not resolve `ERHE_asset_reference` transitively, so only the
  `Prefab_library` side can form a chain.
- `Prefab_library`'s cycle detection is a call-stack property
  (`m_active_load_stack`), which is correct precisely because the nested loads
  run inside one call.
- `Prefab_library::reload`'s `ERHE_VERIFY(m_active_load_stack.empty())` holds
  for the same reason: reload is a top-level synchronous operation.

Turning these into dependent child tasks is future work, and it is what
replaces all three rules at once.

### 2.11 Cancellation

Cancellation is a flag checked at phase boundaries plus a reap once the worker
count reaches zero (2.3 invariant 4). The precedent is
`Lightmap_partitioner::on_scene_closed`: an `std::atomic<bool>
cancel_requested` checked inside worker tasks.

GPU objects a cancelled load already recorded must outlive the frame.
Residency records texture copies into the frame's command buffer (2.6), so
dropping those `shared_ptr<Texture>` (or ring entries) on cancel would destroy
a resource an unfinished command buffer still references. Anything a load
created and recorded against is therefore released from a
`Device::add_completion_handler` - the same mechanism that gates pool frees -
and the reap gates on frame completion as well as on the worker in-flight
count.

Two reap points are needed, not one. `Editor::on_close_scene` cancels every
task targeting the closing scene and reaps before the scene-close leak
watchdog checks, 60 frames later; that looser deadline is the satisfiable one,
because GPU objects are released from frame-completion handlers, which cannot
fire in the same tick as the close, so reaping before the watchdog is armed
would force a blocking drain at close time. The second is application exit:
`~Asset_manager` cancels and reaps, or 2.3 invariant 4 is violated at
shutdown.

### 2.12 Completion semantics

- Idle accounting extends the existing one rather than adding a parallel one:
  an `asset_loads` term in `App_context::get_async_in_flight_count()`,
  surfaced through the existing `get_async_status` MCP tool. Idle
  additionally requires the transfer queue drained past the watermark and
  pending texture uploads recorded, otherwise idle still means "geometry not
  on the GPU". The poll idiom is what `scripts/test_editor_mcp.py` and
  `scripts/creations/common.py` use.
- Callers that assumed a load finished on return are converted:
  `import_gltf`, `Scene_open_operation::execute`, asset-browser Import /
  Instantiate / scan, the drag-and-drop prefab loads. Pure queue sites - the
  file-dialog callback, the prefab-instance context menu, the asset browser's
  queueing rows - were already asynchronous and needed nothing.
- MCP. `load_scene` and `open_scene` were already asynchronous (both return
  `{"queued": true}`). `instantiate_prefab` reports the created `node_id` only
  when the prefab load ran inline, and otherwise says the load is deferred and
  the caller polls `get_async_status`. `reload_prefab` and `load_asset_file`
  are still synchronous.
- The `Load_scene_file` handler's `scan_gltf` lives in the task. The scan is a
  whole-file read plus a full fastgltf JSON parse, and that handler is the
  flagship path - File > Load Scene, `--scene`, `commands.json` and MCP
  `load_scene` all land there - so leaving it on the main thread would make "a
  load never blocks the main loop" false for the primary entry point. The task
  reads and parses once, and the erhe-scene-or-foreign decision comes from its
  own parse. The MCP `scan_gltf` query tool does the same whole-file read and
  parse on the main thread; it is a query rather than a load, so it stays
  synchronous deliberately.
- Startup. `--scene` and `commands.json` queue, and the load no longer
  completes inside tick 1, so a script command following a `scene.load_scene`
  chains off the handle's completion instead of assuming the next command sees
  the scene.

### 2.13 Physics and collision shapes

`import_gltf_physics` runs after mesh finalization because mesh-sourced
collision shapes need the built `Geometry`: `build_shape_from_mesh` reaches
the non-const `render_shape->get_geometry()`, which creates the Geometry from
the triangle soup on demand, plus `make_convex_hull`. That is the same
conversion `deferred_edge_lines` pushes to workers, and the deferred finalize
task builds it too, so the Geometry is built once: the build is guarded (see
`async_asset_loading.md`, "Deferred load finalize") and whichever side arrives
second no-ops.

## 3. Implementation order

The order the pieces depend on each other: each step is usable only once the
one before it exists. Source comments cite these step numbers.

1. Tick skeleton. `Asset_load_tick_context`, `Frame_load_budget`,
   `Asset_manager::tick()` wired into `Editor::tick()` after
   `scene_commit_queue.flush()`. The ordering consequence: the message bus is
   pumped later in the tick, so a load queued by a message first advances on
   the following frame - which is what "a handle exists when the queueing call
   returns" has to mean for the MCP poll contract (2.12).
2. Completion plumbing: the `asset_loads` term in
   `get_async_in_flight_count`, `get_async_status`, MCP poll for the tools
   that break, startup-script chaining, and the `pending` state in
   `Asset_reference` (2.9). This has to exist before any entry point flips, or
   the roundtrip script and MCP tools break with no replacement.
3. GPU out of the parse. `parse_gltf` is CPU-only: it produces decoded
   payloads and no GPU objects, and residency is a separate step that creates
   the `Texture`s and populates `Gltf_data::images`. `images` keeps its
   `shared_ptr<Texture>` type (2.7 requires real textures by publish); what
   changed is when it is filled, not what it holds. The blast radius is that
   `Gltf_data::images` is public and consumed by materials, the round-trip
   export and the `ERHE_GRAPHICS_API_NONE` backend's own `Gltf_data`, so
   synchronous callers keep their behavior by draining residency in a loop.
4. Frame-recorded uploads: `Image_transfer` records into the frame command
   buffer with a byte budget, keeping the documented blocking-drain mode
   (2.6).
5. Budgeted transfers and tickets together: the loader transfer queue,
   `Mesh_memory::flush_budgeted`, the ticket/watermark publish gate and the
   pool-free gate (2.5) are one step - the budget alone is a correctness
   regression.
6. `Gltf_load_task`: phase machine, worker fan-out, backpressure, cancellation
   (2.11), progress, the adoption short-circuit (2.8) and collision shapes as
   a worker output (2.13). Behind `async_gltf_load`.
7. The entry points of 2.12.
8. UI progress from the handle (Operations window), and the asset browser's
   scan as a second customer of the worker queue.

## Future work

- [plans/asset_loading.md](plans/asset_loading.md) - dependent child tasks (2.10), pending acquires that actually fire (2.9), publish slicing, decode backpressure.
