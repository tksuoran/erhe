# Catmull-Clark subdivision

Stability: mostly stable

This document describes erhe's Catmull-Clark implementation: how it relates to
Geogram's, where its cost sits, which optimizations it carries, how semi-sharp
creases work, and the timing harness used to re-measure any change.

## Where the code lives

- erhe CC operation: `src/erhe/geometry/erhe_geometry/operation/subdivision/catmull_clark_subdivision.cpp`
- Shared provenance / interpolation machinery (used by ALL geometry operations):
  `src/erhe/geometry/erhe_geometry/operation/geometry_operation.{hpp,cpp}`
  - `Source_table` (per-destination-element weighted source lists)
  - `interpolate_mesh_attributes()` (applies provenance to ~24 attribute channels)
  - `post_processing()` = `interpolate_mesh_attributes()` + `sanitize()` + `process()`
- `interpolate_attribute<T>()` template: `src/erhe/geometry/erhe_geometry/geometry.hpp`
- `Geometry::process()` (the reprocess tail): `src/erhe/geometry/erhe_geometry/geometry.cpp`
- Geogram reference implementation (read-only, for comparison):
  `.cpm_cache/geogram/.../src/lib/geogram/mesh/mesh_subdivision.cpp`

## erhe vs Geogram

Both compute a genuine Catmull-Clark step (face points, smooth edge points, the
`(F + 2R + (n-3)P)/n` vertex update, quad output). Asymptotically both are
linear, O(V + E + F + C). The difference is the constant factor.

Geogram operates in place on `GEO::Mesh`, over a few preallocated contiguous
arrays: ~6 linear passes, adjacency from existing mesh links, attribute
interpolation folded into the same passes (raw `madd_item` / `scale_item`), one
`connect()` at the end.

erhe builds full weighted provenance into four `Source_table`s (each a
`vector<vector<pair<float,index_t>>>`, roughly one heap allocation per
destination element), uses an `unordered_map<pair,vector<index>,pair_hash>` for
edge -> new vertex (2 hash lookups per corner), then runs a separate ~24-channel
interpolation pass, then a full `process()` reprocess.

The extra cost is not pure waste: erhe preserves per-corner (face-vertex)
attributes (UV seams, hard-normal discontinuities), keeps weighted provenance in
its unified `Mesh_attributes` model, and regenerates normals and UVs. Geogram's
CC only interpolates vertex attributes and leaves mesh boundaries unsmoothed.
erhe is roughly 4-10x slower wall-clock than Geogram on the same mesh.

## Cost model

Per-element destination creation is amortized O(1). Both sides of that were
fixed: CC batch-creates its destination elements (one `create_vertices(n)` per
phase, one `create_quads(n)` for the subdivided facets) through the no-create
`Geometry_operation` helpers `map_dst_vertex_from_src_vertex`,
`map_dst_vertex_from_src_facet_centroid` and `map_dst_facet_from_src_facet`; and
Geogram's `MeshSubElementsStore::create_sub_elements()` now grows from
`attributes_.capacity()` instead of the store size (fork fix, upstream report
https://github.com/BrunoLevy/geogram/issues/371), which restores amortized
growth for every per-element caller, Conway operators included. Before that
fix a `create_vertices(1)` loop reallocated every attribute store per element,
which made CC quadratic and subdivide x6 on a box a practical hang; any new
per-element creation path must preserve the amortized behavior.

Current Release profile of a whole-mesh CC iteration, from the timing harness
chain (cube -> 98304 facets, 7 iterations, ~570 ms total). At the last level
(24576 source facets) the phases rank:

| phase | last-level cost |
|---|---|
| `process` (the reprocess tail: connect / build_edges / normals / texcoords) | ~194 ms |
| `cc_quads` (the subdivide loop) | ~113 ms |
| `cc_edge_midpoints` + `cc_facet_centroids` (Source_table + edge-map traffic) | ~75 ms |
| `interpolate` | ~18 ms |

Debug inflates the absolute numbers ~10x but does not reorder the phases on
this workload. The reprocess tail is legitimate work on the final mesh; the
remaining candidate optimizations aim at `cc_quads` and the Source_table
traffic (see "Future work").

Facts the optimization items rely on:

- `get_vertex_corners` / `get_corner_facet` / `get_edge_facets` are O(1)
  cached-vector lookups (precomputed in `process()`).
- `Geometry` already maintains `m_vertex_pair_to_edge` (`get_edge(v0,v1)`).
- `interpolate_attribute` only early-outs when `interpolation_mode == none`;
  otherwise it iterates every destination element even for unbound channels.

## Optimization items

Items 1-12 are a stable numbering: source comments and tests cite them by
number, so the numbers keep their meaning. Items 1, 2, 3, 11 and 12 are in
place and are described here as behavior; items 4-10 are candidates and are
described in `doc/plans/catmull_clark.md` under the same numbers.

- **Item 1 - skip channels that are unbound or regenerated.**
  `interpolate_attribute()` skips channels with no present source values, and
  `post_processing()` takes `regeneration_flags` so channels the upcoming (or,
  for a structural chain, the final) `process()` regenerates
  (`vertex_normal_smooth`, `corner_texcoord_1`) are not interpolated at all.
  This is the largest in-scope win for ordinary meshes: last-level
  `interpolate` drops from ~83 ms to ~18 ms.
- **Item 2 - normalize provenance weights once.** Per-destination weight sums
  are computed once per `Source_table`; the position loop and every
  fully-present channel reuse them, with the same additions in the same order,
  so results are bit-identical. Partially-present channels keep their own
  per-channel filtered sum, because presence varies per channel.
- **Item 3 - pre-size the `Source_table` outer vectors.** CC output counts are
  known a priori (dst vertices = V + E + F, dst facets = sum of facet degrees,
  dst corners = 4 x dst facets), so CC pre-sizes the provenance tables at each
  batch-create point and `m_vertex_src_to_dst` up front; no outer vector grows
  mid-build.
- **Item 11 - structural-only post-processing for intermediate iterations.**
  `catmull_clark_subdivision()` and `sqrt3_subdivision()` take an optional
  `Post_processing` level. In an iterated chain the normals and texcoords of
  every intermediate result are discarded - the next iteration re-derives them
  from positions and connectivity - so the subdivide node runs intermediate
  iterations with `structural_only` (connect + build_edges + centroids) and the
  final one with the full default flags. `SubdivisionChain` gtests
  (`test_subdivision_chain.cpp`) prove the final output is bit-identical.
- **Item 12 - no redundant `process_for_graph` after a self-post-processing
  operation.** The subdivide, conway and unary-operation graph nodes do not run
  `process_for_graph()` on results whose operation just ran connect +
  build_edges itself. Boolean, join, realize and source nodes keep the call:
  their results genuinely need it.

Items 1-5, 9 and 11 touch the shared `Geometry_operation` base, so they benefit
the Conway operators, CSG and every other operation that builds provenance, not
just Catmull-Clark.

## Semi-sharp creases

The CC operation honors the per-edge `edge_sharpness` attribute;
`doc/subdivision_crease_edges.md` holds the full design (DeRose / Kass / Truong
1998 rules with OpenSubdiv Sdc rule and blend semantics).

- Edge points blend the smooth mask with the plain midpoint by
  `t = min(sharpness, 1)`; vertex points with 2 or more incident sharp edges get
  a fix-up pass that rewrites the accumulated smooth `Source_table` entry into
  the parent/child rule blend (crease mask `1/8 + 6/8 + 1/8`, corner mask =
  pinned). Child sub-edges receive Chaikin-subdivided sharpness after
  `post_processing()`, because the destination edge store exists only then.
- The whole path is gated on a per-edge presence scan (`cc_crease_classify`
  phase). With no sharpness values present the emitted weights are
  bit-identical to a crease-free implementation and the Release timing-harness
  chain shows no measurable delta.
- Phase markers: `cc_crease_classify`, `cc_crease_vertex_masks`,
  `cc_crease_propagate`.

## Timing harness

Per-phase timing is permanent: re-measure with it after every change instead of
re-instrumenting.

- **Phase markers**: `erhe::geometry::operation::Operation_timing` +
  `Scoped_phase_timer` (`erhe_geometry/operation/operation_timing.{hpp,cpp}`).
  A thread-local collector the harness installs; inert (one branch per phase
  scope, no clock reads, no allocation) when nothing is installed. Markers live
  in `catmull_clark_subdivision.cpp` (`cc_classify`, `cc_initial_points`,
  `cc_edge_midpoints`, `cc_facet_centroids`, `cc_quads`) and
  `Geometry_operation::post_processing` (`interpolate`, `sanitize`, `process`).
- **Harness test**: `src/erhe/geometry/test/test_timing_harness.cpp`,
  `TimingHarness.DISABLED_CatmullClarkChain` - 7 whole-mesh CC iterations from a
  processed cube (last level: 24576 -> 98304 facets, the editor's
  "subdivide x6 on the default box" stress case), one table row per level.
  `TimingHarness.DISABLED_CatmullClarkChainStructuralIntermediates` runs the
  same chain the way the editor's subdivide node does (item 11).
- **Release and Debug on Windows**: `scripts\configure_tests.bat` generates
  `build_tests/` (VS generator = multi-config, tests ON, **no ASAN, profiler
  none** - both would distort timings). One tree serves both configs:

  ```bat
  scripts\configure_tests.bat
  cmake --build build_tests --target erhe_geometry_tests --config Release
  build_tests\bin\Release\erhe_geometry_tests.exe ^
      --gtest_also_run_disabled_tests --gtest_filter=*TimingHarness*
  ```

  (`build_tests_asan/` remains the correctness configuration.)

## Future work

- [plans/catmull_clark.md](plans/catmull_clark.md) - optimization items 4-10
  and the `build_extra_connectivity()` early-return quirk.
