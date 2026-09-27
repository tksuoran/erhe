# Geometry Spreadsheet

Stability: experimental

The Geometry Spreadsheet window shows the per-element data of one mesh
primitive's `erhe::geometry::Geometry` as a table: one tab per element domain
(vertex, corner, facet, edge), one row per element, one column per attribute
component. Remaining work (row selection sync, editing) is
`doc/plans/geometry_spreadsheet.md`.

Code: `src/editor/windows/geometry_spreadsheet_window.{hpp,cpp}` (the window)
and `src/editor/windows/geometry_spreadsheet_model.{hpp,cpp}` (column layout,
row order, cell reading and formatting; no ImGui). Opened from Window >
Geometry Spreadsheet.

## 1. Target

The window shows one (mesh, primitive index), held as a `weak_ptr` so the
window never keeps a removed mesh alive.

- **Follow selection** (default). The target is re-resolved when
  `Selection_message`, `Active_item_changed_message` or
  `Mesh_component_mode_changed_message` arrives; the handler only sets a flag
  and the next drawn frame resolves it, so a hidden window does no work. The
  resolution order: the mesh of the one live, non-empty
  `Mesh_component_selection` entry when a component mode is active and exactly
  one such entry exists; else the mesh of `Selection::get_active_item()`
  (`erhe::scene::get_mesh()`: the item itself or its first mesh child); else
  `Selection::get_last_selected<Mesh>()`. A different mesh resets the
  primitive index to 0.
- **Pin** freezes the current target. Unpinning resolves the target from the
  selection again.
- A mesh with several primitives shows a Primitive index field.
- The target is dropped when `Items_removed_message` names the mesh, and on
  access when the mesh has no item host or its host is not a registered scene
  (`App_scenes::is_host_registered()`, the closed-scene rule of
  `doc/editor/coding_rules.md`).
- The geometry is `Primitive::render_shape->get_geometry_const()`, which never
  blocks; while it is null the window shows "Geometry not available".

## 2. Columns

Each tab starts with the element index, then the structural columns of the
domain, then one column per component of every attribute of that domain that
is present on at least one element.

| Domain | Structural columns |
|---|---|
| Vertex | `position.x/y/z`; `corners` (corner count) when the connectivity is built |
| Corner | `vertex`; `facet` when the connectivity is built |
| Facet | `corners` (corner count), `first_corner` |
| Edge | `v0`, `v1`; `facets` (facet count) when the edge connectivity is built |

- Attribute columns come from `Mesh_attributes::for_each_*_attribute()`.
  The label is the member name without its domain prefix plus a component
  suffix: `r g b a` for colors, `u v` for texcoords, `0..3` for joint slots,
  `valency` / `edges` for `valency_edge_count`, `x y z w` otherwise.
- "Built connectivity" is `Geometry::has_connectivity()` /
  `Geometry::has_edge_connectivity()`. A geometry without edges shows "Edges
  are not built for this geometry" in the Edge tab.
- A cell whose element has no value for the attribute (its
  `Attribute_present` flag is clear) shows a dimmed `-`.
- Numbers are right-aligned. Float cells use fixed notation with the
  Decimals setting (default 4); Decimals 0 is the shortest round-trip form.
  Integer cells print as integers.

## 3. Caches

The model keeps, per domain, a column layout (`std::vector<Spreadsheet_column>`)
and a row order (`std::vector<GEO::index_t>`). Cell values are never cached:
the window reads them from the Geometry for the cells it draws, so an in-place
change that announces nothing (a live paint stroke) still shows at once.

- **Column layout.** Each column holds its kind, value type, component,
  label (a fixed `char[48]`), edit flag and, for attribute columns, a pointer
  to the `Attribute_present<T>` and a typed read function, so drawing a cell
  is a pointer call with no lookup. It is rebuilt when the model is given a
  different Geometry object, when the domain's element count changes, and on
  `Mesh_geometry_changed_message` for the target mesh (attributes added in
  place). The presence scan per attribute stops at the first present element.
- **Row order.** Empty in element order (row `i` is element `i`), so a large
  mesh costs no row memory until it is sorted. Sorting fills it with the
  element indices in display order, `std::sort` over live values; elements
  without a value sort last in both directions and ties keep element order.
  It is rebuilt with the layout and when the sort changes.
- Both caches are cleared with `clear()`, keeping capacity. A hidden (or
  collapsed / inactive-tab) window releases them and their memory, and the
  window's column-width cache, on the first hidden frame.
- The window measures the initial column widths once per layout
  (`Geometry_spreadsheet_model::get_layout_serial()`) and font size.

## 4. Drawing

- One `ImGui::BeginTable` per domain with `ScrollX | ScrollY | RowBg |
  BordersInnerV | BordersOuter | Resizable | Hideable | Sortable |
  SortTristate | SizingFixedFit`; `TableSetupScrollFreeze(1, 1)` keeps the
  index column and the header in view.
- Header clicks sort ascending, descending, then back to element order. Sort
  specs are read only on the frame ImGui marks them dirty.
- Rows go through `ImGuiListClipper`, so only the visible rows are visited.
  In a visited row, `ImGui::TableSetColumnIndex()` returns false for a column
  scrolled out horizontally or hidden, and that cell is skipped before any
  value is read or formatted. A frame's work is proportional to the visible
  cells.
- Cells are formatted with `std::to_chars` into a stack buffer and drawn with
  `ImGui::TextUnformatted(begin, end)`; tab labels with `fmt::format_to_n`
  into a stack buffer. A steady-state frame allocates nothing.
- The domain tabs record their plain domain name (`Vertex`, `Corner`, ...)
  for MCP UI driving (`erhe::imgui::set_item_debug_label`).

## 5. MCP and verification

`get_geometry_spreadsheet` reports the window's target, domain, counts, sort
column, columns and the row ranges the clipper drew in the last frame, and
the cell text of rows exactly as the window formats it (`null` for an absent
value); `first_row` / `row_count` read any range. It resolves the target at
call time. `scripts/geometry_spreadsheet_verify.py` launches a headless editor
and checks the window against `get_mesh_attribute_values`, the tabs, sorting,
scrolling, a Catmull-Clark swap and its undo, pinning, hiding and target
removal.

## 6. Future work

- [plans/geometry_spreadsheet.md](../plans/geometry_spreadsheet.md) - row
  selection sync with the mesh component selection, cell editing with undo,
  and the large-mesh performance check.
