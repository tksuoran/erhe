# brushes/

Stability: stable

## Purpose

Implements the brush system for placing parametric mesh shapes onto surfaces.

## Key Types

- **`Brush`** -- A reusable mesh template (Item_base subtype). Holds a `Brush_data` with build info, normal style, collision shape, density and volume, and a `Brush_geometry_slot` that holds the geometry or the recipe for it ("Geometry preparation" below). Supports scale-dependent caching (`Scaled` entries) for GPU primitives and collision shapes at different scales. Provides `make_instance()` to create a scene node from the brush. Computes reference frames for face-aligned placement. A brush owns the mutex and the condition variable of its geometry slot, so it neither moves nor copies; `make_shared_payload_copy()` is how a second content library gets its own `Brush` item over the same payload.

- **`Brush_data`** -- Configuration for creating a `Brush`: name, geometry or geometry generator, part geometries ("Parts" below), the preparation queue a request goes to, build info, collision shape generator, density, and `body`: `Brush_body::from_geometry` (the default: instances get a body from the collision shape or generator, else from the geometry's convex hull) or `Brush_body::none` (no hull is made and instances get no physics body; the level kit's water, `doc/editor/level_tools.md` "Kit brushes"). `get_name()` falls back to the name the geometry carries, for a brush created with a finished geometry.

- **`Brush_geometry_slot`** (`brush_geometry_slot.hpp`) -- The geometry of one brush plus the state machine that prepares it exactly once, whichever thread gets there first. Knows nothing of the editor beyond the brush logger, so `editor_brush_tests` exercises it without an `App_context`.

- **`Brush_geometry_queue`** (`brush_geometry_queue.hpp`) -- The preparation queue of the brush palette, owned by `Scene_builder` next to the palette.

- **`Brush_tool`** -- A `Tool` for placing brushes on hovered surfaces. Handles:
  - Preview mesh display (translucent ghost of the brush at the placement position)
  - Snapping to hovered polygon face or grid
  - Scale-to-match (match face size of target)
  - Insert operation (creates an `Item_insert_remove_operation` via `Operation_stack`), on a right press with no modifier key held - a right press with a modifier belongs to the camera (Alt + right drag tumbles)
  - Brush rotation (cycle through face offsets)
  - Brush picking (select brush from hovered mesh)
  - Drag-and-drop brush placement from content library

- **`Reference_frame`** -- Computes a coordinate frame for a specific polygon face, used to align a brush to a surface. Defined by a facet index and corner offset.

- **`Brush_placement`** -- The value group of the placed node that records how a brush was placed (which brush, which face, which corner offset), keyed on `Brush_placement.brush` and read with `read_brush_placement(node)` (`doc/erhe/property_system.md` section 4.11). Session values: no file carries them.

## Geometry preparation

A palette brush carries a recipe, not a mesh, and the mesh is built when
something needs it.

- **G1. Geometry states.** A brush is `unprepared`, `queued`, `preparing`,
  `ready` or `failed` (`Brush_geometry_state`, `Brush::get_geometry_state()`).
  `ready` means the `Geometry` is built, `Geometry::process()` has run with the
  brush's process flags and texcoord usage indices, and the brush's facet
  statistics (`get_max_corner_count()`, `get_corner_count_to_facets()`) are
  filled - the slot runs the owner's prepared callback under its mutex before
  the state becomes `ready`, so a thread that observes `ready` also sees the
  statistics. `failed` means the generator produced no usable geometry (a null
  result, or a mesh with no facets) and every consumer gets a null geometry
  from then on. A brush created with a finished geometry - the floor brush, a
  user-created brush, an imported brush - starts `ready`; one created with only
  a `Brush_data::geometry_generator` starts `unprepared`.

- **G2. Tier 1: consumers that must have the geometry.** `Brush::get_geometry()`
  returns the geometry once it is `ready`: it runs the generator on the calling
  thread when the brush is `unprepared` or `queued`, waits on the slot's
  condition variable when another thread is already `preparing`, and returns
  null when the brush is `failed`. Its callers are the tier: `Brush`'s own
  `get_scaled()`, `make_instance()`, `get_reference_frame()`,
  `get_bounding_box()`, `get_corner_count_to_facets()` and
  `get_max_corner_count()`; `Brush_tool` placement and its drag-and-drop
  preview; `place_brush_in_scene()` and the `place_brush` MCP tools;
  `Scene_builder::make_mesh_nodes()`; the graph brush source node
  (`geometry_graph/nodes/geometry_source_nodes.cpp`); the per-material brush
  forks in the item tree and the inventory window; and the glTF and USD brush
  writers.

- **G3. Tier 2: consumers that benefit from the geometry.**
  `Brush::request_geometry()` asks for preparation and returns at once: an
  `unprepared` brush becomes `queued` and goes to the front of the preparation
  queue, an already `queued` brush moves to the front, and a `preparing`,
  `ready` or `failed` brush is left alone. Its callers are `draw_brush_thumbnail()`
  - the one path the item tree rows, the hotbar slots and the inventory slots
  take - and the `get_scene_brushes` and `request_brush_geometry` MCP tools. A
  thumbnail whose brush is not `ready` draws `erhe::imgui::draw_spinner()` in
  the square the preview would have filled, keeping the row height, and a
  `failed` brush gets nothing so the site draws its own icon. A waiting brush
  costs no thumbnail slot, because `Thumbnails::draw()` is what claims one and
  the not-ready branch never calls it. Nothing polls: the same row asks again on
  the next frame it is visible, which is the draw that is already happening.
  `get_scene_brushes` reports `geometry_state` for every brush and reports
  `vertex_count` / `facet_count` only for a brush that is already `ready`;
  `get_brush_geometry_states` reports the same states and requests nothing, so
  it is how a caller waits for a batch of requests to finish.

- **G4. A null geometry is a legal result.** Every tier 1 consumer of a
  `failed` brush refuses its action - no node, no scaled entry, no exported
  brush - with a log line naming the brush. A Johnson key whose mesh cannot be
  built therefore still has a brush, and the palette keeps its name.

- **G5. The preparation queue.** `Brush_geometry_queue` is constructed with the
  `tf::Executor`. It holds the pending requests in request order under its own
  mutex, front first, each as a `std::weak_ptr<Brush>` plus a copy of the
  brush's name, and keeps at most `executor.num_workers() - 1` preparation
  tasks in flight, each started through `erhe::task::spawn` - one worker is
  left for the rest of the editor's task work. A request removes the entry the
  brush already had and pushes it to the front, so the brushes wanted now
  overtake the ones that scrolled out of view. A task pops the front entry and
  calls `Brush::prepare_geometry_if_queued()`, which runs the generator only
  while the slot is still `queued`, then takes the next entry until the queue
  runs dry. A brush is therefore prepared exactly once whichever side reaches
  it first: the main thread through `get_geometry()`, or a worker through the
  queue.

- **G6. Lifetime.** The queue's state is co-owned by `shared_ptr` from two
  sides, the `Brush_geometry_queue` and every task in flight, and a brush
  reaches it through the `std::weak_ptr` its `Brush_data` carries. Nothing
  needs cancelling: the queue destructor calls `stop()`, which refuses further
  requests and drops the pending list, the last task to finish releases the
  state, a task whose brush is gone returns, a brush left `queued` is prepared
  by its next tier 1 consumer, and a brush that outlives the queue prepares on
  the calling thread. `Scene_builder` declares the queue before the palette, so
  the palette is destroyed first.

- **G7. What a worker touches.** A generator owns everything it reads: it
  captures the name, the mesh builder, the process flags, the radii, the detail
  read at creation time and the `std::shared_ptr<const Json_library>` by value
  or by shared ownership, so it holds nothing `make_brushes()` outlives. It
  runs with the brush's mutex released, and the slot mutex is the only lock of
  the brush held around it; the content-library mutex is never held while a
  geometry is prepared. A preparation task reads no state of the brush beyond
  its geometry slot - the name it logs a failure with is the copy the
  requesting thread made.

- **G8. Adding a palette brush.** Give its `Brush_data` a
  `geometry_generator` - the whole recipe, building the mesh and calling
  `process()` with that brush's flags - and no `geometry`, and create it
  through `Scene_builder::make_brush()`, which hands it the queue. Everything
  else follows: the brush is named and placed in its folder at construction and
  its geometry is built when a consumer of either tier asks for it.

- **G9. Adding a thumbnail site.** Call `draw_brush_thumbnail()` with a
  `Brush_thumbnail_placement` - a square side, and a top-left corner when the
  caller places the square itself rather than at the ImGui cursor - and act on
  the `Brush_thumbnail_result` it returns: draw the site's own icon for `icon`,
  and nothing further for `thumbnail` or `spinner`.

## Parts

A brush places one mesh primitive per part, each with its own material. A
brush of one part - every palette brush - has its geometry's primitive as that
part. `Brush_data::part_geometries` gives a brush several parts (a level kit
shape with several material slots, `doc/editor/level_tools.md` "Materials"):
`late_initialize()` makes one renderable and raytraced `Primitive` per part
geometry, while the brush's own geometry, the union of the parts, keeps giving
its bounds, convex hull, collision shape, facet statistics and reference
frames. `Brush::Scaled::part_primitives` holds the parts of a scale (scaled
copies at a scale other than 1), `get_part_count()` their count.
`Instance_create_info::part_materials` gives part i its material (a part
without an entry takes `material`), and `make_brush_instance_insert()` takes
one material per part or one for every part.

## Kit mode

Choosing a shape in the Level Kit window hands `Brush_tool` a level kit brush
(`set_kit_brush()`) and makes it the active tool; the tool is then in kit mode
(`doc/editor/level_tools.md` "Level Kit window"). Kit mode keeps the hover,
preview, grid snap and insert machinery and changes two places: the placement
transform, which comes from `Level_kits::make_placement_pose()` for the preview
and the insert alike (`update_kit_preview_transform()`), and the insert, which
is one `Level_kits::place()` at the preview's transform (`try_insert_kit_piece()`).
`set_kit_brush()` also takes the palette material of each slot; the preview
shows each part with a preview material of that palette color, and the insert
places the piece with those slot materials.
The rotate commands turn the next piece about its up axis by the rotation step
(about the surface normal with Orient To Surface).
Choosing a palette brush - `set_active_brush()` (hotbar, `set_tool_asset`), a
drag-and-drop preview or selecting a brush in a content library - or another
tool leaves kit mode (`leave_kit_mode()`). `level_get_placement_preview` reads
the kit mode state over MCP.

## Copies

A brush instance's collision shape is runtime state of its scene's
`Node_physics_system`, not a value of the node, so a `clone()` of the node
carries its `Brush_placement` and `Node_physics` values but no shape; on
entering a scene its body would be built from the convex hull of its mesh
(a stand-in that differs from a brush's triangle mesh shape).
`set_copied_brush_collision_shapes(scene_root, subtree)` gives each brush
instance with a body in a detached copy the shape of its brush scale, the
`Brush::Scaled` entry whose primitive or first part primitive the mesh shares
(`Brush::find_scaled()`), through the destination scene's system, as
`Brush::make_instance()` does; the system scales it to the copy's world scale
once the copy is in the scene (`doc/editor/physics.md` "Collision shapes follow
the node's scale"). Clipboard paste and the level tools' group
duplicate (`doc/editor/level_tools.md` "Groups") call it before their insert.

## Public API / Integration Points

- `Brush::make_instance()` -- create a scene node from this brush
- `Brush::get_reference_frame()` -- get placement coordinate frame for a face
- `Brush::get_geometry()` -- tier 1 geometry access (G2)
- `Brush::request_geometry()` / `Brush::get_geometry_state()` -- tier 2 (G3)
- `Brush_tool::try_insert()` -- place the current brush
- `Brush_tool::preview_drag_and_drop()` -- handle drag-and-drop from content library
- `draw_brush_thumbnail()` -- the one brush thumbnail path (G9)

## Verification

- `editor_brush_tests` covers the slot state machine without an editor: the
  generator runs once under concurrent `get_geometry()` calls, a second thread
  blocks on a latch-held preparation and gets the same geometry, a null or
  zero-facet result is `failed` for every caller, a request queues once, and a
  slot built from a finished geometry starts `ready`.
- `py -3 scripts/brush_geometry_queue_soak.py --runs 20` is the concurrency
  check: each run requests the whole palette (tier 2) and immediately places
  ten brushes (tier 1) while the queue is busy, then checks the editor's exit
  code and its log. It only validates the meshes it builds when the editor
  under test was built with `ERHE_DEBUG_VALIDATE_GEOMETRY` set to 1 - a
  compile-time define at the top of
  `src/erhe/geometry/erhe_geometry/geometry.cpp`, 0 by default - which is what
  makes a corrupt mesh a `MESH CORRUPT` log line.

## Dependencies

- erhe::geometry, erhe::primitive, erhe::physics, erhe::scene, erhe::task
- editor: App_context, Operation_stack, Tools, Scene_root, Icon_set

## Future work

- [Brushes](../plans/brushes.md)
