# Level tools

Stability: experimental

The editor builds walkable levels by kit-bashing: a catalog of parametric
shapes in three kits, placed as pieces that share one mesh per shape and
parameter values. The requirements are `doc/plans/level_tools.md` (R-numbers),
the design `doc/plans/level_tools_design.md` (D-numbers). The code lives in
`src/editor/level/`; the MCP tools are in `src/editor/mcp/mcp_server_level.cpp`.

## Shapes

All in namespace `editor`, depending on `erhe::geometry`, glm and fmt only, so
`editor_level_tests` (`src/editor/level/test/`) builds them without the rest of
the editor.

- `Kit_shape_definition` (`kit_shape.hpp`): one shape: `id` (`<kit>.<shape>`),
  `recreates` ("Recreated shapes"), kit, display name, description, tags, parameters, its material slots
  ("Materials"), normal style (`faceted`, `smooth`, `smooth_with_hard_edges` with its angle),
  triangle budget, collision kind (`Kit_collision::convex_hull`,
  `triangle_mesh` or `none`, "Kit brushes"), mesh kind (`Kit_mesh_kind::solid`,
  a closed manifold, or `surface`, a stated surface whose boundary is open:
  the water), randomness (`Kit_randomness::seeded` for the shapes whose
  generator draws from the seed: every nature shape but the water), the
  mirror planes ("Groups") and the generator.
- `Kit_parameter`: name, type (`float_value`, `int_value`, `enum_value`), unit,
  range and default, and its derive range ("Derive from seed"). A float
  default is stated in meters, modules or wall heights (`Kit_default_scale`)
  and resolves against the level settings ("Level settings") when a piece is
  placed.
- `Kit_parameter_values`: the values of one piece in parameter order and its
  derive mask. `validate()` returns the message naming the parameter and its
  range, or naming a masked parameter that cannot be derived;
  `to_canonical_text()` writes JSON text with a fixed key order and
  hexadecimal floats, then `"derive"` with the masked names when the mask is
  not empty, which is the piece's saved form and its mesh key. Equality
  compares the values and the mask.
- `Kit_shape_library` (`kit_shape_library.hpp`): the kits, each with a
  triangle ceiling (`Kit_definition::triangle_ceiling`, R2.6), the shapes, a
  recreated shape beside each original that has one ("Recreated shapes"),
  and the palette ("Materials"). Adding a shape is one generator and one
  table entry. The kits, the palette and every shape's generator are
  described in `doc/editor/level_kit_shapes.md`.
- Shape conventions: Y up, front toward +Z, the origin at the center of the
  base (a seeded shape, and a shape whose `origin_rule` is
  `Kit_origin_rule::base_contains_origin`: inside its base; the tower, whose
  doorway leaves the base's front open); stairs and the wedge rise toward -Z
  from their front edge. `kit_shape_checks.hpp` holds the mesh checks the
  tests run.

## Derive from seed

Every shape has the int parameter `seed` (0 - 1000000, default 1), its last
parameter. A parameter other than the seed is `Kit_derive::fixed` or
`derivable`; a derivable one states its derive range (`derive_float()`,
`derive_int()`, `derive_enum()` in `kit_shapes.hpp`): a float's minimum,
maximum and step in its default scale, resolved against the level lengths like
the default and clamped to the valid range (`Kit_parameter::resolve_derive_range()`),
an int's minimum and maximum, an enum over all its values.
`from_seed_by_default()` puts it in the shape's default mask.

A piece's mask is one bit per parameter (`Kit_parameter_values::get_value_source()`,
`set_value_source()`, `Kit_value_source::seed`). `resolve_derived(module,
wall_height)` writes every masked value with `derive_parameter_value()`: one draw
of a `Kit_random` stream seeded by the seed and the parameter's name (FNV-1a of
the name mixed into the seed), mapped into the derive range, a float quantized to
the range's step. A derived value depends on the seed, the name and the derive
range alone, so adding a parameter changes no other derived value.
`make_default_values()` sets the default mask and resolves it.
`kit_next_seed(seed)` is the next seed: a hash of the seed, never the seed
itself.

The stored values are the resolved ones; generators, `validate()` and the mesh
key read them like any other value. They are derived again only when the seed
or the mask changes: by the parameter rows, by MCP `level_place_pieces` (always,
at the scene's lengths) and `level_set_piece_parameters` (when the given values
change the seed or the mask), and by "New Seed Per Piece". The saved values are
authoritative at load: `refresh_scene()` derives nothing, so a piece keeps its
values when the level lengths or a derive range change later. A record without
the `seed` parameter, or with a name the shape does not have, does not read and
keeps its loaded mesh (R8.4); there is no migration.

The derivable parameters, their derive ranges and the default masks of
every shape, and how the ranges keep the walkable shapes walkable, are in
`doc/editor/level_kit_shapes.md` ("Derive ranges" and each shape's
section).

## Recreated shapes

A recreated shape is a second version of a shape, written afresh from the
shape's description, registered beside its original so the two can be
compared in one scene (`doc/plans/level_tools.md` U41). The original keeps
its id, generator and pinned mesh.

- Id and name: the original's id plus `.v2` (`basic.block.v2`), the
  original's display name plus ` (F)` (`c_kit_recreated_id_suffix`,
  `c_kit_recreated_name_suffix` in `kit_shapes.hpp`).
- `Kit_shape_definition::recreates` holds the original's id; it is empty for
  an original. Tools, tests and the comparison script pair the two by this
  field, never by parsing ids.
- Pairing rule: a recreated shape names an existing original in
  `recreates`, has the original's kit and the id `<original>.v2`. Its
  parameters (added ones, other ranges and defaults), material slots,
  connectors, seam sides and the rest of its definition are its own.
  `make_recreated_shape()` returns a copy of the original's definition with
  the new id, name, generator and triangle budget, which the generator file
  changes before adding it. Replace ("Replace") from either shape to the
  other carries the parameter values and slot materials the two share.
- Order: the library places each recreated shape directly after its
  original, which is the order of the Level Kit window's rows, the Replace
  With list and `level_get_kits`.
- Source files: the recreated generators of `kit_shapes_<name>.cpp` are in
  `kit_shapes_<name>_v2.cpp`, added by `add_<name>_shapes_v2()` after the
  originals. A batch of shapes - recreated (`basic_v2`, `architecture_v2`,
  `medieval_v2`, `nature_v2`, `nature_plants_v2`, `nature_rocks_v2`) or new
  (`build_13_16`, `build_17_20`, `build_21_24`, `extra_01_08`,
  `extra_09_16`, `extra_17_24`, `extra_25_32`, `extra_33_36`,
  `machine_01_08`, `machine_09_18`) - edits only its generator file
  `kit_shapes_<batch>.cpp`, its expectation rows
  `src/editor/level/test/kit_shape_rows_<batch>.cpp` and its tests
  `src/editor/level/test/test_kit_shapes_<batch>.cpp`; the tests that go
  over the library take the union of every batch's rows.
- `get_contract_id(shape)` (`kit_shape.hpp`) is the original's id for a
  recreated shape and the shape's own otherwise. Code that acts on what a
  shape is - the stairs' "steps, step rise, tread depth" line of the
  parameter rows and the Properties window, `level_get_pieces`' `stairs` -
  compares this id, so it treats both versions alike.
- Which version keeps the canonical id is the user's ruling per shape; both
  ids exist until that ruling, and a saved piece or layout item names the
  one it was placed with.

The recreated shapes and how each differs from its original are described
in `doc/editor/level_kit_shapes.md` (the "(F)" sections of each kit).

`scripts/level_shape_compare.py [--editor <binary>] [--only <id prefix>]
[--gap <meters>] [--parameters <json object>] [--tag <name>]
[--detail <x,y,z,distance>] [--azimuth <degrees>] [--elevation <degrees>]
[--save <path>] [--keep-open]` launches the editor, lists
the pairs from `level_get_kits` (`recreates`), and builds one scene with a
row per pair at the default values: the original on the left (-X), the
recreated shape on the right (+X), their bounds `--gap` (1 m) apart on a
ground pad of the row's own (floor slabs, tiled where the pad is wider
than a slab's 64 m); a seeded shape makes three rows, at seeds 1, 3
and 17. The rows run toward -Z. It writes one three-quarter view per row,
`logs/shape_compare_<original id>.png` (a seeded shape's
`..._<seed>.png`), every row of one size class through the same camera, its
far plane ending the view behind the row. `--parameters` gives both shapes
of every row the given values (used with `--only` naming the shape they
belong to) and `--tag` ends the image names with `_<name>`, so such a run
keeps the default images; `--detail` renders a close-up of the point x, y, z
of the recreated piece's own frame from the given distance instead of the
pair (a negative x is written `--detail=-1,2,0,6`); `--azimuth` and
`--elevation` turn the camera. `--recreated-parameters <json object>`
gives values to the recreated shape of every row alone, for the parameters
its original lacks; a value for a parameter that is derived by default needs
`"derive": []` in the same object. `--single` renders the shapes that have no
recreated twin, each alone on its pad (with `--only <id>`, `--parameters`,
`--tag`, `--detail`, `--azimuth`, `--elevation`). `--sun <intensity>` adds a
directional light and `--ambient <level>` sets the scene's ambient light: a
scene whose only light is a piece's own renders everything that light does
not reach black, so a lit shape is rendered with both; `--light-shadows`
turns shadow casting on for the pieces' light nodes. `--save` writes the
scene for the windowed editor to open; `--keep-open` leaves the launched
editor running with the scene.

## Pieces

A piece is a mesh node carrying the attached value group `Kit_piece`
(`kit_piece.hpp`, `doc/erhe/property_system.md` sections 4.11 and 4.23):

- `Kit_piece.shape` (string, the key property): the shape id.
- `Kit_piece.parameters` (string): the canonical text of the values.

Both carry the serialize flag, so they ride the node's `ERHE_node`
properties. Both are developer-only rows (`Property_ui::developer_only`): the
Properties window shows the piece through its parameter rows ("Parameter
rows"), and MCP `get_item_properties` and `set_item_property` read and write
the raw values. `read_kit_piece(node)` returns the record or nothing,
`write_kit_piece(node, record)` sets both.

A piece is also a brush placement (`doc/editor/brushes.md`): its
`Brush_placement.brush` names the kit brush whose part primitives its mesh
carries, one mesh primitive per material slot ("Materials").

## Materials

A shape declares an ordered list of named material slots,
`Kit_shape_definition::material_slots` (R2.9, D1.11); each shape's slots are
in its section of `doc/editor/level_kit_shapes.md`. The generator assigns every facet to a slot:
`Kit_mesh_builder::set_slot()` names the slot of the facets added after it,
and `write()` stores each facet's slot index in the facet attribute
`kit_slot` (`c_kit_slot_attribute`, `kit_mesh_builder.hpp`).
`read_kit_facet_slots()` reads it back and `extract_kit_slot_mesh()` copies
the facets of one slot with their vertices.

The palette (`Kit_shape_library::get_palette()`) is a list of flat-color
materials (`Kit_palette_material`: name, linear base color, roughness,
metallic, linear emissive color). Each slot name has a `Kit_slot_palette`
(`get_slot_palettes()`, `find_slot_palette()`): its default material and a
few alternatives, by palette name. Both lists are in
`doc/editor/level_kit_shapes.md` "Slots and palette".

A piece's mesh has one primitive per slot, in slot order, each with its own
material; the primitives' materials are the piece's per-slot assignment, so
the record (`Kit_piece`) carries no material. A placement names a material
per slot as a `Kit_slot_material`: a material of the scene, or a palette
material by name, or neither, which takes the slot's default. A palette
material is found in the scene's content library by name, else made and
inserted there in the same undo entry as the pieces that first need it.

`Level_kits::set_slot_material(scene_root, node, slot, material)` gives one
slot of a placed piece a material as one undo entry: a
`Mesh_material_assign_operation` of that primitive, after the palette
material's insert when the scene does not hold it yet. A material equal to
the slot's current one records nothing. The other slots keep theirs.

## Kit brushes

`Level_kits` (`level_kits.hpp`) is the editor part that owns the shape library
and the kit brush cache, reached as `App_context::level_kits`. The cache maps
the mesh key - shape id plus canonical text, `Level_kits::get_mesh_key()` - to
a `Brush`. `get_brush(values)` returns the cached brush or makes it:

- The brush's geometry generator runs the shape's generator and
  `Geometry::process()` (connect, edges, facet texture coordinates); this
  whole geometry, of every slot, gives the brush's bounds, convex hull and
  triangle mesh collision shape. A shape of several slots also gives the
  brush one part geometry per slot (`Brush_data::part_geometries`,
  `doc/editor/brushes.md` "Parts"), each the slot's facets processed the same
  way; a slot without facets for the values makes no brush. A shape of one
  slot has its whole geometry as its one part. A
  faceted shape renders with polygon normals; a smooth shape with corner
  normals averaged over the facets within 80 degrees of each other, so a base
  rim stays sharp; a shape with hard edges with corner normals averaged
  within its stated angle.
- Collision: a `convex_hull` shape takes the brush's convex hull; a
  `triangle_mesh` shape (stairs, door wall, arch, entablature, torii,
  battlement, tower,
  land mass, tree, cliff, shore, palm) has a collision shape generator that builds a triangle mesh
  shape from the generated mesh (`build_triangle_mesh_shape()`,
  `scene/collision_shape_from_mesh.hpp`). A `none` shape (the water and the
  leaf plant, P5 of
  `doc/plans/level_kit_extra_shapes.md`) makes its brush with
  `Brush_body::none` (`doc/editor/brushes.md`): no hull and no generator, so
  its pieces get no physics body, and `place()` gives each piece the
  authored flag `Editor_item_flags::pass_through` (saved by name): the
  mesh is no ground, neither navigation input (`doc/editor/navigation.md`
  "Input") nor a surface for snap to surface and the support report
  ("Surface placement").
- Kit brushes belong to no content library: a scene file carries pieces and
  no kit brushes. Every piece of one mesh key shares the brush's geometry,
  part primitives, GPU buffers and collision shape.

Cache lifetime. The cache holds weak references. The strong references are
held by whoever needs the brush: each piece's `Brush_placement.brush`
(including a piece that only an undo or redo entry still holds) and each
parameter change operation, which holds the brushes of both of its states.
A brush, with its geometry, GPU buffers and collision shape, is released when
the last of those goes - its last piece deleted for good, or the undo history
that held it cleared - and its cache entry is removed the next time the cache
makes a brush. `get_brush()` of a released key makes the brush again.
`get_cached_brush_count()` counts the live brushes; `level_get_kits` reports it
as `cached_brush_count`.

## Placing

`Level_kits::place(scene_root, placements, snap)` takes a list of
`Kit_placement` (shape, values or none for the defaults, parent or the index
of an earlier placement of the same call, position, rotation, name, a
material per slot or none, surface options) in the scene root's space. It
checks every placement first (known shape, values of that shape, values in
range, parent index earlier than the piece, a unit surface direction, no more
materials than slots and each named palette material in the palette) and places
nothing when one fails, reporting the index and the message. Then, per
placement:

- the defaults resolve against the scene's level settings;
- with `Kit_snap::module` the position rounds to multiples of the module;
- each slot's material is the given one, else the slot's default palette
  material ("Materials"), found in the scene's content library by name or
  created and inserted there the first time a piece needs it;
- the surface options of the placement (`Kit_placement::surface`, "Surface
  placement") move and turn the module-snapped transform;
- `make_brush_instance_insert()` (`brushes/brush.hpp`) makes the node with a
  static rigid body, a mesh primitive per brush part with that slot's
  material, and its insert operation; the node takes the given name
  or the shape's display name and its `Kit_piece` record before the insert.

All material inserts and node inserts of the call form one
`Compound_operation`, run through `Operation_stack::execute_now()`: one undo
entry, which removes every piece of the call and the palette materials it
added.

## Pattern expansion

The arrangement of a kit layout node ("Kit layout nodes", R5.4, D3.7) is a
pattern expanded from a start transform into one pose per element.
`level_repeat.hpp` is that scene-free part, built into `editor_level_tests`:
`Repeat_pattern` (kind `Repeat_pattern_kind` line, grid, ring, path,
scatter ("Scatter layouts") or segments and the fields of each) and `expand_repeat(pattern, start, out_poses)`, which
returns the poses of the run with element 0 at the start, or a refusal and no
poses. A pose is a position, a rotation and a scale, which only segments
sets (to (s, 1, 1)); the other kinds keep 1. A kit layout node expands from its own origin
(`resolve_layout_pattern()` turns its values into a `Repeat_pattern`); the
axes are the start rotation's.

- line: `count` elements, element i at the start plus i times `spacing` in
  the start frame, each with the start rotation;
- grid: `count_x` by `count_z` elements along the start frame's x and z axes,
  `spacing_x` and `spacing_z` apart, x fastest;
- ring: `count` elements turned about the start frame's up axis through a
  center `radius` behind the start (against its +z front), counter-clockwise
  seen from above; a full ring (`arc_degrees` 360, the default) spaces them
  360 / count degrees apart, a smaller arc puts the first and the last element
  at its ends. With `Repeat_ring_facing::outward` each element turns with its
  angle, so a piece facing out of the ring at the start faces out everywhere;
  `start_rotation` keeps the start rotation;
- path: `count` elements along a curve, `path` (an `erhe::math::Curve_path_table`
  in the start frame, `doc/erhe/math.md` "Curve path"), at distances 0,
  `path_step`, 2 `path_step` ... (an open curve's last clamped to its length).
  A distance within 1e-4 of a step of a control point's distance is that
  point's distance, so an element at a corner takes the span that leaves it
  (the table's rule) even when the step rounds short of it. The frames are
  the table's up-keeping frames about the start frame's up axis
  (`get_up_frames_at_distances()`). With `Repeat_path_orientation::upright`
  (the default) each element's x axis is the tangent's heading
  (cross(side, up)) and its up axis the start frame's: a sloped span keeps
  the pieces upright, and a vertical span keeps the heading of the samples
  around it, leading vertical samples taking the first heading after them
  (the table's fallback). With `tangent` the x axis is the tangent and the up
  axis the frame's up, so a piece pitches with the curve. A polyline is a
  path along a curve of corner points.
- segments: one element per segment of `path` (a span between two
  neighbouring control points: n - 1 for an open curve of n points, n for a
  closed one, the last from the last point back to the first), element i
  taking `segment_items[i modulo the item count]` (`Repeat_segment_item`,
  the item's bounds `min_x` and `max_x` along its own x axis). Each element
  spans the straight chord from point i to point i + 1; handles and point
  kinds do not bend it (`make_segment_pose()`). With `upright` its x axis is
  the chord's heading in the start frame's xz plane and its up axis the start
  frame's, its scale (s, 1, 1) with s the chord's plan length over
  `max_x - min_x`, and its origin placed so that the scaled `min_x` lands on
  point i: the piece stands upright at point i's height and its scaled
  `max_x` lands above or below point i + 1 by the chord's rise. With
  `tangent` its x axis is the chord itself, its up axis the start frame's up
  made perpendicular to it, and s the chord's length over the extent, so both
  ends land on the points.

A count below 1, a run of more than `c_max_repeat_count` (1024) elements, a
zero spacing along a repeated axis, a radius that is not positive, an arc
outside (0, 360], a path without a curve, a negative path step and a value
that is not finite are refused with a message naming the value. Segments
refuse a curve of fewer than 2 points, no item, an item whose x extent is
not positive, a chord shorter than `c_min_segment_length` (1e-4 m) and a
vertical chord (under `upright` it has no heading, under `tangent` no up
axis across it), naming the segment and its points ("segment 1 (points 1 -
2) ...").

## Scatter layouts

A scatter (R5.5, D3.11) is the kit layout kind `scatter` of an Xform node: its
pieces stand at seeded points of a region about the node's origin and are the
node's children, generated, locked, refreshed, previewed and undone as every
kind's ("Kit layout nodes"). Its values in `Kit_layout_pattern`:

- `region`: `rectangle` (`size_x` by `size_z`, default 8 x 8 m) or `disc`
  (`region_radius`, default 4 m), centred on the node's origin in its xz
  plane; each piece's center lies inside it;
- `scatter_mode`: `count` (the shared `count`) or `density` (`density` pieces
  per square meter, default 0.25, times the region's area, rounded to the
  nearest count and at least 1); a count above 1024 is refused, naming it;
- `min_spacing` (default 1 m): the least gap between two sampled points'
  footprints in the node's xz plane. A footprint is the circle about the
  point's vertical axis (the node's up axis) around the xz rectangle of its
  item's bounds, of radius `get_scatter_footprint_radius()` =
  sqrt(max(|min.x|, |max.x|)^2 + max(|min.z|, |max.z|)^2), from the item's
  own bounds ("Kit layout drawing", Item sizes); it does not depend on the yaw, and spacing 0 lets footprints
  touch. A candidate is accepted when its squared center distance is not
  below the square of the two radii plus `min_spacing` against every
  accepted point, in the sampler's float steps: the test is deterministic
  and free of fused multiply-add on every platform, though a pair within
  about one float step of touching may go either way of the real
  comparison. The region holds the piece centers, not the footprints.
  The gap holds between the sampled points; the generation after sampling
  can narrow it: with Per Piece Seed an element is spaced by its item's
  unseeded bounds while its geometry comes from its seed (`nature.rock` and
  `nature.bush` derive their size, 0.6 - 1.5 and 0.6 - 1.4 module, from
  it), Module Snap moves each position in xz after the spacing test, and
  Surface Orient tilts a piece, which makes it wider in plan (plan step 59
  is spacing by the seeded brush's footprint);
- `yaw_min`, `yaw_max` (degrees, default 0 - 360, within -360 - 360 with
  `yaw_min` <= `yaw_max`, refused otherwise): each piece turns about the
  node's up axis by a seeded angle in the range.

The points and their items come from the layout seed (`Kit_layout.seed`, the
Layout Seed row) whether Per Piece Seed is on or not: `expand_repeat()` throws
darts from one candidate stream, `Kit_random(seed, "layout scatter")`, each
candidate four draws whether taken or not: three exact 24-bit draws (x, z,
yaw) and then a 32-bit draw that picks its item (`pick_scatter_item()`). The
pick is an integer computation: with item weights w_0 ... w_n-1 summing to
W, target = (draw * W) >> 32 in 64 bits, and the item is the first whose
running sum of weights passes the target, so item k is picked with odds
w_k / W and the pick is the same on every platform. The item is picked
before the spacing test, which needs its footprint. A disc's candidate
outside it and a candidate whose footprint comes closer than the spacing to
a taken piece's are skipped (the spacing grid's cells are a little wider
than two of the widest footprints of a picked item plus the spacing, so
every pair that could be too close is compared, and the grid and the taken
points are leaf scratch that keeps its capacity); the scatter stops at the count or after 64 candidates per piece
asked for, holding the points that fit, and the run reports both (`count`,
`requested`; the Run row "n of m pieces fit"). The points in the node's
frame are integer draws and single float operations, each its own statement,
which the fp-contract policy of `doc/cmake_conventions.md` keeps unfused, so
they are the same bits on every platform; the yaw's rotation uses the
platform's sine and cosine, and the world pose goes through the node's
quaternion rotation, as for every kind. The stream depends on neither count
nor spacing: a raised count keeps the first pieces (the generation keeps
them as matching children), another seed places all of them elsewhere. A
region size change scales every point and so moves every piece; a spacing
change decides every candidate again, so the pieces after the first
candidate whose decision changes move; in density mode a region size change
changes the count as well; a weight change picks other items and so, with
their footprints, decides every candidate again from the first candidate
whose decision changes. Each generation samples once: the resolve that
counts the points hands them to the generation, each pose with its item
(`Repeat_pose::item`). With Per Piece Seed on element i's parameters
derive from `derive_layout_element_seed(seed, i)`, a stream apart from the
points. The run's `space` is the region's area.

Item weights (`Kit_layout_item.weight`) are integers in 0 - 1000
(`c_max_scatter_weight`), default 1; weight 0 is an item a scatter never
picks. A scatter whose every item has weight 0 is refused ("every item's
weight is 0"), and so is a sum of weights above 2^32 - 1. The other kinds do
not read the weights: element i takes item i modulo the item count.

A scatter in a file written before item weights (its points from three
draws per candidate, spaced between piece centers) loads as it was saved:
loading regenerates no layout ("Persistence": a load gives the pieces their
brushes, no layout makes children), so a locked or unlocked scatter keeps
its children as saved, and its item texts read with weight 1. A locked one regenerates by the current
rule at its next generation (a value edit, Refresh, a lock), which places
other points; an unlocked one changes no child until it is locked.

"Over a surface" is the layout's Surface Snap and Surface Orient: each piece
snaps to the nearest place within the Surface Max Distance of its point (a
scatter over a hill), else stays at its point. Make Layout from a
piece centres the region on the piece, which is replaced by the scatter.
Make Layout and Place As Layout give the new node's one item weight 1; the
Level Kit window holds no item rows, and an item's Weight is edited in the
Properties window ("Kit layout rows") or over MCP.
Every piece has scale 1 (per-piece scale is plan step 39).

## Groups

A group is an ordinary empty node (an `erhe::scene::Xform`) that is the parent
of its pieces (R5.6, D3.8); groups nest, and a level saves and loads its groups
as the glTF node hierarchy. Every group tool is one undo entry.

- `level_group.hpp` is the scene-free part, built into `editor_level_tests`:
  `mirror_pose(pose, plane_point, plane_normal, symmetry_planes)` and
  `get_group_origin(bounds)`, the center of the bounds' base.
- `Level_kits::group(scene_root, nodes, name)`: the given nodes of one scene,
  of which a node whose ancestor is also given goes with that ancestor, get a
  new group node named `name` at the base center of their combined world
  bounds (the bounds of the subtrees' meshes; of the nodes' origins when there
  is no mesh), unrotated. It goes under the nodes' common parent at the first
  node's place, else under the scene root. The nodes keep their world
  transforms. No nodes, a node listed twice, the scene root, a node of another
  scene and a node inside a reference instance are refused.
- `Level_kits::ungroup(scene_root, group)`: the group's children move to the
  group's parent, after its other children, keeping their world transforms,
  and the group node is removed; undo gives both back. Only an `Xform` is a
  group node.
- Moving a group is a transform of the group node, which the pieces ride
  with: the Transform tool, `set_node_transform` and `transform_selection`,
  each one entry. The surface options snap moved mesh nodes to the surface and let a node
  with a moved ancestor ride with it ("Transform tool"), so a dragged group
  node, which has no mesh, moves its pieces rigidly. Deleting a group is Edit > Delete
  or `delete_nodes` on the group node: the subtree in one entry, and undo
  gives back the same pieces with their records, meshes, bodies and
  collision shapes.
- `Level_kits::duplicate(scene_root, node, copy)`: a copy of the node's
  subtree, nested groups included, after the node under its parent, named
  `copy.name` or the node's name followed by " Copy" (the nodes below keep
  their names), at the node's world transform moved by `copy.value`
  (`Kit_copy_placement::offset`) or with its origin at `copy.value`
  (`position`). The copy is the node's `clone()`, so every copied piece keeps
  its record, shares the mesh of its brush and has a static body; before the
  insert `set_copied_brush_collision_shapes()` gives each brush instance the
  collision shape of its brush (`doc/editor/brushes.md` "Copies"). A copied
  piece is an ordinary piece: a parameter change of the copy leaves the
  original alone.
- `Level_kits::mirror(scene_root, group, axis)`: every node below the group
  node is reflected across the plane through the group node's origin whose
  normal is the group's local `Mirror_axis` x, y or z; the group node stays.
  No node gets a negative scale: a rigid body takes only its node's
  translation and rotation, so a reflected transform would leave a piece's
  body unmirrored. The position is reflected, and the rotation is the
  reflection composed with a reflection in the node's own frame across a
  plane its shape is symmetric about, which is a rotation and gives the exact
  mirror image. A shape declares those planes in
  `Kit_shape_definition::mirror_planes` (`Kit_mirror_plane` x, the plane
  x = 0, or z): x and z for the block, floor slab, wall, door wall and arch,
  x for the wedge and the stairs, z for the cylinder and the pillar, none for
  the seeded nature shapes. Of the declared planes the one whose normal is
  closest to the mirror's is used, so a block mirrored across its own x axis
  only moves, a wedge mirrored across z turns half a turn about its up axis,
  and mirroring twice gives the original. A piece whose shape declares no
  plane and a mesh that is not a piece use their local x plane and are
  reported as not exact (`Mirror_image::not_exact`); a node without a mesh
  uses local x too and has no image to be exact about. Each node's local
  transform is set from its parent's new world transform.

## Kit layout nodes

A kit layout node is a run of pieces kept as one node of the scene graph, the
pieces its children (R5.12, D3.9). It carries the attached property group
`Kit_layout` (`level/kit_layout.hpp`, the pattern of `Kit_piece`), so its values
ride the node's `ERHE_node` properties like a piece record. A line, grid, ring or
scatter layout is an ordinary `erhe::scene::Xform`; a path or segments
layout is an `erhe::scene::Curve` prim, the curve its pieces follow (R5.12.8,
"Path layouts", R5.12.10, "Segment layouts"), and so is a stairs layout,
whose one child is a staircase generated along the curve (R5.12.13, "Stairs
layouts"). Locked, the node makes its children from its values and they are not
edited directly; unlocked, it leaves them alone and they are ordinary pieces.

- The kind and the node type agree (`check_layout_kind()`,
  `is_curve_layout_kind()`): `path`, `segments` and `stairs` only on a Curve prim,
  `line`, `grid`, `ring` and `scatter` only on an Xform. A change among path,
  segments and stairs is made in place; a change between a curve kind and the others
  is refused naming the node type (it would replace the node), so the
  Properties window's Kind row offers the kinds of the node's type; a node
  whose stored kind is not of its type does not read ("Values that do not
  read" below).

- The values (`Kit_layout_data`): `Kit_layout.pattern`, the canonical text of
  a `Kit_layout_pattern`; `Kit_layout.items`, the canonical text of the
  `Kit_layout_item` list; `Kit_layout.module_snap`, `surface_snap`,
  `surface_orient` and `surface_max_distance` (meters, 1 when a saved node
  has no value; a negative or non-finite one makes the values not read), the snap settings applied to every element as a placement
  applies them (`Kit_snap`, `Surface_options` with the downward direction);
  `Kit_layout.per_piece_seed` (its former name `Kit_layout.random_seeds`
  still reads, in scene files and property writes) and `Kit_layout.seed`;
  `Kit_layout.locked`; and the seven connector switches of a path or
  segments layout ("Layout connector switches"; a write of one changes no
  child, so it adds no generation). The
  pattern text is the key value: a node carries the group while it holds one.
  The Properties window edits them in its "Kit Layout" group ("Kit layout
  rows"); the raw rows are developer-only. A write of a
  `Kit_layout` value outside the entry points below (a Properties row,
  `set_item_property`) is a `Property_set_operation` whose follow-ups
  (`Level_kits::append_layout_value_follow_ups()`) are the generation a
  locked node's new values need, so the write and its regeneration are one
  undo entry, as `level_set_layout` makes them; writing `locked` true locks
  and false unlocks as `level_set_layout_lock` does. A write the values
  cannot take (a text that does not read, a pattern the generation refuses)
  (and a kind not of the node's type) is taken back and applies nothing.
  `set_item_property` and the Properties window's rows check it first (`check_layout_value_write()`), so it records
  no undo entry: `set_item_property` refuses it naming `level_set_layout`,
  and a row takes its live edit back and logs the refusal
  (`Dependency_property_rows::refuse_layout_write()`). Both refuse an
  expression and a cleared value of the group. Every operation that writes a
  `Kit_layout` value (and its undo and redo) queues
  `Kit_layout_changed_message` (`App_context::on_item_property_changed()`),
  on which the Properties window's "Kit Layout" group reads the node's values
  again. The entry
  points write their values with `Property_set_follow_ups::none`, adding the
  generation themselves.
- `level/kit_layout_values.hpp` is the scene-free part, built into
  `editor_level_tests`. Both texts are JSON with sorted keys and hexadecimal
  float strings (`format_hexfloat()`), so equal values give equal text; a
  text with a missing, unknown or mistyped key does not read, except the
  scatter keys, which a text written before scatter lacks and which then
  read as their defaults.
  - `Kit_layout_pattern` holds every kind's values, so switching the kind and
    back keeps them: `kind` (line, grid, ring, path, scatter, segments, stairs), `count_mode`
    (`Kit_layout_count_mode::count` or `gap`), `count`, `gap`, the line's
    `direction`, `step` and `length`, the grid's `count_x`, `count_z`,
    `spacing_x` and `spacing_z`, the ring's `radius`, `arc_degrees` and
    `facing`, and the path's and segments' `orientation` (`Repeat_path_orientation`,
    `upright` or `tangent`), the scatter's values ("Scatter layouts") and the
    stairs' values and material name ("Stairs layouts"; written into the text
    only when one is off its default, so a text written before them stays the
    canonical text of its values).
    Segments use `orientation` alone; their stored `count_mode`, `count` and
    `gap` are kept and not used.
    Every kind is in the node's own frame: element 0
    is at the node's origin with its rotation (a path: at its curve's start;
    a scatter's region is centred on it),
    so moving or turning the node moves the run. A pattern text of a kind that
    no longer exists (the former `polyline` with its `points`) does not read.
  - `Kit_layout_item`: a shape id, the canonical parameter text of its values
    (D1.3), a material name per slot, an empty name taking the slot's
    default, and the `weight` a scatter picks it by ("Scatter layouts"),
    written as the key `weight` only when it is not 1, so an item text written
    before weights reads with weight 1 and stays the canonical text of its
    values. The parameter text's `derive` list is the item's seed mask
    (R3.9). In a line, grid, ring, path or segments layout element i takes item
    `get_layout_item_index(i, n)` = i modulo n.
  - `resolve_layout_pattern(pattern, seed, items, path_table, out_run)`
    (the seed: the layout seed, a scatter's point stream; `items` a
    `Kit_layout_item_sizes`: the largest item extent along x and per item its
    weight, footprint radius and x bounds, which `Level_kits::get_layout_item_sizes()`
    reads from the items' brushes; the run refers to them) turns
    the pattern into a count mode `Repeat_pattern` that `expand_repeat()`
    expands from the node's origin ("Pattern expansion"), a path's and a
    segments run's along `path_table` (the node's own curve table, which the
    run refers to; null for the other kinds), and reports the element count,
    the step and the space (`Kit_layout_run`; for segments the count is the
    segment count, the step 0 and the space the sum of the spanned chord
    lengths, plan lengths under `upright`). In count mode a line is `count` elements `step` apart
    along `direction`, a ring `count` elements over its arc and a path `count`
    elements spread evenly over the curve: an open curve holds the first at
    its start and the last at its end (length / (count - 1) apart), a closed
    curve spreads them around without one twice at the seam (length / count
    apart). A grid always states its counts.
  - Gap mode (R5.12.7) states the free distance between neighbours and the
    space determines the count. The step is the item extent plus the gap; the
    item extent is the largest extent along x of the layout's items (the
    bounds of each item's brush), so a run of several items stays regular.
    The space is the line's `length` from the first center to the last, the
    ring's arc length at its radius and the curve's length (the node keeps
    scale 1, so its frame is in meters). A
    line or a path along an open curve holds floor(space / step) + 1 element
    centers spread evenly from the start to the end of the space, both ends
    holding one; an open arc does the same, and a full ring or a closed curve,
    which closes, holds floor(space / step) elements spread evenly around.
    The remainder is spread over the gaps, so the stated gap is a minimum,
    and an exact fit (within 1e-4 of a step) counts. A space shorter than one
    step holds element 0 alone. The gap of a ring and of a path is measured
    along the arc and the curve.
  - Refused with a message naming the value: a determined or stated count
    above `c_max_repeat_count` (1024) or below 1, a gap that leaves no
    positive step, a negative length, a zero direction, a zero step with more
    than one element, a radius that is not positive, an arc outside
    (0, 360], a path or segments layout without a curve, a curve of no length
    holding more than one element, and every refusal of `expand_repeat()`.
  - `transform_curve_path(path, transform, out_path)` takes every control
    point and handle end of a curve through a transform; `mirror()` reflects
    a curve's points with it.
  - `set_layout_spacing_from_extent(pattern, extent)` gives the line's step
    and the grid's spacings of a contiguous run of an item with that bounds
    extent (x for the step and `spacing_x`, z for `spacing_z`, 1 m for an
    extent of 0); `level_create_layout` and Make Layout start from it.
  - `derive_layout_element_seed(layout_seed, i)` is the seed of element i
    with per-element seeds: a hash of the layout seed and the index through
    the integer stream of D1.4, the same on every platform.
- Generation: `Level_kits::build_layout_generation()` reads the items
  (`read_layout_item()`: known shape, readable and valid values, materials of
  the scene or the palette), checks the kind against the node's type,
  resolves and expands the pattern (a path along the node's own curve), and
  makes one `Kit_placement` per element at the node's world transform times
  the element's pose (`Kit_placement::scale` the pose's scale), the module snap and then the surface options applied
  with the node's current children excluded from the rays, each element
  snapping by its own brush's bounds. With per-element seeds
  the element's values take `derive_layout_element_seed()` as their seed and
  derive their masked parameters from it, so the run's derived values are
  stable across regenerations; a segments element then spans its chord by
  the x bounds of its own seeded geometry. Child i is kept when it already is element i:
  a piece without children whose record (shape and parameter text), slot
  materials by name and local pose (translation, rotation and scale) equal the element's. The other
  children are removed with their subtrees, last child first, and the
  missing elements are placed at their indices through the placement path
  of `place()` (`prepare_placements()`), named after the shape and the
  element number from 1. A node whose world scale is not 1 is refused.
- Every edit is one undo entry, and the generation runs only from these:
  - `create_layout(scene_root, Kit_layout_create)`: the node at a world
    position and rotation under a parent (the scene root by default) and its
    generated children, locked or unlocked as the values say. A path layout
    is a new Curve prim with `Kit_layout_create::path`, else a straight open
    curve of two corner points along its x axis, as long as the pattern's
    count and step make a line (count - 1 steps, at least one; in gap mode its
    length). A segments layout is a new Curve prim with the path, else two
    corner points on its x axis at the first item's `min_x` and `max_x`, so
    its one element stands at the node's origin at scale 1. A path given for
    another kind is refused.
  - `make_layout_from_piece(scene_root, Kit_layout_from_piece)`: a node in a
    placed piece's place - under its parent at its index, at its world
    translation and rotation, named after it followed by " Layout" unless a
    name is given - with the piece as the one item
    (`read_piece_layout_item()`: its shape, its record's parameter text with
    its seed mask, and the name of each primitive's material), the given
    pattern, snap settings, seeds and lock; a path or segments layout is a
    Curve prim of the given path, else straight along the piece's x axis as
    above. One
    `Compound_operation`: the piece's remove, the node's insert and its
    generated children, the piece
    excluded from the surface rays, so element 0 takes the piece's place
    rather than standing beside it and undo gives back exactly the piece
    (the same node with its record, mesh, materials and body). A piece with
    children, a scaled piece, a piece inside a reference instance or below a
    locked kit layout node and a piece whose primitive count is not its
    shape's slot count are refused.
  - `set_layout(scene_root, node, data)`: the changed values, one
    `Property_set_operation` each, and for a locked node the generation in
    the same entry, keeping the children that match. An unlocked node takes
    the values alone; they are checked as a generation reads them. The lock
    is not changed here; a kind not of the node's type is refused. No entry
    when nothing changes.
  - `set_layout_lock(scene_root, node, lock)`: unlocking writes the lock
    value alone and changes no child (R5.12.5). Locking writes the value and
    replaces every child with the generated ones, which discards the edits
    made while unlocked; the remove operations hold the user's children
    themselves (records, meshes, collision shapes, bodies), so the lock's
    undo puts exactly them back and unlocks, and redo locks again with the
    same generated nodes (R5.12.6).
  - `refresh_layout(scene_root, node)`: a locked node's generation, keeping
    the children that match, no entry when all match; an unlocked node is
    refused. A level settings change (module, wall height) does not
    regenerate: the item values are stored resolved, and the explicit
    refresh applies a new module to a module-snapped layout.
  - `make_layout_on_curve(scene_root, curve, data)`: a plain Curve prim (no
    children, no layout values, not inside a reference instance or a locked
    layout) becomes a path layout in place: the values, one
    `Property_set_operation` each, and a locked layout's generated children,
    one entry. The kind must be path or segments. Taking the values off a layout node is
    not offered: an unlocked layout's children are ordinary pieces already.
  - `wrap_with_path(scene_root, nodes)`: one path layout in the place of
    the first of several pieces, the pieces its items ("Wrap with Path").
  - `add_layout_of_kind()` and `make_initial_layout_on_curve()`: the Create
    menu's layout entries and the Level Kit window's Add and Make Layout On
    Curve buttons ("Create menu", "Level Kit window").
  - `make_layout_from_piece_initial()` and `add_layout_initial()`: Make
    Layout and the Brush tool's Place As Layout with the initial values of a
    new layout node ("Level Kit window").
  - The end of a move: `append_layout_move_operations()` adds the generation
    of every locked layout node at or below the moved nodes whose surface
    snap or orient is on to the move's own entry, for the Transform tool's
    drag end (`record_transform_operation()`, which `transform_selection` and
    `drag_selection` share), `set_node_transform`, and a write of a node's
    `translation`, `rotation` or `scale` through the property system
    (`append_layout_transform_follow_ups()`, one hook per scene for the
    written nodes): a Properties transform row ("Live layout preview"), Paste
    Properties (`Property_set_apply_operation`) and `set_item_property`
    (`Property_set_operation`). Without a surface
    setting the children of every kind, a path's included, ride with the node
    rigidly and nothing regenerates.
  - A curve edit: see "Path layouts" (a segments layout follows its curve the
    same way).
- A locked node's children cannot be edited directly (R5.12.3):
  - Selection (U24): `Selection` never holds a child of a locked layout
    node: `get_selectable()` puts the outermost locked layout node above it
    in its place on every way into the selection, a selection an operation
    puts back on undo or redo included (`doc/editor/selection.md` "What can
    be selected"; the insert and remove selection rules are in
    `doc/editor/operations.md`). That covers a viewport click, the
    hierarchy window's click, shift range (`Range_selection::end()`) and
    Ctrl+A select all, and `select_items` (whose reply lists the items now
    selected). So every
    selection-driven tool (the
    Transform tool and its gizmo, `transform_selection`, `drag_selection`,
    the Properties window, the Level Kit window's piece rows, Edit > Delete,
    cut, copy, duplicate) works on the layout node. A write of
    `Kit_layout.locked` true (a lock, a raw write, the undo of an unlock)
    hands the selected children's place in the selection to the node in
    `App_context::on_item_property_changed()`; a lock's value is the first
    operation of its entry, before the regeneration replaces those children.
    The undo of a lock leaves the layout node selected, not the children
    selected before it: the removals' undo puts the pre-lock selection back
    while the node is still locked (its value is undone last), so it
    redirects. Redo selects the node again. A Debug build's
    `Selection::sanity_check()` reports a selected child of a locked layout.
  - Structure: `instance_structure_refusal()` and `instance_child_refusal()`
    (`prefabs/instance_structure.hpp`) and their bool forms refuse an item
    below a locked kit layout node and an insert under one
    (`locked_layout_refusal()`, `locked_layout_child_refusal()`), so delete,
    reparent (the item tree's drag and drop, `reparent_item`), paste into the
    node (`clipboard_paste`, the item tree's Paste and Create entries, and
    `Clipboard::try_paste()` for the Paste command), `create_node` and the
    other insert tools, and the group tools refuse it.
  - Values: `Level_kits::set_parameters()`, `preview_parameters()` and
    `set_slot_material()` refuse a child, and so do `set_node_transform`,
    `level_snap_to_surface` (unless `query_only`), `assign_mesh_material`,
    `set_item_property`, `set_item_style`, `clear_item_style`,
    `set_item_flags`, `add_tags`, `remove_tags`, `lock_items`,
    `unlock_items`, `physics_drag`, `apply_physics_force`,
    `create_physics_body`, `edit_physics_body`, `create_joint`, `edit_joint`,
    `navigation_set_seed`, `set_node_graph_mesh`, `generate_lods`, the
    animation keying tools, the mesh component selection tools, every
    geometry tool that takes node ids (`Mcp_server::run_geometry_op_with_target()`),
    `loop_cut_mesh`, `knife_cut_mesh`, `set_mesh_attribute_values`,
    `set_edge_sharpness`, `ik_drag` and `csg` with a child as target or as a
    consumed tool. `merge_static_subtree` refuses a subtree that holds a
    locked kit layout node, and `attach_selection_to_active` (and the Attach
    command) refuses a locked layout node as the parent.
  - Geometry operations of the Operations window refuse a locked kit layout
    node and its children among their items (`resolve_operation_items()`),
    and the mesh component selection tool does not pick a child's mesh.
  - Item tree: a child's row offers no Rename (and a rename commit is
    refused), its Cut, Duplicate, Paste and Create entries are disabled and
    its node menu shows "Locked by its kit layout" in place of the node
    edits; Delete and drag and drop refuse through the structure predicates.
    The recursive Lightmap and No Transform Update entries on the layout
    node's own row reach its children through the layout node, which is an
    edit of the layout, not of a child.
  The message names the node and the layout ("'Block 2' is a child of the
  locked kit layout node 'Wall run'; edit the layout's values, or unlock it to
  edit its children"), and the refused call changes nothing. The physics
  tool grabs no static body, so it never moves a piece.
- Groups: an Xform kit layout node is a group node as well, so `level_ungroup`
  of it leaves its children as plain pieces; a path layout, a Curve prim, is no
  group node. `duplicate()`, a copy and paste and a delete with its undo work on
  the one node through the ordinary node paths: the copy is a kit layout node of
  the same type with its values, its own copied children and, for a path, its
  own copy of the curve, with the same lock state. `mirror()` moves a locked
  layout node as a unit (its children ride with it and are not listed) and
  reports it as not exact, because the run keeps its own direction in the
  node's frame; a curve's points are reflected across its local x = 0 plane in
  the same entry (a `Curve_set_operation`), so its shape is the exact mirror
  image and a locked path layout is laid out along it again.
- Path layouts (C6, C7, D4 of `doc/plans/curves.md`, R5.12.8): a path layout
  is one node, a Curve prim that carries the `Kit_layout` values and has the
  pieces as its children, so the author selects one node to edit the curve's
  points and the layout's values. A polyline is a curve of corner points.
  - The run is the curve in the node's own frame: the generation expands along
    the curve's own table (`Curve::get_table()`), the step and the gap in
    meters (the node keeps scale 1), and the pieces' local transforms are
    their poses on the curve. Moving or turning the node moves the curve and
    the run rigidly with it, as for the other kinds.
  - Selecting a piece of a locked path layout selects the node (the
    redirect below); with that one node selected the curve tool shows and
    edits its points and handles (`doc/editor/curves.md` "Viewport editing")
    and the Properties window shows the "Curve" group and the "Kit Layout"
    group. Unlocked, the children are ordinary pieces again and the curve is
    still edited on the node.
  - Change-driven (C7): every edit of the curve's path is one
    `Curve_set_operation` (MCP `set_curve`, `curve_add_point`,
    `curve_remove_points`, `curve_set_point_kind`, the curve tool's commits -
    Ctrl+click insert, Delete, the kind and closed commands - and its release
    of a drag, the Properties Curve rows), whose first execute records the
    generation of the curve when it is a locked path layout
    (`Level_kits::append_curve_follow_ups()`) and runs it; redo runs it again,
    undo undoes it before the path, so one undo restores the curve and the
    pieces. Nothing polls a curve. During a drag of a curve point (the curve
    tool's gizmo drag, a Properties row drag) the layout follows live ("Live
    layout preview"): `begin_layout_preview()` at the press, a step at each
    shape `Curve_changed_message` of that curve, `end_layout_preview()` at
    the release, after which the release's `Curve_set_operation` records the
    generation from the children before the drag, one entry. A curve edit the
    new values refuse (more than 1024 pieces) leaves the children and logs a
    warning. An unlocked layout is not laid out by a curve edit.
- Segment layouts (U37, R5.12.10): a segments layout is a Curve prim like a
  path layout - one node carrying the values with the pieces as its children
  and its own curve - that puts one piece on each segment between two
  neighbouring control points, scaled along the piece's x axis alone so that
  its item's x extent fills the chord ("Pattern expansion" has the pose); a
  wall's length follows its room's corner points. Everything "Path layouts"
  states holds for it: the curve tool shows and drags its points and
  handles (a handle changes no piece), each curve edit is one
  `Curve_set_operation` whose follow-up lays the layout out again (only the
  pieces of the changed segments are new nodes; the others match and are
  kept), a point drag previews live, a move of the node carries the pieces
  rigidly. Points the curve tool and `curve_add_point` add between corner
  points are corners. Each segment is a handle as well: a drag moves a wall
  along its normal ("Segment handles").
  - The pieces carry their scale as their local scale (s, 1, 1); the layout
    node itself keeps world scale 1. Module snap is not applied (the pieces
    stand on the control points) and neither is surface orient (it would tilt
    a piece off its points); surface snap moves each piece along the
    downward direction by the bounds of its scaled geometry, which keeps its
    ends above its points in plan.
  - A piece's collision shape is its brush's shape at the node's world scale,
    as every body's is (`doc/editor/physics.md` "Collision shapes follow the
    node's scale"), so the body, the overlap report's exact depth (and its
    convex hull stand-in, scaled the same) and the physics export match the
    scaled mesh.
  - Connectors follow the scaled node's world transform (U22); duplicate,
    copy and paste, delete, mirror and the selection redirect treat the node
    as they treat a path layout.
- Live layout preview: while a drag changes what a locked layout's children
  depend on, the children follow at every step without an undo entry, and
  the gesture's commit records one entry from the children before the drag
  (`Level_kits::begin_layout_preview()`, `step_layout_preview()`,
  `preview_layout_values()`, `end_layout_preview()`). Five drags drive it:
  a curve point drag of a path or segments layout ("Path layouts") or a
  segment handle drag ("Segment handles"), a drag of a value
  row of the Properties window's "Kit Layout" group ("Kit layout rows"), a
  Transform tool drag of nodes at or above locked layouts with surface
  snap or orient on (the set `append_layout_move_operations()` regenerates,
  `begin_layout_move_preview()`; every step of a translate, rotate or scale
  drag runs it after the connector snap, the box scale's per node path
  included), a drag of a node's Translation,
  Rotation or Scale row in the Properties window's "Local Transform" group
  (the same set, `begin_layout_transform_preview()`), and a drag of a
  scatter's region handle ("Scatter region handles").
  - A step undoes the previous step's generation, so the layouts stand with
    the children from before the drag, runs one scene transform pass (a
    moved node's descendants take the move), builds every layout's
    generation from those children with the layout's live values, transform
    and curve - as the release builds it, every generation built before any
    runs - and executes them. Pieces are added and removed as the count
    changes; module snap, surface snap and orient and per-element seeds
    apply per piece (a piece snaps by the bounds of its own brush), the
    previous step's pieces excluded from the rays; a
    piece whose pose does not change keeps its node (the keep-matching rule).
    So a step shows exactly what the release records.
  - A value drag writes the values on the node (without an operation) only
    when they lay out, remembering the values from before the drag, so
    `level_get_layouts` and the group's Run line report the previewed run. A
    value the layout cannot take keeps the last valid step and its values;
    the refusal shows in the group's Message row and as an `editor.scene`
    trace line "keeps its last previewed children".
  - The preview keeps the selection: the generation's operations set the
    selection they were made under, so a step and the end restore the one
    standing.
  - The end undoes the last step and gives back the remembered values, so the
    layouts hold the same child nodes (ids, transforms, records, brushes,
    bodies, raytrace instances) and values as before the drag. It runs at the
    gesture's commit (`set_layout()`, `set_layout_lock()` and
    `refresh_layout()` end a preview of their node first; the curve tool and
    the Transform tool end it before they record), before
    `Operation_stack::undo()` and `redo()`, and, queued out of the message's
    dispatch, when a previewed node that was selected at the start leaves the
    selection or a previewed node is removed. A closing scene drops the
    preview without a restore.
  - The Transform tool begins the preview at the first node write of an edit
    (a gizmo drag, `drag_selection`, the Transform window's rows,
    `transform_selection`), steps it after each write
    (`Transform_tool::follow_layout_preview()`) and ends it in
    `record_transform_operation()`, whose move hook then records the
    regeneration in the move's entry. A layout without a surface setting
    rides rigidly and is not previewed. A cancelled node drag
    (`Transform_tool::cancel_node_drag()`, `doc/editor/transform.md` "Drag
    cancel") ends the preview before it restores the moved nodes, so the
    layouts stand with their children from before the drag and nothing is
    recorded.
  - A transform row drag begins the preview at the row's first write
    through the Properties window's edit hook
    (`doc/editor/properties_window.md` "Where rows come from") when the
    edited nodes are in one scene (a selection across scenes is not
    previewed and regenerates at the release alone), steps it after each
    write and ends it when the row deactivates. The release then records the
    rows' writes and one move hook for every edited node in one entry, built
    from the children before the drag as a step builds them. A drag the
    window gives up (a selection change, the window hidden) restores the
    nodes' transforms and their children and records nothing.
  - A step logs the `editor.scene` trace line "layout preview step: N
    layouts, M pieces: T ms". Release build: a value drag of a 12-pillar line
    (each step a new pillar brush) about 1.7 ms, a Transform drag of a
    20-stone surface-snapped path about 0.45 ms. Nothing runs while no
    preview is live; the entries, the excluded meshes and the held selection
    are scratch members of `Level_kits`.
- Values that do not read: a node whose pattern or items text does not read,
  or whose stored kind is not of its type (an Xform carrying a path pattern -
  the form saved before a path layout became its curve node - or a Curve
  carrying a line, grid, ring or scatter), is reported by `level_get_layouts` with a
  `problem` naming it and by the Properties window's "Kit Layout" group as
  its Problem row; `set_layout()`, `set_layout_lock()` and `refresh_layout()`
  refuse it naming the problem, and its children stay as saved. Such a level
  is rebuilt (a saved `Kit_layout.curve` of the former form is reported at
  load as a property the node does not have).
- A level saves its kit layout nodes with their values and their children as
  ordinary pieces (R8.1); at load the children are taken as saved and follow
  the kit as other pieces do ("Persistence"), and nothing regenerates. The
  level check, support and overlap reports, region contents, height map and
  navigation input read the children as the pieces they are.

## Kit layout drawing

Line, grid, ring, scatter and segments kit layout nodes draw their shape in the
viewports (U28, `level/kit_layout_outline.hpp`,
`level/kit_layout_indicators.hpp`,
`Debug_visualizations::kit_layouts_visualization()`), flat in the node's xz
plane (segments: through the curve's points) and taken through its world
matrix, x-ray as the curves are. A path
layout draws its curve ("Drawing" in `doc/editor/curves.md`) and nothing of
its own. The outline comes from the node's values and the run they resolve
to (`Kit_layout_run` and its poses), so the ticks and points sit where the
pieces are generated, before Module Snap and Surface Snap move them:

- line: the segment from the first to the last element center, and a tick
  per element across it in the node's xz plane (a line along the node's y
  axis ticks along z);
- grid: the closed outline of the elements' cells, half a spacing beyond the
  outer centers (`count_x` times `spacing_x` by `count_z` times
  `spacing_z`), with a tick per column on its -z edge and per row on its -x
  edge, each across the edge;
- ring: the circle, or the `arc_degrees` arc from element 0, of `radius`
  about the center `radius` behind the node (along -z), with a radial tick at
  each element;
- scatter: the region outline centred on the node, the `size_x` by `size_z`
  rectangle or the `region_radius` circle. For a scatter that is selected
  (the node or a node below it), per generated point two circles about the
  point's vertical axis: the footprint circle, of its item's footprint radius
  ("Scatter layouts"), and the spacing circle, of that radius plus half of
  `min_spacing`. Two points exactly the minimum gap apart show touching
  spacing circles, and accepted points' spacing circles never overlap. An
  unselected scatter shown by All draws its region alone, which keeps a view
  of many large scatters to their outlines;
- segments: the chords through the curve's control points (closed for a
  closed curve) and a tick at each point across the bisector of the unit
  headings of the segments beside it, so a smooth curve's own drawing and
  its straight pieces' chords show apart. The outline is made from the
  node's curve table (`make_layout_outline()` takes it), and the cache drops
  a node's entry at each shape `Curve_changed_message` of it.

- Which: the Debug Visualization popup's Kit Layouts (per view, saved with
  the view's settings): Off, Selected (the default: the kit layout nodes that
  are selected or have a selected node below them) or All (every kit layout
  node of the view's scene). A node is drawn only where a generation of its
  values would stand: the outline is made by the generation's own first part,
  `Level_kits::prepare_layout_generation()`, which refuses in the
  generation's order (items read with their materials, item geometry, kind,
  run, scale 1, module snap, per-element seeded values), and a node whose
  world matrix does not keep scale 1 (`is_layout_scale_kept()`, checked as it
  is drawn, as the cache holds no transform) is not drawn. A refused layout,
  locked or unlocked, is not drawn and not listed over MCP. A brush that makes
  no renderable mesh is found by a generation only.
- Item sizes: the outline, the scatter sampler and every other reader of an
  item's size (the Run row, the MCP `items` reply) take the item's bounds
  from its CPU geometry, `get_kit_geometry_bounds()` (`level/kit_shape.hpp`):
  the min and max of the generator's facet corner points, with no brush, no
  mesh memory and no upload. They are bit for bit the box a brush of the
  values reports, so the outline's circles and the generation's spacing use
  the same footprint radius. A generation makes the brushes its pieces need
  after the preparation.
- Unlocked value drags (the orchestrator's choice, for the user's review):
  an unlocked layout's values are written at the release and its children
  stay as they are, so during a value drag in the Properties window the
  outline shows the pending values instead (`Kit_layout_indicators::
  set_pending_values()` at each drag step); values the generation refuses
  keep the last shown outline. The release, Escape and a deselection end it,
  and the node's values are drawn again.
- A value edit whose widget stops being drawn mid-edit (the Properties window
  hidden or closed, the Kit Layout group collapsed, the row no longer shown)
  ends as a cancel: ImGui drops the widget's active id and reports no end, so
  `Kit_layout_editor::end_lost_edit()` checks, after the Properties rows are
  drawn, that the open edit's widget is still the active one, and the
  window's hide path calls `abandon_edit()`. A locked layout's preview gives
  the children and values back and an unlocked layout's pending outline goes,
  with no undo entry.
- Colors and size: Settings > Debug Visualizations Style > Kit Layouts
  (`kit_layout_color` and `kit_layout_width` for the outline and ticks,
  `kit_layout_tick_size` the tick's half length per meter of distance to the
  camera (screen-constant), `kit_layout_footprint_color`,
  `kit_layout_spacing_color` and `kit_layout_circle_width` for the scatter
  circles).
- Cost: the parsed values, the resolved run and the outline are cached per
  layout node and made on the first draw after a change. The cache drops the
  named node's entry on `Kit_layout_changed_message` (an operation wrote one
  of its values: an edit, its undo or redo; the message carries the node's
  id), the node's entry when a live layout preview writes its values and
  when the preview ends and gives them back (so the outline follows a held
  value drag), a removed node's entry, and every entry at a scene close and
  at a write of a scene's Level lengths (the Level Kit window's Module and
  Wall Height, MCP `set_scene_settings`), since item footprints and extents
  derive from the lengths, and at a material entering, leaving or renamed in
  a content library (`Item_insert_remove_operation` of a Material, its undo,
  and a write of a Material's name), since layout items name their slot
  materials (`Level_kits::on_scene_materials_changed()`). The outline therefore shows what the next
  generation places; after a lengths change that differs from the placed
  pieces until the layout is generated again. World matrices are read at
  draw time, so a node move needs no new entry; the drawing's scratch keeps
  its capacity and a steady frame allocates nothing.
- MCP: `level_set_layout_visualization {mode, viewport}` sets the setting
  (off, selected, all); `level_get_layout_indicators {viewport}` returns what
  that viewport draws this frame, collected as the drawing collects it:
  `viewport`, `scene_name`, `mode`, `cached_layouts` (entries of the outline
  cache), `count` and per layout node `id`, `name`, `kind`, `polyline` (world
  points; `closed` joins the last to the first), `arcs` (`center`, `start`
  (the point at angle 0), `normal` (the node's up axis), `radius`,
  `degrees`), `ticks` (`position`, unit `direction`), `circles_shown` and
  `circles` (`center`, `footprint_radius`, `spacing_radius`; radii in the
  node's frame, which keeps scale 1) and `region_handles` (a scatter's:
  `index`, `value`, world `position`; "Scatter region handles").

## Scatter region handles

A selected scatter kit layout node shows handles on its region outline in the
viewport, and a drag of one resizes the region (U28,
`level/kit_layout_region_tool.hpp`). The region tool is a point set tool
(`doc/editor/point_set_editing.md`), its points the handles:

- Which handles: the outline's `Kit_region_handle` list
  (`Kit_layout_outline::region_handles`, made with the outline from the
  values it draws, so the handles sit on the drawn outline and follow a live
  preview and a pending outline). A rectangle has 8: its corners in the
  polyline's order (-x -z, +x -z, +x +z, -x +z), each editing `size_x` and
  `size_z`, then the midpoints of the edges between them (-z, +x, +z, -x),
  each editing the size across it; a disc has 4 on its circle at +z, +x,
  -z and -x, each editing `region_radius`. They lie in the node's xz plane
  and go through its world matrix, so a turned or moved node carries them.
- When they show: while the kit layout node is the only selected item (a
  piece of a locked scatter selects the node), in every viewport of its
  scene, whatever the view's Kit Layouts setting, as the curve tool's
  handles show whatever the Curves setting. A node whose outline is not
  drawn (values the generation refuses, a world scale other than 1) shows
  none; a line, grid, ring or path layout has none. They are drawn and
  picked as every point set's: camera-facing quads of the Point Handles
  style, orange, white hovered, yellow selected, picked within 12 px.
- Selection: a click on a handle selects it; one handle is selected at a
  time (two handles of one size would pull it two ways), and Ctrl or Shift
  on the selected handle deselects it. A plain click off the handles clears
  the handle first, so the next click reaches the object selection. The
  handle stays selected through its drags and their undo and redo; a change
  of the target, or of its region or kind, clears it.
- With the Transform gizmo: with a handle selected the gizmo sits on it in
  the node's frame (Local mode turns with the node) and moves it; with none
  the gizmo is the node's, at its origin. The gizmo's handles win the pick
  over a region handle under them, so a handle on a gizmo arrow (the +x edge
  of an unturned node, below the node's X arrow) is picked where it is clear
  of the arrow or from another view; an unhovered region handle takes no
  click from the gizmo or the object selection.
- The drag: the handle's position at the press moved by the gizmo's
  world-space delta, taken into the node's frame, gives the new values
  (`apply_region_handle()`, scene-free): an edge's size is twice its reach
  from the node's origin along its side, a corner's both sizes, a disc's
  radius its distance from the origin in the xz plane; the node's y is
  ignored. The region stays centred on the node's origin and resizes
  symmetrically about it: the pattern is defined about the origin (a
  scatter's points scale with the region about it), so moving the node to
  hold the opposite edge would also move every piece's frame and make one
  drag two edits. Each size is clamped to 0.01 - 10000 m
  (`c_min_scatter_region_size`, `c_max_scatter_region_size`, the Properties
  rows' range); an edge pulled past the origin holds the minimum. The gizmo
  path's constraints and the Move tool's translate snap (Snap Enable or
  Ctrl) act on the handle's position; the XZ plane handle in Local mode
  keeps it on the node's plane. A rotate or scale drag moves nothing (it
  turns and scales the handle about itself).
- Steps: a step whose values differ from the previous step's goes through
  the path a Properties value drag takes: a locked layout through
  `Level_kits::preview_layout_values()` ("Live layout preview"; the children
  regenerate, in density mode with the count the new area gives), an
  unlocked one through `Kit_layout_indicators::set_pending_values()` (the
  outline shows the pending values, the node and its children stay). Values
  a step cannot take keep the last shown step. Nothing runs while the
  pointer rests.
- The end: the release gives the layout its values and children from before
  the drag back and records the last shown values with
  `Level_kits::set_layout()`, one undo entry (with the regeneration of a
  locked layout), none when the values end where they began; Escape and
  `cancel_component_edit` restore the start exactly and record nothing. A
  drag whose node leaves the selection or its scene is given up: the start
  is restored and the release records nothing; a removed node or a closed
  scene cancels it.
- MCP: `level_select_region_handle {scene_name, layout_id, index}` makes the
  node the only selected item and selects the handle (`index` omitted
  clears it); `level_get_region_handles {}` returns the tool's `layout`
  (`id`, `name`, `scene_name`), its `handles` (`index`, `value` - `size_x`,
  `size_z`, `size_x_z` or `region_radius` - and world `position`),
  `selected` and `gizmo_on_handle`. A script drags a handle with
  `get_transform_handles` and `mouse_drag` as a user does, or moves it
  exactly with `transform_selection` (the anchor's world translation;
  `end_edit` false holds the edit as a held drag).

## Segment handles

A segments layout that is the curve tool's target (the only selected item;
a piece of a locked layout selects the node) has a handle per segment beside
its control point handles, and a drag of one moves that segment parallel to
itself along its normal (U38, R5.12.11). The curve tool
(`tools/curve_tool.hpp`) holds them as further entries of its point set
(`doc/editor/point_set_editing.md`): after the three entries of every
control point, entry 3n + i is segment i. A layout of either lock state has
them; another curve or another kind has none (the tool reads the kind when
its target changes and at each `Kit_layout_changed_message` of it).

- Which handles: segment i runs from control point i to i + 1, the last
  segment of a closed curve to point 0; its handle sits at the chord's
  midpoint. It is drawn and picked as a point handle (orange, white
  hovered, yellow selected, within 12 px), and the hovered and the selected
  segment's chord is drawn over the curve twice the selected curve width,
  in those colors.
- Selection: a click on a segment handle selects it, one segment at a time;
  Ctrl or Shift on the selected one deselects it, on another selects that
  one. Selecting a segment clears the point selection and selecting a point
  or handle end clears the segment, since each drags in its own way. A plain
  click off the handles clears the selection first, as for points. The
  segment stays selected through its drags and their undo and redo; like
  the point selection, its index is kept across point inserts and removals
  and dropped only when it is out of range, or when the target changes or
  stops being a segments layout. Delete (`Curve.remove_points`) with a
  segment selected does nothing.
- The normal: horizontal in the node's frame (perpendicular to the chord
  and to the node's y axis), the right of the segment's direction from point
  i to i + 1 (`get_segment_normal()`, `level/level_repeat.hpp`); a segment
  of no plan length has none and does not drag. With a segment selected the
  gizmo sits on its midpoint, and its Local frame turns x onto the normal
  and y onto the node's up axis.
- The drag: the gizmo's world-space delta moves the midpoint from the
  press; only its motion along the normal counts, so an axis, a plane or a
  free drag gives the same offset for the same motion across the segment,
  and a rotate or scale drag moves nothing. `move_curve_segment()` (scene
  free) gives the two new points: each end point moves onto the moved line
  along its neighbouring segment's line in the node's xz plane (the
  segment before point i, the segment after point i + 1), keeping its own
  height, so every other segment keeps its direction and only lengths
  change. An end point with no neighbouring segment (the first or last
  point of an open curve) or a neighbour that meets the segment at less than
  15 degrees (`c_segment_slide_min_sine`; a slide is then at most about 3.9
  times the offset) moves straight along the normal. Handles of the two
  points are kept as offsets. On a closed curve of two points the two
  segments lie on one line, so both ends move along the normal; their two
  handles coincide and the viewport picks segment 0. The offset is in the
  node's frame, meters as a layout node keeps scale 1. A move that carries
  an end point past its neighbour's far end (that neighbour's length passing
  through zero) gives a curve the layout may refuse; the pieces then stay as
  a refused curve edit leaves them.
- Steps and end: as a point drag ("Path layouts"): the path is written at
  each step that changes it, the layout follows live, the release records
  one `Curve_set_operation` ("move segment i") whose follow-up lays out a
  locked layout again, Escape and `cancel_component_edit` restore the start
  and record nothing; a curve the layout then refuses keeps the pieces and
  logs a warning.
- MCP: `level_select_segment_handle {scene_name, layout_id, index}` makes
  the node the only selected item and selects the segment (`index` omitted
  clears it); `level_get_segment_handles {}` returns the target `layout`
  (`id`, `name`, `scene_name`; null when the curve tool's target is not a
  segments layout), its `handles` (`index`, `points`, world `position` and
  unit world `normal`), `selected` and `gizmo_on_handle`;
  `level_move_segment {scene_name, layout_id, segment, offset}` moves a
  segment by a signed offset along its normal through the same
  `move_curve_segment()`, as one undo entry; it is refused while a curve
  tool drag of the same curve is held. A script drags a handle with
  `get_transform_handles` and `mouse_drag` as a user does.

## Wrap with Path

Wrap with Path (U45) turns placed kit pieces into one path layout ("Kit layout
nodes", "Path layouts") that repeats them along a curve the author then
shapes. The Scene Hierarchy window's context menu offers it on every node row
(`Scene_root`'s item context menu): on a selected row it takes the scene's
selected nodes, on another row the clicked node alone. The entry is greyed
with the refusal as its tooltip when `Level_kits::find_wrap_with_path_refusal()`
refuses the nodes; a child of a locked kit layout node shows "Locked by its
kit layout" in its place. The entry and MCP `level_wrap_with_path` ("MCP")
call `Level_kits::wrap_with_path(scene_root, nodes)`:

- The pieces are taken once each, in hierarchy order (pre-order of the scene
  tree). The layout's items are their distinct items, in that order, each read
  as Make Layout reads a piece (`read_piece_layout_item()`: shape, parameter
  text with its seed mask, slot material names), so equal pieces give one
  item; element i takes item i modulo the item count.
- The node is a new locked path layout, a Curve prim named after the first
  piece followed by " Path", under the first piece's parent at its index,
  at its world translation and rotation. The values: `make_wrap_path_pattern()`
  (`kit_layout_values.hpp`), a path in gap mode with gap 0 and the default
  `upright` orientation, so the run stays contiguous and grows as the curve
  is lengthened; per-item seeds; no module snap, surface snap or orient; no
  connector hidden.
- The curve: two corner points along the node's x axis (the first piece's x
  axis), `get_wrap_path_length()` long: `c_wrap_path_min_count` (4) elements
  of the largest item x extent, or one element per item when there are more.
  Element 0 stands exactly where the first piece stood.
- One `Compound_operation`: the removal of every piece with its kit light
  nodes (last in hierarchy order first; each element makes its own lights at
  the frames' starting values), the node's insert and its generated children,
  the pieces excluded from the surface rays. Undo gives back the same piece
  nodes with their records, meshes, materials, bodies, light nodes (with
  their edits), places and transforms, and the selection from before; redo
  makes the same layout node and children again. The copies carry the
  items alone: node state beyond the kit record (a piece's name, physics
  edits such as a dynamic motion mode, visibility and the other flags, tags,
  styles and other attached properties) is not carried to them, and with
  several pieces the place of every piece after the first is dropped (only
  its item stays); undo gives all of it back. After the wrap the new node
  is the selection, so its curve's points can be edited at once (a redo
  leaves the selection the removals left).
- Refused, changing nothing, naming the node: no node, a node of another
  scene, a node that is not a kit piece (a plain mesh, a group, a kit layout
  node, a light, a camera), a child of a locked kit layout node or a node
  inside a reference instance (`instance_structure_refusal()`), a first piece
  whose parent is an unlocked kit layout node (a lock of that layout would
  replace the nested path layout with its generated children; Make Layout
  from a piece does not refuse this case), a piece with
  children other than its kit light nodes, a scaled piece (a kit layout node
  keeps scale 1), a piece with a connector switched off (a layout item
  carries no connector switches, plan step 60), and a piece whose primitive
  count is not its shape's slot count, an item without extent along x (the
  path's step would be 0) and more distinct items than a layout holds
  (`c_max_repeat_count`). One refused node refuses the whole set.
- The menu reads its nodes and the refusal once, when the menu opens
  (`Scene_root`'s scratch), and the entry's tooltip shows that refusal; the
  queued operation holds the scene root and the nodes weakly, runs only while
  the scene is registered (`App_scenes::is_host_registered()`), and
  `wrap_with_path()` checks the nodes again.

## Create menu

The Create menu has a "Level Kit" submenu (U47) in both places it is
offered: the Scene Hierarchy window's row menu "Create" (on a prim row and
on the scene header row, `Scene_root::make_browser_window()`) and the main
menu's Commands > Create (the command menu bindings `Create.*`, to which
`App_windows::viewport_menu()` appends the submenu). Both draw it with
`level_kit_create_menu()` (`level/level_kit_create_menu.hpp`):

- One submenu per kit (Basic, Architecture, Nature) listing every shape of
  the library by display name in library order, a recreated shape beside its
  original as the Level Kit window lists it ("Arch" and "Arch (F)"); the
  tooltip names the shape id and its description. An entry places a piece of
  the shape at its defaults (`Level_kits::add_piece_of_shape()`, through
  `place()`: the kit brush, its static body, the slots' default materials).
- "Layouts": Line Layout, Grid Layout, Ring Layout, Scatter Layout, Path
  Layout, Segments Layout and Path Stairs (`c_layout_kinds`,
  `get_layout_kind_create_label()`), each a new kit layout node of that kind
  (`Level_kits::add_layout_of_kind()`) with the Level Kit window's initial
  values for the kind, a `basic.block` at its defaults as its item (none for
  stairs) and the name of its entry. Path Stairs is a staircase along a new
  curve: a flight of ten steps rising along +x at the stairs values ("Stairs
  layouts").
- Where: the hierarchy menu creates under the clicked prim (under the scene
  root for the scene row), the main menu under the active scene's root, at
  the parent's origin (its world translation and rotation), as the other
  Create entries put a new node there. The new node is selected; each entry
  is one undo entry. The creation is queued (`Imgui_windows::queue()`) with
  the scene and the parent held weakly, and runs only while the scene is
  registered.
- Disabled with the reason as tooltip: the whole submenu without a scene or
  under a prim that is not a node; a layout entry under a kit layout node (a
  lock would replace the nested layout), under a scaled parent (a layout
  node keeps world scale 1) and, for Path Stairs, under a tilted parent
  (`Level_kits::find_layout_parent_refusal()`). The whole Create menu is
  disabled under a locked layout or a reference instance, as before.
- The menu reads the library's kit and shape lists in place (fixed after
  startup) and allocates nothing while open.

## Stairs layouts

Path stairs from the UI (U46): Create > Level Kit > Layouts > Path Stairs in
either Create menu ("Create menu"); the Level Kit window's Layout section
with the kind Stairs and Add Path Stairs; Make Layout On Curve there with a
plain Curve node selected (Create > Curve, its points shaped first); or the
Properties window's Kind row of a path or segments layout switched to
Stairs.


A stairs layout (U42, R5.12.13) is a Curve prim like a path layout - one node
carrying the `Kit_layout` values and its own curve - whose generation makes
one child: a staircase that follows the curve in plan and in height. The
author edits the curve's points and handles and the staircase follows. The
scene-free parts are `level/stairs_fit.hpp` (the fit of flights and landings
to a height profile) and `level/stairs_mesh.hpp` (the staircase mesh along a
`Curve_path_table`), both built into `editor_level_tests`.

- The values (`Stairs_values`, in `Kit_layout_pattern::stairs`, meters):
  `width` (1; 0.3 - 32), `max_rise`, the step rise limit (0.2; 0.05 - 1),
  `min_tread`, the tread depth of a flight (0.3; 0.1 - 4), `thickness` (0.2;
  0.02 - 4) and `tolerance`, the edge tolerance (0.01; 0.001 - 1); and
  `Kit_layout_pattern::stairs_material`, the name of a scene or palette
  material, empty for the default of the `stone` slot, the slot of
  `basic.stairs`. The rise limit and the tread depth default to the step
  rules of `basic.stairs` (R3.5: 0.2 m, and 1.5 times it). The items, the
  snap values, the seeds and the connector switches are kept and not used.
- The curve is read in the node's own frame with y up. A station is a
  distance along the curve's plan (its xz projection); the curve's height at
  a station is its y there. Each cubic span is sampled at 32 parameter steps
  (a line span at its ends) and the plan length integrated per step.
- The fit (`fit_stairs()`), stated exactly:
  1. The height profile is cut where it turns from rising to falling or
     back by at least the rise limit (a hysteresis; a bump or dip below the
     rise limit makes no step). The first stretch starts at the curve's
     start and the last ends at its end.
  2. Each stretch climbs (or descends) from the walking surface's level at
     its begin to the curve's height at its end in the least count of equal
     steps within the rise limit, `ceil(change / max_rise)` (a quotient
     above an integer by less than a millionth of itself takes no further
     step, so no step is higher than the limit beyond rounding). A change
     below 5 % of the rise limit (`c_stairs_min_step_share`, 1 cm at the
     default) makes no step: the tread stays level. So the first tread is at
     the curve's start height, and the last tread at its end height or, where
     the last change is below that share, within that share of it.
  3. Each riser has a window of stations: from the last station where the
     curve is more than the rise limit below the tread after the riser, to
     the first station where it is more than the rise limit above the tread
     before it. Risers stand at least a tread depth apart and in no corner
     landing (below). A riser going up may stand at the curve's very start
     and one going down at its very end (the foot of the stairs, as the
     front of `basic.stairs`); at a top end the tread is at least a tread
     depth deep.
  4. A flight starts as late as these rules let it and runs on, one tread
     depth per step, while each next riser is in its window (to within 1e-4
     m of station); then the tread stays level - a landing - until the next
     flight starts, again as late as it may.
  So every point of the walking surface is within the rise limit of the
  curve's height at its station, to 0.2 mm (`c_stairs_deviation_slack`, for
  the station tolerance of step 4; a fit that would leave the curve by more
  is refused): a flight starts one rise below the curve
  and ends one rise above it. A curve at the flight slope (`max_rise` per
  `min_tread`) is one flight; at half that slope flights of three steps
  alternate with landings; a level curve is one landing; a curve climbing
  less than a rise over its whole length has one step.
- `Stairs_fit` is the readback: the risers (station, the levels before and
  after, the flight), the flights (step count, rise per step - negative
  going down -, tread, begin and end stations; each riser has its tread on
  its upper side), the landings (begin, length, level: the part of a tread
  no flight's step uses), the plan length, the start and end levels, the
  total rise (the curve's end height minus its start height), the step count
  and `max_deviation`, the largest height difference between the walking
  surface and the curve.
- The step shape: a riser is a straight vertical face across the stairs,
  square to the curve's plan tangent at its station; treads are level; the
  two side edges are the curve's plan offset by half the width to each side.
  On a curved span a tread is a winder, deeper at the outer edge; the tread
  depth is measured on the curve itself (the centre line). Between the ends,
  the risers, the underside's knots and the ends of the corner landings,
  cross-sections are added so that a straight piece of the outer side edge
  leaves its arc by at most the edge tolerance (from the largest plan
  curvature of the interval); a straight part has no cross-section beyond
  those, and a subdivision that would take more than 4096 cross-sections
  is refused. Every tread and landing is made of convex quads between
  neighbouring cross-sections.
- A corner (control points where the plan tangent turns by more than 1e-3
  rad) gets a level mitred landing: the cross-section there is the mitre
  line, the landing reaches half the width times tan(turn / 2), and 2 cm, to
  each side of the corner along the plan, both of its ends are
  cross-sections, and no riser and no other cross-section stands between
  them. The landing's side edges are the straight lines from its ends to the
  mitre line's ends: the edge tolerance does not apply within a landing,
  whose edges leave a curved span's offset by what that span bends over the
  landing's reach.
- The body is a closed solid (`check_kit_mesh` holds for it). The underside
  lies the thickness below a landing and below the foot of each riser, and
  under a flight slopes at the flight's slope from one riser's foot to the
  next, reaching the landing's level one tread depth into it; the two ends
  are vertical faces. Side faces and undersides shade smooth along the curve
  and every edge sharper than 20 degrees is hard
  (`c_stairs_crease_angle_degrees`: tread, riser and side boundaries, and
  the underside's change of slope at a default flight). The texture
  coordinates are the kit's per-facet ones.
- Refused, each naming the distance along the plan and the nearest control
  point (its index): a curve too steep for its steps to stay within the
  rise limit of it (steeper than the flight slope over more than about a
  step, or a turn from up to down or down to up without a tread's depth of
  room); a curve too short for its steps (N steps need N treads of plan
  length, also where the curve is gentler than a flight: "it needs N steps
  with treads of at least T m, X m of plan, and has Y m"); a curve that is
  vertical or stops in plan; a layout node tilted off the vertical (its
  treads are level in its own frame; a turn about the up axis is free); a plan radius below 0.75 times the width
  (`c_stairs_min_radius_per_width`, so a winder's inner edge keeps a third of
  its depth); a corner sharper than 2.8 rad or without room for its landing
  before the neighbouring corner or the curve's end; a closed curve; more
  than 512 steps or 4096 cross-sections; a value out of its range; a
  material name that is neither a scene nor a palette material. A refused
  curve edit leaves the previous staircase and logs the warning `Kit layout
  '<node>' keeps its children after the curve edit: <refusal>`; a refused
  value edit or creation changes nothing.
- The child (`Level_kits::build_stairs_generation()`,
  `make_stairs_insert()`) is a Mesh node named "Stairs" at the layout's
  origin, an instance of a brush of its own made from the generated
  geometry, with a static triangle mesh body (the collision kind of
  `basic.stairs`) and one primitive with the material; it is navigation
  input and a snap surface as any piece of such a body is. It is no kit
  piece: it carries no `Kit_piece` record, and instead the attached value
  `Kit_layout_generated.source`, the text of the curve's points and the
  stairs values it was generated from and the generator's version
  (`make_stairs_source_text()`, hexadecimal floats,
  `c_stairs_generator_version`). A generation keeps the child when its
  source text, its material's name and its identity local transform match -
  tested before any mesh is made, so a kept child costs the fit alone - and
  else replaces every child by a new one; so `refresh_layout()` and an edit
  that changes nothing record nothing.
- Level Check (R2.10) tests each generated staircase of the scene as it
  tests `basic.stairs`, through the one `check_stairs_walkability()`: the
  largest step rise of its fit above the agent's climb, the flight slope
  `atan(rise / tread depth)` above the agent's slope, the width below the
  agent's walk width; each is an `unwalkable_parameters` finding on the
  staircase node ("Stairs: stairs layout: step rise 0.5 m is higher than the
  agent's climb 0.4 m"). The values are read from the child's source text, so
  the finding is about the mesh that stands there; a text that does not read
  (another generator version) is an `unreadable_piece` finding. The values
  are not refused: as for the kit stairs, the author may build stairs the
  agent cannot climb and is told.
- Everything "Path layouts" states about edits holds: each curve edit is one
  `Curve_set_operation` whose follow-up regenerates the staircase, a point
  or handle drag previews it live ("Live layout preview"), a value edit is
  `set_layout()`, undo and redo give back the same nodes, selecting the
  staircase of a locked layout selects the layout node, unlocked the child
  is left alone. A new stairs layout without points gets a curve of two
  corner points, a flight of ten steps at the values' slope along +x;
  `make_layout_from_piece()` refuses the kind (it places no piece), and the
  Brush tool's Place As Layout with the kind places that default staircase.
- A level saves the child with its mesh, material and body like any mesh
  node, and its source text; at load nothing regenerates: the loaded child
  is the saved mesh. A refresh keeps it while its source text is the one the
  curve, the values and the current generator version make, and replaces it
  otherwise (a staircase saved by an earlier generator version); the next
  curve or value edit replaces it.
- The Properties window's Kit Layout group shows for the kind the rows Kind,
  Width, Step Rise Limit, Tread Depth, Thickness, Edge Tolerance and
  Material (a combo of the `stone` slot's palette materials), the Run line
  "N steps in F flights, L landings; rise R m over P m" with "within D m of
  the curve" (or the refusal), and Lock; the item groups, Add Shape Item and
  the snap, seed and connector rows are not shown. A drag of a value row
  previews the staircase live.
- `level_get_layouts` and every layout reply report `pattern.stairs`
  (`width`, `step_rise`, `tread_depth`, `thickness`, `edge_tolerance`,
  `material`) and, for the kind, `stairs`: the `summary` ("N steps in F
  flights, L landings"), the fit as above and `mesh` (`valid`, `problems`,
  `triangle_count`, `vertex_count`, `components`, the bounds) of the mesh the
  curve and the values make now, or `problem`, the refusal; the child is
  listed with `generated` true, its `world_aabb` and its material names.
- A regeneration makes a new brush, mesh and body. A Debug build takes about
  95 ms for a staircase of 100 steps (1500 triangles), 7 ms of it the fit,
  the mesh and its normals and the rest the brush, its render and ray
  tracing meshes and the body.

## Parameter changes

`Level_kits::set_parameters(node, values)` gives a placed piece new parameter
values as one undo entry. It refuses, changing nothing, a node that is not a
mesh carrying `Kit_piece`, values of another shape and values out of range
(the message names the parameter and its range). Values equal to the piece's
record change nothing and record no entry. Otherwise it gets the brush of the
new values from the cache and executes one `Kit_piece_parameters_operation`
(`kit_piece_operation.hpp`), which swaps, under the scene's item host mutex:

- the mesh primitives: each keeps its slot's material and takes the brush's
  part primitive of that slot (`swap_mesh_primitives()`, so the raytrace
  instances follow); a mesh whose primitive count is not the shape's slot
  count is refused;
- the collision shape: the brush's, through the node's physics system, which
  rebuilds the static body (a node without a body keeps its shape);
- `Brush_placement.brush` and `Kit_piece.parameters`.

Undo swaps all four back. The node keeps its id, name, transform, children,
materials and body settings; other pieces of the old values keep the old
brush. The body's mass value stays as placed.

Live drags (D3.6, R3.4). `Level_kits::preview_parameters(node, values)` shows
values on a piece without an operation: its first call remembers the piece's
mesh primitives, collision shape, brush and record, and every call gives the
mesh primitives the part primitives of the values' brush (materials kept, raytrace
instances following) and sends `Mesh_geometry_changed_message`; the record,
`Brush_placement.brush` and the collision shape stay. The preview holds the
brush it shows, so the brush of an intermediate value leaves the cache when the
next value replaces it. `set_parameters()` on the previewed piece goes from the
remembered state: one `Kit_piece_parameters_operation` from there to the final
values, whose execute writes all four whatever the preview showed last, or no
entry and the remembered mesh back when the final values are the remembered
record. `cancel_parameter_preview()` gives the piece its remembered primitives
back without an entry; it runs when `set_parameters()` refuses the final
values, when the piece leaves the selection (`Selection_message`), is removed
(`Items_removed_message`) or its scene closes (`Close_scene_message`), and
before `Operation_stack::undo()` and `redo()`. One preview is live at a time;
previewing another piece cancels the first. A preview step of the stairs, the
starter shape with the largest triangle budget, takes about 2 - 3 ms (brush
generation, upload and the primitive swap; `editor.scene` trace line "preview
step").

## Replace

`Level_kits::replace_shape(scene_root, shape, replacements, surface)` (R5.8,
D3.10, U24) gives placed pieces another shape as one undo entry. Per piece it
executes a `Kit_piece_parameters_operation` that swaps `Kit_piece.shape` with
the other four ("Parameter changes"): the mesh primitives become one per slot
of the new shape, the collision shape the new brush's (the static body
rebuilt), `Brush_placement.brush` and `Kit_piece.parameters` the new values.
Across a bodiless shape the piece gets what a fresh placement of the new
shape gets: replaced by a shape without a body it loses its body and gains
`pass_through`; a piece of a shape without a body replaced by one with a body
gets the body a placed piece of it gets (static with the brush's collision
shape, none when the brush has no collision shape, as without static physics)
and loses the flag. Between two shapes with bodies the piece keeps its own
motion mode and flag, so a piece whose body the user removed stays without
one; taken through a bodiless shape and back it gets a fresh body
(`Kit_piece_mesh_state::motion_mode` and `ground`).
Palette materials the new slots need are inserted first in the same entry.
The node keeps its id, name, parent, children, group and selection; undo
gives back the shape, parameters, mesh primitives with their materials,
brush, collision shape, body, navigation flag and transform exactly.

- Snap (U24): with `surface` snapping (Snap To Surface on, or Orient To
  Surface, which implies the snap as for placement) each replaced piece
  takes the resting pose `find_surface_placement()` ("Surface placement")
  finds for the new shape's bounds from the node's world pose, in the same
  entry: a `Node_transform_operation` after the shape swaps, its children
  riding with it. Parents are searched before children: a replaced piece
  below another one starts its search from the pose its ancestor's snap
  carries it to, and its local transform is taken against that ancestor's
  snapped pose, as `snap_to_surface()` does (`plan_surface_snaps()`). The
  replaced pieces of the call are not surfaces for each other (their old geometry is still in the raytrace scene); every other
  mesh is, a listed piece of the shape already included. A piece that finds
  no place within the maximum distance, or rests already, keeps its
  transform bit for bit, as every piece does when `surface` does not snap.
  A piece of the shape already is not snapped. The Level Kit window passes
  the Surface_snap_config pair (`get_surface_settings()`);
  `level_replace_pieces` takes its own surface arguments, as
  `level_place_pieces` does, and does not read the UI setting.

- Parameters (`carry_parameter_values()`, `kit_replace.hpp`): a parameter of
  the new shape that the old shape has with the same name and type keeps its
  value, a float or int clamped to the new range, an enum by its value name
  (a name the new shape lacks takes the default). A carried parameter derived
  from the seed stays derived where the new parameter is derivable, derived
  again from the carried seed in the new range. Every other parameter takes
  the new shape's default, its default derive mask included; the seed carries,
  so a tree replaced by a rock and back is the same tree. Given values
  replace the carried ones. Each parameter is reported `carried`, `clamped`
  or `defaulted`; a value derived again from the seed is `carried`, also when
  the old value lay outside the new range.
- Material slots (`match_material_slots()`): a new slot keeps the material of
  the old slot of the same name (a tree's foliage on a bush), the others take
  their palette default; a given material per slot overrides both.
- Skipped (U24), the other nodes replaced: a kit layout node (its items'
  shapes are its own values, "Kit layout nodes"; a selected one is not
  replaced through), a child of a locked kit layout node (the layout
  refusal; the selection never holds one, so it is reached by id only), a
  group node (an `Xform`; not replaced through), a node that is not a kit
  piece, a record that does not read and a mesh whose primitive count is not
  its shape's slot count (a piece that keeps a loaded mesh), decided in that
  order (`classify_replace_target()`, `kit_replace.hpp`). The result lists
  each skipped node with its `Kit_replace_skip` reason and a message naming
  it (`find_replace_skip()`, which `level_replace_pieces` also asks before it
  reads `parameters` for a node, so a skipped node never refuses them).
- Refused, naming the replacement and changing nothing for any piece: a node
  of another scene, a node listed twice, values the new shape refuses, a
  material the slots refuse and a surface direction or maximum distance out
  of range. A child of an
  unlocked kit layout node is replaced as any piece is; a later lock or
  regeneration of the layout discards the replace, as it discards parameter
  edits. A piece of the shape
  already is left as it is (`replaced` false); when nothing is replaced, no
  entry is recorded.

## Lights on pieces

A shape can carry lights (U34, U36). It states up to four light frames from
its values, as it states connectors: a name, a position and the direction the
light shines in the piece's local space (`Kit_shape_definition::get_light_frames`,
`level/kit_light.hpp`; how a generator states them is in D3.15 of
`doc/plans/level_tools_design.md`). Each frame is a child light node of the
piece carrying `Kit_light.frame`, the frame's name, which it is also named
after when made; afterwards the kit sets only its local transform. Its type,
color, intensity, range, cone angles and shadow casting are the light's own
properties, edited in the Properties window as any light's. A new node
starts with the frame's values, by default a warm white point light of 40 cd
with a range of 8 m, and always with shadow casting off; a shape may state
its own (the stone lantern: 12 cd, 6 m, color (1, 0.72, 0.42), an
orchestrator choice for the user's review).

- Every regeneration of a piece keeps the edits: a parameter change, Replace,
  a kit layout regenerating a kept child and the load refresh move each light
  node to the frame its `Kit_light.frame` names and leave its light
  properties as they are, give a frame without a node a new one and remove a
  node whose frame the values no longer state, in the same undo entry. A
  parameter edit to the values the piece has is no regeneration and changes
  nothing. Replace matches against the new shape's frame names, so a light
  whose frame the new shape also has keeps its edits.
- During a live parameter drag the light nodes stand at every step where the
  release puts them (U43); a cancel puts them back, and the release is one
  undo entry. A frame that appears or goes during the drag gets or loses its
  node at the release.
- A copy of a kit light under its piece (a duplicate or a paste of the light
  node) is the user's: the next regeneration clears its `Kit_light` record and
  it stays as a plain light.
- A duplicate, a copy and paste and a group duplicate copy the light nodes with
  their edited properties; a mirror reflects them with the piece, direction
  included. The pieces a kit layout or scatter places start with the starting
  values; Copy and Paste Properties copies one light's values to another.
- Every lit piece makes its lights, without a limit. The lights save as
  ordinary glTF lights.

## Connectors

A connector (R5.3, D1.12) is a named frame on a shape where another shape
attaches: a position, an outward axis and an up axis in the piece's local
space, computed from its current parameter values
(`Kit_shape_definition::get_connectors`, `level/kit_connector.hpp`). The
shapes and their connectors are listed in D1.12 of
`doc/plans/level_tools_design.md`; the nature shapes have none.

A join (D3.12, `join_connectors()`) puts connector A of a piece on connector
B of a target piece: the positions coincide, A's outward axis opposes B's, and
A's up axis agrees with B's up turned by `twist_steps` quarter turns about B's
outward axis; `gap` then moves the piece that many meters along B's outward
axis. The rotation is always proper (no reflection). A side connector joined
to a top connector tips the piece over; the twist chooses its heading.

- `Level_kits::get_world_connectors()` gives a placed piece's frames in world
  space under its full world matrix (U22): the position by the matrix, the
  outward axis as the transformed face's normal and the up axis along the
  transformed face, so a scaled, stretched, sheared or mirrored piece's
  connectors sit on its mesh and joined faces are flush (D1.12 "World
  frames"). A piece whose matrix has a zero scale axis has no frames and is
  refused.
- Placing: a `Kit_placement` with `connect` takes the joined pose in place of
  its position and rotation, and neither module snap nor the surface options
  apply to it. The target is a kit piece already in the scene (any, a child of
  a locked kit layout node included: the target is not edited). The new piece
  has world scale 1; under a parent (`parent_id`) where that pose would need
  a sheared local transform it is refused with the message of
  `connect_piece()` below.
- Moving: `Level_kits::connect_piece()` moves a placed kit piece, its children
  with it, to the joined pose as one undo entry: a `Node_transform_operation`
  and, in the same `Compound_operation`, the regeneration of each locked
  surface-snapped kit layout node below it, as every move does ("Kit layout
  nodes"); no entry when it is there already (within 0.1 mm and 1e-4 rad).
  Scaled pieces and targets join: the moved piece keeps its own scale, and a
  rigid world motion carries its transformed connector frame onto the
  target's. Under a non-uniformly scaled or sheared parent chain the join is
  made when the new local transform stays a translation, rotation and scale
  (no turn, or a turn about an axis of that scale) and refused otherwise,
  saying that the joined pose would need a sheared local transform (D3.12
  "Scaled pieces"). A node that is not a kit piece (a group, a kit layout
  node), a child of a locked kit layout node, a piece or target with a zero
  scale axis, the piece itself or a node below it as the target and an
  unknown connector name are refused too; the message for an unknown name
  lists the shape's connectors.
- A joined piece is an ordinary piece: it keeps no link to its target, so a
  later move or parameter change of the target leaves it where it is, and a
  connect again lands it on the target's new frame.
- A mirrored piece (Groups) has its frames under its new rotation: on the
  mirror image exactly for a piece of a declared symmetry plane, with a pair
  of names across that plane (start and end, left and right, front and back)
  changing sides.

### Connector switches

A placed piece's connectors can be switched off (U26, U33): the author turns
off the ones a piece should not offer, such as a floor slab's side
connectors.

- Values: two attached values of the Kit_piece group on the piece's node
  (`level/kit_piece.hpp`), both saved with it: `Kit_piece.connectors_enabled`
  (true by default), the switch of all of the piece's connectors, and
  `Kit_piece.disabled_connectors` (empty by default), the names of the
  connectors switched off one by one, comma separated, sorted, unique and
  without spaces, each a connector of the piece's shape (every write path
  holds this; "Raw writes" below). A connector is enabled
  when the piece switch is on and its name is not listed
  (`Kit_connector_switches::is_enabled()`, `level/kit_connector.hpp`). A
  switch at its default is written as no local value, so a piece with every
  connector on carries neither value in the file, and a node without them
  (an older scene) has every connector enabled. The list keeps the names off
  rather than on, so a connector a shape gains later is on.
- One query: `Level_kits::get_connector_switches()` reads a piece's
  switches (`read_piece_connector_switches()`, the piece's own values) and
  adds the connectors its layout hides ("Layout connector switches");
  `Level_kits::get_local_connectors()` and `get_world_connectors()` take
  `Kit_connector_set::all` or `enabled`, the latter dropping the disabled
  connectors in the shape's order; every consumer reads through them or
  through `Kit_connector_switches::is_enabled()` of that query:
  - Connector indicators: a disabled connector is not drawn. A distinct
    "disabled" style would put connectors in the view that nothing can join,
    and the Properties rows already show which are off.
  - The Level Kit window's Connect rows offer the target's and the moved
    piece's enabled connectors; a chosen connector switched off gives way to
    the first enabled one. Place Connected joins to an enabled target
    connector; a new piece has every connector enabled.
  - Joins (`resolve_connect()`: `connect_piece()`, `level_connect_pieces`,
    `level_place_pieces` connect): a disabled connector of the moved piece or
    of the target is refused, the message naming it and listing the enabled
    ones.
  - Connector snap: a disabled connector is neither a moved connector nor a
    target (U27: "any pair when both connectors are enabled"); the gathered
    points keep their index in the shape's order for the tie rule.
  - `level_get_connectors` lists every connector with `enabled` and the
    piece's `connectors_enabled` and `disabled_connectors`.
- Editing: `Level_kits::set_connector_switches()` writes the piece switch and
  any connector switches of one edit as one undo entry (a
  `Property_set_operation` per changed value), none when nothing changes. A
  name the piece's shape does not have, a shape without connectors (a rock), a node that is not a kit piece and a
  child of a locked kit layout node are refused. The Properties window's
  "Kit Piece" group of a piece whose shape has connectors shows a Connectors
  checkbox and one "Connector <name>" checkbox per connector (disabled while
  Connectors is off), each click one entry. Over MCP:
  `level_set_piece_connectors`.
- Raw writes: `set_item_property`, the developer-mode raw rows (a
  multi-selection included) and Paste Properties write the values directly.
  Each write is checked against the piece's shape at that time
  (`Level_kits::check_connector_switch_write()`, as
  `check_layout_value_write()` checks kit layout values) and refused with a
  message, nothing changed: `connectors_enabled` on a shape without
  connectors, a list naming a connector the shape does not have (exact and
  case sensitive: `Top` is not `top`), and a list that is not in the
  canonical form (the message gives that form to write). A refused raw row
  edit is taken back without an undo entry. Paste Properties checks the
  record the paste leaves on each target: a paste carries every local value
  of the source, the record included, so a piece pasted from a piece of
  another shape takes that shape's record and switches together; a paste
  whose switches the left record refuses pastes nothing on any target, logged
  as `Paste Properties onto '<name>' refused, nothing pasted: <reason>`. A
  source with every connector on holds no switch value and leaves the
  target's switches: Paste Properties copies local values only
  (`doc/erhe/property_system.md` D12: Copy Properties reads the item's local
  values), for every property.
- Change: every write of either value, its undo and redo reach
  `App_context::on_item_property_changed()`, which drops the piece's cached
  indicator frames and queues `Kit_piece_connectors_changed_message`; the
  Connect rows and the Properties rows read the piece again on it. A drag
  gathers the switches at its start.
- Shape changes: the names are the same at every parameter value (D1.12),
  so a parameter change keeps the switches. Replace keeps the switch of each
  listed name the new shape also has and clears the others in the replace's
  undo entry; a new shape without connectors also clears
  `connectors_enabled`. Undo of the replace gives both values back.
- A file whose switch values a write would refuse (hand-edited: unknown or
  wrong-case names, spaces, an unsorted list, `connectors_enabled` on a shape
  without connectors) loads with the values as they are:
  `Level_kits::refresh_scene()` names them in one `editor.scene` warning per
  scene (`... connector switch value(s) kept as loaded ...`). The list is
  then read with spaces trimmed and empty entries skipped; a listed name that
  is a connector of the shape is off, any other has no effect, and the next
  write through any path is checked as above.
- Mirror and duplicate: a duplicated piece copies its switches; a mirrored
  piece keeps them by name, so a switch follows its name to the other side
  of the mirror plane.
- Kit layout children: a generated child's own switches are all on (the
  placement writes none); a path or segments layout hides connectors of its
  children by direction ("Layout connector switches"). The kept-child rule
  compares record, materials and pose only, so a child kept by a
  regeneration keeps its switches and a child made again has every
  connector on. A locked layout's children are not edited directly, so their
  switches are refused while it is locked; switches per layout item are a
  later step (`doc/plans/level_tools_implementation.md`).

### Layout connector switches

A path or segments layout hides the connectors between its children, which
a long run would otherwise offer in the dozens (U39, R5.12.12): the
switches target those inter-child connectors, so an open curve's first and
last child follow them only for the connector facing their neighbour.

- Values: seven `Kit_layout` booleans on the layout node, all off by
  default and saved with it, so a node without them hides nothing:
  `hide_all_connectors`, `hide_plus_tangent_connectors`,
  `hide_minus_tangent_connectors`, `hide_plus_normal_connectors`,
  `hide_minus_normal_connectors`, `hide_plus_binormal_connectors` and
  `hide_minus_binormal_connectors` (`Kit_layout_connector_hiding`,
  `Kit_layout_data::connector_hiding`). The six direction switches are
  independent; Hide All hides every direction whatever they hold.
- Which children: only a locked layout hides; an unlocked layout's
  children are ordinary pieces. Every child of a closed curve is inner. On
  an open curve the children between the first (head) and the last (tail)
  are inner; the head follows the switches for its +tangent connectors only
  (facing the second child) and the tail for its -tangent ones only (facing
  the second-to-last), each keeping its other five directions; a layout of
  one child hides nothing (`get_layout_child_role()`,
  `get_layout_hide_reason()`). Line, grid, ring and scatter layouts keep the
  values and ignore them.
- Direction: a child's element frame is its own local rotation as the
  layout made it: tangent its x axis (the direction of travel: a path
  element's heading or tangent, a segment's chord from point i to i + 1),
  binormal its y axis (up) and normal its z axis, the right of the direction
  of travel as a segment handle's normal ("Segment handles") and the
  opposite of the curve frame's side. A connector's outward, carried by the
  child's local transform as a face normal (scale and mirror included, as
  `transform_connector()` carries it; in the element frame only the local
  scale acts, dividing it), faces the axis of its largest absolute
  component in that frame with that component's sign; ties go to tangent,
  then normal, then binormal (`classify_layout_direction()`,
  `get_outward_in_element()`). A connector is classified by its outward
  direction alone, not by where it sits: a roof's connectors, which all face
  down, follow only the -binormal switch. A child with a zero scale axis
  hides nothing.
- The scene-free part is `level/kit_layout_connectors.hpp`, built into
  `editor_level_tests`; `Level_kits::get_layout_child()` reads a child's
  layout, role and local scale, and `get_connector_switches()` sets a bit
  per hidden connector index (the shape's declared order) in
  `Kit_connector_switches::layout_hidden_bits`, which `is_enabled(name,
  index)` reads; a shape has at most `c_max_shape_connectors` (64)
  connectors, which `editor_level_tests` checks for every shape at its
  defaults, its range corners and each parameter's range ends: a hidden connector is
  disabled for every consumer above - not drawn, no snap candidate, not
  offered by the Connect rows, a join to it refused (`... is hidden by its
  kit layout's connector switches ...`).
- Change: a switch write is one `Property_set_operation` (one undo entry,
  no regeneration: the children stay the same nodes). The connector
  indicators drop the cached frames of a layout's children on each
  `Kit_layout_changed_message` of it (a switch, the lock, a regeneration's
  values), on each `Curve_changed_message` of it (closure and point count
  move the head and tail) and during a live value preview; a removed piece
  drops its siblings' frames too, as the child count decides the head and
  tail. A drag gathers the switches at its start.
- Editing: the Properties window's Kit Layout group and the Level Kit
  window's initial values show Hide All Connectors and Hide +Tangent,
  -Tangent, +Normal, -Normal, +Binormal and -Binormal Connectors for a
  path or segments kind ("Kit layout rows"); over MCP `connector_hiding` of
  `level_create_layout` and `level_set_layout`, read back by every layout
  reply, and `level_get_connectors` reports each child's direction and
  reason ("MCP").

## Parameter rows

`kit_parameter_rows.hpp` draws a piece's parameters for both windows that edit
them: `kit_parameter_row()` is the widget of one parameter (a drag per float
with its range and unit, a drag per integer, a combo per enum, the description
and range as tooltip), `kit_stairs_line()` the stairs layout line and
`kit_message_line()` a refusal in red. A derivable parameter's row has a "from
seed" checkbox in front (tooltip "Derive from seed (range a - b)", the range
resolved against the lengths the row is given); a masked row shows its derived
value disabled. The seed row has a New button that takes `kit_next_seed()`. A
toggle and the New button end an edit like a chosen combo entry; a seed change,
dragged or typed, derives the masked values again on every change, so a seed
drag previews live. The row names its items for the UI tools as
`<name>.from_seed` and `seed.new`. `Kit_piece_editor` holds one piece's
values and the lengths of its scene: it reads the record when another piece is shown or after
`mark_dirty()`, which its owner calls on the selection, active item and mesh
geometry messages, and not while a drag previews the piece. Each change during
a drag that `validate()` accepts queues one `preview_parameters()`; the edit's
end - a drag released, a value typed and entered, a combo entry chosen - queues
`set_parameters()`, one undo entry, or with refused values the message and
`cancel_parameter_preview()`; an edit that ends without a change (Escape in a
typed value) cancels too. A drag stays within the parameter's range; a typed
value may leave it and is refused. Everything goes through
`Imgui_windows::queue()`, because a new brush uploads its mesh. The Level Kit
window's rows of the shape to place hand the Brush tool the kit brush of every
valid change in the same way, without an undo entry.

The Properties window draws a single selected piece's "Kit Piece" group with
it (`Property_group_rows`, `doc/editor/properties_window.md`): the shape's
display name (its id and description as tooltip), one row per parameter,
for stairs, the layout line, and for a shape with connectors the connector
switches ("Connector switches"). A piece whose record does not read shows its shape
id and the problem instead. A multi-selection shows the group for no piece, as
the window's other hand-written groups. The Level Kit window's Selection section
is the same editor ("Level Kit window").

## Kit layout rows

`kit_layout_editor.hpp` draws the values of a kit layout (R5.12.1) for both
places that show them: `layout_pattern_row()` is the widget of one pattern
row and `layout_setting_row()` of one snap or seed row, each returning true
when an edit ended (a drag released, a value typed, a radio button or
checkbox at once). `get_layout_rows(pattern)` lists the pattern rows the kind
and count mode show: Kind; for a line, ring and path Count Mode (By Count, By
Gap) and Count or Gap; for a line Direction and Step or Length; for a grid
Count X, Count Z, Spacing X and Spacing Z; for a ring Radius, Arc and Face
Outward; for a path Orientation (Upright, Follow Tangent); for segments
Orientation alone; for stairs Width, Step Rise Limit, Tread Depth,
Thickness, Edge Tolerance and Material ("Stairs layouts"); for a scatter
Scatter Mode (By Count, By Density) and Count or Density, Region (Rectangle,
Disc) and Size X and Size Z or Region Radius, Min Spacing, Yaw Min and Yaw
Max. The Kind row offers `Kit_layout_kinds`: every kind (the Level Kit
window's initial values), Line, Grid, Ring and Scatter on an Xform node (Path,
Segments and Stairs shown as not offered), Path, Segments and Stairs on a Curve node. Beside a widget (`Kit_row_label::beside`, the Level Kit window)
the row's name is the widget's label; in the Properties window
(`Kit_row_label::recorded`) a row's widgets take its name, those of a row of
several as `<row>.<role>`: `Kind.line`, `Kind.grid`, `Kind.ring`,
`Kind.path`, `Kind.scatter`, `Kind.segments`, `Kind.stairs`, `Count Mode.count`, `Count Mode.gap`,
`Scatter Mode.count`, `Scatter Mode.density`, `Region.rectangle`,
`Region.disc`, `Orientation.upright`,
`Orientation.tangent`, `Lock.lock`, `Lock.unlock` and `Lock.refresh`.

The Properties window's "Kit Layout" group (`Kit_layout_editor`) shows a
single selected kit layout node (a locked layout's piece selects it, "Kit
layout nodes"): Layout (locked or unlocked and the piece count), the pattern
rows, Run (the count, the step or the actual gap once the remainder is
spread, and the space - a path's curve length; segments: the piece count
and the spanned chord lengths' sum; the Curve group beside it
shows the curve's own rows), per item a group "Item <n>: <shape>" with its
Shape line (and the stairs line and a refusal), its parameter rows
("Parameter rows", seed mask toggles included), one `<slot> material` combo
per slot (the slot's default shown for an empty name), Weight (0 - 1000, the
odds a scatter picks the item by; the other kinds take item i modulo the item
count) and Remove Item
(disabled for the last item); Add Shape Item (the shape the Brush tool
places, chosen in the Level Kit window, with its rows' values and slot
materials); Module Snap, Surface Snap, Surface Max Distance, Surface Orient,
Per Piece Seed and Layout Seed; for a path or segments layout the connector
switches (`get_layout_setting_rows()`: Hide All Connectors, then Hide
+Tangent, -Tangent, +Normal, -Normal, +Binormal and -Binormal Connectors,
the six disabled while Hide All is on); Lock (its tooltip warns that it discards the edits made to the
children), Unlock and Refresh; a Message row with the last result or refusal
in red. A drag commits when it ends, a radio button, checkbox, combo entry or
button at once; each commit queues one `set_layout()`, `set_layout_lock()` or
`refresh_layout()` (`Imgui_windows::queue()`), the entry points of the MCP
tools, so each is one undo entry; a refusal shows in the Message row and as a
warning line `Kit Layout: '<node>' ... refused` in the log. The rows show a
copy of the node's values, read in a queued call (an item's brush may be made
there) when another node is shown, on the selection and active item
messages, on `Kit_layout_changed_message` and on a shape `Curve_changed_message`
of the shown node, and never while an item of the window is active; nothing
is recomputed per frame. A refused or unchanged edit reads the node's values
back. A drag of a row of a locked layout (an item's parameter rows, the
pattern's Count, Gap, Direction, Step, Length, Count X / Z, Spacing X / Z,
Radius, Arc, Density, Size X / Z, Region Radius, Min Spacing and Yaw Min /
Max, an item's Weight, the Layout Seed, the Surface Max Distance) previews each change through
`Level_kits::preview_layout_values()` ("Live layout preview"), queued like the
commit; the Run line shows the previewed run. Typed input previews each
typed character the same way and commits on Enter; Escape gives the starting
value back, which the commit then records as no change. An item parameter
that fails `validate()` mid-drag keeps the last valid step and shows its
message; a release on it restores the state before the drag. A drag of an
unlocked layout's row changes the value alone at the release.

## Surface placement

`find_surface_placement()` (`level_queries.hpp`) computes the snap to
surface transform of a mesh from its node-space bounds, a start pose (position,
rotation, scale in the scene root's space) and `Surface_options` (`snap`,
`orient`, a unit `direction`, down by default, and `max_distance`, meters, 1
by default). It commits the scene's raytrace scene and searches along the
direction, either way, for the nearest place where the piece rests and fits
(U23, U30). The excluded meshes - the ones being moved - are not there, and
neither is a mesh carrying `Editor_item_flags::pass_through` (a water piece):
the snap, the orient, the support report and the height map pass through it
to what lies below (`is_surface_mesh()`, the one test every surface ray and
search of `level_queries.cpp` asks).

- The base face is chosen by a `Surface_base_rule` (U48;
  `get_surface_base_frame()`, `surface_base_frame.hpp`); the top face is
  the opposite one, and the piece's up axis is the node-space axis from the
  base face toward the top face. A kit piece (a mesh carrying a kit piece
  record, a kit layout's child, a piece the Brush tool or a replace is
  about to place) is authored Y-up and takes `authored_y_up`: of its two Y
  faces the one whose outward normal, taken to world space by the rotation
  and the signs of the scale, is more along the direction: the minimum Y
  face at any tilt below 90 degrees, mirrored along X or Z or not, and the
  face that is below for a piece turned over (a world transform decomposes
  a mirror into all-negative scale and a turned rotation, which this score
  reads correctly). Any
  other mesh takes `from_pose`: the face whose outward normal, taken to
  world space by the start pose's rotation and the signs of its scale, is
  most along the direction, so an imported Z-up glTF mesh under a -90
  degree X rotation rests on the face that is down in the world; exact ties
  go to the minimum Y face, then maximum Y, minimum and maximum X, minimum
  and maximum Z. With orient, once the line through that face's center
  finds the surface normal, a mesh that is not a kit piece takes the face
  whose outward normal is most against the normal (the least turn onto it,
  `get_orient_frame()`) and, when that is another face, searches the line
  through its center once more and turns that face onto the surface; a kit
  piece keeps its face. `get_surface_base_rule()` gives a scene mesh its rule; the
  snap, orient and the support report all take their base points and spans
  from the one frame.
- A line runs along the direction through each base point: for snap the
  base center and the four base corners (the corners 1 mm in world units
  inside the footprint, along the base face's two in-face axes, so a face
  flush with a side of the piece is not met along its edge), for orient the
  base center alone. Two rays from the base point, one each way along the line, collect
  the hit points of the line's search window, from `max_distance` below the
  base point to `max_distance` plus the piece's extent above it: every face
  of a content mesh that is not excluded and not parallel to the line, as a
  height from the base point, in one traversal per ray (a hit filter that
  records each candidate with its distance and accepts none,
  `doc/erhe/raytrace.md` "Hit filters"). Heights are measured from the base
  point, so their precision near the base does not depend on the search
  distance. The hit points are kept in per-thread buffers that grow and keep
  their capacity; nothing caps their count.
- A place is a displacement along the direction at which one line's base
  point lies on a surface hit - a face whose normal is against the direction,
  the one use of a face's facing - and no line has a hit point strictly
  inside the piece's span there (from the base point to the piece's top
  along the direction, taken from the start pose, at least 1 mm, and 0.1 mm
  in from both ends). Free space is the absence of hit points: a face of
  either facing blocks a span it lies in, and solids are not reconstructed,
  so fit is tested along the lines only, not as a volume overlap.
- A piece inside a solid taller than itself has no hit point in its span,
  so the search can snap it to a surface inside that solid (the slab top
  under a hill, inside the hill; the slab top under a wall the piece
  overlaps) when that is nearer than the solid's top (U31; considering
  solidness is future work).
- The place with the smallest displacement within `max_distance` is the
  one taken; of two at the same distance, along and against the direction, the
  one along the direction (lower) wins. Snap moves the mesh by it; a piece
  hanging over an edge rests on the edge, a piece whose span holds the top
  of a solid it stands in rises onto that top or drops to the nearer free
  surface, a piece in a gap lower than itself goes elsewhere, and a piece
  over a stack of thin slabs with gaps lower than itself goes to the top
  slab when that is within the distance.
- Orient searches its line the same way and then turns the mesh by the
  shortest rotation that takes its start pose's up axis (the base face's
  inward axis, local +Y for an upright kit piece) to the normal of the
  top face at the place, which keeps the heading of a piece that starts
  upright and otherwise turns it only as that rotation does; its base center
  comes to rest on that face. Where several top faces meet at the place the
  normal comes from one order that does not depend on traversal: the normal
  most against the direction first, then the normal's components. A base
  center exactly on an edge between faces of different normals - a piece on
  the grid line where a ramp's low edge meets the floor - takes the flatter
  face (the floor). Where that face continues under the other one (ground
  running under a ramp) a snap again from a start that differs by rounding
  takes it again; where both faces end at the edge (a ramp tile beside a
  floor tile, a ridge) a start one rounding step to either side sees one face
  only and takes its normal. The base center is the pivot of the turn and
  moves only along the direction, so the turn moves no line: a snapped pose
  finds its own place and turns by nothing. The surface under the base
  center alone decides: a piece straddling the crease between a slope and
  flat ground takes the slope's normal when its base center is over the
  slope and stays upright when it is over the ground. Orient with snap is
  orient.
- With no place within the distance the pose is unchanged and
  `surface_found` is false; a kit layout element (a path layout's included)
  then stays where its pattern puts it.

Snap to surface is idempotent for one exclusion set: only hit points within the
piece's span at a candidate within `max_distance` are read, so the answer
does not depend on where the window ends; a snapped pose's surface hit is at
its base, outside its span, so the pose is a place at displacement 0 (within
0.1 mm), the nearest there is, and snapping again with the same excluded
meshes answers zero and returns the pose bit for bit, with orient keeping
the rotation, except for an oriented piece whose base center is on an edge
where both faces end. A mesh oriented onto a plane, kit piece or not, stays
there on any slope: a kit piece keeps its base face, and another mesh's
second line finds the face that already lies on the surface. A pose that found no place finds none again. The answers of different callers for the same pose agree only as far as their
exclusion sets do. Layout generation and its preview steps exclude the
layout's own pieces (the previous generation),
`Level_kits::snap_to_surface()` (`level_snap_to_surface`) searches parents
before children, a child from the pose its ancestor's snap carries it to and
its local transform taken against that ancestor's snapped pose
(`plan_surface_snaps()`), and excludes every mesh of the call, the Transform tool excludes the moved nodes and the
meshes below them, and the support report (`find_support()`) excludes only
the piece itself: a tree of a snapped ring reports a negative distance where
a neighbour's canopy lies within its height above its base, while
`level_snap_to_surface` over the whole ring answers zero.

Placement computes the snapped transform before the insert, so the pieces of
one `place()` call are not surfaces for each other: each snaps to what was in
the scene before the call. The module snap applies first, then the surface
options.

`Level_kits::make_placement_pose()` is the same computation for a piece placed
at a point: the origin at the point, rounded to the module with
`Kit_snap::module`, upright and turned about the up axis by a count of rotation
steps, then the surface options. `Kit_placement_settings` (module snap, surface
options, rotation step) carries its inputs, and `get_placement_settings()` reads
them from the editor settings `level_kit` and `surface_snap` ("Surface
settings"). It allocates nothing, so the Brush tool's kit mode runs it on every
hover update ("Level Kit window").

A caller placing several meshes commits the raytrace scene once and passes it to
the `find_surface_placement()` overload that takes a committed
`erhe::raytrace::IScene`.

## Surface settings

Snap To Surface and Orient To Surface are one pair of editor settings,
`surface_snap` (`Surface_snap_config`, `editor_settings.json`; both off by
default, because they change how the Transform tool moves every mesh). The
same config holds Surface Max Distance, `max_distance` (meters, 1 by default;
"Surface placement"; a loaded value that is not a finite number >= 0 is
reported in the log and replaced by 1, as is `level_kit.layout_surface_max_distance`,
`Level_kits::validate_surface_settings()`), Connector Snap, `connector_snap_distance` (meters, 0 = off,
the default; "Connector snap" below), and Connector Snap On Axis, `connector_snap_axis_mode`
(`Connector_snap_axis_mode`: `full_offset`, the default, or `stay_on_axis`; a file older than
`Surface_snap_config` version 4 reads the default), and Connector Snap Orientation,
`connector_snap_orientation` (`Connector_snap_orientation`: `keep`, the default, or `align`;
a file older than version 5 reads the default). The settings have one owner:
`Level_kits::get_surface_settings()`, `set_surface_snap()`,
`set_surface_orient()`, `set_surface_max_distance()` (the distance must be
a finite number >= 0, `check_surface_max_distance()`; the widgets clamp to 0 -
1000 m and the MCP tools refuse other values), `get_connector_snap_distance()` and
`set_connector_snap_distance()` (a negative or non-finite value is stored as
0), `get_connector_snap_axis_mode()` and `set_connector_snap_axis_mode()`,
`get_connector_snap_orientation()` and `set_connector_snap_orientation()`, which touch the settings store. Every widget writes through them at its
change (`level/surface_settings_ui.hpp`), so all of them show the same state:

- the Level Kit window's checkboxes, its "Surface Max Distance" drag after
  Snap To Surface, its "Connector Snap" drag and its "Connector Snap On Axis" and
  "Connector Snap Orientation" checkboxes;
- two toggle buttons with a narrow Surface Max Distance drag between them
  (named "Surface Max Distance") and a narrow Connector Snap drag, labelled with the
  magnet icon ("Connect" without the icon font), in every viewport's
  toolbar, after the Move / Rotate / Scale buttons (icons arrow-collapse-down
  and slope-uphill, tooltips "Snap To Surface" and "Orient To Surface"; they
  report a checked state to the item recorder, so `get_imgui_items` lists
  them as checkable items named "Snap To Surface" and "Orient To Surface";
  the drag is named "Connector Snap"), followed by a toggle button for
  Connector Snap On Axis (icon axis-arrow-lock, "Axis" without the icon font,
  named "Connector Snap On Axis") and one for Connector Snap Orientation (icon
  rotate-3d-variant, "Turn" without the icon font, named "Connector Snap
  Orientation");
- the rows "Snap To Surface", "Surface Max Distance", "Orient To Surface",
  "Connector Snap", "Connector Snap On Axis" and "Connector Snap Orientation" of the Transform
  window's Move tool group, after "Snap Enable".

Kit placement in the viewport reads the pair through
`get_placement_settings()`. MCP `get_surface_snap_settings {}` reads the
settings and `set_surface_snap_settings {snap, orient, max_distance,
connector_snap_distance, connector_snap_axis_mode, connector_snap_orientation}` sets them (editor
state, no undo entry; an omitted argument keeps its value; a negative or non-number distance, a mode
other than `full_offset` or `stay_on_axis` and an orientation other than `keep` or `align` are
refused); both reply `snap`, `orient`, `max_distance`, `connector_snap_distance`,
`connector_snap_axis_mode` and `connector_snap_orientation`. The MCP placement and
transform tools never read them: they take their own arguments.

### Transform tool

With the pair on, a translate drag of the gizmo snaps the moved meshes to their
nearest places on the surface ("Surface placement") at every step (D4.6), a zero step included (a press without
motion, a drag back to the press point or under half a move grid step, a zero
`drag_selection`). After `Transform_tool::adjust_translation()` writes the
step's transforms (for a zero step the drag-start pose exactly),
`apply_drag_surface_step()` runs `Transform_tool::snap_to_surface()`:

- It acts on the drag's targets that are meshes (kit pieces or not), with the
  meshes' own bounds (`mesh_local_bounds()`). Nodes without a mesh, nodes
  locked against viewport transforms, bodies pulled through physics and the
  mesh component modes are left as the tool computed them.
- A target whose ancestor is also a moved target is not snapped: it rides
  rigidly on the ancestor, as every translate step moves it
  (`doc/editor/transform.md` "Rigid descendants").
- Every target and everything under it is excluded from the rays, so moved
  pieces stand neither on themselves nor on each other.
- The raytrace scene is committed once per step; each mesh then casts the rays
  of "Surface placement" from the pose the tool computed for the step, so
  orient starts every step from the rotation at the drag start and tilt does
  not accumulate. A mesh that finds no surface keeps the tool's transform.
- The gizmo anchor moves by the average move of the targets (a ridden target
  counts its ancestor's move), so the handles stay with the selection.
- The step allocates nothing (scratch members); the trace line "surface step:
  N entries, M snapped: T ms" of `editor.transform_tool` times it. A step over
  50 pieces takes about 1.7 ms (headless release build).

Rules:

- Axis rule: a pointer drag whose only free axis is parallel to the surface
  direction (the Y arrow, with the default direction down) is left as the
  tool computed it, so a piece lifts off a surface. Axis arrows along other
  axes and the plane handles snap.
- Only translate drags snap to the surface: rotate and scale drags are left as computed
  (the scale tool has three write paths, and orient would fight a rotation).
- The drag is one undo entry, recorded at release from the snapped transforms;
  its undo restores the transforms from before the drag, rotation included.
- The gizmo reads the pair when the drag starts. The Transform window's
  numeric edits do not snap to the surface.

MCP calls take explicit arguments instead of the UI pair
(`doc/agents/mcp_api_guidelines.md`):

- `transform_selection {..., surface_snap, surface_orient,
  surface_max_distance}` (default false, false, 1 m): after all edits of the
  call, `snap_to_surface()` with those options; they need a translation of a
  node selection. The reply adds `surface_snap`, `surface_orient`,
  `surface_max_distance`, `surface_found_count` and per node `surface_found`.
- `drag_selection {..., surface_snap, surface_orient, surface_max_distance}`
  (default false, false, 1 m,
  read when the drag starts; refused on a rotate or scale drag): every step of
  the translate drag snaps to the surface as a gizmo drag does, without the axis rule. The
  reply adds both options and per node `surface_found` and
  `world_rotation_xyzw`.
- `set_node_transform` does not snap to the surface.

### Connector snap

With Connector Snap above 0, a translate, rotate or scale drag of the gizmo
moves the dragged nodes so that a connector of theirs meets a connector of
another kit piece when the two are within the distance (R5.13, D3.13, U27,
`level/kit_connector_snap.hpp`); the drag goes on uninterrupted:

- Moved set: the drag's targets that are not locked against viewport
  transforms and have no moved ancestor (the moved roots). Their connectors
  are the enabled connectors ("Connector switches") of every kit piece at or below a moved root, so a dragged group
  or layout node snaps by its pieces' connectors. The moved connectors are fixed at drag
  begin in their root's frame (`connector_in_root()`, `level/kit_connector.hpp`),
  so each step carries them with the root's pose of that step, its rotation
  and scale included, and the children a locked layout regenerates during
  the live preview snap by their pre-drag positions.
- Targets: the enabled connectors of every kit piece of the scene that is not a drag target or below
  one, children of locked kit layout nodes included. A selected piece locked
  against viewport transforms is neither moved nor a target.
- Every piece's connectors are taken through its full world matrix, so
  scaled moved pieces, scaled targets and pieces under scaled or sheared
  parents take part (U22); the snap co-locates the transformed positions, by
  translation only unless Connector Snap Orientation turns the set
  ("Orientation" below). A piece whose frames degenerate takes no part.
- Begin: `Transform_tool::begin_connector_snap()` at the drag's start (after
  the physics drag begins) gathers the moved connectors in the frame of their
  moved root and, per target piece, a weak node reference and its local
  connector positions, into persistent scratch (`Kit_connector_drag_snap`).
  A drag with a body pulled through physics does not snap: the simulation
  writes that node, so the set could not move by one offset.
- Step order: the Move tool's grid snap, the tool's translate, the snap to
  surface ("Transform tool" above), then the connector snap, then the live
  layout preview, so the preview and the release take the snapped pose. The
  connector snap comes last and is not snapped to the surface again, because
  co-located connectors define the pose; a snap to surface after it would
  part them again. A rotate or scale step is the tool's rotation or scale
  (after the Rotate tool's angle snap), then the connector snap, then the
  live layout preview; it has no snap to surface.
- Rotate and scale drags (U27): the rotate ring and arcball, and the scale
  axis, plane, uniform and box face handles. The step's rotation or scale is
  about the pivot fixed at the press (the rotation center, the scale
  center or the box face opposite the dragged one, all where they were at the
  press) and applies to the drag-start pose, then the moved set translates
  by the pair's offset; the snap never changes the step's rotation or scale.
  So while a step is snapped the pivot does not stay put: the rotation
  center moves with the set, and the box's opposite face moves off its press
  position by the offset. The gizmo anchor translates with the set, but the
  next step again rotates or scales the drag-start pose of every entry about
  the press pivot (the box scale's per node path first puts every entry back
  at its drag-start pose, the ones it does not resize included), so no
  offset carries into a later step.
- Scaled and mirrored moved sets: the moved connectors follow the root's
  full world matrix of the step (U22: non-uniform scale and the shear it
  makes over a turned root, a mirror; outward stays the face normal), as the
  unit tests `Kit_connector_drag_carry` check. A step whose scale collapses
  a root's axis to zero (a degenerate frame, `is_degenerate_frame()`) takes
  that root's connectors out of the search: the step stays unsnapped unless
  another root's pair is in reach. Editor runs do not drag a placed piece to
  a negative scale (that aborts the Debug build in `draw_list_scene`,
  `doc/plans/open_items.md`); the mirror is covered by the unit tests.
- Each step carries the moved connectors by their root's world transform and
  the targets' by theirs (expired or hostless nodes skipped) and
  `find_connector_snap()` takes the closest (moved, target) pair whose
  distance is at most the setting (inclusive); equal distances take the
  smallest (moved piece id, moved connector index, target piece id, target
  connector index), so the choice does not depend on gather order. Any pair
  counts, whatever their outward axes. Every moved root then translates by
  the offset that puts the moved connector on the target's (translation only,
  unless "Orientation" below turns the set), and the gizmo anchor with them. A snap that cannot
  co-locate is not made: when a moved root's locked translate channel
  (`lock_translation_x` / `_y` / `_z`, in its parent's space) would hold back
  part of the offset, the step stays unsnapped and reports so.
- Every step starts from the unsnapped pose of that step, so there is no
  latch: the drag snaps again when it comes back and unsnaps when the
  unsnapped pair is farther than the distance.
- Motion first (U27): the snap starts only after the pointer has moved. Steps
  look for no pair until one step of the drag has a nonzero pointer motion
  since the press (`Transform_tool::note_translate_drag_motion()`: the Move
  tool's projected pointer position minus the press point, before the grid
  snap; for `drag_selection` the step's translation;
  `Transform_tool::note_node_drag_motion()` for the others: the Rotate
  tool's ring angle before the angle snap or an arcball displacement, the
  Scale tool's factor other than 1, its uniform drive or a moved box face,
  and for `drag_selection` a nonzero angle or a scale other than 1); from
  then on every step may snap, a drag out and back to the press point, under
  half a move grid step or snapped back to angle 0 included. A press and release on a handle changes nothing and records
  nothing ("Undo record" of `doc/editor/transform.md`); `transform_selection`
  counts its edits as motion.
- Stay on axis (U27): with Connector Snap On Axis (`stay_on_axis`, read when
  the drag starts) an axis handle drag applies only the offset's component
  along the dragged axis and a plane handle drag only its part in the dragged
  plane (`constrain_connector_snap_offset()`, `level/kit_connector.hpp`), in
  the basis the Move tool drags in (`Subtool::get_basis()`: global, or the
  anchor's orientation in local, reference and selection space), so the piece
  stays on its axis or plane. The candidate is still the closest pair by
  their full distance within the setting, and the pair may stay apart (an
  offset across the axis applies nothing). So a near pair lying across the
  axis wins over a farther pair along it: the step reports `snapped` with an
  applied offset near zero, and the pair along the axis does not snap. A drag without a handle
  (`drag_selection`, `transform_selection`) and `full_offset` apply the full
  offset. The setting constrains translate drags only: a rotate ring or a
  scale handle names no direction to translate along, so a rotate or scale
  drag applies the full offset (`constraint` `free`) whatever the setting.
  The locked channel rule tests the applied offset.
- Suspend key (U27): while a key of the command
  `Transform_tool.suspend_connector_snap` is down (left or right Shift by
  default, any other modifiers; bindable in the Input Bindings window,
  `doc/editor/input_bindings.md`) the steps of a pointer drag do not snap.
  Shift is the default because it has no meaning during a Transform node
  drag, also when held at the handle press (the drag binding takes any
  modifiers; the Shift selection gestures belong to the mesh component modes,
  where the connector snap does not run) and in ImGui; Alt is not, because
  an Alt press and release with no active item toggles ImGui's menu layer on
  the viewport window (`ImGuiWindowFlags_MenuBar`), and some X11 window
  managers take Alt + drag.
  - Held state: read at each step from the live key state
    (`Input_state::key_down` and `modifier_mask`, which `Editor` updates from
    every key event before ImGui or the commands see it, whichever window
    owns the keyboard) against the command's effective bindings
    (`Transform_tool::is_connector_snap_suspend_held()`; the bindings are
    copied when the bindings change). A key released over another window, or
    with the window unfocused (SDL sends the releases at focus loss), so ends
    the suspend for the next step.
  - Keys only: the command refuses any other binding kind
    (`Command::accepts_binding()`), with the reason shown in the Edit Binding
    modal and by `set_command_bindings`, since only a key has a held state.
  - Key edge: the command's key events (reaching it only while the viewport
    owns the keyboard; it never consumes them) re-run the drag's last step
    when the held state differs from the one that step used
    (`Transform_tool::on_connector_snap_suspend_key_edge()`: a translate drag
    applies its last translation again, a rotate or scale drag runs the
    subtool's update from the unchanged pointer, the same unsnapped pose), so
    the pose snaps or unsnaps at the edge with the pointer still. A drag that
    has not moved does not re-run.
  - Escape cancels with the suspend key or any other modifiers held
    (`doc/editor/transform.md` "Drag cancel").
  - Scripted drags read no key state. Over MCP the key is injected
    (`mouse_drag {modifiers: ["shift"]}`, a `key` event of
    `inject_input_events`).
- Orientation (U44): with Connector Snap Orientation `align` (read when the
  drag starts) a snapped step of a translate drag also turns the moved set,
  so the pair's frames face each other as a connector join does
  (`join_connectors()`, "Connectors" above: outward axes opposed, up axes
  agreeing after a quarter twist about the target's outward axis, no gap).
  - The motion: the moved connector's world frame (its root's frame of the
    step, `transform_connector()`) and the target's give, through
    `align_connector_frames()` (`level/kit_connector.hpp`), one rigid world
    motion, a turn about the moved connector followed by the full offset;
    every moved root takes it, so a multi-selection or a group turns rigidly
    about that connector, each node keeping its stored scale (a mirror
    included) as a join writes it (`make_joined_local_transform()`), and the
    gizmo anchor with them.
  - The twist: of the join's four quarter twists the one whose turn is
    smallest (`closest_join_twist()`: the largest quaternion |w|; a later
    twist wins only by more than 1e-6, so equal ones take the smaller twist),
    so the piece does not spin about the pair's axis.
  - Translation only, reported in `alignment`: a rotate or scale drag
    (`off`: the setting applies to translate drags only; the step's rotation
    or scale stays the tool's), an axis or plane handle drag with Connector
    Snap On Axis (`on_axis`: the constrained offset, no turn), a moved root
    whose locked rotate channel would hold back the turn or whose locked
    translate channel the motion moves (`locked`), a moved root whose
    resulting local transform would be sheared, under a parent with
    non-uniform scale or with a skew of its own (`shear`,
    `has_orthogonal_axes()`, the refusal a join under such a parent gives),
    and a pair frame that degenerated since the drag began, a target scaled
    to zero (`degenerate`). The step then snaps by the offset as without the
    setting.
  - With Snap To Surface and Orient To Surface on as well, the connector
    alignment comes after the surface step and replaces the surface
    orientation of the snapped step.
  - Every step starts from its unsnapped pose, so the drag-start orientation
    comes back when the pair leaves the distance, the suspend key is down or
    Escape cancels; the release records the turned pose in the drag's one
    undo entry.
- The drag is one undo entry recorded at release with the pose of its last
  step, by the rule of `doc/editor/transform.md` "Undo record".
  Escape (`doc/editor/transform.md` "Drag cancel") restores the drag start.
- Not applied to IK bone drags, mesh component edits and the Transform
  window's numeric edits.
- Cost: a step allocates nothing; the trace lines "connector snap begin: ..."
  and "connector snap step: ..." of `editor.transform_tool` time the gather
  and the steps (the clock is read only with that level on). With 6 moved
  and 1212 target connectors (201 pieces) the gather takes about 1.5 ms and
  a step about 0.33 ms (headless Debug build).

MCP (explicit arguments, default off, never the UI setting):

- `drag_selection {..., connector_snap_distance}`: read when the drag starts,
  for a translate, rotate or scale drag. Every reply (drag, move, release) adds
  `connector_snap`: `active`, `moved_connectors`, `target_connectors`,
  `snapped`, `skipped` (`none`, `before_motion` or `suspended`), and when
  snapped `distance` (in the unsnapped pose), `constraint` (`free`, `axis` or
  `plane`), `full_offset` (the pair's), `offset` (the applied one),
  `piece_id`, `piece`, `connector`, `target_id`, `target`,
  `target_connector`, `alignment` (`off`, `aligned`, `on_axis`, `locked`,
  `shear` or `degenerate`), `rotation_xyzw` and `motion_translation` (the rigid world motion
  of an aligned step, the rotation first; identity otherwise) and
  `twist_steps`.
- `drag_selection {..., connector_snap_orientation}` (`keep`, the default,
  or `align`; never the UI setting): read when the drag starts; `align` is
  refused on a rotate or scale drag and without `connector_snap_distance`. The reply echoes it.
- `transform_selection {..., connector_snap_distance}`: one snap after the
  call's edits and snap to surface; it needs a translation, rotation or scale
  of a node selection. `connector_snap_orientation` `align` turns the set as
  a translate drag does; it needs `connector_snap_distance` and a translation
  without a rotation, scale or skew (refused otherwise).
  The reply adds `connector_snap_distance`, `connector_snap_orientation` and
  `connector_snap`.
- `get_connector_snap {}` reads `connector_snap` of the current or last drag
  of any kind (a pointer drag included); the last step's pair stays readable
  after the release or cancel.

### Connector indicators

Connectors are drawn in the viewports (U22, D3.14,
`level/kit_connector_indicators.hpp`,
`Debug_visualizations::connectors_visualization()`): per connector a small
square across its outward axis at its world position, an arrow along the
outward axis and a tick half as long along the up axis, sized per meter of
distance to the camera (screen-constant) and drawn x-ray, as the curves are,
so a connector on a face stays visible. A disabled connector is not drawn
("Connector switches").

- Which: the Debug Visualization popup's Connectors (per view, saved with
  the view's settings): Off, Selected (the default: the connectors of the
  selected kit pieces and of the kit pieces below selected groups and
  layout nodes) or All (every kit piece of the view's scene). The two
  connectors chosen in the Level Kit window's Connect rows (Target
  Connector, Moved Connector) are drawn in the chosen color.
- During a translate, rotate or scale drag with Connector Snap above 0, in
  the views of the drag's scene, whatever the setting: the moved set's
  connectors at the step's pose (turned and scaled with it), the target connectors within 4 snap
  distances of a moved one and, in its own color, the pair the step snapped
  by; the highlight ends with the drag.
- Colors and size: Settings > Debug Visualizations Style > Connectors
  (`connector_color`, `connector_chosen_color`, `connector_moved_color`,
  `connector_target_color`, `connector_snapped_color`, `connector_size`,
  `connector_width`).
- Cost: the enabled local frames are cached per piece and read again only after a
  change of its record or switches (parameters, replace, a switch edit, undo of each, a raw
  write); a removed piece's entry goes, and a scene close empties the cache; world matrices are read at draw time, a drag
  reuses the snap's gathered points, and a steady frame allocates nothing.
- No connector name labels.
- MCP: `level_set_connector_visualization {mode, viewport}` sets the
  setting (off, selected, all); `level_get_connector_indicators {viewport}`
  returns what that viewport draws this frame, collected as the drawing
  collects it: `viewport`, `scene_name`, `mode`, `drag_active`,
  `cached_pieces` (entries of the frame cache), `count` and `indicators` (name, world position, outward, up, piece_id, role piece,
  chosen, moved, target or snapped, and the role's color).

## Persistence

A level saves and loads as any erhe scene (`doc/editor/scene_serialization.md`).
The file holds each piece as a node with an ordinary glTF mesh of its own, so a
viewer without the erhe extensions shows the right geometry (R8.2), and the
node's `ERHE_node` properties carry the `Kit_piece` record. What a piece is right
after the file is read:

- its record, transform, name and a mesh primitive per slot with that slot's
  material (the palette materials come back by name, so a placement after the
  load finds them and adds none);
- a static body with a collision shape `KHR_physics_rigid_bodies` rebuilt from
  the piece's own saved mesh (a convex hull or a triangle mesh), one per piece;
- a mesh of its own: pieces of equal values no longer share one, and a changed
  shape definition would not reach it (R8.1, R8.3);
- no `Brush_placement.brush` (`doc/editor/scene_serialization.md` "What is not
  persisted").

So `finish_open_scene_gltf()` (`parsers/gltf.cpp`), the tail of every
erhe-authored glTF open (`load_scene`, File > Load Scene, `--scene`, the
asynchronous load included), calls `Level_kits::refresh_scene()` once the nodes
are in the scene. It queues the scene's pieces, and
`Level_kits::update_scene_refreshes()` refreshes them over the following
frames: each frame refreshes the pieces in order until the next one would
make a new brush, at most one of those, so a level of many distinct pieces
does not stall one frame while their brushes are made, and a run on the
deterministic clock refreshes the same pieces on the same frames. Until its
turn a piece shows its loaded mesh, the same geometry. The pending pieces count
as work in flight (`App_context::get_async_in_flight_count()`): `get_async_status`
is not idle, a save waits, and viewport hover and picking are gated. A piece
that follows its kit by then (an edit gave it its brush) is left as it is, and
closing the scene drops its queue.
Once the last piece of a scene is refreshed the navigation item's input check
runs. For each node carrying `Kit_piece` the refresh reads the record (`read_piece_values()`: known shape,
readable text, values in range) and gives the node the cached brush of those
values the way a parameter change does, without an undo entry: the brush's
part primitives in place of the loaded ones (each primitive keeps its slot's
material), the
brush's collision shape (the body keeps its motion mode and settings and is
rebuilt around the shape) and `Brush_placement.brush`. The collision shape is
replaced even when the kit is unchanged, because R8.3 makes it follow the kit
and the brush's one shape is shared by all pieces of the mesh key. A prepared
run keeps the meshes it was built with.

A piece whose shape the kits no longer have, whose parameters do not read or
are out of range, or whose loaded mesh has another primitive count than the
shape has slots (a file saved before the shape had them: not migrated), keeps
the loaded mesh and collision shape (R8.4) and is named
in one warning of the `editor.scene` log (`Kit piece '<name>' (id <id>) keeps
its loaded mesh: <problem>`); one info line counts the pieces that follow and
the ones that do not. `Level_kits::check_piece()` reports per piece
`follows_kit` (its mesh primitives are the kit brush's part primitives and its
`Brush_placement.brush` the cached brush of its record) and the `problem`;
`level_get_pieces` returns both.

## Level settings

`Level_settings {module = 1.0, wall_height = 3.0}`
(`scene/definitions/level_settings.py`) is the optional field `level` of
`Scene_settings` (version 4); a scene without it uses the defaults.
`Level_kits::get_lengths(scene_root)` reads it. MCP `set_scene_settings`
writes it (`settings: {"_version": 4, "level": {"_version": 1, "module": ..,
"wall_height": ..}}`). Changing the lengths leaves placed pieces as they are.

## Level Kit window

`Level_kit_window` (`level_kit_window.hpp`, Window > Level Kit, hidden until
opened) acts on the scene of the last hovered viewport, whose name it shows; it
shows nothing else until a viewport has been hovered. Pieces are placed in the
viewport by the Brush tool in kit mode (`doc/editor/brushes.md` "Kit mode", D4.4).
Top to bottom:

- Level Settings: Module and Wall Height. A drag commits when it ends, writing
  the scene's `Scene_settings.level` in place (creating it on the first edit),
  as the Properties window and `set_scene_settings` write scene settings.
- One collapsing section per kit, one row per shape (the tooltip is its id and
  description), a recreated shape's row directly after its original's.
  Choosing a shape fills the parameter rows with its defaults at
  the scene's lengths, makes the Brush tool the active tool and hands it the kit
  brush of those values: the Brush tool is then in kit mode. The row of the
  shape the Brush tool places shows as chosen; the window reads that from the
  tool when it draws. Under the rows a line names the bindings: by default a
  right click in a viewport places the piece, Z and X turn it (the text follows
  the effective bindings of `Brush_tool.insert`, `Brush_tool.rotate_cw` and
  `Brush_tool.rotate_ccw`, `doc/editor/input_bindings.md`).
- The parameter rows of the chosen shape: a drag per float parameter (its
  range, its unit after the value), a drag per integer, a combo per enum, the
  description and range as tooltip. Stairs add a line with the step count, step
  rise and tread depth (R3.5). An edit hands the Brush tool the kit brush of the
  new values when its drag ends, a value is typed or an entry is chosen; values
  `validate()` refuses show the message in red and the tool keeps the brush it
  has. Under them one combo per material slot, `<slot> material` (ImGui id
  under the slot's name, so a row keeps its id whatever other rows show),
  offering the slot's default palette material and its alternatives
  ("Materials"); a choice hands the Brush tool the same brush with the new slot
  materials, which its preview shows and its next piece gets. Reset To
  Defaults fills the rows again, the slots' defaults included, and hands the
  tool their brush.
- Snap To Module, Snap To Surface, Orient To Surface, New Seed Per Piece and
  Rotation Step (90, 45 or 15 degrees): the editor setting `level_kit`
  (`Level_kit_config`, `editor_settings.json`; on, off, 90) for
  the module snap, the seed and the step, and the shared pair `surface_snap`
  for the surface options ("Surface settings").
  With New Seed Per Piece each click's piece is followed by
  `Brush_tool::take_next_kit_seed()`: the tool's kit values take
  `kit_next_seed()`, derive their masked values at the scene's lengths and hand
  the preview the brush of the next piece; the tool sends
  `Kit_values_changed_message` and the window's rows take its values.
- Selection: for the selected piece of the scene (the active item when it is
  one, else the first selected piece) its name and shape, its parameter rows
  ("Parameter rows") and the slot material combos as above, each showing the
  material of that slot's mesh primitive; a choice queues
  `Level_kits::set_slot_material()` with the palette material, one undo entry
  at the edit (`Imgui_windows::queue()`). The combos show only while the
  piece's mesh has one primitive per slot. Snap Selected To Surface and
  Orient Selected To Surface run `Level_kits::snap_to_surface()` - the code
  path of `level_snap_to_surface` - over the selected meshes of the scene, one
  undo entry. Replace With (a combo of every shape, shown as
  `<display name> (<id>)`) and Replace Selected: the press queues
  `Level_kits::replace_shape()` ("Replace") for the selected nodes of the
  scene with the Snap To Surface and Orient To Surface settings and shows the
  result (`make_replace_summary()`: the replaced count, how many snapped, and
  each skipped node by name with its reason) or the refusal under the
  button, logged as a `Level Kit:` line; the button is disabled, its reason shown, while no kit
  piece of the scene is selected or no shape is chosen. The count of selected
  pieces is taken again on the selection and active item messages, which
  also clear the shown result. Connect ("Connectors"): the target is the
  active item when it is a selected kit piece of the scene, else the first
  selected kit piece; with exactly two kit pieces selected the other one moves.
  Target Connector and Moved Connector choose the two connectors by name
  among the pieces' enabled ones ("Connector switches"),
  Twist Steps (0 - 3) and Gap are the join's, and Connect Selected queues
  `connect_piece()`. New Piece Connector (a connector of the shape to place,
  the first by default) and Place Connected queue a `place()` of the shape to
  place, with its parameter rows' values and slot materials, joined to the
  target connector, and select the new piece, so repeated presses build a run.
  Each press shows its result or refusal under the buttons, logged as a
  `Level Kit:` line. The pieces and the connector names of their records are
  read again on the selection, active item and mesh messages, the new piece's
  names when a shape is chosen; a join the press refuses (a sheared local
  transform) shows its message. The chosen target and moved connectors are
  drawn highlighted ("Connector indicators").
- Layout (R5.12.9, `Kit_layout_initial_editor` in `kit_layout_editor.hpp`):
  the initial values of a kit layout node the author adds to the scene. They
  edit no node: a selected layout node's values are edited in the Properties
  window ("Kit layout rows"). The rows: the pattern rows of the shown kind
  ("Kit layout rows", every kind offered: Line, Grid, Ring, Scatter, Path,
  Segments and Stairs, each kind keeping its values; a stairs layout's
  values ride the pattern text), Spacing From Item (on by default: the new
  layout's line step and grid spacings are its item's extent, so Step,
  Spacing X and Spacing Z show disabled), Module Snap, Surface Snap, Surface
  Max Distance (1 m by default), Surface Orient, Per Piece Seed, Layout Seed,
  for a path or segments kind the seven connector switches (off by default)
  and Locked (on by default). They are the
  editor setting `level_kit` (`Level_kit_config` version 8, the pattern as
  its canonical text in `layout_pattern`, the switches as
  `layout_hide_<all|plus_tangent|...>_connectors`; a version 6 file's
  `layout_random_seeds` reads as `layout_per_piece_seed`), read once and written at each
  edit, as the placement settings are. The window adds layout nodes with
  them (`Level_kits::get_layout_initial_values()`), and the new node is
  selected, so its values are at once editable in the Properties window:
  - Add <Kind> Layout, for the stairs kind Add Path Stairs (U46; needs a
    scene only, whatever is selected), queues `Level_kits::add_layout_of_kind()`: a
    new layout node of the shown kind at the scene root's origin with the
    values above, its one item the shape the window places (its parameter
    rows' values and slot materials) or, with no shape chosen, a
    `basic.block` at its defaults; a stairs layout has no item. Named as the
    Create menu names it ("Create menu"). One undo entry.
  - Make Layout (disabled without a selected kit piece, as Selection finds
    it, and for the stairs kind, which places no piece) queues
    `make_layout_from_piece_initial()`: the piece's shape, values
    and slot materials as the one item, the piece replaced by element 0; a
    path layout is a Curve node straight along the piece's x axis. One undo
    entry.
  - Make Layout On Curve (enabled for a path, segments or stairs kind with a
    selected Curve node that has no children and no layout values) queues
    `Level_kits::make_initial_layout_on_curve()`: the curve becomes a layout
    of the kind with the values above, laid out along its points as they
    are (`make_layout_on_curve()`), the item as for Add. One undo entry.
  - Place As Layout (the `level_kit.place_as_layout` setting): a Brush tool
    click in kit mode places, in place of one piece, a kit layout node of the
    shape the window places (its parameter rows' values and slot materials as
    the one item) at the preview's pose, element 0 where the preview showed
    the piece (`add_layout_initial()`), named after the shape followed by
    " Layout", one undo entry. The preview shows the one piece.
  The last result or refusal shows under the rows.
- Group ("Groups"): Group Name (default "Group") and Group Selected, which
  groups the selected nodes of the scene and selects the new group node; for
  the selected group node (the active item when it is a selected `Xform`,
  else the first selected one) its name and child count, Ungroup, Duplicate
  Offset (the group's width along x, taken again when another group becomes
  the selected one or a mesh changes; a typed offset stands until then),
  Duplicate, which selects the copy, and Mirror X and Mirror Z. A press
  queues its `Level_kits` call; the queued call writes the result, the names
  of the pieces a mirror does not show exactly, or the refusal in red under
  the buttons.
- Spatial Queries ("Spatial queries"): Support Report runs `find_support()`
  over the selected meshes of the scene, or over every kit piece when none is
  selected, at the default tolerance, and shows the count, the unsupported
  meshes as rows that select the clicked mesh, and Select Unsupported, which
  selects them all. Region Contents runs `find_region_contents()` for the box
  of the selected meshes' world bounds and lists the meshes found the same
  way. Height Map runs `make_height_map()` over those bounds grown by one
  module on every side, one cell per module, the rays from the grown top to
  the grown bottom, and shows the text picture, with a navigation mesh also the island
  picture, and a line naming the grid, the heights of digits 0 and 9 and a
  stale navigation mesh. Exclude Tags takes comma separated tags (as the
  Properties window's Tags row). Overlap Report runs `find_overlaps()` over
  the selected meshes, or every kit piece without a selection, at the default
  tolerance with those tags left out, and shows the counts and a row per pair
  (names, depth, a measure other than exact) that selects both meshes. Level
  Check runs `run_level_check()` at the defaults with those tags left out and
  shows the counts, the navigation mesh's state and a row per finding (kind
  and detail) that selects its pieces. A press runs its query in the queued
  call (`Imgui_windows::queue()`); the result stays until the next press, the
  meshes held weakly, and a removed mesh shows as "(removed)".

The hand-overs and the row commits run through `Imgui_windows::queue()`
(`doc/editor/coding_rules.md` "ImGui-time work"), because a new brush uploads
its mesh.

In kit mode the Brush tool keeps its hover, preview and insert machinery and
takes the piece's transform from `make_placement_pose()` ("Surface
placement"): the point is the hovered point on content, else the grid-snapped
point on the grid; the rotation steps count the tool's rotate commands and
start at zero when another shape is chosen. The preview mesh carries one
primitive per slot, each with a material of the color of the palette material
the window chose for that slot (`Level_kits::get_preview_material()`, outside
every content library), and sits at exactly that pose; it is no surface
for the rays, while placed pieces are, so a piece placed over a placed block
rests on it. A click places one piece at that pose through `Level_kits::place()`
(without a further snap): its record, the chosen palette material of each slot
and its static body, one undo entry. The tool keeps the selection, as its face-aligned insert does, and
stays in kit mode for the next piece.

## MCP

- `level_get_kits {scene_name}`: the kits and, per shape, its id, `recreates`
  (the original's id, empty for an original: "Recreated shapes"), kind,
  description, tags, parameters (type, unit, range, the default resolved
  against the scene's level settings, its scale and factor, an enum's values;
  `derive` `fixed` or `derivable`, `default_derived`, and for a derivable float
  or int `derive_min` and `derive_max`, a float's resolved to meters with its
  `derive_step`), `default_derive` (the default mask's names),
  `default_size` and `default_bounds` of the mesh at the defaults (default
  mask, default seed), `material_slots` (in slot order: `slot`,
  `default_material` and `alternatives`, palette names), `connectors` (at the
  defaults, in the piece's local space: `name`, `position`, `outward`, `up`),
  `light_frames` (at the defaults: `name`, `position`, `direction` and the
  starting values, "Lights on pieces"), normal style, triangle budget, collision, `seeded` (the generator draws
  from the seed) and `mirror_planes` ("Groups"); the scene's level lengths and the `palette` (`name`,
  `base_color`, `roughness`, `metallic`, `emissive`).
- `level_place_pieces {scene_name, snap, surface_snap, surface_orient,
  surface_direction, surface_max_distance, pieces: [{shape, parameters, position, rotation_xyzw,
  name, materials, parent_id, parent_index, surface_snap, surface_orient}]}`:
  one undo entry. `materials` maps slot names to material names: a material
  of the scene, else a palette material (added to the scene the first time it
  is needed); a slot not named takes its default. `surface_snap` and `surface_orient` are booleans (default
  false) a piece may override; `surface_direction` defaults to `[0, -1, 0]` and `surface_max_distance` to 1 m. `parameters.derive`
  lists the parameters derived from the seed, replacing the default mask; the
  masked values are derived at the scene's lengths. A value given for a masked
  parameter, or a name in `derive` that is not a derivable parameter, fails
  the call naming it.
  Unknown keys, an unknown shape or parameter, a value of the wrong type or
  out of range, a rotation that is not a unit quaternion
  (`make_unit_quaternion()`), a zero direction, a slot the shape does not have
  or a material neither in the scene nor in the palette fails the
  call with the piece index, and nothing is placed. The reply lists per piece
  `id`, `name`, `shape`, the effective `parameters` (with `derive`, the masked names), the placed `position` and
  `rotation_xyzw`, `surface_found` (false also without a surface option),
  `world_aabb`, `triangle_count`, `materials` (slot name to material name),
  `brush_id` and `mesh_key`;
  pieces of equal `brush_id` share one mesh.
- `level_group_nodes {scene_name, node_ids, name}` ("Groups"): `group()`;
  `name` defaults to "Group". The reply: the group's `id`, `name`,
  `position`, `parent_id`, `count`, `piece_count` and `nodes`, the group and
  every node below it, parents first, each with `id`, `name`, `kind`
  (`group`, `piece`, `mesh` or `node`), `parent_id`, world `position` and
  `rotation_xyzw`, `world_aabb`, for a piece `shape`, `parameters_text` and
  `materials`, `brush_id` and `physics {collision_shape, has_body}`.
- `level_ungroup {scene_name, group_id}`: `ungroup()`. The reply:
  `parent_id`, `child_count` and `nodes`, the former children and their
  subtrees as above.
- `level_duplicate_group {scene_name, group_id, offset | position, name}`:
  `duplicate()`, exactly one of `offset` and `position`. The reply is that of
  `level_group_nodes` for the copy, plus `source_id`.
- `level_mirror_group {scene_name, group_id, axis}`: `mirror()`, `axis` `x`,
  `y` or `z`. The reply: `axis`, the world `plane_point` and `plane_normal`,
  `count`, `not_exact_count`, `not_exact_ids` and per node `id`, `name`,
  `kind`, the new world `position` and `rotation_xyzw`, `local_plane` (the
  node's own plane used), `exact_mirror` and `world_aabb`.
  A group tool refuses an unknown key, a `group_id` or node id that names no
  node of the scene or names the scene root, and what the `Level_kits` call
  refuses, changing nothing. Moving and deleting a group use
  `set_node_transform`, `transform_selection` and `delete_nodes`.
- `level_create_layout {scene_name, name, parent_id, position,
  rotation_xyzw, pattern, items | from_piece_id | curve_id, points, closed,
  snap, surface_snap, surface_orient, surface_max_distance, per_piece_seed, seed,
  connector_hiding, locked}` (`random_seeds`, the former name of `per_piece_seed`, still reads;
  both together are refused): `create_layout()` ("Kit layout nodes"), locked by default; with
  `from_piece_id` `make_layout_from_piece()`, as the Level Kit window's Make
  Layout, which takes the item, parent, place and world transform from the
  piece (not combined with `items`, `position`, `rotation_xyzw` or
  `parent_id`; the reply adds `replaced_piece_id`); with `curve_id`
  `make_layout_on_curve()`, which makes that plain curve node the path layout
  itself (not combined with `from_piece_id`, `position`, `rotation_xyzw`,
  `parent_id`, `name`, `points` or `closed`). `pattern` is an object of the
  pattern's keys (`kind`, `count_mode`, `count`, `gap`, `direction`, `step`,
  `length`, `count_x`, `count_z`, `spacing_x`, `spacing_z`, `radius`,
  `arc_degrees`, `facing`, `orientation`, and a scatter's `scatter_mode`,
  `density`, `region`, `size_x`, `size_z`, `region_radius`, `min_spacing`,
  `yaw_min`, `yaw_max`, "Scatter layouts", and `stairs`, an object of a
  stairs layout's values, "Stairs layouts") over the defaults, `step` and the
  grid spacings defaulting to the largest item's extent. A path layout is a
  Curve node: `points` (as `create_curve` takes them, in the node's frame)
  and `closed` (which needs `points`) give its curve, else it is straight
  along its x axis; they are refused for another kind. A stairs layout takes
  `points` the same way (else a flight of ten steps along its x axis) and
  needs no `items`. `items` is a list of
  `{shape, parameters, materials, weight}` read as a `level_place_pieces` piece reads
  them, `weight` an integer in 0 - 1000, 1 when not given ("Scatter
  layouts"). `connector_hiding` is an object of the switches given, merged
  over the node's (or the defaults): `all`, `plus_tangent`, `minus_tangent`,
  `plus_normal`, `minus_normal`, `plus_binormal`, `minus_binormal`, each a
  boolean (true hides); an unknown key or a non-boolean is refused. The reply: the node's `id`, `name`, `type` (`Curve` for a path
  layout, else `Xform`), world `position` and `rotation_xyzw`, `curve` (a
  path layout's own curve: `point_count`, `closed` and `length`; null for an
  Xform), `locked`, `pattern`, `pattern_text`, `items` (`shape`,
  `parameters`, `parameters_text`, `materials`, `weight`, `extent`,
  `bounds` (the item's mesh box in its own frame, from its CPU geometry),
  `footprint_radius`), the snap and seed
  values, `connector_hiding` (all seven), `run {count, requested, step, space, item_extent}` (or
  `{problem}`; `requested` is the count the values ask for, more than `count`
  when a scatter's spacing lets fewer in),
  `child_count` and
  `children` (`id`, `name`, world `position` and `rotation_xyzw`, `shape`,
  `parameters`, `parameters_text`, `materials`), and `changed`,
  `kept_count`, `added_count` and `removed_count`.
- `level_set_layout {scene_name, layout_id, pattern, items, snap,
  surface_snap, surface_orient, surface_max_distance, per_piece_seed, seed,
  connector_hiding}` (`random_seeds` as above): `set_layout()`; the
  values given merge over the node's (the pattern key by key, the items as a
  whole list). The reply as above.
- `level_get_layouts {scene_name}`: every kit layout node as above; a node
  whose values do not read reports its `problem`. A path layout is a curve
  node as well: `get_curve`, `get_curves`, `set_curve`, `curve_add_point`,
  `curve_remove_points`, `curve_set_point_kind`, `select_curve_points` and
  `evaluate_curve` take its id.
- `level_set_layout_lock {scene_name, layout_id, locked}`:
  `set_layout_lock()`.
- `level_refresh_layout {scene_name, layout_id}`: `refresh_layout()`.
  A layout tool refuses an unknown key (the former pattern keys `points` and
  `curve_id` among them), an unknown kind (the former `polyline`), a kind
  not of the node's type, a `curve_id` that names no curve node of the scene
  or a curve that is a layout already or has children, an unknown
  orientation, scatter mode or region, a `layout_id` that names no kit layout node, and what the
  `Level_kits` call refuses, changing nothing.
- `level_wrap_with_path {scene_name, node_ids}`: `wrap_with_path()` ("Wrap
  with Path"), the Scene Hierarchy window's entry; `node_ids` default to the
  scene's selected nodes. The reply is `level_create_layout`'s for the new
  node plus `scene_name` and `wrapped_piece_ids` (the ids given). It refuses
  an unknown key, an empty or non-id `node_ids`, an id that names no node of
  the scene, no selected node without `node_ids`, and what `wrap_with_path()`
  refuses, changing nothing.
- `level_set_piece_parameters {scene_name, node_id, parameters}`: the given
  parameters merge over the piece's current values (`derive` replaces the
  mask; a value for a masked parameter fails naming it), the masked values are
  derived again when the seed or the mask changed, and `set_parameters()`
  applies them. The reply: `id`, `name`, `shape`, `changed`, the effective
  `parameters`, `mesh_key`, `brush_id`, `world_aabb`, `triangle_count`,
  `materials`.
- `level_set_piece_material {scene_name, node_id, slot, material}`:
  `set_slot_material()` ("Materials") for the slot named, `material` read as
  a `level_place_pieces` slot material reads; one undo entry, none when the
  slot has the material already. The reply: `id`, `name`, `shape`, `slot`,
  `material`, `previous_material`, `changed` and `materials`. The generic
  `assign_mesh_material` with `primitive_index` the slot index assigns a
  material the scene already holds the same way; this tool adds the slot
  names and the palette materials the scene does not hold yet.
- `level_replace_pieces {scene_name, node_ids, shape, parameters,
  materials, surface_snap, surface_orient, surface_direction,
  surface_max_distance}`: `replace_shape()` ("Replace") for the listed
  nodes, else the selected nodes of the scene; `parameters` merge over each
  piece's carried values as `level_set_piece_parameters` merges them over a
  piece's values, `materials` read as `level_place_pieces` reads them, and
  the surface keys as `level_place_pieces` reads its call-wide ones
  (default off). One undo entry, none when nothing is replaced. The reply:
  `replaced_count`, `snapped_count`, `skipped_count`, `skipped` (per skipped
  node `id`, `name`, `reason` - `kit_layout_node`, `locked_layout_child`,
  `group`, `not_a_kit_piece`, `unreadable_record` or `loaded_mesh` - and
  `message`) and per replaced or unchanged piece `id`, `name`, `old_shape`,
  `shape`, `replaced`, the effective `parameters`, the parameter names
  `carried`, `clamped`, `defaulted` and `given`, `materials`,
  `kept_materials` (the slots that kept the same-named slot's material),
  `world_aabb`, `collision_shape`, `snapped` and, for a replaced piece of a
  snapping call, `surface_found` and `snap_distance`. A refusal names the
  piece's index and id.
- `level_place_pieces` piece key `connect {connector, to_piece_id,
  to_connector, twist_steps, gap}` ("Connectors"): the piece's pose is the
  join; with `connect` a piece gives no `position`, `rotation_xyzw`,
  `surface_snap` or `surface_orient`, and the call's `snap` and surface
  options do not apply to it.
- `level_get_connectors {scene_name, node_ids}`: per listed piece (else the
  selected kit pieces of the scene) `id`, `name`, `shape`,
  `connectors_enabled`, `disabled_connectors` and `connectors`, every
  connector of the shape in world space (`name`, `position`, `outward`, `up`,
  `enabled`). A child of a locked path or segments layout adds `layout`
  (`id`, `role`: `head`, `tail`, `inner` or `only`, and `connector_hiding`)
  and per connector `layout_direction` (`+tangent`, `-tangent`, `+normal`,
  `-normal`, `+binormal`, `-binormal`, null when the frame degenerates),
  `hidden_by_layout` and `layout_hide_reason` (`shown`, `hide_all`,
  `direction`).
- `level_connect_pieces {scene_name, node_id, connector, to_piece_id,
  to_connector, twist_steps, gap}`: `connect_piece()`; the reply gives the new
  world `position` and `rotation_xyzw`, `changed`, `world_aabb` and the
  piece's world `connectors` (each with `enabled`).
- `level_set_piece_connectors {scene_name, node_id, connectors_enabled,
  connectors}` ("Connector switches"): `set_connector_switches()` with the
  piece switch and, in `connectors`, a switch per connector name (`{"left":
  false}`), one undo entry; the reply gives `changed`, `connectors_enabled`,
  `disabled_connectors` and the piece's world `connectors` with `enabled`.
- `level_set_connector_visualization {mode, viewport}` and
  `level_get_connector_indicators {viewport}`: "Connector indicators".
- `level_set_layout_visualization {mode, viewport}` and
  `level_get_layout_indicators {viewport}`: "Kit layout drawing".
- `level_select_region_handle {scene_name, layout_id, index}` and
  `level_get_region_handles {}`: "Scatter region handles".
- `level_snap_to_surface {scene_name, node_ids, orient, direction,
  max_distance, query_only}` (`max_distance` 1 m by default): `Level_kits::snap_to_surface()`, which runs
  `find_surface_placement()` with snap, and orient when asked, for any mesh
  nodes, the listed nodes excluded from the hits. The nodes that found a
  place move by one `Node_transform_operation` each, all in one undo entry
  (none when no node moves); `query_only` moves nothing. The
  reply: `moved_count` and per node `id`, `name`, the resting world
  `position` and `rotation_xyzw`, `distance` and `surface_found` (false when
  no place lies within `max_distance`, "Surface placement"); the reply also
  echoes `max_distance`.
- `level_get_region_contents {scene_name, box: {min, max} | sphere: {center,
  radius}}` (read-only): `find_region_contents()`; exactly one of box and
  sphere, a box's min not above its max, a sphere's radius positive. The reply
  echoes the region and lists per mesh `id`, `name`, `kit_piece`, `shape`
  (null for a mesh that is not a kit piece), the world `position`,
  `rotation_xyzw` and `scale`, and `world_aabb`.
- `level_get_height_map {scene_name, min, max, cell_size, text}` (read-only):
  `make_height_map()` over `make_height_map_grid(min, max, cell_size)`; more
  than `c_max_height_map_cells` (65536) cells fails the call naming the grid
  and the limit. The reply: `columns`, `rows`, `first_cell_center` (x, z),
  `hit_count`, `height_min` and `height_max`, `heights` (rows, null for a cell
  without a hit) and `navigation_available`; with a navigation mesh also
  `navigation_stale`, `walkable_count`, `walkable` and `islands` (rows, null
  where not walkable); with `text` (default false) `height_text` and, with a
  navigation mesh, `island_text`, one string per row.
- `level_get_support_report {scene_name, node_ids, tolerance}` (read-only):
  `find_support()` over the listed mesh nodes, or every kit piece when
  `node_ids` is absent (`all_kit_pieces`), at `tolerance` (positive, default
  `c_default_support_tolerance`, 0.01 m). The reply: `count`,
  `unsupported_count`, `unsupported_ids` and per node `id`, `name`,
  `supported`, `surface_found` and `distance` (null without a hit).
- `level_get_overlap_report {scene_name, node_ids, tolerance, exclude_tags}`
  (read-only): `find_overlaps()` over the listed mesh nodes, or every kit
  piece when `node_ids` is absent, at `tolerance` (positive, default
  `c_default_overlap_tolerance`, 0.01 m); `exclude_tags` is an array of
  non-empty tag names. The reply: `count`, `tested_count`, `excluded_ids`,
  `bounds_pair_count`, `overlap_count` and per pair `ids` (smaller first),
  `names`, `depth`, `measure` and `position`.
- `level_check {scene_name, support_tolerance, overlap_tolerance,
  min_island_area, exclude_tags}` (read-only): `run_level_check()`; the
  tolerances positive, `min_island_area` 0 or more. The reply: the effective
  arguments, `piece_count`, `excluded_count`, `agent`,
  `navigation_available`, `navigation_stale`, `island_count`,
  `finding_count`, `counts` per kind and `findings`, each with `kind`,
  `position`, `ids`, `names` and `detail`, and `distance` (unsupported, null
  without a surface), `depth` and `measure` (overlap) or `area` and `island`
  (island).
- `level_get_placement_preview {}` (read-only): `active` (kit mode),
  `tool_active` (the Brush tool is the priority tool), `rotation_step_degrees`,
  `yaw_steps`, `hover_position` and `visible`; in kit mode also `shape`,
  `parameters`, `mesh_key`, `brush_id`, the brush's own `bounds` and
  `materials` (slot name to the palette material of the next piece); with a
  visible preview also its `position`, `rotation_xyzw`, `surface_found` and
  `world_aabb`, the transform a click places the next piece at.
- `get_surface_snap_settings {}` and `set_surface_snap_settings {snap, orient}`:
  the shared pair ("Surface settings").
- `level_get_pieces {scene_name}`: every node carrying `Kit_piece`, in node id
  order: its record (`parameters` with `derive`, and `parameters_text`), `mesh_key`, world
  position and rotation, `world_aabb`, `materials` (slot name to material
  name; a primitive beyond the shape's slots as `primitive <index>`),
  `brush_id`, `shares_brush_primitive` (each mesh primitive is its brush's
  part of that slot),
  `follows_kit` and `problem` ("Persistence") and `physics` (motion mode,
  collision shape, whether the body exists); straight stairs, original or
  recreated, also `stairs` (`step_count`, `step_rise`, `tread_depth`, `run`,
  `slope_degrees`: the layout their values produce, R3.5); `light_frames` (the
  shape's at the piece's values) and `lights` (its kit light nodes: `id`,
  `name`, `frame`, local `position` and `direction`, and the light's own
  values, "Lights on pieces").

## Walkable area reads

A level's walkable area is its scene's navigation mesh (`doc/editor/navigation.md`):
pieces are input of the build as any mesh is, a seed node marks where the
viewer stands, and a parameter change of a piece makes the mesh stale
("geometry changed") until the next build. `get_navigation_summary`,
`navigation_find_point` and `navigation_find_path` read the islands, the
nearest walkable point and the path between two points
(`doc/editor/navigation.md` "Islands and queries").

## Spatial queries

The reads of D7 take and return positions in the scene root's space and name
nodes by id (R10.1). The queries are in `level_queries.hpp`, which the Level
Kit window and the MCP tools both call; their scene-free parts are in
`level_region.hpp`, which `editor_level_tests` builds without the editor.

- `find_region_contents(scene_root, region, out_meshes)` (D7.1, R10.2): the
  content meshes whose world bounds (`Mesh::get_aabb_world()`) intersect a
  `Level_region`, a box or a sphere, by id. `region_intersects()` counts
  touching as intersecting; a box test compares the bounds per axis, a sphere
  test the distance from the center to the nearest point of the bounds.
- `make_height_map(scene_root, grid, out_height_map)` (D7.2, R10.6):
  `make_height_map_grid(min, max, cell_size)` covers the x extent with columns
  and the z extent with rows of `cell_size` cells, a partial last cell
  included. The raytrace scene is committed once; per cell one ray runs down
  from `max.y` to `min.y` through the content meshes, and a face seen from
  behind is passed through, so a ray starting inside a solid finds the first
  upward face below it. The hit's y is the cell's height. With a navigation
  mesh each hit asks `Navigation_mesh::find_nearest_point()` within a box of
  5 cm half width and the agent's climb in height; a nearest point over the
  polygon (within 1 mm horizontally) makes the cell walkable with the
  polygon's island id. The cells are row after row, the row of the smallest z
  first, and the caller's `Height_map` keeps their capacity.
- The text pictures (`write_height_text()`, `write_island_text()`): one line
  per row, the row of the smallest z first, x to the right. In the height
  picture a digit maps the cell's height linearly from the lowest hit (0) to
  the highest (9), rounded (`height_digit()`; all 0 when the hits are level),
  and `.` marks a cell without a hit. In the island picture a walkable cell
  shows its island id as 0 - 9 then a - z (modulo 36), `#` a hit that is not
  walkable and `.` no hit.
- `find_support(scene_root, meshes, tolerance, out_results)` (D7.4, R10.4):
  per mesh one ray straight down from the top plane of its bounds above the
  base center and each base corner (the corners of the snap lines, 1 mm
  inside the footprint; at least 1 mm above the base), the
  nearest front face of a content mesh, with only the mesh itself passed
  through, so pieces support pieces. The smallest distance from the base to a hit is
  the result's `distance` (negative when the base is below the surface); the
  mesh is supported when a hit exists and the distance is at most the
  tolerance. `get_kit_piece_meshes()` gives the content meshes carrying a kit
  piece record, by id.
- `find_overlaps(meshes, tolerance, exclude_tags, out_report)` (D7.5,
  R10.3): the meshes carrying none of the excluded tags (`Item::has_tag()`, the
  Properties window's Tags row) are tested. The first pass,
  `find_bounds_pairs()`, sorts their world bounds by min x and sweeps along x,
  keeping the pairs whose bounds overlap by more than the tolerance on every
  axis (`get_bounds_overlap()`, the smallest axis overlap, bounds every
  penetration depth from above), so only overlapping neighbours reach the
  exact pass. The exact pass measures a pair with
  `ICollision_shape::get_penetration_depth()` (`doc/erhe/physics.md`) on the
  meshes' collision shapes (`get_node_collision_shape()`) at the world
  rotation and translation their bodies take, without node scale. `measure`
  says how: `exact` with both shapes; `convex_hull` when a mesh has no
  collision shape (a convex hull of its mesh, `build_shape_from_mesh()`,
  stands in) or both are triangle meshes, which Jolt does not collide - then
  each mesh is measured against the other's hull and the smaller depth is
  kept, an upper bound of the depth; `not_measured` when the backend answers
  for neither form (Box3D, null), with the bounds overlap as the depth. A pair
  deeper than the tolerance is a result: both meshes, the smaller id first,
  the depth, the measure and the center of the box both bounds cover; the
  results are sorted by the ids. Against a triangle mesh the depth is the
  deepest penetration through one of its triangles, which can be less than
  the move that separates the two. The hulls are made per call and dropped at its end.
- `run_level_check(scene_root, library, options, out_check)` (D7.6, R10.7):
  over every kit piece, one list of `Level_finding`s (kind, position in the
  scene root's space, at most two meshes, a value and a detail text) in this
  order: `unsupported` (`find_support()` at `support_tolerance`; the base
  center; the distance, none without a surface), `overlap` (`find_overlaps()`
  at `overlap_tolerance`; the overlap's center; the depth and measure),
  `unreachable_island` (each island of the scene's navigation mesh that is
  not reachable and larger than `min_island_area`, default 1 m2; the island's
  point; the area; the piece under the point, found by a downward ray from the
  agent's climb above it, when one is), `unwalkable_parameters` (R2.10: the
  shape's `check_walkability` over the piece's parameter values for the agent
  of the scene's navigation build settings) and `unreadable_piece` (a record
  naming no known shape or parameters that do not parse; the piece's origin).
  Pieces carrying a tag of `exclude_tags` take no part in the support and
  overlap parts, so ground pieces, which rest on nothing, and a rock sunk into
  the ground on purpose can be left out. Without a navigation mesh no island
  is checked and `navigation_available` is false; a stale mesh is checked as it
  stands with `navigation_stale` true.

## Verification

- `editor_level_tests`: the mesh checks of every shape at its defaults and at
  the ends of its ranges, the triangle ceiling, equal meshes for equal values
  and seeds, the range messages, walkability at defaults and a palette
  default and alternatives for every slot, the palette holding stone, wood,
  plaster, foliage, bark, earth and grass. Material slots, for every shape at
  its defaults and at a seed-derived second parameter set: every facet names a
  declared slot, every slot has facets and the slot meshes add up to the
  mesh, slot names are unique; the tree's slots are bark and foliage, each a
  closed part, the bark the 20 triangles of the trunk. Derive from seed: derived values inside their
  ranges on their steps for 200 seeds, equal for equal seeds and varied across
  seeds, independent of the other parameters, ranges following the lengths,
  walkable shapes walkable at 300 seeds with every derivable parameter masked,
  the canonical text round trip with a mask, a refused mask on a fixed
  parameter, texts without a seed or with a bad `derive` refused, and
  `kit_next_seed()`. Seed sweeps follow one rule (`sweep_seeds()`,
  `derive_seeds()` in `kit_shape_test_support.hpp`): a shape that draws
  nothing from its seed (`Kit_randomness::none`; `seeds_select_the_mesh`
  proves that its explicit values at the defaults give one mesh at two
  seeds, and that a seeded shape's do not, and the random value sets below
  give one mesh at two seeds each) is swept at one seed, a seeded shape at two, and a
  sweep of derived values at eight seeds. Every rock form at 0, 1 and 2
  bevels passes the mesh checks and the triangle ceiling over the rock's
  seed sweep and three roughness values; the
  counts are test properties (`--gtest_output=xml`). The more shapes: an open
  surface passes the mesh checks as a surface and fails them as a solid; the
  water is an open surface facing up without a body, two triangles per
  subdivision squared; every palm species over the derived-value seeds passes the
  checks within the ceiling, its base at y = 0 and its crown at its height
  (the counts are test properties); the cliff's and the shore's end vertices
  are equal for two seeds of equal values, the cliff's grass facets lie at its
  height and its setbacks keep the top walkable at the ends of the ranges,
  and a shore too steep for the agent is refused by its walkability check;
  every palm species at the corners of its size parameters; the cliff's top
  connector between the back and the rim; the second batch at the corners
  where parameters interact (every leaf plant form at the corners of height,
  leaves, droop and leaf width over the seed sweep; every column order at the
  corners of height, width, flutes, taper and segments, its top the
  abacus'; the entablature's, torii's, battlement's and tower's corners, the
  refused combinations left out; the third batch: every rock spire form, the
  basalt columns, every roof form, every palm clump and shrub species, the
  dune and the ground patch at their corners over seeds, neighbouring basalt
  tops at most one step apart and whole steps below the height, a square and
  a near-square hip roof ending in an apex, the roof's wall line connectors,
  no facet of a dune or ground patch whose walkability check passes
  steeper than the agent's slope, long, narrow and tall dunes over every
  segment count and asymmetry valid, a dune shorter than 1.04 widths refused,
  the clump's binding combination at 1940 triangles or more, and a roof's
  front, back, left and right connectors joined to the tops of four walls
  facing out landing it untwisted over the room), the entablature's and battlement's equal
  end outlines, the tower's floor facets at its height and its clear
  doorway, a refused torii whose posts meet and one whose tie beam is too
  low; the column on a slab carrying an entablature, a battlement on a wall
  top, a wall's start at a tower's side and a tower on a tower joined by
  their connectors; a pillar replaced by a column and back, a wall by a
  battlement and back, carrying their sizes. Every shape keeps the ceiling
  and passes the mesh checks at the combinations where its triangle count
  is largest (found by a search over all range-end combinations and random
  values; the most are test properties) and over 24 random in-range value
  sets per shape (a fixed stream; one seed for a shape that draws nothing
  from it); every shape's values derived from 300
  seeds are accepted at six (module, wall height) settings, and the walkable
  ones walkable but for six built shapes (`doc/plans/open_items.md`). Every shape at the all-minimum and
  all-maximum corners of its ranges passes the mesh checks, and at the
  all-maximum corner keeps the kit's ceiling. `generated_meshes_are_pinned`
  pins every shape's generated mesh at its defaults (seeded shapes at seeds 1,
  3 and 17): the vertex, facet and corner counts and the facet count of each
  slot exactly, and the bounds, the mean vertex position (within 0.1 mm) and
  the surface area (within 1 part in 10000) by tolerance. Positions carry a
  tolerance because their last bits differ between platforms - Clang fuses a
  multiply-add within an expression where GCC and MSVC do not
  (`doc/cmake_conventions.md`), and sine and cosine differ between C
  libraries - and the order of a convex hull's vertices and facets (the
  rock) follows those bits. The recreated shapes ("Recreated shapes") are in
  every test that goes over the library (a test that keys an expectation by
  shape id takes a row of the recreated shape's own id first, else the
  original's). Each batch's rows (`kit_shape_rows_<batch>.cpp`: its shape
  ids, recreated originals, seeded and enum shapes, component counts,
  walkability exceptions, triangle counts, worst cases and pins) and helpers
  (`kit_shape_test_support.hpp`) are shared by the test sources; a pin
  mismatch prints every actual row in that syntax. `Kit_recreated`: every
  shape with `recreates` stands directly after an original of its kit, its
  id and name the original's with the suffixes, and the recreated originals
  are those the batches list; the recreated basic shapes have
  their originals' bounds at the defaults, both range corners and four
  derived seeds; the recreated stairs have 12 triangles a step and no vertex
  with more than six facets; the recreated cylinder's points are exact
  mirror images at nine segment counts; the recreated door wall, pillar and
  arch have their originals' bounds at the same seven value sets, and the
  round pillar at six segment counts over the corners of width and height;
  the door wall has no vertex inside its stated opening and vertices on its
  edges; the arch at every segment count from 2 to 48 and five sizes passes
  the mesh checks as one component within the ceiling, has no vertex inside
  the opening's ellipse, its feet at the span and its crown at the rise; the
  square pillar is the 12 triangle box and the round one stays inside its
  cylinder with full radius at the base and the top. The spatial query
  helpers: box and sphere intersection with touching, containing, separate and
  invalid bounds; the grid's columns, rows and cell centers, a partial last
  cell and refused sizes; the height digits; both text pictures of a known
  grid; the bounds overlap of overlapping, touching, separate and invalid
  bounds and its center; the bounds pairs at two tolerances; the tag test.
  The group mirror: a reflected position, a block keeping its rotation across
  its own plane (also yawed, through its z plane), a wedge turning half a turn
  across z, a shape without planes taking local x and not exact, every pose
  mirrored twice equal to the original (scale kept) and the group origin.
  Every shape that declares mirror planes is its own mirror image across each
  of them at its defaults, at a seed-derived second parameter set and at each
  enum value; ten shapes declare planes (the water among them), no seeded shape does.
  The kit layout values: the pattern and items texts round trip bit for bit
  and refuse a non-object, an unknown kind, an unknown key and a decimal
  float; the item cycling and the element seeds (stable, distinct, in range);
  a count mode line along its direction; gap mode lines with the remainder
  spread, an exact fit, one just short, a line shorter than one step and a
  wider item; a closing full ring, an arc and a ring shorter than one step;
  count and gap mode paths over an L of corner points (the corner piece on
  the leaving span) and around a closed square (no piece twice at the seam,
  gap mode closing), a smooth curve's pieces at the table's distances with
  the tangent's heading (upright) and the tangent (tangent), a curve path
  through a transform; and the refusals (the limit, no positive step, a
  negative length, counts 0 and 1025, a path without a curve, a curve of no
  length, a 40 x 40 grid); the step and spacings from an item's extent.
  Scatter: the pattern text round trip with the scatter values, a text
  without them reading as the defaults, the values kept across a kind change
  and an unknown region refused; the first three points of seed 7 as fixed
  hexadecimal floats, the same placement again, a raised count keeping the
  first points and another seed placing them elsewhere; every pair at least
  the spacing apart, every point inside the rectangle or the disc and every
  yaw in its range; density mode (15 pieces over 60 square meters, a density
  rounding to none holding one), a density and a count over 1024, a zero
  density, an inverted yaw range and a zero size refused, and 200 pieces 2 m
  apart over 10 x 6 m holding fewer and reporting the 200 asked for; crowded
  runs (a rectangle at 2 m, a disc at 1.5 m) pinned by their count, last
  point and a hash over every position's bits, and resolving without poses
  and expanding the run giving the same points; spacing 0 taking every
  candidate, a spacing wider than the region and a count of 1 holding one
  piece, a yaw beyond 360 degrees and a scatter key of the wrong type refused.
  Item weights and footprints: the item text writing `weight` only when it
  is not 1 and reading it back, weights above 1000, negative or not an
  integer refused; `pick_scatter_item()` at the boundaries of its integer
  ranges (a weight 0 item never picked); 400 points over three items
  weighted 1, 0 and 3 with spacing 0, each pose's item the pick of its
  candidate's fourth draw from `Kit_random`, and a line over the same items
  taking item i modulo 3; every weight 0 refused for a scatter and not for a
  line; `get_scatter_footprint_radius()` of an off-center box and of no
  bounds; two items of footprint radius 0.25 and 1 m over a rectangle and a
  disc at spacing 0 and 0.5 m, every pair's centers at least both radii plus
  the spacing apart in the sampler's arithmetic and both items placed, and
  the crowded run pinned by its count and position hash.
  The pattern expansion: a line in a yawed start frame, the grid's element
  order, a full ring facing outward and a half ring with both ends, the
  ring keeping the start rotation, a path along corner points turning at a
  corner (and at a distance a rounding short of it), a slope upright and
  pitched, a vertical span keeping the heading, a closed square and a path
  from a turned start pose, the refusals (with the counts and the limit in the messages);
  segments: a closed room of four corners with two items of other x bounds,
  each element's scale (s, 1, 1) and its scaled bounds ending on its two
  corners, an open curve one segment fewer, a slope upright (plan length at
  the first point's height) and tangent (the chord, pitched), a start pose,
  and the refusals (no curve, one point, a zero-length segment naming it, a
  vertical segment in both orientations, an item without x extent, no
  item); the `segments` pattern text, the default pattern's canonical text
  pinned as written before segments, `resolve_layout_pattern()` of a closed
  triangle (count, chord sum, per item x bounds) and `is_curve_layout_kind()`.
  Replace: a parameter of the same name and type carried and clamped to
  the new range (float and int, both ends), an enum by name, an enum name the
  new shape lacks, another type and a missing name defaulted, a seed-derived
  value derived again in the new range and an explicit one kept explicit;
  a block's seed carried to a rock equal to a rock of that seed; the defaults
  of every shape carried to every shape validate (and to itself unchanged);
  slots matched by name. Connectors: every frame unit and orthogonal, names
  unique and equal at two parameter sets, the connector names of the wall,
  door wall, stairs, block and rock, the wall's ends following its length and
  the stairs' top and bottom following rise and run; joins against a turned
  target at twist steps -1 to 4 and three gaps putting the point, outward and
  up where stated with determinant +1 and equal results twice; a wall's start
  on a wall's end, stairs' top on a floor's front edge and a pillar's base on
  a block's top with one twist at their expected poses. Connector snap
  (`find_connector_snap()`): the closest pair, a pair exactly at the distance
  found and one 0.1 mm beyond it not, coincident connectors at distance 0,
  equal distances taking the smallest key in every input order, empty sets
  and a zero, negative, NaN or infinite distance finding nothing;
  connector frames under a uniform scale, a non-uniform scale with a sheared
  parent chain (outward perpendicular to two transformed in-face vectors),
  a mirror (right-handed) and a zero scale axis (none); joins of a
  scaled moved wall onto a stretched target at every twist (start on end,
  outward opposed, the motion's rotation determinant +1, the moved
  matrix's column lengths kept), a wall mirrored by scale (-1, 1, 1) joined
  and rebuilt with that scale as stored, a joined piece needing no motion, and the
  rotation-matrix and orthogonal-axes tests. Layout connector switches
  (`test_kit_layout_connectors.cpp`): each axis direction classified, ties
  to tangent, then normal, then binormal, the head, tail, inner and only
  roles of open and closed curves of one, two and five children, nothing
  hidden by default, each switch alone on an inner child, a combination,
  Hide All, every switch on a head, tail and only child (only the
  neighbour-facing direction follows them), two children facing each
  other, a turned, a stretched (a diagonal outward reweighed) and a
  mirrored child, a child with a zero scale axis hiding nothing, every
  shape's connector count within `c_max_shape_connectors` at its defaults,
  range corners and each parameter's range ends, and the layout-hidden bits
  read by `Kit_connector_switches::is_enabled()`. `get_kit_geometry_bounds()` of
  every shape at three seeds equal bit for bit to the corner box of the
  geometry processed as a brush's is. Kit layout outlines
  (`make_layout_outline()`): a line's segment and ticks across it (and along
  z for a vertical line), a grid's cell outline and its column and row ticks,
  a half ring's arc through its elements with radial ticks, a rectangle and a
  disc scatter's region with a footprint and spacing circle per point whose
  spacing circles do not overlap, a path drawing nothing, and a segments
  outline's chords and ticks on an open and a closed curve; a scatter's
  region handles (a rectangle's corners at the polyline's points and edge
  midpoints between them, a disc's on its circle, none for a line) and
  `apply_region_handle()` (an edge's size twice its reach with the other
  values and y ignored, a corner's both sizes, a disc's radius the xz
  distance, the clamp past the origin and beyond the range, a point that is
  not finite changing nothing).
  `erhe_physics_tests` (Jolt) measures two boxes 0.25 m into each other,
  touching and apart, a hull placed by its corner and turned half a turn, a
  box 0.2 m into a triangle mesh cube and no answer for two meshes.
- `python3 scripts/level_tools_verify.py` (headless editor): the catalog lists
  31 shapes; the 31 shapes placed at their defaults in one row match their
  default sizes, stand on the ground, echo the catalog defaults and carry
  with static bodies (a triangle mesh for the
  stairs, a convex hull for the block) and their slots' default materials;
  equal values share one mesh, different
  values do not, and a later call reuses the mesh; undo removes a batch in one
  step and redo restores it; out-of-range values, unknown keys, parameters
  and shapes and a scaled rotation fail the call and place nothing; snapping
  rounds to the module; defaults and snapping follow `set_scene_settings`
  level lengths. Parameter changes: a stair's new rise and bounds, the node
  keeping its id, name, transform and materials and a static body with the new
  triangle mesh shape, a second piece of the old values unaffected, undo and
  redo, a refused out-of-range value and a refused node that is not a piece,
  and `cached_brush_count` falling back after `clear_undo_history` for a
  brush no piece uses. Surface placement: a block snapped onto a slab's top, a
  yawed bush and a yawed block oriented to a wedge's slope normal with the
  tilt as the only change, `level_snap_to_surface` with `query_only` and as
  one undo entry (dropped from 3 m with the maximum distance raised to 5 m;
  with the default 1 m the query finds no place and keeps the pose), a block
  over a slab's edge resting on the slab, and a placement over empty space
  reporting `surface_found` false; a sunk block
  rising onto a slab (with a negative `distance` from
  `level_snap_to_surface`), a bush placed inside a hill where the slope is
  lower than the bush ending on its surface,
  and a block under a roof higher than itself resting on the floor. It writes a
  `render_scene_image` of the row (1600 x 600) to
  `logs/level_tools_catalog_row.png` and one of the surface placements to
  `logs/level_tools_surface_placement.png` (`--image` and `--surface-image`
  name other paths). Walkable level, in a scene of its own: a floor slab,
  stairs at their defaults and an upper slab meeting the stairs' top (from the
  catalog's `default_bounds`), a seed node on the floor and export settings;
  after `navigation_build` the upper slab is reachable, a complete path climbs
  to it and the two are one island; a `step_rise_limit` of 0.5 m (above
  `agent_max_climb`) makes the mesh stale and, after a rebuild, leaves no path.
  It writes a `render_scene_image` with the navigation mesh (`all`) to
  `logs/level_tools_walkable_level.png` (`--walkable-image`). Persistence: the
  catalog scene and the walkable level saved, closed and loaded again keep
  every piece's name, record and bounds, follow their kit with equal values
  sharing one mesh, have their static bodies, hold every piece as a node with a
  plain glTF mesh in the file; a reloaded piece's parameter change undoes, a
  placement after the reload adds no material, and a hand-edited copy with an
  unknown shape id and an out-of-range width loads with those two pieces
  keeping their meshes, `follows_kit` false and the problem named. The Level
  Kit window and the Brush tool's kit mode, driven through the UI tools in a
  scene whose camera looks at a slab and a wedge: the window has no Place
  button; the Block row enters kit mode; with the pointer over the slab the
  preview is the module-rounded hover point resting on the slab, and a click
  with the insert binding places a piece at exactly the preview's transform as
  one undo entry, unselected; a second click shares the mesh; a block hovered
  on its top takes the next one on top; the rotate binding turns the preview
  and the next piece by 90 degrees; Orient To Surface over the wedge gives the
  preview and the piece the slope normal as up axis; a width edit changes the
  preview's bounds and the next piece; undo removes one piece per step; a
  palette brush chosen with `set_tool_asset` leaves kit mode and places
  face-aligned on the slab; a hotbar key choosing another tool leaves kit mode;
  a selected stairs piece shows its parameter rows. A `capture_screenshot` with
  the preview over the slab goes to `logs/level_tools_level_kit_window.png`
  (`--window-image`). The Properties window with that stairs piece selected
  has the rows rise, width and step_rise_limit in its Kit Piece group and no raw
  Parameters row outside developer mode, `get_item_properties` still lists the
  raw text, and typing 1 into the rise row gives the stairs a 1 m rise as one
  undo entry that undo takes back; a `capture_screenshot` goes to
  `logs/level_tools_properties.png` (`--properties-image`). Live drags: a held drag of
  that rise row shows the new bounds mid-drag with the record and the undo
  depth unchanged, and its release records one entry whose undo gives the
  starting bounds back; a drag back to its starting value, a typed rise of
  100 m, Escape in a typed value, deselecting the piece mid-drag and an undo
  mid-drag record no entry of the drag and leave the piece following its kit;
  a held drag of the Level Kit window's width row changes the placement
  preview's bounds without an entry; after `clear_undo_history` the cached
  brush count is back at its starting value; the preview steps of a dragged
  stairs piece are timed from the "preview step" trace lines. Derive from seed
  and rock forms: the catalog's derive fields (a seed on every shape, derive
  ranges inside the valid ranges, the default masks); in a scene of its own
  every rock form at 0, 1 and 2 bevels and eight rocks of consecutive seeds
  (`kit_next_seed()`, ported to the script) with form, size, roughness and
  bevels derived, each within the ceiling on its flat base, more facets per
  bevel pass, a convex hull collision shape; a `render_scene_image` (1600 x
  1000) of them to `logs/level_tools_rock_forms.png` (`--rock-image`); a masked
  placement deriving its size, two pieces of equal seed and mask sharing one
  mesh, a value for a masked parameter and a derived fixed parameter refused,
  `derive: []` taking a value, a seed change re-deriving as one undo entry, and
  a record of the old form (no `form`, `bevels`, `derive`) not following the
  kit before and after a reload. In the window scene: New Seed Per Piece gives
  two clicks two rocks of consecutive seeds and the preview the next; the
  Properties window shows the `size.from_seed`, `roughness.from_seed` and
  `seed.new` items, a click on `roughness.from_seed` derives it as one undo
  entry and `seed.new` takes the next seed as one. Spatial queries, in a
  scene of its own with a ground slab, a block resting on it, a block stacked
  on that, a floating block and a block over nothing:
  `level_get_support_report` finds the ground, the floating block and the
  block over nothing unsupported, the resting and the stacked block at
  distance 0, the floating block 1.8 m above the ground, and with `node_ids`
  and a 2 m tolerance the floating block supported; a box inside the resting
  block's height holds it alone, a box through all three heights holds the
  ground and both blocks, a sphere touching the floating block's top holds it
  alone and one 0.7 m above the far block nothing, with the entries' shape,
  transform and bounds; a height map over the ground and the block stack
  matches the known heights cell by cell, its text picture the expected
  lines, a ray starting inside the floating block finds the ground and a
  region below the floating block has no hit; refused arguments of all three
  tools and an oversized grid. Overlap report, in a scene of its own (closed
  afterwards): over a tagged ground, two blocks 0.25 m into each other, a
  block touching one of them, one apart, one sunk 0.2 m into the ground and
  two stairs 0.5 m into each other, exactly the three pairs (the blocks and
  the sunk block exact with their depths, the stairs `convex_hull` within
  their bounds overlap) and three bounds pairs; `exclude_tags` leaves out the
  ground and a tagged block; a 0.3 m tolerance and the touching and apart
  blocks alone give none; refused arguments. Level check, in a scene of its
  own (closed afterwards): without a navigation mesh it says so and finds the
  ground unsupported too; after a build with a seed on the ground and the
  ground excluded by tag exactly one finding of each kind - a floating block
  3.8 m above the ground, two blocks 0.5 m into each other, the unreachable
  island on a platform on a pillar, and a door wall with a 1.9 m opening -
  with their pieces and positions; `min_island_area` 1000 drops the island;
  refused arguments; after moving the pieces apart, resetting the opening and
  seeding the platform it reports the mesh stale, and after the rebuild no
  finding. Snap to surface, in a scene of its own (closed afterwards) with
  two hills flush on a ground slab (each hill bottom coincident with the
  slab top; the live layout drag checks put their hill flush on the slab
  too) and a 3 m wall on it: a ring of
  blocks rests where the rule over the hill's hit points under each block's
  base points (sampled from the height map before the blocks stand there)
  puts it, answers 0 to `level_snap_to_surface` `query_only` with
  `surface_found` exactly where the rule finds a place, the support report
  answers 0 for those, snapping again moves none, and a query with
  `max_distance` 1000 keeps each rested block's pose bit for bit; a block
  placed flush against a rested block of the ring on its downhill side rests
  on the hill below it and answers 0 to the query over both and to the
  support report; each tree of a ring with per-element seeds that found a
  place rests between the hill's height under its base center and the hill's
  top under its footprint, each that found none stays at y 0 over a hill top
  more than 1 m up, and all answer 0 with the same `surface_found` in three
  repeated queries, the support report lists every tree as supported; a line
  of overlapping 1.4 m rocks answers 0 and three held Transform drags of it
  rest every rock where the release rests it, both answering 0; for the trees
  and the rocks, `surface_found` is checked against the hill sampled before
  they stand there (an element whose footprint's hill top is within 1 m must
  find a place); a 0.5 m block flush against the wall and one 5 cm into it
  rest on the slab (U31), keeping y 0 through `level_snap_to_surface` and a
  surface-snapped drag along the wall; a 0.5 m block overlapping a stack of
  three slabs 0.3 m apart rests on the top one in one `level_snap_to_surface`,
  over twenty such slabs it stays at y 0 with `surface_found` false, and with
  `max_distance` 6.5 it rests on the twentieth; 0.2 m blocks placed at a
  hill's foot and 0.3 m up inside it rest on the slab top under the hill
  (U31); inside a floating slab a block 0.6 m up drops to the free ground
  under it and one 1.3 m up rises onto the slab top; surface-snapped line and
  path layouts over nothing generate their elements where the same layouts
  without surface snap do.
  Oriented rests, in a scene of their own (closed afterwards) with a wedge
  of slope normal (0, 2, 1) / sqrt(5) on a ground slab: a yawed block placed
  with snap and orient rests its base center on the slope with up on the
  normal and its heading, answers 0 with the same pose to three
  `level_snap_to_surface` orient queries and moves by nothing when applied;
  a held oriented drag uphill rests where its release does, on the slope; an
  oriented layout of three blocks rests on the slope and answers 0 with its
  poses; a block sunk 0.3 m into the slope rises onto it; a block inside a
  stack of three slabs goes to the top one, upright; blocks straddling the
  slope's low edge take the slope or the ground by their base center, and
  one with its base center exactly on the edge stands upright on the
  ground, each answering 0 with identical poses to three repeated queries;
  flat blocks (Y scale 0) on the ground and on a slab top answer 0 with and
  without orient. Layout patterns, in a scene of their own (closed afterwards): a
  line of walls without a step touching end to end, named after the shape
  and sharing one mesh; an arc of 180 degrees with both ends keeping the
  node's rotation; a path layout over a corner curve one undo entry that one undo removes
  with its node and redo restores; with `surface_snap` a line over a ground
  slab and a raised slab rests each block on the surface under it;
  `from_piece_id` of a yawed, parented piece with a width and a material
  lays three pieces along its x axis with its values and material under its
  parent, removes the piece in the same entry, and undo gives the piece back
  exactly (id, place among its siblings, transform, record, material, body);
  14 refused calls (the grid limit, a zero step, radius, arc, facing, an
  empty path, a gap path over the limit, unknown keys, values out of range,
  `from_piece_id` combinations, a missing or non-piece node and a locked
  layout's child) place nothing and record no entry. Groups, in a
  scene of their own (closed afterwards): `level_group_nodes` of a block, a
  wedge, a rock and stairs puts them under an `Xform` at the base center of
  their bounds, world transforms and bounds kept, one entry; undo and redo;
  `set_node_transform` on the group moves every piece, one entry;
  `level_duplicate_group` copies the four as full pieces (record, the
  original's `brush_id`, `follows_kit`, a static body with the original's
  collision shape), a parameter change of a copy leaves the original alone,
  undo and redo; `level_mirror_group` z reflects the positions, keeps the
  block's rotation, turns the wedge, the stairs and the rock half a turn,
  reflects the symmetric pieces' bounds and lists the rock as not exact; twice
  gives the original, x keeps the plane-x shapes' rotations; a group of the
  group, a piece and a piece inside it nests the group, its copy at a
  position copies the nested group; `level_ungroup` and its undo;
  `delete_nodes` on the group and its undo give back the pieces with their
  bodies and the stairs' triangle mesh shape; a pasted stairs piece has that
  shape too; 14 refused calls change nothing; saved and loaded with a copy of
  the group, the pieces are back (as in persistence), the group and its copy
  each hold its four pieces and the reloaded group mirrors. In the walkable level after the rebuild a
  height map strip from the upper slab to the floor has the upper slab's
  height, walkable cells and island in its first row and the floor's, with
  another island, in its last. In the window scene: Support Report over a
  resting and a floating block with Select Unsupported selects the floating
  one, over the resting block alone it lists nothing, Region Contents over the
  resting block lists it and a click on its row selects it, Height Map
  shows its picture, Overlap Report over two selected blocks 0.6 m into each
  other lists the pair and a click on its row selects both, with one of them
  tagged and its tag typed into Exclude Tags it lists nothing, and Level Check
  lists as many rows as `level_check` returns findings, a click on the pair's
  row selecting both; a `capture_screenshot` goes to
  `logs/level_tools_spatial_queries.png` (`--queries-image`). The window's
  Layout section (R5.12.9) shows the initial values' rows, Make Layout and
  Place As Layout and none of a node's (Unlock, Refresh, Remove Item);
  editing them with a layout node selected changes no node and records no
  entry; with module and surface snap off, a Count of 3 and Make Layout make
  a locked Xform layout of three blocks in the piece's place, one entry, the
  piece gone and the node selected, undo giving the piece back exactly; with
  Path, Make Layout makes a Curve layout node in a wedge's place with three
  pieces along its straight curve, selected; with Place As Layout a Brush tool
  click makes a locked line layout of three blocks, element 0 at the
  preview, one entry, selected, and off again a click places one piece; a
  `capture_screenshot` goes to `logs/level_tools_layout.png`
  (`--layout-image`). The Properties window's Kit Layout group, the layout
  selected through one of its pieces (the node selected), shows the pattern
  rows of the kind (no Path for an Xform), the item's rows, the snap, seed
  and lock rows; Count, Step, By Gap, Length, Gap, By Count, a Direction
  component, Grid, Count X, Spacing Z, Ring, Radius, Arc, Face Outward,
  Line, Add Shape Item (the Level Kit window's wedge, the pieces
  alternating), the first item's width row and slot material combo, Remove
  Item, Module Snap, Surface Snap, Surface Orient, Per Piece Seed and Layout
  Seed each regenerate or change the values as one entry; Step 0 for four
  pieces is refused with its message (logged), recording nothing; Unlock
  changes no child, Count typed while unlocked changes the value and leaves
  the children, Lock replaces every child, Refresh of a matching layout
  records nothing, and the buttons' enabled states follow the lock; a path
  layout shows the Curve group beside the Kit Layout group, its Kind row
  offering Path alone, and Follow Tangent there is one entry; every
  entry is undone afterwards. With developer mode on, the selected layout's
  raw Pattern row in the Properties window typed with a text the layout
  cannot take is taken back and records no entry. The window's Group section: Group Name typed and Group
  Selected over two selected pieces makes the named group, one entry, and
  selects it; Duplicate copies it by its width along x as full pieces, Mirror
  X reflects the copy's pieces across its x plane, Ungroup moves them under
  the scene root keeping their transforms, each one entry; a
  `capture_screenshot` goes to `logs/level_tools_group.png`
  (`--group-image`). Material slots, in a scene of their own (closed
  afterwards): the catalog's tree slots bark and foliage with Kit Bark and Kit
  Foliage and alternatives, every slot's default and alternatives in the
  palette; a placed tree with two mesh primitives of those materials sharing
  the kit brush's parts; per-slot materials at placement in one undo entry
  whose undo takes out the palette materials it added; trees on a ground slab
  supported, not overlapping it, and a raised one 1 m above it for
  `level_snap_to_surface`; `level_set_piece_material` on the foliage as one
  entry keeping the bark, no entry when unchanged, a palette material the
  scene lacks added in the same entry; a parameter change, a layout made from
  the piece, a new layout with item `materials`, a duplicate, a mirror and a paste keeping
  the assignments (and the triangle mesh collision shape); ten refused calls
  changing nothing; save and reopen giving every piece its slot materials
  back by name. The window's slot rows: choosing Tree shows bark material and
  foliage material with the defaults in the preview, Kit Autumn Foliage chosen
  in the foliage row reaches the preview and the tree a click places, the
  selected tree's own bark row set to Kit Wood is one entry keeping the
  foliage, and a held drag of its segments row in the Properties window
  previews a new mesh with both materials kept and commits one entry; a
  `capture_screenshot` goes to `logs/level_tools_material_slots.png`
  (`--materials-image`). Replace, in a scene of its own: a yawed block by a
  rock in one entry keeping id, name, parent and transform bit for bit, the
  seed carried and the rock's other parameters at their defaults (the record
  equal to a rock placed with that seed), the rock slot at its default, a
  collision shape and body; undo and redo exact; a tree by a rock and back
  the same tree with its slot defaults, two undos the tree with its chosen
  foliage; a tree by a bush keeping its foliage material; a 40 m block by a
  pillar clamped to 16 m; given parameters and materials; a block by a block
  no entry, also with parameters and materials given; the selection
  replaced in one entry and staying selected, one
  undo exact; a locked layout's child and the layout node skipped with their
  reasons while the piece listed beside them is replaced in one entry, undo
  exact; skipped nodes alone recording no entry; a piece listed twice and an
  unknown shape refused with no entry; an
  unlocked layout's child replaced in place and a lock giving the layout its
  blocks back; a piece whose mesh has another primitive count than its
  record's shape (a tree mesh under a block record written raw) skipped as
  `loaded_mesh`; a grouped piece staying in its group; a selected group
  skipped as `group` beside a replaced piece. Snap, in a scene of its own:
  two pieces floating over a ground block snapping onto its top in the
  replace's one entry, the upper one passing the lower one (pieces of the
  call are not surfaces for each other), heading kept, resting for
  `level_snap_to_surface`, one undo exact; a listed piece of the shape
  already unchanged and a surface for the replaced piece over it; a piece
  2 m over its ground kept in place at the default 1 m maximum distance and
  snapped 2 m down with 3 m; redo giving the snapped pieces again bit for
  bit; a uniformly and a non-uniformly scaled piece resting with their
  scales; a child piece and its parent piece both resting, replaced with
  snap and with `level_snap_to_surface`; parameters given with only skipped
  nodes listed not refused; `surface_orient` alone snapping; a negative
  maximum distance refused. The window: Replace Selected disabled
  without a selected piece, Rock chosen in Replace With and Replace Selected
  replacing two selected pieces in one entry with the logged message, one
  undo exact; a piece keeping a loaded mesh skipped and listed in the logged
  message while the other selected piece is replaced; a selected kit layout
  node and group skipped and listed; with Snap To Surface on a raised piece
  replaced and snapped onto the slab in one entry, the message saying so,
  one undo exact. Connectors (`--only-connectors` runs
  them with the window's), in a scene of their own: the catalog's connector
  names per shape and the wall's end frame at the defaults; a wall's start
  placed on another wall's end collinear and touching in one entry; a 0.5 m
  gap; the call's module snap and surface snap not applied to a connected
  piece; a pillar's base on a block's top with one twist step turned a
  quarter turn; stairs with their top on a floor's front edge reaching its top
  and with their bottom on its back edge starting there, at the poses the
  parameters give; after the target wall is moved and turned,
  `level_get_connectors` frames and `level_connect_pieces` moving the second
  wall onto its end in one entry, undo and redo exact, a second connect no
  entry; after the target's length changes a re-connect at its new end; an
  unknown connector of the new piece or the target listing the valid names,
  a connected piece with a position, a piece connected to itself and a group
  as target or moved node refused with no entry; a locked layout's child
  refused as the moved piece and taken as a target unchanged. Scale (U22):
  `level_get_connectors` of a wall scaled 1.2 and turned and of one scaled
  (1.5, 1, 0.5) equal to the local frames through the node transform; a
  scaled wall joined to an unscaled one, an unscaled wall to a stretched one
  and a wall under a parent scaled 2 and turned, each start on end with the
  axes opposed, the moved wall's scale unchanged, one entry and no entry when
  connected again; a wall under a parent scaled (2, 1, 1) and turned refused
  for the sheared local transform; a connected placement under the unturned
  (2, 1, 1) parent landing on the join. The window:
  no Connect Selected without a selection, the two combos with two selected
  walls, the chosen pair drawn highlighted among the two walls' twelve
  connector indicators, Connect Selected joining the other wall's start to the active wall's
  end in one entry with the logged message, and Place Connected putting a
  pillar's base on the wall's top in one entry with the pillar selected.
  Connector switches, in a scene of their own: a new piece with every
  connector enabled; one connector switched off in one entry, again no entry,
  undo and redo exact; a join naming it as target or moved connector and a
  placement on it refused naming it, an enabled pair of the same pieces
  joining; with All the disabled connector not drawn; the piece switch off
  disabling every connector in one entry (none drawn, a join refused) and its
  undo; two connectors in one call one entry; `set_item_property` of the raw
  list disabling a connector; an unknown name, a locked layout's child, a
  layout node, no switch and a non-boolean value refused with no entry; a
  generated layout child all enabled; raw `set_item_property` lists with an
  unknown name, a wrong-case name, spaces or a repeated name refused naming
  the problem and the canonical list written in one entry; `connectors_enabled`
  on a rock refused by both tools; a duplicated group's wall keeping its
  disabled end and a mirrored one keeping it by name; Replace in one entry
  keeping a wall's front off on the block and dropping its end, undo giving
  both back, and a rock clearing both values; the saved file holding only the
  switches off their defaults, save and reload keeping each piece's switches,
  the file with the switches removed (an older scene) reading every connector
  enabled, and a hand-edited list (`start, Top,zz`) loading as it is with one
  warning naming the piece. Paste Properties through the Properties window's
  context menu: a wall's onto a block in one entry, the block taking the
  wall's record and switches; from the hand-edited piece onto a wall refused,
  nothing pasted, the refusal logged. The window: the
  Target Connector combo without the target's disabled connector; the
  Properties window's Connectors and per-connector rows, one entry per click,
  undone exactly.
  Connector snap (`--only-connector-snap` runs it alone),
  with walls whose expected poses come from the catalog's local connectors:
  `set_surface_snap_settings` round trip of `connector_snap_distance` and its
  refusals; Connector Snap shown in the Level Kit window, the Move rows and
  the toolbar, each typed into setting the shared distance; a pointer X drag
  leaving the wall's start 0.3 m past the other wall's end snapping mid-drag
  (start on end, rotation unchanged, `get_connector_snap` naming the pair),
  Escape restoring the start bit-exact with no entry, a release while
  snapped recording one entry and undo restoring the start, and a drag
  leaving 0.7 m staying where the pointer puts it; a held `drag_selection`
  snapping at 0.3 m, moved to 0.51 m (unsnapped, at the unsnapped drag pose),
  0.5 m (snapped: inclusive), past the target (unsnapped) and back (snapped),
  the release recording one entry, `cancel_transform_drag` while snapped
  restoring the start, no snap without a distance, and the rotate-drag and
  negative-distance refusals; a two-wall selection moving by one offset; a
  group snapping by its member's connector; the nearer of two target
  connectors winning both ways; `transform_selection` snapping in one entry;
  with the target's end, the moved start or the target's piece switch off
  the drag staying unsnapped and `get_connector_snap` counting the enabled
  connectors only, each switch one entry, the drag snapping again after the
  undos;
  200 more blocks (1212 target connectors) gathered and the steps timed
  (printed); a connected `level_place_pieces` under a parent scaled (2, 1, 1)
  and turned refused with `connect_piece()`'s message; a wall scaled 1.2
  snapping onto an unscaled wall's end, an unscaled wall onto a stretched
  wall's end and a wall under a parent scaled (2, 1, 1) and turned onto a
  wall's end; the connector indicators: with Selected a selected wall's six
  at its world frames, none with Off, every piece's with All, a screenshot
  with All differing from one with Off, the refused mode `hovered`, a
  selected locked layout regenerated four times drawing its children with
  a frame cache that does not grow, a held snapped drag with Off showing the five other moved
  connectors, the targets within 4 snap distances and the snapped pair in
  its own color, and nothing once the drag is cancelled; with surface snap the rest first and the
  snap last (the wall lifted onto a raised wall's end, not rested again); a
  locked surface-snapped layout below the dragged wall following the snapped
  pose in the preview and at the release, and `transform_selection` moving
  it so in one entry; motion first (U27): a press and release without motion
  on a wall resting 0.3 m from the pair changing nothing with no entry
  (`skipped` `before_motion`) on the X handle and on the XZ plane handle (the
  translate gizmo has no view-plane handle), a drag out of reach and back to the press pixel
  snapping in one entry, a zero `drag_selection` not snapping and a held one
  moved out and back to zero snapping; the suspend key (U27): the binding
  listed by `list_input_bindings` (left and right Shift), a pointer drag with Shift
  held staying unsnapped (`suspended`), Shift released with the pointer still
  snapping at the key edge and pressed again unsnapping, the release recording
  the unsnapped pose, Escape with Shift held and with Shift and Ctrl held cancelling with no entry, a
  mouse button binding refused, Shift held through a drag and released over
  the Level Kit window leaving the next drag snapping (the count of key edges
  that reached the command printed), and rebound to left Alt, Alt suspending
  while Shift no longer does; Connector Snap On Axis (U27) shown in the three sites, each
  click setting the shared mode, an unknown mode refused, an X axis drag and
  an XZ plane drag of a wall raised 0.2 m keeping its height (`constraint`
  `axis` / `plane`, the y part of `full_offset` dropped) and with
  `full_offset` co-locating, in local space a wall yawed 30 degrees moving
  only along its own X, an X drag with the grid snap and the snap to surface
  on taking the 0.1 m grid step, resting on ground 0.5 m below and snapping
  only in x, `drag_selection` taking the full offset; a locked y translate channel the offset
  needs leaving the drag unsnapped and a locked z it does not need snapping;
  a wall under a parent turned 30 degrees co-locating in world space; Orient
  To Surface with the snap keeping the oriented rest; each scene's close
  leaving no item named by the scene-close watchdog but the Hotbar's
  rendertarget node; and a second editor reading the
  distance and Snap To Surface from an `editor_settings.json` stating them,
  `connector_snap_axis_mode` as `full_offset` from a version 2 `surface_snap`
  and as the `stay_on_axis` a version 4 one states.
  The run first hides the floating windows over a
  viewport (a window `desktop_windows.json` has no entry for opens floating
  there) and gives back `config/editor/editor_settings.json` and
  `desktop_windows.json` as they were. Surface settings, in a scene of its
  own with a slab, a wedge and blocks, driving the gizmo with
  `get_transform_handles` and held `mouse_drag`s that press on a whole pixel
  the probe confirms (its neighbours pick the same handle) and release on a
  whole pixel: the pair shows in the Level
  Kit window, the Move rows and the viewport toolbar, and a click on any of
  the six widgets shows in all three and in `get_surface_snap_settings`; with
  both off an X drag gives the tool's own result; with snap a raised block
  dragged along X rests on the slab as one undo entry, the anchor following;
  mid-drag over the slab's edge it rests on the slab; dragged onto the wedge
  it rests on the highest point under it, and with orient it takes the slope
  normal keeping its heading, one undo entry that undo takes back with the
  rotation; a Y drag lifts it; an XZ plane drag rests it; two blocks rest
  each on its own surface; a parent and its child both selected rest by the
  parent with the child's offset kept; `transform_selection` rests only with
  `surface_snap`; `drag_selection` refuses the options on a rotate drag; a
  `drag_selection` of 50 blocks rests all of them and its steps are timed. A
  `capture_screenshot` of the toolbar buttons and the Move rows goes to
  `logs/level_tools_surface_settings.png` (`--transform-image`).
  Kit layout nodes, in a scene of their own (closed afterwards): a locked
  line of four blocks in a yawed node at known world positions, one entry,
  the node an `Xform` holding them; a grid, a ring and a count mode path
  over an L of corner points at known positions; gap mode lines, rings and
  paths with known counts and positions (7 m with a 0.5 m gap between
  1 m blocks holds 5 at 1.75 m, a radius 4 ring with a 1 m gap 12, a 7 m
  path with 1.5 m blocks 4); a length, a gap and an item width change each
  regenerating as one entry with the counts they determine, undone exactly;
  two items alternating with the first item's slot material; the module
  snap and the surface snap per element; per-element seeds giving three
  rocks three parameter sets, a refresh keeping them with no entry, a fourth
  element keeping the first three; a count and step change as one entry
  whose undo restores the previous children and whose redo makes them again;
  `set_node_transform` moving the run as one entry, and a surface-snapped
  layout moved over the ground resting its pieces again in the move's
  entry; a click-equivalent `select_items` of a child selecting the node and
  `transform_selection` moving the whole run; 15 edit paths refusing a child
  naming the node with nothing changed; unlock as one entry changing no
  child, unlocked children moved, re-parametrized, re-materialed and deleted
  and a `level_set_layout` changing the values alone; lock as one entry
  discarding the edits, its undo giving the edited children back exactly
  with their bodies, redo locking with the same generated children; a
  duplicated group holding a locked layout giving a locked copy with its own
  children, its mirror moving the layout as a unit reported not exact;
  `level_check` over the scene; 14 refused calls; save and reopen keeping
  every layout's values, lock state and children, and a reopened locked
  layout regenerating on a change. 15 more id-addressed tools (flags, tags,
  lock_edit, physics force and drag, navigation seed, a geometry operation,
  CSG with the child as tool, component selection, LODs, a merge of the
  layout, Attach under it, keying) refuse a child naming the node with
  nothing changed; a raw `Kit_layout.pattern` write regenerates as one entry
  whose undo restores the children, a pattern it cannot take, an expression
  and an unreadable text are refused naming `level_set_layout`, raw
  `Kit_layout.locked` false and true unlock and lock as the lock operation
  does. In the item tree, driven with `mouse_click` and `mouse_drag`: a
  child's menu disables Rename, Cut, Duplicate, Paste and Create and shows
  the lock, its Delete deletes nothing; the layout row disables Paste and
  Create and offers Rename; dragging a free piece onto another reparents it
  (the control), dragging the child out and a piece onto the layout change
  nothing.
  Selection paths (`--only-layout-selection` runs them alone), in a scene of
  their own: `select_items` of a child by id and by name, and of two
  children beside a free piece, selecting the layout node once (the reply
  naming it); a viewport click on a child, a hierarchy click on a child's
  row, a shift range from a free piece's row to a child's and Ctrl+A in the
  hierarchy selecting no child; an unlocked layout's children selectable,
  and a lock while they are selected selecting the layout node in their
  place with no replaced child left selected; the lock's undo keeping the
  layout node and the free piece selected with the old children back, its
  redo the same with no child selected; the undo of an unlock selecting the
  node in place of its selected child; `level_create_layout` from the
  selected piece leaving the removed piece out of the selection right after
  the call and its redo, its undo selecting the piece again; no
  `sanity_check()` report of a selected child.
  Path layouts, in a scene of their own (closed afterwards): a path layout
  over an L of corner points is one node of type Curve carrying the values,
  its pieces its children, listed by `get_curves`, giving the former
  polyline's answers (the corner piece on the leaving span); nine pieces on a
  smooth curve at the table's distances with the tangent's heading, and with
  orientation tangent along the tangent; a closed square with no piece twice
  at the seam and its gap mode count; an open curve's gap mode count; a
  `set_curve` moving one point laying the locked layout out in its one entry,
  an unlocked path layout left alone by an edit of its own curve, undo
  restoring the point and the run exactly, redo again; `curve_add_point`,
  `curve_remove_points`, `curve_set_point_kind` and closing the curve each
  laying it out in one entry that undo takes back exactly;
  `set_node_transform` moving and turning the node carrying the same pieces
  rigidly, and a surface-snapped path layout moved over a raised slab resting
  its pieces on it in the move's entry; a piece selected selecting the node,
  the curve tool's target, a viewport gizmo drag of a curve point laying it
  out live with no entry and at the release in one entry (within 0.75 px of
  the pointer travel), the point and the gizmo kept, and a drag of the node carrying
  the same pieces; refusals of the kind rule (a line kind on a Curve node, a
  path kind on an Xform, points for a line, closed without points, the former
  pattern `curve_id`, a `curve_id` naming a piece or a layout already or
  given with a position or a line kind, an unknown orientation, a path
  pattern written on an Xform with `set_item_property`), changing nothing; a
  plain curve made a path layout in place with `curve_id`, one entry whose
  undo leaves the plain curve; `delete_nodes` of the node and its undo giving
  back the same node and pieces; `level_duplicate_group` and copy and paste
  making Curve layout nodes of their own, an edit of the original following
  only it; `level_mirror_group` reflecting the curve's points across the
  plane with the pieces on the mirrored curve, one entry; a save and reopen
  as one Curve node with its pieces and a reopened curve's edit followed; a
  hand-edited copy turning an Xform's line pattern into a path, which does
  not read (the problem reported, a refresh refused, the pieces kept); a
  `capture_screenshot` of the selected path layout with the Properties
  window's Curve and Kit Layout groups goes to
  `logs/level_tools_path_layout.png` (`--path-image`).
  Segment layouts, in a scene of their own (closed afterwards): a 4 m wall on
  a closed curve of four corners as one Curve node of 4 pieces, each scaled
  (s, 1, 1) with its scaled x bounds ending on its corners; a probe block
  inside a scaled wall's far end overlapping it exactly (the collision shape
  scaled); `set_curve` moving one corner laying out the two adjacent pieces
  in one entry with the other two the same nodes, undo and redo exact;
  `curve_add_point` and `curve_remove_points` changing the count; kind path
  and back to segments in place, a line kind refused; a zero-length segment
  leaving the pieces (the run's problem naming it) and the refusals of a
  zero-length segment, a vertical one and a line kind on a plain curve; a new
  segments layout without points holding one wall at its origin at scale 1;
  save and reload keeping the scaled pieces, their collision shapes and the
  layout following a corner edit; a piece selected selecting the node, the
  curve tool's target, and a viewport gizmo drag of a corner with the two
  adjacent pieces spanning it mid-drag, no entry, the release one entry
  and its undo exact; a `render_scene_image` of the room goes to
  `logs/level_tools_segments_layout.png` (`--segments-image`). Live layout
  preview, last, in a scene of its own (a ground slab and a hill): with the
  button held on a Properties row of a locked 12-pillar line, the pillars
  already have the dragged height and the values report it, no entry; the
  release is one entry whose undo gives back the pre-drag children exactly
  (ids, transforms, records) and whose redo the final ones; Escape mid-drag
  restores the children, the cached brush count, the piece count and the
  undo depth; a drag back to the start and a deselection mid-drag record
  nothing and restore the children; a Count drag shows the count in the
  children and the run, one entry at the release; a Step drag into a refused
  0 keeps the last valid children and values (the refusal logged) and its
  release restores with no entry; a gap-mode line's Length and Gap drags
  change the child count mid-drag; a ring's Radius drag moves its pieces to
  the larger ring; a surface-snapped line's Step drag over the hill rests
  every previewed piece where the release rests it; an unlocked layout's
  children stay through a Length drag. `drag_selection` held over the hill:
  a surface-snapped 20-stone path and a surface-snapped ring re-rest their
  pieces mid-drag exactly where the release puts them, a child keeping its
  id exactly when its local pose is unchanged, the release one entry whose
  undo restores the node transform and the children; both dragged together
  follow; a layout without a surface setting keeps its children's ids and
  local transforms. The step times are printed; a mid-drag
  `capture_screenshot` goes to `logs/level_tools_layout_drag.png`
  (`--layout-drag-image`). Transform drag cancel: a pointer drag of a
  block on the translate, rotate and scale handles, cancelled with Escape or
  `cancel_transform_drag` while the button is down, leaves the block
  bit-equal to the drag start through more held motion and the release, with
  no entry, and the next drag records one; a held `drag_selection` drag
  cancels the same way; a cancel without a drag changes nothing; with
  surface snap and orient on, a drag onto the hill that rests and tilts a
  block, a rotate drag of a parent and its child selected together, and a
  drag of a dynamic body (gravity off; at rest at its start 30 frames after
  the cancel) cancel bit-exact with no entry; a pointer
  drag of a locked surface-snapped layout toward the hill re-rests its
  pieces mid-drag, and its Escape cancel leaves the node and its children
  (ids, records, transforms) bit-equal to the drag start.
  Scatter layouts, in a scene of their own over a ground slab and the hill: a
  count mode scatter of 30 over 8 x 6 m (one entry, every piece inside the
  region on the ground, every pair's footprints 0.5 m apart, yaw within -45 - 45 degrees,
  the run's space 48 square meters) and a density mode disc of 14; three
  block items weighted 1, 3 and 0 (the replies' weights and footprint radii,
  the circle around each item's xz bounds; the children exactly the points
  and items of a Python model of the candidate stream - `Kit_random`, the
  four draws, the integer pick, the footprint test in float steps -, the
  weight 0 item never placed, every footprint gap at least the spacing;
  every weight 0 and a weight above 1000 refused) and, after the reopen,
  the last item's Weight row typed 2 as one entry with the children the
  model's for the new weights; another
  seed elsewhere, undo giving the first placement back exactly and redo the
  second; a count raised to 40 keeping the 30 (kept as children); 200 pieces
  2 m apart holding what fits and reporting 200; refusals of a density over
  1024, an inverted yaw range, a yaw beyond 360 degrees, a negative spacing
  and an unknown region,
  leaving the scatter as it was; unlock, a count change taken as a value
  alone, lock making the first 10 points and a refresh keeping all; 16
  pieces over the hill with surface snap, each supported at distance 0 by
  the support report; save and reopen keeping every scatter's children and
  a refresh of each keeping all; the Properties rows clicked by id (Region
  Disc, Region Radius typed, By Density giving 28, Min Spacing typed, Kind
  Line and Scatter again with the values kept), each one entry; a held Yaw
  Max drag of a 200-piece scatter turning the pieces mid-drag without an
  entry and the release's one entry equal to the last step (the median
  preview step printed); the Level Kit window's Layout section showing the
  scatter rows for Scatter.
  Kit layout drawing (`--only-layout-indicators` runs it alone; last in a
  full run), in a scene of its own: a tilted and turned line, a one-element
  line, a turned grid, a turned 120 degree ring, a full ring, a turned
  rectangle scatter, a disc scatter, a density-mode scatter, a path layout
  and an unlocked line; with All the nine non-path nodes listed by
  `level_get_layout_indicators` with no circles while nothing is selected,
  each outline, arc and tick equal in world space to the one computed in the
  script from the pattern values and the node transform (the line's and
  rings' ticks at the children); with Selected only the selected line, only
  the grid above a selected grid piece, a selected scatter piece's scatter
  with a footprint and spacing circle at each child (radius the item's
  footprint radius, plus half the spacing), the same for the selected disc
  and density scatters, and with All only the selected scatter's circles;
  Off listing nothing, a mode "hovered" refused; a camera framing every
  layout node, a screenshot with All differing from one with Off, and a
  close one with the scatter selected differing from it unselected (its
  circles); a value edit, its undo and redo each drawn, with no other entry
  made again; a moved line's outline following with no new cache entry; an
  unlocked layout whose item names an unknown shape or a material that does
  not exist, and one scaled by 2, not drawn and drawn again after undo; one
  naming a scene material drawn, not drawn once the material is deleted and
  drawn after the undo; the items' CPU bounds of a block, rock, bush, wall,
  stairs and tree equal to the box of pieces placed at the origin, float for
  float; a scaled layout with module snap and module 0 refused naming its
  scale; the Properties window hidden mid-drag ending a locked and an
  unlocked value drag as a cancel with no entry; a
  held Size X drag of the locked scatter with the outline at the previewed
  width and the circles at the previewed children and no entry, the
  release's one entry and its undo giving the 6 m outline back; held Step
  drags of the unlocked line showing the pending step with the node's values
  and children unchanged, Escape drawing the node's step again with no entry,
  the release committing the shown step as one entry with the children
  unchanged; a Level lengths change emptying the cache; a deleted layout's
  entry leaving the cache.
  Scatter region handles (`--only-region-handles` runs it alone; after the
  kit layout drawing in a full run), in a scene of its own: the handles of a
  turned rectangle scatter (8, at its corners and edge midpoints) and of a
  disc (4) equal in world space to the ones computed in the script, the same
  in `level_get_layout_indicators`, none for a line (whose handle selection
  is refused), the node the target when one of its pieces is selected; for
  the turned rectangle's +x edge, -z edge and +x +z corner, the disc's +x
  handle and a density scatter's +x edge, the gizmo anchored at the
  selected handle and a `transform_selection` of it setting exactly the
  values the node-frame point gives (a 6 x 4 m density scatter holding 12
  pieces), the other values kept, one entry, undo exact and redo; an edge
  pulled past the origin clamped to 0.01 m; a held `transform_selection`
  (`end_edit` false) previewing the 9 m region with 6 regenerated pieces
  and no entry, `cancel_component_edit` restoring exactly; in the viewport a
  click at the -x edge handle selecting it with the node still selected, a
  plain click off the handles clearing it and the gizmo back on the node,
  the node gizmo dragging the node 0.5 m with the values kept; a held gizmo
  drag of the locked density scatter's +x edge previewing a wider region
  with more pieces and no entry, Escape restoring exactly, a release of one
  entry with size_x 4 + 2 x the drag within the drag tolerance and the
  pieces of the last step, undo exact and redo; a press and release without
  motion recording nothing; a held drag of the locked disc's +x handle
  keeping 5 regenerated pieces and its release of one entry; held drags of
  an unlocked scatter showing the pending size on the outline's handle with
  the node's values and children unchanged, Escape drawing the node's
  region again, the release recording the pending size as one entry with the
  children unchanged; and a screenshot with the handles of the selected
  turned rectangle (`logs/level_region_handles.png`).
  Segment handles, in a scene of their own (closed afterwards): a room, a
  trapezoid and an open 3-point segments layout and a line layout; the
  room's 4 handles at its chord midpoints with their normals; a line
  layout without handles and the refusals of a line layout and an index
  past the segments; `level_move_segment` of wall 0 by -1 m moving its two
  corners 1 m along the normal with the opposite corners unchanged, piece 0's
  scale kept and its neighbours' grown to 7 m, one entry, undo and redo
  exact; the trapezoid's base corners on its slanted sides' lines; the open
  curve's free end moving along the normal; in the viewport a click on a
  midpoint selecting the segment with the gizmo there, a click on a corner
  selecting the point and clearing the segment, Ctrl+clicks selecting one
  segment at a time; a held XZ plane drag of wall 0 by (0.7, 0, 1) moving
  its corners along z alone with the pieces following and no entry, the
  release one entry, undo exact, Escape mid-drag restoring exactly; a point
  still dragging alone; and a screenshot with a selected segment handle
  (`logs/level_tools_segment_handles.png`, `--segment-handles-image`).
  `--only-walkable` runs the walkable level, persistence (of the walkable
  level) and window checks alone, `--only-transform` the surface settings
  checks, `--only-layouts` the kit layout node checks (path layouts
  included), `--only-path-layouts` the path layout checks,
  `--only-segments` the segment layout checks,
  `--only-stairs-layout` the stairs layout checks (a straight slope, a
  flight, a level curve, the default curve, a smooth S-curve, a quarter turn
  at a corner point and an up-and-down curve: the fit readback, the mesh
  report, the body; the refusals at creation; a curve edit, a value edit and
  their undo and redo; refused curve edits leaving the staircase; lock;
  kind switches among stairs, path and segments; the navigation mesh joining
  the S-curve staircase's first and last tread; Level Check findings for a
  step rise, a slope and a width the agent does not pass; save and reload
  with the loaded child matching a fresh generation; a viewport
  drag of a control point with the staircase following; the pictures
  `logs/level_tools_stairs_layout_<view>.png`, `--stairs-image-prefix`) and
  `--only-window-layout` the Level Kit window checks up to its Layout section,
  the Properties Kit Layout group and the Properties write, and
  `--only-layout-drags` the live layout preview checks,
  `--only-drag-cancel` the Transform drag cancel checks,
  `--only-snap-to-surface` the snap to surface checks, `--only-replace`
  the replace checks (with the Level Kit window checks its window check
  needs), `--only-layout-selection` the selection paths of a locked
  layout's children, `--only-scatter` the scatter layout checks,
  `--only-connector-snap` the connector snap checks,
  `--only-layout-indicators` the kit layout drawing checks,
  `--only-region-handles` the scatter region handle checks,
  `--only-segment-handles` the segment handle checks,
  `--only-layout-connectors` the layout connector switch checks (an open
  path of five walls and a closed segments room of four: nothing hidden by
  default; each switch alone, Hide All and a combination hide exactly the
  connectors whose direction the script computes from the readback's world
  frames, the readback's directions and reasons agreeing; an inner wall faces
  the six directions; the path's first and last wall keep all but the
  neighbour-facing one; each change one undo entry, undo and redo exact; the
  indicators draw exactly the enabled connectors after a change, its undo
  and redo; a join to a hidden connector refused; a held drag's connector
  snap picks the nearest enabled connector, a hidden one never; count 6
  moves the tail; closing the curve makes the ends inner; unlocked children
  hide nothing; line and grid layouts keep and ignore the values; save and
  load keep them; and the screenshots
  `logs/level_tools_layout_connectors_off.png` and `..._tangent.png`, the
  path with Connectors "all" before and after +/- tangent are hidden) and
  `--only-recreated-shapes` the checks of the recreated shapes (the catalog
  lists each directly after its original with the same contract and default
  bounds; each pair placed at values off the defaults has equal local
  bounds, body kind, materials and connectors; the recreated stairs report
  their original's `stairs` layout; Replace original ->
  recreated -> original carries every parameter and material, keeps the
  node's place and returns the original piece exactly; the recreated door
  wall between two walls has their ends, faces and top, the recreated arch
  between two walls has its original's footprint, `level_check` reports no
  overlap and no unwalkable opening for them, a navigation path leads
  straight through the door wall's opening and the arch, and none does once
  the door wall is replaced by a wall) and
  `--only-more-shapes` the checks of the more shapes, first the cliff, palm, shore and water: the
  catalog (36 shapes, ceilings 2000, each new shape's slots, collision and
  mesh kind and connectors, the sand and water palettes), the four at their
  defaults (bounds against their values, default materials, the static
  triangle mesh bodies, the water without a body and with
  `pass_through`), values at their range ends accepted and beyond
  refused naming the parameter, derive from seed, surface snap onto a slab,
  a scatter of palms and cliffs (footprints the circle about the origin
  around each item's xz bounds), Replace (a palm by a tree and back, a cliff
  by a land mass, a shore by water and back with the body and the flag
  following, undo and redo), a navigation build from two shore tiles under a
  water piece (the water not input, the shore walked from berm to lip, no
  walkable polygon on the open water), and the shape images
  `logs/shape_<id>_<n>.png` (each shape at its defaults and at contrasting
  values, a 1.8 m block beside it); then the second batch's checks: the
  catalog (31 shapes, each new shape's slots, collision kind, connectors and
  culture tag, the wood palette), the six at their defaults (bounds against
  their values, default materials, hull and triangle mesh bodies, the leaf
  plant without a body and with `pass_through`), range ends accepted, values
  beyond and refused combinations (a tower narrower inside than its
  doorway, a torii whose posts meet) refused, derive from seed (the leaf
  plant's height, leaves and droop, the torii's curve), surface snap onto a
  slab, a line layout of five columns at 2 m and of three battlements end to
  end on a wall's top, the joins (a column on a slab's top carrying an
  entablature centered on it, a battlement on a wall's top, a flat tower
  carrying a second segment, a wall joined to a tower's right side ending
  inside its wall), Replace (a pillar by a column and back, a wall by a
  battlement and back, the body kind following), a navigation build (the
  tower's hollow reached through its doorway, a path straight through a
  torii, the tower's top a walkable floor, the leaf plant not input), and
  the images: each new shape at its defaults and contrasting values, and one
  composed image per culture (`logs/shape_classical_1.png`: a colonnade on a
  stylobate carrying entablatures; `logs/shape_medieval_1.png`: a wall run with
  battlements from a round tower's side into a square tower's;
  `logs/shape_japanese_1.png`: four torii along a path); then the third
  batch's checks: the catalog (each new shape's slots, collision kind,
  connectors and default mask, the roof palette), the seven at their defaults
  (bounds against their values, static bodies of their collision kinds),
  range ends accepted and values beyond refused, derive from seed, surface
  snap onto a slab, a scatter of spires, clumps, shrubs and dunes (footprints
  around each item's xz bounds), Replace (a rock by a spire, a palm by a
  clump, a hill by a dune, each and back), a roof joined by its front to a
  door wall's top over a room of four walls, a navigation build (a path over
  a dune, points on its crest, a ground patch and the basalt tops), and the
  images: each new shape at its defaults and contrasting values, and one
  composed image per theme (`logs/shape_rocky_coast_1.png`: a cliff run with
  sea stacks, a pinnacle, basalt columns and rocks in the water;
  `logs/shape_roofed_house_1.png`: a gable roof on a door wall and three
  walls over a floor; `logs/shape_beach_corner_1.png`: shore tiles into the
  water, dunes, a palm, palm clumps, shrubs and a ground patch).
  `--only-piece-lights` runs the checks of lights on pieces (D3.15) with the
  lit shape `--piece-lights-shape` names (default
  `architecture.stone_lantern`): the light nodes of a placed piece on its
  frames with the starting values; an edited light (intensity, shadow
  casting) kept through a parameter change, its undo and redo; a Properties
  value drag with the light node already where the release puts it mid-drag,
  one entry at the release, its undo, and a deselect mid-drag putting the
  node back without an entry; a light node moved off its frame put back by
  the next regeneration; a user's copy of the kit light kept as a plain light
  by a regeneration (its record back on undo) and by a save and load; a light
  node a regeneration adds under a sheared parent chain exactly on its frame; a duplicate's light
  nodes carrying the edits; Replace by `basic.block` removing them in its one
  entry and its undo giving the same nodes back; a locked line layout's
  pieces lit with the starting values and kept by a refresh; and save,
  close and load keeping the edits.
  `--only-create-menu` runs the checks of U46 and U47 (the full run runs
  them last, with the earlier groups' viewports left as unselected dock
  tabs): the Level Kit window with the kind Stairs offering Add Path
  Stairs and disabling Make Layout; Add Path Stairs making a locked stairs
  layout at the origin with a generated staircase that Level Check passes,
  selected, one undo entry, undo and redo; Make Layout On Curve on a plain
  three-point curve; Commands > Create > Level Kit > Layouts making each of
  the seven kinds at the scene root, with `basic.block` as the item,
  selected, one undo entry each; every catalog shape listed under its kit
  by display name; the Scene Hierarchy window's Create > Level Kit placing
  the first shape of each kit and Path Stairs under a translated node at its
  origin; the layout entries disabled under a kit layout node.
  `--only-wrap-with-path` runs the Wrap with Path checks (U45), which the
  full run ends with: every refusal over MCP (no node, a group, a plain
  mesh, a light, a camera, a kit layout node, a locked layout's child, a
  scaled piece, a piece with a user child, a piece with a connector switched
  off, a good piece with a bad one, a piece of an unlocked kit layout) naming
  its reason and changing nothing;
  a yawed piece with values and a material off the defaults under a
  translated parent wrapped: one locked path layout in gap mode with gap 0,
  its one item the piece's, a straight curve 3 extents long holding 4
  pieces, the piece's parent, index and world transform, element 0 on the
  piece's pose and the others along its x axis, the node selected; one undo
  giving back the piece exactly (record, materials, body, place, transforms)
  and the selection, redo the same layout; four selected pieces in two
  parents in mixed order: one layout in the first's place in hierarchy
  order with the two distinct items cycling, one undo giving back all of
  them; a wrapped `architecture.stone_lantern` with an edited light: every
  element lit on its frames, undo giving back the edited light node; the
  context menu: the entry greyed on a group, a plain mesh, a kit layout node,
  a scaled piece and the new layout's row, enabled on a piece, wrapping an
  unselected clicked row alone and a selected row's whole selection; save,
  close and load keeping the wrapped layout's kind, values, curve and
  children.
- `Mcp_test.document_edits_record_one_undo_entry_each` places three pieces as
  one undo entry, changes a piece's parameters and snaps a raised piece onto
  the one below it, groups it, mirrors the group, duplicates it and ungroups
  the copy, creates a kit layout node, changes it into a gap mode line,
  unlocks and locks it, makes a layout from a placed piece, creates a path
  layout with its points, edits its curve (the layout laid out in the same
  entry), sets the path's orientation, makes a plain curve a path layout and
  makes a path layout from a placed piece, each one undo entry.

## Future work

- [Level tools: requirements](../plans/level_tools.md)
- [Level tools: design](../plans/level_tools_design.md)
- [Level tools: implementation steps](../plans/level_tools_implementation.md)
