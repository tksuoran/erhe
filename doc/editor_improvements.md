# Editor Architecture Improvements

Prioritized list of improvements identified from a review of `src/editor/` (~311 files, ~57K lines).

## Suggestioin from Claude under review

### 1. Operation validation before queue (Small effort, Low impact)

Operations are queued without pre-validation. `Compound_operation` has no rollback on partial undo failure. Add validation and transaction semantics.
### 2. Split monolithic files (Medium effort, Medium impact)

| File | Lines (2026-07) | Issue |
|------|-------|-------|
| `editor.cpp` | 3,108 | Init + main loop + event handling |
| `tools/debug_visualizations.cpp` | 2,274 | Handles every visualization type |
| `windows/item_tree_window.cpp` | 1,711 | Selection state machine + drag-drop + UI |
| `transform/trs_tool.cpp` | 1,380 | Transform interaction + 3 subtools + UI |
| `tools/fly_camera_tool.cpp` | 1,409 | Camera control + physics + settings |

### 3. Fix commented-out mutex / thread safety (Small effort, Medium impact)

- `app_scenes.cpp` - mutex locks commented out with TODOs
- `Tools` - mutex on `m_tools` but not on `priority_tool`
- `Transform_tool_shared` - `atomic<bool>` for visualization readiness but other state unprotected
- `Operation_stack` - `m_queued` modified during `update()` iteration

### 4. Add debug-mode asserts on weak_ptr::lock (Small effort, Low-Medium impact)

945 `nullptr` checks suggest `lock()` failures are silently swallowed. Assert on failure for pointers that should always be valid; reserve silent returns for truly optional refs.

### 5. Struct-of-arrays to array-of-structs in post-processing (Small effort, Low impact)

`Post_processing_node` maintains 9+ parallel vectors that must stay synchronized. Replace with a single `struct Level { width, height, downsample, upsample, ... }` vector.

### 6. Priority system: stale sorting and update-binding order (Small effort, Low-Medium impact)

Migrated from the retired tool-improvements plan (items 5c/5d, deferred because they
require behavioral testing):

- When a tool becomes the priority tool, `Tools::set_priority_tool()` calls
  `set_priority_boost(100)` on the tool, then `commands->sort_bindings()`. But
  `set_priority_boost()` also calls `handle_priority_update()` on the tool *before*
  the sort happens; if `handle_priority_update()` interacts with commands that
  depend on correct ordering, it sees stale state.
- Update bindings (per-frame tick commands) are processed in registration order,
  not priority order. If two tools both register update commands and one should
  take precedence, there is no mechanism for that.

### 7. Extract `Physics_selection_freezer` from `Scene_root` (Small effort, Low impact)

Migrated from the retired Scene_root cleanup plan (deferred step 4): move
`m_selection_subscription`, `m_physics_disabled_nodes`, the constructor closure that
disables physics on selected nodes, and `Scene_root::imgui()`'s body into a new
`src/editor/scene/physics_selection_freezer.{hpp,cpp}`. The closure uses
`item->get_item_host() != this` to scope itself to one scene root, so the helper
needs a `Scene_host*` filter parameter.

### 8. Extract `Rendertarget_mesh_registry` from `Scene_root` (Small effort, Low impact)

Migrated from the retired Scene_root cleanup plan (deferred step 5): move
`m_rendertarget_meshes`, `m_rendertarget_meshes_mutex`, the `is<Rendertarget_mesh>`
branches in `register_mesh` / `unregister_mesh`, and
`update_pointer_for_rendertarget_meshes` to a new helper class. Kills one of
Scene_root's two mutexes.

## Past work

## Async geometry graph evaluation (Medium-Large effort, Medium impact)

**DONE.** Geometry graph evaluation runs on a `tf::Executor` worker via snapshot isolation: `Geometry_graph_window::update_evaluation()` (once per frame from the editor main loop) clones the live graph into a shadow - factory-built nodes carrying the same parameters (via the `write_parameters()` / `read_parameters()` JSON round-trip), links, cached output payloads and dirty flags, so incremental evaluation is preserved - and the worker evaluates only the shadow. The live graph stays fully interactive (canvas, undo / redo, MCP mutations) during a run; results are copied back on the main thread when the worker finishes. `Geometry_output_node` is two-phase, following the async `Mesh_operation` pattern: `evaluate()` builds the render geometry / primitive / collision shape worker-side, `apply_evaluated_to_scene()` mutates the scene main-thread-side (scene Output nodes inside group assets are handled by pairing the shadow and live subgraphs). MCP mutations now return immediately ("accepted, evaluating"); `get_geometry_graph` waits for completion (the completion barrier) and `get_async_status` reports in-flight runs. Verified: a subdivide-x6 mutation returns in 0.13 s (previously blocked ~20 s Debug) with cheap MCP queries answered throughout the evaluation, and the 120-check smoke sweep passes.

## Batch element creation in remaining Geometry_operations (Medium effort, Medium impact)

**RESOLVED at the root cause instead.** The motivation was Geogram's `MeshSubElementsStore::create_sub_elements()` computing capacity growth from store SIZE, not capacity (https://github.com/BrunoLevy/geogram/issues/371), which made per-element `create_vertices(1)` / `create_polygon()` O(n) each and the one-at-a-time `make_new_dst_*` helpers O(n^2) on large meshes. The geogram pin now points at the `tksuoran/geogram` fork commit `daf9e192` which fixes the growth policy (capacity-based doubling), so per-element creation is amortized O(1) again and the conway operations (ambo, kis, gyro, meta, truncate, chamfer, dual, subdivide) no longer need conversion; batching them would only buy a constant factor (fewer create calls), which the 2026-07-02 Catmull-Clark performance pass classified as diminishing returns. Catmull-Clark itself keeps its batch creation (8e52a1b9, `map_dst_*` helpers) - it predates the fork fix and remains valid. Drop the fork pin once an upstream geogram release includes the fix.

## Breadcrumbs in the geometry -> primitive pipeline tail (Small effort, Medium debugging impact)

**DONE.** The previously uncovered phases now set `erhe::log::set_breadcrumb()`: `Scoped_phase_timer` sets its phase name as a breadcrumb on construction (covering every Catmull-Clark build phase plus interpolate / sanitize / process), `Geometry::process()` covers facet centroids, facet texture coordinates and tangent generation, `Geometry::copy_with_transform()` marks itself, and `Primitive_raytrace` marks the raytrace buffer-mesh build (with mesh counts) and the BVH commit. A stall anywhere in the geometry -> primitive pipeline tail now self-localizes in the watchdog dump.

## Color geometry graph pins by payload type (Small effort, Low impact)

**DONE.** `Geometry_graph_node::show_pins()` fills each pin with a color derived from its `Geometry_pin_key` (`pin_key_color()` in `geometry_graph_node.cpp`), so the type-safe connection rules are visible before a drag is rejected: geometry teal, float grey, int muted green, bool pink, vec3 indigo, vec4 yellow, mat4 steel blue, material orange, points light cyan, instances spring green.

## Replace multi-inheritance with composition for Tool+Window (Medium effort, Medium impact)

**DONE.** `Fly_camera_tool`, `Paint_tool`, `Hotbar`, `Hover_tool`, `Transform_tool`, `Grid_tool`, and `Create` inherited from both `Imgui_window` and `Tool`. Refactored to composition: each tool owns a `Tool_window` member that delegates `imgui()` (and optionally `flags()`/`on_begin()`/`on_end()`) back to the tool via callbacks. See `src/editor/tools/tool_window.hpp`.

## Future work to consider

## Narrow Command dependencies away from App_context (Medium effort, Medium impact)

Migrated from the retired tool-improvements plan: every Command subclass
(`Transform_tool_drag_command`, `Toggle_menu_visibility_command`,
`Hotbar_trackpad_command`, ...) stores `App_context& m_context` and reaches through
it even when it only needs one subsystem (`Transform_tool`, `Clipboard`, ...), making
every command implicitly dependent on all of `App_context` and inflating include
fan-out. If the broader App_context split below is ever undertaken, commands should
receive only the specific subsystem they operate on. Parked for the same reason that
split is parked.

## Narrow `App_context` into focused interfaces (Large effort, High impact)

### Idea from Claude

`app_context.hpp` contains 57 raw pointers and is included by 76 files. Every subsystem receives it, creating implicit coupling. Replace with focused interface bundles (`Scene_access`, `Render_access`, `Selection_access`) and inject only what each subsystem needs.

### Why this is rejected for now

App_context is centralized way - a directory - for subsystems to access other subsystems. It makes changes simpler.

## Extract `Editor` initialization into builder (Medium effort, High impact)

### Idea from Claude

`editor.cpp` is ~3,100 lines with 100+ includes. The constructor builds ~30 subsystems and owns 100+ members. Extract subsystem creation into a builder/factory, grouping related subsystems into composite objects.

### Why this is rejected for now

Editor constructs subsystems in controlled manner. It handles all subsystem dependencies. Distributing the subsystem
constructions would make managing dependencies harder.

## Convert node-editor enum steppers to real combos (Small-Medium effort, Medium impact)

### Idea from Claude

The per-node widgets in the graph editors (`src/editor/*_graph/nodes/`) use the
`imgui_enum_stepper` / `imgui_index_stepper` workaround instead of a real
`ImGui::BeginCombo`, because the old vendored `ax::NodeEditor` faked a scaled-down
coordinate space that opened nested popups in the wrong place. Issue #251 removed
that fake space (node content is authored in real screen space now), so combos
and context menus work inside nodes again - the Conway operation selector is
converted as a pilot (`geometry_graph/nodes/conway_node.cpp`) and a canvas
"Add node" background context menu was added to `geometry_graph_window.cpp`.

The broad conversion of the remaining steppers across the three graph editors
(geometry / texture / shader) belongs to the graph-editor de-duplication work
("Phase C" in `prompt_queue.txt`): those conversions should land in the shared
canvas-safe widget layer Phase C creates, not as a fourth per-window copy.
Gotcha to carry over: a combo preview must bounds-check its index
(`operation_names[m_operation]` overreads when a malformed graph file or the MCP
set_parameter abuse path drives the enum out of range - the stepper clamped
internally, a raw combo does not).
