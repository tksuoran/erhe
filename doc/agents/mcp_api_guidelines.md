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
  `set_item_properties`, `set_item_property`, `import_gltf` and the rest - takes
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

## Arguments are validated, never reinterpreted

A tool refuses an argument that is outside its domain and names the argument
and the offending value in the error; it does not silently turn it into
something valid. A value that is only off by the rounding of its decimal
serialization is accepted and cleaned up.

Rotations given as `rotation_xyzw` are the standing case: every tool that
takes one (`set_node_transform`, `transform_selection`, `create_shape`,
`place_brush`, `place_brushes`, `paste_pose`) passes it through `make_unit_quaternion()`
(`src/editor/mcp/mcp_server_shared.hpp`). A quaternion whose length is within
`1e-3` of 1 is normalized; a zero, non-finite or scaled quaternion is refused,
because applying it would bake a scale into the rotation or produce NaNs that
the caller never asked for.

## Document edits are operations

A tool that changes the document (scene content, item properties, materials,
scene settings such as the lightmap tile overrides) builds the same
`Operation` the UI uses for that edit and hands it to the `Operation_stack`
(`execute_now()` when the reply reports the new state, `queue()` otherwise),
several fields of one call grouped into one `Compound_operation` (with
`Compound_child_error::roll_back`, so a child that refuses its edit leaves
nothing of the call applied, `doc/editor/operations.md`). The call then
leaves exactly one undo entry, undo steps back over it, and the UI and MCP
edits of the same field behave alike. A write the property store refuses
(sealed item, validation) makes the call an error result naming the
property, with no undo entry and the redo history kept
(`Mcp_test.refused_physics_edit_is_an_error_and_keeps_the_history`). Writing a field directly is not
undoable and is a defect. `Mcp_test.document_edits_record_one_undo_entry_each`
(`src/editor/mcp/test/mcp_server_tests.cpp`) asserts this for the document
edit tools; a new one adds its case there. Editor state that is not the
document (window visibility, frame pacing, log levels, graphics presets) is
not undoable.

The reflective property write is `set_item_properties`: any number of
registered properties of one item (or of one property sub-object) as one
`Property_edit_operation` run with `execute_now()`, every entry checked
against the live state before anything is written, so one bad entry fails
the whole call naming the property. `set_item_property` is its one-entry
form and also executes at once: a later call in the same `batch` sees the
write, and the reply carries the property's `before` / `after` local layer
and effective `value` (no `"queued"` field).

A field that is a registered property is edited through
`set_item_properties` and has no per-type edit tool: lights, cameras,
materials, the physics items and the scene's `ambient_light` are written
that way (`doc/agents/mcp_server_usage.md` "set_item_properties /
set_item_property"). A per-type tool exists only for what is not a
property: `set_collision_shape` (the collision shape, through
`Collision_shape_set_operation`), `rebuild_joint` (runtime state, no undo
entry) and `set_scene_settings` (the codegen `Scene_settings` struct,
through `Scene_settings_set_operation`; it refuses an `ambient_light`
argument and names the scene item's `ambient_light` property instead).
`get_property_schema` lists the property names and value forms of each
owner type.

## Future work

- [plans/property_undo_and_reflective_mcp.md](../plans/property_undo_and_reflective_mcp.md):
  the scene settings edits that still bypass `Scene_settings_set_operation`,
  and the script runs that verify the move to `set_item_properties`.
