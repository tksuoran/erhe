# Selection

Stability: stable

`editor::Selection` (`src/editor/tools/selection_tool.{hpp,cpp}`) is the single
mutation API, event source and coordinator for what is selected. Several scenes
can be open at once, so the selection is **scoped by host**: selecting in one
scene leaves the other scenes' selections intact, and a command acts on exactly
one scene's items.

The one reference item inside a selection - the active item - has its own rules,
its own message and its own undo behavior; `doc/editor/active_item.md` owns them.

## Storage and host views

`Selection` keeps one authoritative union, `get_selected_items()`, which
per-item consumers (the Properties window, per-item operations) read as they
always have. Two filtered views are derived from it, each clearing and refilling
a retained-capacity scratch at call time, so a host change while an item is
selected - a cross-scene reparent - can never leave a stale view:

- `get_hosted_selection(Item_host*)` - the selected items that host hosts;
  `nullptr` is the bucket of items with no host, such as content-library
  entries.
- `get_command_target_selection()` - what commands act on: the active scene's
  items plus the non-hosted ones.

`is_hosted_or_defined_by()` is the "belongs to this host" test the scoping uses:
prim hosting, extended with the asset manager's defining-container lookup, so a
selected resource another container defines counts as held by this scene's tree
and leaves the selection when the scene closes.

`clear_selection(Item_host*)` removes one host's items and leaves the others
alone; `clear_selection()` clears everything. The host-scoped form takes an
`Active_item` argument saying what happens to the active item (see
`doc/editor/active_item.md` D2): `keep` for a plain click or an MCP `select_items`
reset, `forget_hosted` for a closing scene.

## Scoped selection semantics

- A plain click in a viewport or a hierarchy window clears only that scene's
  items before selecting; other scenes keep their selections.
- Ctrl-click toggles within that scene, so a deliberate multi-scene selection
  is still possible - inspecting items of two scenes side by side is a
  supported use case.
- Ctrl-A in a hierarchy window selects everything in that window's scene only.
  A content-library tree scopes to its own root the same way. The chord is
  routed by `ImGui::Shortcut()` once per frame to the focused tree window, and
  the whole subtree enters the selection in one `set_selection()` call, so a
  tree of thousands of items costs one selection change and one
  `Selection_message`.
- `Range_selection` (shift-range) is host-scoped: `reset(Item_host*)` collapses
  a range only when a terminator belongs to that host, and
  `reset_terminators_for_host()` drops the terminators without the
  selection-clearing side effect, which is what `clear_selection(host)` uses.
  A range running in another scene's tree is never cancelled by work in this
  one.
- A plain click on empty space in a viewport deselects within the hovered
  scene only; other scenes and the non-hosted items keep their selection.
  With no hovered scene it clears everything.

## The active scene

The scene a command acts on is explicit tracked state, not a heuristic
recomputed per call site: `get_active_scene_root()` /
`set_active_scene_root()`, with an `Active_scene_changed_message` so tools and
UI react. It changes on deliberate acts and never on hover alone, which would
flicker as the pointer crosses a viewport on its way elsewhere:

- a selection change in a scene makes that scene active;
- focusing a scene's viewport or hierarchy window makes that scene active;
- with nothing explicitly active the getter falls back to the last hovered
  scene view's scene, then to the single open scene.

Commands that target scene-hosted items - delete, cut, duplicate, merge,
booleans, mesh operations, deselect-all - take
`get_command_target_selection()`. That gives the single-lock property for free:
one scene's items means one `item_host_mutex` to hold, instead of locking the
first selected node's host and then mutating nodes of other scenes without
theirs. Read-only UI keeps showing the union.

`push_active_scene_window_tint()` (`windows/active_scene_highlight.{hpp,cpp}`)
is the UI half: a window showing the active scene draws its title bar and its
dock tab tinted toward the accent color, so which scene a command will hit is
visible in either layout.

## Transform gizmo

The gizmo binds to the active scene rather than to a view. Its
`shared.entries` and `world_from_anchor` are read outside rendering too -
numeric edits, MCP `transform_selection`, undo recording - so making them
view-dependent within a frame would make those consumers depend on render
order.

`Transform_tool::update_target_nodes()` builds the entries and the anchor from
the active scene's items and rebinds on `Active_scene_changed_message` as well
as on selection changes. `update_for_view()` runs once per rendered view and
tells `Handle_visualizations` whether that view shows the active scene, so the
handle meshes - which live in the shared Tools scene root and would otherwise
appear in every viewport at the same world position - are shown only there.
`update_hover` reports no handle hover and the drag command refuses in views of
other scenes, and `tool_render` draws nothing into them. A drag therefore moves
one scene's nodes under one lock, by construction.

## MCP

MCP mirrors the user experience exactly: `get_active_scene` / `set_active_scene`
are explicit tools, `select_items` follows the same activation and scoped-clear
rules as a UI selection, `get_selection` reports each item's scene and the
active scene, and the command-like tools (`transform_selection`, the mesh
operations, the delete-style actions) target the active scene. There is no
MCP-only per-call scene override: a script that wants another scene calls
`set_active_scene` first, just as a user would click that scene's window.
Headless verification therefore exercises the same code paths as an interactive
user.
