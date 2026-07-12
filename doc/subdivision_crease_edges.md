# Subdivision crease edges (issue #244) - implementation plan

Status: IMPLEMENTED 2026-07-04 (all six phases). The plan below is kept as
the design record; see "As built (2026-07-04)" at the end for deviations and
verification results.

Issue: https://github.com/tksuoran/erhe/issues/244

Add semi-sharp crease support to the Catmull-Clark subdivision geometry
operation, per DeRose, Kass, Truong, "Subdivision Surfaces in Character
Animation" (SIGGRAPH 1998). Each edge carries a scalar sharpness weight; a
weight of e.g. 3.6 means the first 3 subdivision levels use the sharp rules,
the 4th level uses a 60%/40% sharp/smooth blend, and further levels are fully
smooth. Also add a per-edge crease strength attribute, painting it onto
selected edges in the editor, and visualizing values as edge colors with
min/max range mapping (values may lie outside [0, 1]).

The paper's rules are fully transcribed in
`memory-bank/papers/derose_kass_truong_1998_subdivision_surfaces.md` (the PDF
need not be re-read). Everything below cites equations from that file.

## 1. The math (from the paper)

Per subdivision step, with s = edge sharpness (float >= 0):

- Face points: unchanged, always facet centroids.
- Edge point rules (paper eq. 1, 8, 11):
  - s = 0: smooth rule, `(v + e + f0 + f1) / 4`.
  - s >= 1: sharp rule, `(v + e) / 2` (plain midpoint).
  - 0 < s < 1: blend `(1 - s) * smooth + s * sharp`.
- Vertex point rules, by the count k of incident edges with sharpness > 0
  (paper eq. 2, 9, 10 + Appendix B):
  - k = 0 or 1 ("dart"): smooth vertex rule (eq. 2).
  - k >= 3 ("corner"): vertex does not move.
  - k = 2 ("crease vertex"): crease rule `(e_j + 6*v + e_k) / 8` where e_j,
    e_k are the far endpoints of the two crease edges - used as-is when
    v.s >= 1, where v.s = average of the two incident edge sharpnesses.
    For v.s < 1, blend with weight v.s. NOTE: the transcription says the
    blend is between the crease mask and the CORNER mask; OpenSubdiv blends
    smooth vs crease here. Re-read the transcription's Appendix B section
    during implementation and follow the paper; if ambiguous, match
    OpenSubdiv semantics (blend smooth <-> crease with weight v.s) and note
    the choice in code.
- Sub-edge sharpness after one step (Appendix B, Chaikin rule): a crease edge
  e_b splits into e_ab (touching neighbor crease edge e_a at the shared
  vertex) and e_bc (touching e_c):
  - `e_ab.s = max((e_a.s + 3*e_b.s) / 4 - 1, 0)`
  - `e_bc.s = max((3*e_b.s + e_c.s) / 4 - 1, 0)`
  - Simple/uniform fallback when an endpoint does not have exactly one other
    incident crease edge (crease end, corner, or crossing): `max(s - 1, 0)`.
  - Infinitely sharp edges stay infinitely sharp (represent as +inf float;
    `inf - 1 == inf` makes the decrement rules work unmodified).

This per-step formulation (Appendix B) subsumes the issue's "3.6 -> 3 sharp
steps, then 60/40 blend" description: sharpness decrements by ~1 per level,
so level 4 sees s = 0.6 and applies the fractional blend. No two-surface
interpolation (paper section 3 case 2) is needed.

Scalar fields (texcoords etc.) subdivide with the same masks as positions
(paper section 5.1) - erhe already does this via the shared `Source_table`
weights, so crease-modified masks automatically apply to all vertex-domain
attribute interpolation. This is correct per the paper and needs no extra
work.

## 2. Current state of the code (survey results, 2026-07-04)

Geometry core (`src/erhe/geometry/`):

- `Geometry` owns a `GEO::Mesh` with a first-class edge store. Edges are
  (re)built from facets by `Geometry::build_edges()`
  (`erhe_geometry/geometry.cpp:1242`), which CLEARS `m_mesh.edges` and
  recreates one edge per canonical `(min, max)` vertex pair, filling
  `m_vertex_pair_to_edge`, `m_vertex_to_edges`, `m_edge_to_facets`.
  Geogram's `edges.clear()` keeps attribute BINDINGS but wipes values, so a
  per-edge attribute does not survive a rebuild without explicit help.
- Attribute channels are hardcoded in `Mesh_attributes`
  (`erhe_geometry/geometry.hpp:244`): only facet/vertex/corner members exist.
  There is NO edge attribute anywhere yet. `Attribute_present<T>` pairs the
  value attribute with a `present` bool mask.
- Attribute names + descriptors: `c_*` constants and `Attribute_descriptors`
  (`geometry.hpp:117-171`).
- `Geometry_operation` (`erhe_geometry/operation/geometry_operation.hpp`)
  tracks weighted provenance in `Source_table`s. Edge hooks exist but are
  dormant: `m_dst_edge_sources` / `add_edge_source` are never read;
  `interpolate_mesh_attributes()` ends with a literal
  `// TODO edge attributes` (`geometry_operation.cpp:492`). Note ordering:
  `post_processing()` runs `interpolate_mesh_attributes()` BEFORE
  `destination.process()` builds the destination edges, so destination edge
  attributes cannot be written through that path as-is; they must be written
  after `build_edges()` has run.
- `m_src_edge_to_dst_vertex` maps each source edge (endpoint pair) to the
  midpoint vertices it spawned; `remap_component_selection()`
  (`geometry_operation.cpp:601-692`) already chains source edges to
  destination sub-edges - the exact mapping crease propagation needs.
- Catmull-Clark (`operation/subdivision/catmull_clark_subdivision.cpp`)
  assembles positions purely from `Source_table` weights across four batch
  phases (initial points, edge midpoints, facet centroids, quads). The
  smooth masks it builds:
  - Edge midpoint: endpoints weight 1 each + each adjacent facet centroid
    weight 1 (via `add_facet_centroid` with 1/corner_count per corner);
    normalized this is the paper's `(v + e + f0 + f1) / 4`.
  - Vertex point: `(n-3)/n` self (initial points phase) + `1/n` per incident
    edge endpoint pair (R term, edge phase) + centroid terms (F term, facet
    phase).
  Selective subdivision already pins selection-boundary vertices and places
  interface-edge midpoints exactly on the segment - conveniently identical
  to the sharp edge rule.
- Serialization: geometry-normative meshes persist the full geogram
  attribute dump (all elements including edges) bit-exact through the
  `ERHE_geometry` glTF extension (`doc/gltf_extensions/ERHE_geometry.md`,
  `doc/scene_serialization.md`), so a per-edge attribute persists for free
  once it survives to save time. (Core glTF cannot carry it - no edge
  domain - but the extension's attribute dump can.)

Editor (`src/editor/`):

- Edge selection exists: `Mesh_component_selection`
  (`tools/mesh_component_selection.hpp`) stores per-(Mesh, primitive,
  Geometry) entries with `std::set` of canonical vertex-pair edge keys;
  `Mesh_component_selection_tool` picks and toggles edges interactively;
  MCP `select_mesh_components` accepts `edges: [[v0, v1], ...]`.
- Selected edges are rendered as surface-aligned wide lines via
  `Primitive_renderer::add_surface_lines()` with a single uniform color per
  call (`tools/mesh_component_selection_tool.cpp:788-806`). Per-edge color
  requires `set_line_color()` between calls or per-endpoint `add_line()`.
- Color mapping utility exists: `editor::gradient::Palette::get(t)` with ~45
  named palettes (`graphics/gradients.hpp`); caller normalizes value -> t.
  Out-of-range t currently returns red (t < 0) / magenta (t > 1).
- Undo pattern for in-place attribute edits on the SAME `Geometry` object
  (so component selection survives): `Move_mesh_vertices_operation`
  (`operations/move_mesh_vertices_operation.cpp`) - stores parallel
  before/after arrays, `execute`/`undo` re-apply them. This is the pattern
  to copy; a crease edit does not even need a primitive rebuild (it changes
  no render geometry, only the overlay and future subdivisions).
- MCP mesh-component surface: `mcp_server_mesh_components.cpp` has
  `action_select_mesh_components` (parses edge pairs),
  `query_mesh_attribute_values` (edge domain currently answers "Edge
  attributes are not stored").
- Subdivide graph node (`geometry_graph/nodes/subdivide_node.cpp`) calls
  `catmull_clark_subdivision()` in a loop; the editor operation
  `Catmull_clark_subdivision_operation`
  (`operations/geometry_operations.cpp:42-60`) threads `selected_facets` +
  `Component_remap` through.

## 3. Design decisions

1. **Storage: a real per-edge float attribute in erhe::geometry**, not an
   editor-side map. Name: `c_edge_sharpness = "edge_sharpness"`, new
   `Attribute_present<float> edge_sharpness` in `Mesh_attributes` bound to
   `mesh.edges.attributes()` (the first edge-domain attribute). Absent or 0
   means smooth. Values are unclamped floats >= 0;
   `std::numeric_limits<float>::infinity()` means infinitely sharp.
   Rationale: must survive geometry operations, graph re-evaluation, and
   scene save/load; `.geogram` serialization then works with no new code.
2. **`build_edges()` becomes edge-attribute-preserving.** Before clearing
   the edge store, snapshot present `edge_sharpness` values keyed by
   canonical vertex pair; reapply after rebuild (vertex indices are not
   touched by an edge rebuild, so keys stay valid). This is the root-cause
   fix for "process() wipes edge attributes" - without it every
   `process_flag_build_edges` pass silently deletes crease data. Implement
   inside `build_edges()` itself so ALL callers are safe. Use a member
   scratch vector (clear-keep-capacity) per the allocation discipline.
3. **Crease rules implemented as weight-mask changes inside the existing CC
   phases**, not a separate pass. All masks are linear, so the fractional
   blend is a blend of `Source_table` weights:
   - Edge midpoint of edge with sharpness s (t = clamp(s, 0, 1)):
     endpoints weight `1 + t` each, adjacent facet centroids weight
     `(1 - t)` each (i.e. `add_facet_centroid` facet_weight
     `(1 - t) / corner_count`). t = 0 reproduces today's weights exactly;
     t = 1 is the plain midpoint.
   - Vertex point: pre-classify each vertex (count k of incident edges with
     s > 0; if k == 2 record the two far endpoints and
     v.s = average of the two sharpnesses; blend factor
     t_v = clamp(v.s, 0, 1)). Scale every smooth-mask contribution
     (initial `(n-3)/n`, R terms, F terms) by `(1 - t_v)` and add the
     crease mask `{far_j: 1/8, self: 6/8, far_k: 1/8}` scaled by `t_v`.
     k >= 3: pin (self weight 1, no smooth contributions).
     Selection-pinned/boundary vertices stay pinned regardless (pinning
     wins; a pinned vertex already satisfies both sharp-ish rules).
   - **Zero-crease fast path / bit-exactness**: when the source has no
     `edge_sharpness` attribute (or all values are 0), the emitted weights
     must be bit-identical to today's, so existing meshes and the timing
     harness see zero change. Guard the classification pass on attribute
     presence.
4. **Sub-edge sharpness propagation** happens in the CC operation after
   `post_processing()` (destination edges now exist): for each source edge
   with s > 0, compute the two child sharpnesses (Chaikin when the endpoint
   has exactly one other incident crease edge, else uniform `max(s-1, 0)`),
   find the child edges via `m_src_edge_to_dst_vertex` (endpoint image +
   midpoint) and `destination.get_edge(v0, v1)`, and write
   `edge_sharpness.set(...)`. Do not write zeros (keeps the attribute
   sparse via the `present` mask).
5. **Other operations**: topology-preserving operations (transform,
   normalize, reverse, repair...) propagate crease through a shared helper
   `Geometry_operation::propagate_edge_sharpness_identity()` that maps
   source edges to destination edges via `m_vertex_src_to_dst` after
   post-processing. Operations that destroy edge identity (boolean, remesh,
   decimate, conway) drop crease - acceptable and documented. Sqrt3
   subdivision: out of scope (triangle scheme; issue is about CC).
   `Geometry::merge`/`copy_with_transform` already raw-copies edge
   attributes (`geometry.cpp:1148`).
6. **Visualization**: in the mesh-component-selection edge overlay, when the
   geometry has `edge_sharpness` values, draw every creased edge colored by
   a gradient palette (default `viridis`) mapped over [min, max] of the
   present values (auto-range, recomputed on change; show the numeric range
   in the tool properties UI). Selected edges keep the selection color on
   top (thicker or drawn after). Clamp t to [0, 1] rather than the
   palette's red/magenta out-of-range sentinels, since the range is exact
   by construction.
7. **Painting UI**: keep it simple first - in edge component mode, the tool
   properties panel gains a "Crease sharpness" `DragFloat` (0..10 soft
   range, unclamped manual entry, checkbox/button for infinite) and "Apply
   to selected edges" / "Clear" buttons, wrapped in a new undoable
   `Set_edge_sharpness_operation` (Move_mesh_vertices_operation pattern:
   edges + before/after value arrays, same Geometry object mutated in
   place, no primitive rebuild, broadcast a message so overlays refresh).
   A drag-to-paint brush gesture can reuse the existing paint-select
   machinery later; not required by the issue.

## 4. Implementation phases

Each phase is a separate commit, built and verified before moving on
(ninja MSVC build + gtests where applicable + headless MCP verification for
editor phases).

### Phase 1 - geometry core: edge attribute channel

- `geometry.hpp`: add `c_edge_sharpness` constant, an
  `Attribute_descriptor s_edge_sharpness` (Transform_mode::none,
  Interpolation_mode::none), and `Attribute_present<float> edge_sharpness`
  to `Mesh_attributes`, bound to `mesh.edges.attributes()`; wire
  bind/unbind (first edge-domain member - check the constructor/unbind
  paths handle the edges manager).
- `Geometry::build_edges()`: snapshot/reapply present sharpness values
  keyed by canonical vertex pair (member scratch, no steady-state
  allocation after high-water mark).
- Convenience accessors on `Geometry`: `get_edge_sharpness(v0, v1)`,
  `set_edge_sharpness(v0, v1, s)` (canonicalize pair, resolve via
  `m_vertex_pair_to_edge`).
- gtests (`src/erhe/geometry/test/`): set values, run
  `process(process_flag_build_edges)` twice, values survive; save/load a
  `.geogram` round-trip if the test harness allows.

### Phase 2 - Catmull-Clark crease rules

- `catmull_clark_subdivision.cpp`:
  - Classification pre-pass (skipped entirely when the source has no
    present sharpness values): per-vertex incident-crease count, the two
    far endpoints for k == 2, v.s, t_v; per-edge t. Scratch vectors, one
    pass over `source_mesh.edges`.
  - Apply the mask changes from section 3 item 3 in the existing phases.
  - After `post_processing(...)`: propagate child sharpness (section 3
    item 4), Chaikin + uniform fallback.
- Resolve the Appendix B k == 2, v.s < 1 blend question against the
  transcription (see section 1 note) and document the choice in a comment.
- gtests, in a new `test/test_catmull_clark_crease.cpp`:
  - No-crease regression: subdivided cube positions bit-identical with the
    attribute absent vs present-but-all-zero vs pre-change reference.
  - Paper figure 7 setup: unit cube, one 4-edge crease loop, sharpness
    0 / 1 / 2 / 3 / inf; assert sharp edge midpoints stay on the segment
    for s >= 1 and move off it for s = 0; assert crease-vertex rule
    positions for a straight crease; assert corner pinning for 3 incident
    sharp edges.
  - Fractional: s = 0.5 midpoint equals the 50/50 blend of the s = 0 and
    s = 1 results (exact float check via the linear mask).
  - Propagation: s = 3.6 chain decrements per level (uniform and Chaikin
    values checked numerically); inf stays inf.
  - Iterated chains with `Post_processing::structural_only` intermediates
    still propagate sharpness (the propagation step must not depend on the
    full post-process flag set).

### Phase 3 - editor: undoable crease editing + MCP

- New `src/editor/operations/set_edge_sharpness_operation.{hpp,cpp}`:
  in-place, same-Geometry, before/after arrays keyed by edge vertex pairs;
  `execute`/`undo` write via `Geometry::set_edge_sharpness`; broadcast
  `mesh_geometry_changed` (or a lighter message) so overlays refresh. No
  primitive rebuild.
- `Mesh_component_selection_tool` properties panel (edge mode): sharpness
  DragFloat + infinite toggle + Apply / Clear buttons creating the
  operation via `Operation_stack`.
- MCP (`mcp_server_mesh_components.cpp` + dispatch + tool list):
  - `set_edge_sharpness` action: mesh/node ref, `edges` array of
    `[v0, v1]` pairs or `"selected"`, `sharpness` float or `"infinity"`;
    goes through the undoable operation.
  - `query_mesh_attribute_values` edge domain: return real
    `edge_sharpness` values instead of the "not stored" note.
- Verify with `erhe-headless-verify`: set values over MCP, query them back,
  undo/redo round-trip, save scene + reload, values survive.

### Phase 4 - visualization

- `Mesh_component_selection_tool::tool_render()` (or a small helper class
  it owns): when the hovered/selected geometry has present sharpness
  values, iterate creased edges, compute auto min/max, draw each edge with
  `gradient::viridis.get((s - min) / (max - min))` (guard max == min ->
  t = 1), using per-edge `set_line_color` + `add_surface_lines` (or
  per-endpoint `add_line`). Infinite values render at t = 1 and are
  reported separately in the UI text.
  Per-frame allocation discipline: reuse member scratch line buffers.
- Show "min .. max (n creased edges)" in the tool properties panel; add
  crease overlay style knobs (thickness, palette name if trivial) to
  `Mesh_component_style` (`src/editor/config/definitions/
  mesh_component_style.py` + regenerate codegen, build twice per the
  codegen gotcha).
- Verify via headless screenshots: cube with mixed sharpness values 0.5 /
  2 / 5, read the PNG, confirm distinct colors along the gradient.

### Phase 5 - subdivide node + operation integration

- Nothing needed in `Subdivide_node::evaluate()` for the math itself (the
  attribute rides on the input Geometry and phase 2 handles it), but:
  - Confirm the shadow-clone async evaluation copies edge attributes
    (payload copies use `copy_with_transform`/merge which raw-copy edge
    attributes; verify with a test graph).
  - The editor `Catmull_clark_subdivision_operation` path: confirm
    `remap_component_selection` keeps edge selections usable after
    subdivision (already implemented; smoke-check only).
- Add `propagate_edge_sharpness_identity()` to the topology-preserving
  operations that should keep creases (transform, reverse, normalize,
  repair); document the drop for boolean/remesh/decimate/conway in
  `geometry_operation.md`.
- Extend `scripts/geometry_nodes_smoke_test.py` with a crease section:
  create box -> set sharpness on 4 edges over MCP -> subdivide x3 ->
  assert vertex positions near the crease differ from the smooth run and
  `edge_sharpness` decremented values exist on the result.

### Phase 6 - docs + cleanup

- Update this document with as-built notes; add a short section to
  `doc/catmull_clark.md` (crease masks + where the propagation happens).
- Run the timing harness (`build_tests` Release, `doc/catmull_clark.md`
  workflow) to confirm the zero-crease path did not regress.

## 5. Risks / open questions

- **Appendix B vertex blend ambiguity** (crease-vs-corner or
  smooth-vs-crease for k == 2, v.s < 1): resolve from the transcription at
  implementation time; the test in phase 2 pins whichever is chosen.
- **`edges.clear()` attribute semantics**: Geogram keeps bindings, wipes
  values (verified in `MeshSubElementsStore::clear` docs). The phase 1
  snapshot/reapply covers erhe's `build_edges()`, but any OTHER code path
  that clears/rebuilds edges (Geogram-internal ops like remesh) will drop
  values - accepted per section 3 item 5.
- **Crossing creases** (k >= 3 corners with fractional sharpness): the
  paper forbids two creases sharing an edge and treats 3+ sharp edges as a
  hard corner; we do the same. No per-vertex sharpness attribute is stored
  (matches the paper's per-edge storage argument).
- **Selective subdivision interplay**: crease rules apply only to
  interior-selected vertices/edges; pinned boundary behavior is unchanged
  and takes precedence. Interface edges already use the plain midpoint,
  which coincides with the sharp rule, so no seam issues are expected -
  still worth one selective-CC-with-crease smoke check.
- **Sqrt3 subdivision** ignores the attribute entirely (values propagate
  nowhere; they are simply absent on the result). Acceptable; note in docs.
- **Painting on geometry-graph meshes**: graph-produced geometry is rebuilt
  on evaluation, so hand-painted creases only make sense on
  geometry-normative (scene) meshes or upstream source geometry. The UI
  should not offer Apply for a `Geometry_graph_mesh`-owned geometry (or
  should warn). A future "Set crease" graph node (select edges by angle /
  tag) is the graph-native answer - out of scope here.

## As built (2026-07-04)

Implemented in six commits on top of the plan commit, one per phase. All
phases landed as designed; deviations and findings:

- **Appendix B ambiguity resolved via OpenSubdiv** (D:\OpenSubdiv reference
  clone, `opensubdiv/sdc/crease.{h,cpp}`, `scheme.h`): the fractional vertex
  blend is between the PARENT rule mask and the CHILD rule mask (parent rule
  from the incident sharp-edge count: 2 = crease, 3+ = corner; child rule
  from the subdivided sharpnesses), weighted by the clamped average of the
  parent sharpness values that decay to zero across the step
  (`ComputeFractionalWeightAtVertex`). Chaikin child sharpness follows
  `SubdivideEdgeSharpnessAtVertex`: with 2+ semi-sharp (0 < s < inf) edges
  at the end vertex, `0.75*own + 0.25*avg(others) - 1`, clamped at 0;
  otherwise uniform `s - 1`. Infinity is a plain float +inf (survives the
  decrement unmodified). Edge points follow paper eq. 11 directly
  (blend by `t = clamp(s, 0, 1)`).
- **Vertex crease masks are a fix-up pass**, not inline scaling: the three
  build phases assemble the smooth mask incrementally with different implicit
  normalizations, so the fix-up computes the accumulated entry's weight sum
  and scales/appends mask components against it. A fully sharp edge point
  takes its corner sources from the endpoints (the interface-edge pattern) so
  texcoords/colors survive; fractional midpoints blend endpoint and centroid
  corner sources by t.
- **build_edges() preserves edge attributes** by snapshotting present values
  keyed by canonical vertex pair before `edges.clear()` (Geogram keeps
  bindings, wipes values) and reapplying after the rebuild. This also makes
  scene load work: load_scene re-runs process() on the loaded mesh.
- **Real bug found by the phase 5 tests**: `transform_mesh`'s non-identity
  path copies the mesh without attributes and transforms a hardcoded channel
  list; `edge_sharpness` was missing, so `bake_transform` (and the graph
  transform node) silently dropped creases. Fixed in both that list and
  `copy_attributes()`. reverse/normalize already carried creases through
  their whole-mesh copies.
- **Visualization simplifications vs the plan**: the overlay reuses the
  existing `Mesh_component_style` edge thickness and a fixed viridis palette
  (no new config knobs; palette choice can be a follow-up); out-of-range t is
  clamped rather than using the palette's red/magenta sentinels. The range
  readout lives in the viewport toolbar ("Crease min .. max (n edges, k
  inf)").
- **MCP surface**: `set_edge_sharpness` (explicit `[v0,v1]` pairs or the
  current edge component selection; number or "infinity"; `clear: true`
  removes; undoable), `get_mesh_attribute_values` domain=edge now reports
  `edge_sharpness`, and a new `catmull_clark` tool applies the editor CC
  operation to the selected node(s) for end-to-end scripting.
- **Verification**: 94 geometry gtests (plain + ASAN; analytic sharp /
  crease / corner / fractional positions, 3.6 decay chain through
  structural_only intermediates, infinity persistence, bit-exact zero-crease
  regression, topology-preserving-op and .geogram save/load round-trips);
  headless MCP end-to-end (set -> query -> undo x2 -> redo x2, selection-
  targeted set, clear, editor CC on a creased cube -> 12 child edges at
  ~2.6, scene save/load); screenshot-verified viridis overlay; geometry
  nodes regression sweep 129/129; Release timing harness unchanged
  (671-691 ms baseline vs 660-678 ms, same machine).
- **Still open** (unchanged from the plan): no per-vertex sharpness
  attribute (3+ sharp edges = hard corner), sqrt3 ignores the attribute,
  boolean/remesh/decimate/conway/triangulate drop it, glTF cannot carry it,
  and painting only makes sense on geometry-normative meshes (a "Set crease"
  graph node would be the graph-native answer - future work).
