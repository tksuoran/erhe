# Editor Code Review

Review date: 2026-03-21

## Findings

### 1. Null pointer dereference in Content_library_node::combo() drag-and-drop

- **File**: `src/editor/content_library/content_library.hpp`, line 206
- **Severity**: bug
- **Description**: When `drag_node_payload` is not null but `drag_item_payload` is null, the code falls through to `drag_node->item.get()` on line 206. However, `drag_node_` could be null if `drag_node_payload->Data` contains null, and `drag_node` (the result of `dynamic_cast`) could also be null if the payload item is not a `Content_library_node`. In that case, dereferencing `drag_node->item.get()` is a null pointer dereference.
- **Suggested fix**: Add a null check for `drag_node` before accessing `drag_node->item`:
  ```cpp
  const erhe::Item_base* drag_item = (drag_item_payload != nullptr)
      ? *(static_cast<erhe::Item_base**>(drag_item_payload->Data))
      : (drag_node != nullptr) ? drag_node->item.get() : nullptr;
  ```

### 2. Frame duration cap value is 1000x too large

- **File**: `src/editor/time.cpp`, line 40
- **Severity**: bug
- **Description**: The frame duration cap is set to `25'000'000'000` nanoseconds, which is 25 seconds, not 25 milliseconds. A frame duration of 25 seconds would never be capped in practice, making the cap ineffective. The cap should be `25'000'000` nanoseconds (25ms) to prevent physics simulation spiral of death.
- **Suggested fix**: Change `25'000'000'000` to `25'000'000`.

### 3. Frame number never incremented

- **File**: `src/editor/time.cpp` / `src/editor/time.hpp`
- **Severity**: warning
- **Description**: `Time::m_frame_number` is initialized to 0 and has a getter `get_frame_number()`, but `prepare_update()` never increments it. The frame number is included in `Time_context` but always remains 0. The `Id_renderer::Transfer_entry::frame_number` depends on accurate frame tracking.
- **Suggested fix**: Add `++m_frame_number;` at the end of `Time::prepare_update()`.

### 4. Operation_stack::can_undo/can_redo not thread-safe

- **File**: `src/editor/operations/operation_stack.cpp`, lines 136-148
- **Severity**: warning
- **Description**: `can_undo()` and `can_redo()` read `m_executed` and `m_undone` without acquiring `m_mutex`. The TODO comments acknowledge this. Since `queue()` and `update()` hold the lock while modifying these vectors, and `Undo_command`/`Redo_command` call `can_undo()`/`can_redo()` followed by `undo()`/`redo()` without holding a lock across both calls, there is a TOCTOU race: the vector could change between the check and the action.
- **Suggested fix**: Either acquire the mutex in `can_undo()`/`can_redo()`, or combine the check-and-execute into a single locked operation.

### 5. Unsafe static_cast from Item_host to Scene_root

- **File**: `src/editor/items.cpp`, line 53; `src/editor/create/create.cpp`, lines 87, 105, 279; `src/editor/operations/mesh_operation.cpp`, line 246; `src/editor/operations/geometry_operations.cpp`, line 281; and others
- **Severity**: warning
- **Description**: Throughout the codebase, `erhe::Item_host*` is `static_cast` to `Scene_root*` without any type check. If the item host is a tool scene root or any other `Item_host` subclass, this is undefined behavior. While currently all scene items are expected to belong to a `Scene_root`, future changes or bugs could violate this assumption.
- **Suggested fix**: Use `dynamic_cast<Scene_root*>` with a null check, or add a virtual method to `Item_host` that identifies its type.

### 6. reinterpret_cast for physics owner pointer

- **File**: `src/editor/scene/scene_root.cpp`, lines 143 and 161
- **Severity**: warning
- **Description**: Physics body activated/deactivated callbacks use `reinterpret_cast<Node_physics*>(owner)` to recover the `Node_physics` pointer from the rigid body's opaque owner `void*`. This is technically correct as long as the owner is always set to a `Node_physics*`, but there is no validation. If any code sets the owner to a different type, this would silently produce undefined behavior.
- **Suggested fix**: Consider a tagged union or type-checked wrapper around the owner pointer, or at minimum add an assertion that the owner pointer is valid.

### 7. Global mutable state for async tasks

- **File**: `src/editor/items.cpp`, lines 21-30
- **Severity**: warning
- **Description**: `s_item_tasks` is a file-scoped global `std::unordered_map` accessed from both the main thread (via `async_for_nodes_with_mesh`) and potentially from taskflow worker threads (if a task completes and triggers another). The `purge_completed_tasks()` function and task insertion are not protected by any mutex. The `Item_async_task_guard` RAII object clears it in its destructor, which runs during Editor destruction while tasks could still be completing.
- **Suggested fix**: Add a mutex to protect `s_item_tasks`, or use an atomic/concurrent map. Ensure all pending tasks complete before `Item_async_task_guard` destructor runs.

### 8. Content_library_node::combo() missing EndDragDropTarget in early return

- **File**: `src/editor/content_library/content_library.hpp`, lines 199-200
- **Severity**: bug
- **Description**: When both `drag_node_payload` and `drag_item_payload` are null (line 199), the function returns `false` without calling `ImGui::EndDragDropTarget()`. The `BeginDragDropTarget()` on line 196 was called successfully, so the matching `EndDragDropTarget()` must be called to avoid corrupting ImGui's internal state.
- **Suggested fix**: Call `ImGui::EndDragDropTarget()` before the early `return false`.

### 9. App_context pointers initialized to nullptr with no null checks at use sites

- **File**: `src/editor/app_context.hpp`; various use sites
- **Severity**: info
- **Description**: `App_context` is a struct of raw pointers all initialized to nullptr. These are filled in `Editor::fill_app_context()` after construction. Between construction and `fill_app_context()`, and potentially if any initialization fails, these pointers remain null. Many use sites (e.g., `Undo_command::try_call()` at `operation_stack.cpp:29`) dereference `m_context.operation_stack->...` without null checks.
- **Suggested fix**: Since `fill_app_context()` is always called before the main loop, this is unlikely to cause runtime issues in practice. However, consider either: (a) using references instead of pointers for fields that are always set, or (b) adding ERHE_VERIFY assertions at critical dereference sites.

### 10. Missing EndDragDropTarget in item_tree_window drag-and-drop

- **File**: `src/editor/scene/scene_root.cpp`, approximately line 268
- **Severity**: info
- **Description**: The drag-and-drop handling in `Scene_root`'s context menu callback uses `ImGui::AcceptDragDropPayload` with peek mode. If the payload type does not match, the flow should ensure `EndDragDropTarget()` is properly called. This pattern appears in several places and requires careful audit of all drag-and-drop code paths to ensure balanced Begin/End calls.
- **Suggested fix**: Audit all drag-and-drop code paths for balanced Begin/End calls. Consider using RAII wrappers (e.g., `erhe_defer`) around ImGui Begin/End pairs.

### 11. Potential integer overflow in time calculations

- **File**: `src/editor/time.cpp`, line 22-23
- **Severity**: info
- **Description**: `host_system_frame_start_time_ns` and `m_host_system_last_frame_start_time` are `int64_t` values from `steady_clock`. On the first frame, `m_host_system_last_frame_start_time` is 0, which means `host_system_frame_duration_ns` will be equal to the time since epoch in nanoseconds. This results in a very large value being added to `m_simulation_time_accumulator`, causing potentially thousands of fixed timestep iterations on the first frame. While this is partially mitigated by the frame cap (see finding 2, though the cap value is wrong), it could cause a large startup stutter.
- **Suggested fix**: Initialize `m_host_system_last_frame_start_time` to the current time in the `Time` constructor, or skip the first frame's physics simulation.

### 12. Operation_stack::update() clears redo stack on every queued operation

- **File**: `src/editor/operations/operation_stack.cpp`, line 107
- **Severity**: info
- **Description**: When queued operations are executed, `m_undone.clear()` is called unconditionally, discarding all redo history. This is the standard behavior for most undo/redo systems (new operations invalidate the redo branch). However, if multiple operations are queued in the same frame, the redo stack is cleared once after executing all of them, which is correct. This is noted for documentation purposes.
- **Suggested fix**: No change needed -- this is standard undo/redo behavior. Just documenting that the behavior is intentional.

### 13. Typo in comment

- **File**: `src/editor/content_library/content_library.hpp`, line 216
- **Severity**: info
- **Description**: Comment says "continue to childnre" instead of "continue to children".
- **Suggested fix**: Fix the typo.

### 14. Viewport_window member named m_nagivation_gizmo (typo)

- **File**: `src/editor/windows/viewport_window.hpp`, line 79
- **Severity**: info
- **Description**: The member variable is named `m_nagivation_gizmo` instead of `m_navigation_gizmo`.
- **Suggested fix**: Rename to `m_navigation_gizmo`.

### 15. Tracy observer thread-local zone matching is O(n)

- **File**: `src/editor/editor.cpp`, lines 183-191
- **Severity**: info
- **Description**: The `Tracy_observer::on_exit()` method searches `st_entries` linearly to find the matching zone by hash. If many nested taskflow tasks execute, this list grows and the search becomes slower. Additionally, matched entries are marked with `zone.active = 0` but never removed, causing the list to grow monotonically.
- **Suggested fix**: Remove or swap-erase matched entries to keep `st_entries` compact. Alternatively, use a hash map keyed by the task hash.
