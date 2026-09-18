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
API schemas of a prim: `rigid_body`, `joint`, `layout`, `grid`,
`frame_controller`, `draw_mode`. Each carries a key, a label, a stateless
`can_add(const Node&)` gate (a node holds at most one `Layout`, at most one
`Grid`, and so on; `joint` is the one kind a node may hold several of) and a
`make(Scene_commands&, Node&)` that queues the undoable operation.
`find_child_prim_type()` / `find_attachment_type()` resolve a key.

An attachment the user does not create stays out of the add catalog and
remains removable: `Brush_placement` comes from the brush placement flow,
`Geometry_graph_mesh` from dropping a `Graph_mesh` asset, `Rendertarget_mesh`
from the node-creating rendertarget command (its construction needs the
graphics device, the command buffer and the DPI).

## Operations

Removal needs no operation class of its own: `Node_attach_operation`
constructed with an empty host node is a pure, undoable detach, and
`Scene_commands::remove_attachment()` queues exactly that for ANY attachment,
catalog kind or not. `Node_physics` needs no special case - its detach
releases the rigid body from the physics world through the item-host update
hook, and an undo puts it back.

The additive half is `Scene_commands::attach_new_layout()` /
`attach_new_grid()` / `attach_new_frame_controller()` /
`attach_new_draw_mode()`, each a bare `Node_attach_operation` on the existing
node, plus `create_new_rigid_body()` / `create_new_joint()`, which the rigid
body and joint entries reuse.

Detaching a `Mesh` a `Geometry_graph_mesh` controls is a legal state: the pure
detach keeps the removed `Mesh` alive, so the bound graph mesh neither
recreates it nor writes visible output, and an undo re-attaches the same
`Mesh` object with its baked geometry intact.

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
`remove_node_attachment { node_id, attachment_id | type }` queues the remove
helper; `type` is the attachment type name `get_node_details` reports (e.g.
`Node_physics`), while `attachment_id` - also in `get_node_details` - removes
unambiguously.

## Verification

Headless, through `scripts/mcp_call.py` against the headless Vulkan build:
create an empty node, add each catalog key and assert through
`get_node_details` that the attachment appears, assert that a gated kind
refuses a second add, remove each and assert it is gone, and round-trip
undo / redo through `get_undo_redo_stack` (for a rigid body, with
`get_physics_items` before and after). The cases worth keeping: removing a
`Node_physics` from a node with live physics and undoing it returns the body
to the world; the `Geometry_graph_mesh` missing-Mesh tolerance; a clean
`capture_screenshot` after adding a light, a camera and a grid. Restore
`config/editor/desktop_window_imgui_host_imgui.ini` after a run.
