# operations/

## Purpose

Implements the undo/redo operation system and all concrete editor operations.

## Key Types

- **`Operation`** -- Abstract base class with `execute(App_context&)` and `undo(App_context&)`. Has a unique serial ID and a description string.

- **`Operation_stack`** -- Manages three vectors: `m_queued`, `m_executed`, `m_undone`. Operations are queued via `queue()`, then executed during `update()` (called once per frame). Undo moves from `m_executed` to `m_undone`; redo moves back. Also an `Imgui_window` that displays the operation history. Binds Ctrl+Z/Ctrl+Y for undo/redo.

- **`Mesh_operation`** -- Base for operations that modify mesh geometry. Contains a list of `Entry` objects, each storing before/after mesh primitives and node physics. `make_entries()` helper applies a geometry transformation function to all selected meshes.

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

- **`Operations`** window -- ImGui window providing buttons for all geometry operations.

## Public API / Integration Points

- `Operation_stack::queue()` -- queue an operation for execution
- `Operation_stack::undo()` / `redo()` -- manual undo/redo
- `Operation_stack::update()` -- called once per frame from `Editor::tick()`

## Dependencies

- erhe::scene, erhe::geometry, erhe::primitive, erhe::physics
- erhe::commands (for undo/redo key bindings)
- editor: App_context, Mesh_memory
