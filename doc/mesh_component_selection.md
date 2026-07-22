# Mesh Component Selection

Selection and viewport display of mesh sub-components -- faces (facets), edges,
and vertices -- as a foundation for mesh editing. This document describes the
implemented feature and the editing / GPU-selection work that is intentionally
left for later.

## 1. Overview and scope

The editor's object `Selection` (`src/editor/tools/selection_tool.*`) selects
whole `erhe::scene::Mesh` / `Node` items. The mesh component selection feature
adds a finer granularity: the user picks individual faces, edges, or vertices
of a mesh in the viewport and they are drawn as overlays.

Implemented scope:

- Non-skinned meshes only. Picking reuses the CPU-side raytrace hover, which
  matches the rendered pose only for static (non-deformed) geometry.
- A Blender-style mode selector: **Object / Vertex / Edge / Face**. In a
  component mode a viewport left-click selects components; Object mode leaves
  object selection unchanged.
- A single active mesh at a time. Switching to a different mesh clears the
  component selection.
- Faces drawn as filled translucent triangles, edges as lines, vertices as
  camera-facing quads, plus a highlight of the component under the pointer.
- Desktop viewport only (see section 6).

Out of scope, documented in section 7: editing the selection (transforming
vertices, setting vertex attribute values), multi-mesh and skinned-mesh
selection, and GPU compute-shader-based selection.

## 2. Mode selector and command coexistence

`Mesh_component_selection_tool` (`src/editor/tools/mesh_component_selection_tool.*`)
is a background tool (always active) with an ImGui window ("Mesh Components")
that selects the mode and shows selection counts / styling. Because the mode is
the activation, the tool does not need to be the active toolbox tool.

Left-click is shared with object selection through the command system:

- `Component_select_command` is bound to the left mouse button (fires on
  release, matching the object-selection binding). Its `try_ready()` returns
  ready only when the mode is not Object and a component is under the pointer.
  Its host is the tool, whose base priority (`c_priority = 4`) is above the
  object `Selection` host, so when both are ready the component command wins.
- As a correctness guarantee independent of the priority arithmetic,
  `Selection::on_viewport_select_try_ready()` returns `false` whenever a
  component mode is active. So in Object mode object selection behaves exactly
  as before, and in a component mode exactly one handler fires.

Plain click replaces the selection (clear, then add the picked component);
Ctrl / Shift extend it (toggle the picked component). These mirror the object
`Selection` modifiers (read from `App_context::input_state`).

## 3. Data model

`Mesh_component_selection` (`src/editor/tools/mesh_component_selection.*`) is a
standalone `App_context` part (no constructor dependencies). The object
`Selection` gate and future editing code reach it through `App_context`,
mirroring how `Selection` is separate from `Selection_tool`.

It stores:

- the current `Mesh_component_mode`;
- the active mesh (`std::weak_ptr<Mesh>`) and primitive index;
- the active `Geometry` (`std::weak_ptr`) the indices were picked against;
- `std::set<GEO::index_t>` of selected vertices and facets;
- `std::set<std::pair<GEO::index_t, GEO::index_t>>` of selected edges, keyed by
  the canonical (min, max) vertex pair so an edge reached from either adjacent
  facet maps to one entry.

Invalidation:

- `set_active_mesh(mesh, primitive_index)` clears the sets when the target
  mesh / primitive changes (single-active-mesh scope).
- `set_active_geometry(geometry)` clears the sets when the `Geometry` object
  changes. Geometry edits (Catmull-Clark, Conway, ...) and undo allocate a
  fresh `Geometry` and swap it in via `Mesh::set_primitives()`, so the stored
  facet / vertex / edge indices no longer address the live mesh. The tool
  calls this each frame with the active mesh's current geometry (and on each
  pick), so a stale selection is dropped before it can index the new -- and
  possibly smaller -- mesh. Because edits always produce a new `Geometry`
  object rather than mutating one in place, this identity check fully covers
  the staleness; no per-index bounds clamping is needed.

## 4. Picking

Picking reuses the content `Hover_entry` produced by the raytrace picker
(`Scene_view::update_hover_with_raytrace()`), which already carries the hovered
`Mesh`, primitive index, `Geometry`, world-space hit position, and the mapped
`GEO::index_t facet` (via `Primitive_shape::get_mesh_facet_from_triangle()` over
`erhe::primitive::Element_mappings`).

`Mesh_component_selection_tool::pick()` validates the hover (valid, has
position / geometry / mesh, facet and primitive index set), rejects skinned
meshes (`Mesh::skin` non-null), transforms the world hit position into mesh-
local space (`Node::transform_point_from_world_to_local`), then:

- **Face**: uses `Hover_entry::facet` directly.
- **Vertex**: nearest facet-corner vertex by squared distance (the same nearest-
  vertex logic as `Paint_tool::tool_render`).
- **Edge**: nearest facet boundary edge by point-to-segment distance over the
  facet's consecutive corner vertex pairs (wrapping). Edges are derived from
  facet corners, so no global edge table (`Geometry::build_edges()`) is needed.

## 5. Rendering

All overlays are drawn with `erhe::renderer::Primitive_renderer`. The renderer
was line-only; filled faces required adding triangle primitives:

- `Primitive_renderer::add_triangle()` / `add_triangles()` reuse the existing
  line vertex layout (position + color; the line-width slot is unused), so no
  new shader or vertex format was needed. `line_simple.vert` only transforms
  position and passes color through, and `line_simple.frag` outputs
  premultiplied alpha, so a color alpha below 1 gives a translucent fill
  through the existing premultiplied-over visible blend state
  (`Debug_renderer_program_interface::color_blend_visible`).
- `Debug_renderer_bucket` carries a `Debug_renderer_shader_key`
  ({`primitive_type`, `tier`}, modeled on `erhe::scene_renderer::Shader_key`)
  that resolves the pipeline / shader variant once. The wide-line compute and
  geometry tiers apply only to the line primitive; triangles (and points) take
  the direct vertex-buffer path on every device. `make_pipeline()` selects the
  draw topology from the key.
- Triangle pipelines enable depth bias (polygon offset). The face fill is
  coplanar with the surface it highlights, so the visible depth pass would
  reject it on equal depth; a reverse-depth-aware polygon offset nudges it
  toward the viewer so it wins the test. The bias is set per pass via
  `Render_command_encoder::set_depth_bias()`.
- Lines on a surface (selected edges) have the same problem, but a screen-space
  wide line cannot use polygon offset cleanly. Instead the debug line vertex
  carries an optional per-vertex surface normal, and the line shaders
  (`line_simple.vert`, `debug_line.vert`, `compute_before_line.comp`, via the
  shared `erhe_line_surface_bias.glsl`) push the line toward the viewer by a
  bias derived from depth precision and surface slope rather than a tuned
  constant: `bias = margin * ulp(depth) * tilt`. `ulp(depth)` is the fp32 depth
  buffer's resolvable step at the fragment (so the bias scales with depth
  precision and the reverse-Z distribution automatically); `tilt =
  clamp(tan(theta), 1, 8)` from the surface normal is the slope-scaled term
  (analogous to slope-scaled shadow-map bias); and `margin` is the only knob, a
  unitless headroom in resolvable units (default 32, live-tunable as "Edge Depth
  Bias (ULPs)"). `clip_depth_direction` and `window_to_ndc_scale` (from the
  device depth-range convention) handle reverse-Z sign and the
  window/NDC mapping. The normal is zero for ordinary debug lines, so they are
  unaffected. (Assumes the viewport's float depth buffer; a fixed-point target
  would substitute a constant `2^-bits` step for `ulp(depth)`.)

The tool draws, for the active mesh:

- selected facets as filled translucent triangles (fan-triangulated, mesh-local
  positions with `transform = world_from_node`);
- selected edges as lines, each tagged with a world-space surface normal (the
  mean of its two endpoints' area-weighted incident-facet normals) so the line
  sits on top of the surface instead of intersecting it;
- selected vertices as small camera-facing quads (two triangles, built in world
  space, sized as a fraction of the camera distance for a roughly constant on-
  screen size);
- the hovered component (in the active mode) in a distinct highlight color.

`tool_render` clears and refills persistent scratch buffers each frame
(positions / indices / lines), so steady-state frames do not allocate (see the
run-time allocation discipline in `AGENTS.md`).

## 6. Multiview / headset limitation

The triangle (and point) direct path of `Debug_renderer` is single-view only;
the non-compute render path asserts `!multiview`. The shared `Debug_renderer`
renders both the single-view desktop viewport and the multiview headset, so:

- `tool_render` returns early unless `Render_context::viewport_scene_view` is
  non-null. That is the desktop viewport only -- it is null for the headset
  (multiview) and for preview renders -- so triangle primitives are never
  queued during a multiview frame.
- `Debug_renderer_bucket::render()` additionally returns before the
  `!multiview` assert when the bucket has no draws this frame, so a persistent
  triangle bucket left over from a desktop frame does not trip the assert
  during the headset's multiview render. A genuine multiview direct-path
  submission still asserts loudly.

Lifting the desktop-only restriction would require a multiview variant of the
`line_simple` shader (and feeding per-eye view data on the direct path), the
same way the wide-line compute path already has a multiview graphics stage.

## 7. Future work (out of scope)

### 7.1 Transform selected vertices

Move / rotate / scale the selected vertices (and the implied vertices of
selected edges / faces). This needs: a transform pivot derived from the
selection, integration with the existing transform tools / `Operation_stack`
for undo, writing transformed positions back into the `Geometry`, and
re-uploading / rebuilding the affected `Primitive` GPU buffers. The component
selection must survive the edit (the geometry-identity invalidation in section
3 would have to be relaxed to an index remap for in-place edits).

### 7.2 Set vertex attribute values

Edit per-vertex / per-corner attributes (color, UV, custom) on the selection,
extending the per-corner editing that `Paint_tool` already performs for vertex
colors. Needs a small attribute-editing UI and the same write-back / re-upload
path as 7.1.

### 7.3 Multiple meshes

Allow component selection to span several meshes at once. The data model would
become a map keyed by (mesh, primitive index) instead of a single active mesh,
and rendering would iterate the map.

### 7.4 Skinned meshes

Support component selection on skinned (deforming) meshes. CPU raytrace picking
uses bind-pose geometry, which does not match the deformed pose, so this needs
GPU-side picking (an ID render of components in the deformed pose) and rendering
of the overlays in the deformed pose (skinning the overlay positions).

### 7.5 GPU compute-shader selection

**Box / paint face selection: implemented (GPU compute gather).** Region (box)
and paint-brush face selection no longer read every scanned pixel back to the
CPU and dedup there. When the device supports compute shaders and shader storage
buffers (Vulkan, Metal, OpenGL >= 4.3), `Id_renderer::submit_scan_compute()`
runs two compute passes over the blitted id-buffer scan region:

1. `res/shaders/id_scan_gather.comp` -- one thread per pixel: decodes each
   pixel's packed id (`id = (r << 16) | (g << 8) | b`), applies the optional
   brush-disk mask, and `atomicOr`s the id's bit into a bitmask over the id
   space. The bitmask is the dedup (each distinct id becomes one set bit).
2. `res/shaders/id_scan_compact.comp` -- one thread per bitmask word: stream-
   compacts the set bits into a dense `{ uint count; uint ids[]; }` vector with
   a single `atomicAdd` per non-empty word.

Only the small compacted result is read back; `Id_renderer::take_scan_result()`
resolves each id to (mesh, primitive, facet) via the scan-frame id-range
snapshot. The interface blocks (`id_scan_input` / `id_scan_bitmask` /
`id_scan_output` / `id_scan_params`) are declared in C++ (`ensure_scan_compute()`)
and injected by `build_shader_stages`. The bitmask is sized dynamically to the
live max id and the output to the total facet count (both from the id-range
snapshot). On devices without compute (e.g. macOS OpenGL 4.1) the original
per-pixel CPU readback + dedup path is kept as a fallback.

Still future work: compute-shader selection over the GPU vertex / index buffers
themselves (vertex / edge marking, lasso), and component ID-buffer picking on
skinned meshes in the deformed pose (pairs with 7.4).

## 8. Testing notes

- Build the `editor` target on Vulkan and on OpenGL. The OpenGL non-compute
  "simple line" render path is shared by the new triangle direct path, so it
  must be exercised, not just the Vulkan compute path.
- In the editor, open the "Mesh Components" window and, for each of Vertex /
  Edge / Face:
  - hover a non-skinned mesh: the component under the pointer highlights;
  - left-click: it is selected and rendered (faces filled translucent with no
    z-fighting, edges as lines, vertices as quads); Ctrl / Shift extend, plain
    click replaces;
  - switch to a different mesh: the previous component selection clears;
  - run a geometry operation (e.g. Catmull-Clark) on the active mesh: the
    component selection clears rather than rendering stale indices;
  - switch to Object mode: left-click selects whole objects as before.
- Confirm existing line debug rendering (grid, fly-camera, physics) is
  unchanged, and that the headset / XR viewport still renders (the tool is
  inactive there).
