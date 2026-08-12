# erhe_voxel

## Purpose
Sparse voxel signed distance fields (SDF) built on OpenVDB narrow-band level
sets. Provides SDF primitives, booleans, offset/smooth filtering, and
conversion to/from `erhe::geometry::Geometry`. Foundation for SDF geometry
graph nodes (see doc/openvdb-integration-plan.md).

Only built when `ERHE_VOXEL_LIBRARY=openvdb` (CMake option, default `none`).

## Key Types
- `Grid` -- A narrow-band signed distance field over an `openvdb::FloatGrid`.
  Pimpl (`Grid_impl`) so consumers never include OpenVDB headers. Deep-copy
  copy semantics.
- `Grid_create_info` -- Voxel size (world units) + narrow band half width in
  voxels (default 3). Follows the PicoGK-style level-set contract: uniform
  voxel size, axis-aligned linear transform, values clamped to +/- background.

## Public API
- `Grid::make_sphere(create_info, center, radius)` / `Grid::make_capsule(create_info, p0, p1, r0, r1)` -- SDF primitives.
- `Grid::from_geometry(create_info, geometry)` -- Voxelize a closed mesh (polygon facets are fan-triangulated) via `meshToLevelSet`.
- `grid.to_geometry(destination, adaptivity)` -- Extract the zero isosurface via `volumeToMesh` into quad-dominant facets, wound outward (OpenVDB output winding is reversed).
- `grid.union_with(other)` / `subtract(other)` / `intersect(other)` -- Grid CSG; operand is deep-copied, voxel sizes must match.
- `grid.offset(distance)` -- Positive grows outward (sign flipped from OpenVDB's inward-positive convention).
- `grid.smooth(iterations)` -- Gaussian level-set filter.
- `grid.sample(position)` -- Trilinear world-space signed distance, clamped to +/- background.
- `grid.get_volume()` / `get_aabb()` / `get_active_voxel_count()` / `get_memory_usage()` / `is_empty()`.

## Dependencies
- OpenVDB (static core, PRIVATE; see top-level CMakeLists ERHE_VOXEL_LIBRARY block)
- erhe::geometry (PUBLIC, `Geometry` in conversion API)
- erhe::math (PUBLIC, `Aabb`)

## Implementation Notes
- `openvdb::initialize()` is handled internally (std::call_once) -- callers
  need no OpenVDB setup.
- `is_empty()` treats grids with no value below 0 as empty (0 values are
  left behind by boolean operations; same check PicoGK uses).
- Conversions only touch mesh-local geogram state (create_vertices,
  facets.connect) so they do not need `erhe::geometry::geogram_lock()`.
- MSVC: C4701 is disabled for voxel.cpp (fires inside OpenVDB's
  ConvexVoxelizer.h at template instantiation time) and consumers of the
  OpenVDB headers need `/bigobj`.
- Tests: src/erhe/voxel/test (`erhe_voxel_tests`; includes the Phase 1
  OpenVDB smoke test). Test main must initialize Geogram + geometry logs
  (same as geometry tests).
