# Potentially visible sets

Stability: experimental

A potentially visible set (PVS) is, per cell of a uniform grid over a
region of the scene, the set of draw-list entries that can be seen from
some point of the cell. The editor bakes it offline with the disocclusion
buffer method of Kuenzel et al., "Potentially Visible Set Generation with
the Disocclusion Buffer", SIGGRAPH Asia 2025
(https://doi.org/10.1145/3757377.3763981; the reference implementation is
the DaRUS dataset https://doi.org/10.18419/DARUS-5385), stores it in the
scene file, and the frame's culling classification reads it (on the GPU,
and on the CPU lanes that check the GPU) to clear a viewport view's mask
bit of every baked entry outside the PVS of the cells around the camera.
The CPU does no per-entry work per frame.

This document holds the method, its rules (D1 to D7, cited from source
comments) and its verification. The parts are described where their code
lives:

- The database: `doc/erhe/scene.md` "Potentially visible sets"; its file
  format: [`ERHE_pvs`](../gltf_extensions/ERHE_pvs.md); its save and open:
  `doc/editor/scene_serialization.md`.
- The classification rule and the staleness mechanism:
  `doc/erhe/draw_list_renderer.md` Q6 "PVS"; the resident pages and their
  streaming: Q6 "PVS resident set and page table".
- The bake's raster pass and disocclusion passes:
  `doc/erhe/draw_list_renderer.md` 9.2 (`draw_pvs_face()`, "PVS
  disocclusion passes"); their shader variants:
  `doc/erhe/shader_variants.md`.
- `Pvs_baker` and `Pvs_bake_node`: `doc/editor/renderers.md`.
- The viewports' query boxes and the PVS settings:
  `doc/editor/rendering.md`; the PVS window: `doc/editor/windows.md`.
- The MCP tools: `doc/agents/mcp_server_usage.md` (`get_pvs_status`,
  `pvs_bake`, `pvs_debug_face`, `get_pvs_bake_status`, `pvs_cancel`,
  `pvs_clear`, `get_pvs_cell`, `get_visible_entries`, `set_pvs_culling`,
  `set_pvs_streaming`); the id capture behind `get_visible_entries`:
  `doc/editor/editor.md` "Whole-image id capture".

## Why a baked PVS next to the occlusion test

The occlusion test (`doc/plans/occlusion_culling.md`) is from-point and
reads the previous frame's bits: it lags one frame and costs fill
proportional to the screen area of the tested boxes. A PVS is from-region
and valid for every viewpoint in the cell: no lag, no per-frame raster cost
beyond a few buffer reads per entry, and one query box covers both eyes
and the head motion of a frame. Its costs are a preprocess, a database and
the restriction to static entries (D3). The two compose: the PVS removes
entries first, so the occlusion pass rasterizes fewer boxes and catches the
dynamic entries and the residual false positives the PVS keeps.

The disocclusion buffer method is the bake because its passes are
order-independent (no global front-to-back order of the geometry, unlike
depth or layer peeling) and map onto the compute and raster paths erhe
has, and because it is conservative by construction: every surface the
cell can see through an opening between depth layers is kept. Sampling
viewpoints is not conservative; it stays the verification oracle (the
`sampled` bake method and `get_visible_entries`).

## Terms

- Entry: one `Draw_list_entry` of the scene's `Draw_list_scene` (one mesh
  primitive of one mesh node). The PVS granularity is the entry, the unit
  the classification culls and the command generation draws; an entry is
  visible when any of its triangles is.
- Column: the database's index of one baked entry, assigned at bake time.
  At runtime every entry maps to its column or to "untracked" (never
  culled by the PVS).
- Identity of a baked entry: the mesh node's glTF uid
  (`Item::get_gltf_uid()`, assigned once and never changed,
  `doc/editor/scene_serialization.md`) and the mesh primitive index. A bake
  gives every mesh node it bakes that has no uid one
  (`erhe::gltf::generate_gltf_uid()`).
- Cell: one axis-aligned box of the bake grid, the paper's viewcell. The
  PVS of a cell is one bit per column. Page: `page_size`^3 cells, the unit
  of storage, GPU residency and streaming.
- Face: one of the six perspective views the bake rasterizes per cell.
- Sample point: the point in the cell the faces are rendered from.
- Layered buffer: the disocclusion buffer of one face, the closest sample
  per (pixel, depth layer).
- Tile and chunk: a tile is K x K pixels; a chunk is a (tile, layer) pair.

## Rules

- D1 Storage. The database is the asset-root glTF extension `ERHE_pvs` of
  the scene file, written by the save pipeline from the scene's in-memory
  database and read by the open pipeline, like `ERHE_raytrace_bvh` and
  `ERHE_gpu_vertex_streams`, so it travels in the one `.glb` that "Save
  for Quest" produces. A PVS is small and baked whole, so it lives in the
  scene file; a sidecar directory (`<scene>.lightmap/`) is for the large,
  partially baked lightmap tiles.
- D2 Bake region. A box and a cell size in the scene's `Scene_settings`
  (`pvs_region_min`, `pvs_region_max`, `pvs_cell_size`, saved through
  `ERHE_scene`), edited in the PVS window ("Fit to content bounds" sets the
  box to the static content's bounds). The cells are a uniform grid over
  the box (`Pvs_baker` in `doc/editor/renderers.md` computes it).
- D3 Static set. The bake gives a column to the entries that are content,
  visible at bake time, not skinned, with valid bounds, opaque or alpha
  tested, on a node whose ancestors and itself carry no rigid body of a
  motion mode other than static and are the target of no animation
  (`Pvs_baker` in `doc/editor/renderers.md` names the checks). Translucent
  entries never occlude and are always kept (the frustum and occlusion
  tests still apply); alpha-tested entries occlude only where their texels
  pass the alpha test, which the bake's shader variant keeps. An entry
  hidden at bake time gets no column, so showing it later culls nothing.
- D4 Staleness. The database is valid while every baked entry is present
  with its baked transform and bounds. Moving a baked entry, re-registering
  it with other bounds or removing it marks the database stale, which
  turns PVS culling off for the scene until the next bake; a transform
  that restores the baked state keeps it stale. A moved occluder exposes
  what it hid and no rule short of a rebake restores conservativeness, so
  the whole database goes. Entries added after the bake are untracked. The
  mechanism is in `doc/erhe/draw_list_renderer.md` Q6 "PVS"; the editor
  routes it through `Scene_root::mark_pvs_stale()`, which announces
  `Pvs_state_changed_message`, and the PVS window shows a warning line. An
  edit that would stale the database fails a running bake.
- D5 Views. The viewport scene views with a perspective camera (four view
  included) get PVS culling, the same set as the occlusion views; previews,
  thumbnails, scene image captures, the id pass and the shadow passes get
  none. Which render contexts register a query box:
  `doc/editor/rendering.md`. The headset's multiview pass registers the
  box around its located eye positions grown by `pvs_query_margin`, built
  in the prepare phase with the union culling view
  (`Headset_view::prepare_pvs_query()`), and streams pages for it like a
  viewport; the per-eye (non-multiview) path registers none, as for
  occlusion.
- D6 Where the bake runs. In the editor: GPU work recorded in the render
  phase, one cell per frame, the way the lightmap offline bake runs
  (`doc/editor/lightmap_baking.md`), driven from the PVS window and from
  MCP, so a headless editor bakes a scene unattended and the windowed
  editor shows progress.
- D7 Bake hardware. The bake uses 32-bit buffer atomics and dense per-face
  buffers only (two raster passes per face: depth, then id), so it runs on
  every device with fragment stores and atomics and first-instance record
  naming (`Draw_list_scene::supports_pvs_faces()`), on either
  classification path, with no 64-bit atomics, no sparse allocation and no
  forward-progress guarantee between GPU threads. It is an offline tool for
  the desktop: its layered buffer is 768 MB at the defaults, and it never
  runs on the headset.

## Bake

Per cell, in the order of the cell index, one cell per frame (D6), with the
scene's draw lists published for the frame as every other pass sees them.
The faces draw the static set (D3) through the draw lists' column arrays.
The id the layered buffer stores is the bake's frame-local entry index (the
classifier's entry order, which the record index names), which the CPU maps
to the column. The bake parameters are `erhe::scene::Pvs_bake_parameters`;
the defaults below are the ones the PVS window bakes with.

1. Sample point: the first free candidate of the cell center and the 26
   points a quarter cell away per axis (the free test: `Pvs_baker` in
   `doc/editor/renderers.md`). A cell without a free candidate gets no PVS
   (everything visible).
2. Near plane: `z_near` is the largest distance from the sample point to a
   face of the cell box plus 1 cm, so every point of the cell lies behind
   every face's near plane, as the cones need. Entries whose bounds reach
   into the cube `[-z_near, z_near]^3` around the sample point are visible
   without a test (step 5, the bounds vote).
3. Faces: six perspective views from the sample point along +X, -X, +Y,
   -Y, +Z, -Z with field of view `face_fov` (default 120 degrees: the faces
   overlap, so a cone leaving one face's 90-degree core is still inside
   that face) at `face_resolution` square (default 1024), tile size K =
   `tile_size` (default 8, a divisor of the resolution), N = `layer_count`
   depth layers (default 64; at most 64, the layer masks of the passes are
   64 bits), far plane `z_far` the farthest corner of the region and the
   static entries' bounds from the sample point (at least `2 z_near`).
   Layer k holds the linear view depths in `[z_near * r^k, z_near *
   r^(k+1))`, `r = (z_far / z_near)^(1 / N)`
   (`erhe::scene_renderer::pvs_layer_of_depth()` and its GLSL twin in
   `erhe_pvs_layer.glsl`; the GPU's log is not correctly rounded, so a depth
   within a few ulps of a layer boundary may land in the neighbouring
   layer, and no pass compares depths across layers). With
   `samples_per_face` above 1 each face repeats with a sub-pixel jitter of
   the projection and the PVS unions the results.
4. Layered buffer: dense `depth`, `id_min` and `id_max` arrays of
   `face_resolution^2 * layer_count` uint32 words each, layer-major (word
   `layer * resolution^2 + y * resolution + x`, so one layer is one
   contiguous range). The depth pass (`VARIANT_PVS_DEPTH`) `atomicMin`s
   each fragment's linear depth bits into its layer; the id pass
   (`VARIANT_PVS_ID`) runs only for a bake trace and `pvs_debug_face`,
   since the vote reads bounds, not ids.
5. Disocclusion, per face: the paper's Algorithm 1 as the DaRUS source
   resolves it, with the rules below (the passes:
   `doc/erhe/draw_list_renderer.md` 9.2 "PVS disocclusion passes").
   - Rule 1, run closure with sheet continuity. A tile column in which
     every pixel has a first hit, the first-hit layers form a gap-free run
     `[L_a, L_b]` and the sheet is continuous is one surface crossing layer
     boundaries at any slope: it is closed at every layer of the run, and
     the layers `L_a .. L_b - 1` in front of the run's back are not windows
     into it. The sheet is continuous when no 4-adjacent pixel pair of
     first hits has a gap: pixels a and b have a gap when they differ by
     more than 1.25 times the local per-pixel step `max(|a - a'|, |b -
     b'|)` (a' the pixel before a and b' the one after b on the same axis)
     plus 1e-4 of the larger depth (`pvs_gap_slope_tolerance`,
     `pvs_gap_depth_epsilon`); a pixel without a sample is infinitely deep.
     The epsilon keeps the one-ulp row-to-row noise of a sheet with zero
     local slope from reading as a gap. A run with a silhouette inside
     does not close its tile: a ray could pass from the free space in
     front of one part to behind the other without crossing geometry.
   - Rule 2, gated spawning. The layers are walked front to back; an open
     tile at layer L is a window only when its chunk is disoccluded (layer
     0 always), and layer L's cones are scattered before layer L + 1 is
     classified, so only chunks reachable through a window spawn cones.
   - Cone sides: every closed same-layer neighbour of a window is a growth
     side (the paper's rule). Rule 3 (`silhouette_tests`, a side only
     across a silhouette) is opt-in and off: `doc/plans/pvs.md` "Open".
   - Geometric cones: the hit rectangle on a deeper layer bounds the rays
     from the eight corners of the cell box through the window's four
     corners, clamped to the tile's own column on each side whose
     neighbour is open.
   - The disoccluded map (per chunk, with an "any" mip chain over the
     tiles) and the bounds vote: an entry is visible on the face when its
     bounds reach into the near cube, or when its bounds' tile rectangle
     (clamped to the face's 90-degree core: the six cores cover every
     direction, and the rim holds the windows and cones that reach the
     core's chunks) and layer range (widened by one layer on each side)
     contain a disoccluded chunk. A visible entry has a visible sample in a
     disoccluded chunk inside its bounds' box, so the vote keeps it; an
     entry fused behind another in one layer of a pixel, or too small to
     cover a sample, is kept by its bounds. The cell ORs its faces' votes.
   - Resolution limits: a gap narrower than a pixel, or a step below the
     gap test's tolerance, is not seen; occluders nearer than `z_near` are
     not rasterized (`doc/plans/pvs.md` "Open", near-plane clipped
     occluders).
   - Determinism: the passes compute only from parameter blocks that carry
     every quotient (multiply, add, compare and floor, with precise
     intermediates), and a CPU reference matches them bit for bit
     (`doc/erhe/draw_list_renderer.md` 9.2). A bake is deterministic per
     backend; Metal and Vulkan rasterize differently at the ULP level, so a
     few cells of a large bake differ between them (Bistro: 25 of 572), and
     each backend's gate is the contract.
6. Finish: the cell's column words are read back when its frame retires
   and ORed into the cell's words; after the last cell the baker builds the
   database, validates it and sets it on the scene (`Pvs_baker` in
   `doc/editor/renderers.md`).
7. Trace: `pvs_bake` with `trace_cell` and `trace_entries` (and
   `trace_only`, `trace_face`) bakes with the id pass on and reports, per
   face and traced entry, the depth-pass fragments, the samples naming it,
   those in the core and in a disoccluded chunk, the bounds vote's tile
   rectangle and layer range, the per-layer closed and disoccluded state
   of a tile column, and whether the column is in the cell's PVS
   (`doc/agents/mcp_server_usage.md`). A diagnostic path: it allocates.

## Verification

Editor-driven runs use the headless editor (on macOS
`build_xcode_metal_headless` and `build_xcode_metal4_headless`,
`doc/erhe/metal_headless.md`; macOS has no headless Vulkan tree, so
MoltenVK runs the windowed `build_xcode_vulkan`).

- Unit tests (deviceless): `erhe_scene_tests` `Pvs_database.*` (indexing,
  lookup, hash, validation, staleness, release and read back through a
  file source); `erhe_gltf_tests` `Gltf_pvs.*` (exact round trip through
  `ERHE_pvs`, no extension for a stale database or a column outside the
  export); `erhe_scene_renderer_tests` `Pvs_layers.*`, `Pvs_disocclusion.*`
  (the CPU reference: the paper's Figure 2 cases, the cone rectangle
  against sampled rays, rules 1 to 3, the bounds vote, random cases),
  `Pvs_residency.*`, `Pvs_page_streamer.*`, `Pvs_page_upload_queue.*` and
  the `Cull_lanes.pvs_*` cases of the classification rule.
- GPU tests (label `gpu`): `erhe_scene_renderer_gpu_tests`
  `Pvs_disocclusion_gpu_test.*` compare the GPU passes with the CPU
  reference bit for bit (Metal 3, Metal 4, MoltenVK and KosmicKrisp pass,
  including the 64-layer case).
- `scripts/visible_entries_check.py`: `get_visible_entries` returns the
  same set ten times from one pose on the wall scene of
  `scripts/occlusion_wall_check.py`, with the expected boxes, and the id
  pass covers what the color pass draws (alpha-tested and double-sided
  materials).
- `scripts/pvs_verify.py` (usage in its docstring), by section: `sampled`
  (the sampled bake of the wall scene: every cell keeps the wall and drops
  the six boxes behind it, a second bake gives the same `bits_hash`, a save
  and load reload the same database; the consumer culls the six boxes in
  both classification modes with 0 classification mismatches and images
  identical with `pvs_culling` on and off; moving a front box stales the
  database); `wall` (the disocclusion bake of the same grid: every cell
  keeps the wall and the four boxes in front of it, the cells keeping a box
  behind the wall are counted, the zero-misses check over every cell, a
  cell inside a 6 m box keeps that box and the one beyond its far wall);
  `heavy` and `bistro` (a disocclusion bake with 4 m cells and the
  zero-misses check over 20 random cells; bistro also compares images with
  the rule on and off from street-level poses); `streaming` (the baked
  Bistro saved and reloaded so the bits stream from the file, a capacity
  below the page count, a camera path of poses two pages apart: images
  identical with the rule on and off, 0 classification mismatches, resident
  pages never above the capacity, at most one upload per frame, residency
  check 0, evictions over the path). The zero-misses check draws random
  viewpoints and orientations in a cell and requires every entry
  `get_visible_entries` sees to be in the cell's PVS; the false-positive
  ratio is the PVS size over the union of the sampled sets, the paper's
  figure of merit.

Current state at the defaults (rule 3 off, 1024-pixel 120-degree faces,
K = 8, N = 64; the oracle samples 12 viewpoints in each of 20 cells, 48 for
the wall; misses / false-positive ratio):

| scene  | Metal 3 headless | Metal 4 headless | MoltenVK windowed |
|--------|------------------|------------------|-------------------|
| wall   | 0 / 1.241        | 0 / 1.241        | 0 / 1.241         |
| heavy  | 0 / 2.208        | 0 / 2.208        | 0 / 2.208         |
| Bistro | 0 / 7.715        | not measured     | 0 / 7.705         |

The wall bake's `bits_hash` is the same on all three; a run's ratio can
differ in the third decimal through the oracle's random samples (the PVS
sizes are equal). 12 of the 48 wall cells keep a box behind the wall (false
positives: `doc/plans/pvs.md` "Open"). Images are identical with
`pvs_culling` on and off and `get_cull_classification_check` reports 0
mismatches. A disocclusion bake costs about 125 to 155 ms per cell in a
Debug build on an Apple M4 Pro. The streaming section passes with
`pvs_resident_pages` 4 and radius 1 over 12 poses of the Bistro grid (44
pages, all stored, 7616 words each), and the other sections pass with the
default capacity (64 slots: nothing is evicted). The reference hashes of
these bakes are in `doc/plans/pvs.md` "Verification".

Runtime cost on Bistro (Release, headless Metal 3, Apple M4 Pro; the table
is in `doc/plans/pvs.md` "Verification"): with the camera outside the grid
the rule costs nothing measurable, and the classification dispatch grows
by 2 to 6 us with it on. At a street-level pose that culls 203 of 601
tested entries, PVS alone lowers GPU `Main` by 144 us (1956 -> 1812 us),
more than occlusion culling there (+22 us) or the two together (+30 us).

## Future work

- [plans/pvs.md](../plans/pvs.md): the Quest wall gate, rule 3, the near-plane clipped occluders, adaptive cells, page
  compression and the other open items.
