# Metal Graphics Backend

Stability: mostly stable

> This Metal backend was substantially created by Claude (Anthropic),
> with architectural guidance and review from Timo Suoranta.

## Overview

The Metal backend implements the `erhe::graphics` abstraction layer using Apple's Metal API via the `metal-cpp` C++ wrapper. It provides feature parity with the OpenGL backend for core rendering on macOS.

### Build Configuration

Select Metal backend via configure script:
```bash
scripts/configure_xcode_metal.sh
cmake --build build_xcode_metal --target editor --config Debug
```

CMake option: `ERHE_GRAPHICS_API=metal`, which defines `ERHE_GRAPHICS_API_METAL`. The metal-cpp headers are fetched via CPM from `bkaradzic/metal-cpp`.

## Architecture

### File Structure

All Metal-specific code lives in `src/erhe/graphics/erhe_graphics/metal/`:

| File | Purpose |
|------|---------|
| `metal_device.cpp/hpp` | MTLDevice creation, format properties, device info |
| `metal_command_buffer.cpp/hpp` | `Command_buffer_impl` owning a real `MTL::CommandBuffer` (begin/commit, completion handlers) |
| `metal_buffer.cpp/hpp` | MTLBuffer with StorageModeShared (persistent mapping) |
| `metal_texture.cpp/hpp` | MTLTexture creation, texture views for array slices |
| `metal_sampler.cpp/hpp` | MTLSamplerState with argument buffer support |
| `metal_render_pass.cpp/hpp` | MTLRenderPassDescriptor, encoder lifecycle, static binding map |
| `metal_render_pipeline.cpp/hpp` | Up-front render pipeline objects (mirror of the Vulkan `Render_pipeline`) |
| `metal_compute_pipeline.cpp/hpp` | Up-front compute pipeline objects (mirror of the Vulkan `Compute_pipeline`) |
| `metal_render_command_encoder.cpp/hpp` | Pipeline state, depth/stencil, draw calls, multi-draw-indirect |
| `metal_compute_command_encoder.cpp/hpp` | Compute dispatch (wide lines, storage-image LUT generation) |
| `metal_blit_command_encoder.cpp/hpp` | Buffer-to-texture copies |
| `metal_command_encoder.cpp/hpp` | UBO/SSBO buffer binding with identity index mapping |
| `metal_bind_group_layout.cpp/hpp` | Bind group layout (referenced by the binding-map machinery below) |
| `metal_shader_stages.cpp/hpp` | Binding map types (UBO/SSBO Metal index lookups) |
| `metal_shader_stages_prototype.cpp` | GLSL-to-SPIRV-to-MSL compilation, explicit resource bindings |
| `metal_texture_heap.cpp/hpp` | Argument buffer for texture arrays (sampler heap) |
| `metal_vertex_input_state.cpp/hpp` | Vertex descriptor setup |
| `metal_pixel_format_table.cpp/hpp` | Pixel format conversion table |
| `metal_helpers.cpp/hpp` | Enum conversion utilities |
| `metal_debug.cpp` | Debug labels |
| `metal_scoped_debug_group.cpp/hpp` | Scoped debug groups (pushDebugGroup/popDebugGroup) |
| `metal_gpu_timer.cpp/hpp` | `Gpu_timer` backend: counter sample buffer pool, per-command-buffer timestamp records |
| `metal_surface.hpp/mm` | CAMetalLayer integration with SDL |
| `metal_swapchain.cpp/hpp` | Swapchain with app-managed depth texture |
| `metal_implementation.mm` | metal-cpp implementation defines |
| `metal_error_handler.mm` | Objective-C error handler registration |

Note: the explicit `Command_buffer` API is fully implemented on Metal --
`Command_buffer_impl` owns a real `MTL::CommandBuffer` (this supersedes the
earlier migration note that described it as a stub).

### Shader Compilation Pipeline

GLSL source -> glslang (GLSL to SPIR-V) -> SPIRV-Cross (SPIR-V to MSL)

Compiled SPIR-V binaries are cached on disk under `<exe>/spirv_cache/`,
keyed by a hash of the final GLSL source (after preamble injection and
define expansion). Subsequent runs read from the cache instead of
re-invoking glslang.

Key SPIRV-Cross options:
- MSL version 3.0 (Metal 3 / `GPUFamilyMetal3` is required at runtime)
- `argument_buffers = true`, Tier 2
- `force_active_argument_buffer_resources = true`
- Descriptor set 0: discrete (UBOs/SSBOs as individual `[[buffer(N)]]`,
  plus dedicated samplers as `[[texture(N)]]`/`[[sampler(N)]]`)
- Descriptor set 7: argument buffer for texture-heap sampled images

### Explicit Metal Buffer Index Layout

All Metal buffer indices are explicitly assigned during SPIRV-Cross compilation via `add_msl_resource_binding()`, eliminating runtime remapping:

```
Index 0-7:   UBOs/SSBOs (= GLSL binding number, identity mapping)
Index 8-13:  (available)
Index 14:    Texture argument buffer
Index 15:    Push constant (ERHE_DRAW_ID, vertex/compute only)
Index 16+:   Vertex attributes (vertex_buffer_index_offset)
```

Push constant declaration is only emitted for vertex and compute shaders. Fragment shaders receive draw ID via interpolated varying.

### Two Sampler Binding Paths: Texture Heap vs. Dedicated Bindings

Sampled images take one of two completely different routes through the
Metal backend, distinguished by the `is_texture_heap` flag on each
`Bind_group_layout_binding` entry of type `combined_image_sampler`. The
caller sets the flag when declaring the binding; `Bind_group_layout_impl`
mirrors every binding into a private `Shader_resource` of type `samplers`
(the synthesized "default uniform block"), and the Metal shader-stages
prototype reads that block to classify each sampled image it finds in the
SPIR-V binary.

**Path A -- Texture-heap samplers** (`is_texture_heap = true`).

These are bindless / argument-buffer samplers like `s_textures[64]` for
material texture arrays, `s_texture_array` for the imgui sampler2DArray, or
the implicit `s_texture` array that `gl_bind_group_layout.cpp` /
`metal_bind_group_layout.cpp` append when a caller does not declare one
explicitly. They live in descriptor set 7 (an argument buffer at
`[[buffer(14)]]`) with deterministic `[[id(N)]]` values, and are populated
via `Texture_heap::allocate(texture, sampler)` + `Texture_heap::bind()`.

Layout formula (the texture heap and the shader emitter compute the same
result from the matching sampler declarations in the layout's default
uniform block, sorted by GLSL binding):

```
For each is_texture_heap sampler with array_size S:
  texture_id = running_offset
  sampler_id = running_offset + S
  running_offset += 2 * S
```

The classification is **explicit**, not derived from `array_size > 1` --
that historical heuristic mis-classified scalar dedicated samplers and
arrays of size 1.

**Path B -- Dedicated samplers** (`is_texture_heap = false`).

These are named samplers like `s_shadow_compare`, `s_input`, `s_depth`, or
`s_tex0/1/2`. They stay in discrete set 0 with explicit
`[[texture(N)]]/[[sampler(N)]]` indices. The host side binds them via:

```cpp
encoder.set_sampled_image(/*binding_point=*/0, *texture, *sampler);
```

The Metal implementation translates `binding_point` to the per-stage Metal
slot via `binding_point + bind_group_layout->get_sampler_binding_offset()`
(which mirrors how SPIRV-Cross numbers them after the buffer bindings) and
calls `setFragmentTexture` / `setFragmentSamplerState`.

`set_sampled_image` reads the `Sampler_aspect` (`color` / `depth` /
`stencil`) from the matching `combined_image_sampler` entry in the active
`Bind_group_layout`. The aspect is not strictly needed on Metal (the
texture object encodes the format), but it has to match what the bind
group layout declared so the cross-backend path matches Vulkan.

Renderers using the dedicated-sampler path: `Light_buffer` (shadow maps),
`post_processing` (`s_input`/`s_downsample`/`s_upsample`), `text_renderer`
(`s_texture`, optional `s_vertex_data`), `imgui_renderer`
(`s_texture_array`).

### Texture Heap

The texture heap manages Path A samplers through Metal argument buffers.

- Constructor takes a `Bind_group_layout*` and reads
  `bind_group_layout->get_default_uniform_block()` to discover the
  texture-heap samplers. Iterates only the members with
  `get_is_texture_heap() == true` when computing argument buffer slot
  layout. Dedicated samplers are skipped -- they are bound via
  `set_sampled_image` instead.
- Argument buffer uses ring buffer allocation (fresh region per encode, no
  aliasing across render passes). A ring buffer range stays valid only
  through the frame that allocated it, so the argument buffer is encoded
  again in every frame that binds the heap, not only when its slots change.
- `allocate(texture, sampler)` returns a material index into the
  texture-heap sampler's array; the shader reads this index from the
  material UBO and samples via `s_textures[index]` / equivalent.
- `bind()` encodes when the slots changed or the current argument buffer
  was encoded in an earlier frame, then calls `setFragmentBuffer` +
  `useResource(Read | Sample)`.
- No pipeline change detection or re-encoding -- layout is fixed at
  construction.
- Material textures occupy `[0, used_slot_count)` in the argument buffer.
  There are no "reserved slots" for named samplers anymore -- dedicated
  samplers (shadow, post-process inputs, etc.) live in discrete set 0 and
  never consume argument-buffer slots.

### Coordinate Conventions

| Property | Metal | OpenGL (with clip control) | Vulkan |
|----------|-------|---------------------------|--------|
| NDC Y direction | Up | Up | Down |
| NDC depth range | [0, 1] | [0, 1] | [0, 1] |
| Framebuffer origin | Top-left | Bottom-left | Top-left |
| Texture origin | Top-left | Bottom-left | Top-left |
| Viewport Y transform | `(1 - y_ndc) * h/2 + oy` | `(y_ndc + 1) * h/2 + oy` | `(y_ndc + 1) * h/2 + oy` |
| Clip space Y flip needed | No | No | Depends |

Metal's viewport transform uses `window_y = (1 - y_ndc) * height/2 + originY`, which maps positive NDC Y to the top of the screen despite the top-left framebuffer origin. This means **no projection Y-flip is needed** for Metal -- the standard OpenGL-convention projection matrix works directly.

The `erhe::math::Coordinate_conventions` class bundles all convention parameters and is passed through the rendering pipeline as a single value:

```cpp
class Coordinate_conventions
{
public:
    Depth_range        native_depth_range;  // [0,1] or [-1,1]
    Framebuffer_origin framebuffer_origin;  // top-left or bottom-left
    Texture_origin     texture_origin;      // top-left or bottom-left
    Clip_space_y_flip  clip_space_y_flip;   // disabled for OpenGL and Metal
};
```

`Clip_space_y_flip` replaces the previous `Ndc_y_direction` enum. It directly answers the question "does the projection need to negate Y?" rather than requiring the application to derive this from framebuffer origin and NDC direction. For Metal, it is `disabled` because the viewport transform handles the Y mapping.

Convention-dependent adjustments in the codebase:

- **Projection matrix**: negates Y column when `clip_space_y_flip == enabled` (currently no backend needs this)
- **Winding order**: `Rasterization_state::with_winding_flip_if()` compensates for the winding reversal caused by projection Y-flip
- **Screen-space math**: `unproject()` and `project_to_screen_space()` negate the Y scale term for `Framebuffer_origin::top_left` to match the viewport transform inverse
- **Mouse input**: `get_viewport_from_window()` flips Y only for `bottom_left` framebuffer origin
- **Shadow maps**: `texture_from_clip` matrix accounts for framebuffer origin
- **Text renderer**: applies `y_scale` based on framebuffer origin for glyph orientation
- **Wide line compute shaders**: `vp_y_sign` uniform adjusts screen-space coordinates for fragment shader `gl_FragCoord` matching
- **ImGui renderer**: `get_rtt_uv0()`/`get_rtt_uv1()` flip UVs for render-to-texture based on texture origin

### Texture Clear

Metal's `clear_texture` for color textures uses `MTL::Texture::replaceRegion()` (shared/managed storage) or a staging buffer blit via `MTL::BlitCommandEncoder` (private storage). This avoids requiring `MTLTextureUsageRenderTarget` on textures that are only sampled. Depth/stencil clears use a render pass with `MTLLoadActionClear`.

### Stencil Support

Metal `DepthStencilDescriptor` configures front/back face stencil operations,
compare functions, read/write masks. Stencil reference values set on the
render command encoder via `setStencilFrontReferenceValue` /
`setStencilBackReferenceValue`.

`Device::choose_depth_stencil_format()` honours
`format_flag_require_stencil` and returns `format_d32_sfloat_s8_uint` when
the caller asks for a packed depth+stencil format. The
`Render_pipeline_create_info::set_format_from_render_pass()` swapchain
branch picks up the stencil format from `desc.stencil_attachment` when set.
Leaving it `Invalid` there is what Metal pipeline validation flags as a
stencil format mismatch.

### MSAA Depth/Stencil Resolve

Metal supports resolving the depth and stencil aspects of an MSAA texture
into a single-sample texture via `MTLMultisampleDepthResolveFilter` /
`MTLMultisampleStencilResolveFilter`. The `erhe::graphics::Resolve_mode`
enum (`sample_zero` / `average` / `min` / `max`) maps to these filters.

### Swapchain

Metal swapchain provides only a color texture (from CAMetalLayer's nextDrawable). The depth-stencil texture is app-managed, created alongside the swapchain and recreated on resize.

`Swapchain_impl` serves both window configurations. With `ERHE_WINDOW_LIBRARY=sdl`
the context window hands out an SDL Metal view, `Surface_impl` owns the
`CAMetalLayer`, and each frame draws into a layer drawable which
`Device_impl::submit_command_buffers` hands to `presentDrawable`.

With `ERHE_WINDOW_LIBRARY=none` there is no layer (`Surface_impl::is_headless()`),
and the swapchain renders into an emulated backbuffer instead: a ring of three
offscreen `MTL::Texture` color targets in the layer format
(`BGRA8Unorm_sRGB`, render target + shader read, private storage), sized from
`Context_window::get_width/get_height` and created once at construction.
`begin_frame` advances the ring index, `get_current_color_texture` hands the
current ring texture to the swapchain render pass exactly where the drawable
texture goes, no drawable is acquired so nothing is presented, and frame pacing
is the device's own completion tracking. This mirrors the Vulkan backend's
`Emulated_swapchain_impl`. Configure it with
`scripts/configure_xcode_metal_headless.sh`; the requirements and the
verification list live in [`metal-headless.md`](metal_headless.md).

Screenshot capture (`Device::capture_last_frame`) differs accordingly. Windowed,
a drawable cannot be read after presentation, so capture is armed
(`Device::request_frame_capture`) and the swapchain render pass epilogue blits
the composited drawable into a persistent shared buffer on the frame's own
command buffer. Headless, the ring textures stay owned and readable, so
`request_frame_capture` is a no-op and `capture_last_frame` blits the most
recently composited ring texture on a fresh command buffer and waits for it,
returning the frame synchronously on the first call.

### Multi-Draw Indirect

Metal does not support `glMultiDrawElementsIndirect`. The implementation loops over individual `drawIndexedPrimitives` calls, reading draw commands from the indirect buffer on the CPU side. Push constant index 15 carries the draw ID for vertex/compute shaders; fragment shaders receive draw ID via an interpolated varying (Metal does not support push constants in fragment).

### GPU timers

Apple GPUs sample timestamps only at stage boundaries
(`MTL::CounterSamplingPointAtStageBoundary`; draw, dispatch and blit
boundaries are unsupported), so a timestamp is taken at the start or the end
of an encoder through the `sampleBufferAttachments` of the pass descriptor the
encoder is created from. While any `Gpu_timer` exists, every render pass
(start of vertex, end of fragment) and every compute encoder (start and end
of encoder) samples both boundaries into the command buffer's chunk of a
`MTL::CounterSampleBuffer` owned by `Gpu_timer_sample_pool` on `Device_impl`.
A sample buffer holds at most 32 KiB (4096 timestamps); the pool splits each
into chunks of 256 samples, one chunk per command buffer, and adds buffers on
demand up to eight.

A timer's `begin` resolves to the start of the encoder open on the command
buffer, or, with none open, the start of the next sampled encoder; its `end`
to the end of the open encoder, or, with none open, the end of the last
sampled encoder that ended. A render-pass bound timer is therefore exact.
An explicit range measures the whole encoders it brackets, so ranges inside
one compute encoder all read that encoder's time: code that times passes
separately gives each timed pass its own encoder (`Ddgi_renderer` blend and
relocate). An encoder without work samples nothing (reads 0); its timer keeps
its previous result.

`Device_impl::submit_command_buffers` hands the command buffer's records to a
completion handler, which resolves the chunk (`resolveCounterRange`) on a
Metal dispatch thread, stores each duration into its timer (looked up by
serial in the pool's registry, so a timer destroyed meanwhile is skipped) and
returns the chunk. GPU ticks convert to nanoseconds by the ratio of
`MTL::Device::sampleTimestamps` CPU and GPU deltas since the pool was created
(1:1 on Apple silicon).

### Deferred GPU Resource Destruction

Metal resources (buffers, textures, samplers, render pipelines, depth-stencil
states, argument encoders, etc.) may still be referenced by command buffers
that have not finished executing on the GPU. The Metal backend uses the
same `add_completion_handler()` pattern as the Vulkan backend: destructors
capture handles by value into a lambda and defer release until the device
shuts down. Releasing a resource that an in-flight command buffer still
references is what this prevents - it shows up as intermittent shutdown
crashes.

### Thread safety

The dedicated render thread and frame pipelining (`doc/editor/render_thread.md`)
run M2 of frame N+1 on the main thread while the render thread records,
submits, presents and ends frame N. Every Metal `Device_impl` entry point
both roles reach has the guards of `doc/erhe/graphics.md` "Thread safety":
a mutex held only for bookkeeping (none across a GPU wait), a split per
frame or command buffer, or one owning role. Metal's `MTL::Device`,
`MTL::CommandQueue` creation calls and `commit` are thread-safe; a command
buffer and its encoders are recorded by one thread.

| Entry point | Members touched | Guard |
|---|---|---|
| `acquire_frame_slot` (main, M2) | `m_prepare_frame_id` (write), `m_render_frame_id` (read), the slot's state | `m_frame_id_mutex`; asserts the main thread |
| `begin_frame`, `end_frame` (render, R) | the slot's state, `m_render_frame_id` (write) | asserts the render thread; the slot turns idle and the id advances together under `m_frame_id_mutex` (the render role reads its own id unlocked) |
| frame retirement (`end_frame`; `wait_idle`) | `m_frame_command_buffer_counts`, `m_latest_completed_frame`; ring buffer pool; completion handlers; the retired frames' `Command_buffer`s | `m_completion_run_mutex` (one retiring thread); `m_completion_mutex`; the pool's mutex; `m_completion_handler_mutex`, released while handlers run; `m_per_thread_mutex`. `wait_idle` retires only the frames that had ended before its wait (bound read before the wait) |
| command buffer completion handlers (Metal dispatch threads) | per-frame completed counts; presented-frame count; GPU timer results | `m_completion_mutex`; `m_presented_frames_mutex` (+ condition variable); `Gpu_timer_sample_pool::m_mutex` |
| `get_frame_index`, `is_frame_ended`, `is_frame_completed` (any) | frame ids; `m_latest_completed_frame` | role-keyed as Vulkan; `m_frame_id_mutex`; `m_completion_mutex` |
| `add_completion_handler` (any) | `m_completion_handlers` | `m_completion_handler_mutex`; stamped with the calling role's frame |
| `get_command_buffer` (both) | `m_per_thread_command_buffers` | `m_per_thread_mutex`; a fresh `MTL::CommandBuffer` per call |
| `submit_command_buffers`, `submit_command_buffer_and_wait` (both) | the cbs' sync registrations; the presented-frame count; per-frame committed counts | per command buffer; `m_presented_frames_mutex`; `m_completion_mutex`. Queue order follows commit order across threads |
| encoders (`Render_pass_impl`, `Compute_command_encoder_impl`) | the active render pass / compute encoder | `thread_local` trackers (`s_active_render_pass`, `s_active_encoder`); per command buffer state |
| `allocate_ring_buffer_entry`, `Ring_buffer_client::acquire` | `Ring_buffer_pool` | the pool's mutex and per-role lists (`doc/erhe/ring_buffer_memory.md`) |
| `Texture_heap` bind | the heap's argument encoder and argument buffer | one role per heap (per-frame material set heaps; text, ImGui and texel renderer heaps bind in R) |
| `Gpu_timer` create, record, resolve, destroy | sample chunks, timer registry, calibration; the timer's result | `Gpu_timer_sample_pool::m_mutex` ("GPU timers"); records per command buffer; `Gpu_timer_impl::m_result_mutex` |
| swapchain (`begin_swapchain`, drawable, present) | `Swapchain_impl` drawable and emulated ring index | render role only |
| `capture_last_frame` / `request_frame_capture` (main) | windowed: the capture request, buffer and carrying cb; headless: the composited frame list | `m_capture_mutex`; `m_composited_mutex`. Headless reads the newest composited frame that has ended |
| pipeline states, depth-stencil states, samplers, textures, buffers | new Metal objects | created per call (no backend caches); destruction deferred with `add_completion_handler` |
| `get_info`, format queries | device properties | written during construction only |

Lock order, outermost first: `m_completion_run_mutex`, the ring buffer pool's
mutex, a ring buffer's mutex, then the leaves (`m_completion_mutex`,
`m_completion_handler_mutex`, `m_frame_id_mutex`, `m_per_thread_mutex`,
`m_presented_frames_mutex`, `m_capture_mutex`, `m_composited_mutex` before
`m_frame_id_mutex`, `Gpu_timer_sample_pool::m_mutex` before
`Gpu_timer_impl::m_result_mutex`).

`acquire_frame_slot` does not wait for frame `frame_id - N` on the GPU (the
Vulkan timeline wait): Metal has no per-slot command pools to reset, GPU
memory is reused through completion-tracked ring buffers and
`is_frame_completed`, and the CPU is paced by the presented-frame bound in
`begin_swapchain`. The headless build has no such bound
(`doc/plans/metal_backend.md` item 7).

## Future work

- [plans/metal_backend.md](../plans/metal_backend.md): `blit_framebuffer`, swapchain resize, present wait, format sorting, GPU multi-draw indirect
