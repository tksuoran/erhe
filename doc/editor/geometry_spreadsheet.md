# Geometry Spreadsheet

Stability: experimental

The Geometry Spreadsheet window shows the per-element data of one mesh
primitive's `erhe::geometry::Geometry` as a table: one tab per element domain
(vertex, corner, facet, edge), one row per element, one column per attribute
component, editable in place with undo. Remaining work is
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
- **Row order.** Unused with no sort and no row filter (row `i` is element
  `i`), so a large mesh costs no row memory until it is sorted or filtered.
  Otherwise it holds the element indices in display order: the row filter's
  elements (section 5) or every element, sorted with `std::sort` over live
  values when a sort column is set; elements without a value sort last in
  both directions and ties keep element order. It is rebuilt with the layout
  and when the sort or the row filter changes.
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

## 5. Row selection

Rows of the Vertex, Facet and Edge tabs are the mesh component selection
(`doc/editor/mesh_component_selection.md` section 3) of the target's entry;
the Corner tab, corners being no component kind, keeps its own row selection
(a `std::set` in the window).

- The index cell is a row-spanning `Selectable`, highlighted when the element
  is selected. Membership is tested per drawn row (`Component_set::contains`
  on the entry, looked up once per frame; an edge row tests its vertex-pair
  key).
- A click edits the selection with the viewport rules: plain click replaces
  it (`clear_all()`, then add), Ctrl toggles the row, Shift adds every row
  from the last plain / Ctrl clicked row, in display order. A Vertex / Facet /
  Edge click also switches the component mode to vertex / face / edge so the
  viewport shows the selection.
- **Selected Only** limits the rows to the selected elements through the
  model's row filter (`Geometry_spreadsheet_model::set_row_filter()`, sorted
  like any row order). The filter is gathered from the entry when
  `Mesh_component_selection_changed_message` arrives, on a target, geometry
  or tab change, and on a Corner-tab click; edge keys map to edge indices with
  `Geometry::get_edge()`.
- The hovered Vertex / Facet / Edge row is highlighted in the viewport in the
  hover color: the window calls
  `Mesh_component_selection_tool::set_external_hover()` /
  `clear_external_hover()` only when the hovered row changes, and the tool
  draws it in any component mode while the pointer is not over a mesh in the
  viewport. A hidden window clears it.
- Rows record the item label `row <element>` for MCP UI driving. A row spans
  every column, so with a horizontally scrolling table its rectangle center
  can lie past the window edge; click near its left end
  (`scripts/geometry_spreadsheet_verify.py` `click_row()`).

## 6. Editing

- **Editable columns:** `position` and every attribute except those the
  geometry pipeline derives (`facet_id`, `facet_centroid`,
  `vertex_normal_smooth`, `vertex_valency_edge_count`;
  `is_editable_geometry_attribute()`); the other structural columns are
  read-only.
- **In place:** double-clicking a row edits the cell under the pointer
  (`ImGui::TableGetHoveredColumn()`) when its column is editable. The cell
  becomes an `ImGui::InputScalar` holding keyboard focus; the edited row stays
  submitted while scrolled out of view (`ImGuiListClipper::IncludeItemByIndex`).
  The commit is the widget's deactivation with a changed value (Enter or focus
  loss) - the change site, no per-frame comparison of geometry state. Escape
  restores the original value and so commits nothing. A layout rebuild, a sort
  or filter change or a tab switch that moves the edited cell ends the edit.
- **Cell menu** (right-click a row, over a column): **Set Selected Rows To
  This Value** writes the cell's value into that column of every selected row
  (section 5) as one operation; **Remove Value** clears the element's value of
  the attribute (its present flag).
- A component edit keeps the element's other components; an element without
  a value gets zeros, with w = 1 for a four-component attribute.
- **Operations:** every edit goes through `make_geometry_attribute_operation()`
  (`src/editor/operations/set_geometry_attribute_operation.*`), which
  validates the edit, captures the before values from the primitive's current
  Geometry and returns one undoable operation, queued on `Operation_stack`:
  - `position` -> `Move_mesh_vertices_operation`, which refreshes the baked
    normals and the collision shape;
  - any other attribute -> `Set_geometry_attribute_operation`: writes the
    values into the same Geometry object (component selection entries keyed
    on it survive), rebuilds one Primitive shared by every mesh referencing
    the Geometry, publishes `Mesh_geometry_changed_message` and kicks off the
    background re-optimization, following `Paint_colors_operation`;
    `edge_sharpness` feeds no render stream, so its edit only publishes the
    message.
- An edit of shared geometry changes every mesh sharing it. The Shared / Fork
  geometry edit mode of the viewport toolbar applies to gizmo transforms of
  the component selection.

## 7. MCP and verification

`set_mesh_attribute_values` makes the same operation as a cell edit (value,
per-element values, or clear). `get_geometry_spreadsheet` reports the window's target, domain, counts, sort
column, columns and the row ranges the clipper drew in the last frame, and
the cell text of rows exactly as the window formats it (`null` for an absent
value) and whether each row is selected; `first_row` / `row_count` read any
range. It resolves the target at
call time. `scripts/geometry_spreadsheet_verify.py` launches a headless editor
and checks the window against `get_mesh_attribute_values`, the tabs, sorting,
scrolling, a Catmull-Clark swap and its undo, pinning, hiding and target
removal, and the row selection in both directions (component selection to
rows, Selected Only, row clicks with Ctrl / Shift to the component
selection), and editing (an attribute edit reaching the geometry, the
window's columns and the GPU vertex buffer, undo and redo, a position edit
refreshing the facet normals, a double-click / type / Enter edit, Escape,
fill down, edge sharpness, refusal of derived attributes).

## 8. Future work

- [plans/geometry_spreadsheet.md](../plans/geometry_spreadsheet.md) - the
  large-mesh performance check.
