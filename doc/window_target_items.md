# Issue #252 - Independent target item for editor/properties windows

https://github.com/tksuoran/erhe/issues/252

Status: IMPLEMENTED (branch `crease`)

## Implementation Status (as-built)

All five phases implemented and verified headless. Commits on `crease`:

- Phase 1 - `7d80b0e8`: graph windows edit an explicit `weak_ptr` target
  (`set_target` / `get_target`, `resolve_target` replacing the
  selection-scanning refresh), a target-selector row (`item_reference_imgui`)
  at the top of each window, node <-> global-selection sync removed from
  `geometry_graph_node` / `texture_graph_node`, create-asset context menu +
  `create_graph_*` MCP tools migrated to `set_target`, and new
  `set_geometry_graph_target` / `set_texture_graph_target` MCP tools. Smoke
  tests migrated to the target model.
- Phase 2 - `16cd28c5`: `Properties` gains a `weak_ptr<Item_base>` target
  (`set_target`), a "Pin" selector row, and `effective_items()` -> `{target}`
  when pinned else the global selection (fallback = original behavior).
- Phase 3 - `177c913f`: `Editor_windows` (`src/editor/windows/editor_windows.*`)
  owns the dynamically-created extra instances (unique title, empty ini_label),
  creation deferred via `Imgui_windows::queue()`, closed instances pruned once
  per frame. Window constructors gained optional title/ini_label. MCP
  `open_geometry_graph_window` / `open_texture_graph_window` /
  `open_properties_window`.
- Phase 4 - `3df49974`: item context menu "Open Editor" (graph assets +
  scenes) / "Open Properties" (any item) and item double-click, both routed
  through `Editor_windows::open_editor_for_item` / `open_properties_for_item`;
  `Scene_views::open_new_viewport_scene_view_node(scene_root)` for the scene
  case. "Open Editor" reuses the primary graph window when it has no target,
  else opens a fresh instance.
- Phase 5 - verification: geometry smoke 129/129 (graph_editor trace on),
  texture smoke 266/266, both on fresh editors; core acceptance test (graph
  nodes are NOT in the global selection, and removing a node keeps the graph
  asset) confirmed over MCP; two Geometry Graph windows on two assets + a
  Properties window pinned to A while selection is B confirmed by screenshot.

### Deviations from the plan

- **Shader graph left as-is.** The plan (Phase 1.2) listed
  `shader_graph_node.cpp:197` for sync removal, but that sync feeds the shader
  graph's `Node_properties_window` (which reads the global selection) and the
  shader graph has NO containing-asset delete bug (its nodes are not
  content-library assets). Removing it there would regress node-property
  display with no corresponding fix, so it was intentionally kept.
- **Node properties in the Properties window** for the geometry / texture
  graphs: removed with the selection sync, as the plan's chosen option. Node
  parameters are edited in-node on the canvas; the Properties window shows a
  node only when explicitly pinned to it.
- **Palette windows**: the primary graph windows keep their companion palette
  window (forwarding to the primary); extra instances use the canvas
  background "Add node" context menu plus their own target selector. No
  focus-tracking was added.
- **Target persistence**: not implemented (matches the extra-viewport
  precedent), as planned.

### Verification limits

The mouse-driven triggers (context-menu entries, double-click) and the literal
"press Delete on a canvas node" gesture are UI gestures with no headless mouse,
so they are inspection-verified and reuse the MCP-verified window-open
machinery. The *fix* for the Delete bug is verified at the mechanism level
(nodes are not in the global selection; `remove_node` keeps the asset).

---


## Problem

- The Properties window shows properties for the global selection.
- The Texture graph and Geometry graph windows edit the graph of the *first
  selected* `Graph_texture` / `Graph_mesh` item
  (`Texture_graph_window::refresh_current_graph_texture()` at
  `src/editor/texture_graph/texture_graph_window.cpp:98`,
  `Geometry_graph_window::refresh_current_graph_mesh()` at
  `src/editor/geometry_graph/geometry_graph_window.cpp:84` - both scan
  `selection->get_selected_items()` via `get<T>()`).
- Selecting a node inside a graph editor pushes the node into the *global*
  selection (`geometry_graph_node.cpp:343-351`, `texture_graph_node.cpp:438`,
  `shader_graph_node.cpp:197`). The global selection then holds both the graph
  asset (a `Content_library_node`, which is a `Hierarchy`) and the node.
  Pressing Delete fires two paths at once:
  1. `Selection_delete_command` -> `Selection::delete_selection()`
     (`src/editor/tools/selection_tool.cpp:328`) deletes `Hierarchy` items ->
     deletes the whole graph asset.
  2. The ax::NodeEditor canvas `handle_deletions()`
     (`geometry_graph_window.cpp:860`) deletes the node.
  One keypress deletes both the node and its containing graph. This is the
  core bug; the root cause is that graph editing is coupled to the global
  selection at all.

## Goal (from the issue)

1. Editor windows (viewport, texture graph, geometry graph) have an explicit
   *target item*. Unset target => the editor shows nothing.
2. Double-click or context-menu "Open Editor" on a texture/geometry graph item
   opens the matching graph editor window with the target explicitly set. For
   a scene, "Open Editor" opens a new viewport window.
3. The Properties window also has a target item; when unset it falls back to
   showing the global selection (current behavior); when set it shows only the
   target.
4. Item context menu gains "Open Properties" which opens a *new* Properties
   window pinned to that item.
5. Multiple Texture graph / Geometry graph / Viewport / Properties windows can
   be open simultaneously. Each has a target-item selector at the top of the
   window (viewport already has scene selection via its toolbar).

## Existing machinery to build on (facts)

- **Multiple instances of one window class are already a solved problem.**
  `Imgui_window` identity is the title string passed to `ImGui::Begin`
  (`src/erhe/imgui/erhe_imgui/imgui_window.cpp:148`); an empty `ini_label`
  means no persisted open-state. Precedents:
  - `Scene_root::make_browser_window` (`src/editor/scene/scene_root.cpp:285`):
    `fmt::format("Scene Hierarchy [{}]", ++s_browser_window_count)`, empty
    ini_label.
  - `Scene_views::create_viewport_window`
    (`src/editor/scene/viewport_scene_views.cpp:360`): `"{}##{}"` title
    suffix, stored in `m_viewport_windows`.
  Window create/destroy must happen outside ImGui iteration - use
  `Imgui_windows::queue()` (`imgui_windows.hpp:50`).
- **Target selector widget already exists**: `editor::item_reference_imgui`
  (`src/editor/windows/item_reference.hpp:33`) - drag-drop source+target,
  type-mask filter, optional picker popup, clear button. Built for exactly
  this (see memory `project_item_reference_widget`, issue #231).
- **Context menu extension point**:
  `Item_tree::add_item_context_menu_callback(Context_menu_callback)`
  (`src/editor/windows/item_tree_window.hpp:70`); callbacks receive the item
  plus a `deferred_operations` vector that runs after the popup closes -
  safe place to create windows. Registered per scene in
  `Scene_root::make_browser_window` (`scene_root.cpp:388,494,593`).
- **Double-click**: no `IsMouseDoubleClicked` handling exists anywhere in the
  item tree; single-click selection is in `Item_tree::item_update_selection()`
  (`item_tree_window.cpp:788`, click handling ~858-885).
- **New viewport programmatically**:
  `Scene_views::open_new_viewport_scene_view_node()`
  (`viewport_scene_views.cpp:457`, already bound to F1).
- **Item types**: target for texture graph = `Graph_texture`
  (`Item_type::graph_texture`), geometry graph = `Graph_mesh`
  (`Item_type::graph_mesh`), properties = any `Item_base`. Selection carries
  `Content_library_node` wrappers; `get<T>()` in `items.hpp` unwraps them -
  the target-setting code must do the same unwrap.

## Design decisions

- **Target storage**: `std::weak_ptr` on each window (graph windows already
  hold `m_graph_texture` / `m_graph_mesh` shared_ptrs - convert to explicit
  targets set only via the selector/open-editor path, never via selection
  scanning). weak_ptr so a deleted asset naturally clears the target (window
  then shows nothing / falls back), avoiding keep-alive of deleted items.
- **Selection decoupling for graph nodes**: remove the node <-> global
  selection sync blocks in `geometry_graph_node.cpp` / `texture_graph_node.cpp`
  / `shader_graph_node.cpp`. Node selection lives purely in the ax::NodeEditor
  canvas; canvas Delete (`handle_deletions()`) remains the only way Delete
  acts inside a graph editor. Consequence: the Properties window no longer
  shows a canvas-selected node via global selection - restore that by having
  the graph window feed its canvas selection to Properties windows targeting
  that graph, or (simpler, chosen) each graph window shows node parameters in
  its own side panel as today, and Properties shows the node only when the
  user explicitly targets it. Verify with the user during implementation if
  node properties in the Properties window matter; the smoke tests
  (`scripts/geometry_nodes_smoke_test.py`) drive parameters via MCP, not via
  Properties, so they are unaffected.
- **Singletons become "primary instances"**: keep the existing singleton
  Properties / Texture graph / Geometry graph windows (persisted ini labels,
  present in the Window menu) as instance #0. The primary graph windows start
  with *unset* target (per the issue: "shows nothing") instead of tracking
  selection; the primary Properties window keeps selection-fallback behavior.
  Additional instances are dynamically created, uniquely titled
  (`"Properties [2]"`, `"Texture Graph [2]"`, ...), empty ini_label (not
  persisted across runs - same policy as extra viewports and per-scene
  hierarchy windows).
- **Ownership of dynamic instances**: a small new manager (e.g.
  `Editor_windows` or extend an existing part) holding
  `std::vector<std::shared_ptr<...>>` per window type, mirroring
  `Scene_views::m_viewport_windows`. Windows get a "close" affordance
  (Imgui_window open flag); closed dynamic instances are destroyed via
  `Imgui_windows::queue()`.
- **No persistence of targets** in this issue (matches the extra-viewport
  precedent; `Viewport_scene_view` scene binding is also not persisted).
  Persistence can be a follow-up.
- **Behavior change to note in commit message**: selecting a graph asset in
  the hierarchy no longer implicitly re-points the graph editor; the MCP
  server and smoke tests rely on the old idiom
  (`selection->set_selection({asset})` in `mcp_server_graphs.cpp:450,586` and
  the create-asset context menu at `scene_root.cpp:536-587`) and must be
  migrated to set the window target explicitly.

## Implementation phases

### Phase 1 - Graph windows: explicit target + decoupled node selection

1. `Texture_graph_window` / `Geometry_graph_window`: add
   `set_target(shared_ptr<Graph_texture/Graph_mesh>)` /
   `get_target()`; store as weak_ptr; delete `refresh_current_graph_*()` and
   all its call sites. When target is expired/unset, `imgui()` draws only the
   target selector and an empty canvas ("no graph" text).
2. Remove the node->global-selection sync in `geometry_graph_node.cpp:343`,
   `texture_graph_node.cpp:438`, `shader_graph_node.cpp:197`; also drop the
   corresponding `remove_from_selection` calls in `erase_node`/`remove_node`
   (`texture_graph_window.cpp:183`, `geometry_graph_window.cpp:154`).
3. Add the target selector at the top of both windows using
   `item_reference_imgui` with the matching type mask
   (`accept_content_library_node = true`; unwrap on assignment).
4. Migrate the "create Graph Texture / Graph Mesh asset" context-menu code
   (`scene_root.cpp:536-587`) and MCP graph tools
   (`mcp_server_graphs.cpp`) from `set_selection({asset})` to setting the
   primary window's target.
5. Verify: headless smoke tests (`geometry_nodes_smoke_test.py`, texture
   sweep) still pass; manual/MCP check that Delete on a canvas node no longer
   deletes the graph asset (this is the acceptance test for the original
   bug).

### Phase 2 - Properties window: target + fallback

1. Add target member (`weak_ptr<Item_base>`) + `set_target()`. In
   `Properties::imgui()` (`properties.cpp:1822`) and `material_properties()`
   (`properties.cpp:1636`): if target set, build the item list from the target
   only; else current selection behavior.
2. Target selector row at the top (`item_reference_imgui`, any item type,
   clear button; when cleared the window reverts to selection mode). Show a
   small "pinned" indicator when targeted.

### Phase 3 - Multiple instances

1. New owner (suggest `src/editor/windows/editor_windows.{hpp,cpp}` part or
   fold into an existing suitable part) with:
   - `open_properties_window(target) -> shared_ptr<Properties>`
   - `open_texture_graph_window(target)`, `open_geometry_graph_window(target)`
   - unique-title counters, empty ini_label, `set_imgui_host(...)`, creation
     deferred via `Imgui_windows::queue()` when called from inside UI code.
   - destruction of instances whose window was closed (poll open flag once
     per frame, or hook `Imgui_window` close).
2. Palette companion windows (`*_graph_palette_window`) forward to the
   *focused/last-active* graph window instead of the singleton - or simplest:
   each graph window keeps its own palette embedded; decide during
   implementation (check how `controls_imgui()` forwarding works when there
   are N instances).
3. Constructor discipline: follow the App_context rule - new windows receive
   needed parts as explicit references (copy the ctor argument lists of the
   existing singletons).

### Phase 4 - Open Editor / Open Properties / double-click / scene viewport

1. Register a context-menu callback (in `Scene_root::make_browser_window`, or
   globally in `Item_tree` since it is type-driven, not scene-driven):
   - `Graph_texture` item -> "Open Editor" -> deferred: open (or retarget
     primary if closed?) texture graph window with target set. Decision:
     always open a *new* window per the issue text ("opens matching graph
     editor window with the target item explicitly set" - reuse the primary
     instance if it has no target, else open a new one; keeps window count
     sane).
   - `Graph_mesh` item -> same for geometry graph.
   - Scene / `Scene_root` item -> "Open Editor" ->
     `Scene_views::open_new_viewport_scene_view_node()` then
     `set_scene_root` on the new view.
   - Any item -> "Open Properties" -> new Properties window with target set.
2. Double-click in `Item_tree::item_update_selection()`
   (`item_tree_window.cpp:~858`): `ImGui::IsMouseDoubleClicked(0)` on the row
   -> same dispatch as "Open Editor" (graph items and scenes only; other item
   types keep default no-op). Must not fight the single-click selection logic
   (double-click also fires the single-click path; that is fine since
   selection then open is the natural result).

### Phase 5 - MCP + verification

1. MCP: extend graph tools so scripted flows work without global selection:
   e.g. `get_geometry_graph`/`get_texture_graph` gain an optional asset-name/
   id argument, or an explicit `set_graph_window_target` tool. Update
   `scripts/geometry_nodes_smoke_test.py` / texture sweep accordingly.
2. Full verification via `erhe-headless-verify`: both smoke sweeps green,
   plus new checks:
   - open editor via MCP with explicit target, mutate, delete node via canvas
     path, confirm graph asset survives;
   - Properties pinned to item A shows A while selection is B;
   - two graph windows on two different assets simultaneously.
3. Screenshot checks for the target-selector row rendering in both graph
   windows and Properties.

## Risks / open questions

- **Node properties in Properties window**: removing the selection sync means
  canvas node selection no longer surfaces in Properties. If that regresses a
  workflow the user cares about, an alternative is a *scoped* selection (per
  graph window) that Properties can opt into. Ask before Phase 1 lands if in
  doubt.
- **Palette windows** (Phase 3.2) assume a single graph window today.
- **Undo/redo**: graph operations reference the window/graph; with N windows,
  ensure operations capture the graph asset, not "the window's current
  graph" (audit `Geometry_graph_parameter_operation` and node insert/remove
  ops for hidden singleton-window assumptions).
- **Async geometry evaluation** (shadow-clone snapshot isolation) iterates
  graphs; with multiple geometry graph windows targeting the *same* asset,
  double-editing one graph from two canvases needs a check (ax::NodeEditor
  context per window - each window already owns its editor context; confirm).
- **`Item_tree` global vs per-scene callback registration** for the new menu
  entries: per-scene registration duplicates code across browser windows;
  putting type-driven entries directly in `Item_tree::item_popup_menu()`
  (`item_tree_window.cpp:917`) may be cleaner. Decide during implementation.
