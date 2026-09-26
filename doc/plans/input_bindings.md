# User-editable, persistent input bindings

Status: proposed

Commands keep declaring their default bindings in code
(`Commands::bind_command_to_*`, see [../erhe/commands.md](../erhe/commands.md)).
This plan adds a per-command user override that replaces those defaults, an
editor window to edit the overrides, and a config file that persists them.

## Requirements

- R1. Code declares the default bindings of every command, as today. A
  command the user has not touched follows the code defaults, so a default
  changed in code reaches existing users.
- R2. The user can, per command: add a binding, remove a binding, change a
  binding's input (key / mouse button / controller button / axis plus
  modifiers), and reset the command to its defaults. "Reset all" resets every
  command.
- R3. Overrides persist across runs in a config file and are applied at
  startup before the first input event is dispatched.
- R4. The user can only assign an input of the kind the command consumes
  (D3): a drag command gets a button + modifiers to drag with, a wheel
  command gets modifiers, a button command gets any key / mouse button /
  controller button chord.
- R5. Commands without an input binding today (menu-only commands such as
  `Geometry.Triangulate`) are bindable too: they are button commands.
- R6. The editor shows the effective binding next to the command where the
  command is offered: the menu item shortcut text in the Commands menu and
  the menus built from `Menu_binding`.
- R7. A chord bound to more than one command is reported in the UI (both
  rows marked, each naming the other command). It is not refused: host
  priority already resolves which command consumes an input, and two
  commands on the same chord in mutually exclusive hosts (e.g. two tools) are
  legitimate.
- R8. An AI-driven run (`ERHE_AI_DRIVER=1`) neither reads nor writes the
  override file: it runs on the code defaults so scripts are deterministic
  (same rule as `user_state.json`, `src/editor/editor_settings_store.hpp`).
- R9. Changing bindings never happens per frame: the binding tables are
  rebuilt once, when an edit is committed (AGENTS.md "No update each frame").
- R10. Menu placement (`Menu_binding` paths), `Update_binding`s and OpenXR
  action bindings are not user-editable. OpenXR bindings are owned by the
  OpenXR runtime's suggested-binding / rebinding flow.

## Design

### D1. Stable command identity

The persisted key is the command name, so names must be unique and stable.
Today they are not unique at runtime:

- `Fly_camera.move` is shared by 12 instances and
  `Fly_camera_active_axis_float_command` by 6
  (`src/editor/tools/fly_camera_tool.cpp`).
- `Map.scroll` appears twice in `src/hextiles`.

`Commands::register_command()` rejects a duplicate name (`ERHE_VERIFY`), and
every command gets a distinct dotted `Group.action` name
(`Fly_camera.move_forward`, `Fly_camera.axis_translate_x`, ...). The group
prefix before the first `.` is the grouping used by the UI (D6).

### D2. Press/release pairs become one command

The fly camera binds each movement key twice - a `*_active_command` on press
and a `*_inactive_command` on release (`fly_camera_tool.cpp` 795-806).
Rebinding "move forward" would require editing two rows and keeping them in
sync. `Key_binding` (and `Mouse_button_binding`) gain a `Button_trigger`
like `Controller_button_binding` already has (`Button_pressed`,
`Button_released`, `Any`), the pressed state reaches the command through
`Input_arguments`, and each pair collapses into one command bound with
`Button_trigger::Any` that reads the pressed state. The `bool pressed`
parameter of `bind_command_to_key` becomes the `Button_trigger` (AGENTS.md:
no boolean arguments in new code). `Key_binding::on_key` consumes a release
only when the binding triggers on release, preserving today's
`consumed && pressed` rule for press-only bindings.

### D3. Command input kind

Each command has an `Input_kind`, inferred at `sort_bindings()` time from its
default bindings:

| Input kind | Default binding types | Assignable inputs |
|---|---|---|
| `button` | Key, Mouse_button, Controller_button, or Menu only | key / mouse button / controller button + modifiers + trigger |
| `drag` | Mouse_drag | mouse button + modifiers |
| `wheel` | Mouse_wheel | modifiers |
| `motion` | Mouse_motion | modifiers |
| `axis` | Controller_axis | axis index + modifiers |
| `internal` | Update / XR only, or no binding | not listed, not bindable |

A command whose defaults mix kinds is a `ERHE_VERIFY` failure (none does
today; verify while implementing phase 1). Binding-semantic parameters that
belong to the command rather than to the user - `call_on_button_down_without_motion`
of a drag, the trigger of an XR button - stay with the default and are copied
onto any user binding of that command.

### D4. Binding description value type (erhe::commands)

A plain value type `Binding_desc` describes one binding independent of the
command: input kind, keycode or button or axis, `std::optional<uint32_t>`
modifier mask (empty = any modifiers, matching today's semantics), trigger.
It has `to_string()` / `parse()` for a compact text form used by the config
file, the UI and MCP:

    key:ctrl+x                 key:w:any          key:delete
    mouse_button:x1            mouse_drag:alt+right
    mouse_wheel                mouse_wheel:shift
    controller_axis:3          controller_button:0:released

Modifier names are `ctrl`, `shift`, `alt` (`Key_modifier_bit_menu`),
`super`; `any+` prefixes a chord that ignores modifiers. Key and mouse button
names come from `erhe::window::c_str(Keycode)` / `c_str(Mouse_button)`;
`erhe::window` gains the inverse lookups `keycode_from_string()` /
`mouse_button_from_string()`. `parse()` returns `std::optional` and the
caller logs and skips an unparseable entry (a stale file must not abort
startup).

`Command_binding::c_type_strings` is fixed to match the `Type` enum (it misses
`Controller_axis` / `Controller_button`, so every name after `Mouse_wheel` is
off by two today) - the UI and MCP print it.

### D5. Defaults vs. effective bindings in `Commands`

`Commands` keeps, per command, the default `Binding_desc` list (recorded by
every `bind_command_to_*` call during setup) and an optional override list.
The binding vectors (`m_key_bindings`, `m_mouse_bindings`, ...) become derived
state rebuilt from "override if present, else defaults" by one function,
`rebuild_bindings()`, which also runs `sort_bindings()`. New public API:

- `get_input_kind(const Command&) -> Input_kind`
- `get_default_bindings(const Command&) -> std::span<const Binding_desc>`
- `get_effective_bindings(const Command&) -> std::span<const Binding_desc>`
- `has_binding_override(const Command&) -> bool`
- `set_binding_override(Command&, std::span<const Binding_desc>)` - an empty
  span is a valid override (command unbound)
- `clear_binding_override(Command&)` / `clear_all_binding_overrides()`
- `get_binding_overrides(std::vector<Binding_override>& out)` /
  `apply_binding_overrides(std::span<const Binding_override>)` for
  persistence; unknown command names are logged and kept in a pass-through
  list so a file written by a build with more commands (or a different
  platform) round-trips without losing entries.

A rebuild while a command is `Active` (mid-drag) would drop the binding the
active mouse command is tracked through. Setters therefore only record the
change; `rebuild_bindings()` runs at the start of the next `tick()` under
`m_command_mutex`, after first inactivating an active command whose binding
set changed. The rebuild happens once per committed edit, never per frame.

`Menu_binding` stays as is (placement, R10), but the menu renderer asks
`Commands` for a cached shortcut label per command (built by
`rebuild_bindings()`, first button binding's `to_string()` pretty form such
as `Ctrl+X`) and passes it to `ImGui::MenuItem(label, shortcut)` (R6).

### D6. Editor: Input Bindings window

A new `Input_bindings_window` (`src/editor/windows/`), also reachable from
the Settings window. One table, rows grouped under collapsible headers by
the command name group (D1):

- Command label, effective bindings as chips, a "modified" marker when an
  override exists, a conflict marker (R7) with a tooltip naming the other
  commands, per-row "Reset".
- Chip click: edit; chip `x`: remove; `+`: add.
- Filter text box (name and chord text), "Show modified only", "Reset all".

Chord capture for `button` commands happens inside the window through ImGui,
not through `Commands`: while a "press a key" popup is open, the window reads
the next `ImGui::IsKeyPressed` / `IsMouseClicked` plus `io.KeyMods` and maps
`ImGuiKey` back to `erhe::window::Keycode` (erhe_imgui gains `to_erhe()`,
the inverse of `from_erhe()` in `imgui_host.cpp`). Escape cancels. Because
the popup has keyboard focus, `want_capture_keyboard` keeps the chord from
also reaching the command system - verify that Delete / Ctrl+D pressed in
the popup do not delete / duplicate the selection. Drag, wheel, motion and
axis inputs are edited with combos (button, modifier checkboxes, axis index)
rather than captured.

Each committed edit calls `Commands::set_binding_override()` and then the
settings store (D7) directly - the ImGui widget is the change site (AGENTS.md
"No update each frame").

### D7. Persistence

A new codegen struct `Input_bindings_config` (version 1,
`src/editor/config/definitions/input_bindings_config.py`):

    Input_binding_override { command: String, bindings: Vector(String) }
    Input_bindings_config  { overrides: Vector(StructRef("Input_binding_override")) }

stored in `config/editor/input_bindings.json`, owned by
`Editor_settings_store` next to `editor_settings.json` / `user_state.json`
with the same load / touch / autosave lifecycle. It is a separate file
because it is edited independently of the settings knobs and is the file a
user would copy between machines. Only overridden commands are written;
the file does not exist until the first edit. The store skips it under
`is_ai_driver()` (R8). Desktop and OpenXR share the file: OpenXR bindings are
not editable (R10) and the desktop chords are the same in both modes.

Load order at startup: all parts construct and call `bind_command_to_*`
(defaults), then the editor applies the loaded overrides, then the first
`tick()` rebuilds. `hextiles`, `example` and the other `erhe::commands` users
never call `apply_binding_overrides()` and behave exactly as today.

### D8. MCP

Three tools for scripted verification, taking explicit arguments:
`list_input_bindings` (name, input kind, defaults, effective, modified,
conflicts), `set_command_bindings {command, bindings: [string]}`,
`reset_command_bindings {command | all}`. Registered in
`config/editor/mcp_tools.json` per the MCP server policy.

## Phases

One commit per phase; each builds `editor`, `src/example`,
`src/hello_swap`, `src/hextiles` (AGENTS.md "Building") and adds its
`CHANGELOG.md` line where it changes `erhe::commands` / `erhe::window` API.

1. Identity and cleanup: unique names (D1), `register_command` verify,
   `c_type_strings` fix, `Button_trigger` on key / mouse button bindings and
   the fly camera pair collapse (D2). Verify: fly camera WASDQE movement
   (press and release) unchanged, headless MCP camera move.
2. `Binding_desc` with `to_string` / `parse`, `erhe::window` name lookups
   (D4). New unit test target for `erhe_commands` (doc/testing.md):
   round-trip every keycode, mouse button and modifier combination; reject
   malformed strings.
3. Defaults / overrides / deferred rebuild in `Commands` (D3, D5), menu
   shortcut labels. Unit tests: override replaces defaults, empty override
   unbinds, reset restores, unknown names pass through, rebuild mid-drag
   inactivates the active command.
4. Persistence (D7) and MCP tools (D8). Verify headless: set an override via
   MCP, confirm the chord triggers the command via input injection
   (doc/agents/mcp_ui_driving.md), restart a non-AI run with a scratch copy
   of the file and confirm it reloads; confirm an AI-driven run ignores it.
   Restore any config file the run rewrote.
5. Input Bindings window (D6). Verify via MCP UI driving: capture a chord,
   conflict marker, reset. Hand the capture popup feel to the user for an
   interactive check.
6. Documentation: move the landed design into `doc/erhe/commands.md` and a
   new editor document `input_bindings.md` under `doc/editor/`, delete this
   plan.
