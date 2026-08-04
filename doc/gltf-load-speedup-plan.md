# glTF load/import speedup plan

**Status (2026-08-04): implemented** (all three parts + `Load_config`
options, defaults on). Measured on hintze-hall_-_vr_tour_1k.glb, headless
Vulkan Debug build: blocking load time 139 s -> 10 s (parse 1105 ms -> 349 ms;
`finalize_imported_meshes` 137.9 s -> 9.8 s, remainder moved to parallel
background tasks), raycast picking verified identical before/after the
deferred tasks complete. Per-stage timings log under `editor.parsers` /
`erhe.gltf.log`.

Goal: make importing/opening glTF files fast by deferring non-essential work
(node raytrace, geometry edge lines) to background taskflow tasks that start
only after the file itself has finished loading, and by parallelizing more of
the parse itself. Every deferral gets a config option that restores the old
serial/eager behavior.

## Current pipeline (as of 4af99c02)

Both entry points funnel through the same machinery, on the main thread:

- `import_gltf()` (src/editor/parsers/gltf.cpp) builds a compound operation:
  `parse_gltf` + `finalize_imported_meshes` run inline in
  `make_import_gltf_operation`, then sub-ops execute via the operation stack.
- `open_scene_gltf()` runs the same steps inline (load_scene_file handler in
  operations_window.cpp).

Stages and their current threading:

1. `erhe::gltf::parse_gltf` (`Gltf_parser::parse_and_build`,
   src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp):
   - fastgltf parse + buffer load: serial.
   - samplers, materials, cameras, lights: serial loops.
   - **images: decoded and GPU-uploaded lazily inside `parse_material` via
     `get_image()` — fully serial, decode (stb/ktx) on the loading thread,
     upload through `Image_transfer` + command buffer.**
   - meshes: already parallel (one taskflow task per mesh, dedup of shared
     primitives under `m_primitive_entries_mutex`).
   - nodes, skins, animations, physics: serial.
2. `finalize_imported_meshes` (src/editor/parsers/gltf.cpp): serial per
   primitive, main thread. For the common Triangle_soup path,
   `render_shape->get_geometry()` **lazily converts the soup to a full
   `erhe::geometry::Geometry`** (`Primitive_shape::make_geometry`:
   `mesh_from_triangle_soup` + tangent computation +
   `process(connect | build_edges | compute_smooth_vertex_normals |
   compute_facet_centroids)`). This is the geometry/edge-lines build and is
   the dominant serial CPU cost. `make_renderable_mesh` then builds the GPU
   buffer mesh from the Geometry (fill + edge lines + points).
3. `import_gltf_physics`: mesh-sourced collision shapes read the Geometry
   (comment: "must run after mesh finalization").
4. `Async_raytrace_kickoff_operation` (last sub-op): already deferred to
   `tf::Executor` via `async_for_nodes_with_mesh` (items.cpp), but it is
   **one single task for the whole import** that holds
   `scene_root->item_host_mutex` across every mesh while
   `Primitive::make_raytrace()` builds a BVH per primitive. Until it
   completes there is no raytrace at all (no picking/hover on the new
   content), and one worker does all the BVH builds serially.

Key structural facts the plan builds on:

- `Mesh::update_rt_primitives()` skips primitives whose
  `Primitive_raytrace` has no geometry — the editor already renders and runs
  fine with raytrace absent.
- `Primitive_raytrace` owns its own CPU-side vertex/index buffers; swapping
  the rt geometry under `Scene_root::begin/end_mesh_rt_update` is the
  established pattern (async raytrace kickoff, mesh operations).
- `get_mesh_facet_from_triangle()` returns `GEO::NO_INDEX` when element
  mappings are absent; hover/facet tools must already tolerate that.

## Part 1 — deferred node raytrace with AABB proxy

At load time, give every mesh primitive a cheap axis-aligned-bounding-box
proxy raytrace (12 triangles, trivially small BVH), so picking/hover work the
moment the scene appears. Queue per-mesh tasks that build the real
triangle-accurate raytrace and swap it in; tasks start only after the glTF
has finished loading (this ordering already holds: the kickoff is the last
sub-operation of the import compound / the last step of open_scene_gltf).

Changes:

1. `erhe_primitive`:
   - Add `Primitive_raytrace::make_aabb_proxy(const erhe::math::Aabb&)`
     (or a constructor): fill `m_rt_vertex_buffer`/`m_rt_index_buffer` with
     the 8 box corners / 12 triangles, commit. Add `is_proxy()` flag so
     consumers and the replacement task can tell proxy from real.
   - Add `Primitive_shape::make_raytrace_proxy(const Aabb&)` and
     `Primitive::make_raytrace_proxy()`. The AABB is available without
     Geometry: `render_shape->get_renderable_mesh().bounding_box` (computed
     by `build_buffer_mesh_from_triangle_soup`), falling back to the
     Triangle_soup positions scan.
2. `finalize_imported_meshes`: after `make_renderable_mesh`, call
   `make_raytrace_proxy()` (option-gated). `update_rt_primitives()` then
   picks the proxy up; `register_mesh` attaches it to the raytrace scene as
   usual.
3. Replacement tasks: rework `Async_raytrace_kickoff_operation` to launch
   **one task per mesh node** instead of one task for the whole list, so the
   work spreads across executor workers and the scene lock is held only for
   the short swap, not the whole build:
   - Build the real `Primitive_raytrace` **outside** the lock (BVH build is
     the expensive part and touches only primitive-local data).
   - Then take `item_host_mutex`, `begin_mesh_rt_update` (detaches proxy
     instances), move the new raytrace into the shape,
     `update_rt_primitives`, `end_mesh_rt_update`.
   - Implementation: either loop calling `async_for_nodes_with_mesh` with a
     single-item vector per node, or add a batched variant in items.cpp that
     creates one `silent_dependent_async` per item (keeps the per-item
     dependency chaining in `s_item_tasks` intact).
4. Hover/pick correctness while the proxy is live: proxy hits report the
   right node but approximate position/normal and no facet
   (`get_mesh_facet_from_triangle` → NO_INDEX). Audit the consumers
   (hover_tool, physics_tool, transform tools, mcp scene query) — they must
   already tolerate missing mappings; verify no crash paths assume real
   triangles.
5. Thread-safety fix required: `Primitive_shape::make_geometry()` /
   `get_geometry()` are lazily mutating and unguarded. With deferred tasks
   AND on-demand callers (properties window, geometry ops, physics tool)
   possible at the same time, guard geometry creation with a mutex or
   `std::once_flag` in `Primitive_shape`.

## Part 2 — deferred geometry + edge lines build

Skip the soup→Geometry conversion at load time entirely; render immediately
with fill triangles only, and build Geometry + edge lines in the same
deferred per-mesh tasks.

Changes:

1. `finalize_imported_meshes` (option-gated):
   - Do NOT call `render_shape->get_geometry()` for soup-backed primitives.
   - Build the renderable mesh from the soup directly:
     `make_renderable_mesh` already falls through to
     `make_buffer_mesh(buffer_info)` when no Geometry exists — fill
     triangles, no edge lines, bounding box computed. The content edge-line
     renderer already skips empty index ranges.
   - ERHE_geometry-extension primitives (byte-exact restored Geometry with
     edges) keep the current path — nothing to defer.
2. Physics constraint: `import_gltf_physics` needs Geometry for mesh-sourced
   collision shapes. Before deferring, collect the set of primitives
   referenced by physics colliders and build their Geometry eagerly (they
   are typically few); everything else defers.
3. Deferred per-mesh task (shared with Part 1 — one task graph per mesh):
   1. `render_shape->make_geometry()` (soup → GEO::Mesh, tangents,
      connect/build_edges/smooth normals) — CPU-only, parallel-safe.
   2. Build the real raytrace from that Geometry (Part 1 swap).
   3. Rebuild the renderable buffer mesh **with** edge lines / corner points
      and swap it in. This is the delicate step:
      - GPU vertex/index allocation goes through `Mesh_memory` buffer sinks;
        confirm the sink is safe off-thread or marshal the allocation+upload
        to the main thread (pattern: worker computes, queues an operation on
        the operation stack, `Operation_stack::update()` applies on the main
        thread — same as existing mesh operations).
      - Free the old fill-only allocation; watch the vertex-pool lockstep
        invariant (cb73aab4) — all streams of a mesh must stay in one pool
        block.
      - Swap under `item_host_mutex` so the renderer never sees a
        half-updated `Buffer_mesh`.
   - Alternative considered and rejected for v1: appending only an edge-line
     index range to the existing allocation — smaller GPU churn but breaks
     the single-allocation assumption in `Buffer_mesh`; revisit if rebuild
     proves too costly.

## Part 3 — more parallelism inside parse_gltf

Meshes are already parallel. The remaining serial hotspots, in likely order
of payoff:

1. **Images** (biggest win for textured scenes): today decoded lazily,
   serially, inside material parsing.
   - Pre-scan materials to determine, per image index, whether it is used
     and whether it needs sRGB or linear (this is exactly what the lazy
     `get_image(linear)` call encodes today).
   - Decode all used images in a taskflow parallel-for (Image_loader decode
     into per-task staging memory).
   - Keep GPU work on the loading thread: texture creation +
     `Image_transfer::upload_to_texture` consume decoded staging data as
     tasks complete (single command buffer, not thread-safe). Either a
     completion queue drained on the loading thread, or decode-all
     (`future.wait()`) then upload in a serial loop.
   - Then parse materials (now just parameter copies + texture lookups).
2. **Animations**: independent per animation (accessor copies) — one task
   each.
3. **Primitive dedup**: `get_primitive_geometry` currently checks the dedup
   cache then builds outside the lock, so two meshes sharing accessors can
   build the same soup twice concurrently. Store a per-entry future/latch in
   the cache so duplicates wait instead of re-building.
4. Keep serial: node hierarchy (ordering- and parent-sensitive), skins
   (parsed just-in-time during node parse), physics, samplers/cameras/lights
   (trivial cost, not worth tasks).

## Config options

Add a codegen'd config struct (src/editor/config/definitions/load_config.py,
pattern: threading_config.py):

- `deferred_raytrace` (Bool, default true) — false: build full triangle
  raytrace synchronously in `finalize_imported_meshes` (no proxy, no
  kickoff tasks).
- `deferred_edge_lines` (Bool, default true) — false: build Geometry + edge
  lines at load time exactly as today.
- `parallel_gltf_parse` (Bool, default true) — false: run image/animation
  (and existing mesh) parse loops inline instead of on the executor. Plumb
  as a flag on `Gltf_parse_arguments`; when false, emplace tasks are
  replaced by direct calls (do not resize the shared executor).

The two defer flags plumb from the editor side (`make_import_build_info` /
new parameter on `finalize_imported_meshes` + kickoff construction); the
parse flag lives in `Gltf_parse_arguments`.

## Phasing

- Phase 0 — baseline: add per-stage timing logs (parse, images, meshes,
  finalize-geometry, finalize-buffer-mesh, raytrace tasks) and measure with
  res/editor/assets/hintze-hall_-_vr_tour_8k.glb and island_lobby_new.glb.
- Phase 1 — Part 1 (proxy raytrace + per-mesh replacement tasks) + the
  `Primitive_shape` geometry-build lock. Verify picking works immediately
  after load, then improves to exact hits.
- Phase 2 — Part 2 (deferred geometry/edge lines), including the physics
  eager-geometry carve-out and the buffer-mesh swap mechanism. This is the
  riskiest phase (GPU allocation threading, pool lockstep).
- Phase 3 — Part 3 (parallel images first, then animations, dedup futures).
- Each phase independently benchmarked against Phase 0 numbers, and run
  with the disable option to confirm behavior parity.

## Risks / open questions

- Deferred tasks vs. scene close/undo: existing `s_item_tasks` +
  scene_root capture semantics already handle this for the raytrace kickoff;
  the new per-mesh tasks must keep capturing `scene_root` and the item
  shared_ptrs (comment in async_raytrace_kickoff_operation.cpp documents the
  contract).
- On-demand `get_geometry()` callers racing the deferred build — addressed
  by the Part 1 lock; on-demand call simply blocks until (or performs) the
  build, and the deferred task's `make_geometry` becomes a no-op.
- Buffer-mesh swap while a frame is being recorded: swap under
  `item_host_mutex` and confirm the renderer snapshots ranges under the same
  lock.
- Proxy hits give approximate normals/positions: acceptable for the
  transient window; tools needing exact hits (physics_tool grab) should be
  spot-checked during the proxy window.
- glTF files with huge node counts but no meshes gain little — out of scope.
