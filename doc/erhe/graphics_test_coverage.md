# erhe::graphics GPU test coverage

Stability: stable

This matrix tracks real-GPU coverage exercised by `erhe_graphics_gpu_tests`. The
target builds and runs on headless Vulkan (107 passed) and on non-headless
OpenGL (118 passed + 1 capability skip, no failures); Metal builds but has not
been run there (see
[`graphics_test_nonheadless_port.md`](graphics_test_nonheadless_port.md)). Each
row maps to one or more `TEST_F` cases on `Gpu_test` or a file-local
fixture derived from it.
`[x]` = covered, `[ ]` = gap, `[-]` = not testable on this device (a device/engine
limitation, not a coverage gap to fill).

## Device / infrastructure

- [x] Device creation and info query (`test_m1_device_up.cpp`)
- [x] Device capability audit: depth/stencil format selection, format-properties vs probe agreement, Device_info self-consistency (`test_device_caps.cpp`)
- [x] Color attachment clear (`test_m2_clear_color.cpp`)

## Rasterization / draw

- [x] Triangle raster, fullscreen (`test_m3_triangle.cpp`)
- [x] Triangle with interpolated per-vertex color (`test_m3_triangle.cpp`, `Triangle_region_test.draw_triangle`)
- [x] Viewport rectangle: bottom-right quadrant, rest untouched (`test_m3_triangle.cpp`, `Triangle_region_test.viewport`)
- [x] Scissor rectangle: off-centre 64x64 of 128x128, exact rectangle written (`test_m3_triangle.cpp`, `Triangle_region_test.scissor_rect`)
- [x] Raster state: cull mode + color write mask (`test_raster_state.cpp`)
- [x] Cull mode none / back / front and front face ccw / cw over two oppositely wound triangles (`test_raster_state.cpp`, `Raster_state_test`)
- [x] Fragment discard against a threshold read from a uniform block (`test_raster_state.cpp`, `Raster_state_test.fragment_discard`)
- [x] Depth test less/greater (`test_m5_depth.cpp`)
- [x] Depth compare ops, all eight, against a floor at depth 0.5 with columns at 0.25 / 0.5 / 0.75; depth clear value; depth write enable on / off (`test_depth_compare.cpp`)
- [x] Depth clamp disabled / enabled: out-of-range columns clipped, or drawn with depth clamped to 0 and 1 (read back); the enabled case skips without `Device_info::use_depth_clamp` (`test_depth_clamp.cpp`)
- [x] Color blend, straight-alpha over (`test_m5_blend.cpp`)
- [x] Color blend, premultiplied-alpha over (`test_blend_premultiplied.cpp`)
- [x] Blend factors zero, one, src_color, one_minus_src_color, dst_color, one_minus_dst_color, src_alpha, one_minus_src_alpha, dst_alpha, one_minus_dst_alpha, constant_color, constant_alpha (source factor, dst factor one, add) and the canonical src_alpha / one_minus_src_alpha blend, over three destination columns of distinct color and alpha plus the cleared background (`test_blend_factors.cpp`, `Blend_factor_test`)
- [x] Blend equations add, subtract, reverse_subtract, min, max with factors one / one over the same destinations (`test_blend_ops.cpp`, `Blend_op_test`)
- [x] Multiple render targets / MRT (`test_mrt.cpp`)
- [x] Indexed vertex-buffer draw (`test_vertex_index.cpp`)
- [x] Instanced draw, triangle_strip topology (`test_instanced.cpp`)
- [x] Primitive topology: point_list and line_list (`test_topology.cpp`)
- [x] Six single-texel points, exact lit count and positions; line_list from a vertex buffer, non-indexed and indexed (uint16), rasterizing identically (`test_topology.cpp`, `Topology_test`)
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

## Golden assertions

`Gpu_test` (`src/erhe/graphics/test/gpu_test_fixture.hpp`) compares a test's
output against a committed golden in `src/erhe/graphics/test/golden/`:

- `expect_buffer_matches_golden(name, bytes)` compares byte-exact against
  `<name>.bin`. A mismatch reports the output and golden sizes, the total
  count of differing bytes and the first differing offsets (up to 4096 are
  recorded).
- `expect_image_matches_golden(name, width, height, format, bytes,
  threshold)` compares against `<name>.png` (`format_8_vec4_unorm`, LDR-FLIP)
  or `<name>.pfm` (`format_32_vec4_float`, HDR-FLIP) with NVIDIA FLIP
  (`NVlabs/flip`, single header, compiled into `erhe_gpu_test_support` only).
  Alpha is dropped. The assertion fails on a size mismatch or a mean FLIP
  error above `threshold` (default 0.05); the mean and maximum error are
  recorded. `bytes` are the read-back texels of a rendered target; the helper
  orders the rows top-down from the device's texture origin, so one golden
  serves every backend. Without a PNG writer (`ERHE_USE_FPNG=OFF`) the image
  helper skips the test with that reason.

Goldens complement the analytic assertions a test makes (specific texel
values, region equality, lit-pixel counts), they never replace them: a golden
records whatever the backend produced when it was written, so only an
analytic check catches a result that has always been wrong.

**Updating goldens.** Run the test binary with the environment variable
`ERHE_GPU_TEST_UPDATE_GOLDENS=1`. The helpers then write the golden into the
source tree instead of comparing (the golden directory is compiled in, so the
working directory does not matter) and the test passes with an "updated
golden" note. Review the new images in the report before committing them with
the change that caused them.

**Results and artifacts.** Every run writes `results.json` into the results
directory: `gpu_test_results/` under the working directory, or
`ERHE_GPU_TEST_RESULTS_DIR` when set. It holds the device, backend, timestamp,
summary counts and one entry per test (name, status, duration, message, and
for each golden assertion its artifact paths, the FLIP mean and maximum error
and the threshold, or the buffer sizes, differing offsets and total count).
Each golden assertion writes `output.*`, `golden.*` and, for images, the
magma-colored FLIP error map `flip.png` to
`artifacts/<suite>.<test>/<golden name>/`. The run empties `artifacts/` when
it starts. A GoogleTest event listener installed by `gpu_test_main.cpp` is the
only writer of `results.json`; the helpers fill a per-test record.

**Report.** `py -3 scripts/gpu_test_report.py <results dir>` renders
`results.json` into a self-contained `report.html` next to it (images are
embedded, no HTTP server): device, backend and counts, a status filter, an
output / golden / FLIP error map triptych with the numbers per image golden,
and a hex view of the differing bytes per buffer golden.

Golden-asserting tests: `msaa_color_resolve` (`msaa_color_resolve.png`),
`Texgen_render_test.uv_gradient` (`texgen_uv_gradient.png`), and the
rasterization and depth state ports of the agfx suite, each against the
golden named in its test: `Triangle_region_test` (3), `Raster_state_test`
(6), `Depth_compare_test` (11), `Depth_clamp_test` (2), `Topology_test` (3),
and the blending ports: `Blend_factor_test` (13), `Blend_op_test` (5).

## Known gaps (not yet covered)

None remaining.

## CI

`.github/workflows/build.yml` builds `erhe_graphics_gpu_tests` on every
matrix entry that supports it and runs ctest with `--label-exclude
"gpu|editor"`: the deviceless `erhe_graphics_tests` runs there, the GPU
target does not (the runners have no GPU; the target carries the ctest label
`gpu`). The GPU coverage above is exercised on developer machines.

## Future work

- [Graphics tests](../plans/graphics_tests.md) - GPU tests in CI under a software
  Vulkan, and running the suite on Metal.
- [Port agfx GPU tests](../plans/graphics_tests_agfx_port.md) - golden buffer
  and image assertions with an HTML report, and about 107 single-feature
  tests re-authored from the agfx suite.
