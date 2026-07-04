# UV Editor (issue #250)

Plan for implementing a UV editor in the erhe editor, modeled on Blender's
UV editor (https://docs.blender.org/manual/en/latest/editors/uv/index.html).
A Blender clone is available at `D:\blender` for reference (see
"Blender reference material" below).

Status: PLANNED, not implemented. This document is the design + phase plan to
be executed later.

## Table of Contents

1. [Goal and scope](#goal-and-scope)
2. [Existing erhe infrastructure](#existing-erhe-infrastructure)
3. [Gap analysis](#gap-analysis)
4. [Design decisions](#design-decisions)
5. [Implementation plan (phases)](#implementation-plan-phases)
6. [Verification plan](#verification-plan)
7. [Blender reference material](#blender-reference-material)
8. [Key files reference](#key-files-reference)
9. [Known constraints and gotchas](#known-constraints-and-gotchas)
10. [Out of scope / future work](#out-of-scope--future-work)

---

## Goal and scope

Deliver, inside the erhe editor:

- A **UV Editor window**: a 2D pan/zoom canvas showing the UV layout
  (wireframe of UV faces/edges/vertices) of the active mesh's selected
  texcoord channel, over a 0..1 grid with an optional checker or material
  texture underlay.
- **UV selection**: vertex / edge / face / island selection modes inside the
  UV canvas (click, box select), synchronized with the existing 3D mesh
  component selection.
- **UV editing**: translate / rotate / scale the UV selection with live
  in-viewport preview (the 3D mesh's texturing updates while dragging), with
  full undo/redo.
- **Unwrap operators surfaced in the UV workflow**: unwrap (LSCM / ABF /
  projection -- the `make_atlas` backend already exists), pack islands
  (xatlas / tetris), operating through the `Operation_stack`.
- **Seams**: mark/clear seam edges in the 3D viewport (stored as a geometry
  edge/corner attribute, drawn as an overlay) and have unwrap respect them.
- **MCP tools** for every query/mutation so the feature is scriptable, plus a
  smoke-test script following the geometry-nodes precedent.

Explicitly out of scope for the initial implementation (see
[Out of scope](#out-of-scope--future-work)): UDIM, live unwrap while dragging
pins, UV sculpt/relax brushes, stitch, copy/paste of UV layouts between
meshes, multi-object UV editing.

## Existing erhe infrastructure

Inventory (verified 2026-07-04). A large part of the backend already exists;
the missing piece is almost entirely the 2D editor front-end.

### UV data model (`erhe::geometry`)

`src/erhe/geometry/erhe_geometry/geometry.hpp`:

- Two UV channels, per-corner and per-vertex:
  `corner_texcoord_0 / corner_texcoord_1` (on `mesh.facet_corners`) and
  `vertex_texcoord_0 / vertex_texcoord_1` (on `mesh.vertices`), all
  `Attribute_present<GEO::vec2f>` (geometry.hpp:264-289). Geogram attribute
  names are `"texcoord_0"` / `"texcoord_1"` with companion
  `"present_texcoord_0/1"` bool stores.
- Read/write API: `attributes.corner_texcoord(i).set(corner, GEO::vec2f{u,v})`,
  `.try_get(corner)`, `.has(corner)` (geometry.hpp:173-242).
- The renderable-mesh builder reads corner texcoords first, falling back to
  vertex texcoords (`Build_context::build_vertex_texcoord`,
  `primitive_builder.cpp:625`). The UV editor edits **corner** texcoords;
  per-corner storage is what allows UV seams/islands in the first place.

### Unwrap backend (already complete)

`src/erhe/geometry/erhe_geometry/operation/make_atlas.{hpp,cpp}`:

- `make_atlas(source, destination, usage_index, hard_angles_threshold, parameterizer, packer)`
  wraps `GEO::mesh_make_atlas()`. Parameterizers: `projection`, `lscm`,
  `spectral_lscm`, `abf`. Packers: `none`, `tetris`, `xatlas`.
- Geogram's atlas maker segments the mesh into charts by hard-angle threshold,
  parameterizes each chart, packs, and writes per-corner UVs; erhe moves them
  into `corner_texcoord(usage_index)` and deletes Geogram's scratch attributes.
- The Geogram call is serialized under a static mutex (Geogram progress/packer
  state is process-global; make_atlas.cpp:111-132). Any new caller must go
  through `generate_mesh_atlas_texture_coordinates()` to inherit this.
- Geogram also ships the underlying pieces individually
  (`.cpm_cache/geogram/.../src/lib/geogram/parameterization/`): `mesh_LSCM.h`,
  `mesh_ABF.h`, `mesh_segmentation.h`, `mesh_param_packer.h` -- usable for
  seam-driven charting and pack-only operations (phases 3-4).
- Editor exposure exists: `Make_atlas_operation`
  (`src/editor/operations/geometry_operations.hpp:161`), the Operations window,
  and the MCP `make_atlas` tool (`mcp_server_mesh_components.cpp:567`) with
  `texcoord_slot`, `hard_angles_deg`, parameterizer and packer arguments.

### Mesh component selection (the 3D-side selection this syncs with)

`doc/mesh_component_selection.md`;
`src/editor/tools/mesh_component_selection.{hpp,cpp}` and
`mesh_component_selection_tool.{hpp,cpp}`:

- `Mesh_component_selection` is an `App_context` part storing, per
  (mesh, primitive, geometry) entry: `std::set<GEO::index_t>` of vertices and
  facets, and `std::set<std::pair<GEO::index_t, GEO::index_t>>` of edges keyed
  by canonical (min,max) vertex pair. Modes: Object / Vertex / Edge / Face.
- Invalidation by Geometry identity (edits swap in a fresh `Geometry`);
  `set_after_operation()` remaps selection across topology edits;
  `on_mesh_geometry_changed` message-bus subscription.
- Overlay rendering of selected facets/edges/vertices via
  `erhe::renderer::Primitive_renderer` (translucent triangle fills, biased
  lines) -- reusable as-is for showing the UV selection and seams in 3D.
- MCP tools already exist: `set_mesh_component_mode`,
  `select_mesh_components`, `get_mesh_component_selection`,
  `grow/shrink_mesh_selection`, `clear_mesh_component_selection`.

### 2D canvas + ImGui display machinery

- `src/erhe/imgui/erhe_imgui/imgui_canvas.{h,cpp}`: a pan/zoom infinite-canvas
  widget (`BeginCanvas`/`EndCanvas`, `CanvasView`, `CanvasFromWorld`/`ToWorld`,
  suspend/resume). This is the natural host for the UV view; it handles the
  view transform so drawing code works in UV space directly.
- `ImGui::GetWindowDrawList()` polyline/quad drawing is already used by the
  graph editors for custom canvas content.
- Displaying an `erhe::graphics::Texture` in ImGui:
  `Imgui_renderer::image(Draw_texture_parameters{.texture_reference = ...})`
  (`imgui_renderer.cpp:875`) or the `Imgui_window::draw_image()` convenience
  wrapper -- used by `Texture_graph_node::draw_preview()` and
  `Viewport_window::imgui_viewport()`. This is how the checker / material
  texture underlay gets drawn.

### Window / part / command / operation / MCP patterns

- `Imgui_window` subclass pattern: ctor
  `(Imgui_renderer&, Imgui_windows&, App_context&, ...)` forwarding
  title + ini_label; auto-registered, automatic window-menu entry;
  instantiated and owned in `editor.cpp`; `App_context` pointer assigned after
  construction (part ctors must not read other `App_context` members).
- Input: subclass `erhe::commands::Command`, register and bind in the owning
  tool/window ctor (`bind_command_to_mouse_button/_drag/_wheel/_key/_update`);
  `Command_host` base priority resolves contention
  (`mesh_component_selection_tool.cpp:373-440` is the model, including box
  select and paint select drags).
- Undo: `Operation` base (`execute`/`undo` with `App_context&`),
  `Operation_stack::execute_now()`. `Mesh_operation`
  (`src/editor/operations/mesh_operation.{hpp,cpp}`) is the geometry-editing
  template: per-mesh `Entry` with before/after primitive lists, geometry op
  into a fresh `Geometry`, `process(...)`, `make_renderable_mesh` +
  `make_raytrace`, swap via `set_primitives`, `mesh_geometry_changed` message,
  component-selection remap.
- Live per-corner GPU attribute write without a rebuild: `Paint_tool`
  (`src/editor/tools/paint_tool.cpp:292-391`) maps a `GEO` corner to a vertex
  buffer index via `Element_mappings::mesh_corner_to_vertex_buffer_index`
  (retained on `Primitive_render_shape`), locates the attribute's
  stream/offset via `Vertex_format::find_attribute`, and patches the vertex
  buffer with `Mesh_memory::enqueue_vertex_data`. The identical mechanism
  works for `Vertex_attribute_usage::tex_coord` -- this is the live-preview
  path for UV dragging.
- MCP: append a schema entry in `refresh_tool_list()`
  (`mcp_server_tool_list.cpp`) + an `else if` dispatch + `action_*` /
  `query_*` implementation (`mcp_server.cpp:360-409`); mesh-component tools
  live in `mcp_server_mesh_components.cpp` -- UV tools should get their own
  `mcp_server_uv.cpp`.

### Persistence

- Scene save/load: geometry-normative meshes serialize the full `GEO::Mesh`
  (all attribute stores, including corner texcoords and any new seam
  attribute) to `.geogram` files (`scene_serialization.cpp:959-1010`,
  load at :1337-1370). UV edits therefore persist with no serializer work.
- glTF: import preserves all `TEXCOORD_n` sets; export emits
  `TEXCOORD_{usage_index}`. Material per-sampler UV set
  (`Material_texture_sampler::tex_coord`) imports from glTF including
  `KHR_texture_transform`.

## Gap analysis

What is missing, in dependency order:

1. **A UV topology model.** Corner texcoords are flat per-corner values; the
   editor needs derived structure: "UV vertices" (corners of the same mesh
   vertex welded when their UVs coincide), UV edges, and UV islands
   (connected components over shared UV edges). Blender calls this the
   element map (`uvedit_islands.cc`). Nothing in erhe computes this today.
2. **The 2D window itself**: canvas, grid, wireframe rendering, texture
   underlay, channel selector, view fit/reset.
3. **UV-space picking and selection**: hover/click/box select in UV space,
   selection modes, sync with `Mesh_component_selection`.
4. **UV transform editing**: drag gestures, pivot, snapping, live GPU
   preview, undo operation, and post-edit tangent regeneration.
5. **Seam attribute + seam-driven unwrap**: erhe has no seam concept;
   Geogram's `mesh_make_atlas` charts by hard angle only. Seam support needs
   chart segmentation from user edges + per-chart parameterization + packing
   (Geogram provides all pieces separately).
6. **UV-editor MCP tools and smoke tests.**

Not gaps (already solved): unwrap algorithms, packing, persistence, glTF
round-trip, undo framework, per-corner GPU patching, 3D overlay rendering.

## Design decisions

### D1. One `Uv_editor_window` on `imgui_canvas`

A single `erhe::imgui::Imgui_window` subclass (`src/editor/windows/` or a new
`src/editor/uv_editor/` directory -- prefer the latter, mirroring
`geometry_graph/` / `texture_graph/`, since this feature will grow several
files). The window hosts:

- an `imgui_canvas` pan/zoom view over UV space (Y up; flip at draw time --
  `get_rtt_uv0/1()` precedent for the texture underlay orientation);
- a toolbar: texcoord channel (0/1), selection mode
  (vertex/edge/face/island), sync-selection toggle, underlay selector
  (none / checker / material base color), unwrap + pack buttons and their
  parameters;
- drawing via `ImDrawList`: 0..1 unit rectangle + optional grid subdivisions,
  UV wireframe (`AddPolyline` per facet), selected faces as filled polys,
  selected edges/vertices highlighted, hovered element highlight.

All per-frame draw data goes through persistent scratch buffers cleared at
point of use (run-time allocation discipline; see
`Mesh_component_selection_tool::tool_render` precedent).

The active mesh is the `Mesh_component_selection` active mesh (or, when its
mode is Object, the last-selected mesh from object `Selection` that has a
`Geometry`). Single active mesh only, matching the 3D component selection
scope.

### D2. UV element map as a cached derived structure

New class `Uv_element_map` (pure logic, lives in `erhe::geometry` so it can be
gtested -- `src/erhe/geometry/erhe_geometry/uv_element_map.{hpp,cpp}`):

- Input: `const GEO::Mesh&` + `Mesh_attributes&` + channel index.
- Builds:
  - `uv_vertex` clusters: corners grouped by (mesh vertex, UV value quantized
    with an epsilon); each cluster is one draggable UV point.
  - UV edges: facet boundary corner pairs; an edge is "welded" when both
    endpoint clusters are shared by the adjacent facet (i.e. not a seam in UV
    space).
  - Islands: connected components of facets over welded UV edges;
    per-island facet lists and UV bounding boxes.
- Rebuilt lazily: invalidated by Geometry identity change (same rule as
  `Mesh_component_selection`), by channel switch, and by any UV write. A
  rebuild after each committed edit is fine; during an interactive drag the
  map is *not* rebuilt (cluster membership cannot change while only UV values
  of the dragged selection move -- welds could split, but Blender behaves the
  same way until the gesture ends).

### D3. UV selection lives beside the mesh component selection

New `Uv_selection` state owned by the UV editor (not a new `App_context`
part unless a second consumer appears): a `std::set` of uv_vertex cluster
ids plus the derived edge/face/island interpretation, and the selection mode.

Sync with 3D (`Mesh_component_selection`), Blender "UV sync" style, one
switch:

- **Sync on** (default): the UV editor shows the whole mesh, and selecting
  in either editor updates the other (UV face <-> facet, UV vertex cluster
  <-> mesh vertex, UV edge <-> mesh edge). The mapping to 3D is lossy (one
  mesh vertex may be several UV vertices); 3D -> UV selects all corresponding
  UV elements, UV -> 3D selects the containing mesh elements. (This is
  simpler than Blender's sync-off default, which shows only the 3D-selected
  faces; showing the whole mesh avoids coupling visibility to selection.)
- **Sync off**: UV selection is independent (needed for island mode, which
  has no 3D counterpart).

### D4. Editing model: live GPU patch + operation on commit

Two tiers, following `Paint_tool` + `Mesh_operation` precedents:

- **During a drag**: write new UV values into the `Geometry`'s
  `corner_texcoord(channel)` for affected corners and patch the GPU vertex
  buffer in place per corner (Paint_tool's
  `mesh_corner_to_vertex_buffer_index` + `enqueue_vertex_data` path, with
  `Vertex_attribute_usage::tex_coord` / the channel's usage_index). No
  primitive rebuild per frame, no allocation beyond the reused scratch
  buffer.
- **On gesture commit**: record a `Uv_edit_operation : Operation` that stores
  the affected corner ids + before/after `vec2f` values (topology is
  unchanged, so the full `Mesh_operation` before/after primitive-list swap is
  unnecessary; a value-delta operation keeps undo cheap and preserves the
  component/UV selection). `execute`/`undo` write the values back into the
  Geometry and re-patch the GPU buffer the same way. Emit
  `mesh_geometry_changed`? No -- geometry identity is intentionally
  unchanged; instead add a narrower notification if any consumer needs it.
- **Tangents**: normal-mapping tangents are derived from channel 0 UVs
  (`Compute_tangents_parameters.texcoord_usage_index{0}`). After a committed
  edit (and on undo/redo) of channel 0, regenerate tangents for affected
  facets and patch the tangent attribute in the vertex buffer, or -- simpler
  and acceptable for the MVP -- schedule a one-shot primitive rebuild on
  commit (reusing the `Mesh_operation` machinery) while keeping the live drag
  on the cheap path. Decide at implementation time after measuring rebuild
  cost on a representative mesh; start with rebuild-on-commit (correct
  first), keep the value-delta path as the recorded undo state either way.

### D5. Unwrap and pack go through the existing operation

`Make_atlas_operation` already provides whole-mesh unwrap + pack with undo.
The UV editor's Unwrap button invokes it (channel = the editor's channel).
Pack-only ("Pack Islands") is a new thin `Geometry_operation` calling
Geogram's `Pack` stage (`mesh_param_packer.h`) on existing UVs -- same
serialization mutex.

Selection-scoped unwrap (unwrap only selected faces) is deferred to the seam
phase, where per-chart parameterization exists anyway.

### D6. Seams are a corner-pair edge attribute

New per-edge seam flag stored as a Geogram attribute. Since `GEO::Mesh` has
no first-class edge store bound to facets, store it as a per-corner bool
(`"seam"` on `facet_corners`: corner c marks the facet boundary edge from
c's vertex to the next corner's vertex; an edge is a seam if either adjacent
facet's corresponding corner is marked -- canonicalize on write so both
sides are always set). It serializes with the mesh automatically and is
carried by attribute-copying operations.

Seam workflow:

- Mark/Clear Seam operates on the 3D edge selection
  (`Mesh_component_selection` edge mode) or the UV editor's edge selection;
  undoable via a small attribute-delta operation (same shape as
  `Uv_edit_operation`).
- 3D overlay: seams drawn as colored lines by the component selection tool's
  renderer (reuses the biased-line path).
- Seam-driven unwrap: segment facets into charts by flood fill that does not
  cross seam edges (plus optional hard-angle threshold), then parameterize
  each chart with LSCM/ABF (Geogram `mesh_LSCM.h` / `mesh_ABF.h` operate on a
  mesh; run per-chart on extracted submeshes, or investigate Geogram's chart
  attribute support in `mesh_segmentation.h` first), then pack. This is
  the largest new backend piece; it lives in `erhe::geometry::operation`
  (e.g. `unwrap_with_seams.{hpp,cpp}`) with gtests.

### D7. Underlay textures

- Checker: generate once into a small `erhe::graphics::Texture` (e.g.
  procedural 8x8-cell checker at 512^2, created by the window on first use)
  and draw via `imgui_renderer->image()` under the wireframe.
- Material: resolve the active mesh's primitive material
  `texture_samplers.base_color` (`texture_source` first, then `texture` --
  the Phase A material source rule) and draw it the same way.

### D8. MCP surface and smoke test

New `mcp_server_uv.cpp` with, at minimum:

- `get_uv_layout` (query): active mesh, channel, uv_vertex/edge/face/island
  counts, per-island bbox, optionally sampled UV values for asserted corners.
- `select_uv_components` (action): by ids or by UV-space rect.
- `transform_uv_selection` (action): translate/rotate/scale deltas.
- `set_uv_editor_channel`, `get_uv_selection`.
- `uv_unwrap`, `uv_pack` (thin wrappers over the operations; `make_atlas`
  MCP tool already exists and stays).
- `mark_seams` / `clear_seams` (action): on the current edge selection.

`scripts/uv_editor_smoke_test.py` following
`scripts/geometry_nodes_smoke_test.py` conventions (busy-tolerant queries,
mutations once, screenshots via headless `capture_screenshot`).

## Implementation plan (phases)

Each phase: build clean (`scripts\build_ninja_win_vulkan.bat editor` +
headless VS build), headless MCP verification, independent diff review,
commit. gtests for all pure logic in `erhe::geometry` (it has a `test/`
directory).

### Phase 0: UV element map (backend, no UI)

- `erhe::geometry::Uv_element_map` (D2): clusters, edges, islands.
- gtests: box (26 verts / 24 facets -- note the default box is NOT 8/6),
  a mesh with a seam (e.g. atlas-unwrapped box has multiple islands),
  epsilon welding, island counts and bboxes.
- No editor changes yet.

### Phase 1: UV Editor window MVP (read-only)

- `src/editor/uv_editor/uv_editor_window.{hpp,cpp}`: window + `imgui_canvas`
  pan/zoom + grid + UV wireframe of the active mesh, channel selector,
  fit-view button, checker/material underlay (D7).
- Active-mesh tracking + Geometry-identity invalidation of the cached
  `Uv_element_map`.
- MCP: `get_uv_layout`.
- Verify: headless -- `make_atlas` a shape via MCP, open the window
  (imgui-ini patch trick from the geometry-graph verification to size the
  window), `capture_screenshot`, read the PNG: wireframe visible over grid.

### Phase 2: UV selection

- Hover + click + box select in UV space (commands bound in the window;
  `want_mouse_events`); modes vertex/edge/face/island; grow/shrink island
  double-click.
- Selection sync with `Mesh_component_selection` (D3) both directions; 3D
  overlay of the UV-driven selection comes free through the existing tool.
- MCP: `select_uv_components`, `get_uv_selection`.
- Verify: scripted select-by-rect, assert counts both in UV and mesh
  component selection; screenshots of highlighted islands.

### Phase 3: UV transform editing

- Drag-translate the selection; modal rotate/scale around the selection
  median (Blender G/R/S equivalents as canvas hotkeys + toolbar buttons);
  optional snap to grid / pixel (when an underlay is set).
- Live GPU patch during drag; `Uv_edit_operation` on commit; tangent
  strategy per D4 (start with rebuild-on-commit).
- Undo/redo restores exact values (hexfloat in any diagnostic dumps).
- MCP: `transform_uv_selection`.
- Verify: transform via MCP, `get_uv_layout` asserts moved values;
  undo/redo round-trip asserts bit-exact restore; screenshot shows texture
  sliding on the mesh in a 3D viewport; scene save/load round-trip keeps
  edited UVs.

### Phase 4: Unwrap / pack in the editor

- Unwrap button + parameters (parameterizer, hard angle) ->
  `Make_atlas_operation`; Pack Islands -> new pack-only operation (D5).
- Margin parameter for packing if the Geogram packer exposes one; otherwise
  document the limitation.
- Verify: unwrap a fresh box via UI-path MCP tools, island count from
  `get_uv_layout` matches expectation, undo restores prior UVs.

### Phase 5: Seams and seam-driven unwrap

- Seam corner attribute + mark/clear operations + 3D overlay (D6).
- `unwrap_with_seams` backend in `erhe::geometry::operation` with gtests
  (chart segmentation respecting seams; per-chart LSCM/ABF; pack).
- Editor: Unwrap uses seams when any exist; selection-scoped unwrap
  (selected faces = the charts to re-unwrap, others pinned in place).
- MCP: `mark_seams`, `clear_seams`.
- Verify: mark a seam ring on a cylinder-like shape, unwrap, assert island
  count changed accordingly; screenshots.

### Phase 6: Polish and overlays

- Stretch overlay (angle/area distortion coloring per face, Blender's
  Display Stretch) -- pure derived data over the element map.
- UV channel management: copy channel 0 <-> 1, clear channel.
- Per-material face filtering when a mesh has multiple primitives.
- Smoke-test consolidation into `scripts/uv_editor_smoke_test.py` covering
  all tools end-to-end.

## Verification plan

- **gtests** (`erhe_geometry_tests`): `Uv_element_map` construction, seam
  segmentation, `unwrap_with_seams` (island counts, all corners have UVs in
  [0,1] after pack, determinism), pack-only operation.
- **Headless MCP sweeps** per phase as listed; final smoke script asserting:
  layout queries, selection sync both directions, transform + undo/redo
  bit-exact round-trip, unwrap/pack island counts, seam-driven unwrap,
  scene save/load persistence of UVs + seams, glTF export/import round-trip
  of edited channel 0 (`TEXCOORD_0`).
- **Visual**: headless `capture_screenshot` of the UV window (wireframe,
  selection highlight, underlay) and of a 3D viewport with a textured
  material before/after a UV drag.
- **Builds**: ninja MSVC Vulkan + headless VS build every step; OpenGL build
  at least once per phase touching rendering.
- Restore `config/editor/desktop_window_imgui_host_imgui.ini` after every
  editor run; follow the stale-editor hygiene (kill editors +
  `get_server_info` pid check) from `prompt_queue.txt`.

## Blender reference material

Blender clone at `D:\blender`. The UV editor implementation lives in:

- `source/blender/editors/uvedit/` -- `uvedit_ops.cc` (operators),
  `uvedit_select.cc` (selection modes, sticky modes, element hashing),
  `uvedit_islands.cc` (island extraction + packing entry),
  `uvedit_unwrap_ops.cc` (unwrap, seams, pinning, live unwrap),
  `uvedit_draw.cc` (overlay drawing), `uvedit_smart_stitch.cc` (out of
  scope), `uvedit_rip.cc` (out of scope).
- Docs: https://docs.blender.org/manual/en/latest/editors/uv/index.html
  (tool semantics, selection sync behavior, stretch display).

Useful semantics to copy: UV sync selection on/off behavior; sticky-selection
"shared vertex" welding when translating; island selection by connected UV
faces; pack margin; stretch display (angle vs area).

## Key files reference

| Area | Files |
|------|-------|
| UV storage | `src/erhe/geometry/erhe_geometry/geometry.hpp` (`corner_texcoord_*`, `Attribute_present`) |
| Unwrap backend | `src/erhe/geometry/erhe_geometry/operation/make_atlas.{hpp,cpp}` |
| Geogram parameterization | `.cpm_cache/geogram/.../src/lib/geogram/parameterization/` (LSCM, ABF, segmentation, packer) |
| 3D component selection | `src/editor/tools/mesh_component_selection{,_tool}.{hpp,cpp}`, `doc/mesh_component_selection.md` |
| Live GPU attribute patch | `src/editor/tools/paint_tool.cpp:292-391`, `src/erhe/primitive/erhe_primitive/build_info.hpp` (`Element_mappings`) |
| Renderable build / texcoord flow | `src/erhe/primitive/erhe_primitive/primitive_builder.cpp:625` |
| Mesh operations / undo | `src/editor/operations/mesh_operation.{hpp,cpp}`, `geometry_operations.{hpp,cpp}` (`Make_atlas_operation`) |
| 2D canvas | `src/erhe/imgui/erhe_imgui/imgui_canvas.{h,cpp}` |
| Texture in ImGui | `src/erhe/imgui/erhe_imgui/imgui_renderer.cpp:875` (`image()`), `imgui_window.cpp:38` (`draw_image`) |
| Window pattern | `src/editor/windows/properties.{hpp,cpp}`, registration in `src/editor/editor.cpp` |
| Command pattern | `src/editor/tools/mesh_component_selection_tool.cpp:373-440` |
| MCP pattern | `src/editor/mcp/mcp_server.cpp`, `mcp_server_tool_list.cpp`, `mcp_server_mesh_components.cpp` |
| Persistence | `src/editor/scene/scene_serialization.cpp:932-1010` (geometry `.geogram` files) |
| Material UV set | `src/erhe/primitive/erhe_primitive/material.hpp:34` (`Material_texture_sampler::tex_coord`) |

## Known constraints and gotchas

- **Channel 1 is auto-regenerated.** `Mesh_operation::make_entries` runs
  `Geometry::process` with `process_flag_generate_facet_texture_coordinates`,
  which writes planar per-facet UVs into channel 1
  (`facet_texcoord_usage_index{1}`). User edits to channel 1 will be
  clobbered by any subsequent mesh operation. The UV editor defaults to
  channel 0; if channel-1 editing matters, `process` must learn to skip
  generation when the channel is user-authored (add a flag or reuse the
  `regeneration_flags` mechanism from the CC performance pass).
- **Tangents depend on channel 0 UVs** (mikktspace/compute_tangents read
  `texcoord_usage_index{0}`). Editing channel 0 without regenerating tangents
  breaks normal mapping; see D4.
- **glTF-imported meshes are not geometry-normative** -- they carry only
  `Triangle_soup`, no `Geometry`, so the UV editor cannot edit them until
  converted (`make_geometry`). The window must show a clear empty-state
  message with a convert affordance (or auto-convert behind a prompt) rather
  than silently doing nothing.
- **Geogram atlas/parameterization calls are process-global** -- always route
  through the serialized `generate_mesh_atlas_texture_coordinates` mutex
  pattern; new per-chart LSCM/ABF calls need the same treatment.
- **Corner texcoords vs vertex texcoords**: the builder prefers corner
  values; a mesh that only has `vertex_texcoord_0` (rare; most erhe shapes
  generate corner UVs) should be promoted to corner storage on first edit.
- **No per-frame allocation** in canvas drawing, picking, or drag preview --
  scratch members cleared at point of use.
- **Undo determinism**: store before/after UV values exactly (float bits);
  any diagnostic serialization uses hexfloat.
- **imgui.ini churn**: every editor run dirties
  `config/editor/desktop_window_imgui_host_imgui.ini`; restore before
  committing. The new window's ini_label adds a section to that file --
  expected, still never committed.

## Out of scope / future work

- **UDIM** (multi-tile UV spaces).
- **Pinning + live unwrap** (Blender's P/Alt-P with continuous re-solve while
  dragging pins). Geogram's LSCM picks its own two anchor points; arbitrary
  user pins need a custom LSCM solve. Revisit after Phase 5.
- **UV sculpt / relax / minimize-stretch brushes.**
- **Stitch** (merging islands along shared mesh edges,
  `uvedit_smart_stitch.cc`) and **UV rip**.
- **Copy/paste UV layouts** between meshes / between channels of different
  meshes (`uvedit_clipboard.cc`).
- **Multi-object UV editing** (blocked on mesh component selection's
  single-active-mesh scope, its section 7.3).
- **Skinned meshes** (blocked on the same constraint as component selection,
  its section 7.4).
- **Follow Active Quads, Smart UV Project, Lightmap Pack** operator ports --
  `make_atlas` projection + xatlas covers the common cases; add on demand.
