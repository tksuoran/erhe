# Geometry nodes: field system and further node types

Status: proposed

This plan extends `doc/editor/geometry_nodes.md`, which describes the geometry node
graph as it is. The field system below is designed but not built; the node
types after it are a backlog.

## Field system

Per-element lazy computation over geometry domains, enabling a Set Position
node (deform by a per-vertex expression), selection masks, and later
field-driven instance scale. Blender's model, scaled down to erhe's graph. The
design is settled and awaits implementation.

**Payload model - a socket carries a value OR a field**, Blender style. A
field-capable pin keeps the existing `float_value` / `vec3_value` pin keys, so
the current value nodes stay connectable. `Geometry_payload::Variant` gains
`std::shared_ptr<Float_field>` and `std::shared_ptr<Vec3_field>` alternatives.
New accessors `get_float_field(fallback)` / `get_vec3_field(fallback)` promote
a plain value payload (or the fallback) to a constant field, so a consumer
handles exactly one case. Multi-link accumulation keeps the first connected
field - no implicit Add - and logs that once.

**Expression tree.** A field is a type-erased, immutable expression tree shared
through `shared_ptr`, which keeps it copy-on-write friendly and safe to cache
in output payloads:

- `Field_node_base` - abstract; `evaluate(Field_context&)` fills a bulk output
  array for all elements at once.
- Leaves: `Field_constant<T>`, `Field_input_position` (vertex position),
  `Field_input_normal` (vertex normal, computed on demand),
  `Field_input_index` (element index as float).
- Interior: `Field_operation` wrapping a small enum of per-element functions
  (add, sub, mul, div, min, max, sin, cos, length, normalize, dot, cross, mix,
  vector compose and decompose) with child fields.

**Evaluation.** `Field_context` holds `{const Geometry&, domain, element
count}` plus a memo map from tree node to evaluated array, so a shared subtree
of the DAG evaluates once. Consumers drive it: evaluate the tree bottom-up into
scratch `std::vector` arrays and read the root's array. The first version
supports the vertex domain only, with single-threaded bulk loops (graph
evaluation is already off the per-frame hot path); parallel chunking is a later
optimization. There is no fusion pass - a chain of N math nodes produces N
arrays, which is acceptable at editor scale.

**Graph nodes.**

- Input nodes (no inputs, output = field): Position, Normal, Index.
- `Math_node` becomes dual-mode: when any input payload is a field it outputs a
  `Field_operation` wrapping its operator (a cheap tree build); when all inputs
  are plain values it computes eagerly as it does today. A new
  `Vector_math_node` follows the same pattern for `vec3`.
- Consumer: `Set_position_node` - geometry in, a position field (vec3,
  defaulting to the input positions) and a selection field (float, defaulting
  to 1; values >= 0.5 keep the new position). It copies the geometry, evaluates
  the fields over the vertices, writes the positions and re-runs
  `process_for_graph()`.

**What does not change.** Serialization (fields are graph topology, not data),
incremental evaluation (a field-producing node rebuilds a cheap tree; the
consumer does the bulk work) and undo (parameter edits go through the existing
gesture operation).

**Slices**, each buildable and verifiable on its own:

1. `geometry_graph/field.{hpp,cpp}`: the tree classes, the context and the
   driver; the payload alternatives and the promoting accessors.
2. The input nodes and `Set_position_node` with constant fields only. Verify by
   flattening a box with a constant y.
3. Dual-mode `Math_node` and `Vector_math_node`. Verify with a sine-wave
   displacement of a subdivided box: Position -> decompose -> sin -> compose ->
   Set Position.
4. The selection field input on `Set_position_node`.

## Graph mesh asset and binding gaps

`doc/editor/geometry_graph_mesh.md` describes the `Graph_mesh` asset and the
`Geometry_graph_mesh.graph_mesh` binding. These are open:

- **Two binding paths record no operation.** The Properties row and MCP
  `set_item_property` write the value through `Property_set_operation`, so
  they are undoable; MCP `set_node_graph_mesh` and the Hierarchy drag-drop
  still write the value directly. Both should record the operation too.
- **Undoing the creation of a bound asset orphans it.** Undoing
  `create_graph_mesh` while a node is bound leaves the node holding the
  orphaned asset, which keeps rendering.
- **`find_scene("")` does not default to the single scene**, so MCP calls have
  to pass `scene_name` explicitly.
- **`create_new_camera` and `create_new_light` omit `Item_flags::visible`** on
  the nodes they create. `create_new_xform` had the same defect: a child prim
  syncs its visibility from the node on attach, so anything attached to an
  invisible empty node was invisibly stuck.
- **Parallel per-asset evaluation.** The engine runs at most one
  `Evaluation_run` at a time, round-robin over the graphs that need
  evaluating. Running one per asset concurrently is the obvious next step once
  a workload needs it.

## Lattice deform, beyond v1

`doc/editor/lattice_deform_geometry_node.md` describes the lattice node as it is. In
rough priority order:

1. **Rest and deformed cage geometry inputs**, for full Houdini parity: two
   optional `geometry` pins. When both are connected and their vertex counts
   match `(nx+1)(ny+1)(nz+1)` they override the internal offsets. This needs a
   documented and enforced point-ordering convention, and it pairs naturally
   with a "Lattice cage" source node - a Bound-SOP equivalent that makes a box
   wireframe with divisions, auto-fitted to an input geometry.
2. **B-spline interpolation with an order parameter** (Houdini's NURBS mode,
   order 2 to 11): local support with more smoothness than trilinear, needed
   for a high-division cage where global Bezier influence is undesirable.
3. **Falloff outside the cage** (Houdini's Falloff parameter) as an
   alternative to hard clamping.
4. **A Point Deform node** - kernel-weighted deformation from arbitrary
   control geometry (Houdini's Points method). It is a different algorithm, a
   capture pass plus a weighted delta pass, so it belongs in its own node, the
   way Houdini eventually split these workflows too.
5. **Selection restriction**: deform only the selected facets or vertices
   through the existing selective-operation machinery, with a smooth boundary
   blend.
6. **A toolbar `Mesh_operation`** that bakes a lattice into the selected
   meshes, for a workflow outside the graph.

## Scene-node references by uid

`transform_from_node` and `Lattice_node` both persist their scene-node
reference as a NAME (`doc/editor/geometry_graph_transform_from_node.md`), which breaks
on rename and is ambiguous under duplicate names. Scene nodes do have a stable
persistent id, the glTF 2.1 uid (`Item_base::get_gltf_uid()`, stamped at
export). Write `"transform_node_uid"` alongside the name and prefer the uid on
load, matching `ERHE_asset_reference`'s "uid first, name fallback" doctrine.
Upgrade both reference holders in the same change so they behave identically.
Caveat: the uid is empty for a node that has never been saved.

## A `mat4` output pin

The `mat4_value` pin key and payload accessor exist and have no users.
`transform_from_node` could expose the captured matrix as a second output, and
`Transform_node` could gain a `mat4` input to consume it - the first users of
that pin key. Check `Geometry_payload::operator+=` multi-link semantics for
`mat4` before wiring it.

## Further node types

- Convex hull, extrude, merge by distance, set material.
- Attribute nodes: read, write and delete a named attribute.
- A "Set crease" node that selects edges by angle or tag and writes
  `edge_sharpness` (`doc/erhe/subdivision_crease_edges.md`). It is the graph-native
  answer to painting creases, which does not survive on graph-produced
  geometry.
- Curve support, which needs a new geometry type.
