# Tool System Improvements

Detailed improvement plan for the Tool class hierarchy and Command system in `src/editor/`.

See also [`editor_improvements.md`](editor_improvements.md) for the broader editor improvement list.

## 1. Migrate Transform_tool and Grid_tool to Tool_window composition

**DONE.**

**Effort:** Small | **Impact:** Medium (consistency)

`Transform_tool` (`transform/transform_tool.hpp:154`) and `Grid_tool` (`grid/grid_tool.hpp:25`) still inherit from both `erhe::imgui::Imgui_window` and `Tool`:

```cpp
class Transform_tool
    : public erhe::imgui::Imgui_window
    , public Tool
{ ... };

class Grid_tool
    : public erhe::imgui::Imgui_window
    , public Tool
{ ... };
```

All other tools that had this pattern — `Fly_camera_tool`, `Paint_tool`, `Hotbar`, `Hover_tool` — were already refactored to the composition approach: they inherit only from `Tool` and own a `Tool_window m_window` member that delegates `imgui()` (and optionally `flags()`, `on_begin()`, `on_end()`) back to the tool via callbacks. See `tools/tool_window.hpp`.

These two remaining classes should be migrated for consistency. The migration is mechanical: remove `Imgui_window` from the inheritance list, add a `Tool_window m_window` member, rename the `imgui()` override to a private `window_imgui()` method, and route any calls to `Imgui_window` methods (like `hide_window()`, `set_window_visibility()`) through `m_window`.

## 2. Auto-register tools with the Tools container

**DONE.**

**Effort:** Small | **Impact:** Low (reduces boilerplate)

Every Tool subclass manually calls `tools.register_tool(this)` at the end of its constructor. This is repeated in 17 places across the codebase:

- `brush_tool.cpp`, `create.cpp`, `grid_tool.cpp`, `physics_tool.cpp`
- `fly_camera_tool.cpp`, `hotbar.cpp`, `hover_tool.cpp`, `hud.cpp`
- `material_paint_tool.cpp`, `paint_tool.cpp`, `selection_tool.cpp`
- `transform_tool.cpp`, `trs_tool.cpp`
- `move_tool.cpp`, `rotate_tool.cpp`, `scale_tool.cpp`

Forgetting this call means the tool silently does not appear in the tool system. There is no compile-time or runtime warning.

The simplest fix is to pass `Tools&` to the `Tool` base class constructor and have it call `register_tool(this)` automatically. Since every Tool subclass already receives `Tools&` and passes `App_context&` to the base, this requires minimal change. The only subtlety is that `register_tool` is called before the derived constructor body runs, but `register_tool` only stores the pointer and reads `get_flags()` — both of which are set via `set_flags()` / `set_base_priority()` in the member initializer list or very early in the constructor. The flag-setting calls would need to move before the base constructor, or `register_tool` could be deferred (e.g., `Tools` could store pending registrations and finalize them in a second pass).

An alternative is a builder pattern where `Tools` constructs all tools itself, but that is a larger change and ties tool creation to the container.

## 3. Extract message bus witness boilerplate into a helper

**DONE.**

**Effort:** Small | **Impact:** Low (reduces boilerplate)

**DONE.** `Message_bus::subscribe()` now returns a `std::shared_ptr<Subscription<Message_type>>` that holds the callback. The bus stores a `weak_ptr<Subscription<T>>` — when the subscriber drops the shared_ptr, the weak_ptr expires and the bus prunes the receiver. Each subscribing class holds explicit typed subscription members (e.g. `std::shared_ptr<Subscription<Hover_scene_view_message>>`), eliminating the dummy `std::make_shared<int>(0)` witness pattern.

## 4. Clarify Tool vs Command_host taxonomy

**DONE** (documentation added to `tool.hpp`; `Create` migrated to `Tool_window` composition).

**Effort:** Medium | **Impact:** Medium (maintainability)

The boundary between "Tool" and "Command_host that is not a Tool" is unclear:

| Class | Inherits from | Is a Tool? | Named `*_tool`? |
|-------|--------------|------------|-----------------|
| `Selection_tool` | `Tool` | Yes | Yes |
| `Selection` | `Command_host` | No | No |
| `Clipboard` | `Command_host` | No | No |
| `Debug_visualizations` | `Imgui_window` + `Renderable` | No | No |
| `Fly_camera_tool` | `Tool` | Yes | Yes |
| `Transform_tool` | `Imgui_window` + `Tool` | Yes | Yes |

`Selection` is a `Command_host` that manages selection state and owns commands like `Selection_delete_command`, but is not a Tool. Meanwhile `Selection_tool` *is* a Tool that handles mouse-based selection interaction. The two are separate classes in the same file (`selection_tool.hpp`). This split is not immediately obvious to someone reading the code.

`Debug_visualizations` renders tool-like overlays (joint axes, physics shapes, light frustums) and has an `Imgui_window` for settings, but inherits from `Renderable` instead of `Tool` — so it doesn't participate in the priority system or appear in the tool list.

Recommendations:
- Document the distinction: a `Tool` is specifically a Command_host that participates in the priority/toolbox system and can render into the viewport. A `Command_host` that only provides keyboard shortcuts or manages state is not a Tool.
- Consider renaming `Selection_tool` to `Selection_interaction` or similar to avoid confusion with `Selection`.
- Consider whether `Debug_visualizations` should be a Tool (it does `tool_render`-like work) or whether the current Renderable approach is intentional.

## 5. Priority system cleanup

**DONE** (5a: named constants, 5b: sort_bindings fixed). Items 5c and 5d are deferred — they require behavioral testing.

**Effort:** Medium | **Impact:** Medium (correctness)

The priority system has several issues:

### 5a. Magic numbers

Active mouse command priority is hardcoded as `10000` in `commands.cpp:357`:
```cpp
return 10000; // TODO max priority
```

Tool priority is calculated as `base_priority * 20 + host_priority` in `command.cpp:191`, where `20` is another unexplained constant. The priority boost for the active tool is `100` (set in `tools.cpp:349`).

Current `c_priority` values across tools:
| Priority | Tools |
|----------|-------|
| 1 | Transform_tool, Move_tool, Rotate_tool, Scale_tool |
| 2 | Material_paint_tool, Physics_tool |
| 3 | Selection_tool |
| 4 | Brush_tool, Create, Paint_tool |
| 5 | Fly_camera_tool |

These should be named constants or an enum, and the `* 20` multiplier and `10000` cap should be documented or derived from the priority range.

### 5b. sort_bindings() only sorts mouse bindings

`Commands::sort_bindings()` delegates entirely to `sort_mouse_bindings()`:
```cpp
void Commands::sort_bindings() { sort_mouse_bindings(); }
```

Key bindings and update bindings are not sorted by priority. If two tools bind the same key, the first one registered wins regardless of priority. This may be intentional (key bindings are typically unique), but it is not documented.

### 5c. Stale sorting after priority changes

When a tool becomes the priority tool, `Tools::set_priority_tool()` calls `set_priority_boost(100)` on the tool, then `commands->sort_bindings()`. But `set_priority_boost()` also calls `handle_priority_update()` on the tool *before* the sort happens. If `handle_priority_update()` tries to interact with commands that depend on correct ordering, it sees stale state.

### 5d. Update bindings run in registration order

Update bindings (per-frame tick commands) are processed in the order they were registered, not in priority order. If two tools both register update commands and one should take precedence, there is no mechanism for that.

## 6. Tool_flags documentation and enforcement

**DONE** (inline documentation added to Tool_flags members in step 5). The original observation that only `trs_tool` uses `toolbox` was incorrect — after step 2 (auto-register), flags are passed via constructors and `toolbox` is used by 10 tool classes.

**Effort:** Small | **Impact:** Low (clarity)

`Tool_flags` defines five flags: `enabled`, `background`, `toolbox`, `secondary`, `allow_secondary`. Their semantics are only partially clear from usage:

- **`enabled`**: Controlled by `set_enabled()` in `Command_host`. Disabled tools' commands are skipped.
- **`background`**: Tool is always active (e.g., debug visualizations). Registered in `m_background_tools` instead of `m_tools`.
- **`toolbox`**: Tool appears in the hotbar/tool selector. Subject to priority-based enable/disable in `set_priority_tool()`.
- **`secondary`**: Tool can remain active alongside the priority tool (if the priority tool sets `allow_secondary`).
- **`allow_secondary`**: When this tool is the priority tool, secondary tools stay enabled.

However, only one place in the entire codebase calls `set_flags()` with `Tool_flags::toolbox` — `trs_tool.cpp:149`. All other tools leave flags at the default value of `0`. This means the toolbox/secondary system is effectively only used by `Trs_tool` (the combined translate/rotate/scale selector). The hotbar populates its slots from tools, but the `toolbox` flag filtering doesn't match what actually appears there.

The flag semantics should be documented in `tool.hpp`, and if flags beyond `toolbox` are unused, they should either be removed or their intended use should be implemented.

## 7. Subtool decoupling from Transform_tool

**DONE.**

**Effort:** Medium | **Impact:** Low (only 3 classes affected)

`Move_tool`, `Rotate_tool`, and `Scale_tool` inherit from `Subtool`, which provides `get_shared()`:

```cpp
auto Subtool::get_shared() const -> Transform_tool_shared& {
    return m_context.transform_tool->shared;
}
```

This reaches through `App_context` → `Transform_tool` → public `shared` member. Every subtool method that needs transform state goes through this chain. The `Subtool::end()` method similarly calls `m_context.transform_tool->record_transform_operation()`.

This means subtools cannot exist without a `Transform_tool` instance, cannot be tested independently, and are tightly coupled to the god object. The coupling is structural rather than just incidental — `Transform_tool_shared` contains the selection entries, anchor transforms, visualization state, and settings that all subtools need.

A cleaner approach would be to pass `Transform_tool_shared&` directly to `Subtool` at construction time (or at `begin()` time), and provide a callback or interface for `record_transform_operation()`. This would make the dependency explicit and remove the `App_context` chain.

---

## Future work

### Narrow Command dependencies away from App_context

Every Command subclass (`Transform_tool_drag_command`, `Toggle_menu_visibility_command`, `Hotbar_trackpad_command`, etc.) stores `App_context& m_context` and reaches through it to access specific tools or subsystems:

```cpp
class Transform_tool_drag_command : public erhe::commands::Command {
    App_context& m_context;  // only needs Transform_tool
};

class Clipboard_paste_command : public erhe::commands::Command {
    App_context& m_context;  // only needs Clipboard
};
```

This means every command is implicitly dependent on all 57 pointers in `App_context`. A command that only needs `Transform_tool` still transitively includes the entire editor dependency graph.

This overlaps with the broader App_context refactoring (item #1 in `editor_improvements.md`). When that work is undertaken, commands should receive only the specific subsystem they operate on, not the entire context. This would make dependencies explicit and testable, and would significantly reduce include fan-out.
