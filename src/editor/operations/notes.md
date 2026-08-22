# operations/

## Purpose

Implements the undo/redo operation system and all concrete editor operations.

## Key Types

- **`Operation`** -- Abstract base class with `execute(App_context&)` and `undo(App_context&)`. Has a unique serial ID, a description string, and an error state (`set_error`/`get_error`/`has_error`). Operations that fail set the error instead of asserting.

- **`Operation_stack`** -- Manages three vectors: `m_queued`, `m_executed`, `m_undone`. Operations are queued via `queue()`, then executed during `update()` (called once per frame). Undo moves from `m_executed` to `m_undone`; redo moves back. Also an `Imgui_window` that displays the operation history. Binds Ctrl+Z/Ctrl+Y for undo/redo.

- **`Mesh_operation`** -- Base for operations that modify mesh geometry. Contains a list of `Entry` objects, each storing before/after mesh primitives and node physics. `make_entries()` helper applies a geometry transformation function to all selected meshes. After each geometry transform, the output is sanitized (`Geometry::sanitize()` - fixes degenerate facets and NaN/Inf vertices) and validated (`Geometry::validate()`). When sanitization fixes problems, the pre-operation input geometry is saved to `debug_geometry/` as a `.geogram` file for investigation.

- **`Compound_operation`** -- Groups multiple operations into a single undo step.

- **Geometry operations** (all extend `Mesh_operation`):
  - `Catmull_clark_subdivision_operation`, `Sqrt3_subdivision_operation`
  - Conway operators: `Dual`, `Ambo`, `Truncate`, `Kis`, `Join`, `Meta`, `Gyro`, `Chamfer`, `Subdivide`
  - `Triangulate`, `Reverse`, `Normalize`, `Repair`, `Weld`
  - `Generate_tangents`, `Make_raytrace`, `Bake_transform`

- **Binary operations** (extend `Compound_operation`): `Union`, `Intersection`, `Difference` -- CSG operations.

- **Scene operations**:
  - `Item_insert_remove_operation` -- insert/remove items from scene hierarchy
  - `Item_parent_change_operation` -- reparent nodes
  - `Item_reposition_in_parent_operation` -- reorder siblings
  - `Node_transform_operation` -- undo/redo node transforms
  - `Node_attach_operation` -- attach/detach node attachments
  - `Material_change_operation` -- undo/redo material property edits
  - `Merge_operation` -- merge multiple meshes

- **In-place vertex edits** (NOT `Mesh_operation`: they mutate and reuse the SAME `Geometry` object so `Mesh_component_selection` entries keyed on the Geometry pointer survive, then rebuild one `Primitive` and share it across every mesh referencing the Geometry):
  - `Move_mesh_vertices_operation` -- moves a vertex set of one primitive (mesh-component transform commit); refreshes normals, rebuilds static physics.
  - `Paint_weights_operation` -- rewrites `vertex_joint_indices_0` / `vertex_joint_weights_0` of a vertex set (one `Weight_paint_tool` stroke); no physics or normal work (positions unchanged), but the primitive rebuild refreshes the solid-wireframe / edge-line streams that carry their own copy of the joint data.

- **`Operations`** window -- ImGui window providing buttons for all geometry operations.

## Public API / Integration Points

- `Operation_stack::queue()` -- queue an operation for execution
- `Operation_stack::undo()` / `redo()` -- manual undo/redo
- `Operation_stack::update()` -- called once per frame from `Editor::tick()`

## Async Execution

Geometry operations run asynchronously via `async_for_nodes_with_mesh()` (in `items.cpp`), which creates `tf::AsyncTask` handles chained to any pending tasks for the same items. The operation callback runs on a worker thread, creates the `Mesh_operation`, and queues it to the operation stack. `Operation_stack::update()` executes queued operations on the main thread. `App_context::pending_async_ops` and `running_async_ops` (atomic counters) track in-flight operations.

`Async_raytrace_kickoff_operation` (the last sub-op of every glTF import compound, and open-scene / prefab-instantiate flows) launches one such task per mesh node. Each task is the deferred load finalize (doc/gltf-load-speedup-plan.md): it prepares the Geometry, the real triangle raytrace (replacing the load-time AABB proxy) and, when the load path deferred it, the full edge-lines buffer mesh on the worker without touching the live scene, then enqueues the swap on `App_context::scene_commit_queue` (`Scene_commit_queue`, scene/scene_commit_queue.hpp). `Editor::tick()` flushes that queue first thing every frame, so the commit (`Primitive_shape::commit_real_raytrace`, `Primitive_render_shape::commit_geometry_buffer_mesh`, `Mesh::update_rt_primitives`, raytrace instance detach / re-attach) runs on the main thread before anything else in the tick reads the scene - workers never mutate raytrace scenes or mesh primitives. Every step no-ops fast when the result already exists, so re-kickoffs and eager-load configurations are safe. `Operations::make_raytrace` uses the same two phases. `get_async_status.pending_scene_commits` (and `App_context::get_async_in_flight_count()`) counts commits not yet flushed.

## Dependencies

- erhe::scene, erhe::geometry, erhe::primitive, erhe::physics
- erhe::commands (for undo/redo key bindings)
- editor: App_context, Mesh_memory
