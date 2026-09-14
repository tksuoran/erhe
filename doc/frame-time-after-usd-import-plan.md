# Frame time after a USD import: plan

The measured case is `res/editor/assets/usd/full_assets/Teapot/DrawModes.usd`
(35 teapot prefab instances). A Tracy capture of the import (editor.exe,
windowed Vulkan build, 78 s, 1154 frames) shows two problems, and the plan
removes both, one commit per section 3 step through
`doc/agent-orchestration-harness.md`.

## 1. Measured behavior

Steady state before the import: `editor::Editor::tick` 10 ms. The import
itself: one tick of 30.5 s. Steady state after the import: 128-150 ms per
tick, of which

| zone (per frame) | before | after |
|---|---|---|
| `App_scenes::update_material_sets` | 2.3 ms | 104-124 ms |
| `Draw_list_scene::check_material_changes` | 0.7 ms | 12-15 ms |
| `Rendergraph::execute` | 4.5 ms | 4.5 ms |

`Material_set::update` runs three times per frame (the scene's forward set,
its draw-list set, a preview root's set) and hashes every live slot's
material through `Material_buffer::get_content_hash`, which calls
`Material::get_values()` (26 layered property reads) plus five texture-slot
resolutions per material. The import adds about 140 materials (35 templates
times the four `Teapot_Payload.usd` materials), and the scan costs about
0.35 ms per material per set. In the whole capture the scan found five real
changes (`Material_buffer::write_records` ran five times).
`Draw_list_scene::check_material_changes` derives a `Shader_key` per
material per frame for the same purpose.

The 30.5 s import tick: `load_usd_prefab_template` 35 calls, 16 s;
`App_scenes::rebuild_display_colors` one call, 13.1 s (282
`build_buffer_mesh` calls plus `make_raytrace` on the main thread);
`Operation_stack::update` 3 s. At startup, `Asset_browser::Asset_browser`
takes 8.1 s walking `res/editor/assets` (about 3700 entries) on the main
thread.

Reproduction: the capture recipe of section 4.

## 2. Requirements

- R1. A frame in which no material changed performs no per-material work in
  `Material_set::update` and `Draw_list_scene::check_material_changes`
  (AGENTS.md "No update each frame patterns").
- R2. Every write that changes what a material's GPU record or `Shader_key`
  is built from reaches the material set through a notification: property
  writes (`Dependency_object::set_value` / `clear_value`), texture-slot and
  sampler-state writes, and a texture graph bake that changes the texture a
  slot's `Texture_reference` resolves to.
- R3. `Material::data` is written only through `Material` member functions;
  the importers, the texture-graph output nodes and the MCP graph tool use
  those functions.
- R4. A prefab chain referenced by several carriers of one file is parsed
  once per (file, prim path, variant selections that file consumes).
- R5. The display-color rebuild of a mesh runs its geometry and raytrace
  builds on an executor worker and commits the swap on the main thread
  through `Scene_commit_queue`, the way `deferred_finalize_mesh_items`
  does; meshes whose (geometry, color, normal style) match share one build.
- R6. The asset browser's directory walk runs on an executor worker; the
  window shows the tree when the walk lands, and the constructor returns
  without walking.

## 3. Steps

Each step is one commit. The verification of every step is section 4.

### Step 1: material change serial and a closed `Material::data` (landed, 350943536)

`erhe::primitive::Material` gains `get_change_serial() -> uint64_t`, a
counter that `on_property_changed` advances for every property of the
material (values, texture references, sampler state, uv transforms). The
`data` member becomes private; `get_data()` returns it const, and the
texture-sampler writers that exist today (`set_slot_texture`,
`set_slot_sampler`, `set_slot_uv_transform`, `set_data`) are the write path.
The direct writers move to those functions:
`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`,
`src/editor/parsers/gltf_extensions_import.cpp`, `src/editor/parsers/usd.cpp`,
`src/editor/texture_graph/nodes/texture_output_node.cpp`,
`src/editor/texture_graph/nodes/texture_material_output_node.cpp`,
`src/editor/mcp/mcp_server_graphs.cpp`, and the scene-renderer test
`test_material_set_gpu.cpp`, whose "written straight through the object"
case becomes a setter write.

A texture graph bake that lands a new texture behind a `Graph_texture`
advances the serial of every material whose slot references that
`Graph_texture`: `Graph_texture` keeps the set of materials referencing it
(registered from `Material::on_property_changed` when a texture-reference
property takes or drops the value) and calls `Material::notify_texture_rebaked()`
on each at the bake landing site. R2 is then complete: there is no
change to a record input outside `on_property_changed`.

### Step 2: change-driven material sets

`Material_set::update` compares each slot's recorded serial with the
material's `get_change_serial()` and re-gathers only changed slots; the
content hash and `get_content_hash` go away. `Draw_list_scene::check_material_changes`
keeps the identity-hash map but recomputes an entry only when the material's
serial moved since the entry was recorded. `Material_set::invalidate()`
stays for device-level events. R1 holds: the per-frame cost is one integer
compare per slot. The three-sets-per-frame reconcile stays as it is; its
cost is the membership diff, which is proportional to library changes.

### Step 3: one parse per referenced prefab chain

`resolve_usd_references` (`src/editor/parsers/usd.cpp`) treats an internal
arc (`references = </World/X_0>`, empty asset path) as the same file, so
each `_1..._4` sibling of DrawModes is its own `Prefab_key` and re-parses
the chain; and `propagate_variant_selections` bakes the carrier's whole
selection set into every nested key even where the nested file authors no
variant set of that name. The fix: the nested key carries only the
selections of variant sets the nested file (or a file below it) authors,
computed from the loaded `Usd_data` variant tables, so
`Teapot_Payload.usd`, `Teapot_Geometry.usd` and `geo/*.usd` load once per
model variant; and an internal arc whose target is a carrier of the same
file resolves to that carrier's already loaded template. R4 holds when
DrawModes loads with one `load_usd_prefab_template` per distinct chain
(section 4 counts them).

### Step 4: display-color rebuild on the deferred path

`App_scenes::rebuild_display_color` becomes the kickoff of a per-mesh task
dispatched through `async_for_nodes_with_mesh` with the contract of
`deferred_finalize_mesh_items`: the worker builds the recolored
`Primitive` (renderable mesh with a narrow `Scoped_worker_context` around
the GPU build, raytrace), the commit lambda swaps `set_primitives` between
`begin_mesh_rt_update` / `end_mesh_rt_update` on the main thread. Meshes
queued in the same flush whose (geometry pointer, color, normal style) match
receive the same built `Primitive`. R5 holds; the import tick no longer
carries the 13 s.

### Step 5: asset browser walk on a worker

`Asset_browser::scan()` runs on `context.executor` (the `Gltf_scan_request`
pattern already in the file): the walk builds the node tree off the main
thread and the window swaps it in when the request reports finished; the
constructor only submits the request. The manual "Scan" button and
`refresh_file` keep their behavior. R6 holds.

## 4. Verification

The capture recipe, run by every step against the headless build:

1. Build: `cmake --build build_vs2026_vulkan_headless --target editor --config Debug`
   (Tracy is on in every wrapper configuration), plus
   `cmake --build build_vs2026_vulkan --target editor --config Debug` (the
   windowed tree, configured with tests on) and the test targets a step
   names, built from that tree.
2. Start `D:\tracy-windows-0.14.1\tracy-capture.exe -o <scratch>\run.tracy -a 127.0.0.1 -f`
   in the background, then launch the headless editor with
   `ERHE_AI_DRIVER=1` (skill `erhe-headless-verify`).
3. `py -3 scripts/mcp_call.py import_usd b64:<{"path": "res/editor/assets/usd/full_assets/Teapot/DrawModes.usd", ...}>`,
   wait for `get_async_status` idle twice, let 10 s of frames pass, then
   `request_exit`; the capture closes with the editor.
4. `tracy-csvexport.exe -u -c -f "editor::Editor::tick" run.tracy` and the
   same for `editor::App_scenes::update_material_sets`,
   `erhe::scene_renderer::Draw_list_scene::check_material_changes`,
   `editor::load_usd_prefab_template`, `editor::App_scenes::rebuild_display_colors`,
   `editor::Asset_browser::Asset_browser`. Report the post-import median
   tick and the per-zone medians and counts against section 1.

Targets: post-import tick under 20 ms with `update_material_sets` and
`check_material_changes` each under 0.5 ms (steps 1-2);
`load_usd_prefab_template` count equal to the distinct chains (step 3);
`rebuild_display_colors` self time under 5 ms per call (step 4);
`Asset_browser::Asset_browser` under 50 ms (step 5).

Correctness checks alongside: `erhe_scene_renderer_tests` and
`erhe_primitive_tests` green; a headless `capture_screenshot` after the
DrawModes import shows the six teapot colors (step 1, 2 and 4);
`edit_material` over MCP changes the rendered color on the next screenshot
(step 2); `scripts/scene_roundtrip_verify.py` baseline unchanged (step 3).
