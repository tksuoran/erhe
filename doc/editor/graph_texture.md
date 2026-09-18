# Graph texture asset

Stability: mostly stable

A texture node graph is a first-class, selectable, serializable asset -
`Graph_texture` - that lives in a scene's content library, and a `Material`
slot samples it directly. This is the back-reference "does the material point
back to the texture graph?": the material holds a texture *reference* that the
renderer resolves every frame, and `Graph_texture` implements that reference.
`Texture_graph_window` owns no graph of its own; it edits the selected asset.

The texture graph itself - the codegen model, the node library and its
verification - is `doc/editor/texture_graph.md`. This document covers the asset, the
material seam and their persistence.

## The texture-source seam

`erhe::primitive::Material_texture_sampler` holds one
`std::shared_ptr<erhe::graphics::Texture_reference> texture_reference` per slot,
alongside that slot's sampler state and UV transform. There is no separate
plain-texture field: `erhe::graphics::Texture` implements `Texture_reference`
(returning itself), so an imported texture and a graph texture are the same kind
of value in a slot. All five PBR slots (base color, metallic-roughness, normal,
occlusion, emissive) take either.

`Texture_reference` (`erhe_graphics/texture.hpp`) is erhe's "indirection that
resolves to the current `Texture` each frame" seam: a pure virtual
`get_referenced_texture() const -> const Texture*`. `Imgui_renderer::image`
already consumes it, so a `Graph_texture` can be handed straight to ImGui for
previews. `erhe::scene_renderer::Material_buffer::update()` resolves the
reference fresh in each render pass, which is what makes the link live: editing
a graph re-bakes its output, and every material sampling it shows the new
texture with no push or observer machinery.

**Trap: the shader-variant key must agree.** `sampler_is_bound()` in
`erhe::scene_renderer`'s `shader_key.cpp` decides whether a material selects a
texture-sampling shader variant. A slot whose reference is set must count as
bound there, or the material picks the texture-less variant and the bound
handle is never sampled - the graph output uploads and nothing shows.

**Bake notification.** A holder registers itself with the reference it holds
(`Texture_reference_user`; `erhe::primitive::Material` does this from its slot
texture property), so `Graph_texture::add_user` / `remove_user` know the live
material slots bound to the graph. When a bake produces a *different* texture
object - not just new contents - the output node calls
`notify_referenced_texture_changed()` so every holder's record is rebuilt.

## `Graph_texture`

`Graph_texture` (`src/editor/texture_graph/graph_texture.{hpp,cpp}`) is
`Graph_asset<Graph_texture, Texture_graph, Texture_graph_node>` plus
`erhe::graphics::Texture_reference`. It owns the `Texture_graph` (links and
evaluation state), the node objects, the per-node canvas positions and the
asset name; the baked output belongs to its output node.
`get_referenced_texture()` returns that output node's current baked texture, or
`nullptr` when the graph has no usable output - which renders as an unbound
slot, that is, white.

- It is a distinct `Item_type` with its own content-library kind, which keeps
  plain textures (and `get_scene_textures`) separate from graph assets.
- It is `not_clonable`, like `erhe::graphics::Texture`: a deep copy would need
  the node factory's `App_context`. Duplicating a graph goes through
  serialization instead.
- It is created undoably through `Item_insert_remove_operation`, from the UI
  and from the MCP `create_graph_texture` tool.
- Its USD type token is the erhe class name, written as a custom `typeName`
  (`doc/erhe/usd_compatibility.md`): USD has no prim type for this kind.

## Window edits the selected asset; every asset is baked

`Texture_graph_window` resolves its target each frame from the selection and
draws that asset's graph in the shared ax::NodeEditor canvas, loading the
asset's node positions into it; it shows an empty state when nothing is
selected. The window keeps one default, window-owned `Graph_texture` that is
edited when no asset is selected. That default is a deliberate scratch surface:
the selected content-library asset always takes precedence and no material ever
references the default, so dropping it would only add a null-graph guard to
every window and MCP edit path.

`Texture_graph_window::update()`, called once per frame, iterates **every**
`Graph_texture` in every scene's content library and evaluates and bakes each
one. Evaluation is cheap string composition and baking only happens for dirty
graphs, so the idle cost is near zero - and a material sampling an unedited or
non-selected graph still gets a baked texture, which matters right after a
scene load. The shared `Texture_renderer` is window-owned and created lazily,
because it needs `graphics_device`, which does not exist during part
construction.

The graph undo operations and the graph (de)serialization operate on a
`Graph_texture`, with the window supplying only the canvas positions.

## Persistence

Materials round-trip through the companion glTF of a scene bundle, and glTF
cannot express "sourced from graph texture X", so the binding is persisted
editor-side in `scene.json`: a codegen struct records
`{material_name, slot, graph_texture_name}` per bound slot. On load, after the
materials and the `Graph_texture` assets have been reconstructed, each binding
is resolved by name and sets the slot's texture reference, degrading to a
`log_parsers->warn` and a skip when either side is missing - the established
graceful-degradation contract.

The asset itself is serialized following the physics-asset model: file-local
ids, orphan preservation through `get_all<Graph_texture>()`, and the graph
stored in the texture-graph JSON format.

## Verification

- `scripts/texture_graph_smoke_test.py` creates and selects a named
  `Graph_texture` per section and threads that selector through its mutation
  and query helpers, and it covers material binding and the save/reload round
  trip.
- End-to-end over the in-editor MCP server on the headless Vulkan build: create
  a graph texture, edit it, bind a material slot, confirm the mesh renders the
  graph output (`capture_screenshot`), edit the graph again and see the render
  change, then save and reload and confirm it is still bound and rendering.
  `texture_graph_export_png` gives pixel-level assertions.

## Future work

- [plans/texture_graph.md](../plans/texture_graph.md) - a dedicated non-window
  home for the per-frame bake driver and the shared `Texture_renderer`.
