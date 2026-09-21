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
- Auto placement: a child without an explicit cell (`grid_cell_auto` true)
  flows into successive cells in document order -
  primary axis fastest, wrapping into secondary, then tertiary; spans are
  honored. Explicitly placed children do not move the auto cursor and there is
  no occupancy tracking, so mixing explicit and auto children can overlap.

### Stack layout
- Children are distributed along a single axis: the (primary) flow direction,
  an axis plus pos/neg.

## Design

A layout node is an ordinary `erhe::scene::Node` that carries the **`Layout`
value group** - attached properties of the node itself, keyed on
`Layout.type` (`none` / `stack` / `grid` / `flow`), with `none` the default
(`doc/erhe/property_system.md` section 4.13). A node becomes a layout node by
being given a `Layout.type` other than `none` - Add Property in the Properties
window, or MCP `set_item_property` - and stops being one when the value goes
back to `none`. Per-child overrides are the `Layout.*` per-child hints
(alignment per axis, margins, grid cell + span) set on the child node itself
(section 4.14); a child without them uses default values.

`read_layout(node)` returns the effective container values as a plain
`Layout_data` record, which is what every consumer reads.

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
  (`App_scenes::update_layout_nodes` -> `Scene::update_layouts` ->
  `Layout_system::update`), before the world-transform passes. Each `Scene`
  owns one `erhe::scene::Layout_system`, the node system of the `Layout` value
  group (`doc/erhe/scene.md` "Node systems"): it keeps one `Layout_data` record
  per layout node, refreshed whenever a value of the group changes, so the pass
  touches the layout nodes alone, never scans the hierarchy and reads no
  property store. The records are sorted by node depth every pass, so a parent
  layout runs before a nested child layout; depth changes with reparenting,
  which is why the (small) list is sorted rather than cached. Every container
  the pass uses is a member cleared at the start of its use, so a steady-state
  pass allocates nothing. Every layout is recomputed each pass.

### Algorithms
- **Stack**: children packed along the signed primary axis; each child's cell is
  its own primary extent on the primary axis and the full volume extent on the two
  cross axes. Inter-child spacing on the primary axis is `gap[primary]`.
- **Grid**: `build_track_edges` builds per-axis track boundaries - honoring
  per-track extents (`grid_track_extent_{x,y,z}`, absolute sizes from the volume
  minimum, clamped >= 0, one per track by the coerce callback) or dividing the
  volume evenly into `grid_track_count` tracks when the list is empty.
  Each child clamps its `grid_cell` / `grid_span` into range and gets the cell
  spanning those tracks.
- **Flow** (two passes): pass 1 groups children into lines along primary (wrap on
  the volume primary extent), then groups lines into sheets along secondary (wrap
  on the secondary extent); a line's cross size is the max child size on
  secondary/tertiary, a sheet's tertiary size is the max line size. Pass 2 walks
  sheets -> lines -> members with signed cursors and gives each child a cell of
  (its primary extent) x (line secondary cross-size) x (sheet tertiary cross-size).

## Key files / symbols

- `src/erhe/scene/erhe_scene/layout.hpp` / `layout.cpp` - the `Layout`
  registration holder, `Layout_data`, `Layout_type`, `Layout_alignment`,
  `Axis_direction` (+ `axis_index`/`axis_sign`/`axis_vector`),
  `carries_layout`, `read_layout`, `get_layout_volume`,
  `measure_child_content` and `compute_content_local_aabb`.
- `src/erhe/scene/erhe_scene/layout_system.hpp` / `layout_system.cpp` -
  `Layout_system` (the per-scene node system and the solve), with the
  anonymous-namespace helpers `is_empty`, `measured_content`,
  `compute_child_placement`, `build_track_edges`, `resolve_item`, `advance`.
  Algorithms: `layout_stack`, `layout_grid`, `layout_flow`.
- `doc/erhe/scene.md` - the library document; `doc/erhe/property_system.md`
  sections 4.13 and 4.14 - the value group and the per-child hints.
- `src/editor/app_scenes.hpp` / `.cpp` - `update_layout_nodes()` driver.
- `src/editor/editor.cpp` - the per-frame hook, before `update_transforms` in
  the "Update scene transforms" block.
- The values are persisted by the node's `ERHE_node` `properties` map
  (`doc/gltf_extensions/ERHE_node.md`) in glTF and by `erhe:Layout:<name>`
  custom attributes in USD; there is no extension of their own.

## Verification

- Make a layout node: select any node, use Add Property in the Properties
  window to add `Layout.type`, and set it to Stack, Grid or Flow (MCP:
  `set_item_property` with property `Layout.type`). The rest of the Layout
  rows appear as soon as the node carries the group.
- Parent a few meshes under it; with Stack / +X they line up along X.
- Select the layout node in Properties and change Type, Volume Min / Max, the
  primary / secondary / tertiary axes and Gap; for Grid set the grid tracks and
  their optional custom sizes. Select a child to set Align X / Y / Z, Margin
  and, for Grid, Grid Cell / Grid Span.
- Nest a layout under another layout and confirm the inner one arranges its own
  children - the depth-sorted pass check.
- A layout's volume is drawn by `Debug_visualizations` from the scene's
  layout-system records, under the Layouts visualization mode (off / all /
  selected / hovered), which is how a misconfigured volume shows up at a
  glance.

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
