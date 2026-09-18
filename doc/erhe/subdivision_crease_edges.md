# Subdivision crease edges

Stability: stable

Semi-sharp creases in the Catmull-Clark subdivision operation, per DeRose,
Kass, Truong, "Subdivision Surfaces in Character Animation" (SIGGRAPH 1998),
with rule selection and fractional blending following OpenSubdiv's Sdc
semantics. Each edge carries a scalar sharpness weight: a weight of 3.6 means
the first 3 subdivision levels use the sharp rules, the 4th uses a 60/40
sharp/smooth blend, and further levels are fully smooth. The editor paints the
weight onto selected edges and visualizes it as edge color.

The paper's rules are transcribed in
`memory-bank/papers/derose_kass_truong_1998_subdivision_surfaces.md`; the
equation numbers below cite that file.

## The math

Per subdivision step, with s = edge sharpness (float >= 0):

- Face points: unchanged, always facet centroids.
- Edge point rules (eq. 1, 8, 11):
  - s = 0: smooth rule, `(v + e + f0 + f1) / 4`.
  - s >= 1: sharp rule, `(v + e) / 2` (plain midpoint).
  - 0 < s < 1: blend `(1 - s) * smooth + s * sharp`.
- Vertex point rules, by the count k of incident edges with sharpness > 0
  (eq. 2, 9, 10 and Appendix B):
  - k = 0 or 1 ("dart"): smooth vertex rule (eq. 2).
  - k >= 3 ("corner"): the vertex does not move.
  - k = 2 ("crease vertex"): crease rule `(e_j + 6*v + e_k) / 8`, where e_j and
    e_k are the far endpoints of the two crease edges.
- Sub-edge sharpness after one step (Appendix B, Chaikin rule): a crease edge
  e_b splits into e_ab (touching neighbor crease edge e_a at the shared vertex)
  and e_bc (touching e_c):
  - `e_ab.s = max((e_a.s + 3*e_b.s) / 4 - 1, 0)`
  - `e_bc.s = max((3*e_b.s + e_c.s) / 4 - 1, 0)`
  - Uniform fallback when an endpoint does not have exactly one other incident
    crease edge (crease end, corner, or crossing): `max(s - 1, 0)`.
  - Infinitely sharp edges stay infinitely sharp: sharpness is a plain float,
    and `inf - 1 == inf` makes the decrement rules work unmodified.

This per-step formulation subsumes the "3.6 -> 3 sharp steps, then a 60/40
blend" description: sharpness decrements by about 1 per level, so level 4 sees
s = 0.6 and applies the fractional blend. No two-surface interpolation (paper
section 3 case 2) is needed.

Scalar fields (texcoords and the rest) subdivide with the same masks as
positions (paper section 5.1). erhe gets that for free through the shared
`Source_table` weights, so crease-modified masks apply to every vertex-domain
attribute automatically.

**Fractional vertex blend.** The paper's Appendix B is ambiguous about what the
k = 2, v.s < 1 blend interpolates between; erhe follows OpenSubdiv
(`opensubdiv/sdc/crease.{h,cpp}`, `scheme.h`). The blend is between the PARENT
rule mask and the CHILD rule mask - parent rule from the incident sharp-edge
count (2 = crease, 3+ = corner), child rule from the subdivided sharpnesses -
weighted by the clamped average of the parent sharpness values that decay to
zero across the step (`ComputeFractionalWeightAtVertex`). Chaikin child
sharpness follows `SubdivideEdgeSharpnessAtVertex`: with 2 or more semi-sharp
(0 < s < inf) edges at the end vertex, `0.75*own + 0.25*avg(others) - 1`
clamped at 0; otherwise uniform `s - 1`.

## Edge sharpness attribute

Sharpness is a real per-edge float attribute of `erhe::geometry`, not an
editor-side map, so it survives geometry operations, graph re-evaluation and
scene save/load.

- `c_edge_sharpness = "edge_sharpness"`, an `Attribute_descriptor`
  (`Transform_mode::none`, `Interpolation_mode::none`) and
  `Attribute_present<float> edge_sharpness` in `Mesh_attributes`, bound to
  `mesh.edges.attributes()` - the first edge-domain attribute. Absent or 0
  means smooth; values are unclamped floats >= 0, and
  `std::numeric_limits<float>::infinity()` means infinitely sharp.
- `Geometry::get_edge_sharpness(v0, v1)` / `set_edge_sharpness(v0, v1, s)`
  canonicalize the vertex pair and resolve the edge through
  `m_vertex_pair_to_edge`.
- **`build_edges()` preserves edge attributes.** Geogram's `edges.clear()`
  keeps attribute bindings but wipes values, so `build_edges()` snapshots the
  present sharpness values keyed by canonical vertex pair before clearing and
  reapplies them after the rebuild (a member scratch vector, cleared keeping
  capacity). Doing it inside `build_edges()` makes every caller safe; without
  it any `process_flag_build_edges` pass silently deletes crease data, and
  scene load - which re-runs `process()` on the loaded mesh - would lose it.
- Geometry-normative meshes persist the whole geogram attribute dump, edges
  included, through the `ERHE_geometry` glTF extension
  (`doc/gltf_extensions/ERHE_geometry.md`), so sharpness persists there. Core
  glTF has no edge domain and cannot carry it.

## Catmull-Clark crease rules

The rules are weight-mask changes inside the existing CC phases, not a separate
pass: every mask is linear, so a fractional blend is a blend of `Source_table`
weights.

- A classification pre-pass, guarded on attribute presence, records per vertex
  the incident-crease count k, the two far endpoints when k == 2, v.s and the
  blend factor, and per edge `t = clamp(s, 0, 1)`.
- **Edge midpoints**: endpoints weight `1 + t` each, adjacent facet centroids
  weight `(1 - t)` each. t = 0 reproduces the smooth weights exactly; t = 1 is
  the plain midpoint. A fully sharp edge point takes its corner sources from
  the endpoints (the interface-edge pattern) so texcoords and colors survive; a
  fractional midpoint blends endpoint and centroid corner sources by t.
- **Vertex points** are handled by a fix-up pass rather than by scaling inline.
  The three build phases assemble the smooth mask incrementally with different
  implicit normalizations, so the fix-up computes the accumulated entry's
  weight sum and scales and appends the mask components against it: crease mask
  `{far_j: 1/8, self: 6/8, far_k: 1/8}`, corner mask = pinned (self weight 1,
  no smooth contributions). Selection-pinned and boundary vertices stay pinned
  regardless; pinning wins, and a pinned vertex already satisfies both
  sharp-ish rules.
- **Zero-crease bit-exactness**: with no present sharpness values the whole
  path is skipped and the emitted weights are bit-identical to a crease-free
  implementation, so existing meshes and the timing harness see no change.
- **Child sharpness propagation** runs in the CC operation after
  `post_processing()`, because the destination edges exist only then: for each
  source edge with s > 0 compute the two child sharpnesses (Chaikin when the
  endpoint has exactly one other incident crease edge, else uniform), find the
  child edges through `m_src_edge_to_dst_vertex` and
  `destination.get_edge(v0, v1)`, and write them. Zeros are not written, which
  keeps the attribute sparse through the `present` mask. Propagation does not
  depend on the full post-process flag set, so it still happens in an iterated
  chain whose intermediate levels run `Post_processing::structural_only`.

## Which operations carry creases

- Topology-preserving operations (transform, reverse, normalize, repair)
  propagate sharpness through
  `Geometry_operation::propagate_edge_sharpness_identity()`, which maps source
  edges to destination edges via `m_vertex_src_to_dst` after post-processing.
  `Geometry::merge` and `copy_with_transform` raw-copy edge attributes.
  `transform_mesh`'s non-identity path copies without attributes and transforms
  an explicit channel list, so `edge_sharpness` has to appear in that list and
  in `copy_attributes()` - it was missing once, and `bake_transform` (and the
  graph transform node) silently dropped creases as a result.
- Operations that destroy edge identity - boolean, remesh, decimate, conway,
  triangulate - drop sharpness.
- Sqrt3 subdivision ignores the attribute: it is a triangle scheme, and
  sharpness values are simply absent on its result.
- Crossing creases are not representable: the paper forbids two creases sharing
  an edge and treats 3 or more sharp edges at a vertex as a hard corner, and
  erhe does the same. There is no per-vertex sharpness attribute.

## Editor

- `Set_edge_sharpness_operation`
  (`src/editor/operations/set_edge_sharpness_operation.{hpp,cpp}`) is the
  undoable edit: before/after value arrays keyed by edge vertex pairs, applied
  in place to the *same* `Geometry` object so the component selection survives.
  It changes no render geometry, only the overlay and future subdivisions, so
  it triggers no primitive rebuild; it broadcasts a message so overlays
  refresh.
- `Mesh_component_selection_tool` in edge mode offers a sharpness value plus
  Apply and Clear in the viewport toolbar, which build that operation through
  the `Operation_stack`.
- The crease overlay draws every edge carrying a sharpness value colored by a
  viridis gradient mapped over the min..max of the present finite values, using
  the existing `Mesh_component_style` edge thickness. Out-of-range t is
  clamped rather than showing the palette's red/magenta sentinels, because the
  range is exact by construction. Selected edges keep the selection color on
  top. The toolbar reports "Crease min .. max (n edges, k inf)". Per-frame
  allocation discipline applies: the line buffers are member scratch.
- MCP: `set_edge_sharpness` (explicit `[v0, v1]` pairs or the current edge
  component selection; a number or `"infinity"`; `clear: true` removes;
  undoable), `get_mesh_attribute_values` with `domain=edge` reports
  `edge_sharpness`, and `catmull_clark` applies the editor CC operation to the
  selected nodes for end-to-end scripting.
- Painting only makes sense on geometry-normative (scene) meshes or upstream
  source geometry: graph-produced geometry is rebuilt on evaluation, so
  hand-painted creases on it are discarded.

## Verification

- `src/erhe/geometry/test/test_edge_sharpness.cpp` covers the attribute
  channel: values survive repeated `process(process_flag_build_edges)` and a
  `.geogram` round-trip.
- `src/erhe/geometry/test/test_catmull_clark_crease.cpp` covers the rules:
  bit-exact zero-crease regression, analytic sharp / crease / corner /
  fractional positions, the 3.6 decay chain through `structural_only`
  intermediates, infinity persistence, and topology-preserving-operation
  round-trips.
- End-to-end: set over MCP, query back, undo and redo, editor CC on a creased
  cube, scene save and load; the crease overlay is screenshot-verified.

## Future work

- [plans/geometry_graph/geometry_nodes.md](../plans/geometry_graph/geometry_nodes.md) -
  a "Set crease" graph node (select edges by angle or tag), the graph-native
  answer to painting creases on graph-produced geometry.
