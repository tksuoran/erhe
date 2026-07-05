# Shared graph-editor layer (Phase C: de-duplicate the three graph editors)

The editor has grown **three** consumers of the `erhe::graph` DAG framework, and
the editor-level machinery around each is copied, not shared:

- the old **shader graph** (`src/editor/graph/`, `Graph_window`) - the original
  prototype the others were forked from;
- the **geometry graph** (`src/editor/geometry_graph/`, `Geometry_graph_window`)
  + its `Graph_mesh` asset (Phase B);
- the **texture graph** (`src/editor/texture_graph/`, `Texture_graph_window`)
  + its `Graph_texture` asset (Phase A).

Phase A (`doc/graph-texture-plan.md`) and Phase B
(`doc/geometry-graph-mesh-plan.md`) each *added a full copy* of the editor-level
pattern (node base, undo operations, JSON serialization, palette window, the
asset-owns-the-graph model, content-library create UI, scene serialization, MCP
create/list/bind tools). Phase A even had to **rename** the canvas stepper
widgets (`texture_index_stepper` / `texture_enum_stepper`) to dodge an ODR clash
with the geometry graph's identically-shaped `imgui_index_stepper` /
`imgui_enum_stepper`. `doc/texture-graph-plan.md` "Architecture Decisions" #5
deliberately deferred a shared extraction "until we have real examples to
validate the abstraction against." After A and B there are three (and, counting
the asset variants, effectively four) concrete consumers, so the precondition is
met.

**Goal:** extract the payload-agnostic editor-level graph machinery into a shared
base/templated layer so a fifth graph feature is not a fifth copy, WITHOUT
regressing the existing consumers. This is a **pure internal refactor** - no
user-visible behavior change - so "no behavior change" is itself the assertion
the two smoke sweeps verify after every step.

## Table of Contents

1. [Implementation Status](#implementation-status)
2. [Current State (what exists today)](#current-state-what-exists-today)
3. [Architecture Decisions](#architecture-decisions)
4. [Implementation Plan (Steps)](#implementation-plan-steps)
5. [Verification Strategy](#verification-strategy)
6. [Key Files Reference](#key-files-reference)

---

## Implementation Status

| Work item                                                                    | Status | Commit |
|------------------------------------------------------------------------------|--------|--------|
| C1: Shared canvas steppers - removes the ODR rename hack                       | DONE   | (this commit) |
| C2: Shared palette window + `Graph_editor_window_base` (controls_imgui seam)  | TODO   | -      |
| C3: Shared asset base `Graph_asset<Self, Graph, Node>`                        | TODO   | -      |
| C4: Shared graph-JSON serializer template                                     | TODO   | -      |
| C5: Shared undo-operations template                                           | TODO   | -      |
| C6: Shared node base `Graph_node<Traits>` (concrete names preserved as alias) | TODO   | -      |
| C7: Shared window base - payload-blind canvas/link/palette/target machinery   | TODO   | -      |
| C8 (optional): MCP + create-UI + scene save/load helper dedup                 | TODO   | -      |

Steps land smallest/safest first, each its own commit, both smoke sweeps green
after each. C7 is the capstone and the riskiest; C6/C7 may be split further or
partially deferred if risk outweighs benefit (erhe "stop at diminishing returns"
discipline, precedent: the Catmull-Clark performance pass).

---

## Current State (what exists today)

Sizes: geometry_graph ~3250 lines, texture_graph ~3850, shader graph ~1540. The
`*_graph_operations.cpp` are both exactly 183 lines; the `*_graph.cpp` both 76;
the two graph-JSON serializers ~160 each - byte-level copy-paste.

**Payload-blind (near-byte-identical between geometry and texture), the dedup
target:**

- **Canvas steppers.** `imgui_index_stepper` / `imgui_enum_stepper`
  (`geometry_graph_node.{hpp,cpp}`) vs `texture_index_stepper` /
  `texture_enum_stepper` (`texture_graph_node.{hpp,cpp}`) - byte-identical, only
  renamed to avoid the ODR clash (both are external-linkage free functions in
  `namespace editor` compiled into one editor library). 5 geometry + 8 texture
  call sites. Fully ImGui-only, no payload/editor type referenced.
- **Palette window.** `Geometry_graph_palette_window` /
  `Texture_graph_palette_window` are byte-for-byte identical modulo the window
  type; each just forwards `imgui()` to `m_graph_window.controls_imgui()`.
- **Graph-JSON serializer.** `write/read_graph_texture_graph` vs
  `write/read_graph_mesh_graph` - line-for-line identical over 6 token
  substitutions (asset type, graph type, node type, factory fn, owning-setter,
  log label). Same version check, same "build unregistered then validate then
  swap" graceful-degradation, same pin-key/slot/cycle rejection.
- **Undo operations.** One link-record struct + one maker + three `Operation`
  subclasses (`*_node_insert_remove_operation`, `*_parameter_operation`,
  `*_link_insert_remove_operation`) - 1:1 identical modulo `{Node, Asset,
  Window}` type tokens + the description-prefix string. `Operation` /
  `Operation_stack` are already payload-agnostic and need no change.
- **`*_graph` dirty-eval wrapper.** `Geometry_graph` / `Texture_graph` (both
  `: erhe::graph::Graph`) - `evaluate_if_dirty` / `mark_dirty` /
  `consume_forced_full` / `is_evaluation_needed` / `evaluate` identical modulo
  the concrete node `dynamic_cast` and one `get_log_id()` vs `get_id()`
  divergence + a log-prefix literal.
- **Node base.** `Geometry_graph_node` / `Texture_graph_node` - `node_editor`,
  `show_pins`, `make_*_pin`, `pull_inputs`, dirty flags,
  `write/read/dump/committed_parameters`, `factory_type_name`,
  `on_removed_from_graph` are byte-identical. Per-editor: the payload type, the
  fan-in accumulation, `pin_key_color`, the owning-asset type, `evaluate(Graph&)`,
  and the concrete parameter-undo-op construction.
- **Window.** `Geometry_graph_window` / `Texture_graph_window` ~70% identical:
  canvas begin/end + node iteration + link drawing, link create/delete (incl.
  cycle guard), the palette, the Issue-#252 selected-target model
  (`resolve_target` / `set_target` / `target_selector_imgui` / empty state),
  `set_node_parameters`, `get_node_position` / `next_node_spawn_position`, and
  the whole MCP-facing method surface. Constructors are identical:
  `(imgui_renderer, imgui_windows, App_context&, title, ini_label)`.
- **Asset class.** `Graph_texture` / `Graph_mesh` share `erhe::Item<...,
  not_clonable>` + `m_graph` + `m_nodes` + `graph()`/`nodes()` accessors +
  byte-identical constructors (`name` + `show_in_ui | content`).
- **Scene save/load, create UI, MCP tools.** Parallel geometry/texture blocks
  keyed on the folder member + write/read fn + data struct + window; ~9 twin MCP
  tool bodies; the asset-lookup helpers (`find_library_item`, `find_scene`,
  `find_content_library_asset`) are already shared.

**Genuine per-editor seams (stay per-editor, or behind a hook - NOT shared
code):**

- **Node factory dispatch** - geometry's ~25-way `if/else` over concrete node
  classes vs texture's descriptor-table lookup (`get_texture_node_descriptor`).
  Different dispatch strategies over disjoint concrete types; only the outer
  contract (nullptr-on-unknown, set-factory-name-on-success) is common and is
  too thin to be worth extracting.
- **Payload types** - `Geometry_payload` is a value `std::variant` of 11 result
  alternatives with a merging `operator+=`; `Texture_payload` is a *reference
  handle* (`source_node` + `output_index` + `value_type`) resolved lazily at
  sinks; the shader graph's `Payload` is a fixed 4-int/4-float register. Only the
  "accumulate on fan-in" concept recurs; the storage is genuinely different in
  kind. **Not unified**; fan-in is a node-base hook.
- **`pin_key_color`** - same function shape, per-editor color table over disjoint
  pin-key enums (`Geometry_pin_key` 10 keys / `Texture_pin_key` 3). A trait/hook,
  not shared code.
- **`build_palette()` data** - geometry hard-codes 7 categories / 24 types;
  texture builds from the descriptor registry. Payload seam; the palette
  *consumption* (`node_palette()`) is shared.
- **Evaluation strategy** - the single biggest divergence. Geometry owns a full
  background **async shadow-clone engine** (`Evaluation_run`, `context.executor`,
  `launch/finish_evaluation`, `wait_for_idle_evaluation`, attachment push, plus
  `Geometry_output_node` / `Group_node` casts in `finish_evaluation`). Texture
  does **synchronous** per-frame evaluation into a `Texture_renderer`. The shader
  graph evaluates inline in `imgui()`. This stays a per-editor virtual hook.

**Consumption model asymmetry (Phase A vs B):** `Graph_texture` is a **pull**
reference (`erhe::graphics::Texture_reference::get_referenced_texture()`, the
renderer resolves per frame); `Graph_mesh` is a **push** producer (baked-products
+ revision + a companion `Geometry_graph_mesh` `Node_attachment` that materializes
the mesh + rigid body). This is deliberate (a texture reference is cheap to
resolve per frame; a mesh + collision shape is not) and stays per-asset.

**The shader graph is a degenerate predecessor, not a peer.** `src/editor/graph/`
predates the dirty-flag/param-JSON/undo/factory/asset model: its nodes have no
dirty flag, no `write/read_parameters`, no factory type name, no owning asset, no
preview; its payload is a fixed struct not a variant; its `get_input/get_output`
return by value; its `show_pins` bundles args in a `Node_context`; it still syncs
the global selection (the behavior geometry/texture deliberately removed in Issue
#252); and it supports 4-edge pin placement. It does not even use the steppers.
Retrofitting it onto the shared layer is high-cost / low-value and out of scope;
it is left as-is (see decision 7).

---

## Architecture Decisions

### 1. Shared home: `src/editor/graph_editor/`, `namespace editor`; no new erhe library

`erhe::graph` is already the shared low-level DAG layer and needs no change
(`Graph` / `Node` / `Pin` / `Link` and `Operation` / `Operation_stack` are
payload-agnostic). The duplication is entirely at the **editor** level, so the
shared code lives in a new `src/editor/graph_editor/` directory in
`namespace editor`, alongside the per-editor `geometry_graph/` and
`texture_graph/`. Sources are listed explicitly in `src/editor/CMakeLists.txt`
(no globbing).

### 2. Templates for type-parameterized code; virtual hooks for the big window body

The payload types are known at compile time, so the type-parameterized pieces
(node base, `*_graph` wrapper, undo operations, asset base, serializer) become
**templates** - zero runtime cost, and distinct instantiations sidestep the
ODR clash that forced the stepper rename. The **window** body is large (700-950
lines) and its per-editor seams (factory, palette data, evaluation strategy) are
better expressed as **virtual hooks** on a non-template base compiled once, so
the derived windows stay small and compile times stay sane. See C7 for the exact
split.

### 3. Preserve the concrete type names as aliases - the key risk-minimizer

Every concrete node class (`Mesh_box_node`, ... ~24 geometry + ~10 texture) and
every call site names `Geometry_graph_node` / `Texture_graph_node` /
`Geometry_graph` / `Texture_graph`. The templated bases are introduced *under*
those names via `using` aliases (e.g. `using Geometry_graph_node =
Graph_node<Geometry_graph_traits>;`), so the ~34 concrete node files, the
factories, the windows, the serializers and the MCP layer are **not touched** by
the base extraction. This keeps each step a small, reviewable diff and lets the
smoke sweeps localize any regression.

### 4. Evaluation strategy stays per-editor behind a hook

The async shadow-clone engine is geometry-only, ~250 lines, and has no texture
twin; texture's synchronous render is cheap and GPU-coupled. The shared window
base exposes a single per-frame `update_evaluation()` (or equivalently named)
pure-virtual hook; geometry implements the async engine, texture the synchronous
render, and (if ever retrofitted) the shader graph the trivial inline path. The
engine internals are NOT extracted.

### 5. Node factory and payload types stay per-editor

The factory dispatch bodies (if-chain vs descriptor table) and the payload
storage (value variant vs reference handle) are intrinsically payload-specific
(decision rationale under "genuine seams" above). The node base takes the
payload type as a template parameter and delegates fan-in accumulation and
`make_node` to hooks; it does not try to unify the dispatch or the storage.

### 6. Asset base unifies ownership + Item identity; the consumption model stays derived

`Graph_asset<Self, GraphT, NodeT> : erhe::Item<Item_base, Item_base, Self,
not_clonable>` holds `m_graph` + `m_nodes` + the two ctors + `graph()`/`nodes()`.
`Graph_texture` additionally derives `erhe::graphics::Texture_reference` (pull);
`Graph_mesh` additionally holds the baked-products/revision/push API and its
companion attachment. The CRTP `Self` parameter keeps `get_static_type()` /
`static_type_name` correct per asset.

### 7. The shader graph is left as-is (explicitly out of scope)

For the reasons in "Current State", `src/editor/graph/` shares almost nothing
extractable and retrofitting it would mean *adding* dirty/param/undo/factory/asset
machinery it does not have. It is intentionally not migrated. If it is ever
modernized into an asset-homed graph, it can adopt the shared layer then; this
plan does not block on it. (It does not use the steppers, so even C1 does not
touch it.)

### 8. Pure internal refactor; the two smoke sweeps are the regression net

No user-visible behavior changes. `scripts/geometry_nodes_smoke_test.py`
(126 checks) and `scripts/texture_graph_smoke_test.py` (266 checks) plus the
build matrix (ninja vulkan + headless Vulkan) are run green after **every** step.
Each extraction validates against **both** the geometry and texture consumers at
once (not one), so the abstraction is proven by more than one example. Follow the
verification-hygiene protocol (kill editors + `get_server_info` pid/build check)
for every headless run.

---

## Implementation Plan (Steps)

Each step: build clean (`scripts\build_ninja_win_vulkan.bat editor` AND
`cmake --build build_vs2026_vulkan_headless --target editor --config Debug`),
headless-verify both smoke sweeps green, independent review of the diff, commit.
Restore `config/editor/*.{ini,json}` after every editor run.

### C1 - Shared canvas steppers (lowest risk; removes the ODR rename)

New `graph_editor/graph_editor_widgets.{hpp,cpp}` holding ONE
`imgui_index_stepper` / `imgui_enum_stepper` pair (the geometry names, the more
general `imgui_` prefix), ImGui-only (no `erhe_texgen` include - the steppers are
fully generic).

Delete the stepper defs/decls from `geometry_graph_node.{hpp,cpp}` and
`texture_graph_node.{hpp,cpp}`. Repoint the 5 geometry `imgui_*` + 8 texture
`texture_*` stepper call sites to the shared header. Redirect
`texture_graph_widgets.cpp`'s `texture_graph_node.hpp` include (pulled in only for
the stepper at line 230) to the shared widgets header. Add the new
`graph_editor/graph_editor_widgets.*` CMake entries.

The texture-unique `texture_gradient_editor` / `texture_curve_editor` stay in
`texture_graph_widgets.{hpp,cpp}` for now: they are NOT duplicated (one copy, one
consumer) so relocating them yields no dedup value and would drag an `erhe_texgen`
dependency into the otherwise-generic shared widgets header. Relocating them into
the shared canvas-widget home is deferred (a trivial move if a second consumer
ever appears).

### C2 - Shared palette window + `Graph_editor_window_base` seam

Introduce a minimal `Graph_editor_window_base : erhe::imgui::Imgui_window` with
`virtual void controls_imgui() = 0` (and later the C7 hooks). Both windows derive
from it (they already have `controls_imgui()`). Replace
`Geometry_graph_palette_window` / `Texture_graph_palette_window` with one
`graph_editor/graph_editor_palette_window.{hpp,cpp}` holding a
`Graph_editor_window_base&` and forwarding `imgui()` to its `controls_imgui()`.
Update the two windows' companion-palette members + `editor.cpp` construction.

### C3 - Shared asset base `Graph_asset<Self, Graph, Node>`

New `graph_editor/graph_asset.hpp`. `Graph_texture` becomes `: public
Graph_asset<Graph_texture, Texture_graph, Texture_graph_node>, public
erhe::graphics::Texture_reference`; `Graph_mesh` becomes `: public
Graph_asset<Graph_mesh, Geometry_graph, Geometry_graph_node>` + its push API.
The base provides the ctors, `m_graph`/`m_nodes`, and `graph()`/`nodes()`.
Item-identity (`static_type_name`, `get_static_type`) stays on the concrete
class. Verified by create/list + scene save/load in both sweeps.

### C4 - Shared graph-JSON serializer template

New `graph_editor/graph_serialization.hpp`: `write_graph_json<Asset>(Asset&)` and
`read_graph_json<Asset, Graph, Node, MakeFn, OwningSetter>(...)` (exact params
tuned to the two call sites). `graph_texture_serialization.*` and
`graph_mesh_serialization.*` become thin wrappers (or are deleted, their two
free functions re-expressed as template instantiations that scene_serialization
calls). Verified by the save/load round-trip sections of both sweeps.

### C5 - Shared undo-operations template

New `graph_editor/graph_operations.hpp`: templated link-record + maker + the
three `Operation` subclasses over `<Node, Asset, Window>` (+ a description-prefix
trait/string). `geometry_graph_operations.*` / `texture_graph_operations.*`
become `using` aliases so the windows' references are unchanged. Verified by the
undo/redo churn sections of both sweeps.

### C6 - Shared node base `Graph_node<Traits>`

New `graph_editor/graph_node.hpp`: the payload-agnostic node base carrying pin
creation/sync, dirty flags, `write/read/dump/committed_parameters`,
`factory_type_name`, `node_editor` + `show_pins` (using a `pin_key_color` hook
and a `commit_parameter_edit` hook that builds the concrete op), and payload
storage `std::vector<Payload>`. `Traits` bundles `Payload`, `Graph`, `Asset`,
`Operation`, the static `pin_key_color`, and the fan-in policy (geometry sum vs
texture last-wins). `Geometry_graph_node` / `Texture_graph_node` become `using`
aliases (decision 3) so the ~34 concrete node files are untouched. This is the
largest node-side change; land it only once C1-C5 are green.

### C7 - Shared window base (capstone)

`Graph_editor_window_base` (from C2) grows the payload-blind machinery, working
through `erhe::graph::Graph&` + `erhe::graph::Node*` + hooks:
- shared (non-template, compiled once): canvas begin/end + node iteration (via a
  `virtual void draw_node(erhe::graph::Node&)` the concrete window forwards to
  its node's `node_editor`) + link drawing + link create/delete (cycle guard) +
  `node_palette()` consumption + the "no target" empty state + `get_node_position`
  / `next_node_spawn_position`.
- hooks: `make_node(type_name)` (factory), `build_palette()` (palette data),
  `update_evaluation()` (evaluation strategy), and the asset-typed target /
  content-library / op-construction bits.

Because target resolution, `content_library->graph_*`, and
`item_reference_imgui<Asset>` need the concrete asset type, the asset-typed
window layer is a thin **templated** derived (`Graph_editor_window<Traits>`)
between the non-template base and the concrete leaf, OR those few methods stay in
the concrete windows. Decide concretely here, informed by C1-C6; split C7 into
sub-commits (canvas/link first, then target/palette, then eval hook) and **stop
at diminishing returns** if the remaining duplication is small relative to the
risk.

### C8 (optional) - MCP, create-UI, scene save/load helper dedup

The ~9 twin MCP tool bodies (`mcp_server_graphs.cpp`), the two scene_root Create
branches, and the parallel scene save/load blocks are payload-blind boilerplate
over `<Asset, Window, folder-member, write/read fn>`. Extract shared helpers
(building on the already-shared `find_library_item` / `find_content_library_asset`)
if the risk/benefit still favors it after C7. Lower value (more surface, each
body is short); explicitly optional.

---

## Verification Strategy

- **Regression net:** `scripts/geometry_nodes_smoke_test.py` (129 checks) +
  `scripts/texture_graph_smoke_test.py` (266 checks), green after every step,
  against the headless Vulkan build. Both must pass - each step is validated by
  BOTH consumers. **Run each sweep in a FRESH editor process** (kill + relaunch
  between sweeps): running the texture sweep in the same process right after the
  geometry sweep hits a known "texture-bake pollution" (the geometry sweep's
  heavy GPU work leaves the texture bake returning `has_output:false` for ~11
  checks). Confirmed baseline on `crease` before Phase C: geometry 129/129,
  texture 266/266, each in its own editor.
- **Builds:** `scripts\build_ninja_win_vulkan.bat editor` +
  `cmake --build build_vs2026_vulkan_headless --target editor --config Debug`
  after every step. (`rendering_test` breakage, if any, is acceptable per
  CLAUDE.md.)
- **Hygiene (mandatory):** kill stray editors, launch ONE headless editor, assert
  `get_server_info` pid/build matches the just-built binary before driving MCP;
  never re-issue a timed-out MCP mutation; `git checkout --
  config/editor/desktop_window_imgui_host_imgui.ini` after each run; `py -3`
  only; the 2 geometry "incremental" checks need
  `editor.graph_editor=trace` in `config/editor/logging.json` (set, run, REVERT
  before commit). Work on branch `crease`; do not switch branches.
- **No-behavior-change assertion:** since this is a pure refactor, a passing sweep
  IS the correctness proof. Any sweep delta is a regression to fix, not a new
  expectation to bless.
- **Independent review** of each step's diff before commit; `erhe::graph` and
  `erhe::item` have gtest suites - extend them only if pure logic is added (this
  refactor mostly moves code, so new gtests are unlikely).

---

## Key Files Reference

Shared low layer (unchanged): `src/erhe/graph/` (`Graph`/`Node`/`Pin`/`Link`),
`src/editor/operations/operation.hpp` + `operation_stack.cpp`.

Per-editor sources to de-duplicate:
- Geometry: `src/editor/geometry_graph/{geometry_graph, geometry_graph_node,
  geometry_graph_node_factory, geometry_graph_operations, geometry_graph_window,
  geometry_payload, graph_mesh, graph_mesh_serialization, geometry_graph_mesh}.*`
  + `nodes/*`.
- Texture: `src/editor/texture_graph/{texture_graph, texture_graph_node,
  texture_graph_node_factory, texture_graph_operations, texture_graph_window,
  texture_graph_widgets, texture_payload, graph_texture,
  graph_texture_serialization, texture_renderer, texture_graph_compose}.*` +
  `nodes/*`.
- Shader (left as-is): `src/editor/graph/*`.

Wiring: `src/editor/editor.cpp` (window construction ~line 1506),
`src/editor/windows/editor_windows.cpp` (multi-instance windows, Issue #252),
`src/editor/scene/scene_root.cpp` (Create context menu ~497-594),
`src/editor/content_library/content_library.{hpp,cpp}` (asset folders),
`src/editor/scene/scene_serialization.cpp` (save/load blocks),
`src/editor/scene/definitions/scene_file.py` (codegen struct, currently v7),
`src/editor/mcp/mcp_server*.cpp` (graph tools),
`src/erhe/item/erhe_item/item.hpp` (Item_type indices 42/43/44; next free 45),
`src/editor/CMakeLists.txt` (explicit source lists).

New shared files (target): `src/editor/graph_editor/{graph_editor_widgets,
graph_editor_palette_window, graph_editor_window_base, graph_asset,
graph_serialization, graph_operations, graph_node}.*`.
