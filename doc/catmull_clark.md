# Catmull-Clark subdivision: optimization notes

Status: the dominant (quadratic) cost was found and FIXED on 2026-07-02 (see
"Measured findings" below). The follow-up performance pass was DONE the same
day: the timing harness (see "Timing harness" at the end) plus items 11, 12,
1, 3 and 2 landed (see "Implemented optimizations (2026-07-02)"); the
remaining candidates (4/5/6/7/8, and sign-off-gated 9/10) stay as the queue
for any future round, re-ranked by the recorded Release numbers.

This document records candidate optimizations for erhe's Catmull-Clark
implementation, with rough gain / complexity / risk for each. It is the result
of comparing erhe's implementation against Geogram's `mesh_split_catmull_clark`,
updated with the measurements from the 2026-07-02 geometry nodes smoke sweep.

## Where the code lives

- erhe CC operation: `src/erhe/geometry/erhe_geometry/operation/subdivision/catmull_clark_subdivision.cpp`
- Shared provenance / interpolation machinery (used by ALL geometry operations):
  `src/erhe/geometry/erhe_geometry/operation/geometry_operation.{hpp,cpp}`
  - `Source_table` (per-destination-element weighted source lists)
  - `interpolate_mesh_attributes()` (applies provenance to ~24 attribute channels)
  - `post_processing()` = `interpolate_mesh_attributes()` + `sanitize()` + `process()`
- `interpolate_attribute<T>()` template: `src/erhe/geometry/erhe_geometry/geometry.hpp`
- `Geometry::process()` (the reprocess tail): `src/erhe/geometry/erhe_geometry/geometry.cpp`
- Geogram reference impl (read-only, for comparison):
  `.cpm_cache/geogram/.../src/lib/geogram/mesh/mesh_subdivision.cpp`

## Background: erhe vs Geogram

Both compute a genuine Catmull-Clark step (face points, smooth edge points, the
`(F + 2R + (n-3)P)/n` vertex update, quad output). Asymptotically both are
linear, O(V + E + F + C). The difference is the constant factor.

Geogram: operates in place on `GEO::Mesh`, a few preallocated contiguous arrays,
~6 linear passes, adjacency from existing mesh links, attribute interpolation
folded into the same passes (raw `madd_item`/`scale_item`), one `connect()` at
the end.

erhe: builds full weighted provenance into four `Source_table`s (each a
`vector<vector<pair<float,index_t>>>` -> roughly one heap allocation per
destination element), uses an `unordered_map<pair,vector<index>,pair_hash>` for
edge -> new vertex (2 hash lookups per corner), then runs a separate ~24-channel
interpolation pass, then a full `process()` reprocess.

The extra cost is not pure waste: erhe preserves per-corner (face-vertex)
attributes (UV seams, hard-normal discontinuities), keeps weighted provenance in
its unified `Mesh_attributes` model, and regenerates normals/UVs. Geogram's CC
only interpolates vertex attributes and leaves mesh boundaries unsmoothed.

Estimated gap: erhe is roughly 4-10x slower wall-clock, and likely
allocation-bound on large meshes. These are analytical estimates; the
2026-07-02 session added the Debug per-phase measurements below, and the
timing harness (last section) added Release numbers the same day.

## Measured findings (2026-07-02, Debug / MSVC, headless editor)

The geometry nodes stress sweep (box -> subdivide x5/x6 -> output) exposed a
cost NOT in the candidate list below, and it dominated everything else:

**Per-element destination creation was O(n) per call - CC was O(n^2).**
Geogram's `MeshSubElementsStore::create_sub_elements()` computed its capacity
doubling from the store SIZE instead of the store capacity, so once size passed
the last reservation every `create_vertices(1)` / `create_polygon()` issued a
reservation a hair above the previous one and `std::vector::reserve` satisfied
it with an exact reallocation of every attribute store. Measured: accumulated
`create_vertices(1)` time in the edge-midpoint loop grew 2 -> 26 -> 380 ->
6674 ms while edges grew 48 -> 192 -> 768 -> 3072 (x13-18 per x4); one CC
iteration on a 1536-facet input took 14370 ms; subdivide x6 on a box (98304
facets) was a practical hang (55+ minutes, unfinished). Fixed twice over:

- erhe side (commit 8e52a1b9): CC batch creates its destination elements (one
  `create_vertices(n)` per phase, one `create_quads(n)` for the subdivided
  facets) registered through new no-create `Geometry_operation` helpers
  (`map_dst_vertex_from_src_vertex`, `map_dst_vertex_from_src_facet_centroid`,
  `map_dst_facet_from_src_facet`). Other operations still create per element.
- geogram side (fork commit on `tksuoran/geogram`, erhe pin bumped in
  88376b78, upstream report
  https://github.com/BrunoLevy/geogram/issues/371): the growth loop now starts
  from `attributes_.capacity()`, restoring amortized O(1) per-element creation
  for ALL per-element callers (Conway operators included).

After both fixes (Debug, box source, includes each iteration's internal
post_processing):

| src facets | classify | initial_p | midpoints | centroids | quads | post_processing | iteration total |
|---|---|---|---|---|---|---|---|
| 384  | 0 | 0 | 7   | 5  | 39  | 57   | 114 ms  |
| 1536 | 0 | 1 | 30  | 24 | 174 | 289  | 542 ms  |
| 6144 | 1 | 8 | 159 | 94 | 760 | 1113 | 2215 ms |

Scaling is now ~linear (x4 per level). Full chains in the editor: subdivide x5
5.5 s, x6 25.1 s total (the x6 total includes the output node's render
processing of the 98304-facet result: process ~8 s, renderable ~5.7 s,
raytrace ~3.1 s; and each subdivide-node iteration also runs a redundant
`process_for_graph` re-doing connect+build_edges, ~2.4 s at the last level).

Two observations that adjust the priorities below:

- **The "out of scope" reprocess tail is now the biggest measured share.**
  `quads` + `post_processing` dominate the in-build cost, and outside build()
  the editor chain pays the reprocess three ways: CC's own internal
  `post_processing()` runs the FULL default flags (smooth normals + facet
  texcoords) on every intermediate iteration whose attributes are immediately
  re-subdivided away; the geometry-graph subdivide node then runs
  `process_for_graph` (connect + build_edges AGAIN) on the same mesh; and the
  output node re-processes the final result with the full render flags anyway.
  Candidates 11-12 below address this.
- The formerly suspected allocation-bound phases (`initial_p`, `midpoints`,
  `centroids` - Source_table + edge map traffic) are comparatively small in
  Debug after the creation fix. Candidates 1-6 remain valid but should be
  re-ranked against Release measurements before investing.

Also noted while reading the reprocess tail:
`build_extra_connectivity()` (geometry.cpp) contains an early `return` (not
`continue`) when ANY vertex has fewer than 3 corners, silently leaving the
remaining vertices' corner rings unsorted. Correctness quirk to investigate
while in the area.

## Scope of this document

Originally the `process()` reprocess (connect / update_connectivity /
build_edges / smooth normals / facet centroids / texcoord regeneration) was
treated as FIXED and out of scope. The 2026-07-02 measurements overturned
that: after the creation fix the reprocess tail is the largest measured share
of an editor subdivide chain, so candidates 11-12 bring it in scope. The
original "in-scope" cost is the CC `build()` itself plus
`interpolate_mesh_attributes()`; within that, the presumed dominant costs are:

1. `Source_table` allocation traffic (one alloc per destination element).
2. The ~24-channel interpolation pass.
3. The edge hash map (2 lookups per corner).

(The Release baseline in the last section confirms this ranking: at src 24576
the reprocess tail is ~215 ms, `cc_quads` ~121 ms, `interpolate` ~83 ms,
midpoints+centroids ~77 ms combined.)

Confirmed facts the candidates rely on:
- `get_vertex_corners` / `get_corner_facet` / `get_edge_facets` are O(1)
  cached-vector lookups (precomputed in `process()`).
- `Geometry` already maintains `m_vertex_pair_to_edge` (`get_edge(v0,v1)`).
- `interpolate_attribute` only early-outs when `interpolation_mode == none`;
  otherwise it iterates every destination element even for unbound channels.

## Candidate optimizations (ranked by value / risk)

| # | Optimization | Est. gain (in-scope) | Complexity | Risk | Scope |
|---|---|---|---|---|---|
| 1 | Skip interpolating channels that are unbound / regenerated | high | low | low | shared base |
| 2 | Normalize weights once (not per channel) | medium-high | low-med | low | shared base |
| 3 | Pre-size `Source_table` outer vectors | low-med | low | low | shared base |
| 4 | Flatten `Source_table` to CSR (count-then-fill) | high | high | med-high | shared base |
| 5 | Arena / SBO backing for inner vectors (lighter #4) | high | medium | medium | shared base |
| 6 | Replace CC edge hash map with flat corner->midpoint | medium | medium | medium | CC-local |
| 7 | Cache vertex valence once | low | low | low | CC-local |
| 8 | Fuse CC build passes | low-med | medium | medium | CC-local |
| 9 | Parallelize interpolation pass | high (big meshes) | high | high | shared base |
| 10 | Specialized in-place CC (Geogram-style) | very high | very high | very high | CC rewrite |
| 11 | Structural-only post-processing for intermediate iterations | high (chains) | low-med | low | shared base |
| 12 | Drop redundant process_for_graph after operations that post-process | medium (chains) | low | low | editor graph |

### 1. Skip channels that are unbound or will be regenerated
`interpolate_mesh_attributes()` calls `interpolate_attribute` for ~24 channels;
each does a full O(dst x sources) pass whenever `interpolation_mode != none`,
even when the source attribute has no present values. Two sub-wins:
(a) guard each channel with a cheap "any-present / is-bound" check and skip empty
ones -- for a typical mesh most of joint weights/indices, `color_1`,
`texcoord_1`, tangents, bitangents, aniso are unbound, removing a large fraction
of the 24 passes;
(b) the reprocess regenerates smooth normals and facet texcoords (and tangents
derive from them), so interpolating `vertex_normal` / `corner_normal` /
`*_texcoord_*` / `*_tangent` / `*_bitangent` is wasted for CC -- skip them.
Gain: likely the single biggest in-scope win for ordinary meshes.
Risk: low. Gate (b) on the process flags the op passes, to stay correct.

### 2. Normalize provenance weights once
Every one of the ~24 channels recomputes `sum_weights` per destination element
before dividing. Compute the per-element inverse-sum once (or store normalized
weights when the `Source_table` is finalized) and have each channel do a plain
weighted sum. Gain: removes ~24x the normalization loop (medium-high once #1
trims the channel count). Risk: low. Keep the per-channel `source_present` skip
semantics intact (presence can vary per channel), so the simplest correct form
is precomputing the unnormalized sum and dividing with the present-check in
place.

### 3. Pre-size `Source_table` outer vectors
CC output counts are known a priori: dst vertices = V + E + F,
dst facets = sum of facet degrees, dst corners = 4 x dst facets. `Source_table::add`
currently geometric-grows the outer vector and capacity-checks on every call.
Reserve to final counts up front. Gain: small-medium (kills outer reallocs +
branch). Risk: low. Good warm-up to pair with #4/#5.

### 4. Flatten `Source_table` to CSR
Structural fix for the allocation traffic. Today it is
`vector<vector<pair<float,index_t>>>` -- one heap allocation per destination
element, the likely dominant in-scope cost on large meshes. Replace with a single
entries array + an offsets array, built count-then-fill (pass 1 counts per dst
element, prefix-sum to offsets, pass 2 fills). Gain: high (removes ~O(#dst)
small allocations, improves interpolation locality). Complexity: high --
`Source_table` / `add()` is shared by every operation (Conway, CSG, edge-midpoint,
...), so it needs a two-phase API (or keep `add()` as a fallback). Risk: med-high
-- must preserve exact weight values and accumulation order across all callers;
broad blast radius.

### 5. Arena / small-buffer backing (lighter alternative to #4)
Keep the vector-of-vectors API but back the inner vectors with a monotonic/arena
allocator, or swap the inner type for a small-buffer-optimized container (most CC
source lists are 1-6 entries). Gain: captures most of #4's allocation win.
Complexity: medium. Risk: medium -- allocator lifetime tied to the operation, but
the API stays stable so far less caller churn than #4. Often the better ROI than
full CSR.

### 6. Replace the CC edge hash map with a flat array
CC creates exactly one midpoint per edge, yet uses
`unordered_map<pair,vector<index>,pair_hash>` with 2 lookups per corner in the
subdivide loop (and a weak xor `pair_hash`). Build a flat
`corner_to_edge_midpoint[corner]` during the edge pass (the edge pass already
iterates `get_edge_facets` and can resolve the corner), then index it directly in
the subdivide loop -- no hashing, no per-key vector. Alternatively reuse
`Geometry::m_vertex_pair_to_edge` + a flat `edge_to_midpoint[edge]`. Gain: medium
(removes map allocs + 2 hashes/corner). Complexity: medium. Risk: medium --
orientation/corner-pairing must be exact; CC-local, so no blast radius.
Stopgap: `reserve()` the map and use a stronger hash (low risk, partial gain).

### 7. Cache vertex valence once
The edge loop calls `get_vertex_corners(v).size()` repeatedly (currently
re-fetched per edge, and note the historical copy-paste bug there). Precompute
`valence[V]` once. Gain: small. Risk: low. Fold into whatever else is touched.

### 8. Fuse CC build passes
CC walks facets/corners in ~4 separate loops (original-vertex weights, edge
midpoints, facet centroids + corner sources, facet subdivision). Some can merge
to cut redundant iteration and improve cache behavior. Gain: low-med. Risk:
medium -- easy to disturb weight accumulation order. Do only after the
data-structure wins, where it actually shows.

### 9. Parallelize the interpolation pass
Applying provenance over destination elements is embarrassingly parallel
(read-only sources, disjoint dst writes), so a taskflow loop over dst
vertices/corners/facets could scale near-linearly on big meshes. Caveats that
make this high-risk: the build / `Source_table::add` phase is not thread-safe as
written; the op already runs on a worker thread and multiple meshes already run
concurrently (intra-op parallelism competes with that); and AGENTS.md forbids
lock-free/atomics without explicit sign-off. Pursue only after #1/#2/#4, and only
for the read-only interpolation half.

### 10. Specialized in-place CC (Geogram-style)
Bypass the generic `Source_table` for the position update (compute
face/edge/vertex points directly into arrays as Geogram does), keeping a thin
provenance only for corner attributes that actually need seam preservation. Gain:
very high (approaches Geogram). Risk/complexity: very high -- essentially a
rewrite that abandons the unified operation model and is most likely to regress
UV-seam / hard-normal handling. Only worth it if CC speed becomes a real
bottleneck and the other items are not enough.

### 11. Structural-only post-processing for intermediate iterations
`catmull_clark_subdivision()` ends with `post_processing()` using
`default_post_process_flags` (connect, build_edges, facet centroids, smooth
vertex normals, facet texcoords) on EVERY iteration -- but in an iterated
chain (the subdivide node loops CC N times; the editor's Catmull-Clark
operation can be applied repeatedly too) the normals/texcoords of every
intermediate result are discarded: the next iteration re-derives everything
from positions + connectivity, and the final consumer (the geometry graph
output node, or `Mesh_operation`'s rebuild) re-processes with its own flags.
Give the subdivision entry points a way to request
`structural_post_process_flags` (connect + build_edges + centroids only --
the parameter already exists on `post_processing()`) for intermediate
iterations, keeping the full flags for the final one. Combined with #1(b)
(skip interpolating regenerated channels) this removes both the *computation*
and the *interpolation* of throwaway normals/texcoords per level. Measured
share at the last x6 level: post_processing was ~1.1 s of a 2.2 s iteration
at src 6144 in Debug (grows with level). Risk: low -- flags plumb through
existing machinery; verify normals/UVs on the FINAL output are unchanged.

### 12. Drop the redundant process_for_graph after self-post-processing operations
The geometry graph's subdivide node runs `process_for_graph()` (connect +
build_edges) after `catmull_clark_subdivision()`, which just ran the same
steps inside its own `post_processing()`. Same pattern in other operation
nodes wrapping `Geometry_operation`-based ops. Measured: ~2.4 s of redundant
work at the x6 last level (Debug). Either drop the call for operations that
post-process, or fold it into #11's flag plumbing (node requests the flags it
needs, operation guarantees them). Risk: low; editor-side only.

## Implemented optimizations (2026-07-02)

The recommended order below was executed the same day; commits in branch
`geometry_nodes` (harness 0171c8c4, item 11 299d0332, item 12 9ad251ae,
item 1 70169d17, item 3 eeb93354, item 2 650dc354).

- **Item 11** - `catmull_clark_subdivision()` / `sqrt3_subdivision()` take an
  optional `Post_processing` level; the subdivide node runs intermediate
  iterations with `structural_only`. `SubdivisionChain` gtests prove the
  final output is bit-identical.
- **Item 12** - the subdivide, conway, and unary-operation graph nodes no
  longer run `process_for_graph()` on results whose operation just ran
  connect + build_edges itself. Boolean / join / realize / source nodes keep
  it (their results genuinely need it).
- **Item 1** - `interpolate_attribute()` skips channels with no present
  source values; `post_processing()` gained `regeneration_flags` so channels
  the upcoming (or, for structural chains, the final) process() regenerates
  (`vertex_normal_smooth`, `corner_texcoord_1`) are not interpolated.
- **Item 3** - CC pre-sizes the provenance tables at each batch-create point
  and `m_vertex_src_to_dst` up front; no outer-vector growth mid-build.
- **Item 2** - per-destination weight sums are computed once per
  `Source_table`; the position loop and every fully-present channel use them
  (bit-identical: same additions, same order). Partially-present channels
  keep the per-channel filtered sum.

Measured results (same workloads as the baselines below):

- Release harness chain (7 CC iterations, cube -> 98304 facets):
  689 -> ~570 ms. Last-level `interpolate` 82.7 -> ~18 ms (item 1);
  intermediate-level `process` -25..30% (item 11); the rest spread across
  the cc_* phases (item 3).
- Editor (Debug, headless, geometry-graph stress): subdivide x5
  5.5 -> 4.4 s, x6 25.1 -> 17.8 s, boolean of two subdiv3 5.3 -> 4.5 s.
  Full smoke sweep 65/65 after the changes.
- Items 3 and 2 measured marginal on the harness chain (box content has few
  present channels); item 2's win applies to attribute-rich meshes where
  fully-present channels drop their per-element sum loops and presence
  lookups.

Returns were diminishing after item 2, so the pass stopped there per plan.
The dominant remaining Release costs at the last level are `process`
(~194 ms - now all legitimately needed connect/build_edges/normals/texcoord
work on the final 98304-facet mesh) and `cc_quads` (~113 ms - the subdivide
loop itself; items 6/8 territory), then midpoints + centroids (~75 ms -
items 4/5 allocation work).

## Recommended order (as planned before implementation)

Revised after the 2026-07-02 measurements: build the timing harness first
(below), then do 11 -> 12 -> 1 -> 3 -> 2. Items 11/12 attack the largest
measured share (the reprocess tail of iterated chains) at low risk; 1/3/2 are
the low-risk shared-base wins (skipped channels + single normalization + no
outer reallocs). Then evaluate 5 (arena) over 4 (CSR) -- same allocation win,
far less caller churn -- and 6 as a clean CC-local follow-up, all gated on
what the Release numbers actually show. Treat 9 and 10 as separate,
sign-off-required efforts.

Note: items 1-5, 9 and 11 touch the shared `Geometry_operation` base, so they
also benefit the Conway operators, CSG, and other operations that build
provenance -- not just Catmull-Clark. Two further shared-base notes from
2026-07-02: per-element destination creation is amortized O(1) again since the
geogram fork fix, but converting the remaining per-element operations (Conway
ops etc.) to batch creation via the `map_dst_*` helpers still removes
per-call overhead (reserve/resize/notify across every attribute store per
element) -- a constant-factor win, no longer a complexity-class one. And
`build_extra_connectivity()`'s early-return quirk (see Measured findings)
should be understood before optimizing around it.

## Timing harness (added 2026-07-02)

Permanent per-phase timing exists now; re-measure with it after every change
instead of re-instrumenting.

- **Phase markers**: `erhe::geometry::operation::Operation_timing` +
  `Scoped_phase_timer` (`erhe_geometry/operation/operation_timing.{hpp,cpp}`).
  A thread-local collector the harness installs; inert (one branch per phase
  scope, no clock reads, no allocation) when nothing is installed. Markers
  live in `catmull_clark_subdivision.cpp` (`cc_classify`, `cc_initial_points`,
  `cc_edge_midpoints`, `cc_facet_centroids`, `cc_quads`) and
  `Geometry_operation::post_processing` (`interpolate`, `sanitize`,
  `process`).
- **Harness test**: `src/erhe/geometry/test/test_timing_harness.cpp`,
  `TimingHarness.DISABLED_CatmullClarkChain` - 7 whole-mesh CC iterations from
  a processed cube (last level: 24576 -> 98304 facets, the editor's
  "subdivide x6 on the default box" stress case), one table row per level.
- **Release + Debug on Windows**: `scripts\configure_tests.bat` generates
  `build_tests/` (VS generator = multi-config, tests ON, **no ASAN, profiler
  none** - both would distort timings). One tree serves both configs:

  ```bat
  scripts\configure_tests.bat
  cmake --build build_tests --target erhe_geometry_tests --config Release
  build_tests\src\erhe\geometry\test\Release\erhe_geometry_tests.exe ^
      --gtest_also_run_disabled_tests --gtest_filter=*TimingHarness*
  ```

  (`build_tests_asan/` remains the correctness configuration.)

### Baseline measurements (2026-07-02, before items 11/12/1/3/2)

Release (MSVC, /O2), milliseconds per whole-mesh CC iteration:

| src facets | classify | initial_p | midpoints | centroids | quads | interpolate | sanitize | process | other | total |
|---|---|---|---|---|---|---|---|---|---|---|
| 384   | 0.0 | 0.1 | 0.7  | 0.4  | 1.8   | 0.4  | 0.2  | 2.5   | 0.3  | 6.2   |
| 1536  | 0.0 | 0.2 | 2.6  | 1.7  | 6.9   | 1.8  | 0.7  | 10.4  | 1.2  | 25.6  |
| 6144  | 0.1 | 1.0 | 10.5 | 7.1  | 29.8  | 10.0 | 2.8  | 42.1  | 7.3  | 110.8 |
| 24576 | 0.2 | 3.3 | 44.0 | 33.0 | 120.6 | 82.7 | 11.5 | 215.3 | 29.7 | 540.3 |

Debug (MSVC, ~10x Release, same ranking):

| src facets | classify | initial_p | midpoints | centroids | quads | interpolate | sanitize | process | other | total |
|---|---|---|---|---|---|---|---|---|---|---|
| 384   | 0.1 | 0.5  | 7.1   | 4.4   | 18.1   | 10.8  | 3.7   | 34.5   | 3.4   | 82.5   |
| 1536  | 0.2 | 1.6  | 27.3  | 20.6  | 69.4   | 41.9  | 14.3  | 135.9  | 15.3  | 326.4  |
| 6144  | 0.9 | 7.3  | 118.3 | 72.6  | 306.2  | 200.3 | 56.4  | 562.4  | 58.3  | 1382.7 |
| 24576 | 3.6 | 26.0 | 494.9 | 363.0 | 1183.3 | 843.3 | 224.3 | 2308.7 | 244.2 | 5691.3 |

The Release ranking confirms the Debug-based priorities: the `process`
reprocess tail is the largest phase (40% of the last level), then `cc_quads`,
then `interpolate`. Items 11/12 (reprocess tail) first, then 1/3/2
(interpolation + Source_table), remains the right order. Debug's container
overhead inflates the absolute numbers ~10x but does not reorder the phases
on this workload.

## Semi-sharp creases (2026-07-04, issue #244)

The CC operation honors the per-edge `edge_sharpness` attribute
(`doc/subdivision_crease_edges.md` is the full design + as-built record;
DeRose/Kass/Truong 1998 rules with OpenSubdiv Sdc rule/blend semantics):

- Edge points blend the smooth mask with the plain midpoint by
  `t = min(sharpness, 1)`; vertex points with 2+ incident sharp edges get a
  fix-up pass that rewrites the accumulated smooth `Source_table` entry into
  the parent/child rule blend (crease mask `1/8 + 6/8 + 1/8`, corner mask =
  pinned). Child sub-edges receive Chaikin-subdivided sharpness after
  `post_processing()` (the destination edge store exists only then).
- Performance: the whole path is gated on a per-edge presence scan
  (`cc_crease_classify` phase); with no sharpness values present the emitted
  weights are bit-identical to the pre-crease implementation, and the Release
  timing-harness chain shows no measurable delta (baseline 671-691 ms vs
  660-678 ms with the crease code, same machine/run).
- New phase markers: `cc_crease_classify`, `cc_crease_vertex_masks`,
  `cc_crease_propagate`.
