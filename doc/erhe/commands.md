# erhe_commands

Stability: stable

## Purpose
Input command system that maps physical input events (keyboard, mouse, controller, XR actions,
menus) to application commands. Commands have a state machine (Disabled -> Inactive -> Ready -> Active)
and bindings are priority-sorted so only the highest-priority eligible command consumes an event.
Key, mouse and controller bindings are user-editable: code declares each command's default
bindings, and a per-command override replaces them.

## Key Types
- `Command` -- Base class for an action; override `try_call()` / `try_call_with_input()`.
  Its name is its identity: `Commands::register_command()` aborts on an empty or duplicate name.
  Editor names follow `Group.action` (`Fly_camera.move_forward`); the group prefix is used by the
  editor's Input Bindings window.
- `Command_host` -- Provides enable/disable context for groups of commands (e.g. a tool window).
- `Commands` -- Central registry. Implements `Input_event_handler`, dispatches events to bindings,
  owns default bindings and user overrides.
- `Command_binding` -- Base for all binding types (Key, Mouse_button, Mouse_drag, Mouse_motion, Mouse_wheel, Menu, Controller_axis, Controller_button, Xr_boolean, Xr_float, Xr_vector2f, Update).
- `Button_trigger` -- `Button_pressed`, `Button_released` or `Any` (both). Key, mouse button,
  controller button and XR boolean bindings take one. A key binding on `Any` calls its command on
  press and release; the command reads the state from `Input_arguments::variant::button_pressed`
  (the fly camera's movement keys work this way). A mouse button binding is a click: the press
  readies the command, which is called on the press or on the release, so it takes no `Any`.
- `Binding_desc` (`binding_desc.hpp`) -- One editable binding as a value: `Binding_kind` (key,
  mouse_button, mouse_drag, mouse_wheel, mouse_motion, controller_axis, controller_button), code
  (keycode, mouse button, axis or button index), optional modifier mask (empty matches any
  modifiers, a value matches exactly), trigger. Text form via `to_string()` / `parse()`, label via
  `to_display_string()`, `overlaps()` for conflicts.
- `Input_kind` -- What a command consumes: `button` (key, mouse_button, controller_button),
  `drag`, `wheel`, `motion`, `axis`, or `internal` (not user-bindable).
- `Input_arguments` -- Union-based payload carrying button, float, vec2, or pose data plus modifiers/timestamp.
- `State` -- Enum: Disabled, Inactive, Ready, Active.
- `Helper_command` / `Drag_enable_command` / `Lambda_command` / `Redirect_command` -- Utility command subclasses.

## Public API
- `commands.register_command(cmd)` -- Register a command instance (unique name).
- `commands.bind_command_to_key(cmd, keycode, trigger, modifier_mask)`,
  `bind_command_to_mouse_button / _mouse_drag / _mouse_wheel / _mouse_motion / _controller_axis /
  _controller_button(...)` -- Declare a default binding.
- `commands.bind_command_to_menu(cmd, menu_path)` -- Offer the command in the ImGui menu; not
  editable. `Menu_binding::get_shortcut_label()` holds the command's first button binding for
  display (`Ctrl+X`).
- `commands.bind_command_to_xr_*_action(...)`, `bind_command_to_update(cmd)` -- XR and per-frame
  bindings; not editable.
- `commands.tick(timestamp_ns, input_events)` -- Process a frame's input events.
- User-editable bindings: `get_input_kind()`, `get_default_bindings()`,
  `get_effective_bindings()`, `has_binding_override()`, `set_binding_override()`,
  `clear_binding_override()`, `clear_all_binding_overrides()`, `get_binding_conflicts()`,
  `add_bindings_changed_callback()`; persistence form `get_binding_overrides()` /
  `apply_binding_overrides()` with `Binding_override {command_name, bindings}`.
- Only one mouse command can be active at a time (`accept_mouse_command` gating).
- `commands.sort_bindings()` -- Re-sort the dispatch tables by command priority after a priority
  change (e.g. a tool switch), rebuilding them first if bindings changed. `tick()` runs commands
  with the command mutex held, and the mutex is recursive, so a command may call back into
  `Commands`. When `sort_bindings()` is called from a command during `tick()`, it only records the
  request; `tick()` applies it before the next event and after the last one, so no dispatch table
  is reordered while a loop iterates it.

## Binding model

- A command's editable bindings are all of one input kind, taken from its default bindings;
  a command with only a menu binding is a `button` command (any key can be given to it). Declaring
  defaults of two kinds for one command aborts. Update and XR bindings do not make a command
  bindable.
- `set_binding_override(command, bindings)` replaces all editable bindings of the command; an
  empty list unbinds it. Every binding must match the command's input kind, and a mouse button
  click cannot trigger on `Any`; otherwise nothing changes and the error names the reason. A user
  drag binding takes `drag_call_on_button_down_without_motion` from the command's default drag
  binding, since that is the command's behavior rather than the user's choice of input.
- The dispatch tables (`get_key_bindings()`, `get_mouse_bindings()`, ...) are derived state,
  rebuilt from "override if present, else defaults" at the start of the next `tick()` or by
  `sort_bindings()` after any declaration or edit, never in the steady state. Declaration order is
  kept, and key bindings dispatch in that order with every binding that has a modifier mask ahead
  of every binding without one, so Ctrl+A reaches a Ctrl+A binding before a mask-less A binding
  (which matches any modifiers) can consume it; an overridden command's bindings take the place
  of its first default. A command whose bindings changed is made inactive first, so a rebind during a
  drag leaves no active mouse command behind. The rebuild also recomputes the conflict list, the
  menu shortcut labels, and then calls the bindings-changed callbacks.
- `apply_binding_overrides()` replaces all overrides at once. Entries naming commands this build
  does not register are kept and returned by `get_binding_overrides()` unchanged; entries that fail
  validation are logged and dropped.
- Conflicts are informational: two commands whose bindings some event would fire both
  (`Binding_desc::overlaps()`: same kind and code, modifier masks that can both match, overlapping
  triggers). Host priority still decides which command consumes the event.

The editor's Input Bindings window, the `input_bindings.json` file and the MCP binding tools are in
[../editor/input_bindings.md](../editor/input_bindings.md).

## Dependencies
- **erhe libraries:** `erhe::window` (public), `erhe::log` (public), `erhe::xr` (public, only when `ERHE_XR_LIBRARY=openxr`), `erhe::profile`, `erhe::verify` (private)
- **External:** glm, fmt

## Notes
- XR bindings (`Xr_*_binding`, `bind_command_to_xr_*_action()`, `get_xr_*_bindings()`) are compiled only with `ERHE_XR_LIBRARY_OPENXR`; without OpenXR the library does not link `erhe::xr`.
- The state machine prevents conflicting commands from activating simultaneously.
- Tests: `erhe_commands_tests` (`src/erhe/commands/test/`): `Binding_desc` text form round trips,
  overrides, input kind enforcement, conflicts, rebind during a drag, `sort_bindings()` from a
  command dispatched by `tick()`, masked key bindings dispatching before mask-less ones.
