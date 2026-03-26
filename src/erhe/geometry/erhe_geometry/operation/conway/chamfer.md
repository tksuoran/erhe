# Conway Chamfer / Bevel Operation

## What It Does

Chamfer replaces each edge of the source polyhedron with a hexagonal face and
shrinks each original face inward. The result is a polyhedron with:

- One shrunk face per source face (same polygon degree, smaller)
- One hexagonal face per source edge (connecting the two adjacent shrunk faces)
- Vertices at inset corner positions and at inset original-vertex positions

## Design Constraints

- **Topology must be preserved.** The input mesh topology must not be altered.
- **Arbitrary input.** The input mesh is not necessarily regular, convex, or
  planar. The algorithm must handle non-planar faces, irregular vertex
  valences, and mixed dihedral angles.
- **Local computation.** Vertex positions should be determined by geometry in
  the immediate neighborhood of the vertex (adjacent edges and faces).
- **Generalizable to bevel.** The algorithm accepts a `bevel_ratio` parameter
  (0..1) controlling the bevel amount. Conway chamfer is a specific bevel
  amount, not a distinct operation.

## Current Algorithm (chamfer3.cpp)

The active implementation uses centroid-lerp corners with corner-loop ray LS
for inset vertex positioning.

### Step 1: Corner Positions (Centroid Lerp)

Each face vertex is lerped toward the face centroid:
```
corner = pos + bevel_ratio * (centroid - pos)
```
This is a uniform scale toward the centroid - it preserves face shape
exactly and produces E' edges parallel to the original edges E. The
`bevel_ratio` parameter controls how much each face shrinks.

### Step 2: Hex Planes

For each source edge (which becomes a hexagonal face), the best-fit plane
is computed from the 4 known corner vertices using the cross product of the
two diagonals. These planes guide the inset vertex depth positioning.

### Step 3: Inset Vertex Positions (Corner-Loop Ray LS)

Each source vertex V is replaced by an inset vertex V'. The position of V'
is determined by a 1D least-squares fit along a ray.

**Corner loop polygon**: For each edge adjacent to V, the hex quad has an
"open edge" connecting V's two corner positions (one from each face sharing
the edge). These open edges form a closed polygon - the boundary that V'
must connect to.

**Ray definition**: V' is constrained to lie on the ray through the polygon
centroid along the polygon normal:
```
P(t) = centroid + t * normal
```
where:
- `centroid` = average of all corner inset positions at V
- `normal` = average of adjacent source face normals, oriented outward

**LS fit**: The scalar `t` minimizes distance to the hex target planes:
```
minimize Σ(n_i · P(t) + d_i)²
```
This has a closed-form solution:
```
a_i = n_i · centroid + d_i
b_i = n_i · normal
t = -Σ(a_i * b_i) / Σ(b_i²)
```

**Why this works**: The 1D parametrization constrains V' to move only along
the vertex normal through the corner centroid. This prevents lateral drift
(which caused self-intersections with unconstrained 3D LS) while still
finding the optimal depth relative to the hex planes. The corner centroid
is the natural center of the valid space, and the face-normal direction is
the only geometrically meaningful degree of freedom for depth.

### Step 4: Mesh Construction

Creates shrunk face polygons (one per source face), hexagonal faces (one
per source edge), then calls `process()` to build connectivity, edges,
normals, and texture coordinates. Guards against inconsistent edge-facet
connectivity by skipping edges where `find_edge` fails.

### Results (bevel_ratio = 0.25)

| Test | x1 | x3 iter 2 | x3 iter 3 |
|------|-----|-----------|-----------|
| Tetrahedron | pass | plan 0.0002 | plan 0.007 |
| Cube | pass | plan 0.002 | plan 0.003 |
| Octahedron | pass | plan 0.002 | plan 0.003 |
| Dodecahedron | pass | plan 0.002 | plan 0.002 |
| Icosahedron | pass | plan 0.001 | plan 0.001 |

All x1 tests pass (planarity < 1e-4, convex, no self-intersection).
Iterations 2-3 still have non-convexity from increasing edge coplanarity,
but planarity is ~10x better than the old unconstrained 3D LS approach.

### Debug Visualization

On the **source** geometry (visible when hovering the source mesh):
- White lines: E' edges (shrunk face edges)
- Orange lines: hexagon non-E' edges (side edges + opposite E')
- Red `NO_EDGE` markers: missing edges in source connectivity
- `F:C` labels at corners, `V` labels at inset vertices

On the **destination** geometry (visible when hovering hex faces):
- Blue lines: source face edges on shrunk faces
- Green lines: source edge on each hexagon
- Cyan lines: corner-loop polygon edges for each vertex

## Test Coverage

- `test_chamfer_self_intersection.cpp`: single and repeated (3x) chamfer on
  all 5 platonic solids; self-intersection checks; mesh convexity checks;
  quality metrics (QualityRef for chamfer_old, Quality3 for chamfer3)
- `test_chamfer_diagnostics.cpp`: per-iteration face quality metrics
  (convexity, planarity, winding) and edge coplanarity using chamfer_old
- `self_intersection.hpp/cpp`: generic O(n²) triangle-pair self-intersection
  checker

## Edge Coplanarity (Tetrahedron, 3 Iterations)

| Iteration | Max Coplanarity | Edges > 0.90 | Edges > 0.95 |
|-----------|-----------------|--------------|--------------|
| 1         | 0.577           | 0            | 0            |
| 2         | 0.900           | 12           | 0            |
| 3         | 0.999           | 240          | 180          |

This is the fundamental challenge: after repeated chamfer, adjacent faces
become nearly coplanar, making hex plane normals ill-conditioned.

---

## Failed Approaches (Historical)

### chamfer_old.cpp - Edge-Plane LS (Original Reference)

**Approach**: Edge planes (dihedral bisectors) with geometry-derived offsets.
Corner positions via per-face LS line fitting. Inset vertices via per-vertex
LS plane fitting to edge planes.

**Why it failed**: The offset amount is derived from `vertex_min_heights`
(per-vertex minimum of edge midpoint-to-centroid-line distances). After 2
iterations, different vertices have very different min_heights, creating
asymmetric offsets that produce non-convex hexagonal faces. At iteration 3,
48 non-convex faces appear consistently across all platonic solids.

The corner computation method (LS line fitting, face-local cross-product, or
Blender-style offset_meet) made no difference - all produce the same 48
non-convex faces. The root cause is the offset computation, not the corner
or vertex positioning.

Also lacks a user-controllable bevel parameter - the `0.5` factor and
`vertex_min_heights` metric are hardcoded.

**Status**: Renamed to `chamfer_old`, removed from UI, kept in tests as
reference for quality comparison.

### chamfer2.cpp - StraightSkel 3D Straight Skeleton (Removed)

**Approach**: Use the StraightSkel library's `shiftFacets()` to compute
offset polyhedra via 3D straight skeleton, then extract inset vertex
positions from the offset result.

**Why it failed**:
1. `shiftFacets` creates vertices in a different order than the source mesh,
   requiring fragile position-based matching to identify which offset vertex
   corresponds to which source vertex.
2. `shiftFacets` only handles degree-3 vertices. Higher-degree vertices need
   splitting first, but the `shiftFacets` API bypasses the initialization
   that handles this.
3. Planarity was 0.002-0.015 at x1 (worse than the reference) due to offset
   depth mismatch between StraightSkel and the LS corner computation.
4. The library added a significant external dependency for marginal benefit.

**Status**: Removed entirely. StraightSkel CPM dependency removed.

### Unconstrained 3D LS for Inset Vertices (chamfer3, earlier version)

**Approach**: Same as the current chamfer3 Steps 1-2, but Step 3 used full
3D LS fitting (minimize Σ(n_i·x + d_i)² with Tikhonov regularization toward
the original vertex position). No ray constraint.

**Why it failed**: The 3D LS solution has three degrees of freedom. When hex
planes are nearly parallel (coplanar adjacent faces at iterations 2-3), the
LS solution can drift laterally - sliding along the nearly-degenerate
direction. This lateral drift causes V' to intersect adjacent shrunk faces
or hex quads. All x1 tests passed, but iterations 2-3 produced
self-intersections and non-convex hexagons with planarity ~0.005.

**Fix**: Constrain to the 1D ray through the corner centroid along the face
normal (the current approach). This eliminates lateral drift while preserving
the optimal depth placement.

### Constrained LS with Half-Space Clamping (chamfer3, earlier version)

**Approach**: Compute the 3D LS solution, then clamp it into a "valid region"
defined by half-space constraints (source face planes as ceiling, shrunk face
planes and hex quad planes as floor).

**Why it failed**: Two variants were tried:
1. **Corner-centroid lerp (approach A)**: Start from the corner centroid,
   lerp toward V, clamp at half-space boundaries. Failed because the corner
   centroid is not guaranteed to be inside the valid region - it can violate
   hex convexity constraints.
2. **LS + V-to-LS clamping (approach C)**: Clamp along the line from V
   toward the LS solution. The shrunk face constraints were correct 3D
   half-spaces, but the hex convexity constraints (formulated as 3D
   half-spaces from the hex plane cross product) were over-restrictive due to
   hex plane tilt. The clamping prevented reaching the LS solution even when
   it was geometrically valid, producing worse planarity (~0.06) than the
   unconstrained LS.

**Fix**: The ray approach avoids the need for explicit half-space constraints
entirely - the 1D parametrization inherently stays in the valid region.

## Industry Comparison

**Blender's Bevel**:
- Uses angular bisectors for edge offset direction
- LS optimization for width consistency at multi-edge junctions
- Vertex mesh (Catmull-Clark) for complex junctions
- Explicit clamping (Clamp Overlap) to prevent self-intersections
- Bevel V2 adopts weighted straight skeleton for collision handling

**Academic approaches**:
- **Face-offsetting**: fails at concave vertices and coplanar faces
- **Straight skeleton**: handles concave polygons but complex
- **QEM**: equivalent to LS plane fitting, robust but unconstrained
