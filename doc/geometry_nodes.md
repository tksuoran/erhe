# Geometry nodes

Stability: mostly stable

The editor's geometry node graph: a directed acyclic graph of nodes that build
and modify `erhe::geometry::Geometry` and feed the result into the scene. It
replicates the core of Blender's Geometry Nodes, scaled down to erhe. All the
code lives in `src/editor/geometry_graph/`.

Related documents: `doc/geometry_graph_mesh.md` (the `Graph_mesh` asset and the
node attachment that binds a scene mesh to a graph), `doc/graph_editor.md` (the
shared graph-editor infrastructure), `doc/texture_graph.md` (the sibling
texture graph).

## Table of Contents

1. [Architecture](#architecture)
2. [Node types](#node-types)
3. [Undo/redo](#undoredo)
4. [Serialization](#serialization)
5. [MCP tools](#mcp-tools)
6. [Verification](#verification)
7. [Blender Geometry Nodes Architecture](#blender-geometry-nodes-architecture)
8. [Key Files Reference](#key-files-reference)
9. [Future work](#future-work)

---

## Architecture

**Payload.** `Geometry_payload` (`geometry_payload.{hpp,cpp}`) is a variant
carrying `shared_ptr<erhe::geometry::Geometry>`, float, `glm::vec3`,
`glm::vec4`, `glm::mat4`, int, bool, `shared_ptr<erhe::primitive::Material>`,
`Point_cloud` (parallel position and normal arrays) and `Geometry_instances`
(entries of shared source geometry plus per-instance transforms). Each payload
type has its own pin key, and `erhe::graph::Graph::connect()` enforces key
equality, so a geometry pin only connects to a geometry pin.

**Evaluation is dirty-flag driven and incremental**, not per-frame: geometry
operations are far too expensive to run every frame the way the older
`Shader_graph` does. `Geometry_graph::evaluate_if_dirty()` re-evaluates only
when topology or a node parameter changed (node widgets call `mark_dirty()`).
Dirtiness propagates along links while nodes are visited in topological order,
clean nodes keep their cached output payloads, and a structural edit marks only
the directly affected nodes dirty at the edit site.

**Copy-on-write is the sharing model.** Geometry flows through the graph as
`shared_ptr` and is copied only by nodes that actually modify it; pass-through
cases share the upstream pointer (a single-link Join, a 0-iteration Subdivide,
an identity Transform). **Upstream geometry is never mutated**: nodes allocate
new `Geometry` objects, and the output node copies before render processing.
Intermediate nodes process their outputs with connect + build_edges only;
render-oriented processing (normals, tangents, texture coordinates) happens
once, in the output node.

**Cycles are refused at connect time.** `erhe::graph::Graph` rejects a
cycle-creating link (`would_create_cycle()`), the window pre-validates before
building the undoable operation, a graph file load rejects a cycle-forming or
key-mismatched link set wholesale, and a group asset with a refused link fails
to load. Without that, `Graph::sort()` fails every frame, the cycle members can
never clear their dirty flags, and the graph re-evaluates forever - a permanent
freeze once a heavy node joins the cycle.

**A node's `evaluate()` runs only on the worker's shadow clone.** Member
state written there is invisible on the live node - finishing a run copies
payloads, previews and products back, nothing else. So a warning an
`imgui()` shows must be derived from the payloads, not from a flag
`evaluate()` set.

**`Geometry_payload::operator+=` runs only on a multi-link pin**
(`make_input_pin(..., true)`). A single-link pin replaces on connect and never
accumulates.

**No popups inside the node canvas.** ImGui `Combo` cannot be used inside
ax::NodeEditor, so enum parameters use arrow-stepper widgets
(`imgui_index_stepper` / `imgui_enum_stepper`).

## Node types

- **Sources**: box, sphere, torus, cone, disc, and the external-item source
  nodes (`geometry_source_nodes`) that capture geometry from a brush or a
  scene mesh.
- **Operations**: subdivide (Catmull-Clark / sqrt3, iteration count), conway
  (all 9 operators via an enum plus a per-operator ratio), boolean (CSG union /
  intersection / difference), transform and lattice deform
  (`doc/lattice_deform_geometry_node.md`).
- **SDF** (`sdf_nodes`, compiled only with `ERHE_VOXEL_LIBRARY=openvdb`, in
  their own palette category with an orchid pin color): `sdf_sphere`,
  `sdf_capsule`, `voxelize` (geometry -> sdf), `sdf_mesh` (sdf -> geometry
  through `volumeToMesh` with an adaptivity parameter, then
  `process_for_graph`), `sdf_boolean` (union / intersection / difference, with
  a multi-link b pin; a voxel-size mismatch passes input a through and warns),
  `sdf_offset` (clamped to twice the background, because
  `LevelSetFilter::offset` steps at about half-voxel CFL rate and an unclamped
  offset looks like a hang) and `sdf_smooth`. A creator node owns a voxel-size
  parameter; an operation node inherits its input grid's resolution. The
  payload alternative is `shared_ptr<erhe::voxel::Grid>` on pin key
  `Geometry_pin_key::sdf`; the header is unconditional because `Grid` is
  pimpl-forward-declarable, and only the accumulate path is guarded.
  Multi-link accumulation on an sdf pin is a union into a new grid (a
  mismatched voxel size keeps the first value). **A grid on a pin is immutable
  by convention** - an operation node deep-copies before mutating - and grids
  are never serialized: they are always re-evaluated from parameters. Grids
  flow only through payloads, so the shadow-clone snapshot model needs no extra
  hooks and the OpenVDB operations are safe on the evaluation worker.
- **Parameterless operations share one class**: triangulate, normalize, reverse
  and repair are instances of `Geometry_unary_operation_node` (a label plus a
  function pointer) rather than four near-identical classes.
- **Combiners and values**: join, math, float / integer / vector constants,
  passthrough, and `transform_from_node`
  (`doc/geometry_graph_transform_from_node.md`).
- **Join has a single multi-link input pin.** Multi-link accumulation in
  `Geometry_payload::operator+=()` merges geometries, so Join needs no A / B
  pins - a Blender-style multi-input socket.
- **Instances**: `Distribute_points_node` scatters points on a surface (facets
  are fan-triangulated, triangles picked with probability proportional to area,
  points sampled uniformly inside the triangle; deterministic per geometry,
  count and seed, and each point carries its facet normal - random sampling,
  not Poisson disk); `Instance_on_points_node` makes one instance transform per
  point, with a uniform scale parameter and optional +Y-to-normal alignment;
  `Realize_instances_node` flattens to real geometry with one
  `merge_with_transform()` per instance. The referenced geometries are never
  mutated, and multi-link accumulation concatenates point clouds and instance
  sets into newly allocated sets.
- **Groups**: `Group_input_node` and `Group_output_node` define the interface
  (one geometry pin each). `Group_node` references a graph asset by path - a
  JSON file the graph window saved that contains the interface nodes - loads it
  into a private subgraph, and evaluates it inline. Groups nest, and a
  thread-local depth guard (8) breaks reference cycles with a warning. The
  shared node factory (`make_geometry_graph_node()`) serves both window graphs
  and group assets. Group editing is the window's file toolbar: load the asset
  file, edit it like any graph, save it back; group nodes pick the changes up
  when their path is re-committed or the graph reloads.
- **Output**: `Geometry_output_node` pipes the result into the scene as
  ordinary content (selectable and movable with the Transform tool), with an
  editable scene node name and optional physics. A source with no facets is
  treated like a disconnected input (primitives cleared, physics attachment
  removed) - feeding an empty geometry to `Primitive_builder` otherwise aborts
  on `ERHE_VERIFY(total_index_count > 0)`.

## Undo/redo

Undo is **structural**: every structural edit (add / remove node, connect /
disconnect link, graph load / clear) goes through the editor `Operation_stack`
(`geometry_graph_operations.hpp`).

- `Geometry_graph_node_insert_remove_operation` - removing a node captures its
  links and canvas position, and undo restores the node exactly, links
  included. Link records hold owning-node `shared_ptr`s so pin pointers stay
  valid while operations sit in the stacks; LIFO undo order guarantees nodes
  are restored before their links reconnect.
- `Geometry_graph_link_insert_remove_operation` - connect and disconnect.
- `Geometry_graph_replace_operation` - whole-graph replacement, used by load
  and clear; it captures the previous nodes, links and positions on first
  execute, so undoing a load restores the prior graph exactly.
- `Geometry_graph_parameter_operation` - node parameter edits. It holds before
  and after state as `write_parameters()` JSON dumps applied through
  `read_parameters()`; the values are already live when the operation is
  pushed, so the first execute only records state. A widget edit commits one
  operation per completed gesture (pushed when the active widget deactivates),
  and the MCP set-parameter tool pushes through the same class.
- `Operation_stack::execute_now()` executes immediately on the main thread and
  records for undo, so toolbar, canvas and MCP edits observe their effects in
  the same frame. Every operation re-evaluates the graph after execute and
  undo, so the scene output stays current even when the graph window is hidden.
- A node leaving the graph gets the `on_removed_from_graph()` hook (deletion,
  undo of an add, clear, load). The node object may stay alive in the undo
  stack, so side effects outside the graph - the output node's scene mesh - are
  released there, not in the destructor.

## Serialization

`save_graph()` / `load_graph()` / `clear_graph()`, JSON version 1:

```json
{
    "version": 1,
    "nodes": [ { "type": "box", "position": [0.0, 0.0], "parameters": {} } ],
    "links": [ { "source_node": 0, "source_slot": 0, "sink_node": 1, "sink_slot": 0 } ]
}
```

- Links reference nodes by index into the `nodes` array, and pins by slot
  index.
- A node's `type` is the factory name
  (`Geometry_graph_node::get_factory_type_name()`, named so it does not clash
  with the `erhe::Item::get_type_name()` virtual); `make_node()` recreates the
  class on load.
- Parameters go through the per-node `write_parameters()` / `read_parameters()`
  virtuals. The output node saves its scene and material by name and
  re-resolves them on load.
- Canvas positions round-trip. ax::NodeEditor reports `ImVec2{FLT_MAX}` for a
  node it has never drawn, so `is_valid_node_position()` filters those on both
  save and restore.
- A malformed graph file fails the load with the graph left unchanged.

## MCP tools

The geometry graph is fully scriptable over the in-editor MCP server:
`get_geometry_graph`, `geometry_graph_add_node`, `geometry_graph_remove_node`,
`geometry_graph_set_parameter`, `geometry_graph_connect`,
`geometry_graph_disconnect`, `geometry_graph_save`, `geometry_graph_load`,
`geometry_graph_clear`. Every mutation is undoable and re-evaluates the graph
immediately, with no window visibility needed. `get_geometry_graph` and
`geometry_graph_add_node` report each node's current `parameters` object and
per-output vertex / facet / point / instance counts;
`geometry_graph_set_parameter` accepts the same shape with partial updates
(an omitted key keeps its current value).

## Verification

`scripts/geometry_nodes_smoke_test.py` sweeps the whole feature against a
running headless editor over MCP, in one long editor session. It covers every
node type with per-node output payload verification, parameter sweeps with
undo/redo round-trips on every parameter of every node type, incremental
evaluation proved from the trace log (editing one chain re-evaluates only that
chain), multi-link join, value -> math -> math chains, vector-driven transform
pins, structural churn (undo to empty and redo back), a save / clear / load
round-trip over a graph containing every node type, output node edge cases
(rename, removal with undo restoring the scene mesh and physics, two outputs at
once, disconnect), stress chains, multi-link partial disconnects, invalid
connect rejection (type mismatch, self link, 2- and 3-node cycles: an MCP
error, no link, no undo entry, evaluation settles), error-path serialization
including group asset errors, nested groups and the self-reference depth guard,
out-of-range parameter abuse, output physics edge cases, and screenshot
checkpoints. A new node type must gain its row in the sweep.

---

## Blender Geometry Nodes Architecture

### Overview

Blender's Geometry Nodes is a visual programming system that constructs and modifies
geometry through a directed acyclic graph (DAG) of nodes. At the DNA level (Blender's
serialized data structures), a Geometry Nodes modifier references a `bNodeTree` of
type `NTREE_GEOMETRY` containing `bNode` objects connected via `bNodeLink` objects
through typed `bNodeSocket` endpoints.

### Data Types Flowing Through Nodes

**GeometrySet -- the primary container:**

The main data type flowing through geometry sockets is `GeometrySet`, a container
that can hold multiple geometry components simultaneously:

| Component       | Description                                            |
|-----------------|--------------------------------------------------------|
| Mesh            | Vertices, edges, faces, face corners (loops)           |
| Curves          | Control points organized into individual curves        |
| Point Cloud     | Unconnected points with attributes                     |
| Instances       | References to other geometry/objects with transforms   |
| Volume          | OpenVDB volume grids                                   |

A single `GeometrySet` can carry mesh AND instances AND point cloud simultaneously.
Nodes operate on the component types they understand and pass others through unchanged.

**Copy-on-Write Semantics:**

`GeometrySet` uses implicit sharing / copy-on-write (CoW). When geometry flows from
one node to multiple downstream nodes, all share the same underlying data. Only when
a node modifies geometry is a copy made. Individual attributes also have independent
sharing -- modifying one attribute does not force a copy of all attributes.

**Other socket types:**

- Float, Integer, Boolean, Vector (float3), Color (float4) -- as single values or fields
- String -- attribute names, file paths
- Object, Collection, Material -- references to other data-blocks
- Rotation (quaternion), Matrix (4x4 transform)

### Evaluation Model

Geometry Nodes uses a pull-based, demand-driven evaluation model:

1. Evaluation starts at the **Group Output** node (terminal node).
2. The evaluator traces each input backward through links to upstream nodes.
3. Upstream nodes are evaluated recursively, pulling from their own inputs.
4. This continues until reaching source nodes (primitives, Group Input, constants).
5. Nodes not connected to the output path are never evaluated.

Internally, Blender has two execution backends:

- **Multi-Function (`fn::MultiFunction`)** -- For per-element field computations.
  Operates on arrays of values in vectorized tight loops.
- **Lazy-Function (`lf::LazyFunction`)** -- Higher-level evaluation framework. Each
  geometry node is wrapped as a lazy-function. Inputs are only requested when needed --
  a `Switch` node evaluates only the condition first, then requests only the chosen
  branch.

The `bNodeTree` is compiled into a lazy-function graph before evaluation. This
compilation step is cached and only rebuilt when tree topology changes.

### Field System (Lazy Per-Element Computation)

A **field** is a function evaluated for every element of a geometry domain to produce
a value. Fields are lazy, composable, per-element computations represented as
expression trees evaluated in bulk.

**Field architecture:**

- `GField` (generic field): Type-erased handle wrapping a `FieldNode` expression tree.
- `FieldInput`: Leaf node that reads data from geometry (e.g., vertex positions,
  element indices, named attributes).
- `FieldOperation`: Interior node wrapping a `fn::MultiFunction` with child fields
  as inputs.

When connecting Position -> Vector Math (Add) -> Set Position, no computation happens.
Instead, a tree is built:

```
FieldOperation(VectorAdd)
  +-- FieldInput(Position)
  +-- FieldInput(SomeOtherField)
```

When a node needs concrete values (e.g., Set Position writes positions), it evaluates
the entire field tree in bulk over all elements, with parallel chunking. A chain of
10 math nodes does NOT produce 10 intermediate arrays -- a single fused evaluation
pass handles all elements.

Sockets carry either a single value (same for all elements, e.g., constant 0.5) or
a field (varies per element, e.g., Position.x * 2.0). Single values are promoted to
constant fields when needed.

### Attribute System

Attributes are named, typed arrays stored on geometry elements per domain:

- **Point (vertex)**: position, normals, vertex groups
- **Edge**: crease, edge data
- **Face**: material index, shade smooth, face normals
- **Corner (face corner / loop)**: UV maps, vertex colors, per-corner normals

Attribute propagation during topology changes:

- **Interpolation**: New elements from interpolation of existing ones get interpolated
  attribute values (linear for float, nearest for int/bool).
- **Copying**: Directly copied elements get copied attributes.
- **Default**: New elements with no clear source get default values.

Key attribute nodes: `Store Named Attribute`, `Named Attribute`, `Remove Named
Attribute`, `Capture Attribute` (anonymous attributes with reference counting).

### Node Categories

**Mesh Primitives:**
Mesh Circle, Grid, Line, Cube, Cone, Cylinder, UV Sphere, Ico Sphere

**Mesh Operations:**
Subdivide Mesh, Subdivision Surface (Catmull-Clark), Triangulate, Dual Mesh,
Extrude Mesh, Flip Faces, Scale Elements, Merge by Distance, Mesh Boolean,
Mesh to Points, Mesh to Curve, Split Edges

**Geometry Operations:**
Join Geometry, Transform Geometry, Set Position, Delete Geometry, Bounding Box,
Convex Hull, Separate Components, Geometry to Instance

**Curve Operations:**
Curve Circle, Curve Line, Bezier Segment, Resample/Subdivide/Trim/Reverse Curve,
Curve to Mesh (sweep profile along curve), Fill Curve, Fillet Curve

**Instances:**
Instance on Points, Realize Instances, Rotate/Scale/Translate Instances,
Distribute Points on Faces (Poisson disk or random)

**Math/Utility:**
Math (add, subtract, multiply, etc.), Vector Math, Boolean Math, Compare, Clamp,
Map Range, Float Curve, Color Ramp, Mix, Switch, Random Value

**Input Nodes (Field Generators):**
Position, Normal, Index, ID, Value, Integer, Boolean, Vector, Color, Scene Time

**Attribute:**
Attribute Statistic, Capture Attribute, Store/Named/Remove Named Attribute,
Domain Size

### Group Nodes

Reusable subgraphs with `Group Input` and `Group Output` defining the interface.
During evaluation, groups are typically inlined into the parent graph. Groups can
be nested but not recursive. They serve as the primary abstraction mechanism and
can be shared across node trees via asset libraries.

### Simulation and Repeat Zones (Blender 3.6+/4.0+)

- **Simulation Zone**: Input/Output pair maintaining persistent state across frames
- **Repeat Zone**: Input/Output pair enabling loops within a single evaluation

---

## Key Files Reference

### File organization

```
src/editor/geometry_graph/
    geometry_graph.hpp / .cpp             -- Geometry_graph (extends erhe::graph::Graph, dirty-flag evaluation)
    geometry_graph_mesh.hpp / .cpp        -- Geometry_graph_mesh node attachment (doc/geometry_graph_mesh.md)
    geometry_graph_node.hpp / .cpp        -- Base class + stepper widgets + JSON vec3 helpers
    geometry_graph_node_factory.hpp / .cpp -- make_geometry_graph_node(), shared by windows and group assets
    geometry_graph_operations.hpp         -- Undoable node / link / whole-graph-replace / parameter operations
    geometry_graph_window.hpp / .cpp      -- The graph window, node spawn grid, edit API
    geometry_payload.hpp / .cpp           -- Variant payload type + typed pin keys
    graph_mesh.hpp / .cpp                 -- The Graph_mesh asset (doc/geometry_graph_mesh.md)
    graph_mesh_serialization.hpp / .cpp   -- Asset save / load / clear
    nodes/
        boolean_node                      -- CSG union / intersection / difference
        conway_node                       -- all 9 Conway operators via enum + per-operator ratio
        geometry_output_node              -- scene output (terminal node)
        geometry_source_nodes             -- brush / scene-mesh geometry sources
        geometry_unary_operation_node     -- triangulate / normalize / reverse / repair
        group_nodes                       -- group input / output / group reference
        instance_nodes                    -- distribute points / instance on points / realize
        join_geometry_node
        lattice_node                      -- doc/lattice_deform_geometry_node.md
        math_node
        mesh_box_node, mesh_cone_node, mesh_disc_node, mesh_sphere_node, mesh_torus_node
        passthrough_node
        sdf_nodes                         -- ERHE_VOXEL_LIBRARY=openvdb only
        subdivide_node                    -- Catmull-Clark / sqrt3, iteration count
        transform_from_node               -- doc/geometry_graph_transform_from_node.md
        transform_node
        value_nodes                       -- float / integer / vector constants
```

All source files are listed explicitly in `src/editor/CMakeLists.txt` (no
globbing, per project conventions). Outside the directory:
`App_context::geometry_graph_window`, `Operation_stack::execute_now()`, the
geometry-graph MCP tools in `src/editor/mcp/`, and window construction in
`editor.cpp`.

### Infrastructure this builds on

| File | What it provides |
|------|------------------|
| `src/erhe/graph/erhe_graph/graph.hpp` | Graph container API (connect, disconnect, sort, `would_create_cycle`) |
| `src/erhe/graph/erhe_graph/node.hpp` | Node base with input / output pins |
| `src/erhe/graph/erhe_graph/pin.hpp` | Pin: source / sink, key, slot, links |
| `src/erhe/graph/erhe_graph/link.hpp` | Link: source pin to sink pin |
| `src/editor/graph_editor/*` | Shared graph-editor node, asset and window infrastructure (`doc/graph_editor.md`) |
| `src/erhe/geometry/erhe_geometry/operation/*` | The geometry operations the nodes wrap (`doc/erhe_geometry.md`) |
| `src/erhe/geometry/erhe_geometry/shapes/*` | The shape generators the source nodes wrap |
| `src/erhe/primitive/erhe_primitive/primitive_builder.hpp` | Builds a `Buffer_mesh` from a `Geometry` for the output node |
| `src/erhe/scene/erhe_scene/mesh.hpp` | The scene mesh the output node produces |
| `src/editor/operations/operation_stack.hpp` | Undo stack, including `execute_now()` |

`src/editor/graph/` holds the older, simpler `Shader_graph` - a second consumer
of `erhe::graph`, unrelated to this feature despite the similar shape.

## Future work

- [plans/geometry_graph/geometry_nodes.md](plans/geometry_graph/geometry_nodes.md) -
  the field system, further node types and curve geometry.
- [plans/geometry_graph/attribute_projection.md](plans/geometry_graph/attribute_projection.md) -
  the attribute projection node.
- [plans/geometry_graph/openvdb_sdf.md](plans/geometry_graph/openvdb_sdf.md) -
  the remaining SDF work.
- [plans/geometry_graph/creation_tools.md](plans/geometry_graph/creation_tools.md) -
  the authoring tools the AI creations still lack.
