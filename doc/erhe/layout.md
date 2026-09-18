# Layout Nodes

Stability: stable

A **layout node** owns a volume and computes the transform of each of its child
nodes so the children are arranged inside that volume - a 3D analogue of CSS
flexbox / grid. Three layout types exist: Stack, Grid and Flow.

### Common to all layouts
- A layout node itself has a volume (an axis-aligned box: min and max corners).
- The layout node is the parent of the child nodes.
- It places each child inside the volume by determining the child's transformation.
- Layouts typically divide their volume into cells and place child nodes in cells.

### Flow layout
- Places children in spans along a primary direction.
- Per-child alignment selects pos / neg / stretch for each axis - this determines
  how the child is placed and scaled within the cell available to it.
- Each node placed into a primary span consumes space from the span: trivially
  along the primary direction; along the secondary and tertiary directions the
  span size is the maximum size required by the children in that span.
- When a node does not fit into the latest span, a new primary span is created.
- Primary spans are arranged into a secondary span growing in the secondary
  direction; when the next primary span does not fit, a new secondary span starts.
- Secondary spans are arranged into tertiary spans growing in the tertiary direction.

### Grid layout
- The volume is divided into rows / columns / slices.
- Each row / column / slice has a size; this defines the size of all cells.
- Parameters: number of columns / rows / slices, and the extent of each.
- Each node selects: cell, cell span, alignment (pos/neg/stretch per axis),
  margin (per axis).
- Auto placement: a child without an explicit cell (no `Layout_item`, or
  `grid_cell_auto` true) flows into successive cells in document order -
  primary axis fastest, wrapping into secondary, then tertiary; spans are
  honored. Explicitly placed children do not move the auto cursor and there is
  no occupancy tracking, so mixing explicit and auto children can overlap.

### Stack layout
- Children are distributed along a single axis: the (primary) flow direction,
  an axis plus pos/neg.

## Design

A layout node is an ordinary `erhe::scene::Node` carrying a **`Layout`** node
attachment, modeled on `erhe::scene::Light`: a single class with a
`Layout_type { stack, grid, flow }` enum and per-type fields. Per-child overrides
live in a second attachment, **`Layout_item`** (alignment per axis, margins, grid
cell + span); a child without one uses default values.

- The layout owns each child's **translation** and (for `stretch` alignment)
  **scale**; child **rotation is forced to identity** (keeps the result a clean
  TRS, avoids shear from non-uniform stretch combined with rotation).
- A child's footprint is measured in the child's own local space from its mesh
  primitives plus descendants (`compute_content_local_aabb`). A child that is
  itself a `Layout` contributes its declared `volume` instead of recursing into
  geometry - this matches intent and breaks the recursion cycle.
- The shared placement core `compute_child_placement(cell, content, alignment,
  margin_min, margin_max)` maps a child's content box into a target cell per axis
  (negative -> content min at cell min; positive -> content max at cell max;
  stretch -> scale to fill, guarded against zero-extent axes). Off-origin and
  empty/degenerate content are handled without NaN.
- Re-flow runs once per frame from the editor
  (`App_scenes::update_layout_nodes` -> `Scene::update_layouts`), before the
  world-transform passes. Each `Scene` keeps its registered `Layout`
  attachments through the `Scene_host` register / unregister hooks, so the pass
  touches the layout nodes alone and never scans the hierarchy. The list is
  sorted by node depth every pass, so a parent layout runs before a nested
  child layout; depth changes with reparenting, which is why the (small) list
  is sorted rather than cached. Every layout is recomputed each pass.

### Algorithms
- **Stack**: children packed along the signed primary axis; each child's cell is
  its own primary extent on the primary axis and the full volume extent on the two
  cross axes. Inter-child spacing on the primary axis is `gap[primary]`.
- **Grid**: `build_track_edges` builds per-axis track boundaries - honoring
  per-track extents (`grid_track_extent`, absolute sizes from the volume minimum,
  clamped >= 0) or dividing the volume evenly into `grid_track_count` tracks.
  Each child clamps its `grid_cell` / `grid_span` into range and gets the cell
  spanning those tracks.
- **Flow** (two passes): pass 1 groups children into lines along primary (wrap on
  the volume primary extent), then groups lines into sheets along secondary (wrap
  on the secondary extent); a line's cross size is the max child size on
  secondary/tertiary, a sheet's tertiary size is the max line size. Pass 2 walks
  sheets -> lines -> members with signed cursors and gives each child a cell of
  (its primary extent) x (line secondary cross-size) x (sheet tertiary cross-size).

## Key files / symbols

- `src/erhe/scene/erhe_scene/layout.hpp` / `layout.cpp` - `Layout` class,
  `Layout_type`, `Axis_direction` (+ `axis_index`/`axis_sign`/`axis_vector`),
  `compute_content_local_aabb`, and the anonymous-namespace helpers
  `is_empty`, `node_own_local_aabb`, `compute_child_placement`,
  `measure_child_content`, `build_track_edges`, `resolve_item`, `advance`,
  `Flow_line`, `Flow_sheet`. Algorithms: `layout_stack`, `layout_grid`, `layout_flow`.
- `src/erhe/scene/erhe_scene/layout_item.hpp` / `layout_item.cpp` - `Layout_item`,
  `Layout_alignment`.
- `doc/erhe/scene.md` - the library document.
- `src/erhe/item/erhe_item/item.hpp` - item-type registration (`index_layout` = 36,
  `index_layout_item` = 37, `count` = 38, bits, and `c_bit_labels`).
- `src/editor/scene/scene_commands.hpp` / `.cpp` - `Create_new_layout_command` +
  `create_new_layout()`; menu "Create.Layout" and key F6.
- `src/editor/app_scenes.hpp` / `.cpp` - `update_layout_nodes()` driver.
- `src/editor/editor.cpp` - the per-frame hook, before `update_transforms` in
  the "Update scene transforms" block.
- `doc/gltf_extensions/ERHE_layout.md` - the `ERHE_layout` extension that
  persists a `Layout` / `Layout_item` attachment's fields.

## Verification

- Create a layout node: Commands > Create > Layout in the main menu bar, or
  right-click a node in the Hierarchy window and pick Create > Layout. The
  Hierarchy context menu's Create list is authored in `Scene_root`, separately
  from the `bind_command_to_menu` registry that feeds the Commands menu.
- Parent a few meshes under it; with the default Stack / +X they line up
  along X.
- Select the layout node in Properties and change Type, Volume Min / Max, the
  primary / secondary / tertiary axes and Gap; for Grid set the grid tracks and
  their optional custom sizes. Select a child to set Align X / Y / Z, Margin
  and, for Grid, Grid Cell / Grid Span.
- Nest a layout under another layout and confirm the inner one arranges its own
  children - the depth-sorted pass check.
- A layout's volume is drawn by `Debug_visualizations` (`create_new_layout`
  sets `show_debug_visualizations`), which is how a misconfigured volume shows
  up at a glance.

## Known behaviors

- A layout owns its children's transforms, so dragging a layout-managed child
  with the transform tool snaps back on the next pass.
- Child rotation is overridden to identity.
- The primary, secondary and tertiary settings are meant to select three
  distinct axes. A duplicate-axis configuration is made safe - the cell is
  seeded from the full volume - but it is not meaningful.

## Future work

- [plans/editor.md](../plans/editor.md) - the layout re-flow dirty scheme, the
  Dock layout type, the per-track extent UI and the missing type icons.
