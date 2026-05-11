# Vulkan Graphics Backend

> This Vulkan backend was substantially created by Claude (Anthropic),
> with architectural guidance and review from Timo Suoranta.

## Status

The Vulkan backend is feature-complete for the editor, example, hextiles and
rendering_test executables on macOS (MoltenVK) and runs cleanly under the
validation layer with the standard core features enabled. All steps marked
[DONE] in Phase 2 below have been implemented; the Phase 3 cross-backend
improvements (set_sampled_image, Sampler_aspect annotation, explicit
texture-heap classification, depth/stencil multisample resolve via
vkCreateRenderPass2) are also in place.

This document is maintained as a historical record of the implementation
plan plus an ongoing log of validation fixes; new fixes are appended to the
Validation Fix Log near the end.

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
if (${ERHE_GRAPHICS_API} STREQUAL "metal" OR ${ERHE_GRAPHICS_API} STREQUAL "vulkan")
    set(ERHE_SPIRV "ON" CACHE STRING "Enable SPIR-V" FORCE)
endif ()
```

This ensures glslang is fetched and linked, and `ERHE_SPIRV` is defined.

### 1.2 Verify editor GL calls are guarded (no changes needed)

All direct `gl::` calls in `src/editor/` are already wrapped with
`#if defined(ERHE_GRAPHICS_API_OPENGL)`:

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
| Resource destruction | Deferred via completion handler | GPU may still reference resources after C++ destructor runs |

### Vulkan Resource Destruction Pattern

Vulkan resources (buffers, textures, samplers, descriptor sets, pipeline layouts, etc.)
may still be referenced by in-flight command buffers when the owning C++ object is
destroyed. Immediate destruction causes validation errors or crashes.

**Rule:** In `~Impl()` destructors, capture Vulkan handles by value into a lambda and
defer destruction via `Device_impl::add_completion_handler()`. The handlers run during
`~Device_impl()` after `vkDeviceWaitIdle()`, guaranteeing the GPU is idle.

```cpp
Foo_impl::~Foo_impl() noexcept
{
    // 1. Capture handles by value
    const VkFoo    vk_foo    = m_vk_foo;
    const VkBar    vk_bar    = m_vk_bar;
    m_vk_foo = VK_NULL_HANDLE;
    m_vk_bar = VK_NULL_HANDLE;

    // 2. Defer destruction -- device_impl is captured by reference
    //    (it outlives all completion handlers)
    m_device_impl.add_completion_handler(
        [&device_impl = m_device_impl, vk_foo, vk_bar]() {
            VkDevice device = device_impl.get_vulkan_device();
            if (vk_foo != VK_NULL_HANDLE) vkDestroyFoo(device, vk_foo, nullptr);
            if (vk_bar != VK_NULL_HANDLE) vkDestroyBar(device, vk_bar, nullptr);
        }
    );
}
```

**Exceptions** (immediate destruction is safe):
- `~Device_impl()` itself -- calls `vkDeviceWaitIdle()` before destroying device-level resources
- `~Render_pass_impl()` -- calls `vkDeviceWaitIdle()` before destroying framebuffer/render pass
- `~Swapchain_impl()` -- uses fence-based frame-in-flight lifecycle tracking
- Staging buffers -- destroyed after `m_immediate_commands->wait(submit)` completes
- `VkShaderModule` -- only needed during `vkCreateGraphicsPipelines()`, not referenced after

**Currently using completion handlers:**
`Buffer_impl`, `Texture_impl`, `Sampler_impl`, `Bind_group_layout_impl`,
`Texture_heap_impl`, `Surface_impl`, compute pipelines.

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
| 27 | 2026-04-05 | all | (safety) deferred Vulkan resource destruction audit | Audited all `vkDestroy*`/`vmaDestroy*` calls across the Vulkan backend. `Bind_group_layout_impl` (pipeline layout + descriptor set layout), `Sampler_impl` (VkSampler), and `Texture_impl` image views were destroyed immediately but may be referenced by in-flight command buffers. Deferred all via `Device_impl::add_completion_handler()`. Texture image views consolidated into single handler with VkImage/VmaAllocation. Shader modules left immediate (only needed at pipeline creation). | `vulkan_bind_group_layout.cpp`, `vulkan_sampler.cpp`, `vulkan_texture.cpp` |
| 28 | 2026-04-05 | rendering_test | VUID-VkShaderModuleCreateInfo-pCode-08740 | SPIR-V `DemoteToHelperInvocation` capability (used by wide line compute shader) requires `shaderDemoteToHelperInvocation` feature. Added to `VkPhysicalDeviceVulkan13Features` query and enable chain. | `vulkan_device_init.cpp` |
| 29 | 2026-04-05 | rendering_test | SYNC-HAZARD-WRITE-AFTER-WRITE | Off-screen render pass subpass dependencies only listed `EARLY_FRAGMENT_TESTS` but depth writes happen at `LATE_FRAGMENT_TESTS`. Added `LATE_FRAGMENT_TESTS` to both EXTERNAL->0 and 0->EXTERNAL dependencies. | `vulkan_render_pass.cpp` |
| 30 | 2026-04-05 | rendering_test | VUID-VkComputePipelineCreateInfo-layout-07990 | Compute pipeline used global device pipeline layout (all UBOs) but wide line compute shader uses SSBOs. Added `set_bind_group_layout()` to `Compute_command_encoder` (all backends) so the correct per-program pipeline layout is used. | `vulkan_compute_command_encoder.cpp/hpp`, `compute_command_encoder.cpp/hpp`, all backend compute encoders, `content_wide_line_renderer.cpp`, `debug_renderer.cpp` |
| 31 | 2026-04-05 | rendering_test | (assert) bind group layout mismatch | Multiple renderers (wide line graphics, quad, multi-tex, sep-tex) missing `set_bind_group_layout()` before `set_render_pipeline_state()`. Added missing calls. | `content_wide_line_renderer.cpp`, `rendering_test.cpp` |
| 32 | 2026-04-05 | rendering_test | VUID-VkGraphicsPipelineCreateInfo-layout-07988 | `s_textures[]` sampler array declared in shaders for Vulkan where it shouldn't be. Replaced `uses_bindless_texture()` with `uses_sampler_array_in_set_0()` in rendering_test. Added `ERHE_VULKAN_DESCRIPTOR_INDEXING` paths to textured_quad.frag, multi_texture_test.frag, separate_samplers_test.frag. | `rendering_test.cpp`, test shaders |
| 33 | 2026-04-05 | rendering_test | VUID-VkWriteDescriptorSet-dstBinding-00315 | Texture heap pushed sampler descriptors to set 0 at offset binding even when the bind group layout had no sampler bindings. Added `has_sampler_bindings()` query to `Bind_group_layout_impl` and gated the push descriptor block. | `vulkan_bind_group_layout.cpp/hpp`, `vulkan_texture_heap.cpp` |
| 34 | 2026-04-05 | rendering_test | (visual) all cells red | `Device::get_handle()` returns 0 for Vulkan, so all texture UBO handles pointed to descriptor index 0. Changed rendering_test to use `texture_heap.assign()` return values as handles (actual descriptor indices). | `rendering_test.cpp` |
| 35 | 2026-04-05 | rendering_test | (visual) no debug groups in RenderDoc | `Scoped_debug_group::s_enabled` was never set to true for Vulkan. Enabled when `VK_EXT_debug_utils` messenger is created. | `vulkan_device_init.cpp` |
| 36 | 2026-04-05 | rendering_test | (visual) no wide line outlines | Compute encoder used `Render_pass_impl::get_active_command_buffer()` which is null outside render passes. Implemented standalone command buffer via immediate commands: `get_command_buffer()` falls back to acquiring an immediate command buffer, destructor submits and waits. Also implemented `set_buffer()` with push descriptors for compute bind point. | `vulkan_compute_command_encoder.cpp/hpp` |
| 37 | 2026-04-05 | editor | (shader) textured.frag missing Vulkan path | Editor and hextiles `textured.frag` referenced `s_texture[]` in the non-bindless `#else` branch. Added `#elif ERHE_VULKAN_DESCRIPTOR_INDEXING` paths using `erhe_texture_heap[]`. | `editor/res/shaders/textured.frag`, `hextiles/res/shaders/textured.frag` |
| 38 | 2026-04-05 | editor | (assert) compile_shaders/link_program state | Vulkan shader prototype constructor compiles and links eagerly, but editor's `load_programs()` calls `compile_shaders()` and `link_program()` again. Changed both to return early when already completed instead of asserting. | `vulkan_shader_stages_prototype.cpp` |
| 39 | 2026-04-05 | editor | VUID-VkShaderModuleCreateInfo-pCode-08740 | `sampleRateShading` feature needed for `gl_SampleID` in `circular_brushed_metal.frag` and `depth_only.frag`. `geometryShader` feature needed for `gl_PrimitiveID` in `id.frag` (SPIR-V requires Geometry capability even without geometry shader stage). Enabled both. | `vulkan_device_init.cpp` |
| 40 | 2026-04-05 | editor | VUID-VkShaderModuleCreateInfo-pCode-08740 | Geometry shader `.geom` files auto-discovered by `Program_interface::make_prototype()`. Skipped with `#if !defined(ERHE_GRAPHICS_API_VULKAN)`. | `program_interface.cpp` |
| 41 | 2026-04-05 | editor | VUID-VkImageViewCreateInfo-imageViewType-04973 | Thumbnails texture created as `texture_2d` with 200 array layers but view type was `VK_IMAGE_VIEW_TYPE_2D`. Auto-select `_ARRAY` variant in `to_vk_image_view_type()` when `array_layer_count > 1`. | `vulkan_texture.cpp` |
| 42 | 2026-04-05 | editor | VUID-VkBufferImageCopy-bufferRowLength-09101 | Dummy texture creation hardcoded `src_bytes_per_row = 2*4` regardless of format. For `format_16_vec4_float` (8 bytes/pixel), `bufferRowLength` computed as 1 texel < width 2. Fixed to use `get_format_size_bytes()`. | `vulkan_device.cpp` |
| 43 | 2026-04-05 | editor | VUID-VkImageMemoryBarrier-oldLayout-01213 | Shadowmap and scene preview textures missing `transfer_dst` usage flag for `clear_texture()`. Added to shadow_render_node.cpp and scene_preview.cpp. | `shadow_render_node.cpp`, `scene_preview.cpp` |
| 44 | 2026-04-05 | editor | VUID-vkCmdClearColorImage-image-00007 | `clear_texture()` used `vkCmdClearColorImage` for depth/stencil formats. Added format detection and `vkCmdClearDepthStencilImage` path. | `vulkan_device.cpp` |
| 45 | 2026-04-05 | editor | VUID-VkFramebufferCreateInfo-pAttachments-00883 | Framebuffer attachments had multi-level image views (10 mip levels). Added `get_vk_image_view()` overload with `base_level`/`level_count` params. Off-screen render pass now requests single-level views for framebuffer attachments. | `vulkan_texture.cpp/hpp`, `vulkan_render_pass.cpp` |
| 46 | 2026-04-05 | editor | VUID-vkCmdDrawIndexedIndirect-None-02721 | Vertex attribute stride 22 not aligned to float component size (4). Added per-attribute offset alignment and stride padding to `Vertex_stream` based on `get_component_byte_size()`. | `vertex_format.cpp/hpp`, `gltf_fastgltf.cpp` |
| 47 | 2026-04-05 | editor | VUID-vkCmdDrawIndexedIndirect-viewType-07752 | Dummy shadowmap in scene preview created as `texture_2d` but shader uses `sampler2DArrayShadow`. Changed to `texture_2d_array`. | `scene_preview.cpp` |
| 48 | 2026-04-05 | editor | VUID-VkImageMemoryBarrier-oldLayout-01213 | Thumbnails color texture missing `transfer_src`/`transfer_dst` usage for mipmap generation blit. | `thumbnails.cpp` |
| 49 | 2026-04-05 | editor | VUID-VkFramebufferCreateInfo-pAttachments-00881 | MSAA texture sample count (2) didn't match render pass attachment (1). `get_sample_count()` was a TODO stub returning 1. Implemented to read sample count from attachment textures. Also matched `rasterizationSamples` in pipeline multisample state from active render pass. | `vulkan_render_pass.cpp`, `vulkan_render_command_encoder.cpp` |
| 50 | 2026-04-05 | editor | VUID-vkCmdDrawIndexedIndirect-viewType-07752 | Shadowmap texture created as `texture_2d` but shader uses `sampler2DArrayShadow`. Changed to `texture_2d_array`. Reverted auto-select in `to_vk_image_view_type()` -- callers must request explicit type. | `shadow_render_node.cpp`, `thumbnails.cpp`, `vulkan_texture.cpp` |
| 51 | 2026-04-05 | editor | VUID-VkPipelineRasterizationStateCreateInfo-depthClampEnable-00782 | Shadow rendering uses depth clamp. Enabled `depthClamp` device feature. | `vulkan_device_init.cpp` |
| 52 | 2026-04-05 | editor | (assert) bind group layout mismatch | Debug renderer bucket, post-processing, text renderer missing `set_bind_group_layout()` before draw/pipeline calls. | `debug_renderer_bucket.cpp`, `post_processing.cpp`, `text_renderer.cpp` |
| 53 | 2026-04-05 | editor | VUID-VkGraphicsPipelineCreateInfo-layout-07988 | Post-processing `s_input`/`s_downsample`/`s_upsample` samplers and text renderer `s_texture` declared in default uniform block for Vulkan. Replaced `uses_bindless_texture()` with `uses_sampler_array_in_set_0()`. Added `ERHE_VULKAN_DESCRIPTOR_INDEXING` shader paths (SOURCE defines, text.vert/frag, upsample.frag). | `post_processing.cpp`, `text_renderer.cpp`, text/upsample shaders |
| 54 | 2026-04-05 | editor | (visual) pink viewport | MSAA resolve (`pResolveAttachments`) was always nullptr in off-screen render passes. Implemented resolve attachment descriptions and references for color attachments with `resolve_texture`. | `vulkan_render_pass.cpp` |
| 55 | 2026-04-05 | editor | (visual) missing texture views | Vulkan `use_texture_view` was false. Implemented texture views: when `view_source` is set, share the source's VkImage and create a VkImageView with specified subresource range. `m_is_view` flag prevents destroying the shared VkImage. | `vulkan_texture.cpp/hpp`, `vulkan_device_init.cpp` |
| 56 | 2026-04-05 | editor | VUID-vkCmdDraw-None-08600 | Text renderer's `Texture_heap` was created without a `bind_group_layout`, falling back to `Device_impl::get_pipeline_layout()` -- a different VkPipelineLayout handle. Push descriptors at set 0 with one layout invalidated set 1 bound with a different layout. Removed `Device_impl::m_pipeline_layout` entirely. All descriptor operations now use the `Bind_group_layout`'s pipeline layout. Fixed text renderer to pass `&m_bind_group_layout` to Texture_heap. | `vulkan_device.cpp/hpp`, `vulkan_device_init.cpp`, `vulkan_render_command_encoder.cpp`, `vulkan_compute_command_encoder.cpp`, `vulkan_texture_heap.cpp`, `text_renderer.cpp` |
| 57 | 2026-04-05 | editor | (visual) depth/stencil not cleared | Off-screen render pass hardcoded `stencilLoadOp = DONT_CARE` in depth attachment description, ignoring `stencil_attachment.load_action = Clear`. Now uses stencil attachment's load/store actions when depth and stencil share the same texture. | `vulkan_render_pass.cpp` |
| 58 | 2026-04-05 | editor | VUID-VkGraphicsPipelineCreateInfo-Input-07904 | `visualize_depth` shader is a fullscreen triangle with no vertex inputs, but `make_prototype()` always injected the standard vertex format. Added `no_vertex_input` flag to `Shader_stages_create_info`; when set, `make_prototype()` skips vertex format assignment. | `shader_stages.hpp`, `program_interface.cpp`, `programs.cpp` |
| 59 | 2026-04-05 | editor | (visual) no shadows | `shadow_light_count` clamped to 0 because `Format_properties::texture_2d_array_max_layers` was not populated in Vulkan `get_format_properties()`. Added `vkGetPhysicalDeviceImageFormatProperties2` query to populate max extent, max array layers, and sample counts. | `vulkan_device.cpp` |
| 60 | 2026-04-05 | editor | (visual) no shadows | Shadow renderer used `light_projection_transform->index` (global light index) for render pass lookup, but render passes are indexed by shadow-casting light order. Added `shadow_index` field to `Light_projection_transforms` counting only shadow casters. | `light.hpp`, `light_buffer.cpp`, `shadow_renderer.cpp` |
| 61 | 2026-04-05 | editor | (assert) bind group layout / viewport | Shadow renderer missing `set_bind_group_layout()` and `set_viewport_rect()`. Text renderer missing `set_bind_group_layout()` and `s_texture` sampler declared for Vulkan. Post-processing missing bind group layout/viewport/scissor. | `shadow_renderer.cpp/hpp`, `text_renderer.cpp`, text shaders, `post_processing.cpp`, `debug_renderer_bucket.cpp` |
| 62 | 2026-04-05 | editor | (visual) brush preview all same layer | Texture view's `get_vk_image_view()` created cached views with raw `base_layer`/`base_level` without adding the view's own offset. All thumbnail framebuffers targeted layer 0. Stored `m_view_base_array_layer`/`m_view_base_level` in `Texture_impl`, applied as offset in `get_vk_image_view()`. | `vulkan_texture.cpp/hpp` |
| 63 | 2026-04-05 | editor | (visual) brush preview sampler2D vs 2D_ARRAY | Thumbnail texture views inherited `texture_2d_array` type from base texture but `erhe_texture_heap[]` declares `sampler2D`. VkImageViewType mismatch caused GPU to ignore `baseArrayLayer`. Changed thumbnail views to `texture_2d` type. | `thumbnails.cpp` |
| 64 | 2026-04-05 | editor | (visual) mipmap only layer 0 | `generate_mipmaps()` hardcoded `baseArrayLayer = 0` in all barriers and blit regions. For texture views targeting layer N, mipmaps were generated for layer 0 only. Added `get_view_base_array_layer()` getter, used as offset in all subresource ranges. | `vulkan_texture.cpp/hpp`, `vulkan_blit_command_encoder.cpp` |
| 65 | 2026-04-05 | editor | (pipeline fail) depth_only | `VkPipelineColorBlendStateCreateInfo::attachmentCount` hardcoded to 1. Shadow render passes have 0 color attachments, causing pipeline creation failure. Added `get_color_attachment_count()` to `Render_pass_impl`, used in pipeline creation. | `vulkan_render_pass.cpp/hpp`, `vulkan_render_command_encoder.cpp` |
| 66 | 2026-04-05 | editor | (warning) ShaderOutputNotConsumed | depth_only fragment shader declared `out_color` at location 0 (via shared `fragment_outputs`) but shadow render pass has no color attachment. Made `make_prototype()` respect caller-provided `fragment_outputs`. Shadow renderer passes empty `Fragment_outputs`. | `program_interface.cpp`, `shadow_renderer.cpp/hpp` |
| 67 | 2026-04-05 | editor | VUID-vkCmdDraw-None-09600 | Shadow map descriptor written with `SHADER_READ_ONLY_OPTIMAL` but actual layout is `DEPTH_STENCIL_READ_ONLY_OPTIMAL`. Texture heap `assign()` and `bind()` now use `DEPTH_STENCIL_READ_ONLY_OPTIMAL` for depth textures. `clear_texture()` also transitions depth textures to this layout. | `vulkan_texture_heap.cpp`, `vulkan_device.cpp` |
| 68 | 2026-04-05 | editor | VUID-vkCmdDraw-None-09600 | Unused shadow map array layers (beyond active shadow casters) remained in `UNDEFINED` layout. Removed conditional on `resolution <= 1` so all layers are always cleared after texture creation. | `shadow_render_node.cpp` |
| 69 | 2026-04-05 | editor | (visual) thumbnail mipmap glitch | `generate_mipmaps()` read stale `m_current_layout` (UNDEFINED) because render pass never updated texture's tracked layout. Added layout tracking in `end_render_pass()`: color attachments set to `SHADER_READ_ONLY_OPTIMAL`, depth to `DEPTH_STENCIL_READ_ONLY_OPTIMAL`, matching `finalLayout`. | `vulkan_render_pass.cpp` |
| 70 | 2026-04-06 | editor | (validation) clear_value vs load_op mismatch | Off-screen render passes counted color clear values for non-`Clear` load actions (e.g. `Load`/`Dont_care`), tripping `BestPractices-vkCmdBeginRenderPass-clearvalues-not-needed`. Now `clearValueCount` is set to the highest attachment index whose load action is `Clear`, and unused entries are zero-padded. Added `make_canonical_subpass_dependencies()` shared between swapchain and off-screen so the validator's strict dependency-array compatibility check passes. | `vulkan_render_pass.cpp` |
| 71 | 2026-04-06 | editor | (perf) MSAA samples written to memory | MSAA color/depth/stencil attachments coerced their LOAD/STORE ops to `DONT_CARE` so per-sample data can stay in tile/lazy memory on tiler GPUs. Resolve attachments stay `STORE`. | `vulkan_render_pass.cpp` |
| 72 | 2026-04-06 | editor | (validation) format probe noise | Skipped `vkGetPhysicalDeviceImageFormatProperties2` probes for formats whose `optimalTilingFeatures` already say the requested attachment usage is unsupported, eliminating `BestPractices` noise from `VK_ERROR_FORMAT_NOT_SUPPORTED`. | `vulkan_device.cpp` |
| 73 | 2026-04-06 | editor | (perf) UNDEFINED -> READ_ONLY pre-transition | Off-screen render passes used `VK_IMAGE_LAYOUT_UNDEFINED` as the attachment `initialLayout` when the texture was freshly created and skipped the wasteful pre-transition; the render pass drives its own discarding transition instead. | `vulkan_render_pass.cpp`, `vulkan_texture.cpp` |
| 74 | 2026-04-06 | editor | (correctness) stencil pipeline cache aliasing | Pipeline cache key now hashes `stencil_front`/`stencil_back` so two pipelines that differ only in stencil ops/reference no longer alias to the same `VkPipeline`. | `vulkan_render_command_encoder.cpp` |
| 75 | 2026-04-06 | editor | (leak) VkShaderModule on shutdown | Added `~Shader_stages_impl()` that calls `destroy_modules()` so VkShaderModule handles do not leak on shutdown. | `vulkan_shader_stages.cpp` |
| 76 | 2026-04-06 | editor | VUID-VkImageMemoryBarrier-oldLayout-01213 | Replaced bare `UNDEFINED -> SHADER_READ_ONLY` transition for the Light_buffer fallback shadow texture with a `clear_texture()` call and added `depth_stencil_attachment` usage so the `DEPTH_STENCIL_READ_ONLY_OPTIMAL` layout is valid. | `light_buffer.cpp` |
| 77 | 2026-04-06 | editor | (correctness) pipeline format match | Pass the active render pass into `Forward_renderer` from `rendering_test` so `Lazy_render_pipeline` picks the matching pipeline variant per render target format combination. | `rendering_test.cpp`, `forward_renderer.cpp` |
| 78 | 2026-04-06 | editor | (validation) gl_PrimitiveID dependency | Compute `v_primitive_id` in `id.vert` (`gl_VertexID/3`) and read it in `id.frag`, removing the SPIR-V Geometry capability dependency that reading `gl_PrimitiveID` in a fragment shader pulls in. | `id.vert`, `id.frag` |
| 79 | 2026-04-08 | editor | (correctness) nested render pass detection | `Render_pass_impl::start_render_pass()` now asserts no other render pass is currently active, catching driver-side mismatches early. Vulkan device init also requires `synchronization2` so the simplified barrier API is available. | `vulkan_render_pass.cpp`, `vulkan_device_init.cpp` |
| 80 | 2026-04-08 | editor | (debug) image layout trace log | Added optional layout-transition tracing to `logs/vulkan.txt` so layout-related validation errors can be debugged offline against a recorded transition history. | `vulkan_texture.cpp` |
| 81 | 2026-04-08 | editor | (perf/cleanup) shared usage mapping + SPIR-V cache | Centralised the `Image_usage_flag_bit_mask -> VkImageUsageFlags` translation in one helper instead of duplicating it across `vulkan_texture.cpp` / `vulkan_render_pass.cpp`. Added a SPIR-V binary cache keyed by GLSL source hash so shaders are not reparsed on every run. | `vulkan_helpers.cpp`, `vulkan_shader_stages_prototype.cpp` |
| 82 | 2026-04-08 | editor | (perf) compute pipeline state per frame | Pre-create compute pipeline states (e.g. content wide line renderer's compute stage) instead of recreating them per frame. | `content_wide_line_renderer.cpp` |
| 83 | 2026-04-08 | editor | (perf) framebuffer cache | Cache framebuffers per (render-pass, attachment-view) tuple instead of recreating one each frame. Saves a noticeable amount of CPU on viewport-heavy frames. | `vulkan_render_pass.cpp` |
| 84 | 2026-04-08 | editor | (correctness) usage_before/usage_after on attachments | `Render_pass_attachment_descriptor` gained explicit `usage_before` / `usage_after` fields. Render passes derive subpass dependencies from these instead of guessing from texture state, fixing several SYNC-HAZARD validation errors and removing the need for the layout-tracker workaround in mixed sampled/attachment workflows. | `render_pass.hpp`, all renderers, `vulkan_render_pass.cpp`, `metal_render_pass.cpp` |
| 85 | 2026-04-09 | editor | VUID-VkDescriptorImageInfo-imageView-01976 | Sampled depth/stencil image views were created with both `DEPTH | STENCIL` aspects, which validation rejects for `COMBINED_IMAGE_SAMPLER` descriptors. Removed the no-arg `Texture_impl::get_vk_image_view()` overload entirely; callers must request a specific aspect. `Texture_heap` writes color textures with `VK_IMAGE_ASPECT_COLOR_BIT` only. | `vulkan_texture.cpp/hpp`, `vulkan_texture_heap.cpp` |
| 86 | 2026-04-09 | editor | (assert) Light_buffer wrote depth shadow into bindless heap | `Light_buffer::update` was calling `texture_heap.assign(slot, shadow_texture, ...)` for the depth shadow map. The bindless heap is color-only (see #85), and slot 0/1 of the heap was a leaky abstraction for "first reserved sampler binding". Migrated `Light_buffer` to bind shadow samplers via the new `Render_command_encoder::set_sampled_image()` API; `c_texture_heap_slot_shadow_*` constants were dropped from the renderer. | `light_buffer.cpp/hpp`, `program_interface.cpp` |
| 87 | 2026-04-09 | all | (API) Render_command_encoder::set_sampled_image | Added a new explicit per-binding sampler API to all backends. The Vulkan implementation pushes a `VkDescriptorImageInfo` via `vkCmdPushDescriptorSetKHR`, reading the image aspect (`color`/`depth`/`stencil`) from the matching `Bind_group_layout` binding. The Metal and OpenGL implementations bind the texture/sampler at the matching slot. This replaces the texture-heap dual-path that used to handle "named samplers". | `render_command_encoder.hpp/.cpp` + per-backend impls, `bind_group_layout.hpp` (combined_image_sampler `sampler_aspect` field) |
| 88 | 2026-04-09 | all | (API) Sampler_aspect annotation on add_sampler | `Shader_resource::add_sampler()` and the matching constructor now take a `Sampler_aspect sampler_aspect` parameter (`color`/`depth`/`stencil`). The aspect is consumed by `Texture_heap` (so it can pick the right `VkImageView` aspect mask without inspecting texture formats) and by the bind group layout binding emitter (so `set_sampled_image` knows which `VkImageLayout` to use). | `shader_resource.hpp/.cpp`, all `add_sampler` callsites |
| 89 | 2026-04-09 | all | (API) explicit is_texture_heap flag | Added a `bool is_texture_heap` parameter to `add_sampler`. Texture-heap samplers (e.g. `s_textures[64]`, `s_texture_array`) are placed in the bindless / argument-buffer path; dedicated samplers (e.g. `s_shadow_compare`, `s_input`, `s_depth`) go through the discrete `set_sampled_image` path. Previously the Metal shader emitter and texture heap classified samplers by `array_size > 1`, which mis-classified scalar dedicated samplers and arrays of size 1. The classification is now driven by the explicit flag, with an assertion that `is_texture_heap` and an explicit `dedicated_texture_unit` are mutually exclusive. | `shader_resource.hpp/.cpp`, `metal_shader_stages_prototype.cpp`, `metal_texture_heap.cpp`, all `add_sampler` callsites |
| 90 | 2026-04-09 | all | (correctness) sampler binding emitted as `binding =`, not `location =` | Auto-allocated sampler binding numbers were going into `m_location` (which produces `layout(location = N)` in GLSL — wrong for samplers) instead of `m_binding_point` (which produces `layout(binding = N)`). `add_sampler` now always synthesizes the auto-allocated value as a binding point, and the constructor's `location` parameter is hardcoded to `-1` for samplers. Fixes example/Metal compile failure of the standard shader. | `shader_resource.cpp` |
| 91 | 2026-04-09 | editor | (Vulkan) renderpass2 + depth/stencil resolve | Both swapchain and off-screen render passes now go through `vkCreateRenderPass2` instead of `vkCreateRenderPass`. The off-screen path emits `VkSubpassDescriptionDepthStencilResolve` for depth and stencil resolves so the MSAA depth-resolve cell in `rendering_test` can resolve the depth aspect into a single-sample texture and sample it as a depth target. The previous DONT_CARE coercion is lifted whenever a resolve target is provided. | `vulkan_render_pass.cpp`, `enums.hpp` (`Resolve_mode`), `render_pass.hpp` (`resolve_mode` on attachment) |
| 92 | 2026-04-09 | editor | (correctness) Render_pipeline stencil format on swapchain | `set_format_from_render_pass` swapchain branch did not pick up the stencil format, so Metal validation flagged a mismatch between the pipeline's stencil format (Invalid) and the framebuffer's. Added a stencil-attachment check parallel to the depth check. | `render_pipeline.cpp` |
| 93 | 2026-04-09 | editor | (correctness) sampled-image set 0 binding offset on Metal | `metal_bind_group_layout.cpp` now computes `m_sampler_binding_offset` from the highest buffer binding, mirroring Vulkan. The Metal `set_sampled_image` implementation translates the user-facing binding_point to the per-stage Metal slot via this offset so direct `[[texture(N)]]/[[sampler(N)]]` indices match what the SPIRV-Cross MSL output expects. | `metal_bind_group_layout.cpp/hpp`, `metal_render_command_encoder.cpp` |
| 94 | 2026-04-09 | editor | (correctness) editor outline / load actions audit | Audited every render pass attachment for `usage_before`/`usage_after` and stencil load/store ops. Fixed: editor xr/headset_view_resources stencil attachment missing usages, imgui swapchain pass silently dropping `stencil_attachment.load_action`, `gl_device.cpp` clear_texture path missing usage flags, hardcoded `.stencil = 0` clear values in Vulkan render pass. | `headset_view_resources.cpp`, `imgui_renderer.cpp`, `gl_device.cpp`, `vulkan_render_pass.cpp` |
| 95 | 2026-04-09 | editor | (cleanup) Texture_heap reduced to color-only bindless heap | After the migration in #86 / #87 the texture heap no longer needs the dual push-descriptor branch for "named samplers in reserved slots". The Vulkan implementation lost the dual path entirely and now asserts color-only. Reserved-slot handling stayed for the Metal argument buffer (where the `s_textures[N]` array still consumes a contiguous run of `[[id(N)]]` entries) but no longer includes scalar samplers, since those live in discrete set 0. | `vulkan_texture_heap.cpp/hpp`, `metal_texture_heap.cpp`, `texture_heap.hpp` |
| 96 | 2026-04-09 | rendering_test | (visual) stencil cell + sep_tex migration | Added a pure-stencil masking test in cell (1,3): clear stencil 0, draw red cube with `stencil always replace 1`, draw green sphere with `stencil test != 1`. Sphere is visible only outside the cube's silhouette. While at it, migrated the sep_tex cell to use `set_sampled_image` for `s_tex0/1/2` (post-processing pattern with a dedicated `m_sep_tex_bind_group_layout`) instead of relying on the texture heap; the texture heap is still constructed so the bindless paths get their UBO handles populated. | `rendering_test.cpp` (now split, see below), `stencil_color.vert/.frag` |
| 97 | 2026-04-09 | rendering_test | (cleanup) split rendering_test.cpp by cell | The single 2000+ line `rendering_test.cpp` was split into a class header (`rendering_test_app.hpp`) plus per-cell `.cpp` files: `rendering_test_setup.cpp` (scene + render passes + textures), `cell_scene.cpp`, `cell_textured_quad.cpp`, `cell_multi_texture.cpp`, `cell_separate_samplers.cpp`, `cell_depth_visualize.cpp`, `cell_stencil.cpp`. The trimmed `rendering_test.cpp` now hosts the constructor, run loop, event handlers, `tick()` and `run_rendering_test()`. | `src/rendering_test/*` |
| 98 | 2026-04-11 | editor | (perf) remove `vkQueueWaitIdle` per render pass | `Render_pass_impl::end_render_pass` used to CPU-stall on `vkQueueWaitIdle` after every off-screen submission, serializing all ~10 render passes per frame. Replaced with a single conservative `VkMemoryBarrier2` (`ALL_COMMANDS|MEMORY_WRITE -> ALL_COMMANDS|MEMORY_READ|WRITE`) emitted via `vkCmdPipelineBarrier2` before `vkCmdBeginRenderPass`; combined with submission order on the single graphics queue this provides correct cross-submission memory visibility without stalling. Non-swapchain command buffers now come from device-wide per-frame-in-flight pools recycled by `Device_impl::frame_completed`; the per-`Render_pass_impl` pool is gone. `Scoped_render_pass` gained optional `render_pass_before`/`render_pass_after` arguments so callers can declare dependencies explicitly (currently used for a debug-only usage-consistency check; room to derive tighter sync later). See 3.12 for the API-level narrative. | `vulkan_render_pass.cpp/hpp`, `vulkan_device.cpp/hpp`, `vulkan_device_init.cpp`, `render_pass.hpp/cpp`, `gl/metal/null_render_pass.hpp/cpp`, `post_processing.cpp` |
| 99 | 2026-04-16 | editor | (perf) gate Vulkan sync/desc traces behind `ERHE_LOG_VULKAN` | The `[FRAME_BEGIN]`/`[RP_BEGIN]`/`[DRAW]`/`[IMG_BARRIER]`/etc. trace lines emitted into `logs/vulkan.txt` are extremely high-volume (multiple per draw call) and `spdlog` still evaluates the format arguments at the call site even when the runtime level filters the line out. Wrapped every `log_sync->trace(...)` / `log_desc->trace(...)` / `log_sync->log(...)` site in `ERHE_VULKAN_SYNC_TRACE` / `ERHE_VULKAN_DESC_TRACE` / `ERHE_VULKAN_SYNC_LOG` macros that compile to `((void)0)` when `ERHE_LOG_VULKAN` is not defined in `graphics_log.hpp`. The two loggers were renamed to `log_vulkan_sync` / `log_vulkan_desc` (spdlog names `erhe.graphics.vulkan_sync` / `erhe.graphics.vulkan_desc`) so they sort with the rest of the Vulkan-only sinks. Default builds leave the define commented out -- enable it when investigating layout/sync/descriptor issues against `logs/vulkan.txt`. | `graphics_log.hpp/cpp`, `vulkan_device.cpp`, `vulkan_device_debug.cpp`, `vulkan_render_pass.cpp`, `vulkan_render_command_encoder.cpp`, `vulkan_blit_command_encoder.cpp`, `vulkan_compute_command_encoder.cpp`, `vulkan_swapchain.cpp`, `vulkan_texture.cpp`, `vulkan_texture_heap.cpp` |
| 100 | 2026-04-17 | editor | SYNC-HAZARD-WRITE-AFTER-WRITE on `post processing` parameter buffer | `Post_processing_node` owned a device-local `Buffer` and re-uploaded the per-level parameter block every frame via `upload_to_buffer` -> `vkCmdCopyBuffer`. With no barrier between frames N and N+1, sync validation flagged the second copy as WAW on the same `VkBuffer`. Migrated the parameter block to the per-device `Ring_buffer` (matching the camera / light / material buffer pattern): each frame's `update_parameters()` now acquires a `Ring_buffer_range` with `Ring_buffer_usage::CPU_write` sized `level_offset_size * level_count`, writes directly into the mapped span, `bytes_written` / `close` / `release`, and stores the range on the node. `Post_processing::post_process` resolves the underlying `Buffer*` and base offset once and binds each mipmap level at `base + source_level * level_offset_size`. The ring buffer's per-frame sync entries handle cross-frame byte recycling, so WAW disappears by construction. | `src/editor/rendergraph/post_processing.hpp/cpp` |
| 101 | 2026-04-17 | editor | SYNC-HAZARD-READ-AFTER-WRITE on `Mesh_memory edge line vertex buffer` (compute SSBO read after `vkCmdCopyBuffer`) | `Device_impl::upload_to_buffer` emitted only a `vkCmdCopyBuffer` with no post-copy barrier, so any downstream reader (vertex input, shader storage, uniform, indirect) saw a transfer write with no sync chain. The `compute_before_content_line` dispatch that reads the edge-line vertex buffer as SSBO after a mesh upload hit RAW; earlier uploads were silently hazardous too. Added a precise range-scoped `VkBufferMemoryBarrier2` after each `vkCmdCopyBuffer` (both in-frame and immediate-commands paths). `dstStageMask` and `dstAccessMask` are derived from the buffer's own `Buffer_usage` via a new helper `buffer_usage_to_vk_stage_access(Buffer_usage) -> Vk_stage_access_2` whose mapping table matches the `Device_impl::memory_barrier` dst translation (vertex -> VERTEX_ATTRIBUTE_INPUT + VERTEX_ATTRIBUTE_READ, storage -> ALL_GRAPHICS\|COMPUTE + SHADER_STORAGE_READ\|WRITE, indirect -> DRAW_INDIRECT + INDIRECT_COMMAND_READ, uniform_texel -> ALL_GRAPHICS\|COMPUTE + SHADER_SAMPLED_READ, etc.). `transfer_dst` in the usage mask covers cross-frame WAW against the next `vkCmdCopyBuffer` on the same buffer without any generic fallback. `Buffer_impl` grew a `get_usage()` accessor. Replaces the earlier temporary `ALL_COMMANDS + MEMORY_READ\|TRANSFER_WRITE` barrier so upload-path barriers no longer over-synchronize unrelated work. | `vulkan_helpers.hpp/cpp`, `vulkan_buffer.hpp/cpp`, `vulkan_device.cpp` |

---

## Phase 3 -- Cross-Backend Improvements

Improvements made to the shared API and all backends (OpenGL, Metal, Vulkan, null)
based on patterns established during the Vulkan implementation.

### 3.1 Metal Backend Fixes

| Fix | Description | Files |
|-----|-------------|-------|
| Blit encoder stubs | Implemented 6 missing Metal blit operations: texture-to-buffer (fixes ID picking crash), texture-to-texture (regions, slices, full), fill_buffer, buffer-to-buffer | `metal_blit_command_encoder.cpp` |
| Depth/stencil store action | Was hardcoded to `MTL::StoreActionStore`. Now respects `Dont_care` for bandwidth savings on Apple Silicon tiler GPUs | `metal_render_pass.cpp` |
| Sample count query | `get_sample_count()` was hardcoded to 1. Now queries attachment textures | `metal_render_pass.cpp` |
| Texture type simplification | `to_mtl_texture_type()` no longer takes `array_layer_count` -- uses `Texture_type` enum directly (`texture_2d_array`, `texture_cube_map_array`) | `metal_helpers.hpp/cpp`, `metal_texture.cpp` |
| Helper functions shared | Moved vertex format, blend, compare, stencil, cull, winding conversions from anonymous namespace in `metal_render_command_encoder.cpp` to shared `metal_helpers.hpp/cpp` | `metal_helpers.hpp/cpp`, `metal_render_command_encoder.cpp` |
| ImGui dummy array texture | Was `texture_2d` with `array_layer_count = 1` (asserts on OpenGL). Changed to `texture_2d_array` | `imgui_renderer.cpp` |

### 3.2 New `Render_pipeline` API (All Backends)

Added `Device::create_render_pipeline()` to create GPU pipeline objects up-front
instead of at draw time. Eliminates per-draw pipeline creation overhead on Metal
and hash-based cache lookups on Vulkan.

**New types:**
- `Render_pipeline_create_info` -- extends old `Render_pipeline_data` with render pass format fields (color formats, depth format, stencil format, sample count, bind group layout)
- `Render_pipeline` -- pimpl wrapping backend-specific compiled pipeline (Metal PSO + DepthStencilState, Vulkan VkPipeline, GL stores state)
- `Lazy_render_pipeline` -- stores create info without format; creates pipeline variants on first use from active render pass format. Caches by format hash.

**New methods:**
- `Device::create_render_pipeline(Render_pipeline_create_info)` -- up-front creation
- `Render_command_encoder::set_render_pipeline(Render_pipeline&)` -- bind only, no creation
- `Render_pass::get_descriptor()` -- access stored format info
- `Render_pipeline_create_info::set_format_from_render_pass()` -- populate format fields from render pass descriptor
- `Swapchain::get_color_format()` / `get_depth_format()` -- query swapchain formats for pipeline creation

**Vulkan-specific:**
- `Device_impl::get_or_create_compatible_render_pass()` -- creates lightweight VkRenderPass objects keyed by format tuple for pipeline compatibility. Destroyed in `~Device_impl()`.
- `Render_pipeline_impl` destructor defers `vkDestroyPipeline()` via completion handler

**Data format:**
- Added `format_8_vec4_bgra_srgb` to `erhe::dataformat::Format` for Metal swapchain BGRA format

**Migrated callers:** imgui_renderer, text_renderer, id_renderer, post_processing, tile_renderer, debug_renderer_bucket, shadow_renderer, forward_renderer, content_wide_line_renderer, texel_renderer, cube_renderer, app_rendering, composition_pass, tools, scene_preview, brdf_slice, depth_visualization_window, rendering_test, example

**Files added:** `render_pipeline.hpp/cpp`, `{gl,vulkan,metal,null}/xxx_render_pipeline.hpp/cpp` (10 new files)

**Files modified:** ~90 files across erhe_graphics, erhe_renderer, erhe_scene_renderer, erhe_imgui, erhe_dataformat, editor, hextiles, rendering_test, example

### 3.3 Render-pass attachment usage_before/usage_after (All Backends)

`Render_pass_attachment_descriptor` gained two new fields:

- `uint64_t usage_before` -- the texture's expected usage *before* the render pass starts
- `uint64_t usage_after`  -- the texture's expected usage *after* the render pass ends

These map directly to image layouts on Vulkan (`COLOR_ATTACHMENT_OPTIMAL` /
`SHADER_READ_ONLY_OPTIMAL` / `DEPTH_STENCIL_READ_ONLY_OPTIMAL` / `PRESENT_SRC` /
`TRANSFER_*`) and to subpass-dependency `srcStageMask`/`dstStageMask` derivation,
without needing per-texture layout tracking. The Metal backend uses them to
choose `MTLLoadAction`/`MTLStoreAction` defaults consistently. They replace the
implicit "the renderer knows what came before" rules that were tripping
SYNC-HAZARD validation errors. Every renderer that builds a render pass now
sets explicit usages (see fix log #84 / #94).

### 3.4 Render-pass MSAA depth/stencil resolve (Vulkan + Metal)

Added `Resolve_mode` (`sample_zero` / `average` / `min` / `max`) and
`resolve_mode` field on `Render_pass_attachment_descriptor`. The Vulkan
backend now goes through `vkCreateRenderPass2` with
`VkSubpassDescriptionDepthStencilResolve` so depth and (where supported)
stencil aspects can resolve into a single-sample texture. The Metal backend
sets the corresponding `MTLMultisampleDepthResolveFilter` /
`MTLMultisampleStencilResolveFilter`. The MSAA depth-resolve cell in
`rendering_test` exercises both backends.

### 3.5 Texture-heap classification: Sampler_aspect + is_texture_heap (All Backends)

Two cross-cutting cleanups landed together because they touch the same API
surface:

**Sampler_aspect annotation.** `Shader_resource::add_sampler()` now takes
`Sampler_aspect sampler_aspect` (`color`/`depth`/`stencil`). The aspect is
consumed by:
- `Texture_heap` -- when writing a `VkImageView` it requests the right aspect
  (`COLOR_BIT` / `DEPTH_BIT` / `STENCIL_BIT`) instead of producing a
  `DEPTH | STENCIL` view that fails `VUID-VkDescriptorImageInfo-imageView-01976`.
- `Bind_group_layout` -- combined-image-sampler bindings store the aspect so
  `Render_command_encoder::set_sampled_image()` knows which `VkImageLayout`
  to use (`SHADER_READ_ONLY_OPTIMAL` for color,
  `DEPTH_STENCIL_READ_ONLY_OPTIMAL` for depth/stencil).

**Explicit `is_texture_heap` flag.** `add_sampler()` also takes `bool
is_texture_heap` to discriminate between bindless / argument-buffer samplers
(e.g. `s_textures[64]`, `s_texture_array`) and dedicated samplers
(`s_shadow_compare`, `s_input`, `s_depth`, ...). The Metal shader emitter
classifies samplers by this flag instead of by `array_size > 1`, which used
to mis-classify scalar dedicated samplers and arrays of size 1. The texture
heap iterates only `is_texture_heap=true` members when computing argument
buffer slot layout.

A runtime assertion enforces that `is_texture_heap == true` and
`dedicated_texture_unit.has_value()` are mutually exclusive.

> Note: in a later cleanup pass (section 3.10) the `is_texture_heap` and
> `Sampler_aspect` fields, along with the sampler name and GLSL type, moved
> from `Shader_resource::add_sampler()` onto `Bind_group_layout_binding`.
> Application code no longer calls `add_sampler()` directly for the
> sampler-declaration block -- `Bind_group_layout_impl` synthesizes it
> from the create_info bindings.

### 3.6 New `Render_command_encoder::set_sampled_image()` API (All Backends)

```cpp
encoder.set_sampled_image(
    /*binding_point=*/0,
    *texture,
    *sampler
);
```

Per-backend implementation:
- **Vulkan** -- `vkCmdPushDescriptorSetKHR` with the `Sampler_aspect` from the
  matching combined-image-sampler binding in the active bind group layout.
- **Metal** -- `setFragmentTexture` / `setFragmentSamplerState` at the slot
  computed via `binding_point + bind_group_layout->get_sampler_binding_offset()`.
- **OpenGL** -- (sampler array path) writes the texture to the assigned unit;
  the bindless path writes a uint64 handle into the UBO, same as before.

This is the "post-processing pattern" already used by `s_input`/`s_downsample`/
`s_upsample` and by `s_shadow_compare`/`s_shadow_no_compare`. Migrating
`Light_buffer`, `post_processing`, `text_renderer`, `imgui_renderer`, and the
rendering_test sep_tex / depth_visualize / stencil cells onto this API let
us drop the dual-path branch from `Texture_heap` and assert color-only on
the bindless heap.

### 3.7 SPIR-V cache and shared usage mapping

A SPIR-V binary cache (keyed by GLSL source hash) lives under
`<exe>/spirv_cache/` and is consulted by the Vulkan and Metal shader
prototypes before invoking glslang. Cuts cold-start shader compile time
significantly. Similarly, `Image_usage_flag_bit_mask -> VkImageUsageFlags`
translation is now centralised in one helper instead of being duplicated
across `vulkan_texture.cpp` and `vulkan_render_pass.cpp`.

### 3.8 Vulkan deferred GPU resource destruction

All Vulkan resources that may still be referenced by in-flight command
buffers (`Buffer`, `Texture`, `Sampler`, `Bind_group_layout`, `Texture_heap`,
`Surface`, compute pipelines, render pipelines) defer their `vkDestroy*` /
`vmaDestroy*` calls to `Device_impl::add_completion_handler()`. Handlers run
during `~Device_impl()` after `vkDeviceWaitIdle()`, guaranteeing the GPU is
idle. The same pattern was extended to Metal (see
`Metal: defer GPU resource destruction via add_completion_handler` commit).

### 3.9 Texture_heap API simplification: drop `reserved_slot_count` and `default_uniform_block` (All Backends)

Two parameters were trimmed from the public `Texture_heap` constructor in
two consecutive commits:

**`reserved_slot_count` removed** (commit *Drop reserved_slot_count from
Texture_heap; derive offset in GL impl*). This parameter used to let the
caller tell the heap "leave the first N texture units alone, they belong
to dedicated samplers". After `set_sampled_image()` (section 3.6) replaced
the reserved-slot mechanism on every backend, the value was dead weight on
GL bindless, Vulkan descriptor indexing, and Metal argument buffer --
only the GL sampler-array path still needed it, and it could derive the
same information from the bind group layout instead. The GL sampler-array
`Texture_heap_impl` now reads `get_texture_heap_base_unit()` /
`get_texture_heap_unit_count()` from the `Bind_group_layout_impl` during
construction; Vulkan and Metal lose the field entirely. The scene
renderer, imgui, hextiles, text renderer, post-processing, and
rendering_test cells all stop passing the argument.

**`default_uniform_block` removed** (commit *Hide default_uniform_block
behind Bind_group_layout*, see 3.10). Metal's texture heap used to iterate
the caller-supplied `Shader_resource*` looking for the texture-heap
sampler it should populate in the argument buffer. It now reads the same
block via `bind_group_layout->get_default_uniform_block()`, so the
parameter disappears from the public API and from every call site. The
public `Texture_heap` constructor is now:

```cpp
Texture_heap(
    Device&                  device,
    const Texture&           fallback_texture,
    const Sampler&           fallback_sampler,
    const Bind_group_layout* bind_group_layout = nullptr
);
```

### 3.10 `default_uniform_block` encapsulation -- `Bind_group_layout` owns the sampler declarations (All Backends)

Before this change, every renderer that needed GLSL uniform sampler
declarations in its shader preamble owned its own
`Shader_resource default_uniform_block` and threaded the pointer through
`Shader_stages_create_info::default_uniform_block` (so the source emitter
could inject `uniform sampler2D s_foo;` lines) and, on Metal, through the
`Texture_heap` constructor (so the argument encoder could discover the
heap sampler). The same samplers were *also* declared as
`combined_image_sampler` bindings in the caller's `Bind_group_layout`, so
`set_sampled_image()` could translate binding points into descriptor
slots and the Vulkan pipeline layout had matching descriptor slots. Two
parallel structures, kept in sync by the caller.

`Bind_group_layout_binding` now carries the full sampler metadata -- name,
GLSL type, aspect, heap-vs-dedicated flag, array size:

```cpp
class Bind_group_layout_binding {
public:
    uint32_t       binding_point;
    Binding_type   type;
    uint32_t       descriptor_count;
    // combined_image_sampler only:
    Sampler_aspect   sampler_aspect;
    std::string_view name;
    Glsl_type        glsl_type;
    bool             is_texture_heap;
    uint32_t         array_size;
};
```

At construction, each backend's `Bind_group_layout_impl` synthesizes a
private `Shader_resource m_default_uniform_block` from the
combined-image-sampler bindings, exposes it via
`get_default_uniform_block()`, and the existing consumers (shader-source
emitter in `shader_stages_create_info.cpp`, GL `<4.30` sampler-location
fallback in `gl_shader_stages_prototype.cpp`, Metal `is_texture_heap`
lookup in `metal_shader_stages_prototype.cpp`, Metal argument encoder in
`metal_texture_heap.cpp`) all read it via `bind_group_layout` instead of
through `Shader_stages_create_info` / the heap constructor.

`Shader_stages_create_info::default_uniform_block` is deleted. Application
code no longer names `default_uniform_block` anywhere outside
`src/erhe/graphics/` -- the type is fully internal to the bind-group-layout
implementation for the sampler-declaration use case.

The GL sampler-array `Bind_group_layout_impl` computes the texture heap's
base unit differently depending on whether the caller supplied an
explicit texture-heap binding:

- **Explicit heap** (imgui's `s_textures`, rendering_test's `s_textures`):
  base = the binding's `binding_point`, count = the binding's `array_size`.
- **Implicit heap** (scene renderer, hextiles): base = one past the last
  dedicated combined-image-sampler, count = leftover
  `max_per_stage_descriptor_samplers`. A default `s_texture` declaration
  is appended automatically unless a binding is already named `s_texture`
  (which Text_renderer uses for its single font sampler).

Affected callers: `Program_interface`, `Imgui_program_interface`,
`Text_renderer`, `Post_processing` (editor), `Tile_renderer` (hextiles),
`rendering_test` cells (textured_quad, multi_texture, separate_samplers,
depth_visualize). All lose their owned `Shader_resource default_uniform_block`
members and any `shadow_sampler_compare` / `shadow_sampler_no_compare` /
`texture_sampler` pointer fields into it.

### 3.11 `Device_info` helper cleanup

Two helper methods on `Device_info` were deleted after 3.10 made them
either always-true or the only call site inlined the check directly:

- `uses_default_uniform_block()` used to return `true` for GL bindless,
  GL sampler-array, and Vulkan descriptor-indexing and `false` for Metal.
  It gated the conditional addition of shadow samplers in
  `Program_interface` / `Programs` and of the combined-image-sampler
  bindings in the scene-renderer bind group layout. After 3.10 the
  shadow declarations always travel with the bindings and the gate is
  always-true on every backend, including Metal (which incidentally was
  silently broken before -- editor / hextiles / imgui never populated
  Metal's argument encoder with a texture-heap sampler because the gate
  skipped the shadow / texture bindings entirely).
- `uses_sampler_array_in_set_0()` used to return `true` only for GL
  sampler-array. It gated whether each caller should add its
  texture-heap sampler (`s_texture`, `s_textures`, `s_texture_array`) to
  its default uniform block. After 3.10 the GL sampler-array-specific
  sampler goes into `Bind_group_layout_impl` as either an explicit
  binding metadata or the implicit `s_texture` convenience add. Imgui
  and the rendering_test cells, which need their texture-heap sampler
  only on the GL sampler-array path, inline the check directly as
  `(graphics_device.get_info().texture_heap_path == Texture_heap_path::opengl_sampler_array)`.

The editor's `Programs` constructor signature also gained a
`Program_interface&` parameter (matching the `example` and
`rendering_test` `Programs` constructors), and the `Forward_renderer`
constructor lost its trailing `Shader_resource* default_uniform_block`
argument -- the scene-renderer default uniform block lives on
`Program_interface` next to `bind_group_layout`, and the forward
renderer reads both from there.

### 3.12 Cross-submission render pass synchronization (Vulkan) and `Scoped_render_pass` dependency arguments (All Backends)

Two changes landed together because they share a motivation: removing
the per-render-pass CPU stall without sacrificing correctness, and
giving the graphics API a clear way for callers to declare render pass
dependencies so backends can synchronize precisely.

**Public API: `Scoped_render_pass` dependency arguments.**
`Scoped_render_pass` and `Render_pass::start_render_pass` /
`end_render_pass` gained two optional arguments:

```cpp
erhe::graphics::Scoped_render_pass scoped_render_pass{
    *render_pass,
    /*render_pass_before=*/previous_render_pass,  // or nullptr
    /*render_pass_after=*/ next_render_pass       // or nullptr
};
```

- `render_pass_before` names the immediately preceding render pass whose
  output this pass consumes.
- `render_pass_after` names the immediately succeeding render pass that
  will consume this pass's output.

Both default to `nullptr`, meaning "no known predecessor / successor",
which falls back to conservative synchronization. Existing call sites
compile unchanged; `post_processing`'s downsample and upsample chains
wire the previous iteration's `Render_pass*` through as a demonstration
of the intended pattern. The OpenGL, Metal, and null backends accept
and ignore the arguments (those drivers handle the synchronization
implicitly).

**Vulkan: remove `vkQueueWaitIdle` per render pass.**
Previously `Render_pass_impl::end_render_pass` submitted its command
buffer and then CPU-stalled on `vkQueueWaitIdle` after every off-screen
render pass, serializing ~10 render passes per frame. A comment at the
site said "simple but correct; could use fences for pipelining". This
is replaced with:

1. A narrowed `VkMemoryBarrier2` emitted via `vkCmdPipelineBarrier2`
   before `vkCmdBeginRenderPass` in every non-swapchain command buffer.
   Source stages are `COLOR_ATTACHMENT_OUTPUT | LATE_FRAGMENT_TESTS`
   (what render passes write); destination stages are
   `FRAGMENT_SHADER | COLOR_ATTACHMENT_OUTPUT | EARLY_FRAGMENT_TESTS |
   LATE_FRAGMENT_TESTS` (what render passes read/write). This allows
   vertex input and vertex shader work in pass N+1 to overlap with
   fragment work in pass N. Compute dispatches and immediate-command
   uploads use separate command pools with synchronous submit+wait so
   they do not need coverage. Ring-buffer host-coherent writes are
   made visible by vkQueueSubmit. Combined with submission order on
   the single graphics queue -- Vulkan spec 7.1 says a pipeline
   barrier in a later submission creates an execution + memory
   dependency against all earlier commands in submission order on that
   queue -- this gives correct cross-submission memory visibility
   without stalling the CPU. Layout transitions continue to be driven
   by the `VkRenderPass` `initialLayout` / `finalLayout` on each
   attachment, so the barrier carries only a plain `VkMemoryBarrier2`,
   no `VkImageMemoryBarrier2` entries.
2. Non-swapchain command buffers now come from device-wide per-frame-
   in-flight command pools
   (`Device_impl::m_per_frame_render_pass_command_pools[2]`) allocated
   via the new `Device_impl::allocate_frame_render_pass_command_buffer()`.
   `Device_impl::frame_completed(frame)` resets the pool whose slot
   matches `frame % s_number_of_frames_in_flight`, which is safe
   because the existing frame-end timeline semaphore
   (`m_vulkan_frame_end_semaphore`) already guarantees GPU completion
   before `frame_completed` fires for that frame index. Ring-buffer
   recycling hangs off the same mechanism.
3. `Render_pass_impl` no longer owns a per-pass `VkCommandPool`; its
   constructor / destructor lose the pool create/destroy, and
   `end_render_pass` loses the `vkResetCommandPool` call. The
   destructor keeps `vkDeviceWaitIdle` as belt-and-braces for the
   cold shutdown / live-resize path.

**Why a plain `VkMemoryBarrier2` rather than fixing the subpass
external dependencies.** Fixing `make_canonical_subpass_dependencies2`
to set real `srcAccessMask` / `srcStageMask` would also work, but the
codebase has scar tissue from that approach: the validation layer's
render-pass compatibility check compares subpass dependency arrays
element-by-element (see fix log entry #70), and
`Device_impl::get_or_create_compatible_render_pass` explicitly silences
caller-supplied mask parameters with `static_cast<void>` and a comment
referencing this. Keeping the canonical deps untouched and emitting a
per-command-buffer pipeline barrier sidesteps that class of validator
complaint entirely.

**Debug assertion on `render_pass_after`.** In debug builds (`NDEBUG`
not defined), `end_render_pass` walks the attachments of `*this` and
`*render_pass_after`. For any attachment texture that appears in both
descriptors, it warns via `log_render_pass` if
`this->usage_after != after->usage_before`. Mismatches indicate a
rendergraph wiring bug that would otherwise manifest as a SYNC-HAZARD
or layout-transition validation warning at the next pass's begin. The
check is silent when `render_pass_after == nullptr`.

**What the backend does not yet use the hint arguments for.** The
current Vulkan implementation uses `render_pass_before` /
`render_pass_after` only for the debug assertion above; the runtime
sync is the same conservative global barrier regardless of whether the
caller declared a dependency. The arguments leave room for a future
refinement that derives a narrower barrier (or a proper
per-attachment `VkImageMemoryBarrier2`) from the
`usage_after` / `usage_before` masks on shared attachment textures.
