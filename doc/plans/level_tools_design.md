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
  name, description, tags, parameters, its material slots (D1.11), normal
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
- D1.10 Mirror symmetry (R5.6). `Kit_shape_definition::mirror_planes` lists
  the planes through the shape's origin its mesh is symmetric about at every
  parameter value, as `Kit_mirror_plane` `x` (the plane x = 0) or `z`. The
  block, floor slab, wall, door wall and arch declare x and z, the wedge and
  the stairs x, the cylinder and the pillar z (a prism of an odd segment count
  is not symmetric about x); the seeded nature shapes declare none.
  `editor_level_tests` reflects the generated mesh of every declaring shape
  across each declared plane, at the defaults, at a seed-derived second
  parameter set and at every enum value, and finds each reflected facet among
  the mesh's own facets.
- D1.11 Material slots (R2.9). `Kit_shape_definition::material_slots` is an
  ordered list of one or a few unique slot names; `nature.tree` has `bark`
  (the trunk) and `foliage` (the canopy), every other starter shape one slot.
  The generator assigns every facet to a slot: `Kit_mesh_builder::set_slot()`
  names the slot index of the facets added after it, and `write()` stores it
  in the facet attribute `kit_slot` of the generated mesh, so the generator
  signature stays `(values, GEO::Mesh&)`. `editor_level_tests` checks every
  shape at its defaults and at a seed-derived second parameter set: each facet
  names a declared slot, each slot has facets and the slot names are unique.
- D1.12 Connectors (R5.3). A connector (`Kit_connector`,
  `level/kit_connector.hpp`) is a name and a frame in the piece's local space:
  a position, a unit outward axis (away from the piece) and a unit up axis
  normal to it. `Kit_shape_definition::get_connectors` computes them from the
  current values, so a frame follows its parameters (a wall's end moves with
  its length, the stairs' top with rise and run); the names and their order are
  the same at every value. A side connector has up +Y; "top" (+Y) and "base"
  (-Y, at the origin) have up +Z, so stacking keeps the heading. A shape
  lists first the connector a new piece of it most often attaches by (base for
  what stands on a surface, start for a wall, bottom for a climb), which the
  window offers by default. The list:
  - `basic.block`: base, top, front, back, left (-X), right, the sides at the
    center of each side's base edge;
  - `basic.floor_slab`: the same names, the sides at the top edge (the walking
    surface, so stairs and slabs meet it flush);
  - `basic.stairs`, `basic.wedge`: bottom (front base edge, +Z), top (back top
    edge, -Z), base;
  - `basic.cylinder`, `architecture.pillar`: base, top;
  - `architecture.wall`: start (-X end), end, base, top, front, back (the faces'
    base centers);
  - `architecture.door_wall`: start, end, base, top, opening_front and
    opening_back (the opening's threshold on each face);
  - `architecture.arch`: start and end (the outer foot ends), base, top (the
    crown's top);
  - the nature shapes: none (their seeded outlines have no stated frame).
  `editor_level_tests` checks every frame unit and orthogonal, the names equal
  at two parameter sets and unique, the first connector of each shape, and
  every shape's frames against the parameters its generator reads.
  - World frames (U22): a connector is a point and frame of its node and
    follows the node's full world matrix, whatever its scale (uniform,
    non-uniform, a parent's, shear, mirror): `transform_connector(connector,
    world_from_node)` carries the position by the matrix, the outward axis
    as a face normal (by the inverse transpose of the linear part) and the
    up axis, which lies in the connector's face, as a direction; both are
    normalized. The outward axis therefore stays normal to the transformed
    face, so two joined end faces are flush under any scale or shear, and up
    is normal to it without a projection. The frame (outward, up, outward x
    up) is right-handed; under a mirror (negative determinant) the third axis
    is that cross product, not the image of the local one. A singular matrix
    (a zero scale axis: the determinant within 1e-6 of the product of the
    column lengths) degenerates the frame: the piece has no world
    connectors, and explicit calls refuse it naming the zero scale axis.
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
  geometry, GPU buffers and the collision shape (R3.1, R3.3). A brush has one
  part per material slot (`Brush_data::part_geometries`, one
  `erhe::primitive::Primitive` each), and an instance's mesh gets one
  `Mesh_primitive` per part with its own material: several primitives on one
  mesh are how an erhe mesh carries several materials, so rendering, raytrace,
  navigation input, glTF save and the material tools handle a piece without
  a kit-specific path. The brush's whole geometry (every slot) stays its
  geometry, so bounds, the convex hull, the triangle mesh collision shape and
  the facet statistics are of all slots; a brush of one part has the whole
  geometry's primitive as its part. `Brush::Scaled::part_primitives` holds
  the parts per scale and `Brush::find_scaled()` finds a scale by any of
  them, so a copy's first primitive names its collision shape.
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
  rotation, name, a material per slot (D3.4), surface options of D7.3) and
  returns the node. It resolves the
  defaults (D1.2), validates (D1.3), gets the brush (D2.2), places it
  through `place_brush_in_scene()` and writes the `Kit_piece` values on
  the node before the insert operation is queued; several placements of
  one call are one `Compound_operation` (R5.10, R6.5).
- D3.3 Changing parameters (R3.4): `Level_kits::set_parameters(node,
  values)` builds one operation that replaces the node's mesh primitives
  (primitive i takes the brush's part i) and collision shape with those of
  the brush for the new values and writes `Kit_piece.parameters`; undo
  restores all three. The node keeps its id, name, transform, children and
  the material of each slot. A mesh whose primitive count is not the
  shape's slot count is refused.
- D3.6 Live parameter drags (R3.4). While a parameter row is dragged,
  every changed value that validates is shown at once:
  `Level_kits::preview_parameters(node, values)` gives the node the part
  primitives of the brush for those values without an operation, and
  remembers the piece's state from before the drag. When the drag ends,
  `set_parameters()` runs the one operation of D3.3 from the remembered
  state to the final values, collision shape included; a drag that ends
  on invalid values, and a piece deselected mid-drag, restore the
  remembered state. The shape being placed updates the Brush tool's kit
  brush the same way on every change. The brushes of the intermediate
  values leave the cache with their last holder (D2.2).
- D3.4 Materials (R2.9). The palette is a list of named flat-color
  materials (`Kit_palette_material`) and, per slot name, a
  `Kit_slot_palette`: the default material and a few alternatives, which
  together cover stone, wood, plaster, foliage, bark, earth and grass.
  `Level_kits` adds a palette material to the scene's content library the
  first time a piece needs it, found again by name, inside the undo entry of
  the edit that needs it. The per-slot assignment of a piece is the materials
  of its mesh primitives, in slot order; `Kit_piece` carries no material. A
  placement gives per slot a `Kit_slot_material` (a scene material, a
  palette material by name, or neither for the slot's default); a parameter
  change, a live drag, a kit layout made from the piece
  (`read_piece_layout_item()` reads the primitives' materials), a duplicate, a mirror, a paste and a save and
  reopen keep the assignments. `Level_kits::set_slot_material(scene_root,
  node, slot, material)` changes one slot as one undo entry: the palette
  material's insert when needed and a `Mesh_material_assign_operation` of
  that primitive; an unchanged slot records nothing.
- D3.5 Following the kit at load (R8.3, R8.4): after a scene loads,
  `Level_kits::refresh_scene()` visits the nodes that carry `Kit_piece`
  and gives each the mesh primitives (materials kept) and collision shape of
  the cached brush for its record; a piece whose shape is unknown, whose
  values the shape rejects, or whose loaded mesh has another primitive count
  than the shape has slots keeps the loaded mesh and is named in one log
  line and in the load report. A file of an older slot layout is not
  migrated: its pieces are that case.

- D3.7 Pattern expansion (R5.4). A run of pieces is a kit layout node
  (D3.9) whose pattern is expanded into one transform per element; the
  elements are placed through the placement path of D3.2, so every piece is
  an ordinary piece with its own `Kit_piece` record and the mesh of its brush
  (D2.2). The expansion (`level/level_repeat.hpp`) is scene-free and tested
  in `editor_level_tests`: `expand_repeat()` takes a `Repeat_pattern` and a
  start transform and returns one transform per element, element 0 at the
  start. The kinds (`Repeat_pattern_kind`):
  - line: a count and a spacing vector in the start frame;
  - grid: counts along the start frame's x and z axes and a spacing per axis,
    x fastest;
  - ring: a count and a radius about the start frame's up axis through a
    center one radius behind the start (against its +z front), a full ring by
    default or an arc whose ends both hold an element; each element faces
    outward (it turns with its angle), or keeps the start rotation
    (`Repeat_ring_facing`);
  - path: a count, a curve (an `erhe::math::Curve_path_table` in the start
    frame) and a step; elements sit at distances 0, step, 2 step ... along
    the curve, a distance within 1e-4 of a step of a control point taking
    that point's distance so an element at a corner takes the leaving span
    (the table's rule). `Repeat_path_orientation` `upright` (the default)
    turns each element about the start frame's up axis to the tangent's
    heading of the up-keeping frame (a vertical span keeps the heading around
    it, the table's fallback); `tangent` puts its x axis on the tangent and
    its up axis on the frame's up. A polyline is a path along a curve of
    corner points.
  More than `c_max_repeat_count` (1024) elements, a count below 1, a zero
  spacing, a non-positive radius, an arc outside (0, 360], a path without a
  curve and a negative path step are refused with an explicit message, as
  the height map's cell limit is (D7.2).

- D3.9 Kit layout node (R5.12). A kit layout node is an ordinary
  `erhe::scene::Xform` node that carries the attached property group
  `Kit_layout` (`level/kit_layout.hpp`, the pattern of `Kit_piece`, D3.1); its
  children are its pieces. The group's serialized values:
  - `Kit_layout.pattern` (the key value): the canonical text of a
    `Kit_layout_pattern` (`level/kit_layout_values.hpp`, scene-free and tested
    in `editor_level_tests`): the kind (line, grid, ring, path, scatter
    (D3.11)), every kind's values (kept across a kind change), a path's
    orientation; a line, grid or ring in the node's own frame with element 0
    at its origin, a scatter in its frame about its origin, a path along its
    curve in world space; JSON with sorted keys and hexadecimal floats as
    D1.3, a key a text written before its value existed lacks reading as the
    default;
  - `Kit_layout.items`: the canonical text of a list of `Kit_layout_item`s,
    each a shape id, the canonical parameter text (D1.3) and a material name
    per slot (D1.11, empty for the slot's default); element i takes item
    i modulo the item count. Each item's `derive` list is its seed mask (D1.8);
  - `Kit_layout.module_snap`, `Kit_layout.surface_snap`,
    `Kit_layout.surface_orient`: the snap settings applied per element (D3.2,
    D7.3, the downward direction);
  - `Kit_layout.per_piece_seed` (Per Piece Seed, U24, U32; the former name
    `Kit_layout.random_seeds` still reads) and `Kit_layout.seed`: on, element i takes the
    seed `derive_layout_element_seed(seed, i)` (the D1.4 integer stream of the
    layout seed and the index) and its item's masked parameters derive from
    it;
  - `Kit_layout.locked`.
  The node type follows the kind (R5.12.8): a line, grid or ring layout is an
  `erhe::scene::Xform`, a path layout an `erhe::scene::Curve` prim carrying
  the values, its own curve the path (`doc/plans/curves.md` C6, D4), with the
  pieces its children. `check_layout_kind()` refuses a kind not of the node's
  type in every write and generation, and a node holding one does not read; a
  kind change between path and the others would replace the node, so it is
  refused and the UI offers the kinds of the node's type.
  Count modes (R5.12.7): a line, a ring and a path have
  `Kit_layout_count_mode` `count` or `gap`; a grid states its counts.
  `resolve_layout_pattern(pattern, item_extent, path_table, out_run)` turns
  either into a count mode `Repeat_pattern` for `expand_repeat()` (D3.7) from
  the node's origin, a path's along the node's own curve table. Count
  mode: a line is `count` elements `step` apart along `direction`, a ring
  `count` elements over its arc, a path `count` elements spread evenly over
  its curve, an open curve with both ends holding one, a closed curve around
  it without one twice at the seam. Gap mode: the step is the item extent
  plus the gap, the item extent being the largest extent along x of the
  items' brushes (the run stays regular); the space is a line's `length`
  (first to last center), a ring's arc length at its radius and a curve's
  length (the node keeps scale 1). A line, a path along an open curve and an open arc hold
  floor(space / step) + 1 centers spread evenly from the start to the end of
  the space; a full ring and a closed curve close and hold floor(space /
  step) elements evenly apart. The remainder spreads over the gaps (the stated gap
  is a minimum), an exact fit within 1e-4 of a step counts, and a space
  shorter than one step holds element 0 alone. A determined count above
  `c_max_repeat_count` is refused with the space and step in the message, as
  is a gap that leaves no positive step.
  `Level_kits::build_layout_generation()` (`level/level_kits_layout.cpp`)
  makes one `Kit_placement` per element at the node's world transform times
  the element's pose (every kind in the node's frame, so moving or turning the
  node moves the run rigidly), then the module snap and the surface options with the
  node's current children excluded from the rays. Child i is kept when it
  equals element i (record, slot materials by name, local pose, no children of
  its own); the others are removed with their subtrees, last first, and the
  missing elements are placed at their indices through the placement path of
  D3.2 (`prepare_placements()`, the part of `place()` that builds the
  operations). A node whose world scale is not 1 is refused. The generation
  runs only inside these operations, never per frame:
  - `create_layout()`: the node's insert (a path: a Curve prim of the given
    path, else straight along its x axis sized from count and step) and its
    generated children;
  - `make_layout_from_piece()`: a placed piece's remove, then the node's
    insert at the piece's parent, index and world transform with the piece's
    shape, values and slot materials as the one item (a path: a Curve prim),
    and its generated children, the piece excluded from the surface rays;
  - `make_layout_on_curve()`: a plain Curve prim (no children, no values)
    takes the values, one `Property_set_operation` each, and a locked
    layout's generated children;
  - `set_layout()`: a `Property_set_operation` per changed value and, for a
    locked node, the generation keeping matching children; an unlocked node
    takes the values alone, checked as a generation reads them;
  - `set_layout_lock()`: unlocking writes the lock value alone; locking
    writes it and replaces every child with the generated ones, the remove
    operations holding the user's children (nodes, records, meshes, collision
    shapes, bodies), so undo puts exactly them back and unlocks;
  - `refresh_layout()` of a locked node, no entry when every child matches; a
    level settings change does not regenerate (item values are stored
    resolved), the refresh applies a new module;
  - `append_layout_move_operations()`: the generation of each locked layout
    node at or below moved nodes whose surface snap or orient is on, appended
    to the move's entry at the Transform tool's drag end and in
    `set_node_transform`; without one the children ride rigidly;
  - a curve edit: `Curve_set_operation` records the generation of the curve
    when it is a locked path layout as its follow-ups
    (`append_curve_follow_ups()`, the mechanism of `Property_set_operation`),
    so the edit, its undo and its redo carry the pieces with them.
  - the live layout preview (`begin_layout_preview()`,
    `step_layout_preview()`, `preview_layout_values()`,
    `end_layout_preview()`): a curve point drag, a Properties value drag and
    a Transform tool move of surface-snapped layouts lay the layouts out
    again at every step from the children before the drag, without an
    entry; the end gives those children back and the gesture's commit
    records the one entry above.
  Each is one undo entry.
  Locked, the children are not edited directly. `Selection` never holds a
  child of a locked node: selecting one, by viewport click, item tree or MCP,
  selects the outermost locked layout node above it, which covers every
  selection-driven tool; a node that becomes locked takes over its selected
  children (`App_context::on_item_property_changed()`). The structure
  predicates of prefab instances (`instance_structure_refusal()`,
  `instance_child_refusal()` and their bool forms) also refuse an item below
  a locked kit layout node and an insert under one, which covers delete,
  reparent, paste, create and the group tools; the id-addressed value edits
  (`Level_kits::set_parameters()`, `preview_parameters()`,
  `set_slot_material()` and every MCP tool that edits an item it names by id,
  the geometry tools through their shared target helper) refuse a child
  through `locked_layout_refusal()`; the Operations window's geometry
  operations refuse a locked layout node and its children among their items,
  the mesh component selection tool does not pick a child, and the item
  tree's child rows offer no rename and no node edits. Each refusal names the
  node and the layout node. A `Kit_layout` value written outside the entry
  points (a Properties row, `set_item_property`) is a
  `Property_set_operation` whose follow-ups are the generation a locked
  node's new values need (one undo entry); a value the generation refuses is
  taken back. Unlocked, no generation runs for the node and its children
  are plain pieces. `duplicate()`, copy and paste and delete with undo work on
  the one node through the ordinary node paths (a path layout's copy is a
  Curve prim with its own curve and copied children); `mirror()` moves a
  locked layout node as a unit and reports it as not exact, and reflects a
  curve's points across its local x plane in the same entry, so a path
  layout lies along the mirrored curve. A level saves the children as ordinary pieces
  (R8.1); at load they are taken as saved and follow the kit as other pieces
  do (D3.5).
- D3.8 Groups (R5.6, R5.10). A group is an ordinary empty `erhe::scene::Xform`
  node that is the parent of its pieces; there is no group record, because
  the saved glTF node hierarchy is the group (R8) and the UI tells a group by
  its type. Groups nest. The scene-free part (`level/level_group.hpp`, tested
  in `editor_level_tests`) is `mirror_pose()` and `get_group_origin()`. The
  `Level_kits` entry points are each one operation:
  - `group(scene_root, nodes, name)`: the top-level nodes of the set (a node
    whose ancestor is also given goes with the ancestor) get a new group node
    at the base center of their combined world bounds (of the subtrees'
    meshes, else of the nodes' origins), unrotated, under their common
    parent at the first node's index, else under the scene root. One
    `Compound_operation`: the group's `Item_insert_remove_operation` insert
    and an `Item_parent_change_operation` per node, which keeps world
    transforms. Refused: no nodes, a node listed twice, the scene root, a node
    of another scene, a node inside a reference instance.
  - `ungroup(scene_root, group)`: the `Item_insert_remove_operation` remove of
    the group node, which moves its children to its parent with world
    transforms kept and gives them back on undo. Only an `Xform` is ungrouped.
  - Move and delete are the existing tools: a transform of the group node is
    one `Node_transform_operation` the pieces ride with (the Transform tool,
    `set_node_transform`, `transform_selection`), and `delete_nodes` / Edit >
    Delete removes the subtree as one entry whose undo gives back the same
    nodes, whose collision shapes the physics system keeps across the removal.
  - `duplicate(scene_root, node, copy)`: the node's `clone()` (the subtree,
    nested groups included, every node's values with it: `Kit_piece`,
    `Brush_placement`, `Node_physics`, mesh primitives shared) at the source's
    world transform moved by an offset or at a position, inserted after the
    source. A clone does not carry the collision shapes, which live in the
    scene's `Node_physics_system`; `set_copied_brush_collision_shapes()`
    (`brushes/brush.hpp`) hands each brush instance of the copy the shape of
    its brush scale before the insert, the way `Brush::make_instance()` does.
    Clipboard paste calls it too, so a pasted piece is a full piece as well.
  - `mirror(scene_root, group, axis)`: every node below the group gets the
    pose `mirror_pose()` gives across the plane through the group node's
    origin normal to its local x, y or z axis; the group node stays. The
    forward renderer handles a negative determinant
    (`Item_flags::negative_determinant`, its front-face flip), but a rigid
    body takes only the translation and rotation of its node
    (`Node_physics_system::create_body()`) and a kit piece's collision shape
    carries no reflection, so a mirrored piece's body would not match its
    mesh: no node gets a negative scale. The position is reflected; the rotation is the reflection composed
    with a reflection in the node's own frame across a declared symmetry plane
    (D1.10), which is a rotation and gives the exact mirror image. Of the
    declared planes the one whose normal is closest to the mirror's is used,
    so mirroring twice gives the original and a piece whose plane matches the
    mirror only moves. A piece without a declared plane, and a mesh that is
    not a piece, use local x and are reported as not exact; a node without a
    mesh uses local x and has no image to be exact about. New world
    transforms are computed for all nodes first, then each node's local
    transform under its parent's new one, one `Node_transform_operation` per
    node in one `Compound_operation`.

- D3.10 Replace (R5.8, R5.10). `Level_kits::replace_shape(scene_root, shape,
  replacements)` gives placed pieces another shape as one undo entry: one
  `Kit_piece_parameters_operation` (D3.3) per piece, which also swaps
  `Kit_piece.shape`, after the inserts of the palette materials the new slots
  need, in one `Compound_operation`. The node stays: its id, name, transform
  (bit for bit, not rested again: a caller that wants the new bounds on the
  surface runs the surface tool of D7.3 after, so replace has no surface
  option), parent, children, group and selection. Undo puts back the shape,
  parameters, mesh primitives with their materials, brush and collision
  shape exactly.
  - Parameters (`carry_parameter_values()`, `level/kit_replace.hpp`,
    scene-free and tested in `editor_level_tests`): a parameter of the new
    shape that the old shape has with the same name and type keeps its value,
    a float or int clamped to the new range, an enum by its value name (a
    name the new shape lacks takes the default); a carried parameter that
    derives from the seed on the old shape derives on the new one where it is
    derivable, from the carried seed in the new range; every other parameter
    takes the new shape's default, its default derive mask included. The
    seed is a parameter of every shape, so it carries. A caller's values,
    given per piece, replace the carried ones; values the new shape refuses
    refuse the call. Each parameter is reported carried, clamped or
    defaulted; a value derived again from the seed is carried, whatever its
    old value.
  - Material slots (`match_material_slots()`): a slot of the new shape keeps
    the material of the old slot of the same name, the others take their
    palette default (D3.4); a caller's material per slot overrides both.
  - Physics and navigation: the brush's collision shape replaces the old one
    through the node's physics system, which rebuilds the static body, as a
    parameter change does; the primitive swap reaches the navigation mesh's
    staleness hook like every mesh edit.
  - All or nothing: a node that is not a kit piece of the scene, a kit layout
    node (its items' shapes are its values, D3.9), a child of a locked kit
    layout node (`locked_layout_refusal()`), a node listed twice, a record
    that does not read and a mesh whose primitive count is not its shape's
    slot count (a piece that keeps a loaded mesh, D3.5) refuse the call,
    naming the piece, and nothing changes. A piece of the shape already is
    left as it is; when every piece is, no entry is recorded.
- D3.11 Scatter (R5.5). A scatter is the kit layout kind `scatter`
  (`Repeat_pattern_kind::scatter`), an Xform kind: its pieces are the node's
  children and it generates, locks, refreshes, previews and undoes through
  D3.9 unchanged.
  - Region: a rectangle `size_x` by `size_z` or a disc of `region_radius`
    (`Repeat_scatter_region`), centred on the node's origin in its xz plane;
    a piece's center lies inside it. "Over a surface" is the layout's
    surface snap and orient (D7.3) snapping each piece to what is under it.
  - Count: `scatter_mode` `count` (the shared `count`) or `density` (pieces
    per square meter times the region's area, rounded, at least 1); a count
    above `c_max_repeat_count` is refused. A line's `count_mode` does not
    apply to a scatter.
  - Points (`expand_repeat()`, scene-free, tested in `editor_level_tests`):
    dart throwing over one candidate stream, `Kit_random(Kit_layout.seed,
    "layout scatter")` of D1.4, each candidate three 24-bit `next_unit()`
    draws (x, z, yaw) taken or not; x = (u - 0.5) size_x, a disc's candidate
    outside it rejected; a candidate closer than `min_spacing` to an accepted
    point (squared distance in float, a grid of cells 1 + 2^-10 times the
    spacing or wider, so an index rounded at a cell edge hides no pair) is
    rejected; it stops at the count or after
    `c_scatter_attempts_per_piece` (64) candidates per piece asked for and
    holds the points that fit, `Kit_layout_run::requested` reporting the
    count asked for. Every product and sum is its own statement, so no
    multiply-add is fused under the fp-contract policy of
    `doc/cmake_conventions.md` and the positions in the node's frame are the
    same bits on every platform (the world pose takes the node's rotation as
    every kind's does). `resolve_layout_pattern()` returns the poses it made
    to the generation, so a generation samples once. The stream does not depend on the count or the spacing, so a
    raised count keeps the first pieces.
  - Yaw: each piece turns about the node's up axis by yaw_min + (yaw_max -
    yaw_min) w degrees (within -360 - 360, `yaw_min` <= `yaw_max`); the angle is exact, its
    rotation takes the platform's sine and cosine as every kind's does.
  - Items: element i takes item i modulo the item count, as every kind; with
    Per Piece Seed on, its seed is `derive_layout_element_seed(seed, i)`, a
    stream apart from the points.
  - Scale: a scatter places every piece at scale 1. Per-piece random scale
    needs a scaled brush per piece (a node scale leaves the collision shape
    unscaled, since bodies take no scale) and a scale in the piece record so
    that follows-kit, load and replace keep it: plan step 39
    (`doc/plans/level_tools_implementation.md`).

- D3.12 Connector joins (R5.3, R5.10). `join_connectors()` (scene-free, tested
  in `editor_level_tests`) gives the pose that puts connector A of a piece on
  connector B of a target, both frames given: the piece's outward axis opposes
  B's, its up axis agrees with B's up turned by `twist_steps` quarter turns
  about B's outward axis (right-handed), and A's point lands on B's point moved
  `gap` meters along B's outward axis. Both frames are right-handed
  orthonormal, so the rotation between them always exists and is proper
  (determinant +1, no reflection); there is no case of up axes that cannot
  agree. A side connector against a top connector tips the piece (a wall's end
  on a floor's top lays the wall down), deterministically; the twist picks one
  of the four headings about the joined axis. The translation uses the
  rotation the node gets from its quaternion, so A's world point is B's.
  - World frames: `Level_kits::get_world_connectors(node)` carries the local
    frames of the piece's record values by its world matrix (D1.12 "World
    frames"). The target is not edited, so any kit piece of the scene is a
    target, a child of a locked kit layout node and a scaled piece included.
  - Scaled pieces (U22): `join_motion()` takes the moved piece's connector
    under its current world matrix and gives the rigid world motion (rotation
    and translation) that carries that frame onto the target's: positions
    coincide, outward axes oppose, up axes agree after the twist, the gap
    along the target's world outward axis. The moved piece's new world matrix
    is the motion times its old one, so it keeps its own scale; the new local
    transform is the parent's inverse times that. A Trs local transform holds
    it exactly when its axes stay orthogonal (`has_orthogonal_axes()`): always
    under rotation-only or uniformly scaled parents, and under a non-uniformly
    scaled or sheared parent chain when the motion's turn keeps the axes
    orthogonal (no turn, or a turn about an axis of the scale). Otherwise the
    join is refused naming the piece and parent: the joined pose would need a
    sheared local transform. The local transform keeps the node's stored
    scale outright, a mirrored one included, when the new local matrix is a
    rotation times that scale (columns divided by it unit and orthogonal
    within 1e-5, determinant +1); otherwise it is decomposed. A motion of at
    most 0.1 mm and 1e-4 rad is no change.
  - Placing (`Kit_placement::connect`): the join replaces the position and
    rotation; module snap and the surface options are not applied, because the
    join is exact. It is resolved and refused in the checking pass of `place()`,
    so the call stays all or nothing; the new piece has world scale 1, and a
    parent under which that pose would need a sheared local transform is
    refused there with Moving's message. The target is an existing node, not a
    piece of the same call.
  - Moving (`Level_kits::connect_piece()`): a placed kit piece moves by one
    `Node_transform_operation` to the joined world pose (local under its
    parent), its children riding with it; no entry when it is there already.
    Refused: a node that is not a kit piece (a group or kit layout node has no
    connectors), a child of a locked kit layout node (`locked_layout_refusal()`),
    the piece itself or a node below it as the target, an unknown connector
    name, whose message lists the shape's connectors, a piece or target with a
    zero scale axis, and a join that would need a sheared local transform
    ("Scaled pieces" above). The piece is moved first and the locked-layout move
    hook of D3.9 appended in the same `Compound_operation`, as every move path
    does, so a surface-snapped locked layout below the piece rests again.
  - A joined piece is an ordinary piece afterwards: no link to the target is
    stored, so moving or changing the target does not move it; a connect again
    lands it on the target's new frame. A persistent attachment is plan step 41.
  - Drag-time use: connectors serve both the explicit join and a snap during
    Transform drags (R5.13, plan step 43), so the queries are pure and
    repeatable, not one-shot. `get_shape_connectors()` and
    `transform_connectors()` fill caller-owned vectors and allocate nothing
    once their capacity is reached (a connector's name is a literal);
    `Level_kits::get_local_connectors()` parses a piece's record and is the
    drag-begin read. A drag consumer gathers once at drag begin the kit pieces
    of the scene that carry connectors (the nodes with a `Kit_piece`, excluding
    the moved nodes and their descendants) with their local frames into its
    own cache, and per step only carries those frames by each node's world
    transform; no per-scene list is kept, because the targets do not change
    during a drag and a list maintained on every place, remove, open and
    parameter change would cost every edit for the one consumer.
  - Mirrored pieces: `mirror()` gives a piece a rotation (D3.8), so its
    connectors are its local frames under that rotation. On a mirrored piece of
    a declared symmetry plane the frames sit exactly on the mirrored mesh, but a
    pair named across the plane changes sides (a wall mirrored across its x
    plane has its start where the end was); a piece without a declared plane
    has frames of its rotated, not mirrored, mesh.

- D3.13 Connector snap (R5.13, U21). One setting, `connector_snap_distance`
  in `Surface_snap_config` (meters, 0 = off), shared by the toolbar, the Move
  rows and the Level Kit window as the surface pair is (D4.6), and explicit
  `connector_snap_distance` arguments on `drag_selection` and
  `transform_selection` (default 0).
  - Scene-free search: `find_connector_snap(moved, others, max_distance)`
    (`level/kit_connector.hpp`, tested in `editor_level_tests`) returns the
    closest pair at most `max_distance` apart (inclusive), none for a
    distance that is not positive and finite; equal squared distances take
    the smallest (moved piece id, moved connector index, other piece id,
    other connector index), a total order independent of the input order.
  - Drag consumer: `Kit_connector_drag_snap` (`level/kit_connector_snap.hpp`),
    owned by the Transform tool, gathers at a translate, rotate or scale
    drag's begin (U27), as
    D3.12 foresees: the moved roots (targets not locked and without a moved
    ancestor), the connector positions of every kit piece at or below a root
    in that root's frame, and per other kit piece of the scene (not a drag
    target, not below one) a weak node reference and its local connector
    positions, each through the piece's full world matrix (D1.12 "World
    frames": scaled pieces, scaled targets and pieces under scaled or sheared
    parents take part; a piece whose frames degenerate does not). The snap is
    translation only, so it co-locates the transformed positions. A step multiplies the
    positions by each root's and target's world matrix into reserved
    vectors and searches; no allocation, no per-scene list.
  - Step order: grid snap, translate, snap to surface (D4.6), connector snap,
    layout preview (D3.9). Last because co-located connectors define the
    pose; a snap to surface after it would separate them. Each step starts from the
    step's unsnapped pose, so release beyond the distance needs no latch.
    A rotate or scale step (plan step 56) is the tool's rotation or scale of
    the drag-start pose about the press pivot, then the connector snap (full
    offset; no snap to surface), then the layout preview; the roots' world
    matrices of the step carry the moved connectors through it.
  - Apply: every moved root translates by the pair's offset (translation
    only), the anchor with it; the undo record and cancel are those of any
    drag. A snap that cannot co-locate is not made: a moved root whose locked
    translate channel (in its parent's space) the offset needs leaves the
    step unsnapped. A drag pulling a body through physics does not snap (the
    simulation writes that node).
  - Pairs: any orientation; the outward axes are not compared.
  - Indicators (U22): the drag's gathered points also keep their axes (the
    moved ones in the root's frame, the targets' local), so the connector
    indicators of a drag read them with the current world matrices
    (`collect_indicators()`), and the last step's pair is kept by index.

- D3.14 Connector indicators (U22, plan step 42). `Kit_connector_indicators`
  (`level/kit_connector_indicators.hpp`, owned by `Level_kits`) collects what
  a view draws; `Debug_visualizations::connectors_visualization()` draws it
  and `level_get_connector_indicators` reads the same collection.
  - What: in the views of the drag's scene, during a node drag with a
    connector snap, the drag's moved connectors, the targets within 4 snap distances of one and the snapped
    pair, whatever the setting; otherwise by the per-view setting
    `Debug_visualizations_settings::connectors` (off, selected (default),
    all) the connectors of the selected kit pieces and of those below
    selected nodes, or of every kit piece of the view's scene. The two
    connectors chosen in the Level Kit window's Connect rows are marked.
  - Cache: the local frames per piece by node id, parsed from the record on
    the first draw and dropped on the changes that rewrite the record: a
    `Mesh_geometry_changed_message` of the piece (parameters, replace, their
    undo, the live parameter drag), a raw record write
    (`App_context::on_item_property_changed()`), the piece's removal
    (`Items_removed_message`, looked up in its set: a regenerated layout's
    old children go) and a scene close (all). World matrices are read at
    draw time; no per-frame comparison, and the scratch vectors keep their
    capacity, so a steady frame allocates nothing.
  - Connect choice: the window hands its chosen target and moved connectors
    over when its pair or a choice changes; a selection message clears it,
    and the window hands it over again when it next lists the pair.
  - No name labels: the editor's label path draws per-mesh text and would
    need a per-frame text layout per connector.

## D4 Exposure

- D4.1 UI (R6.2): the "Level Kit" window (`level/level_kit_window.hpp`)
  lists kits and shapes, shows the parameter rows of the shape to place
  and the placement settings "Snap To Module", "Snap To Surface",
  "Orient To Surface" and the rotation step (R5.2, R5.11; kept in the
  editor settings). For selected pieces it has "Snap Selected To Surface"
  and "Orient Selected To Surface" (D7.3) and, for a selected piece, the
  same parameter rows bound to the piece: a row commits one
  `set_parameters()` at `IsItemDeactivatedAfterEdit()`. Under the parameter
  rows of the shape to place and of the selected piece, one combo per slot
  (`<slot> material`, its ImGui id under the slot name) offers the slot's
  default and alternatives: for the shape to place a choice hands the Brush
  tool the slot materials with the brush (its preview shows them, its next
  piece gets them); for the piece it queues one `set_slot_material()` at the
  choice.
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
  piece gets its record, slot materials and static body, and the tool
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
- D4.7 Pattern rows (R5.12.1, R5.12.7, R6.2). `level/kit_layout_editor.hpp`
  draws a `Kit_layout_pattern` with one set of row widgets
  (`layout_pattern_row()`, the rows of the kind and count mode from
  `get_layout_rows()`): the kind (Line, Grid, Ring, Path, as many as the
  target offers), for a line, ring and path the count mode (By Count, By Gap)
  with Count or Gap, a line's Direction and Step or Length, a grid's counts
  and spacings, a ring's Radius, Arc and Face Outward, and a path's
  orientation (Upright, Follow Tangent); `layout_setting_row()` the snap and
  seed rows. The same rows edit the Level Kit window's initial values (names
  beside the widgets) and a selected node's values in the Properties window
  (one row each, D4.9).
- D4.9 Kit layout node (R5.12, R5.12.8, R5.12.9, R6.1). A selected kit layout
  node's values are edited in the Properties window's "Kit Layout" group
  (`Kit_layout_editor`, registered with `add_group_rows` like "Kit Piece";
  selection redirects a locked node's child to it): the pattern rows (D4.7)
  over a copy of its values, the Kind row offering the kinds of the node's
  type (Line, Grid and Ring on an Xform, Path on a Curve prim, whose own
  "Curve" group shows beside it); the resolved count, the step or, in gap
  mode, the actual gap, and the space; per item its parameter rows (D4.5, the
  seed mask toggles included), slot material combos (D4.1) and Remove Item;
  Add Shape Item (the shape the Brush tool places with its values and slot
  materials); the snap and seed rows; Lock (its tooltip warns that locking
  discards the children's edits), Unlock and Refresh. An edit commits one
  `set_layout()`, `set_layout_lock()` or `refresh_layout()` at the edit
  (`IsItemDeactivatedAfterEdit()` for a drag, at once for a radio button,
  checkbox, combo entry or button); a refusal shows in the group's Message
  row. Each change of a drag (or typed character) of a locked node is
  previewed live (`preview_layout_values()`, the live layout preview of
  D3.9) and the commit starts from the state before the drag. Unlocked, the
  rows change the values alone. The copy is read again in
  a queued call on the selection messages, on `Kit_layout_changed_message`,
  which every operation writing a `Kit_layout` value queues
  (`App_context::on_item_property_changed()`), and on a shape change of the
  node's own curve; nothing is recomputed per frame. A raw developer-mode row
  write of a `Kit_layout` value is checked with `check_layout_value_write()`
  before it is queued, as `set_item_property` checks it: a value the node
  cannot take is taken back and records no undo entry.
  The Level Kit window's Layout section (`Kit_layout_initial_editor`) holds
  the initial values of a new layout node (the editor setting `level_kit`,
  `Level_kits::get_layout_initial_values()`): the pattern rows with every
  kind, Spacing From Item (the line step and grid spacings from the item's
  extent, `set_layout_spacing_from_extent()`), the snap and seed rows and
  Locked; editing them changes no node. Make Layout queues
  `make_layout_from_piece_initial()` for the selected piece: one undo entry
  that removes the piece and inserts the node (a path layout: a Curve prim
  straight along the piece's x axis) at its parent, index and world transform
  with its shape, values (seed mask included) and slot materials as the one
  item, so element 0 replaces the piece and undo gives the piece back. Place
  As Layout makes the Brush tool's kit mode click call `add_layout_initial()`
  with the shape it places, element 0 at the preview's pose. The new node is
  selected. MCP: `level_create_layout {scene_name, name, parent_id, position,
  rotation_xyzw, pattern, items | from_piece_id | curve_id, points, closed,
  snap, surface_snap, surface_orient, per_piece_seed, seed, locked}` (`pattern`
  an object of the `Kit_layout_pattern` keys over the defaults, the step and
  grid spacings defaulting to the largest item extent; `items` a list of
  `{shape, parameters, materials}`; `from_piece_id` Make Layout's call, not
  with `items`, `position`, `rotation_xyzw` or `parent_id`; `curve_id` a
  plain curve node made the path layout itself; `points` and `closed` a path
  layout's curve as `create_curve` takes them; locked by default),
  `level_set_layout {scene_name, layout_id, pattern, items, snap,
  surface_snap, surface_orient, per_piece_seed, seed}` (the values given merge
  over the node's: the pattern key by key, the items as a list),
  `level_get_layouts {scene_name}`, `level_set_layout_lock {scene_name,
  layout_id, locked}` and `level_refresh_layout {scene_name, layout_id}`,
  each edit one undo entry; the replies give the node's type, its own curve
  for a path, the values, the run (`count`, `step`, `space`, `item_extent`)
  and the children.
- D4.8 Groups (R5.6, R6.1). The Level Kit window's "Group" section: Group
  Name and Group Selected act on the selected nodes of the scene; Ungroup,
  Duplicate Offset (the group's width along x by default, taken again when
  another group becomes the selected one or a mesh changes), Duplicate,
  Mirror X and Mirror Z act on the selected group node (the active item when
  it is a selected `Xform`, else the first selected one). A press queues the
  `Level_kits` call (`Imgui_windows::queue()`) and writes its result or
  refusal under the buttons; Group Selected and Duplicate select the new
  node. MCP: `level_group_nodes {scene_name, node_ids, name}`,
  `level_ungroup {scene_name, group_id}`, `level_duplicate_group
  {scene_name, group_id, offset | position, name}` and `level_mirror_group
  {scene_name, group_id, axis}`, each one undo entry; the replies list the
  nodes with ids and world transforms as `level_place_pieces` lists pieces,
  and the mirror's lists the nodes whose image is not exact. Move and delete
  have no level tool of their own (D3.8).
- D4.10 Replace (R5.8, R6.1). The Level Kit window's Selection section has
  Replace With (a combo of every shape, `<display name> (<id>)`) and Replace
  Selected, which queues `replace_shape()` for the selected kit pieces of the
  scene with the carried values and matched slots, and writes the result or
  refusal under it (and to the log). The button is disabled with its reason
  shown while no kit piece of the scene is selected or no shape is chosen;
  the count of selected pieces is taken again on the selection messages, not
  per frame. MCP: `level_replace_pieces {scene_name, node_ids, shape,
  parameters, materials}` (no `node_ids`: the selected kit pieces of the
  scene; `parameters` merge over each piece's carried values as
  `level_set_piece_parameters` merges them; `materials` as
  `level_place_pieces` takes them), one undo entry; the reply gives per piece
  its `id`, `name`, `old_shape`, `shape`, `replaced`, the effective
  `parameters`, the names `carried`, `clamped`, `defaulted` and `given`, the
  `materials`, the `kept_materials` slots, `world_aabb` and `collision_shape`.
- D4.11 Connectors (R5.3, R6.1). The Level Kit window's Connect rows
  (`doc/editor/level_tools.md` "Level Kit window") take the active selected
  piece as the target, which every press keeps still, and move the other of
  two selected pieces or place the shape to place; the rows hold connector
  names only (the record's local connectors), and the queued
  `connect_piece()` or `place()` refuses what the scene no longer allows, so
  the rows read nothing per frame and need no message beyond the selection,
  active item and mesh ones. MCP: `level_get_kits` connectors at the defaults,
  `level_get_connectors` the world frames, `level_place_pieces` per piece
  `connect` (refused with a position, rotation or surface option) and
  `level_connect_pieces` for `connect_piece()`. Viewport placement by the
  Brush tool's preview is plan step 40; the frames are drawn as connector
  indicators (D3.14).
- D4.2 MCP (R6.3 - R6.5; handlers in `mcp/mcp_server_level.cpp`):
  - `level_get_kits {scene_name}`: the catalog of R6.3, with the float
    defaults resolved against the scene's level settings; per shape its
    `material_slots` (slot, default material, alternatives) and the palette.
  - `level_place_pieces {scene_name, snap, surface_snap, surface_orient,
    pieces: [{shape, parameters, position, rotation_xyzw, name, materials,
    parent_id, parent_index}]}`: one undo entry; `materials` maps slot names
    to scene or palette material names; returns per piece `id`, `name`,
    `shape`, the effective `parameters`, the placed `position` and
    `rotation_xyzw`, `surface_found`, `world_aabb`, `triangle_count`,
    `materials`.
    `snap` is `none` (default) or `module` (positions round to the module,
    R5.2); `surface_snap` and `surface_orient` (both default off) are the
    options of D7.3, and a piece may state its own.
  - `level_snap_to_surface {scene_name, node_ids, orient, direction,
    query_only}`: D7.3 on placed pieces, one undo entry; returns per node
    the snapped `position`, `rotation_xyzw`, `distance` and
    `surface_found`.
  - `level_get_pieces {scene_name}`: every piece with shape, parameters,
    transform, world bounds and `materials` per slot.
  - `level_set_piece_parameters {scene_name, node_id, parameters}`: D3.3;
    the given parameters merge over the piece's current values.
  - `level_set_piece_material {scene_name, node_id, slot, material}`:
    `set_slot_material()` (D3.4), one undo entry. The generic
    `assign_mesh_material` reaches one primitive by index with a material the
    scene holds; this tool names the slot and takes palette materials the
    scene does not hold yet, which the window's combos need too (D4.3).
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
- D7.3 Surface placement (R5.11, R10.5, U23, U30): `find_surface_placement()`
  takes a piece's mesh bounds, a start transform, a direction (down by
  default), a maximum distance (1 m by default) and the two options, and
  returns the snapped transform: the nearest place along the direction,
  either way and within the distance, where the piece's base rests on a
  surface and the piece fits along the direction. Through the scene's
  raytrace scene it gathers the hit points of the meshes that are not moved
  on the line through each base point (the base center, and the base corners
  too when only snapping), within the search window, measured from the base
  point, with hit filters so the moved pieces are invisible. A place is a
  displacement at which one line's base point lies on a hit whose face is
  against the direction and no line has a hit strictly inside the piece's
  span; solids are not reconstructed (U31), so a piece inside a solid taller
  than itself can rest on a surface inside it. The smallest displacement
  wins, a tie going along the direction. No place within the distance
  leaves the pose unchanged with no surface found. A snapped pose is its own
  nearest place, so snapping it again with the same excluded meshes moves it
  by nothing (`doc/editor/level_tools.md` "Surface placement", which also
  names each caller's excluded meshes and the order that picks the normal
  where faces tie at an edge). Orient rotates the piece about its base
  center by the shortest rotation that takes its up axis to the normal of
  the face at the place, which keeps the heading of an upright piece, and
  rests the base center there; the turn moves no line, so a rested oriented
  pose turns and moves by nothing.
  Placement (D3.2) applies it to the new piece before the insert; the tool
  form applies it to placed pieces as one transform operation.
- D7.4 Support report (R10.4): one ray down from the bounds' top plane above
  the base center and above each base corner of D7.3 (1 mm inside the
  footprint); a piece whose smallest distance from the base to a hit exceeds
  the tolerance is unsupported.
- D7.5 Overlap report (R10.3): bounds as the first pass, then the physics
  world's body intersection test with the tolerance.
- D7.6 Level check (R10.7): runs D7.4, D7.5, the unreachable islands of
  D6.1 above the area threshold and R2.10 over the pieces' parameter
  values.
