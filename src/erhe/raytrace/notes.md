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
- `erhe::buffer` -- `Cpu_buffer` for geometry data (supports `tail_padding` for Embree 4 alignment)
- External: Embree 4 (when `embree`), tinybvh (when `tinybvh`), madmann91/bvh (when `bvh`), glm

## Backends

Backend selected at CMake time via `ERHE_RAYTRACE_LIBRARY`:

### `bvh` (default) -- madmann91/bvh v2
- Header-only BVH library fetched via CPM (pinned to specific commit)
- Parallel BVH build via `bvh::v2::ThreadPool` + `bvh::v2::ParallelExecutor`
- Hash-based BVH disk caching in `cache/bvh/<git-commit>/<hash>`
- Manual ray traversal with precomputed triangles
- No scene-level acceleration -- O(N) linear scan of instances per ray
- Multi-level instance nesting supported (recursive traversal)
- Uses `vertex_buffer_info->byte_stride` for vertex position lookups (corrected from earlier bug that used index stride)

### `tinybvh` -- jbikker/tinybvh
- Single-header library (`tiny_bvh.h`), fetched via CPM (pinned to commit hash; no git tags)
- `#define TINYBVH_IMPLEMENTATION` in exactly one .cpp file (`tinybvh_geometry.cpp`)
- `tinybvh::BVH::Build()` takes `bvhvec4` triples (3 per triangle, w unused)
- Hit normals computed from stored triangle vertices via cross product (tinybvh does not return normals)
- Hash-based BVH disk caching via `tinybvh::BVH::Save()`/`Load()` in `cache/tinybvh/<git-commit>/<hash>`
- **TLAS (Top-Level Acceleration Structure)**: enabled by default via `Tinybvh_scene::s_use_tlas`. Uses tinybvh's native `BLASInstance`/`Build`/`Intersect` for O(log N) instance traversal at the scene level. Falls back to linear scan when disabled or for nested hierarchies.
- Direction normalization: tinybvh normalizes ray direction internally; the backend compensates by scaling t_far by direction length before/after tinybvh calls
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

Build with `-DERHE_BUILD_TESTS=ON`. Configure headless (`-DERHE_GRAPHICS_LIBRARY=none -DERHE_WINDOW_LIBRARY=none`) since raytrace has no GPU dependency.

## Notes
- All interfaces use virtual dispatch with static factory methods.
- The API mirrors Embree's design: set buffers by type/slot, then commit.
- `Buffer_type` and `Geometry_type` enum values match Embree's `RTCBufferType` / `RTCGeometryType` numerically.
- `IGeometry::set_user_data()` / `IInstance::set_user_data()` allow attaching application pointers (e.g., scene Node) for use after intersection.
- When `ERHE_RAYTRACE_LIBRARY=none`, the editor falls back to GPU ID-buffer picking instead.
