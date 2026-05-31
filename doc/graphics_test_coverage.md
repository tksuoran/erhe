# erhe::graphics GPU test coverage

This matrix tracks real-GPU coverage exercised by `erhe_graphics_gpu_tests`
(headless Vulkan / lavapipe in CI, also runnable on OpenGL). Each row maps to one
or more `TEST_F(Gpu_test, ...)` cases. `[x]` = covered, `[ ]` = gap.

## Device / infrastructure

- [x] Device creation and info query (`test_device.cpp`)
- [x] Color attachment clear (`test_clear.cpp`)

## Rasterization / draw

- [x] Triangle raster, fullscreen (`test_m3_triangle.cpp`)
- [x] Raster state: cull mode + color write mask (`test_raster_state.cpp`)
- [x] Depth test less/greater (`test_m5_depth.cpp`)
- [x] Color blend (`test_m5_blend.cpp`)
- [x] Multiple render targets / MRT (`test_mrt.cpp`)
- [x] Indexed vertex-buffer draw (`test_vertex_index.cpp`)
- [x] Instanced draw, triangle_strip topology (`test_instanced.cpp`)
- [x] Primitive topology: point_list and line_list (`test_topology.cpp`)
- [x] Stencil: two-draw mask/test in a single render pass (`test_stencil.cpp`)

## Compute

- [x] Compute writing an SSBO, 1D dispatch (`test_m4_compute_ssbo.cpp`)
- [x] Compute reading a uniform buffer (`test_m4_compute_ubo.cpp`)
- [x] Compute 2D dispatch, group counts > 1 per dim over non-multiple extents (`test_compute_2d.cpp`)

## Buffers / transfer

- [x] Buffer upload + copy + fill (`test_buffer_transfer.cpp`)
- [x] Texture upload roundtrip + constant clear (`test_texture_upload.cpp`)
- [x] Texture sampling, nearest filter (`test_texture_sample.cpp`)

## Known gaps (not yet covered)

- [ ] copy_from_buffer (buffer -> texture) and copy_from_texture (texture -> texture) on transfer-only textures
- [ ] Mipmap generation / sampling across levels
- [ ] Multisample (MSAA) render + resolve
- [ ] Texture array / 3D texture sampling
