# Selection improvements plan: multi-scene correctness

Status: draft, not started.

## Problem

`editor::Selection` (`src/editor/tools/selection_tool.hpp`) holds a single
global `std::vector<std::shared_ptr<erhe::Item_base>>`. When multiple scenes
are open, the user can build a selection that spans scenes, and the selection
may also contain non-scene items (content library materials / brushes, items
with no `Item_host`). Several consumers assume all selected items live in one
scene, most visibly the transform gizmo.

### How cross-scene selections are produced today

- Ctrl-click in a second scene's Hierarchy window: `Item_tree::item_update_selection`
  (ctrl branch) adds without clearing. There is one hierarchy window per
  `Scene_root` (`Scene_root::make_browser_window`).
- Ctrl-A: `Item_tree::select_all` iterates ALL `App_scenes::get_scene_roots()`
  and selects every node in every open scene.
- Ctrl-click in a viewport: `Selection::on_viewport_select` /
  `toggle_mesh_selection(clear_others=false)` toggles the hovered mesh without
  touching items selected from other scenes' viewports.
- Undo/redo restores selection snapshots (`Item_insert_remove_operation`),
  replaying whatever was legal when recorded.

### Consumers that break or misbehave on a cross-scene selection

| Consumer | Failure |
|----------|---------|
| `Transform_tool::update_target_nodes` (`src/editor/transform/transform_tool.cpp`) | Averages `world_from_node` across ALL selected nodes; different scenes have unrelated world spaces, so the anchor is meaningless. A drag then transforms nodes in scenes not shown in the viewport being dragged in. |
| Gizmo visibility (`Handle_visualizations`) | Handle meshes live in the shared Tools scene root, rendered as overlay in every viewport, so the gizmo appears in viewports of unrelated scenes at the same world position. |
| MCP `transform_selection` (`mcp_server_scene_action.cpp`) | Operates on the same `shared.entries`; inherits all of the above. |
| `mesh_operation.cpp`, `geometry_operations.cpp` (booleans), `items.cpp` `async_for_nodes_with_mesh` | Lock only the FIRST node's `item_host_mutex`, then mutate/read nodes from other scenes without their lock (thread-safety hole vs async workers). Boolean/merge ops also compose world transforms across scenes. |
| `merge_operation.cpp` | Takes scene root and reference frame from the first mesh; detaches other-scene nodes into the first scene. |
| `Fly_camera_tool` frame-selection | Unions world-space AABBs across scenes into one bounding box, then frames one viewport's camera on it. |
| `Scene_commands::get_scene_root(Node*)` (+ Material overload) | "Current scene" = host of whichever qualifying item happens to be first in the selection; ambiguous when the selection spans scenes. |
| `Item_tree` shift-range across two hierarchy windows | `Range_selection` is global but entries are fed per-window per-frame, so a range whose terminators are in different windows mis-toggles (garbled partial selection), independent of the scene question. |

### Non-scene items

Most consumers correctly skip non-`Node` items via `dynamic_pointer_cast` /
`Item_filter`. The sharp edge: `async_for_nodes_with_mesh` (`operations/items.cpp`)
derives the item host from the FIRST selected item and returns early when it has
no host - a material first in the selection silently aborts the whole mesh
operation even when mesh nodes are selected.

### Existing correct patterns to build on

- `Scene_root`'s `Selection_message` subscriber filters by
  `item->get_item_host() == this` (physics acquire/release) - the per-scene
  self-filtering model.
- `Debug_visualizations` and `Scene_views::choose_camera_for_scene` filter by
  `node->get_scene() == <render/viewport scene>`.
- Scene close (`editor.cpp`) removes only the closing scene's items from the
  selection.
- Active-scene candidates already exist: `Scene_views::hover_scene_view()`
  (pointer currently over a viewport), `Scene_views::last_scene_view()`
  (persists after pointer leaves), `App_scenes::get_single_scene_root()`
  (unambiguous only when one scene is open).

## Design

### Core invariant: single-scene selection

At any time, all selected items that have a non-null `Item_host` share ONE
host (the "selection scene"). Non-scene items (null host) may coexist freely
with them; consumers already filter by type.

Enforcement lives in `Selection` itself, not in every producer:
`add_to_selection` and `set_selection` detect an incoming hosted item whose
host differs from the current selection scene and first remove all items of
the old host (through the normal `begin/end_selection_change` flow, so
`Selection_message` subscribers - physics, transform tool - see correct
removals). Selecting in another scene therefore replaces the previous scene's
selection, which matches multi-document editor convention. Producers are then
simplified rather than guarded one by one.

Rationale for "replace" over "reject": rejecting would make ctrl-click in a
second scene's window silently do nothing (confusing); replacing is the
behavior every user already expects from single-scene editors.

`Selection` exposes the invariant:

```cpp
// Host shared by all currently selected hosted items; nullptr when the
// selection has no hosted items.
[[nodiscard]] auto get_selection_scene_root() const -> Scene_root*;
```

This accessor replaces the first-item heuristics in
`Scene_commands::get_scene_root` and friends (with the invariant they agree,
but the accessor states intent and is O(1)).

### Per-scene selection memory: deferred (Phase 5, optional)

A "true" per-scene selection (each `Scene_root` remembers its selection;
selecting in scene B stashes scene A's selection and restores it when the user
returns to scene A) is a UX refinement layered ON TOP of the invariant, not a
prerequisite for correctness. It touches message semantics (do subscribers see
stash/restore as select/deselect? physics acquire/release says yes), undo, and
MCP `get_selection` semantics. Deferred until the invariant has proven itself;
the invariant does not paint us into a corner because stash/restore reuses the
same replace path.

### Transform gizmo scoping

With the invariant, `shared.entries` are all in one scene and the averaged
anchor is meaningful again. Remaining gizmo work:

- The gizmo should be visible and hoverable only in viewports whose
  `Scene_root` matches `get_selection_scene_root()`. The handle meshes live in
  the shared Tools scene, so this needs a per-viewport check rather than global
  mesh visibility flags - investigate whether `Tools::render_viewport_tools` /
  the tool render passes can skip the transform-tool meshes per `Render_context`,
  or whether hover alone should be gated (drag readiness) with the visuals
  filtered at render time. Mesh-component mode (`component_mode`) needs the
  same scoping via the component's mesh host.
- `Transform_tool_drag_command::try_ready` must reject when the hovered
  viewport's scene differs from the selection scene (belt and braces even once
  visuals are filtered).

## Phases

### Phase 1: Selection core invariant

1. Add selection-scene tracking to `Selection` (`selection_tool.{hpp,cpp}`):
   `get_selection_scene_root()`, and host-change handling in
   `add_to_selection` / `set_selection` (remove old-host items via the
   selection-change flow before adding).
2. Scope `Item_tree::select_all` to the window's own root (each hierarchy
   window selects its scene only; content library tree its own root). Ctrl-A
   in a hierarchy window = select all in THAT scene.
3. `Range_selection`: reset terminators when the selection scene changes /
   when a terminator from a different tree is set, fixing the cross-window
   shift-range mis-toggle.
4. MCP `select_items`: with the invariant, a mixed-scene id list resolves to
   last-writer-wins; make it an explicit error instead (report which items
   conflicted) so scripts do not silently lose selection.
5. Audit undo selection snapshots: snapshots recorded post-invariant are
   single-scene by construction; add a `sanity_check` assertion for the
   invariant (Selection::sanity_check already exists).

Verification (headless MCP): open/create two scenes, `select_items` in scene A,
then `select_items` a node in scene B -> `get_selection` shows only the scene B
node; ctrl-style additive selects within one scene still accumulate.
Interactive verification by user: ctrl-click across two hierarchy windows,
Ctrl-A, viewport ctrl-click across two viewports.

### Phase 2: Transform gizmo scoping

1. `update_target_nodes`: with the invariant in place, add a debug-time check
   (log or VERIFY) that all entries share one host, documenting the invariant
   at the consumer.
2. Per-viewport gizmo visibility: hide/skip transform-tool handle meshes when
   rendering a viewport whose scene root differs from
   `get_selection_scene_root()` (investigation task above decides the
   mechanism). Same for the bounding-box scale gizmo and anchor "temp node"
   visuals.
3. Gate `Transform_tool_drag_command::try_ready` (and box-face hover) on
   hovered-viewport scene == selection scene.
4. Mesh-component mode: scope via the component selection's mesh host.

Verification (headless MCP): two scenes in two viewports, select a node in
scene A, `capture_screenshot` both viewports -> gizmo visible only in scene A's
viewport; `transform_selection` still works; drag readiness in scene B's
viewport rejected.

### Phase 3: Consumer cleanup and latent bug fixes

1. `operations/items.cpp` `async_for_nodes_with_mesh`: derive the host from the
   first item that actually is a mesh node (not the first item of any type);
   fixes the leading-material silent abort. With the invariant, one host then
   covers all mesh nodes; assert that.
2. `mesh_operation.cpp`, `geometry_operations.cpp`, `merge_operation.cpp`:
   replace first-item host derivation with `get_selection_scene_root()` and
   assert all participating nodes share it (the single `item_host_mutex` lock
   is then correct by construction).
3. `Scene_commands::get_scene_root(Node*)` and the Material overload: use
   `get_selection_scene_root()` instead of scanning the selection; keep the
   hover-viewport / single-scene fallbacks.
4. `Fly_camera_tool` frame-selection: only frame when the viewport's scene ==
   selection scene (skip otherwise); AABB union is then single-scene.
5. `Clipboard::resolve_paste_target`: null-check `m_last_hover_scene_view`
   before `get_scene_root()` (unchecked deref found in audit).
6. Sweep remaining first-item consumers (grid attach, brush tool parent,
   animation keying) to filter by selection scene where it matters; most
   become correct automatically via the invariant.

Verification: gtest does not reach editor level; use headless MCP smoke: merge
and boolean ops on a two-scene session, mesh op with a material selected first
plus mesh nodes (previously aborted, must now run).

### Phase 4: Scene lifecycle polish

1. Scene close already removes its items from the selection; verify the
   selection-scene tracking resets correctly and the gizmo hides.
2. Decide behavior when the selection scene's LAST item is deselected while
   non-scene items remain selected (selection scene becomes null; gizmo hides).

### Phase 5 (deferred, needs separate go-ahead): per-scene selection memory

Stash/restore selection per `Scene_root` on selection-scene change, so
returning to a scene restores what was selected there. Design questions to
settle first: message semantics for stash/restore, undo interaction, MCP
`get_selection` reporting, and whether inactive-scene selections stay
highlighted in their hierarchy windows.

## Open questions

- Replace-on-cross-scene-select is the recommended and planned behavior; if
  rejecting (selection locked to a scene until explicitly cleared) is
  preferred, Phase 1 step 1 changes but the rest of the plan stands.
- Should materials/brushes hosted by a scene's content library count as
  "hosted" for the invariant (selecting scene A's material + scene B's node
  replaces)? Planned: yes - one `Item_host` rule for everything with a host,
  which keeps `Scene_commands::get_scene_root`'s material overload coherent.
