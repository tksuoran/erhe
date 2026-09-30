# erhe_geometry

Stability: mostly stable

## Purpose
Polygon mesh geometry library built on Geogram. Provides a `Geometry` class wrapping
`GEO::Mesh` with typed attribute handling, mesh processing (normals, tangents, tex coords),
Conway polyhedron operators, subdivision (Catmull-Clark, sqrt3), CSG (experimental via
Geogram), and primitive shape generators.

## Key Types
- `Geometry` -- Main class wrapping `GEO::Mesh` with named attributes, connectivity queries, processing flags, and AABB computation.
- `Index_lists` -- Compressed index lists keyed by an element index (offsets + one flat index array), built in two passes (count, then add in the same order). `Geometry` keeps its vertex-to-corners, vertex-to-edges and edge-to-facets tables in this form and `get_vertex_corners()` / `get_vertex_edges()` / `get_edge_facets()` return a `std::span<const GEO::index_t>` into them, valid until the next `update_connectivity()` / `build_edges()`. A `std::vector` per vertex made `process()` heap-bound: with many worker threads processing meshes at once (editor brush build, async mesh operations) the per-vertex allocations dominated and contended on the heap.
- `Mesh_attributes` -- Typed wrappers (`Attribute_present<T>`) for all standard vertex/corner/facet attributes (normals, tangents, tex coords, colors, joint data, IDs).
- `Attribute_present<T>` -- Binds a `GEO::Attribute<T>` with a presence flag per element.
- `Attribute_descriptor` -- Describes an attribute's name, transform mode, and interpolation mode.
- `Geometry_operation` -- Base class for operations that transform a source geometry into a destination.
- `Mesh_info` / `Mesh_serials` -- Statistics and change-tracking for mesh data.

## Public API
- `Geometry(name)` then `geometry.process(flags)` -- Create and process a mesh.
- `geometry.get_mesh()` -- Access the underlying `GEO::Mesh`.
- `geometry.get_attributes()` -- Access typed attribute wrappers.
- Shape generators: `shapes::make_box()`, `shapes::make_sphere()`, `shapes::make_capsule()`, `shapes::make_torus()`, `shapes::make_cone()`, `shapes::make_disc()`, `shapes::make_icosahedron()`, etc.
- Conway operators: `ambo`, `chamfer`, `dual`, `gyro`, `join`, `kis`, `meta`, `subdivide`, `truncate`.
- Subdivision: `catmull_clark_subdivision`, `sqrt3_subdivision`.
- CSG: `difference`, `intersection`, `union_` (experimental).
- Utilities: `compute_facet_normals()`, `compute_mesh_tangents()`, `triangulate()`, `normalize()`, `reverse()`, `bake_transform()`.
- Convex hulls: `make_convex_hull(const GEO::Mesh& source, GEO::Mesh& destination)` and `shapes::make_convex_hull(GEO::Mesh& mesh, const std::vector<glm::vec3>& points)` (the latter delegates to the former). Both return `false` for a point set that spans no volume; see the convex hull note below.
- Topology walkers (`erhe_geometry/topology.hpp`): `walk_edge_loop()`, `walk_edge_ring()`, `walk_face_loop()`, `walk_boundary_loop()` and `walk_connected_region()` follow the Blender selection rules stated in `doc/plans/mesh_modeling.md` section 4.1. They require a `Geometry` processed with `process_flag_connect | process_flag_build_edges`, take a seed edge (seed vertices for the connected region), and fill a caller-owned vector (cleared, capacity kept) end to end: edge indices, facet indices for the face loop, vertex indices in flood order for the connected region. A closed loop or ring (`Walk_shape::closed`) lists each element once, starting at the seed. `Edge_loop_delimit` (`crease`, `outer_corners`) and `Region_delimit` (`crease`, `winding`) select where a walk stops; an edge is a crease when its `edge_sharpness` is above 0. An interior seed edge with an end vertex of valence 3 and three facets has a hub when its largest facet has more than four corners: the loop then follows that facet's edges through valence-3 vertices, stopping at a missing, visited or boundary edge (a cap edge of a pentagonal prism loops around the pentagon); without a hub a valence-3 vertex ends the loop. Visited sets live in per-thread scratch, so a steady-state walk allocates nothing.
- `Edit_mesh` (`erhe_geometry/edit_mesh.hpp`) -- A mutable polygon mesh scratch on which modeling operations compose local edits (`doc/plans/mesh_modeling.md` D2). `load()` copies a `Geometry` with edge connectivity: vertices (position, provenance as weighted source vertices), facets (corners with provenance as weighted source corners, one source facet), edges keyed by their canonical vertex pair (facet list, optional sharpness, wire edges allowed) and per-vertex facet and edge lists. Deleted elements are tombstoned, so handles stay valid until emission. The primitives, each keeping the adjacency current: `split_edge(edge, t)` (a new vertex and corners weighted `1 - t` / `t` on the edge's first / second vertex), `split_facet(facet, corner_a, corner_b)`, `split_facet_edgenet(facet, edges)` (chords and chains through interior vertices added with `add_vertex()`; dangling edges are dropped; a floating island is connected to the boundary by two non-crossing edges from its nearest vertex pairs, so no facet repeats a vertex; interior corners take mean value coordinate weights of the facet's corners), `join_facets(region)` / `join_facet_pair(edge)` (an edge-connected region with one manifold boundary loop, else a `Join_result` failure and no change), `collapse_vertex(v)` (two edges joined, sharpness the larger), `weld_vertices(merges)` (the weld core of the plan's section 4.4), `separate_vertex(v, edges)` (rip into one vertex per fan), `delete_elements(elements, Delete_context)` (the five contexts of section 4.3) and `create_facet(vertices, reference_facet)`.
- `operation::Edit_mesh_operation` -- A `Geometry_operation` holding an `Edit_mesh` loaded from its source. A subclass's `build()` runs primitives on `m_edit_mesh` and calls `emit()`, which writes the live elements into the destination (one `create_vertices(n)`, one polygon per facet) and records every vertex, corner and facet provenance, so attributes interpolate from the source and `remap_component_selection()` maps a source vertex to the destination vertex whose provenance is that vertex with weight 1 and a source facet to the destination facets carrying it. The scratch positions are written after interpolation, the destination is processed with `structural_post_process_flags`, then each scratch edge's sharpness is written to its destination edge. A wire edge has no destination edge; a loose vertex is emitted.
- `Geometry::get_corner_edge(corner)` -- The edge of the facet edge starting at `corner` (the corner's vertex to the next corner's vertex), valid until the next `build_edges()`.
- `get_vertex_corners(vertex)` is in fan order: consecutive corners share an edge at the vertex, stepping from a corner to the one across its incoming facet edge. A boundary vertex's corners run from the corner whose outgoing facet edge is a boundary edge to the corner whose incoming facet edge is one.

## Dependencies
- **erhe libraries:** `erhe::math`, `erhe::log`, `erhe::verify`, `erhe::profile`
- **External:** Geogram (core mesh library), glm

## Notes
- All mesh data lives in Geogram's `GEO::Mesh`; erhe wraps it with typed accessors.
- Conversion helpers exist between `glm` and `GEO` vector/matrix types.
- Attribute interpolation during operations uses weighted source tracking per destination element.
- **Hand-building a `GEO::Mesh`** (rather than producing one through the geometry operations) has two requirements that are easy to miss, both of which fail far from the cause:
  - End with `mesh.vertices.set_single_precision()`. The primitive builder reads points through `get_pointf()`, and leaving the mesh in double precision trips a geogram assertion.
  - A hand-built mesh carries NO normal attribute, and the primitive builder writes vertex normals from `facet_normal`. Call `compute_facet_normals()`, or anything shading with `dot(V, N)` renders flat black.
- **Convex hulls need four affinely independent points.** `make_convex_hull()` classifies its input with `erhe::math::classify_affine_span()` before reaching Geogram and returns `false` with a `log_geometry` warning naming the reason (fewer than 4 points / all points coincide / collinear / coplanar). Callers treat `false` as "this geometry has no hull": the collision shape is simply left absent. The guard is what keeps degenerate input out of Geogram's Delaunay, where a volume-less point set is a warning plus a garbage triangulation on the sequential path and an unbounded hang on the parallel one.
- Facet winding is counter-clockwise seen from outside. Check a facet with the cross product rather than trusting a comment: for facet `{a, b, c}`, `(v_b - v_a) x (v_c - v_a)` must point away from the interior. Reversed winding normals a closed shape inward -- it renders inside out *and* the raytrace hit normal comes back negated.
