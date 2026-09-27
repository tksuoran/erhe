# Geometry spreadsheet window: remaining work

Status: in progress

This plan extends `doc/editor/geometry_spreadsheet.md` (target, columns, caches,
clipped drawing, sorting, row selection) with numeric cell editing with undo
and the large-mesh performance check. It is also the "Set vertex attribute values"
item of `doc/plans/mesh_component_selection.md`.

## 1. Requirements

- R4 A cell of an editable attribute (D6) can be edited in place. Committing
  the edit makes one undoable operation (D7). The viewport shows the new
  value on the frame the operation runs.
- R8 Performance, measured on a mesh with 1,000,000 vertices and ~6,000,000
  corners:
  - a steady-state frame with the window open does work proportional to the
    visible cells only, and does no heap allocation;
  - row caches are rebuilt only on change events;
  - a frame with the window hidden costs nothing.

## 2. Design

### D6 Editability

- Editable:
  - `position`;
  - normals, texcoords, colors, tangents and bitangents in every domain;
  - `joint_indices` and `joint_weights`;
  - `aniso_control`;
  - `edge_sharpness`.
- Read-only, because the geometry pipeline derives them: `facet_id`,
  `facet_centroid`, `vertex_normal_smooth`, `vertex_valency_edge_count`, and
  the structural columns other than `position`
  (`doc/editor/geometry_spreadsheet.md` section 2).

### D7 Edit and undo

- A double-click on an editable cell, or Enter on a focused cell, opens an
  `ImGui::InputScalar` in that cell. The window holds only one active edit:
  its domain, element, column and a pre-edit value copy.
- The commit happens on `ImGui::IsItemDeactivatedAfterEdit()`, which is the
  change site from `AGENTS.md` "No update each frame patterns". It builds one
  operation and runs it through `Operation_stack`. Escape cancels the edit.
- **Position edits** go through the existing `Move_mesh_vertices_operation`.
  It already recomputes the stale baked normals (`refresh_geometry_normals`)
  and rebuilds the primitives of every mesh that shares the geometry.
- **Other attributes** go through a new `Set_geometry_attribute_operation`
  (`src/editor/operations/set_geometry_attribute_operation.{hpp,cpp}`):
  - Parameters: mesh, primitive index, geometry, domain, attribute name, a
    `std::vector<GEO::index_t>` of elements, before and after values (as
    `glm::vec4` payloads plus before and after presence flags), `build_info`
    and `normal_style`.
  - `execute` and `undo` write the values in place, keeping the `Geometry`
    identity so component selection entries survive. They then rebuild the
    primitive for every mesh that shares the geometry, following the
    `Paint_colors_operation` pattern under `item_host_mutex`, publish
    `Mesh_geometry_changed_message`, and call `kickoff_deferred_finalize()`.
  - `edge_sharpness` edits reuse `Set_edge_sharpness_operation`, which has
    no primitive rebuild.
- **Shared geometry** follows the same policy as
  `Move_mesh_vertices_operation`: the edit applies to every mesh that shares
  the geometry.
- **Fill down:** with several rows selected
  (`doc/editor/geometry_spreadsheet.md` section 5), "Set selected rows" in the
  column's context menu writes the edited value to every selected row, as one
  operation.

## 3. Phases

Each phase ends with a build of every target listed in `AGENTS.md`
"Building", a headless MCP verification, and one commit.

1. **Editing (D6, D7).** Add `Set_geometry_attribute_operation`, in-place
   cell editing, position edits through `Move_mesh_vertices_operation`, and
   fill down. Add an MCP tool, `set_mesh_attribute_values`, that makes the
   same operation, so headless tests exercise the edit path. Verify with
   `scripts/geometry_spreadsheet_verify.py`:
   - set a vertex color, check it with `get_mesh_attribute_values` and
     `get_mesh_buffer_data` (the GPU buffer), then undo and redo;
   - set a position and check that normals are recomputed;
   - check with a `capture_screenshot` before and after.
2. **Performance check (R8).** Load or create a mesh with 1,000,000
   vertices and open the window on the Corner tab:
   - use Tracy to confirm that the window's zone time is flat when scrolling
     from the top of the table to the bottom;
   - use Tracy to confirm that no allocations show in steady-state frames;
   - confirm that a hidden window records no zone.

   Record the measured numbers in `doc/editor/geometry_spreadsheet.md`.

## 4. Documentation on landing

- `doc/editor/geometry_spreadsheet.md` gets the edit path (D7), stated as the
  current design.
- The "Set vertex attribute values" item is removed from
  `doc/plans/mesh_component_selection.md`, and this plan is deleted.
