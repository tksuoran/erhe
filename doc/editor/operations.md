# operations/

Stability: stable

## Purpose

Implements the undo/redo operation system and all concrete editor operations.

## Key Types

- **`Operation`** -- Abstract base class with `execute(App_context&)` and `undo(App_context&)`. Has a unique serial ID, a description string, and an error state (`set_error`/`get_error`/`has_error`). Operations that fail set the error instead of asserting.

- **`Operation_stack`** -- Manages three vectors: `m_queued`, `m_executed`, `m_undone`. Operations are queued via `queue()`, then executed during `update()` (called once per frame). Undo moves from `m_executed` to `m_undone`; redo moves back. Also an `Imgui_window` that displays the operation history. Binds Ctrl+Z/Ctrl+Y for undo/redo.

- **`Mesh_operation`** -- Base for operations that modify mesh geometry. Contains a list of `Entry` objects, each storing before/after mesh primitives and node physics. `make_entries()` helper applies a geometry transformation function to all selected meshes. After each geometry transform, the output is sanitized (`Geometry::sanitize()` - fixes degenerate facets and NaN/Inf vertices) and validated (`Geometry::validate()`). When sanitization fixes problems, the pre-operation input geometry is saved to `debug_geometry/` as a `.geogram` file for investigation. A result with no facets (every face deleted, a merge by distance that collapses the mesh) is a legal result: it yields an empty `Primitive` that renders and raytraces nothing (`doc/erhe/primitive.md` "Empty primitives"), the mesh keeps it (no rigid body, since it has no convex hull), and undo restores the previous primitive like for any other operation.

- **`Compound_operation`** -- Groups multiple operations into a single undo step.

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

- **Binary operations** (extend `Compound_operation`): `Union`, `Intersection`, `Difference` -- CSG operations.

- **Scene operations**:
  - `Item_insert_remove_operation` -- insert/remove prims from the scene tree; also how a content-library resource enters and leaves a scene (`make_library_attach_operation` / `make_resource_insert_operation`, `operations/library_attach_operation.hpp`, which place the resource prim under an explicit prim or, without one, under its kind `Scope`; the attach form also records the library's per-resource bookkeeping)
  - `Item_parent_change_operation` -- reparent any `erhe::Hierarchy`: scene nodes, content-library resource prims and folder `Scope`s alike (the Hierarchy drag and MCP `reparent_item`)
  - `Item_reposition_in_parent_operation` -- reorder siblings
  - `Node_transform_operation` -- undo/redo node transforms
  - `Material_change_operation` -- undo/redo a whole `Material_data` snapshot (MCP `edit_material`); the Properties window records `Property_set_operation`s instead
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

- **`Operations`** window -- ImGui window providing buttons for all geometry operations. Its "Components" section holds the delete and dissolve buttons, each enabled in the component mode whose set it reads (Delete Vertices and Dissolve Vertices in vertex mode; Delete Edges, Delete Only Edges and Faces and Dissolve Edges in edge mode; Delete Faces, Delete Only Faces and Dissolve Faces in face mode; Limited Dissolve with a component or a mesh selection), and the dissolve options as widgets; the options are `Operations` members read by the buttons and the `Geometry.Dissolve.*` commands. The commands are `Geometry.Delete.Vertices` / `.Edges` / `.Faces` / `.OnlyEdgesAndFaces` / `.OnlyFaces`, `Geometry.Dissolve.Faces` / `.Edges` / `.Vertices` / `.Limited`, and the mode-dispatching `Geometry.Delete.Selected` (Delete) and `Geometry.Dissolve.Selected` (Ctrl+X) of `doc/editor/mesh_component_selection.md`. The same section holds the merge buttons - Merge at Center, at Cursor, at First, at Last, Collapse (enabled with a selection in vertex, edge or face mode) and Merge by Distance (with a component or a mesh selection) - with the UVs checkbox and the by-distance threshold, centroid and include-unselected widgets as `Operations` members; the commands are `Geometry.Merge.AtCenter` (M) / `.AtCursor` / `.AtFirst` / `.AtLast` / `.Collapse` / `.ByDistance`. `Geometry.Merge.AtCursor` logs and does nothing when no content point is hovered. The Subdivide Edges button (enabled with a selection in vertex, edge or face mode) comes with the cuts, smoothness and only-quads widgets as `Operations` members; its command is `Geometry.Subdivide.Edges` (no key: Blender reaches it from a menu). The Split and Separate buttons (enabled with a selection in vertex, edge or face mode) and the Rip button (vertex or edge mode) run `Geometry.Split.Selected` (Y), `Geometry.Separate.Selection` (P) and `Geometry.Rip.Selected` (V; toward the component tool's last hovered content point); each declines without a live selection in a mode it reads. The Fill button (vertex, edge or face mode) and the Connect button (vertex or edge mode) run `Geometry.Fill.Selected` (F) and `Geometry.Connect.Selected` (J); both also decline while a modal component edit runs. The Inset button (enabled with a face mode selection) comes with the thickness and depth drags and the boundary, even offset, relative offset, edge rail, outset, individual and interpolate checkboxes (`Operations::m_inset_options`); it runs the numeric inset `Mesh_component_selection_tool::inset()` (one `Fork_geometry_operation` "Inset", `doc/editor/mesh_modeling.md`), not a `Mesh_operation`. The Bevel button (enabled with an edge or vertex mode selection) comes with the amount drag, the offset type combo (offset, width) and the loop slide checkbox (`Operations::m_bevel_options`); it runs the numeric bevel `Mesh_component_selection_tool::bevel()` (one `Fork_geometry_operation` "Bevel", `doc/editor/mesh_modeling.md`), not a `Mesh_operation`.

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
