# erhe_raytrace

## Purpose
Abstraction layer for CPU ray tracing / ray intersection queries. Provides
interfaces for scenes, geometries (triangle meshes), and instances with
swappable backends (BVH, Embree, or null). Used for mouse picking and
other CPU-side ray-object intersection tests.

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
- External: Embree (when `ERHE_RAYTRACE_LIBRARY=embree`), glm

## Notes
- Backend selected at CMake time: `bvh/` (built-in BVH), `embree/` (Intel Embree), `null/` (no-op stubs).
- All interfaces use virtual dispatch with static factory methods.
- The API mirrors Embree's design: set buffers by type/slot, then commit.
- `IGeometry::set_user_data()` / `IInstance::set_user_data()` allow attaching application pointers (e.g., scene Node) for use after intersection.
- When `ERHE_RAYTRACE_LIBRARY=none`, the editor falls back to GPU ID-buffer picking instead.
