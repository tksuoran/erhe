# Geometry spreadsheet window

Status: proposed

This plan extends `doc/editor/windows.md` and
`doc/editor/mesh_component_selection.md` with a spreadsheet-style editor
window that shows and edits the per-element attributes of one mesh
primitive's `erhe::geometry::Geometry` as numbers. It covers the vertex,
corner, facet and edge domains. It is the "Set vertex attribute values"
item of `doc/plans/mesh_component_selection.md` given a UI of its own.

## 1. Requirements

- R1 One window, "Geometry Spreadsheet", with one tab per domain: Vertex,
  Corner, Facet and Edge. A tab shows one row per element of that domain.
- R2 Columns are the element index, then the structural columns of the
  domain (D4), then one column group per attribute of the domain present in
  the geometry. A group has one column per component (`x y z w`, `u v`,
  `r g b a`, joint slots `0..3`).
- R3 A cell whose element has no value for the attribute (the
  `Attribute_present<T>::present` flag is false) is drawn dimmed with the
  text `-`.
- R4 A cell of an editable attribute (D6) can be edited in place. Committing
  the edit makes one undoable operation (D7). The viewport shows the new
  value on the frame the operation runs.
- R5 The window targets one (mesh, primitive index). By default the target
  follows the selection (D2). A pin button freezes the current target, as in
  `doc/editor/window_target_items.md`.
- R6 Row selection and the mesh component selection are the same state in
  the vertex, facet and edge domains (D8). A "Selected only" toggle limits
  the rows to the selected components.
- R7 Clicking a column header sorts by that column (ImGui table sort specs).
- R8 Performance, measured on a mesh with 1,000,000 vertices and ~6,000,000
  corners:
  - a steady-state frame with the window open does work proportional to the
    visible cells only, and does no heap allocation;
  - row caches are rebuilt only on the change events of D3;
  - a frame with the window hidden costs nothing.

## 2. Design

### D1 Placement and window class

- `Geometry_spreadsheet_window` goes in
  `src/editor/windows/geometry_spreadsheet_window.{hpp,cpp}`. It is an
  `erhe::imgui::Imgui_window` constructed in `editor.cpp`'s
  `some_windows_task`, next to `Properties`, with ini label
  `"geometry_spreadsheet"`. It shows up in the Windows menu through
  `Imgui_windows::window_menu_entries()`.
- Its constructor takes `App_context&` and `App_message_bus&`, the way
  `Properties` does.

### D2 Target resolution

- The target is `std::weak_ptr<erhe::scene::Mesh>` plus a primitive index.
  The window holds no strong reference, so it never keeps an undone import
  alive (`doc/editor/import_undo_reference_clearing.md`).
- In follow mode the window re-resolves the target only when one of these
  messages arrives: `Selection_message`, `Active_item_changed_message` or
  `Mesh_component_mode_changed_message`. The rules, in order: the mesh of the
  live `Mesh_component_selection` entry when there is exactly one; otherwise
  `Selection::get_active_item_as<Node>()`'s mesh; otherwise
  `get_last_selected<Mesh>()`.
- If the mesh has more than one primitive, a combo selects which one.
- The geometry is read with `Primitive_shape::get_geometry_const()`, which
  never blocks (`doc/erhe/primitive_shape_locking.md`). When it returns null
  the window shows "geometry not yet available" and waits for the next
  change event.

### D3 Caches and their invalidation

The window keeps two cache levels. It rebuilds them only when an event
arrives, never by polling.

- **Column layout cache** (`m_columns`, one per domain tab). Each entry is a
  `Spreadsheet_column`:
  - a type-erased accessor: an enum `Value_type` (`f32`, `u32`, `i32`), the
    component count, the component index, and a pointer to the
    `Attribute_present<T>`, or a structural kind;
  - the header label, pre-formatted into a fixed `char[32]`;
  - the `editable` flag (D6) and the width measured at rebuild.

  The layout is rebuilt when the target geometry identity changes, when
  `Mesh_geometry_changed_message` names the target mesh, or when the domain
  tab changes. An attribute enters the layout when at least one element has
  it present. That check is a scan of the `present` attribute, done at
  rebuild only.
- **Row index cache** (`std::vector<GEO::index_t> m_rows`, one per domain
  tab).
  - With no filter and no sort, the cache is empty and row `i` is element
    `i`. A million-vertex mesh then costs no memory for rows.
  - With "Selected only" or a sort, the cache holds the element indices in
    display order.
  - It is rebuilt when any of these happens: a layout rebuild; the filter
    toggle changes; `ImGuiTableSortSpecs::SpecsDirty` is set; the component
    selection changes (D8).
  - The rebuild calls `clear()` and refills, so the capacity reaches its
    high-water mark and then stops allocating.
  - Sorting uses `std::sort` over `m_rows`, comparing live attribute values
    through the column accessor.
- **Change events:**
  - Every operation that edits geometry already publishes
    `Mesh_geometry_changed_message` (`Mesh_operation`, paint colors, paint
    weights, move vertices, fork, merge).
  - `Set_edge_sharpness_operation` does not publish it yet. This plan adds
    that message to it, because the window's edge tab and the crease overlay
    both depend on that value.
- **Hidden window:** `hidden()` clears both caches and releases the target's
  geometry pointers, the way `Item_tree_window::hidden()` does.

The cell values themselves are not cached. They are read live from the
geometry for the visible cells only (D5). Reading live costs about the same
as reading a cached copy. It also stays correct for in-place mutations that
announce nothing, such as a live `Paint_tool` dab, so there is no cache that
can go stale.

### D4 Structural columns

These read-only columns come straight from `GEO::Mesh` and the `Geometry`
connectivity:

- Vertex: `position` (`get_pointf`), which is editable (D6), and the
  valence (`get_vertex_corners(v).size()`).
- Corner: `vertex`, `facet` (`Geometry::get_corner_facet`).
- Facet: corner count and the first corner index.
- Edge: `v0`, `v1`, and the facet count (`get_edge_facets(e).size()`). The
  Edge tab lists edges only when the geometry has them (`edges.nb() > 0`).
  Otherwise it shows the note "edges not built" and no rows.

### D5 Per-frame drawing

- `ImGui::BeginTable` with these flags: `ScrollX | ScrollY | RowBg |
  BordersInnerV | Resizable | Hideable | Sortable | SizingFixedFit`. Use
  `TableSetupScrollFreeze(1, 1)` so the index column and the header stay in
  view.
- Rows are clipped with `ImGuiListClipper` over the row count (`m_rows.size()`
  or the element count). Only `DisplayStart..DisplayEnd` is visited, as in
  `Item_tree::imgui_tree`.
- Columns are clipped the same way: `ImGui::TableSetColumnIndex()` returns
  false for a column scrolled out horizontally or hidden, and that cell is
  skipped before any value is read or formatted. A frame's work is therefore
  proportional to the visible rows times the visible columns.
- Formatting goes into a stack `char[32]` with `std::to_chars` (the shortest
  round-trip form, or fixed precision from a window setting). The result is
  drawn with `ImGui::TextUnformatted(begin, end)`. There are no `std::string`
  or `fmt::format` calls in the loop.
- Each row's ID is its element index (`ImGui::PushID(int)`), with no string
  labels.
- The steady state allocates nothing, per `AGENTS.md` "Run-time Memory
  Allocation Discipline".

### D6 Editability

- Editable:
  - `position`;
  - normals, texcoords, colors, tangents and bitangents in every domain;
  - `joint_indices` and `joint_weights`;
  - `aniso_control`;
  - `edge_sharpness`.
- Read-only, because the geometry pipeline derives them: `facet_id`,
  `facet_centroid`, `vertex_normal_smooth`, `vertex_valency_edge_count`, and
  the D4 structural columns other than `position`.

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
- **Fill down:** with several rows selected, "Set selected rows" in the
  column's context menu writes the edited value to every selected row, as one
  operation.

### D8 Selection sync

- A click on a row's index cell in the Vertex, Facet or Edge tab updates
  `Mesh_component_selection`'s entry for the target:
  - plain click replaces the selection;
  - Ctrl toggles the row;
  - Shift selects a range in display order.
- The Corner tab has no component kind of its own, so its selection is local
  to the window. It holds a `std::vector<GEO::index_t>`, cleared on a layout
  rebuild.
- `Mesh_component_selection`'s mutators publish no message today, because
  `tool_render` reads the selection every frame. This plan adds
  `Mesh_component_selection_changed_message` to `App_message_bus`. The
  mutators publish it: set, add, toggle, clear, grow, shrink,
  `set_after_operation` and prune. The window rebuilds its "Selected only"
  row cache on that message.
- Row highlight reads the selection sets directly for the visible rows only.
  A `std::set::contains` per visible row is bounded by the visible count.
- Hovering a row publishes nothing. A hovered row in the Vertex, Facet or
  Edge tab sets `Mesh_component_selection_tool`'s external hover (domain,
  element). `tool_render` draws it in the hover color on the next frame, and
  the window clears it when the pointer leaves the table.

### D9 Shared attribute enumeration

- `for_each_{facet,vertex,corner}_attribute` and `attribute_value_json` live
  in `src/editor/mcp/mcp_server_shared.hpp` today.
- The enumeration (name, domain and `Attribute_present<T>&`) moves into
  `erhe::geometry` as `Mesh_attributes::for_each_facet_attribute(F&&)` and
  its vertex, corner and edge siblings. MCP and the spreadsheet then list
  attributes from one place, so a new attribute appears in both.
- This is a public API change of `erhe::geometry` and needs a `CHANGELOG.md`
  line.

## 3. Phases

Each phase ends with a build of every target listed in `AGENTS.md`
"Building", a headless MCP verification, and one commit.

1. **Shared enumeration (D9).** Move the helpers, switch the MCP handlers
   over, and add the CHANGELOG line. Verify: `get_mesh_geometry_info` and
   `get_mesh_attribute_values` output is unchanged on a test mesh (compare
   the JSON before and after).
2. **Read-only window (D1-D5).** Domain tabs, the column layout and row
   caches, clipped drawing, sorting, and follow and pin targeting. Verify
   with the MCP UI-driving tools (`doc/agents/mcp_ui_driving.md`):
   - open the window, then read the table's visible cell text for a known
     mesh and compare it with `get_mesh_attribute_values`;
   - run Catmull-Clark on the target and check that the window shows the new
     element count;
   - undo and check that it shows the old count.
3. **Selection sync (D8).** Add `Mesh_component_selection_changed_message`,
   row click to component selection, "Selected only", and hover highlight.
   Verify:
   - `select_mesh_components` via MCP, then check that the window's visible
     row set equals the selection;
   - click a row via UI injection, then check that
     `get_mesh_component_selection` contains it.
4. **Editing (D6, D7).** Add `Set_geometry_attribute_operation`, in-place
   cell editing, position edits through `Move_mesh_vertices_operation`, and
   fill down. Add an MCP tool, `set_mesh_attribute_values`, that makes the
   same operation, so headless tests exercise the edit path. Verify:
   - set a vertex color, check it with `get_mesh_attribute_values` and
     `get_mesh_buffer_data` (the GPU buffer), then undo and redo;
   - set a position and check that normals are recomputed;
   - check with a `capture_screenshot` before and after.
5. **Performance check (R8).** Load or create a mesh with 1,000,000
   vertices and open the window on the Corner tab:
   - use Tracy to confirm that the window's zone time is flat when scrolling
     from the top of the table to the bottom;
   - use Tracy to confirm that no allocations show in steady-state frames;
   - confirm that a hidden window records no zone.

   Record the measured numbers in the window's section of
   `doc/editor/windows.md`.

## 4. Documentation on landing

- `doc/editor/windows.md` gets a "Geometry Spreadsheet" section covering the
  cache levels (D3), the drawing (D5) and the edit path (D7), stated as the
  current design.
- `doc/editor/mesh_component_selection.md` section 3 gets the selection
  message (D8).
- The "Set vertex attribute values" item is removed from
  `doc/plans/mesh_component_selection.md`, and this plan is deleted.
