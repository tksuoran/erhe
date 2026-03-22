# erhe_geometry

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
- Shape generators: `shapes::make_box()`, `shapes::make_sphere()`, `shapes::make_torus()`, `shapes::make_cone()`, `shapes::make_disc()`, `shapes::make_icosahedron()`, etc.
- Conway operators: `ambo`, `chamfer`, `dual`, `gyro`, `join`, `kis`, `meta`, `subdivide`, `truncate`.
- Subdivision: `catmull_clark_subdivision`, `sqrt3_subdivision`.
- CSG: `difference`, `intersection`, `union_` (experimental).
- Utilities: `compute_facet_normals()`, `compute_mesh_tangents()`, `triangulate()`, `normalize()`, `reverse()`, `bake_transform()`.

## Dependencies
- **erhe libraries:** `erhe::math`, `erhe::log`, `erhe::verify`, `erhe::profile`
- **External:** Geogram (core mesh library), glm

## Notes
- All mesh data lives in Geogram's `GEO::Mesh`; erhe wraps it with typed accessors.
- Conversion helpers exist between `glm` and `GEO` vector/matrix types.
- Attribute interpolation during operations uses weighted source tracking per destination element.
