# erhe_raytrace

Stability: stable

## Purpose
Abstraction layer for CPU ray tracing / ray intersection queries. Provides
interfaces for scenes, geometries (triangle meshes), and instances with
swappable backends. Used for mouse picking and other CPU-side ray-object
intersection tests.

## Key Types
- `IScene` -- ray tracing scene: attach geometries/instances, commit, intersect rays
- `IGeometry` -- triangle mesh geometry: set vertex/index buffers, enable/disable, user data
- `IInstance` -- instanced reference to an IScene with a transform, mask, and user data
- `Ray` -- ray with origin, direction, t_near/t_far, mask, and flags
- `Hit` -- intersection result: normal, UV, triangle ID, geometry pointer, instance pointer
- `Buffer_type` -- enum for buffer slot types (index, vertex, normal, etc.)
- `Geometry_type` -- enum: triangle, quad, subdivision
- `Prebuilt_bvh` -- an exported / importable BVH with its content hash, triangle count, backend and library tag (`prebuilt_bvh.hpp`)
- `Bvh_node_record` -- one 28-byte node in the `bvh_v2` record layout
- `Tinybvh_node_record` -- one 32-byte node in the `tinybvh` record layout
- `Bvh_source` -- where a committed geometry's BVH came from: `none`, `built`, `prebuilt`

## Public API
```cpp
auto scene = IScene::create_shared("my_scene");
auto geom  = IGeometry::create_shared("mesh", Geometry_type::GEOMETRY_TYPE_TRIANGLE);
geom->set_buffer(Buffer_type::BUFFER_TYPE_VERTEX, 0, format, cpu_buffer, offset, stride, count);
geom->set_buffer(Buffer_type::BUFFER_TYPE_INDEX,  0, format, cpu_buffer, offset, stride, count);
geom->commit();
scene->attach(geom.get());
scene->commit();
Ray ray{...};
Hit hit{};
if (scene->intersect(ray, hit)) { /* hit.geometry, hit.triangle_id, etc. */ }
```

`IGeometry::get_memory_statistics()` reports the CPU bytes the backend holds for a committed geometry (BVH nodes, primitive ids, precomputed triangles, a supplied BVH not yet consumed); the bvh backend fills it, the others report zeros. The vertex / index `Cpu_buffer`s passed to `set_buffer()` are the caller's and are read only by `commit()`.

## Releasing the input buffers

`IGeometry::release_buffers()`, called after `commit()`, makes the geometry
forget the buffers passed to `set_buffer()` and returns true when the backend
does not read them after the commit: `bvh` (traversal and hits use the
precomputed triangles and the BVH) and `tinybvh` (its own triangle array).
`embree` shares the buffers with the device and returns false, as does the
default. After a true return the caller may free the buffers and must not call
`commit()` again. `erhe::primitive::Primitive_raytrace` frees its vertex /
index copies this way. Test: `Geometry.ReleaseBuffersKeepsHitsIdentical`
(`src/erhe/raytrace/test/test_geometry.cpp`).

## Prebuilt BVH

A committed geometry's BVH can be exported and supplied to a later
`commit()` of the same triangles, so the BVH is not built. This is the
runtime half of the Raytrace BVH items (`doc/erhe/gltf.md` "Raytrace BVH
items"). The node records are the backend's own layout, named by
`Prebuilt_bvh::backend`; `primitive_ids[i]` is the core triangle index
(index order) of BVH slot `i` in both:

- `bvh_v2` (the `bvh` backend): `nodes`, `Bvh_node_record`s laid out like
  `bvh::v2::Node<float, 3>` (six floats min x, max x, min y, max y, min z,
  max z, then `index.value` with `prim_count` in bits 0-3 and `first_id` in
  bits 4-31). Every node is in the tree.
- `tinybvh`: `tinybvh_nodes`, `Tinybvh_node_record`s laid out like
  `tinybvh::BVH::BVHNode` (Wald's 32-byte node: `aabb_min` xyz, `left_first`,
  `aabb_max` xyz, `tri_count`; an inner node has `tri_count` 0 and its
  children at `left_first` and `left_first + 1`, a leaf covers slots
  `left_first .. left_first + tri_count - 1`). The pool is exported as tinybvh
  holds it (`usedNodes` records, node 1 unused, holes after a threaded build;
  the default Bonsai builder places children before their parents), with the
  records no inner node reaches zeroed, so equal builds give equal bytes. tinybvh's own `Save()` format is not used: it writes the `BVH`
  object itself, pointers included.

`get_prebuilt_bvh_node_record_size(backend)`, `get_prebuilt_bvh_node_bytes()`
and `set_prebuilt_bvh_node_bytes()` give the records as bytes for the item
payload, so the glTF reader and writer need not know the layouts.

```cpp
// After commit()
Prebuilt_bvh bvh;
if (geometry->export_bvh(bvh)) { /* bvh.triangle_hash, bvh.library, bvh.nodes, ... */ }

// Before commit() of a geometry with the same vertex and index buffers
other->set_prebuilt_bvh(std::move(bvh));
other->commit();
if (other->get_bvh_source() != Bvh_source::prebuilt) {
    log->info("rejected: {}", other->get_prebuilt_bvh_rejection());
}
```

- `get_triangle_hash()` is the FNV-1a hash `commit()` computes over the
  triangle positions in order;
  `get_triangle_count()` is the triangle count of the last commit.
- `get_prebuilt_bvh_backend()` is `bvh_v2` or `tinybvh` and
  `get_prebuilt_bvh_library()` the dependency's pinned `GIT_TAG` (compile
  definition `ERHE_BVH_LIBRARY_TAG` / `ERHE_TINYBVH_LIBRARY_TAG`, set from
  `ERHE_BVH_GIT_TAG` / `ERHE_TINYBVH_GIT_TAG` in the top-level
  `CMakeLists.txt`), the same on every platform; `get_prebuilt_bvh_max_depth()`
  is the backend's traversal stack size (64 for `bvh_v2`, 128 for tinybvh's
  node stack, `TINYBVH_STACK_SIZE`). All are empty or 0 on `embree` and `none`, whose
  `export_bvh()` and `set_prebuilt_bvh()` return false.
- `commit()` takes the supplied BVH for that commit only and checks it with
  `validate_prebuilt_bvh()`: backend and library tag, triangle count and
  hash, node count, child and slot ranges, the tree shape (node 0 has no
  parent, every node it reaches exactly one inner parent, at most `max_depth`
  inner ancestors; `bvh_v2` also every node reachable from node 0, `tinybvh`
  unreachable nodes allowed) and that `primitive_ids` is a permutation. When it passes, the nodes and ids are
  installed and the usual triangle precompute runs; otherwise `commit()`
  logs one warning with the reason, keeps it in
  `get_prebuilt_bvh_rejection()`, and builds one ("BVH sources").
- `hash_triangle_positions()` is one triangle's step of that hash and
  `compute_triangle_hash(indices, positions_xyz)` the hash over an indexed
  triangle list; `commit()` hashes with the former, so a writer that
  reorders triangles (the glTF exporter's prepared core) recomputes the hash
  the loader's `commit()` will compare. `bvh_v2_traversal_stack_size` (64)
  is the `max_depth` for `validate_prebuilt_bvh()`.
- `get_bvh_source()` tells which of `prebuilt` or `built` the last commit
  used. A prebuilt BVH gives the same nodes, ids and hits as the build it was
  taken from (`test/test_prebuilt_bvh.cpp`).

## BVH sources

The `bvh` and `tinybvh` backends' `commit()` takes a prebuilt BVH when one
was supplied and validates, else builds one (logged "BVH build <label> in
<ms> ms" / "tinybvh build <label> in <ms> ms"). Neither reads or writes a
disk cache: the editor stores each BVH it builds as a Raytrace BVH item of
the file's editor asset (doc/editor/prepared_assets.md "The editor's own
loading", R8.4) and supplies it as a prebuilt BVH at the next load;
`cache/bvh` and `cache/tinybvh` directories an earlier build left are not
read. The item's format stamp is the library tag
(doc/editor/prepared_assets.md "Format stamps"), so the items of one backend
are not the other's: a prepared asset made by a `bvh` build and run by a
`tinybvh` build (or the reverse) builds its BVHs in memory.

## Dependencies
- `erhe::dataformat` -- `Format` enum for buffer types
- `erhe::buffer` -- `Cpu_buffer` for geometry data (supports `tail_padding` for Embree 4 alignment)
- External: Embree 4 (when `embree`), tinybvh (when `tinybvh`), madmann91/bvh (when `bvh`), glm

## Backends

Backend selected at CMake time via `ERHE_RAYTRACE_LIBRARY`:

### `bvh` (default) -- madmann91/bvh v2
- Header-only BVH library fetched via CPM (pinned to specific commit)
- Parallel BVH build via `bvh::v2::ThreadPool` + `bvh::v2::ParallelExecutor`
- BVH export / import, see "Prebuilt BVH"
- Manual ray traversal with precomputed triangles
- No scene-level acceleration -- O(N) linear scan of instances per ray
- Multi-level instance nesting supported (recursive traversal)
- Uses `vertex_buffer_info->byte_stride` for vertex position lookups (corrected from earlier bug that used index stride)

### `tinybvh` -- jbikker/tinybvh
- Header-only library (`tiny_bvh.h` and the per-platform headers it includes), fetched via CPM (pinned to commit hash; no git tags)
- `#define TINYBVH_IMPLEMENTATION` in exactly one .cpp file (`tinybvh_geometry.cpp`)
- `tinybvh::BVH::Build()` takes `bvhvec4` triples (3 per triangle, w unused)
- Hit normals computed from stored triangle vertices via cross product (tinybvh does not return normals)
- BVH export / import, see "Prebuilt BVH" (`tinybvh` layout); a commit keeps the `tinybvh::BVH` object (a scene's TLAS names it) and frees its pools first. A prebuilt BVH is installed as `BVH::Load()` installs one (not rebuildable; holes and node order unknown), through `Tinybvh_bvh`, a `tinybvh::BVH` subclass in `tinybvh_geometry.cpp` that reaches the protected build flags
- **TLAS (Top-Level Acceleration Structure)**: enabled by default via `Tinybvh_scene::s_use_tlas`. Uses tinybvh's native `BLASInstance`/`Build`/`Intersect` for O(log N) instance traversal at the scene level. Falls back to linear scan when disabled or for nested hierarchies.
- Direction: tinybvh uses the ray direction as given (`NORMALIZE_RAY_DIR` is not defined; `tinybvh_geometry.cpp` refuses to compile with it), so `t_far` and hit distances are in the caller's direction units on both the BLAS and the TLAS path
- Zero direction components: tinybvh clamps their reciprocal to `BVH_RCP_FAR` (2^100) and evaluates its slab test with an explicit FMA, so axis-aligned rays from origins far from the world origin hit the nodes they pass through (test: `test/test_axis_aligned_ray.cpp`)
- Multi-level instance nesting supported (linear scan path)

### `embree` -- Intel Embree 4.4.0
- Built as static library via CPM from `RenderKit/embree` with ISPC, tutorials, and TBB disabled (`EMBREE_TASKING_SYSTEM INTERNAL`)
- Thin wrapper over Embree's RTCDevice/RTCScene/RTCGeometry API
- Uses `rtcSetSharedGeometryBuffer()` with raw pointer from `Cpu_buffer` (requires 16 bytes tail padding)
- Explicit `RTCFormat` mapping: `format_32_vec3_float` -> `RTC_FORMAT_FLOAT3`, `format_32_vec3_uint` -> `RTC_FORMAT_UINT3`
- Native TLAS: Embree builds scene-level acceleration in `rtcCommitScene()`; O(log N) instance traversal
- `rtcCommitGeometry()` called automatically after `set_mask()`/`enable()`/`disable()` so changes take effect without explicit re-commit
- Default geometry mask set to `0xFFFFFFFF` in constructor (Embree 4 changed default to `0x1`)
- User data stored internally (not delegated to Embree's user data, which is used for backend-internal pointers)

### `none` -- null/stub
- All methods are no-ops; `intersect()` always returns `false`
- Editor falls back to GPU ID-buffer picking

## Scene-Level Acceleration

| Backend | Scene acceleration | Intersect complexity | commit() |
|---------|-------------------|---------------------|----------|
| bvh     | None (linear scan) | O(N) per ray | No-op |
| tinybvh | TLAS (optional, default on) | O(log N) with TLAS | Builds TLAS |
| embree  | Native TLAS | O(log N) | rtcCommitScene() |
| none    | N/A | N/A | No-op |

## Unit Tests

Tests in `test/` using Google Test, run via `scripts/test_raytrace_all_backends.bat`:
- **test_geometry.cpp** -- basic intersection: hit/miss, normal, UV, closest hit, cube
- **test_masking.cpp** -- mask filtering, enable/disable toggle
- **test_instance.cpp** -- identity/translated/scaled transforms, instance mask, multiple instances
- **test_hierarchy.cpp** -- multi-level nesting: nested translation, rotation+translation, scale propagation, three-level nesting
- **test_scene.cpp** -- empty scene, attach/detach geometry and instances
- **test_prebuilt_bvh.cpp** -- BVH export / import round trip (identical nodes, primitive ids and hits over 400 rays through a transformed instance) and rejection of a wrong tag, backend, count or hash, truncated or out-of-range nodes and ids, shared or root-referencing children, unreachable nodes, excess depth and non-permutation ids

Build with `-DERHE_BUILD_TESTS=ON`. Configure headless (`-DERHE_GRAPHICS_API=none -DERHE_WINDOW_LIBRARY=none`) since raytrace has no GPU dependency.

## Notes
- All interfaces use virtual dispatch with static factory methods.
- The API mirrors Embree's design: set buffers by type/slot, then commit.
- `Buffer_type` and `Geometry_type` enum values match Embree's `RTCBufferType` / `RTCGeometryType` numerically.
- `IGeometry::set_user_data()` / `IInstance::set_user_data()` allow attaching application pointers (e.g., scene Node) for use after intersection.
- When `ERHE_RAYTRACE_LIBRARY=none`, the editor falls back to GPU ID-buffer picking instead.
