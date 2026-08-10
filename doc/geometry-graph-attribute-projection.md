# Geometry graph: attribute projection node (design research)

Research 2026-08-10, motivated by creation 18 (fish): a geometry-graph node
that projects a selected attribute from a SOURCE mesh onto a TARGET mesh -
output is the target with that one attribute channel replaced by projected
values; everything else on the target is untouched. Prime use case: transfer
authored UVs from a clean proxy onto a sculpted box->lattice->subdivide body
whose inherited per-quad UVs are unusable; general tool for transferring
colors, masks, aniso control or skin weights between LODs / remeshes.

## Node contract

- Type name `project_attribute` ("Project Attribute"), two geometry inputs
  like `Boolean_node` (input 0 = target, input 1 = source), one geometry
  output.
- Parameters:
  - `attribute`: enum over transferable channels - texcoord_0/1/2,
    color_0/1, normal, aniso_control (extensible via
    `Attribute_descriptors`). Seam-capable channels (texcoords, colors,
    normals) are written on the CORNER domain of the target; a
    `domain` override (corner / vertex) is optional polish.
  - `method`: `closest_point` (default) or `along_normal` (ray both ways
    along the target normal, closest-point fallback on miss).
  - `max_distance`: 0 = unlimited; a miss keeps the target's own value.
  - `reject_backfacing` (default true): ignore source hits whose facet
    normal opposes the target normal - prevents thin shells (fins!) from
    sampling the far side.
- Missing source input or absent source channel: pass target through
  unchanged + node warning (imgui shows projected / missed counts).

## Projection core - what makes it solid

New `erhe_geometry/operation/project_attribute.{hpp,cpp}` using the
EXISTING two-source `Geometry_operation` constructor (lhs=target,
rhs=source; CSG already uses this shape).

1. Destination = full copy of the target (topology-preserving path,
   `copy_mesh_attributes()`; `structural_post_process_flags` so nothing
   regenerates over the result).
2. Build a TRIANGULATED COPY of the source mesh carrying an
   `orig_facet` facet attribute, then a `GEO::MeshFacetsAABB` over it.
   The copy is mandatory: MeshFacetsAABB triangulates its input mesh even
   through the const overload (const_cast inside Geogram) - never hand it
   the live source mesh. Geogram fan-triangulation preserves vertex ids,
   so a hit triangle maps back to (original facet, 3 original vertices).
3. Per target element (corner domain: the corner's vertex position):
   - `nearest_facet(p, nearest_point, sq_dist)` (or the ray variant for
     `along_normal` - MeshFacetsAABB also has ray Intersection queries).
   - Barycentric weights of `nearest_point` in the hit triangle
     (skip degenerate/zero-area triangles when deriving weights).
   - Map the triangle's vertices to the original source facet's CORNERS
     (vertex-match within that one facet) and blend the source corner
     values with those weights. Sampling stays inside ONE source facet,
     so no individual sample ever averages corners across a seam.
     (Necessary but NOT sufficient for seams - see "Seams &
     discontinuities" below.)
   - Blend per the channel's `Interpolation_mode` (linear; normalized =
     re-normalize after blending, for normal-like channels).
   - Respect `Attribute_present` flags: only sample where the source has
     the value present; miss/reject -> keep the target's value.
4. Write through the typed `Mesh_attributes` accessors
   (`corner_texcoord(i)`, `corner_color(i)`, ...). Direct typed writes
   beat routing through `Source_table` + `interpolate_mesh_attributes()`:
   the provenance machinery interpolates EVERY channel from the source,
   but here the destination's other channels come from the TARGET. (If a
   generic-per-channel route is preferred later, expose the internal
   `interpolate_attribute()` per-channel entry point instead.)

Cost: O(target corners x log source tris) - a 40k-corner body against a
few-thousand-facet proxy is instantaneous on the background evaluation
thread; build the AABB once per evaluation.

## Seams: value-space charts + exact seam imprinting

Requirements this section serves (2026-08-10 review): source texture
coordinates are TILED - values run past 1 (repeating patterns) - and
seams must transfer EXACTLY, not approximately.

### Value-space semantics for repeating UVs

Never fract()/wrap anywhere in the transfer. Tiling continuation
(u: 0.8 -> 1.2 across an edge) is CONTINUOUS and must not be treated as
a seam; a seam exists only where the two facets' corner values across a
shared edge DISAGREE (author discontinuity, e.g. u jumps 3.95 -> 0.0).
Chart decomposition therefore runs in VALUE space: connected components
of source facets under "shared edge whose corner values match within
epsilon". Projected values keep their > 1 ranges verbatim - the GPU's
sampler wrap does the tiling, exactly as on the source.

### Why naive sampling smears

With independent per-corner nearest queries, two corners of the SAME
target facet can land on opposite sides of a source seam (u~3.95 next
to u~0.0), and rasterization sweeps the whole tiled texture backwards
across that facet - the classic stripe Blender Data Transfer / Houdini
AttribTransfer show. The seam does not align with target topology, so
without topology changes the discontinuity would cut THROUGH target
facets, which corner-domain storage cannot represent.

### Exact seam imprinting (cut the target along the projected seam)

Project the seam INTO the target: insert vertices and split crossed
facets so the seam curve becomes real target edges, then sample each
side from its own chart. Corner-domain values on the two sides of the
new edges reproduce the source seam's two sides exactly.

1. **Label** every target vertex with its nearest chart (per-chart
   triangulated AABBs; only charts within the distance bound compete).
   Facets whose vertices share one label - the vast majority - sample
   that chart directly, no cutting.
2. **Contour** mixed-label facets: for the two competing charts define
   the scalar field d(v) = dist_chartA(v) - dist_chartB(v) on the
   facet's vertices. Zero crossings along facet edges give cut points
   (linear root first, refined by a few bisection steps against the
   actual chart distances). Each crossing is computed ONCE per edge and
   shared by both adjacent facets - watertight, no T-junctions.
3. **Split** the facet along the crossing chord(s): convex facets by
   chord insertion; multi-crossing / junction cases by a 2D CDT in the
   facet plane. Crossings within epsilon of an existing vertex snap to
   it (sliver control; the vertex becomes an on-seam vertex).
4. **Sample** each sub-facet from its single chart as in the core
   algorithm. The new edge chain IS the seam: both sides evaluate at
   identical 3D positions against their own chart, so the value pairs
   along the cut match the source seam's two sides exactly - including
   tiled ranges (4.0 on one side, 0.0 on the other).

New cut vertices ride the existing `Geometry_operation` machinery: an
edge split at parameter t registers provenance weights (1-t, t) from the
edge endpoints (a per-edge-position variant of `make_edge_midpoints`),
so positions, normals and every OTHER attribute interpolate through the
standard `interpolate_mesh_attributes()` path for free; only the
projected channel is written by chart sampling.

Why bisector contouring instead of geometrically projecting each source
seam segment: the chart-distance zero set is a well-defined scalar
contour evaluated ON the target surface - it cannot fold or
self-intersect on concave targets, terminates/closes naturally, and is
by construction consistent with the sampling rule (each side samples its
nearest chart). Its deviation from the seam's true geometric image
shrinks with proxy-target distance (zero for a proxy on the surface);
for distant proxies the `along_normal` method constrains the
correspondence if the geometric image is preferred.

- `cut_seams` node parameter, default ON for UV-like channels; OFF falls
  back to per-facet chart-coherent sampling (clamp band at seams, no
  topology change) for consumers that must keep vertex count.
- Vertex/facet growth is bounded by the number of seam-facet crossings -
  a handful of cuts along one seam line on typical meshes.
- Channels without wrap topology (colors, normals, aniso control) skip
  cutting by default but share the chart-coherence code path.
- Interactions: `max_distance` misses produce a hard projected/retained
  boundary by construction (only when the cap is opted into). Backface
  rejection composes - on thin shells the two sides are distinct charts,
  so labeling keeps each side sampling its own side.

## Graph integration

- `Project_attribute_node` mirrors `Boolean_node` plumbing: two geometry
  input pins, `evaluate()` pulls both, runs the operation, sets output;
  `write_parameters`/`read_parameters` for attribute/method/distance;
  factory + palette registration ("Attributes" category).
- Both inputs are evaluated in the graph's shared local space, so no
  space alignment is normally needed; a `scene_mesh` source from another
  node should be pre-aligned with a `transform` node (document in node
  help). Zero-offset proxies deformed by the SAME lattice node track the
  target for free - the fish's proxy can be a low-res cylinder branch
  run through the same lattice.

## Fish pipeline payoff

`box -> lattice -> subdivide -> project_attribute(texcoord_0) <- proxy`
where the proxy branch is any mesh with good UVs (a cylinder/capsule with
natural cylindrical UVs, optionally deformed by the same lattice). Also
pairs with a trivial future `uv_atlas` node - the operation already
exists (`erhe_geometry/operation/make_atlas.{hpp,cpp}`, Geogram
mesh_make_atlas, same core the `generate_texture_coordinates` MCP op
uses) - atlas the proxy once, project onto the sculpt. Projection keeps
AUTHORED layouts; atlas alone gives arbitrary charts.

## Verification plan

- gtest in erhe_geometry tests: cube -> displaced-cube projection
  (channel values match analytically); tiled-UV seam imprint (cylinder
  source with u in 0..4 and a 4.0 -> 0.0 seam projected onto a rotated
  cylinder target: (a) after the cut NO facet straddles the seam, (b)
  the cut-edge corners carry exactly 4.0 / 0.0 per side, (c) interior
  tiling-continuation edges (u crossing 1.0, 2.0, ...) trigger NO cuts,
  (d) watertightness - every cut point shared by both adjacent facets,
  no T-junctions); miss fallback (max_distance small -> target values
  retained); normal rejection (two-sided thin plate does not sample the
  far side).
- In-editor: fish body + cylinder proxy, ERHE_SHADER_DEBUG texcoord view
  (fract(v_texcoord_0) - creation 18's debug session) shows a continuous
  cylindrical gradient instead of per-quad moire.

## Open questions

- Attribute enum scope for v1: texcoord_0 alone covers the fish;
  the enum-over-descriptors shape keeps the rest cheap to add.
- `scene_mesh` source-space semantics (local vs world) - resolve when
  wiring the node help text.
- Whether `along_normal` is worth shipping in v1 or added when a real
  case needs it (closest_point + backface rejection covers the fish).
