# Graph mesh asset and the geometry graph mesh binding

Stability: mostly stable

A geometry node graph is a first-class, selectable, serializable asset -
`Graph_mesh` - in a scene's content library, and a scene `Node` sources its
mesh from such an asset by naming it in the node's own
`Geometry_graph_mesh.graph_mesh` value. The binding *controls* the node's mesh
(its geometry is produced by the graph) and *points back* to the graph asset
that produced it. This is the scene-side analogue of `Graph_texture` and a
material's texture reference (`doc/editor/graph_texture.md`).

The graph engine, node classes, async evaluation, undo operations,
serialization format and MCP surface are the geometry graph's
(`doc/editor/geometry_nodes.md`); this document covers who owns a graph, the
binding, and their persistence.

## The back-link is a node value, not a low-level geometry source

The texture case needed a low-level seam (`Texture_reference` in a material
slot) because the consumer and the resolver both live below the editor, and
textures are cheap to re-resolve every frame - a pull model.

Geometry is structurally different. Producing the mesh is expensive (geometry
copy, process, GPU primitive build) and already runs through an explicit push
pipeline: async worker evaluate, then main-thread apply. A per-frame
pull-resolve seam like a `Geometry_reference` would be an unused fiction,
because nothing may rebuild geometry per frame.

So the back-link is a value of the consuming `Node` itself: the attached value
group `Geometry_graph_mesh` (`src/editor/geometry_graph/geometry_graph_mesh.{hpp,cpp}`,
`doc/erhe/property_system.md` section 4.25), whose key and only property
`Geometry_graph_mesh.graph_mesh` is a strong object reference to the asset -
the same shape USD gives `material:binding`, a relationship from the prim to a
resource prim. `read_geometry_graph_mesh(node)` returns the effective value as
a plain record, and `set_geometry_graph_mesh(node, graph_mesh)` writes it
(a null graph clears the value). Every consumer - the Properties row, the
Hierarchy drag-drop, MCP `set_node_graph_mesh`, `set_item_property` and both
importers - goes through those two.

The value carries no serialize flag: the binding's file carriers are the
native ones (see "Persistence"), so authoring it into `ERHE_node.properties`
too would give one binding two authorities that drift apart. A clone of a
bound node copies the value, so a duplicated node shows the same graph's bake
from its own mesh.

## `Geometry_graph_mesh_system` owns the controlled products

The runtime state the binding implies - the controlled `Mesh`, the ghost
`Mesh`, the controlled `Node_physics` and the bake revision already applied -
is owned by `Geometry_graph_mesh_system`
(`src/editor/geometry_graph/geometry_graph_mesh_system.{hpp,cpp}`), one per
scene, held by `Scene_root` and driven by the three node-system change sites
(`doc/erhe/scene.md` "Node systems"). It keeps one `Geometry_graph_mesh_entry`
per bound node, keyed by a raw `Node*`:

- **`on_node_registered`** creates the entry, and asks the evaluation engine
  for a push (`Graph_mesh::request_node_push()`) when the graph already holds
  a bake. Applying inline is not allowed here - the caller may hold
  `item_host_mutex`.
- **`on_values_changed`** releases what the previous graph controlled - a
  rebind target may never publish a bake, and a stale mesh must not linger -
  and applies the new graph's latest bake at once, so a late binder needs no
  evaluation.
- **`on_node_unregistered`** releases the products and erases the entry, so a
  scene close keeps nothing of it alive.

The renderer keeps rendering a perfectly ordinary `erhe::scene::Mesh`: the
system creates that child `Mesh` prim (or ADOPTS a pre-existing one, so a
graph dropped onto a mesh node takes it over instead of adding a duplicate),
replaces its primitives whenever the graph re-bakes, and keeps `Node_physics`
in sync. Layering holds by construction: the system lives in `src/editor/` and
`erhe::scene` only supplies the `INode_system` interface.

Detaching a `Mesh` the system controls is a legal state: the detach keeps the
removed `Mesh` alive, so the binding neither recreates it nor writes visible
output, and an undo re-attaches the same `Mesh` object with its baked geometry
intact.

## `Graph_mesh`

`Graph_mesh` (`src/editor/geometry_graph/graph_mesh.{hpp,cpp}`) is a
`not_clonable` `erhe::Item` with its own content-library kind. Duplication goes
through serialization, as it does for `Graph_texture`.

It owns the `Geometry_graph` by value and the node `shared_ptr` vector, plus
the **baked products** its output node publishes after each evaluation:
`{shared_ptr<Geometry>, shared_ptr<Primitive>, shared_ptr<ICollision_shape>,
uint64_t revision}`. Bound attachments consume those products, and the revision
lets a bind apply the latest bake without re-evaluating.

That is the payoff over the texture case: **N scene nodes can source one graph
asset**. The products are shared pointers, so every bound node's mesh shares
the same GPU primitive.

A bound node is a registered user of the asset, because the value holds a
strong reference: the graph stays alive while a node names it, and the
reference dies with the node.

## Window edits the selected asset; one evaluation run at a time

`Geometry_graph_window` keeps a **scratch default** `Graph_mesh`, edited when
nothing is selected, and resolves the current asset each frame from the
selection (unwrapping `Content_library_node`), falling back to the scratch. The
palette toolbar shows either "Editing asset: <name>" or "(scratch - not saved)"
plus a "New Graph Mesh" button, and the content library has a right-click
"Create Graph Mesh" that creates *and selects* the asset. A create path in the
UI is mandatory: an asset type with no way to make one is unreachable.

`update_evaluation()`, one call per frame from `editor.cpp`, is a scheduler. At
most **one `Evaluation_run` is in flight at a time**, targeting whichever graph
- the scratch or any asset in any scene's content library - needs evaluation,
round-robin. `Evaluation_run` carries its target `shared_ptr<Graph_mesh>`, and
finishing applies to that asset's live nodes. One at a time keeps the engine
simple for single-editor workloads and preserves the existing counters and
logging. `wait_for_idle_evaluation()`, the barrier `get_geometry_graph` and
graph save use, loops until no graph needs evaluation and no run is in flight,
so the MCP eventual-consistency contract is unchanged.

The undo operation classes and save / load / clear capture a
`shared_ptr<Graph_mesh>` rather than the window, so undo and redo stay correct
when the selection changes between an edit and its undo.

## The output node publishes to the owning asset

`Geometry_output_node` keeps its two-phase evaluate: the worker builds
`m_evaluated_{geometry,primitive,collision_shape}`, the main thread applies
them. The apply phase forks on ownership:

- **Asset-owned graph** (the node has a non-null owning `Graph_mesh`, wired at
  insert): apply moves the products into the asset
  (`Graph_mesh::set_baked_products`, bumping the revision) and creates no scene
  node of its own. `Geometry_graph_window::apply_baked_products_to_bound_nodes()`
  then asks every scene's `Geometry_graph_mesh_system` to apply the asset to
  the nodes bound to it, from that system's own entry map - evaluations finish
  rarely, so this costs nothing per frame - and each node's mesh primitives and
  physics are swapped under the scene `item_host_mutex`. An asset with no bound
  node renders nothing, exactly like a `Graph_texture` no material samples.
- **Scratch graph** (no owning asset): the output node creates and owns its own
  scene node and mesh, which is what the geometry-graph smoke sweep exercises.

Writing a binding - from the UI, from MCP or on scene load - applies the
asset's current baked products immediately when the revision shows a bake
exists. A freshly loaded scene's graphs are born dirty, so the first evaluation
pushes to every binding.

Physics stays configured on the output node, graph-side: the baked collision
shape and the motion mode / enable travel with the products, and each bound
node gets its own `Node_physics` from them.

## Persistence

Two codegen structs in `scene.json`:

- `Graph_mesh_data { name, graph }` - the asset, with the graph as a JSON
  string blob in the geometry-graph v1 format that save, load and groups
  already use.
- `Graph_mesh_binding_data { node_id, graph_mesh_name }` - one per bound node,
  `node_id` being the file-local id, the `Node_physics_data` convention.

These two, the glTF `ERHE_node_graphs` `node_bindings` array and the USD
`erhe:scene` block's `graph_meshes.bound_prims` are the binding's native
carriers, fed from `read_geometry_graph_mesh(node)` and consumed by writing
the node's value (`doc/plans/node_attachments_to_properties.md` D8).

Load reconstructs the assets (`graph_meshes->make<Graph_mesh>(name)` plus a
`read_parameters`-based graph parse, degrading to a `log_parsers->warn` and a
skip on a malformed graph), then re-resolves the bindings by node id and asset
name, writes each node's `Geometry_graph_mesh.graph_mesh`, and lets the first
evaluation populate the mesh. An orphan asset with no binding is preserved.

## Verification

- `scripts/geometry_nodes_smoke_test.py` covers asset create / select / edit,
  bind, re-evaluate and unbind, the save and reload round trip, and a
  shared-asset check with two nodes bound to one asset. Its scratch-graph
  sections are the regression net for the unchanged legacy path.
- `scripts/scene_roundtrip_verify.py`'s geometry-graph leg round-trips a bound
  prim through USD, and its build-scene / reload-diff sections do the same
  through glTF.
- Headless verification runs over the in-editor MCP server on the headless
  Vulkan build, with the stale-editor hygiene protocol (kill editors, assert
  pid and build through `get_server_info`) before every run.

## Future work

- [plans/geometry_graph/geometry_nodes.md](../plans/geometry_graph/geometry_nodes.md) -
  the open gaps of the asset and attachment, and parallel per-asset evaluation.
