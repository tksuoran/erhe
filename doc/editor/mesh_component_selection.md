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

The transform gizmo moves, rotates and scales the selection (section 8).
Skinned-mesh selection, compute-shader vertex and edge selection over the GPU
vertex and index buffers, and multiview overlays are
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

### Box and paint select

A second toolbar combo picks the gesture: Click, Box or Paint (B and C switch
to Box and Paint; they consume the key only in a mesh component mode, so in
Object and Bone mode the key falls through). Both gestures work in Vertex,
Edge and Face mode, with the same modifiers:

- **Box** (`Component_box_select_command`, a left-button drag) draws a
  rubber-band rectangle and commits on release: plain replaces the selection,
  Shift extends it, Ctrl subtracts from it. A click without motion falls
  through to the single-click select.
- **Paint** (`Component_paint_select_command`, Blender Circle Select) applies
  a brush disk along the drag, a click paints one dab: plain clears once at
  the stroke start and then adds, Shift adds, Ctrl subtracts. The mouse wheel
  resizes the brush while hovering a viewport in Paint mode.

Face mode gathers the visible facets with the GPU id-buffer scan; Vertex and
Edge mode project the vertices on the CPU (section 7).

### Loop and ring select

Two `Component_loop_select_command`s bound to the left mouse button (on
release) with an exact modifier mask, so at the tool's priority they
dispatch before the mask-less single-click select, and the fly camera's Alt
drags (right and middle button) are unaffected:

| Command | Keys | Action |
|---------|------|--------|
| `Mesh_component_selection.loop_select` | Alt+click, Shift+Alt+click | Edge loop (vertex and edge mode), face loop (face mode) |
| `Mesh_component_selection.ring_select` | Ctrl+Alt+click, Shift+Ctrl+Alt+click | Edge ring (vertex and edge mode), face loop (face mode) |

The walk starts from the picked edge, which is the facet boundary edge
nearest to the pointer in every mode (section 4), and runs the walkers of
`erhe_geometry/topology.hpp` (`doc/erhe/geometry.md`) through
`Mesh_component_selection::select_loop()`. The walked elements go into the
current mode's set: edge mode takes the walked edges, vertex mode their
vertices, face mode the walked facets; then the selection flushes (section
3). A plain click replaces the selection; Shift extends it, or deselects the
walked set when every element of it is already selected. The walk needs the
Geometry's vertex and edge connectivity; without it the click is consumed
and a `log_selection` warning names the Geometry.

- **Boundary cycle.** A plain Alt+click on a boundary edge whose edge loop is
  already fully selected selects the whole boundary loop instead
  (`walk_boundary_loop`); the next Alt+click on the same edge returns to the
  edge loop. The tool keeps the last picked boundary edge and whether its
  click selected the boundary loop.
- **Delimit.** The edge loop stops at outer corners (valence-2 boundary
  vertices) and, while the toolbar checkbox "Loop stops at creases" (vertex
  and edge mode, default on) is checked, at creases: a crease edge
  continues only onto crease edges, a plain edge stops at a vertex with a
  crease edge.
- **Preview.** While Alt (or Ctrl+Alt) is held over a component, the tool
  draws the loop, ring or face loop a click would select in the hover color.
  The walk runs only when its key changes (scene view, mesh, primitive,
  Geometry, picked edge, kind, delimit): on `Hover_mesh_message` (the hovered
  mesh or the pointer ray changed), on the hovered scene view changing, on
  the editor's key handler seeing the Shift / Ctrl / Alt state change, on a
  mode change, on a geometry change and on the delimit checkbox. The preview
  is drawn only while its target is live (`Mesh_component_selection::is_live`).

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

### Delete and dissolve keys

Delete and Ctrl+X act on the components while a mesh component mode has a
live, non-empty selection of its own set
(`Mesh_component_selection::has_live_mode_selection()`), following
`doc/plans/mesh_modeling.md` D7 (Blender's keys where the editor's
bindings allow it, exact modifier masks):

| Command | Key | Action |
|---------|-----|--------|
| `Geometry.Delete.Selected` | Delete | Delete the selection: vertices in vertex mode, edges in edge mode, faces in face mode |
| `Geometry.Dissolve.Selected` | Ctrl+X | Dissolve the selection: vertices, edges or faces by mode, with the Operations window's dissolve options |
| `Geometry.Merge.AtCenter` | M | Merge the selection's vertices at their center (any of vertex, edge, face mode) |
| `Geometry.Split.Selected` | Y | Split the selection off the rest (face mode: the faces; vertex / edge mode: the faces fully selected; edge mode without a complete face: tear along the edges) |
| `Geometry.Rip.Selected` | V | Rip the selected vertices (vertex mode) or edges (edge mode), ripping the side toward the last hovered content point |
| `Geometry.Separate.Selection` | P | Move the selection's faces into a new mesh beside the original |
| `Geometry.Fill.Selected` | F | Fill: faces from the selection's vertices, edges or faces (any of vertex, edge, face mode) |
| `Geometry.Connect.Selected` | J | Connect vertex path between the selected vertices (vertex or edge mode) |
| `Geometry.Normals.RecalculateOutside` | Shift+N | Recalculate the normals of the selection's faces outside (object mode: every face of the selected meshes) |

The object `Selection` binds the same keys (`Selection.delete`,
`Selection.cut`); both of its commands decline in that state and the two
`Geometry` commands decline outside it, so exactly one handler consumes the
key. The other delete contexts and the limited dissolve are Operations window
buttons and `Geometry.Delete.*` / `Geometry.Dissolve.*` commands
(`doc/editor/operations.md`). Blender's M opens a merge menu; the editor has
no popup menu, so M merges at center and the other merge types are the
Operations window's Components buttons and `Geometry.Merge.*` commands. M
declines (the key falls through) without a live component selection. Y, V
and P are bound with the modifier mask 0 (Ctrl+Y is redo, Ctrl+V paste) and
decline the same way (V also outside vertex and edge mode). F and J carry the
mask 0 too and decline without a live component selection (J also outside
vertex and edge mode) and while a modal component edit runs: F is also the
fly camera's frame-selection key (bound without a mask, so it frames in
object mode) and a slide's flip key (the slide's F is declared after the
fill command and receives the key because fill declines during the slide).
Shift+N carries the shift mask; the plain N (create a frame node) is bound
without a mask, so the masked Shift+N dispatches first and N alone never
matches it. Shift+N declines without a live component selection in a
component mode and without a selected mesh in object mode, and the key then
falls through to N. Flip (`Geometry.Normals.Flip`), recalculate inside
(`Geometry.Normals.RecalculateInside`) and smooth vertices
(`Geometry.Smooth.Vertices`) have no key (Blender reaches them from menus).
Every one of these commands, and every Operations window button acting on
the component selection (delete, dissolve, merge, subdivide, split, rip,
separate, inset, bevel, fill, connect, bridge, flip, recalculate, smooth), declines while a modal
component edit (slide, loop cut, inset, bevel, knife) runs, so Delete during
a G slide deletes nothing.

### Slide keys

G starts a pointer slide of the live component selection
(`doc/editor/transform.md` "Scalar edits"): vertex slide in vertex mode,
edge slide in edge and face mode. The gizmo is the grab, so the plain G is
the slide (Blender's G G). While the slide runs, the modal commands below
consume their input; otherwise they decline and the key falls through
(Escape also cancels the mesh component edit of a gizmo drag). The keys are
bound with the exact modifier mask 0, so they dispatch before the mask-less
bindings of the same keys (fly camera E, frame F, paint gesture C, the log
window's Escape). The click commands are Ready for the length of the slide,
which ranks them above the other press commands of their buttons; the
component click, box, paint and loop select gestures stand down meanwhile.

| Command | Key | Action |
|---------|-----|--------|
| `Mesh_component_selection.slide` | G | Start the slide at the pointer |
| `Mesh_component_selection.modal_confirm` | Enter / Numpad Enter | Confirm (one undo entry) |
| `Mesh_component_selection.modal_confirm_click` | Left press | Confirm |
| `Mesh_component_selection.modal_cancel` | Escape | Cancel (positions back, no undo entry) |
| `Mesh_component_selection.modal_cancel_click` | Right press | Cancel |
| `Mesh_component_selection.modal_toggle_even` | E | Toggle even |
| `Mesh_component_selection.modal_toggle_flipped` | F | Toggle flipped |
| `Mesh_component_selection.modal_toggle_clamp` | C | Toggle clamp (Alt held: unclamped while held) |

### Loop cut keys

Ctrl+R (`Mesh_component_selection.loop_cut`) starts the loop cut mode over
the hovered mesh in any component mode; while it runs PageUp / PageDown,
numpad plus / minus, the wheel and the digits set the cut count (with Alt the
smoothness), and the modal commands above confirm (cut, then the slide of the
new loops) and cancel. The whole list, the preview and the gesture are in
`doc/editor/mesh_modeling.md`.

### Knife keys

K (`Mesh_component_selection.knife`, mask 0) starts the knife mode over the
hovered mesh in any component mode; while it runs a left press adds a cut
point, a right press ends the polyline, Ctrl+Z removes the last point, C
toggles cut through, A and X / Y / Z constrain the point, and Enter / Space
cut and Escape cancels. The whole list and the gesture are in
`doc/editor/mesh_modeling.md`.

### Bevel keys

Ctrl+B (`Mesh_component_selection.bevel`) starts the bevel mode on the live
edge selection in edge mode (vertex mode: the edges between selected
vertices); while it runs W cycles the offset type, L toggles loop slide, and
the modal commands above confirm and cancel. The whole list and the gesture
are in `doc/editor/mesh_modeling.md`.

### Inset keys

I (`Mesh_component_selection.inset`, mask 0) starts the inset mode on the
live face selection in face mode; while it runs O / I / B / R toggle
outset / individual / boundary / relative offset, E even offset, Ctrl held
drags the depth, and the modal commands above confirm and cancel. The whole
list and the gesture are in `doc/editor/mesh_modeling.md`.

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
- `select_mesh_loop` - `scene_name` + node + `primitive_index` + `edge`
  ([v0, v1]), `kind` (`edge_loop`, the default, `edge_ring`,
  `boundary_loop` or `face_loop`), `action` (`replace`, the default,
  `extend` or `deselect`) and `delimit_crease` (default true): loop / ring
  select without the pointer. The edge kinds apply in vertex and edge mode,
  `face_loop` in face mode; the result carries `walked`, the element count
  of the walk.
- `clear_mesh_component_selection` - select none.
- `debug_region_select` - a box (or, with `is_brush`, a brush disk) select
  over a rectangle in viewport pixels in `mode` (`vertex`, `edge` or `face`,
  the default), with `replace` (default true) and `subtract`; it commits over
  the next frames like the gesture. Vertex and Edge mode project in the last
  hovered viewport, or the first viewport window when none was hovered.

- `delete_mesh_components` - optional `context` (`vertices`, `edges`,
  `faces`, `only_edges_and_faces`, `only_faces`; default by the current
  mode); needs a live selection in the mode the context reads.
- `dissolve_mesh_components` - optional `kind` (`faces`, `edges`,
  `vertices`; default by the current mode) and the options of that kind
  (`dissolve_vertices`, `preserve_quads`, `angle_threshold_degrees`,
  `face_split`, `boundary_tear`), each defaulting to the library default.
- `dissolve_limited` - `angle_limit_degrees`, `dissolve_boundaries`,
  `delimit_winding`, `delimit_crease` (library defaults); on the component
  selection when one is active, else on the selected meshes.
- `merge_mesh_vertices` - optional `type` (`at_center` default,
  `at_position`, `at_first`, `at_last`, `collapse`), `position` ([x, y, z]
  in the mesh's local space, required for `at_position`) and `merge_uvs`
  (default false); needs a live selection in vertex, edge or face mode.
- `merge_mesh_by_distance` - `threshold` (default 1e-4), `use_centroid`
  (default true), `include_unselected` (default false); on the component
  selection when one is active, else on the selected meshes.
- `subdivide_mesh_edges` - `cuts` (1 .. 500, default 1), `smoothness`
  (default 0), `only_quads` (default false); needs a live selection in
  vertex, edge or face mode (the selected edges, the edges of the selected
  facets, the edges between selected vertices). In edge mode the edges the
  fills created are the selection afterwards (the split halves when no fill
  ran).

- `slide_mesh_components` - `kind` (`edge`, the default, or `vertex`),
  `factor` (required), `even`, `flipped` (default false), `clamp` (default
  true), `direction` ([x, y, z] world space, required for `vertex`): the
  numeric slide of `doc/editor/transform.md` "Scalar edits" (begin, one
  step, commit, one undo entry); returns `slide_vertices`,
  `moved_vertices`, `loops` (edge) and `queued`. Needs a live selection in
  a component mode; refused when it cannot slide or another component edit
  is active.
- `cancel_component_edit` - cancels the knife mode, the running G slide or
  the mesh component edit of a gizmo drag like Escape; `cancelled` is false
  when none was active.
- `loop_cut_mesh` - `scene_name` + node + `primitive_index` + `edge`
  ([v0, v1]), `cuts` (default 1), `smoothness` (default 0), `factor`
  (default 0), `even`, `flipped`: the numeric loop cut of
  `doc/editor/mesh_modeling.md` (cut, slide, one undo entry).
- `knife_cut_mesh` - `scene_name` + node + `primitive_index` + `points`,
  `cut_through`, `close`, optional `view`: the numeric knife of
  `doc/editor/mesh_modeling.md` (one polyline, one undo entry).
- `inset_mesh_faces` - `thickness`, `depth` and the option booleans: the
  numeric inset of `doc/editor/mesh_modeling.md` on the live face
  selection (topology, placement, one undo entry).
- `bevel_mesh_edges` - `amount`, `offset_type` (`offset` | `width`),
  `loop_slide`: the numeric bevel of `doc/editor/mesh_modeling.md` on the
  live edge (vertex) selection (topology, placement, one undo entry).
- `split_mesh_components` - no options; needs a live selection in vertex,
  edge or face mode (`Split_components_operation`, `doc/editor/operations.md`).
- `rip_mesh_vertices` - optional `direction` ([x, y, z] in the mesh's local
  space; the side toward it is ripped, omitted: the library's deterministic
  side); needs a live selection in vertex or edge mode.
- `separate_mesh_selection` - no options; needs a live selection holding a
  complete face; returns `node_id` and `node_name` of the new node (`nodes`
  lists one per separated mesh), which joins the scene when the queued
  operation runs.
- `fill_mesh_selection` - no options; needs a live selection in vertex, edge
  or face mode (`Fill_operation`, `doc/editor/operations.md`); nothing to
  fill leaves the mesh unchanged (logged).
- `connect_mesh_vertices` - no options; needs a live selection in vertex or
  edge mode (`Connect_vertices_operation`).
- `bridge_mesh_loops` - `connection` (`open_loop` default, `closed_loop`,
  `loop_pairs`), `merge` (false), `merge_factor` (0.5, in [0, 1]),
  `twist_offset` (0), `cuts` (0, in [0, 500]); needs a live selection in
  vertex, edge or face mode (`Bridge_loops_operation`,
  `doc/editor/operations.md`); an invalid loop selection leaves the mesh
  unchanged (logged).
- `flip_mesh_facets` - no options; needs a live selection in vertex, edge or
  face mode (`Flip_facets_operation`): the faces of the selection reverse
  their winding; indices and the selection are kept.
- `recalculate_mesh_normals` - `side` (`outside` default, `inside`); the
  faces of the live selection in a component mode, every face of the
  selected meshes in object mode (`Recalculate_normals_operation`).
- `smooth_mesh_vertices` - `factor` (0.5, in [0, 1]), `repeat` (1, in
  [1, 1000]); needs a live selection in vertex, edge or face mode; one
  in-place "Smooth Vertices" vertex move per primitive, so indices and the
  selection are kept; an error when no vertex moves. Node targets do not
  apply (it reads the component selection only).

The geometry tools queue an undoable operation and return
`{queued: true, ...}` with the options they used; node targets (`node_ids`
/ `node_id` / `node_name` + `scene_name`) override the object selection as
for `merge_faces`.

The mutating selection tools return the same JSON as `get_mesh_component_selection`,
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

## 7. Region and brush selection

### Vertex and edge mode: CPU projection

`Mesh_component_selection_tool::select_components_in_region()` takes the
region (the box rectangle or the brush disk, in the viewport pixels of
`Viewport_scene_view::get_viewport_from_window()`) and:

1. collects the candidate targets: every visible mesh of the view's scene
   content layer passed through `append_mesh_component_targets()` (the same
   filters as select all), gathered under the scene's `item_host_mutex`;
2. projects every vertex of each target with the math of
   `Viewport_scene_view::project_to_viewport()`, the camera transforms
   computed once per call; a vertex behind the camera (clip w <= 0) is
   outside;
3. in Vertex mode adds (or, with Ctrl, erases) the vertices inside the
   region; in Edge mode the edges whose both endpoints are inside, walked
   from the facet corners;
4. flushes (section 3).

The selection goes through the mesh: vertices and edges hidden behind
other surfaces are selected too (Blender's X-ray behavior). The per-vertex
inside flags and the target list are persistent scratch on the tool; the
target list is emptied after each call so it holds no mesh across frames.
The projection runs once per box commit (on the first `gesture_update`
after release) and once per gesture frame while a brush stroke is held,
never in idle frames. Selecting only the visible elements is the
compute-shader selection of `doc/plans/mesh_component_selection.md`.

### Face mode: id-buffer scan on the GPU

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

## 8. Transforming the selection

In a component mode with a live selection, the transform gizmo drives
`Mesh_component_transform` (`src/editor/transform/mesh_component_transform.*`)
in place of the object selection:

- The affected vertices of each live entry are the selected vertices, the end
  vertices of the selected edges, or the corner vertices of the selected
  faces, de-duplicated per (mesh, primitive). Entries with a separate
  collision shape are skipped.
- The gizmo anchor is the world-space centroid of the affected vertices of all
  entries. In the Selection reference mode its orientation comes from the
  selected components (face normal / tangent, edge direction, vertex normal);
  otherwise it is the first mesh's orientation.
- During a drag each step writes the moved positions into the `Geometry` and
  the GPU vertex and edge-line buffers; on release one
  `Move_mesh_vertices_operation` per moved primitive (a `Compound_operation`
  for several) rebuilds the primitive and is the undo entry. The edit keeps
  the same `Geometry` object, so the selection stays live through it.
- With `geometry_edit_mode` fork, a `Geometry` shared with another mesh is
  forked on the first real move and the fork is recorded as its own undoable
  operation, so the other meshes keep their shape.
- `transform_mode` (`Mesh_transform_mode`, scene-view toolbar) picks move,
  extrude (duplicate the selection boundary, bridge it with new faces, then
  move along the gizmo delta, or along the group or vertex normals), or the
  edge / vertex slides (`doc/editor/transform.md` "Scalar edits").
- Escape (`Mesh_component_transform::cancel()`) restores the drag-start state
  and queues nothing.

## 9. Testing notes

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
  select all / none, select linked and their keys, and vertex / edge mode
  box and brush select (`debug_region_select`) on a box, loop, ring and
  face loop select (`select_mesh_loop` and Alt / Ctrl+Alt clicks) on the
  box, a torus and a one-sided rectangle, including the boundary cycle, and
  the edge and vertex slides (`slide_mesh_components`, the `edge_slide`
  transform mode, the G key with cancel and confirm) on a Catmull-Clark box, and
  loop cut (`doc/editor/mesh_modeling.md` "Verification"); it
  writes the editor's stderr to `logs/editor_stderr.txt`; the `Mcp_test`
  case `mesh_component_flush_and_select_all` covers the face-to-vertex flush
  and select all in CI.

## 10. Future work

- [plans/mesh_component_selection.md](../plans/mesh_component_selection.md) -
  skinned meshes, compute vertex and edge selection over the vertex and
  index buffers, and multiview overlays.
- [plans/mesh_modeling.md](../plans/mesh_modeling.md) - the remaining
  modeling work: grid fill, the rest of bevel, knife project and bisect,
  proportional and symmetry editing, compute box select.
