# MCP API guidelines

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
