# Level tools: design

Status: in progress

The design that meets `doc/plans/level_tools.md` (requirements, cited as
R-numbers). The build order is in
`doc/plans/level_tools_implementation.md`. The code lives in
`src/editor/level/`; the navigation additions live in
`src/editor/navigation/`.

## D1 Shape definitions

- D1.1 `Kit_shape_definition` (`level/kit_shape.hpp`) describes one shape:
  `id` (`<kit>.<shape>`, for example `basic.block`), kit name, display
  name, description, tags, parameters, one material slot name, normal
  style, and the generator.
- D1.2 `Kit_parameter`: `name`, type (`float_value`, `int_value`,
  `enum_value` with its value names), `unit` (R3.2), the default, the
  minimum and the maximum. A float default is a number in one of three
  scales, `Kit_default_scale`: `meters`, `modules` or `wall_heights`; the
  last two resolve against `Level_settings` (D5) when a piece is placed
  (R2.1). Minimum and maximum are in meters.
- D1.8 Derive from seed (R3.9). Every definition has the int parameter
  `seed`. A `Kit_parameter` other than the seed states `derive`
  (`fixed` or `derivable`) and, when derivable, `derive_min` and
  `derive_max` (floats and ints; an enum derives over all its values), and
  whether it is in the shape's default mask. `Kit_parameter_values` holds
  the mask as one bit per parameter. `resolve_derived()` writes each
  masked parameter's value from a hash of the seed and the parameter's
  name through the integer random stream of D1.4, mapped into the derive
  range; it runs whenever the seed or the mask changes, so the stored
  values are always the resolved ones (a saved piece keeps its saved
  values until then), and generators,
  `validate()` and the mesh key read them like any other value. The
  canonical text (D1.3) carries the mask as the list of masked parameter
  names under the key `derive`, after the values.
- D1.9 Rock forms (R4.1). `nature.rock` has the enum parameter `form`
  (`blob`, `angular`, `worn`, `slab`) and the int parameter `bevels`
  (0 - 2). The hull forms take the convex hull of a seeded point cloud on
  a jittered Fibonacci sphere (about 12 points angular, about 24 worn,
  fewer with two bevels to stay under the triangle ceiling; the slab
  squashed in Y) and then run the bevel passes over all hull edges; the
  base is cut flat so a rock rests on its origin at y = 0. Hull and
  bevel use `erhe::geometry` (`make_convex_hull`, the bevel operation),
  which D1.6 allows.
- D1.3 `Kit_parameter_values` holds the values of one piece in the
  definition's parameter order. `validate()` checks each against its range
  and returns the R3.6 message. `to_canonical_text()` writes the values as
  JSON text with a fixed key order and hexadecimal floats; equal values
  give equal text. The text is the piece's saved form (D3) and the mesh
  sharing key (D2).
- D1.4 A generator is a function `(const Kit_parameter_values&,
  GEO::Mesh&)`. It writes vertices and counter-clockwise facets and reads
  nothing else, so its mesh is a function of the values (R3.2, R3.7). A
  seeded shape draws its random numbers from its own generator seeded with
  the `seed` parameter, using integer arithmetic only for the random
  stream so every platform gives the same mesh.
- D1.5 `Kit_shape_library` (`level/kit_shape_library.hpp`) is the table of
  all definitions, built once: `get_shapes()`, `find(id)`. Adding a shape
  is one generator and one table entry (R4.7). The starter set (R4.1) is
  its content.
- D1.6 D1.1 - D1.5 depend on `erhe::geometry`, glm and the standard
  library only, so `editor_level_tests` (`src/editor/level/test/`) builds
  them without the editor, the way `editor_navigation_tests` builds the
  navigation types. `kit_shape_checks.hpp` holds the mesh checks of R11.1
  (closed manifold, consistent winding, no degenerate facet, triangle
  count) that the tests run over every shape at its defaults and at the
  ends of its ranges.
- D1.7 Shape conventions in generators: Y up, front toward +Z, the origin
  at the center of the base (R2.2, R2.3); stairs and the wedge rise toward
  -Z from their front edge, so a piece placed at a floor's edge climbs
  away from the viewer.

## D2 Meshes are brushes

- D2.1 The mesh of one shape with one set of parameter values is a
  `Brush` (`doc/editor/brushes.md`): its geometry generator calls the
  shape's generator and `Geometry::process()`, and its placements share
  geometry, GPU buffers and the collision shape (R3.1, R3.3).
- D2.2 `Level_kits` (`level/level_kits.hpp`, an editor part reached
  through `App_context`) owns the shape library and the brush cache: a map
  from `id` plus canonical text (D1.3) to the brush. `get_brush(shape,
  values)` returns the cached brush or makes it. The cache holds the
  brushes outside every content library, so a scene file carries pieces
  and no kit brushes.
- D2.3 Collision (R2.11): a convex shape (block, wedge, cylinder, slab,
  wall, pillar, rock, hill, bush) takes the brush's convex hull. A shape
  that is not convex (stairs, door wall, arch, land mass, tree) supplies a
  `collision_shape_generator` that builds a triangle mesh shape from the
  generated geometry. A piece is placed with `Motion_mode::e_static`.

## D3 Pieces

- D3.1 A piece is a mesh node that carries the attached property group
  `Kit_piece` (`level/kit_piece.hpp`, the pattern of `Brush_placement` and
  `Lod`, `doc/erhe/property_system.md` section 4.11): the key
  `Kit_piece.shape` (string, the shape id) and `Kit_piece.parameters`
  (string, the canonical text of D1.3). Both are serialized, so they ride
  the node's `ERHE_node` properties (R8.1). `read_kit_piece(node)` returns
  the record or nothing.
- D3.2 Placing (R5.1): `Level_kits::place()` takes a `Kit_placement`
  (scene, shape id, values or none for the defaults, parent, position,
  rotation, name, material, surface options of D7.3) and returns the node. It resolves the
  defaults (D1.2), validates (D1.3), gets the brush (D2.2), places it
  through `place_brush_in_scene()` and writes the `Kit_piece` values on
  the node before the insert operation is queued; several placements of
  one call are one `Compound_operation` (R5.10, R6.5).
- D3.3 Changing parameters (R3.4): `Level_kits::set_parameters(node,
  values)` builds one operation that replaces the node's mesh primitive
  and collision shape with those of the brush for the new values and
  writes `Kit_piece.parameters`; undo restores all three. The node keeps
  its id, name, transform, children and material.
- D3.6 Live parameter drags (R3.4). While a parameter row is dragged,
  every changed value that validates is shown at once:
  `Level_kits::preview_parameters(node, values)` gives the node the mesh
  primitive of the brush for those values without an operation, and
  remembers the piece's state from before the drag. When the drag ends,
  `set_parameters()` runs the one operation of D3.3 from the remembered
  state to the final values, collision shape included; a drag that ends
  on invalid values, and a piece deselected mid-drag, restore the
  remembered state. The shape being placed updates the Brush tool's kit
  brush the same way on every change. The brushes of the intermediate
  values leave the cache with their last holder (D2.2).
- D3.4 Materials. A kit supplies its palette (R2.9) as named materials
  with flat colors; `Level_kits` adds a palette material to the scene's
  content library the first time a piece needs it, found again by name. A
  piece's material is the caller's choice or the default of its shape's
  slot. A shape has one slot; more slots per shape are in the
  implementation plan's later steps.
- D3.5 Following the kit at load (R8.3, R8.4): after a scene loads,
  `Level_kits::refresh_scene()` visits the nodes that carry `Kit_piece`
  and gives each the mesh and collision shape of the cached brush for its
  record; a piece whose shape is unknown or whose values the shape rejects
  keeps the loaded mesh and is named in one log line and in the load
  report.

## D4 Exposure

- D4.1 UI (R6.2): the "Level Kit" window (`level/level_kit_window.hpp`)
  lists kits and shapes, shows the parameter rows of the shape to place
  and the placement settings "Snap To Module", "Snap To Surface",
  "Orient To Surface" and the rotation step (R5.2, R5.11; kept in the
  editor settings). For selected pieces it has "Snap Selected To Surface"
  and "Orient Selected To Surface" (D7.3) and, for a selected piece, the
  same parameter rows bound to the piece: a row commits one
  `set_parameters()` at `IsItemDeactivatedAfterEdit()`.
- D4.4 Placing in the viewport (R5.1, R6.2). Choosing a shape in the
  window hands the Brush tool (`doc/editor/brushes.md`) the kit brush of
  the shape's current values and makes it the active tool in kit mode;
  changing a parameter row hands it the brush of the new values. In kit
  mode the Brush tool keeps its own hover, preview, grid snap and insert
  machinery and takes the piece's transform from the level tools: the
  origin of the piece at the hovered point (on content, else on the
  grid), rounded to the module when "Snap To Module" is on, upright,
  turned about the up axis by the tool's rotate commands in rotation
  steps (90 degrees by default), then the surface options of D7.3. The
  preview mesh shows the piece at exactly the transform a click places
  it at. A click places through `Level_kits::place()` (D3.2), so the
  piece gets its record, palette material and static body, and the tool
  stays in kit mode for the next piece. Choosing a palette brush, or
  another tool, leaves kit mode.
- D4.5 Seed rows (R3.9). A parameter row of a derivable parameter has a
  "from seed" toggle in front of it; a masked row shows the derived value
  disabled. Toggling it, like a value edit, is one `set_parameters()`.
  The placement settings have "New Seed Per Piece": after each placed
  piece the Brush tool's kit values take the next seed. MCP: the catalog
  reports `derive`, `derive_min`, `derive_max` and `default_derived` per
  parameter; `parameters` objects accept and report `derive: [names]`.
- D4.6 Surface settings everywhere (R5.11). "Snap To Surface" and "Orient
  To Surface" are one setting pair, `Surface_snap_config` in the editor
  settings (both off by default), read by kit placement (D4.4), the Level
  Kit window, two toggle buttons in the viewport toolbar and two rows of
  the Transform tool's Move settings. In the Transform tool they apply to
  translate drags of the gizmo: after the tool computes the new
  transforms of a step, each moved mesh node goes through
  `find_surface_placement()` (D7.3) with all moved meshes excluded, and
  the result is what the step applies; a node that finds no surface keeps
  the tool's transform, and a node with a moved ancestor rides rigidly
  with it. A drag constrained to the surface direction's axis alone is
  left as the tool computed it, so a piece can be lifted off a surface.
  Rotate drags, scale drags and numeric edits are left as computed. The
  drag stays one undo entry. MCP `transform_selection` and
  `drag_selection` take explicit `surface_snap` and `surface_orient`
  (default off) in place of the shared pair.
- D4.2 MCP (R6.3 - R6.5; handlers in `mcp/mcp_server_level.cpp`):
  - `level_get_kits {scene_name}`: the catalog of R6.3, with the float
    defaults resolved against the scene's level settings.
  - `level_place_pieces {scene_name, snap, surface_snap, surface_orient,
    pieces: [{shape, parameters, position, rotation_xyzw, name, material,
    parent_id, parent_index}]}`: one undo entry; returns per piece `id`,
    `name`, `shape`, the effective `parameters`, the placed `position` and
    `rotation_xyzw`, `surface_found`, `world_aabb`, `triangle_count`.
    `snap` is `none` (default) or `module` (positions round to the module,
    R5.2); `surface_snap` and `surface_orient` (both default off) are the
    options of D7.3, and a piece may state its own.
  - `level_snap_to_surface {scene_name, node_ids, orient, direction,
    query_only}`: D7.3 on placed pieces, one undo entry; returns per node
    the resting `position`, `rotation_xyzw`, `distance` and
    `surface_found`.
  - `level_get_pieces {scene_name}`: every piece with shape, parameters,
    transform and world bounds.
  - `level_set_piece_parameters {scene_name, node_id, parameters}`: D3.3;
    the given parameters merge over the piece's current values.
- D4.3 Both sides call `Level_kits` (D3.2, D3.3), which is the one code
  path of R6.1.

## D5 Level settings

`Level_settings {module = 1.0, wall_height = 3.0}` (codegen,
`scene/definitions/level_settings.py`) is the optional field `level` of
`Scene_settings`; a scene without it uses the defaults. It is edited in the
"Level Kit" window and through MCP `set_scene_settings` (R2.1).

## D6 Walkable area reads

- D6.1 Islands (R9.4): `Navigation_mesh` computes, when it is filled, the
  island of each polygon (a flood over the neighbour arrays) and one
  `Navigation_island` per island: polygon count, area, bounds, whether a
  reachable polygon belongs to it, and the centroid of its largest
  polygon as the representative point. `mark_reachable` updates the
  reachable flags of the islands.
- D6.2 Point and path (R9.5, R9.6): `Navigation_mesh` owns one
  `dtNavMeshQuery`, created when the mesh is filled and used on the main
  thread, and keeps the mapping between its polygon indices and Detour
  polygon references in both directions. `find_nearest_point(position,
  half_extents)` returns the point, its polygon and island;
  `find_path(start, end, half_extents)` returns found or not, whether the
  path reaches the end polygon, its length and its corner points
  (`findPath`, `findStraightPath`).
- D6.3 MCP (in `mcp/mcp_server_navigation.cpp`, present with
  `ERHE_USE_DETOUR`, answered during a save like `get_navigation_status`):
  - `get_navigation_summary {scene_name, max_islands}`: `available`,
    `stale`, total and reachable area, `island_count` and the islands
    largest first (`id`, `area`, `polygon_count`, `bounds_min`,
    `bounds_max`, `reachable`, `point`), and the failed seeds with their
    reasons (R9.3).
  - `navigation_find_point {scene_name, position | node_id,
    search_extents}` (R9.5).
  - `navigation_find_path {scene_name, start, end, search_extents}`
    (R9.6).
- D6.4 UI: the scene Properties "Navigation" group shows the island
  count and the reachable island count in its status rows.

## D7 Spatial tools

Handlers in `mcp/mcp_server_level.cpp`, the queries themselves in
`level/level_queries.hpp` so the UI reaches them too. Positions are in the
scene root's space (R10.1).

- D7.1 Region contents (R10.2): world bounds of mesh nodes against the
  box or sphere.
- D7.2 Height map (R10.6): one downward ray per cell center through the
  scene's raytrace scene, committed once; with a navigation mesh, the
  walkable flag and island id of the nearest polygon at the hit. The text
  picture maps heights to digits and marks cells without a hit.
- D7.3 Surface placement (R5.11, R10.5): `find_surface_placement()`
  takes a piece's mesh bounds, a start transform, a direction (down by
  default) and the two options, and returns the resting transform. It
  casts one ray along the direction from the base center, and from the
  base corners too when only snapping, through the scene's raytrace scene
  with the moved pieces masked out. Each ray starts one piece height
  back from the base, against the direction, so a piece sunk into a
  surface rises onto it. Snap moves the piece by the smallest hit
  distance, so its base rests on the highest surface under it. A ray
  that passes through a moved piece's own face and then leaves a solid
  takes the passed face's position as the surface, so a piece already
  resting on a surface stays on it. Orient
  rotates the piece by the shortest rotation that takes its up axis to the
  center hit's normal, which keeps its heading, and then rests the base
  center on the hit point. Placement (D3.2) applies it to the new piece
  before the insert; the tool form applies it to placed pieces as one
  transform operation.
- D7.4 Support report (R10.4): the snap rays of D7.3 without the move; a piece
  whose smallest distance exceeds the tolerance is unsupported.
- D7.5 Overlap report (R10.3): bounds as the first pass, then the physics
  world's body intersection test with the tolerance.
- D7.6 Level check (R10.7): runs D7.4, D7.5, the unreachable islands of
  D6.1 above the area threshold and R2.10 over the pieces' parameter
  values.
