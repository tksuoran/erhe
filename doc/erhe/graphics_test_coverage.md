# erhe::graphics GPU test coverage

Stability: stable

This matrix tracks real-GPU coverage exercised by `erhe_graphics_gpu_tests`. The
target builds and runs on headless Vulkan (176 passed) and on non-headless
OpenGL (170 passed + 12 capability skips, `snorm_color_render_readback` and
the 11 ray query tests, + 6 comparison-sampler failures from a driver defect,
see "Known gaps"); Metal builds but has not
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
- [x] MRT read back by compute: two attachments written with different ramps, then sampled in a compute pass (combined image samplers in the compute layout, `texelFetch`, attachments transitioned to shader_read_only_optimal) that writes the per-texel sum into an SSBO, checked against the analytic sum (`test_mrt.cpp`, `Gpu_test.multiple_render_targets_compute_sum`)
- [x] Indexed vertex-buffer draw (`test_vertex_index.cpp`)
- [x] Instanced draw, triangle_strip topology (`test_instanced.cpp`)
- [x] Indirect indexed draw parameters: one `Draw_indexed_primitives_indirect_command` through `multi_draw_indexed_primitives_indirect` with non-zero `first_index`, `base_vertex` and `base_instance`, per-instance vertex attributes (`Vertex_step::Step_per_instance`) placing and coloring each instance; the columns drawn and their colors asserted analytically; skips without `Device_info::use_base_instance` (`test_instanced.cpp`, `Gpu_test.draw_parameters_indirect`)
- [x] Indexed draw with uint32 and uint16 index buffers, vertices pulled from an SSBO by `gl_VertexID`: a four-color quad checked per texel against the analytic barycentric interpolation; both formats share one golden (`test_vertex_index.cpp`, `Pulled_quad_test`)
- [x] GPU-written indirect draws: a compute shader writes one or three `Draw_indexed_primitives_indirect_command`s into an `indirect | storage` buffer, a `Memory_barrier_mask::command_barrier_bit` barrier orders them before `multi_draw_indexed_primitives_indirect` (CPU-side draw count); `ERHE_DRAW_ID` places and colors each draw's column, and non-zero `first_index` / `base_vertex` select each command's quad; the non-indexed variants go through an identity index buffer and share the indexed variants' goldens (`test_draw_indirect.cpp`, `Draw_indirect_test`)
- [x] Primitive topology: point_list and line_list (`test_topology.cpp`)
- [x] Six single-texel points, exact lit count and positions; line_list from a vertex buffer, non-indexed and indexed (uint16), rasterizing identically (`test_topology.cpp`, `Topology_test`)
- [x] Stencil: two-draw mask/test in a single render pass (`test_stencil.cpp`)
- [-] Polygon mode line/point - NOT testable here: lavapipe (the headless CI device) has `fillModeNonSolid` disabled, so `VK_POLYGON_MODE_LINE`/`VK_POLYGON_MODE_POINT` fail pipeline creation. This is an engine/device limitation, not a coverage gap to fill; no engine feature should be added solely to test it.

## Render pass

- [x] Load_action::Load preserves prior pass across two passes (`test_load_action.cpp`)
- [x] Load_action Clear / Dont_care / Load over a seeded attachment: corners equal the clear color, a full-coverage draw, or the seed byte for byte (`test_load_action.cpp`, `Pass_action_test`)
- [x] Render into one subresource (`texture_level` / `texture_layer`): level 1 of a two-level 2D texture, layer 2 of a 2D array, face 3 (-Y) of a cube map, with Clear or with Load plus a draw; every other level / layer / face reads back byte-exact equal to its seed (`test_render_target_subresource.cpp`, `Render_target_subresource_test`)
- [x] Multisample (4x MSAA) color render + average resolve to single-sample target (`test_msaa_resolve.cpp`)

## Compute

- [x] Compute writing an SSBO, 1D dispatch (`test_m4_compute_ssbo.cpp`)
- [x] Compute reading a uniform buffer (`test_compute_ubo.cpp`)
- [x] Compute 2D dispatch, group counts > 1 per dim over non-multiple extents (`test_compute_2d.cpp`)
- [x] User struct (`add_struct`) in a UBO interface block, `struct_types` wiring + std140 member offsets (`test_struct_types.cpp`)
- [x] SSBO atomics: 128 invocations apply atomicAdd / And / Or / Xor / Min / Max with order-independent operands and one atomicCompSwap only a single invocation can win; every member and the per-invocation winner flags checked against a CPU model (`test_compute_atomics.cpp`)
- [x] `shared` memory: four workgroups reverse their 64 values through a shared array, then sum them with a barriered log-step reduction (`test_compute_shared.cpp`)
- [x] Four dependent dispatches over one SSBO with `memory_barrier(shader_storage_barrier_bit)` between them, the pass index from a uniform block bound at a per-pass offset; the texture variant builds an RGBA8 image in the SSBO and copies it into a 64x64 texture (`copy_from_buffer` after `pixel_buffer_barrier_bit`) (`test_compute_multi_dispatch.cpp`, `Multi_dispatch_test`)
- [x] Storage images (`set_storage_image`, `format_32_vec4_float`, `Image_layout::general`): 16 buffer -> image -> buffer round trips adding 1 per step with `memory_barrier` between the dispatches (buffer ends at 32, texels at 31); a compute-written HDR image with values in [1, 9] read back exactly. Runs on Vulkan and OpenGL; the round trip skips on devices with `Device_info::workaround_no_compute_storage_image_read` (`test_storage_image.cpp`, `Storage_image_test`)

## Buffers / transfer

- [x] Buffer upload + copy + fill (`test_buffer_transfer.cpp`)
- [x] Buffer bindings, each written by one dispatch and read by the next: a raw `uint[]` SSBO (reads cross invocations of the writer), an SSBO array of a 16-byte struct (`add_struct`, `struct_types`), and a uniform block with a vec4 after three uints, written through a `uint[]` storage view of the same buffer at the word offsets `Shader_resource` reports and read as the uniform block after `uniform_barrier_bit` (`test_buffer_bindings.cpp`, `Buffer_binding_test`)
- [x] Buffer -> buffer copy with zero offsets over the whole buffer and with non-zero source / destination offsets into a `fill_buffer`-prefilled buffer; bytes outside the copied range keep the fill (`test_buffer_transfer.cpp`, `Gpu_test.copy_buffer_to_buffer`)
- [x] Texture upload roundtrip + constant clear (`test_texture_upload.cpp`)
- [x] Texture sampling, nearest filter; full image: a 16x8 texture magnified to 64x64, every output texel exact (`test_texture_sample.cpp`, `Gpu_test.texture_sample_2d_image`)
- [x] Sampler linear filter, two-texel midpoint interpolation (`test_sampler_modes.cpp`)
- [x] Sampler address modes: clamp_to_edge / repeat / mirrored_repeat (`test_sampler_modes.cpp`)
- [x] Sampler filters and address modes as full images: a 16x16 source magnified 4x with nearest (exact) and linear (CPU bilinear reference, +-2) filtering; a 4x4 source over uv [-1, 2] with repeat, mirrored_repeat and clamp_to_edge, every output texel exact (`test_sampler_modes.cpp`, `Sampler_mode_test`)
- [x] Texture gather: `textureGather` of the red component at every four-texel corner of an 8x8 texture, borders clamped, in the GLSL x / y / z / w footprint order (`test_texture_gather.cpp`)
- [x] Comparison samplers: a format_d32_sfloat texture cleared to 0.5 sampled through a `sampler2DShadow` binding (`sampler_aspect` depth, `compare_enable`, immutable sampler) at references 0.25 / 0.5 / 0.75, for all eight compare operations (`test_sampler_comparison.cpp`, `Sampler_comparison_test`)
- [x] texelFetch from 2D, 2D array and 3D textures with mirrored x, swizzled channels and reversed layer / slice order (`test_texel_fetch.cpp`, `Texel_fetch_test`)
- [x] copy_from_buffer (buffer -> texture) (`test_copy.cpp`)
- [x] Buffer -> texture and texture -> buffer copies into / out of one subresource: a 64x64 2D texture, level 1 of a two-level 128x128 texture, layer 2 of a four-layer 64x64 array. Every subresource is seeded by a whole-surface copy and read back byte-exact; region copies use non-zero texture origins, a non-zero buffer offset and padded `bytes_per_row`; the texture -> buffer padding and leading / trailing bytes keep the destination prefill; every other subresource reads back equal to its seed (`test_copy.cpp`, `Copy_test`)
- [x] copy_from_texture (texture -> texture): whole-image copy (byte-exact roundtrip) and a non-zero-origin sub-rect copy with correct placement; source filled via copy_from_buffer, destination read back. Validates the engine fix that reads/restores the source's tracked layout and updates the destination's tracked layout (`test_copy_texture.cpp`)
- [x] Texture -> texture region copies at non-zero source and destination origins over a pre-seeded destination: 2D 64x64, level 1 to level 1 of two-level 128x128 textures, layer 1 to layer 2 of four-layer 64x64 arrays; the destination outside the region and every other subresource of both textures read back equal to their seeds (`test_copy_texture.cpp`, `Texture_copy_test`)
- [x] Depth-texture readback: depth-only pass writes a known NDC depth, depth aspect copied to host (`test_depth_readback.cpp`)
- [x] Depth texture sampled as a float: a triangle at depth 0.25 over a 1.0 clear, the depth aspect sampled through a plain `sampler2D` into format_32_vec4_float; exact corner and centre values (`test_depth_readback.cpp`, `Gpu_test.depth_texture_sample_float`)
- [x] Mipmap generation: generate_mipmaps linear-downsamples level 0 (half-black/half-white split averages to mid-gray at the 1x1 level; the 4x4 level keeps the vertical split) (`test_mipmaps.cpp`)
- [x] 2D array texture sampling: texture_2d_array with 3 distinct per-layer solid colors filled via copy_from_buffer destination_slice, each layer sampled through a sampler2DArray (layer via GLSL define) and verified; full image: four 16x16 patterned layers seeded by copy_from_buffer, shown as 2x2 tiles, every output texel exact (`test_texture_array.cpp`, `Gpu_test.texture_2d_array_sample_image`)
- [x] 3D texture sampling: 2x2x2 texture_3d filled in one copy_from_buffer, each voxel center sampled through a sampler3D (nearest) and verified against its distinct color; full image: a 16x16x4 volume, each z-slice sampled at its centre as one of 2x2 tiles, every output texel exact (`test_texture_3d.cpp`, `Gpu_test.texture_3d_sample_image`)
- [x] Cube map (texture_cube_map) sampling: 6-face CUBE-compatible image (1x1 per face) with a distinct per-face color filled via copy_from_buffer destination_slice (Vulkan face order +X,-X,+Y,-Y,+Z,-Z), each face sampled through a samplerCube by its center direction vector (baked as a GLSL define) and verified; set_sampled_image builds a VK_IMAGE_VIEW_TYPE_CUBE view spanning all 6 layers; full image: six patterned 8x8 faces as 3x2 tiles, each tile covering its face's (s, t) square through the cube face selection table, every output texel exact (`test_texture_cube.cpp`, `Gpu_test.texture_cube_sample_image`)

## Color formats

- [x] float32 color render + readback: out-of-[0,1] values into format_32_vec4_float survive unclamped (`test_float32_render.cpp`)
- [x] snorm color render + readback: signed-normalized values into format_8_vec4_snorm decode to the written signs (`test_snorm_render.cpp`)

## Ray query

Every test here skips with "ray query not supported by this device" unless
`Device_info::use_ray_query` (never on OpenGL). `Ray_query_test`
(`gpu_test_fixture.hpp`) builds float3 triangle geometry into bottom and top
level `Acceleration_structure`s in the same command buffer as the trace
(`build()` ends with the build -> ray query barrier) and traces one ray per
texel of a 64x64 orthographic grid down -Z in a compute shader
(`GL_EXT_ray_query`, `Binding_type::acceleration_structure`,
`set_acceleration_structure`). The shader writes RGBA8-packed texels into an
SSBO in image rows, which is copied into a 64x64 texture for the image golden;
the ray query backends have a top-left texture origin, so no row conversion is
needed. A CPU model of the same rays checks every texel: exact for coverage
and ids, +-1/255 for the encoded hit distance and barycentrics; texels within
1e-4 of a triangle edge are not compared. On Vulkan a buffer with
`Buffer_usage::acceleration_structure_build_input` is allocated 16-byte
aligned, as instance and transform data require.

- [x] Bottom level structure with one triangle geometry, and with three overlapping geometries at distinct depths: silhouette, hit distance, `rayQueryGetIntersectionGeometryIndexEXT` (`test_ray_query.cpp`, `Ray_query_silhouette_test`)
- [x] Top level structure with one translated instance, with two bottom level structures in three instances (one rotated, tilted and scaled), and with one bottom level structure referenced by three overlapping instances: `rayQueryGetIntersectionInstanceIdEXT`, nearest hit across instances (`test_ray_query.cpp`, `Ray_query_silhouette_test`)
- [x] Intersection attributes: `rayQueryGetIntersectionBarycentricsEXT`, `rayQueryGetIntersectionPrimitiveIndexEXT` over a four-triangle geometry, `rayQueryGetIntersectionInstanceCustomIndexEXT` for `instance_custom_index` 3 / 5 / 6 (`test_ray_query_attributes.cpp`, `Ray_query_attribute_test`)
- [x] Geometry opacity: with `Acceleration_structure_triangles::opaque` the candidate loop sees no candidates; the hits of a non-opaque geometry arrive as `gl_RayQueryCandidateIntersectionTriangleEXT`, confirmed with `rayQueryConfirmIntersectionEXT` in one half of the image and ignored in the other, where the ray reaches the opaque geometry behind (`test_ray_query_attributes.cpp`, `Ray_query_attribute_test.opaque_geometry`, `non_opaque_candidate`)
- [x] Instance mask: instances with `mask` 0x01 and 0x02 traced with ray cull masks 0x01, 0x02 and 0x03 (`test_ray_query_attributes.cpp`, `Ray_query_attribute_test.instance_mask`)
- [ ] Per-instance force-opaque / force-non-opaque flags, AABB geometry, acceleration structure update / compaction / copy: not in the `Acceleration_structure` API

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
the blending ports: `Blend_factor_test` (13), `Blend_op_test` (5), and the
render pass action and subresource target ports: `Pass_action_test` (3),
`Render_target_subresource_test` (6), the copy ports:
`Gpu_test.copy_buffer_to_buffer` (2 buffer goldens), `Copy_test` (3 image,
3 buffer goldens), `Texture_copy_test` (3), and the sampling ports:
`Sampler_mode_test` (5), `Sampler_comparison_test` (8), `Texel_fetch_test`
(3), `Gpu_test.texture_sample_2d_image`, `texture_2d_array_sample_image`,
`texture_3d_sample_image`, `texture_cube_sample_image`,
`texture_gather_red_corners`, `draw_parameters_indirect` and
`depth_texture_sample_float` (`.pfm`, HDR-FLIP), and the compute and buffer
ports: `Gpu_test.compute_buffer_atomics`, `compute_shared_memory`,
`multiple_render_targets_compute_sum`, `Multi_dispatch_test` (1 buffer, 1
image golden), `Buffer_binding_test` (3 buffer goldens) and
`Storage_image_test` (1 buffer golden, 1 `.pfm` golden), and the indirect
draw ports: `Pulled_quad_test` (2 tests, 1 shared golden) and
`Draw_indirect_test` (4 tests, 2 goldens shared by the indexed and
identity-index variants), and the ray query ports: `Ray_query_silhouette_test`
(5) and `Ray_query_attribute_test` (6 tests, 8 goldens; `instance_mask`
asserts one per ray cull mask). The texture
-> buffer tests order the payload rows top-down from `texture_origin` before
the buffer compare, as the image helper does for images, so their buffer
goldens are shared by every backend too.

**Sampling passes.** `Gpu_test::render_fullscreen_pass` renders one
fullscreen triangle whose fragment shader samples the images bound through a
bind group layout into a fresh color target; the sampling ports write their
whole output image this way rather than through a compute storage image. The
shader gets `IMAGE_POSITION`, the pixel position in image space (row 0 =
image top, the goldens' row order) derived from `gl_FragCoord` and
`texture_origin`. A test that addresses a seeded texture by `IMAGE_POSITION`
reads texel row r for image row r on every backend, so its CPU model is the
shader's own arithmetic and one golden serves all backends; a test that
samples a texture it rendered itself addresses it by `gl_FragCoord` (memory
rows), which reads the texel rendered at that pixel. `make_sampled_texture`,
`find_depth32f_format`, `memory_rows_to_image_rows` and `expect_rgba8_near`
are the helpers the sampling ports share.

**Compute passes.** `Gpu_test::make_compute_program` builds a compute shader
and its pipeline against a bind group layout (`Compute_program`). A compute
test that produces an image in memory-row order (an SSBO copied into a
texture, a storage image) computes its content from the image row, not the
storage row, through an `IMAGE_ROW(y)` define derived from `texture_origin`,
so one golden serves every backend.

## Known gaps (not yet covered)

- Comparison samplers on OpenGL (seen with the AMD Radeon 890M driver,
  "4.6.0 Core Profile Context 26.8.1.260810"): once
  `Sampler_comparison_test.never` has drawn, the later cases, which compile
  the same fragment shader source, return 0 for every reference: `less`,
  `equal`, `less_or_equal`, `greater`, `not_equal` and `greater_or_equal`
  read as `never` (`less` passed in one of several runs; `always` passes).
  Each passes run alone or after any case other than `never`, and all pass
  when the fragment source differs per case. The GL state queried just before
  the failing draws is the requested one: the program's `s_shadow` uniform
  is unit 0, unit 0 has the depth texture and the case's sampler bound, and
  that sampler reads back `GL_COMPARE_REF_TO_TEXTURE` with the case's compare
  function. Removing the immutable sampler changes nothing. erhe hands the
  driver the right state, so the cases are left failing there rather than
  worked around in the test.

## CI

`.github/workflows/build.yml` builds `erhe_graphics_gpu_tests` on every
matrix entry that supports it and runs ctest with `--label-exclude
"gpu|editor"`: the deviceless `erhe_graphics_tests` runs there, the GPU
target does not (the runners have no GPU; the target carries the ctest label
`gpu`). The GPU coverage above is exercised on developer machines.

## Future work

- [Graphics tests](../plans/graphics_tests.md) - GPU tests in CI under a software
  Vulkan, and running the suite on Metal.
- [Port agfx GPU tests](../plans/graphics_tests_agfx_port.md) - the agfx
  tests that wait on an erhe feature, and the engine findings the port left
  open.
