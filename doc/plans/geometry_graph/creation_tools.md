# AI creation tools: outstanding work

Status: in progress

This plan extends `doc/geometry_nodes.md` and
`doc/geometry_graph_transform_from_node.md`. It lists what the AI-creation
workflow still lacks; the workflow itself and its recipes live in
`.agents/skills/erhe-creations/SKILL.md` and its
`references/geometry_graph_sculpt.md`, which are the canonical sources to read
before any creation work.

## Graph-hover to Hierarchy highlight: confirm interactively

Hovering a graph node that references a scene node (`Transform_from_node`,
`Lattice_node`, through the `Geometry_graph_node::get_referenced_scene_node()`
virtual) flags that node, its ancestors and its descendants with the transient
`Item_flags` bits `hovered_in_graph`, `child_hovered_in_graph` and
`ancestor_hovered_in_graph` (not in the glTF persistent allowlist), and the
Hierarchy draws the same blue rect as a viewport hover; a folded ancestor takes
over through `child_hovered`. `Geometry_graph_window::update_graph_hover_flags()`
is driven for the primary window from `update_evaluation()` and for the extra
"[N]" windows from `Editor_windows::update_once_per_frame()`.

The logic mirrors `Hover_tool` and builds clean, but it has never been
confirmed interactively - automated cursor-over-canvas verification does not
work, because taking console focus clears the hover and display scaling skews
the coordinates. **Ask the user whether the highlight works before building on
it.**

## Per-axis UV control for graph bodies

A graph-built body has no usable UV layout, which is why its texturing is
limited to mottle-style procedural noise. The answer is the
`project_attribute` node - see
[attribute_projection.md](attribute_projection.md).

## Nested drivers do not cascade

A `transform_from_node` captures ONE node's local transform, so a pose rig
built from them is FLAT: there are no FK chains. Transform composition needs
the `mat4` pin plumbing described in
[geometry_nodes.md](geometry_nodes.md) ("A `mat4` output pin"), together with
the uid-based reference persistence in the same plan.

## Smaller items

- Refactor `Lattice_node`'s hand-rolled `BeginDragDropTarget` block to the
  shared `item_reference_imgui` widget, which `transform_from_node` already
  uses.
- Hovering the graph's Output node could highlight the BOUND scene nodes, via
  the sweep `apply_baked_products_to_attachments` performs (zero or many
  nodes). Deliberately skipped in the first cut.
- The Laplacian `smooth` MCP operation explodes meshes. It is unfiled and
  unusable; fix it when a creation needs it.

## Iteration notes

- A creation script rebuilds its whole graph on `--reuse` (hundreds of MCP
  calls); there is no `--only` mode, because graph assets cannot be recreated
  by name.
- Repeated `--reuse` scene cycles can spawn the new viewport as a tiny corner
  window; relaunch the editor to restore the docked layout.
- For screenshots, back up `config/editor/default_viewport_config.json`, set
  `edge_lines: false` and restore it afterwards. The hover hotbar and label
  pollute a capture and linger for a few seconds after the mouse stops.
  `screenshot()` runs a hide pass that re-hides windows shown through
  `set_window_visibility`, so use a raw `capture_screenshot` when the windows
  must stay up (its window argument is the title, for example
  `"Scene Hierarchy [1]"`).
