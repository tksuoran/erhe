# erhe_geometry

Stability: mostly stable

## Purpose
Polygon mesh geometry library built on Geogram. Provides a `Geometry` class wrapping
`GEO::Mesh` with typed attribute handling, mesh processing (normals, tangents, tex coords),
Conway polyhedron operators, subdivision (Catmull-Clark, sqrt3), CSG (experimental via
Geogram), and primitive shape generators.

## Key Types
- `Geometry` -- Main class wrapping `GEO::Mesh` with named attributes, connectivity queries, processing flags, and AABB computation.
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
