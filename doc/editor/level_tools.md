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
  `former_ids` ("Former shape ids"), kit, display name, description, tags, parameters, its material slots
  ("Materials"), normal style (`smooth` or `smooth_with_hard_edges` with its angle, by default 50
  degrees: the style the `shading` parameter's `smooth` value takes) and default shading
  (`doc/editor/level_kit_shapes.md` "Shading"),
  triangle budget, collision kind (`Kit_collision::convex_hull`,
  `triangle_mesh`, `box` or `none`, "Kit brushes"; optionally per values,
  `get_collision`), mesh kind (`Kit_mesh_kind::solid`,
  a closed manifold, or `surface`, a stated surface whose boundary is open:
  the water), randomness (`Kit_randomness::seeded` for the shapes whose
  generator draws from the seed: every nature shape but the water), the
  mirror planes ("Groups") and the generator.
- `Kit_parameter`: name, type (`float_value`, `int_value`, `enum_value`), unit,
  range and default, its derive range ("Derive from seed") and whether the
  canonical text leaves a float or enum parameter out at its default
  (`Kit_text_presence`; the level of detail every shape takes and the rocks'
  shading, `doc/editor/level_kit_shapes.md` "Level of detail" and
  "Shading"). A float
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
  triangle ceiling (`Kit_definition::triangle_ceiling`, R2.6), the shapes
  (`find()` resolves an id or a former id), and the palette ("Materials"). Adding a shape is one generator and one
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

## Former shape ids

`Kit_shape_definition::former_ids` lists ids a shape had before.
`Kit_shape_library::find()` resolves a former id to its shape, as
`erhe::property` resolves former property names, so every saved piece
(`Kit_piece.shape`), layout item text and MCP shape argument that names a
former id is that shape. No id is another shape's id or former id (the
library verifies this when built). Eighteen shapes have the former id of
their id plus `.v2` (`basic.block.v2` for `basic.block`): the basic shapes,
the door wall, pillar, arch, battlement, tower, rock, land mass, hill, tree,
bush, rock spire, leaf plant and palm shrub (`doc/plans/level_tools.md` U55).

A piece record that names a former id reads by the parameter-text rules of
"Derive from seed" (`read_kit_piece_record()`, `kit_shape_library.hpp`): a
parameter text that names a parameter the shape lacks, or lacks one it has (a
saved rock's `bevels`, a saved tree's `canopy`), does not read, and the piece
keeps its loaded mesh (R8.4); so does a record whose mesh has not a primitive
per slot of the shape. A piece that follows the kit records the shape's id,
as an `erhe::property` former name migrates: the load refresh writes it
(`Level_kits::refresh_piece()`, without an undo entry), a parameter edit or
its preview's release records it with the edit (its undo gives the former id
back), and a layout's regeneration replaces a child that names a former id by
one of the shape's id. The next save writes the shape's id; a piece that
keeps its loaded mesh keeps the text it was saved with. A layout's item text
(`Kit_layout_item.shape`) keeps the id it names; an item made from a piece
takes the shape's id.

## Source files

A batch of shapes (`build_13_16`, `build_17_20`, `build_21_24`,
`extra_01_08`, `extra_09_16`, `extra_17_24`, `extra_25_32`, `extra_33_36`,
`machine_01_08`, `machine_09_18`) edits only its generator file
`kit_shapes_<batch>.cpp`, its expectation rows
`src/editor/level/test/kit_shape_rows_<batch>.cpp` and its tests
`src/editor/level/test/test_kit_shapes_<batch>.cpp`; the tests that go over
the library take the union of every batch's rows. The shapes of
`kit_shapes_basic.cpp`, `kit_shapes_architecture.cpp`,
`kit_shapes_medieval.cpp`, `kit_shapes_nature.cpp`,
`kit_shapes_nature_plants.cpp` and `kit_shapes_nature_rocks.cpp` have their
rows in `kit_shape_rows_starter.cpp` and their tests in
`test_kit_shapes.cpp` and `test_kit_shapes_<file>.cpp`.

## Shape images

`scripts/level_shape_render.py [--editor <binary>] [--only <id or id
prefix>] [--parameters <json object>] [--tag <name>] [--detail
<x,y,z,distance>] [--azimuth <degrees>] [--elevation <degrees>] [--sun
<intensity>] [--ambient <level>] [--light-shadows] [--save <path>]
[--keep-open]` launches the editor and builds one scene with a row per shape
of `level_get_kits` (or the shape `--only` names, or the shapes whose id
starts with it) at the default values, each piece alone on a ground pad of
its own (floor slabs, tiled where the pad is wider than a slab's 64 m); a
seeded shape makes three rows, at seeds 1, 3 and 17. The rows run toward -Z.
It writes one three-quarter view per row, `logs/shape_render_<id>.png` (a
seeded shape's `..._<seed>.png`), every row of one size class through the
same camera, its far plane ending the view behind the row. `--parameters`
gives every piece the given values (used with `--only` naming the shape they
belong to; a value for a parameter that is derived by default needs
`"derive": []` in the same object) and `--tag` ends the image names with
`_<name>`, so such a run keeps the default images; `--detail` renders a
close-up of the point x, y, z of the piece's own frame from the given
distance (a negative x is written `--detail=-1,2,0,6`); `--azimuth` and
`--elevation` turn the camera. `--sun <intensity>` adds a directional light
and `--ambient <level>` sets the scene's ambient light: a scene whose only
light is a piece's own renders everything that light does not reach black,
so a lit shape is rendered with both; `--light-shadows` turns shadow casting
on for the pieces' light nodes. `--save` writes the scene for the windowed
editor to open; `--keep-open` leaves the launched editor running with the
scene.

`scripts/level_shape_contact_sheet.py --tags <left> <right> --out <png>
[--crop <w> <h>] [--scale <s>] <ids>` puts the images of two tags of each
shape side by side on one sheet, the middle of each cropped: a before and
after review of many shapes at once.

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
  within its stated angle. The style is the values' (`get_kit_shading()`):
  the `shading` parameter at `faceted` makes the piece faceted. The averages weigh each facet by its angle at the
  vertex, so a quad split into two triangles counts as the quad. A corner the
  generator gave a normal at a hard edge keeps it
  (`doc/editor/level_kit_shapes.md` "Hard edges").
- Collision: the kind at the values is `get_kit_collision()`: the shape's
  `get_collision` at the values when it states one, else its `collision`. A
  `convex_hull` shape takes the brush's convex hull; a
  `triangle_mesh` shape (stairs, door wall, arch, entablature, torii,
  battlement, tower,
  land mass, tree, cliff, shore, palm) has a collision shape generator that builds a triangle mesh
  shape from the generated mesh (`build_triangle_mesh_shape()`,
  `scene/collision_shape_from_mesh.hpp`). A `box` shape (U54) has a
  generator that builds a box of the mesh bounds (`get_kit_geometry_bounds()`,
  `build_box_shape()`): a compound of one box at the bounds' center, so the
  box sits in the piece's frame like the mesh; its mass and inertia are the
  box's, as a hull's are the hull's. A shape is a box where its convex hull
  fills at least 90 percent of its bounds' volume and nothing open is left to
  reach (an opening, the space under a deck, steps, a missing fragment):
  always the block, the floor slab and the wall; per values the square
  pillar, the stepped platform of one tier, the crate with its lid closed or
  left out, the louvre box vent, the container with its doors closed, the
  quay wall without fenders and batter and the cracked floor with every
  fragment in place and level (each other value keeps the shape's own kind).
  `editor_level_tests` (`test_kit_collision.cpp`) holds every shape of the
  library with its kind and, for a shape whose hull fills 90 percent of its
  bounds at values that keep another kind, the reason; it checks the hull of
  every box at the defaults, the range corners and, for a kind per values,
  the derived values and each choice of the values that decide it.
  The box follows the node's world scale as every body's shape does (a
  non-uniform scale gives a box of the scaled extents). A `none` shape (the water and the
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
  with a surface option on, only across the surface direction: the
  coordinate along the direction's dominant world axis (ties prefer Y, then
  X) instead puts the point back on the plane through the given position
  across the direction, so on a floor the height is kept and on a slope the
  other two coordinates stay on the module lattice
  (`snap_point_to_module()`);
- each slot's material is the given one, else the slot's default palette
  material ("Materials"), found in the scene's content library by name or
  created and inserted there the first time a piece needs it;
- the surface options of the placement (`Kit_placement::surface`, "Surface
  placement") move and turn the module-snapped transform; with
  `Surface_options::attach` the piece goes under the support its rest lies
  on, with a surface attachment record ("Surface attachments");
- `make_brush_instance_insert()` (`brushes/brush.hpp`) makes the node with a
  static rigid body, a mesh primitive per brush part with that slot's
  material, and its insert operation; the node takes the given name
  or the shape's display name and its `Kit_piece` record before the insert.

All material inserts and node inserts of the call form one
`Compound_operation`, run through `Operation_stack::execute_now()`: one undo
entry, which removes every piece of the call and the palette materials it
added.

Foliage items ("Foliage paint") are made by
`Level_kits::make_foliage_operation(scene_root, placements, result)`. Each
placement names `Kit_placement::foliage_radius` (meters, 0 - 100) and,
through `Kit_placement::foliage_support`, the mesh node it goes under (none:
the scene root). Its parent is its support: a foliage placement with
`parent` or `parent_index` is refused. The call checks the placements as
`place()` does, and also refuses a foliage placement with a connector join,
with a support of another scene, or with a support and a `surface.direction`
that is not a unit vector. A foliage placement takes its
position and rotation as given: no module snap, no surface search, no
connector join and no attachment follow operation. The node gets
`Editor_item_flags::foliage` and its `Kit_piece.foliage_radius` before the
insert. Under a support `find_surface_support()` accepts it also gets the
surface attachment record `make_surface_record()` makes from its world
pose, `surface.direction`, `surface.orient` and `surface.rest`, whatever
`surface.attach` says; under any other mesh it is a plain child. Its body
and `pass_through` follow the shape as for every piece. The call returns
its material and node inserts as one `Compound_operation`, not executed (null
with the refusal in `result.error`); the stroke runs it.

## Pattern expansion

The arrangement of a kit layout node ("Kit layout nodes", R5.4, D3.7) is a
pattern expanded from a start transform into one pose per element.
`level_repeat.hpp` is that scene-free part, built into `editor_level_tests`:
`Repeat_pattern` (kind `Repeat_pattern_kind` line, grid, ring, path,
scatter ("Scatter layouts") or segments and the fields of each) and `expand_repeat(pattern, start, out_poses)`, which
returns the poses of the run with element 0 at the start, or a refusal and no
poses. A pose is a position, a rotation and a scale, which every kind keeps
at 1. A kit layout node expands from its own origin
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
  frame's, its scale 1, and its origin placed so that the item's x bounds
  are centred on the chord's plan midpoint at point i's height. With
  `tangent` its x axis is the chord itself and its up axis the start frame's
  up made perpendicular to it, centred on the chord's midpoint. The layout
  gives each piece whose shape maps `child.length` the span's length (the
  chord's plan length, or the chord's with `tangent`) before the pose is
  made, so such a piece spans the chord end to end (L24). Every segments
  piece has its own values, so preparing a segments layout (each generation,
  and each step of a live drag of its curve) builds each element's geometry
  bounds. A segments layout saved with scaled pieces keeps them until it is
  laid out again, which replaces them with unscaled ones.

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
snaps to the nearest place within the layout's search distances of its point
(a scatter over a hill), else stays at its point. Make Layout from a
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
  original alone. A surface attachment inside the copy names the copied
  support ("Surface attachments").
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
  `surface_orient`, and the search of the surface snap (U51):
  `surface_direction` (world space, normalized on use, (0, -1, 0) when a
  saved node has no value; (0, 1, 0) attaches the elements to a ceiling),
  `surface_forward_distance` (meters along the direction, 1 when a saved
  node has no value), `surface_backward_distance` (meters against the
  direction, 0 when a saved node has no value) and `surface_support`, the
  support rule of the elements' snap (U52, "Surface placement"; an
  enumeration saved as its name, `five_lines`, `center_blocks` or
  `center_fallback`, `five_lines` when a saved node has no value). A node saved before U51
  holds `Kit_layout.surface_max_distance`, a distance searched either way:
  read as both distances while the node holds neither
  (`legacy_surface_max_distance_property`); the first value write of a
  layout writes both, so the node no longer falls back on it. A zero or non-finite
  direction or a negative or non-finite distance makes the values not read.
  They are the snap settings applied to every element as a placement
  applies them (`Kit_snap`, `get_layout_surface_options()`);
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
  a piece without children but its kit lights and surface attached pieces
  whose record (shape and parameter text), slot
  materials by name and local pose (translation, rotation and scale) equal the element's. The other
  children are removed with their subtrees, last child first (their
  attached pieces leave them first, "Kit layout nodes"), and the
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
  - The lock ends at a surface attached piece (S5.1, "Surface
    attachments"): `find_locking_kit_layout()` stops at the first node, the
    item itself or an ancestor, whose surface attachment record names its
    parent (`is_surface_attached_to_parent()`), and every rule above goes
    through it, so a piece attached to a child of a locked layout, and what
    is below it, is selected, moved, edited, deleted and detached as any
    piece. An insert under a locked layout's child stays refused; the attach
    paths insert and reparent onto a support directly.
- A regeneration keeps a layout's attached pieces (S5.2, S5.3): a child is
  kept when its children are kit lights and attached pieces
  (`has_only_kit_light_and_attached_children()`; a generated floor or
  staircase, attached pieces alone), and keeps them untouched. A replaced
  child's attached pieces leave it before its removal
  (`append_generation_removals()`): parked under the scene root, keeping
  their local transforms (`Kit_surface_rehome_operation`), and after the
  inserts moved under the element's new child, their records naming it,
  then carried on its shape by a follow operation
  (`append_generation_arrivals()`). The attached pieces of a child removed
  without a successor (the count shrank) are detached to their home at
  their world poses, with an `editor.scene` info line each (P7). The same
  holds for locked and unlocked layouts, a lock (which replaces every
  child) included; a preview step does the same and is undone with it.
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
  neighbouring control points, unscaled: a piece whose shape maps
  `child.length` takes the segment's length (the chord's plan length, or the
  chord's with tangent orientation) as that parameter, and every piece's x
  extent is centred on the chord ("Pattern expansion" has the pose;
  `doc/plans/level_kit_connections.md` L24), so a wall's length follows its
  room's corner points and a piece without the mapping stands at its own
  size at the segment's middle. Everything "Path layouts"
  states holds for it: the curve tool shows and drags its points and
  handles (a handle changes no piece), each curve edit is one
  `Curve_set_operation` whose follow-up lays the layout out again (only the
  pieces of the changed segments are new nodes; the others match and are
  kept), a point drag previews live, a move of the node carries the pieces
  rigidly. Points the curve tool and `curve_add_point` add between corner
  points are corners. Each segment is a handle as well: a drag moves a wall
  along its normal ("Segment handles").
  - Each piece keeps scale 1 and has its own parameter values (its length;
    with per-element seeds its seed too); the layout node keeps world scale
    1. Module snap is not applied (the pieces
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
  layout handle ("Layout handles").
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
  - A move preview's step also runs the property flow of the step after the
    generations ("Flow preview").
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
  (the node, a node below it or a node above it), per generated point two
  circles about the point's vertical axis: the footprint circle, of its
  item's footprint radius ("Scatter layouts"), and the spacing circle, of
  that radius plus half of `min_spacing`. Two points exactly the minimum gap apart show touching
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
  are selected, have a selected node below them or are below a selected
  node, as the Connectors setting walks down from a selected node) or All
  (every kit layout node of the view's scene). A node is drawn only where a
  generation of its
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
  `index`, `value`, world `position` of a line's, grid's, ring's or
  scatter's handles; "Layout handles").

## Layout handles

A selected line, grid, ring or scatter kit layout node shows handles on its
outline in the viewport, and a drag of one sets the values it stands for
(U28 for the scatter's region, step 64 for the others,
`level/kit_layout_region_tool.hpp`). The tool is a point set tool
(`doc/editor/point_set_editing.md`), its points the handles:

- Which handles: the outline's `Kit_region_handle` list
  (`Kit_layout_outline::region_handles`, made with the outline from the
  values it draws, so the handles sit on the drawn outline and follow a live
  preview and a pending outline), in the node's xz plane (a line's along its
  direction) and through its world matrix, so a turned or moved node carries
  them. Per kind, in index order:
  - line in count mode: its run's end, the last element's center, editing
    `step` (the end is count - 1 steps out; Count stays). A run of one piece
    has no step to set and no handle;
  - line in gap mode: the end of its length along the direction, editing
    `length` (from the first center to the last; Gap stays, so the count
    follows the length). The handle is at the length's end even when the
    length holds a single piece;
  - grid: the far corner (the outline's corner away from element 0, at
    count - 1/2 spacings along each axis), editing `spacing_x` and
    `spacing_z`, then the middles of the far x edge (`spacing_x`) and of the
    far z edge (`spacing_z`); the counts stay;
  - ring: the arc's middle (the point opposite element 0 on a full ring;
    60 degrees round from element 0 on an arc under 120 degrees,
    `c_ring_radius_handle_min_degrees`, so it stays one radius from element
    0 and the node's gizmo), editing `radius`, then the arc's end, editing
    `arc_degrees`, placed at 3/4 of the radius from the center
    (`c_ring_arc_handle_inset`) so that a full ring's end handle stays clear
    of element 0 and the node's gizmo;
  - scatter rectangle: its 4 corners in the polyline's order (-x -z, +x -z,
    +x +z, -x +z), each editing `size_x` and `size_z`, then the midpoints of
    the edges between them (-z, +x, +z, -x), each editing the size across
    it; scatter disc: 4 on its circle at +z, +x, -z and -x, each editing
    `region_radius`.
- When they show: while the kit layout node is the only selected item (a
  piece of a locked layout selects the node), in every viewport of its
  scene, whatever the view's Kit Layouts setting, as the curve tool's
  handles show whatever the Curves setting. A node whose outline is not
  drawn (values the generation refuses, a world scale other than 1) shows
  none; a path, segments or stairs layout has none here (a segments layout
  has its own, "Segment handles"). They are drawn and picked as every point
  set's: camera-facing circles of the Point Handles style, orange, white
  hovered, yellow selected, picked within 12 px.
- Selection: a click on a handle selects it; one handle is selected at a
  time (two handles of one value would pull it two ways), and Ctrl or Shift
  on the selected handle deselects it. A plain click off the handles clears
  the handle first, so the next click reaches the object selection. The
  handle stays selected through its drags and their undo and redo; a change
  of the target, or of what its indices name (`Kit_layout_handle_set`: the
  kind, a line's count mode or whether it has a handle, a scatter's region),
  clears it.
- With the Transform gizmo: with a handle selected the gizmo sits on it in
  the node's frame (Local mode turns with the node) and moves it; with none
  the gizmo is the node's, at its origin. The gizmo's handles win the pick
  over a layout handle under them, so a handle on a gizmo arrow (the +x edge
  of an unturned scatter, below the node's X arrow; a short line's end) is
  picked where it is clear of the arrow or from another view; an unhovered
  layout handle takes no click from the gizmo or the object selection.
- The drag: the handle's position at the press moved by the gizmo's
  world-space delta, taken into the node's frame, gives the new values
  (`apply_region_handle()`, scene-free):
  - a line's `step` is the point's reach along the run's direction over
    count - 1, and its `length` the reach along the line's direction; motion
    across the line counts nothing;
  - a grid spacing is the point's reach along its axis over count - 1/2
    (the far edge is half a spacing beyond the last center); motion along
    the edge counts nothing;
  - a ring's `radius` is the point's reach along the radius handle's
    offset from element 0 per meter of radius (element 0 stays at the
    node's origin and the center moves with the radius, so the handle moves
    along that offset), and its `arc_degrees` the angle of the point about
    the center, counter-clockwise from element 0 seen from above, taking the
    turn nearest the drag's previous angle before the clamp
    (`Kit_layout_handle_drag`): an end dragged on past element 0 holds a full
    ring until the pointer turns back past element 0, however far it goes,
    and one dragged back past element 0 holds the least arc the same way;
  - a scatter's region is centred on the node's origin and resizes
    symmetrically about it: an edge's size is twice its reach from the
    origin along its side, a corner's both sizes, a disc's radius its
    distance from the origin in the xz plane; the node's y is ignored. The
    pattern is defined about the origin (a scatter's points scale with the
    region about it), so moving the node to hold the opposite edge would
    also move every piece's frame and make one drag two edits.
- Ranges (`level/kit_layout_values.hpp`): a line step and a grid spacing
  keep their sign and a size of `c_min_layout_handle_step` (0.01 m) up to
  the Properties row's maximum, 1000 m, so an end or edge pulled past the
  origin holds 0.01 m (the rows also take 0 and the other sign, which a
  handle never sets); a line length 0 - 10000 m; a ring radius 0.01 - 1000
  m and arc 1 - 360 degrees and a scatter size or region radius 0.01 -
  10000 m (`c_min_scatter_region_size`, `c_max_scatter_region_size`), the
  rows' ranges. A gap mode line of length 0, or a count mode line at the
  least step, has its handle on or beside the node's origin, under the
  node's gizmo: the Length or Step row brings it out again. The gizmo path's constraints and the Move
  tool's translate snap (Snap Enable or Ctrl) act on the handle's position;
  the XZ plane handle in Local mode keeps it on the node's plane. A rotate or
  scale drag moves nothing (it turns and scales the handle about itself).
- Steps: a step whose values differ from the previous step's goes through
  the path a Properties value drag takes: a locked layout through
  `Level_kits::preview_layout_values()` ("Live layout preview"; the children
  regenerate, a gap mode line with the count the new length gives and a
  scatter in density mode with the count the new area gives), an unlocked
  one through `Kit_layout_indicators::set_pending_values()` (the outline
  shows the pending values, the node and its children stay). Values a step
  cannot take keep the last shown step. Nothing runs while the pointer
  rests.
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
  (`id`, `name`, `scene_name`), its `handles` (`index`, `value` - `step`,
  `length`, `spacing_x_z`, `spacing_x`, `spacing_z`, `radius`,
  `arc_degrees`, `size_x`, `size_z`, `size_x_z` or `region_radius` - and
  world `position`), `selected` and `gizmo_on_handle`. A script drags a
  handle with `get_transform_handles` and `mouse_drag` as a user does, or
  moves it exactly with `transform_selection` (the anchor's world
  translation; `end_edit` false holds the edit as a held drag).

## Segment handles

A segments layout or a floor layout that is the curve tool's target (the only
selected item; a piece of a locked layout selects the node) has a handle per
segment (a floor's edge) beside its control point handles, and a drag of one
moves that segment parallel to itself along its normal (U38, R5.12.11, L25). The curve tool
(`tools/curve_tool.hpp`) holds them as further entries of its point set
(`doc/editor/point_set_editing.md`): after the three entries of every
control point, entry 3n + i is segment i. A layout of either lock state has
them; another curve or another kind has none (the tool reads the kind when
its target changes and at each `Kit_layout_changed_message` of it).

- Which handles: segment i runs from control point i to i + 1, the last
  segment of a closed curve to point 0; its handle sits at the chord's
  midpoint. It is drawn and picked as every point of the curve tool (a small
  translate gizmo the pointer brings the Transform gizmo to,
  `doc/editor/point_set_editing.md` "Hover gizmo"; a click within 12 px
  selects it), and the hovered and the selected
  segment's chord is drawn over the curve twice the selected curve width,
  in the Point Handles hover and selected colors.
- Selection: a click on a segment handle selects it, one segment at a time;
  Ctrl or Shift on the selected one deselects it, on another selects that
  one. Selecting a segment clears the point selection and selecting a point
  or handle end clears the segment, since each drags in its own way. A plain
  click off the handles clears the selection first, as for points. The
  segment stays selected through its drags and their undo and redo; like
  the point selection, its index is kept across point inserts and removals
  and dropped only when it is out of range, or when the target changes or
  stops being a segments or floor layout. Delete (`Curve.remove_points`) with a
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
  segments or floor layout), its `handles` (`index`, `points`, world `position` and
  unit world `normal`), `selected` and `gizmo_on_handle`;
  `level_move_segment {scene_name, layout_id, segment, offset}` moves a
  segment by a signed offset along its normal through the same
  `move_curve_segment()`, as one undo entry; it is refused while a curve
  tool drag of the same curve is held; its reply carries `flow` ("Flow
  report"). A script drags a handle with `get_transform_handles` and
  `mouse_drag` as a user does, or holds it with `transform_selection`
  (`end_edit` false) after `level_select_segment_handle`.

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
  the library by display name in library order, as the Level Kit window
  lists them; the tooltip names the shape id and its description. An entry places a piece of
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
  Create entries put a new node there. The viewport's context menu creates
  under its target at the point the click hovered (`doc/editor/windows.md`
  "Viewport context menu"). The new node is selected; each entry
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
  `c_stairs_generator_version`). A generation keeps the child when it has no
  children but surface attached pieces and its
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

## Floor layouts

A floor kit layout (`Repeat_pattern_kind::floor`,
`doc/plans/level_kit_connections.md` C8) is a closed curve node of corner
points with one generated child, "Floor": a slab filling the curve's outline
(`level/floor_polygon.{hpp,cpp}`), built the way a stairs layout builds its
staircase (`Level_kits::build_floor_generation()`,
`make_generated_child_insert()`).

- The outline is the plan (x, z) of the curve's control points; handles are
  not used. The slab runs from the node's origin (y = 0) up to `thickness`,
  its caps triangulated by ear clipping, so a concave outline fills.
- Its values (`Kit_layout_pattern::floor`, `Floor_values`, and two material
  names): `thickness` (0.2 m), `wall_thickness` (0.25 m), `room_height`
  (3 m), `rail_inset` (0.1 m, the riders' line, "Riders"), `floor_material`
  (empty: the floor slot's default) and `floor_wall_material` (empty: the
  wall slot's default). They are rows of the Properties window and the Level
  Kit window, the `floor` object of a pattern over MCP, and saved in the
  pattern text (left out at their defaults; `floor_rail_inset` on its own, so
  a text written before riders keeps its form).
- Refusals: an open curve, a scaled or tilted node, values out of range, a
  floor material name that is neither a scene nor a palette material, and an
  outline `check_floor_outline()` refuses: fewer than three or more than 64
  corners, an edge shorter than 1 cm, two edges crossing, no area. The wall
  material is not checked until something reads it.
- Creating: "Add Polygon Floor" in the Level Kit window, "Polygon Floor" in
  the Create menu's Level Kit submenu, and `level_create_layout` with
  `pattern.kind` `floor` (with `points` and `closed`, or `curve_id`); without
  points the floor starts as a square four modules on a side.
- Edge connectors (C2.4): `get_floor_connectors()` gives edge i (from corner i
  to corner i + 1) the connector `edge_<i>`, type `wall-floor`, at the edge's
  midpoint on the top, inset by half `wall_thickness`, facing up with its up
  axis toward the inside, so a wall joined to it with no twist faces the room
  with its outer face flush with the floor's edge. They are targets of every
  join path ("Connections").
- A floor value edit (`level_set_layout`, the Properties rows) ends with the
  connection follow-up, so a thickness change raises or lowers its walls
  and a wall thickness change moves their inset, in the same entry. A live
  drag of a floor's corner, of an edge handle or of a floor value previews
  it: each step of the layout preview runs the follow-ups the release runs
  (`append_curve_follow_ups()`: the edge renames, the shared corners of
  connected floors, moved from where they stood at the drag's begin, and the
  connection follow-up) and undoes them before the next step (K9.3).
- Rail connectors (C10.2): each edge also has `rail_<i>`, type `rail`, a rail
  host, on the top at the middle of the edge's rail span, facing up with its
  up axis toward the inside. The rail span is the edge's line moved
  `rail_inset` inside, from its crossing with the previous edge's moved line
  to its crossing with the next one's (`get_floor_rail_span()`), so riders on
  neighbouring edges meet where their lines cross at any corner angle,
  reflex corners included. A `rail_inset` edit moves them, and the riders
  follow ("Riders").
- Floor connectors: each edge also has `floor_<i>`, type `floor`, on the
  edge's midpoint at the top, facing out, level. Two floors sharing an edge
  connect there (a drag snap, Auto-connect; a floor layout node keeps
  connection records like a kit piece). Connected floors share corners: an
  outline edit that moves a corner moves the corner of each connected floor
  that stood in the same place (within 1 mm), and that edit's own
  follow-ups carry it on through further connected floors
  (`Level_kits::append_shared_corner_moves()`, given the path before the
  edit by `Curve_set_operation`); a floor join is never followed rigidly.
  Not with the flow mask's `floor` off; an edit adding or removing corners
  moves no shared corner.
- Edge handles (L25): the curve tool gives a floor's edges the segment
  handles of a segments layout ("Segment handles"). A drag of one, or
  `level_move_segment`, moves the edge's two corners along its normal, each
  sliding along its neighbouring edge's line; the walls and the corners
  connected floors share follow live as for a corner drag, and the release is
  one curve edit that moves them the same (the shared corners above).
- An outline edit that keeps the corner count (a corner or edge drag) keeps
  every connection on its edge: the walls follow their edges wherever the
  corners go. An outline edit that adds or removes corners renumbers the edges; the
  same edit moves each connection naming one of the floor's connectors to
  the edge nearest the other end's connector when its recorded edge is gone
  or clearly farther, before the other ends follow
  (`Level_kits::append_floor_edge_renames()`). The three families rename
  apart (`parse_floor_connector_name()`), each one connection per edge:
  `edge_<i>` (walls), `floor_<i>` (other floors) and `rail_<i>` (riders), on
  the records naming the floor and on the floor's own records; a
  connection whose new edge already holds another of its family is dropped,
  with a warning.
- A curve edit that makes an outline the floor refuses - crossing edges, a
  curved span (a smooth or free point), a corner off the node's plane - is
  recorded; the slab stays as it was and the layout's run reports the problem
  (`read_floor_outline()`, `check_floor_outline()`), as a stairs layout does.

## Parameter changes

`Level_kits::set_parameters(node, values)` gives a placed piece new parameter
values as one undo entry. It refuses, changing nothing, a node that is not a
mesh carrying `Kit_piece`, values of another shape and values a parameter
cannot take (the message names the parameter). A float parameter takes any
finite value of its declared range's sign - above zero where the range starts
above zero, zero or more where it starts at zero - and the declared range is
where the UI's drags run (`doc/plans/level_kit_connections.md` L23); an
integer or enum parameter keeps its range. Values equal to the piece's
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
brush, collision shape, body, navigation flag and transform exactly. A
replaced support carries its attached pieces onto the new shape, and a
replaced attached piece rests again on its support ("Surface attachments").

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
- Connections (C6.5, K11.2, `append_replace_connection_drops()`): a
  connection of a replaced piece, recorded on it or on the other end, is kept
  when the new shape has a connector of the same name whose types meet the
  other end's connector (another replaced piece of the call at its new
  values); every other one is removed in the same entry and reported:
  `Kit_replaced_piece::dropped_connections` (the piece's connector, the
  other end, its connector and why: no such connector, or types that no
  longer meet) and `Kit_replace_result::dropped_connection_count`. A
  connection whose other end is not in the scene, or lacks its own connector
  already, is kept as it was. The kept ones join again at the new connectors
  and the new shape's values flow ("Property flow").
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

Each connector accepts a set of connector types (`Kit_connector_types`,
`Kit_connector_type`: `generic`, `wall`, `wall-floor`, `floor`, `rail`;
`doc/plans/level_kit_connections.md` C1, C10); two connectors are compatible
when their sets share a type (`is_compatible()`). Every connector is
`generic` except on the wall run shapes (`architecture.wall`,
`architecture.door_wall`, the window wall and the timber bay;
`type_wall_run_connectors()`) and the riders: a wall's `start` and `end`
accept `wall` and `generic` (a wall still meets a tower's or a pillar's side),
its `base` accepts `wall-floor` only, so a wall stands on nothing but a
floor's wall connector, and its `top` accepts `rail` and `generic` as a rail
host; a rider's `base` accepts `rail` and `generic` ("Riders"). The types and
the rail end (`Kit_connector_types::rail_end`, `rider` or `host`) ride every
local, world and in-root frame of the connector.

- A join the author names (`resolve_connect()`: `connect_piece()`, the Level
  Kit window's Connect rows, `level_connect_pieces`, a placement with
  `connect`) of connectors that share no type is refused: "connector '<a>'
  (<types>) of <shape> and connector '<b>' (<types>) of target '<target>'
  share no connector type". The drag's connector indicators show a target
  connector only when a moved connector in reach is compatible with it.
- Every search for a partner skips an incompatible pair silently, with no
  message or log line: the connector snap (`find_connector_snap()`) and the
  Brush tool's connector join preview (a target connector no connector of the
  new piece accepts is no candidate, and the new piece's connector is chosen
  among the compatible ones).
- Loaded attachments are not tested: a record that joins connectors no longer
  compatible keeps working.
- The types are listed by `level_get_kits` and `level_get_connectors`
  (`types`, an array of names) and shown beside each connector switch in the
  Properties window.

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
  `connect_piece()` below. The Brush tool's kit mode makes such a placement
  from its viewport preview ("Placing in the viewport", connector join).
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
- A join records an attachment by default, so the joined piece follows its
  target ("Attachments"); a join without one (`attach` false over MCP) leaves
  an ordinary piece that a later move of the target leaves where it is.
- A mirrored piece (Groups) has its frames under its new rotation: on the
  mirror image exactly for a piece of a declared symmetry plane, with a pair
  of names across that plane (start and end, left and right, front and back)
  changing sides.

### Standard properties and supply

Standard properties (`level/kit_standard_property.hpp`,
`doc/plans/level_kit_connections.md` C2) name a value with one meaning across
shapes: `room.height`, `floor.thickness`, `wall.thickness`, `child.length`
(lengths in meters) and `floor.material`, `wall.material`.

- A shape maps the ones it takes onto its own parameters or material slots
  (`Kit_shape_definition::property_mappings`, listed by `level_get_kits` as
  `property_mappings`). The four wall run shapes map `room.height` to
  `height`, `child.length` to `length`, `wall.material` to slot `wall` and,
  but for the timber bay (no thickness parameter), `wall.thickness` to
  `thickness` (`set_wall_run_flow()`). The riders map `child.length` to
  `length`, the battlement `wall.thickness` to `thickness` too ("Riders").
- A shape states what a piece supplies over each connector
  (`Kit_shape_definition::get_supply`, into a fixed-size `Kit_supply_list`):
  a wall run shape over `start` and `end` supplies its thickness, its height
  and its `wall` slot's material at priority 1, and over `top` its length
  (`child.length`) and its thickness at priority 1.
- A floor layout supplies over each edge connector
  (`get_floor_supply()`) at priority 2: the edge's length minus its
  `wall_thickness` (so walls on neighbouring edges meet at their center
  lines; none when that is under 0.1 m, the walls' least length), the wall
  thickness, the room height and its wall material (a resolved name; the
  caller gives the wall slot's default for an empty `floor_wall_material`).
  Over each rail connector (`get_floor_rail_supply()`) it supplies at
  priority 2 the length of the edge's rail span (none under 0.1 m).
  `get_floor_connector_supply()` picks the supply by the connector's name.
- The property flow reads them ("Property flow").

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

### Connections

A connection (`doc/plans/level_kit_connections.md` C3.1, K6) is a join a
piece remembers on one of its connectors. A piece keeps one connection per
connector, any number in all, in its `Kit_piece.attachment` record: one
object when it has one connection (the form every older scene has), an array
sorted by connector when it has more (`make_attachments_text()`,
`parse_attachments_text()`, `read_kit_attachments()`). Each entry is an
attachment as described below, followed as one ("Attachments").

- Ends: a kit piece or a floor layout node (`is_floor_layout()`,
  `get_floor_layout_connectors()`): a floor's edge connectors are targets of
  `level_connect_pieces`, connected placements, the connector snap and the
  Brush tool's connector join (hovering the floor's slab targets its layout
  node), `level_get_connectors` lists them, and the connector indicators
  draw them (cached per floor, read again when its curve's revision or its
  slab child changes, and on a write of its layout value). The Brush tool
  asks the same cache whether the hovered slab's parent is a floor
  (`Kit_connector_indicators::is_floor_layout()`), so a hover parses no
  layout value in the steady state, and a joined preview stays on the floor
  at its edge's new frame when the floor's outline or values change with the
  pointer still (`refresh_kit_join()` on the curve and layout messages).
- One connection per connector, counting both ends
  (`Level_kits::check_one_connection_per_connector()`,
  `find_connection_end()`): an explicit join or a connected placement onto a
  target connector that is connected to anything but this same pair - another
  piece, or another connector of the joining piece - is refused, naming the
  piece holding it; so is a join of a connector of the piece that a record
  of another piece names, other than this same pair; so are two placements
  of one `place()` call (one `level_place_pieces` call) that connect with
  `attach` to the same target connector, naming both indices, and nothing is
  placed; a snapped release records nothing there. A join on the piece's own
  connector replaces the piece's record of that connector and keeps the
  others; a join of a pair the target records moves the record to the piece,
  so the connection is kept once. A record whose target is not in the scene
  holds its connector no longer: the next connection made there (a join,
  Auto-connect) replaces it.
- Where a connection is recorded (K6.1): on the piece that was connected (the
  joined piece, the snapped piece, the changed piece of Auto-connect), but a
  `wall-floor` connection always on its wall and a `rail` connection always
  on its rider (C10.7): a floor snapped or auto-connected onto a wall, or a
  host joined, placed, snapped or auto-connected onto a rider, writes the
  record on the wall or the rider with the ends swapped
  (`is_host_end_of_join()`, `Level_kits::make_join_connection()`,
  `make_reversed_attachment()`; the twist and the gap read the same from
  both ends). So the floor fits, the wall's return to its edge and the
  rider's return to its rail behave the same whichever piece was moved to
  make the join, and the wall or the rider takes the host's values. A raw record written with the floor as the owner of a
  `wall-floor` join is followed in the stored direction (the floor follows
  the wall); the editor writes such a join on the wall.
- A record list that does not read (a raw write: two entries of one
  connector, a malformed entry) gives the node no connection; its write and
  the load of a scene holding it log one error of the `editor.scene` log
  naming the node.
- A move that takes a piece off one of its joins removes that connection only
  (`make_connection_removal_operation()`). A join no end follows - two `wall`
  or two `floor` connectors, or a type the flow mask switched off - is judged
  the same whichever of its ends the change moved and whichever end records
  it: the change that takes its connectors apart removes it (C3.9, C3.14).
- Joined: one rule per connection kind (`Level_kits::is_connection_joined()`,
  used by the following, the apart report and `level_get_connections`): a
  join of two `wall` or two `floor` connectors is joined while the two
  connectors share a place (within 1 mm; their directions may differ, a room
  corner), any other while the piece's pose is the join's, twist and gap
  included.
- Connected groups (C6.2, `Level_kits::get_connected_groups()`, MCP
  `level_get_connected_groups`): the components of the graph of the records
  with both ends in the scene, dormant ones included, in scene order; with a
  node, the group holding it.
- The Properties window's "Kit Piece" group, its "Kit Layout" group of a
  floor layout, and the Level Kit window's Selection section list the node's
  connections, its own records and the records naming it, one "Connection
  <connector>" row each with Remove: the other end, its connector, the twist
  and gap, and "not followed" with the reason for a dormant entry
  (`Kit_connection_rows`, `level/kit_parameter_rows.hpp`;
  `get_attachment_statuses()`, one `Kit_attachment_status` per entry); the
  Properties groups end with a Detach row. They are read again on an
  attachment write, a removed or inserted node, a renamed piece, a selection
  change and a floor's layout write.
- Status: `Level_kits::get_attachment_statuses()` gives every entry of a
  piece's record its state (`active` or `dormant` with the reason,
  "Attachments"); `level_get_pieces` reports them as `attachments` and the
  first as `attachment`.
- Replace keeps a connection whose connector the new shape has with a type
  that meets the other end's and drops the others ("Replace").
- Save and load: the entries ride the node's `ERHE_node` properties and name
  their ends by glTF uid, so every connection, its status and the joined
  frames come back after a save and a load.
- Detach (`Level_kits::detach_pieces()`, MCP `level_detach_pieces`; the
  Properties window's Detach row, the Level Kit window's Detach Selected,
  the Scene hierarchy context menu's Detach Connections for the clicked node
  or the selection, and `level_detach_piece` for one piece) removes
  every connection with an end in the pieces, the records other pieces keep
  naming them included, as one undo entry, with no other change.
- `level_get_connections` lists a node's connections, its own records and
  the records naming it, each with its other end and whether it is joined;
  `level_remove_connection` removes the one at a connector, whichever end
  records it.

### Property flow

When a connection is made, and when a connected piece changes, values flow
over the connections (`Level_kits::build_connection_flow()`,
`doc/plans/level_kit_connections.md` C3, C4):

- It runs in the operation that ends every kit change
  (`Kit_attachment_follow_operation`), after the floor fits and the wall
  fits, so it is part of the change's undo entry and the following then
  joins the pieces at their new connectors.
- Who takes values: the piece of a new connection (an explicit join with an
  attachment, a connected placement, a snapped release), and the neighbours
  of the pieces the change edited (a parameter edit, a material edit, a
  replace, a floor outline edit), on through the connected pieces in rounds
  (at most the Connection Step Limit setting, 64 by default). A piece the
  author edited keeps its values, also in the flow after Auto-connect; a
  floor takes none; a child of a locked kit layout node takes none.
- What it takes: for each standard property its shape maps, the value of the
  highest priority among the supplies of all its connections ("Standard
  properties and supply"); on equal priority the neighbour that changed in
  this change wins, then a new connection, then the piece's connector earlier
  in its shape's order (`choose_supply()`, `level/kit_connection_flow.hpp`,
  scene free and unit tested). A length is taken as given (L23), clamped to the
  parameter's declared range only where the shape refuses it (a combination
  check), and its parameter stops deriving from the seed; a material is resolved by name (a
  scene material, else a palette material).
- So a wall placed on a floor's edge takes the edge's length minus the
  floor's wall thickness, the floor's wall thickness, room height and wall
  material; a wall joined to a wall's end takes its thickness, height and
  material; an edit of a wall's thickness spreads along the walls joined end
  to end.
- Older scenes whose walls are attached end to end change on edit too: a
  parameter edit, a replace or a snapped drag of one such wall now gives its
  joined walls (or itself, for the snapped wall) the thickness, height and
  wall material over the joins, in the same undo entry.
- A snapped release takes part only where it records its connection.
- Floor fit (C3.15, K5.3, `Level_kits::build_floor_fits()`, run first in
  the same operation): a wall the change moved (the wall or a node above it,
  `Level_kits::is_moved()`, the rule of the wall fits and of the return to
  the edge too) that stands on a floor's edge, the floor not moved with it,
  gives that edge its base line in plan, half a wall thickness toward the
  floor's outside (the side the outline's winding gives, whichever way the
  wall faces). All the changed edges of one floor are solved together: a
  corner between two changed edges goes to their lines' crossing, a corner
  between a changed and an unchanged edge slides along the unchanged edge's
  line (straight along the new line's normal where the two are within 15
  degrees of parallel). Two changed edges within 15 degrees of parallel meet
  at their lines' crossing when it lies within twice the farther line's
  distance from the old corner, keep the corner on their line when they are
  one line, and otherwise refuse the change naming both walls (one corner
  cannot meet both lines). It is one curve edit of the floor, whose follow-ups
  regenerate the slab and make the other walls follow and take their new
  lengths. A turned wall turns its edge's line with it; a wall turned half a
  turn or joined with a twist slid along its run changes no line. The
  operation brings the scene's world matrices up to date before the fits (a
  move writes the moved node's world matrix alone), so the fits, the return
  to the edge and the detach pass read the poses of nodes below the moved
  ones (a wall in a moved group) as moved. A fit that would make
  an invalid outline refuses the whole change (K0.3) on every route that
  moves the wall (a Transform tool release, `set_node_transform`, a
  Properties transform row, `set_item_property` of a transform value, a
  pasted transform, `level_connect_pieces`, snap to surface, a group
  mirror): the follow operation refuses its first execute, every host above
  it takes its own write back and is refused with it
  (`Operation::set_refused()`, `doc/editor/operations.md`), nothing is
  recorded and the redo history stays. The flow report states the refusal
  only. The `Level_kits` entry points return it as their error, the MCP
  tools that record at once (`set_node_transform`, the join, edit and curve
  tools, `level_snap_to_surface`) reply an error naming it, and a queued
  change (`set_item_property`, `transform_selection`, the UI) is logged as a
  warning and read with `level_get_flow_report`; a Transform tool release so
  refused leaves the pieces where the drag started.
- The moved wall goes back onto its edge (K5.5, L20): when the change is
  recorded, a wall the author moved that stands on a floor edge (its
  `wall-floor` join let through the flow mask) joins that edge again after
  the floor fitted it, at the edge's midpoint and upright; so a slide along
  its run, a lift and a tilt, which change no line, put it back where it
  was. A live drag shows it there at every step ("Flow preview").
- Wall fit (C3.16, L22, `Level_kits::build_wall_fits()`, run next): a wall
  joined by a `wall` connector to a moved wall, standing on no floor, keeps
  its far end where it is; its near end goes onto the moved wall's
  connector, so it takes the new length and turns about its up axis,
  upright at its far end's height (a tilt is dropped); a wall whose partners
  at both ends moved spans the two; a scaled wall is not fitted, nor one whose
  partners' connectors still sit on its ends (a value edit that moved no
  end). Its length
  parameter is the one its `child.length` maps. The following of the same
  change leaves that join alone and moves the pieces attached to the fitted
  wall. Joins of generic connectors (an arch's ends) follow
  rigidly as attachments do.

### Riders

A rider (`doc/plans/level_kit_connections.md` L26, C10; design K13) is a
piece that runs along X with a `length` and stands on a host's edge, taking
the host's length: `architecture.railing`, `architecture.battlement` and
`architecture.entablature`.

- Connector type `rail`: a rider's `base` (at its origin, facing down, up
  +Z; `type_rider_base_connector()`) accepts `rail` and `generic`; the hosts'
  rail connectors are a wall run shape's `top` (`rail` and `generic`) and a
  floor layout's `rail_<i>` (`rail`), marked `Kit_rail_end::host`. A rider
  joined with no twist runs along the host's X: a wall's run, a floor edge,
  its front toward the floor's inside.
- `rail` joins a host and a rider only (`get_join_types()`, the test behind
  `is_compatible()`): two hosts (a wall's top and a floor's rail) share no
  type over it. A host and a rider join at twist 0 or 2 only
  (`is_join_twist_allowed()`), so the rider runs along the host: the
  connector snap's orientation, the Brush tool's join preview (the half turn
  nearest its yaw), Auto-connect and the snapped release's record choose
  among those two, and an explicit join at a quarter twist is refused.
- Values: the rider maps `child.length` to `length` (the battlement
  `wall.thickness` to `thickness` too). A wall's top supplies its length
  and thickness at priority 1; a floor's `rail_<i>` the length of the edge's
  rail span at priority 2 ("Floor layouts"). So a railing on a floor edge
  takes the edge less the two corners' inset, and one on a wall's top the
  wall's length; a floor corner edit that changes a wall's length changes
  the rider on its top in the same entry and the same live preview, the
  flow's rounds carrying the wall's new supply on to the rider (C10.6).
- Spacing (C10.4): a rider that repeats along its run keeps the repeat's
  length, so a new length changes the count: the railing's bays
  (`bay_length`, `get_kit_repeat_count()`), the battlement's merlons (from
  `merlon_width` and `crenel_width`), the entablature's triglyphs.
- Response (C10.5): a rider follows its host rigidly over the join (the
  record is on the rider, so it is the follower). A rider the author moved
  alone goes back onto its rail at the end of the change, at the join's pose
  (the K5.5 rule of a wall on a floor, `follow_attachments()`), and a live
  drag shows it there; it does so with `rail` let through the flow mask. A
  host never responds to its rider: no fit, no follow, no value. Detach
  frees a rider, which then moves alone.
- Record (C10.7): a `rail` connection is always recorded on its rider
  ("Connections"), whichever piece was moved to make it: a wall's top joined
  onto a free railing's base with `level_connect_pieces` moves the wall and
  writes the record on the railing, which takes the wall's length.
- Every join path takes `rail` as it takes the other types: the explicit
  join, connected placements, the Brush tool's join and its preview values,
  connector snap, Auto-connect (a floor edit that leaves a free rider's base
  on a rail connects it, recorded on the rider), Replace, the flow mask
  (`rail` switch) and the connector indicators (`connector_rail_color`).

### Flow report

Every recorded change that runs the flow fills a report (C3.13, design K7.6;
`Kit_flow_report`, `level/kit_connection_flow.hpp`), cold and allocating,
through `Level_kits::get_flow_report()`. It is filled only during the first
execute of a new undo entry (`Operation_stack::is_recording()`), never by a
preview, an undo or a redo; the first fill of a new entry
(`get_recording_serial()`) clears the previous report.

- `taken`: per piece and property the value it took, its supplier, the
  supplier's connector, the priority and the tie rule that decided among
  equal priorities (`none`, `changed_in_change`, `new_connection`,
  `connector_order`: the rule that beat the closest equal candidate).
- `not_taken`: values supplied and not taken, with the supplier, priority and
  reason: `higher_priority`, `tie_changed`, `tie_new_connection`,
  `tie_connector_order`, `no_mapping` (the shape maps nothing to the
  property), `locked_layout_child`, `author_edit` (the author edited the
  piece in this change), `floor` (a floor takes no values) and
  `shape_refuses`. One entry per piece, property and supplier: the latest
  round's.
- `responses`: `follow` (a rigid re-join, from its target), `floor_fit` (the
  floor, from the moved wall) and `wall_fit` (the wall, from its moved
  partner).
- `new_connections` (explicit joins, snapped releases, Auto-connect),
  `removed_connections` (moves that took a piece off its join) and
  `apart_connections` (kept with their connectors apart: a cycle that does
  not close, a join whose flow is masked off).
- `refusal`, `limit_reached`, `rounds` (the most rounds of the change's
  value flows) and `pending` (the pieces still changing at the step limit,
  the author's edited pieces left out).

The MCP replies of `level_connect_pieces`, `level_place_pieces`,
`level_set_piece_parameters`, `level_set_piece_material`,
`level_replace_pieces`, `level_set_layout`, `level_move_segment`,
`set_curve`, `curve_add_point`, `curve_remove_points` and
`curve_set_point_kind` carry it as `flow` (null when their entry ran no flow).
A change recorded later by its tool (`set_node_transform`,
`transform_selection`, a viewport drag) is read with
`level_get_flow_report {}` (`flow`, `recording_serial` and `latest`). The
Level Kit window shows the one-line summary of the last report under Detach
Selected (`Level_kits::get_flow_summary()`, made at the end of each follow
operation's first execute and cleared by an undo, a redo, the recording of
the next entry and a closed scene). One MCP call that records several
entries replies the report of the last of them that ran a flow.

### Flow preview

A Transform tool drag (pointer or scripted: `drag_selection`,
`transform_selection`) and a Properties transform row drag of kit pieces or
floors show the flow their release records, live (`doc/plans/level_kit_connections.md`
C3.10, design K9; `Level_kits::step_layout_preview()`, `run_flow_preview()`):

- A move of nodes with kit pieces or floors at or below them has a flow
  preview; a move of other nodes has none.
- Each step of the move preview runs the follow operation the release
  appends (`make_attachment_follow_operation()` with the moved nodes and the
  step's connector snap pair, the floor fits, the wall fits, the value flow,
  the following, the K5.5 return and Auto-connect) on the step's poses, after
  the moved layouts' generations, and keeps it applied; the next step undoes
  it first (the moved nodes keep the poses the drag gave them) and runs again.
  A moved node the run moved (a K5.5 return) that still stands at the run's
  output, bit for bit, was not written by the drag since: its drag pose is
  the run's input; any other moved node's drag pose is its pose. The moved
  nodes may include a node and its descendant (both selected), so the rule
  reads poses, not write serials (a transform pass gives a descendant its
  parent's serial). A
  Properties row writes one component of a moved node, so the row first takes
  the run back (`Property_edit_hook::before_write`,
  `prepare_layout_transform_write()`) and writes over the drag's own pose.
  So a free wall snapped onto a floor's edge connector shows the edge's
  length, thickness, height and material; a connected wall dragged shows the
  floor fitting and the other walls following and taking their lengths; a
  wall slid along its edge stays on the edge; a rider dragged off its rail
  stays on the rail, and a floor corner drag shows the riders on the walls'
  tops taking the walls' new lengths ("Riders").
- Unsnapped, or snapped to another pair, the step runs without the pair: the
  values the snap gave come back (L10).
- A step whose flow is refused (an outline the floor refuses, K0.3) shows the
  last valid run again, the moved nodes where the drag has them, and reports
  the refusal; its release is refused as any refused move is ("Property
  flow").
- The release ends the preview (the run undone, the moved nodes at the
  drag's last poses) and records the move; the follow operation's first
  execute recomputes from the committed state the same inputs, so the entry
  equals the last step.
- No undo entry and no flow report: the preview executes its operations
  outside the stack, and while a preview runs them or takes them back
  `get_flow_report()` gives none and the flow's log lines drop to trace.
  While the Transform tool's own edit's flow preview runs or is taken back
  (`Level_kits::is_move_flow_previewing()`), the tool keeps its drag start
  when the run moves a dragged node (`Transform_tool::on_node_touched()`);
  other touches, the runs of a Properties row drag among them, move the
  gizmo anchor as any edit does.
- Snap targets: the pieces the drag's flow moves are no connector snap
  targets ("Connector snap").
- A cancel (Escape, `cancel_transform_drag`) ends the preview before the
  moved nodes go back, so every flowed value, fit and follower is as before
  the drag and nothing is recorded; a Properties row edit the window gives up
  ends it the same way. A tool switch leaves a scripted drag and its preview
  held. A scene closed mid-drag drops the preview without touching the
  scene; the preview holds no node of it afterwards.
- Cost: a moving step builds a new follow operation and runs the release's
  whole flow (gathering the scene's connection records, the fits, the value
  flow with brush and collision shape swaps, the following and Auto-connect),
  allocating as a recorded change does: about 5 ms for a wall of a four-wall
  room in the headless Debug build (the `editor.scene` trace line "flow
  preview step: N moved nodes, snapped S: T ms"). Only held steps are free of
  allocations (K9.4): a step whose moved nodes stand where the run shown
  started (per matrix element within 1e-6 of the element's magnitude, at
  least 1e-6) with the same snap pair runs nothing, and the moved nodes take
  the run's poses again. Nothing runs between steps.
- `level_get_flow_preview {}` reports `active`, the counters `steps`, `runs`,
  `held_steps` and `refused_steps`, `refusal` (of the last step) and `snap`
  (the pair of the run shown, by id and name); they stay readable after the
  drag.

A live parameter drag (`preview_parameters()`) still only joins the attached
pieces again ("Attachments"); it previews no value flow.

### Auto-connect and the connection settings

- Auto-connect (C5, `Level_kits::build_auto_connections()`, last in the
  operation that ends every kit change): a free enabled connector of a piece
  the change moved, edited, placed connected, snapped or flowed into, that
  sits within 0.1 mm of a free enabled connector of another kit piece or
  floor layout sharing a typed connector type with it (`wall`,
  `wall-floor`, `floor`, `rail`; generic connectors never auto-connect, so
  touching tiles do not chain into rigid groups), is connected, the record on
  the changed piece (a `wall-floor` connection on its wall, a `rail`
  connection on its rider, "Connections"). The two connectors must face each other (outward axes opposed),
  except two `wall` connectors, which meet at a room corner at an angle but
  not pointing the same way (a duplicate in place); up axes are not
  compared (L7). A piece joined with `attach` false in the change is left
  alone: the follow operations made while `connect_piece()` builds that join
  keep the piece (`Kit_attachment_follow_operation`), and nothing is left set
  for a later change. Auto-connect runs once per change, whether the scene had
  connections or not; the new connections' pieces then take their values
  (one more flow). So the walls placed on a floor's edges at the lengths it
  supplies connect at the corners. Each pass gathers the scene's connector
  frames and looks up each candidate's connections by scanning the records,
  a cost that grows with the scene.
- A join of two `wall` connectors is never followed rigidly: the floor and
  wall fits move it (a wall fits a partner that moved or changed), and it is
  removed only when its two connectors come apart (more than 1 mm). A
  connection of flow-masked types is not followed; a move of either end that
  takes its connectors apart removes it.
- One call's overrides (C6.2): `level_connect_pieces` and
  `level_place_pieces` take `flow_mask` and `auto_connect`, which every flow
  of the call's entry reads instead of the settings
  (`Level_kits::set_flow_overrides()`, `get_effective_flow_mask()`,
  `get_effective_auto_connect()`); the settings stay as they were. The
  follow operations made during the call carry the overrides and apply them
  around their first execute, so an entry a held stack defers (a scene save)
  still flows with them.
- Settings (`Surface_snap_config` v9, the connection rows of the surface
  settings, MCP `set_surface_snap_settings`): Auto-connect (on by default),
  the flow mask (`flow_mask`, one switch per connector type, all on: values
  and poses flow over a connection only where a type both connectors accept
  is on; with `wall-floor` off no floor fits, with `wall` off no wall fits,
  with `rail` off no rider takes a host's length or returns to its rail)
  and the Connection Step Limit (the most rounds of a change's flow, 64 by
  default). `Level_kits::set_connector_auto_connect()` takes a
  `Connector_auto_connect`.

### Attachments

An attachment (R5.3, plan step 41) is a join a piece remembers: the piece's
connector stays on a connector of its target piece through later moves and
parameter changes of the target (`level/level_kits_attachment.cpp`).

- Record: `Kit_piece.attachment`, a canonical JSON text (`Kit_attachment`,
  `make_attachment_text()`, `parse_attachment_text()` in `kit_piece.hpp`):
  `owner` (the glTF uid of the attached piece), `target` (the target's glTF
  uid), `connector`, `target_connector`, `twist_steps` (0 - 3; a join's twist
  is stored modulo 4) and `gap` (a hexadecimal float). It rides the node's
  `ERHE_node` properties like the rest of the record, and the uids are what a
  save writes and a load reads back (`doc/editor/scene_serialization.md`
  "Unique IDs"), so an attachment survives a save and a load. No attachment
  is no local value, and only the local value counts (`read_kit_attachment()`:
  a style or an inherited value supplies none).
- Copies: a record is read only on the node its `owner` names, so a copy on
  another node - a duplicate, a clone, a node of a prefab instance (its uid
  derived from the instance's) - is inert and follows nothing. Paste
  Properties does not paste `Kit_piece.attachment`. A uid that names more than
  one node of the scene (an import of the same file twice keeps the file's
  uids) makes the record of each such owner inert and a record naming it as
  target dormant ("ambiguous target").
- What attaches: every explicit join - `connect_piece()` (the Level Kit
  window's Connect rows, `level_connect_pieces`), a placement with `connect`
  (`level_place_pieces`, the Brush tool's kit-mode connector join) - unless
  `Kit_connect::attach` is `none` (`attach` false over MCP); and a Transform
  tool drag (pointer or scripted, `transform_selection` with a connector
  snap distance) released connector-snapped when, at the poses the release
  records (the followers joined), the moved piece sits on a join of the
  snapped pair (`Kit_snap_pair`, `find_snap_attachment()`: a quarter twist, no
  gap, within 0.1 mm and 1e-4 rad). A translation-only snap that leaves the
  frames unaligned attaches nothing. Connecting a piece that is on the join
  already records only the attachment, one entry.
- What does not: a child of a locked kit layout node, on either side - its
  layout owns its pose and regenerates its children. Such a join is made
  without an attachment and the reply's `not_attached` says why
  (`check_attach()`, `Kit_attach_refusal`). Cycles are allowed (C3.2): a
  join whose target follows the piece, directly or through a chain, is made;
  closed in place (four blocks round a square) the cycle moves rigidly, and
  where the join moves the piece the followers move with it and the new
  connection is kept apart (C3.7, "Flow report").
- Following: every change that moves a node or changes a piece's values ends
  its undo entry with a `Kit_attachment_follow_operation`, whose first execute
  calls `build_attachment_follow_ups()` with the change's moved nodes (moved by
  the user) and changed pieces at their new state. The followers are the
  attached pieces not at or below a moved node whose target is at or below a
  moved or changed node or another follower, and a changed attached piece
  (its own connector moved). They join again (the same pair, twist and gap,
  the piece's own scale kept) in dependency order: a follower waits while a
  pending follower is its target, above its target or above the piece itself.
  A cycle in which every pending follower waits for another is ordered by
  `Kit_follow_order` (`level/kit_connection_flow.hpp`, scene free): it gives
  up first an entry of a piece joined already in this change, then one of a
  piece the change edited (the change's origin keeps its pose), and else
  joins the cycle's first entry; a given-up entry is kept, apart when its
  connectors no longer meet. The moves are `Node_transform_operation`s
  inside that operation, so the change, the followers and the detaches are
  one undo entry and undo and redo restore all of them. The changes that end
  so: the move hook `append_layout_move_operations()` (a Transform tool drag
  or value edit, a Properties transform row, a paste of transform values,
  `set_node_transform`, `connect_piece()`), `append_attachment_follow_ups()`
  after unapplied moves (the Operations window's Align, Add Joint's
  alignment, Reset, Bake and Center Transform; the Brush tool's placement
  corner turn, which turns at once instead of in 0.25 s in a scene with
  attachments), a group mirror, `snap_to_surface()` of placed pieces,
  `set_parameters()`, a raw or pasted write of `Kit_piece.parameters` or
  `Kit_piece.shape` (`append_kit_piece_record_follow_ups()`; a name the new
  shape lacks leaves the attachment dormant) and `replace_shape()` (a
  replaced piece's connector of the same name; a connection Replace cannot
  keep is removed, "Replace"). A scene without attachment
  values adds no operation.
- The user's own moves: a piece at or below a moved node is never joined
  again, live or at the release, also when its target moved in the same
  change. Moved rigidly with its target (both selected) it stays on its join
  and attached; moved off its join it detaches, unless the release snapped it
  onto a join, which attaches it there.
- Live drags: a Transform tool drag and a Properties transform row drag run
  the release's follow operation at each step ("Flow preview"), so the
  followers join again as the release joins them. A live parameter drag (`preview_parameters()`)
  gathers them at its first step and at each step reads only the dragged
  piece's connectors from the step's values (its switches read once). The end
  of the preview, a cancel, an undo or redo during it and the release give the
  followers their poses from before the drag back; the release's entry then
  records them once.
- Detaching: in the same entry, with an `editor.scene` info line, the change
  removes the attachment of a piece it moved off its join without joining it
  - moved by the user, or riding below a follower (a child of a follower
  attached elsewhere) - unless its target followed this piece in the same
  change, directly or through a chain of followers (a cycle closing on the
  moved piece), which keeps it (C3.14); a target that followed another
  piece does not keep it. A dormant piece moved together with its target keeps
  its record; moved without it, it loses the record. `detach_piece()` (the
  Properties window's Detach, `level_detach_piece`) removes it as one entry,
  with the piece's surface attachment ("Surface attachments").
- Dormant: the record stays while the target is not in the piece's scene (it
  was deleted; an undo of the delete brings it back), the target uid is
  ambiguous, either connector is missing from the shape or disabled
  (switches, a layout's hiding), or a side is a child of a locked kit layout
  node; a dormant attachment is not followed, and the piece stays where it
  is. `get_attachment_status()` reports one entry `active` or `dormant` with
  the reason, `get_attachment_statuses()` every entry of a piece.
- Not followed (the attached piece stays until the target's next followed
  change): a kit layout regeneration. The followers' locked surface-snapped
  kit layouts below them are not laid out again.
- Viewing: the connection rows ("Connections"); `level_get_pieces` reports
  `uid` and per entry (`attachments`) `state`, `reason`, `target_uid`,
  `target_id`, `target_name`, `connector`, `target_connector`, `twist_steps`
  and `gap`.

### Surface attachments

A surface attachment (`doc/plans/level_kit_surface_attachment.md`, GitHub
issue 304) is the place of a kit piece A on the piece B it rests on, its
support: A is a child of B in the scene graph and carries the record of
where on B it rests. When B moves, A rides the scene graph; when B's shape
changes, A is carried to the same place on the new shape. The code is in
`level/level_kits_surface_attachment.cpp`, the scene-free parts in
`level/kit_surface_attachment.hpp` and `level/kit_piece.hpp`.

- Record: `Kit_piece.surface_attachment` on A, a canonical JSON text with
  sorted keys (`Kit_surface_attachment`, `make_surface_attachment_text()`,
  `parse_surface_attachment_text()`, `read_kit_surface_attachment()`), every
  key required:
  - `target`: B's glTF uid;
  - `position`: A's rest point as a proportion of B's node-space bounds, 0
    at their minimum and 1 at their maximum on each axis, 0 on an axis
    without extent (three hexadecimal doubles);
  - `direction`: the unit search direction in B's node space (three
    hexadecimal floats);
  - `orient` (true or false) and `rest` (`leading_face` or `anchor`): the
    snap that rests A;
  - `on_surface`: false after a carry that found no face of B under A.

  The rest point is the point of A that lies on the surface, in A's node
  space (`get_surface_rest_point()`): the center of the bounds face the
  snap rests ("Surface placement": the face leading along the direction, or
  the -Y face with orient) or A's origin with the `anchor` rest. Only the
  local value counts, and no record is no local value. A record that does
  not read is named by an `editor.scene` error at its write and at a scene
  load. Paste Properties does not paste it. The record and the parent link
  are saved and loaded with the scene, so a loaded A is attached to the
  loaded B.
- Status (`Level_kits::get_surface_attachment_status()`): `none` without a
  record; `active` while A's parent is a mesh node whose uid is the
  record's `target` (`is_surface_attached_to_parent()`) and `on_surface` is
  true; `off_surface` while it is false, attached and followed as active
  (`is_surface_attached()`); `inert` otherwise - another parent, no parent,
  a parent that is no mesh node, a record that does not read - with the
  reason. The status's target is the parent when attached, else the scene's
  one node of the uid. The first change that judges an inert record removes
  it; the piece stays where it is.
- Support (`Level_kits::find_surface_support()`): the mesh owning the face
  A's rest was found on (`Surface_placement::support_mesh`) when it is a kit
  piece or a kit layout's generated mesh (a floor's slab, a staircase), a
  child of a locked kit layout node included. Content that is no kit piece
  is no support. Home (`Level_kits::get_surface_home()`): A's first ancestor
  that is neither a kit piece nor a kit layout node - a group node, another
  plain node or the scene root; a detach moves A, with its subtree, there,
  keeping its world transform.
- Attaching, with Attach To Surface on ("Surface settings": the setting is
  off by default and implies the snap; `Surface_options::attach`; the MCP
  tools' own `attach` argument is true by default and implies nothing):
  - A placement (`place()`: `level_place_pieces`, the Level Kit window, the
    Brush tool's kit mode) with surface options whose rest lies on a support
    is inserted under the support, its record written on the new node,
    unless the placement names a parent or joins a connector. The Brush
    tool's insert snaps again from the hover's rested pose, which finds its
    support itself. Place As Layout puts the layout node under the scene
    root.
  - A snap of an existing piece - `Level_kits::snap_to_surface()` (the Level
    Kit window's snap buttons, `level_snap_to_surface`), a Transform tool
    release whose last surface step rested the piece (gizmo drags,
    `transform_selection`, `drag_selection`), Replace's re-snap - hands the
    change's follow operation (`Kit_attachment_follow_operation`) a
    `Kit_surface_snap` per snapped node. The operation's surface pass finds
    the rest the node holds now (0.1 mm either way along the snap's
    direction, the change's moved and snapped subtrees excluded) and moves
    the node under that rest's support with an
    `Item_parent_change_operation`, which keeps its world transform, and the
    record write. A snap that rests on no support detaches an attached
    piece. A snap call that holds a support and a piece attached at or
    below it does not snap the piece: it rides its support.
  - Nothing attaches a piece holding a connector connection (a record of its
    own or another piece's record naming it, `has_connection()`), a kit
    layout's element (a child of a kit layout node), a node that is no mesh,
    or with Attach To Surface off. A piece that takes part in a new
    connection of a change is detached to its home in the same entry:
    connectors come first.
- Following: the surface pass of a change's follow operation runs after the
  connector follows, the value flow and auto-connect have settled, first
  carrying, then judging the moved pieces.
  - Carry (`build_surface_carry_follow_ups()`): the change's changed pieces -
    `set_parameters()`, raw and pasted writes of `Kit_piece.parameters` and
    `Kit_piece.shape`, Replace, the pieces the value flow wrote into, the
    walls a floor or wall fit changed - are walked parents before children.
    Each attached direct child of a changed piece, and each changed
    attached piece itself, is carried (`compute_surface_carry()`):
    `carry_surface_attachment()` puts the rest point at the record's
    proportion of the support's present bounds, A's local rotation and scale
    kept (a carried translation within 1e-6 m, relative, of the present one
    is the present one, so an unchanged support moves nothing; at unchanged
    bounds the carry computes in double and gives the translation back).
    Then the snap of "Surface placement" runs from the carried pose along
    the record's direction, with its orient and rest, against the support's
    mesh alone (the `only` filter), the surface settings' max distance
    either way. Found, A takes the rest and, when the rest moved it, its
    place is written again; not found, A keeps the carried pose and the
    record its position with `on_surface` false, and a later carry that
    finds the face sets it true. Each carry's operations run as they are
    appended, so A's own attached pieces rest on A's new pose; the walk goes
    below a carried piece only where that piece is itself a changed piece.
    A moved support's pieces ride the scene graph and no record is read for
    them; a piece mirrored with its support (`level_mirror_group`) has its
    place written again from the mirrored poses.
  - The author's moves: every moved node with a record whose support was not
    moved in the same change is judged. Still at rest on a face of its
    support within 0.1 mm (the record's direction, orient and rest, the
    support's mesh alone), it keeps the attachment and its place is written
    again; otherwise it is detached to its home with the `editor.scene` info
    line `Kit piece '<A>' detached from '<B>': <why>`.
  - Every attach, carry and detach is part of the change's one undo entry;
    undo gives back the parent, the local transform and the record. A parent
    change announces the child as touched, so the Transform tool reads its
    baselines again.
- Locked and generated layouts: `find_locking_kit_layout()` stops at an
  attached piece, so A on a locked layout's child is the author's piece
  ("Kit layout nodes"). A regeneration keeps A on a kept child, moves it to
  the new child of a replaced element (its local transform kept, its record
  naming the new child, then carried on the new shape: a crate on a floor
  follows the outline proportionally and reads `off_surface` over a notch),
  and detaches it to its home when its element goes.
- Live: a flow preview step (a Transform tool drag, a layout's curve point,
  floor corner or edge drag, a layout value drag) runs the follow operation
  with the carry moving the pieces without writing records and without the
  judgement, undone with the step; a dragged piece attaches or detaches at
  the release only. A parameter drag (`preview_parameters()`) rests the
  dragged attached piece on its support and carries its attached children
  at every step from their poses before the drag
  (`step_surface_carry_preview()`; the step's mesh is in the raytrace
  scene), restored with the connector followers at the release, a cancel
  and an undo; the release's entry carries them once.
- Detach: `detach_pieces()` (the Properties window's Detach,
  `level_detach_piece`, `level_detach_pieces`) removes the surface
  attachment with the connections, moving the piece to its home. A parent
  change made by the author - an item tree drag, Attach (operations window),
  `reparent_item`, `level_group_nodes` - under any node but the support the
  record names removes the record in the same entry
  (`make_reparent_surface_follow_up()`). Deleting a support deletes its
  attached pieces with its subtree; undo brings all back attached.
- Copies (`remap_copied_surface_attachments()`): a copy of a subtree - the
  group copy (`level_duplicate_group`), Duplicate, Copy and Paste
  (`clipboard_copy_nodes`, `clipboard_paste`), a prefab instance - rewrites
  each copied record naming a copied node to name that node's copy, so the
  copy is attached as its source is. A copy of A alone under the same
  support stays attached; a copy of A alone elsewhere is inert.
- Viewing: the Properties window's Kit Piece group shows the row "On" with
  the support's name and the status while a record exists, and its Detach
  row; the rows are read with the piece's record (on a record write, a
  rename of a piece, the selection). `level_get_pieces` reports
  `surface_attachment` per piece (`state`, `reason`, `target_uid`,
  `target_id`, `target_name`, `position`, `direction`, `orient`, `rest`),
  null without a record; `level_place_pieces` gives each piece's `support`,
  `level_snap_to_surface` each node's `support` (the mesh it rests on) and
  `attached_to` (its support after the call).

## Foliage paint

Foliage is the kit pieces and other meshes that carry the foliage flag
("Surface placement"), painted and kept apart by their radii (GitHub issue
302). A foliage item carries a radius of its own and has a place in its
scene's foliage index; a foliage shape gives its items their default radius.

- Foliage shapes: a shape that states `Kit_shape_definition::foliage_radius`
  (meters) is a foliage shape, the value the default radius of its items.
  `Kit_shape_library::get_foliage_shapes()` gives them in library order.
  They are the vegetation shapes of the nature kit: `nature.grass_tuft`
  0.15 m, `nature.succulent` 0.4 m, `nature.leaf_plant` 0.6 m,
  `nature.palm_shrub` 0.6 m, `nature.bush` 0.6 m, `nature.bamboo` 0.5 m,
  `nature.palm_clump` 1.0 m, `nature.tree` 1.5 m, `nature.blocky_tree`
  1.5 m and `nature.palm` 2.0 m. A former id ("Former shape ids") names the
  same shape, so a recreated form has its radius.
- `Kit_piece.foliage_radius` (`Kit_piece::foliage_radius_property`): the
  radius of one foliage item in meters, a float attached to the mesh node,
  default 0, saved with the node and outside the mesh key, so pieces of one
  shape and values share one brush whatever their radii. A write outside
  0 - 100 m is refused (the property's validation). The Properties window
  shows the row "Foliage Radius" in the Foliage group of every mesh that
  carries the foliage flag, a kit piece or a mesh flagged by hand; the row
  and `set_item_property` write it through `Property_set_operation` (one
  undo entry). A mesh made foliage by hand has radius 0 until it is given
  one, and its radius then counts in the spacing rule as a piece's does.
- The foliage index (`Foliage_index`, `level/foliage_index.hpp`): the points
  and radii of the scene's foliage items in a uniform grid of 4 m cells; a
  query visits the cells of its box or scans the entries, whichever is
  fewer, and costs the items near it.
- The foliage system (`Foliage_system`, `level/foliage_system.hpp`): the
  node system (`doc/erhe/scene.md` "Node systems") each `Scene_root` owns
  beside its other node systems (`Scene_root::get_foliage_system()`). It
  owns the scene's index and one record per registered mesh carrying the
  foliage flag, keyed by the node's address: the node held weakly, its id,
  a generation, its state in the index, the index handle, the node's
  transform observer, the captured world translation (scene root space)
  and `Kit_piece.foliage_radius`, and a queued mark. Its index holds those
  records at their captured values; a record whose position or radius the
  index cannot take (a translation that is not finite - an infinite
  translation, a degenerate ancestor matrix - or, as a guard, a radius
  that is not a finite number >= 0) stays out of it and enters again at its
  next valid values.
  - The hooks capture values: each reads what it needs on the thread that
    made the change, before it takes the system's mutex, and only then
    stores it in the record. Registration and the foliage flag set
    (`Scene_root::on_mesh_flags_changed()` forwards a change of the bit to
    `Foliage_system::on_flags_changed()`; every flag writer reaches it
    through the mesh's `handle_flag_bits_update()`) read the world
    translation and the radius and make the record and its transform
    observer; unregistration (of any node) and the flag cleared erase the
    record and release its observer, so an unregistered node has no record. The radius hook (`on_values_changed()`; the
    property's change callback is `erhe::scene::node_system_property_changed`,
    so every writer - the Properties row, `set_item_property`, undo, a
    load, a raw `set_value()`, an expression - reaches it) reads the value;
    a read that evaluates an expression may call the hook again inside it,
    which completes first. The transform observer reads the translation it
    is called with, for the node's own moves and, in
    `Scene::update_node_transforms()`, for every move of an ancestor. A
    record found at the node's address that belongs to another (dead) node
    is replaced by a new record with a new generation.
  - The observer is added and released in the hooks. One writer changes a
    node at a time - the main thread, or a worker holding the scene's
    `item_host_mutex` - as `Node_physics_system` relies on too.
  - The queue holds at most one entry per record (its key and generation)
    plus one removal of an index entry per record erased since the last
    query; an entry of an erased record is inert (its generation is gone).
    `Foliage_system::get_index()`, `find_nodes_within()` and
    `get_nodes()` run on the main thread and
    apply the queue in order under the system's mutex alone: plain values
    go into the index, and no node, property, scene or other mutex is
    touched, so a caller may hold any lock. Records that leave the index
    for invalid values give one warning per query (the count and the first
    node's id). Nothing runs per frame: a frame without a query or a change
    touches neither the queue nor the index, and both queue lists keep
    their capacity. A captured translation is current once
    `Scene::update_node_transforms()` has run for moved ancestors, so a
    reader calls it before a query, as `level_get_foliage` does.
  - `Foliage_system::find_nodes_within(center, distance, out_nodes)`
    gives the nodes of the entries within a distance, `get_nodes()` every
    indexed node (resolved on the main thread from the weak references
    the index handles keep), `find_handle()` a node's entry; `get_index()`
    returns the index read-only.
- `level_get_foliage` ("MCP") reads the index; `level_get_pieces` reports
  each piece's `foliage_radius`.
- Palette (`Foliage_palette_entry`, `level/foliage_palette.hpp`): an entry
  holds `shape` (an id), `parameters` (the canonical text of
  `Kit_parameter_values`, empty for the shape's defaults), `materials` (a
  name per slot, empty for the slot default), `weight` (0 - 1000),
  `radius` (meters), `yaw_min` and `yaw_max` (degrees), `variants`
  (1 - 64) and `seed` (the entry's base seed). Its text
  (`make_foliage_palette_text()`, `parse_foliage_palette_text()`) is a
  JSON list of objects with sorted keys and hexadecimal floats, so equal
  palettes give equal text. `check_foliage_palette()` names the first
  entry that cannot be painted: an unknown shape, parameters that do not
  read or are out of range, a weight above 1000, a radius that is not a
  finite length >= 0, yaw limits that are not finite or out of order,
  variants outside 1 - 64, more materials than slots, a variant whose
  values `validate()` or the shape's combination check refuses. One
  function derives an entry's variants for given level lengths and reports
  their first problem (`derive_foliage_variants()`); the check runs it at
  the default level lengths (`Kit_lengths{}`: 1 m module, 3 m wall),
  `Foliage_stroke::begin()` at the painted scene's lengths, so a stroke
  refuses what those lengths make refused. Variant `v` of an entry has the seed `get_foliage_variant_seed(seed, v)`
  (`derive_layout_element_seed()`, the function of a layout's Per Piece
  Seed), which goes through the shape's derive-from-seed; an entry whose
  values derive nothing from the seed and whose shape is not seeded has
  one variant (`get_foliage_variant_count()`), so an entry needs at most
  `variants` kit brushes.
- Sampling (`level/foliage_sampler.hpp`, scene-free): a paint stamp makes
  `get_attempt_count(density, radius)` = round(density * pi * radius^2)
  attempts, clamped to 1 - 1024, about the brush frame
  (`make_brush_frame()`: the normal and two unit tangents, the first the
  cross product of the normal and the world axis it is least along). The
  draws come from `Kit_random(seed, "foliage paint")` in a fixed order:
  per attempt one 32-bit draw picks the entry by weight
  (`pick_scatter_item()`); then up to 8 candidates, each two 24-bit draws
  giving a point of the square -1 - 1, spent outside the unit circle, else
  a ray from `center + radius * (x * t0 + y * t1) + h * normal` (h =
  radius * sqrt(1 - x^2 - y^2)) against the normal for 2 h, so its hit is
  inside the brush sphere. A candidate without a hit or one the spacing
  rule refuses is spent, and so is one whose hit mesh is inside a
  reference instance (`is_instance_content()`: nothing is added to an
  instance's content, which its referenced file holds). The rule
  (`is_spacing_free()`): a point p of radius r is free when every indexed
  item q is at least max(r, r_q) away, read with one index query about p
  whose range is that bound for the largest radius the index holds,
  widened by 1/65536 of itself (`c_foliage_spacing_query_margin`) so the
  query can only over-include; and every point accepted earlier in the
  same stamp is that far too. The
  accepted point takes one 24-bit draw for the yaw within the entry's
  range and one 32-bit draw for the variant. `run_foliage_paint_stamp()`
  fills caller-owned buffers with the placements and counts the attempts,
  the candidates the spacing refused, those without a surface and those on
  a mesh inside a reference instance (`on_sealed_support`); a palette whose
  weights sum to 0 makes no attempt.
- Strokes (`Foliage_stroke`, `level/foliage_stroke.hpp`): one
  implementation for the foliage paint tool and the MCP tools.
  `begin(scene_root, kind, settings, palette, seed)` takes the brush
  radius, the paint density and the up axis (world up, or the surface
  normal with Align To Surface) as they are at the press. It refuses,
  with the reason, a brush radius that is not a finite length above 0, a
  density that is not finite or is below 0, a palette
  `check_foliage_palette()` refuses, a palette without an entry of weight
  above 0, and a material name neither in the scene nor in the kit
  palette. It resolves each entry once - its values (the shape's defaults
  at the scene's level lengths, or its text), each variant's values with
  its seed applied and checked, the material of each slot - and the
  stream of the seed. `stamp(center, normal)` refuses a center that is
  not finite and a normal that is not a direction; else it commits the
  raytrace scene, runs `Scene::update_node_transforms()`, takes the
  foliage index (so the items of the earlier stamps count), runs the
  sampler with `find_ray_surface()` as its ray (foliage is passed
  through) and places the accepted points with the one operation
  `Level_kits::make_foliage_operation()` makes ("Placing"), run through
  `Operation_stack::execute_in_live_edit()`. Each item stands at its
  point with its origin (a kit shape's base) there, upright along world
  +Y or turned onto the hit normal, turned by its yaw about that up axis,
  as a child of the hit mesh's node; on a mesh `find_surface_support()`
  accepts (a kit piece, a kit layout's generated mesh) it also gets its
  surface attachment record ("Surface attachments": direction against the
  hit normal, `orient` off and rest `anchor`, with Align To Surface too),
  whatever Attach To Surface says, so it rides its support and is carried
  on the support's shape changes. The record rests the origin, where the
  stroke put it, with the item's own rotation: a carry at unchanged bounds
  gives the stroke's pose back. With `orient` on the rest would be the
  base center of the bounds, which lies off the origin of every foliage
  shape, and the first carry would move the item. The sampler's buffers belong to the stroke and
  keep their capacity, so a stamp's sampling allocates nothing once they
  are big enough. A stroke's queries of the foliage system run at its
  stamps on the main thread, never inside an operation.
- Undo: a stroke is a live edit of the stack (`doc/editor/operations.md`
  "Live edits"). `begin()` refuses while the stack reports a block reason
  (a scene save, another live edit) and then opens its own
  (`Operation_stack::begin_live_edit()`), so undo, redo, scene saves and
  other parties' operations wait for the stroke. Each stamp that places
  something executes one `Compound_operation`, which the live edit holds.
  `end()` records the stroke's operations as one entry
  (`Operation_stack::end_live_edit()`; inside an MCP batch the entry joins
  the batch's), or none when no stamp placed anything, and the redo history
  then stays. `cancel()` undoes the stroke's operations in reverse order
  (`Operation_stack::cancel_live_edit()`) and records nothing. A scene
  close and the editor's shutdown cancel an open stroke through the stack
  (`abandon_live_edit()`), which then clears the stroke's state. The owner
  ends every stroke it begins with `end()` or `cancel()`, or leaves it to
  that abandon; the destructor cancels a stroke left open, with a warning.
- Remove strokes (`Foliage_stroke_kind::remove`): `begin()` takes the
  brush radius, the Remove Density (`remove_density`, 0 - 1) and the filter
  (`Foliage_remove_filter`: every foliage item, or Remove Only Palette
  Shapes, the kit pieces of the palette entries' shapes); it refuses a
  brush radius that is not a finite length above 0 and a density that is
  not finite or lies outside 0 - 1. With Remove Only Palette Shapes it reads
  the entries' shapes alone and refuses an unknown one; removing every item
  it reads no palette. It opens its live edit as a paint stroke does. The draws come from `Kit_random(seed, "foliage remove")`, and the
  threshold `get_remove_threshold(density)` (the count of 24-bit draws
  below density * 2^24; `is_remove_draw()` compares one draw with it) is
  computed once per stroke, so 1 removes every item
  and 0 none. `stamp(center, normal)` refuses a center that is not finite and
  does not read the normal. A remove stamp runs
  `Scene::update_node_transforms()`, takes the foliage index (the removals
  of the earlier stamps are applied) and visits the items whose indexed
  point lies in the brush sphere (distance at most the radius), keeping
  with Remove Only Palette Shapes those whose `Kit_piece.shape` names one
  of the entries' shapes (a former id names its shape; a mesh that is no
  kit piece has no shape and is kept), in the order of
  `is_before_in_remove_order()`: x, then y, then z, then the node id. Each
  visited item takes one 24-bit draw, removed or not, so the result follows
  from the seed and the item set; a draw below the threshold removes the
  item with its subtree. An item the lock of a kit layout node protects
  (`locked_layout_refusal()`; the lock ends at a surface attached piece, so
  foliage painted on a locked layout's child is not protected) takes its
  draw and is kept. The removals of a stamp are the Delete command applied
  to exactly the drawn items: one operation of
  `Selection::make_delete_operation()`, the `Compound_operation` the Delete
  command queues, run through `execute_in_live_edit()`. It removes each item
  and every node below it from its parent, deepest first, collecting an
  item given beside its ancestor once; an edit-locked item (`lock_edit`) and
  its subtree are kept and move to the removed parent's parent, while a
  drawn item below an edit-locked one is removed on its own. A kept item is
  not counted. The selection loses the removed items; undo and a cancelled
  stroke give them back. Undo puts every node back under its parent
  with its transform and values: the surface attachment record, the
  foliage radius and the flag travel with the node. Only items carrying the
  foliage flag are in the index, so nothing else is removed, though the
  subtree of a removed item goes whatever its flags. The stroke counts
  `removed` and keeps the removed foliage nodes in removal order
  (`get_removed_nodes()`) while it is open; `end()`, `cancel()` and an
  abandon release them and the placed nodes, so a finished stroke holds no
  node. A stroke that removes nothing records no entry.
- `level_foliage_paint` ("MCP") runs one paint stroke from its arguments,
  `level_foliage_remove` one remove stroke.
- The Foliage Paint tool (`Foliage_paint_tool`, `level/foliage_paint_tool.hpp`;
  `App_context::foliage_paint_tool`): a toolbox tool of the hotbar (base
  priority 4, as the Brush tool's), the one owner of strokes besides the
  MCP tools. Its commands, each hosted by the tool and editable in the
  Input Bindings window:
  - `Foliage_paint.paint`, a left drag called at the press: the press
    begins a paint stroke and stamps at the brush, and the release ends it.
    It is ready only with a brush, no stroke open, no block reason of the
    stack (`get_undo_block_reason()`) and a palette that can paint. Its drag
    binding matches the press's modifiers exactly
    (`erhe::commands::Drag_modifier_match::exact`, all four modifier bits),
    so a press with other modifiers goes on to the camera's drags and a user
    binding with a modifier (Ctrl + left drag) paints with that modifier
    alone.
  - `Foliage_paint.remove`, a right drag called at the press, the same with a
    remove stroke. While the tool is active it takes every plain right
    press, with a brush or not (without one the press does nothing), so the
    viewport context menu's release click does not follow it (a plain right
    click is a remove stroke of one stamp); Alt + right drag stays the
    camera's tumble.
  - `Foliage_paint.radius`, the wheel with Shift (an exact mask): one step
    multiplies the brush radius by 1.1, a step down divides it, clamped to
    0.1 - 50 m, and writes the setting, whenever the pointer is inside a
    viewport's rectangle, with a brush or not (over the sky too); the plain
    wheel stays the camera's zoom. An open stroke follows the radius
    (`Foliage_stroke::set_brush_radius()`): its next stamp and its travel
    step take it, and the circle shows it.
  - `Foliage_paint.cancel`, Escape with any modifiers: cancels the open
    stroke; it declines when none is open.
  While a stroke is open both drag commands are ready for their button's
  plain press and consume it without starting anything, so a press of the
  other button between the stroke's press and its first motion reaches no
  lower command (the context menu, a camera drag) and begins no second
  stroke.
- The brush (`Foliage_brush`: the scene, the center, the unit normal and the
  pointer ray that found them): read at the hover changes - the
  `hover_scene_view` and `hover_mesh` messages the Brush tool subscribes
  to - from `Level_kits::get_placement_hover()` of the hovered scene view,
  so a foliage item under the pointer is passed through. A drag keeps its
  viewport hovered while the pointer is over another window; the brush is
  only where the pointer is inside the viewport's rectangle. The brush
  center is the nearest hit of the pointer ray on a surface mesh
  (`is_placement_surface()`: neither pass_through nor foliage): a grid or
  analytic hover (the grid in a floor's top plane can win the tie by
  rounding) and a hover on a pass_through mesh (water) give way to
  `Level_kits::find_placement_surface()`, the ray form of the same rule,
  along the same pointer ray; with no surface hit there is no brush.
  While a stroke is open the tool ignores the hover of other scenes: a
  stroke belongs to the scene of its press. Nothing is read per frame.
- Stamps (F5.1, G4): one at the press; then each hover change queues the
  travel from the last brush to the new one, and the walk stamps every half
  brush radius of it, the remainder carried, so the stamp count is a
  function of the path: on flat ground a drag of length L stamps
  1 + floor(L / (r / 2)) times, whatever steps the pointer moves in. A
  step's center is the surface hit (`find_placement_surface()`) of the ray
  from the pointer ray origin interpolated between the two brushes toward
  the point at the same fraction of the chord between their centers, so the
  stamps are equally spaced along the chord on flat ground; a ray without a
  surface hit stamps nothing there and the walk goes on. The walk carries
  the distance since the last stamp, so after a Shift + wheel the next stamp
  is one new step after the last one; when the new step is shorter than the
  distance already carried (Shift + wheel down), the next stamp is at the
  walk's current point, never behind it. One budget per frame covers the hover
  change and the tool's update command (`Foliage_paint.walk`, which walks
  only while travel is pending): 16 stamps while at most 256 stamps are
  pending, and beyond that everything above 256, so the backlog never
  exceeds 256 stamps plus one hover change's travel. The pending hover
  changes are a ring of 32; a full ring walks its head segment to the end
  before the next one is queued, so no travel is dropped and nothing is
  allocated. Leaving the viewport breaks the travel: the re-entry brush
  starts a new segment, with one stamp there when it is at least half a
  radius from the last stamp, and no stamps along the chord across the
  outside. The release (its button no longer down; not a deactivation, a
  hidden view, a binding rebuild or an abandon) first takes the pointer ray
  at the release event (the hover of a motion is read a frame later: the
  tool sets the view's pointer from `Commands::last_mouse_position()` and
  refreshes its ray), when that pointer is over the stroke's viewport, and
  walks everything pending before the stroke ends. The stroke takes the tool's values at the press (densities, Align To
  Surface, Remove Only Palette Shapes, the palette and the seed) and the
  current radius at each stamp; nothing `Foliage_stroke::begin()` builds
  depends on the radius.
- The seed: the stroke draws from the Seed setting, and when a stroke that
  began ends - released, cancelled or abandoned - the setting takes
  `kit_next_seed()` of the seed it drew from. A refused begin changes
  nothing.
- Lifetime: a stroke ends as a release does when the tool loses priority
  (a hotbar key, a tool switch; its two drag commands are made inactive, so
  no active mouse command is left behind), and when the scene view of its
  press is destroyed or hidden, whether or not the drag has moved: on the
  drawn-to-hidden edge `Viewport_window::hidden()` calls
  `Foliage_paint_tool::on_stroke_view_gone()` directly, and the destroyed
  view's hover message does the same. On that edge a viewport window whose
  scene view holds the pointer capture (`Scene_views::owns_pointer_capture()`)
  also makes the active mouse command inactive, since its release cannot
  arrive (for every tool's drag); Escape cancels the stroke; the
  stack's abandon (a scene close, the shutdown) reaches the tool through
  `Foliage_stroke::set_abandon_callback()`, which records the outcome and
  advances the seed only. The shutdown abandons before the parts go, so
  the tool's destructor verifies that no stroke is open. While a stroke is
  open the stack refuses other changes, and the tool's own widgets write
  settings only.
- Drawing (`tool_render()`, x-ray lines of 2 pixels): a 64-segment circle of
  the brush radius about the center in the plane across the normal and the
  normal as a line of a quarter radius, in the scene of the brush. The
  circle's points are a member array, computed when the brush or the
  radius changes. The colors are `Debug_visualizations_style::foliage_paint_color`
  (idle and painting) and `foliage_remove_color` (a remove stroke), edited
  in the Debug Visualizations window's style rows ("Foliage Brush").
- The tool's window (Tool Properties): Brush Radius, Paint Density (0.01 -
  100), Remove Density (0 - 1), Align To Surface, Remove Only Palette
  Shapes and Seed; the palette, one row group per entry - the shape's
  thumbnail from the Level Kit window's shape thumbnails (`Kit_thumbnails`),
  its name, Remove, Move Up, Move Down (the entry order is the weighted
  pick's order and the saved order), Reset (enabled when the entry has
  parameters), Weight, Radius, Yaw (a range), Variants, the shape's
  parameter rows (`kit_parameter_row()`, "Parameter rows") and a material
  combo per slot (the slot's default first, then its alternatives) - and
  Add Entry, which lists the foliage shapes: a new entry takes the shape's
  foliage radius, weight 1, yaw 0 - 360, the shape's defaults (empty
  parameters) and 8 variants for a shape whose defaults derive values from
  a seed or that draws from it, 1 otherwise. The parameter rows show the
  entry's values at the default level lengths (1 m module, 3 m wall), as
  `level_set_foliage_paint_settings` reads a `parameters` object; a change
  that `validate()` accepts writes the values' canonical text into the
  entry, a refused typed value shows the message and the entry's values
  again, and Reset empties the parameters (the shape's defaults at the
  painted scene's lengths). The from-seed toggles choose which values each
  painted variant derives from its seed and which it keeps as shown; the
  entry's Variants decides how many seeded forms are painted. Each variant
  takes the seed `get_foliage_variant_seed()` gives, so for an entry that
  derives values the Seed row is shown disabled. The thumbnails keep the
  lengths of the first scene the thumbnails followed (the Level Kit window's, or the last
  hovered scene's when no window followed one). Below the entries the
  reason painting is disabled, and the last stroke: its kind, outcome
  (ended, cancelled, abandoned or did not begin, with the refusal) and
  counts. Each widget writes the setting at its edit.
- Settings (`Foliage_paint_config`, the `foliage_paint` section of
  `editor_settings.json`, editor settings version 22): `brush_radius` 2,
  `paint_density` 0.5, `remove_density` 1, `align_to_surface` false,
  `remove_only_palette_shapes` false, `seed` 1 and `palette`, the
  palette's canonical text (empty for none). The tool parses and checks the
  text against the kit shape library at construction (it takes
  `Level_kits` as a constructor argument) and at its own writes of the
  palette; a text that does not read leaves the palette empty with the
  reason shown. The painting refusal (F3.3) is computed then: an empty
  palette, an entry `check_foliage_palette()` refuses, or no entry of
  weight above 0.
- `level_get_foliage_paint_settings`, `level_set_foliage_paint_settings` and
  `level_get_foliage_brush` ("MCP") read and write the settings and read
  the brush and the stroke; `set_active_tool` names the tool
  `foliage_paint`.
- Scale (F12), measured in the headless Vulkan editor on Windows, Release
  and Debug builds. An inserted item costs the same whatever the number of
  its siblings (the child name index, `doc/erhe/item.md` "Sibling-unique
  names"): one `level_foliage_paint` stroke of 2,390 grass tufts under one
  floor takes 0.53 s in Release (0.22 ms per item) and 48 s in Debug, where
  the sibling-unique name pass is 13 us of an item's insert. A stamp of 64
  attempts spends 0.075 ms (median; worst 0.11 ms) in the index queries and
  the spacing tests over 10,000 entries in Release and 1.6 ms in Debug (the
  `Foliage_sampler` timed case). On a scene of 1,202 items, 1,000 of them on
  one floor slab: one more stamp of 64 attempts 7 - 18 ms (Release) and
  75 ms (Debug); a set_node_transform move of an unrelated kit piece 7 ms
  (67 ms) and of the carrying slab 24 ms (102 ms); a width change of the
  slab, which carries its 1,000 items with one surface search each, 0.25 s
  (17.6 s); the frame with nothing changing 3.9 ms (3.8 ms), as before the
  painting (3.6 ms); the saved scene 36.6 MB, about 26 KB per item, since
  each item's mesh is written into the file; its open 2.4 s (74 s) until
  idle, its slowest frame 0.5 s (5.2 s). At 5,002 items (Release) the move
  of an unrelated piece takes 17 ms, the carry of 1,000 items 0.29 s, the
  save 109 MB and the open 13.5 s, with a frame of 4.9 s. One
  `level_foliage_remove` stroke of one stamp at density 1 over 1,058 grass
  tufts on one floor slab takes 0.047 s in Release (0.045 ms per item) and
  2.2 s in Debug (2.1 ms per item); the selection update each removal runs
  is about 1 % of that (the stroke without it: 2.19 s in Debug).
- The kit's surface ray visits each raytrace instance that changed in the
  last 30 commits of the scene, outside the scene BVH; the hover ray of an
  interactive session commits once a frame, so the items of a stroke reach
  the scene BVH half a second after they are painted. A headless MCP run
  has no hover and commits once per stamp: an item stays outside the BVH
  for the next 30 stamps, and every candidate ray of a stamp visits each
  such item (Debug, a stamp of 302 attempts: 0.9 s over 300 such items, 9 s
  over 1,200).

## Parameter rows

`kit_parameter_rows.hpp` draws a piece's parameters for both windows that edit
them: `kit_parameter_row()` is the widget of one parameter (a drag per float
with its range and unit, a drag per integer, a combo per enum, the description
and range as tooltip), `kit_stairs_line()` the stairs layout line and
`kit_message_line()` a refusal in red. A derivable parameter's row has a "from
seed" checkbox in front (tooltip "Derive from seed (range a - b)", the range
resolved against the lengths the row is given); a masked row shows its derived
value disabled. The `detail` row of a shape without detail counts is
disabled, its tooltip saying the shape has one level of detail. The seed row has a New button that takes `kit_next_seed()`. A
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
materials); Module Snap, Surface Snap, Surface Direction (three drags, -1 -
1 each, a zero vector refused), Surface Forward Distance, Surface Backward
Distance, Surface Support (a combo: Five Lines, Center Blocks, Center
Fallback), Surface Orient, Per Piece Seed and Layout Seed; for a path or segments layout the connector
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
Max, an item's Weight, the Layout Seed, the Surface Direction, Forward
Distance and Backward Distance) previews each change through
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
rotation, scale in the scene root's space) and `Surface_options`
(`surface_options.hpp`: `snap`, `orient`, `rest`, a unit `direction`, down by
default, `forward_distance`, meters along the direction,
`backward_distance`, meters against it, and `support`, the support rule
below). It commits the scene's raytrace
scene and searches along the direction up to the forward distance and
against it up to the backward distance for the nearest place where the
piece rests and fits (U23, U51); a backward distance of 0 searches along the
direction only. A kit layout takes its own direction and distances
("Kit layout nodes"; 1 m forward and 0 m backward by default); the surface
settings' callers - the Brush tool's kit mode,
`level_snap_to_surface`, `transform_selection`, the MCP placement tools,
`make_placement_pose()` and the carry of a surface attachment ("Surface
attachments", which also attach what they snap) - search their one max distance either way
(`make_surface_options()`); a Transform tool drag snaps its anchor, one point,
along its own direction ("Transform tool"); `make_placement_pose()` searches against the
normal of the surface under the pointer (U51). The excluded meshes - the ones being moved - are not there, and
neither is a mesh carrying `Editor_item_flags::pass_through` (a water piece)
or `Editor_item_flags::foliage`:
the snap, the orient, the support report and the height map pass through it
to what lies below (`is_surface_mesh()`, the one test every surface ray and
search of `level_queries.cpp` asks).

- A mesh rests on one face of its node-space bounds, its base face (U51):
  content is Y-up, a kit shape's origin at its base. The face comes from
  `surface_base_frame.hpp`; the top face is the opposite one, and the
  piece's up axis is the node-space axis from the base face toward the top
  face, the scale's sign folded in.
  - Orient takes the min Y face whichever way the start pose turns it
    (`get_orient_base_frame()`) and turns it onto the surface: a piece
    searching up under a ceiling turns over and mounts on it by its base, a
    piece turned upside down over a floor stands up on its base. Its one
    line runs through that face's center (`get_orient_search()`), but the
    window's distances are measured from the Y face leading along the
    direction, so a piece lying upside down on a floor finds the floor
    under it; its span is the whole base-to-top extent,
    since the turned piece stands along the normal whatever the start's
    extent along the direction (a piece searching a wall sideways is
    refused where a panel lies within its height off the wall).
  - Snap alone does not turn the piece, so it rests on the face of its
    bounds that leads along the direction (`get_surface_base_frame()`: of
    the six faces the one whose outward normal, taken to world space by the
    rotation and the signs of the scale, is most along the direction; an
    exact tie keeps -Y, then +Y, -X, +X, -Z, +Z). Down, that is the min Y
    face at any tilt below 45 degrees, mirrored along X or Z or not (a world
    transform decomposes a mirror into all-negative scale and a turned
    rotation, which this score reads correctly), and the max Y face of a
    piece turned over or mirrored along Y. Up under a ceiling an upright
    piece rests against it by its top; into a wall it rests flush against
    it by the side face toward the wall, the whole piece in front of it
    (user ruling, 2026-11-18). The snap and the support report take their
    base points and spans from the one frame.
  - With `rest` `Surface_snap_rest::anchor` (U60) snap alone rests no face: the
    start pose's origin lands on the surface on one line along the
    direction, the nearest surface hit within the window, with no fit test
    (`get_anchor_rest()`, as a Transform tool drag's anchor point; a start already
    at rest is returned bit for bit). A kit
    shape's origin is at its base, so on a floor both rules agree; into a
    wall the origin lands on the wall's face and the piece reaches into it.
    `rest` is `leading_face` by default and in every caller except the two
    that read the Surface Rest setting (the Brush tool's kit mode and
    Transform tool drags) and the MCP calls that take `surface_rest`; kit
    layouts, `level_snap_to_surface`, `transform_selection`, Replace and the
    Level Kit window's snap buttons rest the leading face. Orient ignores it.
- A line runs along the direction through each base point: for snap the
  base center and the four base corners (the corners 1 mm in world units
  inside the footprint, along the base face's two in-face axes, so a face
  flush with a side of the piece is not met along its edge), for orient the
  base center alone. Two rays from the base point, one each way along the line, collect
  the hit points of the line's search window, from the forward distance
  along the direction to the backward distance plus the piece's extent
  against it: every face
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
  against the direction, taken from the start pose - for orient the
  extent's size, as the turned piece stands against the direction - at
  least 1 mm, and 0.1 mm in from both ends). Free space is the absence of hit points: a face of
  either facing blocks a span it lies in, and solids are not reconstructed,
  so fit is tested along the lines only, not as a volume overlap.
- A piece inside a solid taller than itself has no hit point in its span,
  so the search can snap it to a surface inside that solid (the slab top
  under a hill, inside the hill; the slab top under a wall the piece
  overlaps) when that is nearer than the solid's top (U31; considering
  solidness is future work).
- The support rule (`Surface_options::support`, `Surface_support`, U52)
  chooses which of the five lines of snap alone support and which block
  (`find_support_height()`, `surface_snap_search.hpp`; line 0 is the base
  center). `five_lines`, the default and the rule of every caller but a kit
  layout: every line supports and blocks, as described above. `center_blocks`:
  every line supports, and only a hit point on the base center's line
  refuses a place, so clutter under a footprint corner does not refuse the
  floor under a piece. `center_fallback`: the nearest place of the five
  lines, ignoring fit, is taken when it fits; when a hit point inside the
  span refuses it, no farther place is searched and the base center rests on
  the nearest surface hit of its line along the direction, within the
  forward distance, with no fit test (a chair under a table rests on the
  floor under its center, reaching into the table); with no surface there
  the place is refused. A pose it rests has its center hit at displacement
  0, the nearest there is, so snapping again keeps it. A kit
  layout takes its own rule (`Kit_layout.surface_support`, five_lines by
  default); the Brush tool, Transform tool drags and the other callers keep
  `five_lines`. Orient and the anchor rest search one line and ignore it.
- The place with the smallest displacement within the search window is the
  one taken; of two at the same distance, along and against the direction, the
  one along the direction (lower) wins. Snap moves the mesh by it; a piece
  hanging over an edge rests on the edge, a piece whose span holds the top
  of a solid it stands in rises onto that top or drops to the nearer free
  surface, a piece in a gap lower than itself goes elsewhere, and a piece
  over a stack of thin slabs with gaps lower than itself goes to the top
  slab when that is within the distance.
- Orient searches its line the same way and then turns the mesh by the
  shortest rotation that takes its start pose's up axis (local +Y, the scale's
  sign folded in) to the normal of the top face at the place, which keeps
  the heading of a piece that starts upright and otherwise turns it only as
  that rotation does; where the start's up axis points along the direction
  (an upright piece searching up, a turned-over one searching down) a half
  turn about the base face's u axis (local X, a path element's heading)
  comes first, so the heading is kept there too (`get_oriented_pose()`).
  The half turn depends on the start pose and the direction only, so the
  elements of a layout along a wall share one roll whichever side of
  vertical each found normal leans. Its base center comes to rest on that face. Where several top faces meet at the place the
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
- The placement names its support, `Surface_placement::support_mesh`: the
  mesh owning the face the rest was found on, null when no place was found.
  Each hit point of a line carries its mesh (`Surface_hit_point::mesh`), and
  the search's offset names the supporting hit (`Surface_offset::line` and
  `hit`). For snap alone it is the supporting hit of the taken place, the
  first line resting there within 0.1 mm, the base center's line first
  (`with_first_supporting_line()`), so the support is the mesh under the
  base center wherever the center rests at the place; for orient the face
  under the base center whose normal the turn takes; for the anchor rest
  the face the origin lands on. A start already at rest names its support
  too. `make_placement_pose()` passes it on in
  `Kit_placement_pose::support_mesh`, and `Level_kits::snap_to_surface()` in
  each `Kit_snap_result`'s placement; `level_snap_to_surface` replies it per
  node as `support` `{id, name}` (null without a place). The support is
  reported whatever mesh it is.
- `only`, an optional span of meshes beside `excluded`: when it is not
  empty, no other mesh is a surface for the search (`is_surface_mesh()`), so
  a piece can be rested against one support's faces alone.

Snap to surface is idempotent for one exclusion set: only hit points within the
piece's span at a candidate within the search window are read, so the answer
does not depend on where the window ends; a snapped pose's surface hit is at
its base, outside its span, so the pose is a place at displacement 0 (within
0.1 mm), the nearest there is, and snapping again with the same excluded
meshes answers zero and returns the pose bit for bit, with orient keeping
the rotation, except for an oriented piece whose base center is on an edge
where both faces end. A mesh oriented onto a plane stays there on any slope
or ceiling: its base face is the min Y face whatever the pose. A pose that
found no place finds none again; a rest found with a backward distance of 0
is at displacement 0, which every window holds. The answers of different callers for the same pose agree only as far as their
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

`Level_kits::make_placement_pose()` is the same computation for a piece
placed at a point on a surface with a given normal (U51): the origin at the
point, upright and turned about the up axis by a count of rotation steps,
then the surface options with the negated normal as their direction, so snap
and orient search into the surface the point lies on - a floor, a wall or a
ceiling. Module snap rounds the point as `place()` does (above). Orient turns
the piece's up axis onto the normal: on a wall the piece stands out of the
wall, under a ceiling it hangs from it by its base. The rotation steps turn
the piece about its up axis before the orient, so they turn it about the
normal. Snap alone keeps the piece upright and, with Surface Rest leading face
(the default), rests the leading face of its bounds on the surface: its base
on a floor, its top against a ceiling, its side flush against a wall; with
Surface Rest anchor (U60) its origin lands on the surface instead - the same
pose on a floor, the origin on a wall's face with the piece reaching into
it. On faceted terrain the direction, and with orient
the tilt, follows the facet under the pointer. Without a normal - the ID
renderer's hover before the hovered mesh's geometry is loaded - the settings'
direction (down) is used, so the preview acts as over a floor until the
geometry arrives. `Kit_placement_settings` (module snap, surface options,
rotation step) carries its inputs, and `get_placement_settings()` reads them
from the editor settings `level_kit` and `surface_snap` ("Surface settings").
It allocates nothing, so the Brush tool's kit mode runs it on every hover
update ("Level Kit window"). The pose carries the search direction it was
found along (`Kit_placement_pose::surface_direction`).

A caller placing several meshes commits the raytrace scene once and passes it to
the `find_surface_placement()` overload that takes a committed
`erhe::raytrace::IScene`.

The foliage flag (label "Foliage", saved by name as `foliage`) is an authored
flag on a mesh node, independent of `pass_through`, editable in the
Properties window like every authored flag. A foliage mesh is no surface for
placement rays, and every other ray sees it: viewport hover and selection,
the Transform tool and material paint hit it (`raytrace_node_mask()` keeps
it in the content slot). The flag changes no physics body. The scene's
foliage index holds every mesh carrying it ("Foliage paint").

- `find_ray_surface()` (`level_queries.hpp`) is the pointer ray of
  placement: the nearest front face of a surface mesh along a unit
  direction within a distance, as a `Surface_ray_hit` (position, unit normal
  toward the ray's origin, mesh, and the hit primitive and triangle a caller
  resolves to a facet); it allocates nothing. `is_placement_surface()`
  answers whether a mesh is such a surface (neither pass_through nor
  foliage).
- `Level_kits::get_placement_hover()` gives a placing tool its surface
  point as a `Hover_placement_hit` (`scene/hover_placement.hpp`): its
  source (`surface`, `grid` or `analytic`), the point, the normal turned
  toward the pointer ray's origin, the surface's mesh with the hit mesh
  primitive and facet, or the grid and the plane it was hit in. It is the scene view's nearest content or grid
  hover; when that is a foliage mesh, it casts `find_ray_surface()` along
  the scene view's pointer ray and takes the surface behind the foliage, or
  the grid when the grid is strictly nearer (the rule of
  `Scene_view::get_nearest_hover()`); it casts that ray only then.
- The Brush tool's kit mode (`update_kit_preview_transform()`, run at hover
  changes; over foliage it reads the hit `on_motion()` computed for the
  pass-through, so the ray is cast once per motion) takes everything about a placement from that one hit: the point
  (grid-snapped on the grid), the normal and the connector join target (the
  hit's mesh, the distance measured from the hit's point), so a piece
  placed over a foliage wall joins nothing of it. The Brush tool's ordinary
  (non-kit) brush placement passes foliage too (G8): over a foliage mesh its
  hover entry is that hit (`Brush_tool::make_placement_hover_entry()`), so
  the preview, its face frame (the hit's mesh primitive and facet), the
  insert, the pick of the brush a hovered mesh was placed from
  (`try_pick()`, through `get_hover_brush()`) and the wheel rotate of a
  hovered placed mesh (`try_rotate()`) act on the surface behind, the
  support and not the foliage item; the hover position it reports stays
  the pointer's. The viewport context
  menu's Create / Paste placement (`doc/editor/windows.md` "Viewport
  context menu") takes its point and normal from it. A crate placed over
  painted grass lands on the ground.
- Level Check (`run_level_check()`) leaves foliage meshes out of its
  support and overlap parts and counts them in `excluded_count`.

## Surface settings

Snap To Surface and Orient To Surface are one pair of editor settings,
`surface_snap` (`Surface_snap_config`, `editor_settings.json`; both off by
default, because they change how the Transform tool moves every mesh). The
same config holds Surface Max Distance, `max_distance` (meters, 1 by default;
"Surface placement"; a loaded value that is not a finite number >= 0 is
reported in the log and replaced by 1; `level_kit.layout_surface_forward_distance`,
`layout_surface_backward_distance` and `layout_surface_direction` are reported and
replaced by their defaults the same way, `Level_kits::validate_surface_settings()`;
the settings search the distance either way, `make_surface_options()`), Connector Snap, `connector_snap_distance` (meters, 0 = off,
the default; "Connector snap" below), and Connector Snap On Axis, `connector_snap_axis_mode`
(`Connector_snap_axis_mode`: `full_offset`, the default, or `stay_on_axis`; a file older than
`Surface_snap_config` version 4 reads the default), and Connector Snap Orientation,
`connector_snap_orientation` (`Connector_snap_orientation`: `off`, the default, `rotate` or `full`,
U50; a file older than version 5 reads the default, and the version 5 - 7 names `keep` and `align`
read as `off` and `full`, the enum's former names, `doc/erhe/codegen.md`). Surface Drag Direction Mode, `drag_direction_mode`
(`Surface_drag_direction`: `explicit_vector`, the default, or `camera_ray`), and Surface Drag
Direction, `drag_direction` (a world vector, (0, -1, 0) by default, stored as given - by the
widget and by `set_surface_snap_settings` alike, so the widget shows what it stored - and normalized
when a drag starts; a loaded zero or non-finite vector is reported and replaced by the default), are the
search direction of Transform tool drags ("Transform tool" below); a file older than version 6
reads the defaults. Surface Rest, `rest` (`Surface_snap_rest`: `leading_face`, the default, or
`anchor`; U60), chooses what snap rests with Orient To Surface off in the two tools that read it,
Transform tool drags and the Brush tool's kit mode: the bounds face leading along the search
direction flush on the surface, or the anchor point - the gizmo anchor, the placed piece's origin -
on it ("Surface placement", "Transform tool"); with orient on it does not apply, and kit layouts
always rest the leading face; a file older than version 7 reads the default. Attach To Surface,
`attach_to_surface` (false by default, version 10; a file without it reads it off), makes a
kit piece snapped onto a support its child ("Surface attachments") and implies the snap, as Orient
To Surface does: `get_surface_settings()` has `snap` on while Snap To Surface or Attach To Surface
is on, so a placement or a drag with Attach To Surface alone snaps and attaches, and carries the
setting in `Surface_options::attach`, which only the callers that attach read. `get_surface_snap()`
is Snap To Surface as stored, which the widgets and `get_surface_snap_settings` show;
`set_surface_attach()` writes Attach To Surface; MCP `get_surface_snap_settings` / `set_surface_snap_settings` name it `attach`. The settings have one owner:
`Level_kits::get_surface_settings()`, `set_surface_snap()`,
`set_surface_orient()`, `set_surface_max_distance()` (the distance must be
a finite number >= 0, `check_surface_max_distance()`; the widgets clamp to 0 -
1000 m and the MCP tools refuse other values), `get_connector_snap_distance()` and
`set_connector_snap_distance()` (a negative or non-finite value is stored as
0), `get_surface_rest()` and `set_surface_rest()` (`get_surface_settings()` leaves its `rest`
`leading_face`; `get_placement_settings()` and the Transform tool's drag start set it from the
setting), `get_surface_drag_direction_mode()` and `set_surface_drag_direction_mode()`,
`get_surface_drag_direction()` (as stored) and `set_surface_drag_direction()` (the vector must pass
`check_surface_direction()`; the widget keeps the stored vector while its edit is zero), `get_connector_snap_axis_mode()` and `set_connector_snap_axis_mode()`,
`get_connector_snap_orientation()` and `set_connector_snap_orientation()`, which touch the settings store. Every widget writes through them at its
change (`level/surface_settings_ui.hpp`), so all of them show the same state:

- the Level Kit window's checkboxes, its "Surface Rest" combo (Leading Face,
  Anchor) and "Surface Max Distance" drag after Snap To Surface, its "Surface Drag Direction Mode" combo (Explicit, Camera
  Ray) and "Surface Drag Direction" drag after Orient To Surface (with the "Attach To Surface" checkbox
  between them), its "Connector Snap" drag, its "Connector Snap On Axis" checkbox and
  its "Connector Snap Orientation" combo (Off, Rotate, Full);
- two toggle buttons with a narrow Surface Max Distance drag between them
  (named "Surface Max Distance") and a narrow Connector Snap drag, labelled with the
  magnet icon ("Connect" without the icon font), in every viewport's
  toolbar, after the Move / Rotate / Scale buttons (icons arrow-collapse-down
  and slope-uphill, tooltips "Snap To Surface" and "Orient To Surface"; they
  report a checked state to the item recorder, so `get_imgui_items` lists
  them as checkable items named "Snap To Surface" and "Orient To Surface";
  the drag is named "Connector Snap"); right after the Snap To Surface
  button a toggle button for the Surface Rest anchor rule (icon anchor,
  "Anchor" without the icon font, named "Surface Rest Anchor"; off is the
  leading face); after Orient To Surface a toggle button for the camera ray
  mode (icon camera-outline, "Ray" without the icon font, named "Surface Camera
  Ray"; off is the explicit vector), then the Connector Snap label and drag, followed by a toggle button for
  Connector Snap On Axis (icon axis-arrow-lock, "Axis" without the icon font,
  named "Connector Snap On Axis") and a button for Connector Snap Orientation
  that cycles Off, Rotate, Full (checked unless Off; icon rotate-3d-variant for
  Off and Full, axis-y-rotate-counterclockwise for Rotate, "Turn", "Rotate" and
  "Full" without the icon font; named "Connector Snap Orientation", its id the
  same in every mode);
- the rows "Snap To Surface", "Surface Rest", "Surface Max Distance", "Orient To Surface", "Attach To Surface",
  "Surface Drag Direction Mode", "Surface Drag Direction", "Connector Snap", "Connector Snap On Axis" and "Connector Snap Orientation" of the Transform
  window's Move tool group, after "Snap Enable".

Kit placement in the viewport reads the pair through
`get_placement_settings()`. MCP `get_surface_snap_settings {}` reads the
settings and `set_surface_snap_settings {snap, orient, max_distance, rest, drag_direction_mode,
drag_direction, connector_snap_distance, connector_snap_axis_mode, connector_snap_orientation}` sets them (editor
state, no undo entry; an omitted argument keeps its value; a negative or non-number distance, a rest
other than `leading_face` or `anchor`, a drag direction mode other than `explicit_vector` or `camera_ray`, a zero or non-finite drag direction, a mode
other than `full_offset` or `stay_on_axis` and an orientation other than `off`, `rotate` or `full`
(or the former names `keep` and `align`) are refused); both reply `snap`, `orient`, `max_distance`, `rest`, `drag_direction_mode`, `drag_direction` (as stored),
`connector_snap_distance`,
`connector_snap_axis_mode` and `connector_snap_orientation`. The MCP placement and
transform tools never read them: they take their own arguments (`level_place_pieces` and
`drag_selection` take `surface_rest`, `leading_face` by default, and echo it);
`level_get_placement_preview` reports the Brush tool's live preview and with it the `surface_rest`
it follows.

### Transform tool

With the pair on, a translate drag of the gizmo snaps the moved set to a
surface at every step (D4.6, U51), a zero step
included (a press without motion, a drag back to the press point or under
half a move grid step, a zero `drag_selection`). After
`Transform_tool::adjust_translation()` writes the step's transforms (for a
zero step the drag-start pose exactly), `apply_drag_surface_step()` runs
`Transform_tool::snap_anchor_to_surface()`. With Orient To Surface off and
Surface Rest `leading_face` (the default; U60) the lead - the first moved
target (the active node is moved to the front of the targets), the same
lead orient turns by - rests as "Surface placement" places a piece, by the
combined bounds of every mesh of its subtree taken into its own frame
through the local transforms, since a step writes the moved roots' world
matrices and not their descendants' (the face leading along the direction flush, the five lines and their fit test,
every moved mesh excluded): a level group's empty `Xform`, a Curve kit
layout node or an import root rests by its pieces, and a mesh rests by its
child meshes too. The whole moved set and the anchor move by the lead's
displacement (`find_lead_face_rest()`); a lead whose subtree holds no mesh
is not snapped. With Surface Rest `anchor`, and always with orient, the
anchor point itself is snapped:

- The snapped point is the gizmo anchor the user sees
  (`doc/editor/transform.md` "Key Types", `Transform_tool_settings`): the
  node origin, the average of the origins for several targets, in the BBox
  reference mode the center of the world bounding box, in the Reference
  mode the reference node's origin. The point under the pointer takes no
  part: a handle drag moves the anchor and the anchor is snapped. A kit
  shape's origin is at its base, so a piece's base comes to rest; a mesh
  whose origin is elsewhere (a box made with its origin at its center)
  sinks in by that offset, and in BBox mode the box center rests on the
  surface.
- One line runs through the anchor along the search direction,
  `find_point_surface()` (`level_queries.hpp`): the nearest surface hit
  (a face whose normal is against the direction) within Surface Max
  Distance either way, by the tie order of "Surface placement"; a point has
  no extent, so nothing is tested for fit and the footprint lines take no
  part. Every target and everything under it is excluded from the line, so
  the moved set stands neither on itself nor on its parts. A set without a
  mesh (lights, cameras, empty nodes) is not snapped.
- The search direction is the Surface Drag Direction setting ("Surface
  settings"): the explicit vector, (0, -1, 0) by default, or the camera ray
  from the viewport camera of the drag through the anchor's position for
  the step (before the snap; recomputed every step, so a moving camera is
  followed). An orthographic camera's ray is its view direction. A
  scripted drag (`drag_selection`) takes the last hovered viewport when it
  shows the active scene, else the first viewport that does. Without a
  camera, or with the anchor at the camera position, the step is not
  snapped.
- The moved set - the targets that are not locked against viewport
  transforms, not pulled through physics and have no moved ancestor (a
  target below another rides rigidly on it, `doc/editor/transform.md`
  "Rigid descendants") - moves by the anchor's displacement. With orient,
  the set then turns rigidly about the rested anchor by the shortest
  rotation that takes the up axis of the first moved target (local +Y, the
  scale's sign folded in) to the surface normal, after a half turn about
  its local X when that axis points along the direction
  (`get_orient_turn()`, as "Surface placement" turns a piece). Whether the
  half turn applies (`get_orient_half_turn()`) is decided once per drag,
  from the first moved target's drag-start pose and the direction through
  the anchor where the drag started, so a camera ray whose vertical sign
  flips at the camera's eye height does not flip the piece mid-drag. The
  step starts from the drag-start rotation, so tilt does not accumulate. A
  step that finds no surface keeps the tool's transforms.
- A rest that a moved target's channel locks would refuse (a locked
  translation component the move needs, or any locked rotation channel
  when orient turns) is not made: the step keeps the tool's transforms,
  so the node and the gizmo stay together.
- The gizmo anchor moves to the rested point and, with orient, turns with
  the set (the Local mode handle axes and the BBox mode origin indicator
  follow), so the handles stay with the selection.
- The step allocates nothing (scratch members and one per-thread hit
  buffer); the trace line "surface step: N entries, M snapped: T ms" (with
  "(refused by channel locks)" before the time when the locks refused it) of
  `editor.transform_tool` times it.

Rules:

- Axis rule: a pointer drag whose only free axis is within about 2.6
  degrees of the step's search direction, either sign (the Y arrow, with
  the default direction down; the X arrow with the direction (1, 0, 0)), is
  left as the tool computed it, so a piece lifts off a surface
  (`is_axis_along_direction()`). Axis arrows along other axes and the plane
  handles snap.
- Only translate drags snap to the surface: rotate and scale drags are left as computed
  (the scale tool has three write paths, and orient would fight a rotation).
- The drag is one undo entry, recorded at release from the snapped transforms;
  its undo restores the transforms from before the drag, rotation included.
- The gizmo reads the pair, the distance and the drag direction when the
  drag starts. The Transform window's numeric edits do not snap to the
  surface.

MCP calls take explicit arguments instead of the UI pair
(`doc/agents/mcp_api_guidelines.md`):

- `transform_selection {..., surface_snap, surface_orient,
  surface_max_distance, attach}` (default false, false, 1 m, true; `attach`
  as "Surface attachments" states, echoed in the reply): after all edits of the
  call, `snap_to_surface()` with those options - an edit, not a drag: each
  moved mesh rests by its own bounds as "Surface placement" places it,
  searching down; they need a translation of a node selection. The reply adds `surface_snap`, `surface_orient`,
  `surface_max_distance`, `surface_found_count` and per node `surface_found`.
- `drag_selection {..., surface_snap, surface_orient, surface_max_distance,
  surface_direction, surface_direction_mode, attach}` (`attach` true by
  default, "Surface attachments"; the others default false, false, 1 m,
  (0, -1, 0), `explicit_vector`; never the UI's settings; read when the drag
  starts; refused on a rotate or scale drag; a zero or non-finite direction
  and a mode other than `explicit_vector` or `camera_ray` are refused): every
  step of the translate drag snaps the anchor as a gizmo drag does, without
  the axis rule; `camera_ray` takes the camera of the last hovered viewport
  when it shows the active scene, else of the first viewport that does.
  The reply adds the options and per node `surface_found` (the moved
  targets of a snapped step) and `world_rotation_xyzw`.
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
  one, children of locked kit layout nodes included, except what the drag's
  flow moves (`Level_kits::collect_flow_moved()`, by the follow operation's
  rules: the floors under the moved walls' wall-floor joins, the walls a
  wall fit fits, and the followers of all of them and of the moved nodes by
  record direction, `mark_followers()`; with the flow mask's types) and the
  nodes below them, since a snap to one would meet a pose it leaves, and the
  one joined connector of each partner of a piece of the drag that does not
  move (a dragged record owner leaves its target, C3.14), which would hold
  the dragged piece where it was. A selected piece locked
  against viewport transforms is neither moved nor a target.
- Every piece's connectors are taken through its full world matrix, so
  scaled moved pieces, scaled targets and pieces under scaled or sheared
  parents take part (U22); the snap co-locates the transformed positions, by
  translation only unless Connector Snap Orientation turns the set
  ("Orientation" below, every drag kind). A piece whose frames degenerate takes no part.
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
  by the pair's offset; with Connector Snap Orientation Off the snap never
  changes the step's rotation or scale ("Orientation" below turns the set
  after the step).
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
- Orientation (U44, U50): Connector Snap Orientation is `off`, `rotate` or
  `full` (read when the drag starts, for every drag kind). Off: the snap
  translates only. Full: a snapped step also turns the moved set so the pair's
  frames face each other as a connector join does (`join_connectors()`,
  "Connectors" above: outward axes opposed, up axes agreeing after a quarter
  twist about the target's outward axis, no gap), overriding Orient To
  Surface. Rotate with Orient To Surface on: the step turns the moved set only
  about its up axis, so the surface keeps the axis it aligned with the normal
  and the snap sets the rotation about it. Rotate with Orient To Surface off
  is Full (the setting is read at the drag start, for rotate and scale drags
  too; a `drag_selection` takes its `surface_orient` argument).
  - The motion: the moved connector's world frame (its root's frame of the
    step, `transform_connector()`) and the target's give one rigid world
    motion, a turn about the moved connector followed by the step's offset
    (the full offset, or on an axis or plane handle with Connector Snap On
    Axis the constrained one); every moved root takes it, so a
    multi-selection or a group turns rigidly about that connector, each node
    keeping its stored scale (a mirror included) as a join writes it
    (`make_joined_local_transform()`), and the gizmo anchor with them.
  - Full's twist: of the join's four quarter twists the one whose turn is
    smallest (`closest_join_twist()`: the largest quaternion |w|; a later
    twist wins only by more than 1e-6, so equal ones take the smaller twist),
    so the piece does not spin about the pair's axis
    (`align_connector_frames()`, `level/kit_connector.hpp`).
  - Rotate's turn (`align_connector_frames_about_axis()`): the axis is the up
    axis of the lead (the first moved target, local +Y with the scale's sign
    folded in) in the step's pose - the surface normal when the step's snap to
    surface oriented the set, the lead's up axis as it stands otherwise (a
    step that found no surface, an axis drag along the search direction, a
    rotate or scale drag). Per twist, `best_turn_about_axis()` takes the angle
    about the axis that brings the moved frame's three axes nearest to the
    join frame's (the largest sum of their dot products, solved in closed
    form); a turn whose gain over no turn is at most 1e-3 is no turn, since
    where both outward axes lie nearly along the axis (a top-to-top pair) the
    gain shrinks as the tilt squared and its angle is noise. The twist whose
    turn faces best wins, then the smaller turn (the roll rule above), then
    the smaller twist (`choose_twist_about_axis()`); it is chosen when a pair
    first snaps in the drag and kept while that pair snaps (a different pair
    chooses again, the next drag starts fresh), so a normal that varies over
    the drag cannot flip it. Where both frames' up axes lie along the axis
    (two pieces oriented on one slope) the pair faces exactly and Rotate
    equals Full; where the target leans another way the pair faces as nearly
    as a turn about the axis allows (`facing_score` below 3).
  - Drag kinds. A free translate drag (and an axis or plane handle drag
    without Connector Snap On Axis): the turn, then the full offset. An axis
    or plane handle drag with Connector Snap On Axis: the turn, then only the
    offset's part along the axis or in the plane, so the translation stays
    constrained and only the orientation turns (`constraint` `axis` or
    `plane`, `alignment` `aligned`). A rotate drag: the turn comes after the
    step's rotation about the press pivot, so while snapped the pair faces
    (Full replaces the step's rotation, Rotate its part about the up axis).
    With Full the user's rotation chooses the pair and, by the roll rule, the
    twist; with Rotate it chooses the pair and keeps its tilt off the up axis,
    and the twist is the latched one above. Unsnapped the step is the user's
    rotation; to rotate a piece away from a snapped connector, hold the
    suspend key (Shift) during the drag. A scale drag: the turn comes after the step's
    scale, which the turn keeps.
  - Translation only, reported in `alignment`: a moved root whose locked
    rotate channel would hold back the turn or whose locked translate channel
    the motion moves (`locked`), a moved root whose resulting local transform
    would be sheared, under a parent with non-uniform scale or with a skew of
    its own (`shear`, `has_orthogonal_axes()`, the refusal a join under such a
    parent gives), and a pair frame that degenerated since the drag began, a
    target scaled to zero (`degenerate`). The step then snaps by the offset
    as with Off.
  - With Snap To Surface on as well, the connector step comes after the
    surface step: Full replaces the surface orientation of the snapped step,
    Rotate keeps the up axis the surface set.
  - Every step starts from its unsnapped pose, so the drag-start orientation
    (or the step's own rotation) comes back when the pair leaves the
    distance, the suspend key is down or Escape cancels; the release records
    the turned pose in the drag's one undo entry.
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
  `target_connector`, `alignment` (`off`, `aligned`, `locked`, `shear` or
  `degenerate`), `turn` (`none`, `about_axis` or `full`: the turn asked of the
  step) and `turn_axis` (the world axis of `about_axis`), `rotation_xyzw` and
  `motion_translation` (the rigid world motion of an aligned step, the
  rotation first; identity otherwise), `twist_steps` and, for an aligned step,
  `facing_score` (`get_facing_score()`: the sum of the dot products of the
  turned frame's axes with the join frame's, 3 when the pair faces exactly, so
  a Rotate turn that faced as well as its axis allows but poorly reads below 3).
- `drag_selection {..., connector_snap_orientation}` (`off`, the default,
  `rotate` or `full`, the former names `keep` and `align` accepted; never the
  UI setting): read when the drag starts, for a drag of any kind; anything
  but `off` is refused without `connector_snap_distance`. `rotate` turns
  about the up axis with `surface_orient` true (a rotate or scale drag takes
  `surface_orient` alone for this; `surface_snap` stays a translate drag
  argument), as `full` without it. The reply echoes it.
- `transform_selection {..., connector_snap_distance}`: one snap after the
  call's edits and snap to surface; it needs a translation, rotation or scale
  of a node selection. `connector_snap_orientation` turns the set as a drag
  does (`rotate` about the up axis with `surface_orient`, as `full` without
  it); it needs `connector_snap_distance` (refused otherwise). Without a
  translation `surface_orient` only names that axis (no snap to surface runs)
  and is refused unless the orientation is `rotate`; `surface_snap` still
  needs a translation.
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
  Connector, Moved Connector) are drawn in the chosen color. The pair of the
  Brush tool's joined kit mode preview ("Placing in the viewport"), the target
  connector and the preview's own (`piece_id` the preview node's), is drawn in
  the snapped color, also with Connectors Off.
- During a translate, rotate or scale drag with Connector Snap above 0, in
  the views of the drag's scene, whatever the setting: the moved set's
  connectors at the step's pose (turned and scaled with it), the target connectors within 4 snap
  distances of a moved one and, in its own color, the pair the step snapped
  by; the highlight ends with the drag.
- Colors and size: Settings > Debug Visualizations Style > Connectors
  (`connector_color`, `connector_wall_color`, `connector_wall_floor_color`,
  `connector_rail_color`, `connector_chosen_color`, `connector_moved_color`,
  `connector_target_color`, `connector_snapped_color`, `connector_size`,
  `connector_width`). A shown connector that accepts the `wall` type takes
  the wall color, one that accepts `wall-floor` the wall-floor color, one
  that accepts `rail` the rail color, the others the plain color (C1.8; `get_connector_indicator_color()`); the
  chosen, moved, target and snapped roles take their own colors.
- Cost: the enabled local frames are cached per piece and read again only after a
  change of its record or switches (parameters, replace, a switch edit, undo of each, a raw
  write) or of its parent (an entry keeps the parent it was read under, as no
  message reports a reparent); a removed piece's entry goes, and a scene close empties the cache; world matrices are read at draw time, a drag
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
  load finds them and adds none); its parent and its surface attachment record,
  naming the loaded support's uid ("Surface attachments");
- a static body with a collision shape `KHR_physics_rigid_bodies` rebuilt from
  the piece's own saved mesh (a convex hull or a triangle mesh) or, for a box,
  from the box collider of the synthesized child node the save wrote, folded
  back into the piece's body with the child removed
  (`doc/erhe/khr_physics_rigid_bodies_support.md`), one per piece;
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

Connections and floor layouts load as they were saved. A piece's or a floor
layout node's connections are its `Kit_piece.attachment` record, each end
named by its glTF uid; its connector switches are the
`Kit_piece.connectors_enabled` and `Kit_piece.disabled_connectors` values. A
floor layout is its Curve node: the outline is the curve's points, the
values the layout's pattern text, and its slab child is saved as its mesh
with its materials. After the load the connections are followed as before
the save: an outline edit moves the walls and the shared corners of
connected floors, a wall move makes the floor fit, and the slab is
navigation input.

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
- One collapsing section per kit, one row per shape: the shape's thumbnail
  ("Shape thumbnails") and its display name (the tooltip of both is its id and
  description); a click on either chooses the shape, and a drag from either
  places a piece in a viewport without choosing it ("Dragging a kit item").
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
  rise and tread depth (R3.5). Every change hands the Brush tool the kit brush
  of the new values, each step of a held drag included, so the viewport
  preview follows the drag live (R6.2) without an undo entry; Escape in a typed
  value gives the tool the values from before the edit. Values `validate()`
  refuses show the message in red and the tool keeps the brush it has. Under
  them one combo per material slot, `<slot> material` (ImGui id under the
  slot's name, so a row keeps its id whatever other rows show),
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
  piece's mesh has one primitive per slot. Below them, one row per
  connection of the piece ("Connections"), as the Properties window lists
  them, each with Remove. Snap Selected To Surface and
  Orient Selected To Surface run `Level_kits::snap_to_surface()` - the code
  path of `level_snap_to_surface` - over the selected meshes of the scene, one
  undo entry. Replace With (a combo of every shape, shown as
  `<display name> (<id>)`) and Replace Selected: the press queues
  `Level_kits::replace_shape()` ("Replace") for the selected nodes of the
  scene with the Snap To Surface and Orient To Surface settings and shows the
  result (`make_replace_summary()`: the replaced count, how many snapped, how
  many connections were dropped, and each skipped node by name with its
  reason) or the refusal under the
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
  drawn highlighted ("Connector indicators"). Detach Selected queues
  `Level_kits::detach_pieces()` for the selected kit pieces and floor
  layouts of the scene ("Connections"), one undo entry, and shows the result
  as a `Level Kit:` line; it is disabled while none is selected (counted
  again on the selection and active item messages).
- Layout (R5.12.9, `Kit_layout_initial_editor` in `kit_layout_editor.hpp`):
  the initial values of a kit layout node the author adds to the scene. They
  edit no node: a selected layout node's values are edited in the Properties
  window ("Kit layout rows"). The rows: the pattern rows of the shown kind
  ("Kit layout rows", every kind offered: Line, Grid, Ring, Scatter, Path,
  Segments and Stairs, each kind keeping its values; a stairs layout's
  values ride the pattern text), Spacing From Item (on by default: the new
  layout's line step and grid spacings are its item's extent, so Step,
  Spacing X and Spacing Z show disabled), Module Snap, Surface Snap, Surface
  Direction ((0, -1, 0) by default), Surface Forward Distance (1 m by
  default), Surface Backward Distance (0 m by default), Surface Support
  (Five Lines by default), Surface Orient, Per
  Piece Seed, Layout Seed,
  for a path or segments kind the seven connector switches (off by default)
  and Locked (on by default). They are the
  editor setting `level_kit` (`Level_kit_config` version 10, the pattern as
  its canonical text in `layout_pattern`, the switches as
  `layout_hide_<all|plus_tangent|...>_connectors`, the search as
  `layout_surface_direction`, `layout_surface_forward_distance` and
  `layout_surface_backward_distance`, the support rule as
  `layout_surface_support` (a file before version 10 reads `five_lines`); a version 6 file's
  `layout_random_seeds` reads as `layout_per_piece_seed`, a version 6 - 8
  file's `layout_surface_max_distance` as both `layout_surface_forward_distance`
  and `layout_surface_backward_distance`; a zero or non-finite Surface
  Direction is refused, keeping the stored one, and logged), read once and written at each
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

### Shape thumbnails

`Kit_thumbnails` (`kit_thumbnails.hpp`, owned by `Level_kits`,
`get_thumbnails()`) holds one thumbnail per library shape (R4.5): the shape at
its defaults at the window scene's lengths (`make_default_values()`), each
slot in its default palette material, seen in a three-quarter view (the
piece turned -35 degrees about y). The images are the layers of one 2D array
texture with mipmaps, made at the first render, one layer per shape in
library order (a single-layer view per layer on devices with texture views,
as `Thumbnails`); its side is the editor setting `thumbnails.size_pixels`.
A row shows its thumbnail as an image button three text lines high, an empty
button until its first render; the button shows hover and press feedback and
a click on it chooses the shape as the row does.

- A row drawn for the first time queues its shape: `queue()` captures the
  build input by value on the main thread (the default
  `Kit_parameter_values`, their mesh key, a `Build_info` of fill triangles
  only with the interactive `Mesh_memory` buffer info, the shape's normal
  style) and hands it to `Kit_thumbnail_builds`, the pending list (most
  recently requested first; a shape queued again replaces its pending
  request) shared by `Kit_thumbnails` and the build tasks in flight
  (`shared_ptr`, as `Brush_geometry_queue_state`). Each request starts a
  task on the background executor while fewer than its `num_workers()` are
  in flight; a task takes requests until the list is empty.
- A build task makes the part geometries (`make_kit_part_geometries()`, or
  `make_kit_geometry()` for a one-slot shape) and one
  `erhe::primitive::Primitive` per part with its renderable mesh, holding a
  `Scoped_worker_context` around the renderable mesh build only. It makes
  no brush, no collision shape and no ray-trace BVH, takes no scene lock and
  reads nothing but its input and the kit library, which is fixed for the
  session. It spawns nothing. Taking a request it posts a start notice
  (shape, mesh key); finishing it posts the result (shape, mesh key, part
  primitives or the problem).
- `update()`, run in the prepare phase beside `Thumbnails::update()` on
  rendered frames, takes the notices and the results under the list's mutex
  (swapped into scratch vectors, so a frame with nothing finished allocates
  nothing). A notice turns a `queued` shape whose latest request has its
  mesh key into `building`. Each result is applied inside the
  `kit_thumbnails` `Background_apply_meter` scope: a result whose shape is
  `queued` or `building` with that latest mesh key is rendered by
  `Brush_preview::render_parts_preview()` into the shape's layer, in the
  slots' materials resolved here on the main thread; any other result (its
  lengths superseded, or a second build of an image already rendered) is
  released unrendered. Every result taken is applied on that frame and its
  primitives released there. A thumbnail is rendered once and kept.
- Upload before draw: the task's renderable mesh build enqueues its vertex
  and index uploads on the interactive `Mesh_memory` queue before it posts
  the result, and `Brush_preview::render_primitives()` calls
  `Mesh_memory::flush()` into the same command buffer before it records the
  draw, so the uploads are recorded ahead of the draw on the frame that
  renders it. `update()` runs before the frame's own flush and
  `begin_draw_prepare()`, so the main-thread build of a device without
  worker contexts is not a late upload either.
- Only the main thread writes the per-shape state (`Kit_thumbnail` and the
  latest requested mesh key); the tasks write only the list's pending,
  notice and result vectors and its counts, under its mutex.
- On a device without worker contexts (OpenGL without a window) no task is
  started: `update()` builds the most recent pending request on the main
  thread, one shape per frame, as `Geometry_graph_window` falls back.
- The `kit_thumbnails` owner of `get_async_status` `background` reports the
  pending requests (`queued`), the tasks in flight (`running`) and the
  results not yet taken (`ready`).
- Shutdown calls `stop()` before the executors drain: further requests are
  refused and the pending requests, the notices and the finished results
  are dropped on the main thread; a task in flight releases the result it
  finishes on its worker, before the drain returns.
- The thumbnails' materials are their own palette materials
  (`make_palette_material()`), listed in the preview scene's content library
  (`Brush_preview::add_material()`).
- Before its rows the window calls `follow_scene()`; it reads the scene's
  lengths again only when the window's scene is another one or after a
  lengths change (`Level_kits::on_level_lengths_changed()`, which the
  window's Level Settings rows and MCP `set_scene_settings` call). When the
  lengths differ from those of the thumbnails, each rendered shape whose
  default mesh key (`Level_kits::get_mesh_key()`) the new lengths change is
  queued again and shows its former image until its new one renders; the
  others keep theirs. A `queued` or `building` shape whose latest request
  has other lengths is requested again, and the build of the older request
  is released unrendered when it arrives.
- A graphics preset change (`Graphics_settings_message`) whose preset key
  differs (`make_thumbnail_preset_key()`, the preset without its LOD fields,
  as `Thumbnails` compares it) queues every rendered (`current` or `failed`)
  shape again. A `queued` or `building` shape is not requested again: its
  build is rendered after the change, with the new preset.
- A shape whose defaults make no geometry is `failed` (a warning names it)
  and shows the empty button.

MCP `level_get_kit_thumbnails` reports them ("MCP").

### Placing in the viewport

In kit mode the Brush tool keeps its hover, preview and insert machinery and
takes the piece's transform from `make_placement_pose()` ("Surface
placement"): the point is the hovered point on content, else the grid-snapped
point on the grid, and the normal is the hover's normal there (the hit
facet's world normal on content, the grid's normal on the grid), turned to
face the viewer when the hit is a back face (a double-sided plane seen from
behind); the rotation steps count the tool's rotate commands and start at
zero when another shape is chosen. Place As Layout gives the new layout the
placement's search direction as its `surface_direction`, so a layout placed
on a wall or a ceiling searches into it. The preview mesh carries one
primitive per slot, each with a material of the color of the palette material
the window chose for that slot (`Level_kits::get_preview_material()`, outside
every content library), and sits at exactly that pose; it is no surface
for the rays, while placed pieces are, so a piece placed over a placed block
rests on it. A click places one piece at that pose through `Level_kits::place()`
(without a further snap): its record, the chosen palette material of each slot
and its static body, one undo entry. The tool keeps the selection, as its face-aligned insert does, and
stays in kit mode for the next piece.

Connector join (R5.3, step 40): when the pointer hovers a kit piece and one of
its enabled connectors ("Connector switches": a switched-off or
layout-hidden connector is never a target) lies within Connector Snap
(`connector_snap_distance`, "Connector snap"; 0 turns the join off) of the
hovered point, the preview shows the new piece joined there instead of the
surface placement.

- Target: the hovered piece's free enabled connector nearest the hovered
  point, the first in the shape's order on a tie. A connector is not free
  when another kit piece's enabled connector sits on it (within 1 mm) facing
  it (outward axes opposed within a dot of -0.999): a join made already, so
  after a wall is placed at another's end the seam offers neither, and the
  new wall's far end is the next target. The hovered mesh is the piece's own
  node; a hover of the grid or of a node that is not a kit piece joins
  nothing.
- New piece's connector: the one of its shape whose outward axis opposes the
  target's most, both read in their pieces' own frames, the first in the
  shape's order on a tie: a wall's `end` takes a new wall's `start`, a top
  takes a `base`, a stairs `top` a stairs `bottom`, `front` a `back`.
- Values (K9.2): the preview takes the values the target supplies over the
  join, as the placement's flow gives them (`Level_kits::get_join_supplied_values()`:
  for a wall on a floor edge the edge's length minus the floor's wall
  thickness, its wall thickness, room height and wall material; a flow mask
  without the join's types, a locked layout child as target and Place As
  Layout take none); the preview mesh shows the brush of those values with
  the preview materials of the supplied names, and the join pose uses its
  connector at those values. The values are read again when the pair, the
  kit values or the target change (a move, an outline or value edit of a
  floor, a parameter change of a piece, `Mesh_materials_changed_message`
  of its material assignment). A click places the kit values; the
  placement's flow then gives the piece the shown values.
- Pose: the join of `Kit_placement::connect` (`join_connectors()`, no gap):
  the frames face, whatever Connector Snap Orientation says (that setting
  is the Transform tool's: the join defines the whole pose); module snap and
  the surface options do not apply. The rotate commands pick the twist: the
  quarter turn nearest the rotation steps' yaw angle (steps times Rotation
  Step) is the join's twist about the target connector's outward axis.
- Click: the placement carries that `connect`, so `Level_kits::place()`
  resolves the same join (one undo entry, the same transform as
  `level_place_pieces` with that `connect`); a target removed since the hover
  is refused by `place()`. Place As Layout puts the new layout at the joined
  pose without a join, with the default search direction (0, -1, 0) as its
  `surface_direction` (a joined pose has no surface under it).
- Suspend: while a key of `Transform_tool.suspend_connector_snap` (Shift by
  default, U27) is held the preview places as without a join; the preview
  follows the key at its edge, with the pointer still.
- Change: without pointer motion, the preview keeps the same pair at the
  target's new frame when the target or a node above it moves
  (`Node_touched_message`: a transform operation, its undo and redo, MCP
  `set_node_transform`); when a piece's connector switches change
  (`Kit_piece_connectors_changed_message`) or the target is removed, the pair
  is kept while its target connector is still enabled, and the preview
  places as without it otherwise. A parameter change of the target
  (`Mesh_geometry_changed_message`) reads the join again at once.
- Indicator: the pair is drawn in the snapped color while the preview is
  joined, the target connector and the preview's own, in every view of the
  scene and whatever the Connectors setting ("Connector indicators").
- Cost: the hovered piece's enabled local frames come from the connector
  indicators' per-piece cache, and the new shape's frames are read once per
  chosen shape. A hover of a cached piece allocates nothing; a cache miss
  (the first hover of a piece, or after a change of its record, switches or
  parent) reads the piece and inserts its entry. The free test visits the
  scene's kit pieces and reads the connectors of those whose world bounds
  hold the candidate.

### Dragging a kit item

A shape row of the Level Kit window - its thumbnail or its name - is an ImGui
drag source of a kit item (`Kit_item`, `kit_item.hpp`): a shape's parameter
values (with their derive mask and seed) and one palette material name per
slot, what a kit mode click places. The row of the shape the Brush tool places
carries the window's parameter rows and slot material choices; any other row
carries what a click on it would choose, the shape's defaults at the scene's
lengths and its slots' default materials. A row whose values `validate()`
refuses starts no drag. The drag's tooltip shows the thumbnail and the display
name. Starting a drag chooses nothing and leaves the Brush tool as it is.

The payload (type `c_kit_item_payload_type`, `kit_item_payload.hpp`) is a
`Kit_item_payload` holding a token; `Level_kits` holds the item itself
(`begin_kit_item_drag()` when the drag starts), a target reads it with
`Level_kits::find_dragged_kit_item(payload)`, and the window releases it
(`end_kit_item_drag()`) on the first frame without a payload of the type.

The targets are a viewport (below) and the Inventory window's grid and
hotbar slots ("Kit slots").

Over a viewport the Brush tool previews the item exactly as kit mode would
place it ("Placing in the viewport": the pose, module snap, the surface
options, the connector join with its supplied values, the suspend key), with
rotation steps 0, whether or not the tool is active or in kit mode. The
viewport window queues the tool's calls (`Imgui_windows::queue()`): the
preview begins when the drag enters the viewport and ends when it leaves it or
the drag ends elsewhere, without placing anything. A release over the viewport
places one piece at the shown pose in that viewport's scene, as a kit mode
click does with the item's values and slot materials: one undo entry, the
join included when the preview is joined (with Place As Layout, a kit layout
node, as the click makes). After the drag - dropped or not - the active tool,
the tool's own kit mode shape, values, rotation steps and materials, the
window's rows and the selection are as before it; New Seed Per Piece advances
no seed on a drop.

`level_get_placement_preview` reports the dragged item's preview under
`kit_item_drag` ("MCP"); the verify script drives a real drag
(`mouse_drag` with `hold` from the row into the viewport, then
`mouse_release`; `level_tools_verify.py --only-kit-drag`).

### Kit slots

An Inventory window slot, of the grid or of the hotbar row, can hold a kit
item: `Slot_entry::kit_item` (`tools/hotbar.hpp`), a kit slot when it has a
shape (`is_kit_slot()`). A kit slot's tool is the Brush tool; it holds no
brush or material reference, command or graph node, and its kit brush is no
asset: `Level_kits::get_brush()` makes it from the values at activation. A
slot holds only valid items: every way in validates.

- A kit item dropped from the Level Kit window ("Dragging a kit item") on a
  slot replaces the slot's content with a copy of the item as it was when the
  drag started; later edits of the window's rows leave the slot as it is.
  `Slot_entry::clear()` resets every kind's content, and every accepted drop
  starts from it.
- A kit slot dragged onto another slot swaps with it as the other kinds do:
  the `Slot_entry` travels whole, the kit item with it. A drag from the tools
  palette copies its tool; only palette entries are rebuilt from the drag
  payload. A right click clears a kit slot.
- A kit slot shows its shape's thumbnail ("Shape thumbnails";
  `Kit_thumbnails::prepare_slot_image()` follows the last hovered viewport's
  scene as the window's rows do and queues the render), and until the image
  exists the display name in the Inventory window and the Brush tool icon in
  the hotbar. The tooltip names the shape (display name and id), its
  parameter values and its slot materials; a slot drag's tooltip shows the
  display name. MCP addresses the Inventory window's slot items as
  `Inventory grid slot N` and `Inventory hotbar slot N` (`get_imgui_items`).
- Activating a kit slot - a click on it in the Inventory window (a release
  that ended no drag), or selecting it in the hotbar by a click, a number key
  or `Hotbar.previous_tool` / `next_tool` - calls
  `Level_kits::activate_kit_item()`. The next prepare phase makes the Brush
  tool the active tool and hands it the item's kit brush, values and slot
  materials (`Brush_tool::set_kit_brush()`): the state choosing that shape in
  the Level Kit window and setting those values gives. The values are used as
  stored, not resolved again against the scene's lengths. The tool sends
  `Kit_values_changed_message` with `Kit_values_change::kit_item`, and the
  window's rows take the tool's shape, values and slot materials. Selecting a
  brush slot afterwards leaves kit mode as choosing a palette brush does.
  With New Seed Per Piece the tool takes the next seed after each placed
  piece; the slot's item stays as it is.
- The settings save a kit slot in `Inventory_slot` (version 2) as
  `kit_shape` (the shape id), `kit_parameters` (the canonical parameter
  text, `Kit_parameter_values::to_canonical_text()`) and `kit_materials` (one
  palette name per slot) through `write_kit_item_slot()`; the Inventory window
  reads them back once the tools are registered with `read_kit_item_slot()`
  (`level/kit_item_slot.hpp`), which reads the shape and the text as a piece
  record does (`read_kit_piece_record()`, "Former shape ids"). A shape id the
  library does not know, a text that does not read or validate, more names
  than the shape has slots or a name not in the palette leaves the slot
  vacant with one warning line naming the slot and the reason. A slot stored
  with a former id is saved again with the shape's id.

MCP: `set_inventory_slot` with `kit_item`, `get_inventory_slots` and
`activate_inventory_slot` ("MCP"); `level_tools_verify.py --only-kit-slots`
("Verification").

## MCP

- `level_get_kits {scene_name}`: the kits and, per shape, its id, `former_ids`
  ("Former shape ids"), kind,
  description, tags, parameters (type, unit, range, the default resolved
  against the scene's level settings, its scale and factor, an enum's values;
  `derive` `fixed` or `derivable`, `default_derived`, and for a derivable float
  or int `derive_min` and `derive_max`, a float's resolved to meters with its
  `derive_step`), `default_derive` (the default mask's names),
  `detail_counts` (`parameter`, `dimensions`, `step`;
  `doc/editor/level_kit_shapes.md` "Level of detail"), `default_size` and `default_bounds` of the mesh at the defaults (default
  mask, default seed), `material_slots` (in slot order: `slot`,
  `default_material` and `alternatives`, palette names), `connectors` (at the
  defaults, in the piece's local space: `name`, `position`, `outward`, `up`),
  `light_frames` (at the defaults: `name`, `position`, `direction` and the
  starting values, "Lights on pieces"), normal style, triangle budget, `collision` (the kind at the defaults) and
  `collision_by_values` (the shape states `get_collision`), `seeded` (the generator draws
  from the seed) and `mirror_planes` ("Groups"); the scene's level lengths and the `palette` (`name`,
  `base_color`, `roughness`, `metallic`, `emissive`).
- `level_get_kit_thumbnails {request_all, scene_name}`: the Level Kit
  window's shape thumbnails ("Shape thumbnails"). `request_all: true` queues
  every shape in state `none`, as if the window drew every row
  (`Kit_thumbnails::request_all()`); until the window has drawn its shape
  rows the thumbnails have no lengths, so the call names a scene with
  `scene_name`, whose lengths the thumbnails then follow (`follow_scene()`)
  until the window draws with another scene. Returns `lengths` (`module`,
  `wall_height`; null before the window first drew its rows or a
  `request_all` named a scene), `size_pixels`, `shape_count`, the counts per
  state (`none_count`, `queued_count`, `building_count`, `current_count`,
  `failed_count`), the
  total `render_count`, `requested_count` (the shapes `request_all` queued,
  0 without it), and per shape in library order `id`, `state` (`none`:
  its row not drawn yet, `queued`: waiting for a build task, `building`: a
  task has taken it or its result waits for `update()`, `current`: rendered
  at `lengths`, `failed`), `has_image`, `render_count` and `mesh_key` (of
  the defaults its image shows). A script waits for the thumbnails by
  reading this until no shape is `none`, `queued` or `building`.
- `level_place_pieces {scene_name, snap, surface_snap, surface_orient,
  surface_direction, surface_max_distance, surface_rest, attach, pieces: [{shape, parameters, position, rotation_xyzw,
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
  `brush_id`, `mesh_key` and `support` (`{id, name}` of the support the piece
  was inserted under with `attach`, true by default; null when none, "Surface
  attachments");
  pieces of equal `brush_id` share one mesh. `flow_mask` and `auto_connect`
  at the top level override the settings for the call ("Auto-connect and the
  connection settings"), and the reply carries `flow` ("Flow report").
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
  snap, surface_snap, surface_orient, surface_direction, surface_forward_distance,
  surface_backward_distance, surface_support, per_piece_seed, seed, connector_hiding, locked}` (`random_seeds`, the former name of `per_piece_seed`, still reads;
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
  boolean (true hides); an unknown key or a non-boolean is refused.
  `surface_support` is `five_lines`, `center_blocks` or `center_fallback`
  (U52); any other value is refused. The reply: the node's `id`, `name`, `type` (`Curve` for a path
  layout, else `Xform`), world `position` and `rotation_xyzw`, `curve` (a
  path layout's own curve: `point_count`, `closed` and `length`; null for an
  Xform), `locked`, `pattern`, `pattern_text`, `items` (`shape`,
  `parameters`, `parameters_text`, `materials`, `weight`, `extent`,
  `bounds` (the item's mesh box in its own frame, from its CPU geometry),
  `footprint_radius`), the snap and seed
  values (`surface_support` by name), `connector_hiding` (all seven), `run {count, requested, step, space, item_extent}` (or
  `{problem}`; `requested` is the count the values ask for, more than `count`
  when a scatter's spacing lets fewer in),
  `child_count` and
  `children` (`id`, `name`, world `position` and `rotation_xyzw`, `shape`,
  `parameters`, `parameters_text`, `materials`), and `changed`,
  `kept_count`, `added_count` and `removed_count`.
- `level_set_layout {scene_name, layout_id, pattern, items, snap,
  surface_snap, surface_orient, surface_direction, surface_forward_distance,
  surface_backward_distance, surface_support, per_piece_seed, seed, connector_hiding}` (`random_seeds` as above): `set_layout()`; the
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
  surface_max_distance, attach}` (`attach` true by default: the re-snapped
  pieces attach as "Surface attachments" states): `replace_shape()` ("Replace") for the listed
  nodes, else the selected nodes of the scene; `parameters` merge over each
  piece's carried values as `level_set_piece_parameters` merges them over a
  piece's values, `materials` read as `level_place_pieces` reads them, and
  the surface keys as `level_place_pieces` reads its call-wide ones
  (default off). One undo entry, none when nothing is replaced. The reply:
  `replaced_count`, `snapped_count`, `dropped_connection_count`,
  `skipped_count`, `skipped` (per skipped
  node `id`, `name`, `reason` - `kit_layout_node`, `locked_layout_child`,
  `group`, `not_a_kit_piece`, `unreadable_record` or `loaded_mesh` - and
  `message`) and per replaced or unchanged piece `id`, `name`, `old_shape`,
  `shape`, `replaced`, the effective `parameters`, the parameter names
  `carried`, `clamped`, `defaulted` and `given`, `materials`,
  `kept_materials` (the slots that kept the same-named slot's material),
  `world_aabb`, `collision_shape`, `snapped`, `dropped_connections` (per
  connection Replace removed: `connector`, `other_id`, `other_name`,
  `other_connector`, `reason`; "Replace") and, for a replaced piece of a
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
  to_connector, twist_steps, gap, attach, flow_mask, auto_connect}`:
  `connect_piece()`, `attach` (default true) recording the attachment
  ("Attachments"), `flow_mask` and `auto_connect` overriding the settings for
  the call ("Auto-connect and the connection settings"); the reply gives the
  new world `position` and `rotation_xyzw`, `changed`, `world_aabb`, the
  piece's world `connectors` (each with `enabled`), `attachment`,
  `not_attached` and `flow` ("Flow report").
- `level_get_flow_report {}`: "Flow report".
- `level_get_flow_preview {}`: "Flow preview".
- `level_detach_piece {scene_name, node_id}`: `detach_piece()`, one undo
  entry, connections and the surface attachment; the reply gives `changed`.
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
  `level_get_region_handles {}`: "Layout handles".
- `level_snap_to_surface {scene_name, node_ids, orient, direction,
  max_distance, query_only, attach}` (`max_distance` 1 m by default, `attach`
  true): `Level_kits::snap_to_surface()`, which runs
  `find_surface_placement()` with snap, and orient when asked, for any mesh
  nodes, the listed nodes excluded from the hits. The nodes that found a
  place move by one `Node_transform_operation` each, all in one undo entry
  (none when no node moves or attaches, "Surface attachments"); `query_only`
  moves nothing. The
  reply: `moved_count` and per node `id`, `name`, the resting world
  `position` and `rotation_xyzw`, `distance`, `surface_found` (false when
  no place lies within `max_distance`, "Surface placement") and `support`
  (`{id, name}` of the mesh node it rests on, null without a place) and
  `attached_to` (its support after the call, null when it is not attached);
  the reply also echoes `max_distance` and `attach`.
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
  arguments, `piece_count`, `excluded_count` (the pieces left out by
  `exclude_tags` or the foliage flag), `agent`,
  `navigation_available`, `navigation_stale`, `island_count`,
  `finding_count`, `counts` per kind and `findings`, each with `kind`,
  `position`, `ids`, `names` and `detail`, and `distance` (unsupported, null
  without a surface), `depth` and `measure` (overlap) or `area` and `island`
  (island).
- `level_get_placement_preview {}` (read-only): `active` (kit mode),
  `tool_active` (the Brush tool is the priority tool), `rotation_step_degrees`,
  `yaw_steps`, `hover_position`, `hover_normal` (the normal the preview
  searches against), `preview_position` (the world origin of the shown
  preview, kit mode or not) and `visible`; in kit mode also `shape`,
  `parameters`, `mesh_key`, `brush_id`, the brush's own `bounds` and
  `materials` (slot name to the palette material of the next piece); with a
  visible preview also its `position`, `rotation_xyzw`, `surface_found` and
  `world_aabb`, the transform a click places the next piece at. In and
  outside kit mode: `connector_snap_distance` (the reach of the connector
  join), `connector_join_suspended` (the suspend key was held at the last
  preview update) and `connector_join`: null, or the join the preview shows
  (`target_piece_id`, `target_connector`, `connector` of the new piece,
  `twist_steps`, `distance` from the hover point to the target connector,
  `flowed`, and with it the supplied `parameters` and `materials` the
  preview shows, "Placing in the viewport");
  `level_place_pieces` with that `connect` places the same pose.
  `kit_item_drag`: null, or while a kit item dragged from the Level Kit window
  is over a viewport ("Dragging a kit item") `active`, the item's `shape`,
  `parameters`, `mesh_key`, `brush_id`, `bounds`, `materials` and `visible`,
  with a visible preview its `position`, `rotation_xyzw`, `surface_found` and
  `world_aabb`; the top-level pose fields are then absent and `visible` false
  (kit mode's own preview is not shown), while `connector_join` and
  `hover_position` are the drag's.
- `get_surface_snap_settings {}` and `set_surface_snap_settings {snap, orient, attach, ...}`:
  the shared settings ("Surface settings").
- `set_inventory_slot {section, slot_index, kit_item, scene_name}` ("Kit
  slots"): `section` `grid` (the default) or `hotbar`; `kit_item` `{shape,
  parameters, materials}` as `level_place_pieces` takes a piece, with
  `parameters` either the canonical text, used as given, or an object over
  the shape's defaults at the lengths of `scene_name`, derived and validated;
  `materials` names palette materials by slot, a slot not named taking its
  default. The slot becomes a kit slot of the item; the reply echoes it. The
  tool's other forms (`item_id`, `tool`, `clear`, which also clears a kit
  slot) are in `config/editor/mcp_tools.json`.
- `get_inventory_slots {}`: per section (`grid`, `hotbar`), per slot its
  `kind` (`kit` among the others), `tool` and `kit_item` (`shape`,
  `display_name`, `parameter_text`, `parameters`, `materials`), and for a kit
  slot `config`, the `kit_shape`, `kit_parameters` and `kit_materials` the
  settings save.
- `activate_inventory_slot {section, slot_index}`: a hotbar slot as its number
  key selects it (`Hotbar::activate_slot()`), a grid kit slot as a click on
  it; a grid slot of another kind is refused.
- `level_get_pieces {scene_name}`: every node carrying `Kit_piece`, in node id
  order: its record (`parameters` with `derive`, and `parameters_text`), `mesh_key`, world
  position and rotation, `world_aabb`, `materials` (slot name to material
  name; a primitive beyond the shape's slots as `primitive <index>`),
  `brush_id`, `shares_brush_primitive` (each mesh primitive is its brush's
  part of that slot),
  `follows_kit` and `problem` ("Persistence"), `collision` (the kind at its
  values) and `physics` (motion mode, collision shape, `box`: a box body's box
  in body space, the piece's local space at the node's world scale, and
  whether the body exists); straight stairs also `stairs` (`step_count`, `step_rise`, `tread_depth`, `run`,
  `slope_degrees`: the layout their values produce, R3.5); `light_frames` (the
  shape's at the piece's values) and `lights` (its kit light nodes: `id`,
  `name`, `frame`, local `position` and `direction`, and the light's own
  values, "Lights on pieces"); `uid`, `attachments` (one status per
  connection the piece records, "Connections"), `attachment` (the first
  of them, null for none) and `surface_attachment` ("Surface attachments",
  null for none); `pass_through` and `foliage` (the authored flags,
  "Surface placement"); `foliage_radius` (`Kit_piece.foliage_radius`,
  "Foliage paint").
- `level_get_foliage {scene_name, center?, radius?}`: the scene's foliage
  items from its foliage index ("Foliage paint"), after
  `Scene::update_node_transforms()`: without a sphere every item, with
  `center` and `radius` (given together) the items within it. `count`,
  `items` in id order (`id`, `name`, `shape` - the `Kit_piece.shape`,
  empty for a mesh that is no kit piece -, `position` the indexed world
  translation, `radius`, `parent` the parent node's id or null at the
  root), `max_radius` (the largest radius the index holds) and `density`
  (the count within the sphere over the area of its great circle, items
  per square meter; null without a sphere and for a sphere of radius 0).
  An unknown argument fails the
  call.
- `level_foliage_paint {scene_name, stamps, palette, radius?, density?,
  seed?, align_to_surface?}`: one paint stroke ("Foliage paint"): `stamps`
  a list of `center` and `normal` (world space, the normal not zero),
  `radius` the brush radius (default 2 m), `density` the Paint Density
  (default 0.5 items per square meter), `seed` (default 1),
  `align_to_surface` (default false) and `palette` the entries as objects
  (`shape`; `parameters` an object as `level_place_pieces` takes it or the
  canonical text; `materials` a material name per slot, empty for the
  slot default; `weight` default 1; `radius` default the shape's foliage
  radius, required for a shape without one; `yaw_min`, `yaw_max` default
  0 - 360; `variants` default 8; `seed` default 0). No tool setting is
  read or written. A refusal of the stroke's begin fails the call and
  names its reason; a refused stamp cancels the stroke, so nothing is
  painted and nothing recorded. The reply holds `stamps`, `attempts`,
  `placed`, `refused_by_spacing`, `without_surface` and `on_sealed_support`
  (candidates),
  `recorded` (the stroke changed the scene and is in the undo history, as
  its own entry or, inside a batch, as a part of the batch's entry) and `items` in placement
  order (`id`, `name`, `shape`, world `position` and `rotation_xyzw`,
  `radius`, `parent` the parent node's id or null at the root, `attached`
  whether it holds a surface attachment record, `mesh_key`). An unknown
  argument fails the call.
- `level_foliage_remove {scene_name, stamps, radius?, density?, seed?,
  only_shapes?}`: one remove stroke ("Foliage paint"): `stamps` a list of
  `center` (world space) and an optional `normal`, accepted in the form of
  a paint stamp and not read; `radius` the brush radius (default 2 m),
  `density` the Remove Density (default 1), `seed` (default 1) and
  `only_shapes` shape ids for Remove Only Palette Shapes (empty or absent:
  every foliage item). No tool setting is read or written. A brush radius
  that is not a finite number above 0, a density outside 0 - 1, an unknown
  shape id and a center that is not three numbers fail the call, naming the
  reason. A center beyond the float range is not finite: the stroke refuses
  that stamp and is cancelled, so nothing is removed, nothing recorded and
  the selection is as before. The reply holds `stamps`, `removed` (the
  foliage items removed, each with its subtree), `recorded` (as
  `level_foliage_paint` reports it) and `removed_ids`, the removed items'
  node ids in removal order. An unknown argument fails the call.
- `level_get_foliage_paint_settings {}`: the Foliage Paint tool's settings
  ("Foliage paint"): `brush_radius`, `paint_density`, `remove_density`,
  `align_to_surface`, `remove_only_palette_shapes`, `seed` (the next
  stroke's), `palette` (the entries as `level_foliage_paint` takes them,
  `parameters` as the canonical text), `palette_text` (the saved text) and
  `paint_refusal` (why a paint stroke cannot begin, null when one can).
  Runs while a stroke is open.
- `level_set_foliage_paint_settings {brush_radius?, paint_density?,
  remove_density?, align_to_surface?, remove_only_palette_shapes?, seed?,
  palette?}`: writes the given settings as the tool's widgets do and
  replies as the read. Every argument is checked before anything changes:
  an unknown argument, `brush_radius` outside 0.1 - 50, `paint_density`
  outside 0.01 - 100, `remove_density` outside 0 - 1, a `seed` that is no
  integer 0 - 1000000 and a palette entry that does not read or check fail
  the call. A `palette` replaces the whole palette (an empty list clears
  it); an entry's `parameters` object reads at the default level lengths
  (module 1 m, wall height 3 m), the canonical text gives exact values.
- `level_get_foliage_brush {include_stamp_centers?}`: `tool_active`, `has_brush`, `scene_name`,
  `center` and `normal` (null without a brush), `radius` (the setting),
  `seed`, `stroke_open`, `stroke` (the open stroke: `kind`, `scene_name` and
  its counts so far - `stamps`, `attempts`, `placed`, `removed`,
  `refused_by_spacing`, `without_surface`, `on_sealed_support`, `recorded`;
  null when none is open), `last_stroke` (the last stroke the tool began or
  tried: `kind`, `outcome` ended | cancelled | abandoned | refused,
  `scene_name`, `seed`, `refusal` and its counts; null before the first).
  With `include_stamp_centers: true` (default false) it also returns
  `stamp_centers` (the first 1024 stamp centers of the open stroke, else of
  the last one that began, in stamp order; the tool records them into a
  fixed array and stops recording when it is full) and
  `stamp_centers_truncated` (the stroke stamped more than 1024). Runs while
  a stroke is open; the checks that drive the mouse wait on it.

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
  Pieces carrying a tag of `exclude_tags` or the foliage flag ("Surface
  placement") take no part in the support and overlap parts, so ground
  pieces, which rest on nothing, and a rock sunk into the ground on purpose
  can be left out. Without a navigation mesh no island
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
  sweep of derived values at eight seeds. Every rock form passes the mesh
  checks at the corners of its ranges over seeds (`Kit_nature_shapes`). The more shapes: an open
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
  rock) follows those bits. Each batch's rows (`kit_shape_rows_<batch>.cpp`:
  its shape ids, seeded and enum shapes, component counts, walkability
  exceptions, triangle counts, worst cases and pins) and helpers
  (`kit_shape_test_support.hpp`) are shared by the test sources; a pin
  mismatch prints every actual row in that syntax. `Kit_former_ids`: every
  former id resolves to its shape and names no other, a piece record of
  either id reads through `read_kit_piece_record()` and generates, and a record of parameters the shape lacks
  (the rock's `bevels`, the tree's `canopy`) or without one it has does not
  read. `Kit_basic_shapes`: the bounds of the block, wedge, cylinder, stairs
  and floor slab are the boxes their values state at the defaults, both range
  corners and four derived seeds; the stairs have 12 triangles a step and no vertex
  with more than six facets; the cylinder's points are exact mirror images
  at nine segment counts. `Kit_architecture_shapes`: the door wall's, the
  arch's and the pillar's bounds follow their values at the same value sets,
  the round pillar's over six segment counts and the corners of width and
  height; the door wall has no
  vertex inside its stated opening and vertices on its edges; the arch at
  every segment count from 2 to 48 and five sizes passes the mesh checks as
  one component within the ceiling, has no vertex inside the opening's
  ellipse, its feet at the span and its crown at the rise; the round pillar
  stays inside its cylinder with full radius at the base and the top, and the
  square pillar's plinth and abacus fill its box around an inset shaft. The spatial query
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
  each element unscaled with its x bounds centred on its chord, an open curve one segment fewer, a slope upright (plan length at
  the first point's height) and tangent (the chord, pitched), a start pose,
  and the refusals (no curve, one point, a zero-length segment naming it, a
  vertical segment in both orientations, no item; an item without x extent
  is centred); the `segments` pattern text, the default pattern's canonical text
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
  midpoints between them, a disc's on its circle, none for a count mode line
  of one piece) and `apply_region_handle()` (an edge's size twice its reach
  with the other values and y ignored, a corner's both sizes, a disc's radius
  the xz distance, the clamp past the origin and beyond the range, a point
  that is not finite changing nothing); the layout handles of a line, grid
  and ring ("Layout handles": a count mode line's run end setting the step
  as the reach over count - 1 with its sign kept, a gap mode line's length
  end, a grid's far corner and far edge middles setting the spacings as the
  reach over count - 1/2 with their signs kept, a ring's radius handle at
  the arc's middle (60 degrees round on a 5 degree arc) and arc handle inside
  its end, the arc unwrapped to the turn nearest the drag's previous
  unclamped angle and held at 360 and 1 degree while the pointer goes on past
  half a turn, each value
  clamped at both range ends) and `get_layout_handle_set()` (what the
  indices name per kind, count mode and region).
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
  a selected stairs piece shows its parameter rows. Kit mode places along
  the normal under the pointer (U51), in a scene of its own (closed
  afterwards): with snap alone an upright block rests flush against a wall
  by its back face, with its top against a ceiling and on its base on a
  floor, at the module-rounded hover point; with Surface Rest anchor the
  floor pose is the same and on the wall the block's origin lands on the
  wall's face (a click places it reaching into the wall); the grid hovers with normal
  (0, 1, 0); with orient the block stands out of the wall (up axis along
  the wall's normal, its base on the wall) and hangs from the ceiling by its
  base, a rotate step turns it about the wall's normal, a wedge scaled
  (1, 1, 2) hovers with the slope's transformed normal and the block takes
  it, and on a slab tilted 15 degrees the block takes the slab's normal with
  X and Z on the module lattice; the inner side of a room's wall seen from
  inside hovers with the normal facing the viewer; Place As Layout on the
  wall gives the layout the direction (0, 0, -1); each click places at the
  preview's transform. A `capture_screenshot` with
  the preview over the slab goes to `logs/level_tools_level_kit_window.png`
  (`--window-image`). The shape thumbnails ("Shape thumbnails"): every shape
  has a current thumbnail at the window scene's lengths, rendered once, and
  nothing renders again over 30 frames; a `set_scene_settings` module change
  and the window's Module row typed back each render again exactly the shapes
  whose default mesh key changes, once each; a `capture_screenshot` of the rows
  goes to `logs/level_tools_kit_thumbnails.png`; a `set_graphics_preset`
  change and its restore each render every thumbnail again once, a LOD-only
  change none. The Properties window with that stairs piece selected
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
  every rock form of 1, 2 and 4 pieces and eight rocks of consecutive seeds
  (`kit_next_seed()`, ported to the script) with form, size, roughness and
  pieces derived, each within the ceiling on its flat base, more facets for
  more pieces, a convex hull collision shape; a `render_scene_image` (1600 x
  1000) of them to `logs/level_tools_rock_forms.png` (`--rock-image`); a masked
  placement deriving its size, two pieces of equal seed and mask sharing one
  mesh, a value for a masked parameter and a derived fixed parameter refused,
  `derive: []` taking a value, a seed change re-deriving as one undo entry, and
  a record of an old form (no `form`, `pieces`, `derive`) not following the
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
  without surface snap do. The layouts of this group search 1 m either way
  (backward distance 1 m), as `level_snap_to_surface` does.
  Rotated meshes (`--only-rotated-mesh-snap`), in a scene of their own
  (closed afterwards): Y-up boxes under import roots turned about Y and
  scaled 0.0146 rest on their -Y face, answer 0 to the query and the support
  report, drop back when lifted 0.5 m and stay on the ground through a
  surface-snapped drag (Surface Rest leading face), while a drag with
  `surface_rest` `anchor` lands their origin, the box center, on the ground
  (U60); a Z-up box under a -90 degree X turn, oriented,
  turns its local +Y up and rests on its -Y face; a block oriented onto a
  60 degree slope answers 0 with its pose to three orient queries. Layout
  surface search (`--only-layout-surface-search`, U51), in a scene of its
  own: a new layout reads the search defaults; a path layout started 0.6 m
  up inside a 1 m solid rests on the ground inside it with backward distance
  0 and rises onto the solid's top with 1 m; a line searching up presses
  its blocks' tops to a ceiling slab, unturned, and with orient turns them
  over onto it by their base, heading kept; a path searching east mounts
  its blocks on a wall 0.8 m away with one shared roll, finds nothing with
  forward distance 0.5 m, comes back out of the wall with backward distance
  0.5 m (not with 0), and 0.5 m blocks 0.2 m off the wall behind a panel 0.4
  m off it are refused; `level_set_layout` refuses a
  zero direction, a negative distance and the key `surface_max_distance`.
  Layout surface support (`--only-layout-surface-support`, U52), in a scene
  of its own: a new layout reads `five_lines`; a 1 m block started 0.3 m up
  under a table top covering its base center only is refused by
  `five_lines` and `center_blocks` and rests on the ground with
  `center_fallback`; started 0.6 m up over clutter under one footprint
  corner it rests on the clutter under every rule; started 0.1 m up beside
  it, `five_lines` refuses and the other two drop it to the ground;
  `level_set_layout` refuses an unknown name and a number; a save and reload
  keeps each rule and the children's places.
  The surface-snapped layouts of the other groups whose pieces start inside
  a hill or a slab (the Properties rows, the live drags, the transform rows,
  the drag cancel and record rule, the scatter over the hill) take backward
  distance 1 m, so they rise onto it as the queries they are compared with
  do.
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
  wall (placed without an attachment) onto its end in one entry, undo and
  redo exact, a second connect no entry; after the target's length changes a
  re-connect at its new end; an
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
  Attachments (`--only-attachments`), in a scene of their own: a connected
  placement recording an active attachment naming its target's uid, the
  target none; `set_node_transform` of the target moving and turning the
  follower onto the join (a fresh connect no change) in one entry, undo and
  redo exact for both; a held `drag_selection` of the target followed at
  every step with no entry until the release, one entry, its undo and redo
  exact for the follower; a length change of the target followed in one
  entry; a chain a <- b <- c following a move of a in one entry; attaching a
  to c made (b and c follow a, the new connection kept and reported apart),
  undo giving a its pose back; a move of b detaching it in
  the same entry while c follows b, its undo giving the attachment back; a
  deleted target leaving the attachment dormant and the undo of the delete
  making it active (a later move followed); a disabled target connector
  making it dormant; a drag released connector-snapped with full orientation
  attaching the dragged wall to the snapped pair in one entry and following
  the target; `level_detach_piece` in one entry, again no change, the
  detached wall staying; a placement and a connect onto a locked layout's
  child without an attachment (`not_attached`); save and reload keeping the
  attachments and following a move; a raw `set_item_property` write of the
  target's parameters followed in one entry; the target and its follower
  dragged together staying attached, and scaled together leaving the follower
  where the drag put it (live and at the release) and detaching it; a dormant
  attachment moved with its target kept; a Replace of a piece whose child is a
  follower with a second follower earlier in scene order, both joined in
  dependency order in one entry, and a rider below the follower detached; the
  saved file imported twice into one scene, the copies' attachments dormant
  as ambiguous and nothing following.
  Property flow (`--only-property-flow`), in a scene of its own: a wall on a
  floor's edge taking its values and the placement's report naming them; a
  wall joined to a wall's end; a thickness edit spreading along a run of
  three walls and, with the step limit at 1, stopping with the limit and the
  piece still changing reported; W1 / W2 / F taking the floor's values over
  the wall's, reported as not taken (`higher_priority`); the overrides of one
  call (`flow_mask` without `wall-floor`, `auto_connect` false) leaving the
  settings as they were; a room of four walls: a wall moved outward, turned,
  slid, lifted and tilted (the floor fits, the wall returns to its edge), a
  move into an invalid outline refused with nothing recorded, a corner edit
  and an edge handle (`level_move_segment` and a held `transform_selection`
  with the walls following live), a connected neighbour floor's shared
  corner moving with the edge, and wall-floor masked off moving a wall alone
  and removing its connection; two free walls end to end fitting; a locked
  layout's child taking nothing; four blocks closing a cycle in place and
  moving rigidly; undo and redo of each.
  Riders (`--only-riders`, C10), in a scene of its own: a floor listing its
  four `rail` connectors 0.1 m inside its edges and `rail_inset` in its
  pattern; a railing placed on `rail_0` taking the rail span's 3.8 m from the
  floor at priority 2, recorded on the railing, standing on the rail; a
  railing on a wall's top taking the wall's 3.75 m and running along it, a
  battlement there taking the wall's length and thickness; a wall's top
  joined onto a free railing with `level_connect_pieces` recording on the
  railing; a floor outline edit giving the riders on the floor and on the
  walls' tops their new lengths in one entry, undo and redo; a held corner
  edit showing the rider on a wall's top taking the wall's new length live,
  its release equal; a wall length edit reaching its rider, and a rider
  length edit leaving its wall; a rider moved off its rail going back, and a
  held drag of a rider on a wall's top showing it back on the top; a moved
  floor carrying its riders; Detach freeing a rider, Auto-connect of a floor
  edit joining it again on the rider; `rail` masked off in one call joining
  without values; a `rail_inset` edit moving a rider and its length; save
  and load keeping the lengths, the connections recorded on the riders and
  the inset, and a later outline edit still reaching the riders.
  Flow preview (`--only-flow-preview`, also running the Brush tool's floor
  join and connector join of the Level Kit window steps), in a scene of its own: a held
  `drag_selection` snapping a free wall onto a floor's edge connector
  showing the flowed values with no undo entry and no flow report, its own
  values back when it unsnaps and the flowed ones when it snaps again; five
  steps of a held snap running no flow (`held_steps`, `runs` of
  `level_get_flow_preview`); the release equal to the last step; a room's
  wall dragged 1 m out showing the floor fit live, a move into an invalid
  outline keeping the last valid preview with the refusal reported and its
  release refused, a release at 1 m equal to its last step; a wall slid
  along its edge staying on it while held; a held edge handle edit moving a
  connected floor's shared corner live, its release equal; a cancel of a
  snapped, flowed drag and of a connected wall's drag giving every value,
  fit and pose back with no entry; a tool switch mid-drag keeping the
  preview, its release one entry; a block dragged so its free connector
  reaches where its follower's connector was at the begin snapping nothing
  and drawing nothing there; a block dragged off the block it is attached
  to snapping to that block's other connector and joining there; with
  wall-floor masked off a wall on a floor snapping to another edge of it;
  the scene closed with nothing leaked, and
  closed mid-drag with nothing leaked and a drag in a new scene previewing
  again; a held Properties Rotation.y row drag of a wall on a floor edge
  putting it on the turned edge's midpoint with the gizmo anchor there, and
  a selection change giving it up with nothing changed; the Brush tool's
  joined preview taking a floor edge's length, thickness and height, a new
  wall thickness of the floor and a target wall's new material with the
  pointer still.
  Connection fixes (`--only-connection-fixes`), each in a scene of its own:
  two corner edits of a room keeping a neighbour floor's floor connection,
  the shared corners and every wall's connections; two adjacent walls moved
  out together fitting both edges and their corner in one entry; a
  wall-floor connection Auto-connect made with the floor changed recorded
  on the wall, a wall move then fitting the floor and a floor move carrying
  the wall; an author's height edit kept through the flow after
  Auto-connect; a wall joined with twist 2 slid along its run changing no
  edge line; a wall in a moved group fitting its floor; a stale record
  (target deleted) replaced by Auto-connect, and a raw record list with two
  entries of one connector logged as an error; second connections of one
  connector refused on both ends; with `wall` masked off, moving either end
  of a wall join apart removing it; room corners reporting joined and
  `level_get_connected_groups`; a floor's Properties connection rows, Remove
  and Detach; an attach-none join that did not move leaving no Auto-connect
  exclusion; a floor snapped under a free wall giving the wall the edge's
  values, undo and redo; a wall in a moved group making the wall joined to
  its end fit; two near-parallel changed edges meeting at their crossing,
  and refused naming both walls when their lines move apart; a floor's
  connection row gone at a wall's delete and back at its undo.
  Connection persistence (`--only-connection-persistence`), in a scene of
  its own: a room of a 6 x 4 m polygon floor with values off their defaults
  and a floor material, four walls on its edges joined at the corners, a
  second floor sharing its +x edge and one wall connector switched off,
  saved, closed and loaded: both floors' outlines, values, slab child,
  bounds and materials, every connection record on both ends, the connector
  types and switches, the walls' flowed values and materials as they were;
  then in the loaded room a corner edit lengthening the side walls and
  moving the shared corners of the second floor, a wall moved out making the
  floor fit, Detach freeing a wall that then moves alone, undo and redo of
  each; its navigation mesh walkable and reachable on the floor's top inside
  the walls, the reachable area the inside less the agent radius, and the
  second floor beyond the wall an unreachable island.
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
  with Surface Rest leading face (the default) mid-drag over the slab's
  edge it rests on the slab; dragged onto the wedge it rests on the highest
  point under it, and with orient it takes the slope
  normal keeping its heading, one undo entry that undo takes back with the
  rotation; a Y drag lifts it; an XZ plane drag rests it; two blocks move by
  the one displacement that rests the lead's base (the active node's, the
  last selected) on the slab; a parent and
  its child both selected rest by the parent with the child's offset kept;
  `transform_selection` rests only with `surface_snap`; `drag_selection`
  refuses the options on a rotate drag; a `drag_selection` of 50 blocks
  moves all of them by the one displacement that rests the lead on a slab,
  also with orient, and times the leading face and the oriented steps. A
  `capture_screenshot` of the toolbar buttons and the Move rows goes to
  `logs/level_tools_surface_settings.png` (`--transform-image`).
  Anchor snap (`--only-anchor-snap`, U51, U60), in a scene of its own with a
  ground slab, a wall and blocks, Surface Rest anchor unless named:
  `get_surface_snap_settings` reports the drag direction mode and vector and
  the rest rule, and `set_surface_snap_settings` refuses a zero or
  non-number vector, an unknown mode and an unknown rest; an X-handle drag lands the
  block's origin on the ground below the dragged anchor, the gizmo anchor
  with it, one undo entry; a Y-handle drag with the direction down lifts it;
  in BBox mode the bounding box center lands on the ground; with the
  explicit direction (2, 0, 0) (reported as (1, 0, 0)) an XZ plane drag
  rests the origin on the wall's face, and with Surface Rest leading face
  the same drag rests the block flush against the wall; on the floor both
  rules land a kit block at the same pose; with leading face Escape
  mid-drag restores the start pose and a completed drag records one undo
  entry; in camera ray mode an X-handle drag
  and a `drag_selection` with `surface_direction_mode` `camera_ray` rest it
  where the ray from the camera through the dragged anchor meets the
  ground; `drag_selection` with `surface_direction` (1, 0, 0) rests its
  origin on the wall with `surface_rest` `anchor` and the block flush with
  the default `leading_face`, and refuses a zero direction and an unknown
  `surface_rest`; a two-node X drag moves both by
  one displacement, the average of their origins on the ground; with
  `lock_translation_y` the snap is refused and the gizmo stays at the
  origin; Escape mid-drag restores the start pose and the anchor without
  an undo entry; with orient, in BBox mode and for two nodes, the anchor
  lands on a wedge's slope and the set turns rigidly about it, the anchor
  turned with it; a wall dragged with a connector snap distance rests on
  the ground before its start meets another wall's end; a camera ray drag
  with orient onto a wall keeps one roll whether it ends above or below
  the camera's eye height; with leading face a level group, a Curve layout
  node and a mesh with a child mesh below it rest by their subtree bounds
  (the lowest piece's base on the floor, the set moved rigidly); and with
  the four view's orthographic Top
  camera hovered a camera ray drag drops the anchor straight down.
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
  in `level_get_layout_indicators`, none for a count mode line of one piece
  (whose handle selection is refused), the node the target when one of its pieces is selected; for
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
  Layout handles (`--only-layout-handles` runs it alone; after the region
  handles in a full run), in a scene of its own: the handles of a turned
  count mode line (its run's end), a gap mode line (its length's end), a
  grid (the far corner, then the middles of its far x and z edges), a
  turned half ring (its arc's middle and its arc's end inside the ring) and
  an unlocked full ring equal in world space to the ones computed in the
  script, the same in `level_get_layout_indicators`, none for a count mode
  line of one piece (whose handle selection is refused); for each handle the
  gizmo anchored at the selected handle and a `transform_selection` of it
  setting exactly the value the node-frame point gives (step = reach / 3,
  length = reach, spacing = reach / (count - 1/2), radius = the reach along
  the handle's offset per meter of radius, arc = the angle about the center),
  the other values kept, the children regenerated, the handles following,
  one entry, undo exact and redo; a gap mode line 4.2 m long holding more
  pieces than at 3 m; clamping: a line's end past the origin holding a 0.01 m
  step, a gap line's holding length 0, a grid edge past the near side
  holding a 0.01 m spacing, a radius handle past element 0 holding 0.01 m,
  the arc's end at 270 degrees and then on past element 0 holding 360, and
  at 45 degrees and then back past element 0 holding 1; in the viewport a
  held gizmo drag of the locked grid's x edge previewing a wider spacing
  with regenerated children and no entry, the handles following, Escape
  restoring exactly, a release of one entry with spacing_x (2.5 + the drag)
  / 2.5 within the drag tolerance and the pieces of the last step, undo
  exact; a held drag of the locked gap line's end previewing a longer run
  with more pieces and a release of one entry; held drags of the unlocked
  ring's radius handle showing the pending radius on the outline's handle
  with the node's values and children unchanged, Escape drawing the node's
  ring again, the release recording the pending radius as one entry; and
  screenshots of the selected grid's handles (its x edge handle selected) and of
  the half ring's (none selected)
  (`logs/level_layout_handles_grid.png`, `logs/level_layout_handles_ring.png`).
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
  `--only-thumbnails` the Level Kit window checks, its shape thumbnails, the
  Properties rows and the live parameter drags (the screenshot
  `logs/level_tools_kit_thumbnails.png`, `--thumbnails-image`),
  `--only-window-layout` the Level Kit window checks up to its Layout section,
  the Properties Kit Layout group and the Properties write,
  `--only-brush-normal` the Level Kit window checks and the kit mode
  placement along the surface normal, `--only-brush-connect` the Level Kit
  window checks and the kit mode connector join (a wall's end connector
  joined at the pose of `level_place_pieces` connect, the click one undo
  entry, the target drawn as the snapped pair, the rotate binding's twist,
  Shift, a switched-off connector and a hover beyond reach; the screenshot
  `logs/level_tools_brush_connect.png`), and
  `--only-layout-drags` the live layout preview checks,
  `--only-drag-cancel` the Transform drag cancel checks,
  `--only-snap-to-surface` the snap to surface checks,
  `--only-surface-attachment` the surface attachment checks (the support
  `level_snap_to_surface` names: a table top, the ground, a wall searched
  sideways, the mesh under the base center where a corner rests on another
  at the same height, orient, and null without a place; a record written
  raw on a child of the table read active, one naming another node or no
  node inert, one that does not read inert with its reason; placements onto
  a table and a wall attaching, `attach` false not; a snap attaching to the
  ground and one onto a mesh that is no kit piece detaching; a move off a
  table inside a group detaching to the group; a snapped `transform_selection`
  re-parenting; a nudge keeping the attachment at its new place; a connected
  wall not attaching and a join detaching; `level_detach_piece`;
  `reparent_item`; a chain of three placements; undo and redo of each; a
  support's shape changes carrying its pieces, `off_surface`, chains, a
  held parameter drag; a picture on a locked segments layout's element:
  selectable and editable, re-homed to the replaced element, kept on a
  kept one, detached when its element goes, live during a held segment
  handle edit, kept through unlock and lock; a crate on a floor layout
  following its outline and reading `off_surface` over a notch; a held
  floor edge edit carrying a poster on the fitted wall; a cancelled held
  segment handle edit; a crate on a regenerated staircase; copies (group
  copy, clipboard copy and paste), a mirror, Replace of the support and of
  the piece, deleting the support, a save and a load; the Brush tool's
  placement and the Properties Detach row),
  `--only-replace`
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
  `--only-former-ids` the checks of the former shape ids (the catalog lists
  the former ids of the 18 shapes that have them and no shape id is a former
  id; a placement by a former id records the shape's id and follows the kit;
  a saved scene whose pieces name the former ids loads with every piece
  following its kit at its saved values and bounds and recording the shape's
  id, a former-id tree record of a `canopy` keeps its loaded mesh and its
  former id naming the parameter, and saved again the scene names the
  shapes' ids and the tree's former id; a piece set to a former id takes a
  parameter change as one undo entry that records the shape's id (undo gives
  the former id back) and a Replace carries its values; the door wall between two walls has their ends, faces and top, the
  arch between two walls meets them at its feet, `level_check` reports no
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
  `--only-foliage-flag` runs the foliage flag checks ("Surface placement"),
  which the full run ends with: a ground slab and a block on it, the block
  unflagged and then flagged by hand with `set_item_flags`; flagged, a probe
  over it snaps to the ground (`level_snap_to_surface`), a block standing on
  it is unsupported 1 m over the ground (`level_get_support_report`), the
  height map over it reads the ground, Level Check counts it in
  `excluded_count` and no finding names it, `pick_at` and a viewport click
  still hit and select it, viewport Create > Xform lands where the pointer
  ray meets the ground behind it, and so does viewport Paste of a copied
  block, and the Brush tool's kit mode, its hover
  still on the block's top, previews and places a block on the ground at
  the x and z where the pointer ray meets it behind the block
  (module-rounded), not under the hover point; unflagged, each of these
  meets the block; a hand-flagged wall is no connector join target (its
  end connector joins unflagged); through the foliage block onto the grid
  (no surface behind, Snap To Module off, Snap To Grid on) the preview takes
  the grid-snapped point that hovering the grid directly gives; a non-kit
  brush (the scene's first content library brush, selected) previewing
  (`preview_position`) and placing on the block's top unflagged and on the
  ground behind it flagged; save, close and load keeping the flag.
  `--only-foliage-index` runs the foliage index checks ("Foliage paint"),
  which the full run ends with: two bushes, a grass tuft and a block
  flagged by hand on a ground slab; after every step `level_get_foliage` is
  compared with the script's own search over `level_get_pieces` (the
  flagged pieces, their world positions and `foliage_radius`, the parent
  the script made, the count, `max_radius`, and in a sphere the density):
  placing, flagging, radius edits through `set_item_property` (three undo
  entries, the undo and redo of one), a radius of 250 m refused, unflagging
  the largest and flagging it again, `set_node_transform` of a bush, the
  grass reparented to a block and the block moved (the grass alone moves in
  the index), spheres of 1 m, 30 m and far away, deleting a bush and the
  grass's parent with its child, two undos, a redo, a plain mesh (no kit
  piece, `create_shape`) flagged by hand in the index with radius 0 and an
  empty shape (compared with `get_node_details`), and a save, close and
  load keeping the five items with their positions and radii; after the
  load, `get_item_properties` listing `Kit_piece.foliage_radius` on the
  flagged plain mesh and not once it is unflagged, the plain mesh given
  radius 2 m, and a stroke over it and over a reloaded bush keeping every
  pair apart (the plain mesh's radius counted, nothing within 2 m of it),
  the index equal to the reference. The 250 m radius write is refused by
  the property's validation, the call failing with that reason.
  `--only-foliage-paint` runs the foliage paint stroke checks ("Foliage
  paint"), which the full run ends with. Its reference is the scene's
  foliage pieces as `level_get_pieces` and `get_scene_nodes` give them
  (world pose, radius, parent, record), and after every stroke
  `level_get_foliage` must hold the same items. On a 60 m floor slab, a slab
  tilted 30 degrees, a wall block, a slab turned 40 degrees about y, a slab
  scaled (2, 0.5, 0.5) and a plain box (`create_shape`), `level_foliage_paint`
  strokes whose items lie on the surface (within 1 mm of the plane the script
  computes), inside the brush sphere, under the hit mesh, upright or along
  the surface normal with `align_to_surface`, at the pose the reply names,
  with active records on the kit pieces and none on the box; the spacing
  over every pair after each stroke; a second stroke over the first adding
  items, each under the floor on its top and none on earlier foliage;
  strokes of one spot until one places nothing, under the disc
  packing bound; weights 1, 3 and 0 at one radius (the share of the weight 3
  entry within 0.1 of 0.75, none of the weight 0 entry); bushes of 3
  variants giving 2 or 3 mesh keys, with a palette material the scene lacks
  inserted in the stroke's entry, gone with its undo and back with its redo;
  the slope moved (its items move with it) and widened from 10 m to 14 m
  (upright and aligned items carried proportionally along it, turned as
  before and active, within 1 mm); the floor thickened (its items carried
  onto the new top); one undo entry per stroke, its undo and redo, the index
  equal to the reference after each; a stroke
  over nothing and a stroke cancelled by a zero normal in its second stamp
  recording nothing and keeping the redo history; a `batch` of a placement
  and a stroke as one entry, undone together, and a `batch` whose stroke is
  cancelled keeping its placement; the refusals of a radius of 0, a negative
  density, a palette of weight 0, an unknown shape, a stamp with an unknown
  key and a `scene_name` that is no string; on a 2 m slab with painted
  grass, `Selection.duplicate` and `clipboard_copy_nodes` /
  `clipboard_paste` of one grass and of the slab, each copied foliage piece
  keeping the flag and the 0.15 m radius, the index holding it, the copied
  slab's grass attached to the copied slab, each undone; `level_replace_pieces`
  of the slab by a 1 m block carrying its grass onto the block's top,
  attached, and its undo; a stroke over a Deccer cubes instance
  (`instantiate_prefab`) placing nothing inside it, its candidates there
  counted in `on_sealed_support`, and painting the floor beside it; and,
  the weight check's items deleted to keep the scene small, a save, close
  and load keeping the foliage. A mirrored support is not painted: mirroring a registered mesh at
  run time is fatal (`doc/plans/open_items.md` "Editor defects found in passing").
  `--only-foliage-remove` runs the foliage remove stroke checks ("Foliage
  paint"). Its reference is the foliage as
  `level_get_pieces`, `get_scene_nodes` and `get_node_details` give it, and
  every stroke's removed ids and the items left must equal the script's
  replay of the remove stream (`foliage_remove_model()`: the splitmix64 of
  `Kit_random` over the documented order and threshold; an item within
  1 mm of a brush border fails the check as undecidable; the replay takes
  the items a lock keeps as drawn and kept). On a 40 m kit floor with grass
  and bushes painted in three areas, a kit block and a plain box (no
  foliage) and two hand-flagged plain boxes: density 1 removing every item
  in the brush, the hand-flagged box too, and leaving every item outside
  it, the kit block and the plain box as they were, grass 2.2 m above and
  1.8 m below the stamp's center, 0.1 m from it across, among them; a selected item outside
  the brush staying selected and a selected removed item leaving the
  selection and coming back with the undo; its one undo entry, the undo
  restoring the items with their poses, radii, parents and surface
  records, and the redo; a stroke whose second stamp's center is 1e39
  cancelled with the items and the selection as before; density 0 removing
  nothing, recording nothing and keeping the redo history; density 0.2
  (whose threshold, the ceiling of 0.2 * 2^24 = 3355443.25 in float, the
  `Foliage_sampler.remove_threshold` test pins with draws at the threshold
  and one below) over five stamps, three at one center, thinning the area; `only_shapes` with
  the former id `nature.bush.v2` and with `nature.bush` removing the bushes
  alone (grass and the hand-flagged box stay) and an empty `only_shapes`
  removing all; a bush with a foliage child and a plain child leaving with
  its subtree; an edit-locked bush kept with its unlocked foliage child
  removed; a drawn grass below an edit-locked block below a drawn bush
  removed, the block kept at the bush's parent; their undo; a stroke over
  no foliage recording nothing; a `batch` of a placement and a remove
  stroke as one entry; the refusals of a radius of 0 and -1, a density of
  1.5 and -0.1, an unknown shape, a center of two numbers, a stamp with an
  unknown key and an unknown argument; a zero normal accepted; a save,
  close and load keeping the foliage left; the Delete command's operation
  (`delete_nodes`) on a parent and its child given together, in both
  orders, as one undo entry whose undo puts the child back under the
  parent; and a stroke over a locked line
  layout keeping its flagged child and removing the grass painted on its
  other block. Its index comparison holds the positions and radii, not
  only the ids. A flagged child of a locked layout is a state only a file
  produces (the flag is refused on a locked layout's child, and locking
  replaces the children), so the group makes it by editing the saved
  scene's text, `"Kit_layout.locked":"false"` to `"true"`, before the
  load.
  `--only-foliage-tool` runs the Foliage Paint tool checks ("Foliage
  paint"), which the full run ends with, through the input drivers
  (`mouse_drag` with `hold` and `mouse_release`, `mouse_click`,
  `mouse_wheel` with Shift, `key_press`, `inject_input_events`,
  `imgui_click`) on a 40 m floor slab with a 3 m slab, a kit block and a
  plain box; its waits read `level_get_foliage_brush`, and one finally
  gives back the settings, the bindings, the tool, the windows and the
  scenes. Its Paint Density 0.3 gives one attempt per stamp at every
  radius it uses, and every drag stays on the framed part of the floor, so
  a check paints at most one item per stamp. The settings tools: the
  values and a grass palette written and read back, the entry taking the shape's radius and 8 variants; an
  unknown argument, a radius of 60, an unknown shape and yaw limits out of
  order refused with nothing changed; a weight-0 palette kept with
  "nothing to paint" as the refusal. The brush on the floor with its
  normal; over a 2 m water piece 0.3 m above the floor the brush on the
  floor under it and a short stroke there painting the floor; a short left drag one stamp and one undo entry, the seed then
  `kit_next_seed()` of it; the entry's spread typed as 0.3 into its
  Tool Properties row (Ctrl + click, `type_text`, Enter) written into the
  entry's parameters (`level_get_foliage_paint_settings`), a stroke then
  painting grass tufts of spread 0.3, Reset emptying the parameters and the
  next stroke painting the default spread 0.5 with mesh keys none of the
  edited items have; a 7.7 m drag over the floor, injected in 40
  steps and in one, held until the brush reaches its end, stamping exactly
  1 + floor(L / (r / 2)) times; a one-step jump with a 0.4 m brush walked
  over several frames to the same exact count; a drag released in the
  frame of its only motion (`inject_input_events`) giving the exact count;
  a fast 13 m drag of three jumps with a 0.1 m brush, released in the frame
  of its last motion, giving the exact count through a backlog above 256
  stamps; a Shift + wheel during a backlog continuing one new step after
  the last stamp; a held drag of 1.3 m with a 1 m brush, Shift + wheel ten
  steps down (the new 0.19 m step below the 0.3 m carried), then a turn of
  90 degrees: every stamp center (`stamp_centers`, read with
  `include_stamp_centers`; not truncated, not listed without it) within 1 mm of the
  travelled polyline; a drag out of the viewport over a side edge (the
  left one when the window has room beside it) and back in lower on the
  same edge, its points on the floor clear of the obstacles, stamping exactly the press, the re-entry and one
  per half radius of the short travel after it, and painting nothing along
  the chord across the outside; a middle press without motion
  followed by the viewport window's hide ending the stroke with its press
  stamp as one entry; Shift + wheel 1.1 times the radius and back
  with the camera still, the plain wheel zooming with the radius kept; a
  right drag removing the painted items as one entry with no context menu,
  a plain right click a one-stamp remove stroke with no menu, a plain right
  click over the sky (no brush) opening no menu and beginning no stroke,
  Shift + wheel there changing the radius, a
  left press held without motion taking a right click with no menu and no
  second stroke, and the mirror (a right press held, a left click: no paint
  stroke, the selection kept), the same right click with the Move tool
  active opening the menu; Alt + right drag turning the camera with no stroke; the paint
  drag rebound to Ctrl + left drag (`set_command_bindings`, reset after)
  painting with Ctrl and not without. With a stroke held open: Escape and
  Shift + Escape removing its items with no entry, the redo history kept
  and the seed advanced; a hotbar key making another tool active and
  ending the stroke as a release, its items kept as one entry; MCP `undo`
  and `redo` refused with the stroke's reason and Ctrl+Z undoing nothing
  (the undo command declines without a log line while `can_undo()` reads
  the block reason, so the unchanged stack is the check);
  `level_place_pieces`, `delete_nodes`, `batch`, `close_scene`,
  `reset_editor_state` and `save_scene` refused with the reason while
  `level_get_foliage` runs; a right press beginning no second stroke and
  `level_foliage_paint` refused; Delete through `key_press` running after
  the release as a later entry; Delete of the support painted on running
  after the stroke, two undos giving the scene's nodes and foliage back.
  With the paint command rebound to the middle drag, so the left and right
  buttons are free: the Level Kit window's Group Selected refused with the
  reason and no group left; a Properties thickness drag of the slab
  applied live, its commit refused and the value restored; Catmull-Clark of
  the box from the Operations window, its worker's completion held in the
  queue until the release and recorded after the stroke's entry; Commands >
  File > Save glTF (erhe) queued and started only after the release; the
  viewport window hidden through the Window menu ending the stroke with
  its entry before any release, with no active mouse command left; closing another scene, then the stroke's own, through the
  Scene Hierarchy row menu abandoning the stroke with no entry. The windows
  a check clicks open mid-stroke through the Window menu and hide after
  the release. The run's last act, after its end-of-run state check (in
  the full run and with `--only-foliage-tool`), is `request_exit` with a
  stroke held open on the run's own editor: exit code 0, the log's "Live
  edit abandoned" and "paint stroke abandoned" lines, no stroke destroyed
  open and no crash trace. The viewport scene view's destruction is not
  reached mid-stroke: only the undo of a scene builder's viewport
  operation destroys one, and undo is refused while a stroke is open.
  `--only-kit-slots` runs the checks of "Kit slots" with real ImGui drags
  (`mouse_drag` from a shape row or a slot onto a slot, its windows side by
  side): the Block row (not the one the tool places) onto a vacant grid slot
  giving a kit slot of the Block's defaults and default materials, the tool's
  state and the selection unchanged; the chosen Wall row after a length and a
  slot material edit onto a hotbar slot carrying those, a later row edit
  leaving the slot as it is, and the slot's `config` holding the shape id,
  the parameter text and the material; `activate_inventory_slot` of the
  hotbar slot giving kit mode with the slot's shape, values and materials,
  and the window's Wall row then carrying the slot's item; a kit mode click
  placing a wall with exactly those values and material, one undo entry,
  the tool's seed advanced and the slot's item unchanged; slot to slot
  swaps within the grid and between the grid and the hotbar; a right click
  clearing kit slots; and the slots given back as they were.
- `editor_level_tests` `Kit_item_slot`: a kit item written into
  `Inventory_slot` and read back is the same item and writes the same
  fields; edited values and a derive mask survive; a former id reads as its
  shape and writes the shape's id; a slot without a kit shape reads as no
  item without a problem; an unknown id, malformed text, a value out of
  range, an unknown palette name and too many names give no item and a
  reason; the tooltip names the shape, its values and its materials.
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
- [Level kit connections: implementation steps](../plans/level_kit_connections_implementation.md)
- [Kit layout composition: plan](../plans/kit_layout_composition.md)
- [Level kit surface attachments: plan](../plans/level_kit_surface_attachment.md)
- [Foliage paint tool: requirements](../plans/foliage_paint_tool.md)
- [Foliage paint tool: design](../plans/foliage_paint_tool_design.md)
- [Foliage paint tool: implementation steps](../plans/foliage_paint_tool_implementation.md)
- The Foliage Paint tool's palette as a target of a kit item drag ("Dragging a kit item").
