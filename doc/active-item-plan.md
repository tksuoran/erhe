# Active item plan: one explicit reference item in the selection

Status: LANDED (phases 1-4) and verified interactively by the user
(2026-09-15). Modeled on Blender's active object
(`scene_layout/object/selecting.rst`; the semantics below were read off
Blender's source: `view3d_select.cc` pick code, `object_select.cc`
`base_activate`, `object_relations.cc` `parent_set_exec`,
`overlay_private.hh` `object_wire_theme_id`). Builds on the per-scene
selection and the active scene of `doc/selection-improvements-plan.md`.

## 1. Scope

This document is the standing description of the active item: what it is,
how it changes, how it is shown, which commands take it as their reference,
and how it is exposed over MCP. `editor::Selection` keeps an ordered
`std::vector` of selected items; the active item is the one explicit
reference item beside it, replacing the per-consumer rules (a per-type
"last selected" map, "first item of a type in selection order",
`entries.front()`) that used to pick the reference invisibly.

## 2. Design

### D1. Definition

The **active item** is one `erhe::Item_base` tracked by `Selection`, editor-wide
(the union selection is editor-wide; the active scene is a separate tracked
state, see `doc/selection-improvements-plan.md`). It is the item most recently
selected or explicitly activated, and it is independent of the selection
flag: deselecting it, clearing the selection, or clicking empty space leaves
it active (Blender: `basact` is a pointer beside `BASE_SELECTED`; only
`base_activate` writes it, and `Select > None` and empty-space clicks skip
activation). At most one item is active at any time; there may be none.

The item's own `active` property (`Item_base::active_property`, the USD prim
`active` metadata, derived bit `Item_flags::active`) is a different concept and
keeps its name. This plan's identifiers say `active_item` everywhere:
`Selection::get_active_item()`, `Item_flags::active_item`,
`Active_item_changed_message`, MCP field `active_item`.

### D2. State and lifetime

- `Selection` holds `std::weak_ptr<erhe::Item_base> m_active_item`.
- `Item_flags::active_item` is bit 41 (`count` becomes 42), listed in
  `Item_flags::transient`, and written only by `Selection` through
  `set_flag_bits`. The bit reaches draw-list entries through the existing
  `Mesh::handle_flag_bits_update` -> `Scene_root::on_mesh_flags_changed` ->
  `enqueue_set_flags` path, so rendering needs no new plumbing.
- Because the active item can be outside the selection, its lifetime is
  handled on its own, in exactly these places: `on_items_removed` (the
  `removed.lookup` test, next to the per-type map), `clear_selection(Item_host*)`
  when called for a closing scene (the active item hosted by that scene is
  forgotten; a clear for any other reason keeps it), and the MCP
  `reset_editor_state` tool. Forgetting means: bit cleared, weak reset,
  message sent. No successor is promoted (Blender re-activates a surviving
  base after Delete; erhe leaves no active item, and the next selection sets
  one). The scene-close leak watchdog covers the reference.
- `Item_insert_remove_operation` selection snapshots record the active item
  beside the selection vector and restore both, since restoring the vector
  no longer implies the active item.

### D3. Sources of change (closed list)

1. `Selection::add_to_selection(item)` makes `item` active. This covers the
   viewport click and Ctrl-click, the hierarchy click and Ctrl-click, and MCP
   `select_items` (whose last listed item therefore becomes active).
2. `Selection::set_selection(items, active)` takes the active item
   explicitly; `set_selection(items)` uses the last element.
   `Range_selection::end` passes the secondary terminator (the item the user
   clicked last).
3. `Selection::set_active_item(item)` sets it directly; `item` may be
   unselected (D1). Used by the Ctrl-click rule below, by undo snapshot
   restore and by the MCP tool.
4. Ctrl-click on a selected item that is not active makes it active and
   leaves the selection unchanged; Ctrl-click on the active selected item
   deselects it and it stays active (Blender `SEL_OP_XOR`,
   `view3d_select.cc`). Applies in the viewport
   (`Selection::toggle_mesh_selection`) and the hierarchy
   (`Item_tree::item_update_selection`, ctrl branch).
5. `remove_from_selection`, `clear_selection()`, the empty-space click and
   `Selection_tool::handle_priority_update` leave the active item as it is.

### D4. Notification

`App_message_bus::active_item` carries `Active_item_changed_message{old, new}`
(weak pointers). `Selection` sends it from `end_selection_change` after the
`Selection_message`, and directly from `set_active_item` when no selection
change is open, so subscribers always observe the active item and the
selection in their final state. Consumers that cache a resolved reference
follow the scene-close and items-removed rules of AGENTS.md ("Scene-hosted
references in editor parts").

### D5. Presentation

- Viewport outline: `Selection_outline_style` (version 2) adds
  `active_highlight_low` / `active_highlight_high` (Vec4; defaults a lighter
  yellow than the selected orange, Blender's convention). The composition pass
  feeds them as a third constant color of `Primitive_interface_settings`, and
  `primitive_buffer.cpp` picks it when an entry's flags hold both `selected`
  and `active_item`. An active item that is not selected draws no outline
  (Blender: `TH_ACTIVE` only when `BASE_SELECTED`). The Settings window shows
  the new fields with the existing ones.
- Hierarchy row: the active item's row draws an accent whether or not it is
  selected (Blender's outliner: `TH_SELECT_ACTIVE` on the active row): a
  brighter `ImGuiCol_Header` while selected, a tinted label while not. This
  is the one place that shows an unselected active item.
- Properties window "Individual" mode lists the active item first.

### D6. Consumers (closed list of migrations)

Commands follow Blender's split: the **reference** is the active item, the
**operands** are the selected items (`parent_set_exec` parents
`selected_editable_objects` to `context_active_object`, excluding it; mesh
join targets `CTX_data_active_object`). The active item is a command's
reference when it is non-hosted or hosted by the active scene; an active item
hosted by another scene is treated as no active item, matching the command
target scoping of `doc/selection-improvements-plan.md`. Whether the active
item is itself selected does not matter.

Each consumer resolves its reference through one helper,
`Selection::get_active_item_as<T>()` (the active item when it is of type `T`,
or the node an active attachment belongs to, subject to the scene rule
above, else empty), and falls back to the previous rule only when the helper
returns empty:

| Consumer | Reference rule |
|----------|----------------|
| `Tool::get_node` | active node (or active attachment's node), else first hosted node of the command target selection |
| `Brush_tool` "Parent to Active" | active node |
| `Operations::can_flip_joint` / `flip_joint` | active node |
| `Operations::create_brush` | active mesh, else first selected mesh |
| `Scene_commands::create_new_rigid_body` / `create_new_joint` | target = active node; `create_new_joint` connects to another selected node of the same host as today |
| `Clipboard::resolve_paste_target` | active hierarchy item, else the current fallbacks |
| `Create::find_parent` | active node |
| Operations "Attach" (`Operations::attach_selection_to_active`, also the MCP tool `attach_selection_to_active`) | every selected node of the command target selection other than the active node is parented under the active node in one compound operation (Blender Ctrl-P); nodes that are ancestors of the active node are skipped with a warning; enabled with an active node and at least one other selected node |
| `Merge_operation` and the CSG booleans | `Operations::resolve_operation_items` puts the active mesh node first in the item list, and both take the front item as their target; under `Operation_reference::active_is_target` (merge, difference, intersection, union) an unselected active mesh is inserted as the target (Blender join), while per-mesh operations (`operands_only`) act on the selected operands alone |
| `Transform_tool` representative entry (local reference frame, single-entry numeric edit, IK effector) | `update_target_nodes` rotates the active node's target to the front, so `entries.front()` is the active node when it is among the targets, else the first target; the anchor orientation is that entry's; the tool rebuilds on `Active_item_changed_message` outside component mode and outside a drag |

### D7. The per-type map after the migration

`m_last_selected_by_type` / `get_last_selected<T>()` remain for exactly the
library palette types Material and Brush (`get_default_material`,
`Brush_tool` brush fallback, `Operations` make-mesh material): those answer
"which material / brush is current", a question the single active item does
not answer once the user clicks a node. Every hierarchy-typed use (Node, Mesh,
Node_attachment, Hierarchy) moves to the active item, and the map is written
only for Material and Brush.

### D8. MCP

- `get_selection` reports `active_item` `{name, type, id, scene_name, selected}`.
- `select_items` makes the last listed item active; an optional `active`
  argument (id or path) names another one of the listed items.
- New tool `set_active_item` `{scene_name, id | path}` calls
  `Selection::set_active_item` (D3.3); the item need not be selected.
- `get_editor_references` reports the active item.
- `reset_editor_state` clears it (D2).

## 3. Verification

Headless (`build_vs2026_vulkan_headless`, tests ON):

- `ctest -C Debug -R "Mcp_"` includes `Mcp_test.active_item_follows_selection_and_survives_clearing`
  (last listed item is active; an empty `select_items` keeps it with
  `selected: false`; `set_active_item` on an unselected item; `reset_editor_state`
  forgets it). The first `Mcp_test` case after a fresh editor can time out on
  `reset_editor_state`; that is a startup flake unrelated to the active item.
- `scripts/undo_reference_clearing_smoke_test.py`: the active item is
  forgotten with removed content and on scene close (no `scene-close leak`).
- Outline: `capture_screenshot` with two selected boxes shows the active one
  in the active color and the other in the selected color; after `select_items`
  with an empty list neither draws an outline.
- Commands over MCP: with an unselected active mesh, the `Geometry.Merge`
  command leaves the active mesh as the survivor, `Geometry.Difference` keeps
  the active mesh, `attach_selection_to_active` parents the other selected
  nodes under it, `Geometry.Subdivision.Catmull-Clark` leaves it untouched,
  and `get_transform_state.anchor_frame` follows `set_active_item`.

Interactive (no bare MCP entry reaches these; re-check by hand after
changes to selection, the hierarchy window or the transform tool):

- Ctrl-click on a selected, non-active row or mesh makes it active without
  deselecting; Ctrl-click on the active one deselects it and it stays active.
- The hierarchy accent on an unselected active row, and the two active
  color fields in the Settings window.
- Create window parent, Brush tool "Parent to Active", Ctrl-V paste target
  and the local-mode gizmo axes with an unselected active node.

## 4. Follow-ups this enables (not in scope)

Blender's Select menu entries that key off the active object: select
children / parent / siblings of the active item, select all of the active
item's type, select items sharing the active item's material or mesh.
