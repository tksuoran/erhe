# Lattice deform

Stability: mostly stable

Free-form deformation (Sederberg and Parry 1986) of geometry through a regular
control-point cage, modeled on the Houdini Lattice SOP
(https://www.sidefx.com/docs/houdini/nodes/sop/lattice.html). It exists in both
of erhe's geometry-processing layers, split the way every geometry operation is:

| Layer | Code |
|---|---|
| Math / library operation | `src/erhe/geometry/erhe_geometry/operation/lattice_deform.{hpp,cpp}` |
| Graph node | `src/editor/geometry_graph/nodes/lattice_node.{hpp,cpp}` |
| Viewport tool | `src/editor/tools/lattice_tool.cpp`, `src/editor/transform/lattice_point_transform.cpp` |

## Control points are node parameters

Houdini derives the deformation from two extra geometry inputs, a rest cage and
a deformed cage. erhe's graph has no node for editing the individual points of
a geometry, so a faithful three-input port would be inert - there would be no
way to author the deformed cage. The node therefore owns the cage definition
(bounds plus divisions) and a control-point offset array, edited in the node UI
and the Node Properties window. That matches Blender's Lattice-modifier mental
model and serializes through `write_parameters` / `read_parameters`, which the
background shadow evaluation requires anyway.

## Deformation math

The cage is an axis-aligned box `[cage_min, cage_max]` subdivided into
`divisions = (nx, ny, nz)` cells, giving `(nx+1)*(ny+1)*(nz+1)` control points.
Each control point `P_ijk` has a rest position on the regular grid and a
user-edited offset `O_ijk`, zero by default. **Displacement is interpolated
from the offsets, not from absolute control point positions** - algebraically
equivalent, since both bases have linear precision, but it makes the identity
fast path and the clamping trivial.

For each source vertex `p`:

1. Normalize into cage-local coordinates
   `(s,t,u) = (p - cage_min) / (cage_max - cage_min)`, clamped to `[0,1]^3`.
   Clamping is Houdini's default for points outside the cage: an outside point
   moves rigidly with the nearest cage face, which is continuous and leaves no
   cracks.
2. Interpolate the offset:
   - **Trilinear** (Houdini "Linear", order 2): locate the cell containing
     `(s,t,u)` and trilinearly interpolate the 8 surrounding offsets. Local
     control, C0 across cell boundaries.
   - **Bezier** (Bernstein basis over the full grid): the classic FFD
     formulation, `sum_ijk B_i(s) B_j(t) B_k(u) * O_ijk`. Global smooth
     influence.
3. Identity fast path: when every offset is zero the node passes the input
   through copy-on-write, with no geometry copy at all. Both bases reproduce
   positions exactly for zero offsets (Bernstein partition of unity plus linear
   precision), so the fast path is safe in either mode.

Invalid parameters pass through with a warning; a degenerate cage axis (a flat
input) is padded rather than rejected. Changing the divisions resamples the
existing offset field trilinearly, and toggling Auto Fit off freezes the
currently fitted bounds into the manual cage fields.

## Attribute handling

Topology is unchanged, so this is a position-only deform like `Smooth`:

- The mesh and its attributes are copied 1:1, then the vertex positions are
  rewritten (erhe meshes are single precision).
- Normals and tangents are invalidated by a non-affine map, so a
  `regenerate_attributes` flag follows the `Smooth` precedent: on by default,
  running `process()` with
  `compute_facet_centroids | compute_smooth_vertex_normals` plus the tangent
  flags when the source had tangents; off keeps the source normals, which is
  acceptable for a mild deformation and cheaper.
- Facet texture coordinates are never regenerated: topology and
  parametrization survive the deform.
- `process_flag_connect` and `build_edges` are deliberately not passed -
  connectivity is copied and unchanged.

## The transform driver is the cage frame

The Lattice node has an optional **Transform node** slot: drag a scene node
from the item tree onto the node UI or Node Properties to bind it. The driver
node's transform T is the cage frame - the cage box and the control point
offsets live in the driver's space. Each vertex is mapped into cage space by
inverse(T) to find its lattice coordinates, and its displacement is the
interpolated offset rotated back by T's linear part:

```
v' = v + linear(T) * interp_offsets(clamp((inverse(T) * v - cage_min) / extent))
```

This formulation is the point of the feature, and it is what makes the driver
useful:

- **Zero offsets deform nothing, in any frame.** Moving the driver moves *the
  cage relative to the mesh* - repositioning the deformation region, with the
  outside-cage clamping following the cage - and never moves the geometry
  itself. A whole-cage parent transform would instead reproduce itself through
  the FFD basis (affine precision) and rigidly move the mesh, which is useless
  as a deformer.
- Authored offsets apply within the driver's frame and rotate with it.

**T is the driver's LOCAL, parent-relative transform**, so the intended setup is
to parent the driver under the scene node the graph is bound to. Moving that
parent then moves mesh and cage together with no re-deformation, and moving the
driver moves the cage over the mesh. A driver at scene root behaves the same
when the bound node is at identity.

Plumbing: the reference follows the `Scene_mesh_geometry_node` pattern, an
`Asset_reference` with `Asset_type::node` (scene-local resolution matches scene
nodes by name). Only the driver's *name* serializes; the transform itself is
captured on the main thread and handed to background-evaluation shadows through
`capture_evaluation_state()`. Live tracking uses the per-frame
`Geometry_graph_node::update_live()` hook, called from
`Geometry_graph_window::update_evaluation()` on every live node of every
`Graph_mesh` asset: `Lattice_node::update_live()` re-resolves a deferred
reference, re-captures the driver's local transform and marks the node dirty
when it changed, so dragging the driver re-evaluates the graph. The cage-space
evaluation itself lives in the operation
(`Lattice_deform_parameters::cage_transform`); a non-invertible transform
passes through with a warning.

## Viewport editing

Control points are editable directly in the viewport. The activation contract
is that the graph's **display or ghost designation is on the Lattice node** -
that is what puts the lattice-deformed geometry in the viewport - **and** the
graph's output is bound to a scene node through that node's
`Geometry_graph_mesh.graph_mesh` value in the active scene. While active:

- `Lattice_tool` (a background tool, priority 4) finds the designated lattice
  and the bound scene node, draws the deformed cage wireframe and a billboard
  handle for every control point (orange normal, white hovered, yellow
  selected), and consumes a left click that lands on a control point (a 12 px
  screen-space pick) to select it. The selection is the node's own
  `m_selected_point`, so the viewport and the node UI's offset editor stay in
  sync.
- `Lattice_point_transform` drives the standard transform gizmo, mirroring
  `Mesh_component_transform`: `update_anchor()` places the gizmo at the
  selected point's deformed world position each idle frame
  (`Transform_tool::update_for_view` dispatches through `Component_source`),
  and `begin()` / `apply()` / `commit()` snapshot the node parameters at drag
  start, write the moved offset on each update (`set_control_point_offset` plus
  `mark_dirty`, so the background re-evaluation gives live feedback), and push
  a single undoable `Geometry_graph_parameter_operation` per gesture on
  release. A point drag inverts T (`safe_inverse`) so the stored offsets stay
  in cage space.
- While a lattice designation is active the gizmo belongs to the lattice, the
  way a Houdini display flag works; clear the designation to return the gizmo
  to scene node selection. Mesh component mode (vertex, edge, face) takes
  precedence over lattice mode. Rotate and scale gizmo drags are harmless
  no-ops for a single selected point, which sits at the anchor.

**The operation's own cage wireframe does not reach the viewport.**
`Lattice_deform_parameters::make_cage_debug_lines` emits
`Geometry::add_debug_line` entries on the payload geometry, but the output
node's bake copies the geometry with `copy_with_transform()`, which drops debug
entries, and the only debug-draw path (`Hover_tool`) reads the baked copy.
`Lattice_tool` is what draws the cage in the viewport.

## Registration

A node type is registered at six sites, traced from how `transform` is
registered: the factory (`geometry_graph_node_factory.cpp`), the palette
(`geometry_graph_window.cpp`, `build_palette()`), the editor `CMakeLists.txt`,
the `geometry_graph_add_node` type enum in `mcp_server_tool_list.cpp`,
`scripts/geometry_nodes_smoke_test.py`, and the node list in
`doc/editor/geometry_nodes.md`. Everything else comes free: undo/redo, JSON save and
load, copy and paste, Node Properties, per-node mesh previews, display and
ghost designation, and MCP parameter edits.

The lattice sits in the "Operations" palette category; a "Deform" category is
worth starting once a second deformer (bend, twist, taper) exists.

## Verification

- `src/erhe/geometry/test/test_lattice_deform.cpp` - identity, uniform-offset
  translation, trilinear locality, outside-cage clamping, invalid-parameter
  pass-through. Pure math, no editor.
- `scripts/geometry_nodes_smoke_test.py` inserts a lattice node in the
  operation chain, sets a nonzero offset and asserts the vertex count is
  preserved and the graph evaluates.
- Headless MCP: box -> lattice -> output, push one corner offset, then
  `capture_screenshot` and verify the box bulges.
- Save and reload a lattice graph asset and confirm identical evaluation. That
  guards the serialization contract the shadow evaluator depends on.

Division cap: 16 per axis, which keeps the offset array at most 4913 points and
the JSON small. Houdini allows 30 for Bezier; raise it if a cage needs it.

The cage lives in the geometry's local space, before instancing. Deforming
after instancing would interact with `Realize_instances_node` ordering.

## Future work

- [plans/geometry_graph/geometry_nodes.md](../plans/geometry_graph/geometry_nodes.md) -
  cage geometry input pins, B-spline interpolation, falloff, a point-deform
  node, selection restriction and a toolbar operation.
