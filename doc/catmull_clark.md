# Catmull-Clark subdivision: optimization notes

Status: analysis only, not yet implemented. Picked up later.

This document records candidate optimizations for erhe's Catmull-Clark
implementation, with rough gain / complexity / risk for each. It is the result
of comparing erhe's implementation against Geogram's `mesh_split_catmull_clark`.

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
allocation-bound on large meshes. These are analytical estimates, not measured.

## Scope of this document

The `process()` reprocess (connect / update_connectivity / build_edges / smooth
normals / facet centroids / texcoord regeneration) is treated as FIXED and is
out of scope here. The "in-scope" cost being optimized is the CC `build()`
itself plus `interpolate_mesh_attributes()`. Within that, the dominant costs are:

1. `Source_table` allocation traffic (one alloc per destination element).
2. The ~24-channel interpolation pass.
3. The edge hash map (2 lookups per corner).

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
concurrently (intra-op parallelism competes with that); and CLAUDE.md forbids
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

## Recommended order

Do 1 -> 3 -> 2 first: all low-risk, and together they likely remove a large share
of in-scope time (skipped channels + single normalization + no outer reallocs)
with minimal blast radius. Then evaluate 5 (arena) over 4 (CSR) -- same
allocation win, far less caller churn. 6 is a clean CC-local follow-up. Treat 9
and 10 as separate, sign-off-required efforts.

Note: items 1-5 and 9 touch the shared `Geometry_operation` base, so they also
benefit the Conway operators, CSG, and other operations that build provenance --
not just Catmull-Clark.

## Before committing to any of these: measure

These are analytical estimates. Add a timing harness in
`src/erhe/geometry/test/` that subdivides a known mesh through the current path
and prints per-phase timings (build vs. interpolation vs. the excluded
reprocess), so each optimization can be measured in isolation rather than guessed.
