# Asynchronous asset loading

Stability: mostly stable

How a glTF file gets from disk into a scene without blocking the main loop.
Code lives in `src/editor/assets/` (`asset_load_task`, `asset_load_tick_context`,
`gltf_load_task`, `asset_manager`), with supporting parts in `src/erhe/gltf/`,
`src/erhe/graphics/` and `src/erhe/scene_renderer/`.

This document describes the pipeline as it runs. The design record, whose
numbered sections source comments cite, is
[`async_asset_loading_design.md`](async_asset_loading_design.md); asset
identity, ownership and the registry are
[`asset_manager.md`](asset_manager.md).

A load is an `Asset_load_task` owned by `Asset_manager` and advanced a bounded
amount from `Asset_manager::tick()`, called from `Editor::tick()` immediately
after `Scene_commit_queue::flush()`. The editor keeps ticking, stays
interactive and keeps presenting while a scene streams in.

## Entry points

| Entry point | Path |
|---|---|
| `--scene`, `commands.json`, File > Load Scene, MCP `load_scene` | erhe-authored scene: opened by the task; foreign glTF: parse handed to `Scene_open_operation` |
| Asset browser Import, MCP `import_gltf` | import into an existing scene |
| Viewport drop, hierarchy drop, asset browser Instantiate, MCP `instantiate_prefab` | prefab template load |
| Asset browser tooltip / context menu | `scan_gltf` on a worker |
| Asset browser directory walk (startup and the Scan button) | the walk of `res/editor/assets` and `res/editor/scenes` runs on a worker and publishes batches the main thread attaches (doc/asset_browser_scan.md) |

Deliberately synchronous: `Asset_manager::get_or_load_container`,
`Prefab_library`'s nested external-asset resolution, MCP `reload_prefab` and
`load_asset_file`, and `Image_transfer`'s blocking-drain mode for callers with
no frame loop (`src/example`, OpenXR controller models).

The master switch is `Load_config::async_gltf_load` (default on). With it off
every path falls back to the blocking code, which is kept intact and is what
an async result is compared against.

## Phases

`Gltf_load_task` is a phase machine. Each phase names the thread it runs on
(design record 2.2):

| Phase | Thread | Bounded by |
|---|---|---|
| `scan` | worker | one task |
| `parse` | worker | one task |
| `build` (`Buffer_mesh` construction) | worker | one task |
| `residency` (textures + samplers) | main, in tick | GPU upload bytes/frame, items/frame, time slice |
| `publish` | main, in tick | atomic, gated on the loader watermark |

The scan decides erhe-authored-scene vs plain glTF asset; it is a whole-file
read plus a full JSON parse, which is why it does not run on the main thread.
Import and prefab modes skip it - neither cares.

Three threading invariants hold (design record 2.3): only the main thread
touches `erhe::graphics::Device`, creates GPU objects or records commands, and
only from `Asset_manager::tick`; workers build detached data only, which is
why `parse_gltf` is structurally device-free (`Gltf_parse_arguments` holds no
`Device&` and no `Image_transfer&`, only a `Gltf_device_options` the caller
fills on the main thread through `query_gltf_device_options()`) and parses
into an unhosted root node; and a task is never destroyed while a worker it
spawned is in flight (`Asset_load_task::is_worker_idle`).

## Budget

One `Frame_load_budget` is constructed per frame and shared by every live task
round-robin, so N concurrent loads degrade gracefully instead of multiplying
per-frame cost. The fields come from `Load_config` (v2):

| Field | Default | Meaning |
|---|---|---|
| `async_gltf_load` | `true` | master switch |
| `gpu_upload_bytes_per_frame` | 4 MiB | texture + buffer bytes recorded per frame |
| `io_read_bytes_per_frame` | 64 MiB | file bytes per frame |
| `max_decoded_bytes_in_flight` | 128 MiB | decode backpressure; a standing cap carried on the tick context, not a per-frame allowance |
| `max_residency_items_per_frame` | 64 | guards against many tiny items |
| `max_publish_items_per_frame` | 256 | publish slice |
| `load_time_slice_ms` | 4 | hard cap, checked between items, wins over the byte budgets |

`gpu_upload_bytes_per_frame` is not merely pacing:
`Device::allocate_ring_buffer_entry` never refuses - it spills a new ring
buffer sized to the request (doc/ring_buffer_memory.md) - so in
frame-recording mode this budget is the only thing bounding staging memory.

## Transfer queues and uploads

`Mesh_memory` has two `Buffer_transfer_queue`s (design record 2.6): the
interactive one keeps full-drain semantics in `Mesh_memory::flush`, so a
caller may build a mesh and draw it in the same command buffer with no gate at
all; the loader one is drained a budgeted amount by
`Mesh_memory::flush_budgeted`, called only from `Asset_manager::tick`. Because
`Buffer_info` holds a sink reference and `Mesh_memory` itself is the
interactive sink, selecting the loader queue means a second sink object,
`Mesh_memory::Loader_buffer_sink`; `make_import_build_info` and
`make_primitive_buffer_info` take a `Mesh_memory_queue` selector. The rule is
that only traffic whose publish honours the watermark may use the loader
queue, and the publish gate and the free gate of design record 2.5 are what
make that safe.

`Image_transfer` has two modes (design record 2.6): `blocking_drain` stages
through a private 64 MiB ring and its own transfer command buffer, flushed
with submit-and-wait, and is required by callers with no frame loop, since no
frame completion will ever arrive to reclaim staging; `frame_recording` stages
from the device ring and records copies into the frame's command buffer,
returning `budget_exhausted` instead of blocking, and allocates no private
ring at all, so N concurrent loads do not multiply it.

The GPU half of image loading is split out of the parse into
`Gltf_image_residency`, reachable as `Gltf_data::image_residency`. It holds
the decoded pixels, the sampler `Sampler_create_info`s and the material
texture bindings the parse recorded. Texture and sampler objects are created
before publish - creation is cheap and only the pixel copies may lag under the
byte budget - because content-library entries, `Gltf_source_reference` and
`Gltf_data::images` all need real objects by then. Synchronous callers keep
their behavior by calling `Gltf_image_residency::drain()`.

## Publish

Publish happens once, when structure and fill meshes are resident and past the
watermark - not per mesh. It keeps undo/redo atomic and stops the scene
mutating under the tools mid-import. It is one main-thread step and is not
sliced: a tree big enough to overrun the frame overruns it, which is the
accepted trade.

What publish does depends on the mode:

- erhe-authored scene: `finish_open_scene_gltf`, the tail of
  `open_scene_gltf`, shared by both paths.
- foreign glTF / import / prefab: nothing is published by the task. The
  finished parse is handed back as `Asset_load_result::prepared_parse` and the
  caller queues the cheap, still-undoable operation
  (`Scene_open_operation` / `make_import_gltf_operation`) or finishes the
  template (`Prefab_library::finish_load_template`). Operations stay
  synchronous and cheap; only the loading is async.

`make_renderable_mesh` is idempotent, so the main-thread
`finalize_imported_meshes` pass fast-paths over what the worker built and does
only the scene-side work (raytrace proxy, `update_rt_primitives`, node
collection). On a 119-primitive scene that pass costs about 4 ms, against
about 50 ms when it has to build everything itself.

## Deferred load finalize

Three `Load_config` options decide how much of a mesh is built while the load
is still in progress. All three default on; turning one off restores fully
eager, serial behavior, which is how a deferral is compared against the work
it replaces.

| Option | On |
|---|---|
| `deferred_raytrace` | the load gives each primitive an AABB proxy raytrace and a background task builds the real triangle raytrace |
| `deferred_edge_lines` | the load builds a fill-only buffer mesh straight from the triangle soup and a background task builds the `Geometry` (edges, smooth normals) and the full buffer mesh |
| `parallel_gltf_parse` | image decode, mesh parse and animation parse run as parallel tasks inside `erhe::gltf::parse_gltf` (`Gltf_parse_arguments::parallel`) |

`deferred_edge_lines` puts a GPU buffer-mesh build on a worker, which on GL
needs a worker share context; without one (headless, null window) the edge
lines are built eagerly on the main thread whatever the option says. Picking
works from the moment the scene appears, on approximate bounds: a proxy hit
reports the right node but an approximate position and normal and no facet
(`get_mesh_facet_from_triangle` returns `GEO::NO_INDEX`), which hover and
facet tools already tolerate. Mesh-sourced collision shapes need a built
`Geometry`, so `import_gltf_physics` runs after mesh finalization. Per-stage
timings log under `editor.parsers` and `erhe.gltf.log`.

`Async_raytrace_kickoff_operation` runs the deferred half, one task per mesh
node, and commits on the main thread through `Scene_commit_queue`; the shape
level of that swap and what a commit refreshes are in
doc/editor_operations.md and doc/mesh_memory_deferred_free.md.

Two process-wide serialization points exist because the work is concurrent:
the soup-to-`Geometry` conversion takes `erhe::geometry::geogram_lock()`
(doc/geogram.md), and the multi-stream buffer-mesh allocation groups and the
`Buffer_mesh` free paths take
`erhe::primitive::buffer_mesh_allocation_mutex()`, because the per-stream
vertex pools of a format must see identical allocation and free histories or
the build fails with "vertex stream allocations out of lockstep".
`Primitive_shape::make_geometry()` is lazily mutating, so it is guarded too:
an on-demand caller blocks until the build is done, and the deferred task's
own `make_geometry` then no-ops.

## Display-color rebuild

`App_scenes::rebuild_display_colors` is a kickoff, not a build. It groups the
frame's queued meshes by what decides the built bytes - source geometry or
triangle soup, color, normal style, skinned vertex format - and dispatches one
task per group through `async_for_nodes_with_mesh`, with the contract of
`deferred_finalize_mesh_items`. The worker builds the one recolored
`Primitive` of the group (renderable mesh under a narrow
`Scoped_worker_context`, then the raytrace) and enqueues a commit that swaps
it into every mesh of the group between `begin_mesh_rt_update` and
`end_mesh_rt_update` on the main thread; a mesh that left the scene by then
keeps what it has. Chaining through `async_for_nodes_with_mesh` gives the
later color of a mesh recolored while a build is in flight. A backend without
worker contexts rebuilds on the main thread.

## Cancellation

Cooperative: `Asset_load_handle::request_cancel()` sets a flag the task
notices at its next phase boundary. In-flight workers are not interrupted;
their results are discarded. A task is reaped only once settled and
`is_worker_idle()`.

Three cancellation points:

- User - the Cancel button on the load's progress bar in the Operations
  window. An in-flight load has no operation to undo yet, so cancelling is the
  only thing the user can do to it.
- Scene close - `Asset_manager::on_close_scene` cancels every load importing
  into the closing scene. It does not wait: GPU objects a cancelled load
  recorded are released through frame-completion handlers, which cannot fire
  in the same tick as the close, so waiting here would reintroduce the
  blocking drain the design removes. The task also independently notices its
  target `weak_ptr` expiring.
- Application exit - `~Asset_manager` cancels and blocks until workers are
  idle. This one has to block: the worker lambdas hold `Build_info`s whose
  `Buffer_info` carries references to `Mesh_memory`'s sinks. Member order
  cooperates - `m_asset_manager` is declared after `m_mesh_memory` in
  `Editor`, so it is destroyed first, while `Mesh_memory` is still alive.
  There is a 30 s deadline after which the tasks are leaked rather than
  destroyed under a live worker.

## Observability

`Asset_manager::get_load_task_count()` feeds an `asset_loads` term in
`App_context::get_async_in_flight_count()` and in the MCP `get_async_status`
tool. The scene is settled only when `pending`, `running`, `queued_operations`,
`pending_scene_commits` and `asset_loads` are all zero;
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
consequence: the message bus is pumped later in the tick than the asset tick
slot, so a load queued by a message first advances on the following frame.

`Prefab_library::get_or_load_async(path, on_ready)` is the prefab form.
`on_ready` may run inline - when the prefab is already cached, when the path
is bad, or when async loading is off - so callers must not assume it is
deferred. MCP `instantiate_prefab` uses exactly that distinction to decide
whether it can report a `node_id`.

## Verifying changes to this code

The check that works is: export the scene and compare against the same load
with `async_gltf_load=false`. Two pre-existing sources of nondeterminism have
to be excluded, both unrelated to async loading:

- glTF 2.1 `uid`s are minted fresh on import for files that carry none, so a
  foreign glTF never compares byte-identical. Compare ignoring `uid`.
- The `ERHE_brushes` extra-mesh export order after an import varies run to run
  in the same configuration. Ignore mesh order, or re-run.

With both excluded, async and blocking are identical in every top-level glTF
key. erhe-authored scenes, which carry uids in the file, compare
byte-identical outright.

Also count the parses in the log: `grep -c "parse_gltf '<file>'"` must be 1.
A defect that leaves the prepared parse out of the completion payload makes a
foreign open parse the file twice, and nothing else surfaces it.

`scripts/test_editor_mcp.py --unit-only` and its smoke test cover the
surrounding editor.

## Future work

- [plans/asset_loading.md](plans/asset_loading.md) - dependent child tasks, pending acquires, publish slicing, decode backpressure and the remaining synchronous entry points.
