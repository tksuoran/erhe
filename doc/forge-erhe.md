# forge-gpu (SDL3 GPU) to erhe graphics API mapping

This document maps the SDL3 GPU API used by the forge-gpu lessons / skills
(`/d/forge-gpu`, a C99 + SDL3 GPU tutorial repo) onto the erhe graphics
abstraction (`src/erhe/graphics`, namespace `erhe::graphics`). It exists so that
techniques described in forge-gpu's `.claude/skills/` can be ported to erhe
without re-deriving the equivalence each time.

erhe is a thin, Vulkan-flavoured abstraction over Vulkan (default), OpenGL, and
Metal. SDL3 GPU is itself a thin abstraction over Vulkan / D3D12 / Metal. The two
share most concepts (command buffers, render passes, pipelines, transfer/copy,
cube textures, comparison samplers), so the mapping is mostly mechanical. The
differences worth internalising up front:

- **Shaders**: SDL3 GPU consumes pre-compiled bytecode (SPIR-V / DXIL / MSL) and
  selects a format at runtime. erhe authors **GLSL once** (`res/shaders/`) and the
  backend compiles it. Port HLSL shaders to GLSL; do not ship bytecode.
- **Per-frame data**: SDL3 GPU pushes small uniforms with
  `SDL_PushGPU*UniformData` and streams larger data through transfer buffers. erhe
  streams **all** per-frame uniform/storage data through a persistent **ring
  buffer** (`Ring_buffer` / `Ring_buffer_range`) and binds sub-ranges.
- **Resource binding**: SDL3 GPU binds textures/buffers per pass with positional
  `SDL_BindGPU*` calls. erhe binds through a **`Bind_group_layout`** (a descriptor
  set layout) plus an optional bindless **`Texture_heap`** for material textures.
- **Conventions**: erhe centralises reverse-Z and clip-space Y-flip as **device
  conventions** (`Device::get_reverse_depth()`, the clip-space-y-flip winding
  compensation). Do **not** hand-patch projection matrices (e.g. negating
  `proj.m[5]`) the way SDL3-GPU-specific code does; build projections through
  `erhe::scene::Projection` and let the device convention apply.

All erhe types below are in `namespace erhe::graphics` unless noted, with headers
under `src/erhe/graphics/erhe_graphics/`.

## Device and swapchain

| SDL3 GPU | erhe |
|---|---|
| `SDL_CreateGPUDevice(formats, debug, name)` | `Device` ctor (`device.hpp`) - takes `Surface_create_info`, `Graphics_config` |
| `SDL_GetGPUShaderFormats` / runtime format select | N/A - erhe compiles GLSL for the active backend |
| `SDL_GetGPUDeviceDriver` | `Device::get_info()` (`Device_info`) |
| `SDL_ClaimWindowForGPUDevice(device, window)` | window/surface wired through `erhe::window` + `Surface_create_info` |
| `SDL_SetGPUSwapchainParameters` / `SDL_GetGPUSwapchainTextureFormat` | swapchain managed by `Device`; format queried from the device |
| `SDL_AcquireGPUSwapchainTexture` | `Command_buffer::wait_for_swapchain` / `begin_swapchain` / `end_swapchain` |
| `SDL_DestroyGPUDevice` | `Device` dtor |

Device capabilities / limits that SDL3 GPU exposes ad hoc live on
`Device::get_info()` (`Device_info`): GLSL version, texture-heap path, max texture
sizes, MSAA, reverse-depth, etc.

## Command buffers and submission

| SDL3 GPU | erhe |
|---|---|
| `SDL_AcquireGPUCommandBuffer(device)` | `Device::get_command_buffer(thread_slot)`; `Command_buffer::begin()` |
| record passes into the command buffer | encoders from `Device::make_render_command_encoder` / `make_blit_command_encoder` / `make_compute_command_encoder` |
| `SDL_SubmitGPUCommandBuffer(cmd)` | `Command_buffer::end()` + `Device::submit_command_buffers({&cb})` |
| `SDL_SubmitGPUCommandBufferAndAcquireFence` | `Command_buffer` GPU/CPU wait/signal helpers (`wait_for_gpu`, `signal_cpu`, ...) |
| `SDL_WaitForGPUIdle` | `Device::wait_idle()` |
| `SDL_CancelGPUCommandBuffer` | drop the `Command_buffer` without submitting |

erhe encoders are RAII objects scoped to a `Command_buffer`; a render pass is
entered with a `Scoped_render_pass` (RAII begin/end).

## Render passes and targets

| SDL3 GPU | erhe |
|---|---|
| `SDL_BeginGPURenderPass(cmd, color[], n, depth)` | construct a `Render_pass` from a `Render_pass_descriptor`, then `Scoped_render_pass{render_pass, command_buffer}` |
| `SDL_GPUColorTargetInfo` | `Render_pass_descriptor::color_attachments[i]` (`Render_pass_attachment_descriptor`) |
| `SDL_GPUDepthStencilTargetInfo` | `Render_pass_descriptor::depth_attachment` / `stencil_attachment` |
| `.load_op` / `.store_op` | `Load_action` (`Dont_care`/`Clear`/`Load`), `Store_action` (`Dont_care`/`Store`/`Multisample_resolve`/...) |
| `.clear_color` / `.clear_depth` | `Render_pass_attachment_descriptor::clear_value` (`array<double,4>`) |
| `.layer_or_depth_plane` (cube face / array layer / 3D slice) | `Render_pass_attachment_descriptor::texture_layer` (becomes Vulkan `baseArrayLayer`); `.texture_level` for the mip |
| `SDL_SetGPUViewport` | `Render_command_encoder::set_viewport_rect` (+ `set_viewport_depth_range`) |
| `SDL_SetGPUScissor` | `Render_command_encoder::set_scissor_rect` |
| `SDL_EndGPURenderPass` | end of the `Scoped_render_pass` scope |

Rendering into a single cube face is `texture_layer = 6*cube + face` with the
texture created as `Texture_type::texture_cube_map` / `texture_cube_map_array`
(Vulkan face order +X, -X, +Y, -Y, +Z, -Z). erhe also adds first-class concepts
SDL3 GPU lacks at this layer: `view_mask` (Vulkan multiview), MSAA resolve
attachments, and a fragment-density-map attachment.

## Graphics pipelines

| SDL3 GPU | erhe |
|---|---|
| `SDL_CreateGPUGraphicsPipeline` + `SDL_GPUGraphicsPipelineCreateInfo` | `Device::create_render_pipeline(Render_pipeline_create_info)` -> `Render_pipeline`; or `Base_render_pipeline` (caches a `Render_pipeline` per render-pass format / blend / winding) |
| `.vertex_input_state` (`SDL_GPUVertexAttribute` / `VertexBufferDescription`) | `Vertex_input_state` (`state/vertex_input_state.hpp`) + `erhe::dataformat::Vertex_format` |
| `.rasterizer_state` (cull, fill, depth bias) | `Rasterization_state` (`state/rasterization_state.hpp`); presets like `cull_mode_none`, `cull_mode_front_ccw`, `.with_depth_bias()`, `.with_winding_flip_if(...)` |
| `.depth_stencil_state` | `Depth_stencil_state` (`state/depth_stencil_state.hpp`); `depth_test_enabled_stencil_test_disabled(reverse_depth)` |
| `.target_info.color_target_descriptions[].blend_state` | `Color_blend_state` (`state/color_blend_state.hpp`); presets `color_blend_disabled`, `color_writes_disabled`, `color_blend_premultiplied` |
| `.primitive_type` | `Input_assembly_state` (`Primitive_type`: `triangle`, `line`, ...) |
| `.target_info` formats (color + depth) | set from the render pass: `Render_pipeline_create_info::set_format_from_render_pass(descriptor)` |
| `SDL_ReleaseGPUGraphicsPipeline` | RAII (`Render_pipeline` dtor) |

`Base_render_pipeline::get_pipeline_for(render_pass_descriptor, color_blend,
shader_stages, vertex_input, vertex_format, front_face_flip)` is the idiomatic way
to get a concrete pipeline matching a given render pass - it handles the
format-hash caching and the Y-flip winding variant.

## Shaders

| SDL3 GPU | erhe |
|---|---|
| `SDL_CreateGPUShader` + pre-compiled SPIRV/DXIL/MSL | `Shader_stages` built from a `Shader_stages_prototype` / `Shader_stages_create_info`; GLSL source in `res/shaders/` |
| per-stage `num_samplers` / `num_uniform_buffers` / `num_storage_buffers` | declared once in a `Bind_group_layout` (see below); samplers appear in the generated default uniform block |
| HLSL `register(tN, spaceM)` / `register(bN, spaceM)` | GLSL `layout(binding = ...)`; binding points are erhe constants (`*_binding_point`) |
| compile-time `#define` variants | `Shader_stages_create_info::defines` (and erhe's `Shader_key` variant cache for the editor's scene shaders) |
| `SDL_ReleaseGPUShader` | RAII |

erhe builds the GLSL preamble (interface blocks, sampler declarations, fragment
outputs) from the `Bind_group_layout` and `Shader_resource` descriptions, so the
shader text only contains the body. `Reloadable_shader_stages` adds hot reload.

## Buffers and per-frame data

| SDL3 GPU | erhe |
|---|---|
| `SDL_CreateGPUBuffer` + `SDL_GPUBufferCreateInfo` (`USAGE_VERTEX/INDEX/...`) | `Buffer` + `Buffer_create_info` (`Buffer_usage` bitmask), or `Buffer_target` for the ring buffer |
| `SDL_CreateGPUTransferBuffer` + `Map`/`Unmap` | `Ring_buffer` / `Ring_buffer_range` (persistent mapped staging). `Device::allocate_ring_buffer_entry(target, usage, bytes)` |
| `SDL_PushGPUVertexUniformData` / `SDL_PushGPUFragmentUniformData` | write into a `Ring_buffer_range` span, then bind the sub-range as a UBO/SSBO (`Ring_buffer_client` wraps the common pattern) |
| copy pass: `SDL_UploadToGPUBuffer` / `SDL_UploadToGPUTexture` | `Blit_command_encoder::copy_from_buffer` (buffer->buffer or buffer->texture); `Command_buffer::upload_to_buffer` / `upload_to_texture` |
| `SDL_DownloadFromGPUTexture` | `Blit_command_encoder::copy_from_texture` |
| `SDL_GenerateMipmapsForGPUTexture` | `Blit_command_encoder::generate_mipmaps` |

Pattern: per frame, `acquire` a `Ring_buffer_range`, `memcpy`/`write` into its
span, `bytes_written` + `flush` + `close`, then `bind` the range to a binding
point. Reuse a `Ring_buffer_client` (see `light_buffer.cpp`, `camera_buffer`,
etc.) rather than creating buffers per frame.

## Textures and samplers

| SDL3 GPU | erhe |
|---|---|
| `SDL_CreateGPUTexture` + `SDL_GPUTextureCreateInfo` | `Texture` + `Texture_create_info` |
| `.type` `TEXTURETYPE_2D` / `CUBE` / `3D` / array | `Texture_type` (`texture_2d`, `texture_2d_array`, `texture_3d`, `texture_cube_map`, `texture_cube_map_array`, ...) |
| `.usage` `COLOR_TARGET` / `DEPTH_STENCIL_TARGET` / `SAMPLER` / `COMPUTE_STORAGE_*` | `usage_mask` = `Image_usage_flag_bit_mask::color_attachment` / `depth_stencil_attachment` / `sampled` / `transfer_src` / `transfer_dst` / ... |
| `.format` (`R8G8B8A8_UNORM`, `D32_FLOAT`, `R32_FLOAT`, ...) | `erhe::dataformat::Format` (`format_8_vec4_unorm`, `format_d32_sfloat`, `format_32_scalar_float`, ...) |
| `.layer_count_or_depth` (cube = 6) | `array_layer_count` (cube = 6; cube array = 6 * cubes), `depth` for 3D |
| `SDL_CreateGPUSampler` + `SDL_GPUSamplerCreateInfo` | `Sampler` + `Sampler_create_info` |
| `.min/mag_filter` `NEAREST`/`LINEAR` | `Filter::nearest` / `Filter::linear` |
| `.address_mode_*` `CLAMP_TO_EDGE`/`REPEAT`/... | `Sampler_address_mode::clamp_to_edge` / `repeat` / `mirrored_repeat` (one per axis) |
| comparison sampler (shadow) | `Sampler_create_info::compare_enable` + `compare_operation` |
| bindless textures | `Texture_heap` (backend-selected: GL bindless, GL sampler array, Vulkan descriptor indexing, Metal argument buffer) |

Cube sampling is verified end-to-end in `src/erhe/graphics/test/test_texture_cube.cpp`:
a `texture_cube_map` is filled per face via `copy_from_buffer`'s
`destination_slice`, and `set_sampled_image` builds a `VK_IMAGE_VIEW_TYPE_CUBE`
view so a `samplerCube` lookup by direction reaches each face.

## Binding and drawing (inside a render pass)

| SDL3 GPU | erhe (`Render_command_encoder`) |
|---|---|
| `SDL_BindGPUGraphicsPipeline` | `set_render_pipeline` (and `set_bind_group_layout` once) |
| `SDL_BindGPUVertexBuffers` | `set_vertex_buffer(buffer, offset, index)` |
| `SDL_BindGPUIndexBuffer` | `set_index_buffer(buffer)` |
| `SDL_BindGPUFragmentSamplers` / `BindGPUVertexSamplers` | `set_sampled_image(binding_point, texture, sampler)` |
| `SDL_BindGPU*StorageBuffers` / uniform binds | `set_buffer(Buffer_target, buffer, offset, length, index)` (or `Ring_buffer_client::bind`) |
| `SDL_SetGPUViewport` / `SDL_SetGPUScissor` | `set_viewport_rect` / `set_scissor_rect` |
| depth bias (pipeline state in SDL3 GPU) | `set_depth_bias(constant, slope, clamp)` (the pipeline must enable depth bias) |
| `SDL_DrawGPUPrimitives` | `draw_primitives(primitive_type, first, count[, instances])` |
| `SDL_DrawGPUIndexedPrimitives` | `draw_indexed_primitives(primitive_type, index_count, index_type, index_offset, instance_count)` |
| `SDL_DrawGPUIndexedPrimitivesIndirect` | `multi_draw_indexed_primitives_indirect(...)` |

## Compute (used by other forge lessons)

| SDL3 GPU | erhe |
|---|---|
| `SDL_CreateGPUComputePipeline` | `Compute_pipeline` (`compute_pipeline_state.hpp`) |
| `SDL_BeginGPUComputePass` / `End` | `Device::make_compute_command_encoder` (`Compute_command_encoder`) |
| `SDL_BindGPUComputePipeline` | `Compute_command_encoder::set_compute_pipeline` |
| `SDL_DispatchGPUCompute(x,y,z)` | `Compute_command_encoder::dispatch_compute(x,y,z)` |
| `SDL_PushGPUComputeUniformData` | ring-buffer range bound to the compute encoder |
| `SDL_GPUStorageBufferReadWriteBinding` (SSBO) | `Compute_command_encoder::set_buffer(Buffer_target::storage, ...)` |
| `SDL_GPUStorageTextureReadWriteBinding` (RW image) | `Compute_command_encoder::set_storage_image(binding, texture)` -- declare via `Binding_type::storage_image` + `Glsl_type::image_2d` + `image_format` in the `Bind_group_layout`; emits `layout(binding = N, <format>) uniform image2D` and binds `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` in `GENERAL` layout (Vulkan), `gl::bind_image_texture` (OpenGL 4.3+), or `MTL::ComputeCommandEncoder::setTexture` (Metal). |
| HLSL `RWTexture2D` `imageStore` / `Texture2D.Sample` in compute | GLSL `imageStore` / `imageLoad` on the `image2D`; compute has no sampled-image binding, so read other storage images with `imageLoad` (+ manual bilinear) rather than a sampler |
| compute storage-image needs `COMPUTE_STORAGE_WRITE` + `SAMPLER` | `Image_usage_flag_bit_mask::storage \| sampled` on the `Texture_create_info` |
| compute->compute / compute->fragment image hazard (`SDL` synchronizes between passes) | `Command_buffer::transition_texture_layout(tex, Image_layout::general` then `::shader_read_only_optimal)` around the dispatches, plus `Command_buffer::memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit)` between two compute passes that share a `GENERAL`-layout image |
| one-time LUT compute + `SDL_WaitForGPUIdle` before first frame | dispatch into the per-frame command buffer once (guard a `bool ready`), before the render pass begins; erhe's barriers serialize -- no explicit idle wait. Precedent: `editor::Sky_renderer::ensure_luts` |

Storage-image compute is implemented on Vulkan, OpenGL (GL 4.3+ for compute /
image load-store; `gl_compute_command_encoder.cpp` uses `gl::bind_image_texture`),
and Metal (`metal_compute_command_encoder.cpp` uses `setTexture`; assumes Tier-2
read-write `RGBA16Float`, i.e. Apple Silicon). Features built on it should still
query a capability and fall back (the procedural-sky atmosphere uses
`Sky_renderer::is_atmosphere_supported()` and renders the gradient sky otherwise).
Precedent for the whole compute-LUT pattern: `doc/procedural_sky.md`.

## Porting checklist

1. Translate HLSL shaders to GLSL; declare resources via a `Bind_group_layout`
   instead of HLSL register spaces. Do not pre-compile bytecode.
2. Replace `SDL_Push*UniformData` and transfer buffers with a `Ring_buffer_client`
   per logical buffer; write the range, bind the range.
3. Build projections through `erhe::scene::Projection` and depth state through
   `Depth_stencil_state::...(reverse_depth)`; let reverse-Z and clip-space Y-flip
   come from the device convention. Drop SDL3-GPU-specific matrix patches.
4. Render-to-layer / render-to-cube-face: set `Render_pass_attachment_descriptor::texture_layer`.
5. Get concrete pipelines from `Base_render_pipeline::get_pipeline_for(...)` so the
   render-pass format and winding variants are handled for you.
