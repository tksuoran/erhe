# Lattice Deform Geometry Node — Plan

Plan for adding a lattice (free-form) deformation feature to erhe, modeled on the
Houdini Lattice SOP (https://www.sidefx.com/docs/houdini/nodes/sop/lattice.html).

## 0. Implementation status (2026-07-26)

Phases A-D are implemented:

- `src/erhe/geometry/erhe_geometry/operation/lattice_deform.{hpp,cpp}` -
  the FFD operation (trilinear + Bezier over control point *offsets*, clamped
  outside the cage, invalid parameters pass through with a warning). The cage
  wireframe (Phase D) is emitted here too, behind
  `Lattice_deform_parameters::make_cage_debug_lines`.
- `src/erhe/geometry/test/test_lattice_deform.cpp` - unit tests (identity,
  uniform-offset translation, trilinear locality, outside-cage clamping,
  invalid-parameter pass-through).
- `src/editor/geometry_graph/nodes/lattice_node.{hpp,cpp}` - the graph node.
  Divisions changes resample the existing offset field trilinearly; toggling
  Auto Fit off freezes the currently fitted bounds into the manual cage
  fields; degenerate cage axes (flat inputs) are padded instead of rejected.
- Registered in the node factory, palette ("Operations"), editor CMake, the
  MCP `geometry_graph_add_node` enum, and `scripts/geometry_nodes_smoke_test.py`.

Deviations from the plan text below:

- Displacement is interpolated from control point *offsets* rather than
  absolute control point positions - algebraically equivalent (both bases have
  linear precision) but makes the identity fast path and clamping trivial.
- The operation's `make_cage_debug_lines` emits `Geometry::add_debug_line`
  entries on the *payload* geometry only. They do NOT reach the viewport: the
  output node's bake copies the geometry via `copy_with_transform()`, which
  drops debug entries, and the only debug-draw path (`Hover_tool`) reads the
  baked copy. Viewport cage rendering is done by `Lattice_tool` instead (see
  below).
- Facet texture coordinates are never regenerated (topology and
  parametrization survive the deform); only smooth vertex normals are, behind
  the `Regenerate normals` checkbox.

### Viewport gizmo editing (implemented 2026-07-26)

Control points are editable directly in the viewport. Activation contract:
the graph's **display or ghost designation is on the Lattice node** (that is
what puts the lattice-deformed geometry in the viewport) **and the graph's
output is bound to a scene node** via a `Geometry_graph_mesh` attachment in
the active scene. While active:

- `src/editor/tools/lattice_tool.cpp` (`Lattice_tool`, background tool,
  priority 4) finds the designated lattice + bound scene node, draws the
  deformed cage wireframe and billboard handles for every control point
  (orange = normal, white = hovered, yellow = selected), and consumes left
  clicks that land on a control point (12 px screen-space pick) to select it.
  The selection is the node's own `m_selected_point`, so the viewport and the
  node UI's offset editor stay in sync.
- `src/editor/transform/lattice_point_transform.cpp`
  (`Lattice_point_transform`) drives the standard transform gizmo, mirroring
  `Mesh_component_transform`: `update_anchor()` places the gizmo at the
  selected point's deformed world position each idle frame
  (`Transform_tool::update_for_view` dispatches via `Component_source`);
  `begin()/apply()/commit()` snapshot the node parameters at drag start,
  write the moved offset each update (`set_control_point_offset` +
  `mark_dirty` - the background re-evaluation provides live deformation
  feedback), and push a single undoable
  `Geometry_graph_parameter_operation` per gesture on release.
- While a lattice designation is active the gizmo belongs to the lattice
  (like Houdini: display flag on the SOP you are editing); clear the
  designation to return the gizmo to scene node selection. Mesh component
  mode (vertex/edge/face) takes precedence over lattice mode.
- Rotate/scale gizmo drags are harmless no-ops for the single selected point
  (the point sits at the anchor, which rotation/scale about the anchor fix).

### Transform-driver scene node = the cage frame (implemented 2026-07-26)

The Lattice node has an optional **Transform node** slot: drag a scene node
from the item tree onto the node UI (or Node Properties) to bind it. The
driver node's transform T is the **cage frame**: the cage box and the
control point offsets live in the driver's space. Each vertex is mapped
into cage space (T⁻¹) to find its lattice coordinates, and its displacement
is the interpolated offset rotated back by T's linear part:

    v' = v + linear(T) · interp_offsets( clamp( (T⁻¹ v − cage_min) / extent ) )

Consequences (the point of this formulation):

- **Zero offsets deform nothing, in any frame.** Moving the driver moves
  *the cage relative to the mesh* — repositioning the deformation region
  (the outside-cage clamping follows the cage) — it never moves the
  geometry itself. A whole-cage parent transform would reproduce itself
  through the FFD basis (affine precision) and just rigidly move the mesh,
  which is useless as a deformer; this was the first implementation and was
  corrected.
- Authored offsets apply within the driver's frame and rotate with it.

**T is the driver's LOCAL (parent-relative) transform**, so the intended
setup is: parent the driver node under the scene node the graph is bound
to. Then moving the parent moves mesh and cage together (no
re-deformation), and moving the driver moves the cage over the mesh. A
driver at scene root behaves the same when the bound node is at identity.

Implementation notes:

- Reference plumbing follows the `Scene_mesh_geometry_node` pattern:
  an `Asset_reference` with the new `Asset_type::node` (scene_local
  resolution matches scene nodes by name — one traits row in
  `assets/asset_key.cpp` plus a branch in `asset_manager.cpp`). Only the
  driver's *name* serializes; the transform itself is captured on the main
  thread and handed to background-evaluation shadows via
  `capture_evaluation_state()`.
- Live tracking uses a new per-frame hook,
  `Geometry_graph_node::update_live()`, called from
  `Geometry_graph_window::update_evaluation()` on every live node of every
  Graph_mesh asset. `Lattice_node::update_live()` re-resolves a deferred
  reference and re-captures the driver's local transform, marking the node
  dirty when it changed — so dragging the driver re-evaluates the graph.
- The cage-space evaluation lives in the operation
  (`Lattice_deform_parameters::cage_transform`); a non-invertible transform
  passes through with a warning.
- The viewport tool draws control points at `T * (rest + offset)`, and
  gizmo point drags invert T (`safe_inverse`) so the stored offsets stay in
  cage space.

## 1. Reference: what the Houdini Lattice SOP does

The Lattice SOP deforms geometry indirectly: the user edits a simple control shape
(the cage/lattice) and the deformation propagates to the complex source geometry.

Inputs (three):

1. **Data source** — the geometry to deform.
2. **Initial (rest) source** — the undeformed control shape.
3. **Deformed source** — the modified control shape. The deformation applied to
   each source point is derived from the difference between inputs 2 and 3.

Two deformation methods:

- **Lattice method** — the control shape is a regular grid of points (typically
  produced by a Bound or Box SOP with matching *Divisions*). Parameters:
  - *Divisions* (per axis) — must match the control geometry.
  - *Interpolation* — Linear / Bezier / NURBS.
  - *Order* (2–11) — smoothness for Bezier/NURBS; 2 = linear, 3–4 recommended.
  - *Falloff* — extends influence beyond the rest lattice bounds.
- **Points method** — arbitrary control geometry; each control point captures
  nearby source points with a kernel function (Wyvill, Blinn, …) and a radius.
  This is effectively a scattered-data / metaball-weighted deformer.

Key constraints in Houdini that inform our design:

- Rest and deformed control geometry must have identical topology/point count.
- The control geometry should enclose the geometry being deformed; behavior
  outside the cage is controlled by falloff (default: clamped influence).
- Typical workflow: `Bound (divisions on) → Lattice inputs 2 & 3`, with an
  Edit/Transform SOP between the Bound and input 3 to move lattice points.

## 2. Goals and non-goals

Goals:

- A **Lattice node** for the geometry node graph (`src/editor/geometry_graph/`)
  that deforms input geometry using a regular control-point grid (classic FFD,
  Sederberg & Parry 1986).
- Trilinear and Bézier (Bernstein basis over the whole grid) interpolation.
- Auto-fit cage from the input geometry's AABB, with manual override.
- Control points editable in the UI, serialized with the graph, undoable.
- Cage visualization (wireframe of the deformed lattice).
- Correct attribute handling: normals/tangents recomputed after the non-affine
  deform; other attributes preserved (topology is unchanged).

Non-goals for the first iteration (listed as future work in §8):

- Houdini's *Points method* (kernel-weighted arbitrary control geometry).
- NURBS/B-spline interpolation with arbitrary order.
- Falloff beyond the cage (v1 behavior: clamp to cage, like Houdini's default).
- Viewport gizmo dragging of individual lattice points (v1 edits via node UI;
  gizmo editing is a natural follow-up).
- A toolbar `Mesh_operation` ("apply lattice to selection") — the node graph is
  the right home; the operations-window path can be added later if wanted.

## 3. Design overview

erhe has two geometry-processing layers; the lattice lands in both, split the
same way existing operations are:

| Layer | New code | Modeled on |
|---|---|---|
| Math / library operation | `src/erhe/geometry/erhe_geometry/operation/lattice_deform.{hpp,cpp}` | `operation/normalize.cpp`, `Smooth` in `operation/remesh.cpp:439` |
| Graph node | `src/editor/geometry_graph/nodes/lattice_node.{hpp,cpp}` | `nodes/transform_node.{hpp,cpp}` |

### Where do the control points come from?

Houdini derives the deformation from *two extra geometry inputs* (rest cage,
deformed cage). erhe's graph currently has no node for editing individual points
of a geometry, so a faithful three-input port would be inert — there would be no
way to author the deformed cage. Therefore:

- **v1: control points are node parameters.** The node owns the cage definition
  (bounds + divisions) and a control-point offset array, edited in the node UI /
  Node Properties window. This matches Blender's Lattice-modifier mental model
  and is fully serializable through `write_parameters`/`read_parameters`, which
  the background shadow-evaluation requires anyway.
- **v2 (future, §8): optional rest/deformed geometry input pins** that override
  the internal control points, restoring the full Houdini workflow once a
  point-edit node (or per-point transform node) exists.

### Deformation math (Lattice method)

Cage = axis-aligned box `[cage_min, cage_max]` in the input geometry's local
space, subdivided into `divisions = (nx, ny, nz)` cells, giving
`(nx+1)·(ny+1)·(nz+1)` control points. Each control point `P_ijk` has a rest
position (regular grid) and a user-edited offset `O_ijk` (default zero).

For each source vertex `p`:

1. Normalize into cage-local coordinates `(s,t,u) = (p - cage_min) / (cage_max - cage_min)`,
   clamped to `[0,1]³` (Houdini-default behavior for points outside the cage;
   clamping means outside points move rigidly with the nearest cage face —
   continuous, no cracks).
2. Evaluate the deformed position:
   - **Trilinear** (Houdini "Linear", order 2): locate the cell containing
     `(s,t,u)`, trilinearly interpolate the 8 surrounding deformed control
     points. Local control, C0 across cell boundaries.
   - **Bézier** (Bernstein basis over the full grid):
     `p' = Σᵢ Σⱼ Σₖ Bᵢⁿˣ(s) Bⱼⁿʸ(t) Bₖⁿᶻ(u) · (P_ijk + O_ijk)`
     — the classic FFD formulation. Global smooth influence, C∞.
3. Identity fast path: if every offset is zero the node passes the input
   through copy-on-write (no geometry copy at all), mirroring
   `transform_node.cpp:42-48`.

Note the trilinear case with zero offsets reproduces positions exactly;
the Bézier case does too (Bernstein partition of unity + linear precision), so
"all offsets zero → identity" holds for both modes and the fast path is safe.

### Attribute handling

Topology is unchanged, so this is a position-only deform like `Smooth`
(`remesh.cpp:439`):

- Copy mesh + attributes 1:1 (`destination_mesh.copy(source_mesh, true)` +
  `copy_mesh_attributes()` — see `normalize.cpp:19-24`).
- Rewrite vertex positions with `get_pointf`/`set_pointf`
  (`geometry.hpp:766-773`; erhe meshes are single precision).
- Normals/tangents are invalidated by a non-affine map. Follow the `Smooth`
  precedent with a `regenerate_attributes` flag:
  - `true` (default): `process()` with
    `compute_facet_centroids | compute_smooth_vertex_normals` (+ tangent flags
    if the source had tangents).
  - `false`: keep source normals (acceptable for mild deformations, cheaper).
- Do **not** pass `process_flag_connect`/`build_edges` — connectivity is copied
  and unchanged. (Cheaper than `normalize.cpp`'s full flag set; `Smooth` is the
  precedent for topology-preserving ops.)

## 4. Phase A — library operation

New files: `src/erhe/geometry/erhe_geometry/operation/lattice_deform.{hpp,cpp}`

```cpp
// lattice_deform.hpp
namespace erhe::geometry::operation {

enum class Lattice_interpolation : int {
    trilinear = 0,
    bezier    = 1
};

struct Lattice_deform_parameters {
    glm::vec3               cage_min;
    glm::vec3               cage_max;
    glm::ivec3              divisions{2, 2, 2};        // cells per axis, >= 1
    Lattice_interpolation   interpolation{Lattice_interpolation::trilinear};
    // Offsets from rest positions, size (x+1)*(y+1)*(z+1), x-fastest order:
    // index = i + (divisions.x + 1) * (j + (divisions.y + 1) * k)
    std::vector<glm::vec3>  control_point_offsets;
    bool                    regenerate_attributes{true};
};

void lattice_deform(const Geometry& source, Geometry& destination, const Lattice_deform_parameters& parameters);

} // namespace erhe::geometry::operation
```

Implementation (`Lattice_deform : public Geometry_operation`, following
`normalize.cpp` structure):

1. `destination.get_attributes().unbind(); destination_mesh.copy(source_mesh, true);
   destination.get_attributes().bind(); copy_mesh_attributes();`
2. Degenerate-cage guard: if any `cage_max - cage_min` component is ~0, or the
   offset array size does not match `divisions`, log and pass through unchanged.
3. Per-vertex loop: `get_pointf` → clamp-normalize to `(s,t,u)` → evaluate basis
   against `rest + offset` control points → `set_pointf`.
   - Trilinear: `i = min(floor(s * nx), nx - 1)` etc., then lerp the 8 corners.
   - Bézier: precompute Bernstein weights per axis with the standard recurrence
     (divisions up to ~16 keep this cheap and numerically fine; Houdini caps
     Bézier at 30 divisions — enforce the same cap).
4. `post_processing(...)` gated on `regenerate_attributes` as described in §3.

Registration: add both files to the `operation/` block in
`src/erhe/geometry/CMakeLists.txt` (~lines 53–62; the list is explicit, no glob).

Unit tests, alongside `src/erhe/geometry/test/test_geometry_operation.cpp`:

- Zero offsets (both modes) → output positions equal input (within epsilon).
- Uniform offset `d` on all control points → rigid translation by `d`.
- A single moved corner control point in trilinear mode → only vertices in the
  adjacent cells move; vertices in far cells unchanged.
- Vertex outside the cage → clamped behavior (moves with nearest face).
- Offset-array size mismatch → pass-through, no crash.

## 5. Phase B — graph node

New files: `src/editor/geometry_graph/nodes/lattice_node.{hpp,cpp}`, skeleton
copied from `transform_node.{hpp,cpp}` (83 lines; ctor/evaluate/imgui/serialize).

### Pins

- Input 0: `Geometry_pin_key::geometry` "in"
- Output 0: `Geometry_pin_key::geometry` "out"

No parameter override pins in v1. (`Geometry_pin_key` has no `ivec3` and no way
to pipe a control-point array; the future rest/deformed cage inputs in §8 are
the right way to drive the lattice from upstream, matching Houdini.)

### Node state

```cpp
bool                    m_auto_fit{true};       // cage from source AABB each evaluate
glm::vec3               m_cage_min{-1.0f};      // used when !m_auto_fit
glm::vec3               m_cage_max{ 1.0f};
glm::ivec3              m_divisions{2, 2, 2};   // clamped to [1, 16] per axis
int                     m_interpolation{0};     // Lattice_interpolation
std::vector<glm::vec3>  m_offsets;              // resized on division change
bool                    m_regenerate_attributes{true};
```

One helper `resize_offsets()` owns the resize/resample logic and is called from
both the divisions widget and `read_parameters()` — never resize in two places.
When divisions change, resample existing offsets by evaluating the old lattice
at the new rest-grid positions (trilinear), so a tweaked cage survives a
resolution change instead of resetting.

### `evaluate(Geometry_graph&)`

```cpp
pull_inputs();
source = get_input(0).get_geometry();
if (!source) { set_output(0, {}); return; }
if (all offsets zero) { set_output(0, Geometry_payload{.value = source}); return; }  // CoW pass-through
compute cage_min/max: m_auto_fit ? source->get_aabb() : members;   // geometry.cpp:1727
auto destination = std::make_shared<Geometry>("lattice deformed");
erhe::geometry::operation::lattice_deform(*source, *destination, params);
set_output(0, Geometry_payload{.value = destination});
```

Do **not** also call `process_for_graph()` — the library operation already
post-processes (same reasoning as the comments in `subdivide_node.cpp:48` /
`geometry_unary_operation_node.cpp:25`).

Auto-fit caveat: like Houdini's note about Bound recomputing per frame, an
auto-fit cage refits whenever the upstream geometry changes, which re-anchors
the offsets. That is the expected v1 behavior; users who want a stable cage
uncheck Auto Fit (the checkbox copies the current fitted bounds into
`m_cage_min/max` when toggled off, i.e. "lock the Bound node").

### `imgui()` (renders on-canvas and in Node Properties — one implementation)

- `Checkbox("Auto fit")`; when off, `DragFloat3` for cage min/max.
- `DragInt3("Divisions", ..., 1, 16)` → `resize_offsets()`.
- `imgui_enum_combo("Interpolation", ...)` — Trilinear / Bezier
  (`graph_editor_widgets.hpp:32`).
- `Checkbox("Regenerate normals")`.
- Control-point editor: `imgui_index_stepper` over `(i,j,k)` (or three steppers)
  plus one `DragFloat3("Offset")` for the selected point, and a
  "Reset all" button. Compact enough for the node body; the Node Properties
  window shows the same UI at scale 1.
- Stats readout: `Vertices / Facets` like `subdivide_node.cpp:72-76`.
- Every mutation calls `mark_dirty()`; widget sizes use `content_scale()`.
- Undo for parameter edits is automatic via `commit_parameter_edit()` diffing
  `write_parameters` output — no extra work, but this makes complete
  serialization (below) mandatory.

### Serialization

`write_parameters`/`read_parameters` must round-trip **all** state (background
evaluation runs on a shadow clone reconstructed through this JSON):

- `write_vec3`/`read_vec3` for cage min/max, `write_ivec3`/`read_ivec3` for
  divisions (`geometry_graph_node.hpp:33-36`).
- Offsets as a flat JSON array of numbers (`[x0,y0,z0, x1,y1,z1, ...]`);
  on read, validate length against divisions, else reset via `resize_offsets()`.
- `read_parameters` ends with `mark_dirty()` (as `transform_node.cpp:80`).

## 6. Phase C — registration touch points

All six sites, traced from how `transform` is registered:

1. **Factory** — `src/editor/geometry_graph/geometry_graph_node_factory.cpp:29`:
   `else if (type_name == "lattice") { node = std::make_shared<Lattice_node>(); }`.
2. **Palette** — `geometry_graph_window.cpp:454` `build_palette()`: add
   `{.type_name = "lattice", .label = "Lattice"}` under "Operations" (or start a
   "Deform" category if we expect bend/twist/taper siblings later).
3. **CMake** — `src/editor/CMakeLists.txt` geometry-graph block (~lines 190–242):
   add `lattice_node.cpp/.hpp` next to `transform_node` (~line 239).
4. **MCP enum** — `src/editor/mcp/mcp_server_tool_list.cpp:905`: add
   `"lattice"` to the `geometry_graph_add_node` type enum (the handler itself is
   generic).
5. **Smoke test** — `scripts/geometry_nodes_smoke_test.py`:
   `section_every_node_type()` (~line 266): insert a lattice node in the op
   chain, set a nonzero offset via `geometry_graph_set_parameter`, assert vertex
   count is preserved and the graph evaluates.
6. **Docs** — status row in `doc/geometry-nodes-plan.md` (~line 26) and a short
   section in `doc/graph_editor.md`.

Free once registered: undo/redo, JSON save/load, copy/paste, Node Properties,
per-node mesh previews, display/ghost designation, MCP `geometry_graph_set_parameter`.

## 7. Phase D — cage visualization

Draw the deformed lattice as wireframe so the user can see what they are
editing, using the per-Geometry debug-draw channel
(`Geometry::add_debug_line`, `geometry.hpp:880-885`):

- In the library operation (or the node, post-evaluate), emit lines along the
  three grid directions between adjacent deformed control points, and optionally
  `add_debug_text` indices at the currently selected control point.
- Gate behind a node checkbox `Show cage` (default on), serialized like any
  other parameter.

This is the cheapest correct visualization; an interactive viewport gizmo for
dragging control points (the `Move_mesh_vertices_operation` /
`src/editor/tools/` pattern) is deliberately deferred to §8.

## 8. Future work (post-v1)

In rough priority order:

1. ~~**Viewport gizmo editing** of control points~~ — DONE, see §0
   "Viewport gizmo editing".
2. **Rest/deformed cage geometry inputs** (full Houdini parity): two optional
   `geometry` pins ("rest cage", "deformed cage"). When both are connected and
   have matching vertex counts `(nx+1)(ny+1)(nz+1)`, they override the internal
   offsets. Requires documenting/enforcing the point ordering convention and
   pairs naturally with a future "Lattice cage" source node (a Bound-SOP
   equivalent: box wireframe with divisions, auto-fit to an input geometry).
3. **B-spline interpolation + order parameter** (Houdini's NURBS mode, order
   2–11): local support with smoothness > trilinear; needed for high-division
   cages where global Bézier influence is undesirable.
4. **Falloff** outside the cage (Houdini's *Falloff* parameter) instead of hard
   clamping.
5. **Points method** — kernel-weighted deformation from arbitrary control
   geometry. This is a different algorithm (capture pass + weighted delta
   pass); if pursued, it should be its own node ("Point Deform"), matching how
   Houdini eventually split these workflows too.
6. **Selection restriction** — deform only selected facets/vertices, using the
   existing selective-operation machinery (`geometry_operation.hpp:147-169`),
   with a smooth boundary blend.
7. **Toolbar `Mesh_operation`** ("bake lattice into selected meshes") via
   `operations/geometry_operations.{hpp,cpp}` + the six `operations_window.cpp`
   wiring sites, if a non-graph workflow is wanted.

## 9. Verification plan

1. **Unit tests** (Phase A list in §4) — pure math, no editor needed.
2. **Build**: `scripts\build_ninja_win_vulkan.bat editor`.
3. **Smoke test**: `python scripts\geometry_nodes_smoke_test.py` with the new
   lattice section.
4. **Interactive/MCP check** (project-standard for graph work, see `AGENTS.md`
   and `mcp_server_usage.md`): headless editor → `geometry_graph_add_node`
   (box → lattice → output), `geometry_graph_connect`,
   `geometry_graph_set_parameter` to push one corner offset,
   `capture_screenshot` — verify the box bulges and the cage wireframe renders.
5. **Save/load round-trip**: author a lattice graph, save the graph asset,
   reload, confirm identical evaluation (guards the serialization contract the
   shadow evaluator depends on).

## 10. Open questions

- **Palette category**: keep "Operations" or introduce "Deform" now? (Plan
  assumes "Operations" until a second deformer exists.)
- **Division cap**: plan says 16 per axis (offset array ≤ 4913 points, JSON
  stays small). Houdini allows 30 for Bézier — raise later if needed.
- **Cage space**: v1 cage is in the geometry's local space (pre-instancing).
  If lattice-after-instancing matters, that interacts with
  `Realize_instances_node` ordering and is out of scope here.
