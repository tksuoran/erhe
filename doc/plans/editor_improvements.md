# Editor architecture improvements

Status: proposed

Extends `doc/editor/editor.md`. A prioritized list of improvements to `src/editor/`,
each stated as the change to make and what it buys.

## 1. Operation validation before queue (small effort, low impact)

Operations are queued without pre-validation, and `Compound_operation` has no
rollback when one sub-operation's undo fails partway. Add validation at queue
time and transaction semantics to the compound.

## 2. Split the monolithic files (medium effort, medium impact)

| File | Concerns it carries |
|------|---------------------|
| `editor.cpp` | init, main loop, event handling |
| `tools/debug_visualizations.cpp` | every visualization type |
| `windows/item_tree_window.cpp` | selection state machine, drag-drop, UI |
| `transform/transform_tool.cpp` | transform interaction, three subtools, UI |
| `tools/fly_camera_tool.cpp` | camera control, physics, settings |

## 3. Thread-safety gaps (small effort, medium impact)

- `Tools` locks `m_tools` with `m_mutex` but leaves `m_priority_tool`
  unprotected.
- `Transform_tool_shared` uses an `atomic<bool>` for visualization readiness
  while the rest of its state is unprotected.
- `s_item_tasks` (`src/editor/items.cpp`) is documented main-thread-only and
  purged once per frame, but nothing verifies the contract. Give it the
  `verify_main_thread()` treatment `Operation_stack` has, so an off-thread
  caller fails loudly (`doc/editor/operations.md`, "Threading and
  re-entrancy").

## 4. Assert on `weak_ptr::lock` where the pointer must be valid (small effort, low-medium impact)

The sheer number of `nullptr` checks after `lock()` means failures are
swallowed silently. Assert where the reference must be valid, and keep the
silent return for the genuinely optional ones.

## 5. Post-processing: one vector of levels (small effort, low impact)

`Post_processing_node` keeps ten parallel vectors (downsample / upsample
textures, render passes, GPU timers and their labels, level widths and
heights) that must stay index-synchronized. Replace them with one vector of a
`Level` struct.

## 6. Command priority: sorting and update-binding order (small effort, low-medium impact)

- `Tools::set_priority_tool()` calls `set_priority_boost(100)` on the tool and
  then `commands->sort_bindings()`. `set_priority_boost()` itself calls
  `handle_priority_update()` on the tool BEFORE the sort, so a
  `handle_priority_update()` that touches order-dependent commands sees stale
  ordering. Sort first, then notify.
- Update bindings (per-frame tick commands) are processed in registration
  order. Process them in priority order, so two tools that both register
  update commands have a defined precedence.

## 7. Extract `Physics_selection_freezer` from `Scene_root` (small effort, low impact)

Move `m_selection_subscription`, `m_physics_disabled_nodes`, the constructor
closure that disables physics on selected nodes, and the body of
`Scene_root::imgui()` into `src/editor/scene/physics_selection_freezer.{hpp,cpp}`.
The closure scopes itself to one scene root through
`item->get_item_host() != this`, so the helper takes a `Scene_host*` filter.

## 8. Extract `Rendertarget_mesh_registry` from `Scene_root` (small effort, low impact)

Move `m_rendertarget_meshes`, `m_rendertarget_meshes_mutex`, the
`is<Rendertarget_mesh>` branches of `register_mesh` / `unregister_mesh` and
`update_pointer_for_rendertarget_meshes` into a helper class. It removes one of
`Scene_root`'s two mutexes.

## 9. Type-checked `Item_host` downcasts (small effort, low impact)

`erhe::Item_host*` is `static_cast` to `Scene_root*` at many call sites
(`items.cpp`, `create.cpp`, `geometry_graph_mesh.cpp`, `animation_window.cpp`,
`mesh_operation.cpp`, ...). Give `Item_host` a virtual type identification a
`dynamic_cast` or a checked accessor can use, so a host that is not a
`Scene_root` is caught instead of reinterpreted.

## 10. Validate the physics owner pointer (small effort, low impact)

The Jolt body activation / deactivation callbacks in `scene_root.cpp`
`reinterpret_cast<Node_physics*>(owner)` with no validation. Carry a tagged
handle, or validate the pointer against the scene's registered attachments,
so a stale or foreign owner is rejected rather than dereferenced.

## 11. Initialize the first-frame time baseline (small effort, low impact)

`Time::prepare_update()` computes the host frame duration against
`m_host_system_last_frame_start_time`, which is zero on the first frame, so
`m_host_system_last_frame_duration_ns` is an enormous value for that frame.
The simulation duration is capped at 25 ms, so the simulation is unaffected;
initialize the baseline to the current time anyway, so the reported host frame
duration is meaningful from the first frame.

## Considered and rejected for now

### Narrow `App_context` into focused interfaces (large effort)

`app_context.hpp` holds raw pointers to every subsystem and is included almost
everywhere, which is real coupling; focused interface bundles (`Scene_access`,
`Render_access`, `Selection_access`) would narrow it. Rejected because
`App_context` is deliberately one directory for subsystems to find each other,
and that makes changes simpler than N bundles would.

### Narrow Command dependencies away from `App_context` (medium effort)

Every `Command` subclass stores `App_context&` and reaches through it even when
it needs one subsystem, which inflates include fan-out. Parked for the same
reason as the `App_context` split: it only pays off as part of that change.

### Extract `Editor` initialization into a builder (medium effort)

`editor.cpp` builds around thirty subsystems and owns the members. Rejected
because the constructor is what handles the subsystem dependency order;
distributing the construction would make that order harder to manage, not
easier.
