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
8. Headset. The query box and its registration are built
   (`Headset_view::prepare_pvs_query()`, `doc/editor/pvs.md` D5) and
   measured on a Quest 3 ("Verification"). Left: the Quest wall gate
   (`scripts/quest_occlusion_wall_check.py` extended with the PVS).

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

Cost on Bistro (Apple M4 Pro, Release, headless Metal 3, the baked
Bistro loaded with `load_scene`, `gpu` classification, the occlusion
measurement's recipe: pipelined render thread, `force_disable_vsync`,
frame pacing off, Tracy GPU zones, one editor run of 12 s per
configuration, two runs each; medians over 2700 to 4000 frames per run,
the two runs' means; the runs differ by at most 30 us, at the bench camera
by up to 120 us; GPU `Main` / tick / R in us):

| Pose | off | PVS | PVS + occlusion | occlusion |
|---|---|---|---|---|
| framed (`frame_scene`) | 3365 / 4517 / 1476 | 3359 / 4518 / 1488 | 2304 / 3474 / 1354 | 2295 / 3466 / 1336 |
| bench camera | 3250 / 4441 / 1495 | 3280 / 4471 / 1485 | 2048 / 3229 / 1268 | 2037 / 3220 / 1300 |
| street, cell 458 | 1956 / 3132 / 1297 | 1812 / 2993 / 1294 | 1986 / 3172 / 1289 | 1978 / 3161 / 1294 |

- The grid covers the street slab (y 0.2 to 4.2 m), so the framed camera
  and the bench camera (y 9.7 m) lie outside it: the rule tests nothing
  there, and PVS on costs nothing measurable (within the run-to-run
  spread). The street pose (the `streaming` section's pose 9, the cell
  center at 2.2 m) culls 203 of 601 tested entries.
- Street pose: PVS alone is the fastest configuration, GPU `Main` -144 us
  against off (opaque fill drawn 609 -> 410, tick -139 us). Occlusion
  alone costs +22 us there (its pass costs more than removing 460 of the
  opaque fill's entries saves), and occlusion with PVS +174 us against
  PVS alone.
- With occlusion on, PVS shrinks the occlusion pass's boxes (street: 630
  -> 427 tested; the PVS-culled entries are not tested) but the opaque
  fill draws the same 149 entries and GPU `Main` stays within 10 us of
  occlusion alone: the box count saving is below the noise at that pose.
  The pass runs inside the view's render pass and Metal times only stage
  boundaries, so its own time is not separable from `Main`.
- The classification dispatch (`Cull classification` timer) takes about
  20 us with PVS off and 22 to 26 us with it on; `Cull command
  generation` 25 to 36 us in every configuration. R does not change with
  PVS.
- Occlusion against off: framed -1070 us, bench camera -1213 us (on
  Metal 3; the 2026-10-24 numbers of `doc/plans/occlusion_culling.md` are
  Metal 4 from an earlier tree, so not directly comparable).

Quest 3 (2026-10-31; release APK, 72 Hz, multiview, GPU
classification, no MSAA, the six street-slab bakes of 6 to 1 m written
with Save for Quest by the headless editor; `benchmark_bistro_street.json`:
a 2 m walk at 2.2 m height through the street pose over 600 measured
frames, a timedemo with the same frames every run; two runs per
configuration; fps per run, "-" = cell size not applicable). The results
are bit-identical to the desktop bakes (`bits_hash` read back on the
device); every needed page was resident before the measured window.

| PVS cell | occlusion | clustered (43 lights) fps | one directional light fps |
|---|---|---|---|
| off | off | 10.0 / 10.0 | 20.3 / 20.5 |
| off | on | 13.1 / 13.0 | 49.4 / 49.7 |
| 6 m | off | 12.0 / 12.5 | 38.4 / 38.8 |
| 5 m | off | 12.0 / 12.0 | 38.2 / 38.1 |
| 4 m | off | 12.1 / 12.2 | 39.7 / 39.7 |
| 3 m | off | 12.0 / 12.1 | 39.4 / 39.5 |
| 2 m | off | 12.9 / 11.9 | 38.0 / 37.8 |
| 1 m | off | 12.6 / 12.2 | 40.9 / 40.9 |
| 6 m | on | 14.0 / 13.1 | 49.6 / 49.9 |
| 5 m | on | 13.0 / 13.0 | 49.8 / 49.7 |
| 4 m | on | 13.1 / 13.1 | 49.6 / 49.9 |
| 3 m | on | 13.1 / 13.0 | 49.6 / 50.0 |
| 2 m | on | 13.0 / 13.1 | 49.5 / 49.6 |
| 1 m | on | 13.1 / 13.1 | 49.6 / 50.0 |

- Occlusion is the larger win; PVS on top of occlusion adds nothing
  measurable at any cell size (both leave the same opaque set, 327 of
  3304 entries at the walk's last frame).
- PVS alone: smaller cells help only without occlusion, and not in cell
  size order (1 m best, 2 m worst, about 3 fps apart with one light);
  per-frame culled counts are not recorded, so the 2 m result is
  unexplained.
- Clustered shading dominates the frame (the multiview pass stays near
  75 ms with every culling on).
- 3 of the 56 runs had an intermittent compositor slowdown (compositor
  GPU time above 3 ms against 1.6 ms, the multiview pass 30 to 65 ms
  longer on the same frames); those configurations were rerun and the
  slowed runs are excluded.

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
