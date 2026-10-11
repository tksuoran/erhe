# GPU ray tracing

Stability: experimental

GPU ray tracing in erhe runs as **ray queries in a compute shader**
(`GL_EXT_ray_query` / `VK_KHR_ray_query` on Vulkan, MSL
`raytracing::intersection_query` lowered by SPIRV-Cross on Metal), not as a ray tracing
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
   all enabled, and on Metal when `MTLDevice::supportsRaytracing()` is true
   and Xcode's GPU frame-capture layer is not loaded (that layer crashes every
   acceleration structure command encoder, so
   `Device_info::ray_query_disabled_by_capture_layer` reports the downgrade and
   the Ray Trace window tells the user to disable GPU Frame Capture). Application code
   gates the whole feature on `use_ray_query`.

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
   - The Metal implementation (`metal/metal_acceleration_structure.{hpp,cpp}`)
     builds a bottom level structure from
     `MTL::PrimitiveAccelerationStructureDescriptor` (one triangle geometry
     descriptor per `Acceleration_structure_triangles`, a packed 4x3 transform
     buffer when a geometry carries a non-identity transform) and a top level
     structure from `MTL::InstanceAccelerationStructureDescriptor` with
     user-ID instance descriptors, so the custom index reaches the shader as
     the committed user instance ID. Instances set counter-clockwise front
     facing winding to keep the Vulkan / GLSL culling semantics. `build()`
     records into an `MTL::AccelerationStructureCommandEncoder` on the
     caller's command buffer, ordered against the buffer's other encoders by
     the inter-encoder fence, which stands in for the Vulkan build barrier.
     Releases are deferred to frame completion, as on Vulkan.
   - The null and GL implementations are no-ops.

5. **Binding**: `Binding_type::acceleration_structure` maps to
   `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR` and uses the raw binding
   point (no sampler offset), like `storage_image`. The shader declares
   `layout(binding = N) uniform accelerationStructureEXT name;` explicitly.
   `Compute_command_encoder::set_acceleration_structure(binding_point, as)` is a
   push-descriptor write with `VkWriteDescriptorSetAccelerationStructureKHR`
   chained on Vulkan. On Metal the shader compiler remaps every acceleration
   structure resource to the reserved buffer index
   `c_metal_acceleration_structure_buffer_index` (13), and
   `set_acceleration_structure` binds the top level structure there and
   `useResource()`s each bottom level structure it references (Metal makes
   only the directly bound structure resident). It is a no-op on the null and
   GL backends. Shaders reach vertex data through `Buffer::get_device_address()`,
   which is the buffer device address on Vulkan and `gpuAddress()` on Metal.

6. **Position fetch**: `VK_KHR_ray_tracing_position_fetch`
   (`Device_info::use_ray_tracing_position_fetch`, GLSL
   `GL_EXT_ray_tracing_position_fetch`) is enabled when available, and bottom
   level structures are then built with
   `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR`. It lets a shader
   fetch the committed triangle's object-space vertex positions for a geometric
   normal without a per-instance vertex / index lookup table. It is optional:
   without it (Metal, where SPIRV-Cross has no MSL lowering for the
   extension) the editor's ray query shaders fetch the triangle's positions
   from the `Mesh_memory` stream-0 pool through the per-instance device
   address in the instance record.

## Editor renderer

- `Mesh_memory` vertex and index pools carry
  `acceleration_structure_build_input | shader_device_address` usage, so BLAS
  builds read them in place with no data duplication.
- `Ray_trace_scene` (`src/editor/renderers/ray_trace_scene.{hpp,cpp}`, one per
  `Scene_root` with device dependencies on a ray query device) is what the
  three ray-query consumers - `Ray_trace_renderer`, `Ddgi_renderer` and
  `Radiance_cascades_renderer` - trace. See "Ray_trace_scene" below.
- `Scene_tlas` (`src/editor/renderers/scene_tlas.{hpp,cpp}`, one per consumer,
  render phase) builds the slot's missing bottom level structures, builds the
  consumer's top level structure for the frame-in-flight slot from the slot's
  instances, and uploads the slot's instance records into the consumer's
  record SSBO. Instance capacity grows to a high-water mark on scene change, so
  steady-state frames allocate nothing. `Lightmap_baker` keeps its own
  structures and records (see [`ddgi.md`](ddgi.md)).
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
  Trace") toggles it, edits the `Ray_trace_config` settings (downscale, max
  rays, max bounces) and displays the rgba8 output texture, which is sized to
  the viewport divided by the downscale factor and recreated on resize; the
  renderer no-ops while disabled. The renderer reads the settings from the
  render phase's copy of its frame (`App_context::render_settings->ray_trace`,
  `doc/editor/editor.md` "Frame phases and the render thread"), so an edit
  takes effect at the next frame's snapshot. The switch is the renderer's
  live flag (`set_enabled`, main thread: the window and MCP
  `set_ray_trace`); M2 copies it into the packet
  (`Frame_packet::ray_trace_enabled`) and R hands it to the renderer
  (`set_render_enabled`), which traces with that copy. Each trace leaves a
  `Rendered_output` (output size, instance count) per frame in flight; the
  window and MCP read the last rendered frame's, and the window draws the
  output through `get_output_texture_reference()`, which the render thread
  resolves when it draws the window.

## Ray_trace_scene

The content layer as the ray-query consumers see it, kept in the prepare
phase (M2) and read by the render phase (R) only through one slot per frame
in flight, so R never walks the live scene.

- Hooks. `Scene_root` forwards the mesh hooks of content layer meshes (any
  thread; they only mark state under a mutex): register, unregister, a
  primitive list or in-place `Buffer_mesh` replacement
  (`on_mesh_primitives_changed`), primitive data and a visible or active
  flag change mark the structure dirty; a material change marks the
  materials dirty; a transform change queues the mesh; a bounds change
  (`on_mesh_bounds_changed`, the live mesh-component drag) marks the bounds
  dirty. Before the first prepare a hook only marks the structure dirty, so a
  root the frame loop never prepares (the previews) accumulates nothing.
- Working set (M2 only). `Ray_trace_geometry`, one per unique
  original-variant `Buffer_mesh` (keyed by address, shared by every instance
  and every consumer): it pins the `Primitive` and its `Primitive_render_shape`
  (the shape owns the `Buffer_mesh` and its pool ranges), holds the bottom
  level build input (stream 0 read in place, position at offset 0, the
  `triangle_fill_indices` range; quantized positions read as snorm16 with the
  dequantization affine as a build transform,
  `erhe::scene_renderer::get_blas_position_input`, see
  [`vertex_position_quantization.md`](../erhe/vertex_position_quantization.md))
  and the geometry half of the instance record (addresses, strides, position
  encoding); `valid` folds every skip condition (no triangles, no stream 1, a
  missing device address, a stride stream 1 cannot fetch, an unsupported
  position format). Instances are the visible, active, non-skinned content
  meshes' primitives in content layer times primitive order, which is
  `instance_custom_index`. Skinned meshes are not traced (their structure
  would need post-skinning positions) but have a mesh entry for the content
  bounds.
- `prepare(frame_index, frames_in_flight)` (`App_scenes::
  prepare_ray_trace_scenes`, every frame after `update_material_sets`, under
  `item_host_mutex`): a dirty structure rebuilds the working set, reusing the
  geometries by `Buffer_mesh` except those of meshes whose `Buffer_mesh` was
  replaced in place (`Primitive_render_shape::commit_geometry_buffer_mesh()`
  move-assigns at the same address over freed pool ranges); the geometries
  the new structure does not name are retired. Otherwise the queued
  transforms are applied once per `world_from_node_serial`, the material
  slots and transmissive flags are re-resolved when the materials are dirty
  or the forward set's `get_write_count()` moved (a material property edit
  reaches only the latter), and a bounds change refreshes the mesh bounds
  and the records' dequantization affine. Only instances whose fields
  changed are marked.
- Publish. The slot gets a full copy when its structure serial differs,
  otherwise only the instances marked since its last publish (a per-instance
  slot bitmask); plus the content bounds (a cached union of the non-skinned
  meshes' world bounds and the skinned meshes' posed `get_aabb_world()`, read
  every frame since no notification reaches posed bounds), and the forward
  set's live slot count. A quiet frame copies nothing; a dragged mesh costs
  one transform update and one instance copy per frame.
- R. `Ray_trace_scene::get_slot(frame_index)` (render thread, `r_render`,
  verifies the slot's frame) feeds `Scene_tlas::update`, which creates and
  builds a geometry's bottom level structure the first time any consumer's
  slot names it; one structure per geometry serves all three consumers. A
  retired geometry lives on through the slots that still hold it, and
  `~Acceleration_structure` defers the device destroy to frame completion.
  `Ddgi_renderer::update_volume` and `Radiance_cascades_renderer::
  update_layout` fit to `pad_content_bounds(slot.content_bounds, padding)`.
- The log category `editor.raytrace_frame` (`config/editor/logging.json`) at
  `trace` logs each prepare's structure rebuilds, transform updates, instance copies and full publishes.

## Future work

- [plans/raytrace.md](../plans/raytrace.md) - `Lightmap_baker` BLAS cache identity, skinned
  meshes, TLAS refit, Metal TLAS build allocations, viewport composition.
