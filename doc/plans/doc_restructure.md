# Documentation restructure: content sweep

Status: in progress

The layout, header lines and index described in `doc/README.md` are in
place. What remains is the content sweep: every document under `doc/` is
brought to the "describes the present" rule of AGENTS.md ("Live documents
under `doc/` describe the present, not the past"), its outstanding work is
moved into `doc/plans/`, and documents that only narrated landed work are
folded into the document that owns the subject and deleted. This document
is the worklist; delete it when the last group is done.

## Rules for every document in the sweep

- The code is the ground truth. A claim of "not yet implemented" or "phase
  N pending" is checked against `src/` before it is kept; what has landed
  is rewritten in the present tense, what has not is moved to a plan.
- Commit hashes, dates, "phase N done" tables, per-commit status lines,
  "UPDATE" sections, before / after narration and "an earlier revision"
  stories are removed. Settled decisions and traps stay, stated as standing
  rules with their rationale. Measured results stay only while they describe
  current behavior or are needed to interpret a future re-run.
- Section labels cited from source comments (`D5`, `R3`, `C7`, `2.9`) keep
  their label and meaning; `grep -rn "doc/<file>.md" src scripts` lists the
  citations before editing.
- Outstanding work moves to `doc/plans/<subject>.md` (or
  `doc/plans/<topic>/<subject>.md`) with a `Status:` line; the current
  document ends with a "## Future work" section holding only links to those
  plans, one line per link. A plan opens by naming the current document it
  extends.
- A document that is folded into another is deleted in the same change, and
  every reference to it (sources, scripts, workflows, memory bank, other
  documents) is repointed. `py -3 scripts/check_doc_links.py` must be clean.
- Only ASCII characters. No history in the surviving text: the commit
  message of the sweep change records what was removed.

## Groups

Each group is one change. Files outside a group are touched only to repoint
references.

### A. Draw-list renderer

- `draw_list_renderer_plan.md`, `draw_list_renderer_results.md`: fold the
  standing design (threading contract, phase order as architecture, current
  measurements) into `draw_list_renderer.md`; delete.
- `draw_list_material_set_context.md`: fold the retained root-cause rule
  into `draw_list_material_set.md`; delete.
- `draw_list_material_set.md`, `draw_list_performance_improvements.md`:
  strip commit citations and dated results; follow-ups to
  `plans/draw_list_renderer.md`.

### B. Asset loading and content library

- `frame-time-after-usd-import-plan.md`: the standing rules (change-serial
  material updates, async display-color rebuild, async asset-browser walk)
  belong to `async_asset_loading.md`, `asset_browser_scan.md` and
  `draw_list_material_set.md`; repoint the `R5` / `R6` citations in
  `src/editor/app_scenes.hpp` and `src/editor/asset_browser/asset_browser.hpp`;
  delete.
- `gltf-load-speedup-plan.md`: fold what still holds into
  `async_asset_loading.md`; delete.
- `async_asset_loading.md`, `async_asset_loading_design.md`,
  `reloadable_asset_loads.md`, `import_undo_reference_clearing.md`,
  `asset_manager.md`, `ring_buffer_memory.md`, `content_library_folders.md`,
  `content_library_ownership.md`, `style_library.md`: strip history; risks,
  open questions and follow-ups to `plans/asset_loading.md` and
  `plans/content_library.md`.

### C. Editor: operations, selection, properties, windows, graph editors

- `operation-stack-reentrancy-plan.md`: fold the main-thread-only contract
  into `editor_operations.md`; delete.
- `sdf-mesh-picking-fix-plan.md`: fold the standing rule
  (`Mesh::update_rt_primitives()` transform) into `editor_scene.md`; delete.
- `251-node-editor-native-rendering-notes.md`: fold the standing design into
  `graph_editor.md`; delete. `plans/node_editor_native_rendering.md` shrinks
  to the live-interaction verification that is still open.
- `node_attachment_editing.md`, `window_target_items.md`, `selection.md`,
  `active_item.md`, `properties_window.md`, `property_system.md`,
  `property_inventory.md`, `layout.md`, `editor.md`, `editor_parsers.md`,
  `editor_scene.md`, `editor_settings_codegen_scene_reference.md`,
  `graph_editor.md`, `msvc_build_issues.md`: strip history; open items to
  `plans/editor.md` (properties, layout, windows, settings) and
  `plans/graph_editor.md`; `msvc_build_issues.md` prevention options to
  `plans/build_tooling.md`.
- `reference/claude_review_2026_03_22.md`: still-open findings merge into
  `plans/editor_improvements.md`; delete the review.

### D. Frame pacing

- `frame_pacing/*.md` against `src/erhe/frame_pacing/` and
  `erhe_frame_pacing.md`: requirements, behavior, inputs, control model and
  user interface documents are rewritten as descriptions of the pacer that
  exists; unmet items go to `plans/frame_pacing.md`.
- `frame_pacing/implementation_plan.md`: keep only standing content (risk
  table, phase order as architecture); the open item goes to the plan;
  delete if nothing standing remains.
- `frame_pacing/capability_tiers.md`: measured driver behavior stays;
  narration goes.

### E. USD

- `usd_compatibility_design.md`: section 3 candidates and section 6 future
  work to `plans/usd_compatibility.md`; the design record stays with its
  `C` / `U` / `M` / `X` labels.
- `erhe_usd.md`: strip history (issue numbers, fix narration, survey
  narration); its "Future work" becomes links into `plans/usd_compatibility.md`.
- `plans/usd_texture_graphs.md`: the UsdShade form of texture and geometry
  graphs has landed (`erhe_usd.md`, `usd_compatibility.md` "Geometry node
  graphs"); fold any standing statement into `usd_compatibility_design.md`
  and delete the plan, or shrink it to what is genuinely outstanding.
- `usd_survey_gap_loop.md`, `usd_wg_assets.md` (script-generated): check
  header lines only.

### F. Geometry graph, texture graph, SDF

- `geometry_graph_mesh.md`, `graph_texture.md`, `geometry_nodes.md`,
  `texture_graph.md`, `lattice_deform_geometry_node.md`,
  `geometry_graph_transform_from_node.md`, `erhe_texgen.md`,
  `erhe_voxel.md`, `catmull_clark.md`, `subdivision_crease_edges.md`,
  `geogram.md`: strip commit tables and phase narration; backlogs to
  `plans/geometry_graph/geometry_nodes.md`, `plans/texture_graph.md`,
  `plans/catmull_clark.md`. `geogram.md` is three documents: the unfiled
  issue draft moves to `reference/geogram_thread_safety_issue.md`, the two
  landed contracts stay.
- `intermittent_main_loop_hang.md`: the standing rule is the Geogram
  serialization contract in `geogram.md` and the watchdog it motivated;
  fold, repoint `src/CMakeLists.txt`, delete.
- `plans/geometry_graph/attribute_projection.md` and
  `attribute_projection_handoff.md` merge into one plan;
  `plans/geometry_graph/creation_tools.md`, `openvdb_sdf.md`,
  `sdf_handoff.md` shrink to what is outstanding (`openvdb_sdf.md` and
  `sdf_handoff.md` merge).

### G1. Lighting, shadows, sky, lightmaps, raytracing

- `ddgi.md`, `lightmap_baking.md`, `lightmap_texture_viewer.md`,
  `point_light_shadows.md`, `shadows.md`, `shadow_tight_fit.md`,
  `procedural_sky.md`, `post_processing.md`, `raytrace.md`,
  `raytrace_materials.md`, `bvh_scene_acceleration.md`,
  `plans/lightmap/*.md`: strip history; follow-ups to `plans/ddgi.md`,
  `plans/lightmap/lightmap_baking.md`, `plans/shadows.md`,
  `plans/raytrace.md`. `plans/lightmap/tiling.md` is several stacked
  handoffs: keep one description of the open work.

### G2. Meshes, GPU memory, worker contexts, platforms

- `vertex_position_quantization.md`, `meshoptimizer_integration.md`,
  `meshoptimizer_attribute_encodings.md`, `mesh_memory.md`,
  `mesh_memory_deferred_free.md`, `primitive_shape_locking.md`,
  `gl_worker_context_enforcement.md`, `gl_worker_thread_contexts.md`,
  `graphics_test_nonheadless_port.md`, `graphics_test_coverage.md`,
  `debug_renderer_multiview.md`, `multiview.md`, `prewarm.md`,
  `xr_controller_render_model.md`, `quest.md`, `android.md`,
  `metal_headless.md`, `vulkan_backend.md`: strip history; follow-ups to
  `plans/mesh_memory.md`, `plans/meshoptimizer.md`, `plans/gl_worker_contexts.md`,
  `plans/graphics_tests.md`, `plans/android.md` (phase 3 full editor),
  `plans/xr.md`.

### H. Scenes, physics, glTF, remaining plans

- `gltf_scene_roundtrip.md`, `khr_physics_rigid_bodies_support.md`,
  `box3d_physics.md`, `scene_serialization.md`, `weight_paint.md`,
  `mesh_component_selection.md`, `command_script.md`, `ai_creations.md`
  (the catalog of creations stays, dated progress narration goes),
  `erhe_gltf.md`, `erhe_scene.md`, `erhe_item.md`, `erhe_physics.md`:
  strip history; open items to `plans/gltf.md`, `plans/physics.md`,
  `plans/weight_paint.md`, `plans/mesh_component_selection.md`.
- `plans/todo.md`: each entry moves to the plan of its subsystem (creating
  `plans/<subsystem>.md` where none exists); delete.
- `plans/gltf_prefabs.md`: reconcile with the prefab support that exists
  (`Prefab_library`, `Prefab_instance`); keep only outstanding phases.
- `plans/gltf_properties_extension.md`, `plans/rigging/*.md`,
  `plans/timeline_editor.md`, `plans/uv_editor.md`,
  `plans/wasm_webgpu_port.md`, `plans/animation_keyframing.md`,
  `plans/init_status_display.md`, `plans/virtualcity_vanishing_meshes.md`:
  first line names the current document they extend; nothing else.

### I. Index

- `doc/README.md`: regenerate the index after the groups above (one line per
  document, level, description); the sweep deletes and creates files.
