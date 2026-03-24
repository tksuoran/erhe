# Plan: Add tinybvh Backend + Resurrect Embree 4 Backend

## Status: Implementation complete, not yet build-tested

## What was done

### Task 1: Embree 4 backend (resurrected)

All changes in `erhe_raytrace/embree/`:

- **Root `CMakeLists.txt`**: Updated CPM from Embree 3.13.3 to 4.4.0, repo changed to `RenderKit/embree`, added build options (`EMBREE_ISPC_SUPPORT OFF`, `EMBREE_TUTORIALS OFF`, `EMBREE_TESTING_INTENSITY 0`)
- **Deleted `embree_buffer.hpp` / `embree_buffer.cpp`**: No longer needed. `rtcSetSharedGeometryBuffer()` with raw `Cpu_buffer` pointer replaces Embree-managed `RTCBuffer`.
- **`embree_device.hpp`/`.cpp`**: `embree3` -> `embree4` include, `log.hpp` -> `raytrace_log.hpp`
- **`embree_scene.hpp`/`.cpp`**: `embree4` include, `intersect()` returns `bool`, removed `RTCIntersectContext`/`rtcInitIntersectContext()` (Embree 4 removed these), now uses `rtcIntersect1(scene, &rayhit, nullptr)`, fixed `reinterpret_cast` -> `static_cast`
- **`embree_geometry.hpp`/`.cpp`**: `embree4` include, `set_buffer` takes `Cpu_buffer*` (was `IBuffer*`), `set_user_data`/`get_user_data` const-correctness fixed, added `to_rtc_format()` mapping function, fixed `disable()` bug (`m_enabled` was set to `true` instead of `false`)
- **`embree_instance.cpp`**: `log.hpp` -> `raytrace_log.hpp`
- **`src/erhe/raytrace/CMakeLists.txt`**: Removed embree_buffer entries

### Task 2: tinybvh backend (new)

New files in `erhe_raytrace/tinybvh/`:

- **Root `CMakeLists.txt`**: Added `tinybvh` to `ERHE_RAYTRACE_LIBRARY` options, CPM fetches `jbikker/tinybvh` by commit hash (no git tags exist), `DOWNLOAD_ONLY` + INTERFACE library, git commit tracking for cache versioning
- **`tinybvh_scene.hpp`/`.cpp`**: Manages geometry/instance vectors, iterates for intersection, `intersect_instance()` for two-level dispatch
- **`tinybvh_instance.hpp`/`.cpp`**: Stores transform/mask/enabled/user_data, transforms ray to local space for child scene intersection
- **`tinybvh_geometry.hpp`/`.cpp`**: `#define TINYBVH_IMPLEMENTATION` (once), extracts triangles from `Cpu_buffer`, packs into `bvhvec4` triples, builds BVH via `tinybvh::BVH::Build()`, hash-based disk caching via `Save()`/`Load()`, computes hit normals from triangle vertices via cross product
- **`src/erhe/raytrace/CMakeLists.txt`**: Added tinybvh source list

### Documentation
- `CLAUDE.md`: Added `tinybvh` to CMake options table
- `notes.md`: Expanded with per-backend descriptions

## Interface Compatibility Evaluation (Embree 4)

The erhe raytrace interface is fully compatible with Embree 4. All mismatches were in the backend implementation, not the interface contract. No interface changes were needed.

**Compatible (unchanged in Embree 4):**
- `Buffer_type` / `Geometry_type` enum values match numerically
- Scene attach/detach/commit model
- Instance model (`RTC_GEOMETRY_TYPE_INSTANCE`, transforms)
- Ray/Hit structure fields align with `RTCRayHit`
- Mask-based filtering (now enabled by default in Embree 4)

**Fixed in backend:**
- `intersect()` return type: `void` -> `bool`
- `set_buffer` parameter: `IBuffer*` -> `Cpu_buffer*` (using `rtcSetSharedGeometryBuffer`)
- `set_user_data`/`get_user_data` const-correctness
- Deleted include paths (`ibuffer.hpp`, `log.hpp`)
- `RTCFormat` mapping (erhe `Format` values don't match Embree numerically)

## Remaining work

### Build testing
1. Configure with `cmake -DERHE_RAYTRACE_LIBRARY=embree ...` and build
2. Configure with `cmake -DERHE_RAYTRACE_LIBRARY=tinybvh ...` and build
3. Verify `bvh` and `none` backends still compile

### Runtime testing
1. Run editor with each backend, verify mouse picking works
2. Verify ray mask filtering (tools vs content objects)
3. Compare tinybvh picking accuracy with bvh backend

### Known risks
1. **Embree 4 via CPM**: Large library with TBB dependency. May need extra CMake options or fallback to `find_package`.
2. **tinybvh winding order**: Normal computation via cross product may need winding adjustment if picking normals appear flipped.
3. **bvh backend vertex stride bug**: `bvh_geometry.cpp` uses `index_buffer_info->byte_stride` for vertex lookups (should be `vertex_buffer_info->byte_stride`). Works in practice because strides happen to match, but should be fixed separately.
