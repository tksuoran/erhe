# Handoff: SDF / OpenVDB work - session 2026-08-12

Read this first when continuing SDF work. Companion docs:
- doc/openvdb-integration-plan.md - the phased plan; Phases 1-3 DONE, the
  Phase 3 section records the as-implemented design.
- doc/sdf-mesh-picking-fix-plan.md - resolved picking bug + the
  raycast-vs-closest_point isolation technique.
- src/erhe/voxel/notes.md - erhe_voxel library notes (purpose, API,
  implementation gotchas).

## State: everything below is DONE, verified, committed on main (unpushed)

- ERHE_VOXEL_LIBRARY CMake option (`openvdb`/`none`): oneTBB v2022.3.0
  static + OpenVDB v13.0.0 static core via CPM; find_package(TBB) satisfied
  through a CMAKE_FIND_PACKAGE_REDIRECTS_DIR shim; GEOGRAM_WITH_TBB follows
  the option (Geogram adopts the shared TBB::tbb target). ba13d048.
- src/erhe/voxel: erhe::voxel::Grid pimpl over openvdb::FloatGrid
  (primitives, from_geometry/to_geometry vs erhe::geometry::Geometry, CSG,
  offset, gaussian smooth, sampling, volume/aabb/memory). Tests
  erhe_voxel_tests 8/8. b850bf57 + 22a5732f.
- Geometry graph SDF nodes (sdf pin key 11, orchid): sdf_sphere,
  sdf_capsule, voxelize, sdf_mesh, sdf_boolean (multi-link b pin),
  sdf_offset (clamped to 2x background), sdf_smooth. Factory + palette +
  MCP add_node enum + payload stats {type:"sdf", active_voxel_count,
  voxel_size}. e3064270.
- Picking-after-rebake fix in erhe_scene (generic, not SDF-specific):
  Mesh::update_rt_primitives() now seeds fresh raytrace instances with the
  node's world transform via handle_node_transform_update(). 3b6dd9af.
- USER flipped the vulkan windows configure scripts to
  ERHE_VOXEL_LIBRARY=openvdb themselves (f9d60f0a) - regular builds now
  carry SDF nodes. Existing build dir with everything built:
  build_vs2026_vulkan_openvdb (Release; configured with XR none,
  profile none, tests ON).

## Verification workflow that worked (reuse it)

Launch build_vs2026_vulkan_openvdb editor.exe from repo root, wait for
http://127.0.0.1:8080/health, then JSON-RPC to /mcp:
- create_graph_mesh {name, scene_name:"Default Scene"} auto-targets the
  Geometry Graph window; geometry_graph_add_node / _connect /
  _set_parameter; get_geometry_graph is the evaluation completion barrier
  and reports per-node output payload stats.
- create_node + set_node_transform (both need scene_name) +
  set_node_graph_mesh {node_name, graph_mesh} binds the baked product to a
  scene node.
- raycast probes the raytrace world; geometry_query closest_point probes
  the RENDER mesh - comparing the two at the same spot isolates
  raytrace-vs-render divergence; probing at the origin finds instances
  with unseeded transforms.
- Most tools require explicit scene_name; "Scene not found: " means the
  argument is missing, not that the scene is gone.

## Open follow-ups (deferred, in rough priority order)

1. KNOWN DEBT - cross-build asset compatibility: a graph asset containing
   sdf nodes fails to load WHOLE in an ERHE_VOXEL_LIBRARY=none build
   (read_graph_asset_json drops the entire graph on any unknown node
   type). Needs a placeholder-node or skip-with-warning serialization
   policy; generic problem, sdf just makes it easy to hit.
2. SDF box primitive node (sphere/capsule exist).
3. Narrow band width as a node parameter (fixed 3 voxels everywhere).
4. Resampling to combine grids of mismatched voxel sizes (boolean
   currently passes input a through + shows a warning).
5. Phase 4+ ideas (unplanned, doc/openvdb-integration-plan.md): implicit
   function node (expression/texgen -> SDF), PicoGK-style lattice/beam
   nodes, NanoVDB/PNanoVDB shader-side raymarch preview, .vdb asset
   import/export (revisit USE_BLOSC/USE_ZLIB for DCC compatibility).
6. mesh Boolean_node could adopt the multi-link b-pin pattern too (noted
   during review, out of scope then).

## Gotchas learned this session (violate at your peril)

- Geometry_graph_node::evaluate() runs ONLY on the worker's shadow clone:
  member state written there is invisible on the live node (finish copies
  payloads/preview/products only). Derive imgui() warnings from payloads.
- Payload operator+= only runs on multi-link pins (make_input_pin(...,
  true)); single-link pins replace-on-connect and never accumulate.
- OpenVDB master cannot be add_subdirectory'd (CMAKE_SOURCE_DIR-relative
  paths regression); stay on release tags. v13.0.0 works.
- OpenVDB static on MSVC force-switches consumers to /MT unless
  CMAKE_MSVC_RUNTIME_LIBRARY is set (the option block pins /MD).
- MSVC C4701 from OpenVDB ConvexVoxelizer.h is a codegen-time warning:
  per-TU disable, push/pop around includes does NOT contain it.
- OpenVDB-header consumers need /bigobj on MSVC.
- LevelSetFilter::offset steps at ~half-voxel CFL rate: unclamped offsets
  look like a hang; clamp to a small multiple of the narrow band.
- Voxel test main must initialize Geogram + erhe::geometry log pointers
  (copy of geometry/test/main.cpp) or geometry log calls crash.
- .md docs: ASCII only; no per-machine paths in committed files
  (AGENTS.md rules; both were review findings).
- Grid contract: level-set class, uniform linear transform, values clamped
  to +/- background; is_empty() = no value below 0. Grids on pins are
  immutable (deep-copy before mutating) and never serialized.

## Uncommitted local state (intentional, do not commit blindly)

- config/editor/desktop_windows.json + editor_settings.json: machine-local
  editor session state (window visibility, viewport blocks referencing
  local-only scenes). Review flagged these; left for the user.
