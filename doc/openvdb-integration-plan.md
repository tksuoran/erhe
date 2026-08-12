# OpenVDB integration plan

Goal: SDF (signed distance field) support in erhe geometry-graph nodes -- SDF
primitives, voxel booleans/offsets/smoothing, mesh<->SDF conversion -- built on
OpenVDB, introduced incrementally behind a CMake option.

Decision (2026-08-12): **full OpenVDB core from Phase 1** ("Route A").
Every dependency we will need later is included from the start -- notably
TBB, which is mandatory for OpenVDB core. No NanoVDB-only interim step.
Once TBB is in the dependency set, it becomes a shared erhe facility usable
by other libraries that support it (Geogram has `GEOGRAM_WITH_TBB`).

Status: **Phase 1 DONE 2026-08-12** (commits ba13d048 + 56571fbb: option +
oneTBB v2022.3.0 + OpenVDB v13.0.0 static core + find_package(TBB)
redirection + GEOGRAM_WITH_TBB + smoke gtest, 2/2 pass in
build_vs2026_vulkan_openvdb Release; default configs verified untouched).
Note learned in Phase 1: OpenVDB *master* has regressed to
CMAKE_SOURCE_DIR-relative CMake paths and cannot be add_subdirectory'd --
stay on release tags (v13.0.0). **Phase 2 DONE 2026-08-12** (commit
b850bf57: erhe_voxel wrapper library with pimpl Grid type, mesh<->SDF
conversions vs erhe::geometry::Geometry, primitives/CSG/offset/smooth,
8/8 tests pass; the optional MCP-op stretch goal was not done -- editor
linkage comes with Phase 3). Next: Phase 3 (SDF in geometry-graph nodes),
to be planned in detail.

## Background / findings (2026-08-12)

- Reference material (clone locations are machine-local; see
  memory-bank/local): the upstream openvdb repository, and LEAP71's
  PicoGKRuntime (a ~2.5k-line C++ layer over OpenVDB; useful as a *recipe
  book* for level-set conventions -- GRID_LEVEL_SET class, narrow band 3
  voxels, uniform linear transform -- not as a dependency).
- **Minimal OpenVDB core dependency set** (verified against upstream CMake):
  - **TBB -- mandatory** (`find_package(TBB REQUIRED)` in
    `openvdb/openvdb/CMakeLists.txt`).
  - **Boost -- NOT needed** when `OPENVDB_USE_DELAYED_LOADING=OFF`
    (the `Boost::iostreams` requirement is inside that guard).
  - **Blosc/ZLib -- optional** (`USE_BLOSC=OFF USE_ZLIB=OFF`): only affects
    `.vdb` file compression; files written uncompressed remain valid.
  - Imath half -- optional (`USE_IMATH_HALF=OFF` uses internal half type).
- Geogram (already an erhe dependency via CPM, tksuoran fork) has
  `GEOGRAM_WITH_TBB`, and its `cmake/onetbb.cmake` begins with
  `if(TARGET TBB::tbb)` -- it adopts an externally provided TBB target
  instead of fetching its own. So one CPM-provided `TBB::tbb` can serve
  both OpenVDB and Geogram.
- NanoVDB lives in the same repo (`nanovdb/nanovdb`, header-only) and can be
  enabled later at zero dependency cost (`OPENVDB_BUILD_NANOVDB=ON`) -- of
  interest for GPU-side SDF sampling (PNanoVDB.h in shaders), not needed
  for the CPU pipeline.
- Recast (2.5D span heightfields) and Geogram (has signed-distance
  ingredients via `MeshFacetsAABB::squared_distance`/`contains`, and exact
  mesh-domain CSG, but no isosurface extraction) were evaluated and do not
  replace OpenVDB's `meshToLevelSet`/`volumeToMesh`.
- erhe dependency pattern: CPM (`cmake/CPM.cmake`, cache in `.cpm_cache`),
  `set_option(ERHE_*_LIBRARY ...)` selector strings, one `CPMAddPackage` per
  dependency in the top-level `CMakeLists.txt`, thin `erhe_*` wrapper libs
  under `src/erhe/`.
- License: OpenVDB is Apache-2.0; oneTBB is Apache-2.0 -- fine for erhe.

## Phase 1 -- CMake option: OpenVDB core + TBB, full final dependency set

Deliverable: `ERHE_VOXEL_LIBRARY` option; when `openvdb`, static OpenVDB
core + TBB build as part of erhe, and a smoke gtest proves the core tools
work in our toolchains. No editor code touched. Default `none` = zero
impact on existing builds.

1. Option next to the other selectors in `CMakeLists.txt`:
   `set_option(ERHE_VOXEL_LIBRARY "Voxel/SDF library. Either openvdb or none" "none" "openvdb;none")`
2. **TBB via CPM** (oneTBB, pinned release tag), added *before* OpenVDB:
   `TBB_TEST=OFF TBB_EXAMPLES=OFF TBB_STRICT=OFF`, static preferred
   (mirrors Geogram's own `onetbb.cmake` settings). This defines
   `TBB::tbb` for the whole build.
   - Integration wrinkle to solve here: upstream OpenVDB calls
     `find_package(TBB REQUIRED)` rather than checking for an existing
     target. Candidate fixes, in preference order:
     (a) CMake >=3.24 `FetchContent` find_package redirection
         (`OVERRIDE_FIND_PACKAGE` / `CMAKE_FIND_PACKAGE_REDIRECTS_DIR`
         with a tiny `TBBConfig` shim that just asserts the target exists);
     (b) point `TBB_DIR` at oneTBB's build-tree export;
     (c) last resort: small patch in a fork (we already maintain forks for
         SDL/geogram, but prefer not to fork openvdb).
3. **OpenVDB core via CPM**, pinned to a release tag (v12.x), built via
   upstream CMake (`add_subdirectory` through CPM `OPTIONS`):
   - `OPENVDB_BUILD_CORE=ON`, `OPENVDB_CORE_SHARED=OFF`,
     `OPENVDB_CORE_STATIC=ON` (static lib; consumers need
     `OPENVDB_STATICLIB` define -- upstream target usage requirements
     should handle this, verify on MSVC),
   - `OPENVDB_BUILD_BINARIES=OFF`, `OPENVDB_BUILD_UNITTESTS=OFF`,
   - `OPENVDB_USE_DELAYED_LOADING=OFF` (drops Boost),
   - `USE_BLOSC=OFF USE_ZLIB=OFF USE_IMATH_HALF=OFF`,
   - `OPENVDB_BUILD_NANOVDB=OFF` for now (flip on later for GPU work),
   - `OPENVDB_ENABLE_RPATH=OFF`, `USE_CCACHE=OFF` (erhe manages these).
4. **Share TBB with Geogram**: with the `TBB::tbb` target present, set
   `GEOGRAM_WITH_TBB=ON` for the geogram CPM package when
   `ERHE_VOXEL_LIBRARY=openvdb`. (If we later want TBB independent of
   voxels, promote to an `ERHE_USE_TBB` option that `openvdb` force-enables;
   don't build that generality until something needs it.)
5. Erhe-side compile definition following the existing per-library define
   pattern (e.g. `ERHE_VOXEL_LIBRARY_OPENVDB`).
6. Smoke gtest under the `ERHE_BUILD_TESTS` umbrella, compiled only when
   the option is on:
   - `openvdb::initialize()`,
   - `tools::createLevelSetSphere`, sample known distances
     (center clamped to background, surface ~ 0, outside positive),
   - one `csgUnion` of two spheres + `volumeToMesh` producing a nonzero
     triangle count -- proves the exact tool set the end goal needs, and
     exercises TBB's thread pool.
7. Build verification per repo convention: `build_vs2026_vulkan`-style
   configure with the option ON, plus one existing configuration with it
   OFF to prove zero impact. MSVC first, then ninja/clang. Watch: OpenVDB
   header-heaviness (`/bigobj` may be needed on MSVC for the smoke TU),
   warnings suppressed via `SYSTEM` includes. Android/Quest build cost is
   accepted later -- the option stays `none` there until needed.

Commits: (1) TBB + find_package redirection plumbing, (2) OpenVDB package +
option + defines, (3) smoke test, (4) `GEOGRAM_WITH_TBB` enablement --
each built and tested before commit (split-commit convention).

## Phase 2 -- do something minimal but real with it

Deliverable: a small `erhe_voxel` static library (`src/erhe/voxel/`)
wrapping OpenVDB behind erhe-style types, still no geometry-graph coupling.
Detail to be firmed up when Phase 1 lands; intended scope:

- `erhe::voxel::Grid` value type: owns a `FloatGrid::Ptr`, enforces the
  PicoGK-style contract (level-set class, uniform linear transform,
  configurable narrow band, default 3), exposes voxel size, bbox, memory
  usage, sampling.
- Conversions using erhe's own geometry types:
  mesh->SDF (`tools::meshToLevelSet`) and SDF->mesh (`tools::volumeToMesh`
  + quad split), converting to/from `erhe::geometry::Geometry`.
- Level-set primitives (sphere, tapered capsule via `LevelSetTubes.h`),
  grid CSG (union/subtract/intersect), `LevelSetFilter` offset/smooth --
  each a thin wrapper, mirroring `PicoGKVdbVoxels.h` recipes.
- gtests: mesh round-trip (voxelize a box, mesh it back, sane bbox/volume),
  CSG identities, offset grows/shrinks measured volume
  (`tools::levelSetVolume`).
- Optional stretch (proves editor linkage, not required): an MCP debug op
  that voxelizes the selected mesh and re-meshes it into the scene --
  MCP-verifiable end to end.

## Phase 3 -- SDF in geometry-graph nodes

Design (2026-08-12, as implemented):

- Payload: `Geometry_pin_key::sdf` (= 11) + variant alternative
  `std::shared_ptr<erhe::voxel::Grid>` in `Geometry_payload`. The header is
  unconditional (`Grid` is forward-declarable thanks to the pimpl); only the
  accumulate path in geometry_payload.cpp is `#if ERHE_VOXEL_LIBRARY_OPENVDB`
  guarded. Multi-link accumulation on sdf pins = union into a new grid
  (mismatched voxel sizes keep the first value). Grids on pins are immutable
  by convention (operation nodes deep-copy), and are never serialized --
  always re-evaluated from parameters.
- Nodes (`geometry_graph/nodes/sdf_nodes.{hpp,cpp}`, compiled only when the
  option is on; factory names in parens): SDF Sphere (`sdf_sphere`),
  SDF Capsule (`sdf_capsule`), Voxelize (`voxelize`, geometry -> sdf),
  SDF Mesh (`sdf_mesh`, sdf -> geometry via volumeToMesh + adaptivity,
  runs process_for_graph), SDF Boolean (`sdf_boolean`,
  union/intersection/difference; voxel-size mismatch passes input a through
  and shows a warning), SDF Offset (`sdf_offset`), SDF Smooth (`sdf_smooth`).
  Creator nodes own a voxel-size parameter (`Sdf_create_parameters`);
  operation nodes inherit the resolution of their input grid.
- Registration: factory + "SDF" palette category + orchid pin color, all
  `#if`-guarded; editor CMakeLists adds the sources and links erhe::voxel
  only when `ERHE_VOXEL_LIBRARY=openvdb`.
- Threading: grids flow only through payloads, so the shadow-clone snapshot
  model needs no extra hooks; OpenVDB ops are safe on the evaluation worker.

Deferred within Phase 3:

- SDF box primitive node (sphere/capsule landed first).
- Narrow band width as a node parameter (fixed at 3 voxels for now).
- Resampling to combine grids of mismatched voxel sizes.
- KNOWN DEBT -- cross-build asset compatibility: a graph asset containing
  sdf nodes fails to load in an `ERHE_VOXEL_LIBRARY=none` build, and the
  unknown-node-type policy in read_graph_asset_json drops the WHOLE graph
  (not just the sdf nodes). Needs a placeholder-node or skip-with-warning
  policy in graph serialization (the same issue exists generically for
  assets from newer editors carrying node types older builds lack).

## Phase 4+ -- later ideas (unplanned)

- Implicit-function node (expression/texgen-driven SDF into a grid,
  cf. PicoGK `RenderImplicit`).
- Lattice/beam nodes (PicoGK-style engineering lattices; port its
  round/flat-cone SDFs).
- Wider TBB adoption in erhe where Geogram/other deps benefit; revisit
  `ERHE_USE_TBB` as an independent option.
- GPU: enable NanoVDB (`OPENVDB_BUILD_NANOVDB=ON`), PNanoVDB.h shader-side
  sampling for raymarched SDF preview before meshing.
- `.vdb` asset import/export (revisit `USE_BLOSC`/`USE_ZLIB` for
  compressed-file compatibility with DCC tools).
