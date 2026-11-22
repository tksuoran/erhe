# Foliage paint tool: design

Status: in progress

The design that meets `doc/plans/foliage_paint_tool.md` (F-numbers, rulings
G1 - G12; GitHub issue 302). The build order is
`doc/plans/foliage_paint_tool_implementation.md`. The build is in progress
(`doc/plans/foliage_paint_tool_implementation.md` "Built").
It builds on `doc/editor/level_tools.md` "Placing", "Surface placement",
"Surface attachments" and "Scatter layouts". This document's sections are
FD-numbers.

## FD0 Choices for the user's review

- FD0.1 The foliage index is a uniform hash grid of 4 m cells, not a
  kd-tree: items come and go one at a time, and a grid takes an insert, a
  move and a removal in constant time with no rebalancing (FD4).
- FD0.2 The index is brought up to date at its next query, from values the
  scene's change hooks capture at the change. A change costs at most one
  queue entry per item, a frame without a query or a change costs nothing,
  and a query reads no scene node (FD5).
- FD0.3 A palette entry has Variants (1 - 64, default 8 for a shape that
  derives values from a seed) in place of an unbounded Per Piece Seed:
  each distinct seed is a kit brush of its own with its own GPU buffers, so
  10,000 bushes of 10,000 seeds would be 10,000 meshes; with 8 variants
  they are 8 (FD7.2).
- FD0.4 The tool's seed advances after every stroke, so two strokes over
  the same ground differ and one stroke replays from the seed the window
  showed before it (FD8.3).
- FD0.5 A stroke is a live edit of the undo stack: it executes and keeps
  its own operations, records one entry at its end, and Escape undoes
  them and records nothing. While it is open, undo, redo and scene saves
  wait (FD9).
- FD0.6 The same draws give the same candidates on every platform; the
  placement points come from the ray tracer and are equal within one
  build, which is what the tests compare (FD8.4).
- FD0.7 A remove stamp visits its items in the order of their placement
  points (x, then y, then z), which a saved and loaded scene keeps; item
  ids are per session (FD10.2).
- FD0.8 The default radius of each foliage shape (FD3.2).
- FD0.9 Foliage painted on a kit piece is carried on every shape change of
  that piece (G5): one surface search per item per change, also at every
  step of a live parameter drag. Step 8 of the build order measures it at
  1,000 items on one support and reports; the remedy, should it be needed,
  is a carry that moves the items by their proportional place without a
  search.
- FD0.10 An item painted with Align To Surface is recorded by its origin
  and its own rotation (FD6.2). The other reading, a record that turns the
  item onto the surface at every carry, moved aligned items by 4 - 11 mm
  at the first carry, because the shapes' base centers lie up to 0.3 m off
  their origins.

## FD1 Source layout

All new sources are in `src/editor/level/`:

| File | Holds | Scene-free |
|---|---|---|
| `foliage_index.hpp/.cpp` | `Foliage_index`, the point grid (FD4) | yes |
| `foliage_sampler.hpp/.cpp` | attempt count, brush frame, candidates, the spacing rule, the remove decision (FD8, FD10) | yes |
| `foliage_palette.hpp/.cpp` | `Foliage_palette_entry`, its canonical text (FD7) | yes |
| `foliage_system.hpp/.cpp` | `Foliage_system`, the per-scene node system that feeds the index (FD5) | no |
| `foliage_stroke.hpp/.cpp` | `Foliage_stroke`, the stamps of one stroke (FD9) | no |
| `foliage_paint_tool.hpp/.cpp` | `Foliage_paint_tool`, its commands, drawing and UI (FD11) | no |

The scene-free files name only `glm`, the standard library, `erhe::verify`
(a bad handle or a value that is not finite is fatal), `Kit_random`
(`kit_mesh_builder.hpp`), `pick_scatter_item()` (`level_repeat.hpp`) and
the hexadecimal float helpers of `kit_shape.hpp`, and for the palette's
text and check `nlohmann_json`, the kit shape library and
`kit_layout_values.hpp`; they are compiled into `editor_level_tests`
(`level/test/CMakeLists.txt`).

## FD2 Foliage flag and the rays

- FD2.1 `Editor_item_flags::foliage = erhe::Item_flags::application_bit(30)`
  with the row `{ foliage, "Foliage", "foliage" }` in `c_editor_item_flags`
  (`editor_item_bits.cpp`): authored, saved by name, one checkbox row in
  the Properties window like every authored flag. It sits on the item
  `pass_through` sits on, the piece's mesh node.
- FD2.2 `is_surface_mesh()` (`level/level_queries.cpp`) accepts a mesh that
  carries neither `pass_through` nor `foliage`. Every surface ray and
  search of `level_queries` asks it, so F1.3's first list is this one edit.
- FD2.3 `level_queries.hpp` gains the pointer ray, the public form of the
  file's `cast_surface_ray()`:

  ```cpp
  class Surface_ray_hit
  {
  public:
      bool                     found{false};
      float                    distance{0.0f};
      glm::vec3                position{0.0f};
      glm::vec3                normal{0.0f}; // unit, toward the ray's origin
      const erhe::scene::Mesh* mesh{nullptr};
  };
  [[nodiscard]] auto find_ray_surface(
      erhe::raytrace::IScene&                    committed_rt_scene,
      glm::vec3                                  origin,
      glm::vec3                                  direction,
      float                                      max_distance,
      std::span<const erhe::scene::Mesh* const>  excluded
  ) -> Surface_ray_hit;
  ```

  The nearest front face of a surface mesh along the ray; it allocates
  nothing.
- FD2.4 `Level_kits::get_placement_hover(Scene_view&) ->
  std::optional<Hover_placement_hit>` (position, normal, mesh) gives a
  placing tool its surface point: the scene view's content hover when its
  mesh carries no foliage flag, else `find_ray_surface()` along the scene
  view's pointer ray. The second ray is cast only when foliage is under the
  pointer, at the hover change. Its three callers are the foliage paint
  tool, the Brush tool's kit mode (`update_kit_preview_transform()`) and
  the viewport context menu's `Hover_placement` (G8).
- FD2.5 The viewport hover itself is unchanged: `raytrace_node_mask()`
  keeps foliage in the content slot, so hover, selection, the Transform
  tool and material paint see foliage (F1.4).
- FD2.6 `run_level_check()` leaves the meshes carrying the flag out of
  `tested` and counts them in `excluded_count` (F1.6).
- FD2.7 `level_get_pieces` reports `foliage` (the flag) and
  `foliage_radius` per piece, beside `pass_through`.

## FD3 Radius

- FD3.1 `Kit_shape_definition::foliage_radius`, a
  `std::optional<float>` in meters; a shape with a value is a foliage shape
  (F2.1). `Kit_shape_library` gives the foliage shapes in library order for
  the palette's Add menu.
- FD3.2 Proposed defaults, about a third of each shape's default width, so
  neighbours overlap visibly:

  | Shape | Radius (m) |
  |---|---|
  | `nature.grass_tuft` | 0.15 |
  | `nature.succulent` | 0.4 |
  | `nature.leaf_plant` | 0.6 |
  | `nature.palm_shrub` | 0.6 |
  | `nature.bush` | 0.6 |
  | `nature.bamboo` | 0.5 |
  | `nature.palm_clump` | 1.0 |
  | `nature.tree` | 1.5 |
  | `nature.blocky_tree` | 1.5 |
  | `nature.palm` | 2.0 |

  A recreated shape (`<id>.v2`) takes the value of its original.
- FD3.3 `Kit_piece::foliage_radius_property`, a float attached to the node
  (`Property<float>::register_attached("foliage_radius", ...)`, default 0,
  group "Kit Piece", label "Foliage Radius", visible on a piece that
  carries the foliage flag, range 0 - 100 m), added to
  `Kit_piece::all_properties()`. It is outside the mesh key. Paste
  Properties pastes it like any value.
- FD3.4 Every write of the value - the Properties row, `set_item_property`,
  an expression, an animation - reaches the node system's
  `on_values_changed()` through the property's change callback (FD5.1), so
  no writer calls the index. The property's validation holds the range for
  each of them.

## FD4 Foliage index

`Foliage_index` (`foliage_index.hpp`) is a set of points with radii in a
uniform grid.

- FD4.1 Interface:

  ```cpp
  class Foliage_index_entry
  {
  public:
      glm::vec3     position{0.0f}; // scene root space
      float         radius{0.0f};
      std::uint64_t key{0};         // the caller's, here the node's id
  };
  class Foliage_index
  {
  public:
      using Handle = std::uint32_t;
      [[nodiscard]] auto insert        (const Foliage_index_entry& entry) -> Handle;
      void               remove        (Handle handle);
      void               move          (Handle handle, glm::vec3 position);
      void               set_radius    (Handle handle, float radius);
      void               clear         ();
      [[nodiscard]] auto get_entry     (Handle handle) const -> const Foliage_index_entry&;
      [[nodiscard]] auto get_count     () const -> std::size_t;
      [[nodiscard]] auto get_max_radius() const -> float;
      void               find_within   (glm::vec3 center, float distance, std::vector<Handle>& out_handles) const;
      [[nodiscard]] auto count_within  (glm::vec3 center, float distance) const -> std::size_t;
  };
  ```

  For the tests the class also exposes its constants (`cell_size`,
  `cell_coordinate_min`, `cell_coordinate_max`, `no_slot`), the static
  `get_cell_coordinate()` and `get_cell_key()`, and `get_query_path(center,
  distance) -> Foliage_query_path` (`cell_walk` or `dense_scan`), the one
  choice `find_within()` itself asks.
- FD4.2 Storage: a dense array of slots (entry, cell key, the previous and
  next slot of its cell) with a free list, so a handle stays valid until
  its removal; a hash map from the cell key - the three cell coordinates
  `floor(position / 4 m)` packed into 64 bits, 21 bits each - to the first
  slot of the cell. An insert, a move and a removal relink one slot. The
  map allocates only when a cell is used for the first time: an emptied
  cell keeps its map node, so the map holds one node per cell ever used in
  the scene's lifetime and a refill allocates nothing.
- FD4.3 `find_within()` clears and fills the caller's buffer. It visits
  the cells of the query's bounding box when they are fewer than the
  entries and scans the dense array otherwise, so a 50 m brush over a
  sparse scene costs the item count and a spacing query costs the few
  cells around the point. An entry is within the distance when its squared
  distance is at most the distance squared, on both paths: the walked box
  is wider than the query's by more than the test's float rounding, so the path never changes the
  result.
- FD4.4 `get_max_radius()` is the largest radius held. An insert and a
  radius write raise it at once; the removal or shrink of an entry that
  held it marks it stale, and the next read scans the array once.
- FD4.5 Local density (F8.2): `count_within(center, r) / (pi r^2)`.

## FD5 Foliage system

`Foliage_system` (`foliage_system.hpp`) is an `erhe::scene::INode_system`
owned by `Scene_root` beside `Node_physics_system`, added in the
constructor and removed in the destructor. It owns the scene's
`Foliage_index` and a record per registered foliage mesh (keyed by
`Node*`: the node's `weak_ptr`, the captured world position and radius,
the index handle, the transform observer token, a queued mark and a
generation). It is built (`doc/editor/level_tools.md` "Foliage paint").

- FD5.1 The query side never touches a node. Each hook reads the values it
  needs on the thread that made the change, before it takes the system's
  mutex, and stores them in the record under it; nothing is called while
  the mutex is held. A read that re-enters a hook (an expression's value
  evaluated by the read) therefore runs to completion first.
  - `on_node_registered()` and a foliage flag set
    (`Scene_root::on_mesh_flags_changed()` forwards to
    `Foliage_system::on_flags_changed()`): the world translation, the
    radius value and the observer; the record is made or updated.
  - `on_node_unregistered()` and a flag cleared: the record is erased and
    its index entry queued for removal.
  - `on_values_changed(node, property)` with
    `Kit_piece::foliage_radius_property`: the radius.
  - The record's transform observer: the translation of the node it is
    called with. It fires for the item's own moves and, through
    `Scene::update_node_transforms()`, for every move of an ancestor.
  One writer changes a node at a time: the main thread, or a worker
  holding the scene's `item_host_mutex`.
- FD5.2 The pending list holds at most one entry per record (key and
  generation) and one removal per record erased since the last query; an
  entry of an erased record is ignored by its generation. The queries
  (`get_index() -> const Foliage_index&`, `find_nodes_within()`,
  `get_nodes()`) run on the main thread under the system's mutex alone and
  apply the captured values to the index. A position that is not finite
  keeps an item out of the index until a later move, with one log line per
  query. Readers that need ancestors' moves applied call
  `Scene::update_node_transforms()` first, as the placement code does;
  they may hold any scene lock.
- FD5.3 Nothing runs per frame (F8.3): a frame with no query and no change
  touches neither the list nor the index. A scene load fills the index
  through the same hooks at the first query after it (F8.5).
- FD5.4 `Foliage_system::find_nodes_within(center, distance,
  out_nodes)` gives the nodes of the entries, for removal and the MCP
  read.

## FD6 Placement

- FD6.1 `Kit_placement` (`level_kits.hpp`) gains
  `std::optional<float> foliage_radius` and
  `std::shared_ptr<erhe::scene::Node> foliage_support`. With the radius
  set, `prepare_placements()` enables `Editor_item_flags::foliage` on the
  new node and writes `Kit_piece.foliage_radius` before the insert.
- FD6.2 With `foliage_support` set the piece is inserted as its child at
  the given world pose (F7.1); a foliage placement names no other parent.
  When `Level_kits::find_surface_support()` accepts the support's mesh (a
  kit piece or a kit layout's generated mesh), `make_surface_record()`
  makes the record from the piece's bounds, its world transform, the
  support, the direction against the hit normal, `orient` off and the rest
  `anchor`, and the record is written before the insert (G5, F7.4): the
  record is the item's origin with its own rotation, so a carry puts the
  item back exactly where the stroke placed it, aligned or upright. An
  aligned item keeps its rotation in the support's space; it does not turn
  again when a later shape change tilts the surface under it (FD0.10). On
  any other mesh the piece is a plain child (F7.2). The Attach To Surface
  setting is not read.
- FD6.3 A foliage placement takes no module snap, no surface search of its
  own, no connector join and no attachment follow operation: its pose is
  the stroke's.
- FD6.4 `Level_kits::make_foliage_operation(scene_root, placements,
  result)` checks the placements as `place()` does and builds one
  `Compound_operation` of the material inserts and node inserts without
  executing it; the stroke executes it.
- FD6.5 A piece of a shape with a body gets its body and one of a bodiless
  shape gets `pass_through`, as `place()` decides for every piece (G3).

## FD7 Palette

- FD7.1 `Foliage_palette_entry` (`foliage_palette.hpp`): `shape` (id),
  `parameters` (canonical text, empty for the defaults), `materials` (a
  name per slot, empty for the slot default), `weight` (0 - 1000),
  `radius` (meters), `yaw_min`, `yaw_max` (degrees), `variants` (1 - 64)
  and `seed` (the entry's base seed).
- FD7.2 Variants (FD0.3): the piece's seed is
  `derive_layout_element_seed(entry.seed, variant)` with the variant a
  draw modulo `variants`; the seed goes through the shape's
  derive-from-seed as a layout's Per Piece Seed does. An entry of a shape
  without derived values has one variant. An entry therefore needs at
  most `variants` kit brushes.
- FD7.3 Canonical text: a JSON array of objects with sorted keys, floats
  hexadecimal (`make_foliage_palette_text()`,
  `parse_foliage_palette_text()` returning the problem text), the form
  `Kit_layout_item` lists use.
- FD7.4 `check_foliage_palette(library, entries) -> std::string` names the
  first entry that cannot be painted: an unknown shape, values out of
  range, a weight above 1000, yaw limits out of order, more materials than
  slots. A palette whose weights sum to 0 is the "nothing to paint" state
  of F3.3.

## FD8 Sampling

Scene-free, in `foliage_sampler.hpp`.

- FD8.1 Attempt count (F5.2): `get_attempt_count(density, radius)` =
  round(density * pi * radius^2), clamped to 1 - 1024.
- FD8.2 Brush frame: `make_brush_frame(center, normal)` gives two unit
  tangents: the first is the cross product of the normal and the world
  axis the normal is least along, normalized; the second completes the
  frame.
- FD8.3 Streams: a paint stroke draws from `Kit_random(seed, "foliage
  paint")`, a remove stroke from `Kit_random(seed, "foliage remove")`. The
  tool passes its Seed value and then stores `kit_next_seed(seed)` as the
  new value, at the stroke's end; an MCP stroke takes its seed as an
  argument and changes no setting.
- FD8.4 One paint attempt:
  1. one 32-bit draw picks the entry (`pick_scatter_item()` over the
     weights);
  2. up to 8 candidates, each two 24-bit draws giving (x, y) in the square
     -1 - 1. A pair outside the unit circle is a spent candidate. Else the
     ray starts at `center + radius * (x * t0 + y * t1) + h * normal` with
     h = radius * sqrt(1 - x^2 - y^2), runs against the normal for 2 h,
     and its `find_ray_surface()` hit is the candidate point: it is inside
     the sphere by construction. No hit is a spent candidate; so is a
     point the spacing rule refuses (FD8.5);
  3. for the accepted point, one 24-bit draw gives the yaw within the
     entry's range and one 32-bit draw the variant.
  The draws and the arithmetic up to the ray are integer draws and single
  float operations, each its own statement, as the scatter's
  (`doc/cmake_conventions.md`, fp-contract).
- FD8.5 Spacing (F6): `is_spacing_free(index, point, radius, scratch)`
  queries `find_within()` with max(radius, index.get_max_radius()), a
  little widened so the index can only over-include, and accepts when every entry found is at least max(radius, its radius) away,
  compared on squared distances. The scratch buffer is the stroke's and
  keeps its capacity.
- FD8.6 The stroke inserts each stamp's pieces before the next stamp, and
  within a stamp it adds each accepted point to a small list the later
  attempts of the stamp test too, so F6.3 holds inside one stamp without
  touching the scene per attempt.

## FD9 Stroke

`Foliage_stroke` (`foliage_stroke.hpp`) runs one stroke for the tool and
for the MCP tools alike.

- FD9.1 `begin(scene_root, kind, settings, palette, seed)` snapshots its
  inputs (the tool's values at the press), checks the palette and resolves
  each entry's shape, values and materials once; `stamp(center, normal)`;
  `end() -> Foliage_stroke_result` (placed, removed, attempts, refused by
  spacing, without a surface); `cancel()`.
- FD9.2 Undo (F10.1): the stroke is a live edit of `Operation_stack`
  (`doc/editor/operations.md` "Live edits"). `begin()` begins the live
  edit, refused while the stack reports a block reason, and keeps its
  token; each stamp's operation runs through `execute_in_live_edit()`;
  `end()` ends the live edit with `end_live_edit()`, which records the
  operations as one entry (inside an open batch group they join that
  group), or none when no stamp changed anything.
- FD9.3 `cancel()` undoes the stroke's operations in reverse with
  `cancel_live_edit()` and records nothing, leaving the redo history as it
  was (F10.2). While the live edit is open nothing else changes the scene
  through the stack: undo, redo and scene saves wait, another party's
  `execute_now()` is refused, queued operations and worker completions
  wait, and the MCP server runs only its live-edit tools. The close of any
  scene and the editor's shutdown abandon an open stroke, and a stroke
  destroyed open cancels itself.
- FD9.4 A paint stamp: commit the raytrace scene, update the node
  transforms, take the index (FD5.2), run the attempts (FD8), then one
  operation of `make_foliage_operation()` for the accepted points. The pose is the point, the
  yaw about the up axis, and the up axis is world up or, with Align To
  Surface, the hit normal (F5.5); the support is the hit mesh's node.
- FD9.5 Stamp spacing (F5.1, G4): the tool stamps at the press and then
  walks the brush's travel, a stamp at every half brush radius along the
  path, each intermediate center the surface hit of a ray toward that
  point of the travelled chord; the count is a function of the path. A
  frame walks 16 stamps (more once over 256 are pending), the release
  walks the rest at the release pointer, and leaving the viewport breaks
  the travel. An MCP stroke stamps at each center it is given.
- FD9.6 Brushes: `begin()` asks `Level_kits::get_brush()` for nothing; a
  stamp's operation makes the brushes it needs, at most the
  entry's variant count over the life of the palette (FD7.2).
- FD9.7 While a stroke is open the tool ignores hover changes of other
  scenes: a stroke belongs to the scene of its press.

## FD10 Removing

- FD10.1 A remove stamp takes `find_nodes_within(center, radius)`, keeps
  the nodes whose shape is in the palette when Remove Only Palette Shapes
  is on (`read_kit_piece(node).shape`), and sorts them by placement point:
  x, then y, then z, then the node id (FD0.7).
- FD10.2 Per node one 24-bit draw; the node is removed when
  `draw < remove_density * 2^24` computed once per stroke as an integer
  threshold, so 1.0 removes every node and 0 none (F9.2).
- FD10.3 The removals of a stamp are one `Compound_operation` of the item
  removal operation the Delete command uses, each with the node's subtree,
  executed in the stroke's group.

## FD11 Tool

`Foliage_paint_tool` (`foliage_paint_tool.hpp`) is a `Tool` with
`Tool_flags::toolbox`, a base priority and an icon of the icon set, built
in `editor.cpp` after `Brush_tool` and stored as
`App_context::foliage_paint_tool`.

- FD11.1 Commands, each with the tool as its host:

  | Command | Binding | Does |
  |---|---|---|
  | `Foliage_paint.paint` | left drag, called at the press, exactly no modifiers | ready with a brush; press begins a paint stroke and stamps; motion stamps by FD9.5; inactive ends the stroke |
  | `Foliage_paint.remove` | right drag, called at the press, exactly no modifiers | the same with a remove stroke; while the tool is active it takes every plain right press, brush or not, so the context menu's release click does not follow it (F9.1); a right drag with a modifier stays the camera's |
  | `Foliage_paint.radius` | wheel with Shift | multiplies the radius by 1.1 per step up and divides per step down, clamped to its range, and writes the setting; an open stroke follows the new radius |
  | `Foliage_paint.cancel` | Escape, any modifiers | cancels the open stroke; declines when none is open |

  The drags match their modifiers exactly (`erhe::commands`
  `Drag_modifier_match::exact`), so a rebinding to a modifier drag works.
  While a stroke is open either button's plain press is consumed. A
  stroke ends as a release does when the tool is deactivated or its view
  is hidden or destroyed; only Escape cancels.

  All four are editable in the Input Bindings window. A fifth command,
  `Foliage_paint.walk`, is bound to the update: it continues the travel
  walk of FD9.5 while travel is pending and does one comparison otherwise.
- FD11.2 Brush: the tool subscribes to the hover messages the Brush tool
  subscribes to and, at each, reads `Level_kits::get_placement_hover()`
  for the hovered scene view; the result is the brush (center, normal,
  scene root) or none (F4.1).
- FD11.3 `tool_render()` draws the brush with the line renderer
  (`Primitive_renderer::add_line()`): a 64-segment circle of the brush
  radius about the center in the plane across the normal, and the normal
  as a line of a quarter radius. The circle's points are a member array.
  Paint strokes draw it in the paint color, remove strokes in the remove
  color (two colors of `Debug_visualizations_style`). The brush exists only
  while the pointer is inside the viewport's rectangle.
- FD11.4 `tool_properties()` is the tool's UI in the Tool Properties
  window: Brush Radius, Paint Density, Remove Density, Align To Surface,
  Remove Only Palette Shapes, Seed; the palette as one row group per entry
  (thumbnail from the Level Kit window's shape thumbnails, Weight, Radius,
  Yaw range, Variants, the shape's parameter rows, a material per slot,
  Remove) and Add Entry listing the foliage shapes; the reason painting is
  disabled (F3.3); the last stroke's result. Each widget writes the
  setting at its edit.
- FD11.5 Settings (F11.1): `Foliage_paint_config`
  (`src/editor/config/definitions/foliage_paint_config.py`, version 1):
  `brush_radius` 2.0, `paint_density` 0.5, `remove_density` 1.0,
  `align_to_surface` false, `remove_only_palette_shapes` false, `seed` 1,
  `palette` (the canonical text of FD7.3, empty by default). It is the
  field `foliage_paint` of `Editor_settings_config`, whose version rises
  by one.

## FD12 MCP

Handlers in `mcp/mcp_server_level.cpp`, definitions in
`config/editor/mcp_tools.json`. Every tool names its scene and takes every
input as an argument (F11.2).

- `level_foliage_paint`: `scene`, `stamps` (a list of `center` and
  `normal`), `radius`, `density`, `seed`, `align_to_surface`, `palette`
  (entries as FD7.1). Replies the stroke result and the placed pieces (id,
  shape, position, radius, parent, attached).
- `level_foliage_remove`: `scene`, `stamps`, `radius`, `density`, `seed`,
  `only_shapes` (a list of shape ids, empty for all). Replies the removed
  ids.
- `level_get_foliage`: `scene`, optional `center` and `radius`. Replies
  the foliage items of the sphere or of the scene (id, shape, position,
  radius, parent), the count, the density of F8.2 and the index's
  `max_radius`.
- `level_get_foliage_paint_settings`, `level_set_foliage_paint_settings`:
  the tool's values and palette.
- `level_get_foliage_brush`: the tool's brush (scene, center, normal,
  radius) and whether a stroke is open, for the checks that drive the
  mouse.

## FD13 Verification

- FD13.1 `editor_level_tests`:
  - `test_foliage_index.cpp`: against a brute-force search over
    pseudorandom point sets through inserts, moves, radius writes and
    removals; `find_within()` on both of its paths; the cell key at
    negative coordinates and cell borders; `get_max_radius()` after the
    largest entry leaves; handles reused after removal.
  - `test_foliage_sampler.cpp`: the attempt count's rounding and clamps;
    the brush frame orthonormal for the six axes and tilted normals; every
    candidate ray inside the sphere; the spacing rule on pairs at, below
    and above max(r, r_q); a simulated stroke on a plane (a stand-in ray)
    in which every pair of points holds the rule and the same seed gives
    the same points; saturation (a repeated stamp places nothing after
    the area is full); the remove threshold at 0, 0.25 and 1; the remove
    order.
  - `test_foliage_palette.cpp`: text round trip bit for bit, the refusals
    of FD7.4.
- FD13.2 `scripts/level_tools_verify.py` groups (headless,
  `ERHE_DETERMINISTIC_CLOCK=1`, `ERHE_DEFAULT_LAYOUT=1`):
  - `--only-foliage-flag`: a hand-flagged piece is passed by snap to
    surface, the support report and the height map, by the Brush tool's
    placement and by viewport Create; hover and selection still hit it;
    Level Check counts it excluded; the flag survives save and load.
  - `--only-foliage-index`: `level_get_foliage` against the script's own
    search over `level_get_pieces` after placing, flagging and unflagging,
    moving an item and its parent, a radius edit, delete, undo, redo, save
    and load.
  - `--only-foliage-paint`: `level_foliage_paint` on a floor, a slope and
    a wall (points on the surface, inside the brush, parents the hit
    meshes); upright against Align To Surface; the spacing over all pairs;
    a second stroke over the first (more items, rule held); saturation;
    weights 1, 3 and 0; variants bounded; a record on a kit support and a
    plain child on an imported mesh; the support moved and its parameter
    changed; one undo entry per stroke, undo and redo; save and load.
  - `--only-foliage-remove`: density 1 empties the brush and only the
    brush; 0.25 over repeated stamps; only palette shapes; non-foliage
    untouched; undo restores items with their records.
  - `--only-foliage-tool`: through `mouse_drag`, `mouse_wheel` with Shift
    and `key_press`: a left drag paints one entry of the undo stack, a
    longer drag stamps by travel, Shift + wheel changes the radius and the
    plain wheel zooms, a right drag removes and opens no context menu, a
    plain right click with another tool still opens it, Escape mid-stroke
    restores the scene with no entry, the seed advances per stroke.
- FD13.3 Scale (F12.1): a timed case in `test_foliage_sampler.cpp` logs the
  index and spacing time of a 64-attempt stamp over 10,000 entries; the
  2 ms bound is checked in a Release build of the test, reported otherwise.
