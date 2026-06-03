# Layout Nodes

Status as of 2026-06-03. Stack, Grid, and Flow are implemented, built, reviewed,
and committed on `main`. Scene serialization and a few refinements are deferred
(see "Deferred / TODO"). This document is the handoff for continuing the work.

## Goal / task

Add "layout nodes" to the scene graph. A layout node owns a volume and computes
the transform of each of its child nodes so the children are arranged inside that
volume (a 3D analogue of CSS flexbox/grid).

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

### Stack layout
- Children are distributed along a single axis: the (primary) flow direction,
  an axis plus pos/neg.

### Deferred later: Dock layout
- Places children inside the volume of the parent Dock layout node.
- Cells in an X-Y-Z grid, up to 3 x 3 x 3.
- Each node selects a cell.

## Design (as implemented)

A layout node is an ordinary `erhe::scene::Node` carrying a new **`Layout`** node
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
- Re-flow runs once per frame from the editor (`App_scenes::update_layout_nodes`),
  shallow-to-deep (parents before nested children), BEFORE the world-transform
  passes. It currently recomputes every layout every frame (a dirty/serial
  optimization is intentionally deferred).

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
- `src/erhe/scene/CMakeLists.txt` - new files registered.
- `src/erhe/scene/notes.md` - library docs updated.
- `src/erhe/item/erhe_item/item.hpp` - item-type registration (`index_layout` = 36,
  `index_layout_item` = 37, `count` = 38, bits, and `c_bit_labels`).
- `src/editor/scene/scene_commands.hpp` / `.cpp` - `Create_new_layout_command` +
  `create_new_layout()`; menu "Create.Layout" and key F6.
- `src/editor/app_scenes.hpp` / `.cpp` - `update_layout_nodes()` driver.
- `src/editor/editor.cpp` - per-frame hook (before `update_transforms`, in the
  "Update scene transforms" block).
- `src/editor/windows/properties.hpp` / `.cpp` - `layout_properties()` and
  `layout_item_properties()` plus the `item_properties` dispatch.

## Commits (on `main`)

- `db9a9480` scene: add layout nodes with Stack layout
- `91d2ff6a` scene: add Grid layout type
- `58d85d8e` scene: add Flow layout type (three-level wrapping)

## Build / verify

- Build (Windows): `scripts\configure_vs2026_opengl.bat` then build the `editor`
  target (e.g. `cmake --build build_vs2026_opengl --target editor --config Debug`).
- Verified so far: all three steps compile; the editor boots and the per-frame
  driver runs with no errors in `logs/log.txt`.
- Visual verification (still to be done in the GUI):
  1. Run the editor; Create > Layout (or F6) creates a layout node.
  2. Parent a few meshes under it; with default Stack/+X they line up along X.
  3. Select the layout node in Properties: change Type (Stack/Grid/Flow), Volume
     Min/Max, primary/secondary/tertiary axes, Gap; for Grid set Grid Tracks and
     optional custom sizes. Select a child to set Align X/Y/Z, Margin, and (Grid)
     Grid Cell / Grid Span.
  4. Nest a layout under another layout and confirm the inner one arranges its own
     children (re-entrancy / shallow-to-deep ordering check).

## Deferred / TODO

- **Scene serialization (next)**: add codegen `Layout_data` / `Layout_item_data`
  structs (see `src/editor/scene/definitions/*.py` and `scene_file.py`), reference
  them from `Scene_file`, and add convert + load wiring in
  `src/editor/scene/scene_serialization.cpp`. All `Layout` / `Layout_item` fields
  are POD/glm/enum/vector and map directly. Until this lands, layout nodes are not
  persisted across save/load.
- **Dirty / serial optimization**: the driver recomputes every layout each frame.
  A correct dirty scheme must re-flow on child add/remove/reorder, child content
  resize, and param edits - but NOT merely when the layout node's own transform
  changes (the world-transform pass would otherwise mark it dirty every frame).
  A per-layout input signature (child ids + content extents + item params) is the
  suggested trigger. The earlier `m_dirty`/`m_updating` scaffolding was removed
  because its trigger was wrong and it was unused; reintroduce a correct one here.
- **Dock layout**: 3x3x3 cell grid, each child selects a cell. Not started.
- **Per-track extent UI at scale**: the custom-size row in `layout_properties`
  becomes unwieldy past a handful of tracks (cosmetic).
- **Known behaviors / limitations**:
  - A layout owns its children's transforms, so dragging a layout-managed child
    with the transform tool snaps back next frame. A future option: a per-child
    "pinned" `Layout_item` flag, or routing tool edits into the `Layout_item`.
  - Child rotation is overridden to identity.
  - primary/secondary/tertiary should select three distinct axes; a duplicate-axis
    misconfiguration is made safe (the cell is seeded from the full volume) but is
    not meaningful.
- **No icon**: `index_layout` / `index_layout_item` have no `icon_set` glyph yet
  (they render without a type icon).
