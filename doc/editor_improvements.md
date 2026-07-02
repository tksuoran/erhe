# Editor Architecture Improvements

Prioritized list of improvements identified from a review of `src/editor/` (~311 files, ~57K lines).

## Suggestioin from Claude under review

### 1. Operation validation before queue (Small effort, Low impact)

Operations are queued without pre-validation. `Compound_operation` has no rollback on partial undo failure. Add validation and transaction semantics.
### 2. Split monolithic files (Medium effort, Medium impact)

| File | Lines | Issue |
|------|-------|-------|
| `editor.cpp` | 2,430 | Init + main loop + event handling |
| `tools/debug_visualizations.cpp` | 1,760 | Handles every visualization type |
| `windows/item_tree_window.cpp` | 1,419 | Selection state machine + drag-drop + UI |
| `transform/trs_tool.cpp` | 1,380 | Transform interaction + 3 subtools + UI |
| `tools/fly_camera_tool.cpp` | 1,371 | Camera control + physics + settings |

### 3. Fix commented-out mutex / thread safety (Small effort, Medium impact)

- `app_scenes.cpp` - mutex locks commented out with TODOs
- `Tools` - mutex on `m_tools` but not on `priority_tool`
- `Transform_tool_shared` - `atomic<bool>` for visualization readiness but other state unprotected
- `Operation_stack` - `m_queued` modified during `update()` iteration

### 4. Add debug-mode asserts on weak_ptr::lock (Small effort, Low-Medium impact)

945 `nullptr` checks suggest `lock()` failures are silently swallowed. Assert on failure for pointers that should always be valid; reserve silent returns for truly optional refs.

### 5. Struct-of-arrays to array-of-structs in post-processing (Small effort, Low impact)

`Post_processing_node` maintains 9+ parallel vectors that must stay synchronized. Replace with a single `struct Level { width, height, downsample, upsample, ... }` vector.

### 6. Async geometry graph evaluation (Medium-Large effort, Medium impact)

Geometry graph evaluation runs synchronously on the main thread; a heavy chain (subdivide x6 -> ~27 s Debug) freezes the UI and outlives the in-editor MCP server's per-request wait (the request still executes; clients must wait for the server to drain - see `scripts/geometry_nodes_smoke_test.py` mutate()). Move node evaluation to a worker (the `Mesh_operation` async pattern exists) or at least have the MCP server respond "accepted, evaluating" with a completion query, so long evaluations neither freeze the UI nor look like hangs. Found during the 2026-07-02 geometry nodes smoke sweep.

### 7. Breadcrumbs in the geometry -> primitive pipeline tail (Small effort, Medium debugging impact)

The `editor.watchdog` breadcrumb ring localized the 2026-07-02 CC hang only approximately: `Geometry::process()` sets breadcrumbs per step, but `compute_mesh_tangents`, `Primitive_builder`, `make_raytrace` (BVH build) and `copy_with_transform` set none, so a stall after the last breadcrumb is misattributed to it. Add `erhe::log::set_breadcrumb()` to those phases (and per Catmull-Clark phase) so the next hang self-localizes.

### 8. Color geometry graph pins by payload type (Small effort, Low impact)

`Geometry_graph_node::show_pins()` draws every pin identically; with 10 payload pin keys (geometry, floats, points, instances, ...) type-safe connection rules are invisible until a drag is rejected. Give each `Geometry_pin_key` a color.

### 9. Batch element creation in remaining Geometry_operations (Medium effort, Medium impact)

Geogram's `MeshSubElementsStore::create_sub_elements()` computes capacity growth from store SIZE, not capacity (bug also in upstream geogram main 2026-07), so per-element `create_vertices(1)` / `create_polygon()` is O(n) each and any operation using the one-at-a-time `make_new_dst_*` helpers is O(n^2) on large meshes. Catmull-Clark was converted to batch creation (8e52a1b9, `map_dst_*` helpers); the conway operations (ambo, kis, gyro, meta, truncate, chamfer, dual, subdivide) and others still create per element. Convert them via the same pattern; alternatively fix the growth policy in a geogram fork (needs a user-provided fork repo per CLAUDE.md).

## Past work

## Replace multi-inheritance with composition for Tool+Window (Medium effort, Medium impact)

**DONE.** `Fly_camera_tool`, `Paint_tool`, `Hotbar`, `Hover_tool`, `Transform_tool`, `Grid_tool`, and `Create` inherited from both `Imgui_window` and `Tool`. Refactored to composition: each tool owns a `Tool_window` member that delegates `imgui()` (and optionally `flags()`/`on_begin()`/`on_end()`) back to the tool via callbacks. See `src/editor/tools/tool_window.hpp`.

## Future work to consider

## Narrow `App_context` into focused interfaces (Large effort, High impact)

### Idea from Claude

`app_context.hpp` contains 57 raw pointers and is included by 76 files. Every subsystem receives it, creating implicit coupling. Replace with focused interface bundles (`Scene_access`, `Render_access`, `Selection_access`) and inject only what each subsystem needs.

### Why this is rejected for now

App_context is centralized way - a directory - for subsystems to access other subsystems. It makes changes simpler.

## Extract `Editor` initialization into builder (Medium effort, High impact)

### Idea from Claude

`editor.cpp` is 1,651 lines with 114 includes. The constructor builds ~30 subsystems and owns 100+ members. Extract subsystem creation into a builder/factory, grouping related subsystems into composite objects.

### Why this is rejected for now

Editor constructs subsystems in controlled manner. It handles all subsystem dependencies. Distributing the subsystem
constructions would make managing dependencies harder.
