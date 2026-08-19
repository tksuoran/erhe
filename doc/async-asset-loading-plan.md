# Asynchronous asset loading plan

Goal: a glTF load never blocks the main loop. Loading becomes a task owned by
`Asset_manager`, advanced a bounded amount every frame, so the editor keeps
ticking, stays interactive and keeps presenting frames while a scene streams
in.

Status: plan only, nothing implemented. Passed independent review. Revision 7 (incorporates six rounds
of independent review; every claim below is cited to the code it depends on).

## 1. Where we are today

The load itself is synchronous on the main thread, inside **one tick**:

- Startup `--scene`, `commands.json` `scene.load_scene`, File > Load Scene and
  the MCP `load_scene` tool all only *queue* a `Load_scene_file_message`
  (`editor.cpp:3534`, `editor.cpp:3689`, `mcp_server_file_io.cpp:149`); the
  message bus supports sync / queued / both delivery
  (`app_message_bus.hpp:16-18`) and `load_scene_file` is a **queued** channel
  (`:30`), pumped from the tick. So the load already starts on the main loop - it is not an
  init-time step. What it does *not* do is yield: the whole file loads inside
  the tick that pumps the message.
- The handler (`operations_window.cpp:885`) runs `scan_gltf`, then either
  `Scene_open_operation` - whose `execute()` registers the scene and runs the
  entire import inline (`scene_open_operation.cpp:52-87`) - or
  `open_scene_gltf` (`parsers/gltf.cpp:1226`).
- `erhe::gltf::parse_gltf` (`gltf_fastgltf.cpp:2952`) runs to completion:
  fastgltf parse, image decode (taskflow) **plus GPU texture create + upload
  on the calling thread** (`upload_decoded_image`, `gltf_fastgltf.cpp:1430+`),
  samplers, materials, cameras, lights, meshes (taskflow), nodes, skins,
  animations (taskflow), physics. Only image decode, per-mesh parse and
  animation parse are parallel (`gltf_fastgltf.cpp:1544`, `:1008`, `:1071`);
  everything else runs on the calling thread.
- `finalize_imported_meshes` (`parsers/gltf.cpp`) then builds every
  primitive's `Buffer_mesh`, and `import_gltf_physics` builds collision
  shapes (`parsers/gltf.cpp:941`, `:1380`).
- `Asset_manager::get_or_load_container` (`asset_manager.cpp:925`) does the
  same blocking parse for containers. `Asset_manager` has no per-frame entry
  point.

Consequence worth stating, because it is the strongest argument for this
work: on a large scene the first tick after the message is pumped blocks for
seconds with no event pumping and no present. A **hypothesis** (not yet
proven) is that this is what triggers the macOS presentation stall
investigated in `metal-present-stall-hardening`: the last pre-load frame is
presented, the app then stops servicing its run loop for several seconds, and
presents are never consumed again. If so, this plan fixes that hang as a side
effect. Do not treat that as established.

Three existing mechanisms this plan builds on rather than replaces:

- **`Mesh_memory`** - builders enqueue vertex/index bytes from any thread;
  `Mesh_memory::flush(command_buffer)` records them into the frame's command
  buffer once per frame, with frame-completion-gated frees
  (`mesh_memory.cpp:552-565`). The *pools* are finite and
  `make_renderable_mesh` can already fail (`parsers/gltf.cpp:618-624`); it is
  the **transfer queue** that is unbounded (`buffer_transfer_queue.cpp:38-57`
  drains everything and clears).
- **`Scene_commit_queue`** - the established cross-thread inbox: workers
  prepare detached results, the main thread applies them at the top of
  `Editor::tick()`. `Async_raytrace_kickoff_operation` already builds
  `Buffer_mesh`es **on workers** and commits through it
  (`async_raytrace_kickoff_operation.cpp:119-201`).
- **`App_context::get_async_in_flight_count()`** (`app_context.cpp:8-15`) -
  the existing idle accounting (pending/running async ops, operation-stack
  queue, commit queue), exposed to MCP as `get_async_status`
  (`mcp_server_scene_query.cpp:1552`).

`Image_transfer` is the outlier: a private 64 MiB staging ring, its own
command buffer, flushed with `submit_command_buffer_and_wait`
(`image_transfer.cpp:78`). It exists because uploads happen outside any
frame.

## 2. Target architecture

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

`Asset_manager::tick(Asset_load_tick_context&)` is called from `Editor::tick()`
immediately after `m_scene_commit_queue.flush()` (worker results land first),
with `App_context::current_command_buffer` available.

`Asset_load_tick_context` carries the frame's **shared** budget (§2.4), the
command buffer, the executor and the device. Budget is handed out round-robin
so N concurrent loads degrade gracefully instead of multiplying per-frame cost.

```
auto Asset_manager::queue_load(const std::filesystem::path&, Load_request)
    -> std::shared_ptr<Asset_load_handle>;   // state, progress, error, callbacks
```

### 2.2 Phases of `Gltf_load_task`

| # | Phase | Thread | Bounded by |
|---|-------|--------|------------|
| 0 | `read` - open file, read bytes (chunked for large GLBs) | worker | IO bytes/frame |
| 1a | `parse` - fastgltf parse; **detached** construction only (§2.3) | worker | one task; main thread polls |
| 1b | `bind` - anything main-thread-bound: asset-reference resolution, prefab / external-asset resolution, content-library entries | **main**, in tick | items/frame |
| 2 | `decode` - image decode/transcode, accessor -> triangle soup | workers, N in flight | max concurrent, **max decoded bytes in flight** |
| 3a | `build` - `Buffer_mesh` construction + `Mesh_memory` enqueue; collision shapes | workers | as 2 |
| 3b | `residency` - create `Texture`s, record texture copies, record buffer transfers | **main**, in tick | GPU upload bytes/frame, items/frame, time slice |
| 4 | `publish` - insert node tree, attach library entries, attach collision shapes | **main**, in tick | items/frame |
| 5 | `finalize` - geometry edges, smooth normals, exact raytrace | workers (existing) | unchanged |

Phases 2/3a and 3b overlap: decode and build keep running while residency
drains what is ready. That is what makes a load a stream instead of a stall.

`Buffer_mesh` construction stays on **workers** (3a), not the main thread:
`Async_raytrace_kickoff_operation` already does exactly this
(`async_raytrace_kickoff_operation.cpp:119-139`), so it is a proven path, and
only the *recording* of transfers has to be main-thread.

### 2.3 Threading invariants

1. Only the main thread touches `erhe::graphics::Device`, creates GPU objects
   or records commands, and only from `Asset_manager::tick`.
2. Workers build **detached** data only. Concretely, the following are
   main-thread-bound today and therefore belong to phase 1b / 4, not 1a:
   - `Asset_manager` asserts main thread (the private `verify_main_thread()`,
     `asset_manager.cpp:287-290`, called from ~25 sites including `acquire` at
     `:304`), so
     asset-reference resolution (`parsers/gltf.cpp:210`, `:272`,
     `asset_workflow.cpp:241`) cannot run on a worker.
   - `Prefab_library` is main-thread-only (a contract, not an assert -
     `prefab_library.hpp:45-47`; `load_template` also asserts a live frame,
     `ERHE_VERIFY(m_context.current_command_buffer != nullptr)`,
     `prefab_library.cpp:154`) and mutates the content library under a mutex
     it takes itself (`resolve_external_assets`, `prefab_library.cpp:561`;
     the caller-held contract belongs to `add_prefab_reference_entries`,
     `prefab_library.hpp:145`) - that, not reentrancy, is why it belongs in
     1b. `get_or_load` is deliberately **reentrant**: `m_active_load_stack`
     exists for cycle detection across nested loads
     (`prefab_library.cpp:118-121`, `:164`, `:184`, `:192-198`). Only
     `Prefab_library::reload` (`prefab_library.cpp:285`, declared
     `prefab_library.hpp:69`; `reload_prefab` is only the MCP tool's name) is
     non-reentrant: `ERHE_VERIFY(m_active_load_stack.empty())`
     (`prefab_library.cpp:297`). That verify becomes reachable once loads span
     frames - see §2.10.
   - Mesh-primitive mutation reaches `assert_main_thread()`
     (`draw_list_scene.cpp:236`).
   - The import path currently parses into a live `erhe::scene::Scene
     temp_scene` (`parsers/gltf.cpp:696-703`), so parsed items are *hosted*.
     Phase 1a must build into an unhosted structure instead; note that
     host-less item hierarchy / attach operations serialize on the
     process-wide `Item_host::orphan_item_host_mutex`
     (`src/erhe/item/erhe_item/item_host.hpp:21`, via `resolve_item_host_mutex`
     - construction itself is not serialized), which caps how much
     parallelism phase 1a can actually get.
3. Results cross threads through the task's result queues or
   `Scene_commit_queue`.
4. A task is never destroyed while workers it spawned are in flight;
   `cancel()` sets a flag and the manager reaps when the in-flight count hits
   zero (§2.10).

### 2.4 Budgets (`src/editor/config/definitions/load_config.py`)

`Load_config` is versioned codegen (`version=1`, every field `added_in=1`), so
new fields are `added_in=2` with a version bump.

| Field | Default | Meaning |
|-------|---------|---------|
| `async_gltf_load` | `true` | master switch; `false` keeps the blocking path |
| `gpu_upload_bytes_per_frame` | 4 MiB | texture + buffer bytes recorded per frame |
| `io_read_bytes_per_frame` | 64 MiB | file bytes pulled per frame |
| `max_decoded_bytes_in_flight` | 128 MiB | **backpressure**: stop scheduling decode above this |
| `max_residency_items_per_frame` | 64 | guard against many tiny items |
| `max_publish_items_per_frame` | 256 | node-tree insert slice |
| `load_time_slice_ms` | 4 | hard cap: checked between items, wins over byte budgets |

The two caps interact, so the rule is explicit: `load_time_slice_ms` is
checked between items and **wins**; the byte budget is the upper bound within
that slice. 4 MiB (not 16) because a memcpy into staging of that size is
already a meaningful fraction of a 4 ms slice.

`max_decoded_bytes_in_flight` is what keeps memory bounded: without it a fast
decoder queues an entire scene's decompressed pixels waiting for upload
budget. It must throttle worker *scheduling*, not just buffer results.
A suspended task (§2.10) must **not** hold decode budget while it waits on a
child; that, plus phase ordering (1b precedes 2), is what keeps the global cap
from deadlocking against dependency suspension.

### 2.5 Upload ordering: tickets and watermarks (correctness-critical)

Making `Buffer_transfer_queue::flush` a partial drain breaks the invariant
that "enqueued" implies "uploaded by end of frame", and publish would then be
able to draw a mesh whose bytes are still queued. Two rules are required:

1. **Publish gate.** `enqueue` returns a monotonically increasing ticket;
   `flush` records the highest ticket it drained as a watermark. A mesh may
   only be published once every ticket it depends on is `<= watermark`.
   The gate applies to the loader's publish only, because the budgeted drain
   is a separate entry point (§2.6); full-drain callers keep today's
   "enqueued implies uploaded this frame" guarantee.
2. **Free gate.** `Mesh_memory::flush` frees retired pool ranges from a
   frame-completion handler (`mesh_memory.cpp:552-565`). A freed range can be
   re-allocated and re-enqueued while an older transfer targeting the same
   bytes is still queued, so the stale write would land last. Pool frees must
   additionally wait until the queue has drained past the freed range's last
   write ticket. Pools are **shared** between the two queues of §2.6
   (`Mesh_memory::flush` collects retired ranges from all pools regardless of
   origin), so the gate is evaluated against the **loader** queue's watermark:
   the interactive queue is always current at flush time, but a range retired
   by the interactive path can still have a pending loader write.

### 2.6 GPU upload path - what changes, and what must NOT be deleted

- Every loader constructs its **own** 64 MiB staging ring today
  (`image_transfer.cpp:26`, constructed at `parsers/gltf.cpp:704` and
  `prefab_library.cpp:166`), so N concurrent tasks would multiply it. In the
  frame-recording mode all tasks share the device's ring
  (`Device::allocate_ring_buffer_entry`, `device.hpp:538`) and the private
  64 MiB ring is only kept for the blocking-drain callers below.
- `Image_transfer` gains a frame-recording mode: `upload(...)` records copies
  into the frame's command buffer against a ring acquired from
  `Device::allocate_ring_buffer_entry` (`device.hpp:538`), returning "budget
  exhausted" so the task resumes next frame. Ring ranges are then reclaimed by
  frame-completion, as originally designed, because the frame index advances
  during loading.
- **`Device::submit_command_buffer_and_wait` stays.** It is the only upload
  path for callers that have no frame loop running: `src/example/example.cpp`
  parses a glTF in its constructor (`example.cpp:153-161`) with a hand-rolled
  init cb (`:118-124`) before its loop starts (`:297`), and
  `src/rendering_test` does not call `parse_gltf` at all; it constructs an
  `Image_transfer` (`rendering_test.cpp:92`) and flushes `Mesh_memory` into an
  init cb (`rendering_test_setup.cpp:109`, `:190`) - and is currently out of
  the default build (`src/CMakeLists.txt:76` comments out its
  `add_subdirectory`).
  `Image_transfer` therefore keeps a documented **blocking-drain** mode. The
  complete `parse_gltf` caller set, with each caller's disposition:

  | Caller | Disposition |
  |---|---|
  | `parsers/gltf.cpp:716` (import), `:1271` (open scene) | converted (§2.12) |
  | `asset_manager.cpp:969` (container) | converted (§2.10) |
  | `prefab_library.cpp:176` (template) | converted (§2.10) |
  | `example.cpp:153` | blocking-drain: no frame loop yet (init cb at `:118-124`, loop at `:297`) |
  | `xr/controller_visualization.cpp:225` | blocking-drain **exemption**, see below |

  "The editor stops using the blocking path" is therefore **not** true as a
  blanket statement: `controller_visualization.cpp:214` constructs its own
  `Image_transfer` and loads from a live tick. The exemption is deliberate -
  a small controller GLB already in memory (`parallel=false`, `glb_data`) -
  but it has two consequences: step 3 (CPU-only `parse_gltf`) must give that
  caller the residency drain loop, and `submit_command_buffer_and_wait` keeps
  an editor-side caller.
  (Aside: `rendering_test.cpp:92` constructs `Image_transfer` with two
  arguments against a one-argument constructor (`image_transfer.hpp:29`) -
  that target appears broken/out of the default build; check before relying
  on it.)
- Oversize single images upload mip level by mip level across frames; only a
  single level larger than the whole budget takes a one-shot dedicated
  staging buffer, released on frame completion.
- **The loader gets its own transfer queue; existing flushes are untouched.**
  A partial drain of the shared queue is not viable: `Mesh_memory::flush`
  unconditionally drains it (`mesh_memory.cpp:525`) and callers build a
  mesh and then draw it **in the same command buffer** with no ticket check -
  `rendertarget_mesh.cpp:205`, `brush_preview.cpp:259`,
  `scene_builder.cpp:116` / `:772` / `:908` (the last conditional on
  `current_command_buffer != nullptr`). `example.cpp:179` and
  `rendering_test_setup.cpp:108` / `:189` need full-drain for a different
  reason - they flush into a dedicated init cb that is ended, submitted and
  `wait_idle`-ed before rendering (`example.cpp:218-221`,
  `rendering_test.cpp:174-175`). But merely keeping the shared queue
  full-drain does not bound the loader either: `editor.cpp:757` drains it
  unconditionally once per tick - deliberately, "uploads enqueued during a
  hidden tick must not sit in the queue until the next rendered frame"
  (`editor.cpp:754-756`) - and that call sits ~150 lines **after** the
  proposed `Asset_manager::tick` slot at `editor.cpp:606`. Since the loader
  shares that queue (`make_import_build_info` uses
  `mesh_memory->make_primitive_buffer_info()`, `parsers/gltf.cpp:531`), a
  4 MiB budgeted drain at `:606` would be followed by a full drain of
  everything the workers queued since, up to `max_decoded_bytes_in_flight`
  (128 MiB). Not corruption - the watermark still advances - but
  `gpu_upload_bytes_per_frame` would be a no-op for all vertex/index data,
  which is half of what this plan exists to bound.

  So: `Mesh_memory` gains a **second transfer queue for loader traffic**,
  selected **by the task**, not by `make_import_build_info` as such. The rule
  is: *only traffic whose publish honours the watermark (§2.5) may use the
  loader queue.* `make_import_build_info` (`parsers/gltf.cpp:531`) therefore
  takes a queue selector. Note what that implies mechanically: `Mesh_memory`
  *is* the `Vertex_buffer_sink` / `Index_buffer_sink`
  (`mesh_memory.hpp:74-76`) and `Buffer_info` holds a sink reference, not a
  queue handle - so the selector means a second sink adapter (or a per-sink
  queue field), not merely a parameter. Everything that publishes immediately
  keeps the interactive queue - `async_gltf_load = false` (§2.4), which still calls
  `finalize_imported_meshes` and publishes in the same tick;
  `Prefab_library::load_template` (`prefab_library.cpp:188`) until step 7
  converts it; and `controller_visualization`. Without this the master
  switch's "off" position, and the whole window between step 5 and step 7,
  would draw geometry from buffers trickling in at
  `gpu_upload_bytes_per_frame`. `Mesh_memory::flush` keeps full-drain semantics on the
  interactive queue for every existing caller, `editor.cpp:757` included; a
  new `Mesh_memory::flush_budgeted(command_buffer, budget)` drains **only**
  the loader queue and is called only from `Asset_manager::tick`. Tickets and
  watermarks (§2.5) are per-queue. The ticket lands in the same step as
  `flush_budgeted` - a budgeted drain without it is a correctness regression,
  not an intermediate step.
  (Rejected alternative: make `editor.cpp:757` the budgeted drain. It would
  silently delay non-loader uploads enqueued during hidden ticks, which is
  exactly what that call exists to prevent.)

Side effect: the current pattern of minting dozens of command buffers inside
one never-advancing device frame disappears, and with it the load-time queue
pressure on Metal.

### 2.7 Visibility policy while loading

- **Textures: use the existing per-frame indirection, not a clear.**
  `Material_texture_sampler::texture_reference` is a
  `std::shared_ptr<erhe::graphics::Texture_reference>` resolved **every frame**
  in `Material_buffer::update` (`material.hpp:22-30`,
  `material_buffer.cpp:174-186`), and a null resolve already yields
  `invalid_texture_handle`. So a `Pending_texture_reference` that returns
  null (or a shared dummy) until the pixels land carries the material through
  loading with no render-side change. (`Device::create_dummy_texture` takes an
  `init_command_buffer` (`device.hpp:535`), so a shared dummy is created once
  at init, never per pending texture.)
  **The wrapper must be temporary, not permanent.** The per-frame resolve is
  the *render* path only; several consumers depend on the stored object's
  dynamic type and shared_ptr identity, not on `get_referenced_texture()`:
  `content_library.cpp:264` dynamic-casts the reference to `Item_base` for the
  combo preview name and `:292` compares it by shared_ptr identity against the
  library entry; `clipboard.cpp:105` dynamic-casts it to `Item_base` to pin
  textures on copy; `mcp_server_scene_query.cpp:1298-1299` dynamic-casts it to
  `Texture` / `Graph_texture` for `texture_id`. A permanently installed
  wrapper would show "(unnamed)", never match the library entry, stop pinning
  on copy, and report a null texture id - forever. (The glTF export path is
  safe: it goes through `get_referenced_texture()`,
  `gltf_fastgltf.cpp:4846-4849`.)
  **Ordering invariant, because publish consumes the textures too.** Phase 4
  attaches content-library entries built from `gltf_data.images` as
  `shared_ptr<erhe::graphics::Texture>` plus `image->get_name()`
  (`parsers/gltf.cpp:306-325`; likewise `prefab_library.cpp:368` / `:647`.
  `asset_workflow.cpp:67-69` is not a library entry - it is
  `make_gltf_data_image_source_provider`, which keys a map by `Texture*` - but
  it needs the real `Texture` for the same reason). So phase 3b must **create the `Texture` object
  for every image before publish** - creation is cheap; only the pixel copies
  may lag under the byte budget. The library entry, the `Gltf_source_reference`
  and `Gltf_data::images` always hold the real `Texture`; the pending wrapper
  lives **only** in `Material_texture_sampler::texture_reference`. Publishing
  with a wrapper in `images` would either lose the library entry or bind it to
  a different object than the one later swapped into the sampler - exactly the
  identity break described above.
  Between publish and the swap those four consumers see the wrapper, so the
  combo shows "(unnamed)", the identity compare fails and copy does not pin
  that texture. That window is bounded by the upload budget and is accepted;
  what is not acceptable is making it permanent.
  So the residency step **replaces** the pending wrapper in the sampler slot
  with the real `erhe::graphics::Texture` shared_ptr - a main-thread slot
  assignment, safe because the render resolve is per-frame. That is the swap
  plumbing, and it is one assignment; what it is not is a change to the four
  consumers above.
  The previously proposed "create the texture and clear it to a solid color"
  is not implementable here: clearing needs `use_clear_texture`
  (GL >= 4.4 / `GL_ARB_clear_texture`, `gl_device.cpp:384` - **false on macOS
  GL 4.1**) or the render-pass fallback, which would require render-target
  usage on every imported texture (they are created `sampled |
  transfer_dst`, `gltf_fastgltf.cpp:1430-1447`), and it is impossible for the
  block-compressed images this parser produces (`KHR_texture_basisu`,
  `MSFT_texture_dds`, `EXT_texture_webp`, `gltf_fastgltf.cpp:2973-2982`;
  compressed paths are already special-cased at `:1451` and
  `image_transfer.cpp:114`).
- **Meshes**: a node is published once its fill `Buffer_mesh` is resident *and
  past the watermark* (§2.5). Edge lines and exact raytrace continue to
  arrive through the existing deferred pipeline.
- **Publish granularity**: publish the node tree **once**, when structure and
  fill meshes are resident - not per mesh. `max_publish_items_per_frame`
  (§2.4) is therefore a *slice of one atomic publish*, not a licence to
  interleave: the scene mutation happens under one operation, and if a huge
  tree exceeds the slice the slice loses (publish overruns the frame) rather
  than the atomicity. Keeps undo/redo atomic (§2.8) and
  stops the scene mutating under tools mid-import.
- **Mid-stream pool exhaustion**: `make_renderable_mesh` can fail
  (`parsers/gltf.cpp:618-624`). With publish-once this fails the whole load
  cleanly before publish; that ordering is a reason to keep publish-once.

### 2.8 Operations, undo, and record adoption

Operations stay synchronous and cheap; only loading is async. The completion
callback uses the existing cross-thread inbox
`Operation_stack::queue_from_thread` (`operation_stack.hpp:78`), not a new
mechanism.

Two orderings make this more than "operation takes ready data":

- **`Scene_root` before parse.** `open_scene_gltf` must read the `ERHE_scene`
  payload (`enable_physics`) *before* the `Scene_root` can be constructed
  (`parsers/gltf.cpp:1234-1240`, `:1318`). So the task owns the parse, and the
  handle exposes the parsed scene-settings payload; the completion callback
  constructs the `Scene_root` and then hands the parse over.
- **Record adoption (R5.7).** `make_import_gltf_operation` may adopt an
  `Asset_manager` container record instead of parsing
  (`parsers/gltf.cpp:664-694`, `take_adopted_parse`), and that requires the
  `Scene_root` to be registered first - `Scene_open_operation::execute`
  registers the scene, then builds and executes the import
  (`scene_open_operation.cpp:52-87`). The task must therefore check for an
  adoptable record at **queue time** and, when one exists, skip phases 0-3
  entirely and complete immediately. The plan's phases apply to a fresh parse
  only. The decision must be **re-validated at completion**: a record can be
  courtesy-unloaded between queue time and completion
  (`asset_manager.cpp:915-922`), so the task falls back to a fresh parse if
  the record is gone.
- `resolve_material_asset_references` / `acquire_import_materials_as_references`
  (`parsers/gltf.cpp:736-739`) call `Asset_manager` and so run in phase 1b on
  the main thread, before publish.

### 2.9 Asset_reference needs a pending state

`Asset_resolve_state` is `{unresolved, resolved, failed}`
(`asset_reference.hpp:17-21`) and `resolve()` **latches `failed`** for
file-scope keys when `acquire` returns null (`asset_reference.cpp:140-147`).
If `get_or_load_container` becomes async, "not loaded yet" would latch as a
permanent failure.

Required: add `pending`. `acquire` on a not-yet-loaded container returns
null + pending (never `failed`), the reference retries on later frames, and
only a real load failure latches. `reset_resolution()` already exists
(`asset_reference.hpp:74`) and stays the manual escape hatch.

**That alone is not sufficient**, because the parse-time substitution paths
call `Asset_manager::acquire` *directly*, not through `Asset_reference`:
`resolve_material_asset_references` (`parsers/gltf.cpp:210`) and
`acquire_import_materials_as_references` (`:272`). On a null acquire the
latter logs a warning and `continue`s, "keeping the imported definition"
(`gltf.cpp:273-279`) - so a not-yet-loaded container would silently turn
references into copies. These call sites must distinguish pending from
failure and **suspend the task** (§2.10) instead of falling through to the
copy path.

**`Asset_manager::acquire` itself becomes pending-capable**, because it calls
`get_or_load_container` internally for every file-scope key
(`asset_manager.cpp:314`). So a first touch of an unloaded container returns
"null + pending" to *every* `acquire` caller, and these consume the return
value synchronously today:
`asset_workflow.cpp:241` (`reference_material_into_scene`, reached from the
asset browser, the `Scene_root` context menu and MCP - a null acquire is a
user-visible error string today),
`Asset_manager::debug_acquire` (`asset_manager.cpp:1809`) and its MCP caller
(`mcp_server_assets.cpp:149`),
and the one-argument `acquire` overload (`asset_manager.cpp:295`).
The contract: `acquire` keeps a **synchronous load-or-fail** variant for these
verbs, which spawns the task and reports "loading, retry" rather than
"failed", plus the **pending-returning** variant used by `Asset_reference` and
the parse-time substitution sites. Both are listed in §2.12.

This also resolves an otherwise circular requirement: §2.12 converts
`get_or_load_container` to async, while §2.8 needs phase 1b to finish before
publish. The resolution is that 1b is a suspend point and the substitution
step is **re-run from the start** when the child settles - it is idempotent
(it re-derives keys from the parse and substitutes), whereas resuming
mid-iteration would need to remember which materials were already
substituted.

### 2.10 Child loads are dependent tasks, not nested blocking loads

`Prefab_library::load_template` (private, `prefab_library.cpp:150`, reached
via `get_or_load`, `:101`) is a **second complete blocking loader**: it
constructs its own `Image_transfer` (`prefab_library.cpp:166`), calls
`parse_gltf` (`:177`) and `finalize_imported_meshes` (`:188`), then recurses
through `resolve_external_assets` -> `get_or_load` (`:546`, `:192-198`).
Leaving that inside phase 1b would load an entire nested prefab chain
synchronously inside `Asset_manager::tick` - exactly the stall this plan
exists to remove - and four of §2.12's entry points go through it
(`viewport_window.cpp:287`, `item_tree_window.cpp:491`,
`src/editor/asset_browser/asset_browser.cpp:340`, MCP `instantiate_prefab`).

Required shape, and it is on the critical path of step 7, not an open
question:

- `Prefab_library::get_or_load` (`prefab_library.cpp:101`) and
  `Asset_manager::get_or_load_container`
  become **task-spawning**: when the target is not loaded, they create a child
  `Asset_load_task` and return "pending" with the child handle.
- Phase 1b is a **suspend point**. A task that hits a pending child records
  the dependency, returns `running` from `tick()` and is not advanced again
  until every child handle has settled. The parent then **re-runs the whole
  1b step** (see §2.9 for why re-running, not resuming mid-step, is required).
- **`Prefab_library::reload`'s empty-stack verify must be replaced.**
  `ERHE_VERIFY(m_active_load_stack.empty())` (`prefab_library.cpp:297`) holds
  today only because reload is a top-level synchronous operation. Once loads
  span frames, a reload issued while any prefab load is in flight would either
  abort on the verify or tear a template mid-load. Replace it with a
  dependency-graph query: reload is **rejected (or queued) while a task
  targeting that path is live**, and the MCP `reload_prefab` tool reports that.
- Container loads are **leaf** tasks: `get_or_load_container` deliberately
  does not resolve `ERHE_asset_reference` transitively
  (`asset_manager.cpp:983-990`), so only the `Prefab_library` side needs
  dependency-graph cycle detection.
- `m_active_load_stack` cycle detection (`prefab_library.cpp:118-121`) must
  move from a call-stack property to a property of the dependency graph: a
  child task whose path is already an ancestor in the graph fails the same way
  the recursive check does today.
- Prefab template construction still touches the content library on the main
  thread, so a child prefab task's own 1b/publish phases run in the parent's
  tick slice like any other task.
- **Child tasks are shared, so they are refcounted by their dependents.**
  Both spawners are content-addressed caches - `prefab_library.cpp:108-112`
  returns an existing prefab, `asset_manager.cpp:928-937` an existing record -
  so two parents can legitimately depend on one child, and a child targets a
  *file*, not a scene. Therefore: cancel propagates to a child only when its
  **last** dependent cancels; a cancelled or failed child **settles** its
  remaining parents as failed instead of leaving them suspended forever; and
  invariant 4 of §2.3 extends to dependents - a task is not destroyed while
  any parent still references it. Without this, closing one scene (§2.11) can
  cancel a child that another scene's load is suspended on.

### 2.11 Cancellation

Copy the one working precedent: `Lightmap_partitioner::on_scene_closed` uses
an `std::atomic<bool> cancel_requested` checked inside worker tasks plus
`future.cancel()` (`lightmap_partitioner.cpp:1099-1110`, `:232`, `:266`).

**GPU objects a cancelled load already recorded must outlive the frame.**
Residency records texture copies into the frame's command buffer (§2.6), so
dropping those `shared_ptr<Texture>` (or ring entries) on cancel would destroy
a resource an unfinished command buffer still references. The rule: anything a
load created and recorded against is released from a
`Device::add_completion_handler` (`device.hpp:500`) - the same mechanism that
gates pool frees at `mesh_memory.cpp:552` - and the reap below gates on frame
completion **as well as** on the worker in-flight count of §2.3 invariant 4.

Two reap points are needed, not one (alongside the existing per-tick
`purge_completed_item_async_tasks()`, `editor.cpp:739`). `Editor::on_close_scene`
(`editor.cpp:3219+`) currently neither cancels nor waits for background work. It must cancel every task targeting the closing
scene and reap before the scene-close leak watchdog **checks** - arming
(`editor.cpp:3347`, `:3370`) only collects weak references, and the check runs
`k_scene_close_leak_check_frames = 60` frames later (`editor.cpp:3375`,
`:3969`). That looser deadline is the satisfiable one: GPU objects are
released from frame-completion handlers, which cannot fire in the same tick as
the close, so requiring the reap before *arming* would force a blocking drain
at close time - reintroducing exactly the stall this plan removes. The second is
**application exit**: `~Asset_manager` already has teardown ordering with
`Asset_reference` (`asset_manager.cpp:275-283`) and must cancel and reap
in-flight tasks there too, or §2.3 invariant 4 is violated at shutdown.

### 2.12 Completion semantics: what breaks, and the replacement

- **`wait_for_idle(timeout)` is new** (nothing of the sort exists today; the
  current idiom is polling `get_async_status`, e.g.
  `scripts/test_editor_mcp.py:82-85`, `scripts/creations/common.py:258`). It
  extends the existing accounting rather than adding a parallel one: add an `asset_loads` term to
  `App_context::get_async_in_flight_count()` (`app_context.cpp:8-15`) and
  surface it in the existing `get_async_status` MCP tool
  (`mcp_server_scene_query.cpp:1552`) - no new query tool. "Idle" must
  additionally require the transfer queue drained past the watermark and
  pending texture uploads recorded, otherwise idle still means "geometry not
  on the GPU".
- **Callers that assume a load finished on return** (all must be converted or
  documented):
  `import_gltf` (`parsers/gltf.cpp:1017-1026`),
  `Scene_open_operation::execute` (`scene_open_operation.cpp:84-87`),
  asset-browser Import / Instantiate / scan
  (`src/editor/asset_browser/asset_browser.cpp:279`, `:305`, `:340`),
  drag-and-drop prefab loads (`viewport_window.cpp:287`,
  `item_tree_window.cpp:491`),
  `Asset_manager::get_or_load_container` (`asset_manager.cpp:925`),
  and `Asset_manager::acquire` itself (`asset_manager.cpp:302-330`) with its
  synchronous consumers `asset_workflow.cpp:241`, `debug_acquire`
  (`asset_manager.cpp:1809`) and `mcp_server_assets.cpp:149` (§2.9).
  Already pure queue sites, and therefore **not** in scope: the file-dialog
  callback (`operations_window.cpp:2634`), the prefab-instance context menu
  (`scene_root.cpp:972`) and `asset_browser.cpp:355` / `:368` - all of which
  only `queue_message` / `operation_stack->queue`, like `--scene` and MCP
  `load_scene`.
- **MCP.** `load_scene` and `open_scene` are *already* async
  (`mcp_server_file_io.cpp:149`, `:182`, both return `{"queued": true}`). The
  ones that genuinely break are `import_gltf` (`:278`), `instantiate_prefab`
  (returns the created `node_id`, `:347-366`), `reload_prefab` (`:370`) and
  `load_asset_file` (returns record id + counts, `mcp_server_assets.cpp:356`).
  Each either waits on the handle or returns a handle id plus a documented
  poll.
- **Tests.** `scripts/scene_roundtrip_verify.py:689-692` asserts a synchronous
  import; its `load_scene` uses are already poll-based (`:337`, `:356`,
  `:961`). See §4 for what "identical results" can mean here.
- **The `Load_scene_file` handler's own `scan_gltf` must move into the task.**
  `operations_window.cpp:892` calls `editor::scan_gltf` before any task could
  exist, and `erhe::gltf::scan_gltf` (`gltf_fastgltf.cpp:3443-3487`) does a
  whole-file read (`GltfDataBuffer::FromPath`) plus a full fastgltf JSON parse
  **on the main thread**. That is the flagship path - File > Load Scene,
  `--scene`, `commands.json` and MCP `load_scene` all land there - so leaving
  it makes "a load never blocks the main loop" false for the primary entry
  point even after everything else ships. The scan folds into phase 0/1a: the
  task reads and parses once, and the erhe-scene-or-foreign decision comes
  from the task's own parse instead of a separate pre-scan. The MCP `scan_gltf`
  *query* tool (`mcp_server_file_io.cpp:302`) does the same whole-file read and
  parse on the main thread; it is a query rather than a load, so leaving it
  synchronous is defensible - but after step 8 it is the last main-thread
  whole-file parse in the editor, and that should be a deliberate decision.
- **Startup.** `--scene` and `commands.json` already queue
  (`editor.cpp:3534`, `:3689`); what changes is that the load no longer
  completes inside tick 1. Script commands that follow a `scene.load_scene`
  must chain off the handle's completion instead of assuming the next command
  sees the scene.

### 2.13 Physics / collision shapes

`import_gltf_physics` must run after mesh finalization because mesh-sourced
collision shapes need the built `Geometry`
(`gltf_physics_import.hpp:20-22`) - and `build_shape_from_mesh` reaches the
**non-const** `render_shape->get_geometry()`, which *creates* the Geometry
from the triangle soup on demand (`collision_shape_from_mesh.cpp:25-26`),
plus `make_convex_hull`. That is precisely the conversion `deferred_edge_lines`
pushes to workers (`parsers/gltf.cpp:604-607`), and the deferred finalize task
builds the same Geometry concurrently
(`async_raytrace_kickoff_operation.cpp:119-139`).

So collision-shape construction becomes a **phase 3a worker output** consumed
by publish, and the Geometry must be built **once**: a per-primitive
"geometry ready" shared future that both the collision-shape build and the
deferred finalize await, rather than each calling `get_geometry()`.

## 3. Implementation steps

1. **Tick skeleton.** `Asset_load_tick_context`, `Frame_load_budget`,
   `Asset_manager::tick()` wired into `Editor::tick()` after
   `scene_commit_queue.flush()` (`editor.cpp:606`). No tasks, no behavior
   change. Note the ordering consequence: the message bus is pumped later in
   the tick (`editor.cpp:733`), so a load queued by a message first advances
   on the *following* frame - which is what "a handle exists when the queueing
   call returns" has to mean for the MCP poll contract (§2.12).
2. **Completion plumbing scaffolding.** `wait_for_idle`, the `asset_loads`
   term in `get_async_in_flight_count`, `get_async_status`, MCP wait/poll for
   the four breaking tools, startup-script chaining, and the `pending` state
   in `Asset_reference` (§2.9). This must land **before** any entry point
   flips, or the roundtrip script and MCP tools break with no replacement -
   but be honest about what it is: until step 6 there are no tasks, so
   `asset_loads` is always zero, no handle exists to wait on and nothing ever
   produces `pending`. **Step 2 ships inert scaffolding whose behavior is
   first exercised in step 6**; its acceptance test is that step 6 turns it on
   without further API change. The alternative - folding the handle-facing
   half into step 6 behind `async_gltf_load=false` - is acceptable and
   arguably cleaner; what is not acceptable is flipping entry points (step 7)
   before it exists.
3. **Split GPU out of the parse.** `parse_gltf` becomes CPU-only: it produces
   decoded payloads and no GPU objects, and residency is a separate step that
   creates the `Texture`s and populates `Gltf_data::images`. `images` keeps
   its `shared_ptr<Texture>` type (§2.7 requires real textures by publish);
   what changes is *when* it is filled, not what it holds.
   Note the blast radius: `Gltf_data::images` is public and consumed by
   materials, the round-trip export and the second backend header
   (`gltf_none.hpp`, the ERHE_GRAPHICS_API_NONE backend's own `Gltf_data`),
   and the consumers that take `images[i]` as a `shared_ptr<Texture>`:
   `parsers/gltf.cpp:306-325`, `prefab_library.cpp:368` / `:647`,
   `asset_workflow.cpp:67-69`. Keep behavior identical by having callers drain
   residency in a loop.
4. **Frame-recorded uploads.** `Image_transfer` records into the frame command
   buffer with a byte budget, keeping the documented blocking-drain mode for
   example / rendering_test / headless (§2.6). Editor stops using
   `submit_command_buffer_and_wait`.
5. **Budgeted transfers + tickets together.** The loader transfer queue,
   `Mesh_memory::flush_budgeted`, the ticket/watermark publish gate and the
   pool-free gate (§2.5) in one step - the budget alone is a correctness
   regression. Nothing routes into the loader queue yet: until step 6 exists
   to honour the watermark, every build path keeps the interactive queue
   (§2.6), so this step is inert by construction.
6. **`Gltf_load_task`.** Phase machine, worker fan-out, backpressure,
   cancellation (§2.10), progress, adoption short-circuit (§2.8), collision
   shapes as a worker output with the shared geometry future (§2.13). Behind
   `async_gltf_load`.
7. **Convert the entry points** listed in §2.12.
8. **UI progress** from the handle (status bar / Operations window), and
   `Asset_browser::ensure_scanned`'s synchronous `scan_gltf` inside ImGui
   iteration (`src/editor/asset_browser/asset_browser.cpp:279`) as a second customer of the queue.

## 4. Risks and open questions

- **Child loads.** glTF 2.1 external assets and prefabs load further files;
  tasks need dependency edges and cycle detection (`Prefab_library` does this
  today at parse time, main-thread and non-reentrant).
- **Determinism - name the observable.** Byte-identity of anything derived
  from GPU buffer offsets is already unachievable: `Buffer_mesh` allocation
  order is nondeterministic today via
  `Async_raytrace_kickoff_operation`'s worker-side build
  (`async_raytrace_kickoff_operation.cpp:131-138`). What must match the
  blocking path is the **exported glTF and the scene graph**; `wait_for_idle`
  must be exact, or the round-trip tests become flaky.
- **Phase 1a parallelism** may be limited in practice by
  `orphan_item_host_mutex` serialization; measure before assuming a win.
- **Partial-publish failure modes.** Publish-once bounds this, but a failure
  after significant residency still has to release what it took - pool ranges,
  textures and ring entries alike, all through frame-completion handlers
  (§2.11).
- **Undo of an in-flight load** - the operation only exists after completion,
  so an undo issued during load has nothing to undo; the UI must show the
  load as cancellable instead.
