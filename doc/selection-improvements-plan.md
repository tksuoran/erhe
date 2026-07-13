# Selection improvements plan: per-scene selection

Status: draft, not started.

## Problem

`editor::Selection` (`src/editor/tools/selection_tool.hpp`) holds a single
global `std::vector<std::shared_ptr<erhe::Item_base>>`. When multiple scenes
are open, the selection can span scenes, and it may also contain non-scene
items (content library materials / brushes, items with no `Item_host`).
Several consumers assume all selected items live in one scene, most visibly
the transform gizmo.

Multi-scene selections have legitimate use cases (e.g. inspecting properties
of items from two scenes, cross-scene copy), and silently dropping a scene's
selection when the user clicks in another scene would be bad UX. So the goal
is NOT to forbid multi-scene selection; it is to make selection per-scene:
each `Scene_root` contains the selection of items hosted by it, selecting in
one scene leaves other scenes' selections intact, and single-scene consumers
(the transform gizmo first among them) operate on exactly one scene's
selection instead of the global union.

### How cross-scene selections are produced today

- Ctrl-click in a second scene's Hierarchy window: `Item_tree::item_update_selection`
  (ctrl branch) adds without clearing. There is one hierarchy window per
  `Scene_root` (`Scene_root::make_browser_window`).
- Ctrl-A: `Item_tree::select_all` iterates ALL `App_scenes::get_scene_roots()`
  and selects every node in every open scene.
- Ctrl-click in a viewport: `Selection::on_viewport_select` /
  `toggle_mesh_selection(clear_others=false)` toggles the hovered mesh without
  touching items selected from other scenes' viewports.
- Undo/redo restores selection snapshots (`Item_insert_remove_operation`).

Conversely, a plain (non-ctrl) click ANYWHERE clears the whole global
selection across all scenes - the "losing selection" UX problem this plan
removes.

### Consumers that misbehave on a cross-scene selection (audit)

| Consumer | Failure |
|----------|---------|
| `Transform_tool::update_target_nodes` (`src/editor/transform/transform_tool.cpp`) | Averages `world_from_node` across ALL selected nodes; different scenes have unrelated world spaces, so the anchor is meaningless. A drag then transforms nodes in scenes not shown in the viewport being dragged in. |
| Gizmo visibility (`Handle_visualizations`) | Handle meshes live in the shared Tools scene root, rendered as overlay in every viewport, so one gizmo appears in viewports of unrelated scenes at the same world position. |
| MCP `transform_selection` (`mcp_server_scene_action.cpp`) | Operates on the same `shared.entries`; inherits all of the above. |
| `mesh_operation.cpp`, `geometry_operations.cpp` (booleans), `items.cpp` `async_for_nodes_with_mesh` | Lock only the FIRST node's `item_host_mutex`, then mutate/read nodes from other scenes without their lock (thread-safety hole vs async workers). Boolean/merge ops also compose world transforms across scenes. |
| `merge_operation.cpp` | Takes scene root and reference frame from the first mesh; detaches other-scene nodes into the first scene. |
| `Fly_camera_tool` frame-selection | Unions world-space AABBs across scenes into one bounding box, then frames one viewport's camera on it. |
| `Scene_commands::get_scene_root(Node*)` (+ Material overload) | "Current scene" = host of whichever qualifying item happens to be first in the selection; ambiguous when the selection spans scenes. |
| `Item_tree` shift-range across two hierarchy windows | `Range_selection` is global but entries are fed per-window per-frame, so a range whose terminators are in different windows mis-toggles (garbled partial selection), independent of the scene question. |

### Non-scene items

Most consumers correctly skip non-`Node` items via `dynamic_pointer_cast` /
`Item_filter`. The sharp edge: `async_for_nodes_with_mesh` (`operations/items.cpp`)
derives the item host from the FIRST selected item and returns early when it
has no host - a material first in the selection silently aborts the whole mesh
operation even when mesh nodes are selected.

### Existing patterns to build on

- `Scene_root`'s `Selection_message` subscriber already filters by
  `item->get_item_host() == this` (physics acquire/release) - per-scene
  self-filtering is established.
- `Debug_visualizations` and `Scene_views::choose_camera_for_scene` filter by
  `node->get_scene() == <render/viewport scene>`.
- Scene close (`editor.cpp`) removes only the closing scene's items from the
  selection.
- `Transform_tool::update_for_view(Scene_view*)` already runs once per
  rendered scene view (via `Render_scene_view_message`) and refreshes
  `Handle_visualizations` per view - the natural hook for a per-viewport
  gizmo.
- Active-scene candidates: `Scene_views::hover_scene_view()` (pointer
  currently over a viewport), `Scene_views::last_scene_view()` (persists after
  pointer leaves), `App_scenes::get_single_scene_root()`.

## Design

### Partitioned selection: each scene owns its selection

Selection storage is partitioned by `Item_host`:

- Each `Scene_root` contains the selection of items it hosts (nodes,
  attachments, its content-library items - anything whose `get_item_host()`
  is that scene).
- One additional bucket holds non-hosted items (`get_item_host() == nullptr`).
- `Selection` remains the single mutation API, event source and coordinator:
  `add_to_selection` / `remove_from_selection` / `set_selection` route items
  to the owning bucket by host; `begin/end_selection_change`,
  `Selection_message`, undo snapshots and last-selected tracking stay
  centralized and unchanged in shape. `get_selected_items()` keeps returning
  the union (maintained, not rebuilt per call - no per-frame allocation) so
  per-item consumers (properties window, per-item operations) keep working
  untouched.
- New accessors:
  - `Scene_root::get_selection()` - the items selected in that scene.
  - `Selection::clear_selection(erhe::Item_host* host)` - clear one bucket
    only (nullptr host = the non-hosted bucket); the existing
    `clear_selection()` keeps clearing everything.

Owning the bucket in `Scene_root` (per user direction) also gives the right
lifetime for free: closing a scene destroys its selection with it (the
existing editor.cpp scene-close selection cleanup becomes mostly redundant and
is simplified to just emitting the deselect message).

### Scoped selection semantics (the UX change)

Selection mutations coming from a UI context are scoped to that context's
bucket:

- Plain click in a viewport or hierarchy window clears ONLY that scene's
  bucket before selecting; other scenes' selections persist.
- Ctrl-click toggles within that scene's bucket (already effectively true).
- Ctrl-A in a hierarchy window selects all in THAT window's scene only,
  leaving other buckets intact. Content-library trees likewise scope to their
  own root.
- `Range_selection` becomes per-tree (terminators and entries from one tree),
  fixing the cross-window shift-range mis-toggle.
- Escape / explicit "deselect all" clears every bucket (see open questions).

Multi-scene selection thus remains possible and intentional (ctrl-click in a
second scene ADDS), but a scene's selection is never collateral damage of
working in another scene.

### Transform gizmo: per-viewport, single-scene

The gizmo becomes a function of the viewport it appears in:

- In each viewport, the gizmo anchors to and shows handles for THAT viewport's
  scene bucket only (`scene_view->get_scene_root()->get_selection()`, node
  filter as today). Two viewports showing two scenes each get their own gizmo
  over their own selection. Anchor averaging is then always within one world
  space.
- Idle refresh: `Transform_tool::update_for_view(Scene_view*)` (already called
  per rendered view) rebuilds anchor + `shared.entries` from the view's scene
  bucket before that view's tool render. A viewport whose scene has no
  selected nodes renders no gizmo.
- Drag: `Transform_tool_drag_command::try_ready` captures the hovered
  viewport's scene; entries and anchor are locked to that scene for the whole
  drag (the existing "do not stomp the anchor mid-edit" guard in
  `update_for_view` extends to cover this). Only that scene's nodes move; its
  `item_host_mutex` is the single lock needed - correct by construction.
- Mesh-component mode (`component_mode`) scopes the same way via the component
  selection's mesh host.
- `compute_selection_box` (bounding-box scale gizmo) and the box handles
  follow the per-view entries automatically.

Note: `shared.entries` / `world_from_anchor` being rebuilt per view during
idle means their contents are view-dependent within a frame. Audit the
remaining readers of `shared.entries` (`tool_render` ray rendering, MCP
`transform_selection`, `Node_transform_operation` undo path) for assumptions
that entries are stable across a frame; MCP and numeric edits use the active
scene (below), drags use the drag-captured scene.

### Active scene

Because commands target it (below) and the UI shows it, the active scene is
explicit, tracked state - not a heuristic recomputed at each call site. It is
a single `Scene_root*` (with the usual lifetime care on scene close), owned
centrally (`Selection` or `App_scenes`), with an `Active_scene_changed`
app message so UI and tools react to changes.

The active scene changes on deliberate acts, not on hover (hover-switching
would flicker as the pointer crosses viewports on the way elsewhere):

- a selection change in a scene makes that scene active;
- clicking in / giving ImGui focus to a scene's viewport window or hierarchy
  window makes that scene active;
- closing the active scene falls back: `Scene_views::last_scene_view()`'s
  scene root -> `App_scenes::get_single_scene_root()` -> null.

Exposed as `get_active_scene_root()`. This replaces the first-item scan in
`Scene_commands::get_scene_root` and gives menu-driven operations a
deterministic, user-visible target.

### Operation scoping policy

Commands that target scene-hosted items act on the ACTIVE scene's bucket
only. Selection in other scenes is never an invisible participant in a
command.

- Delete, Cut, duplicate, merge, booleans, mesh operations: active scene's
  bucket. Lock that one scene's `item_host_mutex` - fixes the single-lock
  hole by construction.
- Deselect-all (Escape): consistent with the rule, clears the active scene's
  bucket (plus the non-hosted bucket); other scenes keep their selection.
- Read-only / per-item-safe UI (properties window, selection listing) keeps
  showing the union - inspecting items from several scenes side by side is a
  supported use case.
- Non-hosted items (content library selections with no host) are unaffected
  by scene scoping and handled per command as today.

### Active scene UI highlight

The user must be able to see which scene commands will hit. Planned
indicators, driven by `Active_scene_changed` / `get_active_scene_root()`:

- Viewport windows: title bar tint for viewports whose scene root is the
  active scene (push `ImGuiCol_TitleBg` / `ImGuiCol_TitleBgActive` /
  `ImGuiCol_TitleBgCollapsed` around the window `Begin`; `Imgui_window`
  already has `on_begin` / `on_end` hooks).
- Hierarchy windows: same title tint on the active scene's hierarchy window,
  and/or an accent on the tree (e.g. header/scene-item row color or node text
  tint). Start with the title tint (cheap, consistent with viewports); tree
  text tinting can follow if the title alone is too subtle, but it must not
  fight the selection highlight.
- Keep the treatment consistent between viewport and hierarchy windows so one
  visual language means "active scene" everywhere.

## Phases

### Phase 1: Partitioned selection storage

1. Add the per-host buckets: selection container in `Scene_root` (plus the
   non-hosted bucket in `Selection`), routing in `add_to_selection` /
   `remove_from_selection` / `set_selection` / `clear_selection`, maintained
   union for `get_selected_items()`. No behavior change yet - all producers
   and consumers still see the union.
2. Accessors: `Scene_root::get_selection()`,
   `Selection::clear_selection(Item_host*)`.
3. Active scene tracking: the tracked `Scene_root*`, `get_active_scene_root()`,
   update on selection change and on viewport/hierarchy window focus,
   scene-close fallback chain, and the `Active_scene_changed` app message.
4. Scene close: selection bucket dies with the `Scene_root`; simplify the
   editor.cpp scene-close selection cleanup to message emission.
5. Extend `Selection::sanity_check` to verify bucket/host consistency and
   union == sum of buckets.

Verification: headless MCP - existing selection flows unchanged
(`select_items`, `get_selection`, delete/duplicate smoke); two-scene session
sanity_check clean.

### Phase 2: Scoped selection semantics

1. Plain click in viewport (`Selection::on_viewport_select`) and hierarchy
   (`Item_tree::item_update_selection`) clears only the local scene's bucket.
2. `Item_tree::select_all` scopes to the window's own root.
3. `Range_selection` per tree (move ownership from `Selection` to `Item_tree`,
   or key terminators by tree) - fixes the cross-window shift-range bug.
4. Content-library trees scope their clear/select-all to their own bucket.
5. Deselect-all (Escape): clear the active scene's bucket plus the non-hosted
   bucket, per the operation scoping policy.
6. Active scene UI highlight: viewport window title tint + hierarchy window
   title tint for the active scene, driven by `Active_scene_changed`.

Verification: interactive (user) - two hierarchy windows: plain click in
scene B keeps scene A's selection AND moves the active-scene highlight to
scene B's windows; Ctrl-A selects one scene; shift-range confined to one
window behaves; viewport plain click clears only its scene; focusing a
viewport switches the highlight without changing any selection.
Headless MCP where reachable (`select_items` per scene, `get_selection`).

### Phase 3: Per-viewport transform gizmo

1. `update_target_nodes` takes the scene bucket (a `Scene_root*` parameter)
   instead of the global selection; `on_selection` / `on_node_touched` /
   `on_animation_update` callers pass the affected scene.
2. `update_for_view` rebuilds anchor + entries from the rendered view's scene
   bucket when idle; keep the existing anchor-stomp guards during active
   drag / component edit, extended to scene identity.
3. Drag capture: `try_ready` binds the drag to the hovered viewport's scene;
   reject when that scene's bucket has no transformable nodes.
4. Per-viewport gizmo visibility falls out of (2): no selected nodes in the
   view's scene -> handles hidden for that view. Verify the tool render pass
   ordering (visualizations update vs `Tools::render_viewport_tools`) keeps
   handle transforms consistent per view within a frame.
5. Mesh-component mode scoping via the component mesh's host.
6. Numeric edits (`apply_*_edit`, Transform window) and MCP
   `transform_selection` use `get_active_scene_root()`'s bucket; MCP
   optionally grows an explicit `scene` argument.

Verification: headless MCP - two scenes in two viewports, select a node in
each scene, `capture_screenshot` -> each viewport shows its own gizmo at its
own anchor; `transform_selection` moves only the active scene's nodes;
interactive drag test by user (drag in viewport A moves only scene A nodes).

### Phase 4: Consumer cleanup and latent bug fixes

1. `operations/items.cpp` `async_for_nodes_with_mesh`: operate on the active
   scene's bucket; derive the host from the bucket (not the first item of any
   type) - fixes the leading-material silent abort.
2. `mesh_operation.cpp`, `geometry_operations.cpp`, `merge_operation.cpp`:
   active-scene bucket + that scene's single `item_host_mutex`; VERIFY all
   participating nodes share the host.
3. `Scene_commands::get_scene_root(Node*)` and Material overload: use
   `get_active_scene_root()`; keep hover-viewport / single-scene fallbacks.
4. `Fly_camera_tool` frame-selection: frame the viewport's own scene bucket.
5. `Selection::delete_selection` / `cut_selection` / `duplicate_selection`:
   scope to the active scene's bucket (plus non-hosted items where the
   command applies), per the operation scoping policy.
6. `Clipboard::resolve_paste_target`: null-check `m_last_hover_scene_view`
   before `get_scene_root()` (unchecked deref found in audit).
7. Sweep remaining first-item consumers (grid attach, brush tool parent,
   animation keying) to use the active scene or per-item hosts as appropriate.

Verification: headless MCP smoke - merge and boolean ops in a two-scene
session touch only the active scene; mesh op with a material selected first
plus mesh nodes runs (previously aborted).

### Phase 5: MCP and polish

1. `get_selection` reports each item's scene (and the active scene) so scripts
   can see the partition.
2. `select_items` semantics documented: additive across scenes, scoped clears.
3. Re-run the full selection/gizmo smoke suite; update `doc/` user-facing
   notes if any.

## Decided

- Commands that target scene-hosted items act on the ACTIVE scene only
  (Delete, Cut, duplicate, merge, booleans, mesh ops, deselect-all).
- The active scene is explicit tracked state, changed by selection changes
  and viewport/hierarchy window focus - never by hover alone - and is shown
  in the UI (window title tints).

## Open questions

- Exact highlight treatment: title tint only, or also node-text / scene-row
  accent inside the active hierarchy tree? Start with title tint, evaluate
  visibility, extend if needed (must not fight the selection highlight).
- Should hierarchy windows visually dim selection highlights in NON-active
  scenes to reinforce that commands will not touch them? Nice-to-have,
  deferred.
- MCP: does `select_items` implicitly set the active scene (matching the
  "selection change activates the scene" rule), and should an explicit
  `set_active_scene` tool exist for scripts? Leaning yes to both.
