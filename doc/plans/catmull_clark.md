# Catmull-Clark optimization candidates

Status: proposed

This plan extends `doc/erhe/catmull_clark.md`, which describes erhe's Catmull-Clark
implementation and holds the stable numbering of the optimization items. Items
1, 2, 3, 11 and 12 are in place and are described there; the items below are
the ones still open, under the same numbers. Re-rank them against a fresh run
of the timing harness (`doc/erhe/catmull_clark.md`, "Timing harness") before
investing: the current Release profile puts `cc_quads` and the Source_table
traffic ahead of everything else that is still open.

## Item 4 - flatten `Source_table` to CSR

Structural fix for the allocation traffic. Today it is
`vector<vector<pair<float,index_t>>>` - one heap allocation per destination
element. Replace it with a single entries array plus an offsets array, built
count-then-fill (pass 1 counts per dst element, prefix-sum to offsets, pass 2
fills). Gain: high (removes O(#dst) small allocations, improves interpolation
locality). Complexity: high - `Source_table::add()` is shared by every
operation (Conway, CSG, edge-midpoint, ...), so it needs a two-phase API, or
`add()` kept as a fallback. Risk: medium-high - the exact weight values and
accumulation order must be preserved across all callers.

## Item 5 - arena or small-buffer backing (lighter alternative to item 4)

Keep the vector-of-vectors API but back the inner vectors with a monotonic /
arena allocator, or swap the inner type for a small-buffer-optimized container
(most CC source lists are 1-6 entries). Gain: captures most of item 4's
allocation win. Complexity: medium. Risk: medium - the allocator lifetime is
tied to the operation, but the API stays stable, so far less caller churn than
item 4. Usually the better return of the two; evaluate item 5 before item 4.

## Item 6 - replace the CC edge hash map with a flat array

CC creates exactly one midpoint per edge, yet uses
`unordered_map<pair,vector<index>,pair_hash>` with 2 lookups per corner in the
subdivide loop (and a weak xor `pair_hash`). Build a flat
`corner_to_edge_midpoint[corner]` during the edge pass (that pass already
iterates `get_edge_facets` and can resolve the corner), then index it directly
in the subdivide loop - no hashing, no per-key vector. Alternatively reuse
`Geometry::m_vertex_pair_to_edge` plus a flat `edge_to_midpoint[edge]`. Gain:
medium. Risk: medium - orientation and corner pairing must be exact; CC-local,
so no blast radius. Stopgap: `reserve()` the map and use a stronger hash (low
risk, partial gain).

## Item 7 - cache vertex valence once

The edge loop calls `get_vertex_corners(v).size()` repeatedly. Precompute
`valence[V]` once. Gain: small. Risk: low. Fold it into whatever else touches
that loop.

## Item 8 - fuse CC build passes

CC walks facets and corners in ~4 separate loops (original-vertex weights, edge
midpoints, facet centroids plus corner sources, facet subdivision). Some can
merge, cutting redundant iteration and improving cache behavior. Gain:
low-medium. Risk: medium - easy to disturb weight accumulation order. Do it
only after the data-structure wins, where it actually shows.

## Item 9 - parallelize the interpolation pass

Applying provenance over destination elements is embarrassingly parallel
(read-only sources, disjoint dst writes), so a taskflow loop over dst
vertices / corners / facets could scale near-linearly on big meshes. Caveats
that make this high-risk: the build / `Source_table::add` phase is not
thread-safe as written; the operation already runs on a worker thread and
multiple meshes already run concurrently, so intra-operation parallelism
competes with that; and AGENTS.md requires explicit sign-off for lock-free or
atomic techniques. Pursue only after items 4 and 5, and only for the read-only
interpolation half. Requires user sign-off.

## Item 10 - specialized in-place CC (Geogram-style)

Bypass the generic `Source_table` for the position update (compute face, edge
and vertex points directly into arrays as Geogram does), keeping a thin
provenance only for the corner attributes that actually need seam preservation.
Gain: very high, approaching Geogram. Risk and complexity: very high -
essentially a rewrite that abandons the unified operation model and is the most
likely to regress UV-seam and hard-normal handling. Worth it only if CC speed
becomes a real bottleneck and the other items are not enough. Requires user
sign-off.

## Batch creation for the remaining per-element operations

CC batch-creates its destination elements through the `map_dst_*` helpers;
the Conway operators and the other `Geometry_operation`-based operations still
create one element at a time. Since the geogram growth fix that is a
constant-factor cost (reserve / resize / notify across every attribute store,
per element) rather than a complexity-class one, but converting them still
removes it.

## `build_extra_connectivity()` early return

`build_extra_connectivity()` (`geometry.cpp`) returns - rather than continuing -
when any vertex has fewer than 3 corners, silently leaving the remaining
vertices' corner rings unsorted. Establish whether that is intended before
optimizing around it, and make the loop skip only the vertex it cannot sort if
it is not.
