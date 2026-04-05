# Vulkan Backend Implementation Plan

## Implementation Progress

Steps marked [DONE] have been implemented.

## Original State (before implementation)

The Vulkan backend had working device initialization, swapchain, surface, VMA-backed
buffers and textures, ring buffers, immediate commands, and format conversion helpers
(~8,200 lines across 42 files). Everything needed to actually render was stubbed.

### What is implemented

| Component | File | Status |
|-----------|------|--------|
| Device init, queues, VMA | `vulkan_device.cpp` (1535 lines) | Complete |
| Swapchain, frame sync, present | `vulkan_swapchain.cpp` (994 lines) | Complete |
| Surface, format selection | `vulkan_surface.cpp` (710 lines) | Complete |
| Ring buffer, fence sync | `vulkan_ring_buffer.cpp` (330 lines) | Complete |
| Immediate command buffers | `vulkan_immediate_commands.cpp` (468 lines) | Complete |
| Format/flag conversions | `vulkan_helpers.cpp` (1079 lines) | Complete |
| Buffer creation (VMA) | `vulkan_buffer.cpp` (396 lines) | Partial (clear FATAL, staging TODO) |
| Texture creation (VMA) | `vulkan_texture.cpp` (230 lines) | Partial (clear FATAL) |
| Sampler creation | `vulkan_sampler.cpp` | Partial (no VkSampler, operator== stub) |
| SPIR-V compilation (glslang) | `vulkan_shader_stages_prototype.cpp` | Partial (compiles but never reaches ready) |

### What is stubbed (ERHE_FATAL)

| Component | File | Issue |
|-----------|------|-------|
| Render command encoder | `vulkan_render_command_encoder.cpp` | All draw commands, pipeline, viewport, scissor |
| Compute command encoder | `vulkan_compute_command_encoder.cpp` | All compute dispatch |
| Blit command encoder | `vulkan_blit_command_encoder.cpp` | Most ops (copy_from_buffer partially done) |
| Command encoder base | `vulkan_command_encoder.cpp` | set_buffer |
| Shader stages | `vulkan_shader_stages.cpp` | No VkShaderModule, is_valid() always false |
| Non-swapchain render passes | `vulkan_render_pass.cpp` | FATAL for off-screen (shadows, post-processing) |
| Depth/stencil in render passes | `vulkan_render_pass.cpp` | VERIFY fails for depth/stencil |
| Texture heap (bindless) | `vulkan_texture_heap.cpp` | All functions FATAL |
| Vertex input state | `vulkan_vertex_input_state.cpp` | set/reset/update empty |
| GPU timer | `vulkan_gpu_timer.cpp` | All functions empty |
| Debug groups | `vulkan_debug.cpp` | TODO markers |

---

## Phase 1 -- Changes to erhe::graphics and Code That Uses It

Phase 1 prepares the build system and abstraction layer so the Vulkan backend can
be implemented. The Metal backend was added without changing the public API, and
the Vulkan backend follows the same approach -- all changes are either build-system
or Vulkan-internal.

### 1.1 Force ERHE_SPIRV on for Vulkan builds [DONE]

**File:** `CMakeLists.txt` (line 220)

Currently ERHE_SPIRV is only forced on for Metal. Vulkan also needs SPIR-V
compilation via glslang. Add the same force-enable for Vulkan:

```cmake
if (${ERHE_GRAPHICS_LIBRARY} STREQUAL "metal" OR ${ERHE_GRAPHICS_LIBRARY} STREQUAL "vulkan")
    set(ERHE_SPIRV "ON" CACHE STRING "Enable SPIR-V" FORCE)
endif ()
```

This ensures glslang is fetched and linked, and `ERHE_SPIRV` is defined.

### 1.2 Verify editor GL calls are guarded (no changes needed)

All direct `gl::` calls in `src/editor/` are already wrapped with
`#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)`:

- `app_rendering.cpp:368` -- `gl::clip_control()`
- `editor.cpp:1255-1260` -- `gl::clip_control()`, `gl::enable(framebuffer_srgb)`
- `editor.cpp:1764` -- `gl::initialize_logging()`
- `renderers/frustum_tiler.cpp:462` -- `gl::texture_page_commitment_ext()`
- `xr/headset_view.cpp` -- framebuffer operations (mostly commented out)

No changes required.

### 1.3 No public API changes needed

The pimpl pattern isolates backend implementations behind the same public headers
(`device.hpp`, `buffer.hpp`, `texture.hpp`, `render_command_encoder.hpp`, etc.).
All Vulkan-specific infrastructure (descriptor sets, pipeline layouts, image
layout tracking) lives inside the Vulkan backend files, matching how Metal added
argument buffers and pipeline state creation internally.

### 1.4 No new source files needed

All Vulkan backend files already exist as stubs in
`src/erhe/graphics/erhe_graphics/vulkan/` and are listed in `CMakeLists.txt`.
The implementation work fills in these existing files.

---

## Phase 2 -- Implementing the Vulkan Graphics Backend

Work is ordered so each step builds on the previous and is independently testable.
The first milestone is a visible clear-color frame, then a drawn triangle, then
incrementally adding full feature parity.

### Key Design Decisions

| Decision | Approach | Rationale |
|----------|----------|-----------|
| Descriptor binding | `VK_KHR_push_descriptor` primary | Closest to GL/Metal immediate binding model; already tracked in `Device_extensions` |
| Pipeline creation | At draw time with `VkPipelineCache` | Matches Metal pattern; cache prevents recompilation across runs |
| Pipeline layout | Single global layout with max bindings | Matches GL model where binding points are global; GLSL `binding = N` maps to `set = 0, binding = N` |
| Image layout transitions | Explicit at render pass boundaries, tracked per-texture | Simple initial approach; refinable later |
| Multi-draw indirect | `vkCmdDrawIndexedIndirect` in loop with push constant draw ID | Same approach as Metal backend |
| Active render pass | Static pointer pattern | Matches Metal; enables command encoder to find the active command buffer |

### 2.1 Shader Stages: VkShaderModule Creation [DONE]

**Files:** `vulkan_shader_stages.hpp`, `vulkan_shader_stages.cpp`,
`vulkan_shader_stages_prototype.cpp`

**Problem:** The prototype compiles SPIR-V via glslang but never transitions to
`Shader_build_state::ready` (line 62-65 of prototype). The `Shader_stages_impl`
never creates VkShaderModule and `is_valid()` always returns false.

**Work:**
1. In `link_program()`, set `m_state = Shader_build_state::ready` after successful link
2. In `Shader_stages_impl::reload()`:
   - Extract SPIR-V binaries via `m_glslang_shader_stages.get_spirv_binary()` per stage
   - Call `vkCreateShaderModule()` for each binary
   - Store the VkShaderModule handles
   - Set `m_is_valid = true`
3. Implement `invalidate()` to destroy shader modules
4. Implement move constructor and move assignment (currently ERHE_FATAL)
5. Add SPIR-V reflection using glslang's `TProgram::getIntermediate()` to extract
   descriptor binding information for pipeline layout creation
6. Add accessor methods: `get_vertex_module()`, `get_fragment_module()`, etc.

**Testable result:** Shader programs compile and report `is_valid() == true`.

### 2.2 VkSampler Creation [DONE]

**Files:** `vulkan_sampler.hpp`, `vulkan_sampler.cpp`, `vulkan_helpers.hpp`,
`vulkan_helpers.cpp`

**Work:**
1. Add conversion helpers to `vulkan_helpers.cpp`:
   - `to_vulkan_filter(Filter)` -> `VkFilter`
   - `to_vulkan_sampler_mipmap_mode(Sampler_mipmap_mode)` -> `VkSamplerMipmapMode`
   - `to_vulkan_sampler_address_mode(Sampler_address_mode)` -> `VkSamplerAddressMode`
   - `to_vulkan_compare_op(Compare_operation)` -> `VkCompareOp`
2. Uncomment `m_vulkan_sampler` and `get_vulkan_sampler()` in header
3. Implement constructor: populate `VkSamplerCreateInfo`, call `vkCreateSampler()`
4. Implement destructor: call `vkDestroySampler()`
5. Fix `operator==` to compare sampler handles

**Testable result:** Sampler objects create and destroy without crashing.

### 2.3 Pipeline Layout and Descriptor Infrastructure [DONE]

**Files:** `vulkan_device.hpp`, `vulkan_device.cpp`

**Work:**
1. Create a `VkPipelineCache` in Device_impl constructor (can be saved/loaded to disk)
2. Design descriptor binding using `VK_KHR_push_descriptor`:
   - Create a single `VkDescriptorSetLayout` with bindings for uniform buffers,
     storage buffers, and combined image samplers at fixed indices matching GLSL
     `layout(binding = N)`
   - Mark with `VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR`
3. Create a `VkPipelineLayout` from the descriptor set layout
4. Store pipeline layout and descriptor set layout on Device_impl
5. Add fallback to traditional descriptor sets if push descriptors unavailable:
   - Per-frame `VkDescriptorPool` with dynamic allocation
   - Descriptor set allocation and update before draw calls

**Testable result:** Pipeline layout created during device init.

### 2.4 Active Render Pass Tracking [DONE]

**Files:** `vulkan_render_pass.hpp`, `vulkan_render_pass.cpp`

**Work:**
1. Add `static Render_pass_impl* s_active_render_pass` (matching Metal pattern)
2. Add static accessors:
   - `get_active_command_buffer()` -> `VkCommandBuffer`
   - `get_active_render_pass_handle()` -> `VkRenderPass`
3. Set in `start_render_pass()`, clear in `end_render_pass()`

**Testable result:** Render pass tracking works with existing swapchain paths.

### 2.5 Graphics Pipeline Creation [DONE]

**Files:** `vulkan_render_command_encoder.cpp`, `vulkan_render_command_encoder.hpp`,
`vulkan_helpers.hpp`, `vulkan_helpers.cpp`

**Work:**
1. Add conversion helpers:
   - `to_vulkan_primitive_topology(Primitive_type)` -> `VkPrimitiveTopology`
   - `to_vulkan_cull_mode(Cull_face_mode)` -> `VkCullModeFlags`
   - `to_vulkan_front_face(Front_face_direction)` -> `VkFrontFace`
   - `to_vulkan_stencil_op(Stencil_op)` -> `VkStencilOp`
   - `to_vulkan_blend_factor(Blending_factor)` -> `VkBlendFactor`
   - `to_vulkan_blend_op(Blend_equation_mode)` -> `VkBlendOp`
   - `to_vulkan_vertex_format(erhe::dataformat::Format)` -> `VkFormat`
   - `to_vulkan_index_type(erhe::dataformat::Format)` -> `VkIndexType`
2. Implement `set_render_pipeline_state()` (following Metal pattern):
   - Get active command buffer from `Render_pass_impl::get_active_command_buffer()`
   - Early return if null or shaders invalid
   - Build `VkGraphicsPipelineCreateInfo`:
     - Shader stages from VkShaderModule handles
     - Vertex input from `Vertex_input_state_data`
     - Input assembly from topology
     - Viewport and scissor as dynamic state
     - Rasterization, depth/stencil, color blend, multisample from pipeline data
     - Pipeline layout and render pass from Device_impl and Render_pass_impl
   - Create pipeline via `vkCreateGraphicsPipelines()` with VkPipelineCache
   - Cache by configuration hash to avoid recreating identical pipelines
   - Call `vkCmdBindPipeline()`

**Testable result:** Pipeline objects create at draw time.

### 2.6 Viewport, Scissor, and Draw Commands [DONE]

**Files:** `vulkan_render_command_encoder.cpp`

**Work:**
1. `set_viewport_rect()`: call `vkCmdSetViewport()` on active command buffer
2. `set_viewport_depth_range()`: update stored depth range, call `vkCmdSetViewport()`
   (Vulkan viewport includes depth range)
3. `set_scissor_rect()`: call `vkCmdSetScissor()`
4. `set_index_buffer()`: store buffer, call `vkCmdBindIndexBuffer()` using
   `buffer->get_impl().get_vk_buffer()`
5. `set_vertex_buffer()`: call `vkCmdBindVertexBuffers()`
6. `draw_primitives()`: call `vkCmdDraw()`
7. `draw_indexed_primitives()`: call `vkCmdDrawIndexed()`

**First milestone:** At this point, a clear-color frame should display via
the swapchain render pass, and simple geometry should draw.

### 2.7 Resource Binding (set_buffer) [DONE]

**Files:** `vulkan_command_encoder.cpp`, `vulkan_render_command_encoder.cpp`

**Work:**
1. Implement `set_buffer()` with offset/length/index:
   - For `Buffer_target::draw_indirect`: store buffer for later indirect draw use
   - For `Buffer_target::uniform` / `Buffer_target::storage`:
     - With push descriptors: call `vkCmdPushDescriptorSetKHR()` with
       `VkWriteDescriptorSet` binding the buffer at the given index
     - Without push descriptors: allocate from per-frame descriptor pool, write, bind
   - The `index` parameter maps to GLSL `binding = N`
2. Implement `set_buffer()` without offset/length: bind full buffer at binding 0

**Testable result:** Shaders can access uniform and storage buffer data.

### 2.8 Vertex Input State [DONE]

**Files:** `vulkan_vertex_input_state.cpp`, `vulkan_vertex_input_state.hpp`

**Work:**
1. Implement `set()`: store vertex attribute and binding descriptions
2. Implement `reset()`: clear stored descriptions
3. Implement `update()`: rebuild Vulkan vertex input state info

Note: Pipeline creation (step 2.5) reads vertex input data from the
`Vertex_input_state_data` struct. This step ensures the data is properly
maintained for pipeline compatibility.

**Testable result:** Vertex attributes bind correctly for rendering.

### 2.9 Non-Swapchain Render Passes [DONE]

**Files:** `vulkan_render_pass.cpp`, `vulkan_render_pass.hpp`,
`vulkan_texture.hpp`

This is critical for shadow maps, post-processing, and the ID buffer used
for picking.

**Work:**
1. Add `get_vk_image_view()` accessor to `Texture_impl`
2. Handle `m_swapchain == nullptr` in Render_pass_impl constructor:
   - Iterate color attachments, get VkImageView from each texture
   - Process depth and stencil attachments
   - Create `VkRenderPass` with attachment descriptions mapping
     `Load_action` -> `VkAttachmentLoadOp`, `Store_action` -> `VkAttachmentStoreOp`
   - Create `VkFramebuffer` using render pass and image views
   - Store clear values for all attachments
3. Implement `start_render_pass()` for non-swapchain:
   - Allocate command buffer from Device_impl
   - Begin command buffer
   - Insert image layout transitions via `vkCmdPipelineBarrier()`:
     color `UNDEFINED` -> `COLOR_ATTACHMENT_OPTIMAL`,
     depth `UNDEFINED` -> `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`
   - Call `vkCmdBeginRenderPass()`
   - Set `s_active_render_pass`
4. Implement `end_render_pass()` for non-swapchain:
   - `vkCmdEndRenderPass()`
   - Layout transitions for sampling:
     color -> `SHADER_READ_ONLY_OPTIMAL`,
     depth -> `SHADER_READ_ONLY_OPTIMAL`
   - End and submit command buffer
   - Clear `s_active_render_pass`
5. Implement `get_sample_count()` from attachment textures
6. Add depth/stencil support to swapchain render passes (remove the VERIFY
   at line 72-73 and create depth/stencil attachments)
7. Add MSAA resolve support (remove the VERIFY at line 71)

**Testable result:** Shadow maps, ID buffer, post-processing render to textures.

### 2.10 Multi-Draw Indirect [DONE]

**Files:** `vulkan_render_command_encoder.cpp`

**Work:**
1. Implement `multi_draw_indexed_primitives_indirect()`:
   - Loop over `drawcount` calls to `vkCmdDrawIndexedIndirect()`
   - Set draw ID per call via `vkCmdPushConstants()` (matches Metal approach)
   - If `gl_DrawID` is available (Vulkan 1.1 core via
     `VK_KHR_shader_draw_parameters`), consider using it instead of push constants

**Testable result:** Batched mesh rendering with multi-draw indirect.

### 2.11 Texture Binding / Texture Heap [DONE]

**Files:** `vulkan_texture_heap.cpp`, `vulkan_texture_heap.hpp`,
`vulkan_device.cpp`

Implement texture binding using `VK_EXT_descriptor_indexing` (Vulkan 1.2 core).

**Work:**
1. Create descriptor pool with large combined image sampler count
2. Create descriptor set with sampler array using:
   - `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT`
   - `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT`
3. `assign()` / `allocate()`: write descriptors for texture+sampler pairs,
   return array index as handle
4. `get_shader_handle()`: same as allocate but for ad-hoc lookups
5. `bind()`: bind the descriptor set at the appropriate set index
6. `unbind()`: no-op (set remains bound)
7. `reset_heap()`: reset descriptor pool

The "handle" is an index into the sampler array (matching GL bindless model
where the handle is an opaque uint64_t).

**Testable result:** Textured materials render correctly.

### 2.12 Blit Command Encoder [DONE]

**Files:** `vulkan_blit_command_encoder.cpp`

**Work:**
1. Fix `copy_from_buffer()` (buffer-to-texture): add missing image layout
   transitions (barrier before: `UNDEFINED` -> `TRANSFER_DST_OPTIMAL`,
   barrier after: `TRANSFER_DST_OPTIMAL` -> `SHADER_READ_ONLY_OPTIMAL`)
2. `copy_from_texture()` (texture-to-texture): `vkCmdCopyImage()` with layout transitions
3. `copy_from_texture()` (texture-to-buffer): `vkCmdCopyImageToBuffer()`
4. `blit_framebuffer()`: `vkCmdBlitImage()` with layout transitions
5. `fill_buffer()`: `vkCmdFillBuffer()`
6. `copy_from_buffer()` (buffer-to-buffer): `vkCmdCopyBuffer()`
7. `generate_mipmaps()`: blit chain transitioning each mip level
   `UNDEFINED` -> `TRANSFER_DST_OPTIMAL`, blit from previous level,
   then `TRANSFER_DST_OPTIMAL` -> `SHADER_READ_ONLY_OPTIMAL`
8. Multi-slice/level `copy_from_texture()`: loop over slices and levels

**Testable result:** Texture copies, framebuffer blits, mipmap generation work.

### 2.13 Compute Command Encoder [DONE]

**Files:** `vulkan_compute_command_encoder.cpp`,
`vulkan_compute_command_encoder.hpp`

**Work:**
1. `set_compute_pipeline_state()`: create VkComputePipeline from compute shader
   module and pipeline layout, cache, call `vkCmdBindPipeline()`
2. `dispatch_compute()`: call `vkCmdDispatch()`
3. `set_buffer()`: push descriptors or descriptor set binding for compute

**Testable result:** Compute shader dispatches execute.

### 2.14 Buffer and Texture Clear [DONE]

**Files:** `vulkan_buffer.cpp`, `vulkan_texture.cpp`

**Work:**
1. Buffer `clear()`: `vkCmdFillBuffer()` via immediate commands
2. Texture `clear()`: `vkCmdClearColorImage()` or `vkCmdClearDepthStencilImage()`
   via immediate commands with layout transitions

**Testable result:** Clear operations do not crash.

### 2.15 GPU Timer [DONE]

**Files:** `vulkan_gpu_timer.cpp`, `vulkan_gpu_timer.hpp`

**Work:**
1. Create `VkQueryPool` with `VK_QUERY_TYPE_TIMESTAMP` in Device_impl
2. `begin()`: `vkCmdWriteTimestamp()` at start
3. `end()`: `vkCmdWriteTimestamp()` at end
4. `read()`: `vkGetQueryPoolResults()`, compute elapsed using
   `VkPhysicalDeviceLimits::timestampPeriod`
5. Per-frame query slot reset

**Testable result:** Performance profiling timestamps work.

### 2.16 Debug Groups [DONE]

**Files:** `vulkan_debug.cpp`

**Work:**
1. `begin()`: `vkCmdBeginDebugUtilsLabelEXT()` using active command buffer
   (requires `VK_EXT_debug_utils`, already tracked)
2. `end()`: `vkCmdEndDebugUtilsLabelEXT()`
3. Set `Scoped_debug_group::s_enabled = true` during device init if extension available

**Testable result:** RenderDoc and validation layer debug markers appear.

### 2.17 Image Layout Tracking Refinement [DONE]

**Files:** `vulkan_texture.hpp`, `vulkan_texture.cpp`

After the initial implementation uses explicit transitions at render pass
boundaries, add per-texture layout tracking for correctness and to avoid
redundant barriers.

**Work:**
1. Add `VkImageLayout m_current_layout{VK_IMAGE_LAYOUT_UNDEFINED}` to Texture_impl
2. Add `transition_layout(VkCommandBuffer, VkImageLayout new_layout)` method that
   inserts `vkCmdPipelineBarrier()` only when the current layout differs
3. Update render pass begin/end and blit operations to use tracked layout

**Testable result:** Correct image transitions without redundant barriers.

---

## Implementation Order and Milestones

### Milestone 1: First Visible Output
Steps 1.1, 2.1, 2.2, 2.3, 2.4, 2.5, 2.6

At this point the swapchain render pass should display a clear-color frame and
simple solid-color geometry should draw.

### Milestone 2: Basic 3D Rendering
Steps 2.7, 2.8, 2.9

Uniform/storage buffers work, off-screen render passes enable shadow maps and
the ID buffer. Basic 3D scenes render.

### Milestone 3: Full Mesh Rendering
Steps 2.10, 2.11

Multi-draw indirect and textured materials work. The editor should be broadly
functional.

### Milestone 4: Full Feature Parity
Steps 2.12, 2.13, 2.14, 2.15, 2.16, 2.17

Blit operations, compute shaders, clear operations, GPU timing, debug markers,
and refined image layout tracking complete the backend.

---

## Critical Files Summary

| File | Role |
|------|------|
| `CMakeLists.txt` | Force ERHE_SPIRV on for Vulkan |
| `vulkan_shader_stages.cpp/hpp` | VkShaderModule creation, SPIR-V consumption |
| `vulkan_shader_stages_prototype.cpp` | Fix state machine (set ready after link) |
| `vulkan_sampler.cpp/hpp` | VkSampler creation |
| `vulkan_device.cpp/hpp` | Pipeline cache, descriptor/pipeline layout, push descriptor infrastructure |
| `vulkan_render_pass.cpp/hpp` | Active render pass tracking, non-swapchain render passes, depth/stencil |
| `vulkan_render_command_encoder.cpp/hpp` | Pipeline creation, draw commands, viewport/scissor, buffer binding |
| `vulkan_command_encoder.cpp/hpp` | Resource binding (set_buffer via push descriptors) |
| `vulkan_helpers.cpp/hpp` | All enum/format conversion functions |
| `vulkan_texture_heap.cpp/hpp` | Bindless textures via descriptor indexing |
| `vulkan_blit_command_encoder.cpp` | Copy/blit operations with layout transitions |
| `vulkan_compute_command_encoder.cpp/hpp` | Compute pipeline and dispatch |
| `vulkan_vertex_input_state.cpp/hpp` | Vertex attribute/binding descriptions |
| `vulkan_buffer.cpp` | Buffer clear |
| `vulkan_texture.cpp/hpp` | Texture clear, image layout tracking |
| `vulkan_gpu_timer.cpp/hpp` | Timestamp queries |
| `vulkan_debug.cpp` | Debug labels via VK_EXT_debug_utils |

---

## Validation Fix Log

Fixes applied while running executables and eliminating Vulkan validation layer errors.

| # | Date | Executable | VUID | Fix | File |
|---|------|-----------|------|-----|------|
| 1 | 2026-04-04 | hello_swap | VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-descriptorBindingSampledImageUpdateAfterBind-03006 | Query and enable `VkPhysicalDeviceDescriptorIndexingFeatures` in device creation feature chain (was missing entirely -- texture heap uses UPDATE_AFTER_BIND / PARTIALLY_BOUND / VARIABLE_DESCRIPTOR_COUNT flags) | `vulkan_device.cpp` |
| 2 | 2026-04-04 | hello_swap | VUID-vkDestroyDevice-device-05137 | Fix destruction order in `~Device_impl`: explicitly destroy `m_immediate_commands` (owns command pool + command buffers + fences + semaphores), `m_ring_buffers` (VMA allocations), and `m_vulkan_frame_end_semaphore` before `vkDestroyDevice` -- as `unique_ptr` members they were destroyed after the destructor body, by which time the device was already gone | `vulkan_device.cpp` |
| 3 | 2026-04-04 | example | (assert) binding_point >= max_uniform_buffer_bindings | Populate all `Device_info` limits from `VkPhysicalDeviceLimits` -- UBO/SSBO binding counts, texture sizes, sample counts, vertex attribs, buffer alignments, etc. were all left at 0 | `vulkan_device.cpp` |
| 4 | 2026-04-04 | example | (assert) binding_point >= max_uniform_buffer_bindings (-1) | Cap `uint32_t` limit values at `INT_MAX` before storing in `int` fields -- Vulkan drivers report `UINT32_MAX` for "unlimited", which wraps to -1 and breaks signed comparisons | `vulkan_device.cpp` |
| 5 | 2026-04-04 | example | VUID-VkShaderModuleCreateInfo-pCode-08740 | Query and enable `VkPhysicalDeviceVulkan11Features::shaderDrawParameters` -- SPIR-V uses `DrawParameters` capability (for `gl_DrawID`) but the feature was not enabled at device creation | `vulkan_device.cpp` |
| 6 | 2026-04-04 | example | VUID-VkImageMemoryBarrier-oldLayout-01213 | Add `transfer_dst` to dummy texture usage flags -- texture is copy destination but was created with only `sampled` | `vulkan_device.cpp` |
| 7 | 2026-04-04 | example | VUID-vkCmdCopyBufferToImage-srcBuffer-00174 | Fix `Ring_buffer_create_info` default `buffer_usage` from `0xff` to `0x03ff` -- the old mask excluded `transfer_src` (0x100) and `transfer_dst` (0x200) bits needed for copy operations | `ring_buffer.hpp` |
| 8 | 2026-04-04 | example | (assert) swapchain state `command_buffer_ready` in `begin_render_pass` | SDL redraw callback called `tick()` during `poll_events()` before `begin_frame()` -- add `m_in_tick` / `m_first_frame_rendered` guards across all executables to prevent re-entrant or premature rendering from the callback | `example.cpp`, `rendering_test.cpp`, `hextiles.cpp`, `editor.cpp` |
| 9 | 2026-04-04 | example | VUID-VkWriteDescriptorSet-descriptorType-00319 | Descriptor type mismatch: global layout assumed all bindings 0-7 are UBO, but some are SSBO. Introduced `Bind_group_layout` -- explicit per-program descriptor layout that replaces the hardcoded global one. Added `set_bind_group_layout()` to render command encoder. Applied to all renderers (scene, imgui, text, debug, post_processing, tile, rendering_test). Debug builds verify ordering and compatibility. | `bind_group_layout.*`, `vulkan_render_command_encoder.cpp`, `program_interface.cpp`, many renderers |
| 10 | 2026-04-04 | example | VUID-VkDescriptorImageInfo-imageView-01976 | Depth/stencil image view had both DEPTH and STENCIL aspects but descriptors require only one. Added `Texture_impl::get_vk_image_view(aspect, layer, count)` with caching -- texture heap now uses depth-only view for depth/stencil textures | `vulkan_texture.cpp/hpp`, `vulkan_texture_heap.cpp` |
| 11 | 2026-04-04 | example | VUID-VkPipelineInputAssemblyStateCreateInfo-topology-06252 | `primitiveRestartEnable` was true for triangle list topology -- erhe static presets all set `primitive_restart=true` (fine for GL). Vulkan pipeline creation now forces it to false for non-strip/fan topologies | `vulkan_render_command_encoder.cpp` |
| 12 | 2026-04-04 | example | VUID-VkGraphicsPipelineCreateInfo-layout-07990 | Sampler/buffer binding namespace collision and s_texture[64] descriptor count mismatch. Replaced `use_bindless_texture` bool with `Texture_heap_path` enum. Vulkan uses `vulkan_descriptor_indexing`: material textures via `erhe_texture_heap[]` at set 1, shadow samplers as assigned push descriptors at offset set 0. Eliminated s_texture[] array for Vulkan. | `device.hpp`, `vulkan_device.cpp`, `shader_stages_create_info.cpp`, GLSL shaders, all renderers |
| 13 | 2026-04-04 | example | VUID-VkSamplerCreateInfo-anisotropyEnable-01070 | Copy queried `VkPhysicalDeviceFeatures` (including `samplerAnisotropy`) to device creation -- base features struct was zero-initialized, not copied from query | `vulkan_device.cpp` |
| 14 | 2026-04-05 | example | (visual) gl_DrawID always 0 | `#define ERHE_DRAW_ID gl_DrawID` mapped to built-in `BuiltIn(DrawIndex)` which is always 0 for individual `vkCmdDrawIndexedIndirect` calls in emulation loop. Replaced with `layout(push_constant) uniform Erhe_draw_id_block { int ERHE_DRAW_ID; };` matching Metal pattern. Also added `gl_VertexIndex`/`gl_InstanceIndex` defines for Vulkan SPIR-V. | `shader_stages_create_info.cpp` |
| 15 | 2026-04-05 | example | (improvement) multiDrawIndirect detection | Hardcoded `use_multi_draw_indirect_core = true`. Now queries `VkPhysicalDeviceFeatures::multiDrawIndirect` and `VkPhysicalDeviceVulkan11Features::shaderDrawParameters`. When both supported, uses native `vkCmdDrawIndexedIndirect` with drawcount > 1 and `gl_DrawID` built-in. Otherwise falls back to per-draw push constant emulation. Pipeline layout push constant range conditional on emulation. | `vulkan_device.cpp`, `vulkan_render_command_encoder.cpp`, `vulkan_bind_group_layout.cpp`, `shader_stages_create_info.cpp` |
| 16 | 2026-04-05 | example | (visual) textures missing rows | `VkBufferImageCopy::bufferRowLength` and `bufferImageHeight` were set to bytes, but Vulkan spec requires texels and rows respectively. For RGBA8, this made Vulkan interpret rows as 4x wider, reading every 4th row. Fixed by dividing by `get_format_size_bytes()` for row length and by `bytes_per_row` for image height. | `vulkan_blit_command_encoder.cpp` |
| 17 | 2026-04-05 | example | (visual) no depth test | Swapchain render pass had no depth attachment -- only a single color attachment with `pDepthStencilAttachment = nullptr`. Created VMA-backed D32_SFLOAT depth image in `Swapchain_impl`, added as attachment 1 to render pass and framebuffer. Added `EARLY_FRAGMENT_TESTS` stage and `DEPTH_STENCIL_ATTACHMENT_WRITE` to subpass dependency. Depth image recreated on swapchain resize. | `vulkan_swapchain.cpp/hpp`, `vulkan_render_pass.cpp`, `example.cpp` |
| 18 | 2026-04-05 | example | (visual) dark colors | Surface format scoring preferred `A2R10G10B10_UNORM` (score 4.0) over `B8G8R8A8_SRGB` (score 3.0). UNORM format with `SRGB_NONLINEAR` color space means linear shader output is displayed without sRGB transfer, appearing dark. Rewrote `get_surface_format_score()` to score format+colorSpace as a pair: `_SRGB` + `SRGB_NONLINEAR` is the only correct SDR combination (score 160); `_UNORM` + `SRGB_NONLINEAR` penalized to last-resort (score 1); `_SFLOAT` + `EXTENDED_SRGB_LINEAR` valid for HDR. Uses `Window_configuration::color_bit_depth` and `Surface_create_info::prefer_high_dynamic_range`. | `vulkan_surface.cpp` |
| 19 | 2026-04-05 | example | (leak) VMA texture allocations | `Texture_impl::~Texture_impl()` destroyed image views but never freed VkImage or VmaAllocation. Added `vmaDestroyImage()` via completion handler (same pattern as `Buffer_impl`). Fixed 7 leaked texture allocations (5 material + 2 fallback). | `vulkan_texture.cpp` |
| 20 | 2026-04-05 | example | (leak) VMA ring buffer allocation | `m_ring_buffers.clear()` was called after the completion handler loop in `~Device_impl`. `Buffer_impl` destructors add VMA deallocation as completion handlers, so the ring buffer's handler was queued but never executed. Moved `m_ring_buffers.clear()` before the completion handler loop. | `vulkan_device.cpp` |
| 21 | 2026-04-05 | hextiles | (assert) add_sampler array_size sanity check | `uses_bindless_texture()` returned false for Vulkan descriptor indexing, causing imgui renderer to create `s_textures[UINT_MAX]` sampler array. Replaced with `uses_sampler_array_in_set_0()` in imgui renderer for default uniform block, shader creation, dummy array texture, and texture heap slot reservation. | `imgui_renderer.cpp` |
| 22 | 2026-04-05 | hextiles | (shader) imgui fragment shader missing Vulkan path | Non-bindless `#else` branch referenced undeclared `s_texture_array` and `s_textures[]`. Added `#elif defined(ERHE_VULKAN_DESCRIPTOR_INDEXING)` path sampling from `erhe_texture_heap[v_texture_id]`. | `imgui_renderer.cpp` |
| 23 | 2026-04-05 | hextiles | VUID-VkShaderModuleCreateInfo-pCode-08740 | SPIR-V `ClipDistance` capability requires `shaderClipDistance` feature. Enabled `shaderClipDistance`, `shaderCullDistance`, and `vertexPipelineStoresAndAtomics` at device creation with validation for required features. | `vulkan_device_init.cpp` |
| 24 | 2026-04-05 | hextiles | (assert) m_bind_group_layout nullptr | ImGui renderer and hextiles tile renderer did not call `set_bind_group_layout()` before `set_render_pipeline_state()`. Added missing calls, plus `set_viewport_rect`/`set_scissor_rect` for Vulkan dynamic state. | `imgui_renderer.cpp`, `tile_renderer.cpp` |
| 25 | 2026-04-05 | hextiles | VUID-VkGraphicsPipelineCreateInfo-layout-07988 | Tile shader `s_texture` sampler declared in set 0 but not in pipeline layout. Same `uses_bindless_texture()` issue as #21. Replaced with `uses_sampler_array_in_set_0()` in tile renderer. Added `ERHE_VULKAN_DESCRIPTOR_INDEXING` paths to tile.vert/tile.frag for `erhe_texture_heap[]` sampling. | `tile_renderer.cpp`, `tile.vert`, `tile.frag` |
| 26 | 2026-04-05 | hextiles | VUID-vkCmdEndRenderPass-commandBuffer-recording | Tile renderer created a local `Texture_heap` on the stack whose descriptor set was destroyed before `vkCmdEndRenderPass`. Deferred `Texture_heap_impl` descriptor pool and set layout destruction via `Device_impl::add_completion_handler()`, matching `Texture_impl` and `Buffer_impl` patterns. | `vulkan_texture_heap.cpp` |
