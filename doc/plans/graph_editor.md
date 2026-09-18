# Graph editor: remaining shared-layer work

Status: proposed

Extends `doc/graph_editor.md`, which describes the graph editor as built. This
plan holds what is not shared yet and the open questions around it.

## C7 remainder - finish the window base

The canvas render loop (`imgui()` Begin/End, node iteration, link drawing, the
zoom overlay), link create / delete (`handle_link_create` /
`handle_deletions`) and the node-position helpers are still copied between the
geometry and texture graph windows. `Graph_editor_node` makes node iteration
payload-blind (the loop can cast to the shared base), so these move into
`Graph_editor_window_base` given a small set of hooks - `graph()` returning an
`erhe::graph::Graph*` that is null in the empty state, plus `connect`,
`disconnect` and `remove_node` - and by moving `m_node_editor` (the
`ax::NodeEditor` context) into the base.

Do it as its own commit: it touches the interactive per-frame render path next
to the geometry graph's async evaluation engine and the per-window target
model. Verify with both smoke sweeps and a canvas screenshot. The target
model, the evaluation strategy and the ~15 MCP-facing window methods stay
per-editor.

## C8 - dedup the MCP / create-UI / scene save-load boilerplate

The ~9 twin MCP tool bodies (`mcp_server_graphs.cpp`), the two `scene_root`
"Create Graph *" context-menu branches and the parallel scene save / load
blocks are payload-blind over `<Asset, Window, folder member, write / read
fn>`. Lower value than C7 (short bodies, more surface); it builds on the
shared asset-lookup helpers that already exist.

## Relocate the gradient / curve editors

`texture_gradient_editor` / `texture_curve_editor` live in
`texture_graph_widgets.*`. They are general node-content widgets like the
combos and steppers and belong in `graph_editor_widgets` once a second
consumer appears; they carry an `erhe_texgen` dependency, which is why the
otherwise dependency-free shared widgets header does not hold them yet.

## Modernize or retire the legacy shader graph

`src/editor/graph/` is the prototype the two current graphs were forked from,
and `doc/graph_editor.md` lists what it lacks. Either retrofit it onto the
shared layer - it needs the dirty flag, parameter (de)serialization, undo, a
factory type name and an owning asset - or remove it once nothing needs it.

## A fifth graph feature

Whatever it is, build it directly on the shared layer: a payload type, a node
set, a factory, an evaluation strategy and an asset with its consumption
model.
