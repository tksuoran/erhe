# Asynchronous asset loading

How a glTF file gets from disk into a scene without blocking the main loop.
Code lives in `src/editor/assets/` (`asset_load_task`, `asset_load_tick_context`,
`gltf_load_task`, `asset_manager`), with supporting changes in
`src/erhe/gltf/`, `src/erhe/graphics/` and `src/erhe/scene_renderer/`.

The design rationale, the alternatives that were rejected and the per-step
plan are in [`async-asset-loading-plan.md`](async-asset-loading-plan.md),
whose section 0 tracks exactly which parts are implemented. This document
describes what exists.

## The problem

A glTF load used to run to completion inside **one tick**. The
`load_scene_file` message is pumped from `Editor::tick()`, and the whole file
- read, JSON scan, parse, image decode, GPU texture upload, `Buffer_mesh`
build, publish - happened before that tick returned. On a large scene the
editor stopped servicing its run loop, pumping input and presenting frames for
seconds.

Besides being visibly bad, that is the suspected trigger for the macOS
presentation stall investigated in `metal-present-stall-hardening`: the last
pre-load frame is presented, the application then stops servicing its run loop
for several seconds, and presents are never consumed again.

## What it does now

Loading is an `Asset_load_task` owned by `Asset_manager` and advanced a
bounded amount from `Asset_manager::tick()`, called from `Editor::tick()`
immediately after `Scene_commit_queue::flush()`. The editor keeps ticking,
stays interactive and keeps presenting while a scene streams in.

Converted entry points:

| Entry point | Path |
|---|---|
| `--scene`, `commands.json`, File > Load Scene, MCP `load_scene` | erhe-authored scene: opened by the task; foreign glTF: parse handed to `Scene_open_operation` |
| Asset browser Import, MCP `import_gltf` | import into an existing scene |
| Viewport drop, hierarchy drop, asset browser Instantiate, MCP `instantiate_prefab` | prefab template load |
| Asset browser tooltip / context menu | `scan_gltf` on a worker |

Still synchronous, deliberately: `Asset_manager::get_or_load_container`,
`Prefab_library`'s nested external-asset resolution, MCP `reload_prefab` and
`load_asset_file`, and `Image_transfer`'s blocking-drain mode for callers with
no frame loop (`src/example`, `src/rendering_test`, OpenXR controller models).

The master switch is `Load_config::async_gltf_load` (default on). With it off
every path falls back to the original blocking code, which is kept intact -
that is also how the async and blocking results are compared.

## Design

### Phases

`Gltf_load_task` is a phase machine. Each phase names the thread it runs on:

| Phase | Thread | Bounded by |
|---|---|---|
| `scan` | worker | one task |
| `parse` | worker | one task |
| `build` (`Buffer_mesh` construction) | worker | one task |
| `residency` (textures + samplers) | **main**, in tick | GPU upload bytes/frame, items/frame, time slice |
| `publish` | **main**, in tick | atomic, gated on the loader watermark |

The scan decides erhe-authored-scene vs plain glTF asset; it is a whole-file
read plus a full JSON parse, which is why it cannot stay on the main thread.
Import and prefab modes skip it - neither cares.

### Threading invariants

1. Only the main thread touches `erhe::graphics::Device`, creates GPU objects
   or records commands, and only from `Asset_manager::tick`.
2. Workers build **detached** data only. `parse_gltf` is *structurally*
   device-free: `Gltf_parse_arguments` holds no `Device&` and no
   `Image_transfer&`, only a `Gltf_device_options` by value that the caller
   fills on the main thread via `query_gltf_device_options()`. It parses into
   an **unhosted** root node - no live `erhe::scene::Scene` - the way the
   container and prefab loaders already did.
3. A task is never destroyed while a worker it spawned is in flight
   (`Asset_load_task::is_worker_idle`).

### Budget

One `Frame_load_budget` is constructed per frame and shared by every live task
round-robin, so N concurrent loads degrade gracefully instead of multiplying
per-frame cost. Fields come from `Load_config` (v2):

| Field | Default | Meaning |
|---|---|---|
| `async_gltf_load` | `true` | master switch |
| `gpu_upload_bytes_per_frame` | 4 MiB | texture + buffer bytes recorded per frame |
| `io_read_bytes_per_frame` | 64 MiB | file bytes per frame (reserved) |
| `max_decoded_bytes_in_flight` | 128 MiB | decode backpressure (reserved) |
| `max_residency_items_per_frame` | 64 | guards against many tiny items |
| `max_publish_items_per_frame` | 256 | publish slice (reserved; publish is atomic) |
| `load_time_slice_ms` | 4 | hard cap, checked between items, wins over byte budgets |

`gpu_upload_bytes_per_frame` is not merely pacing.
`Device::allocate_ring_buffer_entry` **never refuses** - it spills a new ring
buffer sized to the request - so in frame-recording mode this budget is the
only thing bounding staging memory.

### Two transfer queues, and the gates between them

`Mesh_memory` has two `Buffer_transfer_queue`s:

- The **interactive** queue keeps full-drain semantics in `Mesh_memory::flush`.
  "Enqueued implies uploaded by end of frame" holds, which is what lets
  callers build a mesh and draw it in the *same* command buffer with no gate
  at all - rendertarget meshes, brush previews, the scene builder, and the
  init-command-buffer paths of `example` / `rendering_test`.
- The **loader** queue is drained a budgeted amount by
  `Mesh_memory::flush_budgeted`, called only from `Asset_manager::tick`.

Because `Buffer_info` holds a sink *reference* and `Mesh_memory` itself IS the
interactive sink, selecting the loader queue means a second sink object -
`Mesh_memory::Loader_buffer_sink` - not a parameter. `make_import_build_info`
and `make_primitive_buffer_info` take a `Mesh_memory_queue` selector.

The rule: **only traffic whose publish honours the watermark may use the
loader queue.**

Two gates make that safe:

- **Publish gate.** `Buffer_transfer_queue::enqueue` returns a monotonic
  `Ticket`; a budgeted drain records the highest ticket it drained as the
  watermark. The task snapshots `get_last_ticket()` when its worker build
  finishes and holds publish until `get_watermark()` has reached it.
  Otherwise the scene could appear drawing from vertex and index bytes still
  sitting in the queue.
- **Free gate.** The pools are **shared** between the two queues, so a range
  retired by the *interactive* path can still be the target of a queued loader
  write; freeing it would let it be re-allocated and re-enqueued while the
  older write is pending, and the stale write would land last.
  `Mesh_memory::flush`'s frame-completion handler therefore parks the retired
  batch with the loader ticket high-water mark, and a later `flush` applies
  the batches the watermark has passed. Net effect with no loader traffic:
  frees land one flush later than before - strictly safer, never earlier.

### Uploads

`Image_transfer` has two modes:

- `blocking_drain` - the original: a private 64 MiB staging ring and its own
  transfer command buffer, flushed with submit-and-wait. Required by callers
  with no frame loop, since no frame completion will ever arrive to reclaim
  staging.
- `frame_recording` - `upload_into_frame` stages from the device ring and
  records copies into the caller's (the frame's) command buffer, returning
  `budget_exhausted` instead of blocking. The private ring is not allocated at
  all in this mode, so N concurrent loads do not multiply it.

The GPU half of image loading is split out of the parse into
`Gltf_image_residency`, reachable as `Gltf_data::image_residency`. It holds
the decoded pixels, the sampler `Sampler_create_info`s and the material
texture bindings the parse recorded. Texture and sampler *objects* are created
before publish - creation is cheap; only the pixel copies may lag under the
byte budget - because content-library entries, `Gltf_source_reference` and
`Gltf_data::images` all need real objects by then.

Synchronous callers keep their behaviour by calling
`Gltf_image_residency::drain()`, which makes everything resident and flushes.

### Publish

Publish happens **once**, when structure and fill meshes are resident and past
the watermark - not per mesh. It keeps undo/redo atomic and stops the scene
mutating under the tools mid-import. It is one main-thread step and is not
sliced: a tree big enough to overrun the frame overruns it, which is the
accepted trade.

What publish does depends on the mode:

- erhe-authored scene: `finish_open_scene_gltf` - the tail of
  `open_scene_gltf`, split out and shared by both paths.
- foreign glTF / import / prefab: nothing is published by the task. The
  finished parse is handed back as `Asset_load_result::prepared_parse` and the
  caller queues the cheap, still-undoable operation
  (`Scene_open_operation` / `make_import_gltf_operation`) or finishes the
  template (`Prefab_library::finish_load_template`). Operations stay
  synchronous and cheap; only the loading is async.

`make_renderable_mesh` is idempotent, so the main-thread
`finalize_imported_meshes` pass fast-paths over what the worker built and only
does the scene-side work (raytrace proxy, `update_rt_primitives`, node
collection). On a 119-primitive scene it drops from ~50 ms to ~4 ms.

### Cancellation

Cooperative: `Asset_load_handle::request_cancel()` sets a flag the task
notices at its next phase boundary. In-flight workers are not interrupted;
their results are discarded. A task is reaped only once settled **and**
`is_worker_idle()`.

Three cancellation points:

- **User** - the Cancel button on the load's progress bar in the Operations
  window. An in-flight load has no operation to undo yet, so cancelling is the
  only thing the user can do to it.
- **Scene close** - `Asset_manager::on_close_scene` cancels every load
  importing into the closing scene. It does **not** wait: GPU objects a
  cancelled load recorded are released through frame-completion handlers,
  which cannot fire in the same tick as the close, so waiting here would
  reintroduce exactly the blocking drain this work removes. The task also
  independently notices its target `weak_ptr` expiring.
- **Application exit** - `~Asset_manager` cancels and **blocks** until workers
  are idle. This one has to block: the worker lambdas hold `Build_info`s whose
  `Buffer_info` carries references to `Mesh_memory`'s sinks. Member order
  cooperates - `m_asset_manager` is declared after `m_mesh_memory` in
  `Editor`, so it is destroyed first, while `Mesh_memory` is still alive.
  There is a 30 s deadline after which the tasks are leaked rather than
  destroyed under a live worker.

### Observability

`Asset_manager::get_load_task_count()` feeds an `asset_loads` term in
`App_context::get_async_in_flight_count()` and in the MCP `get_async_status`
tool. The scene is settled only when `pending`, `running`,
`queued_operations`, `pending_scene_commits` and `asset_loads` are all zero;
`scripts/creations/common.py` and `scripts/test_editor_mcp.py` poll on that.

The Operations window shows a progress bar per live load with its state and a
Cancel button.

## API surface

```cpp
// editor/assets/asset_load_task.hpp
enum class Asset_load_state { queued, running, resident, done, failed, cancelled };

class Asset_load_request {
    std::filesystem::path     path;
    std::weak_ptr<Scene_root> import_target;           // set: import; empty: open
    bool                      materials_as_references;
    bool                      prefab_template;
    std::string               root_node_name;
};

class Asset_load_result {
    std::shared_ptr<Scene_root>          scene_root;      // erhe-scene open only
    bool                                 foreign_gltf;    // caller should import
    std::shared_ptr<Prepared_gltf_parse> prepared_parse;  // hand to the operation
};

class Asset_load_handle {              // state / progress / error, thread-safe
    auto get_state() const -> Asset_load_state;
    auto get_progress() const -> float;
    void request_cancel();
};

// editor/assets/asset_manager.hpp
auto Asset_manager::queue_load(Asset_load_request, std::function<void(const Asset_load_result&)>)
    -> std::shared_ptr<Asset_load_handle>;   // null when async_gltf_load is off
void Asset_manager::tick(Asset_load_tick_context&);
void Asset_manager::cancel_loads_for_scene(const Scene_root*);
auto Asset_manager::get_load_handles() const -> std::vector<std::shared_ptr<Asset_load_handle>>;
```

The handle is returned before the load has advanced at all. Note the ordering
consequence: the message bus is pumped *later* in the tick than the asset tick
slot, so a load queued by a message first advances on the following frame.

`Prefab_library::get_or_load_async(path, on_ready)` is the prefab form.
**`on_ready` may run inline** - when the prefab is already cached, when the
path is bad, or when async loading is off - so callers must not assume it is
deferred. MCP `instantiate_prefab` uses exactly that distinction to decide
whether it can report a `node_id`.

## Verifying changes to this code

The check that works is: **export the scene and compare against the same load
with `async_gltf_load=false`.** Two pre-existing sources of nondeterminism
have to be excluded, both unrelated to async loading:

- glTF 2.1 `uid`s are minted fresh on import for files that carry none, so a
  foreign glTF never compares byte-identical. Compare ignoring `uid`.
- The **ERHE_brushes extra-mesh export order after an import varies run to run
  in the same configuration** (three blocking runs produced three different
  orders). Ignore mesh order, or re-run.

With both excluded, async and blocking are identical - every top-level glTF
key. erhe-authored scenes, which carry uids in the file, compare
byte-identical outright.

Also **count the parses in the log**: `grep -c "parse_gltf '<file>'"` must be
1. A defect that left the prepared parse out of the completion payload made a
foreign open parse the file twice, and nothing else surfaced it.

`scripts/test_editor_mcp.py --unit-only` (55 tests) and its smoke test cover
the surrounding editor.

## Future work

In rough priority order.

1. **Re-test the presentation stall.** The hypothesis that motivated this work
   is that the macOS stall is triggered by the load blocking tick 1 with no
   run-loop servicing. That blocking is gone for every converted path.
   *Caution: the VirtualCity repro previously wedged the machine badly enough
   to need a hard reboot.*
2. **Dependent child tasks (plan 2.10).** `Prefab_library`'s nested
   `resolve_external_assets` recursion and `Asset_manager::get_or_load_container`
   are still synchronous, so the dependency graph, its refcounting and
   graph-based cycle detection do not exist. `m_active_load_stack` still does
   cycle detection on the call stack, which stays correct precisely *because*
   those nested loads are synchronous, and `Prefab_library::reload`'s
   empty-stack verify stays valid. A prefab that references further prefabs
   still loads that chain inline at publish.
3. **`get_or_load_container` as a leaf task.** The plan calls container loads
   leaf tasks - they deliberately do not resolve references transitively - so
   this needs no dependency graph. It is what the `pending` machinery
   (`Asset_resolve_state::pending`, `Asset_manager::acquire_or_pending`,
   `Asset_reference::needs_resolve`) was built for; today that machinery is in
   place but never fires, because `acquire` never actually returns pending.
   Doing this requires the parse-time substitution sites
   (`resolve_material_asset_references`, `acquire_import_materials_as_references`)
   to suspend rather than fall through to their "keep the imported copy" path.
4. **Parallelise the worker `Buffer_mesh` build.**
   `build_imported_buffer_meshes` is deliberately serial: the parse clones a
   mesh per instantiating node and the clones share `Primitive` objects, while
   `make_buffer_mesh` has no per-shape serialization, so a per-mesh fan-out
   could build one shared primitive twice at once. Add that serialization
   first.
5. **Slice publish.** `max_publish_items_per_frame` exists but is unused;
   publish is atomic. Only worth doing if a huge tree measurably overruns.
6. **Decode backpressure.** `max_decoded_bytes_in_flight` is likewise
   unused: the parse currently decodes every referenced image before the task
   reaches residency, so peak decoded-pixel memory is not yet bounded.
7. **Remaining MCP tools.** `reload_prefab` and `load_asset_file` are still
   synchronous.
