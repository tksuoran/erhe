# MCP API guidelines

Stability: stable

Guidelines for the editor's built-in MCP server (`src/editor/mcp/`).

## Do not depend on state the API does not directly control

An MCP tool call must behave the same regardless of what the user last did in
the editor UI. The result of a call should be fully determined by:

- the tool's own arguments (with defaults fixed in the handler and documented
  in the tool schema), and
- the actual document/scene state the tool is defined to act on.

It must NOT be influenced by ephemeral UI-panel state such as window sliders,
checkboxes, combo selections, or "last used" values. Those belong to the
interactive workflow; an MCP client cannot see them, cannot reliably set them,
and silently inheriting them makes tool calls non-reproducible.

Concretely:

- When an editor operation is parameterized by a UI widget (for example the
  Operations window's *Generate UVs* checkbox for Catmull-Clark / Sqrt3
  subdivision), the MCP tool must expose that parameter as an explicit
  argument with its own fixed default (`catmull_clark.generate_texcoords`,
  default `true`). The handler passes the explicit value through and never
  reads the widget's current state.
- The same applies to numeric operation parameters: `remesh` /
  `decimate` / `smooth` take `regenerate_attributes`, target counts, etc. as
  arguments rather than reusing the window's slider values.
- Echo the effective values of such arguments in the tool's response where
  practical, so a transcript records what actually ran.

Acting on the current object selection is the one sanctioned implicit input,
because it is scene state the API itself can control (`select`-family tools),
and geometry tools additionally accept explicit node targets
(`node_ids` / `node_id` / `node_name` + `scene_name`) that override and then
restore the selection. Prefer explicit targets in scripted use.

When adding a new MCP tool that wraps an `Operations` method, check whether
the method reads any `Operations` member that the window's widgets mutate; if
so, add an overload taking the value explicitly and call that from the MCP
handler.

## UI-driving tools and scene-scripting tools are separate

The server carries two families of tools and each has its own job.

- **Scene scripting** - `create_shape`, `transform_selection`,
  `set_item_property`, `edit_material`, `import_gltf` and the rest - takes
  explicit arguments and acts on scene state. This is how a script or an
  agent changes a document.
- **UI driving** - the `get_imgui_*`, `imgui_*`, `mouse_*`, `key_press`,
  `type_text`, `inject_input_events` and `get_transform_handles` tools,
  described in [mcp_ui_driving.md](mcp_ui_driving.md) - exercises the
  interactive entry points (menus, docking, property rows, gizmo drags,
  viewport gestures) so UI behavior can be verified headlessly.

Use a UI-driving call when the interactive path itself is what is under test:
a menu entry that must open a window, a drag that must move a selection, a
row that must accept a typed value, a splitter that must move both axes. Set
a parameter with the explicit tool that exposes it - a UI-driving call that
reaches a widget to change a value the API already exposes is slower, depends
on the layout and window visibility of the moment, and verifies nothing the
explicit call does not.

A new UI-driving tool follows the same rule as the rest: its behavior is
determined by its own arguments and by the scene and window state it is
defined to act on, and it reports what it resolved (`target`, the injected
event counts) so a transcript records what actually ran.
