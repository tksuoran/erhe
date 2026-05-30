# erhe::graphics test coverage

A manually-maintained matrix of the `erhe::graphics` public API surface against the
real-GPU tests in `src/erhe/graphics/test/` (target `erhe_graphics_gpu_tests`, headless
Vulkan). Update a row whenever a test starts or stops exercising a feature. This is an
API-surface checklist, not an automated line-coverage report (line coverage via
OpenCppCoverage / llvm-cov could be layered on later if wanted).

See `doc/graphics_compute_testing_plan.md` for the harness design and milestones.

## Running

```
scripts\configure_vs2026_vulkan_headless.bat -DERHE_BUILD_TESTS=ON
cmake --build build_vs2026_vulkan_headless --target erhe_graphics_gpu_tests --config Debug
ctest --test-dir build_vs2026_vulkan_headless -C Debug -R Gpu_test --output-on-failure
```

Legend: `[x]` covered, `[~]` partial, `[ ]` gap.

## Device (`device.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| construct/destruct headless under validation | `[x]` | device_up_clean |
| `get_info()` reports a backend | `[x]` | device_up_clean |
| frame lifecycle (wait_frame/get_command_buffer/submit/wait_idle/end_frame) | `[x]` | harness `submit_and_wait` (all) |
| `make_render_command_encoder` | `[x]` | triangle_draw_coverage, blend, depth |
| `make_compute_command_encoder` | `[x]` | compute_writes_ssbo_pattern |
| `make_blit_command_encoder` | `[x]` | harness `read_texture_rgba8` (readbacks) |
| `create_render_pipeline` / direct `Render_pipeline` | `[x]` | triangle/blend/depth |
| `get_reverse_depth()` | `[x]` | depth_test_nearer_wins |
| capability flags (compute, storage buffers) | `[~]` | compute test (skips); broader audit `[ ]` |
| `probe_image_format_support` / `get_format_properties` | `[ ]` | - |
| `choose_depth_stencil_format` | `[ ]` | - |

## Texture (`texture.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| 2D color target (8_vec4_unorm) | `[x]` | make_color_target (M2+) |
| depth target (d32_sfloat) | `[x]` | depth_test_nearer_wins |
| `upload_to_texture` | `[ ]` | - |
| `clear_texture` | `[ ]` | - |
| sample in shader (`set_sampled_image`) | `[ ]` | - |
| other color formats (float, snorm, ...) | `[ ]` | - |
| mipmaps / `generate_mipmaps` | `[ ]` | - |
| array / 3D / cube textures | `[ ]` | - |
| MSAA texture + resolve | `[ ]` | - |

## Buffer (`buffer.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| host-visible mappable + map/unmap readback | `[x]` | harness `read_buffer` |
| storage (SSBO) write from compute | `[x]` | compute_writes_ssbo_pattern |
| `upload_to_buffer` | `[x]` | buffer_upload_download |
| buffer-to-buffer copy (`copy_from_buffer`) | `[x]` | buffer_to_buffer_copy |
| `fill_buffer` | `[x]` | fill_buffer_constant |
| vertex buffer (`set_vertex_buffer`) | `[ ]` | - |
| index buffer (`set_index_buffer`) | `[ ]` | - |
| uniform buffer (UBO) | `[ ]` | - |

## Render_pass (`render_pass.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| color attachment Clear/Store + readback | `[x]` | clear_color_offscreen_readback |
| depth attachment + clear | `[x]` | depth_test_nearer_wins |
| stencil attachment | `[ ]` | - |
| multiple color attachments (MRT) | `[ ]` | - |
| Load_action::Load (preserve) | `[ ]` | - |
| MSAA resolve attachment | `[ ]` | - |

## Render_command_encoder (`render_command_encoder.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| set_viewport_rect / set_scissor_rect | `[x]` | triangle/blend/depth |
| set_render_pipeline | `[x]` | triangle/blend/depth |
| set_bind_group_layout | `[x]` | triangle/blend/depth |
| draw_primitives (triangle) | `[x]` | triangle/blend/depth |
| draw_primitives instanced | `[ ]` | - |
| draw_indexed_primitives | `[ ]` | - |
| set_vertex_buffer / set_index_buffer | `[ ]` | - |
| set_sampled_image | `[ ]` | - |
| other primitive types (points/lines/strips) | `[ ]` | - |

## Compute_command_encoder (`compute_command_encoder.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| set_compute_pipeline + dispatch_compute | `[x]` | compute_writes_ssbo_pattern |
| storage buffer binding | `[x]` | compute_writes_ssbo_pattern |
| uniform buffer input | `[ ]` | - |
| multi-dimensional workgroups | `[ ]` | - |

## Pipeline state (`state/*.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| Input_assembly: triangle | `[x]` | triangle/blend/depth |
| Input_assembly: strip/line/point | `[ ]` | - |
| Rasterization: cull none | `[x]` | triangle/blend/depth |
| Rasterization: cull back/front, front face | `[ ]` | - |
| Rasterization: polygon mode line/point | `[ ]` | - |
| Depth_stencil: depth test | `[x]` | depth_test_nearer_wins |
| Depth_stencil: stencil test | `[ ]` | - |
| Color_blend: disabled | `[x]` | triangle/depth |
| Color_blend: alpha over | `[x]` | blend_alpha_over |
| Color_blend: premultiplied / other | `[ ]` | - |
| Color_blend: write mask | `[ ]` | - |
| Multisample | `[ ]` | - |

## Shader_stages (`shader_stages.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| inline GLSL vertex+fragment | `[x]` | triangle/blend/depth |
| inline GLSL compute | `[x]` | compute_writes_ssbo_pattern |
| defines | `[x]` | compute (ELEMENT_COUNT), depth (NEAR/FAR_Z) |
| fragment_outputs (single) | `[x]` | triangle/blend/depth |
| fragment_outputs (multiple, MRT) | `[ ]` | - |
| interface_blocks (SSBO) | `[x]` | compute_writes_ssbo_pattern |
| interface_blocks (UBO) | `[ ]` | - |
| struct_types | `[ ]` | - |
| no_vertex_input | `[x]` | triangle/blend/depth |
| vertex_format / vertex attributes | `[ ]` | - |

## Blit_command_encoder (`blit_command_encoder.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| copy_from_texture (texture -> buffer) | `[x]` | harness `read_texture_rgba8` |
| copy_from_buffer (buffer -> buffer) | `[x]` | buffer_to_buffer_copy |
| copy_from_buffer (buffer -> texture) | `[ ]` | - |
| copy_from_texture (texture -> texture) | `[ ]` | - |
| fill_buffer | `[x]` | fill_buffer_constant |
| generate_mipmaps | `[ ]` | - |

## Sampler (`sampler.hpp`) / Bind_group_layout (`bind_group_layout.hpp`)

| Feature | Status | Test(s) |
|---|---|---|
| Bind_group_layout: empty | `[x]` | triangle/blend/depth |
| Bind_group_layout: storage_buffer | `[x]` | compute_writes_ssbo_pattern |
| Bind_group_layout: uniform_buffer | `[ ]` | - |
| Bind_group_layout: combined_image_sampler | `[ ]` | - |
| Sampler create + filter/address modes | `[ ]` | - |
