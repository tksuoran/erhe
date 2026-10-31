# Potentially visible sets: offline disocclusion-buffer bake, GPU runtime culling

Status: proposed

This plan extends [culling.md](culling.md) (phase 8: a from-region
visibility test next to the per-view frustum and occlusion tests) and
[occlusion_culling.md](occlusion_culling.md) (the from-point raster test
with its one-frame lag). It adds two things to `erhe::scene_renderer` and
the editor:

1. An offline preprocess that bakes, per cell of a grid over the scene, the
   set of draw-list entries that can be seen from any point in the cell (the
   cell's potentially visible set, PVS) with the disocclusion buffer method
   of Kuenzel et al., "Potentially Visible Set Generation with the
   Disocclusion Buffer", SIGGRAPH Asia 2025
   (https://doi.org/10.1145/3757377.3763981; the reference implementation is
   the DaRUS dataset https://doi.org/10.18419/DARUS-5385). The result is a
   database stored with the scene.
2. A runtime consumer: the frame's culling classification reads the
   database on the GPU and clears a color view's mask bit of every entry
   outside the PVS of the cells around the view's position, before the
   frustum survivors reach the occlusion test and the command generation.
   The CPU does no per-entry work per frame.

Reference material for the method: the paper digest and the transcription
of its three compute passes from the DaRUS source are machine-local
(`memory-bank/local/papers/disocclusion_buffer/README.md`, with the PDF and
the unpacked shaders beside it); this plan states everything the
implementation needs, and the digest is the place to check a detail of the
reference implementation.

## Why a baked PVS next to the occlusion test

The occlusion test (occlusion_culling.md) is from-point and reads the
previous frame's bits: it lags one frame, it costs fill proportional to the
screen area of the tested boxes (Bistro bench camera +1 ms), and it needs
the headset's union view. A PVS is from-region and valid for every
viewpoint in the cell: no lag, no per-frame raster cost beyond a few buffer
reads per entry, and one query box covers both eyes and the head motion of
a frame. Its costs are a preprocess (minutes per scene), a database (hundreds
of kilobytes to a few megabytes), and the restriction to static entries.
The two tests compose: the PVS removes entries first, so the occlusion
test rasterizes fewer boxes (the fill-cost item of occlusion_culling.md
"Open" shrinks with it) and catches the dynamic entries and the residual
false positives the PVS keeps.

The method is chosen over the classical from-region alternatives (sampled
viewpoints, which are not conservative; depth-peeling or octree layer
peeling, which need a global front-to-back order) because its passes are
order-independent and map onto the compute and raster paths erhe already
has, and because its conservativeness is by construction: every surface
the cell can see through any opening between depth layers is kept.

## Terms

- Entry: one `Draw_list_entry` of the scene's `Draw_list_scene` (one mesh
  primitive of one mesh node). The PVS granularity is the entry, the unit
  the classification culls and the command generation draws; the paper's
  per-triangle PVS is reduced to entries by construction (an entry is
  visible when any of its triangles is).
- Column: the database's index of one baked entry. Columns are assigned at
  bake time; at runtime every entry maps to its column or to "untracked".
- Identity of a baked entry: the mesh node's glTF uid
  (`Item::get_gltf_uid()`, assigned once and never changed,
  `doc/editor/scene_serialization.md`) and `mesh_primitive_index`; what the
  bake saw of it is the entry's world transform and bounds at bake time
  (the staleness rule below).
- Cell: one axis-aligned box of the bake grid, the viewcell of the paper.
  The PVS of a cell is one bit per column.
- Face: one of the six perspective views the bake rasterizes per cell.
- Sample point: the point in the cell the faces are rendered from.
- Layered buffer: the disocclusion buffer of one face, the per-(pixel,
  depth layer) closest sample.

## Decisions

D1, D2 and D4 are the user's choices (2026-10-30) among the alternatives
named there; the others are the standing defaults of this plan.

- D1 Storage: the database is an asset-root glTF extension `ERHE_pvs` of
  the scene file (JSON parameters plus three `bufferView`s: columns, cell
  table, bits), written by the save pipeline from the scene's in-memory
  database and read by the open pipeline, like `ERHE_raytrace_bvh` and
  `ERHE_gpu_vertex_streams`. The database then travels in the one `.glb`
  that "Save for Quest" produces. (Alternative not taken: a sidecar
  directory like `<scene>.lightmap/`, which the lightmap tiles use because
  they are large and partially baked; a PVS is small and whole.)
- D2 Bake region: a box and a cell size in the scene's settings
  (`Scene_settings`, saved through `ERHE_scene` as the lightmap tile
  overrides are), edited in the PVS window, with "fit to content bounds"
  as the starting value. Cells are a uniform grid over the box. (Adaptive
  cells are a follow-up.)
- D3 Static set: an entry is baked when it is content (`Item_flags::
  content`), visible at bake time (its mesh's `Item_flags::visible`, which
  an ancestor's hide clears; a hidden entry is untracked, always kept), has
  bounds (not skinned), is opaque or alpha tested, and its
  node carries no rigid body of a motion mode other than static and is the
  target of no animation. Translucent entries are never baked: they are
  always visible (the frustum and occlusion tests still apply) and never
  occlude. Alpha-tested entries occlude only where their texels pass the
  alpha test (the standard shader's own test, the bake variant keeps it).
- D4 Staleness: the database is valid while every baked entry is present
  with its baked transform and bounds. Moving, re-registering with other
  bounds, or removing a baked entry marks the database stale, which turns
  PVS culling off for the scene until the next bake and lists the fact in
  the Problems list. Entries added after the bake are untracked and always
  visible. (A moved occluder exposes what it hid; no rule short of a
  rebake restores conservativeness, so the whole database goes.)
- D5 Views: the viewport scene views (four view included) and the headset
  get PVS culling; previews, thumbnails, scene image captures and the id
  pass get none (the same set as the occlusion views).
- D6 Where the bake runs: in the editor, GPU work recorded in R one cell per
  frame slot the way the lightmap offline bake runs
  (`doc/editor/lightmap_baking.md` section 9, `offline_tick`), driven from
  the PVS window and from MCP, so a headless session bakes a scene
  unattended and the windowed editor shows progress.
- D7 Bake hardware: the bake uses 32-bit buffer atomics only (two raster
  passes per face, depth then id, below) and dense per-face buffers, so it
  runs on every backend that runs the GPU classification and needs no
  64-bit atomics, no sparse page allocation and no forward-progress
  guarantee between GPU threads. The bake is an offline tool: its memory
  (about 1 GB of device memory at the default settings) and its time are
  accepted on the desktop and it is never run on the headset.

## Design

### Database (`Pvs_database`, `erhe::scene`)

The plain-data database (header, columns, cell table, bits, state, the
identity-to-column lookup) lives in `erhe::scene`, which `erhe::gltf`
already links, so the glTF writer and parser carry it without a new
dependency; its GPU copy and the consumer live in `erhe::scene_renderer`.

- Header: `version`, the scene's `scene_id`, grid origin (vec3), cell size
  (float), cell counts per axis (uvec3), column count, bake parameters
  (face resolution, tile size, layer count, face field of view, samples
  per face, minimum entry pixels), and the bake's wall-clock duration.
- Columns: per column the identity (node glTF uid, primitive index), the
  baked world transform (12 float32 bits) and world bounds (6 float32
  bits), and `transform_hash`, 64-bit FNV-1a over those 18 values' bits in
  that order (`erhe::hash::hash()`, the convention of `ERHE_raytrace_bvh`'s
  `triangle_hash`; hexfloat is the serialization of any float in JSON).
- Cells and pages: cell index = `x + dims.x * (y + dims.y * z)`. Cells are
  grouped into pages of `page_size` cells per axis (header field, default
  4: 64 cells per page); page dims = `ceil(dims / page_size)` per axis,
  page index = `px + pdims.x * (py + pdims.y * pz)`, local cell index
  inside a page = `lx + page_size * (ly + page_size * lz)`. The page is
  the unit of storage, of GPU residency and of streaming ("Residency and
  streaming" below).
- Page table: per page `word_offset` (uint32; `0xffffffff` = the page is
  not stored, no cell of it has a PVS) and `cells_with_pvs` (uint64, bit =
  the local cell has a baked PVS; a cell without one had no free sample
  point or was not baked).
- Bits: the stored pages back to back; a stored page holds exactly
  `page_size^3 * words_per_cell` uint32 words, `words_per_cell =
  ceil(column_count / 32)`, cell words at `word_offset + local_cell *
  words_per_cell`; bit `column % 32` of word `column / 32` set when the
  column is in the cell's PVS. A cell without a PVS inside a stored page
  holds all-ones words (everything visible; the hot path needs no
  sentinel).
- Bits source: the database always holds the header, the columns and the
  page table. Its bits are in memory after a bake, and after an open they
  are also described by their location in the scene file (`Pvs_bits_source`:
  path, the absolute byte offset of the `bits` bufferView in the `.glb`,
  byte length), so a page can be read by range without the glTF parser.
- State: `absent`, `ready`, `stale`. `Scene_root` owns the database;
  `Draw_list_scene` holds a non-owning reference and the per-entry column
  array.

### Residency and streaming

The GPU holds a resident set of pages, never the whole database by
construction: a device-local buffer of `pvs.resident_pages` page slots
(each `page_size^3 * words_per_cell` words; `Pvs_gpu_database`, which also
keeps the CPU copy the CPU lanes read) and, per frame slot, a page table
(per page: its slot, or none). The classification reads cell -> page ->
slot -> words; a page without a slot reads as everything visible, so a view
whose pages are not resident yet is culled by the frustum and occlusion
tests only, never wrongly. Mechanics: `doc/erhe/draw_list_renderer.md` Q6
"PVS resident set and page table".

- Residency policy (`erhe::scene_renderer::Pvs_residency`). The query-box
  registration of a view (M2, `Composer::prepare()` ->
  `Draw_list_scene::register_pvs_view()`) is the change site: it computes
  the pages of the box (the corner cells clamped to the grid; one page
  unless the box straddles a page boundary) and, when they differ from the
  view's previous ones (or the view was inactive: a view is active while it
  registers every frame), requests the stored pages within
  `pvs.stream_radius_pages` (Chebyshev distance in pages; default 1: the 27
  pages around the view) of them, nearest first. Several views (four view,
  a headset beside a viewport) share the resident set; their requests
  union. Requests are served by a read task on the task executor
  (`Pvs_page_streamer`, the `Lightmap_streamer` pattern: one task at a time,
  `erhe::task::spawn()`) that reads one page's bytes at a time from
  the bits source by range (`Pvs_database::read_page_words()`; a freshly
  baked database serves its pages from memory through the same path) and
  hands them back through a mutex-protected queue; the prepare phase places
  at most one page per frame (one upload per frame slot), the render phase
  of that frame (or of the next frame that classifies the scene) writes it
  into its slot, and the page table of every frame from that one on names
  the slot. Eviction, only when an arriving page needs a slot and none is
  free, frees the slot of the resident page farthest from every active
  view's pages, and only when that page is more than one page (the
  hysteresis) farther away than the arriving page: a view moving back and
  forth across a page boundary evicts nothing, and a view's own page
  replaces pages two or more pages away. The evicted page leaves the page
  table at once; its slot is written again only when every frame whose
  table named it has retired (frames in flight later), so no write races a
  frame in flight. A page that finds no slot when its read arrives is
  dropped, and it and every later request wait for the next view change. A
  page whose read fails is marked failed (a warning in the log), is not
  requested again for that database and those settings, and stays without
  a slot (everything visible). Streaming runs whether or not `pvs_culling`
  is on (only a ready database registers views), so the pages are resident
  when the rule is turned on. A view is inactive once a frame passes
  without its registration (a hidden viewport, a closed one): its pages no
  longer count for eviction, and its next registration is a change. A
  capacity below the request set
  ((2r + 1)^3 pages for a view) keeps the nearest pages; when a view moves
  by one page, its new pages within one page of the pages it left can find
  no evictable slot, so a small capacity serves views that move by two or
  more pages (the streaming gate's camera path).
- Capacity. `pvs.resident_pages` default 64 (Bistro-size: 3791 columns ->
  119 words per cell, 30 KB per page, 1.9 MB resident). A database whose
  stored pages all fit the capacity never evicts: the pages the views
  request (those within the radius of their pages) stay resident, so the
  resident set grows to the pages the views have come near, not the whole
  database. The CPU copy of the bits is released after an
  open once the bits source is on disk (`Pvs_database::release_bits()`; the
  Quest's heap is the constraint, `doc/plans/gltf_fast_load.md`); a bake's
  database keeps its bits in memory (it has no source), and a save onto the
  database's own file reads them back first (`make_bits_resident()`), so
  the database keeps them until the next open.
- Settings `pvs_resident_pages` and `pvs_stream_radius_pages` (editor
  settings v10; Settings window, MCP `set_pvs_streaming`), pushed to the
  scenes' draw lists from their change sites; a change remakes the resident
  set.
- Statistics: resident pages (and the most at once), slot bytes, uploads
  and evictions (total, per frame, the most in one frame), and per view its
  pages, wanted and resident pages and whether its box's pages are
  resident, in `get_pvs_status` `streaming`, with the residency check
  (no view culls through a cell whose page table slot does not hold that
  page).

### Runtime consumption

- Column lookup, change-driven: when a database is set on the
  `Draw_list_scene` (load, bake), it builds a hash map from identity to
  column once, and every live entry gets its column by that lookup (or
  "untracked"); a registration afterwards looks its entry up at
  registration. The column is an entry attribute like the bounds: the
  `Draw_list` keeps it as one more SoA array next to the bounds, `publish()`
  copies it to the frame slot with them, and the GPU bounds upload of a
  changed list carries a parallel uint32 column array.
- Staleness, change-driven (D4): the transform, bounds and refresh hooks
  and the registration of `Draw_list_scene` compare a column-bearing
  entry's new state with the column's baked `transform_hash`, and a column
  whose last entry is unregistered without a registration taking it back in
  the same flush (a re-registration keeps it) is a removed baked entry;
  either calls the `Draw_list_scene` stale callback once per database,
  which the editor wires to `Scene_root::mark_pvs_stale()`
  (`Pvs_database::mark_stale()`). The mark is one message on
  `App_message_bus` (`Pvs_state_changed_message`) for the UI and the
  Problems list. A transform update that restores the baked state keeps
  the database stale: a rebake is the only path back to `ready`.
- Query box per view: a view that gets PVS culling (D5) registers, with its
  planes, its query box (world-space min and max): for a viewport the
  camera position plus the positional margin `pvs.query_margin` (default
  0.05 m); for the headset both eye positions plus the margin, computed in
  M2 with the union view (`Headset_view::prepare_cull_planes()`). The
  view set grows from ten to twelve vec4 per view (`Gpu_cull_classifier::
  vec4s_per_view`); the box of a view without PVS culling is empty (max <
  min).
- Classification: `cull_classify.comp` and the CPU lanes (`cull_lanes.cpp`;
  the CPU lanes are the reference the GPU classification is checked
  against, culling.md phase 5) apply the same rule after the frustum test:
  for an entry inside a view whose query box is non-empty and whose column
  is tracked, take the grid cells the query box overlaps (at most eight, the
  cells of the box's eight corners, clamped to the grid); the entry keeps
  the view's bit when any of those cells lies outside the grid, is in a
  page that is not stored or not resident, or has the column's bit set
  (a cell without a PVS holds all-ones words); otherwise the bit is
  cleared and recorded in
  a `pvs_culled` word region laid out like the occlusion `culled` region.
  The occlusion test bits are computed from the result, so a PVS-culled
  entry is not rasterized by the occlusion pass.
- Setting: `pvs_culling` (`off` | `on`, default `on`; an absent or stale
  database culls nothing), published per frame slot with the classification
  setting as `Occlusion_culling` is, Settings window and MCP
  `set_pvs_culling`. Effective on both classification paths; the GPU path
  is the production path, the CPU path keeps the check meaningful.
- Statistics and check: per view the PVS tested and culled counts of the
  frame in `Composition_pass_stats` (the classification's counts, the CPU
  lanes' decision; the check shows the GPU decides the same bits), MCP
  `get_pvs_status` (state, grid, column count, bytes, the cells of the
  viewport camera, the setting, whether the last publish applied the rule,
  and per viewport its counts) and `get_cull_classification_check`
  extended with the PVS bits: the GPU's `pvs_culled` words are read back
  and compared bit for bit with the CPU lanes' removed bits (0 mismatches
  between the CPU and GPU classification is the gate).

### Bake

Per cell, in the order of the cell index, one cell per frame slot in R
(D6), with the scene's draw lists published for the frame as any other
pass sees them. The entries the bake draws are the static set (D3), one
`Item_filter` over the published lists. The bake's frame-local entry
index (the classifier's entry order, which the record index names) is the
id the layered buffer stores; the CPU maps it to the column when the cell's
result is read back.

1. Sample point. Candidates in order: the cell center, then the 26 points
   at offsets of a quarter cell per axis. A candidate is free when none of
   the six axis-aligned rays from it first hits a back face (the scene's
   raytrace scene, `Ray_trace_scene`; a cell whose static entries have no
   raytrace geometry uses the center). The first free candidate is the
   sample point; a cell with none gets no PVS (cell table `0xffffffff`).
2. Near plane. `z_near` = the largest distance from the sample point to a
   face of the cell box plus 1 cm: every point of the cell is behind every
   face's near plane. Entries that reach into the cube `[-z_near,
   z_near]^3` around the sample point are visible without a test (step 5,
   bounds vote); entries too small to cover a sample need no rule either,
   since the vote reads their bounds, not their samples.
3. Faces. Six perspective views from the sample point along +X, -X, +Y, -Y,
   +Z, -Z with field of view `pvs.face_fov` (default 120 degrees: the
   faces overlap, so a disocclusion cone leaving one face's 90-degree
   core is still inside that face) at `pvs.face_resolution` square
   (default 1024), tile size K = `pvs.tile_size` (default 8), N =
   `pvs.layer_count` depth layers (default 64), far plane = the scene
   content bounds' farthest corner from the sample point. Layer k holds
   linear depths in `[z_near * r^k, z_near * r^(k+1))`, r = `(z_far /
   z_near)^(1/N)`.
4. Layered buffer per face, dense: `depth[pixel][layer]` (uint32 of the
   linear depth's float bits, cleared to `0xffffffff`). One raster pass of
   the static set with a sub-variant of the standard shader like the shadow
   casters' (`VARIANT_DEPTH_ONLY`, `VARIANT_SHADOW_CUBE`; `doc/erhe/
   draw_list_renderer.md` "Environment component inventory"): no
   attachments, no depth test, `early_fragment_tests` off, the material
   alpha test kept. `VARIANT_PVS_DEPTH`: the fragment computes its layer
   from its linear depth (the face's depth range is its camera block's
   `view_depth_near` / `view_depth_far`) and `atomicMin`s the depth. The id
   arrays `id_min[pixel][layer]` / `id_max[pixel][layer]` and the second
   pass `VARIANT_PVS_ID` (a fragment whose depth bits equal the stored
   depth of its layer `atomicMin`s and `atomicMax`s its entry index) run
   only for a bake trace and `pvs_debug_face`: the vote reads bounds. With
   `pvs.samples_per_face` > 1 the passes repeat with a sub-pixel jitter of
   the projection per sample and the PVS unions the results. Every frame in
   flight shares the layered buffer: its header is constant for the bake
   and written once, before any frame uses it; nothing per face is written
   into it from the CPU.
5. Disocclusion, per face (the paper's Algorithm 1 as the DaRUS source
   resolves it, with the closure, the spawning order and the vote of this
   plan; the digest holds the transcription):
   - Tiles (one parallel pass). Per (tile x, tile y) column, per pixel the
     first layer its depth samples are in (its first hit). A tile is closed
     at layer L when all K*K samples are covered at L. When every pixel of
     the column has a first hit, the set of first-hit layers is a
     gap-free run `[L_a, L_b]` and the tile's sheet is continuous (no
     4-adjacent pixel pair of first hits has a gap: two adjacent pixels a,
     b differ by more than 1.25 times the local per-pixel step max(|a -
     a'|, |b - b'|), a' the pixel before a and b' the one after b on the
     same axis, plus 1e-4 of the larger depth;
     `erhe::scene_renderer::pvs_gap_slope_tolerance`,
     `pvs_gap_depth_epsilon`; a pixel without a sample is infinitely
     deep),
     the tile is one surface crossing layer boundaries at any slope (rule
     1): closed at every layer of the run, and the column mask gets bits
     `L_a .. L_b - 1` (the layers in front of the run's back are not
     windows into it). A run with a silhouette inside does not close the
     tile: a ray could pass from the free space in front of one part to
     behind the other without crossing geometry, so the tile keeps the
     coverage rule and its partially covered layers are windows. Otherwise
     the tile is open at every other layer. The last layer is never open.
     The column's run is kept for classify.
   - Layer loop, front to back, `L = 0 .. N - 2`, two dispatches per layer:
     - Classify. An open tile at L is a window only when its chunk is
       disoccluded: bits `0 .. L - 1` all set in (tile mask | column mask
       at (x, y)); layer 0 always. Of a window the four same-layer
       neighbours' closed bits (screen edges count as the tile itself)
       give the cone's growth sides: every closed neighbour is a growth
       (sloped) side (the paper's rule). With the opt-in bake parameter
       `silhouette_tests` (off by default; "Open") a closed neighbour is a
       growth side only across a silhouette (rule 3). No growth side is
       the degenerate case, bit L set in the column mask; else a cone
       record (tile, growth sides) is appended to layer L's list (atomic
       counter).
     - Cones. One workgroup per cone (grid-stride), one thread per layer
       J > L. The window is the tile's rectangle on layer L's front plane;
       the hit rectangle on layer J's front plane is the bounding
       rectangle, over the eight corners c of the cell box (in the face's
       view space) and the four window corners w, of the points where the
       ray from c through w crosses the front planes of layers J and J + 1
       (`t = (z_J - c.z) / (z_L - c.z)`, `x = c.x + t * (w.x - c.x)`,
       likewise y; the two planes bound every depth of layer J), converted
       to tile indices, clamped on each side whose neighbour is open to the
       tile's own column. The cell box is behind every layer plane (step
       2), so every t is positive. The rectangle areas are prefix-summed in
       shared memory, the threads cover the tiles of all rectangles, and
       each covered tile at layer J gets bit L `atomicOr`ed into its tile
       mask. Layer L's cones are scattered before layer L + 1 is
       classified, so only chunks reachable through a window spawn cones.
   - Map. Per chunk (tile, layer) the disoccluded bit (as in classify);
     then a mip chain over the tile grid per layer, each texel the "any"
     of its 2 x 2 children.
   - Bounds vote, one thread per column (static entry). The entry's world
     bounds in the face's view space (the faces look along world axes):
     bounds reaching into the cube `[-z_near, z_near]^3` around the sample
     point are visible; bounds wholly in front of the near plane are not
     (on this face). Else the projected corners of the part beyond the
     near plane give the tile rectangle (clamped to the face's 90-degree
     core: the six cores cover every direction, and the rim holds the
     windows and cones that reach the core's chunks), the depths give the
     layer range widened by one layer on each side, and the map is read at
     the level where the rectangle spans at most 2 x 2 texels. Any
     disoccluded texel in that box sets the column's bit in the cell's
     column words. A visible entry has a visible sample in a disoccluded
     chunk inside its bounds' box, so the vote keeps it; an entry fused
     behind another in one layer of a pixel, or too small to cover a
     sample, is kept by its bounds. The cell ORs the faces' votes.
     Resolution limits: a gap narrower than a pixel, or a step below the
     gap test's tolerance, is not seen; occluders nearer than z_near are
     not rasterized ("Open", near-plane entry). A bake is deterministic per
     backend; Metal and Vulkan differ at the raster's ULP level (a few
     cells of a large bake), so each backend's gate is the contract.
   - The passes read parameter blocks (one per dispatch, from the ring
     buffer) that carry every quotient they need (computed on the CPU), so
     they multiply, add, compare and floor only, with precise
     intermediates; a CPU reference of the chain
     (`erhe::scene_renderer::run_pvs_disocclusion_reference()`) matches them
     bit for bit (`doc/erhe/draw_list_renderer.md` Q6 "PVS disocclusion
     passes").
6. Readback. The cell's column words are read back at the frame's
   retirement (`Render_frame_results` pattern) and stored in the database;
   the cell table gets its offset. The bake records progress (cells done /
   total, seconds per cell) and, when the last cell is done, builds the GPU
   copy and sets the state `ready`. A scene edit during the bake that would
   mark a ready database stale (D4) cancels the bake.
7. Trace. `pvs_bake` with `trace_cell` and `trace_entries` bakes the one
   cell with the id pass on and reports, per face and traced entry, the
   depth-pass fragments, the samples naming it, those in the core and in
   a disoccluded chunk, the bounds vote's tile rectangle and layer range,
   the per-layer closed and disoccluded state of a column, and whether the
   column is in the cell's PVS (`doc/agents/mcp_server_usage.md`). A
   diagnostic path: it allocates.

Bake cost estimate at the defaults, from the paper's figures scaled to the
raster passes erhe runs: per face one raster pass of the static set and
the compute passes over 128 x 128 x 64 tiles (the layer loop's 2 * 63
dispatches); per cell six faces; a
Bistro-size scene at a 1 m cell over a 100 x 10 x 100 m region = 100 000
cells, a few milliseconds per face, tens of minutes per bake. The window
shows the running estimate; the region and cell size (D2) are the levers.

### Editor

- PVS window (`Pvs_window`, `doc/editor/windows.md`): region box and cell
  size (D2), bake parameters, Bake / Cancel / Clear, progress and the
  estimate, the database state and size, the viewport camera's cell and its
  PVS size, and a debug overlay (the grid's cells near the camera and the
  query box, through `Debug_renderer`, recorded in M2 like the other debug
  visualizations).
- MCP: `pvs_bake` (starts a bake; parameters override the scene settings for
  this bake), `get_pvs_bake_status`, `pvs_cancel`, `pvs_clear`,
  `set_pvs_culling`, `get_pvs_status`, and `get_visible_entries` (see
  Verification).
- Settings window: the `pvs_culling` setting next to `occlusion_culling`.
- Stale database (D4): the editor has no Problems list; the PVS window shows
  a warning line.

### Headset

The headset's draw-list passes register their query box with the union
view (D5, "Runtime consumption"); the per-eye (non-multiview) path registers
none, as for occlusion. On the Quest the database's header, columns and
page table are loaded with the scene and the pages around the headset
stream from the `.glb` ("Residency and streaming"); per frame the headset
pays the classification's extra reads and at most one page upload.

## Verification

- Unit tests (`erhe_scene_renderer` tests): the layer function and its
  inverse; the eight-corner hit rectangle against a brute-force ray
  sampling of the box for random cells and windows (the rectangle contains
  every sampled hit); a CPU reference of the three disocclusion passes over
  small synthetic layered buffers (the paper's Figure 2 cases: a window in
  layer 1 and one in layer 2, the tile in both cones visible and the tile
  in one occluded) compared bit for bit with the GPU passes through a GPU
  test fixture (`erhe_scene_renderer_gpu_tests`); the database's round
  trip through `ERHE_pvs` (write, read, identical bytes); the column
  lookup and the staleness rule through a `Draw_list_scene` with a
  synthetic database.
- `get_visible_entries(camera)`: an MCP tool that renders the id pass
  (`Id_renderer`) from a given camera pose at the viewport's resolution,
  reads the whole id image back and returns the set of entry identities
  seen. The exact from-point visibility the PVS must contain.
- Editor-driven runs (the gates below, `pvs_verify.py`,
  `visible_entries_check.py`, bake traces, the Bistro images) use the
  headless editor: on macOS `build_xcode_metal_headless` and
  `build_xcode_metal4_headless` (`doc/erhe/metal_headless.md`); macOS has
  no headless Vulkan tree, so MoltenVK runs the windowed
  `build_xcode_vulkan`.
- `scripts/pvs_verify.py`: its first version bakes the wall scene with the
  sampled method and checks every cell (wall in, the six boxes behind it
  out), that a second bake gives the same `bits_hash` and that a save and
  `load_scene` reload the same database; the full gate below needs the
  disocclusion bake. It bakes the scene the script opens (the wall
  scene of `scripts/occlusion_wall_check.py`, the heavy bench scene,
  Bistro), then for a sample of cells (all, when fewer than 200) draws M
  random viewpoints in the cell and random orientations, calls
  `get_visible_entries` and checks that every seen entry is in the cell's
  PVS. Gate: zero misses on every scene. Report: false-positive ratio (PVS
  size over the union of the samples) per scene, the paper's figure of
  merit; bake time; database bytes.
- Wall scene expectations, as for the occlusion gate: every cell in front
  of the wall keeps the wall and the four boxes in front of it (a
  failure otherwise); a cell inside the 6 m box keeps the box and the box
  beyond its far wall. The cells keeping a box behind the wall are
  counted and printed, not a failure: 12 of 48 cells leak at this commit
  (false positives, future work: "Open").
- Current numbers (defaults: rule 3 off, 1024-pixel 120-degree faces,
  K = 8, N = 64; the sampled oracle with 12 viewpoints in each of 20
  cells, 48 for the wall; misses / false-positive ratio):

  | scene  | Metal 3 windowed | MoltenVK windowed | Metal 4 windowed | Metal 3 headless | Metal 4 headless |
  |--------|------------------|-------------------|------------------|------------------|------------------|
  | wall   | 0 / 1.241        | 0 / 1.241         | 0 / 1.241        | 0 / 1.245        | 0 / 1.245        |
  | heavy  | 0 / 2.208        | 0 / 2.208         | 0 / 2.208        |                  |                  |
  | Bistro | not measured (a raycast timed out after the bake) | 0 / 7.705 | | 0 / 7.715 | |

  The wall ratios differ only through the oracle's samples (the PVS sizes
  are equal). The sampled wall bake's `bits_hash` is c80c51abedaed3c5 on
  every backend; the disocclusion wall bake's is 4fe439f63f8d385d on Metal
  3 headless, Metal 4 headless and MoltenVK; the headless Metal 3 Bistro
  bake's is befefef64ac2a8e1. Images are identical with `pvs_culling` on
  and off, and `get_cull_classification_check` reports 0 mismatches.
  ctest `-L "gpu|editor"`: 101/101 on Metal 3 and on MoltenVK. A bake is
  deterministic per backend; across backends Metal and Vulkan differ at
  the raster's ULP level in a few cells of a large bake (Bistro: 25 of
  572), so each backend's gate is the contract.
- Images: `render_scene_image` byte-identical with `pvs_culling` on and off
  from the Bistro bench camera and along `scripts/render_thread_bench.py`'s
  camera path, on Metal 4, Metal 3 and KosmicKrisp (MoltenVK runs the CPU
  path: identical there too). `get_cull_classification_check` 0 mismatches
  with PVS bits.
- Streaming (`pvs_verify.py` section `streaming`): the baked Bistro saved
  with its PVS to a scratch file and loaded in a fresh editor (the bits
  stream from the file), `pvs_resident_pages` below the page count (4), a
  camera path of poses two pages apart; after the pages around each pose
  arrived: images identical with the rule on and off, 0 classification
  mismatches, resident pages never above the capacity, at most one upload
  per frame, no view culling through a page not resident in its slot, and
  evictions over the path.
- Cost: `scripts/render_thread_bench.py --tracy` on Bistro, PVS off / on /
  on with occlusion: GPU `Main`, tick, R, the occlusion pass's time (fewer
  boxes), classification dispatch time. Quest, when a headset is attached
  again (none is available on either machine as of 2026-10-30): the
  headset Bistro benchmark (`doc/plans/quest_performance.md`) before and
  after, with the database baked on the desktop and saved into
  `bistro_quest.glb`.

## Build order

Each step is one verified commit; the gates are the ones above that the
step makes possible.

1. Database and persistence: `Pvs_database`, `doc/gltf_extensions/
   ERHE_pvs.md` with its schema, save and open, `get_pvs_status`, unit
   tests of the round trip, `get_visible_entries` with
   `scripts/visible_entries_check.py` (done; `doc/erhe/scene.md`
   "Potentially visible sets", `doc/editor/editor.md` "MCP diagnostics
   that read R's output"; the id pass draws double-sided back faces and
   alpha-tests as the color pass does, `doc/editor/renderers.md`).
   No consumer, no bake.
2. Sampled bake (the ground-truth tool; done, `Pvs_baker` in
   `doc/editor/renderers.md`, MCP in `doc/agents/mcp_server_usage.md`):
   `pvs_bake method=sampled` fills a database from whole-image id captures
   (`Scene_id_capture`, re-armed per capture) at M viewpoints per cell, six
   faces each, through the cell loop, progress and finish path the
   disocclusion bake reuses. Not conservative; it is the oracle of
   `pvs_verify.py` and the first data the consumer is gated with. A bake
   assigns a glTF uid to every mesh node it bakes that has none
   (`erhe::gltf::generate_gltf_uid()`, the save pipeline's first-export
   generator, `doc/editor/scene_serialization.md`), since columns are keyed
   by uid. The region and cell size are `Scene_settings` fields (D2) saved
   through `ERHE_scene`; `ERHE_pvs` records the bake method.
3. Consumer (done; `doc/erhe/draw_list_renderer.md` Q6 "PVS",
   `doc/editor/rendering.md`, MCP `set_pvs_culling` and the consumer
   fields of `get_pvs_status` in `doc/agents/mcp_server_usage.md`): column
   lookup, SoA column array and upload, the paged GPU layout with every
   stored page resident (`Pvs_gpu_database`), query boxes, the
   classification rule on both paths, `pvs_culled` words and statistics,
   the setting, the staleness rule, the check. `scripts/pvs_verify.py`
   gates it on the wall scene with the sampled database: 0 mismatches, the
   viewport view culls the six boxes in both classification modes, the
   viewport image and a scene image byte-identical with the rule on and
   off, and a moved front box turns the database stale and the rule off.
4. Disocclusion bake, part 1 (done; `Pvs_baker` and `Pvs_bake_node` in
   `doc/editor/renderers.md`, `draw_pvs_face()` in
   `doc/erhe/draw_list_renderer.md`, the variants in
   `doc/erhe/shader_variants.md`, MCP `pvs_debug_face` in
   `doc/agents/mcp_server_usage.md`): the layered buffer (the two shader
   variants, the dense buffers, the layer function `pvs_layers.hpp` with
   its unit tests) drawn per face from the cell center, the bake's cell
   loop running every face of every cell with each cell ending without a
   PVS, and `pvs_debug_face`, which writes one layer of one face as a depth
   and an id image. The arrays are laid out layer-major (word `layer *
   resolution^2 + y * resolution + x` of each array), so one layer is one
   contiguous range. The faces draw the static set through the draw lists'
   column arrays: the bake sets its column table as the scene's database
   for its duration.
5. Disocclusion bake, part 2 (built; `Pvs_baker` / `Pvs_bake_node` in
   `doc/editor/renderers.md`, the passes in `doc/erhe/draw_list_renderer.md`
   Q6 "PVS disocclusion passes", `Pvs_window` in `doc/editor/windows.md`,
   MCP and the bake trace in `doc/agents/mcp_server_usage.md`): the
   compute passes ("Bake" 5: rule 1 contiguous-run closure with sheet
   continuity, rule 2 gated front-to-back spawning, the paper's cone
   sides, the disoccluded map and the core-clamped bounds vote; rule 3,
   silhouette sides, opt-in and off: "Open"), their CPU reference and the
   GPU tests (bit for bit on Metal 3, Metal 4, MoltenVK and KosmicKrisp),
   one cell per frame, the free sample point, the bake trace (a traced
   cell reproduces the full bake's cell bit for bit), the window and MCP.
   `pvs_verify.py` sections `wall`, `heavy` and `bistro` run the gates
   ("Verification"). A bake is deterministic per backend, not bit-identical
   across backends. Gate state ("Verification"): zero misses on
   the wall and heavy scenes on Metal 3, Metal 4 and MoltenVK and on Bistro
   on Metal 3 (headless) and MoltenVK, images identical with the rule on
   and off; 12 of the 48 wall cells keep boxes behind the wall (future
   work, "Open").
6. Streaming (done; "Residency and streaming", `doc/erhe/draw_list_renderer.md`
   Q6 "PVS resident set and page table", `doc/editor/rendering.md`, MCP
   `set_pvs_streaming` and `get_pvs_status` `streaming` in
   `doc/agents/mcp_server_usage.md`): the residency policy
   (`Pvs_residency`, unit tests for the page selection, the union of views,
   the eviction order, the hysteresis and the delayed reuse of an evicted
   slot), requests at the query-box registration in M2, the worker-thread
   page reads by range (`Pvs_page_streamer`), one upload per frame slot,
   eviction, the per-frame-slot page tables, the CPU bits release after an
   open. Gate (`pvs_verify.py` section `streaming`, headless Metal 3): the
   baked Bistro saved with its PVS (44 pages, all stored, 7616 words each)
   and loaded again (bits released, `bits_hash` befefef64ac2a8e1 read back
   from the file), `pvs_resident_pages` 4, radius 1, a camera path of 12
   poses two pages apart: every pose settled (its box pages resident),
   uploads per pose 4, 1, 3, 2, 2, 2, 1, 1, 2, 2, 2, 2 (24) and evictions
   per pose 4, 1, 3, 2, 2, 2, 1, 1, 2, 2, 2, 2 (24); the run's totals are 28
   uploads and 24 evictions, because 4 uploads (no evictions) happen before
   pose 0, around the `frame_scene` camera's first position, resident pages never above 4,
   at most one upload per frame, residency check 0, classification check 0
   mismatches at every pose (PVS culled 22, 14 and 203 entries at three
   poses), viewport and `render_scene_image` images identical with the rule
   on and off at every pose. The existing sections pass with the default
   capacity (64 slots for Bistro's 44 pages: nothing is evicted; the pages
   within the radius of the poses stream in).
7. Documentation: `doc/erhe/draw_list_renderer.md` (Q6 gains the PVS rule),
   `doc/editor/rendering.md`, `doc/editor/windows.md`, this plan reduced to
   what is outstanding.
8. Headset, when a Quest is available again: the query box of the union
   view ("Headset"), built and compiled in a tree with OpenXR, the Quest
   wall gate (`scripts/quest_occlusion_wall_check.py` extended with the
   PVS) and the Quest measurement of "Verification". Until then the
   headset's views register an empty query box and the desktop steps do
   not wait for it.

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
