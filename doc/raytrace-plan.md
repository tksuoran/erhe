# GPU ray tracing plan (issue #233)

Goal: add basic GPU ray tracing capability to the erhe graphics backend. The
API must be implementable on Vulkan and Metal. Initial milestone: render
primary rays with minimal N.V shading into a texture, visible in the editor.

This is distinct from `erhe::raytrace` (CPU-side Embree/bvh abstraction used
for picking); the GPU path lives in `erhe::graphics` and uses hardware
acceleration structures + ray queries.

## Approach: ray query in compute, not a ray tracing pipeline

The initial implementation uses **ray queries in a compute shader**
(`GL_EXT_ray_query` / `VK_KHR_ray_query` on Vulkan; `MTLAccelerationStructure`
+ MSL `raytracing::intersector` on Metal), not a full ray tracing pipeline
(raygen/hit/miss stages + shader binding table). Rationale:

- Ray query maps 1:1 onto both Vulkan and Metal; SBT machinery does not
  (Metal has no SBT; its function tables are structured differently).
- It reuses erhe's existing, production-grade compute + storage-image path
  (see `sky_renderer.cpp` atmosphere LUTs) with zero new pipeline concepts.
- Primary rays + N.V shading need no recursion, so a ray tracing pipeline
  buys nothing here.

A ray tracing pipeline abstraction can be layered on later if needed.

## Phase 1: erhe::graphics foundation (Vulkan + null; gl/metal stubs)

1. **Buffer usage bits** (`enums.hpp`, `enums.cpp`, `vulkan_helpers.cpp`):
   - `Buffer_usage::acceleration_structure_storage` ->
     `VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR`
   - `Buffer_usage::acceleration_structure_build_input` ->
     `VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR`
   - `Buffer_usage::shader_device_address` ->
     `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`
   GL/Metal/null backends ignore these bits.

2. **Vulkan device support** (`vulkan_device.hpp/.cpp`,
   `vulkan_device_init.cpp`):
   - Device extensions: `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
     `VK_KHR_deferred_host_operations` (dependency of acceleration_structure).
   - Feature query/set chain: `VkPhysicalDeviceAccelerationStructureFeaturesKHR`,
     `VkPhysicalDeviceRayQueryFeaturesKHR`,
     `VkPhysicalDeviceBufferDeviceAddressFeatures` (core 1.2; required by
     acceleration structure builds).
   - `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT` when bufferDeviceAddress
     is enabled.
   - `Device_info::use_ray_query` (cross-API flag, default false; true on
     Vulkan when all of accelerationStructure + rayQuery + bufferDeviceAddress
     were enabled). Application code gates the whole feature on this.

3. **glslang target env**: bump `EShTargetVulkan_1_1` -> `EShTargetVulkan_1_3`
   in `glsl_to_spirv.cpp` (GL_EXT_ray_query requires SPIR-V 1.4 which requires
   a Vulkan 1.2+ client; the instance/device are created as 1.3 and the SPIR-V
   target is already 1.6).

4. **`Acceleration_structure`** (new public type, pimpl like Buffer/Texture):
   - `Acceleration_structure_create_info` with
     `Acceleration_structure_type::bottom_level|top_level`.
   - BLAS: fixed list of `Acceleration_structure_triangles` (vertex buffer +
     offset/stride/count, positions are 3 x float32; index buffer +
     offset/count, indices are uint32; `opaque` flag). Geometry is fixed at
     create; `build(Command_buffer&)` records the GPU build.
   - TLAS: `max_instance_count` capacity at create;
     `build(Command_buffer&, std::span<const Acceleration_structure_instance>)`
     writes the instance array (host-visible persistent buffer) and records
     the build. `Acceleration_structure_instance` = transform (glm::mat4) +
     24-bit custom index + 8-bit mask + BLAS pointer.
   - The Vulkan impl owns the AS buffer, scratch buffer and (TLAS) instance
     buffer; `build()` ends with an AS-write -> AS-read/shader-read barrier so
     callers need no explicit sync. Rebuild-in-place of a TLAS that a prior
     in-flight frame is still reading is the caller's problem (use one TLAS
     per frame-in-flight slot, mirroring erhe's ring-buffer convention).
   - Null/gl/metal impls are no-ops (Metal gets a real implementation later;
     the API shape maps to MTLPrimitiveAccelerationStructureDescriptor /
     MTLInstanceAccelerationStructureDescriptor).

5. **Binding**:
   - `Binding_type::acceleration_structure` ->
     `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`; uses the raw binding
     point (no sampler offset), like `storage_image`. The shader declares
     `layout(binding = N) uniform accelerationStructureEXT name;` explicitly
     (no preamble synthesis for now).
   - `Compute_command_encoder::set_acceleration_structure(binding_point, as)`;
     Vulkan implements it as a push-descriptor write with
     `VkWriteDescriptorSetAccelerationStructureKHR` chained; other backends
     no-op.

### Position fetch (added during implementation)

`VK_KHR_ray_tracing_position_fetch` (`Device_info::use_ray_tracing_position_fetch`,
GLSL `GL_EXT_ray_tracing_position_fetch`) is enabled when available: the ray
query compute shader fetches the committed triangle's object-space vertex
positions to compute the geometric normal for N.V shading, avoiding a
per-instance vertex/index SSBO lookup table entirely. Bottom level structures
are built with `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR` when
the feature is on. The editor renderer requires it in addition to
`use_ray_query` (all current desktop ray-query implementations expose it).

## Phase 2: editor renderer (primary rays, N.V shading)

- `Mesh_memory` vertex/index pools gain `acceleration_structure_build_input |
  shader_device_address` usage so BLAS builds can read them in place
  (no data duplication).
- A BLAS cache keyed by the primitive's buffer ranges: BLAS built lazily,
  once per unique `Buffer_mesh` (non-skinned stream 0 = position-only,
  stride 12, offset from `base_vertex`; `triangle_fill_indices` range).
  Skinned meshes are skipped initially (their BLAS would need post-skinning
  positions).
- Per-frame TLAS from visible scene mesh instances (one TLAS per
  frame-in-flight slot; instance capacity grows to a high-water mark on
  scene change, steady-state frames allocate nothing).
- Compute shader (`res/editor/shaders/ray_trace.comp` or similar):
  full-screen primary rays from the active camera (inverse view-projection),
  `rayQueryEXT` closest-hit; on hit fetch the triangle, compute the geometric
  normal from committed barycentrics/positions or via
  `rayQueryGetIntersectionObjectToWorldEXT`, shade `max(dot(N, V), 0)`;
  miss = background color. Write with `imageStore` to an rgba8/rgba16f
  storage texture.
- Integration (as implemented): `editor::Ray_trace_renderer`
  (`src/editor/renderers/ray_trace_renderer.{hpp,cpp}`) is invoked from
  `Viewport_scene_view::execute_rendergraph_node` before the viewport render
  pass opens (same slot as the sky LUT / wide-line compute pre-passes), using
  that viewport's camera. `Ray_trace_window`
  (`src/editor/developer/ray_trace_window.{hpp,cpp}`, developer menu "Ray
  Trace") toggles it and displays the output texture; the renderer no-ops
  while disabled. A dedicated `Texture_rendergraph_node` + composition into
  the viewport remains future work.

## Later / out of scope for the first milestone

- Metal backend implementation of `Acceleration_structure` + intersector.
- Skinned meshes (needs post-skinning position buffers).
- Materials/textures in hits (needs per-instance geometry lookup table:
  buffer device addresses or descriptor-indexed SSBOs keyed by
  `instance_custom_index`).
- TLAS refit (UPDATE mode) instead of full rebuild.
- Ray tracing pipeline / SBT abstraction, denoising, GI.
