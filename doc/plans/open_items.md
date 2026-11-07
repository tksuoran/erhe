# Open items across subsystems

Status: in progress

This plan collects outstanding items that have no plan of their own: known
defects, unverified paths, unbuilt trees and pending user decisions found
while working on other subsystems. Where a subsystem plan already holds the
full item, the entry names that plan. When an item is picked up, it moves
into its subsystem's plan (or is fixed) and its line here is removed.

## Work order

The tasks a session continuing earlier work picks up, in order (`AGENTS.md`
"Session handoff"). Items 1 and 2 come first; items 3 - 10 each need a user
decision or hardware (Quest, the external 120 Hz display, an NVIDIA GPU):
ask the user which to pick.

0. Prepared assets on Quest: finish the device testing, re-measure R13.4,
   fix the PVS item's empty scene id, then the user rulings. Read
   "Prepared assets" below and `doc/plans/prepared_assets_design.md` D8.
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
7. Quest 3 performance (needs the headset): `doc/plans/quest_performance.md`.
8. Scene round-trip failures: `doc/plans/scene_roundtrip_failures.md`.
9. Audit follow-ups: `doc/plans/audit_2026_09_30_followups.md`.
10. Bistro load decisions (step 5 KTX2, step 7c lazy attribute stores,
    geogram colocate serialization, worker release reopen cost):
    `doc/plans/bistro_load_profiling.md` "Status".

## Quest session follow-ups

- `origin/lod` is stale after its rebase onto `main`: force-push or delete
  (user decision).
- Re-pose `config/editor/benchmark_bistro.json` and
  `config/editor/benchmark_bistro_street.json` for the upright Bistro (the
  source rotated -90 degrees about X); `benchmark_bistro_street_120.json`
  is already in that frame.
- Textures look blotchy on the Mac (plant leaves).
- The loading screen shows a generic reason after a fast failure (the load
  handle is reaped before the screen reads it).
- Metal counts overdraw per sample under MSAA
  (`doc/plans/overdraw_visualization.md`).
- The right trigger still selects while travelling.

## Prepared assets

Plans: `doc/plans/prepared_assets_design.md` (D8 per-step "Open" notes),
`doc/plans/prepared_assets.md` (R numbers). Behavior:
`doc/editor/prepared_assets.md`.

- Quest: re-measure R13.4 (load time and VmHWM) on the current build; the
  open's peak memory is about 14 MB above the plain glb (eight read
  connections and the byte sources hold about 8 MB; page cache during the load
  about 7 MB).
- The PVS item written by Export's rebake carries an empty scene id; the
  device logs "baked for scene id ''" and still applies the database. Export
  records the baked form's scene id in the PVS item.
- User rulings pending on the coder decisions recorded in D8 steps 12, 14
  and 15 (whole-scene key collect per change; the automatic read-only status
  job; Scene-item-only edits and material swaps not hooked into the status;
  load key uses the Export LOD; what is not loaded from items).
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

- Closing a scene while a mesh is selected fires the VERIFY in
  `draw_list_scene.cpp` `get_published_slot` (via
  `Lod_drawn_levels::prepare`); reproduces with a plain `.glb`.
- Undo of `delete_nodes` on a mesh under a holder node restores a different
  world transform.
- `Lattice_tool::update_active_lattice` walks every node of the active scene
  per frame per view (from `Transform_tool::update_for_view`, about 6.5 ms per
  frame in Debug Bistro); make it change-driven.
- Scene close in Release stalls about 10 s in `Hierarchy::set_parent`
  (about 2800 calls from the `Scene_root` destructor inside
  `Viewport_scene_view::prepare_render`).
- Quest: a launch with no scene aborts in `scene_view.cpp:332`.
- Quest: `render_scene_image` crashes (SIGSEGV in
  `Render_pass::start_render_pass` from `Shadow_renderer::draw` in the capture
  chain), also with a plain glb.
- Quest: the device's writable `mcp_tools.json` is the copy made at first
  install and goes stale.
- A malformed PNG fires a VERIFY in the wuffs decoder path.
- TRS values drift in float precision on re-save.
- Three Scene Hierarchy MCP tests fail locally (suspected: the ini's Scene
  Hierarchy height 37; unverified at HEAD).
- `scene_roundtrip_verify.py` failures: a `Graph_texture` is destroyed on the
  render thread (`asset_manager.cpp:343`); see
  `doc/plans/scene_roundtrip_failures.md`.
- The id renderer's region-scan readback sometimes returns 0 entries
  (`gate_id.py` JSON varies run to run).
- The null-backend editor crashes in the `get_command_buffer` stub
  (`doc/plans/audit_2026_09_30_followups.md`).
- macOS: with the Vulkan SDK `setup-env.sh` sourced, SDL's Cocoa surface
  creation gets MoltenVK's `vkGetInstanceProcAddr` and every GPU test and the
  editor segfault (not fixed in code). Measure the editor's SIGTERM exit on
  macOS: exit now waits only for the running shader stats jobs
  (`doc/editor/shader_analytics.md` "Session programs").
- Mesh editing: no weld after an in-place edit; per-facet texcoords with
  seams keep no LOD chain; a reparent that changes only the carrier is not
  re-resolved.
- fp16: the first render after a variant switch is an outlier in the chains.

## Interactive checks for the user

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

## Performance and measurement

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

## Rendering features

- Radiance cascades remaining work and the change-driven refit for both
  probe producers (`doc/plans/radiance_cascades.md` "Remaining work",
  `doc/plans/ddgi.md`).
- Shadow robustness review issues 2 and 4 - 11
  (`doc/reference/shadow_robustness_review_2026_09_29.md`); Metal runs and G7
  profiling (`doc/plans/shadow_robustness.md`).
- Debug line anti-aliasing: joined polylines, content wide lines, a Metal run
  (`doc/plans/debug_renderer_anti_aliasing.md`).
