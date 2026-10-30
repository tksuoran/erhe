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
  content`), has bounds (not skinned), is opaque or alpha tested, and its
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
(each `page_size^3 * words_per_cell` words) and a GPU page table (per
page: its slot, or none). The classification reads cell -> page -> slot ->
words; a page without a slot reads as everything visible, so a view whose
pages are not resident yet is culled by the frustum and occlusion tests
only, never wrongly.

- Residency policy. The query-box registration of a view (M2) is the
  change site: it computes the page of the box and, when that page differs
  from the view's previous one, requests the pages within
  `pvs.stream_radius_pages` of it (default 1: the 27 pages around the
  view). Requests are served by a worker thread that reads the page's bytes
  from the bits source by range (the `Lightmap_streamer` pattern:
  worker-thread file read, staging upload, at most one page upload per
  frame slot, written in R into a free slot), and the GPU page table is
  rewritten for that frame slot. Eviction frees the slot of the resident
  page farthest from every view's page beyond a hysteresis of one page,
  and only when a request needs a slot. A freshly baked database serves
  pages from memory through the same path. Several views (four view, a
  headset beside a viewport) share the resident set; their page requests
  union.
- Capacity. `pvs.resident_pages` default 64 (Bistro-size: 3900 columns ->
  122 words per cell, 31 KB per page, 2 MB resident); a database smaller
  than the capacity is resident whole after its first requests and the
  policy then does nothing. The CPU copy of the bits is released after an
  open once the bits source is on disk (the Quest's heap is the
  constraint, `doc/plans/gltf_fast_load.md`); the editor keeps it while a
  bake is in progress or the database is unsaved.
- Statistics: resident pages, slot bytes, uploads and evictions per frame,
  and per view whether its pages are resident, in `get_pvs_status`.

### Runtime consumption

- Column lookup, change-driven: when a database is set on the
  `Draw_list_scene` (load, bake), it builds a hash map from identity to
  column once, and every live entry gets its column by that lookup (or
  "untracked"); a registration afterwards looks its entry up at
  registration. The column is an entry attribute like the bounds: the
  `Draw_list` keeps it as one more SoA array next to the bounds, `publish()`
  copies it to the frame slot with them, and the GPU bounds upload of a
  changed list carries a parallel uint32 column array.
- Staleness, change-driven (D4): the transform and bounds hooks and the
  unregister path of `Draw_list_scene` compare a column-bearing entry's new
  state with the column's baked `transform_hash` and call
  `Pvs_database::mark_stale()` on a mismatch. The mark is one message on
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
- Statistics and check: per view PVS-culled count in
  `Composition_pass_stats` (from the `pvs_culled` words, retired-frame
  readback as the occlusion counts), MCP `get_pvs_status` (state, grid,
  column count, bytes, the cells of the viewport camera) and
  `get_cull_classification_check` extended with the PVS bits (0 mismatches
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
2. Near region. Every static entry whose bounds intersect the sphere around
   the sample point of radius `near_radius` = `z_near * sec(fov / 2) *
   sqrt(2)` is in the cell's PVS without a test (its triangles may be
   clipped by a face's near plane). `z_near` = the largest distance from the
   sample point to a face of the cell box plus 1 cm: every point of the
   cell is behind every face's near plane. Every static entry whose bounding
   sphere subtends less than `pvs.min_entry_pixels` (default 2) at the face
   resolution from the sample point is in the PVS without a test: such an
   entry can fall between samples, and it is cheap to draw.
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
   linear depth's float bits, cleared to `0xffffffff`), `id_min[pixel]
   [layer]` (cleared to `0xffffffff`) and `id_max[pixel][layer]` (cleared
   to 0). Two raster passes of the static set with one shader variant each,
   sub-variants of the standard shader like the shadow casters'
   (`VARIANT_DEPTH_ONLY`, `VARIANT_SHADOW_CUBE`; `doc/erhe/
   draw_list_renderer.md` "Environment component inventory"): no
   attachments, no depth test, `early_fragment_tests` off, the material
   alpha test kept. `VARIANT_PVS_DEPTH`: the fragment computes its layer
   from its linear depth and `atomicMin`s the depth. `VARIANT_PVS_ID`: a
   fragment whose depth bits equal the stored depth of its layer
   `atomicMin`s and `atomicMax`s the entry index into the two id arrays, so
   a two-way tie at one sample (two entries at exactly the same depth)
   keeps both. With `pvs.samples_per_face` > 1 the two passes repeat with
   a sub-pixel jitter of the projection per sample and the PVS unions the
   results.
5. Disocclusion, three compute passes per face (the paper's Algorithm 1 as
   the DaRUS source resolves it; the digest holds the transcription):
   - Tiles. Per (tile x, tile y, layer): the covered sample count (depth !=
     cleared); closed = count == K*K, open otherwise, including empty
     tiles. The last layer is never open. A partially covered tile whose
     count plus the count of the same (x, y) tile one layer behind equals
     K*K is a surface crossing a layer boundary and is closed. Per open tile
     the four same-layer neighbours' closed bits (screen edges count as
     the tile itself) give the cone's growth sides: no closed neighbour is
     the degenerate case, bit `layer` set in the flat per-(x, y) column
     mask; else a cone record (tile, layer, growth sides) is appended to
     the cone array (atomic counter; capacity = tile count).
   - Cones. One workgroup per cone, one thread per layer J > L (L the
     cone's layer). The window is the tile's rectangle on layer L's front
     plane; the hit rectangle on layer J's front plane is the bounding
     rectangle, over the eight corners c of the cell box (in the face's
     view space) and the four window corners w, of the points where the
     ray from c through w crosses the plane (`t = (z_J - c.z) / (z_L -
     c.z)`, `x = c.x + t * (w.x - c.x)`, likewise y), clamped on each side
     whose neighbour is open to the tile's own column, then converted to
     tile indices. This is the paper's cone for a box viewcell: the cell
     box is behind every layer plane (step 2), so every t is positive. The
     rectangle areas are prefix-summed in shared memory, the workgroup's
     threads cover the tiles in all rectangles (binary search on the
     scan), and each covered tile at layer J gets bit L `atomicOr`ed into
     its tile mask (`uvec2` per tile, dense).
   - Vote. Per (tile, layer L): disoccluded when bits 0..L-1 are all set in
     (tile mask | column mask at (x, y)); layer 0 is disoccluded. For a
     disoccluded tile every sample's `id_min` and `id_max` that is not the
     cleared value sets its entry's bit in the cell's PVS words (atomicOr).
6. Readback. The cell's PVS words (plus the step-2 entries) are read back
   at the frame's retirement (`Render_frame_results` pattern), mapped from
   frame entry index to column, and stored in the database; the cell table
   gets its offset. The bake records progress (cells done / total, seconds
   per cell) and, when the last cell is done, builds the GPU copy and sets
   the state `ready`. A scene edit during the bake that would mark a ready
   database stale (D4) cancels the bake.

Bake cost estimate at the defaults, from the paper's figures scaled to the
raster passes erhe runs: per face two raster passes of the static set and
three compute passes over 128 x 128 x 64 tiles; per cell six faces; a
Bistro-size scene at a 1 m cell over a 100 x 10 x 100 m region = 100 000
cells, a few milliseconds per face, tens of minutes per bake. The window
shows the running estimate; the region and cell size (D2) are the levers.

### Editor

- PVS window: region box and cell size (D2), bake parameters, Bake / Cancel
  / Clear, progress and the estimate, the database state and size, the
  viewport camera's cell and its PVS size, and a debug overlay (the grid's
  cells near the camera and the query box, through `Debug_renderer`,
  recorded in M2 like the other debug visualizations).
- MCP: `pvs_bake` (starts a bake; parameters override the scene settings for
  this bake), `get_pvs_bake_status`, `pvs_cancel`, `pvs_clear`,
  `set_pvs_culling`, `get_pvs_status`, and `get_visible_entries` (see
  Verification).
- Settings window: the `pvs_culling` setting next to `occlusion_culling`.
- Problems list: a stale database (D4).

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
- `scripts/pvs_verify.py`: bakes the scene the script opens (the wall
  scene of `scripts/occlusion_wall_check.py`, the heavy bench scene,
  Bistro), then for a sample of cells (all, when fewer than 200) draws M
  random viewpoints in the cell and random orientations, calls
  `get_visible_entries` and checks that every seen entry is in the cell's
  PVS. Gate: zero misses on every scene. Report: false-positive ratio (PVS
  size over the union of the samples) per scene, the paper's figure of
  merit; bake time; database bytes.
- Wall scene expectations, as for the occlusion gate: every cell in front
  of the wall excludes the six boxes behind it and keeps the four in
  front; a cell inside the 6 m box keeps the box and the box beyond its far
  wall.
- Images: `render_scene_image` byte-identical with `pvs_culling` on and off
  from the Bistro bench camera and along `scripts/render_thread_bench.py`'s
  camera path, on Metal 4, Metal 3 and KosmicKrisp (MoltenVK runs the CPU
  path: identical there too). `get_cull_classification_check` 0 mismatches
  with PVS bits.
- Cost: `scripts/render_thread_bench.py --tracy` on Bistro, PVS off / on /
  on with occlusion: GPU `Main`, tick, R, the occlusion pass's time (fewer
  boxes), classification dispatch time. Quest: the headset Bistro
  benchmark (`doc/plans/quest_performance.md`) before and after, with the
  database baked on the desktop and saved into `bistro_quest.glb`.

## Build order

Each step is one verified commit; the gates are the ones above that the
step makes possible.

1. Database and persistence: `Pvs_database`, `doc/gltf_extensions/
   ERHE_pvs.md` with its schema, save and open, `get_pvs_status`, unit
   tests of the round trip, `get_visible_entries` with
   `scripts/visible_entries_check.py` (done; `doc/erhe/scene.md`
   "Potentially visible sets", `doc/editor/editor.md` "MCP diagnostics
   that read R's output"). The id pass the oracle draws culls back faces
   of double-sided materials and that is fixed in `Id_renderer` (viewport
   picking gets the same fix) before step 2 relies on the oracle.
   No consumer, no bake.
2. Sampled bake (the ground-truth tool): `pvs_bake method=sampled` fills a
   database from `get_visible_entries` at M viewpoints per cell, through the
   same cell loop, progress and readback path the disocclusion bake uses.
   Not conservative; it is the oracle of `pvs_verify.py` and the first data
   the consumer is gated with. A bake assigns a glTF uid to every mesh
   node it bakes that has none (the save pipeline's first-export rule,
   `doc/editor/scene_serialization.md`), since columns are keyed by uid.
3. Consumer: column lookup, SoA column array and upload, the paged GPU
   layout with every stored page resident, query boxes, the
   classification rule on both paths, `pvs_culled` words and statistics,
   the setting, the staleness rule, the check. Gates: 0 mismatches; the
   wall scene with a sampled database (dense sampling) culls the six boxes;
   images identical at the sampled poses.
4. Disocclusion bake, part 1: the layered buffer (the two shader variants,
   the dense buffers, the layer function) and a readback that dumps one
   face's layer 0 as an image for inspection; unit tests of the layer
   function.
5. Disocclusion bake, part 2: the three compute passes, their CPU reference
   and the GPU tests, the cell loop, the near-region and small-entry rules,
   the window and MCP. Gates: `pvs_verify.py` zero misses on the wall,
   heavy and Bistro scenes; the wall expectations; Bistro images identical
   on / off; cost table.
6. Headset: the query box of the union view; Quest measurement.
7. Streaming: the residency policy of "Residency and streaming" (requests
   at the query-box change site, the worker-thread page reads by range,
   one upload per frame slot, eviction, the CPU bits release after an
   open), with `pvs.resident_pages` smaller than the database's page count
   as the gate: images identical on / off along the Bistro camera path
   after the pages around each pose arrive, resident pages never above the
   capacity, no frame culls a page that is not resident. Until this step
   the resident set is every stored page (capacity = page count), which
   the paged GPU layout of step 3 already supports.
8. Documentation: `doc/erhe/draw_list_renderer.md` (Q6 gains the PVS rule),
   `doc/editor/rendering.md`, `doc/editor/windows.md`, this plan reduced to
   what is outstanding.

## Open

- Adaptive cells (an octree over the region, subdividing cells with a
  non-free center or a PVS much larger than their neighbours').
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
