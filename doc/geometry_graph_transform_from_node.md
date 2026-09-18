# Geometry graph: `transform_from_node`

Stability: mostly stable

A geometry-graph node that takes its transform from a scene-graph node. The
geometry graph lives in the content library, which the scene owns
(`Content_library::set_owner` is the `Scene_root`), so a graph node can reach
the scene and reference a node in it. The transform-source scene node is
assignable by dragging it from the Hierarchy onto the graph node.

`Lattice_node` (`doc/lattice_deform_geometry_node.md`) holds the same kind of
reference for its cage frame, and the two nodes share every mechanism described
here.

## Shape

```
transform_from_node
  inputs : geometry "in"
  params : transform_node (scene node name, drag-and-drop), space (enum)
  outputs: geometry "out"
```

It mirrors `Transform_node::evaluate` but composes the *captured* scene-node
matrix instead of an authored TRS: an identity transform is a copy-on-write
passthrough, otherwise `copy_with_transform`. Applying the matrix directly
avoids TRS decomposition entirely - no Euler ambiguity, no negative-scale or
skew loss. That is why it is not a value node emitting translation, rotation
and scale into the existing `Transform_node`: that node's rotation pin is Euler
only (`vec3`, Z*Y*X), so a general matrix would have to survive an Euler
decomposition.

## Space

The `space` parameter (an int enum, serialized):

- `0 = local` (the default): `node->parent_from_node()`. Parent the driver
  under the bound mesh node and the transform composes in the graph output's
  local frame. This matches `Lattice_node`'s choice.
- `1 = world`: `node->world_from_node()`, for a driver living elsewhere in the
  scene. The node UI documents the caveat: the graph output is baked into the
  bound node's local space, so a world-space driver double-transforms when the
  bound node is not at identity.

"Relative to the bound node" is deliberately not offered: a graph asset can be
bound to zero or many scene nodes, so there is no unique bound node to be
relative to.

## Reference, lifecycle and threading

- The reference is an `Asset_reference` with
  `Asset_key{scope = scene_local, type = Asset_type::node, name = <node name>}`.
  `Asset_type::node` exists for exactly this: graph transform-driver
  references. Resolution walks the open scenes by node name; a `scene_local`
  miss does not latch, so a caller retries every frame, and `resolve()`
  self-heals a glTF uid into the key.
- Scene access from a graph node is main-thread only:
  `get_hosting_scene_root(get_owning_graph_mesh().get())` - the content-library
  item's `Item_host` *is* the owning `Scene_root`, and it is null on a shadow
  clone by construction.
- **Threading contract**: graphs evaluate asynchronously on shadow clones, so
  `evaluate()` must never touch live scene state. A live scene read happens in
  `update_live()` - the per-frame main-thread hook
  `Geometry_graph_window` drives for every `Graph_mesh` in every scene - is
  cached in a member, and `capture_evaluation_state()` copies the cache onto
  the shadow clone. `evaluate()` reads only `m_captured_transform`.
- **Live invalidation is polling, on purpose.** `update_live()` re-captures the
  driver's matrix, compares it and calls `mark_dirty()` on a change, so dragging
  the driver in the viewport re-evaluates the graph. Do NOT switch this to
  `Node_touched_message`: that fires only at the end of a gizmo drag or on undo,
  not during a drag and not for physics. (A cheaper compare is available if the
  mat4 compare ever matters: `node_data.transforms.world_from_node_serial`, a
  `uint64` funnelled through `Node::handle_transform_update`.)
- **Deletion**: `Asset_reference` holds a strong `shared_ptr`, so a deleted
  driver keeps feeding its last, frozen transform for the rest of the session
  and shows `"(unresolved: <name>)"` after a reload. No node-removal bus message
  exists, and this feature does not need one invented.

## Serialization

The `parameters` JSON is `{"transform_node": "<name>", "space": 0}`, written
unconditionally - also while the reference is unresolved or empty, so a
reference is never silently lost. `read_parameters` builds the `Asset_key` only
and touches no manager, because it can run off the main thread during a shadow
snapshot; it resets the captured matrix when the name or the space changes and
then calls `mark_dirty()`, which is the mandatory convention.

References persist by NAME, not by index, because the graph JSON is written in
the export collect phase *before* glTF node indices exist.

Because `geometry_graph_set_parameter` passes opaque JSON to
`read_parameters`, setting the driver by node name works over MCP with no
MCP-side code. The one MCP change a new node type always needs is its entry in
the `geometry_graph_add_node` `type` enum in `mcp_server_tool_list.cpp`;
without it MCP cannot create the node at all.

**Known weakness: a name-only key breaks on rename and is ambiguous under
duplicate names** (resolution takes the first match in deterministic order and
debug-logs the ambiguity).

## UI

`imgui()` serves both the canvas and the Node Properties window. It draws the
reference with the shared `editor::item_reference_imgui` widget, which speaks
the Hierarchy's drag payload contract (payload type
`erhe::scene::Node::static_type_name`, data `erhe::Item_base*`), self-scales
inside the node table cell and supplies the highlight rect, icon and clear
button; plus an `imgui_enum_combo` for the space. Keep `options.candidates`
empty on the canvas until the ax::NodeEditor popup Suspend / Resume question is
settled.

A drop commits an undoable `Geometry_graph_parameter_operation` by itself:
`mark_dirty()` inside `imgui()` sets `m_parameter_edit_in_progress`, and the
commit check fires the same frame because nothing stays active after a drop.

## Verification

- Over MCP: build box -> transform_from_node -> output, bind the mesh,
  `set_node_transform` on the driver, use `get_geometry_graph` as the
  evaluation barrier, and screenshot before and after - the graph mesh must
  follow. Then drag the driver with the gizmo live.
- Undo and redo of the drop, and of a driver rename.
- Save and load round trip through
  `scripts/geometry_nodes_smoke_test.py` and
  `scripts/scene_roundtrip_verify.py`, including an unresolved reference
  surviving a save and the behavior when the driver was deleted before the
  save.
- A scene with duplicate node names, to confirm deterministic resolution and
  the debug log.

## Future work

- [plans/geometry_graph/geometry_nodes.md](plans/geometry_graph/geometry_nodes.md) -
  uid-based reference persistence and a `mat4` output pin.
