# erhe_raytrace

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

## Dependencies
- `erhe::dataformat` -- `Format` enum for buffer types
- `erhe::buffer` -- `Cpu_buffer` for geometry data
- External: Embree 4 (when `embree`), tinybvh (when `tinybvh`), madmann91/bvh (when `bvh`), glm

## Backends

Backend selected at CMake time via `ERHE_RAYTRACE_LIBRARY`:

### `bvh` (default) -- madmann91/bvh v2
- Header-only BVH library fetched via CPM (pinned to specific commit)
- Parallel BVH build via `bvh::v2::ThreadPool` + `bvh::v2::ParallelExecutor`
- Hash-based BVH disk caching in `cache/bvh/<git-commit>/<hash>`
- Manual ray traversal with precomputed triangles
- Instance support: ray transformed to local space, child scene intersected

### `tinybvh` -- jbikker/tinybvh
- Single-header library (`tiny_bvh.h`), fetched via CPM (pinned to commit hash; no git tags)
- `#define TINYBVH_IMPLEMENTATION` in exactly one .cpp file (`tinybvh_geometry.cpp`)
- `tinybvh::BVH::Build()` takes `bvhvec4` triples (3 per triangle, w unused)
- `tinybvh::BVH::Intersect()` populates `ray.hit.t`, `ray.hit.prim`, `ray.hit.u`, `ray.hit.v`
- Hit normals computed from stored triangle vertices via cross product (tinybvh does not return normals)
- Hash-based BVH disk caching via `tinybvh::BVH::Save()`/`Load()` in `cache/tinybvh/<git-commit>/<hash>`
- Instance support: same manual ray transform pattern as bvh backend

### `embree` -- Intel Embree 4.4.0
- Fetched via CPM from `RenderKit/embree` with ISPC and tutorials disabled
- Thin wrapper over Embree's RTCDevice/RTCScene/RTCGeometry API
- Uses `rtcSetSharedGeometryBuffer()` with raw pointer from `Cpu_buffer` (no Embree-managed buffers)
- Explicit `RTCFormat` mapping: `format_32_vec3_float` -> `RTC_FORMAT_FLOAT3`, `format_32_vec3_uint` -> `RTC_FORMAT_UINT3`
- Embree handles BVH build and traversal internally; `rtcIntersect1()` for single-ray queries
- Native instance support via `RTC_GEOMETRY_TYPE_INSTANCE` + `rtcSetGeometryInstancedScene()`
- User data stored internally (not delegated to Embree's user data, which is used for backend-internal pointers)

### `none` -- null/stub
- All methods are no-ops; `intersect()` always returns `false`
- Editor falls back to GPU ID-buffer picking

## Notes
- All interfaces use virtual dispatch with static factory methods.
- The API mirrors Embree's design: set buffers by type/slot, then commit.
- `Buffer_type` and `Geometry_type` enum values match Embree's `RTCBufferType` / `RTCGeometryType` numerically.
- `IGeometry::set_user_data()` / `IInstance::set_user_data()` allow attaching application pointers (e.g., scene Node) for use after intersection.
- Known issue in bvh backend: `bvh_geometry.cpp` uses `index_buffer_info->byte_stride` for vertex position lookups instead of `vertex_buffer_info->byte_stride`. The tinybvh backend corrects this.
