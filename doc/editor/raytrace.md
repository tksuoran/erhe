# GPU ray tracing

Stability: experimental

GPU ray tracing in erhe runs as **ray queries in a compute shader**
(`GL_EXT_ray_query` / `VK_KHR_ray_query` on Vulkan), not as a ray tracing
pipeline with raygen / hit / miss stages and a shader binding table. This is
distinct from `erhe::raytrace`, the CPU-side Embree / bvh abstraction used for
picking: the GPU path lives in `erhe::graphics` and uses hardware acceleration
structures.

Why ray query:

- It maps 1:1 onto both Vulkan and Metal (`MTLAccelerationStructure` + MSL
  `raytracing::intersector`); a shader binding table does not, because Metal's
  function tables are structured differently.
- It reuses erhe's compute + storage-image path (the same machinery the
  atmosphere LUTs use) with no new pipeline concepts.
- The shading the renderer does needs no recursion, so a ray tracing pipeline
  would buy nothing.

A ray tracing pipeline abstraction can be layered on later if it is ever needed.

## erhe::graphics foundation

1. **Buffer usage bits** (`enums.hpp`, `enums.cpp`, `vulkan_helpers.cpp`):
   `Buffer_usage::acceleration_structure_storage`,
   `acceleration_structure_build_input` and `shader_device_address` map to the
   matching `VK_BUFFER_USAGE_*` bits. The GL, Metal and null backends ignore
   them.

2. **Vulkan device support** (`vulkan_device.{hpp,cpp}`,
   `vulkan_device_init.cpp`): the `VK_KHR_acceleration_structure`,
   `VK_KHR_ray_query` and `VK_KHR_deferred_host_operations` device extensions,
   the `VkPhysicalDeviceAccelerationStructureFeaturesKHR`,
   `VkPhysicalDeviceRayQueryFeaturesKHR` and
   `VkPhysicalDeviceBufferDeviceAddressFeatures` chain, and
   `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT` when `bufferDeviceAddress`
   is enabled. `Device_info::use_ray_query` (cross-API, default false) is true
   on Vulkan when accelerationStructure, rayQuery and bufferDeviceAddress were
   all enabled; application code gates the whole feature on it.

3. **glslang target env**: `EShTargetVulkan_1_3` in `glsl_to_spirv.cpp`.
   `GL_EXT_ray_query` requires SPIR-V 1.4, which requires a Vulkan 1.2+ client.

4. **`Acceleration_structure`** (public type, pimpl like `Buffer` and
   `Texture`):
   - `Acceleration_structure_create_info` with
     `Acceleration_structure_type::bottom_level|top_level`.
   - BLAS: a fixed list of `Acceleration_structure_triangles` (vertex buffer +
     offset / stride / count, positions 3 x float32; index buffer + offset /
     count, uint32 indices; `opaque` flag). Geometry is fixed at create;
     `build(Command_buffer&)` records the GPU build.
   - TLAS: `max_instance_count` capacity at create;
     `build(Command_buffer&, std::span<const Acceleration_structure_instance>)`
     writes the instance array (host-visible persistent buffer) and records the
     build. `Acceleration_structure_instance` = transform (`glm::mat4`) +
     24-bit custom index + 8-bit mask + BLAS pointer.
   - The Vulkan implementation owns the acceleration-structure buffer, the
     scratch buffer and (for a TLAS) the instance buffer; `build()` ends with an
     AS-write to AS-read / shader-read barrier, so callers need no explicit
     sync. Rebuilding a TLAS a prior in-flight frame is still reading is the
     caller's problem: use one TLAS per frame-in-flight slot, mirroring erhe's
     ring-buffer convention.
   - The null, GL and Metal implementations are no-ops. The API shape maps onto
     `MTLPrimitiveAccelerationStructureDescriptor` /
     `MTLInstanceAccelerationStructureDescriptor` for a future Metal path.

5. **Binding**: `Binding_type::acceleration_structure` maps to
   `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR` and uses the raw binding
   point (no sampler offset), like `storage_image`. The shader declares
   `layout(binding = N) uniform accelerationStructureEXT name;` explicitly.
   `Compute_command_encoder::set_acceleration_structure(binding_point, as)` is a
   push-descriptor write with `VkWriteDescriptorSetAccelerationStructureKHR`
   chained on Vulkan, and a no-op elsewhere.

6. **Position fetch**: `VK_KHR_ray_tracing_position_fetch`
   (`Device_info::use_ray_tracing_position_fetch`, GLSL
   `GL_EXT_ray_tracing_position_fetch`) is enabled when available, and bottom
   level structures are then built with
   `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR`. It lets a shader
   fetch the committed triangle's object-space vertex positions for a geometric
   normal without a per-instance vertex / index lookup table. The editor
   renderer requires it in addition to `use_ray_query`; all current desktop
   ray-query implementations expose it.

## Editor renderer

- `Mesh_memory` vertex and index pools carry
  `acceleration_structure_build_input | shader_device_address` usage, so BLAS
  builds read them in place with no data duplication.
- `Scene_tlas` (`src/editor/renderers/scene_tlas.{hpp,cpp}`) holds the BLAS
  cache, the per-frame-in-flight TLAS slots and the instance-record SSBO, and is
  shared with `Ddgi_renderer`. A BLAS is built lazily, once per unique
  `Buffer_mesh` (non-skinned stream 0 = position-only, stride 12, offset from
  `base_vertex`; the `triangle_fill_indices` range). Skinned meshes are skipped,
  because their BLAS would need post-skinning positions. The TLAS is built per
  frame from the visible scene mesh instances; instance capacity grows to a
  high-water mark on scene change, so steady-state frames allocate nothing.
- `res/editor/shaders/ray_trace.comp` traces full-screen primary rays from the
  active camera and shades the hits; see
  [`raytrace_materials.md`](raytrace_materials.md) for the material, light and
  glass shading it does.
- `editor::Ray_trace_renderer`
  (`src/editor/renderers/ray_trace_renderer.{hpp,cpp}`) is invoked from
  `Viewport_scene_view::execute_rendergraph_node` before the viewport render
  pass opens (the same slot as the sky LUT and wide-line compute pre-passes),
  using that viewport's camera. `Ray_trace_window`
  (`src/editor/developer/ray_trace_window.{hpp,cpp}`, developer menu "Ray
  Trace") toggles it and displays the fixed 960x540 rgba8 output texture; the
  renderer no-ops while disabled.

## Trap: the BLAS cache key

`Scene_tlas::m_blas_cache` is keyed by a raw
`const erhe::primitive::Buffer_mesh*` and `get_or_create_blas()` returns a cache
hit with no validation. Entries whose primitive nothing else references are
evicted (`render_shape.use_count() == 1`, see
[`reloadable_asset_loads.md`](reloadable_asset_loads.md)), which stops the cache
pinning released content and removes the "dead pointer, address reused by a new
`Buffer_mesh`" flavour of the problem.

What eviction does not cover:
`Primitive_render_shape::commit_geometry_buffer_mesh()` move-assigns the new
`Buffer_mesh` **in place** (`primitive.cpp`). The key address is unchanged and
the live mesh keeps the refcount at 2, so no refcount sweep triggers - while the
vertex and index pool ranges the cached acceleration structure was built over
have been freed and can be recycled by another mesh. The result is a bottom
level structure describing geometry that is no longer there. This is reachable
whenever a deferred import finalize or a geometry edit swaps a primitive's
renderable mesh while ray query is enabled. `Lightmap_baker::m_blas_cache` is
the same cache with the same hazard.

## Future work

- [plans/raytrace.md](../plans/raytrace.md) - BLAS cache identity, Metal backend,
  skinned meshes, TLAS refit, viewport composition.
