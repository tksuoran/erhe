# Potentially visible sets: outstanding work

Status: in progress

This plan extends [../editor/pvs.md](../editor/pvs.md), which describes the
landed subsystem (the method, the rules D1 to D7, the bake, the
verification) and lists the documents of its parts. It is phase 8 of
[culling.md](culling.md) (a from-region visibility test next to the
per-view frustum test and the from-point occlusion test of
[occlusion_culling.md](occlusion_culling.md)).

The PVS is baked offline per cell of a grid with the disocclusion buffer
method, stored in the scene file and read by the culling classification on
the GPU. It is complete on the desktop; what remains is the headset (step
8), the open questions of the bake's conservativeness and tightness, and
the follow-ups listed under "Open".

## Decisions that govern the remaining work

- The rules D1 to D7 of `doc/editor/pvs.md` "Rules"; D1 (storage in the
  scene file), D2 (region and cell size in `Scene_settings`) and D4 (strict
  staleness) are the user's choices (2026-10-30).
- The database is paged for streaming (user, 2026-10-30): the page is the
  unit of storage, GPU residency and streaming.
- No Quest is available (user, 2026-10-30): the desktop steps did not wait
  for the headset; step 8 runs when a headset is attached again.
- The wall scene's leaking cells (12 of 48, false positives) are accepted
  for now (user: small leaks are acceptable, to be tightened later).
- Rule 3 stays off until the path in "Open" is understood.
- Every editor-driven gate runs on the headless editor (user, 2026-10-30;
  `doc/editor/pvs.md` "Verification").

## Where the landed sections went

Source comments cite sections of an earlier revision of this plan. Until
they cite the documents directly ("Open" below), the sections are:

- D1 to D7, "Why ...", "Terms", "Bake" 1 to 7 and "Verification":
  `doc/editor/pvs.md`, same labels.
- "Database": `doc/erhe/scene.md` "Potentially visible sets" and
  `doc/gltf_extensions/ERHE_pvs.md`.
- "Runtime consumption": `doc/erhe/draw_list_renderer.md` Q6 "PVS" and
  `doc/editor/rendering.md`.
- "Residency and streaming": `doc/erhe/draw_list_renderer.md` Q6 "PVS
  resident set and page table".
- "Editor": `doc/editor/windows.md` (`Pvs_window`),
  `doc/editor/rendering.md` (settings) and `doc/agents/mcp_server_usage.md`.
- "Build order": below.

## Build order

1. Database, persistence and the id capture (a1eb7cf57, c0c027750,
   1be73c5c4): `doc/erhe/scene.md`, `doc/gltf_extensions/ERHE_pvs.md`,
   `doc/editor/editor.md` "Whole-image id capture".
2. Sampled bake (5dacbfd5a): `Pvs_baker` in `doc/editor/renderers.md`.
3. Consumer (5fc6a519d): `doc/erhe/draw_list_renderer.md` Q6 "PVS",
   `doc/editor/rendering.md`.
4. Disocclusion bake, layered buffer (b2c629468): `draw_pvs_face()` in
   `doc/erhe/draw_list_renderer.md` 9.2, `doc/erhe/shader_variants.md`,
   `Pvs_bake_node` in `doc/editor/renderers.md`.
5. Disocclusion bake, passes (cc6ac8715 32-bit ids, 2a8ff412c):
   `doc/editor/pvs.md` "Bake", `doc/erhe/draw_list_renderer.md` 9.2 "PVS
   disocclusion passes", `doc/editor/windows.md`.
6. Streaming (0e7f3930b): `doc/erhe/draw_list_renderer.md` Q6 "PVS
   resident set and page table", `doc/erhe/scene.md`.
7. Documentation: `doc/editor/pvs.md` and the documents it lists; this
   plan reduced to what is outstanding.
8. Headset, when a Quest is available again. The headset's draw-list passes
   register a query box with the union view (both eye positions plus
   `pvs_query_margin`, computed in M2 with the union view,
   `Headset_view::prepare_cull_planes()`); the per-eye (non-multiview) path
   registers none, as for occlusion. On the Quest the database's header,
   columns and page table load with the scene and the pages around the
   headset stream from the `.glb`; per frame the headset pays the
   classification's extra reads and at most one page upload. Built and
   compiled in a tree with OpenXR; gated by the Quest wall gate
   (`scripts/quest_occlusion_wall_check.py` extended with the PVS) and the
   Quest measurement under "Verification". Until then the headset's views
   register an empty query box.

## Verification

The gates and their current results: `doc/editor/pvs.md` "Verification"
(`scripts/pvs_verify.py` sections `sampled`, `wall`, `heavy`, `bistro`,
`streaming`; `scripts/visible_entries_check.py`; the unit and GPU tests).
Reference values for a re-run:

- `bits_hash` of the sampled wall bake: c80c51abedaed3c5 on every backend.
- `bits_hash` of the disocclusion wall bake: 4fe439f63f8d385d on Metal 3
  headless, Metal 4 headless and MoltenVK.
- `bits_hash` of the headless Metal 3 Bistro bake: befefef64ac2a8e1 (also
  read back from the saved file by the `streaming` section).
- `streaming` with `--resident-pages 4 --stream-radius 1` on headless Metal
  3: uploads per pose 4, 1, 3, 2, 2, 2, 1, 1, 2, 2, 2, 2 (24) and the same
  evictions per pose; 28 uploads and 24 evictions over the run (4 uploads
  happen before pose 0, at the `frame_scene` camera's first position); PVS
  culled 22, 14 and 203 entries at three poses.

Still to measure:

- Cost: `scripts/render_thread_bench.py --tracy` on Bistro with PVS off,
  on, and on with occlusion: GPU `Main`, tick, R, the occlusion pass's
  time (fewer boxes) and the classification dispatch time.
- Quest (step 8): the headset Bistro benchmark
  (`doc/plans/quest_performance.md`) before and after, with the database
  baked on the desktop and saved into `bistro_quest.glb`.

## Open

- Silhouette test for cone sides (rule 3, opt-in, off). The paper slopes
  a window's cone toward every closed 4-neighbour. On a grazing surface
  (the wall scene: a wall seen along its length from the cells next to
  it) the neighbour of an open tile is the same surface one layer nearer,
  each layer's window sets that layer's bit behind the surface, and the
  surface reads as a silhouette at every layer: 12 of the 48 wall cells
  keep the boxes behind the wall. Rule 3 slopes a side only across a
  silhouette, found by a gap test between adjacent pixels (the shared
  test of rule 1's continuity; `pvs_side_has_gap()`, its GLSL twin, CPU
  and GPU tests, the bake parameter `silhouette_tests`). What was
  learned, in order:
  1. Gaps only along pixel rows across the shared edge, tolerance 2x the
     local slope: wall down to 2 leaking cells (the near-plane cells
     below), but a Bistro miss (cell 152, an entry of
     `Paris_Building_01` seen from 0.4 m above the street, 1.8 m below
     the sample point). At 1.25x the miss stays.
  2. Gaps in 2D (any 4-adjacent pixel pair inside the window, either
     axis), which also made rule 1's continuity condition: the Bistro
     entry returns, but only through ULP noise. A sheet with zero local
     slope (a wall's depth along a face's pixel column) differs by one
     ULP between rows, a purely multiplicative test calls that a gap, and
     the wall's 12 cells came back. The relative epsilon (1e-4 of the
     depth) fixes the wall (2 cells) and loses the Bistro entry again.
  3. Single-sided sheets were suspected (a plane cutting the cell, back
     faces culled from part of the cell): refuted. The deciding windows'
     surfaces are double-sided building walls below the street, seen
     through a hole in it, opaque from both sides.
  4. Voting each face's whole 120-degree image instead of its core (a
     chunk in one face's rim reachable only through that face's windows)
     put the entry in a one-cell trace, but not in the full bake, and
     doubled the wall's leaking cells (24); reverted.
  5. The one-cell trace then baked with a different depth range (z_far
     from its one-cell region): not a faithful reproduction. The trace now
     bakes only the traced cell of the full bake's grid
     (`pvs_bake trace_only`); with `silhouette_tests` off the entry is in
     the cell's PVS, with it on it is not (the known hole).
  Open question: which path from the cell reaches the cell-152 entry in
  the full bake, past the double-sided walls that rule 3 treats as one
  continuous surface (`pvs_bake trace_face` re-runs a face on the CPU with
  rule 3 on and off and names the deciding windows, pixel pairs and
  surfaces). Rule 3 stays off until that path is understood.

- Near-plane clipped occluders (future work, not done). With rule 3 on,
  cells 7 and 10 of the wall scene's 48 still keep one box behind the
  wall each (0 misses); with it off (the default) these cells are among
  the 12 that leak through the stair-step. Cause:
  a wall point within z_near along a face's axis is clipped by that face's
  near plane although it lies outside the near cube and the neighbouring
  face draws it, so the clipped ring of the face shows what is behind the
  wall, and its windows grow cones with the cell's parallax (the trace of
  cell 7, face +X: the box's middle tile column is closed at the wall's
  layer; the vote finds the disoccluded chunks where the wall is clipped).
  Candidate fix: raster each face with a near plane at z_near / 2 (every
  point outside the near cube inside a 120-degree frustum has an axis
  distance above z_near / sqrt(3)), discard in the fragment any sample
  inside the near cube (max(|x|, |y|, |z|) < z_near in face view space),
  and put samples with an axis distance below z_near into layer 0.

- Source comments (`src/erhe/scene`, `src/erhe/scene_renderer`,
  `src/erhe/gltf`, `src/editor`, `res/shaders`, `scripts/pvs_verify.py`,
  `scripts/visible_entries_check.py`, `config/editor/mcp_tools.json`,
  `CHANGELOG.md`) cite this plan's former sections ("Bake" N, D3, D4,
  "Runtime consumption", "Residency and streaming", "Database", "Editor",
  "Verification"); rewrite each to the document "Where the landed sections
  went" names, then drop that section.
- Adaptive cells (an octree over the region, subdividing cells with a
  non-free center or a PVS much larger than their neighbours').
- Peak memory at open: the parser still copies the whole bit array out of
  the file before the open releases it (`Pvs_database::release_bits()`), so
  the peak includes the bits once. Making the parser record only the bits
  source, without the copy, is future work.
- Hysteresis alternative: evict only pages farther than radius + 1 from
  every active view. It keeps a view's whole neighbourhood when it moves by
  one page, at a capacity of (2r + 3)^3 pages; worth it if a capacity below
  (2r + 1)^3 ever needs the full neighbourhood (today's rule serves small
  capacities with views that move by two or more pages).
- Page compression in the file (per-cell delta against the page's first
  cell, or a sparse column list for cells with small PVSs), decoded by the
  worker thread before the upload, when the file size matters.
- Prefetch along the camera velocity (requesting the pages ahead of the
  motion before the box crosses into them), if a page upload ever arrives
  after the frame that first needs it.
- Movable small objects: a bake flag "occludee only" for entries the bake
  draws into no layer (they never occlude) so moving them does not stale
  the database.
- Dynamic entries in their own per-frame layered buffer with the static
  buffer kept (the paper's future work); erhe's occlusion test covers them
  today.
- The sampled PVS as a bake method stays a verification tool; it is not
  offered as a production bake.
