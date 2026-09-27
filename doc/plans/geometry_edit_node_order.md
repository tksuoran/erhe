# Geometry edits keep the node's place in the hierarchy

Status: proposed

This plan extends `doc/editor/operations.md`: the operations that swap a
mesh's primitives keep the mesh node at its position among its siblings.

## Problem

To re-attach the raytrace (and rebuild static physics) after
`Mesh::set_primitives()`, these operations detach the mesh node and attach it
again with `set_parent(parent)`, which appends the node as the parent's last
child:

- `src/editor/operations/mesh_operation.cpp` (execute and undo)
- `src/editor/operations/fork_geometry_operation.cpp`
- `src/editor/operations/merge_operation.cpp`
- `src/editor/operations/move_mesh_vertices_operation.cpp`
- `src/editor/operations/paint_colors_operation.cpp`
- `src/editor/operations/paint_weights_operation.cpp`
- `src/editor/operations/set_geometry_attribute_operation.cpp`

Observed: a Geometry Spreadsheet corner color edit
(`Set_geometry_attribute_operation`) on a box created first in the Default
Scene moved it from the top of the Scene Hierarchy to the bottom. Every
listed operation has the same dance, so a Catmull-Clark, a vertex move or a
paint stroke reorders the hierarchy too, and a saved scene records the new
order.

## Design

- Each re-attach records `get_index_in_parent()` before the detach and
  re-attaches with `set_parent(parent, position)`
  (`src/erhe/item/erhe_item/hierarchy.hpp`).
- The "rebuild one Primitive and share it across every mesh referencing the
  Geometry" block is the same code in Paint_colors, Paint_weights,
  Move_mesh_vertices and Set_geometry_attribute operations. It becomes one
  helper in `src/editor/operations/` (collect referers, swap primitives,
  re-attach at the recorded position, publish `Mesh_geometry_changed_message`),
  so the position rule is written once.
- Whether the detach / re-attach is needed at all (versus refreshing the
  raytrace instance and physics directly) is decided first: a direct refresh
  removes the reorder and the per-edit scene-graph churn together.

## Verification

- A headless MCP script: create three shapes, record `get_scene_nodes` order,
  run each listed operation (catmull_clark, transform of a vertex selection,
  set_mesh_attribute_values, paint via MCP where available, merge, fork) and
  its undo, and check the sibling order is unchanged after each.
- `scripts/geometry_spreadsheet_verify.py` and the existing MCP suites still
  pass.
