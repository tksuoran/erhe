# Window target items

Stability: mostly stable

Each editor and Properties window edits or inspects one explicit **target
item** rather than whatever the global selection happens to hold, and several
instances of one window class can be open at once, each on its own target.

## Why targets and not the selection

A graph editor that picked up the first selected `Graph_mesh` / `Graph_texture`
had to put its canvas nodes into the global selection to show their properties.
The selection then held both the graph asset - a `Hierarchy` - and the node, so
one Delete keypress fired two paths at once: `Selection::delete_selection()`
deleted the `Hierarchy`, that is the whole graph asset, while the canvas
`handle_deletions()` deleted the node. Decoupling graph editing from the global
selection is the fix; node selection lives purely in the ax::NodeEditor canvas,
and canvas Delete is the only thing that acts inside a graph editor.

## The target model

- **Storage.** Every targetable window holds a `std::weak_ptr`: the graph
  windows a `Graph_mesh` / `Graph_texture`, `Properties` an `Item_base`
  (`m_target`). Weak, so a deleted asset clears the target instead of keeping
  it alive. The `Selection` carries content-library wrappers, so the
  target-setting code unwraps them the way `get<T>()` in `items.hpp` does.
- **Setting it.** A target is set only through `set_target()`: the selector
  row, "Open Editor", a double-click, the create-asset context menu or an MCP
  tool. Nothing scans the selection.
- **Selector row.** Each window draws `editor::item_reference_imgui`
  (`windows/item_reference.hpp`) at the top - drag-drop source and target, a
  type mask, a picker popup and a clear button. Properties labels its row
  "Pin".
- **Unset target.** A graph window with no target draws the selector and an
  empty canvas. `Properties` falls back to the global selection:
  `effective_items()` returns the target alone when pinned, the selection
  otherwise.
- **Targets are not persisted**, matching the extra-viewport precedent.

## Multiple instances

An `Imgui_window`'s identity is the title string it passes to `ImGui::Begin`,
and an empty `ini_label` means no persisted open state. The singleton
Properties / Texture Graph / Geometry Graph windows are the primary instances:
they keep their persisted ini labels and their Window menu entries. The primary
graph windows start with an unset target.

`Editor_windows` (`src/editor/windows/editor_windows.{hpp,cpp}`) owns the
dynamically created extra instances - `open_properties_window(target)`,
`open_geometry_graph_window(target)`, `open_texture_graph_window(target)` -
each with a unique title (`"Properties [2]"`, ...) and an empty ini_label.
Creation and destruction must happen outside ImGui iteration, so creation is
deferred through `Imgui_windows::queue()` and closed instances are pruned once
per frame. New windows take the parts they need as explicit constructor
references, like the singletons (the `App_context` construction rule).

Each graph window owns its own `ax::NodeEditor` context, so two windows on the
same asset do not share canvas state. The primary graph windows keep their
companion palette window, which forwards to the primary; an extra instance uses
the canvas background "Add node" context menu and its own target selector.

## Entry points

`Editor_windows::open_editor_for_item()` and `open_properties_for_item()` are
the single dispatch: a `Graph_mesh` or `Graph_texture` opens the matching graph
editor (reusing the primary window when it has no target, else a fresh
instance), a scene opens a new viewport through
`Scene_views::open_new_viewport_scene_view_node()`, and any item opens a
Properties window pinned to it. They are reached from the item context menu
("Open Editor" / "Open Properties") and from a double-click on an item row
(`Item_tree`, `ImGui::IsMouseDoubleClicked`); the double-click also runs the
single-click selection path, which is what makes "select, then open" the
natural result. MCP reaches them through `open_geometry_graph_window` /
`open_texture_graph_window` / `open_properties_window`, and points a window at
an asset with `set_geometry_graph_target` / `set_texture_graph_target`.

## Deliberate limits

- **The legacy shader graph keeps its selection sync.** It feeds the shader
  graph's `Node_properties_window`, which reads the global selection, and the
  shader graph's nodes are not content-library assets, so it has no
  containing-asset delete bug to fix. Removing the sync there would regress
  node-property display and fix nothing.
- **Node parameters are edited in-node**, on the canvas. The Properties window
  shows a graph node only when it is explicitly pinned to it.
- **Undo operations capture the graph asset, not "the window's current
  graph"** - with N windows on N assets, an operation that resolved its target
  through the window would act on the wrong graph.

## Verification

Both smoke sweeps (`scripts/geometry_nodes_smoke_test.py`,
`scripts/texture_graph_smoke_test.py`) drive the target model and run on fresh
editors. The checks worth keeping beyond them: a canvas node is NOT in the
global selection and removing a node keeps the graph asset (the acceptance test
for the Delete bug); Properties pinned to item A shows A while the selection is
B; two graph windows on two assets at once; the selector row renders in both
graph windows and in Properties. The mouse-driven triggers - the context-menu
entries, the double-click, a literal Delete keypress on a canvas node - have no
headless mouse, so they are verified at the mechanism level through the same
MCP-driven window-open machinery.
