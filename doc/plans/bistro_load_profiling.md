# Bistro load time: profile and optimization candidates

Status: proposed

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

## Plan

1. Measure wall time of the open in Release (`build_ninja_win_vulkan_release`
   or a Release configure of the headless tree with tests ON for MCP) and
   Debug (`build_vs2026_vulkan_headless`): launch with `--no-scene`, open
   the file over MCP (`open_scene`), poll `list_scenes` / `get_async_status`
   until idle, and time it; a second open in the same process shows the
   cached-BVH / cached-optimize share.
2. Capture both with Tracy, summarize with `tracy_zone_summary.py` and the
   tracy MCP: main-thread vs worker time, the largest zones (mesh optimize,
   BVH build, bounding volumes, texture decode / upload, material set
   publish, draw list flush), allocation counts if the capture has them.
3. Rank by wall time on the critical path. Candidates to check: mesh
   optimize cache hit rate and cache directory (`make_optimized_render_shape`
   takes `cache_directory`), BVH cache write failure, per-mesh work that is
   serial on the main thread, textures decoded before first frame vs
   streamed, Debug-only costs (iterator debugging, `/RTC1`) that explain
   the Debug/Release gap but are not optimization targets.
4. Write the numbers to `memory-bank/local/bistro_load_2026_10.md`
   (per-machine) and the ranked proposal into this document; implement
   only after the user picks.
