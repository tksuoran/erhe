# Issue #249: Node attachment management (plan)

https://github.com/tksuoran/erhe/issues/249

Goal: first-class UI for adding and removing `Node_attachment`s on a node.

- Properties window: an "Add Attachment" affordance offering attachment types
  the node does not yet have, and an "X" (remove) button on every existing
  attachment.
- Hierarchy window (Item_tree) context menu: "Add Attachment" submenu with
  entries for not-yet-existing attachment types, and "Remove Attachment"
  submenu listing existing attachments.

All add/remove actions must be undoable through the operation stack.

## Existing machinery (as of branch `crease`, 2026-07-04)

Everything needed for undo/redo already exists; this issue is UI + a small
factory layer.

- `erhe::scene::Node::attach/detach` (`src/erhe/scene/erhe_scene/node.hpp:88-89`),
  attachment list via `get_attachments()`, presence test via the free helper
  `get_attachment<T>(node)` (`node.hpp:134`).
- `editor::Node_attach_operation`
  (`src/editor/operations/node_attach_operation.{hpp,cpp}`): constructed with
  `(attachment, host_node)`. `execute()` detaches from the current node and
  attaches to the new one; `undo()` reverses. **Passing an empty
  `host_node` performs a pure, undoable detach** - removal needs no new
  operation class.
- `editor::Scene_commands` (`src/editor/scene/scene_commands.{hpp,cpp}`) is the
  canonical attachment factory: `create_new_camera/_light/_layout/
  _rendertarget/_rigid_body/_joint`. Today most of these create a *new* node
  and attach via `Compound_operation` (Item_insert_remove + Node_attach);
  `create_new_rigid_body`/`create_new_joint` already attach to an existing
  node and guard duplicates with `get_attachment<Node_physics>`
  (`scene_commands.cpp:791`).
- Item_tree context-menu extension point:
  `Item_tree::add_item_context_menu_callback`
  (`src/editor/windows/item_tree_window.hpp:54-70`); callbacks push deferred
  lambdas (run after the popup closes). `Scene_root` already registers one
  that builds a `Create` submenu and a minimal `Attach` submenu (Rigid Body,
  Joint) at `src/editor/scene/scene_root.cpp:386-463`. Extend that callback.
- Properties window already has a one-off precedent for an undoable add
  button: "Add Layout Item" at `src/editor/windows/properties.cpp:1595-1605`
  (queues a `Node_attach_operation`). The attachment iteration loop to hang
  the "X" buttons on is `properties.cpp:1580-1606`.
- Windows queue operations via
  `context.operation_stack->queue(std::make_shared<...>())`.

There is NO generic type-name -> attachment factory (scene serialization
constructs types by hand), so the "available attachment types" catalog must
be authored explicitly.

## Design

### Attachment type catalog

New small header in `src/editor/scene/` (e.g. `attachment_types.{hpp,cpp}`)
defining the user-addable attachment kinds, each with:

- display name ("Camera", "Light", "Rigid Body", ...)
- `can_add(const erhe::scene::Node&) -> bool` (duplicate / precondition gate)
- `make(Scene_commands&, erhe::scene::Node&)` which queues the undoable
  operation(s) via `Scene_commands`.

Included kinds and gates:

| Kind | Gate | Construction notes |
|---|---|---|
| Camera | no existing `Camera` | `erhe::scene::Camera(name)` |
| Light | no existing `Light` | `erhe::scene::Light(name)`, set `layer_id` from `scene_root->layers().light()->id` |
| Mesh (empty) | no existing `Mesh` | `erhe::scene::Mesh(name)`, no primitives; user adds geometry later |
| Rigid Body | no existing `Node_physics` | existing `create_new_rigid_body(Node*)` |
| Joint | none (multiple joints legal) | existing `create_new_joint(...)` |
| Layout | no existing `Layout` | `erhe::scene::Layout(name)` |
| Layout Item | parent node has `Layout` and node lacks `Layout_item` (same gate as the existing Properties button) | `erhe::scene::Layout_item(name)` |
| Grid | no existing `Grid` | `editor::Grid()` |
| Frame Controller | no existing `Frame_controller` | default-construct |

Excluded from the add catalog (still removable):
`Brush_placement` (tied to a Brush - created by brush placement flow),
`Geometry_graph_mesh` (created by drag-drop binding of a Graph_mesh asset),
`Rendertarget_mesh` (needs graphics_device/command_buffer/dpi; keep the
existing `create_new_rendertarget` node-creating command; can be added to the
catalog later if wanted).

### Scene_commands additions

Add attach-to-existing-node factory methods (bare `Node_attach_operation`,
no Compound/node creation):

- `attach_new_camera(Node&)`, `attach_new_light(Node&)`,
  `attach_new_empty_mesh(Node&)`, `attach_new_layout(Node&)`,
  `attach_new_layout_item(Node&)`, `attach_new_grid(Node&)`,
  `attach_new_frame_controller(Node&)`.
- Rigid body / joint reuse the existing `create_new_rigid_body/joint`.

Each resolves `Scene_root` via `node.get_item_host()` (pattern at
`scene_commands.cpp:441`).

### Removal

One shared helper (in `Scene_commands` or the catalog file):
`remove_attachment(const std::shared_ptr<Node_attachment>&)` that queues
`Node_attach_operation(attachment, {})`. Note: `Node_physics` detach already
releases the rigid body from the physics world via the item-host update hook;
verify this during testing rather than adding special cases.

### Properties window

In the node section (`properties.cpp:1580` area):

- Per attachment: render a small "X" button on the attachment's header line
  (aligned right, `ImGui::SameLine`), id-scoped by attachment id; clicking
  queues the remove helper. Defer the queue call until after the attachment
  loop (collect into a local pending action) so the list is not mutated
  mid-iteration.
- After the loop: an "Add Attachment" button opening
  `ImGui::BeginPopup`/`MenuItem` list built from the catalog, entries
  disabled (or hidden) when `can_add` fails.
- Delete the now-redundant one-off "Add Layout Item" button (subsumed by the
  catalog entry with the same gate).

### Hierarchy context menu

Extend the existing `Scene_root` callback (`scene_root.cpp:432-461`):

- Grow the existing `Attach` submenu (rename to "Add Attachment" for
  consistency with the issue wording, keep entries) with the full catalog,
  each entry disabled when `can_add` fails, each pushing a deferred lambda.
- Add a `Remove Attachment` submenu, shown only when the item is a `Node`
  with attachments: one entry per attachment, labeled
  `"<type_name> '<name>'"`, pushing a deferred remove.

Both submenus operate on the right-clicked node (the `item` argument), same
as the existing Create/Attach entries. ImGui popups are fine here (this is a
plain window context menu, not the ax::NodeEditor canvas).

### MCP tools (verification surface)

Add two tools to the in-editor MCP server (handler + dispatch +
`refresh_tool_list` schema, precedent: `create_physics_body` in
`mcp_server_physics.cpp:153`):

- `add_node_attachment { node_id, type }` - type is a catalog key; goes
  through the same `Scene_commands` path (undoable).
- `remove_node_attachment { node_id, attachment_id | type }` - queues the
  remove helper.

`get_node_details` already lists attachments for before/after assertions.

## Implementation steps

1. **Catalog + Scene_commands factories.** `attachment_types.{hpp,cpp}`,
   new `attach_new_*` methods, `remove_attachment` helper. Build editor.
2. **Properties window UI.** Per-attachment X + Add Attachment popup; remove
   the one-off Layout Item button. Build.
3. **Hierarchy context menu.** Extend the Attach submenu, add Remove
   Attachment submenu in `scene_root.cpp`. Build.
4. **MCP tools.** `add_node_attachment` / `remove_node_attachment`.
5. **Headless verification** (erhe-headless-verify loop, headless Vulkan
   build + `scripts/mcp_call.py`):
   - create an empty node; add each catalog type via MCP; assert via
     `get_node_details` that the attachment appears; assert duplicate-gated
     types refuse a second add.
   - remove each; assert gone; `get_undo_redo_stack` + undo/redo round-trip
     restores/removes correctly (including rigid body: `get_physics_items`
     before/after).
   - remove a `Node_physics` from a node with active physics and undo -
     verify body returns to the world.
   - `capture_screenshot` sanity after adding Light/Camera/Grid.
   - restore `config/editor/desktop_window_imgui_host_imgui.ini` after runs.
6. **Interactive polish pass** (user): context menu layout, X button
   placement, disabled-entry vs hidden-entry choice.

## Risks / open questions

- Strictly one `Camera`/`Light`/`Mesh` per node (decided): the add UI gates
  every catalog type that already exists on the node, i.e. all entries except
  `Joint` are single-instance.
- Removing a `Mesh` that a `Geometry_graph_mesh` controls is allowed
  (decided): a `Geometry_graph_mesh` on a node without a `Mesh` is a valid
  state - it simply cannot output until a Mesh exists again. Audit
  `Geometry_graph_mesh` so it tolerates a missing controlled Mesh gracefully
  (skip output, no recreate, no crash) and add this case to the headless
  verification (remove Mesh under a bound graph, assert no output and no
  crash, undo restores output).
- `Rendertarget_mesh` in the add catalog is deliberately deferred
  (construction-time resource needs; `App_context` access is fine at runtime
  but constructor args are heavyweight).
- Selection holding a removed attachment: `Node_attach_operation` already
  calls `selection->sanity_check()`; verify no stale-selection crash when the
  removed attachment was selected.

## Implementation status (as built)

Steps 1-5 are implemented and verified; step 6 (interactive polish) is left
to the user.

- Commits (branch `crease`): catalog + `Scene_commands` factories
  (`e86fcf90`), Properties + Hierarchy UI (`a978a565`), MCP tools
  (`1fd683be`).
- Catalog lives in `src/editor/scene/attachment_types.{hpp,cpp}` (function
  pointers, no heap): keys `camera light mesh rigid_body joint layout
  layout_item grid frame_controller`. All single-instance except `joint`.
- `remove_attachment` covers ANY attachment (including `Brush_placement`,
  `Geometry_graph_mesh`, `Rendertarget_mesh`), not just catalog kinds.
- The `Geometry_graph_mesh` missing-Mesh case needed NO code change: a pure
  detach (`Node_attach_operation` with an empty host node) keeps the removed
  Mesh alive, so the bound graph mesh neither recreates nor writes visible
  output, and undo re-attaches the same Mesh object with its baked geometry
  intact. Confirmed by the sweep.
- Verified headless (Vulkan) via `scripts/mcp_call.py`, 63/63 checks: per-type
  add / duplicate-reject / remove / undo / redo, rigid-body live-detach +
  undo (no crash, body returns), the `layout_item` parent-layout gate, the
  `Geometry_graph_mesh` missing-Mesh tolerance, and a clean `capture_screenshot`.
- Removal by `type` accepts the attachment type name reported by
  `get_node_details` (e.g. `Node_physics`, not the `rigid_body` add-key);
  `attachment_id` (now included in `get_node_details`) removes unambiguously.
- The Properties `Add Attachment` joint entry constrains to the world; the
  Hierarchy entry keeps the connect-to-first-selected-node behaviour.
