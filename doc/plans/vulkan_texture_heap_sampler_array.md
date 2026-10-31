# Vulkan texture heap without descriptor indexing (Bifrost: Mali-G52)

Status: proposed

This plan extends `doc/erhe/graphics.md` "Texture heap" and
`doc/erhe/vulkan_backend.md` "Texture heap (bindless)" with a Vulkan heap
path for drivers that have no descriptor indexing: a bounded combined image
sampler array at set 1, written in full, in the shape of the OpenGL sampler
array path. The first target is Mali-G52 (Bifrost), whose drivers also
lack other features the Vulkan backend requires; the plan lists those gaps
with the fallback each needs, so that "erhe runs on Mali-G52" is one piece
of work with the heap as its largest part. The shader variant browser's
Mali analytics (`doc/erhe/graphics.md` "Mali analytics") analyze the
modules this path produces on Bifrost cores once it lands.

## Goals

- The Vulkan backend starts and renders on a Mali-G52 driver: no
  descriptor indexing, Vulkan 1.1 or 1.2, no `synchronization2`.
- The new heap path is selected by the device's features and can be forced
  on any device, so it is verified on desktop with the image gates before a
  Bifrost device is in hand.
- Scenes whose material textures fit the heap render identically on both
  Vulkan heap paths; a scene that exceeds the heap degrades the way the
  OpenGL sampler array path does today (fallback texture) and says so once
  in the log, until the paging follow-up.

## Facts established (2026-10-29)

- **Mali-G52 driver** (vulkan.gpuinfo.org report 15490, driver 25.1.0,
  API 1.2.131, Linux arm64; Android devices in recent crash reports run
  1.1.131): `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`
  and `descriptorIndexing` false, `VK_EXT_descriptor_indexing` absent,
  `shaderSampledImageArrayDynamicIndexing` true. Limits:
  `maxPerStageDescriptorSamplers` 128, `maxPerStageDescriptorSampledImages`
  256, `maxDescriptorSetSamplers` 768, `maxDescriptorSetSampledImages`
  1536, `maxPerSetDescriptors` 65536, `maxBoundDescriptorSets` 4.
  Present: `VK_KHR_timeline_semaphore` (feature true), `VK_KHR_maintenance3`,
  `VK_KHR_shader_draw_parameters` (true), `VK_KHR_multiview` (true),
  `VK_EXT_host_query_reset` (true), `VK_KHR_create_renderpass2`,
  `VK_KHR_buffer_device_address`, `VK_KHR_16bit_storage`. Absent:
  `VK_KHR_synchronization2`, `VK_KHR_push_descriptor`,
  `VK_KHR_draw_indirect_count`, `VK_KHR_dynamic_rendering`. Features false:
  `shaderClipDistance`, `vertexPipelineStoresAndAtomics`,
  `multiDrawIndirect`, `drawIndirectCount`; `fragmentStoresAndAtomics`
  true.
- **What erhe's Vulkan device init aborts on** (`vulkan_device_init.cpp`):
  `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`,
  `descriptorBindingVariableDescriptorCount` (the texture heap);
  `synchronization2` (Vulkan 1.3 feature); on desktop also
  `shaderClipDistance` and `vertexPipelineStoresAndAtomics` (soft warnings
  on Android, where Mali-G715 lacks them too). The instance requests
  `VK_API_VERSION_1_3`.
- **What already has a fallback**: push descriptors (per-frame descriptor
  pools when `VK_KHR_push_descriptor` is absent), multi-draw indirect (a
  per-draw loop with the draw id in a push constant), draw indirect count
  (`Device_info::use_draw_indirect_count` false keeps the CPU command path),
  ray query (`Device_info::use_ray_query`), clip distance in shader source
  (`ERHE_HAS_CLIP_DISTANCE`), non-semantic debug info that needs
  `SPV_KHR_relaxed_extended_instruction` (recompiled without it).
- **The heap today** (`vulkan_texture_heap.cpp`): set 1 is one binding of
  `Device_info::texture_heap_size` combined image samplers
  (`max_texture_heap_size` 4096, 1024 on Android, bounded by
  `maxPerSetDescriptors`) with `PARTIALLY_BOUND | UPDATE_AFTER_BIND |
  VARIABLE_DESCRIPTOR_COUNT`. Each `reset_heap()` acquires a per-pass set
  (recycled once the frame retired), `allocate()` writes one descriptor and
  returns its index, `bind()` binds the set. The shader declares
  `layout(set = 1, binding = 0) uniform sampler2D erhe_texture_heap[];`
  (`shader_stages_create_info.cpp`) and samples
  `erhe_texture_heap[handle.x]` (`erhe_texture.glsl`). The index is a
  per-draw record field, dynamically uniform: no `nonuniformEXT`, no
  `ShaderNonUniform` capability in any module.
- **The OpenGL sampler array path** (`gl_texture_heap.cpp`,
  `Texture_heap_path::opengl_sampler_array`) is the precedent: a unit range
  of `max_per_stage_descriptor_samplers` minus the dedicated samplers,
  filled with the fallback texture, `allocate()` returning the slot or
  `invalid_texture_handle` when the range is full (the shader then samples
  white, `sample_texture()` in `erhe_texture.glsl`).
- **The modules work with a bounded array.** With the heap declared as a
  bounded `OpTypeArray` and the capability dropped, 72 of 74 texture-heap
  modules from a local cache compile on Mali-G52 in the Mali Offline
  Compiler (the two others use ray query); they pass `spirv-val` for
  Vulkan 1.3 (a 74-module sample; the full sweep of 862 modules is in
  `doc/erhe/graphics.md` "Mali analytics").
- **Heap demand**: the material set keeps one persistent heap per set
  (`doc/erhe/draw_list_material_set.md` 6.3); Bistro references 343
  material textures, above the 128 samplers per stage a Mali-G52 allows,
  before the dedicated samplers (shadow maps, lightmap, DDGI) are counted.
- **Other capabilities erhe modules declare**: `SampledCubeArray` (782 of
  862 modules; `imageCubeArray` feature), `DemoteToHelperInvocation` (22;
  `VK_EXT_shader_demote_to_helper_invocation`, listed by the offline
  compiler for Mali-G52), `PhysicalStorageBufferAddresses` (10; ray tracing
  only). The device feature values of the first two on Mali-G52 are
  unverified.

## Design

### 1. `Texture_heap_path::vulkan_sampler_array`

A fifth value of the existing enum, its second Vulkan one (the shader side keeps the
same `ERHE_TEXTURE_HEAP_*` selection pattern as `erhe_texture.glsl` uses
today):

- **Layout**: set 1, binding 0, `descriptorCount = texture_heap_size`, no
  binding flags, a plain pool, plain `vkAllocateDescriptorSets`.
  `texture_heap_size` on this path is
  `min(max_texture_heap_size, maxPerStageDescriptorSamplers - dedicated)`
  where `dedicated` is the largest dedicated sampler count of any bind
  group layout that uses the heap (the layouts are known at device init
  only through their declarations, so the device publishes the budget and
  `Bind_group_layout` verifies at creation that its dedicated samplers plus
  the heap fit `maxPerStageDescriptorSamplers`); the same bound applies to
  `maxPerStageDescriptorSampledImages`.
- **Every slot is written**: a new set is filled with the fallback texture
  and sampler (`write_fallback_slots()` exists for this) before any
  `allocate()` writes a slot, since nothing is partially bound.
- **No update after bind**: a set that a recorded command buffer binds is
  not written again until that command buffer retired. `reset_heap()`
  takes a fresh set per pass as today; `allocate()` after `bind()` on the
  same set is a programming error on this path and a `ERHE_VERIFY`, not a
  silent rewrite. The material set writes its heap in M2 before the pass
  records the bind.
- **Full heap**: `allocate()` returns `invalid_texture_handle` and logs a
  warning once per heap with the texture count it was asked for; the
  shader samples white, as on the OpenGL sampler array path.
- **Shader**: `#define ERHE_TEXTURE_HEAP_VULKAN_SAMPLER_ARRAY 1` and
  `layout(set = 1, binding = 0) uniform sampler2D erhe_texture_heap[ERHE_TEXTURE_HEAP_SIZE];`
  with `ERHE_TEXTURE_HEAP_SIZE` defined from `texture_heap_size`; no
  `GL_EXT_nonuniform_qualifier`. `erhe_texture.glsl` samples
  `erhe_texture_heap[texture_handle.x]` on both Vulkan paths (one branch
  covering both defines). `texture_heap_size` is part of the `Spirv_cache`
  key through the preamble text, as every define is.
- **Compute**: the ray trace compute shader binds the heap at the compute
  bind point as today; the same set serves both bind points.

### 2. Path selection and the override

`vulkan_device_init.cpp` selects `vulkan_descriptor_indexing` when
`runtimeDescriptorArray`, `descriptorBindingPartiallyBound` and
`descriptorBindingVariableDescriptorCount` are all present, else
`vulkan_sampler_array`; the three abort checks go. A graphics config entry
`vulkan.texture_heap_path` (`auto` | `descriptor_indexing` |
`sampler_array`, `added_in` the next config version; environment override
`ERHE_VULKAN_TEXTURE_HEAP_PATH` for the scripts) forces a path, so the
sampler array path is exercised on every desktop driver. Device init logs
the chosen path with the features and limits it was derived from, as it
logs the heap size today.

### 3. The other Mali-G52 gaps

Each is its own commit after the heap, smallest first:

- **API version**: the instance requests the highest of 1.3 and what the
  loader and device offer, and the 1.2 / 1.3 feature structs are queried
  only when the device version has them (the extension structs
  `VK_KHR_timeline_semaphore`, `VK_EXT_host_query_reset` otherwise).
- **`synchronization2`**: the backend records `vkCmdPipelineBarrier2` (8
  sites) and submits with `vkQueueSubmit2` (2 sites). Without the feature
  or `VK_KHR_synchronization2`, a barrier helper lowers the
  `VkDependencyInfo` to `vkCmdPipelineBarrier` (stage and access masks
  mapped from their 64-bit forms) and the submit to `vkQueueSubmit` with
  `VkTimelineSemaphoreSubmitInfo`. `Device_info::use_synchronization2`
  names the path; the abort goes.
- **`shaderClipDistance`**: the ImGui vertex shader clips with
  `gl_ClipDistance`; without the feature it clips with a scissor per draw
  list (the clip rectangle ImGui supplies), which the renderer already
  sets. The desktop abort goes and the gate is the existing
  `ERHE_HAS_CLIP_DISTANCE`.
- **`vertexPipelineStoresAndAtomics`**: find the vertex-stage buffer
  stores (an audit of `res/shaders/*.vert` and the generated preambles);
  each either moves to a compute or fragment stage or is gated off. The
  desktop abort goes once no vertex stage stores.
- **`imageCubeArray`** and **`shaderDemoteToHelperInvocation`**: query and
  gate as the other `ERHE_HAS_*` features are (`shader_stages_create_info.cpp`):
  the cube array sampler declarations compile only when the feature is on,
  and `demote` falls back to `discard`.

### 4. Paging (follow-up, not in this plan's build order)

A Mali-G52 shades at most 128 samplers per stage, so Bistro does not fit
one heap set. The options, to be decided when the base path works on a
device: split each draw list into batches by the heap page their materials
use, with the page part of the command key so GPU-generated commands keep
batches intact; or put same-size, same-format material textures into 2D
array textures so one sampler serves many materials. Both change the draw
list renderer and `Material_set`, so they are a separate plan.

## Build order

1. **Heap path** (`erhe::graphics`): the enum value, the layout and pool,
   `Texture_heap_impl` writing full sets, the preamble define and
   declaration, `erhe_texture.glsl`, `Bind_group_layout` budget check, the
   config and environment override. Verify on desktop with the path
   forced: `render_scene_image` gates and the draw list gates identical to
   the descriptor indexing path (`memory-bank/local` gate scripts on this
   machine), `ctest` graphics GPU tests on both paths, validation clean,
   a scene with more textures than the heap renders with the fallback and
   logs the warning once.
2. **Path selection**: device init chooses by features; the three heap
   aborts go; the log names the path. Verify: a desktop run selects
   descriptor indexing; the forced path still works.
3. **API version and feature queries** (section 3, first item). Verify: a
   run with the loader's `VK_LOADER_DEBUG` shows 1.3 on desktop; a device
   simulation layer profile with `apiVersion` 1.2 and the Mali-G52 features
   (the LunarG `VK_LAYER_KHRONOS_profiles` layer with a profile built from
   report 15490) starts the device.
4. **`synchronization2` lowering**. Verify: forced off on desktop
   (`ERHE_VULKAN_SYNCHRONIZATION2=0`), gates identical, validation clean
   with synchronization validation on.
5. **Clip distance, vertex stores, cube array, demote**. Verify each under
   the profiles layer with the feature off.
6. **Device run**: a Mali-G52 board or phone; the editor starts, the
   default scene renders, the shader variant browser records the modules,
   and `scripts/shader_analytics.py --core Mali-G52` reports them without
   capability errors. Docs: `doc/erhe/graphics.md` "Texture heap" row,
   `doc/erhe/vulkan_backend.md` heap section and feature table,
   `doc/building.md` if a config option is added.

## Open

- Whether the Android Mali-G52 drivers (1.1.131) match the Linux report's
  extension list; step 3's profile covers the 1.1 case only if the report
  it is built from is a 1.1 one.
- Paging (section 4) is required for Bistro-class scenes on Mali-G52 and
  is left to its own plan.
- `maxBoundDescriptorSets` is 4 on Mali-G52; the backend uses sets 0 and 1
  today, and any set the lowering adds stays within that.
