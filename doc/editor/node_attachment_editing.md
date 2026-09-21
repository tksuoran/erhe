# Node attachment editing

Stability: stable

Adding and removing `erhe::scene::Node_attachment`s on a node from the
Properties window, the Hierarchy context menu and MCP. Every add and every
remove is undoable through the operation stack.

## The two catalogs

`src/editor/scene/attachment_types.{hpp,cpp}` holds both catalogs as static
tables of function pointers (no heap, no per-entry state).

`Child_prim_type_info` lists the user-creatable typed child prims - `mesh`,
`camera`, `light` - each with a stable key, a menu label and a
`make(Scene_commands&, erhe::Hierarchy& parent)` that queues the undoable
insert of a new prim as the parent's last child. Any prim parents any prim
(`doc/erhe/usd_compatibility_design.md` C5), so these take a parent rather than a
node and a parent takes any number of them.

`Attachment_type_info` lists the user-addable attachment kinds - the applied
API schemas of a prim: `joint`. Each carries a key, a label, a stateless
`can_add(const Node&)` gate (`joint` is a kind a node may hold several of, so
its gate always admits) and a `make(Scene_commands&, Node&)` that queues the
undoable operation. A rigid body is no longer one of them: it is the
`Node_physics.*` values of the node, added through Add Property and the
Properties window's Rigid Body group (`doc/erhe/property_system.md`
section 4.26).
`find_child_prim_type()` / `find_attachment_type()` resolve a key.

An attachment the user does not create stays out of the add catalog and
remains removable: `Rendertarget_mesh` comes
from the node-creating rendertarget command (its construction needs the
graphics device, the command buffer and the DPI).

## Operations

Removal needs no operation class of its own: `Node_attach_operation`
constructed with an empty host node is a pure, undoable detach, and
`Scene_commands::remove_attachment()` queues exactly that for ANY attachment,
catalog kind or not.

Detaching a `Mesh` a node's geometry graph controls is a legal state: the pure
detach keeps the removed `Mesh` alive, so the bound graph mesh neither
recreates it nor writes visible output, and an undo re-attaches the same
`Mesh` object with its baked geometry intact
(`doc/editor/geometry_graph_mesh.md`).

## User interface

**Properties window.** Each attachment of the inspected node gets its own
section, with an "X" button scoped by the attachment's id that queues the
remove. The queue runs on the next frame, so the attachment list is not
mutated while it is being iterated.

**Hierarchy context menu.** `Scene_root` registers item context-menu
callbacks. "Create" lists the child prim catalog beside the other creatable
kinds and inserts the new item as the last child of the clicked prim. "Add
Attachment" lists the attachment catalog, each entry disabled when its
`can_add` gate refuses. "Remove Attachment" appears only when the node has
attachments, one entry per attachment labeled `"<type name> '<name>'"`. Every
entry pushes a deferred lambda that runs after the popup closes. The joint
entry keeps its own behavior instead of the catalog `make`: it connects to the
first selected node of the same scene other than the clicked one, when there
is one.

**MCP.** `add_node_attachment { node_id, type }` accepts a key of either
catalog and goes through the same `Scene_commands` path, so it is undoable.
Its schema in `config/editor/mcp_tools.json` advertises the same key list the
catalog holds, so a schema-validating client can reach every kind.
`remove_node_attachment { node_id, attachment_id | type }` queues the remove
helper; `type` is the attachment type name `get_node_details` reports (e.g.
`Prefab_instance`), while `attachment_id` - also in `get_node_details` - removes
unambiguously.

## Verification

Headless, through `scripts/mcp_call.py` against the headless Vulkan build:
create an empty node, add each catalog key and assert through
`get_node_details` that the attachment appears, assert that a gated kind
refuses a second add, remove each and assert it is gone, and round-trip
undo / redo through `get_undo_redo_stack`. The cases worth keeping: clearing
`Node_physics.motion_mode` on a node with live physics and undoing it returns
the body to the world; the geometry-graph missing-Mesh tolerance; a clean
`capture_screenshot` after adding a light, a camera and a grid. Restore
`config/editor/desktop_window_imgui_host_imgui.ini` after a run.
