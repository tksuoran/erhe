# Editor Architecture Improvements

Prioritized list of improvements identified from a review of `src/editor/` (~311 files, ~57K lines).

## Suggestioin from Claude under review

### 1. Operation validation before queue (Small effort, Low impact)

Operations are queued without pre-validation. `Compound_operation` has no rollback on partial undo failure. Add validation and transaction semantics.
### 2. Split monolithic files (Medium effort, Medium impact)

| File | Lines | Issue |
|------|-------|-------|
| `debug_visualizations.cpp` | 1,708 | Handles every visualization type |
| `editor.cpp` | 1,651 | Init + main loop + event handling |
| `item_tree_window.cpp` | 1,389 | Selection state machine + drag-drop + UI |
| `fly_camera_tool.cpp` | 1,383 | Camera control + physics + settings |
| `trs_tool.cpp` | 1,380 | Transform interaction + 3 subtools + UI |

### 3. Fix commented-out mutex / thread safety (Small effort, Medium impact)

- `app_scenes.cpp` - mutex locks commented out with TODOs
- `Tools` - mutex on `m_tools` but not on `priority_tool`
- `Transform_tool_shared` - `atomic<bool>` for visualization readiness but other state unprotected
- `Operation_stack` - `m_queued` modified during `update()` iteration

### 4. Add debug-mode asserts on weak_ptr::lock (Small effort, Low-Medium impact)

945 `nullptr` checks suggest `lock()` failures are silently swallowed. Assert on failure for pointers that should always be valid; reserve silent returns for truly optional refs.

### 5. Struct-of-arrays to array-of-structs in post-processing (Small effort, Low impact)

`Post_processing_node` maintains 9+ parallel vectors that must stay synchronized. Replace with a single `struct Level { width, height, downsample, upsample, ... }` vector.

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
