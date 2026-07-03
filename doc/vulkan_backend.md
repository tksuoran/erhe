# Vulkan Graphics Backend

> This Vulkan backend was substantially created by Claude (Anthropic),
> with architectural guidance and review from Timo Suoranta.

## Overview

The Vulkan backend implements the `erhe::graphics` abstraction layer on top of
Vulkan 1.3. It is feature-complete: the `editor`, `example`, `hextiles`, and
`rendering_test` executables run on it, and it is the backend used for the
OpenXR / Quest path. It targets desktop drivers (AMD, NVIDIA, Intel), MoltenVK
and KosmicKrisp on macOS, and Android / Quest mobile drivers. It runs cleanly
under `VK_LAYER_KHRONOS_validation` with core, synchronization, and
best-practices checks enabled.

All Vulkan-specific code lives in `src/erhe/graphics/erhe_graphics/vulkan/` and
is hidden behind the shared pimpl-based public API (`device.hpp`, `buffer.hpp`,
`texture.hpp`, `render_command_encoder.hpp`, ...). No public header is
Vulkan-specific; the backend is selected at configure time.

This document describes the current design. For the API surface shared across
all backends, see `src/erhe/graphics/notes.md`; for the Metal backend see
`doc/metal_backend.md`.

### Build configuration

Select the Vulkan backend via the configure script:

```bat
scripts\configure_vs2026_vulkan.bat
```

CMake option `ERHE_GRAPHICS_API=vulkan` defines `ERHE_GRAPHICS_API_VULKAN` and
forces `ERHE_SPIRV=ON` (the backend always needs glslang for GLSL-to-SPIR-V).
Vulkan entry points are loaded through `volk`; device memory is managed by VMA
(Vulkan Memory Allocator); the portability-subset structs are made visible by
the `VK_ENABLE_BETA_EXTENSIONS` private compile definition on the
`erhe_graphics` target.

## File structure

| File | Purpose |
|------|---------|
| `vulkan_device.cpp/.hpp` | `Device_impl`: queues, frame lifecycle, per-thread command pools, pipeline + compatible-render-pass caches, GPU timer pool, deferred-destruction completion handlers, descriptor/pipeline infrastructure |
| `vulkan_device_init.cpp` | Instance + device creation, layer/extension selection, feature query + enable, VMA setup, `Device_info` population, descriptor-set-layout and pipeline-cache creation |
| `vulkan_device_debug.cpp` | Debug report / debug utils messenger callbacks, `set_debug_label()` |
| `vulkan_device_sync_pool.cpp/.hpp` | `Device_sync_pool`: cross-session pool of reusable `VkFence` / `VkSemaphore` handles |
| `vulkan_command_buffer.cpp/.hpp` | `Command_buffer_impl`: `VkCommandBuffer` wrapper, begin/end, swapchain engage, wait/signal registration, upload / clear / barrier recording |
| `vulkan_command_encoder.cpp/.hpp` | Encoder base: `set_buffer()` |
| `vulkan_render_command_encoder.cpp/.hpp` | Graphics pipeline-state hashing + creation, draw calls, viewport/scissor, `set_buffer` / `set_sampled_image`, multi-draw-indirect |
| `vulkan_compute_command_encoder.cpp/.hpp` | Compute pipeline + `vkCmdDispatch` |
| `vulkan_blit_command_encoder.cpp/.hpp` | Buffer/texture copies, `vkCmdBlitImage`, mipmap generation, `vkCmdFillBuffer` |
| `vulkan_render_pass.cpp/.hpp` | `vkCreateRenderPass2` for swapchain + off-screen passes, MSAA color/depth/stencil resolve, framebuffer, inter-pass and post-pass barriers |
| `vulkan_render_pipeline.cpp/.hpp` | Up-front graphics `VkPipeline` (`Render_pipeline`) |
| `vulkan_compute_pipeline.cpp/.hpp` | Up-front compute `VkPipeline` (`Compute_pipeline`) |
| `vulkan_shader_stages.cpp/.hpp` | `VkShaderModule` creation, prototype build-state machine |
| `vulkan_shader_stages_prototype.cpp` | GLSL-to-SPIR-V via glslang, on-disk SPIR-V cache |
| `vulkan_bind_group_layout.cpp/.hpp` | Per-program `VkDescriptorSetLayout` + `VkPipelineLayout`, sampler metadata, synthesized default uniform block |
| `vulkan_texture.cpp/.hpp` | VMA-backed `VkImage`, cached `VkImageView`s, layout tracking, texture views |
| `vulkan_texture_heap.cpp/.hpp` | Bindless material textures via descriptor indexing (set 1) |
| `vulkan_buffer.cpp/.hpp` | VMA-backed `VkBuffer`, persistent mapping |
| `vulkan_sampler.cpp/.hpp` | `VkSampler` |
| `vulkan_surface.cpp/.hpp` | `VkSurfaceKHR`, surface-format / present-mode scoring, swapchain create-info |
| `vulkan_swapchain.cpp/.hpp` | `VkSwapchainKHR`, per-frame acquire/present semaphores, app-managed depth image, present history, recreation |
| `vulkan_emulated_swapchain.cpp/.hpp` | Surfaceless offscreen "swapchain" for the headless configuration (`ERHE_WINDOW_LIBRARY=none`), backs `Device::capture_last_frame()` |
| `vulkan_vertex_input_state.cpp/.hpp` | Vertex attribute / binding descriptions |
| `vulkan_gpu_timer.cpp/.hpp` | Timestamp queries against the device query pool |
| `vulkan_debug.cpp`, `vulkan_scoped_debug_group.cpp/.hpp` | Debug labels and scoped debug groups via `VK_EXT_debug_utils` |
| `vulkan_helpers.cpp/.hpp` | Enum/format conversions, stage/access mapping, canonical subpass dependencies, layout helpers |
| `vma_usage.cpp` | VMA implementation translation unit |
| `vulkan_metal_layer_mac.hpp/.mm` | `CAMetalLayer` bridge for MoltenVK on macOS |

## Device initialization

`Device_impl` is constructed from a `Surface_create_info`, a `Graphics_config`,
and an optional `Vulkan_external_creators` (the OpenXR hook, see
[OpenXR and multiview](#openxr-and-multiview)). Initialization
(`vulkan_device_init.cpp`) proceeds:

1. `volkInitialize()`, then instance layer + extension enumeration.
2. `vkCreateInstance` with `apiVersion = VK_API_VERSION_1_3`. The validation
   layer is enabled only when `Graphics_config::vulkan.vulkan_validation_layers`
   is set and RenderDoc capture is not active.
3. Physical-device selection (`choose_physical_device`), scored by type and
   queue-family suitability.
4. Property + feature queries via `vkGetPhysicalDeviceProperties2` /
   `vkGetPhysicalDeviceFeatures2` (driver properties, depth/stencil resolve
   properties, multiview properties, portability subset, descriptor indexing,
   16-bit storage, float16/int8, timeline semaphore, Vulkan 1.1 / 1.3 features).
5. `vkCreateDevice` with a single graphics queue (plus a present queue when a
   surface exists).
6. VMA allocator creation (`vmaImportVulkanFunctionsFromVolk`).
7. Per-(frame-in-flight, thread-slot) command pools, the frame-end timeline
   semaphore, the `Device_sync_pool`, the pipeline cache, descriptor set
   layouts, and the GPU timer query pool.

### Required features

Device init (`vulkan_device_init.cpp`) aborts when one of these is missing.
These are the only Vulkan device features `erhe_graphics` hard-depends on.

| Feature | Where it is used |
|---------|------------------|
| `synchronization2` (Vulkan 1.3) | The sync2 barrier API (`VkMemoryBarrier2` / `vkCmdPipelineBarrier2`) used throughout render passes, uploads, and blits |
| `runtimeDescriptorArray` | Texture-heap descriptor-indexing array (`erhe_texture_heap[]`) |
| `descriptorBindingPartiallyBound` | Texture-heap descriptor set layout |
| `descriptorBindingVariableDescriptorCount` | Texture-heap descriptor set allocation |
| `shaderClipDistance` | Required on desktop; soft-warn on Android (the init log notes ImGui clipping is affected) |
| `vertexPipelineStoresAndAtomics` | Required on desktop; soft-warn on Android |

### Optional features the backend uses

Enabled only when the physical device advertises them; each is exercised by a
concrete `erhe_graphics` code path.

| Feature | Where it is used |
|---------|------------------|
| `timelineSemaphore` | Frame-end timeline semaphore + `vkWaitSemaphores` frame pacing (core since Vulkan 1.2, so effectively always present) |
| `multiDrawIndirect` + `shaderDrawParameters` | Selects native `vkCmdDrawIndexedIndirect` (drawcount > 1, `gl_DrawID`) vs. the per-draw push-constant emulation; emulation is forced on MoltenVK |
| `samplerAnisotropy` | `VkSampler` `anisotropyEnable` when `Sampler_create_info::max_anisotropy > 1` (`vulkan_sampler.cpp`) |
| `depthClamp` | Pipeline `depthClampEnable` from `Rasterization_state::depth_clamp_enable` |
| `multiview` (Vulkan 1.1) | Non-zero `viewMask` on off-screen render passes and matching pipelines (see [OpenXR and multiview](#openxr-and-multiview)) |

### Features enabled but not used by the backend

Device init also enables, when advertised, features that no `erhe_graphics` code
path depends on. They are listed here so the device-init code is not mistaken for
evidence of use:

- `geometryShader`, `sampleRateShading`, `shaderCullDistance`, `shaderInt64`,
  `shaderDemoteToHelperInvocation`, and `shaderStorageImageMultisample` are
  copied from the device's advertised feature set so that SPIR-V modules which
  declare the matching capability pass `vkCreateShaderModule`
  (`VUID-VkShaderModuleCreateInfo-pCode-08740`). The shaders that declare those
  capabilities are authored in the applications -- or, for
  `shaderStorageImageMultisample`, in the OpenXR runtime's injected compositor
  shaders (per the in-tree comment) -- not in `erhe_graphics`.
- `shaderFloat16` / `shaderInt8` and the 16-bit-storage features are enabled when
  present and surfaced through `Device_info`; the library itself does not branch
  on them.
- `dynamicRendering` (Vulkan 1.3) is enabled when present but unused: the backend
  renders exclusively through `VkRenderPass` / `vkCmdBeginRenderPass2` and never
  calls `vkCmdBeginRendering`.

The Vulkan 1.1 features struct subsumes the granular 16-bit-storage and multiview
structs, so only the 1.1 struct is chained at device creation (per
`VUID-VkDeviceCreateInfo-pNext-02829`).

Note: device init currently enables most features by copying the value the
device reported (`set_*.feature = query_*.feature`) instead of requesting only
the minimal set the engine requires -- the `VkPhysicalDeviceDescriptorIndexingFeatures`
block, for example, copies all of its fields when the backend depends on just
three. This is convenient but not ideal: it enables features the backend does not
use (above), widens the apparent dependency surface, and can let a shader that
accidentally relies on a capability run on hardware where it happens to be
available while failing `vkCreateShaderModule` elsewhere. The intended direction
is to enable only strictly-required features and fail explicitly when one is
missing. See `doc/todo.md`.

### Device_info

`Device_info` is populated from `VkPhysicalDeviceLimits`. `uint32_t` limits are
capped at `INT_MAX` before being stored in signed fields (drivers report
`UINT32_MAX` for "unlimited"). Notable settings:

- `texture_heap_path = Texture_heap_path::vulkan_descriptor_indexing`
- `use_binary_shaders = true`, `glsl_version = 460`
- `use_compute_shader`, `use_shader_storage_buffers`, `use_clear_texture`,
  `use_texture_view`, `use_persistent_buffers` all true
- Coordinate conventions: depth range `zero_to_one`, framebuffer origin
  `top_left`, no clip-space Y flip, texture origin `top_left`
- Depth/stencil resolve modes from
  `VkPhysicalDeviceDepthStencilResolveProperties` (defaulting to
  `SAMPLE_ZERO` when a driver reports none)

### Portability subset (MoltenVK)

When `VK_KHR_portability_subset` is advertised, its feature and property structs
are queried and stored. When it is not advertised the device is treated as fully
featured: every portability feature flag is forced to `VK_TRUE` and properties
are set to non-constraining defaults (e.g. `minVertexInputBindingStrideAlignment = 1`),
so call sites can read these structs unconditionally. MoltenVK also forces the
multi-draw-indirect emulation path because MSL has no `gl_DrawID` equivalent.

## Frame lifecycle and command buffers

The device frame is bracketed by `Device::wait_frame()` and
`Device::end_frame()`. Between them, callers obtain `Command_buffer` instances,
record into them, and submit through `Device::submit_command_buffers()`. There
is no `Device::present()`: present is implicit in submit.

- **`wait_frame()`** -- paces the per-(frame-in-flight, thread-slot) command
  pools against the device timeline semaphore. For a frame that re-enters a slot
  it waits until the timeline reports `frame_index - frames_in_flight`, then
  resets every per-thread pool for that slot (freeing the `Command_buffer`s that
  were allocated from it and recycling their implicit fence + semaphore back into
  `Device_sync_pool`). GPU timer results for the completing slice are read here.
- **`get_command_buffer(thread_slot)`** -- allocates a fresh primary
  `VkCommandBuffer` from the slot's pool, wraps it in a `Command_buffer` owned by
  the pool, and binds an implicit fence + binary semaphore from the sync pool.
  The caller must `begin()` it before recording.
- **`Command_buffer::wait_for_swapchain` / `begin_swapchain`** -- wait for the
  next swapchain image, acquire it, and bind the per-slot acquire/present
  semaphores to this command buffer.
- **`submit_command_buffers(span)`** -- performs any CPU-side `vkWaitForFences`
  for registered `wait_for_cpu` dependencies, splices all wait/signal semaphores
  into one `VkSubmitInfo2`, runs `vkQueueSubmit2`, then drives
  `vkQueuePresentKHR` for every swapchain a command buffer engaged via
  `begin_swapchain`.
- **`end_frame()`** -- advances the frame index. Mechanically it signals the
  device timeline semaphore at the current frame index via an empty
  `vkQueueSubmit2` and increments the CPU frame counter. It does not submit or
  present.

The backend keeps `s_number_of_frames_in_flight = 2` frames in flight across
`s_number_of_thread_slots = 8` thread slots. Command pools are created with
`VK_COMMAND_POOL_CREATE_TRANSIENT_BIT` and reset wholesale (never per buffer).
`Command_buffer_impl` synchronization is recorded as `wait_for_cpu` /
`wait_for_gpu` / `signal_gpu` / `signal_cpu` and collected into a
`Vulkan_submit_info` at submit time.

## Descriptor and binding model

The authoritative descriptor layout for each shader program is its
`Bind_group_layout`. `Bind_group_layout_impl` (`vulkan_bind_group_layout.cpp`)
builds, from the program's `Bind_group_layout_create_info`:

- a `VkDescriptorSetLayout` for set 0 (uniform buffers, storage buffers, and
  dedicated combined image samplers);
- a `VkPipelineLayout` with set 0 = the layout above and set 1 = the device's
  texture-heap layout (`Device_impl::get_texture_set_layout()`);
- a `sampler_binding_offset` computed from the highest buffer binding so the
  sampler binding namespace does not collide with buffers;
- a synthesized private `Shader_resource` "default uniform block" that mirrors
  the combined-image-sampler bindings into GLSL `uniform sampler...` declarations
  (consumed by the shader-source emitter).

GLSL `layout(binding = N)` maps to set 0, binding N. The device also keeps a
fixed-convention set-0 descriptor layout (uniform buffers at bindings 0..7,
storage buffers 8..11, samplers 12..15) and, when `VK_KHR_push_descriptor` is
not available, a per-frame descriptor pool for the fallback path.

Two ways a shader reads a texture, chosen per `combined_image_sampler` binding:

- **Dedicated sampler** (`is_texture_heap = false`). Bound per draw via
  `Render_command_encoder::set_sampled_image(binding_point, texture, sampler)`,
  which issues `vkCmdPushDescriptorSetKHR` (or allocates from the per-frame pool
  in the fallback path). The binding's `Sampler_aspect` (color / depth / stencil)
  selects the image-view aspect, satisfying
  `VUID-VkDescriptorImageInfo-imageView-01976`. Used for shadow maps
  (`s_shadow_compare`), post-processing chains (`s_input`, `s_downsample`,
  `s_upsample`), the text-renderer font, etc.
- **Texture-heap sampler** (`is_texture_heap = true`). Backed by a
  `Texture_heap`; the shader indexes `erhe_texture_heap[index]` with a handle
  stored in the material UBO. Used for material texture arrays.

Buffers are bound via `set_buffer(target, buffer, offset, length, index)`, also
through push descriptors (or the fallback pool). `draw_indirect` buffers are
stored for the later indirect draw.

Immutable comparison samplers: a binding may be marked immutable, in which case
its sampler is baked into the descriptor set layout via `pImmutableSamplers`.
Encoders then write `VK_NULL_HANDLE` for the descriptor's sampler field, which
MoltenVK requires for comparison samplers
(`VUID-VkDescriptorImageInfo-mutableComparisonSamplers-04450`).

### Texture heap (bindless)

`Texture_heap_impl` (`vulkan_texture_heap.cpp`) is a descriptor-indexing array
at set 1. The device creates `m_texture_set_layout` as a single binding of 256
combined image samplers with `PARTIALLY_BOUND | UPDATE_AFTER_BIND |
VARIABLE_DESCRIPTOR_COUNT`. `allocate(texture, sampler)` writes the descriptor
and returns its array index as an opaque handle; `bind()` binds the set against
the active bind group layout's pipeline layout. The heap is color-only:
depth/stencil samplers go through the dedicated `set_sampled_image` path. The
descriptor pool and set layout are destroyed via deferred completion handlers.

## Pipelines

There are two ways to obtain a graphics `VkPipeline`:

- **Up-front** (`Render_pipeline`, `vulkan_render_pipeline.cpp`). Created from a
  `Render_pipeline_create_info` that carries the render-pass format fields
  (color formats, depth/stencil format, sample count, bind group layout). Bound
  with `Render_command_encoder::set_render_pipeline()`. `Base_render_pipeline`
  defers creation to first use and caches variants keyed by
  `(shader_stages, vertex_input, vertex_format, render-pass format)`.
- **Draw-time** (`set_render_pipeline_state()` in
  `vulkan_render_command_encoder.cpp`). Builds a `VkGraphicsPipelineCreateInfo`
  from the `Render_pipeline_state`, hashes it (the hash includes
  `stencil_front` / `stencil_back` so stencil-only differences do not alias),
  and looks it up in the device pipeline map before creating against the
  `VkPipelineCache`.

Pipelines need a render pass for format compatibility, not the actual in-use
pass. `Device_impl::get_or_create_compatible_render_pass()` creates lightweight
`VkRenderPass` objects keyed by a format tuple (and view mask) for this purpose;
they are cached and destroyed with the device. The in-use render pass and the
compatibility render pass produce identical subpass dependency arrays (see
[Render passes](#render-passes)).

Pipeline notes:
- `primitiveRestartEnable` is forced false for list topologies (erhe presets set
  it true, which is fine for OpenGL but invalid for non-strip/fan topologies on
  Vulkan, `VUID-...-topology-06252`).
- `rasterizationSamples` is taken from the active render pass sample count.
- Color blend `attachmentCount` is taken from the render pass color attachment
  count (shadow passes have zero color attachments).

Compute pipelines (`Compute_pipeline`, `vulkan_compute_pipeline.cpp`, and
`set_compute_pipeline_state()`) are created from the compute shader module and
the bind group layout's pipeline layout, then bound with `vkCmdBindPipeline`.

Multi-draw-indirect: native `vkCmdDrawIndexedIndirect` with `drawcount > 1` and
the `gl_DrawID` built-in when `multiDrawIndirect` + `shaderDrawParameters` are
both available (and not MoltenVK); otherwise a per-draw loop that pushes the
draw ID through a push constant.

## Render passes

`Render_pass_impl` (`vulkan_render_pass.cpp`) always uses `vkCreateRenderPass2`.
A pass is either a swapchain pass (`m_swapchain != nullptr`) or an off-screen
pass.

**Attachments.** `Load_action` maps to `VkAttachmentLoadOp`, `Store_action` to
`VkAttachmentStoreOp`. The attachment descriptor's `usage_before` / `usage_after`
masks map to `initialLayout` / `finalLayout` (via `to_vk_image_layout`). When an
off-screen attachment texture is still `VK_IMAGE_LAYOUT_UNDEFINED` and the load
action is not `Load`, the pass advertises `UNDEFINED` as the initial layout so it
drives its own discarding transition instead of a wasteful pre-transition. A
`Load` with `UNDEFINED` is never emitted (`VUID-VkAttachmentDescription2-format-06699`).

**Clear values.** `clearValueCount` is non-zero only when at least one
attachment has a `Clear` load action, avoiding the best-practices
"clear-values-not-needed" warning.

**MSAA.** Multisampled color and depth/stencil attachments coerce their
load/store ops to `DONT_CARE` (so samples can stay in tile / lazy memory on
tiler GPUs) unless the caller explicitly asked to preserve the samples
(`Store` / `Store_and_multisample_resolve`).

**Resolve.** Color resolve targets are emitted as resolve attachment references.
Depth/stencil resolve uses `VkSubpassDescriptionDepthStencilResolve` chained into
the subpass description, with `Resolve_mode` mapped to the Vulkan resolve-mode
bit. The resolve target's `finalLayout` carries the caller's intended
post-pass layout so the next pass can sample it without an explicit barrier.

**Subpass dependencies.** Both swapchain and off-screen passes use
`make_canonical_subpass_dependencies2()`, which depends only on whether color
and/or depth attachments are present. This guarantees the in-use pass and the
pipeline's compatibility render pass produce byte-identical dependency arrays
(the validation layer compares these element-by-element as part of render-pass
compatibility).

**Framebuffers.** Off-screen passes own a `VkFramebuffer` built from
single-mip-level image views of the attachment textures. Swapchain passes
lazily acquire the per-image framebuffer from `Swapchain_impl`
(`acquire_swapchain_framebuffer`), keyed by the render pass.

The render pass and framebuffer are destroyed via deferred completion handlers.

## Synchronization

The backend runs all rendering on a single graphics queue and does not stall the
CPU between passes (there is no per-pass `vkQueueWaitIdle`). Cross-submission
ordering on the single queue (Vulkan spec 7.1: a pipeline barrier in a later
submission creates a dependency against earlier commands in submission order)
combined with explicit barriers provides correctness:

- **Layout transitions** are driven by the `VkRenderPass` `initialLayout` /
  `finalLayout` on each attachment.
- **Inter-pass barrier** (`compute_inter_pass_barrier`, emitted as a
  `VkMemoryBarrier2` via `vkCmdPipelineBarrier2` before `vkCmdBeginRenderPass2`
  on off-screen passes). Its source stages/access come from each attachment's
  `usage_before`; its destination always includes `FRAGMENT_SHADER + SHADER_READ`
  (any pass may sample descriptor-bound textures not listed as attachments) plus
  color/depth stages for the attachments present.
- **Post-pass barrier** (`compute_post_render_pass_barrier`, emitted after
  `vkCmdEndRenderPass2`). Makes attachment writes visible to the stages implied
  by each attachment's `usage_after` (e.g. `FRAGMENT_SHADER` for
  `usage_after = sampled`), which the subpass `0 -> EXTERNAL` dependency alone
  does not cover. Skipped for attachments whose `usage_after` carries the
  `user_synchronized` bit.
- **Upload barrier.** `Command_buffer::upload_to_buffer` follows each
  `vkCmdCopyBuffer` with a range-scoped `VkBufferMemoryBarrier2` whose
  destination stage/access is derived from the destination buffer's own
  `Buffer_usage` (`buffer_usage_to_vk_stage_access`), so an upload chains exactly
  to the stages the buffer is used by (vertex input, storage, indirect, uniform)
  rather than a conservative `ALL_COMMANDS`.

Ring-buffer host-coherent writes are made visible to the device by
`vkQueueSubmit`. Compute dispatches and immediate-command uploads use separate
synchronous submit+wait paths, so they are not covered by the inter-pass barrier.

`Scoped_render_pass` (and `Render_pass::start_render_pass` / `end_render_pass`)
accept optional `render_pass_before` / `render_pass_after` arguments. In the
current implementation these are used only for a debug-build consistency check:
`end_render_pass` warns when a shared attachment texture's `usage_after` here
does not match `usage_before` in the declared successor. The runtime barrier is
the same regardless; the arguments leave room for deriving narrower
per-attachment barriers later.

`start_render_pass` asserts that no other render pass is currently active,
catching nested-pass wiring bugs early.

### Image layout tracking

`Texture_impl` tracks its current `VkImageLayout` in `m_current_layout`.
`transition_layout(cb, new_layout)` inserts a `VkImageMemoryBarrier2` only when
the layout differs; `set_layout(layout)` updates the tracked layout without a
barrier (used by `end_render_pass` to record the `finalLayout` the render pass
machinery applied). Optional layout-transition tracing can be logged to
`logs/vulkan.txt`.

## Resources

**Buffers** (`vulkan_buffer.cpp`). VMA-backed `VkBuffer`. `Buffer_usage` maps to
`VkBufferUsageFlags`; the usage is retained for the upload barrier. Persistently
mapped, host-coherent memory is used where requested.

**Textures** (`vulkan_texture.cpp`). VMA-backed `VkImage`. Image views are cached
per `(aspect, base_layer, layer_count, base_level, level_count)` tuple;
framebuffer attachments request single-level views, and sampling views commit to
a single aspect. Texture views share the source image's `VkImage` and add their
own base layer / level offset; the `m_is_view` flag prevents the view from
destroying the shared image. The image view type auto-selects array variants
only when the caller requests them explicitly.

**Samplers** (`vulkan_sampler.cpp`). `VkSampler` from `VkSamplerCreateInfo`;
filter, mipmap mode, address modes, compare op, LOD range, and anisotropy come
from `Sampler_create_info`.

**Ring buffers** (`erhe_graphics/ring_buffer.cpp`). The Vulkan backend uses the
backend-agnostic `Ring_buffer`: a single host-coherent VMA buffer (via
`vulkan_buffer.cpp`) written through a moving cursor. The position / wrap-count
bookkeeping (including `frame_completed` reclamation of bytes once the GPU has
consumed the owning frame) lives in
`erhe::circular_ring_buffer::Circular_ring_buffer_algorithm`
(`src/erhe/circular_ring_buffer/`), which has no GPU dependency and is unit
tested in isolation. This is the streaming path for per-frame camera / light /
material / post-processing parameter data.

## Shaders

`Shader_stages_prototype_impl` (`vulkan_shader_stages_prototype.cpp`) compiles
GLSL to SPIR-V with glslang. Compiled binaries are cached on disk under
`<exe>/spirv_cache/`, keyed by a hash of the final GLSL source (after preamble
injection and define expansion), so subsequent runs skip glslang. The build-state
machine (`init -> shader_compilation_started -> program_link_started -> ready`)
returns early when a stage is already complete, so re-running compile/link is
idempotent.

`Shader_stages_impl` (`vulkan_shader_stages.cpp`) calls `vkCreateShaderModule`
for each stage's SPIR-V and exposes `get_vertex_module()` /
`get_fragment_module()` / `get_compute_module()` for pipeline creation. Shader
modules are needed only during pipeline creation, so they are destroyed
immediately in `destroy_modules()` rather than deferred. The associated
`Bind_group_layout` is reachable through the impl.

GLSL authoring conventions for Vulkan paths use `ERHE_VULKAN_DESCRIPTOR_INDEXING`
guards and the `erhe_texture.glsl` sampling macros, plus a push-constant block
for the emulated draw ID.

## Blit, compute, GPU timers, debug groups

**Blit** (`vulkan_blit_command_encoder.cpp`). Buffer-to-texture
(`vkCmdCopyBufferToImage`), texture-to-texture (`vkCmdCopyImage`),
texture-to-buffer (`vkCmdCopyImageToBuffer`), framebuffer blit
(`vkCmdBlitImage`), buffer fill (`vkCmdFillBuffer`), buffer-to-buffer copy, and
mipmap generation (a blit chain transitioning each level). All paths insert the
required layout transitions. `VkBufferImageCopy::bufferRowLength` /
`bufferImageHeight` are in texels / rows (computed from the format size), not
bytes.

**Compute** (`vulkan_compute_command_encoder.cpp`). Records into the supplied
`Command_buffer`; the caller provides a command buffer already in the recording
state. `set_compute_pipeline_state` / `set_compute_pipeline` bind the pipeline,
`set_buffer` pushes descriptors for the compute bind point, `dispatch_compute`
issues `vkCmdDispatch`.

**GPU timers** (`vulkan_gpu_timer.cpp`). A single device `VkQueryPool` sized for
`s_max_gpu_timers (128) * 2 * frames_in_flight` timestamps. Each `Gpu_timer_impl`
claims a slot; `write_begin_timestamp` / `write_end_timestamp` record
`vkCmdWriteTimestamp`. The per-slice reset is recorded by the first
`Command_buffer::begin` of each frame, and results are read in `wait_frame` after
the timeline-semaphore wait guarantees the slice has completed. GPU timers are
disabled when the queue reports `timestampValidBits == 0`.

**Debug groups** (`vulkan_scoped_debug_group.cpp`, `vulkan_debug.cpp`).
`vkCmdBeginDebugUtilsLabelEXT` / `vkCmdEndDebugUtilsLabelEXT` and object debug
labels via `VK_EXT_debug_utils`. Enabled when the debug-utils messenger is
created, so RenderDoc and the validation layer show named markers.

## Surface and swapchain

`Surface_impl` (`vulkan_surface.cpp`) owns the `VkSurfaceKHR` and scores surface
formats and present modes. Format scoring evaluates the (format, color space)
pair together: `_SRGB` + `SRGB_NONLINEAR` is the correct SDR combination and
scores highest, `_UNORM` + `SRGB_NONLINEAR` is penalized to last resort (linear
output displayed without the sRGB transfer looks dark), and `_SFLOAT` +
`EXTENDED_SRGB_LINEAR` is valid for HDR. It honors
`Window_configuration::color_bit_depth` and
`Surface_create_info::prefer_high_dynamic_range`.

`Swapchain_impl` (`vulkan_swapchain.cpp`) owns the `VkSwapchainKHR`, per-image
views and framebuffers, an app-managed depth image (format chosen via
`Device_impl::choose_depth_stencil_format`, recreated on resize), per-frame
acquire/present semaphores recycled through `Device_sync_pool`,
and a present-history queue for safe cleanup of present semaphores and old
swapchains. It supports `VK_EXT_swapchain_maintenance1` and FIFO-latest-ready
present modes when available. Swapchain recreation on `VK_SUBOPTIMAL_KHR` /
`VK_ERROR_OUT_OF_DATE_KHR` is handled in `present()`.

`Surface_impl::recreate_for_new_window()` tears down the `VkSwapchainKHR` and
`VkSurfaceKHR` and rebuilds over a freshly bound native window while keeping the
C++ `Swapchain` object alive, so cached `Swapchain*` pointers (in
`Render_pass_impl`, ImGui hosts) stay valid. This is used on Android when a new
`ANativeWindow` arrives on foreground and the previous surface was lost.

### Headless / surfaceless: the emulated swapchain

The headless Vulkan configuration (`ERHE_WINDOW_LIBRARY=none`, built via
`scripts\configure_vs2026_vulkan_headless.bat`) has no `VkSurfaceKHR` and no
presentation engine. `Emulated_swapchain_impl`
(`vulkan_emulated_swapchain.cpp/.hpp`, owned by `Surface_impl`) stands in for
the real WSI swapchain: it allocates a small ring of offscreen color images
(plus optional depth) with `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`, and the swapchain
render pass leaves them in `TRANSFER_SRC_OPTIMAL`. There is no acquire/present:
image reuse is made safe by single-queue submission order, the render pass
EXTERNAL dependency and the CLEAR (discard) load op; frame pacing comes entirely
from the device timeline semaphore (`Device_impl::wait_frame`), so no
acquire/present semaphores exist. `Render_pass_impl` and
`vulkan_command_buffer.cpp` branch to it wherever they would touch the real
swapchain.

`Emulated_swapchain_impl::read_back_last_frame()` copies the most recently
composited color image back to host memory as RGBA8 (synchronous, drains the
GPU) and is the sole implementation behind `Device_impl::capture_last_frame()`
-- the `capture_screenshot` path used by the in-editor MCP server in the
headless build. Real WSI-swapchain capture is not implemented; the windowed
build returns "Frame capture not available".

The class is a deliberately self-contained sibling of `Swapchain_impl` (some
image-view / framebuffer / depth helper code is duplicated on purpose; the real
WSI path is left untouched).

## Deferred resource destruction

GPU resources may still be referenced by in-flight command buffers when their
owning C++ object is destroyed. Such objects (`Buffer`, `Texture`, `Sampler`,
`Bind_group_layout`, `Texture_heap`, `Surface`, `Render_pass`, render and compute
pipelines) capture their Vulkan handles by value into a lambda and defer
destruction via `Device_impl::add_completion_handler()`. Handlers run from
`frame_completed()` / `~Device_impl()` after the relevant GPU work has completed
(`~Device_impl` calls `vkDeviceWaitIdle` first).

```cpp
Foo_impl::~Foo_impl() noexcept
{
    const VkFoo vk_foo = m_vk_foo;
    m_vk_foo = VK_NULL_HANDLE;
    m_device_impl.add_completion_handler(
        [vk_foo](Device_impl& device_impl) {
            if (vk_foo != VK_NULL_HANDLE) {
                vkDestroyFoo(device_impl.get_vulkan_device(), vk_foo, nullptr);
            }
        }
    );
}
```

Immediate destruction is safe only for handles that are not referenced after the
recording call that consumed them (e.g. `VkShaderModule`, only needed during
pipeline creation) or where the owner drains the GPU itself
(`~Swapchain_impl` uses fence-based frame-in-flight tracking; the swapchain
destructor and `~Device_impl` call `vkDeviceWaitIdle`).

## OpenXR and multiview

`Vulkan_external_creators` (`vulkan_external_creators.hpp`) lets `erhe::xr` wrap
Vulkan creation via `XR_KHR_vulkan_enable2`. When the hooks are set, the backend
forwards its fully populated create-infos to `xrCreateVulkanInstanceKHR`,
`xrGetVulkanGraphicsDevice2KHR`, and `xrCreateVulkanDeviceKHR` (so the runtime can
inject HMD-required extensions and features); when empty it uses the direct
`vkCreateInstance` / `vkCreateDevice` path.

Off-screen render passes support `VK_KHR_multiview` (core in Vulkan 1.1) through
a non-zero `view_mask` on the render pass descriptor. The subpass `viewMask` is
set, and framebuffer image views span enough array layers to cover the highest
set view bit. `Device_info` exposes `multiview`, `max_multiview_view_count`, and
`max_multiview_instance_index`. Swapchain passes do not support multiview (the
swapchain image is a plain 2D color attachment); the OpenXR layered swapchain
target uses the off-screen path. Multiview is the Vulkan-only stereo path -- the
OpenGL `GL_OVR_multiview2` route is out of scope.

## Logging and debugging

- The validation layer is gated by `Graphics_config::vulkan.vulkan_validation_layers`
  (config key `vulkan_validation_layers` in `config/editor/erhe_graphics.json`).
  When enabled it turns on core, synchronization (submit-time), and
  best-practices checks. Validation messages flow through the device message
  callback; in the editor, errors become `ERHE_FATAL` aborts.
- High-volume sync/descriptor traces (`[RP_BEGIN]`, `[BARRIER]`, `[IMG_BARRIER]`,
  ...) are compiled out by default and gated behind `ERHE_LOG_VULKAN` (the
  `ERHE_VULKAN_SYNC_TRACE` / `ERHE_VULKAN_DESC_TRACE` / `ERHE_VULKAN_SYNC_LOG`
  macros in `graphics_log.hpp`). Enable the define to write the per-draw
  layout/sync/descriptor history to `logs/vulkan.txt`.
- Backend-specific logs go to `logs/vulkan.txt`; on Android / Quest the same lines
  flow through `adb logcat` under the `erhe` tag.
- On macOS, `Graphics_config::vulkan.use_kosmickrisp` points the loader at the
  KosmicKrisp ICD instead of MoltenVK (auto-detecting the LunarG SDK when
  `VULKAN_SDK` is not set in the GUI launch environment).

## Known issues

### ID-buffer edge lines: line ends must be rounded in the fragment shader

The ID-buffer edge-line method (`content_edge_lines.use_id_buffer`) draws wide-line
quads into a standalone edge-id buffer (encoded face id in color, plus the pass's
own depth test) and the polygon-fill pass later matches that buffer to paint the
visible lines. The wide-line quads have **square end caps**: the compute tent
(`compute_before_content_line.comp`) extends each half-quad past the edge endpoint
along the edge axis, so the rasterized quad covers a rectangular region at the line
ends rather than a rounded (capsule) one.

The edge-id fragment shader (`res/shaders/content_line_after_compute.frag`) is
responsible for trimming those caps to a rounded shape by `discard`ing fragments
beyond the cap. It already carries a distance-to-segment cap test (the `k` /
`end_weight` term using `v_start_end` and `v_line_width`), but in the edge-id pass
that test does not reliably keep square-cap corner fragments out of the buffers: such
fragments still land in the edge-id buffer (wrong face id) AND in the edge-id depth
buffer (wrong depth). Because the pass owns its depth and the nearest fragment wins
per pixel, a stray cap fragment can occlude the correct edge id, and the
fill-composition match then picks the wrong line color (or none) at that pixel.

The fix is to make the cap `discard` correct for the rounded shape AND
depth-affecting (so a discarded cap fragment writes neither id nor depth). The
fragment shader already has every input it needs (segment endpoints in
viewport-relative pixels `v_start_end`, line width `v_line_width`); no new inputs are
required. (Next edge-line improvement.)
