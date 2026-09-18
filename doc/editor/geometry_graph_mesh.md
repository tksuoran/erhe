# Graph mesh asset and the geometry graph mesh attachment

Stability: mostly stable

A geometry node graph is a first-class, selectable, serializable asset -
`Graph_mesh` - in a scene's content library, and a scene `Node` sources its
mesh from such an asset through a `Geometry Graph Mesh` node attachment. The
attachment *controls* the node's mesh (its geometry is produced by the graph)
and *points back* to the graph asset that produced it. This is the scene-side
analogue of `Graph_texture` and a material's texture reference
(`doc/editor/graph_texture.md`).

The graph engine, node classes, async evaluation, undo operations,
serialization format and MCP surface are the geometry graph's
(`doc/editor/geometry_nodes.md`); this document covers who owns a graph, the
attachment, and their persistence.

## The back-link is a `Node_attachment`, not a low-level geometry source

The texture case needed a low-level seam (`Texture_reference` in a material
slot) because the consumer and the resolver both live below the editor, and
textures are cheap to re-resolve every frame - a pull model.

Geometry is structurally different. Producing the mesh is expensive (geometry
copy, process, GPU primitive build) and already runs through an explicit push
pipeline: async worker evaluate, then main-thread apply. A per-frame
pull-resolve seam like a `Geometry_reference` would be an unused fiction,
because nothing may rebuild geometry per frame. And the consumer is a scene
`Node`, for which `erhe::scene` already has a polymorphic, editor-implementable
extension point: `Node_attachment`, with `Node_physics` as the precedent.

So the back-link is `editor::Geometry_graph_mesh : erhe::scene::Node_attachment`
holding a `shared_ptr<Graph_mesh>`. The renderer keeps rendering a perfectly
ordinary `erhe::scene::Mesh`; the attachment creates that sibling `Mesh`
attachment, replaces its primitives whenever the graph re-bakes, and keeps
`Node_physics` in sync. Layering holds by construction: the attachment class
lives in `src/editor/` and `erhe::scene` is untouched.

**Lifecycle trap: never fire `handle_node_update` on detach from
`Node_attachment::set_node`.** It is a use-after-free when
`handle_remove_attachment` drops the last reference, and detach-time flag
propagation perturbs `Mesh`'s raytrace enable path. The attachment instead
remembers its controlled node (`m_controlled_node`, a `weak_ptr`) and releases
it through `handle_item_host_update` (detach from an in-scene node), move
handling in `handle_node_update`, reclaim-in-apply, and release-on-any-rebind.

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
  node of its own. The engine then pushes to every `Geometry_graph_mesh`
  attachment bound to that asset, enumerated from the scenes' flat node lists
  at finish time - evaluations finish rarely, so this costs nothing per frame -
  and each attachment swaps its mesh primitives and physics under the scene
  `item_host_mutex`. An asset with no bound node renders nothing, exactly like
  a `Graph_texture` no material samples.
- **Scratch graph** (no owning asset): the output node creates and owns its own
  scene node and mesh, which is what the geometry-graph smoke sweep exercises.

Binding an attachment - from the UI, from MCP or on scene load - applies the
asset's current baked products immediately when the revision shows a bake
exists. A freshly loaded scene's graphs are born dirty, so the first evaluation
pushes to every binding.

Physics stays configured on the output node, graph-side: the baked collision
shape and the motion mode / enable travel with the products, and each bound
attachment materializes its own `Node_physics` from them.

## Persistence

Two codegen structs in `scene.json`:

- `Graph_mesh_data { name, graph }` - the asset, with the graph as a JSON
  string blob in the geometry-graph v1 format that save, load and groups
  already use.
- `Graph_mesh_binding_data { node_id, graph_mesh_name }` - one per
  `Geometry_graph_mesh` attachment, `node_id` being the file-local id, the
  `Node_physics_data` convention.

Load reconstructs the assets (`graph_meshes->make<Graph_mesh>(name)` plus a
`read_parameters`-based graph parse, degrading to a `log_parsers->warn` and a
skip on a malformed graph), then re-resolves the bindings by node id and asset
name, attaches a `Geometry_graph_mesh`, and lets the first evaluation populate
the mesh. An orphan asset with no binding is preserved.

## Verification

- `scripts/geometry_nodes_smoke_test.py` covers asset create / select / edit,
  attachment bind, re-evaluate and unbind, the save and reload round trip, and
  a shared-asset check with two nodes bound to one asset. Its scratch-graph
  sections are the regression net for the unchanged legacy path.
- Headless verification runs over the in-editor MCP server on the headless
  Vulkan build, with the stale-editor hygiene protocol (kill editors, assert
  pid and build through `get_server_info`) before every run.

## Future work

- [plans/geometry_graph/geometry_nodes.md](../plans/geometry_graph/geometry_nodes.md) -
  the open gaps of the asset and attachment, and parallel per-asset evaluation.
