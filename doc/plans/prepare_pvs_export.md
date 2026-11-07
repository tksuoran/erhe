# Prepare and the potentially visible set: clean export

Status: proposed

Prepare (PC / Quest, `doc/editor/world_space_bake.md` "Prepare (export)")
writes the scene's existing PVS database (`doc/editor/pvs.md`) as it is. It
never bakes one. When Prepare also bakes static meshes to world space, the
PVS was baked on the unbaked meshes, and the written file loads with the PVS
stale. This plan makes the export clean.

`doc/plans/prepared_assets.md` takes over this subject: its R7.6 states the
PVS modes (`off`, `reuse`, `rebake`), R8.5 - R8.9 the two mesh forms and
the PVS bake that replace the designs below, and R6.1 the save rule that
keeps a bake valid across a Save. What stays useful here is "Established".

## User decisions (2026-11-06)

- A new scene export setting chooses the PVS mode of Prepare, three values:
  - `bake` (the default): Prepare throws away any PVS the scene has, bakes
    to world space, then bakes the PVS on the world-space geometry and writes
    that.
  - `reuse`: Prepare writes the PVS the scene happens to have.
  - `off`: Prepare writes no PVS.
- The PVS of a prepared file is to be baked after the world-space bake.
- The best way to run the `bake` mode cleanly is still to be designed in a
  fresh session ("Designs" below); nothing of it is built.

## Established

- Staleness (`doc/editor/pvs.md` D4): each PVS column records its mesh's
  world transform (12 floats) and world bounds (6 floats) and
  `transform_hash = compute_pvs_transform_hash()` over those bits
  (`erhe_scene/pvs_database.cpp`); `Draw_list_scene::check_pvs_object()`
  compares the hash of the loaded mesh (`world_from_node()`,
  `Mesh::get_aabb_world()`) bit-exactly. A world-baked mesh loads with the
  identity transform and tight world-space bounds, so every column of a
  baked mesh mismatches and the whole database goes stale.
- A loaded prepared primitive's bounding box is its POSITION accessor's
  min / max (cast double -> float, `gltf_fastgltf.cpp` prepared-data parse),
  and the round trip through the file is bit-exact: a PVS baked on meshes
  already in world space (live Bake Static To World Space operation, then
  `pvs_bake`, then Prepare) reloads `ready` (verified, Bistro
  `bistro_quest_lod_cell_4m_v4_ws.glb`, `doc/agents/quest.md`).
- Prepare's planner re-plans meshes already on an identity path (it reports
  them as world-baked); their geometry is unchanged, so the columns still
  match.
- `Mesh::get_aabb_world()` composes every primitive's box (translucent ones
  included) through `transformed_by(world_from_node)`; the PVS baker records
  that box (`Pvs_baker`, `src/editor/renderers/pvs_baker.cpp`).

## Set aside: the `reuse` column rewrite

`git stash` entry "prepare_pvs_export: ERHE_pvs column rewrite for
world-baked meshes (unverified, reuse-mode candidate)": in
`write_pvs_extension()` (`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`) a column
of a mesh the node pass wrote baked (`m_world_baked_meshes`, recorded where
`Node_bake_role::world_space_mesh` is chosen) takes the identity transform and
the union of its glTF primitives' POSITION bounds, composed as
`Mesh::get_aabb_world()` does, with the hash recomputed. It keeps the PVS
bits computed on the unbaked geometry (valid up to rounding), so it suits
`reuse` with world bake on, not `bake`. Built: no; verified: no.

## Designs for `bake` (to choose)

- A. In place: the save service runs `Bake_world_space_operation` on the live
  scene directly (outside the undo stack), bakes the PVS
  (`Pvs_baker::start_bake`, region and cell size from the export settings),
  runs the Prepare export (meshes already world-space: columns match), then
  undoes the operation and restores the scene's previous PVS database. Reuses
  verified machinery; the live scene is mutated for the duration (the bake
  operation's undo restores the tree exactly). The PVS bake renders frames,
  so the scene cannot be held read-only through it the way the export holds
  it (`doc/editor/scene_serialization.md` "Saving in the background"): edits
  during the bake must be held or must fail the job.
- B. Through a file: export world-baked without PVS to a temporary file, load
  it as a hidden scene, bake the PVS there, export again, close it. The live
  scene is never touched; costs a second export and load, and the second
  export re-writes already target-encoded textures (ASTC for Quest) - check
  that it passes them through.
- C. In the exporter: bake the PVS from the export's own world-baked
  primitives. Needs a renderable scene of them; likely B in disguise.

Open questions: where the staged job lives (`Scene_save_service::update()`
already runs per frame and owns quiescence; a Prepare job object beside it),
what the default region is when `pvs_region_min` / `pvs_region_max` are both
zero (the union of the static bounds at the default 1 m cell size is a huge
grid for an open scene), progress reporting in the status line and MCP
`get_save_status`, and cancel.

## Export setting

`Scene_export_settings` (`src/editor/scene/definitions/scene_export_settings.py`,
version 1) gains `pvs_mode` (an enum like `bake_instancing.py`, default
`bake`, `added_in=2`, struct version 2); the codegen file list in
`src/editor/CMakeLists.txt` names each definition and output. Properties
"Export" group and MCP `set_scene_settings` / `prepare_scene` pick it up
through the codegen.

## MCP `set_scene_settings`

Investigated 2026-11-06: a versionless nested object (`export_settings`
without `_version`) is refused with an error; the "silently dropped" report
of that session was a misread reply. Untested: a versioned
`export_settings` inside a settings object without the top-level
`_version` (the top level then deserializes as v1, which has no
`export_settings`); if that drops it silently, refuse it the same way.

## Verification recipe

Bistro, headless Release on NVIDIA (`VK_DRIVER_FILES=.../nvidia_icd.json`,
display unlocked), recipe and region in `doc/agents/quest.md` "Bistro scene
data on the device" (4 m cells, region x/z the content bounds, y 0.6 - 4.6;
2068 cells; the bake takes about 9 minutes since ad4d4bf56). Per mode:
prepare, reload, `get_pvs_status` `ready` (or absent for `off`), the
draw lists in `world_space` lists (`get_draw_lists` verbose), and a
`render_scene_image` of a street view. Unit-test the column rewrite with a
small scene in `erhe_gltf_tests` (`test_gltf_pvs.cpp` has column fixtures).
