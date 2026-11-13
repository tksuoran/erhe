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
  kit, display name, description, tags, parameters, one material slot name,
  normal style (`faceted`, `smooth`, `smooth_with_hard_edges` with its angle),
  triangle budget, collision kind (`Kit_collision::convex_hull` or
  `triangle_mesh`), randomness (`Kit_randomness::seeded` for the shapes whose
  generator draws from the seed: the five nature shapes) and the generator.
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
- `Kit_shape_library` (`kit_shape_library.hpp`): the three kits (basic,
  architecture, nature), the 14 shapes and the palette: one flat-color
  material per slot name (`Kit_palette_material`: slot, material name, linear
  base color, roughness). Adding a shape is one generator and one table entry.
- Shape conventions: Y up, front toward +Z, the origin at the center of the
  base (a seeded shape: inside its base); stairs and the wedge rise toward -Z
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

The derivable parameters, their derive ranges and the default masks (masked in
bold); every seed and every segment count is fixed:

| Shape | Derivable (range) |
|---|---|
| basic.block | width, height, depth (0.5 - 3 modules, step 0.25) |
| basic.wedge | run (2 - 4 modules), rise (0.25 - 1.25 modules), width (1 - 3 modules), step 0.25 |
| basic.cylinder | radius (0.25 - 1.5 modules, step 0.05), height (0.5 - 3 modules, step 0.25) |
| basic.stairs | rise (0.25 - 1.5 wall heights, step 0.05), width (1 - 3 modules, step 0.25); step_rise_limit fixed |
| basic.floor_slab | width, depth (1 - 4 modules, step 0.5), thickness (0.1 - 0.4 m, step 0.05) |
| architecture.wall | length (1 - 6 modules, step 0.5), height (0.75 - 1.25 wall heights, step 0.05), thickness (0.15 - 0.4 m, step 0.05) |
| architecture.door_wall | length (2.5 - 6 modules, step 0.5), height (0.85 - 1.25 wall heights), thickness (0.15 - 0.4 m), opening_height (2.0 - 2.4 m), step 0.05; opening_width (0.9 - 1.6 m, step 0.1) |
| architecture.pillar | profile, width (0.3 - 1 modules), height (0.5 - 1.5 wall heights), step 0.05 |
| architecture.arch | span (2 - 4 modules, step 0.25), rise (0.8 - 1.2 wall heights), depth (0.3 - 1 modules), thickness (0.2 - 0.5 m), step 0.05 |
| nature.rock | form, **size** (0.6 - 1.5 modules, step 0.05), roughness (0.1 - 0.6, step 0.01), bevels (0 - 2) |
| nature.land_mass | footprint (3 - 8 modules, step 0.5), height (0.5 - 2 modules, step 0.25), **roundness** (0.2 - 0.8, step 0.01) |
| nature.hill | radius (2.5 - 6 modules, step 0.5), **height** (0.5 - 1.25 modules, step 0.05) |
| nature.tree | **trunk_height** (0.4 - 0.8 wall heights, step 0.05), **canopy_size** (1.5 - 3 modules, step 0.1), canopy |
| nature.bush | **size** (0.6 - 1.4 modules, step 0.05) |

The basic and architecture shapes mask nothing by default. The ranges of the
walkable shapes keep them walkable at every seed at the default lengths (R2.10):
a derived wedge is at most 32 degrees steep, a derived hill's height is at most
half its radius, a derived door opening and arch opening pass the opening
check, and the stairs' step rise limit is never derived.

## Rock forms

`nature.rock` (D1.9) has the parameters `form` (`blob`, `angular` - the
default -, `worn`, `slab`), `size`, `roughness`, `bevels` (0 - 2, default 1;
the blob has none) and `seed`.

- `blob`: the star-shaped blob of seven segments around Y, its dents scaled by
  the roughness.
- The hull forms: the convex hull (`erhe::geometry::shapes::make_convex_hull()`)
  of a jittered Fibonacci sphere scaled to the form's half extents per meter of
  size (angular 10 points, x and z 0.5, y 0.42; worn 22 points, y 0.40; slab 12
  points, y 0.18, z 0.42), each axis jittered by up to 20 percent (angular) or
  12 percent, each point's direction by up to the point spacing and its radius
  by up to 40 percent, both times the roughness. With two bevel passes the
  forms take 9 points (worn 10), which keeps them within the kit's ceiling.
  Each bevel pass is `erhe::geometry::operation::chamfer3()`, the operation of
  MCP `chamfer`, over the whole mesh, with a bevel ratio of 0.22 for the first
  pass and 0.12 for the second; it runs in the generator as other geometry
  operations run on workers (`make_convex_hull()` takes the geogram lock).
- The base: the beveled mesh's vertices above a plane 22 percent (slab 25
  percent) of its height above its lowest point and its edges' crossings of
  that plane, welded within 1 mm per meter of size, are hulled again; the
  triangles on the plane become one base polygon at y = 0 and the origin is the
  average of its corners. The second hull also makes the result exactly convex
  with planar facets, which the chamfer's best-fit hexagons are not.

The rock collides as a convex hull. At the defaults it has 112 triangles
(budget 120). Over seeds 0 - 39 and roughness 0, 0.3 and 1 the hull forms have
at most 32 (angular), 60 (worn) and 38 (slab) triangles without a bevel, 120,
272 and 138 with one, and 396, 460 and 392 with two; the blob has 42.

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
`Brush_placement.brush` names the kit brush whose primitive its mesh carries.

## Kit brushes

`Level_kits` (`level_kits.hpp`) is the editor part that owns the shape library
and the kit brush cache, reached as `App_context::level_kits`. The cache maps
the mesh key - shape id plus canonical text, `Level_kits::get_mesh_key()` - to
a `Brush`. `get_brush(values)` returns the cached brush or makes it:

- The brush's geometry generator runs the shape's generator and
  `Geometry::process()` (connect, edges, facet texture coordinates). A
  faceted shape renders with polygon normals; a smooth shape with corner
  normals averaged over the facets within 80 degrees of each other, so a base
  rim stays sharp; a shape with hard edges with corner normals averaged
  within its stated angle.
- Collision: a `convex_hull` shape takes the brush's convex hull; a
  `triangle_mesh` shape (stairs, door wall, arch, land mass, tree) has a
  collision shape generator that builds a triangle mesh shape from the
  generated mesh (`build_triangle_mesh_shape()`,
  `scene/collision_shape_from_mesh.hpp`).
- Kit brushes belong to no content library: a scene file carries pieces and
  no kit brushes. Every piece of one mesh key shares the brush's geometry,
  GPU buffers and collision shape.

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
of an earlier placement of the same call, position, rotation, name,
material or none, surface options) in the scene root's space. It checks
every placement first (known shape, values of that shape, values in range,
parent index earlier than the piece, a unit surface direction) and places
nothing when one fails, reporting the index and the message. Then, per
placement:

- the defaults resolve against the scene's level settings;
- with `Kit_snap::module` the position rounds to multiples of the module;
- the material is the given one, else the palette material of the shape's
  slot, found in the scene's content library by name or created and inserted
  there the first time a piece needs it;
- the surface options of the placement (`Kit_placement::surface`, "Surface
  placement") move and turn the module-snapped transform;
- `make_brush_instance_insert()` (`brushes/brush.hpp`) makes the node with a
  static rigid body and its insert operation; the node takes the given name
  or the shape's display name and its `Kit_piece` record before the insert.

All material inserts and node inserts of the call form one
`Compound_operation`, run through `Operation_stack::execute_now()`: one undo
entry, which removes every piece of the call and the palette materials it
added.

## Parameter changes

`Level_kits::set_parameters(node, values)` gives a placed piece new parameter
values as one undo entry. It refuses, changing nothing, a node that is not a
mesh carrying `Kit_piece`, values of another shape and values out of range
(the message names the parameter and its range). Values equal to the piece's
record change nothing and record no entry. Otherwise it gets the brush of the
new values from the cache and executes one `Kit_piece_parameters_operation`
(`kit_piece_operation.hpp`), which swaps, under the scene's item host mutex:

- the mesh primitives: each keeps its material and takes the brush's
  primitive (`swap_mesh_primitives()`, so the raytrace instances follow);
- the collision shape: the brush's, through the node's physics system, which
  rebuilds the static body (a node without a body keeps its shape);
- `Brush_placement.brush` and `Kit_piece.parameters`.

Undo swaps all four back. The node keeps its id, name, transform, children,
materials and body settings; other pieces of the old values keep the old
brush. The body's mass value stays as placed.

Live drags (D3.6, R3.4). `Level_kits::preview_parameters(node, values)` shows
values on a piece without an operation: its first call remembers the piece's
mesh primitives, collision shape, brush and record, and every call gives the
mesh primitives the primitive of the values' brush (materials kept, raytrace
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
display name (its id and description as tooltip), one row per parameter and,
for stairs, the layout line. A piece whose record does not read shows its shape
id and the problem instead. A multi-selection shows the group for no piece, as
the window's other hand-written groups. The Level Kit window's Selection section
is the same editor ("Level Kit window").

## Surface placement

`find_surface_placement()` (`level_queries.hpp`) computes the resting
transform of a mesh from its node-space bounds, a start pose (position,
rotation, scale in the scene root's space) and `Surface_options` (`snap`,
`orient`, a unit `direction`, down by default). It commits the scene's
raytrace scene and casts rays along the direction against content meshes;
hits on the excluded meshes - the ones being moved - are passed through.

- Every ray starts one piece height back against the direction: on the
  plane of the bounds' top face, above its base point. The resting distance
  is measured from the base, so it is negative when the piece rises, and a
  piece rests on the highest surface within its own height above its base or
  anywhere below it: a piece placed sunk into a slab or a hill comes up onto
  it, and a roof higher than the piece above its base is not a surface.
- Snap casts above the base center and the four base corners (the bounds'
  minimum Y face) and moves the mesh along the direction by the smallest
  resting distance, so the base rests on the highest surface under any of
  them; a piece hanging over an edge rests on the edge.
- Orient casts from the top center only. The mesh turns by the shortest
  rotation that takes its up axis to the hit's normal (turned to face against
  the ray), which keeps its heading, and its base center comes to rest on the
  hit point. Orient with snap is orient.
- A ray restarts 0.1 mm past each hit on an excluded mesh. When the first hit
  after such a restart leaves a solid (its geometric normal points along the
  ray), the solid's entry face lay on the passed face - a moved piece resting
  on a surface - so the passed face's distance and plane are the hit.
- With no hit the pose is unchanged and `surface_found` is false.

Placement computes the resting transform before the insert, so the pieces of
one `place()` call are not surfaces for each other: each rests on what was in
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
pair has one owner: `Level_kits::get_surface_settings()`,
`set_surface_snap()` and `set_surface_orient()`, which touch the settings store.
Every widget writes through them at its change
(`level/surface_settings_ui.hpp`), so all of them show the same state:

- the Level Kit window's checkboxes;
- two toggle buttons in every viewport's toolbar, after the Move / Rotate /
  Scale buttons (icons arrow-collapse-down and slope-uphill, tooltips "Snap To
  Surface" and "Orient To Surface"; they report a checked state to the item
  recorder, so `get_imgui_items` lists them as checkable items named
  "Snap To Surface" and "Orient To Surface");
- the rows "Snap To Surface" and "Orient To Surface" of the Transform window's
  Move tool group, after "Snap Enable".

Kit placement in the viewport reads the pair through
`get_placement_settings()`. MCP `get_surface_snap_settings {}` reads it and
`set_surface_snap_settings {snap, orient}` sets it (editor state, no undo
entry; an omitted argument keeps its value); both reply `snap` and `orient`.
The MCP placement and transform tools never read it: they take their own
surface arguments.

### Transform tool

With the pair on, a translate drag of the gizmo rests the moved meshes on the
surface under them at every step (D4.6). After `Transform_tool::adjust_translation()`
writes the step's transforms, `apply_drag_surface_step()` runs
`Transform_tool::rest_on_surface()`:

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
  N entries, M rested: T ms" of `editor.transform_tool` times it. A step over
  50 pieces takes about 1.7 ms (headless release build).

Rules:

- Axis rule: a pointer drag whose only free axis is parallel to the surface
  direction (the Y arrow, with the default direction down) is left as the
  tool computed it, so a piece lifts off a surface. Axis arrows along other
  axes and the plane handles snap.
- Only translate drags rest: rotate and scale drags are left as computed
  (the scale tool has three write paths, and orient would fight a rotation).
- The drag is one undo entry, recorded at release from the rested transforms;
  its undo restores the transforms from before the drag, rotation included.
- The gizmo reads the pair when the drag starts. The Transform window's
  numeric edits do not rest.

MCP calls take explicit arguments instead of the UI pair
(`doc/agents/mcp_api_guidelines.md`):

- `transform_selection {..., surface_snap, surface_orient}` (both default
  false): after all edits of the call, `rest_on_surface()` with those options;
  they need a translation of a node selection. The reply adds `surface_snap`,
  `surface_orient`, `surface_found_count` and per node `surface_found`.
- `drag_selection {..., surface_snap, surface_orient}` (both default false,
  read when the drag starts; refused on a rotate or scale drag): every step of
  the translate drag rests as a gizmo drag does, without the axis rule. The
  reply adds both options and per node `surface_found` and
  `world_rotation_xyzw`.
- `set_node_transform` does not rest.

## Persistence

A level saves and loads as any erhe scene (`doc/editor/scene_serialization.md`).
The file holds each piece as a node with an ordinary glTF mesh of its own, so a
viewer without the erhe extensions shows the right geometry (R8.2), and the
node's `ERHE_node` properties carry the `Kit_piece` record. What a piece is right
after the file is read:

- its record, transform, name and palette material (the palette materials come
  back by name, so a placement after the load finds them and adds none);
- a static body with a collision shape `KHR_physics_rigid_bodies` rebuilt from
  the piece's own saved mesh (a convex hull or a triangle mesh), one per piece;
- a mesh of its own: pieces of equal values no longer share one, and a changed
  shape definition would not reach it (R8.1, R8.3);
- no `Brush_placement.brush` (`doc/editor/scene_serialization.md` "What is not
  persisted").

So `finish_open_scene_gltf()` (`parsers/gltf.cpp`), the tail of every
erhe-authored glTF open (`load_scene`, File > Load Scene, `--scene`, the
asynchronous load included), calls `Level_kits::refresh_scene()` once the nodes
are in the scene and before the navigation item's input check. For each node
carrying `Kit_piece` it reads the record (`read_piece_values()`: known shape,
readable text, values in range) and gives the node the cached brush of those
values the way a parameter change does, without an undo entry: the brush's
primitive in place of the loaded one (each primitive keeps its material), the
brush's collision shape (the body keeps its motion mode and settings and is
rebuilt around the shape) and `Brush_placement.brush`. The collision shape is
replaced even when the kit is unchanged, because R8.3 makes it follow the kit
and the brush's one shape is shared by all pieces of the mesh key. A prepared
run keeps the meshes it was built with.

A piece whose shape the kits no longer have, or whose parameters do not read or
are out of range, keeps the loaded mesh and collision shape (R8.4) and is named
in one warning of the `editor.scene` log (`Kit piece '<name>' (id <id>) keeps
its loaded mesh: <problem>`); one info line counts the pieces that follow and
the ones that do not. `Level_kits::check_piece()` reports per piece
`follows_kit` (its mesh is the kit brush's primitive and its
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
  description). Choosing a shape fills the parameter rows with its defaults at
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
  has. Reset To Defaults fills the rows again and hands the tool their brush.
- Snap To Module, Snap To Surface, Orient To Surface, New Seed Per Piece and
  Rotation Step (90, 45 or 15 degrees): the editor setting `level_kit`
  (`Level_kit_config` version 4, `editor_settings.json`; on, off, 90) for
  the module snap, the seed and the step, and the shared pair `surface_snap`
  for the surface options ("Surface settings").
  With New Seed Per Piece each click's piece is followed by
  `Brush_tool::take_next_kit_seed()`: the tool's kit values take
  `kit_next_seed()`, derive their masked values at the scene's lengths and hand
  the preview the brush of the next piece; the tool sends
  `Kit_values_changed_message` and the window's rows take its values.
- Selection: for the selected piece of the scene (the active item when it is
  one, else the first selected piece) its name and shape, and its parameter rows
  ("Parameter rows"). Snap Selected To Surface and
  Orient Selected To Surface run `Level_kits::snap_to_surface()` - the code
  path of `level_snap_to_surface` - over the selected meshes of the scene, one
  undo entry.

The hand-overs and the row commits run through `Imgui_windows::queue()`
(`doc/editor/coding_rules.md` "ImGui-time work"), because a new brush uploads
its mesh.

In kit mode the Brush tool keeps its hover, preview and insert machinery and
takes the piece's transform from `make_placement_pose()` ("Surface
placement"): the point is the hovered point on content, else the grid-snapped
point on the grid; the rotation steps count the tool's rotate commands and
start at zero when another shape is chosen. The preview mesh carries a material
of the palette color of the shape's slot (`Level_kits::get_preview_material()`,
outside every content library) and sits at exactly that pose; it is no surface
for the rays, while placed pieces are, so a piece placed over a placed block
rests on it. A click places one piece at that pose through `Level_kits::place()`
(without a further snap): its record, palette material and static body, one
undo entry. The tool keeps the selection, as its face-aligned insert does, and
stays in kit mode for the next piece.

## MCP

- `level_get_kits {scene_name}`: the kits and, per shape, its id, kind,
  description, tags, parameters (type, unit, range, the default resolved
  against the scene's level settings, its scale and factor, an enum's values;
  `derive` `fixed` or `derivable`, `default_derived`, and for a derivable float
  or int `derive_min` and `derive_max`, a float's resolved to meters with its
  `derive_step`), `default_derive` (the default mask's names),
  `default_size` and `default_bounds` of the mesh at the defaults (default
  mask, default seed), material
  slot with its palette material, connectors (none yet), normal style,
  triangle budget and collision; the scene's level lengths and the palette.
- `level_place_pieces {scene_name, snap, surface_snap, surface_orient,
  surface_direction, pieces: [{shape, parameters, position, rotation_xyzw,
  name, material, parent_id, parent_index, surface_snap, surface_orient}]}`:
  one undo entry. `surface_snap` and `surface_orient` are booleans (default
  false) a piece may override; `surface_direction` defaults to `[0, -1, 0]`. `parameters.derive`
  lists the parameters derived from the seed, replacing the default mask; the
  masked values are derived at the scene's lengths. A value given for a masked
  parameter, or a name in `derive` that is not a derivable parameter, fails
  the call naming it.
  Unknown keys, an unknown shape or parameter, a value of the wrong type or
  out of range, a rotation that is not a unit quaternion
  (`make_unit_quaternion()`), a zero direction or a missing material fails the
  call with the piece index, and nothing is placed. The reply lists per piece
  `id`, `name`, `shape`, the effective `parameters` (with `derive`, the masked names), the placed `position` and
  `rotation_xyzw`, `surface_found` (false also without a surface option),
  `world_aabb`, `triangle_count`, `material`, `brush_id` and `mesh_key`;
  pieces of equal `brush_id` share one mesh.
- `level_set_piece_parameters {scene_name, node_id, parameters}`: the given
  parameters merge over the piece's current values (`derive` replaces the
  mask; a value for a masked parameter fails naming it), the masked values are
  derived again when the seed or the mask changed, and `set_parameters()`
  applies them. The reply: `id`, `name`, `shape`, `changed`, the effective
  `parameters`, `mesh_key`, `brush_id`, `world_aabb`, `triangle_count`.
- `level_snap_to_surface {scene_name, node_ids, orient, direction,
  query_only}`: `Level_kits::snap_to_surface()`, which runs
  `find_surface_placement()` with snap, and orient when asked, for any mesh
  nodes, the listed nodes excluded from the hits. The nodes that found a
  surface move by one `Node_transform_operation` each, all in one undo entry
  (none when no node moves); `query_only` moves nothing. The
  reply: `moved_count` and per node `id`, `name`, the resting world
  `position` and `rotation_xyzw`, `distance` and `surface_found`.
- `level_get_placement_preview {}` (read-only): `active` (kit mode),
  `tool_active` (the Brush tool is the priority tool), `rotation_step_degrees`,
  `yaw_steps`, `hover_position` and `visible`; in kit mode also `shape`,
  `parameters`, `mesh_key`, `brush_id` and the brush's own `bounds`; with a
  visible preview also its `position`, `rotation_xyzw`, `surface_found` and
  `world_aabb`, the transform a click places the next piece at.
- `get_surface_snap_settings {}` and `set_surface_snap_settings {snap, orient}`:
  the shared pair ("Surface settings").
- `level_get_pieces {scene_name}`: every node carrying `Kit_piece`, in node id
  order: its record (`parameters` with `derive`, and `parameters_text`), `mesh_key`, world
  position and rotation, `world_aabb`, material, `brush_id`,
  `shares_brush_primitive` (the node's mesh primitive is its brush's),
  `follows_kit` and `problem` ("Persistence") and `physics` (motion mode,
  collision shape, whether the body exists).

## Walkable area reads

A level's walkable area is its scene's navigation mesh (`doc/editor/navigation.md`):
pieces are input of the build as any mesh is, a seed node marks where the
viewer stands, and a parameter change of a piece makes the mesh stale
("geometry changed") until the next build. `get_navigation_summary`,
`navigation_find_point` and `navigation_find_path` read the islands, the
nearest walkable point and the path between two points
(`doc/editor/navigation.md` "Islands and queries").

## Verification

- `editor_level_tests`: the mesh checks of every shape at its defaults and at
  the ends of its ranges, the triangle ceiling, equal meshes for equal values
  and seeds, the range messages, walkability at defaults and a palette
  material for every slot. Derive from seed: derived values inside their
  ranges on their steps for 200 seeds, equal for equal seeds and varied across
  seeds, independent of the other parameters, ranges following the lengths,
  walkable shapes walkable at 300 seeds with every derivable parameter masked,
  the canonical text round trip with a mask, a refused mask on a fixed
  parameter, texts without a seed or with a bad `derive` refused, and
  `kit_next_seed()`. Every rock form at 0, 1 and 2 bevels passes the mesh
  checks and the triangle ceiling over 40 seeds and three roughness values; the
  counts are test properties (`--gtest_output=xml`).
- `python3 scripts/level_tools_verify.py` (headless editor): the catalog lists
  14 shapes; the 14 shapes placed at their defaults in one row match their
  default sizes, stand on the ground, echo the catalog defaults and carry
  their palette materials, with static bodies (a triangle mesh for the
  stairs, a convex hull for the block); equal values share one mesh, different
  values do not, and a later call reuses the mesh; undo removes a batch in one
  step and redo restores it; out-of-range values, unknown keys, parameters
  and shapes and a scaled rotation fail the call and place nothing; snapping
  rounds to the module; defaults and snapping follow `set_scene_settings`
  level lengths. Parameter changes: a stair's new rise and bounds, the node
  keeping its id, name, transform and material and a static body with the new
  triangle mesh shape, a second piece of the old values unaffected, undo and
  redo, a refused out-of-range value and a refused node that is not a piece,
  and `cached_brush_count` falling back after `clear_undo_history` for a
  brush no piece uses. Surface placement: a block snapped onto a slab's top, a
  yawed bush and a yawed block oriented to a wedge's slope normal with the
  tilt as the only change, `level_snap_to_surface` with `query_only` and as
  one undo entry, a block over a slab's edge resting on the slab, and a
  placement over empty space reporting `surface_found` false; a sunk block
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
  entry and `seed.new` takes the next seed as one. The run gives back `config/editor/editor_settings.json`
  and `desktop_windows.json` as they were. Surface settings, in a scene of its
  own with a slab, a wedge and blocks, driving the gizmo with
  `get_transform_handles` and held `mouse_drag`s: the pair shows in the Level
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
  `--only-walkable` runs the walkable level, persistence (of the walkable
  level) and window checks alone, `--only-transform` the surface settings
  checks.
- `Mcp_test.document_edits_record_one_undo_entry_each` places three pieces as
  one undo entry, changes a piece's parameters and snaps a raised piece onto
  the one below it, each one undo entry.

## Future work

- [Level tools: requirements](../plans/level_tools.md)
- [Level tools: design](../plans/level_tools_design.md)
- [Level tools: implementation steps](../plans/level_tools_implementation.md)
