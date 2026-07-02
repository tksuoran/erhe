# Geometry Nodes for erhe Editor

Analysis of Blender's Geometry Nodes architecture, assessment of erhe's existing
infrastructure, and implementation plan for replicating minimal geometry nodes
functionality in the erhe editor.

## Table of Contents

1. [Implementation Status](#implementation-status)
2. [Blender Geometry Nodes Architecture](#blender-geometry-nodes-architecture)
3. [erhe Existing Infrastructure](#erhe-existing-infrastructure)
4. [Gap Analysis](#gap-analysis)
5. [Implementation Plan](#implementation-plan)
6. [Phase Details](#phase-details)
7. [Key Files Reference](#key-files-reference)

---

## Implementation Status

Phases 1-5 plus undo/redo, graph serialization, incremental evaluation (6a)
and copy-on-write pass-through (6b) are implemented (2026-07-02, branch
`geometry_nodes`). Phases 6c (fields), 6d (instances) and 6e (node groups)
remain future work. All code lives in `src/editor/geometry_graph/`.

| Work item                                            | Status | Commit   |
|------------------------------------------------------|--------|----------|
| Phase 1: foundation (payload, node base, graph, window) | DONE | 713eb22d |
| Phase 2: mesh primitive nodes                        | DONE   | cf4060d1 |
| Phase 3: geometry operation nodes                    | DONE   | 09e51836 |
| Phase 4: combiner and value nodes                    | DONE   | 53ca69ee |
| Phase 5: scene output node                           | DONE   | c961b319 |
| In-editor MCP tools (headless graph scripting)       | DONE   | cfe79f68 |
| Undo/redo for graph edits                            | DONE   | a8dad173 |
| Graph serialization (save / load JSON)               | DONE   | e9d7bd44 |
| Phase 6a: incremental evaluation (dirty propagation) | DONE   | a11abd21 |
| Undoable parameter edits                             | DONE   | 7585efe7 |
| MCP geometry_graph_set_parameter + parameters in get_geometry_graph | DONE | 1fcc38fc |
| New node spawn grid (no more stacking at origin)     | DONE   | 7fb5b32b |
| Editable output scene node name                      | DONE   | 120e9176 |
| Phase 6b: copy-on-write pass-through                 | DONE   | 0881e107 |
| Optional physics on output node (plan step 6 of phase 5) | DONE | ff414965 |
| Phase 6c / 6d / 6e: fields, instances, groups        | future | -        |

Verified end to end in the headless Vulkan build driven over the in-editor MCP
server: box -> output and box -> conway dual -> output chains render in the
viewport, the produced scene nodes are ordinary content (selectable, movable
with the Transform tool), undo/redo round-trips structural edits and
load/clear, and JSON save/load round-trips node types, parameters, canvas
positions and links.

### Deviations from the plan below

- **Evaluation is dirty-flag driven, not per-frame - and incremental.**
  `Geometry_graph::evaluate_if_dirty()` re-evaluates only when topology or a
  node parameter changed (node widgets call `mark_dirty()`); geometry
  operations are too expensive to run every frame the way `Shader_graph` does.
  Since phase 6a, only dirty nodes and their downstream dependents re-run:
  dirtiness propagates along links while nodes are visited in topological
  order, clean nodes keep their cached output payloads, and structural edits
  mark only the directly affected nodes dirty at the edit site
  (`Geometry_graph_window` insert / erase / connect / disconnect).
- **Copy-on-write is the sharing model (phase 6b).** Geometry flows through
  the graph as `shared_ptr` and is only copied by nodes that actually modify
  it. Pass-through cases share the upstream pointer: single-link Join,
  0-iteration Subdivide, identity Transform.
- **Parameterless operations share one class.** Triangulate, Normalize,
  Reverse and Repair are instances of `Geometry_unary_operation_node`
  (label + function pointer) instead of four near-identical classes.
- **No popups inside the node canvas.** ImGui `Combo` cannot be used inside
  ax::NodeEditor; enum parameters use arrow-stepper widgets
  (`imgui_index_stepper` / `imgui_enum_stepper`).
- **Join has a single multi-link input pin.** Multi-link accumulation in
  `Geometry_payload::operator+=()` merges geometries, so Join needs no A/B
  pins (Blender-style multi-input socket).
- **Upstream geometry is never mutated.** Nodes allocate new `Geometry`
  objects; the output node copies before render processing; a single-link
  Join passes the upstream `shared_ptr` through untouched. Intermediate nodes
  process outputs with connect + build_edges only; render-oriented processing
  (normals, tangents, texture coordinates) happens once in the output node.
- **Undo/redo is structural**, not the before/after mesh snapshot pattern
  sketched in the original Phase 5 text (see below).

### Undo/redo (as built)

All structural edits (add / remove node, connect / disconnect link, graph
load / clear) go through the editor `Operation_stack`
(`geometry_graph_operations.{hpp,cpp}`):

- `Geometry_graph_node_insert_remove_operation` - removing a node captures its
  links and canvas position; undo restores the node exactly, links included.
  Link records hold owning-node `shared_ptr`s so pin pointers stay valid while
  operations sit in the stacks; LIFO undo order guarantees nodes are restored
  before their links reconnect.
- `Geometry_graph_link_insert_remove_operation` - connect / disconnect.
- `Geometry_graph_replace_operation` - whole-graph replacement used by load
  and clear; captures the previous nodes / links / positions on first execute,
  so undoing a load restores the prior graph exactly.
- `Operation_stack::execute_now()` (added for this) executes immediately on
  the main thread and records for undo, so toolbar / canvas gestures and MCP
  calls observe their effects in the same frame. Every operation re-evaluates
  the graph after execute/undo, so the scene output stays current even when
  the graph window is hidden.
- Nodes leaving the graph get the `on_removed_from_graph()` hook (deletion,
  undo of add, clear / load). The node object may stay alive in the undo
  stack, so side effects outside the graph - the output node's scene mesh -
  are released there, not in the destructor.
- `Geometry_graph_parameter_operation` - node parameter edits. Holds before /
  after state as `write_parameters()` JSON dumps applied via
  `read_parameters()`; the values are already live when the operation is
  pushed, so the first execute only records state. Widget edits commit one
  operation per completed gesture (pushed when the active widget
  deactivates); the MCP set-parameter tool pushes through the same class.

### Serialization (as built)

`Geometry_graph_window::save_graph()` / `load_graph()` / `clear_graph()`
(`geometry_graph_serialization.cpp`), JSON version 1:

```json
{
    "version": 1,
    "nodes": [ { "type": "box", "position": [0.0, 0.0], "parameters": {} } ],
    "links": [ { "source_node": 0, "source_slot": 0, "sink_node": 1, "sink_slot": 0 } ]
}
```

- Links reference nodes by index into the `nodes` array; pins by slot index.
- Node `type` is the window factory name
  (`Geometry_graph_node::get_factory_type_name()`, named to avoid clashing
  with the `erhe::Item::get_type_name()` virtual); `make_node()` recreates
  the class on load.
- Parameters go through per-node `write_parameters()` / `read_parameters()`
  virtuals. The output node saves scene and material by name and re-resolves
  them on load.
- Canvas positions round-trip. ax::NodeEditor reports `ImVec2{FLT_MAX}` for
  nodes it has never drawn; `is_valid_node_position()` filters those on both
  save and restore.
- The window has a path field with Save / Load / Clear buttons (default
  `res/editor/graphs/geometry_graph.json`).

### In-editor MCP tools

The geometry graph is fully scriptable over the in-editor MCP server:
`get_geometry_graph`, `geometry_graph_add_node`, `geometry_graph_remove_node`,
`geometry_graph_set_parameter`, `geometry_graph_connect`,
`geometry_graph_disconnect`, `geometry_graph_save`, `geometry_graph_load`,
`geometry_graph_clear`. All mutations are undoable and re-evaluate the graph
immediately (no window visibility needed). `get_geometry_graph` and
`geometry_graph_add_node` report each node's current `parameters` object;
`geometry_graph_set_parameter` accepts the same shape with partial updates
(omitted keys keep current values). `App_context` carries a
`geometry_graph_window` pointer for the handlers.

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

## erhe Existing Infrastructure

### erhe::graph -- Generic DAG Framework (COMPLETE)

Location: `src/erhe/graph/erhe_graph/`

A fully functional generic graph library providing the exact DAG primitives needed:

| Class   | Purpose                                                            |
|---------|--------------------------------------------------------------------|
| `Graph` | Container of nodes and links. `connect()`, `disconnect()`, `sort()` (topological) |
| `Node`  | Extends `erhe::Item`. Has input/output pin vectors                  |
| `Pin`   | Source (output) or sink (input). Key-based type compatibility. Tracks links |
| `Link`  | Directed connection from source `Pin*` to sink `Pin*`. Unique ID    |

The topological sort implementation uses a classic algorithm: iteratively select
nodes whose input dependencies are all already sorted. Detects cycles (non-acyclic
graphs produce an error). Pin keys enforce type matching on connect.

### imgui_node_editor -- Visual Node Editor UI (COMPLETE)

Location: `src/erhe/imgui/erhe_imgui/imgui_node_editor.*`

Bundled copy of ax::NodeEditor v0.9.4 providing:

- Interactive node placement, dragging, selection
- Pin interaction (drag-to-connect with visual feedback)
- Node/link deletion
- Style customization (pin shapes, link curves, padding)
- Zoom and pan on canvas

Supporting files: `crude_json.*` (serialization), `imgui_canvas.*` (canvas widget),
`imgui_bezier_math.*` (link curves), `imgui_extra_math.*` (utilities).

### editor::Graph_window + Shader_graph -- Working Reference (COMPLETE)

Location: `src/editor/graph/`

A complete, working node-based graph system built on `erhe::graph` + ax::NodeEditor:

**`Shader_graph`** extends `erhe::graph::Graph`:
- `evaluate(Sheet*)` -- calls `sort()` then iterates sorted nodes calling `evaluate()`
- Load/store operations for accessing spreadsheet-like data

**`Shader_graph_node`** extends `erhe::graph::Node`:
- `evaluate(Shader_graph&)` -- virtual, overridden by each node type
- `imgui()` -- virtual, in-node UI rendering
- `node_editor()` -- renders the node in ax::NodeEditor with pins and content
- `accumulate_input_from_links(slot)` -- pulls input payload from connected upstream
- Input/output payloads stored per pin slot
- Selection integration with editor selection system

**`Payload`** -- typed data carrier:
- `erhe::dataformat::Format format`
- `std::array<int, 4> int_value`
- `std::array<float, 4> float_value`
- Arithmetic operators: +, -, *, /

**Concrete node types** (each ~20-40 lines):
- `Constant` -- holds a Payload value, outputs it
- `Add` -- pulls two inputs, outputs sum
- `Subtract`, `Multiply`, `Divide` -- same pattern
- `Passthrough` -- passes input to output unchanged
- `Load` -- reads from sheet
- `Store` -- writes to sheet

**`Graph_window`** -- ImGui window hosting the editor:
- Toolbar with buttons to create each node type
- Calls `graph.evaluate()` each frame
- Renders all nodes via `node_editor()` method
- Handles link creation via `BeginCreate()` / `QueryNewLink()` / `AcceptNewItem()`
- Handles node/link deletion via `BeginDelete()` / `QueryDeletedNode/Link()`
- Integrates with editor selection system

**This is the direct template for the geometry nodes implementation.** The geometry
graph window follows this pattern almost exactly, with a geometry-aware payload type
and different node types.

### erhe::rendergraph -- Rendering DAG (Separate System)

Location: `src/erhe/rendergraph/erhe_rendergraph/`

A separate DAG framework specifically for rendering, NOT built on `erhe::graph`:

- `Rendergraph` -- owns nodes, topological sort, executes each frame
- `Rendergraph_node` -- abstract base with `Rendergraph_consumer_connector` (inputs)
  and `Rendergraph_producer_connector` (outputs), connected by integer keys
- Keys: `viewport_texture`, `shadow_maps`, `depth_visualization`, etc.
- Max traversal depth of 10 for queries
- `Texture_rendergraph_node` handles MSAA resolve internally

The `Rendergraph_window` in `src/editor/developer/` visualizes this graph using
the same ax::NodeEditor UI, but read-only.

This is a separate system from `erhe::graph` and not directly usable for geometry
nodes, but demonstrates the same DAG concepts.

### erhe::geometry -- Mesh Operations (COMPLETE)

Location: `src/erhe/geometry/erhe_geometry/`

Full polygon mesh library wrapping Geogram's `GEO::Mesh`:

**`Geometry` class:**
- Wraps `GEO::Mesh` with named attributes, connectivity queries, processing flags
- `process(flags)` -- compute normals, tangents, texcoords, edges, connectivity
- `merge_with_transform()` / `copy_with_transform()` for combining meshes
- `validate()` returns description of first problem found
- `sanitize()` fixes degenerate facets and invalid vertex data
- `debug_trace()` for development diagnostics

**Attribute system:**
- `Attribute_descriptor` -- name, transform mode, interpolation mode
- `Attribute_present<T>` -- wraps `GEO::Attribute<T>` with per-element presence flag
- `Mesh_attributes` -- all standard vertex/corner/facet attributes:
  - Normals, tangents, bitangents (vertex, corner, facet)
  - Texture coordinates (2 sets)
  - Colors (2 sets)
  - Joint indices/weights (2 sets)
  - Valency/edge count, ID, smooth normals, centroids, aniso control

**Transform and interpolation modes:**
- `Transform_mode`: none, mat_mul_vec3_one (positions), normal_mat_mul_vec3_zero (normals), etc.
- `Interpolation_mode`: none, linear, normalized, normalized_vec3_float

**`Geometry_operation` base class:**
- Takes `const Geometry& source, Geometry& destination` (functional pattern)
- `Source_table` tracks weighted provenance from source to destination elements
- Separate source tables for vertices, corners, facets, edges
- `post_processing()` handles attribute interpolation using source tables
- Helper methods: `make_dst_vertices_from_src_vertices()`, `make_facet_centroids()`,
  `make_edge_midpoints()`, `make_new_dst_vertex_from_src_facet_centroid()`, etc.

**Available operations:**

| Category        | Operations                                                    |
|-----------------|---------------------------------------------------------------|
| Shape generators | `make_box`, `make_sphere`, `make_torus`, `make_cone`, `make_disc`, `make_icosahedron`, regular polyhedra, convex hull |
| Conway operators | `ambo`, `chamfer`, `dual`, `gyro`, `join`, `kis`, `meta`, `subdivide`, `truncate` |
| Subdivision     | `catmull_clark_subdivision`, `sqrt3_subdivision`              |
| CSG             | `union_`, `intersection`, `difference` (experimental via Geogram) |
| Utilities       | `triangulate`, `normalize`, `reverse`, `bake_transform`, `repair`, `generate_tangents` |

All Conway/subdivision operations follow the same signature:
`void op(const Geometry& source, Geometry& destination)` -- pure functional transformation.

Some operations take extra parameters:
- `kis(source, destination, height)`
- `gyro(source, destination, ratio)`
- `chamfer3(source, destination, bevel_ratio)`
- `truncate(source, destination, ratio)`

**Change tracking:**
- `Mesh_serials` tracks modification serials for edges, normals, tangents, texcoords
- Enables efficient invalidation of cached GPU data

### erhe::primitive -- Geometry to GPU Pipeline (COMPLETE)

Location: `src/erhe/primitive/erhe_primitive/`

Converts `erhe::geometry::Geometry` into GPU-ready vertex/index buffers:

- `Primitive` -- top-level: owns render shape + collision shape, provides bounding box
- `Primitive_render_shape` -- holds the renderable `Buffer_mesh`
- `Buffer_mesh` -- built result: buffer ranges, index ranges, bounding box/sphere (move-only)
- `Primitive_builder` / `Build_context` -- orchestrates conversion from GEO::Mesh
- `Buffer_sink` -- abstract interface for allocating buffer space (GPU or CPU)
- `Build_info` / `Buffer_info` -- configuration for vertex format, index type, primitive types

Index generation for four primitive modes: triangle fill, edge lines, corner points,
polygon centroids. Element mappings track triangle -> facet relationships for picking.

### erhe::scene -- Scene Graph (COMPLETE)

Location: `src/erhe/scene/erhe_scene/`

glTF-like scene graph:

- `Scene` -- top-level container with root node, mesh layers, light layers, cameras
- `Node` -- extends `Hierarchy`. Holds `Node_transforms` (parent-from-node, world-from-node)
- `Node_attachment` -- base for things attached to nodes (Mesh, Camera, Light)
- `Mesh` -- node attachment holding vector of `Mesh_primitive` (Primitive + Material pairs)
- Transform serial numbers prevent redundant recomputation
- Raytrace primitives for CPU-side picking

### Editor Operation System (COMPLETE)

Location: `src/editor/operations/`

**Core undo/redo framework:**

- `Operation` -- abstract base with `execute(App_context&)` and `undo(App_context&)`
- `Operation_stack` -- manages queued/executed/undone vectors, binds Ctrl+Z/Ctrl+Y
- `Compound_operation` -- groups multiple operations into single undo step

**`Mesh_operation` base class:**

- Contains vector of `Entry` (before/after `Mesh_primitive` + node physics state)
- `make_entries()` -- applies geometry transformation to selected meshes
- Post-operation sanitization: fixes degenerate facets, NaN/Inf vertices
- Saves corrupted pre-operation geometry to `debug_geometry/` for investigation

**Async execution:**

- Uses `tf::AsyncTask` for worker-thread geometry operations via `async_for_nodes_with_mesh()`
- Atomic counters track pending/running operations
- Operations queued for main-thread execution after completion

**Existing editor geometry operations (each extends `Mesh_operation`):**
Catmull_clark_subdivision, Sqrt3_subdivision, Triangulate, Join, Kis, Subdivide,
Meta, Gyro, Chamfer3, Dual, Ambo, Truncate, Reverse, Normalize, Generate_tangents,
Make_raytrace, Bake_transform, Repair, Weld, Union, Intersection, Difference

### Editor Application Architecture

Location: `src/editor/`

**`App_context`** -- service locator struct with ~40 raw pointers to all subsystems.
Components receive `App_context&` but must NOT access members in constructors (all
null during construction, populated by `fill_app_context()` afterward).

**`App_message_bus`** -- typed publish-subscribe for decoupled communication. Message
types include Selection, Hover, Graphics_settings, Node_touched, Tool_select, etc.

**`Content_library`** -- tree-based asset management with `Content_library_node` wrapping
any `erhe::Item_base`. Categories: brushes, animations, skins, materials, textures.

**`Scene_root`** -- owns `erhe::scene::Scene`, physics world, raytrace scene, content
library. Multiple `Scene_root` instances managed by `App_scenes`.

**Tool system** -- priority-based activation. Tools implement `tool_render()`,
`tool_properties()`, `cancel_ready()`. Input via `erhe::commands::Commands`.

**Window system** -- all windows extend `erhe::imgui::Imgui_window` with `imgui()` override.

### erhe::item -- Entity System (COMPLETE)

Location: `src/erhe/item/erhe_item/`

Foundation for all items in the system:

- `Item_base` -- ID, name, flags, tags, `enable_shared_from_this`
- `Hierarchy` -- parent/child tree with depth tracking, cloning, filtering
- `Item_flags` -- visible, selected, hovered, opaque, content, show_in_ui
- `Item_type` -- bitmasks for mesh, camera, light, node, graph_node, etc.
- `Item_filter` -- four-criteria bitmask filter
- `Item<Base, Intermediate, Self, Kind>` -- CRTP for `clone()`, `get_type()`, `get_type_name()`

All items must be `std::make_shared` (uses `enable_shared_from_this`).

---

## Gap Analysis

(Analysis as of planning time; see [Implementation Status](#implementation-status)
for what has since been built.)

### What Already Exists vs What's Needed

| Requirement                    | Existing                              | Gap                                   |
|-------------------------------|---------------------------------------|---------------------------------------|
| DAG graph data model          | `erhe::graph` -- complete             | None                                  |
| Node editor UI                | ax::NodeEditor -- complete            | None                                  |
| Graph window pattern          | `editor::Graph_window` -- complete    | Need geometry-specific version        |
| Graph node base class         | `Shader_graph_node` -- complete       | Need geometry-specific version        |
| Data payload type             | `Payload` (int/float)                 | Need geometry-aware variant type      |
| Geometry operations           | 20+ operations in `erhe::geometry`    | Need node wrappers                    |
| Shape generators              | 8+ generators in `erhe::geometry`     | Need node wrappers                    |
| Geometry to GPU               | `Primitive_builder` pipeline          | Need output node integration          |
| Scene mesh attachment         | `erhe::scene::Mesh`                   | Need output node -> scene wiring      |
| Undo/redo                     | `Operation_stack`                     | Need graph state as operation         |
| Attribute interpolation       | `Source_table`, `Geometry_operation`   | Already handled by geometry ops       |
| Selection integration         | `Selection_tool`                      | `Shader_graph_node` already integrates |
| Node properties inspector     | `Node_properties_window`              | May need geometry-specific extension  |
| Copy-on-write geometry        | Not present                           | Not needed for Phase 1 (use copies)   |
| Field system (per-element)    | Not present                           | Defer to Phase 5                      |
| Instancing                    | Not present                           | Defer to Phase 5                      |
| Curve geometry type           | Not present                           | Defer to Phase 5                      |
| Node groups                   | Not present                           | Defer to Phase 5                      |

### Key Decisions

**1. Eager vs Lazy evaluation:**
Start with eager evaluation. Each node materializes its output geometry immediately.
This matches the existing `Geometry_operation` pattern perfectly. Fields (lazy
per-element computation) are architecturally significant and should be deferred.

**2. Copy vs copy-on-write:**
Start with full copies. `shared_ptr<Geometry>` with explicit copies at each node
that modifies geometry. CoW optimization can be added later without changing the API.

**3. Where to place code:**
- New node types and graph window: `src/editor/geometry_graph/` (new directory)
- Geometry payload could live alongside existing payload in `src/editor/graph/`
  or in the new directory
- No changes needed to `erhe::graph` or `erhe::geometry` libraries

**4. Relationship to existing shader graph:**
The geometry graph is a separate graph instance, not a modification of the shader
graph. Both use `erhe::graph::Graph` as the data model and `ax::NodeEditor` for UI.
They could share some base infrastructure (e.g., the Payload could be generalized),
but keeping them separate initially is simpler.

---

## Implementation Plan

### Phase 1: Foundation Framework (IMPLEMENTED)

Create the geometry graph infrastructure paralleling the shader graph pattern.

**1a. Geometry Payload Type**

A variant-based payload replacing the shader graph's int/float arrays:

```
Geometry_payload:
  variant type containing:
    - std::shared_ptr<erhe::geometry::Geometry>  (mesh data)
    - float                                       (scalar parameter)
    - glm::vec3                                   (vector parameter)
    - glm::vec4                                   (color / 4-component vector)
    - glm::mat4                                   (transform matrix)
    - int                                         (integer parameter)
    - bool                                        (boolean parameter)
    - std::shared_ptr<erhe::primitive::Material>  (material reference)
```

Pin type enum to validate connections (geometry pins only connect to geometry pins,
float pins to float pins, etc.). Type compatibility checking in `Graph::connect()`
uses pin keys -- each type gets a distinct key value.

**1b. Geometry_graph_node Base Class**

Extends `erhe::graph::Node` following `Shader_graph_node` pattern:

- `evaluate(Geometry_graph&)` -- virtual, called during graph evaluation
- `imgui()` -- virtual, renders in-node UI (parameter widgets, vertex/face counts)
- Geometry payload storage per input/output pin
- `accumulate_input_from_links(slot)` -- pulls geometry payload from upstream
- `get_geometry_output(slot)` / `set_geometry_output(slot, payload)` accessors
- Pin factory methods: `make_geometry_input()`, `make_float_input()`, etc.

**1c. Geometry_graph Class**

Extends `erhe::graph::Graph`:

- `evaluate()` -- calls `sort()` then iterates sorted nodes calling `evaluate()`
- Holds context needed for scene output (pointer to `App_context` or scene-specific data)
- Dirty flag to trigger re-evaluation when nodes/links change
- Optional: serial number for change detection

**1d. Geometry_graph_window Class**

Extends `erhe::imgui::Imgui_window` following `Graph_window` pattern:

- Owns `Geometry_graph` and `ax::NodeEditor::EditorContext`
- Node creation UI (toolbar buttons or right-click menu)
- Renders all nodes via ax::NodeEditor
- Handles link creation/deletion
- Calls `graph.evaluate()` each frame (or on change)
- Node deletion with selection integration

### Phase 2: Mesh Primitive Nodes (Input Nodes) (IMPLEMENTED)

These have no geometry inputs, only parameter inputs and a geometry output.
As built, scalar parameters are exposed both as in-node widgets and as
float/int input pins; a connected input overrides the widget value.
Each wraps an existing shape generator from `erhe::geometry::shapes`:

**Mesh_box_node:**
- Parameters: x_size (float), y_size (float), z_size (float),
  x_div (int), y_div (int), z_div (int)
- Output: Geometry
- Wraps: `shapes::make_box(mesh, size, div, p)`

**Mesh_sphere_node:**
- Parameters: radius (float), subdivision_count (int)
- Output: Geometry
- Wraps: `shapes::make_sphere()` or icosahedron + subdivision

**Mesh_torus_node:**
- Parameters: major_radius, minor_radius, major_segments, minor_segments
- Output: Geometry
- Wraps: `shapes::make_torus()`

**Mesh_cone_node:**
- Parameters: radius, height, segments, stacks
- Output: Geometry
- Wraps: `shapes::make_cone()`

**Mesh_disc_node:**
- Parameters: radius, segments
- Output: Geometry
- Wraps: `shapes::make_disc()`

Each node's `evaluate()` creates a new `Geometry`, calls the shape generator,
calls `geometry.process(flags)` to compute normals/tangents, then sets the output
payload. The `imgui()` override renders parameter sliders/inputs.

### Phase 3: Geometry Operation Nodes (IMPLEMENTED)

These have one geometry input and one geometry output, wrapping existing
operations. As built, Triangulate / Normalize / Reverse / Repair share the
`Geometry_unary_operation_node` class instead of one class each; Transform
uses `Geometry::copy_with_transform()` (which also copies connectivity).

**Subdivide_node:**
- Input: Geometry
- Parameters: mode (Catmull-Clark or Sqrt3), iterations (int)
- Output: Geometry
- Wraps: `catmull_clark_subdivision()` or `sqrt3_subdivision()`

**Conway_node:**
- Input: Geometry
- Parameters: operation (enum: ambo, dual, join, kis, meta, subdivide, truncate,
  chamfer, gyro), ratio/height (float, for kis/gyro/chamfer/truncate)
- Output: Geometry
- Wraps: corresponding Conway operation function

**Triangulate_node:**
- Input: Geometry
- Output: Geometry
- Wraps: `triangulate()`

**Transform_node:**
- Input: Geometry
- Parameters: translation (vec3), rotation (vec3 euler), scale (vec3)
- Output: Geometry
- Wraps: `bake_transform()` or `copy_with_transform()`

**Normalize_node:**
- Input: Geometry
- Output: Geometry
- Wraps: `normalize()` -- projects all vertices onto unit sphere

**Reverse_node:**
- Input: Geometry
- Output: Geometry
- Wraps: `reverse()` -- reverses face winding

**Repair_node:**
- Input: Geometry
- Output: Geometry
- Wraps: `repair()`

### Phase 4: Combiner and Value Nodes (IMPLEMENTED)

**Join_geometry_node:**
- Inputs: Geometry A, Geometry B (or multiple via repeated connections)
- Output: Geometry
- Wraps: `Geometry::merge_with_transform()`
- As built: one multi-link input pin; merging happens in
  `Geometry_payload::operator+=()` during input accumulation.

**Boolean_node (CSG):**
- Inputs: Geometry A, Geometry B
- Parameters: mode (enum: union, intersection, difference)
- Output: Geometry
- Wraps: `union_()`, `intersection()`, `difference()`

**Float_value_node:**
- Parameters: value (float slider)
- Output: Float
- Simple constant output

**Vector_value_node:**
- Parameters: x, y, z (float sliders)
- Output: Vector3

**Integer_value_node:**
- Parameters: value (int slider)
- Output: Integer

**Math_node:**
- Inputs: A (float), B (float)
- Parameters: operation (enum: add, subtract, multiply, divide, power, sqrt, sin, cos, etc.)
- Output: Float
- Wraps: corresponding math operation

### Phase 5: Scene Output Node (IMPLEMENTED)

This is the key integration point connecting the geometry graph to the scene.

**Geometry_output_node:**
- Input: Geometry
- Parameters: material (Material combo), mesh layer (enum)
- No outputs (terminal node)

On evaluation (as built):
1. Takes the input `Geometry` and copies it (the input is shared with
   upstream nodes and must not be mutated)
2. Calls `geometry.process(flags)` on the copy for normals / centroids /
   facet texcoords / tangents
3. Builds a `Primitive` via `Primitive(geometry)` +
   `make_renderable_mesh(build_info, normal_style)` + `make_raytrace()`
   (buffers allocated through `Mesh_memory` sinks; the per-frame
   `Mesh_memory::flush()` in the editor tick uploads them)
4. Creates or updates one owned `erhe::scene::Node` + `erhe::scene::Mesh`
   in place (content layer, shadow_cast); the scene node is removed via
   `on_removed_from_graph()` when the graph node leaves the graph
5. Scene defaults to the single registered scene root, material to the first
   content library material; both selectable with in-node steppers

`Node_physics` (step 6 of the original sketch) is implemented as an
opt-in: a Physics checkbox plus motion mode stepper (static / kinematic /
dynamic) on the node. When enabled, a `Node_physics` attachment with a
convex hull collision shape built from the render geometry (non-convex
results are approximated by their hull) is kept in sync with the mesh on
every re-evaluation and released together with the scene node.

**Integration with undo/redo:** implemented differently from the sketch here -
graph edits themselves are undoable operations (structural, not before/after
mesh snapshots); see [Implementation Status](#implementation-status).

### Phase 6: Enhancements (Future)

**6a. Caching and Incremental Evaluation:** (DONE - see Implementation Status)
- Per-node dirty flag / serial number
- Only re-evaluate nodes whose inputs changed
- Cache output geometry per node (use `Mesh_serials` pattern)

**6b. Copy-on-Write Geometry:** (DONE - see Implementation Status)
- `shared_ptr` with reference counting
- Clone-on-modify for efficiency when geometry passes through unchanged

**6c. Field System (Per-Element Computation):**
- `Field<T>` -- lazy expression tree evaluated in bulk
- `FieldInput` -- reads geometry attributes (position, normal, etc.)
- `FieldOperation` -- wraps a function with child fields
- `Set_position_node` accepts a field input
- Evaluation: topological sort of field tree, allocate temp arrays, evaluate in
  parallel chunks
- This is architecturally significant and requires careful design

**6d. Instance System:**
- `Instance_on_points_node` -- instantiate geometry at point locations
- `Realize_instances_node` -- flatten to actual geometry
- `Distribute_points_node` -- scatter points on surfaces (Poisson disk)

**6e. Node Groups:**
- `Group_input_node` / `Group_output_node` defining interface
- Inline expansion during evaluation
- Serialization as separate graph assets
- UI for entering/exiting group editing

**6f. Additional Node Types:**
- Convex hull, extrude, merge by distance, set material
- Attribute nodes: read/write/delete named attributes
- Input nodes: position, normal, index, random value
- Curve support (requires new geometry type)

---

## Phase Details

### Estimated Effort per Phase

| Phase | Description                    | New Classes        | Effort    | Status |
|-------|-------------------------------|--------------------|-----------|--------|
| 1     | Foundation framework          | 4 classes          | Small     | DONE |
| 2     | Mesh primitive nodes          | 5 node classes     | Small     | DONE |
| 3     | Geometry operation nodes      | 4 node classes (unary ops share one) | Small | DONE |
| 4     | Combiner and value nodes      | 6 node classes     | Small     | DONE |
| 5     | Scene output node             | 1 class            | Medium    | DONE |
| -     | Undo/redo for graph edits     | 3 operation classes | Medium   | DONE |
| -     | Graph serialization           | Window methods + per-node parameter IO | Small | DONE |
| 6a    | Caching                       | Modifications      | Small     | DONE |
| 6b    | Copy-on-write                 | Modifications      | Small     | DONE |
| 6c    | Field system                  | ~5+ classes        | Large     | future |
| 6d    | Instancing                    | ~3 node classes    | Medium    | future |
| 6e    | Node groups                   | ~3 classes         | Medium    | future |

### First Working Demo (Phases 1-5 Minimal) - ACHIEVED

The demo works: nodes created in the Geometry Graph window (or over MCP)
produce meshes rendered in the viewport, live-updating as parameters change.

Minimum set for a demo where you can visually create geometry in a node graph
and see it rendered in the viewport:

1. `Geometry_payload` -- variant type
2. `Geometry_graph_node` -- base class
3. `Geometry_graph` -- graph container with evaluate()
4. `Geometry_graph_window` -- ImGui window with toolbar
5. `Mesh_box_node` -- create a box
6. `Mesh_sphere_node` -- create a sphere
7. `Subdivide_node` -- subdivide geometry
8. `Conway_node` -- Conway operations (ambo, dual, kis, etc.)
9. `Transform_node` -- translate/rotate/scale
10. `Join_geometry_node` -- merge geometries
11. `Geometry_output_node` -- pipe to scene

This is ~11 classes. Each node class is ~30-60 lines (constructor setting up pins,
`evaluate()` calling the wrapped operation, `imgui()` rendering parameter widgets).
The framework classes (1-4) follow the shader graph pattern closely.

### File Organization (as built)

```
src/editor/geometry_graph/
    geometry_graph.hpp / .cpp             -- Geometry_graph (extends erhe::graph::Graph, dirty-flag evaluation)
    geometry_graph_node.hpp / .cpp        -- Base class + stepper widgets + JSON vec3 helpers
    geometry_graph_operations.hpp / .cpp  -- Undoable node / link / whole-graph-replace operations
    geometry_graph_serialization.cpp      -- save_graph / load_graph / clear_graph (window members)
    geometry_graph_window.hpp / .cpp      -- ImGui window (extends Imgui_window), node factory, edit API
    geometry_payload.hpp / .cpp           -- Variant payload type + typed pin keys
    nodes/
        boolean_node.hpp / .cpp                  -- CSG union / intersection / difference
        conway_node.hpp / .cpp                   -- all 9 Conway operators via enum + per-op ratio
        geometry_output_node.hpp / .cpp          -- scene output (terminal node)
        geometry_unary_operation_node.hpp / .cpp -- Triangulate / Normalize / Reverse / Repair
        join_geometry_node.hpp / .cpp
        math_node.hpp / .cpp
        mesh_box_node.hpp / .cpp
        mesh_cone_node.hpp / .cpp
        mesh_disc_node.hpp / .cpp
        mesh_sphere_node.hpp / .cpp
        mesh_torus_node.hpp / .cpp
        subdivide_node.hpp / .cpp                -- Catmull-Clark / Sqrt3, iteration count
        transform_node.hpp / .cpp
        value_nodes.hpp / .cpp                   -- Float / Integer / Vector constants
```

Related changes outside the directory: `App_context::geometry_graph_window`
pointer, `Operation_stack::execute_now()`, geometry graph tools in
`src/editor/mcp/mcp_server.{hpp,cpp}`, window construction in `editor.cpp`.

### CMake Integration

All source files are listed explicitly in `src/editor/CMakeLists.txt`
(no globbing per project conventions). No new link dependencies were needed;
the editor target already links `erhe_graph`, `erhe_geometry`,
`erhe_primitive`, `erhe_scene` and `erhe_imgui`.

---

## Key Files Reference

### Graph Infrastructure (to study and follow as template)

| File | What to learn |
|------|---------------|
| `src/erhe/graph/erhe_graph/graph.hpp` | Graph container API (connect, disconnect, sort) |
| `src/erhe/graph/erhe_graph/graph.cpp` | Topological sort implementation, connect/disconnect logic |
| `src/erhe/graph/erhe_graph/node.hpp` | Node base with input/output pins |
| `src/erhe/graph/erhe_graph/pin.hpp` | Pin: source/sink, key, slot, links |
| `src/erhe/graph/erhe_graph/link.hpp` | Link: source pin to sink pin |
| `src/editor/graph/shader_graph.hpp` | How to extend Graph with evaluate() |
| `src/editor/graph/shader_graph_node.hpp` | How to extend Node with payloads |
| `src/editor/graph/shader_graph_node.cpp` | node_editor() rendering, accumulate_input_from_links() |
| `src/editor/graph/graph_window.cpp` | Full graph window: create nodes, handle links, evaluate |
| `src/editor/graph/payload.hpp` | Current payload type (to be replaced for geometry) |
| `src/editor/graph/add.cpp` | Simplest node implementation example |
| `src/editor/graph/constant.cpp` | Input node example (no inputs, parameter + output) |

### Geometry Operations (operations to wrap as nodes)

| File | Operations |
|------|------------|
| `src/erhe/geometry/erhe_geometry/operation/geometry_operation.hpp` | Base class, Source_table |
| `src/erhe/geometry/erhe_geometry/operation/conway/ambo.hpp` | `void ambo(const Geometry&, Geometry&)` |
| `src/erhe/geometry/erhe_geometry/operation/conway/dual.hpp` | `void dual(...)` |
| `src/erhe/geometry/erhe_geometry/operation/conway/join.hpp` | `void join(...)` |
| `src/erhe/geometry/erhe_geometry/operation/conway/kis.hpp` | `void kis(..., float height)` |
| `src/erhe/geometry/erhe_geometry/operation/conway/gyro.hpp` | `void gyro(..., float ratio)` |
| `src/erhe/geometry/erhe_geometry/operation/conway/truncate.hpp` | `void truncate(..., float ratio)` |
| `src/erhe/geometry/erhe_geometry/operation/conway/chamfer3.hpp` | `void chamfer3(..., float ratio)` |
| `src/erhe/geometry/erhe_geometry/operation/conway/meta.hpp` | `void meta(...)` |
| `src/erhe/geometry/erhe_geometry/operation/conway/subdivide.hpp` | `void subdivide(...)` |
| `src/erhe/geometry/erhe_geometry/operation/triangulate.hpp` | `void triangulate(...)` |
| `src/erhe/geometry/erhe_geometry/operation/normalize.hpp` | `void normalize(...)` |
| `src/erhe/geometry/erhe_geometry/operation/reverse.hpp` | `void reverse(...)` |
| `src/erhe/geometry/erhe_geometry/operation/repair.hpp` | `void repair(...)` |
| `src/erhe/geometry/erhe_geometry/operation/bake_transform.hpp` | `void bake_transform(...)` |
| `src/erhe/geometry/erhe_geometry/shapes/box.hpp` | `void make_box(mesh, ...)` |
| `src/erhe/geometry/erhe_geometry/shapes/sphere.hpp` | `void make_sphere(mesh, ...)` |
| `src/erhe/geometry/erhe_geometry/shapes/torus.hpp` | `void make_torus(mesh, ...)` |
| `src/erhe/geometry/erhe_geometry/shapes/cone.hpp` | `void make_cone(mesh, ...)` |
| `src/erhe/geometry/erhe_geometry/shapes/disc.hpp` | `void make_disc(mesh, ...)` |

### Scene Integration (for output node)

| File | Purpose |
|------|---------|
| `src/erhe/primitive/erhe_primitive/primitive_builder.hpp` | Build Buffer_mesh from Geometry |
| `src/erhe/scene/erhe_scene/mesh.hpp` | Mesh node attachment with Mesh_primitive vector |
| `src/editor/scene/scene_root.hpp` | Scene_root owns scene, layers, physics |
| `src/editor/scene/scene_builder.hpp` | Example: how meshes are created and added to scene |
| `src/editor/operations/mesh_operation.hpp` | Mesh before/after state for undo |

### Editor Integration (wired into the application)

| File | Purpose |
|------|---------|
| `src/editor/app_context.hpp` | Service locator -- geometry_graph_window pointer (done) |
| `src/editor/editor.cpp` | Construction -- Geometry_graph_window created with the other windows (done) |
| `src/editor/CMakeLists.txt` | Source files listed explicitly (done) |
| `src/editor/operations/operation_stack.hpp` | execute_now() used by geometry graph edits (done) |
| `src/editor/mcp/mcp_server.cpp` | geometry_graph_* MCP tools (done) |
