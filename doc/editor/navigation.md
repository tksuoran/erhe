# Navigation mesh

Stability: experimental

The editor builds a navigation mesh of a scene's walkable surfaces with
Recast Navigation and marks the part of it reachable from seed points. The
code lives in `src/editor/navigation/` and calls Recast and Detour directly,
with no `erhe::` library around them.

## Build options

| Option | Default | Effect |
|---|---|---|
| `ERHE_USE_DETOUR` | `ON`, forced `OFF` on Android | The editor links `RecastNavigation::Detour` and compiles the navigation mesh, its byte form, the reachability flood, the scene state (`Scene_navigation`), its visualization and its item (`assets/navigation_item`), with the Properties status row, the Clear button, the Debug Visualization popup's "Navigation Mesh" combo, the navigation composition passes and MCP `navigation_clear` / `get_navigation_mesh` / `navigation_set_visualization` / `get_navigation_summary` / `navigation_find_point` / `navigation_find_path`; definition `ERHE_USE_DETOUR` |
| `ERHE_USE_RECAST` | `ON`, forced `OFF` on Android | The editor links `RecastNavigation::Recast` and compiles the build and its job, Mesh > Build Navigation Mesh, the Build button, MCP `navigation_build` and the build of the Export's navigation stage; definition `ERHE_USE_RECAST`. Needs `ERHE_USE_DETOUR` (a configure error states it) |

The package is fetched when either option is on. The settings ("Settings")
and the seeds ("Seeds") exist in every build, so a scene file keeps them on
Android too. Common code reaches `Scene_navigation` through
`Scene_root::get_navigation()`, which exists with `ERHE_USE_DETOUR`; the
Scene_host hooks of `Scene_root` call it through one helper that compiles to
nothing without it.

## Types

All in namespace `editor`. They depend on Recast, Detour, glm, the executor
and the editor log only, so `editor_navigation_tests`
(`src/editor/navigation/test/`) builds them without the rest of the editor.

- `Navigation_build_settings` (`navigation_build_settings.hpp`): the
  settings of the table below in meters and degrees, plus the build bounds.
  `make_navigation_build_settings()` makes one from a scene's
  `Navigation_settings`; `make_recast_config()` converts it to Recast's cell
  units: heights round up, the climb rounds down, the radius rounds up, the
  region sizes become cell areas.
- `Navigation_input`: per input mesh, positions and triangle indices in one
  common space, Y up.
- `build_navigation_mesh()` (`navigation_build.hpp`, `ERHE_USE_RECAST`):
  a tiled build. The tile grid covers `region_min` / `region_max`, or the
  union of the input bounds when both are zero. Each tile is one executor
  task running Recast's pipeline (heightfield, walkable triangles and
  rasterization, the span filters, compact heightfield, erosion by the agent
  radius, the region builder `partitioning` names, contours, polygon mesh,
  detail mesh, Detour tile data). The tiles are added to the Detour
  navigation mesh in tile grid order on the calling thread, so the result is
  a function of the input and the settings alone. Recast's log goes to the
  `editor.navigation` log category.
- `Navigation_mesh` (`navigation_mesh.hpp`): owns the Detour navigation mesh
  and, filled from it in tile grid order, the vertices, polygons (vertex
  indices, neighbours across tiles, detail triangles, area in square meters,
  island, `reachable` flag), the bounds, the totals, the islands and one
  Detour query ("Islands and queries"). `fill_from_detour()` fills them; the
  build and the read of the byte form call it.
  `mark_reachable()` finds per seed the nearest polygon within the seed
  search extents and floods from the found polygons across the Detour
  links; a seed without a polygon is reported with its reason in its
  `Navigation_seed_result`, and the call fails when every seed failed.
  `find_reachable()` is the same flood into a flag array without writing
  the polygons, with a Detour query of its own, since the job floods the
  installed mesh on a worker; `set_reachable()` writes such an array.
- `write_navigation_mesh()` / `read_navigation_mesh()`
  (`navigation_mesh_io.hpp`): the versioned byte form
  (`c_navigation_mesh_format_version`, 2) - the settings the build ran with,
  the Detour mesh parameters, each tile's Detour data, the polygon arrays and
  the build record (`Navigation_build_record`, "Saving"). Floats are written
  bit for bit; a read refuses any other version, a polygon array that does not
  match the tiles and trailing bytes, and returns the reason for its caller to
  log.

## Islands and queries

An island is a set of polygons joined by neighbour links: a walkable area
reaches every point of its island and no point of another. `fill_from_detour()`
joins the polygons across their neighbour links (both directions, so a link
only one side holds still joins) and numbers the islands in the order of their
first polygons; each polygon holds its island's id. A `Navigation_island` holds
its polygon count, area, bounds over its detail triangles (their y is its
height range), the centroid of its largest polygon as its representative
point, and `reachable`: whether one of its polygons is reachable.
`set_reachable()`, `mark_reachable()` and `clear_reachable()` keep that flag and
`get_reachable_island_count()` current, and the totals carry `island_count`
and `reachable_island_count`.

`fill_from_detour()` also creates the mesh's Detour query (4096 search nodes)
with the path buffers it uses, and keeps each polygon's `dtPolyRef`:
`get_polygon_ref()` maps a polygon index to it and `find_polygon_index()` back.
The queries run on the main thread, in the scene root's space:

- `find_nearest_point(position, half_extents)` returns a
  `Navigation_point_result`: `found`, the nearest `point` on the nearest
  polygon within the half extents, its `polygon`, `island` and `reachable`.
- `find_path(start, end, half_extents, out_result)` fills a caller-owned
  `Navigation_path_result`: both ends' point results; when they lie on one
  island, Detour's `findPath` (at most 2048 polygons) and
  `findStraightPath`: `found`, `complete` (the path reaches the end's polygon;
  a partial path ends at the point of its last polygon nearest to the end),
  `length` along the corners and the `corners`, start and end included;
  otherwise `failure` (`no_start_polygon`, `no_end_polygon`,
  `different_islands`, `search_failed`). The corners vector is cleared and
  refilled, so a reused result allocates nothing once it has grown.

## Settings

`Navigation_settings` (codegen, `src/editor/config/definitions/navigation_settings.py`)
is the field `navigation` of the scene's export settings
(`Scene_export_settings` version 6; `doc/editor/prepared_assets.md`
"Export settings"). The editor's default export settings carry the defaults
below, so a scene gets them with "Add Export Settings", and export settings
of an earlier version read them.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `mode` | `Navigation_mode` | `off` | What an Export does ("Export"): `off` nothing; `build` makes the navigation mesh current at the start of the Export. Build Navigation Mesh does not read it |
| `cell_size` | Float | 0.15 | Horizontal voxel edge, meters |
| `cell_height` | Float | 0.10 | Vertical voxel edge, meters |
| `agent_height` | Float | 1.8 | Free height a walkable span needs, meters |
| `agent_radius` | Float | 0.3 | The walkable area is eroded by this distance from every obstacle, meters |
| `agent_max_climb` | Float | 0.4 | Highest step that connects two spans, meters |
| `agent_max_slope` | Float | 45 | Steepest walkable triangle, degrees from horizontal |
| `region_min_size` | Float | 1.2 | Regions smaller than this edge squared are dropped, meters |
| `region_merge_size` | Float | 3.0 | Regions smaller than this edge squared merge into a neighbour, meters |
| `partitioning` | `Navigation_partitioning` | `watershed` | `watershed`, `monotone` or `layers` (Recast's three region builders) |
| `edge_max_length` | Float | 12 | Longest polygon border edge, meters |
| `edge_max_error` | Float | 1.3 | Contour simplification error, in cells |
| `verts_per_poly` | Int | 6 | 3 to 6 |
| `detail_sample_distance` | Float | 0.9 | Detail mesh sample spacing, meters |
| `detail_sample_max_error` | Float | 0.1 | Detail mesh height error, meters |
| `tile_size` | Float | 19.2 | Tile edge, meters (128 cells at the default cell size) |
| `region_min`, `region_max` | Vec3 | 0 | Build bounds in the input's space; both zero: the union of the input bounds |
| `seed_search_radius` | Float | 1.0 | Horizontal half extent of a seed's polygon search, meters |
| `seed_search_height` | Float | 3.0 | Vertical half extent of that search, meters |

The settings are edited at two change sites, both of which call
`Scene_root::on_export_settings_changed()`:

- the scene's Properties, "Scene Overrides" > "Export" > "Navigation": one
  row per field; a drag commits at its release, a combo at its selection;
- MCP `set_scene_settings` with `settings.export_settings.navigation`,
  which merges over the scene's navigation settings as every
  `export_settings` field does; `get_scene_settings` returns them.

The world-space bake reads none of them: `make_world_space_settings_signature()`
and the World-space mesh key leave them out (`doc/editor/world_space_bake.md`
"Validity"), so a navigation edit leaves the world-space form current. The
Scene item writes the scene's export settings with `navigation` at its
defaults, so a navigation edit leaves the Scene key as it is.

## Seeds

A seed is a node that carries `Editor_item_flags::navigation_seed`, a
persistent editor flag named `navigation_seed`
(`doc/gltf_extensions/flags.md`), so the scene file saves it with the node
(`ERHE_node` `flags`). The seed point is the node's origin in the scene
root's space (`get_navigation_seed_point()`: `inverse(root.world_from_node)
* world_from_node`, the space of the world-space bake). Any node can be a
seed - an empty, a camera, a mesh - except the scene root. The seed code
(`navigation_seeds.hpp`) is plain editor code and compiles in every build.

`Scene_root` keeps the scene's seed nodes in a list:
`get_navigation_seed_nodes()` fills a caller-owned vector,
`get_navigation_seed_points()` fills one with their points. The list is
updated at node registration and unregistration (a load, an insert, a
delete and their undo) and by `Scene_root::on_navigation_seed_changed()`,
which every writer of the flag calls after the write:
`Item_set_flag_bits_operation` (execute, undo and redo), MCP
`set_item_flags` and the developer Flags rows. No query walks the node
tree for seeds.

Change sites, each one undoable `Item_set_flag_bits_operation`
(`make_navigation_seed_operation()`; nothing is recorded when every node
already has the requested state):

- the node's Properties: the "Navigation Seed" checkbox;
- the scene's Properties, "Scene Overrides" > "Export" > "Navigation" (so
  with export settings): "Seeds" shows the count, "Mark Selected As Seeds"
  and "Unmark Selected" act on the selected nodes of the scene, "Select
  Seeds" replaces the selection with the seed nodes, and "Add Seed At
  Camera" adds an empty node "Navigation Seed" under the scene root at the
  position of the last hovered viewport's camera - one compound operation of
  the insert and the flag, enabled while that viewport shows the scene;
- MCP `navigation_set_seed {scene_name, node | node_id, seed}`.

The Debug Visualization popup's "Navigation Seeds" toggle
(`Debug_visualizations_settings::navigation_seeds`, per view, default on)
draws each seed node of the view's scene as an orange post 1 m high on its
origin with a diamond of radius 0.25 m around its foot.

## Input

The input of a build is the scene's source meshes in the scene root's space,
level 0 of every LOD chain: every mesh of the content layer in the PVS static
set (`doc/editor/pvs.md` D3: content, visible, not skinned, valid world
bounds, no rigid body of a motion mode other than none or static and no
animation channel on the node or an ancestor; `is_moved_by_physics()` and
`is_animated()` of `scene/static_mesh_set.hpp`, which the PVS bake uses too),
translucent meshes included, whether or not the world-space bake admits it.
Render proxies (the world-space form, bone proxies) and session-only nodes
are not input.

The job's main-thread start lists the meshes with their root-relative
transforms (`inverse(root.world_from_node) * world_from_node`, the
world-space bake's space) and the render shapes of their primitives, and
holds each distinct shape. On the worker each shape is read once, however
many nodes share it: its triangle soup (`Primitive_shape::get_triangle_soup()`,
resident or read back from its file) or, for a geometry-normative shape, the
triangles `erhe::primitive::derive_triangle_soup()` derives from its Geometry
(one derive per Geometry at a time) - the two sources an Export reads for the
mesh keys. The positions are transformed per node; a transform with a
negative determinant flips each triangle's winding, so a mirrored floor stays
walkable. A mesh that yields no triangles (a soup that cannot be read, a shape
without facets) is left out, counted in `skipped_mesh_count` and named in the
log and in the status.

## Job

Mesh > Build Navigation Mesh, the "Build" button of the scene's Properties
"Navigation" group and MCP `navigation_build` queue one job of
`Scene_save_service` (`request_navigation_build()`,
`Scene_export_mode::build_navigation`; `scene/navigation_build_job`, a
`Scene_service_job`). It needs no source file and no editor asset, and runs
whatever the `mode` setting says. It is refused
(`get_navigation_build_refusal()`) for a scene without export settings
(`get_export_settings_refusal()`, action `Build Navigation Mesh`) and for a
scene without a seed node: "Build Navigation Mesh of '<scene>' refused: the
scene has no navigation seed - mark a node (Properties > Navigation Seed)".

It runs with a save's staging (`doc/editor/scene_serialization.md` "Saving
in the background"): it waits for quiescence, the scene is read-only while it
runs, the status line shows "Building navigation mesh of '<scene>': <stage>"
and `get_save_status` reports the progress (stages `reading triangles`,
`building tiles` with the tiles done of the tiles total, `flooding`).

1. Main thread: the build settings (`make_navigation_build_settings()`), the
   seed nodes and points, and the input list ("Input").
2. Worker: the triangles, `build_navigation_mesh()` on the editor's executor
   and the flood (`find_reachable()`). When the input list and the build
   settings equal the installed mesh's (only the seeds or the seed search
   extents differ), the worker floods the installed mesh again without a
   rebuild.
3. Main thread: `Scene_navigation::install()` (a build) or
   `install_flood()` (a flood alone), then the item ("Saving").

A build without a polygon, or one in which every seed failed, fails with the
reason (each seed's: "seed '<node>' (x, y, z): no polygon within <r> m
horizontally and <h> m vertically") and leaves the installed mesh as it was.
The job changes neither the node tree nor the undo stack.

## Export

An Export (`doc/editor/prepared_assets.md` "Export") of a scene whose
`mode` is `build` makes the navigation mesh current before it collects the
export arguments, so the steps after it see the scene as the navigation
stage left it. The Export's own refusals (`get_scene_export_refusal()`) come
first: a refused Export builds nothing. Then, in `Scene_save_service`:

- A scene without a seed node gets no navigation mesh: the `editor.export`
  log warns "Export of '<scene>': the scene has no navigation seed - mark a
  node (Properties > Navigation Seed); the Export goes on without a
  navigation mesh" and the Export goes on.
- A current navigation mesh (`Scene_navigation::is_current()`: installed, not
  stale, flooded with the scene's seed search extents) is used as it is.
- Otherwise the job of "Job" runs as the Export's first stage
  (`Navigation_build_purpose::export_stage`): a build, or the flood alone
  when only the seeds or the seed search extents differ, installed as Build
  Navigation Mesh installs it. The status line shows "Exporting '<scene>':
  navigation mesh: <stage>" and `get_save_status` the job's stages; the scene
  stays read-only from the stage's start to the Export's end, and the
  `Scene_export` starts in the same frame the stage ends. A build that fails
  fails the Export with its reason ("Export of '<scene>' failed: the
  navigation mesh build failed: ..."); nothing else of the Export is made and
  the installed mesh stays as it was.
- A build without `ERHE_USE_RECAST` has no build: the log warns that this
  editor cannot build navigation meshes and the Export goes on (a current
  mesh is still used with `ERHE_USE_DETOUR`).

The Export reports the stage as `counts.navigation_mesh` (`made` 1 for a
build or flood, `present` 1 for a current mesh, `skipped` 1 for a scene
without a seed, `written` 0: the prepared asset holds no navigation item),
and for `made` and `present` `navigation_ms` (from the Export's start to the
install, or the currency check), the installed mesh's
`navigation_polygon_count`, `navigation_reachable_polygon_count`,
`navigation_total_area` and `navigation_reachable_area`, and
`navigation_item {key, made}` (the editor asset's item; `made` when the stage
stored a new one); the summary line
"Export of '<scene>' (<target>) done: ..." ends with the same. With `mode`
`off` the report has none of these. The Scene item, its key and the prepared
asset are the same with `mode` `off` and `build`; the status
(`get_prepared_status`) runs no navigation stage.

## Scene state

`Scene_root::get_navigation()` returns the scene's `Scene_navigation`
(`scene/scene_navigation`): the installed `Navigation_mesh` (or none), the
seed results, the build settings, the seeds (node, name, uid, point) and the
input list it was built with, the build statistics (`tile_count`,
`input_mesh_count`, `input_shape_count`, `input_triangle_count`,
`skipped_mesh_count` and names, `build_ms`, `flood_ms`), `build_count` and
`flood_count` (installs since the scene opened), the staleness and the status
text. `install()` replaces all of it within one main-thread call;
`install_flood()` replaces the reachable flags, the seed results, the seeds
and the seed search extents of the installed mesh; `install_loaded()` installs
the mesh of the saved item at the open ("Saving"); `clear()` removes the mesh
and its visualization ("Visualization"), and the scene's close calls it.
"Clear" (the button, queued to the next frame's prepare phase, and MCP
`navigation_clear`) is `clear_navigation()`: `clear()` and the removal of the
item. A scene opens with the mesh its item holds; `build_count` and
`flood_count` count from the open.

The Properties "Navigation" group shows the status text in its "Navigation
Mesh" row: polygons and reachable polygons, islands and reachable islands,
reachable and total area, tiles, time, skipped meshes and, when stale, the
reasons. The text is made at each install and staleness change, not per frame.

## Staleness

Change-driven, compared with what the build read; nothing is compared per
frame. The record names meshes and seed nodes by their item id (never reused
in a session) and holds the render shapes it read, so a freed address taken by
a new mesh or shape never compares as the built one.

- `on_mesh_transform_changed`, `on_mesh_primitives_changed`, `register_mesh`
  / `unregister_mesh` of `Scene_root` (the hooks `World_space_validity`
  follows, `doc/editor/world_space_bake.md` "Validity"): an input mesh whose
  root-relative transform (bit for bit) or render shapes differ from the
  built ones, an input mesh that left the scene, or a candidate mesh (content,
  visible, not a proxy, not session-only, not skinned) that entered it:
  "geometry changed". The undo of the move, edit, removal or addition ends it.
- `on_mesh_flags_changed` (old and new flag bits): the input admission reads
  `content`, `visible` (the effective visibility, which an ancestor's hide
  mirrors into the mesh's flag word, so the hook fires for it too),
  `render_proxy`, `session_only` and `world_space_form`
  (`is_navigation_input_flag_admitted()`). An input mesh that stops being
  admitted (hidden) leaves the input, and a mesh the build did not read that
  becomes admitted (shown) enters it: "geometry changed", ended when the bits
  are back (the undo, or hiding / showing again). Changes of other bits
  (hover, selection) are ignored at the hook.
- `Scene_root::on_export_settings_changed()`: a build setting that differs
  from the built ones, or removed export settings: "settings changed"; setting
  the value back ends it. `seed_search_radius`, `seed_search_height` and
  `mode` are not build settings: a search extent edit is applied by the next
  build request, which floods again ("Job").
- `Scene_root::on_navigation_seed_changed()` and the unregistration of a seed
  node, and a transform observer (`Xformable::add_transform_observer()`) on
  each built seed node: when the scene's seed nodes or their points differ
  from the built ones (any order, points bit for bit): "seeds changed".

A stale navigation mesh stays installed; the status names every reason that
holds.

## Saving

A scene with a source file keeps its navigation mesh in the editor asset of
that file (`doc/editor/prepared_assets.md` "Locations") as one item of kind
`navigation_mesh`, named by the record `navigation.key`; neither the glTF file
nor a prepared asset carries it, and the Scene item does not name it.

- The item's payload is the byte form of `write_navigation_mesh()` ("Types"),
  whose build record holds what the open compares: the seed search extents of
  the last flood; per seed its node's glTF uid, name, point (in the scene
  root's space) and result; per input mesh its glTF uid, its root-relative
  transform and the content hash of each primitive's render shape
  (`doc/erhe/primitive.md` "Content hash"; the build sets a missing one from
  the soup it read or the triangles it derived, which is the hash a reopen of
  the saved file computes). Its meta is the build statistics (tile and input
  counts, skipped meshes, `build_ms`, `flood_ms`), which the key leaves out.
- The key (`make_navigation_mesh_key`) is the content hash of the kind, the
  code version `c_navigation_mesh_code_version`, the stamp
  `erhe_navigation=<byte form version>` and the payload, so an unchanged
  build or flood finds its item again.
- `store_navigation_item()` (`assets/navigation_item`, main thread) puts the
  item when the editor asset lacks it, removes every other `navigation_mesh`
  item of the editor asset and writes the record, in one write transaction,
  as `store_pvs_item()` does for the PVS item. It runs at every install - a
  build, a flood alone (the reachable flags change), the Export's navigation
  stage - and at Save Scene for a scene with an installed mesh, current or
  stale (its record says what it was built from), into the editor asset of
  the saved file; a save without an installed mesh leaves the item as it is.
  A scene without a source file builds and shows its mesh and stores
  nothing; its first Save Scene stores it.
- Clear removes the scene's `navigation_mesh` items and empties the record.
- No drop removes the item the record names, and the prepared status counts
  it as asked; a `navigation_mesh` item no record names is unreferenced
  (`doc/editor/prepared_assets.md` "Status").

Opening the scene file (`open_scene_gltf`, not a prepared run, not an open
as foreign glTF; `load_navigation_item()`) reads the item the record names
and installs it with its visualization, after the opened nodes are in the
scene and their transforms propagated. The input record is rebuilt against
the opened scene by glTF uid: a saved mesh the scene lacks, a mesh whose
render shapes' content hashes differ from the saved ones, a transform not
bit-equal to the saved one, a mesh the input admission no longer admits, and
an input mesh the build did not read are "geometry changed"; the scene's
build settings against the saved ones are "settings changed", and its seed
nodes and points against the saved seeds (by uid) "seeds changed" - the
reasons of "Staleness", which from then on follow the live edits (an undo of a
move of a reopened mesh back to its saved transform ends it). A record naming
a missing or unreadable item, or an item of another byte form version,
installs nothing and logs one line. A build without `ERHE_USE_DETOUR` ignores
the record and leaves the item alone.

## Visualization

`install()` and `install_flood()` take the "Navigation Mesh" node the job's
main-thread finish builds (`make_navigation_visualization()`,
`scene/navigation_visualization`; in the prepare phase, with the upload
command buffer recording) and attach it under the scene root in place of the
previous one; `clear()` and the scene's close detach it. A flood changes
which polygons each of its meshes holds, so it gets a new node too. Nothing
about it runs per frame.

- The node sits at the scene root, so it is in the scene root's space with
  the navigation mesh, lifted 5 cm along the root's Y so its polygons do not
  z-fight the floor. It and its meshes carry `render_proxy | session_only`
  and `Editor_item_flags::navigation_mesh`, not `content`, `id` or
  `show_in_ui`, and no shadow cast.
- Two meshes in the navigation layer (`Mesh_layer_id::navigation`, a layer
  of its own): "Navigation Mesh Reachable" (also
  `Editor_item_flags::navigation_reachable`) and "Navigation Mesh
  Unreachable", each built only when its class has polygons. Each has one
  primitive, fill triangles and edge lines, from a Geometry whose facets are
  its polygons' detail triangles over bit-equal welded vertices and whose
  mesh edges are the polygon borders alone (an edge two triangles of one
  polygon share is left out), so the fill follows the detail surface and the
  lines are the polygon outlines. Its material is translucent (opacity
  0.45), double-sided, unlit and session-only and sits under the mesh: green
  for the reachable polygons, magenta for the others. Being session-only, it
  is not listed by the scene's content library, so no Materials list shows it
  (`doc/editor/content_library.md` "The index").
- So the ID pass (content, rendertarget and bone layers), ray queries (render
  proxy: raytrace mask 0, and no raytrace geometry), the PVS static set, the
  world-space bake, the prepared keys,
  thumbnails, saves and exports (session-only, render proxy) and the item
  trees (no `show_in_ui`) leave it out, and the input admission
  (`is_navigation_input_flag_admitted()`) never reads it. Attaching it
  touches neither the undo stack nor the node tree the user authored.
- `App_rendering` draws it with four composition passes of the navigation
  layer, after the translucent content fills (`Composition_pass_kind::scene_content`,
  camera exposure ignored): "Navigation mesh reachable" and "Navigation mesh
  unreachable" (translucent fills) and their "... borders" (edge lines in
  dark green and dark magenta, 1.5 px, through `Content_wide_line_renderer`;
  `prepare_wide_line_feed()` feeds them as the other edge-line passes).
- Which polygons a view draws is the view's
  `Debug_visualizations_settings::navigation_mesh`
  (`Navigation_mesh_visualization`: `off`, `reachable` - the default - or
  `all`), the Debug Visualization popup's "Navigation Mesh" combo. The
  setting is per view and the meshes are per scene, so the passes' activation
  predicates read the rendered view's setting as they prepare: the reachable
  passes draw unless it is `off`, the unreachable ones when it is `all`. The
  combo writes the setting and the next frame draws by it; a newly installed
  node draws by the same setting.
- MCP `navigation_set_visualization {mode, viewport}` sets the viewports'
  setting; `render_scene_image` draws the navigation mesh only with
  `navigation_mesh` (`reachable` or `all`; default `off`), its fills and its
  borders (`doc/editor/rendergraph.md` "Scene image capture").

## MCP

- `navigation_set_seed {scene_name, node, node_id, seed}`: marks (`seed`
  true, the default) or unmarks the node, executed at once and undoable;
  returns `{scene_name, node, id, uid, point, seed, changed}`.
- `navigation_build {scene_name}` (`ERHE_USE_RECAST`): queues the job and
  returns `{job_id, ...}` at once; `get_save_status {job_id}` reports it,
  and a done job carries `navigation` (the status fields below). The
  refusals are tool errors.
- `get_navigation_status {scene_name}`: `available`, `pending` (a build of
  the scene is queued or running, or an Export's navigation stage runs for
  it), `seed_count` and `seeds` (per current
  seed node: `node`, `id`, `uid` - the glTF uid - and `point`, in the scene
  root's space); with `ERHE_USE_DETOUR` also `stale`, `stale_reasons`,
  `polygon_count`, `reachable_polygon_count`, `island_count`,
  `reachable_island_count`, `total_area`,
  `reachable_area`, `tile_count`, `input_mesh_count`, `input_shape_count`,
  `input_triangle_count`, `skipped_mesh_count`, `skipped_meshes`,
  `build_ms`, `flood_ms`, `build_count`, `flood_count`, `built_seeds` (per
  seed of the build: `node`, `uid`, `point` and `result` - `{found, polygon,
  snapped_point}` or `{found: false, reason}`), `item` (`{key, made, bytes}`
  of the item last stored or read, "Saving"; null without one) and
  `status_text`. It is answered while a save or build runs.
- `get_navigation_mesh {scene_name, reachable_only, max_polygons}`
  (`ERHE_USE_DETOUR`): `available`, `bounds_min`, `bounds_max`,
  `polygon_count`, `matching_count`, `returned_count`, `truncated` and
  `polygons` (`index`, `tile`, `vertices`, `area`, `reachable`).
- `get_navigation_summary {scene_name, max_islands}` (`ERHE_USE_DETOUR`):
  `available`, `stale`, `total_area`, `reachable_area`, `polygon_count`,
  `island_count`, `reachable_island_count`, the islands largest first, at most
  `max_islands` (default 32; `returned_island_count`, `truncated`), each with
  `id`, `area`, `polygon_count`, `bounds_min`, `bounds_max`, `reachable` and
  `point`, and `failed_seeds` (`node`, `point`, `reason`) of the last flood.
- `navigation_find_point {scene_name, position | node_id | node,
  search_extents}` (`ERHE_USE_DETOUR`): `find_nearest_point()` for the
  position or the node's origin in the scene root's space; returns
  `available`, `stale`, `position`, `node` (when given), `search_extents`,
  `found` and, when found, `point`, `distance` (from the position), `polygon`,
  `island` and `reachable`.
- `navigation_find_path {scene_name, start, end, search_extents}`
  (`ERHE_USE_DETOUR`): `find_path()`; returns `available`, `stale`,
  `search_extents`, `start` and `end` (each a `navigation_find_point` result
  without the scene fields), `found`, `complete`, `length`, `corners` and,
  when not found, `reason` ("no polygon near start", "no polygon near end",
  "different islands" or "path search failed").
- `navigation_clear {scene_name}` (`ERHE_USE_DETOUR`): removes the mesh, its
  visualization node and its item; returns `{scene_name, cleared}`.
- `navigation_set_visualization {mode, viewport}` (`ERHE_USE_DETOUR`): sets
  the Navigation Mesh mode (`off`, `reachable`, `all`) of every viewport, or
  of the one titled `viewport`; returns `{viewports: [{viewport, mode}]}`.

`search_extents` are half extents `[x, y, z]` in meters, by default the
scene's `seed_search_radius`, `seed_search_height`, `seed_search_radius` (the
installed mesh's without export settings). The three reads, like
`get_navigation_status`, are answered while a save or build runs, and a stale
mesh answers them with `stale` true; without a mesh they return `available`
false. A scene name that matches no or several open scenes, and a node name that
matches no or several prims of the scene, are errors.

## Verification

`scripts/navigation_verify.py` (`--editor <binary>`, default the Linux
headless tree's editor when it is built, else the Linux Debug tree's) writes a scene of a 20 m floor, a mirrored and scaled ramp, a 1 m high
platform, a wall, three pillar nodes sharing one mesh and a seed node,
launches an editor with `ERHE_AI_DRIVER=1`, and checks the refusals, the
reachable area against the hand-computed one (within 5 %), the ramp reachable
and the platform present and unreachable, the reads of "Islands and
queries" (two islands - the floor with the ramp, reachable, and the platform's
top - largest first with bounds and points and the status's counts; the
nearest point by position and by node, over the platform, out of reach and
within wider `search_extents`; a path around the wall and one up the ramp,
none to the platform (different islands) or off the mesh; `stale` true on a
stale mesh), a second build equal to the first,
each staleness reason raised by its edit and ended by its undo (a hide of a
mesh and of its parent, and the show of a mesh hidden at build time,
included), the flood
alone after a seed search extent edit, the failure of a seed 50 m above the
scene with the installed mesh kept, and the unchanged node tree (the
visualization's render proxies left out) and undo stack. For the
visualization it checks the "Navigation Mesh" node and its two meshes (render
proxies, not content), three `render_scene_image` renders from 30 m above
(`off`, `all`, `reachable`): `all` differs from `off` over at least half of
the floor's pixels and `reachable` from `all` inside the platform's pixels
only; that `get_visible_entries` from the same pose names no navigation mesh,
a ray from above hits the floor, the saved glTF holds no
navigation node, mesh or material, a build with the node present reads the
same seven input meshes (the flood alone runs) and installs the node again,
`navigation_clear` removes it, `navigation_set_visualization` sets the
viewports, and closing the scene leaves nothing of it alive. For "Saving" it
saves the scene as an erhe scene in its work directory and checks that a build
stores the item and the record and an unchanged build finds it; that a save,
close and `load_scene` installs it with the same totals, seeds, seed results
and polygons, current, with the visualization node and no build; that a flood
alone replaces the item (one item in the editor asset) and the prepared status
counts it as present; that a moved mesh, a build setting edit and an unmarked
seed saved without a build make the reopened mesh stale with their reason
(and setting the value back or marking the seed again ends it); that drops of
unreferenced and of all items keep the item; that Clear removes the item and
the record and a reopen installs nothing; and that `get_scene_materials` lists
no navigation material. It prints one line per check and exits non-zero on a
failure.

The Export stage ("Export") is checked by the navigation section of
`scripts/prepared_asset_verify.py` (`--only-navigation`), on a 20 m floor
with a crate and a seed node: an Export refused before it is queued (no
source file) and one the service refuses (`pvs_mode` `rebake` without a
region) build no navigation mesh; with `mode` `off` the report has no
navigation fields; the first `build` Export reports `made` with the
installed mesh's totals, the Scene key and the prepared asset's items of
`off` and the navigation part of the summary line; a second reports
`present` and builds nothing; the prepared status matches and builds
nothing; the Exports leave the source binding and the undo stack as they
were; after a move of the crate an Export reports `made` again; without a
seed it succeeds with the `editor.export` warning and `skipped`; with the
seed 50 m up it fails with the seed's reason and leaves the prepared asset
(bytes), the editor asset and the installed mesh as they were.

## Measured: Bistro

`res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf` opened as a
foreign glTF in the Release editor (Vulkan, LODs off, no world-space bake), on
a 24-thread desktop CPU, with the default settings and one seed: an empty node
at (-12.51, 0.48, 12.19) in the scene root's space, 10 cm above the street
under the camera key at 2 s of the Bistro camera animation.

| Quantity | Value |
|---|---|
| Input | 2670 meshes, 3796 primitive shapes, 4 104 797 triangles, none skipped |
| Tile grid | 10 x 10 tiles of 19.2 m, 55 with polygons |
| Polygons | 2244, 617 reachable |
| Area | 16 707.8 m2 total, 4327.4 m2 reachable |
| Build (triangles and tiles) | 0.4 - 1.2 s over two runs |
| Flood | 0.02 ms |
| Visualization node | about 25 ms (1489 reachable and 4118 unreachable detail triangles) |

The street, the sidewalks and the plaza around the fountain are one
reachable region. The roofs, the restaurant awnings and the top of the
fountain are polygons that are not reachable.

## Future work

- [../plans/navigation_mesh.md](../plans/navigation_mesh.md)
