# Geometry graph: attribute projection node

Status: proposed

This plan extends `doc/editor/geometry_nodes.md`, which describes the geometry node
graph as it is. Motivated by creation 18 (the fish): a geometry-graph node
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

### Value-space semantics for repeating UVs

Source texture coordinates are TILED - values run past 1 for a repeating
pattern - and seams must transfer EXACTLY, not approximately. Never
fract() or wrap anywhere in the transfer. Tiling continuation
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

## Implementation notes

### File map

Core operation (new):

- `src/erhe/geometry/erhe_geometry/operation/project_attribute.{hpp,cpp}`,
  registered in `src/erhe/geometry/CMakeLists.txt` next to `lattice_deform`.

Templates to read before writing:

- `operation/geometry_operation.{hpp,cpp}` - the two-source constructor
  (lhs = target, rhs = source; CSG uses it), `Source_table`,
  `make_edge_midpoints` (uniform-t edge splits with provenance; this node
  needs a per-edge-t variant), `interpolate_mesh_attributes()`,
  `copy_mesh_attributes()`, and the batch element creation notes - create
  destination elements in bulk, see the "No-create variants" comment block
  and `doc/erhe/catmull_clark.md`.
- `operation/lattice_deform.cpp` - a clean operation of similar size.
- `operation/make_atlas.cpp` - the attribute bind and unbind discipline
  around Geogram calls: attributes must be UNBOUND before Geogram mutates or
  copies meshes, and rebound after.
- `erhe_geometry/geometry.hpp` - the `Mesh_attributes` typed accessors
  (`corner_texcoord(i)` and friends), `Attribute_present<T>` (value plus
  present flag), `Attribute_descriptor::Interpolation_mode`.

Graph node (new):

- `src/editor/geometry_graph/nodes/project_attribute_node.{hpp,cpp}`,
  registered in `geometry_graph_node_factory.cpp` (type name
  `project_attribute`), the palette, the editor `CMakeLists.txt` and the
  `geometry_graph_add_node` type enum in `mcp_server_tool_list.cpp` - without
  that last one MCP cannot create the node at all. `boolean_node` is the exact
  two-geometry-input template and `subdivide_node` the parameter and
  serialization template.
- Node UI: attribute combo, method combo, max distance drag, `cut_seams`
  checkbox, projected and missed counts.

Tests (new):

- `src/erhe/geometry/test/test_project_attribute.cpp`, registered in
  `src/erhe/geometry/test/CMakeLists.txt`. The test list is the Verification
  plan above; the tiled-seam one is the point of the feature. Follow how
  `test_lattice_deform.cpp` builds and runs.

### Phases

One commit per phase, built and tested before the commit.

1. **Core operation, no cutting**: chart decomposition (value-space
   continuity), per-chart triangulated copies plus `GEO::MeshFacetsAABB`,
   per-target-facet chart anchoring, corner sampling with facet-local
   barycentrics, `Interpolation_mode`-aware blending, `Attribute_present`-aware
   miss fallback, backface rejection. This completes the `cut_seams = false`
   behavior. Tests: cube projection, miss fallback, normal rejection.
2. **Seam imprinting**: vertex labeling, bisector zero crossings (per edge,
   shared, bisection-refined, epsilon-snapped), facet splits (chord insertion;
   a CDT only if a junction case actually needs one), the per-edge-t edge-split
   provenance variant, then re-sampling the sub-facets. Test: the tiled-UV seam
   imprint (exact 4.0 / 0.0 per side, no cuts on tiling continuation, no
   T-junctions).
3. **Graph node**: pins, parameters, serialization, factory, palette, UI, and
   `evaluate()` calling the operation; a missing source passes the target
   through with a warning.
4. **End-to-end on the fish**: extend `scripts/creations/creation_18_fish.py`
   with a proxy branch - a cylinder-ish mesh with clean cylindrical UVs run
   through the SAME lattice node, then `project_attribute(texcoord_0)` before
   the output. Verify with the texcoord debug view, then bind the scales
   texture graphs.

### Traps

- **`GEO::MeshFacetsAABB` triangulates its input mesh, even through the const
  overload** (`const_cast` inside Geogram). Always build it on a triangulated
  COPY carrying an `orig_facet` facet attribute. Geogram's fan triangulation
  preserves vertex ids, so a hit triangle maps back to the original facet and
  its three original vertices, which vertex-match to that facet's corners.
- Every node parameter must appear in `write_parameters` / `read_parameters`,
  because that is the path `geometry_graph_set_parameter` takes; a parameter
  outside it cannot be driven from MCP.
- `get_geometry_graph` is the MCP evaluation completion barrier. Graph meshes
  evaluate asynchronously on shadow clones, so `evaluate()` must never touch
  live scene state.
- Attribute-channel traps that have bitten this area before: `build_edges()`
  wipes edge-domain values unless they are snapshotted
  (`doc/erhe/subdivision_crease_edges.md`), and `transform_mesh` transforms a
  hardcoded channel list. Check both when a channel goes missing.
- **Texcoord debug view**: `res/shaders/standard.frag` `ERHE_SHADER_DEBUG == 7`
  visualizes `fract(v_texcoord_0)`; set `"shader_debug": 7` in the active
  graphics preset. Success on the fish is a continuous cylindrical gradient,
  failure is per-quad moire. Meshes with no texcoords at all (the CSG and sweep
  outputs - tail fin, sweep fins, eyes) render black there.
- Screenshot iteration: `edge_lines: true` in
  `config/editor/default_viewport_config.json` makes a dense mesh read black.
  It is read at every viewport construction, so it can be toggled without a
  restart - back it up and restore it.

### Done when

- All four test groups are green and the Vulkan and OpenGL editor targets
  build.
- The fish body shows a continuous texcoord gradient under
  `ERHE_SHADER_DEBUG 7` with the proxy-projection graph, the seams are cut
  exactly (inspect the seam line under the belly), and the scales albedo and
  normal graphs bind and render.
- The open questions above are resolved and this plan is folded into
  `doc/editor/geometry_nodes.md`.
