# Child prim creation

Stability: stable

Creating a typed child prim under any prim from the Hierarchy context menu
and MCP. Every creation is undoable through the operation stack.

## The catalog

`src/editor/scene/child_prim_types.{hpp,cpp}` holds the catalog as a static
table of function pointers (no heap, no per-entry state).

`Child_prim_type_info` lists the user-creatable typed child prims - `mesh`,
`camera`, `light`, `joint` - each with a stable key, a menu label and a
`make(Scene_commands&, erhe::Hierarchy& parent)` that queues the undoable
insert of a new prim as the parent's last child. Any prim parents any prim
(`doc/erhe/usd_compatibility_design.md` C5), so these take a parent rather
than a node and a parent takes any number of them.
`find_child_prim_type()` resolves a key.

The per-node features that are not prims - the rigid body, the draw mode, the
layout, the brush placement and the geometry graph binding - are attached
value groups of the node, added through Add Property and shown in the
Properties window as their own groups (`doc/erhe/property_system.md`
section 4.23). The
Hierarchy row of a node carrying one shows that group's feature icon,
right-aligned (`doc/editor/windows.md`).

## User interface

**Hierarchy context menu.** `Scene_root` registers item context-menu
callbacks. "Create" lists the child prim catalog beside the other creatable
kinds and inserts the new item as the last child of the clicked prim. Every
entry pushes a deferred lambda that runs after the popup closes. The joint
entry keeps its own behavior instead of the catalog `make`: it connects to
the first selected node of the same scene other than the clicked one, when
there is one.

**MCP.** `create_child_prim { node_id, type }` accepts a catalog key and goes
through the same `Scene_commands` path, so it is undoable. Its schema in
`config/editor/mcp_tools.json` advertises the same key list the catalog
holds, so a schema-validating client can reach every kind. A creation under
a reference instance is refused (`doc/erhe/usd_compatibility_design.md` X2).

## Verification

Headless, through `scripts/mcp_call.py` against the headless Vulkan build:
create an empty node, create each catalog key under it and assert through
`get_scene_nodes` that the child prim appears, then round-trip undo / redo
through `get_undo_redo_stack`. The cases worth keeping: clearing
`Node_physics.motion_mode` on a node with live physics and undoing it returns
the body to the world; a clean `capture_screenshot` after creating a light
and a camera. Restore
`config/editor/desktop_window_imgui_host_imgui.ini` after a run.
