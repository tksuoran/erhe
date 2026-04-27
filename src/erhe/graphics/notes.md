# erhe_graphics

## Purpose
Vulkan-style abstraction over OpenGL, Vulkan, and Metal. Provides
GPU resource management, render pipeline state, shader compilation, command encoding,
ring buffers, texture/sampler management, render passes, and a device abstraction that
hides the underlying graphics API behind a pimpl pattern.

## Key Types
- `Device` -- Central graphics device. Creates command encoders, manages ring buffers, queries capabilities, handles frame lifecycle (`wait_frame`/`begin_frame`/`end_frame`).
- `Device_info` -- GPU capability queries: GLSL version, limits, feature flags (bindless textures, sparse textures, persistent buffers, compute shaders, multi-draw indirect). `texture_heap_path` selects which of the four sampler-binding strategies the backend uses (see below).
- `Buffer` -- GPU buffer (vertex, index, uniform, storage, etc.) with mapping and flush operations. Uses pimpl for backend. Pure GPU resource wrapper - allocation is handled externally by `Free_list_allocator` (in `erhe::buffer`).
- `Texture` -- GPU texture (1D/2D/3D/cube) with mipmap, MSAA, sparse, and array layer support. Extends `erhe::Item`.
- `Sampler` -- Texture sampler with filtering, addressing, LOD, and anisotropy settings.
- `Shader_stages` -- Compiled and linked shader program. Created from `Shader_stages_prototype`.
- `Shader_stages_create_info` -- Describes shader sources, defines, interface blocks, vertex format, fragment outputs, and the active `Bind_group_layout`. The layout's synthesized default uniform block supplies the sampler declarations that get injected into the shader preamble -- callers do not pass a `Shader_resource` for samplers directly.
- `Shader_resource` -- Describes GLSL resources (uniform/storage blocks, struct types, samplers). Generates GLSL source strings for injection into shaders, eliminating need for reflection. When used for the sampler-declaration block, ownership belongs to `Bind_group_layout` -- application code only sees the type for buffer interface blocks.
- `Bind_group_layout` -- Describes the full set of resources a pipeline reads: buffer bindings (uniform / storage), combined image samplers (dedicated and texture-heap), and the GLSL sampler declarations that ship with them. See "Bind group layout" below.
- `Texture_heap` -- Runtime sampler-array / argument-buffer / descriptor-indexing container for material textures. See "Texture heap" below.
- `Render_pipeline_state` -- Complete render pipeline: shader, vertex input, rasterization, depth/stencil, color blend, multisample, viewport, scissor states.
- `Render_pipeline` / `Lazy_render_pipeline` -- Compiled pipeline objects. `create_render_pipeline()` builds one up-front; `Lazy_render_pipeline` defers creation to first use and caches variants by render-pass format tuple.
- `Render_pass` -- Framebuffer configuration with color/depth/stencil attachments and load/store actions. Attachment descriptors carry `usage_before` / `usage_after` so backends can derive image layout transitions and subpass dependencies without per-texture layout tracking.
- `Render_command_encoder` -- Records draw commands: set pipeline, bind buffers, bind sampled images via `set_sampled_image()`, draw primitives (including multi-draw indirect).
- `Ring_buffer` -- Circular GPU buffer for streaming per-frame data with fence-based synchronization.
- `Shader_monitor` -- Watches shader source files and hot-reloads programs when files change.
- `Fragment_outputs` -- Describes fragment shader output declarations.
- `Surface` / `Swapchain` -- Window surface and swapchain management.

## Public API
- `Device device(surface_create_info)` -- Create the graphics device.
- `device.make_render_command_encoder()` -- Get a command encoder for drawing.
- `device.allocate_ring_buffer_entry(target, usage, size)` -- Allocate from ring buffer.
- `device.create_render_pipeline(Render_pipeline_create_info)` -- Up-front pipeline creation.
- `Buffer(device, create_info)` / `Texture(device, create_info)` -- Create GPU resources.
- `Bind_group_layout(device, create_info)` -- Create a descriptor layout plus synthesized sampler declarations.
- `Shader_stages(device, prototype)` -- Create shader program from compiled prototype.
- `Texture_heap(device, fallback_texture, fallback_sampler, bind_group_layout)` -- Create a material-texture heap bound to a layout.
- `Scoped_render_pass(render_pass)` -- RAII render pass begin/end.
- `Render_command_encoder::set_render_pipeline()` / `set_bind_group_layout()` / `set_sampled_image(binding_point, texture, sampler)` / `draw_indexed_primitives()` -- Issue draw calls.

## Frame lifecycle

A device frame is bracketed by `Device::wait_frame()` and
`Device::end_frame()`. In between, callers obtain one or more
`Command_buffer` instances from `Device::get_command_buffer(thread_slot)`,
optionally engage the window swapchain via
`Command_buffer::wait_for_swapchain` + `Command_buffer::begin_swapchain`,
record into them, and submit through `Device::submit_command_buffers`.

The contract for each entry point:

- `wait_frame()` -- pace the per-(frame_in_flight, thread_slot) command
  pools on the device's timeline semaphore. For frames that re-enter
  a slot (frame index `> N`) we wait until the timeline reports value
  `frame_index - N`, then `vkResetCommandPool` every per-thread pool
  for that slot. Each freed `Command_buffer` recycles its implicit
  fence + binary semaphore back into `Device_sync_pool`.
- `get_command_buffer(thread_slot)` -- allocate a fresh primary cb
  from the slot's per-thread pool. The wrapper carries an implicit
  fence + binary semaphore (used by `Command_buffer::wait_for_fence`
  / `wait_for_semaphore` / `signal_*`). Lifetime is tied to the next
  pool reset for this slot.
- `Command_buffer::wait_for_swapchain(out_frame_state)` /
  `begin_swapchain(info, out_frame_state)` -- wait on the next
  swapchain image to be acquirable, then acquire it and bind the
  per-slot acquire / present semaphores to this cb. The cb tracks
  the swapchain it engaged so the submit can attach the right
  semaphores and drive the implicit present.
- `Command_buffer::begin()` / `end()` -- bracket recording
  (`vkBeginCommandBuffer` / `vkEndCommandBuffer` on Vulkan). One cb
  per render pass is the recommended pattern.
- `submit_command_buffers(span)` -- submit. Walks each cb,
  performs CPU-side `vkWaitForFences` for any
  `wait_for_fence(other)` registered, splices the wait/signal
  semaphores into a single `VkSubmitInfo2`, runs `vkQueueSubmit2`,
  and finally drives `vkQueuePresentKHR` / `swap_buffers` /
  `presentDrawable` on every swapchain whose cb engaged it via
  `begin_swapchain`. Implicit present is the rule: there is no
  separate `Device::present` call.
- `end_frame()` -- **advance the frame index. That is the entire
  contract.** Does not submit, does not present. Mechanically it
  signals the device timeline semaphore at the current frame index
  via an empty `vkQueueSubmit2` (so `wait_frame` on the next visit
  to a slot can consume that value) and increments
  `m_frame_index`. The legacy `end_frame(Frame_end_info)` overload
  is identical -- the argument is ignored, kept only for migration.

There is no `Device::present()`. Present is implicit in
`submit_command_buffers`; if you didn't run `begin_swapchain` on a
cb, no swapchain was engaged and nothing is presented.

## Bind group layout

`Bind_group_layout` is the single source of truth for the descriptor / binding
layout of a shader program. It is constructed from a `Bind_group_layout_create_info`
that lists every resource the shaders reference:

```cpp
Bind_group_layout_binding {
    uint32_t       binding_point;
    Binding_type   type;              // uniform_buffer | storage_buffer | combined_image_sampler
    uint32_t       descriptor_count;

    // combined_image_sampler only
    Sampler_aspect sampler_aspect;    // color / depth / stencil
    std::string_view name;            // e.g. "s_shadow_compare"
    Glsl_type        glsl_type;       // e.g. sampler_2d_array_shadow
    bool             is_texture_heap; // true for bindless / sampler-array / argument-buffer
    uint32_t         array_size;      // array length when is_texture_heap, else 0
};
```

The backend-specific `Bind_group_layout_impl` constructor consumes this list and
does two things:

1. Builds the host-side descriptor layout (VkDescriptorSetLayout + VkPipelineLayout
   on Vulkan, argument-encoder slot plan on Metal, texture-unit offset tracking on
   GL). The `sampler_binding_offset` is computed from the buffer bindings so the
   sampler namespace does not collide with buffers (Vulkan / Metal).
2. Synthesizes a private `Shader_resource` of type `samplers` -- the "default
   uniform block" -- by mirroring every `combined_image_sampler` binding into
   `Shader_resource::add_sampler()`. The block is exposed via
   `Bind_group_layout::get_default_uniform_block()` and consumed by:
   - the shader source emitter in `shader_stages_create_info.cpp`, which injects
     `uniform sampler2D s_foo;` declarations into the GLSL preamble;
   - the Metal shader-stages prototype, which classifies each sampler as
     texture-heap or dedicated via `Shader_resource::get_is_texture_heap()` and
     routes it to the argument buffer or discrete set 0 accordingly;
   - the OpenGL `<4.30` fallback, which uses the `Shader_resource` member list
     to call `glUniform1i()` for each sampler when `layout(binding = N)` is not
     available.

Application code does not construct or mutate this block directly. Adding a
`combined_image_sampler` binding is the only way to declare a sampler in a
shader; the name / type / is_texture_heap information lives on the binding.

On the GL sampler-array and Metal argument-buffer paths, if no binding is
marked `is_texture_heap=true` and no binding is already named `s_texture`,
the impl appends an implicit `s_texture` array sized to the leftover texture
units. This is a convenience for callers (scene renderer, hextiles) that
just want "all leftover units available for material textures". Callers that
use a custom name or size (imgui's `s_textures`, rendering_test cells)
declare an explicit texture-heap binding and the implicit add is skipped.

## Texture heap

`Texture_heap` is the runtime allocation pool for material textures. It has one
of four backend strategies, selected at device init via `Device_info::texture_heap_path`:

| Path | How the shader samples | Where the handle comes from |
|---|---|---|
| `opengl_bindless_textures` | `sampler2D(handle)` inline | 64-bit bindless handle returned by `allocate()` and made resident by `bind()` |
| `opengl_sampler_array` | `s_texture[index]` array | Index returned by `allocate()` is an offset into the uniform sampler array |
| `vulkan_descriptor_indexing` | `erhe_texture_heap[index]` | Index into a descriptor-indexing array at set 1, binding 0 |
| `metal_argument_buffer` | `s_texture[index]` array (in MSL argument buffer) | Index into the argument buffer at `[[buffer(14)]]` |

The public API is backend-neutral:

```cpp
Texture_heap heap{device, fallback_texture, fallback_sampler, &bind_group_layout};
heap.reset_heap();
uint64_t handle = heap.allocate(texture, sampler);
heap.bind();      // makes handles resident / updates descriptors / encodes arg buffer
heap.unbind();    // releases residency / clears bindings
```

The `Bind_group_layout*` parameter is how each backend derives its runtime
state without the caller having to know the layout details:

- **GL sampler-array** reads `get_texture_heap_base_unit()` / `get_texture_heap_unit_count()`
  from the layout impl to know where the heap's texture-unit range begins. When
  a caller declares an explicit texture-heap `combined_image_sampler` binding,
  the base unit and count come from *that binding's* `binding_point` and
  `array_size`. Otherwise they come from "one past the last dedicated sampler"
  and "whatever is left in `max_per_stage_descriptor_samplers`".
- **GL bindless** ignores the offset; handles are opaque 64-bit values.
- **Vulkan descriptor indexing** writes into its own descriptor pool; the layout
  parameter is used to locate the pipeline layout for set 1.
- **Metal argument buffer** reads `bind_group_layout->get_default_uniform_block()`
  to find the texture-heap sampler declaration and compute argument-buffer slot
  IDs. Dedicated samplers in the same layout go to discrete set 0 instead.

Handles returned from `allocate()` are written into the material UBO and
sampled in the shader via the helper macros in `erhe_texture.glsl`, which
wrap the four backend-specific expressions.

## Set_sampled_image vs. texture heap

There are two ways a shader can end up reading a texture, and the choice is
made per `combined_image_sampler` binding:

- **Dedicated sampler** (`is_texture_heap = false`). The caller binds the
  texture per draw via `encoder.set_sampled_image(binding_point, texture, sampler)`.
  On Vulkan it becomes `vkCmdPushDescriptorSetKHR`; on Metal it becomes
  `setFragmentTexture`/`setFragmentSamplerState`; on GL it becomes
  `glBindTextureUnit`/`glBindSampler`. Used for resources that change per-draw
  and are not in a heap: shadow maps (`s_shadow_compare`, `s_shadow_no_compare`),
  post-processing chains (`s_input`, `s_downsample`, `s_upsample`),
  text-renderer font (`s_texture`), etc.
- **Texture-heap sampler** (`is_texture_heap = true`). The caller drives the
  backing store through a `Texture_heap`. The shader indexes into the
  heap-visible sampler array; the handle/index is stored in the material UBO.
  Used for material texture arrays that change per-object rather than per-draw.

`Render_command_encoder::set_sampled_image()` reads the `Sampler_aspect` of
the matching binding to pick the correct image view aspect (`color` /
`depth` / `stencil`) -- a sampling view must commit to a single aspect per
VUID-VkDescriptorImageInfo-imageView-01976.

## Dependencies
- **erhe libraries:** `erhe::dataformat` (public), `erhe::item` (public), `erhe::utility` (public), `erhe::gl` (for OpenGL backend), `erhe::log`, `erhe::verify`, `erhe::profile`
- **External:** glm, OpenGL, Vulkan, or Metal (selected at CMake time)

## Notes
- All major types use the pimpl pattern (`*_impl` classes) to isolate backend-specific code. Backend implementations live in `gl/` (OpenGL), `vulkan/` (Vulkan), `metal/` (Metal), and `null/` (headless).
- The OpenGL backend includes a runtime compatibility layer for OpenGL 4.1 (macOS). DSA, SSBOs, compute shaders, persistent mapping, and texture views are emulated or gracefully degraded based on `Device_info` capability flags. See `doc/opengl41_compatibility.md`.
- `Shader_resource` is used to programmatically build GLSL interface declarations from C++, keeping shader sources and C++ code in sync without reflection. For sampler declarations it is an implementation detail of `Bind_group_layout`.
- `Reloadable_shader_stages` combines `Shader_stages_create_info` with a live `Shader_stages` for hot-reload via `Shader_monitor`.
- Enums in `enums.hpp` mirror Vulkan concepts (Buffer_target, Texture_type, Memory_usage, Texture_heap_path, Resolve_mode, etc.) to keep the API backend-neutral.
- The `Graphics_config` type is generated (see `generated/graphics_config.hpp`).
- See `doc/vulkan_backend.md` and `doc/metal_backend.md` for backend-specific design notes and historical implementation plans.