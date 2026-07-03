# erhe::graphics GPU test coverage

This matrix tracks real-GPU coverage exercised by `erhe_graphics_gpu_tests`. The
target builds and runs on headless Vulkan / lavapipe (41/41) and on non-headless
OpenGL (40 passed + 1 capability skip, no failures); Metal builds but still needs
macOS validation (see
[`graphics_test_nonheadless_port.md`](graphics_test_nonheadless_port.md)). Each row
maps to one or more `TEST_F(Gpu_test, ...)` cases.
`[x]` = covered, `[ ]` = gap, `[-]` = not testable on this device (a device/engine
limitation, not a coverage gap to fill).

## Device / infrastructure

- [x] Device creation and info query (`test_m1_device_up.cpp`)
- [x] Device capability audit: depth/stencil format selection, format-properties vs probe agreement, Device_info self-consistency (`test_device_caps.cpp`)
- [x] Color attachment clear (`test_m2_clear_color.cpp`)

## Rasterization / draw

- [x] Triangle raster, fullscreen (`test_m3_triangle.cpp`)
- [x] Raster state: cull mode + color write mask (`test_raster_state.cpp`)
- [x] Depth test less/greater (`test_m5_depth.cpp`)
- [x] Color blend, straight-alpha over (`test_m5_blend.cpp`)
- [x] Color blend, premultiplied-alpha over (`test_blend_premultiplied.cpp`)
- [x] Multiple render targets / MRT (`test_mrt.cpp`)
- [x] Indexed vertex-buffer draw (`test_vertex_index.cpp`)
- [x] Instanced draw, triangle_strip topology (`test_instanced.cpp`)
- [x] Primitive topology: point_list and line_list (`test_topology.cpp`)
- [x] Stencil: two-draw mask/test in a single render pass (`test_stencil.cpp`)
- [-] Polygon mode line/point - NOT testable here: lavapipe (the headless CI device) has `fillModeNonSolid` disabled, so `VK_POLYGON_MODE_LINE`/`VK_POLYGON_MODE_POINT` fail pipeline creation. This is an engine/device limitation, not a coverage gap to fill; no engine feature should be added solely to test it.

## Render pass

- [x] Load_action::Load preserves prior pass across two passes (`test_load_action.cpp`)
- [x] Multisample (4x MSAA) color render + average resolve to single-sample target (`test_msaa_resolve.cpp`)

## Compute

- [x] Compute writing an SSBO, 1D dispatch (`test_m4_compute_ssbo.cpp`)
- [x] Compute reading a uniform buffer (`test_compute_ubo.cpp`)
- [x] Compute 2D dispatch, group counts > 1 per dim over non-multiple extents (`test_compute_2d.cpp`)
- [x] User struct (`add_struct`) in a UBO interface block, `struct_types` wiring + std140 member offsets (`test_struct_types.cpp`)

## Buffers / transfer

- [x] Buffer upload + copy + fill (`test_buffer_transfer.cpp`)
- [x] Texture upload roundtrip + constant clear (`test_texture_upload.cpp`)
- [x] Texture sampling, nearest filter (`test_texture_sample.cpp`)
- [x] Sampler linear filter, two-texel midpoint interpolation (`test_sampler_modes.cpp`)
- [x] Sampler address modes: clamp_to_edge / repeat / mirrored_repeat (`test_sampler_modes.cpp`)
- [x] copy_from_buffer (buffer -> texture) (`test_copy.cpp`)
- [x] copy_from_texture (texture -> texture): whole-image copy (byte-exact roundtrip) and a non-zero-origin sub-rect copy with correct placement; source filled via copy_from_buffer, destination read back. Validates the engine fix that reads/restores the source's tracked layout and updates the destination's tracked layout (`test_copy_texture.cpp`)
- [x] Depth-texture readback: depth-only pass writes a known NDC depth, depth aspect copied to host (`test_depth_readback.cpp`)
- [x] Mipmap generation: generate_mipmaps linear-downsamples level 0 (half-black/half-white split averages to mid-gray at the 1x1 level; the 4x4 level keeps the vertical split) (`test_mipmaps.cpp`)
- [x] 2D array texture sampling: texture_2d_array with 3 distinct per-layer solid colors filled via copy_from_buffer destination_slice, each layer sampled through a sampler2DArray (layer via GLSL define) and verified (`test_texture_array.cpp`)
- [x] 3D texture sampling: 2x2x2 texture_3d filled in one copy_from_buffer, each voxel center sampled through a sampler3D (nearest) and verified against its distinct color (`test_texture_3d.cpp`)
- [x] Cube map (texture_cube_map) sampling: 6-face CUBE-compatible image (1x1 per face) with a distinct per-face color filled via copy_from_buffer destination_slice (Vulkan face order +X,-X,+Y,-Y,+Z,-Z), each face sampled through a samplerCube by its center direction vector (baked as a GLSL define) and verified; set_sampled_image builds a VK_IMAGE_VIEW_TYPE_CUBE view spanning all 6 layers (`test_texture_cube.cpp`)

## Color formats

- [x] float32 color render + readback: out-of-[0,1] values into format_32_vec4_float survive unclamped (`test_float32_render.cpp`)
- [x] snorm color render + readback: signed-normalized values into format_8_vec4_snorm decode to the written signs (`test_snorm_render.cpp`)

## Known gaps (not yet covered)

None remaining.

## Future work: CI ctest job (deferred)

The one unlanded piece of the original testing plan (the plan doc itself is
retired; this sketch is its surviving implementation guide). Deferred by
decision: it cannot be developed/verified from the (Windows) dev machine and
needs new infrastructure.

`.github/workflows/build.yml` currently only builds the editor and runs no
ctest. Add a job that configures `-DERHE_BUILD_TESTS=ON
-DERHE_GRAPHICS_API=vulkan -DERHE_WINDOW_LIBRARY=none`, installs a software
Vulkan (Mesa lavapipe on Ubuntu runners; SwiftShader as alternative), points
the loader at its ICD, verifies with `vulkaninfo`, then runs
`ctest -R erhe_graphics_gpu_tests`. Keep a separate
`ctest -R erhe_graphics_tests` (deviceless) job that needs no Vulkan.

Software Vulkan does not support every format/feature; tests must probe and
skip: `probe_image_format_support`, `get_format_properties`,
`get_supported_depth_stencil_formats` / `choose_depth_stencil_format`, and
`Device_info` flags. The Environment `SetUp` should `GTEST_SKIP()` (not fail)
if no Vulkan device can be created. Keep targets tiny (16x16, N~1000).
Optionally enable `VK_LAYER_KHRONOS_validation` in CI to keep the 0-VUID
guarantee under software.
