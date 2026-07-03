# Graph Mesh asset + Geometry Graph Mesh node attachment (Phase B)

Make a geometry node graph a first-class, selectable, serializable **asset**
(`Graph_mesh`) in a scene's Content library, and give scene `Node`s a
**"Geometry Graph Mesh"** `Node_attachment` that sources its mesh from such an
asset - the scene-side analogue of Phase A's `Graph_texture` +
`Material::texture_source` (see [`doc/graph-texture-plan.md`](graph-texture-plan.md)).
Today `Geometry_output_node` creates ordinary scene nodes/meshes in place with
no back-link; Phase B closes exactly that gap: the attachment CONTROLS the
node's mesh (its geometry is produced by the graph) and POINTS BACK to the
graph asset that produced it.

This continues the geometry-nodes feature (see
[`doc/geometry-nodes-plan.md`](geometry-nodes-plan.md), all phases complete).
The graph engine, node classes, async shadow-clone evaluation, undo operations,
serialization format and MCP surface carry over in substance - what changes is
*who owns the graph* (window -> asset), plus the new attachment and its bindings.

## Table of Contents

1. [Implementation Status](#implementation-status)
2. [Current State (what exists today)](#current-state-what-exists-today)
3. [Architecture Decisions](#architecture-decisions)
4. [Implementation Plan (Steps)](#implementation-plan-steps)
5. [Verification Strategy](#verification-strategy)
6. [Key Files Reference](#key-files-reference)

---

## Implementation Status

| Work item                                                           | Status | Commit |
|---------------------------------------------------------------------|--------|--------|
| B1: Item types + `Graph_mesh` asset class + Content-library folder   | TODO   |        |
| B2: Re-home graph state window -> asset; per-asset async engine; create UI | TODO |  |
| B3: `Geometry_graph_mesh` attachment; output node publishes to asset | TODO   |        |
| B4: MCP tools (create/list/bind); per-asset graph tools               | TODO   |        |
| B5: Scene serialization of asset + binding (scene_file v7)            | TODO   |        |
| B6: Smoke coverage + full live headless verification                  | TODO   |        |

---

## Current State (what exists today)

Established by a research pass (three parallel Explore agents) before any code:

- **One global graph, window-owned.** `Geometry_graph_window`
  (`geometry_graph_window.hpp:149-153`) owns `Geometry_graph m_graph` by value,
  `std::vector<std::shared_ptr<Geometry_graph_node>> m_nodes` (node lifetime;
  the graph's `m_nodes` holds raw pointers into these), the ax::NodeEditor
  context, the in-flight `Evaluation_run`, and a single file path. All nine
  geometry-graph MCP tools and all five undo operation classes reach the graph
  through `m_context.geometry_graph_window` / a `Geometry_graph_window&` member.

- **Async evaluation is window-driven.** `editor.cpp` calls
  `update_evaluation()` once per frame. `launch_evaluation()` builds a shadow
  clone of the whole graph (factory + parameter JSON + links + cached payloads
  + dirty flags) into an `Evaluation_run` and evaluates it on
  `context.executor->silent_async`. `finish_evaluation()` matches shadow nodes
  back to live nodes by id, copies payloads back, and for output nodes calls
  `take_evaluated()` + `apply_evaluated_to_scene()` (main thread).
  `wait_for_idle_evaluation()` is the completion barrier used by
  `get_geometry_graph` and graph save.

- **The output node owns the scene node - inverted from the target.**
  `Geometry_output_node` (`nodes/geometry_output_node.hpp:80-97`) owns
  `shared_ptr<erhe::scene::Node> m_node` + `shared_ptr<Mesh> m_mesh` +
  `shared_ptr<Node_physics>`, created lazily in `apply_evaluated_to_scene()`
  under the scene `item_host_mutex` and parented to the scene root. Released in
  `on_removed_from_graph()` (the node object survives in the undo stack). Its
  two-phase evaluate builds `m_evaluated_{geometry,primitive,collision_shape}`
  on the worker; apply consumes them on the main thread. No back-link: the only
  trace is the node name string "Geometry Graph".

- **Phase A established the asset pattern** (mirrored decision-for-decision
  here): `Graph_texture` is `erhe::Item<..., not_clonable>` +
  `erhe::graphics::Texture_reference`, owns `Texture_graph` + node vector, in a
  `graph_textures` Content-library folder; the window keeps a scratch default
  and edits the selected asset (`get<T>(selection->get_selected_items())`
  unwraps `Content_library_node`); undo operations carry the specific
  `shared_ptr<Graph_texture>`; create UI = content-library right-click + window
  "New" button (mandatory - its absence was the one real Phase A bug);
  MCP `create_graph_texture` / `get_graph_textures` /
  `set_material_texture_source`; scene_file v6 persists assets as
  `{name, graph JSON blob}` + name-resolved bindings with warn+skip degradation.

- **`Node_attachment` is an existing editor-implementable seam.**
  `erhe::scene::Node_attachment` (node_attachment.hpp:15) is an `erhe::Item`
  with `set_node` / `handle_node_update` / `handle_item_host_update`
  lifecycle. Editor-defined subclasses already exist (`Node_physics`,
  `Node_joint`, `Frame_controller`, `Brush_placement`, `Grid`), so a new
  attachment class in `src/editor/` requires **zero** changes to `erhe::scene`.
  `Mesh` itself is a `Node_attachment`; `get_attachment<T>(node)` finds typed
  attachments. Attachments are serialized editor-side (`Node_physics_data`
  keyed by `node_id` is the template).

- **Item registry.** `item.hpp`: indices 1..42 taken (42 = `graph_texture`),
  `count = 43`. Next free index is **43**.

- **Group_node precedent.** Sub-graphs are already referenced as external
  assets (by file path, same JSON v1 format) and reloaded on path change;
  `adopt_subgraph_outputs` pairs live/shadow subgraphs during async runs.

---

## Architecture Decisions

### 1. The back-link lives in an editor-side `Node_attachment` - no new low-level "geometry source" interface

Phase A needed a low-level seam (`Texture_reference` beside
`Material_texture_sampler::texture`) because the consumer (`Material`,
`erhe::primitive`) and the resolver (`Material_buffer`, `erhe::scene_renderer`)
both live *below* the editor, and textures are cheap to re-resolve every frame
(pull model).

The geometry case is structurally different:

- Producing the mesh is **expensive** (geometry copy + process + GPU primitive
  build) and already runs through an explicit **push** pipeline (async worker
  evaluate -> main-thread apply). A per-frame pull-resolve seam like
  `Geometry_reference` would be an unused fiction - nothing may rebuild
  geometry per frame.
- The consumer is a scene `Node`, and `erhe::scene` already has a polymorphic,
  editor-implementable extension point designed for exactly this:
  `Node_attachment`. `Node_physics` is the precedent (editor-defined attachment
  holding editor/backend state, zero `erhe::scene` -> editor dependency).

So the back-link is `editor::Geometry_graph_mesh : erhe::scene::Node_attachment`,
holding `std::shared_ptr<Graph_mesh>` (the asset). The renderer keeps rendering
a perfectly ordinary `erhe::scene::Mesh`; the attachment *controls* that mesh
(creates the sibling `Mesh` attachment, replaces its primitives whenever the
graph re-bakes, keeps `Node_physics` in sync). Layering is respected by
construction: the attachment class lives in `src/editor/`, `erhe::scene` is
untouched. This is the minimal seam; introducing a parallel
`Geometry_reference` interface in `erhe::geometry`/`erhe::primitive` would be
invention without a consumer, rejected for the same "reuse over invention"
reason Phase A reused `Texture_reference`.

### 2. `Graph_mesh` is a `not_clonable` `erhe::Item` asset owning the graph AND its baked products

- New `Item_type::graph_mesh` (index 43) + a `graph_meshes` folder on
  `Content_library` ("Graph Meshes"). `class Graph_mesh : public
  erhe::Item<erhe::Item_base, erhe::Item_base, Graph_mesh, not_clonable>`
  (mirrors the shipped `Graph_texture`, which deviated from its plan by being
  `not_clonable` - duplication goes through serialization).
- It owns the `Geometry_graph` (by value) + the node `shared_ptr` vector -
  re-homed off the window - plus the **baked products** published by its output
  node after each evaluation: `{shared_ptr<Geometry>, shared_ptr<Primitive>,
  shared_ptr<ICollision_shape>, uint64_t revision}`. Bound attachments consume
  these; the revision lets bind-time application reuse the latest bake without
  re-evaluating.
- This is the payoff over Phase A's texture case: **N scene nodes can source
  one graph asset** (the products are shared `shared_ptr`s; each attachment's
  mesh shares the same GPU primitive).

### 3. Window edits the *selected* `Graph_mesh`; the async engine runs per-asset, one run in flight

- `Geometry_graph_window` keeps a **scratch default** `Graph_mesh` (edited when
  nothing is selected - same intentional deviation Phase A shipped) and a
  per-frame `refresh_current_graph_mesh()` that resolves
  `get<Graph_mesh>(selection->get_selected_items())` (unwraps
  `Content_library_node`), falling back to the scratch. The palette toolbar
  shows "Editing asset: <name>" vs "(scratch - not saved)" plus a
  "New Graph Mesh" button; the content library gets a right-click
  "Create Graph Mesh" that creates AND selects (mandatory create UI,
  Phase A lesson #5).
- `update_evaluation()` (still one per-frame call from `editor.cpp`) becomes a
  scheduler: at most **one `Evaluation_run` in flight at a time**, targeting
  whichever graph (scratch or any asset in any scene's content library) needs
  evaluation, round-robin. `Evaluation_run` gains the target
  `shared_ptr<Graph_mesh>`; finish applies to that asset's live nodes.
  One-at-a-time keeps the engine simple (single-editor workloads) and
  preserves the existing counters/logging; parallel per-asset runs are a
  possible later step.
- `wait_for_idle_evaluation()` (the `get_geometry_graph` / save barrier) loops
  until **no** graph needs evaluation and no run is in flight - the MCP
  eventual-consistency contract is unchanged.
- The five undo operation classes and save/load/clear retarget from
  `Geometry_graph_window&` to a captured `shared_ptr<Graph_mesh>` (mirror of
  Phase A's ops carrying `m_graph_texture`), so undo/redo stays correct when
  the selection changes between an edit and its undo.

### 4. The output node *publishes to the owning asset*; attachments consume (scratch keeps the legacy path)

`Geometry_output_node` keeps its two-phase evaluate unchanged (worker builds
`m_evaluated_*`). The apply phase forks on ownership (exact mirror of Phase A's
output-node "assign" - source-reference for assets, legacy push for scratch):

- **Asset-owned graph** (node has a non-null owning `Graph_mesh`, wired at
  insert like `set_owning_graph_texture`): `apply_evaluated_to_scene()` moves
  the products into the asset (`Graph_mesh::set_baked_products`, bumping the
  revision) and does NOT create/own any scene node. The engine then pushes to
  every `Geometry_graph_mesh` attachment bound to that asset (enumerated via
  the scenes' flat node lists at finish time - evaluation finishes are rare, so
  no per-frame cost); each attachment swaps its mesh primitives + physics under
  the scene `item_host_mutex`. An asset with no bound node renders nothing -
  exactly like a `Graph_texture` no material samples.
- **Scratch graph** (no owning asset): legacy behavior, unchanged - the output
  node creates/owns its own scene node + mesh. This keeps the existing
  120-check smoke sweep semantics intact (it never selects an asset).

Binding an attachment (UI / MCP / scene load) applies the asset's current baked
products immediately when the revision shows a bake exists; a freshly loaded
scene's graphs are born dirty, so the first evaluation pushes to all bindings.

Physics stays configured on the output node (graph-side, as today): the baked
collision shape + motion mode/enable travel with the products; each bound
attachment materializes its own `Node_physics` from them.

### 5. Bindings serialize by node id + asset name in `scene.json` (scene_file v7)

Two new codegen structs (Phase A shape):

- `Graph_mesh_data { name, graph }` - the asset, graph as a JSON string blob
  (reusing the geometry-graph v1 format that save/load/groups already use).
- `Graph_mesh_binding_data { node_id, graph_mesh_name }` - one per
  `Geometry_graph_mesh` attachment (`node_id` is the file-local id, the
  `Node_physics_data` convention).

`scene_file.py` bumps 6 -> 7 with two `added_in=7` Vector fields. Load
reconstructs assets via `graph_meshes->make<Graph_mesh>(name)` +
`read_parameters`-based graph parse (`log_parsers->warn` + skip on malformed),
then re-resolves bindings by node id + asset name, attaching a
`Geometry_graph_mesh` and letting the first evaluation populate the mesh.
Orphan assets (no binding) are preserved. Codegen double-build gotcha applies.

---

## Implementation Plan (Steps)

Each step: build clean (`scripts\build_ninja_win_vulkan.bat editor` **and**
`cmake --build build_vs2026_vulkan_headless --target editor --config Debug`),
headless MCP verification (kill stale editors + `get_server_info` pid/build
check first), independent diff review, commit. Keep the geometry-nodes
120-check smoke sweep green throughout. Restore
`desktop_window_imgui_host_imgui.ini` after runs; never commit
`config/editor/*.json`.

### B1 - Item types + `Graph_mesh` asset + Content-library folder (no behavior change)

- `item.hpp`: `index_graph_mesh = 43`, `index_geometry_graph_mesh = 44`,
  `count = 45`, bits + labels `"Graph_mesh"` / `"Geometry_graph_mesh"`.
- New `src/editor/geometry_graph/graph_mesh.{hpp,cpp}`: the asset class owning
  `Geometry_graph` + node vector + baked products + revision. Flags
  `show_in_ui | content`.
- `Content_library::graph_meshes` folder + parenting.
- Verify: builds green, folder visible, smoke sweep unaffected.

### B2 - Re-home graph state (window -> asset) + per-asset engine + create UI

- Window drops `m_graph`/`m_nodes` for `m_default_graph_mesh` (scratch) +
  `m_graph_mesh` (current), per-frame refresh, `get_current_graph_mesh()`
  re-refreshing per access (MCP calls arrive between frames).
- Low-level primitives (`insert_node`/`erase_node`/`connect_pins`/
  `disconnect_pins`/`set_node_position`) take `Graph_mesh&`; the five undo
  operations capture `shared_ptr<Graph_mesh>`; save/load/clear retarget.
- `update_evaluation()` scheduler over scratch + all assets;
  `Evaluation_run::target` asset; `wait_for_idle_evaluation()` loops all.
- Create UI: content-library right-click "Create Graph Mesh" (creates +
  selects) + palette toolbar "Editing asset:" label + "New Graph Mesh" button.
- Verify: smoke sweep still 120/120 against the scratch; create two assets,
  edit each independently, selection switches the canvas.

### B3 - `Geometry_graph_mesh` attachment + output-node publish

- New `src/editor/geometry_graph/geometry_graph_mesh.{hpp,cpp}`: attachment
  holding `shared_ptr<Graph_mesh>`, controlled sibling `Mesh` +
  `Node_physics`; `apply_baked_products()`; cleanup on detach/host change.
- `Geometry_output_node`: owning-asset `weak_ptr` (set at insert); asset-owned
  apply publishes products to the asset; engine pushes to bound attachments;
  scratch path unchanged.
- Properties: attachment panel (shows/rebinds the referenced asset via
  `combo<Graph_mesh>`).
- Verify: bind a node to an asset over a temporary path, graph edit re-renders
  the node's mesh (screenshot before/after), two nodes sharing one asset.

### B4 - MCP tools

- `create_graph_mesh` (execute_now + select; mirror `create_graph_texture`),
  `get_graph_meshes`, `set_node_graph_mesh { node_id|name, graph_mesh }` (+
  clear form; attaches/detaches the attachment undoably).
- Existing `geometry_graph_*` tools keep targeting the window's current
  (selection-driven) graph; `get_geometry_graph` reports which asset it
  describes (`graph_mesh_name`/`id`).
- `get_node_details`/`get_scene_nodes` report the attachment + bound asset for
  scripted assertions.
- Verify: create two assets by name over MCP, mutate each independently, bind
  a scene node, assert the back-reference in `get_node_details`.

### B5 - Scene serialization (asset + binding, scene_file v7)

- `definitions/graph_mesh_data.py` + `graph_mesh_binding_data.py`; two
  `added_in=7` Vectors on `scene_file.py`; version 7; regenerate (build twice).
- `scene_serialization.cpp`: write assets from `get_all<Graph_mesh>()` + one
  binding per attachment; read gated on `context`, name/id resolution with
  `warn` + skip; first evaluation repopulates meshes.
- Verify: save scene with bound nodes, reload, attachment still points at the
  asset and the mesh renders; malformed/missing entries degrade gracefully.

### B6 - Smoke coverage + full live verification

- Extend `scripts/geometry_nodes_smoke_test.py`: asset create/select/edit
  sections, attachment bind + re-evaluate + unbind, save/reload round-trip,
  shared-asset (two nodes) check; keep existing 120 checks green (scratch
  semantics preserved).
- Full headless end-to-end (erhe-headless-verify): create asset (UI path via
  MCP create + selection), edit graph, bind node, render (capture_screenshot),
  edit graph -> re-render changed, save + reload -> still bound + rendering.

---

## Verification Strategy

- The geometry-nodes 120-check smoke sweep is the regression net; it runs
  against the scratch graph and must stay green after every step. New checks
  cover asset + attachment + round-trip.
- Headless verification per step over the in-editor MCP server (headless
  Vulkan build), with the stale-editor hygiene protocol (kill editors,
  `get_server_info` pid/build assert) before every run.
- gtests only where logic is pure and a lib has a `test/` dir; the asset /
  attachment / engine wiring is editor-level and verified live.

---

## Key Files Reference

Touched / created (Phase B):

- `src/erhe/item/erhe_item/item.hpp` - `graph_mesh` + `geometry_graph_mesh`
  types (B1)
- `src/editor/geometry_graph/graph_mesh.{hpp,cpp}` (new) - the asset (B1)
- `src/editor/content_library/content_library.{hpp,cpp}` - `graph_meshes`
  folder (B1)
- `src/editor/geometry_graph/geometry_graph_window.{hpp,cpp}` - selected-asset
  editing, per-asset engine, create UI (B2)
- `src/editor/geometry_graph/geometry_graph_operations.{hpp,cpp}`,
  `geometry_graph_serialization.cpp` - retarget to `Graph_mesh` (B2)
- `src/editor/scene/scene_root.cpp` - right-click "Create Graph Mesh" (B2)
- `src/editor/geometry_graph/geometry_graph_mesh.{hpp,cpp}` (new) - the
  attachment (B3)
- `src/editor/geometry_graph/nodes/geometry_output_node.{hpp,cpp}` - publish to
  owning asset (B3)
- `src/editor/windows/properties.cpp` - attachment panel (B3)
- `src/editor/mcp/mcp_server.cpp` - create/list/bind tools (B4)
- `src/editor/scene/definitions/{graph_mesh_data,graph_mesh_binding_data,scene_file}.py`,
  `src/editor/scene/scene_serialization.cpp` - persistence, v7 (B5)
- `scripts/geometry_nodes_smoke_test.py` - asset/attachment coverage (B6)

Reference (the patterns being mirrored):

- `src/editor/texture_graph/graph_texture.{hpp,cpp}` - Phase A asset
- `src/editor/texture_graph/texture_graph_window.{hpp,cpp}` - selected-asset
  editing + scratch default
- `src/editor/scene/node_physics.hpp` - editor-defined `Node_attachment`
- `src/editor/scene/scene_serialization.cpp:820-853 / 1589-1635` - Phase A
  asset + binding persistence
- `src/editor/scene/scene_root.cpp:507-532` - "Create Graph Texture" menu
