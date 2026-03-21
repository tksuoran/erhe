# erhe_commands

## Purpose
Input command system that maps physical input events (keyboard, mouse, controller, XR actions,
menus) to application commands. Commands have a state machine (Disabled -> Inactive -> Ready -> Active)
and bindings are priority-sorted so only the highest-priority eligible command consumes an event.

## Key Types
- `Command` -- Base class for an action; override `try_call()` / `try_call_with_input()`.
- `Command_host` -- Provides enable/disable context for groups of commands (e.g. a tool window).
- `Commands` -- Central registry. Implements `Input_event_handler`, dispatches events to bindings.
- `Command_binding` -- Base for all binding types (Key, Mouse_button, Mouse_drag, Mouse_motion, Mouse_wheel, Menu, Controller_axis, Controller_button, Xr_boolean, Xr_float, Xr_vector2f, Update).
- `Input_arguments` -- Union-based payload carrying button, float, vec2, or pose data plus modifiers/timestamp.
- `State` -- Enum: Disabled, Inactive, Ready, Active.
- `Helper_command` / `Drag_enable_command` / `Lambda_command` / `Redirect_command` -- Utility command subclasses.

## Public API
- `commands.register_command(cmd)` -- Register a command instance.
- `commands.bind_command_to_key(cmd, keycode, pressed, modifier_mask)` -- Bind to keyboard.
- `commands.bind_command_to_mouse_drag(cmd, button, ...)` -- Bind to mouse drag gesture.
- `commands.bind_command_to_menu(cmd, menu_path)` -- Bind to ImGui menu item.
- `commands.bind_command_to_update(cmd)` -- Called every frame.
- `commands.tick(timestamp_ns, input_events)` -- Process a frame's input events.
- Only one mouse command can be active at a time (`accept_mouse_command` gating).

## Dependencies
- **erhe libraries:** `erhe::window` (public), `erhe::log` (public), `erhe::xr` (public, optional), `erhe::profile`, `erhe::verify` (private)
- **External:** glm, fmt

## Notes
- Bindings are sorted by command priority; call `sort_bindings()` after setup.
- XR bindings are conditionally compiled with `ERHE_XR_LIBRARY_OPENXR`.
- The state machine prevents conflicting commands from activating simultaneously.
