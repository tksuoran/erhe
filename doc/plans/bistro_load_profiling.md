# Bistro load time: profile and optimization candidates

Status: in progress

Profile what the editor spends while opening
`res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf` (2782
nodes, 206 materials, 43 lights; ~360 MB of KTX2 textures), in a Release
and in a Debug build, and rank what could be optimized. Report and propose
before implementing anything. This document holds the task's state;
`prompt_queue.txt` only points here.

## Starting points

- The load path is `Scene_open_operation::execute` ->
  `make_import_gltf_operation` -> `finalize_imported_meshes` ->
  `attach_optimized_render_shape` -> `make_optimized_render_shape` (mesh
  optimizer, one log line per mesh: "mesh optimize <name>: ... ms"), then
  bounding volumes (`calculate_bounding_volume` ->
  `optimal_enclosing_sphere`, made scale-robust in fdeb77588), BVH build
  ("BVH save failed" lines mean the cache write failed, so every load
  rebuilds), texture upload. `doc/editor/async_asset_loading.md` describes
  which of this runs on workers and which on the main thread.
- A Debug headless load observed 2026-10-17 took several minutes, with
  the log dominated by per-mesh optimize lines (3-12 ms each for small
  meshes); Release was not measured.
- Startup profiling precedent: `scripts/tracy_startup_profile.py`
  (16.9 s -> 1.68 s startup, 2026-09-21) and `scripts/tracy_zone_summary.py`;
  the tracy MCP (`capture_info` / `hotspots` / `zone`) reads saved
  `.tracy` captures. Per-machine tool paths: `memory-bank/local/profiling.md`.
  `build_ninja_win_vulkan` is configured with `ERHE_TRACY_ON_DEMAND=OFF`
  (events buffer without a profiler attached; see
  `memory-bank/local/context.md`).

## Measurements (2026-10-18)

Tool: `py -3 scripts/scene_load_profile.py --editor <editor.exe> --opens 2
[--tool load_scene] [--tracy-dir <dir> --out <file>.tracy]`. It launches
the editor with `--no-scene`, opens the file over MCP, polls
`get_async_status` until the editor is idle, closes the scene and repeats;
"listed" is when the scene appears in `list_scenes` (publish done), "idle"
when every async task has settled. `--tool open_scene` (default) is the
asset browser's "Load" route, `--tool load_scene` is File > Load Scene.
The per-machine numbers are in `memory-bank/local/bistro_load_2026_10.md`;
the shape of the result, which is what the proposal rests on:

| Build | Path | listed | idle |
|---|---|---|---|
| Release | File > Load Scene (async loader) | 13.8 s | 20.6 s |
| Release | asset browser Load / MCP `open_scene` | 12.0 s | 18.1 s |
| Debug | asset browser Load / MCP `open_scene` | 70.8 s | 149.6 s |

A second open in the same process is not faster: the BVH cache was
already warm (`cache/bvh`, 0 builds and 0 save failures in these runs), and
`mesh_optimize_cache` is `false` in `config/editor/mesh_memory.json`, so the
optimizer never replays.

Release critical path of File > Load Scene, from the log and the Tracy
capture (24 hardware threads):

1. scan 0.07 s and `parse_gltf` 2.8 s on a worker (393 KTX2 decodes spread
   over 24 workers, 21 worker-seconds; mesh parse 0.6 s).
2. `build_imported_buffer_meshes` 1.0 s on a worker.
3. residency 1.4 s on the main thread, budgeted (4 MiB per frame).
4. `Scene_open_operation::execute`: one main-thread frame of 9.3 to 9.5 s.
   `finalize_imported_meshes` is 9.48 s of it, and 8.33 s of that is
   `attach_optimized_render_shape` (the triangle-soup optimizer,
   `optimize_triangle_soup` 7.38 s plus the staged build). The remaining
   1.15 s is the per-primitive scene-side work over 3896 primitives. Then the
   scene is listed.
5. Deferred finalize: 6 to 7 s with all 24 workers busy (per mesh: soup to
   `Geometry`, full buffer mesh, geometry-path optimizer 28 worker-seconds,
   raytrace BVH loaded from cache); the main thread's commit flush is
   negligible (2 to 34 ms per second). Then idle.

Two facts decide the ranking. First, the soup-path optimizer result is
thrown away: it reports vertices 3900054 -> 3900054 (+0.0%), ACMR 0.977 ->
0.987, and seconds later the deferred full build produces its own optimized
variant from the `Geometry` (vertices -42%, ACMR 1.65 -> 0.99) and swaps it
in. Second, the asset browser's Load entry and MCP `open_scene` queue a
`Scene_open_operation` without a prepared parse, so for them the parse
(1.6 s Release, 16 s Debug) also runs on the main thread, in the same frame;
only File > Load Scene and MCP `load_scene` go through
`Asset_manager::queue_load`.

The Debug numbers have the same shape (parse 16 s on the main thread,
finalize 52.6 s with 21 s in the optimizer, deferred phase 79 s); the gap to
Release is the usual iterator debugging and unoptimized code, not a target.

## Ranked proposal

Ranked by main-thread time removed from the path to the scene appearing,
Release, File > Load Scene:

1. **Skip the soup-path optimizer when the full build is deferred.** In
   `finalize_imported_meshes`, do not call `attach_optimized_render_shape`
   for a primitive whose edge lines are deferred: the deferred task's
   `prepare_geometry_buffer_mesh` builds the optimized variant from the
   `Geometry` anyway, and the soup pass has nothing to contribute on a mesh
   the asset already optimized. Removes ~8.3 s of the 9.3 s frame (Debug:
   ~21 s of 52 s). The draw-list registration comment in
   `finalize_imported_meshes` still holds: the deferred commit re-registers
   the mesh when it swaps the shapes. Eager mode (`deferred_edge_lines` off,
   or GL without worker contexts) keeps the pass.
2. **Route every glTF open through the async loader.** Make the asset
   browser Load entry and MCP `open_scene` queue the load with
   `Asset_manager::queue_load` and hand the prepared parse to the operation,
   as `Operations_window`'s Load Scene handler does. Removes 1.6 s (Release)
   / 16 s (Debug) of main-thread parse from those two entry points.
3. **Slice the remaining finalize work.** After item 1 the operation frame
   is ~1.2 s of per-primitive scene-side work (`make_renderable_mesh` fast
   path, raytrace proxy, `update_rt_primitives`) plus the node attach. The
   proxy and renderable fast path can move into the worker `build` phase
   next to `build_imported_buffer_meshes`; what must stay on the main thread
   can be sliced under `max_publish_items_per_frame` once publish is no
   longer one atomic step (doc/plans/asset_loading.md "publish slicing").
   Worth ~1 s; do after items 1 and 2.
4. **Residency budget.** 1.4 s at 4 MiB per frame for ~360 MB of KTX2
   (uploaded as compressed GPU bytes). A bandwidth-based budget or a higher
   default when no frame deadline is at stake would take this to a few
   hundred ms. Small, off the one-frame stall; optional.
5. **Deferred phase.** 6 to 7 s of saturated workers before idle, not on
   the path to the scene appearing. The geometry-path optimizer is 28
   worker-seconds and could use `mesh_optimize_cache`; the full buffer
   build and `Geometry::process` are the rest. Measure again after item 1,
   with the per-corner zones gone (next item), before deciding.
6. **Profiling hygiene (do first, tiny).** `primitive_builder.cpp` carries
   `ERHE_PROFILE_SCOPE` per polygon, per corner and per vertex ("polygon",
   "corner", "n", "2n", `build_vertex_position`, `build_vertex_normal`): 290
   million of the capture's 350 million zones, a 1.5 GB capture per load,
   and the deferred phase stretched from 6 s to 22 s under Tracy. Remove
   them (keep the per-primitive function zones).

Found alongside, outside this task's scope:

- **Scene close stalls the main thread for ~10 s in Release** (Debug the
  same). The Scene_root of the closed scene is destroyed inside
  `Viewport_scene_view::prepare_render`, and `Hierarchy::set_parent` is
  called ~2800 times at ~3.5 ms self time each (no child zones). Suspects:
  `hierarchy_sanity_check` and `sanity_check_root_path` run per call under
  `#if 1`, the inheritance snapshot capture / apply, and
  `invalidate_dependents(child_count_property)` per removed child. The
  watchdog reports it as `tick: prepare_render` stalled 5 to 11 s. Needs its
  own task.

## Build order

Picked 2026-10-18: steps 1 to 4 below, in order, one commit each through
the orchestration harness (`doc/agents/orchestration_harness.md`; Opus
coder, review per commit). Steps 5 and 6 are decided after step 4's
re-measurement. Every step's gate is run with
`scripts/scene_load_profile.py` on the Release headless editor
(`build_vs2026_vulkan_headless/bin/Release/editor.exe`); the baseline
render of the loaded scene (`--render-image`, fixed camera over the
content fit) must stay byte-identical, since none of the steps may change
what the loaded scene looks like once it has settled.

### Step 1: profiling hygiene in the primitive builder

Remove the per-polygon, per-corner and per-vertex profile zones from
`src/erhe/primitive/erhe_primitive/primitive_builder.cpp`: the
`ERHE_PROFILE_SCOPE` zones named "polygon", "corner", "n" and "2n", and the
`ERHE_PROFILE_FUNCTION` zones of the per-vertex helpers
(`build_vertex_position`, `build_vertex_normal` and the other helpers
called once per corner). Keep the per-primitive zones
(`Primitive_builder::build`, `build_polygon_fill`,
`build_expanded_polygon_fill`, `Build_context`, `take_optimizable_snapshot`,
`build_buffer_mesh`). Gate: a two-open Tracy capture of the Bistro load
holds under 70 million zones (was 350 million) and the deferred phase
measured under Tracy is within 2 s of the un-profiled run (was 22 s vs
6 s); the render is byte-identical.

### Step 2: no soup-path optimizer when the full build is deferred

In `finalize_imported_meshes` (`src/editor/parsers/gltf.cpp`), skip
`attach_optimized_render_shape` for a primitive the deferred finalize task
is going to rebuild in full, which is exactly the task's own condition:
edge lines deferred, `render_shape->has_buffer_mesh_triangles()` and
`!render_shape->has_edge_lines()`. The deferred task's
`prepare_geometry_buffer_mesh` builds the optimized variant from the
`Geometry`. Every other case (eager mode, a `Geometry` restored from the
ERHE_geometry extension that already has edges, GL without worker
contexts) keeps the pass. Record the rule in
`doc/editor/async_asset_loading.md` "Deferred load finalize". Gate:
`finalize_imported_meshes: N ms` in the log under 2000 ms in Release (was
9300 to 9500); the "mesh optimize totals" line written at finalize time
reports 0 measured primitives and the one written after the load settles
reports 3852; the scene is listed in under 6 s (was 13.8 s via File > Load
Scene); idle no later than before; render byte-identical. Also run once
with `deferred_edge_lines` off (temporary edit of
`config/editor/editor_settings.json`, restored afterwards) and confirm the
eager path still optimizes at finalize time.

### Step 3: every glTF open goes through the async loader

Make the asset browser's Load entry (`Asset_browser::try_open`) and MCP
`open_scene` (`Mcp_server::action_open_scene`) queue the load with
`Asset_manager::queue_load` and hand the prepared parse to
`Scene_open_operation`, the way `Operations_window`'s Load Scene handler
does; factor that completion into one shared function so the three entry
points do not carry three copies. The fallback when `queue_load` returns
null (async loading off) stays the direct operation. Update the entry
points table in `doc/editor/async_asset_loading.md`. Gate: with `--tool
open_scene` the log carries `async parse_gltf` and `consuming
asynchronously prepared parse`, exactly one `parse_gltf 'bistro_lights_c`
line per open, and the listed time is within 1 s of `--tool load_scene`;
`mcp_server_tests` filtered to `*open_scene*` passes; render
byte-identical.

### Step 4: the rest of the finalize frame

After steps 2 and 3 the operation frame is the per-primitive scene-side
work over 3896 primitives (about 1.2 s): `make_renderable_mesh`'s fast
path, `make_raytrace_proxy`, `update_rt_primitives`, plus the node attach
and draw-list registration. Move what needs no scene into the worker
`build` phase next to `build_imported_buffer_meshes` (the proxy raytrace
and the renderable fast path are candidates) and re-measure. Gate: the
`finalize_imported_meshes` log line under 500 ms; listed under 5 s; idle
no later than before; render byte-identical. Then re-measure both paths,
record the numbers in `memory-bank/local/bistro_load_2026_10.md` and
decide steps 5 and 6.

### Step 5 (decide after step 4): residency budget

1.4 s at 4 MiB per frame; a bandwidth- or time-based budget when no
interactive frame deadline is at stake.

### Step 6 (decide after step 4): deferred phase

6 to 7 s of saturated workers before idle: geometry-path optimizer (28
worker-seconds, could replay through `mesh_optimize_cache`), the full
buffer build and `Geometry::process`. Measure with the step 1 capture.

## Plan

1. Done: wall time of the open in Release and Debug, both entry points,
   two opens per process (table above).
2. Done: Tracy captures of both; summarized with the tracy MCP and
   `tracy-csvexport -u -f <zone>` per zone name (the capture is too large
   for a full export).
3. Done: ranking above.
4. Done: numbers in `memory-bank/local/bistro_load_2026_10.md`, proposal
   here; the user picked the build order above (2026-10-18).
