# Graph editor

erhe's **graph editor** is a node-based editing surface built on the
`erhe::graph` DAG framework and the `erhe::imgui` node-editor canvas (an
`ax::NodeEditor` fork). Two feature graphs use it today, sharing one editor-level
layer:

- **Geometry graph** (`src/editor/geometry_graph/`) - authors procedural mesh
  geometry (primitives, Catmull-Clark / Conway operators, CSG, point
  distribution / instancing). Its output is a **`Graph_mesh`** content-library
  asset; a scene `Node` consumes it through a **Geometry Graph Mesh**
  attachment. See [`geometry-nodes-plan.md`](geometry-nodes-plan.md) and
  [`geometry-graph-mesh-plan.md`](geometry-graph-mesh-plan.md).
- **Texture graph** (`src/editor/texture_graph/`) - authors procedural textures
  by Material-Maker-style GLSL composition. Its output is a **`Graph_texture`**
  content-library asset; a `Material` texture slot sources from it. See
  [`texture-graph-plan.md`](texture-graph-plan.md) and
  [`graph-texture-plan.md`](graph-texture-plan.md).

A third, older **shader graph** (`src/editor/graph/`, `Graph_window`) is the
prototype the other two were forked from. It predates the current
asset/undo/serialization model and is intentionally *not* built on the shared
layer (see [Legacy shader graph](#legacy-shader-graph)).

The editor-level machinery that used to be copy-pasted between the geometry and
texture graphs now lives in a shared **`src/editor/graph_editor/`** layer
(namespace `editor`). This document describes the graph editor and its features
as built; the shared layer was extracted in "Phase C" (commits
`5a211b01`..`f85e3f56` on the `crease` branch - see the git history, which also
contains this file's predecessor `graph-editor-shared-plan.md` and its
step-by-step refactoring plan).

## Table of Contents

1. [User-facing features](#user-facing-features)
2. [Architecture](#architecture)
3. [The shared layer (`src/editor/graph_editor/`)](#the-shared-layer-srceditorgraph_editor)
4. [Per-editor code](#per-editor-code)
5. [Assets, attachments and the consumption model](#assets-attachments-and-the-consumption-model)
6. [Evaluation](#evaluation)
7. [Serialization and persistence](#serialization-and-persistence)
8. [MCP surface](#mcp-surface)
9. [Legacy shader graph](#legacy-shader-graph)
10. [Verification](#verification)
11. [Future development](#future-development)
12. [Key files](#key-files)

---

## User-facing features

Common to both the geometry and texture graph windows:

- **Node canvas.** A pannable / zoomable `ax::NodeEditor` canvas. Each node draws
  a header row, input pins on the left edge, output pins on the right edge, and
  its parameter widgets in the center. A zoom overlay shows the current zoom in
  the corner. Zoom is authored in screen space so frames, pins and widgets scale
  together (Issue #251).
- **Node palette** (companion window). A searchable, categorized list - a filter
  box plus one collapsing header per category whose entries spawn a node. Lives
  in a separate `Graph_editor_palette_window` so the palette and the canvas can
  be docked / sized independently.
- **Canvas "Add node" context menu.** Right-click the canvas background for a
  submenu-per-category menu that spawns a node at the cursor. Shared by both
  windows.
- **Type-safe, color-coded pins.** Every pin has a *key*; pins only connect to
  pins with the same key, and each key has a fill color so legal connections are
  visible before a drag is even attempted. A link that would create a cycle is
  refused.
- **Undo / redo.** Node add / delete (with its links and canvas position),
  link connect / disconnect, and parameter edits are each undoable operations on
  the shared `Operation_stack`. A parameter edit commits **one** operation per
  completed widget gesture (on widget deactivation).
- **Parameter widgets.** Because `ax::NodeEditor` cannot host normal ImGui popups
  inside the canvas, node content uses canvas-safe **stepper** widgets
  (left/right arrows cycling an enum / index) instead of combos, plus drag
  floats/ints and (texture only) gradient and curve editors.
- **Explicit per-window target asset (Issue #252).** Each window edits one
  explicit target asset (a `Graph_mesh` / `Graph_texture`), chosen with a target
  selector row at the top of the window (drag-drop, pick, or clear) - decoupled
  from the global selection. This fixed a bug where selecting the graph asset put
  both the asset and its nodes in the selection, so a single Delete removed the
  whole asset. Node selection now lives purely in the canvas.
- **Multiple window instances.** "Open Editor" (content-library context menu /
  double-click) opens additional graph windows on other assets via the
  `Editor_windows` manager (Issue #252). The primary window persists its layout;
  extra instances get a unique title and no ini label.
- **Content-library assets.** A graph lives only as a selectable, serializable
  content-library asset. Create one with the folder's right-click
  "Create Graph Mesh" / "Create Graph Texture" (which also selects + targets it),
  or over MCP.

Geometry-graph-specific:

- Node categories: **Primitives** (box, sphere, torus, cone, disc),
  **Operations** (subdivide, conway, transform, triangulate, normalize, reverse,
  repair), **Points** (distribute, instance, realize), **Combine** (join,
  boolean), **Values** (float, integer, vector, math), **Groups** (group_input,
  group_output, group), **Output**.
- The output node can attach the baked mesh to a scene `Node` and optionally give
  it physics (static / kinematic / dynamic from a generated convex hull).
- **Group nodes** load a sub-graph from a file and evaluate it inline.

Texture-graph-specific:

- Nodes are data-driven from a descriptor registry (generators, filters, SDF,
  color, etc.), plus explicit sink nodes (output, material output, buffer).
- Gradient and curve parameter editors (canvas-safe custom widgets).
- Node preview thumbnails render each node's composed subtree.
- A "Reseed all" control re-randomizes seeded nodes.

---

## Architecture

### `erhe::graph` foundation

The low-level DAG lives in `src/erhe/graph/` (`erhe::graph`): `Graph` (owns
`Node*` + `Link`, provides `connect` / `disconnect` / `sort` /
`would_create_cycle` / `register_node`), `Node` (typed input/output `Pin`s, a
unique graph id), `Pin` (a connection point with a *key* and a slot index), and
`Link` (a directed source-pin -> sink-pin connection). It is payload-agnostic and
also underpins the render graph. See `src/erhe/graph/notes.md`.

The editor undo system (`src/editor/operations/`) - `Operation` and
`Operation_stack` - is likewise payload-agnostic and used unchanged.

### Layering

```
erhe::graph  (Graph / Node / Pin / Link)         <- shared low-level DAG
   |
src/editor/graph_editor/  (shared editor layer)   <- this document
   |                    \
geometry_graph/          texture_graph/            <- per-feature code
   |                          |
Graph_mesh asset          Graph_texture asset       <- content-library assets
   |                          |
Geometry Graph Mesh       Material texture slot      <- scene consumers
 node attachment           (texture_source)
```

The split follows one rule: **payload-blind machinery is shared; the payload and
its genuine per-editor seams stay per-editor** (behind a template parameter or a
virtual hook). "Payload" means the value that flows along a link - a
`Geometry_payload` (a value `std::variant` of geometry / scalar / vector /
material / point-cloud / instances) versus a `Texture_payload` (a lazy reference
to a producing node's output, composed to GLSL at sinks). These are different in
*kind*, so payload storage, fan-in accumulation, the node factory and the
evaluation strategy are never unified - only the machinery around them is.

---

## The shared layer (`src/editor/graph_editor/`)

| File | Role |
|------|------|
| `graph_editor_widgets.{hpp,cpp}` | Canvas-safe stepper widgets `imgui_index_stepper` / `imgui_enum_stepper` (ImGui-only, payload-agnostic), shared by every node's parameter UI. |
| `graph_asset.hpp` | `Graph_asset<Self, GraphT, NodeT>` - CRTP base for the content-library graph assets. Holds the graph + node vector + accessors + constructors + Item identity. |
| `graph_serialization.hpp` | `write_graph_asset_json` / `read_graph_asset_json` function templates - the v1 node/link JSON format with graceful degradation, shared by both assets' serializers. |
| `graph_operations.hpp` | Templated undo operations (`Graph_node_insert_remove_operation`, `Graph_parameter_operation`, `Graph_link_insert_remove_operation`) + link record, parameterized on a per-editor `Traits`. |
| `graph_editor_node.{hpp,cpp}` | `Graph_editor_node : erhe::graph::Node` - the payload-agnostic node base: canvas rendering (`node_editor` / `show_pins`), dirty flag, factory type name, parameter (de)serialization + undo-commit plumbing. |
| `graph_editor_window_base.{hpp,cpp}` | `Graph_editor_window_base : erhe::imgui::Imgui_window` - the window base: the node palette, the canvas "Add node" context menu, and the `controls_imgui` seam the palette window forwards to. |
| `graph_editor_palette_window.{hpp,cpp}` | `Graph_editor_palette_window` - one companion palette window that forwards `imgui()` to a `Graph_editor_window_base&`'s `controls_imgui()`. |

Design notes:

- **Templates vs hooks.** Type-parameterized pieces (asset base, serializer, undo
  operations) are **templates** - zero runtime cost, and distinct instantiations
  sidestep the ODR clash that once forced the stepper widgets to be renamed
  apart. The **node base and window base are non-template** classes with a few
  **virtual hooks**, so their large bodies compile once and the derived classes
  stay small.
- **`Graph_editor_node` hooks (3).** `pin_key_color(key)` (the per-editor pin-key
  -> color table), `commit_parameter_operation(...)` (build + push the concrete
  parameter undo op), and `after_node_content(...)` (optional extra content after
  the node's widgets - the texture graph draws its preview thumbnail here).
  `mark_dirty()` is virtual so the texture node can additionally invalidate its
  preview cache. The concrete `Geometry_graph_node` / `Texture_graph_node` add
  payload storage, input pulling, `evaluate()`, the owning-asset back-link, and
  these hooks - so the ~34 concrete node classes derive from them unchanged.
- **`Graph_editor_window_base` hooks (palette).** `build_palette()` fills the
  categories (geometry's static list vs texture's descriptor-registry scan) and
  `add_node_from_palette(type_name)` spawns the chosen type via the concrete
  factory + an undoable insert. The palette render loop and the background
  context menu are shared and payload-blind.
- **`Graph_asset<Self, GraphT, NodeT>`.** Derives
  `erhe::Item<Item_base, Item_base, Self, not_clonable>`, threading `Self` so
  `get_type()` / `get_type_name()` resolve to each asset's static type. Holds
  `m_graph` + `m_nodes` + `graph()` / `nodes()`; the *consumption model* stays in
  the derived asset (see below).
- **Undo op traits.** Each editor's `*_graph_operations.hpp` is now just a
  `Traits` class (`Window` / `Asset` / `Node` types + a `label` string) plus
  `using` aliases that keep the original concrete operation names, so the window
  construction sites are unchanged.

---

## Per-editor code

What stays specific to each editor:

- **Payload type** and **fan-in.** `Geometry_payload` (value variant, `operator+=`
  merges multi-link inputs) vs `Texture_payload` (reference handle, single-link
  last-wins). The node base stores `std::vector<Payload>` per pin slot; the
  concrete node supplies the accumulation.
- **Node factory.** `make_geometry_graph_node` (a ~25-way `if/else` over concrete
  node classes) vs `make_texture_graph_node` (a descriptor-registry lookup
  building a generic `Texture_descriptor_node`, plus a few bespoke sink nodes).
- **Pin keys / colors.** `Geometry_pin_key` (10 keys) vs `Texture_pin_key` (3,
  mirroring `erhe::texgen::Value_type`).
- **The `*_graph` dirty-eval wrapper.** `Geometry_graph` / `Texture_graph` (both
  `: erhe::graph::Graph`) add the dirty-flag propagation used by evaluation.
- **The output / sink node** and the **evaluation strategy** (below).
- **The window's canvas render loop, link create/delete, target model, and
  per-frame evaluation driver** - see [Future development](#future-development);
  today these are still per-window (the palette and context menu are the shared
  parts of the window).

---

## Assets, attachments and the consumption model

Both graphs produce a content-library asset, but they are consumed differently -
deliberately.

**Texture graph -> pull.** `Graph_texture` also implements
`erhe::graphics::Texture_reference`; its `get_referenced_texture()` returns the
graph output node's most recently baked texture. A `Material` slot holds a
`texture_source` beside its raw `texture`, and the renderer resolves the
reference every frame. Editing the graph updates every material that sources it
with no push logic - a texture reference is cheap to resolve per frame.

**Geometry graph -> push.** Producing a mesh (geometry copy + process + GPU
primitive build + optional collision shape) is expensive and runs through an
explicit push pipeline. `Graph_mesh` stores the **baked products**
(`geometry`, `primitive`, `material`, `collision_shape`, physics settings) plus a
**revision** counter. A scene `Node` gets an editor-defined
**`Geometry_graph_mesh`** `Node_attachment` (the `Node_physics` precedent) that
points back at the `Graph_mesh` and swaps its controlled `Mesh`'s primitives (and
keeps `Node_physics` in sync) whenever the revision advances. N scene nodes can
share one graph asset (the products are shared `shared_ptr`s). `erhe::scene` is
untouched - the attachment lives entirely in `src/editor/`.

`Item_type` indices: `graph_texture` = 42, `graph_mesh` = 43,
`geometry_graph_mesh` = 44 (`src/erhe/item/erhe_item/item.hpp`).

---

## Evaluation

Nodes are born dirty; a parameter edit calls `mark_dirty()`. Evaluation walks the
graph in topological order, re-running dirty nodes and everything downstream,
while clean nodes keep their cached output payloads. The two editors differ in
*how* they run this:

- **Geometry graph - asynchronous, shadow-clone.** `Geometry_graph_window` owns a
  background engine: once per frame it snapshots the live graph into a shadow
  clone and evaluates it on a `tf::Executor` worker (at most one run in flight).
  The worker never touches live nodes, so the canvas, undo/redo and MCP stay
  interactive during evaluation; on completion the payloads (and the output
  node's evaluated scene products) are copied back on the main thread. The output
  node is two-phase: the worker builds the geometry / primitive / hull, the main
  thread applies them to the asset and pushes to bound attachments. MCP queries
  that need settled state (`get_geometry_graph`, save) block on
  `wait_for_idle_evaluation()` - eventual-consistency is the MCP contract.
- **Texture graph - synchronous.** Composition is cheap (string GLSL assembly);
  `Texture_graph_window::update()` evaluates dirty nodes and renders their
  previews / bakes inline each frame via a `Texture_renderer`. No async barrier is
  needed.

The evaluation strategy is the single biggest divergence between the two and
stays per-editor.

---

## Serialization and persistence

A graph is serialized as a JSON string blob (its nodes with factory type +
parameters, its links by node index + pin slot; canvas positions are not stored -
a loaded graph lays out on a spawn grid). The shared
`write_graph_asset_json` / `read_graph_asset_json` templates implement this once;
`read` validates version, node types, pin slots and keys before mutating the live
graph, and degrades gracefully (an unknown node type or a cyclic link is refused,
not accepted).

The assets and their bindings persist in the **scene file** (erhe-authored
`.glb`, `doc/scene_serialization.md`): the root-level `ERHE_node_graphs`
extension carries `graph_textures` / `graph_meshes` (the embedded node-graph
JSON) plus `material_bindings` (glTF material index + slot -> texture asset)
and `node_bindings` (glTF node index -> mesh asset); spec in
`doc/gltf_extensions/ERHE_node_graphs.md`, writer/reader in
`src/editor/parsers/gltf_extensions_export.cpp` /
`gltf_extensions_import.cpp`. Load reconstructs the assets, re-resolves the
bindings (warn + skip on a mismatch; bindings on materials no mesh references
are dropped at save with a warning), and lets the first evaluation re-bake.

---

## MCP surface

The in-editor MCP server exposes the graph editors for scripted setup and
headless verification. Per editor: `get_*_graph`, `set_*_graph_target`,
`*_graph_add_node` / `remove_node` / `set_parameter` / `connect` / `disconnect`,
`create_graph_*` (creates + selects + targets), `get_graph_*` (list), the binding
tool (`set_material_texture_source` / `set_node_graph_mesh`), and
`open_*_graph_window`. Geometry adds `geometry_graph_set_view` (canvas zoom);
texture adds `texture_graph_export_png` / `export_material` and `reseed_all`.
Graph-mutation tools target the window's current target asset; asset-scoped tools
resolve by `scene_name` + `name` via the shared `find_library_item` /
`find_content_library_asset` helpers. Implementations are in
`src/editor/mcp/mcp_server_graphs.cpp`.

---

## Legacy shader graph

`src/editor/graph/` (`Graph_window`, `Shader_graph`, `Shader_graph_node`) is the
original prototype. It predates the current model and shares almost nothing
extractable: its nodes have no dirty flag, no parameter (de)serialization, no
factory type name, no owning asset and no preview; its payload is a fixed
4-int/4-float register (not a variant); `get_input` / `get_output` return by
value; `show_pins` bundles its arguments in a `Node_context`; it still syncs the
global selection (the behavior the geometry/texture graphs removed in Issue #252);
and it supports 4-edge pin placement. It does not even use the stepper widgets. It
is intentionally **not** built on the shared layer. If it is ever modernized into
an asset-homed graph it can adopt the shared pieces then; nothing depends on it
doing so.

---

## Verification

The two graph editors are covered by headless MCP smoke sweeps driven through the
in-editor MCP server (headless Vulkan build):

- `scripts/geometry_nodes_smoke_test.py` - **129 checks** (every node type,
  parameter sweeps with undo/redo, incremental-evaluation proof, multi-link join,
  structural churn with deep undo/redo, invalid-connect rejection, the Graph Mesh
  asset + attachment create/bind/re-bake/physics/unbind + scene save/load
  round-trip, stress chains).
- `scripts/texture_graph_smoke_test.py` - **266 checks** (every node's pins +
  defaults, parameter round-trips, connect/disconnect rules, undo/redo, and the
  "it actually renders" `export_png` pixel checks + the Graph_texture asset +
  material-source save/load round-trip).

Gotchas worth knowing:

- **Run each sweep in a fresh editor process.** Running the texture sweep in the
  same process right after the geometry sweep hits a known texture-bake pollution
  (~11 `has_output:false` failures from the geometry sweep's heavy GPU work).
- The geometry sweep's two "incremental" checks need
  `editor.graph_editor=trace` in `config/editor/logging.json` (set, run, revert).
- `get_server_info`'s `build` timestamp lags (it is the `__DATE__` TU compile
  time, not the link time) - assert the reported `pid` instead.
- See the `erhe-headless-verify` skill for the full build -> launch -> drive ->
  screenshot -> clean-up loop, and always
  `git checkout -- config/editor/*.{ini,json}` after an editor run.

---

## Future development

- **Finish the window base ("C7-remainder").** The canvas render loop (`imgui()`
  Begin/End + node iteration + link drawing + zoom overlay), link create/delete
  (`handle_link_create` / `handle_deletions`), and the node-position helpers are
  still copied between the two windows. Because `Graph_editor_node` made node
  iteration payload-blind (the loop can cast to the shared base), these are
  extractable into `Graph_editor_window_base` given a small set of hooks
  (`graph()` -> `erhe::graph::Graph*` with an empty-state null, `connect` /
  `disconnect` / `remove_node`) plus moving `m_node_editor` (the `ax::NodeEditor`
  context) into the base. Deferred because it touches the interactive per-frame
  render path next to the geometry graph's async engine and the #252 target
  model; do it as its own commit and verify with both sweeps + a canvas
  screenshot. The target model, evaluation strategy and the ~15 MCP-facing
  methods stay per-editor regardless.
- **Dedup the MCP / create-UI / scene-save-load boilerplate ("C8").** The ~9 twin
  MCP tool bodies (`mcp_server_graphs.cpp`), the two `scene_root` "Create Graph *"
  context-menu branches, and the parallel scene save/load blocks are payload-blind
  over `<Asset, Window, folder-member, write/read fn>`. Lower value (short bodies,
  more surface); builds on the already-shared asset-lookup helpers.
- **Relocate the gradient / curve editors.** `texture_gradient_editor` /
  `texture_curve_editor` (currently in `texture_graph_widgets.*`) are canvas-safe
  widgets like the steppers; move them into `graph_editor_widgets` if a second
  consumer appears (they carry an `erhe_texgen` dependency, so kept out of the
  otherwise-generic shared widgets header for now).
- **Modernize or retire the shader graph.** Either retrofit `src/editor/graph/`
  onto the shared layer (it would need dirty/param/undo/factory/asset machinery
  added) or remove it once nothing needs it.
- **A fifth graph feature** (whatever it is) should be built directly on the
  shared layer - a payload type + node set + factory + evaluation strategy + an
  asset with its consumption model - rather than a fresh copy.

---

## Key files

Shared low layer (unchanged): `src/erhe/graph/`,
`src/editor/operations/operation.hpp` + `operation_stack.cpp`.

Shared editor layer: `src/editor/graph_editor/*` (see the table above).

Geometry graph: `src/editor/geometry_graph/` - `geometry_graph`,
`geometry_graph_node`, `geometry_graph_node_factory`, `geometry_graph_operations`
(traits), `geometry_graph_window`, `geometry_payload`, `graph_mesh`,
`graph_mesh_serialization`, `geometry_graph_mesh` (the attachment), `nodes/*`.

Texture graph: `src/editor/texture_graph/` - `texture_graph`,
`texture_graph_node`, `texture_graph_node_factory`, `texture_graph_operations`
(traits), `texture_graph_window`, `texture_graph_widgets`, `texture_payload`,
`graph_texture`, `graph_texture_serialization`, `texture_renderer`,
`texture_graph_compose`, `nodes/*`. Pure codegen core: `src/erhe/texgen/`.

Legacy shader graph: `src/editor/graph/*`.

Wiring: `src/editor/editor.cpp` (window construction),
`src/editor/windows/editor_windows.cpp` (multi-instance windows, #252),
`src/editor/scene/scene_root.cpp` (content-library Create menu),
`src/editor/content_library/content_library.{hpp,cpp}` (asset folders),
`src/editor/parsers/gltf_extensions_export.cpp` / `gltf_extensions_import.cpp`
(persistence, `ERHE_node_graphs` extension - see `doc/scene_serialization.md`),
`src/editor/mcp/mcp_server_graphs.cpp` (MCP tools),
`src/erhe/item/erhe_item/item.hpp` (`Item_type` indices 42/43/44).
