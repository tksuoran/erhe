# Mesh Component Selection

Stability: mostly stable

Selection and viewport display of mesh sub-components -- faces (facets), edges,
and vertices -- as a foundation for mesh editing.

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
- Selections on several meshes at once, one entry per mesh primitive
  (section 3).
- Faces drawn as filled translucent triangles, edges as lines, vertices as
  camera-facing quads, plus a highlight of the component under the pointer.
- Desktop viewport only (see section 6).

Editing the selection, skinned-mesh selection, and compute-shader selection
over the GPU vertex and index buffers are
`doc/plans/mesh_component_selection.md`.

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

The mode combo in the viewport toolbar switches the mode with
`Mode_conversion::flush`; holding Ctrl while choosing the mode switches with
`Mode_conversion::expand` (section 3).

### Selection commands

Each command is a `Component_selection_action_command` hosted by the tool,
bound through the Commands system (so `doc/editor/input_bindings.md` overrides
apply), and consumes its key only in a mesh component mode; in Object and
Bone mode the key falls through to other bindings.

| Command | Key | Action |
|---------|-----|--------|
| `Mesh_component_selection.select_all` | Ctrl+A | Select every element of the targets in the current mode |
| `Mesh_component_selection.select_none` | Alt+A | Deselect everything (as the Clear button, cancelling pending region scans) |
| `Mesh_component_selection.invert` | Ctrl+I | Replace the current mode's set of every live entry with its complement |
| `Mesh_component_selection.select_linked_under_cursor` | L | Add the connected region grown from the hovered facet's vertices |
| `Mesh_component_selection.select_linked_from_selection` | Ctrl+L | Add the connected region grown from the vertices of every selected element |
| `Mesh_component_selection.grow` | Ctrl+Numpad+ / Ctrl+= | Grow the selection by one border ring |
| `Mesh_component_selection.shrink` | Ctrl+Numpad- / Ctrl+- | Shrink the selection by one border ring |

The toolbar has All / None / Invert / Linked buttons (Linked is the
from-selection form) beside Clear while a component mode is active. Ctrl+A
and Alt+A share the A key with the fly camera's strafe binding, which has no
modifier mask; `erhe::commands` dispatches key bindings with a modifier mask
first (`doc/erhe/commands.md`), so the selection commands see the chord.

Select all targets the meshes of the live entries plus the meshes of the
object `Selection` that component selection can address (scene content, not
skinned, not lock_edit, no separate collision shape:
`append_mesh_component_targets()`); when both are empty, the hovered mesh.
Select linked floods with `erhe::geometry::walk_connected_region()`
(`doc/erhe/geometry.md`) from the seed vertices; face mode adds the facets
whose vertices all lie in the region. The walk needs the Geometry's vertex
and edge connectivity; an entry whose Geometry lacks it is skipped with a
`log_selection` warning (grow and shrink skip it the same way). Every
command ends with a flush (section 3).

### MCP tools

`src/editor/mcp/mcp_server_mesh_components.cpp` (schemas in
`config/editor/mcp_tools.json`):

- `set_mesh_component_mode` - `mode`, optional `conversion` (`flush`, the
  default, or `expand`).
- `select_mesh_components` - adds vertices / edges / facets of one node's
  primitive (replacing the selection unless `extend`), then flushes.
- `get_mesh_component_selection` - mode and every entry's sets.
- `select_all_mesh_components` - select all; `scene_name` + `node_id` /
  `node_name` restrict the targets to that node's mesh.
- `invert_mesh_selection`, `grow_mesh_selection`, `shrink_mesh_selection`.
- `select_linked_mesh_components` - `from_selection: true`, or `scene_name`
  + node + `primitive_index` + `vertices` (seed list); optional
  `delimit_crease` stops the flood at crease edges.
- `clear_mesh_component_selection` - select none.

The mutating tools return the same JSON as `get_mesh_component_selection`,
except `select_mesh_components` (the entry's counts) and
`clear_mesh_component_selection`.

## 3. Data model

`Mesh_component_selection` (`src/editor/tools/mesh_component_selection.*`) is a
standalone `App_context` part (no constructor dependencies). The object
`Selection` gate and future editing code reach it through `App_context`,
mirroring how `Selection` is separate from `Selection_tool`.

It stores the current `Mesh_component_mode` and a list of
`Mesh_component_entry`, one per (mesh, primitive index, `Geometry`) that has
selected components. An entry holds the mesh and the `Geometry` as
`std::weak_ptr`, and the selected vertices, facets and edges; an edge is keyed
by its canonical (min, max) vertex pair (`make_edge_key()`), so an edge reached
from either adjacent facet maps to one key.

Liveness instead of invalidation:

- `is_live(entry)` holds while the mesh is in a scene (its node has an item
  host), the primitive index exists, the primitive has no separate collision
  shape, and the primitive's shape still carries exactly the entry's
  `Geometry`. Only live entries are drawn and edited.
- Geometry edits (Catmull-Clark, Conway, ...) allocate a new `Geometry` and
  swap it in with `Mesh::set_primitives()`; the old entry becomes dormant and
  is kept, so undo, which restores the old `Geometry`, makes it live again.
  `set_after_operation()` installs the operation's remapped selection as an
  entry on the result `Geometry`.
- `prune()` drops the entries whose mesh or `Geometry` is gone or that hold
  nothing.

### Flush and mode conversion

The three sets of an entry are kept consistent with the current mode's set:
`Mesh_component_selection::flush()` derives the other two from it for every
live entry, and every selection command (click, region and brush select,
grow / shrink, select all / invert / linked, the MCP tools) calls it after its
write.

- Vertex mode keeps the vertices; the edges are those with both vertices
  selected, the facets those with all vertices selected.
- Edge mode keeps the edges; the vertices are their end points, the facets
  those with all edges selected.
- Face mode keeps the facets and selects their edges and vertices.

`set_mode(mode, conversion)` converts every live entry before it sends
`Mesh_component_mode_changed_message`. `Mode_conversion::flush` (the default)
is the flush above for the new mode. `Mode_conversion::expand` (from one
component mode to another) first rewrites the new mode's set from the old
mode's: going up (vertex to edge to face) it selects every element touching
the selection; going down it keeps only the elements completely surrounded
by it (not part of any unselected element of the old mode); then it flushes.
Flush and conversion read the facet corners only (an edge is two consecutive
corners of a facet), so they need no connectivity.

The Geometry Spreadsheet's row selection and the operations that install a
post-operation selection (`set_after_operation()`, extrude) write their sets
as given, without a flush.

### Change announcement

The three sets are `Component_set<Key>`, a `std::set`
wrapper whose every write (insert, erase, clear, assignment of new keys)
calls `Mesh_component_selection::on_components_changed()`, and `clear_all()` /
`prune()` do the same for the entry list. That queues one
`Mesh_component_selection_changed_message` per message bus update, however
many writes happen before it, so a region select of thousands of facets
announces once. Because the notification lives in the set type, every editing
site (tool clicks, region and brush select, grow / shrink, MCP, operations
remapping the selection, the Geometry Spreadsheet) announces its change
without code of its own. A move between two `Component_set`s is the entry
vector reorganizing itself and announces nothing. Reads keep the `std::set`
interface (`begin` / `end` / `size` / `contains` / `find`, and a conversion
to `const std::set<Key>&`).

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

All overlays are drawn with `erhe::renderer::Primitive_renderer`, which draws
lines, points and triangles:

- `Primitive_renderer::add_triangle()` / `add_triangles()` reuse the line
  vertex layout (position + color; the line-width slot is unused), so filled
  faces need no shader or vertex format of their own. `line_simple.vert` only transforms
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

## 7. Region and brush face selection on the GPU

Region (box) and paint-brush face selection gather their result on the GPU.
When the device supports compute shaders and shader storage buffers (Vulkan,
Metal, OpenGL >= 4.3), `Id_renderer::submit_scan_compute()` runs two compute
passes over the blitted id-buffer scan region:

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
snapshot). A device without compute falls back to a per-pixel CPU readback
that dedups on the CPU; every supported GL device has compute, since OpenGL
4.5 is the hard minimum.

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
- `py -3 scripts/mesh_modeling_verify.py [--editor <editor.exe>]` launches a
  headless editor and checks the flush rules, both mode conversions, invert,
  select all / none, select linked and their keys on a box; the `Mcp_test`
  case `mesh_component_flush_and_select_all` covers the face-to-vertex flush
  and select all in CI.

## 9. Future work

- [plans/mesh_component_selection.md](../plans/mesh_component_selection.md) -
  editing the selection, skinned meshes, and compute selection over the
  vertex and index buffers.
- [plans/mesh_modeling.md](../plans/mesh_modeling.md) - Blender-style
  modeling operations: loop and ring select, loop cut, knife, slide, merge,
  dissolve, inset, bevel.
