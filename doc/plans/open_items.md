# Open items across subsystems

Status: in progress

This plan collects outstanding items that have no plan of their own: known
defects, unverified paths, unbuilt trees and pending user decisions found
while working on other subsystems. Where a subsystem plan already holds the
full item, the entry names that plan. When an item is picked up, it moves
into its subsystem's plan (or is fixed) and its line here is removed.

## Work order

The tasks a session continuing earlier work picks up, in order (`AGENTS.md`
"Session handoff"). Items 1 and 2 come first; items 3 - 13 each need a user
decision or hardware (Quest, the external 120 Hz display, an NVIDIA GPU):
ask the user which to pick. Item 14 starts with writing its plan. Items 15,
17, 18 and 20 wait for the user's review. Item 21 continues at its final pass. Item 22 waits
for the user's test.

0. The user rulings on the D8 coder decisions ("Prepared assets" below,
   `doc/plans/prepared_assets_design.md` D8).
1. Quest session follow-ups: "Quest session follow-ups" below
   (`doc/agents/quest.md` "Bistro scene data on the device").
2. LOD follow-ups: `doc/plans/lod_gpu_selection.md` step 6 "Results" and
   "Later"; shadow culling in `doc/plans/occlusion_culling.md` "Open".
3. PVS: the Quest wall gate (plan step 8), the bake at 10 cells per frame,
   the other "Open" items (`doc/plans/pvs.md`, `doc/editor/pvs.md`).
4. Raster occlusion culling, user decisions first: the step 5 second draw,
   fill cost at the Bistro bench camera, headset per-eye culling
   (`doc/plans/occlusion_culling.md` "Build order" and "Open").
5. Metal 4 gates at the external display, then the Metal 3 keep or remove
   decision (`doc/plans/metal_backend.md` section 7;
   `doc/plans/resource_barriers.md` for the barrier leftovers).
6. Frame pacing re-gates: fixed 60 Hz, the display's update granularity,
   MoltenVK fullscreen (`doc/plans/frame_pacing_adaptive_sync.md`
   "Verification").
7. Quest 3 performance (needs the headset): `doc/plans/quest_performance.md`;
   the Bistro configuration sweep continues its "Next configurations" list.
8. Quest fragment shader, the mobile shading variant (needs the headset
   for the device gates; the desktop gates run headless):
   `doc/plans/quest_fragment_shader.md` "Build order".
9. Prepared scene phantom shadow (Intel ANV ring-mode indirect draws;
   user deferred the minimal repro, start only when asked):
   `doc/plans/prepared_scene_shadow_phantom.md`.
10. Background scene save follow-ups: `doc/plans/background_scene_save.md`,
    then `doc/editor/scene_serialization.md` "Saving in the background".
11. Scene round-trip device loss (needs the NVIDIA GPU):
    `doc/plans/scene_roundtrip_failures.md`.
12. Audit follow-ups: `doc/plans/audit_2026_09_30_followups.md`.
13. Bistro load decisions (step 5 KTX2, step 7c lazy attribute stores,
    geogram colocate serialization, worker release reopen cost):
    `doc/plans/bistro_load_profiling.md` "Status".

14. Triangle classification from walkable sample points: a plan to write,
    then build (`doc/plans/navigation_mesh.md` "Triangle classification from
    walkable sample points"; the navigation mesh it reads is built).
15. Level tools: the user's windowed review of the steps built on
    2026-11-18 (`doc/plans/level_tools_implementation.md` "Built"), then the
    push (U58).
16. Level tools and curves: the "On hold" steps of
    `doc/plans/level_tools_implementation.md` "Next steps" and the "Steps" of
    `doc/plans/curves.md` wait for the user to make them active.
17. Level kit connections: every step is built. The user's windowed test
    ("User test (windowed, pending)") and rulings ("For the user to rule
    on") of `doc/plans/level_kit_connections_implementation.md`, and the
    review of design K0. Riders (`doc/plans/level_kit_connections.md` C10, design
    K13) are built and the user verified them windowed; open is the
    review of the choices of ruling L26, and the full verify scripts and
    `ctest` before the push.
18. Kit layout composition (GitHub issue 301): the user's rulings on the
    open questions of `doc/plans/kit_layout_composition.md`, then its build
    order.
19. Worker executors: built and measured on desktop
    (`doc/plans/worker_executors.md` "State" and "Remaining"); the Quest
    measurement waits for the headset.
    Curve hover gizmo (U62 - U65) waits for the
    user's windowed review; its last commit (the miniature scale) is not
    built.
20. Level kit surface attachments (GitHub issue 304): every step is built.
    The user's windowed test ("State") and the review of the planner's
    choices ("For the user to review") of
    `doc/plans/level_kit_surface_attachment.md`.
21. Foliage paint tool (GitHub issue 302): steps 1 - 8 are built and
    committed; step 9 is in progress - read
    `doc/plans/foliage_paint_tool_implementation.md` "9 Final pass" first
    (uncommitted fixes of a coder may be in the working tree, its report in
    `logs/foliage_final_fixes_report.md`). Then the user's windowed test
    and rulings (`doc/plans/foliage_paint_tool.md` "For the user to rule
    on"), then the push.
22. Detail blend weight (height blended per vertex weight): every step
    is built. The user's windowed test ("User test") and rulings ("Open")
    of `doc/plans/detail_blend_weight_implementation.md`, then the push.
23. Leaf material follow-ups (textures, baked translucency, ray query
    path, USD): `doc/plans/leaf_material.md`.
24. Level kit shared points: the user's comparison of the former builds,
    their removal, and the user's pick of the joins not yet fixed
    (`doc/plans/level_kit_shared_points.md`).

## Quest session follow-ups

- Worker executors: the acceptance runs of `doc/plans/worker_executors.md`
  W8 on the Quest (needs the headset and the executors built), to set the
  device's background thread count default.
- `origin/lod` is stale after its rebase onto `main`: force-push or delete
  (user decision).
- Re-pose `config/editor/benchmark_bistro.json` for the upright Bistro (the
  source rotated -90 degrees about X); the street benchmarks follow the
  scene's camera animation and need no pose of their own.
- The Bistro street path (`benchmark_bistro_street_90.json`) runs GPU-bound
  at about 36 fps on the Quest with the headlamp as the only light (eye pass
  22 ms median, 36 ms in its heaviest second at 2 - 3 s): the heavy section
  is fragment work from overdraw at the bistro corner (desktop count: 8.3
  rasterized and 2.7 shaded fragments per covered pixel against 2.4 / 1.5 in
  the light section; 114k alpha-tested triangles in view), with the shader
  ALUs 30 % utilized and a 45 % vertex fetch stall. Candidates: front-to-back
  opaque order, less alpha-tested foliage in view, or a path that keeps the
  camera out of the corner.
- World-space pieces are built (`world_piece_extent`, commit of
  2026-11-08 evening): Bistro corner pose, world-space form, opaque pass
  triangles 267k (whole primitives) -> 71k (8 m) and 65k (5 m), rasterized
  fragments per covered pixel 6.75 -> 6.16 / 5.89, shaded unchanged (2.4),
  cull check 0 mismatches, no visible cracks at the corner (screenshots of
  the three extents differ in under 0.1 % of pixels;
  `doc/erhe/primitive.md` "Pieces"). On the Quest (2026-11-08 21:22 -
  21:27, street path at 90 Hz, headlamp, each extent twice, one input
  hash; `doc/reference/quest_bench_runs.md`) pieces are slower: whole
  primitives 33.6 fps (XR multiview 25.0 ms median, 457 draw-list entries
  after load), 8 m 25.7 fps (31.3 ms, 1388 entries), 5 m 21.2 fps
  (37.4 ms, 2294 entries); the shadow pass grows too (2.2 -> 2.4 / 3.1 ms).
  The desktop triangle saving does not pay for the extra draws and the
  split vertices on the Adreno; find out which (per-draw cost, or binning
  of more, smaller draws) before splitting further.
- Metal counts overdraw per sample under MSAA
  (`doc/plans/overdraw_visualization.md`).
- The right trigger still selects while travelling.

## Prepared assets

Plans: `doc/plans/prepared_assets_design.md` (D8 per-step "Open" notes),
`doc/plans/prepared_assets.md` (R numbers). Behavior:
`doc/editor/prepared_assets.md`.

- `scripts/prepared_asset_bistro_verify.py` compares its images with the
  editor's LOD preset (2 of 32 fail on the Mac's Medium preset, LOD on):
  turn LOD off for its images as `prepared_asset_verify.py` does.
- User rulings pending on the coder decisions recorded in D8 steps 12, 14
  and 15 (Scene-item-only edits and material swaps do not mark the status
  stale; load key uses the Export LOD; what is not loaded from items). Ruled
  2026-11-07: the status computes on request only; the PVS and world-space
  bake invalidation stays as it is.
- Build and test the GL, Metal, Windows and Android trees; D8 step 15 dropped
  `<filesystem>` includes from `buffer_info.hpp` and `mesh_optimizer.hpp`,
  which other trees may have relied on transitively. `erhe::asset_store` is
  unbuilt on MSVC.
- `mcp_server_tests` (windowed) is unrun since the prepared-asset work.
- `pvs_verify.py --sections streaming` opens a saved scene, which holds no
  PVS any more; the case converts to a prepared run.
- An open scene does not load the PVS item of its editor asset.
- Prefab templates and imports do not load from items.
- A template stub material from a missing container is not detected.
- Bake World-Space Meshes does not read dependency files first.
- The viewport toolbar Mesh form combo is not driven by an automated check.
- The Export camera (session-only) is unverified interactively; a click on it
  may queue a selection operation.
- Export has no cancel (`Scene_save_service` has none).
- On Windows, renaming over a prepared asset that a prepared run holds open
  fails the Export (documented, not run); deleting a prepared asset while a
  run holds it is untested.
- The editor asset's `-wal` / `-shm` files remain after SIGTERM; a clean quit
  is unchecked.
- The window between the open's compatibility check and the load is not
  re-checked against a concurrent Export copy.
- The same source imported twice gives duplicate uids in the scene (the
  writer resolves them).
- A soup that does not prepare always shows as missing in the status.
- Graph-made meshes in the Scene item are untested.
- The USD refusal of Export is untested (no LightUSD build).
- The Basis encoder is scalar; its time on large PNG textures is unmeasured.
- Old files whose textures were named only via `ERHE_texture_ktx2` open
  without an image.
- Existing erhe-saved scenes whose settings are all defaults open with a copy
  of the editor's export settings; verify Prepare and Export on one.
- The `git stash` entry with the `ERHE_pvs` column rewrite is superseded;
  drop it when the user agrees. `doc/plans/prepare_pvs_export.md` is
  superseded except its "Established" section.

## Editor defects found in passing

- Box3D: `ICollision_shape::get_penetration_depth()`
  (`box3d_collision_shape.cpp`, used by `find_overlaps()`) has not been
  compiled or run (no Box3D build tree on the machines used).
- Curves: the USD save's warning for a Curve node (`doc/editor/curves.md`
  "USD") has not been compiled; the build trees use
  `ERHE_USD_LIBRARY=none`.
- A rigid body on an animated node rewrites the node's base transform
  each physics step, conflicting with the animated layer (seen
  2026-09-12; not checked since).
- Material Design icons `ICON_MDI_LINK` (U+F0337) and `ICON_MDI_BRUSH`
  (U+F00E3) were seen rendering blank in the item tree (feature icons);
  check the glyph range built in `imgui_renderer.cpp`
  (`ICON_MIN_MDI`..`ICON_MAX_MDI`, `ImWchar` width).
- Four viewports exhaust the Vulkan GPU timer pool (`s_max_gpu_timers` =
  128, `vulkan_device.hpp`): "GPU timer slot pool exhausted" warnings
  follow and the extra timers are inert.
- A mouse drag that is active when its tool loses priority keeps the mouse:
  `Commands::on_mouse_button_event()` skips bindings whose host is disabled,
  so the release never reaches the command, it stays Active and
  `m_active_mouse_command` is never cleared; every later mouse command is
  refused. `Physics_tool` does not inactivate its drag command on priority
  loss, `Weight_paint_tool` ends its stroke but leaves the command Active.
  Reproduce: Weight Paint tool, left drag, a hotbar key, then release. Found by reading; the fix belongs in `erhe::commands` (a held
  command whose host is disabled is made inactive).
- `Mesh_operation::make_entries()` holds the scene's `item_host_mutex` for
  the whole geometry computation of an async mesh operation, so the main
  thread stalls in `flush_draw_lists` while it runs (five chained
  Catmull-Clark steps on eight boxes time out an MCP request in a headless
  Release run). The lock should cover the reads of the scene only
  (`doc/plans/worker_executors.md` D5.5). Found at worker executors step 3.
- Scene close under running tasks has had no thread sanitizer run: the
  sanitizer tree is not built on the Linux machine (disk), and its editor is
  windowed. The scenario is the `mcp_server_tests` case
  `scene_close_and_reset_with_mesh_operations_and_import_in_flight`.
- Kit pieces refresh over frames after a load (`doc/editor/level_tools.md`
  "Persistence"); three edges of that window remain. A parameter edit made
  and undone before the piece's turn leaves the piece without its brush
  (undo restores the loaded mesh and no brush placement, and the queue has
  passed it), and a copy made in the window is not queued; Level Check then
  reports it until the scene reloads. The navigation item installs when the
  last piece is refreshed and replaces a navigation clear or settings edit
  made in the window. The raytrace kickoff of the open still works on the
  loaded primitives that the refresh replaces (worker time only).
- Mirroring a registered mesh at run time is fatal: `set_node_transform` with
  scale [-1, 1, 1] on a placed kit piece, or `level_place_pieces` under a
  mirrored parent, logs "Runtime negative-determinant flip on registered mesh
  ... not supported in initial scope" and `Draw_list_scene::flush_pending()`
  (`draw_list_scene.cpp`) asserts `observed_negative_determinant ==
  object.negative_determinant`. A mirrored kit piece's snap to surface has
  unit tests only until this is fixed (`doc/editor/level_tools.md` "Surface
  placement").
- A headless Metal Debug editor once stopped in the render thread's "tick:
  submit + end_frame" (watchdog "Main loop STALLED") during the persistence
  group of `scripts/level_tools_verify.py` (2026-11-18, step 79 work); the
  re-run passed. Not reproduced.
- Importing the same erhe glTF twice into one scene keeps the file's node
  uids (`resolve_object_uids` in `gltf_fastgltf.cpp`), so two live nodes
  share each uid; uid-keyed lookups across them are ambiguous (kit
  attachments treat such a uid as dormant / inert).
- Collision under scale, what remains (`doc/editor/physics.md` "Collision
  shapes follow the node's scale"): a brush of a density other than the
  default writes the mass of its placed volume, which a later scale change
  does not follow; a body loaded from a scene saved before 2026-11-16 keeps
  the explicit mass its brush wrote then. An implicit imported collider on a
  node without a mesh, or a compound with a rotated child, under a scale the
  backend cannot take, gets the nearest valid scale with a warning. The
  Box3D backend's `set_collision_shape()` and the shape scale paths have not
  been compiled or run (no Box3D tree on the machines used).
- Derived walkability at other level lengths: the derive ranges of six built
  kit shapes are stated in modules or wall heights while their walkability
  checks use the agent's fixed size, so at other level lengths derived
  values fail the check (`editor_level_tests`
  `Kit_derive.derived_values_are_accepted_and_walkable_at_other_level_lengths`
  leaves them out by name and records the counts; of 300 seeds): at module
  0.5 m and wall height 2.5 m the wedge 97, stairs 97, floor slab 141, arch
  150, land mass 21, shore 21; at module 1 m, wall height 2.4 m the arch 79;
  at module 2 m, wall height 2.4 m the arch 54; at module 0.25 m nearly all.
  The torii and the tower state their agent-bound sizes in meters
  (`doc/editor/level_kit_shapes.md` "Derive ranges").

- `erhe::raytrace` hit filters (`doc/erhe/raytrace.md` "Hit filters"): the
  tinybvh and embree paths are verified only by `erhe_raytrace_tests` in
  macOS arm64 Debug scratch builds (tinybvh 55/55, embree 36/36 without the
  `Hierarchy` tests, which abort in Embree's instance stack on nested
  instances); the editor has not run on either backend.
  `scripts/test_raytrace_all_backends.bat` covers the backends on Windows.
- A runtime flip of a registered mesh's world determinant to negative (a
  node scaled through zero to a negative scale: `set_node_transform`, a
  scale gizmo drag) aborts a Debug editor on the `ERHE_VERIFY` in
  `draw_list_scene.cpp` `flush_pending()`
  (`observed_negative_determinant == object.negative_determinant`); the
  draw list supports the flip only at registration.
- `scripts/level_test_session.py` (the hand-off script of
  `doc/agents/editor_runs.md` "Handing the editor to the user") has not been
  run end to end: it was written on 2026-11-15 while the user's editor was
  open, and only its read-only queries were checked against that editor. Run
  it at the next hand-off and fix what fails.
- The Hotbar's rendertarget node outlives its scene: after a Hotbar slot
  key is pressed with a scene active, "Hotbar imgui host RT node" and
  "Rendertarget Node" are still alive 60 frames after that scene closes
  (repro: place a piece, select it, `Hotbar.activate_slot_1`, close the
  scene; the scene-close watchdog logs them).
- SPIRV-Tools `MergeReturnPass` aborts in a Debug editor (assertion in
  `Instruction::unique_id()` from `UserEntryLess` during a def-use update in
  `MergeReturnPass::BranchToBlock`), reached from the shader statistics job
  (`Shader_session_programs::run_stats_job()` -> `make_report_stage()` ->
  `optimize_spirv()`). Seen once, 2026-11-15; the shader is not identified.
  Same signature as KhronosGroup/SPIRV-Tools issue 6711 (open); the pin
  already holds the one merged fix (PR 6885). Next: capture the module that
  triggers it.
- `Hit::normal` points outward for mirrored instances since the hit filter
  work, which changes the PVS baker's front / back tests for scenes with
  negative-determinant nodes: re-check the PVS reference hashes
  (`doc/editor/pvs.md`) on a scene that has such nodes.
- With `load.async_gltf_load` off, a failed synchronous load is not
  recorded in `Asset_manager::get_failed_loads()` (Operations and MCP
  `get_async_status` report nothing for it).
- Quest: a launch with no scene aborts in `scene_view.cpp:332`.
- Quest: `render_scene_image` crashes (SIGSEGV in
  `Render_pass::start_render_pass` from `Shadow_renderer::draw` in the capture
  chain), also with a plain glb.
- Quest: the device's writable `mcp_tools.json` is the copy made at first
  install and goes stale.
- Three Scene Hierarchy MCP tests failed on one machine (suspected: the
  ini's Scene Hierarchy height 37); `ctest -L editor` passes in full on
  macOS at 3b181b4f4; re-check on Windows.
- A `Graph_texture` destroyed on the render thread (the main-thread VERIFY of
  `Asset_manager`, seen in a `scene_roundtrip_verify.py` run on Windows) does
  not reproduce on macOS (Metal headless, glTF + USD legs, editor exit);
  re-check on Windows.
- The id renderer's region-scan readback sometimes returns 0 entries
  (`gate_id.py` JSON varies run to run).
- macOS: with the Vulkan SDK `setup-env.sh` sourced, SDL's Cocoa surface
  creation gets MoltenVK's `vkGetInstanceProcAddr` and every GPU test and the
  editor segfault (not fixed in code). Measure the editor's SIGTERM exit on
  macOS: exit now waits only for the running shader stats jobs
  (`doc/editor/shader_analytics.md` "Session programs").
- Mesh editing: no weld after an in-place edit; per-facet texcoords with
  seams keep no LOD chain; a reparent that changes only the carrier is not
  re-resolved.
- fp16: the first render after a variant switch is an outlier in the chains.
- Snap to surface, unverified: a base-point line that passes exactly
  through an edge shared by two triangles (a base center over the diagonal
  of a slab) may miss both triangles, so that line sees no surface there
  (`find_surface_placement()`, `doc/editor/level_tools.md` "Surface
  placement"). Check with a unit test of the bvh backend's triangle test.
- Snap to surface, unverified: under a parent with non-uniform scale a
  rotated child's local transform has shear, which the stored translate /
  rotate / scale drops, so the child lands off its planned pose
  (`plan_surface_snaps()`; `level_snap_to_surface` records an operation
  whenever a place is found, so a node that does not move can shift).
- Kit pieces: a Paste Properties (or raw write) of another shape's
  `Kit_piece.shape` / `Kit_piece.parameters` changes the record without
  rebuilding the mesh, so the mesh and the record disagree.
- Kit pieces: `set_item_style` and `set_prefab_template_property` can
  carry `Kit_piece.connectors_enabled` / `Kit_piece.disabled_connectors`
  values without the write check of `check_connector_switch_write()`.
- Kit layout outlines: removing a whole Scope that holds materials does not
  drop the outline cache (`Level_kits::on_scene_materials_changed()` runs
  only when a Material itself is the inserted or removed item), so a layout
  naming such a material keeps its outline until its next value change.
- Kit shapes: `nature.hill` takes at most 60 segments (the triangle ceiling
  binds at every in-range value), so a piece saved with 61 - 64 segments
  no longer reads its parameters; for the user to rule whether such pieces
  read clamped.

## Interactive checks for the user

- USD features verified headless only (Asset Browser import / save,
  references and instances, styles from class prims, variants, skinning,
  graphs, folders, physics, animation, DrawModes):
  `doc/plans/usd_compatibility.md` "User checks (windowed, pending)".
- Property system: the windowed checks of the 2026-09 property work
  (`doc/plans/property_system.md` "Interactive checks not yet made").
- Input Bindings window in the windowed build: the feel of the capture
  modal, real keyboard chords, and the Edit Input Bindings button under
  Settings > User Interface (`doc/editor/input_bindings.md`).
- Cross splitter of the four view: mouse feel, the ResizeAll cursor, and
  the "Cross splitter" dock menu toggle (`doc/erhe/imgui.md`).
- Ctrl+A select-all in the Scene Hierarchy and the Asset Browser,
  windowed.
- Physics: a gizmo mouse drag of a jointed body in the windowed build (the
  right-drag is verified; `doc/editor/physics.md`).
- Viewport context menu (`doc/editor/windows.md` "Viewport context
  menu"): the Level Kit entries placing at the hovered point, and a
  windowed run. For the user's review, the orchestrator's choices: Create
  and Paste parent under the target item; the viewport menu has no
  Rename; a Bone is not placed.
- Kit item drag and kit slots (`doc/editor/level_tools.md` "Dragging a kit
  item", "Kit slots"): checked headless only (`level_tools_verify.py
  --only-kit-drag`, `--only-kit-slots`); the windowed drag from the Level Kit
  window into a viewport and onto Inventory slots, and the load of saved kit
  slots from `user_state.json` in a normal session, have not run by hand.
- Geometry Spreadsheet: the hovered row's highlight in the viewport
  (`doc/editor/geometry_spreadsheet.md` section 5); the headless camera could
  not show it.
- Content edge lines on the vertex-shader ribbon (`content_line_ribbon.vert`,
  `doc/erhe/scene_renderer.md`): the skinned variant (a posed skinned mesh's
  edge lines follow the pose) and the multiview variant in the headset have
  not run.
- Viewport review: light visualization (spot cylinder, directional light at
  the vanishing point, light picking); the headset runtime of these is
  untested.
- Headset: worn visual check of the draw-list path; headset per-eye passes
  of the shadow-texel visualization barrier path.
- Render thread step 6: headset overlap verification and the windowed pacer
  check; then the user decides on merging (`doc/editor/render_thread.md`).
  `xrEndFrame` queue access stays outside the device queue mutex until then;
  headset hover entries are still read in the render phase.
- LOD window interactive pass; LOD testing in the headset.
- Mesh modeling user-interactive checks (`doc/plans/mesh_modeling.md`).
- Rigging: stiffness and the Phase 1 feel questions; the deferred interactive
  items (IK settings slice, pole picker and angle rows, live drag with pole,
  Move tool Effector Orientation combo, chain / root / pole visualization
  during drag) (`doc/plans/rigging/interactive_test_pass.md`).

- Level tools, windowed Metal: a path stairs control point drag shows no
  stale staircase geometry (the mesh upload order is verified headless
  only).

## Performance and measurement

- Threaded rendering against its design: capture the benchmark scenes with
  Tracy and compare what each phase does (thread, overlap, where the waits
  sit) with `doc/editor/render_thread.md` sections 2.1 - 2.7, 3.3 and 3.4;
  report and propose improvements before building them
  (`scripts/render_thread_bench.py --tracy`, `scripts/tracy_zone_summary.py`).
- Layout nodes recompute only when there is a need. `Layout_system::update`
  recomputes every layout every frame (`doc/erhe/layout.md`); it shall run
  a layout when a `Layout` value of the node or of a child changes, when
  the child list changes and when a child's measured content changes, and
  do nothing in a frame without such a change.
- GPU memory on Bistro after Generate LODs (2026-11-09 out-of-memory abort on
  a 12 GB card): the mesh vertex pools held 5.9 GB in 352 blocks of 32 MB
  (index pools 384 MB), about 5 - 7x what Bistro's 3.8M level-0 + 3.0M LOD
  triangles should need. Find what holds the vertex pool space: per-pool
  `get_memory_usage` used / free bytes before and after `generate_lods`
  (MCP), whether the original and optimized variants and the replaced
  Buffer_mesh variants each keep full vertex copies. The out-of-memory log
  (`Device_impl::log_memory_state_at_allocation_failure()`) gives each
  allocator's blocks.
- `Ring_buffer_pool` (`erhe_graphics/ring_buffer_pool.cpp`) has no upper
  bound: while frames lag it creates a spill buffer sized to each request
  (the content edge lines spilled 1.7 GB in seconds before they moved to the
  vertex shader). Decide whether the pool needs a cap or back-pressure.
- Bistro load decisions (`doc/plans/bistro_load_profiling.md` Status): KTX2
  transcode (step 5), lazy attribute stores (step 7c; about 1.3 GB of empty
  stores, blocked by store sharing between `Mesh_attributes`), geogram
  colocate serialization, reopen during payload release contending on the
  heap; import completion follow-up.
- Quest VmHWM is CPU heap; the stored raytrace data (about 362 MB) is the
  next candidate (lazy raytrace).
- Quest: binning is unmeasured (needs metavr); the lit fps gap against
  `doc/plans/pvs.md`; heavy-scene edge line compute (about 39 ms of the GPU
  frame) and the default color pass (grid 2 ms, shadow math)
  (`doc/plans/quest_performance.md`).
- Bistro GPU, found and not fixed: alpha-tested casters cast no shadows
  (`shadow_renderer.cpp` opaque primitives only); Bistro image run-to-run
  nondeterminism; the selection stencil mask writes depth-only full quads for
  alpha-tested meshes; windowed viewport is 27 % slower than headless at the
  same size; windowed tier-M deadline offset estimate (11.7 - 12.7 ms) against
  calibration (9.62 ms).
- Robust BVH traversal of the bvh backend's BLAS: cost unmeasured.
- Render thread: the CPU-bound measurement on a lower-floor GPU (user; NVIDIA
  driver hang risk); the frame-pipelining gain is untested on the iGPU.
- Shader fp16 open questions (`doc/plans/shader_fp16.md`): P1 roughness in
  fp32, sharp metal silhouette pixels, R3 for spot and point lights.
- Overdraw visualization: Metal, GL and the mode-off gates are unverified
  (`doc/plans/overdraw_visualization.md`).

## Culling

Plans: `doc/plans/culling.md`, `doc/plans/occlusion_culling.md`.

- User decisions: occlusion culling on by default; the step 5 second draw;
  the fill cost of large near boxes (screen-size threshold or hierarchical
  Z); an occlusion view for the headset's union frustum view.
- Survivor tracking is purpose-wide; color passes on the CPU path patch
  records per pass; a selected node makes the draw lists change every frame;
  the draws scene runs as one job.
- `cull: none` doubles the occlusion pass's fragments.
- KosmicKrisp: 3 - 8 pixel 1-LSB differences in selected image cases.
- GL and Metal are not run for the record indexing and GPU shadow command
  paths.

## Graphics backends and frame pacing

- Metal 4 gates at the Dell (pacing cached vs fresh, Metal 4 vs Metal 3,
  fullscreen re-gate on the Dell and ProMotion), then the Metal 3 keep or
  remove decision (`doc/plans/metal_backend.md` section 7).
- Barrier leftovers: Vulkan tracked-layout VERIFY, intra-dispatch barrier
  cleanup, the Metal 4 drawable wait tail
  (`doc/plans/resource_barriers.md`).
- In-place uploads in `doc/plans/mesh_memory.md` keep their write-after-read
  gap.
- Metal: `MTLBinaryArchive` (audit item 18).
- MoltenVK: the SPIRV-Cross inline-precise-helper fix needs an upstream PR
  and release (`doc/plans/apple_gpu_shader_gap.md`); swapchain min image
  count per present mode is untested on MoltenVK.
- Frame pacing (`doc/plans/frame_pacing.md`,
  `doc/plans/frame_pacing_adaptive_sync.md`): 60 Hz and ProMotion re-gates;
  continuous VRR cadence; the near-T alternation limit; MoltenVK fullscreen;
  Dell `displayUpdateGranularity`; Vulkan present calibration and prewarm on
  macOS; tier W on Linux.
- CI: LeakSanitizer is not enabled.
- SPIRV-Tools is pinned at upstream `19e35eae` with SPIRV-Headers from its
  DEPS, ahead of the `known_good.json` of the `tksuoran/glslang` commit
  (`CMakeLists.txt`); built and tested on macOS only. Check the Windows,
  Linux and Android CI builds, and move back to glslang's pair once glslang
  includes the fix.
- Window transparency: ImGui draws the viewport image with straight-alpha
  blending over premultiplied content, so partly transparent viewport pixels
  (a Clear Color alpha between 0 and 1, anti-aliased edges against it) come
  out darker than intended; the sky colors' alpha channel carries the falloff
  powers, so the sky itself cannot be transparent
  (`doc/erhe/window.md` "Window transparency").
- NVIDIA OpenGL: `#pragma optionNV(unroll none)` disables loop unrolling in
  every shader (`doc/erhe/shader_workarounds.md`); its frame-time cost is
  unmeasured.

## Rendering features

- Radiance cascades remaining work and the change-driven refit for both
  probe producers (`doc/plans/radiance_cascades.md` "Remaining work",
  `doc/plans/ddgi.md`).
- Shadow robustness review issues 2 and 4 - 11
  (`doc/reference/shadow_robustness_review_2026_09_29.md`); Metal runs and G7
  profiling (`doc/plans/shadow_robustness.md`).
- Debug line anti-aliasing: joined polylines, content wide lines, a Metal run
  (`doc/plans/debug_renderer_anti_aliasing.md`).

## Foliage scale

Costs that grow with the number of foliage items or mesh nodes, measured in
the scale step of the foliage paint tool (`doc/editor/level_tools.md`
"Foliage paint", Scale; working size 1,000 items,
`doc/plans/foliage_paint_tool.md` F12). Headless editor, one support
carrying 1,000 items.

- The carry of a surface attachment is one surface search per item:
  `Level_kits::compute_surface_carry()`
  (`level_kits_surface_attachment.cpp`, from
  `build_surface_carry_follow_ups()`) costs 17 ms per item in Debug and
  0.22 ms in Release, linear in the carried count: a shape change of a
  support with 1,000 items takes 17.6 s in Debug, 0.25 s in Release, and
  runs at every step of a live parameter drag. The remedy is a carry by
  proportional place without a search
  (`doc/plans/foliage_paint_tool_design.md` FD0.9).
- `Level_kits::build_auto_connections()` (`level_kits.cpp`) calls
  `get_local_connectors()` on every kit piece of the scene on every follow
  operation, about 35 us per piece in Debug: most of the time of moving
  one unrelated piece (7 ms at 1,200 items and 17 ms at 5,000 in Release).
  It should read only the pieces near the changed ones.
- `gather_attachments()` (`level_kits_attachment.cpp`) runs three times
  per follow operation, each a scan of the scene with a hash map of its
  own; one gather per follow operation serves all three.
- A saved scene carries each item's mesh, about 26 KB per item (36.6 MB
  at 1,200 items, 109 MB at 5,000), and the open uploads all of it before
  the kit refresh points the items at the shared kit brushes. Kit pieces
  need a saved form that names the kit brush and carries no mesh.
- Scene open: after the rendergraph prepare one frame takes 0.49 s at
  1,200 mesh nodes and 4.9 s at 5,000 (Release), growing faster than the
  node count (likely the raytrace scene preparation); open until idle is
  2.4 s and 13.5 s. In Debug the open of 1,400 items takes 74 s.
- Kit brushes are held only weakly, so every stroke and every scene open
  makes the brushes of its palette's variants again: about 1 s in Debug
  and 80 ms in Release on the first stamp of each stroke. The tool's
  palette can hold its brushes while the tool is active.
- A reopened scene runs about 1.5 ms per frame slower than the same scene
  before its save (the cause is not known).
- Items painted in one MCP call stay out of the raytrace scene's BVH for
  30 commits, so the rays of that call's later stamps test them linearly;
  interactive painting commits once a frame and is not affected.
- `erhe.raytrace.geometry` logs one info line per mesh at load ("BVH
  build ... in 0 ms"): one line per foliage item.

## Verify scripts

- `Mcp_test.property_row_is_addressable_by_its_label` fails in the
  Windows headless Debug `ctest -L editor` when the working tree has the
  user's own edits of `config/editor/desktop_windows.json` /
  `editor_settings.json`, and passes with the committed config.
- `scripts/viewport_input_focus_verify.py` fails check 6.1 (a brush dragged
  from the Hierarchy and dropped on a box adds one node): the drag never
  starts. `erhe::imgui::begin_drag_drop_source()` (`imgui_helpers.cpp`)
  requires the source item to be hovered on the frame the drag starts, and
  the script's 20-frame `mouse_drag` leaves the Hierarchy row on its first
  step, before ImGui reports a drag, so no payload is set; whether it
  starts depends on the window layout. Decide: drop the hover gate (ImGui's
  own `BeginDragDropSource` checks the active item), or keep the script's
  first drag step inside the row.
- `scripts/scene_roundtrip_verify.py` on Windows passes its checks and
  then exits 1: its cleanup unlinks the `phase6_*.erhe_editor` files under
  `res/editor/scenes/` while the running editor still holds the stores
  open after `close_scene`, which Windows refuses (WinError 32). The files
  stay behind (gitignored).
- `scripts/shader_fp16_verify.py` station `grazing` times out in the
  Windows headless Debug editor: `create_shape` of the 192 x 96 uv sphere
  takes about 10.6 s against the 10 s MCP client timeout. The other four
  stations pass.
- `Mcp_test.get_async_status_reports_background_work` times out (60 s) on
  the first `ctest -L editor` run after a shader source change in Debug:
  the shader module stats are recomputed on the cold SPIR-V cache and
  keep `background_idle` false. The second run passes.
- `scripts/mesh_modeling_verify.py` has no section selection, so its
  component box and paint select checks run only with the whole script.
- `scripts/undo_reference_clearing_smoke_test.py` fails 1 of 58 on the
  headless editor: "tool references: brdf_slice armed". The BRDF slice gets
  its material only while the Properties window draws the selected material
  with its "BRDF Slice" tree node expanded (`Brdf_slice::show_brdf_slice()`,
  called from `properties.cpp`), and the script selects the material without
  expanding that node. The script also has no port or editor option: it
  needs a wrapper that starts the editor and sets its `PORT`.
- `curve_verify.py` fails 1 of 157 on Linux headless (2026-11-20): "a
  magenta curve_selected_color ... draw the selected curve magenta at the
  next start (848 vs 0)". Reported cause, not confirmed: the curve tool's
  point gizmos (commit "curve tool: the pointer brings a translate gizmo to
  the nearest point") cover most of the selected curve in the image.
- `ctest -L editor`: `Mcp_test.undo_of_gltf_import_clears_the_animation_target`
  failed once of two runs on Linux headless (2026-11-20); not looked into.
- `level_tools_verify.py` depends on which windows the tracked
  `config/editor/desktop_windows.json` shows: with `"operations": false`
  twelve Brush tool hover / click placement checks fail (`--only-brush-normal`),
  and with `"asset_browser": false` one Create > Level Kit node-row click
  fails (`--only-create-menu`); four `ctest -L editor` cases
  (`imgui_click_focuses_a_text_field...`, `imgui_hover_and_scroll...`,
  `imgui_scroll_to_item...`, `capture_screenshot_annotates_imgui_items`)
  fail the same way. `ERHE_DEFAULT_LAYOUT=1` covers the dock layout only.
  To look at: why a hidden window changes viewport hover and clicks, and
  whether scripted runs should take their window set from the run and not
  from the file. `level_tools_verify.py --cwd <dir>` runs against a copy of
  the committed config until then.
- Linux headless Release tree, 2026-11-20, with locally modified
  `config/editor/desktop_windows.json` and `editor_settings.json` in the
  working tree: `level_tools_verify.py --only-brush-normal` fails 11 of 51
  (the pointer over the slab hovers the origin, clicks place nothing),
  `--only-create-menu` 1 of 48 (Create > Level Kit > Basic > Block on a node
  row), and `ctest -L editor` 4 `Mcp_test.imgui_*` / `capture_screenshot_annotates_imgui_items`
  tests ("No item labelled 'Merge'"). Cause not looked into; first check
  whether they pass with the committed config files.
- Deterministic clock and no time-based sleeps (AGENTS.md "Waiting for the
  editor: no time-based sleeps"; `doc/testing.md` "Verify scripts"). The
  scripts that launch the editor through `prepared_asset_verify.Editor` and
  `mcp_server_tests` follow the rule. These launch the editor themselves
  without `ERHE_DETERMINISTIC_CLOCK=1` and still sleep on editor waits:
  `pvs_verify.py`, `shadow_map_cache_check.py`, `creations/common.py`,
  `shadow_verify.py`, `gi_verify.py`, `visible_entries_check.py`,
  `shader_fp16_verify.py`, `shadow_fit_ab.py`, `mesh_modeling_verify.py`,
  `mesh_ab_capture.py`, `lod_dolly_verify.py`, `lod_roundtrip_verify.py`,
  `physics_node_pose_gate.py`, `ik_interactive_pass_verify.py`,
  `geometry_spreadsheet_verify.py`, `attr_encoding_ab.py`,
  `brush_geometry_queue_soak.py`, `usd_wg_asset_survey.py`. Scripts that
  drive an editor someone else started also sleep on editor waits:
  `scene_roundtrip_verify.py`, `geometry_nodes_smoke_test.py`,
  `test_editor_mcp.py`, `texture_graph_smoke_test.py`, `overdraw_verify.py`,
  `rc_texel_verify.py`, `occlusion_wall_check.py`,
  `geometry_graph_zoom_harness.py`, `physics_drag_joint_sweep.py`,
  `content_draws_verify.py`, `joint_constraint_assets_verify.py`,
  `reloadable_asset_loads_smoke_test.py`, `undo_reference_clearing_smoke_test.py`,
  the `mcp_ragdoll_*.py` scripts and the creations. Wall-time measurements
  (`bistro_frame_bench.py`, `render_thread_bench.py`,
  `tracy_startup_profile.py`, `scene_load_profile.py`,
  `frame_pacing_live_gate.py`, `vk_alloc_census.py`, `idle_memory_check.py`)
  and device scripts (`quest_bench.py`, `soak_quest.py`,
  `device_loss_repro.py`) keep the wall clock; their editor waits still
  follow the rule.

- Optimized editor for verify script runs (user, 2026-11-17: future work,
  because of the build time). A RelWithDebInfo headless editor runs the full
  `level_tools_verify.py` in about 220 s against 290 s in Debug and
  `curve_verify.py` in about 27 s against 65 s, with the same results;
  building it from scratch takes about 5 minutes. `ERHE_VERIFY` /
  `ERHE_FATAL` stay active; `assert()` and the libraries' debug checks do
  not.

- The tracked config files `desktop_windows.json` and
  `editor_settings.json` still change a headless run when a windowed session
  modified them; a test launcher could ignore them as `ERHE_DEFAULT_LAYOUT`
  does the dock layout.

## Editor features

- Kit items, further targets and sources (`doc/editor/level_tools.md`
  "Dragging a kit item"): the foliage tool palette accepts a dragged kit item
  (user, 2026-11-22: "later also to foliage tool palette"); a kit slot dragged
  from the Inventory window into a viewport places its piece; the hotbar
  overlay itself accepts drops; the XR radial menu shows a kit slot's
  thumbnail. For the user to review: a drop places one piece and leaves the
  tool as it was, a non-chosen row carries the shape's defaults, a kit slot's
  values are used as stored (not resolved against the scene's lengths again),
  and a kit slot in the Inventory grid activates on a click.
- Property rows: migrate the remaining hand-written `Property_editor` rows
  (each named by a `Property_row_id` enumerator) into registered properties,
  so every row is identified by its property; then remove `Property_row_id`
  (`doc/editor/properties_window.md` "Row ids").
- Asset fetch: show each source's license beside its Fetch button (the
  Khronos Sponza's Cryengine Limited License Agreement, the Intel scenes'
  CC BY 4.0; `doc/editor/asset_fetch.md`).
- Window defaults: the PVS, LOD and Light Mixer windows are hidden by
  default (user, 2026-11-15; `Imgui_window::set_default_visibility`). Today
  they are visible by default and open floating over the viewport when
  `desktop_windows.json` has no entry for them.
