# Lightmap spatial tiling + world-space partition - session handoff (2026-08-05)

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
  lightmap_partitioner`): prepare = layout from originals -> bake_transform ->
  clip -> per-piece `make_atlas` (usage 2, world density, per-facet fallback,
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
  stale warning / Render with lightmaps). MCP: `lightmap_prepare_tiles`,
  `lightmap_revert_tiles`, `lightmap_set_render`.

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

0. **QUEUED (user-directed 2026-08-05, prompt_queue.txt ITEM -1): fuse
   Generate Lightmap UVs + Update Atlas Layout + Prepare World-Space Tiles
   into one self-contained Prepare.** The whole-mesh unwrap only feeds kd
   split sizing and is discarded (pieces re-unwrap); size the split from
   world areas instead and drop the channel-2 precondition. Plan in a
   fresh context; the queue item carries the full rationale and anchors.
1. prepare() is blocking (main thread); async pipeline + cancel + progress
   bar is future work (per-piece work is parallelizable; per_facet unwrap
   needs no geogram solver serialization).
2. Cross-tile cut boundaries are lightmap seams (positions crack-free,
   shading discontinuity only). Future: cross-tile seam blend at bake time.
3. Clipper fan-triangulates every facet, including unclipped ones (spec says
   cut triangles; costs extra facets/charts on polygon meshes).
4. Editing originals (transform/geometry) while partitioned leaves stale
   pieces; the window warns, re-prepare fixes. Undo of scene edits does not
   know about the partition (it is deliberately outside the undo stack).
5. Piece meshes are not pickable-flagged and carry no physics.

## Gotchas

- clangd shows false errors until `scripts\configure_ninja_win_clang.bat`
  is re-run (new files were added; done) and the generated
  `lightmap_config.hpp` (v9, `render_with_lightmaps`) exists in the build.
- Piece meshes are NOT `Item_flags::lightmapped` - the partitioned layout
  enumerates them through the partitioner store; the tick hash mixes the
  piece buffer meshes explicitly.
- The baker's `update_layout` is re-entered by `prepare()` (originals first,
  then partitioned after commit) - the partitioned branch triggers only when
  `is_prepared()` and the scene matches.
- Memory files: `lightmap-spatial-tiles-2026-08-05`,
  `lightmap-uv-parameterizer-default` (auto-memory MEMORY.md).
