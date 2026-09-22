# operations/

Stability: stable

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
  - `Item_insert_remove_operation` -- insert/remove prims from the scene tree; also how a content-library resource enters and leaves a scene (`make_library_attach_operation` / `make_resource_insert_operation`, `operations/library_attach_operation.hpp`, which place the resource prim under an explicit prim or, without one, under its kind `Scope`; the attach form also records the library's per-resource bookkeeping)
  - `Item_parent_change_operation` -- reparent any `erhe::Hierarchy`: scene nodes, content-library resource prims and folder `Scope`s alike (the Hierarchy drag and MCP `reparent_item`)
  - `Item_reposition_in_parent_operation` -- reorder siblings
  - `Node_transform_operation` -- undo/redo node transforms
  - `Material_change_operation` -- undo/redo a whole `Material_data` snapshot (MCP `edit_material`); the Properties window records `Property_set_operation`s instead
  - `Merge_operation` -- merge multiple meshes

- **In-place vertex edits** (NOT `Mesh_operation`: they mutate and reuse the SAME `Geometry` object so `Mesh_component_selection` entries keyed on the Geometry pointer survive, then rebuild one `Primitive` and share it across every mesh referencing the Geometry):
  - `Move_mesh_vertices_operation` -- moves a vertex set of one primitive (mesh-component transform commit); refreshes normals, rebuilds static physics.
  - `Paint_weights_operation` -- rewrites `vertex_joint_indices_0` / `vertex_joint_weights_0` of a vertex set (one `Weight_paint_tool` stroke); no physics or normal work (positions unchanged), but the primitive rebuild refreshes the solid-wireframe / edge-line streams that carry their own copy of the joint data.

- **`Operations`** window -- ImGui window providing buttons for all geometry operations.

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
