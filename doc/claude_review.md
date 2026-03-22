# Editor Code Review

Review date: 2026-03-22 (updated)
Original review: 2026-03-21

## Fixed Issues

The following issues from the original review have been addressed:

- **#2 Frame duration cap** — Needs verification: may still be 25s instead of 25ms
- **#8 Missing EndDragDropTarget** — Fixed: added `EndDragDropTarget()` before early return in `Content_library_node::combo()`
- **#10 Missing EndDragDropTarget in scene_root** — Fixed: removed unnecessary `DragDropWithinTarget` guard, verified all Begin/End pairs are balanced

## Open Findings

### 1. Null pointer dereference in Content_library_node::combo() drag-and-drop

- **File**: `src/editor/content_library/content_library.hpp`, line 206
- **Severity**: bug
- **Description**: When `drag_node_payload` is not null but `drag_item_payload` is null, `drag_node` (result of `dynamic_cast`) could be null. Dereferencing `drag_node->item.get()` would be a null pointer dereference.
- **Suggested fix**: Add null check for `drag_node` before accessing `drag_node->item`.

### 2. Frame duration cap value may be too large

- **File**: `src/editor/time.cpp`, line 40
- **Severity**: bug
- **Description**: The frame duration cap may be `25'000'000'000` nanoseconds (25 seconds) instead of `25'000'000` (25 milliseconds). Needs verification.

### 3. Frame number never incremented

- **File**: `src/editor/time.cpp`
- **Severity**: warning
- **Description**: `Time::m_frame_number` is never incremented in `prepare_update()`. Always remains 0.
- **Suggested fix**: Add `++m_frame_number;` at end of `Time::prepare_update()`.

### 4. Operation_stack::can_undo/can_redo not thread-safe

- **File**: `src/editor/operations/operation_stack.cpp`, lines 136-148
- **Severity**: warning
- **Description**: `can_undo()` and `can_redo()` read vectors without acquiring `m_mutex`. TOCTOU race with `queue()`/`update()`.

### 5. Unsafe static_cast from Item_host to Scene_root

- **File**: Multiple files (`items.cpp`, `create.cpp`, `mesh_operation.cpp`, etc.)
- **Severity**: warning
- **Description**: `erhe::Item_host*` is `static_cast` to `Scene_root*` without type check. Should use `dynamic_cast` or virtual type identification.

### 6. reinterpret_cast for physics owner pointer

- **File**: `src/editor/scene/scene_root.cpp`, lines 143 and 161
- **Severity**: warning
- **Description**: Physics callbacks use `reinterpret_cast<Node_physics*>(owner)` with no validation.

### 7. Global mutable state for async tasks

- **File**: `src/editor/items.cpp`, lines 21-30
- **Severity**: warning
- **Description**: `s_item_tasks` global map has no mutex protection. Task insertion and purging could race.

### 8. App_context pointers null during construction

- **File**: `src/editor/app_context.hpp`
- **Severity**: info
- **Description**: All pointers are nullptr until `fill_app_context()`. Component constructors must not access them (documented in editor notes.md).

### 9. First-frame time spike

- **File**: `src/editor/time.cpp`, line 22-23
- **Severity**: info
- **Description**: On first frame, `m_host_system_last_frame_start_time` is 0, causing a huge delta. Should initialize to current time.

### 10. Typo: "childnre" in content_library.hpp comment

- **File**: `src/editor/content_library/content_library.hpp`, line 216
- **Severity**: info

### 11. Typo: m_nagivation_gizmo

- **File**: `src/editor/windows/viewport_window.hpp`, line 79
- **Severity**: info
- **Description**: Should be `m_navigation_gizmo`.

### 12. Tracy observer O(n) zone matching

- **File**: `src/editor/editor.cpp`, lines 183-191
- **Severity**: info
- **Description**: Linear search in `st_entries`, matched entries never removed.

## Notes on Recent Changes

### Buffer Memory Reclamation (2026-03-22)

The bump-only `Buffer_allocator` in `erhe::graphics` has been replaced with `Free_list_allocator` in `erhe::buffer`. GPU and CPU buffer space is now reclaimed when meshes are destroyed via `Buffer_allocation` RAII handles held in `Buffer_mesh`. Key design points:

- `Buffer_allocation` destructor calls `Free_list_allocator::free()` — member declaration order in containing classes must ensure the allocation is destroyed before the allocator
- `Primitive_raytrace` member order was fixed to prevent use-after-free (confirmed with ASAN)
- `Buffer_mesh` is now move-only (not copyable)
- `Primitive_render_shape`, `Primitive_shape`, `Primitive_raytrace` are also move-only

### Geometry Operation Robustness (2026-03-22)

All geometry operations now:
- Skip degenerate facets (< 3 corners) and vertices (< 3 corners)
- Validate all corner vertices/midpoints **before** calling `create_polygon()` (prevents partially-filled facets)
- `post_processing()` runs `sanitize()` before Geogram's `connect()` (removes duplicate-vertex facets)
- `get_src_edge_new_vertex()` returns `GEO::NO_VERTEX` instead of asserting
- `Mesh_operation::execute/undo` use error reporting instead of asserts on empty entries
- Debug geometry dumps saved to `debug_geometry/` when sanitization fixes problems

### Item Locking and Tagging (2026-03-22)

- `lock_edit` flag (bit 25) prevents deletion and property editing
- `lock_viewport_selection` / `lock_viewport_transform` prevent viewport interaction
- Properties window shows lock toggles and disables editing when `lock_edit` is set
- String tags on items via `add_tag()`/`remove_tag()`/`has_tag()`
- All exposed via MCP server tools

### MCP Server (2026-03-22)

Query tools: `list_scenes`, `get_scene_nodes`, `get_node_details`, `get_scene_cameras`, `get_scene_lights`, `get_scene_materials`, `get_material_details`, `get_scene_brushes`, `get_selection`, `get_undo_redo_stack`, `get_async_status`

Action tools: `select_items`, `place_brush`, `toggle_physics`, `lock_items`, `unlock_items`, `add_tags`, `remove_tags`

All registered editor commands also exposed. Python test suite: 42 unit tests + smoke test with geometry chains.
