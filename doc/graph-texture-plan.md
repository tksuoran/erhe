# Graph Texture asset + Material texture-source (Phase A)

Make a texture node graph a first-class, selectable, serializable **asset**
(`Graph_texture`) that lives in a scene's Content library, and let a `Material`
source a texture slot from either a plain `erhe::graphics::Texture` **or** a
`Graph_texture`. This is the back-reference "does the material point back to the
texture graph?" - answered by giving `Material` a proper texture *source*
reference, and by re-homing the graph from the editor window (which owns one
global graph today) into a per-asset object the window merely *edits*.

This continues the texture-graph feature (see
[`doc/texture-graph-plan.md`](texture-graph-plan.md), all phases complete on
branch `texture`). It reuses that feature's texgen core, node classes,
serialization, undo operations and MCP surface unchanged in substance - only
*who owns the graph* changes, plus a new asset type and a texture-source seam.

## Table of Contents

1. [Implementation Status](#implementation-status)
2. [Current State (what exists today)](#current-state-what-exists-today)
3. [Architecture Decisions](#architecture-decisions)
4. [Implementation Plan (Steps)](#implementation-plan-steps)
5. [Verification Strategy](#verification-strategy)
6. [Key Files Reference](#key-files-reference)

---

## Implementation Status

| Work item                                                        | Status  | Commit |
|------------------------------------------------------------------|---------|--------|
| A1: `Item_type::graph_texture` + `Material` texture-source seam   | DONE    | 993358cd |
| A2: `Graph_texture` asset class + `Content_library` folder        | DONE    | 60223a97 |
| A3: Re-home graph state window -> asset; retarget edit/undo/save   | DONE    | c5827bbf |
| A4: MCP tools target a named/selected `Graph_texture`; create tool | DONE    | 70b095bd |
| A5: Material -> `Graph_texture` binding (Properties UI + MCP)      | DONE    | ef667f19 |
| A6: Scene serialization of asset + binding (scene_file v6)         | DONE    | d93f8f17 |
| A7: Smoke coverage for the asset + binding; live headless verify   | DONE    | 477b07f0 |

Phase A is functionally complete and headless-verified (create -> edit -> bind
material -> mesh renders the graph output -> graph edit re-renders live ->
save/reload round-trips the asset and the binding). Full smoke sweep 283/283.

Note (A3/A7 deviation): the window keeps a default (window-owned) Graph_texture
edited when no asset is selected, rather than dropping it as A7 originally
proposed. It is a benign scratch surface - the SELECTED content-library asset
always takes precedence, and the default is never referenced by a material.
Dropping it would require guarding every window/MCP edit path for a null current
graph with no functional gain, so it is kept intentionally.

Note (A5): the texture-source seam needed one more site than A1 covered - the
shader-variant key. `erhe::scene_renderer::shader_key.cpp sampler_is_bound()`
must treat a set `texture_source` as bound, else the material selects the
texture-less shader variant and the bound handle is ignored (the graph output
uploads but never samples). Found + fixed during A5 live verification.

---

## Current State (what exists today)

Established by a research pass (four parallel Explore agents) before any code:

- **One global graph, window-owned.** `Texture_graph_window`
  (`src/editor/texture_graph/texture_graph_window.hpp:151-159`) owns
  `Texture_graph m_graph`, `std::vector<std::shared_ptr<Texture_graph_node>>
  m_nodes`, the ax::NodeEditor `EditorContext` (node positions), the shared
  `Texture_renderer`, and a single file path
  `res/editor/graphs/texture_graph.json`. `update()` (per frame from
  `editor.cpp:453`) runs `m_graph.evaluate_if_dirty()` + `render_dirty_products()`.
  All 11 MCP tools resolve this one global graph via
  `m_context.texture_graph_window`.

- **Material -> texture is a raw pointer, resolved every frame.**
  `erhe::primitive::Material_texture_sampler` (`material.hpp:19-28`) holds only
  `std::shared_ptr<erhe::graphics::Texture> texture` (plus sampler + UV xform).
  `erhe::scene_renderer::Material_buffer::update()` dereferences it fresh each
  render pass (`texture_heap.allocate(data.texture.get(), sampler)`) - nothing
  caches the pointer across frames. So a *source* that resolves to the current
  `Texture` each frame is transparent to the renderer. `standard.frag` reads an
  unbound slot (handle `0xffffffff`) as white `vec4(1)`.

- **`erhe::graphics::Texture_reference` is the existing per-frame resolve seam**
  (`texture.hpp:48-53`): pure virtual `get_referenced_texture() const ->
  const Texture*`. `Texture` implements it (returns `this`);
  `Imgui_renderer::image` already takes a `shared_ptr<Texture_reference>` and
  resolves it each frame. `erhe::primitive` does **not** link `erhe::graphics`
  (only forward-declared `shared_ptr<Texture>`); holding a
  `shared_ptr<Texture_reference>` needs only a forward declaration, so no new
  link is introduced.

- **The output nodes already push into a Material by overwriting `.texture`.**
  `Texture_output_node`/`Texture_material_output_node` hold the target Material
  by name and, after each bake, do
  `m_material->data.texture_samplers.base_color.texture = baked_texture;`
  (`texture_output_node.cpp:176`) - relying on the "renderer re-reads `.texture`
  every frame" property. They register baked textures into
  `Scene_root::get_content_library()->textures`.

- **Content library.** `Content_library` (`content_library.hpp:127-143`) holds
  fixed category folders (materials, textures, ...). Each
  `Content_library_node` wraps one `erhe::Item`; its copy-ctor *clones* the
  wrapped item. Undoable create = `Item_insert_remove_operation`
  (`Mode::insert`, `parent = folder`); MCP `action_create_physics_material`
  (`mcp_server.cpp:4679`) is the template. `Scene_root::get_content_library()`
  returns the per-scene library.

- **Selection-driven editing.** `Selection::get_last_selected<T>()`
  (`selection_tool.hpp:215`) yields "the currently selected T".
  `Properties::material_properties()` (`properties.cpp:1594-1762`) is the exact
  pattern to mirror: read the selected asset, cache+diff it to bracket undoable
  edits, draw an empty state when none, edit the asset in place, render a live
  preview. Content-library items become selected by clicking them in the
  `Item_tree` browser (they enter `Selection` wrapped in a
  `Content_library_node`).

- **Serialization.** Scene saves as a directory bundle (`scene.json` +
  `data.glb` + geogram), version **5** (`definitions/scene_file.py`). Materials
  are *not* stored field-by-field in `scene.json` - they round-trip through the
  companion `.glb` by name, so glTF cannot express "sourced from Graph_texture
  X". The physics-asset collection (`scene_serialization.cpp:639-814` /
  `1109-1185`) is the worked example for persisting content-library assets:
  codegen struct, file-local ids, orphan preservation via `get_all<T>()`, id/
  name resolution with `warn` + null fallback.

- **Item registry.** `item.hpp`: indices 1..41, `count=42`; next free index is
  **42**. Adding a type = index constant + bit + `c_bit_labels` string
  (must match the class name) + bump `count`.

---

## Architecture Decisions

### 1. Texture source = the existing `erhe::graphics::Texture_reference`, held *beside* the plain texture

`Material_texture_sampler` gains a second field:

```cpp
std::shared_ptr<erhe::graphics::Texture> texture;                    // plain / imported (unchanged)
std::shared_ptr<erhe::graphics::Texture_reference> texture_source;   // NEW - authoritative when set
```

When `texture_source` is non-null it is authoritative; otherwise `texture` is
used. The renderer resolves source-first in exactly one place
(`Material_buffer::update`):

```cpp
const erhe::graphics::Texture* tex =
    data.texture_source ? data.texture_source->get_referenced_texture()
                        : data.texture.get();
```

Rationale:
- **Reuse over invention.** `Texture_reference` is *already* erhe's "indirection
  that resolves to the current `Texture` each frame" seam, already implemented
  by `Texture` and consumed by `Imgui_renderer::image`. Adding a parallel
  `ITexture_source` interface that does the same thing would be duplication.
- **Least-invasive to the renderer** (handoff lead (b)). The plain-`.texture`
  path is untouched (glTF import, existing content-library textures keep
  working); the change is one additive branch. No two-fields-must-stay-in-sync
  invariant: the fields are *alternatives*, not a cache-of relationship
  (`texture_source` wins when present). This is not a band-aid - there is a
  single authoritative value per frame.
- **Live link for free.** Because the renderer resolves `texture_source` every
  frame and `Graph_texture::get_referenced_texture()` returns its current baked
  output every frame, editing the graph (re-bake -> new/updated texture)
  updates every material that sources it, with no push/observer machinery.
- **Layering respected.** The interface lives in `erhe::graphics` (below
  `erhe::primitive`); `Material` holds it by forward-declared `shared_ptr`, so
  `erhe::primitive` gains no new link and never depends on the editor.
  `Graph_texture` (editor) implements the graphics interface - editor depends on
  graphics, which is allowed.

Convention: when a slot's `texture_source` is set, its plain `texture` is
cleared (source is the intent); `operator==` compares both fields. Preview and
ImGui display can hand the `Graph_texture` straight to `Imgui_renderer::image`
(it already accepts a `Texture_reference`).

### 2. `Graph_texture` is a clonable `erhe::Item` in its own Content-library folder

- New `Item_type::graph_texture` (index 42) and a `graph_textures` folder on
  `Content_library` (keeps plain `textures` / `get_scene_textures` distinct from
  graph assets; mirrors the physics-asset one-folder-per-type layout).
- `class Graph_texture : public erhe::Item<...>, public
  erhe::graphics::Texture_reference`. It **owns** the `Texture_graph`, the node
  vector, per-node canvas positions, an editable name, and (indirectly, via its
  output node) the baked output `Texture`. `get_referenced_texture()` returns the
  output node's current baked texture (or `nullptr` when the graph has no usable
  output - resolves to white, i.e. an unbound slot).
- **Clonable** via a JSON round-trip (serialize the graph, deserialize into a
  fresh copy), because `Content_library_node`'s copy-ctor clones its item.
  (Contrast `erhe::graphics::Texture`, which is `not_clonable`; a graph asset is
  authored data, so it must survive cloning.)
- Created undoably through `Item_insert_remove_operation` (UI + a new MCP
  `create_graph_texture`), non-undoably via `graph_textures->make<Graph_texture>()`.

### 3. Window edits the *selected* `Graph_texture`; a per-frame driver bakes *all* of them

- `Texture_graph_window` stops owning a graph. Each frame it looks up
  `selection->get_last_selected<Graph_texture>()`, caches/diffs it (the
  `Properties::material_properties` pattern), loads that asset's node positions
  into the single shared `EditorContext`, and draws/edit-routes that asset's
  graph. Empty state when nothing is selected.
- The window's `update()` (still called per frame from `editor.cpp`) iterates
  **every** `Graph_texture` in every scene's content library and runs
  `evaluate_if_dirty()` + `render_dirty_products()` on each. Evaluation is cheap
  (string composition); baking only happens for dirty graphs, so idle cost is
  ~zero and a material referencing an unedited/non-selected graph still gets a
  baked texture (notably right after scene load). The shared `Texture_renderer`
  stays window-owned (lazy-created, needs `graphics_device`).
- The four undo operations and the (de)serialization move from operating on
  `Texture_graph_window` to operating on a `Graph_texture` (plus the window for
  canvas position). This is mechanical retargeting, not a rewrite.

Note: a cleaner long-term home for the per-frame driver + shared renderer is a
dedicated non-window part; deferred to keep Phase A's blast radius contained and
because Phase C (shared graph-editor extraction) will revisit this anyway.

### 4. Material -> Graph_texture binding is serialized explicitly in `scene.json`

Because materials round-trip through glTF (which cannot express the link), the
binding is persisted editor-side. A new codegen struct records, per bound slot,
`{ material_name, slot, graph_texture_name }`; on load, after materials and
`Graph_texture` assets are reconstructed, each binding is resolved by name and
sets `material->data.texture_samplers.<slot>.texture_source = graph_texture`,
degrading to a `log_parsers->warn` + skip when either side is missing (the
established graceful-degradation contract). The `Graph_texture` asset itself is
serialized following the physics-asset model (file-local ids, orphan
preservation, its graph JSON reusing the existing texture-graph v1 format).
Scene file version bumps 5 -> 6.

MVP targets the **base_color** slot; the field, UI, MCP tool and serialization
are written so the other four PBR slots follow by table extension.

---

## Implementation Plan (Steps)

Each step: build clean (`scripts\build_ninja_win_vulkan.bat editor` **and**
`cmake --build build_vs2026_vulkan_headless --target editor --config Debug`),
headless MCP verification where applicable, independent diff review, commit.
`src/rendering_test/` may break when shared infrastructure changes - acceptable
(see CLAUDE.md). Restore `desktop_window_imgui_host_imgui.ini` after editor runs;
never commit `config/editor/*.json`.

### A1 - Item type + `Material` texture-source seam (foundation, no behavior change)

- `item.hpp`: add `index_graph_texture = 42`, bump `count` to `43`, add the
  `graph_texture` bit and the `"Graph_texture"` label.
- `material.hpp`: forward-declare `erhe::graphics::Texture_reference`; add
  `std::shared_ptr<erhe::graphics::Texture_reference> texture_source` to
  `Material_texture_sampler`; extend `operator==`.
- `material_buffer.cpp`: resolve `texture_source` first (single branch;
  const-correctness of `Texture_heap::allocate` handled at the call site).
- Verify: builds green; existing smoke test + a scene still render unchanged
  (no material sets a source yet).

### A2 - `Graph_texture` asset class + Content-library folder

- New `Graph_texture` (`erhe::Item` + `Texture_reference`) owning a
  `Texture_graph` + node vector + positions + name; `get_referenced_texture()`
  finds its output node's baked texture. Clonable via JSON round-trip.
- Add `graph_textures` folder to `Content_library`. Icon + browser wiring so it
  is selectable (reuses `Item_tree` machinery).
- No window changes yet; asset can be created/listed but the window still edits
  its own (soon-removed) graph. Verify: create via a temporary path, listed by a
  new/adapted query.

### A3 - Re-home graph state (window -> asset) + retarget edit/undo/serialize

- Move `m_graph` / `m_nodes` / positions / path off the window onto
  `Graph_texture`. Window gains `weak_ptr<Graph_texture> m_inspected`, per-frame
  lookup + diff + position load, empty state.
- `update()` iterates all `Graph_texture`s for evaluate + bake.
- Retarget the four operations and `save_graph`/`load_graph`/`clear_graph` to a
  `Graph_texture`.
- Verify: select a graph asset, edit its graph in the canvas, a second asset
  keeps an independent graph.

### A4 - MCP: target a named/selected `Graph_texture`; `create_graph_texture`

- Add `create_graph_texture` (mirror `action_create_physics_material`).
- Give the 11 texture-graph tools a graph selector (name/id; default to the
  selected asset) and retarget `find_texture_graph_node` /
  `texture_graph_node_json` to an asset's node list. `get_texture_graph` reports
  which asset it describes.
- Verify: create two graph assets by name, mutate each independently over MCP.

### A5 - Material -> Graph_texture binding (Properties UI + MCP)

- Properties material editor: a "Base Color Graph Texture" picker (a `combo`
  over `graph_textures`) that sets/clears `base_color.texture_source`; live
  preview reflects the graph.
- MCP tool `set_material_texture_source { material, slot, graph_texture }` (and
  its clear form).
- Convert the output nodes' "assign to material" to set `texture_source` to the
  owning `Graph_texture` (one mechanism), keeping the existing name-based target.
- Verify: bind a mesh's material base_color to a graph asset; editing the graph
  changes the rendered material (screenshot before/after).

### A6 - Scene serialization (asset + binding, scene_file v6)

- `definitions/graph_texture.py` (asset: name + embedded graph JSON) and a
  binding struct `{material_name, slot, graph_texture_name}`; new `Vector`
  fields on `scene_file.py`; bump version to 6; regenerate (build twice - see
  `project_codegen_double_build`).
- `scene_serialization.cpp`: write/read following the physics-asset model
  (orphan preservation via `get_all<Graph_texture>()`, name resolution, graceful
  warn). Re-establish `texture_source` bindings on load.
- Verify: save a scene with a bound graph material, reload, confirm the material
  still sources the graph and renders; missing-asset file degrades gracefully.

### A7 - Smoke test adaptation + full live verification

- `scripts/texture_graph_smoke_test.py`: create/select a named `Graph_texture`
  per section instead of assuming one global graph; thread the graph selector
  through the mutation/query helpers; add a material-binding + round-trip
  section. Keep the check count at parity or higher.
- Full headless end-to-end (erhe-headless-verify): create graph texture -> edit
  via MCP -> bind material base_color -> mesh renders the graph output
  (`capture_screenshot`) -> edit graph, re-render (changed) -> save + reload,
  still bound and rendering.

---

## Verification Strategy

- **Unit tests** where logic is pure: `Graph_texture` clone/serialize round-trip
  (a JSON in == JSON out check) and any pure binding-resolution helper can get
  gtests; `erhe::texgen` already has its suite. Renderer/asset wiring is verified
  live.
- **Headless end-to-end** each applicable step via the in-editor MCP server on
  the headless Vulkan build, using `capture_screenshot` and
  `texture_graph_export_png` pixel checks. The adapted 150+-check smoke sweep is
  the regression net.
- **Process** per step: edit -> build (ninja MSVC + headless VS) -> verify ->
  independent diff review -> commit (per-topic). Restore the imgui ini after
  runs; keep `src/rendering_test/` breakage as acceptable collateral.

---

## Key Files Reference

Touched / created (Phase A):

- `src/erhe/item/erhe_item/item.hpp` - new `graph_texture` type (A1)
- `src/erhe/primitive/erhe_primitive/material.hpp` / `.cpp` - `texture_source`
  field (A1)
- `src/erhe/scene_renderer/erhe_scene_renderer/material_buffer.cpp` -
  source-first resolve (A1)
- `src/editor/content_library/content_library.hpp` / `.cpp` - `graph_textures`
  folder (A2)
- `src/editor/texture_graph/graph_texture.{hpp,cpp}` (new) - the asset (A2)
- `src/editor/texture_graph/texture_graph_window.{hpp,cpp}` - edits the selected
  asset; per-frame bake driver (A3)
- `src/editor/texture_graph/texture_graph_operations.{hpp,cpp}`,
  `texture_graph_serialization.cpp` - retarget to `Graph_texture` (A3)
- `src/editor/mcp/mcp_server.cpp` - `create_graph_texture` +
  graph-selector tools + `set_material_texture_source` (A4, A5)
- `src/editor/windows/properties.cpp` - base_color graph-texture picker (A5)
- `src/editor/scene/definitions/{graph_texture,scene_file}.py`,
  `src/editor/scene/scene_serialization.cpp` - persistence, v6 (A6)
- `scripts/texture_graph_smoke_test.py` - per-asset addressing (A7)

Reference (unchanged, the patterns being mirrored):

- `src/editor/windows/properties.cpp:1594-1762` - selected-asset editor pattern
- `src/editor/operations/item_insert_remove_operation.hpp` - undoable create
- `src/editor/mcp/mcp_server.cpp:4679` - `action_create_physics_material`
- `src/editor/scene/scene_serialization.cpp:639-814` - physics-asset persistence
- `src/erhe/graphics/erhe_graphics/texture.hpp:48-59` - `Texture_reference`
