# Lightmap spatial tiling + world-space partition - session handoff (2026-08-05)

## UPDATE 2026-08-05 (later session): quadtree grid replaced the adaptive kd split

The content-adaptive kd split described below was REPLACED (user-directed,
no back-compat kept) by a uniform world-origin quadtree grid:

- **Grid**: level-0 cells of `lightmap.cell_size_m` (default 8 m) anchored at
  multiples of the cell size from the world origin, over the XZ AABBs of the
  lightmapped content. Tile boundaries depend only on grid parameters +
  overrides - never on content - so they are stable across edits/sessions.
  Addressing: `Lightmap_tile_key {level, ix, iz}`
  (renderers/lightmap_grid.hpp); level +1 halves the cell (2x nominal texel
  density), -1 doubles it.
- **Per-tile down-only density**: nominal texels/m = `tile_texture_size`
  (default now 1024) / cell side; packing flexes density DOWN per tile when
  content does not fit (reported), never up. `texels_per_meter` config no
  longer drives tile layout (legacy standalone unwrap only).
- **Subdivide / merge** (density control): scene-persisted leaf overrides
  (`Scene_settings::lightmap_tile_overrides`, list of {level, ix, iz} with
  level != 0; survives GLB save/load via the ERHE_scene extension). UI:
  Lightmap window > World-Space Tiles > "Tiles" tree. MCP:
  `lightmap_get_tiles`, `lightmap_subdivide_tile`, `lightmap_merge_tile`.
  With a live partition an override change launches an async re-prepare;
  the legacy path relayouts through the tick's grid-parameters hash.
- **kd tree emission**: each quadtree split = X plane + two Z planes, so
  `clip_by_tile_tree` is untouched. A world-origin quadtree has NO aligned
  cell spanning the origin, so the tree root is a fixed origin cross (x=0,
  z=0) with one aligned subtree per occupied signed quadrant. Empty
  quadrants are tile -1 leaves; the partitioner drops any piece routed
  there (occupancy is AABB-conservative, so none should be).
- **Manifest v3**: per tile {level, ix, iz} + nominal texels_per_meter;
  payloads named `tile_L<level>_<ix>_<iz>.lmt`. Save-on-evict /
  restore-on-activate match tiles by grid key (+ per-tile density), not
  index. v2 sets are stale (no back-compat by design).
- The split "estimate" is now exact (content-independent); the estimated-
  coverage staleness gotcha below is obsolete. Overflow splits are no
  longer emitted (the clipper still supports them).
- Per-piece unwrap uses ITS TILE's nominal density (gutters/min-chart sized
  at the raster density), snapshot into the prepare job.

Verified headless 2026-08-05 (Default Scene, 512 tiles, budget 1): uniform
4x8 m cells at 64 tpm nominal with down-only flex; subdivide -> 4 m/128 tpm
children; merge back; merge below level 0 -> 16 m/32 tpm; overrides survive
close/reload; evict-save wrote tile_L0_-1_-1.lmt etc. and revisit restored
from disk by grid key.

## UPDATE 2026-08-05 (third session): pieces are render proxies; auto re-prepare

- New `Item_flags::render_proxy` (bit 31, on piece meshes/nodes/group) and
  `Item_flags::proxy_hidden` (bit 32, on originals while "Render with
  lightmaps" is ON). Neither flag persists (allowlist in
  gltf_item_flags.cpp).
- Pieces are pure render stand-ins: no show_in_ui (item tree), no
  Item_flags::id (ID render), raytrace mask 0 via raytrace_node_mask (rays
  pass through, bone-proxy pattern), skipped by glTF export
  (process_child_nodes) - this also FIXED the earlier known limitation of
  save_scene exporting the pieces.
- Originals stay `visible` even while lightmap-rendering (proxy_hidden
  replaces the old visible-flag flip): ID render, raytrace picking,
  selection, the gizmo and export all keep operating on the source. They
  are excluded from the visual content passes (app_rendering filters), the
  shadow passes (shadow_renderer) and the bake occluder set
  (collect_instances + the tick's occluder hash) - the pieces do all of
  that in their place. NOTE: a SELECTED proxy-hidden original draws no
  selection fill/outline (excluded from those passes too); selection
  feedback is the gizmo + item tree until an outline-only pass is added.
- Editing a partitioned source (transform move or geometry swap - geometry
  identity is now snapshot per source primitive, count_stale_sources)
  auto-launches an async re-prepare after the edit settles (~60 frames
  stable source-state hash; Lightmap_window::update debounce). The old
  pieces + old lightmap keep rendering until the commit swaps them; the
  resident tiles are saved right before the launch so restore-on-activate
  brings the UNAFFECTED tiles straight back after the commit (region
  tables re-validate; the edited tiles decline and re-bake
  progressively). A moved proxy-hidden source no longer resets
  accumulation by itself (it is not in the occluder hash); the reset
  arrives with the commit's piece swap.

Verified headless: raycast under lightmap rendering hits the ORIGINAL
(floor, not floor.lm); moving `cube` auto-saved the resident tile,
re-prepared (~1 s later), re-clipped the piece to the new position (piece
AABB == moved source) and restored the unaffected resident tile from disk
immediately after the commit.

The sections below describe the ORIGINAL kd-split design; tile-identity and
split-estimate details are superseded by the grid above.

State: **implemented, built green (ninja + headless VS), verified headless via
MCP.** Two feature sets landed on top of each other this day:

1. **Spatial tiles + bake-to-disk + streaming** (earlier session):
   kd-split world tiles, tile IO (`.lmt` + manifest), `Lightmap_streamer`,
   `Lightmap_report` Problems UI. Design: `doc/lightmap_baking_plan.md` section 9.
2. **World-space partition** (this session): every lightmapped mesh/primitive
   instance made unique, node transform baked into world-space vertices,
   geometry clipped against the tile boundary planes (binary-exact shared cut
   vertices), per-piece channel-2 re-unwrap, "Render with lightmaps" toggle
   with a flat-white fallback for non-resident tiles, manifest v2 identity.
   Design: `doc/lightmap_baking_plan.md` section 10.

## What was built (world-space partition)

- `src/erhe/geometry/erhe_geometry/operation/clip_tile_tree.{hpp,cpp}` -
  recursive kd-tree clipper with a memoized cut table; identical ordered
  source lists make `interpolate_mesh_attributes()` bitwise-identical on both
  sides of every plane. Unit tests: `src/erhe/geometry/test/test_clip_tile_tree.cpp`
  (5 tests incl. memcmp bitwise equality, 4-tile corner, overflow routing).
- `Lightmap_baker::update_layout` records the kd split as
  `Atlas_layout::kd_nodes` (explicit plane values; overflow splits axis = -1);
  MCP `lightmap_update_atlas` returns the tree as `kd_nodes`.
- `src/editor/renderers/lightmap_partitioner.{hpp,cpp}` (`App_context::
  lightmap_partitioner`): prepare = geometry-only split estimate
  (`Lightmap_baker::compute_tile_split_estimate`, fused 2026-08-05 - no
  unwrap or prior layout needed) -> bake_transform -> clip -> per-piece
  `make_atlas` (usage 2, world density, per-facet fallback,
  `Lightmap_report::Stage::partition`) -> renderable + raytrace primitives ->
  one piece mesh per source mesh on identity nodes under "Lightmap Pieces".
  Originals + params retained; revert/re-prepare supported; scene-close safe
  (`on_scene_closed` from `Editor::on_close_scene`); stale-transform count
  shown in the window. prepare() is blocking and runs on the main thread.
- Partitioned layout mode in `update_layout` (regions from pieces,
  pre-assigned tiles, density-flex only), `finalize_layout` shared tail.
- White fallback: `Atlas_layout::display_uv_scale_offset` and the streamer
  publish `vec4(-1,0,0,0)` for non-resident PIECE regions; `standard.frag`
  renders scale.x < 0 as flat white ambient (analytic lights stay gated off);
  bounce records clamp the sentinel to zero. Toggle = visibility flip
  originals <-> pieces, persisted as `lightmap.render_with_lightmaps`
  (config v9).
- Manifest v2 (`lightmap_tile_io`): + `node_index_path` (root-to-node child
  indices - duplicate-name fix for ALL regions) + `piece_ordinal`; pieces
  store the SOURCE mesh identity. Streamer resolves pieces via
  `Lightmap_partitioner::find_piece`; evicted pieces get the white sentinel;
  a piece manifest without a live partition warns once. Partition
  prepare/revert invalidates the streamer (`App_context::lightmap_streamer`).
- UI: Lightmap window "World-Space Tiles" section (Prepare / Revert / stats /
  stale warning / Render with lightmaps) is the precondition-free front door
  (fused 2026-08-05); Generate Lightmap UVs + Update Atlas Layout live under
  a collapsed "Legacy Atlas (non-partitioned)" header. MCP:
  `lightmap_prepare_tiles` (self-contained), `lightmap_revert_tiles`,
  `lightmap_set_render`.

## Interactive-bake persistence (added same day, user-requested)

Interactive bake results are never lost to a residency swap:

- **Save-on-evict**: the residency ranking never drops a tile whose
  published content is unsaved (`Tile_state::dirty_since_save`); it parks
  the tile in a pending-save queue keeping its slot (gathering stops), and
  `Lightmap_window::update()` persists it (display-slot region readback ->
  `tile_<id>.lmt` + manifest) before releasing it for eviction. Enabled in
  editor.cpp via `Lightmap_baker::set_save_on_evict(true)`.
- **Save All Tiles** (window button + MCP `lightmap_save_all_tiles`):
  writes every resident published tile's current lightmap to disk now.
  Non-resident tiles have nothing in memory to save.
- **"Bake To Disk" renamed to "Batch Process All Tiles"** (section
  "Tile Persistence"); MCP tool name stays `lightmap_bake_to_disk`.
- Incremental manifests list every layout tile before all payloads exist;
  the streamer stats payloads at manifest load and skips absent ones
  (not-yet-baked, not errors).
- Shared helpers `build_tile_manifest` / `persist_tile_payload`
  (lightmap_window.cpp) serve the batch bake, Save All and evict-save.
- **Restore-on-activate** (2026-08-05, the inverse of save-on-evict): a
  tile that gains a display slot with no accumulated content is queued in
  the baker (`take_tile_pending_restore`); `Lightmap_window::update()`
  validates the saved payload - bake-parameter hash, tile size, bounds and
  the full region packing table must match the CURRENT layout - and uploads
  it into the display slot (`Lightmap_baker::restore_tile`). The tile shows
  its saved bake instantly instead of re-baking from black. The fp32
  accumulation is NOT persisted, so gathering still restarts, but republish
  is held until fresh sweeps reach the payload's saved sweep count (stored
  in the `.lmt` header word that was `reserved0`; old payloads read 0 and
  hold for one sweep), so the display never regresses to an early-sweep
  result. Lighting/occluder invalidations mark restores stale
  (`restore_attempted`) so a reset never resurrects pre-edit content; a
  declined restore (no payload / stale hash / changed packing) re-bakes
  from scratch exactly as before.

Verified headless: Save All wrote the resident tile; a camera jump across
tiles (512^2, budget 1) auto-saved the evicted tile with no save-all call;
no missing-payload errors from the streamer. NOTE: the editor tick pushes
`lightmap_config` tile size/budget into the baker every frame, so MCP
`lightmap_update_atlas` overrides do not survive into interactive baking -
config values rule (that is why the evict test edited the config).

## Verification done (headless MCP, Default Scene + 6 boxes spread +-40 m)

- `erhe_geometry_tests`: 109/109 (5 new ClipTileTree tests).
- kd tree consistency: every tile's bounds center on the correct side of all
  ancestor planes (3 tiles / 19 regions / 5 kd nodes).
- Partition: 19 meshes -> 26 pieces / 3 tiles, 5 meshes clipped across
  planes; prepare/toggle/revert/re-prepare idempotent; originals untouched.
- Partitioned layout: 26 piece regions, budget-1 residency; screenshots show
  toggle OFF = baseline, ON pre-bake = flat white pieces + black resident
  tile (unbaked atlas), analytic lights gated off.
- Bake-to-disk on the partition: 3/3 tiles, manifest v2, all 26 piece
  regions carry source identity + node_index_path; streamer auto-loaded all
  tiles and bound them through the partition (log: `tile N resident in slot
  M`); screenshot shows the streamed baked lightmap on the pieces.

**Not exercised:** eviction under camera movement with pieces (white
sentinel path is unit-verified via budget-1 layout only), a real glb scene
with duplicate node names, undo interactions while a partition is live.

## Known limitations / next work

1. DONE 2026-08-05: prepare is fully async - request_prepare snapshots on
   the main thread and launches the heavy phase (per-region bake + clip,
   per-piece unwrap + primitive build, all parallel) on the app executor;
   Lightmap_partitioner::update() (editor tick, after the operation stack)
   commits when done. The old partition stays live until the commit; a
   mid-flight source-primitive swap or mesh removal aborts the commit
   (Problems error, old partition kept); transform moves are tolerated
   (stale-transforms warning). pending_async_ops is held for the flight so
   every async_busy gate covers it. UI shows a progress bar + Cancel;
   MCP lightmap_prepare_tiles is async by default ({queued:true}; poll
   get_async_status.lightmap_prepare; wait:true for the old blocking
   behavior on small scenes), lightmap_prepare_cancel cancels.
2. Cross-tile cut boundaries are lightmap seams (positions crack-free,
   shading discontinuity only). Future: cross-tile seam blend at bake time.
3. Clipper fan-triangulates every facet, including unclipped ones (spec says
   cut triangles; costs extra facets/charts on polygon meshes).
4. Editing originals (transform/geometry) while partitioned leaves stale
   pieces; the window warns, re-prepare fixes. Undo of scene edits does not
   know about the partition (it is deliberately outside the undo stack).
5. Piece meshes are not pickable-flagged and carry no physics.
6. PRE-EXISTING (found 2026-08-05 while testing async prepare, NOT caused
   by it): closing a scene with a prepared partition trips the
   scene-close leak watchdog - Lightmap_partitioner::on_scene_closed
   drops its store, but Lightmap_baker::m_layout still holds the piece
   meshes (Instance_region::mesh shared_ptrs; the baker has no
   scene-close hook), keeping their primitives/materials alive until the
   next update_layout. Fix idea: clear m_layout/m_tiles in the baker when
   m_layout_scene_root closes.

## Gotchas

- clangd shows false errors until `scripts\configure_ninja_win_clang.bat`
  is re-run (new files were added; done) and the generated
  `lightmap_config.hpp` (v9, `render_with_lightmaps`) exists in the build.
- Piece meshes are NOT `Item_flags::lightmapped` - the partitioned layout
  enumerates them through the partitioner store; the tick hash mixes the
  piece buffer meshes explicitly.
- The baker's `update_layout` is entered ONCE by `prepare()` (after the piece
  commit; the tile tree comes from `compute_tile_split_estimate`, which never
  touches `m_layout`) - the partitioned branch triggers only when
  `is_prepared()` and the scene matches. `revert()` after a fused prepare
  re-runs the legacy layout on originals that typically have no channel-2
  UVs -> empty layout (originals render unlit) until re-prepare. Tile
  boundaries come from an estimated coverage (0.7), so tile sets baked to
  disk before the fusion go stale on the first re-prepare (re-bake).
- Memory files: `lightmap-spatial-tiles-2026-08-05`,
  `lightmap-uv-parameterizer-default` (auto-memory MEMORY.md).
