# SDF / OpenVDB: remaining work

Status: in progress

This plan extends `doc/erhe_voxel.md` (the `erhe::voxel` library) and the SDF
node section of `doc/geometry_nodes.md`. SDF support is built on OpenVDB behind
the `ERHE_VOXEL_LIBRARY` CMake option (`openvdb` or `none`): oneTBB and the
OpenVDB static core come in through CPM, `erhe::voxel` wraps them, and the
geometry graph has SDF primitive, voxelize, mesh, boolean, offset and smooth
nodes. What is left is below.

## Known debt: cross-build asset compatibility

A graph asset containing SDF nodes fails to load in an
`ERHE_VOXEL_LIBRARY=none` build, and `read_graph_asset_json` drops the WHOLE
graph on any unknown node type rather than the offending nodes. Graph
serialization needs a placeholder-node or skip-with-warning policy. The problem
is generic - an asset from a newer editor carrying node types an older build
lacks hits it the same way - and SDF just makes it easy to reach.

## Node gaps

- An SDF box primitive node (sphere and capsule exist).
- Narrow band width as a node parameter; it is fixed at 3 voxels everywhere.
- Resampling, to combine grids of mismatched voxel sizes. `sdf_boolean`
  currently passes input a through and warns.
- The mesh `Boolean_node` could adopt the multi-link b-pin pattern that
  `sdf_boolean` uses.

## Later ideas

- An implicit-function node: an expression or texgen-driven SDF rendered into
  a grid, as PicoGK's `RenderImplicit` does.
- Lattice and beam nodes - PicoGK-style engineering lattices; its round and
  flat-cone SDFs are the recipe.
- GPU: enable NanoVDB (`OPENVDB_BUILD_NANOVDB=ON`; it lives in the same
  repository, is header-only and costs no extra dependency) and sample it
  shader-side through `PNanoVDB.h` for a raymarched SDF preview before
  meshing.
- `.vdb` asset import and export, which means revisiting `USE_BLOSC` and
  `USE_ZLIB` for file compatibility with DCC tools.
- Wider TBB adoption where other dependencies benefit. Geogram already adopts
  the shared `TBB::tbb` target through `GEOGRAM_WITH_TBB`, which follows the
  voxel option; promote it to an independent `ERHE_USE_TBB` option when
  something needs TBB without voxels.

## Build traps to keep in mind

These cost real time when the dependency is touched again:

- **OpenVDB master cannot be `add_subdirectory`'d**: it regressed to
  `CMAKE_SOURCE_DIR`-relative CMake paths. Stay on release tags.
- **OpenVDB static on MSVC force-switches consumers to `/MT`** unless
  `CMAKE_MSVC_RUNTIME_LIBRARY` is set; the option block pins `/MD`.
- **Upstream OpenVDB calls `find_package(TBB REQUIRED)`** rather than checking
  for an existing target, which is why the build supplies a
  `CMAKE_FIND_PACKAGE_REDIRECTS_DIR` shim that asserts the CPM-provided target
  exists.
- The Boost dependency is only inside the `OPENVDB_USE_DELAYED_LOADING` guard,
  Blosc and ZLib only affect `.vdb` compression, and `USE_IMATH_HALF=OFF` uses
  the internal half type - so the minimal core needs TBB and nothing else.
