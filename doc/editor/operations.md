# operations/

Stability: stable

## Purpose

Implements the undo/redo operation system and all concrete editor operations.

## Key Types

- **`Operation`** -- Abstract base class with `execute(App_context&)` and `undo(App_context&)`. Has a unique serial ID, a description string, and an error state (`set_error`/`get_error`/`has_error`). Operations that fail set the error instead of asserting.

- **`Operation_stack`** -- Manages three vectors: `m_queued`, `m_executed`, `m_undone`. Operations are queued via `queue()`, then executed during `update()` (called once per frame). Undo moves from `m_executed` to `m_undone`; redo moves back. Also an `Imgui_window` that displays the operation history. Binds Ctrl+Z/Ctrl+Y for undo/redo. An operation that is in error after its first execute (`queue()` / `update()`, `execute_now()`, or an undo group) is not recorded: it is logged, it does not join the undo history or the group, and it leaves the redo history in place. Execute is all or nothing: an operation reports such an error only when it changed nothing - it checks before it mutates, and `Mesh_operation` and `Separate_selection_operation` return at the top of `execute()` when an error was recorded while their entries were built (one mesh failing validation leaves every mesh unchanged). A caller that needs the reason reads `get_error()` after `execute_now()`. A `Compound_operation` propagates a child's first-execute error only with `Compound_child_error::roll_back` (below); by default a failed `Property_edit_operation` child has restored its writes and stays inert inside the compound while its siblings stay applied.

- **`Mesh_operation`** -- Base for operations that modify mesh geometry. Contains a list of `Entry` objects, each storing before/after mesh primitives and node physics. `make_entries()` helper applies a geometry transformation function to all selected meshes. After each geometry transform, the output is sanitized (`Geometry::sanitize()` - fixes degenerate facets and NaN/Inf vertices) and validated (`Geometry::validate()`). When sanitization fixes problems, the pre-operation input geometry is saved to `debug_geometry/` as a `.geogram` file for investigation. A result with no facets (every face deleted, a merge by distance that collapses the mesh) is a legal result: it yields an empty `Primitive` that renders and raytraces nothing (`doc/erhe/primitive.md` "Empty primitives"), the mesh keeps it (no rigid body, since it has no convex hull), and undo restores the previous primitive like for any other operation.

- **`Compound_operation`** -- Groups multiple operations into a single undo step; undo runs the children in reverse. `Compound_operation::Parameters::child_error` says what the first execute does with a child that is in error after its own first execute: `Compound_child_error::keep_siblings` (the default) leaves that child inert and its siblings applied, and the compound is recorded; `Compound_child_error::roll_back` undoes the children executed before it in reverse order and gives the compound the child's error, so `Operation_stack` does not record it and nothing of the compound stays applied. The variant switch's compound uses `roll_back` (`make_select_variant_operation`, below).

- **Geometry operations** (all extend `Mesh_operation`):
  - `Catmull_clark_subdivision_operation`, `Sqrt3_subdivision_operation`
  - Conway operators: `Dual`, `Ambo`, `Truncate`, `Kis`, `Join`, `Meta`, `Gyro`, `Chamfer`, `Subdivide`
  - `Triangulate`, `Reverse`, `Normalize`, `Repair`, `Weld`
  - `Generate_tangents`, `Make_raytrace`, `Bake_transform`
  - `Merge_faces`
  - Delete and dissolve on the mesh-component selection
    (`doc/plans/mesh_modeling.md` section 4.3, `erhe_geometry/operation/dissolve.hpp`):
    `Delete_components` (a `Delete_context`: vertices, edges, faces, only
    edges and faces, only faces), `Dissolve_faces`, `Dissolve_edges`,
    `Dissolve_vertices` (each with its library options class) and
    `Dissolve_limited`. They read the active mode's set of the
    `Mesh_operation_parameters::component_selection` snapshot, emit a
    primitive without a selection unchanged, and carry the selection over to
    the result. `Dissolve_limited` runs on the component selection when the
    snapshot holds one, else on the whole of every mesh of the object
    selection.
  - Merge on the mesh-component selection (`doc/plans/mesh_modeling.md`
    section 4.4, `erhe_geometry/operation/merge_vertices.hpp`):
    `Merge_vertices` (a `Merge_vertices_options`: at center, at position,
    at first, at last, collapse, and the UVs option) and `Merge_by_distance`
    (a `Merge_by_distance_options`: threshold, centroid, include
    unselected). The library merges the vertices of whichever set the
    snapshot holds (vertices, the endpoints of edges, the vertices of
    facets), so both work in vertex, edge and face mode, and the survivor is
    the selection afterwards. The selection has no history order, so at
    first / at last keep the lowest / highest selected vertex index. The
    editor has no 3D cursor: at position takes the point in each mesh's
    local space - `Merge_vertices_operation` converts a world point (the
    component tool's last hovered content point,
    `Mesh_component_selection_tool::get_hovered_content_position()`) per
    mesh, and the MCP tool passes a mesh-local `position`.
    `Merge_by_distance` runs on the component selection when the snapshot
    holds one, else on the whole of every mesh of the object selection.
  - `Subdivide_edges` (`doc/plans/mesh_modeling.md` section 4.5,
    `erhe_geometry/operation/subdivide_edges.hpp`, a
    `Subdivide_edges_options`: cuts, smoothness, only quads) on the
    mesh-component selection: the library derives the edges from the
    snapshot's set (`get_selection_edges()`: the selected edges, every edge
    of the selected facets, every edge between two selected vertices). In
    edge mode the operation installs the result's inner edges (the edges the
    fills created) as the selection, falling back to the split halves of the
    general remap when no fill ran; in vertex and face mode the general
    remap stays (the selected vertices, the facets descended from the
    selected facets).
  - Split and rip (`doc/plans/mesh_modeling.md` catalog M9,
    `erhe_geometry/operation/split_components.hpp`) on the mesh-component
    selection: `Split_components_operation` splits the region (face mode:
    the selected facets; vertex / edge mode: the facets fully selected) off
    the rest, and in edge mode, when the selected edges hold no complete
    facet, tears the mesh along them (edge split); the region (or the torn
    side's edge copies) is the selection afterwards.
    `Rip_vertices_operation` (a `Rip_options` direction, or a world point
    converted per mesh into the direction from the torn vertices' centroid)
    rips in vertex and edge mode and selects the ripped vertices or edges.
    Positions do not change; the editor has no grab after the rip, so the
    user moves the ripped selection with the transform gizmo or a slide.
  - Fill and connect vertex path (`doc/plans/mesh_modeling.md` section
    4.10, catalog M15 and M16, `erhe_geometry/operation/fill.hpp` and
    `connect_vertices.hpp`) on the mesh-component selection:
    `Fill_operation` runs `fill_selection()` (two vertices closing a
    boundary chain, a free vertex plus a chain, edge cycles and chains or an
    edge net, the selected faces joined, or three or more vertices sorted
    around their centre - the first case that creates anything) and
    selects the new faces; nothing to fill leaves the geometry unchanged.
    `Connect_vertices_operation` runs `connect_selection()` (two vertices
    sharing no face: the cutting plane path; otherwise facet splits between
    the selected corners) and selects the new edges with the selected and
    inserted vertices.
  - Bridge edge loops (`doc/plans/mesh_modeling.md` section 4.10, catalog
    M14, `erhe_geometry/operation/bridge_loops.hpp`):
    `Bridge_loops_operation` runs `bridge_loops()` with its
    `Bridge_loops_options` on the mesh-component selection (face mode: the
    selected faces are deleted and their region boundaries bridged; edge
    and vertex mode: the loops of the selected edges) and selects the
    bridge faces; an invalid loop selection leaves the geometry unchanged.
  - Normals (catalog M10, `erhe_geometry/operation/flip_facets.hpp`):
    `Flip_facets_operation` runs `flip_facets()` on the facets of
    `get_selection_facets()` of the snapshot; `Recalculate_normals_operation`
    runs `recalculate_facet_normals()` with its `Normal_side` on those facets,
    or, when the snapshot is empty (object mode), on every facet of each
    selected mesh (a primitive without a selection in a component mode is
    emitted unchanged). Both keep every index and the selection.

- **Binary operations** (extend `Compound_operation`): `Union`, `Intersection`, `Difference` -- CSG operations.

- **Scene operations**:
  - `Item_insert_remove_operation` -- insert/remove prims from the scene tree; also how a content-library resource enters and leaves a scene (`make_library_attach_operation` / `make_resource_insert_operation`, `operations/library_attach_operation.hpp`, which place the resource prim under an explicit prim or, without one, under its kind `Scope`; the attach form also records the library's per-resource bookkeeping)
  - `Item_parent_change_operation` -- reparent any `erhe::Hierarchy`: scene nodes, content-library resource prims and folder `Scope`s alike (the Hierarchy drag and MCP `reparent_item`)
  - `Item_reposition_in_parent_operation` -- reorder siblings
  - `Node_transform_operation` -- undo/redo node transforms
  - `Property_set_operation` -- one property's local state (value, expression or none) before / after; the Properties window rows
  - `Property_edit_operation` -- the property writes of an edit function, recorded on the first execute (see "Property_edit_operation" below); the Properties window's Paste Properties, MCP `set_item_properties` / `set_item_property` (every MCP property edit: lights, cameras, materials, the physics items, the scene item's `ambient_light`), the property opinions of a variant switch and the Hierarchy window's no-transform-update flag
  - `Lightmap_tile_overrides_operation` -- a scene's lightmap quadtree leaf overrides before / after (the Lightmap window's and MCP's subdivide / merge); execute and undo let the Lightmap window re-prepare a live partition
  - `Collision_shape_set_operation` -- the collision shape a node's rigid body is made from, before / after (MCP `set_collision_shape`): `Node_physics_system::set_collision_shape`, which recreates a live body. The before state is the node's authored shape (`Node_physics_system::get_authored_collision_shape`: the held shape without the center-of-mass wrapper the system adds for a nonzero `center_of_mass_offset`), so undo restores the same body shape and the offset is applied once. `Mesh_operation::capture_physics` takes the same authored shape for its versions.
  - `Scene_settings_set_operation` -- a scene's per-scene setting overrides (the codegen `Scene_settings`) before / after (MCP `set_scene_settings`). Execute and undo assign the struct and notify directly each consumer that keeps derived state from a field that changed, decided once at construction from the serialized fields: `camera_controls` -> `Fly_camera_tool::on_scene_camera_controls_changed()` (re-adopts the controls when the scene is hovered), `lightmap_tile_overrides` -> `Lightmap_window::on_tile_overrides_changed()`. `sky`, `grid`, `physics` and `shadow_frustum_fit` are read through `scene_settings_resolve.hpp` where they are used (sky and grid rendering, the lightmap bake's sky, the physics step and drags, the shadow fit), so nothing caches them; `clear_color` and `post_processing` have no reader. `scene_id` and `variant_selections` are managed by the scene (side-data identity; `Variant_select_operation`), and before and after must agree on them (verified).
  - `Merge_operation` -- merge multiple meshes
  - `Separate_selection_operation` (`operations/separate_operation.hpp`) --
    Blender's separate selection (P): the facets of each mesh's component
    selection (`get_selection_facets()`) become a new `Mesh` named
    `<original> separated` under the original's parent, at the sibling index
    after it, with its transform, material per primitive, persistent flags
    and layer; the original keeps the rest
    (`erhe::geometry::operation::extract_facets()`). The constructor builds
    both geometries and primitives on the main thread from a
    `snapshot_component_selection()` (`items.hpp`); execute swaps the kept
    primitives in and inserts the node, undo removes the node (remembering
    its parent and index for redo) and swaps the original's primitives
    back, one undo entry. The original's rigid body follows the kept
    geometry's convex hull (`Mesh_operation::make_convex_hull_collision_shape()`)
    with its motion mode; the new mesh gets a static body only when the
    original's is static. The object selection is unchanged and the
    original's new geometry has no component selection. Separate by loose
    parts and by material are not implemented.

- **In-place vertex edits** (NOT `Mesh_operation`: they mutate and reuse the SAME `Geometry` object so `Mesh_component_selection` entries keyed on the Geometry pointer survive, then rebuild one `Primitive` and share it across every mesh referencing the Geometry):
  - `Move_mesh_vertices_operation` -- moves a vertex set of one primitive (mesh-component transform commit) and, for a slide, sets the corner texcoords the slide re-interpolated (`doc/editor/transform.md` "Scalar edits"); refreshes normals, rebuilds static physics.
  - `Paint_weights_operation` -- rewrites `vertex_joint_indices_0` / `vertex_joint_weights_0` of a vertex set (one `Weight_paint_tool` stroke); no physics or normal work (positions unchanged), but the primitive rebuild refreshes the solid-wireframe / edge-line streams that carry their own copy of the joint data.

- **`Operations`** window -- ImGui window providing buttons for all geometry operations. Its "Components" section holds the delete and dissolve buttons, each enabled in the component mode whose set it reads (Delete Vertices and Dissolve Vertices in vertex mode; Delete Edges, Delete Only Edges and Faces and Dissolve Edges in edge mode; Delete Faces, Delete Only Faces and Dissolve Faces in face mode; Limited Dissolve with a component or a mesh selection), and the dissolve options as widgets; the options are `Operations` members read by the buttons and the `Geometry.Dissolve.*` commands. The commands are `Geometry.Delete.Vertices` / `.Edges` / `.Faces` / `.OnlyEdgesAndFaces` / `.OnlyFaces`, `Geometry.Dissolve.Faces` / `.Edges` / `.Vertices` / `.Limited`, and the mode-dispatching `Geometry.Delete.Selected` (Delete) and `Geometry.Dissolve.Selected` (Ctrl+X) of `doc/editor/mesh_component_selection.md`. The same section holds the merge buttons - Merge at Center, at Cursor, at First, at Last, Collapse (enabled with a selection in vertex, edge or face mode) and Merge by Distance (with a component or a mesh selection) - with the UVs checkbox and the by-distance threshold, centroid and include-unselected widgets as `Operations` members; the commands are `Geometry.Merge.AtCenter` (M) / `.AtCursor` / `.AtFirst` / `.AtLast` / `.Collapse` / `.ByDistance`. `Geometry.Merge.AtCursor` logs and does nothing when no content point is hovered. The Subdivide Edges button (enabled with a selection in vertex, edge or face mode) comes with the cuts, smoothness and only-quads widgets as `Operations` members; its command is `Geometry.Subdivide.Edges` (no key: Blender reaches it from a menu). The Split and Separate buttons (enabled with a selection in vertex, edge or face mode) and the Rip button (vertex or edge mode) run `Geometry.Split.Selected` (Y), `Geometry.Separate.Selection` (P) and `Geometry.Rip.Selected` (V; toward the component tool's last hovered content point); each declines without a live selection in a mode it reads. The Fill button (vertex, edge or face mode) and the Connect button (vertex or edge mode) run `Geometry.Fill.Selected` (F) and `Geometry.Connect.Selected` (J). The Bridge Edge Loops button (vertex, edge or face mode) comes with the connect loops combo (open loop, closed loop, loop pairs), the merge checkbox, merge factor, twist and cuts widgets (`Operations::m_bridge_loops_options`); its command is `Geometry.Bridge.Loops` (no key: Blender reaches it from a menu). The Flip Normals button (vertex, edge or face mode) runs `Geometry.Normals.Flip`; Recalculate Outside and Recalculate Inside (a component selection, or a mesh selection in object mode) run `Geometry.Normals.RecalculateOutside` (Shift+N) and `Geometry.Normals.RecalculateInside`. The Smooth Vertices button (vertex, edge or face mode) comes with the smoothing factor and repeat widgets (`Operations::m_smooth_vertices_options`); its command `Geometry.Smooth.Vertices` computes the positions with `smooth_vertices()` on the main thread and queues one `Move_mesh_vertices_operation` "Smooth Vertices" per affected primitive (a `Compound_operation` for several), which edits the Geometry in place, so the component selection survives; it is not a `Mesh_operation`. Every command and button of the section that acts on the component selection goes through one guard (`has_component_mode_selection()` in `operations_window.cpp`): it declines without a live selection in a mode the command reads and while a modal component edit (slide, loop cut, inset, bevel, knife) runs. The Inset button (enabled with a face mode selection) comes with the thickness and depth drags and the boundary, even offset, relative offset, edge rail, outset, individual and interpolate checkboxes (`Operations::m_inset_options`); it runs the numeric inset `Mesh_component_selection_tool::inset()` (one `Fork_geometry_operation` "Inset", `doc/editor/mesh_modeling.md`), not a `Mesh_operation`. The Bevel button (enabled with an edge or vertex mode selection) comes with the amount drag, the offset type combo (offset, width) and the loop slide checkbox (`Operations::m_bevel_options`); it runs the numeric bevel `Mesh_component_selection_tool::bevel()` (one `Fork_geometry_operation` "Bevel", `doc/editor/mesh_modeling.md`), not a `Mesh_operation`.

## Property_edit_operation

`Property_edit_operation` (`operations/property_edit_operation.hpp`) is an
undoable property edit described by a function instead of hand-written
before / after states. It is constructed with a description and an edit
function (`std::function<void()>`) that performs property writes on items:
`set_value`, `clear_value`, `set_expression`, or setters that write
properties (`Light::set_intensity`, `Material::set_data`).

- First execute: the operation opens an
  `erhe::property::Property_write_recording` (`doc/erhe/property.md`
  "Write recording"), runs the edit function once, takes the records and
  releases the function. Each record's object resolves to the item that
  owns it: the item itself, or for a `Mesh_primitive` its mesh
  (`get_owner()`) with the primitive's index (`get_index()`) as the D29
  sub-object index, the form `Property_set_operation` uses. A record whose
  object no live item owns (a `Property_style`, a primitive outside a mesh)
  is a fatal error: that write is not document state the operation can
  restore and belongs in a bespoke operation. The writes have happened, so
  the first execute does not re-apply them; it runs
  `App_context::on_item_property_changed` once per record.
- Redo applies the after states and undo the before states, each through
  `apply_item_property` (`operations/item_property_apply.hpp`, shared with
  `Property_set_operation`, so both reach the same consequences), each followed by `on_item_property_changed`. No
  recording may be open while they apply (verified), so a restore is never
  taken for an edit.
- Undo runs in record order, like redo, not in reverse. A changed callback
  that writes another property during the edit is recorded after the write
  that caused it: an edit of A whose callback writes B records [A, B].
  Undo restores A first (the callback writes B from A's old value) and then
  B's own before state, which puts back a B the user authored
  independently; reverse order would leave the callback's value. For the
  same reason no `Change_batch` is open around the restores: a batch defers
  the changed callbacks to its end, after B was restored, so A's callback
  would overwrite it. Consumers are therefore notified per restored
  property, not once per object.
- The seal (D24) orders an item's records on top of record order. A record
  of the item's own property flagged `Property_flags::writable_when_sealed`
  (`Item_base::lock_edit_property`) is the item's seal record, and every
  other record of that item (sub-object records included, since the item's
  seal covers its sub-objects) is written only while the item is unsealed.
  When undo, redo or the rollback of a refused edit reaches the first record
  of such an item, the item's live sealed state places its seal record: an
  item that is sealed then gets its seal record applied first (the state
  being applied lifts the seal), and an unsealed item gets it applied right
  after its last other record (a state that seals takes effect once they
  are written). Every other record keeps record order. So
  `set_item_properties` with `{"color": ..., "lock_edit": true}` undoes the
  seal before restoring the color, and an edit that lifts the seal and then
  renames the item restores the name before re-sealing it. An edit that
  unseals an item, writes it and re-seals it within the one edit has a seal
  record whose before and after states both seal, which no order can
  restore: its rollback, undo and redo log the refused writes as errors.
- A state that `apply_item_property` refuses during undo or redo is logged
  as an error naming the operation, property and item: the item no longer
  accepts what the operation recorded (an object value the D28 host check
  now refuses, a sub-object that is gone), so the undo history and the
  document disagree from that point. It is an error log rather than a
  verify because the cause is state changed outside the undo history, not
  a defect of the operation.
- Refusals: a write a gate refused during the edit (read-only, sealed,
  validate, bridge validate such as a sibling-unique name), a write on a
  sub-object of a sealed item (D24: a mesh primitive carries no seal of its
  own, so the store accepts it, but `apply_item_property` refuses it), an
  object value
  the D28 host check of `apply_item_property` refuses (`is_item_reference_allowed`,
  applied after the edit to the after state of every recorded property, so
  it covers object values written through setters too), or an edit that
  wrote nothing puts the operation in error naming the property and item.
  The writes that did happen are restored from their before states (in
  the seal order above, without the consequence hook, since the edit never
  reported them), the operation keeps no records, and `Operation_stack`
  does not record it.
- The edit function is released at the end of the first execute, after the
  records hold their items and after any restore: its captures may be the
  only owners of what it edited.
- Every object value in a before or after state that names a managed asset
  is adopted as an `Asset_reference` usership, as `Property_set_operation`
  does, and `collect_item_references` reports every recorded item and every
  referenced item.
- `get_records()` lists the records (item, sub-object, property, before,
  after) for callers that report what an edit changed.
- Follow-ups (`Property_edit_follow_ups`): constructed with
  `bone_connect`, the first execute also records the
  `Node_transform_operation`s the connected bone rule implies for every
  recorded `Rig.tail` / `Rig.connected` write on an item
  (`rig/bone_connect.hpp`), as `Property_set_operation` does, and runs
  them; redo runs them after the writes and undo undoes them in reverse
  before the restores, so the edit and the moves are one undo step. MCP
  `set_item_properties` uses it; the default `none` records the writes
  only (a variant switch applies file opinions without follow-ups).

Edit functions write only through `set_value` and property setters. A
member write that bypasses the property layer is invisible to the
recording and is lost on undo: `Item_base::set_flag_bits` (the flag
properties are bridges whose setter calls it; write the flag property with
`set_value` instead), `Node::set_parent_from_node` (a transform; that is
`Node_transform_operation`), `Node_physics_system::set_collision_shape`
(that is `Collision_shape_set_operation`), `Item_base::set_name` (write
`Item_base::name_property` with `set_value`), or any setter that assigns a
plain member. Structural changes (insert, remove,
reparent, geometry replacement) stay bespoke operations.

There is no run-time check for such member writes. The item mutation
serial (`erhe::get_item_mutation_serial()`, `item.hpp`) cannot provide one:
it moves on recorded property writes too (a name, a flag bridge such as
the locks or visibility) and on hierarchy edits, and it does not move on
transform, physics or other plain member writes, so a serial comparison
around the edit would both flag correct edits and miss the writes it is
meant to catch.

Edits built on it (doc/plans/property_undo_and_reflective_mcp.md
section 3.3):

- `make_property_set_edit_operation` (`property_edit_operation.hpp`): a
  `Property_set` written as local values on one item. Each entry is checked
  against the item's live state just before it is written (seal,
  `validate_value` with the bridge validate, the D28 host check of an
  object value); a refused entry is skipped with a warning and the rest are
  written - pasting onto two siblings, the second skips the copied name the
  first now holds. Paste Properties builds one per item (entries of
  properties the item's type has and that are writable) in a
  `Compound_operation` with the default `keep_siblings`.
- MCP `set_item_properties` and its one-entry form `set_item_property`
  (`mcp/mcp_server_properties.cpp`): every entry is checked against the
  live state before the operation is built (lookup, read-only, seal,
  parse, `validate_value` with the bridge validate, expression
  compilation, the D28 host check), so one bad entry fails the call and
  nothing is written; the operation, run with `execute_now`, writes the
  entries in property-name order with `set_value`, `set_expression` or
  `clear_value` (a writable computed property through `set_value`, whose
  recording holds the stored property its setter writes). A refusal that
  shows only while it runs (an entry that sets `lock_edit` before a later
  one) is the operation's error, which the reply returns. The reply
  reports each entry's before / after local layer from `get_records()`.
  It runs with `Property_edit_follow_ups::bone_connect`, so a `Rig.tail`
  or `Rig.connected` write moves the connected bones in the same undo
  entry, as the Properties window row does.
- `make_select_variant_operation` (`operations/variant_select_operation.hpp`):
  every opinion and `active` write of the switch, in collection order (the
  sets the left block declares, deepest first, then the switched set, then
  the sets the chosen block declares), is one `Property_edit_operation` in
  the switch's compound. The compound runs the `Variant_select_operation`
  (variant table and `Scene_settings`), then that property edit, then the
  `Node_transform_operation`s and the `Mesh_material_assign_operation`s in
  the same set order: all property writes come before every transform and
  material assignment, which read and write state the property writes do
  not touch. A switch applies the file's opinions only, as an import does:
  it adds none of the follow-ups an interactive edit implies (the
  connected child bones a `Rig.tail` / `Rig.connected` edit through
  `Property_set_operation` moves).
  The compound uses `Compound_child_error::roll_back`: a refused opinion
  write takes the selection entry back, so a switch never records a
  variant whose opinions did not apply; a switch applied at once
  (`Variant_switch_mode::immediate`) returns the error.
- The Hierarchy window's "Set / Clear No Transform Update (Recursive)"
  writes `Item_base::no_transform_update_property` with `set_value` on each
  node whose flag differs.

`editor_operation_tests` (`src/editor/operations/test/`) covers the record
/ undo / redo round trip on two items with an expression surviving undo, a
mesh primitive property, a cascade, a refused write, a refused reference,
a primitive write on a sealed mesh, an edit that writes nothing, and an
edit function that is the only owner of its items, and for the property
bag and the no-transform-update flag an undo / redo round trip of the
item's full property dump with an overwritten expression restored
(`test_migrated_property_edits.cpp`). The test compiles
`property_edit_operation.cpp` and `item_property_apply.cpp`; the editor
functions they call that need the whole editor
(`App_context::on_item_property_changed`, the scene lookup of
`is_item_reference_allowed`, the asset usership) have minimal test
definitions in `editor_glue.cpp`. In the running editor, `mcp_server_tests`
covers the same round trip of the full property dump (`get_item_properties`)
for a material edit through `set_item_properties` (an overwritten
expression restored, an untouched one kept) and for a variant switch of `nested_variants.usda` (every prim).

## Primitive swaps keep the node in place

An operation that replaces a mesh's primitives leaves the mesh node where it
is in the scene tree: same parent, same position among its siblings, so a
geometry edit neither reorders the Scene Hierarchy nor changes the order a
saved scene records.

- The swap goes through `swap_mesh_primitives()`
  (`operations/mesh_primitive_swap.hpp`): `Mesh::set_primitives()` inside the
  `Scene_root::begin_mesh_rt_update()` / `end_mesh_rt_update()` brackets,
  which move the mesh's raytrace instances from the old primitives to the new
  ones. The node stays attached throughout.
- Physics follows separately: `Mesh_operation::restore_physics()` sets the
  node's collision shape and motion mode directly
  (`Node_physics_system::set_collision_shape` recreates the body).
- The in-place vertex edits (`Move_mesh_vertices_operation`,
  `Paint_colors_operation`, `Paint_weights_operation`,
  `Set_geometry_attribute_operation`) share their rebuilt `Primitive` through
  `share_rebuilt_primitive()`: every mesh of the scene that references the
  Geometry gets it, keeping its own material, then its optional per-mesh
  step (the physics rebuild of a vertex move) and a
  `Mesh_geometry_changed_message`.
- `Merge_operation` records each removed source's sibling index when it
  removes it and undo re-inserts the sources in reverse removal order at
  those indices.
- `Separate_selection_operation` swaps the original's primitives in place
  and inserts its new node right after the original; undo takes the node
  out and redo puts it back at the parent and index it had.

`scripts/geometry_edit_node_order_verify.py` checks the sibling order after
Catmull-Clark, attribute and position edits, vertex-selection transforms
(including the fork of shared geometry), merge and separate, each with its
undo.

## Threading and re-entrancy

`Operation_stack` is main-thread-only and takes no lock around execution.
Every entry point (`queue`, `execute_now`, `undo`, `redo`, `update`,
`clear_history`, `begin_group` / `end_group`, `discard_queued`,
`get_queued_count`) verifies `std::this_thread::get_id() ==
App_context::main_thread_id` with an `ERHE_VERIFY` kept in every build
configuration, so an off-thread caller fails loudly instead of racing
silently. Every operation source runs on the main thread: ImGui windows,
tools and context menus (drawn inside `Editor::tick`), command handlers,
MCP handlers (dispatched by `Mcp_server::process_queued_requests()` once
per frame), the startup script and message-bus subscription callbacks.

`queue_from_thread()` is the one entry point legal off the main thread, for
the async mesh-operation completions that finish on a `tf::Executor`
worker: it appends to a mutex-protected inbox that `update()` drains on the
main thread before it executes anything. Work computed off the main thread
reaches the scene that way - compute on a worker, apply through the
main-thread stack - and never by locking around `execute()`. The stack is
constructed inside an init taskflow task, so it captures no owner thread id
in its constructor; `App_context::main_thread_id` is assigned on the main
thread at the top of editor init.

Re-entrancy contract: while an operation is executing or being undone, the
only legal call is `queue()`. `execute_now()`, `undo()`, `redo()` and
`clear_history()` verify `!m_executing`. `update()` drains with an index
loop, so `m_queued` may grow while it drains and an operation queued during
execution runs later in the SAME update pass, in append order: an operation
that queues another one has its full effect within the frame that triggered
it, and the init-time drain sees a fully drained scene before prewarm.

An action that must be a single undo entry composes instead of queueing from
inside `execute()`: `make_import_gltf_operation()` returns the import
compound, and `Scene_open_operation::execute()` runs that compound inline as
part of its own execution, so opening a glTF scene is one Ctrl+Z. Its
`undo()` only unregisters the scene - the imported content stays alive in
the kept `Scene_root`, so redo re-registers it without re-importing.

## Public API / Integration Points

- `Operation_stack::queue()` -- queue an operation for execution
- `Operation_stack::undo()` / `redo()` -- manual undo/redo
- `Operation_stack::update()` -- called once per frame from `Editor::tick()`

## Async Execution

Geometry operations run asynchronously via `async_for_nodes_with_mesh()` (in `items.cpp`), which creates `tf::AsyncTask` handles chained to any pending tasks for the same items. The operation callback runs on a worker thread, creates the `Mesh_operation`, and queues it to the operation stack. `Operation_stack::update()` executes queued operations on the main thread. `App_context::pending_async_ops` and `running_async_ops` (atomic counters) track in-flight operations.

`Async_raytrace_kickoff_operation` (the last sub-op of every glTF import compound, and open-scene / prefab-instantiate flows) launches one such task per mesh node. Each task is the deferred load finalize (doc/editor/async_asset_loading.md): it prepares the Geometry, the real triangle raytrace (replacing the load-time AABB proxy) and, when the load path deferred it, the full edge-lines buffer mesh on the worker without touching the live scene, then enqueues the swap on `App_context::scene_commit_queue` (`Scene_commit_queue`, scene/scene_commit_queue.hpp). `Editor::tick()` flushes that queue first thing every frame, so the commit (`Primitive_shape::commit_real_raytrace`, `Primitive_render_shape::commit_geometry_buffer_mesh`, `Mesh::update_rt_primitives`, raytrace instance detach / re-attach) runs on the main thread before anything else in the tick reads the scene - workers never mutate raytrace scenes or mesh primitives. Every step no-ops fast when the result already exists, so re-kickoffs and eager-load configurations are safe. `Operations::make_raytrace` uses the same two phases. `get_async_status.pending_scene_commits` (and `App_context::get_async_in_flight_count()`) counts commits not yet flushed. The swap is shape-level and shapes are shared (glTF instances, brush instances and prefab clones hold the same `Primitive`), so the commit refreshes every mesh naming a committed primitive, not only the task's own mesh; it reads them from the scene's shape-to-meshes index (`Scene_root::collect_meshes_sharing_primitives`, which fills a caller-owned buffer), so a commit costs the sharers of the committed shapes rather than a walk of every mesh of every layer.

## Dependencies

- erhe::scene, erhe::geometry, erhe::primitive, erhe::physics
- erhe::commands (for undo/redo key bindings)
- editor: App_context, Mesh_memory

## Future work

- [plans/property_undo_and_reflective_mcp.md](../plans/property_undo_and_reflective_mcp.md): property edits recorded instead of hand-coded before / after, and the MCP item-edit tools replaced by one reflective verb.
